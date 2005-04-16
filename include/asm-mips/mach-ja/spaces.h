/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03, 04 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef __ASM_MACH_JA_SPACES_H
#define __ASM_MACH_JA_SPACES_H

/*
 * Memory above this physical address will be considered highmem.
 */
#define HIGHMEM_START		0x08000000UL

#include_next <spaces.h>

#endif /* __ASM_MACH_JA_SPACES_H */
