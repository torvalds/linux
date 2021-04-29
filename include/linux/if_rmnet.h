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
	__be16 csum_info;		/* MAP_CSUM_UL_* */
} __aligned(1);

/* csum_info field:
 *  OFFSET:	where (offset in bytes) to insert computed checksum
 *  UDP:	1 = UDP checksum (zero checkum means no checksum)
 *  ENABLED:	1 = checksum computation requested
 */
#define MAP_CSUM_UL_OFFSET_MASK		GENMASK(13, 0)
#define MAP_CSUM_UL_UDP_FLAG		BIT(14)
#define MAP_CSUM_UL_ENABLED_FLAG	BIT(15)

#endif /* !(_LINUX_IF_RMNET_H_) */
