#ifndef TOOLS_ARCH_SCORE_UAPI_ASM_MMAN_FIX_H
#define TOOLS_ARCH_SCORE_UAPI_ASM_MMAN_FIX_H
#include <uapi/asm-generic/mman.h>
/* MAP_32BIT is undefined on score, fix it for perf */
#define MAP_32BIT	0
#endif
