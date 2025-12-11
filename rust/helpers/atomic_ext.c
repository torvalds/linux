// SPDX-License-Identifier: GPL-2.0

#include <asm/barrier.h>
#include <asm/rwonce.h>

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
