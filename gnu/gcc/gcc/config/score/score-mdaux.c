/* score-mdaux.c for Sunplus S+CORE processor
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Sunnorth

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include <signal.h>
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-attr.h"
#include "recog.h"
#include "toplev.h"
#include "output.h"
#include "tree.h"
#include "function.h"
#include "expr.h"
#include "optabs.h"
#include "flags.h"
#include "reload.h"
#include "tm_p.h"
#include "ggc.h"
#include "gstab.h"
#include "hashtab.h"
#include "debug.h"
#include "target.h"
#include "target-def.h"
#include "integrate.h"
#include "langhooks.h"
#include "cfglayout.h"
#include "score-mdaux.h"

#define BITSET_P(VALUE, BIT)      (((VALUE) & (1L << (BIT))) != 0)
#define INS_BUF_SZ                100

/* Define the information needed to generate branch insns.  This is
   stored from the compare operation.  */
rtx cmp_op0, cmp_op1;

static char ins[INS_BUF_SZ + 8];

/* Return true if SYMBOL is a SYMBOL_REF and OFFSET + SYMBOL points
   to the same object as SYMBOL.  */
static int
score_offset_within_object_p (rtx symbol, HOST_WIDE_INT offset)
{
  if (GET_CODE (symbol) != SYMBOL_REF)
    return 0;

  if (CONSTANT_POOL_ADDRESS_P (symbol)
      && offset >= 0
      && offset < (int)GET_MODE_SIZE (get_pool_mode (symbol)))
    return 1;

  if (SYMBOL_REF_DECL (symbol) != 0
      && offset >= 0
      && offset < int_size_in_bytes (TREE_TYPE (SYMBOL_REF_DECL (symbol))))
    return 1;

  return 0;
}

/* Split X into a base and a constant offset, storing them in *BASE
   and *OFFSET respectively.  */
static void
score_split_const (rtx x, rtx *base, HOST_WIDE_INT *offset)
{
  *offset = 0;

  if (GET_CODE (x) == CONST)
    x = XEXP (x, 0);

  if (GET_CODE (x) == PLUS && GET_CODE (XEXP (x, 1)) == CONST_INT)
    {
      *offset += INTVAL (XEXP (x, 1));
      x = XEXP (x, 0);
    }

  *base = x;
}

/* Classify symbol X, which must be a SYMBOL_REF or a LABEL_REF.  */
static enum
score_symbol_type score_classify_symbol (rtx x)
{
  if (GET_CODE (x) == LABEL_REF)
    return SYMBOL_GENERAL;

  gcc_assert (GET_CODE (x) == SYMBOL_REF);

  if (CONSTANT_POOL_ADDRESS_P (x))
    {
      if (GET_MODE_SIZE (get_pool_mode (x)) <= SCORE_SDATA_MAX)
        return SYMBOL_SMALL_DATA;
      return SYMBOL_GENERAL;
    }
  if (SYMBOL_REF_SMALL_P (x))
    return SYMBOL_SMALL_DATA;
  return SYMBOL_GENERAL;
}

/* Return true if the current function must save REGNO.  */
static int
score_save_reg_p (unsigned int regno)
{
  /* Check call-saved registers.  */
  if (regs_ever_live[regno] && !call_used_regs[regno])
    return 1;

  /* We need to save the old frame pointer before setting up a new one.  */
  if (regno == HARD_FRAME_POINTER_REGNUM && frame_pointer_needed)
    return 1;

  /* We need to save the incoming return address if it is ever clobbered
     within the function.  */
  if (regno == RA_REGNUM && regs_ever_live[regno])
    return 1;

  return 0;
}

/* Return one word of double-word value OP, taking into account the fixed
   endianness of certain registers.  HIGH_P is true to select the high part,
   false to select the low part.  */
static rtx
subw (rtx op, int high_p)
{
  unsigned int byte;
  enum machine_mode mode = GET_MODE (op);

  if (mode == VOIDmode)
    mode = DImode;

  byte = (TARGET_LITTLE_ENDIAN ? high_p : !high_p) ? UNITS_PER_WORD : 0;

  if (GET_CODE (op) == REG && REGNO (op) == HI_REGNUM)
    return gen_rtx_REG (SImode, high_p ? HI_REGNUM : LO_REGNUM);

  if (GET_CODE (op) == MEM)
    return adjust_address (op, SImode, byte);

  return simplify_gen_subreg (SImode, op, mode, byte);
}

struct score_frame_info *
mda_cached_frame (void)
{
  static struct score_frame_info _frame_info;
  return &_frame_info;
}

/* Return the bytes needed to compute the frame pointer from the current
   stack pointer.  SIZE is the size (in bytes) of the local variables.  */
struct score_frame_info *
mda_compute_frame_size (HOST_WIDE_INT size)
{
  unsigned int regno;
  struct score_frame_info *f = mda_cached_frame ();

  memset (f, 0, sizeof (struct score_frame_info));
  f->gp_reg_size = 0;
  f->mask = 0;
  f->var_size = SCORE_STACK_ALIGN (size);
  f->args_size = current_function_outgoing_args_size;
  f->cprestore_size = flag_pic ? UNITS_PER_WORD : 0;
  if (f->var_size == 0 && current_function_is_leaf)
    f->args_size = f->cprestore_size = 0;

  if (f->args_size == 0 && current_function_calls_alloca)
    f->args_size = UNITS_PER_WORD;

  f->total_size = f->var_size + f->args_size + f->cprestore_size;
  for (regno = GP_REG_FIRST; regno <= GP_REG_LAST; regno++)
    {
      if (score_save_reg_p (regno))
        {
          f->gp_reg_size += GET_MODE_SIZE (SImode);
          f->mask |= 1 << (regno - GP_REG_FIRST);
        }
    }

  if (current_function_calls_eh_return)
    {
      unsigned int i;
      for (i = 0;; ++i)
        {
          regno = EH_RETURN_DATA_REGNO (i);
          if (regno == INVALID_REGNUM)
            break;
          f->gp_reg_size += GET_MODE_SIZE (SImode);
          f->mask |= 1 << (regno - GP_REG_FIRST);
        }
    }

  f->total_size += f->gp_reg_size;
  f->num_gp = f->gp_reg_size / UNITS_PER_WORD;

  if (f->mask)
    {
      HOST_WIDE_INT offset;
      offset = (f->args_size + f->cprestore_size + f->var_size
                + f->gp_reg_size - GET_MODE_SIZE (SImode));
      f->gp_sp_offset = offset;
    }
  else
    f->gp_sp_offset = 0;

  return f;
}

/* Generate the prologue instructions for entry into a S+core function.  */
void
mdx_prologue (void)
{
#define EMIT_PL(_rtx)        RTX_FRAME_RELATED_P (_rtx) = 1

  struct score_frame_info *f = mda_compute_frame_size (get_frame_size ());
  HOST_WIDE_INT size;
  int regno;

  size = f->total_size - f->gp_reg_size;

  if (flag_pic)
    emit_insn (gen_cpload ());

  for (regno = (int) GP_REG_LAST; regno >= (int) GP_REG_FIRST; regno--)
    {
      if (BITSET_P (f->mask, regno - GP_REG_FIRST))
        {
          rtx mem = gen_rtx_MEM (SImode,
                                 gen_rtx_PRE_DEC (SImode, stack_pointer_rtx));
          rtx reg = gen_rtx_REG (SImode, regno);
          if (!current_function_calls_eh_return)
            MEM_READONLY_P (mem) = 1;
          EMIT_PL (emit_insn (gen_pushsi (mem, reg)));
        }
    }

  if (size > 0)
    {
      rtx insn;

      if (CONST_OK_FOR_LETTER_P (-size, 'L'))
        EMIT_PL (emit_insn (gen_add3_insn (stack_pointer_rtx,
                                           stack_pointer_rtx,
                                           GEN_INT (-size))));
      else
        {
          EMIT_PL (emit_move_insn (gen_rtx_REG (Pmode, PROLOGUE_TEMP_REGNUM),
                                   GEN_INT (size)));
          EMIT_PL (emit_insn
                   (gen_sub3_insn (stack_pointer_rtx,
                                   stack_pointer_rtx,
                                   gen_rtx_REG (Pmode,
                                                PROLOGUE_TEMP_REGNUM))));
        }
      insn = get_last_insn ();
      REG_NOTES (insn) =
        alloc_EXPR_LIST (REG_FRAME_RELATED_EXPR,
                         gen_rtx_SET (VOIDmode, stack_pointer_rtx,
                                      plus_constant (stack_pointer_rtx,
                                                     -size)),
                                      REG_NOTES (insn));
    }

  if (frame_pointer_needed)
    EMIT_PL (emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx));

  if (flag_pic && f->cprestore_size)
    {
      if (frame_pointer_needed)
        emit_insn (gen_cprestore_use_fp (GEN_INT (size - f->cprestore_size)));
      else
        emit_insn (gen_cprestore_use_sp (GEN_INT (size - f->cprestore_size)));
    }

#undef EMIT_PL
}

/* Generate the epilogue instructions in a S+core function.  */
void
mdx_epilogue (int sibcall_p)
{
  struct score_frame_info *f = mda_compute_frame_size (get_frame_size ());
  HOST_WIDE_INT size;
  int regno;
  rtx base;

  size = f->total_size - f->gp_reg_size;

  if (!frame_pointer_needed)
    base = stack_pointer_rtx;
  else
    base = hard_frame_pointer_rtx;

  if (size)
    {
      if (CONST_OK_FOR_LETTER_P (size, 'L'))
        emit_insn (gen_add3_insn (base, base, GEN_INT (size)));
      else
        {
          emit_move_insn (gen_rtx_REG (Pmode, EPILOGUE_TEMP_REGNUM),
                          GEN_INT (size));
          emit_insn (gen_add3_insn (base, base,
                                    gen_rtx_REG (Pmode,
                                                 EPILOGUE_TEMP_REGNUM)));
        }
    }

  if (base != stack_pointer_rtx)
    emit_move_insn (stack_pointer_rtx, base);

  if (current_function_calls_eh_return)
    emit_insn (gen_add3_insn (stack_pointer_rtx,
                              stack_pointer_rtx,
                              EH_RETURN_STACKADJ_RTX));

  for (regno = (int) GP_REG_FIRST; regno <= (int) GP_REG_LAST; regno++)
    {
      if (BITSET_P (f->mask, regno - GP_REG_FIRST))
        {
          rtx mem = gen_rtx_MEM (SImode,
                                 gen_rtx_POST_INC (SImode, stack_pointer_rtx));
          rtx reg = gen_rtx_REG (SImode, regno);

          if (!current_function_calls_eh_return)
            MEM_READONLY_P (mem) = 1;

          emit_insn (gen_popsi (reg, mem));
        }
    }

  if (!sibcall_p)
    emit_jump_insn (gen_return_internal (gen_rtx_REG (Pmode, RA_REGNUM)));
}

/* Return true if X is a valid base register for the given mode.
   Allow only hard registers if STRICT.  */
int
mda_valid_base_register_p (rtx x, int strict)
{
  if (!strict && GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);

  return (GET_CODE (x) == REG
          && score_regno_mode_ok_for_base_p (REGNO (x), strict));
}

/* Return true if X is a valid address for machine mode MODE.  If it is,
   fill in INFO appropriately.  STRICT is true if we should only accept
   hard base registers.  */
int
mda_classify_address (struct score_address_info *info,
                      enum machine_mode mode, rtx x, int strict)
{
  info->code = GET_CODE (x);

  switch (info->code)
    {
    case REG:
    case SUBREG:
      info->type = ADD_REG;
      info->reg = x;
      info->offset = const0_rtx;
      return mda_valid_base_register_p (info->reg, strict);
    case PLUS:
      info->type = ADD_REG;
      info->reg = XEXP (x, 0);
      info->offset = XEXP (x, 1);
      return (mda_valid_base_register_p (info->reg, strict)
              && GET_CODE (info->offset) == CONST_INT
              && IMM_IN_RANGE (INTVAL (info->offset), 15, 1));
    case PRE_DEC:
    case POST_DEC:
    case PRE_INC:
    case POST_INC:
      if (GET_MODE_SIZE (mode) > GET_MODE_SIZE (SImode))
        return false;
      info->type = ADD_REG;
      info->reg = XEXP (x, 0);
      info->offset = GEN_INT (GET_MODE_SIZE (mode));
      return mda_valid_base_register_p (info->reg, strict);
    case CONST_INT:
      info->type = ADD_CONST_INT;
      return IMM_IN_RANGE (INTVAL (x), 15, 1);
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      info->type = ADD_SYMBOLIC;
      return (mda_symbolic_constant_p (x, &info->symbol_type)
              && (info->symbol_type == SYMBOL_GENERAL
                  || info->symbol_type == SYMBOL_SMALL_DATA));
    default:
      return 0;
    }
}

void
mda_gen_cmp (enum machine_mode mode)
{
  emit_insn (gen_rtx_SET (VOIDmode, gen_rtx_REG (mode, CC_REGNUM),
                          gen_rtx_COMPARE (mode, cmp_op0, cmp_op1)));
}

/* Return true if X is a symbolic constant that can be calculated in
   the same way as a bare symbol.  If it is, store the type of the
   symbol in *SYMBOL_TYPE.  */
int
mda_symbolic_constant_p (rtx x, enum score_symbol_type *symbol_type)
{
  HOST_WIDE_INT offset;

  score_split_const (x, &x, &offset);
  if (GET_CODE (x) == SYMBOL_REF || GET_CODE (x) == LABEL_REF)
    *symbol_type = score_classify_symbol (x);
  else
    return 0;

  if (offset == 0)
    return 1;

  /* if offset > 15bit, must reload  */
  if (!IMM_IN_RANGE (offset, 15, 1))
    return 0;

  switch (*symbol_type)
    {
    case SYMBOL_GENERAL:
      return 1;
    case SYMBOL_SMALL_DATA:
      return score_offset_within_object_p (x, offset);
    }
  gcc_unreachable ();
}

void
mdx_movsicc (rtx *ops)
{
  enum machine_mode mode;

  mode = score_select_cc_mode (GET_CODE (ops[1]), ops[2], ops[3]);
  emit_insn (gen_rtx_SET (VOIDmode, gen_rtx_REG (mode, CC_REGNUM),
                          gen_rtx_COMPARE (mode, cmp_op0, cmp_op1)));
}

/* Call and sibcall pattern all need call this function.  */
void
mdx_call (rtx *ops, bool sib)
{
  rtx addr = XEXP (ops[0], 0);
  if (!call_insn_operand (addr, VOIDmode))
    {
      rtx oaddr = addr;
      addr = gen_reg_rtx (Pmode);
      gen_move_insn (addr, oaddr);
    }

  if (sib)
    emit_call_insn (gen_sibcall_internal (addr, ops[1]));
  else
    emit_call_insn (gen_call_internal (addr, ops[1]));
}

/* Call value and sibcall value pattern all need call this function.  */
void
mdx_call_value (rtx *ops, bool sib)
{
  rtx result = ops[0];
  rtx addr = XEXP (ops[1], 0);
  rtx arg = ops[2];

  if (!call_insn_operand (addr, VOIDmode))
    {
      rtx oaddr = addr;
      addr = gen_reg_rtx (Pmode);
      gen_move_insn (addr, oaddr);
    }

  if (sib)
    emit_call_insn (gen_sibcall_value_internal (result, addr, arg));
  else
    emit_call_insn (gen_call_value_internal (result, addr, arg));
}

/* Machine Split  */
void
mds_movdi (rtx *ops)
{
  rtx dst = ops[0];
  rtx src = ops[1];
  rtx dst0 = subw (dst, 0);
  rtx dst1 = subw (dst, 1);
  rtx src0 = subw (src, 0);
  rtx src1 = subw (src, 1);

  if (GET_CODE (dst0) == REG && reg_overlap_mentioned_p (dst0, src))
    {
      emit_move_insn (dst1, src1);
      emit_move_insn (dst0, src0);
    }
  else
    {
      emit_move_insn (dst0, src0);
      emit_move_insn (dst1, src1);
    }
}

void
mds_zero_extract_andi (rtx *ops)
{
  if (INTVAL (ops[1]) == 1 && const_uimm5 (ops[2], SImode))
    emit_insn (gen_zero_extract_bittst (ops[0], ops[2]));
  else
    {
      unsigned HOST_WIDE_INT mask;
      mask = (0xffffffffU & ((1U << INTVAL (ops[1])) - 1U));
      mask = mask << INTVAL (ops[2]);
      emit_insn (gen_andsi3_cmp (ops[3], ops[0],
                                 gen_int_mode (mask, SImode)));
    }
}

/* Check addr could be present as PRE/POST mode.  */
static bool
mda_pindex_mem (rtx addr)
{
  if (GET_CODE (addr) == MEM)
    {
      switch (GET_CODE (XEXP (addr, 0)))
        {
        case PRE_DEC:
        case POST_DEC:
        case PRE_INC:
        case POST_INC:
          return true;
        default:
          break;
        }
    }
  return false;
}

/* Output asm code for ld/sw insn.  */
static int
pr_addr_post (rtx *ops, int idata, int iaddr, char *ip, enum mda_mem_unit unit)
{
  struct score_address_info ai;

  gcc_assert (GET_CODE (ops[idata]) == REG);
  gcc_assert (mda_classify_address (&ai, SImode, XEXP (ops[iaddr], 0), true));

  if (!mda_pindex_mem (ops[iaddr])
      && ai.type == ADD_REG
      && GET_CODE (ai.offset) == CONST_INT
      && G16_REG_P (REGNO (ops[idata]))
      && G16_REG_P (REGNO (ai.reg)))
    {
      if (INTVAL (ai.offset) == 0)
        {
          ops[iaddr] = ai.reg;
          return snprintf (ip, INS_BUF_SZ,
                           "!        %%%d, [%%%d]", idata, iaddr);
        }
      if (REGNO (ai.reg) == HARD_FRAME_POINTER_REGNUM)
        {
          HOST_WIDE_INT offset = INTVAL (ai.offset);
          if (MDA_ALIGN_UNIT (offset, unit)
              && CONST_OK_FOR_LETTER_P (offset >> unit, 'J'))
            {
              ops[iaddr] = ai.offset;
              return snprintf (ip, INS_BUF_SZ,
                               "p!        %%%d, %%c%d", idata, iaddr);
            }
        }
    }
  return snprintf (ip, INS_BUF_SZ, "        %%%d, %%a%d", idata, iaddr);
}

/* Output asm insn for load.  */
const char *
mdp_linsn (rtx *ops, enum mda_mem_unit unit, bool sign)
{
  const char *pre_ins[] =
    {"lbu", "lhu", "lw", "??", "lb", "lh", "lw", "??"};
  char *ip;

  strcpy (ins, pre_ins[(sign ? 4 : 0) + unit]);
  ip = ins + strlen (ins);

  if ((!sign && unit != MDA_HWORD)
      || (sign && unit != MDA_BYTE))
    pr_addr_post (ops, 0, 1, ip, unit);
  else
    snprintf (ip, INS_BUF_SZ, "        %%0, %%a1");

  return ins;
}

/* Output asm insn for store.  */
const char *
mdp_sinsn (rtx *ops, enum mda_mem_unit unit)
{
  const char *pre_ins[] = {"sb", "sh", "sw"};
  char *ip;

  strcpy (ins, pre_ins[unit]);
  ip = ins + strlen (ins);
  pr_addr_post (ops, 1, 0, ip, unit);
  return ins;
}

/* Output asm insn for load immediate.  */
const char *
mdp_limm (rtx *ops)
{
  HOST_WIDE_INT v;

  gcc_assert (GET_CODE (ops[0]) == REG);
  gcc_assert (GET_CODE (ops[1]) == CONST_INT);

  v = INTVAL (ops[1]);
  if (G16_REG_P (REGNO (ops[0])) && IMM_IN_RANGE (v, 8, 0))
    return "ldiu!   %0, %c1";
  else if (IMM_IN_RANGE (v, 16, 1))
    return "ldi     %0, %c1";
  else if ((v & 0xffff) == 0)
    return "ldis    %0, %U1";
  else
    return "li      %0, %c1";
}

/* Output asm insn for move.  */
const char *
mdp_move (rtx *ops)
{
  gcc_assert (GET_CODE (ops[0]) == REG);
  gcc_assert (GET_CODE (ops[1]) == REG);

  if (G16_REG_P (REGNO (ops[0])))
    {
      if (G16_REG_P (REGNO (ops[1])))
        return "mv!     %0, %1";
      else
        return "mlfh!   %0, %1";
    }
  else if (G16_REG_P (REGNO (ops[1])))
    return "mhfl!   %0, %1";
  else
    return "mv      %0, %1";
}

/* Emit lcb/lce insns.  */
bool
mdx_unaligned_load (rtx *ops)
{
  rtx dst = ops[0];
  rtx src = ops[1];
  rtx len = ops[2];
  rtx off = ops[3];
  rtx addr_reg;

  if (INTVAL (len) != BITS_PER_WORD
      || (INTVAL (off) % BITS_PER_UNIT) != 0)
    return false;

  gcc_assert (GET_MODE_SIZE (GET_MODE (dst)) == GET_MODE_SIZE (SImode));

  addr_reg = copy_addr_to_reg (XEXP (src, 0));
  emit_insn (gen_move_lcb (addr_reg, addr_reg));
  emit_insn (gen_move_lce (addr_reg, addr_reg, dst));

  return true;
}

/* Emit scb/sce insns.  */
bool
mdx_unaligned_store (rtx *ops)
{
  rtx dst = ops[0];
  rtx len = ops[1];
  rtx off = ops[2];
  rtx src = ops[3];
  rtx addr_reg;

  if (INTVAL(len) != BITS_PER_WORD
      || (INTVAL(off) % BITS_PER_UNIT) != 0)
    return false;

  gcc_assert (GET_MODE_SIZE (GET_MODE (src)) == GET_MODE_SIZE (SImode));

  addr_reg = copy_addr_to_reg (XEXP (dst, 0));
  emit_insn (gen_move_scb (addr_reg, addr_reg, src));
  emit_insn (gen_move_sce (addr_reg, addr_reg));

  return true;
}

/* If length is short, generate move insns straight.  */
static void
mdx_block_move_straight (rtx dst, rtx src, HOST_WIDE_INT length)
{
  HOST_WIDE_INT leftover;
  int i, reg_count;
  rtx *regs;

  leftover = length % UNITS_PER_WORD;
  length -= leftover;
  reg_count = length / UNITS_PER_WORD;

  regs = alloca (sizeof (rtx) * reg_count);
  for (i = 0; i < reg_count; i++)
    regs[i] = gen_reg_rtx (SImode);

  /* Load from src to regs.  */
  if (MEM_ALIGN (src) >= BITS_PER_WORD)
    {
      HOST_WIDE_INT offset = 0;
      for (i = 0; i < reg_count; offset += UNITS_PER_WORD, i++)
        emit_move_insn (regs[i], adjust_address (src, SImode, offset));
    }
  else if (reg_count >= 1)
    {
      rtx src_reg = copy_addr_to_reg (XEXP (src, 0));

      emit_insn (gen_move_lcb (src_reg, src_reg));
      for (i = 0; i < (reg_count - 1); i++)
        emit_insn (gen_move_lcw (src_reg, src_reg, regs[i]));
      emit_insn (gen_move_lce (src_reg, src_reg, regs[i]));
    }

  /* Store regs to dest.  */
  if (MEM_ALIGN (dst) >= BITS_PER_WORD)
    {
      HOST_WIDE_INT offset = 0;
      for (i = 0; i < reg_count; offset += UNITS_PER_WORD, i++)
        emit_move_insn (adjust_address (dst, SImode, offset), regs[i]);
    }
  else if (reg_count >= 1)
    {
      rtx dst_reg = copy_addr_to_reg (XEXP (dst, 0));

      emit_insn (gen_move_scb (dst_reg, dst_reg, regs[0]));
      for (i = 1; i < reg_count; i++)
        emit_insn (gen_move_scw (dst_reg, dst_reg, regs[i]));
      emit_insn (gen_move_sce (dst_reg, dst_reg));
    }

  /* Mop up any left-over bytes.  */
  if (leftover > 0)
    {
      src = adjust_address (src, BLKmode, length);
      dst = adjust_address (dst, BLKmode, length);
      move_by_pieces (dst, src, leftover,
                      MIN (MEM_ALIGN (src), MEM_ALIGN (dst)), 0);
    }
}

/* Generate loop head when dst or src is unaligned.  */
static void
mdx_block_move_loop_head (rtx dst_reg, HOST_WIDE_INT dst_align,
                          rtx src_reg, HOST_WIDE_INT src_align,
                          HOST_WIDE_INT length)
{
  bool src_unaligned = (src_align < BITS_PER_WORD);
  bool dst_unaligned = (dst_align < BITS_PER_WORD);

  rtx temp = gen_reg_rtx (SImode);

  gcc_assert (length == UNITS_PER_WORD);

  if (src_unaligned)
    {
      emit_insn (gen_move_lcb (src_reg, src_reg));
      emit_insn (gen_move_lcw (src_reg, src_reg, temp));
    }
  else
    emit_insn (gen_move_lw_a (src_reg,
                              src_reg, gen_int_mode (4, SImode), temp));

  if (dst_unaligned)
    emit_insn (gen_move_scb (dst_reg, dst_reg, temp));
  else
    emit_insn (gen_move_sw_a (dst_reg,
                              dst_reg, gen_int_mode (4, SImode), temp));
}

/* Generate loop body, copy length bytes per iteration.  */
static void
mdx_block_move_loop_body (rtx dst_reg, HOST_WIDE_INT dst_align,
                          rtx src_reg, HOST_WIDE_INT src_align,
                          HOST_WIDE_INT length)
{
  int reg_count = length / UNITS_PER_WORD;
  rtx *regs = alloca (sizeof (rtx) * reg_count);
  int i;
  bool src_unaligned = (src_align < BITS_PER_WORD);
  bool dst_unaligned = (dst_align < BITS_PER_WORD);

  for (i = 0; i < reg_count; i++)
    regs[i] = gen_reg_rtx (SImode);

  if (src_unaligned)
    {
      for (i = 0; i < reg_count; i++)
        emit_insn (gen_move_lcw (src_reg, src_reg, regs[i]));
    }
  else
    {
      for (i = 0; i < reg_count; i++)
        emit_insn (gen_move_lw_a (src_reg,
                                  src_reg, gen_int_mode (4, SImode), regs[i]));
    }

  if (dst_unaligned)
    {
      for (i = 0; i < reg_count; i++)
        emit_insn (gen_move_scw (dst_reg, dst_reg, regs[i]));
    }
  else
    {
      for (i = 0; i < reg_count; i++)
        emit_insn (gen_move_sw_a (dst_reg,
                                  dst_reg, gen_int_mode (4, SImode), regs[i]));
    }
}

/* Generate loop foot, copy the leftover bytes.  */
static void
mdx_block_move_loop_foot (rtx dst_reg, HOST_WIDE_INT dst_align,
                          rtx src_reg, HOST_WIDE_INT src_align,
                          HOST_WIDE_INT length)
{
  bool src_unaligned = (src_align < BITS_PER_WORD);
  bool dst_unaligned = (dst_align < BITS_PER_WORD);

  HOST_WIDE_INT leftover;

  leftover = length % UNITS_PER_WORD;
  length -= leftover;

  if (length > 0)
    mdx_block_move_loop_body (dst_reg, dst_align,
                              src_reg, src_align, length);

  if (dst_unaligned)
    emit_insn (gen_move_sce (dst_reg, dst_reg));

  if (leftover > 0)
    {
      HOST_WIDE_INT src_adj = src_unaligned ? -4 : 0;
      HOST_WIDE_INT dst_adj = dst_unaligned ? -4 : 0;
      rtx temp;

      gcc_assert (leftover < UNITS_PER_WORD);

      if (leftover >= UNITS_PER_WORD / 2
          && src_align >= BITS_PER_WORD / 2
          && dst_align >= BITS_PER_WORD / 2)
        {
          temp = gen_reg_rtx (HImode);
          emit_insn (gen_move_lhu_b (src_reg, src_reg,
                                     gen_int_mode (src_adj, SImode), temp));
          emit_insn (gen_move_sh_b (dst_reg, dst_reg,
                                    gen_int_mode (dst_adj, SImode), temp));
          leftover -= UNITS_PER_WORD / 2;
          src_adj = UNITS_PER_WORD / 2;
          dst_adj = UNITS_PER_WORD / 2;
        }

      while (leftover > 0)
        {
          temp = gen_reg_rtx (QImode);
          emit_insn (gen_move_lbu_b (src_reg, src_reg,
                                     gen_int_mode (src_adj, SImode), temp));
          emit_insn (gen_move_sb_b (dst_reg, dst_reg,
                                    gen_int_mode (dst_adj, SImode), temp));
          leftover--;
          src_adj = 1;
          dst_adj = 1;
        }
    }
}

#define MIN_MOVE_REGS 3
#define MIN_MOVE_BYTES (MIN_MOVE_REGS * UNITS_PER_WORD)
#define MAX_MOVE_REGS 4
#define MAX_MOVE_BYTES (MAX_MOVE_REGS * UNITS_PER_WORD)

/* The length is large, generate a loop if necessary.
   The loop is consisted by loop head/body/foot.  */
static void
mdx_block_move_loop (rtx dst, rtx src, HOST_WIDE_INT length)
{
  HOST_WIDE_INT src_align = MEM_ALIGN (src);
  HOST_WIDE_INT dst_align = MEM_ALIGN (dst);
  HOST_WIDE_INT loop_mov_bytes;
  HOST_WIDE_INT iteration = 0;
  HOST_WIDE_INT head_length = 0, leftover;
  rtx label, src_reg, dst_reg, final_dst;

  bool gen_loop_head = (src_align < BITS_PER_WORD
                        || dst_align < BITS_PER_WORD);

  if (gen_loop_head)
    head_length += UNITS_PER_WORD;

  for (loop_mov_bytes = MAX_MOVE_BYTES;
       loop_mov_bytes >= MIN_MOVE_BYTES;
       loop_mov_bytes -= UNITS_PER_WORD)
    {
      iteration = (length - head_length) / loop_mov_bytes;
      if (iteration > 1)
        break;
    }
  if (iteration <= 1)
    {
      mdx_block_move_straight (dst, src, length);
      return;
    }

  leftover = (length - head_length) % loop_mov_bytes;
  length -= leftover;

  src_reg = copy_addr_to_reg (XEXP (src, 0));
  dst_reg = copy_addr_to_reg (XEXP (dst, 0));
  final_dst = expand_simple_binop (Pmode, PLUS, dst_reg, GEN_INT (length),
                                   0, 0, OPTAB_WIDEN);

  if (gen_loop_head)
    mdx_block_move_loop_head (dst_reg, dst_align,
                              src_reg, src_align, head_length);

  label = gen_label_rtx ();
  emit_label (label);

  mdx_block_move_loop_body (dst_reg, dst_align,
                            src_reg, src_align, loop_mov_bytes);

  emit_insn (gen_cmpsi (dst_reg, final_dst));
  emit_jump_insn (gen_bne (label));

  mdx_block_move_loop_foot (dst_reg, dst_align,
                            src_reg, src_align, leftover);
}

/* Generate block move, for misc.md: "movmemsi".  */
bool
mdx_block_move (rtx *ops)
{
  rtx dst = ops[0];
  rtx src = ops[1];
  rtx length = ops[2];

  if (TARGET_LITTLE_ENDIAN
      && (MEM_ALIGN (src) < BITS_PER_WORD || MEM_ALIGN (dst) < BITS_PER_WORD)
      && INTVAL (length) >= UNITS_PER_WORD)
    return false;

  if (GET_CODE (length) == CONST_INT)
    {
      if (INTVAL (length) <= 2 * MAX_MOVE_BYTES)
        {
           mdx_block_move_straight (dst, src, INTVAL (length));
           return true;
        }
      else if (optimize &&
               !(flag_unroll_loops || flag_unroll_all_loops))
        {
          mdx_block_move_loop (dst, src, INTVAL (length));
          return true;
        }
    }
  return false;
}

/* Generate add insn.  */
const char *
mdp_select_add_imm (rtx *ops, bool set_cc)
{
  HOST_WIDE_INT v = INTVAL (ops[2]);

  gcc_assert (GET_CODE (ops[2]) == CONST_INT);
  gcc_assert (REGNO (ops[0]) == REGNO (ops[1]));

  if (set_cc && G16_REG_P (REGNO (ops[0])))
    {
      if (v > 0 && IMM_IS_POW_OF_2 ((unsigned HOST_WIDE_INT) v, 0, 15))
        {
          ops[2] = GEN_INT (ffs (v) - 1);
          return "addei!  %0, %c2";
        }

      if (v < 0 && IMM_IS_POW_OF_2 ((unsigned HOST_WIDE_INT) (-v), 0, 15))
        {
          ops[2] = GEN_INT (ffs (-v) - 1);
          return "subei!  %0, %c2";
        }
    }

  if (set_cc)
    return "addi.c  %0, %c2";
  else
    return "addi    %0, %c2";
}

/* Output arith insn.  */
const char *
mdp_select (rtx *ops, const char *inst_pre,
            bool commu, const char *letter, bool set_cc)
{
  gcc_assert (GET_CODE (ops[0]) == REG);
  gcc_assert (GET_CODE (ops[1]) == REG);

  if (set_cc && G16_REG_P (REGNO (ops[0]))
      && (GET_CODE (ops[2]) == REG ? G16_REG_P (REGNO (ops[2])) : 1)
      && REGNO (ops[0]) == REGNO (ops[1]))
    {
      snprintf (ins, INS_BUF_SZ, "%s!  %%0, %%%s2", inst_pre, letter);
      return ins;
    }

  if (commu && set_cc && G16_REG_P (REGNO (ops[0]))
      && G16_REG_P (REGNO (ops[1]))
      && REGNO (ops[0]) == REGNO (ops[2]))
    {
      gcc_assert (GET_CODE (ops[2]) == REG);
      snprintf (ins, INS_BUF_SZ, "%s!  %%0, %%%s1", inst_pre, letter);
      return ins;
    }

  if (set_cc)
    snprintf (ins, INS_BUF_SZ, "%s.c  %%0, %%1, %%%s2", inst_pre, letter);
  else
    snprintf (ins, INS_BUF_SZ, "%s    %%0, %%1, %%%s2", inst_pre, letter);
  return ins;
}

