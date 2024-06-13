/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef TOOLS_ARCH_SPARC_UAPI_ASM_MMAN_FIX_H
#define TOOLS_ARCH_SPARC_UAPI_ASM_MMAN_FIX_H
#define MAP_DENYWRITE	0x0800
#define MAP_EXECUTABLE	0x1000
#define MAP_GROWSDOWN	0x0200
#define MAP_LOCKED      0x100
#define MAP_NORESERVE   0x40
#include <uapi/asm-generic/mman-common.h>
/* MAP_32BIT is undefined on sparc, fix it for perf */
#define MAP_32BIT	0
#endif
