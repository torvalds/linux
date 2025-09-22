/* Subroutines for code generation on Motorola 68HC11 and 68HC12.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Stephane Carrez (stcarrez@nerim.fr)

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.

Note:
   A first 68HC11 port was made by Otto Lind (otto@coactive.com)
   on gcc 2.6.3.  I have used it as a starting point for this port.
   However, this new port is a complete re-write.  Its internal
   design is completely different.  The generated code is not
   compatible with the gcc 2.6.3 port.

   The gcc 2.6.3 port is available at:

   ftp.unina.it/pub/electronics/motorola/68hc11/gcc/gcc-6811-fsf.tar.gz

*/

#include <stdio.h>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "recog.h"
#include "expr.h"
#include "libfuncs.h"
#include "toplev.h"
#include "basic-block.h"
#include "function.h"
#include "ggc.h"
#include "reload.h"
#include "target.h"
#include "target-def.h"

static void emit_move_after_reload (rtx, rtx, rtx);
static rtx simplify_logical (enum machine_mode, int, rtx, rtx *);
static void m68hc11_emit_logical (enum machine_mode, int, rtx *);
static void m68hc11_reorg (void);
static int go_if_legitimate_address_internal (rtx, enum machine_mode, int);
static rtx m68hc11_expand_compare (enum rtx_code, rtx, rtx);
static int must_parenthesize (rtx);
static int m68hc11_address_cost (rtx);
static int m68hc11_shift_cost (enum machine_mode, rtx, int);
static int m68hc11_rtx_costs_1 (rtx, enum rtx_code, enum rtx_code);
static bool m68hc11_rtx_costs (rtx, int, int, int *);
static tree m68hc11_handle_fntype_attribute (tree *, tree, tree, int, bool *);
const struct attribute_spec m68hc11_attribute_table[];

void create_regs_rtx (void);

static void asm_print_register (FILE *, int);
static void m68hc11_output_function_epilogue (FILE *, HOST_WIDE_INT);
static void m68hc11_asm_out_constructor (rtx, int);
static void m68hc11_asm_out_destructor (rtx, int);
static void m68hc11_file_start (void);
static void m68hc11_encode_section_info (tree, rtx, int);
static const char *m68hc11_strip_name_encoding (const char* str);
static unsigned int m68hc11_section_type_flags (tree, const char*, int);
static int autoinc_mode (rtx);
static int m68hc11_make_autoinc_notes (rtx *, void *);
static void m68hc11_init_libfuncs (void);
static rtx m68hc11_struct_value_rtx (tree, int);
static bool m68hc11_return_in_memory (tree, tree);

/* Must be set to 1 to produce debug messages.  */
int debug_m6811 = 0;

extern FILE *asm_out_file;

rtx ix_reg;
rtx iy_reg;
rtx d_reg;
rtx m68hc11_soft_tmp_reg;
static GTY(()) rtx stack_push_word;
static GTY(()) rtx stack_pop_word;
static GTY(()) rtx z_reg;
static GTY(()) rtx z_reg_qi;
static int regs_inited = 0;

/* Set to 1 by expand_prologue() when the function is an interrupt handler.  */
int current_function_interrupt;

/* Set to 1 by expand_prologue() when the function is a trap handler.  */
int current_function_trap;

/* Set to 1 when the current function is placed in 68HC12 banked
   memory and must return with rtc.  */
int current_function_far;

/* Min offset that is valid for the indirect addressing mode.  */
HOST_WIDE_INT m68hc11_min_offset = 0;

/* Max offset that is valid for the indirect addressing mode.  */
HOST_WIDE_INT m68hc11_max_offset = 256;

/* The class value for base registers.  */
enum reg_class m68hc11_base_reg_class = A_REGS;

/* The class value for index registers.  This is NO_REGS for 68HC11.  */
enum reg_class m68hc11_index_reg_class = NO_REGS;

enum reg_class m68hc11_tmp_regs_class = NO_REGS;

/* Tables that tell whether a given hard register is valid for
   a base or an index register.  It is filled at init time depending
   on the target processor.  */
unsigned char m68hc11_reg_valid_for_base[FIRST_PSEUDO_REGISTER];
unsigned char m68hc11_reg_valid_for_index[FIRST_PSEUDO_REGISTER];

/* A correction offset which is applied to the stack pointer.
   This is 1 for 68HC11 and 0 for 68HC12.  */
int m68hc11_sp_correction;

int m68hc11_addr_mode;
int m68hc11_mov_addr_mode;

/* Comparison operands saved by the "tstxx" and "cmpxx" expand patterns.  */
rtx m68hc11_compare_op0;
rtx m68hc11_compare_op1;


const struct processor_costs *m68hc11_cost;

/* Costs for a 68HC11.  */
static const struct processor_costs m6811_cost = {
  /* add */
  COSTS_N_INSNS (2),
  /* logical */
  COSTS_N_INSNS (2),
  /* non-constant shift */
  COSTS_N_INSNS (20),
  /* shiftQI const */
  { COSTS_N_INSNS (0), COSTS_N_INSNS (1), COSTS_N_INSNS (2),
    COSTS_N_INSNS (3), COSTS_N_INSNS (4), COSTS_N_INSNS (3),
    COSTS_N_INSNS (2), COSTS_N_INSNS (1) },

  /* shiftHI const */
  { COSTS_N_INSNS (0), COSTS_N_INSNS (1), COSTS_N_INSNS (4),
    COSTS_N_INSNS (6), COSTS_N_INSNS (8), COSTS_N_INSNS (6),
    COSTS_N_INSNS (4), COSTS_N_INSNS (2),
    COSTS_N_INSNS (2), COSTS_N_INSNS (4),
    COSTS_N_INSNS (6), COSTS_N_INSNS (8), COSTS_N_INSNS (10),
    COSTS_N_INSNS (8), COSTS_N_INSNS (6), COSTS_N_INSNS (4)
  },
  /* mulQI */
  COSTS_N_INSNS (20),
  /* mulHI */
  COSTS_N_INSNS (20 * 4),
  /* mulSI */
  COSTS_N_INSNS (20 * 16),
  /* divQI */
  COSTS_N_INSNS (20),
  /* divHI */
  COSTS_N_INSNS (80),
  /* divSI */
  COSTS_N_INSNS (100)
};

/* Costs for a 68HC12.  */
static const struct processor_costs m6812_cost = {
  /* add */
  COSTS_N_INSNS (2),
  /* logical */
  COSTS_N_INSNS (2),
  /* non-constant shift */
  COSTS_N_INSNS (20),
  /* shiftQI const */
  { COSTS_N_INSNS (0), COSTS_N_INSNS (1), COSTS_N_INSNS (2),
    COSTS_N_INSNS (3), COSTS_N_INSNS (4), COSTS_N_INSNS (3),
    COSTS_N_INSNS (2), COSTS_N_INSNS (1) },

  /* shiftHI const */
  { COSTS_N_INSNS (0), COSTS_N_INSNS (1), COSTS_N_INSNS (4),
    COSTS_N_INSNS (6), COSTS_N_INSNS (8), COSTS_N_INSNS (6),
    COSTS_N_INSNS (4), COSTS_N_INSNS (2),
    COSTS_N_INSNS (2), COSTS_N_INSNS (4), COSTS_N_INSNS (6),
    COSTS_N_INSNS (8), COSTS_N_INSNS (10), COSTS_N_INSNS (8),
    COSTS_N_INSNS (6), COSTS_N_INSNS (4)
  },
  /* mulQI */
  COSTS_N_INSNS (3),
  /* mulHI */
  COSTS_N_INSNS (3),
  /* mulSI */
  COSTS_N_INSNS (3 * 4),
  /* divQI */
  COSTS_N_INSNS (12),
  /* divHI */
  COSTS_N_INSNS (12),
  /* divSI */
  COSTS_N_INSNS (100)
};

/* Initialize the GCC target structure.  */
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE m68hc11_attribute_table

#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"

#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE m68hc11_output_function_epilogue

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START m68hc11_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS TARGET_DEFAULT

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO  m68hc11_encode_section_info

#undef TARGET_SECTION_TYPE_FLAGS
#define TARGET_SECTION_TYPE_FLAGS m68hc11_section_type_flags

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS m68hc11_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST m68hc11_address_cost

#undef TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG m68hc11_reorg

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS m68hc11_init_libfuncs

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX m68hc11_struct_value_rtx
#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY m68hc11_return_in_memory
#undef TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES hook_callee_copies_named

#undef TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING m68hc11_strip_name_encoding

struct gcc_target targetm = TARGET_INITIALIZER;

int
m68hc11_override_options (void)
{
  memset (m68hc11_reg_valid_for_index, 0,
	  sizeof (m68hc11_reg_valid_for_index));
  memset (m68hc11_reg_valid_for_base, 0, sizeof (m68hc11_reg_valid_for_base));

  /* Compilation with -fpic generates a wrong code.  */
  if (flag_pic)
    {
      warning (0, "-f%s ignored for 68HC11/68HC12 (not supported)",
	       (flag_pic > 1) ? "PIC" : "pic");
      flag_pic = 0;
    }

  /* Do not enable -fweb because it breaks the 32-bit shift patterns
     by breaking the match_dup of those patterns.  The shift patterns
     will no longer be recognized after that.  */
  flag_web = 0;

  /* Configure for a 68hc11 processor.  */
  if (TARGET_M6811)
    {
      target_flags &= ~(TARGET_AUTO_INC_DEC | TARGET_MIN_MAX);
      m68hc11_cost = &m6811_cost;
      m68hc11_min_offset = 0;
      m68hc11_max_offset = 256;
      m68hc11_index_reg_class = NO_REGS;
      m68hc11_base_reg_class = A_REGS;
      m68hc11_reg_valid_for_base[HARD_X_REGNUM] = 1;
      m68hc11_reg_valid_for_base[HARD_Y_REGNUM] = 1;
      m68hc11_reg_valid_for_base[HARD_Z_REGNUM] = 1;
      m68hc11_sp_correction = 1;
      m68hc11_tmp_regs_class = D_REGS;
      m68hc11_addr_mode = ADDR_OFFSET;
      m68hc11_mov_addr_mode = 0;
      if (m68hc11_soft_reg_count < 0)
	m68hc11_soft_reg_count = 4;
    }

  /* Configure for a 68hc12 processor.  */
  if (TARGET_M6812)
    {
      m68hc11_cost = &m6812_cost;
      m68hc11_min_offset = -65536;
      m68hc11_max_offset = 65536;
      m68hc11_index_reg_class = D_REGS;
      m68hc11_base_reg_class = A_OR_SP_REGS;
      m68hc11_reg_valid_for_base[HARD_X_REGNUM] = 1;
      m68hc11_reg_valid_for_base[HARD_Y_REGNUM] = 1;
      m68hc11_reg_valid_for_base[HARD_Z_REGNUM] = 1;
      m68hc11_reg_valid_for_base[HARD_SP_REGNUM] = 1;
      m68hc11_reg_valid_for_index[HARD_D_REGNUM] = 1;
      m68hc11_sp_correction = 0;
      m68hc11_tmp_regs_class = TMP_REGS;
      m68hc11_addr_mode = ADDR_INDIRECT | ADDR_OFFSET | ADDR_CONST
        | (TARGET_AUTO_INC_DEC ? ADDR_INCDEC : 0);
      m68hc11_mov_addr_mode = ADDR_OFFSET | ADDR_CONST
        | (TARGET_AUTO_INC_DEC ? ADDR_INCDEC : 0);
      target_flags |= MASK_NO_DIRECT_MODE;
      if (m68hc11_soft_reg_count < 0)
	m68hc11_soft_reg_count = 0;

      if (TARGET_LONG_CALLS)
        current_function_far = 1;
    }
  return 0;
}


void
m68hc11_conditional_register_usage (void)
{
  int i;

  if (m68hc11_soft_reg_count > SOFT_REG_LAST - SOFT_REG_FIRST)
    m68hc11_soft_reg_count = SOFT_REG_LAST - SOFT_REG_FIRST;

  for (i = SOFT_REG_FIRST + m68hc11_soft_reg_count; i < SOFT_REG_LAST; i++)
    {
      fixed_regs[i] = 1;
      call_used_regs[i] = 1;
    }

  /* For 68HC12, the Z register emulation is not necessary when the
     frame pointer is not used.  The frame pointer is eliminated and
     replaced by the stack register (which is a BASE_REG_CLASS).  */
  if (TARGET_M6812 && flag_omit_frame_pointer && optimize)
    {
      fixed_regs[HARD_Z_REGNUM] = 1;
    }
}


/* Reload and register operations.  */


void
create_regs_rtx (void)
{
  /*  regs_inited = 1; */
  ix_reg = gen_rtx_REG (HImode, HARD_X_REGNUM);
  iy_reg = gen_rtx_REG (HImode, HARD_Y_REGNUM);
  d_reg = gen_rtx_REG (HImode, HARD_D_REGNUM);
  m68hc11_soft_tmp_reg = gen_rtx_REG (HImode, SOFT_TMP_REGNUM);

  stack_push_word = gen_rtx_MEM (HImode,
			     gen_rtx_PRE_DEC (HImode,
				      gen_rtx_REG (HImode, HARD_SP_REGNUM)));
  stack_pop_word = gen_rtx_MEM (HImode,
			    gen_rtx_POST_INC (HImode,
				     gen_rtx_REG (HImode, HARD_SP_REGNUM)));

}

/* Value is 1 if hard register REGNO can hold a value of machine-mode MODE.
    - 8 bit values are stored anywhere (except the SP register).
    - 16 bit values can be stored in any register whose mode is 16
    - 32 bit values can be stored in D, X registers or in a soft register
      (except the last one because we need 2 soft registers)
    - Values whose size is > 32 bit are not stored in real hard
      registers.  They may be stored in soft registers if there are
      enough of them.  */
int
hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  switch (GET_MODE_SIZE (mode))
    {
    case 8:
      return S_REGNO_P (regno) && m68hc11_soft_reg_count >= 4;

    case 4:
      return (X_REGNO_P (regno)
	      || (S_REGNO_P (regno) && m68hc11_soft_reg_count >= 2));

    case 2:
      return G_REGNO_P (regno);

    case 1:
      /* We have to accept a QImode in X or Y registers.  Otherwise, the
         reload pass will fail when some (SUBREG:QI (REG:HI X)) are defined
         in the insns.  Reload fails if the insn rejects the register class 'a'
         as well as if it accepts it.  Patterns that failed were
         zero_extend_qihi2 and iorqi3.  */

      return G_REGNO_P (regno) && !SP_REGNO_P (regno);

    default:
      return 0;
    }
}

int
m68hc11_hard_regno_rename_ok (int reg1, int reg2)
{
  /* Don't accept renaming to Z register.  We will replace it to
     X,Y or D during machine reorg pass.  */
  if (reg2 == HARD_Z_REGNUM)
    return 0;

  /* Don't accept renaming D,X to Y register as the code will be bigger.  */
  if (TARGET_M6811 && reg2 == HARD_Y_REGNUM
      && (D_REGNO_P (reg1) || X_REGNO_P (reg1)))
    return 0;

  return 1;
}

enum reg_class
preferred_reload_class (rtx operand, enum reg_class class)
{
  enum machine_mode mode;

  mode = GET_MODE (operand);

  if (debug_m6811)
    {
      printf ("Preferred reload: (class=%s): ", reg_class_names[class]);
    }

  if (class == D_OR_A_OR_S_REGS && SP_REG_P (operand))
    return m68hc11_base_reg_class;

  if (class >= S_REGS && (GET_CODE (operand) == MEM
			  || GET_CODE (operand) == CONST_INT))
    {
      /* S_REGS class must not be used.  The movhi template does not
         work to move a memory to a soft register.
         Restrict to a hard reg.  */
      switch (class)
	{
	default:
	case G_REGS:
	case D_OR_A_OR_S_REGS:
	  class = A_OR_D_REGS;
	  break;
	case A_OR_S_REGS:
	  class = A_REGS;
	  break;
	case D_OR_SP_OR_S_REGS:
	  class = D_OR_SP_REGS;
	  break;
	case D_OR_Y_OR_S_REGS:
	  class = D_OR_Y_REGS;
	  break;
	case D_OR_X_OR_S_REGS:
	  class = D_OR_X_REGS;
	  break;
	case SP_OR_S_REGS:
	  class = SP_REGS;
	  break;
	case Y_OR_S_REGS:
	  class = Y_REGS;
	  break;
	case X_OR_S_REGS:
	  class = X_REGS;
	  break;
	case D_OR_S_REGS:
	  class = D_REGS;
	}
    }
  else if (class == Y_REGS && GET_CODE (operand) == MEM)
    {
      class = Y_REGS;
    }
  else if (class == A_OR_D_REGS && GET_MODE_SIZE (mode) == 4)
    {
      class = D_OR_X_REGS;
    }
  else if (class >= S_REGS && S_REG_P (operand))
    {
      switch (class)
	{
	default:
	case G_REGS:
	case D_OR_A_OR_S_REGS:
	  class = A_OR_D_REGS;
	  break;
	case A_OR_S_REGS:
	  class = A_REGS;
	  break;
	case D_OR_SP_OR_S_REGS:
	  class = D_OR_SP_REGS;
	  break;
	case D_OR_Y_OR_S_REGS:
	  class = D_OR_Y_REGS;
	  break;
	case D_OR_X_OR_S_REGS:
	  class = D_OR_X_REGS;
	  break;
	case SP_OR_S_REGS:
	  class = SP_REGS;
	  break;
	case Y_OR_S_REGS:
	  class = Y_REGS;
	  break;
	case X_OR_S_REGS:
	  class = X_REGS;
	  break;
	case D_OR_S_REGS:
	  class = D_REGS;
	}
    }
  else if (class >= S_REGS)
    {
      if (debug_m6811)
	{
	  printf ("Class = %s for: ", reg_class_names[class]);
	  fflush (stdout);
	  debug_rtx (operand);
	}
    }

  if (debug_m6811)
    {
      printf (" => class=%s\n", reg_class_names[class]);
      fflush (stdout);
      debug_rtx (operand);
    }

  return class;
}

/* Return 1 if the operand is a valid indexed addressing mode.
   For 68hc11:  n,r    with n in [0..255] and r in A_REGS class
   For 68hc12:  n,r    no constraint on the constant, r in A_REGS class.  */
int
m68hc11_valid_addressing_p (rtx operand, enum machine_mode mode, int addr_mode)
{
  rtx base, offset;

  switch (GET_CODE (operand))
    {
    case MEM:
      if ((addr_mode & ADDR_INDIRECT) && GET_MODE_SIZE (mode) <= 2)
        return m68hc11_valid_addressing_p (XEXP (operand, 0), mode,
                                   addr_mode & (ADDR_STRICT | ADDR_OFFSET));
      return 0;

    case POST_INC:
    case PRE_INC:
    case POST_DEC:
    case PRE_DEC:
      if (addr_mode & ADDR_INCDEC)
	return m68hc11_valid_addressing_p (XEXP (operand, 0), mode,
                                   addr_mode & ADDR_STRICT);
      return 0;

    case PLUS:
      base = XEXP (operand, 0);
      if (GET_CODE (base) == MEM)
	return 0;

      offset = XEXP (operand, 1);
      if (GET_CODE (offset) == MEM)
	return 0;

      /* Indexed addressing mode with 2 registers.  */
      if (GET_CODE (base) == REG && GET_CODE (offset) == REG)
        {
          if (!(addr_mode & ADDR_INDEXED))
            return 0;

          addr_mode &= ADDR_STRICT;
          if (REGNO_OK_FOR_BASE_P2 (REGNO (base), addr_mode)
              && REGNO_OK_FOR_INDEX_P2 (REGNO (offset), addr_mode))
            return 1;

          if (REGNO_OK_FOR_BASE_P2 (REGNO (offset), addr_mode)
              && REGNO_OK_FOR_INDEX_P2 (REGNO (base), addr_mode))
            return 1;

          return 0;
        }

      if (!(addr_mode & ADDR_OFFSET))
        return 0;

      if (GET_CODE (base) == REG)
	{
          if (!VALID_CONSTANT_OFFSET_P (offset, mode))
	    return 0;

	  if (!(addr_mode & ADDR_STRICT))
	    return 1;

	  return REGNO_OK_FOR_BASE_P2 (REGNO (base), 1);
	}

      if (GET_CODE (offset) == REG)
	{
	  if (!VALID_CONSTANT_OFFSET_P (base, mode))
	    return 0;

	  if (!(addr_mode & ADDR_STRICT))
	    return 1;

	  return REGNO_OK_FOR_BASE_P2 (REGNO (offset), 1);
	}
      return 0;

    case REG:
      return REGNO_OK_FOR_BASE_P2 (REGNO (operand), addr_mode & ADDR_STRICT);

    case CONST_INT:
      if (addr_mode & ADDR_CONST)
        return VALID_CONSTANT_OFFSET_P (operand, mode);
      return 0;

    default:
      return 0;
    }
}

/* Returns 1 if the operand fits in a 68HC11 indirect mode or in
   a 68HC12 1-byte index addressing mode.  */
int
m68hc11_small_indexed_indirect_p (rtx operand, enum machine_mode mode)
{
  rtx base, offset;
  int addr_mode;

  if (GET_CODE (operand) == REG && reload_in_progress
      && REGNO (operand) >= FIRST_PSEUDO_REGISTER
      && reg_equiv_memory_loc[REGNO (operand)])
    {
      operand = reg_equiv_memory_loc[REGNO (operand)];
      operand = eliminate_regs (operand, 0, NULL_RTX);
    }

  if (GET_CODE (operand) != MEM)
    return 0;

  operand = XEXP (operand, 0);
  if (CONSTANT_ADDRESS_P (operand))
    return 1;

  if (PUSH_POP_ADDRESS_P (operand))
    return 1;

  addr_mode = m68hc11_mov_addr_mode | (reload_completed ? ADDR_STRICT : 0);
  if (!m68hc11_valid_addressing_p (operand, mode, addr_mode))
    return 0;

  if (TARGET_M6812 && GET_CODE (operand) == PLUS
      && (reload_completed | reload_in_progress))
    {
      base = XEXP (operand, 0);
      offset = XEXP (operand, 1);

      /* The offset can be a symbol address and this is too big
         for the operand constraint.  */
      if (GET_CODE (base) != CONST_INT && GET_CODE (offset) != CONST_INT)
        return 0;

      if (GET_CODE (base) == CONST_INT)
	offset = base;

      switch (GET_MODE_SIZE (mode))
	{
	case 8:
	  if (INTVAL (offset) < -16 + 6 || INTVAL (offset) > 15 - 6)
	    return 0;
	  break;

	case 4:
	  if (INTVAL (offset) < -16 + 2 || INTVAL (offset) > 15 - 2)
	    return 0;
	  break;

	default:
	  if (INTVAL (offset) < -16 || INTVAL (offset) > 15)
	    return 0;
	  break;
	}
    }
  return 1;
}

int
m68hc11_register_indirect_p (rtx operand, enum machine_mode mode)
{
  int addr_mode;

  if (GET_CODE (operand) == REG && reload_in_progress
      && REGNO (operand) >= FIRST_PSEUDO_REGISTER
      && reg_equiv_memory_loc[REGNO (operand)])
    {
      operand = reg_equiv_memory_loc[REGNO (operand)];
      operand = eliminate_regs (operand, 0, NULL_RTX);
    }
  if (GET_CODE (operand) != MEM)
    return 0;

  operand = XEXP (operand, 0);
  addr_mode = m68hc11_addr_mode | (reload_completed ? ADDR_STRICT : 0);
  return m68hc11_valid_addressing_p (operand, mode, addr_mode);
}

static int
go_if_legitimate_address_internal (rtx operand, enum machine_mode mode,
                                   int strict)
{
  int addr_mode;

  if (CONSTANT_ADDRESS_P (operand) && TARGET_M6812)
    {
      /* Reject the global variables if they are too wide.  This forces
         a load of their address in a register and generates smaller code.  */
      if (GET_MODE_SIZE (mode) == 8)
	return 0;

      return 1;
    }
  addr_mode = m68hc11_addr_mode | (strict ? ADDR_STRICT : 0);
  if (m68hc11_valid_addressing_p (operand, mode, addr_mode))
    {
      return 1;
    }
  if (PUSH_POP_ADDRESS_P (operand))
    {
      return 1;
    }
  if (symbolic_memory_operand (operand, mode))
    {
      return 1;
    }
  return 0;
}

int
m68hc11_go_if_legitimate_address (rtx operand, enum machine_mode mode,
                                  int strict)
{
  int result;

  if (debug_m6811)
    {
      printf ("Checking: ");
      fflush (stdout);
      debug_rtx (operand);
    }

  result = go_if_legitimate_address_internal (operand, mode, strict);

  if (debug_m6811)
    {
      printf (" -> %s\n", result == 0 ? "NO" : "YES");
    }

  if (result == 0)
    {
      if (debug_m6811)
	{
	  printf ("go_if_legitimate%s, ret 0: %d:",
		  (strict ? "_strict" : ""), mode);
	  fflush (stdout);
	  debug_rtx (operand);
	}
    }
  return result;
}

int
m68hc11_legitimize_address (rtx *operand ATTRIBUTE_UNUSED,
                            rtx old_operand ATTRIBUTE_UNUSED,
                            enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return 0;
}


int
m68hc11_reload_operands (rtx operands[])
{
  enum machine_mode mode;

  if (regs_inited == 0)
    create_regs_rtx ();

  mode = GET_MODE (operands[1]);

  /* Input reload of indirect addressing (MEM (PLUS (REG) (CONST))).  */
  if (A_REG_P (operands[0]) && memory_reload_operand (operands[1], mode))
    {
      rtx big_offset = XEXP (XEXP (operands[1], 0), 1);
      rtx base = XEXP (XEXP (operands[1], 0), 0);

      if (GET_CODE (base) != REG)
	{
	  rtx tmp = base;
	  base = big_offset;
	  big_offset = tmp;
	}

      /* If the offset is out of range, we have to compute the address
         with a separate add instruction.  We try to do this with an 8-bit
         add on the A register.  This is possible only if the lowest part
         of the offset (i.e., big_offset % 256) is a valid constant offset
         with respect to the mode.  If it's not, we have to generate a
         16-bit add on the D register.  From:
       
         (SET (REG X (MEM (PLUS (REG X) (CONST_INT 1000)))))
       
         we generate:
        
         [(SET (REG D) (REG X)) (SET (REG X) (REG D))]
         (SET (REG A) (PLUS (REG A) (CONST_INT 1000 / 256)))
         [(SET (REG D) (REG X)) (SET (REG X) (REG D))]
         (SET (REG X) (MEM (PLUS (REG X) (CONST_INT 1000 % 256)))
       
         (SET (REG X) (PLUS (REG X) (CONST_INT 1000 / 256 * 256)))
         (SET (REG X) (MEM (PLUS (REG X) (CONST_INT 1000 % 256)))) 

      */
      if (!VALID_CONSTANT_OFFSET_P (big_offset, mode))
	{
	  int vh, vl;
	  rtx reg = operands[0];
	  rtx offset;
	  int val = INTVAL (big_offset);


	  /* We use the 'operands[0]' as a scratch register to compute the
	     address. Make sure 'base' is in that register.  */
	  if (!rtx_equal_p (base, operands[0]))
	    {
	      emit_move_insn (reg, base);
	    }

	  if (val > 0)
	    {
	      vh = val >> 8;
	      vl = val & 0x0FF;
	    }
	  else
	    {
	      vh = (val >> 8) & 0x0FF;
	      vl = val & 0x0FF;
	    }

	  /* Create the lowest part offset that still remains to be added.
	     If it's not a valid offset, do a 16-bit add.  */
	  offset = GEN_INT (vl);
	  if (!VALID_CONSTANT_OFFSET_P (offset, mode))
	    {
	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				  gen_rtx_PLUS (HImode, reg, big_offset)));
	      offset = const0_rtx;
	    }
	  else
	    {
	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				  gen_rtx_PLUS (HImode, reg,
					   GEN_INT (vh << 8))));
	    }
	  emit_move_insn (operands[0],
			  gen_rtx_MEM (GET_MODE (operands[1]),
				   gen_rtx_PLUS (Pmode, reg, offset)));
	  return 1;
	}
    }

  /* Use the normal gen_movhi pattern.  */
  return 0;
}

void
m68hc11_emit_libcall (const char *name, enum rtx_code code,
                      enum machine_mode dmode, enum machine_mode smode,
                      int noperands, rtx *operands)
{
  rtx ret;
  rtx insns;
  rtx libcall;
  rtx equiv;

  start_sequence ();
  libcall = gen_rtx_SYMBOL_REF (Pmode, name);
  switch (noperands)
    {
    case 2:
      ret = emit_library_call_value (libcall, NULL_RTX, LCT_CONST,
                                     dmode, 1, operands[1], smode);
      equiv = gen_rtx_fmt_e (code, dmode, operands[1]);
      break;

    case 3:
      ret = emit_library_call_value (libcall, NULL_RTX,
                                     LCT_CONST, dmode, 2,
                                     operands[1], smode, operands[2],
                                     smode);
      equiv = gen_rtx_fmt_ee (code, dmode, operands[1], operands[2]);
      break;

    default:
      gcc_unreachable ();
    }

  insns = get_insns ();
  end_sequence ();
  emit_libcall_block (insns, operands[0], ret, equiv);
}

/* Returns true if X is a PRE/POST increment decrement
   (same as auto_inc_p() in rtlanal.c but do not take into
   account the stack).  */
int
m68hc11_auto_inc_p (rtx x)
{
  return GET_CODE (x) == PRE_DEC
    || GET_CODE (x) == POST_INC
    || GET_CODE (x) == POST_DEC || GET_CODE (x) == PRE_INC;
}


/* Predicates for machine description.  */

int
memory_reload_operand (rtx operand, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return GET_CODE (operand) == MEM
    && GET_CODE (XEXP (operand, 0)) == PLUS
    && ((GET_CODE (XEXP (XEXP (operand, 0), 0)) == REG
	 && GET_CODE (XEXP (XEXP (operand, 0), 1)) == CONST_INT)
	|| (GET_CODE (XEXP (XEXP (operand, 0), 1)) == REG
	    && GET_CODE (XEXP (XEXP (operand, 0), 0)) == CONST_INT));
}

int
m68hc11_symbolic_p (rtx operand, enum machine_mode mode)
{
  if (GET_CODE (operand) == MEM)
    {
      rtx op = XEXP (operand, 0);

      if (symbolic_memory_operand (op, mode))
	return 1;
    }
  return 0;
}

int
m68hc11_indirect_p (rtx operand, enum machine_mode mode)
{
  if (GET_CODE (operand) == MEM && GET_MODE (operand) == mode)
    {
      rtx op = XEXP (operand, 0);
      int addr_mode;

      if (m68hc11_page0_symbol_p (op))
        return 1;

      if (symbolic_memory_operand (op, mode))
	return TARGET_M6812;

      if (reload_in_progress)
        return 1;

      operand = XEXP (operand, 0);
      addr_mode = m68hc11_addr_mode | (reload_completed ? ADDR_STRICT : 0);
      return m68hc11_valid_addressing_p (operand, mode, addr_mode);
    }
  return 0;
}

int
memory_indexed_operand (rtx operand, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  if (GET_CODE (operand) != MEM)
    return 0;

  operand = XEXP (operand, 0);
  if (GET_CODE (operand) == PLUS)
    {
      if (GET_CODE (XEXP (operand, 0)) == REG)
	operand = XEXP (operand, 0);
      else if (GET_CODE (XEXP (operand, 1)) == REG)
	operand = XEXP (operand, 1);
    }
  return GET_CODE (operand) == REG
    && (REGNO (operand) >= FIRST_PSEUDO_REGISTER
	|| A_REGNO_P (REGNO (operand)));
}

int
push_pop_operand_p (rtx operand)
{
  if (GET_CODE (operand) != MEM)
    {
      return 0;
    }
  operand = XEXP (operand, 0);
  return PUSH_POP_ADDRESS_P (operand);
}

/* Returns 1 if OP is either a symbol reference or a sum of a symbol
   reference and a constant.  */

int
symbolic_memory_operand (rtx op, enum machine_mode mode)
{
  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
    case LABEL_REF:
      return 1;

    case CONST:
      op = XEXP (op, 0);
      return ((GET_CODE (XEXP (op, 0)) == SYMBOL_REF
	       || GET_CODE (XEXP (op, 0)) == LABEL_REF)
	      && GET_CODE (XEXP (op, 1)) == CONST_INT);

      /* ??? This clause seems to be irrelevant.  */
    case CONST_DOUBLE:
      return GET_MODE (op) == mode;

    case PLUS:
      return symbolic_memory_operand (XEXP (op, 0), mode)
	&& symbolic_memory_operand (XEXP (op, 1), mode);

    default:
      return 0;
    }
}

/* Emit the code to build the trampoline used to call a nested function.
   
   68HC11               68HC12

   ldy #&CXT            movw #&CXT,*_.d1
   sty *_.d1            jmp FNADDR
   jmp FNADDR

*/
void
m68hc11_initialize_trampoline (rtx tramp, rtx fnaddr, rtx cxt)
{
  const char *static_chain_reg = reg_names[STATIC_CHAIN_REGNUM];

  /* Skip the '*'.  */
  if (*static_chain_reg == '*')
    static_chain_reg++;
  if (TARGET_M6811)
    {
      emit_move_insn (gen_rtx_MEM (HImode, tramp), GEN_INT (0x18ce));
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, 2)), cxt);
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, 4)),
                      GEN_INT (0x18df));
      emit_move_insn (gen_rtx_MEM (QImode, plus_constant (tramp, 6)),
                      gen_rtx_CONST (QImode,
                                     gen_rtx_SYMBOL_REF (Pmode,
                                                         static_chain_reg)));
      emit_move_insn (gen_rtx_MEM (QImode, plus_constant (tramp, 7)),
                      GEN_INT (0x7e));
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, 8)), fnaddr);
    }
  else
    {
      emit_move_insn (gen_rtx_MEM (HImode, tramp), GEN_INT (0x1803));
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, 2)), cxt);
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, 4)),
                      gen_rtx_CONST (HImode,
                                     gen_rtx_SYMBOL_REF (Pmode,
                                                         static_chain_reg)));
      emit_move_insn (gen_rtx_MEM (QImode, plus_constant (tramp, 6)),
                      GEN_INT (0x06));
      emit_move_insn (gen_rtx_MEM (HImode, plus_constant (tramp, 7)), fnaddr);
    }
}

/* Declaration of types.  */

/* Handle an "tiny_data" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
m68hc11_handle_page0_attribute (tree *node, tree name,
                                tree args ATTRIBUTE_UNUSED,
                                int flags ATTRIBUTE_UNUSED, bool *no_add_attrs)
{
  tree decl = *node;

  if (TREE_STATIC (decl) || DECL_EXTERNAL (decl))
    {
      DECL_SECTION_NAME (decl) = build_string (6, ".page0");
    }
  else
    {
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

const struct attribute_spec m68hc11_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt", 0, 0, false, true,  true,  m68hc11_handle_fntype_attribute },
  { "trap",      0, 0, false, true,  true,  m68hc11_handle_fntype_attribute },
  { "far",       0, 0, false, true,  true,  m68hc11_handle_fntype_attribute },
  { "near",      0, 0, false, true,  true,  m68hc11_handle_fntype_attribute },
  { "page0",     0, 0, false, false, false, m68hc11_handle_page0_attribute },
  { NULL,        0, 0, false, false, false, NULL }
};

/* Keep track of the symbol which has a `trap' attribute and which uses
   the `swi' calling convention.  Since there is only one trap, we only
   record one such symbol.  If there are several, a warning is reported.  */
static rtx trap_handler_symbol = 0;

/* Handle an attribute requiring a FUNCTION_TYPE, FIELD_DECL or TYPE_DECL;
   arguments as in struct attribute_spec.handler.  */
static tree
m68hc11_handle_fntype_attribute (tree *node, tree name,
                                 tree args ATTRIBUTE_UNUSED,
                                 int flags ATTRIBUTE_UNUSED,
                                 bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_TYPE
      && TREE_CODE (*node) != METHOD_TYPE
      && TREE_CODE (*node) != FIELD_DECL
      && TREE_CODE (*node) != TYPE_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}
/* Undo the effects of the above.  */

static const char *
m68hc11_strip_name_encoding (const char *str)
{
  return str + (*str == '*' || *str == '@' || *str == '&');
}

static void
m68hc11_encode_label (tree decl)
{
  const char *str = XSTR (XEXP (DECL_RTL (decl), 0), 0);
  int len = strlen (str);
  char *newstr = alloca (len + 2);

  newstr[0] = '@';
  strcpy (&newstr[1], str);

  XSTR (XEXP (DECL_RTL (decl), 0), 0) = ggc_alloc_string (newstr, len + 1);
}

/* Return 1 if this is a symbol in page0  */
int
m68hc11_page0_symbol_p (rtx x)
{
  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
      return XSTR (x, 0) != 0 && XSTR (x, 0)[0] == '@';

    case CONST:
      return m68hc11_page0_symbol_p (XEXP (x, 0));

    case PLUS:
      if (!m68hc11_page0_symbol_p (XEXP (x, 0)))
        return 0;

      return GET_CODE (XEXP (x, 1)) == CONST_INT
        && INTVAL (XEXP (x, 1)) < 256
        && INTVAL (XEXP (x, 1)) >= 0;

    default:
      return 0;
    }
}

/* We want to recognize trap handlers so that we handle calls to traps
   in a special manner (by issuing the trap).  This information is stored
   in SYMBOL_REF_FLAG.  */

static void
m68hc11_encode_section_info (tree decl, rtx rtl, int first ATTRIBUTE_UNUSED)
{
  tree func_attr;
  int trap_handler;
  int is_far = 0;
  
  if (TREE_CODE (decl) == VAR_DECL)
    {
      if (lookup_attribute ("page0", DECL_ATTRIBUTES (decl)) != 0)
        m68hc11_encode_label (decl);
      return;
    }

  if (TREE_CODE (decl) != FUNCTION_DECL)
    return;

  func_attr = TYPE_ATTRIBUTES (TREE_TYPE (decl));


  if (lookup_attribute ("far", func_attr) != NULL_TREE)
    is_far = 1;
  else if (lookup_attribute ("near", func_attr) == NULL_TREE)
    is_far = TARGET_LONG_CALLS != 0;

  trap_handler = lookup_attribute ("trap", func_attr) != NULL_TREE;
  if (trap_handler && is_far)
    {
      warning (OPT_Wattributes, "%<trap%> and %<far%> attributes are "
	       "not compatible, ignoring %<far%>");
      trap_handler = 0;
    }
  if (trap_handler)
    {
      if (trap_handler_symbol != 0)
        warning (OPT_Wattributes, "%<trap%> attribute is already used");
      else
        trap_handler_symbol = XEXP (rtl, 0);
    }
  SYMBOL_REF_FLAG (XEXP (rtl, 0)) = is_far;
}

static unsigned int
m68hc11_section_type_flags (tree decl, const char *name, int reloc)
{
  unsigned int flags = default_section_type_flags (decl, name, reloc);

  if (strncmp (name, ".eeprom", 7) == 0)
    {
      flags |= SECTION_WRITE | SECTION_CODE | SECTION_OVERRIDE;
    }

  return flags;
}

int
m68hc11_is_far_symbol (rtx sym)
{
  if (GET_CODE (sym) == MEM)
    sym = XEXP (sym, 0);

  return SYMBOL_REF_FLAG (sym);
}

int
m68hc11_is_trap_symbol (rtx sym)
{
  if (GET_CODE (sym) == MEM)
    sym = XEXP (sym, 0);

  return trap_handler_symbol != 0 && rtx_equal_p (trap_handler_symbol, sym);
}


/* Argument support functions.  */

/* Define the offset between two registers, one to be eliminated, and the
   other its replacement, at the start of a routine.  */
int
m68hc11_initial_elimination_offset (int from, int to)
{
  int trap_handler;
  tree func_attr;
  int size;
  int regno;

  /* For a trap handler, we must take into account the registers which
     are pushed on the stack during the trap (except the PC).  */
  func_attr = TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl));
  current_function_interrupt = lookup_attribute ("interrupt",
						 func_attr) != NULL_TREE;
  trap_handler = lookup_attribute ("trap", func_attr) != NULL_TREE;

  if (lookup_attribute ("far", func_attr) != 0)
    current_function_far = 1;
  else if (lookup_attribute ("near", func_attr) != 0)
    current_function_far = 0;
  else
    current_function_far = (TARGET_LONG_CALLS != 0
                            && !current_function_interrupt
                            && !trap_handler);

  if (trap_handler && from == ARG_POINTER_REGNUM)
    size = 7;

  /* For a function using 'call/rtc' we must take into account the
     page register which is pushed in the call.  */
  else if (current_function_far && from == ARG_POINTER_REGNUM)
    size = 1;
  else
    size = 0;

  if (from == ARG_POINTER_REGNUM && to == HARD_FRAME_POINTER_REGNUM)
    {
      /* 2 is for the saved frame.
         1 is for the 'sts' correction when creating the frame.  */
      return get_frame_size () + 2 + m68hc11_sp_correction + size;
    }

  if (from == FRAME_POINTER_REGNUM && to == HARD_FRAME_POINTER_REGNUM)
    {
      return m68hc11_sp_correction;
    }

  /* Push any 2 byte pseudo hard registers that we need to save.  */
  for (regno = SOFT_REG_FIRST; regno < SOFT_REG_LAST; regno++)
    {
      if (regs_ever_live[regno] && !call_used_regs[regno])
	{
	  size += 2;
	}
    }

  if (from == ARG_POINTER_REGNUM && to == HARD_SP_REGNUM)
    {
      return get_frame_size () + size;
    }

  if (from == FRAME_POINTER_REGNUM && to == HARD_SP_REGNUM)
    {
      return size;
    }
  return 0;
}

/* Initialize a variable CUM of type CUMULATIVE_ARGS
   for a call to a function whose data type is FNTYPE.
   For a library call, FNTYPE is 0.  */

void
m68hc11_init_cumulative_args (CUMULATIVE_ARGS *cum, tree fntype, rtx libname)
{
  tree ret_type;

  z_replacement_completed = 0;
  cum->words = 0;
  cum->nregs = 0;

  /* For a library call, we must find out the type of the return value.
     When the return value is bigger than 4 bytes, it is returned in
     memory.  In that case, the first argument of the library call is a
     pointer to the memory location.  Because the first argument is passed in
     register D, we have to identify this, so that the first function
     parameter is not passed in D either.  */
  if (fntype == 0)
    {
      const char *name;
      size_t len;

      if (libname == 0 || GET_CODE (libname) != SYMBOL_REF)
	return;

      /* If the library ends in 'di' or in 'df', we assume it's
         returning some DImode or some DFmode which are 64-bit wide.  */
      name = XSTR (libname, 0);
      len = strlen (name);
      if (len > 3
	  && ((name[len - 2] == 'd'
	       && (name[len - 1] == 'f' || name[len - 1] == 'i'))
	      || (name[len - 3] == 'd'
		  && (name[len - 2] == 'i' || name[len - 2] == 'f'))))
	{
	  /* We are in.  Mark the first parameter register as already used.  */
	  cum->words = 1;
	  cum->nregs = 1;
	}
      return;
    }

  ret_type = TREE_TYPE (fntype);

  if (ret_type && aggregate_value_p (ret_type, fntype))
    {
      cum->words = 1;
      cum->nregs = 1;
    }
}

/* Update the data in CUM to advance over an argument
   of mode MODE and data type TYPE.
   (TYPE is null for libcalls where that information may not be available.)  */

void
m68hc11_function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
                              tree type, int named ATTRIBUTE_UNUSED)
{
  if (mode != BLKmode)
    {
      if (cum->words == 0 && GET_MODE_SIZE (mode) == 4)
	{
	  cum->nregs = 2;
	  cum->words = GET_MODE_SIZE (mode);
	}
      else
	{
	  cum->words += GET_MODE_SIZE (mode);
	  if (cum->words <= HARD_REG_SIZE)
	    cum->nregs = 1;
	}
    }
  else
    {
      cum->words += int_size_in_bytes (type);
    }
  return;
}

/* Define where to put the arguments to a function.
   Value is zero to push the argument on the stack,
   or a hard register in which to store the argument.

   MODE is the argument's machine mode.
   TYPE is the data type of the argument (as a tree).
    This is null for libcalls where that information may
    not be available.
   CUM is a variable of type CUMULATIVE_ARGS which gives info about
    the preceding args and about the function being called.
   NAMED is nonzero if this argument is a named parameter
    (otherwise it is an extra parameter matching an ellipsis).  */

struct rtx_def *
m68hc11_function_arg (const CUMULATIVE_ARGS *cum, enum machine_mode mode,
                      tree type ATTRIBUTE_UNUSED, int named ATTRIBUTE_UNUSED)
{
  if (cum->words != 0)
    {
      return NULL_RTX;
    }

  if (mode != BLKmode)
    {
      if (GET_MODE_SIZE (mode) == 2 * HARD_REG_SIZE)
	return gen_rtx_REG (mode, HARD_X_REGNUM);

      if (GET_MODE_SIZE (mode) > HARD_REG_SIZE)
	{
	  return NULL_RTX;
	}
      return gen_rtx_REG (mode, HARD_D_REGNUM);
    }
  return NULL_RTX;
}

/* If defined, a C expression which determines whether, and in which direction,
   to pad out an argument with extra space.  The value should be of type
   `enum direction': either `upward' to pad above the argument,
   `downward' to pad below, or `none' to inhibit padding.

   Structures are stored left shifted in their argument slot.  */
int
m68hc11_function_arg_padding (enum machine_mode mode, tree type)
{
  if (type != 0 && AGGREGATE_TYPE_P (type))
    return upward;

  /* Fall back to the default.  */
  return DEFAULT_FUNCTION_ARG_PADDING (mode, type);
}


/* Function prologue and epilogue.  */

/* Emit a move after the reload pass has completed.  This is used to
   emit the prologue and epilogue.  */
static void
emit_move_after_reload (rtx to, rtx from, rtx scratch)
{
  rtx insn;

  if (TARGET_M6812 || H_REG_P (to) || H_REG_P (from))
    {
      insn = emit_move_insn (to, from);
    }
  else
    {
      emit_move_insn (scratch, from);
      insn = emit_move_insn (to, scratch);
    }

  /* Put a REG_INC note to tell the flow analysis that the instruction
     is necessary.  */
  if (IS_STACK_PUSH (to))
    {
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_INC,
					    XEXP (XEXP (to, 0), 0),
					    REG_NOTES (insn));
    }
  else if (IS_STACK_POP (from))
    {
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_INC,
					    XEXP (XEXP (from, 0), 0),
					    REG_NOTES (insn));
    }

  /* For 68HC11, put a REG_INC note on `sts _.frame' to prevent the cse-reg
     to think that sp == _.frame and later replace a x = sp with x = _.frame.
     The problem is that we are lying to gcc and use `txs' for x = sp
     (which is not really true because txs is really x = sp + 1).  */
  else if (TARGET_M6811 && SP_REG_P (from))
    {
      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_INC,
					    from,
					    REG_NOTES (insn));
    }
}

int
m68hc11_total_frame_size (void)
{
  int size;
  int regno;

  size = get_frame_size ();
  if (current_function_interrupt)
    {
      size += 3 * HARD_REG_SIZE;
    }
  if (frame_pointer_needed)
    size += HARD_REG_SIZE;

  for (regno = SOFT_REG_FIRST; regno <= SOFT_REG_LAST; regno++)
    if (regs_ever_live[regno] && !call_used_regs[regno])
      size += HARD_REG_SIZE;

  return size;
}

static void
m68hc11_output_function_epilogue (FILE *out ATTRIBUTE_UNUSED,
                                  HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  /* We catch the function epilogue generation to have a chance
     to clear the z_replacement_completed flag.  */
  z_replacement_completed = 0;
}

void
expand_prologue (void)
{
  tree func_attr;
  int size;
  int regno;
  rtx scratch;

  gcc_assert (reload_completed == 1);

  size = get_frame_size ();

  create_regs_rtx ();

  /* Generate specific prologue for interrupt handlers.  */
  func_attr = TYPE_ATTRIBUTES (TREE_TYPE (current_function_decl));
  current_function_interrupt = lookup_attribute ("interrupt",
						 func_attr) != NULL_TREE;
  current_function_trap = lookup_attribute ("trap", func_attr) != NULL_TREE;
  if (lookup_attribute ("far", func_attr) != NULL_TREE)
    current_function_far = 1;
  else if (lookup_attribute ("near", func_attr) != NULL_TREE)
    current_function_far = 0;
  else
    current_function_far = (TARGET_LONG_CALLS != 0
                            && !current_function_interrupt
                            && !current_function_trap);

  /* Get the scratch register to build the frame and push registers.
     If the first argument is a 32-bit quantity, the D+X registers
     are used.  Use Y to compute the frame.  Otherwise, X is cheaper.
     For 68HC12, this scratch register is not used.  */
  if (current_function_args_info.nregs == 2)
    scratch = iy_reg;
  else
    scratch = ix_reg;

  /* Save current stack frame.  */
  if (frame_pointer_needed)
    emit_move_after_reload (stack_push_word, hard_frame_pointer_rtx, scratch);

  /* For an interrupt handler, we must preserve _.tmp, _.z and _.xy.
     Other soft registers in page0 need not to be saved because they
     will be restored by C functions.  For a trap handler, we don't
     need to preserve these registers because this is a synchronous call.  */
  if (current_function_interrupt)
    {
      emit_move_after_reload (stack_push_word, m68hc11_soft_tmp_reg, scratch);
      emit_move_after_reload (stack_push_word,
			      gen_rtx_REG (HImode, SOFT_Z_REGNUM), scratch);
      emit_move_after_reload (stack_push_word,
			      gen_rtx_REG (HImode, SOFT_SAVED_XY_REGNUM),
			      scratch);
    }

  /* Allocate local variables.  */
  if (TARGET_M6812 && (size > 4 || size == 3))
    {
      emit_insn (gen_addhi3 (stack_pointer_rtx,
			     stack_pointer_rtx, GEN_INT (-size)));
    }
  else if ((!optimize_size && size > 8) || (optimize_size && size > 10))
    {
      rtx insn;

      insn = gen_rtx_PARALLEL
	(VOIDmode,
	 gen_rtvec (2,
		    gen_rtx_SET (VOIDmode,
				 stack_pointer_rtx,
				 gen_rtx_PLUS (HImode,
					       stack_pointer_rtx,
					       GEN_INT (-size))),
		    gen_rtx_CLOBBER (VOIDmode, scratch)));
      emit_insn (insn);
    }
  else
    {
      int i;

      /* Allocate by pushing scratch values.  */
      for (i = 2; i <= size; i += 2)
	emit_move_after_reload (stack_push_word, ix_reg, 0);

      if (size & 1)
	emit_insn (gen_addhi3 (stack_pointer_rtx,
			       stack_pointer_rtx, constm1_rtx));
    }

  /* Create the frame pointer.  */
  if (frame_pointer_needed)
    emit_move_after_reload (hard_frame_pointer_rtx,
			    stack_pointer_rtx, scratch);

  /* Push any 2 byte pseudo hard registers that we need to save.  */
  for (regno = SOFT_REG_FIRST; regno <= SOFT_REG_LAST; regno++)
    {
      if (regs_ever_live[regno] && !call_used_regs[regno])
	{
	  emit_move_after_reload (stack_push_word,
				  gen_rtx_REG (HImode, regno), scratch);
	}
    }
}

void
expand_epilogue (void)
{
  int size;
  register int regno;
  int return_size;
  rtx scratch;

  gcc_assert (reload_completed == 1);

  size = get_frame_size ();

  /* If we are returning a value in two registers, we have to preserve the
     X register and use the Y register to restore the stack and the saved
     registers.  Otherwise, use X because it's faster (and smaller).  */
  if (current_function_return_rtx == 0)
    return_size = 0;
  else if (GET_CODE (current_function_return_rtx) == MEM)
    return_size = HARD_REG_SIZE;
  else
    return_size = GET_MODE_SIZE (GET_MODE (current_function_return_rtx));

  if (return_size > HARD_REG_SIZE && return_size <= 2 * HARD_REG_SIZE)
    scratch = iy_reg;
  else
    scratch = ix_reg;

  /* Pop any 2 byte pseudo hard registers that we saved.  */
  for (regno = SOFT_REG_LAST; regno >= SOFT_REG_FIRST; regno--)
    {
      if (regs_ever_live[regno] && !call_used_regs[regno])
	{
	  emit_move_after_reload (gen_rtx_REG (HImode, regno),
				  stack_pop_word, scratch);
	}
    }

  /* de-allocate auto variables */
  if (TARGET_M6812 && (size > 4 || size == 3))
    {
      emit_insn (gen_addhi3 (stack_pointer_rtx,
			     stack_pointer_rtx, GEN_INT (size)));
    }
  else if ((!optimize_size && size > 8) || (optimize_size && size > 10))
    {
      rtx insn;

      insn = gen_rtx_PARALLEL
	(VOIDmode,
	 gen_rtvec (2,
		    gen_rtx_SET (VOIDmode,
				 stack_pointer_rtx,
				 gen_rtx_PLUS (HImode,
					       stack_pointer_rtx,
					       GEN_INT (size))),
		    gen_rtx_CLOBBER (VOIDmode, scratch)));
      emit_insn (insn);
    }
  else
    {
      int i;

      for (i = 2; i <= size; i += 2)
	emit_move_after_reload (scratch, stack_pop_word, scratch);
      if (size & 1)
	emit_insn (gen_addhi3 (stack_pointer_rtx,
			       stack_pointer_rtx, const1_rtx));
    }

  /* For an interrupt handler, restore ZTMP, ZREG and XYREG.  */
  if (current_function_interrupt)
    {
      emit_move_after_reload (gen_rtx_REG (HImode, SOFT_SAVED_XY_REGNUM),
			      stack_pop_word, scratch);
      emit_move_after_reload (gen_rtx_REG (HImode, SOFT_Z_REGNUM),
			      stack_pop_word, scratch);
      emit_move_after_reload (m68hc11_soft_tmp_reg, stack_pop_word, scratch);
    }

  /* Restore previous frame pointer.  */
  if (frame_pointer_needed)
    emit_move_after_reload (hard_frame_pointer_rtx, stack_pop_word, scratch);

  /* If the trap handler returns some value, copy the value
     in D, X onto the stack so that the rti will pop the return value
     correctly.  */
  else if (current_function_trap && return_size != 0)
    {
      rtx addr_reg = stack_pointer_rtx;

      if (!TARGET_M6812)
	{
	  emit_move_after_reload (scratch, stack_pointer_rtx, 0);
	  addr_reg = scratch;
	}
      emit_move_after_reload (gen_rtx_MEM (HImode,
				       gen_rtx_PLUS (HImode, addr_reg,
						const1_rtx)), d_reg, 0);
      if (return_size > HARD_REG_SIZE)
	emit_move_after_reload (gen_rtx_MEM (HImode,
					 gen_rtx_PLUS (HImode, addr_reg,
						  GEN_INT (3))), ix_reg, 0);
    }

  emit_jump_insn (gen_return ());
}


/* Low and High part extraction for 68HC11.  These routines are
   similar to gen_lowpart and gen_highpart but they have been
   fixed to work for constants and 68HC11 specific registers.  */

rtx
m68hc11_gen_lowpart (enum machine_mode mode, rtx x)
{
  /* We assume that the low part of an auto-inc mode is the same with
     the mode changed and that the caller split the larger mode in the
     correct order.  */
  if (GET_CODE (x) == MEM && m68hc11_auto_inc_p (XEXP (x, 0)))
    {
      return gen_rtx_MEM (mode, XEXP (x, 0));
    }

  /* Note that a CONST_DOUBLE rtx could represent either an integer or a
     floating-point constant.  A CONST_DOUBLE is used whenever the
     constant requires more than one word in order to be adequately
     represented.  */
  if (GET_CODE (x) == CONST_DOUBLE)
    {
      long l[2];

      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
	{
	  REAL_VALUE_TYPE r;

	  if (GET_MODE (x) == SFmode)
	    {
	      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	      REAL_VALUE_TO_TARGET_SINGLE (r, l[0]);
	    }
	  else
	    {
	      rtx first, second;

	      split_double (x, &first, &second);
	      return second;
	    }
	  if (mode == SImode)
	    return GEN_INT (l[0]);

	  return gen_int_mode (l[0], HImode);
	}
      else
	{
	  l[0] = CONST_DOUBLE_LOW (x);
	}
      switch (mode)
	{
	case SImode:
	  return GEN_INT (l[0]);
	case HImode:
	  gcc_assert (GET_MODE (x) == SFmode);
	  return gen_int_mode (l[0], HImode);
	default:
	  gcc_unreachable ();
	}
    }

  if (mode == QImode && D_REG_P (x))
    return gen_rtx_REG (mode, HARD_B_REGNUM);

  /* gen_lowpart crashes when it is called with a SUBREG.  */
  if (GET_CODE (x) == SUBREG && SUBREG_BYTE (x) != 0)
    {
      switch (mode)
	{
	case SImode:
	  return gen_rtx_SUBREG (mode, SUBREG_REG (x), SUBREG_BYTE (x) + 4);
	case HImode:
	  return gen_rtx_SUBREG (mode, SUBREG_REG (x), SUBREG_BYTE (x) + 2);
	default:
	  gcc_unreachable ();
	}
    }
  x = gen_lowpart (mode, x);

  /* Return a different rtx to avoid to share it in several insns
     (when used by a split pattern).  Sharing addresses within
     a MEM breaks the Z register replacement (and reloading).  */
  if (GET_CODE (x) == MEM)
    x = copy_rtx (x);
  return x;
}

rtx
m68hc11_gen_highpart (enum machine_mode mode, rtx x)
{
  /* We assume that the high part of an auto-inc mode is the same with
     the mode changed and that the caller split the larger mode in the
     correct order.  */
  if (GET_CODE (x) == MEM && m68hc11_auto_inc_p (XEXP (x, 0)))
    {
      return gen_rtx_MEM (mode, XEXP (x, 0));
    }

  /* Note that a CONST_DOUBLE rtx could represent either an integer or a
     floating-point constant.  A CONST_DOUBLE is used whenever the
     constant requires more than one word in order to be adequately
     represented.  */
  if (GET_CODE (x) == CONST_DOUBLE)
    {
      long l[2];

      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
	{
	  REAL_VALUE_TYPE r;

	  if (GET_MODE (x) == SFmode)
	    {
	      REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	      REAL_VALUE_TO_TARGET_SINGLE (r, l[1]);
	    }
	  else
	    {
	      rtx first, second;

	      split_double (x, &first, &second);
	      return first;
	    }
	  if (mode == SImode)
	    return GEN_INT (l[1]);

	  return gen_int_mode ((l[1] >> 16), HImode);
	}
      else
	{
	  l[1] = CONST_DOUBLE_HIGH (x);
	}

      switch (mode)
	{
	case SImode:
	  return GEN_INT (l[1]);
	case HImode:
	  gcc_assert (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT);
	  return gen_int_mode ((l[0] >> 16), HImode);
	default:
	  gcc_unreachable ();
	}
    }
  if (GET_CODE (x) == CONST_INT)
    {
      HOST_WIDE_INT val = INTVAL (x);

      if (mode == QImode)
	{
	  return gen_int_mode (val >> 8, QImode);
	}
      else if (mode == HImode)
	{
	  return gen_int_mode (val >> 16, HImode);
	}
      else if (mode == SImode)
       {
         return gen_int_mode (val >> 32, SImode);
       }
    }
  if (mode == QImode && D_REG_P (x))
    return gen_rtx_REG (mode, HARD_A_REGNUM);

  /* There is no way in GCC to represent the upper part of a word register.
     To obtain the 8-bit upper part of a soft register, we change the
     reg into a mem rtx.  This is possible because they are physically
     located in memory.  There is no offset because we are big-endian.  */
  if (mode == QImode && S_REG_P (x))
    {
      int pos;

      /* Avoid the '*' for direct addressing mode when this
         addressing mode is disabled.  */
      pos = TARGET_NO_DIRECT_MODE ? 1 : 0;
      return gen_rtx_MEM (QImode,
		      gen_rtx_SYMBOL_REF (Pmode,
			       &reg_names[REGNO (x)][pos]));
    }

  /* gen_highpart crashes when it is called with a SUBREG.  */
  switch (GET_CODE (x))
    {
    case SUBREG:
      return gen_rtx_SUBREG (mode, XEXP (x, 0), XEXP (x, 1));
    case REG:
      if (REGNO (x) < FIRST_PSEUDO_REGISTER)
        return gen_rtx_REG (mode, REGNO (x));
      else
        return gen_rtx_SUBREG (mode, x, 0);
    case MEM:
      x = change_address (x, mode, 0);

      /* Return a different rtx to avoid to share it in several insns
	 (when used by a split pattern).  Sharing addresses within
	 a MEM breaks the Z register replacement (and reloading).  */
      if (GET_CODE (x) == MEM)
	x = copy_rtx (x);
      return x;

    default:
      gcc_unreachable ();
    }
}


/* Obscure register manipulation.  */

/* Finds backward in the instructions to see if register 'reg' is
   dead.  This is used when generating code to see if we can use 'reg'
   as a scratch register.  This allows us to choose a better generation
   of code when we know that some register dies or can be clobbered.  */

int
dead_register_here (rtx x, rtx reg)
{
  rtx x_reg;
  rtx p;

  if (D_REG_P (reg))
    x_reg = gen_rtx_REG (SImode, HARD_X_REGNUM);
  else
    x_reg = 0;

  for (p = PREV_INSN (x); p && GET_CODE (p) != CODE_LABEL; p = PREV_INSN (p))
    if (INSN_P (p))
      {
	rtx body;

	body = PATTERN (p);

	if (GET_CODE (body) == CALL_INSN)
	  break;
	if (GET_CODE (body) == JUMP_INSN)
	  break;

	if (GET_CODE (body) == SET)
	  {
	    rtx dst = XEXP (body, 0);

	    if (GET_CODE (dst) == REG && REGNO (dst) == REGNO (reg))
	      break;
	    if (x_reg && rtx_equal_p (dst, x_reg))
	      break;

	    if (find_regno_note (p, REG_DEAD, REGNO (reg)))
	      return 1;
	  }
	else if (reg_mentioned_p (reg, p)
		 || (x_reg && reg_mentioned_p (x_reg, p)))
	  break;
      }

  /* Scan forward to see if the register is set in some insns and never
     used since then.  */
  for (p = x /*NEXT_INSN (x) */ ; p; p = NEXT_INSN (p))
    {
      rtx body;

      if (GET_CODE (p) == CODE_LABEL
	  || GET_CODE (p) == JUMP_INSN
	  || GET_CODE (p) == CALL_INSN || GET_CODE (p) == BARRIER)
	break;

      if (GET_CODE (p) != INSN)
	continue;

      body = PATTERN (p);
      if (GET_CODE (body) == SET)
	{
	  rtx src = XEXP (body, 1);
	  rtx dst = XEXP (body, 0);

	  if (GET_CODE (dst) == REG
	      && REGNO (dst) == REGNO (reg) && !reg_mentioned_p (reg, src))
	    return 1;
	}

      /* Register is used (may be in source or in dest).  */
      if (reg_mentioned_p (reg, p)
	  || (x_reg != 0 && GET_MODE (p) == SImode
	      && reg_mentioned_p (x_reg, p)))
	break;
    }
  return p == 0 ? 1 : 0;
}


/* Code generation operations called from machine description file.  */

/* Print the name of register 'regno' in the assembly file.  */
static void
asm_print_register (FILE *file, int regno)
{
  const char *name = reg_names[regno];

  if (TARGET_NO_DIRECT_MODE && name[0] == '*')
    name++;

  fprintf (file, "%s", name);
}

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand X.  X is an RTL
   expression.

   CODE is a value that can be used to specify one of several ways
   of printing the operand.  It is used when identical operands
   must be printed differently depending on the context.  CODE
   comes from the `%' specification that was used to request
   printing of the operand.  If the specification was just `%DIGIT'
   then CODE is 0; if the specification was `%LTR DIGIT' then CODE
   is the ASCII code for LTR.

   If X is a register, this macro should print the register's name.
   The names can be found in an array `reg_names' whose type is
   `char *[]'.  `reg_names' is initialized from `REGISTER_NAMES'.

   When the machine description has a specification `%PUNCT' (a `%'
   followed by a punctuation character), this macro is called with
   a null pointer for X and the punctuation character for CODE.

   The M68HC11 specific codes are:

   'b' for the low part of the operand.
   'h' for the high part of the operand
       The 'b' or 'h' modifiers have no effect if the operand has
       the QImode and is not a S_REG_P (soft register).  If the
       operand is a hard register, these two modifiers have no effect.
   't' generate the temporary scratch register.  The operand is
       ignored.
   'T' generate the low-part temporary scratch register.  The operand is
       ignored.  */

void
print_operand (FILE *file, rtx op, int letter)
{
  if (letter == 't')
    {
      asm_print_register (file, SOFT_TMP_REGNUM);
      return;
    }
  else if (letter == 'T')
    {
      asm_print_register (file, SOFT_TMP_REGNUM);
      fprintf (file, "+1");
      return;
    }
  else if (letter == '#')
    {
      asm_fprintf (file, "%I");
    }

  if (GET_CODE (op) == REG)
    {
      if (letter == 'b' && S_REG_P (op))
	{
	  asm_print_register (file, REGNO (op));
	  fprintf (file, "+1");
	}
      else if (letter == 'b' && D_REG_P (op))
	{
	  asm_print_register (file, HARD_B_REGNUM);
	}
      else
	{
	  asm_print_register (file, REGNO (op));
	}
      return;
    }

  if (GET_CODE (op) == SYMBOL_REF && (letter == 'b' || letter == 'h'))
    {
      if (letter == 'b')
	asm_fprintf (file, "%I%%lo(");
      else
	asm_fprintf (file, "%I%%hi(");

      output_addr_const (file, op);
      fprintf (file, ")");
      return;
    }

  /* Get the low or high part of the operand when 'b' or 'h' modifiers
     are specified.  If we already have a QImode, there is nothing to do.  */
  if (GET_MODE (op) == HImode || GET_MODE (op) == VOIDmode)
    {
      if (letter == 'b')
	{
	  op = m68hc11_gen_lowpart (QImode, op);
	}
      else if (letter == 'h')
	{
	  op = m68hc11_gen_highpart (QImode, op);
	}
    }

  if (GET_CODE (op) == MEM)
    {
      rtx base = XEXP (op, 0);
      switch (GET_CODE (base))
	{
	case PRE_DEC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,-", GET_MODE_SIZE (GET_MODE (op)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  break;

	case POST_DEC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,", GET_MODE_SIZE (GET_MODE (op)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  fprintf (file, "-");
	  break;

	case POST_INC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,", GET_MODE_SIZE (GET_MODE (op)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  fprintf (file, "+");
	  break;

	case PRE_INC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,+", GET_MODE_SIZE (GET_MODE (op)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  break;

        case MEM:
          gcc_assert (TARGET_M6812);
	  fprintf (file, "[");
	  print_operand_address (file, XEXP (base, 0));
	  fprintf (file, "]");
          break;

	default:
          if (m68hc11_page0_symbol_p (base))
            fprintf (file, "*");

	  output_address (base);
	  break;
	}
    }
  else if (GET_CODE (op) == CONST_DOUBLE && GET_MODE (op) == SFmode)
    {
      REAL_VALUE_TYPE r;
      long l;

      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      REAL_VALUE_TO_TARGET_SINGLE (r, l);
      asm_fprintf (file, "%I0x%lx", l);
    }
  else if (GET_CODE (op) == CONST_DOUBLE && GET_MODE (op) == DFmode)
    {
      char dstr[30];

      real_to_decimal (dstr, CONST_DOUBLE_REAL_VALUE (op),
		       sizeof (dstr), 0, 1);
      asm_fprintf (file, "%I0r%s", dstr);
    }
  else
    {
      int need_parenthesize = 0;

      if (letter != 'i')
	asm_fprintf (file, "%I");
      else
        need_parenthesize = must_parenthesize (op);

      if (need_parenthesize)
        fprintf (file, "(");

      output_addr_const (file, op);
      if (need_parenthesize)
        fprintf (file, ")");
    }
}

/* Returns true if the operand 'op' must be printed with parenthesis
   around it.  This must be done only if there is a symbol whose name
   is a processor register.  */
static int
must_parenthesize (rtx op)
{
  const char *name;

  switch (GET_CODE (op))
    {
    case SYMBOL_REF:
      name = XSTR (op, 0);
      /* Avoid a conflict between symbol name and a possible
         register.  */
      return (strcasecmp (name, "a") == 0
	      || strcasecmp (name, "b") == 0
	      || strcasecmp (name, "d") == 0
	      || strcasecmp (name, "x") == 0
	      || strcasecmp (name, "y") == 0
	      || strcasecmp (name, "ix") == 0
	      || strcasecmp (name, "iy") == 0
	      || strcasecmp (name, "pc") == 0
	      || strcasecmp (name, "sp") == 0
	      || strcasecmp (name, "ccr") == 0) ? 1 : 0;

    case PLUS:
    case MINUS:
      return must_parenthesize (XEXP (op, 0))
	|| must_parenthesize (XEXP (op, 1));

    case MEM:
    case CONST:
    case ZERO_EXTEND:
    case SIGN_EXTEND:
      return must_parenthesize (XEXP (op, 0));

    case CONST_DOUBLE:
    case CONST_INT:
    case LABEL_REF:
    case CODE_LABEL:
    default:
      return 0;
    }
}

/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.  */

void
print_operand_address (FILE *file, rtx addr)
{
  rtx base;
  rtx offset;
  int need_parenthesis = 0;

  switch (GET_CODE (addr))
    {
    case REG:
      gcc_assert (REG_P (addr) && REG_OK_FOR_BASE_STRICT_P (addr));

      fprintf (file, "0,");
      asm_print_register (file, REGNO (addr));
      break;

    case MEM:
      base = XEXP (addr, 0);
      switch (GET_CODE (base))
	{
	case PRE_DEC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,-", GET_MODE_SIZE (GET_MODE (addr)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  break;

	case POST_DEC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,", GET_MODE_SIZE (GET_MODE (addr)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  fprintf (file, "-");
	  break;

	case POST_INC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,", GET_MODE_SIZE (GET_MODE (addr)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  fprintf (file, "+");
	  break;

	case PRE_INC:
	  gcc_assert (TARGET_M6812);
	  fprintf (file, "%u,+", GET_MODE_SIZE (GET_MODE (addr)));
	  asm_print_register (file, REGNO (XEXP (base, 0)));
	  break;

	default:
	  need_parenthesis = must_parenthesize (base);
	  if (need_parenthesis)
	    fprintf (file, "(");

	  output_addr_const (file, base);
	  if (need_parenthesis)
	    fprintf (file, ")");
	  break;
	}
      break;

    case PLUS:
      base = XEXP (addr, 0);
      offset = XEXP (addr, 1);
      if (!G_REG_P (base) && G_REG_P (offset))
	{
	  base = XEXP (addr, 1);
	  offset = XEXP (addr, 0);
	}
      if (CONSTANT_ADDRESS_P (base))
	{
	  need_parenthesis = must_parenthesize (addr);

	  gcc_assert (CONSTANT_ADDRESS_P (offset));
	  if (need_parenthesis)
	    fprintf (file, "(");

	  output_addr_const (file, base);
	  fprintf (file, "+");
	  output_addr_const (file, offset);
	  if (need_parenthesis)
	    fprintf (file, ")");
	}
      else
	{
	  gcc_assert (REG_P (base) && REG_OK_FOR_BASE_STRICT_P (base));
	  if (REG_P (offset))
	    {
	      gcc_assert (TARGET_M6812);
	      asm_print_register (file, REGNO (offset));
	      fprintf (file, ",");
	      asm_print_register (file, REGNO (base));
	    }
	  else
	    {
              need_parenthesis = must_parenthesize (offset);
              if (need_parenthesis)
                fprintf (file, "(");

	      output_addr_const (file, offset);
              if (need_parenthesis)
                fprintf (file, ")");
	      fprintf (file, ",");
	      asm_print_register (file, REGNO (base));
	    }
	}
      break;

    default:
      if (GET_CODE (addr) == CONST_INT
	  && INTVAL (addr) < 0x8000 && INTVAL (addr) >= -0x8000)
	{
	  fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (addr));
	}
      else
	{
	  need_parenthesis = must_parenthesize (addr);
	  if (need_parenthesis)
	    fprintf (file, "(");

	  output_addr_const (file, addr);
	  if (need_parenthesis)
	    fprintf (file, ")");
	}
      break;
    }
}


/* Splitting of some instructions.  */

static rtx
m68hc11_expand_compare (enum rtx_code code, rtx op0, rtx op1)
{
  rtx ret = 0;

  gcc_assert (GET_MODE_CLASS (GET_MODE (op0)) != MODE_FLOAT);
  emit_insn (gen_rtx_SET (VOIDmode, cc0_rtx,
			  gen_rtx_COMPARE (VOIDmode, op0, op1)));
  ret = gen_rtx_fmt_ee (code, VOIDmode, cc0_rtx, const0_rtx);

  return ret;
}

rtx
m68hc11_expand_compare_and_branch (enum rtx_code code, rtx op0, rtx op1,
                                   rtx label)
{
  rtx tmp;

  switch (GET_MODE (op0))
    {
    case QImode:
    case HImode:
      tmp = m68hc11_expand_compare (code, op0, op1);
      tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
				  gen_rtx_LABEL_REF (VOIDmode, label),
				  pc_rtx);
      emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx, tmp));
      return 0;
#if 0

      /* SCz: from i386.c  */
    case SFmode:
    case DFmode:
      /* Don't expand the comparison early, so that we get better code
         when jump or whoever decides to reverse the comparison.  */
      {
	rtvec vec;
	int use_fcomi;

	code = m68hc11_prepare_fp_compare_args (code, &m68hc11_compare_op0,
						&m68hc11_compare_op1);

	tmp = gen_rtx_fmt_ee (code, m68hc11_fp_compare_mode (code),
			      m68hc11_compare_op0, m68hc11_compare_op1);
	tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
				    gen_rtx_LABEL_REF (VOIDmode, label),
				    pc_rtx);
	tmp = gen_rtx_SET (VOIDmode, pc_rtx, tmp);

	use_fcomi = ix86_use_fcomi_compare (code);
	vec = rtvec_alloc (3 + !use_fcomi);
	RTVEC_ELT (vec, 0) = tmp;
	RTVEC_ELT (vec, 1)
	  = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCFPmode, 18));
	RTVEC_ELT (vec, 2)
	  = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCFPmode, 17));
	if (!use_fcomi)
	  RTVEC_ELT (vec, 3)
	    = gen_rtx_CLOBBER (VOIDmode, gen_rtx_SCRATCH (HImode));

	emit_jump_insn (gen_rtx_PARALLEL (VOIDmode, vec));
	return;
      }
#endif

    case SImode:
      /* Expand SImode branch into multiple compare+branch.  */
      {
	rtx lo[2], hi[2], label2;
	enum rtx_code code1, code2, code3;

	if (CONSTANT_P (op0) && !CONSTANT_P (op1))
	  {
	    tmp = op0;
	    op0 = op1;
	    op1 = tmp;
	    code = swap_condition (code);
	  }
	lo[0] = m68hc11_gen_lowpart (HImode, op0);
	lo[1] = m68hc11_gen_lowpart (HImode, op1);
	hi[0] = m68hc11_gen_highpart (HImode, op0);
	hi[1] = m68hc11_gen_highpart (HImode, op1);

	/* Otherwise, if we are doing less-than, op1 is a constant and the
	   low word is zero, then we can just examine the high word.  */

	if (GET_CODE (hi[1]) == CONST_INT && lo[1] == const0_rtx
	    && (code == LT || code == LTU))
	  {
	    return m68hc11_expand_compare_and_branch (code, hi[0], hi[1],
						      label);
	  }

	/* Otherwise, we need two or three jumps.  */

	label2 = gen_label_rtx ();

	code1 = code;
	code2 = swap_condition (code);
	code3 = unsigned_condition (code);

	switch (code)
	  {
	  case LT:
	  case GT:
	  case LTU:
	  case GTU:
	    break;

	  case LE:
	    code1 = LT;
	    code2 = GT;
	    break;
	  case GE:
	    code1 = GT;
	    code2 = LT;
	    break;
	  case LEU:
	    code1 = LTU;
	    code2 = GTU;
	    break;
	  case GEU:
	    code1 = GTU;
	    code2 = LTU;
	    break;

	  case EQ:
	    code1 = UNKNOWN;
	    code2 = NE;
	    break;
	  case NE:
	    code2 = UNKNOWN;
	    break;

	  default:
	    gcc_unreachable ();
	  }

	/*
	 * a < b =>
	 *    if (hi(a) < hi(b)) goto true;
	 *    if (hi(a) > hi(b)) goto false;
	 *    if (lo(a) < lo(b)) goto true;
	 *  false:
	 */
	if (code1 != UNKNOWN)
	  m68hc11_expand_compare_and_branch (code1, hi[0], hi[1], label);
	if (code2 != UNKNOWN)
	  m68hc11_expand_compare_and_branch (code2, hi[0], hi[1], label2);

	m68hc11_expand_compare_and_branch (code3, lo[0], lo[1], label);

	if (code2 != UNKNOWN)
	  emit_label (label2);
	return 0;
      }

    default:
      gcc_unreachable ();
    }
  return 0;
}

/* Return the increment/decrement mode of a MEM if it is such.
   Return CONST if it is anything else.  */
static int
autoinc_mode (rtx x)
{
  if (GET_CODE (x) != MEM)
    return CONST;

  x = XEXP (x, 0);
  if (GET_CODE (x) == PRE_INC
      || GET_CODE (x) == PRE_DEC
      || GET_CODE (x) == POST_INC
      || GET_CODE (x) == POST_DEC)
    return GET_CODE (x);

  return CONST;
}

static int
m68hc11_make_autoinc_notes (rtx *x, void *data)
{
  rtx insn;
  
  switch (GET_CODE (*x))
    {
    case PRE_DEC:
    case PRE_INC:
    case POST_DEC:
    case POST_INC:
      insn = (rtx) data;
      REG_NOTES (insn) = alloc_EXPR_LIST (REG_INC, XEXP (*x, 0),
                                          REG_NOTES (insn));
      return -1;

    default:
      return 0;
    }
}

/* Split a DI, SI or HI move into several smaller move operations.
   The scratch register 'scratch' is used as a temporary to load
   store intermediate values.  It must be a hard register.  */
void
m68hc11_split_move (rtx to, rtx from, rtx scratch)
{
  rtx low_to, low_from;
  rtx high_to, high_from;
  rtx insn;
  enum machine_mode mode;
  int offset = 0;
  int autoinc_from = autoinc_mode (from);
  int autoinc_to = autoinc_mode (to);

  mode = GET_MODE (to);

  /* If the TO and FROM contain autoinc modes that are not compatible
     together (one pop and the other a push), we must change one to
     an offsetable operand and generate an appropriate add at the end.  */
  if (TARGET_M6812 && GET_MODE_SIZE (mode) > 2)
    {
      rtx reg;
      int code;

      /* The source uses an autoinc mode which is not compatible with
         a split (this would result in a word swap).  */
      if (autoinc_from == PRE_INC || autoinc_from == POST_DEC)
        {
          code = GET_CODE (XEXP (from, 0));
          reg = XEXP (XEXP (from, 0), 0);
          offset = GET_MODE_SIZE (GET_MODE (from));
          if (code == POST_DEC)
            offset = -offset;

          if (code == PRE_INC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));

          m68hc11_split_move (to, gen_rtx_MEM (GET_MODE (from), reg), scratch);
          if (code == POST_DEC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));
          return;
        }

      /* Likewise for destination.  */
      if (autoinc_to == PRE_INC || autoinc_to == POST_DEC)
        {
          code = GET_CODE (XEXP (to, 0));
          reg = XEXP (XEXP (to, 0), 0);
          offset = GET_MODE_SIZE (GET_MODE (to));
          if (code == POST_DEC)
            offset = -offset;

          if (code == PRE_INC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));

          m68hc11_split_move (gen_rtx_MEM (GET_MODE (to), reg), from, scratch);
          if (code == POST_DEC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));
          return;
        }

      /* The source and destination auto increment modes must be compatible
         with each other: same direction.  */
      if ((autoinc_to != autoinc_from
           && autoinc_to != CONST && autoinc_from != CONST)
          /* The destination address register must not be used within
             the source operand because the source address would change
             while doing the copy.  */
          || (autoinc_to != CONST
              && reg_mentioned_p (XEXP (XEXP (to, 0), 0), from)
              && !IS_STACK_PUSH (to)))
        {
          /* Must change the destination.  */
          code = GET_CODE (XEXP (to, 0));
          reg = XEXP (XEXP (to, 0), 0);
          offset = GET_MODE_SIZE (GET_MODE (to));
          if (code == PRE_DEC || code == POST_DEC)
            offset = -offset;

          if (code == PRE_DEC || code == PRE_INC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));
          m68hc11_split_move (gen_rtx_MEM (GET_MODE (to), reg), from, scratch);
          if (code == POST_DEC || code == POST_INC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));

          return;
        }

      /* Likewise, the source address register must not be used within
         the destination operand.  */
      if (autoinc_from != CONST
          && reg_mentioned_p (XEXP (XEXP (from, 0), 0), to)
          && !IS_STACK_PUSH (to))
        {
          /* Must change the source.  */
          code = GET_CODE (XEXP (from, 0));
          reg = XEXP (XEXP (from, 0), 0);
          offset = GET_MODE_SIZE (GET_MODE (from));
          if (code == PRE_DEC || code == POST_DEC)
            offset = -offset;

          if (code == PRE_DEC || code == PRE_INC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));
          m68hc11_split_move (to, gen_rtx_MEM (GET_MODE (from), reg), scratch);
          if (code == POST_DEC || code == POST_INC)
            emit_insn (gen_addhi3 (reg, reg, GEN_INT (offset)));

          return;
        }
    }

  if (GET_MODE_SIZE (mode) == 8)
    mode = SImode;
  else if (GET_MODE_SIZE (mode) == 4)
    mode = HImode;
  else
    mode = QImode;

  if (TARGET_M6812
      && IS_STACK_PUSH (to)
      && reg_mentioned_p (gen_rtx_REG (HImode, HARD_SP_REGNUM), from))
    {
      if (mode == SImode)
        {
          offset = 4;
        }
      else if (mode == HImode)
        {
          offset = 2;
        }
      else
        offset = 0;
    }

  low_to = m68hc11_gen_lowpart (mode, to);
  high_to = m68hc11_gen_highpart (mode, to);

  low_from = m68hc11_gen_lowpart (mode, from);
  high_from = m68hc11_gen_highpart (mode, from);

  if (offset)
    {
      high_from = adjust_address (high_from, mode, offset);
      low_from = high_from;
    }

  /* When copying with a POST_INC mode, we must copy the
     high part and then the low part to guarantee a correct
     32/64-bit copy.  */
  if (TARGET_M6812
      && GET_MODE_SIZE (mode) >= 2
      && autoinc_from != autoinc_to
      && (autoinc_from == POST_INC || autoinc_to == POST_INC))
    {
      rtx swap;

      swap = low_to;
      low_to = high_to;
      high_to = swap;

      swap = low_from;
      low_from = high_from;
      high_from = swap;
    }
  if (mode == SImode)
    {
      m68hc11_split_move (low_to, low_from, scratch);
      m68hc11_split_move (high_to, high_from, scratch);
    }
  else if (H_REG_P (to) || H_REG_P (from)
	   || (low_from == const0_rtx
	       && high_from == const0_rtx
	       && ! push_operand (to, GET_MODE (to))
	       && ! H_REG_P (scratch))
	   || (TARGET_M6812
	       && (!m68hc11_register_indirect_p (from, GET_MODE (from))
		   || m68hc11_small_indexed_indirect_p (from,
							GET_MODE (from)))
	       && (!m68hc11_register_indirect_p (to, GET_MODE (to))
		   || m68hc11_small_indexed_indirect_p (to, GET_MODE (to)))))
    {
      insn = emit_move_insn (low_to, low_from);
      for_each_rtx (&PATTERN (insn), m68hc11_make_autoinc_notes, insn);

      insn = emit_move_insn (high_to, high_from);
      for_each_rtx (&PATTERN (insn), m68hc11_make_autoinc_notes, insn);
    }
  else
    {
      insn = emit_move_insn (scratch, low_from);
      for_each_rtx (&PATTERN (insn), m68hc11_make_autoinc_notes, insn);
      insn = emit_move_insn (low_to, scratch);
      for_each_rtx (&PATTERN (insn), m68hc11_make_autoinc_notes, insn);

      insn = emit_move_insn (scratch, high_from);
      for_each_rtx (&PATTERN (insn), m68hc11_make_autoinc_notes, insn);
      insn = emit_move_insn (high_to, scratch);
      for_each_rtx (&PATTERN (insn), m68hc11_make_autoinc_notes, insn);
    }
}

static rtx
simplify_logical (enum machine_mode mode, int code, rtx operand, rtx *result)
{
  int val;
  int mask;

  *result = 0;
  if (GET_CODE (operand) != CONST_INT)
    return operand;

  if (mode == HImode)
    mask = 0x0ffff;
  else
    mask = 0x0ff;

  val = INTVAL (operand);
  switch (code)
    {
    case IOR:
      if ((val & mask) == 0)
	return 0;
      if ((val & mask) == mask)
	*result = constm1_rtx;
      break;

    case AND:
      if ((val & mask) == 0)
	*result = const0_rtx;
      if ((val & mask) == mask)
	return 0;
      break;

    case XOR:
      if ((val & mask) == 0)
	return 0;
      break;
    }
  return operand;
}

static void
m68hc11_emit_logical (enum machine_mode mode, int code, rtx *operands)
{
  rtx result;
  int need_copy;

  need_copy = (rtx_equal_p (operands[0], operands[1])
	       || rtx_equal_p (operands[0], operands[2])) ? 0 : 1;

  operands[1] = simplify_logical (mode, code, operands[1], &result);
  operands[2] = simplify_logical (mode, code, operands[2], &result);

  if (result && GET_CODE (result) == CONST_INT)
    {
      if (!H_REG_P (operands[0]) && operands[3]
	  && (INTVAL (result) != 0 || IS_STACK_PUSH (operands[0])))
	{
	  emit_move_insn (operands[3], result);
	  emit_move_insn (operands[0], operands[3]);
	}
      else
	{
	  emit_move_insn (operands[0], result);
	}
    }
  else if (operands[1] != 0 && operands[2] != 0)
    {
      rtx insn;

      if (!H_REG_P (operands[0]) && operands[3])
	{
	  emit_move_insn (operands[3], operands[1]);
	  emit_insn (gen_rtx_SET (mode,
				  operands[3],
				  gen_rtx_fmt_ee (code, mode,
						  operands[3], operands[2])));
	  insn = emit_move_insn (operands[0], operands[3]);
	}
      else
	{
	  insn = emit_insn (gen_rtx_SET (mode,
					 operands[0],
					 gen_rtx_fmt_ee (code, mode,
							 operands[0],
							 operands[2])));
	}
    }

  /* The logical operation is similar to a copy.  */
  else if (need_copy)
    {
      rtx src;

      if (GET_CODE (operands[1]) == CONST_INT)
	src = operands[2];
      else
	src = operands[1];

      if (!H_REG_P (operands[0]) && !H_REG_P (src))
	{
	  emit_move_insn (operands[3], src);
	  emit_move_insn (operands[0], operands[3]);
	}
      else
	{
	  emit_move_insn (operands[0], src);
	}
    }
}

void
m68hc11_split_logical (enum machine_mode mode, int code, rtx *operands)
{
  rtx low[4];
  rtx high[4];

  low[0] = m68hc11_gen_lowpart (mode, operands[0]);
  low[1] = m68hc11_gen_lowpart (mode, operands[1]);
  low[2] = m68hc11_gen_lowpart (mode, operands[2]);

  high[0] = m68hc11_gen_highpart (mode, operands[0]);
  high[1] = m68hc11_gen_highpart (mode, operands[1]);
  high[2] = m68hc11_gen_highpart (mode, operands[2]);

  low[3] = operands[3];
  high[3] = operands[3];
  if (mode == SImode)
    {
      m68hc11_split_logical (HImode, code, low);
      m68hc11_split_logical (HImode, code, high);
      return;
    }

  m68hc11_emit_logical (mode, code, low);
  m68hc11_emit_logical (mode, code, high);
}


/* Code generation.  */

void
m68hc11_output_swap (rtx insn ATTRIBUTE_UNUSED, rtx operands[])
{
  /* We have to be careful with the cc_status.  An address register swap
     is generated for some comparison.  The comparison is made with D
     but the branch really uses the address register.  See the split
     pattern for compare.  The xgdx/xgdy preserve the flags but after
     the exchange, the flags will reflect to the value of X and not D.
     Tell this by setting the cc_status according to the cc_prev_status.  */
  if (X_REG_P (operands[1]) || X_REG_P (operands[0]))
    {
      if (cc_prev_status.value1 != 0
	  && (D_REG_P (cc_prev_status.value1)
	      || X_REG_P (cc_prev_status.value1)))
	{
	  cc_status = cc_prev_status;
	  if (D_REG_P (cc_status.value1))
	    cc_status.value1 = gen_rtx_REG (GET_MODE (cc_status.value1),
					HARD_X_REGNUM);
	  else
	    cc_status.value1 = gen_rtx_REG (GET_MODE (cc_status.value1),
					HARD_D_REGNUM);
	}
      else
	CC_STATUS_INIT;

      output_asm_insn ("xgdx", operands);
    }
  else
    {
      if (cc_prev_status.value1 != 0
	  && (D_REG_P (cc_prev_status.value1)
	      || Y_REG_P (cc_prev_status.value1)))
	{
	  cc_status = cc_prev_status;
	  if (D_REG_P (cc_status.value1))
	    cc_status.value1 = gen_rtx_REG (GET_MODE (cc_status.value1),
					HARD_Y_REGNUM);
	  else
	    cc_status.value1 = gen_rtx_REG (GET_MODE (cc_status.value1),
					HARD_D_REGNUM);
	}
      else
	CC_STATUS_INIT;

      output_asm_insn ("xgdy", operands);
    }
}

/* Returns 1 if the next insn after 'insn' is a test of the register 'reg'.
   This is used to decide whether a move that set flags should be used
   instead.  */
int
next_insn_test_reg (rtx insn, rtx reg)
{
  rtx body;

  insn = next_nonnote_insn (insn);
  if (GET_CODE (insn) != INSN)
    return 0;

  body = PATTERN (insn);
  if (sets_cc0_p (body) != 1)
    return 0;

  if (rtx_equal_p (XEXP (body, 1), reg) == 0)
    return 0;

  return 1;
}

/* Generate the code to move a 16-bit operand into another one.  */

void
m68hc11_gen_movhi (rtx insn, rtx *operands)
{
  int reg;

  /* Move a register or memory to the same location.
     This is possible because such insn can appear
     in a non-optimizing mode.  */
  if (operands[0] == operands[1] || rtx_equal_p (operands[0], operands[1]))
    {
      cc_status = cc_prev_status;
      return;
    }

  if (TARGET_M6812)
    {
      rtx from = operands[1];
      rtx to = operands[0];

      if (IS_STACK_PUSH (to) && H_REG_P (from))
	{
          cc_status = cc_prev_status;
	  switch (REGNO (from))
	    {
	    case HARD_X_REGNUM:
	    case HARD_Y_REGNUM:
	    case HARD_D_REGNUM:
	      output_asm_insn ("psh%1", operands);
	      break;
            case HARD_SP_REGNUM:
              output_asm_insn ("sts\t2,-sp", operands);
              break;
	    default:
	      gcc_unreachable ();
	    }
	  return;
	}
      if (IS_STACK_POP (from) && H_REG_P (to))
	{
          cc_status = cc_prev_status;
	  switch (REGNO (to))
	    {
	    case HARD_X_REGNUM:
	    case HARD_Y_REGNUM:
	    case HARD_D_REGNUM:
	      output_asm_insn ("pul%0", operands);
	      break;
	    default:
	      gcc_unreachable ();
	    }
	  return;
	}
      if (H_REG_P (operands[0]) && H_REG_P (operands[1]))
	{
          m68hc11_notice_keep_cc (operands[0]);
	  output_asm_insn ("tfr\t%1,%0", operands);
	}
      else if (H_REG_P (operands[0]))
	{
	  if (SP_REG_P (operands[0]))
	    output_asm_insn ("lds\t%1", operands);
	  else
	    output_asm_insn ("ld%0\t%1", operands);
	}
      else if (H_REG_P (operands[1]))
	{
	  if (SP_REG_P (operands[1]))
	    output_asm_insn ("sts\t%0", operands);
	  else
	    output_asm_insn ("st%1\t%0", operands);
	}

      /* The 68hc12 does not support (MEM:HI (MEM:HI)) with the movw
         instruction.  We have to use a scratch register as temporary location.
         Trying to use a specific pattern or constrain failed.  */
      else if (GET_CODE (to) == MEM && GET_CODE (XEXP (to, 0)) == MEM)
        {
          rtx ops[4];

          ops[0] = to;
          ops[2] = from;
          ops[3] = 0;
          if (dead_register_here (insn, d_reg))
            ops[1] = d_reg;
          else if (dead_register_here (insn, ix_reg))
            ops[1] = ix_reg;
          else if (dead_register_here (insn, iy_reg))
            ops[1] = iy_reg;
          else
            {
              ops[1] = d_reg;
              ops[3] = d_reg;
              output_asm_insn ("psh%3", ops);
            }

          ops[0] = to;
          ops[2] = from;
          output_asm_insn ("ld%1\t%2", ops);
          output_asm_insn ("st%1\t%0", ops);
          if (ops[3])
            output_asm_insn ("pul%3", ops);
        }

      /* Use movw for non-null constants or when we are clearing
         a volatile memory reference.  However, this is possible
         only if the memory reference has a small offset or is an
         absolute address.  */
      else if (GET_CODE (from) == CONST_INT
               && INTVAL (from) == 0
               && (MEM_VOLATILE_P (to) == 0
                   || m68hc11_small_indexed_indirect_p (to, HImode) == 0))
        {
          output_asm_insn ("clr\t%h0", operands);
          output_asm_insn ("clr\t%b0", operands);
        }
      else
	{
	  if ((m68hc11_register_indirect_p (from, GET_MODE (from))
	       && !m68hc11_small_indexed_indirect_p (from, GET_MODE (from)))
	      || (m68hc11_register_indirect_p (to, GET_MODE (to))
		  && !m68hc11_small_indexed_indirect_p (to, GET_MODE (to))))
	    {
	      rtx ops[3];

	      if (operands[2])
		{
		  ops[0] = operands[2];
		  ops[1] = from;
		  ops[2] = 0;
		  m68hc11_gen_movhi (insn, ops);
		  ops[0] = to;
		  ops[1] = operands[2];
		  m68hc11_gen_movhi (insn, ops);
                  return;
		}
	      else
		{
		  /* !!!! SCz wrong here.  */
                  fatal_insn ("move insn not handled", insn);
		}
	    }
          else
            {
              m68hc11_notice_keep_cc (operands[0]);
              output_asm_insn ("movw\t%1,%0", operands);
            }
	}
      return;
    }

  if (IS_STACK_POP (operands[1]) && H_REG_P (operands[0]))
    {
      cc_status = cc_prev_status;
      switch (REGNO (operands[0]))
	{
	case HARD_X_REGNUM:
	case HARD_Y_REGNUM:
	  output_asm_insn ("pul%0", operands);
	  break;
	case HARD_D_REGNUM:
	  output_asm_insn ("pula", operands);
	  output_asm_insn ("pulb", operands);
	  break;
	default:
	  gcc_unreachable ();
	}
      return;
    }
  /* Some moves to a hard register are special. Not all of them
     are really supported and we have to use a temporary
     location to provide them (either the stack of a temp var).  */
  if (H_REG_P (operands[0]))
    {
      switch (REGNO (operands[0]))
	{
	case HARD_D_REGNUM:
	  if (X_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_X_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else if (next_insn_test_reg (insn, operands[0]))
		{
		  output_asm_insn ("stx\t%t0\n\tldd\t%t0", operands);
		}
	      else
		{
                  m68hc11_notice_keep_cc (operands[0]);
		  output_asm_insn ("pshx\n\tpula\n\tpulb", operands);
		}
	    }
	  else if (Y_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_Y_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else
		{
		  /* %t means *ZTMP scratch register.  */
		  output_asm_insn ("sty\t%t1", operands);
		  output_asm_insn ("ldd\t%t1", operands);
		}
	    }
	  else if (SP_REG_P (operands[1]))
	    {
	      CC_STATUS_INIT;
	      if (ix_reg == 0)
		create_regs_rtx ();
	      if (optimize == 0 || dead_register_here (insn, ix_reg) == 0)
		output_asm_insn ("xgdx", operands);
	      output_asm_insn ("tsx", operands);
	      output_asm_insn ("xgdx", operands);
	    }
	  else if (IS_STACK_POP (operands[1]))
	    {
	      output_asm_insn ("pula\n\tpulb", operands);
	    }
	  else if (GET_CODE (operands[1]) == CONST_INT
		   && INTVAL (operands[1]) == 0)
	    {
	      output_asm_insn ("clra\n\tclrb", operands);
	    }
	  else
	    {
	      output_asm_insn ("ldd\t%1", operands);
	    }
	  break;

	case HARD_X_REGNUM:
	  if (D_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_D_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else if (next_insn_test_reg (insn, operands[0]))
		{
		  output_asm_insn ("std\t%t0\n\tldx\t%t0", operands);
		}
	      else
		{
		  m68hc11_notice_keep_cc (operands[0]);
		  output_asm_insn ("pshb", operands);
		  output_asm_insn ("psha", operands);
		  output_asm_insn ("pulx", operands);
		}
	    }
	  else if (Y_REG_P (operands[1]))
	    {
              /* When both D and Y are dead, use the sequence xgdy, xgdx
                 to move Y into X.  The D and Y registers are modified.  */
              if (optimize && find_regno_note (insn, REG_DEAD, HARD_Y_REGNUM)
                  && dead_register_here (insn, d_reg))
                {
                  output_asm_insn ("xgdy", operands);
                  output_asm_insn ("xgdx", operands);
                  CC_STATUS_INIT;
                }
              else if (!optimize_size)
                {
                  output_asm_insn ("sty\t%t1", operands);
                  output_asm_insn ("ldx\t%t1", operands);
                }
              else
                {
                  CC_STATUS_INIT;
                  output_asm_insn ("pshy", operands);
                  output_asm_insn ("pulx", operands);
                }
	    }
	  else if (SP_REG_P (operands[1]))
	    {
	      /* tsx, tsy preserve the flags */
	      cc_status = cc_prev_status;
	      output_asm_insn ("tsx", operands);
	    }
	  else
	    {
	      output_asm_insn ("ldx\t%1", operands);
	    }
	  break;

	case HARD_Y_REGNUM:
	  if (D_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_D_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else
		{
		  output_asm_insn ("std\t%t1", operands);
		  output_asm_insn ("ldy\t%t1", operands);
		}
	    }
	  else if (X_REG_P (operands[1]))
	    {
              /* When both D and X are dead, use the sequence xgdx, xgdy
                 to move X into Y.  The D and X registers are modified.  */
              if (optimize && find_regno_note (insn, REG_DEAD, HARD_X_REGNUM)
                  && dead_register_here (insn, d_reg))
                {
                  output_asm_insn ("xgdx", operands);
                  output_asm_insn ("xgdy", operands);
                  CC_STATUS_INIT;
                }
              else if (!optimize_size)
                {
                  output_asm_insn ("stx\t%t1", operands);
                  output_asm_insn ("ldy\t%t1", operands);
                }
              else
                {
                  CC_STATUS_INIT;
                  output_asm_insn ("pshx", operands);
                  output_asm_insn ("puly", operands);
                }
	    }
	  else if (SP_REG_P (operands[1]))
	    {
	      /* tsx, tsy preserve the flags */
	      cc_status = cc_prev_status;
	      output_asm_insn ("tsy", operands);
	    }
          else
	    {
	      output_asm_insn ("ldy\t%1", operands);
	    }
	  break;

	case HARD_SP_REGNUM:
	  if (D_REG_P (operands[1]))
	    {
	      m68hc11_notice_keep_cc (operands[0]);
	      output_asm_insn ("xgdx", operands);
	      output_asm_insn ("txs", operands);
	      output_asm_insn ("xgdx", operands);
	    }
	  else if (X_REG_P (operands[1]))
	    {
	      /* tys, txs preserve the flags */
	      cc_status = cc_prev_status;
	      output_asm_insn ("txs", operands);
	    }
	  else if (Y_REG_P (operands[1]))
	    {
	      /* tys, txs preserve the flags */
	      cc_status = cc_prev_status;
	      output_asm_insn ("tys", operands);
	    }
	  else
	    {
	      /* lds sets the flags but the des does not.  */
	      CC_STATUS_INIT;
	      output_asm_insn ("lds\t%1", operands);
	      output_asm_insn ("des", operands);
	    }
	  break;

	default:
	  fatal_insn ("invalid register in the move instruction", insn);
	  break;
	}
      return;
    }
  if (SP_REG_P (operands[1]) && REG_P (operands[0])
      && REGNO (operands[0]) == HARD_FRAME_POINTER_REGNUM)
    {
      output_asm_insn ("sts\t%0", operands);
      return;
    }

  if (IS_STACK_PUSH (operands[0]) && H_REG_P (operands[1]))
    {
      cc_status = cc_prev_status;
      switch (REGNO (operands[1]))
	{
	case HARD_X_REGNUM:
	case HARD_Y_REGNUM:
	  output_asm_insn ("psh%1", operands);
	  break;
	case HARD_D_REGNUM:
	  output_asm_insn ("pshb", operands);
	  output_asm_insn ("psha", operands);
	  break;
	default:
	  gcc_unreachable ();
	}
      return;
    }

  /* Operand 1 must be a hard register.  */
  if (!H_REG_P (operands[1]))
    {
      fatal_insn ("invalid operand in the instruction", insn);
    }

  reg = REGNO (operands[1]);
  switch (reg)
    {
    case HARD_D_REGNUM:
      output_asm_insn ("std\t%0", operands);
      break;

    case HARD_X_REGNUM:
      output_asm_insn ("stx\t%0", operands);
      break;

    case HARD_Y_REGNUM:
      output_asm_insn ("sty\t%0", operands);
      break;

    case HARD_SP_REGNUM:
      if (ix_reg == 0)
	create_regs_rtx ();

      if (REG_P (operands[0]) && REGNO (operands[0]) == SOFT_TMP_REGNUM)
        {
          output_asm_insn ("pshx", operands);
          output_asm_insn ("tsx", operands);
          output_asm_insn ("inx", operands);
          output_asm_insn ("inx", operands);
          output_asm_insn ("stx\t%0", operands);
          output_asm_insn ("pulx", operands);
        }
          
      else if (reg_mentioned_p (ix_reg, operands[0]))
	{
	  output_asm_insn ("sty\t%t0", operands);
	  output_asm_insn ("tsy", operands);
	  output_asm_insn ("sty\t%0", operands);
	  output_asm_insn ("ldy\t%t0", operands);
	}
      else
	{
	  output_asm_insn ("stx\t%t0", operands);
	  output_asm_insn ("tsx", operands);
	  output_asm_insn ("stx\t%0", operands);
	  output_asm_insn ("ldx\t%t0", operands);
	}
      CC_STATUS_INIT;
      break;

    default:
      fatal_insn ("invalid register in the move instruction", insn);
      break;
    }
}

void
m68hc11_gen_movqi (rtx insn, rtx *operands)
{
  /* Move a register or memory to the same location.
     This is possible because such insn can appear
     in a non-optimizing mode.  */
  if (operands[0] == operands[1] || rtx_equal_p (operands[0], operands[1]))
    {
      cc_status = cc_prev_status;
      return;
    }

  if (TARGET_M6812)
    {

      if (H_REG_P (operands[0]) && H_REG_P (operands[1]))
	{
          m68hc11_notice_keep_cc (operands[0]);
	  output_asm_insn ("tfr\t%1,%0", operands);
	}
      else if (H_REG_P (operands[0]))
	{
          if (IS_STACK_POP (operands[1]))
            output_asm_insn ("pul%b0", operands);
	  else if (Q_REG_P (operands[0]))
            output_asm_insn ("lda%0\t%b1", operands);
	  else if (D_REG_P (operands[0]))
	    output_asm_insn ("ldab\t%b1", operands);
	  else
	    goto m6811_move;
	}
      else if (H_REG_P (operands[1]))
	{
	  if (Q_REG_P (operands[1]))
	    output_asm_insn ("sta%1\t%b0", operands);
	  else if (D_REG_P (operands[1]))
	    output_asm_insn ("stab\t%b0", operands);
	  else
	    goto m6811_move;
	}
      else
	{
	  rtx from = operands[1];
	  rtx to = operands[0];

	  if ((m68hc11_register_indirect_p (from, GET_MODE (from))
	       && !m68hc11_small_indexed_indirect_p (from, GET_MODE (from)))
	      || (m68hc11_register_indirect_p (to, GET_MODE (to))
		  && !m68hc11_small_indexed_indirect_p (to, GET_MODE (to))))
	    {
	      rtx ops[3];

	      if (operands[2])
		{
		  ops[0] = operands[2];
		  ops[1] = from;
		  ops[2] = 0;
		  m68hc11_gen_movqi (insn, ops);
		  ops[0] = to;
		  ops[1] = operands[2];
		  m68hc11_gen_movqi (insn, ops);
		}
	      else
		{
		  /* !!!! SCz wrong here.  */
                  fatal_insn ("move insn not handled", insn);
		}
	    }
	  else
	    {
	      if (GET_CODE (from) == CONST_INT && INTVAL (from) == 0)
		{
		  output_asm_insn ("clr\t%b0", operands);
		}
	      else
		{
                  m68hc11_notice_keep_cc (operands[0]);
		  output_asm_insn ("movb\t%b1,%b0", operands);
		}
	    }
	}
      return;
    }

 m6811_move:
  if (H_REG_P (operands[0]))
    {
      switch (REGNO (operands[0]))
	{
	case HARD_B_REGNUM:
	case HARD_D_REGNUM:
	  if (X_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_X_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else
		{
		  output_asm_insn ("stx\t%t1", operands);
		  output_asm_insn ("ldab\t%T0", operands);
		}
	    }
	  else if (Y_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_Y_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else
		{
		  output_asm_insn ("sty\t%t1", operands);
		  output_asm_insn ("ldab\t%T0", operands);
		}
	    }
	  else if (!DB_REG_P (operands[1]) && !D_REG_P (operands[1])
		   && !DA_REG_P (operands[1]))
	    {
	      output_asm_insn ("ldab\t%b1", operands);
	    }
	  else if (DA_REG_P (operands[1]))
	    {
	      output_asm_insn ("tab", operands);
	    }
	  else
	    {
	      cc_status = cc_prev_status;
	      return;
	    }
	  break;

	case HARD_A_REGNUM:
	  if (X_REG_P (operands[1]))
	    {
	      output_asm_insn ("stx\t%t1", operands);
	      output_asm_insn ("ldaa\t%T0", operands);
	    }
	  else if (Y_REG_P (operands[1]))
	    {
	      output_asm_insn ("sty\t%t1", operands);
	      output_asm_insn ("ldaa\t%T0", operands);
	    }
	  else if (!DB_REG_P (operands[1]) && !D_REG_P (operands[1])
		   && !DA_REG_P (operands[1]))
	    {
	      output_asm_insn ("ldaa\t%b1", operands);
	    }
	  else if (!DA_REG_P (operands[1]))
	    {
	      output_asm_insn ("tba", operands);
	    }
	  else
	    {
	      cc_status = cc_prev_status;
	    }
	  break;

	case HARD_X_REGNUM:
	  if (D_REG_P (operands[1]))
	    {
	      if (optimize && find_regno_note (insn, REG_DEAD, HARD_D_REGNUM))
		{
		  m68hc11_output_swap (insn, operands);
		}
	      else
		{
		  output_asm_insn ("stab\t%T1", operands);
		  output_asm_insn ("ldx\t%t1", operands);
		}
	      CC_STATUS_INIT;
	    }
	  else if (Y_REG_P (operands[1]))
	    {
	      output_asm_insn ("sty\t%t0", operands);
	      output_asm_insn ("ldx\t%t0", operands);
	    }
	  else if (GET_CODE (operands[1]) == CONST_INT)
	    {
	      output_asm_insn ("ldx\t%1", operands);
	    }
	  else if (dead_register_here (insn, d_reg))
	    {
	      output_asm_insn ("ldab\t%b1", operands);
	      output_asm_insn ("xgdx", operands);
	    }
	  else if (!reg_mentioned_p (operands[0], operands[1]))
	    {
	      output_asm_insn ("xgdx", operands);
	      output_asm_insn ("ldab\t%b1", operands);
	      output_asm_insn ("xgdx", operands);
	    }
	  else
	    {
	      output_asm_insn ("pshb", operands);
	      output_asm_insn ("ldab\t%b1", operands);
	      output_asm_insn ("stab\t%T1", operands);
	      output_asm_insn ("ldx\t%t1", operands);
	      output_asm_insn ("pulb", operands);
	      CC_STATUS_INIT;
	    }
	  break;

	case HARD_Y_REGNUM:
	  if (D_REG_P (operands[1]))
	    {
	      output_asm_insn ("stab\t%T1", operands);
	      output_asm_insn ("ldy\t%t1", operands);
	      CC_STATUS_INIT;
	    }
	  else if (X_REG_P (operands[1]))
	    {
	      output_asm_insn ("stx\t%t1", operands);
	      output_asm_insn ("ldy\t%t1", operands);
	      CC_STATUS_INIT;
	    }
	  else if (GET_CODE (operands[1]) == CONST_INT)
	    {
	      output_asm_insn ("ldy\t%1", operands);
	    }
	  else if (dead_register_here (insn, d_reg))
	    {
	      output_asm_insn ("ldab\t%b1", operands);
	      output_asm_insn ("xgdy", operands);
	    }
	  else if (!reg_mentioned_p (operands[0], operands[1]))
	    {
	      output_asm_insn ("xgdy", operands);
	      output_asm_insn ("ldab\t%b1", operands);
	      output_asm_insn ("xgdy", operands);
	    }
	  else
	    {
	      output_asm_insn ("pshb", operands);
	      output_asm_insn ("ldab\t%b1", operands);
	      output_asm_insn ("stab\t%T1", operands);
	      output_asm_insn ("ldy\t%t1", operands);
	      output_asm_insn ("pulb", operands);
	      CC_STATUS_INIT;
	    }
	  break;

	default:
	  fatal_insn ("invalid register in the instruction", insn);
	  break;
	}
    }
  else if (H_REG_P (operands[1]))
    {
      switch (REGNO (operands[1]))
	{
	case HARD_D_REGNUM:
	case HARD_B_REGNUM:
	  output_asm_insn ("stab\t%b0", operands);
	  break;

	case HARD_A_REGNUM:
	  output_asm_insn ("staa\t%b0", operands);
	  break;

	case HARD_X_REGNUM:
	  output_asm_insn ("xgdx\n\tstab\t%b0\n\txgdx", operands);
	  break;

	case HARD_Y_REGNUM:
	  output_asm_insn ("xgdy\n\tstab\t%b0\n\txgdy", operands);
	  break;

	default:
	  fatal_insn ("invalid register in the move instruction", insn);
	  break;
	}
      return;
    }
  else
    {
      fatal_insn ("operand 1 must be a hard register", insn);
    }
}

/* Generate the code for a ROTATE or ROTATERT on a QI or HI mode.
   The source and destination must be D or A and the shift must
   be a constant.  */
void
m68hc11_gen_rotate (enum rtx_code code, rtx insn, rtx operands[])
{
  int val;
  
  if (GET_CODE (operands[2]) != CONST_INT
      || (!D_REG_P (operands[0]) && !DA_REG_P (operands[0])))
    fatal_insn ("invalid rotate insn", insn);

  val = INTVAL (operands[2]);
  if (code == ROTATERT)
    val = GET_MODE_SIZE (GET_MODE (operands[0])) * BITS_PER_UNIT - val;

  if (GET_MODE (operands[0]) != QImode)
    CC_STATUS_INIT;

  /* Rotate by 8-bits if the shift is within [5..11].  */
  if (val >= 5 && val <= 11)
    {
      if (TARGET_M6812)
	output_asm_insn ("exg\ta,b", operands);
      else
	{
	  output_asm_insn ("psha", operands);
	  output_asm_insn ("tba", operands);
	  output_asm_insn ("pulb", operands);
	}
      val -= 8;
    }

  /* If the shift is big, invert the rotation.  */
  else if (val >= 12)
    {
      val = val - 16;
    }

  if (val > 0)
    {
      while (--val >= 0)
        {
          /* Set the carry to bit-15, but don't change D yet.  */
          if (GET_MODE (operands[0]) != QImode)
            {
              output_asm_insn ("asra", operands);
              output_asm_insn ("rola", operands);
            }

          /* Rotate B first to move the carry to bit-0.  */
          if (D_REG_P (operands[0]))
            output_asm_insn ("rolb", operands);

          if (GET_MODE (operands[0]) != QImode || DA_REG_P (operands[0]))
            output_asm_insn ("rola", operands);
        }
    }
  else
    {
      while (++val <= 0)
        {
          /* Set the carry to bit-8 of D.  */
          if (GET_MODE (operands[0]) != QImode)
            output_asm_insn ("tap", operands);

          /* Rotate B first to move the carry to bit-7.  */
          if (D_REG_P (operands[0]))
            output_asm_insn ("rorb", operands);

          if (GET_MODE (operands[0]) != QImode || DA_REG_P (operands[0]))
            output_asm_insn ("rora", operands);
        }
    }
}



/* Store in cc_status the expressions that the condition codes will
   describe after execution of an instruction whose pattern is EXP.
   Do not alter them if the instruction would not alter the cc's.  */

void
m68hc11_notice_update_cc (rtx exp, rtx insn ATTRIBUTE_UNUSED)
{
  /* recognize SET insn's.  */
  if (GET_CODE (exp) == SET)
    {
      /* Jumps do not alter the cc's.  */
      if (SET_DEST (exp) == pc_rtx)
	;

      /* NOTE: most instructions don't affect the carry bit, but the
         bhi/bls/bhs/blo instructions use it.  This isn't mentioned in
         the conditions.h header.  */

      /* Function calls clobber the cc's.  */
      else if (GET_CODE (SET_SRC (exp)) == CALL)
	{
	  CC_STATUS_INIT;
	}

      /* Tests and compares set the cc's in predictable ways.  */
      else if (SET_DEST (exp) == cc0_rtx)
	{
	  cc_status.flags = 0;
	  cc_status.value1 = XEXP (exp, 0);
	  cc_status.value2 = XEXP (exp, 1);
	}
      else
	{
	  /* All other instructions affect the condition codes.  */
	  cc_status.flags = 0;
	  cc_status.value1 = XEXP (exp, 0);
	  cc_status.value2 = XEXP (exp, 1);
	}
    }
  else
    {
      /* Default action if we haven't recognized something
         and returned earlier.  */
      CC_STATUS_INIT;
    }

  if (cc_status.value2 != 0)
    switch (GET_CODE (cc_status.value2))
      {
	/* These logical operations can generate several insns.
	   The flags are setup according to what is generated.  */
      case IOR:
      case XOR:
      case AND:
	break;

	/* The (not ...) generates several 'com' instructions for
	   non QImode.  We have to invalidate the flags.  */
      case NOT:
	if (GET_MODE (cc_status.value2) != QImode)
	  CC_STATUS_INIT;
	break;

      case PLUS:
      case MINUS:
      case MULT:
      case DIV:
      case UDIV:
      case MOD:
      case UMOD:
      case NEG:
	if (GET_MODE (cc_status.value2) != VOIDmode)
	  cc_status.flags |= CC_NO_OVERFLOW;
	break;

	/* The asl sets the overflow bit in such a way that this
	   makes the flags unusable for a next compare insn.  */
      case ASHIFT:
      case ROTATE:
      case ROTATERT:
	if (GET_MODE (cc_status.value2) != VOIDmode)
	  cc_status.flags |= CC_NO_OVERFLOW;
	break;

	/* A load/store instruction does not affect the carry.  */
      case MEM:
      case SYMBOL_REF:
      case REG:
      case CONST_INT:
	cc_status.flags |= CC_NO_OVERFLOW;
	break;

      default:
	break;
      }
  if (cc_status.value1 && GET_CODE (cc_status.value1) == REG
      && cc_status.value2
      && reg_overlap_mentioned_p (cc_status.value1, cc_status.value2))
    cc_status.value2 = 0;

  else if (cc_status.value1 && side_effects_p (cc_status.value1))
    cc_status.value1 = 0;

  else if (cc_status.value2 && side_effects_p (cc_status.value2))
    cc_status.value2 = 0;
}

/* The current instruction does not affect the flags but changes
   the register 'reg'.  See if the previous flags can be kept for the
   next instruction to avoid a comparison.  */
void
m68hc11_notice_keep_cc (rtx reg)
{
  if (reg == 0
      || cc_prev_status.value1 == 0
      || rtx_equal_p (reg, cc_prev_status.value1)
      || (cc_prev_status.value2
          && reg_mentioned_p (reg, cc_prev_status.value2)))
    CC_STATUS_INIT;
  else
    cc_status = cc_prev_status;
}



/* Machine Specific Reorg.  */

/* Z register replacement:

   GCC treats the Z register as an index base address register like
   X or Y.  In general, it uses it during reload to compute the address
   of some operand.  This helps the reload pass to avoid to fall into the
   register spill failure.

   The Z register is in the A_REGS class.  In the machine description,
   the 'A' constraint matches it.  The 'x' or 'y' constraints do not.

   It can appear everywhere an X or Y register can appear, except for
   some templates in the clobber section (when a clobber of X or Y is asked).
   For a given instruction, the template must ensure that no more than
   2 'A' registers are used.  Otherwise, the register replacement is not
   possible.

   To replace the Z register, the algorithm is not terrific:
   1. Insns that do not use the Z register are not changed
   2. When a Z register is used, we scan forward the insns to see
   a potential register to use: either X or Y and sometimes D.
   We stop when a call, a label or a branch is seen, or when we
   detect that both X and Y are used (probably at different times, but it does
   not matter).
   3. The register that will be used for the replacement of Z is saved
   in a .page0 register or on the stack.  If the first instruction that
   used Z, uses Z as an input, the value is loaded from another .page0
   register.  The replacement register is pushed on the stack in the
   rare cases where a compare insn uses Z and we couldn't find if X/Y
   are dead.
   4. The Z register is replaced in all instructions until we reach
   the end of the Z-block, as detected by step 2.
   5. If we detect that Z is still alive, its value is saved.
   If the replacement register is alive, its old value is loaded.

   The Z register can be disabled with -ffixed-z.
*/

struct replace_info
{
  rtx first;
  rtx replace_reg;
  int need_save_z;
  int must_load_z;
  int must_save_reg;
  int must_restore_reg;
  rtx last;
  int regno;
  int x_used;
  int y_used;
  int can_use_d;
  int found_call;
  int z_died;
  int z_set_count;
  rtx z_value;
  int must_push_reg;
  int save_before_last;
  int z_loaded_with_sp;
};

static int m68hc11_check_z_replacement (rtx, struct replace_info *);
static void m68hc11_find_z_replacement (rtx, struct replace_info *);
static void m68hc11_z_replacement (rtx);
static void m68hc11_reassign_regs (rtx);

int z_replacement_completed = 0;

/* Analyze the insn to find out which replacement register to use and
   the boundaries of the replacement.
   Returns 0 if we reached the last insn to be replaced, 1 if we can
   continue replacement in next insns.  */

static int
m68hc11_check_z_replacement (rtx insn, struct replace_info *info)
{
  int this_insn_uses_ix;
  int this_insn_uses_iy;
  int this_insn_uses_z;
  int this_insn_uses_z_in_dst;
  int this_insn_uses_d;
  rtx body;
  int z_dies_here;

  /* A call is said to clobber the Z register, we don't need
     to save the value of Z.  We also don't need to restore
     the replacement register (unless it is used by the call).  */
  if (GET_CODE (insn) == CALL_INSN)
    {
      body = PATTERN (insn);

      info->can_use_d = 0;

      /* If the call is an indirect call with Z, we have to use the
         Y register because X can be used as an input (D+X).
         We also must not save Z nor restore Y.  */
      if (reg_mentioned_p (z_reg, body))
	{
	  insn = NEXT_INSN (insn);
	  info->x_used = 1;
	  info->y_used = 0;
	  info->found_call = 1;
	  info->must_restore_reg = 0;
	  info->last = NEXT_INSN (insn);
	}
      info->need_save_z = 0;
      return 0;
    }
  if (GET_CODE (insn) == CODE_LABEL
      || GET_CODE (insn) == BARRIER || GET_CODE (insn) == ASM_INPUT)
    return 0;

  if (GET_CODE (insn) == JUMP_INSN)
    {
      if (reg_mentioned_p (z_reg, insn) == 0)
	return 0;

      info->can_use_d = 0;
      info->must_save_reg = 0;
      info->must_restore_reg = 0;
      info->need_save_z = 0;
      info->last = NEXT_INSN (insn);
      return 0;
    }
  if (GET_CODE (insn) != INSN && GET_CODE (insn) != JUMP_INSN)
    {
      return 1;
    }

  /* Z register dies here.  */
  z_dies_here = find_regno_note (insn, REG_DEAD, HARD_Z_REGNUM) != NULL;

  body = PATTERN (insn);
  if (GET_CODE (body) == SET)
    {
      rtx src = XEXP (body, 1);
      rtx dst = XEXP (body, 0);

      /* Condition code is set here. We have to restore the X/Y and
         save into Z before any test/compare insn because once we save/restore
         we can change the condition codes. When the compare insn uses Z and
         we can't use X/Y, the comparison is made with the *ZREG soft register
         (this is supported by cmphi, cmpqi, tsthi, tstqi patterns).  */
      if (dst == cc0_rtx)
	{
	  if ((GET_CODE (src) == REG && REGNO (src) == HARD_Z_REGNUM)
	      || (GET_CODE (src) == COMPARE &&
		  ((rtx_equal_p (XEXP (src, 0), z_reg)
                    && H_REG_P (XEXP (src, 1)))
		   || (rtx_equal_p (XEXP (src, 1), z_reg)
                       && H_REG_P (XEXP (src, 0))))))
	    {
	      if (insn == info->first)
		{
		  info->must_load_z = 0;
		  info->must_save_reg = 0;
		  info->must_restore_reg = 0;
		  info->need_save_z = 0;
		  info->found_call = 1;
		  info->regno = SOFT_Z_REGNUM;
		  info->last = NEXT_INSN (insn);
		}
	      return 0;
	    }
	  if (reg_mentioned_p (z_reg, src) == 0)
	    {
	      info->can_use_d = 0;
	      return 0;
	    }

	  if (insn != info->first)
	    return 0;

	  /* Compare insn which uses Z.  We have to save/restore the X/Y
	     register without modifying the condition codes.  For this
	     we have to use a push/pop insn.  */
	  info->must_push_reg = 1;
	  info->last = insn;
	}

      /* Z reg is set to something new. We don't need to load it.  */
      if (Z_REG_P (dst))
	{
	  if (!reg_mentioned_p (z_reg, src))
	    {
              /* Z reg is used before being set.  Treat this as
                 a new sequence of Z register replacement.  */
	      if (insn != info->first)
		{
                  return 0;
		}
              info->must_load_z = 0;
	    }
	  info->z_set_count++;
	  info->z_value = src;
	  if (SP_REG_P (src))
	    info->z_loaded_with_sp = 1;
	}
      else if (reg_mentioned_p (z_reg, dst))
	info->can_use_d = 0;

      this_insn_uses_d = reg_mentioned_p (d_reg, src)
	| reg_mentioned_p (d_reg, dst);
      this_insn_uses_ix = reg_mentioned_p (ix_reg, src)
	| reg_mentioned_p (ix_reg, dst);
      this_insn_uses_iy = reg_mentioned_p (iy_reg, src)
	| reg_mentioned_p (iy_reg, dst);
      this_insn_uses_z = reg_mentioned_p (z_reg, src);

      /* If z is used as an address operand (like (MEM (reg z))),
         we can't replace it with d.  */
      if (this_insn_uses_z && !Z_REG_P (src)
          && !(m68hc11_arith_operator (src, GET_MODE (src))
               && Z_REG_P (XEXP (src, 0))
               && !reg_mentioned_p (z_reg, XEXP (src, 1))
               && insn == info->first
               && dead_register_here (insn, d_reg)))
	info->can_use_d = 0;

      this_insn_uses_z_in_dst = reg_mentioned_p (z_reg, dst);
      if (TARGET_M6812 && !z_dies_here
          && ((this_insn_uses_z && side_effects_p (src))
              || (this_insn_uses_z_in_dst && side_effects_p (dst))))
        {
          info->need_save_z = 1;
          info->z_set_count++;
        }
      this_insn_uses_z |= this_insn_uses_z_in_dst;

      if (this_insn_uses_z && this_insn_uses_ix && this_insn_uses_iy)
	{
	  fatal_insn ("registers IX, IY and Z used in the same INSN", insn);
	}

      if (this_insn_uses_d)
	info->can_use_d = 0;

      /* IX and IY are used at the same time, we have to restore
         the value of the scratch register before this insn.  */
      if (this_insn_uses_ix && this_insn_uses_iy)
	{
	  return 0;
	}

      if (this_insn_uses_ix && X_REG_P (dst) && GET_MODE (dst) == SImode)
        info->can_use_d = 0;

      if (info->x_used == 0 && this_insn_uses_ix)
	{
	  if (info->y_used)
	    {
	      /* We have a (set (REG:HI X) (REG:HI Z)).
	         Since we use Z as the replacement register, this insn
	         is no longer necessary.  We turn it into a note.  We must
	         not reload the old value of X.  */
	      if (X_REG_P (dst) && rtx_equal_p (src, z_reg))
		{
		  if (z_dies_here)
		    {
		      info->need_save_z = 0;
		      info->z_died = 1;
		    }
		  info->must_save_reg = 0;
		  info->must_restore_reg = 0;
		  info->found_call = 1;
		  info->can_use_d = 0;
		  PUT_CODE (insn, NOTE);
		  NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED;
		  NOTE_SOURCE_FILE (insn) = 0;
		  info->last = NEXT_INSN (insn);
		  return 0;
		}

	      if (X_REG_P (dst)
		  && (rtx_equal_p (src, z_reg)
		      || (z_dies_here && !reg_mentioned_p (ix_reg, src))))
		{
		  if (z_dies_here)
		    {
		      info->need_save_z = 0;
		      info->z_died = 1;
		    }
		  info->last = NEXT_INSN (insn);
		  info->must_save_reg = 0;
		  info->must_restore_reg = 0;
		}
	      else if (X_REG_P (dst) && reg_mentioned_p (z_reg, src)
		       && !reg_mentioned_p (ix_reg, src))
		{
		  if (z_dies_here)
		    {
		      info->z_died = 1;
		      info->need_save_z = 0;
		    }
		  else if (TARGET_M6812 && side_effects_p (src))
                    {
                      info->last = 0;
                      info->must_restore_reg = 0;
                      return 0;
                    }
                  else
		    {
		      info->save_before_last = 1;
		    }
		  info->must_restore_reg = 0;
		  info->last = NEXT_INSN (insn);
		}
	      else if (info->can_use_d)
		{
		  info->last = NEXT_INSN (insn);
		  info->x_used = 1;
		}
	      return 0;
	    }
	  info->x_used = 1;
	  if (z_dies_here && !reg_mentioned_p (ix_reg, src)
	      && GET_CODE (dst) == REG && REGNO (dst) == HARD_X_REGNUM)
	    {
	      info->need_save_z = 0;
	      info->z_died = 1;
	      info->last = NEXT_INSN (insn);
	      info->regno = HARD_X_REGNUM;
	      info->must_save_reg = 0;
	      info->must_restore_reg = 0;
	      return 0;
	    }
          if (rtx_equal_p (src, z_reg) && rtx_equal_p (dst, ix_reg))
            {
              info->regno = HARD_X_REGNUM;
              info->must_restore_reg = 0;
              info->must_save_reg = 0;
              return 0;
            }
	}
      if (info->y_used == 0 && this_insn_uses_iy)
	{
	  if (info->x_used)
	    {
	      if (Y_REG_P (dst) && rtx_equal_p (src, z_reg))
		{
		  if (z_dies_here)
		    {
		      info->need_save_z = 0;
		      info->z_died = 1;
		    }
		  info->must_save_reg = 0;
		  info->must_restore_reg = 0;
		  info->found_call = 1;
		  info->can_use_d = 0;
		  PUT_CODE (insn, NOTE);
		  NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED;
		  NOTE_SOURCE_FILE (insn) = 0;
		  info->last = NEXT_INSN (insn);
		  return 0;
		}

	      if (Y_REG_P (dst)
		  && (rtx_equal_p (src, z_reg)
		      || (z_dies_here && !reg_mentioned_p (iy_reg, src))))
		{
		  if (z_dies_here)
		    {
		      info->z_died = 1;
		      info->need_save_z = 0;
		    }
		  info->last = NEXT_INSN (insn);
		  info->must_save_reg = 0;
		  info->must_restore_reg = 0;
		}
	      else if (Y_REG_P (dst) && reg_mentioned_p (z_reg, src)
		       && !reg_mentioned_p (iy_reg, src))
		{
		  if (z_dies_here)
		    {
		      info->z_died = 1;
		      info->need_save_z = 0;
		    }
		  else if (TARGET_M6812 && side_effects_p (src))
                    {
                      info->last = 0;
                      info->must_restore_reg = 0;
                      return 0;
                    }
                  else
		    {
		      info->save_before_last = 1;
		    }
		  info->must_restore_reg = 0;
		  info->last = NEXT_INSN (insn);
		}
	      else if (info->can_use_d)
		{
		  info->last = NEXT_INSN (insn);
		  info->y_used = 1;
		}

	      return 0;
	    }
	  info->y_used = 1;
	  if (z_dies_here && !reg_mentioned_p (iy_reg, src)
	      && GET_CODE (dst) == REG && REGNO (dst) == HARD_Y_REGNUM)
	    {
	      info->need_save_z = 0;
	      info->z_died = 1;
	      info->last = NEXT_INSN (insn);
	      info->regno = HARD_Y_REGNUM;
	      info->must_save_reg = 0;
	      info->must_restore_reg = 0;
	      return 0;
	    }
          if (rtx_equal_p (src, z_reg) && rtx_equal_p (dst, iy_reg))
            {
              info->regno = HARD_Y_REGNUM;
              info->must_restore_reg = 0;
              info->must_save_reg = 0;
              return 0;
            }
	}
      if (z_dies_here)
	{
	  info->need_save_z = 0;
	  info->z_died = 1;
	  if (info->last == 0)
	    info->last = NEXT_INSN (insn);
	  return 0;
	}
      return info->last != NULL_RTX ? 0 : 1;
    }
  if (GET_CODE (body) == PARALLEL)
    {
      int i;
      char ix_clobber = 0;
      char iy_clobber = 0;
      char z_clobber = 0;
      this_insn_uses_iy = 0;
      this_insn_uses_ix = 0;
      this_insn_uses_z = 0;

      for (i = XVECLEN (body, 0) - 1; i >= 0; i--)
	{
	  rtx x;
	  int uses_ix, uses_iy, uses_z;

	  x = XVECEXP (body, 0, i);

	  if (info->can_use_d && reg_mentioned_p (d_reg, x))
	    info->can_use_d = 0;

	  uses_ix = reg_mentioned_p (ix_reg, x);
	  uses_iy = reg_mentioned_p (iy_reg, x);
	  uses_z = reg_mentioned_p (z_reg, x);
	  if (GET_CODE (x) == CLOBBER)
	    {
	      ix_clobber |= uses_ix;
	      iy_clobber |= uses_iy;
	      z_clobber |= uses_z;
	    }
	  else
	    {
	      this_insn_uses_ix |= uses_ix;
	      this_insn_uses_iy |= uses_iy;
	      this_insn_uses_z |= uses_z;
	    }
	  if (uses_z && GET_CODE (x) == SET)
	    {
	      rtx dst = XEXP (x, 0);

	      if (Z_REG_P (dst))
		info->z_set_count++;
	    }
          if (TARGET_M6812 && uses_z && side_effects_p (x))
            info->need_save_z = 1;

	  if (z_clobber)
	    info->need_save_z = 0;
	}
      if (debug_m6811)
	{
	  printf ("Uses X:%d Y:%d Z:%d CX:%d CY:%d CZ:%d\n",
		  this_insn_uses_ix, this_insn_uses_iy,
		  this_insn_uses_z, ix_clobber, iy_clobber, z_clobber);
	  debug_rtx (insn);
	}
      if (this_insn_uses_z)
	info->can_use_d = 0;

      if (z_clobber && info->first != insn)
	{
	  info->need_save_z = 0;
	  info->last = insn;
	  return 0;
	}
      if (z_clobber && info->x_used == 0 && info->y_used == 0)
	{
	  if (this_insn_uses_z == 0 && insn == info->first)
	    {
	      info->must_load_z = 0;
	    }
	  if (dead_register_here (insn, d_reg))
	    {
	      info->regno = HARD_D_REGNUM;
	      info->must_save_reg = 0;
	      info->must_restore_reg = 0;
	    }
	  else if (dead_register_here (insn, ix_reg))
	    {
	      info->regno = HARD_X_REGNUM;
	      info->must_save_reg = 0;
	      info->must_restore_reg = 0;
	    }
	  else if (dead_register_here (insn, iy_reg))
	    {
	      info->regno = HARD_Y_REGNUM;
	      info->must_save_reg = 0;
	      info->must_restore_reg = 0;
	    }
	  if (info->regno >= 0)
	    {
	      info->last = NEXT_INSN (insn);
	      return 0;
	    }
	  if (this_insn_uses_ix == 0)
	    {
	      info->regno = HARD_X_REGNUM;
	      info->must_save_reg = 1;
	      info->must_restore_reg = 1;
	    }
	  else if (this_insn_uses_iy == 0)
	    {
	      info->regno = HARD_Y_REGNUM;
	      info->must_save_reg = 1;
	      info->must_restore_reg = 1;
	    }
	  else
	    {
	      info->regno = HARD_D_REGNUM;
	      info->must_save_reg = 1;
	      info->must_restore_reg = 1;
	    }
	  info->last = NEXT_INSN (insn);
	  return 0;
	}

      if (((info->x_used || this_insn_uses_ix) && iy_clobber)
	  || ((info->y_used || this_insn_uses_iy) && ix_clobber))
	{
	  if (this_insn_uses_z)
	    {
	      if (info->y_used == 0 && iy_clobber)
		{
		  info->regno = HARD_Y_REGNUM;
		  info->must_save_reg = 0;
		  info->must_restore_reg = 0;
		}
	      if (info->first != insn
		  && ((info->y_used && ix_clobber)
		      || (info->x_used && iy_clobber)))
		info->last = insn;
	      else
		info->last = NEXT_INSN (insn);
	      info->save_before_last = 1;
	    }
	  return 0;
	}
      if (this_insn_uses_ix && this_insn_uses_iy)
	{
          if (this_insn_uses_z)
            {
              fatal_insn ("cannot do z-register replacement", insn);
            }
	  return 0;
	}
      if (info->x_used == 0 && (this_insn_uses_ix || ix_clobber))
	{
	  if (info->y_used)
	    {
	      return 0;
	    }
	  info->x_used = 1;
	  if (iy_clobber || z_clobber)
	    {
	      info->last = NEXT_INSN (insn);
	      info->save_before_last = 1;
	      return 0;
	    }
	}

      if (info->y_used == 0 && (this_insn_uses_iy || iy_clobber))
	{
	  if (info->x_used)
	    {
	      return 0;
	    }
	  info->y_used = 1;
	  if (ix_clobber || z_clobber)
	    {
	      info->last = NEXT_INSN (insn);
	      info->save_before_last = 1;
	      return 0;
	    }
	}
      if (z_dies_here)
	{
	  info->z_died = 1;
	  info->need_save_z = 0;
	}
      return 1;
    }
  if (GET_CODE (body) == CLOBBER)
    {

      /* IX and IY are used at the same time, we have to restore
         the value of the scratch register before this insn.  */
      if (this_insn_uses_ix && this_insn_uses_iy)
	{
	  return 0;
	}
      if (info->x_used == 0 && this_insn_uses_ix)
	{
	  if (info->y_used)
	    {
	      return 0;
	    }
	  info->x_used = 1;
	}
      if (info->y_used == 0 && this_insn_uses_iy)
	{
	  if (info->x_used)
	    {
	      return 0;
	    }
	  info->y_used = 1;
	}
      return 1;
    }
  return 1;
}

static void
m68hc11_find_z_replacement (rtx insn, struct replace_info *info)
{
  int reg;

  info->replace_reg = NULL_RTX;
  info->must_load_z = 1;
  info->need_save_z = 1;
  info->must_save_reg = 1;
  info->must_restore_reg = 1;
  info->first = insn;
  info->x_used = 0;
  info->y_used = 0;
  info->can_use_d = TARGET_M6811 ? 1 : 0;
  info->found_call = 0;
  info->z_died = 0;
  info->last = 0;
  info->regno = -1;
  info->z_set_count = 0;
  info->z_value = NULL_RTX;
  info->must_push_reg = 0;
  info->save_before_last = 0;
  info->z_loaded_with_sp = 0;

  /* Scan the insn forward to find an address register that is not used.
     Stop when:
     - the flow of the program changes,
     - when we detect that both X and Y are necessary,
     - when the Z register dies,
     - when the condition codes are set.  */

  for (; insn && info->z_died == 0; insn = NEXT_INSN (insn))
    {
      if (m68hc11_check_z_replacement (insn, info) == 0)
	break;
    }

  /* May be we can use Y or X if they contain the same value as Z.
     This happens very often after the reload.  */
  if (info->z_set_count == 1)
    {
      rtx p = info->first;
      rtx v = 0;

      if (info->x_used)
	{
	  v = find_last_value (iy_reg, &p, insn, 1);
	}
      else if (info->y_used)
	{
	  v = find_last_value (ix_reg, &p, insn, 1);
	}
      if (v && (v != iy_reg && v != ix_reg) && rtx_equal_p (v, info->z_value))
	{
	  if (info->x_used)
	    info->regno = HARD_Y_REGNUM;
	  else
	    info->regno = HARD_X_REGNUM;
	  info->must_load_z = 0;
	  info->must_save_reg = 0;
	  info->must_restore_reg = 0;
	  info->found_call = 1;
	}
    }
  if (info->z_set_count == 0)
    info->need_save_z = 0;

  if (insn == 0)
    info->need_save_z = 0;

  if (info->last == 0)
    info->last = insn;

  if (info->regno >= 0)
    {
      reg = info->regno;
      info->replace_reg = gen_rtx_REG (HImode, reg);
    }
  else if (info->can_use_d)
    {
      reg = HARD_D_REGNUM;
      info->replace_reg = d_reg;
    }
  else if (info->x_used)
    {
      reg = HARD_Y_REGNUM;
      info->replace_reg = iy_reg;
    }
  else
    {
      reg = HARD_X_REGNUM;
      info->replace_reg = ix_reg;
    }
  info->regno = reg;

  if (info->must_save_reg && info->must_restore_reg)
    {
      if (insn && dead_register_here (insn, info->replace_reg))
	{
	  info->must_save_reg = 0;
	  info->must_restore_reg = 0;
	}
    }
}

/* The insn uses the Z register.  Find a replacement register for it
   (either X or Y) and replace it in the insn and the next ones until
   the flow changes or the replacement register is used.  Instructions
   are emitted before and after the Z-block to preserve the value of
   Z and of the replacement register.  */

static void
m68hc11_z_replacement (rtx insn)
{
  rtx replace_reg_qi;
  rtx replace_reg;
  struct replace_info info;

  /* Find trivial case where we only need to replace z with the
     equivalent soft register.  */
  if (GET_CODE (insn) == INSN && GET_CODE (PATTERN (insn)) == SET)
    {
      rtx body = PATTERN (insn);
      rtx src = XEXP (body, 1);
      rtx dst = XEXP (body, 0);

      if (Z_REG_P (dst) && (H_REG_P (src) && !SP_REG_P (src)))
	{
	  XEXP (body, 0) = gen_rtx_REG (GET_MODE (dst), SOFT_Z_REGNUM);
	  return;
	}
      else if (Z_REG_P (src)
	       && ((H_REG_P (dst) && !SP_REG_P (src)) || dst == cc0_rtx))
	{
	  XEXP (body, 1) = gen_rtx_REG (GET_MODE (src), SOFT_Z_REGNUM);
	  return;
	}
      else if (D_REG_P (dst)
	       && m68hc11_arith_operator (src, GET_MODE (src))
	       && D_REG_P (XEXP (src, 0)) && Z_REG_P (XEXP (src, 1)))
	{
	  XEXP (src, 1) = gen_rtx_REG (GET_MODE (src), SOFT_Z_REGNUM);
	  return;
	}
      else if (Z_REG_P (dst) && GET_CODE (src) == CONST_INT
	       && INTVAL (src) == 0)
	{
	  XEXP (body, 0) = gen_rtx_REG (GET_MODE (dst), SOFT_Z_REGNUM);
          /* Force it to be re-recognized.  */
          INSN_CODE (insn) = -1;
	  return;
	}
    }

  m68hc11_find_z_replacement (insn, &info);

  replace_reg = info.replace_reg;
  replace_reg_qi = NULL_RTX;

  /* Save the X register in a .page0 location.  */
  if (info.must_save_reg && !info.must_push_reg)
    {
      rtx dst;

      if (info.must_push_reg && 0)
	dst = gen_rtx_MEM (HImode,
		       gen_rtx_PRE_DEC (HImode,
				gen_rtx_REG (HImode, HARD_SP_REGNUM)));
      else
	dst = gen_rtx_REG (HImode, SOFT_SAVED_XY_REGNUM);

      emit_insn_before (gen_movhi (dst,
				   gen_rtx_REG (HImode, info.regno)), insn);
    }
  if (info.must_load_z && !info.must_push_reg)
    {
      emit_insn_before (gen_movhi (gen_rtx_REG (HImode, info.regno),
				   gen_rtx_REG (HImode, SOFT_Z_REGNUM)),
			insn);
    }


  /* Replace all occurrence of Z by replace_reg.
     Stop when the last instruction to replace is reached.
     Also stop when we detect a change in the flow (but it's not
     necessary; just safeguard).  */

  for (; insn && insn != info.last; insn = NEXT_INSN (insn))
    {
      rtx body;

      if (GET_CODE (insn) == CODE_LABEL || GET_CODE (insn) == BARRIER)
	break;

      if (GET_CODE (insn) != INSN
	  && GET_CODE (insn) != CALL_INSN && GET_CODE (insn) != JUMP_INSN)
	continue;

      body = PATTERN (insn);
      if (GET_CODE (body) == SET || GET_CODE (body) == PARALLEL
          || GET_CODE (body) == ASM_OPERANDS
	  || GET_CODE (insn) == CALL_INSN || GET_CODE (insn) == JUMP_INSN)
	{
          rtx note;

	  if (debug_m6811 && reg_mentioned_p (replace_reg, body))
	    {
	      printf ("Reg mentioned here...:\n");
	      fflush (stdout);
	      debug_rtx (insn);
	    }

	  /* Stack pointer was decremented by 2 due to the push.
	     Correct that by adding 2 to the destination.  */
	  if (info.must_push_reg
	      && info.z_loaded_with_sp && GET_CODE (body) == SET)
	    {
	      rtx src, dst;

	      src = SET_SRC (body);
	      dst = SET_DEST (body);
	      if (SP_REG_P (src) && Z_REG_P (dst))
		emit_insn_after (gen_addhi3 (dst, dst, const2_rtx), insn);
	    }

	  /* Replace any (REG:HI Z) occurrence by either X or Y.  */
	  if (!validate_replace_rtx (z_reg, replace_reg, insn))
	    {
	      INSN_CODE (insn) = -1;
	      if (!validate_replace_rtx (z_reg, replace_reg, insn))
		fatal_insn ("cannot do z-register replacement", insn);
	    }

	  /* Likewise for (REG:QI Z).  */
	  if (reg_mentioned_p (z_reg, insn))
	    {
	      if (replace_reg_qi == NULL_RTX)
		replace_reg_qi = gen_rtx_REG (QImode, REGNO (replace_reg));
	      validate_replace_rtx (z_reg_qi, replace_reg_qi, insn);
	    }

          /* If there is a REG_INC note on Z, replace it with a
             REG_INC note on the replacement register.  This is necessary
             to make sure that the flow pass will identify the change
             and it will not remove a possible insn that saves Z.  */
          for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
            {
              if (REG_NOTE_KIND (note) == REG_INC
                  && GET_CODE (XEXP (note, 0)) == REG
                  && REGNO (XEXP (note, 0)) == REGNO (z_reg))
                {
                  XEXP (note, 0) = replace_reg;
                }
            }
	}
      if (GET_CODE (insn) == CALL_INSN || GET_CODE (insn) == JUMP_INSN)
	break;
    }

  /* Save Z before restoring the old value.  */
  if (insn && info.need_save_z && !info.must_push_reg)
    {
      rtx save_pos_insn = insn;

      /* If Z is clobber by the last insn, we have to save its value
         before the last instruction.  */
      if (info.save_before_last)
	save_pos_insn = PREV_INSN (save_pos_insn);

      emit_insn_before (gen_movhi (gen_rtx_REG (HImode, SOFT_Z_REGNUM),
				   gen_rtx_REG (HImode, info.regno)),
			save_pos_insn);
    }

  if (info.must_push_reg && info.last)
    {
      rtx new_body, body;

      body = PATTERN (info.last);
      new_body = gen_rtx_PARALLEL (VOIDmode,
			  gen_rtvec (3, body,
				     gen_rtx_USE (VOIDmode,
					      replace_reg),
				     gen_rtx_USE (VOIDmode,
					      gen_rtx_REG (HImode,
						       SOFT_Z_REGNUM))));
      PATTERN (info.last) = new_body;

      /* Force recognition on insn since we changed it.  */
      INSN_CODE (insn) = -1;

      if (!validate_replace_rtx (z_reg, replace_reg, info.last))
	{
	  fatal_insn ("invalid Z register replacement for insn", insn);
	}
      insn = NEXT_INSN (info.last);
    }

  /* Restore replacement register unless it was died.  */
  if (insn && info.must_restore_reg && !info.must_push_reg)
    {
      rtx dst;

      if (info.must_push_reg && 0)
	dst = gen_rtx_MEM (HImode,
		       gen_rtx_POST_INC (HImode,
				gen_rtx_REG (HImode, HARD_SP_REGNUM)));
      else
	dst = gen_rtx_REG (HImode, SOFT_SAVED_XY_REGNUM);

      emit_insn_before (gen_movhi (gen_rtx_REG (HImode, info.regno),
				   dst), insn);
    }

}


/* Scan all the insn and re-affects some registers
    - The Z register (if it was used), is affected to X or Y depending
      on the instruction.  */

static void
m68hc11_reassign_regs (rtx first)
{
  rtx insn;

  ix_reg = gen_rtx_REG (HImode, HARD_X_REGNUM);
  iy_reg = gen_rtx_REG (HImode, HARD_Y_REGNUM);
  z_reg = gen_rtx_REG (HImode, HARD_Z_REGNUM);
  z_reg_qi = gen_rtx_REG (QImode, HARD_Z_REGNUM);

  /* Scan all insns to replace Z by X or Y preserving the old value
     of X/Y and restoring it afterward.  */

  for (insn = first; insn; insn = NEXT_INSN (insn))
    {
      rtx body;

      if (GET_CODE (insn) == CODE_LABEL
	  || GET_CODE (insn) == NOTE || GET_CODE (insn) == BARRIER)
	continue;

      if (!INSN_P (insn))
	continue;

      body = PATTERN (insn);
      if (GET_CODE (body) == CLOBBER || GET_CODE (body) == USE)
	continue;

      if (GET_CODE (body) == CONST_INT || GET_CODE (body) == ASM_INPUT
	  || GET_CODE (body) == ASM_OPERANDS
	  || GET_CODE (body) == UNSPEC || GET_CODE (body) == UNSPEC_VOLATILE)
	continue;

      if (GET_CODE (body) == SET || GET_CODE (body) == PARALLEL
	  || GET_CODE (insn) == CALL_INSN || GET_CODE (insn) == JUMP_INSN)
	{

	  /* If Z appears in this insn, replace it in the current insn
	     and the next ones until the flow changes or we have to
	     restore back the replacement register.  */

	  if (reg_mentioned_p (z_reg, body))
	    {
	      m68hc11_z_replacement (insn);
	    }
	}
      else
	{
	  printf ("insn not handled by Z replacement:\n");
	  fflush (stdout);
	  debug_rtx (insn);
	}
    }
}


/* Machine-dependent reorg pass.
   Specific optimizations are defined here:
    - this pass changes the Z register into either X or Y
      (it preserves X/Y previous values in a memory slot in page0).

   When this pass is finished, the global variable
   'z_replacement_completed' is set to 2.  */

static void
m68hc11_reorg (void)
{
  int split_done = 0;
  rtx insn, first;

  z_replacement_completed = 0;
  z_reg = gen_rtx_REG (HImode, HARD_Z_REGNUM);
  first = get_insns ();

  /* Some RTX are shared at this point.  This breaks the Z register
     replacement, unshare everything.  */
  unshare_all_rtl_again (first);

  /* Force a split of all splittable insn.  This is necessary for the
     Z register replacement mechanism because we end up with basic insns.  */
  split_all_insns_noflow ();
  split_done = 1;

  z_replacement_completed = 1;
  m68hc11_reassign_regs (first);

  if (optimize)
    compute_bb_for_insn ();

  /* After some splitting, there are some opportunities for CSE pass.
     This happens quite often when 32-bit or above patterns are split.  */
  if (optimize > 0 && split_done)
    {
      reload_cse_regs (first);
    }

  /* Re-create the REG_DEAD notes.  These notes are used in the machine
     description to use the best assembly directives.  */
  if (optimize)
    {
      /* Before recomputing the REG_DEAD notes, remove all of them.
         This is necessary because the reload_cse_regs() pass can
         have replaced some (MEM) with a register.  In that case,
         the REG_DEAD that could exist for that register may become
         wrong.  */
      for (insn = first; insn; insn = NEXT_INSN (insn))
        {
          if (INSN_P (insn))
            {
              rtx *pnote;

              pnote = &REG_NOTES (insn);
              while (*pnote != 0)
                {
                  if (REG_NOTE_KIND (*pnote) == REG_DEAD)
                    *pnote = XEXP (*pnote, 1);
                  else
                    pnote = &XEXP (*pnote, 1);
                }
            }
        }

      life_analysis (PROP_REG_INFO | PROP_DEATH_NOTES);
    }

  z_replacement_completed = 2;

  /* If optimizing, then go ahead and split insns that must be
     split after Z register replacement.  This gives more opportunities
     for peephole (in particular for consecutives xgdx/xgdy).  */
  if (optimize > 0)
    split_all_insns_noflow ();

  /* Once insns are split after the z_replacement_completed == 2,
     we must not re-run the life_analysis.  The xgdx/xgdy patterns
     are not recognized and the life_analysis pass removes some
     insns because it thinks some (SETs) are noops or made to dead
     stores (which is false due to the swap).

     Do a simple pass to eliminate the noop set that the final
     split could generate (because it was easier for split definition).  */
  {
    rtx insn;

    for (insn = first; insn; insn = NEXT_INSN (insn))
      {
	rtx body;

	if (INSN_DELETED_P (insn))
	  continue;
	if (!INSN_P (insn))
	  continue;

	/* Remove the (set (R) (R)) insns generated by some splits.  */
	body = PATTERN (insn);
	if (GET_CODE (body) == SET
	    && rtx_equal_p (SET_SRC (body), SET_DEST (body)))
	  {
	    PUT_CODE (insn, NOTE);
	    NOTE_LINE_NUMBER (insn) = NOTE_INSN_DELETED;
	    NOTE_SOURCE_FILE (insn) = 0;
	    continue;
	  }
      }
  }
}

/* Override memcpy */

static void
m68hc11_init_libfuncs (void)
{
  memcpy_libfunc = init_one_libfunc ("__memcpy");
  memcmp_libfunc = init_one_libfunc ("__memcmp");
  memset_libfunc = init_one_libfunc ("__memset");
}



/* Cost functions.  */

/* Cost of moving memory.  */
int
m68hc11_memory_move_cost (enum machine_mode mode, enum reg_class class,
                          int in ATTRIBUTE_UNUSED)
{
  if (class <= H_REGS && class > NO_REGS)
    {
      if (GET_MODE_SIZE (mode) <= 2)
	return COSTS_N_INSNS (1) + (reload_completed | reload_in_progress);
      else
	return COSTS_N_INSNS (2) + (reload_completed | reload_in_progress);
    }
  else
    {
      if (GET_MODE_SIZE (mode) <= 2)
	return COSTS_N_INSNS (3);
      else
	return COSTS_N_INSNS (4);
    }
}


/* Cost of moving data from a register of class 'from' to on in class 'to'.
   Reload does not check the constraint of set insns when the two registers
   have a move cost of 2.  Setting a higher cost will force reload to check
   the constraints.  */
int
m68hc11_register_move_cost (enum machine_mode mode, enum reg_class from,
                            enum reg_class to)
{
  /* All costs are symmetric, so reduce cases by putting the
     lower number class as the destination.  */
  if (from < to)
    {
      enum reg_class tmp = to;
      to = from, from = tmp;
    }
  if (to >= S_REGS)
    return m68hc11_memory_move_cost (mode, S_REGS, 0);
  else if (from <= S_REGS)
    return COSTS_N_INSNS (1) + (reload_completed | reload_in_progress);
  else
    return COSTS_N_INSNS (2);
}


/* Provide the costs of an addressing mode that contains ADDR.
   If ADDR is not a valid address, its cost is irrelevant.  */

static int
m68hc11_address_cost (rtx addr)
{
  int cost = 4;

  switch (GET_CODE (addr))
    {
    case REG:
      /* Make the cost of hard registers and specially SP, FP small.  */
      if (REGNO (addr) < FIRST_PSEUDO_REGISTER)
	cost = 0;
      else
	cost = 1;
      break;

    case SYMBOL_REF:
      cost = 8;
      break;

    case LABEL_REF:
    case CONST:
      cost = 0;
      break;

    case PLUS:
      {
	register rtx plus0 = XEXP (addr, 0);
	register rtx plus1 = XEXP (addr, 1);

	if (GET_CODE (plus0) != REG)
	  break;

	switch (GET_CODE (plus1))
	  {
	  case CONST_INT:
	    if (INTVAL (plus1) >= 2 * m68hc11_max_offset
		|| INTVAL (plus1) < m68hc11_min_offset)
	      cost = 3;
	    else if (INTVAL (plus1) >= m68hc11_max_offset)
	      cost = 2;
	    else
	      cost = 1;
	    if (REGNO (plus0) < FIRST_PSEUDO_REGISTER)
	      cost += 0;
	    else
	      cost += 1;
	    break;

	  case SYMBOL_REF:
	    cost = 8;
	    break;

	  case CONST:
	  case LABEL_REF:
	    cost = 0;
	    break;

	  default:
	    break;
	  }
	break;
      }
    case PRE_DEC:
    case PRE_INC:
      if (SP_REG_P (XEXP (addr, 0)))
	cost = 1;
      break;

    default:
      break;
    }
  if (debug_m6811)
    {
      printf ("Address cost: %d for :", cost);
      fflush (stdout);
      debug_rtx (addr);
    }

  return cost;
}

static int
m68hc11_shift_cost (enum machine_mode mode, rtx x, int shift)
{
  int total;

  total = rtx_cost (x, SET);
  if (mode == QImode)
    total += m68hc11_cost->shiftQI_const[shift % 8];
  else if (mode == HImode)
    total += m68hc11_cost->shiftHI_const[shift % 16];
  else if (shift == 8 || shift == 16 || shift == 32)
    total += m68hc11_cost->shiftHI_const[8];
  else if (shift != 0 && shift != 16 && shift != 32)
    {
      total += m68hc11_cost->shiftHI_const[1] * shift;
    }

  /* For SI and others, the cost is higher.  */
  if (GET_MODE_SIZE (mode) > 2 && (shift % 16) != 0)
    total *= GET_MODE_SIZE (mode) / 2;

  /* When optimizing for size, make shift more costly so that
     multiplications are preferred.  */
  if (optimize_size && (shift % 8) != 0)
    total *= 2;
  
  return total;
}

static int
m68hc11_rtx_costs_1 (rtx x, enum rtx_code code,
                     enum rtx_code outer_code ATTRIBUTE_UNUSED)
{
  enum machine_mode mode = GET_MODE (x);
  int extra_cost = 0;
  int total;

  switch (code)
    {
    case ROTATE:
    case ROTATERT:
    case ASHIFT:
    case LSHIFTRT:
    case ASHIFTRT:
      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	{
          return m68hc11_shift_cost (mode, XEXP (x, 0), INTVAL (XEXP (x, 1)));
	}

      total = rtx_cost (XEXP (x, 0), code) + rtx_cost (XEXP (x, 1), code);
      total += m68hc11_cost->shift_var;
      return total;

    case AND:
    case XOR:
    case IOR:
      total = rtx_cost (XEXP (x, 0), code) + rtx_cost (XEXP (x, 1), code);
      total += m68hc11_cost->logical;

      /* Logical instructions are byte instructions only.  */
      total *= GET_MODE_SIZE (mode);
      return total;

    case MINUS:
    case PLUS:
      total = rtx_cost (XEXP (x, 0), code) + rtx_cost (XEXP (x, 1), code);
      total += m68hc11_cost->add;
      if (GET_MODE_SIZE (mode) > 2)
	{
	  total *= GET_MODE_SIZE (mode) / 2;
	}
      return total;

    case UDIV:
    case DIV:
    case MOD:
      total = rtx_cost (XEXP (x, 0), code) + rtx_cost (XEXP (x, 1), code);
      switch (mode)
        {
        case QImode:
          total += m68hc11_cost->divQI;
          break;

        case HImode:
          total += m68hc11_cost->divHI;
          break;

        case SImode:
        default:
          total += m68hc11_cost->divSI;
          break;
        }
      return total;
      
    case MULT:
      /* mul instruction produces 16-bit result.  */
      if (mode == HImode && GET_CODE (XEXP (x, 0)) == ZERO_EXTEND
          && GET_CODE (XEXP (x, 1)) == ZERO_EXTEND)
        return m68hc11_cost->multQI
          + rtx_cost (XEXP (XEXP (x, 0), 0), code)
          + rtx_cost (XEXP (XEXP (x, 1), 0), code);

      /* emul instruction produces 32-bit result for 68HC12.  */
      if (TARGET_M6812 && mode == SImode
          && GET_CODE (XEXP (x, 0)) == ZERO_EXTEND
          && GET_CODE (XEXP (x, 1)) == ZERO_EXTEND)
        return m68hc11_cost->multHI
          + rtx_cost (XEXP (XEXP (x, 0), 0), code)
          + rtx_cost (XEXP (XEXP (x, 1), 0), code);

      total = rtx_cost (XEXP (x, 0), code) + rtx_cost (XEXP (x, 1), code);
      switch (mode)
        {
        case QImode:
          total += m68hc11_cost->multQI;
          break;

        case HImode:
          total += m68hc11_cost->multHI;
          break;

        case SImode:
        default:
          total += m68hc11_cost->multSI;
          break;
        }
      return total;

    case NEG:
    case SIGN_EXTEND:
      extra_cost = COSTS_N_INSNS (2);

      /* Fall through */
    case NOT:
    case COMPARE:
    case ABS:
    case ZERO_EXTEND:
      total = extra_cost + rtx_cost (XEXP (x, 0), code);
      if (mode == QImode)
	{
	  return total + COSTS_N_INSNS (1);
	}
      if (mode == HImode)
	{
	  return total + COSTS_N_INSNS (2);
	}
      if (mode == SImode)
	{
	  return total + COSTS_N_INSNS (4);
	}
      return total + COSTS_N_INSNS (8);

    case IF_THEN_ELSE:
      if (GET_CODE (XEXP (x, 1)) == PC || GET_CODE (XEXP (x, 2)) == PC)
	return COSTS_N_INSNS (1);

      return COSTS_N_INSNS (1);

    default:
      return COSTS_N_INSNS (4);
    }
}

static bool
m68hc11_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
      /* Constants are cheap.  Moving them in registers must be avoided
         because most instructions do not handle two register operands.  */
    case CONST_INT:
    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
    case CONST_DOUBLE:
      /* Logical and arithmetic operations with a constant operand are
	 better because they are not supported with two registers.  */
      /* 'clr' is slow */
      if (outer_code == SET && x == const0_rtx)
	 /* After reload, the reload_cse pass checks the cost to change
	    a SET into a PLUS.  Make const0 cheap then.  */
	*total = 1 - reload_completed;
      else
	*total = 0;
      return true;
    
    case ROTATE:
    case ROTATERT:
    case ASHIFT:
    case LSHIFTRT:
    case ASHIFTRT:
    case MINUS:
    case PLUS:
    case AND:
    case XOR:
    case IOR:
    case UDIV:
    case DIV:
    case MOD:
    case MULT:
    case NEG:
    case SIGN_EXTEND:
    case NOT:
    case COMPARE:
    case ZERO_EXTEND:
    case IF_THEN_ELSE:
      *total = m68hc11_rtx_costs_1 (x, code, outer_code);
      return true;

    default:
      return false;
    }
}


/* Worker function for TARGET_ASM_FILE_START.  */

static void
m68hc11_file_start (void)
{
  default_file_start ();
  
  fprintf (asm_out_file, "\t.mode %s\n", TARGET_SHORT ? "mshort" : "mlong");
}


/* Worker function for TARGET_ASM_CONSTRUCTOR.  */

static void
m68hc11_asm_out_constructor (rtx symbol, int priority)
{
  default_ctor_section_asm_out_constructor (symbol, priority);
  fprintf (asm_out_file, "\t.globl\t__do_global_ctors\n");
}

/* Worker function for TARGET_ASM_DESTRUCTOR.  */

static void
m68hc11_asm_out_destructor (rtx symbol, int priority)
{
  default_dtor_section_asm_out_destructor (symbol, priority);
  fprintf (asm_out_file, "\t.globl\t__do_global_dtors\n");
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
m68hc11_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
			  int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, HARD_D_REGNUM);
}

/* Return true if type TYPE should be returned in memory.
   Blocks and data types largers than 4 bytes cannot be returned
   in the register (D + X = 4).  */

static bool
m68hc11_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  if (TYPE_MODE (type) == BLKmode)
    {
      HOST_WIDE_INT size = int_size_in_bytes (type);
      return (size == -1 || size > 4);
    }
  else
    return GET_MODE_SIZE (TYPE_MODE (type)) > 4;
}

#include "gt-m68hc11.h"
