#include <doctest/doctest.h> // for ResultBuilder, REQUIRE, TestCase

#include <array>                // for array
#include <credence/ir/binder.h> // for Binder, emit
#include <credence/ir/ita.h>    // for Instruction, Quadruple, emit_to, emit
#include <credence/language/hir/analyzer.h> // for Analyzer
#include <credence/map.h>                   // for Ordered_Map
#include <credence/object/table.h>          // for Object, Function, Vector
#include <credence/symbol.h>                // for Symbol_Table
#include <credence/types.h> // for is_unary_expression, get_unary_operator
#include <credence/util.h>  // for AST_Node, STRINGIFY, AST
#include <cstddef>          // for size_t
#include <deque>            // for deque
#include <easyjson.h>       // for JSON, object
#include <filesystem>       // for path
#include <format>           // for format
#include <initializer_list> // for initializer_list
#include <map>              // for map
#include <memory>           // for allocator, shared_ptr, make_shared
#include <sstream>          // for basic_ostringstream, ostringstream
#include <string>           // for basic_string, char_traits, string
#include <string_view>      // for basic_string_view
#include <test/fixture.h>   // for load_json_fixture
#include <tuple>            // for get, tuple
#include <utility>          // for get

namespace fs = std::filesystem;

#define ROOT_PATH STRINGIFY(ROOT_TEST_PATH)

#define EMIT(os, inst) credence::ir::detail::emit_to(os, inst)

inline std::filesystem::path get_root_path()
{
    if (const char* env_root = std::getenv("CREDENCE_TEST_ROOT"))
        return fs::path(env_root);
    return fs::path(ROOT_PATH);
}

#define SETUP_TABLE_FIXTURE_AND_TEST_FROM_AST(ast_path, expected)            \
    do {                                                                     \
        using namespace credence::ir;                                        \
        auto test = std::ostringstream{};                                    \
        auto fixture_path = get_root_path();                                 \
        fixture_path.append("test/fixtures/ast");                            \
        auto file_path =                                                     \
            fs::path(fixture_path).append(fmt::format("{}.json", ast_path)); \
        auto fixture_content =                                               \
            easyjson::JSON::load_file(file_path.string()).to_deque();        \
        credence::ir::emit(test, fixture_content[0], fixture_content[1]);    \
        REQUIRE(test.str() == (expected));                                   \
    } while (0)

struct Binder_Fixture
{
    using NULL_symbols = std::deque<std::string>;
    using Node = credence::util::AST_Node;
    const Node VECTOR_SYMBOLS =
        credence::test::load_json_fixture("ir/binder/vector_symbols");
    inline auto make_node() { return credence::util::AST::object(); }
    static inline credence::ir::Binder make_binder(Node const& symbols)
    {
        return credence::ir::Binder{ symbols };
    }
    static inline credence::ir::Binder make_binder_with_global_symbols(
        Node const& node,
        Node const& symbols)
    {
        using namespace credence::ir;

        auto objects = credence::language::hir::Analyzer::build(symbols, node);
        auto instructions = ITA::make_ita_instructions(symbols, node);
        auto binder = Binder{ objects, instructions };
        binder.bind();
        return binder;
    }
    static inline credence::ir::Binder make_binder_with_frame(
        Node const& symbols)
    {
        auto binder = credence::ir::Binder{ symbols };

        binder.get_objects()->set_function_definition("main");
        binder.instruction_index = 1;
        binder.from_func_start_ita_instruction("__main()");
        return binder;
    };
};

TEST_CASE_FIXTURE(Binder_Fixture, "from_globl_ita_instruction")
{
    auto binder = make_binder_with_frame(VECTOR_SYMBOLS);
    auto table = binder.get_objects();

    table->set_vector_definition("mess", 10);
    REQUIRE_NOTHROW(binder.from_globl_ita_instruction("mess"));
    REQUIRE(binder.get_stack_frame().locals.is_defined("mess"));
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_locl_ita_instruction")
{
    auto binder = make_binder_with_frame(VECTOR_SYMBOLS);
    auto table = binder.get_objects();
    auto& locals = binder.get_stack_frame().locals;
    auto test1 = credence::ir::Quadruple{
        credence::ir::Instruction::LOCL, "snide", "", ""
    };
    auto test2 = credence::ir::Quadruple{
        credence::ir::Instruction::LOCL, "*ptr", "", ""
    };
    REQUIRE_NOTHROW(binder.from_locl_ita_instruction(test1));
    REQUIRE_NOTHROW(binder.from_locl_ita_instruction(test2));
    REQUIRE(locals.table_.contains("snide"));
    REQUIRE(locals.addr_.contains("ptr"));
}

TEST_CASE_FIXTURE(Binder_Fixture, "build_vector_definitions_from_globals")
{
    auto ast = credence::test::load_json_fixture(
        "ir/binder/vector_definitions_from_globals");
    auto binder = make_binder_with_global_symbols(ast, VECTOR_SYMBOLS);
    auto table = binder.get_objects();
    auto& locals = table->get_stack_frame("main").locals;
    locals.set_symbol_by_name("mess", credence::type::NULL_RVALUE_LITERAL);
    locals.set_symbol_by_name("putchar", credence::type::NULL_RVALUE_LITERAL);
    locals.set_symbol_by_name("unit", credence::type::NULL_RVALUE_LITERAL);
    REQUIRE(table->get_vectors().size() == 4);
    REQUIRE(table->get_vector("mess").data.size() == 6);
    REQUIRE(table->get_vector("putchar").data.size() == 1);
    REQUIRE(table->get_vector("putchar").data["0"].value == "puts");
    REQUIRE(table->get_vector("unit").data["0"].value == "10");
    REQUIRE(table->get_vector("unit").data["0"].type == "int");
    REQUIRE(table->get_vector("mess").data["0"].value == "too bad");
    REQUIRE(table->get_vector("mess").data["1"].value == "tough luck");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_label_ita_instruction")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    binder.instruction_index = 5;
    auto test1 = credence::ir::Quadruple{
        credence::ir::Instruction::LABEL, "_L1", "", ""
    };
    REQUIRE_NOTHROW(binder.from_label_ita_instruction(test1));
    REQUIRE(frame.labels.contains("_L1"));
    REQUIRE(
        binder.label_addresses.at(binder.frame_symbol_).addr_["_L1"] == 5UL);
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_mov_ita_instruction")
{
    auto binder = make_binder_with_frame(VECTOR_SYMBOLS);
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    auto test1 = credence::ir::Quadruple{
        credence::ir::Instruction::MOV, "_t1", "(5:int:4)", ""
    };
    auto test2 = credence::ir::Quadruple{
        credence::ir::Instruction::MOV, "a", "(10:int:4)", ""
    };
    auto test3 =
        credence::ir::Quadruple{ credence::ir::Instruction::MOV, "z", "a", "" };
    auto test4 = credence::ir::Quadruple{
        credence::ir::Instruction::MOV, "z", "--", "z"
    };
    frame.locals.set_symbol_by_name("z", credence::type::NULL_RVALUE_LITERAL);
    binder.from_mov_ita_instruction(test1);
    binder.from_mov_ita_instruction(test2);
    binder.objects_->hoisted_symbols()["a"]["type"] = "lvalue";
    binder.from_mov_ita_instruction(test3);
    binder.from_mov_ita_instruction(test4);
    REQUIRE(frame.temporary["_t1"] == "(5:int:4)");
    REQUIRE(frame.locals.get_symbol_by_name("a") ==
            credence::type::Typed_RValue{ "10", "int", 4UL });
    REQUIRE(frame.locals.get_symbol_by_name("z") ==
            credence::type::Typed_RValue{ "10", "int", 4UL });
    REQUIRE(binder.allocations.at(binder.frame_symbol_) == 8);
}

TEST_CASE_FIXTURE(Binder_Fixture, "bind_vector")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& vector = table->set_vector_definition("v", 3);
    vector.data["0"] = { "4", "int", 4UL };
    // the left-hand-side inserts into the vector it copies from
    REQUIRE_NOTHROW(binder.bind_vector("v[1]", "v[0]"));
    REQUIRE(
        vector.data["1"] == credence::type::Typed_RValue{ "4", "int", 4UL });
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_func_start_ita_instruction")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    table->set_function_definition("convert");
    binder.instruction_index = 10;
    binder.from_func_start_ita_instruction("__convert(x,y,z)");
    REQUIRE(&binder.get_stack_frame() == &table->get_stack_frame("convert"));
    REQUIRE(binder.address_table.addr_["__convert(x,y,z)"] == 9UL);
    REQUIRE(binder.get_stack_frame().address_location[0] == 11);
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_return_instruction")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    auto test1 = credence::ir::Quadruple{
        credence::ir::Instruction::RETURN, "(1:int:4)", "", ""
    };
    auto test2 = credence::ir::Quadruple{
        credence::ir::Instruction::RETURN, "(2:int:4)", "", ""
    };
    binder.from_return_instruction(test1);
    REQUIRE(frame.ret.value().second == "(1:int:4)");
    // the last return statement is the return value for callers
    REQUIRE_NOTHROW(binder.from_return_instruction(test2));
    REQUIRE(frame.ret.value().first == "2");
    REQUIRE(frame.ret.value().second == "(2:int:4)");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_func_end_ita_instruction")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    auto& locals = binder.get_stack_frame().locals;
    binder.instruction_index = 10;
    table->get_stack_frame("main").locals.table_["x"] = { "10", "int", 4UL };
    binder.instruction_index = 10;
    frame.parameters.emplace_back("x");
    binder.from_func_end_ita_instruction();
    REQUIRE(table->get_stack_frame("main").address_location[1] == 9);
    REQUIRE(locals.table_.contains("x"));
}

TEST_CASE_FIXTURE(Binder_Fixture, "set_parameters_from_symbolic_label")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    frame.set_parameters_from_symbolic_label("__main(x,y,z,j)");
    REQUIRE(frame.parameters.size() == 4);
    REQUIRE(frame.parameters[0] == "x");
    REQUIRE(frame.parameters[1] == "y");
    REQUIRE(frame.parameters[2] == "z");
    REQUIRE(frame.parameters[3] == "j");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_push_instruction")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto push_instruction = credence::ir::Quadruple{
        credence::ir::Instruction::PUSH, "_p1", "", ""
    };
    auto push_instruction2 = credence::ir::Quadruple{
        credence::ir::Instruction::PUSH, "_p2", "", ""
    };
    table->get_stack_frame("main").temporary["_p1"] = "_p1";
    table->get_stack_frame("main").temporary["_p2"] = "_p2";
    binder.from_push_instruction(push_instruction);
    REQUIRE(binder.stack.size() == 1);
    REQUIRE(binder.stack.back() == "_p1");
    binder.from_push_instruction(push_instruction2);
    REQUIRE(binder.stack.size() == 2);
    REQUIRE(binder.stack.back() == "_p2");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_rvalue_unary_expression")
{
    using std::get;
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    table->get_stack_frame("main").locals.table_["a"] = { "5", "int", 4UL };
    table->get_stack_frame("main").locals.addr_["b"] = "a";
    table->get_stack_frame("main").locals.table_["c"] = { "5", "int", 4UL };
    std::string test_rvalue = "~ 5";
    std::string test_pointer2 = "a";
    std::string test_pointer3 = "*b";
    REQUIRE_NOTHROW(binder.from_rvalue_unary_expression("c", test_rvalue, "~"));
    auto test = binder.from_rvalue_unary_expression("--a", test_rvalue, "--");
    auto test2 = binder.from_rvalue_unary_expression("a++", test_rvalue, "++");
    auto test3 = binder.from_rvalue_unary_expression("+a", test_rvalue, "+");
    auto test4 = binder.from_rvalue_unary_expression("b", test_pointer2, "&");
    auto test5 =
        binder.from_rvalue_unary_expression("a", test_pointer3, test_pointer3);
    REQUIRE(test.value == "5");
    REQUIRE(test2.value == "5");
    REQUIRE(test3.value == "5");
    REQUIRE(test4.value == "b");
    REQUIRE(test5.value == "5");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_rvalue_binary_expression")
{
    auto table = make_binder_with_frame(make_node());
    auto test1 = credence::type::from_rvalue_binary_expression("5 || 10");
    auto test2 = credence::type::from_rvalue_binary_expression("_t1 && _t2");
    auto test3 = credence::type::from_rvalue_binary_expression("~_t1 + *_t2");
    REQUIRE(test1 == credence::type::Binary_Expression{ "5", "10", "||" });
    REQUIRE(test2 == credence::type::Binary_Expression{ "_t1", "_t2", "&&" });
    REQUIRE(test3 == credence::type::Binary_Expression{ "~_t1", "*_t2", "+" });
}

TEST_CASE_FIXTURE(Binder_Fixture, "lvalue_at_temporary_object_address")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    binder.get_stack_frame().temporary["_t1"] = "100";
    binder.get_stack_frame().temporary["_t2"] = "5";
    binder.get_stack_frame().temporary["_t3"] = "_t2";
    binder.get_stack_frame().temporary["_t4"] = "_t3";
    binder.get_stack_frame().temporary["_t5"] = "10";
    binder.get_stack_frame().temporary["_t6"] = "_t4 || _t5";
    REQUIRE(credence::object::lvalue_at_temporary_object_address(
                "_t1", frame) == "100");
    REQUIRE(credence::object::lvalue_at_temporary_object_address(
                "_t4", frame) == "5");
    REQUIRE(credence::object::lvalue_at_temporary_object_address(
                "_t6", frame) == "_t4 || _t5");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_temporary_reassignment")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& locals = binder.get_stack_frame().locals;
    REQUIRE_NOTHROW(binder.from_temporary_reassignment("_t1", "~ _t2"));
    binder.get_stack_frame().temporary["_t1"] = "100";
    binder.get_stack_frame().temporary["_t2"] = "5";
    binder.from_temporary_reassignment("_t1", "50");
    binder.from_temporary_reassignment("_t2", "~ _t1");
    binder.from_temporary_reassignment("_t3", "_t1");
    binder.from_temporary_reassignment("_t4", "_t1 || _t2");
    locals = table->get_stack_frame("main").locals;
    auto test = locals.get_symbol_by_name("_t1");
    auto test2 = locals.get_symbol_by_name("_t2");
    auto test3 = locals.get_symbol_by_name("_t4");
    REQUIRE(test.value == "50");
    REQUIRE(test2.value == "~ _t1");
    REQUIRE(test3.value == "_t1 || _t2");
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_scaler_symbol_assignment")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& frame = binder.get_stack_frame();
    auto& locals = binder.get_stack_frame().locals;
    locals.table_["a"] = { "5", "int", 4UL };
    locals.table_["c"] = { "6", "int", 4UL };
    binder.from_scaler_symbol_assignment("a", "c");
    REQUIRE(locals.table_["a"].value == "6");
    // a parameter has no storage in the table, so its value is a word
    frame.parameters.emplace_back("p");
    binder.from_scaler_symbol_assignment("a", "p");
    REQUIRE(locals.table_["a"] ==
            credence::type::Typed_RValue{ "p", "word", sizeof(void*) });
}

TEST_CASE_FIXTURE(Binder_Fixture, "from_integral_unary_expression")
{
    auto binder = make_binder_with_frame(make_node());
    auto table = binder.get_objects();
    auto& locals = binder.get_stack_frame().locals;
    credence::type::Typed_RValue expected1 = { "5", "int", 4UL };
    credence::type::Typed_RValue expected2 = { "5", "long", 8UL };
    credence::type::Typed_RValue expected3 = { "5", "double", 8UL };
    locals.set_symbol_by_name("a", expected1);
    locals.set_symbol_by_name("b", expected2);
    locals.set_symbol_by_name("c", expected3);
    binder.get_stack_frame().parameters.emplace_back("p");
    auto test = binder.from_integral_unary_expression("a");
    auto test2 = binder.from_integral_unary_expression("b");
    auto test3 = binder.from_integral_unary_expression("c");
    auto test4 = binder.from_integral_unary_expression("p");
    REQUIRE(test == expected1);
    REQUIRE(test2 == expected2);
    REQUIRE(test3 == expected3);
    REQUIRE(
        test4 == credence::type::Typed_RValue{ "p", "word", sizeof(void*) });
}

TEST_CASE_FIXTURE(Binder_Fixture, "is_unary")
{
    auto table = make_binder(make_node());
    auto test1 = credence::type::is_unary_expression("*k");
    auto test2 = credence::type::is_unary_expression("!x");
    auto test3 = credence::type::is_unary_expression("~1000");
    auto test4 = credence::type::is_unary_expression("&z_1");
    auto test5 = credence::type::is_unary_expression("-100");
    auto test6 = credence::type::is_unary_expression("u++");
    auto test7 = credence::type::is_unary_expression("--u");
    auto test8 = credence::type::is_unary_expression("u");
    auto test9 = credence::type::is_unary_expression("500");
    auto test10 = credence::type::is_unary_expression("k[20]");

    REQUIRE(test1 == true);
    REQUIRE(test2 == true);
    REQUIRE(test3 == true);
    REQUIRE(test4 == true);
    REQUIRE(test5 == true);
    REQUIRE(test6 == true);
    REQUIRE(test7 == true);
    REQUIRE(test8 == false);
    REQUIRE(test9 == false);
    REQUIRE(test10 == false);
}

TEST_CASE_FIXTURE(Binder_Fixture, "get_unary")
{
    auto table = make_binder(make_node());
    auto test1 = credence::type::get_unary_operator("*k");
    auto test2 = credence::type::get_unary_operator("!x");
    auto test3 = credence::type::get_unary_operator("~1000");
    auto test4 = credence::type::get_unary_operator("&z_1");
    auto test5 = credence::type::get_unary_operator("-100");
    auto test6 = credence::type::get_unary_operator("u++");
    auto test7 = credence::type::get_unary_operator("--u");

    REQUIRE(test1 == "*");
    REQUIRE(test2 == "!");
    REQUIRE(test3 == "~");
    REQUIRE(test4 == "&");
    REQUIRE(test5 == "-");
    REQUIRE(test6 == "++");
    REQUIRE(test7 == "--");
}

TEST_CASE_FIXTURE(Binder_Fixture, "get_unary_rvalue_reference")
{
    auto table = make_binder(make_node());
    auto test1 = credence::type::get_unary_rvalue_reference("*k");
    auto test2 = credence::type::get_unary_rvalue_reference("!x");
    auto test3 = credence::type::get_unary_rvalue_reference("~1000");
    auto test4 = credence::type::get_unary_rvalue_reference("&z_1");
    auto test5 = credence::type::get_unary_rvalue_reference("-100");
    auto test6 = credence::type::get_unary_rvalue_reference("u++");

    REQUIRE(test1 == "k");
    REQUIRE(test2 == "x");
    REQUIRE(test3 == "1000");
    REQUIRE(test4 == "z_1");
    REQUIRE(test5 == "100");
    REQUIRE(test6 == "u");
}

TEST_CASE_FIXTURE(Binder_Fixture, "floats and doubles")
{
    std::string expected = R"ita(__main():
 BeginFunc ;
    LOCL x;
    _p2_1 = ("%s %f %d %g %b %c":string:17);
    _p3_2 = ("hello":string:5);
    _p4_3 = (5.55:float:4);
    _p5_4 = (10:int:4);
    _p6_5 = (3.14155:double:8);
    _p7_6 = (1:int:4);
    _p8_7 = ('120':char:1);
    PUSH _p8_7;
    PUSH _p7_6;
    PUSH _p6_5;
    PUSH _p5_4;
    PUSH _p4_3;
    PUSH _p3_2;
    PUSH _p2_1;
    CALL printf;
    POP 56;
_L1:
    LEAVE;
 EndFunc ;


__printf():
 BeginFunc ;
_L1:
    LEAVE;
 EndFunc ;


__print():
 BeginFunc ;
_L1:
    LEAVE;
 EndFunc ;

)ita";
    SETUP_TABLE_FIXTURE_AND_TEST_FROM_AST("floats", expected);
}

TEST_CASE("get_typed_rvalue_from_string")
{
    auto [test1_1, test1_2, test1_3] =
        credence::type::get_typed_rvalue_from_string("(10:int:4)");
    auto [test2_1, test2_2, test2_3] =
        credence::type::get_typed_rvalue_from_string(
            std::format("(10.005:float:{})", sizeof(float)));
    auto [test3_1, test3_2, test3_3] =
        credence::type::get_typed_rvalue_from_string(
            std::format("(10.000000000000000005:double:{})", sizeof(double)));
    auto [test4_1, test4_2, test4_3] =
        credence::type::get_typed_rvalue_from_string(
            std::format("('0':byte:{})", sizeof(char)));
    auto [test5_1, test5_2, test5_3] =
        credence::type::get_typed_rvalue_from_string(
            std::format("(__WORD__:word:{})", sizeof(void*)));
    auto [test6_1, test6_2, test6_3] =
        credence::type::get_typed_rvalue_from_string(
            std::format("(\"hello this is a very long string\":string:{})",
                std::string{ "hello this is a very long string" }.size()));

    REQUIRE(test1_1 == "10");
    REQUIRE(test1_2 == "int");
    REQUIRE(test1_3 == 4UL);

    REQUIRE(test2_1 == "10.005");
    REQUIRE(test2_2 == "float");
    REQUIRE(test2_3 == sizeof(float));

    REQUIRE(test3_1 == "10.000000000000000005");
    REQUIRE(test3_2 == "double");
    REQUIRE(test3_3 == sizeof(double));

    REQUIRE(test4_1 == "'0'");
    REQUIRE(test4_2 == "byte");
    REQUIRE(test4_3 == 1UL);

    REQUIRE(test5_1 == "__WORD__");
    REQUIRE(test5_2 == "word");
    REQUIRE(test5_3 == sizeof(void*));

    REQUIRE(test6_1 == "hello this is a very long string");
    REQUIRE(test6_2 == "string");
    REQUIRE(
        test6_3 == std::string{ "hello this is a very long string" }.size());
}