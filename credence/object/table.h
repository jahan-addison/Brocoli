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

using Functions = Ordered_Map<std::string, std::reference_wrapper<Function>>;
using Vectors = Ordered_Map<std::string, std::reference_wrapper<Vector>>;
using Tagged_Unions =
    Ordered_Map<std::string, std::reference_wrapper<Tagged_Union>>;

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
 * @brief The functions, vectors, unions, symbols, and data section of a
 * program, where each is private and read through the queries of the table
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
     * @brief Get the vector definition of a label in the object table
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
     * @brief The stack frame of a function by its label
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
     * @brief Get the union of a variable from the union table of the frame,
     * and then from the union globals
     */
    /**
     * @brief Check if an rvalue, or the vector of an element such as
     * `v[2]`, is a vector of the program
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
     * Each query resolves an lvalue or rvalue against the stack frame under
     * translation and the vectors of the program, where an element may be
     * resolved, so the frame is an argument of the stage that walks the
     * program.
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
     * @brief Check if an lvalue is the tag of a union variable, such as
     * `v.text` or `v.pair[0]`
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
     * @brief Check if a vector is a global, a pointer in the globals of the
     * object table
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
     * @brief Get the union definition of a label in the object table
     */
    [[nodiscard]] Tagged_Union& get_union(Label const& label)
    {
        credence_assert(unions_.contains(label));
        return unions_.at(label);
    }

    /**
     * @brief Set the union of a global variable, such as `union value_t
     * last;`
     */
    void set_union_global(LValue const& variable, Label const& label)
    {
        union_globals_.insert(variable, label);
    }

    /**
     * @brief Check if an rvalue is the tag of a union type such as
     * `value_t.text`, where the tag of a union variable like `v.text` is
     * checked by is_union_variable instead
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
     * @brief Get the functions and the vectors of the program, read-only
     * and in insertion order
     */
    [[nodiscard]] Functions const& get_functions() const { return functions_; }
    [[nodiscard]] Vectors const& get_vectors() const { return vectors_; }

    /**
     * @brief Get the symbols and the data section of the program
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
