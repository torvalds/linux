/* Output routines for Sunplus S+CORE processor
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Sunnorth.

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

#define GR_REG_CLASS_P(C)        ((C) == G16_REGS || (C) == G32_REGS)
#define SP_REG_CLASS_P(C) \
  ((C) == CN_REG || (C) == LC_REG || (C) == SC_REG || (C) == SP_REGS)
#define CP_REG_CLASS_P(C) \
  ((C) == CP1_REGS || (C) == CP2_REGS || (C) == CP3_REGS || (C) == CPA_REGS)
#define CE_REG_CLASS_P(C) \
  ((C) == HI_REG || (C) == LO_REG || (C) == CE_REGS)

static int score_arg_partial_bytes (const CUMULATIVE_ARGS *,
                                    enum machine_mode, tree, int);

static int score_symbol_insns (enum score_symbol_type);

static int score_address_insns (rtx, enum machine_mode);

static bool score_rtx_costs (rtx, enum rtx_code, enum rtx_code, int *);

static int score_address_cost (rtx);

#undef  TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START           th_asm_file_start

#undef  TARGET_ASM_FILE_END
#define TARGET_ASM_FILE_END             th_asm_file_end

#undef  TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE    th_function_prologue

#undef  TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE    th_function_epilogue

#undef  TARGET_SCHED_ISSUE_RATE
#define TARGET_SCHED_ISSUE_RATE         th_issue_rate

#undef TARGET_ASM_SELECT_RTX_SECTION
#define TARGET_ASM_SELECT_RTX_SECTION   th_select_rtx_section

#undef  TARGET_IN_SMALL_DATA_P
#define TARGET_IN_SMALL_DATA_P          th_in_small_data_p

#undef  TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL  th_function_ok_for_sibcall

#undef TARGET_STRICT_ARGUMENT_NAMING
#define TARGET_STRICT_ARGUMENT_NAMING   th_strict_argument_naming

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK      th_output_mi_thunk

#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK  hook_bool_tree_hwi_hwi_tree_true

#undef TARGET_PROMOTE_FUNCTION_ARGS
#define TARGET_PROMOTE_FUNCTION_ARGS    hook_bool_tree_true

#undef TARGET_PROMOTE_FUNCTION_RETURN
#define TARGET_PROMOTE_FUNCTION_RETURN  hook_bool_tree_true

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES       hook_bool_tree_true

#undef TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK       must_pass_in_stack_var_size

#undef TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES        score_arg_partial_bytes

#undef TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE        score_pass_by_reference

#undef TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY         score_return_in_memory

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS                score_rtx_costs

#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST             score_address_cost

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS     TARGET_DEFAULT

/* Implement TARGET_RETURN_IN_MEMORY.  In S+core,
   small structures are returned in a register.
   Objects with varying size must still be returned in memory.  */
static bool
score_return_in_memory (tree type, tree fndecl ATTRIBUTE_UNUSED)
{
  return ((TYPE_MODE (type) == BLKmode)
          || (int_size_in_bytes (type) > 2 * UNITS_PER_WORD)
          || (int_size_in_bytes (type) == -1));
}

/* Return nonzero when an argument must be passed by reference.  */
static bool
score_pass_by_reference (CUMULATIVE_ARGS *cum ATTRIBUTE_UNUSED,
                         enum machine_mode mode, tree type,
                         bool named ATTRIBUTE_UNUSED)
{
  /* If we have a variable-sized parameter, we have no choice.  */
  return targetm.calls.must_pass_in_stack (mode, type);
}

/* Return a legitimate address for REG + OFFSET.  */
static rtx
score_add_offset (rtx temp, rtx reg, HOST_WIDE_INT offset)
{
  if (!IMM_IN_RANGE (offset, 15, 1))
    {
      reg = expand_simple_binop (GET_MODE (reg), PLUS,
                                 gen_int_mode (offset & 0xffffc000,
                                               GET_MODE (reg)),
                                 reg, NULL, 0, OPTAB_WIDEN);
      offset &= 0x3fff;
    }

  return plus_constant (reg, offset);
}

/* Implement TARGET_ASM_OUTPUT_MI_THUNK.  Generate rtl rather than asm text
   in order to avoid duplicating too much logic from elsewhere.  */
static void
th_output_mi_thunk (FILE *file, tree thunk_fndecl ATTRIBUTE_UNUSED,
                    HOST_WIDE_INT delta, HOST_WIDE_INT vcall_offset,
                    tree function)
{
  rtx this, temp1, temp2, insn, fnaddr;

  /* Pretend to be a post-reload pass while generating rtl.  */
  no_new_pseudos = 1;
  reload_completed = 1;
  reset_block_changes ();

  /* We need two temporary registers in some cases.  */
  temp1 = gen_rtx_REG (Pmode, 8);
  temp2 = gen_rtx_REG (Pmode, 9);

  /* Find out which register contains the "this" pointer.  */
  if (aggregate_value_p (TREE_TYPE (TREE_TYPE (function)), function))
    this = gen_rtx_REG (Pmode, ARG_REG_FIRST + 1);
  else
    this = gen_rtx_REG (Pmode, ARG_REG_FIRST);

  /* Add DELTA to THIS.  */
  if (delta != 0)
    {
      rtx offset = GEN_INT (delta);
      if (!CONST_OK_FOR_LETTER_P (delta, 'L'))
        {
          emit_move_insn (temp1, offset);
          offset = temp1;
        }
      emit_insn (gen_add3_insn (this, this, offset));
    }

  /* If needed, add *(*THIS + VCALL_OFFSET) to THIS.  */
  if (vcall_offset != 0)
    {
      rtx addr;

      /* Set TEMP1 to *THIS.  */
      emit_move_insn (temp1, gen_rtx_MEM (Pmode, this));

      /* Set ADDR to a legitimate address for *THIS + VCALL_OFFSET.  */
      addr = score_add_offset (temp2, temp1, vcall_offset);

      /* Load the offset and add it to THIS.  */
      emit_move_insn (temp1, gen_rtx_MEM (Pmode, addr));
      emit_insn (gen_add3_insn (this, this, temp1));
    }

  /* Jump to the target function.  */
  fnaddr = XEXP (DECL_RTL (function), 0);
  insn = emit_call_insn (gen_sibcall_internal (fnaddr, const0_rtx));
  SIBLING_CALL_P (insn) = 1;

  /* Run just enough of rest_of_compilation.  This sequence was
     "borrowed" from alpha.c.  */
  insn = get_insns ();
  insn_locators_initialize ();
  split_all_insns_noflow ();
  shorten_branches (insn);
  final_start_function (insn, file, 1);
  final (insn, file, 1);
  final_end_function ();

  /* Clean up the vars set above.  Note that final_end_function resets
     the global pointer for us.  */
  reload_completed = 0;
  no_new_pseudos = 0;
}

/* Implement TARGET_STRICT_ARGUMENT_NAMING.  */
static bool
th_strict_argument_naming (CUMULATIVE_ARGS *ca ATTRIBUTE_UNUSED)
{
  return true;
}

/* Implement TARGET_FUNCTION_OK_FOR_SIBCALL.  */
static bool
th_function_ok_for_sibcall (ATTRIBUTE_UNUSED tree decl,
                            ATTRIBUTE_UNUSED tree exp)
{
  return true;
}

struct score_arg_info
{
  /* The argument's size, in bytes.  */
  unsigned int num_bytes;

  /* The number of words passed in registers, rounded up.  */
  unsigned int reg_words;

  /* The offset of the first register from GP_ARG_FIRST or FP_ARG_FIRST,
     or ARG_REG_NUM if the argument is passed entirely on the stack.  */
  unsigned int reg_offset;

  /* The number of words that must be passed on the stack, rounded up.  */
  unsigned int stack_words;

  /* The offset from the start of the stack overflow area of the argument's
     first stack word.  Only meaningful when STACK_WORDS is nonzero.  */
  unsigned int stack_offset;
};

/* Fill INFO with information about a single argument.  CUM is the
   cumulative state for earlier arguments.  MODE is the mode of this
   argument and TYPE is its type (if known).  NAMED is true if this
   is a named (fixed) argument rather than a variable one.  */
static void
classify_arg (const CUMULATIVE_ARGS *cum, enum machine_mode mode,
              tree type, int named, struct score_arg_info *info)
{
  int even_reg_p;
  unsigned int num_words, max_regs;

  even_reg_p = 0;
  if (GET_MODE_CLASS (mode) == MODE_INT
      || GET_MODE_CLASS (mode) == MODE_FLOAT)
    even_reg_p = (GET_MODE_SIZE (mode) > UNITS_PER_WORD);
  else
    if (type != NULL_TREE && TYPE_ALIGN (type) > BITS_PER_WORD && named)
      even_reg_p = 1;

  if (TARGET_MUST_PASS_IN_STACK (mode, type))
    info->reg_offset = ARG_REG_NUM;
  else
    {
      info->reg_offset = cum->num_gprs;
      if (even_reg_p)
        info->reg_offset += info->reg_offset & 1;
    }

  if (mode == BLKmode)
    info->num_bytes = int_size_in_bytes (type);
  else
    info->num_bytes = GET_MODE_SIZE (mode);

  num_words = (info->num_bytes + UNITS_PER_WORD - 1) / UNITS_PER_WORD;
  max_regs = ARG_REG_NUM - info->reg_offset;

  /* Partition the argument between registers and stack.  */
  info->reg_words = MIN (num_words, max_regs);
  info->stack_words = num_words - info->reg_words;

  /* The alignment applied to registers is also applied to stack arguments.  */
  if (info->stack_words)
    {
      info->stack_offset = cum->stack_words;
      if (even_reg_p)
        info->stack_offset += info->stack_offset & 1;
    }
}

/* Set up the stack and frame (if desired) for the function.  */
static void
th_function_prologue (FILE *file, HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  const char *fnname;
  struct score_frame_info *f = mda_cached_frame ();
  HOST_WIDE_INT tsize = f->total_size;

  fnname = XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0);
  if (!flag_inhibit_size_directive)
    {
      fputs ("\t.ent\t", file);
      assemble_name (file, fnname);
      fputs ("\n", file);
    }
  assemble_name (file, fnname);
  fputs (":\n", file);

  if (!flag_inhibit_size_directive)
    {
      fprintf (file,
               "\t.frame\t%s," HOST_WIDE_INT_PRINT_DEC ",%s, %d\t\t"
               "# vars= " HOST_WIDE_INT_PRINT_DEC ", regs= %d"
               ", args= " HOST_WIDE_INT_PRINT_DEC
               ", gp= " HOST_WIDE_INT_PRINT_DEC "\n",
               (reg_names[(frame_pointer_needed)
                ? HARD_FRAME_POINTER_REGNUM : STACK_POINTER_REGNUM]),
               tsize,
               reg_names[RA_REGNUM],
               current_function_is_leaf ? 1 : 0,
               f->var_size,
               f->num_gp,
               f->args_size,
               f->cprestore_size);

      fprintf(file, "\t.mask\t0x%08x," HOST_WIDE_INT_PRINT_DEC "\n",
              f->mask,
              (f->gp_sp_offset - f->total_size));
    }
}

/* Do any necessary cleanup after a function to restore stack, frame,
   and regs.  */
static void
th_function_epilogue (FILE *file,
                      HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  if (!flag_inhibit_size_directive)
    {
      const char *fnname;
      fnname = XSTR (XEXP (DECL_RTL (current_function_decl), 0), 0);
      fputs ("\t.end\t", file);
      assemble_name (file, fnname);
      fputs ("\n", file);
    }
}

/* Implement TARGET_SCHED_ISSUE_RATE.  */
static int
th_issue_rate (void)
{
  return 1;
}

/* Returns true if X contains a SYMBOL_REF.  */
static bool
symbolic_expression_p (rtx x)
{
  if (GET_CODE (x) == SYMBOL_REF)
    return true;

  if (GET_CODE (x) == CONST)
    return symbolic_expression_p (XEXP (x, 0));

  if (UNARY_P (x))
    return symbolic_expression_p (XEXP (x, 0));

  if (ARITHMETIC_P (x))
    return (symbolic_expression_p (XEXP (x, 0))
            || symbolic_expression_p (XEXP (x, 1)));

  return false;
}

/* Choose the section to use for the constant rtx expression X that has
   mode MODE.  */
static section *
th_select_rtx_section (enum machine_mode mode, rtx x,
                       unsigned HOST_WIDE_INT align)
{
  if (GET_MODE_SIZE (mode) <= SCORE_SDATA_MAX)
    return get_named_section (0, ".sdata", 0);
  else if (flag_pic && symbolic_expression_p (x))
    return get_named_section (0, ".data.rel.ro", 3);
  else
    return mergeable_constant_section (mode, align, 0);
}

/* Implement TARGET_IN_SMALL_DATA_P.  */
static bool
th_in_small_data_p (tree decl)
{
  HOST_WIDE_INT size;

  if (TREE_CODE (decl) == STRING_CST
      || TREE_CODE (decl) == FUNCTION_DECL)
    return false;

  if (TREE_CODE (decl) == VAR_DECL && DECL_SECTION_NAME (decl) != 0)
    {
      const char *name;
      name = TREE_STRING_POINTER (DECL_SECTION_NAME (decl));
      if (strcmp (name, ".sdata") != 0
          && strcmp (name, ".sbss") != 0)
        return true;
      if (!DECL_EXTERNAL (decl))
        return false;
    }
  size = int_size_in_bytes (TREE_TYPE (decl));
  return (size > 0 && size <= SCORE_SDATA_MAX);
}

/* Implement TARGET_ASM_FILE_START.  */
static void
th_asm_file_start (void)
{
  default_file_start ();
  fprintf (asm_out_file, ASM_COMMENT_START
           "GCC for S+core %s \n", SCORE_GCC_VERSION);

  if (flag_pic)
    fprintf (asm_out_file, "\t.set pic\n");
}

/* Implement TARGET_ASM_FILE_END.  When using assembler macros, emit
   .externs for any small-data variables that turned out to be external.  */
struct extern_list *extern_head = 0;

static void
th_asm_file_end (void)
{
  tree name_tree;
  struct extern_list *p;
  if (extern_head)
    {
      fputs ("\n", asm_out_file);
      for (p = extern_head; p != 0; p = p->next)
        {
          name_tree = get_identifier (p->name);
          if (!TREE_ASM_WRITTEN (name_tree)
              && TREE_SYMBOL_REFERENCED (name_tree))
            {
              TREE_ASM_WRITTEN (name_tree) = 1;
              fputs ("\t.extern\t", asm_out_file);
              assemble_name (asm_out_file, p->name);
              fprintf (asm_out_file, ", %d\n", p->size);
            }
        }
    }
}

static unsigned int sdata_max;

int
score_sdata_max (void)
{
  return sdata_max;
}

/* default 0 = NO_REGS  */
enum reg_class score_char_to_class[256];

/* Implement OVERRIDE_OPTIONS macro.  */
void
score_override_options (void)
{
  flag_pic = false;
  if (!flag_pic)
    sdata_max = g_switch_set ? g_switch_value : DEFAULT_SDATA_MAX;
  else
    {
      sdata_max = 0;
      if (g_switch_set && (g_switch_value != 0))
        warning (0, "-fPIC and -G are incompatible");
    }

  score_char_to_class['d'] = G32_REGS;
  score_char_to_class['e'] = G16_REGS;
  score_char_to_class['t'] = T32_REGS;

  score_char_to_class['h'] = HI_REG;
  score_char_to_class['l'] = LO_REG;
  score_char_to_class['x'] = CE_REGS;

  score_char_to_class['q'] = CN_REG;
  score_char_to_class['y'] = LC_REG;
  score_char_to_class['z'] = SC_REG;
  score_char_to_class['a'] = SP_REGS;

  score_char_to_class['c'] = CR_REGS;

  score_char_to_class['b'] = CP1_REGS;
  score_char_to_class['f'] = CP2_REGS;
  score_char_to_class['i'] = CP3_REGS;
  score_char_to_class['j'] = CPA_REGS;
}

/* Implement REGNO_REG_CLASS macro.  */
int
score_reg_class (int regno)
{
  int c;
  gcc_assert (regno >= 0 && regno < FIRST_PSEUDO_REGISTER);

  if (regno == FRAME_POINTER_REGNUM
      || regno == ARG_POINTER_REGNUM)
    return ALL_REGS;

  for (c = 0; c < N_REG_CLASSES; c++)
    if (TEST_HARD_REG_BIT (reg_class_contents[c], regno))
      return c;

  return NO_REGS;
}

/* Implement PREFERRED_RELOAD_CLASS macro.  */
enum reg_class
score_preferred_reload_class (rtx x ATTRIBUTE_UNUSED, enum reg_class class)
{
  if (reg_class_subset_p (G16_REGS, class))
    return G16_REGS;
  if (reg_class_subset_p (G32_REGS, class))
    return G32_REGS;
  return class;
}

/* Implement SECONDARY_INPUT_RELOAD_CLASS
   and SECONDARY_OUTPUT_RELOAD_CLASS macro.  */
enum reg_class
score_secondary_reload_class (enum reg_class class,
                              enum machine_mode mode ATTRIBUTE_UNUSED,
                              rtx x)
{
  int regno = -1;
  if (GET_CODE (x) == REG || GET_CODE(x) == SUBREG)
    regno = true_regnum (x);

  if (!GR_REG_CLASS_P (class))
    return GP_REG_P (regno) ? NO_REGS : G32_REGS;
  return NO_REGS;
}

/* Implement CONST_OK_FOR_LETTER_P macro.  */
/* imm constraints
   I        imm16 << 16
   J        uimm5
   K        uimm16
   L        simm16
   M        uimm14
   N        simm14  */
int
score_const_ok_for_letter_p (HOST_WIDE_INT value, char c)
{
  switch (c)
    {
    case 'I': return ((value & 0xffff) == 0);
    case 'J': return IMM_IN_RANGE (value, 5, 0);
    case 'K': return IMM_IN_RANGE (value, 16, 0);
    case 'L': return IMM_IN_RANGE (value, 16, 1);
    case 'M': return IMM_IN_RANGE (value, 14, 0);
    case 'N': return IMM_IN_RANGE (value, 14, 1);
    default : return 0;
    }
}

/* Implement EXTRA_CONSTRAINT macro.  */
/* Z        symbol_ref  */
int
score_extra_constraint (rtx op, char c)
{
  switch (c)
    {
    case 'Z':
      return GET_CODE (op) == SYMBOL_REF;
    default:
      gcc_unreachable ();
    }
}

/* Return truth value on whether or not a given hard register
   can support a given mode.  */
int
score_hard_regno_mode_ok (unsigned int regno, enum machine_mode mode)
{
  int size = GET_MODE_SIZE (mode);
  enum mode_class class = GET_MODE_CLASS (mode);

  if (class == MODE_CC)
    return regno == CC_REGNUM;
  else if (regno == FRAME_POINTER_REGNUM
           || regno == ARG_POINTER_REGNUM)
    return class == MODE_INT;
  else if (GP_REG_P (regno))
    /* ((regno <= (GP_REG_LAST- HARD_REGNO_NREGS (dummy, mode)) + 1)  */
    return !(regno & 1) || (size <= UNITS_PER_WORD);
  else if (CE_REG_P (regno))
    return (class == MODE_INT
            && ((size <= UNITS_PER_WORD)
                || (regno == CE_REG_FIRST && size == 2 * UNITS_PER_WORD)));
  else
    return (class == MODE_INT) && (size <= UNITS_PER_WORD);
}

/* Implement INITIAL_ELIMINATION_OFFSET.  FROM is either the frame
   pointer or argument pointer.  TO is either the stack pointer or
   hard frame pointer.  */
HOST_WIDE_INT
score_initial_elimination_offset (int from,
                                  int to ATTRIBUTE_UNUSED)
{
  struct score_frame_info *f = mda_compute_frame_size (get_frame_size ());
  switch (from)
    {
    case ARG_POINTER_REGNUM:
      return f->total_size;
    case FRAME_POINTER_REGNUM:
      return 0;
    default:
      gcc_unreachable ();
    }
}

/* Argument support functions.  */

/* Initialize CUMULATIVE_ARGS for a function.  */
void
score_init_cumulative_args (CUMULATIVE_ARGS *cum,
                            tree fntype ATTRIBUTE_UNUSED,
                            rtx libname ATTRIBUTE_UNUSED)
{
  memset (cum, 0, sizeof (CUMULATIVE_ARGS));
}

/* Implement FUNCTION_ARG_ADVANCE macro.  */
void
score_function_arg_advance (CUMULATIVE_ARGS *cum, enum machine_mode mode,
                            tree type, int named)
{
  struct score_arg_info info;
  classify_arg (cum, mode, type, named, &info);
  cum->num_gprs = info.reg_offset + info.reg_words;
  if (info.stack_words > 0)
    cum->stack_words = info.stack_offset + info.stack_words;
  cum->arg_number++;
}

/* Implement TARGET_ARG_PARTIAL_BYTES macro.  */
static int
score_arg_partial_bytes (const CUMULATIVE_ARGS *cum,
                         enum machine_mode mode, tree type, int named)
{
  struct score_arg_info info;
  classify_arg (cum, mode, type, named, &info);
  return info.stack_words > 0 ? info.reg_words * UNITS_PER_WORD : 0;
}

/* Implement FUNCTION_ARG macro.  */
rtx
score_function_arg (const CUMULATIVE_ARGS *cum, enum machine_mode mode,
                    tree type, int named)
{
  struct score_arg_info info;

  if (mode == VOIDmode || !named)
    return 0;

  classify_arg (cum, mode, type, named, &info);

  if (info.reg_offset == ARG_REG_NUM)
    return 0;

  if (!info.stack_words)
    return gen_rtx_REG (mode, ARG_REG_FIRST + info.reg_offset);
  else
    {
      rtx ret = gen_rtx_PARALLEL (mode, rtvec_alloc (info.reg_words));
      unsigned int i, part_offset = 0;
      for (i = 0; i < info.reg_words; i++)
        {
          rtx reg;
          reg = gen_rtx_REG (SImode, ARG_REG_FIRST + info.reg_offset + i);
          XVECEXP (ret, 0, i) = gen_rtx_EXPR_LIST (SImode, reg,
                                                   GEN_INT (part_offset));
          part_offset += UNITS_PER_WORD;
        }
      return ret;
    }
}

/* Implement FUNCTION_VALUE and LIBCALL_VALUE.  For normal calls,
   VALTYPE is the return type and MODE is VOIDmode.  For libcalls,
   VALTYPE is null and MODE is the mode of the return value.  */
rtx
score_function_value (tree valtype, tree func ATTRIBUTE_UNUSED,
                      enum machine_mode mode)
{
  if (valtype)
    {
      int unsignedp;
      mode = TYPE_MODE (valtype);
      unsignedp = TYPE_UNSIGNED (valtype);
      mode = promote_mode (valtype, mode, &unsignedp, 1);
    }
  return gen_rtx_REG (mode, RT_REGNUM);
}

/* Implement INITIALIZE_TRAMPOLINE macro.  */
void
score_initialize_trampoline (rtx ADDR, rtx FUNC, rtx CHAIN)
{
#define FFCACHE          "_flush_cache"
#define CODE_SIZE        (TRAMPOLINE_INSNS * UNITS_PER_WORD)

  unsigned int tramp[TRAMPOLINE_INSNS] = {
    0x8103bc56,                         /* mv      r8, r3          */
    0x9000bc05,                         /* bl      0x0x8           */
    0xc1238000 | (CODE_SIZE - 8),       /* lw      r9, &func       */
    0xc0038000
    | (STATIC_CHAIN_REGNUM << 21)
    | (CODE_SIZE - 4),                  /* lw  static chain reg, &chain */
    0x8068bc56,                         /* mv      r3, r8          */
    0x8009bc08,                         /* br      r9              */
    0x0,
    0x0,
    };
  rtx pfunc, pchain;
  int i;

  for (i = 0; i < TRAMPOLINE_INSNS; i++)
    emit_move_insn (gen_rtx_MEM (ptr_mode, plus_constant (ADDR, i << 2)),
                    GEN_INT (tramp[i]));

  pfunc = plus_constant (ADDR, CODE_SIZE);
  pchain = plus_constant (ADDR, CODE_SIZE + GET_MODE_SIZE (ptr_mode));

  emit_move_insn (gen_rtx_MEM (ptr_mode, pfunc), FUNC);
  emit_move_insn (gen_rtx_MEM (ptr_mode, pchain), CHAIN);
  emit_library_call (gen_rtx_SYMBOL_REF (Pmode, FFCACHE),
                     0, VOIDmode, 2,
                     ADDR, Pmode,
                     GEN_INT (TRAMPOLINE_SIZE), SImode);
#undef FFCACHE
#undef CODE_SIZE
}

/* This function is used to implement REG_MODE_OK_FOR_BASE_P macro.  */
int
score_regno_mode_ok_for_base_p (int regno, int strict)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      if (!strict)
        return 1;
      regno = reg_renumber[regno];
    }
  if (regno == ARG_POINTER_REGNUM
      || regno == FRAME_POINTER_REGNUM)
    return 1;
  return GP_REG_P (regno);
}

/* Implement GO_IF_LEGITIMATE_ADDRESS macro.  */
int
score_address_p (enum machine_mode mode, rtx x, int strict)
{
  struct score_address_info addr;

  return mda_classify_address (&addr, mode, x, strict);
}

/* Copy VALUE to a register and return that register.  If new psuedos
   are allowed, copy it into a new register, otherwise use DEST.  */
static rtx
score_force_temporary (rtx dest, rtx value)
{
  if (!no_new_pseudos)
    return force_reg (Pmode, value);
  else
    {
      emit_move_insn (copy_rtx (dest), value);
      return dest;
    }
}

/* Return a LO_SUM expression for ADDR.  TEMP is as for score_force_temporary
   and is used to load the high part into a register.  */
static rtx
score_split_symbol (rtx temp, rtx addr)
{
  rtx high = score_force_temporary (temp,
                                    gen_rtx_HIGH (Pmode, copy_rtx (addr)));
  return gen_rtx_LO_SUM (Pmode, high, addr);
}

/* This function is used to implement LEGITIMIZE_ADDRESS.  If *XLOC can
   be legitimized in a way that the generic machinery might not expect,
   put the new address in *XLOC and return true.  */
int
score_legitimize_address (rtx *xloc)
{
  enum score_symbol_type symbol_type;

  if (mda_symbolic_constant_p (*xloc, &symbol_type)
      && symbol_type == SYMBOL_GENERAL)
    {
      *xloc = score_split_symbol (0, *xloc);
      return 1;
    }

  if (GET_CODE (*xloc) == PLUS
      && GET_CODE (XEXP (*xloc, 1)) == CONST_INT)
    {
      rtx reg = XEXP (*xloc, 0);
      if (!mda_valid_base_register_p (reg, 0))
        reg = copy_to_mode_reg (Pmode, reg);
      *xloc = score_add_offset (NULL, reg, INTVAL (XEXP (*xloc, 1)));
      return 1;
    }
  return 0;
}

/* Return a number assessing the cost of moving a register in class
   FROM to class TO. */
int
score_register_move_cost (enum machine_mode mode ATTRIBUTE_UNUSED,
                          enum reg_class from, enum reg_class to)
{
  if (GR_REG_CLASS_P (from))
    {
      if (GR_REG_CLASS_P (to))
        return 2;
      else if (SP_REG_CLASS_P (to))
        return 4;
      else if (CP_REG_CLASS_P (to))
        return 5;
      else if (CE_REG_CLASS_P (to))
        return 6;
    }
  if (GR_REG_CLASS_P (to))
    {
      if (GR_REG_CLASS_P (from))
        return 2;
      else if (SP_REG_CLASS_P (from))
        return 4;
      else if (CP_REG_CLASS_P (from))
        return 5;
      else if (CE_REG_CLASS_P (from))
        return 6;
    }
  return 12;
}

/* Return the number of instructions needed to load a symbol of the
   given type into a register.  */
static int
score_symbol_insns (enum score_symbol_type type)
{
  switch (type)
    {
    case SYMBOL_GENERAL:
      return 2;

    case SYMBOL_SMALL_DATA:
      return 1;
    }

  gcc_unreachable ();
}

/* Return the number of instructions needed to load or store a value
   of mode MODE at X.  Return 0 if X isn't valid for MODE.  */
static int
score_address_insns (rtx x, enum machine_mode mode)
{
  struct score_address_info addr;
  int factor;

  if (mode == BLKmode)
    factor = 1;
  else
    factor = (GET_MODE_SIZE (mode) + UNITS_PER_WORD - 1) / UNITS_PER_WORD;

  if (mda_classify_address (&addr, mode, x, false))
    switch (addr.type)
      {
      case ADD_REG:
      case ADD_CONST_INT:
        return factor;

      case ADD_SYMBOLIC:
        return factor * score_symbol_insns (addr.symbol_type);
      }
  return 0;
}

/* Implement TARGET_RTX_COSTS macro.  */
static bool
score_rtx_costs (rtx x, enum rtx_code code, enum rtx_code outer_code,
                 int *total)
{
  enum machine_mode mode = GET_MODE (x);

  switch (code)
    {
    case CONST_INT:
      if (outer_code == SET)
        {
          if (CONST_OK_FOR_LETTER_P (INTVAL (x), 'I')
              || CONST_OK_FOR_LETTER_P (INTVAL (x), 'L'))
            *total = COSTS_N_INSNS (1);
          else
            *total = COSTS_N_INSNS (2);
        }
      else if (outer_code == PLUS || outer_code == MINUS)
        {
          if (CONST_OK_FOR_LETTER_P (INTVAL (x), 'N'))
            *total = 0;
          else if (CONST_OK_FOR_LETTER_P (INTVAL (x), 'I')
                   || CONST_OK_FOR_LETTER_P (INTVAL (x), 'L'))
            *total = 1;
          else
            *total = COSTS_N_INSNS (2);
        }
      else if (outer_code == AND || outer_code == IOR)
        {
          if (CONST_OK_FOR_LETTER_P (INTVAL (x), 'M'))
            *total = 0;
          else if (CONST_OK_FOR_LETTER_P (INTVAL (x), 'I')
                   || CONST_OK_FOR_LETTER_P (INTVAL (x), 'K'))
            *total = 1;
          else
            *total = COSTS_N_INSNS (2);
        }
      else
        {
          *total = 0;
        }
      return true;

    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
    case CONST_DOUBLE:
      *total = COSTS_N_INSNS (2);
      return true;

    case MEM:
      {
        /* If the address is legitimate, return the number of
           instructions it needs, otherwise use the default handling.  */
        int n = score_address_insns (XEXP (x, 0), GET_MODE (x));
        if (n > 0)
          {
            *total = COSTS_N_INSNS (n + 1);
            return true;
          }
        return false;
      }

    case FFS:
      *total = COSTS_N_INSNS (6);
      return true;

    case NOT:
      *total = COSTS_N_INSNS (1);
      return true;

    case AND:
    case IOR:
    case XOR:
      if (mode == DImode)
        {
          *total = COSTS_N_INSNS (2);
          return true;
        }
      return false;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (mode == DImode)
        {
          *total = COSTS_N_INSNS ((GET_CODE (XEXP (x, 1)) == CONST_INT)
                                  ? 4 : 12);
          return true;
        }
      return false;

    case ABS:
      *total = COSTS_N_INSNS (4);
      return true;

    case PLUS:
    case MINUS:
      if (mode == DImode)
        {
          *total = COSTS_N_INSNS (4);
          return true;
        }
      *total = COSTS_N_INSNS (1);
      return true;

    case NEG:
      if (mode == DImode)
        {
          *total = COSTS_N_INSNS (4);
          return true;
        }
      return false;

    case MULT:
      *total = optimize_size ? COSTS_N_INSNS (2) : COSTS_N_INSNS (12);
      return true;

    case DIV:
    case MOD:
    case UDIV:
    case UMOD:
      *total = optimize_size ? COSTS_N_INSNS (2) : COSTS_N_INSNS (33);
      return true;

    case SIGN_EXTEND:
    case ZERO_EXTEND:
      switch (GET_MODE (XEXP (x, 0)))
        {
        case QImode:
        case HImode:
          if (GET_CODE (XEXP (x, 0)) == MEM)
            {
              *total = COSTS_N_INSNS (2);

              if (!TARGET_LITTLE_ENDIAN &&
                  side_effects_p (XEXP (XEXP (x, 0), 0)))
                *total = 100;
            }
          else
            *total = COSTS_N_INSNS (1);
          break;

        default:
          *total = COSTS_N_INSNS (1);
          break;
        }
      return true;

    default:
      return false;
    }
}

/* Implement TARGET_ADDRESS_COST macro.  */
int
score_address_cost (rtx addr)
{
  return score_address_insns (addr, SImode);
}

/* Implement ASM_OUTPUT_EXTERNAL macro.  */
int
score_output_external (FILE *file ATTRIBUTE_UNUSED,
                       tree decl, const char *name)
{
  register struct extern_list *p;

  if (th_in_small_data_p (decl))
    {
      p = (struct extern_list *) ggc_alloc (sizeof (struct extern_list));
      p->next = extern_head;
      p->name = name;
      p->size = int_size_in_bytes (TREE_TYPE (decl));
      extern_head = p;
    }
  return 0;
}

/* Output format asm string.  */
void
score_declare_object (FILE *stream, const char *name,
                      const char *directive, const char *fmt, ...)
{
  va_list ap;
  fputs (directive, stream);
  assemble_name (stream, name);
  va_start (ap, fmt);
  vfprintf (stream, fmt, ap);
  va_end (ap);
}

/* Implement RETURN_ADDR_RTX.  Note, we do not support moving
   back to a previous frame.  */
rtx
score_return_addr (int count, rtx frame ATTRIBUTE_UNUSED)
{
  if (count != 0)
    return const0_rtx;
  return get_hard_reg_initial_val (Pmode, RA_REGNUM);
}

/* Implement PRINT_OPERAND macro.  */
/* Score-specific operand codes:
   '['        print .set nor1 directive
   ']'        print .set r1 directive
   'U'        print hi part of a CONST_INT rtx
   'E'        print log2(v)
   'F'        print log2(~v)
   'D'        print SFmode const double
   'S'        selectively print "!" if operand is 15bit instruction accessible
   'V'        print "v!" if operand is 15bit instruction accessible, or "lfh!"
   'L'        low  part of DImode reg operand
   'H'        high part of DImode reg operand
   'C'        print part of opcode for a branch condition.  */
void
score_print_operand (FILE *file, rtx op, int c)
{
  enum rtx_code code = -1;
  if (!PRINT_OPERAND_PUNCT_VALID_P (c))
    code = GET_CODE (op);

  if (c == '[')
    {
      fprintf (file, ".set r1\n");
    }
  else if (c == ']')
    {
      fprintf (file, "\n\t.set nor1");
    }
  else if (c == 'U')
    {
      gcc_assert (code == CONST_INT);
      fprintf (file, HOST_WIDE_INT_PRINT_HEX,
               (INTVAL (op) >> 16) & 0xffff);
    }
  else if (c == 'D')
    {
      if (GET_CODE (op) == CONST_DOUBLE)
        {
          rtx temp = gen_lowpart (SImode, op);
          gcc_assert (GET_MODE (op) == SFmode);
          fprintf (file, HOST_WIDE_INT_PRINT_HEX, INTVAL (temp) & 0xffffffff);
        }
      else
        output_addr_const (file, op);
    }
  else if (c == 'S')
    {
      gcc_assert (code == REG);
      if (G16_REG_P (REGNO (op)))
        fprintf (file, "!");
    }
  else if (c == 'V')
    {
      gcc_assert (code == REG);
      fprintf (file, G16_REG_P (REGNO (op)) ? "v!" : "lfh!");
    }
  else if (c == 'C')
    {
      enum machine_mode mode = GET_MODE (XEXP (op, 0));

      switch (code)
        {
        case EQ: fputs ("eq", file); break;
        case NE: fputs ("ne", file); break;
        case GT: fputs ("gt", file); break;
        case GE: fputs (mode != CCmode ? "pl" : "ge", file); break;
        case LT: fputs (mode != CCmode ? "mi" : "lt", file); break;
        case LE: fputs ("le", file); break;
        case GTU: fputs ("gtu", file); break;
        case GEU: fputs ("cs", file); break;
        case LTU: fputs ("cc", file); break;
        case LEU: fputs ("leu", file); break;
        default:
          output_operand_lossage ("invalid operand for code: '%c'", code);
        }
    }
  else if (c == 'E')
    {
      unsigned HOST_WIDE_INT i;
      unsigned HOST_WIDE_INT pow2mask = 1;
      unsigned HOST_WIDE_INT val;

      val = INTVAL (op);
      for (i = 0; i < 32; i++)
        {
          if (val == pow2mask)
            break;
          pow2mask <<= 1;
        }
      gcc_assert (i < 32);
      fprintf (file, HOST_WIDE_INT_PRINT_HEX, i);
    }
  else if (c == 'F')
    {
      unsigned HOST_WIDE_INT i;
      unsigned HOST_WIDE_INT pow2mask = 1;
      unsigned HOST_WIDE_INT val;

      val = ~INTVAL (op);
      for (i = 0; i < 32; i++)
        {
          if (val == pow2mask)
            break;
          pow2mask <<= 1;
        }
      gcc_assert (i < 32);
      fprintf (file, HOST_WIDE_INT_PRINT_HEX, i);
    }
  else if (code == REG)
    {
      int regnum = REGNO (op);
      if ((c == 'H' && !WORDS_BIG_ENDIAN)
          || (c == 'L' && WORDS_BIG_ENDIAN))
        regnum ++;
      fprintf (file, "%s", reg_names[regnum]);
    }
  else
    {
      switch (code)
        {
        case MEM:
          score_print_operand_address (file, op);
          break;
        default:
          output_addr_const (file, op);
        }
    }
}

/* Implement PRINT_OPERAND_ADDRESS macro.  */
void
score_print_operand_address (FILE *file, rtx x)
{
  struct score_address_info addr;
  enum rtx_code code = GET_CODE (x);
  enum machine_mode mode = GET_MODE (x);

  if (code == MEM)
    x = XEXP (x, 0);

  if (mda_classify_address (&addr, mode, x, true))
    {
      switch (addr.type)
        {
        case ADD_REG:
          {
            switch (addr.code)
              {
              case PRE_DEC:
                fprintf (file, "[%s,-%ld]+", reg_names[REGNO (addr.reg)],
                         INTVAL (addr.offset));
                break;
              case POST_DEC:
                fprintf (file, "[%s]+,-%ld", reg_names[REGNO (addr.reg)],
                         INTVAL (addr.offset));
                break;
              case PRE_INC:
                fprintf (file, "[%s, %ld]+", reg_names[REGNO (addr.reg)],
                         INTVAL (addr.offset));
                break;
              case POST_INC:
                fprintf (file, "[%s]+, %ld", reg_names[REGNO (addr.reg)],
                         INTVAL (addr.offset));
                break;
              default:
                fprintf (file, "[%s,%ld]", reg_names[REGNO (addr.reg)],
                         INTVAL (addr.offset));
                break;
              }
          }
          return;
        case ADD_CONST_INT:
        case ADD_SYMBOLIC:
          output_addr_const (file, x);
          return;
        }
    }
  print_rtl (stderr, x);
  gcc_unreachable ();
}

/* Implement SELECT_CC_MODE macro.  */
enum machine_mode
score_select_cc_mode (enum rtx_code op, rtx x, rtx y)
{
  if ((op == EQ || op == NE || op == LT || op == GE)
      && y == const0_rtx
      && GET_MODE (x) == SImode)
    {
      switch (GET_CODE (x))
        {
        case PLUS:
        case MINUS:
        case NEG:
        case AND:
        case IOR:
        case XOR:
        case NOT:
        case ASHIFT:
        case LSHIFTRT:
        case ASHIFTRT:
          return CC_NZmode;

        case SIGN_EXTEND:
        case ZERO_EXTEND:
        case ROTATE:
        case ROTATERT:
          return (op == LT || op == GE) ? CC_Nmode : CCmode;

        default:
          return CCmode;
        }
    }

  if ((op == EQ || op == NE)
      && (GET_CODE (y) == NEG)
      && register_operand (XEXP (y, 0), SImode)
      && register_operand (x, SImode))
    {
      return CC_NZmode;
    }

  return CCmode;
}

struct gcc_target targetm = TARGET_INITIALIZER;
