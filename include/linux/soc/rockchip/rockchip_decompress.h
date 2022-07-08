/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd */

#ifndef _ROCKCHIP_DECOMPRESS
#define _ROCKCHIP_DECOMPRESS

enum decom_mod {
	LZ4_MOD,
	GZIP_MOD,
	ZLIB_MOD,
};

/* The high 16 bits indicate whether decompression is non-blocking */
#define DECOM_NOBLOCKING		(0x00010000)

static inline u32 rk_get_decom_mode(u32 mode)
{
	return mode & 0x0000ffff;
}

static inline bool rk_get_noblocking_flag(u32 mode)
{
	return !!(mode & DECOM_NOBLOCKING);
}

#ifdef CONFIG_ROCKCHIP_HW_DECOMPRESS
int rk_decom_start(u32 mode, phys_addr_t src, phys_addr_t dst, u32 dst_max_size);
/* timeout in seconds */
int rk_decom_wait_done(u32 timeout, u64 *decom_len);
#else
static inline int rk_decom_start(u32 mode, phys_addr_t src, phys_addr_t dst, u32 dst_max_size)
{
	return -EINVAL;
}

static inline int rk_decom_wait_done(u32 timeout, u64 *decom_len)
{
	return -EINVAL;
}
#endif

#endif
