/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_TYPES_32_64_H
#define _UAPI_LINUX_TYPES_32_64_H

/*
 * linux/types_32_64.h
 *
 * Integer type declaration for pointers across 32-bit and 64-bit systems.
 *
 * Copyright (c) 2015-2018 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

#ifdef __KERNEL__
# include <linux/types.h>
#else
# include <stdint.h>
#endif

#include <asm/byteorder.h>

#ifdef __BYTE_ORDER
# if (__BYTE_ORDER == __BIG_ENDIAN)
#  define LINUX_BYTE_ORDER_BIG_ENDIAN
# else
#  define LINUX_BYTE_ORDER_LITTLE_ENDIAN
# endif
#else
# ifdef __BIG_ENDIAN
#  define LINUX_BYTE_ORDER_BIG_ENDIAN
# else
#  define LINUX_BYTE_ORDER_LITTLE_ENDIAN
# endif
#endif

#ifdef __LP64__
# define LINUX_FIELD_u32_u64(field)			__u64 field
# define LINUX_FIELD_u32_u64_INIT_ONSTACK(field, v)	field = (intptr_t)v
#else
# ifdef LINUX_BYTE_ORDER_BIG_ENDIAN
#  define LINUX_FIELD_u32_u64(field)	__u32 field ## _padding, field
#  define LINUX_FIELD_u32_u64_INIT_ONSTACK(field, v)	\
	field ## _padding = 0, field = (intptr_t)v
# else
#  define LINUX_FIELD_u32_u64(field)	__u32 field, field ## _padding
#  define LINUX_FIELD_u32_u64_INIT_ONSTACK(field, v)	\
	field = (intptr_t)v, field ## _padding = 0
# endif
#endif

#endif /* _UAPI_LINUX_TYPES_32_64_H */
