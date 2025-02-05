/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Emulated 1-byte and 2-byte cmpxchg operations for architectures
 * lacking direct support for these sizes.  These are implemented in terms
 * of 4-byte cmpxchg operations.
 *
 * Copyright (C) 2024 Paul E. McKenney.
 */

#ifndef __LINUX_CMPXCHG_EMU_H
#define __LINUX_CMPXCHG_EMU_H

uintptr_t cmpxchg_emu_u8(volatile u8 *p, uintptr_t old, uintptr_t new);

#endif /* __LINUX_CMPXCHG_EMU_H */
