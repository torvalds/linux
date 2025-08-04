/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Shim elf.h header for NOLIBC.
 * Copyright (C) 2025 Thomas Weißschuh <thomas.weissschuh@linutronix.de>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_SYS_ELF_H
#define _NOLIBC_SYS_ELF_H

#include <linux/elf.h>

#endif /* _NOLIBC_SYS_ELF_H */
