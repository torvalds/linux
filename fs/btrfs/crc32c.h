#ifndef __BTRFS_CRC32C__
#define __BTRFS_CRC32C__
#include <asm/byteorder.h>
#include <linux/crc32c.h>
#include <linux/version.h>

/* #define CONFIG_BTRFS_HW_SUM 1 */

#ifdef CONFIG_BTRFS_HW_SUM
#ifdef CONFIG_X86
/*
 * Using hardware provided CRC32 instruction to accelerate the CRC32 disposal.
 * CRC32C polynomial:0x1EDC6F41(BE)/0x82F63B78(LE)
 * CRC32 is a new instruction in Intel SSE4.2, the reference can be found at:
 * http://www.intel.com/products/processor/manuals/
 * Intel(R) 64 and IA-32 Architectures Software Developer's Manual
 * Volume 2A: Instruction Set Reference, A-M
 */

#include <asm/cpufeature.h>
#include <asm/processor.h>

#define X86_FEATURE_XMM4_2     (4*32+20) /* Streaming SIMD Extensions-4.2 */
#define cpu_has_xmm4_2         boot_cpu_has(X86_FEATURE_XMM4_2)

#ifdef CONFIG_X86_64
#define REX_PRE	"0x48, "
#define SCALE_F	8
#else
#define REX_PRE
#define SCALE_F	4
#endif

static inline u32 btrfs_crc32c_le_hw_byte(u32 crc, unsigned char const *data,
				   size_t length)
{
	while (length--) {
		__asm__ __volatile__(
			".byte 0xf2, 0xf, 0x38, 0xf0, 0xf1"
			:"=S"(crc)
			:"0"(crc), "c"(*data)
		);
		data++;
	}

	return crc;
}

static inline u32 __pure btrfs_crc32c_le_hw(u32 crc, unsigned char const *p,
				     size_t len)
{
	unsigned int iquotient = len / SCALE_F;
	unsigned int iremainder = len % SCALE_F;
#ifdef CONFIG_X86_64
	u64 *ptmp = (u64 *)p;
#else
	u32 *ptmp = (u32 *)p;
#endif

	while (iquotient--) {
		__asm__ __volatile__(
			".byte 0xf2, " REX_PRE "0xf, 0x38, 0xf1, 0xf1;"
			:"=S"(crc)
			:"0"(crc), "c"(*ptmp)
		);
		ptmp++;
	}

	if (iremainder)
		crc = btrfs_crc32c_le_hw_byte(crc, (unsigned char *)ptmp,
					      iremainder);

	return crc;
}
#endif /* CONFIG_BTRFS_HW_SUM */

static inline u32 __btrfs_crc32c(u32 crc, unsigned char const *address,
				 size_t len)
{
#ifdef CONFIG_BTRFS_HW_SUM
	if (cpu_has_xmm4_2)
		return btrfs_crc32c_le_hw(crc, address, len);
#endif
	return crc32c_le(crc, address, len);
}

#else

#define __btrfs_crc32c(seed, data, length) crc32c(seed, data, length)

#endif /* CONFIG_X86 */

/**
 * implementation of crc32c_le() changed in linux-2.6.23,
 * has of v0.13 btrfs-progs is using the latest version.
 * We must workaround older implementations of crc32c_le()
 * found on older kernel versions.
 */
#define btrfs_crc32c(seed, data, length) \
	__btrfs_crc32c(seed, (unsigned char const *)data, length)
#endif

