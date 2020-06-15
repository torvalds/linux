/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd */

#ifndef _ROCKCHIP_DECOMPRESS
#define _ROCKCHIP_DECOMPRESS

enum decom_mod {
	LZ4_MOD,
	GZIP_MOD,
	ZLIB_MOD,
};

#ifdef CONFIG_ROCKCHIP_HW_DECOMPRESS
int rk_decom_start(u32 mode, phys_addr_t src, phys_addr_t dst, u32 dst_max_size);
#else
static inline int rk_decom_start(u32 mode, phys_addr_t src, phys_addr_t dst, u32 dst_max_size)
{
	return -EINVAL;
}
#endif

#endif
