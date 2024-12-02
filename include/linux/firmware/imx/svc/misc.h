/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017~2018 NXP
 *
 * Header file containing the public API for the System Controller (SC)
 * Miscellaneous (MISC) function.
 *
 * MISC_SVC (SVC) Miscellaneous Service
 *
 * Module for the Miscellaneous (MISC) service.
 */

#ifndef _SC_MISC_API_H
#define _SC_MISC_API_H

#include <linux/firmware/imx/sci.h>

/*
 * This type is used to indicate RPC MISC function calls.
 */
enum imx_misc_func {
	IMX_SC_MISC_FUNC_UNKNOWN = 0,
	IMX_SC_MISC_FUNC_SET_CONTROL = 1,
	IMX_SC_MISC_FUNC_GET_CONTROL = 2,
	IMX_SC_MISC_FUNC_SET_MAX_DMA_GROUP = 4,
	IMX_SC_MISC_FUNC_SET_DMA_GROUP = 5,
	IMX_SC_MISC_FUNC_SECO_IMAGE_LOAD = 8,
	IMX_SC_MISC_FUNC_SECO_AUTHENTICATE = 9,
	IMX_SC_MISC_FUNC_DEBUG_OUT = 10,
	IMX_SC_MISC_FUNC_WAVEFORM_CAPTURE = 6,
	IMX_SC_MISC_FUNC_BUILD_INFO = 15,
	IMX_SC_MISC_FUNC_UNIQUE_ID = 19,
	IMX_SC_MISC_FUNC_SET_ARI = 3,
	IMX_SC_MISC_FUNC_BOOT_STATUS = 7,
	IMX_SC_MISC_FUNC_BOOT_DONE = 14,
	IMX_SC_MISC_FUNC_OTP_FUSE_READ = 11,
	IMX_SC_MISC_FUNC_OTP_FUSE_WRITE = 17,
	IMX_SC_MISC_FUNC_SET_TEMP = 12,
	IMX_SC_MISC_FUNC_GET_TEMP = 13,
	IMX_SC_MISC_FUNC_GET_BOOT_DEV = 16,
	IMX_SC_MISC_FUNC_GET_BUTTON_STATUS = 18,
};

/*
 * Control Functions
 */

#ifdef CONFIG_IMX_SCU
int imx_sc_misc_set_control(struct imx_sc_ipc *ipc, u32 resource,
			    u8 ctrl, u32 val);

int imx_sc_misc_get_control(struct imx_sc_ipc *ipc, u32 resource,
			    u8 ctrl, u32 *val);

int imx_sc_pm_cpu_start(struct imx_sc_ipc *ipc, u32 resource,
			bool enable, u64 phys_addr);
#else
static inline int imx_sc_misc_set_control(struct imx_sc_ipc *ipc,
					  u32 resource, u8 ctrl, u32 val)
{
	return -ENOTSUPP;
}

static inline int imx_sc_misc_get_control(struct imx_sc_ipc *ipc,
					  u32 resource, u8 ctrl, u32 *val)
{
	return -ENOTSUPP;
}

static inline int imx_sc_pm_cpu_start(struct imx_sc_ipc *ipc, u32 resource,
				      bool enable, u64 phys_addr)
{
	return -ENOTSUPP;
}
#endif
#endif /* _SC_MISC_API_H */
