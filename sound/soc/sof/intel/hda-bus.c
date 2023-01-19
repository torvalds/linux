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
#include <sound/hda_i915.h>
#include <sound/hda_codec.h>
#include <sound/hda_register.h>
#include "../sof-priv.h"
#include "hda.h"

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
#include "../../codecs/hdac_hda.h"
#define sof_hda_ext_ops	snd_soc_hdac_hda_get_ops()

static void update_codec_wake_enable(struct hdac_bus *bus, unsigned int addr, bool link_power)
{
	unsigned int mask = snd_hdac_chip_readw(bus, WAKEEN);

	if (link_power)
		mask &= ~BIT(addr);
	else
		mask |= BIT(addr);

	snd_hdac_chip_updatew(bus, WAKEEN, STATESTS_INT_MASK, mask);
}

static void sof_hda_bus_link_power(struct hdac_device *codec, bool enable)
{
	struct hdac_bus *bus = codec->bus;
	bool oldstate = test_bit(codec->addr, &bus->codec_powered);

	snd_hdac_ext_bus_link_power(codec, enable);

	if (enable == oldstate)
		return;

	/*
	 * Both codec driver and controller can hold references to
	 * display power. To avoid unnecessary power-up/down cycles,
	 * controller doesn't immediately release its reference.
	 *
	 * If the codec driver powers down the link, release
	 * the controller reference as well.
	 */
	if (codec->addr == HDA_IDISP_ADDR && !enable)
		snd_hdac_display_power(bus, HDA_CODEC_IDX_CONTROLLER, false);

	/* WAKEEN needs to be set for disabled links */
	update_codec_wake_enable(bus, codec->addr, enable);
}

static const struct hdac_bus_ops bus_core_ops = {
	.command = snd_hdac_bus_send_cmd,
	.get_response = snd_hdac_bus_get_response,
	.link_power = sof_hda_bus_link_power,
};
#endif

/*
 * This can be used for both with/without hda link support.
 */
void sof_hda_bus_init(struct snd_sof_dev *sdev, struct device *dev)
{
	struct hdac_bus *bus = sof_to_bus(sdev);

#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	snd_hdac_ext_bus_init(bus, dev, &bus_core_ops, sof_hda_ext_ops);
#else /* CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC */
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
#endif /* CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC */
}

void sof_hda_bus_exit(struct snd_sof_dev *sdev)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_HDA_AUDIO_CODEC)
	struct hdac_bus *bus = sof_to_bus(sdev);

	snd_hdac_ext_bus_exit(bus);
#endif
}
