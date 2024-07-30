/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8650_H
#define _DT_BINDINGS_CLK_QCOM_VIDEO_CC_SM8650_H

#include "qcom,sm8450-videocc.h"

/* SM8650 introduces below new clocks and resets compared to SM8450 */

/* VIDEO_CC clocks */
#define VIDEO_CC_MVS0_SHIFT_CLK					12
#define VIDEO_CC_MVS0C_SHIFT_CLK				13
#define VIDEO_CC_MVS1_SHIFT_CLK					14
#define VIDEO_CC_MVS1C_SHIFT_CLK				15
#define VIDEO_CC_XO_CLK_SRC					16

/* VIDEO_CC resets */
#define VIDEO_CC_XO_CLK_ARES					7

#endif
