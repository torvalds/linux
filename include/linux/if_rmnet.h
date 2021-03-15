/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_IF_RMNET_H_
#define _LINUX_IF_RMNET_H_

struct rmnet_map_header {
	u8 flags;			/* MAP_CMD_FLAG, MAP_PAD_LEN_MASK */
	u8 mux_id;
	__be16 pkt_len;			/* Length of packet, including pad */
}  __aligned(1);

/* rmnet_map_header flags field:
 *  PAD_LEN:	number of pad bytes following packet data
 *  CMD:	1 = packet contains a MAP command; 0 = packet contains data
 */
#define MAP_PAD_LEN_MASK		GENMASK(5, 0)
#define MAP_CMD_FLAG			BIT(7)

struct rmnet_map_dl_csum_trailer {
	u8 reserved1;
	u8 flags;			/* MAP_CSUM_DL_VALID_FLAG */
	__be16 csum_start_offset;
	__be16 csum_length;
	__be16 csum_value;
} __aligned(1);

/* rmnet_map_dl_csum_trailer flags field:
 *  VALID:	1 = checksum and length valid; 0 = ignore them
 */
#define MAP_CSUM_DL_VALID_FLAG		BIT(0)

struct rmnet_map_ul_csum_header {
	__be16 csum_start_offset;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u16 csum_insert_offset:14;
	u16 udp_ind:1;
	u16 csum_enabled:1;
#elif defined (__BIG_ENDIAN_BITFIELD)
	u16 csum_enabled:1;
	u16 udp_ind:1;
	u16 csum_insert_offset:14;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
} __aligned(1);

#endif /* !(_LINUX_IF_RMNET_H_) */
