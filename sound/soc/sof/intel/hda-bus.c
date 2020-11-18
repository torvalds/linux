// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Authors: Keyon Jie <yang.jie@linux.intel.com>

#include <linux/io.h>
#include <sound/hdaudio.h>
#include "../sof-priv.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
#include "../../codecs/hdac_hda.h"
#define sof_hda_ext_ops	snd_soc_hdac_hda_get_ops()
#else
#define sof_hda_ext_ops	NULL
#endif

/*
 * This can be used for both with/without hda link support.
 */
void sof_hda_bus_init(struct hdac_bus *bus, struct device *dev)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	snd_hdac_ext_bus_init(bus, dev, NULL, sof_hda_ext_ops);
#else /* CONFIG_SND_SOC_SOF_HDA */
	memset(bus, 0, sizeof(*bus));
	bus->dev = dev;

	INIT_LIST_HEAD(&bus->stream_list);

	bus->irq = -1;

	/*
	 * There is only one HDA bus atm. keep the index as 0.
	 * Need to fix when there are more than one HDA bus.
	 */
	bus->idx = 0;

	spin_lock_init(&bus->reg_lock);
#endif /* CONFIG_SND_SOC_SOF_HDA */
}
