// SPDX-License-Identifier: GPL-2.0
/*
 * Audient EVO driver for ALSA
 * Copyright (C) 2026 Christian Ruppert <arc@gmx.li>
 *
 * Based on the work of Ivan Hazucha for audient-evo-py.
 * https://github.com/vanzaho/audient-evo-py/
 */


#include <asm/byteorder.h>
#include <linux/usb.h>
#include <linux/usb/audio-v2.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>

#include "usbaudio.h"
#include "mixer.h"
#include "mixer_quirks.h"
#include "helper.h"
#include "mixer_evo.h"


enum snd_evo_type {
	SND_EVO_TYPE_MONITOR = 0,
	SND_EVO_TYPE_PHANTOM,
	SND_EVO_TYPE_MUTE,
	SND_EVO_TYPE_MUTE_OUT,
	SND_EVO_TYPE_MIXER,
	SND_EVO_TYPE_NCTYPES
};

static const struct snd_evo_ctrl_type {
	snd_ctl_elem_type_t type;
	bool invert;
	union {
		int integer;
	} min;
	union {
		int integer;
	} max;
	u16 wValue;
	u16 wIndex;
} snd_evo_ctypes[SND_EVO_TYPE_NCTYPES] = {
	[SND_EVO_TYPE_MONITOR] = {
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.min.integer = 0,
		.max.integer = 127,
		.wValue = 0x0000,
		.wIndex = 0x3800,
	},
	[SND_EVO_TYPE_PHANTOM] = {
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.invert = false,
		.min.integer = 0,
		.max.integer = 1,
		.wValue = 0x0000,
		.wIndex = 0x3A00,
	},
	[SND_EVO_TYPE_MUTE] = {
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.invert = true,
		.min.integer = 0,
		.max.integer = 1,
		.wValue = 0x0200,
		.wIndex = 0x3A00,
	},
	[SND_EVO_TYPE_MUTE_OUT] = {
		.type = SNDRV_CTL_ELEM_TYPE_BOOLEAN,
		.invert = true,
		.min.integer = 0,
		.max.integer = 1,
		.wValue = 0x0100,
		.wIndex = 0x3B00,
	},
	[SND_EVO_TYPE_MIXER] = {
		.type = SNDRV_CTL_ELEM_TYPE_INTEGER,
		.min.integer = -128 * 256,
		.max.integer = 6 * 256,
		.wValue = 0x0100,
		.wIndex = 0x3C00,
	},
};

#define SND_EVO_FLAGS_DIRECTION (1L<<0)
#define SND_EVO_FLAGS_CAPTURE (1L<<0)
#define SND_EVO_FLAGS_PLAYBACK (0L<<0)

struct snd_evo_ctrl {
	char *basename;
	enum snd_evo_type type;
	unsigned int channels;
	unsigned int hwchannels;
	unsigned int val_offs;
	unsigned long flags;
};

static const struct snd_evo_ctrl evo4_controls[] = {
	{
		.basename = "Master",
		.type = SND_EVO_TYPE_MUTE_OUT,
		.channels = 1,
		.hwchannels = 1,
		.val_offs = 0,
		.flags = SND_EVO_FLAGS_PLAYBACK,
	},
	{
		.basename = "Mic",
		.type = SND_EVO_TYPE_MUTE,
		.channels = 4,
		.hwchannels = 2,
		.val_offs = 0,
		.flags = SND_EVO_FLAGS_PLAYBACK,
	},
	{
		.basename = "Monitor",
		.type = SND_EVO_TYPE_MONITOR,
		.channels = 1,
		.hwchannels = 1,
		.val_offs = 0,
		.flags = SND_EVO_FLAGS_PLAYBACK,
	},
	{
		.basename = "Mic 1",
		.type = SND_EVO_TYPE_MIXER,
		.channels = 2,
		.hwchannels = 2,
		.val_offs = 0,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Mic 2",
		.type = SND_EVO_TYPE_MIXER,
		.channels = 2,
		.hwchannels = 2,
		.val_offs = 2,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Master Left",
		.type = SND_EVO_TYPE_MIXER,
		.channels = 2,
		.hwchannels = 2,
		.val_offs = 4,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Master Right",
		.type = SND_EVO_TYPE_MIXER,
		.channels = 2,
		.hwchannels = 2,
		.val_offs = 6,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Loopback Left",
		.type = SND_EVO_TYPE_MIXER,
		.channels = 2,
		.hwchannels = 2,
		.val_offs = 8,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Loopback Right",
		.type = SND_EVO_TYPE_MIXER,
		.channels = 2,
		.hwchannels = 2,
		.val_offs = 10,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Mic 1 Phantom",
		.type = SND_EVO_TYPE_PHANTOM,
		.channels = 1,
		.hwchannels = 1,
		.val_offs = 0,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{
		.basename = "Mic 2 Phantom",
		.type = SND_EVO_TYPE_PHANTOM,
		.channels = 1,
		.hwchannels = 1,
		.val_offs = 1,
		.flags = SND_EVO_FLAGS_CAPTURE,
	},
	{ 0 }, /* sentinel */
};

static const struct evo_devinfo {
	u32 usb_id;
	const struct snd_evo_ctrl *controls;
} evo_devinfo[] = {
	{
		.usb_id = USB_ID(USB_AUDIENT_VID, USB_EVO4_PID),
		.controls = evo4_controls,
	},
	{
		.usb_id = USB_ID(USB_AUDIENT_VID, USB_EVO4_PID),
		.controls = NULL,
	},
	{ 0 } /* sentinel */
};


static inline unsigned long snd_evo_kctl_priv(enum snd_evo_type type,
					      u8 valoffs, u8 hwchannels)
{
	unsigned long type_l = type & 0xFF;
	unsigned long valoffs_l = valoffs & 0xFF;
	unsigned long hwchannels_l = hwchannels & 0xFF;

	return (hwchannels_l << 16) | (valoffs_l << 8) | type_l;
}

static __always_inline
const struct snd_evo_ctrl_type *snd_evo_kctl_type(struct snd_kcontrol *kctl)
{
	return &snd_evo_ctypes[kctl->private_value & 0xFF];
}

static __always_inline u16 snd_evo_kctl_valoffs(struct snd_kcontrol *kctl)
{
	return (kctl->private_value >> 8) & 0xFF;
}

static __always_inline u16 snd_evo_kctl_hwchannels(struct snd_kcontrol *kctl)
{
	return (kctl->private_value >> 16) & 0xFF;
}

static inline u16 snd_evo_kctl_channels(struct snd_kcontrol *kctl)
{
	struct usb_mixer_elem_info *info = kctl->private_data;

	return info->channels;
}


#define EVO_REQ_CUR (0x01)
#define EVO_REQTYPE_SET (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define EVO_REQTYPE_GET (USB_DIR_IN  | USB_TYPE_CLASS | USB_RECIP_INTERFACE)

static inline int snd_evo_get_cur(struct usb_mixer_interface *mixer,
				  u16 value, u16 index,
				  void *buf, size_t len)
{
	struct usb_device *const dev = mixer->chip->dev;

	return snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			       EVO_REQ_CUR, EVO_REQTYPE_GET,
			       value, index, buf, len);
}

static inline int snd_evo_set_cur(struct usb_mixer_interface *mixer,
				  u16 value, u16 index,
				  void *buf, size_t len)
{
	struct usb_device *const dev = mixer->chip->dev;

	return snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
			       EVO_REQ_CUR, EVO_REQTYPE_SET,
			       value, index, buf, len);
}


static int snd_ctl_evo_boolean_get(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ctl_val)
{
	struct usb_mixer_elem_list *const list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *const mixer = list->mixer;

	const unsigned int nchan = snd_evo_kctl_channels(kctl);
	const unsigned int hwchan = snd_evo_kctl_hwchannels(kctl);
	const struct snd_evo_ctrl_type *const ctype = snd_evo_kctl_type(kctl);
	const u16 value = ctype->wValue + snd_evo_kctl_valoffs(kctl);
	const u16 index = ctype->wIndex;

	int i;

	for (i = 0; i < hwchan; i++) {
		u32 buf;
		int ret;

		ret = snd_evo_get_cur(mixer, value + i, index,
				      &buf, sizeof(buf));
		if (ret < 0)
			return ret;

		ctl_val->value.integer.value[i] = le32_to_cpu(buf) ? 1 : 0;

		if (ctype->invert)
			ctl_val->value.integer.value[i] ^= 0x1;
	}

	for (; i < nchan; i++)
		ctl_val->value.integer.value[i] = ctype->invert
							? ctype->max.integer
							: ctype->min.integer;

	return 0;
}

static int snd_ctl_evo_boolean_set(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ctl_val)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *mixer = list->mixer;

	const unsigned int hwchan = snd_evo_kctl_hwchannels(kctl);
	const struct snd_evo_ctrl_type *const ctype = snd_evo_kctl_type(kctl);
	const u16 value = ctype->wValue + snd_evo_kctl_valoffs(kctl);
	const u16 index = ctype->wIndex;

	int changed = 0;
	int i;

	for (i = 0; i < hwchan; i++) {
		u32 rbuf;
		u32 buf = cpu_to_le32(ctl_val->value.integer.value[i] ? 1 : 0);
		int ret;

		if (ctype->invert)
			buf ^= 0x1;

		ret = snd_evo_get_cur(mixer, value + i, index,
				      &rbuf, sizeof(rbuf));
		if (ret < 0)
			return ret;

		if (rbuf != buf) {
			changed = 1;

			ret = snd_evo_set_cur(mixer, value + i, index,
					      &buf, sizeof(buf));
			if (ret < 0)
				return ret;
		}
	}

	return changed;
}

static int snd_ctl_evo_integer_get(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ctl_val)
{
	struct usb_mixer_elem_list *const list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *const mixer = list->mixer;

	const unsigned int nchan = snd_evo_kctl_channels(kctl);
	const unsigned int hwchan = snd_evo_kctl_hwchannels(kctl);
	const struct snd_evo_ctrl_type *const ctype = snd_evo_kctl_type(kctl);
	const u16 value = ctype->wValue + snd_evo_kctl_valoffs(kctl);
	const u16 index = ctype->wIndex;

	int i;

	for (i = 0; i < hwchan; i++) {
		u16 buf;
		s16 val;
		int ret;

		ret = snd_evo_get_cur(mixer, value + i, index,
				      &buf, sizeof(buf));
		if (ret < 0)
			return ret;

		val = le16_to_cpu(buf);
		ctl_val->value.integer.value[i] = val;
	}

	for (; i < nchan; i++)
		ctl_val->value.integer.value[i] = ctype->min.integer;

	return 0;
}

static int snd_ctl_evo_integer_set(struct snd_kcontrol *kctl,
				   struct snd_ctl_elem_value *ctl_val)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *mixer = list->mixer;

	const unsigned int hwchan = snd_evo_kctl_hwchannels(kctl);
	const struct snd_evo_ctrl_type *const ctype = snd_evo_kctl_type(kctl);
	const u16 value = ctype->wValue + snd_evo_kctl_valoffs(kctl);
	const u16 index = ctype->wIndex;

	int changed = 0;
	int i;

	for (i = 0; i < hwchan; i++) {
		s16 rbuf;
		s16 buf = cpu_to_le16(ctl_val->value.integer.value[i]);
		int ret;

		ret = snd_evo_get_cur(mixer, value + i, index,
				      &rbuf, sizeof(rbuf));
		if (ret < 0)
			return ret;

		if (rbuf != buf) {
			changed = 1;

			ret = snd_evo_set_cur(mixer, value + i, index,
					      &buf, sizeof(buf));
			if (ret < 0)
				return ret;
		}
	}

	return changed;
}

static int snd_ctl_evo_info(struct snd_kcontrol *kctl,
			    struct snd_ctl_elem_info *uinfo)
{
	const struct snd_evo_ctrl_type *const ctype = snd_evo_kctl_type(kctl);

	uinfo->type = ctype->type;
	uinfo->access = SNDRV_CTL_ELEM_ACCESS_READWRITE
			| SNDRV_CTL_ELEM_ACCESS_VOLATILE;
	uinfo->count = snd_evo_kctl_channels(kctl);
	uinfo->value.integer.min = ctype->min.integer;
	uinfo->value.integer.max = ctype->max.integer;
	return 0;
}


static int snd_evo_add_ctrl(struct usb_mixer_interface *mixer,
			    const struct snd_evo_ctrl *ctrl)
{
	const struct snd_evo_ctrl_type *ctype = &snd_evo_ctypes[ctrl->type];
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	struct snd_kcontrol_new knew = { 0 };
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *elem;

	knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	knew.name = name;
	knew.info = snd_ctl_evo_info;
	knew.private_value = snd_evo_kctl_priv(ctrl->type, ctrl->val_offs,
					       ctrl->hwchannels);

	switch (ctype->type) {
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		knew.get = snd_ctl_evo_integer_get;
		knew.put = snd_ctl_evo_integer_set;
		break;
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		knew.get = snd_ctl_evo_boolean_get;
		knew.put = snd_ctl_evo_boolean_set;
		break;
	default:
		return -EINVAL;
	}

	snprintf(name, sizeof(name), "%s %s %s", ctrl->basename,
		 ((ctrl->flags & SND_EVO_FLAGS_DIRECTION)
			== SND_EVO_FLAGS_CAPTURE) ? "Capture" : "Playback",
		 (ctype->type == SNDRV_CTL_ELEM_TYPE_BOOLEAN)
			? "Switch" : "Volume");

	elem = kzalloc_obj(*elem);
	if (!elem)
		return -ENOMEM;

	elem->head.mixer = mixer;
	elem->channels = ctrl->channels;

	kctl = snd_ctl_new1(&knew, elem);
	if (!kctl) {
		kfree(elem);
		return -ENOMEM;
	}

	kctl->private_free = snd_usb_mixer_elem_free;

	return snd_usb_mixer_add_control(&elem->head, kctl);
}


/* called from mixer_quirks.c */
int snd_evo_controls_create(struct usb_mixer_interface *mixer)
{
	const struct evo_devinfo *info;
	const struct snd_evo_ctrl *ctrl;

	for (info = evo_devinfo; info->controls; info++)
		if (info->usb_id == mixer->chip->usb_id)
			break;

	if (!info->controls)
		return -ENODEV;

	for (ctrl = info->controls; ctrl->basename; ctrl++) {
		const int ret = snd_evo_add_ctrl(mixer, ctrl);

		if (ret < 0)
			return ret;
	}

	return 0;
}
