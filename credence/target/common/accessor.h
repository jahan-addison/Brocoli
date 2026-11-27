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

#include "stack_frame.h" // for Locals
#include "types.h"       // for RValue, LValue, Stack_Pointer, Table...
#include <concepts>      // for derived_from, same_as
#include <credence/object/table.h> // for Object, get_rvalue_at_lvalue_object_...
#include <credence/symbol.h>       // for Symbol_Table
#include <credence/types.h> // for from_lvalue_offset, get_rvalue_data_...
#include <credence/util.h>  // for overload
#include <cstddef>          // for size_t
#include <deque>            // for deque
#include <memory>           // for shared_ptr, unique_ptr
#include <optional>         // for optional
#include <string>           // for basic_string, char_traits, string
#include <string_view>      // for basic_string_view, operator==, strin...
#include <utility>          // for pair, exchange
#include <variant>          // for monostate, visit

namespace credence::target::common {
struct Flag_Accessor;
}

/****************************************************************************
 * Memory Accessors
 *
 * The platform-agnostic layers of each target's Memory_Accessor, a Layered
 * Aggregate that owns its layers as private members wired in declaration
 * order, so each depends only on the layers declared before it and an
 * inserter reaches each layer through its role such as `ir()` or `buffers()`,
 * and never through another layer.
 *
 *  Example:
 *
 *    main() {
 *      auto x;
 *      x = 5;
 *      print("hello");
 *    }
 *
 *  Becomes, on arm64:
 *
 *    ldr w10, [sp, #16]               // x is on the stack of the device
 *    mov w10, #5
 *    str w10, [sp, #16]
 *    adrp x0, ._L_str1__@PAGE         // the label of the buffer accessor
 *    add x0, x0, ._L_str1__@PAGEOFF
 *    mov w1, #5                       // the size of the buffer accessor
 *    bl _print
 *
 ****************************************************************************/

namespace credence::target::common::memory {

/**
 * @brief Context of each target's Memory_Accessor
 *
 * Owns the object table and the current function's stack frame, and is
 * constructed before any layer. Each target's Memory_Accessor adds its own
 * layers, and is shared by every inserter as a Memory_Access pointer
 */
class Memory_Accessor
{
  public:
    explicit Memory_Accessor(
        std::shared_ptr<object::Object_Table const> objects);
    ~Memory_Accessor();
    Memory_Accessor(const Memory_Accessor&) = delete;
    Memory_Accessor& operator=(const Memory_Accessor&) = delete;

    Stack_Frame& get_frame_in_memory();
    const Stack_Frame& get_frame_in_memory() const;

  private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

/**
 * @brief Storage device layer below the address, where each target decides if a
 * local is a register or a stack slot, reset at each `BeginFunc`
 *
 *  Example:
 *
 *    accessor_->devices().insert_lvalue_to_device("x");
 *    accessor_->devices().get_device_by_lvalue("x");   // w9 or [rbp - 4]
 */
template<typename T>
concept Device_Accessor_T = requires(T& devices, LValue const& lvalue) {
    devices.reset_storage_devices();
    devices.insert_lvalue_to_device(lvalue);
    { devices.is_lvalue_allocated_in_memory(lvalue) } -> std::same_as<bool>;
    devices.get_device_by_lvalue(lvalue);
    devices.get_word_size_from_lvalue(lvalue);
};

/**
 * @brief Roles of a target's Memory_Accessor, each handing out one private
 * layer, so an inserter reaches a layer only through its role
 *
 * Note: a target may add its own roles, and each target's `devices()` satisfies
 * Device_Accessor_T
 *
 *  Example:
 *
 *    accessor_->buffers().insert_string_literal(rvalue, label);
 */
template<typename T>
concept Memory_Accessor_T =
    std::derived_from<T, Memory_Accessor> and requires(T& accessor) {
        { accessor.devices() } -> Device_Accessor_T;
        accessor.state();
        accessor.ir();
        accessor.flags();
        accessor.registers();
        accessor.accumulator();
        accessor.vectors();
        accessor.addresses();
        accessor.buffers();
        accessor.instructions();
        accessor.stack();
    };

template<Memory_Accessor_T Accessor>
using Memory_Access = std::shared_ptr<Accessor>;

template<Enum_T Registers, Stack_T Stack, Pair_T Address_Result>
class Address_Accessor;

/**
 * @brief State an inserter passes to a later IR instruction
 *
 *  * the signal register, which holds a value for the next lvalue that reads
 *    it, such as a syscall result. Taking it resets it to the target's empty
 *    register
 *
 *  * the held temporary, and its register after the instruction that computes
 *    it, valid until the next instruction
 *
 *  * the relational jump, set when a relational expression inserted the next
 *    `IF`'s conditional jump
 *
 *  * the address assignment, set while an address-of expression is stored to
 *    an lvalue
 */
template<Enum_T Register>
class Instruction_State
{
  public:
    using Held_Temporary = std::optional<std::pair<LValue, Register>>;

  public:
    explicit Instruction_State(Register empty_register)
        : empty_register_(empty_register)
        , signal_register_(empty_register)
    {
    }

  public:
    void set_signal_register(Register signal) { signal_register_ = signal; }
    Register get_signal_register() const { return signal_register_; }
    bool has_signal_register() const
    {
        return signal_register_ != empty_register_;
    }
    Register take_signal_register()
    {
        return std::exchange(signal_register_, empty_register_);
    }

  public:
    void hold_temporary(LValue const& temporary, Register storage)
    {
        held_temporary_ = { temporary, storage };
    }
    Held_Temporary const& get_held_temporary() const { return held_temporary_; }
    void release_held_temporary() { held_temporary_.reset(); }

  public:
    void set_relational_jump() { relational_jump_ = true; }
    bool take_relational_jump()
    {
        return std::exchange(relational_jump_, false);
    }

  public:
    void set_address_assignment() { address_assignment_ = true; }
    bool take_address_assignment()
    {
        return std::exchange(address_assignment_, false);
    }

  private:
    Register empty_register_;
    Register signal_register_;
    Held_Temporary held_temporary_{};
    bool relational_jump_{ false };
    bool address_assignment_{ false };
};

/**
 * @brief Compile-time buffer sizes and data section literals
 *
 * A buffer argument to a library call or syscall, such as a `print` or
 * `write(2)` string, has its length passed in a register, known at compile time
 * from the object table. The last `read(2)` length is cached for the buffer it
 * filled.
 *
 * String, float, and double literals are emitted once in the data section,
 * and each literal is mapped to its label so every use loads the same
 * address.
 *
 *  Example:
 *
 *    x = "hello";
 *    print(x);
 *
 *  Becomes:
 *
 *    ._L_str1__:
 *        .asciz "hello"
 *
 *    get_string_address_offset("\"hello\"")   // ._L_str1__
 *    get_size_of_string_lvalue_buffer_address("x", frame)   // 5
 */
class Buffer_Accessor
{
  public:
    explicit Buffer_Accessor(Table_Pointer& table);
    ~Buffer_Accessor();

    Buffer_Accessor(const Buffer_Accessor&) = delete;
    Buffer_Accessor& operator=(const Buffer_Accessor&) = delete;

    Size get_size_of_string_lvalue_buffer_address(LValue const& lvalue,
        Stack_Frame const& stack_frame);

    void set_buffer_size_from_syscall(std::string_view routine,
        memory::Locals& argument_stack);

    bool has_read_bytes();

    std::size_t read_bytes();
    void insert_string_literal(RValue const& key, Label const& asciz_address);
    void insert_float_literal(RValue const& key, Label const& floatz_address);
    void insert_double_literal(RValue const& key, Label const& doublez_address);
    RValue get_string_address_offset(RValue const& string);

    RValue get_float_address_offset(RValue const& string);
    RValue get_double_address_offset(RValue const& string);
    bool is_allocated_string(RValue const& rvalue);
    bool is_allocated_float(RValue const& rvalue);
    bool is_allocated_double(RValue const& rvalue);

    std::size_t* get_constant_size_index();
    void set_constant_size_index(std::size_t index);

  private:
    Size get_size_in_local_address(LValue const& lvalue,
        Stack_Frame const& stack_frame);
    Size get_size_of_return_string(Stack_Frame const& stack_frame);

  private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

/**
 * @brief Access to the object table and the IR instruction under translation
 *
 * The visitor sets each IR instruction's index before it is translated, and the
 * inserters look behind and ahead of it. An inserter may emit different
 * instructions when the next IR instruction is an assignment or a temporary,
 * such as an address-of expression stored to a local.
 */
struct Table_Accessor
{
    Table_Accessor() = delete;
    explicit Table_Accessor(Table_Pointer& table,
        ir::Instructions_PTR& instructions);
    ~Table_Accessor();

    Table_Accessor(const Table_Accessor&) = delete;
    Table_Accessor& operator=(const Table_Accessor&) = delete;

    void set_ir_iterator_index(unsigned int index_);
    LValue get_last_lvalue_assignment(unsigned int index_,
        object::Function const& stack_frame);
    bool is_ir_instruction_temporary();
    LValue get_ir_instruction_lvalue();
    bool last_ir_instruction_is_assignment();
    bool next_ir_instruction_is_assignment();
    bool next_ir_instruction_is_temporary();

    Table_Pointer& get_table();
    const Table_Pointer& get_table() const;
    ir::Instructions& get_ir_instructions();
    unsigned int get_index() const;

  private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

/**
 * @brief A target's machine code instructions
 *
 * The inserters append to it in order, and the emitter writes it to the
 * text section after the function epilogues and labels are resolved.
 */
template<Deque_T T>
struct Instruction_Accessor
{
    T& get_instructions() { return instructions_; }

    auto push(T& instruction) { instructions_.emplace_back(instruction); }

    auto begin() { return instructions_.begin(); }
    auto end() { return instructions_.end(); }
    auto begin() const { return instructions_.begin(); }
    auto end() const { return instructions_.end(); }

    std::size_t size() { return instructions_.size(); }

  private:
    T instructions_{};
};

/**
 * @brief Global vector element offset and size
 *
 * A global vector is in the data section, so an element is the vector's address
 * plus its offset. The index is an integer literal or a local whose value is
 * known at compile time, and an index out of range is a compile-time error.
 *
 *  Example:
 *
 *    mess [3] "too bad", "tough luck", "that sucks";
 *    x = mess[1];
 *
 *  Becomes:
 *
 *    get_offset_address("mess[1]", "1")   // (8, Doubleword)
 */
template<typename Entry>
class Vector_Accessor
{
  public:
    using Entry_Size = Entry;

  public:
    explicit Vector_Accessor(Table_Pointer& table, Stack_Frame& stack_frame)
        : table_(table)
        , stack_frame_(stack_frame)
    {
    }

    virtual ~Vector_Accessor() = default;

    virtual Entry_Size get_size_from_vector_offset(
        Immediate const& immediate) = 0;

  public:
    using Entry_Pair = std::pair<object::Vector::Address, Entry>;
    using Offsets = Ordered_Map<Label, object::Vector::Address>;

    Entry_Pair get_offset_address(LValue const& lvalue, RValue const& offset);

  private:
    Entry_Pair get_offset_from_hoisted_symbols(LValue const& vector,
        RValue const& offset);
    void type_check_invalid_vector_symbol(LValue const& vector,
        RValue const& offset);
    Entry_Pair get_offset_from_integer_rvalue(LValue const& vector,
        RValue const& offset);
    Entry_Pair get_offset_from_trivial_vector(LValue const& vector);

  public:
    /**
     * @brief Set a global vector element's data section offset, as the emitter
     * lays out the data section
     */
    void set_address_offset(Label const& vector,
        Label const& index,
        object::Vector::Address address)
    {
        offsets_[vector].insert(index, address);
    }

  protected:
    Table_Pointer& table_;
    Stack_Frame& stack_frame_;

  private:
    Ordered_Map<Label, Offsets> offsets_{};
};

/**
 * @brief Accumulator register selection for an operand
 *
 * The accumulator holds an expression's value, as wide as the operand it is
 * loaded from: a word register for an `int`, a doubleword register for a
 * pointer or `long`.
 *
 *  Example:
 *
 *    get_accumulator_register_from_storage(Immediate{ "5", "int", 4 })
 *    // w8 on arm64, eax on x86-64
 */
template<typename T, Enum_T Register, Stack_T Stack>
struct Accumulator_Accessor
{
    explicit Accumulator_Accessor(Instruction_State<Register>& state)
        : state_(state)
    {
    }

    virtual ~Accumulator_Accessor() = default;

    /**
     * @brief Get the accumulator register from storage
     *
     * An empty storage is the first register of the target, and a register
     * is its own accumulator
     */
    constexpr Register get_accumulator_register_from_storage(
        Storage_T<Register> const& storage,
        Stack_Pointer<Stack>& stack)
    {
        Register accumulator{};
        std::visit(
            util::overload{ [&](std::monostate) {
                               accumulator = get_first_of_enum_t<Register>();
                           },
                [&](Stack_Offset const& offset) {
                    auto size = stack->get(offset).second;
                    accumulator = get_accumulator_register_from_size(size);
                },
                [&](Register const& device) { accumulator = device; },
                [&](Immediate const& immediate) {
                    auto size = get_operand_size_from_immediate(immediate);
                    accumulator = get_accumulator_register_from_size(size);
                } },
            storage);
        return accumulator;
    }

    virtual T get_operand_size_from_immediate(Immediate const& immediate) = 0;
    virtual Register get_accumulator_register_from_size(T size) = 0;

  protected:
    Instruction_State<Register>& state_;
};

/**
 * @brief Address resolution of lvalues on the stack and in the data section
 *
 * Each target derives the lvalue address and insertion instructions from it,
 * the instructions that load a vector element, a dereference, or a stack
 * local into a register before it is used.
 */
template<Enum_T Registers, Stack_T Stack, Pair_T Address_Result>
class Address_Accessor
{
  public:
    explicit Address_Accessor(Table_Pointer& table,
        Stack_Pointer<Stack>& stack,
        Flag_Accessor& flag_accessor,
        Stack_Frame& stack_frame)
        : table_(table)
        , stack_(stack)
        , flag_accessor_(flag_accessor)
        , stack_frame_(stack_frame)
    {
    }

    using Stack_Pointer_Type = Stack_Pointer<Stack>;
    using Address = Address_Result;
    using Storage = Storage_T<Registers>;

    /**
     * @brief Check the type of the value an lvalue holds
     *
     * An lvalue not in the object table is not the type, such as a
     * temporary or a register argument
     *
     *  Examples:
     *
     *    is_lvalue_storage_type("x", "string")   // true, x = "hello"
     *    is_lvalue_storage_type("_t1", "int")    // false
     */
    bool is_lvalue_storage_type(LValue const& lvalue,
        std::string_view type_check)
    {
        try {
            auto& frame = stack_frame_.get_stack_frame();
            return type::get_type_from_typed_rvalue(
                       type::get_typed_rvalue_as_string(
                           table_->get_rvalue_at_lvalue_object_storage(
                               lvalue, frame))) == type_check;
        } catch (...) {
            return false;
        }
    }

  protected:
    // a global vector is a pointer in the object table's globals
    Operand_Lambda is_vector = [&](RValue const& rvalue) {
        return table_->vector_contains(type::from_lvalue_offset(rvalue));
    };
    Operand_Lambda is_global_vector = [&](RValue const& rvalue) {
        auto rvalue_reference = type::from_lvalue_offset(rvalue);
        return table_->is_global_vector(rvalue_reference);
    };

  protected:
    Table_Pointer& table_;
    Stack_Pointer<Stack>& stack_;
    Flag_Accessor& flag_accessor_;
    Stack_Frame& stack_frame_;
};

/**
 * @brief A target's registers for locals and temporaries, where a signal
 * register in the instruction state is taken before the next available one
 */
template<Enum_T Register>
struct Register_Accessor
{
    explicit Register_Accessor(Instruction_State<Register>& state)
        : state_(state)
    {
    }
    using available_registers = std::deque<Register>;

  protected:
    Instruction_State<Register>& state_;
    std::deque<Register> d_size_registers;
    std::deque<Register> w_size_registers;
};

} // namespace credence::target::common::memory