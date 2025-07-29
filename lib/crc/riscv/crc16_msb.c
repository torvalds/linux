// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RISC-V optimized most-significant-bit-first CRC16
 *
 * Copyright 2025 Google LLC
 */

#include "crc-clmul.h"

typedef u16 crc_t;
#define LSB_CRC 0
#include "crc-clmul-template.h"

u16 crc16_msb_clmul(u16 crc, const void *p, size_t len,
		    const struct crc_clmul_consts *consts)
{
	return crc_clmul(crc, p, len, consts);
}
