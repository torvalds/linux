#ifndef __ASM_SH_STRING_64_H
#define __ASM_SH_STRING_64_H

/*
 * include/asm-sh/string_64.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define __HAVE_ARCH_MEMCPY
extern void *memcpy(void *dest, const void *src, size_t count);

#endif /* __ASM_SH_STRING_64_H */
