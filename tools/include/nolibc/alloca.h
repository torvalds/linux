/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * alloca() for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_ALLOCA_H
#define _NOLIBC_ALLOCA_H

#define alloca(size) __builtin_alloca(size)

#endif /* _NOLIBC_ALLOCA_H */
