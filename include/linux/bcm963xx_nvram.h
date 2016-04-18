#ifndef __LINUX_BCM963XX_NVRAM_H__
#define __LINUX_BCM963XX_NVRAM_H__

#include <linux/crc32.h>
#include <linux/if_ether.h>
#include <linux/sizes.h>
#include <linux/types.h>

/*
 * Broadcom BCM963xx SoC board nvram data structure.
 *
 * The nvram structure varies in size depending on the SoC board version. Use
 * the appropriate minimum BCM963XX_NVRAM_*_SIZE define for the information
 * you need instead of sizeof(struct bcm963xx_nvram) as this may change.
 */

#define BCM963XX_NVRAM_V4_SIZE		300
#define BCM963XX_NVRAM_V5_SIZE		(1 * SZ_1K)

#define BCM963XX_DEFAULT_PSI_SIZE	64

enum bcm963xx_nvram_nand_part {
	BCM963XX_NVRAM_NAND_PART_BOOT = 0,
	BCM963XX_NVRAM_NAND_PART_ROOTFS_1,
	BCM963XX_NVRAM_NAND_PART_ROOTFS_2,
	BCM963XX_NVRAM_NAND_PART_DATA,
	BCM963XX_NVRAM_NAND_PART_BBT,

	__BCM963XX_NVRAM_NAND_NR_PARTS
};

struct bcm963xx_nvram {
	u32	version;
	char	bootline[256];
	char	name[16];
	u32	main_tp_number;
	u32	psi_size;
	u32	mac_addr_count;
	u8	mac_addr_base[ETH_ALEN];
	u8	__reserved1[2];
	u32	checksum_v4;

	u8	__reserved2[292];
	u32	nand_part_offset[__BCM963XX_NVRAM_NAND_NR_PARTS];
	u32	nand_part_size[__BCM963XX_NVRAM_NAND_NR_PARTS];
	u8	__reserved3[388];
	u32	checksum_v5;
};

#define BCM963XX_NVRAM_NAND_PART_OFFSET(nvram, part) \
	bcm963xx_nvram_nand_part_offset(nvram, BCM963XX_NVRAM_NAND_PART_ ##part)

static inline u64 __pure bcm963xx_nvram_nand_part_offset(
	const struct bcm963xx_nvram *nvram,
	enum bcm963xx_nvram_nand_part part)
{
	return nvram->nand_part_offset[part] * SZ_1K;
}

#define BCM963XX_NVRAM_NAND_PART_SIZE(nvram, part) \
	bcm963xx_nvram_nand_part_size(nvram, BCM963XX_NVRAM_NAND_PART_ ##part)

static inline u64 __pure bcm963xx_nvram_nand_part_size(
	const struct bcm963xx_nvram *nvram,
	enum bcm963xx_nvram_nand_part part)
{
	return nvram->nand_part_size[part] * SZ_1K;
}

/*
 * bcm963xx_nvram_checksum - Verify nvram checksum
 *
 * @nvram: pointer to full size nvram data structure
 * @expected_out: optional pointer to store expected checksum value
 * @actual_out: optional pointer to store actual checksum value
 *
 * Return: 0 if the checksum is valid, otherwise -EINVAL
 */
static int __maybe_unused bcm963xx_nvram_checksum(
	const struct bcm963xx_nvram *nvram,
	u32 *expected_out, u32 *actual_out)
{
	u32 expected, actual;
	size_t len;

	if (nvram->version <= 4) {
		expected = nvram->checksum_v4;
		len = BCM963XX_NVRAM_V4_SIZE - sizeof(u32);
	} else {
		expected = nvram->checksum_v5;
		len = BCM963XX_NVRAM_V5_SIZE - sizeof(u32);
	}

	/*
	 * Calculate the CRC32 value for the nvram with a checksum value
	 * of 0 without modifying or copying the nvram by combining:
	 * - The CRC32 of the nvram without the checksum value
	 * - The CRC32 of a zero checksum value (which is also 0)
	 */
	actual = crc32_le_combine(
		crc32_le(~0, (u8 *)nvram, len), 0, sizeof(u32));

	if (expected_out)
		*expected_out = expected;

	if (actual_out)
		*actual_out = actual;

	return expected == actual ? 0 : -EINVAL;
};

#endif /* __LINUX_BCM963XX_NVRAM_H__ */
