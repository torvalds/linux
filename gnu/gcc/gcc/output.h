/* Declarations for insn-output.c.  These functions are defined in recog.c,
   final.c, and varasm.c.
   Copyright (C) 1987, 1991, 1994, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_OUTPUT_H
#define GCC_OUTPUT_H

/* Initialize data in final at the beginning of a compilation.  */
extern void init_final (const char *);

/* Enable APP processing of subsequent output.
   Used before the output from an `asm' statement.  */
extern void app_enable (void);

/* Disable APP processing of subsequent output.
   Called from varasm.c before most kinds of output.  */
extern void app_disable (void);

/* Return the number of slots filled in the current
   delayed branch sequence (we don't count the insn needing the
   delay slot).   Zero if not in a delayed branch sequence.  */
extern int dbr_sequence_length (void);

/* Indicate that branch shortening hasn't yet been done.  */
extern void init_insn_lengths (void);

/* Obtain the current length of an insn.  If branch shortening has been done,
   get its actual length.  Otherwise, get its maximum length.  */
extern int get_attr_length (rtx);

/* Obtain the current length of an insn.  If branch shortening has been done,
   get its actual length.  Otherwise, get its minimum length.  */
extern int get_attr_min_length (rtx);

/* Make a pass over all insns and compute their actual lengths by shortening
   any branches of variable length if possible.  */
extern void shorten_branches (rtx);

/* Output assembler code for the start of a function,
   and initialize some of the variables in this file
   for the new function.  The label for the function and associated
   assembler pseudo-ops have already been output in
   `assemble_start_function'.  */
extern void final_start_function (rtx, FILE *, int);

/* Output assembler code for the end of a function.
   For clarity, args are same as those of `final_start_function'
   even though not all of them are needed.  */
extern void final_end_function (void);

/* Output assembler code for some insns: all or part of a function.  */
extern void final (rtx, FILE *, int);

/* The final scan for one insn, INSN.  Args are same as in `final', except
   that INSN is the insn being scanned.  Value returned is the next insn to
   be scanned.  */
extern rtx final_scan_insn (rtx, FILE *, int, int, int *);

/* Replace a SUBREG with a REG or a MEM, based on the thing it is a
   subreg of.  */
extern rtx alter_subreg (rtx *);

/* Report inconsistency between the assembler template and the operands.
   In an `asm', it's the user's fault; otherwise, the compiler's fault.  */
extern void output_operand_lossage (const char *, ...) ATTRIBUTE_PRINTF_1;

/* Output a string of assembler code, substituting insn operands.
   Defined in final.c.  */
extern void output_asm_insn (const char *, rtx *);

/* Compute a worst-case reference address of a branch so that it
   can be safely used in the presence of aligned labels.
   Defined in final.c.  */
extern int insn_current_reference_address (rtx);

/* Find the alignment associated with a CODE_LABEL.
   Defined in final.c.  */
extern int label_to_alignment (rtx);

/* Output a LABEL_REF, or a bare CODE_LABEL, as an assembler symbol.  */
extern void output_asm_label (rtx);

/* Print a memory reference operand for address X
   using machine-dependent assembler syntax.  */
extern void output_address (rtx);

/* Print an integer constant expression in assembler syntax.
   Addition and subtraction are the only arithmetic
   that may appear in these expressions.  */
extern void output_addr_const (FILE *, rtx);

/* Output a string of assembler code, substituting numbers, strings
   and fixed syntactic prefixes.  */
#if GCC_VERSION >= 3004
#define ATTRIBUTE_ASM_FPRINTF(m, n) __attribute__ ((__format__ (__asm_fprintf__, m, n))) ATTRIBUTE_NONNULL(m)
/* This is a magic identifier which allows GCC to figure out the type
   of HOST_WIDE_INT for %wd specifier checks.  You must issue this
   typedef before using the __asm_fprintf__ format attribute.  */
typedef HOST_WIDE_INT __gcc_host_wide_int__;
#else
#define ATTRIBUTE_ASM_FPRINTF(m, n) ATTRIBUTE_NONNULL(m)
#endif

extern void asm_fprintf (FILE *file, const char *p, ...)
     ATTRIBUTE_ASM_FPRINTF(2, 3);

/* Split up a CONST_DOUBLE or integer constant rtx into two rtx's for single
   words.  */
extern void split_double (rtx, rtx *, rtx *);

/* Return nonzero if this function has no function calls.  */
extern int leaf_function_p (void);

/* Return 1 if branch is a forward branch.
   Uses insn_shuid array, so it works only in the final pass.  May be used by
   output templates to add branch prediction hints, for example.  */
extern int final_forward_branch_p (rtx);

/* Return 1 if this function uses only the registers that can be
   safely renumbered.  */
extern int only_leaf_regs_used (void);

/* Scan IN_RTX and its subexpressions, and renumber all regs into those
   available in leaf functions.  */
extern void leaf_renumber_regs_insn (rtx);

/* Locate the proper template for the given insn-code.  */
extern const char *get_insn_template (int, rtx);

/* Functions in flow.c */
extern int regno_clobbered_at_setjmp (int);

/* Functions in varasm.c.  */

/* Declare DECL to be a weak symbol.  */
extern void declare_weak (tree);
/* Merge weak status.  */
extern void merge_weak (tree, tree);

/* Emit any pending weak declarations.  */
extern void weak_finish (void);

/* Decode an `asm' spec for a declaration as a register name.
   Return the register number, or -1 if nothing specified,
   or -2 if the ASMSPEC is not `cc' or `memory' and is not recognized,
   or -3 if ASMSPEC is `cc' and is not recognized,
   or -4 if ASMSPEC is `memory' and is not recognized.
   Accept an exact spelling or a decimal number.
   Prefixes such as % are optional.  */
extern int decode_reg_name (const char *);

extern void assemble_alias (tree, tree);

extern void default_assemble_visibility (tree, int);

/* Output a string of literal assembler code
   for an `asm' keyword used between functions.  */
extern void assemble_asm (tree);

/* Output assembler code for the constant pool of a function and associated
   with defining the name of the function.  DECL describes the function.
   NAME is the function's name.  For the constant pool, we use the current
   constant pool data.  */
extern void assemble_start_function (tree, const char *);

/* Output assembler code associated with defining the size of the
   function.  DECL describes the function.  NAME is the function's name.  */
extern void assemble_end_function (tree, const char *);

/* Assemble everything that is needed for a variable or function declaration.
   Not used for automatic variables, and not used for function definitions.
   Should not be called for variables of incomplete structure type.

   TOP_LEVEL is nonzero if this variable has file scope.
   AT_END is nonzero if this is the special handling, at end of compilation,
   to define things that have had only tentative definitions.
   DONT_OUTPUT_DATA if nonzero means don't actually output the
   initial value (that will be done by the caller).  */
extern void assemble_variable (tree, int, int, int);

/* Compute the alignment of variable specified by DECL.
   DONT_OUTPUT_DATA is from assemble_variable.  */
extern void align_variable (tree decl, bool dont_output_data);

/* Queue for outputing something to declare an external symbol to the
   assembler.  (Most assemblers don't need this, so we normally output
   nothing.)  Do nothing if DECL is not external.  */
extern void assemble_external (tree);

/* Assemble code to leave SIZE bytes of zeros.  */
extern void assemble_zeros (unsigned HOST_WIDE_INT);

/* Assemble an alignment pseudo op for an ALIGN-bit boundary.  */
extern void assemble_align (int);

/* Assemble a string constant with the specified C string as contents.  */
extern void assemble_string (const char *, int);

/* Similar, for calling a library function FUN.  */
extern void assemble_external_libcall (rtx);

/* Assemble a label named NAME.  */
extern void assemble_label (const char *);

/* Output to FILE (an assembly file) a reference to NAME.  If NAME
   starts with a *, the rest of NAME is output verbatim.  Otherwise
   NAME is transformed in a target-specific way (usually by the
   addition of an underscore).  */
extern void assemble_name_raw (FILE *, const char *);

/* Like assemble_name_raw, but should be used when NAME might refer to
   an entity that is also represented as a tree (like a function or
   variable).  If NAME does refer to such an entity, that entity will
   be marked as referenced.  */
extern void assemble_name (FILE *, const char *);

/* Return the assembler directive for creating a given kind of integer
   object.  SIZE is the number of bytes in the object and ALIGNED_P
   indicates whether it is known to be aligned.  Return NULL if the
   assembly dialect has no such directive.

   The returned string should be printed at the start of a new line and
   be followed immediately by the object's initial value.  */
extern const char *integer_asm_op (int, int);

/* Use directive OP to assemble an integer object X.  Print OP at the
   start of the line, followed immediately by the value of X.  */
extern void assemble_integer_with_op (const char *, rtx);

/* The default implementation of the asm_out.integer target hook.  */
extern bool default_assemble_integer (rtx, unsigned int, int);

/* Assemble the integer constant X into an object of SIZE bytes.  ALIGN is
   the alignment of the integer in bits.  Return 1 if we were able to output
   the constant, otherwise 0.  If FORCE is nonzero the constant must
   be outputable. */
extern bool assemble_integer (rtx, unsigned, unsigned, int);

/* An interface to assemble_integer for the common case in which a value is
   fully aligned and must be printed.  VALUE is the value of the integer
   object and SIZE is the number of bytes it contains.  */
#define assemble_aligned_integer(SIZE, VALUE) \
  assemble_integer (VALUE, SIZE, (SIZE) * BITS_PER_UNIT, 1)

#ifdef REAL_VALUE_TYPE_SIZE
/* Assemble the floating-point constant D into an object of size MODE.  */
extern void assemble_real (REAL_VALUE_TYPE, enum machine_mode, unsigned);
#endif

/* Return the size of the constant pool.  */
extern int get_pool_size (void);

#ifdef HAVE_peephole
extern rtx peephole (rtx);
#endif

extern void output_shared_constant_pool (void);

extern void output_object_blocks (void);

/* Whether a constructor CTOR is a valid static constant initializer if all
   its elements are.  This used to be internal to initializer_constant_valid_p
   and has been exposed to let other functions like categorize_ctor_elements
   evaluate the property while walking a constructor for other purposes.  */

extern bool constructor_static_from_elts_p (tree);

/* Return nonzero if VALUE is a valid constant-valued expression
   for use in initializing a static variable; one that can be an
   element of a "constant" initializer.

   Return null_pointer_node if the value is absolute;
   if it is relocatable, return the variable that determines the relocation.
   We assume that VALUE has been folded as much as possible;
   therefore, we do not need to check for such things as
   arithmetic-combinations of integers.  */
extern tree initializer_constant_valid_p (tree, tree);

/* Output assembler code for constant EXP to FILE, with no label.
   This includes the pseudo-op such as ".int" or ".byte", and a newline.
   Assumes output_addressed_constants has been done on EXP already.

   Generate exactly SIZE bytes of assembler data, padding at the end
   with zeros if necessary.  SIZE must always be specified.

   ALIGN is the alignment in bits that may be assumed for the data.  */
extern void output_constant (tree, unsigned HOST_WIDE_INT, unsigned int);

/* When outputting delayed branch sequences, this rtx holds the
   sequence being output.  It is null when no delayed branch
   sequence is being output, so it can be used as a test in the
   insn output code.

   This variable is defined  in final.c.  */
extern rtx final_sequence;

/* The line number of the beginning of the current function.  Various
   md code needs this so that it can output relative linenumbers.  */

#ifdef SDB_DEBUGGING_INFO /* Avoid undef sym in certain broken linkers.  */
extern int sdb_begin_function_line;
#endif

/* File in which assembler code is being written.  */

#ifdef BUFSIZ
extern FILE *asm_out_file;
#endif

/* The first global object in the file.  */
extern const char *first_global_object_name;

/* The first weak object in the file.  */
extern const char *weak_global_object_name;

/* Nonzero if function being compiled doesn't contain any calls
   (ignoring the prologue and epilogue).  This is set prior to
   local register allocation and is valid for the remaining
   compiler passes.  */

extern int current_function_is_leaf;

/* Nonzero if function being compiled doesn't modify the stack pointer
   (ignoring the prologue and epilogue).  This is only valid after
   life_analysis has run.  */

extern int current_function_sp_is_unchanging;

/* Nonzero if the function being compiled is a leaf function which only
   uses leaf registers.  This is valid after reload (specifically after
   sched2) and is useful only if the port defines LEAF_REGISTERS.  */

extern int current_function_uses_only_leaf_regs;

/* Default file in which to dump debug output.  */

#ifdef BUFSIZ
extern FILE *dump_file;
#endif

/* Nonnull if the insn currently being emitted was a COND_EXEC pattern.  */
extern rtx current_insn_predicate;

/* Last insn processed by final_scan_insn.  */
extern rtx current_output_insn;

/* Nonzero while outputting an `asm' with operands.
   This means that inconsistencies are the user's fault, so don't die.
   The precise value is the insn being output, to pass to error_for_asm.  */
extern rtx this_is_asm_operands;

/* Carry information from ASM_DECLARE_OBJECT_NAME
   to ASM_FINISH_DECLARE_OBJECT.  */
extern int size_directive_output;
extern tree last_assemble_variable_decl;

extern bool first_function_block_is_cold;

/* Decide whether DECL needs to be in a writable section.
   RELOC is the same as for SELECT_SECTION.  */
extern bool decl_readonly_section (tree, int);

/* This can be used to compute RELOC for the function above, when
   given a constant expression.  */
extern int compute_reloc_for_constant (tree);

/* User label prefix in effect for this compilation.  */
extern const char *user_label_prefix;

/* Default target function prologue and epilogue assembler output.  */
extern void default_function_pro_epilogue (FILE *, HOST_WIDE_INT);

/* Default target hook that outputs nothing to a stream.  */
extern void no_asm_to_stream (FILE *);

/* Flags controlling properties of a section.  */
#define SECTION_ENTSIZE	 0x000ff	/* entity size in section */
#define SECTION_CODE	 0x00100	/* contains code */
#define SECTION_WRITE	 0x00200	/* data is writable */
#define SECTION_DEBUG	 0x00400	/* contains debug data */
#define SECTION_LINKONCE 0x00800	/* is linkonce */
#define SECTION_SMALL	 0x01000	/* contains "small data" */
#define SECTION_BSS	 0x02000	/* contains zeros only */
#define SECTION_FORGET	 0x04000	/* forget that we've entered the section */
#define SECTION_MERGE	 0x08000	/* contains mergeable data */
#define SECTION_STRINGS  0x10000	/* contains zero terminated strings without
					   embedded zeros */
#define SECTION_OVERRIDE 0x20000	/* allow override of default flags */
#define SECTION_TLS	 0x40000	/* contains thread-local storage */
#define SECTION_NOTYPE	 0x80000	/* don't output @progbits */
#define SECTION_DECLARED 0x100000	/* section has been used */
#define SECTION_STYLE_MASK 0x600000	/* bits used for SECTION_STYLE */
#define SECTION_COMMON   0x800000	/* contains common data */
#define SECTION_MACH_DEP 0x1000000	/* subsequent bits reserved for target */

/* This SECTION_STYLE is used for unnamed sections that we can switch
   to using a special assembler directive.  */
#define SECTION_UNNAMED	 0x000000

/* This SECTION_STYLE is used for named sections that we can switch
   to using a general section directive.  */
#define SECTION_NAMED	 0x200000

/* This SECTION_STYLE is used for sections that we cannot switch to at
   all.  The choice of section is implied by the directive that we use
   to declare the object.  */
#define SECTION_NOSWITCH 0x400000

/* A helper function for default_elf_select_section and
   default_elf_unique_section.  Categorizes the DECL.  */

enum section_category
{
  SECCAT_TEXT,

  SECCAT_RODATA,
  SECCAT_RODATA_MERGE_STR,
  SECCAT_RODATA_MERGE_STR_INIT,
  SECCAT_RODATA_MERGE_CONST,
  SECCAT_SRODATA,

  SECCAT_DATA,

  /* To optimize loading of shared programs, define following subsections
     of data section:
	_REL	Contains data that has relocations, so they get grouped
		together and dynamic linker will visit fewer pages in memory.
	_RO	Contains data that is otherwise read-only.  This is useful
		with prelinking as most relocations won't be dynamically
		linked and thus stay read only.
	_LOCAL	Marks data containing relocations only to local objects.
		These relocations will get fully resolved by prelinking.  */
  SECCAT_DATA_REL,
  SECCAT_DATA_REL_LOCAL,
  SECCAT_DATA_REL_RO,
  SECCAT_DATA_REL_RO_LOCAL,

  SECCAT_SDATA,
  SECCAT_TDATA,

  SECCAT_BSS,
  SECCAT_SBSS,
  SECCAT_TBSS
};

/* Information that is provided by all instances of the section type.  */
struct section_common GTY(()) {
  /* The set of SECTION_* flags that apply to this section.  */
  unsigned int flags;
};

/* Information about a SECTION_NAMED section.  */
struct named_section GTY(()) {
  struct section_common common;

  /* The name of the section.  */
  const char *name;

  /* If nonnull, the VAR_DECL or FUNCTION_DECL with which the
     section is associated.  */
  tree decl;
};

/* A callback that writes the assembly code for switching to an unnamed
   section.  The argument provides callback-specific data.  */
typedef void (*unnamed_section_callback) (const void *);

/* Information about a SECTION_UNNAMED section.  */
struct unnamed_section GTY(()) {
  struct section_common common;

  /* The callback used to switch to the section, and the data that
     should be passed to the callback.  */
  unnamed_section_callback GTY ((skip)) callback;
  const void *GTY ((skip)) data;

  /* The next entry in the chain of unnamed sections.  */
  section *next;
};

/* A callback that writes the assembly code for a decl in a
   SECTION_NOSWITCH section.  DECL is the decl that should be assembled
   and NAME is the name of its SYMBOL_REF.  SIZE is the size of the decl
   in bytes and ROUNDED is that size rounded up to the next
   BIGGEST_ALIGNMENT / BITS_PER_UNIT boundary.

   Return true if the callback used DECL_ALIGN to set the object's
   alignment.  A false return value implies that we are relying
   on the rounded size to align the decl.  */
typedef bool (*noswitch_section_callback) (tree decl, const char *name,
					   unsigned HOST_WIDE_INT size,
					   unsigned HOST_WIDE_INT rounded);

/* Information about a SECTION_NOSWITCH section.  */
struct noswitch_section GTY(()) {
  struct section_common common;

  /* The callback used to assemble decls in this section.  */
  noswitch_section_callback GTY ((skip)) callback;
};

/* Information about a section, which may be named or unnamed.  */
union section GTY ((desc ("SECTION_STYLE (&(%h))")))
{
  struct section_common GTY ((skip)) common;
  struct named_section GTY ((tag ("SECTION_NAMED"))) named;
  struct unnamed_section GTY ((tag ("SECTION_UNNAMED"))) unnamed;
  struct noswitch_section GTY ((tag ("SECTION_NOSWITCH"))) noswitch;
};

/* Return the style of section SECT.  */
#define SECTION_STYLE(SECT) ((SECT)->common.flags & SECTION_STYLE_MASK)

struct object_block;

/* Special well-known sections.  */
extern GTY(()) section *text_section;
extern GTY(()) section *data_section;
extern GTY(()) section *readonly_data_section;
extern GTY(()) section *sdata_section;
extern GTY(()) section *ctors_section;
extern GTY(()) section *dtors_section;
extern GTY(()) section *bss_section;
extern GTY(()) section *sbss_section;
extern GTY(()) section *exception_section;
extern GTY(()) section *eh_frame_section;
extern GTY(()) section *tls_comm_section;
extern GTY(()) section *comm_section;
extern GTY(()) section *lcomm_section;
extern GTY(()) section *bss_noswitch_section;

extern GTY(()) section *in_section;
extern GTY(()) bool in_cold_section_p;

extern section *get_unnamed_section (unsigned int, void (*) (const void *),
				     const void *);
extern section *get_section (const char *, unsigned int, tree);
extern section *get_named_section (tree, const char *, int);
extern void place_block_symbol (rtx);
extern rtx get_section_anchor (struct object_block *, HOST_WIDE_INT,
			       enum tls_model);
extern section *mergeable_constant_section (enum machine_mode,
					    unsigned HOST_WIDE_INT,
					    unsigned int);
extern section *function_section (tree);
extern section *unlikely_text_section (void);
extern section *current_function_section (void);

extern bool unlikely_text_section_p (section *);
extern void switch_to_section (section *);
extern void output_section_asm_op (const void *);

extern unsigned int default_section_type_flags (tree, const char *, int);

extern bool have_global_bss_p (void);
extern void default_no_named_section (const char *, unsigned int, tree);
extern void default_elf_asm_named_section (const char *, unsigned int, tree);
extern enum section_category categorize_decl_for_section (tree, int);
extern void default_coff_asm_named_section (const char *, unsigned int, tree);
extern void default_pe_asm_named_section (const char *, unsigned int, tree);

extern void default_stabs_asm_out_destructor (rtx, int);
extern void default_named_section_asm_out_destructor (rtx, int);
extern void default_dtor_section_asm_out_destructor (rtx, int);
extern void default_stabs_asm_out_constructor (rtx, int);
extern void default_named_section_asm_out_constructor (rtx, int);
extern void default_ctor_section_asm_out_constructor (rtx, int);

extern section *default_select_section (tree, int, unsigned HOST_WIDE_INT);
extern section *default_elf_select_section (tree, int, unsigned HOST_WIDE_INT);
extern void default_unique_section (tree, int);
extern section *default_function_rodata_section (tree);
extern section *default_no_function_rodata_section (tree);
extern section *default_select_rtx_section (enum machine_mode, rtx,
					    unsigned HOST_WIDE_INT);
extern section *default_elf_select_rtx_section (enum machine_mode, rtx,
						unsigned HOST_WIDE_INT);
extern void default_encode_section_info (tree, rtx, int);
extern const char *default_strip_name_encoding (const char *);
extern void default_asm_output_anchor (rtx);
extern bool default_use_anchors_for_symbol_p (rtx);
extern bool default_binds_local_p (tree);
extern bool default_binds_local_p_1 (tree, int);
extern void default_globalize_label (FILE *, const char *);
extern void default_emit_unwind_label (FILE *, tree, int, int);
extern void default_emit_except_table_label (FILE *);
extern void default_internal_label (FILE *, const char *, unsigned long);
extern void default_file_start (void);
extern void file_end_indicate_exec_stack (void);
extern bool default_valid_pointer_mode (enum machine_mode);

extern void default_elf_asm_output_external (FILE *file, tree,
					     const char *);
extern int maybe_assemble_visibility (tree);

extern int default_address_cost (rtx);

/* dbxout helper functions */
#if defined DBX_DEBUGGING_INFO || defined XCOFF_DEBUGGING_INFO

extern void dbxout_int (int);
extern void dbxout_stabd (int, int);
extern void dbxout_begin_stabn (int);
extern void dbxout_begin_stabn_sline (int);
extern void dbxout_begin_empty_stabs (int);
extern void dbxout_begin_simple_stabs (const char *, int);
extern void dbxout_begin_simple_stabs_desc (const char *, int, int);

extern void dbxout_stab_value_zero (void);
extern void dbxout_stab_value_label (const char *);
extern void dbxout_stab_value_label_diff (const char *, const char *);
extern void dbxout_stab_value_internal_label (const char *, int *);
extern void dbxout_stab_value_internal_label_diff (const char *, int *,
						   const char *);

#endif

#endif /* ! GCC_OUTPUT_H */
