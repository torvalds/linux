/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CRC_T10DIF_H
#define _LINUX_CRC_T10DIF_H

#include <linux/types.h>

#define CRC_T10DIF_DIGEST_SIZE 2
#define CRC_T10DIF_BLOCK_SIZE 1
#define CRC_T10DIF_STRING "crct10dif"

extern __u16 crc_t10dif_generic(__u16 crc, const unsigned char *buffer,
				size_t len);
extern __u16 crc_t10dif(unsigned char const *, size_t);
extern __u16 crc_t10dif_update(__u16 crc, unsigned char const *, size_t);

#endif
