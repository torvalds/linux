/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2013-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _LINUX_IF_RMNET_H_
#define _LINUX_IF_RMNET_H_

struct rmnet_map_header {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8  pad_len:6;
	u8  reserved_bit:1;
	u8  cd_bit:1;
#elif defined (__BIG_ENDIAN_BITFIELD)
	u8  cd_bit:1;
	u8  reserved_bit:1;
	u8  pad_len:6;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	u8  mux_id;
	__be16 pkt_len;
}  __aligned(1);

struct rmnet_map_dl_csum_trailer {
	u8  reserved1;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	u8  valid:1;
	u8  reserved2:7;
#elif defined (__BIG_ENDIAN_BITFIELD)
	u8  reserved2:7;
	u8  valid:1;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__be16 csum_start_offset;
	__be16 csum_length;
	__be16 csum_value;
} __aligned(1);

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
