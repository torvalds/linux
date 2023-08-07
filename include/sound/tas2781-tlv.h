/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2022 - 2023 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
//

#ifndef __TAS2781_TLV_H__
#define __TAS2781_TLV_H__

static const DECLARE_TLV_DB_SCALE(dvc_tlv, -10000, 100, 0);
static const DECLARE_TLV_DB_SCALE(amp_vol_tlv, 1100, 50, 0);

#endif
