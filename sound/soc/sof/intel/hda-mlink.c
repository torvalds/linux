// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//

/*
 * Management of HDaudio multi-link (capabilities, power, coupling)
 */

#include <sound/hdaudio_ext.h>
#include <sound/hda_register.h>

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include <sound/intel-dsp-config.h>
#include <sound/intel-nhlt.h>
#include <sound/sof.h>
#include <sound/sof/xtensa.h>
#include "../sof-audio.h"
#include "../sof-pci-dev.h"
#include "../ops.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)

void hda_bus_ml_get_capabilities(struct hdac_bus *bus)
{
	if (bus->mlcap)
		snd_hdac_ext_bus_get_ml_capabilities(bus);
}

void hda_bus_ml_put_all(struct hdac_bus *bus)
{
	struct hdac_ext_link *hlink;

	list_for_each_entry(hlink, &bus->hlink_list, list)
		snd_hdac_ext_bus_link_put(bus, hlink);
}

#endif
