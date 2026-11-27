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

#include "memory.h"

#include "assembly.h"                        // for Register, is_immediate_...
#include "credence/target/common/flags.h"    // for Flag_Accessor, Instruct...
#include "credence/target/common/memory.h"   // for is_vector_offset
#include "credence/target/common/types.h"    // for Stack_Offset
#include "stack.h"                           // for Stack
#include <credence/error.h>                  // for throw_compiletime_error
#include <credence/object/table.h>           // for Object, LValue, RValue
#include <credence/symbol.h>                 // for Symbol_Table
#include <credence/target/common/assembly.h> // for make_direct_immediate
#include <credence/types.h>                  // for from_decay_offset, from...
#include <credence/util.h>                   // for is_numeric, overload
#include <deque>                             // for deque
#include <fmt/format.h>                      // for format
#include <matchit.h>                         // for matchit
#include <string>                            // for basic_string, char_traits
#include <string_view>                       // for basic_string_view
#include <utility>                           // for pair
#include <variant>                           // for variant, monostate, visit

/****************************************************************************
 * x86-64 Memory Accessor
 *
 * The Memory_Accessor of the x86-64 target, and the address of each lvalue.
 * A local is on the stack below rbp, a global is relative to rip,
 * and a vector element is an offset in its vector.
 *
 *  Example:
 *
 *    main() {
 *      auto x, arr[5];
 *      extrn value;
 *      x = value;
 *      arr[2] = 42;
 *    }
 *
 *  Becomes:
 *
 *    mov eax, dword ptr [rip + value]   // a global, from rip
 *    mov dword ptr [rbp - 4], eax       // x
 *    mov dword ptr [rbp - 12], 42       // arr[2]
 *
 ****************************************************************************/

/****************************************************************************
 * Register selection table:
 *
 *   rax      = accumulator for expression expansion, return value, and
 *              syscall number
 *   rdi, r8, r9, rsi, rdx, rcx
 *            = arguments to a program function
 *   rdi, rsi, rdx, rcx, r8, r9
 *            = standard library arguments, System V
 *   rdi, rsi, rdx, r10, r8, r9
 *            = syscall arguments
 *   xmm0 - xmm7
 *            = standard library float and double arguments
 *   xmm7     = float and double literal store register
 *   rcx      = address-of register, and shift count in cl
 *   rdx      = dividend high half, and `idiv` remainder
 *   r10      = dereference address register
 *   r11      = right-hand-side scratch register, `idiv` divisor, and
 *              global argument value
 *   r15      = argc, argv address on the stack
 *   rbp      = frame pointer
 *
 *   Locals, parameters, vectors, and vector offsets are always on the
 *   stack below rbp
 *
 *****************************************************************************/

namespace credence::target::x86_64::memory {

namespace m = matchit;

namespace detail {

/**
 * @brief Take the next argument register for a size, or a stack slot when none
 * is left
 *
 * The quadword and doubleword lists are the same registers, so both are
 * taken together, and the next argument after `rdi` is `r8d`, not `edi`
 */
assembly::Storage Register_Accessor::get_available_register(
    assembly::Operand_Size size,
    Stack_Pointer& stack)
{
    if (d_size_registers.empty() or w_size_registers.empty())
        return stack->allocate(size);
    Storage storage = size == Operand_Size::Qword ? d_size_registers.front()
                                                  : w_size_registers.front();
    d_size_registers.pop_front();
    w_size_registers.pop_front();
    return storage;
}

/**
 * @brief Allocate a local's stack slot for its type
 *
 * The size is the local's value size in the object table
 */
void Device_Accessor::insert_lvalue_to_device(LValue const& lvalue)
{
    auto type = table_->get_type_from_object_data_type(
        lvalue, stack_frame_.get_stack_frame());
    stack_->set_address_from_type(lvalue, type);
}

/**
 * @brief Allocate a pointer's stack slot, an aligned quadword
 */
void Device_Accessor::insert_pointer_to_device(LValue const& lvalue)
{
    stack_->set_address_from_address(lvalue);
}

/**
 * @brief Get an lvalue's address and the instructions that load it
 *
 * A dereference loads the pointer into rax and flags the next instruction as
 * indirect, a global vector element is relative to rip, a local vector element
 * is its offset in the vector, and a local is its stack slot. A global's size
 * prefix is left out for an address, such as `lea`.
 */
Address_Accessor::Address
Address_Accessor::get_lvalue_address_and_insertion_instructions(
    LValue const& lvalue,
    std::size_t instruction_index,
    bool use_prefix)
{
    assembly::Instruction_Pair instructions{ Register::eax, {} };
    auto lhs = type::from_lvalue_offset(lvalue);
    auto offset = type::from_decay_offset(lvalue);

    if (type::is_dereference_expression(lvalue)) {
        auto storage =
            stack_->get(type::get_unary_rvalue_reference(lvalue)).first;
        x8664_add_asm(instructions.second, mov, Register::rax, storage);
        flag_accessor_.set_instruction_flag(
            common::flag::Indirect, instruction_index + 1);
        instructions.first = Register::rax;
    } else if (is_global_vector(lhs)) {
        if (table_->globals().is_pointer(lhs)) {
            auto rip_storage =
                vector_accessor_.get_offset_address(lvalue, offset);
            auto prefix = storage_prefix_from_operand_size(rip_storage.second);
            auto rip_arithmetic =
                rip_storage.first == 0UL
                    ? lhs
                    : fmt::format("{}+{}", lhs, rip_storage.first);
            rip_arithmetic =
                use_prefix
                    ? fmt::format("{} [rip + {}]", prefix, rip_arithmetic)
                    : fmt::format("[rip + {}]", rip_arithmetic);
            instructions.first =
                common::assembly::make_array_immediate(rip_arithmetic);
        }
    } else if (is_vector_offset(lvalue)) {
        auto& vector = table_->get_vector(lhs);
        instructions.first = stack_->get_stack_offset_from_table_vector_index(
            lhs, offset, vector);

    } else if (device_accessor_.is_lvalue_allocated_in_memory(lvalue)) {
        instructions.first = device_accessor_.get_device_by_lvalue(lvalue);
    }
    return instructions;
}

/**
 * @brief Check if a storage device is a quadword
 *
 * A string, an address relative to rip, or an element of `argv` is an
 * address, so it is a quadword
 */
bool Address_Accessor::is_qword_storage_size(
    assembly::Storage const& storage) const
{
    auto result{ false };

    std::visit(
        util::overload{
            [&](std::monostate) {},
            [&](common::Stack_Offset const& s) {
                result = stack_->get_operand_size_from_offset(s) ==
                         Operand_Size::Qword;
            },
            [&](Register const& s) { result = assembly::is_qword_register(s); },
            [&](Immediate const& s) {
                result = type::is_typed_rvalue_string(s) or
                         assembly::is_immediate_r15_address_offset(s) or
                         assembly::is_immediate_rip_address_offset(s);
            },
        },
        storage);

    return result;
}

} // namespace detail

} // namespace memory