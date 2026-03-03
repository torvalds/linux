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

/*
 * xchg helpers depend on ARCH_SUPPORTS_ATOMIC_RMW and on the
 * architecture provding xchg() support for i8 and i16.
 *
 * The architectures that currently support Rust (x86_64, armv7,
 * arm64, riscv, and loongarch) satisfy these requirements.
 */
__rust_helper s8 rust_helper_atomic_i8_xchg(s8 *ptr, s8 new)
{
	return xchg(ptr, new);
}

__rust_helper s16 rust_helper_atomic_i16_xchg(s16 *ptr, s16 new)
{
	return xchg(ptr, new);
}

__rust_helper s8 rust_helper_atomic_i8_xchg_acquire(s8 *ptr, s8 new)
{
	return xchg_acquire(ptr, new);
}

__rust_helper s16 rust_helper_atomic_i16_xchg_acquire(s16 *ptr, s16 new)
{
	return xchg_acquire(ptr, new);
}

__rust_helper s8 rust_helper_atomic_i8_xchg_release(s8 *ptr, s8 new)
{
	return xchg_release(ptr, new);
}

__rust_helper s16 rust_helper_atomic_i16_xchg_release(s16 *ptr, s16 new)
{
	return xchg_release(ptr, new);
}

__rust_helper s8 rust_helper_atomic_i8_xchg_relaxed(s8 *ptr, s8 new)
{
	return xchg_relaxed(ptr, new);
}

__rust_helper s16 rust_helper_atomic_i16_xchg_relaxed(s16 *ptr, s16 new)
{
	return xchg_relaxed(ptr, new);
}

/*
 * try_cmpxchg helpers depend on ARCH_SUPPORTS_ATOMIC_RMW and on the
 * architecture provding try_cmpxchg() support for i8 and i16.
 *
 * The architectures that currently support Rust (x86_64, armv7,
 * arm64, riscv, and loongarch) satisfy these requirements.
 */
__rust_helper bool rust_helper_atomic_i8_try_cmpxchg(s8 *ptr, s8 *old, s8 new)
{
	return try_cmpxchg(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i16_try_cmpxchg(s16 *ptr, s16 *old, s16 new)
{
	return try_cmpxchg(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i8_try_cmpxchg_acquire(s8 *ptr, s8 *old, s8 new)
{
	return try_cmpxchg_acquire(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i16_try_cmpxchg_acquire(s16 *ptr, s16 *old, s16 new)
{
	return try_cmpxchg_acquire(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i8_try_cmpxchg_release(s8 *ptr, s8 *old, s8 new)
{
	return try_cmpxchg_release(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i16_try_cmpxchg_release(s16 *ptr, s16 *old, s16 new)
{
	return try_cmpxchg_release(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i8_try_cmpxchg_relaxed(s8 *ptr, s8 *old, s8 new)
{
	return try_cmpxchg_relaxed(ptr, old, new);
}

__rust_helper bool rust_helper_atomic_i16_try_cmpxchg_relaxed(s16 *ptr, s16 *old, s16 new)
{
	return try_cmpxchg_relaxed(ptr, old, new);
}
