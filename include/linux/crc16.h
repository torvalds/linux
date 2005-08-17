/*
 *	crc16.h - CRC-16 routine
 *
 * Implements the standard CRC-16, as used with 1-wire devices:
 *   Width 16
 *   Poly  0x8005 (x^16 + x^15 + x^2 + 1)
 *   Init  0
 *
 * For 1-wire devices, the CRC is stored inverted, LSB-first
 *
 * Example buffer with the CRC attached:
 *   31 32 33 34 35 36 37 38 39 C2 44
 *
 * The CRC over a buffer with the CRC attached is 0xB001.
 * So, if (crc16(0, buf, size) == 0xB001) then the buffer is valid.
 *
 * Refer to "Application Note 937: Book of iButton Standards" for details.
 * http://www.maxim-ic.com/appnotes.cfm/appnote_number/937
 *
 * Copyright (c) 2005 Ben Gardner <bgardner@wabtec.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#ifndef __CRC16_H
#define __CRC16_H

#include <linux/types.h>

#define CRC16_INIT		0
#define CRC16_VALID		0xb001

extern u16 const crc16_table[256];

extern u16 crc16(u16 crc, const u8 *buffer, size_t len);

static inline u16 crc16_byte(u16 crc, const u8 data)
{
	return (crc >> 8) ^ crc16_table[(crc ^ data) & 0xff];
}

#endif /* __CRC16_H */

