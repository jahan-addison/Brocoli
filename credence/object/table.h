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

#include <credence/error.h>        // for credence_assert
#include <credence/map.h>          // for Ordered_Map
#include <credence/object/frame.h> // for Function, LValue, RValue, Label, Size
#include <credence/symbol.h>       // for Symbol_Table
#include <credence/types.h> // for Parameters, Address, Typed_RValue, Label
#include <credence/util.h>  // for contains, AST_Node
#include <cstddef>          // for size_t
#include <deque>            // for deque
#include <functional>       // for reference_wrapper
#include <numeric>          // for accumulate
#include <optional>         // for optional
#include <source_location>  // for source_location
#include <string>           // for basic_string, string
#include <utility>          // for pair, move
#include <variant>          // for variant

/****************************************************************************
 * Object Table
 *
 * The vectors, unions, functions, and stack frames of a program, as
 * queryable lvalues and rvalues. hir::Analyzer defines each function,
 * vector, and global, ir::Binder defines the labels and locals of each frame
 * and binds the IR onto them, and each backend allocates and addresses memory
 * from it, where the record of each function's frame is in frame.h.
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

constexpr Size k_pointer_size{ 8 };

/**
 * @brief Get a typed rvalue's storage size, where a string is its pointer's
 * size and not its literal's length
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
    /**
     * @brief Get the vector data's unpadded storage size, since each backend
     * aligns it
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
    int decay_index{ 0 };
    Size size{ 0 };
    Label symbol{};
};

static_assert(std::equality_comparable<Vector>);

/**
 * @brief Tagged union definition with each tag's value, a typed rvalue for a
 * scalar tag or a Vector of a tuple tag's elements
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

using Functions = Ordered_Map<std::string, std::reference_wrapper<Function>>;
using Vectors = Ordered_Map<std::string, std::reference_wrapper<Vector>>;
using Tagged_Unions =
    Ordered_Map<std::string, std::reference_wrapper<Tagged_Union>>;

/**
 * @brief Data section string, float, and double literals
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
 * @brief A program's functions, vectors, unions, symbols, and data section,
 * each private and read through the table's queries
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
     * @brief Check if a vector is defined in the object table
     */
    [[nodiscard]] constexpr bool vector_contains(LValue const& lvalue) const
    {
        return vectors_.contains(lvalue);
    }

    /**
     * @brief Check if a function is defined in the object table
     */
    [[nodiscard]] bool function_contains(Label const& label) const
    {
        return functions_.contains(label);
    }

    /**
     * @brief Get a label's vector definition in the object table
     */
    [[nodiscard]] Vector& get_vector(Label const& label)
    {
        credence_assert(vectors_.contains(label));
        return vectors_.at(label);
    }
    [[nodiscard]] Vector const& get_vector(Label const& label) const
    {
        credence_assert(vectors_.contains(label));
        return vectors_.at(label);
    }

    /**
     * @brief Get a function's stack frame by its label
     */
    [[nodiscard]] constexpr Function& get_stack_frame(Label const& label)
    {
        credence_assert(functions_.contains(label));
        return functions_.at(label);
    }
    [[nodiscard]] Function const& get_stack_frame(Label const& label) const
    {
        credence_assert(functions_.contains(label));
        return functions_.at(label);
    }

    /**
     * @brief Get a variable's union from the frame's union table, then the
     * union globals
     */
    /**
     * @brief Check if an rvalue, or an element's vector such as `v[2]`, is a
     * program vector
     */
    [[nodiscard]] bool is_vector(RValue const& rvalue) const
    {
        auto label = util::contains(rvalue, "[")
                         ? type::from_lvalue_offset(rvalue)
                         : rvalue;
        return vectors_.contains(label);
    }

    /**
     * @brief Check if an rvalue is a vector, a pointer, or a dereference in
     * a stack frame
     */
    [[nodiscard]] bool is_vector_or_pointer(RValue const& rvalue,
        Function const& stack_frame) const
    {
        return is_vector(rvalue) or is_pointer(rvalue, stack_frame) or
               type::is_dereference_expression(rvalue);
    }

  public:
    /**
     * @brief Query the storage of an rvalue in a stack frame
     *
     * Each query resolves an lvalue or rvalue against the current stack frame
     * and the program's vectors, so the frame is an argument from the stage
     * that walks the program.
     */
    type::Typed_RValue get_rvalue_at_lvalue_object_storage(LValue const& lvalue,
        Function const& stack_frame,
        std::source_location const& location =
            std::source_location::current()) const;
    Type get_type_from_object_data_type(LValue const& lvalue,
        Function const& stack_frame) const;
    void set_stack_frame_return_value(RValue const& rvalue,
        Function& frame,
        Stack const& stack);

  public:
    [[nodiscard]] constexpr std::optional<std::reference_wrapper<Tagged_Union>>
    get_union_of_variable(LValue const& variable, Function const& frame) const
    {
        if (frame.union_table.contains(variable))
            return unions_.at(frame.union_table.at(variable));
        if (union_globals_.contains(variable))
            return unions_.at(union_globals_.at(variable));
        return std::nullopt;
    }

    /**
     * @brief Check if an lvalue is a union variable's tag, such as `v.text` or
     * `v.pair[0]`
     */
    [[nodiscard]] constexpr bool is_union_variable(LValue const& lvalue,
        Function const& frame) const
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
    /**
     * @brief Check if a vector is a global, a pointer in the object table's
     * globals
     */
    [[nodiscard]] bool is_global_vector(Label const& label) const
    {
        return vector_contains(label) and globals_.is_pointer(label);
    }

    /**
     * @brief Check if a union is defined in the object table
     */
    [[nodiscard]] bool union_contains(Label const& label) const
    {
        return unions_.contains(label);
    }

    /**
     * @brief Get a label's union definition in the object table
     */
    [[nodiscard]] Tagged_Union& get_union(Label const& label)
    {
        credence_assert(unions_.contains(label));
        return unions_.at(label);
    }

    /**
     * @brief Set a global variable's union, such as `union value_t last;`
     */
    void set_union_global(LValue const& variable, Label const& label)
    {
        union_globals_.insert(variable, label);
    }

    /**
     * @brief Check if an rvalue is a union type's tag, such as `value_t.text`,
     * where a union variable's tag like `v.text` is checked by
     * is_union_variable
     */
    [[nodiscard]] bool is_union(RValue const& rvalue) const
    {
        if (not type::is_dot_notation_object_identifier(rvalue))
            return false;
        auto [label, tag] = type::get_union_identifier_sections(rvalue);
        return unions_.contains(std::string{ label }) and
               unions_.at(label).get().is_tag_entry(std::string{ tag });
    }

  public:
    /**
     * @brief Get the program's functions and vectors, read-only and in
     * insertion order
     */
    [[nodiscard]] Functions const& get_functions() const { return functions_; }
    [[nodiscard]] Vectors const& get_vectors() const { return vectors_; }

    /**
     * @brief Get the program's symbols and data section
     */
    util::AST_Node& hoisted_symbols() { return hoisted_symbols_; }
    util::AST_Node const& hoisted_symbols() const { return hoisted_symbols_; }
    Symbol_Table<>& globals() { return globals_; }
    Symbol_Table<> const& globals() const { return globals_; }
    Data_Section& data() { return data_; }
    Data_Section const& data() const { return data_; }
    Labels& labels() { return labels_; }
    Labels const& labels() const { return labels_; }

  private:
    util::AST_Node hoisted_symbols_;
    Symbol_Table<> globals_{};
    Ordered_Map<LValue, Label> union_globals_{};
    Functions functions_{};
    Vectors vectors_{};
    Tagged_Unions unions_{};
    Data_Section data_{};
    Labels labels_{};
};

} // namespace credence::object
