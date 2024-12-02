/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CRC_T10DIF_H
#define _LINUX_CRC_T10DIF_H

#include <linux/types.h>

#define CRC_T10DIF_DIGEST_SIZE 2
#define CRC_T10DIF_BLOCK_SIZE 1

u16 crc_t10dif_generic(u16 crc, const u8 *p, size_t len);

static inline u16 crc_t10dif_update(u16 crc, const u8 *p, size_t len)
{
	return crc_t10dif_generic(crc, p, len);
}

static inline u16 crc_t10dif(const u8 *p, size_t len)
{
	return crc_t10dif_update(0, p, len);
}

#endif
