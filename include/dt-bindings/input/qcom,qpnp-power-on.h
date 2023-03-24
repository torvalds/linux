/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2019,2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _DT_BINDINGS_INPUT_QCOM_POWER_ON_H
#define _DT_BINDINGS_INPUT_QCOM_POWER_ON_H

/* PMIC PON peripheral logical power on types: */
#define PON_POWER_ON_TYPE_KPDPWR		0
#define PON_POWER_ON_TYPE_RESIN			1
#define PON_POWER_ON_TYPE_CBLPWR		2
#define PON_POWER_ON_TYPE_KPDPWR_RESIN		3

/* PMIC PON peripheral physical power off types: */
#define PON_POWER_OFF_TYPE_WARM_RESET		0x01
#define PON_POWER_OFF_TYPE_SHUTDOWN		0x04
#define PON_POWER_OFF_TYPE_DVDD_SHUTDOWN	0x05
#define PON_POWER_OFF_TYPE_HARD_RESET		0x07
#define PON_POWER_OFF_TYPE_DVDD_HARD_RESET	0x08

#endif
