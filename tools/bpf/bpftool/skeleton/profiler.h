/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __PROFILER_H
#define __PROFILER_H

/* useful typedefs from vmlinux.h */

typedef signed char __s8;
typedef unsigned char __u8;
typedef short int __s16;
typedef short unsigned int __u16;
typedef int __s32;
typedef unsigned int __u32;
typedef long long int __s64;
typedef long long unsigned int __u64;

typedef __s8 s8;
typedef __u8 u8;
typedef __s16 s16;
typedef __u16 u16;
typedef __s32 s32;
typedef __u32 u32;
typedef __s64 s64;
typedef __u64 u64;

enum {
	false = 0,
	true = 1,
};

#ifdef __CHECKER__
#define __bitwise__ __attribute__((bitwise))
#else
#define __bitwise__
#endif

typedef __u16 __bitwise__ __le16;
typedef __u16 __bitwise__ __be16;
typedef __u32 __bitwise__ __le32;
typedef __u32 __bitwise__ __be32;
typedef __u64 __bitwise__ __le64;
typedef __u64 __bitwise__ __be64;

typedef __u16 __bitwise__ __sum16;
typedef __u32 __bitwise__ __wsum;

#endif /* __PROFILER_H */
