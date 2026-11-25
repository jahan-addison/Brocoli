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
#include <credence/error.h>  // for credence_assert
#include <credence/map.h>    // for Ordered_Map
#include <credence/symbol.h> // for Symbol_Table
#include <credence/types.h>  // for Parameters, Address, Typed_RValue, Label
#include <credence/util.h>   // for contains, AST_Node
#include <cstddef>           // for size_t
#include <deque>             // for deque
#include <functional>        // for reference_wrapper
#include <numeric>           // for accumulate
#include <optional>          // for optional
#include <source_location>   // for source_location
#include <string>            // for basic_string, string
#include <utility>           // for pair, move
#include <variant>           // for variant

/****************************************************************************
 * Object Table
 *
 * The vectors, unions, functions, and stack frames of a program, as
 * queryable lvalues and rvalues. hir::Table defines each function, vector,
 * union, and label, ir::Table binds the IR onto their frames and storage,
 * and each backend allocates and addresses memory from it.
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

constexpr Size k_pointer_size{ 8 };

/**
 * @brief Get the storage size of a typed rvalue, where a string is the size of
 * its pointer and not the length of its literal
 *
 *  Examples:
 *
 *    get_storage_size((10:int:4))         // 4
 *    get_storage_size(("hello":string:5)) // 8
 */
constexpr Size get_storage_size(type::Typed_RValue const& rvalue)
{
    return rvalue.type == "string" ? k_pointer_size : rvalue.size;
}

constexpr bool is_vector_lvalue(LValue const& lvalue)
{
    return util::contains(lvalue, "[") and util::contains(lvalue, "]");
}

/**
 * @brief Vector definition map and storage sizes for the object table
 */
class Vector
{
  public:
    using Label = credence::type::semantic::Label;
    using Address = credence::type::semantic::Address;
    using Size = credence::type::semantic::Size;
    using Entry = std::pair<Label, credence::type::Typed_RValue>;
    using Storage = Ordered_Map<Label, credence::type::Typed_RValue>;
    using Offset = Ordered_Map<Label, Address>;

    static constexpr std::size_t max_size{ 999 };

  public:
    Vector() = delete;
    explicit Vector(Label label, Address size_of)
        : size(size_of)
        , symbol(std::move(label))
    {
    }
    // move only
    Vector(Vector const&) = delete;
    Vector& operator=(Vector const&) = delete;
    Vector(Vector&&) = default;
    Vector& operator=(Vector&&) = default;

  public:
    bool operator==(Vector const&) const = default;

  public:
    constexpr void set_address_offset(Label const& index, Address address)
    {
        offset.insert(index, address);
    }
    /**
     * @brief Get the unpadded storage size of the vector data, since
     * alignment is left to each backend
     */
    [[nodiscard]] constexpr Size size_of_data() const
    {
        return std::accumulate(data.begin(),
            data.end(),
            Size{ 0 },
            [](Size size, auto const& entry) {
                return size + get_storage_size(entry.second);
            });
    }

  public:
    Storage data{};
    Offset offset{};
    int decay_index{ 0 };
    Size size{ 0 };
    Label symbol{};
};

static_assert(std::equality_comparable<Vector>);

/**
 * @brief Tagged union definition with the value of each of its tags, a typed
 * rvalue for a scalar tag or a Vector of the elements of a tuple tag
 */
class Tagged_Union
{
  public:
    using Tag_Entry = std::variant<credence::type::Typed_RValue, Vector>;
    using Entries = Ordered_Map<Label, Tag_Entry>;

  public:
    explicit Tagged_Union(Label label)
        : symbol(std::move(label))
    {
    }
    // move only
    Tagged_Union(Tagged_Union const&) = delete;
    Tagged_Union& operator=(Tagged_Union const&) = delete;
    Tagged_Union(Tagged_Union&&) = default;
    Tagged_Union& operator=(Tagged_Union&&) = default;

  public:
    bool operator==(Tagged_Union const&) const = default;

  public:
    void set_tag_entry(Label const& label,
        credence::type::Typed_RValue const& entry);
    void set_tag_entry(Label const& label, Vector entry);
    [[nodiscard]] Size get_largest_tag_entry() const;

    /**
     * @brief Check if a tag is defined in the union
     */
    [[nodiscard]] constexpr bool is_tag_entry(Label const& entry) const
    {
        return entries.contains(entry);
    }

  public:
    Label symbol{};
    Entries entries{};
};

static_assert(std::equality_comparable<Tagged_Union>);

/**
 * @brief Function and function frame storage data for the object table
 */
class Function
{
  public:
    using Return_RValue = std::optional<std::pair<RValue, RValue>>;
    using Address_Table = Symbol_Table<Label, Address>;
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
    constexpr void reset_calls() { calls = 0; }

  public:
    Return_RValue ret{};
    Label label_before_reserved{};
    Parameters parameters{};
    Ordered_Map<LValue, RValue> temporary{};
    Address_Table label_address{};
    Union_Table union_table{};
    std::array<Address, 2> address_location{};
    Label symbol{};
    Labels labels{};
    Locals locals{};
    RValues tokens{};
    Pointers pointers{};
    unsigned int calls{ 0 };
    unsigned int allocation{ 0 };
};

using Functions = Ordered_Map<std::string, std::reference_wrapper<Function>>;
using Vectors = Ordered_Map<std::string, std::reference_wrapper<Vector>>;
using Tagged_Unions =
    Ordered_Map<std::string, std::reference_wrapper<Tagged_Union>>;

credence::type::Typed_RValue get_rvalue_at_lvalue_object_storage(
    LValue const& lvalue,
    Function& stack_frame,
    Vectors& vectors,
    std::source_location const& location = std::source_location::current());

namespace detail {
/**
 * @brief Vector offset rvalue resolution in the stack frame and global symbols
 */
struct Vector_Offset
{
  public:
    Vector_Offset() = delete;
    explicit Vector_Offset(Function& stack_frame, Vectors& vectors)
        : stack_frame_(stack_frame)
        , vectors_(vectors)
    {
    }

  public:
    /**
     * @brief Check that the offset rvalue is a valid address in the vector
     */
    [[nodiscard]] constexpr bool is_valid_vector_address_offset(
        LValue const& lvalue)
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
    inline RValue get_rvalue_offset_of_vector(RValue const& offset)
    {
        // clang-format off
        return stack_frame_.locals.is_defined(offset)
        ? type::get_value_from_typed_rvalue(
            get_rvalue_at_lvalue_object_storage(offset, stack_frame_, vectors_))
        : offset;
        // clang-format on
    }

  private:
    Function& stack_frame_;
    Vectors& vectors_;
};

} // namespace detail

/**
 * @brief Query the storage of an rvalue in a stack frame
 *
 * Each query resolves an lvalue or rvalue against a stack frame, and the
 * vectors of the program where an element may be resolved, so the frame
 * under translation is an argument of the stage that walks the program.
 */
Type get_type_from_object_data_type(LValue const& lvalue,
    Function& stack_frame,
    Vectors& vectors);

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
 * @brief Whether an rvalue, or the vector of an element, is a vector
 */
[[nodiscard]] constexpr bool is_vector(RValue const& rvalue,
    Vectors const& vectors)
{
    auto label =
        util::contains(rvalue, "[") ? type::from_lvalue_offset(rvalue) : rvalue;
    return vectors.contains(label);
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
 * @brief Check if an rvalue is the tag of a union type such as `value_t.text`,
 * where the tag of a union variable like `v.text` is checked by
 * Object_Table::is_union_variable instead
 */
[[nodiscard]] constexpr bool is_union(RValue const& rvalue,
    Tagged_Unions const& unions)
{
    if (not type::is_dot_notation_object_identifier(rvalue))
        return false;
    auto union_data = type::get_union_identifier_sections(rvalue);
    return unions.contains(std::string{ union_data.first }) and
           unions.at(union_data.first)
               .get()
               .is_tag_entry(std::string{ union_data.second });
}

/**
 * @brief Whether an rvalue is a vector, a pointer, or a dereference
 */
[[nodiscard]] constexpr bool is_vector_or_pointer(RValue const& rvalue,
    Function& stack_frame,
    Vectors const& vectors)
{
    return is_vector(rvalue, vectors) or is_pointer(rvalue, stack_frame) or
           type::is_dereference_expression(rvalue);
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

/**
 * @brief The string, float, and double literals of the data section
 *
 * Each literal that is addressed relative to the program counter, rather
 * than as an immediate, is stored once.
 */
struct Data_Section
{
    void insert(RValue const& rvalue);
    void insert(credence::type::Typed_RValue const& rvalue);

    RValues strings{};
    RValues floats{};
    RValues doubles{};
};

/**
 * @brief The functions, vectors, symbols, and data section of a program
 */
class Object_Table
{
  public:
    explicit Object_Table() = default;
    Object_Table(Object_Table const&) = delete;
    Object_Table& operator=(Object_Table const&) = delete;
    Object_Table(Object_Table&&) = default;
    Object_Table& operator=(Object_Table&&) = default;

  public:
    Function& set_function_definition(Label const& label);
    Vector& set_vector_definition(Label const& label, Address size_of);
    Tagged_Union& set_union_definition(Label const& label);

  public:
    /**
     * @brief Whether a vector is defined in the object table
     */
    [[nodiscard]] constexpr bool vector_contains(LValue const& lvalue)
    {
        return vectors.contains(lvalue);
    }

    /**
     * @brief Reset the number of calls of each function
     */
    constexpr void reset_frame_calls()
    {
        for (auto& function : functions) {
            function.second.get().reset_calls();
        }
    }
    /**
     * @brief The stack frame of a function by its label
     */
    [[nodiscard]] constexpr Function& get_stack_frame(Label const& label)
    {
        credence_assert(functions.contains(label));
        return functions.at(label);
    }

    /**
     * @brief Get the union of a variable from the union table of the frame,
     * and then from the union globals
     */
    [[nodiscard]] constexpr std::optional<std::reference_wrapper<Tagged_Union>>
    get_union_of_variable(LValue const& variable, Function const& frame)
    {
        if (frame.union_table.contains(variable))
            return unions.at(frame.union_table.at(variable));
        if (union_globals.contains(variable))
            return unions.at(union_globals.at(variable));
        return std::nullopt;
    }

    /**
     * @brief Check if an lvalue is the tag of a union variable, such as
     * `v.text` or `v.pair[0]`
     */
    [[nodiscard]] constexpr bool is_union_variable(LValue const& lvalue,
        Function const& frame)
    {
        if (not type::is_dot_notation_object_identifier(lvalue))
            return false;
        auto [variable, tag] = type::get_union_identifier_sections(lvalue);
        auto tagged_union =
            get_union_of_variable(std::string{ variable }, frame);
        return tagged_union and
               tagged_union->get().is_tag_entry(std::string{ tag });
    }

  private:
    std::deque<Function> function_storage_{};
    std::deque<Vector> vector_storage_{};
    std::deque<Tagged_Union> union_storage_{};

  public:
    util::AST_Node hoisted_symbols;
    Symbol_Table<> globals{};
    Ordered_Map<LValue, Label> union_globals{};
    Functions functions{};
    Vectors vectors{};
    Tagged_Unions unions{};
    Data_Section data{};
    Labels labels{};
};

void set_stack_frame_return_value(RValue const& rvalue,
    Function& frame,
    Vectors& vectors,
    Stack const& stack);

} // namespace credence::object
