/*
 * Copied from the kernel sources to tools/:
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2012 Tensilica Inc.
 */

#ifndef _TOOLS_LINUX_XTENSA_SYSTEM_H
#define _TOOLS_LINUX_XTENSA_SYSTEM_H

#define mb()  ({ __asm__ __volatile__("memw" : : : "memory"); })
#define rmb() barrier()
#define wmb() mb()

#endif /* _TOOLS_LINUX_XTENSA_SYSTEM_H */
