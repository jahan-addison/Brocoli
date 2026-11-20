#include <doctest/doctest.h> // for ResultBuilder, CHECK, TestCase, TEST_CASE

#include <credence/map.h> // for Ordered_Map, Comparable_Key
#include <functional>     // for less
#include <stdexcept>      // for out_of_range
#include <string>         // for basic_string, string
#include <string_view>    // for basic_string_view, string_view
#include <utility>        // for pair
#include <vector>         // for vector

using namespace credence;
using namespace std::literals;

static_assert(Comparable_Key<std::string_view, std::string, std::less<>>);
static_assert(Comparable_Key<char const*, std::string, std::less<>>);
static_assert(not Comparable_Key<int, std::string, std::less<>>);

static_assert([] {
    Ordered_Map<std::string_view, int> map{};
    map.insert("third", 3);
    map.insert("first", 1);
    map["second"] = 2;
    return map.first().first == "third" and map.at("second") == 2;
}());

TEST_CASE("map.h: Ordered_Map::insert")
{
    Ordered_Map<std::string, int> map{};
    map.insert("third", 3);
    map.insert("first", 1);
    map.insert("second", 2);
    map.insert("third", 30);

    std::vector<std::pair<std::string, int>> entries{ map.begin(), map.end() };
    CHECK(entries == std::vector<std::pair<std::string, int>>{
                         { "third",  30 },
                         { "first",  1  },
                         { "second", 2  }
    });
    CHECK(map.size() == 3);
}

TEST_CASE("map.h: Ordered_Map::operator[]")
{
    Ordered_Map<std::string, int> map{};
    map["b"] = 2;
    map["a"] = 1;
    map["b"] += 10;

    CHECK(map.first().first == "b");
    CHECK(map.first().second == 12);
    CHECK(map.last().first == "a");
    CHECK(map["c"] == 0);
    CHECK(map.size() == 3);
}

TEST_CASE("map.h: Ordered_Map::at")
{
    Ordered_Map<std::string, int> map{};
    map.insert("x", 10);

    CHECK(map.at("x"sv) == 10);
    CHECK(map.at(std::string{ "x" }) == 10);
    CHECK_THROWS_AS(static_cast<void>(map.at("y")), std::out_of_range);

    auto const& constant = map;
    CHECK(constant.at("x") == 10);
}

TEST_CASE("map.h: Ordered_Map::find")
{
    Ordered_Map<std::string, int> map{};
    map.insert("x", 10);
    map.insert("y", 20);

    CHECK(map.find("y"sv)->second == 20);
    CHECK(map.find("z"sv) == map.end());
    CHECK(map.contains("x"));
    CHECK(not map.contains("z"sv));
}

TEST_CASE("map.h: Ordered_Map::prev")
{
    Ordered_Map<std::string, int> map{};
    map.insert("a", 1);
    CHECK(map.prev().first == "a");
    map.insert("b", 2);
    map.insert("c", 3);
    CHECK(map.prev().first == "b");
}

TEST_CASE("map.h: Ordered_Map::clear")
{
    Ordered_Map<std::string, int> map{};
    map.insert("a", 1);
    auto copy = map;
    map.clear();

    CHECK(map.empty());
    CHECK(not map.contains("a"));
    CHECK(copy.at("a") == 1);
    copy.insert("b", 2);
    CHECK(copy.rbegin()->first == "b");
}
