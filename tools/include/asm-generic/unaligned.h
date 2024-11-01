/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copied from the kernel sources to tools/perf/:
 */

#ifndef __TOOLS_LINUX_ASM_GENERIC_UNALIGNED_H
#define __TOOLS_LINUX_ASM_GENERIC_UNALIGNED_H

#define __get_unaligned_t(type, ptr) ({						\
	const struct { type x; } __packed *__pptr = (typeof(__pptr))(ptr);	\
	__pptr->x;								\
})

#define __put_unaligned_t(type, val, ptr) do {					\
	struct { type x; } __packed *__pptr = (typeof(__pptr))(ptr);		\
	__pptr->x = (val);							\
} while (0)

#define get_unaligned(ptr)	__get_unaligned_t(typeof(*(ptr)), (ptr))
#define put_unaligned(val, ptr) __put_unaligned_t(typeof(*(ptr)), (val), (ptr))

#endif /* __TOOLS_LINUX_ASM_GENERIC_UNALIGNED_H */

