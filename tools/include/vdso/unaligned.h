/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_UNALIGNED_H
#define __VDSO_UNALIGNED_H

#define __get_unaligned_t(type, ptr) ({						\
	const struct { type x; } __packed *__pptr = (typeof(__pptr))(ptr);	\
	__pptr->x;								\
})

#define __put_unaligned_t(type, val, ptr) do {					\
	struct { type x; } __packed *__pptr = (typeof(__pptr))(ptr);		\
	__pptr->x = (val);							\
} while (0)

#endif /* __VDSO_UNALIGNED_H */
