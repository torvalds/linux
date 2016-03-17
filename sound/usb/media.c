/*
 * media.c - Media Controller specific ALSA driver code
 *
 * Copyright (c) 2016 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *
 * This file is released under the GPLv2.
 */

/*
 * This file adds Media Controller support to ALSA driver
 * to use the Media Controller API to share tuner with DVB
 * and V4L2 drivers that control media device. Media device
 * is created based on existing quirks framework. Using this
 * approach, the media controller API usage can be added for
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

static int media_snd_enable_source(struct media_ctl *mctl)
{
	if (mctl && mctl->media_dev->enable_source)
		return mctl->media_dev->enable_source(&mctl->media_entity,
						      &mctl->media_pipe);
	return 0;
}

static void media_snd_disable_source(struct media_ctl *mctl)
{
	if (mctl && mctl->media_dev->disable_source)
		mctl->media_dev->disable_source(&mctl->media_entity);
}

int media_snd_stream_init(struct snd_usb_substream *subs, struct snd_pcm *pcm,
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
		return -ENODEV;

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

void media_snd_stream_delete(struct snd_usb_substream *subs)
{
	struct media_ctl *mctl = subs->media_ctl;

	if (mctl && mctl->media_dev) {
		struct media_device *mdev;

		mdev = subs->stream->chip->media_dev;
		if (mdev && media_devnode_is_registered(&mdev->devnode)) {
			media_devnode_remove(mctl->intf_devnode);
			media_device_unregister_entity(&mctl->media_entity);
			media_entity_cleanup(&mctl->media_entity);
		}
		kfree(mctl);
		subs->media_ctl = NULL;
	}
}

int media_snd_start_pipeline(struct snd_usb_substream *subs)
{
	struct media_ctl *mctl = subs->media_ctl;

	if (mctl)
		return media_snd_enable_source(mctl);
	return 0;
}

void media_snd_stop_pipeline(struct snd_usb_substream *subs)
{
	struct media_ctl *mctl = subs->media_ctl;

	if (mctl)
		media_snd_disable_source(mctl);
}

int media_snd_mixer_init(struct snd_usb_audio *chip)
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

static void media_snd_mixer_delete(struct snd_usb_audio *chip)
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

		if (media_devnode_is_registered(&mdev->devnode)) {
			media_device_unregister_entity(&mctl->media_entity);
			media_entity_cleanup(&mctl->media_entity);
		}
		kfree(mctl);
		mixer->media_mixer_ctl = NULL;
	}
	if (media_devnode_is_registered(&mdev->devnode))
		media_devnode_remove(chip->ctl_intf_media_devnode);
	chip->ctl_intf_media_devnode = NULL;
}

int media_snd_device_create(struct snd_usb_audio *chip,
			struct usb_interface *iface)
{
	struct media_device *mdev;
	struct usb_device *usbdev = interface_to_usbdev(iface);
	int ret;

	mdev = media_device_get_devres(&usbdev->dev);
	if (!mdev)
		return -ENOMEM;
	if (!mdev->dev) {
		/* register media device */
		mdev->dev = &usbdev->dev;
		if (usbdev->product)
			strlcpy(mdev->model, usbdev->product,
				sizeof(mdev->model));
		if (usbdev->serial)
			strlcpy(mdev->serial, usbdev->serial,
				sizeof(mdev->serial));
		strcpy(mdev->bus_info, usbdev->devpath);
		mdev->hw_revision = le16_to_cpu(usbdev->descriptor.bcdDevice);
		media_device_init(mdev);
	}
	if (!media_devnode_is_registered(&mdev->devnode)) {
		ret = media_device_register(mdev);
		if (ret) {
			dev_err(&usbdev->dev,
				"Couldn't register media device. Error: %d\n",
				ret);
			return ret;
		}
	}

	/* save media device - avoid lookups */
	chip->media_dev = mdev;

	/* Create media entities for mixer and control dev */
	ret = media_snd_mixer_init(chip);
	if (ret) {
		dev_err(&usbdev->dev,
			"Couldn't create media mixer entities. Error: %d\n",
			ret);

		/* clear saved media_dev */
		chip->media_dev = NULL;

		return ret;
	}
	return 0;
}

void media_snd_device_delete(struct snd_usb_audio *chip)
{
	struct media_device *mdev = chip->media_dev;

	media_snd_mixer_delete(chip);

	if (mdev) {
		if (media_devnode_is_registered(&mdev->devnode))
			media_device_unregister(mdev);
		chip->media_dev = NULL;
	}
}
