// SPDX-License-Identifier: GPL-2.0

#include <asm/barrier.h>
#include <asm/rwonce.h>
#include <linux/atomic.h>

__rust_helper s8 rust_helper_atomic_i8_read(s8 *ptr)
{
	return READ_ONCE(*ptr);
}

__rust_helper s8 rust_helper_atomic_i8_read_acquire(s8 *ptr)
{
	return smp_load_acquire(ptr);
}

__rust_helper s16 rust_helper_atomic_i16_read(s16 *ptr)
{
	return READ_ONCE(*ptr);
}

__rust_helper s16 rust_helper_atomic_i16_read_acquire(s16 *ptr)
{
	return smp_load_acquire(ptr);
}

__rust_helper void rust_helper_atomic_i8_set(s8 *ptr, s8 val)
{
	WRITE_ONCE(*ptr, val);
}

__rust_helper void rust_helper_atomic_i8_set_release(s8 *ptr, s8 val)
{
	smp_store_release(ptr, val);
}

__rust_helper void rust_helper_atomic_i16_set(s16 *ptr, s16 val)
{
	WRITE_ONCE(*ptr, val);
}

__rust_helper void rust_helper_atomic_i16_set_release(s16 *ptr, s16 val)
{
	smp_store_release(ptr, val);
}

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
