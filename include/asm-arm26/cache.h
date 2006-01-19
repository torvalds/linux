/*
 *  linux/include/asm-arm26/cache.h
 */
#ifndef __ASMARM_CACHE_H
#define __ASMARM_CACHE_H

#define        L1_CACHE_SHIFT  5
#define        L1_CACHE_BYTES  (1 << L1_CACHE_SHIFT)
#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))
#define        SMP_CACHE_BYTES L1_CACHE_BYTES

#endif
