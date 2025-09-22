/*===------ cet.h -Control-flow Enforcement Technology  feature ------------===
 * Add x86 feature with IBT and/or SHSTK bits to ELF program property if they
 * are enabled. Otherwise, contents in this header file are unused. This file
 * is mainly design for assembly source code which want to enable CET.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __CET_H
#define __CET_H

#ifdef __ASSEMBLER__

#ifndef __CET__
# define _CET_ENDBR
#endif

#ifdef __CET__

# ifdef __LP64__
#  if __CET__ & 0x1
#    define _CET_ENDBR endbr64
#  else
#    define _CET_ENDBR
#  endif
# else
#  if __CET__ & 0x1
#    define _CET_ENDBR endbr32
#  else
#    define _CET_ENDBR
#  endif
# endif


#  ifdef __LP64__
#   define __PROPERTY_ALIGN 3
#  else
#   define __PROPERTY_ALIGN 2
#  endif

	.pushsection ".note.gnu.property", "a"
	.p2align __PROPERTY_ALIGN
	.long 1f - 0f		/* name length.  */
	.long 4f - 1f		/* data length.  */
	/* NT_GNU_PROPERTY_TYPE_0.   */
	.long 5			/* note type.  */
0:
	.asciz "GNU"		/* vendor name.  */
1:
	.p2align __PROPERTY_ALIGN
	/* GNU_PROPERTY_X86_FEATURE_1_AND.  */
	.long 0xc0000002	/* pr_type.  */
	.long 3f - 2f		/* pr_datasz.  */
2:
	/* GNU_PROPERTY_X86_FEATURE_1_XXX.  */
	.long __CET__
3:
	.p2align __PROPERTY_ALIGN
4:
	.popsection
#endif
#endif
#endif
