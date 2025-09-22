/* Subroutines for insn-output.c for Renesas H8/300.
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Steve Chamberlain (sac@cygnus.com),
   Jim Wilson (wilson@cygnus.com), and Doug Evans (dje@cygnus.com).

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
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
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
#include "function.h"
#include "optabs.h"
#include "toplev.h"
#include "c-pragma.h"
#include "tm_p.h"
#include "ggc.h"
#include "target.h"
#include "target-def.h"

/* Classifies a h8300_src_operand or h8300_dst_operand.

   H8OP_IMMEDIATE
	A constant operand of some sort.

   H8OP_REGISTER
	An ordinary register.

   H8OP_MEM_ABSOLUTE
	A memory reference with a constant address.

   H8OP_MEM_BASE
	A memory reference with a register as its address.

   H8OP_MEM_COMPLEX
	Some other kind of memory reference.  */
enum h8300_operand_class
{
  H8OP_IMMEDIATE,
  H8OP_REGISTER,
  H8OP_MEM_ABSOLUTE,
  H8OP_MEM_BASE,
  H8OP_MEM_COMPLEX,
  NUM_H8OPS
};

/* For a general two-operand instruction, element [X][Y] gives
   the length of the opcode fields when the first operand has class
   (X + 1) and the second has class Y.  */
typedef unsigned char h8300_length_table[NUM_H8OPS - 1][NUM_H8OPS];

/* Forward declarations.  */
static const char *byte_reg (rtx, int);
static int h8300_interrupt_function_p (tree);
static int h8300_saveall_function_p (tree);
static int h8300_monitor_function_p (tree);
static int h8300_os_task_function_p (tree);
static void h8300_emit_stack_adjustment (int, HOST_WIDE_INT);
static HOST_WIDE_INT round_frame_size (HOST_WIDE_INT);
static unsigned int compute_saved_regs (void);
static void push (int);
static void pop (int);
static const char *cond_string (enum rtx_code);
static unsigned int h8300_asm_insn_count (const char *);
static tree h8300_handle_fndecl_attribute (tree *, tree, tree, int, bool *);
static tree h8300_handle_eightbit_data_attribute (tree *, tree, tree, int, bool *);
static tree h8300_handle_tiny_data_attribute (tree *, tree, tree, int, bool *);
#ifndef OBJECT_FORMAT_ELF
static void h8300_asm_named_section (const char *, unsigned int, tree);
#endif
static int h8300_and_costs (rtx);
static int h8300_shift_costs (rtx);
static void          h8300_push_pop               (int, int, int, int);
static int           h8300_stack_offset_p         (rtx, int);
static int           h8300_ldm_stm_regno          (rtx, int, int, int);
static void          h8300_reorg                  (void);
static unsigned int  h8300_constant_length        (rtx);
static unsigned int  h8300_displacement_length    (rtx, int);
static unsigned int  h8300_classify_operand       (rtx, int, enum h8300_operand_class *);
static unsigned int  h8300_length_from_table      (rtx, rtx, const h8300_length_table *);
static unsigned int  h8300_unary_length           (rtx);
static unsigned int  h8300_short_immediate_length (rtx);
static unsigned int  h8300_bitfield_length        (rtx, rtx);
static unsigned int  h8300_binary_length          (rtx, const h8300_length_table *);
static bool          h8300_short_move_mem_p       (rtx, enum rtx_code);
static unsigned int  h8300_move_length            (rtx *, const h8300_length_table *);

/* CPU_TYPE, says what cpu we're compiling for.  */
int cpu_type;

/* True if a #pragma interrupt has been seen for the current function.  */
static int pragma_interrupt;

/* True if a #pragma saveall has been seen for the current function.  */
static int pragma_saveall;

static const char *const names_big[] =
{ "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7" };

static const char *const names_extended[] =
{ "er0", "er1", "er2", "er3", "er4", "er5", "er6", "er7" };

static const char *const names_upper_extended[] =
{ "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7" };

/* Points to one of the above.  */
/* ??? The above could be put in an array indexed by CPU_TYPE.  */
const char * const *h8_reg_names;

/* Various operations needed by the following, indexed by CPU_TYPE.  */

const char *h8_push_op, *h8_pop_op, *h8_mov_op;

/* Value of MOVE_RATIO.  */
int h8300_move_ratio;

/* See below where shifts are handled for explanation of this enum.  */

enum shift_alg
{
  SHIFT_INLINE,
  SHIFT_ROT_AND,
  SHIFT_SPECIAL,
  SHIFT_LOOP
};

/* Symbols of the various shifts which can be used as indices.  */

enum shift_type
{
  SHIFT_ASHIFT, SHIFT_LSHIFTRT, SHIFT_ASHIFTRT
};

/* Macros to keep the shift algorithm tables small.  */
#define INL SHIFT_INLINE
#define ROT SHIFT_ROT_AND
#define LOP SHIFT_LOOP
#define SPC SHIFT_SPECIAL

/* The shift algorithms for each machine, mode, shift type, and shift
   count are defined below.  The three tables below correspond to
   QImode, HImode, and SImode, respectively.  Each table is organized
   by, in the order of indices, machine, shift type, and shift count.  */

static enum shift_alg shift_alg_qi[3][3][8] = {
  {
    /* TARGET_H8300  */
    /* 0    1    2    3    4    5    6    7  */
    { INL, INL, INL, INL, INL, ROT, ROT, ROT }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, ROT, ROT, ROT }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, LOP, LOP, SPC }  /* SHIFT_ASHIFTRT */
  },
  {
    /* TARGET_H8300H  */
    /* 0    1    2    3    4    5    6    7  */
    { INL, INL, INL, INL, INL, ROT, ROT, ROT }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, ROT, ROT, ROT }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, LOP, LOP, SPC }  /* SHIFT_ASHIFTRT */
  },
  {
    /* TARGET_H8300S  */
    /*  0    1    2    3    4    5    6    7  */
    { INL, INL, INL, INL, INL, INL, ROT, ROT }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, INL, ROT, ROT }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, INL, INL, SPC }  /* SHIFT_ASHIFTRT */
  }
};

static enum shift_alg shift_alg_hi[3][3][16] = {
  {
    /* TARGET_H8300  */
    /*  0    1    2    3    4    5    6    7  */
    /*  8    9   10   11   12   13   14   15  */
    { INL, INL, INL, INL, INL, INL, INL, SPC,
      SPC, SPC, SPC, SPC, SPC, SPC, SPC, SPC }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, LOP, LOP, SPC,
      SPC, SPC, SPC, SPC, SPC, SPC, SPC, SPC }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, LOP, LOP, SPC,
      SPC, SPC, SPC, SPC, SPC, SPC, SPC, SPC }, /* SHIFT_ASHIFTRT */
  },
  {
    /* TARGET_H8300H  */
    /*  0    1    2    3    4    5    6    7  */
    /*  8    9   10   11   12   13   14   15  */
    { INL, INL, INL, INL, INL, INL, INL, SPC,
      SPC, SPC, SPC, SPC, SPC, ROT, ROT, ROT }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, INL, INL, SPC,
      SPC, SPC, SPC, SPC, SPC, ROT, ROT, ROT }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, INL, INL, SPC,
      SPC, SPC, SPC, SPC, SPC, SPC, SPC, SPC }, /* SHIFT_ASHIFTRT */
  },
  {
    /* TARGET_H8300S  */
    /*  0    1    2    3    4    5    6    7  */
    /*  8    9   10   11   12   13   14   15  */
    { INL, INL, INL, INL, INL, INL, INL, INL,
      SPC, SPC, SPC, SPC, SPC, ROT, ROT, ROT }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, INL, INL, INL,
      SPC, SPC, SPC, SPC, SPC, ROT, ROT, ROT }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, INL, INL, INL,
      SPC, SPC, SPC, SPC, SPC, SPC, SPC, SPC }, /* SHIFT_ASHIFTRT */
  }
};

static enum shift_alg shift_alg_si[3][3][32] = {
  {
    /* TARGET_H8300  */
    /*  0    1    2    3    4    5    6    7  */
    /*  8    9   10   11   12   13   14   15  */
    /* 16   17   18   19   20   21   22   23  */
    /* 24   25   26   27   28   29   30   31  */
    { INL, INL, INL, LOP, LOP, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, LOP, LOP, LOP, LOP,
      SPC, SPC, SPC, SPC, SPC, LOP, LOP, LOP,
      SPC, SPC, SPC, SPC, LOP, LOP, LOP, SPC }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, LOP, LOP, LOP, LOP, LOP,
      SPC, SPC, LOP, LOP, LOP, LOP, LOP, SPC,
      SPC, SPC, SPC, LOP, LOP, LOP, LOP, LOP,
      SPC, SPC, SPC, SPC, SPC, LOP, LOP, SPC }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, LOP, LOP, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, LOP, LOP, LOP, SPC,
      SPC, SPC, LOP, LOP, LOP, LOP, LOP, LOP,
      SPC, SPC, SPC, LOP, LOP, LOP, LOP, SPC }, /* SHIFT_ASHIFTRT */
  },
  {
    /* TARGET_H8300H  */
    /*  0    1    2    3    4    5    6    7  */
    /*  8    9   10   11   12   13   14   15  */
    /* 16   17   18   19   20   21   22   23  */
    /* 24   25   26   27   28   29   30   31  */
    { INL, INL, INL, INL, INL, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, LOP, LOP, LOP, SPC,
      SPC, SPC, SPC, SPC, LOP, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, SPC, SPC, SPC, SPC }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, LOP, LOP, LOP, SPC,
      SPC, SPC, SPC, SPC, LOP, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, SPC, SPC, SPC, SPC }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, LOP, LOP, LOP, LOP,
      SPC, SPC, SPC, SPC, LOP, LOP, LOP, LOP,
      SPC, LOP, LOP, LOP, LOP, LOP, LOP, SPC }, /* SHIFT_ASHIFTRT */
  },
  {
    /* TARGET_H8300S  */
    /*  0    1    2    3    4    5    6    7  */
    /*  8    9   10   11   12   13   14   15  */
    /* 16   17   18   19   20   21   22   23  */
    /* 24   25   26   27   28   29   30   31  */
    { INL, INL, INL, INL, INL, INL, INL, INL,
      INL, INL, INL, LOP, LOP, LOP, LOP, SPC,
      SPC, SPC, SPC, SPC, SPC, SPC, LOP, LOP,
      SPC, SPC, LOP, LOP, SPC, SPC, SPC, SPC }, /* SHIFT_ASHIFT   */
    { INL, INL, INL, INL, INL, INL, INL, INL,
      INL, INL, INL, LOP, LOP, LOP, LOP, SPC,
      SPC, SPC, SPC, SPC, SPC, SPC, LOP, LOP,
      SPC, SPC, LOP, LOP, SPC, SPC, SPC, SPC }, /* SHIFT_LSHIFTRT */
    { INL, INL, INL, INL, INL, INL, INL, INL,
      INL, INL, INL, LOP, LOP, LOP, LOP, LOP,
      SPC, SPC, SPC, SPC, SPC, SPC, LOP, LOP,
      SPC, SPC, LOP, LOP, LOP, LOP, LOP, SPC }, /* SHIFT_ASHIFTRT */
  }
};

#undef INL
#undef ROT
#undef LOP
#undef SPC

enum h8_cpu
{
  H8_300,
  H8_300H,
  H8_S
};

/* Initialize various cpu specific globals at start up.  */

void
h8300_init_once (void)
{
  static const char *const h8_push_ops[2] = { "push" , "push.l" };
  static const char *const h8_pop_ops[2]  = { "pop"  , "pop.l"  };
  static const char *const h8_mov_ops[2]  = { "mov.w", "mov.l"  };

  if (TARGET_H8300)
    {
      cpu_type = (int) CPU_H8300;
      h8_reg_names = names_big;
    }
  else
    {
      /* For this we treat the H8/300H and H8S the same.  */
      cpu_type = (int) CPU_H8300H;
      h8_reg_names = names_extended;
    }
  h8_push_op = h8_push_ops[cpu_type];
  h8_pop_op = h8_pop_ops[cpu_type];
  h8_mov_op = h8_mov_ops[cpu_type];

  if (!TARGET_H8300S && TARGET_MAC)
    {
      error ("-ms2600 is used without -ms");
      target_flags |= MASK_H8300S_1;
    }

  if (TARGET_H8300 && TARGET_NORMAL_MODE)
    {
      error ("-mn is used without -mh or -ms");
      target_flags ^= MASK_NORMAL_MODE;
    }

  /* Some of the shifts are optimized for speed by default.
     See http://gcc.gnu.org/ml/gcc-patches/2002-07/msg01858.html
     If optimizing for size, change shift_alg for those shift to
     SHIFT_LOOP.  */
  if (optimize_size)
    {
      /* H8/300 */
      shift_alg_hi[H8_300][SHIFT_ASHIFT][5] = SHIFT_LOOP;
      shift_alg_hi[H8_300][SHIFT_ASHIFT][6] = SHIFT_LOOP;
      shift_alg_hi[H8_300][SHIFT_ASHIFT][13] = SHIFT_LOOP;
      shift_alg_hi[H8_300][SHIFT_ASHIFT][14] = SHIFT_LOOP;

      shift_alg_hi[H8_300][SHIFT_LSHIFTRT][13] = SHIFT_LOOP;
      shift_alg_hi[H8_300][SHIFT_LSHIFTRT][14] = SHIFT_LOOP;

      shift_alg_hi[H8_300][SHIFT_ASHIFTRT][13] = SHIFT_LOOP;
      shift_alg_hi[H8_300][SHIFT_ASHIFTRT][14] = SHIFT_LOOP;

      /* H8/300H */
      shift_alg_hi[H8_300H][SHIFT_ASHIFT][5] = SHIFT_LOOP;
      shift_alg_hi[H8_300H][SHIFT_ASHIFT][6] = SHIFT_LOOP;

      shift_alg_hi[H8_300H][SHIFT_LSHIFTRT][5] = SHIFT_LOOP;
      shift_alg_hi[H8_300H][SHIFT_LSHIFTRT][6] = SHIFT_LOOP;

      shift_alg_hi[H8_300H][SHIFT_ASHIFTRT][5] = SHIFT_LOOP;
      shift_alg_hi[H8_300H][SHIFT_ASHIFTRT][6] = SHIFT_LOOP;
      shift_alg_hi[H8_300H][SHIFT_ASHIFTRT][13] = SHIFT_LOOP;
      shift_alg_hi[H8_300H][SHIFT_ASHIFTRT][14] = SHIFT_LOOP;

      /* H8S */
      shift_alg_hi[H8_S][SHIFT_ASHIFTRT][14] = SHIFT_LOOP;
    }

  /* Work out a value for MOVE_RATIO.  */
  if (!TARGET_H8300SX)
    {
      /* Memory-memory moves are quite expensive without the
	 h8sx instructions.  */
      h8300_move_ratio = 3;
    }
  else if (flag_omit_frame_pointer)
    {
      /* movmd sequences are fairly cheap when er6 isn't fixed.  They can
	 sometimes be as short as two individual memory-to-memory moves,
	 but since they use all the call-saved registers, it seems better
	 to allow up to three moves here.  */
      h8300_move_ratio = 4;
    }
  else if (optimize_size)
    {
      /* In this case we don't use movmd sequences since they tend
	 to be longer than calls to memcpy().  Memory-to-memory
	 moves are cheaper than for !TARGET_H8300SX, so it makes
	 sense to have a slightly higher threshold.  */
      h8300_move_ratio = 4;
    }
  else
    {
      /* We use movmd sequences for some moves since it can be quicker
	 than calling memcpy().  The sequences will need to save and
	 restore er6 though, so bump up the cost.  */
      h8300_move_ratio = 6;
    }
}

/* Implement REG_CLASS_FROM_LETTER.

   Some patterns need to use er6 as a scratch register.  This is
   difficult to arrange since er6 is the frame pointer and usually
   can't be spilled.

   Such patterns should define two alternatives, one which allows only
   er6 and one which allows any general register.  The former alternative
   should have a 'd' constraint while the latter should be disparaged and
   use 'D'.

   Normally, 'd' maps to DESTINATION_REGS and 'D' maps to GENERAL_REGS.
   However, there are cases where they should be NO_REGS:

     - 'd' should be NO_REGS when reloading a function that uses the
       frame pointer.  In this case, DESTINATION_REGS won't contain any
       spillable registers, so the first alternative can't be used.

     - -fno-omit-frame-pointer means that the frame pointer will
       always be in use.  It's therefore better to map 'd' to NO_REGS
       before reload so that register allocator will pick the second
       alternative.

     - we would like 'D' to be be NO_REGS when the frame pointer isn't
       live, but we the frame pointer may turn out to be needed after
       we start reload, and then we may have already decided we don't
       have a choice, so we can't do that.  Forcing the register
       allocator to use er6 if possible might produce better code for
       small functions: it's more efficient to save and restore er6 in
       the prologue & epilogue than to do it in a define_split.
       Hopefully disparaging 'D' will have a similar effect, without
       forcing a reload failure if the frame pointer is found to be
       needed too late.  */

enum reg_class
h8300_reg_class_from_letter (int c)
{
  switch (c)
    {
    case 'a':
      return MAC_REGS;

    case 'c':
      return COUNTER_REGS;

    case 'd':
      if (!flag_omit_frame_pointer && !reload_completed)
	return NO_REGS;
      if (frame_pointer_needed && reload_in_progress)
	return NO_REGS;
      return DESTINATION_REGS;

    case 'D':
      /* The meaning of a constraint shouldn't change dynamically, so
	 we can't make this NO_REGS.  */
      return GENERAL_REGS;

    case 'f':
      return SOURCE_REGS;

    default:
      return NO_REGS;
    }
}

/* Return the byte register name for a register rtx X.  B should be 0
   if you want a lower byte register.  B should be 1 if you want an
   upper byte register.  */

static const char *
byte_reg (rtx x, int b)
{
  static const char *const names_small[] = {
    "r0l", "r0h", "r1l", "r1h", "r2l", "r2h", "r3l", "r3h",
    "r4l", "r4h", "r5l", "r5h", "r6l", "r6h", "r7l", "r7h"
  };

  gcc_assert (REG_P (x));

  return names_small[REGNO (x) * 2 + b];
}

/* REGNO must be saved/restored across calls if this macro is true.  */

#define WORD_REG_USED(regno)						\
  (regno < SP_REG							\
   /* No need to save registers if this function will not return.  */	\
   && ! TREE_THIS_VOLATILE (current_function_decl)			\
   && (h8300_saveall_function_p (current_function_decl)			\
       /* Save any call saved register that was used.  */		\
       || (regs_ever_live[regno] && !call_used_regs[regno])		\
       /* Save the frame pointer if it was used.  */			\
       || (regno == HARD_FRAME_POINTER_REGNUM && regs_ever_live[regno])	\
       /* Save any register used in an interrupt handler.  */		\
       || (h8300_current_function_interrupt_function_p ()		\
	   && regs_ever_live[regno])					\
       /* Save call clobbered registers in non-leaf interrupt		\
	  handlers.  */							\
       || (h8300_current_function_interrupt_function_p ()		\
	   && call_used_regs[regno]					\
	   && !current_function_is_leaf)))

/* Output assembly language to FILE for the operation OP with operand size
   SIZE to adjust the stack pointer.  */

static void
h8300_emit_stack_adjustment (int sign, HOST_WIDE_INT size)
{
  /* If the frame size is 0, we don't have anything to do.  */
  if (size == 0)
    return;

  /* H8/300 cannot add/subtract a large constant with a single
     instruction.  If a temporary register is available, load the
     constant to it and then do the addition.  */
  if (TARGET_H8300
      && size > 4
      && !h8300_current_function_interrupt_function_p ()
      && !(cfun->static_chain_decl != NULL && sign < 0))
    {
      rtx r3 = gen_rtx_REG (Pmode, 3);
      emit_insn (gen_movhi (r3, GEN_INT (sign * size)));
      emit_insn (gen_addhi3 (stack_pointer_rtx,
			     stack_pointer_rtx, r3));
    }
  else
    {
      /* The stack adjustment made here is further optimized by the
	 splitter.  In case of H8/300, the splitter always splits the
	 addition emitted here to make the adjustment
	 interrupt-safe.  */
      if (Pmode == HImode)
	emit_insn (gen_addhi3 (stack_pointer_rtx,
			       stack_pointer_rtx, GEN_INT (sign * size)));
      else
	emit_insn (gen_addsi3 (stack_pointer_rtx,
			       stack_pointer_rtx, GEN_INT (sign * size)));
    }
}

/* Round up frame size SIZE.  */

static HOST_WIDE_INT
round_frame_size (HOST_WIDE_INT size)
{
  return ((size + STACK_BOUNDARY / BITS_PER_UNIT - 1)
	  & -STACK_BOUNDARY / BITS_PER_UNIT);
}

/* Compute which registers to push/pop.
   Return a bit vector of registers.  */

static unsigned int
compute_saved_regs (void)
{
  unsigned int saved_regs = 0;
  int regno;

  /* Construct a bit vector of registers to be pushed/popped.  */
  for (regno = 0; regno <= HARD_FRAME_POINTER_REGNUM; regno++)
    {
      if (WORD_REG_USED (regno))
	saved_regs |= 1 << regno;
    }

  /* Don't push/pop the frame pointer as it is treated separately.  */
  if (frame_pointer_needed)
    saved_regs &= ~(1 << HARD_FRAME_POINTER_REGNUM);

  return saved_regs;
}

/* Emit an insn to push register RN.  */

static void
push (int rn)
{
  rtx reg = gen_rtx_REG (word_mode, rn);
  rtx x;

  if (TARGET_H8300)
    x = gen_push_h8300 (reg);
  else if (!TARGET_NORMAL_MODE)
    x = gen_push_h8300hs_advanced (reg);
  else
    x = gen_push_h8300hs_normal (reg);
  x = emit_insn (x);
  REG_NOTES (x) = gen_rtx_EXPR_LIST (REG_INC, stack_pointer_rtx, 0);
}

/* Emit an insn to pop register RN.  */

static void
pop (int rn)
{
  rtx reg = gen_rtx_REG (word_mode, rn);
  rtx x;

  if (TARGET_H8300)
    x = gen_pop_h8300 (reg);
  else if (!TARGET_NORMAL_MODE)
    x = gen_pop_h8300hs_advanced (reg);
  else
    x = gen_pop_h8300hs_normal (reg);
  x = emit_insn (x);
  REG_NOTES (x) = gen_rtx_EXPR_LIST (REG_INC, stack_pointer_rtx, 0);
}

/* Emit an instruction to push or pop NREGS consecutive registers
   starting at register REGNO.  POP_P selects a pop rather than a
   push and RETURN_P is true if the instruction should return.

   It must be possible to do the requested operation in a single
   instruction.  If NREGS == 1 && !RETURN_P, use a normal push
   or pop insn.  Otherwise emit a parallel of the form:

     (parallel
       [(return)  ;; if RETURN_P
	(save or restore REGNO)
	(save or restore REGNO + 1)
	...
	(save or restore REGNO + NREGS - 1)
	(set sp (plus sp (const_int adjust)))]  */

static void
h8300_push_pop (int regno, int nregs, int pop_p, int return_p)
{
  int i, j;
  rtvec vec;
  rtx sp, offset;

  /* See whether we can use a simple push or pop.  */
  if (!return_p && nregs == 1)
    {
      if (pop_p)
	pop (regno);
      else
	push (regno);
      return;
    }

  /* We need one element for the return insn, if present, one for each
     register, and one for stack adjustment.  */
  vec = rtvec_alloc ((return_p != 0) + nregs + 1);
  sp = stack_pointer_rtx;
  i = 0;

  /* Add the return instruction.  */
  if (return_p)
    {
      RTVEC_ELT (vec, i) = gen_rtx_RETURN (VOIDmode);
      i++;
    }

  /* Add the register moves.  */
  for (j = 0; j < nregs; j++)
    {
      rtx lhs, rhs;

      if (pop_p)
	{
	  /* Register REGNO + NREGS - 1 is popped first.  Before the
	     stack adjustment, its slot is at address @sp.  */
	  lhs = gen_rtx_REG (SImode, regno + j);
	  rhs = gen_rtx_MEM (SImode, plus_constant (sp, (nregs - j - 1) * 4));
	}
      else
	{
	  /* Register REGNO is pushed first and will be stored at @(-4,sp).  */
	  lhs = gen_rtx_MEM (SImode, plus_constant (sp, (j + 1) * -4));
	  rhs = gen_rtx_REG (SImode, regno + j);
	}
      RTVEC_ELT (vec, i + j) = gen_rtx_SET (VOIDmode, lhs, rhs);
    }

  /* Add the stack adjustment.  */
  offset = GEN_INT ((pop_p ? nregs : -nregs) * 4);
  RTVEC_ELT (vec, i + j) = gen_rtx_SET (VOIDmode, sp,
					gen_rtx_PLUS (Pmode, sp, offset));

  emit_insn (gen_rtx_PARALLEL (VOIDmode, vec));
}

/* Return true if X has the value sp + OFFSET.  */

static int
h8300_stack_offset_p (rtx x, int offset)
{
  if (offset == 0)
    return x == stack_pointer_rtx;

  return (GET_CODE (x) == PLUS
	  && XEXP (x, 0) == stack_pointer_rtx
	  && GET_CODE (XEXP (x, 1)) == CONST_INT
	  && INTVAL (XEXP (x, 1)) == offset);
}

/* A subroutine of h8300_ldm_stm_parallel.  X is one pattern in
   something that may be an ldm or stm instruction.  If it fits
   the required template, return the register it loads or stores,
   otherwise return -1.

   LOAD_P is true if X should be a load, false if it should be a store.
   NREGS is the number of registers that the whole instruction is expected
   to load or store.  INDEX is the index of the register that X should
   load or store, relative to the lowest-numbered register.  */

static int
h8300_ldm_stm_regno (rtx x, int load_p, int index, int nregs)
{
  int regindex, memindex, offset;

  if (load_p)
    regindex = 0, memindex = 1, offset = (nregs - index - 1) * 4;
  else
    memindex = 0, regindex = 1, offset = (index + 1) * -4;

  if (GET_CODE (x) == SET
      && GET_CODE (XEXP (x, regindex)) == REG
      && GET_CODE (XEXP (x, memindex)) == MEM
      && h8300_stack_offset_p (XEXP (XEXP (x, memindex), 0), offset))
    return REGNO (XEXP (x, regindex));

  return -1;
}

/* Return true if the elements of VEC starting at FIRST describe an
   ldm or stm instruction (LOAD_P says which).  */

int
h8300_ldm_stm_parallel (rtvec vec, int load_p, int first)
{
  rtx last;
  int nregs, i, regno, adjust;

  /* There must be a stack adjustment, a register move, and at least one
     other operation (a return or another register move).  */
  if (GET_NUM_ELEM (vec) < 3)
    return false;

  /* Get the range of registers to be pushed or popped.  */
  nregs = GET_NUM_ELEM (vec) - first - 1;
  regno = h8300_ldm_stm_regno (RTVEC_ELT (vec, first), load_p, 0, nregs);

  /* Check that the call to h8300_ldm_stm_regno succeeded and
     that we're only dealing with GPRs.  */
  if (regno < 0 || regno + nregs > 8)
    return false;

  /* 2-register h8s instructions must start with an even-numbered register.
     3- and 4-register instructions must start with er0 or er4.  */
  if (!TARGET_H8300SX)
    {
      if ((regno & 1) != 0)
	return false;
      if (nregs > 2 && (regno & 3) != 0)
	return false;
    }

  /* Check the other loads or stores.  */
  for (i = 1; i < nregs; i++)
    if (h8300_ldm_stm_regno (RTVEC_ELT (vec, first + i), load_p, i, nregs)
	!= regno + i)
      return false;

  /* Check the stack adjustment.  */
  last = RTVEC_ELT (vec, first + nregs);
  adjust = (load_p ? nregs : -nregs) * 4;
  return (GET_CODE (last) == SET
	  && SET_DEST (last) == stack_pointer_rtx
	  && h8300_stack_offset_p (SET_SRC (last), adjust));
}

/* This is what the stack looks like after the prolog of
   a function with a frame has been set up:

   <args>
   PC
   FP			<- fp
   <locals>
   <saved registers>	<- sp

   This is what the stack looks like after the prolog of
   a function which doesn't have a frame:

   <args>
   PC
   <locals>
   <saved registers>	<- sp
*/

/* Generate RTL code for the function prologue.  */

void
h8300_expand_prologue (void)
{
  int regno;
  int saved_regs;
  int n_regs;

  /* If the current function has the OS_Task attribute set, then
     we have a naked prologue.  */
  if (h8300_os_task_function_p (current_function_decl))
    return;

  if (h8300_monitor_function_p (current_function_decl))
    /* My understanding of monitor functions is they act just like
       interrupt functions, except the prologue must mask
       interrupts.  */
    emit_insn (gen_monitor_prologue ());

  if (frame_pointer_needed)
    {
      /* Push fp.  */
      push (HARD_FRAME_POINTER_REGNUM);
      emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
    }

  /* Push the rest of the registers in ascending order.  */
  saved_regs = compute_saved_regs ();
  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno += n_regs)
    {
      n_regs = 1;
      if (saved_regs & (1 << regno))
	{
	  if (TARGET_H8300S)
	    {
	      /* See how many registers we can push at the same time.  */
	      if ((!TARGET_H8300SX || (regno & 3) == 0)
		  && ((saved_regs >> regno) & 0x0f) == 0x0f)
		n_regs = 4;

	      else if ((!TARGET_H8300SX || (regno & 3) == 0)
		       && ((saved_regs >> regno) & 0x07) == 0x07)
		n_regs = 3;

	      else if ((!TARGET_H8300SX || (regno & 1) == 0)
		       && ((saved_regs >> regno) & 0x03) == 0x03)
		n_regs = 2;
	    }

	  h8300_push_pop (regno, n_regs, 0, 0);
	}
    }

  /* Leave room for locals.  */
  h8300_emit_stack_adjustment (-1, round_frame_size (get_frame_size ()));
}

/* Return nonzero if we can use "rts" for the function currently being
   compiled.  */

int
h8300_can_use_return_insn_p (void)
{
  return (reload_completed
	  && !frame_pointer_needed
	  && get_frame_size () == 0
	  && compute_saved_regs () == 0);
}

/* Generate RTL code for the function epilogue.  */

void
h8300_expand_epilogue (void)
{
  int regno;
  int saved_regs;
  int n_regs;
  HOST_WIDE_INT frame_size;
  bool returned_p;

  if (h8300_os_task_function_p (current_function_decl))
    /* OS_Task epilogues are nearly naked -- they just have an
       rts instruction.  */
    return;

  frame_size = round_frame_size (get_frame_size ());
  returned_p = false;

  /* Deallocate locals.  */
  h8300_emit_stack_adjustment (1, frame_size);

  /* Pop the saved registers in descending order.  */
  saved_regs = compute_saved_regs ();
  for (regno = FIRST_PSEUDO_REGISTER - 1; regno >= 0; regno -= n_regs)
    {
      n_regs = 1;
      if (saved_regs & (1 << regno))
	{
	  if (TARGET_H8300S)
	    {
	      /* See how many registers we can pop at the same time.  */
	      if ((TARGET_H8300SX || (regno & 3) == 3)
		  && ((saved_regs << 3 >> regno) & 0x0f) == 0x0f)
		n_regs = 4;

	      else if ((TARGET_H8300SX || (regno & 3) == 2)
		       && ((saved_regs << 2 >> regno) & 0x07) == 0x07)
		n_regs = 3;

	      else if ((TARGET_H8300SX || (regno & 1) == 1)
		       && ((saved_regs << 1 >> regno) & 0x03) == 0x03)
		n_regs = 2;
	    }

	  /* See if this pop would be the last insn before the return.
	     If so, use rte/l or rts/l instead of pop or ldm.l.  */
	  if (TARGET_H8300SX
	      && !frame_pointer_needed
	      && frame_size == 0
	      && (saved_regs & ((1 << (regno - n_regs + 1)) - 1)) == 0)
	    returned_p = true;

	  h8300_push_pop (regno - n_regs + 1, n_regs, 1, returned_p);
	}
    }

  /* Pop frame pointer if we had one.  */
  if (frame_pointer_needed)
    {
      if (TARGET_H8300SX)
	returned_p = true;
      h8300_push_pop (HARD_FRAME_POINTER_REGNUM, 1, 1, returned_p);
    }

  if (!returned_p)
    emit_insn (gen_rtx_RETURN (VOIDmode));
}

/* Return nonzero if the current function is an interrupt
   function.  */

int
h8300_current_function_interrupt_function_p (void)
{
  return (h8300_interrupt_function_p (current_function_decl)
	  || h8300_monitor_function_p (current_function_decl));
}

/* Output assembly code for the start of the file.  */

static void
h8300_file_start (void)
{
  default_file_start ();

  if (TARGET_H8300H)
    fputs (TARGET_NORMAL_MODE ? "\t.h8300hn\n" : "\t.h8300h\n", asm_out_file);
  else if (TARGET_H8300SX)
    fputs (TARGET_NORMAL_MODE ? "\t.h8300sxn\n" : "\t.h8300sx\n", asm_out_file);
  else if (TARGET_H8300S)
    fputs (TARGET_NORMAL_MODE ? "\t.h8300sn\n" : "\t.h8300s\n", asm_out_file);
}

/* Output assembly language code for the end of file.  */

static void
h8300_file_end (void)
{
  fputs ("\t.end\n", asm_out_file);
}

/* Split an add of a small constant into two adds/subs insns.

   If USE_INCDEC_P is nonzero, we generate the last insn using inc/dec
   instead of adds/subs.  */

void
split_adds_subs (enum machine_mode mode, rtx *operands)
{
  HOST_WIDE_INT val = INTVAL (operands[1]);
  rtx reg = operands[0];
  HOST_WIDE_INT sign = 1;
  HOST_WIDE_INT amount;
  rtx (*gen_add) (rtx, rtx, rtx);

  /* Force VAL to be positive so that we do not have to consider the
     sign.  */
  if (val < 0)
    {
      val = -val;
      sign = -1;
    }

  switch (mode)
    {
    case HImode:
      gen_add = gen_addhi3;
      break;

    case SImode:
      gen_add = gen_addsi3;
      break;

    default:
      gcc_unreachable ();
    }

  /* Try different amounts in descending order.  */
  for (amount = (TARGET_H8300H || TARGET_H8300S) ? 4 : 2;
       amount > 0;
       amount /= 2)
    {
      for (; val >= amount; val -= amount)
	emit_insn (gen_add (reg, reg, GEN_INT (sign * amount)));
    }

  return;
}

/* Handle machine specific pragmas for compatibility with existing
   compilers for the H8/300.

   pragma saveall generates prologue/epilogue code which saves and
   restores all the registers on function entry.

   pragma interrupt saves and restores all registers, and exits with
   an rte instruction rather than an rts.  A pointer to a function
   with this attribute may be safely used in an interrupt vector.  */

void
h8300_pr_interrupt (struct cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  pragma_interrupt = 1;
}

void
h8300_pr_saveall (struct cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  pragma_saveall = 1;
}

/* If the next function argument with MODE and TYPE is to be passed in
   a register, return a reg RTX for the hard register in which to pass
   the argument.  CUM represents the state after the last argument.
   If the argument is to be pushed, NULL_RTX is returned.  */

rtx
function_arg (CUMULATIVE_ARGS *cum, enum machine_mode mode,
	      tree type, int named)
{
  static const char *const hand_list[] = {
    "__main",
    "__cmpsi2",
    "__divhi3",
    "__modhi3",
    "__udivhi3",
    "__umodhi3",
    "__divsi3",
    "__modsi3",
    "__udivsi3",
    "__umodsi3",
    "__mulhi3",
    "__mulsi3",
    "__reg_memcpy",
    "__reg_memset",
    "__ucmpsi2",
    0,
  };

  rtx result = NULL_RTX;
  const char *fname;
  int regpass = 0;

  /* Never pass unnamed arguments in registers.  */
  if (!named)
    return NULL_RTX;

  /* Pass 3 regs worth of data in regs when user asked on the command line.  */
  if (TARGET_QUICKCALL)
    regpass = 3;

  /* If calling hand written assembler, use 4 regs of args.  */
  if (cum->libcall)
    {
      const char * const *p;

      fname = XSTR (cum->libcall, 0);

      /* See if this libcall is one of the hand coded ones.  */
      for (p = hand_list; *p && strcmp (*p, fname) != 0; p++)
	;

      if (*p)
	regpass = 4;
    }

  if (regpass)
    {
      int size;

      if (mode == BLKmode)
	size = int_size_in_bytes (type);
      else
	size = GET_MODE_SIZE (mode);

      if (size + cum->nbytes <= regpass * UNITS_PER_WORD
	  && cum->nbytes / UNITS_PER_WORD <= 3)
	result = gen_rtx_REG (mode, cum->nbytes / UNITS_PER_WORD);
    }

  return result;
}

/* Compute the cost of an and insn.  */

static int
h8300_and_costs (rtx x)
{
  rtx operands[4];

  if (GET_MODE (x) == QImode)
    return 1;

  if (GET_MODE (x) != HImode
      && GET_MODE (x) != SImode)
    return 100;

  operands[0] = NULL;
  operands[1] = XEXP (x, 0);
  operands[2] = XEXP (x, 1);
  operands[3] = x;
  return compute_logical_op_length (GET_MODE (x), operands) / 2;
}

/* Compute the cost of a shift insn.  */

static int
h8300_shift_costs (rtx x)
{
  rtx operands[4];

  if (GET_MODE (x) != QImode
      && GET_MODE (x) != HImode
      && GET_MODE (x) != SImode)
    return 100;

  operands[0] = NULL;
  operands[1] = NULL;
  operands[2] = XEXP (x, 1);
  operands[3] = x;
  return compute_a_shift_length (NULL, operands) / 2;
}

/* Worker function for TARGET_RTX_COSTS.  */

static bool
h8300_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  if (TARGET_H8300SX && outer_code == MEM)
    {
      /* Estimate the number of execution states needed to calculate
	 the address.  */
      if (register_operand (x, VOIDmode)
	  || GET_CODE (x) == POST_INC
	  || GET_CODE (x) == POST_DEC
	  || CONSTANT_P (x))
	*total = 0;
      else
	*total = COSTS_N_INSNS (1);
      return true;
    }

  switch (code)
    {
    case CONST_INT:
      {
	HOST_WIDE_INT n = INTVAL (x);

	if (TARGET_H8300SX)
	  {
	    /* Constant operands need the same number of processor
	       states as register operands.  Although we could try to
	       use a size-based cost for optimize_size, the lack of
	       of a mode makes the results very unpredictable.  */
	    *total = 0;
	    return true;
	  }
	if (-4 <= n || n <= 4)
	  {
	    switch ((int) n)
	      {
	      case 0:
		*total = 0;
		return true;
	      case 1:
	      case 2:
	      case -1:
	      case -2:
		*total = 0 + (outer_code == SET);
		return true;
	      case 4:
	      case -4:
		if (TARGET_H8300H || TARGET_H8300S)
		  *total = 0 + (outer_code == SET);
		else
		  *total = 1;
		return true;
	      }
	  }
	*total = 1;
	return true;
      }

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      if (TARGET_H8300SX)
	{
	  /* See comment for CONST_INT.  */
	  *total = 0;
	  return true;
	}
      *total = 3;
      return true;

    case CONST_DOUBLE:
      *total = 20;
      return true;

    case AND:
      if (!h8300_dst_operand (XEXP (x, 0), VOIDmode)
	  || !h8300_src_operand (XEXP (x, 1), VOIDmode))
	return false;
      *total = COSTS_N_INSNS (h8300_and_costs (x));
      return true;

    /* We say that MOD and DIV are so expensive because otherwise we'll
       generate some really horrible code for division of a power of two.  */
    case MOD:
    case DIV:
    case UMOD:
    case UDIV:
      if (TARGET_H8300SX)
	switch (GET_MODE (x))
	  {
	  case QImode:
	  case HImode:
	    *total = COSTS_N_INSNS (optimize_size ? 4 : 10);
	    return false;

	  case SImode:
	    *total = COSTS_N_INSNS (optimize_size ? 4 : 18);
	    return false;

	  default:
	    break;
	  }
      *total = COSTS_N_INSNS (12);
      return true;

    case MULT:
      if (TARGET_H8300SX)
	switch (GET_MODE (x))
	  {
	  case QImode:
	  case HImode:
	    *total = COSTS_N_INSNS (2);
	    return false;

	  case SImode:
	    *total = COSTS_N_INSNS (5);
	    return false;

	  default:
	    break;
	  }
      *total = COSTS_N_INSNS (4);
      return true;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (h8sx_binary_shift_operator (x, VOIDmode))
	{
	  *total = COSTS_N_INSNS (2);
	  return false;
	}
      else if (h8sx_unary_shift_operator (x, VOIDmode))
	{
	  *total = COSTS_N_INSNS (1);
	  return false;
	}
      *total = COSTS_N_INSNS (h8300_shift_costs (x));
      return true;

    case ROTATE:
    case ROTATERT:
      if (GET_MODE (x) == HImode)
	*total = 2;
      else
	*total = 8;
      return true;

    default:
      *total = COSTS_N_INSNS (1);
      return false;
    }
}

/* Documentation for the machine specific operand escapes:

   'E' like s but negative.
   'F' like t but negative.
   'G' constant just the negative
   'R' print operand as a byte:8 address if appropriate, else fall back to
       'X' handling.
   'S' print operand as a long word
   'T' print operand as a word
   'V' find the set bit, and print its number.
   'W' find the clear bit, and print its number.
   'X' print operand as a byte
   'Y' print either l or h depending on whether last 'Z' operand < 8 or >= 8.
       If this operand isn't a register, fall back to 'R' handling.
   'Z' print int & 7.
   'c' print the opcode corresponding to rtl
   'e' first word of 32 bit value - if reg, then least reg. if mem
       then least. if const then most sig word
   'f' second word of 32 bit value - if reg, then biggest reg. if mem
       then +2. if const then least sig word
   'j' print operand as condition code.
   'k' print operand as reverse condition code.
   'm' convert an integer operand to a size suffix (.b, .w or .l)
   'o' print an integer without a leading '#'
   's' print as low byte of 16 bit value
   't' print as high byte of 16 bit value
   'w' print as low byte of 32 bit value
   'x' print as 2nd byte of 32 bit value
   'y' print as 3rd byte of 32 bit value
   'z' print as msb of 32 bit value
*/

/* Return assembly language string which identifies a comparison type.  */

static const char *
cond_string (enum rtx_code code)
{
  switch (code)
    {
    case NE:
      return "ne";
    case EQ:
      return "eq";
    case GE:
      return "ge";
    case GT:
      return "gt";
    case LE:
      return "le";
    case LT:
      return "lt";
    case GEU:
      return "hs";
    case GTU:
      return "hi";
    case LEU:
      return "ls";
    case LTU:
      return "lo";
    default:
      gcc_unreachable ();
    }
}

/* Print operand X using operand code CODE to assembly language output file
   FILE.  */

void
print_operand (FILE *file, rtx x, int code)
{
  /* This is used for communication between codes V,W,Z and Y.  */
  static int bitint;

  switch (code)
    {
    case 'E':
      switch (GET_CODE (x))
	{
	case REG:
	  fprintf (file, "%sl", names_big[REGNO (x)]);
	  break;
	case CONST_INT:
	  fprintf (file, "#%ld", (-INTVAL (x)) & 0xff);
	  break;
	default:
	  gcc_unreachable ();
	}
      break;
    case 'F':
      switch (GET_CODE (x))
	{
	case REG:
	  fprintf (file, "%sh", names_big[REGNO (x)]);
	  break;
	case CONST_INT:
	  fprintf (file, "#%ld", ((-INTVAL (x)) & 0xff00) >> 8);
	  break;
	default:
	  gcc_unreachable ();
	}
      break;
    case 'G':
      gcc_assert (GET_CODE (x) == CONST_INT);
      fprintf (file, "#%ld", 0xff & (-INTVAL (x)));
      break;
    case 'S':
      if (GET_CODE (x) == REG)
	fprintf (file, "%s", names_extended[REGNO (x)]);
      else
	goto def;
      break;
    case 'T':
      if (GET_CODE (x) == REG)
	fprintf (file, "%s", names_big[REGNO (x)]);
      else
	goto def;
      break;
    case 'V':
      bitint = exact_log2 (INTVAL (x) & 0xff);
      gcc_assert (bitint >= 0);
      fprintf (file, "#%d", bitint);
      break;
    case 'W':
      bitint = exact_log2 ((~INTVAL (x)) & 0xff);
      gcc_assert (bitint >= 0);
      fprintf (file, "#%d", bitint);
      break;
    case 'R':
    case 'X':
      if (GET_CODE (x) == REG)
	fprintf (file, "%s", byte_reg (x, 0));
      else
	goto def;
      break;
    case 'Y':
      gcc_assert (bitint >= 0);
      if (GET_CODE (x) == REG)
	fprintf (file, "%s%c", names_big[REGNO (x)], bitint > 7 ? 'h' : 'l');
      else
	print_operand (file, x, 'R');
      bitint = -1;
      break;
    case 'Z':
      bitint = INTVAL (x);
      fprintf (file, "#%d", bitint & 7);
      break;
    case 'c':
      switch (GET_CODE (x))
	{
	case IOR:
	  fprintf (file, "or");
	  break;
	case XOR:
	  fprintf (file, "xor");
	  break;
	case AND:
	  fprintf (file, "and");
	  break;
	default:
	  break;
	}
      break;
    case 'e':
      switch (GET_CODE (x))
	{
	case REG:
	  if (TARGET_H8300)
	    fprintf (file, "%s", names_big[REGNO (x)]);
	  else
	    fprintf (file, "%s", names_upper_extended[REGNO (x)]);
	  break;
	case MEM:
	  print_operand (file, x, 0);
	  break;
	case CONST_INT:
	  fprintf (file, "#%ld", ((INTVAL (x) >> 16) & 0xffff));
	  break;
	case CONST_DOUBLE:
	  {
	    long val;
	    REAL_VALUE_TYPE rv;
	    REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
	    REAL_VALUE_TO_TARGET_SINGLE (rv, val);
	    fprintf (file, "#%ld", ((val >> 16) & 0xffff));
	    break;
	  }
	default:
	  gcc_unreachable ();
	  break;
	}
      break;
    case 'f':
      switch (GET_CODE (x))
	{
	case REG:
	  if (TARGET_H8300)
	    fprintf (file, "%s", names_big[REGNO (x) + 1]);
	  else
	    fprintf (file, "%s", names_big[REGNO (x)]);
	  break;
	case MEM:
	  x = adjust_address (x, HImode, 2);
	  print_operand (file, x, 0);
	  break;
	case CONST_INT:
	  fprintf (file, "#%ld", INTVAL (x) & 0xffff);
	  break;
	case CONST_DOUBLE:
	  {
	    long val;
	    REAL_VALUE_TYPE rv;
	    REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
	    REAL_VALUE_TO_TARGET_SINGLE (rv, val);
	    fprintf (file, "#%ld", (val & 0xffff));
	    break;
	  }
	default:
	  gcc_unreachable ();
	}
      break;
    case 'j':
      fputs (cond_string (GET_CODE (x)), file);
      break;
    case 'k':
      fputs (cond_string (reverse_condition (GET_CODE (x))), file);
      break;
    case 'm':
      gcc_assert (GET_CODE (x) == CONST_INT);
      switch (INTVAL (x))
	{
	case 1:
	  fputs (".b", file);
	  break;

	case 2:
	  fputs (".w", file);
	  break;

	case 4:
	  fputs (".l", file);
	  break;

	default:
	  gcc_unreachable ();
	}
      break;
    case 'o':
      print_operand_address (file, x);
      break;
    case 's':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "#%ld", (INTVAL (x)) & 0xff);
      else
	fprintf (file, "%s", byte_reg (x, 0));
      break;
    case 't':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "#%ld", (INTVAL (x) >> 8) & 0xff);
      else
	fprintf (file, "%s", byte_reg (x, 1));
      break;
    case 'w':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "#%ld", INTVAL (x) & 0xff);
      else
	fprintf (file, "%s",
		 byte_reg (x, TARGET_H8300 ? 2 : 0));
      break;
    case 'x':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "#%ld", (INTVAL (x) >> 8) & 0xff);
      else
	fprintf (file, "%s",
		 byte_reg (x, TARGET_H8300 ? 3 : 1));
      break;
    case 'y':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "#%ld", (INTVAL (x) >> 16) & 0xff);
      else
	fprintf (file, "%s", byte_reg (x, 0));
      break;
    case 'z':
      if (GET_CODE (x) == CONST_INT)
	fprintf (file, "#%ld", (INTVAL (x) >> 24) & 0xff);
      else
	fprintf (file, "%s", byte_reg (x, 1));
      break;

    default:
    def:
      switch (GET_CODE (x))
	{
	case REG:
	  switch (GET_MODE (x))
	    {
	    case QImode:
#if 0 /* Is it asm ("mov.b %0,r2l", ...) */
	      fprintf (file, "%s", byte_reg (x, 0));
#else /* ... or is it asm ("mov.b %0l,r2l", ...) */
	      fprintf (file, "%s", names_big[REGNO (x)]);
#endif
	      break;
	    case HImode:
	      fprintf (file, "%s", names_big[REGNO (x)]);
	      break;
	    case SImode:
	    case SFmode:
	      fprintf (file, "%s", names_extended[REGNO (x)]);
	      break;
	    default:
	      gcc_unreachable ();
	    }
	  break;

	case MEM:
	  {
	    rtx addr = XEXP (x, 0);

	    fprintf (file, "@");
	    output_address (addr);

	    /* Add a length suffix to constant addresses.  Although this
	       is often unnecessary, it helps to avoid ambiguity in the
	       syntax of mova.  If we wrote an insn like:

		    mova/w.l @(1,@foo.b),er0

	       then .b would be considered part of the symbol name.
	       Adding a length after foo will avoid this.  */
	    if (CONSTANT_P (addr))
	      switch (code)
		{
		case 'R':
		  /* Used for mov.b and bit operations.  */
		  if (h8300_eightbit_constant_address_p (addr))
		    {
		      fprintf (file, ":8");
		      break;
		    }

		  /* Fall through.  We should not get here if we are
		     processing bit operations on H8/300 or H8/300H
		     because 'U' constraint does not allow bit
		     operations on the tiny area on these machines.  */

		case 'X':
		case 'T':
		case 'S':
		  if (h8300_constant_length (addr) == 2)
		    fprintf (file, ":16");
		  else
		    fprintf (file, ":32");
		  break;
		default:
		  break;
		}
	  }
	  break;

	case CONST_INT:
	case SYMBOL_REF:
	case CONST:
	case LABEL_REF:
	  fprintf (file, "#");
	  print_operand_address (file, x);
	  break;
	case CONST_DOUBLE:
	  {
	    long val;
	    REAL_VALUE_TYPE rv;
	    REAL_VALUE_FROM_CONST_DOUBLE (rv, x);
	    REAL_VALUE_TO_TARGET_SINGLE (rv, val);
	    fprintf (file, "#%ld", val);
	    break;
	  }
	default:
	  break;
	}
    }
}

/* Output assembly language output for the address ADDR to FILE.  */

void
print_operand_address (FILE *file, rtx addr)
{
  rtx index;
  int size;

  switch (GET_CODE (addr))
    {
    case REG:
      fprintf (file, "%s", h8_reg_names[REGNO (addr)]);
      break;

    case PRE_DEC:
      fprintf (file, "-%s", h8_reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_INC:
      fprintf (file, "%s+", h8_reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case PRE_INC:
      fprintf (file, "+%s", h8_reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_DEC:
      fprintf (file, "%s-", h8_reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case PLUS:
      fprintf (file, "(");

      index = h8300_get_index (XEXP (addr, 0), VOIDmode, &size);
      if (GET_CODE (index) == REG)
	{
	  /* reg,foo */
	  print_operand_address (file, XEXP (addr, 1));
	  fprintf (file, ",");
	  switch (size)
	    {
	    case 0:
	      print_operand_address (file, index);
	      break;

	    case 1:
	      print_operand (file, index, 'X');
	      fputs (".b", file);
	      break;

	    case 2:
	      print_operand (file, index, 'T');
	      fputs (".w", file);
	      break;

	    case 4:
	      print_operand (file, index, 'S');
	      fputs (".l", file);
	      break;
	    }
	  /* print_operand_address (file, XEXP (addr, 0)); */
	}
      else
	{
	  /* foo+k */
	  print_operand_address (file, XEXP (addr, 0));
	  fprintf (file, "+");
	  print_operand_address (file, XEXP (addr, 1));
	}
      fprintf (file, ")");
      break;

    case CONST_INT:
      {
	/* Since the H8/300 only has 16 bit pointers, negative values are also
	   those >= 32768.  This happens for example with pointer minus a
	   constant.  We don't want to turn (char *p - 2) into
	   (char *p + 65534) because loop unrolling can build upon this
	   (IE: char *p + 131068).  */
	int n = INTVAL (addr);
	if (TARGET_H8300)
	  n = (int) (short) n;
	fprintf (file, "%d", n);
	break;
      }

    default:
      output_addr_const (file, addr);
      break;
    }
}

/* Output all insn addresses and their sizes into the assembly language
   output file.  This is helpful for debugging whether the length attributes
   in the md file are correct.  This is not meant to be a user selectable
   option.  */

void
final_prescan_insn (rtx insn, rtx *operand ATTRIBUTE_UNUSED,
		    int num_operands ATTRIBUTE_UNUSED)
{
  /* This holds the last insn address.  */
  static int last_insn_address = 0;

  const int uid = INSN_UID (insn);

  if (TARGET_ADDRESSES)
    {
      fprintf (asm_out_file, "; 0x%x %d\n", INSN_ADDRESSES (uid),
	       INSN_ADDRESSES (uid) - last_insn_address);
      last_insn_address = INSN_ADDRESSES (uid);
    }
}

/* Prepare for an SI sized move.  */

int
h8300_expand_movsi (rtx operands[])
{
  rtx src = operands[1];
  rtx dst = operands[0];
  if (!reload_in_progress && !reload_completed)
    {
      if (!register_operand (dst, GET_MODE (dst)))
	{
	  rtx tmp = gen_reg_rtx (GET_MODE (dst));
	  emit_move_insn (tmp, src);
	  operands[1] = tmp;
	}
    }
  return 0;
}

/* Function for INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET).
   Define the offset between two registers, one to be eliminated, and
   the other its replacement, at the start of a routine.  */

int
h8300_initial_elimination_offset (int from, int to)
{
  /* The number of bytes that the return address takes on the stack.  */
  int pc_size = POINTER_SIZE / BITS_PER_UNIT;

  /* The number of bytes that the saved frame pointer takes on the stack.  */
  int fp_size = frame_pointer_needed * UNITS_PER_WORD;

  /* The number of bytes that the saved registers, excluding the frame
     pointer, take on the stack.  */
  int saved_regs_size = 0;

  /* The number of bytes that the locals takes on the stack.  */
  int frame_size = round_frame_size (get_frame_size ());

  int regno;

  for (regno = 0; regno <= HARD_FRAME_POINTER_REGNUM; regno++)
    if (WORD_REG_USED (regno))
      saved_regs_size += UNITS_PER_WORD;

  /* Adjust saved_regs_size because the above loop took the frame
     pointer int account.  */
  saved_regs_size -= fp_size;

  switch (to)
    {
    case HARD_FRAME_POINTER_REGNUM:
      switch (from)
	{
	case ARG_POINTER_REGNUM:
	  return pc_size + fp_size;
	case RETURN_ADDRESS_POINTER_REGNUM:
	  return fp_size;
	case FRAME_POINTER_REGNUM:
	  return -saved_regs_size;
	default:
	  gcc_unreachable ();
	}
      break;
    case STACK_POINTER_REGNUM:
      switch (from)
	{
	case ARG_POINTER_REGNUM:
	  return pc_size + saved_regs_size + frame_size;
	case RETURN_ADDRESS_POINTER_REGNUM:
	  return saved_regs_size + frame_size;
	case FRAME_POINTER_REGNUM:
	  return frame_size;
	default:
	  gcc_unreachable ();
	}
      break;
    default:
      gcc_unreachable ();
    }
  gcc_unreachable ();
}

/* Worker function for RETURN_ADDR_RTX.  */

rtx
h8300_return_addr_rtx (int count, rtx frame)
{
  rtx ret;

  if (count == 0)
    ret = gen_rtx_MEM (Pmode,
		       gen_rtx_REG (Pmode, RETURN_ADDRESS_POINTER_REGNUM));
  else if (flag_omit_frame_pointer)
    return (rtx) 0;
  else
    ret = gen_rtx_MEM (Pmode,
		       memory_address (Pmode,
				       plus_constant (frame, UNITS_PER_WORD)));
  set_mem_alias_set (ret, get_frame_alias_set ());
  return ret;
}

/* Update the condition code from the insn.  */

void
notice_update_cc (rtx body, rtx insn)
{
  rtx set;

  switch (get_attr_cc (insn))
    {
    case CC_NONE:
      /* Insn does not affect CC at all.  */
      break;

    case CC_NONE_0HIT:
      /* Insn does not change CC, but the 0'th operand has been changed.  */
      if (cc_status.value1 != 0
	  && reg_overlap_mentioned_p (recog_data.operand[0], cc_status.value1))
	cc_status.value1 = 0;
      if (cc_status.value2 != 0
	  && reg_overlap_mentioned_p (recog_data.operand[0], cc_status.value2))
	cc_status.value2 = 0;
      break;

    case CC_SET_ZN:
      /* Insn sets the Z,N flags of CC to recog_data.operand[0].
	 The V flag is unusable.  The C flag may or may not be known but
	 that's ok because alter_cond will change tests to use EQ/NE.  */
      CC_STATUS_INIT;
      cc_status.flags |= CC_OVERFLOW_UNUSABLE | CC_NO_CARRY;
      set = single_set (insn);
      cc_status.value1 = SET_SRC (set);
      if (SET_DEST (set) != cc0_rtx)
	cc_status.value2 = SET_DEST (set);
      break;

    case CC_SET_ZNV:
      /* Insn sets the Z,N,V flags of CC to recog_data.operand[0].
	 The C flag may or may not be known but that's ok because
	 alter_cond will change tests to use EQ/NE.  */
      CC_STATUS_INIT;
      cc_status.flags |= CC_NO_CARRY;
      set = single_set (insn);
      cc_status.value1 = SET_SRC (set);
      if (SET_DEST (set) != cc0_rtx)
	{
	  /* If the destination is STRICT_LOW_PART, strip off
	     STRICT_LOW_PART.  */
	  if (GET_CODE (SET_DEST (set)) == STRICT_LOW_PART)
	    cc_status.value2 = XEXP (SET_DEST (set), 0);
	  else
	    cc_status.value2 = SET_DEST (set);
	}
      break;

    case CC_COMPARE:
      /* The insn is a compare instruction.  */
      CC_STATUS_INIT;
      cc_status.value1 = SET_SRC (body);
      break;

    case CC_CLOBBER:
      /* Insn doesn't leave CC in a usable state.  */
      CC_STATUS_INIT;
      break;
    }
}

/* Given that X occurs in an address of the form (plus X constant),
   return the part of X that is expected to be a register.  There are
   four kinds of addressing mode to recognize:

	@(dd,Rn)
	@(dd,RnL.b)
	@(dd,Rn.w)
	@(dd,ERn.l)

   If SIZE is nonnull, and the address is one of the last three forms,
   set *SIZE to the index multiplication factor.  Set it to 0 for
   plain @(dd,Rn) addresses.

   MODE is the mode of the value being accessed.  It can be VOIDmode
   if the address is known to be valid, but its mode is unknown.  */

rtx
h8300_get_index (rtx x, enum machine_mode mode, int *size)
{
  int dummy, factor;

  if (size == 0)
    size = &dummy;

  factor = (mode == VOIDmode ? 0 : GET_MODE_SIZE (mode));
  if (TARGET_H8300SX
      && factor <= 4
      && (mode == VOIDmode
	  || GET_MODE_CLASS (mode) == MODE_INT
	  || GET_MODE_CLASS (mode) == MODE_FLOAT))
    {
      if (factor <= 1 && GET_CODE (x) == ZERO_EXTEND)
	{
	  /* When accessing byte-sized values, the index can be
	     a zero-extended QImode or HImode register.  */
	  *size = GET_MODE_SIZE (GET_MODE (XEXP (x, 0)));
	  return XEXP (x, 0);
	}
      else
	{
	  /* We're looking for addresses of the form:

		 (mult X I)
	      or (mult (zero_extend X) I)

	     where I is the size of the operand being accessed.
	     The canonical form of the second expression is:

		 (and (mult (subreg X) I) J)

	     where J == GET_MODE_MASK (GET_MODE (X)) * I.  */
	  rtx index;

	  if (GET_CODE (x) == AND
	      && GET_CODE (XEXP (x, 1)) == CONST_INT
	      && (factor == 0
		  || INTVAL (XEXP (x, 1)) == 0xff * factor
		  || INTVAL (XEXP (x, 1)) == 0xffff * factor))
	    {
	      index = XEXP (x, 0);
	      *size = (INTVAL (XEXP (x, 1)) >= 0xffff ? 2 : 1);
	    }
	  else
	    {
	      index = x;
	      *size = 4;
	    }

	  if (GET_CODE (index) == MULT
	      && GET_CODE (XEXP (index, 1)) == CONST_INT
	      && (factor == 0 || factor == INTVAL (XEXP (index, 1))))
	    return XEXP (index, 0);
	}
    }
  *size = 0;
  return x;
}

static const h8300_length_table addb_length_table =
{
  /* #xx  Rs   @aa  @Rs  @xx  */
  {  2,   2,   4,   4,   4  }, /* add.b xx,Rd  */
  {  4,   4,   4,   4,   6  }, /* add.b xx,@aa */
  {  4,   4,   4,   4,   6  }, /* add.b xx,@Rd */
  {  6,   4,   4,   4,   6  }  /* add.b xx,@xx */
};

static const h8300_length_table addw_length_table =
{
  /* #xx  Rs   @aa  @Rs  @xx  */
  {  2,   2,   4,   4,   4  }, /* add.w xx,Rd  */
  {  4,   4,   4,   4,   6  }, /* add.w xx,@aa */
  {  4,   4,   4,   4,   6  }, /* add.w xx,@Rd */
  {  4,   4,   4,   4,   6  }  /* add.w xx,@xx */
};

static const h8300_length_table addl_length_table =
{
  /* #xx  Rs   @aa  @Rs  @xx  */
  {  2,   2,   4,   4,   4  }, /* add.l xx,Rd  */
  {  4,   4,   6,   6,   6  }, /* add.l xx,@aa */
  {  4,   4,   6,   6,   6  }, /* add.l xx,@Rd */
  {  4,   4,   6,   6,   6  }  /* add.l xx,@xx */
};

#define logicb_length_table addb_length_table
#define logicw_length_table addw_length_table

static const h8300_length_table logicl_length_table =
{
  /* #xx  Rs   @aa  @Rs  @xx  */
  {  2,   4,   4,   4,   4  }, /* and.l xx,Rd  */
  {  4,   4,   6,   6,   6  }, /* and.l xx,@aa */
  {  4,   4,   6,   6,   6  }, /* and.l xx,@Rd */
  {  4,   4,   6,   6,   6  }  /* and.l xx,@xx */
};

static const h8300_length_table movb_length_table =
{
  /* #xx  Rs   @aa  @Rs  @xx  */
  {  2,   2,   2,   2,   4  }, /* mov.b xx,Rd  */
  {  4,   2,   4,   4,   4  }, /* mov.b xx,@aa */
  {  4,   2,   4,   4,   4  }, /* mov.b xx,@Rd */
  {  4,   4,   4,   4,   4  }  /* mov.b xx,@xx */
};

#define movw_length_table movb_length_table

static const h8300_length_table movl_length_table =
{
  /* #xx  Rs   @aa  @Rs  @xx  */
  {  2,   2,   4,   4,   4  }, /* mov.l xx,Rd  */
  {  4,   4,   4,   4,   4  }, /* mov.l xx,@aa */
  {  4,   4,   4,   4,   4  }, /* mov.l xx,@Rd */
  {  4,   4,   4,   4,   4  }  /* mov.l xx,@xx */
};

/* Return the size of the given address or displacement constant.  */

static unsigned int
h8300_constant_length (rtx constant)
{
  /* Check for (@d:16,Reg).  */
  if (GET_CODE (constant) == CONST_INT
      && IN_RANGE (INTVAL (constant), -0x8000, 0x7fff))
    return 2;

  /* Check for (@d:16,Reg) in cases where the displacement is
     an absolute address.  */
  if (Pmode == HImode || h8300_tiny_constant_address_p (constant))
    return 2;

  return 4;
}

/* Return the size of a displacement field in address ADDR, which should
   have the form (plus X constant).  SIZE is the number of bytes being
   accessed.  */

static unsigned int
h8300_displacement_length (rtx addr, int size)
{
  rtx offset;

  offset = XEXP (addr, 1);

  /* Check for @(d:2,Reg).  */
  if (register_operand (XEXP (addr, 0), VOIDmode)
      && GET_CODE (offset) == CONST_INT
      && (INTVAL (offset) == size
	  || INTVAL (offset) == size * 2
	  || INTVAL (offset) == size * 3))
    return 0;

  return h8300_constant_length (offset);
}

/* Store the class of operand OP in *CLASS and return the length of any
   extra operand fields.  SIZE is the number of bytes in OP.  CLASS
   can be null if only the length is needed.  */

static unsigned int
h8300_classify_operand (rtx op, int size, enum h8300_operand_class *class)
{
  enum h8300_operand_class dummy;

  if (class == 0)
    class = &dummy;

  if (CONSTANT_P (op))
    {
      *class = H8OP_IMMEDIATE;

      /* Byte-sized immediates are stored in the opcode fields.  */
      if (size == 1)
	return 0;

      /* If this is a 32-bit instruction, see whether the constant
	 will fit into a 16-bit immediate field.  */
      if (TARGET_H8300SX
	  && size == 4
	  && GET_CODE (op) == CONST_INT
	  && IN_RANGE (INTVAL (op), 0, 0xffff))
	return 2;

      return size;
    }
  else if (GET_CODE (op) == MEM)
    {
      op = XEXP (op, 0);
      if (CONSTANT_P (op))
	{
	  *class = H8OP_MEM_ABSOLUTE;
	  return h8300_constant_length (op);
	}
      else if (GET_CODE (op) == PLUS && CONSTANT_P (XEXP (op, 1)))
	{
	  *class = H8OP_MEM_COMPLEX;
	  return h8300_displacement_length (op, size);
	}
      else if (GET_RTX_CLASS (GET_CODE (op)) == RTX_AUTOINC)
	{
	  *class = H8OP_MEM_COMPLEX;
	  return 0;
	}
      else if (register_operand (op, VOIDmode))
	{
	  *class = H8OP_MEM_BASE;
	  return 0;
	}
    }
  gcc_assert (register_operand (op, VOIDmode));
  *class = H8OP_REGISTER;
  return 0;
}

/* Return the length of the instruction described by TABLE given that
   its operands are OP1 and OP2.  OP1 must be an h8300_dst_operand
   and OP2 must be an h8300_src_operand.  */

static unsigned int
h8300_length_from_table (rtx op1, rtx op2, const h8300_length_table *table)
{
  enum h8300_operand_class op1_class, op2_class;
  unsigned int size, immediate_length;

  size = GET_MODE_SIZE (GET_MODE (op1));
  immediate_length = (h8300_classify_operand (op1, size, &op1_class)
		      + h8300_classify_operand (op2, size, &op2_class));
  return immediate_length + (*table)[op1_class - 1][op2_class];
}

/* Return the length of a unary instruction such as neg or not given that
   its operand is OP.  */

unsigned int
h8300_unary_length (rtx op)
{
  enum h8300_operand_class class;
  unsigned int size, operand_length;

  size = GET_MODE_SIZE (GET_MODE (op));
  operand_length = h8300_classify_operand (op, size, &class);
  switch (class)
    {
    case H8OP_REGISTER:
      return 2;

    case H8OP_MEM_BASE:
      return (size == 4 ? 6 : 4);

    case H8OP_MEM_ABSOLUTE:
      return operand_length + (size == 4 ? 6 : 4);

    case H8OP_MEM_COMPLEX:
      return operand_length + 6;

    default:
      gcc_unreachable ();
    }
}

/* Likewise short immediate instructions such as add.w #xx:3,OP.  */

static unsigned int
h8300_short_immediate_length (rtx op)
{
  enum h8300_operand_class class;
  unsigned int size, operand_length;

  size = GET_MODE_SIZE (GET_MODE (op));
  operand_length = h8300_classify_operand (op, size, &class);

  switch (class)
    {
    case H8OP_REGISTER:
      return 2;

    case H8OP_MEM_BASE:
    case H8OP_MEM_ABSOLUTE:
    case H8OP_MEM_COMPLEX:
      return 4 + operand_length;

    default:
      gcc_unreachable ();
    }
}

/* Likewise bitfield load and store instructions.  */

static unsigned int
h8300_bitfield_length (rtx op, rtx op2)
{
  enum h8300_operand_class class;
  unsigned int size, operand_length;

  if (GET_CODE (op) == REG)
    op = op2;
  gcc_assert (GET_CODE (op) != REG);
  
  size = GET_MODE_SIZE (GET_MODE (op));
  operand_length = h8300_classify_operand (op, size, &class);

  switch (class)
    {
    case H8OP_MEM_BASE:
    case H8OP_MEM_ABSOLUTE:
    case H8OP_MEM_COMPLEX:
      return 4 + operand_length;

    default:
      gcc_unreachable ();
    }
}

/* Calculate the length of general binary instruction INSN using TABLE.  */

static unsigned int
h8300_binary_length (rtx insn, const h8300_length_table *table)
{
  rtx set;

  set = single_set (insn);
  gcc_assert (set);

  if (BINARY_P (SET_SRC (set)))
    return h8300_length_from_table (XEXP (SET_SRC (set), 0),
				    XEXP (SET_SRC (set), 1), table);
  else
    {
      gcc_assert (GET_RTX_CLASS (GET_CODE (SET_SRC (set))) == RTX_TERNARY);
      return h8300_length_from_table (XEXP (XEXP (SET_SRC (set), 1), 0),
				      XEXP (XEXP (SET_SRC (set), 1), 1),
				      table);
    }
}

/* Subroutine of h8300_move_length.  Return true if OP is 1- or 2-byte
   memory reference and either (1) it has the form @(d:16,Rn) or
   (2) its address has the code given by INC_CODE.  */

static bool
h8300_short_move_mem_p (rtx op, enum rtx_code inc_code)
{
  rtx addr;
  unsigned int size;

  if (GET_CODE (op) != MEM)
    return false;

  addr = XEXP (op, 0);
  size = GET_MODE_SIZE (GET_MODE (op));
  if (size != 1 && size != 2)
    return false;

  return (GET_CODE (addr) == inc_code
	  || (GET_CODE (addr) == PLUS
	      && GET_CODE (XEXP (addr, 0)) == REG
	      && h8300_displacement_length (addr, size) == 2));
}

/* Calculate the length of move instruction INSN using the given length
   table.  Although the tables are correct for most cases, there is some
   irregularity in the length of mov.b and mov.w.  The following forms:

	mov @ERs+, Rd
	mov @(d:16,ERs), Rd
	mov Rs, @-ERd
	mov Rs, @(d:16,ERd)

   are two bytes shorter than most other "mov Rs, @complex" or
   "mov @complex,Rd" combinations.  */

static unsigned int
h8300_move_length (rtx *operands, const h8300_length_table *table)
{
  unsigned int size;

  size = h8300_length_from_table (operands[0], operands[1], table);
  if (REG_P (operands[0]) && h8300_short_move_mem_p (operands[1], POST_INC))
    size -= 2;
  if (REG_P (operands[1]) && h8300_short_move_mem_p (operands[0], PRE_DEC))
    size -= 2;
  return size;
}

/* Return the length of a mova instruction with the given operands.
   DEST is the register destination, SRC is the source address and
   OFFSET is the 16-bit or 32-bit displacement.  */

static unsigned int
h8300_mova_length (rtx dest, rtx src, rtx offset)
{
  unsigned int size;

  size = (2
	  + h8300_constant_length (offset)
	  + h8300_classify_operand (src, GET_MODE_SIZE (GET_MODE (src)), 0));
  if (!REG_P (dest) || !REG_P (src) || REGNO (src) != REGNO (dest))
    size += 2;
  return size;
}

/* Compute the length of INSN based on its length_table attribute.
   OPERANDS is the array of its operands.  */

unsigned int
h8300_insn_length_from_table (rtx insn, rtx * operands)
{
  switch (get_attr_length_table (insn))
    {
    case LENGTH_TABLE_NONE:
      gcc_unreachable ();

    case LENGTH_TABLE_ADDB:
      return h8300_binary_length (insn, &addb_length_table);

    case LENGTH_TABLE_ADDW:
      return h8300_binary_length (insn, &addw_length_table);

    case LENGTH_TABLE_ADDL:
      return h8300_binary_length (insn, &addl_length_table);

    case LENGTH_TABLE_LOGICB:
      return h8300_binary_length (insn, &logicb_length_table);

    case LENGTH_TABLE_MOVB:
      return h8300_move_length (operands, &movb_length_table);

    case LENGTH_TABLE_MOVW:
      return h8300_move_length (operands, &movw_length_table);

    case LENGTH_TABLE_MOVL:
      return h8300_move_length (operands, &movl_length_table);

    case LENGTH_TABLE_MOVA:
      return h8300_mova_length (operands[0], operands[1], operands[2]);

    case LENGTH_TABLE_MOVA_ZERO:
      return h8300_mova_length (operands[0], operands[1], const0_rtx);

    case LENGTH_TABLE_UNARY:
      return h8300_unary_length (operands[0]);

    case LENGTH_TABLE_MOV_IMM4:
      return 2 + h8300_classify_operand (operands[0], 0, 0);

    case LENGTH_TABLE_SHORT_IMMEDIATE:
      return h8300_short_immediate_length (operands[0]);

    case LENGTH_TABLE_BITFIELD:
      return h8300_bitfield_length (operands[0], operands[1]);
      
    case LENGTH_TABLE_BITBRANCH:
      return h8300_bitfield_length (operands[1], operands[2]) - 2;

    default:
      gcc_unreachable ();
    }
}

/* Return true if LHS and RHS are memory references that can be mapped
   to the same h8sx assembly operand.  LHS appears as the destination of
   an instruction and RHS appears as a source.

   Three cases are allowed:

	- RHS is @+Rn or @-Rn, LHS is @Rn
	- RHS is @Rn, LHS is @Rn+ or @Rn-
	- RHS and LHS have the same address and neither has side effects.  */

bool
h8sx_mergeable_memrefs_p (rtx lhs, rtx rhs)
{
  if (GET_CODE (rhs) == MEM && GET_CODE (lhs) == MEM)
    {
      rhs = XEXP (rhs, 0);
      lhs = XEXP (lhs, 0);

      if (GET_CODE (rhs) == PRE_INC || GET_CODE (rhs) == PRE_DEC)
	return rtx_equal_p (XEXP (rhs, 0), lhs);

      if (GET_CODE (lhs) == POST_INC || GET_CODE (lhs) == POST_DEC)
	return rtx_equal_p (rhs, XEXP (lhs, 0));

      if (rtx_equal_p (rhs, lhs))
	return true;
    }
  return false;
}

/* Return true if OPERANDS[1] can be mapped to the same assembly
   operand as OPERANDS[0].  */

bool
h8300_operands_match_p (rtx *operands)
{
  if (register_operand (operands[0], VOIDmode)
      && register_operand (operands[1], VOIDmode))
    return true;

  if (h8sx_mergeable_memrefs_p (operands[0], operands[1]))
    return true;

  return false;
}

/* Try using movmd to move LENGTH bytes from memory region SRC to memory
   region DEST.  The two regions do not overlap and have the common
   alignment given by ALIGNMENT.  Return true on success.

   Using movmd for variable-length moves seems to involve some
   complex trade-offs.  For instance:

      - Preparing for a movmd instruction is similar to preparing
	for a memcpy.  The main difference is that the arguments
	are moved into er4, er5 and er6 rather than er0, er1 and er2.

      - Since movmd clobbers the frame pointer, we need to save
	and restore it somehow when frame_pointer_needed.  This can
	sometimes make movmd sequences longer than calls to memcpy().

      - The counter register is 16 bits, so the instruction is only
	suitable for variable-length moves when sizeof (size_t) == 2.
	That's only true in normal mode.

      - We will often lack static alignment information.  Falling back
	on movmd.b would likely be slower than calling memcpy(), at least
	for big moves.

   This function therefore only uses movmd when the length is a
   known constant, and only then if -fomit-frame-pointer is in
   effect or if we're not optimizing for size.

   At the moment the function uses movmd for all in-range constants,
   but it might be better to fall back on memcpy() for large moves
   if ALIGNMENT == 1.  */

bool
h8sx_emit_movmd (rtx dest, rtx src, rtx length,
		 HOST_WIDE_INT alignment)
{
  if (!flag_omit_frame_pointer && optimize_size)
    return false;

  if (GET_CODE (length) == CONST_INT)
    {
      rtx dest_reg, src_reg, first_dest, first_src;
      HOST_WIDE_INT n;
      int factor;

      /* Use movmd.l if the alignment allows it, otherwise fall back
	 on movmd.b.  */
      factor = (alignment >= 2 ? 4 : 1);

      /* Make sure the length is within range.  We can handle counter
	 values up to 65536, although HImode truncation will make
	 the count appear negative in rtl dumps.  */
      n = INTVAL (length);
      if (n <= 0 || n / factor > 65536)
	return false;

      /* Create temporary registers for the source and destination
	 pointers.  Initialize them to the start of each region.  */
      dest_reg = copy_addr_to_reg (XEXP (dest, 0));
      src_reg = copy_addr_to_reg (XEXP (src, 0));

      /* Create references to the movmd source and destination blocks.  */
      first_dest = replace_equiv_address (dest, dest_reg);
      first_src = replace_equiv_address (src, src_reg);

      set_mem_size (first_dest, GEN_INT (n & -factor));
      set_mem_size (first_src, GEN_INT (n & -factor));

      length = copy_to_mode_reg (HImode, gen_int_mode (n / factor, HImode));
      emit_insn (gen_movmd (first_dest, first_src, length, GEN_INT (factor)));

      if ((n & -factor) != n)
	{
	  /* Move SRC and DEST past the region we just copied.
	     This is done to update the memory attributes.  */
	  dest = adjust_address (dest, BLKmode, n & -factor);
	  src = adjust_address (src, BLKmode, n & -factor);

	  /* Replace the addresses with the source and destination
	     registers, which movmd has left with the right values.  */
	  dest = replace_equiv_address (dest, dest_reg);
	  src = replace_equiv_address (src, src_reg);

	  /* Mop up the left-over bytes.  */
	  if (n & 2)
	    emit_move_insn (adjust_address (dest, HImode, 0),
			    adjust_address (src, HImode, 0));
	  if (n & 1)
	    emit_move_insn (adjust_address (dest, QImode, n & 2),
			    adjust_address (src, QImode, n & 2));
	}
      return true;
    }
  return false;
}

/* Move ADDR into er6 after pushing its old value onto the stack.  */

void
h8300_swap_into_er6 (rtx addr)
{
  push (HARD_FRAME_POINTER_REGNUM);
  emit_move_insn (hard_frame_pointer_rtx, addr);
  if (REGNO (addr) == SP_REG)
    emit_move_insn (hard_frame_pointer_rtx,
		    plus_constant (hard_frame_pointer_rtx,
				   GET_MODE_SIZE (word_mode)));
}

/* Move the current value of er6 into ADDR and pop its old value
   from the stack.  */

void
h8300_swap_out_of_er6 (rtx addr)
{
  if (REGNO (addr) != SP_REG)
    emit_move_insn (addr, hard_frame_pointer_rtx);
  pop (HARD_FRAME_POINTER_REGNUM);
}

/* Return the length of mov instruction.  */

unsigned int
compute_mov_length (rtx *operands)
{
  /* If the mov instruction involves a memory operand, we compute the
     length, assuming the largest addressing mode is used, and then
     adjust later in the function.  Otherwise, we compute and return
     the exact length in one step.  */
  enum machine_mode mode = GET_MODE (operands[0]);
  rtx dest = operands[0];
  rtx src = operands[1];
  rtx addr;

  if (GET_CODE (src) == MEM)
    addr = XEXP (src, 0);
  else if (GET_CODE (dest) == MEM)
    addr = XEXP (dest, 0);
  else
    addr = NULL_RTX;

  if (TARGET_H8300)
    {
      unsigned int base_length;

      switch (mode)
	{
	case QImode:
	  if (addr == NULL_RTX)
	    return 2;

	  /* The eightbit addressing is available only in QImode, so
	     go ahead and take care of it.  */
	  if (h8300_eightbit_constant_address_p (addr))
	    return 2;

	  base_length = 4;
	  break;

	case HImode:
	  if (addr == NULL_RTX)
	    {
	      if (REG_P (src))
		return 2;

	      if (src == const0_rtx)
		return 2;

	      return 4;
	    }

	  base_length = 4;
	  break;

	case SImode:
	  if (addr == NULL_RTX)
	    {
	      if (REG_P (src))
		return 4;

	      if (GET_CODE (src) == CONST_INT)
		{
		  if (src == const0_rtx)
		    return 4;

		  if ((INTVAL (src) & 0xffff) == 0)
		    return 6;

		  if ((INTVAL (src) & 0xffff) == 0)
		    return 6;

		  if ((INTVAL (src) & 0xffff)
		      == ((INTVAL (src) >> 16) & 0xffff))
		    return 6;
		}
	      return 8;
	    }

	  base_length = 8;
	  break;

	case SFmode:
	  if (addr == NULL_RTX)
	    {
	      if (REG_P (src))
		return 4;

	      if (CONST_DOUBLE_OK_FOR_LETTER_P (src, 'G'))
		return 4;

	      return 8;
	    }

	  base_length = 8;
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Adjust the length based on the addressing mode used.
	 Specifically, we subtract the difference between the actual
	 length and the longest one, which is @(d:16,Rs).  For SImode
	 and SFmode, we double the adjustment because two mov.w are
	 used to do the job.  */

      /* @Rs+ and @-Rd are 2 bytes shorter than the longest.  */
      if (GET_CODE (addr) == PRE_DEC
	  || GET_CODE (addr) == POST_INC)
	{
	  if (mode == QImode || mode == HImode)
	    return base_length - 2;
	  else
	    /* In SImode and SFmode, we use two mov.w instructions, so
	       double the adjustment.  */
	    return base_length - 4;
	}

      /* @Rs and @Rd are 2 bytes shorter than the longest.  Note that
	 in SImode and SFmode, the second mov.w involves an address
	 with displacement, namely @(2,Rs) or @(2,Rd), so we subtract
	 only 2 bytes.  */
      if (GET_CODE (addr) == REG)
	return base_length - 2;

      return base_length;
    }
  else
    {
      unsigned int base_length;

      switch (mode)
	{
	case QImode:
	  if (addr == NULL_RTX)
	    return 2;

	  /* The eightbit addressing is available only in QImode, so
	     go ahead and take care of it.  */
	  if (h8300_eightbit_constant_address_p (addr))
	    return 2;

	  base_length = 8;
	  break;

	case HImode:
	  if (addr == NULL_RTX)
	    {
	      if (REG_P (src))
		return 2;

	      if (src == const0_rtx)
		return 2;

	      return 4;
	    }

	  base_length = 8;
	  break;

	case SImode:
	  if (addr == NULL_RTX)
	    {
	      if (REG_P (src))
		{
		  if (REGNO (src) == MAC_REG || REGNO (dest) == MAC_REG)
		    return 4;
		  else
		    return 2;
		}

	      if (GET_CODE (src) == CONST_INT)
		{
		  int val = INTVAL (src);

		  if (val == 0)
		    return 2;

		  if (val == (val & 0x00ff) || val == (val & 0xff00))
		    return 4;

		  switch (val & 0xffffffff)
		    {
		    case 0xffffffff:
		    case 0xfffffffe:
		    case 0xfffffffc:
		    case 0x0000ffff:
		    case 0x0000fffe:
		    case 0xffff0000:
		    case 0xfffe0000:
		    case 0x00010000:
		    case 0x00020000:
		      return 4;
		    }
		}
	      return 6;
	    }

	  base_length = 10;
	  break;

	case SFmode:
	  if (addr == NULL_RTX)
	    {
	      if (REG_P (src))
		return 2;

	      if (CONST_DOUBLE_OK_FOR_LETTER_P (src, 'G'))
		return 2;

	      return 6;
	    }

	  base_length = 10;
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Adjust the length based on the addressing mode used.
	 Specifically, we subtract the difference between the actual
	 length and the longest one, which is @(d:24,ERs).  */

      /* @ERs+ and @-ERd are 6 bytes shorter than the longest.  */
      if (GET_CODE (addr) == PRE_DEC
	  || GET_CODE (addr) == POST_INC)
	return base_length - 6;

      /* @ERs and @ERd are 6 bytes shorter than the longest.  */
      if (GET_CODE (addr) == REG)
	return base_length - 6;

      /* @(d:16,ERs) and @(d:16,ERd) are 4 bytes shorter than the
	 longest.  */
      if (GET_CODE (addr) == PLUS
	  && GET_CODE (XEXP (addr, 0)) == REG
	  && GET_CODE (XEXP (addr, 1)) == CONST_INT
	  && INTVAL (XEXP (addr, 1)) > -32768
	  && INTVAL (XEXP (addr, 1)) < 32767)
	return base_length - 4;

      /* @aa:16 is 4 bytes shorter than the longest.  */
      if (h8300_tiny_constant_address_p (addr))
	return base_length - 4;

      /* @aa:24 is 2 bytes shorter than the longest.  */
      if (CONSTANT_P (addr))
	return base_length - 2;

      return base_length;
    }
}

/* Output an addition insn.  */

const char *
output_plussi (rtx *operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);

  gcc_assert (mode == SImode);

  if (TARGET_H8300)
    {
      if (GET_CODE (operands[2]) == REG)
	return "add.w\t%f2,%f0\n\taddx\t%y2,%y0\n\taddx\t%z2,%z0";

      if (GET_CODE (operands[2]) == CONST_INT)
	{
	  HOST_WIDE_INT n = INTVAL (operands[2]);

	  if ((n & 0xffffff) == 0)
	    return "add\t%z2,%z0";
	  if ((n & 0xffff) == 0)
	    return "add\t%y2,%y0\n\taddx\t%z2,%z0";
	  if ((n & 0xff) == 0)
	    return "add\t%x2,%x0\n\taddx\t%y2,%y0\n\taddx\t%z2,%z0";
	}

      return "add\t%w2,%w0\n\taddx\t%x2,%x0\n\taddx\t%y2,%y0\n\taddx\t%z2,%z0";
    }
  else
    {
      if (GET_CODE (operands[2]) == CONST_INT
	  && register_operand (operands[1], VOIDmode))
	{
	  HOST_WIDE_INT intval = INTVAL (operands[2]);

	  if (TARGET_H8300SX && (intval >= 1 && intval <= 7))
	    return "add.l\t%S2,%S0";
	  if (TARGET_H8300SX && (intval >= -7 && intval <= -1))
	    return "sub.l\t%G2,%S0";

	  /* See if we can finish with 2 bytes.  */

	  switch ((unsigned int) intval & 0xffffffff)
	    {
	    case 0x00000001:
	    case 0x00000002:
	    case 0x00000004:
	      return "adds\t%2,%S0";

	    case 0xffffffff:
	    case 0xfffffffe:
	    case 0xfffffffc:
	      return "subs\t%G2,%S0";

	    case 0x00010000:
	    case 0x00020000:
	      operands[2] = GEN_INT (intval >> 16);
	      return "inc.w\t%2,%e0";

	    case 0xffff0000:
	    case 0xfffe0000:
	      operands[2] = GEN_INT (intval >> 16);
	      return "dec.w\t%G2,%e0";
	    }

	  /* See if we can finish with 4 bytes.  */
	  if ((intval & 0xffff) == 0)
	    {
	      operands[2] = GEN_INT (intval >> 16);
	      return "add.w\t%2,%e0";
	    }
	}

      if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) < 0)
	{
	  operands[2] = GEN_INT (-INTVAL (operands[2]));
	  return "sub.l\t%S2,%S0";
	}
      return "add.l\t%S2,%S0";
    }
}

/* ??? It would be much easier to add the h8sx stuff if a single function
   classified the addition as either inc/dec, adds/subs, add.w or add.l.  */
/* Compute the length of an addition insn.  */

unsigned int
compute_plussi_length (rtx *operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);

  gcc_assert (mode == SImode);

  if (TARGET_H8300)
    {
      if (GET_CODE (operands[2]) == REG)
	return 6;

      if (GET_CODE (operands[2]) == CONST_INT)
	{
	  HOST_WIDE_INT n = INTVAL (operands[2]);

	  if ((n & 0xffffff) == 0)
	    return 2;
	  if ((n & 0xffff) == 0)
	    return 4;
	  if ((n & 0xff) == 0)
	    return 6;
	}

      return 8;
    }
  else
    {
      if (GET_CODE (operands[2]) == CONST_INT
	  && register_operand (operands[1], VOIDmode))
	{
	  HOST_WIDE_INT intval = INTVAL (operands[2]);

	  if (TARGET_H8300SX && (intval >= 1 && intval <= 7))
	    return 2;
	  if (TARGET_H8300SX && (intval >= -7 && intval <= -1))
	    return 2;

	  /* See if we can finish with 2 bytes.  */

	  switch ((unsigned int) intval & 0xffffffff)
	    {
	    case 0x00000001:
	    case 0x00000002:
	    case 0x00000004:
	      return 2;

	    case 0xffffffff:
	    case 0xfffffffe:
	    case 0xfffffffc:
	      return 2;

	    case 0x00010000:
	    case 0x00020000:
	      return 2;

	    case 0xffff0000:
	    case 0xfffe0000:
	      return 2;
	    }

	  /* See if we can finish with 4 bytes.  */
	  if ((intval & 0xffff) == 0)
	    return 4;
	}

      if (GET_CODE (operands[2]) == CONST_INT && INTVAL (operands[2]) < 0)
	return h8300_length_from_table (operands[0],
					GEN_INT (-INTVAL (operands[2])),
					&addl_length_table);
      else
	return h8300_length_from_table (operands[0], operands[2],
					&addl_length_table);
      return 6;
    }
}

/* Compute which flag bits are valid after an addition insn.  */

int
compute_plussi_cc (rtx *operands)
{
  enum machine_mode mode = GET_MODE (operands[0]);

  gcc_assert (mode == SImode);

  if (TARGET_H8300)
    {
      return CC_CLOBBER;
    }
  else
    {
      if (GET_CODE (operands[2]) == CONST_INT
	  && register_operand (operands[1], VOIDmode))
	{
	  HOST_WIDE_INT intval = INTVAL (operands[2]);

	  if (TARGET_H8300SX && (intval >= 1 && intval <= 7))
	    return CC_SET_ZN;
	  if (TARGET_H8300SX && (intval >= -7 && intval <= -1))
	    return CC_SET_ZN;

	  /* See if we can finish with 2 bytes.  */

	  switch ((unsigned int) intval & 0xffffffff)
	    {
	    case 0x00000001:
	    case 0x00000002:
	    case 0x00000004:
	      return CC_NONE_0HIT;

	    case 0xffffffff:
	    case 0xfffffffe:
	    case 0xfffffffc:
	      return CC_NONE_0HIT;

	    case 0x00010000:
	    case 0x00020000:
	      return CC_CLOBBER;

	    case 0xffff0000:
	    case 0xfffe0000:
	      return CC_CLOBBER;
	    }

	  /* See if we can finish with 4 bytes.  */
	  if ((intval & 0xffff) == 0)
	    return CC_CLOBBER;
	}

      return CC_SET_ZN;
    }
}

/* Output a logical insn.  */

const char *
output_logical_op (enum machine_mode mode, rtx *operands)
{
  /* Figure out the logical op that we need to perform.  */
  enum rtx_code code = GET_CODE (operands[3]);
  /* Pretend that every byte is affected if both operands are registers.  */
  const unsigned HOST_WIDE_INT intval =
    (unsigned HOST_WIDE_INT) ((GET_CODE (operands[2]) == CONST_INT)
			      /* Always use the full instruction if the
				 first operand is in memory.  It is better
				 to use define_splits to generate the shorter
				 sequence where valid.  */
			      && register_operand (operands[1], VOIDmode)
			      ? INTVAL (operands[2]) : 0x55555555);
  /* The determinant of the algorithm.  If we perform an AND, 0
     affects a bit.  Otherwise, 1 affects a bit.  */
  const unsigned HOST_WIDE_INT det = (code != AND) ? intval : ~intval;
  /* Break up DET into pieces.  */
  const unsigned HOST_WIDE_INT b0 = (det >>  0) & 0xff;
  const unsigned HOST_WIDE_INT b1 = (det >>  8) & 0xff;
  const unsigned HOST_WIDE_INT b2 = (det >> 16) & 0xff;
  const unsigned HOST_WIDE_INT b3 = (det >> 24) & 0xff;
  const unsigned HOST_WIDE_INT w0 = (det >>  0) & 0xffff;
  const unsigned HOST_WIDE_INT w1 = (det >> 16) & 0xffff;
  int lower_half_easy_p = 0;
  int upper_half_easy_p = 0;
  /* The name of an insn.  */
  const char *opname;
  char insn_buf[100];

  switch (code)
    {
    case AND:
      opname = "and";
      break;
    case IOR:
      opname = "or";
      break;
    case XOR:
      opname = "xor";
      break;
    default:
      gcc_unreachable ();
    }

  switch (mode)
    {
    case HImode:
      /* First, see if we can finish with one insn.  */
      if ((TARGET_H8300H || TARGET_H8300S)
	  && b0 != 0
	  && b1 != 0)
	{
	  sprintf (insn_buf, "%s.w\t%%T2,%%T0", opname);
	  output_asm_insn (insn_buf, operands);
	}
      else
	{
	  /* Take care of the lower byte.  */
	  if (b0 != 0)
	    {
	      sprintf (insn_buf, "%s\t%%s2,%%s0", opname);
	      output_asm_insn (insn_buf, operands);
	    }
	  /* Take care of the upper byte.  */
	  if (b1 != 0)
	    {
	      sprintf (insn_buf, "%s\t%%t2,%%t0", opname);
	      output_asm_insn (insn_buf, operands);
	    }
	}
      break;
    case SImode:
      if (TARGET_H8300H || TARGET_H8300S)
	{
	  /* Determine if the lower half can be taken care of in no more
	     than two bytes.  */
	  lower_half_easy_p = (b0 == 0
			       || b1 == 0
			       || (code != IOR && w0 == 0xffff));

	  /* Determine if the upper half can be taken care of in no more
	     than two bytes.  */
	  upper_half_easy_p = ((code != IOR && w1 == 0xffff)
			       || (code == AND && w1 == 0xff00));
	}

      /* Check if doing everything with one insn is no worse than
	 using multiple insns.  */
      if ((TARGET_H8300H || TARGET_H8300S)
	  && w0 != 0 && w1 != 0
	  && !(lower_half_easy_p && upper_half_easy_p)
	  && !(code == IOR && w1 == 0xffff
	       && (w0 & 0x8000) != 0 && lower_half_easy_p))
	{
	  sprintf (insn_buf, "%s.l\t%%S2,%%S0", opname);
	  output_asm_insn (insn_buf, operands);
	}
      else
	{
	  /* Take care of the lower and upper words individually.  For
	     each word, we try different methods in the order of

	     1) the special insn (in case of AND or XOR),
	     2) the word-wise insn, and
	     3) The byte-wise insn.  */
	  if (w0 == 0xffff
	      && (TARGET_H8300 ? (code == AND) : (code != IOR)))
	    output_asm_insn ((code == AND)
			     ? "sub.w\t%f0,%f0" : "not.w\t%f0",
			     operands);
	  else if ((TARGET_H8300H || TARGET_H8300S)
		   && (b0 != 0)
		   && (b1 != 0))
	    {
	      sprintf (insn_buf, "%s.w\t%%f2,%%f0", opname);
	      output_asm_insn (insn_buf, operands);
	    }
	  else
	    {
	      if (b0 != 0)
		{
		  sprintf (insn_buf, "%s\t%%w2,%%w0", opname);
		  output_asm_insn (insn_buf, operands);
		}
	      if (b1 != 0)
		{
		  sprintf (insn_buf, "%s\t%%x2,%%x0", opname);
		  output_asm_insn (insn_buf, operands);
		}
	    }

	  if ((w1 == 0xffff)
	      && (TARGET_H8300 ? (code == AND) : (code != IOR)))
	    output_asm_insn ((code == AND)
			     ? "sub.w\t%e0,%e0" : "not.w\t%e0",
			     operands);
	  else if ((TARGET_H8300H || TARGET_H8300S)
		   && code == IOR
		   && w1 == 0xffff
		   && (w0 & 0x8000) != 0)
	    {
	      output_asm_insn ("exts.l\t%S0", operands);
	    }
	  else if ((TARGET_H8300H || TARGET_H8300S)
		   && code == AND
		   && w1 == 0xff00)
	    {
	      output_asm_insn ("extu.w\t%e0", operands);
	    }
	  else if (TARGET_H8300H || TARGET_H8300S)
	    {
	      if (w1 != 0)
		{
		  sprintf (insn_buf, "%s.w\t%%e2,%%e0", opname);
		  output_asm_insn (insn_buf, operands);
		}
	    }
	  else
	    {
	      if (b2 != 0)
		{
		  sprintf (insn_buf, "%s\t%%y2,%%y0", opname);
		  output_asm_insn (insn_buf, operands);
		}
	      if (b3 != 0)
		{
		  sprintf (insn_buf, "%s\t%%z2,%%z0", opname);
		  output_asm_insn (insn_buf, operands);
		}
	    }
	}
      break;
    default:
      gcc_unreachable ();
    }
  return "";
}

/* Compute the length of a logical insn.  */

unsigned int
compute_logical_op_length (enum machine_mode mode, rtx *operands)
{
  /* Figure out the logical op that we need to perform.  */
  enum rtx_code code = GET_CODE (operands[3]);
  /* Pretend that every byte is affected if both operands are registers.  */
  const unsigned HOST_WIDE_INT intval =
    (unsigned HOST_WIDE_INT) ((GET_CODE (operands[2]) == CONST_INT)
			      /* Always use the full instruction if the
				 first operand is in memory.  It is better
				 to use define_splits to generate the shorter
				 sequence where valid.  */
			      && register_operand (operands[1], VOIDmode)
			      ? INTVAL (operands[2]) : 0x55555555);
  /* The determinant of the algorithm.  If we perform an AND, 0
     affects a bit.  Otherwise, 1 affects a bit.  */
  const unsigned HOST_WIDE_INT det = (code != AND) ? intval : ~intval;
  /* Break up DET into pieces.  */
  const unsigned HOST_WIDE_INT b0 = (det >>  0) & 0xff;
  const unsigned HOST_WIDE_INT b1 = (det >>  8) & 0xff;
  const unsigned HOST_WIDE_INT b2 = (det >> 16) & 0xff;
  const unsigned HOST_WIDE_INT b3 = (det >> 24) & 0xff;
  const unsigned HOST_WIDE_INT w0 = (det >>  0) & 0xffff;
  const unsigned HOST_WIDE_INT w1 = (det >> 16) & 0xffff;
  int lower_half_easy_p = 0;
  int upper_half_easy_p = 0;
  /* Insn length.  */
  unsigned int length = 0;

  switch (mode)
    {
    case HImode:
      /* First, see if we can finish with one insn.  */
      if ((TARGET_H8300H || TARGET_H8300S)
	  && b0 != 0
	  && b1 != 0)
	{
	  length = h8300_length_from_table (operands[1], operands[2],
					    &logicw_length_table);
	}
      else
	{
	  /* Take care of the lower byte.  */
	  if (b0 != 0)
	    length += 2;

	  /* Take care of the upper byte.  */
	  if (b1 != 0)
	    length += 2;
	}
      break;
    case SImode:
      if (TARGET_H8300H || TARGET_H8300S)
	{
	  /* Determine if the lower half can be taken care of in no more
	     than two bytes.  */
	  lower_half_easy_p = (b0 == 0
			       || b1 == 0
			       || (code != IOR && w0 == 0xffff));

	  /* Determine if the upper half can be taken care of in no more
	     than two bytes.  */
	  upper_half_easy_p = ((code != IOR && w1 == 0xffff)
			       || (code == AND && w1 == 0xff00));
	}

      /* Check if doing everything with one insn is no worse than
	 using multiple insns.  */
      if ((TARGET_H8300H || TARGET_H8300S)
	  && w0 != 0 && w1 != 0
	  && !(lower_half_easy_p && upper_half_easy_p)
	  && !(code == IOR && w1 == 0xffff
	       && (w0 & 0x8000) != 0 && lower_half_easy_p))
	{
	  length = h8300_length_from_table (operands[1], operands[2],
					    &logicl_length_table);
	}
      else
	{
	  /* Take care of the lower and upper words individually.  For
	     each word, we try different methods in the order of

	     1) the special insn (in case of AND or XOR),
	     2) the word-wise insn, and
	     3) The byte-wise insn.  */
	  if (w0 == 0xffff
	      && (TARGET_H8300 ? (code == AND) : (code != IOR)))
	    {
	      length += 2;
	    }
	  else if ((TARGET_H8300H || TARGET_H8300S)
		   && (b0 != 0)
		   && (b1 != 0))
	    {
	      length += 4;
	    }
	  else
	    {
	      if (b0 != 0)
		length += 2;

	      if (b1 != 0)
		length += 2;
	    }

	  if (w1 == 0xffff
	      && (TARGET_H8300 ? (code == AND) : (code != IOR)))
	    {
	      length += 2;
	    }
	  else if ((TARGET_H8300H || TARGET_H8300S)
		   && code == IOR
		   && w1 == 0xffff
		   && (w0 & 0x8000) != 0)
	    {
	      length += 2;
	    }
	  else if ((TARGET_H8300H || TARGET_H8300S)
		   && code == AND
		   && w1 == 0xff00)
	    {
	      length += 2;
	    }
	  else if (TARGET_H8300H || TARGET_H8300S)
	    {
	      if (w1 != 0)
		length += 4;
	    }
	  else
	    {
	      if (b2 != 0)
		length += 2;

	      if (b3 != 0)
		length += 2;
	    }
	}
      break;
    default:
      gcc_unreachable ();
    }
  return length;
}

/* Compute which flag bits are valid after a logical insn.  */

int
compute_logical_op_cc (enum machine_mode mode, rtx *operands)
{
  /* Figure out the logical op that we need to perform.  */
  enum rtx_code code = GET_CODE (operands[3]);
  /* Pretend that every byte is affected if both operands are registers.  */
  const unsigned HOST_WIDE_INT intval =
    (unsigned HOST_WIDE_INT) ((GET_CODE (operands[2]) == CONST_INT)
			      /* Always use the full instruction if the
				 first operand is in memory.  It is better
				 to use define_splits to generate the shorter
				 sequence where valid.  */
			      && register_operand (operands[1], VOIDmode)
			      ? INTVAL (operands[2]) : 0x55555555);
  /* The determinant of the algorithm.  If we perform an AND, 0
     affects a bit.  Otherwise, 1 affects a bit.  */
  const unsigned HOST_WIDE_INT det = (code != AND) ? intval : ~intval;
  /* Break up DET into pieces.  */
  const unsigned HOST_WIDE_INT b0 = (det >>  0) & 0xff;
  const unsigned HOST_WIDE_INT b1 = (det >>  8) & 0xff;
  const unsigned HOST_WIDE_INT w0 = (det >>  0) & 0xffff;
  const unsigned HOST_WIDE_INT w1 = (det >> 16) & 0xffff;
  int lower_half_easy_p = 0;
  int upper_half_easy_p = 0;
  /* Condition code.  */
  enum attr_cc cc = CC_CLOBBER;

  switch (mode)
    {
    case HImode:
      /* First, see if we can finish with one insn.  */
      if ((TARGET_H8300H || TARGET_H8300S)
	  && b0 != 0
	  && b1 != 0)
	{
	  cc = CC_SET_ZNV;
	}
      break;
    case SImode:
      if (TARGET_H8300H || TARGET_H8300S)
	{
	  /* Determine if the lower half can be taken care of in no more
	     than two bytes.  */
	  lower_half_easy_p = (b0 == 0
			       || b1 == 0
			       || (code != IOR && w0 == 0xffff));

	  /* Determine if the upper half can be taken care of in no more
	     than two bytes.  */
	  upper_half_easy_p = ((code != IOR && w1 == 0xffff)
			       || (code == AND && w1 == 0xff00));
	}

      /* Check if doing everything with one insn is no worse than
	 using multiple insns.  */
      if ((TARGET_H8300H || TARGET_H8300S)
	  && w0 != 0 && w1 != 0
	  && !(lower_half_easy_p && upper_half_easy_p)
	  && !(code == IOR && w1 == 0xffff
	       && (w0 & 0x8000) != 0 && lower_half_easy_p))
	{
	  cc = CC_SET_ZNV;
	}
      else
	{
	  if ((TARGET_H8300H || TARGET_H8300S)
	      && code == IOR
	      && w1 == 0xffff
	      && (w0 & 0x8000) != 0)
	    {
	      cc = CC_SET_ZNV;
	    }
	}
      break;
    default:
      gcc_unreachable ();
    }
  return cc;
}

/* Expand a conditional branch.  */

void
h8300_expand_branch (enum rtx_code code, rtx label)
{
  rtx tmp;

  tmp = gen_rtx_fmt_ee (code, VOIDmode, cc0_rtx, const0_rtx);
  tmp = gen_rtx_IF_THEN_ELSE (VOIDmode, tmp,
			      gen_rtx_LABEL_REF (VOIDmode, label),
			      pc_rtx);
  emit_jump_insn (gen_rtx_SET (VOIDmode, pc_rtx, tmp));
}

/* Shifts.

   We devote a fair bit of code to getting efficient shifts since we
   can only shift one bit at a time on the H8/300 and H8/300H and only
   one or two bits at a time on the H8S.

   All shift code falls into one of the following ways of
   implementation:

   o SHIFT_INLINE: Emit straight line code for the shift; this is used
     when a straight line shift is about the same size or smaller than
     a loop.

   o SHIFT_ROT_AND: Rotate the value the opposite direction, then mask
     off the bits we don't need.  This is used when only a few of the
     bits in the original value will survive in the shifted value.

   o SHIFT_SPECIAL: Often it's possible to move a byte or a word to
     simulate a shift by 8, 16, or 24 bits.  Once moved, a few inline
     shifts can be added if the shift count is slightly more than 8 or
     16.  This case also includes other oddballs that are not worth
     explaining here.

   o SHIFT_LOOP: Emit a loop using one (or two on H8S) bit shifts.

   For each shift count, we try to use code that has no trade-off
   between code size and speed whenever possible.

   If the trade-off is unavoidable, we try to be reasonable.
   Specifically, the fastest version is one instruction longer than
   the shortest version, we take the fastest version.  We also provide
   the use a way to switch back to the shortest version with -Os.

   For the details of the shift algorithms for various shift counts,
   refer to shift_alg_[qhs]i.  */

/* Classify a shift with the given mode and code.  OP is the shift amount.  */

enum h8sx_shift_type
h8sx_classify_shift (enum machine_mode mode, enum rtx_code code, rtx op)
{
  if (!TARGET_H8300SX)
    return H8SX_SHIFT_NONE;

  switch (code)
    {
    case ASHIFT:
    case LSHIFTRT:
      /* Check for variable shifts (shll Rs,Rd and shlr Rs,Rd).  */
      if (GET_CODE (op) != CONST_INT)
	return H8SX_SHIFT_BINARY;

      /* Reject out-of-range shift amounts.  */
      if (INTVAL (op) <= 0 || INTVAL (op) >= GET_MODE_BITSIZE (mode))
	return H8SX_SHIFT_NONE;

      /* Power-of-2 shifts are effectively unary operations.  */
      if (exact_log2 (INTVAL (op)) >= 0)
	return H8SX_SHIFT_UNARY;

      return H8SX_SHIFT_BINARY;

    case ASHIFTRT:
      if (op == const1_rtx || op == const2_rtx)
	return H8SX_SHIFT_UNARY;
      return H8SX_SHIFT_NONE;

    case ROTATE:
      if (GET_CODE (op) == CONST_INT
	  && (INTVAL (op) == 1
	      || INTVAL (op) == 2
	      || INTVAL (op) == GET_MODE_BITSIZE (mode) - 2
	      || INTVAL (op) == GET_MODE_BITSIZE (mode) - 1))
	return H8SX_SHIFT_UNARY;
      return H8SX_SHIFT_NONE;

    default:
      return H8SX_SHIFT_NONE;
    }
}

/* Return the asm template for a single h8sx shift instruction.
   OPERANDS[0] and OPERANDS[1] are the destination, OPERANDS[2]
   is the source and OPERANDS[3] is the shift.  SUFFIX is the
   size suffix ('b', 'w' or 'l') and OPTYPE is the print_operand
   prefix for the destination operand.  */

const char *
output_h8sx_shift (rtx *operands, int suffix, int optype)
{
  static char buffer[16];
  const char *stem;

  switch (GET_CODE (operands[3]))
    {
    case ASHIFT:
      stem = "shll";
      break;

    case ASHIFTRT:
      stem = "shar";
      break;

    case LSHIFTRT:
      stem = "shlr";
      break;

    case ROTATE:
      stem = "rotl";
      if (INTVAL (operands[2]) > 2)
	{
	  /* This is really a right rotate.  */
	  operands[2] = GEN_INT (GET_MODE_BITSIZE (GET_MODE (operands[0]))
				 - INTVAL (operands[2]));
	  stem = "rotr";
	}
      break;

    default:
      gcc_unreachable ();
    }
  if (operands[2] == const1_rtx)
    sprintf (buffer, "%s.%c\t%%%c0", stem, suffix, optype);
  else
    sprintf (buffer, "%s.%c\t%%X2,%%%c0", stem, suffix, optype);
  return buffer;
}

/* Emit code to do shifts.  */

bool
expand_a_shift (enum machine_mode mode, int code, rtx operands[])
{
  switch (h8sx_classify_shift (mode, code, operands[2]))
    {
    case H8SX_SHIFT_BINARY:
      operands[1] = force_reg (mode, operands[1]);
      return false;

    case H8SX_SHIFT_UNARY:
      return false;

    case H8SX_SHIFT_NONE:
      break;
    }

  emit_move_insn (operands[0], operands[1]);

  /* Need a loop to get all the bits we want  - we generate the
     code at emit time, but need to allocate a scratch reg now.  */

  emit_insn (gen_rtx_PARALLEL
	     (VOIDmode,
	      gen_rtvec (2,
			 gen_rtx_SET (VOIDmode, operands[0],
				      gen_rtx_fmt_ee (code, mode,
						      operands[0], operands[2])),
			 gen_rtx_CLOBBER (VOIDmode,
					  gen_rtx_SCRATCH (QImode)))));
  return true;
}

/* Symbols of the various modes which can be used as indices.  */

enum shift_mode
{
  QIshift, HIshift, SIshift
};

/* For single bit shift insns, record assembler and what bits of the
   condition code are valid afterwards (represented as various CC_FOO
   bits, 0 means CC isn't left in a usable state).  */

struct shift_insn
{
  const char *const assembler;
  const int cc_valid;
};

/* Assembler instruction shift table.

   These tables are used to look up the basic shifts.
   They are indexed by cpu, shift_type, and mode.  */

static const struct shift_insn shift_one[2][3][3] =
{
/* H8/300 */
  {
/* SHIFT_ASHIFT */
    {
      { "shll\t%X0", CC_SET_ZNV },
      { "add.w\t%T0,%T0", CC_SET_ZN },
      { "add.w\t%f0,%f0\n\taddx\t%y0,%y0\n\taddx\t%z0,%z0", CC_CLOBBER }
    },
/* SHIFT_LSHIFTRT */
    {
      { "shlr\t%X0", CC_SET_ZNV },
      { "shlr\t%t0\n\trotxr\t%s0", CC_CLOBBER },
      { "shlr\t%z0\n\trotxr\t%y0\n\trotxr\t%x0\n\trotxr\t%w0", CC_CLOBBER }
    },
/* SHIFT_ASHIFTRT */
    {
      { "shar\t%X0", CC_SET_ZNV },
      { "shar\t%t0\n\trotxr\t%s0", CC_CLOBBER },
      { "shar\t%z0\n\trotxr\t%y0\n\trotxr\t%x0\n\trotxr\t%w0", CC_CLOBBER }
    }
  },
/* H8/300H */
  {
/* SHIFT_ASHIFT */
    {
      { "shll.b\t%X0", CC_SET_ZNV },
      { "shll.w\t%T0", CC_SET_ZNV },
      { "shll.l\t%S0", CC_SET_ZNV }
    },
/* SHIFT_LSHIFTRT */
    {
      { "shlr.b\t%X0", CC_SET_ZNV },
      { "shlr.w\t%T0", CC_SET_ZNV },
      { "shlr.l\t%S0", CC_SET_ZNV }
    },
/* SHIFT_ASHIFTRT */
    {
      { "shar.b\t%X0", CC_SET_ZNV },
      { "shar.w\t%T0", CC_SET_ZNV },
      { "shar.l\t%S0", CC_SET_ZNV }
    }
  }
};

static const struct shift_insn shift_two[3][3] =
{
/* SHIFT_ASHIFT */
    {
      { "shll.b\t#2,%X0", CC_SET_ZNV },
      { "shll.w\t#2,%T0", CC_SET_ZNV },
      { "shll.l\t#2,%S0", CC_SET_ZNV }
    },
/* SHIFT_LSHIFTRT */
    {
      { "shlr.b\t#2,%X0", CC_SET_ZNV },
      { "shlr.w\t#2,%T0", CC_SET_ZNV },
      { "shlr.l\t#2,%S0", CC_SET_ZNV }
    },
/* SHIFT_ASHIFTRT */
    {
      { "shar.b\t#2,%X0", CC_SET_ZNV },
      { "shar.w\t#2,%T0", CC_SET_ZNV },
      { "shar.l\t#2,%S0", CC_SET_ZNV }
    }
};

/* Rotates are organized by which shift they'll be used in implementing.
   There's no need to record whether the cc is valid afterwards because
   it is the AND insn that will decide this.  */

static const char *const rotate_one[2][3][3] =
{
/* H8/300 */
  {
/* SHIFT_ASHIFT */
    {
      "rotr\t%X0",
      "shlr\t%t0\n\trotxr\t%s0\n\tbst\t#7,%t0",
      0
    },
/* SHIFT_LSHIFTRT */
    {
      "rotl\t%X0",
      "shll\t%s0\n\trotxl\t%t0\n\tbst\t#0,%s0",
      0
    },
/* SHIFT_ASHIFTRT */
    {
      "rotl\t%X0",
      "shll\t%s0\n\trotxl\t%t0\n\tbst\t#0,%s0",
      0
    }
  },
/* H8/300H */
  {
/* SHIFT_ASHIFT */
    {
      "rotr.b\t%X0",
      "rotr.w\t%T0",
      "rotr.l\t%S0"
    },
/* SHIFT_LSHIFTRT */
    {
      "rotl.b\t%X0",
      "rotl.w\t%T0",
      "rotl.l\t%S0"
    },
/* SHIFT_ASHIFTRT */
    {
      "rotl.b\t%X0",
      "rotl.w\t%T0",
      "rotl.l\t%S0"
    }
  }
};

static const char *const rotate_two[3][3] =
{
/* SHIFT_ASHIFT */
    {
      "rotr.b\t#2,%X0",
      "rotr.w\t#2,%T0",
      "rotr.l\t#2,%S0"
    },
/* SHIFT_LSHIFTRT */
    {
      "rotl.b\t#2,%X0",
      "rotl.w\t#2,%T0",
      "rotl.l\t#2,%S0"
    },
/* SHIFT_ASHIFTRT */
    {
      "rotl.b\t#2,%X0",
      "rotl.w\t#2,%T0",
      "rotl.l\t#2,%S0"
    }
};

struct shift_info {
  /* Shift algorithm.  */
  enum shift_alg alg;

  /* The number of bits to be shifted by shift1 and shift2.  Valid
     when ALG is SHIFT_SPECIAL.  */
  unsigned int remainder;

  /* Special insn for a shift.  Valid when ALG is SHIFT_SPECIAL.  */
  const char *special;

  /* Insn for a one-bit shift.  Valid when ALG is either SHIFT_INLINE
     or SHIFT_SPECIAL, and REMAINDER is nonzero.  */
  const char *shift1;

  /* Insn for a two-bit shift.  Valid when ALG is either SHIFT_INLINE
     or SHIFT_SPECIAL, and REMAINDER is nonzero.  */
  const char *shift2;

  /* CC status for SHIFT_INLINE.  */
  int cc_inline;

  /* CC status  for SHIFT_SPECIAL.  */
  int cc_special;
};

static void get_shift_alg (enum shift_type,
			   enum shift_mode, unsigned int,
			   struct shift_info *);

/* Given SHIFT_TYPE, SHIFT_MODE, and shift count COUNT, determine the
   best algorithm for doing the shift.  The assembler code is stored
   in the pointers in INFO.  We achieve the maximum efficiency in most
   cases when !TARGET_H8300.  In case of TARGET_H8300, shifts in
   SImode in particular have a lot of room to optimize.

   We first determine the strategy of the shift algorithm by a table
   lookup.  If that tells us to use a hand crafted assembly code, we
   go into the big switch statement to find what that is.  Otherwise,
   we resort to a generic way, such as inlining.  In either case, the
   result is returned through INFO.  */

static void
get_shift_alg (enum shift_type shift_type, enum shift_mode shift_mode,
	       unsigned int count, struct shift_info *info)
{
  enum h8_cpu cpu;

  /* Find the target CPU.  */
  if (TARGET_H8300)
    cpu = H8_300;
  else if (TARGET_H8300H)
    cpu = H8_300H;
  else
    cpu = H8_S;

  /* Find the shift algorithm.  */
  info->alg = SHIFT_LOOP;
  switch (shift_mode)
    {
    case QIshift:
      if (count < GET_MODE_BITSIZE (QImode))
	info->alg = shift_alg_qi[cpu][shift_type][count];
      break;

    case HIshift:
      if (count < GET_MODE_BITSIZE (HImode))
	info->alg = shift_alg_hi[cpu][shift_type][count];
      break;

    case SIshift:
      if (count < GET_MODE_BITSIZE (SImode))
	info->alg = shift_alg_si[cpu][shift_type][count];
      break;

    default:
      gcc_unreachable ();
    }

  /* Fill in INFO.  Return unless we have SHIFT_SPECIAL.  */
  switch (info->alg)
    {
    case SHIFT_INLINE:
      info->remainder = count;
      /* Fall through.  */

    case SHIFT_LOOP:
      /* It is up to the caller to know that looping clobbers cc.  */
      info->shift1 = shift_one[cpu_type][shift_type][shift_mode].assembler;
      info->shift2 = shift_two[shift_type][shift_mode].assembler;
      info->cc_inline = shift_one[cpu_type][shift_type][shift_mode].cc_valid;
      goto end;

    case SHIFT_ROT_AND:
      info->shift1 = rotate_one[cpu_type][shift_type][shift_mode];
      info->shift2 = rotate_two[shift_type][shift_mode];
      info->cc_inline = CC_CLOBBER;
      goto end;

    case SHIFT_SPECIAL:
      /* REMAINDER is 0 for most cases, so initialize it to 0.  */
      info->remainder = 0;
      info->shift1 = shift_one[cpu_type][shift_type][shift_mode].assembler;
      info->shift2 = shift_two[shift_type][shift_mode].assembler;
      info->cc_inline = shift_one[cpu_type][shift_type][shift_mode].cc_valid;
      info->cc_special = CC_CLOBBER;
      break;
    }

  /* Here we only deal with SHIFT_SPECIAL.  */
  switch (shift_mode)
    {
    case QIshift:
      /* For ASHIFTRT by 7 bits, the sign bit is simply replicated
	 through the entire value.  */
      gcc_assert (shift_type == SHIFT_ASHIFTRT && count == 7);
      info->special = "shll\t%X0\n\tsubx\t%X0,%X0";
      goto end;

    case HIshift:
      if (count == 7)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      if (TARGET_H8300)
		info->special = "shar.b\t%t0\n\tmov.b\t%s0,%t0\n\trotxr.b\t%t0\n\trotr.b\t%s0\n\tand.b\t#0x80,%s0";
	      else
		info->special = "shar.b\t%t0\n\tmov.b\t%s0,%t0\n\trotxr.w\t%T0\n\tand.b\t#0x80,%s0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300)
		info->special = "shal.b\t%s0\n\tmov.b\t%t0,%s0\n\trotxl.b\t%s0\n\trotl.b\t%t0\n\tand.b\t#0x01,%t0";
	      else
		info->special = "shal.b\t%s0\n\tmov.b\t%t0,%s0\n\trotxl.w\t%T0\n\tand.b\t#0x01,%t0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "shal.b\t%s0\n\tmov.b\t%t0,%s0\n\trotxl.b\t%s0\n\tsubx\t%t0,%t0";
	      goto end;
	    }
	}
      else if ((8 <= count && count <= 13)
	       || (TARGET_H8300S && count == 14))
	{
	  info->remainder = count - 8;

	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "mov.b\t%s0,%t0\n\tsub.b\t%s0,%s0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300)
		{
		  info->special = "mov.b\t%t0,%s0\n\tsub.b\t%t0,%t0";
		  info->shift1  = "shlr.b\t%s0";
		  info->cc_inline = CC_SET_ZNV;
		}
	      else
		{
		  info->special = "mov.b\t%t0,%s0\n\textu.w\t%T0";
		  info->cc_special = CC_SET_ZNV;
		}
	      goto end;
	    case SHIFT_ASHIFTRT:
	      if (TARGET_H8300)
		{
		  info->special = "mov.b\t%t0,%s0\n\tbld\t#7,%s0\n\tsubx\t%t0,%t0";
		  info->shift1  = "shar.b\t%s0";
		}
	      else
		{
		  info->special = "mov.b\t%t0,%s0\n\texts.w\t%T0";
		  info->cc_special = CC_SET_ZNV;
		}
	      goto end;
	    }
	}
      else if (count == 14)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      if (TARGET_H8300)
		info->special = "mov.b\t%s0,%t0\n\trotr.b\t%t0\n\trotr.b\t%t0\n\tand.b\t#0xC0,%t0\n\tsub.b\t%s0,%s0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300)
		info->special = "mov.b\t%t0,%s0\n\trotl.b\t%s0\n\trotl.b\t%s0\n\tand.b\t#3,%s0\n\tsub.b\t%t0,%t0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      if (TARGET_H8300)
		info->special = "mov.b\t%t0,%s0\n\tshll.b\t%s0\n\tsubx.b\t%t0,%t0\n\tshll.b\t%s0\n\tmov.b\t%t0,%s0\n\tbst.b\t#0,%s0";
	      else if (TARGET_H8300H)
		{
		  info->special = "shll.b\t%t0\n\tsubx.b\t%s0,%s0\n\tshll.b\t%t0\n\trotxl.b\t%s0\n\texts.w\t%T0";
		  info->cc_special = CC_SET_ZNV;
		}
	      else /* TARGET_H8300S */
		gcc_unreachable ();
	      goto end;
	    }
	}
      else if (count == 15)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "bld\t#0,%s0\n\txor\t%s0,%s0\n\txor\t%t0,%t0\n\tbst\t#7,%t0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      info->special = "bld\t#7,%t0\n\txor\t%s0,%s0\n\txor\t%t0,%t0\n\tbst\t#0,%s0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "shll\t%t0\n\tsubx\t%t0,%t0\n\tmov.b\t%t0,%s0";
	      goto end;
	    }
	}
      gcc_unreachable ();

    case SIshift:
      if (TARGET_H8300 && 8 <= count && count <= 9)
	{
	  info->remainder = count - 8;

	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "mov.b\t%y0,%z0\n\tmov.b\t%x0,%y0\n\tmov.b\t%w0,%x0\n\tsub.b\t%w0,%w0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      info->special = "mov.b\t%x0,%w0\n\tmov.b\t%y0,%x0\n\tmov.b\t%z0,%y0\n\tsub.b\t%z0,%z0";
	      info->shift1  = "shlr\t%y0\n\trotxr\t%x0\n\trotxr\t%w0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "mov.b\t%x0,%w0\n\tmov.b\t%y0,%x0\n\tmov.b\t%z0,%y0\n\tshll\t%z0\n\tsubx\t%z0,%z0";
	      goto end;
	    }
	}
      else if (count == 8 && !TARGET_H8300)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "mov.w\t%e0,%f4\n\tmov.b\t%s4,%t4\n\tmov.b\t%t0,%s4\n\tmov.b\t%s0,%t0\n\tsub.b\t%s0,%s0\n\tmov.w\t%f4,%e0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      info->special = "mov.w\t%e0,%f4\n\tmov.b\t%t0,%s0\n\tmov.b\t%s4,%t0\n\tmov.b\t%t4,%s4\n\textu.w\t%f4\n\tmov.w\t%f4,%e0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "mov.w\t%e0,%f4\n\tmov.b\t%t0,%s0\n\tmov.b\t%s4,%t0\n\tmov.b\t%t4,%s4\n\texts.w\t%f4\n\tmov.w\t%f4,%e0";
	      goto end;
	    }
	}
      else if (count == 15 && TARGET_H8300)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      gcc_unreachable ();
	    case SHIFT_LSHIFTRT:
	      info->special = "bld\t#7,%z0\n\tmov.w\t%e0,%f0\n\txor\t%y0,%y0\n\txor\t%z0,%z0\n\trotxl\t%w0\n\trotxl\t%x0\n\trotxl\t%y0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "bld\t#7,%z0\n\tmov.w\t%e0,%f0\n\trotxl\t%w0\n\trotxl\t%x0\n\tsubx\t%y0,%y0\n\tsubx\t%z0,%z0";
	      goto end;
	    }
	}
      else if (count == 15 && !TARGET_H8300)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "shlr.w\t%e0\n\tmov.w\t%f0,%e0\n\txor.w\t%f0,%f0\n\trotxr.l\t%S0";
	      info->cc_special = CC_SET_ZNV;
	      goto end;
	    case SHIFT_LSHIFTRT:
	      info->special = "shll.w\t%f0\n\tmov.w\t%e0,%f0\n\txor.w\t%e0,%e0\n\trotxl.l\t%S0";
	      info->cc_special = CC_SET_ZNV;
	      goto end;
	    case SHIFT_ASHIFTRT:
	      gcc_unreachable ();
	    }
	}
      else if ((TARGET_H8300 && 16 <= count && count <= 20)
	       || (TARGET_H8300H && 16 <= count && count <= 19)
	       || (TARGET_H8300S && 16 <= count && count <= 21))
	{
	  info->remainder = count - 16;

	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "mov.w\t%f0,%e0\n\tsub.w\t%f0,%f0";
	      if (TARGET_H8300)
		info->shift1 = "add.w\t%e0,%e0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300)
		{
		  info->special = "mov.w\t%e0,%f0\n\tsub.w\t%e0,%e0";
		  info->shift1  = "shlr\t%x0\n\trotxr\t%w0";
		}
	      else
		{
		  info->special = "mov.w\t%e0,%f0\n\textu.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		}
	      goto end;
	    case SHIFT_ASHIFTRT:
	      if (TARGET_H8300)
		{
		  info->special = "mov.w\t%e0,%f0\n\tshll\t%z0\n\tsubx\t%z0,%z0\n\tmov.b\t%z0,%y0";
		  info->shift1  = "shar\t%x0\n\trotxr\t%w0";
		}
	      else
		{
		  info->special = "mov.w\t%e0,%f0\n\texts.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		}
	      goto end;
	    }
	}
      else if (TARGET_H8300 && 24 <= count && count <= 28)
	{
	  info->remainder = count - 24;

	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "mov.b\t%w0,%z0\n\tsub.b\t%y0,%y0\n\tsub.w\t%f0,%f0";
	      info->shift1  = "shll.b\t%z0";
	      info->cc_inline = CC_SET_ZNV;
	      goto end;
	    case SHIFT_LSHIFTRT:
	      info->special = "mov.b\t%z0,%w0\n\tsub.b\t%x0,%x0\n\tsub.w\t%e0,%e0";
	      info->shift1  = "shlr.b\t%w0";
	      info->cc_inline = CC_SET_ZNV;
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "mov.b\t%z0,%w0\n\tbld\t#7,%w0\n\tsubx\t%x0,%x0\n\tsubx\t%x0,%x0\n\tsubx\t%x0,%x0";
	      info->shift1  = "shar.b\t%w0";
	      info->cc_inline = CC_SET_ZNV;
	      goto end;
	    }
	}
      else if ((TARGET_H8300H && count == 24)
	       || (TARGET_H8300S && 24 <= count && count <= 25))
	{
	  info->remainder = count - 24;

	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      info->special = "mov.b\t%s0,%t0\n\tsub.b\t%s0,%s0\n\tmov.w\t%f0,%e0\n\tsub.w\t%f0,%f0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      info->special = "mov.w\t%e0,%f0\n\tmov.b\t%t0,%s0\n\textu.w\t%f0\n\textu.l\t%S0";
	      info->cc_special = CC_SET_ZNV;
	      goto end;
	    case SHIFT_ASHIFTRT:
	      info->special = "mov.w\t%e0,%f0\n\tmov.b\t%t0,%s0\n\texts.w\t%f0\n\texts.l\t%S0";
	      info->cc_special = CC_SET_ZNV;
	      goto end;
	    }
	}
      else if (!TARGET_H8300 && count == 28)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      if (TARGET_H8300H)
		info->special = "sub.w\t%e0,%e0\n\trotr.l\t%S0\n\trotr.l\t%S0\n\trotr.l\t%S0\n\trotr.l\t%S0\n\tsub.w\t%f0,%f0";
	      else
		info->special = "sub.w\t%e0,%e0\n\trotr.l\t#2,%S0\n\trotr.l\t#2,%S0\n\tsub.w\t%f0,%f0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300H)
		{
		  info->special = "sub.w\t%f0,%f0\n\trotl.l\t%S0\n\trotl.l\t%S0\n\trotl.l\t%S0\n\trotl.l\t%S0\n\textu.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		}
	      else
		info->special = "sub.w\t%f0,%f0\n\trotl.l\t#2,%S0\n\trotl.l\t#2,%S0\n\textu.l\t%S0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      gcc_unreachable ();
	    }
	}
      else if (!TARGET_H8300 && count == 29)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      if (TARGET_H8300H)
		info->special = "sub.w\t%e0,%e0\n\trotr.l\t%S0\n\trotr.l\t%S0\n\trotr.l\t%S0\n\tsub.w\t%f0,%f0";
	      else
		info->special = "sub.w\t%e0,%e0\n\trotr.l\t#2,%S0\n\trotr.l\t%S0\n\tsub.w\t%f0,%f0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300H)
		{
		  info->special = "sub.w\t%f0,%f0\n\trotl.l\t%S0\n\trotl.l\t%S0\n\trotl.l\t%S0\n\textu.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		}
	      else
		{
		  info->special = "sub.w\t%f0,%f0\n\trotl.l\t#2,%S0\n\trotl.l\t%S0\n\textu.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		}
	      goto end;
	    case SHIFT_ASHIFTRT:
	      gcc_unreachable ();
	    }
	}
      else if (!TARGET_H8300 && count == 30)
	{
	  switch (shift_type)
	    {
	    case SHIFT_ASHIFT:
	      if (TARGET_H8300H)
		info->special = "sub.w\t%e0,%e0\n\trotr.l\t%S0\n\trotr.l\t%S0\n\tsub.w\t%f0,%f0";
	      else
		info->special = "sub.w\t%e0,%e0\n\trotr.l\t#2,%S0\n\tsub.w\t%f0,%f0";
	      goto end;
	    case SHIFT_LSHIFTRT:
	      if (TARGET_H8300H)
		info->special = "sub.w\t%f0,%f0\n\trotl.l\t%S0\n\trotl.l\t%S0\n\textu.l\t%S0";
	      else
		info->special = "sub.w\t%f0,%f0\n\trotl.l\t#2,%S0\n\textu.l\t%S0";
	      goto end;
	    case SHIFT_ASHIFTRT:
	      gcc_unreachable ();
	    }
	}
      else if (count == 31)
	{
	  if (TARGET_H8300)
	    {
	      switch (shift_type)
		{
		case SHIFT_ASHIFT:
		  info->special = "sub.w\t%e0,%e0\n\tshlr\t%w0\n\tmov.w\t%e0,%f0\n\trotxr\t%z0";
		  goto end;
		case SHIFT_LSHIFTRT:
		  info->special = "sub.w\t%f0,%f0\n\tshll\t%z0\n\tmov.w\t%f0,%e0\n\trotxl\t%w0";
		  goto end;
		case SHIFT_ASHIFTRT:
		  info->special = "shll\t%z0\n\tsubx\t%w0,%w0\n\tmov.b\t%w0,%x0\n\tmov.w\t%f0,%e0";
		  goto end;
		}
	    }
	  else
	    {
	      switch (shift_type)
		{
		case SHIFT_ASHIFT:
		  info->special = "shlr.l\t%S0\n\txor.l\t%S0,%S0\n\trotxr.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		  goto end;
		case SHIFT_LSHIFTRT:
		  info->special = "shll.l\t%S0\n\txor.l\t%S0,%S0\n\trotxl.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		  goto end;
		case SHIFT_ASHIFTRT:
		  info->special = "shll\t%e0\n\tsubx\t%w0,%w0\n\texts.w\t%T0\n\texts.l\t%S0";
		  info->cc_special = CC_SET_ZNV;
		  goto end;
		}
	    }
	}
      gcc_unreachable ();

    default:
      gcc_unreachable ();
    }

 end:
  if (!TARGET_H8300S)
    info->shift2 = NULL;
}

/* Given COUNT and MODE of a shift, return 1 if a scratch reg may be
   needed for some shift with COUNT and MODE.  Return 0 otherwise.  */

int
h8300_shift_needs_scratch_p (int count, enum machine_mode mode)
{
  enum h8_cpu cpu;
  int a, lr, ar;

  if (GET_MODE_BITSIZE (mode) <= count)
    return 1;

  /* Find out the target CPU.  */
  if (TARGET_H8300)
    cpu = H8_300;
  else if (TARGET_H8300H)
    cpu = H8_300H;
  else
    cpu = H8_S;

  /* Find the shift algorithm.  */
  switch (mode)
    {
    case QImode:
      a  = shift_alg_qi[cpu][SHIFT_ASHIFT][count];
      lr = shift_alg_qi[cpu][SHIFT_LSHIFTRT][count];
      ar = shift_alg_qi[cpu][SHIFT_ASHIFTRT][count];
      break;

    case HImode:
      a  = shift_alg_hi[cpu][SHIFT_ASHIFT][count];
      lr = shift_alg_hi[cpu][SHIFT_LSHIFTRT][count];
      ar = shift_alg_hi[cpu][SHIFT_ASHIFTRT][count];
      break;

    case SImode:
      a  = shift_alg_si[cpu][SHIFT_ASHIFT][count];
      lr = shift_alg_si[cpu][SHIFT_LSHIFTRT][count];
      ar = shift_alg_si[cpu][SHIFT_ASHIFTRT][count];
      break;

    default:
      gcc_unreachable ();
    }

  /* On H8/300H, count == 8 uses a scratch register.  */
  return (a == SHIFT_LOOP || lr == SHIFT_LOOP || ar == SHIFT_LOOP
	  || (TARGET_H8300H && mode == SImode && count == 8));
}

/* Output the assembler code for doing shifts.  */

const char *
output_a_shift (rtx *operands)
{
  static int loopend_lab;
  rtx shift = operands[3];
  enum machine_mode mode = GET_MODE (shift);
  enum rtx_code code = GET_CODE (shift);
  enum shift_type shift_type;
  enum shift_mode shift_mode;
  struct shift_info info;
  int n;

  loopend_lab++;

  switch (mode)
    {
    case QImode:
      shift_mode = QIshift;
      break;
    case HImode:
      shift_mode = HIshift;
      break;
    case SImode:
      shift_mode = SIshift;
      break;
    default:
      gcc_unreachable ();
    }

  switch (code)
    {
    case ASHIFTRT:
      shift_type = SHIFT_ASHIFTRT;
      break;
    case LSHIFTRT:
      shift_type = SHIFT_LSHIFTRT;
      break;
    case ASHIFT:
      shift_type = SHIFT_ASHIFT;
      break;
    default:
      gcc_unreachable ();
    }

  /* This case must be taken care of by one of the two splitters
     that convert a variable shift into a loop.  */
  gcc_assert (GET_CODE (operands[2]) == CONST_INT);
  
  n = INTVAL (operands[2]);

  /* If the count is negative, make it 0.  */
  if (n < 0)
    n = 0;
  /* If the count is too big, truncate it.
     ANSI says shifts of GET_MODE_BITSIZE are undefined - we choose to
     do the intuitive thing.  */
  else if ((unsigned int) n > GET_MODE_BITSIZE (mode))
    n = GET_MODE_BITSIZE (mode);

  get_shift_alg (shift_type, shift_mode, n, &info);
  
  switch (info.alg)
    {
    case SHIFT_SPECIAL:
      output_asm_insn (info.special, operands);
      /* Fall through.  */

    case SHIFT_INLINE:
      n = info.remainder;

      /* Emit two bit shifts first.  */
      if (info.shift2 != NULL)
	{
	  for (; n > 1; n -= 2)
	    output_asm_insn (info.shift2, operands);
	}

      /* Now emit one bit shifts for any residual.  */
      for (; n > 0; n--)
	output_asm_insn (info.shift1, operands);
      return "";
      
    case SHIFT_ROT_AND:
      {
	int m = GET_MODE_BITSIZE (mode) - n;
	const int mask = (shift_type == SHIFT_ASHIFT
			  ? ((1 << m) - 1) << n
			  : (1 << m) - 1);
	char insn_buf[200];

	/* Not all possibilities of rotate are supported.  They shouldn't
	   be generated, but let's watch for 'em.  */
	gcc_assert (info.shift1);
	
	/* Emit two bit rotates first.  */
	if (info.shift2 != NULL)
	  {
	    for (; m > 1; m -= 2)
	      output_asm_insn (info.shift2, operands);
	  }
	
	/* Now single bit rotates for any residual.  */
	for (; m > 0; m--)
	  output_asm_insn (info.shift1, operands);
	
	/* Now mask off the high bits.  */
	switch (mode)
	  {
	  case QImode:
	    sprintf (insn_buf, "and\t#%d,%%X0", mask);
	    break;

	  case HImode:
	    gcc_assert (TARGET_H8300H || TARGET_H8300S);
	    sprintf (insn_buf, "and.w\t#%d,%%T0", mask);
	    break;

	  default:
	    gcc_unreachable ();
	  }

	output_asm_insn (insn_buf, operands);
	return "";
      }

    case SHIFT_LOOP:
      /* A loop to shift by a "large" constant value.
	 If we have shift-by-2 insns, use them.  */
      if (info.shift2 != NULL)
	{
	  fprintf (asm_out_file, "\tmov.b	#%d,%sl\n", n / 2,
		   names_big[REGNO (operands[4])]);
	  fprintf (asm_out_file, ".Llt%d:\n", loopend_lab);
	  output_asm_insn (info.shift2, operands);
	  output_asm_insn ("add	#0xff,%X4", operands);
	  fprintf (asm_out_file, "\tbne	.Llt%d\n", loopend_lab);
	  if (n % 2)
	    output_asm_insn (info.shift1, operands);
	}
      else
	{
	  fprintf (asm_out_file, "\tmov.b	#%d,%sl\n", n,
		   names_big[REGNO (operands[4])]);
	  fprintf (asm_out_file, ".Llt%d:\n", loopend_lab);
	  output_asm_insn (info.shift1, operands);
	  output_asm_insn ("add	#0xff,%X4", operands);
	  fprintf (asm_out_file, "\tbne	.Llt%d\n", loopend_lab);
	}
      return "";
      
    default:
      gcc_unreachable ();
    }
}

/* Count the number of assembly instructions in a string TEMPLATE.  */

static unsigned int
h8300_asm_insn_count (const char *template)
{
  unsigned int count = 1;

  for (; *template; template++)
    if (*template == '\n')
      count++;

  return count;
}

/* Compute the length of a shift insn.  */

unsigned int
compute_a_shift_length (rtx insn ATTRIBUTE_UNUSED, rtx *operands)
{
  rtx shift = operands[3];
  enum machine_mode mode = GET_MODE (shift);
  enum rtx_code code = GET_CODE (shift);
  enum shift_type shift_type;
  enum shift_mode shift_mode;
  struct shift_info info;
  unsigned int wlength = 0;

  switch (mode)
    {
    case QImode:
      shift_mode = QIshift;
      break;
    case HImode:
      shift_mode = HIshift;
      break;
    case SImode:
      shift_mode = SIshift;
      break;
    default:
      gcc_unreachable ();
    }

  switch (code)
    {
    case ASHIFTRT:
      shift_type = SHIFT_ASHIFTRT;
      break;
    case LSHIFTRT:
      shift_type = SHIFT_LSHIFTRT;
      break;
    case ASHIFT:
      shift_type = SHIFT_ASHIFT;
      break;
    default:
      gcc_unreachable ();
    }

  if (GET_CODE (operands[2]) != CONST_INT)
    {
      /* Get the assembler code to do one shift.  */
      get_shift_alg (shift_type, shift_mode, 1, &info);

      return (4 + h8300_asm_insn_count (info.shift1)) * 2;
    }
  else
    {
      int n = INTVAL (operands[2]);

      /* If the count is negative, make it 0.  */
      if (n < 0)
	n = 0;
      /* If the count is too big, truncate it.
         ANSI says shifts of GET_MODE_BITSIZE are undefined - we choose to
	 do the intuitive thing.  */
      else if ((unsigned int) n > GET_MODE_BITSIZE (mode))
	n = GET_MODE_BITSIZE (mode);

      get_shift_alg (shift_type, shift_mode, n, &info);

      switch (info.alg)
	{
	case SHIFT_SPECIAL:
	  wlength += h8300_asm_insn_count (info.special);

	  /* Every assembly instruction used in SHIFT_SPECIAL case
	     takes 2 bytes except xor.l, which takes 4 bytes, so if we
	     see xor.l, we just pretend that xor.l counts as two insns
	     so that the insn length will be computed correctly.  */
	  if (strstr (info.special, "xor.l") != NULL)
	    wlength++;

	  /* Fall through.  */

	case SHIFT_INLINE:
	  n = info.remainder;

	  if (info.shift2 != NULL)
	    {
	      wlength += h8300_asm_insn_count (info.shift2) * (n / 2);
	      n = n % 2;
	    }

	  wlength += h8300_asm_insn_count (info.shift1) * n;

	  return 2 * wlength;

	case SHIFT_ROT_AND:
	  {
	    int m = GET_MODE_BITSIZE (mode) - n;

	    /* Not all possibilities of rotate are supported.  They shouldn't
	       be generated, but let's watch for 'em.  */
	    gcc_assert (info.shift1);

	    if (info.shift2 != NULL)
	      {
		wlength += h8300_asm_insn_count (info.shift2) * (m / 2);
		m = m % 2;
	      }

	    wlength += h8300_asm_insn_count (info.shift1) * m;

	    /* Now mask off the high bits.  */
	    switch (mode)
	      {
	      case QImode:
		wlength += 1;
		break;
	      case HImode:
		wlength += 2;
		break;
	      case SImode:
		gcc_assert (!TARGET_H8300);
		wlength += 3;
		break;
	      default:
		gcc_unreachable ();
	      }
	    return 2 * wlength;
	  }

	case SHIFT_LOOP:
	  /* A loop to shift by a "large" constant value.
	     If we have shift-by-2 insns, use them.  */
	  if (info.shift2 != NULL)
	    {
	      wlength += 3 + h8300_asm_insn_count (info.shift2);
	      if (n % 2)
		wlength += h8300_asm_insn_count (info.shift1);
	    }
	  else
	    {
	      wlength += 3 + h8300_asm_insn_count (info.shift1);
	    }
	  return 2 * wlength;

	default:
	  gcc_unreachable ();
	}
    }
}

/* Compute which flag bits are valid after a shift insn.  */

int
compute_a_shift_cc (rtx insn ATTRIBUTE_UNUSED, rtx *operands)
{
  rtx shift = operands[3];
  enum machine_mode mode = GET_MODE (shift);
  enum rtx_code code = GET_CODE (shift);
  enum shift_type shift_type;
  enum shift_mode shift_mode;
  struct shift_info info;
  int n;
  
  switch (mode)
    {
    case QImode:
      shift_mode = QIshift;
      break;
    case HImode:
      shift_mode = HIshift;
      break;
    case SImode:
      shift_mode = SIshift;
      break;
    default:
      gcc_unreachable ();
    }

  switch (code)
    {
    case ASHIFTRT:
      shift_type = SHIFT_ASHIFTRT;
      break;
    case LSHIFTRT:
      shift_type = SHIFT_LSHIFTRT;
      break;
    case ASHIFT:
      shift_type = SHIFT_ASHIFT;
      break;
    default:
      gcc_unreachable ();
    }

  /* This case must be taken care of by one of the two splitters
     that convert a variable shift into a loop.  */
  gcc_assert (GET_CODE (operands[2]) == CONST_INT);
  
  n = INTVAL (operands[2]);

  /* If the count is negative, make it 0.  */
  if (n < 0)
    n = 0;
  /* If the count is too big, truncate it.
     ANSI says shifts of GET_MODE_BITSIZE are undefined - we choose to
     do the intuitive thing.  */
  else if ((unsigned int) n > GET_MODE_BITSIZE (mode))
    n = GET_MODE_BITSIZE (mode);
  
  get_shift_alg (shift_type, shift_mode, n, &info);
  
  switch (info.alg)
    {
    case SHIFT_SPECIAL:
      if (info.remainder == 0)
	return info.cc_special;

      /* Fall through.  */

    case SHIFT_INLINE:
      return info.cc_inline;
      
    case SHIFT_ROT_AND:
      /* This case always ends with an and instruction.  */
      return CC_SET_ZNV;
      
    case SHIFT_LOOP:
      /* A loop to shift by a "large" constant value.
	 If we have shift-by-2 insns, use them.  */
      if (info.shift2 != NULL)
	{
	  if (n % 2)
	    return info.cc_inline;
	}
      return CC_CLOBBER;
      
    default:
      gcc_unreachable ();
    }
}

/* A rotation by a non-constant will cause a loop to be generated, in
   which a rotation by one bit is used.  A rotation by a constant,
   including the one in the loop, will be taken care of by
   output_a_rotate () at the insn emit time.  */

int
expand_a_rotate (rtx operands[])
{
  rtx dst = operands[0];
  rtx src = operands[1];
  rtx rotate_amount = operands[2];
  enum machine_mode mode = GET_MODE (dst);

  if (h8sx_classify_shift (mode, ROTATE, rotate_amount) == H8SX_SHIFT_UNARY)
    return false;

  /* We rotate in place.  */
  emit_move_insn (dst, src);

  if (GET_CODE (rotate_amount) != CONST_INT)
    {
      rtx counter = gen_reg_rtx (QImode);
      rtx start_label = gen_label_rtx ();
      rtx end_label = gen_label_rtx ();

      /* If the rotate amount is less than or equal to 0,
	 we go out of the loop.  */
      emit_cmp_and_jump_insns (rotate_amount, const0_rtx, LE, NULL_RTX,
			       QImode, 0, end_label);

      /* Initialize the loop counter.  */
      emit_move_insn (counter, rotate_amount);

      emit_label (start_label);

      /* Rotate by one bit.  */
      switch (mode)
	{
	case QImode:
	  emit_insn (gen_rotlqi3_1 (dst, dst, const1_rtx));
	  break;
	case HImode:
	  emit_insn (gen_rotlhi3_1 (dst, dst, const1_rtx));
	  break;
	case SImode:
	  emit_insn (gen_rotlsi3_1 (dst, dst, const1_rtx));
	  break;
	default:
	  gcc_unreachable ();
	}

      /* Decrement the counter by 1.  */
      emit_insn (gen_addqi3 (counter, counter, constm1_rtx));

      /* If the loop counter is nonzero, we go back to the beginning
	 of the loop.  */
      emit_cmp_and_jump_insns (counter, const0_rtx, NE, NULL_RTX, QImode, 1,
			       start_label);

      emit_label (end_label);
    }
  else
    {
      /* Rotate by AMOUNT bits.  */
      switch (mode)
	{
	case QImode:
	  emit_insn (gen_rotlqi3_1 (dst, dst, rotate_amount));
	  break;
	case HImode:
	  emit_insn (gen_rotlhi3_1 (dst, dst, rotate_amount));
	  break;
	case SImode:
	  emit_insn (gen_rotlsi3_1 (dst, dst, rotate_amount));
	  break;
	default:
	  gcc_unreachable ();
	}
    }

  return 1;
}

/* Output a rotate insn.  */

const char *
output_a_rotate (enum rtx_code code, rtx *operands)
{
  rtx dst = operands[0];
  rtx rotate_amount = operands[2];
  enum shift_mode rotate_mode;
  enum shift_type rotate_type;
  const char *insn_buf;
  int bits;
  int amount;
  enum machine_mode mode = GET_MODE (dst);

  gcc_assert (GET_CODE (rotate_amount) == CONST_INT);

  switch (mode)
    {
    case QImode:
      rotate_mode = QIshift;
      break;
    case HImode:
      rotate_mode = HIshift;
      break;
    case SImode:
      rotate_mode = SIshift;
      break;
    default:
      gcc_unreachable ();
    }

  switch (code)
    {
    case ROTATERT:
      rotate_type = SHIFT_ASHIFT;
      break;
    case ROTATE:
      rotate_type = SHIFT_LSHIFTRT;
      break;
    default:
      gcc_unreachable ();
    }

  amount = INTVAL (rotate_amount);

  /* Clean up AMOUNT.  */
  if (amount < 0)
    amount = 0;
  if ((unsigned int) amount > GET_MODE_BITSIZE (mode))
    amount = GET_MODE_BITSIZE (mode);

  /* Determine the faster direction.  After this phase, amount will be
     at most a half of GET_MODE_BITSIZE (mode).  */
  if ((unsigned int) amount > GET_MODE_BITSIZE (mode) / (unsigned) 2)
    {
      /* Flip the direction.  */
      amount = GET_MODE_BITSIZE (mode) - amount;
      rotate_type =
	(rotate_type == SHIFT_ASHIFT) ? SHIFT_LSHIFTRT : SHIFT_ASHIFT;
    }

  /* See if a byte swap (in HImode) or a word swap (in SImode) can
     boost up the rotation.  */
  if ((mode == HImode && TARGET_H8300 && amount >= 5)
      || (mode == HImode && TARGET_H8300H && amount >= 6)
      || (mode == HImode && TARGET_H8300S && amount == 8)
      || (mode == SImode && TARGET_H8300H && amount >= 10)
      || (mode == SImode && TARGET_H8300S && amount >= 13))
    {
      switch (mode)
	{
	case HImode:
	  /* This code works on any family.  */
	  insn_buf = "xor.b\t%s0,%t0\n\txor.b\t%t0,%s0\n\txor.b\t%s0,%t0";
	  output_asm_insn (insn_buf, operands);
	  break;

	case SImode:
	  /* This code works on the H8/300H and H8S.  */
	  insn_buf = "xor.w\t%e0,%f0\n\txor.w\t%f0,%e0\n\txor.w\t%e0,%f0";
	  output_asm_insn (insn_buf, operands);
	  break;

	default:
	  gcc_unreachable ();
	}

      /* Adjust AMOUNT and flip the direction.  */
      amount = GET_MODE_BITSIZE (mode) / 2 - amount;
      rotate_type =
	(rotate_type == SHIFT_ASHIFT) ? SHIFT_LSHIFTRT : SHIFT_ASHIFT;
    }

  /* Output rotate insns.  */
  for (bits = TARGET_H8300S ? 2 : 1; bits > 0; bits /= 2)
    {
      if (bits == 2)
	insn_buf = rotate_two[rotate_type][rotate_mode];
      else
	insn_buf = rotate_one[cpu_type][rotate_type][rotate_mode];

      for (; amount >= bits; amount -= bits)
	output_asm_insn (insn_buf, operands);
    }

  return "";
}

/* Compute the length of a rotate insn.  */

unsigned int
compute_a_rotate_length (rtx *operands)
{
  rtx src = operands[1];
  rtx amount_rtx = operands[2];
  enum machine_mode mode = GET_MODE (src);
  int amount;
  unsigned int length = 0;

  gcc_assert (GET_CODE (amount_rtx) == CONST_INT);

  amount = INTVAL (amount_rtx);

  /* Clean up AMOUNT.  */
  if (amount < 0)
    amount = 0;
  if ((unsigned int) amount > GET_MODE_BITSIZE (mode))
    amount = GET_MODE_BITSIZE (mode);

  /* Determine the faster direction.  After this phase, amount
     will be at most a half of GET_MODE_BITSIZE (mode).  */
  if ((unsigned int) amount > GET_MODE_BITSIZE (mode) / (unsigned) 2)
    /* Flip the direction.  */
    amount = GET_MODE_BITSIZE (mode) - amount;

  /* See if a byte swap (in HImode) or a word swap (in SImode) can
     boost up the rotation.  */
  if ((mode == HImode && TARGET_H8300 && amount >= 5)
      || (mode == HImode && TARGET_H8300H && amount >= 6)
      || (mode == HImode && TARGET_H8300S && amount == 8)
      || (mode == SImode && TARGET_H8300H && amount >= 10)
      || (mode == SImode && TARGET_H8300S && amount >= 13))
    {
      /* Adjust AMOUNT and flip the direction.  */
      amount = GET_MODE_BITSIZE (mode) / 2 - amount;
      length += 6;
    }

  /* We use 2-bit rotations on the H8S.  */
  if (TARGET_H8300S)
    amount = amount / 2 + amount % 2;

  /* The H8/300 uses three insns to rotate one bit, taking 6
     length.  */
  length += amount * ((TARGET_H8300 && mode == HImode) ? 6 : 2);

  return length;
}

/* Fix the operands of a gen_xxx so that it could become a bit
   operating insn.  */

int
fix_bit_operand (rtx *operands, enum rtx_code code)
{
  /* The bit_operand predicate accepts any memory during RTL generation, but
     only 'U' memory afterwards, so if this is a MEM operand, we must force
     it to be valid for 'U' by reloading the address.  */

  if (code == AND
      ? single_zero_operand (operands[2], QImode)
      : single_one_operand (operands[2], QImode))
    {
      /* OK to have a memory dest.  */
      if (GET_CODE (operands[0]) == MEM
	  && !OK_FOR_U (operands[0]))
	{
	  rtx mem = gen_rtx_MEM (GET_MODE (operands[0]),
				 copy_to_mode_reg (Pmode,
						   XEXP (operands[0], 0)));
	  MEM_COPY_ATTRIBUTES (mem, operands[0]);
	  operands[0] = mem;
	}

      if (GET_CODE (operands[1]) == MEM
	  && !OK_FOR_U (operands[1]))
	{
	  rtx mem = gen_rtx_MEM (GET_MODE (operands[1]),
				 copy_to_mode_reg (Pmode,
						   XEXP (operands[1], 0)));
	  MEM_COPY_ATTRIBUTES (mem, operands[0]);
	  operands[1] = mem;
	}
      return 0;
    }

  /* Dest and src op must be register.  */

  operands[1] = force_reg (QImode, operands[1]);
  {
    rtx res = gen_reg_rtx (QImode);
    switch (code)
      {
      case AND:
	emit_insn (gen_andqi3_1 (res, operands[1], operands[2]));
	break;
      case IOR:
	emit_insn (gen_iorqi3_1 (res, operands[1], operands[2]));
	break;
      case XOR:
	emit_insn (gen_xorqi3_1 (res, operands[1], operands[2]));
	break;
      default:
	gcc_unreachable ();
      }
    emit_insn (gen_movqi (operands[0], res));
  }
  return 1;
}

/* Return nonzero if FUNC is an interrupt function as specified
   by the "interrupt" attribute.  */

static int
h8300_interrupt_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("interrupt_handler", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return nonzero if FUNC is a saveall function as specified by the
   "saveall" attribute.  */

static int
h8300_saveall_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("saveall", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return nonzero if FUNC is an OS_Task function as specified
   by the "OS_Task" attribute.  */

static int
h8300_os_task_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("OS_Task", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return nonzero if FUNC is a monitor function as specified
   by the "monitor" attribute.  */

static int
h8300_monitor_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("monitor", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return nonzero if FUNC is a function that should be called
   through the function vector.  */

int
h8300_funcvec_function_p (tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return 0;

  a = lookup_attribute ("function_vector", DECL_ATTRIBUTES (func));
  return a != NULL_TREE;
}

/* Return nonzero if DECL is a variable that's in the eight bit
   data area.  */

int
h8300_eightbit_data_p (tree decl)
{
  tree a;

  if (TREE_CODE (decl) != VAR_DECL)
    return 0;

  a = lookup_attribute ("eightbit_data", DECL_ATTRIBUTES (decl));
  return a != NULL_TREE;
}

/* Return nonzero if DECL is a variable that's in the tiny
   data area.  */

int
h8300_tiny_data_p (tree decl)
{
  tree a;

  if (TREE_CODE (decl) != VAR_DECL)
    return 0;

  a = lookup_attribute ("tiny_data", DECL_ATTRIBUTES (decl));
  return a != NULL_TREE;
}

/* Generate an 'interrupt_handler' attribute for decls.  We convert
   all the pragmas to corresponding attributes.  */

static void
h8300_insert_attributes (tree node, tree *attributes)
{
  if (TREE_CODE (node) == FUNCTION_DECL)
    {
      if (pragma_interrupt)
	{
	  pragma_interrupt = 0;

	  /* Add an 'interrupt_handler' attribute.  */
	  *attributes = tree_cons (get_identifier ("interrupt_handler"),
				   NULL, *attributes);
	}

      if (pragma_saveall)
	{
	  pragma_saveall = 0;

	  /* Add an 'saveall' attribute.  */
	  *attributes = tree_cons (get_identifier ("saveall"),
				   NULL, *attributes);
	}
    }
}

/* Supported attributes:

   interrupt_handler: output a prologue and epilogue suitable for an
   interrupt handler.

   saveall: output a prologue and epilogue that saves and restores
   all registers except the stack pointer.

   function_vector: This function should be called through the
   function vector.

   eightbit_data: This variable lives in the 8-bit data area and can
   be referenced with 8-bit absolute memory addresses.

   tiny_data: This variable lives in the tiny data area and can be
   referenced with 16-bit absolute memory references.  */

const struct attribute_spec h8300_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt_handler", 0, 0, true,  false, false, h8300_handle_fndecl_attribute },
  { "saveall",           0, 0, true,  false, false, h8300_handle_fndecl_attribute },
  { "OS_Task",           0, 0, true,  false, false, h8300_handle_fndecl_attribute },
  { "monitor",           0, 0, true,  false, false, h8300_handle_fndecl_attribute },
  { "function_vector",   0, 0, true,  false, false, h8300_handle_fndecl_attribute },
  { "eightbit_data",     0, 0, true,  false, false, h8300_handle_eightbit_data_attribute },
  { "tiny_data",         0, 0, true,  false, false, h8300_handle_tiny_data_attribute },
  { NULL,                0, 0, false, false, false, NULL }
};


/* Handle an attribute requiring a FUNCTION_DECL; arguments as in
   struct attribute_spec.handler.  */
static tree
h8300_handle_fndecl_attribute (tree *node, tree name,
			       tree args ATTRIBUTE_UNUSED,
			       int flags ATTRIBUTE_UNUSED,
			       bool *no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute only applies to functions",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle an "eightbit_data" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
h8300_handle_eightbit_data_attribute (tree *node, tree name,
				      tree args ATTRIBUTE_UNUSED,
				      int flags ATTRIBUTE_UNUSED,
				      bool *no_add_attrs)
{
  tree decl = *node;

  if (TREE_STATIC (decl) || DECL_EXTERNAL (decl))
    {
      DECL_SECTION_NAME (decl) = build_string (7, ".eight");
    }
  else
    {
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle an "tiny_data" attribute; arguments as in
   struct attribute_spec.handler.  */
static tree
h8300_handle_tiny_data_attribute (tree *node, tree name,
				  tree args ATTRIBUTE_UNUSED,
				  int flags ATTRIBUTE_UNUSED,
				  bool *no_add_attrs)
{
  tree decl = *node;

  if (TREE_STATIC (decl) || DECL_EXTERNAL (decl))
    {
      DECL_SECTION_NAME (decl) = build_string (6, ".tiny");
    }
  else
    {
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Mark function vectors, and various small data objects.  */

static void
h8300_encode_section_info (tree decl, rtx rtl, int first)
{
  int extra_flags = 0;

  default_encode_section_info (decl, rtl, first);

  if (TREE_CODE (decl) == FUNCTION_DECL
      && h8300_funcvec_function_p (decl))
    extra_flags = SYMBOL_FLAG_FUNCVEC_FUNCTION;
  else if (TREE_CODE (decl) == VAR_DECL
	   && (TREE_STATIC (decl) || DECL_EXTERNAL (decl)))
    {
      if (h8300_eightbit_data_p (decl))
	extra_flags = SYMBOL_FLAG_EIGHTBIT_DATA;
      else if (first && h8300_tiny_data_p (decl))
	extra_flags = SYMBOL_FLAG_TINY_DATA;
    }

  if (extra_flags)
    SYMBOL_REF_FLAGS (XEXP (rtl, 0)) |= extra_flags;
}

/* Output a single-bit extraction.  */

const char *
output_simode_bld (int bild, rtx operands[])
{
  if (TARGET_H8300)
    {
      /* Clear the destination register.  */
      output_asm_insn ("sub.w\t%e0,%e0\n\tsub.w\t%f0,%f0", operands);

      /* Now output the bit load or bit inverse load, and store it in
	 the destination.  */
      if (bild)
	output_asm_insn ("bild\t%Z2,%Y1", operands);
      else
	output_asm_insn ("bld\t%Z2,%Y1", operands);

      output_asm_insn ("bst\t#0,%w0", operands);
    }
  else
    {
      /* Determine if we can clear the destination first.  */
      int clear_first = (REG_P (operands[0]) && REG_P (operands[1])
			 && REGNO (operands[0]) != REGNO (operands[1]));

      if (clear_first)
	output_asm_insn ("sub.l\t%S0,%S0", operands);

      /* Output the bit load or bit inverse load.  */
      if (bild)
	output_asm_insn ("bild\t%Z2,%Y1", operands);
      else
	output_asm_insn ("bld\t%Z2,%Y1", operands);

      if (!clear_first)
	output_asm_insn ("xor.l\t%S0,%S0", operands);

      /* Perform the bit store.  */
      output_asm_insn ("rotxl.l\t%S0", operands);
    }

  /* All done.  */
  return "";
}

/* Delayed-branch scheduling is more effective if we have some idea
   how long each instruction will be.  Use a shorten_branches pass
   to get an initial estimate.  */

static void
h8300_reorg (void)
{
  if (flag_delayed_branch)
    shorten_branches (get_insns ());
}

#ifndef OBJECT_FORMAT_ELF
static void
h8300_asm_named_section (const char *name, unsigned int flags ATTRIBUTE_UNUSED,
			 tree decl)
{
  /* ??? Perhaps we should be using default_coff_asm_named_section.  */
  fprintf (asm_out_file, "\t.section %s\n", name);
}
#endif /* ! OBJECT_FORMAT_ELF */

/* Nonzero if X is a constant address suitable as an 8-bit absolute,
   which is a special case of the 'R' operand.  */

int
h8300_eightbit_constant_address_p (rtx x)
{
  /* The ranges of the 8-bit area.  */
  const unsigned HOST_WIDE_INT n1 = trunc_int_for_mode (0xff00, HImode);
  const unsigned HOST_WIDE_INT n2 = trunc_int_for_mode (0xffff, HImode);
  const unsigned HOST_WIDE_INT h1 = trunc_int_for_mode (0x00ffff00, SImode);
  const unsigned HOST_WIDE_INT h2 = trunc_int_for_mode (0x00ffffff, SImode);
  const unsigned HOST_WIDE_INT s1 = trunc_int_for_mode (0xffffff00, SImode);
  const unsigned HOST_WIDE_INT s2 = trunc_int_for_mode (0xffffffff, SImode);

  unsigned HOST_WIDE_INT addr;

  /* We accept symbols declared with eightbit_data.  */
  if (GET_CODE (x) == SYMBOL_REF)
    return (SYMBOL_REF_FLAGS (x) & SYMBOL_FLAG_EIGHTBIT_DATA) != 0;

  if (GET_CODE (x) != CONST_INT)
    return 0;

  addr = INTVAL (x);

  return (0
	  || ((TARGET_H8300 || TARGET_NORMAL_MODE) && IN_RANGE (addr, n1, n2))
	  || (TARGET_H8300H && IN_RANGE (addr, h1, h2))
	  || (TARGET_H8300S && IN_RANGE (addr, s1, s2)));
}

/* Nonzero if X is a constant address suitable as an 16-bit absolute
   on H8/300H and H8S.  */

int
h8300_tiny_constant_address_p (rtx x)
{
  /* The ranges of the 16-bit area.  */
  const unsigned HOST_WIDE_INT h1 = trunc_int_for_mode (0x00000000, SImode);
  const unsigned HOST_WIDE_INT h2 = trunc_int_for_mode (0x00007fff, SImode);
  const unsigned HOST_WIDE_INT h3 = trunc_int_for_mode (0x00ff8000, SImode);
  const unsigned HOST_WIDE_INT h4 = trunc_int_for_mode (0x00ffffff, SImode);
  const unsigned HOST_WIDE_INT s1 = trunc_int_for_mode (0x00000000, SImode);
  const unsigned HOST_WIDE_INT s2 = trunc_int_for_mode (0x00007fff, SImode);
  const unsigned HOST_WIDE_INT s3 = trunc_int_for_mode (0xffff8000, SImode);
  const unsigned HOST_WIDE_INT s4 = trunc_int_for_mode (0xffffffff, SImode);

  unsigned HOST_WIDE_INT addr;

  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
      /* In the normal mode, any symbol fits in the 16-bit absolute
	 address range.  We also accept symbols declared with
	 tiny_data.  */
      return (TARGET_NORMAL_MODE
	      || (SYMBOL_REF_FLAGS (x) & SYMBOL_FLAG_TINY_DATA) != 0);

    case CONST_INT:
      addr = INTVAL (x);
      return (TARGET_NORMAL_MODE
	      || (TARGET_H8300H
		  && (IN_RANGE (addr, h1, h2) || IN_RANGE (addr, h3, h4)))
	      || (TARGET_H8300S
		  && (IN_RANGE (addr, s1, s2) || IN_RANGE (addr, s3, s4))));

    case CONST:
      return TARGET_NORMAL_MODE;

    default:
      return 0;
    }

}

/* Return nonzero if ADDR1 and ADDR2 point to consecutive memory
   locations that can be accessed as a 16-bit word.  */

int
byte_accesses_mergeable_p (rtx addr1, rtx addr2)
{
  HOST_WIDE_INT offset1, offset2;
  rtx reg1, reg2;

  if (REG_P (addr1))
    {
      reg1 = addr1;
      offset1 = 0;
    }
  else if (GET_CODE (addr1) == PLUS
	   && REG_P (XEXP (addr1, 0))
	   && GET_CODE (XEXP (addr1, 1)) == CONST_INT)
    {
      reg1 = XEXP (addr1, 0);
      offset1 = INTVAL (XEXP (addr1, 1));
    }
  else
    return 0;

  if (REG_P (addr2))
    {
      reg2 = addr2;
      offset2 = 0;
    }
  else if (GET_CODE (addr2) == PLUS
	   && REG_P (XEXP (addr2, 0))
	   && GET_CODE (XEXP (addr2, 1)) == CONST_INT)
    {
      reg2 = XEXP (addr2, 0);
      offset2 = INTVAL (XEXP (addr2, 1));
    }
  else
    return 0;

  if (((reg1 == stack_pointer_rtx && reg2 == stack_pointer_rtx)
       || (reg1 == frame_pointer_rtx && reg2 == frame_pointer_rtx))
      && offset1 % 2 == 0
      && offset1 + 1 == offset2)
    return 1;

  return 0;
}

/* Return nonzero if we have the same comparison insn as I3 two insns
   before I3.  I3 is assumed to be a comparison insn.  */

int
same_cmp_preceding_p (rtx i3)
{
  rtx i1, i2;

  /* Make sure we have a sequence of three insns.  */
  i2 = prev_nonnote_insn (i3);
  if (i2 == NULL_RTX)
    return 0;
  i1 = prev_nonnote_insn (i2);
  if (i1 == NULL_RTX)
    return 0;

  return (INSN_P (i1) && rtx_equal_p (PATTERN (i1), PATTERN (i3))
	  && any_condjump_p (i2) && onlyjump_p (i2));
}

/* Return nonzero if we have the same comparison insn as I1 two insns
   after I1.  I1 is assumed to be a comparison insn.  */

int
same_cmp_following_p (rtx i1)
{
  rtx i2, i3;

  /* Make sure we have a sequence of three insns.  */
  i2 = next_nonnote_insn (i1);
  if (i2 == NULL_RTX)
    return 0;
  i3 = next_nonnote_insn (i2);
  if (i3 == NULL_RTX)
    return 0;

  return (INSN_P (i3) && rtx_equal_p (PATTERN (i1), PATTERN (i3))
	  && any_condjump_p (i2) && onlyjump_p (i2));
}

/* Return nonzero if OPERANDS are valid for stm (or ldm) that pushes
   (or pops) N registers.  OPERANDS are assumed to be an array of
   registers.  */

int
h8300_regs_ok_for_stm (int n, rtx operands[])
{
  switch (n)
    {
    case 2:
      return ((REGNO (operands[0]) == 0 && REGNO (operands[1]) == 1)
	      || (REGNO (operands[0]) == 2 && REGNO (operands[1]) == 3)
	      || (REGNO (operands[0]) == 4 && REGNO (operands[1]) == 5));
    case 3:
      return ((REGNO (operands[0]) == 0
	       && REGNO (operands[1]) == 1
	       && REGNO (operands[2]) == 2)
	      || (REGNO (operands[0]) == 4
		  && REGNO (operands[1]) == 5
		  && REGNO (operands[2]) == 6));

    case 4:
      return (REGNO (operands[0]) == 0
	      && REGNO (operands[1]) == 1
	      && REGNO (operands[2]) == 2
	      && REGNO (operands[3]) == 3);
    default:
      gcc_unreachable ();
    }
}

/* Return nonzero if register OLD_REG can be renamed to register NEW_REG.  */

int
h8300_hard_regno_rename_ok (unsigned int old_reg ATTRIBUTE_UNUSED,
			    unsigned int new_reg)
{
  /* Interrupt functions can only use registers that have already been
     saved by the prologue, even if they would normally be
     call-clobbered.  */

  if (h8300_current_function_interrupt_function_p ()
      && !regs_ever_live[new_reg])
    return 0;

  return 1;
}

/* Return nonzero if X is a legitimate constant.  */

int
h8300_legitimate_constant_p (rtx x ATTRIBUTE_UNUSED)
{
  return 1;
}

/* Return nonzero if X is a REG or SUBREG suitable as a base register.  */

static int
h8300_rtx_ok_for_base_p (rtx x, int strict)
{
  /* Strip off SUBREG if any.  */
  if (GET_CODE (x) == SUBREG)
    x = SUBREG_REG (x);

  return (REG_P (x)
	  && (strict
	      ? REG_OK_FOR_BASE_STRICT_P (x)
	      : REG_OK_FOR_BASE_NONSTRICT_P (x)));
}

/* Return nozero if X is a legitimate address.  On the H8/300, a
   legitimate address has the form REG, REG+CONSTANT_ADDRESS or
   CONSTANT_ADDRESS.  */

int
h8300_legitimate_address_p (enum machine_mode mode, rtx x, int strict)
{
  /* The register indirect addresses like @er0 is always valid.  */
  if (h8300_rtx_ok_for_base_p (x, strict))
    return 1;

  if (CONSTANT_ADDRESS_P (x))
    return 1;

  if (TARGET_H8300SX
      && (   GET_CODE (x) == PRE_INC
	  || GET_CODE (x) == PRE_DEC
	  || GET_CODE (x) == POST_INC
	  || GET_CODE (x) == POST_DEC)
      && h8300_rtx_ok_for_base_p (XEXP (x, 0), strict))
    return 1;

  if (GET_CODE (x) == PLUS
      && CONSTANT_ADDRESS_P (XEXP (x, 1))
      && h8300_rtx_ok_for_base_p (h8300_get_index (XEXP (x, 0),
						   mode, 0), strict))
    return 1;

  return 0;
}

/* Worker function for HARD_REGNO_NREGS.

   We pretend the MAC register is 32bits -- we don't have any data
   types on the H8 series to handle more than 32bits.  */

int
h8300_hard_regno_nregs (int regno ATTRIBUTE_UNUSED, enum machine_mode mode)
{
  return (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
}

/* Worker function for HARD_REGNO_MODE_OK.  */

int
h8300_hard_regno_mode_ok (int regno, enum machine_mode mode)
{
  if (TARGET_H8300)
    /* If an even reg, then anything goes.  Otherwise the mode must be
       QI or HI.  */
    return ((regno & 1) == 0) || (mode == HImode) || (mode == QImode);
  else
    /* MAC register can only be of SImode.  Otherwise, anything
       goes.  */
    return regno == MAC_REG ? mode == SImode : 1;
}

/* Perform target dependent optabs initialization.  */
static void
h8300_init_libfuncs (void)
{
  set_optab_libfunc (smul_optab, HImode, "__mulhi3");
  set_optab_libfunc (sdiv_optab, HImode, "__divhi3");
  set_optab_libfunc (udiv_optab, HImode, "__udivhi3");
  set_optab_libfunc (smod_optab, HImode, "__modhi3");
  set_optab_libfunc (umod_optab, HImode, "__umodhi3");
}

/* Worker function for TARGET_RETURN_IN_MEMORY.  */

static bool
h8300_return_in_memory (tree type, tree fntype ATTRIBUTE_UNUSED)
{
  return (TYPE_MODE (type) == BLKmode
	  || GET_MODE_SIZE (TYPE_MODE (type)) > (TARGET_H8300 ? 4 : 8));
}

/* Initialize the GCC target structure.  */
#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE h8300_attribute_table

#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START h8300_file_start
#undef TARGET_ASM_FILE_START_FILE_DIRECTIVE
#define TARGET_ASM_FILE_START_FILE_DIRECTIVE true

#undef TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END h8300_file_end

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO h8300_encode_section_info

#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES h8300_insert_attributes

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS h8300_rtx_costs

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS h8300_init_libfuncs

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY h8300_return_in_memory

#undef  TARGET_MACHINE_DEPENDENT_REORG
#define TARGET_MACHINE_DEPENDENT_REORG h8300_reorg

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS TARGET_DEFAULT

struct gcc_target targetm = TARGET_INITIALIZER;
