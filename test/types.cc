#include <doctest/doctest.h> // for ResultBuilder, CHECK, TestCase, TEST_CASE

#include <credence/error.h> // for Credence_Exception
#include <credence/types.h> // for is_valid_identifier, get_subscript_sections
#include <string_view>      // for basic_string_view, string_view
#include <utility>          // for pair

using namespace credence::type;
using namespace std::literals;

static_assert(is_dot_notation_object_identifier("v.pair[0]"));
static_assert(get_union_tag_offset("v.pair[1]") == "1");

TEST_CASE("types.h: is_valid_identifier")
{
    CHECK(is_valid_identifier("value_t"));
    CHECK(is_valid_identifier("_t1"));
    CHECK(is_valid_identifier("x"));
    CHECK(not is_valid_identifier(""));
    CHECK(not is_valid_identifier("1x"));
    CHECK(not is_valid_identifier("v.text"));
    CHECK(not is_valid_identifier("x[0]"));
}

TEST_CASE("types.h: get_subscript_sections")
{
    using Sections = std::pair<std::string_view, std::string_view>;
    CHECK(get_subscript_sections("v.pair[0]") == Sections{ "v.pair", "0" });
    CHECK(get_subscript_sections("v.pair[n]") == Sections{ "v.pair", "n" });
    CHECK(get_subscript_sections("v.text") == Sections{ "v.text", "" });
    CHECK(get_subscript_sections("v.pair[0") == Sections{ "v.pair[0", "" });
    CHECK(get_subscript_sections("x[0][1]") == Sections{ "x[0][1]", "" });
}

TEST_CASE("types.h: is_valid_offset")
{
    CHECK(is_valid_offset("0"));
    CHECK(is_valid_offset("10"));
    CHECK(is_valid_offset("n"));
    CHECK(not is_valid_offset(""));
    CHECK(not is_valid_offset("1x"));
}

TEST_CASE("types.h: is_dot_notation_object_identifier")
{
    CHECK(is_dot_notation_object_identifier("value_t.text"));
    CHECK(is_dot_notation_object_identifier("v.text"));
    CHECK(is_dot_notation_object_identifier("v.pair[0]"));
    CHECK(is_dot_notation_object_identifier("v.pair[n]"));
    CHECK(not is_dot_notation_object_identifier("x"));
    CHECK(not is_dot_notation_object_identifier("x[0]"));
    CHECK(not is_dot_notation_object_identifier(".text"));
    CHECK(not is_dot_notation_object_identifier("v."));
    CHECK(not is_dot_notation_object_identifier("2.5"));
    CHECK(not is_dot_notation_object_identifier("v.pair[]"));
    CHECK(not is_dot_notation_object_identifier("v.pair[0"));
    CHECK(not is_dot_notation_object_identifier("v.pair[0][1]"));
    CHECK(not is_dot_notation_object_identifier("v.pair[1x]"));
    CHECK(not is_dot_notation_object_identifier("v[0].pair"));
    CHECK(not is_dot_notation_object_identifier("(\"a.b\":string:3)"));
}

TEST_CASE("types.h: get_union_identifier_sections")
{
    using Sections = std::pair<std::string_view, std::string_view>;
    CHECK(get_union_identifier_sections("value_t.text") ==
          Sections{ "value_t", "text" });
    CHECK(
        get_union_identifier_sections("v.pair[1]") == Sections{ "v", "pair" });
    CHECK_THROWS_AS(static_cast<void>(get_union_identifier_sections("x")),
        credence::detail::Credence_Exception);
}

TEST_CASE("types.h: get_union_tag_offset")
{
    CHECK(get_union_tag_offset("v.pair[1]") == "1");
    CHECK(get_union_tag_offset("v.pair[n]") == "n");
    CHECK(get_union_tag_offset("v.text").empty());
    CHECK_THROWS_AS(static_cast<void>(get_union_tag_offset("x[0]")),
        credence::detail::Credence_Exception);
}
