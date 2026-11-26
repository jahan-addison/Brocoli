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

#include "table.h"

#include <algorithm>         // for max
#include <array>             // for array
#include <credence/error.h>  // for credence_assert, assert_nequal_impl
#include <credence/map.h>    // for Ordered_Map
#include <credence/symbol.h> // for Symbol_Table
#include <credence/types.h>  // for get_size_from_typed_rvalue, get_val...
#include <credence/util.h>   // for contains, range_contains, AST_Node, __s...
#include <easyjson.h>        // for JSON
#include <fmt/base.h>        // for copy
#include <fmt/compile.h>     // for format, operator""_cf
#include <fmt/format.h>      // for format
#include <matchit.h>         // for App, app, pattern, PatternHelper, Patte...
#include <numeric>           // for accumulate
#include <source_location>   // for source_location
#include <string>            // for basic_string, char_traits, operator==
#include <string_view>       // for basic_string_view, string_view
#include <tuple>             // for get, tuple
#include <utility>           // for move
#include <variant>           // for visit

/****************************************************************************
 * Object Table
 *
 * The vectors, unions, functions, and stack frames of a program, as
 * queryable lvalues and rvalues. hir::Table defines each function, vector,
 * union, and label, ir::Table binds the IR onto their frames and storage,
 * and each backend allocates and addresses memory from it, where the record of
 * each function's frame is in frame.h.
 *
 *   union value_t { integer 0, pair (0, 0) };
 *
 *   main() {
 *     auto array[10];
 *     union value_t v;
 *     array[5] = 42;
 *   }
 *
 * Becomes:
 *
 *  * a Vector "array" of size 10, where array[5] is (42:int:4)
 *
 *  * a Tagged_Union "value_t", where "integer" is (0:int:4) and "pair" is a
 *    Vector of two ints
 *
 *  * a Function "main", with "array" in its locals and "v" of "value_t" in
 *    its union table
 *
 ****************************************************************************/

namespace credence::object {

/**
 * @brief Insert a function definition owned by the object table
 */
Function& Object_Table::set_function_definition(Label const& label)
{
    auto& function = function_storage_.emplace_back(label);
    functions_.insert(label, std::ref(function));
    return function;
}

/**
 * @brief Insert a union definition owned by the object table
 */
Tagged_Union& Object_Table::set_union_definition(Label const& label)
{
    auto& tagged_union = union_storage_.emplace_back(label);
    unions_.insert(label, std::ref(tagged_union));
    return tagged_union;
}

/**
 * @brief Insert a vector definition owned by the object table
 */
Vector& Object_Table::set_vector_definition(Label const& label, Address size_of)
{
    auto& vector = vector_storage_.emplace_back(label, size_of);
    vectors_.insert(label, std::ref(vector));
    return vector;
}

/**
 * @brief Set the typed rvalue of a scalar tag in the union
 */
void Tagged_Union::set_tag_entry(Label const& label,
    credence::type::Typed_RValue const& entry)
{
    entries.insert(label, entry);
}

/**
 * @brief Set the Vector of the elements of a tuple tag, moved into and owned
 * by the union
 */
void Tagged_Union::set_tag_entry(Label const& label, Vector entry)
{
    entries.insert(label, std::move(entry));
}

/**
 * @brief Get the unpadded storage size of the largest tag in the union, which
 * along with the active tag is the storage of each union variable
 */
Size Tagged_Union::get_largest_tag_entry() const
{
    return std::accumulate(entries.begin(),
        entries.end(),
        Size{ 0 },
        [](Size largest, auto const& entry) {
            return std::max(largest,
                std::visit(
                    util::overload{ [](type::Typed_RValue const& rvalue) {
                                       return get_storage_size(rvalue);
                                   },
                        [](Vector const& vector) {
                            return vector.size_of_data();
                        } },
                    entry.second));
        });
}

/**
 * @brief Save a literal that is addressed relative to the program counter
 */
void Data_Section::insert(RValue const& rvalue)
{
    auto type = type::get_type_from_typed_rvalue(rvalue);
    if (type == "float")
        floats.insert(type::get_value_from_typed_rvalue(rvalue));
    if (type == "double")
        doubles.insert(type::get_value_from_typed_rvalue(rvalue));
    if (type == "string")
        strings.insert(type::get_value_from_typed_rvalue(rvalue));
}

void Data_Section::insert(type::Typed_RValue const& rvalue)
{
    auto type = type::get_type_from_typed_rvalue(rvalue);
    if (type == "float")
        floats.insert(type::get_value_from_typed_rvalue(rvalue));
    if (type == "double")
        doubles.insert(type::get_value_from_typed_rvalue(rvalue));
    if (type == "string")
        strings.insert(type::get_value_from_typed_rvalue(rvalue));
}

namespace detail {

type::Typed_RValue get_rvalue_at_lvalue_object_storage(LValue const& lvalue,
    Function const& stack_frame,
    Vectors const& vectors,
    std::source_location const& location = std::source_location::current());

/**
 * @brief Vector offset rvalue resolution in the stack frame and global symbols
 */
struct Vector_Offset
{
  public:
    Vector_Offset() = delete;
    explicit Vector_Offset(Function const& stack_frame, Vectors const& vectors)
        : stack_frame_(stack_frame)
        , vectors_(vectors)
    {
    }

  public:
    /**
     * @brief Check that the offset rvalue is a valid address in the vector
     */
    [[nodiscard]] bool is_valid_vector_address_offset(LValue const& lvalue)
    {
        auto lvalue_reference = type::get_unary_rvalue_reference(lvalue);
        auto address = type::from_lvalue_offset(lvalue_reference);
        // a global with no index, `counter`, is its first element
        auto offset = util::contains(lvalue_reference, "[")
                          ? type::from_decay_offset(lvalue_reference)
                          : std::string{ "0" };
        if (stack_frame_.is_parameter(offset)) {
            return true;
        }
        return vectors_.at(address).get().data.contains(
            get_rvalue_offset_of_vector(offset));
    }

    /**
     * @brief Get the rvalue at the address of an offset in memory
     */
    RValue get_rvalue_offset_of_vector(RValue const& offset)
    {
        return stack_frame_.locals.is_defined(offset)
                   ? type::get_value_from_typed_rvalue(
                         get_rvalue_at_lvalue_object_storage(
                             offset, stack_frame_, vectors_))
                   : offset;
    }

  private:
    Function const& stack_frame_;
    Vectors const& vectors_;
};

/**
 * @brief Resolve the rvalue of an element of a vector in the object table
 */
type::Typed_RValue get_rvalue_at_vector_object_storage(LValue const& lvalue,
    Function const& stack_frame,
    Vectors const& vectors,
    std::source_location const& location)
{
    auto lvalue_reference = type::get_unary_rvalue_reference(lvalue);
    auto vector_offset = Vector_Offset{ stack_frame, vectors };
    auto address = type::from_lvalue_offset(lvalue_reference);
    // a global with no index, `counter`, is its first element
    auto offset = util::contains(lvalue_reference, "[")
                      ? type::from_decay_offset(lvalue_reference)
                      : std::string{ "0" };
    auto offset_rvalue = vector_offset.get_rvalue_offset_of_vector(offset);
    if (stack_frame.is_parameter(offset)) {
        return type::Typed_RValue{ lvalue, "word", 8UL };
    }
    if (!vector_offset.is_valid_vector_address_offset(lvalue))
        throw_compiletime_error(
            fmt::format("lvalue '{}' is not a vector with offset '{}' with "
                        "and storage of '{}'",
                address,
                offset,
                offset_rvalue),
            lvalue,
            location);

    return vectors.at(address).get().data[offset_rvalue];
}

/**
 * @brief Resolve the rvalue of a pointer in the table object and stack frame
 */
type::Typed_RValue get_rvalue_at_lvalue_object_storage(LValue const& lvalue,
    Function const& stack_frame,
    Vectors const& vectors,
    std::source_location const& location)
{
    auto& locals = stack_frame.locals;
    auto lvalue_reference = type::get_unary_rvalue_reference(lvalue);

    if (lvalue_reference == "RET")
        return type::NULL_RVALUE_LITERAL;
    if (locals.is_pointer(lvalue_reference)) {
        auto address_at =
            locals.get_pointer_by_name(lvalue_reference, location);
        if (address_at == "NULL")
            return type::NULL_RVALUE_LITERAL;
        return get_rvalue_at_lvalue_object_storage(
            address_at, stack_frame, vectors, location);
    }
    if (vectors.contains(type::from_lvalue_offset(lvalue_reference)))
        return get_rvalue_at_vector_object_storage(
            lvalue, stack_frame, vectors, location);

    if (type::is_typed_rvalue(lvalue))
        return type::get_typed_rvalue_from_string(lvalue);

    return locals.get_symbol_by_name(lvalue_reference, location);
}

/**
 * @brief Get the type from a symbol in the stack frame or vector storage
 *
 * Note: vector boundaries are not checked here, see
 * Pointer_Vector_Decay_Checker::is_boundary_out_of_range
 */
Type get_type_from_object_data_type(LValue const& lvalue,
    Function const& stack_frame,
    Vectors const& vectors)
{
    auto& locals = stack_frame.locals;
    if (is_vector_lvalue(lvalue)) {
        auto address = type::from_lvalue_offset(lvalue);
        auto offset = type::from_decay_offset(lvalue);
        credence_assert(vectors.contains(address));
        return vectors.at(address).get().data[offset].type;
    }
    return locals.get_symbol_by_name(lvalue).type;
}

/**
 * @brief Set the stack frame return value in the table
 */
void set_stack_frame_return_value(RValue const& rvalue,
    Function& frame,
    Vectors const& vectors,
    Stack const& stack)
{
    auto local_contains = [&](LValue const& lvalue) {
        const auto& locals = frame.locals;
        return locals.is_defined(lvalue) and not is_vector_lvalue(lvalue);
    };
    auto is_pointer_ = [&](RValue const& value) {
        return frame.locals.is_pointer(value);
    };
    auto is_parameter = [&](RValue const& value) {
        return frame.is_parameter(value);
    };
    namespace m = matchit;
    m::match(rvalue)(
        m::pattern | m::app(is_pointer_, true) =
            [&] {
                frame.ret = std::make_pair(
                    frame.locals.get_pointer_by_name(rvalue), rvalue);
            },
        m::pattern | m::app(is_parameter, true) =
            [&] {
                if (stack.empty()) {
                    frame.ret = std::make_pair("NULL", rvalue);
                    return;
                }
                auto index_of = frame.get_index_of_parameter(rvalue);
                credence_assert_nequal(index_of, -1);
                frame.ret = std::make_pair(stack.at(index_of), rvalue);
            },
        m::pattern | m::app(local_contains, true) =
            [&] {
                auto value_at = type::get_value_from_typed_rvalue(
                    frame.locals.get_symbol_by_name(rvalue));
                frame.ret = std::make_pair(value_at, rvalue);
            },
        m::pattern | m::app(type::is_typed_rvalue, true) =
            [&] {
                auto typed_rvalue = type::get_typed_rvalue_from_string(rvalue);
                auto value_at = type::get_value_from_typed_rvalue(typed_rvalue);
                frame.ret = std::make_pair(value_at, rvalue);
            },
        m::pattern | m::app(is_vector_lvalue, true) =
            [&] {
                auto value_at =
                    get_rvalue_at_lvalue_object_storage(rvalue, frame, vectors);
                frame.ret = std::make_pair(
                    type::get_value_from_typed_rvalue(value_at), rvalue);
            },
        m::pattern | m::app(type::is_temporary, true) =
            [&] {
                frame.ret = std::make_pair(frame.temporary.at(rvalue), rvalue);
            },
        m::pattern | m::_ = [&] { credence_error("unreachable"); });
}

} // namespace detail

/**
 * @brief Resolve the rvalue of a pointer, a vector element, or a local in a
 * stack frame
 */
type::Typed_RValue Object_Table::get_rvalue_at_lvalue_object_storage(
    LValue const& lvalue,
    Function const& stack_frame,
    std::source_location const& location) const
{
    return detail::get_rvalue_at_lvalue_object_storage(
        lvalue, stack_frame, vectors_, location);
}

/**
 * @brief Get the type from a symbol in the stack frame or vector storage
 */
Type Object_Table::get_type_from_object_data_type(LValue const& lvalue,
    Function const& stack_frame) const
{
    return detail::get_type_from_object_data_type(
        lvalue, stack_frame, vectors_);
}

/**
 * @brief Set the stack frame return value in the table
 */
void Object_Table::set_stack_frame_return_value(RValue const& rvalue,
    Function& frame,
    Stack const& stack)
{
    detail::set_stack_frame_return_value(rvalue, frame, vectors_, stack);
}

} // namespace credence::object
