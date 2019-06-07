/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_GENERIC_MMAN_COMMON_TOOLS_ONLY_H
#define __ASM_GENERIC_MMAN_COMMON_TOOLS_ONLY_H

#include <asm-generic/mman-common.h>

/* We need this because we need to have tools/include/uapi/ included in the tools
 * header search path to get access to stuff that is not yet in the system's
 * copy of the files in that directory, but since this cset:
 *
 *     746c9398f5ac ("arch: move common mmap flags to linux/mman.h")
 *
 * We end up making sys/mman.h, that is in the system headers, to not find the
 * MAP_SHARED and MAP_PRIVATE defines because they are not anymore in our copy
 * of asm-generic/mman-common.h. So we define them here and include this header
 * from each of the per arch mman.h headers.
 */
#ifndef MAP_SHARED
#define MAP_SHARED	0x01		/* Share changes */
#define MAP_PRIVATE	0x02		/* Changes are private */
#define MAP_SHARED_VALIDATE 0x03	/* share + validate extension flags */
#endif
#endif // __ASM_GENERIC_MMAN_COMMON_TOOLS_ONLY_H
