/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_UNALIGNED_H
#define __VDSO_UNALIGNED_H

#include <linux/compiler_types.h>

/**
 * __get_unaligned_t - read an unaligned value from memory.
 * @type:	the type to load from the pointer.
 * @ptr:	the pointer to load from.
 *
 * Use memcpy to affect an unaligned type sized load avoiding undefined behavior
 * from approaches like type punning that require -fno-strict-aliasing in order
 * to be correct. As type may be const, use __unqual_scalar_typeof to map to a
 * non-const type - you can't memcpy into a const type. The
 * __get_unaligned_ctrl_type gives __unqual_scalar_typeof its required
 * expression rather than type, a pointer is used to avoid warnings about mixing
 * the use of 0 and NULL. The void* cast silences ubsan warnings.
 */
#define __get_unaligned_t(type, ptr) ({					\
	type *__get_unaligned_ctrl_type __always_unused = NULL;		\
	__unqual_scalar_typeof(*__get_unaligned_ctrl_type) __get_unaligned_val; \
	__builtin_memcpy(&__get_unaligned_val, (void *)(ptr),		\
			 sizeof(__get_unaligned_val));			\
	__get_unaligned_val;						\
})

/**
 * __put_unaligned_t - write an unaligned value to memory.
 * @type:	the type of the value to store.
 * @val:	the value to store.
 * @ptr:	the pointer to store to.
 *
 * Use memcpy to affect an unaligned type sized store avoiding undefined
 * behavior from approaches like type punning that require -fno-strict-aliasing
 * in order to be correct. The void* cast silences ubsan warnings.
 */
#define __put_unaligned_t(type, val, ptr) do {				\
	type __put_unaligned_val = (val);				\
	__builtin_memcpy((void *)(ptr), &__put_unaligned_val,		\
			 sizeof(__put_unaligned_val));			\
} while (0)

#endif /* __VDSO_UNALIGNED_H */
