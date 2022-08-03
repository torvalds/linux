/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defining registers address and its bit definitions of MAX96776
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#ifndef _MFD_MAX96776_H_
#define _MFD_MAX96776_H_

#include <linux/bitfield.h>

/* 07f0h */
#define TRAINING_SUCCESSFUL	BIT(0)

/* 1700h */
#define CMD_RESET		BIT(7)

/* 6230h */
#define HPD_PRESENT		BIT(0)

/* e776h */
#define REBOOT_TRAINNING	BIT(0)
#define RUN_LINK_TRAINING	BIT(1)
#define AUX_READ		BIT(4)
#define AUX_WRITE		BIT(5)

/* e777h */
#define RUN_COMMAND		BIT(7)

/* e778h */
#define USER_DATA1_B0		GENMASK(7, 0)

/* e779h */
#define USER_DATA1_B1		GENMASK(7, 0)

/* e77ah */
#define USER_DATA2_B0		GENMASK(7, 0)

/* e77ch */
#define USER_DATA3_B0		GENMASK(7, 0)

/* e790h */
#define LINK_RATE		GENMASK(4, 0)

/* e792h */
#define LANE_COUNT		GENMASK(2, 0)

/* e794h */
#define HRES_B0			GENMASK(7, 0)

/* e795h */
#define HRES_B1			GENMASK(7, 0)

/* e796h */
#define HFP_B0			GENMASK(7, 0)

/* e797h */
#define HFP_B1			GENMASK(7, 0)

/* e798h */
#define HSW_B0			GENMASK(7, 0)

/* e799h */
#define HSW_B1			GENMASK(7, 0)

/* e79ah */
#define HBP_B0			GENMASK(7, 0)

/* e79bh */
#define HBP_B1			GENMASK(7, 0)

/* e79ch */
#define VRES_B0			GENMASK(7, 0)

/* e79dh */
#define VRES_B1			GENMASK(7, 0)

/* e79eh */
#define VFP_B0			GENMASK(7, 0)

/* e79fh */
#define VFP_B1			GENMASK(7, 0)

/* e7a0h */
#define VSW_B0			GENMASK(7, 0)

/* e7a1h */
#define VSW_B1			GENMASK(7, 0)

/* e7a2h */
#define VBP_B0			GENMASK(7, 0)

/* e7a3h */
#define VBP_B1			GENMASK(7, 0)

/* e7a4h */
#define HWORDS_B0		GENMASK(7, 0)

/* e7a5h */
#define HWORDS_B1		GENMASK(7, 0)

/* e7a6h */
#define MVID_B0			GENMASK(7, 0)

/* e7a7h */
#define MVID_B1			GENMASK(7, 0)

/* e7a8h */
#define NVID_B0			GENMASK(7, 0)

/* e7a9h */
#define NVID_B1			GENMASK(7, 0)

/* e7aah */
#define TUC_VALUE_B0		GENMASK(7, 0)

/* e7abh */
#define TUC_VALUE_B1		GENMASK(7, 0)

/* e7ach */
#define HSYNC_POL		BIT(0)
#define VSYNC_POL               BIT(1)

/* e7b0h */
#define SS_ENABLE		BIT(0)

/* e7b1h */
#define SSC_ENABLE		BIT(4)

#endif /* _MFD_MAX96776_H_ */
