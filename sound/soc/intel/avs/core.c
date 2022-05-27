// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//
// Special thanks to:
//    Krzysztof Hejmowski <krzysztof.hejmowski@intel.com>
//    Michal Sienkiewicz <michal.sienkiewicz@intel.com>
//    Filip Proborszcz
//
// for sharing Intel AudioDSP expertise and helping shape the very
// foundation of this driver
//

#include <linux/pci.h>
#include <sound/hdaudio.h>
#include "avs.h"

static void
avs_hda_update_config_dword(struct hdac_bus *bus, u32 reg, u32 mask, u32 value)
{
	struct pci_dev *pci = to_pci_dev(bus->dev);
	u32 data;

	pci_read_config_dword(pci, reg, &data);
	data &= ~mask;
	data |= (value & mask);
	pci_write_config_dword(pci, reg, data);
}

void avs_hda_power_gating_enable(struct avs_dev *adev, bool enable)
{
	u32 value;

	value = enable ? 0 : AZX_PGCTL_LSRMD_MASK;
	avs_hda_update_config_dword(&adev->base.core, AZX_PCIREG_PGCTL,
				    AZX_PGCTL_LSRMD_MASK, value);
}

static void avs_hdac_clock_gating_enable(struct hdac_bus *bus, bool enable)
{
	u32 value;

	value = enable ? AZX_CGCTL_MISCBDCGE_MASK : 0;
	avs_hda_update_config_dword(bus, AZX_PCIREG_CGCTL, AZX_CGCTL_MISCBDCGE_MASK, value);
}

void avs_hda_clock_gating_enable(struct avs_dev *adev, bool enable)
{
	avs_hdac_clock_gating_enable(&adev->base.core, enable);
}

void avs_hda_l1sen_enable(struct avs_dev *adev, bool enable)
{
	u32 value;

	value = enable ? AZX_VS_EM2_L1SEN : 0;
	snd_hdac_chip_updatel(&adev->base.core, VS_EM2, AZX_VS_EM2_L1SEN, value);
}
