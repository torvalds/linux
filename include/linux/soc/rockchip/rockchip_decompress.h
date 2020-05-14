/* SPDX-License-Identifier: GPL-2.0+ */

/* Copyright (c) 2020 Fuzhou Rockchip Electronics Co., Ltd */

#ifndef _ROCKCHIP_DECOMPRESS
#define _ROCKCHIP_DECOMPRESS

enum decom_mod {
	LZ4_MOD,
	GZIP_MOD,
	ZLIB_MOD,
};

int rk_decom_start(u32 mode, phys_addr_t mem_start, phys_addr_t mem_dst, u32 limit_size);

#endif
