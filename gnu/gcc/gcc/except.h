/* Exception Handling interface routines.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Mike Stump <mrs@cygnus.com>.

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


struct function;

/* Per-function EH data.  Used only in except.c, but GC and others
   manipulate pointers to the opaque type.  */
struct eh_status;

/* Internal structure describing a region.  */
struct eh_region;

/* Test: is exception handling turned on?  */
extern int doing_eh (int);

/* Note that the current EH region (if any) may contain a throw, or a
   call to a function which itself may contain a throw.  */
extern void note_eh_region_may_contain_throw (struct eh_region *);
extern void note_current_region_may_contain_throw (void);

/* Invokes CALLBACK for every exception handler label.  Only used by old
   loop hackery; should not be used by new code.  */
extern void for_each_eh_label (void (*) (rtx));

/* Invokes CALLBACK for every exception region in the current function.  */
extern void for_each_eh_region (void (*) (struct eh_region *));

/* Determine if the given INSN can throw an exception.  */
extern bool can_throw_internal_1 (int, bool);
extern bool can_throw_internal (rtx);
extern bool can_throw_external_1 (int, bool);
extern bool can_throw_external (rtx);

/* Set TREE_NOTHROW and cfun->all_throwers_are_sibcalls.  */
extern unsigned int set_nothrow_function_flags (void);

/* After initial rtl generation, call back to finish generating
   exception support code.  */
extern void finish_eh_generation (void);

extern void init_eh (void);
extern void init_eh_for_function (void);

extern rtx reachable_handlers (rtx);
extern void maybe_remove_eh_handler (rtx);

extern void convert_from_eh_region_ranges (void);
extern unsigned int convert_to_eh_region_ranges (void);
extern void find_exception_handler_labels (void);
extern bool current_function_has_exception_handlers (void);
extern void output_function_exception_table (void);

extern void expand_builtin_unwind_init (void);
extern rtx expand_builtin_eh_return_data_regno (tree);
extern rtx expand_builtin_extract_return_addr (tree);
extern void expand_builtin_init_dwarf_reg_sizes (tree);
extern rtx expand_builtin_frob_return_addr (tree);
extern rtx expand_builtin_dwarf_sp_column (void);
extern void expand_builtin_eh_return (tree, tree);
extern void expand_eh_return (void);
extern rtx expand_builtin_extend_pointer (tree);
extern rtx get_exception_pointer (struct function *);
extern rtx get_exception_filter (struct function *);
typedef tree (*duplicate_eh_regions_map) (tree, void *);
extern int duplicate_eh_regions (struct function *, duplicate_eh_regions_map,
				 void *, int, int);

extern void sjlj_emit_function_exit_after (rtx);
extern void default_init_unwind_resume_libfunc (void);

extern struct eh_region *gen_eh_region_cleanup (struct eh_region *,
						struct eh_region *);
extern struct eh_region *gen_eh_region_try (struct eh_region *);
extern struct eh_region *gen_eh_region_catch (struct eh_region *, tree);
extern struct eh_region *gen_eh_region_allowed (struct eh_region *, tree);
extern struct eh_region *gen_eh_region_must_not_throw (struct eh_region *);
extern int get_eh_region_number (struct eh_region *);
extern bool get_eh_region_may_contain_throw (struct eh_region *);
extern tree get_eh_region_tree_label (struct eh_region *);
extern void set_eh_region_tree_label (struct eh_region *, tree);

extern void foreach_reachable_handler (int, bool,
				       void (*) (struct eh_region *, void *),
				       void *);

extern void collect_eh_region_array (void);
extern void expand_resx_expr (tree);
extern void verify_eh_tree (struct function *);
extern void dump_eh_tree (FILE *, struct function *);
extern bool eh_region_outer_p (struct function *, int, int);
extern int eh_region_outermost (struct function *, int, int);

/* tree-eh.c */
extern void add_stmt_to_eh_region_fn (struct function *, tree, int);
extern bool remove_stmt_from_eh_region_fn (struct function *, tree);
extern int lookup_stmt_eh_region_fn (struct function *, tree);
extern int lookup_stmt_eh_region (tree);
extern bool verify_eh_edges (tree);

/* If non-NULL, this is a function that returns an expression to be
   executed if an unhandled exception is propagated out of a cleanup
   region.  For example, in C++, an exception thrown by a destructor
   during stack unwinding is required to result in a call to
   `std::terminate', so the C++ version of this function returns a
   CALL_EXPR for `std::terminate'.  */
extern tree (*lang_protect_cleanup_actions) (void);

/* Return true if type A catches type B.  */
extern int (*lang_eh_type_covers) (tree a, tree b);

/* Map a type to a runtime object to match type.  */
extern tree (*lang_eh_runtime_type) (tree);


/* Just because the user configured --with-sjlj-exceptions=no doesn't
   mean that we can use call frame exceptions.  Detect that the target
   has appropriate support.  */

#ifndef MUST_USE_SJLJ_EXCEPTIONS
# if !(defined (EH_RETURN_DATA_REGNO)			\
       && (defined (TARGET_UNWIND_INFO)			\
	   || (DWARF2_UNWIND_INFO			\
	       && (defined (EH_RETURN_HANDLER_RTX)	\
		   || defined (HAVE_eh_return)))))
#  define MUST_USE_SJLJ_EXCEPTIONS	1
# else
#  define MUST_USE_SJLJ_EXCEPTIONS	0
# endif
#endif

#ifdef CONFIG_SJLJ_EXCEPTIONS
# if CONFIG_SJLJ_EXCEPTIONS == 1
#  define USING_SJLJ_EXCEPTIONS		1
# endif
# if CONFIG_SJLJ_EXCEPTIONS == 0
#  define USING_SJLJ_EXCEPTIONS		0
#  ifndef EH_RETURN_DATA_REGNO
    #error "EH_RETURN_DATA_REGNO required"
#  endif
#  if !defined(EH_RETURN_HANDLER_RTX) && !defined(HAVE_eh_return)
    #error "EH_RETURN_HANDLER_RTX or eh_return required"
#  endif
#  if !defined(DWARF2_UNWIND_INFO) && !defined(TARGET_UNWIND_INFO)
    #error "{DWARF2,TARGET}_UNWIND_INFO required"
#  endif
# endif
#else
# define USING_SJLJ_EXCEPTIONS		MUST_USE_SJLJ_EXCEPTIONS
#endif

struct throw_stmt_node GTY(())
{
  tree stmt;
  int region_nr;
};

extern struct htab *get_eh_throw_stmt_table (struct function *);
extern void set_eh_throw_stmt_table (struct function *, struct htab *);

#ifdef ENABLE_CHECKING
extern void verify_eh_throw_table_statements (void);
#endif
