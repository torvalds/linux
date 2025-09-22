/* Frv map GCC names to FR-V ABI.
   Copyright (C) 2000, 2003, 2004 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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

/* For each of the functions in the library that has a corresponding name in
   the ABI, add an equivalence between the GCC name and the ABI name.  This is
   in a separate file from frv.h so that fp-bit.c can be made to include it.  */

#ifdef __GNUC__
#ifdef __FRV_UNDERSCORE__
#define RENAME_LIBRARY(OLD,NEW)						\
__asm__ (".globl\t_" #NEW "\n"						\
	 "_" #NEW "=_" #OLD "\n"					\
	 "\t.type\t_" #NEW ",@function\n");

#else
#define RENAME_LIBRARY(OLD,NEW)						\
__asm__ (".globl\t" #NEW "\n"						\
	 #NEW "=" #OLD "\n"						\
	 "\t.type\t" #NEW ",@function\n");
#endif

#define CREATE_DOUBLE_SHIFT(OLD,NEW)					\
__asm__ (".text\n"							\
	 "\t.globl\t" #NEW "\n"						\
	 "\t.type\t" #NEW ",@function\n"				\
	 #NEW ":\n"							\
	 "\tor\tgr11, gr0, gr10\n"					\
	 ".L" #OLD " = " #OLD "\n"					\
	 "\tbra\t.L" #OLD "\n");

#ifdef L_sf_to_df
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__extendsfdf2,__ftod)
#endif

#ifdef L_sf_to_si
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixsfsi,__ftoi)
#endif

#ifdef L_sf_to_usi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixunssfsi,__ftoui)
#endif

#ifdef L_df_to_si
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixdfsi,__dtoi)
#endif

#ifdef L_fixunssfsi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixunssfsi,__ftoui)
#endif

#ifdef L_fixunsdfsi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixunsdfsi,__dtoui)
#endif

#ifdef L_fixsfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixsfdi,__ftoll)
#endif

#ifdef L_fixdfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixdfdi,__dtoll)
#endif

#ifdef L_fixunssfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixunssfdi,__ftoull)
#endif

#ifdef L_fixunsdfdi
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__fixunsdfdi,__dtoull)
#endif

#ifdef L_si_to_sf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__floatsisf,__itof)
#endif

#ifdef L_di_to_sf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__floatdisf,__lltof)
#endif

#ifdef L_df_to_sf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__truncdfsf2,__dtof)
#endif

#ifdef L_si_to_df
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__floatsidf,__itod)
#endif

#ifdef L_floatdisf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__floatdisf,__lltof)
#endif

#ifdef L_floatdidf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__floatdidf,__lltod)
#endif

#ifdef L_addsub_df
#define DECLARE_LIBRARY_RENAMES \
	RENAME_LIBRARY(__adddf3,__addd)
	RENAME_LIBRARY(__subdf3,__subd)
#endif

#ifdef L_mul_df
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__muldf3,__muld)
#endif

#ifdef L_div_df
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__divdf3,__divd)
#endif

#ifdef L_addsub_sf
#define DECLARE_LIBRARY_RENAMES \
	RENAME_LIBRARY(__addsf3,__addf) \
	RENAME_LIBRARY(__subsf3,__subf)
#endif

#ifdef L_mul_sf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__mulsf3,__mulf)
#endif

#ifdef L_div_sf
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__divsf3,__divf)
#endif

#ifdef L_ashldi3
#define DECLARE_LIBRARY_RENAMES CREATE_DOUBLE_SHIFT (__ashldi3,__sllll)
#endif

#ifdef L_lshrdi3
#define DECLARE_LIBRARY_RENAMES CREATE_DOUBLE_SHIFT (__lshrdi3,__srlll)
#endif

#ifdef L_ashrdi3
#define DECLARE_LIBRARY_RENAMES CREATE_DOUBLE_SHIFT (__ashrdi3,__srall)
#endif

#ifdef L_adddi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__adddi3,__addll)
#endif

#ifdef L_subdi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__subdi3,__subll)
#endif

#ifdef L_muldi3
#define DECLARE_LIBRARY_RENAMES \
	RENAME_LIBRARY(__muldi3,__mulll)
	RENAME_LIBRARY(__muldi3,__umulll)
#endif

#ifdef L_divdi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__divdi3,__divll)
#endif

#ifdef L_udivdi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__udivdi3,__udivll)
#endif

#ifdef L_moddi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__moddi3,__modll)
#endif

#ifdef L_umoddi3
#define DECLARE_LIBRARY_RENAMES RENAME_LIBRARY(__umoddi3,__umodll)
#endif
#endif /* __GNUC__ */
