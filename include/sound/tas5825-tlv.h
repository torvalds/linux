/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS5825 Audio Smart Amplifier
//
// Copyright (C) 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS5825 hda driver implements for one or two TAS5825 chips.
//
// Author: Baojun Xu <baojun.xu@ti.com>
//

#ifndef __TAS5825_TLV_H__
#define __TAS5825_TLV_H__

#define TAS5825_DVC_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x4c)
#define TAS5825_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x54)

static const __maybe_unused DECLARE_TLV_DB_SCALE(
		tas5825_dvc_tlv, -10300, 50, 0);
static const __maybe_unused DECLARE_TLV_DB_SCALE(
		tas5825_amp_tlv, -1550, 50, 0);

#endif
