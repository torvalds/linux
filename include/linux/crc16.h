/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	crc16.h - CRC-16 routine
 *
 * Implements the standard CRC-16:
 *   Width 16
 *   Poly  0x8005 (x^16 + x^15 + x^2 + 1)
 *   Init  0
 *
 * Copyright (c) 2005 Ben Gardner <bgardner@wabtec.com>
 */

#ifndef __CRC16_H
#define __CRC16_H

#include <linux/types.h>

u16 crc16(u16 crc, const u8 *p, size_t len);

#endif /* __CRC16_H */

