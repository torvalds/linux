/*
 * include/asm-xtensa/coprocessor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_COPROCESSOR_H
#define _XTENSA_COPROCESSOR_H

#include <asm/variant/core.h>
#include <asm/variant/tie.h>

#if !XCHAL_HAVE_CP

#define XTENSA_CP_EXTRA_OFFSET 	0
#define XTENSA_CP_EXTRA_ALIGN	1	/* must be a power of 2 */
#define XTENSA_CP_EXTRA_SIZE	0

#else

#define XTOFS(last_start,last_size,align) \
	((last_start+last_size+align-1) & -align)

#define XTENSA_CP_EXTRA_OFFSET	0
#define XTENSA_CP_EXTRA_ALIGN	XCHAL_EXTRA_SA_ALIGN

#define XTENSA_CPE_CP0_OFFSET	\
	XTOFS(XTENSA_CP_EXTRA_OFFSET, XCHAL_EXTRA_SA_SIZE, XCHAL_CP0_SA_ALIGN)
#define XTENSA_CPE_CP1_OFFSET	\
	XTOFS(XTENSA_CPE_CP0_OFFSET, XCHAL_CP0_SA_SIZE, XCHAL_CP1_SA_ALIGN)
#define XTENSA_CPE_CP2_OFFSET	\
	XTOFS(XTENSA_CPE_CP1_OFFSET, XCHAL_CP1_SA_SIZE, XCHAL_CP2_SA_ALIGN)
#define XTENSA_CPE_CP3_OFFSET	\
	XTOFS(XTENSA_CPE_CP2_OFFSET, XCHAL_CP2_SA_SIZE, XCHAL_CP3_SA_ALIGN)
#define XTENSA_CPE_CP4_OFFSET	\
	XTOFS(XTENSA_CPE_CP3_OFFSET, XCHAL_CP3_SA_SIZE, XCHAL_CP4_SA_ALIGN)
#define XTENSA_CPE_CP5_OFFSET	\
	XTOFS(XTENSA_CPE_CP4_OFFSET, XCHAL_CP4_SA_SIZE, XCHAL_CP5_SA_ALIGN)
#define XTENSA_CPE_CP6_OFFSET	\
	XTOFS(XTENSA_CPE_CP5_OFFSET, XCHAL_CP5_SA_SIZE, XCHAL_CP6_SA_ALIGN)
#define XTENSA_CPE_CP7_OFFSET	\
	XTOFS(XTENSA_CPE_CP6_OFFSET, XCHAL_CP6_SA_SIZE, XCHAL_CP7_SA_ALIGN)
#define XTENSA_CP_EXTRA_SIZE	\
	XTOFS(XTENSA_CPE_CP7_OFFSET, XCHAL_CP7_SA_SIZE, 16)

#if XCHAL_CP_NUM > 0
# ifndef __ASSEMBLY__
/*
 * Tasks that own contents of (last user) each coprocessor.
 * Entries are 0 for not-owned or non-existent coprocessors.
 * Note: The size of this structure is fixed to 8 bytes in entry.S
 */
typedef struct {
	struct task_struct *owner;	/* owner */
	int offset;			/* offset in cpextra space. */
} coprocessor_info_t;
# else
#  define COPROCESSOR_INFO_OWNER 0
#  define COPROCESSOR_INFO_OFFSET 4
#  define COPROCESSOR_INFO_SIZE 8
# endif
#endif


#ifndef __ASSEMBLY__
# if XCHAL_CP_NUM > 0
struct task_struct;
extern void release_coprocessors (struct task_struct*);
extern void save_coprocessor_registers(void*, int);
# else
#  define release_coprocessors(task)
# endif
#endif

#endif

#endif	/* _XTENSA_COPROCESSOR_H */
