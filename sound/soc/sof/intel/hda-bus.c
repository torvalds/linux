// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)

static const struct hdac_bus_ops bus_ops = {
	.command = snd_hdac_bus_send_cmd,
	.get_response = snd_hdac_bus_get_response,
};

#endif

/*
 * This can be used for both with/without hda link support.
 */
void sof_hda_bus_init(struct hdac_bus *bus, struct device *dev,
		      const struct hdac_ext_bus_ops *ext_ops)
{
	memset(bus, 0, sizeof(*bus));
	bus->dev = dev;

	INIT_LIST_HEAD(&bus->stream_list);

	bus->irq = -1;
	bus->ext_ops = ext_ops;

	/*
	 * There is only one HDA bus atm. keep the index as 0.
	 * Need to fix when there are more than one HDA bus.
	 */
	bus->idx = 0;

	spin_lock_init(&bus->reg_lock);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA)
	INIT_LIST_HEAD(&bus->codec_list);
	INIT_LIST_HEAD(&bus->hlink_list);

	mutex_init(&bus->cmd_mutex);
	mutex_init(&bus->lock);
	bus->ops = &bus_ops;
	INIT_WORK(&bus->unsol_work, snd_hdac_bus_process_unsol_events);
	bus->cmd_dma_state = true;
#endif

}
