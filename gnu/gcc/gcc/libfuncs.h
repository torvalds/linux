/* Definitions for code generation pass of GNU compiler.
   Copyright (C) 2001, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_LIBFUNCS_H
#define GCC_LIBFUNCS_H

/* Enumeration of indexes into libfunc_table.  */
enum libfunc_index
{
  LTI_abort,
  LTI_memcpy,
  LTI_memmove,
  LTI_memcmp,
  LTI_memset,
  LTI_setbits,

  LTI_unwind_resume,
  LTI_eh_personality,
  LTI_setjmp,
  LTI_longjmp,
  LTI_unwind_sjlj_register,
  LTI_unwind_sjlj_unregister,

  LTI_profile_function_entry,
  LTI_profile_function_exit,

  LTI_gcov_flush,

  LTI_MAX
};

/* SYMBOL_REF rtx's for the library functions that are called
   implicitly and not via optabs.  */
extern GTY(()) rtx libfunc_table[LTI_MAX];

/* Accessor macros for libfunc_table.  */

#define abort_libfunc	(libfunc_table[LTI_abort])
#define memcpy_libfunc	(libfunc_table[LTI_memcpy])
#define memmove_libfunc	(libfunc_table[LTI_memmove])
#define memcmp_libfunc	(libfunc_table[LTI_memcmp])
#define memset_libfunc	(libfunc_table[LTI_memset])
#define setbits_libfunc	(libfunc_table[LTI_setbits])

#define unwind_resume_libfunc	(libfunc_table[LTI_unwind_resume])
#define eh_personality_libfunc	(libfunc_table[LTI_eh_personality])
#define setjmp_libfunc	(libfunc_table[LTI_setjmp])
#define longjmp_libfunc	(libfunc_table[LTI_longjmp])
#define unwind_sjlj_register_libfunc (libfunc_table[LTI_unwind_sjlj_register])
#define unwind_sjlj_unregister_libfunc \
  (libfunc_table[LTI_unwind_sjlj_unregister])

#define profile_function_entry_libfunc	(libfunc_table[LTI_profile_function_entry])
#define profile_function_exit_libfunc	(libfunc_table[LTI_profile_function_exit])

#define gcov_flush_libfunc	(libfunc_table[LTI_gcov_flush])

#endif /* GCC_LIBFUNCS_H */
