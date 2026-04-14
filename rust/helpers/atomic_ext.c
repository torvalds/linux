// SPDX-License-Identifier: GPL-2.0

#include <asm/barrier.h>
#include <asm/rwonce.h>
#include <linux/atomic.h>

#define GEN_READ_HELPER(tname, type)						\
__rust_helper type rust_helper_atomic_##tname##_read(type *ptr)			\
{										\
	return READ_ONCE(*ptr);							\
}

#define GEN_SET_HELPER(tname, type)						\
__rust_helper void rust_helper_atomic_##tname##_set(type *ptr, type val)	\
{										\
	WRITE_ONCE(*ptr, val);							\
}

#define GEN_READ_ACQUIRE_HELPER(tname, type)					\
__rust_helper type rust_helper_atomic_##tname##_read_acquire(type *ptr)		\
{										\
	return smp_load_acquire(ptr);						\
}

#define GEN_SET_RELEASE_HELPER(tname, type)					\
__rust_helper void rust_helper_atomic_##tname##_set_release(type *ptr, type val)\
{										\
	smp_store_release(ptr, val);						\
}

#define GEN_READ_SET_HELPERS(tname, type)					\
	GEN_READ_HELPER(tname, type)						\
	GEN_SET_HELPER(tname, type)						\
	GEN_READ_ACQUIRE_HELPER(tname, type)					\
	GEN_SET_RELEASE_HELPER(tname, type)					\

GEN_READ_SET_HELPERS(i8, s8)
GEN_READ_SET_HELPERS(i16, s16)
GEN_READ_SET_HELPERS(ptr, const void *)

/*
 * xchg helpers depend on ARCH_SUPPORTS_ATOMIC_RMW and on the
 * architecture provding xchg() support for i8 and i16.
 *
 * The architectures that currently support Rust (x86_64, armv7,
 * arm64, riscv, and loongarch) satisfy these requirements.
 */
#define GEN_XCHG_HELPER(tname, type, suffix)					\
__rust_helper type								\
rust_helper_atomic_##tname##_xchg##suffix(type *ptr, type new)			\
{										\
	return xchg##suffix(ptr, new);					\
}

#define GEN_XCHG_HELPERS(tname, type)						\
	GEN_XCHG_HELPER(tname, type, )						\
	GEN_XCHG_HELPER(tname, type, _acquire)					\
	GEN_XCHG_HELPER(tname, type, _release)					\
	GEN_XCHG_HELPER(tname, type, _relaxed)					\

GEN_XCHG_HELPERS(i8, s8)
GEN_XCHG_HELPERS(i16, s16)
GEN_XCHG_HELPERS(ptr, const void *)

/*
 * try_cmpxchg helpers depend on ARCH_SUPPORTS_ATOMIC_RMW and on the
 * architecture provding try_cmpxchg() support for i8 and i16.
 *
 * The architectures that currently support Rust (x86_64, armv7,
 * arm64, riscv, and loongarch) satisfy these requirements.
 */
#define GEN_TRY_CMPXCHG_HELPER(tname, type, suffix)				\
__rust_helper bool								\
rust_helper_atomic_##tname##_try_cmpxchg##suffix(type *ptr, type *old, type new)\
{										\
	return try_cmpxchg##suffix(ptr, old, new);				\
}

#define GEN_TRY_CMPXCHG_HELPERS(tname, type)					\
	GEN_TRY_CMPXCHG_HELPER(tname, type, )					\
	GEN_TRY_CMPXCHG_HELPER(tname, type, _acquire)				\
	GEN_TRY_CMPXCHG_HELPER(tname, type, _release)				\
	GEN_TRY_CMPXCHG_HELPER(tname, type, _relaxed)				\

GEN_TRY_CMPXCHG_HELPERS(i8, s8)
GEN_TRY_CMPXCHG_HELPERS(i16, s16)
GEN_TRY_CMPXCHG_HELPERS(ptr, const void *)
