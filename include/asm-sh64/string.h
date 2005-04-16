#ifndef __ASM_SH64_STRING_H
#define __ASM_SH64_STRING_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/string.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * Empty on purpose. ARCH SH64 ASM libs are out of the current project scope.
 *
 */

#define __HAVE_ARCH_MEMCPY

extern void *memcpy(void *dest, const void *src, size_t count);

#endif
