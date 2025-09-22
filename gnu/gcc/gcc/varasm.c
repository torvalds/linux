/* Output variables, constants and external declarations, for GNU compiler.
   Copyright (C) 1987, 1988, 1989, 1992, 1993, 1994, 1995, 1996, 1997,
   1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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


/* This file handles generation of all the assembler code
   *except* the instructions of a function.
   This includes declarations of variables and their initial values.

   We also output the assembler code for constants stored in memory
   and are responsible for combining constants with the same value.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "hard-reg-set.h"
#include "regs.h"
#include "real.h"
#include "output.h"
#include "toplev.h"
#include "hashtab.h"
#include "c-pragma.h"
#include "ggc.h"
#include "langhooks.h"
#include "tm_p.h"
#include "debug.h"
#include "target.h"
#include "tree-mudflap.h"
#include "cgraph.h"
#include "cfglayout.h"
#include "basic-block.h"

#ifdef XCOFF_DEBUGGING_INFO
#include "xcoffout.h"		/* Needed for external data
				   declarations for e.g. AIX 4.x.  */
#endif

/* The (assembler) name of the first globally-visible object output.  */
extern GTY(()) const char *first_global_object_name;
extern GTY(()) const char *weak_global_object_name;

const char *first_global_object_name;
const char *weak_global_object_name;

struct addr_const;
struct constant_descriptor_rtx;
struct rtx_constant_pool;

struct varasm_status GTY(())
{
  /* If we're using a per-function constant pool, this is it.  */
  struct rtx_constant_pool *pool;

  /* Number of tree-constants deferred during the expansion of this
     function.  */
  unsigned int deferred_constants;
};

#define n_deferred_constants (cfun->varasm->deferred_constants)

/* Number for making the label on the next
   constant that is stored in memory.  */

static GTY(()) int const_labelno;

/* Carry information from ASM_DECLARE_OBJECT_NAME
   to ASM_FINISH_DECLARE_OBJECT.  */

int size_directive_output;

/* The last decl for which assemble_variable was called,
   if it did ASM_DECLARE_OBJECT_NAME.
   If the last call to assemble_variable didn't do that,
   this holds 0.  */

tree last_assemble_variable_decl;

/* The following global variable indicates if the first basic block
   in a function belongs to the cold partition or not.  */

bool first_function_block_is_cold;

/* We give all constants their own alias set.  Perhaps redundant with
   MEM_READONLY_P, but pre-dates it.  */

static HOST_WIDE_INT const_alias_set;

static const char *strip_reg_name (const char *);
static int contains_pointers_p (tree);
#ifdef ASM_OUTPUT_EXTERNAL
static bool incorporeal_function_p (tree);
#endif
static void decode_addr_const (tree, struct addr_const *);
static hashval_t const_desc_hash (const void *);
static int const_desc_eq (const void *, const void *);
static hashval_t const_hash_1 (const tree);
static int compare_constant (const tree, const tree);
static tree copy_constant (tree);
static void output_constant_def_contents (rtx);
static void output_addressed_constants (tree);
static unsigned HOST_WIDE_INT array_size_for_constructor (tree);
static unsigned min_align (unsigned, unsigned);
static void output_constructor (tree, unsigned HOST_WIDE_INT, unsigned int);
static void globalize_decl (tree);
#ifdef BSS_SECTION_ASM_OP
#ifdef ASM_OUTPUT_BSS
static void asm_output_bss (FILE *, tree, const char *,
			    unsigned HOST_WIDE_INT, unsigned HOST_WIDE_INT);
#endif
#ifdef ASM_OUTPUT_ALIGNED_BSS
static void asm_output_aligned_bss (FILE *, tree, const char *,
				    unsigned HOST_WIDE_INT, int)
     ATTRIBUTE_UNUSED;
#endif
#endif /* BSS_SECTION_ASM_OP */
static void mark_weak (tree);
static void output_constant_pool (const char *, tree);

/* Well-known sections, each one associated with some sort of *_ASM_OP.  */
section *text_section;
section *data_section;
section *readonly_data_section;
section *sdata_section;
section *ctors_section;
section *dtors_section;
section *bss_section;
section *sbss_section;

/* Various forms of common section.  All are guaranteed to be nonnull.  */
section *tls_comm_section;
section *comm_section;
section *lcomm_section;

/* A SECTION_NOSWITCH section used for declaring global BSS variables.
   May be null.  */
section *bss_noswitch_section;

/* The section that holds the main exception table, when known.  The section
   is set either by the target's init_sections hook or by the first call to
   switch_to_exception_section.  */
section *exception_section;

/* The section that holds the DWARF2 frame unwind information, when known.
   The section is set either by the target's init_sections hook or by the
   first call to switch_to_eh_frame_section.  */
section *eh_frame_section;

/* asm_out_file's current section.  This is NULL if no section has yet
   been selected or if we lose track of what the current section is.  */
section *in_section;

/* True if code for the current function is currently being directed
   at the cold section.  */
bool in_cold_section_p;

/* A linked list of all the unnamed sections.  */
static GTY(()) section *unnamed_sections;

/* Return a nonzero value if DECL has a section attribute.  */
#ifndef IN_NAMED_SECTION
#define IN_NAMED_SECTION(DECL) \
  ((TREE_CODE (DECL) == FUNCTION_DECL || TREE_CODE (DECL) == VAR_DECL) \
   && DECL_SECTION_NAME (DECL) != NULL_TREE)
#endif

/* Hash table of named sections.  */
static GTY((param_is (section))) htab_t section_htab;

/* A table of object_blocks, indexed by section.  */
static GTY((param_is (struct object_block))) htab_t object_block_htab;

/* The next number to use for internal anchor labels.  */
static GTY(()) int anchor_labelno;

/* A pool of constants that can be shared between functions.  */
static GTY(()) struct rtx_constant_pool *shared_constant_pool;

/* Helper routines for maintaining section_htab.  */

static int
section_entry_eq (const void *p1, const void *p2)
{
  const section *old = p1;
  const char *new = p2;

  return strcmp (old->named.name, new) == 0;
}

static hashval_t
section_entry_hash (const void *p)
{
  const section *old = p;
  return htab_hash_string (old->named.name);
}

/* Return a hash value for section SECT.  */

static hashval_t
hash_section (section *sect)
{
  if (sect->common.flags & SECTION_NAMED)
    return htab_hash_string (sect->named.name);
  return sect->common.flags;
}

/* Helper routines for maintaining object_block_htab.  */

static int
object_block_entry_eq (const void *p1, const void *p2)
{
  const struct object_block *old = p1;
  const section *new = p2;

  return old->sect == new;
}

static hashval_t
object_block_entry_hash (const void *p)
{
  const struct object_block *old = p;
  return hash_section (old->sect);
}

/* Return a new unnamed section with the given fields.  */

section *
get_unnamed_section (unsigned int flags, void (*callback) (const void *),
		     const void *data)
{
  section *sect;

  sect = ggc_alloc (sizeof (struct unnamed_section));
  sect->unnamed.common.flags = flags | SECTION_UNNAMED;
  sect->unnamed.callback = callback;
  sect->unnamed.data = data;
  sect->unnamed.next = unnamed_sections;

  unnamed_sections = sect;
  return sect;
}

/* Return a SECTION_NOSWITCH section with the given fields.  */

static section *
get_noswitch_section (unsigned int flags, noswitch_section_callback callback)
{
  section *sect;

  sect = ggc_alloc (sizeof (struct unnamed_section));
  sect->noswitch.common.flags = flags | SECTION_NOSWITCH;
  sect->noswitch.callback = callback;

  return sect;
}

/* Return the named section structure associated with NAME.  Create
   a new section with the given fields if no such structure exists.  */

section *
get_section (const char *name, unsigned int flags, tree decl)
{
  section *sect, **slot;

  slot = (section **)
    htab_find_slot_with_hash (section_htab, name,
			      htab_hash_string (name), INSERT);
  flags |= SECTION_NAMED;
  if (*slot == NULL)
    {
      sect = ggc_alloc (sizeof (struct named_section));
      sect->named.common.flags = flags;
      sect->named.name = ggc_strdup (name);
      sect->named.decl = decl;
      *slot = sect;
    }
  else
    {
      sect = *slot;
      if ((sect->common.flags & ~SECTION_DECLARED) != flags
	  && ((sect->common.flags | flags) & SECTION_OVERRIDE) == 0)
	{
	  /* Sanity check user variables for flag changes.  */
	  if (decl == 0)
	    decl = sect->named.decl;
	  gcc_assert (decl);
	  error ("%+D causes a section type conflict", decl);
	}
    }
  return sect;
}

/* Return true if the current compilation mode benefits from having
   objects grouped into blocks.  */

static bool
use_object_blocks_p (void)
{
  return flag_section_anchors;
}

/* Return the object_block structure for section SECT.  Create a new
   structure if we haven't created one already.  Return null if SECT
   itself is null.  */

static struct object_block *
get_block_for_section (section *sect)
{
  struct object_block *block;
  void **slot;

  if (sect == NULL)
    return NULL;

  slot = htab_find_slot_with_hash (object_block_htab, sect,
				   hash_section (sect), INSERT);
  block = (struct object_block *) *slot;
  if (block == NULL)
    {
      block = (struct object_block *)
	ggc_alloc_cleared (sizeof (struct object_block));
      block->sect = sect;
      *slot = block;
    }
  return block;
}

/* Create a symbol with label LABEL and place it at byte offset
   OFFSET in BLOCK.  OFFSET can be negative if the symbol's offset
   is not yet known.  LABEL must be a garbage-collected string.  */

static rtx
create_block_symbol (const char *label, struct object_block *block,
		     HOST_WIDE_INT offset)
{
  rtx symbol;
  unsigned int size;

  /* Create the extended SYMBOL_REF.  */
  size = RTX_HDR_SIZE + sizeof (struct block_symbol);
  symbol = ggc_alloc_zone (size, &rtl_zone);

  /* Initialize the normal SYMBOL_REF fields.  */
  memset (symbol, 0, size);
  PUT_CODE (symbol, SYMBOL_REF);
  PUT_MODE (symbol, Pmode);
  XSTR (symbol, 0) = label;
  SYMBOL_REF_FLAGS (symbol) = SYMBOL_FLAG_HAS_BLOCK_INFO;

  /* Initialize the block_symbol stuff.  */
  SYMBOL_REF_BLOCK (symbol) = block;
  SYMBOL_REF_BLOCK_OFFSET (symbol) = offset;

  return symbol;
}

static void
initialize_cold_section_name (void)
{
  const char *stripped_name;
  char *name, *buffer;
  tree dsn;

  gcc_assert (cfun && current_function_decl);
  if (cfun->unlikely_text_section_name)
    return;

  dsn = DECL_SECTION_NAME (current_function_decl);
  if (flag_function_sections && dsn)
    {
      name = alloca (TREE_STRING_LENGTH (dsn) + 1);
      memcpy (name, TREE_STRING_POINTER (dsn), TREE_STRING_LENGTH (dsn) + 1);

      stripped_name = targetm.strip_name_encoding (name);

      buffer = ACONCAT ((stripped_name, "_unlikely", NULL));
      cfun->unlikely_text_section_name = ggc_strdup (buffer);
    }
  else
    cfun->unlikely_text_section_name =  UNLIKELY_EXECUTED_TEXT_SECTION_NAME;
}

/* Tell assembler to switch to unlikely-to-be-executed text section.  */

section *
unlikely_text_section (void)
{
  if (cfun)
    {
      if (!cfun->unlikely_text_section_name)
	initialize_cold_section_name ();

      return get_named_section (NULL, cfun->unlikely_text_section_name, 0);
    }
  else
    return get_named_section (NULL, UNLIKELY_EXECUTED_TEXT_SECTION_NAME, 0);
}

/* When called within a function context, return true if the function
   has been assigned a cold text section and if SECT is that section.
   When called outside a function context, return true if SECT is the
   default cold section.  */

bool
unlikely_text_section_p (section *sect)
{
  const char *name;

  if (cfun)
    name = cfun->unlikely_text_section_name;
  else
    name = UNLIKELY_EXECUTED_TEXT_SECTION_NAME;

  return (name
	  && sect
	  && SECTION_STYLE (sect) == SECTION_NAMED
	  && strcmp (name, sect->named.name) == 0);
}

/* Return a section with a particular name and with whatever SECTION_*
   flags section_type_flags deems appropriate.  The name of the section
   is taken from NAME if nonnull, otherwise it is taken from DECL's
   DECL_SECTION_NAME.  DECL is the decl associated with the section
   (see the section comment for details) and RELOC is as for
   section_type_flags.  */

section *
get_named_section (tree decl, const char *name, int reloc)
{
  unsigned int flags;

  gcc_assert (!decl || DECL_P (decl));
  if (name == NULL)
    name = TREE_STRING_POINTER (DECL_SECTION_NAME (decl));

  flags = targetm.section_type_flags (decl, name, reloc);

  return get_section (name, flags, decl);
}

/* If required, set DECL_SECTION_NAME to a unique name.  */

void
resolve_unique_section (tree decl, int reloc ATTRIBUTE_UNUSED,
			int flag_function_or_data_sections)
{
  if (DECL_SECTION_NAME (decl) == NULL_TREE
      && targetm.have_named_sections
      && (flag_function_or_data_sections
	  || DECL_ONE_ONLY (decl)))
    targetm.asm_out.unique_section (decl, reloc);
}

#ifdef BSS_SECTION_ASM_OP

#ifdef ASM_OUTPUT_BSS

/* Utility function for ASM_OUTPUT_BSS for targets to use if
   they don't support alignments in .bss.
   ??? It is believed that this function will work in most cases so such
   support is localized here.  */

static void
asm_output_bss (FILE *file, tree decl ATTRIBUTE_UNUSED,
		const char *name,
		unsigned HOST_WIDE_INT size ATTRIBUTE_UNUSED,
		unsigned HOST_WIDE_INT rounded)
{
  targetm.asm_out.globalize_label (file, name);
  switch_to_section (bss_section);
#ifdef ASM_DECLARE_OBJECT_NAME
  last_assemble_variable_decl = decl;
  ASM_DECLARE_OBJECT_NAME (file, name, decl);
#else
  /* Standard thing is just output label for the object.  */
  ASM_OUTPUT_LABEL (file, name);
#endif /* ASM_DECLARE_OBJECT_NAME */
  ASM_OUTPUT_SKIP (file, rounded ? rounded : 1);
}

#endif

#ifdef ASM_OUTPUT_ALIGNED_BSS

/* Utility function for targets to use in implementing
   ASM_OUTPUT_ALIGNED_BSS.
   ??? It is believed that this function will work in most cases so such
   support is localized here.  */

static void
asm_output_aligned_bss (FILE *file, tree decl ATTRIBUTE_UNUSED,
			const char *name, unsigned HOST_WIDE_INT size,
			int align)
{
  switch_to_section (bss_section);
  ASM_OUTPUT_ALIGN (file, floor_log2 (align / BITS_PER_UNIT));
#ifdef ASM_DECLARE_OBJECT_NAME
  last_assemble_variable_decl = decl;
  ASM_DECLARE_OBJECT_NAME (file, name, decl);
#else
  /* Standard thing is just output label for the object.  */
  ASM_OUTPUT_LABEL (file, name);
#endif /* ASM_DECLARE_OBJECT_NAME */
  ASM_OUTPUT_SKIP (file, size ? size : 1);
}

#endif

#endif /* BSS_SECTION_ASM_OP */

#ifndef USE_SELECT_SECTION_FOR_FUNCTIONS
/* Return the hot section for function DECL.  Return text_section for
   null DECLs.  */

static section *
hot_function_section (tree decl)
{
  if (decl != NULL_TREE
      && DECL_SECTION_NAME (decl) != NULL_TREE
      && targetm.have_named_sections)
    return get_named_section (decl, NULL, 0);
  else
    return text_section;
}
#endif

/* Return the section for function DECL.

   If DECL is NULL_TREE, return the text section.  We can be passed
   NULL_TREE under some circumstances by dbxout.c at least.  */

section *
function_section (tree decl)
{
  int reloc = 0;

  if (first_function_block_is_cold)
    reloc = 1;

#ifdef USE_SELECT_SECTION_FOR_FUNCTIONS
  if (decl != NULL_TREE
      && DECL_SECTION_NAME (decl) != NULL_TREE)
    return reloc ? unlikely_text_section ()
		 : get_named_section (decl, NULL, 0);
  else
    return targetm.asm_out.select_section (decl, reloc, DECL_ALIGN (decl));
#else
  return reloc ? unlikely_text_section () : hot_function_section (decl);
#endif
}

section *
current_function_section (void)
{
#ifdef USE_SELECT_SECTION_FOR_FUNCTIONS
  if (current_function_decl != NULL_TREE
      && DECL_SECTION_NAME (current_function_decl) != NULL_TREE)
    return in_cold_section_p ? unlikely_text_section ()
			     : get_named_section (current_function_decl,
						  NULL, 0);
  else
    return targetm.asm_out.select_section (current_function_decl,
					   in_cold_section_p,
					   DECL_ALIGN (current_function_decl));
#else
  return (in_cold_section_p
	  ? unlikely_text_section ()
	  : hot_function_section (current_function_decl));
#endif
}

/* Return the read-only data section associated with function DECL.  */

section *
default_function_rodata_section (tree decl)
{
  if (decl != NULL_TREE && DECL_SECTION_NAME (decl))
    {
      const char *name = TREE_STRING_POINTER (DECL_SECTION_NAME (decl));

      if (DECL_ONE_ONLY (decl) && HAVE_COMDAT_GROUP)
        {
	  size_t len = strlen (name) + 3;
	  char* rname = alloca (len);

	  strcpy (rname, ".rodata");
	  strcat (rname, name + 5);
	  return get_section (rname, SECTION_LINKONCE, decl);
	}
      /* For .gnu.linkonce.t.foo we want to use .gnu.linkonce.r.foo.  */
      else if (DECL_ONE_ONLY (decl)
	       && strncmp (name, ".gnu.linkonce.t.", 16) == 0)
	{
	  size_t len = strlen (name) + 1;
	  char *rname = alloca (len);

	  memcpy (rname, name, len);
	  rname[14] = 'r';
	  return get_section (rname, SECTION_LINKONCE, decl);
	}
      /* For .text.foo we want to use .rodata.foo.  */
      else if (flag_function_sections && flag_data_sections
	       && strncmp (name, ".text.", 6) == 0)
	{
	  size_t len = strlen (name) + 1;
	  char *rname = alloca (len + 2);

	  memcpy (rname, ".rodata", 7);
	  memcpy (rname + 7, name + 5, len - 5);
	  return get_section (rname, 0, decl);
	}
    }

  return readonly_data_section;
}

/* Return the read-only data section associated with function DECL
   for targets where that section should be always the single
   readonly data section.  */

section *
default_no_function_rodata_section (tree decl ATTRIBUTE_UNUSED)
{
  return readonly_data_section;
}

/* Return the section to use for string merging.  */

static section *
mergeable_string_section (tree decl ATTRIBUTE_UNUSED,
			  unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED,
			  unsigned int flags ATTRIBUTE_UNUSED)
{
  HOST_WIDE_INT len;

  if (HAVE_GAS_SHF_MERGE && flag_merge_constants
      && TREE_CODE (decl) == STRING_CST
      && TREE_CODE (TREE_TYPE (decl)) == ARRAY_TYPE
      && align <= 256
      && (len = int_size_in_bytes (TREE_TYPE (decl))) > 0
      && TREE_STRING_LENGTH (decl) >= len)
    {
      enum machine_mode mode;
      unsigned int modesize;
      const char *str;
      HOST_WIDE_INT i;
      int j, unit;
      char name[30];

      mode = TYPE_MODE (TREE_TYPE (TREE_TYPE (decl)));
      modesize = GET_MODE_BITSIZE (mode);
      if (modesize >= 8 && modesize <= 256
	  && (modesize & (modesize - 1)) == 0)
	{
	  if (align < modesize)
	    align = modesize;

	  str = TREE_STRING_POINTER (decl);
	  unit = GET_MODE_SIZE (mode);

	  /* Check for embedded NUL characters.  */
	  for (i = 0; i < len; i += unit)
	    {
	      for (j = 0; j < unit; j++)
		if (str[i + j] != '\0')
		  break;
	      if (j == unit)
		break;
	    }
	  if (i == len - unit)
	    {
	      sprintf (name, ".rodata.str%d.%d", modesize / 8,
		       (int) (align / 8));
	      flags |= (modesize / 8) | SECTION_MERGE | SECTION_STRINGS;
	      return get_section (name, flags, NULL);
	    }
	}
    }

  return readonly_data_section;
}

/* Return the section to use for constant merging.  */

section *
mergeable_constant_section (enum machine_mode mode ATTRIBUTE_UNUSED,
			    unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED,
			    unsigned int flags ATTRIBUTE_UNUSED)
{
  unsigned int modesize = GET_MODE_BITSIZE (mode);

  if (HAVE_GAS_SHF_MERGE && flag_merge_constants
      && mode != VOIDmode
      && mode != BLKmode
      && modesize <= align
      && align >= 8
      && align <= 256
      && (align & (align - 1)) == 0)
    {
      char name[24];

      sprintf (name, ".rodata.cst%d", (int) (align / 8));
      flags |= (align / 8) | SECTION_MERGE;
      return get_section (name, flags, NULL);
    }
  return readonly_data_section;
}

/* Given NAME, a putative register name, discard any customary prefixes.  */

static const char *
strip_reg_name (const char *name)
{
#ifdef REGISTER_PREFIX
  if (!strncmp (name, REGISTER_PREFIX, strlen (REGISTER_PREFIX)))
    name += strlen (REGISTER_PREFIX);
#endif
  if (name[0] == '%' || name[0] == '#')
    name++;
  return name;
}

/* The user has asked for a DECL to have a particular name.  Set (or
   change) it in such a way that we don't prefix an underscore to
   it.  */
void
set_user_assembler_name (tree decl, const char *name)
{
  char *starred = alloca (strlen (name) + 2);
  starred[0] = '*';
  strcpy (starred + 1, name);
  change_decl_assembler_name (decl, get_identifier (starred));
  SET_DECL_RTL (decl, NULL_RTX);
}

/* Decode an `asm' spec for a declaration as a register name.
   Return the register number, or -1 if nothing specified,
   or -2 if the ASMSPEC is not `cc' or `memory' and is not recognized,
   or -3 if ASMSPEC is `cc' and is not recognized,
   or -4 if ASMSPEC is `memory' and is not recognized.
   Accept an exact spelling or a decimal number.
   Prefixes such as % are optional.  */

int
decode_reg_name (const char *asmspec)
{
  if (asmspec != 0)
    {
      int i;

      /* Get rid of confusing prefixes.  */
      asmspec = strip_reg_name (asmspec);

      /* Allow a decimal number as a "register name".  */
      for (i = strlen (asmspec) - 1; i >= 0; i--)
	if (! ISDIGIT (asmspec[i]))
	  break;
      if (asmspec[0] != 0 && i < 0)
	{
	  i = atoi (asmspec);
	  if (i < FIRST_PSEUDO_REGISTER && i >= 0)
	    return i;
	  else
	    return -2;
	}

      for (i = 0; i < FIRST_PSEUDO_REGISTER; i++)
	if (reg_names[i][0]
	    && ! strcmp (asmspec, strip_reg_name (reg_names[i])))
	  return i;

#ifdef ADDITIONAL_REGISTER_NAMES
      {
	static const struct { const char *const name; const int number; } table[]
	  = ADDITIONAL_REGISTER_NAMES;

	for (i = 0; i < (int) ARRAY_SIZE (table); i++)
	  if (table[i].name[0]
	      && ! strcmp (asmspec, table[i].name))
	    return table[i].number;
      }
#endif /* ADDITIONAL_REGISTER_NAMES */

      if (!strcmp (asmspec, "memory"))
	return -4;

      if (!strcmp (asmspec, "cc"))
	return -3;

      return -2;
    }

  return -1;
}

/* Return true if DECL's initializer is suitable for a BSS section.  */

static bool
bss_initializer_p (tree decl)
{
  return (DECL_INITIAL (decl) == NULL
	  || DECL_INITIAL (decl) == error_mark_node
	  || (flag_zero_initialized_in_bss
	      /* Leave constant zeroes in .rodata so they
		 can be shared.  */
	      && !TREE_READONLY (decl)
	      && initializer_zerop (DECL_INITIAL (decl))));
}

/* Compute the alignment of variable specified by DECL.
   DONT_OUTPUT_DATA is from assemble_variable.  */

void
align_variable (tree decl, bool dont_output_data)
{
  unsigned int align = DECL_ALIGN (decl);

  /* In the case for initialing an array whose length isn't specified,
     where we have not yet been able to do the layout,
     figure out the proper alignment now.  */
  if (dont_output_data && DECL_SIZE (decl) == 0
      && TREE_CODE (TREE_TYPE (decl)) == ARRAY_TYPE)
    align = MAX (align, TYPE_ALIGN (TREE_TYPE (TREE_TYPE (decl))));

  /* Some object file formats have a maximum alignment which they support.
     In particular, a.out format supports a maximum alignment of 4.  */
  if (align > MAX_OFILE_ALIGNMENT)
    {
      warning (0, "alignment of %q+D is greater than maximum object "
               "file alignment.  Using %d", decl,
	       MAX_OFILE_ALIGNMENT/BITS_PER_UNIT);
      align = MAX_OFILE_ALIGNMENT;
    }

  /* On some machines, it is good to increase alignment sometimes.  */
  if (! DECL_USER_ALIGN (decl))
    {
#ifdef DATA_ALIGNMENT
      align = DATA_ALIGNMENT (TREE_TYPE (decl), align);
#endif
#ifdef CONSTANT_ALIGNMENT
      if (DECL_INITIAL (decl) != 0 && DECL_INITIAL (decl) != error_mark_node)
	align = CONSTANT_ALIGNMENT (DECL_INITIAL (decl), align);
#endif
    }

  /* Reset the alignment in case we have made it tighter, so we can benefit
     from it in get_pointer_alignment.  */
  DECL_ALIGN (decl) = align;
}

/* Return the section into which the given VAR_DECL or CONST_DECL
   should be placed.  PREFER_NOSWITCH_P is true if a noswitch
   section should be used wherever possible.  */

static section *
get_variable_section (tree decl, bool prefer_noswitch_p)
{
  int reloc;

  /* If the decl has been given an explicit section name, then it
     isn't common, and shouldn't be handled as such.  */
  if (DECL_COMMON (decl) && DECL_SECTION_NAME (decl) == NULL)
    {
      if (DECL_THREAD_LOCAL_P (decl))
	return tls_comm_section;
      if (TREE_PUBLIC (decl) && bss_initializer_p (decl))
	return comm_section;
    }

  if (DECL_INITIAL (decl) == error_mark_node)
    reloc = contains_pointers_p (TREE_TYPE (decl)) ? 3 : 0;
  else if (DECL_INITIAL (decl))
    reloc = compute_reloc_for_constant (DECL_INITIAL (decl));
  else
    reloc = 0;

  resolve_unique_section (decl, reloc, flag_data_sections);
  if (IN_NAMED_SECTION (decl))
    return get_named_section (decl, NULL, reloc);

  if (!DECL_THREAD_LOCAL_P (decl)
      && !(prefer_noswitch_p && targetm.have_switchable_bss_sections)
      && bss_initializer_p (decl))
    {
      if (!TREE_PUBLIC (decl))
	return lcomm_section;
      if (bss_noswitch_section)
	return bss_noswitch_section;
    }

  return targetm.asm_out.select_section (decl, reloc, DECL_ALIGN (decl));
}

/* Return the block into which object_block DECL should be placed.  */

static struct object_block *
get_block_for_decl (tree decl)
{
  section *sect;

  if (TREE_CODE (decl) == VAR_DECL)
    {
      /* The object must be defined in this translation unit.  */
      if (DECL_EXTERNAL (decl))
	return NULL;

      /* There's no point using object blocks for something that is
	 isolated by definition.  */
      if (DECL_ONE_ONLY (decl))
	return NULL;
    }

  /* We can only calculate block offsets if the decl has a known
     constant size.  */
  if (DECL_SIZE_UNIT (decl) == NULL)
    return NULL;
  if (!host_integerp (DECL_SIZE_UNIT (decl), 1))
    return NULL;

  /* Find out which section should contain DECL.  We cannot put it into
     an object block if it requires a standalone definition.  */
  if (TREE_CODE (decl) == VAR_DECL)
      align_variable (decl, 0);
  sect = get_variable_section (decl, true);
  if (SECTION_STYLE (sect) == SECTION_NOSWITCH)
    return NULL;

  return get_block_for_section (sect);
}

/* Make sure block symbol SYMBOL is in block BLOCK.  */

static void
change_symbol_block (rtx symbol, struct object_block *block)
{
  if (block != SYMBOL_REF_BLOCK (symbol))
    {
      gcc_assert (SYMBOL_REF_BLOCK_OFFSET (symbol) < 0);
      SYMBOL_REF_BLOCK (symbol) = block;
    }
}

/* Return true if it is possible to put DECL in an object_block.  */

static bool
use_blocks_for_decl_p (tree decl)
{
  /* Only data DECLs can be placed into object blocks.  */
  if (TREE_CODE (decl) != VAR_DECL && TREE_CODE (decl) != CONST_DECL)
    return false;

  /* Detect decls created by dw2_force_const_mem.  Such decls are
     special because DECL_INITIAL doesn't specify the decl's true value.
     dw2_output_indirect_constants will instead call assemble_variable
     with dont_output_data set to 1 and then print the contents itself.  */
  if (DECL_INITIAL (decl) == decl)
    return false;

  /* If this decl is an alias, then we don't want to emit a definition.  */
  if (lookup_attribute ("alias", DECL_ATTRIBUTES (decl)))
    return false;

  return true;
}

/* Create the DECL_RTL for a VAR_DECL or FUNCTION_DECL.  DECL should
   have static storage duration.  In other words, it should not be an
   automatic variable, including PARM_DECLs.

   There is, however, one exception: this function handles variables
   explicitly placed in a particular register by the user.

   This is never called for PARM_DECL nodes.  */

void
make_decl_rtl (tree decl)
{
  const char *name = 0;
  int reg_number;
  rtx x;

  /* Check that we are not being given an automatic variable.  */
  gcc_assert (TREE_CODE (decl) != PARM_DECL
	      && TREE_CODE (decl) != RESULT_DECL);

  /* A weak alias has TREE_PUBLIC set but not the other bits.  */
  gcc_assert (TREE_CODE (decl) != VAR_DECL
	      || TREE_STATIC (decl)
	      || TREE_PUBLIC (decl)
	      || DECL_EXTERNAL (decl)
	      || DECL_REGISTER (decl));

  /* And that we were not given a type or a label.  */
  gcc_assert (TREE_CODE (decl) != TYPE_DECL
	      && TREE_CODE (decl) != LABEL_DECL);

  /* For a duplicate declaration, we can be called twice on the
     same DECL node.  Don't discard the RTL already made.  */
  if (DECL_RTL_SET_P (decl))
    {
      /* If the old RTL had the wrong mode, fix the mode.  */
      x = DECL_RTL (decl);
      if (GET_MODE (x) != DECL_MODE (decl))
	SET_DECL_RTL (decl, adjust_address_nv (x, DECL_MODE (decl), 0));

      if (TREE_CODE (decl) != FUNCTION_DECL && DECL_REGISTER (decl))
	return;

      /* ??? Another way to do this would be to maintain a hashed
	 table of such critters.  Instead of adding stuff to a DECL
	 to give certain attributes to it, we could use an external
	 hash map from DECL to set of attributes.  */

      /* Let the target reassign the RTL if it wants.
	 This is necessary, for example, when one machine specific
	 decl attribute overrides another.  */
      targetm.encode_section_info (decl, DECL_RTL (decl), false);

      /* If the symbol has a SYMBOL_REF_BLOCK field, update it based
	 on the new decl information.  */
      if (MEM_P (x)
	  && GET_CODE (XEXP (x, 0)) == SYMBOL_REF
	  && SYMBOL_REF_HAS_BLOCK_INFO_P (XEXP (x, 0)))
	change_symbol_block (XEXP (x, 0), get_block_for_decl (decl));

      /* Make this function static known to the mudflap runtime.  */
      if (flag_mudflap && TREE_CODE (decl) == VAR_DECL)
	mudflap_enqueue_decl (decl);

      return;
    }

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));

  if (name[0] != '*' && TREE_CODE (decl) != FUNCTION_DECL
      && DECL_REGISTER (decl))
    {
      error ("register name not specified for %q+D", decl);
    }
  else if (TREE_CODE (decl) != FUNCTION_DECL && DECL_REGISTER (decl))
    {
      const char *asmspec = name+1;
      reg_number = decode_reg_name (asmspec);
      /* First detect errors in declaring global registers.  */
      if (reg_number == -1)
	error ("register name not specified for %q+D", decl);
      else if (reg_number < 0)
	error ("invalid register name for %q+D", decl);
      else if (TYPE_MODE (TREE_TYPE (decl)) == BLKmode)
	error ("data type of %q+D isn%'t suitable for a register",
	       decl);
      else if (! HARD_REGNO_MODE_OK (reg_number, TYPE_MODE (TREE_TYPE (decl))))
	error ("register specified for %q+D isn%'t suitable for data type",
               decl);
      /* Now handle properly declared static register variables.  */
      else
	{
	  int nregs;

	  if (DECL_INITIAL (decl) != 0 && TREE_STATIC (decl))
	    {
	      DECL_INITIAL (decl) = 0;
	      error ("global register variable has initial value");
	    }
	  if (TREE_THIS_VOLATILE (decl))
	    warning (OPT_Wvolatile_register_var,
		     "optimization may eliminate reads and/or "
		     "writes to register variables");

	  /* If the user specified one of the eliminables registers here,
	     e.g., FRAME_POINTER_REGNUM, we don't want to get this variable
	     confused with that register and be eliminated.  This usage is
	     somewhat suspect...  */

	  SET_DECL_RTL (decl, gen_rtx_raw_REG (DECL_MODE (decl), reg_number));
	  ORIGINAL_REGNO (DECL_RTL (decl)) = reg_number;
	  REG_USERVAR_P (DECL_RTL (decl)) = 1;

	  if (TREE_STATIC (decl))
	    {
	      /* Make this register global, so not usable for anything
		 else.  */
#ifdef ASM_DECLARE_REGISTER_GLOBAL
	      name = IDENTIFIER_POINTER (DECL_NAME (decl));
	      ASM_DECLARE_REGISTER_GLOBAL (asm_out_file, decl, reg_number, name);
#endif
	      nregs = hard_regno_nregs[reg_number][DECL_MODE (decl)];
	      while (nregs > 0)
		globalize_reg (reg_number + --nregs);
	    }

	  /* As a register variable, it has no section.  */
	  return;
	}
    }
  /* Now handle ordinary static variables and functions (in memory).
     Also handle vars declared register invalidly.  */
  else if (name[0] == '*')
  {
#ifdef REGISTER_PREFIX
    if (strlen (REGISTER_PREFIX) != 0)
      {
	reg_number = decode_reg_name (name);
	if (reg_number >= 0 || reg_number == -3)
	  error ("register name given for non-register variable %q+D", decl);
      }
#endif
  }

  /* Specifying a section attribute on a variable forces it into a
     non-.bss section, and thus it cannot be common.  */
  if (TREE_CODE (decl) == VAR_DECL
      && DECL_SECTION_NAME (decl) != NULL_TREE
      && DECL_INITIAL (decl) == NULL_TREE
      && DECL_COMMON (decl))
    DECL_COMMON (decl) = 0;

  /* Variables can't be both common and weak.  */
  if (TREE_CODE (decl) == VAR_DECL && DECL_WEAK (decl))
    DECL_COMMON (decl) = 0;

  if (use_object_blocks_p () && use_blocks_for_decl_p (decl))
    x = create_block_symbol (name, get_block_for_decl (decl), -1);
  else
    x = gen_rtx_SYMBOL_REF (Pmode, name);
  SYMBOL_REF_WEAK (x) = DECL_WEAK (decl);
  SET_SYMBOL_REF_DECL (x, decl);

  x = gen_rtx_MEM (DECL_MODE (decl), x);
  if (TREE_CODE (decl) != FUNCTION_DECL)
    set_mem_attributes (x, decl, 1);
  SET_DECL_RTL (decl, x);

  /* Optionally set flags or add text to the name to record information
     such as that it is a function name.
     If the name is changed, the macro ASM_OUTPUT_LABELREF
     will have to know how to strip this information.  */
  targetm.encode_section_info (decl, DECL_RTL (decl), true);

  /* Make this function static known to the mudflap runtime.  */
  if (flag_mudflap && TREE_CODE (decl) == VAR_DECL)
    mudflap_enqueue_decl (decl);
}

/* Output a string of literal assembler code
   for an `asm' keyword used between functions.  */

void
assemble_asm (tree string)
{
  app_enable ();

  if (TREE_CODE (string) == ADDR_EXPR)
    string = TREE_OPERAND (string, 0);

  fprintf (asm_out_file, "\t%s\n", TREE_STRING_POINTER (string));
}

/* Record an element in the table of global destructors.  SYMBOL is
   a SYMBOL_REF of the function to be called; PRIORITY is a number
   between 0 and MAX_INIT_PRIORITY.  */

void
default_stabs_asm_out_destructor (rtx symbol ATTRIBUTE_UNUSED,
				  int priority ATTRIBUTE_UNUSED)
{
#if defined DBX_DEBUGGING_INFO || defined XCOFF_DEBUGGING_INFO
  /* Tell GNU LD that this is part of the static destructor set.
     This will work for any system that uses stabs, most usefully
     aout systems.  */
  dbxout_begin_simple_stabs ("___DTOR_LIST__", 22 /* N_SETT */);
  dbxout_stab_value_label (XSTR (symbol, 0));
#else
  sorry ("global destructors not supported on this target");
#endif
}

void
default_named_section_asm_out_destructor (rtx symbol, int priority)
{
  const char *section = ".dtors";
  char buf[16];

  /* ??? This only works reliably with the GNU linker.  */
  if (priority != DEFAULT_INIT_PRIORITY)
    {
      sprintf (buf, ".dtors.%.5u",
	       /* Invert the numbering so the linker puts us in the proper
		  order; constructors are run from right to left, and the
		  linker sorts in increasing order.  */
	       MAX_INIT_PRIORITY - priority);
      section = buf;
    }

  switch_to_section (get_section (section, SECTION_WRITE, NULL));
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);
}

#ifdef DTORS_SECTION_ASM_OP
void
default_dtor_section_asm_out_destructor (rtx symbol,
					 int priority ATTRIBUTE_UNUSED)
{
  switch_to_section (dtors_section);
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);
}
#endif

/* Likewise for global constructors.  */

void
default_stabs_asm_out_constructor (rtx symbol ATTRIBUTE_UNUSED,
				   int priority ATTRIBUTE_UNUSED)
{
#if defined DBX_DEBUGGING_INFO || defined XCOFF_DEBUGGING_INFO
  /* Tell GNU LD that this is part of the static destructor set.
     This will work for any system that uses stabs, most usefully
     aout systems.  */
  dbxout_begin_simple_stabs ("___CTOR_LIST__", 22 /* N_SETT */);
  dbxout_stab_value_label (XSTR (symbol, 0));
#else
  sorry ("global constructors not supported on this target");
#endif
}

void
default_named_section_asm_out_constructor (rtx symbol, int priority)
{
  const char *section = ".ctors";
  char buf[16];

  /* ??? This only works reliably with the GNU linker.  */
  if (priority != DEFAULT_INIT_PRIORITY)
    {
      sprintf (buf, ".ctors.%.5u",
	       /* Invert the numbering so the linker puts us in the proper
		  order; constructors are run from right to left, and the
		  linker sorts in increasing order.  */
	       MAX_INIT_PRIORITY - priority);
      section = buf;
    }

  switch_to_section (get_section (section, SECTION_WRITE, NULL));
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);
}

#ifdef CTORS_SECTION_ASM_OP
void
default_ctor_section_asm_out_constructor (rtx symbol,
					  int priority ATTRIBUTE_UNUSED)
{
  switch_to_section (ctors_section);
  assemble_align (POINTER_SIZE);
  assemble_integer (symbol, POINTER_SIZE / BITS_PER_UNIT, POINTER_SIZE, 1);
}
#endif

/* CONSTANT_POOL_BEFORE_FUNCTION may be defined as an expression with
   a nonzero value if the constant pool should be output before the
   start of the function, or a zero value if the pool should output
   after the end of the function.  The default is to put it before the
   start.  */

#ifndef CONSTANT_POOL_BEFORE_FUNCTION
#define CONSTANT_POOL_BEFORE_FUNCTION 1
#endif

/* DECL is an object (either VAR_DECL or FUNCTION_DECL) which is going
   to be output to assembler.
   Set first_global_object_name and weak_global_object_name as appropriate.  */

void
notice_global_symbol (tree decl)
{
  const char **type = &first_global_object_name;

  if (first_global_object_name
      || !TREE_PUBLIC (decl)
      || DECL_EXTERNAL (decl)
      || !DECL_NAME (decl)
      || (TREE_CODE (decl) != FUNCTION_DECL
	  && (TREE_CODE (decl) != VAR_DECL
	      || (DECL_COMMON (decl)
		  && (DECL_INITIAL (decl) == 0
		      || DECL_INITIAL (decl) == error_mark_node))))
      || !MEM_P (DECL_RTL (decl)))
    return;

  /* We win when global object is found, but it is useful to know about weak
     symbol as well so we can produce nicer unique names.  */
  if (DECL_WEAK (decl) || DECL_ONE_ONLY (decl))
    type = &weak_global_object_name;

  if (!*type)
    {
      const char *p;
      const char *name;
      rtx decl_rtl = DECL_RTL (decl);

      p = targetm.strip_name_encoding (XSTR (XEXP (decl_rtl, 0), 0));
      name = ggc_strdup (p);

      *type = name;
    }
}

/* Output assembler code for the constant pool of a function and associated
   with defining the name of the function.  DECL describes the function.
   NAME is the function's name.  For the constant pool, we use the current
   constant pool data.  */

void
assemble_start_function (tree decl, const char *fnname)
{
  int align;
  char tmp_label[100];
  bool hot_label_written = false;

  cfun->unlikely_text_section_name = NULL;

  first_function_block_is_cold = false;
  if (flag_reorder_blocks_and_partition)
    {
      ASM_GENERATE_INTERNAL_LABEL (tmp_label, "LHOTB", const_labelno);
      cfun->hot_section_label = ggc_strdup (tmp_label);
      ASM_GENERATE_INTERNAL_LABEL (tmp_label, "LCOLDB", const_labelno);
      cfun->cold_section_label = ggc_strdup (tmp_label);
      ASM_GENERATE_INTERNAL_LABEL (tmp_label, "LHOTE", const_labelno);
      cfun->hot_section_end_label = ggc_strdup (tmp_label);
      ASM_GENERATE_INTERNAL_LABEL (tmp_label, "LCOLDE", const_labelno);
      cfun->cold_section_end_label = ggc_strdup (tmp_label);
      const_labelno++;
    }
  else
    {
      cfun->hot_section_label = NULL;
      cfun->cold_section_label = NULL;
      cfun->hot_section_end_label = NULL;
      cfun->cold_section_end_label = NULL;
    }

  /* The following code does not need preprocessing in the assembler.  */

  app_disable ();

  if (CONSTANT_POOL_BEFORE_FUNCTION)
    output_constant_pool (fnname, decl);

  resolve_unique_section (decl, 0, flag_function_sections);

  /* Make sure the not and cold text (code) sections are properly
     aligned.  This is necessary here in the case where the function
     has both hot and cold sections, because we don't want to re-set
     the alignment when the section switch happens mid-function.  */

  if (flag_reorder_blocks_and_partition)
    {
      switch_to_section (unlikely_text_section ());
      assemble_align (FUNCTION_BOUNDARY);
      ASM_OUTPUT_LABEL (asm_out_file, cfun->cold_section_label);

      /* When the function starts with a cold section, we need to explicitly
	 align the hot section and write out the hot section label.
	 But if the current function is a thunk, we do not have a CFG.  */
      if (!current_function_is_thunk
	  && BB_PARTITION (ENTRY_BLOCK_PTR->next_bb) == BB_COLD_PARTITION)
	{
	  switch_to_section (text_section);
	  assemble_align (FUNCTION_BOUNDARY);
	  ASM_OUTPUT_LABEL (asm_out_file, cfun->hot_section_label);
	  hot_label_written = true;
	  first_function_block_is_cold = true;
	}
    }
  else if (DECL_SECTION_NAME (decl))
    {
      /* Calls to function_section rely on first_function_block_is_cold
	 being accurate.  The first block may be cold even if we aren't
	 doing partitioning, if the entire function was decided by
	 choose_function_section (predict.c) to be cold.  */

      initialize_cold_section_name ();

      if (cfun->unlikely_text_section_name
	  && strcmp (TREE_STRING_POINTER (DECL_SECTION_NAME (decl)),
		     cfun->unlikely_text_section_name) == 0)
	first_function_block_is_cold = true;
    }

  in_cold_section_p = first_function_block_is_cold;

  /* Switch to the correct text section for the start of the function.  */

  switch_to_section (function_section (decl));
  if (flag_reorder_blocks_and_partition
      && !hot_label_written)
    ASM_OUTPUT_LABEL (asm_out_file, cfun->hot_section_label);

  /* Tell assembler to move to target machine's alignment for functions.  */
  align = floor_log2 (FUNCTION_BOUNDARY / BITS_PER_UNIT);
  if (align < force_align_functions_log)
    align = force_align_functions_log;
  if (align > 0)
    {
      ASM_OUTPUT_ALIGN (asm_out_file, align);
    }

  /* Handle a user-specified function alignment.
     Note that we still need to align to FUNCTION_BOUNDARY, as above,
     because ASM_OUTPUT_MAX_SKIP_ALIGN might not do any alignment at all.  */
  if (align_functions_log > align
      && cfun->function_frequency != FUNCTION_FREQUENCY_UNLIKELY_EXECUTED)
    {
#ifdef ASM_OUTPUT_MAX_SKIP_ALIGN
      ASM_OUTPUT_MAX_SKIP_ALIGN (asm_out_file,
				 align_functions_log, align_functions - 1);
#else
      ASM_OUTPUT_ALIGN (asm_out_file, align_functions_log);
#endif
    }

#ifdef ASM_OUTPUT_FUNCTION_PREFIX
  ASM_OUTPUT_FUNCTION_PREFIX (asm_out_file, fnname);
#endif

  (*debug_hooks->begin_function) (decl);

  /* Make function name accessible from other files, if appropriate.  */

  if (TREE_PUBLIC (decl))
    {
      notice_global_symbol (decl);

      globalize_decl (decl);

      maybe_assemble_visibility (decl);
    }

  if (DECL_PRESERVE_P (decl))
    targetm.asm_out.mark_decl_preserved (fnname);

  /* Do any machine/system dependent processing of the function name.  */
#ifdef ASM_DECLARE_FUNCTION_NAME
  ASM_DECLARE_FUNCTION_NAME (asm_out_file, fnname, current_function_decl);
#else
  /* Standard thing is just output label for the function.  */
  ASM_OUTPUT_LABEL (asm_out_file, fnname);
#endif /* ASM_DECLARE_FUNCTION_NAME */
}

/* Output assembler code associated with defining the size of the
   function.  DECL describes the function.  NAME is the function's name.  */

void
assemble_end_function (tree decl, const char *fnname ATTRIBUTE_UNUSED)
{
#ifdef ASM_DECLARE_FUNCTION_SIZE
  /* We could have switched section in the middle of the function.  */
  if (flag_reorder_blocks_and_partition)
    switch_to_section (function_section (decl));
  ASM_DECLARE_FUNCTION_SIZE (asm_out_file, fnname, decl);
#endif
  if (! CONSTANT_POOL_BEFORE_FUNCTION)
    {
      output_constant_pool (fnname, decl);
      switch_to_section (function_section (decl)); /* need to switch back */
    }
  /* Output labels for end of hot/cold text sections (to be used by
     debug info.)  */
  if (flag_reorder_blocks_and_partition)
    {
      section *save_text_section;

      save_text_section = in_section;
      switch_to_section (unlikely_text_section ());
      ASM_OUTPUT_LABEL (asm_out_file, cfun->cold_section_end_label);
      if (first_function_block_is_cold)
	switch_to_section (text_section);
      else
	switch_to_section (function_section (decl));
      ASM_OUTPUT_LABEL (asm_out_file, cfun->hot_section_end_label);
      switch_to_section (save_text_section);
    }
}

/* Assemble code to leave SIZE bytes of zeros.  */

void
assemble_zeros (unsigned HOST_WIDE_INT size)
{
  /* Do no output if -fsyntax-only.  */
  if (flag_syntax_only)
    return;

#ifdef ASM_NO_SKIP_IN_TEXT
  /* The `space' pseudo in the text section outputs nop insns rather than 0s,
     so we must output 0s explicitly in the text section.  */
  if (ASM_NO_SKIP_IN_TEXT && (in_section->common.flags & SECTION_CODE) != 0)
    {
      unsigned HOST_WIDE_INT i;
      for (i = 0; i < size; i++)
	assemble_integer (const0_rtx, 1, BITS_PER_UNIT, 1);
    }
  else
#endif
    if (size > 0)
      ASM_OUTPUT_SKIP (asm_out_file, size);
}

/* Assemble an alignment pseudo op for an ALIGN-bit boundary.  */

void
assemble_align (int align)
{
  if (align > BITS_PER_UNIT)
    {
      ASM_OUTPUT_ALIGN (asm_out_file, floor_log2 (align / BITS_PER_UNIT));
    }
}

/* Assemble a string constant with the specified C string as contents.  */

void
assemble_string (const char *p, int size)
{
  int pos = 0;
  int maximum = 2000;

  /* If the string is very long, split it up.  */

  while (pos < size)
    {
      int thissize = size - pos;
      if (thissize > maximum)
	thissize = maximum;

      ASM_OUTPUT_ASCII (asm_out_file, p, thissize);

      pos += thissize;
      p += thissize;
    }
}


/* A noswitch_section_callback for lcomm_section.  */

static bool
emit_local (tree decl ATTRIBUTE_UNUSED,
	    const char *name ATTRIBUTE_UNUSED,
	    unsigned HOST_WIDE_INT size ATTRIBUTE_UNUSED,
	    unsigned HOST_WIDE_INT rounded ATTRIBUTE_UNUSED)
{
#if defined ASM_OUTPUT_ALIGNED_DECL_LOCAL
  ASM_OUTPUT_ALIGNED_DECL_LOCAL (asm_out_file, decl, name,
				 size, DECL_ALIGN (decl));
  return true;
#elif defined ASM_OUTPUT_ALIGNED_LOCAL
  ASM_OUTPUT_ALIGNED_LOCAL (asm_out_file, name, size, DECL_ALIGN (decl));
  return true;
#else
  ASM_OUTPUT_LOCAL (asm_out_file, name, size, rounded);
  return false;
#endif
}

/* A noswitch_section_callback for bss_noswitch_section.  */

#if defined ASM_OUTPUT_ALIGNED_BSS || defined ASM_OUTPUT_BSS
static bool
emit_bss (tree decl ATTRIBUTE_UNUSED,
	  const char *name ATTRIBUTE_UNUSED,
	  unsigned HOST_WIDE_INT size ATTRIBUTE_UNUSED,
	  unsigned HOST_WIDE_INT rounded ATTRIBUTE_UNUSED)
{
#if defined ASM_OUTPUT_ALIGNED_BSS
  ASM_OUTPUT_ALIGNED_BSS (asm_out_file, decl, name, size, DECL_ALIGN (decl));
  return true;
#else
  ASM_OUTPUT_BSS (asm_out_file, decl, name, size, rounded);
  return false;
#endif
}
#endif

/* A noswitch_section_callback for comm_section.  */

static bool
emit_common (tree decl ATTRIBUTE_UNUSED,
	     const char *name ATTRIBUTE_UNUSED,
	     unsigned HOST_WIDE_INT size ATTRIBUTE_UNUSED,
	     unsigned HOST_WIDE_INT rounded ATTRIBUTE_UNUSED)
{
#if defined ASM_OUTPUT_ALIGNED_DECL_COMMON
  ASM_OUTPUT_ALIGNED_DECL_COMMON (asm_out_file, decl, name,
				  size, DECL_ALIGN (decl));
  return true;
#elif defined ASM_OUTPUT_ALIGNED_COMMON
  ASM_OUTPUT_ALIGNED_COMMON (asm_out_file, name, size, DECL_ALIGN (decl));
  return true;
#else
  ASM_OUTPUT_COMMON (asm_out_file, name, size, rounded);
  return false;
#endif
}

/* A noswitch_section_callback for tls_comm_section.  */

static bool
emit_tls_common (tree decl ATTRIBUTE_UNUSED,
		 const char *name ATTRIBUTE_UNUSED,
		 unsigned HOST_WIDE_INT size ATTRIBUTE_UNUSED,
		 unsigned HOST_WIDE_INT rounded ATTRIBUTE_UNUSED)
{
#ifdef ASM_OUTPUT_TLS_COMMON
  ASM_OUTPUT_TLS_COMMON (asm_out_file, decl, name, size);
  return true;
#else
  sorry ("thread-local COMMON data not implemented");
  return true;
#endif
}

/* Assemble DECL given that it belongs in SECTION_NOSWITCH section SECT.
   NAME is the name of DECL's SYMBOL_REF.  */

static void
assemble_noswitch_variable (tree decl, const char *name, section *sect)
{
  unsigned HOST_WIDE_INT size, rounded;

  size = tree_low_cst (DECL_SIZE_UNIT (decl), 1);
  rounded = size;

  /* Don't allocate zero bytes of common,
     since that means "undefined external" in the linker.  */
  if (size == 0)
    rounded = 1;

  /* Round size up to multiple of BIGGEST_ALIGNMENT bits
     so that each uninitialized object starts on such a boundary.  */
  rounded += (BIGGEST_ALIGNMENT / BITS_PER_UNIT) - 1;
  rounded = (rounded / (BIGGEST_ALIGNMENT / BITS_PER_UNIT)
	     * (BIGGEST_ALIGNMENT / BITS_PER_UNIT));

  if (!sect->noswitch.callback (decl, name, size, rounded)
      && (unsigned HOST_WIDE_INT) DECL_ALIGN_UNIT (decl) > rounded)
    warning (0, "requested alignment for %q+D is greater than "
	     "implemented alignment of %wu", decl, rounded);
}

/* A subroutine of assemble_variable.  Output the label and contents of
   DECL, whose address is a SYMBOL_REF with name NAME.  DONT_OUTPUT_DATA
   is as for assemble_variable.  */

static void
assemble_variable_contents (tree decl, const char *name,
			    bool dont_output_data)
{
  /* Do any machine/system dependent processing of the object.  */
#ifdef ASM_DECLARE_OBJECT_NAME
  last_assemble_variable_decl = decl;
  ASM_DECLARE_OBJECT_NAME (asm_out_file, name, decl);
#else
  /* Standard thing is just output label for the object.  */
  ASM_OUTPUT_LABEL (asm_out_file, name);
#endif /* ASM_DECLARE_OBJECT_NAME */

  if (!dont_output_data)
    {
      if (DECL_INITIAL (decl)
	  && DECL_INITIAL (decl) != error_mark_node
	  && !initializer_zerop (DECL_INITIAL (decl)))
	/* Output the actual data.  */
	output_constant (DECL_INITIAL (decl),
			 tree_low_cst (DECL_SIZE_UNIT (decl), 1),
			 DECL_ALIGN (decl));
      else
	/* Leave space for it.  */
	assemble_zeros (tree_low_cst (DECL_SIZE_UNIT (decl), 1));
    }
}

/* Assemble everything that is needed for a variable or function declaration.
   Not used for automatic variables, and not used for function definitions.
   Should not be called for variables of incomplete structure type.

   TOP_LEVEL is nonzero if this variable has file scope.
   AT_END is nonzero if this is the special handling, at end of compilation,
   to define things that have had only tentative definitions.
   DONT_OUTPUT_DATA if nonzero means don't actually output the
   initial value (that will be done by the caller).  */

void
assemble_variable (tree decl, int top_level ATTRIBUTE_UNUSED,
		   int at_end ATTRIBUTE_UNUSED, int dont_output_data)
{
  const char *name;
  rtx decl_rtl, symbol;
  section *sect;

  if (lang_hooks.decls.prepare_assemble_variable)
    lang_hooks.decls.prepare_assemble_variable (decl);

  last_assemble_variable_decl = 0;

  /* Normally no need to say anything here for external references,
     since assemble_external is called by the language-specific code
     when a declaration is first seen.  */

  if (DECL_EXTERNAL (decl))
    return;

  /* Output no assembler code for a function declaration.
     Only definitions of functions output anything.  */

  if (TREE_CODE (decl) == FUNCTION_DECL)
    return;

  /* Do nothing for global register variables.  */
  if (DECL_RTL_SET_P (decl) && REG_P (DECL_RTL (decl)))
    {
      TREE_ASM_WRITTEN (decl) = 1;
      return;
    }

  /* If type was incomplete when the variable was declared,
     see if it is complete now.  */

  if (DECL_SIZE (decl) == 0)
    layout_decl (decl, 0);

  /* Still incomplete => don't allocate it; treat the tentative defn
     (which is what it must have been) as an `extern' reference.  */

  if (!dont_output_data && DECL_SIZE (decl) == 0)
    {
      error ("storage size of %q+D isn%'t known", decl);
      TREE_ASM_WRITTEN (decl) = 1;
      return;
    }

  /* The first declaration of a variable that comes through this function
     decides whether it is global (in C, has external linkage)
     or local (in C, has internal linkage).  So do nothing more
     if this function has already run.  */

  if (TREE_ASM_WRITTEN (decl))
    return;

  /* Make sure targetm.encode_section_info is invoked before we set
     ASM_WRITTEN.  */
  decl_rtl = DECL_RTL (decl);

  TREE_ASM_WRITTEN (decl) = 1;

  /* Do no output if -fsyntax-only.  */
  if (flag_syntax_only)
    return;

  app_disable ();

  if (! dont_output_data
      && ! host_integerp (DECL_SIZE_UNIT (decl), 1))
    {
      error ("size of variable %q+D is too large", decl);
      return;
    }

  gcc_assert (MEM_P (decl_rtl));
  gcc_assert (GET_CODE (XEXP (decl_rtl, 0)) == SYMBOL_REF);
  symbol = XEXP (decl_rtl, 0);
  name = XSTR (symbol, 0);
  if (TREE_PUBLIC (decl) && DECL_NAME (decl))
    notice_global_symbol (decl);

  /* Compute the alignment of this data.  */

  align_variable (decl, dont_output_data);
  set_mem_align (decl_rtl, DECL_ALIGN (decl));

  if (TREE_PUBLIC (decl))
    maybe_assemble_visibility (decl);

  if (DECL_PRESERVE_P (decl))
    targetm.asm_out.mark_decl_preserved (name);

  /* First make the assembler name(s) global if appropriate.  */
  sect = get_variable_section (decl, false);
  if (TREE_PUBLIC (decl)
      && DECL_NAME (decl)
      && (sect->common.flags & SECTION_COMMON) == 0)
    globalize_decl (decl);

  /* Output any data that we will need to use the address of.  */
  if (DECL_INITIAL (decl) && DECL_INITIAL (decl) != error_mark_node)
    output_addressed_constants (DECL_INITIAL (decl));

  /* dbxout.c needs to know this.  */
  if (sect && (sect->common.flags & SECTION_CODE) != 0)
    DECL_IN_TEXT_SECTION (decl) = 1;

  /* If the decl is part of an object_block, make sure that the decl
     has been positioned within its block, but do not write out its
     definition yet.  output_object_blocks will do that later.  */
  if (SYMBOL_REF_HAS_BLOCK_INFO_P (symbol) && SYMBOL_REF_BLOCK (symbol))
    {
      gcc_assert (!dont_output_data);
      place_block_symbol (symbol);
    }
  else if (SECTION_STYLE (sect) == SECTION_NOSWITCH)
    assemble_noswitch_variable (decl, name, sect);
  else
    {
      switch_to_section (sect);
      if (DECL_ALIGN (decl) > BITS_PER_UNIT)
	ASM_OUTPUT_ALIGN (asm_out_file, floor_log2 (DECL_ALIGN_UNIT (decl)));
      assemble_variable_contents (decl, name, dont_output_data);
    }
}

/* Return 1 if type TYPE contains any pointers.  */

static int
contains_pointers_p (tree type)
{
  switch (TREE_CODE (type))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      /* I'm not sure whether OFFSET_TYPE needs this treatment,
	 so I'll play safe and return 1.  */
    case OFFSET_TYPE:
      return 1;

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      {
	tree fields;
	/* For a type that has fields, see if the fields have pointers.  */
	for (fields = TYPE_FIELDS (type); fields; fields = TREE_CHAIN (fields))
	  if (TREE_CODE (fields) == FIELD_DECL
	      && contains_pointers_p (TREE_TYPE (fields)))
	    return 1;
	return 0;
      }

    case ARRAY_TYPE:
      /* An array type contains pointers if its element type does.  */
      return contains_pointers_p (TREE_TYPE (type));

    default:
      return 0;
    }
}

/* In unit-at-a-time mode, we delay assemble_external processing until
   the compilation unit is finalized.  This is the best we can do for
   right now (i.e. stage 3 of GCC 4.0) - the right thing is to delay
   it all the way to final.  See PR 17982 for further discussion.  */
static GTY(()) tree pending_assemble_externals;

#ifdef ASM_OUTPUT_EXTERNAL
/* True if DECL is a function decl for which no out-of-line copy exists.
   It is assumed that DECL's assembler name has been set.  */

static bool
incorporeal_function_p (tree decl)
{
  if (TREE_CODE (decl) == FUNCTION_DECL && DECL_BUILT_IN (decl))
    {
      const char *name;

      if (DECL_BUILT_IN_CLASS (decl) == BUILT_IN_NORMAL
	  && DECL_FUNCTION_CODE (decl) == BUILT_IN_ALLOCA)
	return true;

      name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
      if (strncmp (name, "__builtin_", strlen ("__builtin_")) == 0)
	return true;
    }
  return false;
}

/* Actually do the tests to determine if this is necessary, and invoke
   ASM_OUTPUT_EXTERNAL.  */
static void
assemble_external_real (tree decl)
{
  rtx rtl = DECL_RTL (decl);

  if (MEM_P (rtl) && GET_CODE (XEXP (rtl, 0)) == SYMBOL_REF
      && !SYMBOL_REF_USED (XEXP (rtl, 0))
      && !incorporeal_function_p (decl))
    {
      /* Some systems do require some output.  */
      SYMBOL_REF_USED (XEXP (rtl, 0)) = 1;
      ASM_OUTPUT_EXTERNAL (asm_out_file, decl, XSTR (XEXP (rtl, 0), 0));
    }
}
#endif

void
process_pending_assemble_externals (void)
{
#ifdef ASM_OUTPUT_EXTERNAL
  tree list;
  for (list = pending_assemble_externals; list; list = TREE_CHAIN (list))
    assemble_external_real (TREE_VALUE (list));

  pending_assemble_externals = 0;
#endif
}

/* Output something to declare an external symbol to the assembler.
   (Most assemblers don't need this, so we normally output nothing.)
   Do nothing if DECL is not external.  */

void
assemble_external (tree decl ATTRIBUTE_UNUSED)
{
  /* Because most platforms do not define ASM_OUTPUT_EXTERNAL, the
     main body of this code is only rarely exercised.  To provide some
     testing, on all platforms, we make sure that the ASM_OUT_FILE is
     open.  If it's not, we should not be calling this function.  */
  gcc_assert (asm_out_file);

#ifdef ASM_OUTPUT_EXTERNAL
  if (!DECL_P (decl) || !DECL_EXTERNAL (decl) || !TREE_PUBLIC (decl))
    return;

  /* We want to output external symbols at very last to check if they
     are references or not.  */
  pending_assemble_externals = tree_cons (0, decl,
					  pending_assemble_externals);
#endif
}

/* Similar, for calling a library function FUN.  */

void
assemble_external_libcall (rtx fun)
{
  /* Declare library function name external when first used, if nec.  */
  if (! SYMBOL_REF_USED (fun))
    {
      SYMBOL_REF_USED (fun) = 1;
      targetm.asm_out.external_libcall (fun);
    }
}

/* Assemble a label named NAME.  */

void
assemble_label (const char *name)
{
  ASM_OUTPUT_LABEL (asm_out_file, name);
}

/* Set the symbol_referenced flag for ID.  */
void
mark_referenced (tree id)
{
  TREE_SYMBOL_REFERENCED (id) = 1;
}

/* Set the symbol_referenced flag for DECL and notify callgraph.  */
void
mark_decl_referenced (tree decl)
{
  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      /* Extern inline functions don't become needed when referenced.
	 If we know a method will be emitted in other TU and no new
	 functions can be marked reachable, just use the external
	 definition.  */
      struct cgraph_node *node = cgraph_node (decl);
      if (!DECL_EXTERNAL (decl)
	  && (!node->local.vtable_method || !cgraph_global_info_ready
	      || !node->local.finalized))
	cgraph_mark_needed_node (node);
    }
  else if (TREE_CODE (decl) == VAR_DECL)
    {
      struct cgraph_varpool_node *node = cgraph_varpool_node (decl);
      cgraph_varpool_mark_needed_node (node);
      /* C++ frontend use mark_decl_references to force COMDAT variables
         to be output that might appear dead otherwise.  */
      node->force_output = true;
    }
  /* else do nothing - we can get various sorts of CST nodes here,
     which do not need to be marked.  */
}


/* Follow the IDENTIFIER_TRANSPARENT_ALIAS chain starting at *ALIAS
   until we find an identifier that is not itself a transparent alias.
   Modify the alias passed to it by reference (and all aliases on the
   way to the ultimate target), such that they do not have to be
   followed again, and return the ultimate target of the alias
   chain.  */

static inline tree
ultimate_transparent_alias_target (tree *alias)
{
  tree target = *alias;

  if (IDENTIFIER_TRANSPARENT_ALIAS (target))
    {
      gcc_assert (TREE_CHAIN (target));
      target = ultimate_transparent_alias_target (&TREE_CHAIN (target));
      gcc_assert (! IDENTIFIER_TRANSPARENT_ALIAS (target)
		  && ! TREE_CHAIN (target));
      *alias = target;
    }

  return target;
}

/* Output to FILE (an assembly file) a reference to NAME.  If NAME
   starts with a *, the rest of NAME is output verbatim.  Otherwise
   NAME is transformed in a target-specific way (usually by the
   addition of an underscore).  */

void
assemble_name_raw (FILE *file, const char *name)
{
  if (name[0] == '*')
    fputs (&name[1], file);
  else
    ASM_OUTPUT_LABELREF (file, name);
}

/* Like assemble_name_raw, but should be used when NAME might refer to
   an entity that is also represented as a tree (like a function or
   variable).  If NAME does refer to such an entity, that entity will
   be marked as referenced.  */

void
assemble_name (FILE *file, const char *name)
{
  const char *real_name;
  tree id;

  real_name = targetm.strip_name_encoding (name);

  id = maybe_get_identifier (real_name);
  if (id)
    {
      tree id_orig = id;

      mark_referenced (id);
      ultimate_transparent_alias_target (&id);
      if (id != id_orig)
	name = IDENTIFIER_POINTER (id);
      gcc_assert (! TREE_CHAIN (id));
    }

  assemble_name_raw (file, name);
}

/* Allocate SIZE bytes writable static space with a gensym name
   and return an RTX to refer to its address.  */

rtx
assemble_static_space (unsigned HOST_WIDE_INT size)
{
  char name[12];
  const char *namestring;
  rtx x;

  ASM_GENERATE_INTERNAL_LABEL (name, "LF", const_labelno);
  ++const_labelno;
  namestring = ggc_strdup (name);

  x = gen_rtx_SYMBOL_REF (Pmode, namestring);
  SYMBOL_REF_FLAGS (x) = SYMBOL_FLAG_LOCAL;

#ifdef ASM_OUTPUT_ALIGNED_DECL_LOCAL
  ASM_OUTPUT_ALIGNED_DECL_LOCAL (asm_out_file, NULL_TREE, name, size,
				 BIGGEST_ALIGNMENT);
#else
#ifdef ASM_OUTPUT_ALIGNED_LOCAL
  ASM_OUTPUT_ALIGNED_LOCAL (asm_out_file, name, size, BIGGEST_ALIGNMENT);
#else
  {
    /* Round size up to multiple of BIGGEST_ALIGNMENT bits
       so that each uninitialized object starts on such a boundary.  */
    /* Variable `rounded' might or might not be used in ASM_OUTPUT_LOCAL.  */
    unsigned HOST_WIDE_INT rounded ATTRIBUTE_UNUSED
      = ((size + (BIGGEST_ALIGNMENT / BITS_PER_UNIT) - 1)
	 / (BIGGEST_ALIGNMENT / BITS_PER_UNIT)
	 * (BIGGEST_ALIGNMENT / BITS_PER_UNIT));
    ASM_OUTPUT_LOCAL (asm_out_file, name, size, rounded);
  }
#endif
#endif
  return x;
}

/* Assemble the static constant template for function entry trampolines.
   This is done at most once per compilation.
   Returns an RTX for the address of the template.  */

static GTY(()) rtx initial_trampoline;

#ifdef TRAMPOLINE_TEMPLATE
rtx
assemble_trampoline_template (void)
{
  char label[256];
  const char *name;
  int align;
  rtx symbol;

  if (initial_trampoline)
    return initial_trampoline;

  /* By default, put trampoline templates in read-only data section.  */

#ifdef TRAMPOLINE_SECTION
  switch_to_section (TRAMPOLINE_SECTION);
#else
  switch_to_section (readonly_data_section);
#endif

  /* Write the assembler code to define one.  */
  align = floor_log2 (TRAMPOLINE_ALIGNMENT / BITS_PER_UNIT);
  if (align > 0)
    {
      ASM_OUTPUT_ALIGN (asm_out_file, align);
    }

  targetm.asm_out.internal_label (asm_out_file, "LTRAMP", 0);
  TRAMPOLINE_TEMPLATE (asm_out_file);

  /* Record the rtl to refer to it.  */
  ASM_GENERATE_INTERNAL_LABEL (label, "LTRAMP", 0);
  name = ggc_strdup (label);
  symbol = gen_rtx_SYMBOL_REF (Pmode, name);
  SYMBOL_REF_FLAGS (symbol) = SYMBOL_FLAG_LOCAL;

  initial_trampoline = gen_rtx_MEM (BLKmode, symbol);
  set_mem_align (initial_trampoline, TRAMPOLINE_ALIGNMENT);

  return initial_trampoline;
}
#endif

/* A and B are either alignments or offsets.  Return the minimum alignment
   that may be assumed after adding the two together.  */

static inline unsigned
min_align (unsigned int a, unsigned int b)
{
  return (a | b) & -(a | b);
}

/* Return the assembler directive for creating a given kind of integer
   object.  SIZE is the number of bytes in the object and ALIGNED_P
   indicates whether it is known to be aligned.  Return NULL if the
   assembly dialect has no such directive.

   The returned string should be printed at the start of a new line and
   be followed immediately by the object's initial value.  */

const char *
integer_asm_op (int size, int aligned_p)
{
  struct asm_int_op *ops;

  if (aligned_p)
    ops = &targetm.asm_out.aligned_op;
  else
    ops = &targetm.asm_out.unaligned_op;

  switch (size)
    {
    case 1:
      return targetm.asm_out.byte_op;
    case 2:
      return ops->hi;
    case 4:
      return ops->si;
    case 8:
      return ops->di;
    case 16:
      return ops->ti;
    default:
      return NULL;
    }
}

/* Use directive OP to assemble an integer object X.  Print OP at the
   start of the line, followed immediately by the value of X.  */

void
assemble_integer_with_op (const char *op, rtx x)
{
  fputs (op, asm_out_file);
  output_addr_const (asm_out_file, x);
  fputc ('\n', asm_out_file);
}

/* The default implementation of the asm_out.integer target hook.  */

bool
default_assemble_integer (rtx x ATTRIBUTE_UNUSED,
			  unsigned int size ATTRIBUTE_UNUSED,
			  int aligned_p ATTRIBUTE_UNUSED)
{
  const char *op = integer_asm_op (size, aligned_p);
  /* Avoid GAS bugs for large values.  Specifically negative values whose
     absolute value fits in a bfd_vma, but not in a bfd_signed_vma.  */
  if (size > UNITS_PER_WORD && size > POINTER_SIZE / BITS_PER_UNIT)
    return false;
  return op && (assemble_integer_with_op (op, x), true);
}

/* Assemble the integer constant X into an object of SIZE bytes.  ALIGN is
   the alignment of the integer in bits.  Return 1 if we were able to output
   the constant, otherwise 0.  We must be able to output the constant,
   if FORCE is nonzero.  */

bool
assemble_integer (rtx x, unsigned int size, unsigned int align, int force)
{
  int aligned_p;

  aligned_p = (align >= MIN (size * BITS_PER_UNIT, BIGGEST_ALIGNMENT));

  /* See if the target hook can handle this kind of object.  */
  if (targetm.asm_out.integer (x, size, aligned_p))
    return true;

  /* If the object is a multi-byte one, try splitting it up.  Split
     it into words it if is multi-word, otherwise split it into bytes.  */
  if (size > 1)
    {
      enum machine_mode omode, imode;
      unsigned int subalign;
      unsigned int subsize, i;

      subsize = size > UNITS_PER_WORD? UNITS_PER_WORD : 1;
      subalign = MIN (align, subsize * BITS_PER_UNIT);
      omode = mode_for_size (subsize * BITS_PER_UNIT, MODE_INT, 0);
      imode = mode_for_size (size * BITS_PER_UNIT, MODE_INT, 0);

      for (i = 0; i < size; i += subsize)
	{
	  rtx partial = simplify_subreg (omode, x, imode, i);
	  if (!partial || !assemble_integer (partial, subsize, subalign, 0))
	    break;
	}
      if (i == size)
	return true;

      /* If we've printed some of it, but not all of it, there's no going
	 back now.  */
      gcc_assert (!i);
    }

  gcc_assert (!force);

  return false;
}

void
assemble_real (REAL_VALUE_TYPE d, enum machine_mode mode, unsigned int align)
{
  long data[4] = {0, 0, 0, 0};
  int i;
  int bitsize, nelts, nunits, units_per;

  /* This is hairy.  We have a quantity of known size.  real_to_target
     will put it into an array of *host* longs, 32 bits per element
     (even if long is more than 32 bits).  We need to determine the
     number of array elements that are occupied (nelts) and the number
     of *target* min-addressable units that will be occupied in the
     object file (nunits).  We cannot assume that 32 divides the
     mode's bitsize (size * BITS_PER_UNIT) evenly.

     size * BITS_PER_UNIT is used here to make sure that padding bits
     (which might appear at either end of the value; real_to_target
     will include the padding bits in its output array) are included.  */

  nunits = GET_MODE_SIZE (mode);
  bitsize = nunits * BITS_PER_UNIT;
  nelts = CEIL (bitsize, 32);
  units_per = 32 / BITS_PER_UNIT;

  real_to_target (data, &d, mode);

  /* Put out the first word with the specified alignment.  */
  assemble_integer (GEN_INT (data[0]), MIN (nunits, units_per), align, 1);
  nunits -= units_per;

  /* Subsequent words need only 32-bit alignment.  */
  align = min_align (align, 32);

  for (i = 1; i < nelts; i++)
    {
      assemble_integer (GEN_INT (data[i]), MIN (nunits, units_per), align, 1);
      nunits -= units_per;
    }
}

/* Given an expression EXP with a constant value,
   reduce it to the sum of an assembler symbol and an integer.
   Store them both in the structure *VALUE.
   EXP must be reducible.  */

struct addr_const GTY(())
{
  rtx base;
  HOST_WIDE_INT offset;
};

static void
decode_addr_const (tree exp, struct addr_const *value)
{
  tree target = TREE_OPERAND (exp, 0);
  int offset = 0;
  rtx x;

  while (1)
    {
      if (TREE_CODE (target) == COMPONENT_REF
	  && host_integerp (byte_position (TREE_OPERAND (target, 1)), 0))

	{
	  offset += int_byte_position (TREE_OPERAND (target, 1));
	  target = TREE_OPERAND (target, 0);
	}
      else if (TREE_CODE (target) == ARRAY_REF
	       || TREE_CODE (target) == ARRAY_RANGE_REF)
	{
	  offset += (tree_low_cst (TYPE_SIZE_UNIT (TREE_TYPE (target)), 1)
		     * tree_low_cst (TREE_OPERAND (target, 1), 0));
	  target = TREE_OPERAND (target, 0);
	}
      else
	break;
    }

  switch (TREE_CODE (target))
    {
    case VAR_DECL:
    case FUNCTION_DECL:
      x = DECL_RTL (target);
      break;

    case LABEL_DECL:
      x = gen_rtx_MEM (FUNCTION_MODE,
		       gen_rtx_LABEL_REF (Pmode, force_label_rtx (target)));
      break;

    case REAL_CST:
    case STRING_CST:
    case COMPLEX_CST:
    case CONSTRUCTOR:
    case INTEGER_CST:
      x = output_constant_def (target, 1);
      break;

    default:
      gcc_unreachable ();
    }

  gcc_assert (MEM_P (x));
  x = XEXP (x, 0);

  value->base = x;
  value->offset = offset;
}

/* Uniquize all constants that appear in memory.
   Each constant in memory thus far output is recorded
   in `const_desc_table'.  */

struct constant_descriptor_tree GTY(())
{
  /* A MEM for the constant.  */
  rtx rtl;

  /* The value of the constant.  */
  tree value;

  /* Hash of value.  Computing the hash from value each time
     hashfn is called can't work properly, as that means recursive
     use of the hash table during hash table expansion.  */
  hashval_t hash;
};

static GTY((param_is (struct constant_descriptor_tree)))
     htab_t const_desc_htab;

static struct constant_descriptor_tree * build_constant_desc (tree);
static void maybe_output_constant_def_contents (struct constant_descriptor_tree *, int);

/* Compute a hash code for a constant expression.  */

static hashval_t
const_desc_hash (const void *ptr)
{
  return ((struct constant_descriptor_tree *)ptr)->hash;
}

static hashval_t
const_hash_1 (const tree exp)
{
  const char *p;
  hashval_t hi;
  int len, i;
  enum tree_code code = TREE_CODE (exp);

  /* Either set P and LEN to the address and len of something to hash and
     exit the switch or return a value.  */

  switch (code)
    {
    case INTEGER_CST:
      p = (char *) &TREE_INT_CST (exp);
      len = sizeof TREE_INT_CST (exp);
      break;

    case REAL_CST:
      return real_hash (TREE_REAL_CST_PTR (exp));

    case STRING_CST:
      p = TREE_STRING_POINTER (exp);
      len = TREE_STRING_LENGTH (exp);
      break;

    case COMPLEX_CST:
      return (const_hash_1 (TREE_REALPART (exp)) * 5
	      + const_hash_1 (TREE_IMAGPART (exp)));

    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;
	tree value;

	hi = 5 + int_size_in_bytes (TREE_TYPE (exp));

	FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (exp), idx, value)
	  if (value)
	    hi = hi * 603 + const_hash_1 (value);

	return hi;
      }

    case ADDR_EXPR:
    case FDESC_EXPR:
      {
	struct addr_const value;

	decode_addr_const (exp, &value);
	switch (GET_CODE (value.base))
	  {
	  case SYMBOL_REF:
	    /* Don't hash the address of the SYMBOL_REF;
	       only use the offset and the symbol name.  */
	    hi = value.offset;
	    p = XSTR (value.base, 0);
	    for (i = 0; p[i] != 0; i++)
	      hi = ((hi * 613) + (unsigned) (p[i]));
	    break;

	  case LABEL_REF:
	    hi = value.offset + CODE_LABEL_NUMBER (XEXP (value.base, 0)) * 13;
	    break;

	  default:
	    gcc_unreachable ();
	  }
      }
      return hi;

    case PLUS_EXPR:
    case MINUS_EXPR:
      return (const_hash_1 (TREE_OPERAND (exp, 0)) * 9
	      + const_hash_1 (TREE_OPERAND (exp, 1)));

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
      return const_hash_1 (TREE_OPERAND (exp, 0)) * 7 + 2;

    default:
      /* A language specific constant. Just hash the code.  */
      return code;
    }

  /* Compute hashing function.  */
  hi = len;
  for (i = 0; i < len; i++)
    hi = ((hi * 613) + (unsigned) (p[i]));

  return hi;
}

/* Wrapper of compare_constant, for the htab interface.  */
static int
const_desc_eq (const void *p1, const void *p2)
{
  const struct constant_descriptor_tree *c1 = p1;
  const struct constant_descriptor_tree *c2 = p2;
  if (c1->hash != c2->hash)
    return 0;
  return compare_constant (c1->value, c2->value);
}

/* Compare t1 and t2, and return 1 only if they are known to result in
   the same bit pattern on output.  */

static int
compare_constant (const tree t1, const tree t2)
{
  enum tree_code typecode;

  if (t1 == NULL_TREE)
    return t2 == NULL_TREE;
  if (t2 == NULL_TREE)
    return 0;

  if (TREE_CODE (t1) != TREE_CODE (t2))
    return 0;

  switch (TREE_CODE (t1))
    {
    case INTEGER_CST:
      /* Integer constants are the same only if the same width of type.  */
      if (TYPE_PRECISION (TREE_TYPE (t1)) != TYPE_PRECISION (TREE_TYPE (t2)))
	return 0;
      if (TYPE_MODE (TREE_TYPE (t1)) != TYPE_MODE (TREE_TYPE (t2)))
	return 0;
      return tree_int_cst_equal (t1, t2);

    case REAL_CST:
      /* Real constants are the same only if the same width of type.  */
      if (TYPE_PRECISION (TREE_TYPE (t1)) != TYPE_PRECISION (TREE_TYPE (t2)))
	return 0;

      return REAL_VALUES_IDENTICAL (TREE_REAL_CST (t1), TREE_REAL_CST (t2));

    case STRING_CST:
      if (TYPE_MODE (TREE_TYPE (t1)) != TYPE_MODE (TREE_TYPE (t2)))
	return 0;

      return (TREE_STRING_LENGTH (t1) == TREE_STRING_LENGTH (t2)
	      && ! memcmp (TREE_STRING_POINTER (t1), TREE_STRING_POINTER (t2),
			 TREE_STRING_LENGTH (t1)));

    case COMPLEX_CST:
      return (compare_constant (TREE_REALPART (t1), TREE_REALPART (t2))
	      && compare_constant (TREE_IMAGPART (t1), TREE_IMAGPART (t2)));

    case CONSTRUCTOR:
      {
	VEC(constructor_elt, gc) *v1, *v2;
	unsigned HOST_WIDE_INT idx;

	typecode = TREE_CODE (TREE_TYPE (t1));
	if (typecode != TREE_CODE (TREE_TYPE (t2)))
	  return 0;

	if (typecode == ARRAY_TYPE)
	  {
	    HOST_WIDE_INT size_1 = int_size_in_bytes (TREE_TYPE (t1));
	    /* For arrays, check that the sizes all match.  */
	    if (TYPE_MODE (TREE_TYPE (t1)) != TYPE_MODE (TREE_TYPE (t2))
		|| size_1 == -1
		|| size_1 != int_size_in_bytes (TREE_TYPE (t2)))
	      return 0;
	  }
	else
	  {
	    /* For record and union constructors, require exact type
               equality.  */
	    if (TREE_TYPE (t1) != TREE_TYPE (t2))
	      return 0;
	  }

	v1 = CONSTRUCTOR_ELTS (t1);
	v2 = CONSTRUCTOR_ELTS (t2);
	if (VEC_length (constructor_elt, v1)
	    != VEC_length (constructor_elt, v2))
	    return 0;

	for (idx = 0; idx < VEC_length (constructor_elt, v1); ++idx)
	  {
	    constructor_elt *c1 = VEC_index (constructor_elt, v1, idx);
	    constructor_elt *c2 = VEC_index (constructor_elt, v2, idx);

	    /* Check that each value is the same...  */
	    if (!compare_constant (c1->value, c2->value))
	      return 0;
	    /* ... and that they apply to the same fields!  */
	    if (typecode == ARRAY_TYPE)
	      {
		if (!compare_constant (c1->index, c2->index))
		  return 0;
	      }
	    else
	      {
		if (c1->index != c2->index)
		  return 0;
	      }
	  }

	return 1;
      }

    case ADDR_EXPR:
    case FDESC_EXPR:
      {
	struct addr_const value1, value2;

	decode_addr_const (t1, &value1);
	decode_addr_const (t2, &value2);
	return (value1.offset == value2.offset
		&& strcmp (XSTR (value1.base, 0), XSTR (value2.base, 0)) == 0);
      }

    case PLUS_EXPR:
    case MINUS_EXPR:
    case RANGE_EXPR:
      return (compare_constant (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0))
	      && compare_constant(TREE_OPERAND (t1, 1), TREE_OPERAND (t2, 1)));

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
    case VIEW_CONVERT_EXPR:
      return compare_constant (TREE_OPERAND (t1, 0), TREE_OPERAND (t2, 0));

    default:
      {
	tree nt1, nt2;
	nt1 = lang_hooks.expand_constant (t1);
	nt2 = lang_hooks.expand_constant (t2);
	if (nt1 != t1 || nt2 != t2)
	  return compare_constant (nt1, nt2);
	else
	  return 0;
      }
    }

  gcc_unreachable ();
}

/* Make a copy of the whole tree structure for a constant.  This
   handles the same types of nodes that compare_constant handles.  */

static tree
copy_constant (tree exp)
{
  switch (TREE_CODE (exp))
    {
    case ADDR_EXPR:
      /* For ADDR_EXPR, we do not want to copy the decl whose address
	 is requested.  We do want to copy constants though.  */
      if (CONSTANT_CLASS_P (TREE_OPERAND (exp, 0)))
	return build1 (TREE_CODE (exp), TREE_TYPE (exp),
		       copy_constant (TREE_OPERAND (exp, 0)));
      else
	return copy_node (exp);

    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
      return copy_node (exp);

    case COMPLEX_CST:
      return build_complex (TREE_TYPE (exp),
			    copy_constant (TREE_REALPART (exp)),
			    copy_constant (TREE_IMAGPART (exp)));

    case PLUS_EXPR:
    case MINUS_EXPR:
      return build2 (TREE_CODE (exp), TREE_TYPE (exp),
		     copy_constant (TREE_OPERAND (exp, 0)),
		     copy_constant (TREE_OPERAND (exp, 1)));

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
    case VIEW_CONVERT_EXPR:
      return build1 (TREE_CODE (exp), TREE_TYPE (exp),
		     copy_constant (TREE_OPERAND (exp, 0)));

    case CONSTRUCTOR:
      {
	tree copy = copy_node (exp);
	VEC(constructor_elt, gc) *v;
	unsigned HOST_WIDE_INT idx;
	tree purpose, value;

	v = VEC_alloc(constructor_elt, gc, VEC_length(constructor_elt,
						      CONSTRUCTOR_ELTS (exp)));
	FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (exp), idx, purpose, value)
	  {
	    constructor_elt *ce = VEC_quick_push (constructor_elt, v, NULL);
	    ce->index = purpose;
	    ce->value = copy_constant (value);
	  }
	CONSTRUCTOR_ELTS (copy) = v;
	return copy;
      }

    default:
      {
	tree t = lang_hooks.expand_constant (exp);

	gcc_assert (t != exp);
	return copy_constant (t);
      }
    }
}

/* Return the alignment of constant EXP in bits.  */

static unsigned int
get_constant_alignment (tree exp)
{
  unsigned int align;

  align = TYPE_ALIGN (TREE_TYPE (exp));
#ifdef CONSTANT_ALIGNMENT
  align = CONSTANT_ALIGNMENT (exp, align);
#endif
  return align;
}

/* Return the section into which constant EXP should be placed.  */

static section *
get_constant_section (tree exp)
{
  if (IN_NAMED_SECTION (exp))
    return get_named_section (exp, NULL, compute_reloc_for_constant (exp));
  else
    return targetm.asm_out.select_section (exp,
					   compute_reloc_for_constant (exp),
					   get_constant_alignment (exp));
}

/* Return the size of constant EXP in bytes.  */

static HOST_WIDE_INT
get_constant_size (tree exp)
{
  HOST_WIDE_INT size;

  size = int_size_in_bytes (TREE_TYPE (exp));
  if (TREE_CODE (exp) == STRING_CST)
    size = MAX (TREE_STRING_LENGTH (exp), size);
  return size;
}

/* Subroutine of output_constant_def:
   No constant equal to EXP is known to have been output.
   Make a constant descriptor to enter EXP in the hash table.
   Assign the label number and construct RTL to refer to the
   constant's location in memory.
   Caller is responsible for updating the hash table.  */

static struct constant_descriptor_tree *
build_constant_desc (tree exp)
{
  rtx symbol;
  rtx rtl;
  char label[256];
  int labelno;
  struct constant_descriptor_tree *desc;

  desc = ggc_alloc (sizeof (*desc));
  desc->value = copy_constant (exp);

  /* Propagate marked-ness to copied constant.  */
  if (flag_mudflap && mf_marked_p (exp))
    mf_mark (desc->value);

  /* Create a string containing the label name, in LABEL.  */
  labelno = const_labelno++;
  ASM_GENERATE_INTERNAL_LABEL (label, "LC", labelno);

  /* We have a symbol name; construct the SYMBOL_REF and the MEM.  */
  if (use_object_blocks_p ())
    {
      section *sect = get_constant_section (exp);
      symbol = create_block_symbol (ggc_strdup (label),
				    get_block_for_section (sect), -1);
    }
  else
    symbol = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (label));
  SYMBOL_REF_FLAGS (symbol) |= SYMBOL_FLAG_LOCAL;
  SET_SYMBOL_REF_DECL (symbol, desc->value);
  TREE_CONSTANT_POOL_ADDRESS_P (symbol) = 1;

  rtl = gen_rtx_MEM (TYPE_MODE (TREE_TYPE (exp)), symbol);
  set_mem_attributes (rtl, exp, 1);
  set_mem_alias_set (rtl, 0);
  set_mem_alias_set (rtl, const_alias_set);

  /* Set flags or add text to the name to record information, such as
     that it is a local symbol.  If the name is changed, the macro
     ASM_OUTPUT_LABELREF will have to know how to strip this
     information.  This call might invalidate our local variable
     SYMBOL; we can't use it afterward.  */

  targetm.encode_section_info (exp, rtl, true);

  desc->rtl = rtl;

  return desc;
}

/* Return an rtx representing a reference to constant data in memory
   for the constant expression EXP.

   If assembler code for such a constant has already been output,
   return an rtx to refer to it.
   Otherwise, output such a constant in memory
   and generate an rtx for it.

   If DEFER is nonzero, this constant can be deferred and output only
   if referenced in the function after all optimizations.

   `const_desc_table' records which constants already have label strings.  */

rtx
output_constant_def (tree exp, int defer)
{
  struct constant_descriptor_tree *desc;
  struct constant_descriptor_tree key;
  void **loc;

  /* Look up EXP in the table of constant descriptors.  If we didn't find
     it, create a new one.  */
  key.value = exp;
  key.hash = const_hash_1 (exp);
  loc = htab_find_slot_with_hash (const_desc_htab, &key, key.hash, INSERT);

  desc = *loc;
  if (desc == 0)
    {
      desc = build_constant_desc (exp);
      desc->hash = key.hash;
      *loc = desc;
    }

  maybe_output_constant_def_contents (desc, defer);
  return desc->rtl;
}

/* Subroutine of output_constant_def: Decide whether or not we need to
   output the constant DESC now, and if so, do it.  */
static void
maybe_output_constant_def_contents (struct constant_descriptor_tree *desc,
				    int defer)
{
  rtx symbol = XEXP (desc->rtl, 0);
  tree exp = desc->value;

  if (flag_syntax_only)
    return;

  if (TREE_ASM_WRITTEN (exp))
    /* Already output; don't do it again.  */
    return;

  /* We can always defer constants as long as the context allows
     doing so.  */
  if (defer)
    {
      /* Increment n_deferred_constants if it exists.  It needs to be at
	 least as large as the number of constants actually referred to
	 by the function.  If it's too small we'll stop looking too early
	 and fail to emit constants; if it's too large we'll only look
	 through the entire function when we could have stopped earlier.  */
      if (cfun)
	n_deferred_constants++;
      return;
    }

  output_constant_def_contents (symbol);
}

/* Subroutine of output_constant_def_contents.  Output the definition
   of constant EXP, which is pointed to by label LABEL.  ALIGN is the
   constant's alignment in bits.  */

static void
assemble_constant_contents (tree exp, const char *label, unsigned int align)
{
  HOST_WIDE_INT size;

  size = get_constant_size (exp);

  /* Do any machine/system dependent processing of the constant.  */
#ifdef ASM_DECLARE_CONSTANT_NAME
  ASM_DECLARE_CONSTANT_NAME (asm_out_file, label, exp, size);
#else
  /* Standard thing is just output label for the constant.  */
  ASM_OUTPUT_LABEL (asm_out_file, label);
#endif /* ASM_DECLARE_CONSTANT_NAME */

  /* Output the value of EXP.  */
  output_constant (exp, size, align);
}

/* We must output the constant data referred to by SYMBOL; do so.  */

static void
output_constant_def_contents (rtx symbol)
{
  tree exp = SYMBOL_REF_DECL (symbol);
  unsigned int align;

  /* Make sure any other constants whose addresses appear in EXP
     are assigned label numbers.  */
  output_addressed_constants (exp);

  /* We are no longer deferring this constant.  */
  TREE_ASM_WRITTEN (exp) = 1;

  /* If the constant is part of an object block, make sure that the
     decl has been positioned within its block, but do not write out
     its definition yet.  output_object_blocks will do that later.  */
  if (SYMBOL_REF_HAS_BLOCK_INFO_P (symbol) && SYMBOL_REF_BLOCK (symbol))
    place_block_symbol (symbol);
  else
    {
      switch_to_section (get_constant_section (exp));
      align = get_constant_alignment (exp);
      if (align > BITS_PER_UNIT)
	ASM_OUTPUT_ALIGN (asm_out_file, floor_log2 (align / BITS_PER_UNIT));
      assemble_constant_contents (exp, XSTR (symbol, 0), align);
    }
  if (flag_mudflap)
    mudflap_enqueue_constant (exp);
}

/* Look up EXP in the table of constant descriptors.  Return the rtl
   if it has been emitted, else null.  */

rtx
lookup_constant_def (tree exp)
{
  struct constant_descriptor_tree *desc;
  struct constant_descriptor_tree key;

  key.value = exp;
  key.hash = const_hash_1 (exp);
  desc = htab_find_with_hash (const_desc_htab, &key, key.hash);

  return (desc ? desc->rtl : NULL_RTX);
}

/* Used in the hash tables to avoid outputting the same constant
   twice.  Unlike 'struct constant_descriptor_tree', RTX constants
   are output once per function, not once per file.  */
/* ??? Only a few targets need per-function constant pools.  Most
   can use one per-file pool.  Should add a targetm bit to tell the
   difference.  */

struct rtx_constant_pool GTY(())
{
  /* Pointers to first and last constant in pool, as ordered by offset.  */
  struct constant_descriptor_rtx *first;
  struct constant_descriptor_rtx *last;

  /* Hash facility for making memory-constants from constant rtl-expressions.
     It is used on RISC machines where immediate integer arguments and
     constant addresses are restricted so that such constants must be stored
     in memory.  */
  htab_t GTY((param_is (struct constant_descriptor_rtx))) const_rtx_htab;

  /* Current offset in constant pool (does not include any
     machine-specific header).  */
  HOST_WIDE_INT offset;
};

struct constant_descriptor_rtx GTY((chain_next ("%h.next")))
{
  struct constant_descriptor_rtx *next;
  rtx mem;
  rtx sym;
  rtx constant;
  HOST_WIDE_INT offset;
  hashval_t hash;
  enum machine_mode mode;
  unsigned int align;
  int labelno;
  int mark;
};

/* Hash and compare functions for const_rtx_htab.  */

static hashval_t
const_desc_rtx_hash (const void *ptr)
{
  const struct constant_descriptor_rtx *desc = ptr;
  return desc->hash;
}

static int
const_desc_rtx_eq (const void *a, const void *b)
{
  const struct constant_descriptor_rtx *x = a;
  const struct constant_descriptor_rtx *y = b;

  if (x->mode != y->mode)
    return 0;
  return rtx_equal_p (x->constant, y->constant);
}

/* This is the worker function for const_rtx_hash, called via for_each_rtx.  */

static int
const_rtx_hash_1 (rtx *xp, void *data)
{
  unsigned HOST_WIDE_INT hwi;
  enum machine_mode mode;
  enum rtx_code code;
  hashval_t h, *hp;
  rtx x;

  x = *xp;
  code = GET_CODE (x);
  mode = GET_MODE (x);
  h = (hashval_t) code * 1048573 + mode;

  switch (code)
    {
    case CONST_INT:
      hwi = INTVAL (x);
    fold_hwi:
      {
	const int shift = sizeof (hashval_t) * CHAR_BIT;
	const int n = sizeof (HOST_WIDE_INT) / sizeof (hashval_t);
	int i;

	h ^= (hashval_t) hwi;
	for (i = 1; i < n; ++i)
	  {
	    hwi >>= shift;
	    h ^= (hashval_t) hwi;
	  }
      }
      break;

    case CONST_DOUBLE:
      if (mode == VOIDmode)
	{
	  hwi = CONST_DOUBLE_LOW (x) ^ CONST_DOUBLE_HIGH (x);
	  goto fold_hwi;
	}
      else
	h ^= real_hash (CONST_DOUBLE_REAL_VALUE (x));
      break;

    case CONST_VECTOR:
      {
	int i;
	for (i = XVECLEN (x, 0); i-- > 0; )
	  h = h * 251 + const_rtx_hash_1 (&XVECEXP (x, 0, i), data);
      }
      break;

    case SYMBOL_REF:
      h ^= htab_hash_string (XSTR (x, 0));
      break;

    case LABEL_REF:
      h = h * 251 + CODE_LABEL_NUMBER (XEXP (x, 0));
      break;

    case UNSPEC:
    case UNSPEC_VOLATILE:
      h = h * 251 + XINT (x, 1);
      break;

    default:
      break;
    }

  hp = data;
  *hp = *hp * 509 + h;
  return 0;
}

/* Compute a hash value for X, which should be a constant.  */

static hashval_t
const_rtx_hash (rtx x)
{
  hashval_t h = 0;
  for_each_rtx (&x, const_rtx_hash_1, &h);
  return h;
}


/* Create and return a new rtx constant pool.  */

static struct rtx_constant_pool *
create_constant_pool (void)
{
  struct rtx_constant_pool *pool;

  pool = ggc_alloc (sizeof (struct rtx_constant_pool));
  pool->const_rtx_htab = htab_create_ggc (31, const_desc_rtx_hash,
					  const_desc_rtx_eq, NULL);
  pool->first = NULL;
  pool->last = NULL;
  pool->offset = 0;
  return pool;
}

/* Initialize constant pool hashing for a new function.  */

void
init_varasm_status (struct function *f)
{
  struct varasm_status *p;

  p = ggc_alloc (sizeof (struct varasm_status));
  f->varasm = p;

  p->pool = create_constant_pool ();
  p->deferred_constants = 0;
}

/* Given a MINUS expression, simplify it if both sides
   include the same symbol.  */

rtx
simplify_subtraction (rtx x)
{
  rtx r = simplify_rtx (x);
  return r ? r : x;
}

/* Given a constant rtx X, make (or find) a memory constant for its value
   and return a MEM rtx to refer to it in memory.  */

rtx
force_const_mem (enum machine_mode mode, rtx x)
{
  struct constant_descriptor_rtx *desc, tmp;
  struct rtx_constant_pool *pool;
  char label[256];
  rtx def, symbol;
  hashval_t hash;
  unsigned int align;
  void **slot;

  /* If we're not allowed to drop X into the constant pool, don't.  */
  if (targetm.cannot_force_const_mem (x))
    return NULL_RTX;

  /* Record that this function has used a constant pool entry.  */
  current_function_uses_const_pool = 1;

  /* Decide which pool to use.  */
  pool = (targetm.use_blocks_for_constant_p (mode, x)
	  ? shared_constant_pool
	  : cfun->varasm->pool);

  /* Lookup the value in the hashtable.  */
  tmp.constant = x;
  tmp.mode = mode;
  hash = const_rtx_hash (x);
  slot = htab_find_slot_with_hash (pool->const_rtx_htab, &tmp, hash, INSERT);
  desc = *slot;

  /* If the constant was already present, return its memory.  */
  if (desc)
    return copy_rtx (desc->mem);

  /* Otherwise, create a new descriptor.  */
  desc = ggc_alloc (sizeof (*desc));
  *slot = desc;

  /* Align the location counter as required by EXP's data type.  */
  align = GET_MODE_ALIGNMENT (mode == VOIDmode ? word_mode : mode);
#ifdef CONSTANT_ALIGNMENT
  {
    tree type = lang_hooks.types.type_for_mode (mode, 0);
    if (type != NULL_TREE)
      align = CONSTANT_ALIGNMENT (make_tree (type, x), align);
  }
#endif

  pool->offset += (align / BITS_PER_UNIT) - 1;
  pool->offset &= ~ ((align / BITS_PER_UNIT) - 1);

  desc->next = NULL;
  desc->constant = tmp.constant;
  desc->offset = pool->offset;
  desc->hash = hash;
  desc->mode = mode;
  desc->align = align;
  desc->labelno = const_labelno;
  desc->mark = 0;

  pool->offset += GET_MODE_SIZE (mode);
  if (pool->last)
    pool->last->next = desc;
  else
    pool->first = pool->last = desc;
  pool->last = desc;

  /* Create a string containing the label name, in LABEL.  */
  ASM_GENERATE_INTERNAL_LABEL (label, "LC", const_labelno);
  ++const_labelno;

  /* Construct the SYMBOL_REF.  Make sure to mark it as belonging to
     the constants pool.  */
  if (use_object_blocks_p () && targetm.use_blocks_for_constant_p (mode, x))
    {
      section *sect = targetm.asm_out.select_rtx_section (mode, x, align);
      symbol = create_block_symbol (ggc_strdup (label),
				    get_block_for_section (sect), -1);
    }
  else
    symbol = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (label));
  desc->sym = symbol;
  SYMBOL_REF_FLAGS (symbol) |= SYMBOL_FLAG_LOCAL;
  CONSTANT_POOL_ADDRESS_P (symbol) = 1;
  SET_SYMBOL_REF_CONSTANT (symbol, desc);

  /* Construct the MEM.  */
  desc->mem = def = gen_const_mem (mode, symbol);
  set_mem_attributes (def, lang_hooks.types.type_for_mode (mode, 0), 1);
  set_mem_align (def, align);

  /* If we're dropping a label to the constant pool, make sure we
     don't delete it.  */
  if (GET_CODE (x) == LABEL_REF)
    LABEL_PRESERVE_P (XEXP (x, 0)) = 1;

  return copy_rtx (def);
}

/* Given a constant pool SYMBOL_REF, return the corresponding constant.  */

rtx
get_pool_constant (rtx addr)
{
  return SYMBOL_REF_CONSTANT (addr)->constant;
}

/* Given a constant pool SYMBOL_REF, return the corresponding constant
   and whether it has been output or not.  */

rtx
get_pool_constant_mark (rtx addr, bool *pmarked)
{
  struct constant_descriptor_rtx *desc;

  desc = SYMBOL_REF_CONSTANT (addr);
  *pmarked = (desc->mark != 0);
  return desc->constant;
}

/* Similar, return the mode.  */

enum machine_mode
get_pool_mode (rtx addr)
{
  return SYMBOL_REF_CONSTANT (addr)->mode;
}

/* Return the size of the constant pool.  */

int
get_pool_size (void)
{
  return cfun->varasm->pool->offset;
}

/* Worker function for output_constant_pool_1.  Emit assembly for X
   in MODE with known alignment ALIGN.  */

static void
output_constant_pool_2 (enum machine_mode mode, rtx x, unsigned int align)
{
  switch (GET_MODE_CLASS (mode))
    {
    case MODE_FLOAT:
    case MODE_DECIMAL_FLOAT:
      {
	REAL_VALUE_TYPE r;

	gcc_assert (GET_CODE (x) == CONST_DOUBLE);
	REAL_VALUE_FROM_CONST_DOUBLE (r, x);
	assemble_real (r, mode, align);
	break;
      }

    case MODE_INT:
    case MODE_PARTIAL_INT:
      assemble_integer (x, GET_MODE_SIZE (mode), align, 1);
      break;

    case MODE_VECTOR_FLOAT:
    case MODE_VECTOR_INT:
      {
	int i, units;
        enum machine_mode submode = GET_MODE_INNER (mode);
	unsigned int subalign = MIN (align, GET_MODE_BITSIZE (submode));

	gcc_assert (GET_CODE (x) == CONST_VECTOR);
	units = CONST_VECTOR_NUNITS (x);

	for (i = 0; i < units; i++)
	  {
	    rtx elt = CONST_VECTOR_ELT (x, i);
	    output_constant_pool_2 (submode, elt, i ? subalign : align);
	  }
      }
      break;

    default:
      gcc_unreachable ();
    }
}

/* Worker function for output_constant_pool.  Emit constant DESC,
   giving it ALIGN bits of alignment.  */

static void
output_constant_pool_1 (struct constant_descriptor_rtx *desc,
			unsigned int align)
{
  rtx x, tmp;

  x = desc->constant;

  /* See if X is a LABEL_REF (or a CONST referring to a LABEL_REF)
     whose CODE_LABEL has been deleted.  This can occur if a jump table
     is eliminated by optimization.  If so, write a constant of zero
     instead.  Note that this can also happen by turning the
     CODE_LABEL into a NOTE.  */
  /* ??? This seems completely and utterly wrong.  Certainly it's
     not true for NOTE_INSN_DELETED_LABEL, but I disbelieve proper
     functioning even with INSN_DELETED_P and friends.  */

  tmp = x;
  switch (GET_CODE (x))
    {
    case CONST:
      if (GET_CODE (XEXP (x, 0)) != PLUS
	  || GET_CODE (XEXP (XEXP (x, 0), 0)) != LABEL_REF)
	break;
      tmp = XEXP (XEXP (x, 0), 0);
      /* FALLTHRU  */

    case LABEL_REF:
      tmp = XEXP (x, 0);
      gcc_assert (!INSN_DELETED_P (tmp));
      gcc_assert (!NOTE_P (tmp)
		  || NOTE_LINE_NUMBER (tmp) != NOTE_INSN_DELETED);
      break;

    default:
      break;
    }

#ifdef ASM_OUTPUT_SPECIAL_POOL_ENTRY
  ASM_OUTPUT_SPECIAL_POOL_ENTRY (asm_out_file, x, desc->mode,
				 align, desc->labelno, done);
#endif

  assemble_align (align);

  /* Output the label.  */
  targetm.asm_out.internal_label (asm_out_file, "LC", desc->labelno);

  /* Output the data.  */
  output_constant_pool_2 (desc->mode, x, align);

  /* Make sure all constants in SECTION_MERGE and not SECTION_STRINGS
     sections have proper size.  */
  if (align > GET_MODE_BITSIZE (desc->mode)
      && in_section
      && (in_section->common.flags & SECTION_MERGE))
    assemble_align (align);

#ifdef ASM_OUTPUT_SPECIAL_POOL_ENTRY
 done:
#endif
  return;
}

/* Given a SYMBOL_REF CURRENT_RTX, mark it and all constants it refers
   to as used.  Emit referenced deferred strings.  This function can
   be used with for_each_rtx to mark all SYMBOL_REFs in an rtx.  */

static int
mark_constant (rtx *current_rtx, void *data ATTRIBUTE_UNUSED)
{
  rtx x = *current_rtx;

  if (x == NULL_RTX || GET_CODE (x) != SYMBOL_REF)
    return 0;

  if (CONSTANT_POOL_ADDRESS_P (x))
    {
      struct constant_descriptor_rtx *desc = SYMBOL_REF_CONSTANT (x);
      if (desc->mark == 0)
	{
	  desc->mark = 1;
	  for_each_rtx (&desc->constant, mark_constant, NULL);
	}
    }
  else if (TREE_CONSTANT_POOL_ADDRESS_P (x))
    {
      tree exp = SYMBOL_REF_DECL (x);
      if (!TREE_ASM_WRITTEN (exp))
	{
	  n_deferred_constants--;
	  output_constant_def_contents (x);
	}
    }

  return -1;
}

/* Look through appropriate parts of INSN, marking all entries in the
   constant pool which are actually being used.  Entries that are only
   referenced by other constants are also marked as used.  Emit
   deferred strings that are used.  */

static void
mark_constants (rtx insn)
{
  if (!INSN_P (insn))
    return;

  /* Insns may appear inside a SEQUENCE.  Only check the patterns of
     insns, not any notes that may be attached.  We don't want to mark
     a constant just because it happens to appear in a REG_EQUIV note.  */
  if (GET_CODE (PATTERN (insn)) == SEQUENCE)
    {
      rtx seq = PATTERN (insn);
      int i, n = XVECLEN (seq, 0);
      for (i = 0; i < n; ++i)
	{
	  rtx subinsn = XVECEXP (seq, 0, i);
	  if (INSN_P (subinsn))
	    for_each_rtx (&PATTERN (subinsn), mark_constant, NULL);
	}
    }
  else
    for_each_rtx (&PATTERN (insn), mark_constant, NULL);
}

/* Look through the instructions for this function, and mark all the
   entries in POOL which are actually being used.  Emit deferred constants
   which have indeed been used.  */

static void
mark_constant_pool (void)
{
  rtx insn, link;

  if (!current_function_uses_const_pool && n_deferred_constants == 0)
    return;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    mark_constants (insn);

  for (link = current_function_epilogue_delay_list;
       link;
       link = XEXP (link, 1))
    mark_constants (XEXP (link, 0));
}

/* Write all the constants in POOL.  */

static void
output_constant_pool_contents (struct rtx_constant_pool *pool)
{
  struct constant_descriptor_rtx *desc;

  for (desc = pool->first; desc ; desc = desc->next)
    if (desc->mark)
      {
	/* If the constant is part of an object_block, make sure that
	   the constant has been positioned within its block, but do not
	   write out its definition yet.  output_object_blocks will do
	   that later.  */
	if (SYMBOL_REF_HAS_BLOCK_INFO_P (desc->sym)
	    && SYMBOL_REF_BLOCK (desc->sym))
	  place_block_symbol (desc->sym);
	else
	  {
	    switch_to_section (targetm.asm_out.select_rtx_section
			       (desc->mode, desc->constant, desc->align));
	    output_constant_pool_1 (desc, desc->align);
	  }
      }
}

/* Mark all constants that are used in the current function, then write
   out the function's private constant pool.  */

static void
output_constant_pool (const char *fnname ATTRIBUTE_UNUSED,
		      tree fndecl ATTRIBUTE_UNUSED)
{
  struct rtx_constant_pool *pool = cfun->varasm->pool;

  /* It is possible for gcc to call force_const_mem and then to later
     discard the instructions which refer to the constant.  In such a
     case we do not need to output the constant.  */
  mark_constant_pool ();

#ifdef ASM_OUTPUT_POOL_PROLOGUE
  ASM_OUTPUT_POOL_PROLOGUE (asm_out_file, fnname, fndecl, pool->offset);
#endif

  output_constant_pool_contents (pool);

#ifdef ASM_OUTPUT_POOL_EPILOGUE
  ASM_OUTPUT_POOL_EPILOGUE (asm_out_file, fnname, fndecl, pool->offset);
#endif
}

/* Write the contents of the shared constant pool.  */

void
output_shared_constant_pool (void)
{
  output_constant_pool_contents (shared_constant_pool);
}

/* Determine what kind of relocations EXP may need.  */

int
compute_reloc_for_constant (tree exp)
{
  int reloc = 0, reloc2;
  tree tem;

  /* Give the front-end a chance to convert VALUE to something that
     looks more like a constant to the back-end.  */
  exp = lang_hooks.expand_constant (exp);

  switch (TREE_CODE (exp))
    {
    case ADDR_EXPR:
    case FDESC_EXPR:
      /* Go inside any operations that get_inner_reference can handle and see
	 if what's inside is a constant: no need to do anything here for
	 addresses of variables or functions.  */
      for (tem = TREE_OPERAND (exp, 0); handled_component_p (tem);
	   tem = TREE_OPERAND (tem, 0))
	;

      if (TREE_PUBLIC (tem))
	reloc |= 2;
      else
	reloc |= 1;
      break;

    case PLUS_EXPR:
      reloc = compute_reloc_for_constant (TREE_OPERAND (exp, 0));
      reloc |= compute_reloc_for_constant (TREE_OPERAND (exp, 1));
      break;

    case MINUS_EXPR:
      reloc = compute_reloc_for_constant (TREE_OPERAND (exp, 0));
      reloc2 = compute_reloc_for_constant (TREE_OPERAND (exp, 1));
      /* The difference of two local labels is computable at link time.  */
      if (reloc == 1 && reloc2 == 1)
	reloc = 0;
      else
	reloc |= reloc2;
      break;

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
    case VIEW_CONVERT_EXPR:
      reloc = compute_reloc_for_constant (TREE_OPERAND (exp, 0));
      break;

    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;
	FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (exp), idx, tem)
	  if (tem != 0)
	    reloc |= compute_reloc_for_constant (tem);
      }
      break;

    default:
      break;
    }
  return reloc;
}

/* Find all the constants whose addresses are referenced inside of EXP,
   and make sure assembler code with a label has been output for each one.
   Indicate whether an ADDR_EXPR has been encountered.  */

static void
output_addressed_constants (tree exp)
{
  tree tem;

  /* Give the front-end a chance to convert VALUE to something that
     looks more like a constant to the back-end.  */
  exp = lang_hooks.expand_constant (exp);

  switch (TREE_CODE (exp))
    {
    case ADDR_EXPR:
    case FDESC_EXPR:
      /* Go inside any operations that get_inner_reference can handle and see
	 if what's inside is a constant: no need to do anything here for
	 addresses of variables or functions.  */
      for (tem = TREE_OPERAND (exp, 0); handled_component_p (tem);
	   tem = TREE_OPERAND (tem, 0))
	;

      /* If we have an initialized CONST_DECL, retrieve the initializer.  */
      if (TREE_CODE (tem) == CONST_DECL && DECL_INITIAL (tem))
	tem = DECL_INITIAL (tem);

      if (CONSTANT_CLASS_P (tem) || TREE_CODE (tem) == CONSTRUCTOR)
	output_constant_def (tem, 0);
      break;

    case PLUS_EXPR:
    case MINUS_EXPR:
      output_addressed_constants (TREE_OPERAND (exp, 1));
      /* Fall through.  */

    case NOP_EXPR:
    case CONVERT_EXPR:
    case NON_LVALUE_EXPR:
    case VIEW_CONVERT_EXPR:
      output_addressed_constants (TREE_OPERAND (exp, 0));
      break;

    case CONSTRUCTOR:
      {
	unsigned HOST_WIDE_INT idx;
	FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (exp), idx, tem)
	  if (tem != 0)
	    output_addressed_constants (tem);
      }
      break;

    default:
      break;
    }
}

/* Whether a constructor CTOR is a valid static constant initializer if all
   its elements are.  This used to be internal to initializer_constant_valid_p
   and has been exposed to let other functions like categorize_ctor_elements
   evaluate the property while walking a constructor for other purposes.  */

bool
constructor_static_from_elts_p (tree ctor)
{
  return (TREE_CONSTANT (ctor)
	  && (TREE_CODE (TREE_TYPE (ctor)) == UNION_TYPE
	      || TREE_CODE (TREE_TYPE (ctor)) == RECORD_TYPE)
	  && !VEC_empty (constructor_elt, CONSTRUCTOR_ELTS (ctor)));
}

/* Return nonzero if VALUE is a valid constant-valued expression
   for use in initializing a static variable; one that can be an
   element of a "constant" initializer.

   Return null_pointer_node if the value is absolute;
   if it is relocatable, return the variable that determines the relocation.
   We assume that VALUE has been folded as much as possible;
   therefore, we do not need to check for such things as
   arithmetic-combinations of integers.  */

tree
initializer_constant_valid_p (tree value, tree endtype)
{
  /* Give the front-end a chance to convert VALUE to something that
     looks more like a constant to the back-end.  */
  value = lang_hooks.expand_constant (value);

  switch (TREE_CODE (value))
    {
    case CONSTRUCTOR:
      if (constructor_static_from_elts_p (value))
	{
	  unsigned HOST_WIDE_INT idx;
	  tree elt;
	  bool absolute = true;

	  FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (value), idx, elt)
	    {
	      tree reloc;
	      reloc = initializer_constant_valid_p (elt, TREE_TYPE (elt));
	      if (!reloc)
		return NULL_TREE;
	      if (reloc != null_pointer_node)
		absolute = false;
	    }
	  /* For a non-absolute relocation, there is no single
	     variable that can be "the variable that determines the
	     relocation."  */
	  return absolute ? null_pointer_node : error_mark_node;
	}

      return TREE_STATIC (value) ? null_pointer_node : NULL_TREE;

    case INTEGER_CST:
    case VECTOR_CST:
    case REAL_CST:
    case STRING_CST:
    case COMPLEX_CST:
      return null_pointer_node;

    case ADDR_EXPR:
    case FDESC_EXPR:
      value = staticp (TREE_OPERAND (value, 0));
      if (value)
	{
	  /* "&(*a).f" is like unto pointer arithmetic.  If "a" turns out to
	     be a constant, this is old-skool offsetof-like nonsense.  */
	  if (TREE_CODE (value) == INDIRECT_REF
	      && TREE_CONSTANT (TREE_OPERAND (value, 0)))
	    return null_pointer_node;
	  /* Taking the address of a nested function involves a trampoline.  */
	  if (TREE_CODE (value) == FUNCTION_DECL
	      && ((decl_function_context (value)
		   && !DECL_NO_STATIC_CHAIN (value))
		  || DECL_DLLIMPORT_P (value)))
	    return NULL_TREE;
	  /* "&{...}" requires a temporary to hold the constructed
	     object.  */
	  if (TREE_CODE (value) == CONSTRUCTOR)
	    return NULL_TREE;
	}
      return value;

    case VIEW_CONVERT_EXPR:
    case NON_LVALUE_EXPR:
      return initializer_constant_valid_p (TREE_OPERAND (value, 0), endtype);

    case CONVERT_EXPR:
    case NOP_EXPR:
      {
	tree src;
	tree src_type;
	tree dest_type;

	src = TREE_OPERAND (value, 0);
	src_type = TREE_TYPE (src);
	dest_type = TREE_TYPE (value);

	/* Allow conversions between pointer types, floating-point
	   types, and offset types.  */
	if ((POINTER_TYPE_P (dest_type) && POINTER_TYPE_P (src_type))
	    || (FLOAT_TYPE_P (dest_type) && FLOAT_TYPE_P (src_type))
	    || (TREE_CODE (dest_type) == OFFSET_TYPE
		&& TREE_CODE (src_type) == OFFSET_TYPE))
	  return initializer_constant_valid_p (src, endtype);

	/* Allow length-preserving conversions between integer types.  */
	if (INTEGRAL_TYPE_P (dest_type) && INTEGRAL_TYPE_P (src_type)
	    && (TYPE_PRECISION (dest_type) == TYPE_PRECISION (src_type)))
	  return initializer_constant_valid_p (src, endtype);

	/* Allow conversions between other integer types only if
	   explicit value.  */
	if (INTEGRAL_TYPE_P (dest_type) && INTEGRAL_TYPE_P (src_type))
	  {
	    tree inner = initializer_constant_valid_p (src, endtype);
	    if (inner == null_pointer_node)
	      return null_pointer_node;
	    break;
	  }

	/* Allow (int) &foo provided int is as wide as a pointer.  */
	if (INTEGRAL_TYPE_P (dest_type) && POINTER_TYPE_P (src_type)
	    && (TYPE_PRECISION (dest_type) >= TYPE_PRECISION (src_type)))
	  return initializer_constant_valid_p (src, endtype);

	/* Likewise conversions from int to pointers, but also allow
	   conversions from 0.  */
	if ((POINTER_TYPE_P (dest_type)
	     || TREE_CODE (dest_type) == OFFSET_TYPE)
	    && INTEGRAL_TYPE_P (src_type))
	  {
	    if (TREE_CODE (src) == INTEGER_CST
		&& TYPE_PRECISION (dest_type) >= TYPE_PRECISION (src_type))
	      return null_pointer_node;
	    if (integer_zerop (src))
	      return null_pointer_node;
	    else if (TYPE_PRECISION (dest_type) <= TYPE_PRECISION (src_type))
	      return initializer_constant_valid_p (src, endtype);
	  }

	/* Allow conversions to struct or union types if the value
	   inside is okay.  */
	if (TREE_CODE (dest_type) == RECORD_TYPE
	    || TREE_CODE (dest_type) == UNION_TYPE)
	  return initializer_constant_valid_p (src, endtype);
      }
      break;

    case PLUS_EXPR:
      if (! INTEGRAL_TYPE_P (endtype)
	  || TYPE_PRECISION (endtype) >= POINTER_SIZE)
	{
	  tree valid0 = initializer_constant_valid_p (TREE_OPERAND (value, 0),
						      endtype);
	  tree valid1 = initializer_constant_valid_p (TREE_OPERAND (value, 1),
						      endtype);
	  /* If either term is absolute, use the other terms relocation.  */
	  if (valid0 == null_pointer_node)
	    return valid1;
	  if (valid1 == null_pointer_node)
	    return valid0;
	}
      break;

    case MINUS_EXPR:
      if (! INTEGRAL_TYPE_P (endtype)
	  || TYPE_PRECISION (endtype) >= POINTER_SIZE)
	{
	  tree valid0 = initializer_constant_valid_p (TREE_OPERAND (value, 0),
						      endtype);
	  tree valid1 = initializer_constant_valid_p (TREE_OPERAND (value, 1),
						      endtype);
	  /* Win if second argument is absolute.  */
	  if (valid1 == null_pointer_node)
	    return valid0;
	  /* Win if both arguments have the same relocation.
	     Then the value is absolute.  */
	  if (valid0 == valid1 && valid0 != 0)
	    return null_pointer_node;

	  /* Since GCC guarantees that string constants are unique in the
	     generated code, a subtraction between two copies of the same
	     constant string is absolute.  */
	  if (valid0 && TREE_CODE (valid0) == STRING_CST
	      && valid1 && TREE_CODE (valid1) == STRING_CST
	      && operand_equal_p (valid0, valid1, 1))
	    return null_pointer_node;
	}

      /* Support narrowing differences.  */
      if (INTEGRAL_TYPE_P (endtype))
	{
	  tree op0, op1;

	  op0 = TREE_OPERAND (value, 0);
	  op1 = TREE_OPERAND (value, 1);

	  /* Like STRIP_NOPS except allow the operand mode to widen.
	     This works around a feature of fold that simplifies
	     (int)(p1 - p2) to ((int)p1 - (int)p2) under the theory
	     that the narrower operation is cheaper.  */

	  while (TREE_CODE (op0) == NOP_EXPR
		 || TREE_CODE (op0) == CONVERT_EXPR
		 || TREE_CODE (op0) == NON_LVALUE_EXPR)
	    {
	      tree inner = TREE_OPERAND (op0, 0);
	      if (inner == error_mark_node
	          || ! INTEGRAL_MODE_P (TYPE_MODE (TREE_TYPE (inner)))
		  || (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (op0)))
		      > GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (inner)))))
		break;
	      op0 = inner;
	    }

	  while (TREE_CODE (op1) == NOP_EXPR
		 || TREE_CODE (op1) == CONVERT_EXPR
		 || TREE_CODE (op1) == NON_LVALUE_EXPR)
	    {
	      tree inner = TREE_OPERAND (op1, 0);
	      if (inner == error_mark_node
	          || ! INTEGRAL_MODE_P (TYPE_MODE (TREE_TYPE (inner)))
		  || (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (op1)))
		      > GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (inner)))))
		break;
	      op1 = inner;
	    }

	  op0 = initializer_constant_valid_p (op0, endtype);
	  op1 = initializer_constant_valid_p (op1, endtype);

	  /* Both initializers must be known.  */
	  if (op0 && op1)
	    {
	      if (op0 == op1)
		return null_pointer_node;

	      /* Support differences between labels.  */
	      if (TREE_CODE (op0) == LABEL_DECL
		  && TREE_CODE (op1) == LABEL_DECL)
		return null_pointer_node;

	      if (TREE_CODE (op0) == STRING_CST
		  && TREE_CODE (op1) == STRING_CST
		  && operand_equal_p (op0, op1, 1))
		return null_pointer_node;
	    }
	}
      break;

    default:
      break;
    }

  return 0;
}

/* Output assembler code for constant EXP to FILE, with no label.
   This includes the pseudo-op such as ".int" or ".byte", and a newline.
   Assumes output_addressed_constants has been done on EXP already.

   Generate exactly SIZE bytes of assembler data, padding at the end
   with zeros if necessary.  SIZE must always be specified.

   SIZE is important for structure constructors,
   since trailing members may have been omitted from the constructor.
   It is also important for initialization of arrays from string constants
   since the full length of the string constant might not be wanted.
   It is also needed for initialization of unions, where the initializer's
   type is just one member, and that may not be as long as the union.

   There a case in which we would fail to output exactly SIZE bytes:
   for a structure constructor that wants to produce more than SIZE bytes.
   But such constructors will never be generated for any possible input.

   ALIGN is the alignment of the data in bits.  */

void
output_constant (tree exp, unsigned HOST_WIDE_INT size, unsigned int align)
{
  enum tree_code code;
  unsigned HOST_WIDE_INT thissize;

  /* Some front-ends use constants other than the standard language-independent
     varieties, but which may still be output directly.  Give the front-end a
     chance to convert EXP to a language-independent representation.  */
  exp = lang_hooks.expand_constant (exp);

  if (size == 0 || flag_syntax_only)
    return;

  /* See if we're trying to initialize a pointer in a non-default mode
     to the address of some declaration somewhere.  If the target says
     the mode is valid for pointers, assume the target has a way of
     resolving it.  */
  if (TREE_CODE (exp) == NOP_EXPR
      && POINTER_TYPE_P (TREE_TYPE (exp))
      && targetm.valid_pointer_mode (TYPE_MODE (TREE_TYPE (exp))))
    {
      tree saved_type = TREE_TYPE (exp);

      /* Peel off any intermediate conversions-to-pointer for valid
	 pointer modes.  */
      while (TREE_CODE (exp) == NOP_EXPR
	     && POINTER_TYPE_P (TREE_TYPE (exp))
	     && targetm.valid_pointer_mode (TYPE_MODE (TREE_TYPE (exp))))
	exp = TREE_OPERAND (exp, 0);

      /* If what we're left with is the address of something, we can
	 convert the address to the final type and output it that
	 way.  */
      if (TREE_CODE (exp) == ADDR_EXPR)
	exp = build1 (ADDR_EXPR, saved_type, TREE_OPERAND (exp, 0));
      /* Likewise for constant ints.  */
      else if (TREE_CODE (exp) == INTEGER_CST)
	exp = build_int_cst_wide (saved_type, TREE_INT_CST_LOW (exp),
				  TREE_INT_CST_HIGH (exp));
      
    }

  /* Eliminate any conversions since we'll be outputting the underlying
     constant.  */
  while (TREE_CODE (exp) == NOP_EXPR || TREE_CODE (exp) == CONVERT_EXPR
	 || TREE_CODE (exp) == NON_LVALUE_EXPR
	 || TREE_CODE (exp) == VIEW_CONVERT_EXPR)
    {
      HOST_WIDE_INT type_size = int_size_in_bytes (TREE_TYPE (exp));
      HOST_WIDE_INT op_size = int_size_in_bytes (TREE_TYPE (TREE_OPERAND (exp, 0)));

      /* Make sure eliminating the conversion is really a no-op, except with
	 VIEW_CONVERT_EXPRs to allow for wild Ada unchecked conversions and
	 union types to allow for Ada unchecked unions.  */
      if (type_size > op_size
	  && TREE_CODE (exp) != VIEW_CONVERT_EXPR
	  && TREE_CODE (TREE_TYPE (exp)) != UNION_TYPE)
	/* Keep the conversion. */
	break;
      else
	exp = TREE_OPERAND (exp, 0);
    }

  code = TREE_CODE (TREE_TYPE (exp));
  thissize = int_size_in_bytes (TREE_TYPE (exp));

  /* Give the front end another chance to expand constants.  */
  exp = lang_hooks.expand_constant (exp);

  /* Allow a constructor with no elements for any data type.
     This means to fill the space with zeros.  */
  if (TREE_CODE (exp) == CONSTRUCTOR
      && VEC_empty (constructor_elt, CONSTRUCTOR_ELTS (exp)))
    {
      assemble_zeros (size);
      return;
    }

  if (TREE_CODE (exp) == FDESC_EXPR)
    {
#ifdef ASM_OUTPUT_FDESC
      HOST_WIDE_INT part = tree_low_cst (TREE_OPERAND (exp, 1), 0);
      tree decl = TREE_OPERAND (exp, 0);
      ASM_OUTPUT_FDESC (asm_out_file, decl, part);
#else
      gcc_unreachable ();
#endif
      return;
    }

  /* Now output the underlying data.  If we've handling the padding, return.
     Otherwise, break and ensure SIZE is the size written.  */
  switch (code)
    {
    case BOOLEAN_TYPE:
    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    case OFFSET_TYPE:
      if (! assemble_integer (expand_expr (exp, NULL_RTX, VOIDmode,
					   EXPAND_INITIALIZER),
			      MIN (size, thissize), align, 0))
	error ("initializer for integer value is too complicated");
      break;

    case REAL_TYPE:
      if (TREE_CODE (exp) != REAL_CST)
	error ("initializer for floating value is not a floating constant");

      assemble_real (TREE_REAL_CST (exp), TYPE_MODE (TREE_TYPE (exp)), align);
      break;

    case COMPLEX_TYPE:
      output_constant (TREE_REALPART (exp), thissize / 2, align);
      output_constant (TREE_IMAGPART (exp), thissize / 2,
		       min_align (align, BITS_PER_UNIT * (thissize / 2)));
      break;

    case ARRAY_TYPE:
    case VECTOR_TYPE:
      switch (TREE_CODE (exp))
	{
	case CONSTRUCTOR:
	  output_constructor (exp, size, align);
	  return;
	case STRING_CST:
	  thissize = MIN ((unsigned HOST_WIDE_INT)TREE_STRING_LENGTH (exp),
			  size);
	  assemble_string (TREE_STRING_POINTER (exp), thissize);
	  break;

	case VECTOR_CST:
	  {
	    int elt_size;
	    tree link;
	    unsigned int nalign;
	    enum machine_mode inner;

	    inner = TYPE_MODE (TREE_TYPE (TREE_TYPE (exp)));
	    nalign = MIN (align, GET_MODE_ALIGNMENT (inner));

	    elt_size = GET_MODE_SIZE (inner);

	    link = TREE_VECTOR_CST_ELTS (exp);
	    output_constant (TREE_VALUE (link), elt_size, align);
	    thissize = elt_size;
	    while ((link = TREE_CHAIN (link)) != NULL)
	      {
		output_constant (TREE_VALUE (link), elt_size, nalign);
		thissize += elt_size;
	      }
	    break;
	  }
	default:
	  gcc_unreachable ();
	}
      break;

    case RECORD_TYPE:
    case UNION_TYPE:
      gcc_assert (TREE_CODE (exp) == CONSTRUCTOR);
      output_constructor (exp, size, align);
      return;

    case ERROR_MARK:
      return;

    default:
      gcc_unreachable ();
    }

  if (size > thissize)
    assemble_zeros (size - thissize);
}


/* Subroutine of output_constructor, used for computing the size of
   arrays of unspecified length.  VAL must be a CONSTRUCTOR of an array
   type with an unspecified upper bound.  */

static unsigned HOST_WIDE_INT
array_size_for_constructor (tree val)
{
  tree max_index, i;
  unsigned HOST_WIDE_INT cnt;
  tree index, value, tmp;

  /* This code used to attempt to handle string constants that are not
     arrays of single-bytes, but nothing else does, so there's no point in
     doing it here.  */
  if (TREE_CODE (val) == STRING_CST)
    return TREE_STRING_LENGTH (val);

  max_index = NULL_TREE;
  FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (val), cnt, index, value)
    {
      if (TREE_CODE (index) == RANGE_EXPR)
	index = TREE_OPERAND (index, 1);
      if (max_index == NULL_TREE || tree_int_cst_lt (max_index, index))
	max_index = index;
    }

  if (max_index == NULL_TREE)
    return 0;

  /* Compute the total number of array elements.  */
  tmp = TYPE_MIN_VALUE (TYPE_DOMAIN (TREE_TYPE (val)));
  i = size_binop (MINUS_EXPR, fold_convert (sizetype, max_index),
		  fold_convert (sizetype, tmp));
  i = size_binop (PLUS_EXPR, i, build_int_cst (sizetype, 1));

  /* Multiply by the array element unit size to find number of bytes.  */
  i = size_binop (MULT_EXPR, i, TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (val))));

  return tree_low_cst (i, 1);
}

/* Subroutine of output_constant, used for CONSTRUCTORs (aggregate constants).
   Generate at least SIZE bytes, padding if necessary.  */

static void
output_constructor (tree exp, unsigned HOST_WIDE_INT size,
		    unsigned int align)
{
  tree type = TREE_TYPE (exp);
  tree field = 0;
  tree min_index = 0;
  /* Number of bytes output or skipped so far.
     In other words, current position within the constructor.  */
  HOST_WIDE_INT total_bytes = 0;
  /* Nonzero means BYTE contains part of a byte, to be output.  */
  int byte_buffer_in_use = 0;
  int byte = 0;
  unsigned HOST_WIDE_INT cnt;
  constructor_elt *ce;

  gcc_assert (HOST_BITS_PER_WIDE_INT >= BITS_PER_UNIT);

  if (TREE_CODE (type) == RECORD_TYPE)
    field = TYPE_FIELDS (type);

  if (TREE_CODE (type) == ARRAY_TYPE
      && TYPE_DOMAIN (type) != 0)
    min_index = TYPE_MIN_VALUE (TYPE_DOMAIN (type));

  /* As LINK goes through the elements of the constant,
     FIELD goes through the structure fields, if the constant is a structure.
     if the constant is a union, then we override this,
     by getting the field from the TREE_LIST element.
     But the constant could also be an array.  Then FIELD is zero.

     There is always a maximum of one element in the chain LINK for unions
     (even if the initializer in a source program incorrectly contains
     more one).  */
  for (cnt = 0;
       VEC_iterate (constructor_elt, CONSTRUCTOR_ELTS (exp), cnt, ce);
       cnt++, field = field ? TREE_CHAIN (field) : 0)
    {
      tree val = ce->value;
      tree index = 0;

      /* The element in a union constructor specifies the proper field
	 or index.  */
      if ((TREE_CODE (type) == RECORD_TYPE || TREE_CODE (type) == UNION_TYPE
	   || TREE_CODE (type) == QUAL_UNION_TYPE)
	  && ce->index != 0)
	field = ce->index;

      else if (TREE_CODE (type) == ARRAY_TYPE)
	index = ce->index;

#ifdef ASM_COMMENT_START
      if (field && flag_verbose_asm)
	fprintf (asm_out_file, "%s %s:\n",
		 ASM_COMMENT_START,
		 DECL_NAME (field)
		 ? IDENTIFIER_POINTER (DECL_NAME (field))
		 : "<anonymous>");
#endif

      /* Eliminate the marker that makes a cast not be an lvalue.  */
      if (val != 0)
	STRIP_NOPS (val);

      if (index && TREE_CODE (index) == RANGE_EXPR)
	{
	  unsigned HOST_WIDE_INT fieldsize
	    = int_size_in_bytes (TREE_TYPE (type));
	  HOST_WIDE_INT lo_index = tree_low_cst (TREE_OPERAND (index, 0), 0);
	  HOST_WIDE_INT hi_index = tree_low_cst (TREE_OPERAND (index, 1), 0);
	  HOST_WIDE_INT index;
	  unsigned int align2 = min_align (align, fieldsize * BITS_PER_UNIT);

	  for (index = lo_index; index <= hi_index; index++)
	    {
	      /* Output the element's initial value.  */
	      if (val == 0)
		assemble_zeros (fieldsize);
	      else
		output_constant (val, fieldsize, align2);

	      /* Count its size.  */
	      total_bytes += fieldsize;
	    }
	}
      else if (field == 0 || !DECL_BIT_FIELD (field))
	{
	  /* An element that is not a bit-field.  */

	  unsigned HOST_WIDE_INT fieldsize;
	  /* Since this structure is static,
	     we know the positions are constant.  */
	  HOST_WIDE_INT pos = field ? int_byte_position (field) : 0;
	  unsigned int align2;

	  if (index != 0)
	    pos = (tree_low_cst (TYPE_SIZE_UNIT (TREE_TYPE (val)), 1)
		   * (tree_low_cst (index, 0) - tree_low_cst (min_index, 0)));

	  /* Output any buffered-up bit-fields preceding this element.  */
	  if (byte_buffer_in_use)
	    {
	      assemble_integer (GEN_INT (byte), 1, BITS_PER_UNIT, 1);
	      total_bytes++;
	      byte_buffer_in_use = 0;
	    }

	  /* Advance to offset of this element.
	     Note no alignment needed in an array, since that is guaranteed
	     if each element has the proper size.  */
	  if ((field != 0 || index != 0) && pos != total_bytes)
	    {
	      gcc_assert (pos >= total_bytes);
	      assemble_zeros (pos - total_bytes);
	      total_bytes = pos;
	    }

	  /* Find the alignment of this element.  */
	  align2 = min_align (align, BITS_PER_UNIT * pos);

	  /* Determine size this element should occupy.  */
	  if (field)
	    {
	      fieldsize = 0;

	      /* If this is an array with an unspecified upper bound,
		 the initializer determines the size.  */
	      /* ??? This ought to only checked if DECL_SIZE_UNIT is NULL,
		 but we cannot do this until the deprecated support for
		 initializing zero-length array members is removed.  */
	      if (TREE_CODE (TREE_TYPE (field)) == ARRAY_TYPE
		  && TYPE_DOMAIN (TREE_TYPE (field))
		  && ! TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (field))))
		{
		  fieldsize = array_size_for_constructor (val);
		  /* Given a non-empty initialization, this field had
		     better be last.  */
		  gcc_assert (!fieldsize || !TREE_CHAIN (field));
		}
	      else if (DECL_SIZE_UNIT (field))
		{
		  /* ??? This can't be right.  If the decl size overflows
		     a host integer we will silently emit no data.  */
		  if (host_integerp (DECL_SIZE_UNIT (field), 1))
		    fieldsize = tree_low_cst (DECL_SIZE_UNIT (field), 1);
		}
	    }
	  else
	    fieldsize = int_size_in_bytes (TREE_TYPE (type));

	  /* Output the element's initial value.  */
	  if (val == 0)
	    assemble_zeros (fieldsize);
	  else
	    output_constant (val, fieldsize, align2);

	  /* Count its size.  */
	  total_bytes += fieldsize;
	}
      else if (val != 0 && TREE_CODE (val) != INTEGER_CST)
	error ("invalid initial value for member %qs",
	       IDENTIFIER_POINTER (DECL_NAME (field)));
      else
	{
	  /* Element that is a bit-field.  */

	  HOST_WIDE_INT next_offset = int_bit_position (field);
	  HOST_WIDE_INT end_offset
	    = (next_offset + tree_low_cst (DECL_SIZE (field), 1));

	  if (val == 0)
	    val = integer_zero_node;

	  /* If this field does not start in this (or, next) byte,
	     skip some bytes.  */
	  if (next_offset / BITS_PER_UNIT != total_bytes)
	    {
	      /* Output remnant of any bit field in previous bytes.  */
	      if (byte_buffer_in_use)
		{
		  assemble_integer (GEN_INT (byte), 1, BITS_PER_UNIT, 1);
		  total_bytes++;
		  byte_buffer_in_use = 0;
		}

	      /* If still not at proper byte, advance to there.  */
	      if (next_offset / BITS_PER_UNIT != total_bytes)
		{
		  gcc_assert (next_offset / BITS_PER_UNIT >= total_bytes);
		  assemble_zeros (next_offset / BITS_PER_UNIT - total_bytes);
		  total_bytes = next_offset / BITS_PER_UNIT;
		}
	    }

	  if (! byte_buffer_in_use)
	    byte = 0;

	  /* We must split the element into pieces that fall within
	     separate bytes, and combine each byte with previous or
	     following bit-fields.  */

	  /* next_offset is the offset n fbits from the beginning of
	     the structure to the next bit of this element to be processed.
	     end_offset is the offset of the first bit past the end of
	     this element.  */
	  while (next_offset < end_offset)
	    {
	      int this_time;
	      int shift;
	      HOST_WIDE_INT value;
	      HOST_WIDE_INT next_byte = next_offset / BITS_PER_UNIT;
	      HOST_WIDE_INT next_bit = next_offset % BITS_PER_UNIT;

	      /* Advance from byte to byte
		 within this element when necessary.  */
	      while (next_byte != total_bytes)
		{
		  assemble_integer (GEN_INT (byte), 1, BITS_PER_UNIT, 1);
		  total_bytes++;
		  byte = 0;
		}

	      /* Number of bits we can process at once
		 (all part of the same byte).  */
	      this_time = MIN (end_offset - next_offset,
			       BITS_PER_UNIT - next_bit);
	      if (BYTES_BIG_ENDIAN)
		{
		  /* On big-endian machine, take the most significant bits
		     first (of the bits that are significant)
		     and put them into bytes from the most significant end.  */
		  shift = end_offset - next_offset - this_time;

		  /* Don't try to take a bunch of bits that cross
		     the word boundary in the INTEGER_CST. We can
		     only select bits from the LOW or HIGH part
		     not from both.  */
		  if (shift < HOST_BITS_PER_WIDE_INT
		      && shift + this_time > HOST_BITS_PER_WIDE_INT)
		    {
		      this_time = shift + this_time - HOST_BITS_PER_WIDE_INT;
		      shift = HOST_BITS_PER_WIDE_INT;
		    }

		  /* Now get the bits from the appropriate constant word.  */
		  if (shift < HOST_BITS_PER_WIDE_INT)
		    value = TREE_INT_CST_LOW (val);
		  else
		    {
		      gcc_assert (shift < 2 * HOST_BITS_PER_WIDE_INT);
		      value = TREE_INT_CST_HIGH (val);
		      shift -= HOST_BITS_PER_WIDE_INT;
		    }

		  /* Get the result. This works only when:
		     1 <= this_time <= HOST_BITS_PER_WIDE_INT.  */
		  byte |= (((value >> shift)
			    & (((HOST_WIDE_INT) 2 << (this_time - 1)) - 1))
			   << (BITS_PER_UNIT - this_time - next_bit));
		}
	      else
		{
		  /* On little-endian machines,
		     take first the least significant bits of the value
		     and pack them starting at the least significant
		     bits of the bytes.  */
		  shift = next_offset - int_bit_position (field);

		  /* Don't try to take a bunch of bits that cross
		     the word boundary in the INTEGER_CST. We can
		     only select bits from the LOW or HIGH part
		     not from both.  */
		  if (shift < HOST_BITS_PER_WIDE_INT
		      && shift + this_time > HOST_BITS_PER_WIDE_INT)
		    this_time = (HOST_BITS_PER_WIDE_INT - shift);

		  /* Now get the bits from the appropriate constant word.  */
		  if (shift < HOST_BITS_PER_WIDE_INT)
		    value = TREE_INT_CST_LOW (val);
		  else
		    {
		      gcc_assert (shift < 2 * HOST_BITS_PER_WIDE_INT);
		      value = TREE_INT_CST_HIGH (val);
		      shift -= HOST_BITS_PER_WIDE_INT;
		    }

		  /* Get the result. This works only when:
		     1 <= this_time <= HOST_BITS_PER_WIDE_INT.  */
		  byte |= (((value >> shift)
			    & (((HOST_WIDE_INT) 2 << (this_time - 1)) - 1))
			   << next_bit);
		}

	      next_offset += this_time;
	      byte_buffer_in_use = 1;
	    }
	}
    }

  if (byte_buffer_in_use)
    {
      assemble_integer (GEN_INT (byte), 1, BITS_PER_UNIT, 1);
      total_bytes++;
    }

  if ((unsigned HOST_WIDE_INT)total_bytes < size)
    assemble_zeros (size - total_bytes);
}

/* This TREE_LIST contains any weak symbol declarations waiting
   to be emitted.  */
static GTY(()) tree weak_decls;

/* Mark DECL as weak.  */

static void
mark_weak (tree decl)
{
  DECL_WEAK (decl) = 1;

  if (DECL_RTL_SET_P (decl)
      && MEM_P (DECL_RTL (decl))
      && XEXP (DECL_RTL (decl), 0)
      && GET_CODE (XEXP (DECL_RTL (decl), 0)) == SYMBOL_REF)
    SYMBOL_REF_WEAK (XEXP (DECL_RTL (decl), 0)) = 1;
}

/* Merge weak status between NEWDECL and OLDDECL.  */

void
merge_weak (tree newdecl, tree olddecl)
{
  if (DECL_WEAK (newdecl) == DECL_WEAK (olddecl))
    {
      if (DECL_WEAK (newdecl) && SUPPORTS_WEAK)
        {
          tree *pwd;
          /* We put the NEWDECL on the weak_decls list at some point
             and OLDDECL as well.  Keep just OLDDECL on the list.  */
	  for (pwd = &weak_decls; *pwd; pwd = &TREE_CHAIN (*pwd))
	    if (TREE_VALUE (*pwd) == newdecl)
	      {
	        *pwd = TREE_CHAIN (*pwd);
		break;
	      }
        }
      return;
    }

  if (DECL_WEAK (newdecl))
    {
      tree wd;

      /* NEWDECL is weak, but OLDDECL is not.  */

      /* If we already output the OLDDECL, we're in trouble; we can't
	 go back and make it weak.  This error cannot caught in
	 declare_weak because the NEWDECL and OLDDECL was not yet
	 been merged; therefore, TREE_ASM_WRITTEN was not set.  */
      if (TREE_ASM_WRITTEN (olddecl))
	error ("weak declaration of %q+D must precede definition",
	       newdecl);

      /* If we've already generated rtl referencing OLDDECL, we may
	 have done so in a way that will not function properly with
	 a weak symbol.  */
      else if (TREE_USED (olddecl)
	       && TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (olddecl)))
	warning (0, "weak declaration of %q+D after first use results "
                 "in unspecified behavior", newdecl);

      if (SUPPORTS_WEAK)
	{
	  /* We put the NEWDECL on the weak_decls list at some point.
	     Replace it with the OLDDECL.  */
	  for (wd = weak_decls; wd; wd = TREE_CHAIN (wd))
	    if (TREE_VALUE (wd) == newdecl)
	      {
		TREE_VALUE (wd) = olddecl;
		break;
	      }
	  /* We may not find the entry on the list.  If NEWDECL is a
	     weak alias, then we will have already called
	     globalize_decl to remove the entry; in that case, we do
	     not need to do anything.  */
	}

      /* Make the OLDDECL weak; it's OLDDECL that we'll be keeping.  */
      mark_weak (olddecl);
    }
  else
    /* OLDDECL was weak, but NEWDECL was not explicitly marked as
       weak.  Just update NEWDECL to indicate that it's weak too.  */
    mark_weak (newdecl);
}

/* Declare DECL to be a weak symbol.  */

void
declare_weak (tree decl)
{
  if (! TREE_PUBLIC (decl))
    error ("weak declaration of %q+D must be public", decl);
  else if (TREE_CODE (decl) == FUNCTION_DECL && TREE_ASM_WRITTEN (decl))
    error ("weak declaration of %q+D must precede definition", decl);
  else if (SUPPORTS_WEAK)
    {
      if (! DECL_WEAK (decl))
	weak_decls = tree_cons (NULL, decl, weak_decls);
    }
  else
    warning (0, "weak declaration of %q+D not supported", decl);

  mark_weak (decl);
}

static void
weak_finish_1 (tree decl)
{
#if defined (ASM_WEAKEN_DECL) || defined (ASM_WEAKEN_LABEL)
  const char *const name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
#endif

  if (! TREE_USED (decl))
    return;

#ifdef ASM_WEAKEN_DECL
  ASM_WEAKEN_DECL (asm_out_file, decl, name, NULL);
#else
#ifdef ASM_WEAKEN_LABEL
  ASM_WEAKEN_LABEL (asm_out_file, name);
#else
#ifdef ASM_OUTPUT_WEAK_ALIAS
  {
    static bool warn_once = 0;
    if (! warn_once)
      {
	warning (0, "only weak aliases are supported in this configuration");
	warn_once = 1;
      }
    return;
  }
#endif
#endif
#endif
}

/* This TREE_LIST contains weakref targets.  */

static GTY(()) tree weakref_targets;

/* Forward declaration.  */
static tree find_decl_and_mark_needed (tree decl, tree target);

/* Emit any pending weak declarations.  */

void
weak_finish (void)
{
  tree t;

  for (t = weakref_targets; t; t = TREE_CHAIN (t))
    {
      tree alias_decl = TREE_PURPOSE (t);
      tree target = ultimate_transparent_alias_target (&TREE_VALUE (t));

      if (! TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (alias_decl)))
	/* Remove alias_decl from the weak list, but leave entries for
	   the target alone.  */
	target = NULL_TREE;
#ifndef ASM_OUTPUT_WEAKREF
      else if (! TREE_SYMBOL_REFERENCED (target))
	{
	  /* Use ASM_WEAKEN_LABEL only if ASM_WEAKEN_DECL is not
	     defined, otherwise we and weak_finish_1 would use a
	     different macros.  */
# if defined ASM_WEAKEN_LABEL && ! defined ASM_WEAKEN_DECL
	  ASM_WEAKEN_LABEL (asm_out_file, IDENTIFIER_POINTER (target));
# else
	  tree decl = find_decl_and_mark_needed (alias_decl, target);

	  if (! decl)
	    {
	      decl = build_decl (TREE_CODE (alias_decl), target,
				 TREE_TYPE (alias_decl));

	      DECL_EXTERNAL (decl) = 1;
	      TREE_PUBLIC (decl) = 1;
	      DECL_ARTIFICIAL (decl) = 1;
	      TREE_NOTHROW (decl) = TREE_NOTHROW (alias_decl);
	      TREE_USED (decl) = 1;
	    }

	  weak_finish_1 (decl);
# endif
	}
#endif

      {
	tree *p;
	tree t2;

	/* Remove the alias and the target from the pending weak list
	   so that we do not emit any .weak directives for the former,
	   nor multiple .weak directives for the latter.  */
	for (p = &weak_decls; (t2 = *p) ; )
	  {
	    if (TREE_VALUE (t2) == alias_decl
		|| target == DECL_ASSEMBLER_NAME (TREE_VALUE (t2)))
	      *p = TREE_CHAIN (t2);
	    else
	      p = &TREE_CHAIN (t2);
	  }

	/* Remove other weakrefs to the same target, to speed things up.  */
	for (p = &TREE_CHAIN (t); (t2 = *p) ; )
	  {
	    if (target == ultimate_transparent_alias_target (&TREE_VALUE (t2)))
	      *p = TREE_CHAIN (t2);
	    else
	      p = &TREE_CHAIN (t2);
	  }
      }
    }

  for (t = weak_decls; t; t = TREE_CHAIN (t))
    {
      tree decl = TREE_VALUE (t);

      weak_finish_1 (decl);
    }
}

/* Emit the assembly bits to indicate that DECL is globally visible.  */

static void
globalize_decl (tree decl)
{
  const char *name = XSTR (XEXP (DECL_RTL (decl), 0), 0);

#if defined (ASM_WEAKEN_LABEL) || defined (ASM_WEAKEN_DECL)
  if (DECL_WEAK (decl))
    {
      tree *p, t;

#ifdef ASM_WEAKEN_DECL
      ASM_WEAKEN_DECL (asm_out_file, decl, name, 0);
#else
      ASM_WEAKEN_LABEL (asm_out_file, name);
#endif

      /* Remove this function from the pending weak list so that
	 we do not emit multiple .weak directives for it.  */
      for (p = &weak_decls; (t = *p) ; )
	{
	  if (DECL_ASSEMBLER_NAME (decl) == DECL_ASSEMBLER_NAME (TREE_VALUE (t)))
	    *p = TREE_CHAIN (t);
	  else
	    p = &TREE_CHAIN (t);
	}

      /* Remove weakrefs to the same target from the pending weakref
	 list, for the same reason.  */
      for (p = &weakref_targets; (t = *p) ; )
	{
	  if (DECL_ASSEMBLER_NAME (decl)
	      == ultimate_transparent_alias_target (&TREE_VALUE (t)))
	    *p = TREE_CHAIN (t);
	  else
	    p = &TREE_CHAIN (t);
	}

      return;
    }
#elif defined(ASM_MAKE_LABEL_LINKONCE)
  if (DECL_ONE_ONLY (decl))
    ASM_MAKE_LABEL_LINKONCE (asm_out_file, name);
#endif

  targetm.asm_out.globalize_label (asm_out_file, name);
}

/* We have to be able to tell cgraph about the needed-ness of the target
   of an alias.  This requires that the decl have been defined.  Aliases
   that precede their definition have to be queued for later processing.  */

typedef struct alias_pair GTY(())
{
  tree decl;
  tree target;
} alias_pair;

/* Define gc'd vector type.  */
DEF_VEC_O(alias_pair);
DEF_VEC_ALLOC_O(alias_pair,gc);

static GTY(()) VEC(alias_pair,gc) *alias_pairs;

/* Given an assembly name, find the decl it is associated with.  At the
   same time, mark it needed for cgraph.  */

static tree
find_decl_and_mark_needed (tree decl, tree target)
{
  struct cgraph_node *fnode = NULL;
  struct cgraph_varpool_node *vnode = NULL;

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      fnode = cgraph_node_for_asm (target);
      if (fnode == NULL)
	vnode = cgraph_varpool_node_for_asm (target);
    }
  else
    {
      vnode = cgraph_varpool_node_for_asm (target);
      if (vnode == NULL)
	fnode = cgraph_node_for_asm (target);
    }

  if (fnode)
    {
      /* We can't mark function nodes as used after cgraph global info
	 is finished.  This wouldn't generally be necessary, but C++
	 virtual table thunks are introduced late in the game and
	 might seem like they need marking, although in fact they
	 don't.  */
      if (! cgraph_global_info_ready)
	cgraph_mark_needed_node (fnode);
      return fnode->decl;
    }
  else if (vnode)
    {
      cgraph_varpool_mark_needed_node (vnode);
      return vnode->decl;
    }
  else
    return NULL_TREE;
}

/* Output the assembler code for a define (equate) using ASM_OUTPUT_DEF
   or ASM_OUTPUT_DEF_FROM_DECLS.  The function defines the symbol whose
   tree node is DECL to have the value of the tree node TARGET.  */

static void
do_assemble_alias (tree decl, tree target)
{
  if (TREE_ASM_WRITTEN (decl))
    return;

  TREE_ASM_WRITTEN (decl) = 1;
  TREE_ASM_WRITTEN (DECL_ASSEMBLER_NAME (decl)) = 1;

  if (lookup_attribute ("weakref", DECL_ATTRIBUTES (decl)))
    {
      ultimate_transparent_alias_target (&target);

      if (!TREE_SYMBOL_REFERENCED (target))
	weakref_targets = tree_cons (decl, target, weakref_targets);

#ifdef ASM_OUTPUT_WEAKREF
      ASM_OUTPUT_WEAKREF (asm_out_file, decl,
			  IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)),
			  IDENTIFIER_POINTER (target));
#else
      if (!SUPPORTS_WEAK)
	{
	  error ("%Jweakref is not supported in this configuration", decl);
	  return;
	}
#endif
      return;
    }

#ifdef ASM_OUTPUT_DEF
  /* Make name accessible from other files, if appropriate.  */

  if (TREE_PUBLIC (decl))
    {
      globalize_decl (decl);
      maybe_assemble_visibility (decl);
    }

# ifdef ASM_OUTPUT_DEF_FROM_DECLS
  ASM_OUTPUT_DEF_FROM_DECLS (asm_out_file, decl, target);
# else
  ASM_OUTPUT_DEF (asm_out_file,
		  IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl)),
		  IDENTIFIER_POINTER (target));
# endif
#elif defined (ASM_OUTPUT_WEAK_ALIAS) || defined (ASM_WEAKEN_DECL)
  {
    const char *name;
    tree *p, t;

    name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
# ifdef ASM_WEAKEN_DECL
    ASM_WEAKEN_DECL (asm_out_file, decl, name, IDENTIFIER_POINTER (target));
# else
    ASM_OUTPUT_WEAK_ALIAS (asm_out_file, name, IDENTIFIER_POINTER (target));
# endif
    /* Remove this function from the pending weak list so that
       we do not emit multiple .weak directives for it.  */
    for (p = &weak_decls; (t = *p) ; )
      if (DECL_ASSEMBLER_NAME (decl) == DECL_ASSEMBLER_NAME (TREE_VALUE (t)))
	*p = TREE_CHAIN (t);
      else
	p = &TREE_CHAIN (t);

    /* Remove weakrefs to the same target from the pending weakref
       list, for the same reason.  */
    for (p = &weakref_targets; (t = *p) ; )
      {
	if (DECL_ASSEMBLER_NAME (decl)
	    == ultimate_transparent_alias_target (&TREE_VALUE (t)))
	  *p = TREE_CHAIN (t);
	else
	  p = &TREE_CHAIN (t);
      }
  }
#endif
}

/* First pass of completing pending aliases.  Make sure that cgraph knows
   which symbols will be required.  */

void
finish_aliases_1 (void)
{
  unsigned i;
  alias_pair *p;

  for (i = 0; VEC_iterate (alias_pair, alias_pairs, i, p); i++)
    {
      tree target_decl;

      target_decl = find_decl_and_mark_needed (p->decl, p->target);
      if (target_decl == NULL)
	{
	  if (! lookup_attribute ("weakref", DECL_ATTRIBUTES (p->decl)))
	    error ("%q+D aliased to undefined symbol %qs",
		   p->decl, IDENTIFIER_POINTER (p->target));
	}
      else if (DECL_EXTERNAL (target_decl)
	       && ! lookup_attribute ("weakref", DECL_ATTRIBUTES (p->decl)))
	error ("%q+D aliased to external symbol %qs",
	       p->decl, IDENTIFIER_POINTER (p->target));
    }
}

/* Second pass of completing pending aliases.  Emit the actual assembly.
   This happens at the end of compilation and thus it is assured that the
   target symbol has been emitted.  */

void
finish_aliases_2 (void)
{
  unsigned i;
  alias_pair *p;

  for (i = 0; VEC_iterate (alias_pair, alias_pairs, i, p); i++)
    do_assemble_alias (p->decl, p->target);

  VEC_truncate (alias_pair, alias_pairs, 0);
}

/* Emit an assembler directive to make the symbol for DECL an alias to
   the symbol for TARGET.  */

void
assemble_alias (tree decl, tree target)
{
  tree target_decl;
  bool is_weakref = false;

  if (lookup_attribute ("weakref", DECL_ATTRIBUTES (decl)))
    {
      tree alias = DECL_ASSEMBLER_NAME (decl);

      is_weakref = true;

      ultimate_transparent_alias_target (&target);

      if (alias == target)
	error ("weakref %q+D ultimately targets itself", decl);
      else
	{
#ifndef ASM_OUTPUT_WEAKREF
	  IDENTIFIER_TRANSPARENT_ALIAS (alias) = 1;
	  TREE_CHAIN (alias) = target;
#endif
	}
      if (TREE_PUBLIC (decl))
	error ("weakref %q+D must have static linkage", decl);
    }
  else
    {
#if !defined (ASM_OUTPUT_DEF)
# if !defined(ASM_OUTPUT_WEAK_ALIAS) && !defined (ASM_WEAKEN_DECL)
      error ("%Jalias definitions not supported in this configuration", decl);
      return;
# else
      if (!DECL_WEAK (decl))
	{
	  error ("%Jonly weak aliases are supported in this configuration", decl);
	  return;
	}
# endif
#endif
    }

  /* We must force creation of DECL_RTL for debug info generation, even though
     we don't use it here.  */
  make_decl_rtl (decl);
  TREE_USED (decl) = 1;

  /* A quirk of the initial implementation of aliases required that the user
     add "extern" to all of them.  Which is silly, but now historical.  Do
     note that the symbol is in fact locally defined.  */
  if (! is_weakref)
    DECL_EXTERNAL (decl) = 0;

  /* Allow aliases to aliases.  */
  if (TREE_CODE (decl) == FUNCTION_DECL)
    cgraph_node (decl)->alias = true;
  else
    cgraph_varpool_node (decl)->alias = true;

  /* If the target has already been emitted, we don't have to queue the
     alias.  This saves a tad o memory.  */
  target_decl = find_decl_and_mark_needed (decl, target);
  if (target_decl && TREE_ASM_WRITTEN (target_decl))
    do_assemble_alias (decl, target);
  else
    {
      alias_pair *p = VEC_safe_push (alias_pair, gc, alias_pairs, NULL);
      p->decl = decl;
      p->target = target;
    }
}

/* Emit an assembler directive to set symbol for DECL visibility to
   the visibility type VIS, which must not be VISIBILITY_DEFAULT.  */

void
default_assemble_visibility (tree decl, int vis)
{
  static const char * const visibility_types[] = {
    NULL, "protected", "hidden", "internal"
  };

  const char *name, *type;

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  type = visibility_types[vis];

#ifdef HAVE_GAS_HIDDEN
  fprintf (asm_out_file, "\t.%s\t", type);
  assemble_name (asm_out_file, name);
  fprintf (asm_out_file, "\n");
#else
  warning (OPT_Wattributes, "visibility attribute not supported "
	   "in this configuration; ignored");
#endif
}

/* A helper function to call assemble_visibility when needed for a decl.  */

int
maybe_assemble_visibility (tree decl)
{
  enum symbol_visibility vis = DECL_VISIBILITY (decl);

  if (vis != VISIBILITY_DEFAULT)
    {
      targetm.asm_out.visibility (decl, vis);
      return 1;
    }
  else
    return 0;
}

/* Returns 1 if the target configuration supports defining public symbols
   so that one of them will be chosen at link time instead of generating a
   multiply-defined symbol error, whether through the use of weak symbols or
   a target-specific mechanism for having duplicates discarded.  */

int
supports_one_only (void)
{
  if (SUPPORTS_ONE_ONLY)
    return 1;
  return SUPPORTS_WEAK;
}

/* Set up DECL as a public symbol that can be defined in multiple
   translation units without generating a linker error.  */

void
make_decl_one_only (tree decl)
{
  gcc_assert (TREE_CODE (decl) == VAR_DECL
	      || TREE_CODE (decl) == FUNCTION_DECL);

  TREE_PUBLIC (decl) = 1;

  if (SUPPORTS_ONE_ONLY)
    {
#ifdef MAKE_DECL_ONE_ONLY
      MAKE_DECL_ONE_ONLY (decl);
#endif
      DECL_ONE_ONLY (decl) = 1;
    }
  else if (TREE_CODE (decl) == VAR_DECL
      && (DECL_INITIAL (decl) == 0 || DECL_INITIAL (decl) == error_mark_node))
    DECL_COMMON (decl) = 1;
  else
    {
      gcc_assert (SUPPORTS_WEAK);
      DECL_WEAK (decl) = 1;
    }
}

void
init_varasm_once (void)
{
  section_htab = htab_create_ggc (31, section_entry_hash,
				  section_entry_eq, NULL);
  object_block_htab = htab_create_ggc (31, object_block_entry_hash,
				       object_block_entry_eq, NULL);
  const_desc_htab = htab_create_ggc (1009, const_desc_hash,
				     const_desc_eq, NULL);

  const_alias_set = new_alias_set ();
  shared_constant_pool = create_constant_pool ();

#ifdef TEXT_SECTION_ASM_OP
  text_section = get_unnamed_section (SECTION_CODE, output_section_asm_op,
				      TEXT_SECTION_ASM_OP);
#endif

#ifdef DATA_SECTION_ASM_OP
  data_section = get_unnamed_section (SECTION_WRITE, output_section_asm_op,
				      DATA_SECTION_ASM_OP);
#endif

#ifdef SDATA_SECTION_ASM_OP
  sdata_section = get_unnamed_section (SECTION_WRITE, output_section_asm_op,
				       SDATA_SECTION_ASM_OP);
#endif

#ifdef READONLY_DATA_SECTION_ASM_OP
  readonly_data_section = get_unnamed_section (0, output_section_asm_op,
					       READONLY_DATA_SECTION_ASM_OP);
#endif

#ifdef CTORS_SECTION_ASM_OP
  ctors_section = get_unnamed_section (0, output_section_asm_op,
				       CTORS_SECTION_ASM_OP);
#endif

#ifdef DTORS_SECTION_ASM_OP
  dtors_section = get_unnamed_section (0, output_section_asm_op,
				       DTORS_SECTION_ASM_OP);
#endif

#ifdef BSS_SECTION_ASM_OP
  bss_section = get_unnamed_section (SECTION_WRITE | SECTION_BSS,
				     output_section_asm_op,
				     BSS_SECTION_ASM_OP);
#endif

#ifdef SBSS_SECTION_ASM_OP
  sbss_section = get_unnamed_section (SECTION_WRITE | SECTION_BSS,
				      output_section_asm_op,
				      SBSS_SECTION_ASM_OP);
#endif

  tls_comm_section = get_noswitch_section (SECTION_WRITE | SECTION_BSS
					   | SECTION_COMMON, emit_tls_common);
  lcomm_section = get_noswitch_section (SECTION_WRITE | SECTION_BSS
					| SECTION_COMMON, emit_local);
  comm_section = get_noswitch_section (SECTION_WRITE | SECTION_BSS
				       | SECTION_COMMON, emit_common);

#if defined ASM_OUTPUT_ALIGNED_BSS || defined ASM_OUTPUT_BSS
  bss_noswitch_section = get_noswitch_section (SECTION_WRITE | SECTION_BSS,
					       emit_bss);
#endif

  targetm.asm_out.init_sections ();

  if (readonly_data_section == NULL)
    readonly_data_section = text_section;
}

enum tls_model
decl_default_tls_model (tree decl)
{
  enum tls_model kind;
  bool is_local;

  is_local = targetm.binds_local_p (decl);
  if (!flag_shlib)
    {
      if (is_local)
	kind = TLS_MODEL_LOCAL_EXEC;
      else
	kind = TLS_MODEL_INITIAL_EXEC;
    }

  /* Local dynamic is inefficient when we're not combining the
     parts of the address.  */
  else if (optimize && is_local)
    kind = TLS_MODEL_LOCAL_DYNAMIC;
  else
    kind = TLS_MODEL_GLOBAL_DYNAMIC;
  if (kind < flag_tls_default)
    kind = flag_tls_default;

  return kind;
}

/* Select a set of attributes for section NAME based on the properties
   of DECL and whether or not RELOC indicates that DECL's initializer
   might contain runtime relocations.

   We make the section read-only and executable for a function decl,
   read-only for a const data decl, and writable for a non-const data decl.  */

unsigned int
default_section_type_flags (tree decl, const char *name, int reloc)
{
  unsigned int flags;

  if (decl && TREE_CODE (decl) == FUNCTION_DECL)
    flags = SECTION_CODE;
  else if (decl && decl_readonly_section (decl, reloc))
    flags = 0;
  else if (current_function_decl
	   && cfun
	   && cfun->unlikely_text_section_name
	   && strcmp (name, cfun->unlikely_text_section_name) == 0)
    flags = SECTION_CODE;
  else if (!decl
	   && (!current_function_decl || !cfun)
	   && strcmp (name, UNLIKELY_EXECUTED_TEXT_SECTION_NAME) == 0)
    flags = SECTION_CODE;
  else
    flags = SECTION_WRITE;

  if (decl && DECL_ONE_ONLY (decl))
    flags |= SECTION_LINKONCE;

  if (decl && TREE_CODE (decl) == VAR_DECL && DECL_THREAD_LOCAL_P (decl))
    flags |= SECTION_TLS | SECTION_WRITE;

  if (strcmp (name, ".bss") == 0
      || strncmp (name, ".bss.", 5) == 0
      || strncmp (name, ".gnu.linkonce.b.", 16) == 0
      || strcmp (name, ".sbss") == 0
      || strncmp (name, ".sbss.", 6) == 0
      || strncmp (name, ".gnu.linkonce.sb.", 17) == 0)
    flags |= SECTION_BSS;

  if (strcmp (name, ".tdata") == 0
      || strncmp (name, ".tdata.", 7) == 0
      || strncmp (name, ".gnu.linkonce.td.", 17) == 0)
    flags |= SECTION_TLS;

  if (strcmp (name, ".tbss") == 0
      || strncmp (name, ".tbss.", 6) == 0
      || strncmp (name, ".gnu.linkonce.tb.", 17) == 0)
    flags |= SECTION_TLS | SECTION_BSS;

  /* These three sections have special ELF types.  They are neither
     SHT_PROGBITS nor SHT_NOBITS, so when changing sections we don't
     want to print a section type (@progbits or @nobits).  If someone
     is silly enough to emit code or TLS variables to one of these
     sections, then don't handle them specially.  */
  if (!(flags & (SECTION_CODE | SECTION_BSS | SECTION_TLS))
      && (strcmp (name, ".init_array") == 0
	  || strcmp (name, ".fini_array") == 0
	  || strcmp (name, ".preinit_array") == 0))
    flags |= SECTION_NOTYPE;

  return flags;
}

/* Return true if the target supports some form of global BSS,
   either through bss_noswitch_section, or by selecting a BSS
   section in TARGET_ASM_SELECT_SECTION.  */

bool
have_global_bss_p (void)
{
  return bss_noswitch_section || targetm.have_switchable_bss_sections;
}

/* Output assembly to switch to section NAME with attribute FLAGS.
   Four variants for common object file formats.  */

void
default_no_named_section (const char *name ATTRIBUTE_UNUSED,
			  unsigned int flags ATTRIBUTE_UNUSED,
			  tree decl ATTRIBUTE_UNUSED)
{
  /* Some object formats don't support named sections at all.  The
     front-end should already have flagged this as an error.  */
  gcc_unreachable ();
}

void
default_elf_asm_named_section (const char *name, unsigned int flags,
			       tree decl ATTRIBUTE_UNUSED)
{
  char flagchars[10], *f = flagchars;

  /* If we have already declared this section, we can use an
     abbreviated form to switch back to it -- unless this section is
     part of a COMDAT groups, in which case GAS requires the full
     declaration every time.  */
  if (!(HAVE_COMDAT_GROUP && (flags & SECTION_LINKONCE))
      && (flags & SECTION_DECLARED))
    {
      fprintf (asm_out_file, "\t.section\t%s\n", name);
      return;
    }

  if (!(flags & SECTION_DEBUG))
    *f++ = 'a';
  if (flags & SECTION_WRITE)
    *f++ = 'w';
  if (flags & SECTION_CODE)
    *f++ = 'x';
  if (flags & SECTION_SMALL)
    *f++ = 's';
  if (flags & SECTION_MERGE)
    *f++ = 'M';
  if (flags & SECTION_STRINGS)
    *f++ = 'S';
  if (flags & SECTION_TLS)
    *f++ = 'T';
  if (HAVE_COMDAT_GROUP && (flags & SECTION_LINKONCE))
    *f++ = 'G';
  *f = '\0';

  fprintf (asm_out_file, "\t.section\t%s,\"%s\"", name, flagchars);

  if (!(flags & SECTION_NOTYPE))
    {
      const char *type;
      const char *format;

      if (flags & SECTION_BSS)
	type = "nobits";
      else
	type = "progbits";

      format = ",@%s";
#ifdef ASM_COMMENT_START
      /* On platforms that use "@" as the assembly comment character,
	 use "%" instead.  */
      if (strcmp (ASM_COMMENT_START, "@") == 0)
	format = ",%%%s";
#endif
      fprintf (asm_out_file, format, type);

      if (flags & SECTION_ENTSIZE)
	fprintf (asm_out_file, ",%d", flags & SECTION_ENTSIZE);
      if (HAVE_COMDAT_GROUP && (flags & SECTION_LINKONCE))
	fprintf (asm_out_file, ",%s,comdat",
		 lang_hooks.decls.comdat_group (decl));
    }

  putc ('\n', asm_out_file);
}

void
default_coff_asm_named_section (const char *name, unsigned int flags,
				tree decl ATTRIBUTE_UNUSED)
{
  char flagchars[8], *f = flagchars;

  if (flags & SECTION_WRITE)
    *f++ = 'w';
  if (flags & SECTION_CODE)
    *f++ = 'x';
  *f = '\0';

  fprintf (asm_out_file, "\t.section\t%s,\"%s\"\n", name, flagchars);
}

void
default_pe_asm_named_section (const char *name, unsigned int flags,
			      tree decl)
{
  default_coff_asm_named_section (name, flags, decl);

  if (flags & SECTION_LINKONCE)
    {
      /* Functions may have been compiled at various levels of
         optimization so we can't use `same_size' here.
         Instead, have the linker pick one.  */
      fprintf (asm_out_file, "\t.linkonce %s\n",
	       (flags & SECTION_CODE ? "discard" : "same_size"));
    }
}

/* The lame default section selector.  */

section *
default_select_section (tree decl, int reloc,
			unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  if (DECL_P (decl))
    {
      if (decl_readonly_section (decl, reloc))
	return readonly_data_section;
    }
  else if (TREE_CODE (decl) == CONSTRUCTOR)
    {
      if (! ((flag_pic && reloc)
	     || !TREE_READONLY (decl)
	     || TREE_SIDE_EFFECTS (decl)
	     || !TREE_CONSTANT (decl)))
	return readonly_data_section;
    }
  else if (TREE_CODE (decl) == STRING_CST)
    return readonly_data_section;
  else if (! (flag_pic && reloc))
    return readonly_data_section;

  return data_section;
}

enum section_category
categorize_decl_for_section (tree decl, int reloc)
{
  enum section_category ret;

  if (TREE_CODE (decl) == FUNCTION_DECL)
    return SECCAT_TEXT;
  else if (TREE_CODE (decl) == STRING_CST)
    {
      if (flag_mudflap) /* or !flag_merge_constants */
        return SECCAT_RODATA;
      else
	return SECCAT_RODATA_MERGE_STR;
    }
  else if (TREE_CODE (decl) == VAR_DECL)
    {
      if (bss_initializer_p (decl))
	ret = SECCAT_BSS;
      else if (! TREE_READONLY (decl)
	       || TREE_SIDE_EFFECTS (decl)
	       || ! TREE_CONSTANT (DECL_INITIAL (decl)))
	{
	  /* Here the reloc_rw_mask is not testing whether the section should
	     be read-only or not, but whether the dynamic link will have to
	     do something.  If so, we wish to segregate the data in order to
	     minimize cache misses inside the dynamic linker.  */
	  if (reloc & targetm.asm_out.reloc_rw_mask ())
	    ret = reloc == 1 ? SECCAT_DATA_REL_LOCAL : SECCAT_DATA_REL;
	  else
	    ret = SECCAT_DATA;
	}
      else if (reloc & targetm.asm_out.reloc_rw_mask ())
	ret = reloc == 1 ? SECCAT_DATA_REL_RO_LOCAL : SECCAT_DATA_REL_RO;
      else if (reloc || flag_merge_constants < 2)
	/* C and C++ don't allow different variables to share the same
	   location.  -fmerge-all-constants allows even that (at the
	   expense of not conforming).  */
	ret = SECCAT_RODATA;
      else if (TREE_CODE (DECL_INITIAL (decl)) == STRING_CST)
	ret = SECCAT_RODATA_MERGE_STR_INIT;
      else
	ret = SECCAT_RODATA_MERGE_CONST;
    }
  else if (TREE_CODE (decl) == CONSTRUCTOR)
    {
      if ((reloc & targetm.asm_out.reloc_rw_mask ())
	  || TREE_SIDE_EFFECTS (decl)
	  || ! TREE_CONSTANT (decl))
	ret = SECCAT_DATA;
      else
	ret = SECCAT_RODATA;
    }
  else
    ret = SECCAT_RODATA;

  /* There are no read-only thread-local sections.  */
  if (TREE_CODE (decl) == VAR_DECL && DECL_THREAD_LOCAL_P (decl))
    {
      /* Note that this would be *just* SECCAT_BSS, except that there's
	 no concept of a read-only thread-local-data section.  */
      if (ret == SECCAT_BSS
	  || (flag_zero_initialized_in_bss
	      && initializer_zerop (DECL_INITIAL (decl))))
	ret = SECCAT_TBSS;
      else
	ret = SECCAT_TDATA;
    }

  /* If the target uses small data sections, select it.  */
  else if (targetm.in_small_data_p (decl))
    {
      if (ret == SECCAT_BSS)
	ret = SECCAT_SBSS;
      else if (targetm.have_srodata_section && ret == SECCAT_RODATA)
	ret = SECCAT_SRODATA;
      else
	ret = SECCAT_SDATA;
    }

  return ret;
}

bool
decl_readonly_section (tree decl, int reloc)
{
  switch (categorize_decl_for_section (decl, reloc))
    {
    case SECCAT_RODATA:
    case SECCAT_RODATA_MERGE_STR:
    case SECCAT_RODATA_MERGE_STR_INIT:
    case SECCAT_RODATA_MERGE_CONST:
    case SECCAT_SRODATA:
      return true;
      break;
    default:
      return false;
      break;
    }
}

/* Select a section based on the above categorization.  */

section *
default_elf_select_section (tree decl, int reloc,
			    unsigned HOST_WIDE_INT align)
{
  const char *sname;
  switch (categorize_decl_for_section (decl, reloc))
    {
    case SECCAT_TEXT:
      /* We're not supposed to be called on FUNCTION_DECLs.  */
      gcc_unreachable ();
    case SECCAT_RODATA:
      return readonly_data_section;
    case SECCAT_RODATA_MERGE_STR:
      return mergeable_string_section (decl, align, 0);
    case SECCAT_RODATA_MERGE_STR_INIT:
      return mergeable_string_section (DECL_INITIAL (decl), align, 0);
    case SECCAT_RODATA_MERGE_CONST:
      return mergeable_constant_section (DECL_MODE (decl), align, 0);
    case SECCAT_SRODATA:
      sname = ".sdata2";
      break;
    case SECCAT_DATA:
      return data_section;
    case SECCAT_DATA_REL:
      sname = ".data.rel";
      break;
    case SECCAT_DATA_REL_LOCAL:
      sname = ".data.rel.local";
      break;
    case SECCAT_DATA_REL_RO:
      sname = ".data.rel.ro";
      break;
    case SECCAT_DATA_REL_RO_LOCAL:
      sname = ".data.rel.ro.local";
      break;
    case SECCAT_SDATA:
      sname = ".sdata";
      break;
    case SECCAT_TDATA:
      sname = ".tdata";
      break;
    case SECCAT_BSS:
      if (bss_section)
	return bss_section;
      sname = ".bss";
      break;
    case SECCAT_SBSS:
      sname = ".sbss";
      break;
    case SECCAT_TBSS:
      sname = ".tbss";
      break;
    default:
      gcc_unreachable ();
    }

  if (!DECL_P (decl))
    decl = NULL_TREE;
  return get_named_section (decl, sname, reloc);
}

/* Construct a unique section name based on the decl name and the
   categorization performed above.  */

void
default_unique_section (tree decl, int reloc)
{
  /* We only need to use .gnu.linkonce if we don't have COMDAT groups.  */
  bool one_only = DECL_ONE_ONLY (decl) && !HAVE_COMDAT_GROUP;
  const char *prefix, *name;
  size_t nlen, plen;
  char *string;

  switch (categorize_decl_for_section (decl, reloc))
    {
    case SECCAT_TEXT:
      prefix = one_only ? ".gnu.linkonce.t." : ".text.";
      break;
    case SECCAT_RODATA:
    case SECCAT_RODATA_MERGE_STR:
    case SECCAT_RODATA_MERGE_STR_INIT:
    case SECCAT_RODATA_MERGE_CONST:
      prefix = one_only ? ".gnu.linkonce.r." : ".rodata.";
      break;
    case SECCAT_SRODATA:
      prefix = one_only ? ".gnu.linkonce.s2." : ".sdata2.";
      break;
    case SECCAT_DATA:
      prefix = one_only ? ".gnu.linkonce.d." : ".data.";
      break;
    case SECCAT_DATA_REL:
      prefix = one_only ? ".gnu.linkonce.d.rel." : ".data.rel.";
      break;
    case SECCAT_DATA_REL_LOCAL:
      prefix = one_only ? ".gnu.linkonce.d.rel.local." : ".data.rel.local.";
      break;
    case SECCAT_DATA_REL_RO:
      prefix = one_only ? ".gnu.linkonce.d.rel.ro." : ".data.rel.ro.";
      break;
    case SECCAT_DATA_REL_RO_LOCAL:
      prefix = one_only ? ".gnu.linkonce.d.rel.ro.local."
	       : ".data.rel.ro.local.";
      break;
    case SECCAT_SDATA:
      prefix = one_only ? ".gnu.linkonce.s." : ".sdata.";
      break;
    case SECCAT_BSS:
      prefix = one_only ? ".gnu.linkonce.b." : ".bss.";
      break;
    case SECCAT_SBSS:
      prefix = one_only ? ".gnu.linkonce.sb." : ".sbss.";
      break;
    case SECCAT_TDATA:
      prefix = one_only ? ".gnu.linkonce.td." : ".tdata.";
      break;
    case SECCAT_TBSS:
      prefix = one_only ? ".gnu.linkonce.tb." : ".tbss.";
      break;
    default:
      gcc_unreachable ();
    }
  plen = strlen (prefix);

  name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
  name = targetm.strip_name_encoding (name);
  nlen = strlen (name);

  string = alloca (nlen + plen + 1);
  memcpy (string, prefix, plen);
  memcpy (string + plen, name, nlen + 1);

  DECL_SECTION_NAME (decl) = build_string (nlen + plen, string);
}

/* Like compute_reloc_for_constant, except for an RTX.  The return value
   is a mask for which bit 1 indicates a global relocation, and bit 0
   indicates a local relocation.  */

static int
compute_reloc_for_rtx_1 (rtx *xp, void *data)
{
  int *preloc = data;
  rtx x = *xp;

  switch (GET_CODE (x))
    {
    case SYMBOL_REF:
      *preloc |= SYMBOL_REF_LOCAL_P (x) ? 1 : 2;
      break;
    case LABEL_REF:
      *preloc |= 1;
      break;
    default:
      break;
    }

  return 0;
}

static int
compute_reloc_for_rtx (rtx x)
{
  int reloc;

  switch (GET_CODE (x))
    {
    case CONST:
    case SYMBOL_REF:
    case LABEL_REF:
      reloc = 0;
      for_each_rtx (&x, compute_reloc_for_rtx_1, &reloc);
      return reloc;

    default:
      return 0;
    }
}

section *
default_select_rtx_section (enum machine_mode mode ATTRIBUTE_UNUSED,
			    rtx x,
			    unsigned HOST_WIDE_INT align ATTRIBUTE_UNUSED)
{
  if (compute_reloc_for_rtx (x) & targetm.asm_out.reloc_rw_mask ())
    return data_section;
  else
    return readonly_data_section;
}

section *
default_elf_select_rtx_section (enum machine_mode mode, rtx x,
				unsigned HOST_WIDE_INT align)
{
  int reloc = compute_reloc_for_rtx (x);

  /* ??? Handle small data here somehow.  */

  if (reloc & targetm.asm_out.reloc_rw_mask ())
    {
      if (reloc == 1)
	return get_named_section (NULL, ".data.rel.ro.local", 1);
      else
	return get_named_section (NULL, ".data.rel.ro", 3);
    }

  return mergeable_constant_section (mode, align, 0);
}

/* Set the generally applicable flags on the SYMBOL_REF for EXP.  */

void
default_encode_section_info (tree decl, rtx rtl, int first ATTRIBUTE_UNUSED)
{
  rtx symbol;
  int flags;

  /* Careful not to prod global register variables.  */
  if (!MEM_P (rtl))
    return;
  symbol = XEXP (rtl, 0);
  if (GET_CODE (symbol) != SYMBOL_REF)
    return;

  flags = SYMBOL_REF_FLAGS (symbol) & SYMBOL_FLAG_HAS_BLOCK_INFO;
  if (TREE_CODE (decl) == FUNCTION_DECL)
    flags |= SYMBOL_FLAG_FUNCTION;
  if (targetm.binds_local_p (decl))
    flags |= SYMBOL_FLAG_LOCAL;
  if (TREE_CODE (decl) == VAR_DECL && DECL_THREAD_LOCAL_P (decl))
    flags |= DECL_TLS_MODEL (decl) << SYMBOL_FLAG_TLS_SHIFT;
  else if (targetm.in_small_data_p (decl))
    flags |= SYMBOL_FLAG_SMALL;
  /* ??? Why is DECL_EXTERNAL ever set for non-PUBLIC names?  Without
     being PUBLIC, the thing *must* be defined in this translation unit.
     Prevent this buglet from being propagated into rtl code as well.  */
  if (DECL_P (decl) && DECL_EXTERNAL (decl) && TREE_PUBLIC (decl))
    flags |= SYMBOL_FLAG_EXTERNAL;

  SYMBOL_REF_FLAGS (symbol) = flags;
}

/* By default, we do nothing for encode_section_info, so we need not
   do anything but discard the '*' marker.  */

const char *
default_strip_name_encoding (const char *str)
{
  return str + (*str == '*');
}

#ifdef ASM_OUTPUT_DEF
/* The default implementation of TARGET_ASM_OUTPUT_ANCHOR.  Define the
   anchor relative to ".", the current section position.  */

void
default_asm_output_anchor (rtx symbol)
{
  char buffer[100];

  sprintf (buffer, ". + " HOST_WIDE_INT_PRINT_DEC,
	   SYMBOL_REF_BLOCK_OFFSET (symbol));
  ASM_OUTPUT_DEF (asm_out_file, XSTR (symbol, 0), buffer);
}
#endif

/* The default implementation of TARGET_USE_ANCHORS_FOR_SYMBOL_P.  */

bool
default_use_anchors_for_symbol_p (rtx symbol)
{
  section *sect;
  tree decl;

  /* Don't use anchors for mergeable sections.  The linker might move
     the objects around.  */
  sect = SYMBOL_REF_BLOCK (symbol)->sect;
  if (sect->common.flags & SECTION_MERGE)
    return false;

  /* Don't use anchors for small data sections.  The small data register
     acts as an anchor for such sections.  */
  if (sect->common.flags & SECTION_SMALL)
    return false;

  decl = SYMBOL_REF_DECL (symbol);
  if (decl && DECL_P (decl))
    {
      /* Don't use section anchors for decls that might be defined by
	 other modules.  */
      if (!targetm.binds_local_p (decl))
	return false;

      /* Don't use section anchors for decls that will be placed in a
	 small data section.  */
      /* ??? Ideally, this check would be redundant with the SECTION_SMALL
	 one above.  The problem is that we only use SECTION_SMALL for
	 sections that should be marked as small in the section directive.  */
      if (targetm.in_small_data_p (decl))
	return false;
    }
  return true;
}

/* Assume ELF-ish defaults, since that's pretty much the most liberal
   wrt cross-module name binding.  */

bool
default_binds_local_p (tree exp)
{
  return default_binds_local_p_1 (exp, flag_shlib);
}

bool
default_binds_local_p_1 (tree exp, int shlib)
{
  bool local_p;

  /* A non-decl is an entry in the constant pool.  */
  if (!DECL_P (exp))
    local_p = true;
  /* Weakrefs may not bind locally, even though the weakref itself is
     always static and therefore local.  */
  else if (lookup_attribute ("weakref", DECL_ATTRIBUTES (exp)))
    local_p = false;
  /* Static variables are always local.  */
  else if (! TREE_PUBLIC (exp))
    local_p = true;
  /* A variable is local if the user has said explicitly that it will
     be.  */
  else if (DECL_VISIBILITY_SPECIFIED (exp)
	   && DECL_VISIBILITY (exp) != VISIBILITY_DEFAULT)
    local_p = true;
  /* Variables defined outside this object might not be local.  */
  else if (DECL_EXTERNAL (exp))
    local_p = false;
  /* If defined in this object and visibility is not default, must be
     local.  */
  else if (DECL_VISIBILITY (exp) != VISIBILITY_DEFAULT)
    local_p = true;
  /* Default visibility weak data can be overridden by a strong symbol
     in another module and so are not local.  */
  else if (DECL_WEAK (exp))
    local_p = false;
  /* If PIC, then assume that any global name can be overridden by
     symbols resolved from other modules.  */
  else if (shlib)
    local_p = false;
  /* Uninitialized COMMON variable may be unified with symbols
     resolved from other modules.  */
  else if (DECL_COMMON (exp)
	   && (DECL_INITIAL (exp) == NULL
	       || DECL_INITIAL (exp) == error_mark_node))
    local_p = false;
  /* Otherwise we're left with initialized (or non-common) global data
     which is of necessity defined locally.  */
  else
    local_p = true;

  return local_p;
}

/* Determine whether or not a pointer mode is valid. Assume defaults
   of ptr_mode or Pmode - can be overridden.  */
bool
default_valid_pointer_mode (enum machine_mode mode)
{
  return (mode == ptr_mode || mode == Pmode);
}

/* Default function to output code that will globalize a label.  A
   target must define GLOBAL_ASM_OP or provide its own function to
   globalize a label.  */
#ifdef GLOBAL_ASM_OP
void
default_globalize_label (FILE * stream, const char *name)
{
  fputs (GLOBAL_ASM_OP, stream);
  assemble_name (stream, name);
  putc ('\n', stream);
}
#endif /* GLOBAL_ASM_OP */

/* Default function to output a label for unwind information.  The
   default is to do nothing.  A target that needs nonlocal labels for
   unwind information must provide its own function to do this.  */
void
default_emit_unwind_label (FILE * stream ATTRIBUTE_UNUSED,
			   tree decl ATTRIBUTE_UNUSED,
			   int for_eh ATTRIBUTE_UNUSED,
			   int empty ATTRIBUTE_UNUSED)
{
}

/* Default function to output a label to divide up the exception table.
   The default is to do nothing.  A target that needs/wants to divide
   up the table must provide it's own function to do this.  */
void
default_emit_except_table_label (FILE * stream ATTRIBUTE_UNUSED)
{
}

/* This is how to output an internal numbered label where PREFIX is
   the class of label and LABELNO is the number within the class.  */

void
default_internal_label (FILE *stream, const char *prefix,
			unsigned long labelno)
{
  char *const buf = alloca (40 + strlen (prefix));
  ASM_GENERATE_INTERNAL_LABEL (buf, prefix, labelno);
  ASM_OUTPUT_INTERNAL_LABEL (stream, buf);
}

/* This is the default behavior at the beginning of a file.  It's
   controlled by two other target-hook toggles.  */
void
default_file_start (void)
{
  if (targetm.file_start_app_off && !flag_verbose_asm)
    fputs (ASM_APP_OFF, asm_out_file);

  if (targetm.file_start_file_directive)
    output_file_directive (asm_out_file, main_input_filename);
}

/* This is a generic routine suitable for use as TARGET_ASM_FILE_END
   which emits a special section directive used to indicate whether or
   not this object file needs an executable stack.  This is primarily
   a GNU extension to ELF but could be used on other targets.  */

int trampolines_created;

void
file_end_indicate_exec_stack (void)
{
  unsigned int flags = SECTION_DEBUG;
  if (trampolines_created)
    flags |= SECTION_CODE;

  switch_to_section (get_section (".note.GNU-stack", flags, NULL));
}

/* Output DIRECTIVE (a C string) followed by a newline.  This is used as
   a get_unnamed_section callback.  */

void
output_section_asm_op (const void *directive)
{
  fprintf (asm_out_file, "%s\n", (const char *) directive);
}

/* Emit assembly code to switch to section NEW_SECTION.  Do nothing if
   the current section is NEW_SECTION.  */

void
switch_to_section (section *new_section)
{
  if (in_section == new_section)
    return;

  if (new_section->common.flags & SECTION_FORGET)
    in_section = NULL;
  else
    in_section = new_section;

  switch (SECTION_STYLE (new_section))
    {
    case SECTION_NAMED:
      if (cfun
	  && !cfun->unlikely_text_section_name
	  && strcmp (new_section->named.name,
		     UNLIKELY_EXECUTED_TEXT_SECTION_NAME) == 0)
	cfun->unlikely_text_section_name = UNLIKELY_EXECUTED_TEXT_SECTION_NAME;

      targetm.asm_out.named_section (new_section->named.name,
				     new_section->named.common.flags,
				     new_section->named.decl);
      break;

    case SECTION_UNNAMED:
      new_section->unnamed.callback (new_section->unnamed.data);
      break;

    case SECTION_NOSWITCH:
      gcc_unreachable ();
      break;
    }

  new_section->common.flags |= SECTION_DECLARED;
}

/* If block symbol SYMBOL has not yet been assigned an offset, place
   it at the end of its block.  */

void
place_block_symbol (rtx symbol)
{
  unsigned HOST_WIDE_INT size, mask, offset;
  struct constant_descriptor_rtx *desc;
  unsigned int alignment;
  struct object_block *block;
  tree decl;

  gcc_assert (SYMBOL_REF_BLOCK (symbol));
  if (SYMBOL_REF_BLOCK_OFFSET (symbol) >= 0)
    return;

  /* Work out the symbol's size and alignment.  */
  if (CONSTANT_POOL_ADDRESS_P (symbol))
    {
      desc = SYMBOL_REF_CONSTANT (symbol);
      alignment = desc->align;
      size = GET_MODE_SIZE (desc->mode);
    }
  else if (TREE_CONSTANT_POOL_ADDRESS_P (symbol))
    {
      decl = SYMBOL_REF_DECL (symbol);
      alignment = get_constant_alignment (decl);
      size = get_constant_size (decl);
    }
  else
    {
      decl = SYMBOL_REF_DECL (symbol);
      alignment = DECL_ALIGN (decl);
      size = tree_low_cst (DECL_SIZE_UNIT (decl), 1);
    }

  /* Calculate the object's offset from the start of the block.  */
  block = SYMBOL_REF_BLOCK (symbol);
  mask = alignment / BITS_PER_UNIT - 1;
  offset = (block->size + mask) & ~mask;
  SYMBOL_REF_BLOCK_OFFSET (symbol) = offset;

  /* Record the block's new alignment and size.  */
  block->alignment = MAX (block->alignment, alignment);
  block->size = offset + size;

  VEC_safe_push (rtx, gc, block->objects, symbol);
}

/* Return the anchor that should be used to address byte offset OFFSET
   from the first object in BLOCK.  MODEL is the TLS model used
   to access it.  */

rtx
get_section_anchor (struct object_block *block, HOST_WIDE_INT offset,
		    enum tls_model model)
{
  char label[100];
  unsigned int begin, middle, end;
  unsigned HOST_WIDE_INT min_offset, max_offset, range, bias, delta;
  rtx anchor;

  /* Work out the anchor's offset.  Use an offset of 0 for the first
     anchor so that we don't pessimize the case where we take the address
     of a variable at the beginning of the block.  This is particularly
     useful when a block has only one variable assigned to it.

     We try to place anchors RANGE bytes apart, so there can then be
     anchors at +/-RANGE, +/-2 * RANGE, and so on, up to the limits of
     a ptr_mode offset.  With some target settings, the lowest such
     anchor might be out of range for the lowest ptr_mode offset;
     likewise the highest anchor for the highest offset.  Use anchors
     at the extreme ends of the ptr_mode range in such cases.

     All arithmetic uses unsigned integers in order to avoid
     signed overflow.  */
  max_offset = (unsigned HOST_WIDE_INT) targetm.max_anchor_offset;
  min_offset = (unsigned HOST_WIDE_INT) targetm.min_anchor_offset;
  range = max_offset - min_offset + 1;
  if (range == 0)
    offset = 0;
  else
    {
      bias = 1 << (GET_MODE_BITSIZE (ptr_mode) - 1);
      if (offset < 0)
	{
	  delta = -(unsigned HOST_WIDE_INT) offset + max_offset;
	  delta -= delta % range;
	  if (delta > bias)
	    delta = bias;
	  offset = (HOST_WIDE_INT) (-delta);
	}
      else
	{
	  delta = (unsigned HOST_WIDE_INT) offset - min_offset;
	  delta -= delta % range;
	  if (delta > bias - 1)
	    delta = bias - 1;
	  offset = (HOST_WIDE_INT) delta;
	}
    }

  /* Do a binary search to see if there's already an anchor we can use.
     Set BEGIN to the new anchor's index if not.  */
  begin = 0;
  end = VEC_length (rtx, block->anchors);
  while (begin != end)
    {
      middle = (end + begin) / 2;
      anchor = VEC_index (rtx, block->anchors, middle);
      if (SYMBOL_REF_BLOCK_OFFSET (anchor) > offset)
	end = middle;
      else if (SYMBOL_REF_BLOCK_OFFSET (anchor) < offset)
	begin = middle + 1;
      else if (SYMBOL_REF_TLS_MODEL (anchor) > model)
	end = middle;
      else if (SYMBOL_REF_TLS_MODEL (anchor) < model)
	begin = middle + 1;
      else
	return anchor;
    }

  /* Create a new anchor with a unique label.  */
  ASM_GENERATE_INTERNAL_LABEL (label, "LANCHOR", anchor_labelno++);
  anchor = create_block_symbol (ggc_strdup (label), block, offset);
  SYMBOL_REF_FLAGS (anchor) |= SYMBOL_FLAG_LOCAL | SYMBOL_FLAG_ANCHOR;
  SYMBOL_REF_FLAGS (anchor) |= model << SYMBOL_FLAG_TLS_SHIFT;

  /* Insert it at index BEGIN.  */
  VEC_safe_insert (rtx, gc, block->anchors, begin, anchor);
  return anchor;
}

/* Output the objects in BLOCK.  */

static void
output_object_block (struct object_block *block)
{
  struct constant_descriptor_rtx *desc;
  unsigned int i;
  HOST_WIDE_INT offset;
  tree decl;
  rtx symbol;

  if (block->objects == NULL)
    return;

  /* Switch to the section and make sure that the first byte is
     suitably aligned.  */
  switch_to_section (block->sect);
  assemble_align (block->alignment);

  /* Define the values of all anchors relative to the current section
     position.  */
  for (i = 0; VEC_iterate (rtx, block->anchors, i, symbol); i++)
    targetm.asm_out.output_anchor (symbol);

  /* Output the objects themselves.  */
  offset = 0;
  for (i = 0; VEC_iterate (rtx, block->objects, i, symbol); i++)
    {
      /* Move to the object's offset, padding with zeros if necessary.  */
      assemble_zeros (SYMBOL_REF_BLOCK_OFFSET (symbol) - offset);
      offset = SYMBOL_REF_BLOCK_OFFSET (symbol);
      if (CONSTANT_POOL_ADDRESS_P (symbol))
	{
	  desc = SYMBOL_REF_CONSTANT (symbol);
	  output_constant_pool_1 (desc, 1);
	  offset += GET_MODE_SIZE (desc->mode);
	}
      else if (TREE_CONSTANT_POOL_ADDRESS_P (symbol))
	{
	  decl = SYMBOL_REF_DECL (symbol);
	  assemble_constant_contents (decl, XSTR (symbol, 0),
				      get_constant_alignment (decl));
	  offset += get_constant_size (decl);
	}
      else
	{
	  decl = SYMBOL_REF_DECL (symbol);
	  assemble_variable_contents (decl, XSTR (symbol, 0), false);
	  offset += tree_low_cst (DECL_SIZE_UNIT (decl), 1);
	}
    }
}

/* A htab_traverse callback used to call output_object_block for
   each member of object_block_htab.  */

static int
output_object_block_htab (void **slot, void *data ATTRIBUTE_UNUSED)
{
  output_object_block ((struct object_block *) (*slot));
  return 1;
}

/* Output the definitions of all object_blocks.  */

void
output_object_blocks (void)
{
  htab_traverse (object_block_htab, output_object_block_htab, NULL);
}

/* Emit text to declare externally defined symbols. It is needed to
   properly support non-default visibility.  */
void
default_elf_asm_output_external (FILE *file ATTRIBUTE_UNUSED,
				 tree decl,
				 const char *name ATTRIBUTE_UNUSED)
{
  /* We output the name if and only if TREE_SYMBOL_REFERENCED is
     set in order to avoid putting out names that are never really
     used. */
  if (TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl))
      && targetm.binds_local_p (decl))
    maybe_assemble_visibility (decl);
}

#include "gt-varasm.h"
