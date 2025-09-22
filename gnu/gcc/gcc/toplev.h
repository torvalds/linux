/* toplev.h - Various declarations for functions found in toplev.c
   Copyright (C) 1998, 1999, 2000, 2001, 2003, 2004, 2005
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

#ifndef GCC_TOPLEV_H
#define GCC_TOPLEV_H

/* If non-NULL, return one past-the-end of the matching SUBPART of
   the WHOLE string.  */
#define skip_leading_substring(whole,  part) \
   (strncmp (whole, part, strlen (part)) ? NULL : whole + strlen (part))

extern int toplev_main (unsigned int, const char **);
extern int read_integral_parameter (const char *, const char *, const int);
extern void strip_off_ending (char *, int);
extern const char *trim_filename (const char *);
extern void _fatal_insn_not_found (rtx, const char *, int, const char *)
     ATTRIBUTE_NORETURN;
extern void _fatal_insn (const char *, rtx, const char *, int, const char *)
     ATTRIBUTE_NORETURN;

#define fatal_insn(msgid, insn) \
	_fatal_insn (msgid, insn, __FILE__, __LINE__, __FUNCTION__)
#define fatal_insn_not_found(insn) \
	_fatal_insn_not_found (insn, __FILE__, __LINE__, __FUNCTION__)

/* If we haven't already defined a frontend specific diagnostics
   style, use the generic one.  */
#ifndef GCC_DIAG_STYLE
#define GCC_DIAG_STYLE __gcc_tdiag__
#endif
/* None of these functions are suitable for ATTRIBUTE_PRINTF, because
   each language front end can extend them with its own set of format
   specifiers.  We must use custom format checks.  */
#if GCC_VERSION >= 4001
#define ATTRIBUTE_GCC_DIAG(m, n) __attribute__ ((__format__ (GCC_DIAG_STYLE, m, n))) ATTRIBUTE_NONNULL(m)
#else
#define ATTRIBUTE_GCC_DIAG(m, n) ATTRIBUTE_NONNULL(m)
#endif
extern void internal_error (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2)
     ATTRIBUTE_NORETURN;
extern void warning0 (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2);
/* Pass one of the OPT_W* from options.h as the first parameter.  */
extern void warning (int, const char *, ...) ATTRIBUTE_GCC_DIAG(2,3);
extern void error (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2);
extern void fatal_error (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2)
     ATTRIBUTE_NORETURN;
extern void pedwarn (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2);
extern void sorry (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2);
extern void inform (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2);
extern void verbatim (const char *, ...) ATTRIBUTE_GCC_DIAG(1,2);

extern void rest_of_decl_compilation (tree, int, int);
extern void rest_of_type_compilation (tree, int);
extern void tree_rest_of_compilation (tree);
extern void init_optimization_passes (void);
extern void finish_optimization_passes (void);
extern bool enable_rtl_dump_file (int);

extern void announce_function (tree);

extern void error_for_asm (rtx, const char *, ...) ATTRIBUTE_GCC_DIAG(2,3);
extern void warning_for_asm (rtx, const char *, ...) ATTRIBUTE_GCC_DIAG(2,3);
extern void warn_deprecated_use (tree);

#ifdef BUFSIZ
extern void output_quoted_string	(FILE *, const char *);
extern void output_file_directive	(FILE *, const char *);
#endif

#ifdef BUFSIZ
  /* N.B. Unlike all the others, fnotice is just gettext+fprintf, and
     therefore it can have ATTRIBUTE_PRINTF.  */
extern void fnotice			(FILE *, const char *, ...)
     ATTRIBUTE_PRINTF_2;
#endif

extern void wrapup_global_declaration_1 (tree);
extern bool wrapup_global_declaration_2 (tree);
extern bool wrapup_global_declarations (tree *, int);
extern void check_global_declaration_1 (tree);
extern void check_global_declarations (tree *, int);
extern void emit_debug_global_declarations (tree *, int);
extern void write_global_declarations (void);

/* A unique local time stamp, might be zero if none is available.  */
extern unsigned local_tick;

extern const char *progname;
extern const char *dump_base_name;
extern const char *aux_base_name;
extern const char *aux_info_file_name;
extern const char *asm_file_name;
extern bool exit_after_options;

extern int target_flags_explicit;

/* True if the user has tagged the function with the 'section'
   attribute.  */

extern bool user_defined_section_attribute;

/* See toplev.c.  */
extern int flag_crossjumping;
extern int flag_if_conversion;
extern int flag_if_conversion2;
extern int flag_keep_static_consts;
extern int flag_peel_loops;
extern int flag_rerun_cse_after_loop;
extern int flag_thread_jumps;
extern int flag_tracer;
extern int flag_unroll_loops;
extern int flag_unroll_all_loops;
extern int flag_unswitch_loops;
extern int flag_cprop_registers;
extern int time_report;

/* Things to do with target switches.  */
extern void print_version (FILE *, const char *);
extern void * default_get_pch_validity (size_t *);
extern const char * default_pch_valid_p (const void *, size_t);

/* The hashtable, so that the C front ends can pass it to cpplib.  */
extern struct ht *ident_hash;

/* This function can be used by targets to set the flags originally
    implied by -ffast-math and -fno-fast-math.  */

extern void set_fast_math_flags         (int);

/* Handle -d switch.  */
extern void decode_d_option		(const char *);

/* Return true iff flags are set as if -ffast-math.  */
extern bool fast_math_flags_set_p	(void);

/* Return log2, or -1 if not exact.  */
extern int exact_log2                  (unsigned HOST_WIDE_INT);

/* Return floor of log2, with -1 for zero.  */
extern int floor_log2                  (unsigned HOST_WIDE_INT);

/* Inline versions of the above for speed.  */
#if GCC_VERSION >= 3004
# if HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG
#  define CLZ_HWI __builtin_clzl
#  define CTZ_HWI __builtin_ctzl
# elif HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONGLONG
#  define CLZ_HWI __builtin_clzll
#  define CTZ_HWI __builtin_ctzll
# else
#  define CLZ_HWI __builtin_clz
#  define CTZ_HWI __builtin_ctz
# endif
/*
extern inline int
floor_log2 (unsigned HOST_WIDE_INT x)
{
  return x ? HOST_BITS_PER_WIDE_INT - 1 - (int) CLZ_HWI (x) : -1;
}

extern inline int
exact_log2 (unsigned HOST_WIDE_INT x)
{
  return x == (x & -x) && x ? (int) CTZ_HWI (x) : -1;
}
*/
#endif /* GCC_VERSION >= 3004 */

/* Functions used to get and set GCC's notion of in what directory
   compilation was started.  */

extern const char *get_src_pwd	       (void);
extern bool set_src_pwd		       (const char *);

#endif /* ! GCC_TOPLEV_H */
