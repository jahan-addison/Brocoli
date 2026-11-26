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

#pragma once

#include <array>             // for array
#include <credence/map.h>    // for Ordered_Map
#include <credence/symbol.h> // for Symbol_Table
#include <credence/types.h>  // for Parameters, Locals, Labels, Typed_RValue
#include <credence/util.h>   // for contains, range_contains
#include <cstddef>           // for size_t
#include <deque>             // for deque
#include <fmt/compile.h>     // for operator""_cf
#include <fmt/format.h>      // for format
#include <optional>          // for optional
#include <string>            // for basic_string, string
#include <utility>           // for pair

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

using type::Labels;
using type::Locals;
using type::Parameters;
using type::Pointers;
using type::RValues;
using type::semantic::Address;
using type::semantic::Label;
using type::semantic::LValue;
using type::semantic::RValue;
using type::semantic::Size;
using type::semantic::Type;
using Stack = std::deque<type::semantic::RValue>;

constexpr bool is_vector_lvalue(LValue const& lvalue)
{
    return util::contains(lvalue, "[") and util::contains(lvalue, "]");
}

/**
 * @brief The record of a function frame in the object table, defined by the
 * HIR and then bound by the IR for the backends to read
 *
 * Note: the state a stage keeps for itself is in a side table of that stage
 * keyed by the function, such as the label addresses of ir::Table or the
 * pointer slots of each backend's stack, so the record only holds what more
 * than one stage reads
 */
class Function
{
  public:
    using Return_RValue = std::optional<std::pair<RValue, RValue>>;
    using Union_Table = Ordered_Map<LValue, Label>;

  public:
    static constexpr int max_depth = 999;

  public:
    explicit Function(Label const& label)
        : symbol(label)
    {
    }
    // move only
    Function(Function const&) = delete;
    Function& operator=(Function const&) = delete;
    Function(Function&&) = default;
    Function& operator=(Function&&) = default;

  public:
    void set_parameters_from_symbolic_label(Label const& label);

  public:
    constexpr bool is_pointer_parameter(RValue const& parameter)
    {
        using namespace fmt::literals;
        return util::range_contains(
            fmt::format("*{}"_cf, parameter), parameters);
    }
    constexpr bool is_scaler_parameter(RValue const& parameter)
    {
        return util::range_contains(
            type::from_lvalue_offset(parameter), parameters);
    }

    constexpr bool is_pointer_in_stack_frame(RValue const& rvalue)
    {
        return locals.is_pointer(rvalue) or is_pointer_parameter(rvalue);
    }

    constexpr bool is_parameter(RValue const& parameter)
    {
        return is_scaler_parameter(parameter) or
               is_pointer_parameter(parameter);
    }

    constexpr int get_index_of_parameter(RValue const& parameter)
    {
        for (std::size_t i = 0; i < parameters.size(); i++) {
            if (type::get_unary_rvalue_reference(parameters[i]) ==
                type::from_lvalue_offset(parameter))
                return i;
        }
        return -1;
    }

  public:
    // defined by the HIR, where the IR binds the value of each local
    Label symbol{};
    Parameters parameters{};
    Labels labels{};
    Locals locals{};
    Union_Table union_table{};

  public:
    // bound by the IR, and read by the backends
    Ordered_Map<LValue, RValue> temporary{};
    Return_RValue ret{};
    std::array<Address, 2> address_location{};
};

/**
 * @brief Whether an lvalue is a local of a stack frame, and not a vector
 */
[[nodiscard]] constexpr bool local_contains(LValue const& lvalue,
    Function& stack_frame)
{
    return stack_frame.locals.is_defined(lvalue) and
           not is_vector_lvalue(lvalue);
}

/**
 * @brief Whether an rvalue is a pointer, an address-of, or a string literal
 */
[[nodiscard]] constexpr bool is_pointer(RValue const& rvalue,
    Function& stack_frame)
{
    return stack_frame.locals.is_pointer(rvalue) or rvalue.starts_with("&") or
           type::is_typed_rvalue_string(rvalue);
}

/**
 * @brief Resolve a temporary to the rvalue or size it holds in a stack frame
 */
RValue lvalue_at_temporary_object_address(LValue const& lvalue,
    Function& stack_frame);
Size lvalue_size_at_temporary_object_address(LValue const& lvalue,
    Function& stack_frame);
Size get_size_of_temporary_binary_rvalue(RValue const& rvalue,
    Function& stack_frame);

} // namespace credence::object
