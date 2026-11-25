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

#include <array>           // for array
#include <credence/util.h> // for to_constexpr_string, AST_Node
#include <cstdlib>         // for free
#include <easyjson.h>      // for JSON
#include <exception>       // for exception
#include <fmt/compile.h>   // for operator""_cf
#include <fmt/format.h>    // for format, format_string
#include <source_location> // for source_location
#include <string>          // for basic_string, string
#include <string_view>     // for string_view
#include <utility>         // for forward

#if defined(DEBUG) && __has_include(<execinfo.h>)
#define CREDENCE_BACKTRACE
#include <cxxabi.h>   // for __cxa_demangle
#include <execinfo.h> // for backtrace, backtrace_symbols
#endif

/****************************************************************************
 *
 * Error handling and debugging diagnostics
 *
 * Provides compile-time error detection and reporting, including semantic
 * errors, type mismatches, out-of-bounds array access, undefined symbols, and
 * other programming errors throughout translation phases.
 *
 * Example - compile-time boundary checking:
 *
 *   main() {
 *     extrn values;
 *     print(values[10]);  // Error: array 'values' has size 3
 *   }
 *   values [3] 1, 2, 3;
 *
 *   Output: "Out of range: index 10 exceeds array size 3"
 *
 * Example - type mismatch:
 *
 *   add(x, y) { return(x + y); }
 *
 *   main() {
 *     add(5);  // Error: function 'add' expects 2 arguments, got 1
 *   }
 *
 * Example - undefined symbol:
 *
 *   main() {
 *     auto x;
 *     x = unknown_var;  // Error: undefined symbol 'unknown_var'
 *   }
 *
 * All errors include source location (file, line, column) for easy debugging.
 * An internal assertion may be used in a constant expression, and in a debug
 * build will print the backtrace of its caller.
 *
 *****************************************************************************/

#define credence_error(message) \
    (credence::detail::assert_fail(std::source_location::current(), message))

#define credence_compile_error(location, message, symbol, symbols) \
    (credence::detail::compile_error_impl(location, message, symbol, symbols))

#define credence_compile_error_with_type(     \
    location, message, symbol, symbols, type) \
    (credence::detail::compile_error_impl(    \
        location, message, symbol, symbols, type))

#define credence_assert(condition) \
    (credence::detail::assert_impl(std::source_location::current(), condition))

#define credence_assert_message(condition, message) \
    (credence::detail::assert_impl(                 \
        std::source_location::current(), condition, message))

#define credence_assert_message_trace(condition, message, location) \
    (credence::detail::assert_impl(location, condition, message))

#define credence_assert_equal(actual, expected) \
    (credence::detail::assert_equal_impl(       \
        std::source_location::current(), actual, expected))

#define credence_assert_nequal(actual, expected) \
    (credence::detail::assert_nequal_impl(       \
        std::source_location::current(), actual, expected))

namespace credence {

namespace detail {

class Credence_Exception : public std::exception
{
    std::string message_;

  public:
    template<typename... Args>
    explicit constexpr Credence_Exception(
        fmt::format_string<Args...> fmt_string,
        Args&&... args)
        : message_{ fmt::format(fmt_string, std::forward<Args>(args)...) }
    {
    }

    [[nodiscard]] const char* what() const noexcept override
    {
        return message_.c_str();
    }
};

inline void compile_error_impl(
    [[maybe_unused]] std::source_location const& location,
    std::string_view message,
    std::string_view symbol_name,
    easyjson::JSON const& symbols,
    std::string_view type = "symbol")
{
    auto symbol = symbol_name.data();
#ifndef DEBUG
    if (symbols.has_key(symbol) and type == "symbol")
        throw Credence_Exception("\n  Credence could not compile "
                                 "source:\n    on {} '{}'\n    "
                                 "with: "
                                 "\"{}\"\n  > from line {} column {}:{}",
            type,
            symbol,
            message,
            symbols[symbol]["line"].to_int(),
            symbols[symbol]["column"].to_int(),
            symbols[symbol]["end_column"].to_int());
    else
        throw Credence_Exception("\n  Credence could not compile "
                                 "source:\n    on {} '{}'\n    "
                                 "with: {}",
            type,
            symbol,
            message);
#else
    if (symbols.has_key(symbol) and type == "symbol")
        throw Credence_Exception(
            "\n  Credence could not compile source:\n    on {} '{}'\n "
            "   "
            "with: "
            "\"{}\"\n  > from line {} column {}:{}\n\n\n >>> In file "
            "'{}'\n line {}\n   ::: '{}'\n",
            type,
            symbol,
            message,
            symbols[symbol]["line"].to_int(),
            symbols[symbol]["column"].to_int(),
            symbols[symbol]["end_column"].to_int(),
            location.file_name(),
            location.line(),
            location.function_name());
    else
        throw Credence_Exception("\n  Credence could not compile "
                                 "source:\n    on {} '{}'\n    "
                                 "with: \"{}\""
                                 "\n\n\n >>> In file "
                                 "'{}'\n line {}\n   ::: '{}'\n",
            type,
            symbol,
            message,
            location.file_name(),
            location.line(),
            location.function_name());
#endif
}

#ifdef CREDENCE_BACKTRACE

/**
 * @brief Demangle the first `_Z` symbol in a frame of backtrace_symbols, where
 * a frame without a mangled name is returned unchanged
 *
 * Note: the symbol ends at a space, `+`, or `)`, since a frame is
 * `main + 24` on macOS and `credence(main+0x18)` on Linux
 */
inline std::string demangle_frame(std::string_view frame)
{
    auto start = frame.find("_Z");
    if (start == std::string_view::npos)
        return std::string{ frame };
    auto end = frame.find_first_of(" +)", start);
    auto mangled = std::string{ frame.substr(start, end - start) };
    int status = 0;
    char* name =
        abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
    if (status != 0 or name == nullptr)
        return std::string{ frame };
    auto demangled = fmt::format("{}{}{}",
        frame.substr(0, start),
        name,
        end == std::string_view::npos ? "" : frame.substr(end));
    std::free(static_cast<void*>(name));
    return demangled;
}

#endif

/**
 * @brief Get the call stack of an internal assertion in a debug build, without
 * the frames of the assertion itself in credence::detail
 *
 * Note: the backtrace is empty in a release build, or on a platform without
 * execinfo.h
 */
inline std::string get_backtrace()
{
#ifdef CREDENCE_BACKTRACE
    std::array<void*, 64> frames{};
    auto size = ::backtrace(frames.data(), static_cast<int>(frames.size()));
    auto* symbols = ::backtrace_symbols(frames.data(), size);
    if (symbols == nullptr)
        return {};
    auto trace = std::string{ "\n    Backtrace:\n" };
    for (int i = 0; i < size; i++) {
        auto frame = demangle_frame(symbols[i]);
        if (not util::contains(frame, "credence::detail::"))
            trace += fmt::format("      {}\n", frame);
    }
    std::free(static_cast<void*>(symbols));
    return trace;
#else
    return {};
#endif
}

/**
 * @brief Throw an internal assertion failure with its location, message, and
 * backtrace
 */
inline void assert_fail(std::source_location const& location,
    std::string_view message = "")
{
    if (message.empty())
        throw Credence_Exception(
            "\n    Assertion failed in '{}'\n    at line {}\n  ::: '{}'\n{}",
            location.file_name(),
            location.line(),
            location.function_name(),
            get_backtrace());
    throw Credence_Exception("\n    Assertion failed in '{}'\n    at line "
                             "{}\n  ::: '{}'\n    with '{}'\n{}",
        location.file_name(),
        location.line(),
        location.function_name(),
        message,
        get_backtrace());
}

/**
 * @brief An internal assertion that may be used in a constant expression,
 * where a failed assertion during constant evaluation is a compile error
 * since the failure itself is not a constant expression
 */
constexpr void assert_impl(std::source_location const& location,
    bool condition,
    std::string_view message = "")
{
    if (not condition)
        assert_fail(location, message);
}

template<typename T1, typename T2>
constexpr inline void assert_equal_impl(std::source_location const& location,
    const T1& actual,
    const T2& expected)
{
    assert_impl(location,
        actual == expected,
        fmt::format("expected '{}' to equal '{}'",
            util::to_constexpr_string(actual),
            util::to_constexpr_string(expected)));
}

template<typename T1, typename T2>
constexpr inline void assert_nequal_impl(std::source_location const& location,
    const T1& actual,
    const T2& expected)
{
    assert_impl(location,
        actual != expected,
        fmt::format("expected '{}' to not equal '{}'",
            util::to_constexpr_string(actual),
            util::to_constexpr_string(expected)));
}

} // namespace detail

inline void throw_compiletime_error(std::string_view message,
    std::string_view symbol,
    std::source_location const& location = std::source_location::current(),
    std::string_view type_ = "symbol",
    std::string_view scope = "main",
    util::AST_Node const& symbols = util::AST::object())
{
    detail::compile_error_impl(location,
        fmt::format("{} in function '{}'", message, scope),
        symbol,
        symbols,
        type_);
}

} // namespace credence