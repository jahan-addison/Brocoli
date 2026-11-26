#include <doctest/doctest.h> // for ResultBuilder, CHECK, TestCase, TEST_CASE

#include <credence/error.h>        // for Credence_Exception
#include <credence/object/table.h> // for Object_Table, Tagged_Union, Vector
#include <credence/types.h>        // for Typed_RValue
#include <initializer_list>        // for initializer_list
#include <string>                  // for basic_string, string, to_string
#include <variant>                 // for get

using namespace credence::object;
using credence::type::Typed_RValue;

namespace {

/**
 * @brief Make a tuple tag from the elements of a list of typed rvalues
 */
Vector make_tuple_tag(std::string const& label,
    std::initializer_list<Typed_RValue> elements)
{
    auto vector = Vector{ label, elements.size() };
    auto index = 0;
    for (auto const& element : elements)
        vector.data.insert(std::to_string(index++), element);
    return vector;
}

/**
 * @brief Set the tags of `value_t`, the union of examples/unions.b
 */
void set_value_t(Tagged_Union& value_t)
{
    value_t.set_tag_entry("integer", Typed_RValue{ "0", "int", 4 });
    value_t.set_tag_entry("real", Typed_RValue{ "0.0", "float", 4 });
    value_t.set_tag_entry("text", Typed_RValue{ "\"\"", "string", 0 });
    value_t.set_tag_entry("pair",
        make_tuple_tag("pair",
            {
                Typed_RValue{ "0", "int", 4 },
                Typed_RValue{ "0", "int", 4 }
    }));
}

} // namespace

TEST_CASE("object/table.h: get_storage_size")
{
    CHECK(get_storage_size(Typed_RValue{ "10", "int", 4 }) == 4);
    CHECK(get_storage_size(Typed_RValue{ "1.5", "double", 8 }) == 8);
    CHECK(get_storage_size(Typed_RValue{ "'a'", "char", 1 }) == 1);
    CHECK(get_storage_size(Typed_RValue{ "\"\"", "string", 0 }) ==
          k_pointer_size);
    CHECK(get_storage_size(Typed_RValue{ "\"hello\"", "string", 5 }) ==
          k_pointer_size);
}

TEST_CASE("object/table.h: Vector::size_of_data")
{
    CHECK(Vector{ "empty", 0 }.size_of_data() == 0);
    CHECK(
        make_tuple_tag("pair",
            {
                Typed_RValue{ "0", "int", 4 },
                Typed_RValue{ "0", "int", 4 }
    })
            .size_of_data() == 8);
    // unpadded, since alignment is left to each backend
    CHECK(make_tuple_tag("mixed",
              {
                  Typed_RValue{ "0",     "int",    4 },
                  Typed_RValue{ "0.0",   "double", 8 },
                  Typed_RValue{ "\"a\"", "string", 1 }
    })
              .size_of_data() == 20);
}

TEST_CASE("object/table.h: Vector::operator==")
{
    auto a = make_tuple_tag("pair",
        {
            Typed_RValue{ "1", "int", 4 }
    });
    auto b = make_tuple_tag("pair",
        {
            Typed_RValue{ "1", "int", 4 }
    });
    CHECK(a == b);
    b.data.insert("0", Typed_RValue{ "2", "int", 4 });
    CHECK(a != b);
}

TEST_CASE("object/table.cc: Object_Table::set_union_definition")
{
    Object_Table table{};
    auto& value_t = table.set_union_definition("value_t");
    CHECK(value_t.symbol == "value_t");
    CHECK(table.union_contains("value_t"));
    CHECK(&table.get_union("value_t") == &value_t);

    // each definition keeps its address as more definitions are inserted
    auto& option_t = table.set_union_definition("option_t");
    CHECK(&table.get_union("value_t") == &value_t);
    CHECK(&table.get_union("option_t") == &option_t);
}

TEST_CASE("object/table.cc: Tagged_Union::set_tag_entry")
{
    Tagged_Union value_t{ "value_t" };
    set_value_t(value_t);
    CHECK(value_t.entries.size() == 4);
    CHECK(std::get<Typed_RValue>(value_t.entries.at("integer")).type == "int");
    CHECK(std::get<Vector>(value_t.entries.at("pair")).data.size() == 2);

    value_t.set_tag_entry("integer", Typed_RValue{ "1", "long", 8 });
    CHECK(value_t.entries.size() == 4);
    CHECK(std::get<Typed_RValue>(value_t.entries.at("integer")).type == "long");
}

TEST_CASE("object/table.h: Tagged_Union::is_tag_entry")
{
    Tagged_Union value_t{ "value_t" };
    set_value_t(value_t);
    CHECK(value_t.is_tag_entry("text"));
    CHECK(value_t.is_tag_entry("pair"));
    CHECK(not value_t.is_tag_entry("list"));
}

TEST_CASE("object/table.cc: Tagged_Union::get_largest_tag_entry")
{
    Tagged_Union empty{ "empty_t" };
    CHECK(empty.get_largest_tag_entry() == 0);

    Tagged_Union scalar{ "scalar_t" };
    scalar.set_tag_entry("small", Typed_RValue{ "'a'", "char", 1 });
    scalar.set_tag_entry("large", Typed_RValue{ "0", "long", 8 });
    CHECK(scalar.get_largest_tag_entry() == 8);

    // a string is the size of its pointer and not the length of its literal
    Tagged_Union text{ "text_t" };
    text.set_tag_entry("text", Typed_RValue{ "\"\"", "string", 0 });
    CHECK(text.get_largest_tag_entry() == k_pointer_size);

    Tagged_Union value_t{ "value_t" };
    set_value_t(value_t);
    CHECK(value_t.get_largest_tag_entry() == 8);

    Tagged_Union triple{ "triple_t" };
    triple.set_tag_entry("one", Typed_RValue{ "0", "int", 4 });
    triple.set_tag_entry("three",
        make_tuple_tag("three",
            {
                Typed_RValue{ "0", "int", 4 },
                Typed_RValue{ "0", "int", 4 },
                Typed_RValue{ "0", "int", 4 }
    }));
    CHECK(triple.get_largest_tag_entry() == 12);
}

TEST_CASE("object/table.h: Tagged_Union::operator==")
{
    Tagged_Union a{ "value_t" };
    Tagged_Union b{ "value_t" };
    set_value_t(a);
    set_value_t(b);
    CHECK(a == b);

    b.set_tag_entry("pair", Typed_RValue{ "0", "int", 4 });
    CHECK(a != b);

    Tagged_Union c{ "other_t" };
    set_value_t(c);
    CHECK(a != c);
}

TEST_CASE("object/table.h: Object_Table::is_union")
{
    Object_Table table{};
    set_value_t(table.set_union_definition("value_t"));
    CHECK(table.is_union("value_t.text"));
    CHECK(table.is_union("value_t.pair"));
    CHECK(not table.is_union("value_t.list"));
    CHECK(not table.is_union("option_t.text"));
    CHECK(not table.is_union("value_t"));
    CHECK(not table.is_union("x"));
}

TEST_CASE("object/table.h: Object_Table::get_union_of_variable")
{
    Object_Table table{};
    auto& value_t = table.set_union_definition("value_t");
    auto& main = table.set_function_definition("main");
    main.union_table.insert("v", "value_t");
    table.set_union_global("last", "value_t");

    auto local = table.get_union_of_variable("v", main);
    REQUIRE(local.has_value());
    CHECK(&local->get() == &value_t);

    auto global = table.get_union_of_variable("last", main);
    REQUIRE(global.has_value());
    CHECK(&global->get() == &value_t);

    CHECK(not table.get_union_of_variable("w", main).has_value());
    CHECK(not table.get_union_of_variable("value_t", main).has_value());
}

TEST_CASE("object/table.h: Object_Table::is_union_variable")
{
    Object_Table table{};
    set_value_t(table.set_union_definition("value_t"));
    auto& main = table.set_function_definition("main");
    auto& other = table.set_function_definition("other");
    main.union_table.insert("v", "value_t");
    table.set_union_global("last", "value_t");

    CHECK(table.is_union_variable("v.text", main));
    CHECK(table.is_union_variable("v.pair[0]", main));
    CHECK(table.is_union_variable("v.pair[n]", main));
    CHECK(table.is_union_variable("last.real", main));
    CHECK(table.is_union_variable("last.real", other));
    CHECK(not table.is_union_variable("v.text", other));
    CHECK(not table.is_union_variable("v.list", main));
    CHECK(not table.is_union_variable("w.text", main));
    CHECK(not table.is_union_variable("value_t.text", main));
    CHECK(not table.is_union_variable("v", main));
    CHECK(not table.is_union_variable("x[0]", main));
}

TEST_CASE("object/table.h: Object_Table::get_stack_frame")
{
    Object_Table table{};
    auto& main = table.set_function_definition("main");
    CHECK(&table.get_stack_frame("main") == &main);
    CHECK_THROWS_AS(static_cast<void>(table.get_stack_frame("missing")),
        credence::detail::Credence_Exception);
}
