/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CRC7_H
#define _LINUX_CRC7_H
#include <linux/types.h>

extern u8 crc7_be(u8 crc, const u8 *buffer, size_t len);

#endif
