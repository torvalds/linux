/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UAPI_LINUX_TYPES_H
#define _UAPI_LINUX_TYPES_H

#include <asm-generic/int-ll64.h>

#ifndef __ASSEMBLER__

/* copied from linux:include/uapi/linux/types.h */
#define __bitwise
typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;

typedef __u16 __bitwise __sum16;
typedef __u32 __bitwise __wsum;

#define __aligned_u64 __u64 __attribute__((aligned(8)))
#define __aligned_be64 __be64 __attribute__((aligned(8)))
#define __aligned_le64 __le64 __attribute__((aligned(8)))

#endif /* __ASSEMBLER__ */
#endif /* _UAPI_LINUX_TYPES_H */
