/*
 * include/asm-sh/cpu-sh4/shmparam.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_SHMPARAM_H
#define __ASM_CPU_SH4_SHMPARAM_H

/*
 * SH-4 has D-cache alias issue
 */
#define	SHMLBA (PAGE_SIZE*4)		 /* attach addr a multiple of this */

#endif /* __ASM_CPU_SH4_SHMPARAM_H */

