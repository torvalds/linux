/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef TOOLS_ARCH_TILE_UAPI_ASM_MMAN_FIX_H
#define TOOLS_ARCH_TILE_UAPI_ASM_MMAN_FIX_H
#define MAP_DENYWRITE	0x0800
#define MAP_EXECUTABLE	0x1000
#define MAP_GROWSDOWN	0x0100
#define MAP_HUGETLB	0x4000
#define MAP_LOCKED	0x0200
#define MAP_NONBLOCK	0x0080
#define MAP_NORESERVE	0x0400
#define MAP_POPULATE	0x0040
#define MAP_STACK	MAP_GROWSDOWN
#include <uapi/asm-generic/mman-common.h>
/* MAP_32BIT is undefined on tile, fix it for perf */
#define MAP_32BIT	0
#endif
