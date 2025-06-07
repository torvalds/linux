/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _CRC32_VX_S390_H
#define _CRC32_VX_S390_H

#include <linux/types.h>

u32 crc32_be_vgfm_16(u32 crc, unsigned char const *buf, size_t size);
u32 crc32_le_vgfm_16(u32 crc, unsigned char const *buf, size_t size);
u32 crc32c_le_vgfm_16(u32 crc, unsigned char const *buf, size_t size);

#endif /* _CRC32_VX_S390_H */
