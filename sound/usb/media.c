// SPDX-License-Identifier: GPL-2.0
/*
 * media.c - Media Controller specific ALSA driver code
 *
 * Copyright (c) 2019 Shuah Khan <shuah@kernel.org>
 *
 */

/*
 * This file adds Media Controller support to the ALSA driver
 * to use the Media Controller API to share the tuner with DVB
 * and V4L2 drivers that control the media device.
 *
 * The media device is created based on the existing quirks framework.
 * Using this approach, the media controller API usage can be added for
 * a specific device.
 */

#include <linux/init.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <sound/pcm.h>
#include <sound/core.h>

#include "usbaudio.h"
#include "card.h"
#include "mixer.h"
#include "media.h"

int snd_media_stream_init(struct snd_usb_substream *subs, struct snd_pcm *pcm,
			  int stream)
{
	struct media_device *mdev;
	struct media_ctl *mctl;
	struct device *pcm_dev = &pcm->streams[stream].dev;
	u32 intf_type;
	int ret = 0;
	u16 mixer_pad;
	struct media_entity *entity;

	mdev = subs->stream->chip->media_dev;
	if (!mdev)
		return 0;

	if (subs->media_ctl)
		return 0;

	/* allocate media_ctl */
	mctl = kzalloc(sizeof(*mctl), GFP_KERNEL);
	if (!mctl)
		return -ENOMEM;

	mctl->media_dev = mdev;
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		intf_type = MEDIA_INTF_T_ALSA_PCM_PLAYBACK;
		mctl->media_entity.function = MEDIA_ENT_F_AUDIO_PLAYBACK;
		mctl->media_pad.flags = MEDIA_PAD_FL_SOURCE;
		mixer_pad = 1;
	} else {
		intf_type = MEDIA_INTF_T_ALSA_PCM_CAPTURE;
		mctl->media_entity.function = MEDIA_ENT_F_AUDIO_CAPTURE;
		mctl->media_pad.flags = MEDIA_PAD_FL_SINK;
		mixer_pad = 2;
	}
	mctl->media_entity.name = pcm->name;
	media_entity_pads_init(&mctl->media_entity, 1, &mctl->media_pad);
	ret =  media_device_register_entity(mctl->media_dev,
					    &mctl->media_entity);
	if (ret)
		goto free_mctl;

	mctl->intf_devnode = media_devnode_create(mdev, intf_type, 0,
						  MAJOR(pcm_dev->devt),
						  MINOR(pcm_dev->devt));
	if (!mctl->intf_devnode) {
		ret = -ENOMEM;
		goto unregister_entity;
	}
	mctl->intf_link = media_create_intf_link(&mctl->media_entity,
						 &mctl->intf_devnode->intf,
						 MEDIA_LNK_FL_ENABLED);
	if (!mctl->intf_link) {
		ret = -ENOMEM;
		goto devnode_remove;
	}

	/* create link between mixer and audio */
	media_device_for_each_entity(entity, mdev) {
		switch (entity->function) {
		case MEDIA_ENT_F_AUDIO_MIXER:
			ret = media_create_pad_link(entity, mixer_pad,
						    &mctl->media_entity, 0,
						    MEDIA_LNK_FL_ENABLED);
			if (ret)
				goto remove_intf_link;
			break;
		}
	}

	subs->media_ctl = mctl;
	return 0;

remove_intf_link:
	media_remove_intf_link(mctl->intf_link);
devnode_remove:
	media_devnode_remove(mctl->intf_devnode);
unregister_entity:
	media_device_unregister_entity(&mctl->media_entity);
free_mctl:
	kfree(mctl);
	return ret;
}

void snd_media_stream_delete(struct snd_usb_substream *subs)
{
	struct media_ctl *mctl = subs->media_ctl;

	if (mctl) {
		struct media_device *mdev;

		mdev = mctl->media_dev;
		if (mdev && media_devnode_is_registered(mdev->devnode)) {
			media_devnode_remove(mctl->intf_devnode);
			media_device_unregister_entity(&mctl->media_entity);
			media_entity_cleanup(&mctl->media_entity);
		}
		kfree(mctl);
		subs->media_ctl = NULL;
	}
}

int snd_media_start_pipeline(struct snd_usb_substream *subs)
{
	struct media_ctl *mctl = subs->media_ctl;
	int ret = 0;

	if (!mctl)
		return 0;

	mutex_lock(&mctl->media_dev->graph_mutex);
	if (mctl->media_dev->enable_source)
		ret = mctl->media_dev->enable_source(&mctl->media_entity,
						     &mctl->media_pipe);
	mutex_unlock(&mctl->media_dev->graph_mutex);
	return ret;
}

void snd_media_stop_pipeline(struct snd_usb_substream *subs)
{
	struct media_ctl *mctl = subs->media_ctl;

	if (!mctl)
		return;

	mutex_lock(&mctl->media_dev->graph_mutex);
	if (mctl->media_dev->disable_source)
		mctl->media_dev->disable_source(&mctl->media_entity);
	mutex_unlock(&mctl->media_dev->graph_mutex);
}

static int snd_media_mixer_init(struct snd_usb_audio *chip)
{
	struct device *ctl_dev = &chip->card->ctl_dev;
	struct media_intf_devnode *ctl_intf;
	struct usb_mixer_interface *mixer;
	struct media_device *mdev = chip->media_dev;
	struct media_mixer_ctl *mctl;
	u32 intf_type = MEDIA_INTF_T_ALSA_CONTROL;
	int ret;

	if (!mdev)
		return -ENODEV;

	ctl_intf = chip->ctl_intf_media_devnode;
	if (!ctl_intf) {
		ctl_intf = media_devnode_create(mdev, intf_type, 0,
						MAJOR(ctl_dev->devt),
						MINOR(ctl_dev->devt));
		if (!ctl_intf)
			return -ENOMEM;
		chip->ctl_intf_media_devnode = ctl_intf;
	}

	list_for_each_entry(mixer, &chip->mixer_list, list) {

		if (mixer->media_mixer_ctl)
			continue;

		/* allocate media_mixer_ctl */
		mctl = kzalloc(sizeof(*mctl), GFP_KERNEL);
		if (!mctl)
			return -ENOMEM;

		mctl->media_dev = mdev;
		mctl->media_entity.function = MEDIA_ENT_F_AUDIO_MIXER;
		mctl->media_entity.name = chip->card->mixername;
		mctl->media_pad[0].flags = MEDIA_PAD_FL_SINK;
		mctl->media_pad[1].flags = MEDIA_PAD_FL_SOURCE;
		mctl->media_pad[2].flags = MEDIA_PAD_FL_SOURCE;
		media_entity_pads_init(&mctl->media_entity, MEDIA_MIXER_PAD_MAX,
				  mctl->media_pad);
		ret =  media_device_register_entity(mctl->media_dev,
						    &mctl->media_entity);
		if (ret) {
			kfree(mctl);
			return ret;
		}

		mctl->intf_link = media_create_intf_link(&mctl->media_entity,
							 &ctl_intf->intf,
							 MEDIA_LNK_FL_ENABLED);
		if (!mctl->intf_link) {
			media_device_unregister_entity(&mctl->media_entity);
			media_entity_cleanup(&mctl->media_entity);
			kfree(mctl);
			return -ENOMEM;
		}
		mctl->intf_devnode = ctl_intf;
		mixer->media_mixer_ctl = mctl;
	}
	return 0;
}

static void snd_media_mixer_delete(struct snd_usb_audio *chip)
{
	struct usb_mixer_interface *mixer;
	struct media_device *mdev = chip->media_dev;

	if (!mdev)
		return;

	list_for_each_entry(mixer, &chip->mixer_list, list) {
		struct media_mixer_ctl *mctl;

		mctl = mixer->media_mixer_ctl;
		if (!mixer->media_mixer_ctl)
			continue;

		if (media_devnode_is_registered(mdev->devnode)) {
			media_device_unregister_entity(&mctl->media_entity);
			media_entity_cleanup(&mctl->media_entity);
		}
		kfree(mctl);
		mixer->media_mixer_ctl = NULL;
	}
	if (media_devnode_is_registered(mdev->devnode))
		media_devnode_remove(chip->ctl_intf_media_devnode);
	chip->ctl_intf_media_devnode = NULL;
}

int snd_media_device_create(struct snd_usb_audio *chip,
			struct usb_interface *iface)
{
	struct media_device *mdev;
	struct usb_device *usbdev = interface_to_usbdev(iface);
	int ret = 0;

	/* usb-audio driver is probed for each usb interface, and
	 * there are multiple interfaces per device. Avoid calling
	 * media_device_usb_allocate() each time usb_audio_probe()
	 * is called. Do it only once.
	 */
	if (chip->media_dev) {
		mdev = chip->media_dev;
		goto snd_mixer_init;
	}

	mdev = media_device_usb_allocate(usbdev, KBUILD_MODNAME, THIS_MODULE);
	if (IS_ERR(mdev))
		return -ENOMEM;

	/* save media device - avoid lookups */
	chip->media_dev = mdev;

snd_mixer_init:
	/* Create media entities for mixer and control dev */
	ret = snd_media_mixer_init(chip);
	/* media_device might be registered, print error and continue */
	if (ret)
		dev_err(&usbdev->dev,
			"Couldn't create media mixer entities. Error: %d\n",
			ret);

	if (!media_devnode_is_registered(mdev->devnode)) {
		/* don't register if snd_media_mixer_init() failed */
		if (ret)
			goto create_fail;

		/* register media_device */
		ret = media_device_register(mdev);
create_fail:
		if (ret) {
			snd_media_mixer_delete(chip);
			media_device_delete(mdev, KBUILD_MODNAME, THIS_MODULE);
			/* clear saved media_dev */
			chip->media_dev = NULL;
			dev_err(&usbdev->dev,
				"Couldn't register media device. Error: %d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

void snd_media_device_delete(struct snd_usb_audio *chip)
{
	struct media_device *mdev = chip->media_dev;
	struct snd_usb_stream *stream;

	/* release resources */
	list_for_each_entry(stream, &chip->pcm_list, list) {
		snd_media_stream_delete(&stream->substream[0]);
		snd_media_stream_delete(&stream->substream[1]);
	}

	snd_media_mixer_delete(chip);

	if (mdev) {
		media_device_delete(mdev, KBUILD_MODNAME, THIS_MODULE);
		chip->media_dev = NULL;
	}
}
