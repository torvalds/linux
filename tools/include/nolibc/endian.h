/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Byte order conversion for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_ENDIAN_H
#define _NOLIBC_ENDIAN_H

#include "stdint.h"

#include <asm/byteorder.h>

#define htobe16(_x) __cpu_to_be16(_x)
#define htole16(_x) __cpu_to_le16(_x)
#define be16toh(_x) __be16_to_cpu(_x)
#define le16toh(_x) __le16_to_cpu(_x)

#define htobe32(_x) __cpu_to_be32(_x)
#define htole32(_x) __cpu_to_le32(_x)
#define be32toh(_x) __be32_to_cpu(_x)
#define le32toh(_x) __le32_to_cpu(_x)

#define htobe64(_x) __cpu_to_be64(_x)
#define htole64(_x) __cpu_to_le64(_x)
#define be64toh(_x) __be64_to_cpu(_x)
#define le64toh(_x) __le64_to_cpu(_x)

#endif /* _NOLIBC_ENDIAN_H */
