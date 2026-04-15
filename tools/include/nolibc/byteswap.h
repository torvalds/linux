/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Byte swapping for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_BYTESWAP_H
#define _NOLIBC_BYTESWAP_H

#include "stdint.h"

#include <linux/swab.h>

#define bswap_16(_x) __swab16(_x)
#define bswap_32(_x) __swab32(_x)
#define bswap_64(_x) __swab64(_x)

#endif /* _NOLIBC_BYTESWAP_H */
