/* Subroutines for insn-output.c for Motorola 68000 family.
   Copyright (C) 1987, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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
#include "tree.h"
#include "rtl.h"
#include "function.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "output.h"
#include "insn-attr.h"
#include "recog.h"
#include "toplev.h"
#include "expr.h"
#include "reload.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"
#include "debug.h"
#include "flags.h"

enum reg_class regno_reg_class[] =
{
  DATA_REGS, DATA_REGS, DATA_REGS, DATA_REGS,
  DATA_REGS, DATA_REGS, DATA_REGS, DATA_REGS,
  ADDR_REGS, ADDR_REGS, ADDR_REGS, ADDR_REGS,
  ADDR_REGS, ADDR_REGS, ADDR_REGS, ADDR_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  FP_REGS, FP_REGS, FP_REGS, FP_REGS,
  ADDR_REGS
};


/* The ASM_DOT macro allows easy string pasting to handle the differences
   between MOTOROLA and MIT syntaxes in asm_fprintf(), which doesn't
   support the %. option.  */
#if MOTOROLA
# define ASM_DOT "."
# define ASM_DOTW ".w"
# define ASM_DOTL ".l"
#else
# define ASM_DOT ""
# define ASM_DOTW ""
# define ASM_DOTL ""
#endif


/* Structure describing stack frame layout.  */
struct m68k_frame
{
  /* Stack pointer to frame pointer offset.  */
  HOST_WIDE_INT offset;

  /* Offset of FPU registers.  */
  HOST_WIDE_INT foffset;

  /* Frame size in bytes (rounded up).  */
  HOST_WIDE_INT size;

  /* Data and address register.  */
  int reg_no;
  unsigned int reg_mask;
  unsigned int reg_rev_mask;

  /* FPU registers.  */
  int fpu_no;
  unsigned int fpu_mask;
  unsigned int fpu_rev_mask;

  /* Offsets relative to ARG_POINTER.  */
  HOST_WIDE_INT frame_pointer_offset;
  HOST_WIDE_INT stack_pointer_offset;

  /* Function which the above information refers to.  */
  int funcdef_no;
};

/* Current frame information calculated by m68k_compute_frame_layout().  */
static struct m68k_frame current_frame;

static bool m68k_handle_option (size_t, const char *, int);
static rtx find_addr_reg (rtx);
static const char *singlemove_string (rtx *);
static void m68k_output_function_prologue (FILE *, HOST_WIDE_INT);
static void m68k_output_function_epilogue (FILE *, HOST_WIDE_INT);
#ifdef M68K_TARGET_COFF
static void m68k_coff_asm_named_section (const char *, unsigned int, tree);
#endif /* M68K_TARGET_COFF */
static void m68k_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
					  HOST_WIDE_INT, tree);
static rtx m68k_struct_value_rtx (tree, int);
static bool m68k_interrupt_function_p (tree func);
static tree m68k_handle_fndecl_attribute (tree *node, tree name,
					  tree args, int flags,
					  bool *no_add_attrs);
static void m68k_compute_frame_layout (void);
static bool m68k_save_reg (unsigned int regno, bool interrupt_handler);
static int const_int_cost (rtx);
static bool m68k_rtx_costs (rtx, int, int, int *);


/* Specify the identification number of the library being built */
const char *m68k_library_id_string = "_current_shared_library_a5_offset_";

/* Nonzero if the last compare/test insn had FP operands.  The
   sCC expanders peek at this to determine what to do for the
   68060, which has no fsCC instructions.  */
int m68k_last_compare_had_fp_operands;

/* Initialize the GCC target structure.  */

#if INT_OP_GROUP == INT_OP_DOT_WORD
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"
#endif

#if INT_OP_GROUP == INT_OP_NO_DOT
#undef TARGET_ASM_BYTE_OP
#define TARGET_ASM_BYTE_OP "\tbyte\t"
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\tshort\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\tlong\t"
#endif

#if INT_OP_GROUP == INT_OP_DC
#undef TARGET_ASM_BYTE_OP
#define TARGET_ASM_BYTE_OP "\tdc.b\t"
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\tdc.w\t"
#undef TARGET_ASM_ALIGNED_SI_OP
#define TARGET_ASM_ALIGNED_SI_OP "\tdc.l\t"
#endif

#undef TARGET_ASM_UNALIGNED_HI_OP
#define TARGET_ASM_UNALIGNED_HI_OP TARGET_ASM_ALIGNED_HI_OP
#undef TARGET_ASM_UNALIGNED_SI_OP
#define TARGET_ASM_UNALIGNED_SI_OP TARGET_ASM_ALIGNED_SI_OP

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE m68k_output_function_prologue
#undef TARGET_ASM_FUNCTION_EPILOGUE
#define TARGET_ASM_FUNCTION_EPILOGUE m68k_output_function_epilogue

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK m68k_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

#undef TARGET_ASM_FILE_START_APP_OFF
#define TARGET_ASM_FILE_START_APP_OFF true

#undef TARGET_DEFAULT_TARGET_FLAGS
#define TARGET_DEFAULT_TARGET_FLAGS (TARGET_DEFAULT | MASK_STRICT_ALIGNMENT)
#undef TARGET_HANDLE_OPTION
#define TARGET_HANDLE_OPTION m68k_handle_option

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS m68k_rtx_costs

#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE m68k_attribute_table

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX m68k_struct_value_rtx

static const struct attribute_spec m68k_attribute_table[] =
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req, handler } */
  { "interrupt_handler", 0, 0, true,  false, false, m68k_handle_fndecl_attribute },
  { NULL,                0, 0, false, false, false, NULL }
};

struct gcc_target targetm = TARGET_INITIALIZER;

/* These bits are controlled by all CPU selection options.  Many options
   also control MASK_68881, but some (notably -m68020) leave it alone.  */

#define MASK_ALL_CPU_BITS \
  (MASK_COLDFIRE | MASK_CF_HWDIV | MASK_68060 | MASK_68040 \
   | MASK_68040_ONLY | MASK_68030 | MASK_68020 | MASK_BITFIELD)

/* Implement TARGET_HANDLE_OPTION.  */

static bool
m68k_handle_option (size_t code, const char *arg, int value)
{
  switch (code)
    {
    case OPT_m5200:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_5200;
      return true;

    case OPT_m5206e:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_5200 | MASK_CF_HWDIV;
      return true;

    case OPT_m528x:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_528x | MASK_CF_HWDIV;
      return true;

    case OPT_m5307:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_CFV3 | MASK_CF_HWDIV;
      return true;

    case OPT_m5407:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_CFV4 | MASK_CF_HWDIV;
      return true;

    case OPT_mcfv4e:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_CFV4 | MASK_CF_HWDIV | MASK_CFV4E;
      return true;

    case OPT_m68000:
    case OPT_mc68000:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      return true;

    case OPT_m68020:
    case OPT_mc68020:
      target_flags &= ~MASK_ALL_CPU_BITS;
      target_flags |= MASK_68020 | MASK_BITFIELD;
      return true;

    case OPT_m68020_40:
      target_flags &= ~MASK_ALL_CPU_BITS;
      target_flags |= MASK_BITFIELD | MASK_68881 | MASK_68020 | MASK_68040;
      return true;

    case OPT_m68020_60:
      target_flags &= ~MASK_ALL_CPU_BITS;
      target_flags |= (MASK_BITFIELD | MASK_68881 | MASK_68020
		       | MASK_68040 | MASK_68060);
      return true;

    case OPT_m68030:
      target_flags &= ~MASK_ALL_CPU_BITS;
      target_flags |= MASK_68020 | MASK_68030 | MASK_BITFIELD;
      return true;

    case OPT_m68040:
      target_flags &= ~MASK_ALL_CPU_BITS;
      target_flags |= (MASK_68020 | MASK_68881 | MASK_BITFIELD
		       | MASK_68040_ONLY | MASK_68040);
      return true;

    case OPT_m68060:
      target_flags &= ~MASK_ALL_CPU_BITS;
      target_flags |= (MASK_68020 | MASK_68881 | MASK_BITFIELD
		       | MASK_68040_ONLY | MASK_68060);
      return true;

    case OPT_m68302:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      return true;

    case OPT_m68332:
    case OPT_mcpu32:
      target_flags &= ~(MASK_ALL_CPU_BITS | MASK_68881);
      target_flags |= MASK_68020;
      return true;

    case OPT_mshared_library_id_:
      if (value > MAX_LIBRARY_ID)
	error ("-mshared-library-id=%s is not between 0 and %d",
	       arg, MAX_LIBRARY_ID);
      else
	asprintf ((char **) &m68k_library_id_string, "%d", (value * -4) - 4);
      return true;

    default:
      return true;
    }
}

/* Sometimes certain combinations of command options do not make
   sense on a particular target machine.  You can define a macro
   `OVERRIDE_OPTIONS' to take account of this.  This macro, if
   defined, is executed once just after all the command options have
   been parsed.

   Don't use this macro to turn on various extra optimizations for
   `-O'.  That is what `OPTIMIZATION_OPTIONS' is for.  */

void
override_options (void)
{
  /* Sanity check to ensure that msep-data and mid-sahred-library are not
   * both specified together.  Doing so simply doesn't make sense.
   */
  if (TARGET_SEP_DATA && TARGET_ID_SHARED_LIBRARY)
    error ("cannot specify both -msep-data and -mid-shared-library");

  /* If we're generating code for a separate A5 relative data segment,
   * we've got to enable -fPIC as well.  This might be relaxable to
   * -fpic but it hasn't been tested properly.
   */
  if (TARGET_SEP_DATA || TARGET_ID_SHARED_LIBRARY)
    flag_pic = 2;

  /* -fPIC uses 32-bit pc-relative displacements, which don't exist
     until the 68020.  */
  if (!TARGET_68020 && !TARGET_COLDFIRE && (flag_pic == 2))
    error ("-fPIC is not currently supported on the 68000 or 68010");

  /* ??? A historic way of turning on pic, or is this intended to
     be an embedded thing that doesn't have the same name binding
     significance that it does on hosted ELF systems?  */
  if (TARGET_PCREL && flag_pic == 0)
    flag_pic = 1;

  /* Turn off function cse if we are doing PIC.  We always want function call
     to be done as `bsr foo@PLTPC', so it will force the assembler to create
     the PLT entry for `foo'. Doing function cse will cause the address of
     `foo' to be loaded into a register, which is exactly what we want to
     avoid when we are doing PIC on svr4 m68k.  */
  if (flag_pic)
    flag_no_function_cse = 1;

  SUBTARGET_OVERRIDE_OPTIONS;
}

/* Return nonzero if FUNC is an interrupt function as specified by the
   "interrupt_handler" attribute.  */
static bool
m68k_interrupt_function_p(tree func)
{
  tree a;

  if (TREE_CODE (func) != FUNCTION_DECL)
    return false;

  a = lookup_attribute ("interrupt_handler", DECL_ATTRIBUTES (func));
  return (a != NULL_TREE);
}

/* Handle an attribute requiring a FUNCTION_DECL; arguments as in
   struct attribute_spec.handler.  */
static tree
m68k_handle_fndecl_attribute (tree *node, tree name,
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

static void
m68k_compute_frame_layout (void)
{
  int regno, saved;
  unsigned int mask, rmask;
  bool interrupt_handler = m68k_interrupt_function_p (current_function_decl);

  /* Only compute the frame once per function.
     Don't cache information until reload has been completed.  */
  if (current_frame.funcdef_no == current_function_funcdef_no
      && reload_completed)
    return;

  current_frame.size = (get_frame_size () + 3) & -4;

  mask = rmask = saved = 0;
  for (regno = 0; regno < 16; regno++)
    if (m68k_save_reg (regno, interrupt_handler))
      {
	mask |= 1 << regno;
	rmask |= 1 << (15 - regno);
	saved++;
      }
  current_frame.offset = saved * 4;
  current_frame.reg_no = saved;
  current_frame.reg_mask = mask;
  current_frame.reg_rev_mask = rmask;

  current_frame.foffset = 0;
  mask = rmask = saved = 0;
  if (TARGET_HARD_FLOAT)
    {
      for (regno = 16; regno < 24; regno++)
	if (m68k_save_reg (regno, interrupt_handler))
	  {
	    mask |= 1 << (regno - 16);
	    rmask |= 1 << (23 - regno);
	    saved++;
	  }
      current_frame.foffset = saved * TARGET_FP_REG_SIZE;
      current_frame.offset += current_frame.foffset;
    }
  current_frame.fpu_no = saved;
  current_frame.fpu_mask = mask;
  current_frame.fpu_rev_mask = rmask;

  /* Remember what function this frame refers to.  */
  current_frame.funcdef_no = current_function_funcdef_no;
}

HOST_WIDE_INT
m68k_initial_elimination_offset (int from, int to)
{
  int argptr_offset;
  /* The arg pointer points 8 bytes before the start of the arguments,
     as defined by FIRST_PARM_OFFSET.  This makes it coincident with the
     frame pointer in most frames.  */
  argptr_offset = frame_pointer_needed ? 0 : UNITS_PER_WORD;
  if (from == ARG_POINTER_REGNUM && to == FRAME_POINTER_REGNUM)
    return argptr_offset;

  m68k_compute_frame_layout ();

  gcc_assert (to == STACK_POINTER_REGNUM);
  switch (from)
    {
    case ARG_POINTER_REGNUM:
      return current_frame.offset + current_frame.size - argptr_offset;
    case FRAME_POINTER_REGNUM:
      return current_frame.offset + current_frame.size;
    default:
      gcc_unreachable ();
    }
}

/* Refer to the array `regs_ever_live' to determine which registers
   to save; `regs_ever_live[I]' is nonzero if register number I
   is ever used in the function.  This function is responsible for
   knowing which registers should not be saved even if used.
   Return true if we need to save REGNO.  */

static bool
m68k_save_reg (unsigned int regno, bool interrupt_handler)
{
  if (flag_pic && regno == PIC_OFFSET_TABLE_REGNUM)
    {
      if (current_function_uses_pic_offset_table)
	return true;
      if (!current_function_is_leaf && TARGET_ID_SHARED_LIBRARY)
	return true;
    }

  if (current_function_calls_eh_return)
    {
      unsigned int i;
      for (i = 0; ; i++)
	{
	  unsigned int test = EH_RETURN_DATA_REGNO (i);
	  if (test == INVALID_REGNUM)
	    break;
	  if (test == regno)
	    return true;
	}
    }

  /* Fixed regs we never touch.  */
  if (fixed_regs[regno])
    return false;

  /* The frame pointer (if it is such) is handled specially.  */
  if (regno == FRAME_POINTER_REGNUM && frame_pointer_needed)
    return false;

  /* Interrupt handlers must also save call_used_regs
     if they are live or when calling nested functions.  */
  if (interrupt_handler)
    {
      if (regs_ever_live[regno])
	return true;

      if (!current_function_is_leaf && call_used_regs[regno])
	return true;
    }

  /* Never need to save registers that aren't touched.  */
  if (!regs_ever_live[regno])
    return false;

  /* Otherwise save everything that isn't call-clobbered.  */
  return !call_used_regs[regno];
}

/* This function generates the assembly code for function entry.
   STREAM is a stdio stream to output the code to.
   SIZE is an int: how many units of temporary storage to allocate.  */

static void
m68k_output_function_prologue (FILE *stream,
			       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT fsize_with_regs;
  HOST_WIDE_INT cfa_offset = INCOMING_FRAME_SP_OFFSET;

  m68k_compute_frame_layout();

  /* If the stack limit is a symbol, we can check it here,
     before actually allocating the space.  */
  if (current_function_limit_stack
      && GET_CODE (stack_limit_rtx) == SYMBOL_REF)
    asm_fprintf (stream, "\tcmp" ASM_DOT "l %I%s+%wd,%Rsp\n\ttrapcs\n",
		 XSTR (stack_limit_rtx, 0), current_frame.size + 4);

  /* On ColdFire add register save into initial stack frame setup, if possible.  */
  fsize_with_regs = current_frame.size;
  if (TARGET_COLDFIRE)
    {
      if (current_frame.reg_no > 2)
	fsize_with_regs += current_frame.reg_no * 4;
      if (current_frame.fpu_no)
	fsize_with_regs += current_frame.fpu_no * 8;
    }

  if (frame_pointer_needed)
    {
      if (current_frame.size == 0 && TARGET_68040)
	/* on the 68040, pea + move is faster than link.w 0 */
	fprintf (stream, (MOTOROLA
			  ? "\tpea (%s)\n\tmove.l %s,%s\n"
			  : "\tpea %s@\n\tmovel %s,%s\n"),
		 M68K_REGNAME (FRAME_POINTER_REGNUM),
		 M68K_REGNAME (STACK_POINTER_REGNUM),
		 M68K_REGNAME (FRAME_POINTER_REGNUM));
      else if (fsize_with_regs < 0x8000)
	asm_fprintf (stream, "\tlink" ASM_DOTW " %s,%I%wd\n",
		     M68K_REGNAME (FRAME_POINTER_REGNUM), -fsize_with_regs);
      else if (TARGET_68020)
	asm_fprintf (stream, "\tlink" ASM_DOTL " %s,%I%wd\n",
		     M68K_REGNAME (FRAME_POINTER_REGNUM), -fsize_with_regs);
      else
	/* Adding negative number is faster on the 68040.  */
	asm_fprintf (stream,
		     "\tlink" ASM_DOTW " %s,%I0\n"
		     "\tadd" ASM_DOT "l %I%wd,%Rsp\n",
		     M68K_REGNAME (FRAME_POINTER_REGNUM), -fsize_with_regs);
    }
  else if (fsize_with_regs) /* !frame_pointer_needed */
    {
      if (fsize_with_regs < 0x8000)
	{
	  if (fsize_with_regs <= 8)
	    {
	      if (!TARGET_COLDFIRE)
		asm_fprintf (stream, "\tsubq" ASM_DOT "w %I%wd,%Rsp\n",
		             fsize_with_regs);
	      else
		asm_fprintf (stream, "\tsubq" ASM_DOT "l %I%wd,%Rsp\n",
		             fsize_with_regs);
	    }
	  else if (fsize_with_regs <= 16 && TARGET_CPU32)
	    /* On the CPU32 it is faster to use two subqw instructions to
	       subtract a small integer (8 < N <= 16) to a register.  */
	    asm_fprintf (stream,
			 "\tsubq" ASM_DOT "w %I8,%Rsp\n"
			 "\tsubq" ASM_DOT "w %I%wd,%Rsp\n",
			 fsize_with_regs - 8);
	  else if (TARGET_68040)
	    /* Adding negative number is faster on the 68040.  */
	    asm_fprintf (stream, "\tadd" ASM_DOT "w %I%wd,%Rsp\n",
			 -fsize_with_regs);
	  else
	    asm_fprintf (stream, (MOTOROLA
				  ? "\tlea (%wd,%Rsp),%Rsp\n"
				  : "\tlea %Rsp@(%wd),%Rsp\n"),
			 -fsize_with_regs);
	}
      else /* fsize_with_regs >= 0x8000 */
	asm_fprintf (stream, "\tadd" ASM_DOT "l %I%wd,%Rsp\n",
		     -fsize_with_regs);
    } /* !frame_pointer_needed */

  if (dwarf2out_do_frame ())
    {
      if (frame_pointer_needed)
	{
	  char *l;
	  l = (char *) dwarf2out_cfi_label ();
	  cfa_offset += 4;
	  dwarf2out_reg_save (l, FRAME_POINTER_REGNUM, -cfa_offset);
	  dwarf2out_def_cfa (l, FRAME_POINTER_REGNUM, cfa_offset);
	  cfa_offset += current_frame.size;
        }
      else
        {
	  cfa_offset += current_frame.size;
	  dwarf2out_def_cfa ("", STACK_POINTER_REGNUM, cfa_offset);
        }
    }

  if (current_frame.fpu_mask)
    {
      if (TARGET_68881)
	{
	  asm_fprintf (stream, (MOTOROLA
				? "\tfmovm %I0x%x,-(%Rsp)\n"
				: "\tfmovem %I0x%x,%Rsp@-\n"),
		       current_frame.fpu_mask);
	}
      else
	{
	  int offset;

	  /* stack already has registers in it.  Find the offset from
	     the bottom of stack to where the FP registers go */
	  if (current_frame.reg_no <= 2)
	    offset = 0;
	  else
	    offset = current_frame.reg_no * 4;
	  if (offset)
	    asm_fprintf (stream,
			 "\tfmovem %I0x%x,%d(%Rsp)\n",
			 current_frame.fpu_rev_mask,
			 offset);
	  else
	    asm_fprintf (stream,
			 "\tfmovem %I0x%x,(%Rsp)\n",
			 current_frame.fpu_rev_mask);
	}

      if (dwarf2out_do_frame ())
	{
	  char *l = (char *) dwarf2out_cfi_label ();
	  int n_regs, regno;

	  cfa_offset += current_frame.fpu_no * TARGET_FP_REG_SIZE;
	  if (! frame_pointer_needed)
	    dwarf2out_def_cfa (l, STACK_POINTER_REGNUM, cfa_offset);
	  for (regno = 16, n_regs = 0; regno < 24; regno++)
	    if (current_frame.fpu_mask & (1 << (regno - 16)))
	      dwarf2out_reg_save (l, regno, -cfa_offset
				  + n_regs++ * TARGET_FP_REG_SIZE);
	}
    }

  /* If the stack limit is not a symbol, check it here.
     This has the disadvantage that it may be too late...  */
  if (current_function_limit_stack)
    {
      if (REG_P (stack_limit_rtx))
	asm_fprintf (stream, "\tcmp" ASM_DOT "l %s,%Rsp\n\ttrapcs\n",
		     M68K_REGNAME (REGNO (stack_limit_rtx)));
      else if (GET_CODE (stack_limit_rtx) != SYMBOL_REF)
	warning (0, "stack limit expression is not supported");
    }

  if (current_frame.reg_no <= 2)
    {
      /* Store each separately in the same order moveml uses.
         Using two movel instructions instead of a single moveml
         is about 15% faster for the 68020 and 68030 at no expense
         in code size.  */

      int i;

      for (i = 0; i < 16; i++)
        if (current_frame.reg_rev_mask & (1 << i))
	  {
	    asm_fprintf (stream, (MOTOROLA
				  ? "\t%Omove.l %s,-(%Rsp)\n"
				  : "\tmovel %s,%Rsp@-\n"),
			 M68K_REGNAME (15 - i));
	    if (dwarf2out_do_frame ())
	      {
		char *l = (char *) dwarf2out_cfi_label ();

		cfa_offset += 4;
		if (! frame_pointer_needed)
		  dwarf2out_def_cfa (l, STACK_POINTER_REGNUM, cfa_offset);
		dwarf2out_reg_save (l, 15 - i, -cfa_offset);
	      }
	  }
    }
  else if (current_frame.reg_rev_mask)
    {
      if (TARGET_COLDFIRE)
	/* The ColdFire does not support the predecrement form of the
	   MOVEM instruction, so we must adjust the stack pointer and
	   then use the plain address register indirect mode.
	   The required register save space was combined earlier with
	   the fsize_with_regs amount.  */

	asm_fprintf (stream, (MOTOROLA
			      ? "\tmovm.l %I0x%x,(%Rsp)\n"
			      : "\tmoveml %I0x%x,%Rsp@\n"),
		     current_frame.reg_mask);
      else
	asm_fprintf (stream, (MOTOROLA
			      ? "\tmovm.l %I0x%x,-(%Rsp)\n"
			      : "\tmoveml %I0x%x,%Rsp@-\n"),
		     current_frame.reg_rev_mask);
      if (dwarf2out_do_frame ())
	{
	  char *l = (char *) dwarf2out_cfi_label ();
	  int n_regs, regno;

	  cfa_offset += current_frame.reg_no * 4;
	  if (! frame_pointer_needed)
	    dwarf2out_def_cfa (l, STACK_POINTER_REGNUM, cfa_offset);
	  for (regno = 0, n_regs = 0; regno < 16; regno++)
	    if (current_frame.reg_mask & (1 << regno))
	      dwarf2out_reg_save (l, regno, -cfa_offset + n_regs++ * 4);
	}
    }
  if (!TARGET_SEP_DATA && flag_pic
      && (current_function_uses_pic_offset_table
	  || (!current_function_is_leaf && TARGET_ID_SHARED_LIBRARY)))
    {
      if (TARGET_ID_SHARED_LIBRARY)
	{
	  asm_fprintf (stream, "\tmovel %s@(%s), %s\n",
		       M68K_REGNAME (PIC_OFFSET_TABLE_REGNUM),
		       m68k_library_id_string,
		       M68K_REGNAME (PIC_OFFSET_TABLE_REGNUM));
	}
      else
	{
	  if (MOTOROLA)
	    asm_fprintf (stream,
			 "\t%Olea (%Rpc, %U_GLOBAL_OFFSET_TABLE_@GOTPC), %s\n",
			 M68K_REGNAME (PIC_OFFSET_TABLE_REGNUM));
	  else
	    {
	      asm_fprintf (stream, "\tmovel %I%U_GLOBAL_OFFSET_TABLE_, %s\n",
			   M68K_REGNAME (PIC_OFFSET_TABLE_REGNUM));
	      asm_fprintf (stream, "\tlea %Rpc@(0,%s:l),%s\n",
			   M68K_REGNAME (PIC_OFFSET_TABLE_REGNUM),
			   M68K_REGNAME (PIC_OFFSET_TABLE_REGNUM));
	    }
	}
    }
}

/* Return true if this function's epilogue can be output as RTL.  */

bool
use_return_insn (void)
{
  if (!reload_completed || frame_pointer_needed || get_frame_size () != 0)
    return false;

  /* We can output the epilogue as RTL only if no registers need to be
     restored.  */
  m68k_compute_frame_layout ();
  return current_frame.reg_no ? false : true;
}

/* This function generates the assembly code for function exit,
   on machines that need it.

   The function epilogue should not depend on the current stack pointer!
   It should use the frame pointer only, if there is a frame pointer.
   This is mandatory because of alloca; we also take advantage of it to
   omit stack adjustments before returning.  */

static void
m68k_output_function_epilogue (FILE *stream,
			       HOST_WIDE_INT size ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT fsize, fsize_with_regs;
  bool big = false;
  bool restore_from_sp = false;
  rtx insn = get_last_insn ();

  m68k_compute_frame_layout ();

  /* If the last insn was a BARRIER, we don't have to write any code.  */
  if (GET_CODE (insn) == NOTE)
    insn = prev_nonnote_insn (insn);
  if (insn && GET_CODE (insn) == BARRIER)
    {
      /* Output just a no-op so that debuggers don't get confused
	 about which function the pc is in at this address.  */
      fprintf (stream, "\tnop\n");
      return;
    }

#ifdef FUNCTION_EXTRA_EPILOGUE
  FUNCTION_EXTRA_EPILOGUE (stream, size);
#endif

  fsize = current_frame.size;

  /* FIXME: leaf_function_p below is too strong.
     What we really need to know there is if there could be pending
     stack adjustment needed at that point.  */
  restore_from_sp
    = (! frame_pointer_needed
       || (! current_function_calls_alloca && leaf_function_p ()));

  /* fsize_with_regs is the size we need to adjust the sp when
     popping the frame.  */
  fsize_with_regs = fsize;

  /* Because the ColdFire doesn't support moveml with
     complex address modes, we must adjust the stack manually
     after restoring registers. When the frame pointer isn't used,
     we can merge movem adjustment into frame unlinking
     made immediately after it.  */
  if (TARGET_COLDFIRE && restore_from_sp)
    {
      if (current_frame.reg_no > 2)
	fsize_with_regs += current_frame.reg_no * 4;
      if (current_frame.fpu_no)
	fsize_with_regs += current_frame.fpu_no * 8;
    }

  if (current_frame.offset + fsize >= 0x8000
      && ! restore_from_sp
      && (current_frame.reg_mask || current_frame.fpu_mask))
    {
      /* Because the ColdFire doesn't support moveml with
         complex address modes we make an extra correction here.  */
      if (TARGET_COLDFIRE)
        fsize += current_frame.offset;

      asm_fprintf (stream, "\t%Omove" ASM_DOT "l %I%wd,%Ra1\n", -fsize);
      fsize = 0, big = true;
    }
  if (current_frame.reg_no <= 2)
    {
      /* Restore each separately in the same order moveml does.
         Using two movel instructions instead of a single moveml
         is about 15% faster for the 68020 and 68030 at no expense
         in code size.  */

      int i;
      HOST_WIDE_INT offset = current_frame.offset + fsize;

      for (i = 0; i < 16; i++)
        if (current_frame.reg_mask & (1 << i))
          {
            if (big)
	      {
		if (MOTOROLA)
		  asm_fprintf (stream, "\t%Omove.l -%wd(%s,%Ra1.l),%s\n",
			       offset,
			       M68K_REGNAME (FRAME_POINTER_REGNUM),
			       M68K_REGNAME (i));
		else
		  asm_fprintf (stream, "\tmovel %s@(-%wd,%Ra1:l),%s\n",
			       M68K_REGNAME (FRAME_POINTER_REGNUM),
			       offset,
			       M68K_REGNAME (i));
	      }
            else if (restore_from_sp)
	      asm_fprintf (stream, (MOTOROLA
				    ? "\t%Omove.l (%Rsp)+,%s\n"
				    : "\tmovel %Rsp@+,%s\n"),
			   M68K_REGNAME (i));
            else
	      {
	        if (MOTOROLA)
		  asm_fprintf (stream, "\t%Omove.l -%wd(%s),%s\n",
			       offset,
			       M68K_REGNAME (FRAME_POINTER_REGNUM),
			       M68K_REGNAME (i));
		else
		  asm_fprintf (stream, "\tmovel %s@(-%wd),%s\n",
			       M68K_REGNAME (FRAME_POINTER_REGNUM),
			       offset,
			       M68K_REGNAME (i));
	      }
            offset -= 4;
          }
    }
  else if (current_frame.reg_mask)
    {
      /* The ColdFire requires special handling due to its limited moveml
	 insn.  */
      if (TARGET_COLDFIRE)
        {
          if (big)
            {
              asm_fprintf (stream, "\tadd" ASM_DOT "l %s,%Ra1\n",
			   M68K_REGNAME (FRAME_POINTER_REGNUM));
              asm_fprintf (stream, (MOTOROLA
				    ? "\tmovm.l (%Ra1),%I0x%x\n"
				    : "\tmoveml %Ra1@,%I0x%x\n"),
			   current_frame.reg_mask);
	     }
	   else if (restore_from_sp)
	     asm_fprintf (stream, (MOTOROLA
				   ? "\tmovm.l (%Rsp),%I0x%x\n"
				   : "\tmoveml %Rsp@,%I0x%x\n"),
			  current_frame.reg_mask);
          else
            {
	      if (MOTOROLA)
		asm_fprintf (stream, "\tmovm.l -%wd(%s),%I0x%x\n",
			     current_frame.offset + fsize,
			     M68K_REGNAME (FRAME_POINTER_REGNUM),
			     current_frame.reg_mask);
	      else
		asm_fprintf (stream, "\tmoveml %s@(-%wd),%I0x%x\n",
			     M68K_REGNAME (FRAME_POINTER_REGNUM),
			     current_frame.offset + fsize,
			     current_frame.reg_mask);
	    }
        }
      else /* !TARGET_COLDFIRE */
	{
	  if (big)
	    {
	      if (MOTOROLA)
		asm_fprintf (stream, "\tmovm.l -%wd(%s,%Ra1.l),%I0x%x\n",
			     current_frame.offset + fsize,
			     M68K_REGNAME (FRAME_POINTER_REGNUM),
			     current_frame.reg_mask);
	      else
		asm_fprintf (stream, "\tmoveml %s@(-%wd,%Ra1:l),%I0x%x\n",
			     M68K_REGNAME (FRAME_POINTER_REGNUM),
			     current_frame.offset + fsize,
			     current_frame.reg_mask);
	    }
	  else if (restore_from_sp)
	    {
	      asm_fprintf (stream, (MOTOROLA
				    ? "\tmovm.l (%Rsp)+,%I0x%x\n"
				    : "\tmoveml %Rsp@+,%I0x%x\n"),
			   current_frame.reg_mask);
	    }
	  else
	    {
	      if (MOTOROLA)
		asm_fprintf (stream, "\tmovm.l -%wd(%s),%I0x%x\n",
			     current_frame.offset + fsize,
			     M68K_REGNAME (FRAME_POINTER_REGNUM),
			     current_frame.reg_mask);
	      else
		asm_fprintf (stream, "\tmoveml %s@(-%wd),%I0x%x\n",
			     M68K_REGNAME (FRAME_POINTER_REGNUM),
			     current_frame.offset + fsize,
			     current_frame.reg_mask);
	    }
	}
    }
  if (current_frame.fpu_rev_mask)
    {
      if (big)
	{
	  if (TARGET_COLDFIRE)
	    {
	      if (current_frame.reg_no)
		asm_fprintf (stream, MOTOROLA ?
			     "\tfmovem.d %d(%Ra1),%I0x%x\n" :
			     "\tfmovmd (%d,%Ra1),%I0x%x\n",
			     current_frame.reg_no * 4,
			     current_frame.fpu_rev_mask);
	      else
		asm_fprintf (stream, MOTOROLA ?
			     "\tfmovem.d (%Ra1),%I0x%x\n" :
			     "\tfmovmd (%Ra1),%I0x%x\n",
			     current_frame.fpu_rev_mask);
	    }
	  else if (MOTOROLA)
	    asm_fprintf (stream, "\tfmovm -%wd(%s,%Ra1.l),%I0x%x\n",
		         current_frame.foffset + fsize,
		         M68K_REGNAME (FRAME_POINTER_REGNUM),
		         current_frame.fpu_rev_mask);
	  else
	    asm_fprintf (stream, "\tfmovem %s@(-%wd,%Ra1:l),%I0x%x\n",
			 M68K_REGNAME (FRAME_POINTER_REGNUM),
			 current_frame.foffset + fsize,
			 current_frame.fpu_rev_mask);
	}
      else if (restore_from_sp)
	{
	  if (TARGET_COLDFIRE)
	    {
	      int offset;

	      /* Stack already has registers in it.  Find the offset from
		 the bottom of stack to where the FP registers go.  */
	      if (current_frame.reg_no <= 2)
		offset = 0;
	      else
		offset = current_frame.reg_no * 4;
	      if (offset)
		asm_fprintf (stream,
			     "\tfmovem %Rsp@(%d), %I0x%x\n",
			     offset, current_frame.fpu_rev_mask);
	      else
		asm_fprintf (stream,
			     "\tfmovem %Rsp@, %I0x%x\n",
			     current_frame.fpu_rev_mask);
	    }
	  else
	    asm_fprintf (stream, MOTOROLA ?
			 "\tfmovm (%Rsp)+,%I0x%x\n" :
			 "\tfmovem %Rsp@+,%I0x%x\n",
			 current_frame.fpu_rev_mask);
	}
      else
	{
	  if (MOTOROLA && !TARGET_COLDFIRE)
	    asm_fprintf (stream, "\tfmovm -%wd(%s),%I0x%x\n",
			 current_frame.foffset + fsize,
			 M68K_REGNAME (FRAME_POINTER_REGNUM),
			 current_frame.fpu_rev_mask);
	  else
	    asm_fprintf (stream, "\tfmovem %s@(-%wd),%I0x%x\n",
			 M68K_REGNAME (FRAME_POINTER_REGNUM),
			 current_frame.foffset + fsize,
			 current_frame.fpu_rev_mask);
	}
    }
  if (frame_pointer_needed)
    fprintf (stream, "\tunlk %s\n", M68K_REGNAME (FRAME_POINTER_REGNUM));
  else if (fsize_with_regs)
    {
      if (fsize_with_regs <= 8)
	{
	  if (!TARGET_COLDFIRE)
	    asm_fprintf (stream, "\taddq" ASM_DOT "w %I%wd,%Rsp\n",
			 fsize_with_regs);
	  else
	    asm_fprintf (stream, "\taddq" ASM_DOT "l %I%wd,%Rsp\n",
			 fsize_with_regs);
	}
      else if (fsize_with_regs <= 16 && TARGET_CPU32)
	{
	  /* On the CPU32 it is faster to use two addqw instructions to
	     add a small integer (8 < N <= 16) to a register.  */
	  asm_fprintf (stream,
		       "\taddq" ASM_DOT "w %I8,%Rsp\n"
		       "\taddq" ASM_DOT "w %I%wd,%Rsp\n",
		       fsize_with_regs - 8);
	}
      else if (fsize_with_regs < 0x8000)
	{
	  if (TARGET_68040)
	    asm_fprintf (stream, "\tadd" ASM_DOT "w %I%wd,%Rsp\n",
			 fsize_with_regs);
	  else
	    asm_fprintf (stream, (MOTOROLA
				  ? "\tlea (%wd,%Rsp),%Rsp\n"
				  : "\tlea %Rsp@(%wd),%Rsp\n"),
			 fsize_with_regs);
	}
      else
	asm_fprintf (stream, "\tadd" ASM_DOT "l %I%wd,%Rsp\n", fsize_with_regs);
    }
  if (current_function_calls_eh_return)
    asm_fprintf (stream, "\tadd" ASM_DOT "l %Ra0,%Rsp\n");
  if (m68k_interrupt_function_p (current_function_decl))
    fprintf (stream, "\trte\n");
  else if (current_function_pops_args)
    asm_fprintf (stream, "\trtd %I%d\n", current_function_pops_args);
  else
    fprintf (stream, "\trts\n");
}

/* Return true if X is a valid comparison operator for the dbcc 
   instruction.  

   Note it rejects floating point comparison operators.
   (In the future we could use Fdbcc).

   It also rejects some comparisons when CC_NO_OVERFLOW is set.  */
   
int
valid_dbcc_comparison_p_2 (rtx x, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  switch (GET_CODE (x))
    {
      case EQ: case NE: case GTU: case LTU:
      case GEU: case LEU:
        return 1;

      /* Reject some when CC_NO_OVERFLOW is set.  This may be over
         conservative */
      case GT: case LT: case GE: case LE:
        return ! (cc_prev_status.flags & CC_NO_OVERFLOW);
      default:
        return 0;
    }
}

/* Return nonzero if flags are currently in the 68881 flag register.  */
int
flags_in_68881 (void)
{
  /* We could add support for these in the future */
  return cc_status.flags & CC_IN_68881;
}

/* Output a BSR instruction suitable for PIC code.  */
void
m68k_output_pic_call (rtx dest)
{
  const char *out;

  if (!(GET_CODE (dest) == MEM && GET_CODE (XEXP (dest, 0)) == SYMBOL_REF))
    out = "jsr %0";
      /* We output a BSR instruction if we're building for a target that
	 supports long branches.  Otherwise we generate one of two sequences:
	 a shorter one that uses a GOT entry or a longer one that doesn't.
	 We'll use the -Os command-line flag to decide which to generate.
	 Both sequences take the same time to execute on the ColdFire.  */
  else if (TARGET_PCREL)
    out = "bsr.l %o0";
  else if (TARGET_68020)
#if defined(USE_GAS)
    out = "bsr.l %0@PLTPC";
#else
    out = "bsr %0@PLTPC";
#endif
  else if (optimize_size || TARGET_ID_SHARED_LIBRARY)
    out = "move.l %0@GOT(%%a5), %%a1\n\tjsr (%%a1)";
  else
    out = "lea %0-.-8,%%a1\n\tjsr 0(%%pc,%%a1)";

  output_asm_insn (out, &dest);
}

/* Output a dbCC; jCC sequence.  Note we do not handle the 
   floating point version of this sequence (Fdbcc).  We also
   do not handle alternative conditions when CC_NO_OVERFLOW is
   set.  It is assumed that valid_dbcc_comparison_p and flags_in_68881 will
   kick those out before we get here.  */

void
output_dbcc_and_branch (rtx *operands)
{
  switch (GET_CODE (operands[3]))
    {
      case EQ:
	output_asm_insn (MOTOROLA
			 ? "dbeq %0,%l1\n\tjbeq %l2"
			 : "dbeq %0,%l1\n\tjeq %l2",
			 operands);
	break;

      case NE:
	output_asm_insn (MOTOROLA
			 ? "dbne %0,%l1\n\tjbne %l2"
			 : "dbne %0,%l1\n\tjne %l2",
			 operands);
	break;

      case GT:
	output_asm_insn (MOTOROLA
			 ? "dbgt %0,%l1\n\tjbgt %l2"
			 : "dbgt %0,%l1\n\tjgt %l2",
			 operands);
	break;

      case GTU:
	output_asm_insn (MOTOROLA
			 ? "dbhi %0,%l1\n\tjbhi %l2"
			 : "dbhi %0,%l1\n\tjhi %l2",
			 operands);
	break;

      case LT:
	output_asm_insn (MOTOROLA
			 ? "dblt %0,%l1\n\tjblt %l2"
			 : "dblt %0,%l1\n\tjlt %l2",
			 operands);
	break;

      case LTU:
	output_asm_insn (MOTOROLA
			 ? "dbcs %0,%l1\n\tjbcs %l2"
			 : "dbcs %0,%l1\n\tjcs %l2",
			 operands);
	break;

      case GE:
	output_asm_insn (MOTOROLA
			 ? "dbge %0,%l1\n\tjbge %l2"
			 : "dbge %0,%l1\n\tjge %l2",
			 operands);
	break;

      case GEU:
	output_asm_insn (MOTOROLA
			 ? "dbcc %0,%l1\n\tjbcc %l2"
			 : "dbcc %0,%l1\n\tjcc %l2",
			 operands);
	break;

      case LE:
	output_asm_insn (MOTOROLA
			 ? "dble %0,%l1\n\tjble %l2"
			 : "dble %0,%l1\n\tjle %l2",
			 operands);
	break;

      case LEU:
	output_asm_insn (MOTOROLA
			 ? "dbls %0,%l1\n\tjbls %l2"
			 : "dbls %0,%l1\n\tjls %l2",
			 operands);
	break;

      default:
	gcc_unreachable ();
    }

  /* If the decrement is to be done in SImode, then we have
     to compensate for the fact that dbcc decrements in HImode.  */
  switch (GET_MODE (operands[0]))
    {
      case SImode:
        output_asm_insn (MOTOROLA
			 ? "clr%.w %0\n\tsubq%.l #1,%0\n\tjbpl %l1"
			 : "clr%.w %0\n\tsubq%.l #1,%0\n\tjpl %l1",
			 operands);
        break;

      case HImode:
        break;

      default:
        gcc_unreachable ();
    }
}

const char *
output_scc_di (rtx op, rtx operand1, rtx operand2, rtx dest)
{
  rtx loperands[7];
  enum rtx_code op_code = GET_CODE (op);

  /* This does not produce a useful cc.  */
  CC_STATUS_INIT;

  /* The m68k cmp.l instruction requires operand1 to be a reg as used
     below.  Swap the operands and change the op if these requirements
     are not fulfilled.  */
  if (GET_CODE (operand2) == REG && GET_CODE (operand1) != REG)
    {
      rtx tmp = operand1;

      operand1 = operand2;
      operand2 = tmp;
      op_code = swap_condition (op_code);
    }
  loperands[0] = operand1;
  if (GET_CODE (operand1) == REG)
    loperands[1] = gen_rtx_REG (SImode, REGNO (operand1) + 1);
  else
    loperands[1] = adjust_address (operand1, SImode, 4);
  if (operand2 != const0_rtx)
    {
      loperands[2] = operand2;
      if (GET_CODE (operand2) == REG)
	loperands[3] = gen_rtx_REG (SImode, REGNO (operand2) + 1);
      else
	loperands[3] = adjust_address (operand2, SImode, 4);
    }
  loperands[4] = gen_label_rtx ();
  if (operand2 != const0_rtx)
    {
      output_asm_insn (MOTOROLA
		       ? "cmp%.l %2,%0\n\tjbne %l4\n\tcmp%.l %3,%1"
		       : "cmp%.l %2,%0\n\tjne %l4\n\tcmp%.l %3,%1",
		       loperands);
    }
  else
    {
      if (TARGET_68020 || TARGET_COLDFIRE || ! ADDRESS_REG_P (loperands[0]))
	output_asm_insn ("tst%.l %0", loperands);
      else
	output_asm_insn ("cmp%.w #0,%0", loperands);

      output_asm_insn (MOTOROLA ? "jbne %l4" : "jne %l4", loperands);

      if (TARGET_68020 || TARGET_COLDFIRE || ! ADDRESS_REG_P (loperands[1]))
	output_asm_insn ("tst%.l %1", loperands);
      else
	output_asm_insn ("cmp%.w #0,%1", loperands);
    }

  loperands[5] = dest;

  switch (op_code)
    {
      case EQ:
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("seq %5", loperands);
        break;

      case NE:
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("sne %5", loperands);
        break;

      case GT:
        loperands[6] = gen_label_rtx ();
        output_asm_insn (MOTOROLA ? "shi %5\n\tjbra %l6" : "shi %5\n\tjra %l6",
			 loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("sgt %5", loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[6]));
        break;

      case GTU:
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("shi %5", loperands);
        break;

      case LT:
        loperands[6] = gen_label_rtx ();
        output_asm_insn (MOTOROLA ? "scs %5\n\tjbra %l6" : "scs %5\n\tjra %l6",
			 loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("slt %5", loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[6]));
        break;

      case LTU:
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("scs %5", loperands);
        break;

      case GE:
        loperands[6] = gen_label_rtx ();
        output_asm_insn (MOTOROLA ? "scc %5\n\tjbra %l6" : "scc %5\n\tjra %l6",
			 loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("sge %5", loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[6]));
        break;

      case GEU:
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("scc %5", loperands);
        break;

      case LE:
        loperands[6] = gen_label_rtx ();
        output_asm_insn (MOTOROLA ? "sls %5\n\tjbra %l6" : "sls %5\n\tjra %l6",
			 loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("sle %5", loperands);
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[6]));
        break;

      case LEU:
        (*targetm.asm_out.internal_label) (asm_out_file, "L",
					   CODE_LABEL_NUMBER (loperands[4]));
        output_asm_insn ("sls %5", loperands);
        break;

      default:
	gcc_unreachable ();
    }
  return "";
}

const char *
output_btst (rtx *operands, rtx countop, rtx dataop, rtx insn, int signpos)
{
  operands[0] = countop;
  operands[1] = dataop;

  if (GET_CODE (countop) == CONST_INT)
    {
      register int count = INTVAL (countop);
      /* If COUNT is bigger than size of storage unit in use,
	 advance to the containing unit of same size.  */
      if (count > signpos)
	{
	  int offset = (count & ~signpos) / 8;
	  count = count & signpos;
	  operands[1] = dataop = adjust_address (dataop, QImode, offset);
	}
      if (count == signpos)
	cc_status.flags = CC_NOT_POSITIVE | CC_Z_IN_NOT_N;
      else
	cc_status.flags = CC_NOT_NEGATIVE | CC_Z_IN_NOT_N;

      /* These three statements used to use next_insns_test_no...
	 but it appears that this should do the same job.  */
      if (count == 31
	  && next_insn_tests_no_inequality (insn))
	return "tst%.l %1";
      if (count == 15
	  && next_insn_tests_no_inequality (insn))
	return "tst%.w %1";
      if (count == 7
	  && next_insn_tests_no_inequality (insn))
	return "tst%.b %1";

      cc_status.flags = CC_NOT_NEGATIVE;
    }
  return "btst %0,%1";
}

/* Legitimize PIC addresses.  If the address is already
   position-independent, we return ORIG.  Newly generated
   position-independent addresses go to REG.  If we need more
   than one register, we lose.  

   An address is legitimized by making an indirect reference
   through the Global Offset Table with the name of the symbol
   used as an offset.  

   The assembler and linker are responsible for placing the 
   address of the symbol in the GOT.  The function prologue
   is responsible for initializing a5 to the starting address
   of the GOT.

   The assembler is also responsible for translating a symbol name
   into a constant displacement from the start of the GOT.  

   A quick example may make things a little clearer:

   When not generating PIC code to store the value 12345 into _foo
   we would generate the following code:

	movel #12345, _foo

   When generating PIC two transformations are made.  First, the compiler
   loads the address of foo into a register.  So the first transformation makes:

	lea	_foo, a0
	movel   #12345, a0@

   The code in movsi will intercept the lea instruction and call this
   routine which will transform the instructions into:

	movel   a5@(_foo:w), a0
	movel   #12345, a0@
   

   That (in a nutshell) is how *all* symbol and label references are 
   handled.  */

rtx
legitimize_pic_address (rtx orig, enum machine_mode mode ATTRIBUTE_UNUSED,
		        rtx reg)
{
  rtx pic_ref = orig;

  /* First handle a simple SYMBOL_REF or LABEL_REF */
  if (GET_CODE (orig) == SYMBOL_REF || GET_CODE (orig) == LABEL_REF)
    {
      gcc_assert (reg);

      pic_ref = gen_rtx_MEM (Pmode,
			     gen_rtx_PLUS (Pmode,
					   pic_offset_table_rtx, orig));
      current_function_uses_pic_offset_table = 1;
      MEM_READONLY_P (pic_ref) = 1;
      emit_move_insn (reg, pic_ref);
      return reg;
    }
  else if (GET_CODE (orig) == CONST)
    {
      rtx base;

      /* Make sure this has not already been legitimized.  */
      if (GET_CODE (XEXP (orig, 0)) == PLUS
	  && XEXP (XEXP (orig, 0), 0) == pic_offset_table_rtx)
	return orig;

      gcc_assert (reg);

      /* legitimize both operands of the PLUS */
      gcc_assert (GET_CODE (XEXP (orig, 0)) == PLUS);
      
      base = legitimize_pic_address (XEXP (XEXP (orig, 0), 0), Pmode, reg);
      orig = legitimize_pic_address (XEXP (XEXP (orig, 0), 1), Pmode,
				     base == reg ? 0 : reg);

      if (GET_CODE (orig) == CONST_INT)
	return plus_constant (base, INTVAL (orig));
      pic_ref = gen_rtx_PLUS (Pmode, base, orig);
      /* Likewise, should we set special REG_NOTEs here?  */
    }
  return pic_ref;
}


typedef enum { MOVL, SWAP, NEGW, NOTW, NOTB, MOVQ, MVS, MVZ } CONST_METHOD;

static CONST_METHOD const_method (rtx);

#define USE_MOVQ(i)	((unsigned) ((i) + 128) <= 255)

static CONST_METHOD
const_method (rtx constant)
{
  int i;
  unsigned u;

  i = INTVAL (constant);
  if (USE_MOVQ (i))
    return MOVQ;

  /* The ColdFire doesn't have byte or word operations.  */
  /* FIXME: This may not be useful for the m68060 either.  */
  if (!TARGET_COLDFIRE) 
    {
      /* if -256 < N < 256 but N is not in range for a moveq
	 N^ff will be, so use moveq #N^ff, dreg; not.b dreg.  */
      if (USE_MOVQ (i ^ 0xff))
	return NOTB;
      /* Likewise, try with not.w */
      if (USE_MOVQ (i ^ 0xffff))
	return NOTW;
      /* This is the only value where neg.w is useful */
      if (i == -65408)
	return NEGW;
    }

  /* Try also with swap.  */
  u = i;
  if (USE_MOVQ ((u >> 16) | (u << 16)))
    return SWAP;

  if (TARGET_CFV4)
    {
      /* Try using MVZ/MVS with an immediate value to load constants.  */
      if (i >= 0 && i <= 65535)
	return MVZ;
      if (i >= -32768 && i <= 32767)
	return MVS;
    }

  /* Otherwise, use move.l */
  return MOVL;
}

static int
const_int_cost (rtx constant)
{
  switch (const_method (constant))
    {
    case MOVQ:
      /* Constants between -128 and 127 are cheap due to moveq.  */
      return 0;
    case MVZ:
    case MVS:
    case NOTB:
    case NOTW:
    case NEGW:
    case SWAP:
      /* Constants easily generated by moveq + not.b/not.w/neg.w/swap.  */
      return 1;
    case MOVL:
      return 2;
    default:
      gcc_unreachable ();
    }
}

static bool
m68k_rtx_costs (rtx x, int code, int outer_code, int *total)
{
  switch (code)
    {
    case CONST_INT:
      /* Constant zero is super cheap due to clr instruction.  */
      if (x == const0_rtx)
	*total = 0;
      else
        *total = const_int_cost (x);
      return true;

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      *total = 3;
      return true;

    case CONST_DOUBLE:
      /* Make 0.0 cheaper than other floating constants to
         encourage creating tstsf and tstdf insns.  */
      if (outer_code == COMPARE
          && (x == CONST0_RTX (SFmode) || x == CONST0_RTX (DFmode)))
	*total = 4;
      else
	*total = 5;
      return true;

    /* These are vaguely right for a 68020.  */
    /* The costs for long multiply have been adjusted to work properly
       in synth_mult on the 68020, relative to an average of the time
       for add and the time for shift, taking away a little more because
       sometimes move insns are needed.  */
    /* div?.w is relatively cheaper on 68000 counted in COSTS_N_INSNS
       terms.  */
#define MULL_COST (TARGET_68060 ? 2 : TARGET_68040 ? 5		\
		   : (TARGET_COLDFIRE && !TARGET_5200) ? 3	\
		   : TARGET_COLDFIRE ? 10 : 13)
#define MULW_COST (TARGET_68060 ? 2 : TARGET_68040 ? 3 : TARGET_68020 ? 8 \
		   : (TARGET_COLDFIRE && !TARGET_5200) ? 2 : 5)
#define DIVW_COST (TARGET_68020 ? 27 : TARGET_CF_HWDIV ? 11 : 12)

    case PLUS:
      /* An lea costs about three times as much as a simple add.  */
      if (GET_MODE (x) == SImode
	  && GET_CODE (XEXP (x, 1)) == REG
	  && GET_CODE (XEXP (x, 0)) == MULT
	  && GET_CODE (XEXP (XEXP (x, 0), 0)) == REG
	  && GET_CODE (XEXP (XEXP (x, 0), 1)) == CONST_INT
	  && (INTVAL (XEXP (XEXP (x, 0), 1)) == 2
	      || INTVAL (XEXP (XEXP (x, 0), 1)) == 4
	      || INTVAL (XEXP (XEXP (x, 0), 1)) == 8))
	{
	    /* lea an@(dx:l:i),am */
	    *total = COSTS_N_INSNS (TARGET_COLDFIRE ? 2 : 3);
	    return true;
	}
      return false;

    case ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
      if (TARGET_68060)
	{
          *total = COSTS_N_INSNS(1);
	  return true;
	}
      if (! TARGET_68020 && ! TARGET_COLDFIRE)
        {
	  if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	    {
	      if (INTVAL (XEXP (x, 1)) < 16)
	        *total = COSTS_N_INSNS (2) + INTVAL (XEXP (x, 1)) / 2;
	      else
	        /* We're using clrw + swap for these cases.  */
	        *total = COSTS_N_INSNS (4) + (INTVAL (XEXP (x, 1)) - 16) / 2;
	    }
	  else
	    *total = COSTS_N_INSNS (10); /* Worst case.  */
	  return true;
        }
      /* A shift by a big integer takes an extra instruction.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && (INTVAL (XEXP (x, 1)) == 16))
	{
	  *total = COSTS_N_INSNS (2);	 /* clrw;swap */
	  return true;
	}
      if (GET_CODE (XEXP (x, 1)) == CONST_INT
	  && !(INTVAL (XEXP (x, 1)) > 0
	       && INTVAL (XEXP (x, 1)) <= 8))
	{
	  *total = COSTS_N_INSNS (TARGET_COLDFIRE ? 1 : 3);	 /* lsr #i,dn */
	  return true;
	}
      return false;

    case MULT:
      if ((GET_CODE (XEXP (x, 0)) == ZERO_EXTEND
	   || GET_CODE (XEXP (x, 0)) == SIGN_EXTEND)
	  && GET_MODE (x) == SImode)
        *total = COSTS_N_INSNS (MULW_COST);
      else if (GET_MODE (x) == QImode || GET_MODE (x) == HImode)
        *total = COSTS_N_INSNS (MULW_COST);
      else
        *total = COSTS_N_INSNS (MULL_COST);
      return true;

    case DIV:
    case UDIV:
    case MOD:
    case UMOD:
      if (GET_MODE (x) == QImode || GET_MODE (x) == HImode)
        *total = COSTS_N_INSNS (DIVW_COST);	/* div.w */
      else if (TARGET_CF_HWDIV)
        *total = COSTS_N_INSNS (18);
      else
	*total = COSTS_N_INSNS (43);		/* div.l */
      return true;

    default:
      return false;
    }
}

const char *
output_move_const_into_data_reg (rtx *operands)
{
  int i;

  i = INTVAL (operands[1]);
  switch (const_method (operands[1]))
    {
    case MVZ:
      return "mvzw %1,%0";
    case MVS:
      return "mvsw %1,%0";
    case MOVQ:
      return "moveq %1,%0";
    case NOTB:
      CC_STATUS_INIT;
      operands[1] = GEN_INT (i ^ 0xff);
      return "moveq %1,%0\n\tnot%.b %0";
    case NOTW:
      CC_STATUS_INIT;
      operands[1] = GEN_INT (i ^ 0xffff);
      return "moveq %1,%0\n\tnot%.w %0";
    case NEGW:
      CC_STATUS_INIT;
      return "moveq #-128,%0\n\tneg%.w %0";
    case SWAP:
      {
	unsigned u = i;

	operands[1] = GEN_INT ((u << 16) | (u >> 16));
	return "moveq %1,%0\n\tswap %0";
      }
    case MOVL:
	return "move%.l %1,%0";
    default:
	gcc_unreachable ();
    }
}

/* Return 1 if 'constant' can be represented by
   mov3q on a ColdFire V4 core.  */
int
valid_mov3q_const (rtx constant)
{
  int i;

  if (TARGET_CFV4 && GET_CODE (constant) == CONST_INT)
    {
      i = INTVAL (constant);
      if (i == -1 || (i >= 1 && i <= 7))
	return 1;
    }
  return 0;
}


const char *
output_move_simode_const (rtx *operands)
{
  if (operands[1] == const0_rtx
      && (DATA_REG_P (operands[0])
	  || GET_CODE (operands[0]) == MEM)
      /* clr insns on 68000 read before writing.
	 This isn't so on the 68010, but we have no TARGET_68010.  */
      && ((TARGET_68020 || TARGET_COLDFIRE)
	  || !(GET_CODE (operands[0]) == MEM
	       && MEM_VOLATILE_P (operands[0]))))
    return "clr%.l %0";
  else if ((GET_MODE (operands[0]) == SImode)
           && valid_mov3q_const (operands[1]))
    return "mov3q%.l %1,%0";
  else if (operands[1] == const0_rtx
	   && ADDRESS_REG_P (operands[0]))
    return "sub%.l %0,%0";
  else if (DATA_REG_P (operands[0]))
    return output_move_const_into_data_reg (operands);
  else if (ADDRESS_REG_P (operands[0])
	   && INTVAL (operands[1]) < 0x8000
	   && INTVAL (operands[1]) >= -0x8000)
    {
      if (valid_mov3q_const (operands[1]))
        return "mov3q%.l %1,%0";
      return "move%.w %1,%0";
    }
  else if (GET_CODE (operands[0]) == MEM
	   && GET_CODE (XEXP (operands[0], 0)) == PRE_DEC
	   && REGNO (XEXP (XEXP (operands[0], 0), 0)) == STACK_POINTER_REGNUM
	   && INTVAL (operands[1]) < 0x8000
	   && INTVAL (operands[1]) >= -0x8000)
    {
      if (valid_mov3q_const (operands[1]))
        return "mov3q%.l %1,%-";
      return "pea %a1";
    }
  return "move%.l %1,%0";
}

const char *
output_move_simode (rtx *operands)
{
  if (GET_CODE (operands[1]) == CONST_INT)
    return output_move_simode_const (operands);
  else if ((GET_CODE (operands[1]) == SYMBOL_REF
	    || GET_CODE (operands[1]) == CONST)
	   && push_operand (operands[0], SImode))
    return "pea %a1";
  else if ((GET_CODE (operands[1]) == SYMBOL_REF
	    || GET_CODE (operands[1]) == CONST)
	   && ADDRESS_REG_P (operands[0]))
    return "lea %a1,%0";
  return "move%.l %1,%0";
}

const char *
output_move_himode (rtx *operands)
{
 if (GET_CODE (operands[1]) == CONST_INT)
    {
      if (operands[1] == const0_rtx
	  && (DATA_REG_P (operands[0])
	      || GET_CODE (operands[0]) == MEM)
	  /* clr insns on 68000 read before writing.
	     This isn't so on the 68010, but we have no TARGET_68010.  */
	  && ((TARGET_68020 || TARGET_COLDFIRE)
	      || !(GET_CODE (operands[0]) == MEM
		   && MEM_VOLATILE_P (operands[0]))))
	return "clr%.w %0";
      else if (operands[1] == const0_rtx
	       && ADDRESS_REG_P (operands[0]))
	return "sub%.l %0,%0";
      else if (DATA_REG_P (operands[0])
	       && INTVAL (operands[1]) < 128
	       && INTVAL (operands[1]) >= -128)
	return "moveq %1,%0";
      else if (INTVAL (operands[1]) < 0x8000
	       && INTVAL (operands[1]) >= -0x8000)
	return "move%.w %1,%0";
    }
  else if (CONSTANT_P (operands[1]))
    return "move%.l %1,%0";
  /* Recognize the insn before a tablejump, one that refers
     to a table of offsets.  Such an insn will need to refer
     to a label on the insn.  So output one.  Use the label-number
     of the table of offsets to generate this label.  This code,
     and similar code below, assumes that there will be at most one
     reference to each table.  */
  if (GET_CODE (operands[1]) == MEM
      && GET_CODE (XEXP (operands[1], 0)) == PLUS
      && GET_CODE (XEXP (XEXP (operands[1], 0), 1)) == LABEL_REF
      && GET_CODE (XEXP (XEXP (operands[1], 0), 0)) != PLUS)
    {
      rtx labelref = XEXP (XEXP (operands[1], 0), 1);
      if (MOTOROLA)
	asm_fprintf (asm_out_file, "\t.set %LLI%d,.+2\n",
		     CODE_LABEL_NUMBER (XEXP (labelref, 0)));
      else
	(*targetm.asm_out.internal_label) (asm_out_file, "LI",
					   CODE_LABEL_NUMBER (XEXP (labelref, 0)));
    }
  return "move%.w %1,%0";
}

const char *
output_move_qimode (rtx *operands)
{
  /* 68k family always modifies the stack pointer by at least 2, even for
     byte pushes.  The 5200 (ColdFire) does not do this.  */
  
  /* This case is generated by pushqi1 pattern now.  */
  gcc_assert (!(GET_CODE (operands[0]) == MEM
		&& GET_CODE (XEXP (operands[0], 0)) == PRE_DEC
		&& XEXP (XEXP (operands[0], 0), 0) == stack_pointer_rtx
		&& ! ADDRESS_REG_P (operands[1])
		&& ! TARGET_COLDFIRE));

  /* clr and st insns on 68000 read before writing.
     This isn't so on the 68010, but we have no TARGET_68010.  */
  if (!ADDRESS_REG_P (operands[0])
      && ((TARGET_68020 || TARGET_COLDFIRE)
	  || !(GET_CODE (operands[0]) == MEM && MEM_VOLATILE_P (operands[0]))))
    {
      if (operands[1] == const0_rtx)
	return "clr%.b %0";
      if ((!TARGET_COLDFIRE || DATA_REG_P (operands[0]))
	  && GET_CODE (operands[1]) == CONST_INT
	  && (INTVAL (operands[1]) & 255) == 255)
	{
	  CC_STATUS_INIT;
	  return "st %0";
	}
    }
  if (GET_CODE (operands[1]) == CONST_INT
      && DATA_REG_P (operands[0])
      && INTVAL (operands[1]) < 128
      && INTVAL (operands[1]) >= -128)
    return "moveq %1,%0";
  if (operands[1] == const0_rtx && ADDRESS_REG_P (operands[0]))
    return "sub%.l %0,%0";
  if (GET_CODE (operands[1]) != CONST_INT && CONSTANT_P (operands[1]))
    return "move%.l %1,%0";
  /* 68k family (including the 5200 ColdFire) does not support byte moves to
     from address registers.  */
  if (ADDRESS_REG_P (operands[0]) || ADDRESS_REG_P (operands[1]))
    return "move%.w %1,%0";
  return "move%.b %1,%0";
}

const char *
output_move_stricthi (rtx *operands)
{
  if (operands[1] == const0_rtx
      /* clr insns on 68000 read before writing.
	 This isn't so on the 68010, but we have no TARGET_68010.  */
      && ((TARGET_68020 || TARGET_COLDFIRE)
	  || !(GET_CODE (operands[0]) == MEM && MEM_VOLATILE_P (operands[0]))))
    return "clr%.w %0";
  return "move%.w %1,%0";
}

const char *
output_move_strictqi (rtx *operands)
{
  if (operands[1] == const0_rtx
      /* clr insns on 68000 read before writing.
         This isn't so on the 68010, but we have no TARGET_68010.  */
      && ((TARGET_68020 || TARGET_COLDFIRE)
          || !(GET_CODE (operands[0]) == MEM && MEM_VOLATILE_P (operands[0]))))
    return "clr%.b %0";
  return "move%.b %1,%0";
}

/* Return the best assembler insn template
   for moving operands[1] into operands[0] as a fullword.  */

static const char *
singlemove_string (rtx *operands)
{
  if (GET_CODE (operands[1]) == CONST_INT)
    return output_move_simode_const (operands);
  return "move%.l %1,%0";
}


/* Output assembler code to perform a doubleword move insn
   with operands OPERANDS.  */

const char *
output_move_double (rtx *operands)
{
  enum
    {
      REGOP, OFFSOP, MEMOP, PUSHOP, POPOP, CNSTOP, RNDOP
    } optype0, optype1;
  rtx latehalf[2];
  rtx middlehalf[2];
  rtx xops[2];
  rtx addreg0 = 0, addreg1 = 0;
  int dest_overlapped_low = 0;
  int size = GET_MODE_SIZE (GET_MODE (operands[0]));

  middlehalf[0] = 0;
  middlehalf[1] = 0;

  /* First classify both operands.  */

  if (REG_P (operands[0]))
    optype0 = REGOP;
  else if (offsettable_memref_p (operands[0]))
    optype0 = OFFSOP;
  else if (GET_CODE (XEXP (operands[0], 0)) == POST_INC)
    optype0 = POPOP;
  else if (GET_CODE (XEXP (operands[0], 0)) == PRE_DEC)
    optype0 = PUSHOP;
  else if (GET_CODE (operands[0]) == MEM)
    optype0 = MEMOP;
  else
    optype0 = RNDOP;

  if (REG_P (operands[1]))
    optype1 = REGOP;
  else if (CONSTANT_P (operands[1]))
    optype1 = CNSTOP;
  else if (offsettable_memref_p (operands[1]))
    optype1 = OFFSOP;
  else if (GET_CODE (XEXP (operands[1], 0)) == POST_INC)
    optype1 = POPOP;
  else if (GET_CODE (XEXP (operands[1], 0)) == PRE_DEC)
    optype1 = PUSHOP;
  else if (GET_CODE (operands[1]) == MEM)
    optype1 = MEMOP;
  else
    optype1 = RNDOP;

  /* Check for the cases that the operand constraints are not supposed
     to allow to happen.  Generating code for these cases is
     painful.  */
  gcc_assert (optype0 != RNDOP && optype1 != RNDOP);

  /* If one operand is decrementing and one is incrementing
     decrement the former register explicitly
     and change that operand into ordinary indexing.  */

  if (optype0 == PUSHOP && optype1 == POPOP)
    {
      operands[0] = XEXP (XEXP (operands[0], 0), 0);
      if (size == 12)
        output_asm_insn ("sub%.l #12,%0", operands);
      else
        output_asm_insn ("subq%.l #8,%0", operands);
      if (GET_MODE (operands[1]) == XFmode)
	operands[0] = gen_rtx_MEM (XFmode, operands[0]);
      else if (GET_MODE (operands[0]) == DFmode)
	operands[0] = gen_rtx_MEM (DFmode, operands[0]);
      else
	operands[0] = gen_rtx_MEM (DImode, operands[0]);
      optype0 = OFFSOP;
    }
  if (optype0 == POPOP && optype1 == PUSHOP)
    {
      operands[1] = XEXP (XEXP (operands[1], 0), 0);
      if (size == 12)
        output_asm_insn ("sub%.l #12,%1", operands);
      else
        output_asm_insn ("subq%.l #8,%1", operands);
      if (GET_MODE (operands[1]) == XFmode)
	operands[1] = gen_rtx_MEM (XFmode, operands[1]);
      else if (GET_MODE (operands[1]) == DFmode)
	operands[1] = gen_rtx_MEM (DFmode, operands[1]);
      else
	operands[1] = gen_rtx_MEM (DImode, operands[1]);
      optype1 = OFFSOP;
    }

  /* If an operand is an unoffsettable memory ref, find a register
     we can increment temporarily to make it refer to the second word.  */

  if (optype0 == MEMOP)
    addreg0 = find_addr_reg (XEXP (operands[0], 0));

  if (optype1 == MEMOP)
    addreg1 = find_addr_reg (XEXP (operands[1], 0));

  /* Ok, we can do one word at a time.
     Normally we do the low-numbered word first,
     but if either operand is autodecrementing then we
     do the high-numbered word first.

     In either case, set up in LATEHALF the operands to use
     for the high-numbered word and in some cases alter the
     operands in OPERANDS to be suitable for the low-numbered word.  */

  if (size == 12)
    {
      if (optype0 == REGOP)
	{
	  latehalf[0] = gen_rtx_REG (SImode, REGNO (operands[0]) + 2);
	  middlehalf[0] = gen_rtx_REG (SImode, REGNO (operands[0]) + 1);
	}
      else if (optype0 == OFFSOP)
	{
	  middlehalf[0] = adjust_address (operands[0], SImode, 4);
	  latehalf[0] = adjust_address (operands[0], SImode, size - 4);
	}
      else
	{
	  middlehalf[0] = operands[0];
	  latehalf[0] = operands[0];
	}

      if (optype1 == REGOP)
	{
	  latehalf[1] = gen_rtx_REG (SImode, REGNO (operands[1]) + 2);
	  middlehalf[1] = gen_rtx_REG (SImode, REGNO (operands[1]) + 1);
	}
      else if (optype1 == OFFSOP)
	{
	  middlehalf[1] = adjust_address (operands[1], SImode, 4);
	  latehalf[1] = adjust_address (operands[1], SImode, size - 4);
	}
      else if (optype1 == CNSTOP)
	{
	  if (GET_CODE (operands[1]) == CONST_DOUBLE)
	    {
	      REAL_VALUE_TYPE r;
	      long l[3];

	      REAL_VALUE_FROM_CONST_DOUBLE (r, operands[1]);
	      REAL_VALUE_TO_TARGET_LONG_DOUBLE (r, l);
	      operands[1] = GEN_INT (l[0]);
	      middlehalf[1] = GEN_INT (l[1]);
	      latehalf[1] = GEN_INT (l[2]);
	    }
	  else
	    {
	      /* No non-CONST_DOUBLE constant should ever appear
		 here.  */
	      gcc_assert (!CONSTANT_P (operands[1]));
	    }
	}
      else
	{
	  middlehalf[1] = operands[1];
	  latehalf[1] = operands[1];
	}
    }
  else
    /* size is not 12: */
    {
      if (optype0 == REGOP)
	latehalf[0] = gen_rtx_REG (SImode, REGNO (operands[0]) + 1);
      else if (optype0 == OFFSOP)
	latehalf[0] = adjust_address (operands[0], SImode, size - 4);
      else
	latehalf[0] = operands[0];

      if (optype1 == REGOP)
	latehalf[1] = gen_rtx_REG (SImode, REGNO (operands[1]) + 1);
      else if (optype1 == OFFSOP)
	latehalf[1] = adjust_address (operands[1], SImode, size - 4);
      else if (optype1 == CNSTOP)
	split_double (operands[1], &operands[1], &latehalf[1]);
      else
	latehalf[1] = operands[1];
    }

  /* If insn is effectively movd N(sp),-(sp) then we will do the
     high word first.  We should use the adjusted operand 1 (which is N+4(sp))
     for the low word as well, to compensate for the first decrement of sp.  */
  if (optype0 == PUSHOP
      && REGNO (XEXP (XEXP (operands[0], 0), 0)) == STACK_POINTER_REGNUM
      && reg_overlap_mentioned_p (stack_pointer_rtx, operands[1]))
    operands[1] = middlehalf[1] = latehalf[1];

  /* For (set (reg:DI N) (mem:DI ... (reg:SI N) ...)),
     if the upper part of reg N does not appear in the MEM, arrange to
     emit the move late-half first.  Otherwise, compute the MEM address
     into the upper part of N and use that as a pointer to the memory
     operand.  */
  if (optype0 == REGOP
      && (optype1 == OFFSOP || optype1 == MEMOP))
    {
      rtx testlow = gen_rtx_REG (SImode, REGNO (operands[0]));

      if (reg_overlap_mentioned_p (testlow, XEXP (operands[1], 0))
	  && reg_overlap_mentioned_p (latehalf[0], XEXP (operands[1], 0)))
	{
	  /* If both halves of dest are used in the src memory address,
	     compute the address into latehalf of dest.
	     Note that this can't happen if the dest is two data regs.  */
	compadr:
	  xops[0] = latehalf[0];
	  xops[1] = XEXP (operands[1], 0);
	  output_asm_insn ("lea %a1,%0", xops);
	  if (GET_MODE (operands[1]) == XFmode )
	    {
	      operands[1] = gen_rtx_MEM (XFmode, latehalf[0]);
	      middlehalf[1] = adjust_address (operands[1], DImode, size - 8);
	      latehalf[1] = adjust_address (operands[1], DImode, size - 4);
	    }
	  else
	    {
	      operands[1] = gen_rtx_MEM (DImode, latehalf[0]);
	      latehalf[1] = adjust_address (operands[1], DImode, size - 4);
	    }
	}
      else if (size == 12
	       && reg_overlap_mentioned_p (middlehalf[0],
					   XEXP (operands[1], 0)))
	{
	  /* Check for two regs used by both source and dest.
	     Note that this can't happen if the dest is all data regs.
	     It can happen if the dest is d6, d7, a0.
	     But in that case, latehalf is an addr reg, so
	     the code at compadr does ok.  */

	  if (reg_overlap_mentioned_p (testlow, XEXP (operands[1], 0))
	      || reg_overlap_mentioned_p (latehalf[0], XEXP (operands[1], 0)))
	    goto compadr;

	  /* JRV says this can't happen: */
	  gcc_assert (!addreg0 && !addreg1);

	  /* Only the middle reg conflicts; simply put it last.  */
	  output_asm_insn (singlemove_string (operands), operands);
	  output_asm_insn (singlemove_string (latehalf), latehalf);
	  output_asm_insn (singlemove_string (middlehalf), middlehalf);
	  return "";
	}
      else if (reg_overlap_mentioned_p (testlow, XEXP (operands[1], 0)))
	/* If the low half of dest is mentioned in the source memory
	   address, the arrange to emit the move late half first.  */
	dest_overlapped_low = 1;
    }

  /* If one or both operands autodecrementing,
     do the two words, high-numbered first.  */

  /* Likewise,  the first move would clobber the source of the second one,
     do them in the other order.  This happens only for registers;
     such overlap can't happen in memory unless the user explicitly
     sets it up, and that is an undefined circumstance.  */

  if (optype0 == PUSHOP || optype1 == PUSHOP
      || (optype0 == REGOP && optype1 == REGOP
	  && ((middlehalf[1] && REGNO (operands[0]) == REGNO (middlehalf[1]))
	      || REGNO (operands[0]) == REGNO (latehalf[1])))
      || dest_overlapped_low)
    {
      /* Make any unoffsettable addresses point at high-numbered word.  */
      if (addreg0)
	{
	  if (size == 12)
	    output_asm_insn ("addq%.l #8,%0", &addreg0);
	  else
	    output_asm_insn ("addq%.l #4,%0", &addreg0);
	}
      if (addreg1)
	{
	  if (size == 12)
	    output_asm_insn ("addq%.l #8,%0", &addreg1);
	  else
	    output_asm_insn ("addq%.l #4,%0", &addreg1);
	}

      /* Do that word.  */
      output_asm_insn (singlemove_string (latehalf), latehalf);

      /* Undo the adds we just did.  */
      if (addreg0)
	output_asm_insn ("subq%.l #4,%0", &addreg0);
      if (addreg1)
	output_asm_insn ("subq%.l #4,%0", &addreg1);

      if (size == 12)
	{
	  output_asm_insn (singlemove_string (middlehalf), middlehalf);
	  if (addreg0)
	    output_asm_insn ("subq%.l #4,%0", &addreg0);
	  if (addreg1)
	    output_asm_insn ("subq%.l #4,%0", &addreg1);
	}

      /* Do low-numbered word.  */
      return singlemove_string (operands);
    }

  /* Normal case: do the two words, low-numbered first.  */

  output_asm_insn (singlemove_string (operands), operands);

  /* Do the middle one of the three words for long double */
  if (size == 12)
    {
      if (addreg0)
	output_asm_insn ("addq%.l #4,%0", &addreg0);
      if (addreg1)
	output_asm_insn ("addq%.l #4,%0", &addreg1);

      output_asm_insn (singlemove_string (middlehalf), middlehalf);
    }

  /* Make any unoffsettable addresses point at high-numbered word.  */
  if (addreg0)
    output_asm_insn ("addq%.l #4,%0", &addreg0);
  if (addreg1)
    output_asm_insn ("addq%.l #4,%0", &addreg1);

  /* Do that word.  */
  output_asm_insn (singlemove_string (latehalf), latehalf);

  /* Undo the adds we just did.  */
  if (addreg0)
    {
      if (size == 12)
        output_asm_insn ("subq%.l #8,%0", &addreg0);
      else
        output_asm_insn ("subq%.l #4,%0", &addreg0);
    }
  if (addreg1)
    {
      if (size == 12)
        output_asm_insn ("subq%.l #8,%0", &addreg1);
      else
        output_asm_insn ("subq%.l #4,%0", &addreg1);
    }

  return "";
}


/* Ensure mode of ORIG, a REG rtx, is MODE.  Returns either ORIG or a
   new rtx with the correct mode.  */

static rtx
force_mode (enum machine_mode mode, rtx orig)
{
  if (mode == GET_MODE (orig))
    return orig;

  if (REGNO (orig) >= FIRST_PSEUDO_REGISTER)
    abort ();

  return gen_rtx_REG (mode, REGNO (orig));
}

static int
fp_reg_operand (rtx op, enum machine_mode mode ATTRIBUTE_UNUSED)
{
  return reg_renumber && FP_REG_P (op);
}

/* Emit insns to move operands[1] into operands[0].

   Return 1 if we have written out everything that needs to be done to
   do the move.  Otherwise, return 0 and the caller will emit the move
   normally.

   Note SCRATCH_REG may not be in the proper mode depending on how it
   will be used.  This routine is responsible for creating a new copy
   of SCRATCH_REG in the proper mode.  */

int
emit_move_sequence (rtx *operands, enum machine_mode mode, rtx scratch_reg)
{
  register rtx operand0 = operands[0];
  register rtx operand1 = operands[1];
  register rtx tem;

  if (scratch_reg
      && reload_in_progress && GET_CODE (operand0) == REG
      && REGNO (operand0) >= FIRST_PSEUDO_REGISTER)
    operand0 = reg_equiv_mem[REGNO (operand0)];
  else if (scratch_reg
	   && reload_in_progress && GET_CODE (operand0) == SUBREG
	   && GET_CODE (SUBREG_REG (operand0)) == REG
	   && REGNO (SUBREG_REG (operand0)) >= FIRST_PSEUDO_REGISTER)
    {
     /* We must not alter SUBREG_BYTE (operand0) since that would confuse
	the code which tracks sets/uses for delete_output_reload.  */
      rtx temp = gen_rtx_SUBREG (GET_MODE (operand0),
				 reg_equiv_mem [REGNO (SUBREG_REG (operand0))],
				 SUBREG_BYTE (operand0));
      operand0 = alter_subreg (&temp);
    }

  if (scratch_reg
      && reload_in_progress && GET_CODE (operand1) == REG
      && REGNO (operand1) >= FIRST_PSEUDO_REGISTER)
    operand1 = reg_equiv_mem[REGNO (operand1)];
  else if (scratch_reg
	   && reload_in_progress && GET_CODE (operand1) == SUBREG
	   && GET_CODE (SUBREG_REG (operand1)) == REG
	   && REGNO (SUBREG_REG (operand1)) >= FIRST_PSEUDO_REGISTER)
    {
     /* We must not alter SUBREG_BYTE (operand0) since that would confuse
	the code which tracks sets/uses for delete_output_reload.  */
      rtx temp = gen_rtx_SUBREG (GET_MODE (operand1),
				 reg_equiv_mem [REGNO (SUBREG_REG (operand1))],
				 SUBREG_BYTE (operand1));
      operand1 = alter_subreg (&temp);
    }

  if (scratch_reg && reload_in_progress && GET_CODE (operand0) == MEM
      && ((tem = find_replacement (&XEXP (operand0, 0)))
	  != XEXP (operand0, 0)))
    operand0 = gen_rtx_MEM (GET_MODE (operand0), tem);
  if (scratch_reg && reload_in_progress && GET_CODE (operand1) == MEM
      && ((tem = find_replacement (&XEXP (operand1, 0)))
	  != XEXP (operand1, 0)))
    operand1 = gen_rtx_MEM (GET_MODE (operand1), tem);

  /* Handle secondary reloads for loads/stores of FP registers where
     the address is symbolic by using the scratch register */
  if (fp_reg_operand (operand0, mode)
      && ((GET_CODE (operand1) == MEM
	   && ! memory_address_p (DFmode, XEXP (operand1, 0)))
	  || ((GET_CODE (operand1) == SUBREG
	       && GET_CODE (XEXP (operand1, 0)) == MEM
	       && !memory_address_p (DFmode, XEXP (XEXP (operand1, 0), 0)))))
      && scratch_reg)
    {
      if (GET_CODE (operand1) == SUBREG)
	operand1 = XEXP (operand1, 0);

      /* SCRATCH_REG will hold an address.  We want
	 it in SImode regardless of what mode it was originally given
	 to us.  */
      scratch_reg = force_mode (SImode, scratch_reg);

      /* D might not fit in 14 bits either; for such cases load D into
	 scratch reg.  */
      if (!memory_address_p (Pmode, XEXP (operand1, 0)))
	{
	  emit_move_insn (scratch_reg, XEXP (XEXP (operand1, 0), 1));
	  emit_move_insn (scratch_reg, gen_rtx_fmt_ee (GET_CODE (XEXP (operand1, 0)),
						       Pmode,
						       XEXP (XEXP (operand1, 0), 0),
						       scratch_reg));
	}
      else
	emit_move_insn (scratch_reg, XEXP (operand1, 0));
      emit_insn (gen_rtx_SET (VOIDmode, operand0,
			      gen_rtx_MEM (mode, scratch_reg)));
      return 1;
    }
  else if (fp_reg_operand (operand1, mode)
	   && ((GET_CODE (operand0) == MEM
		&& ! memory_address_p (DFmode, XEXP (operand0, 0)))
	       || ((GET_CODE (operand0) == SUBREG)
		   && GET_CODE (XEXP (operand0, 0)) == MEM
		   && !memory_address_p (DFmode, XEXP (XEXP (operand0, 0), 0))))
	   && scratch_reg)
    {
      if (GET_CODE (operand0) == SUBREG)
	operand0 = XEXP (operand0, 0);

      /* SCRATCH_REG will hold an address and maybe the actual data.  We want
	 it in SIMODE regardless of what mode it was originally given
	 to us.  */
      scratch_reg = force_mode (SImode, scratch_reg);

      /* D might not fit in 14 bits either; for such cases load D into
	 scratch reg.  */
      if (!memory_address_p (Pmode, XEXP (operand0, 0)))
	{
	  emit_move_insn (scratch_reg, XEXP (XEXP (operand0, 0), 1));
	  emit_move_insn (scratch_reg, gen_rtx_fmt_ee (GET_CODE (XEXP (operand0,
								        0)),
						       Pmode,
						       XEXP (XEXP (operand0, 0),
								   0),
						       scratch_reg));
	}
      else
	emit_move_insn (scratch_reg, XEXP (operand0, 0));
      emit_insn (gen_rtx_SET (VOIDmode, gen_rtx_MEM (mode, scratch_reg),
			      operand1));
      return 1;
    }
  /* Handle secondary reloads for loads of FP registers from constant
     expressions by forcing the constant into memory.

     use scratch_reg to hold the address of the memory location.

     The proper fix is to change PREFERRED_RELOAD_CLASS to return
     NO_REGS when presented with a const_int and an register class
     containing only FP registers.  Doing so unfortunately creates
     more problems than it solves.   Fix this for 2.5.  */
  else if (fp_reg_operand (operand0, mode)
	   && CONSTANT_P (operand1)
	   && scratch_reg)
    {
      rtx xoperands[2];

      /* SCRATCH_REG will hold an address and maybe the actual data.  We want
	 it in SIMODE regardless of what mode it was originally given
	 to us.  */
      scratch_reg = force_mode (SImode, scratch_reg);

      /* Force the constant into memory and put the address of the
	 memory location into scratch_reg.  */
      xoperands[0] = scratch_reg;
      xoperands[1] = XEXP (force_const_mem (mode, operand1), 0);
      emit_insn (gen_rtx_SET (mode, scratch_reg, xoperands[1]));

      /* Now load the destination register.  */
      emit_insn (gen_rtx_SET (mode, operand0,
			      gen_rtx_MEM (mode, scratch_reg)));
      return 1;
    }

  /* Now have insn-emit do whatever it normally does.  */
  return 0;
}

/* Return a REG that occurs in ADDR with coefficient 1.
   ADDR can be effectively incremented by incrementing REG.  */

static rtx
find_addr_reg (rtx addr)
{
  while (GET_CODE (addr) == PLUS)
    {
      if (GET_CODE (XEXP (addr, 0)) == REG)
	addr = XEXP (addr, 0);
      else if (GET_CODE (XEXP (addr, 1)) == REG)
	addr = XEXP (addr, 1);
      else if (CONSTANT_P (XEXP (addr, 0)))
	addr = XEXP (addr, 1);
      else if (CONSTANT_P (XEXP (addr, 1)))
	addr = XEXP (addr, 0);
      else
	gcc_unreachable ();
    }
  gcc_assert (GET_CODE (addr) == REG);
  return addr;
}

/* Output assembler code to perform a 32-bit 3-operand add.  */

const char *
output_addsi3 (rtx *operands)
{
  if (! operands_match_p (operands[0], operands[1]))
    {
      if (!ADDRESS_REG_P (operands[1]))
	{
	  rtx tmp = operands[1];

	  operands[1] = operands[2];
	  operands[2] = tmp;
	}

      /* These insns can result from reloads to access
	 stack slots over 64k from the frame pointer.  */
      if (GET_CODE (operands[2]) == CONST_INT
	  && (INTVAL (operands[2]) < -32768 || INTVAL (operands[2]) > 32767))
        return "move%.l %2,%0\n\tadd%.l %1,%0";
      if (GET_CODE (operands[2]) == REG)
	return MOTOROLA ? "lea (%1,%2.l),%0" : "lea %1@(0,%2:l),%0";
      return MOTOROLA ? "lea (%c2,%1),%0" : "lea %1@(%c2),%0";
    }
  if (GET_CODE (operands[2]) == CONST_INT)
    {
      if (INTVAL (operands[2]) > 0
	  && INTVAL (operands[2]) <= 8)
	return "addq%.l %2,%0";
      if (INTVAL (operands[2]) < 0
	  && INTVAL (operands[2]) >= -8)
        {
	  operands[2] = GEN_INT (- INTVAL (operands[2]));
	  return "subq%.l %2,%0";
	}
      /* On the CPU32 it is faster to use two addql instructions to
	 add a small integer (8 < N <= 16) to a register.
	 Likewise for subql.  */
      if (TARGET_CPU32 && REG_P (operands[0]))
	{
	  if (INTVAL (operands[2]) > 8
	      && INTVAL (operands[2]) <= 16)
	    {
	      operands[2] = GEN_INT (INTVAL (operands[2]) - 8);
	      return "addq%.l #8,%0\n\taddq%.l %2,%0";
	    }
	  if (INTVAL (operands[2]) < -8
	      && INTVAL (operands[2]) >= -16)
	    {
	      operands[2] = GEN_INT (- INTVAL (operands[2]) - 8);
	      return "subq%.l #8,%0\n\tsubq%.l %2,%0";
	    }
	}
      if (ADDRESS_REG_P (operands[0])
	  && INTVAL (operands[2]) >= -0x8000
	  && INTVAL (operands[2]) < 0x8000)
	{
	  if (TARGET_68040)
	    return "add%.w %2,%0";
	  else
	    return MOTOROLA ? "lea (%c2,%0),%0" : "lea %0@(%c2),%0";
	}
    }
  return "add%.l %2,%0";
}

/* Store in cc_status the expressions that the condition codes will
   describe after execution of an instruction whose pattern is EXP.
   Do not alter them if the instruction would not alter the cc's.  */

/* On the 68000, all the insns to store in an address register fail to
   set the cc's.  However, in some cases these instructions can make it
   possibly invalid to use the saved cc's.  In those cases we clear out
   some or all of the saved cc's so they won't be used.  */

void
notice_update_cc (rtx exp, rtx insn)
{
  if (GET_CODE (exp) == SET)
    {
      if (GET_CODE (SET_SRC (exp)) == CALL)
	CC_STATUS_INIT; 
      else if (ADDRESS_REG_P (SET_DEST (exp)))
	{
	  if (cc_status.value1 && modified_in_p (cc_status.value1, insn))
	    cc_status.value1 = 0;
	  if (cc_status.value2 && modified_in_p (cc_status.value2, insn))
	    cc_status.value2 = 0; 
	}
      else if (!FP_REG_P (SET_DEST (exp))
	       && SET_DEST (exp) != cc0_rtx
	       && (FP_REG_P (SET_SRC (exp))
		   || GET_CODE (SET_SRC (exp)) == FIX
		   || GET_CODE (SET_SRC (exp)) == FLOAT_TRUNCATE
		   || GET_CODE (SET_SRC (exp)) == FLOAT_EXTEND))
	CC_STATUS_INIT; 
      /* A pair of move insns doesn't produce a useful overall cc.  */
      else if (!FP_REG_P (SET_DEST (exp))
	       && !FP_REG_P (SET_SRC (exp))
	       && GET_MODE_SIZE (GET_MODE (SET_SRC (exp))) > 4
	       && (GET_CODE (SET_SRC (exp)) == REG
		   || GET_CODE (SET_SRC (exp)) == MEM
		   || GET_CODE (SET_SRC (exp)) == CONST_DOUBLE))
	CC_STATUS_INIT; 
      else if (SET_DEST (exp) != pc_rtx)
	{
	  cc_status.flags = 0;
	  cc_status.value1 = SET_DEST (exp);
	  cc_status.value2 = SET_SRC (exp);
	}
    }
  else if (GET_CODE (exp) == PARALLEL
	   && GET_CODE (XVECEXP (exp, 0, 0)) == SET)
    {
      rtx dest = SET_DEST (XVECEXP (exp, 0, 0));
      rtx src  = SET_SRC  (XVECEXP (exp, 0, 0));

      if (ADDRESS_REG_P (dest))
	CC_STATUS_INIT;
      else if (dest != pc_rtx)
	{
	  cc_status.flags = 0;
	  cc_status.value1 = dest;
	  cc_status.value2 = src;
	}
    }
  else
    CC_STATUS_INIT;
  if (cc_status.value2 != 0
      && ADDRESS_REG_P (cc_status.value2)
      && GET_MODE (cc_status.value2) == QImode)
    CC_STATUS_INIT;
  if (cc_status.value2 != 0)
    switch (GET_CODE (cc_status.value2))
      {
      case ASHIFT: case ASHIFTRT: case LSHIFTRT:
      case ROTATE: case ROTATERT:
	/* These instructions always clear the overflow bit, and set
	   the carry to the bit shifted out.  */
	/* ??? We don't currently have a way to signal carry not valid,
	   nor do we check for it in the branch insns.  */
	CC_STATUS_INIT;
	break;

      case PLUS: case MINUS: case MULT:
      case DIV: case UDIV: case MOD: case UMOD: case NEG:
	if (GET_MODE (cc_status.value2) != VOIDmode)
	  cc_status.flags |= CC_NO_OVERFLOW;
	break;
      case ZERO_EXTEND:
	/* (SET r1 (ZERO_EXTEND r2)) on this machine
	   ends with a move insn moving r2 in r2's mode.
	   Thus, the cc's are set for r2.
	   This can set N bit spuriously.  */
	cc_status.flags |= CC_NOT_NEGATIVE; 

      default:
	break;
      }
  if (cc_status.value1 && GET_CODE (cc_status.value1) == REG
      && cc_status.value2
      && reg_overlap_mentioned_p (cc_status.value1, cc_status.value2))
    cc_status.value2 = 0;
  if (((cc_status.value1 && FP_REG_P (cc_status.value1))
       || (cc_status.value2 && FP_REG_P (cc_status.value2))))
    cc_status.flags = CC_IN_68881;
}

const char *
output_move_const_double (rtx *operands)
{
  int code = standard_68881_constant_p (operands[1]);

  if (code != 0)
    {
      static char buf[40];

      sprintf (buf, "fmovecr #0x%x,%%0", code & 0xff);
      return buf;
    }
  return "fmove%.d %1,%0";
}

const char *
output_move_const_single (rtx *operands)
{
  int code = standard_68881_constant_p (operands[1]);

  if (code != 0)
    {
      static char buf[40];

      sprintf (buf, "fmovecr #0x%x,%%0", code & 0xff);
      return buf;
    }
  return "fmove%.s %f1,%0";
}

/* Return nonzero if X, a CONST_DOUBLE, has a value that we can get
   from the "fmovecr" instruction.
   The value, anded with 0xff, gives the code to use in fmovecr
   to get the desired constant.  */

/* This code has been fixed for cross-compilation.  */
  
static int inited_68881_table = 0;

static const char *const strings_68881[7] = {
  "0.0",
  "1.0",
  "10.0",
  "100.0",
  "10000.0",
  "1e8",
  "1e16"
};

static const int codes_68881[7] = {
  0x0f,
  0x32,
  0x33,
  0x34,
  0x35,
  0x36,
  0x37
};

REAL_VALUE_TYPE values_68881[7];

/* Set up values_68881 array by converting the decimal values
   strings_68881 to binary.  */

void
init_68881_table (void)
{
  int i;
  REAL_VALUE_TYPE r;
  enum machine_mode mode;

  mode = SFmode;
  for (i = 0; i < 7; i++)
    {
      if (i == 6)
        mode = DFmode;
      r = REAL_VALUE_ATOF (strings_68881[i], mode);
      values_68881[i] = r;
    }
  inited_68881_table = 1;
}

int
standard_68881_constant_p (rtx x)
{
  REAL_VALUE_TYPE r;
  int i;

  /* fmovecr must be emulated on the 68040 and 68060, so it shouldn't be
     used at all on those chips.  */
  if (TARGET_68040 || TARGET_68060)
    return 0;

  if (! inited_68881_table)
    init_68881_table ();

  REAL_VALUE_FROM_CONST_DOUBLE (r, x);

  /* Use REAL_VALUES_IDENTICAL instead of REAL_VALUES_EQUAL so that -0.0
     is rejected.  */
  for (i = 0; i < 6; i++)
    {
      if (REAL_VALUES_IDENTICAL (r, values_68881[i]))
        return (codes_68881[i]);
    }
  
  if (GET_MODE (x) == SFmode)
    return 0;

  if (REAL_VALUES_EQUAL (r, values_68881[6]))
    return (codes_68881[6]);

  /* larger powers of ten in the constants ram are not used
     because they are not equal to a `double' C constant.  */
  return 0;
}

/* If X is a floating-point constant, return the logarithm of X base 2,
   or 0 if X is not a power of 2.  */

int
floating_exact_log2 (rtx x)
{
  REAL_VALUE_TYPE r, r1;
  int exp;

  REAL_VALUE_FROM_CONST_DOUBLE (r, x);

  if (REAL_VALUES_LESS (r, dconst1))
    return 0;

  exp = real_exponent (&r);
  real_2expN (&r1, exp);
  if (REAL_VALUES_EQUAL (r1, r))
    return exp;

  return 0;
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

   The m68k specific codes are:

   '.' for dot needed in Motorola-style opcode names.
   '-' for an operand pushing on the stack:
       sp@-, -(sp) or -(%sp) depending on the style of syntax.
   '+' for an operand pushing on the stack:
       sp@+, (sp)+ or (%sp)+ depending on the style of syntax.
   '@' for a reference to the top word on the stack:
       sp@, (sp) or (%sp) depending on the style of syntax.
   '#' for an immediate operand prefix (# in MIT and Motorola syntax
       but & in SGS syntax).
   '!' for the cc register (used in an `and to cc' insn).
   '$' for the letter `s' in an op code, but only on the 68040.
   '&' for the letter `d' in an op code, but only on the 68040.
   '/' for register prefix needed by longlong.h.

   'b' for byte insn (no effect, on the Sun; this is for the ISI).
   'd' to force memory addressing to be absolute, not relative.
   'f' for float insn (print a CONST_DOUBLE as a float rather than in hex)
   'o' for operands to go directly to output_operand_address (bypassing
       print_operand_address--used only for SYMBOL_REFs under TARGET_PCREL)
   'x' for float insn (print a CONST_DOUBLE as a float rather than in hex),
       or print pair of registers as rx:ry.

   */

void
print_operand (FILE *file, rtx op, int letter)
{
  if (letter == '.')
    {
      if (MOTOROLA)
	fprintf (file, ".");
    }
  else if (letter == '#')
    asm_fprintf (file, "%I");
  else if (letter == '-')
    asm_fprintf (file, MOTOROLA ? "-(%Rsp)" : "%Rsp@-");
  else if (letter == '+')
    asm_fprintf (file, MOTOROLA ? "(%Rsp)+" : "%Rsp@+");
  else if (letter == '@')
    asm_fprintf (file, MOTOROLA ? "(%Rsp)" : "%Rsp@");
  else if (letter == '!')
    asm_fprintf (file, "%Rfpcr");
  else if (letter == '$')
    {
      if (TARGET_68040_ONLY)
	fprintf (file, "s");
    }
  else if (letter == '&')
    {
      if (TARGET_68040_ONLY)
	fprintf (file, "d");
    }
  else if (letter == '/')
    asm_fprintf (file, "%R");
  else if (letter == 'o')
    {
      /* This is only for direct addresses with TARGET_PCREL */
      gcc_assert (GET_CODE (op) == MEM
		  && GET_CODE (XEXP (op, 0)) == SYMBOL_REF
		  && TARGET_PCREL);
      output_addr_const (file, XEXP (op, 0));
    }
  else if (GET_CODE (op) == REG)
    {
      if (letter == 'R')
	/* Print out the second register name of a register pair.
	   I.e., R (6) => 7.  */
	fputs (M68K_REGNAME(REGNO (op) + 1), file);
      else
	fputs (M68K_REGNAME(REGNO (op)), file);
    }
  else if (GET_CODE (op) == MEM)
    {
      output_address (XEXP (op, 0));
      if (letter == 'd' && ! TARGET_68020
	  && CONSTANT_ADDRESS_P (XEXP (op, 0))
	  && !(GET_CODE (XEXP (op, 0)) == CONST_INT
	       && INTVAL (XEXP (op, 0)) < 0x8000
	       && INTVAL (XEXP (op, 0)) >= -0x8000))
	fprintf (file, MOTOROLA ? ".l" : ":l");
    }
  else if (GET_CODE (op) == CONST_DOUBLE && GET_MODE (op) == SFmode)
    {
      REAL_VALUE_TYPE r;
      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      ASM_OUTPUT_FLOAT_OPERAND (letter, file, r);
    }
  else if (GET_CODE (op) == CONST_DOUBLE && GET_MODE (op) == XFmode)
    {
      REAL_VALUE_TYPE r;
      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      ASM_OUTPUT_LONG_DOUBLE_OPERAND (file, r);
    }
  else if (GET_CODE (op) == CONST_DOUBLE && GET_MODE (op) == DFmode)
    {
      REAL_VALUE_TYPE r;
      REAL_VALUE_FROM_CONST_DOUBLE (r, op);
      ASM_OUTPUT_DOUBLE_OPERAND (file, r);
    }
  else
    {
      /* Use `print_operand_address' instead of `output_addr_const'
	 to ensure that we print relevant PIC stuff.  */
      asm_fprintf (file, "%I");
      if (TARGET_PCREL
	  && (GET_CODE (op) == SYMBOL_REF || GET_CODE (op) == CONST))
	print_operand_address (file, op);
      else
	output_addr_const (file, op);
    }
}


/* A C compound statement to output to stdio stream STREAM the
   assembler syntax for an instruction operand that is a memory
   reference whose address is ADDR.  ADDR is an RTL expression.

   Note that this contains a kludge that knows that the only reason
   we have an address (plus (label_ref...) (reg...)) when not generating
   PIC code is in the insn before a tablejump, and we know that m68k.md
   generates a label LInnn: on such an insn.

   It is possible for PIC to generate a (plus (label_ref...) (reg...))
   and we handle that just like we would a (plus (symbol_ref...) (reg...)).

   Some SGS assemblers have a bug such that "Lnnn-LInnn-2.b(pc,d0.l*2)"
   fails to assemble.  Luckily "Lnnn(pc,d0.l*2)" produces the results
   we want.  This difference can be accommodated by using an assembler
   define such "LDnnn" to be either "Lnnn-LInnn-2.b", "Lnnn", or any other
   string, as necessary.  This is accomplished via the ASM_OUTPUT_CASE_END
   macro.  See m68k/sgs.h for an example; for versions without the bug.
   Some assemblers refuse all the above solutions.  The workaround is to
   emit "K(pc,d0.l*2)" with K being a small constant known to give the
   right behavior.

   They also do not like things like "pea 1.w", so we simple leave off
   the .w on small constants. 

   This routine is responsible for distinguishing between -fpic and -fPIC 
   style relocations in an address.  When generating -fpic code the
   offset is output in word mode (e.g. movel a5@(_foo:w), a0).  When generating
   -fPIC code the offset is output in long mode (e.g. movel a5@(_foo:l), a0) */

#if MOTOROLA
#  define ASM_OUTPUT_CASE_FETCH(file, labelno, regname) \
  asm_fprintf (file, "%LL%d-%LLI%d.b(%Rpc,%s.", labelno, labelno, regname)
#else /* !MOTOROLA */
# define ASM_OUTPUT_CASE_FETCH(file, labelno, regname) \
  asm_fprintf (file, "%Rpc@(%LL%d-%LLI%d-2:b,%s:", labelno, labelno, regname)
#endif /* !MOTOROLA */

void
print_operand_address (FILE *file, rtx addr)
{
  register rtx reg1, reg2, breg, ireg;
  rtx offset;

  switch (GET_CODE (addr))
    {
    case REG:
      fprintf (file, MOTOROLA ? "(%s)" : "%s@", M68K_REGNAME (REGNO (addr)));
      break;
    case PRE_DEC:
      fprintf (file, MOTOROLA ? "-(%s)" : "%s@-",
	       M68K_REGNAME (REGNO (XEXP (addr, 0))));
      break;
    case POST_INC:
      fprintf (file, MOTOROLA ? "(%s)+" : "%s@+",
	       M68K_REGNAME (REGNO (XEXP (addr, 0))));
      break;
    case PLUS:
      reg1 = reg2 = ireg = breg = offset = 0;
      if (CONSTANT_ADDRESS_P (XEXP (addr, 0)))
	{
	  offset = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (CONSTANT_ADDRESS_P (XEXP (addr, 1)))
	{
	  offset = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      if (GET_CODE (addr) != PLUS)
	{
	  ;
	}
      else if (GET_CODE (XEXP (addr, 0)) == SIGN_EXTEND)
	{
	  reg1 = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (GET_CODE (XEXP (addr, 1)) == SIGN_EXTEND)
	{
	  reg1 = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 0)) == MULT)
	{
	  reg1 = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (GET_CODE (XEXP (addr, 1)) == MULT)
	{
	  reg1 = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 0)) == REG)
	{
	  reg1 = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (GET_CODE (XEXP (addr, 1)) == REG)
	{
	  reg1 = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      if (GET_CODE (addr) == REG || GET_CODE (addr) == MULT
	  || GET_CODE (addr) == SIGN_EXTEND)
	{
	  if (reg1 == 0)
	    reg1 = addr;
	  else
	    reg2 = addr;
	  addr = 0;
	}
#if 0	/* for OLD_INDEXING */
      else if (GET_CODE (addr) == PLUS)
	{
	  if (GET_CODE (XEXP (addr, 0)) == REG)
	    {
	      reg2 = XEXP (addr, 0);
	      addr = XEXP (addr, 1);
	    }
	  else if (GET_CODE (XEXP (addr, 1)) == REG)
	    {
	      reg2 = XEXP (addr, 1);
	      addr = XEXP (addr, 0);
	    }
	}
#endif
      if (offset != 0)
	{
	  gcc_assert (!addr);
	  addr = offset;
	}
      if ((reg1 && (GET_CODE (reg1) == SIGN_EXTEND
		    || GET_CODE (reg1) == MULT))
	  || (reg2 != 0 && REGNO_OK_FOR_BASE_P (REGNO (reg2))))
	{
	  breg = reg2;
	  ireg = reg1;
	}
      else if (reg1 != 0 && REGNO_OK_FOR_BASE_P (REGNO (reg1)))
	{
	  breg = reg1;
	  ireg = reg2;
	}
      if (ireg != 0 && breg == 0 && GET_CODE (addr) == LABEL_REF
	  && ! (flag_pic && ireg == pic_offset_table_rtx))
	{
	  int scale = 1;
	  if (GET_CODE (ireg) == MULT)
	    {
	      scale = INTVAL (XEXP (ireg, 1));
	      ireg = XEXP (ireg, 0);
	    }
	  if (GET_CODE (ireg) == SIGN_EXTEND)
	    {
	      ASM_OUTPUT_CASE_FETCH (file,
				     CODE_LABEL_NUMBER (XEXP (addr, 0)),
				     M68K_REGNAME (REGNO (XEXP (ireg, 0))));
	      fprintf (file, "w");
	    }
	  else
	    {
	      ASM_OUTPUT_CASE_FETCH (file,
				     CODE_LABEL_NUMBER (XEXP (addr, 0)),
				     M68K_REGNAME (REGNO (ireg)));
	      fprintf (file, "l");
	    }
	  if (scale != 1)
	    fprintf (file, MOTOROLA ? "*%d" : ":%d", scale);
	  putc (')', file);
	  break;
	}
      if (breg != 0 && ireg == 0 && GET_CODE (addr) == LABEL_REF
	  && ! (flag_pic && breg == pic_offset_table_rtx))
	{
	  ASM_OUTPUT_CASE_FETCH (file,
				 CODE_LABEL_NUMBER (XEXP (addr, 0)),
				 M68K_REGNAME (REGNO (breg)));
	  fprintf (file, "l)");
	  break;
	}
      if (ireg != 0 || breg != 0)
	{
	  int scale = 1;
	    
	  gcc_assert (breg);
	  gcc_assert (flag_pic || !addr || GET_CODE (addr) != LABEL_REF);
	    
	  if (MOTOROLA)
	    {
	      if (addr != 0)
		{
		  output_addr_const (file, addr);
		  if (flag_pic && (breg == pic_offset_table_rtx))
		    {
		      fprintf (file, "@GOT");
		      if (flag_pic == 1)
			fprintf (file, ".w");
		    }
		}
	      fprintf (file, "(%s", M68K_REGNAME (REGNO (breg)));
	      if (ireg != 0)
		putc (',', file);
	    }
	  else /* !MOTOROLA */
	    {
	      fprintf (file, "%s@(", M68K_REGNAME (REGNO (breg)));
	      if (addr != 0)
		{
		  output_addr_const (file, addr);
		  if (breg == pic_offset_table_rtx)
		    switch (flag_pic)
		      {
		      case 1:
			fprintf (file, ":w");
			break;
		      case 2:
			fprintf (file, ":l");
			break;
		      default:
			break;
		      }
		  if (ireg != 0)
		    putc (',', file);
		}
	    } /* !MOTOROLA */
	  if (ireg != 0 && GET_CODE (ireg) == MULT)
	    {
	      scale = INTVAL (XEXP (ireg, 1));
	      ireg = XEXP (ireg, 0);
	    }
	  if (ireg != 0 && GET_CODE (ireg) == SIGN_EXTEND)
	    fprintf (file, MOTOROLA ? "%s.w" : "%s:w",
		     M68K_REGNAME (REGNO (XEXP (ireg, 0))));
	  else if (ireg != 0)
	    fprintf (file, MOTOROLA ? "%s.l" : "%s:l",
		     M68K_REGNAME (REGNO (ireg)));
	  if (scale != 1)
	    fprintf (file, MOTOROLA ? "*%d" : ":%d", scale);
	  putc (')', file);
	  break;
	}
      else if (reg1 != 0 && GET_CODE (addr) == LABEL_REF
	       && ! (flag_pic && reg1 == pic_offset_table_rtx))
	{
	  ASM_OUTPUT_CASE_FETCH (file,
				 CODE_LABEL_NUMBER (XEXP (addr, 0)),
				 M68K_REGNAME (REGNO (reg1)));
	  fprintf (file, "l)");
	  break;
	}
      /* FALL-THROUGH (is this really what we want?)  */
    default:
      if (GET_CODE (addr) == CONST_INT
	  && INTVAL (addr) < 0x8000
	  && INTVAL (addr) >= -0x8000)
	{
	  fprintf (file, MOTOROLA ? "%d.w" : "%d:w", (int) INTVAL (addr));
	}
      else if (GET_CODE (addr) == CONST_INT)
	{
	  fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (addr));
	}
      else if (TARGET_PCREL)
	{
	  fputc ('(', file);
	  output_addr_const (file, addr);
	  if (flag_pic == 1)
	    asm_fprintf (file, ":w,%Rpc)");
	  else
	    asm_fprintf (file, ":l,%Rpc)");
	}
      else
	{
	  /* Special case for SYMBOL_REF if the symbol name ends in
	     `.<letter>', this can be mistaken as a size suffix.  Put
	     the name in parentheses.  */
	  if (GET_CODE (addr) == SYMBOL_REF
	      && strlen (XSTR (addr, 0)) > 2
	      && XSTR (addr, 0)[strlen (XSTR (addr, 0)) - 2] == '.')
	    {
	      putc ('(', file);
	      output_addr_const (file, addr);
	      putc (')', file);
	    }
	  else
	    output_addr_const (file, addr);
	}
      break;
    }
}

/* Check for cases where a clr insns can be omitted from code using
   strict_low_part sets.  For example, the second clrl here is not needed:
   clrl d0; movw a0@+,d0; use d0; clrl d0; movw a0@+; use d0; ...

   MODE is the mode of this STRICT_LOW_PART set.  FIRST_INSN is the clear
   insn we are checking for redundancy.  TARGET is the register set by the
   clear insn.  */

bool
strict_low_part_peephole_ok (enum machine_mode mode, rtx first_insn,
                             rtx target)
{
  rtx p;

  p = prev_nonnote_insn (first_insn);

  while (p)
    {
      /* If it isn't an insn, then give up.  */
      if (GET_CODE (p) != INSN)
	return false;

      if (reg_set_p (target, p))
	{
	  rtx set = single_set (p);
	  rtx dest;

	  /* If it isn't an easy to recognize insn, then give up.  */
	  if (! set)
	    return false;

	  dest = SET_DEST (set);

	  /* If this sets the entire target register to zero, then our
	     first_insn is redundant.  */
	  if (rtx_equal_p (dest, target)
	      && SET_SRC (set) == const0_rtx)
	    return true;
	  else if (GET_CODE (dest) == STRICT_LOW_PART
		   && GET_CODE (XEXP (dest, 0)) == REG
		   && REGNO (XEXP (dest, 0)) == REGNO (target)
		   && (GET_MODE_SIZE (GET_MODE (XEXP (dest, 0)))
		       <= GET_MODE_SIZE (mode)))
	    /* This is a strict low part set which modifies less than
	       we are using, so it is safe.  */
	    ;
	  else
	    return false;
	}

      p = prev_nonnote_insn (p);
    }

  return false;
}

/* Operand predicates for implementing asymmetric pc-relative addressing
   on m68k.  The m68k supports pc-relative addressing (mode 7, register 2)
   when used as a source operand, but not as a destination operand.

   We model this by restricting the meaning of the basic predicates
   (general_operand, memory_operand, etc) to forbid the use of this
   addressing mode, and then define the following predicates that permit
   this addressing mode.  These predicates can then be used for the
   source operands of the appropriate instructions.

   n.b.  While it is theoretically possible to change all machine patterns
   to use this addressing more where permitted by the architecture,
   it has only been implemented for "common" cases: SImode, HImode, and
   QImode operands, and only for the principle operations that would
   require this addressing mode: data movement and simple integer operations.

   In parallel with these new predicates, two new constraint letters
   were defined: 'S' and 'T'.  'S' is the -mpcrel analog of 'm'.
   'T' replaces 's' in the non-pcrel case.  It is a no-op in the pcrel case.
   In the pcrel case 's' is only valid in combination with 'a' registers.
   See addsi3, subsi3, cmpsi, and movsi patterns for a better understanding
   of how these constraints are used.

   The use of these predicates is strictly optional, though patterns that
   don't will cause an extra reload register to be allocated where one
   was not necessary:

	lea (abc:w,%pc),%a0	; need to reload address
	moveq &1,%d1		; since write to pc-relative space
	movel %d1,%a0@		; is not allowed
	...
	lea (abc:w,%pc),%a1	; no need to reload address here
	movel %a1@,%d0		; since "movel (abc:w,%pc),%d0" is ok

   For more info, consult tiemann@cygnus.com.


   All of the ugliness with predicates and constraints is due to the
   simple fact that the m68k does not allow a pc-relative addressing
   mode as a destination.  gcc does not distinguish between source and
   destination addresses.  Hence, if we claim that pc-relative address
   modes are valid, e.g. GO_IF_LEGITIMATE_ADDRESS accepts them, then we
   end up with invalid code.  To get around this problem, we left
   pc-relative modes as invalid addresses, and then added special
   predicates and constraints to accept them.

   A cleaner way to handle this is to modify gcc to distinguish
   between source and destination addresses.  We can then say that
   pc-relative is a valid source address but not a valid destination
   address, and hopefully avoid a lot of the predicate and constraint
   hackery.  Unfortunately, this would be a pretty big change.  It would
   be a useful change for a number of ports, but there aren't any current
   plans to undertake this.

   ***************************************************************************/


const char *
output_andsi3 (rtx *operands)
{
  int logval;
  if (GET_CODE (operands[2]) == CONST_INT
      && (INTVAL (operands[2]) | 0xffff) == -1
      && (DATA_REG_P (operands[0])
	  || offsettable_memref_p (operands[0]))
      && !TARGET_COLDFIRE)
    {
      if (GET_CODE (operands[0]) != REG)
        operands[0] = adjust_address (operands[0], HImode, 2);
      operands[2] = GEN_INT (INTVAL (operands[2]) & 0xffff);
      /* Do not delete a following tstl %0 insn; that would be incorrect.  */
      CC_STATUS_INIT;
      if (operands[2] == const0_rtx)
        return "clr%.w %0";
      return "and%.w %2,%0";
    }
  if (GET_CODE (operands[2]) == CONST_INT
      && (logval = exact_log2 (~ INTVAL (operands[2]))) >= 0
      && (DATA_REG_P (operands[0])
          || offsettable_memref_p (operands[0])))
    {
      if (DATA_REG_P (operands[0]))
	operands[1] = GEN_INT (logval);
      else
        {
	  operands[0] = adjust_address (operands[0], SImode, 3 - (logval / 8));
	  operands[1] = GEN_INT (logval % 8);
        }
      /* This does not set condition codes in a standard way.  */
      CC_STATUS_INIT;
      return "bclr %1,%0";
    }
  return "and%.l %2,%0";
}

const char *
output_iorsi3 (rtx *operands)
{
  register int logval;
  if (GET_CODE (operands[2]) == CONST_INT
      && INTVAL (operands[2]) >> 16 == 0
      && (DATA_REG_P (operands[0])
	  || offsettable_memref_p (operands[0]))
      && !TARGET_COLDFIRE)
    {
      if (GET_CODE (operands[0]) != REG)
        operands[0] = adjust_address (operands[0], HImode, 2);
      /* Do not delete a following tstl %0 insn; that would be incorrect.  */
      CC_STATUS_INIT;
      if (INTVAL (operands[2]) == 0xffff)
	return "mov%.w %2,%0";
      return "or%.w %2,%0";
    }
  if (GET_CODE (operands[2]) == CONST_INT
      && (logval = exact_log2 (INTVAL (operands[2]))) >= 0
      && (DATA_REG_P (operands[0])
	  || offsettable_memref_p (operands[0])))
    {
      if (DATA_REG_P (operands[0]))
	operands[1] = GEN_INT (logval);
      else
        {
	  operands[0] = adjust_address (operands[0], SImode, 3 - (logval / 8));
	  operands[1] = GEN_INT (logval % 8);
	}
      CC_STATUS_INIT;
      return "bset %1,%0";
    }
  return "or%.l %2,%0";
}

const char *
output_xorsi3 (rtx *operands)
{
  register int logval;
  if (GET_CODE (operands[2]) == CONST_INT
      && INTVAL (operands[2]) >> 16 == 0
      && (offsettable_memref_p (operands[0]) || DATA_REG_P (operands[0]))
      && !TARGET_COLDFIRE)
    {
      if (! DATA_REG_P (operands[0]))
	operands[0] = adjust_address (operands[0], HImode, 2);
      /* Do not delete a following tstl %0 insn; that would be incorrect.  */
      CC_STATUS_INIT;
      if (INTVAL (operands[2]) == 0xffff)
	return "not%.w %0";
      return "eor%.w %2,%0";
    }
  if (GET_CODE (operands[2]) == CONST_INT
      && (logval = exact_log2 (INTVAL (operands[2]))) >= 0
      && (DATA_REG_P (operands[0])
	  || offsettable_memref_p (operands[0])))
    {
      if (DATA_REG_P (operands[0]))
	operands[1] = GEN_INT (logval);
      else
        {
	  operands[0] = adjust_address (operands[0], SImode, 3 - (logval / 8));
	  operands[1] = GEN_INT (logval % 8);
	}
      CC_STATUS_INIT;
      return "bchg %1,%0";
    }
  return "eor%.l %2,%0";
}

#ifdef M68K_TARGET_COFF

/* Output assembly to switch to section NAME with attribute FLAGS.  */

static void
m68k_coff_asm_named_section (const char *name, unsigned int flags, 
			     tree decl ATTRIBUTE_UNUSED)
{
  char flagchar;

  if (flags & SECTION_WRITE)
    flagchar = 'd';
  else
    flagchar = 'x';

  fprintf (asm_out_file, "\t.section\t%s,\"%c\"\n", name, flagchar);
}

#endif /* M68K_TARGET_COFF */

static void
m68k_output_mi_thunk (FILE *file, tree thunk ATTRIBUTE_UNUSED,
		      HOST_WIDE_INT delta,
		      HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
		      tree function)
{
  rtx xops[1];
  const char *fmt;

  if (delta > 0 && delta <= 8)
    asm_fprintf (file, (MOTOROLA
			? "\taddq.l %I%d,4(%Rsp)\n"
			: "\taddql %I%d,%Rsp@(4)\n"),
		 (int) delta);
  else if (delta < 0 && delta >= -8)
    asm_fprintf (file, (MOTOROLA
			? "\tsubq.l %I%d,4(%Rsp)\n"
			: "\tsubql %I%d,%Rsp@(4)\n"),
		 (int) -delta);
  else if (TARGET_COLDFIRE)
    {
      /* ColdFire can't add/sub a constant to memory unless it is in
	 the range of addq/subq.  So load the value into %d0 and
	 then add it to 4(%sp). */
      if (delta >= -128 && delta <= 127)
	asm_fprintf (file, (MOTOROLA
			    ? "\tmoveq.l %I%wd,%Rd0\n"
			    : "\tmoveql %I%wd,%Rd0\n"),
		     delta);
      else
	asm_fprintf (file, (MOTOROLA
			    ? "\tmove.l %I%wd,%Rd0\n"
			    : "\tmovel %I%wd,%Rd0\n"),
		     delta);
      asm_fprintf (file, (MOTOROLA
			  ? "\tadd.l %Rd0,4(%Rsp)\n"
			  : "\taddl %Rd0,%Rsp@(4)\n"));
    }
  else
    asm_fprintf (file, (MOTOROLA
			? "\tadd.l %I%wd,4(%Rsp)\n"
			: "\taddl %I%wd,%Rsp@(4)\n"),
		 delta);

  xops[0] = DECL_RTL (function);

  /* Logic taken from call patterns in m68k.md.  */
  if (flag_pic)
    {
      if (TARGET_PCREL)
	fmt = "bra.l %o0";
      else if (flag_pic == 1 || TARGET_68020)
	{
	  if (MOTOROLA)
	    {
#if defined (USE_GAS)
	      fmt = "bra.l %0@PLTPC";
#else
	      fmt = "bra %0@PLTPC";
#endif
	    }
	  else /* !MOTOROLA */
	    {
#ifdef USE_GAS
	      fmt = "bra.l %0";
#else
	      fmt = "jra %0,a1";
#endif
	    }
	}
      else if (optimize_size || TARGET_ID_SHARED_LIBRARY)
        fmt = "move.l %0@GOT(%%a5), %%a1\n\tjmp (%%a1)";
      else
        fmt = "lea %0-.-8,%%a1\n\tjmp 0(%%pc,%%a1)";
    }
  else
    {
#if MOTOROLA && !defined (USE_GAS)
      fmt = "jmp %0";
#else
      fmt = "jra %0";
#endif
    }

  output_asm_insn (fmt, xops);
}

/* Worker function for TARGET_STRUCT_VALUE_RTX.  */

static rtx
m68k_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		       int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, M68K_STRUCT_VALUE_REGNUM);
}

/* Return nonzero if register old_reg can be renamed to register new_reg.  */
int
m68k_hard_regno_rename_ok (unsigned int old_reg ATTRIBUTE_UNUSED,
			   unsigned int new_reg)
{

  /* Interrupt functions can only use registers that have already been
     saved by the prologue, even if they would normally be
     call-clobbered.  */

  if (m68k_interrupt_function_p (current_function_decl)
      && !regs_ever_live[new_reg])
    return 0;

  return 1;
}

/* Value is true if hard register REGNO can hold a value of machine-mode MODE.
   On the 68000, the cpu registers can hold any mode except bytes in address
   registers, but the 68881 registers can hold only SFmode or DFmode.  */
bool
m68k_regno_mode_ok (int regno, enum machine_mode mode)
{
  if (regno < 8)
    {
      /* Data Registers, can hold aggregate if fits in.  */
      if (regno + GET_MODE_SIZE (mode) / 4 <= 8)
	return true;
    }
  else if (regno < 16)
    {
      /* Address Registers, can't hold bytes, can hold aggregate if
	 fits in.  */
      if (GET_MODE_SIZE (mode) == 1)
	return false;
      if (regno + GET_MODE_SIZE (mode) / 4 <= 16)
	return true;
    }
  else if (regno < 24)
    {
      /* FPU registers, hold float or complex float of long double or
	 smaller.  */
      if ((GET_MODE_CLASS (mode) == MODE_FLOAT
	   || GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT)
	  && GET_MODE_UNIT_SIZE (mode) <= TARGET_FP_REG_SIZE)
	return true;
    }
  return false;
}

/* Return floating point values in a 68881 register.  This makes 68881 code
   a little bit faster.  It also makes -msoft-float code incompatible with
   hard-float code, so people have to be careful not to mix the two.
   For ColdFire it was decided the ABI incompatibility is undesirable.
   If there is need for a hard-float ABI it is probably worth doing it
   properly and also passing function arguments in FP registers.  */
rtx
m68k_libcall_value (enum machine_mode mode)
{
  switch (mode) {
  case SFmode:
  case DFmode:
  case XFmode:
    if (TARGET_68881)
      return gen_rtx_REG (mode, 16);
    break;
  default:
    break;
  }
  return gen_rtx_REG (mode, 0);
}

rtx
m68k_function_value (tree valtype, tree func ATTRIBUTE_UNUSED)
{
  enum machine_mode mode;

  mode = TYPE_MODE (valtype);
  switch (mode) {
  case SFmode:
  case DFmode:
  case XFmode:
    if (TARGET_68881)
      return gen_rtx_REG (mode, 16);
    break;
  default:
    break;
  }

  /* If the function returns a pointer, push that into %a0 */
  if (POINTER_TYPE_P (valtype))
    return gen_rtx_REG (mode, 8);
  else
    return gen_rtx_REG (mode, 0);
}
