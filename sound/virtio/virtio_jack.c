// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#include <linux/virtio_config.h>
#include <sound/jack.h>
#include <sound/hda_verbs.h>

#include "virtio_card.h"

/**
 * DOC: Implementation Status
 *
 * At the moment jacks have a simple implementation and can only be used to
 * receive notifications about a plugged in/out device.
 *
 * VIRTIO_SND_R_JACK_REMAP
 *   is not supported
 */

/**
 * struct virtio_jack - VirtIO jack.
 * @jack: Kernel jack control.
 * @nid: Functional group node identifier.
 * @features: Jack virtio feature bit map (1 << VIRTIO_SND_JACK_F_XXX).
 * @defconf: Pin default configuration value.
 * @caps: Pin capabilities value.
 * @connected: Current jack connection status.
 * @type: Kernel jack type (SND_JACK_XXX).
 */
struct virtio_jack {
	struct snd_jack *jack;
	u32 nid;
	u32 features;
	u32 defconf;
	u32 caps;
	bool connected;
	int type;
};

/**
 * virtsnd_jack_get_label() - Get the name string for the jack.
 * @vjack: VirtIO jack.
 *
 * Returns the jack name based on the default pin configuration value (see HDA
 * specification).
 *
 * Context: Any context.
 * Return: Name string.
 */
static const char *virtsnd_jack_get_label(struct virtio_jack *vjack)
{
	unsigned int defconf = vjack->defconf;
	unsigned int device =
		(defconf & AC_DEFCFG_DEVICE) >> AC_DEFCFG_DEVICE_SHIFT;
	unsigned int location =
		(defconf & AC_DEFCFG_LOCATION) >> AC_DEFCFG_LOCATION_SHIFT;

	switch (device) {
	case AC_JACK_LINE_OUT:
		return "Line Out";
	case AC_JACK_SPEAKER:
		return "Speaker";
	case AC_JACK_HP_OUT:
		return "Headphone";
	case AC_JACK_CD:
		return "CD";
	case AC_JACK_SPDIF_OUT:
	case AC_JACK_DIG_OTHER_OUT:
		if (location == AC_JACK_LOC_HDMI)
			return "HDMI Out";
		else
			return "SPDIF Out";
	case AC_JACK_LINE_IN:
		return "Line";
	case AC_JACK_AUX:
		return "Aux";
	case AC_JACK_MIC_IN:
		return "Mic";
	case AC_JACK_SPDIF_IN:
		return "SPDIF In";
	case AC_JACK_DIG_OTHER_IN:
		return "Digital In";
	default:
		return "Misc";
	}
}

/**
 * virtsnd_jack_get_type() - Get the type for the jack.
 * @vjack: VirtIO jack.
 *
 * Returns the jack type based on the default pin configuration value (see HDA
 * specification).
 *
 * Context: Any context.
 * Return: SND_JACK_XXX value.
 */
static int virtsnd_jack_get_type(struct virtio_jack *vjack)
{
	unsigned int defconf = vjack->defconf;
	unsigned int device =
		(defconf & AC_DEFCFG_DEVICE) >> AC_DEFCFG_DEVICE_SHIFT;

	switch (device) {
	case AC_JACK_LINE_OUT:
	case AC_JACK_SPEAKER:
		return SND_JACK_LINEOUT;
	case AC_JACK_HP_OUT:
		return SND_JACK_HEADPHONE;
	case AC_JACK_SPDIF_OUT:
	case AC_JACK_DIG_OTHER_OUT:
		return SND_JACK_AVOUT;
	case AC_JACK_MIC_IN:
		return SND_JACK_MICROPHONE;
	default:
		return SND_JACK_LINEIN;
	}
}

/**
 * virtsnd_jack_parse_cfg() - Parse the jack configuration.
 * @snd: VirtIO sound device.
 *
 * This function is called during initial device initialization.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_jack_parse_cfg(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	struct virtio_snd_jack_info *info;
	u32 i;
	int rc;

	virtio_cread_le(vdev, struct virtio_snd_config, jacks, &snd->njacks);
	if (!snd->njacks)
		return 0;

	snd->jacks = devm_kcalloc(&vdev->dev, snd->njacks, sizeof(*snd->jacks),
				  GFP_KERNEL);
	if (!snd->jacks)
		return -ENOMEM;

	info = kcalloc(snd->njacks, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	rc = virtsnd_ctl_query_info(snd, VIRTIO_SND_R_JACK_INFO, 0, snd->njacks,
				    sizeof(*info), info);
	if (rc)
		goto on_exit;

	for (i = 0; i < snd->njacks; ++i) {
		struct virtio_jack *vjack = &snd->jacks[i];

		vjack->nid = le32_to_cpu(info[i].hdr.hda_fn_nid);
		vjack->features = le32_to_cpu(info[i].features);
		vjack->defconf = le32_to_cpu(info[i].hda_reg_defconf);
		vjack->caps = le32_to_cpu(info[i].hda_reg_caps);
		vjack->connected = info[i].connected;
	}

on_exit:
	kfree(info);

	return rc;
}

/**
 * virtsnd_jack_build_devs() - Build ALSA controls for jacks.
 * @snd: VirtIO sound device.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
int virtsnd_jack_build_devs(struct virtio_snd *snd)
{
	u32 i;
	int rc;

	for (i = 0; i < snd->njacks; ++i) {
		struct virtio_jack *vjack = &snd->jacks[i];

		vjack->type = virtsnd_jack_get_type(vjack);

		rc = snd_jack_new(snd->card, virtsnd_jack_get_label(vjack),
				  vjack->type, &vjack->jack, true, true);
		if (rc)
			return rc;

		if (vjack->jack)
			vjack->jack->private_data = vjack;

		snd_jack_report(vjack->jack,
				vjack->connected ? vjack->type : 0);
	}

	return 0;
}

/**
 * virtsnd_jack_event() - Handle the jack event notification.
 * @snd: VirtIO sound device.
 * @event: VirtIO sound event.
 *
 * Context: Interrupt context.
 */
void virtsnd_jack_event(struct virtio_snd *snd, struct virtio_snd_event *event)
{
	u32 jack_id = le32_to_cpu(event->data);
	struct virtio_jack *vjack;

	if (jack_id >= snd->njacks)
		return;

	vjack = &snd->jacks[jack_id];

	switch (le32_to_cpu(event->hdr.code)) {
	case VIRTIO_SND_EVT_JACK_CONNECTED:
		vjack->connected = true;
		break;
	case VIRTIO_SND_EVT_JACK_DISCONNECTED:
		vjack->connected = false;
		break;
	default:
		return;
	}

	snd_jack_report(vjack->jack, vjack->connected ? vjack->type : 0);
}
