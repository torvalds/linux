/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Gateworks Corporation
 */
#ifndef _DT_BINDINGS_MEDIA_TDA1997X_H
#define _DT_BINDINGS_MEDIA_TDA1997X_H

/* TDA19973 36bit Video Port control registers */
#define TDA1997X_VP36_35_32	0
#define TDA1997X_VP36_31_28	1
#define TDA1997X_VP36_27_24	2
#define TDA1997X_VP36_23_20	3
#define TDA1997X_VP36_19_16	4
#define TDA1997X_VP36_15_12	5
#define TDA1997X_VP36_11_08	6
#define TDA1997X_VP36_07_04	7
#define TDA1997X_VP36_03_00	8

/* TDA19971 24bit Video Port control registers */
#define TDA1997X_VP24_V23_20	0
#define TDA1997X_VP24_V19_16	1
#define TDA1997X_VP24_V15_12	3
#define TDA1997X_VP24_V11_08	4
#define TDA1997X_VP24_V07_04	6
#define TDA1997X_VP24_V03_00	7

/* Pin groups */
#define TDA1997X_VP_OUT_EN        0x80	/* enable output group */
#define TDA1997X_VP_HIZ           0x40	/* hi-Z output group when not used */
#define TDA1997X_VP_SWP           0x10	/* pin-swap output group */
#define TDA1997X_R_CR_CBCR_3_0    (0 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_R_CR_CBCR_7_4    (1 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_R_CR_CBCR_11_8   (2 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_B_CB_3_0         (3 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_B_CB_7_4         (4 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_B_CB_11_8        (5 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_G_Y_3_0          (6 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_G_Y_7_4          (7 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
#define TDA1997X_G_Y_11_8         (8 | TDA1997X_VP_OUT_EN | TDA1997X_VP_HIZ)
/* pinswapped groups */
#define TDA1997X_R_CR_CBCR_3_0_S  (TDA1997X_R_CR_CBCR_3_0 | TDA1997X_VP_SWAP)
#define TDA1997X_R_CR_CBCR_7_4_S  (TDA1997X_R_CR_CBCR_7_4 | TDA1997X_VP_SWAP)
#define TDA1997X_R_CR_CBCR_11_8_S (TDA1997X_R_CR_CBCR_11_8 | TDA1997X_VP_SWAP)
#define TDA1997X_B_CB_3_0_S       (TDA1997X_B_CB_3_0 | TDA1997X_VP_SWAP)
#define TDA1997X_B_CB_7_4_S       (TDA1997X_B_CB_7_4 | TDA1997X_VP_SWAP)
#define TDA1997X_B_CB_11_8_S      (TDA1997X_B_CB_11_8 | TDA1997X_VP_SWAP)
#define TDA1997X_G_Y_3_0_S        (TDA1997X_G_Y_3_0 | TDA1997X_VP_SWAP)
#define TDA1997X_G_Y_7_4_S        (TDA1997X_G_Y_7_4 | TDA1997X_VP_SWAP)
#define TDA1997X_G_Y_11_8_S       (TDA1997X_G_Y_11_8 | TDA1997X_VP_SWAP)

/* Audio bus DAI format */
#define TDA1997X_I2S16			1 /* I2S 16bit */
#define TDA1997X_I2S32			2 /* I2S 32bit */
#define TDA1997X_SPDIF			3 /* SPDIF */
#define TDA1997X_OBA			4 /* One Bit Audio */
#define TDA1997X_DST			5 /* Direct Stream Transfer */
#define TDA1997X_I2S16_HBR		6 /* HBR straight in I2S 16bit mode */
#define TDA1997X_I2S16_HBR_DEMUX	7 /* HBR demux in I2S 16bit mode */
#define TDA1997X_I2S32_HBR_DEMUX	8 /* HBR demux in I2S 32bit mode */
#define TDA1997X_SPDIF_HBR_DEMUX	9 /* HBR demux in SPDIF mode */

/* Audio bus channel layout */
#define TDA1997X_LAYOUT0	0	/* 2-channel */
#define TDA1997X_LAYOUT1	1	/* 8-channel */

/* Audio bus clock */
#define TDA1997X_ACLK_16FS	0
#define TDA1997X_ACLK_32FS	1
#define TDA1997X_ACLK_64FS	2
#define TDA1997X_ACLK_128FS	3
#define TDA1997X_ACLK_256FS	4
#define TDA1997X_ACLK_512FS	5

#endif /* _DT_BINDINGS_MEDIA_TDA1997X_H */
