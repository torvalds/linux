/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef __LIBETH_CACHE_H
#define __LIBETH_CACHE_H

#include <linux/cache.h>

/**
 * libeth_cacheline_group_assert - make sure cacheline group size is expected
 * @type: type of the structure containing the group
 * @grp: group name inside the struct
 * @sz: expected group size
 */
#if defined(CONFIG_64BIT) && SMP_CACHE_BYTES == 64
#define libeth_cacheline_group_assert(type, grp, sz)			      \
	static_assert(offsetof(type, __cacheline_group_end__##grp) -	      \
		      offsetofend(type, __cacheline_group_begin__##grp) ==    \
		      (sz))
#define __libeth_cacheline_struct_assert(type, sz)			      \
	static_assert(sizeof(type) == (sz))
#else /* !CONFIG_64BIT || SMP_CACHE_BYTES != 64 */
#define libeth_cacheline_group_assert(type, grp, sz)			      \
	static_assert(offsetof(type, __cacheline_group_end__##grp) -	      \
		      offsetofend(type, __cacheline_group_begin__##grp) <=    \
		      (sz))
#define __libeth_cacheline_struct_assert(type, sz)			      \
	static_assert(sizeof(type) <= (sz))
#endif /* !CONFIG_64BIT || SMP_CACHE_BYTES != 64 */

#define __libeth_cls1(sz1)	SMP_CACHE_ALIGN(sz1)
#define __libeth_cls2(sz1, sz2)	(SMP_CACHE_ALIGN(sz1) + SMP_CACHE_ALIGN(sz2))
#define __libeth_cls3(sz1, sz2, sz3)					      \
	(SMP_CACHE_ALIGN(sz1) + SMP_CACHE_ALIGN(sz2) + SMP_CACHE_ALIGN(sz3))
#define __libeth_cls(...)						      \
	CONCATENATE(__libeth_cls, COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

/**
 * libeth_cacheline_struct_assert - make sure CL-based struct size is expected
 * @type: type of the struct
 * @...: from 1 to 3 CL group sizes (read-mostly, read-write, cold)
 *
 * When a struct contains several CL groups, it's difficult to predict its size
 * on different architectures. The macro instead takes sizes of all of the
 * groups the structure contains and generates the final struct size.
 */
#define libeth_cacheline_struct_assert(type, ...)			      \
	__libeth_cacheline_struct_assert(type, __libeth_cls(__VA_ARGS__));    \
	static_assert(__alignof(type) >= SMP_CACHE_BYTES)

/**
 * libeth_cacheline_set_assert - make sure CL-based struct layout is expected
 * @type: type of the struct
 * @ro: expected size of the read-mostly group
 * @rw: expected size of the read-write group
 * @c: expected size of the cold group
 *
 * Check that each group size is expected and then do final struct size check.
 */
#define libeth_cacheline_set_assert(type, ro, rw, c)			      \
	libeth_cacheline_group_assert(type, read_mostly, ro);		      \
	libeth_cacheline_group_assert(type, read_write, rw);		      \
	libeth_cacheline_group_assert(type, cold, c);			      \
	libeth_cacheline_struct_assert(type, ro, rw, c)

#endif /* __LIBETH_CACHE_H */
