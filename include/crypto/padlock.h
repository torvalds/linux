/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for VIA PadLock
 *
 * Copyright (c) 2004 Michal Ludvig <michal@logix.cz>
 */

#ifndef _CRYPTO_PADLOCK_H
#define _CRYPTO_PADLOCK_H

#define PADLOCK_ALIGNMENT 16

#define PFX	KBUILD_MODNAME ": "

#define PADLOCK_CRA_PRIORITY	300
#define PADLOCK_COMPOSITE_PRIORITY 400

#ifdef CONFIG_64BIT
#define STACK_ALIGN 16
#else
#define STACK_ALIGN 4
#endif

#endif	/* _CRYPTO_PADLOCK_H */
