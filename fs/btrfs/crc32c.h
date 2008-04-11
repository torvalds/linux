#include <asm/byteorder.h>
#include <linux/crc32c.h>
#include <linux/version.h>

/**
 * implementation of crc32c_le() changed in linux-2.6.23,
 * has of v0.13 btrfs-progs is using the latest version.
 * We must workaround older implementations of crc32c_le()
 * found on older kernel versions.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
#define btrfs_crc32c(seed, data, length) \
	__cpu_to_le32( crc32c( __le32_to_cpu(seed), data, length) )
#else
#define btrfs_crc32c(seed, data, length) \
	crc32c(seed, data, length)
#endif
