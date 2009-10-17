#ifndef PERF_ASM_TYPES_H_
#define PERF_ASM_TYPES_H_

#include <linux/compiler.h>
#include "../../types.h"
#include <sys/types.h>

/* CHECKME: Not sure both always match */
#define BITS_PER_LONG	__WORDSIZE

typedef u64	__u64;
typedef u32	__u32;
typedef u16	__u16;
typedef u8	__u8;
typedef s64	__s64;

#endif /* PERF_ASM_TYPES_H_ */
