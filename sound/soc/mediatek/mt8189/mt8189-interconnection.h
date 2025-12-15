/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Mediatek MT8189 audio driver interconnection definition
 *
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Darren Ye <darren.ye@mediatek.com>
 */

#ifndef _MT8189_INTERCONNECTION_H_
#define _MT8189_INTERCONNECTION_H_

/* in port define */
#define I_CONNSYS_I2S_CH1 0
#define I_CONNSYS_I2S_CH2 1
#define I_GAIN0_OUT_CH1 6
#define I_GAIN0_OUT_CH2 7
#define I_GAIN1_OUT_CH1 8
#define I_GAIN1_OUT_CH2 9
#define I_GAIN2_OUT_CH1 10
#define I_GAIN2_OUT_CH2 11
#define I_GAIN3_OUT_CH1 12
#define I_GAIN3_OUT_CH2 13
#define I_STF_CH1 14
#define I_ADDA_UL_CH1 16
#define I_ADDA_UL_CH2 17
#define I_ADDA_UL_CH3 18
#define I_ADDA_UL_CH4 19
#define I_UL_PROX_CH1 20
#define I_UL_PROX_CH2 21
#define I_ADDA_UL_CH5 24
#define I_ADDA_UL_CH6 25
#define I_DMIC0_CH1 28
#define I_DMIC0_CH2 29
#define I_DMIC1_CH1 30
#define I_DMIC1_CH2 31

/* in port define >= 32 */
#define I_32_OFFSET 32
#define I_DL0_CH1 (32 - I_32_OFFSET)
#define I_DL0_CH2 (33 - I_32_OFFSET)
#define I_DL1_CH1 (34 - I_32_OFFSET)
#define I_DL1_CH2 (35 - I_32_OFFSET)
#define I_DL2_CH1 (36 - I_32_OFFSET)
#define I_DL2_CH2 (37 - I_32_OFFSET)
#define I_DL3_CH1 (38 - I_32_OFFSET)
#define I_DL3_CH2 (39 - I_32_OFFSET)
#define I_DL4_CH1 (40 - I_32_OFFSET)
#define I_DL4_CH2 (41 - I_32_OFFSET)
#define I_DL5_CH1 (42 - I_32_OFFSET)
#define I_DL5_CH2 (43 - I_32_OFFSET)
#define I_DL6_CH1 (44 - I_32_OFFSET)
#define I_DL6_CH2 (45 - I_32_OFFSET)
#define I_DL7_CH1 (46 - I_32_OFFSET)
#define I_DL7_CH2 (47 - I_32_OFFSET)
#define I_DL8_CH1 (48 - I_32_OFFSET)
#define I_DL8_CH2 (49 - I_32_OFFSET)
#define I_DL_24CH_CH1 (54 - I_32_OFFSET)
#define I_DL_24CH_CH2 (55 - I_32_OFFSET)
#define I_DL_24CH_CH3 (56 - I_32_OFFSET)
#define I_DL_24CH_CH4 (57 - I_32_OFFSET)
#define I_DL_24CH_CH5 (58 - I_32_OFFSET)
#define I_DL_24CH_CH6 (59 - I_32_OFFSET)
#define I_DL_24CH_CH7 (60 - I_32_OFFSET)
#define I_DL_24CH_CH8 (61 - I_32_OFFSET)

/* in port define >= 64 */
#define I_64_OFFSET 64
#define I_DL23_CH1 (78 - I_64_OFFSET)
#define I_DL23_CH2 (79 - I_64_OFFSET)
#define I_DL24_CH1 (80 - I_64_OFFSET)
#define I_DL24_CH2 (81 - I_64_OFFSET)
#define I_DL25_CH1 (82 - I_64_OFFSET)
#define I_DL25_CH2 (83 - I_64_OFFSET)

/* in port define >= 128 */
#define I_128_OFFSET 128
#define I_PCM_0_CAP_CH1 (130 - I_128_OFFSET)
#define I_PCM_0_CAP_CH2 (131 - I_128_OFFSET)
#define I_I2SIN0_CH1 (134 - I_128_OFFSET)
#define I_I2SIN0_CH2 (135 - I_128_OFFSET)
#define I_I2SIN1_CH1 (136 - I_128_OFFSET)
#define I_I2SIN1_CH2 (137 - I_128_OFFSET)

/* in port define >= 192 */
#define I_192_OFFSET 192
#define I_SRC_0_OUT_CH1 (198 - I_192_OFFSET)
#define I_SRC_0_OUT_CH2 (199 - I_192_OFFSET)
#define I_SRC_1_OUT_CH1 (200 - I_192_OFFSET)
#define I_SRC_1_OUT_CH2 (201 - I_192_OFFSET)
#define I_SRC_2_OUT_CH1 (202 - I_192_OFFSET)
#define I_SRC_2_OUT_CH2 (203 - I_192_OFFSET)
#define I_SRC_3_OUT_CH1 (204 - I_192_OFFSET)
#define I_SRC_3_OUT_CH2 (205 - I_192_OFFSET)
#define I_SRC_4_OUT_CH1 (206 - I_192_OFFSET)
#define I_SRC_4_OUT_CH2 (207 - I_192_OFFSET)

#endif
