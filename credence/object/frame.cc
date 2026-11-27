/*****************************************************************************
 * Copyright (c) Jahan Addison
 *
 * This software is dual-licensed under the Apache License, Version 2.0 or
 * the GNU General Public License, Version 3.0 or later.
 *
 * You may use this work, in part or in whole, under the terms of either
 * license.
 *
 * See the LICENSE.Apache-v2 and LICENSE.GPL-v3 files in the project root
 * for the full text of these licenses.
 ****************************************************************************/

#include "frame.h"

#include <credence/error.h>  // for credence_error, credence_assert_nequal
#include <credence/symbol.h> // for Symbol_Table
#include <credence/types.h>  // for get_size_from_typed_rvalue, is_temporary
#include <credence/util.h>   // for contains
#include <fmt/format.h>      // for format
#include <string>            // for basic_string, string
#include <string_view>       // for basic_string_view, string_view

/****************************************************************************
 * Function Frame
 *
 * The record of a function in the object table, where its parameters,
 * labels, and locals are defined by the HIR and then bound by the IR, and the
 * queries of a single frame, such as whether an rvalue is a pointer or what a
 * temporary holds.
 *
 *   add(x, y) {
 *     auto z;
 *     z = x + y;
 *     return(z);
 *   }
 *
 * Becomes:
 *
 *  * a Function "add", with the parameters x and y, and z in its locals
 *
 *  * the temporaries of `x + y` in its temporary table, and z as its return
 *    value
 *
 ****************************************************************************/

namespace credence::object {

/**
 * @brief Parse ITA function parameters into locals on the frame stack
 *
 * e.g. `__convert(s,v,*k) = (s,v,*k)`
 */
void Function::set_parameters_from_symbolic_label(Label const& label)
{
    auto open = label.find_first_of('(');
    auto close = label.find_first_of(')');
    auto search = std::string_view{ label }.substr(open + 1, close - open - 1);

    if (!search.empty()) {
        std::string parameter;
        for (auto it = search.begin(); it <= search.end(); it++) {
            auto slice = *it;
            if (slice != ',' and it != search.end())
                parameter += slice;
            else {
                parameters.emplace_back(parameter);
                parameter = "";
            }
        }
    }
}

/**
 * @brief Resolve the rvalue at a temporary storage address in the object table
 */
RValue lvalue_at_temporary_object_address(LValue const& lvalue,
    Function const& stack_frame)
{
    auto is_temporary =
        type::is_temporary(lvalue) or util::contains(lvalue, "_p");
    auto rvalue = not is_temporary ? lvalue
                  : stack_frame.temporary.contains(lvalue)
                      ? stack_frame.temporary.at(lvalue)
                      : RValue{};
    if (util::contains(rvalue, "_t") and util::contains(rvalue, " ")) {
        return rvalue;
    } else {
        if (util::contains(rvalue, "_t") or util::contains(lvalue, "_p"))
            return lvalue_at_temporary_object_address(rvalue, stack_frame);
        else
            return rvalue;
    }
}

namespace {

/**
 * @brief Resolve a binary expression's size in a stack frame
 *
 * Note: the size is from the first operand that has one, the left and then
 * the right, since both sides are assumed to be the same size - see
 * `mixed_operand_sizes` in docs/known_bugs.md
 */
Size lvalue_size_at_temporary_binary_expression(RValue const& rvalue,
    Function const& stack_frame)
{
    if (type::is_binary_expression(rvalue)) {
        auto const& locals = stack_frame.locals;
        auto [left, right, op] = type::from_rvalue_binary_expression(rvalue);
        // a literal operand with a size, the left side and then the right
        if (type::is_typed_rvalue(left) and
            not type::is_typed_rvalue_word(left))
            return type::get_size_from_typed_rvalue(left);
        if (type::is_typed_rvalue(right) and
            not type::is_typed_rvalue_word(right))
            return type::get_size_from_typed_rvalue(right);

        // a local defined in the stack frame, the left side and then the right
        if (locals.is_defined(left) and not locals.is_pointer(left)) {
            auto typed_rvalue = locals.get_symbol_by_name(left);
            if (type::is_typed_rvalue_word(typed_rvalue))
                return lvalue_size_at_temporary_object_address(
                    type::get_value_from_typed_rvalue(typed_rvalue),
                    stack_frame);
            else
                return type::get_size_from_typed_rvalue(
                    locals.get_symbol_by_name(left));
        }
        if (locals.is_defined(right) and not locals.is_pointer(right)) {
            auto typed_rvalue = locals.get_symbol_by_name(right);
            if (type::is_typed_rvalue_word(typed_rvalue))
                return lvalue_size_at_temporary_object_address(
                    type::get_value_from_typed_rvalue(typed_rvalue),
                    stack_frame);
            else
                return type::get_size_from_typed_rvalue(
                    locals.get_symbol_by_name(right));
        }
    }
    return 0UL;
}

} // namespace

/**
 * @brief Resolve the size at a temporary storage address in the object table
 *
 * Note: a binary expression is sized by one of its operands, see
 * lvalue_size_at_temporary_binary_expression
 */
Size lvalue_size_at_temporary_object_address(LValue const& lvalue,
    Function const& stack_frame)
{
    auto rvalue = lvalue_at_temporary_object_address(lvalue, stack_frame);
    auto& locals = stack_frame.locals;

    if (type::is_typed_rvalue(rvalue) and
        not type::is_typed_rvalue_word(rvalue))
        return type::get_size_from_typed_rvalue(rvalue);
    if (type::is_unary_expression(rvalue))
        return lvalue_size_at_temporary_object_address(
            type::get_unary_rvalue_reference(rvalue), stack_frame);

    if (auto binary_expression_size =
            lvalue_size_at_temporary_binary_expression(rvalue, stack_frame))
        return binary_expression_size;

    // A temporary binary expression, e.g. `_t5 * 102`
    if (type::is_temporary_typed_rvalue_binary_expression(rvalue)) {
        auto [left, right, op] = type::from_rvalue_binary_expression(rvalue);
        return lvalue_size_at_temporary_object_address(left, stack_frame);
    }

    if (locals.is_defined(rvalue)) {
        auto typed_rvalue = locals.get_symbol_by_name(rvalue);
        if (type::is_typed_rvalue_word(typed_rvalue))
            return lvalue_size_at_temporary_object_address(
                type::get_value_from_typed_rvalue(typed_rvalue), stack_frame);
        else
            return type::get_size_from_typed_rvalue(
                locals.get_symbol_by_name(rvalue));
    }

    if (rvalue == "RET")
        return 8UL;

    credence_error(fmt::format("undefined object: {}", rvalue));

    return 0UL;
}

Size get_size_of_temporary_binary_rvalue(RValue const& rvalue,
    Function const& stack_frame)
{
    Size size = 0UL;
    auto temp_side = type::is_temporary_operand_binary_expression(rvalue);
    auto [left, right, _] = type::from_rvalue_binary_expression(rvalue);
    if (type::is_temporary(left) and type::is_temporary(right))
        return lvalue_size_at_temporary_object_address(left, stack_frame);
    if (temp_side == "left")
        if (!type::is_typed_rvalue(right))
            size = lvalue_size_at_temporary_object_address(right, stack_frame);
        else
            size = type::get_size_from_typed_rvalue(right);
    else {
        if (!type::is_typed_rvalue(left))
            size = lvalue_size_at_temporary_object_address(left, stack_frame);
        else
            size = type::get_size_from_typed_rvalue(left);
    }
    credence_assert_nequal(size, 0UL);
    return size;
}

} // namespace credence::object
