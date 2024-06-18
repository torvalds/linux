/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 */

#ifndef __LPASS_MACRO_COMMON_H__
#define __LPASS_MACRO_COMMON_H__

/* NPL clock is expected */
#define LPASS_MACRO_FLAG_HAS_NPL_CLOCK		BIT(0)
/* The soundwire block should be internally reset at probe */
#define LPASS_MACRO_FLAG_RESET_SWR		BIT(1)

enum lpass_version {
	LPASS_VER_9_0_0,
	LPASS_VER_9_2_0,
	LPASS_VER_10_0_0,
	LPASS_VER_11_0_0,
};

struct lpass_macro {
	struct device *macro_pd;
	struct device *dcodec_pd;
};

struct lpass_macro *lpass_macro_pds_init(struct device *dev);
void lpass_macro_pds_exit(struct lpass_macro *pds);

#endif /* __LPASS_MACRO_COMMON_H__ */
