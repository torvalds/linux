/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt7986-reg.h  --  MediaTek 7986 audio driver reg definition
 *
 * Copyright (c) 2023 MediaTek Inc.
 * Authors: Vic Wu <vic.wu@mediatek.com>
 *          Maso Huang <maso.huang@mediatek.com>
 */

#ifndef _MT7986_REG_H_
#define _MT7986_REG_H_

#define AUDIO_TOP_CON2                  0x0008
#define AUDIO_TOP_CON4                  0x0010
#define AUDIO_ENGEN_CON0                0x0014
#define AFE_IRQ_MCU_EN                  0x0100
#define AFE_IRQ_MCU_STATUS              0x0120
#define AFE_IRQ_MCU_CLR                 0x0128
#define AFE_IRQ0_MCU_CFG0               0x0140
#define AFE_IRQ0_MCU_CFG1               0x0144
#define AFE_IRQ1_MCU_CFG0               0x0148
#define AFE_IRQ1_MCU_CFG1               0x014c
#define AFE_IRQ2_MCU_CFG0               0x0150
#define AFE_IRQ2_MCU_CFG1               0x0154
#define ETDM_IN5_CON0                   0x13f0
#define ETDM_IN5_CON1                   0x13f4
#define ETDM_IN5_CON2                   0x13f8
#define ETDM_IN5_CON3                   0x13fc
#define ETDM_IN5_CON4                   0x1400
#define ETDM_OUT5_CON0                  0x1570
#define ETDM_OUT5_CON4                  0x1580
#define ETDM_OUT5_CON5                  0x1584
#define ETDM_4_7_COWORK_CON0            0x15e0
#define ETDM_4_7_COWORK_CON1            0x15e4
#define AFE_CONN018_1                   0x1b44
#define AFE_CONN018_4                   0x1b50
#define AFE_CONN019_1                   0x1b64
#define AFE_CONN019_4                   0x1b70
#define AFE_CONN124_1                   0x2884
#define AFE_CONN124_4                   0x2890
#define AFE_CONN125_1                   0x28a4
#define AFE_CONN125_4                   0x28b0
#define AFE_CONN_RS_0                   0x3920
#define AFE_CONN_RS_3                   0x392c
#define AFE_CONN_16BIT_0                0x3960
#define AFE_CONN_16BIT_3                0x396c
#define AFE_CONN_24BIT_0                0x3980
#define AFE_CONN_24BIT_3                0x398c
#define AFE_MEMIF_CON0                  0x3d98
#define AFE_MEMIF_RD_MON                0x3da0
#define AFE_MEMIF_WR_MON                0x3da4
#define AFE_DL0_BASE_MSB                0x3e40
#define AFE_DL0_BASE                    0x3e44
#define AFE_DL0_CUR_MSB                 0x3e48
#define AFE_DL0_CUR                     0x3e4c
#define AFE_DL0_END_MSB                 0x3e50
#define AFE_DL0_END                     0x3e54
#define AFE_DL0_RCH_MON                 0x3e58
#define AFE_DL0_LCH_MON                 0x3e5c
#define AFE_DL0_CON0                    0x3e60
#define AFE_VUL0_BASE_MSB               0x4220
#define AFE_VUL0_BASE                   0x4224
#define AFE_VUL0_CUR_MSB                0x4228
#define AFE_VUL0_CUR                    0x422c
#define AFE_VUL0_END_MSB                0x4230
#define AFE_VUL0_END                    0x4234
#define AFE_VUL0_CON0                   0x4238

#define AFE_MAX_REGISTER AFE_VUL0_CON0
#define AFE_IRQ_STATUS_BITS             0x7
#define AFE_IRQ_CNT_SHIFT               0
#define AFE_IRQ_CNT_MASK	        0xffffff

/* AUDIO_TOP_CON2 */
#define CLK_OUT5_PDN                    BIT(14)
#define CLK_OUT5_PDN_MASK               BIT(14)
#define CLK_IN5_PDN                     BIT(7)
#define CLK_IN5_PDN_MASK                BIT(7)

/* AUDIO_TOP_CON4 */
#define PDN_APLL_TUNER2                 BIT(12)
#define PDN_APLL_TUNER2_MASK            BIT(12)

/* AUDIO_ENGEN_CON0 */
#define AUD_APLL2_EN                    BIT(3)
#define AUD_APLL2_EN_MASK               BIT(3)
#define AUD_26M_EN                      BIT(0)
#define AUD_26M_EN_MASK                 BIT(0)

/* AFE_DL0_CON0 */
#define DL0_ON_SFT                      28
#define DL0_ON_MASK                     0x1
#define DL0_ON_MASK_SFT                 BIT(28)
#define DL0_MINLEN_SFT                  20
#define DL0_MINLEN_MASK                 0xf
#define DL0_MINLEN_MASK_SFT             (0xf << 20)
#define DL0_MODE_SFT                    8
#define DL0_MODE_MASK                   0x1f
#define DL0_MODE_MASK_SFT               (0x1f << 8)
#define DL0_PBUF_SIZE_SFT               5
#define DL0_PBUF_SIZE_MASK              0x3
#define DL0_PBUF_SIZE_MASK_SFT          (0x3 << 5)
#define DL0_MONO_SFT                    4
#define DL0_MONO_MASK                   0x1
#define DL0_MONO_MASK_SFT               BIT(4)
#define DL0_HALIGN_SFT                  2
#define DL0_HALIGN_MASK                 0x1
#define DL0_HALIGN_MASK_SFT             BIT(2)
#define DL0_HD_MODE_SFT                 0
#define DL0_HD_MODE_MASK                0x3
#define DL0_HD_MODE_MASK_SFT            (0x3 << 0)

/* AFE_VUL0_CON0 */
#define VUL0_ON_SFT                     28
#define VUL0_ON_MASK                    0x1
#define VUL0_ON_MASK_SFT                BIT(28)
#define VUL0_MODE_SFT                   8
#define VUL0_MODE_MASK                  0x1f
#define VUL0_MODE_MASK_SFT              (0x1f << 8)
#define VUL0_MONO_SFT                   4
#define VUL0_MONO_MASK                  0x1
#define VUL0_MONO_MASK_SFT              BIT(4)
#define VUL0_HALIGN_SFT                 2
#define VUL0_HALIGN_MASK                0x1
#define VUL0_HALIGN_MASK_SFT            BIT(2)
#define VUL0_HD_MODE_SFT                0
#define VUL0_HD_MODE_MASK               0x3
#define VUL0_HD_MODE_MASK_SFT           (0x3 << 0)

/* AFE_IRQ_MCU_CON */
#define IRQ_MCU_MODE_SFT                4
#define IRQ_MCU_MODE_MASK               0x1f
#define IRQ_MCU_MODE_MASK_SFT           (0x1f << 4)
#define IRQ_MCU_ON_SFT                  0
#define IRQ_MCU_ON_MASK                 0x1
#define IRQ_MCU_ON_MASK_SFT             BIT(0)
#define IRQ0_MCU_CLR_SFT                0
#define IRQ0_MCU_CLR_MASK               0x1
#define IRQ0_MCU_CLR_MASK_SFT           BIT(0)
#define IRQ1_MCU_CLR_SFT                1
#define IRQ1_MCU_CLR_MASK               0x1
#define IRQ1_MCU_CLR_MASK_SFT           BIT(1)
#define IRQ2_MCU_CLR_SFT                2
#define IRQ2_MCU_CLR_MASK               0x1
#define IRQ2_MCU_CLR_MASK_SFT           BIT(2)

/* ETDM_IN5_CON2 */
#define IN_CLK_SRC(x)                   ((x) << 10)
#define IN_CLK_SRC_SFT                  10
#define IN_CLK_SRC_MASK                 GENMASK(12, 10)

/* ETDM_IN5_CON3 */
#define IN_SEL_FS(x)                    ((x) << 26)
#define IN_SEL_FS_SFT                   26
#define IN_SEL_FS_MASK                  GENMASK(30, 26)

/* ETDM_IN5_CON4 */
#define IN_RELATCH(x)                   ((x) << 20)
#define IN_RELATCH_SFT                  20
#define IN_RELATCH_MASK                 GENMASK(24, 20)
#define IN_CLK_INV                      BIT(18)
#define IN_CLK_INV_MASK                 BIT(18)

/* ETDM_IN5_CON0 & ETDM_OUT5_CON0 */
#define RELATCH_SRC_MASK                GENMASK(30, 28)
#define ETDM_CH_NUM_MASK                GENMASK(27, 23)
#define ETDM_WRD_LEN_MASK               GENMASK(20, 16)
#define ETDM_BIT_LEN_MASK               GENMASK(15, 11)
#define ETDM_FMT_MASK                   GENMASK(8, 6)
#define ETDM_SYNC                       BIT(1)
#define ETDM_SYNC_MASK                  BIT(1)
#define ETDM_EN                         BIT(0)
#define ETDM_EN_MASK                    BIT(0)

/* ETDM_OUT5_CON4 */
#define OUT_RELATCH(x)                  ((x) << 24)
#define OUT_RELATCH_SFT                 24
#define OUT_RELATCH_MASK                GENMASK(28, 24)
#define OUT_CLK_SRC(x)                  ((x) << 6)
#define OUT_CLK_SRC_SFT                 6
#define OUT_CLK_SRC_MASK                GENMASK(8, 6)
#define OUT_SEL_FS(x)                   (x)
#define OUT_SEL_FS_SFT                  0
#define OUT_SEL_FS_MASK                 GENMASK(4, 0)

/* ETDM_OUT5_CON5 */
#define ETDM_CLK_DIV                    BIT(12)
#define ETDM_CLK_DIV_MASK               BIT(12)
#define OUT_CLK_INV                     BIT(9)
#define OUT_CLK_INV_MASK                BIT(9)

/* ETDM_4_7_COWORK_CON0 */
#define OUT_SEL(x)                      ((x) << 12)
#define OUT_SEL_SFT                     12
#define OUT_SEL_MASK                    GENMASK(15, 12)
#endif
