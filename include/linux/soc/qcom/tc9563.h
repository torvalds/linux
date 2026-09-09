/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 * Author: Lorenzo Bianconi <lorenzo.bianconi@oss.qualcomm.com>
 */

#ifndef __QCOM_TC9563_H
#define __QCOM_TC9563_H

#define TC9563_GPIO_DEV_NAME	"tc9563-gpio"

#define TC9563_GPIO_IN0_OFFSET		0x801200
#define TC9563_GPIO_EN0_OFFSET		0x801208
#define TC9563_GPIO_OUT0_OFFSET		0x801210

#define TC9563_GPIO_CONFIG		TC9563_GPIO_EN0_OFFSET
#define TC9563_RESET_GPIO		TC9563_GPIO_OUT0_OFFSET

#endif /* __QCOM_TC9563_H */
