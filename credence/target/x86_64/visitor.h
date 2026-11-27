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

#include "assembly.h"                       // for Instructions
#include "credence/ir/ita.h"                // for Quadruple
#include "memory.h"                         // for Memory_Access, Memory_Ac...
#include <credence/object/table.h>          // for Label
#include <credence/target/common/visitor.h> // for IR_Visitor
#include <cstddef>                          // for size_t

/****************************************************************************
 * x86-64 IR Visitor
 *
 * Visits ITA instructions and emits x86-64 instructions. Every local is on
 * the stack below rbp, and an expression's value is in the accumulator, eax
 * or rax.
 *
 *  Example:
 *
 *    PUSH _p5_4;
 *    PUSH _p4_3;
 *    CALL add;
 *    POP 16;
 *    _t8 = RET;
 *
 *  Becomes:
 *
 *    mov edi, dword ptr [rbp - 4]
 *    mov r8d, 2
 *    call add
 *    mov dword ptr [rbp - 8], eax
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

namespace credence::target::x86_64 {

using X8664_IR_Visitor =
    common::IR_Visitor<ir::Quadruple, assembly::Instructions>;

/**
 * @brief IR visitor for the x86-64 ISA
 *
 * A MOV dispatches to an inserter by its lvalue and rvalue. The accumulator
 * holds a temporary until the next instruction, so a temporary used later is
 * stored on the stack
 */
class IR_Instruction_Visitor final : public X8664_IR_Visitor
{
  public:
    explicit IR_Instruction_Visitor(memory::Memory_Access& accessor)
        : accessor_(accessor)
        , stack_frame_(accessor_->get_frame_in_memory())
    {
    }

    using Instructions = assembly::Instructions;

  public:
    constexpr void set_iterator_index(std::size_t index)
    {
        iterator_index_ = index;
    }

  public:
    void from_locl_ita(ir::Quadruple const& inst) override;
    void from_pop_ita() override;
    void from_push_ita(ir::Quadruple const& inst) override;
    void from_func_start_ita(Label const& name) override;
    void from_func_end_ita() override;
    void from_cmp_ita(ir::Quadruple const& inst) override;
    void from_mov_ita(ir::Quadruple const& inst) override;
    void from_return_ita(ir::Quadruple const& inst) override;
    void from_leave_ita() override;
    void from_label_ita(ir::Quadruple const& inst) override;
    void from_call_ita(ir::Quadruple const& inst) override;
    void from_if_ita(ir::Quadruple const& inst) override;
    void from_jmp_e_ita(ir::Quadruple const& inst) override;
    void from_goto_ita(ir::Quadruple const& inst) override;

  private:
    bool is_temporary_used_after_next_instruction(LValue const& temporary);
    void insert_temporary_to_stack(LValue const& temporary, LValue const& slot);

  private:
    std::size_t iterator_index_{ 0 };

  private:
    memory::Memory_Access accessor_;
    memory::Stack_Frame& stack_frame_;
};

}