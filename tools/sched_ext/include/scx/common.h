/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 */
#ifndef __SCHED_EXT_COMMON_H
#define __SCHED_EXT_COMMON_H

#ifdef __KERNEL__
#error "Should not be included by BPF programs"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define SCX_BUG(__fmt, ...)							\
	do {									\
		fprintf(stderr, "[SCX_BUG] %s:%d", __FILE__, __LINE__);		\
		if (errno)							\
			fprintf(stderr, " (%s)\n", strerror(errno));		\
		else								\
			fprintf(stderr, "\n");					\
		fprintf(stderr, __fmt __VA_OPT__(,) __VA_ARGS__);		\
		fprintf(stderr, "\n");						\
										\
		exit(EXIT_FAILURE);						\
	} while (0)

#define SCX_BUG_ON(__cond, __fmt, ...)					\
	do {								\
		if (__cond)						\
			SCX_BUG((__fmt) __VA_OPT__(,) __VA_ARGS__);	\
	} while (0)

/**
 * RESIZE_ARRAY - Convenience macro for resizing a BPF array
 * @__skel: the skeleton containing the array
 * @elfsec: the data section of the BPF program in which the array exists
 * @arr: the name of the array
 * @n: the desired array element count
 *
 * For BPF arrays declared with RESIZABLE_ARRAY(), this macro performs two
 * operations. It resizes the map which corresponds to the custom data
 * section that contains the target array. As a side effect, the BTF info for
 * the array is adjusted so that the array length is sized to cover the new
 * data section size. The second operation is reassigning the skeleton pointer
 * for that custom data section so that it points to the newly memory mapped
 * region.
 */
#define RESIZE_ARRAY(__skel, elfsec, arr, n)						\
	do {										\
		size_t __sz;								\
		bpf_map__set_value_size((__skel)->maps.elfsec##_##arr,			\
				sizeof((__skel)->elfsec##_##arr->arr[0]) * (n));	\
		(__skel)->elfsec##_##arr =						\
			bpf_map__initial_value((__skel)->maps.elfsec##_##arr, &__sz);	\
	} while (0)

#include "user_exit_info.h"
#include "compat.h"

#endif	/* __SCHED_EXT_COMMON_H */
