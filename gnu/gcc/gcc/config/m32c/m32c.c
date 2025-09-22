/* Target Code for R8C/M16C/M32C
   Copyright (C) 2005
   Free Software Foundation, Inc.
   Contributed by Red Hat.

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
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-flags.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "recog.h"
#include "reload.h"
#include "toplev.h"
#include "obstack.h"
#include "tree.h"
#include "expr.h"
#include "optabs.h"
#include "except.h"
#include "function.h"
#include "ggc.h"
#include "target.h"
#include "target-def.h"
#include "tm_p.h"
#include "langhooks.h"
#include "tree-gimple.h"

/* Prototypes */

/* Used by m32c_pushm_popm.  */
typedef enum
{
  PP_pushm,
  PP_popm,
  PP_justcount
} Push_Pop_Type;

static tree interrupt_handler (tree *, tree, tree, int, bool *);
static int interrupt_p (tree node);
static bool m32c_asm_integer (rtx, unsigned int, int);
static int m32c_comp_type_attributes (tree, tree);
static bool m32c_fixed_condition_code_regs (unsigned int *, unsigned int *);
static struct machine_function *m32c_init_machine_status (void);
static void m32c_insert_attributes (tree, tree *);
static bool m32c_pass_by_reference (CUMULATIVE_ARGS *, enum machine_mode,
				    tree, bool);
static bool m32c_promote_prototypes (tree);
static int m32c_pushm_popm (Push_Pop_Type);
static bool m32c_strict_argument_naming (CUMULATIVE_ARGS *);
static rtx m32c_struct_value_rtx (tree, int);
static rtx m32c_subreg (enum machine_mode, rtx, enum machine_mode, int);
static int need_to_save (int);

#define streq(a,b) (strcmp ((a), (b)) == 0)

/* Internal support routines */

/* Debugging statements are tagged with DEBUG0 only so that they can
   be easily enabled individually, by replacing the '0' with '1' as
   needed.  */
#define DEBUG0 0
#define DEBUG1 1

#if DEBUG0
/* This is needed by some of the commented-out debug statements
   below.  */
static char const *class_names[LIM_REG_CLASSES] = REG_CLASS_NAMES;
#endif
static int class_contents[LIM_REG_CLASSES][1] = REG_CLASS_CONTENTS;

/* These are all to support encode_pattern().  */
static char pattern[30], *patternp;
static GTY(()) rtx patternr[30];
#define RTX_IS(x) (streq (pattern, x))

/* Some macros to simplify the logic throughout this file.  */
#define IS_MEM_REGNO(regno) ((regno) >= MEM0_REGNO && (regno) <= MEM7_REGNO)
#define IS_MEM_REG(rtx) (GET_CODE (rtx) == REG && IS_MEM_REGNO (REGNO (rtx)))

#define IS_CR_REGNO(regno) ((regno) >= SB_REGNO && (regno) <= PC_REGNO)
#define IS_CR_REG(rtx) (GET_CODE (rtx) == REG && IS_CR_REGNO (REGNO (rtx)))

/* We do most RTX matching by converting the RTX into a string, and
   using string compares.  This vastly simplifies the logic in many of
   the functions in this file.

   On exit, pattern[] has the encoded string (use RTX_IS("...") to
   compare it) and patternr[] has pointers to the nodes in the RTX
   corresponding to each character in the encoded string.  The latter
   is mostly used by print_operand().

   Unrecognized patterns have '?' in them; this shows up when the
   assembler complains about syntax errors.
*/

static void
encode_pattern_1 (rtx x)
{
  int i;

  if (patternp == pattern + sizeof (pattern) - 2)
    {
      patternp[-1] = '?';
      return;
    }

  patternr[patternp - pattern] = x;

  switch (GET_CODE (x))
    {
    case REG:
      *patternp++ = 'r';
      break;
    case SUBREG:
      if (GET_MODE_SIZE (GET_MODE (x)) !=
	  GET_MODE_SIZE (GET_MODE (XEXP (x, 0))))
	*patternp++ = 'S';
      encode_pattern_1 (XEXP (x, 0));
      break;
    case MEM:
      *patternp++ = 'm';
    case CONST:
      encode_pattern_1 (XEXP (x, 0));
      break;
    case PLUS:
      *patternp++ = '+';
      encode_pattern_1 (XEXP (x, 0));
      encode_pattern_1 (XEXP (x, 1));
      break;
    case PRE_DEC:
      *patternp++ = '>';
      encode_pattern_1 (XEXP (x, 0));
      break;
    case POST_INC:
      *patternp++ = '<';
      encode_pattern_1 (XEXP (x, 0));
      break;
    case LO_SUM:
      *patternp++ = 'L';
      encode_pattern_1 (XEXP (x, 0));
      encode_pattern_1 (XEXP (x, 1));
      break;
    case HIGH:
      *patternp++ = 'H';
      encode_pattern_1 (XEXP (x, 0));
      break;
    case SYMBOL_REF:
      *patternp++ = 's';
      break;
    case LABEL_REF:
      *patternp++ = 'l';
      break;
    case CODE_LABEL:
      *patternp++ = 'c';
      break;
    case CONST_INT:
    case CONST_DOUBLE:
      *patternp++ = 'i';
      break;
    case UNSPEC:
      *patternp++ = 'u';
      *patternp++ = '0' + XCINT (x, 1, UNSPEC);
      for (i = 0; i < XVECLEN (x, 0); i++)
	encode_pattern_1 (XVECEXP (x, 0, i));
      break;
    case USE:
      *patternp++ = 'U';
      break;
    case PARALLEL:
      *patternp++ = '|';
      for (i = 0; i < XVECLEN (x, 0); i++)
	encode_pattern_1 (XVECEXP (x, 0, i));
      break;
    case EXPR_LIST:
      *patternp++ = 'E';
      encode_pattern_1 (XEXP (x, 0));
      if (XEXP (x, 1))
	encode_pattern_1 (XEXP (x, 1));
      break;
    default:
      *patternp++ = '?';
#if DEBUG0
      fprintf (stderr, "can't encode pattern %s\n",
	       GET_RTX_NAME (GET_CODE (x)));
      debug_rtx (x);
      gcc_unreachable ();
#endif
      break;
    }
}

static void
encode_pattern (rtx x)
{
  patternp = pattern;
  encode_pattern_1 (x);
  *patternp = 0;
}

/* Since register names indicate the mode they're used in, we need a
   way to determine which name to refer to the register with.  Called
   by print_operand().  */

static const char *
reg_name_with_mode (int regno, enum machine_mode mode)
{
  int mlen = GET_MODE_SIZE (mode);
  if (regno == R0_REGNO && mlen == 1)
    return "r0l";
  if (regno == R0_REGNO && (mlen == 3 || mlen == 4))
    return "r2r0";
  if (regno == R0_REGNO && mlen == 6)
    return "r2r1r0";
  if (regno == R0_REGNO && mlen == 8)
    return "r3r1r2r0";
  if (regno == R1_REGNO && mlen == 1)
    return "r1l";
  if (regno == R1_REGNO && (mlen == 3 || mlen == 4))
    return "r3r1";
  if (regno == A0_REGNO && TARGET_A16 && (mlen == 3 || mlen == 4))
    return "a1a0";
  return reg_names[regno];
}

/* How many bytes a register uses on stack when it's pushed.  We need
   to know this because the push opcode needs to explicitly indicate
   the size of the register, even though the name of the register
   already tells it that.  Used by m32c_output_reg_{push,pop}, which
   is only used through calls to ASM_OUTPUT_REG_{PUSH,POP}.  */

static int
reg_push_size (int regno)
{
  switch (regno)
    {
    case R0_REGNO:
    case R1_REGNO:
      return 2;
    case R2_REGNO:
    case R3_REGNO:
    case FLG_REGNO:
      return 2;
    case A0_REGNO:
    case A1_REGNO:
    case SB_REGNO:
    case FB_REGNO:
    case SP_REGNO:
      if (TARGET_A16)
	return 2;
      else
	return 3;
    default:
      gcc_unreachable ();
    }
}

static int *class_sizes = 0;

/* Given two register classes, find the largest intersection between
   them.  If there is no intersection, return RETURNED_IF_EMPTY
   instead.  */
static int
reduce_class (int original_class, int limiting_class, int returned_if_empty)
{
  int cc = class_contents[original_class][0];
  int i, best = NO_REGS;
  int best_size = 0;

  if (original_class == limiting_class)
    return original_class;

  if (!class_sizes)
    {
      int r;
      class_sizes = (int *) xmalloc (LIM_REG_CLASSES * sizeof (int));
      for (i = 0; i < LIM_REG_CLASSES; i++)
	{
	  class_sizes[i] = 0;
	  for (r = 0; r < FIRST_PSEUDO_REGISTER; r++)
	    if (class_contents[i][0] & (1 << r))
	      class_sizes[i]++;
	}
    }

  cc &= class_contents[limiting_class][0];
  for (i = 0; i < LIM_REG_CLASSES; i++)
    {
      int ic = class_contents[i][0];

      if ((~cc & ic) == 0)
	if (best_size < class_sizes[i])
	  {
	    best = i;
	    best_size = class_sizes[i];
	  }

    }
  if (best == NO_REGS)
    return returned_if_empty;
  return best;
}

/* Returns TRUE If there are any registers that exist in both register
   classes.  */
static int
classes_intersect (int class1, int class2)
{
  return class_contents[class1][0] & class_contents[class2][0];
}

/* Used by m32c_register_move_cost to determine if a move is
   impossibly expensive.  */
static int
class_can_hold_mode (int class, enum machine_mode mode)
{
  /* Cache the results:  0=untested  1=no  2=yes */
  static char results[LIM_REG_CLASSES][MAX_MACHINE_MODE];
  if (results[class][mode] == 0)
    {
      int r, n, i;
      results[class][mode] = 1;
      for (r = 0; r < FIRST_PSEUDO_REGISTER; r++)
	if (class_contents[class][0] & (1 << r)
	    && HARD_REGNO_MODE_OK (r, mode))
	  {
	    int ok = 1;
	    n = HARD_REGNO_NREGS (r, mode);
	    for (i = 1; i < n; i++)
	      if (!(class_contents[class][0] & (1 << (r + i))))
		ok = 0;
	    if (ok)
	      {
		results[class][mode] = 2;
		break;
	      }
	  }
    }
#if DEBUG0
  fprintf (stderr, "class %s can hold %s? %s\n",
	   class_names[class], mode_name[mode],
	   (results[class][mode] == 2) ? "yes" : "no");
#endif
  return results[class][mode] == 2;
}

/* Run-time Target Specification.  */

/* Memregs are memory locations that gcc treats like general
   registers, as there are a limited number of true registers and the
   m32c families can use memory in most places that registers can be
   used.

   However, since memory accesses are more expensive than registers,
   we allow the user to limit the number of memregs available, in
   order to try to persuade gcc to try harder to use real registers.

   Memregs are provided by m32c-lib1.S.
*/

int target_memregs = 16;
static bool target_memregs_set = FALSE;
int ok_to_change_target_memregs = TRUE;

#undef  TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION m32c_handle_option
static bool
m32c_handle_option (size_t code,
		    const char *arg ATTRIBUTE_UNUSED,
		    int value ATTRIBUTE_UNUSED)
{
  if (code == OPT_memregs_)
    {
      target_memregs_set = TRUE;
      target_memregs = atoi (arg);
    }
  return TRUE;
}

/* Implements OVERRIDE_OPTIONS.  We limit memregs to 0..16, and
   provide a default.  */
void
m32c_override_options (void)
{
  if (target_memregs_set)
    {
      if (target_memregs < 0 || target_memregs > 16)
	error ("invalid target memregs value '%d'", target_memregs);
    }
  else
    target_memregs = 16;
}

/* Defining data structures for per-function information */

/* The usual; we set up our machine_function data.  */
static struct machine_function *
m32c_init_machine_status (void)
{
  struct machine_function *machine;
  machine =
    (machine_function *) ggc_alloc_cleared (sizeof (machine_function));

  return machine;
}

/* Implements INIT_EXPANDERS.  We just set up to call the above
   function.  */
void
m32c_init_expanders (void)
{
  init_machine_status = m32c_init_machine_status;
}

/* Storage Layout */

#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN m32c_promote_function_return
bool
m32c_promote_function_return (tree fntype ATTRIBUTE_UNUSED)
{
  return false;
}

/* Register Basics */

/* Basic Characteristics of Registers */

/* Whether a mode fits in a register is complex enough to warrant a
   table.  */
static struct
{
  char qi_regs;
  char hi_regs;
  char pi_regs;
  char si_regs;
  char di_regs;
} nregs_table[FIRST_PSEUDO_REGISTER] =
{
  { 1, 1, 2, 2, 4 },		/* r0 */
  { 0, 1, 0, 0, 0 },		/* r2 */
  { 1, 1, 2, 2, 0 },		/* r1 */
  { 0, 1, 0, 0, 0 },		/* r3 */
  { 0, 1, 1, 0, 0 },		/* a0 */
  { 0, 1, 1, 0, 0 },		/* a1 */
  { 0, 1, 1, 0, 0 },		/* sb */
  { 0, 1, 1, 0, 0 },		/* fb */
  { 0, 1, 1, 0, 0 },		/* sp */
  { 1, 1, 1, 0, 0 },		/* pc */
  { 0, 0, 0, 0, 0 },		/* fl */
  { 1, 1, 1, 0, 0 },		/* ap */
  { 1, 1, 2, 2, 4 },		/* mem0 */
  { 1, 1, 2, 2, 4 },		/* mem1 */
  { 1, 1, 2, 2, 4 },		/* mem2 */
  { 1, 1, 2, 2, 4 },		/* mem3 */
  { 1, 1, 2, 2, 4 },		/* mem4 */
  { 1, 1, 2, 2, 0 },		/* mem5 */
  { 1, 1, 2, 2, 0 },		/* mem6 */
  { 1, 1, 0, 0, 0 },		/* mem7 */
};

/* Implements CONDITIONAL_REGISTER_USAGE.  We adjust the number of
   available memregs, and select which registers need to be preserved
   across calls based on the chip family.  */

void
m32c_conditional_register_usage (void)
{
  int i;

  if (0 <= target_memregs && target_memregs <= 16)
    {
      /* The command line option is bytes, but our "registers" are
	 16-bit words.  */
      for (i = target_memregs/2; i < 8; i++)
	{
	  fixed_regs[MEM0_REGNO + i] = 1;
	  CLEAR_HARD_REG_BIT (reg_class_contents[MEM_REGS], MEM0_REGNO + i);
	}
    }

  /* M32CM and M32C preserve more registers across function calls.  */
  if (TARGET_A24)
    {
      call_used_regs[R1_REGNO] = 0;
      call_used_regs[R2_REGNO] = 0;
      call_used_regs[R3_REGNO] = 0;
      call_used_regs[A0_REGNO] = 0;
      call_used_regs[A1_REGNO] = 0;
    }
}

/* How Values Fit in Registers */

/* Implements HARD_REGNO_NREGS.  This is complicated by the fact that
   different registers are different sizes from each other, *and* may
   be different sizes in different chip families.  */
int
m32c_hard_regno_nregs (int regno, enum machine_mode mode)
{
  if (regno == FLG_REGNO && mode == CCmode)
    return 1;
  if (regno >= FIRST_PSEUDO_REGISTER)
    return ((GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD);

  if (regno >= MEM0_REGNO && regno <= MEM7_REGNO)
    return (GET_MODE_SIZE (mode) + 1) / 2;

  if (GET_MODE_SIZE (mode) <= 1)
    return nregs_table[regno].qi_regs;
  if (GET_MODE_SIZE (mode) <= 2)
    return nregs_table[regno].hi_regs;
  if (regno == A0_REGNO && mode == PSImode && TARGET_A16)
    return 2;
  if ((GET_MODE_SIZE (mode) <= 3 || mode == PSImode) && TARGET_A24)
    return nregs_table[regno].pi_regs;
  if (GET_MODE_SIZE (mode) <= 4)
    return nregs_table[regno].si_regs;
  if (GET_MODE_SIZE (mode) <= 8)
    return nregs_table[regno].di_regs;
  return 0;
}

/* Implements HARD_REGNO_MODE_OK.  The above function does the work
   already; just test its return value.  */
int
m32c_hard_regno_ok (int regno, enum machine_mode mode)
{
  return m32c_hard_regno_nregs (regno, mode) != 0;
}

/* Implements MODES_TIEABLE_P.  In general, modes aren't tieable since
   registers are all different sizes.  However, since most modes are
   bigger than our registers anyway, it's easier to implement this
   function that way, leaving QImode as the only unique case.  */
int
m32c_modes_tieable_p (enum machine_mode m1, enum machine_mode m2)
{
  if (GET_MODE_SIZE (m1) == GET_MODE_SIZE (m2))
    return 1;

#if 0
  if (m1 == QImode || m2 == QImode)
    return 0;
#endif

  return 1;
}

/* Register Classes */

/* Implements REGNO_REG_CLASS.  */
enum machine_mode
m32c_regno_reg_class (int regno)
{
  switch (regno)
    {
    case R0_REGNO:
      return R0_REGS;
    case R1_REGNO:
      return R1_REGS;
    case R2_REGNO:
      return R2_REGS;
    case R3_REGNO:
      return R3_REGS;
    case A0_REGNO:
    case A1_REGNO:
      return A_REGS;
    case SB_REGNO:
      return SB_REGS;
    case FB_REGNO:
      return FB_REGS;
    case SP_REGNO:
      return SP_REGS;
    case FLG_REGNO:
      return FLG_REGS;
    default:
      if (IS_MEM_REGNO (regno))
	return MEM_REGS;
      return ALL_REGS;
    }
}

/* Implements REG_CLASS_FROM_CONSTRAINT.  Note that some constraints only match
   for certain chip families.  */
int
m32c_reg_class_from_constraint (char c ATTRIBUTE_UNUSED, const char *s)
{
  if (memcmp (s, "Rsp", 3) == 0)
    return SP_REGS;
  if (memcmp (s, "Rfb", 3) == 0)
    return FB_REGS;
  if (memcmp (s, "Rsb", 3) == 0)
    return SB_REGS;
  if (memcmp (s, "Rcr", 3) == 0)
    return TARGET_A16 ? CR_REGS : NO_REGS;
  if (memcmp (s, "Rcl", 3) == 0)
    return TARGET_A24 ? CR_REGS : NO_REGS;
  if (memcmp (s, "R0w", 3) == 0)
    return R0_REGS;
  if (memcmp (s, "R1w", 3) == 0)
    return R1_REGS;
  if (memcmp (s, "R2w", 3) == 0)
    return R2_REGS;
  if (memcmp (s, "R3w", 3) == 0)
    return R3_REGS;
  if (memcmp (s, "R02", 3) == 0)
    return R02_REGS;
  if (memcmp (s, "R03", 3) == 0)
    return R03_REGS;
  if (memcmp (s, "Rdi", 3) == 0)
    return DI_REGS;
  if (memcmp (s, "Rhl", 3) == 0)
    return HL_REGS;
  if (memcmp (s, "R23", 3) == 0)
    return R23_REGS;
  if (memcmp (s, "Ra0", 3) == 0)
    return A0_REGS;
  if (memcmp (s, "Ra1", 3) == 0)
    return A1_REGS;
  if (memcmp (s, "Raa", 3) == 0)
    return A_REGS;
  if (memcmp (s, "Raw", 3) == 0)
    return TARGET_A16 ? A_REGS : NO_REGS;
  if (memcmp (s, "Ral", 3) == 0)
    return TARGET_A24 ? A_REGS : NO_REGS;
  if (memcmp (s, "Rqi", 3) == 0)
    return QI_REGS;
  if (memcmp (s, "Rad", 3) == 0)
    return AD_REGS;
  if (memcmp (s, "Rsi", 3) == 0)
    return SI_REGS;
  if (memcmp (s, "Rhi", 3) == 0)
    return HI_REGS;
  if (memcmp (s, "Rhc", 3) == 0)
    return HC_REGS;
  if (memcmp (s, "Rra", 3) == 0)
    return RA_REGS;
  if (memcmp (s, "Rfl", 3) == 0)
    return FLG_REGS;
  if (memcmp (s, "Rmm", 3) == 0)
    {
      if (fixed_regs[MEM0_REGNO])
	return NO_REGS;
      return MEM_REGS;
    }

  /* PSImode registers - i.e. whatever can hold a pointer.  */
  if (memcmp (s, "Rpi", 3) == 0)
    {
      if (TARGET_A16)
	return HI_REGS;
      else
	return RA_REGS; /* r2r0 and r3r1 can hold pointers.  */
    }

  /* We handle this one as an EXTRA_CONSTRAINT.  */
  if (memcmp (s, "Rpa", 3) == 0)
    return NO_REGS;

  if (*s == 'R')
    {
      fprintf(stderr, "unrecognized R constraint: %.3s\n", s);
      gcc_unreachable();
    }

  return NO_REGS;
}

/* Implements REGNO_OK_FOR_BASE_P.  */
int
m32c_regno_ok_for_base_p (int regno)
{
  if (regno == A0_REGNO
      || regno == A1_REGNO || regno >= FIRST_PSEUDO_REGISTER)
    return 1;
  return 0;
}

#define DEBUG_RELOAD 0

/* Implements PREFERRED_RELOAD_CLASS.  In general, prefer general
   registers of the appropriate size.  */
int
m32c_preferred_reload_class (rtx x, int rclass)
{
  int newclass = rclass;

#if DEBUG_RELOAD
  fprintf (stderr, "\npreferred_reload_class for %s is ",
	   class_names[rclass]);
#endif
  if (rclass == NO_REGS)
    rclass = GET_MODE (x) == QImode ? HL_REGS : R03_REGS;

  if (classes_intersect (rclass, CR_REGS))
    {
      switch (GET_MODE (x))
	{
	case QImode:
	  newclass = HL_REGS;
	  break;
	default:
	  /*      newclass = HI_REGS; */
	  break;
	}
    }

  else if (newclass == QI_REGS && GET_MODE_SIZE (GET_MODE (x)) > 2)
    newclass = SI_REGS;
  else if (GET_MODE_SIZE (GET_MODE (x)) > 4
	   && ~class_contents[rclass][0] & 0x000f)
    newclass = DI_REGS;

  rclass = reduce_class (rclass, newclass, rclass);

  if (GET_MODE (x) == QImode)
    rclass = reduce_class (rclass, HL_REGS, rclass);

#if DEBUG_RELOAD
  fprintf (stderr, "%s\n", class_names[rclass]);
  debug_rtx (x);

  if (GET_CODE (x) == MEM
      && GET_CODE (XEXP (x, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (x, 0), 0)) == PLUS)
    fprintf (stderr, "Glorm!\n");
#endif
  return rclass;
}

/* Implements PREFERRED_OUTPUT_RELOAD_CLASS.  */
int
m32c_preferred_output_reload_class (rtx x, int rclass)
{
  return m32c_preferred_reload_class (x, rclass);
}

/* Implements LIMIT_RELOAD_CLASS.  We basically want to avoid using
   address registers for reloads since they're needed for address
   reloads.  */
int
m32c_limit_reload_class (enum machine_mode mode, int rclass)
{
#if DEBUG_RELOAD
  fprintf (stderr, "limit_reload_class for %s: %s ->",
	   mode_name[mode], class_names[rclass]);
#endif

  if (mode == QImode)
    rclass = reduce_class (rclass, HL_REGS, rclass);
  else if (mode == HImode)
    rclass = reduce_class (rclass, HI_REGS, rclass);
  else if (mode == SImode)
    rclass = reduce_class (rclass, SI_REGS, rclass);

  if (rclass != A_REGS)
    rclass = reduce_class (rclass, DI_REGS, rclass);

#if DEBUG_RELOAD
  fprintf (stderr, " %s\n", class_names[rclass]);
#endif
  return rclass;
}

/* Implements SECONDARY_RELOAD_CLASS.  QImode have to be reloaded in
   r0 or r1, as those are the only real QImode registers.  CR regs get
   reloaded through appropriately sized general or address
   registers.  */
int
m32c_secondary_reload_class (int rclass, enum machine_mode mode, rtx x)
{
  int cc = class_contents[rclass][0];
#if DEBUG0
  fprintf (stderr, "\nsecondary reload class %s %s\n",
	   class_names[rclass], mode_name[mode]);
  debug_rtx (x);
#endif
  if (mode == QImode
      && GET_CODE (x) == MEM && (cc & ~class_contents[R23_REGS][0]) == 0)
    return QI_REGS;
  if (classes_intersect (rclass, CR_REGS)
      && GET_CODE (x) == REG
      && REGNO (x) >= SB_REGNO && REGNO (x) <= SP_REGNO)
    return TARGET_A16 ? HI_REGS : A_REGS;
  return NO_REGS;
}

/* Implements CLASS_LIKELY_SPILLED_P.  A_REGS is needed for address
   reloads.  */
int
m32c_class_likely_spilled_p (int regclass)
{
  if (regclass == A_REGS)
    return 1;
  return reg_class_size[regclass] == 1;
}

/* Implements CLASS_MAX_NREGS.  We calculate this according to its
   documented meaning, to avoid potential inconsistencies with actual
   class definitions.  */
int
m32c_class_max_nregs (int regclass, enum machine_mode mode)
{
  int rn, max = 0;

  for (rn = 0; rn < FIRST_PSEUDO_REGISTER; rn++)
    if (class_contents[regclass][0] & (1 << rn))
      {
	int n = m32c_hard_regno_nregs (rn, mode);
	if (max < n)
	  max = n;
      }
  return max;
}

/* Implements CANNOT_CHANGE_MODE_CLASS.  Only r0 and r1 can change to
   QI (r0l, r1l) because the chip doesn't support QI ops on other
   registers (well, it does on a0/a1 but if we let gcc do that, reload
   suffers).  Otherwise, we allow changes to larger modes.  */
int
m32c_cannot_change_mode_class (enum machine_mode from,
			       enum machine_mode to, int rclass)
{
#if DEBUG0
  fprintf (stderr, "cannot change from %s to %s in %s\n",
	   mode_name[from], mode_name[to], class_names[rclass]);
#endif

  if (to == QImode)
    return (class_contents[rclass][0] & 0x1ffa);

  if (class_contents[rclass][0] & 0x0005	/* r0, r1 */
      && GET_MODE_SIZE (from) > 1)
    return 0;
  if (GET_MODE_SIZE (from) > 2)	/* all other regs */
    return 0;

  return 1;
}

/* Helpers for the rest of the file.  */
/* TRUE if the rtx is a REG rtx for the given register.  */
#define IS_REG(rtx,regno) (GET_CODE (rtx) == REG \
			   && REGNO (rtx) == regno)
/* TRUE if the rtx is a pseudo - specifically, one we can use as a
   base register in address calculations (hence the "strict"
   argument).  */
#define IS_PSEUDO(rtx,strict) (!strict && GET_CODE (rtx) == REG \
			       && (REGNO (rtx) == AP_REGNO \
				   || REGNO (rtx) >= FIRST_PSEUDO_REGISTER))

/* Implements CONST_OK_FOR_CONSTRAINT_P.  Currently, all constant
   constraints start with 'I', with the next two characters indicating
   the type and size of the range allowed.  */
int
m32c_const_ok_for_constraint_p (HOST_WIDE_INT value,
				char c ATTRIBUTE_UNUSED, const char *str)
{
  /* s=signed u=unsigned n=nonzero m=minus l=log2able,
     [sun] bits [SUN] bytes, p=pointer size
     I[-0-9][0-9] matches that number */
  if (memcmp (str, "Is3", 3) == 0)
    {
      return (-8 <= value && value <= 7);
    }
  if (memcmp (str, "IS1", 3) == 0)
    {
      return (-128 <= value && value <= 127);
    }
  if (memcmp (str, "IS2", 3) == 0)
    {
      return (-32768 <= value && value <= 32767);
    }
  if (memcmp (str, "IU2", 3) == 0)
    {
      return (0 <= value && value <= 65535);
    }
  if (memcmp (str, "IU3", 3) == 0)
    {
      return (0 <= value && value <= 0x00ffffff);
    }
  if (memcmp (str, "In4", 3) == 0)
    {
      return (-8 <= value && value && value <= 8);
    }
  if (memcmp (str, "In5", 3) == 0)
    {
      return (-16 <= value && value && value <= 16);
    }
  if (memcmp (str, "In6", 3) == 0)
    {
      return (-32 <= value && value && value <= 32);
    }
  if (memcmp (str, "IM2", 3) == 0)
    {
      return (-65536 <= value && value && value <= -1);
    }
  if (memcmp (str, "Ilb", 3) == 0)
    {
      int b = exact_log2 (value);
      return (b >= 0 && b <= 7);
    }
  if (memcmp (str, "Imb", 3) == 0)
    {
      int b = exact_log2 ((value ^ 0xff) & 0xff);
      return (b >= 0 && b <= 7);
    }
  if (memcmp (str, "Ilw", 3) == 0)
    {
      int b = exact_log2 (value);
      return (b >= 0 && b <= 15);
    }
  if (memcmp (str, "Imw", 3) == 0)
    {
      int b = exact_log2 ((value ^ 0xffff) & 0xffff);
      return (b >= 0 && b <= 15);
    }
  if (memcmp (str, "I00", 3) == 0)
    {
      return (value == 0);
    }
  return 0;
}

/* Implements EXTRA_CONSTRAINT_STR (see next function too).  'S' is
   for memory constraints, plus "Rpa" for PARALLEL rtx's we use for
   call return values.  */
int
m32c_extra_constraint_p2 (rtx value, char c ATTRIBUTE_UNUSED, const char *str)
{
  encode_pattern (value);
  if (memcmp (str, "Sd", 2) == 0)
    {
      /* This is the common "src/dest" address */
      rtx r;
      if (GET_CODE (value) == MEM && CONSTANT_P (XEXP (value, 0)))
	return 1;
      if (RTX_IS ("ms") || RTX_IS ("m+si"))
	return 1;
      if (RTX_IS ("m++rii"))
	{
	  if (REGNO (patternr[3]) == FB_REGNO
	      && INTVAL (patternr[4]) == 0)
	    return 1;
	}
      if (RTX_IS ("mr"))
	r = patternr[1];
      else if (RTX_IS ("m+ri") || RTX_IS ("m+rs") || RTX_IS ("m+r+si"))
	r = patternr[2];
      else
	return 0;
      if (REGNO (r) == SP_REGNO)
	return 0;
      return m32c_legitimate_address_p (GET_MODE (value), XEXP (value, 0), 1);
    }
  else if (memcmp (str, "Sa", 2) == 0)
    {
      rtx r;
      if (RTX_IS ("mr"))
	r = patternr[1];
      else if (RTX_IS ("m+ri"))
	r = patternr[2];
      else
	return 0;
      return (IS_REG (r, A0_REGNO) || IS_REG (r, A1_REGNO));
    }
  else if (memcmp (str, "Si", 2) == 0)
    {
      return (RTX_IS ("mi") || RTX_IS ("ms") || RTX_IS ("m+si"));
    }
  else if (memcmp (str, "Ss", 2) == 0)
    {
      return ((RTX_IS ("mr")
	       && (IS_REG (patternr[1], SP_REGNO)))
	      || (RTX_IS ("m+ri") && (IS_REG (patternr[2], SP_REGNO))));
    }
  else if (memcmp (str, "Sf", 2) == 0)
    {
      return ((RTX_IS ("mr")
	       && (IS_REG (patternr[1], FB_REGNO)))
	      || (RTX_IS ("m+ri") && (IS_REG (patternr[2], FB_REGNO))));
    }
  else if (memcmp (str, "Sb", 2) == 0)
    {
      return ((RTX_IS ("mr")
	       && (IS_REG (patternr[1], SB_REGNO)))
	      || (RTX_IS ("m+ri") && (IS_REG (patternr[2], SB_REGNO))));
    }
  else if (memcmp (str, "Sp", 2) == 0)
    {
      /* Absolute addresses 0..0x1fff used for bit addressing (I/O ports) */
      return (RTX_IS ("mi")
	      && !(INTVAL (patternr[1]) & ~0x1fff));
    }
  else if (memcmp (str, "S1", 2) == 0)
    {
      return r1h_operand (value, QImode);
    }

  gcc_assert (str[0] != 'S');

  if (memcmp (str, "Rpa", 2) == 0)
    return GET_CODE (value) == PARALLEL;

  return 0;
}

/* This is for when we're debugging the above.  */
int
m32c_extra_constraint_p (rtx value, char c, const char *str)
{
  int rv = m32c_extra_constraint_p2 (value, c, str);
#if DEBUG0
  fprintf (stderr, "\nconstraint %.*s: %d\n", CONSTRAINT_LEN (c, str), str,
	   rv);
  debug_rtx (value);
#endif
  return rv;
}

/* Implements EXTRA_MEMORY_CONSTRAINT.  Currently, we only use strings
   starting with 'S'.  */
int
m32c_extra_memory_constraint (char c, const char *str ATTRIBUTE_UNUSED)
{
  return c == 'S';
}

/* Implements EXTRA_ADDRESS_CONSTRAINT.  We reserve 'A' strings for these,
   but don't currently define any.  */
int
m32c_extra_address_constraint (char c, const char *str ATTRIBUTE_UNUSED)
{
  return c == 'A';
}

/* STACK AND CALLING */

/* Frame Layout */

/* Implements RETURN_ADDR_RTX.  Note that R8C and M16C push 24 bits
   (yes, THREE bytes) onto the stack for the return address, but we
   don't support pointers bigger than 16 bits on those chips.  This
   will likely wreak havoc with exception unwinding.  FIXME.  */
rtx
m32c_return_addr_rtx (int count)
{
  enum machine_mode mode;
  int offset;
  rtx ra_mem;

  if (count)
    return NULL_RTX;
  /* we want 2[$fb] */

  if (TARGET_A24)
    {
      mode = SImode;
      offset = 4;
    }
  else
    {
      /* FIXME: it's really 3 bytes */
      mode = HImode;
      offset = 2;
    }

  ra_mem =
    gen_rtx_MEM (mode, plus_constant (gen_rtx_REG (Pmode, FP_REGNO), offset));
  return copy_to_mode_reg (mode, ra_mem);
}

/* Implements INCOMING_RETURN_ADDR_RTX.  See comment above.  */
rtx
m32c_incoming_return_addr_rtx (void)
{
  /* we want [sp] */
  return gen_rtx_MEM (PSImode, gen_rtx_REG (PSImode, SP_REGNO));
}

/* Exception Handling Support */

/* Implements EH_RETURN_DATA_REGNO.  Choose registers able to hold
   pointers.  */
int
m32c_eh_return_data_regno (int n)
{
  switch (n)
    {
    case 0:
      return A0_REGNO;
    case 1:
      return A1_REGNO;
    default:
      return INVALID_REGNUM;
    }
}

/* Implements EH_RETURN_STACKADJ_RTX.  Saved and used later in
   m32c_emit_eh_epilogue.  */
rtx
m32c_eh_return_stackadj_rtx (void)
{
  if (!cfun->machine->eh_stack_adjust)
    {
      rtx sa;

      sa = gen_reg_rtx (Pmode);
      cfun->machine->eh_stack_adjust = sa;
    }
  return cfun->machine->eh_stack_adjust;
}

/* Registers That Address the Stack Frame */

/* Implements DWARF_FRAME_REGNUM and DBX_REGISTER_NUMBER.  Note that
   the original spec called for dwarf numbers to vary with register
   width as well, for example, r0l, r0, and r2r0 would each have
   different dwarf numbers.  GCC doesn't support this, and we don't do
   it, and gdb seems to like it this way anyway.  */
unsigned int
m32c_dwarf_frame_regnum (int n)
{
  switch (n)
    {
    case R0_REGNO:
      return 5;
    case R1_REGNO:
      return 6;
    case R2_REGNO:
      return 7;
    case R3_REGNO:
      return 8;
    case A0_REGNO:
      return 9;
    case A1_REGNO:
      return 10;
    case FB_REGNO:
      return 11;
    case SB_REGNO:
      return 19;

    case SP_REGNO:
      return 12;
    case PC_REGNO:
      return 13;
    default:
      return DWARF_FRAME_REGISTERS + 1;
    }
}

/* The frame looks like this:

   ap -> +------------------------------
         | Return address (3 or 4 bytes)
	 | Saved FB (2 or 4 bytes)
   fb -> +------------------------------
	 | local vars
         | register saves fb
	 |        through r0 as needed
   sp -> +------------------------------
*/

/* We use this to wrap all emitted insns in the prologue.  */
static rtx
F (rtx x)
{
  RTX_FRAME_RELATED_P (x) = 1;
  return x;
}

/* This maps register numbers to the PUSHM/POPM bitfield, and tells us
   how much the stack pointer moves for each, for each cpu family.  */
static struct
{
  int reg1;
  int bit;
  int a16_bytes;
  int a24_bytes;
} pushm_info[] =
{
  /* These are in reverse push (nearest-to-sp) order.  */
  { R0_REGNO, 0x80, 2, 2 },
  { R1_REGNO, 0x40, 2, 2 },
  { R2_REGNO, 0x20, 2, 2 },
  { R3_REGNO, 0x10, 2, 2 },
  { A0_REGNO, 0x08, 2, 4 },
  { A1_REGNO, 0x04, 2, 4 },
  { SB_REGNO, 0x02, 2, 4 },
  { FB_REGNO, 0x01, 2, 4 }
};

#define PUSHM_N (sizeof(pushm_info)/sizeof(pushm_info[0]))

/* Returns TRUE if we need to save/restore the given register.  We
   save everything for exception handlers, so that any register can be
   unwound.  For interrupt handlers, we save everything if the handler
   calls something else (because we don't know what *that* function
   might do), but try to be a bit smarter if the handler is a leaf
   function.  We always save $a0, though, because we use that in the
   epilog to copy $fb to $sp.  */
static int
need_to_save (int regno)
{
  if (fixed_regs[regno])
    return 0;
  if (cfun->calls_eh_return)
    return 1;
  if (regno == FP_REGNO)
    return 0;
  if (cfun->machine->is_interrupt
      && (!cfun->machine->is_leaf || regno == A0_REGNO))
    return 1;
  if (regs_ever_live[regno]
      && (!call_used_regs[regno] || cfun->machine->is_interrupt))
    return 1;
  return 0;
}

/* This function contains all the intelligence about saving and
   restoring registers.  It always figures out the register save set.
   When called with PP_justcount, it merely returns the size of the
   save set (for eliminating the frame pointer, for example).  When
   called with PP_pushm or PP_popm, it emits the appropriate
   instructions for saving (pushm) or restoring (popm) the
   registers.  */
static int
m32c_pushm_popm (Push_Pop_Type ppt)
{
  int reg_mask = 0;
  int byte_count = 0, bytes;
  int i;
  rtx dwarf_set[PUSHM_N];
  int n_dwarfs = 0;
  int nosave_mask = 0;

  if (cfun->return_rtx
      && GET_CODE (cfun->return_rtx) == PARALLEL
      && !(cfun->calls_eh_return || cfun->machine->is_interrupt))
    {
      rtx exp = XVECEXP (cfun->return_rtx, 0, 0);
      rtx rv = XEXP (exp, 0);
      int rv_bytes = GET_MODE_SIZE (GET_MODE (rv));

      if (rv_bytes > 2)
	nosave_mask |= 0x20;	/* PSI, SI */
      else
	nosave_mask |= 0xf0;	/* DF */
      if (rv_bytes > 4)
	nosave_mask |= 0x50;	/* DI */
    }

  for (i = 0; i < (int) PUSHM_N; i++)
    {
      /* Skip if neither register needs saving.  */
      if (!need_to_save (pushm_info[i].reg1))
	continue;

      if (pushm_info[i].bit & nosave_mask)
	continue;

      reg_mask |= pushm_info[i].bit;
      bytes = TARGET_A16 ? pushm_info[i].a16_bytes : pushm_info[i].a24_bytes;

      if (ppt == PP_pushm)
	{
	  enum machine_mode mode = (bytes == 2) ? HImode : SImode;
	  rtx addr;

	  /* Always use stack_pointer_rtx instead of calling
	     rtx_gen_REG ourselves.  Code elsewhere in GCC assumes
	     that there is a single rtx representing the stack pointer,
	     namely stack_pointer_rtx, and uses == to recognize it.  */
	  addr = stack_pointer_rtx;

	  if (byte_count != 0)
	    addr = gen_rtx_PLUS (GET_MODE (addr), addr, GEN_INT (byte_count));

	  dwarf_set[n_dwarfs++] =
	    gen_rtx_SET (VOIDmode,
			 gen_rtx_MEM (mode, addr),
			 gen_rtx_REG (mode, pushm_info[i].reg1));
	  F (dwarf_set[n_dwarfs - 1]);

	}
      byte_count += bytes;
    }

  if (cfun->machine->is_interrupt)
    {
      cfun->machine->intr_pushm = reg_mask & 0xfe;
      reg_mask = 0;
      byte_count = 0;
    }

  if (cfun->machine->is_interrupt)
    for (i = MEM0_REGNO; i <= MEM7_REGNO; i++)
      if (need_to_save (i))
	{
	  byte_count += 2;
	  cfun->machine->intr_pushmem[i - MEM0_REGNO] = 1;
	}

  if (ppt == PP_pushm && byte_count)
    {
      rtx note = gen_rtx_SEQUENCE (VOIDmode, rtvec_alloc (n_dwarfs + 1));
      rtx pushm;

      if (reg_mask)
	{
	  XVECEXP (note, 0, 0)
	    = gen_rtx_SET (VOIDmode,
			   stack_pointer_rtx,
			   gen_rtx_PLUS (GET_MODE (stack_pointer_rtx),
					 stack_pointer_rtx,
					 GEN_INT (-byte_count)));
	  F (XVECEXP (note, 0, 0));

	  for (i = 0; i < n_dwarfs; i++)
	    XVECEXP (note, 0, i + 1) = dwarf_set[i];

	  pushm = F (emit_insn (gen_pushm (GEN_INT (reg_mask))));

	  REG_NOTES (pushm) = gen_rtx_EXPR_LIST (REG_FRAME_RELATED_EXPR, note,
						 REG_NOTES (pushm));
	}

      if (cfun->machine->is_interrupt)
	for (i = MEM0_REGNO; i <= MEM7_REGNO; i++)
	  if (cfun->machine->intr_pushmem[i - MEM0_REGNO])
	    {
	      if (TARGET_A16)
		pushm = emit_insn (gen_pushhi_16 (gen_rtx_REG (HImode, i)));
	      else
		pushm = emit_insn (gen_pushhi_24 (gen_rtx_REG (HImode, i)));
	      F (pushm);
	    }
    }
  if (ppt == PP_popm && byte_count)
    {
      if (cfun->machine->is_interrupt)
	for (i = MEM7_REGNO; i >= MEM0_REGNO; i--)
	  if (cfun->machine->intr_pushmem[i - MEM0_REGNO])
	    {
	      if (TARGET_A16)
		emit_insn (gen_pophi_16 (gen_rtx_REG (HImode, i)));
	      else
		emit_insn (gen_pophi_24 (gen_rtx_REG (HImode, i)));
	    }
      if (reg_mask)
	emit_insn (gen_popm (GEN_INT (reg_mask)));
    }

  return byte_count;
}

/* Implements INITIAL_ELIMINATION_OFFSET.  See the comment above that
   diagrams our call frame.  */
int
m32c_initial_elimination_offset (int from, int to)
{
  int ofs = 0;

  if (from == AP_REGNO)
    {
      if (TARGET_A16)
	ofs += 5;
      else
	ofs += 8;
    }

  if (to == SP_REGNO)
    {
      ofs += m32c_pushm_popm (PP_justcount);
      ofs += get_frame_size ();
    }

  /* Account for push rounding.  */
  if (TARGET_A24)
    ofs = (ofs + 1) & ~1;
#if DEBUG0
  fprintf (stderr, "initial_elimination_offset from=%d to=%d, ofs=%d\n", from,
	   to, ofs);
#endif
  return ofs;
}

/* Passing Function Arguments on the Stack */

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES m32c_promote_prototypes
static bool
m32c_promote_prototypes (tree fntype ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Implements PUSH_ROUNDING.  The R8C and M16C have byte stacks, the
   M32C has word stacks.  */
int
m32c_push_rounding (int n)
{
  if (TARGET_R8C || TARGET_M16C)
    return n;
  return (n + 1) & ~1;
}

/* Passing Arguments in Registers */

/* Implements FUNCTION_ARG.  Arguments are passed partly in registers,
   partly on stack.  If our function returns a struct, a pointer to a
   buffer for it is at the top of the stack (last thing pushed).  The
   first few real arguments may be in registers as follows:

   R8C/M16C:	arg1 in r1 if it's QI or HI (else it's pushed on stack)
		arg2 in r2 if it's HI (else pushed on stack)
		rest on stack
   M32C:        arg1 in r0 if it's QI or HI (else it's pushed on stack)
		rest on stack

   Structs are not passed in registers, even if they fit.  Only
   integer and pointer types are passed in registers.

   Note that when arg1 doesn't fit in r1, arg2 may still be passed in
   r2 if it fits.  */
rtx
m32c_function_arg (CUMULATIVE_ARGS * ca,
		   enum machine_mode mode, tree type, int named)
{
  /* Can return a reg, parallel, or 0 for stack */
  rtx rv = NULL_RTX;
#if DEBUG0
  fprintf (stderr, "func_arg %d (%s, %d)\n",
	   ca->parm_num, mode_name[mode], named);
  debug_tree (type);
#endif

  if (mode == VOIDmode)
    return GEN_INT (0);

  if (ca->force_mem || !named)
    {
#if DEBUG0
      fprintf (stderr, "func arg: force %d named %d, mem\n", ca->force_mem,
	       named);
#endif
      return NULL_RTX;
    }

  if (type && INTEGRAL_TYPE_P (type) && POINTER_TYPE_P (type))
    return NULL_RTX;

  if (type && AGGREGATE_TYPE_P (type))
    return NULL_RTX;

  switch (ca->parm_num)
    {
    case 1:
      if (GET_MODE_SIZE (mode) == 1 || GET_MODE_SIZE (mode) == 2)
	rv = gen_rtx_REG (mode, TARGET_A16 ? R1_REGNO : R0_REGNO);
      break;

    case 2:
      if (TARGET_A16 && GET_MODE_SIZE (mode) == 2)
	rv = gen_rtx_REG (mode, R2_REGNO);
      break;
    }

#if DEBUG0
  debug_rtx (rv);
#endif
  return rv;
}

#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE m32c_pass_by_reference
static bool
m32c_pass_by_reference (CUMULATIVE_ARGS * ca ATTRIBUTE_UNUSED,
			enum machine_mode mode ATTRIBUTE_UNUSED,
			tree type ATTRIBUTE_UNUSED,
			bool named ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Implements INIT_CUMULATIVE_ARGS.  */
void
m32c_init_cumulative_args (CUMULATIVE_ARGS * ca,
			   tree fntype,
			   rtx libname ATTRIBUTE_UNUSED,
			   tree fndecl,
			   int n_named_args ATTRIBUTE_UNUSED)
{
  if (fntype && aggregate_value_p (TREE_TYPE (fntype), fndecl))
    ca->force_mem = 1;
  else
    ca->force_mem = 0;
  ca->parm_num = 1;
}

/* Implements FUNCTION_ARG_ADVANCE.  force_mem is set for functions
   returning structures, so we always reset that.  Otherwise, we only
   need to know the sequence number of the argument to know what to do
   with it.  */
void
m32c_function_arg_advance (CUMULATIVE_ARGS * ca,
			   enum machine_mode mode ATTRIBUTE_UNUSED,
			   tree type ATTRIBUTE_UNUSED,
			   int named ATTRIBUTE_UNUSED)
{
  if (ca->force_mem)
    ca->force_mem = 0;
  else
    ca->parm_num++;
}

/* Implements FUNCTION_ARG_REGNO_P.  */
int
m32c_function_arg_regno_p (int r)
{
  if (TARGET_A24)
    return (r == R0_REGNO);
  return (r == R1_REGNO || r == R2_REGNO);
}

/* HImode and PSImode are the two "native" modes as far as GCC is
   concerned, but the chips also support a 32 bit mode which is used
   for some opcodes in R8C/M16C and for reset vectors and such.  */
#undef TARGET_VALID_POINTER_MODE
#define TARGET_VALID_POINTER_MODE m32c_valid_pointer_mode
static bool
m32c_valid_pointer_mode (enum machine_mode mode)
{
  if (mode == HImode
      || mode == PSImode
      || mode == SImode
      )
    return 1;
  return 0;
}

/* How Scalar Function Values Are Returned */

/* Implements LIBCALL_VALUE.  Most values are returned in $r0, or some
   combination of registers starting there (r2r0 for longs, r3r1r2r0
   for long long, r3r2r1r0 for doubles), except that that ABI
   currently doesn't work because it ends up using all available
   general registers and gcc often can't compile it.  So, instead, we
   return anything bigger than 16 bits in "mem0" (effectively, a
   memory location).  */
rtx
m32c_libcall_value (enum machine_mode mode)
{
  /* return reg or parallel */
#if 0
  /* FIXME: GCC has difficulty returning large values in registers,
     because that ties up most of the general registers and gives the
     register allocator little to work with.  Until we can resolve
     this, large values are returned in memory.  */
  if (mode == DFmode)
    {
      rtx rv;

      rv = gen_rtx_PARALLEL (mode, rtvec_alloc (4));
      XVECEXP (rv, 0, 0) = gen_rtx_EXPR_LIST (VOIDmode,
					      gen_rtx_REG (HImode,
							   R0_REGNO),
					      GEN_INT (0));
      XVECEXP (rv, 0, 1) = gen_rtx_EXPR_LIST (VOIDmode,
					      gen_rtx_REG (HImode,
							   R1_REGNO),
					      GEN_INT (2));
      XVECEXP (rv, 0, 2) = gen_rtx_EXPR_LIST (VOIDmode,
					      gen_rtx_REG (HImode,
							   R2_REGNO),
					      GEN_INT (4));
      XVECEXP (rv, 0, 3) = gen_rtx_EXPR_LIST (VOIDmode,
					      gen_rtx_REG (HImode,
							   R3_REGNO),
					      GEN_INT (6));
      return rv;
    }

  if (TARGET_A24 && GET_MODE_SIZE (mode) > 2)
    {
      rtx rv;

      rv = gen_rtx_PARALLEL (mode, rtvec_alloc (1));
      XVECEXP (rv, 0, 0) = gen_rtx_EXPR_LIST (VOIDmode,
					      gen_rtx_REG (mode,
							   R0_REGNO),
					      GEN_INT (0));
      return rv;
    }
#endif

  if (GET_MODE_SIZE (mode) > 2)
    return gen_rtx_REG (mode, MEM0_REGNO);
  return gen_rtx_REG (mode, R0_REGNO);
}

/* Implements FUNCTION_VALUE.  Functions and libcalls have the same
   conventions.  */
rtx
m32c_function_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  /* return reg or parallel */
  enum machine_mode mode = TYPE_MODE (valtype);
  return m32c_libcall_value (mode);
}

/* How Large Values Are Returned */

/* We return structures by pushing the address on the stack, even if
   we use registers for the first few "real" arguments.  */
#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX m32c_struct_value_rtx
static rtx
m32c_struct_value_rtx (tree fndecl ATTRIBUTE_UNUSED,
		       int incoming ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Function Entry and Exit */

/* Implements EPILOGUE_USES.  Interrupts restore all registers.  */
int
m32c_epilogue_uses (int regno ATTRIBUTE_UNUSED)
{
  if (cfun->machine->is_interrupt)
    return 1;
  return 0;
}

/* Implementing the Varargs Macros */

#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING m32c_strict_argument_naming
static bool
m32c_strict_argument_naming (CUMULATIVE_ARGS * ca ATTRIBUTE_UNUSED)
{
  return 1;
}

/* Trampolines for Nested Functions */

/*
   m16c:
   1 0000 75C43412              mov.w   #0x1234,a0
   2 0004 FC000000              jmp.a   label

   m32c:
   1 0000 BC563412              mov.l:s #0x123456,a0
   2 0004 CC000000              jmp.a   label
*/

/* Implements TRAMPOLINE_SIZE.  */
int
m32c_trampoline_size (void)
{
  /* Allocate extra space so we can avoid the messy shifts when we
     initialize the trampoline; we just write past the end of the
     opcode.  */
  return TARGET_A16 ? 8 : 10;
}

/* Implements TRAMPOLINE_ALIGNMENT.  */
int
m32c_trampoline_alignment (void)
{
  return 2;
}

/* Implements INITIALIZE_TRAMPOLINE.  */
void
m32c_initialize_trampoline (rtx tramp, rtx function, rtx chainval)
{
#define A0(m,i) gen_rtx_MEM (m, plus_constant (tramp, i))
  if (TARGET_A16)
    {
      /* Note: we subtract a "word" because the moves want signed
	 constants, not unsigned constants.  */
      emit_move_insn (A0 (HImode, 0), GEN_INT (0xc475 - 0x10000));
      emit_move_insn (A0 (HImode, 2), chainval);
      emit_move_insn (A0 (QImode, 4), GEN_INT (0xfc - 0x100));
      /* We use 16 bit addresses here, but store the zero to turn it
	 into a 24 bit offset.  */
      emit_move_insn (A0 (HImode, 5), function);
      emit_move_insn (A0 (QImode, 7), GEN_INT (0x00));
    }
  else
    {
      /* Note that the PSI moves actually write 4 bytes.  Make sure we
	 write stuff out in the right order, and leave room for the
	 extra byte at the end.  */
      emit_move_insn (A0 (QImode, 0), GEN_INT (0xbc - 0x100));
      emit_move_insn (A0 (PSImode, 1), chainval);
      emit_move_insn (A0 (QImode, 4), GEN_INT (0xcc - 0x100));
      emit_move_insn (A0 (PSImode, 5), function);
    }
#undef A0
}

/* Implicit Calls to Library Routines */

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS m32c_init_libfuncs
static void
m32c_init_libfuncs (void)
{
  if (TARGET_A24)
    {
      /* We do this because the M32C has an HImode operand, but the
	 M16C has an 8 bit operand.  Since gcc looks at the match data
	 and not the expanded rtl, we have to reset the array so that
	 the right modes are found. */
      setcc_gen_code[EQ] = CODE_FOR_seq_24;
      setcc_gen_code[NE] = CODE_FOR_sne_24;
      setcc_gen_code[GT] = CODE_FOR_sgt_24;
      setcc_gen_code[GE] = CODE_FOR_sge_24;
      setcc_gen_code[LT] = CODE_FOR_slt_24;
      setcc_gen_code[LE] = CODE_FOR_sle_24;
      setcc_gen_code[GTU] = CODE_FOR_sgtu_24;
      setcc_gen_code[GEU] = CODE_FOR_sgeu_24;
      setcc_gen_code[LTU] = CODE_FOR_sltu_24;
      setcc_gen_code[LEU] = CODE_FOR_sleu_24;
    }
}

/* Addressing Modes */

/* Used by GO_IF_LEGITIMATE_ADDRESS.  The r8c/m32c family supports a
   wide range of non-orthogonal addressing modes, including the
   ability to double-indirect on *some* of them.  Not all insns
   support all modes, either, but we rely on predicates and
   constraints to deal with that.  */
int
m32c_legitimate_address_p (enum machine_mode mode, rtx x, int strict)
{
  int mode_adjust;
  if (CONSTANT_P (x))
    return 1;

  /* Wide references to memory will be split after reload, so we must
     ensure that all parts of such splits remain legitimate
     addresses.  */
  mode_adjust = GET_MODE_SIZE (mode) - 1;

  /* allowing PLUS yields mem:HI(plus:SI(mem:SI(plus:SI in m32c_split_move */
  if (GET_CODE (x) == PRE_DEC
      || GET_CODE (x) == POST_INC || GET_CODE (x) == PRE_MODIFY)
    {
      return (GET_CODE (XEXP (x, 0)) == REG
	      && REGNO (XEXP (x, 0)) == SP_REGNO);
    }

#if 0
  /* This is the double indirection detection, but it currently
     doesn't work as cleanly as this code implies, so until we've had
     a chance to debug it, leave it disabled.  */
  if (TARGET_A24 && GET_CODE (x) == MEM && GET_CODE (XEXP (x, 0)) != PLUS)
    {
#if DEBUG_DOUBLE
      fprintf (stderr, "double indirect\n");
#endif
      x = XEXP (x, 0);
    }
#endif

  encode_pattern (x);
  if (RTX_IS ("r"))
    {
      /* Most indexable registers can be used without displacements,
	 although some of them will be emitted with an explicit zero
	 to please the assembler.  */
      switch (REGNO (patternr[0]))
	{
	case A0_REGNO:
	case A1_REGNO:
	case SB_REGNO:
	case FB_REGNO:
	case SP_REGNO:
	  return 1;

	default:
	  if (IS_PSEUDO (patternr[0], strict))
	    return 1;
	  return 0;
	}
    }
  if (RTX_IS ("+ri"))
    {
      /* This is more interesting, because different base registers
	 allow for different displacements - both range and signedness
	 - and it differs from chip series to chip series too.  */
      int rn = REGNO (patternr[1]);
      HOST_WIDE_INT offs = INTVAL (patternr[2]);
      switch (rn)
	{
	case A0_REGNO:
	case A1_REGNO:
	case SB_REGNO:
	  /* The syntax only allows positive offsets, but when the
	     offsets span the entire memory range, we can simulate
	     negative offsets by wrapping.  */
	  if (TARGET_A16)
	    return (offs >= -65536 && offs <= 65535 - mode_adjust);
	  if (rn == SB_REGNO)
	    return (offs >= 0 && offs <= 65535 - mode_adjust);
	  /* A0 or A1 */
	  return (offs >= -16777216 && offs <= 16777215);

	case FB_REGNO:
	  if (TARGET_A16)
	    return (offs >= -128 && offs <= 127 - mode_adjust);
	  return (offs >= -65536 && offs <= 65535 - mode_adjust);

	case SP_REGNO:
	  return (offs >= -128 && offs <= 127 - mode_adjust);

	default:
	  if (IS_PSEUDO (patternr[1], strict))
	    return 1;
	  return 0;
	}
    }
  if (RTX_IS ("+rs") || RTX_IS ("+r+si"))
    {
      rtx reg = patternr[1];

      /* We don't know where the symbol is, so only allow base
	 registers which support displacements spanning the whole
	 address range.  */
      switch (REGNO (reg))
	{
	case A0_REGNO:
	case A1_REGNO:
	  /* $sb needs a secondary reload, but since it's involved in
	     memory address reloads too, we don't deal with it very
	     well.  */
	  /*    case SB_REGNO: */
	  return 1;
	default:
	  if (IS_PSEUDO (reg, strict))
	    return 1;
	  return 0;
	}
    }
  return 0;
}

/* Implements REG_OK_FOR_BASE_P.  */
int
m32c_reg_ok_for_base_p (rtx x, int strict)
{
  if (GET_CODE (x) != REG)
    return 0;
  switch (REGNO (x))
    {
    case A0_REGNO:
    case A1_REGNO:
    case SB_REGNO:
    case FB_REGNO:
    case SP_REGNO:
      return 1;
    default:
      if (IS_PSEUDO (x, strict))
	return 1;
      return 0;
    }
}

/* We have three choices for choosing fb->aN offsets.  If we choose -128,
   we need one MOVA -128[fb],aN opcode and 16 bit aN displacements,
   like this:
       EB 4B FF    mova    -128[$fb],$a0
       D8 0C FF FF mov.w:Q #0,-1[$a0]

   Alternately, we subtract the frame size, and hopefully use 8 bit aN
   displacements:
       7B F4       stc $fb,$a0
       77 54 00 01 sub #256,$a0
       D8 08 01    mov.w:Q #0,1[$a0]

   If we don't offset (i.e. offset by zero), we end up with:
       7B F4       stc $fb,$a0
       D8 0C 00 FF mov.w:Q #0,-256[$a0]

   We have to subtract *something* so that we have a PLUS rtx to mark
   that we've done this reload.  The -128 offset will never result in
   an 8 bit aN offset, and the payoff for the second case is five
   loads *if* those loads are within 256 bytes of the other end of the
   frame, so the third case seems best.  Note that we subtract the
   zero, but detect that in the addhi3 pattern.  */

#define BIG_FB_ADJ 0

/* Implements LEGITIMIZE_ADDRESS.  The only address we really have to
   worry about is frame base offsets, as $fb has a limited
   displacement range.  We deal with this by attempting to reload $fb
   itself into an address register; that seems to result in the best
   code.  */
int
m32c_legitimize_address (rtx * x ATTRIBUTE_UNUSED,
			 rtx oldx ATTRIBUTE_UNUSED,
			 enum machine_mode mode ATTRIBUTE_UNUSED)
{
#if DEBUG0
  fprintf (stderr, "m32c_legitimize_address for mode %s\n", mode_name[mode]);
  debug_rtx (*x);
  fprintf (stderr, "\n");
#endif

  if (GET_CODE (*x) == PLUS
      && GET_CODE (XEXP (*x, 0)) == REG
      && REGNO (XEXP (*x, 0)) == FB_REGNO
      && GET_CODE (XEXP (*x, 1)) == CONST_INT
      && (INTVAL (XEXP (*x, 1)) < -128
	  || INTVAL (XEXP (*x, 1)) > (128 - GET_MODE_SIZE (mode))))
    {
      /* reload FB to A_REGS */
      rtx temp = gen_reg_rtx (Pmode);
      *x = copy_rtx (*x);
      emit_insn (gen_rtx_SET (VOIDmode, temp, XEXP (*x, 0)));
      XEXP (*x, 0) = temp;
      return 1;
    }

  return 0;
}

/* Implements LEGITIMIZE_RELOAD_ADDRESS.  See comment above.  */
int
m32c_legitimize_reload_address (rtx * x,
				enum machine_mode mode,
				int opnum,
				int type, int ind_levels ATTRIBUTE_UNUSED)
{
#if DEBUG0
  fprintf (stderr, "\nm32c_legitimize_reload_address for mode %s\n",
	   mode_name[mode]);
  debug_rtx (*x);
#endif

  /* At one point, this function tried to get $fb copied to an address
     register, which in theory would maximize sharing, but gcc was
     *also* still trying to reload the whole address, and we'd run out
     of address registers.  So we let gcc do the naive (but safe)
     reload instead, when the above function doesn't handle it for
     us.

     The code below is a second attempt at the above.  */

  if (GET_CODE (*x) == PLUS
      && GET_CODE (XEXP (*x, 0)) == REG
      && REGNO (XEXP (*x, 0)) == FB_REGNO
      && GET_CODE (XEXP (*x, 1)) == CONST_INT
      && (INTVAL (XEXP (*x, 1)) < -128
	  || INTVAL (XEXP (*x, 1)) > (128 - GET_MODE_SIZE (mode))))
    {
      rtx sum;
      int offset = INTVAL (XEXP (*x, 1));
      int adjustment = -BIG_FB_ADJ;

      sum = gen_rtx_PLUS (Pmode, XEXP (*x, 0),
			  GEN_INT (adjustment));
      *x = gen_rtx_PLUS (Pmode, sum, GEN_INT (offset - adjustment));
      if (type == RELOAD_OTHER)
	type = RELOAD_FOR_OTHER_ADDRESS;
      push_reload (sum, NULL_RTX, &XEXP (*x, 0), NULL,
		   A_REGS, Pmode, VOIDmode, 0, 0, opnum,
		   type);
      return 1;
    }

  if (GET_CODE (*x) == PLUS
      && GET_CODE (XEXP (*x, 0)) == PLUS
      && GET_CODE (XEXP (XEXP (*x, 0), 0)) == REG
      && REGNO (XEXP (XEXP (*x, 0), 0)) == FB_REGNO
      && GET_CODE (XEXP (XEXP (*x, 0), 1)) == CONST_INT
      && GET_CODE (XEXP (*x, 1)) == CONST_INT
      )
    {
      if (type == RELOAD_OTHER)
	type = RELOAD_FOR_OTHER_ADDRESS;
      push_reload (XEXP (*x, 0), NULL_RTX, &XEXP (*x, 0), NULL,
		   A_REGS, Pmode, VOIDmode, 0, 0, opnum,
		   type);
      return 1;
    }

  return 0;
}

/* Used in GO_IF_MODE_DEPENDENT_ADDRESS.  */
int
m32c_mode_dependent_address (rtx addr)
{
  if (GET_CODE (addr) == POST_INC || GET_CODE (addr) == PRE_DEC)
    return 1;
  return 0;
}

/* Implements LEGITIMATE_CONSTANT_P.  We split large constants anyway,
   so we can allow anything.  */
int
m32c_legitimate_constant_p (rtx x ATTRIBUTE_UNUSED)
{
  return 1;
}


/* Condition Code Status */

#undef TARGET_FIXED_CONDITION_CODE_REGS
#define TARGET_FIXED_CONDITION_CODE_REGS m32c_fixed_condition_code_regs
static bool
m32c_fixed_condition_code_regs (unsigned int *p1, unsigned int *p2)
{
  *p1 = FLG_REGNO;
  *p2 = INVALID_REGNUM;
  return true;
}

/* Describing Relative Costs of Operations */

/* Implements REGISTER_MOVE_COST.  We make impossible moves
   prohibitively expensive, like trying to put QIs in r2/r3 (there are
   no opcodes to do that).  We also discourage use of mem* registers
   since they're really memory.  */
int
m32c_register_move_cost (enum machine_mode mode, int from, int to)
{
  int cost = COSTS_N_INSNS (3);
  int cc = class_contents[from][0] | class_contents[to][0];
  /* FIXME: pick real values, but not 2 for now.  */
  if (mode == QImode && (cc & class_contents[R23_REGS][0]))
    {
      if (!(cc & ~class_contents[R23_REGS][0]))
	cost = COSTS_N_INSNS (1000);
      else
	cost = COSTS_N_INSNS (80);
    }

  if (!class_can_hold_mode (from, mode) || !class_can_hold_mode (to, mode))
    cost = COSTS_N_INSNS (1000);

  if (classes_intersect (from, CR_REGS))
    cost += COSTS_N_INSNS (5);

  if (classes_intersect (to, CR_REGS))
    cost += COSTS_N_INSNS (5);

  if (from == MEM_REGS || to == MEM_REGS)
    cost += COSTS_N_INSNS (50);
  else if (classes_intersect (from, MEM_REGS)
	   || classes_intersect (to, MEM_REGS))
    cost += COSTS_N_INSNS (10);

#if DEBUG0
  fprintf (stderr, "register_move_cost %s from %s to %s = %d\n",
	   mode_name[mode], class_names[from], class_names[to], cost);
#endif
  return cost;
}

/*  Implements MEMORY_MOVE_COST.  */
int
m32c_memory_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
		       int reg_class ATTRIBUTE_UNUSED,
		       int in ATTRIBUTE_UNUSED)
{
  /* FIXME: pick real values.  */
  return COSTS_N_INSNS (10);
}

/* Here we try to describe when we use multiple opcodes for one RTX so
   that gcc knows when to use them.  */
#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS m32c_rtx_costs
static bool
m32c_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case REG:
      if (REGNO (x) >= MEM0_REGNO && REGNO (x) <= MEM7_REGNO)
	*total += COSTS_N_INSNS (500);
      else
	*total += COSTS_N_INSNS (1);
      return true;

    case ASHIFT:
    case LSHIFTRT:
    case ASHIFTRT:
      if (GET_CODE (XEXP (x, 1)) != CONST_INT)
	{
	  /* mov.b r1l, r1h */
	  *total +=  COSTS_N_INSNS (1);
	  return true;
	}
      if (INTVAL (XEXP (x, 1)) > 8
	  || INTVAL (XEXP (x, 1)) < -8)
	{
	  /* mov.b #N, r1l */
	  /* mov.b r1l, r1h */
	  *total +=  COSTS_N_INSNS (2);
	  return true;
	}
      return true;

    case LE:
    case LEU:
    case LT:
    case LTU:
    case GT:
    case GTU:
    case GE:
    case GEU:
    case NE:
    case EQ:
      if (outer_code == SET)
	{
	  *total += COSTS_N_INSNS (2);
	  return true;
	}
      break;

    case ZERO_EXTRACT:
      {
	rtx dest = XEXP (x, 0);
	rtx addr = XEXP (dest, 0);
	switch (GET_CODE (addr))
	  {
	  case CONST_INT:
	    *total += COSTS_N_INSNS (1);
	    break;
	  case SYMBOL_REF:
	    *total += COSTS_N_INSNS (3);
	    break;
	  default:
	    *total += COSTS_N_INSNS (2);
	    break;
	  }
	return true;
      }
      break;

    default:
      /* Reasonable default.  */
      if (TARGET_A16 && GET_MODE(x) == SImode)
	*total += COSTS_N_INSNS (2);
      break;
    }
  return false;
}

#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST m32c_address_cost
static int
m32c_address_cost (rtx addr)
{
  /*  fprintf(stderr, "\naddress_cost\n");
      debug_rtx(addr);*/
  switch (GET_CODE (addr))
    {
    case CONST_INT:
      return COSTS_N_INSNS(1);
    case SYMBOL_REF:
      return COSTS_N_INSNS(3);
    case REG:
      return COSTS_N_INSNS(2);
    default:
      return 0;
    }
}

/* Defining the Output Assembler Language */

/* The Overall Framework of an Assembler File */

#undef TARGET_HAVE_NAMED_SECTIONS
#define TARGET_HAVE_NAMED_SECTIONS true

/* Output of Data */

/* We may have 24 bit sizes, which is the native address size.
   Currently unused, but provided for completeness.  */
#undef TARGET_ASM_INTEGER
#define TARGET_ASM_INTEGER m32c_asm_integer
static bool
m32c_asm_integer (rtx x, unsigned int size, int aligned_p)
{
  switch (size)
    {
    case 3:
      fprintf (asm_out_file, "\t.3byte\t");
      output_addr_const (asm_out_file, x);
      fputc ('\n', asm_out_file);
      return true;
    case 4:
      if (GET_CODE (x) == SYMBOL_REF)
	{
	  fprintf (asm_out_file, "\t.long\t");
	  output_addr_const (asm_out_file, x);
	  fputc ('\n', asm_out_file);
	  return true;
	}
      break;
    }
  return default_assemble_integer (x, size, aligned_p);
}

/* Output of Assembler Instructions */

/* We use a lookup table because the addressing modes are non-orthogonal.  */

static struct
{
  char code;
  char const *pattern;
  char const *format;
}
const conversions[] = {
  { 0, "r", "0" },

  { 0, "mr", "z[1]" },
  { 0, "m+ri", "3[2]" },
  { 0, "m+rs", "3[2]" },
  { 0, "m+r+si", "4+5[2]" },
  { 0, "ms", "1" },
  { 0, "mi", "1" },
  { 0, "m+si", "2+3" },

  { 0, "mmr", "[z[2]]" },
  { 0, "mm+ri", "[4[3]]" },
  { 0, "mm+rs", "[4[3]]" },
  { 0, "mm+r+si", "[5+6[3]]" },
  { 0, "mms", "[[2]]" },
  { 0, "mmi", "[[2]]" },
  { 0, "mm+si", "[4[3]]" },

  { 0, "i", "#0" },
  { 0, "s", "#0" },
  { 0, "+si", "#1+2" },
  { 0, "l", "#0" },

  { 'l', "l", "0" },
  { 'd', "i", "0" },
  { 'd', "s", "0" },
  { 'd', "+si", "1+2" },
  { 'D', "i", "0" },
  { 'D', "s", "0" },
  { 'D', "+si", "1+2" },
  { 'x', "i", "#0" },
  { 'X', "i", "#0" },
  { 'm', "i", "#0" },
  { 'b', "i", "#0" },
  { 'B', "i", "0" },
  { 'p', "i", "0" },

  { 0, 0, 0 }
};

/* This is in order according to the bitfield that pushm/popm use.  */
static char const *pushm_regs[] = {
  "fb", "sb", "a1", "a0", "r3", "r2", "r1", "r0"
};

/* Implements PRINT_OPERAND.  */
void
m32c_print_operand (FILE * file, rtx x, int code)
{
  int i, j, b;
  const char *comma;
  HOST_WIDE_INT ival;
  int unsigned_const = 0;
  int force_sign;

  /* Multiplies; constants are converted to sign-extended format but
   we need unsigned, so 'u' and 'U' tell us what size unsigned we
   need.  */
  if (code == 'u')
    {
      unsigned_const = 2;
      code = 0;
    }
  if (code == 'U')
    {
      unsigned_const = 1;
      code = 0;
    }
  /* This one is only for debugging; you can put it in a pattern to
     force this error.  */
  if (code == '!')
    {
      fprintf (stderr, "dj: unreviewed pattern:");
      if (current_output_insn)
	debug_rtx (current_output_insn);
      gcc_unreachable ();
    }
  /* PSImode operations are either .w or .l depending on the target.  */
  if (code == '&')
    {
      if (TARGET_A16)
	fprintf (file, "w");
      else
	fprintf (file, "l");
      return;
    }
  /* Inverted conditionals.  */
  if (code == 'C')
    {
      switch (GET_CODE (x))
	{
	case LE:
	  fputs ("gt", file);
	  break;
	case LEU:
	  fputs ("gtu", file);
	  break;
	case LT:
	  fputs ("ge", file);
	  break;
	case LTU:
	  fputs ("geu", file);
	  break;
	case GT:
	  fputs ("le", file);
	  break;
	case GTU:
	  fputs ("leu", file);
	  break;
	case GE:
	  fputs ("lt", file);
	  break;
	case GEU:
	  fputs ("ltu", file);
	  break;
	case NE:
	  fputs ("eq", file);
	  break;
	case EQ:
	  fputs ("ne", file);
	  break;
	default:
	  gcc_unreachable ();
	}
      return;
    }
  /* Regular conditionals.  */
  if (code == 'c')
    {
      switch (GET_CODE (x))
	{
	case LE:
	  fputs ("le", file);
	  break;
	case LEU:
	  fputs ("leu", file);
	  break;
	case LT:
	  fputs ("lt", file);
	  break;
	case LTU:
	  fputs ("ltu", file);
	  break;
	case GT:
	  fputs ("gt", file);
	  break;
	case GTU:
	  fputs ("gtu", file);
	  break;
	case GE:
	  fputs ("ge", file);
	  break;
	case GEU:
	  fputs ("geu", file);
	  break;
	case NE:
	  fputs ("ne", file);
	  break;
	case EQ:
	  fputs ("eq", file);
	  break;
	default:
	  gcc_unreachable ();
	}
      return;
    }
  /* Used in negsi2 to do HImode ops on the two parts of an SImode
     operand.  */
  if (code == 'h' && GET_MODE (x) == SImode)
    {
      x = m32c_subreg (HImode, x, SImode, 0);
      code = 0;
    }
  if (code == 'H' && GET_MODE (x) == SImode)
    {
      x = m32c_subreg (HImode, x, SImode, 2);
      code = 0;
    }
  if (code == 'h' && GET_MODE (x) == HImode)
    {
      x = m32c_subreg (QImode, x, HImode, 0);
      code = 0;
    }
  if (code == 'H' && GET_MODE (x) == HImode)
    {
      /* We can't actually represent this as an rtx.  Do it here.  */
      if (GET_CODE (x) == REG)
	{
	  switch (REGNO (x))
	    {
	    case R0_REGNO:
	      fputs ("r0h", file);
	      return;
	    case R1_REGNO:
	      fputs ("r1h", file);
	      return;
	    default:
	      gcc_unreachable();
	    }
	}
      /* This should be a MEM.  */
      x = m32c_subreg (QImode, x, HImode, 1);
      code = 0;
    }
  /* This is for BMcond, which always wants word register names.  */
  if (code == 'h' && GET_MODE (x) == QImode)
    {
      if (GET_CODE (x) == REG)
	x = gen_rtx_REG (HImode, REGNO (x));
      code = 0;
    }
  /* 'x' and 'X' need to be ignored for non-immediates.  */
  if ((code == 'x' || code == 'X') && GET_CODE (x) != CONST_INT)
    code = 0;

  encode_pattern (x);
  force_sign = 0;
  for (i = 0; conversions[i].pattern; i++)
    if (conversions[i].code == code
	&& streq (conversions[i].pattern, pattern))
      {
	for (j = 0; conversions[i].format[j]; j++)
	  /* backslash quotes the next character in the output pattern.  */
	  if (conversions[i].format[j] == '\\')
	    {
	      fputc (conversions[i].format[j + 1], file);
	      j++;
	    }
	  /* Digits in the output pattern indicate that the
	     corresponding RTX is to be output at that point.  */
	  else if (ISDIGIT (conversions[i].format[j]))
	    {
	      rtx r = patternr[conversions[i].format[j] - '0'];
	      switch (GET_CODE (r))
		{
		case REG:
		  fprintf (file, "%s",
			   reg_name_with_mode (REGNO (r), GET_MODE (r)));
		  break;
		case CONST_INT:
		  switch (code)
		    {
		    case 'b':
		    case 'B':
		      {
			int v = INTVAL (r);
			int i = (int) exact_log2 (v);
			if (i == -1)
			  i = (int) exact_log2 ((v ^ 0xffff) & 0xffff);
			if (i == -1)
			  i = (int) exact_log2 ((v ^ 0xff) & 0xff);
			/* Bit position.  */
			fprintf (file, "%d", i);
		      }
		      break;
		    case 'x':
		      /* Unsigned byte.  */
		      fprintf (file, HOST_WIDE_INT_PRINT_HEX,
			       INTVAL (r) & 0xff);
		      break;
		    case 'X':
		      /* Unsigned word.  */
		      fprintf (file, HOST_WIDE_INT_PRINT_HEX,
			       INTVAL (r) & 0xffff);
		      break;
		    case 'p':
		      /* pushm and popm encode a register set into a single byte.  */
		      comma = "";
		      for (b = 7; b >= 0; b--)
			if (INTVAL (r) & (1 << b))
			  {
			    fprintf (file, "%s%s", comma, pushm_regs[b]);
			    comma = ",";
			  }
		      break;
		    case 'm':
		      /* "Minus".  Output -X  */
		      ival = (-INTVAL (r) & 0xffff);
		      if (ival & 0x8000)
			ival = ival - 0x10000;
		      fprintf (file, HOST_WIDE_INT_PRINT_DEC, ival);
		      break;
		    default:
		      ival = INTVAL (r);
		      if (conversions[i].format[j + 1] == '[' && ival < 0)
			{
			  /* We can simulate negative displacements by
			     taking advantage of address space
			     wrapping when the offset can span the
			     entire address range.  */
			  rtx base =
			    patternr[conversions[i].format[j + 2] - '0'];
			  if (GET_CODE (base) == REG)
			    switch (REGNO (base))
			      {
			      case A0_REGNO:
			      case A1_REGNO:
				if (TARGET_A24)
				  ival = 0x1000000 + ival;
				else
				  ival = 0x10000 + ival;
				break;
			      case SB_REGNO:
				if (TARGET_A16)
				  ival = 0x10000 + ival;
				break;
			      }
			}
		      else if (code == 'd' && ival < 0 && j == 0)
			/* The "mova" opcode is used to do addition by
			   computing displacements, but again, we need
			   displacements to be unsigned *if* they're
			   the only component of the displacement
			   (i.e. no "symbol-4" type displacement).  */
			ival = (TARGET_A24 ? 0x1000000 : 0x10000) + ival;

		      if (conversions[i].format[j] == '0')
			{
			  /* More conversions to unsigned.  */
			  if (unsigned_const == 2)
			    ival &= 0xffff;
			  if (unsigned_const == 1)
			    ival &= 0xff;
			}
		      if (streq (conversions[i].pattern, "mi")
			  || streq (conversions[i].pattern, "mmi"))
			{
			  /* Integers used as addresses are unsigned.  */
			  ival &= (TARGET_A24 ? 0xffffff : 0xffff);
			}
		      if (force_sign && ival >= 0)
			fputc ('+', file);
		      fprintf (file, HOST_WIDE_INT_PRINT_DEC, ival);
		      break;
		    }
		  break;
		case CONST_DOUBLE:
		  /* We don't have const_double constants.  If it
		     happens, make it obvious.  */
		  fprintf (file, "[const_double 0x%lx]",
			   (unsigned long) CONST_DOUBLE_HIGH (r));
		  break;
		case SYMBOL_REF:
		  assemble_name (file, XSTR (r, 0));
		  break;
		case LABEL_REF:
		  output_asm_label (r);
		  break;
		default:
		  fprintf (stderr, "don't know how to print this operand:");
		  debug_rtx (r);
		  gcc_unreachable ();
		}
	    }
	  else
	    {
	      if (conversions[i].format[j] == 'z')
		{
		  /* Some addressing modes *must* have a displacement,
		     so insert a zero here if needed.  */
		  int k;
		  for (k = j + 1; conversions[i].format[k]; k++)
		    if (ISDIGIT (conversions[i].format[k]))
		      {
			rtx reg = patternr[conversions[i].format[k] - '0'];
			if (GET_CODE (reg) == REG
			    && (REGNO (reg) == SB_REGNO
				|| REGNO (reg) == FB_REGNO
				|| REGNO (reg) == SP_REGNO))
			  fputc ('0', file);
		      }
		  continue;
		}
	      /* Signed displacements off symbols need to have signs
		 blended cleanly.  */
	      if (conversions[i].format[j] == '+'
		  && (!code || code == 'D' || code == 'd')
		  && ISDIGIT (conversions[i].format[j + 1])
		  && (GET_CODE (patternr[conversions[i].format[j + 1] - '0'])
		      == CONST_INT))
		{
		  force_sign = 1;
		  continue;
		}
	      fputc (conversions[i].format[j], file);
	    }
	break;
      }
  if (!conversions[i].pattern)
    {
      fprintf (stderr, "unconvertible operand %c `%s'", code ? code : '-',
	       pattern);
      debug_rtx (x);
      fprintf (file, "[%c.%s]", code ? code : '-', pattern);
    }

  return;
}

/* Implements PRINT_OPERAND_PUNCT_VALID_P.  See m32c_print_operand
   above for descriptions of what these do.  */
int
m32c_print_operand_punct_valid_p (int c)
{
  if (c == '&' || c == '!')
    return 1;
  return 0;
}

/* Implements PRINT_OPERAND_ADDRESS.  Nothing unusual here.  */
void
m32c_print_operand_address (FILE * stream, rtx address)
{
  gcc_assert (GET_CODE (address) == MEM);
  m32c_print_operand (stream, XEXP (address, 0), 0);
}

/* Implements ASM_OUTPUT_REG_PUSH.  Control registers are pushed
   differently than general registers.  */
void
m32c_output_reg_push (FILE * s, int regno)
{
  if (regno == FLG_REGNO)
    fprintf (s, "\tpushc\tflg\n");
  else
    fprintf (s, "\tpush.%c\t%s\n",
	     " bwll"[reg_push_size (regno)], reg_names[regno]);
}

/* Likewise for ASM_OUTPUT_REG_POP.  */
void
m32c_output_reg_pop (FILE * s, int regno)
{
  if (regno == FLG_REGNO)
    fprintf (s, "\tpopc\tflg\n");
  else
    fprintf (s, "\tpop.%c\t%s\n",
	     " bwll"[reg_push_size (regno)], reg_names[regno]);
}

/* Defining target-specific uses of `__attribute__' */

/* Used to simplify the logic below.  Find the attributes wherever
   they may be.  */
#define M32C_ATTRIBUTES(decl) \
  (TYPE_P (decl)) ? TYPE_ATTRIBUTES (decl) \
                : DECL_ATTRIBUTES (decl) \
                  ? (DECL_ATTRIBUTES (decl)) \
		  : TYPE_ATTRIBUTES (TREE_TYPE (decl))

/* Returns TRUE if the given tree has the "interrupt" attribute.  */
static int
interrupt_p (tree node ATTRIBUTE_UNUSED)
{
  tree list = M32C_ATTRIBUTES (node);
  while (list)
    {
      if (is_attribute_p ("interrupt", TREE_PURPOSE (list)))
	return 1;
      list = TREE_CHAIN (list);
    }
  return 0;
}

static tree
interrupt_handler (tree * node ATTRIBUTE_UNUSED,
		   tree name ATTRIBUTE_UNUSED,
		   tree args ATTRIBUTE_UNUSED,
		   int flags ATTRIBUTE_UNUSED,
		   bool * no_add_attrs ATTRIBUTE_UNUSED)
{
  return NULL_TREE;
}

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE m32c_attribute_table
static const struct attribute_spec m32c_attribute_table[] = {
  {"interrupt", 0, 0, false, false, false, interrupt_handler},
  {0, 0, 0, 0, 0, 0, 0}
};

#undef TARGET_COMP_TYPE_ATTRIBUTES
#define TARGET_COMP_TYPE_ATTRIBUTES m32c_comp_type_attributes
static int
m32c_comp_type_attributes (tree type1 ATTRIBUTE_UNUSED,
			   tree type2 ATTRIBUTE_UNUSED)
{
  /* 0=incompatible 1=compatible 2=warning */
  return 1;
}

#undef TARGET_INSERT_ATTRIBUTES
#define TARGET_INSERT_ATTRIBUTES m32c_insert_attributes
static void
m32c_insert_attributes (tree node ATTRIBUTE_UNUSED,
			tree * attr_ptr ATTRIBUTE_UNUSED)
{
  /* Nothing to do here.  */
}

/* Predicates */

/* Returns TRUE if we support a move between the first two operands.
   At the moment, we just want to discourage mem to mem moves until
   after reload, because reload has a hard time with our limited
   number of address registers, and we can get into a situation where
   we need three of them when we only have two.  */
bool
m32c_mov_ok (rtx * operands, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  rtx op0 = operands[0];
  rtx op1 = operands[1];

  if (TARGET_A24)
    return true;

#define DEBUG_MOV_OK 0
#if DEBUG_MOV_OK
  fprintf (stderr, "m32c_mov_ok %s\n", mode_name[mode]);
  debug_rtx (op0);
  debug_rtx (op1);
#endif

  if (GET_CODE (op0) == SUBREG)
    op0 = XEXP (op0, 0);
  if (GET_CODE (op1) == SUBREG)
    op1 = XEXP (op1, 0);

  if (GET_CODE (op0) == MEM
      && GET_CODE (op1) == MEM
      && ! reload_completed)
    {
#if DEBUG_MOV_OK
      fprintf (stderr, " - no, mem to mem\n");
#endif
      return false;
    }

#if DEBUG_MOV_OK
  fprintf (stderr, " - ok\n");
#endif
  return true;
}

/* Returns TRUE if two consecutive HImode mov instructions, generated
   for moving an immediate double data to a double data type variable
   location, can be combined into single SImode mov instruction.  */
bool
m32c_immd_dbl_mov (rtx * operands, 
		   enum machine_mode mode ATTRIBUTE_UNUSED)
{
  int flag = 0, okflag = 0, offset1 = 0, offset2 = 0, offsetsign = 0;
  const char *str1;
  const char *str2;

  if (GET_CODE (XEXP (operands[0], 0)) == SYMBOL_REF
      && MEM_SCALAR_P (operands[0])
      && !MEM_IN_STRUCT_P (operands[0])
      && GET_CODE (XEXP (operands[2], 0)) == CONST
      && GET_CODE (XEXP (XEXP (operands[2], 0), 0)) == PLUS
      && GET_CODE (XEXP (XEXP (XEXP (operands[2], 0), 0), 0)) == SYMBOL_REF
      && GET_CODE (XEXP (XEXP (XEXP (operands[2], 0), 0), 1)) == CONST_INT
      && MEM_SCALAR_P (operands[2])
      && !MEM_IN_STRUCT_P (operands[2]))
    flag = 1; 

  else if (GET_CODE (XEXP (operands[0], 0)) == CONST
           && GET_CODE (XEXP (XEXP (operands[0], 0), 0)) == PLUS
           && GET_CODE (XEXP (XEXP (XEXP (operands[0], 0), 0), 0)) == SYMBOL_REF
           && MEM_SCALAR_P (operands[0])
           && !MEM_IN_STRUCT_P (operands[0])
           && !(XINT (XEXP (XEXP (XEXP (operands[0], 0), 0), 1), 0) %4)
           && GET_CODE (XEXP (operands[2], 0)) == CONST
           && GET_CODE (XEXP (XEXP (operands[2], 0), 0)) == PLUS
           && GET_CODE (XEXP (XEXP (XEXP (operands[2], 0), 0), 0)) == SYMBOL_REF
           && MEM_SCALAR_P (operands[2])
           && !MEM_IN_STRUCT_P (operands[2]))
    flag = 2; 

  else if (GET_CODE (XEXP (operands[0], 0)) == PLUS
           &&  GET_CODE (XEXP (XEXP (operands[0], 0), 0)) == REG
           &&  REGNO (XEXP (XEXP (operands[0], 0), 0)) == FB_REGNO 
           &&  GET_CODE (XEXP (XEXP (operands[0], 0), 1)) == CONST_INT
           &&  MEM_SCALAR_P (operands[0])
           &&  !MEM_IN_STRUCT_P (operands[0])
           &&  !(XINT (XEXP (XEXP (operands[0], 0), 1), 0) %4)
           &&  REGNO (XEXP (XEXP (operands[2], 0), 0)) == FB_REGNO 
           &&  GET_CODE (XEXP (XEXP (operands[2], 0), 1)) == CONST_INT
           &&  MEM_SCALAR_P (operands[2])
           &&  !MEM_IN_STRUCT_P (operands[2]))
    flag = 3; 

  else
    return false;

  switch (flag)
    {
    case 1:
      str1 = XSTR (XEXP (operands[0], 0), 0);
      str2 = XSTR (XEXP (XEXP (XEXP (operands[2], 0), 0), 0), 0);
      if (strcmp (str1, str2) == 0)
	okflag = 1; 
      else
	okflag = 0; 
      break;
    case 2:
      str1 = XSTR (XEXP (XEXP (XEXP (operands[0], 0), 0), 0), 0);
      str2 = XSTR (XEXP (XEXP (XEXP (operands[2], 0), 0), 0), 0);
      if (strcmp(str1,str2) == 0)
	okflag = 1; 
      else
	okflag = 0; 
      break; 
    case 3:
      offset1 = XINT (XEXP (XEXP (operands[0], 0), 1), 0);
      offset2 = XINT (XEXP (XEXP (operands[2], 0), 1), 0);
      offsetsign = offset1 >> ((sizeof (offset1) * 8) -1);
      if (((offset2-offset1) == 2) && offsetsign != 0)
	okflag = 1;
      else 
	okflag = 0; 
      break; 
    default:
      okflag = 0; 
    } 
      
  if (okflag == 1)
    {
      HOST_WIDE_INT val;
      operands[4] = gen_rtx_MEM (SImode, XEXP (operands[0], 0));

      val = (XINT (operands[3], 0) << 16) + (XINT (operands[1], 0) & 0xFFFF);
      operands[5] = gen_rtx_CONST_INT (VOIDmode, val);
     
      return true;
    }

  return false;
}  

/* Expanders */

/* Subregs are non-orthogonal for us, because our registers are all
   different sizes.  */
static rtx
m32c_subreg (enum machine_mode outer,
	     rtx x, enum machine_mode inner, int byte)
{
  int r, nr = -1;

  /* Converting MEMs to different types that are the same size, we
     just rewrite them.  */
  if (GET_CODE (x) == SUBREG
      && SUBREG_BYTE (x) == 0
      && GET_CODE (SUBREG_REG (x)) == MEM
      && (GET_MODE_SIZE (GET_MODE (x))
	  == GET_MODE_SIZE (GET_MODE (SUBREG_REG (x)))))
    {
      rtx oldx = x;
      x = gen_rtx_MEM (GET_MODE (x), XEXP (SUBREG_REG (x), 0));
      MEM_COPY_ATTRIBUTES (x, SUBREG_REG (oldx));
    }

  /* Push/pop get done as smaller push/pops.  */
  if (GET_CODE (x) == MEM
      && (GET_CODE (XEXP (x, 0)) == PRE_DEC
	  || GET_CODE (XEXP (x, 0)) == POST_INC))
    return gen_rtx_MEM (outer, XEXP (x, 0));
  if (GET_CODE (x) == SUBREG
      && GET_CODE (XEXP (x, 0)) == MEM
      && (GET_CODE (XEXP (XEXP (x, 0), 0)) == PRE_DEC
	  || GET_CODE (XEXP (XEXP (x, 0), 0)) == POST_INC))
    return gen_rtx_MEM (outer, XEXP (XEXP (x, 0), 0));

  if (GET_CODE (x) != REG)
    return simplify_gen_subreg (outer, x, inner, byte);

  r = REGNO (x);
  if (r >= FIRST_PSEUDO_REGISTER || r == AP_REGNO)
    return simplify_gen_subreg (outer, x, inner, byte);

  if (IS_MEM_REGNO (r))
    return simplify_gen_subreg (outer, x, inner, byte);

  /* This is where the complexities of our register layout are
     described.  */
  if (byte == 0)
    nr = r;
  else if (outer == HImode)
    {
      if (r == R0_REGNO && byte == 2)
	nr = R2_REGNO;
      else if (r == R0_REGNO && byte == 4)
	nr = R1_REGNO;
      else if (r == R0_REGNO && byte == 6)
	nr = R3_REGNO;
      else if (r == R1_REGNO && byte == 2)
	nr = R3_REGNO;
      else if (r == A0_REGNO && byte == 2)
	nr = A1_REGNO;
    }
  else if (outer == SImode)
    {
      if (r == R0_REGNO && byte == 0)
	nr = R0_REGNO;
      else if (r == R0_REGNO && byte == 4)
	nr = R1_REGNO;
    }
  if (nr == -1)
    {
      fprintf (stderr, "m32c_subreg %s %s %d\n",
	       mode_name[outer], mode_name[inner], byte);
      debug_rtx (x);
      gcc_unreachable ();
    }
  return gen_rtx_REG (outer, nr);
}

/* Used to emit move instructions.  We split some moves,
   and avoid mem-mem moves.  */
int
m32c_prepare_move (rtx * operands, enum machine_mode mode)
{
  if (TARGET_A16 && mode == PSImode)
    return m32c_split_move (operands, mode, 1);
  if ((GET_CODE (operands[0]) == MEM)
      && (GET_CODE (XEXP (operands[0], 0)) == PRE_MODIFY))
    {
      rtx pmv = XEXP (operands[0], 0);
      rtx dest_reg = XEXP (pmv, 0);
      rtx dest_mod = XEXP (pmv, 1);

      emit_insn (gen_rtx_SET (Pmode, dest_reg, dest_mod));
      operands[0] = gen_rtx_MEM (mode, dest_reg);
    }
  if (!no_new_pseudos && MEM_P (operands[0]) && MEM_P (operands[1]))
    operands[1] = copy_to_mode_reg (mode, operands[1]);
  return 0;
}

#define DEBUG_SPLIT 0

/* Returns TRUE if the given PSImode move should be split.  We split
   for all r8c/m16c moves, since it doesn't support them, and for
   POP.L as we can only *push* SImode.  */
int
m32c_split_psi_p (rtx * operands)
{
#if DEBUG_SPLIT
  fprintf (stderr, "\nm32c_split_psi_p\n");
  debug_rtx (operands[0]);
  debug_rtx (operands[1]);
#endif
  if (TARGET_A16)
    {
#if DEBUG_SPLIT
      fprintf (stderr, "yes, A16\n");
#endif
      return 1;
    }
  if (GET_CODE (operands[1]) == MEM
      && GET_CODE (XEXP (operands[1], 0)) == POST_INC)
    {
#if DEBUG_SPLIT
      fprintf (stderr, "yes, pop.l\n");
#endif
      return 1;
    }
#if DEBUG_SPLIT
  fprintf (stderr, "no, default\n");
#endif
  return 0;
}

/* Split the given move.  SPLIT_ALL is 0 if splitting is optional
   (define_expand), 1 if it is not optional (define_insn_and_split),
   and 3 for define_split (alternate api). */
int
m32c_split_move (rtx * operands, enum machine_mode mode, int split_all)
{
  rtx s[4], d[4];
  int parts, si, di, rev = 0;
  int rv = 0, opi = 2;
  enum machine_mode submode = HImode;
  rtx *ops, local_ops[10];

  /* define_split modifies the existing operands, but the other two
     emit new insns.  OPS is where we store the operand pairs, which
     we emit later.  */
  if (split_all == 3)
    ops = operands;
  else
    ops = local_ops;

  /* Else HImode.  */
  if (mode == DImode)
    submode = SImode;

  /* Before splitting mem-mem moves, force one operand into a
     register.  */
  if (!no_new_pseudos && MEM_P (operands[0]) && MEM_P (operands[1]))
    {
#if DEBUG0
      fprintf (stderr, "force_reg...\n");
      debug_rtx (operands[1]);
#endif
      operands[1] = force_reg (mode, operands[1]);
#if DEBUG0
      debug_rtx (operands[1]);
#endif
    }

  parts = 2;

#if DEBUG_SPLIT
  fprintf (stderr, "\nsplit_move %d all=%d\n", no_new_pseudos, split_all);
  debug_rtx (operands[0]);
  debug_rtx (operands[1]);
#endif

  /* Note that split_all is not used to select the api after this
     point, so it's safe to set it to 3 even with define_insn.  */
  /* None of the chips can move SI operands to sp-relative addresses,
     so we always split those.  */
  if (m32c_extra_constraint_p (operands[0], 'S', "Ss"))
    split_all = 3;

  /* We don't need to split these.  */
  if (TARGET_A24
      && split_all != 3
      && (mode == SImode || mode == PSImode)
      && !(GET_CODE (operands[1]) == MEM
	   && GET_CODE (XEXP (operands[1], 0)) == POST_INC))
    return 0;

  /* First, enumerate the subregs we'll be dealing with.  */
  for (si = 0; si < parts; si++)
    {
      d[si] =
	m32c_subreg (submode, operands[0], mode,
		     si * GET_MODE_SIZE (submode));
      s[si] =
	m32c_subreg (submode, operands[1], mode,
		     si * GET_MODE_SIZE (submode));
    }

  /* Split pushes by emitting a sequence of smaller pushes.  */
  if (GET_CODE (d[0]) == MEM && GET_CODE (XEXP (d[0], 0)) == PRE_DEC)
    {
      for (si = parts - 1; si >= 0; si--)
	{
	  ops[opi++] = gen_rtx_MEM (submode,
				    gen_rtx_PRE_DEC (Pmode,
						     gen_rtx_REG (Pmode,
								  SP_REGNO)));
	  ops[opi++] = s[si];
	}

      rv = 1;
    }
  /* Likewise for pops.  */
  else if (GET_CODE (s[0]) == MEM && GET_CODE (XEXP (s[0], 0)) == POST_INC)
    {
      for (di = 0; di < parts; di++)
	{
	  ops[opi++] = d[di];
	  ops[opi++] = gen_rtx_MEM (submode,
				    gen_rtx_POST_INC (Pmode,
						      gen_rtx_REG (Pmode,
								   SP_REGNO)));
	}
      rv = 1;
    }
  else if (split_all)
    {
      /* if d[di] == s[si] for any di < si, we'll early clobber. */
      for (di = 0; di < parts - 1; di++)
	for (si = di + 1; si < parts; si++)
	  if (reg_mentioned_p (d[di], s[si]))
	    rev = 1;

      if (rev)
	for (si = 0; si < parts; si++)
	  {
	    ops[opi++] = d[si];
	    ops[opi++] = s[si];
	  }
      else
	for (si = parts - 1; si >= 0; si--)
	  {
	    ops[opi++] = d[si];
	    ops[opi++] = s[si];
	  }
      rv = 1;
    }
  /* Now emit any moves we may have accumulated.  */
  if (rv && split_all != 3)
    {
      int i;
      for (i = 2; i < opi; i += 2)
	emit_move_insn (ops[i], ops[i + 1]);
    }
  return rv;
}

/* The m32c has a number of opcodes that act like memcpy, strcmp, and
   the like.  For the R8C they expect one of the addresses to be in
   R1L:An so we need to arrange for that.  Otherwise, it's just a
   matter of picking out the operands we want and emitting the right
   pattern for them.  All these expanders, which correspond to
   patterns in blkmov.md, must return nonzero if they expand the insn,
   or zero if they should FAIL.  */

/* This is a memset() opcode.  All operands are implied, so we need to
   arrange for them to be in the right registers.  The opcode wants
   addresses, not [mem] syntax.  $0 is the destination (MEM:BLK), $1
   the count (HI), and $2 the value (QI).  */
int
m32c_expand_setmemhi(rtx *operands)
{
  rtx desta, count, val;
  rtx desto, counto;

  desta = XEXP (operands[0], 0);
  count = operands[1];
  val = operands[2];

  desto = gen_reg_rtx (Pmode);
  counto = gen_reg_rtx (HImode);

  if (GET_CODE (desta) != REG
      || REGNO (desta) < FIRST_PSEUDO_REGISTER)
    desta = copy_to_mode_reg (Pmode, desta);

  /* This looks like an arbitrary restriction, but this is by far the
     most common case.  For counts 8..14 this actually results in
     smaller code with no speed penalty because the half-sized
     constant can be loaded with a shorter opcode.  */
  if (GET_CODE (count) == CONST_INT
      && GET_CODE (val) == CONST_INT
      && ! (INTVAL (count) & 1)
      && (INTVAL (count) > 1)
      && (INTVAL (val) <= 7 && INTVAL (val) >= -8))
    {
      unsigned v = INTVAL (val) & 0xff;
      v = v | (v << 8);
      count = copy_to_mode_reg (HImode, GEN_INT (INTVAL (count) / 2));
      val = copy_to_mode_reg (HImode, GEN_INT (v));
      if (TARGET_A16)
	emit_insn (gen_setmemhi_whi_op (desto, counto, val, desta, count));
      else
	emit_insn (gen_setmemhi_wpsi_op (desto, counto, val, desta, count));
      return 1;
    }

  /* This is the generalized memset() case.  */
  if (GET_CODE (val) != REG
      || REGNO (val) < FIRST_PSEUDO_REGISTER)
    val = copy_to_mode_reg (QImode, val);

  if (GET_CODE (count) != REG
      || REGNO (count) < FIRST_PSEUDO_REGISTER)
    count = copy_to_mode_reg (HImode, count);

  if (TARGET_A16)
    emit_insn (gen_setmemhi_bhi_op (desto, counto, val, desta, count));
  else
    emit_insn (gen_setmemhi_bpsi_op (desto, counto, val, desta, count));

  return 1;
}

/* This is a memcpy() opcode.  All operands are implied, so we need to
   arrange for them to be in the right registers.  The opcode wants
   addresses, not [mem] syntax.  $0 is the destination (MEM:BLK), $1
   is the source (MEM:BLK), and $2 the count (HI).  */
int
m32c_expand_movmemhi(rtx *operands)
{
  rtx desta, srca, count;
  rtx desto, srco, counto;

  desta = XEXP (operands[0], 0);
  srca = XEXP (operands[1], 0);
  count = operands[2];

  desto = gen_reg_rtx (Pmode);
  srco = gen_reg_rtx (Pmode);
  counto = gen_reg_rtx (HImode);

  if (GET_CODE (desta) != REG
      || REGNO (desta) < FIRST_PSEUDO_REGISTER)
    desta = copy_to_mode_reg (Pmode, desta);

  if (GET_CODE (srca) != REG
      || REGNO (srca) < FIRST_PSEUDO_REGISTER)
    srca = copy_to_mode_reg (Pmode, srca);

  /* Similar to setmem, but we don't need to check the value.  */
  if (GET_CODE (count) == CONST_INT
      && ! (INTVAL (count) & 1)
      && (INTVAL (count) > 1))
    {
      count = copy_to_mode_reg (HImode, GEN_INT (INTVAL (count) / 2));
      if (TARGET_A16)
	emit_insn (gen_movmemhi_whi_op (desto, srco, counto, desta, srca, count));
      else
	emit_insn (gen_movmemhi_wpsi_op (desto, srco, counto, desta, srca, count));
      return 1;
    }

  /* This is the generalized memset() case.  */
  if (GET_CODE (count) != REG
      || REGNO (count) < FIRST_PSEUDO_REGISTER)
    count = copy_to_mode_reg (HImode, count);

  if (TARGET_A16)
    emit_insn (gen_movmemhi_bhi_op (desto, srco, counto, desta, srca, count));
  else
    emit_insn (gen_movmemhi_bpsi_op (desto, srco, counto, desta, srca, count));

  return 1;
}

/* This is a stpcpy() opcode.  $0 is the destination (MEM:BLK) after
   the copy, which should point to the NUL at the end of the string,
   $1 is the destination (MEM:BLK), and $2 is the source (MEM:BLK).
   Since our opcode leaves the destination pointing *after* the NUL,
   we must emit an adjustment.  */
int
m32c_expand_movstr(rtx *operands)
{
  rtx desta, srca;
  rtx desto, srco;

  desta = XEXP (operands[1], 0);
  srca = XEXP (operands[2], 0);

  desto = gen_reg_rtx (Pmode);
  srco = gen_reg_rtx (Pmode);

  if (GET_CODE (desta) != REG
      || REGNO (desta) < FIRST_PSEUDO_REGISTER)
    desta = copy_to_mode_reg (Pmode, desta);

  if (GET_CODE (srca) != REG
      || REGNO (srca) < FIRST_PSEUDO_REGISTER)
    srca = copy_to_mode_reg (Pmode, srca);

  emit_insn (gen_movstr_op (desto, srco, desta, srca));
  /* desto ends up being a1, which allows this type of add through MOVA.  */
  emit_insn (gen_addpsi3 (operands[0], desto, GEN_INT (-1)));

  return 1;
}

/* This is a strcmp() opcode.  $0 is the destination (HI) which holds
   <=>0 depending on the comparison, $1 is one string (MEM:BLK), and
   $2 is the other (MEM:BLK).  We must do the comparison, and then
   convert the flags to a signed integer result.  */
int
m32c_expand_cmpstr(rtx *operands)
{
  rtx src1a, src2a;

  src1a = XEXP (operands[1], 0);
  src2a = XEXP (operands[2], 0);

  if (GET_CODE (src1a) != REG
      || REGNO (src1a) < FIRST_PSEUDO_REGISTER)
    src1a = copy_to_mode_reg (Pmode, src1a);

  if (GET_CODE (src2a) != REG
      || REGNO (src2a) < FIRST_PSEUDO_REGISTER)
    src2a = copy_to_mode_reg (Pmode, src2a);

  emit_insn (gen_cmpstrhi_op (src1a, src2a, src1a, src2a));
  emit_insn (gen_cond_to_int (operands[0]));

  return 1;
}


typedef rtx (*shift_gen_func)(rtx, rtx, rtx);

static shift_gen_func
shift_gen_func_for (int mode, int code)
{
#define GFF(m,c,f) if (mode == m && code == c) return f
  GFF(QImode,  ASHIFT,   gen_ashlqi3_i);
  GFF(QImode,  ASHIFTRT, gen_ashrqi3_i);
  GFF(QImode,  LSHIFTRT, gen_lshrqi3_i);
  GFF(HImode,  ASHIFT,   gen_ashlhi3_i);
  GFF(HImode,  ASHIFTRT, gen_ashrhi3_i);
  GFF(HImode,  LSHIFTRT, gen_lshrhi3_i);
  GFF(PSImode, ASHIFT,   gen_ashlpsi3_i);
  GFF(PSImode, ASHIFTRT, gen_ashrpsi3_i);
  GFF(PSImode, LSHIFTRT, gen_lshrpsi3_i);
  GFF(SImode,  ASHIFT,   TARGET_A16 ? gen_ashlsi3_16 : gen_ashlsi3_24);
  GFF(SImode,  ASHIFTRT, TARGET_A16 ? gen_ashrsi3_16 : gen_ashrsi3_24);
  GFF(SImode,  LSHIFTRT, TARGET_A16 ? gen_lshrsi3_16 : gen_lshrsi3_24);
#undef GFF
  gcc_unreachable ();
}

/* The m32c only has one shift, but it takes a signed count.  GCC
   doesn't want this, so we fake it by negating any shift count when
   we're pretending to shift the other way.  Also, the shift count is
   limited to -8..8.  It's slightly better to use two shifts for 9..15
   than to load the count into r1h, so we do that too.  */
int
m32c_prepare_shift (rtx * operands, int scale, int shift_code)
{
  enum machine_mode mode = GET_MODE (operands[0]);
  shift_gen_func func = shift_gen_func_for (mode, shift_code);
  rtx temp;

  if (GET_CODE (operands[2]) == CONST_INT)
    {
      int maxc = TARGET_A24 && (mode == PSImode || mode == SImode) ? 32 : 8;
      int count = INTVAL (operands[2]) * scale;

      while (count > maxc)
	{
	  temp = gen_reg_rtx (mode);
	  emit_insn (func (temp, operands[1], GEN_INT (maxc)));
	  operands[1] = temp;
	  count -= maxc;
	}
      while (count < -maxc)
	{
	  temp = gen_reg_rtx (mode);
	  emit_insn (func (temp, operands[1], GEN_INT (-maxc)));
	  operands[1] = temp;
	  count += maxc;
	}
      emit_insn (func (operands[0], operands[1], GEN_INT (count)));
      return 1;
    }

  temp = gen_reg_rtx (QImode);
  if (scale < 0)
    /* The pattern has a NEG that corresponds to this. */
    emit_move_insn (temp, gen_rtx_NEG (QImode, operands[2]));
  else if (TARGET_A16 && mode == SImode)
    /* We do this because the code below may modify this, we don't
       want to modify the origin of this value.  */
    emit_move_insn (temp, operands[2]);
  else
    /* We'll only use it for the shift, no point emitting a move.  */
    temp = operands[2];

  if (TARGET_A16 && GET_MODE_SIZE (mode) == 4)
    {
      /* The m16c has a limit of -16..16 for SI shifts, even when the
	 shift count is in a register.  Since there are so many targets
	 of these shifts, it's better to expand the RTL here than to
	 call a helper function.

	 The resulting code looks something like this:

		cmp.b	r1h,-16
		jge.b	1f
		shl.l	-16,dest
		add.b	r1h,16
	1f:	cmp.b	r1h,16
		jle.b	1f
		shl.l	16,dest
		sub.b	r1h,16
	1f:	shl.l	r1h,dest

	 We take advantage of the fact that "negative" shifts are
	 undefined to skip one of the comparisons.  */

      rtx count;
      rtx label, lref, insn, tempvar;

      emit_move_insn (operands[0], operands[1]);

      count = temp;
      label = gen_label_rtx ();
      lref = gen_rtx_LABEL_REF (VOIDmode, label);
      LABEL_NUSES (label) ++;

      tempvar = gen_reg_rtx (mode);

      if (shift_code == ASHIFT)
	{
	  /* This is a left shift.  We only need check positive counts.  */
	  emit_jump_insn (gen_cbranchqi4 (gen_rtx_LE (VOIDmode, 0, 0),
					  count, GEN_INT (16), label));
	  emit_insn (func (tempvar, operands[0], GEN_INT (8)));
	  emit_insn (func (operands[0], tempvar, GEN_INT (8)));
	  insn = emit_insn (gen_addqi3 (count, count, GEN_INT (-16)));
	  emit_label_after (label, insn);
	}
      else
	{
	  /* This is a right shift.  We only need check negative counts.  */
	  emit_jump_insn (gen_cbranchqi4 (gen_rtx_GE (VOIDmode, 0, 0),
					  count, GEN_INT (-16), label));
	  emit_insn (func (tempvar, operands[0], GEN_INT (-8)));
	  emit_insn (func (operands[0], tempvar, GEN_INT (-8)));
	  insn = emit_insn (gen_addqi3 (count, count, GEN_INT (16)));
	  emit_label_after (label, insn);
	}
      operands[1] = operands[0];
      emit_insn (func (operands[0], operands[0], count));
      return 1;
    }

  operands[2] = temp;
  return 0;
}

/* The m32c has a limited range of operations that work on PSImode
   values; we have to expand to SI, do the math, and truncate back to
   PSI.  Yes, this is expensive, but hopefully gcc will learn to avoid
   those cases.  */
void
m32c_expand_neg_mulpsi3 (rtx * operands)
{
  /* operands: a = b * i */
  rtx temp1; /* b as SI */
  rtx scale /* i as SI */;
  rtx temp2; /* a*b as SI */

  temp1 = gen_reg_rtx (SImode);
  temp2 = gen_reg_rtx (SImode);
  if (GET_CODE (operands[2]) != CONST_INT)
    {
      scale = gen_reg_rtx (SImode);
      emit_insn (gen_zero_extendpsisi2 (scale, operands[2]));
    }
  else
    scale = copy_to_mode_reg (SImode, operands[2]);

  emit_insn (gen_zero_extendpsisi2 (temp1, operands[1]));
  temp2 = expand_simple_binop (SImode, MULT, temp1, scale, temp2, 1, OPTAB_LIB);
  emit_insn (gen_truncsipsi2 (operands[0], temp2));
}

static rtx compare_op0, compare_op1;

void
m32c_pend_compare (rtx *operands)
{
  compare_op0 = operands[0];
  compare_op1 = operands[1];
}

void
m32c_unpend_compare (void)
{
  switch (GET_MODE (compare_op0))
    {
    case QImode:
      emit_insn (gen_cmpqi_op (compare_op0, compare_op1));
    case HImode:
      emit_insn (gen_cmphi_op (compare_op0, compare_op1));
    case PSImode:
      emit_insn (gen_cmppsi_op (compare_op0, compare_op1));
    }
}

void
m32c_expand_scc (int code, rtx *operands)
{
  enum machine_mode mode = TARGET_A16 ? QImode : HImode;

  emit_insn (gen_rtx_SET (mode,
			  operands[0],
			  gen_rtx_fmt_ee (code,
					  mode,
					  compare_op0,
					  compare_op1)));
}

/* Pattern Output Functions */

/* Returns a (OP (reg:CC FLG_REGNO) (const_int 0)) from some other
   match_operand rtx's OP.  */
rtx
m32c_cmp_flg_0 (rtx cmp)
{
  return gen_rtx_fmt_ee (GET_CODE (cmp),
			 GET_MODE (cmp),
			 gen_rtx_REG (CCmode, FLG_REGNO),
			 GEN_INT (0));
}

int
m32c_expand_movcc (rtx *operands)
{
  rtx rel = operands[1];
  rtx cmp;

  if (GET_CODE (rel) != EQ && GET_CODE (rel) != NE)
    return 1;
  if (GET_CODE (operands[2]) != CONST_INT
      || GET_CODE (operands[3]) != CONST_INT)
    return 1;
  emit_insn (gen_cmpqi(XEXP (rel, 0), XEXP (rel, 1)));
  if (GET_CODE (rel) == NE)
    {
      rtx tmp = operands[2];
      operands[2] = operands[3];
      operands[3] = tmp;
    }

  cmp = gen_rtx_fmt_ee (GET_CODE (rel),
			GET_MODE (rel),
			compare_op0,
			compare_op1);

  emit_move_insn (operands[0],
		  gen_rtx_IF_THEN_ELSE (GET_MODE (operands[0]),
					cmp,
					operands[2],
					operands[3]));
  return 0;
}

/* Used for the "insv" pattern.  Return nonzero to fail, else done.  */
int
m32c_expand_insv (rtx *operands)
{
  rtx op0, src0, p;
  int mask;

  if (INTVAL (operands[1]) != 1)
    return 1;

  /* Our insv opcode (bset, bclr) can only insert a one-bit constant.  */
  if (GET_CODE (operands[3]) != CONST_INT)
    return 1;
  if (INTVAL (operands[3]) != 0
      && INTVAL (operands[3]) != 1
      && INTVAL (operands[3]) != -1)
    return 1;

  mask = 1 << INTVAL (operands[2]);

  op0 = operands[0];
  if (GET_CODE (op0) == SUBREG
      && SUBREG_BYTE (op0) == 0)
    {
      rtx sub = SUBREG_REG (op0);
      if (GET_MODE (sub) == HImode || GET_MODE (sub) == QImode)
	op0 = sub;
    }

  if (no_new_pseudos
      || (GET_CODE (op0) == MEM && MEM_VOLATILE_P (op0)))
    src0 = op0;
  else
    {
      src0 = gen_reg_rtx (GET_MODE (op0));
      emit_move_insn (src0, op0);
    }

  if (GET_MODE (op0) == HImode
      && INTVAL (operands[2]) >= 8
      && GET_MODE (op0) == MEM)
    {
      /* We are little endian.  */
      rtx new_mem = gen_rtx_MEM (QImode, plus_constant (XEXP (op0, 0), 1));
      MEM_COPY_ATTRIBUTES (new_mem, op0);
      mask >>= 8;
    }

  /* First, we generate a mask with the correct polarity.  If we are
     storing a zero, we want an AND mask, so invert it.  */
  if (INTVAL (operands[3]) == 0)
    {
      /* Storing a zero, use an AND mask */
      if (GET_MODE (op0) == HImode)
	mask ^= 0xffff;
      else
	mask ^= 0xff;
    }
  /* Now we need to properly sign-extend the mask in case we need to
     fall back to an AND or OR opcode.  */
  if (GET_MODE (op0) == HImode)
    {
      if (mask & 0x8000)
	mask -= 0x10000;
    }
  else
    {
      if (mask & 0x80)
	mask -= 0x100;
    }

  switch (  (INTVAL (operands[3]) ? 4 : 0)
	  + ((GET_MODE (op0) == HImode) ? 2 : 0)
	  + (TARGET_A24 ? 1 : 0))
    {
    case 0: p = gen_andqi3_16 (op0, src0, GEN_INT (mask)); break;
    case 1: p = gen_andqi3_24 (op0, src0, GEN_INT (mask)); break;
    case 2: p = gen_andhi3_16 (op0, src0, GEN_INT (mask)); break;
    case 3: p = gen_andhi3_24 (op0, src0, GEN_INT (mask)); break;
    case 4: p = gen_iorqi3_16 (op0, src0, GEN_INT (mask)); break;
    case 5: p = gen_iorqi3_24 (op0, src0, GEN_INT (mask)); break;
    case 6: p = gen_iorhi3_16 (op0, src0, GEN_INT (mask)); break;
    case 7: p = gen_iorhi3_24 (op0, src0, GEN_INT (mask)); break;
    }

  emit_insn (p);
  return 0;
}

const char *
m32c_scc_pattern(rtx *operands, RTX_CODE code)
{
  static char buf[30];
  if (GET_CODE (operands[0]) == REG
      && REGNO (operands[0]) == R0_REGNO)
    {
      if (code == EQ)
	return "stzx\t#1,#0,r0l";
      if (code == NE)
	return "stzx\t#0,#1,r0l";
    }
  sprintf(buf, "bm%s\t0,%%h0\n\tand.b\t#1,%%0", GET_RTX_NAME (code));
  return buf;
}

/* Returns TRUE if the current function is a leaf, and thus we can
   determine which registers an interrupt function really needs to
   save.  The logic below is mostly about finding the insn sequence
   that's the function, versus any sequence that might be open for the
   current insn.  */
static int
m32c_leaf_function_p (void)
{
  rtx saved_first, saved_last;
  struct sequence_stack *seq;
  int rv;

  saved_first = cfun->emit->x_first_insn;
  saved_last = cfun->emit->x_last_insn;
  for (seq = cfun->emit->sequence_stack; seq && seq->next; seq = seq->next)
    ;
  if (seq)
    {
      cfun->emit->x_first_insn = seq->first;
      cfun->emit->x_last_insn = seq->last;
    }

  rv = leaf_function_p ();

  cfun->emit->x_first_insn = saved_first;
  cfun->emit->x_last_insn = saved_last;
  return rv;
}

/* Returns TRUE if the current function needs to use the ENTER/EXIT
   opcodes.  If the function doesn't need the frame base or stack
   pointer, it can use the simpler RTS opcode.  */
static bool
m32c_function_needs_enter (void)
{
  rtx insn;
  struct sequence_stack *seq;
  rtx sp = gen_rtx_REG (Pmode, SP_REGNO);
  rtx fb = gen_rtx_REG (Pmode, FB_REGNO);

  insn = get_insns ();
  for (seq = cfun->emit->sequence_stack;
       seq;
       insn = seq->first, seq = seq->next);

  while (insn)
    {
      if (reg_mentioned_p (sp, insn))
	return true;
      if (reg_mentioned_p (fb, insn))
	return true;
      insn = NEXT_INSN (insn);
    }
  return false;
}

/* Mark all the subexpressions of the PARALLEL rtx PAR as
   frame-related.  Return PAR.

   dwarf2out.c:dwarf2out_frame_debug_expr ignores sub-expressions of a
   PARALLEL rtx other than the first if they do not have the
   FRAME_RELATED flag set on them.  So this function is handy for
   marking up 'enter' instructions.  */
static rtx
m32c_all_frame_related (rtx par)
{
  int len = XVECLEN (par, 0);
  int i;

  for (i = 0; i < len; i++)
    F (XVECEXP (par, 0, i));

  return par;
}

/* Emits the prologue.  See the frame layout comment earlier in this
   file.  We can reserve up to 256 bytes with the ENTER opcode, beyond
   that we manually update sp.  */
void
m32c_emit_prologue (void)
{
  int frame_size, extra_frame_size = 0, reg_save_size;
  int complex_prologue = 0;

  cfun->machine->is_leaf = m32c_leaf_function_p ();
  if (interrupt_p (cfun->decl))
    {
      cfun->machine->is_interrupt = 1;
      complex_prologue = 1;
    }

  reg_save_size = m32c_pushm_popm (PP_justcount);

  if (interrupt_p (cfun->decl))
    emit_insn (gen_pushm (GEN_INT (cfun->machine->intr_pushm)));

  frame_size =
    m32c_initial_elimination_offset (FB_REGNO, SP_REGNO) - reg_save_size;
  if (frame_size == 0
      && !cfun->machine->is_interrupt
      && !m32c_function_needs_enter ())
    cfun->machine->use_rts = 1;

  if (frame_size > 254)
    {
      extra_frame_size = frame_size - 254;
      frame_size = 254;
    }
  if (cfun->machine->use_rts == 0)
    F (emit_insn (m32c_all_frame_related
		  (TARGET_A16
		   ? gen_prologue_enter_16 (GEN_INT (frame_size))
		   : gen_prologue_enter_24 (GEN_INT (frame_size)))));

  if (extra_frame_size)
    {
      complex_prologue = 1;
      if (TARGET_A16)
	F (emit_insn (gen_addhi3 (gen_rtx_REG (HImode, SP_REGNO),
				  gen_rtx_REG (HImode, SP_REGNO),
				  GEN_INT (-extra_frame_size))));
      else
	F (emit_insn (gen_addpsi3 (gen_rtx_REG (PSImode, SP_REGNO),
				   gen_rtx_REG (PSImode, SP_REGNO),
				   GEN_INT (-extra_frame_size))));
    }

  complex_prologue += m32c_pushm_popm (PP_pushm);

  /* This just emits a comment into the .s file for debugging.  */
  if (complex_prologue)
    emit_insn (gen_prologue_end ());
}

/* Likewise, for the epilogue.  The only exception is that, for
   interrupts, we must manually unwind the frame as the REIT opcode
   doesn't do that.  */
void
m32c_emit_epilogue (void)
{
  /* This just emits a comment into the .s file for debugging.  */
  if (m32c_pushm_popm (PP_justcount) > 0 || cfun->machine->is_interrupt)
    emit_insn (gen_epilogue_start ());

  m32c_pushm_popm (PP_popm);

  if (cfun->machine->is_interrupt)
    {
      enum machine_mode spmode = TARGET_A16 ? HImode : PSImode;

      emit_move_insn (gen_rtx_REG (spmode, A0_REGNO),
		      gen_rtx_REG (spmode, FP_REGNO));
      emit_move_insn (gen_rtx_REG (spmode, SP_REGNO),
		      gen_rtx_REG (spmode, A0_REGNO));
      if (TARGET_A16)
	emit_insn (gen_pophi_16 (gen_rtx_REG (HImode, FP_REGNO)));
      else
	emit_insn (gen_poppsi (gen_rtx_REG (PSImode, FP_REGNO)));
      emit_insn (gen_popm (GEN_INT (cfun->machine->intr_pushm)));
      emit_jump_insn (gen_epilogue_reit (GEN_INT (TARGET_A16 ? 4 : 6)));
    }
  else if (cfun->machine->use_rts)
    emit_jump_insn (gen_epilogue_rts ());
  else
    emit_jump_insn (gen_epilogue_exitd (GEN_INT (TARGET_A16 ? 2 : 4)));
  emit_barrier ();
}

void
m32c_emit_eh_epilogue (rtx ret_addr)
{
  /* R0[R2] has the stack adjustment.  R1[R3] has the address to
     return to.  We have to fudge the stack, pop everything, pop SP
     (fudged), and return (fudged).  This is actually easier to do in
     assembler, so punt to libgcc.  */
  emit_jump_insn (gen_eh_epilogue (ret_addr, cfun->machine->eh_stack_adjust));
  /*  emit_insn (gen_rtx_CLOBBER (HImode, gen_rtx_REG (HImode, R0L_REGNO))); */
  emit_barrier ();
}

/* Indicate which flags must be properly set for a given conditional.  */
static int
flags_needed_for_conditional (rtx cond)
{
  switch (GET_CODE (cond))
    {
    case LE:
    case GT:
      return FLAGS_OSZ;
    case LEU:
    case GTU:
      return FLAGS_ZC;
    case LT:
    case GE:
      return FLAGS_OS;
    case LTU:
    case GEU:
      return FLAGS_C;
    case EQ:
    case NE:
      return FLAGS_Z;
    default:
      return FLAGS_N;
    }
}

#define DEBUG_CMP 0

/* Returns true if a compare insn is redundant because it would only
   set flags that are already set correctly.  */
static bool
m32c_compare_redundant (rtx cmp, rtx *operands)
{
  int flags_needed;
  int pflags;
  rtx prev, pp, next;
  rtx op0, op1, op2;
#if DEBUG_CMP
  int prev_icode, i;
#endif

  op0 = operands[0];
  op1 = operands[1];
  op2 = operands[2];

#if DEBUG_CMP
  fprintf(stderr, "\n\033[32mm32c_compare_redundant\033[0m\n");
  debug_rtx(cmp);
  for (i=0; i<2; i++)
    {
      fprintf(stderr, "operands[%d] = ", i);
      debug_rtx(operands[i]);
    }
#endif

  next = next_nonnote_insn (cmp);
  if (!next || !INSN_P (next))
    {
#if DEBUG_CMP
      fprintf(stderr, "compare not followed by insn\n");
      debug_rtx(next);
#endif
      return false;
    }
  if (GET_CODE (PATTERN (next)) == SET
      && GET_CODE (XEXP ( PATTERN (next), 1)) == IF_THEN_ELSE)
    {
      next = XEXP (XEXP (PATTERN (next), 1), 0);
    }
  else if (GET_CODE (PATTERN (next)) == SET)
    {
      /* If this is a conditional, flags_needed will be something
	 other than FLAGS_N, which we test below.  */
      next = XEXP (PATTERN (next), 1);
    }
  else
    {
#if DEBUG_CMP
      fprintf(stderr, "compare not followed by conditional\n");
      debug_rtx(next);
#endif
      return false;
    }
#if DEBUG_CMP
  fprintf(stderr, "conditional is: ");
  debug_rtx(next);
#endif

  flags_needed = flags_needed_for_conditional (next);
  if (flags_needed == FLAGS_N)
    {
#if DEBUG_CMP
      fprintf(stderr, "compare not followed by conditional\n");
      debug_rtx(next);
#endif
      return false;
    }

  /* Compare doesn't set overflow and carry the same way that
     arithmetic instructions do, so we can't replace those.  */
  if (flags_needed & FLAGS_OC)
    return false;

  prev = cmp;
  do {
    prev = prev_nonnote_insn (prev);
    if (!prev)
      {
#if DEBUG_CMP
	fprintf(stderr, "No previous insn.\n");
#endif
	return false;
      }
    if (!INSN_P (prev))
      {
#if DEBUG_CMP
	fprintf(stderr, "Previous insn is a non-insn.\n");
#endif
	return false;
      }
    pp = PATTERN (prev);
    if (GET_CODE (pp) != SET)
      {
#if DEBUG_CMP
	fprintf(stderr, "Previous insn is not a SET.\n");
#endif
	return false;
      }
    pflags = get_attr_flags (prev);

    /* Looking up attributes of previous insns corrupted the recog
       tables.  */
    INSN_UID (cmp) = -1;
    recog (PATTERN (cmp), cmp, 0);

    if (pflags == FLAGS_N
	&& reg_mentioned_p (op0, pp))
      {
#if DEBUG_CMP
	fprintf(stderr, "intermediate non-flags insn uses op:\n");
	debug_rtx(prev);
#endif
	return false;
      }
  } while (pflags == FLAGS_N);
#if DEBUG_CMP
  fprintf(stderr, "previous flag-setting insn:\n");
  debug_rtx(prev);
  debug_rtx(pp);
#endif

  if (GET_CODE (pp) == SET
      && GET_CODE (XEXP (pp, 0)) == REG
      && REGNO (XEXP (pp, 0)) == FLG_REGNO
      && GET_CODE (XEXP (pp, 1)) == COMPARE)
    {
      /* Adjacent cbranches must have the same operands to be
	 redundant.  */
      rtx pop0 = XEXP (XEXP (pp, 1), 0);
      rtx pop1 = XEXP (XEXP (pp, 1), 1);
#if DEBUG_CMP
      fprintf(stderr, "adjacent cbranches\n");
      debug_rtx(pop0);
      debug_rtx(pop1);
#endif
      if (rtx_equal_p (op0, pop0)
	  && rtx_equal_p (op1, pop1))
	return true;
#if DEBUG_CMP
      fprintf(stderr, "prev cmp not same\n");
#endif
      return false;
    }

  /* Else the previous insn must be a SET, with either the source or
     dest equal to operands[0], and operands[1] must be zero.  */

  if (!rtx_equal_p (op1, const0_rtx))
    {
#if DEBUG_CMP
      fprintf(stderr, "operands[1] not const0_rtx\n");
#endif
      return false;
    }
  if (GET_CODE (pp) != SET)
    {
#if DEBUG_CMP
      fprintf (stderr, "pp not set\n");
#endif
      return false;
    }
  if (!rtx_equal_p (op0, SET_SRC (pp))
      && !rtx_equal_p (op0, SET_DEST (pp)))
    {
#if DEBUG_CMP
      fprintf(stderr, "operands[0] not found in set\n");
#endif
      return false;
    }

#if DEBUG_CMP
  fprintf(stderr, "cmp flags %x prev flags %x\n", flags_needed, pflags);
#endif
  if ((pflags & flags_needed) == flags_needed)
    return true;

  return false;
}

/* Return the pattern for a compare.  This will be commented out if
   the compare is redundant, else a normal pattern is returned.  Thus,
   the assembler output says where the compare would have been.  */
char *
m32c_output_compare (rtx insn, rtx *operands)
{
  static char template[] = ";cmp.b\t%1,%0";
  /*                             ^ 5  */

  template[5] = " bwll"[GET_MODE_SIZE(GET_MODE(operands[0]))];
  if (m32c_compare_redundant (insn, operands))
    {
#if DEBUG_CMP
      fprintf(stderr, "cbranch: cmp not needed\n");
#endif
      return template;
    }

#if DEBUG_CMP
  fprintf(stderr, "cbranch: cmp needed: `%s'\n", template);
#endif
  return template + 1;
}

/* The Global `targetm' Variable. */

struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-m32c.h"
