// SPDX-License-Identifier: GPL-2.0+
/*
 * Emulated 1-byte and 2-byte cmpxchg operations for architectures lacking
 * direct support for these sizes.  These are implemented in terms of
 * 4-byte cmpxchg operations.
 *
 * Copyright (C) 2024 Paul E. McKenney <paulmck@kernel.org>
 * Copyright (C) 2026 Bradley Morgan <brads@mainlining.org>
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

union u16_32 {
	u16 h[2];
	u32 w;
};

/* Emulate two-byte cmpxchg() in terms of 4-byte cmpxchg. */
unsigned long cmpxchg_emu_u16(volatile u16 *p, unsigned long old, unsigned long new)
{
	u32 *p32 = (u32 *)(((uintptr_t)p) & ~0x3);
	int i = (((uintptr_t)p) & 0x2) / 2;
	union u16_32 old32;
	union u16_32 new32;
	u32 ret;

	WARN_ON_ONCE(((uintptr_t)p) & 0x1);
	ret = READ_ONCE(*p32);
	do {
		old32.w = ret;
		if (old32.h[i] != (u16)old)
			return (unsigned long)old32.h[i];
		new32.w = old32.w;
		new32.h[i] = (u16)new;
		instrument_atomic_read_write(p, 2);
		ret = data_race(cmpxchg(p32, old32.w, new32.w)); // Overridden above.
	} while (ret != old32.w);
	return (u16)old;
}
EXPORT_SYMBOL_GPL(cmpxchg_emu_u16);
