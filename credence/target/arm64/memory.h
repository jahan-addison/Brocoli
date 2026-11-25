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

#include "assembly.h"                           // for Register, Operand_Size
#include "stack.h"                              // for Stack
#include <credence/map.h>                       // for Ordered_Map
#include <credence/object.h>                    // for LValue, Size, RValue
#include <credence/target/common/accessor.h>    // for Accumulator_Accessor
#include <credence/target/common/flags.h>       // for Flag_Accessor
#include <credence/target/common/memory.h>      // for Operand_Type, is_imm...
#include <credence/target/common/stack_frame.h> // for Stack_Frame
#include <credence/target/common/types.h>       // for Storage_T
#include <credence/types.h>                     // for get_size_from_rvalue...
#include <credence/util.h>                      // for overload
#include <cstddef>                              // for size_t
#include <deque>                                // for deque
#include <functional>                           // for function
#include <memory>                               // for shared_ptr, make_shared
#include <optional>                             // for optional
#include <set>                                  // for set
#include <source_location>                      // for source_location
#include <string>                               // for basic_string, string
#include <utility>                              // for move

/****************************************************************************
 * ARM64 Memory Accessor
 *
 * The Memory_Accessor of the ARM64 target, and the device of each local:
 * a register in a frame with no calls, or a stack slot. A local whose
 * address is taken is always on the stack, since a register has no address.
 *
 *  Example:
 *
 *    main() {
 *      auto x, *p, y;
 *      x = 1;
 *      y = 2;
 *      p = &y;
 *    }
 *
 *  Becomes:
 *
 *    mov w9, #1             // x is in w9
 *    ldr w10, [sp, #16]     // y is on the stack, loaded into x10
 *    mov w10, #2
 *    str w10, [sp, #16]
 *    add x8, sp, #16        // the address of y
 *    mov x11, x8            // p is in x11
 *
 ****************************************************************************/

/****************************************************************************
 * Register selection table:
 *
 *   x0 - x7  = The arguments of a call, and x0 the return value
 *   x6       = The data section address register, and the second operand
 *              of a binary expression in a temporary
 *      s6    = floating point
 *      d6    = double
 *   x7       = The scratch register of the right-hand-side of a binary
 *              expression
 *   x8       = The default "accumulator" register for expression expansion
 *   x9, x11 - x14, x16, x17
 *            = If there are no function calls in a stack frame, local scope
 *              variables are stored in these registers, after which the
 *              stack is used
 *   x10      = The stack move register, and the address register of a
 *              pointer on the stack
 *   x15      = The address of a vector element on the stack
 *   x18      = The platform register on Darwin, never used
 *   x19      = The argc, argv scratch register
 *
 *   A local whose address is taken, vectors, and vector offsets are always
 *   on the stack
 *
 *****************************************************************************/

namespace credence::target::arm64::memory {
class Memory_Accessor;
namespace detail {
class Device_Accessor;
struct Instruction_Accessor;
}
}

namespace credence::target::arm64 {

using Register = assembly::Register;
using Directive = assembly::Directive;
using Mnemonic = assembly::Mnemonic;
using Storage = common::Storage_T<assembly::Register>;
using Operator_Symbol = std::string;
using Immediate = assembly::Immediate;
using Directives = assembly::Directives;

/**
 * Short-form helpers for matchit predicate pattern matching
 */
using common::memory::is_immediate;
using common::memory::is_parameter;
using common::memory::is_temporary;
using common::memory::is_vector_offset;

namespace memory {

using Operand_Type = common::memory::Operand_Type;

namespace registers {

using general_purpose = std::deque<Register>;

// the registers of locals in a frame with no calls, without the stack move
// register x10, the vector address register x15, and the Darwin platform
// register x18
const general_purpose available_doubleword = {
    Register::x9,
    Register::x11,
    Register::x12,
    Register::x13,
    Register::x14,
    Register::x16,
    Register::x17,
};
const general_purpose available_word = {
    Register::w9,
    Register::w11,
    Register::w12,
    Register::w13,
    Register::w14,
    Register::w16,
    Register::w17,
};

const general_purpose available_doubleword_argument = { Register::x0,
    Register::x1,
    Register::x2,
    Register::x3,
    Register::x4,
    Register::x5,
    Register::x6,
    Register::x7,
    Register::x8 };
const general_purpose available_word_argument = { Register::w0,
    Register::w1,
    Register::w2,
    Register::w3,
    Register::w4,
    Register::w5,
    Register::w6,
    Register::w7,
    Register::x8 };

} // namespace registers

using Memory_Access = std::shared_ptr<Memory_Accessor>;
using Instruction_Pointer = std::shared_ptr<detail::Instruction_Accessor>;
using Stack_Pointer = std::shared_ptr<assembly::Stack>;
using Table_Pointer = std::shared_ptr<object::Object_Table>;
using Stack_Frame = target::common::memory::Stack_Frame;

Register get_second_register_for_binary_operand(assembly::Operand_Size size);

bool is_doubleword_storage_size(assembly::Storage const& storage,
    Stack_Pointer& stack,
    Stack_Frame& stack_frame);

inline assembly::Operand_Size get_word_size_from_storage(
    assembly::Storage const& storage,
    Stack_Pointer& stack,
    Stack_Frame& stack_frame)
{
    return is_doubleword_storage_size(storage, stack, stack_frame)
               ? assembly::Operand_Size::Doubleword
               : assembly::Operand_Size::Word;
}

assembly::Operand_Size get_operand_size_from_storage(Storage const& storage,
    memory::Stack_Pointer& stack);

using ARM64_Memory_Accessor = common::memory::Memory_Accessor;

namespace detail {

class Device_Accessor;

using ARM64_Address_Accessor = common::memory::
    Address_Accessor<Register, assembly::Stack, assembly::Instruction_Pair>;

using ARM64_Accumulator_Accessor = common::memory::
    Accumulator_Accessor<assembly::Operand_Size, Register, assembly::Stack>;

using ARM64_Instruction_Accessor =
    common::memory::Instruction_Accessor<assembly::Instructions>;

using ARM64_Vector_Accessor = common::memory::Vector_Accessor<Size>;

using ARM64_Register_Accessor = common::memory::Register_Accessor<Register>;

using Instruction_State = common::memory::Instruction_State<Register>;

using Buffer_Accessor = common::memory::Buffer_Accessor;

using Table_Accessor = common::memory::Table_Accessor;

using Operand_Lambda = std::function<bool(RValue)>;

using Flag_Accessor = common::Flag_Accessor;

/**
 * @brief The accumulator register of ARM64, w8 or x8
 *
 * A signal register set by an inserter is returned once in place of the
 * accumulator
 */
struct Accumulator_Accessor : public ARM64_Accumulator_Accessor
{
    explicit Accumulator_Accessor(Instruction_State& state)
        : ARM64_Accumulator_Accessor(state)
    {
    }

    assembly::Operand_Size get_operand_size_from_immediate(
        Immediate const& immediate) override
    {
        return assembly::get_operand_size_from_typed_rvalue(immediate);
    }

    Register get_accumulator_register_from_size(
        assembly::Operand_Size size) override;
};

/**
 * @brief The ARM64 instructions of the program
 */
struct Instruction_Accessor : public ARM64_Instruction_Accessor
{};

/**
 * @brief The registers of locals, and the stack move register
 *
 * The registers of locals are reset at the end of each function, and the
 * scratch registers of an assignment are held until it is done
 */
struct Register_Accessor : public ARM64_Register_Accessor
{
    explicit Register_Accessor(Instruction_State& state)
        : ARM64_Register_Accessor(state)
    {
        d_size_registers = registers::available_doubleword;
        w_size_registers = registers::available_word;
    }

    Storage get_available_register(assembly::Operand_Size size);

    inline registers::general_purpose get_register_list_by_size(
        assembly::Operand_Size size)
    {
        if (size == assembly::Operand_Size::Doubleword)
            return d_size_registers;
        else
            return w_size_registers;
    }

    inline void reset_available_registers()
    {
        d_size_registers = registers::available_doubleword;
        w_size_registers = registers::available_word;
    }

    /**
     * @brief Hold a scratch register of an assignment until it is done
     */
    void push_scratch_register(Register scratch)
    {
        scratch_registers_.emplace_back(scratch);
    }
    void clear_scratch_registers() { scratch_registers_.clear(); }

  private:
    std::deque<Register> scratch_registers_{};
};

/**
 * @brief The storage device of each local of the function
 *
 * A local is assigned a register from the local registers when its frame
 * makes no calls, there is a register left, and its address is not taken,
 * and a stack slot otherwise. The devices are reset at each `BeginFunc`.
 */
class Device_Accessor
{
  public:
    using Device = Storage;

  public:
    Device_Accessor(Table_Pointer& table,
        Table_Accessor& table_accessor,
        Register_Accessor& register_accessor,
        Stack_Frame& stack_frame,
        Stack_Pointer& stack)
        : table_(table)
        , table_accessor_(table_accessor)
        , register_accessor_(register_accessor)
        , stack_frame_(stack_frame)
        , stack_(stack)
    {
    }

    using Operand_Size = assembly::Operand_Size;

    void reset_storage_devices()
    {
        register_id.clear();
        id_index = 0;
        address_table.clear();
    }

  public:
    bool is_lvalue_allocated_in_memory(LValue const& lvalue);
    Device get_operand_rvalue_device(RValue const& rvalue);
    Device get_device_by_lvalue(LValue const& lvalue);
    inline Device get_device_by_lvalue_reference(RValue const& rvalue)
    {
        return get_device_by_lvalue(rvalue);
    }
    constexpr void set_current_frame_symbol(Label const& label)
    {
        frame_symbol = label;
    }
    constexpr Label get_current_frame_name() { return frame_symbol; }

  public:
  public:
    inline Operand_Size get_word_size_from_lvalue(LValue const& lvalue)
    {
        return get_word_size_from_storage(
            get_device_by_lvalue(lvalue), stack_, stack_frame_);
    }

  public:
    Register get_available_storage_register(Operand_Size size);
    void insert_lvalue_to_device(LValue const& lvalue, Operand_Size size);
    void insert_lvalue_to_device(LValue const& lvalue,
        std::source_location const& location = std::source_location::current());
    void set_vector_offset_to_storage_space(LValue const& lvalue);
    bool is_address_taken(LValue const& lvalue);
    Size get_size_from_rvalue_reference(RValue const& rvalue);
    Size get_size_from_typed_rvalue(LValue const& lvalue,
        Immediate const& rvalue);

  private:
    void set_word_or_doubleword_register(LValue const& lvalue,
        Operand_Size size);
    Size get_size_of_address_table();

  private:
    Table_Pointer& table_;
    Table_Accessor& table_accessor_;
    Register_Accessor& register_accessor_;
    Stack_Frame& stack_frame_;
    Stack_Pointer& stack_;

  private:
    Label frame_symbol{ "main" };

    Ordered_Map<LValue, Storage> address_table{};
    std::set<unsigned int> register_id{};
    unsigned int id_index{ 0 };

    unsigned int vector_index{ 0 };
};

/**
 * @brief The address of an lvalue and the instructions that load it
 *
 * A global vector element is addressed from its page, a local vector
 * element from the stack pointer, and a local on the stack is loaded into
 * the stack move register
 *
 *  Example:
 *
 *    y = (2:int:4);
 *
 *  Becomes, where y is on the stack:
 *
 *    ldr w10, [sp, #16]   // the insertion instructions, the address is w10
 *    mov w10, #2
 */
struct Address_Accessor : public ARM64_Address_Accessor
{
  public:
    explicit Address_Accessor(Table_Pointer& table,
        ARM64_Address_Accessor::Stack_Pointer_Type& stack,
        Flag_Accessor& flag_accessor,
        Register_Accessor& register_accessor,
        Device_Accessor& device_accessor,
        Instruction_State& state,
        Stack_Frame& stack_frame)
        : ARM64_Address_Accessor(table, stack, flag_accessor, stack_frame)
        , register_accessor_(register_accessor)
        , device_accessor_(device_accessor)
        , state_(state)
    {
    }

    Address_Accessor::Address get_arm64_lvalue_and_insertion_instructions(
        LValue const& lvalue,
        std::size_t instruction_index);

  private:
    Address_Accessor::Address get_lvalue_address_and_from_unary_and_vectors(
        Address& instructions,
        LValue const& lvalue,
        std::size_t instruction_index);

  private:
    Register_Accessor& register_accessor_;
    Device_Accessor& device_accessor_;
    Instruction_State& state_;
};

/**
 * @brief The offset and size of an element of a global vector on ARM64
 */
struct Vector_Accessor : public ARM64_Vector_Accessor
{
  public:
    explicit Vector_Accessor(Table_Pointer& table, Stack_Frame& stack_frame)
        : ARM64_Vector_Accessor(table, stack_frame)
    {
    }
    Entry_Size get_size_from_vector_offset(Immediate const& immediate) override
    {
        return type::get_size_from_typed_rvalue(immediate);
    }
};

} // namespace detail

/**
 * @brief The Memory_Accessor of the ARM64 target, a Layered Aggregate
 *
 * The inserters, visitor, and emitter share one Memory_Accessor, where its
 * layers are private and declared in dependency order with the device below
 * the address, and each is reached through its role such as `ir()`,
 * `devices()`, or `state()`
 */
class Memory_Accessor final : public ARM64_Memory_Accessor
{
  public:
    Memory_Accessor() = delete;

    // the layers refer to members of this object, so a copy or a move
    // would refer to the members of its source
    Memory_Accessor(Memory_Accessor const&) = delete;
    Memory_Accessor& operator=(Memory_Accessor const&) = delete;
    Memory_Accessor(Memory_Accessor&&) = delete;
    Memory_Accessor& operator=(Memory_Accessor&&) = delete;

    explicit Memory_Accessor(Table_Pointer table,
        ir::Instructions_PTR instructions,
        Stack_Pointer stack_pointer)
        : ARM64_Memory_Accessor(table)
        , table_(std::move(table))
        , instructions_(std::move(instructions))
        , stack_(std::move(stack_pointer))
    {
    }

  public:
    /**
     * @brief Get each private layer of the aggregate by its role
     */
    detail::Instruction_State& state() { return state_; }
    detail::Table_Accessor& ir() { return table_accessor_; }
    detail::Flag_Accessor& flags() { return flag_accessor_; }
    detail::Register_Accessor& registers() { return register_accessor_; }
    detail::Accumulator_Accessor& accumulator()
    {
        return accumulator_accessor_;
    }
    detail::Vector_Accessor& vectors() { return vector_accessor_; }
    detail::Address_Accessor& addresses() { return address_accessor_; }
    detail::Device_Accessor& devices() { return device_accessor_; }
    detail::Buffer_Accessor& buffers() { return buffer_accessor_; }
    Instruction_Pointer& instructions() { return instruction_accessor_; }
    Stack_Pointer& stack() { return stack_; }

  public:
    /**
     * @brief Queries of the aggregate that are answered from more than one
     * layer
     */
    Register get_accumulator_with_rvalue_context(Storage const& device);
    Register get_accumulator_with_rvalue_context(assembly::Operand_Size size);

  private:
    Table_Pointer table_;
    ir::Instructions_PTR instructions_;
    Stack_Pointer stack_;

  private:
    detail::Instruction_State state_{ Register::wzr };
    detail::Flag_Accessor flag_accessor_{};
    detail::Table_Accessor table_accessor_{ table_, instructions_ };
    detail::Buffer_Accessor buffer_accessor_{ table_ };
    detail::Accumulator_Accessor accumulator_accessor_{ state_ };
    detail::Vector_Accessor vector_accessor_{ table_, get_frame_in_memory() };
    detail::Register_Accessor register_accessor_{ state_ };
    detail::Device_Accessor device_accessor_{ table_,
        table_accessor_,
        register_accessor_,
        get_frame_in_memory(),
        stack_ };
    detail::Address_Accessor address_accessor_{ table_,
        stack_,
        flag_accessor_,
        register_accessor_,
        device_accessor_,
        state_,
        get_frame_in_memory() };
    Instruction_Pointer instruction_accessor_{
        std::make_shared<detail::Instruction_Accessor>()
    };
};

static_assert(common::memory::Memory_Accessor_T<Memory_Accessor>);

} // namespace memory

} // namespace arm64