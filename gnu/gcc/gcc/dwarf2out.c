/* Output Dwarf2 format symbol table information from GCC.
   Copyright (C) 1992, 1993, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by Gary Funck (gary@intrepid.com).
   Derived from DWARF 1 implementation of Ron Guilmette (rfg@monkeys.com).
   Extensively modified by Jason Merrill (jason@cygnus.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* TODO: Emit .debug_line header even when there are no functions, since
	   the file numbers are used by .debug_info.  Alternately, leave
	   out locations for types and decls.
	 Avoid talking about ctors and op= for PODs.
	 Factor out common prologue sequences into multiple CIEs.  */

/* The first part of this file deals with the DWARF 2 frame unwind
   information, which is also used by the GCC efficient exception handling
   mechanism.  The second part, controlled only by an #ifdef
   DWARF2_DEBUGGING_INFO, deals with the other DWARF 2 debugging
   information.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "version.h"
#include "flags.h"
#include "real.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "insn-config.h"
#include "reload.h"
#include "function.h"
#include "output.h"
#include "expr.h"
#include "libfuncs.h"
#include "except.h"
#include "dwarf2.h"
#include "dwarf2out.h"
#include "dwarf2asm.h"
#include "toplev.h"
#include "varray.h"
#include "ggc.h"
#include "md5.h"
#include "tm_p.h"
#include "diagnostic.h"
#include "debug.h"
#include "target.h"
#include "langhooks.h"
#include "hashtab.h"
#include "cgraph.h"
#include "input.h"

#ifdef DWARF2_DEBUGGING_INFO
static void dwarf2out_source_line (unsigned int, const char *);
#endif

/* DWARF2 Abbreviation Glossary:
   CFA = Canonical Frame Address
	   a fixed address on the stack which identifies a call frame.
	   We define it to be the value of SP just before the call insn.
	   The CFA register and offset, which may change during the course
	   of the function, are used to calculate its value at runtime.
   CFI = Call Frame Instruction
	   an instruction for the DWARF2 abstract machine
   CIE = Common Information Entry
	   information describing information common to one or more FDEs
   DIE = Debugging Information Entry
   FDE = Frame Description Entry
	   information describing the stack call frame, in particular,
	   how to restore registers

   DW_CFA_... = DWARF2 CFA call frame instruction
   DW_TAG_... = DWARF2 DIE tag */

#ifndef DWARF2_FRAME_INFO
# ifdef DWARF2_DEBUGGING_INFO
#  define DWARF2_FRAME_INFO \
  (write_symbols == DWARF2_DEBUG || write_symbols == VMS_AND_DWARF2_DEBUG)
# else
#  define DWARF2_FRAME_INFO 0
# endif
#endif

/* Map register numbers held in the call frame info that gcc has
   collected using DWARF_FRAME_REGNUM to those that should be output in
   .debug_frame and .eh_frame.  */
#ifndef DWARF2_FRAME_REG_OUT
#define DWARF2_FRAME_REG_OUT(REGNO, FOR_EH) (REGNO)
#endif

/* Decide whether we want to emit frame unwind information for the current
   translation unit.  */

int
dwarf2out_do_frame (void)
{
  /* We want to emit correct CFA location expressions or lists, so we
     have to return true if we're going to output debug info, even if
     we're not going to output frame or unwind info.  */
  return (write_symbols == DWARF2_DEBUG
	  || write_symbols == VMS_AND_DWARF2_DEBUG
	  || DWARF2_FRAME_INFO
#ifdef DWARF2_UNWIND_INFO
	  || (DWARF2_UNWIND_INFO
	      && (flag_unwind_tables
		  || (flag_exceptions && ! USING_SJLJ_EXCEPTIONS)))
#endif
	  );
}

/* The size of the target's pointer type.  */
#ifndef PTR_SIZE
#define PTR_SIZE (POINTER_SIZE / BITS_PER_UNIT)
#endif

/* Array of RTXes referenced by the debugging information, which therefore
   must be kept around forever.  */
static GTY(()) VEC(rtx,gc) *used_rtx_array;

/* A pointer to the base of a list of incomplete types which might be
   completed at some later time.  incomplete_types_list needs to be a
   VEC(tree,gc) because we want to tell the garbage collector about
   it.  */
static GTY(()) VEC(tree,gc) *incomplete_types;

/* A pointer to the base of a table of references to declaration
   scopes.  This table is a display which tracks the nesting
   of declaration scopes at the current scope and containing
   scopes.  This table is used to find the proper place to
   define type declaration DIE's.  */
static GTY(()) VEC(tree,gc) *decl_scope_table;

/* Pointers to various DWARF2 sections.  */
static GTY(()) section *debug_info_section;
static GTY(()) section *debug_abbrev_section;
static GTY(()) section *debug_aranges_section;
static GTY(()) section *debug_macinfo_section;
static GTY(()) section *debug_line_section;
static GTY(()) section *debug_loc_section;
static GTY(()) section *debug_pubnames_section;
static GTY(()) section *debug_str_section;
static GTY(()) section *debug_ranges_section;
static GTY(()) section *debug_frame_section;

/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif

typedef struct dw_cfi_struct *dw_cfi_ref;
typedef struct dw_fde_struct *dw_fde_ref;
typedef union  dw_cfi_oprnd_struct *dw_cfi_oprnd_ref;

/* Call frames are described using a sequence of Call Frame
   Information instructions.  The register number, offset
   and address fields are provided as possible operands;
   their use is selected by the opcode field.  */

enum dw_cfi_oprnd_type {
  dw_cfi_oprnd_unused,
  dw_cfi_oprnd_reg_num,
  dw_cfi_oprnd_offset,
  dw_cfi_oprnd_addr,
  dw_cfi_oprnd_loc
};

typedef union dw_cfi_oprnd_struct GTY(())
{
  unsigned int GTY ((tag ("dw_cfi_oprnd_reg_num"))) dw_cfi_reg_num;
  HOST_WIDE_INT GTY ((tag ("dw_cfi_oprnd_offset"))) dw_cfi_offset;
  const char * GTY ((tag ("dw_cfi_oprnd_addr"))) dw_cfi_addr;
  struct dw_loc_descr_struct * GTY ((tag ("dw_cfi_oprnd_loc"))) dw_cfi_loc;
}
dw_cfi_oprnd;

typedef struct dw_cfi_struct GTY(())
{
  dw_cfi_ref dw_cfi_next;
  enum dwarf_call_frame_info dw_cfi_opc;
  dw_cfi_oprnd GTY ((desc ("dw_cfi_oprnd1_desc (%1.dw_cfi_opc)")))
    dw_cfi_oprnd1;
  dw_cfi_oprnd GTY ((desc ("dw_cfi_oprnd2_desc (%1.dw_cfi_opc)")))
    dw_cfi_oprnd2;
}
dw_cfi_node;

/* This is how we define the location of the CFA. We use to handle it
   as REG + OFFSET all the time,  but now it can be more complex.
   It can now be either REG + CFA_OFFSET or *(REG + BASE_OFFSET) + CFA_OFFSET.
   Instead of passing around REG and OFFSET, we pass a copy
   of this structure.  */
typedef struct cfa_loc GTY(())
{
  HOST_WIDE_INT offset;
  HOST_WIDE_INT base_offset;
  unsigned int reg;
  int indirect;            /* 1 if CFA is accessed via a dereference.  */
} dw_cfa_location;

/* All call frame descriptions (FDE's) in the GCC generated DWARF
   refer to a single Common Information Entry (CIE), defined at
   the beginning of the .debug_frame section.  This use of a single
   CIE obviates the need to keep track of multiple CIE's
   in the DWARF generation routines below.  */

typedef struct dw_fde_struct GTY(())
{
  tree decl;
  const char *dw_fde_begin;
  const char *dw_fde_current_label;
  const char *dw_fde_end;
  const char *dw_fde_hot_section_label;
  const char *dw_fde_hot_section_end_label;
  const char *dw_fde_unlikely_section_label;
  const char *dw_fde_unlikely_section_end_label;
  bool dw_fde_switched_sections;
  dw_cfi_ref dw_fde_cfi;
  unsigned funcdef_number;
  unsigned all_throwers_are_sibcalls : 1;
  unsigned nothrow : 1;
  unsigned uses_eh_lsda : 1;
}
dw_fde_node;

/* Maximum size (in bytes) of an artificially generated label.  */
#define MAX_ARTIFICIAL_LABEL_BYTES	30

/* The size of addresses as they appear in the Dwarf 2 data.
   Some architectures use word addresses to refer to code locations,
   but Dwarf 2 info always uses byte addresses.  On such machines,
   Dwarf 2 addresses need to be larger than the architecture's
   pointers.  */
#ifndef DWARF2_ADDR_SIZE
#define DWARF2_ADDR_SIZE (POINTER_SIZE / BITS_PER_UNIT)
#endif

/* The size in bytes of a DWARF field indicating an offset or length
   relative to a debug info section, specified to be 4 bytes in the
   DWARF-2 specification.  The SGI/MIPS ABI defines it to be the same
   as PTR_SIZE.  */

#ifndef DWARF_OFFSET_SIZE
#define DWARF_OFFSET_SIZE 4
#endif

/* According to the (draft) DWARF 3 specification, the initial length
   should either be 4 or 12 bytes.  When it's 12 bytes, the first 4
   bytes are 0xffffffff, followed by the length stored in the next 8
   bytes.

   However, the SGI/MIPS ABI uses an initial length which is equal to
   DWARF_OFFSET_SIZE.  It is defined (elsewhere) accordingly.  */

#ifndef DWARF_INITIAL_LENGTH_SIZE
#define DWARF_INITIAL_LENGTH_SIZE (DWARF_OFFSET_SIZE == 4 ? 4 : 12)
#endif

#define DWARF_VERSION 2

/* Round SIZE up to the nearest BOUNDARY.  */
#define DWARF_ROUND(SIZE,BOUNDARY) \
  ((((SIZE) + (BOUNDARY) - 1) / (BOUNDARY)) * (BOUNDARY))

/* Offsets recorded in opcodes are a multiple of this alignment factor.  */
#ifndef DWARF_CIE_DATA_ALIGNMENT
#ifdef STACK_GROWS_DOWNWARD
#define DWARF_CIE_DATA_ALIGNMENT (-((int) UNITS_PER_WORD))
#else
#define DWARF_CIE_DATA_ALIGNMENT ((int) UNITS_PER_WORD)
#endif
#endif

/* CIE identifier.  */
#if HOST_BITS_PER_WIDE_INT >= 64
#define DWARF_CIE_ID \
  (unsigned HOST_WIDE_INT) (DWARF_OFFSET_SIZE == 4 ? DW_CIE_ID : DW64_CIE_ID)
#else
#define DWARF_CIE_ID DW_CIE_ID
#endif

/* A pointer to the base of a table that contains frame description
   information for each routine.  */
static GTY((length ("fde_table_allocated"))) dw_fde_ref fde_table;

/* Number of elements currently allocated for fde_table.  */
static GTY(()) unsigned fde_table_allocated;

/* Number of elements in fde_table currently in use.  */
static GTY(()) unsigned fde_table_in_use;

/* Size (in elements) of increments by which we may expand the
   fde_table.  */
#define FDE_TABLE_INCREMENT 256

/* A list of call frame insns for the CIE.  */
static GTY(()) dw_cfi_ref cie_cfi_head;

#if defined (DWARF2_DEBUGGING_INFO) || defined (DWARF2_UNWIND_INFO)
/* Some DWARF extensions (e.g., MIPS/SGI) implement a subprogram
   attribute that accelerates the lookup of the FDE associated
   with the subprogram.  This variable holds the table index of the FDE
   associated with the current function (body) definition.  */
static unsigned current_funcdef_fde;
#endif

struct indirect_string_node GTY(())
{
  const char *str;
  unsigned int refcount;
  unsigned int form;
  char *label;
};

static GTY ((param_is (struct indirect_string_node))) htab_t debug_str_hash;

static GTY(()) int dw2_string_counter;
static GTY(()) unsigned long dwarf2out_cfi_label_num;

#if defined (DWARF2_DEBUGGING_INFO) || defined (DWARF2_UNWIND_INFO)

/* Forward declarations for functions defined in this file.  */

static char *stripattributes (const char *);
static const char *dwarf_cfi_name (unsigned);
static dw_cfi_ref new_cfi (void);
static void add_cfi (dw_cfi_ref *, dw_cfi_ref);
static void add_fde_cfi (const char *, dw_cfi_ref);
static void lookup_cfa_1 (dw_cfi_ref, dw_cfa_location *);
static void lookup_cfa (dw_cfa_location *);
static void reg_save (const char *, unsigned, unsigned, HOST_WIDE_INT);
static void initial_return_save (rtx);
static HOST_WIDE_INT stack_adjust_offset (rtx);
static void output_cfi (dw_cfi_ref, dw_fde_ref, int);
static void output_call_frame_info (int);
static void dwarf2out_stack_adjust (rtx, bool);
static void flush_queued_reg_saves (void);
static bool clobbers_queued_reg_save (rtx);
static void dwarf2out_frame_debug_expr (rtx, const char *);

/* Support for complex CFA locations.  */
static void output_cfa_loc (dw_cfi_ref);
static void get_cfa_from_loc_descr (dw_cfa_location *,
				    struct dw_loc_descr_struct *);
static struct dw_loc_descr_struct *build_cfa_loc
  (dw_cfa_location *, HOST_WIDE_INT);
static void def_cfa_1 (const char *, dw_cfa_location *);

/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif

/* Data and reference forms for relocatable data.  */
#define DW_FORM_data (DWARF_OFFSET_SIZE == 8 ? DW_FORM_data8 : DW_FORM_data4)
#define DW_FORM_ref (DWARF_OFFSET_SIZE == 8 ? DW_FORM_ref8 : DW_FORM_ref4)

#ifndef DEBUG_FRAME_SECTION
#define DEBUG_FRAME_SECTION	".debug_frame"
#endif

#ifndef FUNC_BEGIN_LABEL
#define FUNC_BEGIN_LABEL	"LFB"
#endif

#ifndef FUNC_END_LABEL
#define FUNC_END_LABEL		"LFE"
#endif

#ifndef FRAME_BEGIN_LABEL
#define FRAME_BEGIN_LABEL	"Lframe"
#endif
#define CIE_AFTER_SIZE_LABEL	"LSCIE"
#define CIE_END_LABEL		"LECIE"
#define FDE_LABEL		"LSFDE"
#define FDE_AFTER_SIZE_LABEL	"LASFDE"
#define FDE_END_LABEL		"LEFDE"
#define LINE_NUMBER_BEGIN_LABEL	"LSLT"
#define LINE_NUMBER_END_LABEL	"LELT"
#define LN_PROLOG_AS_LABEL	"LASLTP"
#define LN_PROLOG_END_LABEL	"LELTP"
#define DIE_LABEL_PREFIX	"DW"

/* The DWARF 2 CFA column which tracks the return address.  Normally this
   is the column for PC, or the first column after all of the hard
   registers.  */
#ifndef DWARF_FRAME_RETURN_COLUMN
#ifdef PC_REGNUM
#define DWARF_FRAME_RETURN_COLUMN	DWARF_FRAME_REGNUM (PC_REGNUM)
#else
#define DWARF_FRAME_RETURN_COLUMN	DWARF_FRAME_REGISTERS
#endif
#endif

/* The mapping from gcc register number to DWARF 2 CFA column number.  By
   default, we just provide columns for all registers.  */
#ifndef DWARF_FRAME_REGNUM
#define DWARF_FRAME_REGNUM(REG) DBX_REGISTER_NUMBER (REG)
#endif

/* Hook used by __throw.  */

rtx
expand_builtin_dwarf_sp_column (void)
{
  unsigned int dwarf_regnum = DWARF_FRAME_REGNUM (STACK_POINTER_REGNUM);
  return GEN_INT (DWARF2_FRAME_REG_OUT (dwarf_regnum, 1));
}

/* Return a pointer to a copy of the section string name S with all
   attributes stripped off, and an asterisk prepended (for assemble_name).  */

static inline char *
stripattributes (const char *s)
{
  char *stripped = XNEWVEC (char, strlen (s) + 2);
  char *p = stripped;

  *p++ = '*';

  while (*s && *s != ',')
    *p++ = *s++;

  *p = '\0';
  return stripped;
}

/* Generate code to initialize the register size table.  */

void
expand_builtin_init_dwarf_reg_sizes (tree address)
{
  unsigned int i;
  enum machine_mode mode = TYPE_MODE (char_type_node);
  rtx addr = expand_normal (address);
  rtx mem = gen_rtx_MEM (BLKmode, addr);
  bool wrote_return_column = false;

  for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
    {
      int rnum = DWARF2_FRAME_REG_OUT (DWARF_FRAME_REGNUM (i), 1);
      
      if (rnum < DWARF_FRAME_REGISTERS)
	{
	  HOST_WIDE_INT offset = rnum * GET_MODE_SIZE (mode);
	  enum machine_mode save_mode = reg_raw_mode[i];
	  HOST_WIDE_INT size;
	  
	  if (HARD_REGNO_CALL_PART_CLOBBERED (i, save_mode))
	    save_mode = choose_hard_reg_mode (i, 1, true);
	  if (DWARF_FRAME_REGNUM (i) == DWARF_FRAME_RETURN_COLUMN)
	    {
	      if (save_mode == VOIDmode)
		continue;
	      wrote_return_column = true;
	    }
	  size = GET_MODE_SIZE (save_mode);
	  if (offset < 0)
	    continue;
	  
	  emit_move_insn (adjust_address (mem, mode, offset),
			  gen_int_mode (size, mode));
	}
    }

#ifdef DWARF_ALT_FRAME_RETURN_COLUMN
  gcc_assert (wrote_return_column);
  i = DWARF_ALT_FRAME_RETURN_COLUMN;
  wrote_return_column = false;
#else
  i = DWARF_FRAME_RETURN_COLUMN;
#endif

  if (! wrote_return_column)
    {
      enum machine_mode save_mode = Pmode;
      HOST_WIDE_INT offset = i * GET_MODE_SIZE (mode);
      HOST_WIDE_INT size = GET_MODE_SIZE (save_mode);
      emit_move_insn (adjust_address (mem, mode, offset), GEN_INT (size));
    }
}

/* Convert a DWARF call frame info. operation to its string name */

static const char *
dwarf_cfi_name (unsigned int cfi_opc)
{
  switch (cfi_opc)
    {
    case DW_CFA_advance_loc:
      return "DW_CFA_advance_loc";
    case DW_CFA_offset:
      return "DW_CFA_offset";
    case DW_CFA_restore:
      return "DW_CFA_restore";
    case DW_CFA_nop:
      return "DW_CFA_nop";
    case DW_CFA_set_loc:
      return "DW_CFA_set_loc";
    case DW_CFA_advance_loc1:
      return "DW_CFA_advance_loc1";
    case DW_CFA_advance_loc2:
      return "DW_CFA_advance_loc2";
    case DW_CFA_advance_loc4:
      return "DW_CFA_advance_loc4";
    case DW_CFA_offset_extended:
      return "DW_CFA_offset_extended";
    case DW_CFA_restore_extended:
      return "DW_CFA_restore_extended";
    case DW_CFA_undefined:
      return "DW_CFA_undefined";
    case DW_CFA_same_value:
      return "DW_CFA_same_value";
    case DW_CFA_register:
      return "DW_CFA_register";
    case DW_CFA_remember_state:
      return "DW_CFA_remember_state";
    case DW_CFA_restore_state:
      return "DW_CFA_restore_state";
    case DW_CFA_def_cfa:
      return "DW_CFA_def_cfa";
    case DW_CFA_def_cfa_register:
      return "DW_CFA_def_cfa_register";
    case DW_CFA_def_cfa_offset:
      return "DW_CFA_def_cfa_offset";

    /* DWARF 3 */
    case DW_CFA_def_cfa_expression:
      return "DW_CFA_def_cfa_expression";
    case DW_CFA_expression:
      return "DW_CFA_expression";
    case DW_CFA_offset_extended_sf:
      return "DW_CFA_offset_extended_sf";
    case DW_CFA_def_cfa_sf:
      return "DW_CFA_def_cfa_sf";
    case DW_CFA_def_cfa_offset_sf:
      return "DW_CFA_def_cfa_offset_sf";

    /* SGI/MIPS specific */
    case DW_CFA_MIPS_advance_loc8:
      return "DW_CFA_MIPS_advance_loc8";

    /* GNU extensions */
    case DW_CFA_GNU_window_save:
      return "DW_CFA_GNU_window_save";
    case DW_CFA_GNU_args_size:
      return "DW_CFA_GNU_args_size";
    case DW_CFA_GNU_negative_offset_extended:
      return "DW_CFA_GNU_negative_offset_extended";

    default:
      return "DW_CFA_<unknown>";
    }
}

/* Return a pointer to a newly allocated Call Frame Instruction.  */

static inline dw_cfi_ref
new_cfi (void)
{
  dw_cfi_ref cfi = ggc_alloc (sizeof (dw_cfi_node));

  cfi->dw_cfi_next = NULL;
  cfi->dw_cfi_oprnd1.dw_cfi_reg_num = 0;
  cfi->dw_cfi_oprnd2.dw_cfi_reg_num = 0;

  return cfi;
}

/* Add a Call Frame Instruction to list of instructions.  */

static inline void
add_cfi (dw_cfi_ref *list_head, dw_cfi_ref cfi)
{
  dw_cfi_ref *p;

  /* Find the end of the chain.  */
  for (p = list_head; (*p) != NULL; p = &(*p)->dw_cfi_next)
    ;

  *p = cfi;
}

/* Generate a new label for the CFI info to refer to.  */

char *
dwarf2out_cfi_label (void)
{
  static char label[20];

  ASM_GENERATE_INTERNAL_LABEL (label, "LCFI", dwarf2out_cfi_label_num++);
  ASM_OUTPUT_LABEL (asm_out_file, label);
  return label;
}

/* Add CFI to the current fde at the PC value indicated by LABEL if specified,
   or to the CIE if LABEL is NULL.  */

static void
add_fde_cfi (const char *label, dw_cfi_ref cfi)
{
  if (label)
    {
      dw_fde_ref fde = &fde_table[fde_table_in_use - 1];

      if (*label == 0)
	label = dwarf2out_cfi_label ();

      if (fde->dw_fde_current_label == NULL
	  || strcmp (label, fde->dw_fde_current_label) != 0)
	{
	  dw_cfi_ref xcfi;

	  label = xstrdup (label);

	  /* Set the location counter to the new label.  */
	  xcfi = new_cfi ();
	  /* If we have a current label, advance from there, otherwise
	     set the location directly using set_loc.  */
	  xcfi->dw_cfi_opc = fde->dw_fde_current_label
			     ? DW_CFA_advance_loc4
			     : DW_CFA_set_loc;
	  xcfi->dw_cfi_oprnd1.dw_cfi_addr = label;
	  add_cfi (&fde->dw_fde_cfi, xcfi);

	  fde->dw_fde_current_label = label;
	}

      add_cfi (&fde->dw_fde_cfi, cfi);
    }

  else
    add_cfi (&cie_cfi_head, cfi);
}

/* Subroutine of lookup_cfa.  */

static void
lookup_cfa_1 (dw_cfi_ref cfi, dw_cfa_location *loc)
{
  switch (cfi->dw_cfi_opc)
    {
    case DW_CFA_def_cfa_offset:
      loc->offset = cfi->dw_cfi_oprnd1.dw_cfi_offset;
      break;
    case DW_CFA_def_cfa_offset_sf:
      loc->offset
	= cfi->dw_cfi_oprnd1.dw_cfi_offset * DWARF_CIE_DATA_ALIGNMENT;
      break;
    case DW_CFA_def_cfa_register:
      loc->reg = cfi->dw_cfi_oprnd1.dw_cfi_reg_num;
      break;
    case DW_CFA_def_cfa:
      loc->reg = cfi->dw_cfi_oprnd1.dw_cfi_reg_num;
      loc->offset = cfi->dw_cfi_oprnd2.dw_cfi_offset;
      break;
    case DW_CFA_def_cfa_sf:
      loc->reg = cfi->dw_cfi_oprnd1.dw_cfi_reg_num;
      loc->offset
	= cfi->dw_cfi_oprnd2.dw_cfi_offset * DWARF_CIE_DATA_ALIGNMENT;
      break;
    case DW_CFA_def_cfa_expression:
      get_cfa_from_loc_descr (loc, cfi->dw_cfi_oprnd1.dw_cfi_loc);
      break;
    default:
      break;
    }
}

/* Find the previous value for the CFA.  */

static void
lookup_cfa (dw_cfa_location *loc)
{
  dw_cfi_ref cfi;

  loc->reg = INVALID_REGNUM;
  loc->offset = 0;
  loc->indirect = 0;
  loc->base_offset = 0;

  for (cfi = cie_cfi_head; cfi; cfi = cfi->dw_cfi_next)
    lookup_cfa_1 (cfi, loc);

  if (fde_table_in_use)
    {
      dw_fde_ref fde = &fde_table[fde_table_in_use - 1];
      for (cfi = fde->dw_fde_cfi; cfi; cfi = cfi->dw_cfi_next)
	lookup_cfa_1 (cfi, loc);
    }
}

/* The current rule for calculating the DWARF2 canonical frame address.  */
static dw_cfa_location cfa;

/* The register used for saving registers to the stack, and its offset
   from the CFA.  */
static dw_cfa_location cfa_store;

/* The running total of the size of arguments pushed onto the stack.  */
static HOST_WIDE_INT args_size;

/* The last args_size we actually output.  */
static HOST_WIDE_INT old_args_size;

/* Entry point to update the canonical frame address (CFA).
   LABEL is passed to add_fde_cfi.  The value of CFA is now to be
   calculated from REG+OFFSET.  */

void
dwarf2out_def_cfa (const char *label, unsigned int reg, HOST_WIDE_INT offset)
{
  dw_cfa_location loc;
  loc.indirect = 0;
  loc.base_offset = 0;
  loc.reg = reg;
  loc.offset = offset;
  def_cfa_1 (label, &loc);
}

/* Determine if two dw_cfa_location structures define the same data.  */

static bool
cfa_equal_p (const dw_cfa_location *loc1, const dw_cfa_location *loc2)
{
  return (loc1->reg == loc2->reg
	  && loc1->offset == loc2->offset
	  && loc1->indirect == loc2->indirect
	  && (loc1->indirect == 0
	      || loc1->base_offset == loc2->base_offset));
}

/* This routine does the actual work.  The CFA is now calculated from
   the dw_cfa_location structure.  */

static void
def_cfa_1 (const char *label, dw_cfa_location *loc_p)
{
  dw_cfi_ref cfi;
  dw_cfa_location old_cfa, loc;

  cfa = *loc_p;
  loc = *loc_p;

  if (cfa_store.reg == loc.reg && loc.indirect == 0)
    cfa_store.offset = loc.offset;

  loc.reg = DWARF_FRAME_REGNUM (loc.reg);
  lookup_cfa (&old_cfa);

  /* If nothing changed, no need to issue any call frame instructions.  */
  if (cfa_equal_p (&loc, &old_cfa))
    return;

  cfi = new_cfi ();

  if (loc.reg == old_cfa.reg && !loc.indirect)
    {
      /* Construct a "DW_CFA_def_cfa_offset <offset>" instruction, indicating
	 the CFA register did not change but the offset did.  */
      if (loc.offset < 0)
	{
	  HOST_WIDE_INT f_offset = loc.offset / DWARF_CIE_DATA_ALIGNMENT;
	  gcc_assert (f_offset * DWARF_CIE_DATA_ALIGNMENT == loc.offset);

	  cfi->dw_cfi_opc = DW_CFA_def_cfa_offset_sf;
	  cfi->dw_cfi_oprnd1.dw_cfi_offset = f_offset;
	}
      else
	{
	  cfi->dw_cfi_opc = DW_CFA_def_cfa_offset;
	  cfi->dw_cfi_oprnd1.dw_cfi_offset = loc.offset;
	}
    }

#ifndef MIPS_DEBUGGING_INFO  /* SGI dbx thinks this means no offset.  */
  else if (loc.offset == old_cfa.offset
	   && old_cfa.reg != INVALID_REGNUM
	   && !loc.indirect)
    {
      /* Construct a "DW_CFA_def_cfa_register <register>" instruction,
	 indicating the CFA register has changed to <register> but the
	 offset has not changed.  */
      cfi->dw_cfi_opc = DW_CFA_def_cfa_register;
      cfi->dw_cfi_oprnd1.dw_cfi_reg_num = loc.reg;
    }
#endif

  else if (loc.indirect == 0)
    {
      /* Construct a "DW_CFA_def_cfa <register> <offset>" instruction,
	 indicating the CFA register has changed to <register> with
	 the specified offset.  */
      if (loc.offset < 0)
	{
	  HOST_WIDE_INT f_offset = loc.offset / DWARF_CIE_DATA_ALIGNMENT;
	  gcc_assert (f_offset * DWARF_CIE_DATA_ALIGNMENT == loc.offset);

	  cfi->dw_cfi_opc = DW_CFA_def_cfa_sf;
	  cfi->dw_cfi_oprnd1.dw_cfi_reg_num = loc.reg;
	  cfi->dw_cfi_oprnd2.dw_cfi_offset = f_offset;
	}
      else
	{
	  cfi->dw_cfi_opc = DW_CFA_def_cfa;
	  cfi->dw_cfi_oprnd1.dw_cfi_reg_num = loc.reg;
	  cfi->dw_cfi_oprnd2.dw_cfi_offset = loc.offset;
	}
    }
  else
    {
      /* Construct a DW_CFA_def_cfa_expression instruction to
	 calculate the CFA using a full location expression since no
	 register-offset pair is available.  */
      struct dw_loc_descr_struct *loc_list;

      cfi->dw_cfi_opc = DW_CFA_def_cfa_expression;
      loc_list = build_cfa_loc (&loc, 0);
      cfi->dw_cfi_oprnd1.dw_cfi_loc = loc_list;
    }

  add_fde_cfi (label, cfi);
}

/* Add the CFI for saving a register.  REG is the CFA column number.
   LABEL is passed to add_fde_cfi.
   If SREG is -1, the register is saved at OFFSET from the CFA;
   otherwise it is saved in SREG.  */

static void
reg_save (const char *label, unsigned int reg, unsigned int sreg, HOST_WIDE_INT offset)
{
  dw_cfi_ref cfi = new_cfi ();

  cfi->dw_cfi_oprnd1.dw_cfi_reg_num = reg;

  if (sreg == INVALID_REGNUM)
    {
      if (reg & ~0x3f)
	/* The register number won't fit in 6 bits, so we have to use
	   the long form.  */
	cfi->dw_cfi_opc = DW_CFA_offset_extended;
      else
	cfi->dw_cfi_opc = DW_CFA_offset;

#ifdef ENABLE_CHECKING
      {
	/* If we get an offset that is not a multiple of
	   DWARF_CIE_DATA_ALIGNMENT, there is either a bug in the
	   definition of DWARF_CIE_DATA_ALIGNMENT, or a bug in the machine
	   description.  */
	HOST_WIDE_INT check_offset = offset / DWARF_CIE_DATA_ALIGNMENT;

	gcc_assert (check_offset * DWARF_CIE_DATA_ALIGNMENT == offset);
      }
#endif
      offset /= DWARF_CIE_DATA_ALIGNMENT;
      if (offset < 0)
	cfi->dw_cfi_opc = DW_CFA_offset_extended_sf;

      cfi->dw_cfi_oprnd2.dw_cfi_offset = offset;
    }
  else if (sreg == reg)
    cfi->dw_cfi_opc = DW_CFA_same_value;
  else
    {
      cfi->dw_cfi_opc = DW_CFA_register;
      cfi->dw_cfi_oprnd2.dw_cfi_reg_num = sreg;
    }

  add_fde_cfi (label, cfi);
}

/* Add the CFI for saving a register window.  LABEL is passed to reg_save.
   This CFI tells the unwinder that it needs to restore the window registers
   from the previous frame's window save area.

   ??? Perhaps we should note in the CIE where windows are saved (instead of
   assuming 0(cfa)) and what registers are in the window.  */

void
dwarf2out_window_save (const char *label)
{
  dw_cfi_ref cfi = new_cfi ();

  cfi->dw_cfi_opc = DW_CFA_GNU_window_save;
  add_fde_cfi (label, cfi);
}

/* Add a CFI to update the running total of the size of arguments
   pushed onto the stack.  */

void
dwarf2out_args_size (const char *label, HOST_WIDE_INT size)
{
  dw_cfi_ref cfi;

  if (size == old_args_size)
    return;

  old_args_size = size;

  cfi = new_cfi ();
  cfi->dw_cfi_opc = DW_CFA_GNU_args_size;
  cfi->dw_cfi_oprnd1.dw_cfi_offset = size;
  add_fde_cfi (label, cfi);
}

/* Entry point for saving a register to the stack.  REG is the GCC register
   number.  LABEL and OFFSET are passed to reg_save.  */

void
dwarf2out_reg_save (const char *label, unsigned int reg, HOST_WIDE_INT offset)
{
  reg_save (label, DWARF_FRAME_REGNUM (reg), INVALID_REGNUM, offset);
}

/* Entry point for saving the return address in the stack.
   LABEL and OFFSET are passed to reg_save.  */

void
dwarf2out_return_save (const char *label, HOST_WIDE_INT offset)
{
  reg_save (label, DWARF_FRAME_RETURN_COLUMN, INVALID_REGNUM, offset);
}

/* Entry point for saving the return address in a register.
   LABEL and SREG are passed to reg_save.  */

void
dwarf2out_return_reg (const char *label, unsigned int sreg)
{
  reg_save (label, DWARF_FRAME_RETURN_COLUMN, DWARF_FRAME_REGNUM (sreg), 0);
}

/* Record the initial position of the return address.  RTL is
   INCOMING_RETURN_ADDR_RTX.  */

static void
initial_return_save (rtx rtl)
{
  unsigned int reg = INVALID_REGNUM;
  HOST_WIDE_INT offset = 0;

  switch (GET_CODE (rtl))
    {
    case REG:
      /* RA is in a register.  */
      reg = DWARF_FRAME_REGNUM (REGNO (rtl));
      break;

    case MEM:
      /* RA is on the stack.  */
      rtl = XEXP (rtl, 0);
      switch (GET_CODE (rtl))
	{
	case REG:
	  gcc_assert (REGNO (rtl) == STACK_POINTER_REGNUM);
	  offset = 0;
	  break;

	case PLUS:
	  gcc_assert (REGNO (XEXP (rtl, 0)) == STACK_POINTER_REGNUM);
	  offset = INTVAL (XEXP (rtl, 1));
	  break;

	case MINUS:
	  gcc_assert (REGNO (XEXP (rtl, 0)) == STACK_POINTER_REGNUM);
	  offset = -INTVAL (XEXP (rtl, 1));
	  break;

	default:
	  gcc_unreachable ();
	}

      break;

    case PLUS:
      /* The return address is at some offset from any value we can
	 actually load.  For instance, on the SPARC it is in %i7+8. Just
	 ignore the offset for now; it doesn't matter for unwinding frames.  */
      gcc_assert (GET_CODE (XEXP (rtl, 1)) == CONST_INT);
      initial_return_save (XEXP (rtl, 0));
      return;

    default:
      gcc_unreachable ();
    }

  if (reg != DWARF_FRAME_RETURN_COLUMN)
    reg_save (NULL, DWARF_FRAME_RETURN_COLUMN, reg, offset - cfa.offset);
}

/* Given a SET, calculate the amount of stack adjustment it
   contains.  */

static HOST_WIDE_INT
stack_adjust_offset (rtx pattern)
{
  rtx src = SET_SRC (pattern);
  rtx dest = SET_DEST (pattern);
  HOST_WIDE_INT offset = 0;
  enum rtx_code code;

  if (dest == stack_pointer_rtx)
    {
      /* (set (reg sp) (plus (reg sp) (const_int))) */
      code = GET_CODE (src);
      if (! (code == PLUS || code == MINUS)
	  || XEXP (src, 0) != stack_pointer_rtx
	  || GET_CODE (XEXP (src, 1)) != CONST_INT)
	return 0;

      offset = INTVAL (XEXP (src, 1));
      if (code == PLUS)
	offset = -offset;
    }
  else if (MEM_P (dest))
    {
      /* (set (mem (pre_dec (reg sp))) (foo)) */
      src = XEXP (dest, 0);
      code = GET_CODE (src);

      switch (code)
	{
	case PRE_MODIFY:
	case POST_MODIFY:
	  if (XEXP (src, 0) == stack_pointer_rtx)
	    {
	      rtx val = XEXP (XEXP (src, 1), 1);
	      /* We handle only adjustments by constant amount.  */
	      gcc_assert (GET_CODE (XEXP (src, 1)) == PLUS
			  && GET_CODE (val) == CONST_INT);
	      offset = -INTVAL (val);
	      break;
	    }
	  return 0;

	case PRE_DEC:
	case POST_DEC:
	  if (XEXP (src, 0) == stack_pointer_rtx)
	    {
	      offset = GET_MODE_SIZE (GET_MODE (dest));
	      break;
	    }
	  return 0;

	case PRE_INC:
	case POST_INC:
	  if (XEXP (src, 0) == stack_pointer_rtx)
	    {
	      offset = -GET_MODE_SIZE (GET_MODE (dest));
	      break;
	    }
	  return 0;

	default:
	  return 0;
	}
    }
  else
    return 0;

  return offset;
}

/* Check INSN to see if it looks like a push or a stack adjustment, and
   make a note of it if it does.  EH uses this information to find out how
   much extra space it needs to pop off the stack.  */

static void
dwarf2out_stack_adjust (rtx insn, bool after_p)
{
  HOST_WIDE_INT offset;
  const char *label;
  int i;

  /* Don't handle epilogues at all.  Certainly it would be wrong to do so
     with this function.  Proper support would require all frame-related
     insns to be marked, and to be able to handle saving state around
     epilogues textually in the middle of the function.  */
  if (prologue_epilogue_contains (insn) || sibcall_epilogue_contains (insn))
    return;

  /* If only calls can throw, and we have a frame pointer,
     save up adjustments until we see the CALL_INSN.  */
  if (!flag_asynchronous_unwind_tables && cfa.reg != STACK_POINTER_REGNUM)
    {
      if (CALL_P (insn) && !after_p)
	{
	  /* Extract the size of the args from the CALL rtx itself.  */
	  insn = PATTERN (insn);
	  if (GET_CODE (insn) == PARALLEL)
	    insn = XVECEXP (insn, 0, 0);
	  if (GET_CODE (insn) == SET)
	    insn = SET_SRC (insn);
	  gcc_assert (GET_CODE (insn) == CALL);
	  dwarf2out_args_size ("", INTVAL (XEXP (insn, 1)));
	}
      return;
    }

  if (CALL_P (insn) && !after_p)
    {
      if (!flag_asynchronous_unwind_tables)
	dwarf2out_args_size ("", args_size);
      return;
    }
  else if (BARRIER_P (insn))
    {
      /* When we see a BARRIER, we know to reset args_size to 0.  Usually
	 the compiler will have already emitted a stack adjustment, but
	 doesn't bother for calls to noreturn functions.  */
#ifdef STACK_GROWS_DOWNWARD
      offset = -args_size;
#else
      offset = args_size;
#endif
    }
  else if (GET_CODE (PATTERN (insn)) == SET)
    offset = stack_adjust_offset (PATTERN (insn));
  else if (GET_CODE (PATTERN (insn)) == PARALLEL
	   || GET_CODE (PATTERN (insn)) == SEQUENCE)
    {
      /* There may be stack adjustments inside compound insns.  Search
	 for them.  */
      for (offset = 0, i = XVECLEN (PATTERN (insn), 0) - 1; i >= 0; i--)
	if (GET_CODE (XVECEXP (PATTERN (insn), 0, i)) == SET)
	  offset += stack_adjust_offset (XVECEXP (PATTERN (insn), 0, i));
    }
  else
    return;

  if (offset == 0)
    return;

  if (cfa.reg == STACK_POINTER_REGNUM)
    cfa.offset += offset;

#ifndef STACK_GROWS_DOWNWARD
  offset = -offset;
#endif

  args_size += offset;
  if (args_size < 0)
    args_size = 0;

  label = dwarf2out_cfi_label ();
  def_cfa_1 (label, &cfa);
  if (flag_asynchronous_unwind_tables)
    dwarf2out_args_size (label, args_size);
}

#endif

/* We delay emitting a register save until either (a) we reach the end
   of the prologue or (b) the register is clobbered.  This clusters
   register saves so that there are fewer pc advances.  */

struct queued_reg_save GTY(())
{
  struct queued_reg_save *next;
  rtx reg;
  HOST_WIDE_INT cfa_offset;
  rtx saved_reg;
};

static GTY(()) struct queued_reg_save *queued_reg_saves;

/* The caller's ORIG_REG is saved in SAVED_IN_REG.  */
struct reg_saved_in_data GTY(()) {
  rtx orig_reg;
  rtx saved_in_reg;
};

/* A list of registers saved in other registers.
   The list intentionally has a small maximum capacity of 4; if your
   port needs more than that, you might consider implementing a
   more efficient data structure.  */
static GTY(()) struct reg_saved_in_data regs_saved_in_regs[4];
static GTY(()) size_t num_regs_saved_in_regs;

#if defined (DWARF2_DEBUGGING_INFO) || defined (DWARF2_UNWIND_INFO)
static const char *last_reg_save_label;

/* Add an entry to QUEUED_REG_SAVES saying that REG is now saved at
   SREG, or if SREG is NULL then it is saved at OFFSET to the CFA.  */

static void
queue_reg_save (const char *label, rtx reg, rtx sreg, HOST_WIDE_INT offset)
{
  struct queued_reg_save *q;

  /* Duplicates waste space, but it's also necessary to remove them
     for correctness, since the queue gets output in reverse
     order.  */
  for (q = queued_reg_saves; q != NULL; q = q->next)
    if (REGNO (q->reg) == REGNO (reg))
      break;

  if (q == NULL)
    {
      q = ggc_alloc (sizeof (*q));
      q->next = queued_reg_saves;
      queued_reg_saves = q;
    }

  q->reg = reg;
  q->cfa_offset = offset;
  q->saved_reg = sreg;

  last_reg_save_label = label;
}

/* Output all the entries in QUEUED_REG_SAVES.  */

static void
flush_queued_reg_saves (void)
{
  struct queued_reg_save *q;

  for (q = queued_reg_saves; q; q = q->next)
    {
      size_t i;
      unsigned int reg, sreg;

      for (i = 0; i < num_regs_saved_in_regs; i++)
	if (REGNO (regs_saved_in_regs[i].orig_reg) == REGNO (q->reg))
	  break;
      if (q->saved_reg && i == num_regs_saved_in_regs)
	{
	  gcc_assert (i != ARRAY_SIZE (regs_saved_in_regs));
	  num_regs_saved_in_regs++;
	}
      if (i != num_regs_saved_in_regs)
	{
	  regs_saved_in_regs[i].orig_reg = q->reg;
	  regs_saved_in_regs[i].saved_in_reg = q->saved_reg;
	}

      reg = DWARF_FRAME_REGNUM (REGNO (q->reg));
      if (q->saved_reg)
	sreg = DWARF_FRAME_REGNUM (REGNO (q->saved_reg));
      else
	sreg = INVALID_REGNUM;
      reg_save (last_reg_save_label, reg, sreg, q->cfa_offset);
    }

  queued_reg_saves = NULL;
  last_reg_save_label = NULL;
}

/* Does INSN clobber any register which QUEUED_REG_SAVES lists a saved
   location for?  Or, does it clobber a register which we've previously
   said that some other register is saved in, and for which we now
   have a new location for?  */

static bool
clobbers_queued_reg_save (rtx insn)
{
  struct queued_reg_save *q;

  for (q = queued_reg_saves; q; q = q->next)
    {
      size_t i;
      if (modified_in_p (q->reg, insn))
	return true;
      for (i = 0; i < num_regs_saved_in_regs; i++)
	if (REGNO (q->reg) == REGNO (regs_saved_in_regs[i].orig_reg)
	    && modified_in_p (regs_saved_in_regs[i].saved_in_reg, insn))
	  return true;
    }

  return false;
}

/* Entry point for saving the first register into the second.  */

void
dwarf2out_reg_save_reg (const char *label, rtx reg, rtx sreg)
{
  size_t i;
  unsigned int regno, sregno;

  for (i = 0; i < num_regs_saved_in_regs; i++)
    if (REGNO (regs_saved_in_regs[i].orig_reg) == REGNO (reg))
      break;
  if (i == num_regs_saved_in_regs)
    {
      gcc_assert (i != ARRAY_SIZE (regs_saved_in_regs));
      num_regs_saved_in_regs++;
    }
  regs_saved_in_regs[i].orig_reg = reg;
  regs_saved_in_regs[i].saved_in_reg = sreg;

  regno = DWARF_FRAME_REGNUM (REGNO (reg));
  sregno = DWARF_FRAME_REGNUM (REGNO (sreg));
  reg_save (label, regno, sregno, 0);
}

/* What register, if any, is currently saved in REG?  */

static rtx
reg_saved_in (rtx reg)
{
  unsigned int regn = REGNO (reg);
  size_t i;
  struct queued_reg_save *q;

  for (q = queued_reg_saves; q; q = q->next)
    if (q->saved_reg && regn == REGNO (q->saved_reg))
      return q->reg;

  for (i = 0; i < num_regs_saved_in_regs; i++)
    if (regs_saved_in_regs[i].saved_in_reg
	&& regn == REGNO (regs_saved_in_regs[i].saved_in_reg))
      return regs_saved_in_regs[i].orig_reg;

  return NULL_RTX;
}


/* A temporary register holding an integral value used in adjusting SP
   or setting up the store_reg.  The "offset" field holds the integer
   value, not an offset.  */
static dw_cfa_location cfa_temp;

/* Record call frame debugging information for an expression EXPR,
   which either sets SP or FP (adjusting how we calculate the frame
   address) or saves a register to the stack or another register.
   LABEL indicates the address of EXPR.

   This function encodes a state machine mapping rtxes to actions on
   cfa, cfa_store, and cfa_temp.reg.  We describe these rules so
   users need not read the source code.

  The High-Level Picture

  Changes in the register we use to calculate the CFA: Currently we
  assume that if you copy the CFA register into another register, we
  should take the other one as the new CFA register; this seems to
  work pretty well.  If it's wrong for some target, it's simple
  enough not to set RTX_FRAME_RELATED_P on the insn in question.

  Changes in the register we use for saving registers to the stack:
  This is usually SP, but not always.  Again, we deduce that if you
  copy SP into another register (and SP is not the CFA register),
  then the new register is the one we will be using for register
  saves.  This also seems to work.

  Register saves: There's not much guesswork about this one; if
  RTX_FRAME_RELATED_P is set on an insn which modifies memory, it's a
  register save, and the register used to calculate the destination
  had better be the one we think we're using for this purpose.
  It's also assumed that a copy from a call-saved register to another
  register is saving that register if RTX_FRAME_RELATED_P is set on
  that instruction.  If the copy is from a call-saved register to
  the *same* register, that means that the register is now the same
  value as in the caller.

  Except: If the register being saved is the CFA register, and the
  offset is nonzero, we are saving the CFA, so we assume we have to
  use DW_CFA_def_cfa_expression.  If the offset is 0, we assume that
  the intent is to save the value of SP from the previous frame.

  In addition, if a register has previously been saved to a different
  register,

  Invariants / Summaries of Rules

  cfa	       current rule for calculating the CFA.  It usually
	       consists of a register and an offset.
  cfa_store    register used by prologue code to save things to the stack
	       cfa_store.offset is the offset from the value of
	       cfa_store.reg to the actual CFA
  cfa_temp     register holding an integral value.  cfa_temp.offset
	       stores the value, which will be used to adjust the
	       stack pointer.  cfa_temp is also used like cfa_store,
	       to track stores to the stack via fp or a temp reg.

  Rules  1- 4: Setting a register's value to cfa.reg or an expression
	       with cfa.reg as the first operand changes the cfa.reg and its
	       cfa.offset.  Rule 1 and 4 also set cfa_temp.reg and
	       cfa_temp.offset.

  Rules  6- 9: Set a non-cfa.reg register value to a constant or an
	       expression yielding a constant.  This sets cfa_temp.reg
	       and cfa_temp.offset.

  Rule 5:      Create a new register cfa_store used to save items to the
	       stack.

  Rules 10-14: Save a register to the stack.  Define offset as the
	       difference of the original location and cfa_store's
	       location (or cfa_temp's location if cfa_temp is used).

  The Rules

  "{a,b}" indicates a choice of a xor b.
  "<reg>:cfa.reg" indicates that <reg> must equal cfa.reg.

  Rule 1:
  (set <reg1> <reg2>:cfa.reg)
  effects: cfa.reg = <reg1>
	   cfa.offset unchanged
	   cfa_temp.reg = <reg1>
	   cfa_temp.offset = cfa.offset

  Rule 2:
  (set sp ({minus,plus,losum} {sp,fp}:cfa.reg
			      {<const_int>,<reg>:cfa_temp.reg}))
  effects: cfa.reg = sp if fp used
	   cfa.offset += {+/- <const_int>, cfa_temp.offset} if cfa.reg==sp
	   cfa_store.offset += {+/- <const_int>, cfa_temp.offset}
	     if cfa_store.reg==sp

  Rule 3:
  (set fp ({minus,plus,losum} <reg>:cfa.reg <const_int>))
  effects: cfa.reg = fp
	   cfa_offset += +/- <const_int>

  Rule 4:
  (set <reg1> ({plus,losum} <reg2>:cfa.reg <const_int>))
  constraints: <reg1> != fp
	       <reg1> != sp
  effects: cfa.reg = <reg1>
	   cfa_temp.reg = <reg1>
	   cfa_temp.offset = cfa.offset

  Rule 5:
  (set <reg1> (plus <reg2>:cfa_temp.reg sp:cfa.reg))
  constraints: <reg1> != fp
	       <reg1> != sp
  effects: cfa_store.reg = <reg1>
	   cfa_store.offset = cfa.offset - cfa_temp.offset

  Rule 6:
  (set <reg> <const_int>)
  effects: cfa_temp.reg = <reg>
	   cfa_temp.offset = <const_int>

  Rule 7:
  (set <reg1>:cfa_temp.reg (ior <reg2>:cfa_temp.reg <const_int>))
  effects: cfa_temp.reg = <reg1>
	   cfa_temp.offset |= <const_int>

  Rule 8:
  (set <reg> (high <exp>))
  effects: none

  Rule 9:
  (set <reg> (lo_sum <exp> <const_int>))
  effects: cfa_temp.reg = <reg>
	   cfa_temp.offset = <const_int>

  Rule 10:
  (set (mem (pre_modify sp:cfa_store (???? <reg1> <const_int>))) <reg2>)
  effects: cfa_store.offset -= <const_int>
	   cfa.offset = cfa_store.offset if cfa.reg == sp
	   cfa.reg = sp
	   cfa.base_offset = -cfa_store.offset

  Rule 11:
  (set (mem ({pre_inc,pre_dec} sp:cfa_store.reg)) <reg>)
  effects: cfa_store.offset += -/+ mode_size(mem)
	   cfa.offset = cfa_store.offset if cfa.reg == sp
	   cfa.reg = sp
	   cfa.base_offset = -cfa_store.offset

  Rule 12:
  (set (mem ({minus,plus,losum} <reg1>:{cfa_store,cfa_temp} <const_int>))

       <reg2>)
  effects: cfa.reg = <reg1>
	   cfa.base_offset = -/+ <const_int> - {cfa_store,cfa_temp}.offset

  Rule 13:
  (set (mem <reg1>:{cfa_store,cfa_temp}) <reg2>)
  effects: cfa.reg = <reg1>
	   cfa.base_offset = -{cfa_store,cfa_temp}.offset

  Rule 14:
  (set (mem (postinc <reg1>:cfa_temp <const_int>)) <reg2>)
  effects: cfa.reg = <reg1>
	   cfa.base_offset = -cfa_temp.offset
	   cfa_temp.offset -= mode_size(mem)

  Rule 15:
  (set <reg> {unspec, unspec_volatile})
  effects: target-dependent  */

static void
dwarf2out_frame_debug_expr (rtx expr, const char *label)
{
  rtx src, dest;
  HOST_WIDE_INT offset;

  /* If RTX_FRAME_RELATED_P is set on a PARALLEL, process each member of
     the PARALLEL independently. The first element is always processed if
     it is a SET. This is for backward compatibility.   Other elements
     are processed only if they are SETs and the RTX_FRAME_RELATED_P
     flag is set in them.  */
  if (GET_CODE (expr) == PARALLEL || GET_CODE (expr) == SEQUENCE)
    {
      int par_index;
      int limit = XVECLEN (expr, 0);

      for (par_index = 0; par_index < limit; par_index++)
	if (GET_CODE (XVECEXP (expr, 0, par_index)) == SET
	    && (RTX_FRAME_RELATED_P (XVECEXP (expr, 0, par_index))
		|| par_index == 0))
	  dwarf2out_frame_debug_expr (XVECEXP (expr, 0, par_index), label);

      return;
    }

  gcc_assert (GET_CODE (expr) == SET);

  src = SET_SRC (expr);
  dest = SET_DEST (expr);

  if (REG_P (src))
    {
      rtx rsi = reg_saved_in (src);
      if (rsi)
	src = rsi;
    }

  switch (GET_CODE (dest))
    {
    case REG:
      switch (GET_CODE (src))
	{
	  /* Setting FP from SP.  */
	case REG:
	  if (cfa.reg == (unsigned) REGNO (src))
	    {
	      /* Rule 1 */
	      /* Update the CFA rule wrt SP or FP.  Make sure src is
		 relative to the current CFA register.

		 We used to require that dest be either SP or FP, but the
		 ARM copies SP to a temporary register, and from there to
		 FP.  So we just rely on the backends to only set
		 RTX_FRAME_RELATED_P on appropriate insns.  */
	      cfa.reg = REGNO (dest);
	      cfa_temp.reg = cfa.reg;
	      cfa_temp.offset = cfa.offset;
	    }
	  else
	    {
	      /* Saving a register in a register.  */
	      gcc_assert (!fixed_regs [REGNO (dest)]
			  /* For the SPARC and its register window.  */
			  || (DWARF_FRAME_REGNUM (REGNO (src))
			      == DWARF_FRAME_RETURN_COLUMN));
	      queue_reg_save (label, src, dest, 0);
	    }
	  break;

	case PLUS:
	case MINUS:
	case LO_SUM:
	  if (dest == stack_pointer_rtx)
	    {
	      /* Rule 2 */
	      /* Adjusting SP.  */
	      switch (GET_CODE (XEXP (src, 1)))
		{
		case CONST_INT:
		  offset = INTVAL (XEXP (src, 1));
		  break;
		case REG:
		  gcc_assert ((unsigned) REGNO (XEXP (src, 1))
			      == cfa_temp.reg);
		  offset = cfa_temp.offset;
		  break;
		default:
		  gcc_unreachable ();
		}

	      if (XEXP (src, 0) == hard_frame_pointer_rtx)
		{
		  /* Restoring SP from FP in the epilogue.  */
		  gcc_assert (cfa.reg == (unsigned) HARD_FRAME_POINTER_REGNUM);
		  cfa.reg = STACK_POINTER_REGNUM;
		}
	      else if (GET_CODE (src) == LO_SUM)
		/* Assume we've set the source reg of the LO_SUM from sp.  */
		;
	      else
		gcc_assert (XEXP (src, 0) == stack_pointer_rtx);

	      if (GET_CODE (src) != MINUS)
		offset = -offset;
	      if (cfa.reg == STACK_POINTER_REGNUM)
		cfa.offset += offset;
	      if (cfa_store.reg == STACK_POINTER_REGNUM)
		cfa_store.offset += offset;
	    }
	  else if (dest == hard_frame_pointer_rtx)
	    {
	      /* Rule 3 */
	      /* Either setting the FP from an offset of the SP,
		 or adjusting the FP */
	      gcc_assert (frame_pointer_needed);

	      gcc_assert (REG_P (XEXP (src, 0))
			  && (unsigned) REGNO (XEXP (src, 0)) == cfa.reg
			  && GET_CODE (XEXP (src, 1)) == CONST_INT);
	      offset = INTVAL (XEXP (src, 1));
	      if (GET_CODE (src) != MINUS)
		offset = -offset;
	      cfa.offset += offset;
	      cfa.reg = HARD_FRAME_POINTER_REGNUM;
	    }
	  else
	    {
	      gcc_assert (GET_CODE (src) != MINUS);

	      /* Rule 4 */
	      if (REG_P (XEXP (src, 0))
		  && REGNO (XEXP (src, 0)) == cfa.reg
		  && GET_CODE (XEXP (src, 1)) == CONST_INT)
		{
		  /* Setting a temporary CFA register that will be copied
		     into the FP later on.  */
		  offset = - INTVAL (XEXP (src, 1));
		  cfa.offset += offset;
		  cfa.reg = REGNO (dest);
		  /* Or used to save regs to the stack.  */
		  cfa_temp.reg = cfa.reg;
		  cfa_temp.offset = cfa.offset;
		}

	      /* Rule 5 */
	      else if (REG_P (XEXP (src, 0))
		       && REGNO (XEXP (src, 0)) == cfa_temp.reg
		       && XEXP (src, 1) == stack_pointer_rtx)
		{
		  /* Setting a scratch register that we will use instead
		     of SP for saving registers to the stack.  */
		  gcc_assert (cfa.reg == STACK_POINTER_REGNUM);
		  cfa_store.reg = REGNO (dest);
		  cfa_store.offset = cfa.offset - cfa_temp.offset;
		}

	      /* Rule 9 */
	      else if (GET_CODE (src) == LO_SUM
		       && GET_CODE (XEXP (src, 1)) == CONST_INT)
		{
		  cfa_temp.reg = REGNO (dest);
		  cfa_temp.offset = INTVAL (XEXP (src, 1));
		}
	      else
		gcc_unreachable ();
	    }
	  break;

	  /* Rule 6 */
	case CONST_INT:
	  cfa_temp.reg = REGNO (dest);
	  cfa_temp.offset = INTVAL (src);
	  break;

	  /* Rule 7 */
	case IOR:
	  gcc_assert (REG_P (XEXP (src, 0))
		      && (unsigned) REGNO (XEXP (src, 0)) == cfa_temp.reg
		      && GET_CODE (XEXP (src, 1)) == CONST_INT);

	  if ((unsigned) REGNO (dest) != cfa_temp.reg)
	    cfa_temp.reg = REGNO (dest);
	  cfa_temp.offset |= INTVAL (XEXP (src, 1));
	  break;

	  /* Skip over HIGH, assuming it will be followed by a LO_SUM,
	     which will fill in all of the bits.  */
	  /* Rule 8 */
	case HIGH:
	  break;

	  /* Rule 15 */
	case UNSPEC:
	case UNSPEC_VOLATILE:
	  gcc_assert (targetm.dwarf_handle_frame_unspec);
	  targetm.dwarf_handle_frame_unspec (label, expr, XINT (src, 1));
	  return;

	default:
	  gcc_unreachable ();
	}

      def_cfa_1 (label, &cfa);
      break;

    case MEM:
      gcc_assert (REG_P (src));

      /* Saving a register to the stack.  Make sure dest is relative to the
	 CFA register.  */
      switch (GET_CODE (XEXP (dest, 0)))
	{
	  /* Rule 10 */
	  /* With a push.  */
	case PRE_MODIFY:
	  /* We can't handle variable size modifications.  */
	  gcc_assert (GET_CODE (XEXP (XEXP (XEXP (dest, 0), 1), 1))
		      == CONST_INT);
	  offset = -INTVAL (XEXP (XEXP (XEXP (dest, 0), 1), 1));

	  gcc_assert (REGNO (XEXP (XEXP (dest, 0), 0)) == STACK_POINTER_REGNUM
		      && cfa_store.reg == STACK_POINTER_REGNUM);

	  cfa_store.offset += offset;
	  if (cfa.reg == STACK_POINTER_REGNUM)
	    cfa.offset = cfa_store.offset;

	  offset = -cfa_store.offset;
	  break;

	  /* Rule 11 */
	case PRE_INC:
	case PRE_DEC:
	  offset = GET_MODE_SIZE (GET_MODE (dest));
	  if (GET_CODE (XEXP (dest, 0)) == PRE_INC)
	    offset = -offset;

	  gcc_assert (REGNO (XEXP (XEXP (dest, 0), 0)) == STACK_POINTER_REGNUM
		      && cfa_store.reg == STACK_POINTER_REGNUM);

	  cfa_store.offset += offset;
	  if (cfa.reg == STACK_POINTER_REGNUM)
	    cfa.offset = cfa_store.offset;

	  offset = -cfa_store.offset;
	  break;

	  /* Rule 12 */
	  /* With an offset.  */
	case PLUS:
	case MINUS:
	case LO_SUM:
	  {
	    int regno;

	    gcc_assert (GET_CODE (XEXP (XEXP (dest, 0), 1)) == CONST_INT
			&& REG_P (XEXP (XEXP (dest, 0), 0)));
	    offset = INTVAL (XEXP (XEXP (dest, 0), 1));
	    if (GET_CODE (XEXP (dest, 0)) == MINUS)
	      offset = -offset;

	    regno = REGNO (XEXP (XEXP (dest, 0), 0));

	    if (cfa_store.reg == (unsigned) regno)
	      offset -= cfa_store.offset;
	    else
	      {
		gcc_assert (cfa_temp.reg == (unsigned) regno);
		offset -= cfa_temp.offset;
	      }
	  }
	  break;

	  /* Rule 13 */
	  /* Without an offset.  */
	case REG:
	  {
	    int regno = REGNO (XEXP (dest, 0));

	    if (cfa_store.reg == (unsigned) regno)
	      offset = -cfa_store.offset;
	    else
	      {
		gcc_assert (cfa_temp.reg == (unsigned) regno);
		offset = -cfa_temp.offset;
	      }
	  }
	  break;

	  /* Rule 14 */
	case POST_INC:
	  gcc_assert (cfa_temp.reg
		      == (unsigned) REGNO (XEXP (XEXP (dest, 0), 0)));
	  offset = -cfa_temp.offset;
	  cfa_temp.offset -= GET_MODE_SIZE (GET_MODE (dest));
	  break;

	default:
	  gcc_unreachable ();
	}

      if (REGNO (src) != STACK_POINTER_REGNUM
	  && REGNO (src) != HARD_FRAME_POINTER_REGNUM
	  && (unsigned) REGNO (src) == cfa.reg)
	{
	  /* We're storing the current CFA reg into the stack.  */

	  if (cfa.offset == 0)
	    {
	      /* If the source register is exactly the CFA, assume
		 we're saving SP like any other register; this happens
		 on the ARM.  */
	      def_cfa_1 (label, &cfa);
	      queue_reg_save (label, stack_pointer_rtx, NULL_RTX, offset);
	      break;
	    }
	  else
	    {
	      /* Otherwise, we'll need to look in the stack to
		 calculate the CFA.  */
	      rtx x = XEXP (dest, 0);

	      if (!REG_P (x))
		x = XEXP (x, 0);
	      gcc_assert (REG_P (x));

	      cfa.reg = REGNO (x);
	      cfa.base_offset = offset;
	      cfa.indirect = 1;
	      def_cfa_1 (label, &cfa);
	      break;
	    }
	}

      def_cfa_1 (label, &cfa);
      queue_reg_save (label, src, NULL_RTX, offset);
      break;

    default:
      gcc_unreachable ();
    }
}

/* Record call frame debugging information for INSN, which either
   sets SP or FP (adjusting how we calculate the frame address) or saves a
   register to the stack.  If INSN is NULL_RTX, initialize our state.

   If AFTER_P is false, we're being called before the insn is emitted,
   otherwise after.  Call instructions get invoked twice.  */

void
dwarf2out_frame_debug (rtx insn, bool after_p)
{
  const char *label;
  rtx src;

  if (insn == NULL_RTX)
    {
      size_t i;

      /* Flush any queued register saves.  */
      flush_queued_reg_saves ();

      /* Set up state for generating call frame debug info.  */
      lookup_cfa (&cfa);
      gcc_assert (cfa.reg
		  == (unsigned long)DWARF_FRAME_REGNUM (STACK_POINTER_REGNUM));

      cfa.reg = STACK_POINTER_REGNUM;
      cfa_store = cfa;
      cfa_temp.reg = -1;
      cfa_temp.offset = 0;

      for (i = 0; i < num_regs_saved_in_regs; i++)
	{
	  regs_saved_in_regs[i].orig_reg = NULL_RTX;
	  regs_saved_in_regs[i].saved_in_reg = NULL_RTX;
	}
      num_regs_saved_in_regs = 0;
      return;
    }

  if (!NONJUMP_INSN_P (insn) || clobbers_queued_reg_save (insn))
    flush_queued_reg_saves ();

  if (! RTX_FRAME_RELATED_P (insn))
    {
      if (!ACCUMULATE_OUTGOING_ARGS)
	dwarf2out_stack_adjust (insn, after_p);
      return;
    }

  label = dwarf2out_cfi_label ();
  src = find_reg_note (insn, REG_FRAME_RELATED_EXPR, NULL_RTX);
  if (src)
    insn = XEXP (src, 0);
  else
    insn = PATTERN (insn);

  dwarf2out_frame_debug_expr (insn, label);
}

#endif

/* Describe for the GTY machinery what parts of dw_cfi_oprnd1 are used.  */
static enum dw_cfi_oprnd_type dw_cfi_oprnd1_desc
 (enum dwarf_call_frame_info cfi);

static enum dw_cfi_oprnd_type
dw_cfi_oprnd1_desc (enum dwarf_call_frame_info cfi)
{
  switch (cfi)
    {
    case DW_CFA_nop:
    case DW_CFA_GNU_window_save:
      return dw_cfi_oprnd_unused;

    case DW_CFA_set_loc:
    case DW_CFA_advance_loc1:
    case DW_CFA_advance_loc2:
    case DW_CFA_advance_loc4:
    case DW_CFA_MIPS_advance_loc8:
      return dw_cfi_oprnd_addr;

    case DW_CFA_offset:
    case DW_CFA_offset_extended:
    case DW_CFA_def_cfa:
    case DW_CFA_offset_extended_sf:
    case DW_CFA_def_cfa_sf:
    case DW_CFA_restore_extended:
    case DW_CFA_undefined:
    case DW_CFA_same_value:
    case DW_CFA_def_cfa_register:
    case DW_CFA_register:
      return dw_cfi_oprnd_reg_num;

    case DW_CFA_def_cfa_offset:
    case DW_CFA_GNU_args_size:
    case DW_CFA_def_cfa_offset_sf:
      return dw_cfi_oprnd_offset;

    case DW_CFA_def_cfa_expression:
    case DW_CFA_expression:
      return dw_cfi_oprnd_loc;

    default:
      gcc_unreachable ();
    }
}

/* Describe for the GTY machinery what parts of dw_cfi_oprnd2 are used.  */
static enum dw_cfi_oprnd_type dw_cfi_oprnd2_desc
 (enum dwarf_call_frame_info cfi);

static enum dw_cfi_oprnd_type
dw_cfi_oprnd2_desc (enum dwarf_call_frame_info cfi)
{
  switch (cfi)
    {
    case DW_CFA_def_cfa:
    case DW_CFA_def_cfa_sf:
    case DW_CFA_offset:
    case DW_CFA_offset_extended_sf:
    case DW_CFA_offset_extended:
      return dw_cfi_oprnd_offset;

    case DW_CFA_register:
      return dw_cfi_oprnd_reg_num;

    default:
      return dw_cfi_oprnd_unused;
    }
}

#if defined (DWARF2_DEBUGGING_INFO) || defined (DWARF2_UNWIND_INFO)

/* Switch to eh_frame_section.  If we don't have an eh_frame_section,
   switch to the data section instead, and write out a synthetic label
   for collect2.  */

static void
switch_to_eh_frame_section (void)
{
  tree label;

#ifdef EH_FRAME_SECTION_NAME
  if (eh_frame_section == 0)
    {
      int flags;

      if (EH_TABLES_CAN_BE_READ_ONLY)
	{
	  int fde_encoding;
	  int per_encoding;
	  int lsda_encoding;

	  fde_encoding = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/1,
						       /*global=*/0);
	  per_encoding = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/2,
						       /*global=*/1);
	  lsda_encoding = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/0,
							/*global=*/0);
	  flags = ((! flag_pic
		    || ((fde_encoding & 0x70) != DW_EH_PE_absptr
			&& (fde_encoding & 0x70) != DW_EH_PE_aligned
			&& (per_encoding & 0x70) != DW_EH_PE_absptr
			&& (per_encoding & 0x70) != DW_EH_PE_aligned
			&& (lsda_encoding & 0x70) != DW_EH_PE_absptr
			&& (lsda_encoding & 0x70) != DW_EH_PE_aligned))
		   ? 0 : SECTION_WRITE);
	}
      else
	flags = SECTION_WRITE;
      eh_frame_section = get_section (EH_FRAME_SECTION_NAME, flags, NULL);
    }
#endif

  if (eh_frame_section)
    switch_to_section (eh_frame_section);
  else
    {
      /* We have no special eh_frame section.  Put the information in
	 the data section and emit special labels to guide collect2.  */
      switch_to_section (data_section);
      label = get_file_function_name ('F');
      ASM_OUTPUT_ALIGN (asm_out_file, floor_log2 (PTR_SIZE));
      targetm.asm_out.globalize_label (asm_out_file,
				       IDENTIFIER_POINTER (label));
      ASM_OUTPUT_LABEL (asm_out_file, IDENTIFIER_POINTER (label));
    }
}

/* Output a Call Frame Information opcode and its operand(s).  */

static void
output_cfi (dw_cfi_ref cfi, dw_fde_ref fde, int for_eh)
{
  unsigned long r;
  if (cfi->dw_cfi_opc == DW_CFA_advance_loc)
    dw2_asm_output_data (1, (cfi->dw_cfi_opc
			     | (cfi->dw_cfi_oprnd1.dw_cfi_offset & 0x3f)),
			 "DW_CFA_advance_loc " HOST_WIDE_INT_PRINT_HEX,
			 cfi->dw_cfi_oprnd1.dw_cfi_offset);
  else if (cfi->dw_cfi_opc == DW_CFA_offset)
    {
      r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd1.dw_cfi_reg_num, for_eh);
      dw2_asm_output_data (1, (cfi->dw_cfi_opc | (r & 0x3f)),
			   "DW_CFA_offset, column 0x%lx", r);
      dw2_asm_output_data_uleb128 (cfi->dw_cfi_oprnd2.dw_cfi_offset, NULL);
    }
  else if (cfi->dw_cfi_opc == DW_CFA_restore)
    {
      r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd1.dw_cfi_reg_num, for_eh);
      dw2_asm_output_data (1, (cfi->dw_cfi_opc | (r & 0x3f)),
			   "DW_CFA_restore, column 0x%lx", r);
    }
  else
    {
      dw2_asm_output_data (1, cfi->dw_cfi_opc,
			   "%s", dwarf_cfi_name (cfi->dw_cfi_opc));

      switch (cfi->dw_cfi_opc)
	{
	case DW_CFA_set_loc:
	  if (for_eh)
	    dw2_asm_output_encoded_addr_rtx (
		ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/1, /*global=*/0),
		gen_rtx_SYMBOL_REF (Pmode, cfi->dw_cfi_oprnd1.dw_cfi_addr),
		false, NULL);
	  else
	    dw2_asm_output_addr (DWARF2_ADDR_SIZE,
				 cfi->dw_cfi_oprnd1.dw_cfi_addr, NULL);
	  fde->dw_fde_current_label = cfi->dw_cfi_oprnd1.dw_cfi_addr;
	  break;

	case DW_CFA_advance_loc1:
	  dw2_asm_output_delta (1, cfi->dw_cfi_oprnd1.dw_cfi_addr,
				fde->dw_fde_current_label, NULL);
	  fde->dw_fde_current_label = cfi->dw_cfi_oprnd1.dw_cfi_addr;
	  break;

	case DW_CFA_advance_loc2:
	  dw2_asm_output_delta (2, cfi->dw_cfi_oprnd1.dw_cfi_addr,
				fde->dw_fde_current_label, NULL);
	  fde->dw_fde_current_label = cfi->dw_cfi_oprnd1.dw_cfi_addr;
	  break;

	case DW_CFA_advance_loc4:
	  dw2_asm_output_delta (4, cfi->dw_cfi_oprnd1.dw_cfi_addr,
				fde->dw_fde_current_label, NULL);
	  fde->dw_fde_current_label = cfi->dw_cfi_oprnd1.dw_cfi_addr;
	  break;

	case DW_CFA_MIPS_advance_loc8:
	  dw2_asm_output_delta (8, cfi->dw_cfi_oprnd1.dw_cfi_addr,
				fde->dw_fde_current_label, NULL);
	  fde->dw_fde_current_label = cfi->dw_cfi_oprnd1.dw_cfi_addr;
	  break;

	case DW_CFA_offset_extended:
	case DW_CFA_def_cfa:
	  r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd1.dw_cfi_reg_num, for_eh);
	  dw2_asm_output_data_uleb128 (r, NULL);
	  dw2_asm_output_data_uleb128 (cfi->dw_cfi_oprnd2.dw_cfi_offset, NULL);
	  break;

	case DW_CFA_offset_extended_sf:
	case DW_CFA_def_cfa_sf:
	  r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd1.dw_cfi_reg_num, for_eh);
	  dw2_asm_output_data_uleb128 (r, NULL);
	  dw2_asm_output_data_sleb128 (cfi->dw_cfi_oprnd2.dw_cfi_offset, NULL);
	  break;

	case DW_CFA_restore_extended:
	case DW_CFA_undefined:
	case DW_CFA_same_value:
	case DW_CFA_def_cfa_register:
	  r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd1.dw_cfi_reg_num, for_eh);
	  dw2_asm_output_data_uleb128 (r, NULL);
	  break;

	case DW_CFA_register:
	  r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd1.dw_cfi_reg_num, for_eh);
	  dw2_asm_output_data_uleb128 (r, NULL);
	  r = DWARF2_FRAME_REG_OUT (cfi->dw_cfi_oprnd2.dw_cfi_reg_num, for_eh);
	  dw2_asm_output_data_uleb128 (r, NULL);
	  break;

	case DW_CFA_def_cfa_offset:
	case DW_CFA_GNU_args_size:
	  dw2_asm_output_data_uleb128 (cfi->dw_cfi_oprnd1.dw_cfi_offset, NULL);
	  break;

	case DW_CFA_def_cfa_offset_sf:
	  dw2_asm_output_data_sleb128 (cfi->dw_cfi_oprnd1.dw_cfi_offset, NULL);
	  break;

	case DW_CFA_GNU_window_save:
	  break;

	case DW_CFA_def_cfa_expression:
	case DW_CFA_expression:
	  output_cfa_loc (cfi);
	  break;

	case DW_CFA_GNU_negative_offset_extended:
	  /* Obsoleted by DW_CFA_offset_extended_sf.  */
	  gcc_unreachable ();

	default:
	  break;
	}
    }
}

/* Output the call frame information used to record information
   that relates to calculating the frame pointer, and records the
   location of saved registers.  */

static void
output_call_frame_info (int for_eh)
{
  unsigned int i;
  dw_fde_ref fde;
  dw_cfi_ref cfi;
  char l1[20], l2[20], section_start_label[20];
  bool any_lsda_needed = false;
  char augmentation[6];
  int augmentation_size;
  int fde_encoding = DW_EH_PE_absptr;
  int per_encoding = DW_EH_PE_absptr;
  int lsda_encoding = DW_EH_PE_absptr;
  int return_reg;

  /* Don't emit a CIE if there won't be any FDEs.  */
  if (fde_table_in_use == 0)
    return;

  /* If we make FDEs linkonce, we may have to emit an empty label for
     an FDE that wouldn't otherwise be emitted.  We want to avoid
     having an FDE kept around when the function it refers to is
     discarded.  Example where this matters: a primary function
     template in C++ requires EH information, but an explicit
     specialization doesn't.  */
  if (TARGET_USES_WEAK_UNWIND_INFO
      && ! flag_asynchronous_unwind_tables
      && for_eh)
    for (i = 0; i < fde_table_in_use; i++)
      if ((fde_table[i].nothrow || fde_table[i].all_throwers_are_sibcalls)
          && !fde_table[i].uses_eh_lsda
	  && ! DECL_WEAK (fde_table[i].decl))
	targetm.asm_out.unwind_label (asm_out_file, fde_table[i].decl,
				      for_eh, /* empty */ 1);

  /* If we don't have any functions we'll want to unwind out of, don't
     emit any EH unwind information.  Note that if exceptions aren't
     enabled, we won't have collected nothrow information, and if we
     asked for asynchronous tables, we always want this info.  */
  if (for_eh)
    {
      bool any_eh_needed = !flag_exceptions || flag_asynchronous_unwind_tables;

      for (i = 0; i < fde_table_in_use; i++)
	if (fde_table[i].uses_eh_lsda)
	  any_eh_needed = any_lsda_needed = true;
        else if (TARGET_USES_WEAK_UNWIND_INFO && DECL_WEAK (fde_table[i].decl))
	  any_eh_needed = true;
	else if (! fde_table[i].nothrow
		 && ! fde_table[i].all_throwers_are_sibcalls)
	  any_eh_needed = true;

      if (! any_eh_needed)
	return;
    }

  /* We're going to be generating comments, so turn on app.  */
  if (flag_debug_asm)
    app_enable ();

  if (for_eh)
    switch_to_eh_frame_section ();
  else
    {
      if (!debug_frame_section)
	debug_frame_section = get_section (DEBUG_FRAME_SECTION,
					   SECTION_DEBUG, NULL);
      switch_to_section (debug_frame_section);
    }

  ASM_GENERATE_INTERNAL_LABEL (section_start_label, FRAME_BEGIN_LABEL, for_eh);
  ASM_OUTPUT_LABEL (asm_out_file, section_start_label);

  /* Output the CIE.  */
  ASM_GENERATE_INTERNAL_LABEL (l1, CIE_AFTER_SIZE_LABEL, for_eh);
  ASM_GENERATE_INTERNAL_LABEL (l2, CIE_END_LABEL, for_eh);
  if (DWARF_INITIAL_LENGTH_SIZE - DWARF_OFFSET_SIZE == 4 && !for_eh)
    dw2_asm_output_data (4, 0xffffffff,
      "Initial length escape value indicating 64-bit DWARF extension");
  dw2_asm_output_delta (for_eh ? 4 : DWARF_OFFSET_SIZE, l2, l1,
			"Length of Common Information Entry");
  ASM_OUTPUT_LABEL (asm_out_file, l1);

  /* Now that the CIE pointer is PC-relative for EH,
     use 0 to identify the CIE.  */
  dw2_asm_output_data ((for_eh ? 4 : DWARF_OFFSET_SIZE),
		       (for_eh ? 0 : DWARF_CIE_ID),
		       "CIE Identifier Tag");

  dw2_asm_output_data (1, DW_CIE_VERSION, "CIE Version");

  augmentation[0] = 0;
  augmentation_size = 0;
  if (for_eh)
    {
      char *p;

      /* Augmentation:
	 z	Indicates that a uleb128 is present to size the
		augmentation section.
	 L	Indicates the encoding (and thus presence) of
		an LSDA pointer in the FDE augmentation.
	 R	Indicates a non-default pointer encoding for
		FDE code pointers.
	 P	Indicates the presence of an encoding + language
		personality routine in the CIE augmentation.  */

      fde_encoding = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/1, /*global=*/0);
      per_encoding = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/2, /*global=*/1);
      lsda_encoding = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/0, /*global=*/0);

      p = augmentation + 1;
      if (eh_personality_libfunc)
	{
	  *p++ = 'P';
	  augmentation_size += 1 + size_of_encoded_value (per_encoding);
	}
      if (any_lsda_needed)
	{
	  *p++ = 'L';
	  augmentation_size += 1;
	}
      if (fde_encoding != DW_EH_PE_absptr)
	{
	  *p++ = 'R';
	  augmentation_size += 1;
	}
      if (p > augmentation + 1)
	{
	  augmentation[0] = 'z';
	  *p = '\0';
	}

      /* Ug.  Some platforms can't do unaligned dynamic relocations at all.  */
      if (eh_personality_libfunc && per_encoding == DW_EH_PE_aligned)
	{
	  int offset = (  4		/* Length */
			+ 4		/* CIE Id */
			+ 1		/* CIE version */
			+ strlen (augmentation) + 1	/* Augmentation */
			+ size_of_uleb128 (1)		/* Code alignment */
			+ size_of_sleb128 (DWARF_CIE_DATA_ALIGNMENT)
			+ 1		/* RA column */
			+ 1		/* Augmentation size */
			+ 1		/* Personality encoding */ );
	  int pad = -offset & (PTR_SIZE - 1);

	  augmentation_size += pad;

	  /* Augmentations should be small, so there's scarce need to
	     iterate for a solution.  Die if we exceed one uleb128 byte.  */
	  gcc_assert (size_of_uleb128 (augmentation_size) == 1);
	}
    }

  dw2_asm_output_nstring (augmentation, -1, "CIE Augmentation");
  dw2_asm_output_data_uleb128 (1, "CIE Code Alignment Factor");
  dw2_asm_output_data_sleb128 (DWARF_CIE_DATA_ALIGNMENT,
			       "CIE Data Alignment Factor");

  return_reg = DWARF2_FRAME_REG_OUT (DWARF_FRAME_RETURN_COLUMN, for_eh);
  if (DW_CIE_VERSION == 1)
    dw2_asm_output_data (1, return_reg, "CIE RA Column");
  else
    dw2_asm_output_data_uleb128 (return_reg, "CIE RA Column");

  if (augmentation[0])
    {
      dw2_asm_output_data_uleb128 (augmentation_size, "Augmentation size");
      if (eh_personality_libfunc)
	{
	  dw2_asm_output_data (1, per_encoding, "Personality (%s)",
			       eh_data_format_name (per_encoding));
	  dw2_asm_output_encoded_addr_rtx (per_encoding,
					   eh_personality_libfunc,
					   true, NULL);
	}

      if (any_lsda_needed)
	dw2_asm_output_data (1, lsda_encoding, "LSDA Encoding (%s)",
			     eh_data_format_name (lsda_encoding));

      if (fde_encoding != DW_EH_PE_absptr)
	dw2_asm_output_data (1, fde_encoding, "FDE Encoding (%s)",
			     eh_data_format_name (fde_encoding));
    }

  for (cfi = cie_cfi_head; cfi != NULL; cfi = cfi->dw_cfi_next)
    output_cfi (cfi, NULL, for_eh);

  /* Pad the CIE out to an address sized boundary.  */
  ASM_OUTPUT_ALIGN (asm_out_file,
		    floor_log2 (for_eh ? PTR_SIZE : DWARF2_ADDR_SIZE));
  ASM_OUTPUT_LABEL (asm_out_file, l2);

  /* Loop through all of the FDE's.  */
  for (i = 0; i < fde_table_in_use; i++)
    {
      fde = &fde_table[i];

      /* Don't emit EH unwind info for leaf functions that don't need it.  */
      if (for_eh && !flag_asynchronous_unwind_tables && flag_exceptions
	  && (fde->nothrow || fde->all_throwers_are_sibcalls)
	  && ! (TARGET_USES_WEAK_UNWIND_INFO && DECL_WEAK (fde_table[i].decl))
	  && !fde->uses_eh_lsda)
	continue;

      targetm.asm_out.unwind_label (asm_out_file, fde->decl, for_eh, /* empty */ 0);
      targetm.asm_out.internal_label (asm_out_file, FDE_LABEL, for_eh + i * 2);
      ASM_GENERATE_INTERNAL_LABEL (l1, FDE_AFTER_SIZE_LABEL, for_eh + i * 2);
      ASM_GENERATE_INTERNAL_LABEL (l2, FDE_END_LABEL, for_eh + i * 2);
      if (DWARF_INITIAL_LENGTH_SIZE - DWARF_OFFSET_SIZE == 4 && !for_eh)
	dw2_asm_output_data (4, 0xffffffff,
			     "Initial length escape value indicating 64-bit DWARF extension");
      dw2_asm_output_delta (for_eh ? 4 : DWARF_OFFSET_SIZE, l2, l1,
			    "FDE Length");
      ASM_OUTPUT_LABEL (asm_out_file, l1);

      if (for_eh)
	dw2_asm_output_delta (4, l1, section_start_label, "FDE CIE offset");
      else
	dw2_asm_output_offset (DWARF_OFFSET_SIZE, section_start_label,
			       debug_frame_section, "FDE CIE offset");

      if (for_eh)
	{
	  rtx sym_ref = gen_rtx_SYMBOL_REF (Pmode, fde->dw_fde_begin);
	  SYMBOL_REF_FLAGS (sym_ref) |= SYMBOL_FLAG_LOCAL;
	  dw2_asm_output_encoded_addr_rtx (fde_encoding,
					   sym_ref,
					   false,
					   "FDE initial location");
	  if (fde->dw_fde_switched_sections)
	    {
	      rtx sym_ref2 = gen_rtx_SYMBOL_REF (Pmode, 
				      fde->dw_fde_unlikely_section_label);
	      rtx sym_ref3= gen_rtx_SYMBOL_REF (Pmode, 
				      fde->dw_fde_hot_section_label);
	      SYMBOL_REF_FLAGS (sym_ref2) |= SYMBOL_FLAG_LOCAL;
	      SYMBOL_REF_FLAGS (sym_ref3) |= SYMBOL_FLAG_LOCAL;
	      dw2_asm_output_encoded_addr_rtx (fde_encoding, sym_ref3, false,
					       "FDE initial location");
	      dw2_asm_output_delta (size_of_encoded_value (fde_encoding),
				    fde->dw_fde_hot_section_end_label,
				    fde->dw_fde_hot_section_label,
				    "FDE address range");
	      dw2_asm_output_encoded_addr_rtx (fde_encoding, sym_ref2, false,
					       "FDE initial location");
	      dw2_asm_output_delta (size_of_encoded_value (fde_encoding),
				    fde->dw_fde_unlikely_section_end_label,
				    fde->dw_fde_unlikely_section_label,
				    "FDE address range");
	    }
	  else
	    dw2_asm_output_delta (size_of_encoded_value (fde_encoding),
				  fde->dw_fde_end, fde->dw_fde_begin,
				  "FDE address range");
	}
      else
	{
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, fde->dw_fde_begin,
			       "FDE initial location");
	  if (fde->dw_fde_switched_sections)
	    {
	      dw2_asm_output_addr (DWARF2_ADDR_SIZE,
				   fde->dw_fde_hot_section_label,
				   "FDE initial location");
	      dw2_asm_output_delta (DWARF2_ADDR_SIZE,
				    fde->dw_fde_hot_section_end_label,
				    fde->dw_fde_hot_section_label,
				    "FDE address range");
	      dw2_asm_output_addr (DWARF2_ADDR_SIZE,
				   fde->dw_fde_unlikely_section_label,
				   "FDE initial location");
	      dw2_asm_output_delta (DWARF2_ADDR_SIZE, 
				    fde->dw_fde_unlikely_section_end_label,
				    fde->dw_fde_unlikely_section_label,
				    "FDE address range");
	    }
	  else
	    dw2_asm_output_delta (DWARF2_ADDR_SIZE,
				  fde->dw_fde_end, fde->dw_fde_begin,
				  "FDE address range");
	}

      if (augmentation[0])
	{
	  if (any_lsda_needed)
	    {
	      int size = size_of_encoded_value (lsda_encoding);

	      if (lsda_encoding == DW_EH_PE_aligned)
		{
		  int offset = (  4		/* Length */
				+ 4		/* CIE offset */
				+ 2 * size_of_encoded_value (fde_encoding)
				+ 1		/* Augmentation size */ );
		  int pad = -offset & (PTR_SIZE - 1);

		  size += pad;
		  gcc_assert (size_of_uleb128 (size) == 1);
		}

	      dw2_asm_output_data_uleb128 (size, "Augmentation size");

	      if (fde->uses_eh_lsda)
		{
		  ASM_GENERATE_INTERNAL_LABEL (l1, "LLSDA",
					       fde->funcdef_number);
		  dw2_asm_output_encoded_addr_rtx (
			lsda_encoding, gen_rtx_SYMBOL_REF (Pmode, l1),
			false, "Language Specific Data Area");
		}
	      else
		{
		  if (lsda_encoding == DW_EH_PE_aligned)
		    ASM_OUTPUT_ALIGN (asm_out_file, floor_log2 (PTR_SIZE));
		  dw2_asm_output_data
		    (size_of_encoded_value (lsda_encoding), 0,
		     "Language Specific Data Area (none)");
		}
	    }
	  else
	    dw2_asm_output_data_uleb128 (0, "Augmentation size");
	}

      /* Loop through the Call Frame Instructions associated with
	 this FDE.  */
      fde->dw_fde_current_label = fde->dw_fde_begin;
      for (cfi = fde->dw_fde_cfi; cfi != NULL; cfi = cfi->dw_cfi_next)
	output_cfi (cfi, fde, for_eh);

      /* Pad the FDE out to an address sized boundary.  */
      ASM_OUTPUT_ALIGN (asm_out_file,
			floor_log2 ((for_eh ? PTR_SIZE : DWARF2_ADDR_SIZE)));
      ASM_OUTPUT_LABEL (asm_out_file, l2);
    }

  if (for_eh && targetm.terminate_dw2_eh_frame_info)
    dw2_asm_output_data (4, 0, "End of Table");
#ifdef MIPS_DEBUGGING_INFO
  /* Work around Irix 6 assembler bug whereby labels at the end of a section
     get a value of 0.  Putting .align 0 after the label fixes it.  */
  ASM_OUTPUT_ALIGN (asm_out_file, 0);
#endif

  /* Turn off app to make assembly quicker.  */
  if (flag_debug_asm)
    app_disable ();
}

/* Output a marker (i.e. a label) for the beginning of a function, before
   the prologue.  */

void
dwarf2out_begin_prologue (unsigned int line ATTRIBUTE_UNUSED,
			  const char *file ATTRIBUTE_UNUSED)
{
  char label[MAX_ARTIFICIAL_LABEL_BYTES];
  char * dup_label;
  dw_fde_ref fde;

  current_function_func_begin_label = NULL;

#ifdef TARGET_UNWIND_INFO
  /* ??? current_function_func_begin_label is also used by except.c
     for call-site information.  We must emit this label if it might
     be used.  */
  if ((! flag_exceptions || USING_SJLJ_EXCEPTIONS)
      && ! dwarf2out_do_frame ())
    return;
#else
  if (! dwarf2out_do_frame ())
    return;
#endif

  switch_to_section (function_section (current_function_decl));
  ASM_GENERATE_INTERNAL_LABEL (label, FUNC_BEGIN_LABEL,
			       current_function_funcdef_no);
  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, FUNC_BEGIN_LABEL,
			  current_function_funcdef_no);
  dup_label = xstrdup (label);
  current_function_func_begin_label = dup_label;

#ifdef TARGET_UNWIND_INFO
  /* We can elide the fde allocation if we're not emitting debug info.  */
  if (! dwarf2out_do_frame ())
    return;
#endif

  /* Expand the fde table if necessary.  */
  if (fde_table_in_use == fde_table_allocated)
    {
      fde_table_allocated += FDE_TABLE_INCREMENT;
      fde_table = ggc_realloc (fde_table,
			       fde_table_allocated * sizeof (dw_fde_node));
      memset (fde_table + fde_table_in_use, 0,
	      FDE_TABLE_INCREMENT * sizeof (dw_fde_node));
    }

  /* Record the FDE associated with this function.  */
  current_funcdef_fde = fde_table_in_use;

  /* Add the new FDE at the end of the fde_table.  */
  fde = &fde_table[fde_table_in_use++];
  fde->decl = current_function_decl;
  fde->dw_fde_begin = dup_label;
  fde->dw_fde_current_label = dup_label;
  fde->dw_fde_hot_section_label = NULL;
  fde->dw_fde_hot_section_end_label = NULL;
  fde->dw_fde_unlikely_section_label = NULL;
  fde->dw_fde_unlikely_section_end_label = NULL;
  fde->dw_fde_switched_sections = false;
  fde->dw_fde_end = NULL;
  fde->dw_fde_cfi = NULL;
  fde->funcdef_number = current_function_funcdef_no;
  fde->nothrow = TREE_NOTHROW (current_function_decl);
  fde->uses_eh_lsda = cfun->uses_eh_lsda;
  fde->all_throwers_are_sibcalls = cfun->all_throwers_are_sibcalls;

  args_size = old_args_size = 0;

  /* We only want to output line number information for the genuine dwarf2
     prologue case, not the eh frame case.  */
#ifdef DWARF2_DEBUGGING_INFO
  if (file)
    dwarf2out_source_line (line, file);
#endif
}

/* Output a marker (i.e. a label) for the absolute end of the generated code
   for a function definition.  This gets called *after* the epilogue code has
   been generated.  */

void
dwarf2out_end_epilogue (unsigned int line ATTRIBUTE_UNUSED,
			const char *file ATTRIBUTE_UNUSED)
{
  dw_fde_ref fde;
  char label[MAX_ARTIFICIAL_LABEL_BYTES];

  /* Output a label to mark the endpoint of the code generated for this
     function.  */
  ASM_GENERATE_INTERNAL_LABEL (label, FUNC_END_LABEL,
			       current_function_funcdef_no);
  ASM_OUTPUT_LABEL (asm_out_file, label);
  fde = &fde_table[fde_table_in_use - 1];
  fde->dw_fde_end = xstrdup (label);
}

void
dwarf2out_frame_init (void)
{
  /* Allocate the initial hunk of the fde_table.  */
  fde_table = ggc_alloc_cleared (FDE_TABLE_INCREMENT * sizeof (dw_fde_node));
  fde_table_allocated = FDE_TABLE_INCREMENT;
  fde_table_in_use = 0;

  /* Generate the CFA instructions common to all FDE's.  Do it now for the
     sake of lookup_cfa.  */

  /* On entry, the Canonical Frame Address is at SP.  */
  dwarf2out_def_cfa (NULL, STACK_POINTER_REGNUM, INCOMING_FRAME_SP_OFFSET);

#ifdef DWARF2_UNWIND_INFO
  if (DWARF2_UNWIND_INFO)
    initial_return_save (INCOMING_RETURN_ADDR_RTX);
#endif
}

void
dwarf2out_frame_finish (void)
{
  /* Output call frame information.  */
  if (DWARF2_FRAME_INFO)
    output_call_frame_info (0);

#ifndef TARGET_UNWIND_INFO
  /* Output another copy for the unwinder.  */
  if (! USING_SJLJ_EXCEPTIONS && (flag_unwind_tables || flag_exceptions))
    output_call_frame_info (1);
#endif
}
#endif

/* And now, the subset of the debugging information support code necessary
   for emitting location expressions.  */

/* Data about a single source file.  */
struct dwarf_file_data GTY(())
{
  const char * filename;
  int emitted_number;
};

/* We need some way to distinguish DW_OP_addr with a direct symbol
   relocation from DW_OP_addr with a dtp-relative symbol relocation.  */
#define INTERNAL_DW_OP_tls_addr		(0x100 + DW_OP_addr)


typedef struct dw_val_struct *dw_val_ref;
typedef struct die_struct *dw_die_ref;
typedef struct dw_loc_descr_struct *dw_loc_descr_ref;
typedef struct dw_loc_list_struct *dw_loc_list_ref;

/* Each DIE may have a series of attribute/value pairs.  Values
   can take on several forms.  The forms that are used in this
   implementation are listed below.  */

enum dw_val_class
{
  dw_val_class_addr,
  dw_val_class_offset,
  dw_val_class_loc,
  dw_val_class_loc_list,
  dw_val_class_range_list,
  dw_val_class_const,
  dw_val_class_unsigned_const,
  dw_val_class_long_long,
  dw_val_class_vec,
  dw_val_class_flag,
  dw_val_class_die_ref,
  dw_val_class_fde_ref,
  dw_val_class_lbl_id,
  dw_val_class_lineptr,
  dw_val_class_str,
  dw_val_class_macptr,
  dw_val_class_file
};

/* Describe a double word constant value.  */
/* ??? Every instance of long_long in the code really means CONST_DOUBLE.  */

typedef struct dw_long_long_struct GTY(())
{
  unsigned long hi;
  unsigned long low;
}
dw_long_long_const;

/* Describe a floating point constant value, or a vector constant value.  */

typedef struct dw_vec_struct GTY(())
{
  unsigned char * GTY((length ("%h.length"))) array;
  unsigned length;
  unsigned elt_size;
}
dw_vec_const;

/* The dw_val_node describes an attribute's value, as it is
   represented internally.  */

typedef struct dw_val_struct GTY(())
{
  enum dw_val_class val_class;
  union dw_val_struct_union
    {
      rtx GTY ((tag ("dw_val_class_addr"))) val_addr;
      unsigned HOST_WIDE_INT GTY ((tag ("dw_val_class_offset"))) val_offset;
      dw_loc_list_ref GTY ((tag ("dw_val_class_loc_list"))) val_loc_list;
      dw_loc_descr_ref GTY ((tag ("dw_val_class_loc"))) val_loc;
      HOST_WIDE_INT GTY ((default)) val_int;
      unsigned HOST_WIDE_INT GTY ((tag ("dw_val_class_unsigned_const"))) val_unsigned;
      dw_long_long_const GTY ((tag ("dw_val_class_long_long"))) val_long_long;
      dw_vec_const GTY ((tag ("dw_val_class_vec"))) val_vec;
      struct dw_val_die_union
	{
	  dw_die_ref die;
	  int external;
	} GTY ((tag ("dw_val_class_die_ref"))) val_die_ref;
      unsigned GTY ((tag ("dw_val_class_fde_ref"))) val_fde_index;
      struct indirect_string_node * GTY ((tag ("dw_val_class_str"))) val_str;
      char * GTY ((tag ("dw_val_class_lbl_id"))) val_lbl_id;
      unsigned char GTY ((tag ("dw_val_class_flag"))) val_flag;
      struct dwarf_file_data * GTY ((tag ("dw_val_class_file"))) val_file;
    }
  GTY ((desc ("%1.val_class"))) v;
}
dw_val_node;

/* Locations in memory are described using a sequence of stack machine
   operations.  */

typedef struct dw_loc_descr_struct GTY(())
{
  dw_loc_descr_ref dw_loc_next;
  enum dwarf_location_atom dw_loc_opc;
  dw_val_node dw_loc_oprnd1;
  dw_val_node dw_loc_oprnd2;
  int dw_loc_addr;
}
dw_loc_descr_node;

/* Location lists are ranges + location descriptions for that range,
   so you can track variables that are in different places over
   their entire life.  */
typedef struct dw_loc_list_struct GTY(())
{
  dw_loc_list_ref dw_loc_next;
  const char *begin; /* Label for begin address of range */
  const char *end;  /* Label for end address of range */
  char *ll_symbol; /* Label for beginning of location list.
		      Only on head of list */
  const char *section; /* Section this loclist is relative to */
  dw_loc_descr_ref expr;
} dw_loc_list_node;

#if defined (DWARF2_DEBUGGING_INFO) || defined (DWARF2_UNWIND_INFO)

static const char *dwarf_stack_op_name (unsigned);
static dw_loc_descr_ref new_loc_descr (enum dwarf_location_atom,
				       unsigned HOST_WIDE_INT, unsigned HOST_WIDE_INT);
static void add_loc_descr (dw_loc_descr_ref *, dw_loc_descr_ref);
static unsigned long size_of_loc_descr (dw_loc_descr_ref);
static unsigned long size_of_locs (dw_loc_descr_ref);
static void output_loc_operands (dw_loc_descr_ref);
static void output_loc_sequence (dw_loc_descr_ref);

/* Convert a DWARF stack opcode into its string name.  */

static const char *
dwarf_stack_op_name (unsigned int op)
{
  switch (op)
    {
    case DW_OP_addr:
    case INTERNAL_DW_OP_tls_addr:
      return "DW_OP_addr";
    case DW_OP_deref:
      return "DW_OP_deref";
    case DW_OP_const1u:
      return "DW_OP_const1u";
    case DW_OP_const1s:
      return "DW_OP_const1s";
    case DW_OP_const2u:
      return "DW_OP_const2u";
    case DW_OP_const2s:
      return "DW_OP_const2s";
    case DW_OP_const4u:
      return "DW_OP_const4u";
    case DW_OP_const4s:
      return "DW_OP_const4s";
    case DW_OP_const8u:
      return "DW_OP_const8u";
    case DW_OP_const8s:
      return "DW_OP_const8s";
    case DW_OP_constu:
      return "DW_OP_constu";
    case DW_OP_consts:
      return "DW_OP_consts";
    case DW_OP_dup:
      return "DW_OP_dup";
    case DW_OP_drop:
      return "DW_OP_drop";
    case DW_OP_over:
      return "DW_OP_over";
    case DW_OP_pick:
      return "DW_OP_pick";
    case DW_OP_swap:
      return "DW_OP_swap";
    case DW_OP_rot:
      return "DW_OP_rot";
    case DW_OP_xderef:
      return "DW_OP_xderef";
    case DW_OP_abs:
      return "DW_OP_abs";
    case DW_OP_and:
      return "DW_OP_and";
    case DW_OP_div:
      return "DW_OP_div";
    case DW_OP_minus:
      return "DW_OP_minus";
    case DW_OP_mod:
      return "DW_OP_mod";
    case DW_OP_mul:
      return "DW_OP_mul";
    case DW_OP_neg:
      return "DW_OP_neg";
    case DW_OP_not:
      return "DW_OP_not";
    case DW_OP_or:
      return "DW_OP_or";
    case DW_OP_plus:
      return "DW_OP_plus";
    case DW_OP_plus_uconst:
      return "DW_OP_plus_uconst";
    case DW_OP_shl:
      return "DW_OP_shl";
    case DW_OP_shr:
      return "DW_OP_shr";
    case DW_OP_shra:
      return "DW_OP_shra";
    case DW_OP_xor:
      return "DW_OP_xor";
    case DW_OP_bra:
      return "DW_OP_bra";
    case DW_OP_eq:
      return "DW_OP_eq";
    case DW_OP_ge:
      return "DW_OP_ge";
    case DW_OP_gt:
      return "DW_OP_gt";
    case DW_OP_le:
      return "DW_OP_le";
    case DW_OP_lt:
      return "DW_OP_lt";
    case DW_OP_ne:
      return "DW_OP_ne";
    case DW_OP_skip:
      return "DW_OP_skip";
    case DW_OP_lit0:
      return "DW_OP_lit0";
    case DW_OP_lit1:
      return "DW_OP_lit1";
    case DW_OP_lit2:
      return "DW_OP_lit2";
    case DW_OP_lit3:
      return "DW_OP_lit3";
    case DW_OP_lit4:
      return "DW_OP_lit4";
    case DW_OP_lit5:
      return "DW_OP_lit5";
    case DW_OP_lit6:
      return "DW_OP_lit6";
    case DW_OP_lit7:
      return "DW_OP_lit7";
    case DW_OP_lit8:
      return "DW_OP_lit8";
    case DW_OP_lit9:
      return "DW_OP_lit9";
    case DW_OP_lit10:
      return "DW_OP_lit10";
    case DW_OP_lit11:
      return "DW_OP_lit11";
    case DW_OP_lit12:
      return "DW_OP_lit12";
    case DW_OP_lit13:
      return "DW_OP_lit13";
    case DW_OP_lit14:
      return "DW_OP_lit14";
    case DW_OP_lit15:
      return "DW_OP_lit15";
    case DW_OP_lit16:
      return "DW_OP_lit16";
    case DW_OP_lit17:
      return "DW_OP_lit17";
    case DW_OP_lit18:
      return "DW_OP_lit18";
    case DW_OP_lit19:
      return "DW_OP_lit19";
    case DW_OP_lit20:
      return "DW_OP_lit20";
    case DW_OP_lit21:
      return "DW_OP_lit21";
    case DW_OP_lit22:
      return "DW_OP_lit22";
    case DW_OP_lit23:
      return "DW_OP_lit23";
    case DW_OP_lit24:
      return "DW_OP_lit24";
    case DW_OP_lit25:
      return "DW_OP_lit25";
    case DW_OP_lit26:
      return "DW_OP_lit26";
    case DW_OP_lit27:
      return "DW_OP_lit27";
    case DW_OP_lit28:
      return "DW_OP_lit28";
    case DW_OP_lit29:
      return "DW_OP_lit29";
    case DW_OP_lit30:
      return "DW_OP_lit30";
    case DW_OP_lit31:
      return "DW_OP_lit31";
    case DW_OP_reg0:
      return "DW_OP_reg0";
    case DW_OP_reg1:
      return "DW_OP_reg1";
    case DW_OP_reg2:
      return "DW_OP_reg2";
    case DW_OP_reg3:
      return "DW_OP_reg3";
    case DW_OP_reg4:
      return "DW_OP_reg4";
    case DW_OP_reg5:
      return "DW_OP_reg5";
    case DW_OP_reg6:
      return "DW_OP_reg6";
    case DW_OP_reg7:
      return "DW_OP_reg7";
    case DW_OP_reg8:
      return "DW_OP_reg8";
    case DW_OP_reg9:
      return "DW_OP_reg9";
    case DW_OP_reg10:
      return "DW_OP_reg10";
    case DW_OP_reg11:
      return "DW_OP_reg11";
    case DW_OP_reg12:
      return "DW_OP_reg12";
    case DW_OP_reg13:
      return "DW_OP_reg13";
    case DW_OP_reg14:
      return "DW_OP_reg14";
    case DW_OP_reg15:
      return "DW_OP_reg15";
    case DW_OP_reg16:
      return "DW_OP_reg16";
    case DW_OP_reg17:
      return "DW_OP_reg17";
    case DW_OP_reg18:
      return "DW_OP_reg18";
    case DW_OP_reg19:
      return "DW_OP_reg19";
    case DW_OP_reg20:
      return "DW_OP_reg20";
    case DW_OP_reg21:
      return "DW_OP_reg21";
    case DW_OP_reg22:
      return "DW_OP_reg22";
    case DW_OP_reg23:
      return "DW_OP_reg23";
    case DW_OP_reg24:
      return "DW_OP_reg24";
    case DW_OP_reg25:
      return "DW_OP_reg25";
    case DW_OP_reg26:
      return "DW_OP_reg26";
    case DW_OP_reg27:
      return "DW_OP_reg27";
    case DW_OP_reg28:
      return "DW_OP_reg28";
    case DW_OP_reg29:
      return "DW_OP_reg29";
    case DW_OP_reg30:
      return "DW_OP_reg30";
    case DW_OP_reg31:
      return "DW_OP_reg31";
    case DW_OP_breg0:
      return "DW_OP_breg0";
    case DW_OP_breg1:
      return "DW_OP_breg1";
    case DW_OP_breg2:
      return "DW_OP_breg2";
    case DW_OP_breg3:
      return "DW_OP_breg3";
    case DW_OP_breg4:
      return "DW_OP_breg4";
    case DW_OP_breg5:
      return "DW_OP_breg5";
    case DW_OP_breg6:
      return "DW_OP_breg6";
    case DW_OP_breg7:
      return "DW_OP_breg7";
    case DW_OP_breg8:
      return "DW_OP_breg8";
    case DW_OP_breg9:
      return "DW_OP_breg9";
    case DW_OP_breg10:
      return "DW_OP_breg10";
    case DW_OP_breg11:
      return "DW_OP_breg11";
    case DW_OP_breg12:
      return "DW_OP_breg12";
    case DW_OP_breg13:
      return "DW_OP_breg13";
    case DW_OP_breg14:
      return "DW_OP_breg14";
    case DW_OP_breg15:
      return "DW_OP_breg15";
    case DW_OP_breg16:
      return "DW_OP_breg16";
    case DW_OP_breg17:
      return "DW_OP_breg17";
    case DW_OP_breg18:
      return "DW_OP_breg18";
    case DW_OP_breg19:
      return "DW_OP_breg19";
    case DW_OP_breg20:
      return "DW_OP_breg20";
    case DW_OP_breg21:
      return "DW_OP_breg21";
    case DW_OP_breg22:
      return "DW_OP_breg22";
    case DW_OP_breg23:
      return "DW_OP_breg23";
    case DW_OP_breg24:
      return "DW_OP_breg24";
    case DW_OP_breg25:
      return "DW_OP_breg25";
    case DW_OP_breg26:
      return "DW_OP_breg26";
    case DW_OP_breg27:
      return "DW_OP_breg27";
    case DW_OP_breg28:
      return "DW_OP_breg28";
    case DW_OP_breg29:
      return "DW_OP_breg29";
    case DW_OP_breg30:
      return "DW_OP_breg30";
    case DW_OP_breg31:
      return "DW_OP_breg31";
    case DW_OP_regx:
      return "DW_OP_regx";
    case DW_OP_fbreg:
      return "DW_OP_fbreg";
    case DW_OP_bregx:
      return "DW_OP_bregx";
    case DW_OP_piece:
      return "DW_OP_piece";
    case DW_OP_deref_size:
      return "DW_OP_deref_size";
    case DW_OP_xderef_size:
      return "DW_OP_xderef_size";
    case DW_OP_nop:
      return "DW_OP_nop";
    case DW_OP_push_object_address:
      return "DW_OP_push_object_address";
    case DW_OP_call2:
      return "DW_OP_call2";
    case DW_OP_call4:
      return "DW_OP_call4";
    case DW_OP_call_ref:
      return "DW_OP_call_ref";
    case DW_OP_GNU_push_tls_address:
      return "DW_OP_GNU_push_tls_address";
    default:
      return "OP_<unknown>";
    }
}

/* Return a pointer to a newly allocated location description.  Location
   descriptions are simple expression terms that can be strung
   together to form more complicated location (address) descriptions.  */

static inline dw_loc_descr_ref
new_loc_descr (enum dwarf_location_atom op, unsigned HOST_WIDE_INT oprnd1,
	       unsigned HOST_WIDE_INT oprnd2)
{
  dw_loc_descr_ref descr = ggc_alloc_cleared (sizeof (dw_loc_descr_node));

  descr->dw_loc_opc = op;
  descr->dw_loc_oprnd1.val_class = dw_val_class_unsigned_const;
  descr->dw_loc_oprnd1.v.val_unsigned = oprnd1;
  descr->dw_loc_oprnd2.val_class = dw_val_class_unsigned_const;
  descr->dw_loc_oprnd2.v.val_unsigned = oprnd2;

  return descr;
}

/* Add a location description term to a location description expression.  */

static inline void
add_loc_descr (dw_loc_descr_ref *list_head, dw_loc_descr_ref descr)
{
  dw_loc_descr_ref *d;

  /* Find the end of the chain.  */
  for (d = list_head; (*d) != NULL; d = &(*d)->dw_loc_next)
    ;

  *d = descr;
}

/* Return the size of a location descriptor.  */

static unsigned long
size_of_loc_descr (dw_loc_descr_ref loc)
{
  unsigned long size = 1;

  switch (loc->dw_loc_opc)
    {
    case DW_OP_addr:
    case INTERNAL_DW_OP_tls_addr:
      size += DWARF2_ADDR_SIZE;
      break;
    case DW_OP_const1u:
    case DW_OP_const1s:
      size += 1;
      break;
    case DW_OP_const2u:
    case DW_OP_const2s:
      size += 2;
      break;
    case DW_OP_const4u:
    case DW_OP_const4s:
      size += 4;
      break;
    case DW_OP_const8u:
    case DW_OP_const8s:
      size += 8;
      break;
    case DW_OP_constu:
      size += size_of_uleb128 (loc->dw_loc_oprnd1.v.val_unsigned);
      break;
    case DW_OP_consts:
      size += size_of_sleb128 (loc->dw_loc_oprnd1.v.val_int);
      break;
    case DW_OP_pick:
      size += 1;
      break;
    case DW_OP_plus_uconst:
      size += size_of_uleb128 (loc->dw_loc_oprnd1.v.val_unsigned);
      break;
    case DW_OP_skip:
    case DW_OP_bra:
      size += 2;
      break;
    case DW_OP_breg0:
    case DW_OP_breg1:
    case DW_OP_breg2:
    case DW_OP_breg3:
    case DW_OP_breg4:
    case DW_OP_breg5:
    case DW_OP_breg6:
    case DW_OP_breg7:
    case DW_OP_breg8:
    case DW_OP_breg9:
    case DW_OP_breg10:
    case DW_OP_breg11:
    case DW_OP_breg12:
    case DW_OP_breg13:
    case DW_OP_breg14:
    case DW_OP_breg15:
    case DW_OP_breg16:
    case DW_OP_breg17:
    case DW_OP_breg18:
    case DW_OP_breg19:
    case DW_OP_breg20:
    case DW_OP_breg21:
    case DW_OP_breg22:
    case DW_OP_breg23:
    case DW_OP_breg24:
    case DW_OP_breg25:
    case DW_OP_breg26:
    case DW_OP_breg27:
    case DW_OP_breg28:
    case DW_OP_breg29:
    case DW_OP_breg30:
    case DW_OP_breg31:
      size += size_of_sleb128 (loc->dw_loc_oprnd1.v.val_int);
      break;
    case DW_OP_regx:
      size += size_of_uleb128 (loc->dw_loc_oprnd1.v.val_unsigned);
      break;
    case DW_OP_fbreg:
      size += size_of_sleb128 (loc->dw_loc_oprnd1.v.val_int);
      break;
    case DW_OP_bregx:
      size += size_of_uleb128 (loc->dw_loc_oprnd1.v.val_unsigned);
      size += size_of_sleb128 (loc->dw_loc_oprnd2.v.val_int);
      break;
    case DW_OP_piece:
      size += size_of_uleb128 (loc->dw_loc_oprnd1.v.val_unsigned);
      break;
    case DW_OP_deref_size:
    case DW_OP_xderef_size:
      size += 1;
      break;
    case DW_OP_call2:
      size += 2;
      break;
    case DW_OP_call4:
      size += 4;
      break;
    case DW_OP_call_ref:
      size += DWARF2_ADDR_SIZE;
      break;
    default:
      break;
    }

  return size;
}

/* Return the size of a series of location descriptors.  */

static unsigned long
size_of_locs (dw_loc_descr_ref loc)
{
  dw_loc_descr_ref l;
  unsigned long size;

  /* If there are no skip or bra opcodes, don't fill in the dw_loc_addr
     field, to avoid writing to a PCH file.  */
  for (size = 0, l = loc; l != NULL; l = l->dw_loc_next)
    {
      if (l->dw_loc_opc == DW_OP_skip || l->dw_loc_opc == DW_OP_bra)
	break;
      size += size_of_loc_descr (l);
    }
  if (! l)
    return size;

  for (size = 0, l = loc; l != NULL; l = l->dw_loc_next)
    {
      l->dw_loc_addr = size;
      size += size_of_loc_descr (l);
    }

  return size;
}

/* Output location description stack opcode's operands (if any).  */

static void
output_loc_operands (dw_loc_descr_ref loc)
{
  dw_val_ref val1 = &loc->dw_loc_oprnd1;
  dw_val_ref val2 = &loc->dw_loc_oprnd2;

  switch (loc->dw_loc_opc)
    {
#ifdef DWARF2_DEBUGGING_INFO
    case DW_OP_addr:
      dw2_asm_output_addr_rtx (DWARF2_ADDR_SIZE, val1->v.val_addr, NULL);
      break;
    case DW_OP_const2u:
    case DW_OP_const2s:
      dw2_asm_output_data (2, val1->v.val_int, NULL);
      break;
    case DW_OP_const4u:
    case DW_OP_const4s:
      dw2_asm_output_data (4, val1->v.val_int, NULL);
      break;
    case DW_OP_const8u:
    case DW_OP_const8s:
      gcc_assert (HOST_BITS_PER_LONG >= 64);
      dw2_asm_output_data (8, val1->v.val_int, NULL);
      break;
    case DW_OP_skip:
    case DW_OP_bra:
      {
	int offset;

	gcc_assert (val1->val_class == dw_val_class_loc);
	offset = val1->v.val_loc->dw_loc_addr - (loc->dw_loc_addr + 3);

	dw2_asm_output_data (2, offset, NULL);
      }
      break;
#else
    case DW_OP_addr:
    case DW_OP_const2u:
    case DW_OP_const2s:
    case DW_OP_const4u:
    case DW_OP_const4s:
    case DW_OP_const8u:
    case DW_OP_const8s:
    case DW_OP_skip:
    case DW_OP_bra:
      /* We currently don't make any attempt to make sure these are
	 aligned properly like we do for the main unwind info, so
	 don't support emitting things larger than a byte if we're
	 only doing unwinding.  */
      gcc_unreachable ();
#endif
    case DW_OP_const1u:
    case DW_OP_const1s:
      dw2_asm_output_data (1, val1->v.val_int, NULL);
      break;
    case DW_OP_constu:
      dw2_asm_output_data_uleb128 (val1->v.val_unsigned, NULL);
      break;
    case DW_OP_consts:
      dw2_asm_output_data_sleb128 (val1->v.val_int, NULL);
      break;
    case DW_OP_pick:
      dw2_asm_output_data (1, val1->v.val_int, NULL);
      break;
    case DW_OP_plus_uconst:
      dw2_asm_output_data_uleb128 (val1->v.val_unsigned, NULL);
      break;
    case DW_OP_breg0:
    case DW_OP_breg1:
    case DW_OP_breg2:
    case DW_OP_breg3:
    case DW_OP_breg4:
    case DW_OP_breg5:
    case DW_OP_breg6:
    case DW_OP_breg7:
    case DW_OP_breg8:
    case DW_OP_breg9:
    case DW_OP_breg10:
    case DW_OP_breg11:
    case DW_OP_breg12:
    case DW_OP_breg13:
    case DW_OP_breg14:
    case DW_OP_breg15:
    case DW_OP_breg16:
    case DW_OP_breg17:
    case DW_OP_breg18:
    case DW_OP_breg19:
    case DW_OP_breg20:
    case DW_OP_breg21:
    case DW_OP_breg22:
    case DW_OP_breg23:
    case DW_OP_breg24:
    case DW_OP_breg25:
    case DW_OP_breg26:
    case DW_OP_breg27:
    case DW_OP_breg28:
    case DW_OP_breg29:
    case DW_OP_breg30:
    case DW_OP_breg31:
      dw2_asm_output_data_sleb128 (val1->v.val_int, NULL);
      break;
    case DW_OP_regx:
      dw2_asm_output_data_uleb128 (val1->v.val_unsigned, NULL);
      break;
    case DW_OP_fbreg:
      dw2_asm_output_data_sleb128 (val1->v.val_int, NULL);
      break;
    case DW_OP_bregx:
      dw2_asm_output_data_uleb128 (val1->v.val_unsigned, NULL);
      dw2_asm_output_data_sleb128 (val2->v.val_int, NULL);
      break;
    case DW_OP_piece:
      dw2_asm_output_data_uleb128 (val1->v.val_unsigned, NULL);
      break;
    case DW_OP_deref_size:
    case DW_OP_xderef_size:
      dw2_asm_output_data (1, val1->v.val_int, NULL);
      break;

    case INTERNAL_DW_OP_tls_addr:
      if (targetm.asm_out.output_dwarf_dtprel)
	{
	  targetm.asm_out.output_dwarf_dtprel (asm_out_file,
					       DWARF2_ADDR_SIZE,
					       val1->v.val_addr);
	  fputc ('\n', asm_out_file);
	}
      else
	gcc_unreachable ();
      break;

    default:
      /* Other codes have no operands.  */
      break;
    }
}

/* Output a sequence of location operations.  */

static void
output_loc_sequence (dw_loc_descr_ref loc)
{
  for (; loc != NULL; loc = loc->dw_loc_next)
    {
      /* Output the opcode.  */
      dw2_asm_output_data (1, loc->dw_loc_opc,
			   "%s", dwarf_stack_op_name (loc->dw_loc_opc));

      /* Output the operand(s) (if any).  */
      output_loc_operands (loc);
    }
}

/* This routine will generate the correct assembly data for a location
   description based on a cfi entry with a complex address.  */

static void
output_cfa_loc (dw_cfi_ref cfi)
{
  dw_loc_descr_ref loc;
  unsigned long size;

  /* Output the size of the block.  */
  loc = cfi->dw_cfi_oprnd1.dw_cfi_loc;
  size = size_of_locs (loc);
  dw2_asm_output_data_uleb128 (size, NULL);

  /* Now output the operations themselves.  */
  output_loc_sequence (loc);
}

/* This function builds a dwarf location descriptor sequence from a
   dw_cfa_location, adding the given OFFSET to the result of the
   expression.  */

static struct dw_loc_descr_struct *
build_cfa_loc (dw_cfa_location *cfa, HOST_WIDE_INT offset)
{
  struct dw_loc_descr_struct *head, *tmp;

  offset += cfa->offset;

  if (cfa->indirect)
    {
      if (cfa->base_offset)
	{
	  if (cfa->reg <= 31)
	    head = new_loc_descr (DW_OP_breg0 + cfa->reg, cfa->base_offset, 0);
	  else
	    head = new_loc_descr (DW_OP_bregx, cfa->reg, cfa->base_offset);
	}
      else if (cfa->reg <= 31)
	head = new_loc_descr (DW_OP_reg0 + cfa->reg, 0, 0);
      else
	head = new_loc_descr (DW_OP_regx, cfa->reg, 0);

      head->dw_loc_oprnd1.val_class = dw_val_class_const;
      tmp = new_loc_descr (DW_OP_deref, 0, 0);
      add_loc_descr (&head, tmp);
      if (offset != 0)
	{
	  tmp = new_loc_descr (DW_OP_plus_uconst, offset, 0);
	  add_loc_descr (&head, tmp);
	}
    }
  else
    {
      if (offset == 0)
	if (cfa->reg <= 31)
	  head = new_loc_descr (DW_OP_reg0 + cfa->reg, 0, 0);
	else
	  head = new_loc_descr (DW_OP_regx, cfa->reg, 0);
      else if (cfa->reg <= 31)
	head = new_loc_descr (DW_OP_breg0 + cfa->reg, offset, 0);
      else
	head = new_loc_descr (DW_OP_bregx, cfa->reg, offset);
    }

  return head;
}

/* This function fills in aa dw_cfa_location structure from a dwarf location
   descriptor sequence.  */

static void
get_cfa_from_loc_descr (dw_cfa_location *cfa, struct dw_loc_descr_struct *loc)
{
  struct dw_loc_descr_struct *ptr;
  cfa->offset = 0;
  cfa->base_offset = 0;
  cfa->indirect = 0;
  cfa->reg = -1;

  for (ptr = loc; ptr != NULL; ptr = ptr->dw_loc_next)
    {
      enum dwarf_location_atom op = ptr->dw_loc_opc;

      switch (op)
	{
	case DW_OP_reg0:
	case DW_OP_reg1:
	case DW_OP_reg2:
	case DW_OP_reg3:
	case DW_OP_reg4:
	case DW_OP_reg5:
	case DW_OP_reg6:
	case DW_OP_reg7:
	case DW_OP_reg8:
	case DW_OP_reg9:
	case DW_OP_reg10:
	case DW_OP_reg11:
	case DW_OP_reg12:
	case DW_OP_reg13:
	case DW_OP_reg14:
	case DW_OP_reg15:
	case DW_OP_reg16:
	case DW_OP_reg17:
	case DW_OP_reg18:
	case DW_OP_reg19:
	case DW_OP_reg20:
	case DW_OP_reg21:
	case DW_OP_reg22:
	case DW_OP_reg23:
	case DW_OP_reg24:
	case DW_OP_reg25:
	case DW_OP_reg26:
	case DW_OP_reg27:
	case DW_OP_reg28:
	case DW_OP_reg29:
	case DW_OP_reg30:
	case DW_OP_reg31:
	  cfa->reg = op - DW_OP_reg0;
	  break;
	case DW_OP_regx:
	  cfa->reg = ptr->dw_loc_oprnd1.v.val_int;
	  break;
	case DW_OP_breg0:
	case DW_OP_breg1:
	case DW_OP_breg2:
	case DW_OP_breg3:
	case DW_OP_breg4:
	case DW_OP_breg5:
	case DW_OP_breg6:
	case DW_OP_breg7:
	case DW_OP_breg8:
	case DW_OP_breg9:
	case DW_OP_breg10:
	case DW_OP_breg11:
	case DW_OP_breg12:
	case DW_OP_breg13:
	case DW_OP_breg14:
	case DW_OP_breg15:
	case DW_OP_breg16:
	case DW_OP_breg17:
	case DW_OP_breg18:
	case DW_OP_breg19:
	case DW_OP_breg20:
	case DW_OP_breg21:
	case DW_OP_breg22:
	case DW_OP_breg23:
	case DW_OP_breg24:
	case DW_OP_breg25:
	case DW_OP_breg26:
	case DW_OP_breg27:
	case DW_OP_breg28:
	case DW_OP_breg29:
	case DW_OP_breg30:
	case DW_OP_breg31:
	  cfa->reg = op - DW_OP_breg0;
	  cfa->base_offset = ptr->dw_loc_oprnd1.v.val_int;
	  break;
	case DW_OP_bregx:
	  cfa->reg = ptr->dw_loc_oprnd1.v.val_int;
	  cfa->base_offset = ptr->dw_loc_oprnd2.v.val_int;
	  break;
	case DW_OP_deref:
	  cfa->indirect = 1;
	  break;
	case DW_OP_plus_uconst:
	  cfa->offset = ptr->dw_loc_oprnd1.v.val_unsigned;
	  break;
	default:
	  internal_error ("DW_LOC_OP %s not implemented",
			  dwarf_stack_op_name (ptr->dw_loc_opc));
	}
    }
}
#endif /* .debug_frame support */

/* And now, the support for symbolic debugging information.  */
#ifdef DWARF2_DEBUGGING_INFO

/* .debug_str support.  */
static int output_indirect_string (void **, void *);

static void dwarf2out_init (const char *);
static void dwarf2out_finish (const char *);
static void dwarf2out_define (unsigned int, const char *);
static void dwarf2out_undef (unsigned int, const char *);
static void dwarf2out_start_source_file (unsigned, const char *);
static void dwarf2out_end_source_file (unsigned);
static void dwarf2out_begin_block (unsigned, unsigned);
static void dwarf2out_end_block (unsigned, unsigned);
static bool dwarf2out_ignore_block (tree);
static void dwarf2out_global_decl (tree);
static void dwarf2out_type_decl (tree, int);
static void dwarf2out_imported_module_or_decl (tree, tree);
static void dwarf2out_abstract_function (tree);
static void dwarf2out_var_location (rtx);
static void dwarf2out_begin_function (tree);
static void dwarf2out_switch_text_section (void);

/* The debug hooks structure.  */

const struct gcc_debug_hooks dwarf2_debug_hooks =
{
  dwarf2out_init,
  dwarf2out_finish,
  dwarf2out_define,
  dwarf2out_undef,
  dwarf2out_start_source_file,
  dwarf2out_end_source_file,
  dwarf2out_begin_block,
  dwarf2out_end_block,
  dwarf2out_ignore_block,
  dwarf2out_source_line,
  dwarf2out_begin_prologue,
  debug_nothing_int_charstar,	/* end_prologue */
  dwarf2out_end_epilogue,
  dwarf2out_begin_function,
  debug_nothing_int,		/* end_function */
  dwarf2out_decl,		/* function_decl */
  dwarf2out_global_decl,
  dwarf2out_type_decl,		/* type_decl */
  dwarf2out_imported_module_or_decl,
  debug_nothing_tree,		/* deferred_inline_function */
  /* The DWARF 2 backend tries to reduce debugging bloat by not
     emitting the abstract description of inline functions until
     something tries to reference them.  */
  dwarf2out_abstract_function,	/* outlining_inline_function */
  debug_nothing_rtx,		/* label */
  debug_nothing_int,		/* handle_pch */
  dwarf2out_var_location,
  dwarf2out_switch_text_section,
  1                             /* start_end_main_source_file */
};
#endif

/* NOTE: In the comments in this file, many references are made to
   "Debugging Information Entries".  This term is abbreviated as `DIE'
   throughout the remainder of this file.  */

/* An internal representation of the DWARF output is built, and then
   walked to generate the DWARF debugging info.  The walk of the internal
   representation is done after the entire program has been compiled.
   The types below are used to describe the internal representation.  */

/* Various DIE's use offsets relative to the beginning of the
   .debug_info section to refer to each other.  */

typedef long int dw_offset;

/* Define typedefs here to avoid circular dependencies.  */

typedef struct dw_attr_struct *dw_attr_ref;
typedef struct dw_line_info_struct *dw_line_info_ref;
typedef struct dw_separate_line_info_struct *dw_separate_line_info_ref;
typedef struct pubname_struct *pubname_ref;
typedef struct dw_ranges_struct *dw_ranges_ref;

/* Each entry in the line_info_table maintains the file and
   line number associated with the label generated for that
   entry.  The label gives the PC value associated with
   the line number entry.  */

typedef struct dw_line_info_struct GTY(())
{
  unsigned long dw_file_num;
  unsigned long dw_line_num;
}
dw_line_info_entry;

/* Line information for functions in separate sections; each one gets its
   own sequence.  */
typedef struct dw_separate_line_info_struct GTY(())
{
  unsigned long dw_file_num;
  unsigned long dw_line_num;
  unsigned long function;
}
dw_separate_line_info_entry;

/* Each DIE attribute has a field specifying the attribute kind,
   a link to the next attribute in the chain, and an attribute value.
   Attributes are typically linked below the DIE they modify.  */

typedef struct dw_attr_struct GTY(())
{
  enum dwarf_attribute dw_attr;
  dw_val_node dw_attr_val;
}
dw_attr_node;

DEF_VEC_O(dw_attr_node);
DEF_VEC_ALLOC_O(dw_attr_node,gc);

/* The Debugging Information Entry (DIE) structure.  DIEs form a tree.
   The children of each node form a circular list linked by
   die_sib.  die_child points to the node *before* the "first" child node.  */

typedef struct die_struct GTY(())
{
  enum dwarf_tag die_tag;
  char *die_symbol;
  VEC(dw_attr_node,gc) * die_attr;
  dw_die_ref die_parent;
  dw_die_ref die_child;
  dw_die_ref die_sib;
  dw_die_ref die_definition; /* ref from a specification to its definition */
  dw_offset die_offset;
  unsigned long die_abbrev;
  int die_mark;
  /* Die is used and must not be pruned as unused.  */
  int die_perennial_p;
  unsigned int decl_id;
}
die_node;

/* Evaluate 'expr' while 'c' is set to each child of DIE in order.  */
#define FOR_EACH_CHILD(die, c, expr) do {	\
  c = die->die_child;				\
  if (c) do {					\
    c = c->die_sib;				\
    expr;					\
  } while (c != die->die_child);		\
} while (0)

/* The pubname structure */

typedef struct pubname_struct GTY(())
{
  dw_die_ref die;
  char *name;
}
pubname_entry;

struct dw_ranges_struct GTY(())
{
  int block_num;
};

/* The limbo die list structure.  */
typedef struct limbo_die_struct GTY(())
{
  dw_die_ref die;
  tree created_for;
  struct limbo_die_struct *next;
}
limbo_die_node;

/* How to start an assembler comment.  */
#ifndef ASM_COMMENT_START
#define ASM_COMMENT_START ";#"
#endif

/* Define a macro which returns nonzero for a TYPE_DECL which was
   implicitly generated for a tagged type.

   Note that unlike the gcc front end (which generates a NULL named
   TYPE_DECL node for each complete tagged type, each array type, and
   each function type node created) the g++ front end generates a
   _named_ TYPE_DECL node for each tagged type node created.
   These TYPE_DECLs have DECL_ARTIFICIAL set, so we know not to
   generate a DW_TAG_typedef DIE for them.  */

#define TYPE_DECL_IS_STUB(decl)				\
  (DECL_NAME (decl) == NULL_TREE			\
   || (DECL_ARTIFICIAL (decl)				\
       && is_tagged_type (TREE_TYPE (decl))		\
       && ((decl == TYPE_STUB_DECL (TREE_TYPE (decl)))	\
	   /* This is necessary for stub decls that	\
	      appear in nested inline functions.  */	\
	   || (DECL_ABSTRACT_ORIGIN (decl) != NULL_TREE	\
	       && (decl_ultimate_origin (decl)		\
		   == TYPE_STUB_DECL (TREE_TYPE (decl)))))))

/* Information concerning the compilation unit's programming
   language, and compiler version.  */

/* Fixed size portion of the DWARF compilation unit header.  */
#define DWARF_COMPILE_UNIT_HEADER_SIZE \
  (DWARF_INITIAL_LENGTH_SIZE + DWARF_OFFSET_SIZE + 3)

/* Fixed size portion of public names info.  */
#define DWARF_PUBNAMES_HEADER_SIZE (2 * DWARF_OFFSET_SIZE + 2)

/* Fixed size portion of the address range info.  */
#define DWARF_ARANGES_HEADER_SIZE					\
  (DWARF_ROUND (DWARF_INITIAL_LENGTH_SIZE + DWARF_OFFSET_SIZE + 4,	\
                DWARF2_ADDR_SIZE * 2)					\
   - DWARF_INITIAL_LENGTH_SIZE)

/* Size of padding portion in the address range info.  It must be
   aligned to twice the pointer size.  */
#define DWARF_ARANGES_PAD_SIZE \
  (DWARF_ROUND (DWARF_INITIAL_LENGTH_SIZE + DWARF_OFFSET_SIZE + 4, \
                DWARF2_ADDR_SIZE * 2) \
   - (DWARF_INITIAL_LENGTH_SIZE + DWARF_OFFSET_SIZE + 4))

/* Use assembler line directives if available.  */
#ifndef DWARF2_ASM_LINE_DEBUG_INFO
#ifdef HAVE_AS_DWARF2_DEBUG_LINE
#define DWARF2_ASM_LINE_DEBUG_INFO 1
#else
#define DWARF2_ASM_LINE_DEBUG_INFO 0
#endif
#endif

/* Minimum line offset in a special line info. opcode.
   This value was chosen to give a reasonable range of values.  */
#define DWARF_LINE_BASE  -10

/* First special line opcode - leave room for the standard opcodes.  */
#define DWARF_LINE_OPCODE_BASE  10

/* Range of line offsets in a special line info. opcode.  */
#define DWARF_LINE_RANGE  (254-DWARF_LINE_OPCODE_BASE+1)

/* Flag that indicates the initial value of the is_stmt_start flag.
   In the present implementation, we do not mark any lines as
   the beginning of a source statement, because that information
   is not made available by the GCC front-end.  */
#define	DWARF_LINE_DEFAULT_IS_STMT_START 1

#ifdef DWARF2_DEBUGGING_INFO
/* This location is used by calc_die_sizes() to keep track
   the offset of each DIE within the .debug_info section.  */
static unsigned long next_die_offset;
#endif

/* Record the root of the DIE's built for the current compilation unit.  */
static GTY(()) dw_die_ref comp_unit_die;

/* A list of DIEs with a NULL parent waiting to be relocated.  */
static GTY(()) limbo_die_node *limbo_die_list;

/* Filenames referenced by this compilation unit.  */
static GTY((param_is (struct dwarf_file_data))) htab_t file_table;

/* A hash table of references to DIE's that describe declarations.
   The key is a DECL_UID() which is a unique number identifying each decl.  */
static GTY ((param_is (struct die_struct))) htab_t decl_die_table;

/* Node of the variable location list.  */
struct var_loc_node GTY ((chain_next ("%h.next")))
{
  rtx GTY (()) var_loc_note;
  const char * GTY (()) label;
  const char * GTY (()) section_label;
  struct var_loc_node * GTY (()) next;
};

/* Variable location list.  */
struct var_loc_list_def GTY (())
{
  struct var_loc_node * GTY (()) first;

  /* Do not mark the last element of the chained list because
     it is marked through the chain.  */
  struct var_loc_node * GTY ((skip ("%h"))) last;

  /* DECL_UID of the variable decl.  */
  unsigned int decl_id;
};
typedef struct var_loc_list_def var_loc_list;


/* Table of decl location linked lists.  */
static GTY ((param_is (var_loc_list))) htab_t decl_loc_table;

/* A pointer to the base of a list of references to DIE's that
   are uniquely identified by their tag, presence/absence of
   children DIE's, and list of attribute/value pairs.  */
static GTY((length ("abbrev_die_table_allocated")))
  dw_die_ref *abbrev_die_table;

/* Number of elements currently allocated for abbrev_die_table.  */
static GTY(()) unsigned abbrev_die_table_allocated;

/* Number of elements in type_die_table currently in use.  */
static GTY(()) unsigned abbrev_die_table_in_use;

/* Size (in elements) of increments by which we may expand the
   abbrev_die_table.  */
#define ABBREV_DIE_TABLE_INCREMENT 256

/* A pointer to the base of a table that contains line information
   for each source code line in .text in the compilation unit.  */
static GTY((length ("line_info_table_allocated")))
     dw_line_info_ref line_info_table;

/* Number of elements currently allocated for line_info_table.  */
static GTY(()) unsigned line_info_table_allocated;

/* Number of elements in line_info_table currently in use.  */
static GTY(()) unsigned line_info_table_in_use;

/* True if the compilation unit places functions in more than one section.  */
static GTY(()) bool have_multiple_function_sections = false;

/* A pointer to the base of a table that contains line information
   for each source code line outside of .text in the compilation unit.  */
static GTY ((length ("separate_line_info_table_allocated")))
     dw_separate_line_info_ref separate_line_info_table;

/* Number of elements currently allocated for separate_line_info_table.  */
static GTY(()) unsigned separate_line_info_table_allocated;

/* Number of elements in separate_line_info_table currently in use.  */
static GTY(()) unsigned separate_line_info_table_in_use;

/* Size (in elements) of increments by which we may expand the
   line_info_table.  */
#define LINE_INFO_TABLE_INCREMENT 1024

/* A pointer to the base of a table that contains a list of publicly
   accessible names.  */
static GTY ((length ("pubname_table_allocated"))) pubname_ref pubname_table;

/* Number of elements currently allocated for pubname_table.  */
static GTY(()) unsigned pubname_table_allocated;

/* Number of elements in pubname_table currently in use.  */
static GTY(()) unsigned pubname_table_in_use;

/* Size (in elements) of increments by which we may expand the
   pubname_table.  */
#define PUBNAME_TABLE_INCREMENT 64

/* Array of dies for which we should generate .debug_arange info.  */
static GTY((length ("arange_table_allocated"))) dw_die_ref *arange_table;

/* Number of elements currently allocated for arange_table.  */
static GTY(()) unsigned arange_table_allocated;

/* Number of elements in arange_table currently in use.  */
static GTY(()) unsigned arange_table_in_use;

/* Size (in elements) of increments by which we may expand the
   arange_table.  */
#define ARANGE_TABLE_INCREMENT 64

/* Array of dies for which we should generate .debug_ranges info.  */
static GTY ((length ("ranges_table_allocated"))) dw_ranges_ref ranges_table;

/* Number of elements currently allocated for ranges_table.  */
static GTY(()) unsigned ranges_table_allocated;

/* Number of elements in ranges_table currently in use.  */
static GTY(()) unsigned ranges_table_in_use;

/* Size (in elements) of increments by which we may expand the
   ranges_table.  */
#define RANGES_TABLE_INCREMENT 64

/* Whether we have location lists that need outputting */
static GTY(()) bool have_location_lists;

/* Unique label counter.  */
static GTY(()) unsigned int loclabel_num;

#ifdef DWARF2_DEBUGGING_INFO
/* Record whether the function being analyzed contains inlined functions.  */
static int current_function_has_inlines;
#endif
#if 0 && defined (MIPS_DEBUGGING_INFO)
static int comp_unit_has_inlines;
#endif

/* The last file entry emitted by maybe_emit_file().  */
static GTY(()) struct dwarf_file_data * last_emitted_file;

/* Number of internal labels generated by gen_internal_sym().  */
static GTY(()) int label_num;

/* Cached result of previous call to lookup_filename.  */
static GTY(()) struct dwarf_file_data * file_table_last_lookup;

#ifdef DWARF2_DEBUGGING_INFO

/* Offset from the "steady-state frame pointer" to the frame base,
   within the current function.  */
static HOST_WIDE_INT frame_pointer_fb_offset;

/* Forward declarations for functions defined in this file.  */

static int is_pseudo_reg (rtx);
static tree type_main_variant (tree);
static int is_tagged_type (tree);
static const char *dwarf_tag_name (unsigned);
static const char *dwarf_attr_name (unsigned);
static const char *dwarf_form_name (unsigned);
static tree decl_ultimate_origin (tree);
static tree block_ultimate_origin (tree);
static tree decl_class_context (tree);
static void add_dwarf_attr (dw_die_ref, dw_attr_ref);
static inline enum dw_val_class AT_class (dw_attr_ref);
static void add_AT_flag (dw_die_ref, enum dwarf_attribute, unsigned);
static inline unsigned AT_flag (dw_attr_ref);
static void add_AT_int (dw_die_ref, enum dwarf_attribute, HOST_WIDE_INT);
static inline HOST_WIDE_INT AT_int (dw_attr_ref);
static void add_AT_unsigned (dw_die_ref, enum dwarf_attribute, unsigned HOST_WIDE_INT);
static inline unsigned HOST_WIDE_INT AT_unsigned (dw_attr_ref);
static void add_AT_long_long (dw_die_ref, enum dwarf_attribute, unsigned long,
			      unsigned long);
static inline void add_AT_vec (dw_die_ref, enum dwarf_attribute, unsigned int,
			       unsigned int, unsigned char *);
static hashval_t debug_str_do_hash (const void *);
static int debug_str_eq (const void *, const void *);
static void add_AT_string (dw_die_ref, enum dwarf_attribute, const char *);
static inline const char *AT_string (dw_attr_ref);
static int AT_string_form (dw_attr_ref);
static void add_AT_die_ref (dw_die_ref, enum dwarf_attribute, dw_die_ref);
static void add_AT_specification (dw_die_ref, dw_die_ref);
static inline dw_die_ref AT_ref (dw_attr_ref);
static inline int AT_ref_external (dw_attr_ref);
static inline void set_AT_ref_external (dw_attr_ref, int);
static void add_AT_fde_ref (dw_die_ref, enum dwarf_attribute, unsigned);
static void add_AT_loc (dw_die_ref, enum dwarf_attribute, dw_loc_descr_ref);
static inline dw_loc_descr_ref AT_loc (dw_attr_ref);
static void add_AT_loc_list (dw_die_ref, enum dwarf_attribute,
			     dw_loc_list_ref);
static inline dw_loc_list_ref AT_loc_list (dw_attr_ref);
static void add_AT_addr (dw_die_ref, enum dwarf_attribute, rtx);
static inline rtx AT_addr (dw_attr_ref);
static void add_AT_lbl_id (dw_die_ref, enum dwarf_attribute, const char *);
static void add_AT_lineptr (dw_die_ref, enum dwarf_attribute, const char *);
static void add_AT_macptr (dw_die_ref, enum dwarf_attribute, const char *);
static void add_AT_offset (dw_die_ref, enum dwarf_attribute,
			   unsigned HOST_WIDE_INT);
static void add_AT_range_list (dw_die_ref, enum dwarf_attribute,
			       unsigned long);
static inline const char *AT_lbl (dw_attr_ref);
static dw_attr_ref get_AT (dw_die_ref, enum dwarf_attribute);
static const char *get_AT_low_pc (dw_die_ref);
static const char *get_AT_hi_pc (dw_die_ref);
static const char *get_AT_string (dw_die_ref, enum dwarf_attribute);
static int get_AT_flag (dw_die_ref, enum dwarf_attribute);
static unsigned get_AT_unsigned (dw_die_ref, enum dwarf_attribute);
static inline dw_die_ref get_AT_ref (dw_die_ref, enum dwarf_attribute);
static bool is_c_family (void);
static bool is_cxx (void);
static bool is_java (void);
static bool is_fortran (void);
static bool is_ada (void);
static void remove_AT (dw_die_ref, enum dwarf_attribute);
static void remove_child_TAG (dw_die_ref, enum dwarf_tag);
static void add_child_die (dw_die_ref, dw_die_ref);
static dw_die_ref new_die (enum dwarf_tag, dw_die_ref, tree);
static dw_die_ref lookup_type_die (tree);
static void equate_type_number_to_die (tree, dw_die_ref);
static hashval_t decl_die_table_hash (const void *);
static int decl_die_table_eq (const void *, const void *);
static dw_die_ref lookup_decl_die (tree);
static hashval_t decl_loc_table_hash (const void *);
static int decl_loc_table_eq (const void *, const void *);
static var_loc_list *lookup_decl_loc (tree);
static void equate_decl_number_to_die (tree, dw_die_ref);
static void add_var_loc_to_decl (tree, struct var_loc_node *);
static void print_spaces (FILE *);
static void print_die (dw_die_ref, FILE *);
static void print_dwarf_line_table (FILE *);
static dw_die_ref push_new_compile_unit (dw_die_ref, dw_die_ref);
static dw_die_ref pop_compile_unit (dw_die_ref);
static void loc_checksum (dw_loc_descr_ref, struct md5_ctx *);
static void attr_checksum (dw_attr_ref, struct md5_ctx *, int *);
static void die_checksum (dw_die_ref, struct md5_ctx *, int *);
static int same_loc_p (dw_loc_descr_ref, dw_loc_descr_ref, int *);
static int same_dw_val_p (dw_val_node *, dw_val_node *, int *);
static int same_attr_p (dw_attr_ref, dw_attr_ref, int *);
static int same_die_p (dw_die_ref, dw_die_ref, int *);
static int same_die_p_wrap (dw_die_ref, dw_die_ref);
static void compute_section_prefix (dw_die_ref);
static int is_type_die (dw_die_ref);
static int is_comdat_die (dw_die_ref);
static int is_symbol_die (dw_die_ref);
static void assign_symbol_names (dw_die_ref);
static void break_out_includes (dw_die_ref);
static hashval_t htab_cu_hash (const void *);
static int htab_cu_eq (const void *, const void *);
static void htab_cu_del (void *);
static int check_duplicate_cu (dw_die_ref, htab_t, unsigned *);
static void record_comdat_symbol_number (dw_die_ref, htab_t, unsigned);
static void add_sibling_attributes (dw_die_ref);
static void build_abbrev_table (dw_die_ref);
static void output_location_lists (dw_die_ref);
static int constant_size (long unsigned);
static unsigned long size_of_die (dw_die_ref);
static void calc_die_sizes (dw_die_ref);
static void mark_dies (dw_die_ref);
static void unmark_dies (dw_die_ref);
static void unmark_all_dies (dw_die_ref);
static unsigned long size_of_pubnames (void);
static unsigned long size_of_aranges (void);
static enum dwarf_form value_format (dw_attr_ref);
static void output_value_format (dw_attr_ref);
static void output_abbrev_section (void);
static void output_die_symbol (dw_die_ref);
static void output_die (dw_die_ref);
static void output_compilation_unit_header (void);
static void output_comp_unit (dw_die_ref, int);
static const char *dwarf2_name (tree, int);
static void add_pubname (tree, dw_die_ref);
static void output_pubnames (void);
static void add_arange (tree, dw_die_ref);
static void output_aranges (void);
static unsigned int add_ranges (tree);
static void output_ranges (void);
static void output_line_info (void);
static void output_file_names (void);
static dw_die_ref base_type_die (tree);
static tree root_type (tree);
static int is_base_type (tree);
static bool is_subrange_type (tree);
static dw_die_ref subrange_type_die (tree, dw_die_ref);
static dw_die_ref modified_type_die (tree, int, int, dw_die_ref);
static int type_is_enum (tree);
static unsigned int dbx_reg_number (rtx);
static void add_loc_descr_op_piece (dw_loc_descr_ref *, int);
static dw_loc_descr_ref reg_loc_descriptor (rtx);
static dw_loc_descr_ref one_reg_loc_descriptor (unsigned int);
static dw_loc_descr_ref multiple_reg_loc_descriptor (rtx, rtx);
static dw_loc_descr_ref int_loc_descriptor (HOST_WIDE_INT);
static dw_loc_descr_ref based_loc_descr (rtx, HOST_WIDE_INT);
static int is_based_loc (rtx);
static dw_loc_descr_ref mem_loc_descriptor (rtx, enum machine_mode mode);
static dw_loc_descr_ref concat_loc_descriptor (rtx, rtx);
static dw_loc_descr_ref loc_descriptor (rtx);
static dw_loc_descr_ref loc_descriptor_from_tree_1 (tree, int);
static dw_loc_descr_ref loc_descriptor_from_tree (tree);
static HOST_WIDE_INT ceiling (HOST_WIDE_INT, unsigned int);
static tree field_type (tree);
static unsigned int simple_type_align_in_bits (tree);
static unsigned int simple_decl_align_in_bits (tree);
static unsigned HOST_WIDE_INT simple_type_size_in_bits (tree);
static HOST_WIDE_INT field_byte_offset (tree);
static void add_AT_location_description	(dw_die_ref, enum dwarf_attribute,
					 dw_loc_descr_ref);
static void add_data_member_location_attribute (dw_die_ref, tree);
static void add_const_value_attribute (dw_die_ref, rtx);
static void insert_int (HOST_WIDE_INT, unsigned, unsigned char *);
static HOST_WIDE_INT extract_int (const unsigned char *, unsigned);
static void insert_float (rtx, unsigned char *);
static rtx rtl_for_decl_location (tree);
static void add_location_or_const_value_attribute (dw_die_ref, tree,
						   enum dwarf_attribute);
static void tree_add_const_value_attribute (dw_die_ref, tree);
static void add_name_attribute (dw_die_ref, const char *);
static void add_comp_dir_attribute (dw_die_ref);
static void add_bound_info (dw_die_ref, enum dwarf_attribute, tree);
static void add_subscript_info (dw_die_ref, tree);
static void add_byte_size_attribute (dw_die_ref, tree);
static void add_bit_offset_attribute (dw_die_ref, tree);
static void add_bit_size_attribute (dw_die_ref, tree);
static void add_prototyped_attribute (dw_die_ref, tree);
static void add_abstract_origin_attribute (dw_die_ref, tree);
static void add_pure_or_virtual_attribute (dw_die_ref, tree);
static void add_src_coords_attributes (dw_die_ref, tree);
static void add_name_and_src_coords_attributes (dw_die_ref, tree);
static void push_decl_scope (tree);
static void pop_decl_scope (void);
static dw_die_ref scope_die_for (tree, dw_die_ref);
static inline int local_scope_p (dw_die_ref);
static inline int class_or_namespace_scope_p (dw_die_ref);
static void add_type_attribute (dw_die_ref, tree, int, int, dw_die_ref);
static void add_calling_convention_attribute (dw_die_ref, tree);
static const char *type_tag (tree);
static tree member_declared_type (tree);
#if 0
static const char *decl_start_label (tree);
#endif
static void gen_array_type_die (tree, dw_die_ref);
#if 0
static void gen_entry_point_die (tree, dw_die_ref);
#endif
static void gen_inlined_enumeration_type_die (tree, dw_die_ref);
static void gen_inlined_structure_type_die (tree, dw_die_ref);
static void gen_inlined_union_type_die (tree, dw_die_ref);
static dw_die_ref gen_enumeration_type_die (tree, dw_die_ref);
static dw_die_ref gen_formal_parameter_die (tree, dw_die_ref);
static void gen_unspecified_parameters_die (tree, dw_die_ref);
static void gen_formal_types_die (tree, dw_die_ref);
static void gen_subprogram_die (tree, dw_die_ref);
static void gen_variable_die (tree, dw_die_ref);
static void gen_label_die (tree, dw_die_ref);
static void gen_lexical_block_die (tree, dw_die_ref, int);
static void gen_inlined_subroutine_die (tree, dw_die_ref, int);
static void gen_field_die (tree, dw_die_ref);
static void gen_ptr_to_mbr_type_die (tree, dw_die_ref);
static dw_die_ref gen_compile_unit_die (const char *);
static void gen_inheritance_die (tree, tree, dw_die_ref);
static void gen_member_die (tree, dw_die_ref);
static void gen_struct_or_union_type_die (tree, dw_die_ref);
static void gen_subroutine_type_die (tree, dw_die_ref);
static void gen_typedef_die (tree, dw_die_ref);
static void gen_type_die (tree, dw_die_ref);
static void gen_tagged_type_instantiation_die (tree, dw_die_ref);
static void gen_block_die (tree, dw_die_ref, int);
static void decls_for_scope (tree, dw_die_ref, int);
static int is_redundant_typedef (tree);
static void gen_namespace_die (tree);
static void gen_decl_die (tree, dw_die_ref);
static dw_die_ref force_decl_die (tree);
static dw_die_ref force_type_die (tree);
static dw_die_ref setup_namespace_context (tree, dw_die_ref);
static void declare_in_namespace (tree, dw_die_ref);
static struct dwarf_file_data * lookup_filename (const char *);
static void retry_incomplete_types (void);
static void gen_type_die_for_member (tree, tree, dw_die_ref);
static void splice_child_die (dw_die_ref, dw_die_ref);
static int file_info_cmp (const void *, const void *);
static dw_loc_list_ref new_loc_list (dw_loc_descr_ref, const char *,
				     const char *, const char *, unsigned);
static void add_loc_descr_to_loc_list (dw_loc_list_ref *, dw_loc_descr_ref,
				       const char *, const char *,
				       const char *);
static void output_loc_list (dw_loc_list_ref);
static char *gen_internal_sym (const char *);

static void prune_unmark_dies (dw_die_ref);
static void prune_unused_types_mark (dw_die_ref, int);
static void prune_unused_types_walk (dw_die_ref);
static void prune_unused_types_walk_attribs (dw_die_ref);
static void prune_unused_types_prune (dw_die_ref);
static void prune_unused_types (void);
static int maybe_emit_file (struct dwarf_file_data *fd);

/* Section names used to hold DWARF debugging information.  */
#ifndef DEBUG_INFO_SECTION
#define DEBUG_INFO_SECTION	".debug_info"
#endif
#ifndef DEBUG_ABBREV_SECTION
#define DEBUG_ABBREV_SECTION	".debug_abbrev"
#endif
#ifndef DEBUG_ARANGES_SECTION
#define DEBUG_ARANGES_SECTION	".debug_aranges"
#endif
#ifndef DEBUG_MACINFO_SECTION
#define DEBUG_MACINFO_SECTION	".debug_macinfo"
#endif
#ifndef DEBUG_LINE_SECTION
#define DEBUG_LINE_SECTION	".debug_line"
#endif
#ifndef DEBUG_LOC_SECTION
#define DEBUG_LOC_SECTION	".debug_loc"
#endif
#ifndef DEBUG_PUBNAMES_SECTION
#define DEBUG_PUBNAMES_SECTION	".debug_pubnames"
#endif
#ifndef DEBUG_STR_SECTION
#define DEBUG_STR_SECTION	".debug_str"
#endif
#ifndef DEBUG_RANGES_SECTION
#define DEBUG_RANGES_SECTION	".debug_ranges"
#endif

/* Standard ELF section names for compiled code and data.  */
#ifndef TEXT_SECTION_NAME
#define TEXT_SECTION_NAME	".text"
#endif

/* Section flags for .debug_str section.  */
#define DEBUG_STR_SECTION_FLAGS \
  (HAVE_GAS_SHF_MERGE && flag_merge_constants			\
   ? SECTION_DEBUG | SECTION_MERGE | SECTION_STRINGS | 1	\
   : SECTION_DEBUG)

/* Labels we insert at beginning sections we can reference instead of
   the section names themselves.  */

#ifndef TEXT_SECTION_LABEL
#define TEXT_SECTION_LABEL		"Ltext"
#endif
#ifndef COLD_TEXT_SECTION_LABEL
#define COLD_TEXT_SECTION_LABEL         "Ltext_cold"
#endif
#ifndef DEBUG_LINE_SECTION_LABEL
#define DEBUG_LINE_SECTION_LABEL	"Ldebug_line"
#endif
#ifndef DEBUG_INFO_SECTION_LABEL
#define DEBUG_INFO_SECTION_LABEL	"Ldebug_info"
#endif
#ifndef DEBUG_ABBREV_SECTION_LABEL
#define DEBUG_ABBREV_SECTION_LABEL	"Ldebug_abbrev"
#endif
#ifndef DEBUG_LOC_SECTION_LABEL
#define DEBUG_LOC_SECTION_LABEL		"Ldebug_loc"
#endif
#ifndef DEBUG_RANGES_SECTION_LABEL
#define DEBUG_RANGES_SECTION_LABEL	"Ldebug_ranges"
#endif
#ifndef DEBUG_MACINFO_SECTION_LABEL
#define DEBUG_MACINFO_SECTION_LABEL     "Ldebug_macinfo"
#endif

/* Definitions of defaults for formats and names of various special
   (artificial) labels which may be generated within this file (when the -g
   options is used and DWARF2_DEBUGGING_INFO is in effect.
   If necessary, these may be overridden from within the tm.h file, but
   typically, overriding these defaults is unnecessary.  */

static char text_end_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char text_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char cold_text_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char cold_end_label[MAX_ARTIFICIAL_LABEL_BYTES]; 
static char abbrev_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char debug_info_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char debug_line_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char macinfo_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char loc_section_label[MAX_ARTIFICIAL_LABEL_BYTES];
static char ranges_section_label[2 * MAX_ARTIFICIAL_LABEL_BYTES];

#ifndef TEXT_END_LABEL
#define TEXT_END_LABEL		"Letext"
#endif
#ifndef COLD_END_LABEL
#define COLD_END_LABEL          "Letext_cold"
#endif
#ifndef BLOCK_BEGIN_LABEL
#define BLOCK_BEGIN_LABEL	"LBB"
#endif
#ifndef BLOCK_END_LABEL
#define BLOCK_END_LABEL		"LBE"
#endif
#ifndef LINE_CODE_LABEL
#define LINE_CODE_LABEL		"LM"
#endif
#ifndef SEPARATE_LINE_CODE_LABEL
#define SEPARATE_LINE_CODE_LABEL	"LSM"
#endif

/* We allow a language front-end to designate a function that is to be
   called to "demangle" any name before it is put into a DIE.  */

static const char *(*demangle_name_func) (const char *);

void
dwarf2out_set_demangle_name_func (const char *(*func) (const char *))
{
  demangle_name_func = func;
}

/* Test if rtl node points to a pseudo register.  */

static inline int
is_pseudo_reg (rtx rtl)
{
  return ((REG_P (rtl) && REGNO (rtl) >= FIRST_PSEUDO_REGISTER)
	  || (GET_CODE (rtl) == SUBREG
	      && REGNO (SUBREG_REG (rtl)) >= FIRST_PSEUDO_REGISTER));
}

/* Return a reference to a type, with its const and volatile qualifiers
   removed.  */

static inline tree
type_main_variant (tree type)
{
  type = TYPE_MAIN_VARIANT (type);

  /* ??? There really should be only one main variant among any group of
     variants of a given type (and all of the MAIN_VARIANT values for all
     members of the group should point to that one type) but sometimes the C
     front-end messes this up for array types, so we work around that bug
     here.  */
  if (TREE_CODE (type) == ARRAY_TYPE)
    while (type != TYPE_MAIN_VARIANT (type))
      type = TYPE_MAIN_VARIANT (type);

  return type;
}

/* Return nonzero if the given type node represents a tagged type.  */

static inline int
is_tagged_type (tree type)
{
  enum tree_code code = TREE_CODE (type);

  return (code == RECORD_TYPE || code == UNION_TYPE
	  || code == QUAL_UNION_TYPE || code == ENUMERAL_TYPE);
}

/* Convert a DIE tag into its string name.  */

static const char *
dwarf_tag_name (unsigned int tag)
{
  switch (tag)
    {
    case DW_TAG_padding:
      return "DW_TAG_padding";
    case DW_TAG_array_type:
      return "DW_TAG_array_type";
    case DW_TAG_class_type:
      return "DW_TAG_class_type";
    case DW_TAG_entry_point:
      return "DW_TAG_entry_point";
    case DW_TAG_enumeration_type:
      return "DW_TAG_enumeration_type";
    case DW_TAG_formal_parameter:
      return "DW_TAG_formal_parameter";
    case DW_TAG_imported_declaration:
      return "DW_TAG_imported_declaration";
    case DW_TAG_label:
      return "DW_TAG_label";
    case DW_TAG_lexical_block:
      return "DW_TAG_lexical_block";
    case DW_TAG_member:
      return "DW_TAG_member";
    case DW_TAG_pointer_type:
      return "DW_TAG_pointer_type";
    case DW_TAG_reference_type:
      return "DW_TAG_reference_type";
    case DW_TAG_compile_unit:
      return "DW_TAG_compile_unit";
    case DW_TAG_string_type:
      return "DW_TAG_string_type";
    case DW_TAG_structure_type:
      return "DW_TAG_structure_type";
    case DW_TAG_subroutine_type:
      return "DW_TAG_subroutine_type";
    case DW_TAG_typedef:
      return "DW_TAG_typedef";
    case DW_TAG_union_type:
      return "DW_TAG_union_type";
    case DW_TAG_unspecified_parameters:
      return "DW_TAG_unspecified_parameters";
    case DW_TAG_variant:
      return "DW_TAG_variant";
    case DW_TAG_common_block:
      return "DW_TAG_common_block";
    case DW_TAG_common_inclusion:
      return "DW_TAG_common_inclusion";
    case DW_TAG_inheritance:
      return "DW_TAG_inheritance";
    case DW_TAG_inlined_subroutine:
      return "DW_TAG_inlined_subroutine";
    case DW_TAG_module:
      return "DW_TAG_module";
    case DW_TAG_ptr_to_member_type:
      return "DW_TAG_ptr_to_member_type";
    case DW_TAG_set_type:
      return "DW_TAG_set_type";
    case DW_TAG_subrange_type:
      return "DW_TAG_subrange_type";
    case DW_TAG_with_stmt:
      return "DW_TAG_with_stmt";
    case DW_TAG_access_declaration:
      return "DW_TAG_access_declaration";
    case DW_TAG_base_type:
      return "DW_TAG_base_type";
    case DW_TAG_catch_block:
      return "DW_TAG_catch_block";
    case DW_TAG_const_type:
      return "DW_TAG_const_type";
    case DW_TAG_constant:
      return "DW_TAG_constant";
    case DW_TAG_enumerator:
      return "DW_TAG_enumerator";
    case DW_TAG_file_type:
      return "DW_TAG_file_type";
    case DW_TAG_friend:
      return "DW_TAG_friend";
    case DW_TAG_namelist:
      return "DW_TAG_namelist";
    case DW_TAG_namelist_item:
      return "DW_TAG_namelist_item";
    case DW_TAG_namespace:
      return "DW_TAG_namespace";
    case DW_TAG_packed_type:
      return "DW_TAG_packed_type";
    case DW_TAG_subprogram:
      return "DW_TAG_subprogram";
    case DW_TAG_template_type_param:
      return "DW_TAG_template_type_param";
    case DW_TAG_template_value_param:
      return "DW_TAG_template_value_param";
    case DW_TAG_thrown_type:
      return "DW_TAG_thrown_type";
    case DW_TAG_try_block:
      return "DW_TAG_try_block";
    case DW_TAG_variant_part:
      return "DW_TAG_variant_part";
    case DW_TAG_variable:
      return "DW_TAG_variable";
    case DW_TAG_volatile_type:
      return "DW_TAG_volatile_type";
    case DW_TAG_imported_module:
      return "DW_TAG_imported_module";
    case DW_TAG_MIPS_loop:
      return "DW_TAG_MIPS_loop";
    case DW_TAG_format_label:
      return "DW_TAG_format_label";
    case DW_TAG_function_template:
      return "DW_TAG_function_template";
    case DW_TAG_class_template:
      return "DW_TAG_class_template";
    case DW_TAG_GNU_BINCL:
      return "DW_TAG_GNU_BINCL";
    case DW_TAG_GNU_EINCL:
      return "DW_TAG_GNU_EINCL";
    default:
      return "DW_TAG_<unknown>";
    }
}

/* Convert a DWARF attribute code into its string name.  */

static const char *
dwarf_attr_name (unsigned int attr)
{
  switch (attr)
    {
    case DW_AT_sibling:
      return "DW_AT_sibling";
    case DW_AT_location:
      return "DW_AT_location";
    case DW_AT_name:
      return "DW_AT_name";
    case DW_AT_ordering:
      return "DW_AT_ordering";
    case DW_AT_subscr_data:
      return "DW_AT_subscr_data";
    case DW_AT_byte_size:
      return "DW_AT_byte_size";
    case DW_AT_bit_offset:
      return "DW_AT_bit_offset";
    case DW_AT_bit_size:
      return "DW_AT_bit_size";
    case DW_AT_element_list:
      return "DW_AT_element_list";
    case DW_AT_stmt_list:
      return "DW_AT_stmt_list";
    case DW_AT_low_pc:
      return "DW_AT_low_pc";
    case DW_AT_high_pc:
      return "DW_AT_high_pc";
    case DW_AT_language:
      return "DW_AT_language";
    case DW_AT_member:
      return "DW_AT_member";
    case DW_AT_discr:
      return "DW_AT_discr";
    case DW_AT_discr_value:
      return "DW_AT_discr_value";
    case DW_AT_visibility:
      return "DW_AT_visibility";
    case DW_AT_import:
      return "DW_AT_import";
    case DW_AT_string_length:
      return "DW_AT_string_length";
    case DW_AT_common_reference:
      return "DW_AT_common_reference";
    case DW_AT_comp_dir:
      return "DW_AT_comp_dir";
    case DW_AT_const_value:
      return "DW_AT_const_value";
    case DW_AT_containing_type:
      return "DW_AT_containing_type";
    case DW_AT_default_value:
      return "DW_AT_default_value";
    case DW_AT_inline:
      return "DW_AT_inline";
    case DW_AT_is_optional:
      return "DW_AT_is_optional";
    case DW_AT_lower_bound:
      return "DW_AT_lower_bound";
    case DW_AT_producer:
      return "DW_AT_producer";
    case DW_AT_prototyped:
      return "DW_AT_prototyped";
    case DW_AT_return_addr:
      return "DW_AT_return_addr";
    case DW_AT_start_scope:
      return "DW_AT_start_scope";
    case DW_AT_stride_size:
      return "DW_AT_stride_size";
    case DW_AT_upper_bound:
      return "DW_AT_upper_bound";
    case DW_AT_abstract_origin:
      return "DW_AT_abstract_origin";
    case DW_AT_accessibility:
      return "DW_AT_accessibility";
    case DW_AT_address_class:
      return "DW_AT_address_class";
    case DW_AT_artificial:
      return "DW_AT_artificial";
    case DW_AT_base_types:
      return "DW_AT_base_types";
    case DW_AT_calling_convention:
      return "DW_AT_calling_convention";
    case DW_AT_count:
      return "DW_AT_count";
    case DW_AT_data_member_location:
      return "DW_AT_data_member_location";
    case DW_AT_decl_column:
      return "DW_AT_decl_column";
    case DW_AT_decl_file:
      return "DW_AT_decl_file";
    case DW_AT_decl_line:
      return "DW_AT_decl_line";
    case DW_AT_declaration:
      return "DW_AT_declaration";
    case DW_AT_discr_list:
      return "DW_AT_discr_list";
    case DW_AT_encoding:
      return "DW_AT_encoding";
    case DW_AT_external:
      return "DW_AT_external";
    case DW_AT_frame_base:
      return "DW_AT_frame_base";
    case DW_AT_friend:
      return "DW_AT_friend";
    case DW_AT_identifier_case:
      return "DW_AT_identifier_case";
    case DW_AT_macro_info:
      return "DW_AT_macro_info";
    case DW_AT_namelist_items:
      return "DW_AT_namelist_items";
    case DW_AT_priority:
      return "DW_AT_priority";
    case DW_AT_segment:
      return "DW_AT_segment";
    case DW_AT_specification:
      return "DW_AT_specification";
    case DW_AT_static_link:
      return "DW_AT_static_link";
    case DW_AT_type:
      return "DW_AT_type";
    case DW_AT_use_location:
      return "DW_AT_use_location";
    case DW_AT_variable_parameter:
      return "DW_AT_variable_parameter";
    case DW_AT_virtuality:
      return "DW_AT_virtuality";
    case DW_AT_vtable_elem_location:
      return "DW_AT_vtable_elem_location";

    case DW_AT_allocated:
      return "DW_AT_allocated";
    case DW_AT_associated:
      return "DW_AT_associated";
    case DW_AT_data_location:
      return "DW_AT_data_location";
    case DW_AT_stride:
      return "DW_AT_stride";
    case DW_AT_entry_pc:
      return "DW_AT_entry_pc";
    case DW_AT_use_UTF8:
      return "DW_AT_use_UTF8";
    case DW_AT_extension:
      return "DW_AT_extension";
    case DW_AT_ranges:
      return "DW_AT_ranges";
    case DW_AT_trampoline:
      return "DW_AT_trampoline";
    case DW_AT_call_column:
      return "DW_AT_call_column";
    case DW_AT_call_file:
      return "DW_AT_call_file";
    case DW_AT_call_line:
      return "DW_AT_call_line";

    case DW_AT_MIPS_fde:
      return "DW_AT_MIPS_fde";
    case DW_AT_MIPS_loop_begin:
      return "DW_AT_MIPS_loop_begin";
    case DW_AT_MIPS_tail_loop_begin:
      return "DW_AT_MIPS_tail_loop_begin";
    case DW_AT_MIPS_epilog_begin:
      return "DW_AT_MIPS_epilog_begin";
    case DW_AT_MIPS_loop_unroll_factor:
      return "DW_AT_MIPS_loop_unroll_factor";
    case DW_AT_MIPS_software_pipeline_depth:
      return "DW_AT_MIPS_software_pipeline_depth";
    case DW_AT_MIPS_linkage_name:
      return "DW_AT_MIPS_linkage_name";
    case DW_AT_MIPS_stride:
      return "DW_AT_MIPS_stride";
    case DW_AT_MIPS_abstract_name:
      return "DW_AT_MIPS_abstract_name";
    case DW_AT_MIPS_clone_origin:
      return "DW_AT_MIPS_clone_origin";
    case DW_AT_MIPS_has_inlines:
      return "DW_AT_MIPS_has_inlines";

    case DW_AT_sf_names:
      return "DW_AT_sf_names";
    case DW_AT_src_info:
      return "DW_AT_src_info";
    case DW_AT_mac_info:
      return "DW_AT_mac_info";
    case DW_AT_src_coords:
      return "DW_AT_src_coords";
    case DW_AT_body_begin:
      return "DW_AT_body_begin";
    case DW_AT_body_end:
      return "DW_AT_body_end";
    case DW_AT_GNU_vector:
      return "DW_AT_GNU_vector";

    case DW_AT_VMS_rtnbeg_pd_address:
      return "DW_AT_VMS_rtnbeg_pd_address";

    default:
      return "DW_AT_<unknown>";
    }
}

/* Convert a DWARF value form code into its string name.  */

static const char *
dwarf_form_name (unsigned int form)
{
  switch (form)
    {
    case DW_FORM_addr:
      return "DW_FORM_addr";
    case DW_FORM_block2:
      return "DW_FORM_block2";
    case DW_FORM_block4:
      return "DW_FORM_block4";
    case DW_FORM_data2:
      return "DW_FORM_data2";
    case DW_FORM_data4:
      return "DW_FORM_data4";
    case DW_FORM_data8:
      return "DW_FORM_data8";
    case DW_FORM_string:
      return "DW_FORM_string";
    case DW_FORM_block:
      return "DW_FORM_block";
    case DW_FORM_block1:
      return "DW_FORM_block1";
    case DW_FORM_data1:
      return "DW_FORM_data1";
    case DW_FORM_flag:
      return "DW_FORM_flag";
    case DW_FORM_sdata:
      return "DW_FORM_sdata";
    case DW_FORM_strp:
      return "DW_FORM_strp";
    case DW_FORM_udata:
      return "DW_FORM_udata";
    case DW_FORM_ref_addr:
      return "DW_FORM_ref_addr";
    case DW_FORM_ref1:
      return "DW_FORM_ref1";
    case DW_FORM_ref2:
      return "DW_FORM_ref2";
    case DW_FORM_ref4:
      return "DW_FORM_ref4";
    case DW_FORM_ref8:
      return "DW_FORM_ref8";
    case DW_FORM_ref_udata:
      return "DW_FORM_ref_udata";
    case DW_FORM_indirect:
      return "DW_FORM_indirect";
    default:
      return "DW_FORM_<unknown>";
    }
}

/* Determine the "ultimate origin" of a decl.  The decl may be an inlined
   instance of an inlined instance of a decl which is local to an inline
   function, so we have to trace all of the way back through the origin chain
   to find out what sort of node actually served as the original seed for the
   given block.  */

static tree
decl_ultimate_origin (tree decl)
{
  if (!CODE_CONTAINS_STRUCT (TREE_CODE (decl), TS_DECL_COMMON))
    return NULL_TREE;

  /* output_inline_function sets DECL_ABSTRACT_ORIGIN for all the
     nodes in the function to point to themselves; ignore that if
     we're trying to output the abstract instance of this function.  */
  if (DECL_ABSTRACT (decl) && DECL_ABSTRACT_ORIGIN (decl) == decl)
    return NULL_TREE;

  /* Since the DECL_ABSTRACT_ORIGIN for a DECL is supposed to be the
     most distant ancestor, this should never happen.  */
  gcc_assert (!DECL_FROM_INLINE (DECL_ORIGIN (decl)));

  return DECL_ABSTRACT_ORIGIN (decl);
}

/* Determine the "ultimate origin" of a block.  The block may be an inlined
   instance of an inlined instance of a block which is local to an inline
   function, so we have to trace all of the way back through the origin chain
   to find out what sort of node actually served as the original seed for the
   given block.  */

static tree
block_ultimate_origin (tree block)
{
  tree immediate_origin = BLOCK_ABSTRACT_ORIGIN (block);

  /* output_inline_function sets BLOCK_ABSTRACT_ORIGIN for all the
     nodes in the function to point to themselves; ignore that if
     we're trying to output the abstract instance of this function.  */
  if (BLOCK_ABSTRACT (block) && immediate_origin == block)
    return NULL_TREE;

  if (immediate_origin == NULL_TREE)
    return NULL_TREE;
  else
    {
      tree ret_val;
      tree lookahead = immediate_origin;

      do
	{
	  ret_val = lookahead;
	  lookahead = (TREE_CODE (ret_val) == BLOCK
		       ? BLOCK_ABSTRACT_ORIGIN (ret_val) : NULL);
	}
      while (lookahead != NULL && lookahead != ret_val);
      
      /* The block's abstract origin chain may not be the *ultimate* origin of
	 the block. It could lead to a DECL that has an abstract origin set.
	 If so, we want that DECL's abstract origin (which is what DECL_ORIGIN
	 will give us if it has one).  Note that DECL's abstract origins are
	 supposed to be the most distant ancestor (or so decl_ultimate_origin
	 claims), so we don't need to loop following the DECL origins.  */
      if (DECL_P (ret_val))
	return DECL_ORIGIN (ret_val);

      return ret_val;
    }
}

/* Get the class to which DECL belongs, if any.  In g++, the DECL_CONTEXT
   of a virtual function may refer to a base class, so we check the 'this'
   parameter.  */

static tree
decl_class_context (tree decl)
{
  tree context = NULL_TREE;

  if (TREE_CODE (decl) != FUNCTION_DECL || ! DECL_VINDEX (decl))
    context = DECL_CONTEXT (decl);
  else
    context = TYPE_MAIN_VARIANT
      (TREE_TYPE (TREE_VALUE (TYPE_ARG_TYPES (TREE_TYPE (decl)))));

  if (context && !TYPE_P (context))
    context = NULL_TREE;

  return context;
}

/* Add an attribute/value pair to a DIE.  */

static inline void
add_dwarf_attr (dw_die_ref die, dw_attr_ref attr)
{
  /* Maybe this should be an assert?  */
  if (die == NULL)
    return;
  
  if (die->die_attr == NULL)
    die->die_attr = VEC_alloc (dw_attr_node, gc, 1);
  VEC_safe_push (dw_attr_node, gc, die->die_attr, attr);
}

static inline enum dw_val_class
AT_class (dw_attr_ref a)
{
  return a->dw_attr_val.val_class;
}

/* Add a flag value attribute to a DIE.  */

static inline void
add_AT_flag (dw_die_ref die, enum dwarf_attribute attr_kind, unsigned int flag)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_flag;
  attr.dw_attr_val.v.val_flag = flag;
  add_dwarf_attr (die, &attr);
}

static inline unsigned
AT_flag (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_flag);
  return a->dw_attr_val.v.val_flag;
}

/* Add a signed integer attribute value to a DIE.  */

static inline void
add_AT_int (dw_die_ref die, enum dwarf_attribute attr_kind, HOST_WIDE_INT int_val)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_const;
  attr.dw_attr_val.v.val_int = int_val;
  add_dwarf_attr (die, &attr);
}

static inline HOST_WIDE_INT
AT_int (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_const);
  return a->dw_attr_val.v.val_int;
}

/* Add an unsigned integer attribute value to a DIE.  */

static inline void
add_AT_unsigned (dw_die_ref die, enum dwarf_attribute attr_kind,
		 unsigned HOST_WIDE_INT unsigned_val)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_unsigned_const;
  attr.dw_attr_val.v.val_unsigned = unsigned_val;
  add_dwarf_attr (die, &attr);
}

static inline unsigned HOST_WIDE_INT
AT_unsigned (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_unsigned_const);
  return a->dw_attr_val.v.val_unsigned;
}

/* Add an unsigned double integer attribute value to a DIE.  */

static inline void
add_AT_long_long (dw_die_ref die, enum dwarf_attribute attr_kind,
		  long unsigned int val_hi, long unsigned int val_low)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_long_long;
  attr.dw_attr_val.v.val_long_long.hi = val_hi;
  attr.dw_attr_val.v.val_long_long.low = val_low;
  add_dwarf_attr (die, &attr);
}

/* Add a floating point attribute value to a DIE and return it.  */

static inline void
add_AT_vec (dw_die_ref die, enum dwarf_attribute attr_kind,
	    unsigned int length, unsigned int elt_size, unsigned char *array)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_vec;
  attr.dw_attr_val.v.val_vec.length = length;
  attr.dw_attr_val.v.val_vec.elt_size = elt_size;
  attr.dw_attr_val.v.val_vec.array = array;
  add_dwarf_attr (die, &attr);
}

/* Hash and equality functions for debug_str_hash.  */

static hashval_t
debug_str_do_hash (const void *x)
{
  return htab_hash_string (((const struct indirect_string_node *)x)->str);
}

static int
debug_str_eq (const void *x1, const void *x2)
{
  return strcmp ((((const struct indirect_string_node *)x1)->str),
		 (const char *)x2) == 0;
}

/* Add a string attribute value to a DIE.  */

static inline void
add_AT_string (dw_die_ref die, enum dwarf_attribute attr_kind, const char *str)
{
  dw_attr_node attr;
  struct indirect_string_node *node;
  void **slot;

  if (! debug_str_hash)
    debug_str_hash = htab_create_ggc (10, debug_str_do_hash,
				      debug_str_eq, NULL);

  slot = htab_find_slot_with_hash (debug_str_hash, str,
				   htab_hash_string (str), INSERT);
  if (*slot == NULL)
    *slot = ggc_alloc_cleared (sizeof (struct indirect_string_node));
  node = (struct indirect_string_node *) *slot;
  node->str = ggc_strdup (str);
  node->refcount++;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_str;
  attr.dw_attr_val.v.val_str = node;
  add_dwarf_attr (die, &attr);
}

static inline const char *
AT_string (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_str);
  return a->dw_attr_val.v.val_str->str;
}

/* Find out whether a string should be output inline in DIE
   or out-of-line in .debug_str section.  */

static int
AT_string_form (dw_attr_ref a)
{
  struct indirect_string_node *node;
  unsigned int len;
  char label[32];

  gcc_assert (a && AT_class (a) == dw_val_class_str);

  node = a->dw_attr_val.v.val_str;
  if (node->form)
    return node->form;

  len = strlen (node->str) + 1;

  /* If the string is shorter or equal to the size of the reference, it is
     always better to put it inline.  */
  if (len <= DWARF_OFFSET_SIZE || node->refcount == 0)
    return node->form = DW_FORM_string;

  /* If we cannot expect the linker to merge strings in .debug_str
     section, only put it into .debug_str if it is worth even in this
     single module.  */
  if ((debug_str_section->common.flags & SECTION_MERGE) == 0
      && (len - DWARF_OFFSET_SIZE) * node->refcount <= len)
    return node->form = DW_FORM_string;

  ASM_GENERATE_INTERNAL_LABEL (label, "LASF", dw2_string_counter);
  ++dw2_string_counter;
  node->label = xstrdup (label);

  return node->form = DW_FORM_strp;
}

/* Add a DIE reference attribute value to a DIE.  */

static inline void
add_AT_die_ref (dw_die_ref die, enum dwarf_attribute attr_kind, dw_die_ref targ_die)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_die_ref;
  attr.dw_attr_val.v.val_die_ref.die = targ_die;
  attr.dw_attr_val.v.val_die_ref.external = 0;
  add_dwarf_attr (die, &attr);
}

/* Add an AT_specification attribute to a DIE, and also make the back
   pointer from the specification to the definition.  */

static inline void
add_AT_specification (dw_die_ref die, dw_die_ref targ_die)
{
  add_AT_die_ref (die, DW_AT_specification, targ_die);
  gcc_assert (!targ_die->die_definition);
  targ_die->die_definition = die;
}

static inline dw_die_ref
AT_ref (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_die_ref);
  return a->dw_attr_val.v.val_die_ref.die;
}

static inline int
AT_ref_external (dw_attr_ref a)
{
  if (a && AT_class (a) == dw_val_class_die_ref)
    return a->dw_attr_val.v.val_die_ref.external;

  return 0;
}

static inline void
set_AT_ref_external (dw_attr_ref a, int i)
{
  gcc_assert (a && AT_class (a) == dw_val_class_die_ref);
  a->dw_attr_val.v.val_die_ref.external = i;
}

/* Add an FDE reference attribute value to a DIE.  */

static inline void
add_AT_fde_ref (dw_die_ref die, enum dwarf_attribute attr_kind, unsigned int targ_fde)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_fde_ref;
  attr.dw_attr_val.v.val_fde_index = targ_fde;
  add_dwarf_attr (die, &attr);
}

/* Add a location description attribute value to a DIE.  */

static inline void
add_AT_loc (dw_die_ref die, enum dwarf_attribute attr_kind, dw_loc_descr_ref loc)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_loc;
  attr.dw_attr_val.v.val_loc = loc;
  add_dwarf_attr (die, &attr);
}

static inline dw_loc_descr_ref
AT_loc (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_loc);
  return a->dw_attr_val.v.val_loc;
}

static inline void
add_AT_loc_list (dw_die_ref die, enum dwarf_attribute attr_kind, dw_loc_list_ref loc_list)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_loc_list;
  attr.dw_attr_val.v.val_loc_list = loc_list;
  add_dwarf_attr (die, &attr);
  have_location_lists = true;
}

static inline dw_loc_list_ref
AT_loc_list (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_loc_list);
  return a->dw_attr_val.v.val_loc_list;
}

/* Add an address constant attribute value to a DIE.  */

static inline void
add_AT_addr (dw_die_ref die, enum dwarf_attribute attr_kind, rtx addr)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_addr;
  attr.dw_attr_val.v.val_addr = addr;
  add_dwarf_attr (die, &attr);
}

/* Get the RTX from to an address DIE attribute.  */

static inline rtx
AT_addr (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_addr);
  return a->dw_attr_val.v.val_addr;
}

/* Add a file attribute value to a DIE.  */

static inline void
add_AT_file (dw_die_ref die, enum dwarf_attribute attr_kind,
	     struct dwarf_file_data *fd)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_file;
  attr.dw_attr_val.v.val_file = fd;
  add_dwarf_attr (die, &attr);
}

/* Get the dwarf_file_data from a file DIE attribute.  */

static inline struct dwarf_file_data *
AT_file (dw_attr_ref a)
{
  gcc_assert (a && AT_class (a) == dw_val_class_file);
  return a->dw_attr_val.v.val_file;
}

/* Add a label identifier attribute value to a DIE.  */

static inline void
add_AT_lbl_id (dw_die_ref die, enum dwarf_attribute attr_kind, const char *lbl_id)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_lbl_id;
  attr.dw_attr_val.v.val_lbl_id = xstrdup (lbl_id);
  add_dwarf_attr (die, &attr);
}

/* Add a section offset attribute value to a DIE, an offset into the
   debug_line section.  */

static inline void
add_AT_lineptr (dw_die_ref die, enum dwarf_attribute attr_kind,
		const char *label)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_lineptr;
  attr.dw_attr_val.v.val_lbl_id = xstrdup (label);
  add_dwarf_attr (die, &attr);
}

/* Add a section offset attribute value to a DIE, an offset into the
   debug_macinfo section.  */

static inline void
add_AT_macptr (dw_die_ref die, enum dwarf_attribute attr_kind,
	       const char *label)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_macptr;
  attr.dw_attr_val.v.val_lbl_id = xstrdup (label);
  add_dwarf_attr (die, &attr);
}

/* Add an offset attribute value to a DIE.  */

static inline void
add_AT_offset (dw_die_ref die, enum dwarf_attribute attr_kind,
	       unsigned HOST_WIDE_INT offset)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_offset;
  attr.dw_attr_val.v.val_offset = offset;
  add_dwarf_attr (die, &attr);
}

/* Add an range_list attribute value to a DIE.  */

static void
add_AT_range_list (dw_die_ref die, enum dwarf_attribute attr_kind,
		   long unsigned int offset)
{
  dw_attr_node attr;

  attr.dw_attr = attr_kind;
  attr.dw_attr_val.val_class = dw_val_class_range_list;
  attr.dw_attr_val.v.val_offset = offset;
  add_dwarf_attr (die, &attr);
}

static inline const char *
AT_lbl (dw_attr_ref a)
{
  gcc_assert (a && (AT_class (a) == dw_val_class_lbl_id
		    || AT_class (a) == dw_val_class_lineptr
		    || AT_class (a) == dw_val_class_macptr));
  return a->dw_attr_val.v.val_lbl_id;
}

/* Get the attribute of type attr_kind.  */

static dw_attr_ref
get_AT (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a;
  unsigned ix;
  dw_die_ref spec = NULL;

  if (! die)
    return NULL;

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    if (a->dw_attr == attr_kind)
      return a;
    else if (a->dw_attr == DW_AT_specification
	     || a->dw_attr == DW_AT_abstract_origin)
      spec = AT_ref (a);
  
  if (spec)
    return get_AT (spec, attr_kind);

  return NULL;
}

/* Return the "low pc" attribute value, typically associated with a subprogram
   DIE.  Return null if the "low pc" attribute is either not present, or if it
   cannot be represented as an assembler label identifier.  */

static inline const char *
get_AT_low_pc (dw_die_ref die)
{
  dw_attr_ref a = get_AT (die, DW_AT_low_pc);

  return a ? AT_lbl (a) : NULL;
}

/* Return the "high pc" attribute value, typically associated with a subprogram
   DIE.  Return null if the "high pc" attribute is either not present, or if it
   cannot be represented as an assembler label identifier.  */

static inline const char *
get_AT_hi_pc (dw_die_ref die)
{
  dw_attr_ref a = get_AT (die, DW_AT_high_pc);

  return a ? AT_lbl (a) : NULL;
}

/* Return the value of the string attribute designated by ATTR_KIND, or
   NULL if it is not present.  */

static inline const char *
get_AT_string (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a = get_AT (die, attr_kind);

  return a ? AT_string (a) : NULL;
}

/* Return the value of the flag attribute designated by ATTR_KIND, or -1
   if it is not present.  */

static inline int
get_AT_flag (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a = get_AT (die, attr_kind);

  return a ? AT_flag (a) : 0;
}

/* Return the value of the unsigned attribute designated by ATTR_KIND, or 0
   if it is not present.  */

static inline unsigned
get_AT_unsigned (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a = get_AT (die, attr_kind);

  return a ? AT_unsigned (a) : 0;
}

static inline dw_die_ref
get_AT_ref (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a = get_AT (die, attr_kind);

  return a ? AT_ref (a) : NULL;
}

static inline struct dwarf_file_data *
get_AT_file (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a = get_AT (die, attr_kind);

  return a ? AT_file (a) : NULL;
}

/* Return TRUE if the language is C or C++.  */

static inline bool
is_c_family (void)
{
  unsigned int lang = get_AT_unsigned (comp_unit_die, DW_AT_language);

  return (lang == DW_LANG_C || lang == DW_LANG_C89 || lang == DW_LANG_ObjC
	  || lang == DW_LANG_C99
	  || lang == DW_LANG_C_plus_plus || lang == DW_LANG_ObjC_plus_plus);
}

/* Return TRUE if the language is C++.  */

static inline bool
is_cxx (void)
{
  unsigned int lang = get_AT_unsigned (comp_unit_die, DW_AT_language);
  
  return lang == DW_LANG_C_plus_plus || lang == DW_LANG_ObjC_plus_plus;
}

/* Return TRUE if the language is Fortran.  */

static inline bool
is_fortran (void)
{
  unsigned int lang = get_AT_unsigned (comp_unit_die, DW_AT_language);

  return (lang == DW_LANG_Fortran77
	  || lang == DW_LANG_Fortran90
	  || lang == DW_LANG_Fortran95);
}

/* Return TRUE if the language is Java.  */

static inline bool
is_java (void)
{
  unsigned int lang = get_AT_unsigned (comp_unit_die, DW_AT_language);

  return lang == DW_LANG_Java;
}

/* Return TRUE if the language is Ada.  */

static inline bool
is_ada (void)
{
  unsigned int lang = get_AT_unsigned (comp_unit_die, DW_AT_language);

  return lang == DW_LANG_Ada95 || lang == DW_LANG_Ada83;
}

/* Remove the specified attribute if present.  */

static void
remove_AT (dw_die_ref die, enum dwarf_attribute attr_kind)
{
  dw_attr_ref a;
  unsigned ix;

  if (! die)
    return;

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    if (a->dw_attr == attr_kind)
      {
	if (AT_class (a) == dw_val_class_str)
	  if (a->dw_attr_val.v.val_str->refcount)
	    a->dw_attr_val.v.val_str->refcount--;

	/* VEC_ordered_remove should help reduce the number of abbrevs
	   that are needed.  */
	VEC_ordered_remove (dw_attr_node, die->die_attr, ix);
	return;
      }
}

/* Remove CHILD from its parent.  PREV must have the property that
   PREV->DIE_SIB == CHILD.  Does not alter CHILD.  */

static void
remove_child_with_prev (dw_die_ref child, dw_die_ref prev)
{
  gcc_assert (child->die_parent == prev->die_parent);
  gcc_assert (prev->die_sib == child);
  if (prev == child)
    {
      gcc_assert (child->die_parent->die_child == child);
      prev = NULL;
    }
  else
    prev->die_sib = child->die_sib;
  if (child->die_parent->die_child == child)
    child->die_parent->die_child = prev;
}

/* Remove child DIE whose die_tag is TAG.  Do nothing if no child
   matches TAG.  */

static void
remove_child_TAG (dw_die_ref die, enum dwarf_tag tag)
{
  dw_die_ref c;
  
  c = die->die_child;
  if (c) do {
    dw_die_ref prev = c;
    c = c->die_sib;
    while (c->die_tag == tag)
      {
	remove_child_with_prev (c, prev);
	/* Might have removed every child.  */
	if (c == c->die_sib)
	  return;
	c = c->die_sib;
      }
  } while (c != die->die_child);
}

/* Add a CHILD_DIE as the last child of DIE.  */

static void
add_child_die (dw_die_ref die, dw_die_ref child_die)
{
  /* FIXME this should probably be an assert.  */
  if (! die || ! child_die)
    return;
  gcc_assert (die != child_die);

  child_die->die_parent = die;
  if (die->die_child)
    {
      child_die->die_sib = die->die_child->die_sib;
      die->die_child->die_sib = child_die;
    }
  else
    child_die->die_sib = child_die;
  die->die_child = child_die;
}

/* Move CHILD, which must be a child of PARENT or the DIE for which PARENT
   is the specification, to the end of PARENT's list of children.  
   This is done by removing and re-adding it.  */

static void
splice_child_die (dw_die_ref parent, dw_die_ref child)
{
  dw_die_ref p;

  /* We want the declaration DIE from inside the class, not the
     specification DIE at toplevel.  */
  if (child->die_parent != parent)
    {
      dw_die_ref tmp = get_AT_ref (child, DW_AT_specification);

      if (tmp)
	child = tmp;
    }

  gcc_assert (child->die_parent == parent
	      || (child->die_parent
		  == get_AT_ref (parent, DW_AT_specification)));
  
  for (p = child->die_parent->die_child; ; p = p->die_sib)
    if (p->die_sib == child)
      {
	remove_child_with_prev (child, p);
	break;
      }

  add_child_die (parent, child);
}

/* Return a pointer to a newly created DIE node.  */

static inline dw_die_ref
new_die (enum dwarf_tag tag_value, dw_die_ref parent_die, tree t)
{
  dw_die_ref die = ggc_alloc_cleared (sizeof (die_node));

  die->die_tag = tag_value;

  if (parent_die != NULL)
    add_child_die (parent_die, die);
  else
    {
      limbo_die_node *limbo_node;

      limbo_node = ggc_alloc_cleared (sizeof (limbo_die_node));
      limbo_node->die = die;
      limbo_node->created_for = t;
      limbo_node->next = limbo_die_list;
      limbo_die_list = limbo_node;
    }

  return die;
}

/* Return the DIE associated with the given type specifier.  */

static inline dw_die_ref
lookup_type_die (tree type)
{
  return TYPE_SYMTAB_DIE (type);
}

/* Equate a DIE to a given type specifier.  */

static inline void
equate_type_number_to_die (tree type, dw_die_ref type_die)
{
  TYPE_SYMTAB_DIE (type) = type_die;
}

/* Returns a hash value for X (which really is a die_struct).  */

static hashval_t
decl_die_table_hash (const void *x)
{
  return (hashval_t) ((const dw_die_ref) x)->decl_id;
}

/* Return nonzero if decl_id of die_struct X is the same as UID of decl *Y.  */

static int
decl_die_table_eq (const void *x, const void *y)
{
  return (((const dw_die_ref) x)->decl_id == DECL_UID ((const tree) y));
}

/* Return the DIE associated with a given declaration.  */

static inline dw_die_ref
lookup_decl_die (tree decl)
{
  return htab_find_with_hash (decl_die_table, decl, DECL_UID (decl));
}

/* Returns a hash value for X (which really is a var_loc_list).  */

static hashval_t
decl_loc_table_hash (const void *x)
{
  return (hashval_t) ((const var_loc_list *) x)->decl_id;
}

/* Return nonzero if decl_id of var_loc_list X is the same as
   UID of decl *Y.  */

static int
decl_loc_table_eq (const void *x, const void *y)
{
  return (((const var_loc_list *) x)->decl_id == DECL_UID ((const tree) y));
}

/* Return the var_loc list associated with a given declaration.  */

static inline var_loc_list *
lookup_decl_loc (tree decl)
{
  return htab_find_with_hash (decl_loc_table, decl, DECL_UID (decl));
}

/* Equate a DIE to a particular declaration.  */

static void
equate_decl_number_to_die (tree decl, dw_die_ref decl_die)
{
  unsigned int decl_id = DECL_UID (decl);
  void **slot;

  slot = htab_find_slot_with_hash (decl_die_table, decl, decl_id, INSERT);
  *slot = decl_die;
  decl_die->decl_id = decl_id;
}

/* Add a variable location node to the linked list for DECL.  */

static void
add_var_loc_to_decl (tree decl, struct var_loc_node *loc)
{
  unsigned int decl_id = DECL_UID (decl);
  var_loc_list *temp;
  void **slot;

  slot = htab_find_slot_with_hash (decl_loc_table, decl, decl_id, INSERT);
  if (*slot == NULL)
    {
      temp = ggc_alloc_cleared (sizeof (var_loc_list));
      temp->decl_id = decl_id;
      *slot = temp;
    }
  else
    temp = *slot;

  if (temp->last)
    {
      /* If the current location is the same as the end of the list,
	 we have nothing to do.  */
      if (!rtx_equal_p (NOTE_VAR_LOCATION_LOC (temp->last->var_loc_note),
			NOTE_VAR_LOCATION_LOC (loc->var_loc_note)))
	{
	  /* Add LOC to the end of list and update LAST.  */
	  temp->last->next = loc;
	  temp->last = loc;
	}
    }
  /* Do not add empty location to the beginning of the list.  */
  else if (NOTE_VAR_LOCATION_LOC (loc->var_loc_note) != NULL_RTX)
    {
      temp->first = loc;
      temp->last = loc;
    }
}

/* Keep track of the number of spaces used to indent the
   output of the debugging routines that print the structure of
   the DIE internal representation.  */
static int print_indent;

/* Indent the line the number of spaces given by print_indent.  */

static inline void
print_spaces (FILE *outfile)
{
  fprintf (outfile, "%*s", print_indent, "");
}

/* Print the information associated with a given DIE, and its children.
   This routine is a debugging aid only.  */

static void
print_die (dw_die_ref die, FILE *outfile)
{
  dw_attr_ref a;
  dw_die_ref c;
  unsigned ix;

  print_spaces (outfile);
  fprintf (outfile, "DIE %4lu: %s\n",
	   die->die_offset, dwarf_tag_name (die->die_tag));
  print_spaces (outfile);
  fprintf (outfile, "  abbrev id: %lu", die->die_abbrev);
  fprintf (outfile, " offset: %lu\n", die->die_offset);

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    {
      print_spaces (outfile);
      fprintf (outfile, "  %s: ", dwarf_attr_name (a->dw_attr));

      switch (AT_class (a))
	{
	case dw_val_class_addr:
	  fprintf (outfile, "address");
	  break;
	case dw_val_class_offset:
	  fprintf (outfile, "offset");
	  break;
	case dw_val_class_loc:
	  fprintf (outfile, "location descriptor");
	  break;
	case dw_val_class_loc_list:
	  fprintf (outfile, "location list -> label:%s",
		   AT_loc_list (a)->ll_symbol);
	  break;
	case dw_val_class_range_list:
	  fprintf (outfile, "range list");
	  break;
	case dw_val_class_const:
	  fprintf (outfile, HOST_WIDE_INT_PRINT_DEC, AT_int (a));
	  break;
	case dw_val_class_unsigned_const:
	  fprintf (outfile, HOST_WIDE_INT_PRINT_UNSIGNED, AT_unsigned (a));
	  break;
	case dw_val_class_long_long:
	  fprintf (outfile, "constant (%lu,%lu)",
		   a->dw_attr_val.v.val_long_long.hi,
		   a->dw_attr_val.v.val_long_long.low);
	  break;
	case dw_val_class_vec:
	  fprintf (outfile, "floating-point or vector constant");
	  break;
	case dw_val_class_flag:
	  fprintf (outfile, "%u", AT_flag (a));
	  break;
	case dw_val_class_die_ref:
	  if (AT_ref (a) != NULL)
	    {
	      if (AT_ref (a)->die_symbol)
		fprintf (outfile, "die -> label: %s", AT_ref (a)->die_symbol);
	      else
		fprintf (outfile, "die -> %lu", AT_ref (a)->die_offset);
	    }
	  else
	    fprintf (outfile, "die -> <null>");
	  break;
	case dw_val_class_lbl_id:
	case dw_val_class_lineptr:
	case dw_val_class_macptr:
	  fprintf (outfile, "label: %s", AT_lbl (a));
	  break;
	case dw_val_class_str:
	  if (AT_string (a) != NULL)
	    fprintf (outfile, "\"%s\"", AT_string (a));
	  else
	    fprintf (outfile, "<null>");
	  break;
	case dw_val_class_file:
	  fprintf (outfile, "\"%s\" (%d)", AT_file (a)->filename,
		   AT_file (a)->emitted_number);
	  break;
	default:
	  break;
	}

      fprintf (outfile, "\n");
    }

  if (die->die_child != NULL)
    {
      print_indent += 4;
      FOR_EACH_CHILD (die, c, print_die (c, outfile));
      print_indent -= 4;
    }
  if (print_indent == 0)
    fprintf (outfile, "\n");
}

/* Print the contents of the source code line number correspondence table.
   This routine is a debugging aid only.  */

static void
print_dwarf_line_table (FILE *outfile)
{
  unsigned i;
  dw_line_info_ref line_info;

  fprintf (outfile, "\n\nDWARF source line information\n");
  for (i = 1; i < line_info_table_in_use; i++)
    {
      line_info = &line_info_table[i];
      fprintf (outfile, "%5d: %4ld %6ld\n", i,
	       line_info->dw_file_num,
	       line_info->dw_line_num);
    }

  fprintf (outfile, "\n\n");
}

/* Print the information collected for a given DIE.  */

void
debug_dwarf_die (dw_die_ref die)
{
  print_die (die, stderr);
}

/* Print all DWARF information collected for the compilation unit.
   This routine is a debugging aid only.  */

void
debug_dwarf (void)
{
  print_indent = 0;
  print_die (comp_unit_die, stderr);
  if (! DWARF2_ASM_LINE_DEBUG_INFO)
    print_dwarf_line_table (stderr);
}

/* Start a new compilation unit DIE for an include file.  OLD_UNIT is the CU
   for the enclosing include file, if any.  BINCL_DIE is the DW_TAG_GNU_BINCL
   DIE that marks the start of the DIEs for this include file.  */

static dw_die_ref
push_new_compile_unit (dw_die_ref old_unit, dw_die_ref bincl_die)
{
  const char *filename = get_AT_string (bincl_die, DW_AT_name);
  dw_die_ref new_unit = gen_compile_unit_die (filename);

  new_unit->die_sib = old_unit;
  return new_unit;
}

/* Close an include-file CU and reopen the enclosing one.  */

static dw_die_ref
pop_compile_unit (dw_die_ref old_unit)
{
  dw_die_ref new_unit = old_unit->die_sib;

  old_unit->die_sib = NULL;
  return new_unit;
}

#define CHECKSUM(FOO) md5_process_bytes (&(FOO), sizeof (FOO), ctx)
#define CHECKSUM_STRING(FOO) md5_process_bytes ((FOO), strlen (FOO), ctx)

/* Calculate the checksum of a location expression.  */

static inline void
loc_checksum (dw_loc_descr_ref loc, struct md5_ctx *ctx)
{
  CHECKSUM (loc->dw_loc_opc);
  CHECKSUM (loc->dw_loc_oprnd1);
  CHECKSUM (loc->dw_loc_oprnd2);
}

/* Calculate the checksum of an attribute.  */

static void
attr_checksum (dw_attr_ref at, struct md5_ctx *ctx, int *mark)
{
  dw_loc_descr_ref loc;
  rtx r;

  CHECKSUM (at->dw_attr);

  /* We don't care that this was compiled with a different compiler
     snapshot; if the output is the same, that's what matters.  */
  if (at->dw_attr == DW_AT_producer)
    return;

  switch (AT_class (at))
    {
    case dw_val_class_const:
      CHECKSUM (at->dw_attr_val.v.val_int);
      break;
    case dw_val_class_unsigned_const:
      CHECKSUM (at->dw_attr_val.v.val_unsigned);
      break;
    case dw_val_class_long_long:
      CHECKSUM (at->dw_attr_val.v.val_long_long);
      break;
    case dw_val_class_vec:
      CHECKSUM (at->dw_attr_val.v.val_vec);
      break;
    case dw_val_class_flag:
      CHECKSUM (at->dw_attr_val.v.val_flag);
      break;
    case dw_val_class_str:
      CHECKSUM_STRING (AT_string (at));
      break;

    case dw_val_class_addr:
      r = AT_addr (at);
      gcc_assert (GET_CODE (r) == SYMBOL_REF);
      CHECKSUM_STRING (XSTR (r, 0));
      break;

    case dw_val_class_offset:
      CHECKSUM (at->dw_attr_val.v.val_offset);
      break;

    case dw_val_class_loc:
      for (loc = AT_loc (at); loc; loc = loc->dw_loc_next)
	loc_checksum (loc, ctx);
      break;

    case dw_val_class_die_ref:
      die_checksum (AT_ref (at), ctx, mark);
      break;

    case dw_val_class_fde_ref:
    case dw_val_class_lbl_id:
    case dw_val_class_lineptr:
    case dw_val_class_macptr:
      break;

    case dw_val_class_file:
      CHECKSUM_STRING (AT_file (at)->filename);
      break;

    default:
      break;
    }
}

/* Calculate the checksum of a DIE.  */

static void
die_checksum (dw_die_ref die, struct md5_ctx *ctx, int *mark)
{
  dw_die_ref c;
  dw_attr_ref a;
  unsigned ix;

  /* To avoid infinite recursion.  */
  if (die->die_mark)
    {
      CHECKSUM (die->die_mark);
      return;
    }
  die->die_mark = ++(*mark);

  CHECKSUM (die->die_tag);

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    attr_checksum (a, ctx, mark);

  FOR_EACH_CHILD (die, c, die_checksum (c, ctx, mark));
}

#undef CHECKSUM
#undef CHECKSUM_STRING

/* Do the location expressions look same?  */
static inline int
same_loc_p (dw_loc_descr_ref loc1, dw_loc_descr_ref loc2, int *mark)
{
  return loc1->dw_loc_opc == loc2->dw_loc_opc
	 && same_dw_val_p (&loc1->dw_loc_oprnd1, &loc2->dw_loc_oprnd1, mark)
	 && same_dw_val_p (&loc1->dw_loc_oprnd2, &loc2->dw_loc_oprnd2, mark);
}

/* Do the values look the same?  */
static int
same_dw_val_p (dw_val_node *v1, dw_val_node *v2, int *mark)
{
  dw_loc_descr_ref loc1, loc2;
  rtx r1, r2;

  if (v1->val_class != v2->val_class)
    return 0;

  switch (v1->val_class)
    {
    case dw_val_class_const:
      return v1->v.val_int == v2->v.val_int;
    case dw_val_class_unsigned_const:
      return v1->v.val_unsigned == v2->v.val_unsigned;
    case dw_val_class_long_long:
      return v1->v.val_long_long.hi == v2->v.val_long_long.hi
	     && v1->v.val_long_long.low == v2->v.val_long_long.low;
    case dw_val_class_vec:
      if (v1->v.val_vec.length != v2->v.val_vec.length
	  || v1->v.val_vec.elt_size != v2->v.val_vec.elt_size)
	return 0;
      if (memcmp (v1->v.val_vec.array, v2->v.val_vec.array,
		  v1->v.val_vec.length * v1->v.val_vec.elt_size))
	return 0;
      return 1;
    case dw_val_class_flag:
      return v1->v.val_flag == v2->v.val_flag;
    case dw_val_class_str:
      return !strcmp(v1->v.val_str->str, v2->v.val_str->str);

    case dw_val_class_addr:
      r1 = v1->v.val_addr;
      r2 = v2->v.val_addr;
      if (GET_CODE (r1) != GET_CODE (r2))
	return 0;
      gcc_assert (GET_CODE (r1) == SYMBOL_REF);
      return !strcmp (XSTR (r1, 0), XSTR (r2, 0));

    case dw_val_class_offset:
      return v1->v.val_offset == v2->v.val_offset;

    case dw_val_class_loc:
      for (loc1 = v1->v.val_loc, loc2 = v2->v.val_loc;
	   loc1 && loc2;
	   loc1 = loc1->dw_loc_next, loc2 = loc2->dw_loc_next)
	if (!same_loc_p (loc1, loc2, mark))
	  return 0;
      return !loc1 && !loc2;

    case dw_val_class_die_ref:
      return same_die_p (v1->v.val_die_ref.die, v2->v.val_die_ref.die, mark);

    case dw_val_class_fde_ref:
    case dw_val_class_lbl_id:
    case dw_val_class_lineptr:
    case dw_val_class_macptr:
      return 1;

    case dw_val_class_file:
      return v1->v.val_file == v2->v.val_file;

    default:
      return 1;
    }
}

/* Do the attributes look the same?  */

static int
same_attr_p (dw_attr_ref at1, dw_attr_ref at2, int *mark)
{
  if (at1->dw_attr != at2->dw_attr)
    return 0;

  /* We don't care that this was compiled with a different compiler
     snapshot; if the output is the same, that's what matters. */
  if (at1->dw_attr == DW_AT_producer)
    return 1;

  return same_dw_val_p (&at1->dw_attr_val, &at2->dw_attr_val, mark);
}

/* Do the dies look the same?  */

static int
same_die_p (dw_die_ref die1, dw_die_ref die2, int *mark)
{
  dw_die_ref c1, c2;
  dw_attr_ref a1;
  unsigned ix;

  /* To avoid infinite recursion.  */
  if (die1->die_mark)
    return die1->die_mark == die2->die_mark;
  die1->die_mark = die2->die_mark = ++(*mark);

  if (die1->die_tag != die2->die_tag)
    return 0;

  if (VEC_length (dw_attr_node, die1->die_attr)
      != VEC_length (dw_attr_node, die2->die_attr))
    return 0;
  
  for (ix = 0; VEC_iterate (dw_attr_node, die1->die_attr, ix, a1); ix++)
    if (!same_attr_p (a1, VEC_index (dw_attr_node, die2->die_attr, ix), mark))
      return 0;

  c1 = die1->die_child;
  c2 = die2->die_child;
  if (! c1)
    {
      if (c2)
	return 0;
    }
  else
    for (;;)
      {
	if (!same_die_p (c1, c2, mark))
	  return 0;
	c1 = c1->die_sib;
	c2 = c2->die_sib;
	if (c1 == die1->die_child)
	  {
	    if (c2 == die2->die_child)
	      break;
	    else
	      return 0;
	  }
    }

  return 1;
}

/* Do the dies look the same?  Wrapper around same_die_p.  */

static int
same_die_p_wrap (dw_die_ref die1, dw_die_ref die2)
{
  int mark = 0;
  int ret = same_die_p (die1, die2, &mark);

  unmark_all_dies (die1);
  unmark_all_dies (die2);

  return ret;
}

/* The prefix to attach to symbols on DIEs in the current comdat debug
   info section.  */
static char *comdat_symbol_id;

/* The index of the current symbol within the current comdat CU.  */
static unsigned int comdat_symbol_number;

/* Calculate the MD5 checksum of the compilation unit DIE UNIT_DIE and its
   children, and set comdat_symbol_id accordingly.  */

static void
compute_section_prefix (dw_die_ref unit_die)
{
  const char *die_name = get_AT_string (unit_die, DW_AT_name);
  const char *base = die_name ? lbasename (die_name) : "anonymous";
  char *name = alloca (strlen (base) + 64);
  char *p;
  int i, mark;
  unsigned char checksum[16];
  struct md5_ctx ctx;

  /* Compute the checksum of the DIE, then append part of it as hex digits to
     the name filename of the unit.  */

  md5_init_ctx (&ctx);
  mark = 0;
  die_checksum (unit_die, &ctx, &mark);
  unmark_all_dies (unit_die);
  md5_finish_ctx (&ctx, checksum);

  sprintf (name, "%s.", base);
  clean_symbol_name (name);

  p = name + strlen (name);
  for (i = 0; i < 4; i++)
    {
      sprintf (p, "%.2x", checksum[i]);
      p += 2;
    }

  comdat_symbol_id = unit_die->die_symbol = xstrdup (name);
  comdat_symbol_number = 0;
}

/* Returns nonzero if DIE represents a type, in the sense of TYPE_P.  */

static int
is_type_die (dw_die_ref die)
{
  switch (die->die_tag)
    {
    case DW_TAG_array_type:
    case DW_TAG_class_type:
    case DW_TAG_enumeration_type:
    case DW_TAG_pointer_type:
    case DW_TAG_reference_type:
    case DW_TAG_string_type:
    case DW_TAG_structure_type:
    case DW_TAG_subroutine_type:
    case DW_TAG_union_type:
    case DW_TAG_ptr_to_member_type:
    case DW_TAG_set_type:
    case DW_TAG_subrange_type:
    case DW_TAG_base_type:
    case DW_TAG_const_type:
    case DW_TAG_file_type:
    case DW_TAG_packed_type:
    case DW_TAG_volatile_type:
    case DW_TAG_typedef:
      return 1;
    default:
      return 0;
    }
}

/* Returns 1 iff C is the sort of DIE that should go into a COMDAT CU.
   Basically, we want to choose the bits that are likely to be shared between
   compilations (types) and leave out the bits that are specific to individual
   compilations (functions).  */

static int
is_comdat_die (dw_die_ref c)
{
  /* I think we want to leave base types and __vtbl_ptr_type in the main CU, as
     we do for stabs.  The advantage is a greater likelihood of sharing between
     objects that don't include headers in the same order (and therefore would
     put the base types in a different comdat).  jason 8/28/00 */

  if (c->die_tag == DW_TAG_base_type)
    return 0;

  if (c->die_tag == DW_TAG_pointer_type
      || c->die_tag == DW_TAG_reference_type
      || c->die_tag == DW_TAG_const_type
      || c->die_tag == DW_TAG_volatile_type)
    {
      dw_die_ref t = get_AT_ref (c, DW_AT_type);

      return t ? is_comdat_die (t) : 0;
    }

  return is_type_die (c);
}

/* Returns 1 iff C is the sort of DIE that might be referred to from another
   compilation unit.  */

static int
is_symbol_die (dw_die_ref c)
{
  return (is_type_die (c)
	  || (get_AT (c, DW_AT_declaration)
	      && !get_AT (c, DW_AT_specification))
	  || c->die_tag == DW_TAG_namespace);
}

static char *
gen_internal_sym (const char *prefix)
{
  char buf[256];

  ASM_GENERATE_INTERNAL_LABEL (buf, prefix, label_num++);
  return xstrdup (buf);
}

/* Assign symbols to all worthy DIEs under DIE.  */

static void
assign_symbol_names (dw_die_ref die)
{
  dw_die_ref c;

  if (is_symbol_die (die))
    {
      if (comdat_symbol_id)
	{
	  char *p = alloca (strlen (comdat_symbol_id) + 64);

	  sprintf (p, "%s.%s.%x", DIE_LABEL_PREFIX,
		   comdat_symbol_id, comdat_symbol_number++);
	  die->die_symbol = xstrdup (p);
	}
      else
	die->die_symbol = gen_internal_sym ("LDIE");
    }

  FOR_EACH_CHILD (die, c, assign_symbol_names (c));
}

struct cu_hash_table_entry
{
  dw_die_ref cu;
  unsigned min_comdat_num, max_comdat_num;
  struct cu_hash_table_entry *next;
};

/* Routines to manipulate hash table of CUs.  */
static hashval_t
htab_cu_hash (const void *of)
{
  const struct cu_hash_table_entry *entry = of;

  return htab_hash_string (entry->cu->die_symbol);
}

static int
htab_cu_eq (const void *of1, const void *of2)
{
  const struct cu_hash_table_entry *entry1 = of1;
  const struct die_struct *entry2 = of2;

  return !strcmp (entry1->cu->die_symbol, entry2->die_symbol);
}

static void
htab_cu_del (void *what)
{
  struct cu_hash_table_entry *next, *entry = what;

  while (entry)
    {
      next = entry->next;
      free (entry);
      entry = next;
    }
}

/* Check whether we have already seen this CU and set up SYM_NUM
   accordingly.  */
static int
check_duplicate_cu (dw_die_ref cu, htab_t htable, unsigned int *sym_num)
{
  struct cu_hash_table_entry dummy;
  struct cu_hash_table_entry **slot, *entry, *last = &dummy;

  dummy.max_comdat_num = 0;

  slot = (struct cu_hash_table_entry **)
    htab_find_slot_with_hash (htable, cu, htab_hash_string (cu->die_symbol),
	INSERT);
  entry = *slot;

  for (; entry; last = entry, entry = entry->next)
    {
      if (same_die_p_wrap (cu, entry->cu))
	break;
    }

  if (entry)
    {
      *sym_num = entry->min_comdat_num;
      return 1;
    }

  entry = XCNEW (struct cu_hash_table_entry);
  entry->cu = cu;
  entry->min_comdat_num = *sym_num = last->max_comdat_num;
  entry->next = *slot;
  *slot = entry;

  return 0;
}

/* Record SYM_NUM to record of CU in HTABLE.  */
static void
record_comdat_symbol_number (dw_die_ref cu, htab_t htable, unsigned int sym_num)
{
  struct cu_hash_table_entry **slot, *entry;

  slot = (struct cu_hash_table_entry **)
    htab_find_slot_with_hash (htable, cu, htab_hash_string (cu->die_symbol),
	NO_INSERT);
  entry = *slot;

  entry->max_comdat_num = sym_num;
}

/* Traverse the DIE (which is always comp_unit_die), and set up
   additional compilation units for each of the include files we see
   bracketed by BINCL/EINCL.  */

static void
break_out_includes (dw_die_ref die)
{
  dw_die_ref c;
  dw_die_ref unit = NULL;
  limbo_die_node *node, **pnode;
  htab_t cu_hash_table;

  c = die->die_child;
  if (c) do {
    dw_die_ref prev = c;
    c = c->die_sib;
    while (c->die_tag == DW_TAG_GNU_BINCL || c->die_tag == DW_TAG_GNU_EINCL
	   || (unit && is_comdat_die (c)))
      {
	dw_die_ref next = c->die_sib;

	/* This DIE is for a secondary CU; remove it from the main one.  */
	remove_child_with_prev (c, prev);
	
	if (c->die_tag == DW_TAG_GNU_BINCL)
	  unit = push_new_compile_unit (unit, c);
	else if (c->die_tag == DW_TAG_GNU_EINCL)
	  unit = pop_compile_unit (unit);
	else
	  add_child_die (unit, c);
	c = next;
	if (c == die->die_child)
	  break;
      }
  } while (c != die->die_child);

#if 0
  /* We can only use this in debugging, since the frontend doesn't check
     to make sure that we leave every include file we enter.  */
  gcc_assert (!unit);
#endif

  assign_symbol_names (die);
  cu_hash_table = htab_create (10, htab_cu_hash, htab_cu_eq, htab_cu_del);
  for (node = limbo_die_list, pnode = &limbo_die_list;
       node;
       node = node->next)
    {
      int is_dupl;

      compute_section_prefix (node->die);
      is_dupl = check_duplicate_cu (node->die, cu_hash_table,
			&comdat_symbol_number);
      assign_symbol_names (node->die);
      if (is_dupl)
	*pnode = node->next;
      else
	{
	  pnode = &node->next;
	  record_comdat_symbol_number (node->die, cu_hash_table,
		comdat_symbol_number);
	}
    }
  htab_delete (cu_hash_table);
}

/* Traverse the DIE and add a sibling attribute if it may have the
   effect of speeding up access to siblings.  To save some space,
   avoid generating sibling attributes for DIE's without children.  */

static void
add_sibling_attributes (dw_die_ref die)
{
  dw_die_ref c;

  if (! die->die_child)
    return;

  if (die->die_parent && die != die->die_parent->die_child)
    add_AT_die_ref (die, DW_AT_sibling, die->die_sib);

  FOR_EACH_CHILD (die, c, add_sibling_attributes (c));
}

/* Output all location lists for the DIE and its children.  */

static void
output_location_lists (dw_die_ref die)
{
  dw_die_ref c;
  dw_attr_ref a;
  unsigned ix;

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    if (AT_class (a) == dw_val_class_loc_list)
      output_loc_list (AT_loc_list (a));

  FOR_EACH_CHILD (die, c, output_location_lists (c));
}

/* The format of each DIE (and its attribute value pairs) is encoded in an
   abbreviation table.  This routine builds the abbreviation table and assigns
   a unique abbreviation id for each abbreviation entry.  The children of each
   die are visited recursively.  */

static void
build_abbrev_table (dw_die_ref die)
{
  unsigned long abbrev_id;
  unsigned int n_alloc;
  dw_die_ref c;
  dw_attr_ref a;
  unsigned ix;

  /* Scan the DIE references, and mark as external any that refer to
     DIEs from other CUs (i.e. those which are not marked).  */
  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    if (AT_class (a) == dw_val_class_die_ref
	&& AT_ref (a)->die_mark == 0)
      {
	gcc_assert (AT_ref (a)->die_symbol);

	set_AT_ref_external (a, 1);
      }

  for (abbrev_id = 1; abbrev_id < abbrev_die_table_in_use; ++abbrev_id)
    {
      dw_die_ref abbrev = abbrev_die_table[abbrev_id];
      dw_attr_ref die_a, abbrev_a;
      unsigned ix;
      bool ok = true;
      
      if (abbrev->die_tag != die->die_tag)
	continue;
      if ((abbrev->die_child != NULL) != (die->die_child != NULL))
	continue;
      
      if (VEC_length (dw_attr_node, abbrev->die_attr)
	  != VEC_length (dw_attr_node, die->die_attr))
	continue;
  
      for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, die_a); ix++)
	{
	  abbrev_a = VEC_index (dw_attr_node, abbrev->die_attr, ix);
	  if ((abbrev_a->dw_attr != die_a->dw_attr)
	      || (value_format (abbrev_a) != value_format (die_a)))
	    {
	      ok = false;
	      break;
	    }
	}
      if (ok)
	break;
    }

  if (abbrev_id >= abbrev_die_table_in_use)
    {
      if (abbrev_die_table_in_use >= abbrev_die_table_allocated)
	{
	  n_alloc = abbrev_die_table_allocated + ABBREV_DIE_TABLE_INCREMENT;
	  abbrev_die_table = ggc_realloc (abbrev_die_table,
					  sizeof (dw_die_ref) * n_alloc);

	  memset (&abbrev_die_table[abbrev_die_table_allocated], 0,
		 (n_alloc - abbrev_die_table_allocated) * sizeof (dw_die_ref));
	  abbrev_die_table_allocated = n_alloc;
	}

      ++abbrev_die_table_in_use;
      abbrev_die_table[abbrev_id] = die;
    }

  die->die_abbrev = abbrev_id;
  FOR_EACH_CHILD (die, c, build_abbrev_table (c));
}

/* Return the power-of-two number of bytes necessary to represent VALUE.  */

static int
constant_size (long unsigned int value)
{
  int log;

  if (value == 0)
    log = 0;
  else
    log = floor_log2 (value);

  log = log / 8;
  log = 1 << (floor_log2 (log) + 1);

  return log;
}

/* Return the size of a DIE as it is represented in the
   .debug_info section.  */

static unsigned long
size_of_die (dw_die_ref die)
{
  unsigned long size = 0;
  dw_attr_ref a;
  unsigned ix;

  size += size_of_uleb128 (die->die_abbrev);
  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    {
      switch (AT_class (a))
	{
	case dw_val_class_addr:
	  size += DWARF2_ADDR_SIZE;
	  break;
	case dw_val_class_offset:
	  size += DWARF_OFFSET_SIZE;
	  break;
	case dw_val_class_loc:
	  {
	    unsigned long lsize = size_of_locs (AT_loc (a));

	    /* Block length.  */
	    size += constant_size (lsize);
	    size += lsize;
	  }
	  break;
	case dw_val_class_loc_list:
	  size += DWARF_OFFSET_SIZE;
	  break;
	case dw_val_class_range_list:
	  size += DWARF_OFFSET_SIZE;
	  break;
	case dw_val_class_const:
	  size += size_of_sleb128 (AT_int (a));
	  break;
	case dw_val_class_unsigned_const:
	  size += constant_size (AT_unsigned (a));
	  break;
	case dw_val_class_long_long:
	  size += 1 + 2*HOST_BITS_PER_LONG/HOST_BITS_PER_CHAR; /* block */
	  break;
	case dw_val_class_vec:
	  size += 1 + (a->dw_attr_val.v.val_vec.length
		       * a->dw_attr_val.v.val_vec.elt_size); /* block */
	  break;
	case dw_val_class_flag:
	  size += 1;
	  break;
	case dw_val_class_die_ref:
	  if (AT_ref_external (a))
	    size += DWARF2_ADDR_SIZE;
	  else
	    size += DWARF_OFFSET_SIZE;
	  break;
	case dw_val_class_fde_ref:
	  size += DWARF_OFFSET_SIZE;
	  break;
	case dw_val_class_lbl_id:
	  size += DWARF2_ADDR_SIZE;
	  break;
	case dw_val_class_lineptr:
	case dw_val_class_macptr:
	  size += DWARF_OFFSET_SIZE;
	  break;
	case dw_val_class_str:
	  if (AT_string_form (a) == DW_FORM_strp)
	    size += DWARF_OFFSET_SIZE;
	  else
	    size += strlen (a->dw_attr_val.v.val_str->str) + 1;
	  break;
	case dw_val_class_file:
	  size += constant_size (maybe_emit_file (a->dw_attr_val.v.val_file));
	  break;
	default:
	  gcc_unreachable ();
	}
    }

  return size;
}

/* Size the debugging information associated with a given DIE.  Visits the
   DIE's children recursively.  Updates the global variable next_die_offset, on
   each time through.  Uses the current value of next_die_offset to update the
   die_offset field in each DIE.  */

static void
calc_die_sizes (dw_die_ref die)
{
  dw_die_ref c;

  die->die_offset = next_die_offset;
  next_die_offset += size_of_die (die);

  FOR_EACH_CHILD (die, c, calc_die_sizes (c));

  if (die->die_child != NULL)
    /* Count the null byte used to terminate sibling lists.  */
    next_die_offset += 1;
}

/* Set the marks for a die and its children.  We do this so
   that we know whether or not a reference needs to use FORM_ref_addr; only
   DIEs in the same CU will be marked.  We used to clear out the offset
   and use that as the flag, but ran into ordering problems.  */

static void
mark_dies (dw_die_ref die)
{
  dw_die_ref c;

  gcc_assert (!die->die_mark);

  die->die_mark = 1;
  FOR_EACH_CHILD (die, c, mark_dies (c));
}

/* Clear the marks for a die and its children.  */

static void
unmark_dies (dw_die_ref die)
{
  dw_die_ref c;

  gcc_assert (die->die_mark);

  die->die_mark = 0;
  FOR_EACH_CHILD (die, c, unmark_dies (c));
}

/* Clear the marks for a die, its children and referred dies.  */

static void
unmark_all_dies (dw_die_ref die)
{
  dw_die_ref c;
  dw_attr_ref a;
  unsigned ix;

  if (!die->die_mark)
    return;
  die->die_mark = 0;

  FOR_EACH_CHILD (die, c, unmark_all_dies (c));

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    if (AT_class (a) == dw_val_class_die_ref)
      unmark_all_dies (AT_ref (a));
}

/* Return the size of the .debug_pubnames table  generated for the
   compilation unit.  */

static unsigned long
size_of_pubnames (void)
{
  unsigned long size;
  unsigned i;

  size = DWARF_PUBNAMES_HEADER_SIZE;
  for (i = 0; i < pubname_table_in_use; i++)
    {
      pubname_ref p = &pubname_table[i];
      size += DWARF_OFFSET_SIZE + strlen (p->name) + 1;
    }

  size += DWARF_OFFSET_SIZE;
  return size;
}

/* Return the size of the information in the .debug_aranges section.  */

static unsigned long
size_of_aranges (void)
{
  unsigned long size;

  size = DWARF_ARANGES_HEADER_SIZE;

  /* Count the address/length pair for this compilation unit.  */
  size += 2 * DWARF2_ADDR_SIZE;
  size += 2 * DWARF2_ADDR_SIZE * arange_table_in_use;

  /* Count the two zero words used to terminated the address range table.  */
  size += 2 * DWARF2_ADDR_SIZE;
  return size;
}

/* Select the encoding of an attribute value.  */

static enum dwarf_form
value_format (dw_attr_ref a)
{
  switch (a->dw_attr_val.val_class)
    {
    case dw_val_class_addr:
      return DW_FORM_addr;
    case dw_val_class_range_list:
    case dw_val_class_offset:
    case dw_val_class_loc_list:
      switch (DWARF_OFFSET_SIZE)
	{
	case 4:
	  return DW_FORM_data4;
	case 8:
	  return DW_FORM_data8;
	default:
	  gcc_unreachable ();
	}
    case dw_val_class_loc:
      switch (constant_size (size_of_locs (AT_loc (a))))
	{
	case 1:
	  return DW_FORM_block1;
	case 2:
	  return DW_FORM_block2;
	default:
	  gcc_unreachable ();
	}
    case dw_val_class_const:
      return DW_FORM_sdata;
    case dw_val_class_unsigned_const:
      switch (constant_size (AT_unsigned (a)))
	{
	case 1:
	  return DW_FORM_data1;
	case 2:
	  return DW_FORM_data2;
	case 4:
	  return DW_FORM_data4;
	case 8:
	  return DW_FORM_data8;
	default:
	  gcc_unreachable ();
	}
    case dw_val_class_long_long:
      return DW_FORM_block1;
    case dw_val_class_vec:
      return DW_FORM_block1;
    case dw_val_class_flag:
      return DW_FORM_flag;
    case dw_val_class_die_ref:
      if (AT_ref_external (a))
	return DW_FORM_ref_addr;
      else
	return DW_FORM_ref;
    case dw_val_class_fde_ref:
      return DW_FORM_data;
    case dw_val_class_lbl_id:
      return DW_FORM_addr;
    case dw_val_class_lineptr:
    case dw_val_class_macptr:
      return DW_FORM_data;
    case dw_val_class_str:
      return AT_string_form (a);
    case dw_val_class_file:
      switch (constant_size (maybe_emit_file (a->dw_attr_val.v.val_file)))
	{
	case 1:
	  return DW_FORM_data1;
	case 2:
	  return DW_FORM_data2;
	case 4:
	  return DW_FORM_data4;
	default:
	  gcc_unreachable ();
	}

    default:
      gcc_unreachable ();
    }
}

/* Output the encoding of an attribute value.  */

static void
output_value_format (dw_attr_ref a)
{
  enum dwarf_form form = value_format (a);

  dw2_asm_output_data_uleb128 (form, "(%s)", dwarf_form_name (form));
}

/* Output the .debug_abbrev section which defines the DIE abbreviation
   table.  */

static void
output_abbrev_section (void)
{
  unsigned long abbrev_id;

  for (abbrev_id = 1; abbrev_id < abbrev_die_table_in_use; ++abbrev_id)
    {
      dw_die_ref abbrev = abbrev_die_table[abbrev_id];
      unsigned ix;
      dw_attr_ref a_attr;

      dw2_asm_output_data_uleb128 (abbrev_id, "(abbrev code)");
      dw2_asm_output_data_uleb128 (abbrev->die_tag, "(TAG: %s)",
				   dwarf_tag_name (abbrev->die_tag));

      if (abbrev->die_child != NULL)
	dw2_asm_output_data (1, DW_children_yes, "DW_children_yes");
      else
	dw2_asm_output_data (1, DW_children_no, "DW_children_no");

      for (ix = 0; VEC_iterate (dw_attr_node, abbrev->die_attr, ix, a_attr);
	   ix++)
	{
	  dw2_asm_output_data_uleb128 (a_attr->dw_attr, "(%s)",
				       dwarf_attr_name (a_attr->dw_attr));
	  output_value_format (a_attr);
	}

      dw2_asm_output_data (1, 0, NULL);
      dw2_asm_output_data (1, 0, NULL);
    }

  /* Terminate the table.  */
  dw2_asm_output_data (1, 0, NULL);
}

/* Output a symbol we can use to refer to this DIE from another CU.  */

static inline void
output_die_symbol (dw_die_ref die)
{
  char *sym = die->die_symbol;

  if (sym == 0)
    return;

  if (strncmp (sym, DIE_LABEL_PREFIX, sizeof (DIE_LABEL_PREFIX) - 1) == 0)
    /* We make these global, not weak; if the target doesn't support
       .linkonce, it doesn't support combining the sections, so debugging
       will break.  */
    targetm.asm_out.globalize_label (asm_out_file, sym);

  ASM_OUTPUT_LABEL (asm_out_file, sym);
}

/* Return a new location list, given the begin and end range, and the
   expression. gensym tells us whether to generate a new internal symbol for
   this location list node, which is done for the head of the list only.  */

static inline dw_loc_list_ref
new_loc_list (dw_loc_descr_ref expr, const char *begin, const char *end,
	      const char *section, unsigned int gensym)
{
  dw_loc_list_ref retlist = ggc_alloc_cleared (sizeof (dw_loc_list_node));

  retlist->begin = begin;
  retlist->end = end;
  retlist->expr = expr;
  retlist->section = section;
  if (gensym)
    retlist->ll_symbol = gen_internal_sym ("LLST");

  return retlist;
}

/* Add a location description expression to a location list.  */

static inline void
add_loc_descr_to_loc_list (dw_loc_list_ref *list_head, dw_loc_descr_ref descr,
			   const char *begin, const char *end,
			   const char *section)
{
  dw_loc_list_ref *d;

  /* Find the end of the chain.  */
  for (d = list_head; (*d) != NULL; d = &(*d)->dw_loc_next)
    ;

  /* Add a new location list node to the list.  */
  *d = new_loc_list (descr, begin, end, section, 0);
}

static void
dwarf2out_switch_text_section (void)
{
  dw_fde_ref fde;

  gcc_assert (cfun);

  fde = &fde_table[fde_table_in_use - 1];
  fde->dw_fde_switched_sections = true;
  fde->dw_fde_hot_section_label = cfun->hot_section_label;
  fde->dw_fde_hot_section_end_label = cfun->hot_section_end_label;
  fde->dw_fde_unlikely_section_label = cfun->cold_section_label;
  fde->dw_fde_unlikely_section_end_label = cfun->cold_section_end_label;
  have_multiple_function_sections = true;

  /* Reset the current label on switching text sections, so that we
     don't attempt to advance_loc4 between labels in different sections.  */
  fde->dw_fde_current_label = NULL;
}

/* Output the location list given to us.  */

static void
output_loc_list (dw_loc_list_ref list_head)
{
  dw_loc_list_ref curr = list_head;

  ASM_OUTPUT_LABEL (asm_out_file, list_head->ll_symbol);

  /* Walk the location list, and output each range + expression.  */
  for (curr = list_head; curr != NULL; curr = curr->dw_loc_next)
    {
      unsigned long size;
      if (!have_multiple_function_sections)
	{
	  dw2_asm_output_delta (DWARF2_ADDR_SIZE, curr->begin, curr->section,
				"Location list begin address (%s)",
				list_head->ll_symbol);
	  dw2_asm_output_delta (DWARF2_ADDR_SIZE, curr->end, curr->section,
				"Location list end address (%s)",
				list_head->ll_symbol);
	}
      else
	{
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, curr->begin,
			       "Location list begin address (%s)",
			       list_head->ll_symbol);
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, curr->end,
			       "Location list end address (%s)",
			       list_head->ll_symbol);
	}
      size = size_of_locs (curr->expr);

      /* Output the block length for this list of location operations.  */
      gcc_assert (size <= 0xffff);
      dw2_asm_output_data (2, size, "%s", "Location expression size");

      output_loc_sequence (curr->expr);
    }

  dw2_asm_output_data (DWARF2_ADDR_SIZE, 0,
		       "Location list terminator begin (%s)",
		       list_head->ll_symbol);
  dw2_asm_output_data (DWARF2_ADDR_SIZE, 0,
		       "Location list terminator end (%s)",
		       list_head->ll_symbol);
}

/* Output the DIE and its attributes.  Called recursively to generate
   the definitions of each child DIE.  */

static void
output_die (dw_die_ref die)
{
  dw_attr_ref a;
  dw_die_ref c;
  unsigned long size;
  unsigned ix;

  /* If someone in another CU might refer to us, set up a symbol for
     them to point to.  */
  if (die->die_symbol)
    output_die_symbol (die);

  dw2_asm_output_data_uleb128 (die->die_abbrev, "(DIE (0x%lx) %s)",
			       die->die_offset, dwarf_tag_name (die->die_tag));

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    {
      const char *name = dwarf_attr_name (a->dw_attr);

      switch (AT_class (a))
	{
	case dw_val_class_addr:
	  dw2_asm_output_addr_rtx (DWARF2_ADDR_SIZE, AT_addr (a), "%s", name);
	  break;

	case dw_val_class_offset:
	  dw2_asm_output_data (DWARF_OFFSET_SIZE, a->dw_attr_val.v.val_offset,
			       "%s", name);
	  break;

	case dw_val_class_range_list:
	  {
	    char *p = strchr (ranges_section_label, '\0');

	    sprintf (p, "+" HOST_WIDE_INT_PRINT_HEX,
		     a->dw_attr_val.v.val_offset);
	    dw2_asm_output_offset (DWARF_OFFSET_SIZE, ranges_section_label,
				   debug_ranges_section, "%s", name);
	    *p = '\0';
	  }
	  break;

	case dw_val_class_loc:
	  size = size_of_locs (AT_loc (a));

	  /* Output the block length for this list of location operations.  */
	  dw2_asm_output_data (constant_size (size), size, "%s", name);

	  output_loc_sequence (AT_loc (a));
	  break;

	case dw_val_class_const:
	  /* ??? It would be slightly more efficient to use a scheme like is
	     used for unsigned constants below, but gdb 4.x does not sign
	     extend.  Gdb 5.x does sign extend.  */
	  dw2_asm_output_data_sleb128 (AT_int (a), "%s", name);
	  break;

	case dw_val_class_unsigned_const:
	  dw2_asm_output_data (constant_size (AT_unsigned (a)),
			       AT_unsigned (a), "%s", name);
	  break;

	case dw_val_class_long_long:
	  {
	    unsigned HOST_WIDE_INT first, second;

	    dw2_asm_output_data (1,
				 2 * HOST_BITS_PER_LONG / HOST_BITS_PER_CHAR,
				 "%s", name);

	    if (WORDS_BIG_ENDIAN)
	      {
		first = a->dw_attr_val.v.val_long_long.hi;
		second = a->dw_attr_val.v.val_long_long.low;
	      }
	    else
	      {
		first = a->dw_attr_val.v.val_long_long.low;
		second = a->dw_attr_val.v.val_long_long.hi;
	      }

	    dw2_asm_output_data (HOST_BITS_PER_LONG / HOST_BITS_PER_CHAR,
				 first, "long long constant");
	    dw2_asm_output_data (HOST_BITS_PER_LONG / HOST_BITS_PER_CHAR,
				 second, NULL);
	  }
	  break;

	case dw_val_class_vec:
	  {
	    unsigned int elt_size = a->dw_attr_val.v.val_vec.elt_size;
	    unsigned int len = a->dw_attr_val.v.val_vec.length;
	    unsigned int i;
	    unsigned char *p;

	    dw2_asm_output_data (1, len * elt_size, "%s", name);
	    if (elt_size > sizeof (HOST_WIDE_INT))
	      {
		elt_size /= 2;
		len *= 2;
	      }
	    for (i = 0, p = a->dw_attr_val.v.val_vec.array;
		 i < len;
		 i++, p += elt_size)
	      dw2_asm_output_data (elt_size, extract_int (p, elt_size),
				   "fp or vector constant word %u", i);
	    break;
	  }

	case dw_val_class_flag:
	  dw2_asm_output_data (1, AT_flag (a), "%s", name);
	  break;

	case dw_val_class_loc_list:
	  {
	    char *sym = AT_loc_list (a)->ll_symbol;

	    gcc_assert (sym);
	    dw2_asm_output_offset (DWARF_OFFSET_SIZE, sym, debug_loc_section,
				   "%s", name);
	  }
	  break;

	case dw_val_class_die_ref:
	  if (AT_ref_external (a))
	    {
	      char *sym = AT_ref (a)->die_symbol;

	      gcc_assert (sym);
	      dw2_asm_output_offset (DWARF2_ADDR_SIZE, sym, debug_info_section,
				     "%s", name);
	    }
	  else
	    {
	      gcc_assert (AT_ref (a)->die_offset);
	      dw2_asm_output_data (DWARF_OFFSET_SIZE, AT_ref (a)->die_offset,
				   "%s", name);
	    }
	  break;

	case dw_val_class_fde_ref:
	  {
	    char l1[20];

	    ASM_GENERATE_INTERNAL_LABEL (l1, FDE_LABEL,
					 a->dw_attr_val.v.val_fde_index * 2);
	    dw2_asm_output_offset (DWARF_OFFSET_SIZE, l1, debug_frame_section,
				   "%s", name);
	  }
	  break;

	case dw_val_class_lbl_id:
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, AT_lbl (a), "%s", name);
	  break;

	case dw_val_class_lineptr:
	  dw2_asm_output_offset (DWARF_OFFSET_SIZE, AT_lbl (a),
				 debug_line_section, "%s", name);
	  break;

	case dw_val_class_macptr:
	  dw2_asm_output_offset (DWARF_OFFSET_SIZE, AT_lbl (a),
				 debug_macinfo_section, "%s", name);
	  break;

	case dw_val_class_str:
	  if (AT_string_form (a) == DW_FORM_strp)
	    dw2_asm_output_offset (DWARF_OFFSET_SIZE,
				   a->dw_attr_val.v.val_str->label,
				   debug_str_section,
				   "%s: \"%s\"", name, AT_string (a));
	  else
	    dw2_asm_output_nstring (AT_string (a), -1, "%s", name);
	  break;

	case dw_val_class_file:
	  {
	    int f = maybe_emit_file (a->dw_attr_val.v.val_file);
	    
	    dw2_asm_output_data (constant_size (f), f, "%s (%s)", name,
				 a->dw_attr_val.v.val_file->filename);
	    break;
	  }

	default:
	  gcc_unreachable ();
	}
    }

  FOR_EACH_CHILD (die, c, output_die (c));

  /* Add null byte to terminate sibling list.  */
  if (die->die_child != NULL)
    dw2_asm_output_data (1, 0, "end of children of DIE 0x%lx",
			 die->die_offset);
}

/* Output the compilation unit that appears at the beginning of the
   .debug_info section, and precedes the DIE descriptions.  */

static void
output_compilation_unit_header (void)
{
  if (DWARF_INITIAL_LENGTH_SIZE - DWARF_OFFSET_SIZE == 4)
    dw2_asm_output_data (4, 0xffffffff,
      "Initial length escape value indicating 64-bit DWARF extension");
  dw2_asm_output_data (DWARF_OFFSET_SIZE,
                       next_die_offset - DWARF_INITIAL_LENGTH_SIZE,
		       "Length of Compilation Unit Info");
  dw2_asm_output_data (2, DWARF_VERSION, "DWARF version number");
  dw2_asm_output_offset (DWARF_OFFSET_SIZE, abbrev_section_label,
			 debug_abbrev_section,
			 "Offset Into Abbrev. Section");
  dw2_asm_output_data (1, DWARF2_ADDR_SIZE, "Pointer Size (in bytes)");
}

/* Output the compilation unit DIE and its children.  */

static void
output_comp_unit (dw_die_ref die, int output_if_empty)
{
  const char *secname;
  char *oldsym, *tmp;

  /* Unless we are outputting main CU, we may throw away empty ones.  */
  if (!output_if_empty && die->die_child == NULL)
    return;

  /* Even if there are no children of this DIE, we must output the information
     about the compilation unit.  Otherwise, on an empty translation unit, we
     will generate a present, but empty, .debug_info section.  IRIX 6.5 `nm'
     will then complain when examining the file.  First mark all the DIEs in
     this CU so we know which get local refs.  */
  mark_dies (die);

  build_abbrev_table (die);

  /* Initialize the beginning DIE offset - and calculate sizes/offsets.  */
  next_die_offset = DWARF_COMPILE_UNIT_HEADER_SIZE;
  calc_die_sizes (die);

  oldsym = die->die_symbol;
  if (oldsym)
    {
      tmp = alloca (strlen (oldsym) + 24);

      sprintf (tmp, ".gnu.linkonce.wi.%s", oldsym);
      secname = tmp;
      die->die_symbol = NULL;
      switch_to_section (get_section (secname, SECTION_DEBUG, NULL));
    }
  else
    switch_to_section (debug_info_section);

  /* Output debugging information.  */
  output_compilation_unit_header ();
  output_die (die);

  /* Leave the marks on the main CU, so we can check them in
     output_pubnames.  */
  if (oldsym)
    {
      unmark_dies (die);
      die->die_symbol = oldsym;
    }
}

/* Return the DWARF2/3 pubname associated with a decl.  */

static const char *
dwarf2_name (tree decl, int scope)
{
  return lang_hooks.dwarf_name (decl, scope ? 1 : 0);
}

/* Add a new entry to .debug_pubnames if appropriate.  */

static void
add_pubname (tree decl, dw_die_ref die)
{
  pubname_ref p;

  if (! TREE_PUBLIC (decl))
    return;

  if (pubname_table_in_use == pubname_table_allocated)
    {
      pubname_table_allocated += PUBNAME_TABLE_INCREMENT;
      pubname_table
	= ggc_realloc (pubname_table,
		       (pubname_table_allocated * sizeof (pubname_entry)));
      memset (pubname_table + pubname_table_in_use, 0,
	      PUBNAME_TABLE_INCREMENT * sizeof (pubname_entry));
    }

  p = &pubname_table[pubname_table_in_use++];
  p->die = die;
  p->name = xstrdup (dwarf2_name (decl, 1));
}

/* Output the public names table used to speed up access to externally
   visible names.  For now, only generate entries for externally
   visible procedures.  */

static void
output_pubnames (void)
{
  unsigned i;
  unsigned long pubnames_length = size_of_pubnames ();

  if (DWARF_INITIAL_LENGTH_SIZE - DWARF_OFFSET_SIZE == 4)
    dw2_asm_output_data (4, 0xffffffff,
      "Initial length escape value indicating 64-bit DWARF extension");
  dw2_asm_output_data (DWARF_OFFSET_SIZE, pubnames_length,
		       "Length of Public Names Info");
  dw2_asm_output_data (2, DWARF_VERSION, "DWARF Version");
  dw2_asm_output_offset (DWARF_OFFSET_SIZE, debug_info_section_label,
			 debug_info_section,
			 "Offset of Compilation Unit Info");
  dw2_asm_output_data (DWARF_OFFSET_SIZE, next_die_offset,
		       "Compilation Unit Length");

  for (i = 0; i < pubname_table_in_use; i++)
    {
      pubname_ref pub = &pubname_table[i];

      /* We shouldn't see pubnames for DIEs outside of the main CU.  */
      gcc_assert (pub->die->die_mark);

      dw2_asm_output_data (DWARF_OFFSET_SIZE, pub->die->die_offset,
			   "DIE offset");

      dw2_asm_output_nstring (pub->name, -1, "external name");
    }

  dw2_asm_output_data (DWARF_OFFSET_SIZE, 0, NULL);
}

/* Add a new entry to .debug_aranges if appropriate.  */

static void
add_arange (tree decl, dw_die_ref die)
{
  if (! DECL_SECTION_NAME (decl))
    return;

  if (arange_table_in_use == arange_table_allocated)
    {
      arange_table_allocated += ARANGE_TABLE_INCREMENT;
      arange_table = ggc_realloc (arange_table,
				  (arange_table_allocated
				   * sizeof (dw_die_ref)));
      memset (arange_table + arange_table_in_use, 0,
	      ARANGE_TABLE_INCREMENT * sizeof (dw_die_ref));
    }

  arange_table[arange_table_in_use++] = die;
}

/* Output the information that goes into the .debug_aranges table.
   Namely, define the beginning and ending address range of the
   text section generated for this compilation unit.  */

static void
output_aranges (void)
{
  unsigned i;
  unsigned long aranges_length = size_of_aranges ();

  if (DWARF_INITIAL_LENGTH_SIZE - DWARF_OFFSET_SIZE == 4)
    dw2_asm_output_data (4, 0xffffffff,
      "Initial length escape value indicating 64-bit DWARF extension");
  dw2_asm_output_data (DWARF_OFFSET_SIZE, aranges_length,
		       "Length of Address Ranges Info");
  dw2_asm_output_data (2, DWARF_VERSION, "DWARF Version");
  dw2_asm_output_offset (DWARF_OFFSET_SIZE, debug_info_section_label,
			 debug_info_section,
			 "Offset of Compilation Unit Info");
  dw2_asm_output_data (1, DWARF2_ADDR_SIZE, "Size of Address");
  dw2_asm_output_data (1, 0, "Size of Segment Descriptor");

  /* We need to align to twice the pointer size here.  */
  if (DWARF_ARANGES_PAD_SIZE)
    {
      /* Pad using a 2 byte words so that padding is correct for any
	 pointer size.  */
      dw2_asm_output_data (2, 0, "Pad to %d byte boundary",
			   2 * DWARF2_ADDR_SIZE);
      for (i = 2; i < (unsigned) DWARF_ARANGES_PAD_SIZE; i += 2)
	dw2_asm_output_data (2, 0, NULL);
    }

  dw2_asm_output_addr (DWARF2_ADDR_SIZE, text_section_label, "Address");
  dw2_asm_output_delta (DWARF2_ADDR_SIZE, text_end_label,
			text_section_label, "Length");
  if (flag_reorder_blocks_and_partition)
    {
      dw2_asm_output_addr (DWARF2_ADDR_SIZE, cold_text_section_label, 
			   "Address");
      dw2_asm_output_delta (DWARF2_ADDR_SIZE, cold_end_label,
			    cold_text_section_label, "Length");
    }

  for (i = 0; i < arange_table_in_use; i++)
    {
      dw_die_ref die = arange_table[i];

      /* We shouldn't see aranges for DIEs outside of the main CU.  */
      gcc_assert (die->die_mark);

      if (die->die_tag == DW_TAG_subprogram)
	{
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, get_AT_low_pc (die),
			       "Address");
	  dw2_asm_output_delta (DWARF2_ADDR_SIZE, get_AT_hi_pc (die),
				get_AT_low_pc (die), "Length");
	}
      else
	{
	  /* A static variable; extract the symbol from DW_AT_location.
	     Note that this code isn't currently hit, as we only emit
	     aranges for functions (jason 9/23/99).  */
	  dw_attr_ref a = get_AT (die, DW_AT_location);
	  dw_loc_descr_ref loc;

	  gcc_assert (a && AT_class (a) == dw_val_class_loc);

	  loc = AT_loc (a);
	  gcc_assert (loc->dw_loc_opc == DW_OP_addr);

	  dw2_asm_output_addr_rtx (DWARF2_ADDR_SIZE,
				   loc->dw_loc_oprnd1.v.val_addr, "Address");
	  dw2_asm_output_data (DWARF2_ADDR_SIZE,
			       get_AT_unsigned (die, DW_AT_byte_size),
			       "Length");
	}
    }

  /* Output the terminator words.  */
  dw2_asm_output_data (DWARF2_ADDR_SIZE, 0, NULL);
  dw2_asm_output_data (DWARF2_ADDR_SIZE, 0, NULL);
}

/* Add a new entry to .debug_ranges.  Return the offset at which it
   was placed.  */

static unsigned int
add_ranges (tree block)
{
  unsigned int in_use = ranges_table_in_use;

  if (in_use == ranges_table_allocated)
    {
      ranges_table_allocated += RANGES_TABLE_INCREMENT;
      ranges_table
	= ggc_realloc (ranges_table, (ranges_table_allocated
				      * sizeof (struct dw_ranges_struct)));
      memset (ranges_table + ranges_table_in_use, 0,
	      RANGES_TABLE_INCREMENT * sizeof (struct dw_ranges_struct));
    }

  ranges_table[in_use].block_num = (block ? BLOCK_NUMBER (block) : 0);
  ranges_table_in_use = in_use + 1;

  return in_use * 2 * DWARF2_ADDR_SIZE;
}

static void
output_ranges (void)
{
  unsigned i;
  static const char *const start_fmt = "Offset 0x%x";
  const char *fmt = start_fmt;

  for (i = 0; i < ranges_table_in_use; i++)
    {
      int block_num = ranges_table[i].block_num;

      if (block_num)
	{
	  char blabel[MAX_ARTIFICIAL_LABEL_BYTES];
	  char elabel[MAX_ARTIFICIAL_LABEL_BYTES];

	  ASM_GENERATE_INTERNAL_LABEL (blabel, BLOCK_BEGIN_LABEL, block_num);
	  ASM_GENERATE_INTERNAL_LABEL (elabel, BLOCK_END_LABEL, block_num);

	  /* If all code is in the text section, then the compilation
	     unit base address defaults to DW_AT_low_pc, which is the
	     base of the text section.  */
	  if (!have_multiple_function_sections)
	    {
	      dw2_asm_output_delta (DWARF2_ADDR_SIZE, blabel,
				    text_section_label,
				    fmt, i * 2 * DWARF2_ADDR_SIZE);
	      dw2_asm_output_delta (DWARF2_ADDR_SIZE, elabel,
				    text_section_label, NULL);
	    }

	  /* Otherwise, we add a DW_AT_entry_pc attribute to force the
	     compilation unit base address to zero, which allows us to
	     use absolute addresses, and not worry about whether the
	     target supports cross-section arithmetic.  */
	  else
	    {
	      dw2_asm_output_addr (DWARF2_ADDR_SIZE, blabel,
				   fmt, i * 2 * DWARF2_ADDR_SIZE);
	      dw2_asm_output_addr (DWARF2_ADDR_SIZE, elabel, NULL);
	    }

	  fmt = NULL;
	}
      else
	{
	  dw2_asm_output_data (DWARF2_ADDR_SIZE, 0, NULL);
	  dw2_asm_output_data (DWARF2_ADDR_SIZE, 0, NULL);
	  fmt = start_fmt;
	}
    }
}

/* Data structure containing information about input files.  */
struct file_info
{
  const char *path;	/* Complete file name.  */
  const char *fname;	/* File name part.  */
  int length;		/* Length of entire string.  */
  struct dwarf_file_data * file_idx;	/* Index in input file table.  */
  int dir_idx;		/* Index in directory table.  */
};

/* Data structure containing information about directories with source
   files.  */
struct dir_info
{
  const char *path;	/* Path including directory name.  */
  int length;		/* Path length.  */
  int prefix;		/* Index of directory entry which is a prefix.  */
  int count;		/* Number of files in this directory.  */
  int dir_idx;		/* Index of directory used as base.  */
};

/* Callback function for file_info comparison.  We sort by looking at
   the directories in the path.  */

static int
file_info_cmp (const void *p1, const void *p2)
{
  const struct file_info *s1 = p1;
  const struct file_info *s2 = p2;
  unsigned char *cp1;
  unsigned char *cp2;

  /* Take care of file names without directories.  We need to make sure that
     we return consistent values to qsort since some will get confused if
     we return the same value when identical operands are passed in opposite
     orders.  So if neither has a directory, return 0 and otherwise return
     1 or -1 depending on which one has the directory.  */
  if ((s1->path == s1->fname || s2->path == s2->fname))
    return (s2->path == s2->fname) - (s1->path == s1->fname);

  cp1 = (unsigned char *) s1->path;
  cp2 = (unsigned char *) s2->path;

  while (1)
    {
      ++cp1;
      ++cp2;
      /* Reached the end of the first path?  If so, handle like above.  */
      if ((cp1 == (unsigned char *) s1->fname)
	  || (cp2 == (unsigned char *) s2->fname))
	return ((cp2 == (unsigned char *) s2->fname)
		- (cp1 == (unsigned char *) s1->fname));

      /* Character of current path component the same?  */
      else if (*cp1 != *cp2)
	return *cp1 - *cp2;
    }
}

struct file_name_acquire_data 
{
  struct file_info *files;
  int used_files;
  int max_files;
};

/* Traversal function for the hash table.  */

static int
file_name_acquire (void ** slot, void *data)
{
  struct file_name_acquire_data *fnad = data;
  struct dwarf_file_data *d = *slot;
  struct file_info *fi;
  const char *f;

  gcc_assert (fnad->max_files >= d->emitted_number);

  if (! d->emitted_number)
    return 1;

  gcc_assert (fnad->max_files != fnad->used_files);

  fi = fnad->files + fnad->used_files++;

  /* Skip all leading "./".  */
  f = d->filename;
  while (f[0] == '.' && f[1] == '/')
    f += 2;
  
  /* Create a new array entry.  */
  fi->path = f;
  fi->length = strlen (f);
  fi->file_idx = d;
  
  /* Search for the file name part.  */
  f = strrchr (f, '/');
  fi->fname = f == NULL ? fi->path : f + 1;
  return 1;
}

/* Output the directory table and the file name table.  We try to minimize
   the total amount of memory needed.  A heuristic is used to avoid large
   slowdowns with many input files.  */

static void
output_file_names (void)
{
  struct file_name_acquire_data fnad;
  int numfiles;
  struct file_info *files;
  struct dir_info *dirs;
  int *saved;
  int *savehere;
  int *backmap;
  int ndirs;
  int idx_offset;
  int i;
  int idx;

  if (!last_emitted_file)
    {
      dw2_asm_output_data (1, 0, "End directory table");
      dw2_asm_output_data (1, 0, "End file name table");
      return;
    }

  numfiles = last_emitted_file->emitted_number;

  /* Allocate the various arrays we need.  */
  files = alloca (numfiles * sizeof (struct file_info));
  dirs = alloca (numfiles * sizeof (struct dir_info));

  fnad.files = files;
  fnad.used_files = 0;
  fnad.max_files = numfiles;
  htab_traverse (file_table, file_name_acquire, &fnad);
  gcc_assert (fnad.used_files == fnad.max_files);

  qsort (files, numfiles, sizeof (files[0]), file_info_cmp);

  /* Find all the different directories used.  */
  dirs[0].path = files[0].path;
  dirs[0].length = files[0].fname - files[0].path;
  dirs[0].prefix = -1;
  dirs[0].count = 1;
  dirs[0].dir_idx = 0;
  files[0].dir_idx = 0;
  ndirs = 1;

  for (i = 1; i < numfiles; i++)
    if (files[i].fname - files[i].path == dirs[ndirs - 1].length
	&& memcmp (dirs[ndirs - 1].path, files[i].path,
		   dirs[ndirs - 1].length) == 0)
      {
	/* Same directory as last entry.  */
	files[i].dir_idx = ndirs - 1;
	++dirs[ndirs - 1].count;
      }
    else
      {
	int j;

	/* This is a new directory.  */
	dirs[ndirs].path = files[i].path;
	dirs[ndirs].length = files[i].fname - files[i].path;
	dirs[ndirs].count = 1;
	dirs[ndirs].dir_idx = ndirs;
	files[i].dir_idx = ndirs;

	/* Search for a prefix.  */
	dirs[ndirs].prefix = -1;
	for (j = 0; j < ndirs; j++)
	  if (dirs[j].length < dirs[ndirs].length
	      && dirs[j].length > 1
	      && (dirs[ndirs].prefix == -1
		  || dirs[j].length > dirs[dirs[ndirs].prefix].length)
	      && memcmp (dirs[j].path, dirs[ndirs].path, dirs[j].length) == 0)
	    dirs[ndirs].prefix = j;

	++ndirs;
      }

  /* Now to the actual work.  We have to find a subset of the directories which
     allow expressing the file name using references to the directory table
     with the least amount of characters.  We do not do an exhaustive search
     where we would have to check out every combination of every single
     possible prefix.  Instead we use a heuristic which provides nearly optimal
     results in most cases and never is much off.  */
  saved = alloca (ndirs * sizeof (int));
  savehere = alloca (ndirs * sizeof (int));

  memset (saved, '\0', ndirs * sizeof (saved[0]));
  for (i = 0; i < ndirs; i++)
    {
      int j;
      int total;

      /* We can always save some space for the current directory.  But this
	 does not mean it will be enough to justify adding the directory.  */
      savehere[i] = dirs[i].length;
      total = (savehere[i] - saved[i]) * dirs[i].count;

      for (j = i + 1; j < ndirs; j++)
	{
	  savehere[j] = 0;
	  if (saved[j] < dirs[i].length)
	    {
	      /* Determine whether the dirs[i] path is a prefix of the
		 dirs[j] path.  */
	      int k;

	      k = dirs[j].prefix;
	      while (k != -1 && k != (int) i)
		k = dirs[k].prefix;

	      if (k == (int) i)
		{
		  /* Yes it is.  We can possibly save some memory by
		     writing the filenames in dirs[j] relative to
		     dirs[i].  */
		  savehere[j] = dirs[i].length;
		  total += (savehere[j] - saved[j]) * dirs[j].count;
		}
	    }
	}

      /* Check whether we can save enough to justify adding the dirs[i]
	 directory.  */
      if (total > dirs[i].length + 1)
	{
	  /* It's worthwhile adding.  */
	  for (j = i; j < ndirs; j++)
	    if (savehere[j] > 0)
	      {
		/* Remember how much we saved for this directory so far.  */
		saved[j] = savehere[j];

		/* Remember the prefix directory.  */
		dirs[j].dir_idx = i;
	      }
	}
    }

  /* Emit the directory name table.  */
  idx = 1;
  idx_offset = dirs[0].length > 0 ? 1 : 0;
  for (i = 1 - idx_offset; i < ndirs; i++)
    dw2_asm_output_nstring (dirs[i].path, dirs[i].length - 1,
			    "Directory Entry: 0x%x", i + idx_offset);

  dw2_asm_output_data (1, 0, "End directory table");

  /* We have to emit them in the order of emitted_number since that's
     used in the debug info generation.  To do this efficiently we
     generate a back-mapping of the indices first.  */
  backmap = alloca (numfiles * sizeof (int));
  for (i = 0; i < numfiles; i++)
    backmap[files[i].file_idx->emitted_number - 1] = i;

  /* Now write all the file names.  */
  for (i = 0; i < numfiles; i++)
    {
      int file_idx = backmap[i];
      int dir_idx = dirs[files[file_idx].dir_idx].dir_idx;

      dw2_asm_output_nstring (files[file_idx].path + dirs[dir_idx].length, -1,
			      "File Entry: 0x%x", (unsigned) i + 1);

      /* Include directory index.  */
      dw2_asm_output_data_uleb128 (dir_idx + idx_offset, NULL);

      /* Modification time.  */
      dw2_asm_output_data_uleb128 (0, NULL);

      /* File length in bytes.  */
      dw2_asm_output_data_uleb128 (0, NULL);
    }

  dw2_asm_output_data (1, 0, "End file name table");
}


/* Output the source line number correspondence information.  This
   information goes into the .debug_line section.  */

static void
output_line_info (void)
{
  char l1[20], l2[20], p1[20], p2[20];
  char line_label[MAX_ARTIFICIAL_LABEL_BYTES];
  char prev_line_label[MAX_ARTIFICIAL_LABEL_BYTES];
  unsigned opc;
  unsigned n_op_args;
  unsigned long lt_index;
  unsigned long current_line;
  long line_offset;
  long line_delta;
  unsigned long current_file;
  unsigned long function;

  ASM_GENERATE_INTERNAL_LABEL (l1, LINE_NUMBER_BEGIN_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (l2, LINE_NUMBER_END_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (p1, LN_PROLOG_AS_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (p2, LN_PROLOG_END_LABEL, 0);

  if (DWARF_INITIAL_LENGTH_SIZE - DWARF_OFFSET_SIZE == 4)
    dw2_asm_output_data (4, 0xffffffff,
      "Initial length escape value indicating 64-bit DWARF extension");
  dw2_asm_output_delta (DWARF_OFFSET_SIZE, l2, l1,
			"Length of Source Line Info");
  ASM_OUTPUT_LABEL (asm_out_file, l1);

  dw2_asm_output_data (2, DWARF_VERSION, "DWARF Version");
  dw2_asm_output_delta (DWARF_OFFSET_SIZE, p2, p1, "Prolog Length");
  ASM_OUTPUT_LABEL (asm_out_file, p1);

  /* Define the architecture-dependent minimum instruction length (in
   bytes).  In this implementation of DWARF, this field is used for
   information purposes only.  Since GCC generates assembly language,
   we have no a priori knowledge of how many instruction bytes are
   generated for each source line, and therefore can use only the
   DW_LNE_set_address and DW_LNS_fixed_advance_pc line information
   commands.  Accordingly, we fix this as `1', which is "correct
   enough" for all architectures, and don't let the target override.  */
  dw2_asm_output_data (1, 1,
		       "Minimum Instruction Length");

  dw2_asm_output_data (1, DWARF_LINE_DEFAULT_IS_STMT_START,
		       "Default is_stmt_start flag");
  dw2_asm_output_data (1, DWARF_LINE_BASE,
		       "Line Base Value (Special Opcodes)");
  dw2_asm_output_data (1, DWARF_LINE_RANGE,
		       "Line Range Value (Special Opcodes)");
  dw2_asm_output_data (1, DWARF_LINE_OPCODE_BASE,
		       "Special Opcode Base");

  for (opc = 1; opc < DWARF_LINE_OPCODE_BASE; opc++)
    {
      switch (opc)
	{
	case DW_LNS_advance_pc:
	case DW_LNS_advance_line:
	case DW_LNS_set_file:
	case DW_LNS_set_column:
	case DW_LNS_fixed_advance_pc:
	  n_op_args = 1;
	  break;
	default:
	  n_op_args = 0;
	  break;
	}

      dw2_asm_output_data (1, n_op_args, "opcode: 0x%x has %d args",
			   opc, n_op_args);
    }

  /* Write out the information about the files we use.  */
  output_file_names ();
  ASM_OUTPUT_LABEL (asm_out_file, p2);

  /* We used to set the address register to the first location in the text
     section here, but that didn't accomplish anything since we already
     have a line note for the opening brace of the first function.  */

  /* Generate the line number to PC correspondence table, encoded as
     a series of state machine operations.  */
  current_file = 1;
  current_line = 1;

  if (cfun && in_cold_section_p)
    strcpy (prev_line_label, cfun->cold_section_label);
  else
    strcpy (prev_line_label, text_section_label);
  for (lt_index = 1; lt_index < line_info_table_in_use; ++lt_index)
    {
      dw_line_info_ref line_info = &line_info_table[lt_index];

#if 0
      /* Disable this optimization for now; GDB wants to see two line notes
	 at the beginning of a function so it can find the end of the
	 prologue.  */

      /* Don't emit anything for redundant notes.  Just updating the
	 address doesn't accomplish anything, because we already assume
	 that anything after the last address is this line.  */
      if (line_info->dw_line_num == current_line
	  && line_info->dw_file_num == current_file)
	continue;
#endif

      /* Emit debug info for the address of the current line.

	 Unfortunately, we have little choice here currently, and must always
	 use the most general form.  GCC does not know the address delta
	 itself, so we can't use DW_LNS_advance_pc.  Many ports do have length
	 attributes which will give an upper bound on the address range.  We
	 could perhaps use length attributes to determine when it is safe to
	 use DW_LNS_fixed_advance_pc.  */

      ASM_GENERATE_INTERNAL_LABEL (line_label, LINE_CODE_LABEL, lt_index);
      if (0)
	{
	  /* This can handle deltas up to 0xffff.  This takes 3 bytes.  */
	  dw2_asm_output_data (1, DW_LNS_fixed_advance_pc,
			       "DW_LNS_fixed_advance_pc");
	  dw2_asm_output_delta (2, line_label, prev_line_label, NULL);
	}
      else
	{
	  /* This can handle any delta.  This takes
	     4+DWARF2_ADDR_SIZE bytes.  */
	  dw2_asm_output_data (1, 0, "DW_LNE_set_address");
	  dw2_asm_output_data_uleb128 (1 + DWARF2_ADDR_SIZE, NULL);
	  dw2_asm_output_data (1, DW_LNE_set_address, NULL);
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, line_label, NULL);
	}

      strcpy (prev_line_label, line_label);

      /* Emit debug info for the source file of the current line, if
	 different from the previous line.  */
      if (line_info->dw_file_num != current_file)
	{
	  current_file = line_info->dw_file_num;
	  dw2_asm_output_data (1, DW_LNS_set_file, "DW_LNS_set_file");
	  dw2_asm_output_data_uleb128 (current_file, "%lu", current_file);
	}

      /* Emit debug info for the current line number, choosing the encoding
	 that uses the least amount of space.  */
      if (line_info->dw_line_num != current_line)
	{
	  line_offset = line_info->dw_line_num - current_line;
	  line_delta = line_offset - DWARF_LINE_BASE;
	  current_line = line_info->dw_line_num;
	  if (line_delta >= 0 && line_delta < (DWARF_LINE_RANGE - 1))
	    /* This can handle deltas from -10 to 234, using the current
	       definitions of DWARF_LINE_BASE and DWARF_LINE_RANGE.  This
	       takes 1 byte.  */
	    dw2_asm_output_data (1, DWARF_LINE_OPCODE_BASE + line_delta,
				 "line %lu", current_line);
	  else
	    {
	      /* This can handle any delta.  This takes at least 4 bytes,
		 depending on the value being encoded.  */
	      dw2_asm_output_data (1, DW_LNS_advance_line,
				   "advance to line %lu", current_line);
	      dw2_asm_output_data_sleb128 (line_offset, NULL);
	      dw2_asm_output_data (1, DW_LNS_copy, "DW_LNS_copy");
	    }
	}
      else
	/* We still need to start a new row, so output a copy insn.  */
	dw2_asm_output_data (1, DW_LNS_copy, "DW_LNS_copy");
    }

  /* Emit debug info for the address of the end of the function.  */
  if (0)
    {
      dw2_asm_output_data (1, DW_LNS_fixed_advance_pc,
			   "DW_LNS_fixed_advance_pc");
      dw2_asm_output_delta (2, text_end_label, prev_line_label, NULL);
    }
  else
    {
      dw2_asm_output_data (1, 0, "DW_LNE_set_address");
      dw2_asm_output_data_uleb128 (1 + DWARF2_ADDR_SIZE, NULL);
      dw2_asm_output_data (1, DW_LNE_set_address, NULL);
      dw2_asm_output_addr (DWARF2_ADDR_SIZE, text_end_label, NULL);
    }

  dw2_asm_output_data (1, 0, "DW_LNE_end_sequence");
  dw2_asm_output_data_uleb128 (1, NULL);
  dw2_asm_output_data (1, DW_LNE_end_sequence, NULL);

  function = 0;
  current_file = 1;
  current_line = 1;
  for (lt_index = 0; lt_index < separate_line_info_table_in_use;)
    {
      dw_separate_line_info_ref line_info
	= &separate_line_info_table[lt_index];

#if 0
      /* Don't emit anything for redundant notes.  */
      if (line_info->dw_line_num == current_line
	  && line_info->dw_file_num == current_file
	  && line_info->function == function)
	goto cont;
#endif

      /* Emit debug info for the address of the current line.  If this is
	 a new function, or the first line of a function, then we need
	 to handle it differently.  */
      ASM_GENERATE_INTERNAL_LABEL (line_label, SEPARATE_LINE_CODE_LABEL,
				   lt_index);
      if (function != line_info->function)
	{
	  function = line_info->function;

	  /* Set the address register to the first line in the function.  */
	  dw2_asm_output_data (1, 0, "DW_LNE_set_address");
	  dw2_asm_output_data_uleb128 (1 + DWARF2_ADDR_SIZE, NULL);
	  dw2_asm_output_data (1, DW_LNE_set_address, NULL);
	  dw2_asm_output_addr (DWARF2_ADDR_SIZE, line_label, NULL);
	}
      else
	{
	  /* ??? See the DW_LNS_advance_pc comment above.  */
	  if (0)
	    {
	      dw2_asm_output_data (1, DW_LNS_fixed_advance_pc,
				   "DW_LNS_fixed_advance_pc");
	      dw2_asm_output_delta (2, line_label, prev_line_label, NULL);
	    }
	  else
	    {
	      dw2_asm_output_data (1, 0, "DW_LNE_set_address");
	      dw2_asm_output_data_uleb128 (1 + DWARF2_ADDR_SIZE, NULL);
	      dw2_asm_output_data (1, DW_LNE_set_address, NULL);
	      dw2_asm_output_addr (DWARF2_ADDR_SIZE, line_label, NULL);
	    }
	}

      strcpy (prev_line_label, line_label);

      /* Emit debug info for the source file of the current line, if
	 different from the previous line.  */
      if (line_info->dw_file_num != current_file)
	{
	  current_file = line_info->dw_file_num;
	  dw2_asm_output_data (1, DW_LNS_set_file, "DW_LNS_set_file");
	  dw2_asm_output_data_uleb128 (current_file, "%lu", current_file);
	}

      /* Emit debug info for the current line number, choosing the encoding
	 that uses the least amount of space.  */
      if (line_info->dw_line_num != current_line)
	{
	  line_offset = line_info->dw_line_num - current_line;
	  line_delta = line_offset - DWARF_LINE_BASE;
	  current_line = line_info->dw_line_num;
	  if (line_delta >= 0 && line_delta < (DWARF_LINE_RANGE - 1))
	    dw2_asm_output_data (1, DWARF_LINE_OPCODE_BASE + line_delta,
				 "line %lu", current_line);
	  else
	    {
	      dw2_asm_output_data (1, DW_LNS_advance_line,
				   "advance to line %lu", current_line);
	      dw2_asm_output_data_sleb128 (line_offset, NULL);
	      dw2_asm_output_data (1, DW_LNS_copy, "DW_LNS_copy");
	    }
	}
      else
	dw2_asm_output_data (1, DW_LNS_copy, "DW_LNS_copy");

#if 0
    cont:
#endif

      lt_index++;

      /* If we're done with a function, end its sequence.  */
      if (lt_index == separate_line_info_table_in_use
	  || separate_line_info_table[lt_index].function != function)
	{
	  current_file = 1;
	  current_line = 1;

	  /* Emit debug info for the address of the end of the function.  */
	  ASM_GENERATE_INTERNAL_LABEL (line_label, FUNC_END_LABEL, function);
	  if (0)
	    {
	      dw2_asm_output_data (1, DW_LNS_fixed_advance_pc,
				   "DW_LNS_fixed_advance_pc");
	      dw2_asm_output_delta (2, line_label, prev_line_label, NULL);
	    }
	  else
	    {
	      dw2_asm_output_data (1, 0, "DW_LNE_set_address");
	      dw2_asm_output_data_uleb128 (1 + DWARF2_ADDR_SIZE, NULL);
	      dw2_asm_output_data (1, DW_LNE_set_address, NULL);
	      dw2_asm_output_addr (DWARF2_ADDR_SIZE, line_label, NULL);
	    }

	  /* Output the marker for the end of this sequence.  */
	  dw2_asm_output_data (1, 0, "DW_LNE_end_sequence");
	  dw2_asm_output_data_uleb128 (1, NULL);
	  dw2_asm_output_data (1, DW_LNE_end_sequence, NULL);
	}
    }

  /* Output the marker for the end of the line number info.  */
  ASM_OUTPUT_LABEL (asm_out_file, l2);
}

/* Given a pointer to a tree node for some base type, return a pointer to
   a DIE that describes the given type.

   This routine must only be called for GCC type nodes that correspond to
   Dwarf base (fundamental) types.  */

static dw_die_ref
base_type_die (tree type)
{
  dw_die_ref base_type_result;
  enum dwarf_type encoding;

  if (TREE_CODE (type) == ERROR_MARK || TREE_CODE (type) == VOID_TYPE)
    return 0;

  switch (TREE_CODE (type))
    {
    case INTEGER_TYPE:
      if (TYPE_STRING_FLAG (type))
	{
	  if (TYPE_UNSIGNED (type))
	    encoding = DW_ATE_unsigned_char;
	  else
	    encoding = DW_ATE_signed_char;
	}
      else if (TYPE_UNSIGNED (type))
	encoding = DW_ATE_unsigned;
      else
	encoding = DW_ATE_signed;
      break;

    case REAL_TYPE:
      if (DECIMAL_FLOAT_MODE_P (TYPE_MODE (type)))
	encoding = DW_ATE_decimal_float;
      else
	encoding = DW_ATE_float;
      break;

      /* Dwarf2 doesn't know anything about complex ints, so use
	 a user defined type for it.  */
    case COMPLEX_TYPE:
      if (TREE_CODE (TREE_TYPE (type)) == REAL_TYPE)
	encoding = DW_ATE_complex_float;
      else
	encoding = DW_ATE_lo_user;
      break;

    case BOOLEAN_TYPE:
      /* GNU FORTRAN/Ada/C++ BOOLEAN type.  */
      encoding = DW_ATE_boolean;
      break;

    default:
      /* No other TREE_CODEs are Dwarf fundamental types.  */
      gcc_unreachable ();
    }

  base_type_result = new_die (DW_TAG_base_type, comp_unit_die, type);

  /* This probably indicates a bug.  */
  if (! TYPE_NAME (type))
    add_name_attribute (base_type_result, "__unknown__");

  add_AT_unsigned (base_type_result, DW_AT_byte_size,
		   int_size_in_bytes (type));
  add_AT_unsigned (base_type_result, DW_AT_encoding, encoding);

  return base_type_result;
}

/* Given a pointer to an arbitrary ..._TYPE tree node, return a pointer to
   the Dwarf "root" type for the given input type.  The Dwarf "root" type of
   a given type is generally the same as the given type, except that if the
   given type is a pointer or reference type, then the root type of the given
   type is the root type of the "basis" type for the pointer or reference
   type.  (This definition of the "root" type is recursive.) Also, the root
   type of a `const' qualified type or a `volatile' qualified type is the
   root type of the given type without the qualifiers.  */

static tree
root_type (tree type)
{
  if (TREE_CODE (type) == ERROR_MARK)
    return error_mark_node;

  switch (TREE_CODE (type))
    {
    case ERROR_MARK:
      return error_mark_node;

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      return type_main_variant (root_type (TREE_TYPE (type)));

    default:
      return type_main_variant (type);
    }
}

/* Given a pointer to an arbitrary ..._TYPE tree node, return nonzero if the
   given input type is a Dwarf "fundamental" type.  Otherwise return null.  */

static inline int
is_base_type (tree type)
{
  switch (TREE_CODE (type))
    {
    case ERROR_MARK:
    case VOID_TYPE:
    case INTEGER_TYPE:
    case REAL_TYPE:
    case COMPLEX_TYPE:
    case BOOLEAN_TYPE:
      return 1;

    case ARRAY_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
    case ENUMERAL_TYPE:
    case FUNCTION_TYPE:
    case METHOD_TYPE:
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case OFFSET_TYPE:
    case LANG_TYPE:
    case VECTOR_TYPE:
      return 0;

    default:
      gcc_unreachable ();
    }

  return 0;
}

/* Given a pointer to a tree node, assumed to be some kind of a ..._TYPE
   node, return the size in bits for the type if it is a constant, or else
   return the alignment for the type if the type's size is not constant, or
   else return BITS_PER_WORD if the type actually turns out to be an
   ERROR_MARK node.  */

static inline unsigned HOST_WIDE_INT
simple_type_size_in_bits (tree type)
{
  if (TREE_CODE (type) == ERROR_MARK)
    return BITS_PER_WORD;
  else if (TYPE_SIZE (type) == NULL_TREE)
    return 0;
  else if (host_integerp (TYPE_SIZE (type), 1))
    return tree_low_cst (TYPE_SIZE (type), 1);
  else
    return TYPE_ALIGN (type);
}

/* Return true if the debug information for the given type should be
   emitted as a subrange type.  */

static inline bool
is_subrange_type (tree type)
{
  tree subtype = TREE_TYPE (type);

  /* Subrange types are identified by the fact that they are integer
     types, and that they have a subtype which is either an integer type
     or an enumeral type.  */

  if (TREE_CODE (type) != INTEGER_TYPE
      || subtype == NULL_TREE)
    return false;

  if (TREE_CODE (subtype) != INTEGER_TYPE
      && TREE_CODE (subtype) != ENUMERAL_TYPE)
    return false;

  if (TREE_CODE (type) == TREE_CODE (subtype)
      && int_size_in_bytes (type) == int_size_in_bytes (subtype)
      && TYPE_MIN_VALUE (type) != NULL
      && TYPE_MIN_VALUE (subtype) != NULL
      && tree_int_cst_equal (TYPE_MIN_VALUE (type), TYPE_MIN_VALUE (subtype))
      && TYPE_MAX_VALUE (type) != NULL
      && TYPE_MAX_VALUE (subtype) != NULL
      && tree_int_cst_equal (TYPE_MAX_VALUE (type), TYPE_MAX_VALUE (subtype)))
    {
      /* The type and its subtype have the same representation.  If in
         addition the two types also have the same name, then the given
         type is not a subrange type, but rather a plain base type.  */
      /* FIXME: brobecker/2004-03-22:
         Sizetype INTEGER_CSTs nodes are canonicalized.  It should
         therefore be sufficient to check the TYPE_SIZE node pointers
         rather than checking the actual size.  Unfortunately, we have
         found some cases, such as in the Ada "integer" type, where
         this is not the case.  Until this problem is solved, we need to
         keep checking the actual size.  */
      tree type_name = TYPE_NAME (type);
      tree subtype_name = TYPE_NAME (subtype);

      if (type_name != NULL && TREE_CODE (type_name) == TYPE_DECL)
        type_name = DECL_NAME (type_name);

      if (subtype_name != NULL && TREE_CODE (subtype_name) == TYPE_DECL)
        subtype_name = DECL_NAME (subtype_name);

      if (type_name == subtype_name)
        return false;
    }

  return true;
}

/*  Given a pointer to a tree node for a subrange type, return a pointer
    to a DIE that describes the given type.  */

static dw_die_ref
subrange_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref subrange_die;
  const HOST_WIDE_INT size_in_bytes = int_size_in_bytes (type);

  if (context_die == NULL)
    context_die = comp_unit_die;

  subrange_die = new_die (DW_TAG_subrange_type, context_die, type);

  if (int_size_in_bytes (TREE_TYPE (type)) != size_in_bytes)
    {
      /* The size of the subrange type and its base type do not match,
         so we need to generate a size attribute for the subrange type.  */
      add_AT_unsigned (subrange_die, DW_AT_byte_size, size_in_bytes);
    }

  if (TYPE_MIN_VALUE (type) != NULL)
    add_bound_info (subrange_die, DW_AT_lower_bound,
                    TYPE_MIN_VALUE (type));
  if (TYPE_MAX_VALUE (type) != NULL)
    add_bound_info (subrange_die, DW_AT_upper_bound,
                    TYPE_MAX_VALUE (type));

  return subrange_die;
}

/* Given a pointer to an arbitrary ..._TYPE tree node, return a debugging
   entry that chains various modifiers in front of the given type.  */

static dw_die_ref
modified_type_die (tree type, int is_const_type, int is_volatile_type,
		   dw_die_ref context_die)
{
  enum tree_code code = TREE_CODE (type);
  dw_die_ref mod_type_die;
  dw_die_ref sub_die = NULL;
  tree item_type = NULL;
  tree qualified_type;
  tree name;

  if (code == ERROR_MARK)
    return NULL;

  /* See if we already have the appropriately qualified variant of
     this type.  */
  qualified_type
    = get_qualified_type (type,
			  ((is_const_type ? TYPE_QUAL_CONST : 0)
			   | (is_volatile_type ? TYPE_QUAL_VOLATILE : 0)));
  
  /* If we do, then we can just use its DIE, if it exists.  */
  if (qualified_type)
    {
      mod_type_die = lookup_type_die (qualified_type);
      if (mod_type_die)
	return mod_type_die;
    }
  
  name = qualified_type ? TYPE_NAME (qualified_type) : NULL;
  
  /* Handle C typedef types.  */
  if (name && TREE_CODE (name) == TYPE_DECL && DECL_ORIGINAL_TYPE (name))
    {
      tree dtype = TREE_TYPE (name);
      
      if (qualified_type == dtype)
	{
	  /* For a named type, use the typedef.  */
	  gen_type_die (qualified_type, context_die);
	  return lookup_type_die (qualified_type);
	}
      else if (is_const_type < TYPE_READONLY (dtype)
	       || is_volatile_type < TYPE_VOLATILE (dtype)
	       || (is_const_type <= TYPE_READONLY (dtype)
		   && is_volatile_type <= TYPE_VOLATILE (dtype)
		   && DECL_ORIGINAL_TYPE (name) != type))
	/* cv-unqualified version of named type.  Just use the unnamed
	   type to which it refers.  */
	return modified_type_die (DECL_ORIGINAL_TYPE (name),
				  is_const_type, is_volatile_type,
				  context_die);
      /* Else cv-qualified version of named type; fall through.  */
    }
  
  if (is_const_type)
    {
      mod_type_die = new_die (DW_TAG_const_type, comp_unit_die, type);
      sub_die = modified_type_die (type, 0, is_volatile_type, context_die);
    }
  else if (is_volatile_type)
    {
      mod_type_die = new_die (DW_TAG_volatile_type, comp_unit_die, type);
      sub_die = modified_type_die (type, 0, 0, context_die);
    }
  else if (code == POINTER_TYPE)
    {
      mod_type_die = new_die (DW_TAG_pointer_type, comp_unit_die, type);
      add_AT_unsigned (mod_type_die, DW_AT_byte_size,
		       simple_type_size_in_bits (type) / BITS_PER_UNIT);
      item_type = TREE_TYPE (type);
    }
  else if (code == REFERENCE_TYPE)
    {
      mod_type_die = new_die (DW_TAG_reference_type, comp_unit_die, type);
      add_AT_unsigned (mod_type_die, DW_AT_byte_size,
		       simple_type_size_in_bits (type) / BITS_PER_UNIT);
      item_type = TREE_TYPE (type);
    }
  else if (is_subrange_type (type))
    {
      mod_type_die = subrange_type_die (type, context_die);
      item_type = TREE_TYPE (type);
    }
  else if (is_base_type (type))
    mod_type_die = base_type_die (type);
  else
    {
      gen_type_die (type, context_die);
      
      /* We have to get the type_main_variant here (and pass that to the
	 `lookup_type_die' routine) because the ..._TYPE node we have
	 might simply be a *copy* of some original type node (where the
	 copy was created to help us keep track of typedef names) and
	 that copy might have a different TYPE_UID from the original
	 ..._TYPE node.  */
      if (TREE_CODE (type) != VECTOR_TYPE)
	return lookup_type_die (type_main_variant (type));
      else
	/* Vectors have the debugging information in the type,
	   not the main variant.  */
	return lookup_type_die (type);
    }
  
  /* Builtin types don't have a DECL_ORIGINAL_TYPE.  For those,
     don't output a DW_TAG_typedef, since there isn't one in the
     user's program; just attach a DW_AT_name to the type.  */
  if (name
      && (TREE_CODE (name) != TYPE_DECL || TREE_TYPE (name) == qualified_type))
    {
      if (TREE_CODE (name) == TYPE_DECL)
	/* Could just call add_name_and_src_coords_attributes here,
	   but since this is a builtin type it doesn't have any
	   useful source coordinates anyway.  */
	name = DECL_NAME (name);
      add_name_attribute (mod_type_die, IDENTIFIER_POINTER (name));
    }
  
  if (qualified_type)
    equate_type_number_to_die (qualified_type, mod_type_die);

  if (item_type)
    /* We must do this after the equate_type_number_to_die call, in case
       this is a recursive type.  This ensures that the modified_type_die
       recursion will terminate even if the type is recursive.  Recursive
       types are possible in Ada.  */
    sub_die = modified_type_die (item_type,
				 TYPE_READONLY (item_type),
				 TYPE_VOLATILE (item_type),
				 context_die);

  if (sub_die != NULL)
    add_AT_die_ref (mod_type_die, DW_AT_type, sub_die);

  return mod_type_die;
}

/* Given a pointer to an arbitrary ..._TYPE tree node, return true if it is
   an enumerated type.  */

static inline int
type_is_enum (tree type)
{
  return TREE_CODE (type) == ENUMERAL_TYPE;
}

/* Return the DBX register number described by a given RTL node.  */

static unsigned int
dbx_reg_number (rtx rtl)
{
  unsigned regno = REGNO (rtl);

  gcc_assert (regno < FIRST_PSEUDO_REGISTER);

#ifdef LEAF_REG_REMAP
  if (current_function_uses_only_leaf_regs)
    {
      int leaf_reg = LEAF_REG_REMAP (regno);
      if (leaf_reg != -1)
	regno = (unsigned) leaf_reg;
    }
#endif

  return DBX_REGISTER_NUMBER (regno);
}

/* Optionally add a DW_OP_piece term to a location description expression.
   DW_OP_piece is only added if the location description expression already
   doesn't end with DW_OP_piece.  */

static void
add_loc_descr_op_piece (dw_loc_descr_ref *list_head, int size)
{
  dw_loc_descr_ref loc;

  if (*list_head != NULL)
    {
      /* Find the end of the chain.  */
      for (loc = *list_head; loc->dw_loc_next != NULL; loc = loc->dw_loc_next)
	;

      if (loc->dw_loc_opc != DW_OP_piece)
	loc->dw_loc_next = new_loc_descr (DW_OP_piece, size, 0);
    }
}

/* Return a location descriptor that designates a machine register or
   zero if there is none.  */

static dw_loc_descr_ref
reg_loc_descriptor (rtx rtl)
{
  rtx regs;

  if (REGNO (rtl) >= FIRST_PSEUDO_REGISTER)
    return 0;

  regs = targetm.dwarf_register_span (rtl);

  if (hard_regno_nregs[REGNO (rtl)][GET_MODE (rtl)] > 1 || regs)
    return multiple_reg_loc_descriptor (rtl, regs);
  else
    return one_reg_loc_descriptor (dbx_reg_number (rtl));
}

/* Return a location descriptor that designates a machine register for
   a given hard register number.  */

static dw_loc_descr_ref
one_reg_loc_descriptor (unsigned int regno)
{
  if (regno <= 31)
    return new_loc_descr (DW_OP_reg0 + regno, 0, 0);
  else
    return new_loc_descr (DW_OP_regx, regno, 0);
}

/* Given an RTL of a register, return a location descriptor that
   designates a value that spans more than one register.  */

static dw_loc_descr_ref
multiple_reg_loc_descriptor (rtx rtl, rtx regs)
{
  int nregs, size, i;
  unsigned reg;
  dw_loc_descr_ref loc_result = NULL;

  reg = REGNO (rtl);
#ifdef LEAF_REG_REMAP
  if (current_function_uses_only_leaf_regs)
    {
      int leaf_reg = LEAF_REG_REMAP (reg);
      if (leaf_reg != -1)
	reg = (unsigned) leaf_reg;
    }
#endif
  gcc_assert ((unsigned) DBX_REGISTER_NUMBER (reg) == dbx_reg_number (rtl));
  nregs = hard_regno_nregs[REGNO (rtl)][GET_MODE (rtl)];

  /* Simple, contiguous registers.  */
  if (regs == NULL_RTX)
    {
      size = GET_MODE_SIZE (GET_MODE (rtl)) / nregs;

      loc_result = NULL;
      while (nregs--)
	{
	  dw_loc_descr_ref t;

	  t = one_reg_loc_descriptor (DBX_REGISTER_NUMBER (reg));
	  add_loc_descr (&loc_result, t);
	  add_loc_descr_op_piece (&loc_result, size);
	  ++reg;
	}
      return loc_result;
    }

  /* Now onto stupid register sets in non contiguous locations.  */

  gcc_assert (GET_CODE (regs) == PARALLEL);

  size = GET_MODE_SIZE (GET_MODE (XVECEXP (regs, 0, 0)));
  loc_result = NULL;

  for (i = 0; i < XVECLEN (regs, 0); ++i)
    {
      dw_loc_descr_ref t;

      t = one_reg_loc_descriptor (REGNO (XVECEXP (regs, 0, i)));
      add_loc_descr (&loc_result, t);
      size = GET_MODE_SIZE (GET_MODE (XVECEXP (regs, 0, 0)));
      add_loc_descr_op_piece (&loc_result, size);
    }
  return loc_result;
}

/* Return a location descriptor that designates a constant.  */

static dw_loc_descr_ref
int_loc_descriptor (HOST_WIDE_INT i)
{
  enum dwarf_location_atom op;

  /* Pick the smallest representation of a constant, rather than just
     defaulting to the LEB encoding.  */
  if (i >= 0)
    {
      if (i <= 31)
	op = DW_OP_lit0 + i;
      else if (i <= 0xff)
	op = DW_OP_const1u;
      else if (i <= 0xffff)
	op = DW_OP_const2u;
      else if (HOST_BITS_PER_WIDE_INT == 32
	       || i <= 0xffffffff)
	op = DW_OP_const4u;
      else
	op = DW_OP_constu;
    }
  else
    {
      if (i >= -0x80)
	op = DW_OP_const1s;
      else if (i >= -0x8000)
	op = DW_OP_const2s;
      else if (HOST_BITS_PER_WIDE_INT == 32
	       || i >= -0x80000000)
	op = DW_OP_const4s;
      else
	op = DW_OP_consts;
    }

  return new_loc_descr (op, i, 0);
}

/* Return a location descriptor that designates a base+offset location.  */

static dw_loc_descr_ref
based_loc_descr (rtx reg, HOST_WIDE_INT offset)
{
  unsigned int regno;

  /* We only use "frame base" when we're sure we're talking about the
     post-prologue local stack frame.  We do this by *not* running
     register elimination until this point, and recognizing the special
     argument pointer and soft frame pointer rtx's.  */
  if (reg == arg_pointer_rtx || reg == frame_pointer_rtx)
    {
      rtx elim = eliminate_regs (reg, VOIDmode, NULL_RTX);

      if (elim != reg)
	{
	  if (GET_CODE (elim) == PLUS)
	    {
	      offset += INTVAL (XEXP (elim, 1));
	      elim = XEXP (elim, 0);
	    }
	  gcc_assert (elim == (frame_pointer_needed ? hard_frame_pointer_rtx
		      : stack_pointer_rtx));
          offset += frame_pointer_fb_offset;

          return new_loc_descr (DW_OP_fbreg, offset, 0);
	}
    }

  regno = dbx_reg_number (reg);
  if (regno <= 31)
    return new_loc_descr (DW_OP_breg0 + regno, offset, 0);
  else
    return new_loc_descr (DW_OP_bregx, regno, offset);
}

/* Return true if this RTL expression describes a base+offset calculation.  */

static inline int
is_based_loc (rtx rtl)
{
  return (GET_CODE (rtl) == PLUS
	  && ((REG_P (XEXP (rtl, 0))
	       && REGNO (XEXP (rtl, 0)) < FIRST_PSEUDO_REGISTER
	       && GET_CODE (XEXP (rtl, 1)) == CONST_INT)));
}

/* The following routine converts the RTL for a variable or parameter
   (resident in memory) into an equivalent Dwarf representation of a
   mechanism for getting the address of that same variable onto the top of a
   hypothetical "address evaluation" stack.

   When creating memory location descriptors, we are effectively transforming
   the RTL for a memory-resident object into its Dwarf postfix expression
   equivalent.  This routine recursively descends an RTL tree, turning
   it into Dwarf postfix code as it goes.

   MODE is the mode of the memory reference, needed to handle some
   autoincrement addressing modes.

   CAN_USE_FBREG is a flag whether we can use DW_AT_frame_base in the
   location list for RTL.

   Return 0 if we can't represent the location.  */

static dw_loc_descr_ref
mem_loc_descriptor (rtx rtl, enum machine_mode mode)
{
  dw_loc_descr_ref mem_loc_result = NULL;
  enum dwarf_location_atom op;

  /* Note that for a dynamically sized array, the location we will generate a
     description of here will be the lowest numbered location which is
     actually within the array.  That's *not* necessarily the same as the
     zeroth element of the array.  */

  rtl = targetm.delegitimize_address (rtl);

  switch (GET_CODE (rtl))
    {
    case POST_INC:
    case POST_DEC:
    case POST_MODIFY:
      /* POST_INC and POST_DEC can be handled just like a SUBREG.  So we
	 just fall into the SUBREG code.  */

      /* ... fall through ...  */

    case SUBREG:
      /* The case of a subreg may arise when we have a local (register)
	 variable or a formal (register) parameter which doesn't quite fill
	 up an entire register.  For now, just assume that it is
	 legitimate to make the Dwarf info refer to the whole register which
	 contains the given subreg.  */
      rtl = XEXP (rtl, 0);

      /* ... fall through ...  */

    case REG:
      /* Whenever a register number forms a part of the description of the
	 method for calculating the (dynamic) address of a memory resident
	 object, DWARF rules require the register number be referred to as
	 a "base register".  This distinction is not based in any way upon
	 what category of register the hardware believes the given register
	 belongs to.  This is strictly DWARF terminology we're dealing with
	 here. Note that in cases where the location of a memory-resident
	 data object could be expressed as: OP_ADD (OP_BASEREG (basereg),
	 OP_CONST (0)) the actual DWARF location descriptor that we generate
	 may just be OP_BASEREG (basereg).  This may look deceptively like
	 the object in question was allocated to a register (rather than in
	 memory) so DWARF consumers need to be aware of the subtle
	 distinction between OP_REG and OP_BASEREG.  */
      if (REGNO (rtl) < FIRST_PSEUDO_REGISTER)
	mem_loc_result = based_loc_descr (rtl, 0);
      break;

    case MEM:
      mem_loc_result = mem_loc_descriptor (XEXP (rtl, 0), GET_MODE (rtl));
      if (mem_loc_result != 0)
	add_loc_descr (&mem_loc_result, new_loc_descr (DW_OP_deref, 0, 0));
      break;

    case LO_SUM:
	 rtl = XEXP (rtl, 1);

      /* ... fall through ...  */

    case LABEL_REF:
      /* Some ports can transform a symbol ref into a label ref, because
	 the symbol ref is too far away and has to be dumped into a constant
	 pool.  */
    case CONST:
    case SYMBOL_REF:
      /* Alternatively, the symbol in the constant pool might be referenced
	 by a different symbol.  */
      if (GET_CODE (rtl) == SYMBOL_REF && CONSTANT_POOL_ADDRESS_P (rtl))
	{
	  bool marked;
	  rtx tmp = get_pool_constant_mark (rtl, &marked);

	  if (GET_CODE (tmp) == SYMBOL_REF)
	    {
	      rtl = tmp;
	      if (CONSTANT_POOL_ADDRESS_P (tmp))
		get_pool_constant_mark (tmp, &marked);
	      else
		marked = true;
	    }

	  /* If all references to this pool constant were optimized away,
	     it was not output and thus we can't represent it.
	     FIXME: might try to use DW_OP_const_value here, though
	     DW_OP_piece complicates it.  */
	  if (!marked)
	    return 0;
	}

      mem_loc_result = new_loc_descr (DW_OP_addr, 0, 0);
      mem_loc_result->dw_loc_oprnd1.val_class = dw_val_class_addr;
      mem_loc_result->dw_loc_oprnd1.v.val_addr = rtl;
      VEC_safe_push (rtx, gc, used_rtx_array, rtl);
      break;

    case PRE_MODIFY:
      /* Extract the PLUS expression nested inside and fall into
	 PLUS code below.  */
      rtl = XEXP (rtl, 1);
      goto plus;

    case PRE_INC:
    case PRE_DEC:
      /* Turn these into a PLUS expression and fall into the PLUS code
	 below.  */
      rtl = gen_rtx_PLUS (word_mode, XEXP (rtl, 0),
			  GEN_INT (GET_CODE (rtl) == PRE_INC
				   ? GET_MODE_UNIT_SIZE (mode)
				   : -GET_MODE_UNIT_SIZE (mode)));

      /* ... fall through ...  */

    case PLUS:
    plus:
      if (is_based_loc (rtl))
	mem_loc_result = based_loc_descr (XEXP (rtl, 0),
					  INTVAL (XEXP (rtl, 1)));
      else
	{
	  mem_loc_result = mem_loc_descriptor (XEXP (rtl, 0), mode);
	  if (mem_loc_result == 0)
	    break;

	  if (GET_CODE (XEXP (rtl, 1)) == CONST_INT
	      && INTVAL (XEXP (rtl, 1)) >= 0)
	    add_loc_descr (&mem_loc_result,
			   new_loc_descr (DW_OP_plus_uconst,
					  INTVAL (XEXP (rtl, 1)), 0));
	  else
	    {
	      add_loc_descr (&mem_loc_result,
			     mem_loc_descriptor (XEXP (rtl, 1), mode));
	      add_loc_descr (&mem_loc_result,
			     new_loc_descr (DW_OP_plus, 0, 0));
	    }
	}
      break;

    /* If a pseudo-reg is optimized away, it is possible for it to
       be replaced with a MEM containing a multiply or shift.  */
    case MULT:
      op = DW_OP_mul;
      goto do_binop;

    case ASHIFT:
      op = DW_OP_shl;
      goto do_binop;

    case ASHIFTRT:
      op = DW_OP_shra;
      goto do_binop;

    case LSHIFTRT:
      op = DW_OP_shr;
      goto do_binop;

    do_binop:
      {
	dw_loc_descr_ref op0 = mem_loc_descriptor (XEXP (rtl, 0), mode);
	dw_loc_descr_ref op1 = mem_loc_descriptor (XEXP (rtl, 1), mode);

	if (op0 == 0 || op1 == 0)
	  break;

	mem_loc_result = op0;
	add_loc_descr (&mem_loc_result, op1);
	add_loc_descr (&mem_loc_result, new_loc_descr (op, 0, 0));
	break;
      }

    case CONST_INT:
      mem_loc_result = int_loc_descriptor (INTVAL (rtl));
      break;

    default:
      gcc_unreachable ();
    }

  return mem_loc_result;
}

/* Return a descriptor that describes the concatenation of two locations.
   This is typically a complex variable.  */

static dw_loc_descr_ref
concat_loc_descriptor (rtx x0, rtx x1)
{
  dw_loc_descr_ref cc_loc_result = NULL;
  dw_loc_descr_ref x0_ref = loc_descriptor (x0);
  dw_loc_descr_ref x1_ref = loc_descriptor (x1);

  if (x0_ref == 0 || x1_ref == 0)
    return 0;

  cc_loc_result = x0_ref;
  add_loc_descr_op_piece (&cc_loc_result, GET_MODE_SIZE (GET_MODE (x0)));

  add_loc_descr (&cc_loc_result, x1_ref);
  add_loc_descr_op_piece (&cc_loc_result, GET_MODE_SIZE (GET_MODE (x1)));

  return cc_loc_result;
}

/* Output a proper Dwarf location descriptor for a variable or parameter
   which is either allocated in a register or in a memory location.  For a
   register, we just generate an OP_REG and the register number.  For a
   memory location we provide a Dwarf postfix expression describing how to
   generate the (dynamic) address of the object onto the address stack.

   If we don't know how to describe it, return 0.  */

static dw_loc_descr_ref
loc_descriptor (rtx rtl)
{
  dw_loc_descr_ref loc_result = NULL;

  switch (GET_CODE (rtl))
    {
    case SUBREG:
      /* The case of a subreg may arise when we have a local (register)
	 variable or a formal (register) parameter which doesn't quite fill
	 up an entire register.  For now, just assume that it is
	 legitimate to make the Dwarf info refer to the whole register which
	 contains the given subreg.  */
      rtl = SUBREG_REG (rtl);

      /* ... fall through ...  */

    case REG:
      loc_result = reg_loc_descriptor (rtl);
      break;

    case MEM:
      loc_result = mem_loc_descriptor (XEXP (rtl, 0), GET_MODE (rtl));
      break;

    case CONCAT:
      loc_result = concat_loc_descriptor (XEXP (rtl, 0), XEXP (rtl, 1));
      break;

    case VAR_LOCATION:
      /* Single part.  */
      if (GET_CODE (XEXP (rtl, 1)) != PARALLEL)
	{
	  loc_result = loc_descriptor (XEXP (XEXP (rtl, 1), 0));
	  break;
	}

      rtl = XEXP (rtl, 1);
      /* FALLTHRU */

    case PARALLEL:
      {
	rtvec par_elems = XVEC (rtl, 0);
	int num_elem = GET_NUM_ELEM (par_elems);
	enum machine_mode mode;
	int i;

	/* Create the first one, so we have something to add to.  */
	loc_result = loc_descriptor (XEXP (RTVEC_ELT (par_elems, 0), 0));
	mode = GET_MODE (XEXP (RTVEC_ELT (par_elems, 0), 0));
	add_loc_descr_op_piece (&loc_result, GET_MODE_SIZE (mode));
	for (i = 1; i < num_elem; i++)
	  {
	    dw_loc_descr_ref temp;

	    temp = loc_descriptor (XEXP (RTVEC_ELT (par_elems, i), 0));
	    add_loc_descr (&loc_result, temp);
	    mode = GET_MODE (XEXP (RTVEC_ELT (par_elems, i), 0));
	    add_loc_descr_op_piece (&loc_result, GET_MODE_SIZE (mode));
	  }
      }
      break;

    default:
      gcc_unreachable ();
    }

  return loc_result;
}

/* Similar, but generate the descriptor from trees instead of rtl.  This comes
   up particularly with variable length arrays.  WANT_ADDRESS is 2 if this is
   a top-level invocation of loc_descriptor_from_tree; is 1 if this is not a
   top-level invocation, and we require the address of LOC; is 0 if we require
   the value of LOC.  */

static dw_loc_descr_ref
loc_descriptor_from_tree_1 (tree loc, int want_address)
{
  dw_loc_descr_ref ret, ret1;
  int have_address = 0;
  enum dwarf_location_atom op;

  /* ??? Most of the time we do not take proper care for sign/zero
     extending the values properly.  Hopefully this won't be a real
     problem...  */

  switch (TREE_CODE (loc))
    {
    case ERROR_MARK:
      return 0;

    case PLACEHOLDER_EXPR:
      /* This case involves extracting fields from an object to determine the
	 position of other fields.  We don't try to encode this here.  The
	 only user of this is Ada, which encodes the needed information using
	 the names of types.  */
      return 0;

    case CALL_EXPR:
      return 0;

    case PREINCREMENT_EXPR:
    case PREDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
      /* There are no opcodes for these operations.  */
      return 0;

    case ADDR_EXPR:
      /* If we already want an address, there's nothing we can do.  */
      if (want_address)
	return 0;

      /* Otherwise, process the argument and look for the address.  */
      return loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), 1);

    case VAR_DECL:
      if (DECL_THREAD_LOCAL_P (loc))
	{
	  rtx rtl;

	  /* If this is not defined, we have no way to emit the data.  */
	  if (!targetm.asm_out.output_dwarf_dtprel)
	    return 0;

	  /* The way DW_OP_GNU_push_tls_address is specified, we can only
	     look up addresses of objects in the current module.  */
	  if (DECL_EXTERNAL (loc))
	    return 0;

	  rtl = rtl_for_decl_location (loc);
	  if (rtl == NULL_RTX)
	    return 0;

	  if (!MEM_P (rtl))
	    return 0;
	  rtl = XEXP (rtl, 0);
	  if (! CONSTANT_P (rtl))
	    return 0;

	  ret = new_loc_descr (INTERNAL_DW_OP_tls_addr, 0, 0);
	  ret->dw_loc_oprnd1.val_class = dw_val_class_addr;
	  ret->dw_loc_oprnd1.v.val_addr = rtl;

	  ret1 = new_loc_descr (DW_OP_GNU_push_tls_address, 0, 0);
	  add_loc_descr (&ret, ret1);

	  have_address = 1;
	  break;
	}
      /* FALLTHRU */

    case PARM_DECL:
      if (DECL_HAS_VALUE_EXPR_P (loc))
	return loc_descriptor_from_tree_1 (DECL_VALUE_EXPR (loc),
					   want_address);
      /* FALLTHRU */

    case RESULT_DECL:
    case FUNCTION_DECL:
      {
	rtx rtl = rtl_for_decl_location (loc);

	if (rtl == NULL_RTX)
	  return 0;
        else if (GET_CODE (rtl) == CONST_INT)
	  {
	    HOST_WIDE_INT val = INTVAL (rtl);
	    if (TYPE_UNSIGNED (TREE_TYPE (loc)))
	      val &= GET_MODE_MASK (DECL_MODE (loc));
	    ret = int_loc_descriptor (val);
	  }
	else if (GET_CODE (rtl) == CONST_STRING)
	  return 0;
	else if (CONSTANT_P (rtl))
	  {
	    ret = new_loc_descr (DW_OP_addr, 0, 0);
	    ret->dw_loc_oprnd1.val_class = dw_val_class_addr;
	    ret->dw_loc_oprnd1.v.val_addr = rtl;
	  }
	else
	  {
	    enum machine_mode mode;

	    /* Certain constructs can only be represented at top-level.  */
	    if (want_address == 2)
	      return loc_descriptor (rtl);

	    mode = GET_MODE (rtl);
	    if (MEM_P (rtl))
	      {
		rtl = XEXP (rtl, 0);
		have_address = 1;
	      }
	    ret = mem_loc_descriptor (rtl, mode);
	  }
      }
      break;

    case INDIRECT_REF:
      ret = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), 0);
      have_address = 1;
      break;

    case COMPOUND_EXPR:
      return loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 1), want_address);

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
    case VIEW_CONVERT_EXPR:
    case SAVE_EXPR:
    case MODIFY_EXPR:
      return loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), want_address);

    case COMPONENT_REF:
    case BIT_FIELD_REF:
    case ARRAY_REF:
    case ARRAY_RANGE_REF:
      {
	tree obj, offset;
	HOST_WIDE_INT bitsize, bitpos, bytepos;
	enum machine_mode mode;
	int volatilep;
	int unsignedp = TYPE_UNSIGNED (TREE_TYPE (loc));

	obj = get_inner_reference (loc, &bitsize, &bitpos, &offset, &mode,
				   &unsignedp, &volatilep, false);

	if (obj == loc)
	  return 0;

	ret = loc_descriptor_from_tree_1 (obj, 1);
	if (ret == 0
	    || bitpos % BITS_PER_UNIT != 0 || bitsize % BITS_PER_UNIT != 0)
	  return 0;

	if (offset != NULL_TREE)
	  {
	    /* Variable offset.  */
	    add_loc_descr (&ret, loc_descriptor_from_tree_1 (offset, 0));
	    add_loc_descr (&ret, new_loc_descr (DW_OP_plus, 0, 0));
	  }

	bytepos = bitpos / BITS_PER_UNIT;
	if (bytepos > 0)
	  add_loc_descr (&ret, new_loc_descr (DW_OP_plus_uconst, bytepos, 0));
	else if (bytepos < 0)
	  {
	    add_loc_descr (&ret, int_loc_descriptor (bytepos));
	    add_loc_descr (&ret, new_loc_descr (DW_OP_plus, 0, 0));
	  }

	have_address = 1;
	break;
      }

    case INTEGER_CST:
      if (host_integerp (loc, 0))
	ret = int_loc_descriptor (tree_low_cst (loc, 0));
      else
	return 0;
      break;

    case CONSTRUCTOR:
      {
	/* Get an RTL for this, if something has been emitted.  */
	rtx rtl = lookup_constant_def (loc);
	enum machine_mode mode;

	if (!rtl || !MEM_P (rtl))
	  return 0;
	mode = GET_MODE (rtl);
	rtl = XEXP (rtl, 0);
	ret = mem_loc_descriptor (rtl, mode);
	have_address = 1;
	break;
      }

    case TRUTH_AND_EXPR:
    case TRUTH_ANDIF_EXPR:
    case BIT_AND_EXPR:
      op = DW_OP_and;
      goto do_binop;

    case TRUTH_XOR_EXPR:
    case BIT_XOR_EXPR:
      op = DW_OP_xor;
      goto do_binop;

    case TRUTH_OR_EXPR:
    case TRUTH_ORIF_EXPR:
    case BIT_IOR_EXPR:
      op = DW_OP_or;
      goto do_binop;

    case FLOOR_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case TRUNC_DIV_EXPR:
      op = DW_OP_div;
      goto do_binop;

    case MINUS_EXPR:
      op = DW_OP_minus;
      goto do_binop;

    case FLOOR_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case TRUNC_MOD_EXPR:
      op = DW_OP_mod;
      goto do_binop;

    case MULT_EXPR:
      op = DW_OP_mul;
      goto do_binop;

    case LSHIFT_EXPR:
      op = DW_OP_shl;
      goto do_binop;

    case RSHIFT_EXPR:
      op = (TYPE_UNSIGNED (TREE_TYPE (loc)) ? DW_OP_shr : DW_OP_shra);
      goto do_binop;

    case PLUS_EXPR:
      if (TREE_CODE (TREE_OPERAND (loc, 1)) == INTEGER_CST
	  && host_integerp (TREE_OPERAND (loc, 1), 0))
	{
	  ret = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), 0);
	  if (ret == 0)
	    return 0;

	  add_loc_descr (&ret,
			 new_loc_descr (DW_OP_plus_uconst,
					tree_low_cst (TREE_OPERAND (loc, 1),
						      0),
					0));
	  break;
	}

      op = DW_OP_plus;
      goto do_binop;

    case LE_EXPR:
      if (TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (loc, 0))))
	return 0;

      op = DW_OP_le;
      goto do_binop;

    case GE_EXPR:
      if (TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (loc, 0))))
	return 0;

      op = DW_OP_ge;
      goto do_binop;

    case LT_EXPR:
      if (TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (loc, 0))))
	return 0;

      op = DW_OP_lt;
      goto do_binop;

    case GT_EXPR:
      if (TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (loc, 0))))
	return 0;

      op = DW_OP_gt;
      goto do_binop;

    case EQ_EXPR:
      op = DW_OP_eq;
      goto do_binop;

    case NE_EXPR:
      op = DW_OP_ne;
      goto do_binop;

    do_binop:
      ret = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), 0);
      ret1 = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 1), 0);
      if (ret == 0 || ret1 == 0)
	return 0;

      add_loc_descr (&ret, ret1);
      add_loc_descr (&ret, new_loc_descr (op, 0, 0));
      break;

    case TRUTH_NOT_EXPR:
    case BIT_NOT_EXPR:
      op = DW_OP_not;
      goto do_unop;

    case ABS_EXPR:
      op = DW_OP_abs;
      goto do_unop;

    case NEGATE_EXPR:
      op = DW_OP_neg;
      goto do_unop;

    do_unop:
      ret = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), 0);
      if (ret == 0)
	return 0;

      add_loc_descr (&ret, new_loc_descr (op, 0, 0));
      break;

    case MIN_EXPR:
    case MAX_EXPR:
      {
        const enum tree_code code =
          TREE_CODE (loc) == MIN_EXPR ? GT_EXPR : LT_EXPR;

        loc = build3 (COND_EXPR, TREE_TYPE (loc),
		      build2 (code, integer_type_node,
			      TREE_OPERAND (loc, 0), TREE_OPERAND (loc, 1)),
                      TREE_OPERAND (loc, 1), TREE_OPERAND (loc, 0));
      }

      /* ... fall through ...  */

    case COND_EXPR:
      {
	dw_loc_descr_ref lhs
	  = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 1), 0);
	dw_loc_descr_ref rhs
	  = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 2), 0);
	dw_loc_descr_ref bra_node, jump_node, tmp;

	ret = loc_descriptor_from_tree_1 (TREE_OPERAND (loc, 0), 0);
	if (ret == 0 || lhs == 0 || rhs == 0)
	  return 0;

	bra_node = new_loc_descr (DW_OP_bra, 0, 0);
	add_loc_descr (&ret, bra_node);

	add_loc_descr (&ret, rhs);
	jump_node = new_loc_descr (DW_OP_skip, 0, 0);
	add_loc_descr (&ret, jump_node);

	add_loc_descr (&ret, lhs);
	bra_node->dw_loc_oprnd1.val_class = dw_val_class_loc;
	bra_node->dw_loc_oprnd1.v.val_loc = lhs;

	/* ??? Need a node to point the skip at.  Use a nop.  */
	tmp = new_loc_descr (DW_OP_nop, 0, 0);
	add_loc_descr (&ret, tmp);
	jump_node->dw_loc_oprnd1.val_class = dw_val_class_loc;
	jump_node->dw_loc_oprnd1.v.val_loc = tmp;
      }
      break;

    case FIX_TRUNC_EXPR:
    case FIX_CEIL_EXPR:
    case FIX_FLOOR_EXPR:
    case FIX_ROUND_EXPR:
      return 0;

    default:
      /* Leave front-end specific codes as simply unknown.  This comes
	 up, for instance, with the C STMT_EXPR.  */
      if ((unsigned int) TREE_CODE (loc)
          >= (unsigned int) LAST_AND_UNUSED_TREE_CODE)
	return 0;

#ifdef ENABLE_CHECKING
      /* Otherwise this is a generic code; we should just lists all of
	 these explicitly.  We forgot one.  */
      gcc_unreachable ();
#else
      /* In a release build, we want to degrade gracefully: better to
	 generate incomplete debugging information than to crash.  */
      return NULL;
#endif
    }

  /* Show if we can't fill the request for an address.  */
  if (want_address && !have_address)
    return 0;

  /* If we've got an address and don't want one, dereference.  */
  if (!want_address && have_address && ret)
    {
      HOST_WIDE_INT size = int_size_in_bytes (TREE_TYPE (loc));

      if (size > DWARF2_ADDR_SIZE || size == -1)
	return 0;
      else if (size == DWARF2_ADDR_SIZE)
	op = DW_OP_deref;
      else
	op = DW_OP_deref_size;

      add_loc_descr (&ret, new_loc_descr (op, size, 0));
    }

  return ret;
}

static inline dw_loc_descr_ref
loc_descriptor_from_tree (tree loc)
{
  return loc_descriptor_from_tree_1 (loc, 2);
}

/* Given a value, round it up to the lowest multiple of `boundary'
   which is not less than the value itself.  */

static inline HOST_WIDE_INT
ceiling (HOST_WIDE_INT value, unsigned int boundary)
{
  return (((value + boundary - 1) / boundary) * boundary);
}

/* Given a pointer to what is assumed to be a FIELD_DECL node, return a
   pointer to the declared type for the relevant field variable, or return
   `integer_type_node' if the given node turns out to be an
   ERROR_MARK node.  */

static inline tree
field_type (tree decl)
{
  tree type;

  if (TREE_CODE (decl) == ERROR_MARK)
    return integer_type_node;

  type = DECL_BIT_FIELD_TYPE (decl);
  if (type == NULL_TREE)
    type = TREE_TYPE (decl);

  return type;
}

/* Given a pointer to a tree node, return the alignment in bits for
   it, or else return BITS_PER_WORD if the node actually turns out to
   be an ERROR_MARK node.  */

static inline unsigned
simple_type_align_in_bits (tree type)
{
  return (TREE_CODE (type) != ERROR_MARK) ? TYPE_ALIGN (type) : BITS_PER_WORD;
}

static inline unsigned
simple_decl_align_in_bits (tree decl)
{
  return (TREE_CODE (decl) != ERROR_MARK) ? DECL_ALIGN (decl) : BITS_PER_WORD;
}

/* Given a pointer to a FIELD_DECL, compute and return the byte offset of the
   lowest addressed byte of the "containing object" for the given FIELD_DECL,
   or return 0 if we are unable to determine what that offset is, either
   because the argument turns out to be a pointer to an ERROR_MARK node, or
   because the offset is actually variable.  (We can't handle the latter case
   just yet).  */

static HOST_WIDE_INT
field_byte_offset (tree decl)
{
  unsigned int type_align_in_bits;
  unsigned int decl_align_in_bits;
  unsigned HOST_WIDE_INT type_size_in_bits;
  HOST_WIDE_INT object_offset_in_bits;
  tree type;
  tree field_size_tree;
  HOST_WIDE_INT bitpos_int;
  HOST_WIDE_INT deepest_bitpos;
  unsigned HOST_WIDE_INT field_size_in_bits;

  if (TREE_CODE (decl) == ERROR_MARK)
    return 0;

  gcc_assert (TREE_CODE (decl) == FIELD_DECL);

  type = field_type (decl);
  field_size_tree = DECL_SIZE (decl);

  /* The size could be unspecified if there was an error, or for
     a flexible array member.  */
  if (! field_size_tree)
    field_size_tree = bitsize_zero_node;

  /* We cannot yet cope with fields whose positions are variable, so
     for now, when we see such things, we simply return 0.  Someday, we may
     be able to handle such cases, but it will be damn difficult.  */
  if (! host_integerp (bit_position (decl), 0))
    return 0;

  bitpos_int = int_bit_position (decl);

  /* If we don't know the size of the field, pretend it's a full word.  */
  if (host_integerp (field_size_tree, 1))
    field_size_in_bits = tree_low_cst (field_size_tree, 1);
  else
    field_size_in_bits = BITS_PER_WORD;

  type_size_in_bits = simple_type_size_in_bits (type);
  type_align_in_bits = simple_type_align_in_bits (type);
  decl_align_in_bits = simple_decl_align_in_bits (decl);

  /* The GCC front-end doesn't make any attempt to keep track of the starting
     bit offset (relative to the start of the containing structure type) of the
     hypothetical "containing object" for a bit-field.  Thus, when computing
     the byte offset value for the start of the "containing object" of a
     bit-field, we must deduce this information on our own. This can be rather
     tricky to do in some cases.  For example, handling the following structure
     type definition when compiling for an i386/i486 target (which only aligns
     long long's to 32-bit boundaries) can be very tricky:

	 struct S { int field1; long long field2:31; };

     Fortunately, there is a simple rule-of-thumb which can be used in such
     cases.  When compiling for an i386/i486, GCC will allocate 8 bytes for the
     structure shown above.  It decides to do this based upon one simple rule
     for bit-field allocation.  GCC allocates each "containing object" for each
     bit-field at the first (i.e. lowest addressed) legitimate alignment
     boundary (based upon the required minimum alignment for the declared type
     of the field) which it can possibly use, subject to the condition that
     there is still enough available space remaining in the containing object
     (when allocated at the selected point) to fully accommodate all of the
     bits of the bit-field itself.

     This simple rule makes it obvious why GCC allocates 8 bytes for each
     object of the structure type shown above.  When looking for a place to
     allocate the "containing object" for `field2', the compiler simply tries
     to allocate a 64-bit "containing object" at each successive 32-bit
     boundary (starting at zero) until it finds a place to allocate that 64-
     bit field such that at least 31 contiguous (and previously unallocated)
     bits remain within that selected 64 bit field.  (As it turns out, for the
     example above, the compiler finds it is OK to allocate the "containing
     object" 64-bit field at bit-offset zero within the structure type.)

     Here we attempt to work backwards from the limited set of facts we're
     given, and we try to deduce from those facts, where GCC must have believed
     that the containing object started (within the structure type). The value
     we deduce is then used (by the callers of this routine) to generate
     DW_AT_location and DW_AT_bit_offset attributes for fields (both bit-fields
     and, in the case of DW_AT_location, regular fields as well).  */

  /* Figure out the bit-distance from the start of the structure to the
     "deepest" bit of the bit-field.  */
  deepest_bitpos = bitpos_int + field_size_in_bits;

  /* This is the tricky part.  Use some fancy footwork to deduce where the
     lowest addressed bit of the containing object must be.  */
  object_offset_in_bits = deepest_bitpos - type_size_in_bits;

  /* Round up to type_align by default.  This works best for bitfields.  */
  object_offset_in_bits += type_align_in_bits - 1;
  object_offset_in_bits /= type_align_in_bits;
  object_offset_in_bits *= type_align_in_bits;

  if (object_offset_in_bits > bitpos_int)
    {
      /* Sigh, the decl must be packed.  */
      object_offset_in_bits = deepest_bitpos - type_size_in_bits;

      /* Round up to decl_align instead.  */
      object_offset_in_bits += decl_align_in_bits - 1;
      object_offset_in_bits /= decl_align_in_bits;
      object_offset_in_bits *= decl_align_in_bits;
    }

  return object_offset_in_bits / BITS_PER_UNIT;
}

/* The following routines define various Dwarf attributes and any data
   associated with them.  */

/* Add a location description attribute value to a DIE.

   This emits location attributes suitable for whole variables and
   whole parameters.  Note that the location attributes for struct fields are
   generated by the routine `data_member_location_attribute' below.  */

static inline void
add_AT_location_description (dw_die_ref die, enum dwarf_attribute attr_kind,
			     dw_loc_descr_ref descr)
{
  if (descr != 0)
    add_AT_loc (die, attr_kind, descr);
}

/* Attach the specialized form of location attribute used for data members of
   struct and union types.  In the special case of a FIELD_DECL node which
   represents a bit-field, the "offset" part of this special location
   descriptor must indicate the distance in bytes from the lowest-addressed
   byte of the containing struct or union type to the lowest-addressed byte of
   the "containing object" for the bit-field.  (See the `field_byte_offset'
   function above).

   For any given bit-field, the "containing object" is a hypothetical object
   (of some integral or enum type) within which the given bit-field lives.  The
   type of this hypothetical "containing object" is always the same as the
   declared type of the individual bit-field itself (for GCC anyway... the
   DWARF spec doesn't actually mandate this).  Note that it is the size (in
   bytes) of the hypothetical "containing object" which will be given in the
   DW_AT_byte_size attribute for this bit-field.  (See the
   `byte_size_attribute' function below.)  It is also used when calculating the
   value of the DW_AT_bit_offset attribute.  (See the `bit_offset_attribute'
   function below.)  */

static void
add_data_member_location_attribute (dw_die_ref die, tree decl)
{
  HOST_WIDE_INT offset;
  dw_loc_descr_ref loc_descr = 0;

  if (TREE_CODE (decl) == TREE_BINFO)
    {
      /* We're working on the TAG_inheritance for a base class.  */
      if (BINFO_VIRTUAL_P (decl) && is_cxx ())
	{
	  /* For C++ virtual bases we can't just use BINFO_OFFSET, as they
	     aren't at a fixed offset from all (sub)objects of the same
	     type.  We need to extract the appropriate offset from our
	     vtable.  The following dwarf expression means

	       BaseAddr = ObAddr + *((*ObAddr) - Offset)

	     This is specific to the V3 ABI, of course.  */

	  dw_loc_descr_ref tmp;

	  /* Make a copy of the object address.  */
	  tmp = new_loc_descr (DW_OP_dup, 0, 0);
	  add_loc_descr (&loc_descr, tmp);

	  /* Extract the vtable address.  */
	  tmp = new_loc_descr (DW_OP_deref, 0, 0);
	  add_loc_descr (&loc_descr, tmp);

	  /* Calculate the address of the offset.  */
	  offset = tree_low_cst (BINFO_VPTR_FIELD (decl), 0);
	  gcc_assert (offset < 0);

	  tmp = int_loc_descriptor (-offset);
	  add_loc_descr (&loc_descr, tmp);
	  tmp = new_loc_descr (DW_OP_minus, 0, 0);
	  add_loc_descr (&loc_descr, tmp);

	  /* Extract the offset.  */
	  tmp = new_loc_descr (DW_OP_deref, 0, 0);
	  add_loc_descr (&loc_descr, tmp);

	  /* Add it to the object address.  */
	  tmp = new_loc_descr (DW_OP_plus, 0, 0);
	  add_loc_descr (&loc_descr, tmp);
	}
      else
	offset = tree_low_cst (BINFO_OFFSET (decl), 0);
    }
  else
    offset = field_byte_offset (decl);

  if (! loc_descr)
    {
      enum dwarf_location_atom op;

      /* The DWARF2 standard says that we should assume that the structure
	 address is already on the stack, so we can specify a structure field
	 address by using DW_OP_plus_uconst.  */

#ifdef MIPS_DEBUGGING_INFO
      /* ??? The SGI dwarf reader does not handle the DW_OP_plus_uconst
	 operator correctly.  It works only if we leave the offset on the
	 stack.  */
      op = DW_OP_constu;
#else
      op = DW_OP_plus_uconst;
#endif

      loc_descr = new_loc_descr (op, offset, 0);
    }

  add_AT_loc (die, DW_AT_data_member_location, loc_descr);
}

/* Writes integer values to dw_vec_const array.  */

static void
insert_int (HOST_WIDE_INT val, unsigned int size, unsigned char *dest)
{
  while (size != 0)
    {
      *dest++ = val & 0xff;
      val >>= 8;
      --size;
    }
}

/* Reads integers from dw_vec_const array.  Inverse of insert_int.  */

static HOST_WIDE_INT
extract_int (const unsigned char *src, unsigned int size)
{
  HOST_WIDE_INT val = 0;

  src += size;
  while (size != 0)
    {
      val <<= 8;
      val |= *--src & 0xff;
      --size;
    }
  return val;
}

/* Writes floating point values to dw_vec_const array.  */

static void
insert_float (rtx rtl, unsigned char *array)
{
  REAL_VALUE_TYPE rv;
  long val[4];
  int i;

  REAL_VALUE_FROM_CONST_DOUBLE (rv, rtl);
  real_to_target (val, &rv, GET_MODE (rtl));

  /* real_to_target puts 32-bit pieces in each long.  Pack them.  */
  for (i = 0; i < GET_MODE_SIZE (GET_MODE (rtl)) / 4; i++)
    {
      insert_int (val[i], 4, array);
      array += 4;
    }
}

/* Attach a DW_AT_const_value attribute for a variable or a parameter which
   does not have a "location" either in memory or in a register.  These
   things can arise in GNU C when a constant is passed as an actual parameter
   to an inlined function.  They can also arise in C++ where declared
   constants do not necessarily get memory "homes".  */

static void
add_const_value_attribute (dw_die_ref die, rtx rtl)
{
  switch (GET_CODE (rtl))
    {
    case CONST_INT:
      {
	HOST_WIDE_INT val = INTVAL (rtl);

	if (val < 0)
	  add_AT_int (die, DW_AT_const_value, val);
	else
	  add_AT_unsigned (die, DW_AT_const_value, (unsigned HOST_WIDE_INT) val);
      }
      break;

    case CONST_DOUBLE:
      /* Note that a CONST_DOUBLE rtx could represent either an integer or a
	 floating-point constant.  A CONST_DOUBLE is used whenever the
	 constant requires more than one word in order to be adequately
	 represented.  We output CONST_DOUBLEs as blocks.  */
      {
	enum machine_mode mode = GET_MODE (rtl);

	if (SCALAR_FLOAT_MODE_P (mode))
	  {
	    unsigned int length = GET_MODE_SIZE (mode);
	    unsigned char *array = ggc_alloc (length);

	    insert_float (rtl, array);
	    add_AT_vec (die, DW_AT_const_value, length / 4, 4, array);
	  }
	else
	  {
	    /* ??? We really should be using HOST_WIDE_INT throughout.  */
	    gcc_assert (HOST_BITS_PER_LONG == HOST_BITS_PER_WIDE_INT);

	    add_AT_long_long (die, DW_AT_const_value,
			      CONST_DOUBLE_HIGH (rtl), CONST_DOUBLE_LOW (rtl));
	  }
      }
      break;

    case CONST_VECTOR:
      {
	enum machine_mode mode = GET_MODE (rtl);
	unsigned int elt_size = GET_MODE_UNIT_SIZE (mode);
	unsigned int length = CONST_VECTOR_NUNITS (rtl);
	unsigned char *array = ggc_alloc (length * elt_size);
	unsigned int i;
	unsigned char *p;

	switch (GET_MODE_CLASS (mode))
	  {
	  case MODE_VECTOR_INT:
	    for (i = 0, p = array; i < length; i++, p += elt_size)
	      {
		rtx elt = CONST_VECTOR_ELT (rtl, i);
		HOST_WIDE_INT lo, hi;

		switch (GET_CODE (elt))
		  {
		  case CONST_INT:
		    lo = INTVAL (elt);
		    hi = -(lo < 0);
		    break;

		  case CONST_DOUBLE:
		    lo = CONST_DOUBLE_LOW (elt);
		    hi = CONST_DOUBLE_HIGH (elt);
		    break;

		  default:
		    gcc_unreachable ();
		  }

		if (elt_size <= sizeof (HOST_WIDE_INT))
		  insert_int (lo, elt_size, p);
		else
		  {
		    unsigned char *p0 = p;
		    unsigned char *p1 = p + sizeof (HOST_WIDE_INT);

		    gcc_assert (elt_size == 2 * sizeof (HOST_WIDE_INT));
		    if (WORDS_BIG_ENDIAN)
		      {
			p0 = p1;
			p1 = p;
		      }
		    insert_int (lo, sizeof (HOST_WIDE_INT), p0);
		    insert_int (hi, sizeof (HOST_WIDE_INT), p1);
		  }
	      }
	    break;

	  case MODE_VECTOR_FLOAT:
	    for (i = 0, p = array; i < length; i++, p += elt_size)
	      {
		rtx elt = CONST_VECTOR_ELT (rtl, i);
		insert_float (elt, p);
	      }
	    break;

	  default:
	    gcc_unreachable ();
	  }

	add_AT_vec (die, DW_AT_const_value, length, elt_size, array);
      }
      break;

    case CONST_STRING:
      add_AT_string (die, DW_AT_const_value, XSTR (rtl, 0));
      break;

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
      add_AT_addr (die, DW_AT_const_value, rtl);
      VEC_safe_push (rtx, gc, used_rtx_array, rtl);
      break;

    case PLUS:
      /* In cases where an inlined instance of an inline function is passed
	 the address of an `auto' variable (which is local to the caller) we
	 can get a situation where the DECL_RTL of the artificial local
	 variable (for the inlining) which acts as a stand-in for the
	 corresponding formal parameter (of the inline function) will look
	 like (plus:SI (reg:SI FRAME_PTR) (const_int ...)).  This is not
	 exactly a compile-time constant expression, but it isn't the address
	 of the (artificial) local variable either.  Rather, it represents the
	 *value* which the artificial local variable always has during its
	 lifetime.  We currently have no way to represent such quasi-constant
	 values in Dwarf, so for now we just punt and generate nothing.  */
      break;

    default:
      /* No other kinds of rtx should be possible here.  */
      gcc_unreachable ();
    }

}

/* Determine whether the evaluation of EXPR references any variables
   or functions which aren't otherwise used (and therefore may not be
   output).  */
static tree
reference_to_unused (tree * tp, int * walk_subtrees,
		     void * data ATTRIBUTE_UNUSED)
{
  if (! EXPR_P (*tp) && ! CONSTANT_CLASS_P (*tp))
    *walk_subtrees = 0;
  
  if (DECL_P (*tp) && ! TREE_PUBLIC (*tp) && ! TREE_USED (*tp)
      && ! TREE_ASM_WRITTEN (*tp))
    return *tp;
  else if (!flag_unit_at_a_time)
    return NULL_TREE;
  else if (!cgraph_global_info_ready
	   && (TREE_CODE (*tp) == VAR_DECL || TREE_CODE (*tp) == FUNCTION_DECL))
    gcc_unreachable ();
  else if (DECL_P (*tp) && TREE_CODE (*tp) == VAR_DECL)
    {
      struct cgraph_varpool_node *node = cgraph_varpool_node (*tp);
      if (!node->needed)
	return *tp;
    }
   else if (DECL_P (*tp) && TREE_CODE (*tp) == FUNCTION_DECL
	    && (!DECL_EXTERNAL (*tp) || DECL_DECLARED_INLINE_P (*tp)))
    {
      struct cgraph_node *node = cgraph_node (*tp);
      if (!node->output)
        return *tp;
    }

  return NULL_TREE;
}

/* Generate an RTL constant from a decl initializer INIT with decl type TYPE,
   for use in a later add_const_value_attribute call.  */

static rtx
rtl_for_decl_init (tree init, tree type)
{
  rtx rtl = NULL_RTX;

  /* If a variable is initialized with a string constant without embedded
     zeros, build CONST_STRING.  */
  if (TREE_CODE (init) == STRING_CST && TREE_CODE (type) == ARRAY_TYPE)
    {
      tree enttype = TREE_TYPE (type);
      tree domain = TYPE_DOMAIN (type);
      enum machine_mode mode = TYPE_MODE (enttype);

      if (GET_MODE_CLASS (mode) == MODE_INT && GET_MODE_SIZE (mode) == 1
	  && domain
	  && integer_zerop (TYPE_MIN_VALUE (domain))
	  && compare_tree_int (TYPE_MAX_VALUE (domain),
			       TREE_STRING_LENGTH (init) - 1) == 0
	  && ((size_t) TREE_STRING_LENGTH (init)
	      == strlen (TREE_STRING_POINTER (init)) + 1))
	rtl = gen_rtx_CONST_STRING (VOIDmode,
				    ggc_strdup (TREE_STRING_POINTER (init)));
    }
  /* Other aggregates, and complex values, could be represented using
     CONCAT: FIXME!  */
  else if (AGGREGATE_TYPE_P (type) || TREE_CODE (type) == COMPLEX_TYPE)
    ;
  /* Vectors only work if their mode is supported by the target.  
     FIXME: generic vectors ought to work too.  */
  else if (TREE_CODE (type) == VECTOR_TYPE && TYPE_MODE (type) == BLKmode)
    ;
  /* If the initializer is something that we know will expand into an
     immediate RTL constant, expand it now.  We must be careful not to
     reference variables which won't be output.  */
  else if (initializer_constant_valid_p (init, type)
	   && ! walk_tree (&init, reference_to_unused, NULL, NULL))
    {
      rtl = expand_expr (init, NULL_RTX, VOIDmode, EXPAND_INITIALIZER);

      /* If expand_expr returns a MEM, it wasn't immediate.  */
      gcc_assert (!rtl || !MEM_P (rtl));
    }

  return rtl;
}

/* Generate RTL for the variable DECL to represent its location.  */

static rtx
rtl_for_decl_location (tree decl)
{
  rtx rtl;

  /* Here we have to decide where we are going to say the parameter "lives"
     (as far as the debugger is concerned).  We only have a couple of
     choices.  GCC provides us with DECL_RTL and with DECL_INCOMING_RTL.

     DECL_RTL normally indicates where the parameter lives during most of the
     activation of the function.  If optimization is enabled however, this
     could be either NULL or else a pseudo-reg.  Both of those cases indicate
     that the parameter doesn't really live anywhere (as far as the code
     generation parts of GCC are concerned) during most of the function's
     activation.  That will happen (for example) if the parameter is never
     referenced within the function.

     We could just generate a location descriptor here for all non-NULL
     non-pseudo values of DECL_RTL and ignore all of the rest, but we can be
     a little nicer than that if we also consider DECL_INCOMING_RTL in cases
     where DECL_RTL is NULL or is a pseudo-reg.

     Note however that we can only get away with using DECL_INCOMING_RTL as
     a backup substitute for DECL_RTL in certain limited cases.  In cases
     where DECL_ARG_TYPE (decl) indicates the same type as TREE_TYPE (decl),
     we can be sure that the parameter was passed using the same type as it is
     declared to have within the function, and that its DECL_INCOMING_RTL
     points us to a place where a value of that type is passed.

     In cases where DECL_ARG_TYPE (decl) and TREE_TYPE (decl) are different,
     we cannot (in general) use DECL_INCOMING_RTL as a substitute for DECL_RTL
     because in these cases DECL_INCOMING_RTL points us to a value of some
     type which is *different* from the type of the parameter itself.  Thus,
     if we tried to use DECL_INCOMING_RTL to generate a location attribute in
     such cases, the debugger would end up (for example) trying to fetch a
     `float' from a place which actually contains the first part of a
     `double'.  That would lead to really incorrect and confusing
     output at debug-time.

     So, in general, we *do not* use DECL_INCOMING_RTL as a backup for DECL_RTL
     in cases where DECL_ARG_TYPE (decl) != TREE_TYPE (decl).  There
     are a couple of exceptions however.  On little-endian machines we can
     get away with using DECL_INCOMING_RTL even when DECL_ARG_TYPE (decl) is
     not the same as TREE_TYPE (decl), but only when DECL_ARG_TYPE (decl) is
     an integral type that is smaller than TREE_TYPE (decl). These cases arise
     when (on a little-endian machine) a non-prototyped function has a
     parameter declared to be of type `short' or `char'.  In such cases,
     TREE_TYPE (decl) will be `short' or `char', DECL_ARG_TYPE (decl) will
     be `int', and DECL_INCOMING_RTL will point to the lowest-order byte of the
     passed `int' value.  If the debugger then uses that address to fetch
     a `short' or a `char' (on a little-endian machine) the result will be
     the correct data, so we allow for such exceptional cases below.

     Note that our goal here is to describe the place where the given formal
     parameter lives during most of the function's activation (i.e. between the
     end of the prologue and the start of the epilogue).  We'll do that as best
     as we can. Note however that if the given formal parameter is modified
     sometime during the execution of the function, then a stack backtrace (at
     debug-time) will show the function as having been called with the *new*
     value rather than the value which was originally passed in.  This happens
     rarely enough that it is not a major problem, but it *is* a problem, and
     I'd like to fix it.

     A future version of dwarf2out.c may generate two additional attributes for
     any given DW_TAG_formal_parameter DIE which will describe the "passed
     type" and the "passed location" for the given formal parameter in addition
     to the attributes we now generate to indicate the "declared type" and the
     "active location" for each parameter.  This additional set of attributes
     could be used by debuggers for stack backtraces. Separately, note that
     sometimes DECL_RTL can be NULL and DECL_INCOMING_RTL can be NULL also.
     This happens (for example) for inlined-instances of inline function formal
     parameters which are never referenced.  This really shouldn't be
     happening.  All PARM_DECL nodes should get valid non-NULL
     DECL_INCOMING_RTL values.  FIXME.  */

  /* Use DECL_RTL as the "location" unless we find something better.  */
  rtl = DECL_RTL_IF_SET (decl);

  /* When generating abstract instances, ignore everything except
     constants, symbols living in memory, and symbols living in
     fixed registers.  */
  if (! reload_completed)
    {
      if (rtl
	  && (CONSTANT_P (rtl)
	      || (MEM_P (rtl)
	          && CONSTANT_P (XEXP (rtl, 0)))
	      || (REG_P (rtl)
	          && TREE_CODE (decl) == VAR_DECL
		  && TREE_STATIC (decl))))
	{
	  rtl = targetm.delegitimize_address (rtl);
	  return rtl;
	}
      rtl = NULL_RTX;
    }
  else if (TREE_CODE (decl) == PARM_DECL)
    {
      if (rtl == NULL_RTX || is_pseudo_reg (rtl))
	{
	  tree declared_type = TREE_TYPE (decl);
	  tree passed_type = DECL_ARG_TYPE (decl);
	  enum machine_mode dmode = TYPE_MODE (declared_type);
	  enum machine_mode pmode = TYPE_MODE (passed_type);

	  /* This decl represents a formal parameter which was optimized out.
	     Note that DECL_INCOMING_RTL may be NULL in here, but we handle
	     all cases where (rtl == NULL_RTX) just below.  */
	  if (dmode == pmode)
	    rtl = DECL_INCOMING_RTL (decl);
	  else if (SCALAR_INT_MODE_P (dmode)
		   && GET_MODE_SIZE (dmode) <= GET_MODE_SIZE (pmode)
		   && DECL_INCOMING_RTL (decl))
	    {
	      rtx inc = DECL_INCOMING_RTL (decl);
	      if (REG_P (inc))
		rtl = inc;
	      else if (MEM_P (inc))
		{
		  if (BYTES_BIG_ENDIAN)
		    rtl = adjust_address_nv (inc, dmode,
					     GET_MODE_SIZE (pmode)
					     - GET_MODE_SIZE (dmode));
		  else
		    rtl = inc;
		}
	    }
	}

      /* If the parm was passed in registers, but lives on the stack, then
	 make a big endian correction if the mode of the type of the
	 parameter is not the same as the mode of the rtl.  */
      /* ??? This is the same series of checks that are made in dbxout.c before
	 we reach the big endian correction code there.  It isn't clear if all
	 of these checks are necessary here, but keeping them all is the safe
	 thing to do.  */
      else if (MEM_P (rtl)
	       && XEXP (rtl, 0) != const0_rtx
	       && ! CONSTANT_P (XEXP (rtl, 0))
	       /* Not passed in memory.  */
	       && !MEM_P (DECL_INCOMING_RTL (decl))
	       /* Not passed by invisible reference.  */
	       && (!REG_P (XEXP (rtl, 0))
		   || REGNO (XEXP (rtl, 0)) == HARD_FRAME_POINTER_REGNUM
		   || REGNO (XEXP (rtl, 0)) == STACK_POINTER_REGNUM
#if ARG_POINTER_REGNUM != HARD_FRAME_POINTER_REGNUM
		   || REGNO (XEXP (rtl, 0)) == ARG_POINTER_REGNUM
#endif
		     )
	       /* Big endian correction check.  */
	       && BYTES_BIG_ENDIAN
	       && TYPE_MODE (TREE_TYPE (decl)) != GET_MODE (rtl)
	       && (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (decl)))
		   < UNITS_PER_WORD))
	{
	  int offset = (UNITS_PER_WORD
			- GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (decl))));

	  rtl = gen_rtx_MEM (TYPE_MODE (TREE_TYPE (decl)),
			     plus_constant (XEXP (rtl, 0), offset));
	}
    }
  else if (TREE_CODE (decl) == VAR_DECL
	   && rtl
	   && MEM_P (rtl)
	   && GET_MODE (rtl) != TYPE_MODE (TREE_TYPE (decl))
	   && BYTES_BIG_ENDIAN)
    {
      int rsize = GET_MODE_SIZE (GET_MODE (rtl));
      int dsize = GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (decl)));

      /* If a variable is declared "register" yet is smaller than
	 a register, then if we store the variable to memory, it
	 looks like we're storing a register-sized value, when in
	 fact we are not.  We need to adjust the offset of the
	 storage location to reflect the actual value's bytes,
	 else gdb will not be able to display it.  */
      if (rsize > dsize)
	rtl = gen_rtx_MEM (TYPE_MODE (TREE_TYPE (decl)),
			   plus_constant (XEXP (rtl, 0), rsize-dsize));
    }

  /* A variable with no DECL_RTL but a DECL_INITIAL is a compile-time constant,
     and will have been substituted directly into all expressions that use it.
     C does not have such a concept, but C++ and other languages do.  */
  if (!rtl && TREE_CODE (decl) == VAR_DECL && DECL_INITIAL (decl))
    rtl = rtl_for_decl_init (DECL_INITIAL (decl), TREE_TYPE (decl));

  if (rtl)
    rtl = targetm.delegitimize_address (rtl);

  /* If we don't look past the constant pool, we risk emitting a
     reference to a constant pool entry that isn't referenced from
     code, and thus is not emitted.  */
  if (rtl)
    rtl = avoid_constant_pool_reference (rtl);

  return rtl;
}

/* We need to figure out what section we should use as the base for the
   address ranges where a given location is valid.
   1. If this particular DECL has a section associated with it, use that.
   2. If this function has a section associated with it, use that.
   3. Otherwise, use the text section.
   XXX: If you split a variable across multiple sections, we won't notice.  */

static const char *
secname_for_decl (tree decl)
{
  const char *secname;

  if (VAR_OR_FUNCTION_DECL_P (decl) && DECL_SECTION_NAME (decl))
    {
      tree sectree = DECL_SECTION_NAME (decl);
      secname = TREE_STRING_POINTER (sectree);
    }
  else if (current_function_decl && DECL_SECTION_NAME (current_function_decl))
    {
      tree sectree = DECL_SECTION_NAME (current_function_decl);
      secname = TREE_STRING_POINTER (sectree);
    }
  else if (cfun && in_cold_section_p)
    secname = cfun->cold_section_label;
  else
    secname = text_section_label;

  return secname;
}

/* Generate *either* a DW_AT_location attribute or else a DW_AT_const_value
   data attribute for a variable or a parameter.  We generate the
   DW_AT_const_value attribute only in those cases where the given variable
   or parameter does not have a true "location" either in memory or in a
   register.  This can happen (for example) when a constant is passed as an
   actual argument in a call to an inline function.  (It's possible that
   these things can crop up in other ways also.)  Note that one type of
   constant value which can be passed into an inlined function is a constant
   pointer.  This can happen for example if an actual argument in an inlined
   function call evaluates to a compile-time constant address.  */

static void
add_location_or_const_value_attribute (dw_die_ref die, tree decl,
				       enum dwarf_attribute attr)
{
  rtx rtl;
  dw_loc_descr_ref descr;
  var_loc_list *loc_list;
  struct var_loc_node *node;
  if (TREE_CODE (decl) == ERROR_MARK)
    return;

  gcc_assert (TREE_CODE (decl) == VAR_DECL || TREE_CODE (decl) == PARM_DECL
	      || TREE_CODE (decl) == RESULT_DECL);
	     
  /* See if we possibly have multiple locations for this variable.  */
  loc_list = lookup_decl_loc (decl);

  /* If it truly has multiple locations, the first and last node will
     differ.  */
  if (loc_list && loc_list->first != loc_list->last)
    {
      const char *endname, *secname;
      dw_loc_list_ref list;
      rtx varloc;

      /* Now that we know what section we are using for a base,
         actually construct the list of locations.
	 The first location information is what is passed to the
	 function that creates the location list, and the remaining
	 locations just get added on to that list.
	 Note that we only know the start address for a location
	 (IE location changes), so to build the range, we use
	 the range [current location start, next location start].
	 This means we have to special case the last node, and generate
	 a range of [last location start, end of function label].  */

      node = loc_list->first;
      varloc = NOTE_VAR_LOCATION (node->var_loc_note);
      secname = secname_for_decl (decl);

      list = new_loc_list (loc_descriptor (varloc),
			   node->label, node->next->label, secname, 1);
      node = node->next;

      for (; node->next; node = node->next)
	if (NOTE_VAR_LOCATION_LOC (node->var_loc_note) != NULL_RTX)
	  {
	    /* The variable has a location between NODE->LABEL and
	       NODE->NEXT->LABEL.  */
	    varloc = NOTE_VAR_LOCATION (node->var_loc_note);
	    add_loc_descr_to_loc_list (&list, loc_descriptor (varloc),
				       node->label, node->next->label, secname);
	  }

      /* If the variable has a location at the last label
	 it keeps its location until the end of function.  */
      if (NOTE_VAR_LOCATION_LOC (node->var_loc_note) != NULL_RTX)
	{
	  char label_id[MAX_ARTIFICIAL_LABEL_BYTES];

	  varloc = NOTE_VAR_LOCATION (node->var_loc_note);
	  if (!current_function_decl)
	    endname = text_end_label;
	  else
	    {
	      ASM_GENERATE_INTERNAL_LABEL (label_id, FUNC_END_LABEL,
					   current_function_funcdef_no);
	      endname = ggc_strdup (label_id);
	    }
	  add_loc_descr_to_loc_list (&list, loc_descriptor (varloc),
				     node->label, endname, secname);
	}

      /* Finally, add the location list to the DIE, and we are done.  */
      add_AT_loc_list (die, attr, list);
      return;
    }

  /* Try to get some constant RTL for this decl, and use that as the value of
     the location.  */
  
  rtl = rtl_for_decl_location (decl);
  if (rtl && (CONSTANT_P (rtl) || GET_CODE (rtl) == CONST_STRING))
    {
      add_const_value_attribute (die, rtl);
      return;
    }
  
  /* If we have tried to generate the location otherwise, and it
     didn't work out (we wouldn't be here if we did), and we have a one entry
     location list, try generating a location from that.  */
  if (loc_list && loc_list->first)
    {
      node = loc_list->first;
      descr = loc_descriptor (NOTE_VAR_LOCATION (node->var_loc_note));
      if (descr)
	{
	  add_AT_location_description (die, attr, descr);
	  return;
	}
    }

  /* We couldn't get any rtl, so try directly generating the location
     description from the tree.  */
  descr = loc_descriptor_from_tree (decl);
  if (descr)
    {
      add_AT_location_description (die, attr, descr);
      return;
    }
  /* None of that worked, so it must not really have a location;
     try adding a constant value attribute from the DECL_INITIAL.  */
  tree_add_const_value_attribute (die, decl);
}

/* If we don't have a copy of this variable in memory for some reason (such
   as a C++ member constant that doesn't have an out-of-line definition),
   we should tell the debugger about the constant value.  */

static void
tree_add_const_value_attribute (dw_die_ref var_die, tree decl)
{
  tree init = DECL_INITIAL (decl);
  tree type = TREE_TYPE (decl);
  rtx rtl;

  if (TREE_READONLY (decl) && ! TREE_THIS_VOLATILE (decl) && init)
    /* OK */;
  else
    return;

  rtl = rtl_for_decl_init (init, type);
  if (rtl)
    add_const_value_attribute (var_die, rtl);
}

/* Convert the CFI instructions for the current function into a
   location list.  This is used for DW_AT_frame_base when we targeting
   a dwarf2 consumer that does not support the dwarf3
   DW_OP_call_frame_cfa.  OFFSET is a constant to be added to all CFA
   expressions.  */

static dw_loc_list_ref
convert_cfa_to_fb_loc_list (HOST_WIDE_INT offset)
{
  dw_fde_ref fde;
  dw_loc_list_ref list, *list_tail;
  dw_cfi_ref cfi;
  dw_cfa_location last_cfa, next_cfa;
  const char *start_label, *last_label, *section;

  fde = &fde_table[fde_table_in_use - 1];

  section = secname_for_decl (current_function_decl);
  list_tail = &list;
  list = NULL;

  next_cfa.reg = INVALID_REGNUM;
  next_cfa.offset = 0;
  next_cfa.indirect = 0;
  next_cfa.base_offset = 0;

  start_label = fde->dw_fde_begin;

  /* ??? Bald assumption that the CIE opcode list does not contain
     advance opcodes.  */
  for (cfi = cie_cfi_head; cfi; cfi = cfi->dw_cfi_next)
    lookup_cfa_1 (cfi, &next_cfa);

  last_cfa = next_cfa;
  last_label = start_label;

  for (cfi = fde->dw_fde_cfi; cfi; cfi = cfi->dw_cfi_next)
    switch (cfi->dw_cfi_opc)
      {
      case DW_CFA_set_loc:
      case DW_CFA_advance_loc1:
      case DW_CFA_advance_loc2:
      case DW_CFA_advance_loc4:
	if (!cfa_equal_p (&last_cfa, &next_cfa))
	  {
	    *list_tail = new_loc_list (build_cfa_loc (&last_cfa, offset),
				       start_label, last_label, section,
				       list == NULL);

	    list_tail = &(*list_tail)->dw_loc_next;
	    last_cfa = next_cfa;
	    start_label = last_label;
	  }
	last_label = cfi->dw_cfi_oprnd1.dw_cfi_addr;
	break;

      case DW_CFA_advance_loc:
	/* The encoding is complex enough that we should never emit this.  */
      case DW_CFA_remember_state:
      case DW_CFA_restore_state:
	/* We don't handle these two in this function.  It would be possible
	   if it were to be required.  */
	gcc_unreachable ();

      default:
	lookup_cfa_1 (cfi, &next_cfa);
	break;
      }

  if (!cfa_equal_p (&last_cfa, &next_cfa))
    {
      *list_tail = new_loc_list (build_cfa_loc (&last_cfa, offset),
				 start_label, last_label, section,
				 list == NULL);
      list_tail = &(*list_tail)->dw_loc_next;
      start_label = last_label;
    }
  *list_tail = new_loc_list (build_cfa_loc (&next_cfa, offset),
			     start_label, fde->dw_fde_end, section,
			     list == NULL);

  return list;
}

/* Compute a displacement from the "steady-state frame pointer" to the
   frame base (often the same as the CFA), and store it in
   frame_pointer_fb_offset.  OFFSET is added to the displacement
   before the latter is negated.  */

static void
compute_frame_pointer_to_fb_displacement (HOST_WIDE_INT offset)
{
  rtx reg, elim;

#ifdef FRAME_POINTER_CFA_OFFSET
  reg = frame_pointer_rtx;
  offset += FRAME_POINTER_CFA_OFFSET (current_function_decl);
#else
  reg = arg_pointer_rtx;
  offset += ARG_POINTER_CFA_OFFSET (current_function_decl);
#endif

  elim = eliminate_regs (reg, VOIDmode, NULL_RTX);
  if (GET_CODE (elim) == PLUS)
    {
      offset += INTVAL (XEXP (elim, 1));
      elim = XEXP (elim, 0);
    }
  gcc_assert (elim == (frame_pointer_needed ? hard_frame_pointer_rtx
		       : stack_pointer_rtx));

  frame_pointer_fb_offset = -offset;
}

/* Generate a DW_AT_name attribute given some string value to be included as
   the value of the attribute.  */

static void
add_name_attribute (dw_die_ref die, const char *name_string)
{
  if (name_string != NULL && *name_string != 0)
    {
      if (demangle_name_func)
	name_string = (*demangle_name_func) (name_string);

      add_AT_string (die, DW_AT_name, name_string);
    }
}

/* Generate a DW_AT_comp_dir attribute for DIE.  */

static void
add_comp_dir_attribute (dw_die_ref die)
{
  const char *wd = get_src_pwd ();
  if (wd != NULL)
    add_AT_string (die, DW_AT_comp_dir, wd);
}

/* Given a tree node describing an array bound (either lower or upper) output
   a representation for that bound.  */

static void
add_bound_info (dw_die_ref subrange_die, enum dwarf_attribute bound_attr, tree bound)
{
  switch (TREE_CODE (bound))
    {
    case ERROR_MARK:
      return;

    /* All fixed-bounds are represented by INTEGER_CST nodes.  */
    case INTEGER_CST:
      if (! host_integerp (bound, 0)
	  || (bound_attr == DW_AT_lower_bound
	      && (((is_c_family () || is_java ()) &&  integer_zerop (bound))
		  || (is_fortran () && integer_onep (bound)))))
	/* Use the default.  */
	;
      else
	add_AT_unsigned (subrange_die, bound_attr, tree_low_cst (bound, 0));
      break;

    case CONVERT_EXPR:
    case NOP_EXPR:
    case NON_LVALUE_EXPR:
    case VIEW_CONVERT_EXPR:
      add_bound_info (subrange_die, bound_attr, TREE_OPERAND (bound, 0));
      break;

    case SAVE_EXPR:
      break;

    case VAR_DECL:
    case PARM_DECL:
    case RESULT_DECL:
      {
	dw_die_ref decl_die = lookup_decl_die (bound);

	/* ??? Can this happen, or should the variable have been bound
	   first?  Probably it can, since I imagine that we try to create
	   the types of parameters in the order in which they exist in
	   the list, and won't have created a forward reference to a
	   later parameter.  */
	if (decl_die != NULL)
	  add_AT_die_ref (subrange_die, bound_attr, decl_die);
	break;
      }

    default:
      {
	/* Otherwise try to create a stack operation procedure to
	   evaluate the value of the array bound.  */

	dw_die_ref ctx, decl_die;
	dw_loc_descr_ref loc;

	loc = loc_descriptor_from_tree (bound);
	if (loc == NULL)
	  break;

	if (current_function_decl == 0)
	  ctx = comp_unit_die;
	else
	  ctx = lookup_decl_die (current_function_decl);

	decl_die = new_die (DW_TAG_variable, ctx, bound);
	add_AT_flag (decl_die, DW_AT_artificial, 1);
	add_type_attribute (decl_die, TREE_TYPE (bound), 1, 0, ctx);
	add_AT_loc (decl_die, DW_AT_location, loc);

	add_AT_die_ref (subrange_die, bound_attr, decl_die);
	break;
      }
    }
}

/* Note that the block of subscript information for an array type also
   includes information about the element type of type given array type.  */

static void
add_subscript_info (dw_die_ref type_die, tree type)
{
#ifndef MIPS_DEBUGGING_INFO
  unsigned dimension_number;
#endif
  tree lower, upper;
  dw_die_ref subrange_die;

  /* The GNU compilers represent multidimensional array types as sequences of
     one dimensional array types whose element types are themselves array
     types.  Here we squish that down, so that each multidimensional array
     type gets only one array_type DIE in the Dwarf debugging info. The draft
     Dwarf specification say that we are allowed to do this kind of
     compression in C (because there is no difference between an array or
     arrays and a multidimensional array in C) but for other source languages
     (e.g. Ada) we probably shouldn't do this.  */

  /* ??? The SGI dwarf reader fails for multidimensional arrays with a
     const enum type.  E.g. const enum machine_mode insn_operand_mode[2][10].
     We work around this by disabling this feature.  See also
     gen_array_type_die.  */
#ifndef MIPS_DEBUGGING_INFO
  for (dimension_number = 0;
       TREE_CODE (type) == ARRAY_TYPE;
       type = TREE_TYPE (type), dimension_number++)
#endif
    {
      tree domain = TYPE_DOMAIN (type);

      /* Arrays come in three flavors: Unspecified bounds, fixed bounds,
	 and (in GNU C only) variable bounds.  Handle all three forms
	 here.  */
      subrange_die = new_die (DW_TAG_subrange_type, type_die, NULL);
      if (domain)
	{
	  /* We have an array type with specified bounds.  */
	  lower = TYPE_MIN_VALUE (domain);
	  upper = TYPE_MAX_VALUE (domain);

	  /* Define the index type.  */
	  if (TREE_TYPE (domain))
	    {
	      /* ??? This is probably an Ada unnamed subrange type.  Ignore the
		 TREE_TYPE field.  We can't emit debug info for this
		 because it is an unnamed integral type.  */
	      if (TREE_CODE (domain) == INTEGER_TYPE
		  && TYPE_NAME (domain) == NULL_TREE
		  && TREE_CODE (TREE_TYPE (domain)) == INTEGER_TYPE
		  && TYPE_NAME (TREE_TYPE (domain)) == NULL_TREE)
		;
	      else
		add_type_attribute (subrange_die, TREE_TYPE (domain), 0, 0,
				    type_die);
	    }

	  /* ??? If upper is NULL, the array has unspecified length,
	     but it does have a lower bound.  This happens with Fortran
	       dimension arr(N:*)
	     Since the debugger is definitely going to need to know N
	     to produce useful results, go ahead and output the lower
	     bound solo, and hope the debugger can cope.  */

	  add_bound_info (subrange_die, DW_AT_lower_bound, lower);
	  if (upper)
	    add_bound_info (subrange_die, DW_AT_upper_bound, upper);
	}

      /* Otherwise we have an array type with an unspecified length.  The
	 DWARF-2 spec does not say how to handle this; let's just leave out the
	 bounds.  */
    }
}

static void
add_byte_size_attribute (dw_die_ref die, tree tree_node)
{
  unsigned size;

  switch (TREE_CODE (tree_node))
    {
    case ERROR_MARK:
      size = 0;
      break;
    case ENUMERAL_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      size = int_size_in_bytes (tree_node);
      break;
    case FIELD_DECL:
      /* For a data member of a struct or union, the DW_AT_byte_size is
	 generally given as the number of bytes normally allocated for an
	 object of the *declared* type of the member itself.  This is true
	 even for bit-fields.  */
      size = simple_type_size_in_bits (field_type (tree_node)) / BITS_PER_UNIT;
      break;
    default:
      gcc_unreachable ();
    }

  /* Note that `size' might be -1 when we get to this point.  If it is, that
     indicates that the byte size of the entity in question is variable.  We
     have no good way of expressing this fact in Dwarf at the present time,
     so just let the -1 pass on through.  */
  add_AT_unsigned (die, DW_AT_byte_size, size);
}

/* For a FIELD_DECL node which represents a bit-field, output an attribute
   which specifies the distance in bits from the highest order bit of the
   "containing object" for the bit-field to the highest order bit of the
   bit-field itself.

   For any given bit-field, the "containing object" is a hypothetical object
   (of some integral or enum type) within which the given bit-field lives.  The
   type of this hypothetical "containing object" is always the same as the
   declared type of the individual bit-field itself.  The determination of the
   exact location of the "containing object" for a bit-field is rather
   complicated.  It's handled by the `field_byte_offset' function (above).

   Note that it is the size (in bytes) of the hypothetical "containing object"
   which will be given in the DW_AT_byte_size attribute for this bit-field.
   (See `byte_size_attribute' above).  */

static inline void
add_bit_offset_attribute (dw_die_ref die, tree decl)
{
  HOST_WIDE_INT object_offset_in_bytes = field_byte_offset (decl);
  tree type = DECL_BIT_FIELD_TYPE (decl);
  HOST_WIDE_INT bitpos_int;
  HOST_WIDE_INT highest_order_object_bit_offset;
  HOST_WIDE_INT highest_order_field_bit_offset;
  HOST_WIDE_INT unsigned bit_offset;

  /* Must be a field and a bit field.  */
  gcc_assert (type && TREE_CODE (decl) == FIELD_DECL);

  /* We can't yet handle bit-fields whose offsets are variable, so if we
     encounter such things, just return without generating any attribute
     whatsoever.  Likewise for variable or too large size.  */
  if (! host_integerp (bit_position (decl), 0)
      || ! host_integerp (DECL_SIZE (decl), 1))
    return;

  bitpos_int = int_bit_position (decl);

  /* Note that the bit offset is always the distance (in bits) from the
     highest-order bit of the "containing object" to the highest-order bit of
     the bit-field itself.  Since the "high-order end" of any object or field
     is different on big-endian and little-endian machines, the computation
     below must take account of these differences.  */
  highest_order_object_bit_offset = object_offset_in_bytes * BITS_PER_UNIT;
  highest_order_field_bit_offset = bitpos_int;

  if (! BYTES_BIG_ENDIAN)
    {
      highest_order_field_bit_offset += tree_low_cst (DECL_SIZE (decl), 0);
      highest_order_object_bit_offset += simple_type_size_in_bits (type);
    }

  bit_offset
    = (! BYTES_BIG_ENDIAN
       ? highest_order_object_bit_offset - highest_order_field_bit_offset
       : highest_order_field_bit_offset - highest_order_object_bit_offset);

  add_AT_unsigned (die, DW_AT_bit_offset, bit_offset);
}

/* For a FIELD_DECL node which represents a bit field, output an attribute
   which specifies the length in bits of the given field.  */

static inline void
add_bit_size_attribute (dw_die_ref die, tree decl)
{
  /* Must be a field and a bit field.  */
  gcc_assert (TREE_CODE (decl) == FIELD_DECL
	      && DECL_BIT_FIELD_TYPE (decl));

  if (host_integerp (DECL_SIZE (decl), 1))
    add_AT_unsigned (die, DW_AT_bit_size, tree_low_cst (DECL_SIZE (decl), 1));
}

/* If the compiled language is ANSI C, then add a 'prototyped'
   attribute, if arg types are given for the parameters of a function.  */

static inline void
add_prototyped_attribute (dw_die_ref die, tree func_type)
{
  if (get_AT_unsigned (comp_unit_die, DW_AT_language) == DW_LANG_C89
      && TYPE_ARG_TYPES (func_type) != NULL)
    add_AT_flag (die, DW_AT_prototyped, 1);
}

/* Add an 'abstract_origin' attribute below a given DIE.  The DIE is found
   by looking in either the type declaration or object declaration
   equate table.  */

static inline void
add_abstract_origin_attribute (dw_die_ref die, tree origin)
{
  dw_die_ref origin_die = NULL;

  if (TREE_CODE (origin) != FUNCTION_DECL)
    {
      /* We may have gotten separated from the block for the inlined
	 function, if we're in an exception handler or some such; make
	 sure that the abstract function has been written out.

	 Doing this for nested functions is wrong, however; functions are
	 distinct units, and our context might not even be inline.  */
      tree fn = origin;

      if (TYPE_P (fn))
	fn = TYPE_STUB_DECL (fn);
      
      fn = decl_function_context (fn);
      if (fn)
	dwarf2out_abstract_function (fn);
    }

  if (DECL_P (origin))
    origin_die = lookup_decl_die (origin);
  else if (TYPE_P (origin))
    origin_die = lookup_type_die (origin);

  /* XXX: Functions that are never lowered don't always have correct block
     trees (in the case of java, they simply have no block tree, in some other
     languages).  For these functions, there is nothing we can really do to
     output correct debug info for inlined functions in all cases.  Rather
     than die, we'll just produce deficient debug info now, in that we will
     have variables without a proper abstract origin.  In the future, when all
     functions are lowered, we should re-add a gcc_assert (origin_die)
     here.  */

  if (origin_die)
      add_AT_die_ref (die, DW_AT_abstract_origin, origin_die);
}

/* We do not currently support the pure_virtual attribute.  */

static inline void
add_pure_or_virtual_attribute (dw_die_ref die, tree func_decl)
{
  if (DECL_VINDEX (func_decl))
    {
      add_AT_unsigned (die, DW_AT_virtuality, DW_VIRTUALITY_virtual);

      if (host_integerp (DECL_VINDEX (func_decl), 0))
	add_AT_loc (die, DW_AT_vtable_elem_location,
		    new_loc_descr (DW_OP_constu,
				   tree_low_cst (DECL_VINDEX (func_decl), 0),
				   0));

      /* GNU extension: Record what type this method came from originally.  */
      if (debug_info_level > DINFO_LEVEL_TERSE)
	add_AT_die_ref (die, DW_AT_containing_type,
			lookup_type_die (DECL_CONTEXT (func_decl)));
    }
}

/* Add source coordinate attributes for the given decl.  */

static void
add_src_coords_attributes (dw_die_ref die, tree decl)
{
  expanded_location s = expand_location (DECL_SOURCE_LOCATION (decl));

  add_AT_file (die, DW_AT_decl_file, lookup_filename (s.file));
  add_AT_unsigned (die, DW_AT_decl_line, s.line);
}

/* Add a DW_AT_name attribute and source coordinate attribute for the
   given decl, but only if it actually has a name.  */

static void
add_name_and_src_coords_attributes (dw_die_ref die, tree decl)
{
  tree decl_name;

  decl_name = DECL_NAME (decl);
  if (decl_name != NULL && IDENTIFIER_POINTER (decl_name) != NULL)
    {
      add_name_attribute (die, dwarf2_name (decl, 0));
      if (! DECL_ARTIFICIAL (decl))
	add_src_coords_attributes (die, decl);

      if ((TREE_CODE (decl) == FUNCTION_DECL || TREE_CODE (decl) == VAR_DECL)
	  && TREE_PUBLIC (decl)
	  && DECL_ASSEMBLER_NAME (decl) != DECL_NAME (decl)
	  && !DECL_ABSTRACT (decl)
	  && !(TREE_CODE (decl) == VAR_DECL && DECL_REGISTER (decl)))
	add_AT_string (die, DW_AT_MIPS_linkage_name,
		       IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)));
    }

#ifdef VMS_DEBUGGING_INFO
  /* Get the function's name, as described by its RTL.  This may be different
     from the DECL_NAME name used in the source file.  */
  if (TREE_CODE (decl) == FUNCTION_DECL && TREE_ASM_WRITTEN (decl))
    {
      add_AT_addr (die, DW_AT_VMS_rtnbeg_pd_address,
		   XEXP (DECL_RTL (decl), 0));
      VEC_safe_push (tree, gc, used_rtx_array, XEXP (DECL_RTL (decl), 0));
    }
#endif
}

/* Push a new declaration scope.  */

static void
push_decl_scope (tree scope)
{
  VEC_safe_push (tree, gc, decl_scope_table, scope);
}

/* Pop a declaration scope.  */

static inline void
pop_decl_scope (void)
{
  VEC_pop (tree, decl_scope_table);
}

/* Return the DIE for the scope that immediately contains this type.
   Non-named types get global scope.  Named types nested in other
   types get their containing scope if it's open, or global scope
   otherwise.  All other types (i.e. function-local named types) get
   the current active scope.  */

static dw_die_ref
scope_die_for (tree t, dw_die_ref context_die)
{
  dw_die_ref scope_die = NULL;
  tree containing_scope;
  int i;

  /* Non-types always go in the current scope.  */
  gcc_assert (TYPE_P (t));

  containing_scope = TYPE_CONTEXT (t);

  /* Use the containing namespace if it was passed in (for a declaration).  */
  if (containing_scope && TREE_CODE (containing_scope) == NAMESPACE_DECL)
    {
      if (context_die == lookup_decl_die (containing_scope))
	/* OK */;
      else
	containing_scope = NULL_TREE;
    }

  /* Ignore function type "scopes" from the C frontend.  They mean that
     a tagged type is local to a parmlist of a function declarator, but
     that isn't useful to DWARF.  */
  if (containing_scope && TREE_CODE (containing_scope) == FUNCTION_TYPE)
    containing_scope = NULL_TREE;

  if (containing_scope == NULL_TREE)
    scope_die = comp_unit_die;
  else if (TYPE_P (containing_scope))
    {
      /* For types, we can just look up the appropriate DIE.  But
	 first we check to see if we're in the middle of emitting it
	 so we know where the new DIE should go.  */
      for (i = VEC_length (tree, decl_scope_table) - 1; i >= 0; --i)
	if (VEC_index (tree, decl_scope_table, i) == containing_scope)
	  break;

      if (i < 0)
	{
	  gcc_assert (debug_info_level <= DINFO_LEVEL_TERSE
		      || TREE_ASM_WRITTEN (containing_scope));

	  /* If none of the current dies are suitable, we get file scope.  */
	  scope_die = comp_unit_die;
	}
      else
	scope_die = lookup_type_die (containing_scope);
    }
  else
    scope_die = context_die;

  return scope_die;
}

/* Returns nonzero if CONTEXT_DIE is internal to a function.  */

static inline int
local_scope_p (dw_die_ref context_die)
{
  for (; context_die; context_die = context_die->die_parent)
    if (context_die->die_tag == DW_TAG_inlined_subroutine
	|| context_die->die_tag == DW_TAG_subprogram)
      return 1;

  return 0;
}

/* Returns nonzero if CONTEXT_DIE is a class or namespace, for deciding
   whether or not to treat a DIE in this context as a declaration.  */

static inline int
class_or_namespace_scope_p (dw_die_ref context_die)
{
  return (context_die
	  && (context_die->die_tag == DW_TAG_structure_type
	      || context_die->die_tag == DW_TAG_union_type
	      || context_die->die_tag == DW_TAG_namespace));
}

/* Many forms of DIEs require a "type description" attribute.  This
   routine locates the proper "type descriptor" die for the type given
   by 'type', and adds a DW_AT_type attribute below the given die.  */

static void
add_type_attribute (dw_die_ref object_die, tree type, int decl_const,
		    int decl_volatile, dw_die_ref context_die)
{
  enum tree_code code  = TREE_CODE (type);
  dw_die_ref type_die  = NULL;

  /* ??? If this type is an unnamed subrange type of an integral or
     floating-point type, use the inner type.  This is because we have no
     support for unnamed types in base_type_die.  This can happen if this is
     an Ada subrange type.  Correct solution is emit a subrange type die.  */
  if ((code == INTEGER_TYPE || code == REAL_TYPE)
      && TREE_TYPE (type) != 0 && TYPE_NAME (type) == 0)
    type = TREE_TYPE (type), code = TREE_CODE (type);

  if (code == ERROR_MARK
      /* Handle a special case.  For functions whose return type is void, we
	 generate *no* type attribute.  (Note that no object may have type
	 `void', so this only applies to function return types).  */
      || code == VOID_TYPE)
    return;

  type_die = modified_type_die (type,
				decl_const || TYPE_READONLY (type),
				decl_volatile || TYPE_VOLATILE (type),
				context_die);

  if (type_die != NULL)
    add_AT_die_ref (object_die, DW_AT_type, type_die);
}

/* Given an object die, add the calling convention attribute for the
   function call type.  */
static void
add_calling_convention_attribute (dw_die_ref subr_die, tree type)
{
  enum dwarf_calling_convention value = DW_CC_normal;

  value = targetm.dwarf_calling_convention (type);

  /* Only add the attribute if the backend requests it, and
     is not DW_CC_normal.  */
  if (value && (value != DW_CC_normal))
    add_AT_unsigned (subr_die, DW_AT_calling_convention, value);
}

/* Given a tree pointer to a struct, class, union, or enum type node, return
   a pointer to the (string) tag name for the given type, or zero if the type
   was declared without a tag.  */

static const char *
type_tag (tree type)
{
  const char *name = 0;

  if (TYPE_NAME (type) != 0)
    {
      tree t = 0;

      /* Find the IDENTIFIER_NODE for the type name.  */
      if (TREE_CODE (TYPE_NAME (type)) == IDENTIFIER_NODE)
	t = TYPE_NAME (type);

      /* The g++ front end makes the TYPE_NAME of *each* tagged type point to
	 a TYPE_DECL node, regardless of whether or not a `typedef' was
	 involved.  */
      else if (TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
	       && ! DECL_IGNORED_P (TYPE_NAME (type)))
	t = DECL_NAME (TYPE_NAME (type));

      /* Now get the name as a string, or invent one.  */
      if (t != 0)
	name = IDENTIFIER_POINTER (t);
    }

  return (name == 0 || *name == '\0') ? 0 : name;
}

/* Return the type associated with a data member, make a special check
   for bit field types.  */

static inline tree
member_declared_type (tree member)
{
  return (DECL_BIT_FIELD_TYPE (member)
	  ? DECL_BIT_FIELD_TYPE (member) : TREE_TYPE (member));
}

/* Get the decl's label, as described by its RTL. This may be different
   from the DECL_NAME name used in the source file.  */

#if 0
static const char *
decl_start_label (tree decl)
{
  rtx x;
  const char *fnname;

  x = DECL_RTL (decl);
  gcc_assert (MEM_P (x));

  x = XEXP (x, 0);
  gcc_assert (GET_CODE (x) == SYMBOL_REF);

  fnname = XSTR (x, 0);
  return fnname;
}
#endif

/* These routines generate the internal representation of the DIE's for
   the compilation unit.  Debugging information is collected by walking
   the declaration trees passed in from dwarf2out_decl().  */

static void
gen_array_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref scope_die = scope_die_for (type, context_die);
  dw_die_ref array_die;
  tree element_type;

  /* ??? The SGI dwarf reader fails for array of array of enum types unless
     the inner array type comes before the outer array type.  Thus we must
     call gen_type_die before we call new_die.  See below also.  */
#ifdef MIPS_DEBUGGING_INFO
  gen_type_die (TREE_TYPE (type), context_die);
#endif

  array_die = new_die (DW_TAG_array_type, scope_die, type);
  add_name_attribute (array_die, type_tag (type));
  equate_type_number_to_die (type, array_die);

  if (TREE_CODE (type) == VECTOR_TYPE)
    {
      /* The frontend feeds us a representation for the vector as a struct
	 containing an array.  Pull out the array type.  */
      type = TREE_TYPE (TYPE_FIELDS (TYPE_DEBUG_REPRESENTATION_TYPE (type)));
      add_AT_flag (array_die, DW_AT_GNU_vector, 1);
    }

#if 0
  /* We default the array ordering.  SDB will probably do
     the right things even if DW_AT_ordering is not present.  It's not even
     an issue until we start to get into multidimensional arrays anyway.  If
     SDB is ever caught doing the Wrong Thing for multi-dimensional arrays,
     then we'll have to put the DW_AT_ordering attribute back in.  (But if
     and when we find out that we need to put these in, we will only do so
     for multidimensional arrays.  */
  add_AT_unsigned (array_die, DW_AT_ordering, DW_ORD_row_major);
#endif

#ifdef MIPS_DEBUGGING_INFO
  /* The SGI compilers handle arrays of unknown bound by setting
     AT_declaration and not emitting any subrange DIEs.  */
  if (! TYPE_DOMAIN (type))
    add_AT_flag (array_die, DW_AT_declaration, 1);
  else
#endif
    add_subscript_info (array_die, type);

  /* Add representation of the type of the elements of this array type.  */
  element_type = TREE_TYPE (type);

  /* ??? The SGI dwarf reader fails for multidimensional arrays with a
     const enum type.  E.g. const enum machine_mode insn_operand_mode[2][10].
     We work around this by disabling this feature.  See also
     add_subscript_info.  */
#ifndef MIPS_DEBUGGING_INFO
  while (TREE_CODE (element_type) == ARRAY_TYPE)
    element_type = TREE_TYPE (element_type);

  gen_type_die (element_type, context_die);
#endif

  add_type_attribute (array_die, element_type, 0, 0, context_die);
}

#if 0
static void
gen_entry_point_die (tree decl, dw_die_ref context_die)
{
  tree origin = decl_ultimate_origin (decl);
  dw_die_ref decl_die = new_die (DW_TAG_entry_point, context_die, decl);

  if (origin != NULL)
    add_abstract_origin_attribute (decl_die, origin);
  else
    {
      add_name_and_src_coords_attributes (decl_die, decl);
      add_type_attribute (decl_die, TREE_TYPE (TREE_TYPE (decl)),
			  0, 0, context_die);
    }

  if (DECL_ABSTRACT (decl))
    equate_decl_number_to_die (decl, decl_die);
  else
    add_AT_lbl_id (decl_die, DW_AT_low_pc, decl_start_label (decl));
}
#endif

/* Walk through the list of incomplete types again, trying once more to
   emit full debugging info for them.  */

static void
retry_incomplete_types (void)
{
  int i;

  for (i = VEC_length (tree, incomplete_types) - 1; i >= 0; i--)
    gen_type_die (VEC_index (tree, incomplete_types, i), comp_unit_die);
}

/* Generate a DIE to represent an inlined instance of an enumeration type.  */

static void
gen_inlined_enumeration_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref type_die = new_die (DW_TAG_enumeration_type, context_die, type);

  /* We do not check for TREE_ASM_WRITTEN (type) being set, as the type may
     be incomplete and such types are not marked.  */
  add_abstract_origin_attribute (type_die, type);
}

/* Generate a DIE to represent an inlined instance of a structure type.  */

static void
gen_inlined_structure_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref type_die = new_die (DW_TAG_structure_type, context_die, type);

  /* We do not check for TREE_ASM_WRITTEN (type) being set, as the type may
     be incomplete and such types are not marked.  */
  add_abstract_origin_attribute (type_die, type);
}

/* Generate a DIE to represent an inlined instance of a union type.  */

static void
gen_inlined_union_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref type_die = new_die (DW_TAG_union_type, context_die, type);

  /* We do not check for TREE_ASM_WRITTEN (type) being set, as the type may
     be incomplete and such types are not marked.  */
  add_abstract_origin_attribute (type_die, type);
}

/* Generate a DIE to represent an enumeration type.  Note that these DIEs
   include all of the information about the enumeration values also. Each
   enumerated type name/value is listed as a child of the enumerated type
   DIE.  */

static dw_die_ref
gen_enumeration_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref type_die = lookup_type_die (type);

  if (type_die == NULL)
    {
      type_die = new_die (DW_TAG_enumeration_type,
			  scope_die_for (type, context_die), type);
      equate_type_number_to_die (type, type_die);
      add_name_attribute (type_die, type_tag (type));
    }
  else if (! TYPE_SIZE (type))
    return type_die;
  else
    remove_AT (type_die, DW_AT_declaration);

  /* Handle a GNU C/C++ extension, i.e. incomplete enum types.  If the
     given enum type is incomplete, do not generate the DW_AT_byte_size
     attribute or the DW_AT_element_list attribute.  */
  if (TYPE_SIZE (type))
    {
      tree link;

      TREE_ASM_WRITTEN (type) = 1;
      add_byte_size_attribute (type_die, type);
      if (TYPE_STUB_DECL (type) != NULL_TREE)
	add_src_coords_attributes (type_die, TYPE_STUB_DECL (type));

      /* If the first reference to this type was as the return type of an
	 inline function, then it may not have a parent.  Fix this now.  */
      if (type_die->die_parent == NULL)
	add_child_die (scope_die_for (type, context_die), type_die);

      for (link = TYPE_VALUES (type);
	   link != NULL; link = TREE_CHAIN (link))
	{
	  dw_die_ref enum_die = new_die (DW_TAG_enumerator, type_die, link);
	  tree value = TREE_VALUE (link);

	  add_name_attribute (enum_die,
			      IDENTIFIER_POINTER (TREE_PURPOSE (link)));

	  if (host_integerp (value, TYPE_UNSIGNED (TREE_TYPE (value))))
	    /* DWARF2 does not provide a way of indicating whether or
	       not enumeration constants are signed or unsigned.  GDB
	       always assumes the values are signed, so we output all
	       values as if they were signed.  That means that
	       enumeration constants with very large unsigned values
	       will appear to have negative values in the debugger.  */
	    add_AT_int (enum_die, DW_AT_const_value,
			tree_low_cst (value, tree_int_cst_sgn (value) > 0));
	}
    }
  else
    add_AT_flag (type_die, DW_AT_declaration, 1);

  return type_die;
}

/* Generate a DIE to represent either a real live formal parameter decl or to
   represent just the type of some formal parameter position in some function
   type.

   Note that this routine is a bit unusual because its argument may be a
   ..._DECL node (i.e. either a PARM_DECL or perhaps a VAR_DECL which
   represents an inlining of some PARM_DECL) or else some sort of a ..._TYPE
   node.  If it's the former then this function is being called to output a
   DIE to represent a formal parameter object (or some inlining thereof).  If
   it's the latter, then this function is only being called to output a
   DW_TAG_formal_parameter DIE to stand as a placeholder for some formal
   argument type of some subprogram type.  */

static dw_die_ref
gen_formal_parameter_die (tree node, dw_die_ref context_die)
{
  dw_die_ref parm_die
    = new_die (DW_TAG_formal_parameter, context_die, node);
  tree origin;

  switch (TREE_CODE_CLASS (TREE_CODE (node)))
    {
    case tcc_declaration:
      origin = decl_ultimate_origin (node);
      if (origin != NULL)
	add_abstract_origin_attribute (parm_die, origin);
      else
	{
	  add_name_and_src_coords_attributes (parm_die, node);
	  add_type_attribute (parm_die, TREE_TYPE (node),
			      TREE_READONLY (node),
			      TREE_THIS_VOLATILE (node),
			      context_die);
	  if (DECL_ARTIFICIAL (node))
	    add_AT_flag (parm_die, DW_AT_artificial, 1);
	}

      equate_decl_number_to_die (node, parm_die);
      if (! DECL_ABSTRACT (node))
	add_location_or_const_value_attribute (parm_die, node, DW_AT_location);

      break;

    case tcc_type:
      /* We were called with some kind of a ..._TYPE node.  */
      add_type_attribute (parm_die, node, 0, 0, context_die);
      break;

    default:
      gcc_unreachable ();
    }

  return parm_die;
}

/* Generate a special type of DIE used as a stand-in for a trailing ellipsis
   at the end of an (ANSI prototyped) formal parameters list.  */

static void
gen_unspecified_parameters_die (tree decl_or_type, dw_die_ref context_die)
{
  new_die (DW_TAG_unspecified_parameters, context_die, decl_or_type);
}

/* Generate a list of nameless DW_TAG_formal_parameter DIEs (and perhaps a
   DW_TAG_unspecified_parameters DIE) to represent the types of the formal
   parameters as specified in some function type specification (except for
   those which appear as part of a function *definition*).  */

static void
gen_formal_types_die (tree function_or_method_type, dw_die_ref context_die)
{
  tree link;
  tree formal_type = NULL;
  tree first_parm_type;
  tree arg;

  if (TREE_CODE (function_or_method_type) == FUNCTION_DECL)
    {
      arg = DECL_ARGUMENTS (function_or_method_type);
      function_or_method_type = TREE_TYPE (function_or_method_type);
    }
  else
    arg = NULL_TREE;

  first_parm_type = TYPE_ARG_TYPES (function_or_method_type);

  /* Make our first pass over the list of formal parameter types and output a
     DW_TAG_formal_parameter DIE for each one.  */
  for (link = first_parm_type; link; )
    {
      dw_die_ref parm_die;

      formal_type = TREE_VALUE (link);
      if (formal_type == void_type_node)
	break;

      /* Output a (nameless) DIE to represent the formal parameter itself.  */
      parm_die = gen_formal_parameter_die (formal_type, context_die);
      if ((TREE_CODE (function_or_method_type) == METHOD_TYPE
	   && link == first_parm_type)
	  || (arg && DECL_ARTIFICIAL (arg)))
	add_AT_flag (parm_die, DW_AT_artificial, 1);

      link = TREE_CHAIN (link);
      if (arg)
	arg = TREE_CHAIN (arg);
    }

  /* If this function type has an ellipsis, add a
     DW_TAG_unspecified_parameters DIE to the end of the parameter list.  */
  if (formal_type != void_type_node)
    gen_unspecified_parameters_die (function_or_method_type, context_die);

  /* Make our second (and final) pass over the list of formal parameter types
     and output DIEs to represent those types (as necessary).  */
  for (link = TYPE_ARG_TYPES (function_or_method_type);
       link && TREE_VALUE (link);
       link = TREE_CHAIN (link))
    gen_type_die (TREE_VALUE (link), context_die);
}

/* We want to generate the DIE for TYPE so that we can generate the
   die for MEMBER, which has been defined; we will need to refer back
   to the member declaration nested within TYPE.  If we're trying to
   generate minimal debug info for TYPE, processing TYPE won't do the
   trick; we need to attach the member declaration by hand.  */

static void
gen_type_die_for_member (tree type, tree member, dw_die_ref context_die)
{
  gen_type_die (type, context_die);

  /* If we're trying to avoid duplicate debug info, we may not have
     emitted the member decl for this function.  Emit it now.  */
  if (TYPE_DECL_SUPPRESS_DEBUG (TYPE_STUB_DECL (type))
      && ! lookup_decl_die (member))
    {
      dw_die_ref type_die;
      gcc_assert (!decl_ultimate_origin (member));

      push_decl_scope (type);
      type_die = lookup_type_die (type);
      if (TREE_CODE (member) == FUNCTION_DECL)
	gen_subprogram_die (member, type_die);
      else if (TREE_CODE (member) == FIELD_DECL)
	{
	  /* Ignore the nameless fields that are used to skip bits but handle
	     C++ anonymous unions and structs.  */
	  if (DECL_NAME (member) != NULL_TREE
	      || TREE_CODE (TREE_TYPE (member)) == UNION_TYPE
	      || TREE_CODE (TREE_TYPE (member)) == RECORD_TYPE)
	    {
	      gen_type_die (member_declared_type (member), type_die);
	      gen_field_die (member, type_die);
	    }
	}
      else
	gen_variable_die (member, type_die);

      pop_decl_scope ();
    }
}

/* Generate the DWARF2 info for the "abstract" instance of a function which we
   may later generate inlined and/or out-of-line instances of.  */

static void
dwarf2out_abstract_function (tree decl)
{
  dw_die_ref old_die;
  tree save_fn;
  struct function *save_cfun;
  tree context;
  int was_abstract = DECL_ABSTRACT (decl);

  /* Make sure we have the actual abstract inline, not a clone.  */
  decl = DECL_ORIGIN (decl);

  old_die = lookup_decl_die (decl);
  if (old_die && get_AT (old_die, DW_AT_inline))
    /* We've already generated the abstract instance.  */
    return;

  /* Be sure we've emitted the in-class declaration DIE (if any) first, so
     we don't get confused by DECL_ABSTRACT.  */
  if (debug_info_level > DINFO_LEVEL_TERSE)
    {
      context = decl_class_context (decl);
      if (context)
	gen_type_die_for_member
	  (context, decl, decl_function_context (decl) ? NULL : comp_unit_die);
    }

  /* Pretend we've just finished compiling this function.  */
  save_fn = current_function_decl;
  save_cfun = cfun;
  current_function_decl = decl;
  cfun = DECL_STRUCT_FUNCTION (decl);

  set_decl_abstract_flags (decl, 1);
  dwarf2out_decl (decl);
  if (! was_abstract)
    set_decl_abstract_flags (decl, 0);

  current_function_decl = save_fn;
  cfun = save_cfun;
}

/* Helper function of premark_used_types() which gets called through
   htab_traverse_resize().

   Marks the DIE of a given type in *SLOT as perennial, so it never gets
   marked as unused by prune_unused_types.  */
static int
premark_used_types_helper (void **slot, void *data ATTRIBUTE_UNUSED)
{
  tree type;
  dw_die_ref die;

  type = *slot;
  die = lookup_type_die (type);
  if (die != NULL)
    die->die_perennial_p = 1;
  return 1;
}

/* Mark all members of used_types_hash as perennial.  */
static void
premark_used_types (void)
{
  if (cfun && cfun->used_types_hash)
    htab_traverse (cfun->used_types_hash, premark_used_types_helper, NULL);
}

/* Generate a DIE to represent a declared function (either file-scope or
   block-local).  */

static void
gen_subprogram_die (tree decl, dw_die_ref context_die)
{
  char label_id[MAX_ARTIFICIAL_LABEL_BYTES];
  tree origin = decl_ultimate_origin (decl);
  dw_die_ref subr_die;
  tree fn_arg_types;
  tree outer_scope;
  dw_die_ref old_die = lookup_decl_die (decl);
  int declaration = (current_function_decl != decl
		     || class_or_namespace_scope_p (context_die));

  premark_used_types ();

  /* It is possible to have both DECL_ABSTRACT and DECLARATION be true if we
     started to generate the abstract instance of an inline, decided to output
     its containing class, and proceeded to emit the declaration of the inline
     from the member list for the class.  If so, DECLARATION takes priority;
     we'll get back to the abstract instance when done with the class.  */

  /* The class-scope declaration DIE must be the primary DIE.  */
  if (origin && declaration && class_or_namespace_scope_p (context_die))
    {
      origin = NULL;
      gcc_assert (!old_die);
    }

  /* Now that the C++ front end lazily declares artificial member fns, we
     might need to retrofit the declaration into its class.  */
  if (!declaration && !origin && !old_die
      && DECL_CONTEXT (decl) && TYPE_P (DECL_CONTEXT (decl))
      && !class_or_namespace_scope_p (context_die)
      && debug_info_level > DINFO_LEVEL_TERSE)
    old_die = force_decl_die (decl);

  if (origin != NULL)
    {
      gcc_assert (!declaration || local_scope_p (context_die));

      /* Fixup die_parent for the abstract instance of a nested
	 inline function.  */
      if (old_die && old_die->die_parent == NULL)
	add_child_die (context_die, old_die);

      subr_die = new_die (DW_TAG_subprogram, context_die, decl);
      add_abstract_origin_attribute (subr_die, origin);
    }
  else if (old_die)
    {
      expanded_location s = expand_location (DECL_SOURCE_LOCATION (decl));
      struct dwarf_file_data * file_index = lookup_filename (s.file);

      if (!get_AT_flag (old_die, DW_AT_declaration)
	  /* We can have a normal definition following an inline one in the
	     case of redefinition of GNU C extern inlines.
	     It seems reasonable to use AT_specification in this case.  */
	  && !get_AT (old_die, DW_AT_inline))
	{
	  /* Detect and ignore this case, where we are trying to output
	     something we have already output.  */
	  return;
	}

      /* If the definition comes from the same place as the declaration,
	 maybe use the old DIE.  We always want the DIE for this function
	 that has the *_pc attributes to be under comp_unit_die so the
	 debugger can find it.  We also need to do this for abstract
	 instances of inlines, since the spec requires the out-of-line copy
	 to have the same parent.  For local class methods, this doesn't
	 apply; we just use the old DIE.  */
      if ((old_die->die_parent == comp_unit_die || context_die == NULL)
	  && (DECL_ARTIFICIAL (decl)
	      || (get_AT_file (old_die, DW_AT_decl_file) == file_index
		  && (get_AT_unsigned (old_die, DW_AT_decl_line)
		      == (unsigned) s.line))))
	{
	  subr_die = old_die;

	  /* Clear out the declaration attribute and the formal parameters.
	     Do not remove all children, because it is possible that this
	     declaration die was forced using force_decl_die(). In such
	     cases die that forced declaration die (e.g. TAG_imported_module)
	     is one of the children that we do not want to remove.  */
	  remove_AT (subr_die, DW_AT_declaration);
	  remove_child_TAG (subr_die, DW_TAG_formal_parameter);
	}
      else
	{
	  subr_die = new_die (DW_TAG_subprogram, context_die, decl);
	  add_AT_specification (subr_die, old_die);
	  if (get_AT_file (old_die, DW_AT_decl_file) != file_index)
	    add_AT_file (subr_die, DW_AT_decl_file, file_index);
	  if (get_AT_unsigned (old_die, DW_AT_decl_line) != (unsigned) s.line)
	    add_AT_unsigned (subr_die, DW_AT_decl_line, s.line);
	}
    }
  else
    {
      subr_die = new_die (DW_TAG_subprogram, context_die, decl);

      if (TREE_PUBLIC (decl))
	add_AT_flag (subr_die, DW_AT_external, 1);

      add_name_and_src_coords_attributes (subr_die, decl);
      if (debug_info_level > DINFO_LEVEL_TERSE)
	{
	  add_prototyped_attribute (subr_die, TREE_TYPE (decl));
	  add_type_attribute (subr_die, TREE_TYPE (TREE_TYPE (decl)),
			      0, 0, context_die);
	}

      add_pure_or_virtual_attribute (subr_die, decl);
      if (DECL_ARTIFICIAL (decl))
	add_AT_flag (subr_die, DW_AT_artificial, 1);

      if (TREE_PROTECTED (decl))
	add_AT_unsigned (subr_die, DW_AT_accessibility, DW_ACCESS_protected);
      else if (TREE_PRIVATE (decl))
	add_AT_unsigned (subr_die, DW_AT_accessibility, DW_ACCESS_private);
    }

  if (declaration)
    {
      if (!old_die || !get_AT (old_die, DW_AT_inline))
	{
	  add_AT_flag (subr_die, DW_AT_declaration, 1);

	  /* The first time we see a member function, it is in the context of
	     the class to which it belongs.  We make sure of this by emitting
	     the class first.  The next time is the definition, which is
	     handled above.  The two may come from the same source text.

	     Note that force_decl_die() forces function declaration die. It is
	     later reused to represent definition.  */
	  equate_decl_number_to_die (decl, subr_die);
	}
    }
  else if (DECL_ABSTRACT (decl))
    {
      if (DECL_DECLARED_INLINE_P (decl))
	{
          if (cgraph_function_possibly_inlined_p (decl))
	    add_AT_unsigned (subr_die, DW_AT_inline, DW_INL_declared_inlined);
	  else
	    add_AT_unsigned (subr_die, DW_AT_inline, DW_INL_declared_not_inlined);
	}
      else
	{
	  if (cgraph_function_possibly_inlined_p (decl))
            add_AT_unsigned (subr_die, DW_AT_inline, DW_INL_inlined);
	  else
            add_AT_unsigned (subr_die, DW_AT_inline, DW_INL_not_inlined);
	}

      equate_decl_number_to_die (decl, subr_die);
    }
  else if (!DECL_EXTERNAL (decl))
    {
      HOST_WIDE_INT cfa_fb_offset;

      if (!old_die || !get_AT (old_die, DW_AT_inline))
	equate_decl_number_to_die (decl, subr_die);

      if (!flag_reorder_blocks_and_partition)
	{
	  ASM_GENERATE_INTERNAL_LABEL (label_id, FUNC_BEGIN_LABEL,
				       current_function_funcdef_no);
	  add_AT_lbl_id (subr_die, DW_AT_low_pc, label_id);
	  ASM_GENERATE_INTERNAL_LABEL (label_id, FUNC_END_LABEL,
				       current_function_funcdef_no);
	  add_AT_lbl_id (subr_die, DW_AT_high_pc, label_id);
	  
	  add_pubname (decl, subr_die);
	  add_arange (decl, subr_die);
	}
      else
	{  /* Do nothing for now; maybe need to duplicate die, one for
	      hot section and ond for cold section, then use the hot/cold
	      section begin/end labels to generate the aranges...  */
	  /*
	    add_AT_lbl_id (subr_die, DW_AT_low_pc, hot_section_label);
	    add_AT_lbl_id (subr_die, DW_AT_high_pc, hot_section_end_label);
	    add_AT_lbl_id (subr_die, DW_AT_lo_user, unlikely_section_label);
	    add_AT_lbl_id (subr_die, DW_AT_hi_user, cold_section_end_label);

	    add_pubname (decl, subr_die);
	    add_arange (decl, subr_die);
	    add_arange (decl, subr_die);
	   */
	}

#ifdef MIPS_DEBUGGING_INFO
      /* Add a reference to the FDE for this routine.  */
      add_AT_fde_ref (subr_die, DW_AT_MIPS_fde, current_funcdef_fde);
#endif

      cfa_fb_offset = CFA_FRAME_BASE_OFFSET (decl);

      /* We define the "frame base" as the function's CFA.  This is more
	 convenient for several reasons: (1) It's stable across the prologue
	 and epilogue, which makes it better than just a frame pointer,
	 (2) With dwarf3, there exists a one-byte encoding that allows us
	 to reference the .debug_frame data by proxy, but failing that,
	 (3) We can at least reuse the code inspection and interpretation
	 code that determines the CFA position at various points in the
	 function.  */
      /* ??? Use some command-line or configury switch to enable the use
	 of dwarf3 DW_OP_call_frame_cfa.  At present there are no dwarf
	 consumers that understand it; fall back to "pure" dwarf2 and
	 convert the CFA data into a location list.  */
      {
	dw_loc_list_ref list = convert_cfa_to_fb_loc_list (cfa_fb_offset);
	if (list->dw_loc_next)
	  add_AT_loc_list (subr_die, DW_AT_frame_base, list);
	else
	  add_AT_loc (subr_die, DW_AT_frame_base, list->expr);
      }

      /* Compute a displacement from the "steady-state frame pointer" to
	 the CFA.  The former is what all stack slots and argument slots
	 will reference in the rtl; the later is what we've told the 
	 debugger about.  We'll need to adjust all frame_base references
	 by this displacement.  */
      compute_frame_pointer_to_fb_displacement (cfa_fb_offset);

      if (cfun->static_chain_decl)
	add_AT_location_description (subr_die, DW_AT_static_link,
		 loc_descriptor_from_tree (cfun->static_chain_decl));
    }

  /* Now output descriptions of the arguments for this function. This gets
     (unnecessarily?) complex because of the fact that the DECL_ARGUMENT list
     for a FUNCTION_DECL doesn't indicate cases where there was a trailing
     `...' at the end of the formal parameter list.  In order to find out if
     there was a trailing ellipsis or not, we must instead look at the type
     associated with the FUNCTION_DECL.  This will be a node of type
     FUNCTION_TYPE. If the chain of type nodes hanging off of this
     FUNCTION_TYPE node ends with a void_type_node then there should *not* be
     an ellipsis at the end.  */

  /* In the case where we are describing a mere function declaration, all we
     need to do here (and all we *can* do here) is to describe the *types* of
     its formal parameters.  */
  if (debug_info_level <= DINFO_LEVEL_TERSE)
    ;
  else if (declaration)
    gen_formal_types_die (decl, subr_die);
  else
    {
      /* Generate DIEs to represent all known formal parameters.  */
      tree arg_decls = DECL_ARGUMENTS (decl);
      tree parm;

      /* When generating DIEs, generate the unspecified_parameters DIE
	 instead if we come across the arg "__builtin_va_alist" */
      for (parm = arg_decls; parm; parm = TREE_CHAIN (parm))
	if (TREE_CODE (parm) == PARM_DECL)
	  {
	    if (DECL_NAME (parm)
		&& !strcmp (IDENTIFIER_POINTER (DECL_NAME (parm)),
			    "__builtin_va_alist"))
	      gen_unspecified_parameters_die (parm, subr_die);
	    else
	      gen_decl_die (parm, subr_die);
	  }

      /* Decide whether we need an unspecified_parameters DIE at the end.
	 There are 2 more cases to do this for: 1) the ansi ... declaration -
	 this is detectable when the end of the arg list is not a
	 void_type_node 2) an unprototyped function declaration (not a
	 definition).  This just means that we have no info about the
	 parameters at all.  */
      fn_arg_types = TYPE_ARG_TYPES (TREE_TYPE (decl));
      if (fn_arg_types != NULL)
	{
	  /* This is the prototyped case, check for....  */
	  if (TREE_VALUE (tree_last (fn_arg_types)) != void_type_node)
	    gen_unspecified_parameters_die (decl, subr_die);
	}
      else if (DECL_INITIAL (decl) == NULL_TREE)
	gen_unspecified_parameters_die (decl, subr_die);
    }

  /* Output Dwarf info for all of the stuff within the body of the function
     (if it has one - it may be just a declaration).  */
  outer_scope = DECL_INITIAL (decl);

  /* OUTER_SCOPE is a pointer to the outermost BLOCK node created to represent
     a function.  This BLOCK actually represents the outermost binding contour
     for the function, i.e. the contour in which the function's formal
     parameters and labels get declared. Curiously, it appears that the front
     end doesn't actually put the PARM_DECL nodes for the current function onto
     the BLOCK_VARS list for this outer scope, but are strung off of the
     DECL_ARGUMENTS list for the function instead.

     The BLOCK_VARS list for the `outer_scope' does provide us with a list of
     the LABEL_DECL nodes for the function however, and we output DWARF info
     for those in decls_for_scope.  Just within the `outer_scope' there will be
     a BLOCK node representing the function's outermost pair of curly braces,
     and any blocks used for the base and member initializers of a C++
     constructor function.  */
  if (! declaration && TREE_CODE (outer_scope) != ERROR_MARK)
    {
      /* Emit a DW_TAG_variable DIE for a named return value.  */
      if (DECL_NAME (DECL_RESULT (decl)))
	gen_decl_die (DECL_RESULT (decl), subr_die);

      current_function_has_inlines = 0;
      decls_for_scope (outer_scope, subr_die, 0);

#if 0 && defined (MIPS_DEBUGGING_INFO)
      if (current_function_has_inlines)
	{
	  add_AT_flag (subr_die, DW_AT_MIPS_has_inlines, 1);
	  if (! comp_unit_has_inlines)
	    {
	      add_AT_flag (comp_unit_die, DW_AT_MIPS_has_inlines, 1);
	      comp_unit_has_inlines = 1;
	    }
	}
#endif
    }
  /* Add the calling convention attribute if requested.  */
  add_calling_convention_attribute (subr_die, TREE_TYPE (decl));

#ifdef TARGET_SAVE_ARGS
  if (TARGET_SAVE_ARGS)
    add_AT_flag (subr_die, DW_AT_SUN_amd64_parmdump, 1);
#endif
}

/* Generate a DIE to represent a declared data object.  */

static void
gen_variable_die (tree decl, dw_die_ref context_die)
{
  tree origin = decl_ultimate_origin (decl);
  dw_die_ref var_die = new_die (DW_TAG_variable, context_die, decl);

  dw_die_ref old_die = lookup_decl_die (decl);
  int declaration = (DECL_EXTERNAL (decl)
		     /* If DECL is COMDAT and has not actually been
			emitted, we cannot take its address; there
			might end up being no definition anywhere in
			the program.  For example, consider the C++
			test case:

                          template <class T>
                          struct S { static const int i = 7; };

                          template <class T>
                          const int S<T>::i;

                          int f() { return S<int>::i; }
			  
			Here, S<int>::i is not DECL_EXTERNAL, but no
			definition is required, so the compiler will
			not emit a definition.  */  
		     || (TREE_CODE (decl) == VAR_DECL
			 && DECL_COMDAT (decl) && !TREE_ASM_WRITTEN (decl))
		     || class_or_namespace_scope_p (context_die));

  if (origin != NULL)
    add_abstract_origin_attribute (var_die, origin);

  /* Loop unrolling can create multiple blocks that refer to the same
     static variable, so we must test for the DW_AT_declaration flag.

     ??? Loop unrolling/reorder_blocks should perhaps be rewritten to
     copy decls and set the DECL_ABSTRACT flag on them instead of
     sharing them.

     ??? Duplicated blocks have been rewritten to use .debug_ranges.

     ??? The declare_in_namespace support causes us to get two DIEs for one
     variable, both of which are declarations.  We want to avoid considering
     one to be a specification, so we must test that this DIE is not a
     declaration.  */
  else if (old_die && TREE_STATIC (decl) && ! declaration
	   && get_AT_flag (old_die, DW_AT_declaration) == 1)
    {
      /* This is a definition of a C++ class level static.  */
      add_AT_specification (var_die, old_die);
      if (DECL_NAME (decl))
	{
	  expanded_location s = expand_location (DECL_SOURCE_LOCATION (decl));
	  struct dwarf_file_data * file_index = lookup_filename (s.file);

	  if (get_AT_file (old_die, DW_AT_decl_file) != file_index)
	    add_AT_file (var_die, DW_AT_decl_file, file_index);

	  if (get_AT_unsigned (old_die, DW_AT_decl_line) != (unsigned) s.line)

	    add_AT_unsigned (var_die, DW_AT_decl_line, s.line);
	}
    }
  else
    {
      add_name_and_src_coords_attributes (var_die, decl);
      add_type_attribute (var_die, TREE_TYPE (decl), TREE_READONLY (decl),
			  TREE_THIS_VOLATILE (decl), context_die);

      if (TREE_PUBLIC (decl))
	add_AT_flag (var_die, DW_AT_external, 1);

      if (DECL_ARTIFICIAL (decl))
	add_AT_flag (var_die, DW_AT_artificial, 1);

      if (TREE_PROTECTED (decl))
	add_AT_unsigned (var_die, DW_AT_accessibility, DW_ACCESS_protected);
      else if (TREE_PRIVATE (decl))
	add_AT_unsigned (var_die, DW_AT_accessibility, DW_ACCESS_private);
    }

  if (declaration)
    add_AT_flag (var_die, DW_AT_declaration, 1);

  if (DECL_ABSTRACT (decl) || declaration)
    equate_decl_number_to_die (decl, var_die);

  if (! declaration && ! DECL_ABSTRACT (decl))
    {
      add_location_or_const_value_attribute (var_die, decl, DW_AT_location);
      add_pubname (decl, var_die);
    }
  else
    tree_add_const_value_attribute (var_die, decl);
}

/* Generate a DIE to represent a label identifier.  */

static void
gen_label_die (tree decl, dw_die_ref context_die)
{
  tree origin = decl_ultimate_origin (decl);
  dw_die_ref lbl_die = new_die (DW_TAG_label, context_die, decl);
  rtx insn;
  char label[MAX_ARTIFICIAL_LABEL_BYTES];

  if (origin != NULL)
    add_abstract_origin_attribute (lbl_die, origin);
  else
    add_name_and_src_coords_attributes (lbl_die, decl);

  if (DECL_ABSTRACT (decl))
    equate_decl_number_to_die (decl, lbl_die);
  else
    {
      insn = DECL_RTL_IF_SET (decl);

      /* Deleted labels are programmer specified labels which have been
	 eliminated because of various optimizations.  We still emit them
	 here so that it is possible to put breakpoints on them.  */
      if (insn
	  && (LABEL_P (insn)
	      || ((NOTE_P (insn)
	           && NOTE_LINE_NUMBER (insn) == NOTE_INSN_DELETED_LABEL))))
	{
	  /* When optimization is enabled (via -O) some parts of the compiler
	     (e.g. jump.c and cse.c) may try to delete CODE_LABEL insns which
	     represent source-level labels which were explicitly declared by
	     the user.  This really shouldn't be happening though, so catch
	     it if it ever does happen.  */
	  gcc_assert (!INSN_DELETED_P (insn));

	  ASM_GENERATE_INTERNAL_LABEL (label, "L", CODE_LABEL_NUMBER (insn));
	  add_AT_lbl_id (lbl_die, DW_AT_low_pc, label);
	}
    }
}

/* A helper function for gen_inlined_subroutine_die.  Add source coordinate
   attributes to the DIE for a block STMT, to describe where the inlined
   function was called from.  This is similar to add_src_coords_attributes.  */

static inline void
add_call_src_coords_attributes (tree stmt, dw_die_ref die)
{
  expanded_location s = expand_location (BLOCK_SOURCE_LOCATION (stmt));

  add_AT_file (die, DW_AT_call_file, lookup_filename (s.file));
  add_AT_unsigned (die, DW_AT_call_line, s.line);
}

/* A helper function for gen_lexical_block_die and gen_inlined_subroutine_die.
   Add low_pc and high_pc attributes to the DIE for a block STMT.  */

static inline void
add_high_low_attributes (tree stmt, dw_die_ref die)
{
  char label[MAX_ARTIFICIAL_LABEL_BYTES];

  if (BLOCK_FRAGMENT_CHAIN (stmt))
    {
      tree chain;

      add_AT_range_list (die, DW_AT_ranges, add_ranges (stmt));

      chain = BLOCK_FRAGMENT_CHAIN (stmt);
      do
	{
	  add_ranges (chain);
	  chain = BLOCK_FRAGMENT_CHAIN (chain);
	}
      while (chain);
      add_ranges (NULL);
    }
  else
    {
      ASM_GENERATE_INTERNAL_LABEL (label, BLOCK_BEGIN_LABEL,
				   BLOCK_NUMBER (stmt));
      add_AT_lbl_id (die, DW_AT_low_pc, label);
      ASM_GENERATE_INTERNAL_LABEL (label, BLOCK_END_LABEL,
				   BLOCK_NUMBER (stmt));
      add_AT_lbl_id (die, DW_AT_high_pc, label);
    }
}

/* Generate a DIE for a lexical block.  */

static void
gen_lexical_block_die (tree stmt, dw_die_ref context_die, int depth)
{
  dw_die_ref stmt_die = new_die (DW_TAG_lexical_block, context_die, stmt);

  if (! BLOCK_ABSTRACT (stmt))
    add_high_low_attributes (stmt, stmt_die);

  decls_for_scope (stmt, stmt_die, depth);
}

/* Generate a DIE for an inlined subprogram.  */

static void
gen_inlined_subroutine_die (tree stmt, dw_die_ref context_die, int depth)
{
  tree decl = block_ultimate_origin (stmt);

  /* Emit info for the abstract instance first, if we haven't yet.  We
     must emit this even if the block is abstract, otherwise when we
     emit the block below (or elsewhere), we may end up trying to emit
     a die whose origin die hasn't been emitted, and crashing.  */
  dwarf2out_abstract_function (decl);

  if (! BLOCK_ABSTRACT (stmt))
    {
      dw_die_ref subr_die
	= new_die (DW_TAG_inlined_subroutine, context_die, stmt);

      add_abstract_origin_attribute (subr_die, decl);
      add_high_low_attributes (stmt, subr_die);
      add_call_src_coords_attributes (stmt, subr_die);

      decls_for_scope (stmt, subr_die, depth);
      current_function_has_inlines = 1;
    }
  else
    /* We may get here if we're the outer block of function A that was
       inlined into function B that was inlined into function C.  When
       generating debugging info for C, dwarf2out_abstract_function(B)
       would mark all inlined blocks as abstract, including this one.
       So, we wouldn't (and shouldn't) expect labels to be generated
       for this one.  Instead, just emit debugging info for
       declarations within the block.  This is particularly important
       in the case of initializers of arguments passed from B to us:
       if they're statement expressions containing declarations, we
       wouldn't generate dies for their abstract variables, and then,
       when generating dies for the real variables, we'd die (pun
       intended :-)  */
    gen_lexical_block_die (stmt, context_die, depth);
}

/* Generate a DIE for a field in a record, or structure.  */

static void
gen_field_die (tree decl, dw_die_ref context_die)
{
  dw_die_ref decl_die;

  if (TREE_TYPE (decl) == error_mark_node)
    return;

  decl_die = new_die (DW_TAG_member, context_die, decl);
  add_name_and_src_coords_attributes (decl_die, decl);
  add_type_attribute (decl_die, member_declared_type (decl),
		      TREE_READONLY (decl), TREE_THIS_VOLATILE (decl),
		      context_die);

  if (DECL_BIT_FIELD_TYPE (decl))
    {
      add_byte_size_attribute (decl_die, decl);
      add_bit_size_attribute (decl_die, decl);
      add_bit_offset_attribute (decl_die, decl);
    }

  if (TREE_CODE (DECL_FIELD_CONTEXT (decl)) != UNION_TYPE)
    add_data_member_location_attribute (decl_die, decl);

  if (DECL_ARTIFICIAL (decl))
    add_AT_flag (decl_die, DW_AT_artificial, 1);

  if (TREE_PROTECTED (decl))
    add_AT_unsigned (decl_die, DW_AT_accessibility, DW_ACCESS_protected);
  else if (TREE_PRIVATE (decl))
    add_AT_unsigned (decl_die, DW_AT_accessibility, DW_ACCESS_private);

  /* Equate decl number to die, so that we can look up this decl later on.  */
  equate_decl_number_to_die (decl, decl_die);
}

#if 0
/* Don't generate either pointer_type DIEs or reference_type DIEs here.
   Use modified_type_die instead.
   We keep this code here just in case these types of DIEs may be needed to
   represent certain things in other languages (e.g. Pascal) someday.  */

static void
gen_pointer_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref ptr_die
    = new_die (DW_TAG_pointer_type, scope_die_for (type, context_die), type);

  equate_type_number_to_die (type, ptr_die);
  add_type_attribute (ptr_die, TREE_TYPE (type), 0, 0, context_die);
  add_AT_unsigned (mod_type_die, DW_AT_byte_size, PTR_SIZE);
}

/* Don't generate either pointer_type DIEs or reference_type DIEs here.
   Use modified_type_die instead.
   We keep this code here just in case these types of DIEs may be needed to
   represent certain things in other languages (e.g. Pascal) someday.  */

static void
gen_reference_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref ref_die
    = new_die (DW_TAG_reference_type, scope_die_for (type, context_die), type);

  equate_type_number_to_die (type, ref_die);
  add_type_attribute (ref_die, TREE_TYPE (type), 0, 0, context_die);
  add_AT_unsigned (mod_type_die, DW_AT_byte_size, PTR_SIZE);
}
#endif

/* Generate a DIE for a pointer to a member type.  */

static void
gen_ptr_to_mbr_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref ptr_die
    = new_die (DW_TAG_ptr_to_member_type,
	       scope_die_for (type, context_die), type);

  equate_type_number_to_die (type, ptr_die);
  add_AT_die_ref (ptr_die, DW_AT_containing_type,
		  lookup_type_die (TYPE_OFFSET_BASETYPE (type)));
  add_type_attribute (ptr_die, TREE_TYPE (type), 0, 0, context_die);
}

/* Generate the DIE for the compilation unit.  */

static dw_die_ref
gen_compile_unit_die (const char *filename)
{
  dw_die_ref die;
  char producer[250];
  const char *language_string = lang_hooks.name;
  int language;

  die = new_die (DW_TAG_compile_unit, NULL, NULL);

  if (filename)
    {
      add_name_attribute (die, filename);
      /* Don't add cwd for <built-in>.  */
      if (filename[0] != DIR_SEPARATOR && filename[0] != '<')
	add_comp_dir_attribute (die);
    }

  sprintf (producer, "%s %s", language_string, version_string);

#ifdef MIPS_DEBUGGING_INFO
  /* The MIPS/SGI compilers place the 'cc' command line options in the producer
     string.  The SGI debugger looks for -g, -g1, -g2, or -g3; if they do
     not appear in the producer string, the debugger reaches the conclusion
     that the object file is stripped and has no debugging information.
     To get the MIPS/SGI debugger to believe that there is debugging
     information in the object file, we add a -g to the producer string.  */
  if (debug_info_level > DINFO_LEVEL_TERSE)
    strcat (producer, " -g");
#endif

  add_AT_string (die, DW_AT_producer, producer);

  if (strcmp (language_string, "GNU C++") == 0)
    language = DW_LANG_C_plus_plus;
  else if (strcmp (language_string, "GNU Ada") == 0)
    language = DW_LANG_Ada95;
  else if (strcmp (language_string, "GNU F77") == 0)
    language = DW_LANG_Fortran77;
  else if (strcmp (language_string, "GNU F95") == 0)
    language = DW_LANG_Fortran95;
  else if (strcmp (language_string, "GNU Pascal") == 0)
    language = DW_LANG_Pascal83;
  else if (strcmp (language_string, "GNU Java") == 0)
    language = DW_LANG_Java;
  else if (strcmp (language_string, "GNU Objective-C") == 0)
    language = DW_LANG_ObjC;
  else if (strcmp (language_string, "GNU Objective-C++") == 0)
    language = DW_LANG_ObjC_plus_plus;
  else
    language = DW_LANG_C89;

  add_AT_unsigned (die, DW_AT_language, language);
  return die;
}

/* Generate the DIE for a base class.  */

static void
gen_inheritance_die (tree binfo, tree access, dw_die_ref context_die)
{
  dw_die_ref die = new_die (DW_TAG_inheritance, context_die, binfo);

  add_type_attribute (die, BINFO_TYPE (binfo), 0, 0, context_die);
  add_data_member_location_attribute (die, binfo);

  if (BINFO_VIRTUAL_P (binfo))
    add_AT_unsigned (die, DW_AT_virtuality, DW_VIRTUALITY_virtual);

  if (access == access_public_node)
    add_AT_unsigned (die, DW_AT_accessibility, DW_ACCESS_public);
  else if (access == access_protected_node)
    add_AT_unsigned (die, DW_AT_accessibility, DW_ACCESS_protected);
}

/* Generate a DIE for a class member.  */

static void
gen_member_die (tree type, dw_die_ref context_die)
{
  tree member;
  tree binfo = TYPE_BINFO (type);
  dw_die_ref child;

  /* If this is not an incomplete type, output descriptions of each of its
     members. Note that as we output the DIEs necessary to represent the
     members of this record or union type, we will also be trying to output
     DIEs to represent the *types* of those members. However the `type'
     function (above) will specifically avoid generating type DIEs for member
     types *within* the list of member DIEs for this (containing) type except
     for those types (of members) which are explicitly marked as also being
     members of this (containing) type themselves.  The g++ front- end can
     force any given type to be treated as a member of some other (containing)
     type by setting the TYPE_CONTEXT of the given (member) type to point to
     the TREE node representing the appropriate (containing) type.  */

  /* First output info about the base classes.  */
  if (binfo)
    {
      VEC(tree,gc) *accesses = BINFO_BASE_ACCESSES (binfo);
      int i;
      tree base;

      for (i = 0; BINFO_BASE_ITERATE (binfo, i, base); i++)
	gen_inheritance_die (base,
			     (accesses ? VEC_index (tree, accesses, i)
			      : access_public_node), context_die);
    }

  /* Now output info about the data members and type members.  */
  for (member = TYPE_FIELDS (type); member; member = TREE_CHAIN (member))
    {
      /* If we thought we were generating minimal debug info for TYPE
	 and then changed our minds, some of the member declarations
	 may have already been defined.  Don't define them again, but
	 do put them in the right order.  */

      child = lookup_decl_die (member);
      if (child)
	splice_child_die (context_die, child);
      else
	gen_decl_die (member, context_die);
    }

  /* Now output info about the function members (if any).  */
  for (member = TYPE_METHODS (type); member; member = TREE_CHAIN (member))
    {
      /* Don't include clones in the member list.  */
      if (DECL_ABSTRACT_ORIGIN (member))
	continue;

      child = lookup_decl_die (member);
      if (child)
	splice_child_die (context_die, child);
      else
	gen_decl_die (member, context_die);
    }
}

/* Generate a DIE for a structure or union type.  If TYPE_DECL_SUPPRESS_DEBUG
   is set, we pretend that the type was never defined, so we only get the
   member DIEs needed by later specification DIEs.  */

static void
gen_struct_or_union_type_die (tree type, dw_die_ref context_die)
{
  dw_die_ref type_die = lookup_type_die (type);
  dw_die_ref scope_die = 0;
  int nested = 0;
  int complete = (TYPE_SIZE (type)
		  && (! TYPE_STUB_DECL (type)
		      || ! TYPE_DECL_SUPPRESS_DEBUG (TYPE_STUB_DECL (type))));
  int ns_decl = (context_die && context_die->die_tag == DW_TAG_namespace);

  if (type_die && ! complete)
    return;

  if (TYPE_CONTEXT (type) != NULL_TREE
      && (AGGREGATE_TYPE_P (TYPE_CONTEXT (type))
	  || TREE_CODE (TYPE_CONTEXT (type)) == NAMESPACE_DECL))
    nested = 1;

  scope_die = scope_die_for (type, context_die);

  if (! type_die || (nested && scope_die == comp_unit_die))
    /* First occurrence of type or toplevel definition of nested class.  */
    {
      dw_die_ref old_die = type_die;

      type_die = new_die (TREE_CODE (type) == RECORD_TYPE
			  ? DW_TAG_structure_type : DW_TAG_union_type,
			  scope_die, type);
      equate_type_number_to_die (type, type_die);
      if (old_die)
	add_AT_specification (type_die, old_die);
      else
	add_name_attribute (type_die, type_tag (type));
    }
  else
    remove_AT (type_die, DW_AT_declaration);

  /* If this type has been completed, then give it a byte_size attribute and
     then give a list of members.  */
  if (complete && !ns_decl)
    {
      /* Prevent infinite recursion in cases where the type of some member of
	 this type is expressed in terms of this type itself.  */
      TREE_ASM_WRITTEN (type) = 1;
      add_byte_size_attribute (type_die, type);
      if (TYPE_STUB_DECL (type) != NULL_TREE)
	add_src_coords_attributes (type_die, TYPE_STUB_DECL (type));

      /* If the first reference to this type was as the return type of an
	 inline function, then it may not have a parent.  Fix this now.  */
      if (type_die->die_parent == NULL)
	add_child_die (scope_die, type_die);

      push_decl_scope (type);
      gen_member_die (type, type_die);
      pop_decl_scope ();

      /* GNU extension: Record what type our vtable lives in.  */
      if (TYPE_VFIELD (type))
	{
	  tree vtype = DECL_FCONTEXT (TYPE_VFIELD (type));

	  gen_type_die (vtype, context_die);
	  add_AT_die_ref (type_die, DW_AT_containing_type,
			  lookup_type_die (vtype));
	}
    }
  else
    {
      add_AT_flag (type_die, DW_AT_declaration, 1);

      /* We don't need to do this for function-local types.  */
      if (TYPE_STUB_DECL (type)
	  && ! decl_function_context (TYPE_STUB_DECL (type)))
	VEC_safe_push (tree, gc, incomplete_types, type);
    }
}

/* Generate a DIE for a subroutine _type_.  */

static void
gen_subroutine_type_die (tree type, dw_die_ref context_die)
{
  tree return_type = TREE_TYPE (type);
  dw_die_ref subr_die
    = new_die (DW_TAG_subroutine_type,
	       scope_die_for (type, context_die), type);

  equate_type_number_to_die (type, subr_die);
  add_prototyped_attribute (subr_die, type);
  add_type_attribute (subr_die, return_type, 0, 0, context_die);
  gen_formal_types_die (type, subr_die);
}

/* Generate a DIE for a type definition.  */

static void
gen_typedef_die (tree decl, dw_die_ref context_die)
{
  dw_die_ref type_die;
  tree origin;

  if (TREE_ASM_WRITTEN (decl))
    return;

  TREE_ASM_WRITTEN (decl) = 1;
  type_die = new_die (DW_TAG_typedef, context_die, decl);
  origin = decl_ultimate_origin (decl);
  if (origin != NULL)
    add_abstract_origin_attribute (type_die, origin);
  else
    {
      tree type;

      add_name_and_src_coords_attributes (type_die, decl);
      if (DECL_ORIGINAL_TYPE (decl))
	{
	  type = DECL_ORIGINAL_TYPE (decl);

	  gcc_assert (type != TREE_TYPE (decl));
	  equate_type_number_to_die (TREE_TYPE (decl), type_die);
	}
      else
	type = TREE_TYPE (decl);

      add_type_attribute (type_die, type, TREE_READONLY (decl),
			  TREE_THIS_VOLATILE (decl), context_die);
    }

  if (DECL_ABSTRACT (decl))
    equate_decl_number_to_die (decl, type_die);
}

/* Generate a type description DIE.  */

static void
gen_type_die (tree type, dw_die_ref context_die)
{
  int need_pop;

  if (type == NULL_TREE || type == error_mark_node)
    return;

  if (TYPE_NAME (type) && TREE_CODE (TYPE_NAME (type)) == TYPE_DECL
      && DECL_ORIGINAL_TYPE (TYPE_NAME (type)))
    {
      if (TREE_ASM_WRITTEN (type))
	return;

      /* Prevent broken recursion; we can't hand off to the same type.  */
      gcc_assert (DECL_ORIGINAL_TYPE (TYPE_NAME (type)) != type);

      TREE_ASM_WRITTEN (type) = 1;
      gen_decl_die (TYPE_NAME (type), context_die);
      return;
    }

  /* We are going to output a DIE to represent the unqualified version
     of this type (i.e. without any const or volatile qualifiers) so
     get the main variant (i.e. the unqualified version) of this type
     now.  (Vectors are special because the debugging info is in the
     cloned type itself).  */
  if (TREE_CODE (type) != VECTOR_TYPE)
    type = type_main_variant (type);

  if (TREE_ASM_WRITTEN (type))
    return;

  switch (TREE_CODE (type))
    {
    case ERROR_MARK:
      break;

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      /* We must set TREE_ASM_WRITTEN in case this is a recursive type.  This
	 ensures that the gen_type_die recursion will terminate even if the
	 type is recursive.  Recursive types are possible in Ada.  */
      /* ??? We could perhaps do this for all types before the switch
	 statement.  */
      TREE_ASM_WRITTEN (type) = 1;

      /* For these types, all that is required is that we output a DIE (or a
	 set of DIEs) to represent the "basis" type.  */
      gen_type_die (TREE_TYPE (type), context_die);
      break;

    case OFFSET_TYPE:
      /* This code is used for C++ pointer-to-data-member types.
	 Output a description of the relevant class type.  */
      gen_type_die (TYPE_OFFSET_BASETYPE (type), context_die);

      /* Output a description of the type of the object pointed to.  */
      gen_type_die (TREE_TYPE (type), context_die);

      /* Now output a DIE to represent this pointer-to-data-member type
	 itself.  */
      gen_ptr_to_mbr_type_die (type, context_die);
      break;

    case FUNCTION_TYPE:
      /* Force out return type (in case it wasn't forced out already).  */
      gen_type_die (TREE_TYPE (type), context_die);
      gen_subroutine_type_die (type, context_die);
      break;

    case METHOD_TYPE:
      /* Force out return type (in case it wasn't forced out already).  */
      gen_type_die (TREE_TYPE (type), context_die);
      gen_subroutine_type_die (type, context_die);
      break;

    case ARRAY_TYPE:
      gen_array_type_die (type, context_die);
      break;

    case VECTOR_TYPE:
      gen_array_type_die (type, context_die);
      break;

    case ENUMERAL_TYPE:
    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      /* If this is a nested type whose containing class hasn't been written
	 out yet, writing it out will cover this one, too.  This does not apply
	 to instantiations of member class templates; they need to be added to
	 the containing class as they are generated.  FIXME: This hurts the
	 idea of combining type decls from multiple TUs, since we can't predict
	 what set of template instantiations we'll get.  */
      if (TYPE_CONTEXT (type)
	  && AGGREGATE_TYPE_P (TYPE_CONTEXT (type))
	  && ! TREE_ASM_WRITTEN (TYPE_CONTEXT (type)))
	{
	  gen_type_die (TYPE_CONTEXT (type), context_die);

	  if (TREE_ASM_WRITTEN (type))
	    return;

	  /* If that failed, attach ourselves to the stub.  */
	  push_decl_scope (TYPE_CONTEXT (type));
	  context_die = lookup_type_die (TYPE_CONTEXT (type));
	  need_pop = 1;
	}
      else
	{
	  declare_in_namespace (type, context_die);
	  need_pop = 0;
	}

      if (TREE_CODE (type) == ENUMERAL_TYPE)
	{
	  /* This might have been written out by the call to
	     declare_in_namespace.  */
	  if (!TREE_ASM_WRITTEN (type))
	    gen_enumeration_type_die (type, context_die);
	}
      else
	gen_struct_or_union_type_die (type, context_die);

      if (need_pop)
	pop_decl_scope ();

      /* Don't set TREE_ASM_WRITTEN on an incomplete struct; we want to fix
	 it up if it is ever completed.  gen_*_type_die will set it for us
	 when appropriate.  */
      return;

    case VOID_TYPE:
    case INTEGER_TYPE:
    case REAL_TYPE:
    case COMPLEX_TYPE:
    case BOOLEAN_TYPE:
      /* No DIEs needed for fundamental types.  */
      break;

    case LANG_TYPE:
      /* No Dwarf representation currently defined.  */
      break;

    default:
      gcc_unreachable ();
    }

  TREE_ASM_WRITTEN (type) = 1;
}

/* Generate a DIE for a tagged type instantiation.  */

static void
gen_tagged_type_instantiation_die (tree type, dw_die_ref context_die)
{
  if (type == NULL_TREE || type == error_mark_node)
    return;

  /* We are going to output a DIE to represent the unqualified version of
     this type (i.e. without any const or volatile qualifiers) so make sure
     that we have the main variant (i.e. the unqualified version) of this
     type now.  */
  gcc_assert (type == type_main_variant (type));

  /* Do not check TREE_ASM_WRITTEN (type) as it may not be set if this is
     an instance of an unresolved type.  */

  switch (TREE_CODE (type))
    {
    case ERROR_MARK:
      break;

    case ENUMERAL_TYPE:
      gen_inlined_enumeration_type_die (type, context_die);
      break;

    case RECORD_TYPE:
      gen_inlined_structure_type_die (type, context_die);
      break;

    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      gen_inlined_union_type_die (type, context_die);
      break;

    default:
      gcc_unreachable ();
    }
}

/* Generate a DW_TAG_lexical_block DIE followed by DIEs to represent all of the
   things which are local to the given block.  */

static void
gen_block_die (tree stmt, dw_die_ref context_die, int depth)
{
  int must_output_die = 0;
  tree origin;
  tree decl;
  enum tree_code origin_code;

  /* Ignore blocks that are NULL.  */
  if (stmt == NULL_TREE)
    return;

  /* If the block is one fragment of a non-contiguous block, do not
     process the variables, since they will have been done by the
     origin block.  Do process subblocks.  */
  if (BLOCK_FRAGMENT_ORIGIN (stmt))
    {
      tree sub;

      for (sub = BLOCK_SUBBLOCKS (stmt); sub; sub = BLOCK_CHAIN (sub))
	gen_block_die (sub, context_die, depth + 1);

      return;
    }

  /* Determine the "ultimate origin" of this block.  This block may be an
     inlined instance of an inlined instance of inline function, so we have
     to trace all of the way back through the origin chain to find out what
     sort of node actually served as the original seed for the creation of
     the current block.  */
  origin = block_ultimate_origin (stmt);
  origin_code = (origin != NULL) ? TREE_CODE (origin) : ERROR_MARK;

  /* Determine if we need to output any Dwarf DIEs at all to represent this
     block.  */
  if (origin_code == FUNCTION_DECL)
    /* The outer scopes for inlinings *must* always be represented.  We
       generate DW_TAG_inlined_subroutine DIEs for them.  (See below.) */
    must_output_die = 1;
  else
    {
      /* In the case where the current block represents an inlining of the
	 "body block" of an inline function, we must *NOT* output any DIE for
	 this block because we have already output a DIE to represent the whole
	 inlined function scope and the "body block" of any function doesn't
	 really represent a different scope according to ANSI C rules.  So we
	 check here to make sure that this block does not represent a "body
	 block inlining" before trying to set the MUST_OUTPUT_DIE flag.  */
      if (! is_body_block (origin ? origin : stmt))
	{
	  /* Determine if this block directly contains any "significant"
	     local declarations which we will need to output DIEs for.  */
	  if (debug_info_level > DINFO_LEVEL_TERSE)
	    /* We are not in terse mode so *any* local declaration counts
	       as being a "significant" one.  */
	    must_output_die = (BLOCK_VARS (stmt) != NULL 
			       && (TREE_USED (stmt) 
				   || TREE_ASM_WRITTEN (stmt)
				   || BLOCK_ABSTRACT (stmt)));
	  else
	    /* We are in terse mode, so only local (nested) function
	       definitions count as "significant" local declarations.  */
	    for (decl = BLOCK_VARS (stmt);
		 decl != NULL; decl = TREE_CHAIN (decl))
	      if (TREE_CODE (decl) == FUNCTION_DECL
		  && DECL_INITIAL (decl))
		{
		  must_output_die = 1;
		  break;
		}
	}
    }

  /* It would be a waste of space to generate a Dwarf DW_TAG_lexical_block
     DIE for any block which contains no significant local declarations at
     all.  Rather, in such cases we just call `decls_for_scope' so that any
     needed Dwarf info for any sub-blocks will get properly generated. Note
     that in terse mode, our definition of what constitutes a "significant"
     local declaration gets restricted to include only inlined function
     instances and local (nested) function definitions.  */
  if (must_output_die)
    {
      if (origin_code == FUNCTION_DECL)
	gen_inlined_subroutine_die (stmt, context_die, depth);
      else
	gen_lexical_block_die (stmt, context_die, depth);
    }
  else
    decls_for_scope (stmt, context_die, depth);
}

/* Generate all of the decls declared within a given scope and (recursively)
   all of its sub-blocks.  */

static void
decls_for_scope (tree stmt, dw_die_ref context_die, int depth)
{
  tree decl;
  tree subblocks;

  /* Ignore NULL blocks.  */
  if (stmt == NULL_TREE)
    return;

  if (TREE_USED (stmt))
    {
      /* Output the DIEs to represent all of the data objects and typedefs
	 declared directly within this block but not within any nested
	 sub-blocks.  Also, nested function and tag DIEs have been
	 generated with a parent of NULL; fix that up now.  */
      for (decl = BLOCK_VARS (stmt); decl != NULL; decl = TREE_CHAIN (decl))
	{
	  dw_die_ref die;
	  
	  if (TREE_CODE (decl) == FUNCTION_DECL)
	    die = lookup_decl_die (decl);
	  else if (TREE_CODE (decl) == TYPE_DECL && TYPE_DECL_IS_STUB (decl))
	    die = lookup_type_die (TREE_TYPE (decl));
	  else
	    die = NULL;
	  
	  if (die != NULL && die->die_parent == NULL)
	    add_child_die (context_die, die);
	  /* Do not produce debug information for static variables since
	     these might be optimized out.  We are called for these later
	     in cgraph_varpool_analyze_pending_decls. */
	  if (TREE_CODE (decl) == VAR_DECL && TREE_STATIC (decl))
	    ;
	  else
	    gen_decl_die (decl, context_die);
	}
    }

  /* If we're at -g1, we're not interested in subblocks.  */
  if (debug_info_level <= DINFO_LEVEL_TERSE)
    return;

  /* Output the DIEs to represent all sub-blocks (and the items declared
     therein) of this block.  */
  for (subblocks = BLOCK_SUBBLOCKS (stmt);
       subblocks != NULL;
       subblocks = BLOCK_CHAIN (subblocks))
    gen_block_die (subblocks, context_die, depth + 1);
}

/* Is this a typedef we can avoid emitting?  */

static inline int
is_redundant_typedef (tree decl)
{
  if (TYPE_DECL_IS_STUB (decl))
    return 1;

  if (DECL_ARTIFICIAL (decl)
      && DECL_CONTEXT (decl)
      && is_tagged_type (DECL_CONTEXT (decl))
      && TREE_CODE (TYPE_NAME (DECL_CONTEXT (decl))) == TYPE_DECL
      && DECL_NAME (decl) == DECL_NAME (TYPE_NAME (DECL_CONTEXT (decl))))
    /* Also ignore the artificial member typedef for the class name.  */
    return 1;

  return 0;
}

/* Returns the DIE for decl.  A DIE will always be returned.  */

static dw_die_ref
force_decl_die (tree decl)
{
  dw_die_ref decl_die;
  unsigned saved_external_flag;
  tree save_fn = NULL_TREE;
  decl_die = lookup_decl_die (decl);
  if (!decl_die)
    {
      dw_die_ref context_die;
      tree decl_context = DECL_CONTEXT (decl);
      if (decl_context)
	{
	  /* Find die that represents this context.  */
	  if (TYPE_P (decl_context))
	    context_die = force_type_die (decl_context);
	  else
	    context_die = force_decl_die (decl_context);
	}
      else
	context_die = comp_unit_die;

      decl_die = lookup_decl_die (decl);
      if (decl_die)
	return decl_die;

      switch (TREE_CODE (decl))
	{
	case FUNCTION_DECL:
	  /* Clear current_function_decl, so that gen_subprogram_die thinks
	     that this is a declaration. At this point, we just want to force
	     declaration die.  */
	  save_fn = current_function_decl;
	  current_function_decl = NULL_TREE;
	  gen_subprogram_die (decl, context_die);
	  current_function_decl = save_fn;
	  break;

	case VAR_DECL:
	  /* Set external flag to force declaration die. Restore it after
	   gen_decl_die() call.  */
	  saved_external_flag = DECL_EXTERNAL (decl);
	  DECL_EXTERNAL (decl) = 1;
	  gen_decl_die (decl, context_die);
	  DECL_EXTERNAL (decl) = saved_external_flag;
	  break;

	case NAMESPACE_DECL:
	  dwarf2out_decl (decl);
	  break;

	default:
	  gcc_unreachable ();
	}

      /* We should be able to find the DIE now.  */
      if (!decl_die)
	decl_die = lookup_decl_die (decl);
      gcc_assert (decl_die);
    }

  return decl_die;
}

/* Returns the DIE for TYPE, that must not be a base type.  A DIE is
   always returned.  */

static dw_die_ref
force_type_die (tree type)
{
  dw_die_ref type_die;

  type_die = lookup_type_die (type);
  if (!type_die)
    {
      dw_die_ref context_die;
      if (TYPE_CONTEXT (type))
	{
	  if (TYPE_P (TYPE_CONTEXT (type)))
	    context_die = force_type_die (TYPE_CONTEXT (type));
	  else
	    context_die = force_decl_die (TYPE_CONTEXT (type));
	}
      else
	context_die = comp_unit_die;

      type_die = lookup_type_die (type);
      if (type_die)
	return type_die;
      gen_type_die (type, context_die);
      type_die = lookup_type_die (type);
      gcc_assert (type_die);
    }
  return type_die;
}

/* Force out any required namespaces to be able to output DECL,
   and return the new context_die for it, if it's changed.  */

static dw_die_ref
setup_namespace_context (tree thing, dw_die_ref context_die)
{
  tree context = (DECL_P (thing)
		  ? DECL_CONTEXT (thing) : TYPE_CONTEXT (thing));
  if (context && TREE_CODE (context) == NAMESPACE_DECL)
    /* Force out the namespace.  */
    context_die = force_decl_die (context);

  return context_die;
}

/* Emit a declaration DIE for THING (which is either a DECL or a tagged
   type) within its namespace, if appropriate.

   For compatibility with older debuggers, namespace DIEs only contain
   declarations; all definitions are emitted at CU scope.  */

static void
declare_in_namespace (tree thing, dw_die_ref context_die)
{
  dw_die_ref ns_context;

  if (debug_info_level <= DINFO_LEVEL_TERSE)
    return;

  /* If this decl is from an inlined function, then don't try to emit it in its
     namespace, as we will get confused.  It would have already been emitted
     when the abstract instance of the inline function was emitted anyways.  */
  if (DECL_P (thing) && DECL_ABSTRACT_ORIGIN (thing))
    return;

  ns_context = setup_namespace_context (thing, context_die);

  if (ns_context != context_die)
    {
      if (DECL_P (thing))
	gen_decl_die (thing, ns_context);
      else
	gen_type_die (thing, ns_context);
    }
}

/* Generate a DIE for a namespace or namespace alias.  */

static void
gen_namespace_die (tree decl)
{
  dw_die_ref context_die = setup_namespace_context (decl, comp_unit_die);

  /* Namespace aliases have a DECL_ABSTRACT_ORIGIN of the namespace
     they are an alias of.  */
  if (DECL_ABSTRACT_ORIGIN (decl) == NULL)
    {
      /* Output a real namespace.  */
      dw_die_ref namespace_die
	= new_die (DW_TAG_namespace, context_die, decl);
      add_name_and_src_coords_attributes (namespace_die, decl);
      equate_decl_number_to_die (decl, namespace_die);
    }
  else
    {
      /* Output a namespace alias.  */

      /* Force out the namespace we are an alias of, if necessary.  */
      dw_die_ref origin_die
	= force_decl_die (DECL_ABSTRACT_ORIGIN (decl));

      /* Now create the namespace alias DIE.  */
      dw_die_ref namespace_die
	= new_die (DW_TAG_imported_declaration, context_die, decl);
      add_name_and_src_coords_attributes (namespace_die, decl);
      add_AT_die_ref (namespace_die, DW_AT_import, origin_die);
      equate_decl_number_to_die (decl, namespace_die);
    }
}

/* Generate Dwarf debug information for a decl described by DECL.  */

static void
gen_decl_die (tree decl, dw_die_ref context_die)
{
  tree origin;

  if (DECL_P (decl) && DECL_IGNORED_P (decl))
    return;

  switch (TREE_CODE (decl))
    {
    case ERROR_MARK:
      break;

    case CONST_DECL:
      /* The individual enumerators of an enum type get output when we output
	 the Dwarf representation of the relevant enum type itself.  */
      break;

    case FUNCTION_DECL:
      /* Don't output any DIEs to represent mere function declarations,
	 unless they are class members or explicit block externs.  */
      if (DECL_INITIAL (decl) == NULL_TREE && DECL_CONTEXT (decl) == NULL_TREE
	  && (current_function_decl == NULL_TREE || DECL_ARTIFICIAL (decl)))
	break;

#if 0
      /* FIXME */
      /* This doesn't work because the C frontend sets DECL_ABSTRACT_ORIGIN
	 on local redeclarations of global functions.  That seems broken.  */
      if (current_function_decl != decl)
	/* This is only a declaration.  */;
#endif

      /* If we're emitting a clone, emit info for the abstract instance.  */
      if (DECL_ORIGIN (decl) != decl)
	dwarf2out_abstract_function (DECL_ABSTRACT_ORIGIN (decl));

      /* If we're emitting an out-of-line copy of an inline function,
	 emit info for the abstract instance and set up to refer to it.  */
      else if (cgraph_function_possibly_inlined_p (decl)
	       && ! DECL_ABSTRACT (decl)
	       && ! class_or_namespace_scope_p (context_die)
	       /* dwarf2out_abstract_function won't emit a die if this is just
		  a declaration.  We must avoid setting DECL_ABSTRACT_ORIGIN in
		  that case, because that works only if we have a die.  */
	       && DECL_INITIAL (decl) != NULL_TREE)
	{
	  dwarf2out_abstract_function (decl);
	  set_decl_origin_self (decl);
	}

      /* Otherwise we're emitting the primary DIE for this decl.  */
      else if (debug_info_level > DINFO_LEVEL_TERSE)
	{
	  /* Before we describe the FUNCTION_DECL itself, make sure that we
	     have described its return type.  */
	  gen_type_die (TREE_TYPE (TREE_TYPE (decl)), context_die);

	  /* And its virtual context.  */
	  if (DECL_VINDEX (decl) != NULL_TREE)
	    gen_type_die (DECL_CONTEXT (decl), context_die);

	  /* And its containing type.  */
	  origin = decl_class_context (decl);
	  if (origin != NULL_TREE)
	    gen_type_die_for_member (origin, decl, context_die);

	  /* And its containing namespace.  */
	  declare_in_namespace (decl, context_die);
	}

      /* Now output a DIE to represent the function itself.  */
      gen_subprogram_die (decl, context_die);
      break;

    case TYPE_DECL:
      /* If we are in terse mode, don't generate any DIEs to represent any
	 actual typedefs.  */
      if (debug_info_level <= DINFO_LEVEL_TERSE)
	break;

      /* In the special case of a TYPE_DECL node representing the declaration
	 of some type tag, if the given TYPE_DECL is marked as having been
	 instantiated from some other (original) TYPE_DECL node (e.g. one which
	 was generated within the original definition of an inline function) we
	 have to generate a special (abbreviated) DW_TAG_structure_type,
	 DW_TAG_union_type, or DW_TAG_enumeration_type DIE here.  */
      if (TYPE_DECL_IS_STUB (decl) && decl_ultimate_origin (decl) != NULL_TREE)
	{
	  gen_tagged_type_instantiation_die (TREE_TYPE (decl), context_die);
	  break;
	}

      if (is_redundant_typedef (decl))
	gen_type_die (TREE_TYPE (decl), context_die);
      else
	/* Output a DIE to represent the typedef itself.  */
	gen_typedef_die (decl, context_die);
      break;

    case LABEL_DECL:
      if (debug_info_level >= DINFO_LEVEL_NORMAL)
	gen_label_die (decl, context_die);
      break;

    case VAR_DECL:
    case RESULT_DECL:
      /* If we are in terse mode, don't generate any DIEs to represent any
	 variable declarations or definitions.  */
      if (debug_info_level <= DINFO_LEVEL_TERSE)
	break;

      /* Output any DIEs that are needed to specify the type of this data
	 object.  */
      gen_type_die (TREE_TYPE (decl), context_die);

      /* And its containing type.  */
      origin = decl_class_context (decl);
      if (origin != NULL_TREE)
	gen_type_die_for_member (origin, decl, context_die);

      /* And its containing namespace.  */
      declare_in_namespace (decl, context_die);

      /* Now output the DIE to represent the data object itself.  This gets
	 complicated because of the possibility that the VAR_DECL really
	 represents an inlined instance of a formal parameter for an inline
	 function.  */
      origin = decl_ultimate_origin (decl);
      if (origin != NULL_TREE && TREE_CODE (origin) == PARM_DECL)
	gen_formal_parameter_die (decl, context_die);
      else
	gen_variable_die (decl, context_die);
      break;

    case FIELD_DECL:
      /* Ignore the nameless fields that are used to skip bits but handle C++
	 anonymous unions and structs.  */
      if (DECL_NAME (decl) != NULL_TREE
	  || TREE_CODE (TREE_TYPE (decl)) == UNION_TYPE
	  || TREE_CODE (TREE_TYPE (decl)) == RECORD_TYPE)
	{
	  gen_type_die (member_declared_type (decl), context_die);
	  gen_field_die (decl, context_die);
	}
      break;

    case PARM_DECL:
      gen_type_die (TREE_TYPE (decl), context_die);
      gen_formal_parameter_die (decl, context_die);
      break;

    case NAMESPACE_DECL:
      gen_namespace_die (decl);
      break;

    default:
      /* Probably some frontend-internal decl.  Assume we don't care.  */
      gcc_assert ((int)TREE_CODE (decl) > NUM_TREE_CODES);
      break;
    }
}

/* Output debug information for global decl DECL.  Called from toplev.c after
   compilation proper has finished.  */

static void
dwarf2out_global_decl (tree decl)
{
  /* Output DWARF2 information for file-scope tentative data object
     declarations, file-scope (extern) function declarations (which had no
     corresponding body) and file-scope tagged type declarations and
     definitions which have not yet been forced out.  */
  if (TREE_CODE (decl) != FUNCTION_DECL || !DECL_INITIAL (decl))
    dwarf2out_decl (decl);
}

/* Output debug information for type decl DECL.  Called from toplev.c
   and from language front ends (to record built-in types).  */
static void
dwarf2out_type_decl (tree decl, int local)
{
  if (!local)
    dwarf2out_decl (decl);
}

/* Output debug information for imported module or decl.  */

static void
dwarf2out_imported_module_or_decl (tree decl, tree context)
{
  dw_die_ref imported_die, at_import_die;
  dw_die_ref scope_die;
  expanded_location xloc;

  if (debug_info_level <= DINFO_LEVEL_TERSE)
    return;

  gcc_assert (decl);

  /* To emit DW_TAG_imported_module or DW_TAG_imported_decl, we need two DIEs.
     We need decl DIE for reference and scope die. First, get DIE for the decl
     itself.  */

  /* Get the scope die for decl context. Use comp_unit_die for global module
     or decl. If die is not found for non globals, force new die.  */
  if (!context)
    scope_die = comp_unit_die;
  else if (TYPE_P (context))
    scope_die = force_type_die (context);
  else
    scope_die = force_decl_die (context);

  /* For TYPE_DECL or CONST_DECL, lookup TREE_TYPE.  */
  if (TREE_CODE (decl) == TYPE_DECL || TREE_CODE (decl) == CONST_DECL)
    {
      if (is_base_type (TREE_TYPE (decl)))
	at_import_die = base_type_die (TREE_TYPE (decl));
      else
	at_import_die = force_type_die (TREE_TYPE (decl));
    }
  else
    {
      at_import_die = lookup_decl_die (decl);
      if (!at_import_die)
	{
	  /* If we're trying to avoid duplicate debug info, we may not have
	     emitted the member decl for this field.  Emit it now.  */
	  if (TREE_CODE (decl) == FIELD_DECL)
	    {
	      tree type = DECL_CONTEXT (decl);
	      dw_die_ref type_context_die;

	      if (TYPE_CONTEXT (type))
		if (TYPE_P (TYPE_CONTEXT (type)))
		  type_context_die = force_type_die (TYPE_CONTEXT (type));
	      else
		type_context_die = force_decl_die (TYPE_CONTEXT (type));
	      else
		type_context_die = comp_unit_die;
	      gen_type_die_for_member (type, decl, type_context_die);
	    }
	  at_import_die = force_decl_die (decl);
	}
    }

  /* OK, now we have DIEs for decl as well as scope. Emit imported die.  */
  if (TREE_CODE (decl) == NAMESPACE_DECL)
    imported_die = new_die (DW_TAG_imported_module, scope_die, context);
  else
    imported_die = new_die (DW_TAG_imported_declaration, scope_die, context);

  xloc = expand_location (input_location);
  add_AT_file (imported_die, DW_AT_decl_file, lookup_filename (xloc.file));
  add_AT_unsigned (imported_die, DW_AT_decl_line, xloc.line);
  add_AT_die_ref (imported_die, DW_AT_import, at_import_die);
}

/* Write the debugging output for DECL.  */

void
dwarf2out_decl (tree decl)
{
  dw_die_ref context_die = comp_unit_die;

  switch (TREE_CODE (decl))
    {
    case ERROR_MARK:
      return;

    case FUNCTION_DECL:
      /* What we would really like to do here is to filter out all mere
	 file-scope declarations of file-scope functions which are never
	 referenced later within this translation unit (and keep all of ones
	 that *are* referenced later on) but we aren't clairvoyant, so we have
	 no idea which functions will be referenced in the future (i.e. later
	 on within the current translation unit). So here we just ignore all
	 file-scope function declarations which are not also definitions.  If
	 and when the debugger needs to know something about these functions,
	 it will have to hunt around and find the DWARF information associated
	 with the definition of the function.

	 We can't just check DECL_EXTERNAL to find out which FUNCTION_DECL
	 nodes represent definitions and which ones represent mere
	 declarations.  We have to check DECL_INITIAL instead. That's because
	 the C front-end supports some weird semantics for "extern inline"
	 function definitions.  These can get inlined within the current
	 translation unit (and thus, we need to generate Dwarf info for their
	 abstract instances so that the Dwarf info for the concrete inlined
	 instances can have something to refer to) but the compiler never
	 generates any out-of-lines instances of such things (despite the fact
	 that they *are* definitions).

	 The important point is that the C front-end marks these "extern
	 inline" functions as DECL_EXTERNAL, but we need to generate DWARF for
	 them anyway. Note that the C++ front-end also plays some similar games
	 for inline function definitions appearing within include files which
	 also contain `#pragma interface' pragmas.  */
      if (DECL_INITIAL (decl) == NULL_TREE)
	return;

      /* If we're a nested function, initially use a parent of NULL; if we're
	 a plain function, this will be fixed up in decls_for_scope.  If
	 we're a method, it will be ignored, since we already have a DIE.  */
      if (decl_function_context (decl)
	  /* But if we're in terse mode, we don't care about scope.  */
	  && debug_info_level > DINFO_LEVEL_TERSE)
	context_die = NULL;
      break;

    case VAR_DECL:
      /* Ignore this VAR_DECL if it refers to a file-scope extern data object
	 declaration and if the declaration was never even referenced from
	 within this entire compilation unit.  We suppress these DIEs in
	 order to save space in the .debug section (by eliminating entries
	 which are probably useless).  Note that we must not suppress
	 block-local extern declarations (whether used or not) because that
	 would screw-up the debugger's name lookup mechanism and cause it to
	 miss things which really ought to be in scope at a given point.  */
      if (DECL_EXTERNAL (decl) && !TREE_USED (decl))
	return;

      /* For local statics lookup proper context die.  */
      if (TREE_STATIC (decl) && decl_function_context (decl))
	context_die = lookup_decl_die (DECL_CONTEXT (decl));

      /* If we are in terse mode, don't generate any DIEs to represent any
	 variable declarations or definitions.  */
      if (debug_info_level <= DINFO_LEVEL_TERSE)
	return;
      break;

    case NAMESPACE_DECL:
      if (debug_info_level <= DINFO_LEVEL_TERSE)
	return;
      if (lookup_decl_die (decl) != NULL)
        return;
      break;

    case TYPE_DECL:
      /* Don't emit stubs for types unless they are needed by other DIEs.  */
      if (TYPE_DECL_SUPPRESS_DEBUG (decl))
	return;

      /* Don't bother trying to generate any DIEs to represent any of the
	 normal built-in types for the language we are compiling.  */
      if (DECL_IS_BUILTIN (decl))
	{
	  /* OK, we need to generate one for `bool' so GDB knows what type
	     comparisons have.  */
	  if (is_cxx ()
	      && TREE_CODE (TREE_TYPE (decl)) == BOOLEAN_TYPE
	      && ! DECL_IGNORED_P (decl))
	    modified_type_die (TREE_TYPE (decl), 0, 0, NULL);

	  return;
	}

      /* If we are in terse mode, don't generate any DIEs for types.  */
      if (debug_info_level <= DINFO_LEVEL_TERSE)
	return;

      /* If we're a function-scope tag, initially use a parent of NULL;
	 this will be fixed up in decls_for_scope.  */
      if (decl_function_context (decl))
	context_die = NULL;

      break;

    default:
      return;
    }

  gen_decl_die (decl, context_die);
}

/* Output a marker (i.e. a label) for the beginning of the generated code for
   a lexical block.  */

static void
dwarf2out_begin_block (unsigned int line ATTRIBUTE_UNUSED,
		       unsigned int blocknum)
{
  switch_to_section (current_function_section ());
  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, BLOCK_BEGIN_LABEL, blocknum);
}

/* Output a marker (i.e. a label) for the end of the generated code for a
   lexical block.  */

static void
dwarf2out_end_block (unsigned int line ATTRIBUTE_UNUSED, unsigned int blocknum)
{
  switch_to_section (current_function_section ());
  ASM_OUTPUT_DEBUG_LABEL (asm_out_file, BLOCK_END_LABEL, blocknum);
}

/* Returns nonzero if it is appropriate not to emit any debugging
   information for BLOCK, because it doesn't contain any instructions.

   Don't allow this for blocks with nested functions or local classes
   as we would end up with orphans, and in the presence of scheduling
   we may end up calling them anyway.  */

static bool
dwarf2out_ignore_block (tree block)
{
  tree decl;

  for (decl = BLOCK_VARS (block); decl; decl = TREE_CHAIN (decl))
    if (TREE_CODE (decl) == FUNCTION_DECL
	|| (TREE_CODE (decl) == TYPE_DECL && TYPE_DECL_IS_STUB (decl)))
      return 0;

  return 1;
}

/* Hash table routines for file_hash.  */

static int
file_table_eq (const void *p1_p, const void *p2_p)
{
  const struct dwarf_file_data * p1 = p1_p;
  const char * p2 = p2_p;
  return strcmp (p1->filename, p2) == 0;
}

static hashval_t
file_table_hash (const void *p_p)
{
  const struct dwarf_file_data * p = p_p;
  return htab_hash_string (p->filename);
}

/* Lookup FILE_NAME (in the list of filenames that we know about here in
   dwarf2out.c) and return its "index".  The index of each (known) filename is
   just a unique number which is associated with only that one filename.  We
   need such numbers for the sake of generating labels (in the .debug_sfnames
   section) and references to those files numbers (in the .debug_srcinfo
   and.debug_macinfo sections).  If the filename given as an argument is not
   found in our current list, add it to the list and assign it the next
   available unique index number.  In order to speed up searches, we remember
   the index of the filename was looked up last.  This handles the majority of
   all searches.  */

static struct dwarf_file_data *
lookup_filename (const char *file_name)
{
  void ** slot;
  struct dwarf_file_data * created;

  /* Check to see if the file name that was searched on the previous
     call matches this file name.  If so, return the index.  */
  if (file_table_last_lookup
      && (file_name == file_table_last_lookup->filename
	  || strcmp (file_table_last_lookup->filename, file_name) == 0))
    return file_table_last_lookup;

  /* Didn't match the previous lookup, search the table.  */
  slot = htab_find_slot_with_hash (file_table, file_name,
				   htab_hash_string (file_name), INSERT);
  if (*slot)
    return *slot;

  created = ggc_alloc (sizeof (struct dwarf_file_data));
  created->filename = file_name;
  created->emitted_number = 0;
  *slot = created;
  return created;
}

/* If the assembler will construct the file table, then translate the compiler
   internal file table number into the assembler file table number, and emit
   a .file directive if we haven't already emitted one yet.  The file table
   numbers are different because we prune debug info for unused variables and
   types, which may include filenames.  */

static int
maybe_emit_file (struct dwarf_file_data * fd)
{
  if (! fd->emitted_number)
    {
      if (last_emitted_file)
	fd->emitted_number = last_emitted_file->emitted_number + 1;
      else
	fd->emitted_number = 1;
      last_emitted_file = fd;
      
      if (DWARF2_ASM_LINE_DEBUG_INFO)
	{
	  fprintf (asm_out_file, "\t.file %u ", fd->emitted_number);
	  output_quoted_string (asm_out_file, fd->filename);
	  fputc ('\n', asm_out_file);
	}
    }
  
  return fd->emitted_number;
}

/* Called by the final INSN scan whenever we see a var location.  We
   use it to drop labels in the right places, and throw the location in
   our lookup table.  */

static void
dwarf2out_var_location (rtx loc_note)
{
  char loclabel[MAX_ARTIFICIAL_LABEL_BYTES];
  struct var_loc_node *newloc;
  rtx prev_insn;
  static rtx last_insn;
  static const char *last_label;
  tree decl;

  if (!DECL_P (NOTE_VAR_LOCATION_DECL (loc_note)))
    return;
  prev_insn = PREV_INSN (loc_note);

  newloc = ggc_alloc_cleared (sizeof (struct var_loc_node));
  /* If the insn we processed last time is the previous insn
     and it is also a var location note, use the label we emitted
     last time.  */
  if (last_insn != NULL_RTX
      && last_insn == prev_insn
      && NOTE_P (prev_insn)
      && NOTE_LINE_NUMBER (prev_insn) == NOTE_INSN_VAR_LOCATION)
    {
      newloc->label = last_label;
    }
  else
    {
      ASM_GENERATE_INTERNAL_LABEL (loclabel, "LVL", loclabel_num);
      ASM_OUTPUT_DEBUG_LABEL (asm_out_file, "LVL", loclabel_num);
      loclabel_num++;
      newloc->label = ggc_strdup (loclabel);
    }
  newloc->var_loc_note = loc_note;
  newloc->next = NULL;

  if (cfun && in_cold_section_p)
    newloc->section_label = cfun->cold_section_label;
  else
    newloc->section_label = text_section_label;

  last_insn = loc_note;
  last_label = newloc->label;
  decl = NOTE_VAR_LOCATION_DECL (loc_note);
  add_var_loc_to_decl (decl, newloc);
}

/* We need to reset the locations at the beginning of each
   function. We can't do this in the end_function hook, because the
   declarations that use the locations won't have been output when
   that hook is called.  Also compute have_multiple_function_sections here.  */

static void
dwarf2out_begin_function (tree fun)
{
  htab_empty (decl_loc_table);
  
  if (function_section (fun) != text_section)
    have_multiple_function_sections = true;
}

/* Output a label to mark the beginning of a source code line entry
   and record information relating to this source line, in
   'line_info_table' for later output of the .debug_line section.  */

static void
dwarf2out_source_line (unsigned int line, const char *filename)
{
  if (debug_info_level >= DINFO_LEVEL_NORMAL
      && line != 0)
    {
      int file_num = maybe_emit_file (lookup_filename (filename));
      
      switch_to_section (current_function_section ());

      /* If requested, emit something human-readable.  */
      if (flag_debug_asm)
	fprintf (asm_out_file, "\t%s %s:%d\n", ASM_COMMENT_START,
		 filename, line);

      if (DWARF2_ASM_LINE_DEBUG_INFO)
	{
	  /* Emit the .loc directive understood by GNU as.  */
	  fprintf (asm_out_file, "\t.loc %d %d 0\n", file_num, line);

	  /* Indicate that line number info exists.  */
	  line_info_table_in_use++;
	}
      else if (function_section (current_function_decl) != text_section)
	{
	  dw_separate_line_info_ref line_info;
	  targetm.asm_out.internal_label (asm_out_file, 
					  SEPARATE_LINE_CODE_LABEL,
					  separate_line_info_table_in_use);

	  /* Expand the line info table if necessary.  */
	  if (separate_line_info_table_in_use
	      == separate_line_info_table_allocated)
	    {
	      separate_line_info_table_allocated += LINE_INFO_TABLE_INCREMENT;
	      separate_line_info_table
		= ggc_realloc (separate_line_info_table,
			       separate_line_info_table_allocated
			       * sizeof (dw_separate_line_info_entry));
	      memset (separate_line_info_table
		       + separate_line_info_table_in_use,
		      0,
		      (LINE_INFO_TABLE_INCREMENT
		       * sizeof (dw_separate_line_info_entry)));
	    }

	  /* Add the new entry at the end of the line_info_table.  */
	  line_info
	    = &separate_line_info_table[separate_line_info_table_in_use++];
	  line_info->dw_file_num = file_num;
	  line_info->dw_line_num = line;
	  line_info->function = current_function_funcdef_no;
	}
      else
	{
	  dw_line_info_ref line_info;

	  targetm.asm_out.internal_label (asm_out_file, LINE_CODE_LABEL,
				     line_info_table_in_use);

	  /* Expand the line info table if necessary.  */
	  if (line_info_table_in_use == line_info_table_allocated)
	    {
	      line_info_table_allocated += LINE_INFO_TABLE_INCREMENT;
	      line_info_table
		= ggc_realloc (line_info_table,
			       (line_info_table_allocated
				* sizeof (dw_line_info_entry)));
	      memset (line_info_table + line_info_table_in_use, 0,
		      LINE_INFO_TABLE_INCREMENT * sizeof (dw_line_info_entry));
	    }

	  /* Add the new entry at the end of the line_info_table.  */
	  line_info = &line_info_table[line_info_table_in_use++];
	  line_info->dw_file_num = file_num;
	  line_info->dw_line_num = line;
	}
    }
}

/* Record the beginning of a new source file.  */

static void
dwarf2out_start_source_file (unsigned int lineno, const char *filename)
{
  if (flag_eliminate_dwarf2_dups)
    {
      /* Record the beginning of the file for break_out_includes.  */
      dw_die_ref bincl_die;

      bincl_die = new_die (DW_TAG_GNU_BINCL, comp_unit_die, NULL);
      add_AT_string (bincl_die, DW_AT_name, filename);
    }

  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    {
      int file_num = maybe_emit_file (lookup_filename (filename));

      switch_to_section (debug_macinfo_section);
      dw2_asm_output_data (1, DW_MACINFO_start_file, "Start new file");
      dw2_asm_output_data_uleb128 (lineno, "Included from line number %d",
				   lineno);

      dw2_asm_output_data_uleb128 (file_num, "file %s", filename);
    }
}

/* Record the end of a source file.  */

static void
dwarf2out_end_source_file (unsigned int lineno ATTRIBUTE_UNUSED)
{
  if (flag_eliminate_dwarf2_dups)
    /* Record the end of the file for break_out_includes.  */
    new_die (DW_TAG_GNU_EINCL, comp_unit_die, NULL);

  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    {
      switch_to_section (debug_macinfo_section);
      dw2_asm_output_data (1, DW_MACINFO_end_file, "End file");
    }
}

/* Called from debug_define in toplev.c.  The `buffer' parameter contains
   the tail part of the directive line, i.e. the part which is past the
   initial whitespace, #, whitespace, directive-name, whitespace part.  */

static void
dwarf2out_define (unsigned int lineno ATTRIBUTE_UNUSED,
		  const char *buffer ATTRIBUTE_UNUSED)
{
  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    {
      switch_to_section (debug_macinfo_section);
      dw2_asm_output_data (1, DW_MACINFO_define, "Define macro");
      dw2_asm_output_data_uleb128 (lineno, "At line number %d", lineno);
      dw2_asm_output_nstring (buffer, -1, "The macro");
    }
}

/* Called from debug_undef in toplev.c.  The `buffer' parameter contains
   the tail part of the directive line, i.e. the part which is past the
   initial whitespace, #, whitespace, directive-name, whitespace part.  */

static void
dwarf2out_undef (unsigned int lineno ATTRIBUTE_UNUSED,
		 const char *buffer ATTRIBUTE_UNUSED)
{
  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    {
      switch_to_section (debug_macinfo_section);
      dw2_asm_output_data (1, DW_MACINFO_undef, "Undefine macro");
      dw2_asm_output_data_uleb128 (lineno, "At line number %d", lineno);
      dw2_asm_output_nstring (buffer, -1, "The macro");
    }
}

/* Set up for Dwarf output at the start of compilation.  */

static void
dwarf2out_init (const char *filename ATTRIBUTE_UNUSED)
{
  /* Allocate the file_table.  */
  file_table = htab_create_ggc (50, file_table_hash,
				file_table_eq, NULL);

  /* Allocate the decl_die_table.  */
  decl_die_table = htab_create_ggc (10, decl_die_table_hash,
				    decl_die_table_eq, NULL);

  /* Allocate the decl_loc_table.  */
  decl_loc_table = htab_create_ggc (10, decl_loc_table_hash,
				    decl_loc_table_eq, NULL);

  /* Allocate the initial hunk of the decl_scope_table.  */
  decl_scope_table = VEC_alloc (tree, gc, 256);

  /* Allocate the initial hunk of the abbrev_die_table.  */
  abbrev_die_table = ggc_alloc_cleared (ABBREV_DIE_TABLE_INCREMENT
					* sizeof (dw_die_ref));
  abbrev_die_table_allocated = ABBREV_DIE_TABLE_INCREMENT;
  /* Zero-th entry is allocated, but unused.  */
  abbrev_die_table_in_use = 1;

  /* Allocate the initial hunk of the line_info_table.  */
  line_info_table = ggc_alloc_cleared (LINE_INFO_TABLE_INCREMENT
				       * sizeof (dw_line_info_entry));
  line_info_table_allocated = LINE_INFO_TABLE_INCREMENT;

  /* Zero-th entry is allocated, but unused.  */
  line_info_table_in_use = 1;

  /* Generate the initial DIE for the .debug section.  Note that the (string)
     value given in the DW_AT_name attribute of the DW_TAG_compile_unit DIE
     will (typically) be a relative pathname and that this pathname should be
     taken as being relative to the directory from which the compiler was
     invoked when the given (base) source file was compiled.  We will fill
     in this value in dwarf2out_finish.  */
  comp_unit_die = gen_compile_unit_die (NULL);

  incomplete_types = VEC_alloc (tree, gc, 64);

  used_rtx_array = VEC_alloc (rtx, gc, 32);

  debug_info_section = get_section (DEBUG_INFO_SECTION,
				    SECTION_DEBUG, NULL);
  debug_abbrev_section = get_section (DEBUG_ABBREV_SECTION,
				      SECTION_DEBUG, NULL);
  debug_aranges_section = get_section (DEBUG_ARANGES_SECTION,
				       SECTION_DEBUG, NULL);
  debug_macinfo_section = get_section (DEBUG_MACINFO_SECTION,
				       SECTION_DEBUG, NULL);
  debug_line_section = get_section (DEBUG_LINE_SECTION,
				    SECTION_DEBUG, NULL);
  debug_loc_section = get_section (DEBUG_LOC_SECTION,
				   SECTION_DEBUG, NULL);
  debug_pubnames_section = get_section (DEBUG_PUBNAMES_SECTION,
					SECTION_DEBUG, NULL);
  debug_str_section = get_section (DEBUG_STR_SECTION,
				   DEBUG_STR_SECTION_FLAGS, NULL);
  debug_ranges_section = get_section (DEBUG_RANGES_SECTION,
				      SECTION_DEBUG, NULL);
  debug_frame_section = get_section (DEBUG_FRAME_SECTION,
				     SECTION_DEBUG, NULL);

  ASM_GENERATE_INTERNAL_LABEL (text_end_label, TEXT_END_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (abbrev_section_label,
			       DEBUG_ABBREV_SECTION_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (text_section_label, TEXT_SECTION_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (cold_text_section_label, 
			       COLD_TEXT_SECTION_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (cold_end_label, COLD_END_LABEL, 0);

  ASM_GENERATE_INTERNAL_LABEL (debug_info_section_label,
			       DEBUG_INFO_SECTION_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (debug_line_section_label,
			       DEBUG_LINE_SECTION_LABEL, 0);
  ASM_GENERATE_INTERNAL_LABEL (ranges_section_label,
			       DEBUG_RANGES_SECTION_LABEL, 0);
  switch_to_section (debug_abbrev_section);
  ASM_OUTPUT_LABEL (asm_out_file, abbrev_section_label);
  switch_to_section (debug_info_section);
  ASM_OUTPUT_LABEL (asm_out_file, debug_info_section_label);
  switch_to_section (debug_line_section);
  ASM_OUTPUT_LABEL (asm_out_file, debug_line_section_label);

  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    {
      switch_to_section (debug_macinfo_section);
      ASM_GENERATE_INTERNAL_LABEL (macinfo_section_label,
				   DEBUG_MACINFO_SECTION_LABEL, 0);
      ASM_OUTPUT_LABEL (asm_out_file, macinfo_section_label);
    }

  switch_to_section (text_section);
  ASM_OUTPUT_LABEL (asm_out_file, text_section_label);
  if (flag_reorder_blocks_and_partition)
    {
      switch_to_section (unlikely_text_section ());
      ASM_OUTPUT_LABEL (asm_out_file, cold_text_section_label);
    }
}

/* A helper function for dwarf2out_finish called through
   ht_forall.  Emit one queued .debug_str string.  */

static int
output_indirect_string (void **h, void *v ATTRIBUTE_UNUSED)
{
  struct indirect_string_node *node = (struct indirect_string_node *) *h;

  if (node->form == DW_FORM_strp)
    {
      switch_to_section (debug_str_section);
      ASM_OUTPUT_LABEL (asm_out_file, node->label);
      assemble_string (node->str, strlen (node->str) + 1);
    }

  return 1;
}

#if ENABLE_ASSERT_CHECKING
/* Verify that all marks are clear.  */

static void
verify_marks_clear (dw_die_ref die)
{
  dw_die_ref c;
  
  gcc_assert (! die->die_mark);
  FOR_EACH_CHILD (die, c, verify_marks_clear (c));
}
#endif /* ENABLE_ASSERT_CHECKING */

/* Clear the marks for a die and its children.
   Be cool if the mark isn't set.  */

static void
prune_unmark_dies (dw_die_ref die)
{
  dw_die_ref c;
  
  if (die->die_mark)
    die->die_mark = 0;
  FOR_EACH_CHILD (die, c, prune_unmark_dies (c));
}

/* Given DIE that we're marking as used, find any other dies
   it references as attributes and mark them as used.  */

static void
prune_unused_types_walk_attribs (dw_die_ref die)
{
  dw_attr_ref a;
  unsigned ix;

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    {
      if (a->dw_attr_val.val_class == dw_val_class_die_ref)
	{
	  /* A reference to another DIE.
	     Make sure that it will get emitted.  */
	  prune_unused_types_mark (a->dw_attr_val.v.val_die_ref.die, 1);
	}
      /* Set the string's refcount to 0 so that prune_unused_types_mark
	 accounts properly for it.  */
      if (AT_class (a) == dw_val_class_str)
	a->dw_attr_val.v.val_str->refcount = 0;
    }
}


/* Mark DIE as being used.  If DOKIDS is true, then walk down
   to DIE's children.  */

static void
prune_unused_types_mark (dw_die_ref die, int dokids)
{
  dw_die_ref c;

  if (die->die_mark == 0)
    {
      /* We haven't done this node yet.  Mark it as used.  */
      die->die_mark = 1;

      /* We also have to mark its parents as used.
	 (But we don't want to mark our parents' kids due to this.)  */
      if (die->die_parent)
	prune_unused_types_mark (die->die_parent, 0);

      /* Mark any referenced nodes.  */
      prune_unused_types_walk_attribs (die);

      /* If this node is a specification,
         also mark the definition, if it exists.  */
      if (get_AT_flag (die, DW_AT_declaration) && die->die_definition)
        prune_unused_types_mark (die->die_definition, 1);
    }

  if (dokids && die->die_mark != 2)
    {
      /* We need to walk the children, but haven't done so yet.
	 Remember that we've walked the kids.  */
      die->die_mark = 2;

      /* If this is an array type, we need to make sure our
	 kids get marked, even if they're types.  */
      if (die->die_tag == DW_TAG_array_type)
	FOR_EACH_CHILD (die, c, prune_unused_types_mark (c, 1));
      else
	FOR_EACH_CHILD (die, c, prune_unused_types_walk (c));
    }
}


/* Walk the tree DIE and mark types that we actually use.  */

static void
prune_unused_types_walk (dw_die_ref die)
{
  dw_die_ref c;

  /* Don't do anything if this node is already marked.  */
  if (die->die_mark)
    return;

  switch (die->die_tag) {
  case DW_TAG_const_type:
  case DW_TAG_packed_type:
  case DW_TAG_pointer_type:
  case DW_TAG_reference_type:
  case DW_TAG_volatile_type:
  case DW_TAG_typedef:
  case DW_TAG_array_type:
  case DW_TAG_structure_type:
  case DW_TAG_union_type:
  case DW_TAG_class_type:
  case DW_TAG_friend:
  case DW_TAG_variant_part:
  case DW_TAG_enumeration_type:
  case DW_TAG_subroutine_type:
  case DW_TAG_string_type:
  case DW_TAG_set_type:
  case DW_TAG_subrange_type:
  case DW_TAG_ptr_to_member_type:
  case DW_TAG_file_type:
    if (die->die_perennial_p)
      break;

    /* It's a type node --- don't mark it.  */
    return;

  default:
    /* Mark everything else.  */
    break;
  }

  die->die_mark = 1;

  /* Now, mark any dies referenced from here.  */
  prune_unused_types_walk_attribs (die);

  /* Mark children.  */
  FOR_EACH_CHILD (die, c, prune_unused_types_walk (c));
}

/* Increment the string counts on strings referred to from DIE's
   attributes.  */

static void
prune_unused_types_update_strings (dw_die_ref die)
{
  dw_attr_ref a;
  unsigned ix;

  for (ix = 0; VEC_iterate (dw_attr_node, die->die_attr, ix, a); ix++)
    if (AT_class (a) == dw_val_class_str)
      {
	struct indirect_string_node *s = a->dw_attr_val.v.val_str;
	s->refcount++;
	/* Avoid unnecessarily putting strings that are used less than
	   twice in the hash table.  */
	if (s->refcount
	    == ((DEBUG_STR_SECTION_FLAGS & SECTION_MERGE) ? 1 : 2))
	  {
	    void ** slot;
	    slot = htab_find_slot_with_hash (debug_str_hash, s->str,
					     htab_hash_string (s->str),
					     INSERT);
	    gcc_assert (*slot == NULL);
	    *slot = s;
	  }
      }
}

/* Remove from the tree DIE any dies that aren't marked.  */

static void
prune_unused_types_prune (dw_die_ref die)
{
  dw_die_ref c;

  gcc_assert (die->die_mark);
  prune_unused_types_update_strings (die);

  if (! die->die_child)
    return;
  
  c = die->die_child;
  do {
    dw_die_ref prev = c;
    for (c = c->die_sib; ! c->die_mark; c = c->die_sib)
      if (c == die->die_child)
	{
	  /* No marked children between 'prev' and the end of the list.  */
	  if (prev == c)
	    /* No marked children at all.  */
	    die->die_child = NULL;
	  else
	    {
	      prev->die_sib = c->die_sib;
	      die->die_child = prev;
	    }
	  return;
	}

    if (c != prev->die_sib)
      prev->die_sib = c;
    prune_unused_types_prune (c);
  } while (c != die->die_child);
}


/* Remove dies representing declarations that we never use.  */

static void
prune_unused_types (void)
{
  unsigned int i;
  limbo_die_node *node;

#if ENABLE_ASSERT_CHECKING
  /* All the marks should already be clear.  */
  verify_marks_clear (comp_unit_die);
  for (node = limbo_die_list; node; node = node->next)
    verify_marks_clear (node->die);
#endif /* ENABLE_ASSERT_CHECKING */

  /* Set the mark on nodes that are actually used.  */
  prune_unused_types_walk (comp_unit_die);
  for (node = limbo_die_list; node; node = node->next)
    prune_unused_types_walk (node->die);

  /* Also set the mark on nodes referenced from the
     pubname_table or arange_table.  */
  for (i = 0; i < pubname_table_in_use; i++)
    prune_unused_types_mark (pubname_table[i].die, 1);
  for (i = 0; i < arange_table_in_use; i++)
    prune_unused_types_mark (arange_table[i], 1);

  /* Get rid of nodes that aren't marked; and update the string counts.  */
  if (debug_str_hash)
    htab_empty (debug_str_hash);
  prune_unused_types_prune (comp_unit_die);
  for (node = limbo_die_list; node; node = node->next)
    prune_unused_types_prune (node->die);

  /* Leave the marks clear.  */
  prune_unmark_dies (comp_unit_die);
  for (node = limbo_die_list; node; node = node->next)
    prune_unmark_dies (node->die);
}

/* Set the parameter to true if there are any relative pathnames in
   the file table.  */
static int
file_table_relative_p (void ** slot, void *param)
{
  bool *p = param;
  struct dwarf_file_data *d = *slot;
  if (d->emitted_number && d->filename[0] != DIR_SEPARATOR)
    {
      *p = true;
      return 0;
    }
  return 1;
}

/* Output stuff that dwarf requires at the end of every file,
   and generate the DWARF-2 debugging info.  */

static void
dwarf2out_finish (const char *filename)
{
  limbo_die_node *node, *next_node;
  dw_die_ref die = 0;

  /* Add the name for the main input file now.  We delayed this from
     dwarf2out_init to avoid complications with PCH.  */
  add_name_attribute (comp_unit_die, filename);
  if (filename[0] != DIR_SEPARATOR)
    add_comp_dir_attribute (comp_unit_die);
  else if (get_AT (comp_unit_die, DW_AT_comp_dir) == NULL)
    {
      bool p = false;
      htab_traverse (file_table, file_table_relative_p, &p);
      if (p)
	add_comp_dir_attribute (comp_unit_die);
    }

  /* Traverse the limbo die list, and add parent/child links.  The only
     dies without parents that should be here are concrete instances of
     inline functions, and the comp_unit_die.  We can ignore the comp_unit_die.
     For concrete instances, we can get the parent die from the abstract
     instance.  */
  for (node = limbo_die_list; node; node = next_node)
    {
      next_node = node->next;
      die = node->die;

      if (die->die_parent == NULL)
	{
	  dw_die_ref origin = get_AT_ref (die, DW_AT_abstract_origin);

	  if (origin)
	    add_child_die (origin->die_parent, die);
	  else if (die == comp_unit_die)
	    ;
	  else if (errorcount > 0 || sorrycount > 0)
	    /* It's OK to be confused by errors in the input.  */
	    add_child_die (comp_unit_die, die);
	  else
	    {
	      /* In certain situations, the lexical block containing a
		 nested function can be optimized away, which results
		 in the nested function die being orphaned.  Likewise
		 with the return type of that nested function.  Force
		 this to be a child of the containing function.

		 It may happen that even the containing function got fully
		 inlined and optimized out.  In that case we are lost and
		 assign the empty child.  This should not be big issue as
		 the function is likely unreachable too.  */
	      tree context = NULL_TREE;

	      gcc_assert (node->created_for);

	      if (DECL_P (node->created_for))
		context = DECL_CONTEXT (node->created_for);
	      else if (TYPE_P (node->created_for))
		context = TYPE_CONTEXT (node->created_for);

	      gcc_assert (context
			  && (TREE_CODE (context) == FUNCTION_DECL
			      || TREE_CODE (context) == NAMESPACE_DECL));

	      origin = lookup_decl_die (context);
	      if (origin)
	        add_child_die (origin, die);
	      else
	        add_child_die (comp_unit_die, die);
	    }
	}
    }

  limbo_die_list = NULL;

  /* Walk through the list of incomplete types again, trying once more to
     emit full debugging info for them.  */
  retry_incomplete_types ();

  if (flag_eliminate_unused_debug_types)
    prune_unused_types ();

  /* Generate separate CUs for each of the include files we've seen.
     They will go into limbo_die_list.  */
  if (flag_eliminate_dwarf2_dups)
    break_out_includes (comp_unit_die);

  /* Traverse the DIE's and add add sibling attributes to those DIE's
     that have children.  */
  add_sibling_attributes (comp_unit_die);
  for (node = limbo_die_list; node; node = node->next)
    add_sibling_attributes (node->die);

  /* Output a terminator label for the .text section.  */
  switch_to_section (text_section);
  targetm.asm_out.internal_label (asm_out_file, TEXT_END_LABEL, 0);
  if (flag_reorder_blocks_and_partition)
    {
      switch_to_section (unlikely_text_section ());
      targetm.asm_out.internal_label (asm_out_file, COLD_END_LABEL, 0);
    }

  /* We can only use the low/high_pc attributes if all of the code was
     in .text.  */
  if (!have_multiple_function_sections)
    {
      add_AT_lbl_id (comp_unit_die, DW_AT_low_pc, text_section_label);
      add_AT_lbl_id (comp_unit_die, DW_AT_high_pc, text_end_label);
    }

  /* If it wasn't, we need to give .debug_loc and .debug_ranges an appropriate
     "base address".  Use zero so that these addresses become absolute.  */
  else if (have_location_lists || ranges_table_in_use)
    add_AT_addr (comp_unit_die, DW_AT_entry_pc, const0_rtx);

  /* Output location list section if necessary.  */
  if (have_location_lists)
    {
      /* Output the location lists info.  */
      switch_to_section (debug_loc_section);
      ASM_GENERATE_INTERNAL_LABEL (loc_section_label,
				   DEBUG_LOC_SECTION_LABEL, 0);
      ASM_OUTPUT_LABEL (asm_out_file, loc_section_label);
      output_location_lists (die);
    }

  if (debug_info_level >= DINFO_LEVEL_NORMAL)
    add_AT_lineptr (comp_unit_die, DW_AT_stmt_list,
		    debug_line_section_label);

  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    add_AT_macptr (comp_unit_die, DW_AT_macro_info, macinfo_section_label);

  /* Output all of the compilation units.  We put the main one last so that
     the offsets are available to output_pubnames.  */
  for (node = limbo_die_list; node; node = node->next)
    output_comp_unit (node->die, 0);

  output_comp_unit (comp_unit_die, 0);

  /* Output the abbreviation table.  */
  switch_to_section (debug_abbrev_section);
  output_abbrev_section ();

  /* Output public names table if necessary.  */
  if (pubname_table_in_use)
    {
      switch_to_section (debug_pubnames_section);
      output_pubnames ();
    }

  /* Output the address range information.  We only put functions in the arange
     table, so don't write it out if we don't have any.  */
  if (fde_table_in_use)
    {
      switch_to_section (debug_aranges_section);
      output_aranges ();
    }

  /* Output ranges section if necessary.  */
  if (ranges_table_in_use)
    {
      switch_to_section (debug_ranges_section);
      ASM_OUTPUT_LABEL (asm_out_file, ranges_section_label);
      output_ranges ();
    }

  /* Output the source line correspondence table.  We must do this
     even if there is no line information.  Otherwise, on an empty
     translation unit, we will generate a present, but empty,
     .debug_info section.  IRIX 6.5 `nm' will then complain when
     examining the file.  This is done late so that any filenames
     used by the debug_info section are marked as 'used'.  */
  if (! DWARF2_ASM_LINE_DEBUG_INFO)
    {
      switch_to_section (debug_line_section);
      output_line_info ();
    }

  /* Have to end the macro section.  */
  if (debug_info_level >= DINFO_LEVEL_VERBOSE)
    {
      switch_to_section (debug_macinfo_section);
      dw2_asm_output_data (1, 0, "End compilation unit");
    }

  /* If we emitted any DW_FORM_strp form attribute, output the string
     table too.  */
  if (debug_str_hash)
    htab_traverse (debug_str_hash, output_indirect_string, NULL);
}
#else

/* This should never be used, but its address is needed for comparisons.  */
const struct gcc_debug_hooks dwarf2_debug_hooks;

#endif /* DWARF2_DEBUGGING_INFO */

#include "gt-dwarf2out.h"
