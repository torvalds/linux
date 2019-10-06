/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 */

#ifndef _MESON_SM_FW_H_
#define _MESON_SM_FW_H_

enum {
	SM_EFUSE_READ,
	SM_EFUSE_WRITE,
	SM_EFUSE_USER_MAX,
	SM_GET_CHIP_ID,
};

struct meson_sm_firmware;

int meson_sm_call(unsigned int cmd_index, u32 *ret, u32 arg0, u32 arg1,
		  u32 arg2, u32 arg3, u32 arg4);
int meson_sm_call_write(void *buffer, unsigned int b_size, unsigned int cmd_index,
			u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4);
int meson_sm_call_read(void *buffer, unsigned int bsize, unsigned int cmd_index,
		       u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4);

#endif /* _MESON_SM_FW_H_ */
