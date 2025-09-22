/* Functions for generic Darwin as target machine for GNU C compiler.
   Copyright (C) 1989, 1990, 1991, 1992, 1993, 2000, 2001, 2002, 2003, 2004,
   2005
   Free Software Foundation, Inc.
   Contributed by Apple Computer Inc.

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
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "insn-flags.h"
#include "output.h"
#include "insn-attr.h"
#include "flags.h"
#include "tree.h"
#include "expr.h"
#include "reload.h"
#include "function.h"
#include "ggc.h"
#include "langhooks.h"
#include "target.h"
#include "tm_p.h"
#include "toplev.h"
#include "hashtab.h"

/* Darwin supports a feature called fix-and-continue, which is used
   for rapid turn around debugging.  When code is compiled with the
   -mfix-and-continue flag, two changes are made to the generated code
   that allow the system to do things that it would normally not be
   able to do easily.  These changes allow gdb to load in
   recompilation of a translation unit that has been changed into a
   running program and replace existing functions and methods of that
   translation unit with versions of those functions and methods
   from the newly compiled translation unit.  The new functions access
   the existing static symbols from the old translation unit, if the
   symbol existed in the unit to be replaced, and from the new
   translation unit, otherwise.

   The changes are to insert 5 nops at the beginning of all functions
   and to use indirection to get at static symbols.  The 5 nops
   are required by consumers of the generated code.  Currently, gdb
   uses this to patch in a jump to the overriding function, this
   allows all uses of the old name to forward to the replacement,
   including existing function pointers and virtual methods.  See
   rs6000_emit_prologue for the code that handles the nop insertions.

   The added indirection allows gdb to redirect accesses to static
   symbols from the newly loaded translation unit to the existing
   symbol, if any.  @code{static} symbols are special and are handled by
   setting the second word in the .non_lazy_symbol_pointer data
   structure to symbol.  See indirect_data for the code that handles
   the extra indirection, and machopic_output_indirection and its use
   of MACHO_SYMBOL_STATIC for the code that handles @code{static}
   symbol indirection.  */

/* Section names.  */
section * darwin_sections[NUM_DARWIN_SECTIONS];

/* True if we're setting __attribute__ ((ms_struct)).  */
int darwin_ms_struct = false;

/* A get_unnamed_section callback used to switch to an ObjC section.
   DIRECTIVE is as for output_section_asm_op.  */

static void
output_objc_section_asm_op (const void *directive)
{
  static bool been_here = false;

  if (! been_here)
    {
      static const enum darwin_section_enum tomark[] =
	{
	  /* written, cold -> hot */
	  objc_cat_cls_meth_section,
	  objc_cat_inst_meth_section,
	  objc_string_object_section,
	  objc_constant_string_object_section,
	  objc_selector_refs_section,
	  objc_selector_fixup_section,
	  objc_cls_refs_section,
	  objc_class_section,
	  objc_meta_class_section,
	  /* shared, hot -> cold */
	  objc_cls_meth_section,
	  objc_inst_meth_section,
	  objc_protocol_section,
	  objc_class_names_section,
	  objc_meth_var_types_section,
	  objc_meth_var_names_section,
	  objc_category_section,
	  objc_class_vars_section,
	  objc_instance_vars_section,
	  objc_module_info_section,
	  objc_symbols_section
	};
      size_t i;

      been_here = true;
      for (i = 0; i < ARRAY_SIZE (tomark); i++)
	switch_to_section (darwin_sections[tomark[i]]);
    }
  output_section_asm_op (directive);
}

/* Implement TARGET_ASM_INIT_SECTIONS.  */

void
darwin_init_sections (void)
{
#define DEF_SECTION(NAME, FLAGS, DIRECTIVE, OBJC)		\
  darwin_sections[NAME] =					\
    get_unnamed_section (FLAGS, (OBJC				\
				 ? output_objc_section_asm_op	\
				 : output_section_asm_op),	\
			 "\t" DIRECTIVE);
#include "config/darwin-sections.def"
#undef DEF_SECTION

  readonly_data_section = darwin_sections[const_section];
  exception_section = darwin_sections[darwin_exception_section];
  eh_frame_section = darwin_sections[darwin_eh_frame_section];
}

int
name_needs_quotes (const char *name)
{
  int c;
  while ((c = *name++) != '\0')
    if (! ISIDNUM (c) && c != '.' && c != '$')
      return 1;
  return 0;
}

/* Return true if SYM_REF can be used without an indirection.  */
static int
machopic_symbol_defined_p (rtx sym_ref)
{
  if (SYMBOL_REF_FLAGS (sym_ref) & MACHO_SYMBOL_FLAG_DEFINED)
    return true;

  /* If a symbol references local and is not an extern to this
     file, then the symbol might be able to declared as defined.  */
  if (SYMBOL_REF_LOCAL_P (sym_ref) && ! SYMBOL_REF_EXTERNAL_P (sym_ref))
    {
      /* If the symbol references a variable and the variable is a
	 common symbol, then this symbol is not defined.  */
      if (SYMBOL_REF_FLAGS (sym_ref) & MACHO_SYMBOL_FLAG_VARIABLE)
	{
	  tree decl = SYMBOL_REF_DECL (sym_ref);
	  if (!decl)
	    return true;
	  if (DECL_COMMON (decl))
	    return false;
	}
      return true;
    }
  return false;
}

/* This module assumes that (const (symbol_ref "foo")) is a legal pic
   reference, which will not be changed.  */

enum machopic_addr_class
machopic_classify_symbol (rtx sym_ref)
{
  int flags;
  bool function_p;

  flags = SYMBOL_REF_FLAGS (sym_ref);
  function_p = SYMBOL_REF_FUNCTION_P (sym_ref);
  if (machopic_symbol_defined_p (sym_ref))
    return (function_p
	    ? MACHOPIC_DEFINED_FUNCTION : MACHOPIC_DEFINED_DATA);
  else
    return (function_p
	    ? MACHOPIC_UNDEFINED_FUNCTION : MACHOPIC_UNDEFINED_DATA);
}

#ifndef TARGET_FIX_AND_CONTINUE
#define TARGET_FIX_AND_CONTINUE 0
#endif

/* Indicate when fix-and-continue style code generation is being used
   and when a reference to data should be indirected so that it can be
   rebound in a new translation unit to reference the original instance
   of that data.  Symbol names that are for code generation local to
   the translation unit are bound to the new translation unit;
   currently this means symbols that begin with L or _OBJC_;
   otherwise, we indicate that an indirect reference should be made to
   permit the runtime to rebind new instances of the translation unit
   to the original instance of the data.  */

static int
indirect_data (rtx sym_ref)
{
  int lprefix;
  const char *name;

  /* If we aren't generating fix-and-continue code, don't do anything special.  */
  if (TARGET_FIX_AND_CONTINUE == 0)
    return 0;

  /* Otherwise, all symbol except symbols that begin with L or _OBJC_
     are indirected.  Symbols that begin with L and _OBJC_ are always
     bound to the current translation unit as they are used for
     generated local data of the translation unit.  */

  name = XSTR (sym_ref, 0);

  lprefix = (((name[0] == '*' || name[0] == '&')
              && (name[1] == 'L' || (name[1] == '"' && name[2] == 'L')))
             || (strncmp (name, "_OBJC_", 6) == 0));

  return ! lprefix;
}


static int
machopic_data_defined_p (rtx sym_ref)
{
  if (indirect_data (sym_ref))
    return 0;

  switch (machopic_classify_symbol (sym_ref))
    {
    case MACHOPIC_DEFINED_DATA:
    case MACHOPIC_DEFINED_FUNCTION:
      return 1;
    default:
      return 0;
    }
}

void
machopic_define_symbol (rtx mem)
{
  rtx sym_ref;

  gcc_assert (GET_CODE (mem) == MEM);
  sym_ref = XEXP (mem, 0);
  SYMBOL_REF_FLAGS (sym_ref) |= MACHO_SYMBOL_FLAG_DEFINED;
}

static GTY(()) char * function_base;

const char *
machopic_function_base_name (void)
{
  /* if dynamic-no-pic is on, we should not get here */
  gcc_assert (!MACHO_DYNAMIC_NO_PIC_P);

  if (function_base == NULL)
    function_base =
      (char *) ggc_alloc_string ("<pic base>", sizeof ("<pic base>"));

  current_function_uses_pic_offset_table = 1;

  return function_base;
}

/* Return a SYMBOL_REF for the PIC function base.  */

rtx
machopic_function_base_sym (void)
{
  rtx sym_ref;

  sym_ref = gen_rtx_SYMBOL_REF (Pmode, machopic_function_base_name ());
  SYMBOL_REF_FLAGS (sym_ref)
    |= (MACHO_SYMBOL_FLAG_VARIABLE | MACHO_SYMBOL_FLAG_DEFINED);
  return sym_ref;
}

/* Return either ORIG or (const:P (minus:P ORIG PIC_BASE)), depending
   on whether pic_base is NULL or not.  */
static inline rtx
gen_pic_offset (rtx orig, rtx pic_base)
{
  if (!pic_base)
    return orig;
  else
    return gen_rtx_CONST (Pmode, gen_rtx_MINUS (Pmode, orig, pic_base));
}

static GTY(()) const char * function_base_func_name;
static GTY(()) int current_pic_label_num;

void
machopic_output_function_base_name (FILE *file)
{
  const char *current_name;

  /* If dynamic-no-pic is on, we should not get here.  */
  gcc_assert (!MACHO_DYNAMIC_NO_PIC_P);
  current_name =
    IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (current_function_decl));
  if (function_base_func_name != current_name)
    {
      ++current_pic_label_num;
      function_base_func_name = current_name;
    }
  fprintf (file, "\"L%011d$pb\"", current_pic_label_num);
}

/* The suffix attached to non-lazy pointer symbols.  */
#define NON_LAZY_POINTER_SUFFIX "$non_lazy_ptr"
/* The suffix attached to stub symbols.  */
#define STUB_SUFFIX "$stub"

typedef struct machopic_indirection GTY (())
{
  /* The SYMBOL_REF for the entity referenced.  */
  rtx symbol;
  /* The name of the stub or non-lazy pointer.  */
  const char * ptr_name;
  /* True iff this entry is for a stub (as opposed to a non-lazy
     pointer).  */
  bool stub_p;
  /* True iff this stub or pointer pointer has been referenced.  */
  bool used;
} machopic_indirection;

/* A table mapping stub names and non-lazy pointer names to
   SYMBOL_REFs for the stubbed-to and pointed-to entities.  */

static GTY ((param_is (struct machopic_indirection))) htab_t
  machopic_indirections;

/* Return a hash value for a SLOT in the indirections hash table.  */

static hashval_t
machopic_indirection_hash (const void *slot)
{
  const machopic_indirection *p = (const machopic_indirection *) slot;
  return htab_hash_string (p->ptr_name);
}

/* Returns true if the KEY is the same as that associated with
   SLOT.  */

static int
machopic_indirection_eq (const void *slot, const void *key)
{
  return strcmp (((const machopic_indirection *) slot)->ptr_name, key) == 0;
}

/* Return the name of the non-lazy pointer (if STUB_P is false) or
   stub (if STUB_B is true) corresponding to the given name.  */

const char *
machopic_indirection_name (rtx sym_ref, bool stub_p)
{
  char *buffer;
  const char *name = XSTR (sym_ref, 0);
  size_t namelen = strlen (name);
  machopic_indirection *p;
  void ** slot;
  bool saw_star = false;
  bool needs_quotes;
  const char *suffix;
  const char *prefix = user_label_prefix;
  const char *quote = "";
  tree id;

  id = maybe_get_identifier (name);
  if (id)
    {
      tree id_orig = id;

      while (IDENTIFIER_TRANSPARENT_ALIAS (id))
	id = TREE_CHAIN (id);
      if (id != id_orig)
	{
	  name = IDENTIFIER_POINTER (id);
	  namelen = strlen (name);
	}
    }

  if (name[0] == '*')
    {
      saw_star = true;
      prefix = "";
      ++name;
      --namelen;
    }

  needs_quotes = name_needs_quotes (name);
  if (needs_quotes)
    {
      quote = "\"";
    }

  if (stub_p)
    suffix = STUB_SUFFIX;
  else
    suffix = NON_LAZY_POINTER_SUFFIX;

  buffer = alloca (strlen ("&L")
		   + strlen (prefix)
		   + namelen
		   + strlen (suffix)
		   + 2 * strlen (quote)
		   + 1 /* '\0' */);

  /* Construct the name of the non-lazy pointer or stub.  */
  sprintf (buffer, "&%sL%s%s%s%s", quote, prefix, name, suffix, quote);

  if (!machopic_indirections)
    machopic_indirections = htab_create_ggc (37,
					     machopic_indirection_hash,
					     machopic_indirection_eq,
					     /*htab_del=*/NULL);

  slot = htab_find_slot_with_hash (machopic_indirections, buffer,
				   htab_hash_string (buffer), INSERT);
  if (*slot)
    {
      p = (machopic_indirection *) *slot;
    }
  else
    {
      p = (machopic_indirection *) ggc_alloc (sizeof (machopic_indirection));
      p->symbol = sym_ref;
      p->ptr_name = xstrdup (buffer);
      p->stub_p = stub_p;
      p->used = false;
      *slot = p;
    }

  return p->ptr_name;
}

/* Return the name of the stub for the mcount function.  */

const char*
machopic_mcount_stub_name (void)
{
  rtx symbol = gen_rtx_SYMBOL_REF (Pmode, "*mcount");
  return machopic_indirection_name (symbol, /*stub_p=*/true);
}

/* If NAME is the name of a stub or a non-lazy pointer , mark the stub
   or non-lazy pointer as used -- and mark the object to which the
   pointer/stub refers as used as well, since the pointer/stub will
   emit a reference to it.  */

void
machopic_validate_stub_or_non_lazy_ptr (const char *name)
{
  machopic_indirection *p;

  p = ((machopic_indirection *)
       (htab_find_with_hash (machopic_indirections, name,
			     htab_hash_string (name))));
  if (p && ! p->used)
    {
      const char *real_name;
      tree id;

      p->used = true;

      /* Do what output_addr_const will do when we actually call it.  */
      if (SYMBOL_REF_DECL (p->symbol))
	mark_decl_referenced (SYMBOL_REF_DECL (p->symbol));

      real_name = targetm.strip_name_encoding (XSTR (p->symbol, 0));

      id = maybe_get_identifier (real_name);
      if (id)
	mark_referenced (id);
    }
}

/* Transform ORIG, which may be any data source, to the corresponding
   source using indirections.  */

rtx
machopic_indirect_data_reference (rtx orig, rtx reg)
{
  rtx ptr_ref = orig;

  if (! MACHOPIC_INDIRECT)
    return orig;

  if (GET_CODE (orig) == SYMBOL_REF)
    {
      int defined = machopic_data_defined_p (orig);

      if (defined && MACHO_DYNAMIC_NO_PIC_P)
	{
#if defined (TARGET_TOC)
	  /* Create a new register for CSE opportunities.  */
	  rtx hi_reg = (no_new_pseudos ? reg : gen_reg_rtx (Pmode));
 	  emit_insn (gen_macho_high (hi_reg, orig));
 	  emit_insn (gen_macho_low (reg, hi_reg, orig));
#else
	   /* some other cpu -- writeme!  */
	   gcc_unreachable ();
#endif
	   return reg;
	}
      else if (defined)
	{
#if defined (TARGET_TOC) || defined (HAVE_lo_sum)
	  rtx pic_base = machopic_function_base_sym ();
	  rtx offset = gen_pic_offset (orig, pic_base);
#endif

#if defined (TARGET_TOC) /* i.e., PowerPC */
	  rtx hi_sum_reg = (no_new_pseudos ? reg : gen_reg_rtx (Pmode));

	  gcc_assert (reg);

	  emit_insn (gen_rtx_SET (Pmode, hi_sum_reg,
			      gen_rtx_PLUS (Pmode, pic_offset_table_rtx,
				       gen_rtx_HIGH (Pmode, offset))));
	  emit_insn (gen_rtx_SET (Pmode, reg,
				  gen_rtx_LO_SUM (Pmode, hi_sum_reg, offset)));

	  orig = reg;
#else
#if defined (HAVE_lo_sum)
	  gcc_assert (reg);

	  emit_insn (gen_rtx_SET (VOIDmode, reg,
				  gen_rtx_HIGH (Pmode, offset)));
	  emit_insn (gen_rtx_SET (VOIDmode, reg,
				  gen_rtx_LO_SUM (Pmode, reg, offset)));
	  emit_insn (gen_rtx_USE (VOIDmode, pic_offset_table_rtx));

	  orig = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, reg);
#endif
#endif
	  return orig;
	}

      ptr_ref = (gen_rtx_SYMBOL_REF
		 (Pmode,
		  machopic_indirection_name (orig, /*stub_p=*/false)));

      SYMBOL_REF_DATA (ptr_ref) = SYMBOL_REF_DATA (orig);

      ptr_ref = gen_const_mem (Pmode, ptr_ref);
      machopic_define_symbol (ptr_ref);

      return ptr_ref;
    }
  else if (GET_CODE (orig) == CONST)
    {
      rtx base, result;

      /* legitimize both operands of the PLUS */
      if (GET_CODE (XEXP (orig, 0)) == PLUS)
	{
	  base = machopic_indirect_data_reference (XEXP (XEXP (orig, 0), 0),
						   reg);
	  orig = machopic_indirect_data_reference (XEXP (XEXP (orig, 0), 1),
						   (base == reg ? 0 : reg));
	}
      else
	return orig;

      if (MACHOPIC_PURE && GET_CODE (orig) == CONST_INT)
	result = plus_constant (base, INTVAL (orig));
      else
	result = gen_rtx_PLUS (Pmode, base, orig);

      if (MACHOPIC_JUST_INDIRECT && GET_CODE (base) == MEM)
	{
	  if (reg)
	    {
	      emit_move_insn (reg, result);
	      result = reg;
	    }
	  else
	    {
	      result = force_reg (GET_MODE (result), result);
	    }
	}

      return result;

    }
  else if (GET_CODE (orig) == MEM)
    XEXP (ptr_ref, 0) = machopic_indirect_data_reference (XEXP (orig, 0), reg);
  /* When the target is i386, this code prevents crashes due to the
     compiler's ignorance on how to move the PIC base register to
     other registers.  (The reload phase sometimes introduces such
     insns.)  */
  else if (GET_CODE (orig) == PLUS
	   && GET_CODE (XEXP (orig, 0)) == REG
	   && REGNO (XEXP (orig, 0)) == PIC_OFFSET_TABLE_REGNUM
#ifdef I386
	   /* Prevent the same register from being erroneously used
	      as both the base and index registers.  */
	   && GET_CODE (XEXP (orig, 1)) == CONST
#endif
	   && reg)
    {
      emit_move_insn (reg, XEXP (orig, 0));
      XEXP (ptr_ref, 0) = reg;
    }
  return ptr_ref;
}

/* Transform TARGET (a MEM), which is a function call target, to the
   corresponding symbol_stub if necessary.  Return a new MEM.  */

rtx
machopic_indirect_call_target (rtx target)
{
  if (GET_CODE (target) != MEM)
    return target;

  if (MACHOPIC_INDIRECT
      && GET_CODE (XEXP (target, 0)) == SYMBOL_REF
      && !(SYMBOL_REF_FLAGS (XEXP (target, 0))
	   & MACHO_SYMBOL_FLAG_DEFINED))
    {
      rtx sym_ref = XEXP (target, 0);
      const char *stub_name = machopic_indirection_name (sym_ref,
							 /*stub_p=*/true);
      enum machine_mode mode = GET_MODE (sym_ref);

      XEXP (target, 0) = gen_rtx_SYMBOL_REF (mode, stub_name);
      SYMBOL_REF_DATA (XEXP (target, 0)) = SYMBOL_REF_DATA (sym_ref);
      MEM_READONLY_P (target) = 1;
      MEM_NOTRAP_P (target) = 1;
    }

  return target;
}

rtx
machopic_legitimize_pic_address (rtx orig, enum machine_mode mode, rtx reg)
{
  rtx pic_ref = orig;

  if (! MACHOPIC_INDIRECT)
    return orig;

  /* First handle a simple SYMBOL_REF or LABEL_REF */
  if (GET_CODE (orig) == LABEL_REF
      || (GET_CODE (orig) == SYMBOL_REF
	  ))
    {
      /* addr(foo) = &func+(foo-func) */
      rtx pic_base;

      orig = machopic_indirect_data_reference (orig, reg);

      if (GET_CODE (orig) == PLUS
	  && GET_CODE (XEXP (orig, 0)) == REG)
	{
	  if (reg == 0)
	    return force_reg (mode, orig);

	  emit_move_insn (reg, orig);
	  return reg;
	}

      /* if dynamic-no-pic we don't have a pic base  */
      if (MACHO_DYNAMIC_NO_PIC_P)
	pic_base = NULL;
      else
	pic_base = machopic_function_base_sym ();

      if (GET_CODE (orig) == MEM)
	{
	  if (reg == 0)
	    {
	      gcc_assert (!reload_in_progress);
	      reg = gen_reg_rtx (Pmode);
	    }

#ifdef HAVE_lo_sum
	  if (MACHO_DYNAMIC_NO_PIC_P
	      && (GET_CODE (XEXP (orig, 0)) == SYMBOL_REF
		  || GET_CODE (XEXP (orig, 0)) == LABEL_REF))
	    {
#if defined (TARGET_TOC)	/* ppc  */
	      rtx temp_reg = (no_new_pseudos) ? reg : gen_reg_rtx (Pmode);
	      rtx asym = XEXP (orig, 0);
	      rtx mem;

	      emit_insn (gen_macho_high (temp_reg, asym));
	      mem = gen_const_mem (GET_MODE (orig),
				   gen_rtx_LO_SUM (Pmode, temp_reg, asym));
	      emit_insn (gen_rtx_SET (VOIDmode, reg, mem));
#else
	      /* Some other CPU -- WriteMe! but right now there are no other platform that can use dynamic-no-pic  */
	      gcc_unreachable ();
#endif
	      pic_ref = reg;
	    }
	  else
	  if (GET_CODE (XEXP (orig, 0)) == SYMBOL_REF
	      || GET_CODE (XEXP (orig, 0)) == LABEL_REF)
	    {
	      rtx offset = gen_pic_offset (XEXP (orig, 0), pic_base);
#if defined (TARGET_TOC) /* i.e., PowerPC */
	      /* Generating a new reg may expose opportunities for
		 common subexpression elimination.  */
              rtx hi_sum_reg = no_new_pseudos ? reg : gen_reg_rtx (Pmode);
	      rtx mem;
	      rtx insn;
	      rtx sum;

	      sum = gen_rtx_HIGH (Pmode, offset);
	      if (! MACHO_DYNAMIC_NO_PIC_P)
		sum = gen_rtx_PLUS (Pmode, pic_offset_table_rtx, sum);

	      emit_insn (gen_rtx_SET (Pmode, hi_sum_reg, sum));

	      mem = gen_const_mem (GET_MODE (orig),
				  gen_rtx_LO_SUM (Pmode,
						  hi_sum_reg, offset));
	      insn = emit_insn (gen_rtx_SET (VOIDmode, reg, mem));
	      REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EQUAL, pic_ref,
						    REG_NOTES (insn));

	      pic_ref = reg;
#else
	      emit_insn (gen_rtx_USE (VOIDmode,
				      gen_rtx_REG (Pmode,
						   PIC_OFFSET_TABLE_REGNUM)));

	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				      gen_rtx_HIGH (Pmode,
						    gen_rtx_CONST (Pmode,
								   offset))));
	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				  gen_rtx_LO_SUM (Pmode, reg,
					   gen_rtx_CONST (Pmode, offset))));
	      pic_ref = gen_rtx_PLUS (Pmode,
				      pic_offset_table_rtx, reg);
#endif
	    }
	  else
#endif  /* HAVE_lo_sum */
	    {
	      rtx pic = pic_offset_table_rtx;
	      if (GET_CODE (pic) != REG)
		{
		  emit_move_insn (reg, pic);
		  pic = reg;
		}
#if 0
	      emit_insn (gen_rtx_USE (VOIDmode,
				      gen_rtx_REG (Pmode,
						   PIC_OFFSET_TABLE_REGNUM)));
#endif

	      if (reload_in_progress)
		regs_ever_live[REGNO (pic)] = 1;
	      pic_ref = gen_rtx_PLUS (Pmode, pic,
				      gen_pic_offset (XEXP (orig, 0),
						      pic_base));
	    }

#if !defined (TARGET_TOC)
	  emit_move_insn (reg, pic_ref);
	  pic_ref = gen_const_mem (GET_MODE (orig), reg);
#endif
	}
      else
	{

#ifdef HAVE_lo_sum
	  if (GET_CODE (orig) == SYMBOL_REF
	      || GET_CODE (orig) == LABEL_REF)
	    {
	      rtx offset = gen_pic_offset (orig, pic_base);
#if defined (TARGET_TOC) /* i.e., PowerPC */
              rtx hi_sum_reg;

	      if (reg == 0)
		{
		  gcc_assert (!reload_in_progress);
		  reg = gen_reg_rtx (Pmode);
		}

	      hi_sum_reg = reg;

	      emit_insn (gen_rtx_SET (Pmode, hi_sum_reg,
				      (MACHO_DYNAMIC_NO_PIC_P)
				      ? gen_rtx_HIGH (Pmode, offset)
				      : gen_rtx_PLUS (Pmode,
						      pic_offset_table_rtx,
						      gen_rtx_HIGH (Pmode,
								    offset))));
	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				      gen_rtx_LO_SUM (Pmode,
						      hi_sum_reg, offset)));
	      pic_ref = reg;
#else
	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				      gen_rtx_HIGH (Pmode, offset)));
	      emit_insn (gen_rtx_SET (VOIDmode, reg,
				      gen_rtx_LO_SUM (Pmode, reg, offset)));
	      pic_ref = gen_rtx_PLUS (Pmode,
				      pic_offset_table_rtx, reg);
#endif
	    }
	  else
#endif  /*  HAVE_lo_sum  */
	    {
	      if (REG_P (orig)
	          || GET_CODE (orig) == SUBREG)
		{
		  return orig;
		}
	      else
		{
		  rtx pic = pic_offset_table_rtx;
		  if (GET_CODE (pic) != REG)
		    {
		      emit_move_insn (reg, pic);
		      pic = reg;
		    }
#if 0
		  emit_insn (gen_rtx_USE (VOIDmode,
					  pic_offset_table_rtx));
#endif
		  if (reload_in_progress)
		    regs_ever_live[REGNO (pic)] = 1;
		  pic_ref = gen_rtx_PLUS (Pmode,
					  pic,
					  gen_pic_offset (orig, pic_base));
		}
	    }
	}

      if (GET_CODE (pic_ref) != REG)
        {
          if (reg != 0)
            {
              emit_move_insn (reg, pic_ref);
              return reg;
            }
          else
            {
              return force_reg (mode, pic_ref);
            }
        }
      else
        {
          return pic_ref;
        }
    }

  else if (GET_CODE (orig) == SYMBOL_REF)
    return orig;

  else if (GET_CODE (orig) == PLUS
	   && (GET_CODE (XEXP (orig, 0)) == MEM
	       || GET_CODE (XEXP (orig, 0)) == SYMBOL_REF
	       || GET_CODE (XEXP (orig, 0)) == LABEL_REF)
	   && XEXP (orig, 0) != pic_offset_table_rtx
	   && GET_CODE (XEXP (orig, 1)) != REG)

    {
      rtx base;
      int is_complex = (GET_CODE (XEXP (orig, 0)) == MEM);

      base = machopic_legitimize_pic_address (XEXP (orig, 0), Pmode, reg);
      orig = machopic_legitimize_pic_address (XEXP (orig, 1),
					      Pmode, (base == reg ? 0 : reg));
      if (GET_CODE (orig) == CONST_INT)
	{
	  pic_ref = plus_constant (base, INTVAL (orig));
	  is_complex = 1;
	}
      else
	pic_ref = gen_rtx_PLUS (Pmode, base, orig);

      if (reg && is_complex)
	{
	  emit_move_insn (reg, pic_ref);
	  pic_ref = reg;
	}
      /* Likewise, should we set special REG_NOTEs here?  */
    }

  else if (GET_CODE (orig) == CONST)
    {
      return machopic_legitimize_pic_address (XEXP (orig, 0), Pmode, reg);
    }

  else if (GET_CODE (orig) == MEM
	   && GET_CODE (XEXP (orig, 0)) == SYMBOL_REF)
    {
      rtx addr = machopic_legitimize_pic_address (XEXP (orig, 0), Pmode, reg);
      addr = replace_equiv_address (orig, addr);
      emit_move_insn (reg, addr);
      pic_ref = reg;
    }

  return pic_ref;
}

/* Output the stub or non-lazy pointer in *SLOT, if it has been used.
   DATA is the FILE* for assembly output.  Called from
   htab_traverse.  */

static int
machopic_output_indirection (void **slot, void *data)
{
  machopic_indirection *p = *((machopic_indirection **) slot);
  FILE *asm_out_file = (FILE *) data;
  rtx symbol;
  const char *sym_name;
  const char *ptr_name;

  if (!p->used)
    return 1;

  symbol = p->symbol;
  sym_name = XSTR (symbol, 0);
  ptr_name = p->ptr_name;

  if (p->stub_p)
    {
      char *sym;
      char *stub;
      tree id;

      id = maybe_get_identifier (sym_name);
      if (id)
	{
	  tree id_orig = id;

	  while (IDENTIFIER_TRANSPARENT_ALIAS (id))
	    id = TREE_CHAIN (id);
	  if (id != id_orig)
	    sym_name = IDENTIFIER_POINTER (id);
	}

      sym = alloca (strlen (sym_name) + 2);
      if (sym_name[0] == '*' || sym_name[0] == '&')
	strcpy (sym, sym_name + 1);
      else if (sym_name[0] == '-' || sym_name[0] == '+')
	strcpy (sym, sym_name);
      else
	sprintf (sym, "%s%s", user_label_prefix, sym_name);

      stub = alloca (strlen (ptr_name) + 2);
      if (ptr_name[0] == '*' || ptr_name[0] == '&')
	strcpy (stub, ptr_name + 1);
      else
	sprintf (stub, "%s%s", user_label_prefix, ptr_name);

      machopic_output_stub (asm_out_file, sym, stub);
    }
  else if (! indirect_data (symbol)
	   && (machopic_symbol_defined_p (symbol)
	       || SYMBOL_REF_LOCAL_P (symbol)))
    {
      switch_to_section (data_section);
      assemble_align (GET_MODE_ALIGNMENT (Pmode));
      assemble_label (ptr_name);
      assemble_integer (gen_rtx_SYMBOL_REF (Pmode, sym_name),
			GET_MODE_SIZE (Pmode),
			GET_MODE_ALIGNMENT (Pmode), 1);
    }
  else
    {
      rtx init = const0_rtx;

      switch_to_section (darwin_sections[machopic_nl_symbol_ptr_section]);
      assemble_name (asm_out_file, ptr_name);
      fprintf (asm_out_file, ":\n");

      fprintf (asm_out_file, "\t.indirect_symbol ");
      assemble_name (asm_out_file, sym_name);
      fprintf (asm_out_file, "\n");

      /* Variables that are marked with MACHO_SYMBOL_STATIC need to
	 have their symbol name instead of 0 in the second entry of
	 the non-lazy symbol pointer data structure when they are
	 defined.  This allows the runtime to rebind newer instances
	 of the translation unit with the original instance of the
	 symbol.  */

      if ((SYMBOL_REF_FLAGS (symbol) & MACHO_SYMBOL_STATIC)
	  && machopic_symbol_defined_p (symbol))
	init = gen_rtx_SYMBOL_REF (Pmode, sym_name);

      assemble_integer (init, GET_MODE_SIZE (Pmode),
			GET_MODE_ALIGNMENT (Pmode), 1);
    }

  return 1;
}

void
machopic_finish (FILE *asm_out_file)
{
  if (machopic_indirections)
    htab_traverse_noresize (machopic_indirections,
			    machopic_output_indirection,
			    asm_out_file);
}

int
machopic_operand_p (rtx op)
{
  if (MACHOPIC_JUST_INDIRECT)
    {
      while (GET_CODE (op) == CONST)
	op = XEXP (op, 0);

      if (GET_CODE (op) == SYMBOL_REF)
	return machopic_symbol_defined_p (op);
      else
	return 0;
    }

  while (GET_CODE (op) == CONST)
    op = XEXP (op, 0);

  if (GET_CODE (op) == MINUS
      && GET_CODE (XEXP (op, 0)) == SYMBOL_REF
      && GET_CODE (XEXP (op, 1)) == SYMBOL_REF
      && machopic_symbol_defined_p (XEXP (op, 0))
      && machopic_symbol_defined_p (XEXP (op, 1)))
      return 1;

  return 0;
}

/* This function records whether a given name corresponds to a defined
   or undefined function or variable, for machopic_classify_ident to
   use later.  */

void
darwin_encode_section_info (tree decl, rtx rtl, int first ATTRIBUTE_UNUSED)
{
  rtx sym_ref;

  /* Do the standard encoding things first.  */
  default_encode_section_info (decl, rtl, first);

  if (TREE_CODE (decl) != FUNCTION_DECL && TREE_CODE (decl) != VAR_DECL)
    return;

  sym_ref = XEXP (rtl, 0);
  if (TREE_CODE (decl) == VAR_DECL)
    SYMBOL_REF_FLAGS (sym_ref) |= MACHO_SYMBOL_FLAG_VARIABLE;

  if (!DECL_EXTERNAL (decl)
      && (!TREE_PUBLIC (decl) || !DECL_WEAK (decl))
      && ! lookup_attribute ("weakref", DECL_ATTRIBUTES (decl))
      && ((TREE_STATIC (decl)
	   && (!DECL_COMMON (decl) || !TREE_PUBLIC (decl)))
	  || (!DECL_COMMON (decl) && DECL_INITIAL (decl)
	      && DECL_INITIAL (decl) != error_mark_node)))
    SYMBOL_REF_FLAGS (sym_ref) |= MACHO_SYMBOL_FLAG_DEFINED;

  if (! TREE_PUBLIC (decl))
    SYMBOL_REF_FLAGS (sym_ref) |= MACHO_SYMBOL_STATIC;
}

void
darwin_mark_decl_preserved (const char *name)
{
  fprintf (asm_out_file, ".no_dead_strip ");
  assemble_name (asm_out_file, name);
  fputc ('\n', asm_out_file);
}

int
machopic_reloc_rw_mask (void)
{
  return MACHOPIC_INDIRECT ? 3 : 0;
}

section *
machopic_select_section (tree exp, int reloc,
			 unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  section *base_section;
  bool weak_p = (DECL_P (exp) && DECL_WEAK (exp)
		 && (lookup_attribute ("weak", DECL_ATTRIBUTES (exp))
		     || ! lookup_attribute ("weak_import",
					    DECL_ATTRIBUTES (exp))));

  if (TREE_CODE (exp) == FUNCTION_DECL)
    {
      if (reloc == 1)
	base_section = (weak_p
			? darwin_sections[text_unlikely_coal_section]
			: unlikely_text_section ());
      else
	base_section = weak_p ? darwin_sections[text_coal_section] : text_section;
    }
  else if (decl_readonly_section (exp, reloc))
    base_section = weak_p ? darwin_sections[const_coal_section] : darwin_sections[const_section];
  else if (TREE_READONLY (exp) || TREE_CONSTANT (exp))
    base_section = weak_p ? darwin_sections[const_data_coal_section] : darwin_sections[const_data_section];
  else
    base_section = weak_p ? darwin_sections[data_coal_section] : data_section;

  if (TREE_CODE (exp) == STRING_CST
      && ((size_t) TREE_STRING_LENGTH (exp)
	  == strlen (TREE_STRING_POINTER (exp)) + 1))
    return darwin_sections[cstring_section];
  else if ((TREE_CODE (exp) == INTEGER_CST || TREE_CODE (exp) == REAL_CST)
	   && flag_merge_constants)
    {
      tree size = TYPE_SIZE_UNIT (TREE_TYPE (exp));

      if (TREE_CODE (size) == INTEGER_CST &&
	  TREE_INT_CST_LOW (size) == 4 &&
	  TREE_INT_CST_HIGH (size) == 0)
	return darwin_sections[literal4_section];
      else if (TREE_CODE (size) == INTEGER_CST &&
	       TREE_INT_CST_LOW (size) == 8 &&
	       TREE_INT_CST_HIGH (size) == 0)
	return darwin_sections[literal8_section];
      else if (TARGET_64BIT
	       && TREE_CODE (size) == INTEGER_CST
	       && TREE_INT_CST_LOW (size) == 16
	       && TREE_INT_CST_HIGH (size) == 0)
	return darwin_sections[literal16_section];
      else
	return base_section;
    }
  else if (TREE_CODE (exp) == CONSTRUCTOR
	   && TREE_TYPE (exp)
	   && TREE_CODE (TREE_TYPE (exp)) == RECORD_TYPE
	   && TYPE_NAME (TREE_TYPE (exp)))
    {
      tree name = TYPE_NAME (TREE_TYPE (exp));
      if (TREE_CODE (name) == TYPE_DECL)
	name = DECL_NAME (name);

      if (!strcmp (IDENTIFIER_POINTER (name), "__builtin_ObjCString"))
	{
	  if (flag_next_runtime)
	    return darwin_sections[objc_constant_string_object_section];
	  else
	    return darwin_sections[objc_string_object_section];
	}
      else
	return base_section;
    }
  else if (TREE_CODE (exp) == VAR_DECL &&
	   DECL_NAME (exp) &&
	   TREE_CODE (DECL_NAME (exp)) == IDENTIFIER_NODE &&
	   IDENTIFIER_POINTER (DECL_NAME (exp)) &&
	   !strncmp (IDENTIFIER_POINTER (DECL_NAME (exp)), "_OBJC_", 6))
    {
      const char *name = IDENTIFIER_POINTER (DECL_NAME (exp));

      if (!strncmp (name, "_OBJC_CLASS_METHODS_", 20))
	return darwin_sections[objc_cls_meth_section];
      else if (!strncmp (name, "_OBJC_INSTANCE_METHODS_", 23))
	return darwin_sections[objc_inst_meth_section];
      else if (!strncmp (name, "_OBJC_CATEGORY_CLASS_METHODS_", 20))
	return darwin_sections[objc_cat_cls_meth_section];
      else if (!strncmp (name, "_OBJC_CATEGORY_INSTANCE_METHODS_", 23))
	return darwin_sections[objc_cat_inst_meth_section];
      else if (!strncmp (name, "_OBJC_CLASS_VARIABLES_", 22))
	return darwin_sections[objc_class_vars_section];
      else if (!strncmp (name, "_OBJC_INSTANCE_VARIABLES_", 25))
	return darwin_sections[objc_instance_vars_section];
      else if (!strncmp (name, "_OBJC_CLASS_PROTOCOLS_", 22))
	return darwin_sections[objc_cat_cls_meth_section];
      else if (!strncmp (name, "_OBJC_CLASS_NAME_", 17))
	return darwin_sections[objc_class_names_section];
      else if (!strncmp (name, "_OBJC_METH_VAR_NAME_", 20))
	return darwin_sections[objc_meth_var_names_section];
      else if (!strncmp (name, "_OBJC_METH_VAR_TYPE_", 20))
	return darwin_sections[objc_meth_var_types_section];
      else if (!strncmp (name, "_OBJC_CLASS_REFERENCES", 22))
	return darwin_sections[objc_cls_refs_section];
      else if (!strncmp (name, "_OBJC_CLASS_", 12))
	return darwin_sections[objc_class_section];
      else if (!strncmp (name, "_OBJC_METACLASS_", 16))
	return darwin_sections[objc_meta_class_section];
      else if (!strncmp (name, "_OBJC_CATEGORY_", 15))
	return darwin_sections[objc_category_section];
      else if (!strncmp (name, "_OBJC_SELECTOR_REFERENCES", 25))
	return darwin_sections[objc_selector_refs_section];
      else if (!strncmp (name, "_OBJC_SELECTOR_FIXUP", 20))
	return darwin_sections[objc_selector_fixup_section];
      else if (!strncmp (name, "_OBJC_SYMBOLS", 13))
	return darwin_sections[objc_symbols_section];
      else if (!strncmp (name, "_OBJC_MODULES", 13))
	return darwin_sections[objc_module_info_section];
      else if (!strncmp (name, "_OBJC_IMAGE_INFO", 16))
	return darwin_sections[objc_image_info_section];
      else if (!strncmp (name, "_OBJC_PROTOCOL_INSTANCE_METHODS_", 32))
	return darwin_sections[objc_cat_inst_meth_section];
      else if (!strncmp (name, "_OBJC_PROTOCOL_CLASS_METHODS_", 29))
	return darwin_sections[objc_cat_cls_meth_section];
      else if (!strncmp (name, "_OBJC_PROTOCOL_REFS_", 20))
	return darwin_sections[objc_cat_cls_meth_section];
      else if (!strncmp (name, "_OBJC_PROTOCOL_", 15))
	return darwin_sections[objc_protocol_section];
      else
	return base_section;
    }
  else
    return base_section;
}

/* This can be called with address expressions as "rtx".
   They must go in "const".  */

section *
machopic_select_rtx_section (enum machine_mode mode, rtx x,
			     unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  if (GET_MODE_SIZE (mode) == 8
      && (GET_CODE (x) == CONST_INT
	  || GET_CODE (x) == CONST_DOUBLE))
    return darwin_sections[literal8_section];
  else if (GET_MODE_SIZE (mode) == 4
	   && (GET_CODE (x) == CONST_INT
	       || GET_CODE (x) == CONST_DOUBLE))
    return darwin_sections[literal4_section];
  else if (TARGET_64BIT
	   && GET_MODE_SIZE (mode) == 16
	   && (GET_CODE (x) == CONST_INT
	       || GET_CODE (x) == CONST_DOUBLE
	       || GET_CODE (x) == CONST_VECTOR))
    return darwin_sections[literal16_section];
  else if (MACHOPIC_INDIRECT
	   && (GET_CODE (x) == SYMBOL_REF
	       || GET_CODE (x) == CONST
	       || GET_CODE (x) == LABEL_REF))
    return darwin_sections[const_data_section];
  else
    return darwin_sections[const_section];
}

void
machopic_asm_out_constructor (rtx symbol, int priority ATTRIBUTE_UNUSED)
{
  if (MACHOPIC_INDIRECT)
    switch_to_section (darwin_sections[mod_init_section]);
  else
    switch_to_section (darwin_sections[constructor_section]);
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);

  if (! MACHOPIC_INDIRECT)
    fprintf (asm_out_file, ".reference .constructors_used\n");
}

void
machopic_asm_out_destructor (rtx symbol, int priority ATTRIBUTE_UNUSED)
{
  if (MACHOPIC_INDIRECT)
    switch_to_section (darwin_sections[mod_term_section]);
  else
    switch_to_section (darwin_sections[destructor_section]);
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);

  if (! MACHOPIC_INDIRECT)
    fprintf (asm_out_file, ".reference .destructors_used\n");
}

void
darwin_globalize_label (FILE *stream, const char *name)
{
  if (!!strncmp (name, "_OBJC_", 6))
    default_globalize_label (stream, name);
}

void
darwin_asm_named_section (const char *name,
			  unsigned int flags ATTRIBUTE_UNUSED,
			  tree decl ATTRIBUTE_UNUSED)
{
  fprintf (asm_out_file, "\t.section %s\n", name);
}

void
darwin_unique_section (tree decl ATTRIBUTE_UNUSED, int reloc ATTRIBUTE_UNUSED)
{
  /* Darwin does not use unique sections.  */
}

/* Handle __attribute__ ((apple_kext_compatibility)).
   This only applies to darwin kexts for 2.95 compatibility -- it shrinks the
   vtable for classes with this attribute (and their descendants) by not
   outputting the new 3.0 nondeleting destructor.  This means that such
   objects CANNOT be allocated on the stack or as globals UNLESS they have
   a completely empty `operator delete'.
   Luckily, this fits in with the Darwin kext model.

   This attribute also disables gcc3's potential overlaying of derived
   class data members on the padding at the end of the base class.  */

tree
darwin_handle_kext_attribute (tree *node, tree name,
			      tree args ATTRIBUTE_UNUSED,
			      int flags ATTRIBUTE_UNUSED,
			      bool *no_add_attrs)
{
  /* APPLE KEXT stuff -- only applies with pure static C++ code.  */
  if (! TARGET_KEXTABI)
    {
      warning (0, "%<%s%> 2.95 vtable-compatability attribute applies "
	       "only when compiling a kext", IDENTIFIER_POINTER (name));

      *no_add_attrs = true;
    }
  else if (TREE_CODE (*node) != RECORD_TYPE)
    {
      warning (0, "%<%s%> 2.95 vtable-compatability attribute applies "
	       "only to C++ classes", IDENTIFIER_POINTER (name));

      *no_add_attrs = true;
    }

  return NULL_TREE;
}

/* Handle a "weak_import" attribute; arguments as in
   struct attribute_spec.handler.  */

tree
darwin_handle_weak_import_attribute (tree *node, tree name,
				     tree ARG_UNUSED (args),
				     int ARG_UNUSED (flags),
				     bool * no_add_attrs)
{
  if (TREE_CODE (*node) != FUNCTION_DECL && TREE_CODE (*node) != VAR_DECL)
    {
      warning (OPT_Wattributes, "%qs attribute ignored",
	       IDENTIFIER_POINTER (name));
      *no_add_attrs = true;
    }
  else
    declare_weak (*node);

  return NULL_TREE;
}

static void
no_dead_strip (FILE *file, const char *lab)
{
  fprintf (file, ".no_dead_strip %s\n", lab);
}

/* Emit a label for an FDE, making it global and/or weak if appropriate.
   The third parameter is nonzero if this is for exception handling.
   The fourth parameter is nonzero if this is just a placeholder for an
   FDE that we are omitting. */

void
darwin_emit_unwind_label (FILE *file, tree decl, int for_eh, int empty)
{
  const char *base;
  char *lab;
  bool need_quotes;

  if (DECL_ASSEMBLER_NAME_SET_P (decl))
    base = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  else
    base = IDENTIFIER_POINTER (DECL_NAME (decl));

  base = targetm.strip_name_encoding (base);
  need_quotes = name_needs_quotes (base);

  if (! for_eh)
    return;

  lab = concat (need_quotes ? "\"" : "", user_label_prefix, base, ".eh",
		need_quotes ? "\"" : "", NULL);

  if (TREE_PUBLIC (decl))
    fprintf (file, "\t%s %s\n",
	     (DECL_VISIBILITY (decl) != VISIBILITY_HIDDEN
	      ? ".globl"
	      : ".private_extern"),
	     lab);

  if (DECL_WEAK (decl))
    fprintf (file, "\t.weak_definition %s\n", lab);

  if (empty)
    {
      fprintf (file, "%s = 0\n", lab);

      /* Mark the absolute .eh and .eh1 style labels as needed to
	 ensure that we don't dead code strip them and keep such
	 labels from another instantiation point until we can fix this
	 properly with group comdat support.  */
      no_dead_strip (file, lab);
    }
  else
    fprintf (file, "%s:\n", lab);

  free (lab);
}

static GTY(()) unsigned long except_table_label_num;

void
darwin_emit_except_table_label (FILE *file)
{
  char section_start_label[30];

  ASM_GENERATE_INTERNAL_LABEL (section_start_label, "GCC_except_table",
			       except_table_label_num++);
  ASM_OUTPUT_LABEL (file, section_start_label);
}
/* Generate a PC-relative reference to a Mach-O non-lazy-symbol.  */

void
darwin_non_lazy_pcrel (FILE *file, rtx addr)
{
  const char *nlp_name;

  gcc_assert (GET_CODE (addr) == SYMBOL_REF);

  nlp_name = machopic_indirection_name (addr, /*stub_p=*/false);
  fputs ("\t.long\t", file);
  ASM_OUTPUT_LABELREF (file, nlp_name);
  fputs ("-.", file);
}

/* Emit an assembler directive to set visibility for a symbol.  The
   only supported visibilities are VISIBILITY_DEFAULT and
   VISIBILITY_HIDDEN; the latter corresponds to Darwin's "private
   extern".  There is no MACH-O equivalent of ELF's
   VISIBILITY_INTERNAL or VISIBILITY_PROTECTED. */

void
darwin_assemble_visibility (tree decl, int vis)
{
  if (vis == VISIBILITY_DEFAULT)
    ;
  else if (vis == VISIBILITY_HIDDEN)
    {
      fputs ("\t.private_extern ", asm_out_file);
      assemble_name (asm_out_file,
		     (IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl))));
      fputs ("\n", asm_out_file);
    }
  else
    warning (OPT_Wattributes, "internal and protected visibility attributes "
	     "not supported in this configuration; ignored");
}

/* Output a difference of two labels that will be an assembly time
   constant if the two labels are local.  (.long lab1-lab2 will be
   very different if lab1 is at the boundary between two sections; it
   will be relocated according to the second section, not the first,
   so one ends up with a difference between labels in different
   sections, which is bad in the dwarf2 eh context for instance.)  */

static int darwin_dwarf_label_counter;

void
darwin_asm_output_dwarf_delta (FILE *file, int size,
			       const char *lab1, const char *lab2)
{
  int islocaldiff = (lab1[0] == '*' && lab1[1] == 'L'
		     && lab2[0] == '*' && lab2[1] == 'L');
  const char *directive = (size == 8 ? ".quad" : ".long");

  if (islocaldiff)
    fprintf (file, "\t.set L$set$%d,", darwin_dwarf_label_counter);
  else
    fprintf (file, "\t%s\t", directive);
  assemble_name_raw (file, lab1);
  fprintf (file, "-");
  assemble_name_raw (file, lab2);
  if (islocaldiff)
    fprintf (file, "\n\t%s L$set$%d", directive, darwin_dwarf_label_counter++);
}

/* Output labels for the start of the DWARF sections if necessary.  */
void
darwin_file_start (void)
{
  if (write_symbols == DWARF2_DEBUG)
    {
      static const char * const debugnames[] =
	{
	  DEBUG_FRAME_SECTION,
	  DEBUG_INFO_SECTION,
	  DEBUG_ABBREV_SECTION,
	  DEBUG_ARANGES_SECTION,
	  DEBUG_MACINFO_SECTION,
	  DEBUG_LINE_SECTION,
	  DEBUG_LOC_SECTION,
	  DEBUG_PUBNAMES_SECTION,
	  DEBUG_STR_SECTION,
	  DEBUG_RANGES_SECTION
	};
      size_t i;

      for (i = 0; i < ARRAY_SIZE (debugnames); i++)
	{
	  int namelen;

	  switch_to_section (get_section (debugnames[i], SECTION_DEBUG, NULL));

	  gcc_assert (strncmp (debugnames[i], "__DWARF,", 8) == 0);
	  gcc_assert (strchr (debugnames[i] + 8, ','));

	  namelen = strchr (debugnames[i] + 8, ',') - (debugnames[i] + 8);
	  fprintf (asm_out_file, "Lsection%.*s:\n", namelen, debugnames[i] + 8);
	}
    }
}

/* Output an offset in a DWARF section on Darwin.  On Darwin, DWARF section
   offsets are not represented using relocs in .o files; either the
   section never leaves the .o file, or the linker or other tool is
   responsible for parsing the DWARF and updating the offsets.  */

void
darwin_asm_output_dwarf_offset (FILE *file, int size, const char * lab,
				section *base)
{
  char sname[64];
  int namelen;

  gcc_assert (base->common.flags & SECTION_NAMED);
  gcc_assert (strncmp (base->named.name, "__DWARF,", 8) == 0);
  gcc_assert (strchr (base->named.name + 8, ','));

  namelen = strchr (base->named.name + 8, ',') - (base->named.name + 8);
  sprintf (sname, "*Lsection%.*s", namelen, base->named.name + 8);
  darwin_asm_output_dwarf_delta (file, size, lab, sname);
}

void
darwin_file_end (void)
{
  machopic_finish (asm_out_file);
  if (strcmp (lang_hooks.name, "GNU C++") == 0)
    {
      switch_to_section (darwin_sections[constructor_section]);
      switch_to_section (darwin_sections[destructor_section]);
      ASM_OUTPUT_ALIGN (asm_out_file, 1);
    }
  fprintf (asm_out_file, "\t.subsections_via_symbols\n");
}

/* TODO: Add a language hook for identifying if a decl is a vtable.  */
#define DARWIN_VTABLE_P(DECL) 0

/* Cross-module name binding.  Darwin does not support overriding
   functions at dynamic-link time, except for vtables in kexts.  */

bool
darwin_binds_local_p (tree decl)
{
  return default_binds_local_p_1 (decl,
				  TARGET_KEXTABI && DARWIN_VTABLE_P (decl));
}

#if 0
/* See TARGET_ASM_OUTPUT_ANCHOR for why we can't do this yet.  */
/* The Darwin's implementation of TARGET_ASM_OUTPUT_ANCHOR.  Define the
   anchor relative to ".", the current section position.  We cannot use
   the default one because ASM_OUTPUT_DEF is wrong for Darwin.  */

void
darwin_asm_output_anchor (rtx symbol)
{
  fprintf (asm_out_file, "\t.set\t");
  assemble_name (asm_out_file, XSTR (symbol, 0));
  fprintf (asm_out_file, ", . + " HOST_WIDE_INT_PRINT_DEC "\n",
	   SYMBOL_REF_BLOCK_OFFSET (symbol));
}
#endif

/* Set the darwin specific attributes on TYPE.  */
void
darwin_set_default_type_attributes (tree type)
{
  if (darwin_ms_struct
      && TREE_CODE (type) == RECORD_TYPE)
    TYPE_ATTRIBUTES (type) = tree_cons (get_identifier ("ms_struct"),
                                        NULL_TREE,
                                        TYPE_ATTRIBUTES (type));
}

/* True, iff we're generating code for loadable kernel extentions.  */

bool
darwin_kextabi_p (void) {
  return flag_apple_kext;
}

void
darwin_override_options (void)
{
  if (flag_apple_kext && strcmp (lang_hooks.name, "GNU C++") != 0)
    {
      warning (0, "command line option %<-fapple-kext%> is only valid for C++");
      flag_apple_kext = 0;
    }
  if (flag_mkernel || flag_apple_kext)
    {
      /* -mkernel implies -fapple-kext for C++ */
      if (strcmp (lang_hooks.name, "GNU C++") == 0)
	flag_apple_kext = 1;

      flag_no_common = 1;

      /* No EH in kexts.  */
      flag_exceptions = 0;
      /* No -fnon-call-exceptions data in kexts.  */
      flag_non_call_exceptions = 0;
    }
}

#include "gt-darwin.h"
