/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *	crc-itu-t.h - CRC ITU-T V.41 routine
 *
 * Implements the standard CRC ITU-T V.41:
 *   Width 16
 *   Poly  0x1021 (x^16 + x^12 + x^15 + 1)
 *   Init  0
 */

#ifndef CRC_ITU_T_H
#define CRC_ITU_T_H

#include <linux/types.h>

extern u16 const crc_itu_t_table[256];

extern u16 crc_itu_t(u16 crc, const u8 *buffer, size_t len);

static inline u16 crc_itu_t_byte(u16 crc, const u8 data)
{
	return (crc << 8) ^ crc_itu_t_table[((crc >> 8) ^ data) & 0xff];
}

#endif /* CRC_ITU_T_H */

