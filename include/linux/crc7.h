#ifndef _LINUX_CRC7_H
#define _LINUX_CRC7_H
#include <linux/types.h>

extern const u8 crc7_be_syndrome_table[256];

static inline u8 crc7_be_byte(u8 crc, u8 data)
{
	return crc7_be_syndrome_table[crc ^ data];
}

extern u8 crc7_be(u8 crc, const u8 *buffer, size_t len);

#endif
