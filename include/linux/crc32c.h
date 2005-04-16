#ifndef _LINUX_CRC32C_H
#define _LINUX_CRC32C_H

#include <linux/types.h>

extern u32 crc32c_le(u32 crc, unsigned char const *address, size_t length);
extern u32 crc32c_be(u32 crc, unsigned char const *address, size_t length);

#define crc32c(seed, data, length)  crc32c_le(seed, (unsigned char const *)data, length)

#endif	/* _LINUX_CRC32C_H */
