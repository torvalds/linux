/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2770 Audio Smart Amplifier
//
// Copyright (C) 2025 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2770 hda driver implements for one, two, or even multiple
// TAS2770 chips.
//
// Author: Baojun Xu <baojun.xu@ti.com>
//

#ifndef __TAS2770_TLV_H__
#define __TAS2770_TLV_H__

#define TAS2770_DVC_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x05)
#define TAS2770_AMP_LEVEL		TASDEVICE_REG(0x0, 0x0, 0x03)

static const __maybe_unused DECLARE_TLV_DB_SCALE(tas2770_dvc_tlv, -10000, 50, 0);
static const __maybe_unused DECLARE_TLV_DB_SCALE(tas2770_amp_tlv, 1100, 50, 0);

#endif
