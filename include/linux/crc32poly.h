/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_CRC32_POLY_H
#define _LINUX_CRC32_POLY_H

/* The polynomial used by crc32_le(), in integer form.  See crc32_le(). */
#define CRC32_POLY_LE 0xedb88320

/* The polynomial used by crc32_be(), in integer form.  See crc32_be(). */
#define CRC32_POLY_BE 0x04c11db7

/* The polynomial used by crc32c(), in integer form.  See crc32c(). */
#define CRC32C_POLY_LE 0x82f63b78

#endif /* _LINUX_CRC32_POLY_H */
