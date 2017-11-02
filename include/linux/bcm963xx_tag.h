/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BCM963XX_TAG_H__
#define __LINUX_BCM963XX_TAG_H__

#include <linux/types.h>

#define TAGVER_LEN		4	/* Length of Tag Version */
#define TAGLAYOUT_LEN		4	/* Length of FlashLayoutVer */
#define SIG1_LEN		20	/* Company Signature 1 Length */
#define SIG2_LEN		14	/* Company Signature 2 Length */
#define BOARDID_LEN		16	/* Length of BoardId */
#define ENDIANFLAG_LEN		2	/* Endian Flag Length */
#define CHIPID_LEN		6	/* Chip Id Length */
#define IMAGE_LEN		10	/* Length of Length Field */
#define ADDRESS_LEN		12	/* Length of Address field */
#define IMAGE_SEQUENCE_LEN	4	/* Image sequence Length */
#define RSASIG_LEN		20	/* Length of RSA Signature in tag */
#define TAGINFO1_LEN		30	/* Length of vendor information field1 in tag */
#define FLASHLAYOUTVER_LEN	4	/* Length of Flash Layout Version String tag */
#define TAGINFO2_LEN		16	/* Length of vendor information field2 in tag */
#define ALTTAGINFO_LEN		54	/* Alternate length for vendor information; Pirelli */

#define NUM_PIRELLI		2
#define IMAGETAG_CRC_START	0xFFFFFFFF

#define PIRELLI_BOARDS { \
	"AGPF-S0", \
	"DWV-S0", \
}

/* Extended flash address, needs to be subtracted
 * from bcm_tag flash image offsets.
 */
#define BCM963XX_EXTENDED_SIZE	0xBFC00000

/*
 * The broadcom firmware assumes the rootfs starts the image,
 * therefore uses the rootfs start (flash_image_address)
 * to determine where to flash the image.  Since we have the kernel first
 * we have to give it the kernel address, but the crc uses the length
 * associated with this address (root_length), which is added to the kernel
 * length (kernel_length) to determine the length of image to flash and thus
 * needs to be rootfs + deadcode (jffs2 EOF marker)
*/

struct bcm_tag {
	/* 0-3: Version of the image tag */
	char tag_version[TAGVER_LEN];
	/* 4-23: Company Line 1 */
	char sig_1[SIG1_LEN];
	/*  24-37: Company Line 2 */
	char sig_2[SIG2_LEN];
	/* 38-43: Chip this image is for */
	char chip_id[CHIPID_LEN];
	/* 44-59: Board name */
	char board_id[BOARDID_LEN];
	/* 60-61: Map endianness -- 1 BE 0 LE */
	char big_endian[ENDIANFLAG_LEN];
	/* 62-71: Total length of image */
	char total_length[IMAGE_LEN];
	/* 72-83: Address in memory of CFE */
	char cfe__address[ADDRESS_LEN];
	/* 84-93: Size of CFE */
	char cfe_length[IMAGE_LEN];
	/* 94-105: Address in memory of image start
	 * (kernel for OpenWRT, rootfs for stock firmware)
	 */
	char flash_image_start[ADDRESS_LEN];
	/* 106-115: Size of rootfs */
	char root_length[IMAGE_LEN];
	/* 116-127: Address in memory of kernel */
	char kernel_address[ADDRESS_LEN];
	/* 128-137: Size of kernel */
	char kernel_length[IMAGE_LEN];
	/* 138-141: Image sequence number
	 * (to be incremented when flashed with a new image)
	 */
	char image_sequence[IMAGE_SEQUENCE_LEN];
	/* 142-161: RSA Signature (not used; some vendors may use this) */
	char rsa_signature[RSASIG_LEN];
	/* 162-191: Compilation and related information (not used in OpenWrt) */
	char information1[TAGINFO1_LEN];
	/* 192-195: Version flash layout */
	char flash_layout_ver[FLASHLAYOUTVER_LEN];
	/* 196-199: kernel+rootfs CRC32 */
	__u32 fskernel_crc;
	/* 200-215: Unused except on Alice Gate where is is information */
	char information2[TAGINFO2_LEN];
	/* 216-219: CRC32 of image less imagetag (kernel for Alice Gate) */
	__u32 image_crc;
	/* 220-223: CRC32 of rootfs partition */
	__u32 rootfs_crc;
	/* 224-227: CRC32 of kernel partition */
	__u32 kernel_crc;
	/* 228-235: Unused at present */
	char reserved1[8];
	/* 236-239: CRC32 of header excluding last 20 bytes */
	__u32 header_crc;
	/* 240-255: Unused at present */
	char reserved2[16];
};

#endif /* __LINUX_BCM63XX_TAG_H__ */
