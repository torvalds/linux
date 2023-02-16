/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_DLOAD_MODE_H__
#define __QCOM_DLOAD_MODE_H__

#if IS_ENABLED(CONFIG_POWER_RESET_QCOM_DOWNLOAD_MODE)
extern int get_dump_mode(void);
#else
static inline int get_dump_mode(void)
{
	return -ENOENT;
}
#endif
#endif
