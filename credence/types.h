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

#include <algorithm>         // for all_of
#include <compare>           // for operator<=>
#include <concepts>          // for integral
#include <credence/error.h>  // for credence_assert_message
#include <credence/symbol.h> // for Symbol_Table
#include <credence/util.h>   // for AST_Node, CREDENCE_PRIVATE_UNLESS_TESTED
#include <deque>             // for deque
#include <easyjson.h>        // for JSON
#include <fmt/format.h>      // for format
#include <initializer_list>  // for initializer_list
#include <list>              // for list
#include <memory>            // for allocator, make_shared
#include <set>               // for set
#include <source_location>   // for source_location
#include <string>            // for basic_string, string
#include <string_view>       // for string_view
#include <tuple>             // for tuple
#include <utility>           // for pair
#include <variant>           // for variant
#include <vector>            // for vector

#include <credence/language/literal.h> // for Array

/****************************************************************************
 * Type category, string, and numeric utilities
 *   See language/literal.h for details on literals and typed rvalues
 *****************************************************************************/

namespace credence::type {

namespace semantic {

/**
 * Semantic scaler type definitions
 */
using Label = std::string;
using Type = std::string;
using LValue = std::string;
using Size = std::size_t;
using RValue = std::string;
using Address = std::size_t;

} // namespace semantic

/**
 * Algebraic type definitions
 */
/**
 * @brief An rvalue with its type and size in bytes
 *
 * The IR writes it as "(value:type:size)", such as "(10:int:4)", and
 * get_typed_rvalue_from_string and get_typed_rvalue_as_string convert
 * between the two.
 */
struct Typed_RValue
{
    semantic::RValue value{};
    semantic::Type type{};
    semantic::Size size{};

    friend auto operator<=>(Typed_RValue const&, Typed_RValue const&) = default;
};
using RValue_Reference = std::string_view;

/**
 * @brief Get the offsets of the spaces outside the string and char literals
 * of an rvalue
 *
 * The operands and operator of a binary expression are separated by
 * spaces, and a literal may contain a space, `"m is %d"` or `' '`
 *
 *  Examples:
 *
 *    get_unquoted_space_offsets("x + y")                       // { 1, 3 }
 *    get_unquoted_space_offsets("(\"a b\":string:3) == x")    // { 16, 19 }
 */
constexpr std::vector<std::size_t> get_unquoted_space_offsets(
    RValue_Reference rvalue)
{
    std::vector<std::size_t> offsets{};
    char quote{ '\0' };
    for (std::size_t i = 0; i < rvalue.size(); i++) {
        auto c = rvalue[i];
        if (quote != '\0') {
            if (c == '\\')
                i++;
            else if (c == quote)
                quote = '\0';
        } else if (c == '"' or c == '\'')
            quote = c;
        else if (c == ' ')
            offsets.emplace_back(i);
    }
    return offsets;
}

/**
 * @brief Get the operator of a binary expression, the token between its
 * operands, or empty when the rvalue is not a binary expression
 */
constexpr RValue_Reference get_binary_expression_operator(
    RValue_Reference rvalue)
{
    auto offsets = get_unquoted_space_offsets(rvalue);
    if (offsets.size() != 2)
        return {};
    return rvalue.substr(offsets[0] + 1, offsets[1] - offsets[0] - 1);
}
using Stack = std::deque<semantic::RValue>;
using Labels = std::set<semantic::Label>;
using Strings = std::set<semantic::RValue>;
using RValues = Strings;
using Floats = std::set<float>;
using Doubles = std::set<double>;
using Globals = language::literal::Array;
using Binary_Expression =
    std::tuple<semantic::RValue, std::string, semantic::RValue>;
using RValue_Reference_Type = std::variant<semantic::RValue, Typed_RValue>;
using Locals = Symbol_Table<Typed_RValue, semantic::LValue>;
using Temporary = std::pair<semantic::LValue, semantic::RValue>;
using Parameters = std::vector<semantic::RValue>;
using Pointers = std::list<semantic::RValue>;

constexpr auto unary_operators = { "++", "--", "*", "&", "-", "+", "~", "!" };
constexpr auto arithmetic_unary_operators = { "++", "--", "-", "+" };
constexpr auto arithmetic_binary_operators = { "*", "/", "-", "+", "%" };
constexpr auto bitwise_binary_operators = { "<<", ">>", "|", "^", "&" };
constexpr auto relation_binary_operators = { "==",
    "!=",
    "<",
    "&&",
    "||",
    ">",
    "<=",
    ">=" };

constexpr auto integral_unary_types = { "int", "double", "float", "long" };

const Typed_RValue NULL_RVALUE_LITERAL =
    Typed_RValue{ "NULL", "null", sizeof(void*) };

constexpr semantic::RValue get_value_from_typed_rvalue(
    Typed_RValue const& rvalue);

constexpr semantic::Type get_value_from_typed_rvalue(
    type::semantic::RValue const& rvalue);

inline int integral_from_type_int(std::string const& t)
{
    return std::stoi(t);
}

inline int integral_from_type_ulint(std::string const& t)
{
    return std::stoul(t);
}

inline long integral_from_type_long(std::string const& t)
{
    return std::stol(t);
}

inline float integral_from_type_float(std::string const& t)
{
    return std::stof(t);
}

inline double integral_from_type_double(std::string const& t)
{
    return std::stod(t);
}

template<util::Numeric T>
T integral_from_type(std::string const& t)
{
    if constexpr (std::is_same_v<T, int>) {
        return integral_from_type_int(t);
    } else if constexpr (std::is_same_v<T, long>) {
        return integral_from_type_long(t);
    } else if constexpr (std::is_same_v<T, float>) {
        return integral_from_type_float(t);
    } else if constexpr (std::is_same_v<T, double>) {
        return integral_from_type_double(t);
    } else {
        return integral_from_type_int(t);
    }
}

/**
 * @brief Check if expression contains arithmetic expression
 */
constexpr bool is_binary_arithmetic_expression(semantic::RValue const& rvalue)
{
    auto op = get_binary_expression_operator(rvalue);
    return std::ranges::find(arithmetic_binary_operators, op) !=
           arithmetic_binary_operators.end();
}

constexpr bool is_binary_arithmetic_operator(RValue_Reference rvalue)
{
    auto test = std::ranges::find_if(arithmetic_binary_operators.begin(),
        arithmetic_binary_operators.end(),
        [&](std::string_view s) { return rvalue == s; });
    return test != arithmetic_binary_operators.end();
}

constexpr bool is_unary_arithmetic_operator(RValue_Reference rvalue)
{
    auto test = std::ranges::find_if(arithmetic_unary_operators.begin(),
        arithmetic_unary_operators.end(),
        [&](std::string_view s) { return rvalue == s; });
    return test != arithmetic_unary_operators.end();
}

constexpr bool is_bitwise_binary_operator(RValue_Reference rvalue)
{
    auto test = std::ranges::find_if(bitwise_binary_operators.begin(),
        bitwise_binary_operators.end(),
        [&](std::string_view s) { return rvalue == s; });
    return test != bitwise_binary_operators.end();
}

/**
 * @brief Check if expression is a bitwise expression
 *
 * The operator is the middle operand, so `||` and `&&` are not bitwise
 */
constexpr bool is_bitwise_binary_expression(semantic::RValue const& rvalue)
{
    auto op = get_binary_expression_operator(rvalue);
    return not op.empty() and is_bitwise_binary_operator(op);
}

/**
 * @brief Check if expression contains relational expression
 */
constexpr bool is_relation_binary_expression(semantic::RValue const& rvalue)
{
    auto op = get_binary_expression_operator(rvalue);
    return std::ranges::find(relation_binary_operators, op) !=
           relation_binary_operators.end();
}

constexpr bool is_relation_binary_operator(RValue_Reference rvalue)
{
    auto test = std::ranges::find_if(relation_binary_operators.begin(),
        relation_binary_operators.end(),
        [&](std::string_view s) { return rvalue == s; });
    return test != relation_binary_operators.end();
}

constexpr bool is_typed_rvalue(semantic::RValue const& rvalue)
{
    return util::substring_count_of(rvalue, ":") >= 2 and
           rvalue.starts_with("(") and rvalue.ends_with(")");
}

/**
 * @brief A typed rvalue as its IR string, such as "(10:int:4)"
 */
constexpr std::string get_typed_rvalue_as_string(Typed_RValue const& value)
{
    using namespace fmt::literals;
    return fmt::format("({}:{}:{})"_cf, value.value, value.type, value.size);
}

/**
 * @brief Get a label as human readable object
 *    e.g. "__main(argc, argv)" -> "main"
 */
constexpr semantic::Label get_label_as_human_readable(
    semantic::Label const& label)
{
    if (util::contains(label, "("))
        return semantic::Label{ label.begin() + 2,
            label.begin() + label.find_first_of("(") };
    else
        return label;
}

/**
 * @brief Get unary rvalue from ITA rvalue string
 */
constexpr semantic::RValue get_unary_rvalue_reference(RValue_Reference rvalue,
    std::string_view unary_chracters = "+-*&+~!")
{
    auto lvalue = std::string{ rvalue.begin(), rvalue.end() };
    lvalue.erase(std::remove_if(lvalue.begin(),
                     lvalue.end(),
                     [&](char ch) {
                         return ::isspace(ch) or unary_chracters.find(ch) !=
                                                     std::string_view::npos;
                     }),
        lvalue.end());
    return lvalue;
}

/**
 * @brief Get unary operator from ITA rvalue string
 */
constexpr semantic::RValue get_unary_operator(RValue_Reference rvalue)
{
    auto it = std::ranges::find_if(unary_operators, [&](std::string_view op) {
        return rvalue.find(op) != std::string_view::npos;
    });
    if (it == unary_operators.end())
        return "";
    return *it;
}

/**
 * @brief Check if an expression contains unary operator
 */
constexpr bool is_unary_expression(RValue_Reference rvalue)
{
    if (util::substring_count_of(rvalue, " ") >= 2)
        return false;
    return std::ranges::any_of(unary_operators, [&](std::string_view x) {
        return rvalue.starts_with(x) or rvalue.ends_with(x);
    });
}

constexpr bool is_unary_typed_rvalue_expression(
    Typed_RValue const& typed_rvalue)
{
    auto rvalue = type::get_value_from_typed_rvalue(typed_rvalue);
    if (util::substring_count_of(rvalue, " ") >= 2)
        return false;
    return std::ranges::any_of(unary_operators, [&](std::string_view x) {
        return rvalue.starts_with(x) or rvalue.ends_with(x);
    });
}

/**
 * @brief Check if an expression contains dereference unary operator
 */
constexpr bool is_dereference_expression(RValue_Reference rvalue)
{
    // a literal is not a dereference, such as the string "bad syntax*n"
    if (util::substring_count_of(rvalue, " ") >= 2 or
        is_typed_rvalue(semantic::RValue{ rvalue }))
        return false;
    return get_unary_operator(rvalue) == "*";
}

constexpr bool is_address_of_expression(RValue_Reference rvalue)
{
    if (util::substring_count_of(rvalue, " ") >= 2)
        return false;
    return get_unary_operator(rvalue) == "&";
}

/**
 * @brief Check if an operator is binary operator
 */
constexpr bool is_binary_operator(semantic::RValue const& op)
{
    return is_binary_arithmetic_operator(op) or
           is_relation_binary_operator(op) or is_bitwise_binary_operator(op);
}

/**
 * @brief Check if symbol is a temporary (i.e. "_t1" )
 */
constexpr bool is_temporary(RValue_Reference rvalue)
{
    for (std::size_t i = 0; i < rvalue.size(); i++) {
        const auto c = rvalue[i];
        if (i == 0 and c != '_')
            return false;
        if (i == 1 and c != 't')
            return false;
        if (i > 1 and not std::isdigit(c))
            return false;
    }
    return true;
}

/**
 * @brief Check if a name is a valid identifier, a letter or underscore
 * followed by any letters, digits, or underscores
 *
 *  Examples:
 *
 *    is_valid_identifier("value_t")    // true
 *    is_valid_identifier("_t1")        // true
 *    is_valid_identifier("1x")         // false
 */
constexpr bool is_valid_identifier(std::string_view id)
{
    if (id.empty())
        return false;
    auto is_alpha_or_underscore = [](char c) {
        return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or c == '_';
    };
    auto is_alnum_or_underscore = [&](char c) {
        return is_alpha_or_underscore(c) or (c >= '0' and c <= '9');
    };
    return is_alpha_or_underscore(id.front()) and
           std::ranges::all_of(id.substr(1), is_alnum_or_underscore);
}

/**
 * @brief Split an lvalue into its object and the offset of its subscript,
 * where the offset is empty if there's no subscript or it isn't a single
 * offset at the end
 *
 *  Examples:
 *
 *    get_subscript_sections("v.pair[0]")    // { "v.pair", "0" }
 *    get_subscript_sections("v.pair[n]")    // { "v.pair", "n" }
 *    get_subscript_sections("v.text")       // { "v.text", "" }
 *    get_subscript_sections("v.pair[0")     // { "v.pair[0", "" }
 */
constexpr std::pair<std::string_view, std::string_view> get_subscript_sections(
    std::string_view lvalue)
{
    auto open = lvalue.find('[');
    if (open == std::string_view::npos or not lvalue.ends_with(']') or
        lvalue.find('[', open + 1) != std::string_view::npos)
        return { lvalue, {} };
    return { lvalue.substr(0, open),
        lvalue.substr(open + 1, lvalue.size() - open - 2) };
}

/**
 * @brief Check if an offset is an integer or an identifier
 *
 *  Examples:
 *
 *    is_valid_offset("0")     // true
 *    is_valid_offset("n")     // true
 *    is_valid_offset("1x")    // false
 */
constexpr bool is_valid_offset(std::string_view offset)
{
    auto is_digit = [](char c) { return c >= '0' and c <= '9'; };
    return is_valid_identifier(offset) or
           (not offset.empty() and std::ranges::all_of(offset, is_digit));
}

/**
 * @brief Check if an rvalue is an object and its tag with an optional offset,
 * where the object is a union like `value_t.text` or a union variable like
 * `v.text`
 *
 *  Examples:
 *
 *    is_dot_notation_object_identifier("value_t.text")    // true
 *    is_dot_notation_object_identifier("v.pair[0]")       // true
 *    is_dot_notation_object_identifier("v.pair[0][1]")    // false
 *    is_dot_notation_object_identifier("2.5")             // false
 */
constexpr bool is_dot_notation_object_identifier(std::string_view rvalue)
{
    auto [object, offset] = get_subscript_sections(rvalue);
    if (object.size() != rvalue.size() and not is_valid_offset(offset))
        return false;
    auto dot = object.find('.');
    if (dot == std::string_view::npos)
        return false;
    return is_valid_identifier(object.substr(0, dot)) and
           is_valid_identifier(object.substr(dot + 1));
}

/**
 * @brief Get the object and the tag of a dot notation identifier, without its
 * offset
 *
 *  Examples:
 *
 *    get_union_identifier_sections("value_t.text")    // { "value_t", "text" }
 *    get_union_identifier_sections("v.pair[1]")       // { "v", "pair" }
 */
constexpr std::pair<std::string_view, std::string_view>
get_union_identifier_sections(std::string_view identifier)
{
    credence_assert_message(is_dot_notation_object_identifier(identifier),
        "identifier is not a union");
    auto object = get_subscript_sections(identifier).first;
    auto dot = object.find('.');
    return { object.substr(0, dot), object.substr(dot + 1) };
}

/**
 * @brief Get the offset of an element of a tuple tag, or an empty offset for a
 * scalar tag
 *
 *  Examples:
 *
 *    get_union_tag_offset("v.pair[1]")    // "1"
 *    get_union_tag_offset("v.text")       // ""
 */
constexpr std::string_view get_union_tag_offset(std::string_view identifier)
{
    credence_assert_message(is_dot_notation_object_identifier(identifier),
        "identifier is not a union");
    return get_subscript_sections(identifier).second;
}

/**
 * @brief Parse semantic::RValue::Value_Type string into a 3-tuple of
 * value, type, and size
 *
 * e.g. "(10:int:4)" -> (10, "int", 4UL)
 */
inline Typed_RValue get_typed_rvalue_from_string(semantic::RValue const& rvalue,
    std::source_location const& location = std::source_location::current())
{
    credence::detail::assert_impl(location,
        is_typed_rvalue(rvalue) == true,
        fmt::format("rvalue \"{}\" is not a typed rvalue", rvalue));
    auto search = rvalue.find_last_of(":");
    auto bytes = std::string{ rvalue.begin() + search + 1, rvalue.end() - 1 };
    auto type_search = rvalue.substr(0, search - 1).find_last_of(":") + 1;
    auto type =
        std::string{ rvalue.begin() + type_search, rvalue.begin() + search };

    auto value = std::ranges::find(rvalue, '"') == rvalue.end()
                     ? rvalue.substr(1, type_search - 2)
                     : rvalue.substr(2, type_search - 4);

    return { value, type, std::stoul(bytes) };
}

/**
 * @brief Parse ITA binary expression into its operator and operands
 */
constexpr Binary_Expression from_rvalue_binary_expression(
    semantic::RValue const& rvalue)
{
    auto offsets = get_unquoted_space_offsets(rvalue);
    if (offsets.size() < 2)
        return { rvalue, std::string{}, std::string{} };
    auto lhs = offsets.front();
    auto rhs = offsets.back();
    auto lhs_lvalue = std::string{ rvalue.begin(), rvalue.begin() + lhs };
    auto rhs_lvalue = std::string{ rvalue.begin() + 1 + rhs, rvalue.end() };
    auto binary_operator =
        std::string{ rvalue.begin() + lhs + 1, rvalue.begin() + rhs };

    return { lhs_lvalue, rhs_lvalue, binary_operator };
}

constexpr Binary_Expression from_rvalue_binary_expression(
    Typed_RValue const& typed_rvalue)
{
    auto rvalue =
        std::string{ type::get_value_from_typed_rvalue(typed_rvalue) };
    auto offsets = get_unquoted_space_offsets(rvalue);
    if (offsets.size() < 2)
        return { rvalue, std::string{}, std::string{} };
    auto lhs = offsets.front();
    auto rhs = offsets.back();
    auto lhs_lvalue = std::string{ rvalue.begin(), rvalue.begin() + lhs };
    auto rhs_lvalue = std::string{ rvalue.begin() + 1 + rhs, rvalue.end() };
    auto binary_operator =
        std::string{ rvalue.begin() + lhs + 1, rvalue.begin() + rhs };

    return { lhs_lvalue, rhs_lvalue, binary_operator };
}

/**
 * @brief Get binary operator from ITA rvalue string
 */
constexpr semantic::RValue get_binary_operator(RValue_Reference rvalue)
{
    semantic::RValue op{};
    auto it = std::ranges::find_if(
        arithmetic_binary_operators, [&](std::string_view op) {
            return rvalue.find(op) != std::string_view::npos;
        });
    if (it != arithmetic_binary_operators.end())
        op = *it;
    it = std::ranges::find_if(
        relation_binary_operators, [&](std::string_view op) {
            return rvalue.find(op) != std::string_view::npos;
        });
    if (it != relation_binary_operators.end())
        op = *it;
    it = std::ranges::find_if(
        bitwise_binary_operators, [&](std::string_view op) {
            return rvalue.find(op) != std::string_view::npos;
        });
    if (it != bitwise_binary_operators.end())
        op = *it;

    return op;
}

/**
 * @brief Get the value of a typed rvalue
 */
constexpr semantic::RValue get_value_from_typed_rvalue(
    Typed_RValue const& rvalue)
{
    return rvalue.value;
}

constexpr semantic::Type get_value_from_typed_rvalue(
    type::semantic::RValue const& rvalue)
{
    if (is_typed_rvalue(rvalue)) {
        return get_value_from_typed_rvalue(
            get_typed_rvalue_from_string(rvalue));
    } else
        return "null";
}

/**
 * @brief Get the type from a local in the stack frame
 */
constexpr semantic::Type get_type_from_typed_rvalue(Typed_RValue const& rvalue)
{
    return rvalue.type;
}

constexpr semantic::Type get_type_from_typed_rvalue(
    type::semantic::RValue const& rvalue)
{
    if (is_typed_rvalue(rvalue)) {
        return get_type_from_typed_rvalue(get_typed_rvalue_from_string(rvalue));
    } else
        return "null";
}

/**
 * @brief Check if a typed rvalue is a provided type
 */
constexpr bool is_typed_rvalue_a_type(Typed_RValue const& rvalue,
    std::string_view type_)
{
    return rvalue.type == type_;
}

constexpr bool is_typed_rvalue_a_type(type::semantic::RValue const& rvalue,
    std::string_view type_)
{
    if (is_typed_rvalue(rvalue)) {
        return get_type_from_typed_rvalue(
                   get_typed_rvalue_from_string(rvalue)) == type_;
    } else
        return false;
}

/**
 * @brief Check if a typed rvalue is a string
 */
constexpr bool is_typed_rvalue_string(Typed_RValue const& rvalue)
{
    return is_typed_rvalue_a_type(rvalue, "string");
}

constexpr bool is_typed_rvalue_string(type::semantic::RValue const& rvalue)
{
    return is_typed_rvalue_a_type(rvalue, "string");
}

/**
 * @brief Check if a typed rvalue is a word (pointer)
 */

constexpr bool is_typed_rvalue_word(Typed_RValue const& rvalue)
{
    return is_typed_rvalue_a_type(rvalue, "word");
}

constexpr bool is_typed_rvalue_word(type::semantic::RValue const& rvalue)
{
    return is_typed_rvalue_a_type(rvalue, "word");
}

/**
 * @brief Get the type from a local in the stack frame
 */
constexpr semantic::Size get_size_from_typed_rvalue(Typed_RValue const& rvalue)
{
    return rvalue.size;
}

constexpr semantic::Size get_size_from_typed_rvalue(
    type::semantic::RValue const& rvalue)
{
    if (is_typed_rvalue(rvalue)) {
        return get_size_from_typed_rvalue(get_typed_rvalue_from_string(rvalue));
    } else
        return 0UL;
}

/**
 * @brief Get the lvalue from a vector or pointer offset
 *
 *   * v[19]        = v
 *   * sidno[errno] = sidno
 *
 */
constexpr semantic::RValue from_lvalue_offset(RValue_Reference rvalue)
{
    if (!util::contains(rvalue, "]"))
        return rvalue.data();
    return std::string{ rvalue.begin(),
        rvalue.begin() + rvalue.find_first_of("[") };
}

/**
 * @brief Get the integer or rvalue reference offset
 *   * v[20]        = 20
 *   * sidno[errno] = errno
 */
constexpr semantic::RValue from_decay_offset(RValue_Reference rvalue)
{
    if (!util::contains(rvalue, "]"))
        return rvalue.data();
    return std::string{ rvalue.begin() + rvalue.find_first_of("[") + 1,
        rvalue.begin() + rvalue.find_first_of("]") };
}

/**
 * @brief Check if symbol is an expression with two symbol::Typed_RValues
 */
constexpr bool is_binary_typed_rvalue_expression(semantic::RValue const& rvalue)
{
    if (get_unquoted_space_offsets(rvalue).size() != 2)
        return false;
    auto test = from_rvalue_binary_expression(rvalue);
    if (!is_typed_rvalue(std::get<0>(test)))
        return false;
    if (!is_typed_rvalue(std::get<1>(test)))
        return false;
    return true;
}

constexpr bool is_binary_typed_rvalue_expression(
    Typed_RValue const& typed_rvalue)
{
    auto rvalue = get_value_from_typed_rvalue(typed_rvalue);

    if (get_unquoted_space_offsets(rvalue).size() != 2)
        return false;
    auto test = from_rvalue_binary_expression(rvalue);
    if (!is_typed_rvalue(std::get<0>(test)))
        return false;
    if (!is_typed_rvalue(std::get<1>(test)))
        return false;
    return true;
}

/**
 *
 * @brief Check if symbol is an expression with two temporaries
 */
constexpr bool is_temporary_typed_rvalue_binary_expression(
    semantic::RValue const& rvalue)
{
    if (get_unquoted_space_offsets(rvalue).size() != 2)
        return false;
    auto test = from_rvalue_binary_expression(rvalue);
    if (!is_temporary(std::get<0>(test)) and
        not is_temporary(std::get<1>(test)))
        return false;
    return true;
}

constexpr bool is_temporary_typed_rvalue_binary_expression(
    Typed_RValue const& typed_rvalue)
{
    return is_temporary_typed_rvalue_binary_expression(
        get_value_from_typed_rvalue(typed_rvalue));
}

/**
 * @brief Check and get the operand of a temporary binary rvalue
 */
constexpr std::string is_temporary_operand_binary_expression(
    semantic::RValue const& rvalue)
{
    if (get_unquoted_space_offsets(rvalue).size() != 2)
        return std::string{};
    auto test = from_rvalue_binary_expression(rvalue);
    if (is_temporary(std::get<0>(test)))
        return "left";
    if (is_temporary(std::get<1>(test)))
        return "right";

    return std::string{};
}

/**
 * @brief Check if an expression contains binary operators
 */
constexpr bool is_binary_expression(semantic::RValue const& rvalue)
{
    if (get_unquoted_space_offsets(rvalue).size() != 2)
        return false;
    return is_binary_arithmetic_expression(rvalue) or
           is_relation_binary_expression(rvalue) or
           is_temporary_typed_rvalue_binary_expression(rvalue) or
           is_bitwise_binary_expression(rvalue);
}

constexpr std::string is_temporary_operand_binary_expression(
    Typed_RValue const& typed_rvalue)
{
    return is_temporary_operand_binary_expression(
        get_value_from_typed_rvalue(typed_rvalue));
}

} // namespace type

namespace credence {

/**
 * Semantic scaler types used throughout the compiler
 */
using LValue = type::semantic::LValue;
using Type = type::semantic::Type;
using Size = type::semantic::Size;
using RValue = type::semantic::RValue;
using Label = type::semantic::Label;

} // namespace credence
