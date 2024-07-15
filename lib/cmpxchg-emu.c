// SPDX-License-Identifier: GPL-2.0+
/*
 * Emulated 1-byte cmpxchg operation for architectures lacking direct
 * support for this size.  This is implemented in terms of 4-byte cmpxchg
 * operations.
 *
 * Copyright (C) 2024 Paul E. McKenney.
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/instrumented.h>
#include <linux/atomic.h>
#include <linux/panic.h>
#include <linux/bug.h>
#include <asm-generic/rwonce.h>
#include <linux/cmpxchg-emu.h>

union u8_32 {
	u8 b[4];
	u32 w;
};

/* Emulate one-byte cmpxchg() in terms of 4-byte cmpxchg. */
uintptr_t cmpxchg_emu_u8(volatile u8 *p, uintptr_t old, uintptr_t new)
{
	u32 *p32 = (u32 *)(((uintptr_t)p) & ~0x3);
	int i = ((uintptr_t)p) & 0x3;
	union u8_32 old32;
	union u8_32 new32;
	u32 ret;

	ret = READ_ONCE(*p32);
	do {
		old32.w = ret;
		if (old32.b[i] != old)
			return old32.b[i];
		new32.w = old32.w;
		new32.b[i] = new;
		instrument_atomic_read_write(p, 1);
		ret = data_race(cmpxchg(p32, old32.w, new32.w)); // Overridden above.
	} while (ret != old32.w);
	return old;
}
EXPORT_SYMBOL_GPL(cmpxchg_emu_u8);
