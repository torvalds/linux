// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/usb.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/soc-usb.h>

#include "../usbaudio.h"
#include "../card.h"
#include "../helper.h"
#include "../mixer.h"

#include "mixer_usb_offload.h"

#define PCM_IDX(n)  ((n) & 0xffff)
#define CARD_IDX(n) ((n) >> 16)

static int
snd_usb_offload_card_route_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct device *sysdev = snd_kcontrol_chip(kcontrol);
	int ret;

	ret = snd_soc_usb_update_offload_route(sysdev,
					       CARD_IDX(kcontrol->private_value),
					       PCM_IDX(kcontrol->private_value),
					       SNDRV_PCM_STREAM_PLAYBACK,
					       SND_SOC_USB_KCTL_CARD_ROUTE,
					       ucontrol->value.integer.value);
	if (ret < 0) {
		ucontrol->value.integer.value[0] = -1;
		ucontrol->value.integer.value[1] = -1;
	}

	return 0;
}

static int snd_usb_offload_card_route_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = -1;
	uinfo->value.integer.max = SNDRV_CARDS;

	return 0;
}

static struct snd_kcontrol_new snd_usb_offload_mapped_card_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_usb_offload_card_route_info,
	.get = snd_usb_offload_card_route_get,
};

static int
snd_usb_offload_pcm_route_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct device *sysdev = snd_kcontrol_chip(kcontrol);
	int ret;

	ret = snd_soc_usb_update_offload_route(sysdev,
					       CARD_IDX(kcontrol->private_value),
					       PCM_IDX(kcontrol->private_value),
					       SNDRV_PCM_STREAM_PLAYBACK,
					       SND_SOC_USB_KCTL_PCM_ROUTE,
					       ucontrol->value.integer.value);
	if (ret < 0) {
		ucontrol->value.integer.value[0] = -1;
		ucontrol->value.integer.value[1] = -1;
	}

	return 0;
}

static int snd_usb_offload_pcm_route_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = -1;
	/* Arbitrary max value, as there is no 'limit' on number of PCM devices */
	uinfo->value.integer.max = 0xff;

	return 0;
}

static struct snd_kcontrol_new snd_usb_offload_mapped_pcm_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_usb_offload_pcm_route_info,
	.get = snd_usb_offload_pcm_route_get,
};

/**
 * snd_usb_offload_create_ctl() - Add USB offload bounded mixer
 * @chip: USB SND chip device
 * @bedev: Reference to USB backend DAI device
 *
 * Creates a sound control for a USB audio device, so that applications can
 * query for if there is an available USB audio offload path, and which
 * card is managing it.
 */
int snd_usb_offload_create_ctl(struct snd_usb_audio *chip, struct device *bedev)
{
	struct snd_kcontrol_new *chip_kctl;
	struct snd_usb_substream *subs;
	struct snd_usb_stream *as;
	char ctl_name[48];
	int ret;

	list_for_each_entry(as, &chip->pcm_list, list) {
		subs = &as->substream[SNDRV_PCM_STREAM_PLAYBACK];
		if (!subs->ep_num || as->pcm_index > 0xff)
			continue;

		chip_kctl = &snd_usb_offload_mapped_card_ctl;
		chip_kctl->count = 1;
		/*
		 * Store the associated USB SND card number and PCM index for
		 * the kctl.
		 */
		chip_kctl->private_value = as->pcm_index |
					  chip->card->number << 16;
		sprintf(ctl_name, "USB Offload Playback Card Route PCM#%d",
			as->pcm_index);
		chip_kctl->name = ctl_name;
		ret = snd_ctl_add(chip->card, snd_ctl_new1(chip_kctl, bedev));
		if (ret < 0)
			break;

		chip_kctl = &snd_usb_offload_mapped_pcm_ctl;
		chip_kctl->count = 1;
		/*
		 * Store the associated USB SND card number and PCM index for
		 * the kctl.
		 */
		chip_kctl->private_value = as->pcm_index |
					  chip->card->number << 16;
		sprintf(ctl_name, "USB Offload Playback PCM Route PCM#%d",
			as->pcm_index);
		chip_kctl->name = ctl_name;
		ret = snd_ctl_add(chip->card, snd_ctl_new1(chip_kctl, bedev));
		if (ret < 0)
			break;
	}

	return ret;
}
