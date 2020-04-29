// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Scarlett Driver for ALSA
 *
 *   Copyright (c) 2013 by Tobias Hoffmann
 *   Copyright (c) 2013 by Robin Gareus <robin at gareus.org>
 *   Copyright (c) 2002 by Takashi Iwai <tiwai at suse.de>
 *   Copyright (c) 2014 by Chris J Arges <chris.j.arges at canonical.com>
 *
 *   Many codes borrowed from audio.c by
 *	    Alan Cox (alan at lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer at ife.ee.ethz.ch)
 *
 *   Code cleanup:
 *   David Henningsson <david.henningsson at canonical.com>
 */

/*
 * Rewritten and extended to support more models, e.g. Scarlett 18i8.
 *
 * Auto-detection via UAC2 is not feasible to properly discover the vast
 * majority of features. It's related to both Linux/ALSA's UAC2 as well as
 * Focusrite's implementation of it. Eventually quirks may be sufficient but
 * right now it's a major headache to work arount these things.
 *
 * NB. Neither the OSX nor the win driver provided by Focusrite performs
 * discovery, they seem to operate the same as this driver.
 */

/* Mixer Interface for the Focusrite Scarlett 18i6 audio interface.
 *
 * The protocol was reverse engineered by looking at communication between
 * Scarlett MixControl (v 1.2.128.0) and the Focusrite(R) Scarlett 18i6
 * (firmware v305) using wireshark and usbmon in January 2013.
 * Extended in July 2013.
 *
 * this mixer gives complete access to all features of the device:
 *  - change Impedance of inputs (Line-in, Mic / Instrument, Hi-Z)
 *  - select clock source
 *  - dynamic input to mixer-matrix assignment
 *  - 18 x 6 mixer-matrix gain stages
 *  - bus routing & volume control
 *  - automatic re-initialization on connect if device was power-cycled
 *
 * USB URB commands overview (bRequest = 0x01 = UAC2_CS_CUR)
 * wIndex
 * 0x01 Analog Input line/instrument impedance switch, wValue=0x0901 +
 *      channel, data=Line/Inst (2bytes)
 *      pad (-10dB) switch, wValue=0x0b01 + channel, data=Off/On (2bytes)
 *      ?? wValue=0x0803/04, ?? (2bytes)
 * 0x0a Master Volume, wValue=0x0200+bus[0:all + only 1..4?] data(2bytes)
 *      Bus Mute/Unmute wValue=0x0100+bus[0:all + only 1..4?], data(2bytes)
 * 0x28 Clock source, wValue=0x0100, data={1:int,2:spdif,3:adat} (1byte)
 * 0x29 Set Sample-rate, wValue=0x0100, data=sample-rate(4bytes)
 * 0x32 Mixer mux, wValue=0x0600 + mixer-channel, data=input-to-connect(2bytes)
 * 0x33 Output mux, wValue=bus, data=input-to-connect(2bytes)
 * 0x34 Capture mux, wValue=0...18, data=input-to-connect(2bytes)
 * 0x3c Matrix Mixer gains, wValue=mixer-node  data=gain(2bytes)
 *      ?? [sometimes](4bytes, e.g 0x000003be 0x000003bf ...03ff)
 *
 * USB reads: (i.e. actually issued by original software)
 * 0x01 wValue=0x0901+channel (1byte!!), wValue=0x0b01+channed (1byte!!)
 * 0x29 wValue=0x0100 sample-rate(4bytes)
 *      wValue=0x0200 ?? 1byte (only once)
 * 0x2a wValue=0x0100 ?? 4bytes, sample-rate2 ??
 *
 * USB reads with bRequest = 0x03 = UAC2_CS_MEM
 * 0x3c wValue=0x0002 1byte: sync status (locked=1)
 *      wValue=0x0000 18*2byte: peak meter (inputs)
 *      wValue=0x0001 8(?)*2byte: peak meter (mix)
 *      wValue=0x0003 6*2byte: peak meter (pcm/daw)
 *
 * USB write with bRequest = 0x03
 * 0x3c Save settings to hardware: wValue=0x005a, data=0xa5
 *
 *
 * <ditaa>
 *  /--------------\    18chn            6chn    /--------------\
 *  | Hardware  in +--+-------\        /------+--+ ALSA PCM out |
 *  \--------------/  |       |        |      |  \--------------/
 *                    |       |        |      |
 *                    |       v        v      |
 *                    |   +---------------+   |
 *                    |    \ Matrix  Mux /    |
 *                    |     +-----+-----+     |
 *                    |           |           |
 *                    |           | 18chn     |
 *                    |           v           |
 *                    |     +-----------+     |
 *                    |     | Mixer     |     |
 *                    |     |    Matrix |     |
 *                    |     |           |     |
 *                    |     | 18x6 Gain |     |
 *                    |     |   stages  |     |
 *                    |     +-----+-----+     |
 *                    |           |           |
 *                    |           |           |
 *                    | 18chn     | 6chn      | 6chn
 *                    v           v           v
 *                    =========================
 *             +---------------+     +--—------------+
 *              \ Output  Mux /       \ Capture Mux /
 *               +-----+-----+         +-----+-----+
 *                     |                     |
 *                     | 6chn                |
 *                     v                     |
 *              +-------------+              |
 *              | Master Gain |              |
 *              +------+------+              |
 *                     |                     |
 *                     | 6chn                | 18chn
 *                     | (3 stereo pairs)    |
 *  /--------------\   |                     |   /--------------\
 *  | Hardware out |<--/                     \-->| ALSA PCM  in |
 *  \--------------/                             \--------------/
 * </ditaa>
 *
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio-v2.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>

#include "usbaudio.h"
#include "mixer.h"
#include "helper.h"
#include "power.h"

#include "mixer_scarlett.h"

/* some gui mixers can't handle negative ctl values */
#define SND_SCARLETT_LEVEL_BIAS 128
#define SND_SCARLETT_MATRIX_IN_MAX 18
#define SND_SCARLETT_CONTROLS_MAX 10
#define SND_SCARLETT_OFFSETS_MAX 5

enum {
	SCARLETT_OUTPUTS,
	SCARLETT_SWITCH_IMPEDANCE,
	SCARLETT_SWITCH_PAD,
	SCARLETT_SWITCH_GAIN,
};

enum {
	SCARLETT_OFFSET_PCM = 0,
	SCARLETT_OFFSET_ANALOG = 1,
	SCARLETT_OFFSET_SPDIF = 2,
	SCARLETT_OFFSET_ADAT = 3,
	SCARLETT_OFFSET_MIX = 4,
};

struct scarlett_mixer_elem_enum_info {
	int start;
	int len;
	int offsets[SND_SCARLETT_OFFSETS_MAX];
	char const * const *names;
};

struct scarlett_mixer_control {
	unsigned char num;
	unsigned char type;
	const char *name;
};

struct scarlett_device_info {
	int matrix_in;
	int matrix_out;
	int input_len;
	int output_len;

	struct scarlett_mixer_elem_enum_info opt_master;
	struct scarlett_mixer_elem_enum_info opt_matrix;

	/* initial values for matrix mux */
	int matrix_mux_init[SND_SCARLETT_MATRIX_IN_MAX];

	int num_controls;	/* number of items in controls */
	const struct scarlett_mixer_control controls[SND_SCARLETT_CONTROLS_MAX];
};

/********************** Enum Strings *************************/

static const struct scarlett_mixer_elem_enum_info opt_pad = {
	.start = 0,
	.len = 2,
	.offsets = {},
	.names = (char const * const []){
		"0dB", "-10dB"
	}
};

static const struct scarlett_mixer_elem_enum_info opt_gain = {
	.start = 0,
	.len = 2,
	.offsets = {},
	.names = (char const * const []){
		"Lo", "Hi"
	}
};

static const struct scarlett_mixer_elem_enum_info opt_impedance = {
	.start = 0,
	.len = 2,
	.offsets = {},
	.names = (char const * const []){
		"Line", "Hi-Z"
	}
};

static const struct scarlett_mixer_elem_enum_info opt_clock = {
	.start = 1,
	.len = 3,
	.offsets = {},
	.names = (char const * const []){
		"Internal", "SPDIF", "ADAT"
	}
};

static const struct scarlett_mixer_elem_enum_info opt_sync = {
	.start = 0,
	.len = 2,
	.offsets = {},
	.names = (char const * const []){
		"No Lock", "Locked"
	}
};

static int scarlett_ctl_switch_info(struct snd_kcontrol *kctl,
		struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int scarlett_ctl_switch_get(struct snd_kcontrol *kctl,
		struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	int i, err, val;

	for (i = 0; i < elem->channels; i++) {
		err = snd_usb_get_cur_mix_value(elem, i, i, &val);
		if (err < 0)
			return err;

		val = !val; /* invert mute logic for mixer */
		ucontrol->value.integer.value[i] = val;
	}

	return 0;
}

static int scarlett_ctl_switch_put(struct snd_kcontrol *kctl,
		struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	int i, changed = 0;
	int err, oval, val;

	for (i = 0; i < elem->channels; i++) {
		err = snd_usb_get_cur_mix_value(elem, i, i, &oval);
		if (err < 0)
			return err;

		val = ucontrol->value.integer.value[i];
		val = !val;
		if (oval != val) {
			err = snd_usb_set_cur_mix_value(elem, i, i, val);
			if (err < 0)
				return err;

			changed = 1;
		}
	}

	return changed;
}

static int scarlett_ctl_resume(struct usb_mixer_elem_list *list)
{
	struct usb_mixer_elem_info *elem = mixer_elem_list_to_info(list);
	int i;

	for (i = 0; i < elem->channels; i++)
		if (elem->cached & (1 << i))
			snd_usb_set_cur_mix_value(elem, i, i,
						  elem->cache_val[i]);
	return 0;
}

static int scarlett_ctl_info(struct snd_kcontrol *kctl,
			     struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = elem->channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = (int)kctl->private_value +
		SND_SCARLETT_LEVEL_BIAS;
	uinfo->value.integer.step = 1;
	return 0;
}

static int scarlett_ctl_get(struct snd_kcontrol *kctl,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	int i, err, val;

	for (i = 0; i < elem->channels; i++) {
		err = snd_usb_get_cur_mix_value(elem, i, i, &val);
		if (err < 0)
			return err;

		val = clamp(val / 256, -128, (int)kctl->private_value) +
				    SND_SCARLETT_LEVEL_BIAS;
		ucontrol->value.integer.value[i] = val;
	}

	return 0;
}

static int scarlett_ctl_put(struct snd_kcontrol *kctl,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	int i, changed = 0;
	int err, oval, val;

	for (i = 0; i < elem->channels; i++) {
		err = snd_usb_get_cur_mix_value(elem, i, i, &oval);
		if (err < 0)
			return err;

		val = ucontrol->value.integer.value[i] -
			SND_SCARLETT_LEVEL_BIAS;
		val = val * 256;
		if (oval != val) {
			err = snd_usb_set_cur_mix_value(elem, i, i, val);
			if (err < 0)
				return err;

			changed = 1;
		}
	}

	return changed;
}

static void scarlett_generate_name(int i, char *dst, int offsets[])
{
	if (i > offsets[SCARLETT_OFFSET_MIX])
		sprintf(dst, "Mix %c",
			'A'+(i - offsets[SCARLETT_OFFSET_MIX] - 1));
	else if (i > offsets[SCARLETT_OFFSET_ADAT])
		sprintf(dst, "ADAT %d", i - offsets[SCARLETT_OFFSET_ADAT]);
	else if (i > offsets[SCARLETT_OFFSET_SPDIF])
		sprintf(dst, "SPDIF %d", i - offsets[SCARLETT_OFFSET_SPDIF]);
	else if (i > offsets[SCARLETT_OFFSET_ANALOG])
		sprintf(dst, "Analog %d", i - offsets[SCARLETT_OFFSET_ANALOG]);
	else if (i > offsets[SCARLETT_OFFSET_PCM])
		sprintf(dst, "PCM %d", i - offsets[SCARLETT_OFFSET_PCM]);
	else
		sprintf(dst, "Off");
}

static int scarlett_ctl_enum_dynamic_info(struct snd_kcontrol *kctl,
					  struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett_mixer_elem_enum_info *opt = elem->private_data;
	unsigned int items = opt->len;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = elem->channels;
	uinfo->value.enumerated.items = items;

	if (uinfo->value.enumerated.item >= items)
		uinfo->value.enumerated.item = items - 1;

	/* generate name dynamically based on item number and offset info */
	scarlett_generate_name(uinfo->value.enumerated.item,
			       uinfo->value.enumerated.name,
			       opt->offsets);

	return 0;
}

static int scarlett_ctl_enum_info(struct snd_kcontrol *kctl,
				  struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett_mixer_elem_enum_info *opt = elem->private_data;

	return snd_ctl_enum_info(uinfo, elem->channels, opt->len,
				 (const char * const *)opt->names);
}

static int scarlett_ctl_enum_get(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett_mixer_elem_enum_info *opt = elem->private_data;
	int err, val;

	err = snd_usb_get_cur_mix_value(elem, 0, 0, &val);
	if (err < 0)
		return err;

	val = clamp(val - opt->start, 0, opt->len-1);

	ucontrol->value.enumerated.item[0] = val;

	return 0;
}

static int scarlett_ctl_enum_put(struct snd_kcontrol *kctl,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct scarlett_mixer_elem_enum_info *opt = elem->private_data;
	int err, oval, val;

	err = snd_usb_get_cur_mix_value(elem, 0, 0, &oval);
	if (err < 0)
		return err;

	val = ucontrol->value.integer.value[0];
	val = val + opt->start;
	if (val != oval) {
		snd_usb_set_cur_mix_value(elem, 0, 0, val);
		return 1;
	}
	return 0;
}

static int scarlett_ctl_enum_resume(struct usb_mixer_elem_list *list)
{
	struct usb_mixer_elem_info *elem = mixer_elem_list_to_info(list);

	if (elem->cached)
		snd_usb_set_cur_mix_value(elem, 0, 0, *elem->cache_val);
	return 0;
}

static int scarlett_ctl_meter_get(struct snd_kcontrol *kctl,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *elem = kctl->private_data;
	struct snd_usb_audio *chip = elem->head.mixer->chip;
	unsigned char buf[2 * MAX_CHANNELS] = {0, };
	int wValue = (elem->control << 8) | elem->idx_off;
	int idx = snd_usb_ctrl_intf(chip) | (elem->head.id << 8);
	int err;

	err = snd_usb_ctl_msg(chip->dev,
				usb_rcvctrlpipe(chip->dev, 0),
				UAC2_CS_MEM,
				USB_RECIP_INTERFACE | USB_TYPE_CLASS |
				USB_DIR_IN, wValue, idx, buf, elem->channels);
	if (err < 0)
		return err;

	ucontrol->value.enumerated.item[0] = clamp((int)buf[0], 0, 1);
	return 0;
}

static const struct snd_kcontrol_new usb_scarlett_ctl_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett_ctl_switch_info,
	.get =  scarlett_ctl_switch_get,
	.put =  scarlett_ctl_switch_put,
};

static const DECLARE_TLV_DB_SCALE(db_scale_scarlett_gain, -12800, 100, 0);

static const struct snd_kcontrol_new usb_scarlett_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett_ctl_info,
	.get =  scarlett_ctl_get,
	.put =  scarlett_ctl_put,
	.private_value = 6,  /* max value */
	.tlv = { .p = db_scale_scarlett_gain }
};

static const struct snd_kcontrol_new usb_scarlett_ctl_master = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.name = "",
	.info = scarlett_ctl_info,
	.get =  scarlett_ctl_get,
	.put =  scarlett_ctl_put,
	.private_value = 6,  /* max value */
	.tlv = { .p = db_scale_scarlett_gain }
};

static const struct snd_kcontrol_new usb_scarlett_ctl_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett_ctl_enum_info,
	.get =  scarlett_ctl_enum_get,
	.put =  scarlett_ctl_enum_put,
};

static const struct snd_kcontrol_new usb_scarlett_ctl_dynamic_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "",
	.info = scarlett_ctl_enum_dynamic_info,
	.get =  scarlett_ctl_enum_get,
	.put =  scarlett_ctl_enum_put,
};

static const struct snd_kcontrol_new usb_scarlett_ctl_sync = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.name = "",
	.info = scarlett_ctl_enum_info,
	.get =  scarlett_ctl_meter_get,
};

static int add_new_ctl(struct usb_mixer_interface *mixer,
		       const struct snd_kcontrol_new *ncontrol,
		       usb_mixer_elem_resume_func_t resume,
		       int index, int offset, int num,
		       int val_type, int channels, const char *name,
		       const struct scarlett_mixer_elem_enum_info *opt,
		       struct usb_mixer_elem_info **elem_ret
)
{
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *elem;
	int err;

	elem = kzalloc(sizeof(*elem), GFP_KERNEL);
	if (!elem)
		return -ENOMEM;

	elem->head.mixer = mixer;
	elem->head.resume = resume;
	elem->control = offset;
	elem->idx_off = num;
	elem->head.id = index;
	elem->val_type = val_type;

	elem->channels = channels;

	/* add scarlett_mixer_elem_enum_info struct */
	elem->private_data = (void *)opt;

	kctl = snd_ctl_new1(ncontrol, elem);
	if (!kctl) {
		kfree(elem);
		return -ENOMEM;
	}
	kctl->private_free = snd_usb_mixer_elem_free;

	strlcpy(kctl->id.name, name, sizeof(kctl->id.name));

	err = snd_usb_mixer_add_control(&elem->head, kctl);
	if (err < 0)
		return err;

	if (elem_ret)
		*elem_ret = elem;

	return 0;
}

static int add_output_ctls(struct usb_mixer_interface *mixer,
			   int index, const char *name,
			   const struct scarlett_device_info *info)
{
	int err;
	char mx[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	struct usb_mixer_elem_info *elem;

	/* Add mute switch */
	snprintf(mx, sizeof(mx), "Master %d (%s) Playback Switch",
		index + 1, name);
	err = add_new_ctl(mixer, &usb_scarlett_ctl_switch,
			  scarlett_ctl_resume, 0x0a, 0x01,
			  2*index+1, USB_MIXER_S16, 2, mx, NULL, &elem);
	if (err < 0)
		return err;

	/* Add volume control and initialize to 0 */
	snprintf(mx, sizeof(mx), "Master %d (%s) Playback Volume",
		index + 1, name);
	err = add_new_ctl(mixer, &usb_scarlett_ctl_master,
			  scarlett_ctl_resume, 0x0a, 0x02,
			  2*index+1, USB_MIXER_S16, 2, mx, NULL, &elem);
	if (err < 0)
		return err;

	/* Add L channel source playback enumeration */
	snprintf(mx, sizeof(mx), "Master %dL (%s) Source Playback Enum",
		index + 1, name);
	err = add_new_ctl(mixer, &usb_scarlett_ctl_dynamic_enum,
			  scarlett_ctl_enum_resume, 0x33, 0x00,
			  2*index, USB_MIXER_S16, 1, mx, &info->opt_master,
			  &elem);
	if (err < 0)
		return err;

	/* Add R channel source playback enumeration */
	snprintf(mx, sizeof(mx), "Master %dR (%s) Source Playback Enum",
		index + 1, name);
	err = add_new_ctl(mixer, &usb_scarlett_ctl_dynamic_enum,
			  scarlett_ctl_enum_resume, 0x33, 0x00,
			  2*index+1, USB_MIXER_S16, 1, mx, &info->opt_master,
			  &elem);
	if (err < 0)
		return err;

	return 0;
}

/********************** device-specific config *************************/

/*  untested...  */
static const struct scarlett_device_info s6i6_info = {
	.matrix_in = 18,
	.matrix_out = 8,
	.input_len = 6,
	.output_len = 6,

	.opt_master = {
		.start = -1,
		.len = 27,
		.offsets = {0, 12, 16, 18, 18},
		.names = NULL
	},

	.opt_matrix = {
		.start = -1,
		.len = 19,
		.offsets = {0, 12, 16, 18, 18},
		.names = NULL
	},

	.num_controls = 9,
	.controls = {
		{ .num = 0, .type = SCARLETT_OUTPUTS, .name = "Monitor" },
		{ .num = 1, .type = SCARLETT_OUTPUTS, .name = "Headphone" },
		{ .num = 2, .type = SCARLETT_OUTPUTS, .name = "SPDIF" },
		{ .num = 1, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 1, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 3, .type = SCARLETT_SWITCH_GAIN, .name = NULL},
		{ .num = 4, .type = SCARLETT_SWITCH_GAIN, .name = NULL},
	},

	.matrix_mux_init = {
		12, 13, 14, 15,                 /* Analog -> 1..4 */
		16, 17,                          /* SPDIF -> 5,6 */
		0, 1, 2, 3, 4, 5, 6, 7,     /* PCM[1..12] -> 7..18 */
		8, 9, 10, 11
	}
};

/*  untested...  */
static const struct scarlett_device_info s8i6_info = {
	.matrix_in = 18,
	.matrix_out = 6,
	.input_len = 8,
	.output_len = 6,

	.opt_master = {
		.start = -1,
		.len = 25,
		.offsets = {0, 12, 16, 18, 18},
		.names = NULL
	},

	.opt_matrix = {
		.start = -1,
		.len = 19,
		.offsets = {0, 12, 16, 18, 18},
		.names = NULL
	},

	.num_controls = 7,
	.controls = {
		{ .num = 0, .type = SCARLETT_OUTPUTS, .name = "Monitor" },
		{ .num = 1, .type = SCARLETT_OUTPUTS, .name = "Headphone" },
		{ .num = 2, .type = SCARLETT_OUTPUTS, .name = "SPDIF" },
		{ .num = 1, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 3, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 4, .type = SCARLETT_SWITCH_PAD, .name = NULL},
	},

	.matrix_mux_init = {
		12, 13, 14, 15,                 /* Analog -> 1..4 */
		16, 17,                          /* SPDIF -> 5,6 */
		0, 1, 2, 3, 4, 5, 6, 7,     /* PCM[1..12] -> 7..18 */
		8, 9, 10, 11
	}
};

static const struct scarlett_device_info s18i6_info = {
	.matrix_in = 18,
	.matrix_out = 6,
	.input_len = 18,
	.output_len = 6,

	.opt_master = {
		.start = -1,
		.len = 31,
		.offsets = {0, 6, 14, 16, 24},
		.names = NULL,
	},

	.opt_matrix = {
		.start = -1,
		.len = 25,
		.offsets = {0, 6, 14, 16, 24},
		.names = NULL,
	},

	.num_controls = 5,
	.controls = {
		{ .num = 0, .type = SCARLETT_OUTPUTS, .name = "Monitor" },
		{ .num = 1, .type = SCARLETT_OUTPUTS, .name = "Headphone" },
		{ .num = 2, .type = SCARLETT_OUTPUTS, .name = "SPDIF" },
		{ .num = 1, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
	},

	.matrix_mux_init = {
		 6,  7,  8,  9, 10, 11, 12, 13, /* Analog -> 1..8 */
		16, 17, 18, 19, 20, 21,     /* ADAT[1..6] -> 9..14 */
		14, 15,                          /* SPDIF -> 15,16 */
		0, 1                          /* PCM[1,2] -> 17,18 */
	}
};

static const struct scarlett_device_info s18i8_info = {
	.matrix_in = 18,
	.matrix_out = 8,
	.input_len = 18,
	.output_len = 8,

	.opt_master = {
		.start = -1,
		.len = 35,
		.offsets = {0, 8, 16, 18, 26},
		.names = NULL
	},

	.opt_matrix = {
		.start = -1,
		.len = 27,
		.offsets = {0, 8, 16, 18, 26},
		.names = NULL
	},

	.num_controls = 10,
	.controls = {
		{ .num = 0, .type = SCARLETT_OUTPUTS, .name = "Monitor" },
		{ .num = 1, .type = SCARLETT_OUTPUTS, .name = "Headphone 1" },
		{ .num = 2, .type = SCARLETT_OUTPUTS, .name = "Headphone 2" },
		{ .num = 3, .type = SCARLETT_OUTPUTS, .name = "SPDIF" },
		{ .num = 1, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 1, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 3, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 4, .type = SCARLETT_SWITCH_PAD, .name = NULL},
	},

	.matrix_mux_init = {
		 8,  9, 10, 11, 12, 13, 14, 15, /* Analog -> 1..8 */
		18, 19, 20, 21, 22, 23,     /* ADAT[1..6] -> 9..14 */
		16, 17,                          /* SPDIF -> 15,16 */
		0, 1                          /* PCM[1,2] -> 17,18 */
	}
};

static const struct scarlett_device_info s18i20_info = {
	.matrix_in = 18,
	.matrix_out = 8,
	.input_len = 18,
	.output_len = 20,

	.opt_master = {
		.start = -1,
		.len = 47,
		.offsets = {0, 20, 28, 30, 38},
		.names = NULL
	},

	.opt_matrix = {
		.start = -1,
		.len = 39,
		.offsets = {0, 20, 28, 30, 38},
		.names = NULL
	},

	.num_controls = 10,
	.controls = {
		{ .num = 0, .type = SCARLETT_OUTPUTS, .name = "Monitor" },
		{ .num = 1, .type = SCARLETT_OUTPUTS, .name = "Line 3/4" },
		{ .num = 2, .type = SCARLETT_OUTPUTS, .name = "Line 5/6" },
		{ .num = 3, .type = SCARLETT_OUTPUTS, .name = "Line 7/8" },
		{ .num = 4, .type = SCARLETT_OUTPUTS, .name = "Line 9/10" },
		{ .num = 5, .type = SCARLETT_OUTPUTS, .name = "SPDIF" },
		{ .num = 6, .type = SCARLETT_OUTPUTS, .name = "ADAT 1/2" },
		{ .num = 7, .type = SCARLETT_OUTPUTS, .name = "ADAT 3/4" },
		{ .num = 8, .type = SCARLETT_OUTPUTS, .name = "ADAT 5/6" },
		{ .num = 9, .type = SCARLETT_OUTPUTS, .name = "ADAT 7/8" },
		/*{ .num = 1, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 1, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_IMPEDANCE, .name = NULL},
		{ .num = 2, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 3, .type = SCARLETT_SWITCH_PAD, .name = NULL},
		{ .num = 4, .type = SCARLETT_SWITCH_PAD, .name = NULL},*/
	},

	.matrix_mux_init = {
		20, 21, 22, 23, 24, 25, 26, 27, /* Analog -> 1..8 */
		30, 31, 32, 33, 34, 35,     /* ADAT[1..6] -> 9..14 */
		28, 29,                          /* SPDIF -> 15,16 */
		0, 1                          /* PCM[1,2] -> 17,18 */
	}
};


static int scarlett_controls_create_generic(struct usb_mixer_interface *mixer,
	const struct scarlett_device_info *info)
{
	int i, err;
	char mx[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	const struct scarlett_mixer_control *ctl;
	struct usb_mixer_elem_info *elem;

	/* create master switch and playback volume */
	err = add_new_ctl(mixer, &usb_scarlett_ctl_switch,
			  scarlett_ctl_resume, 0x0a, 0x01, 0,
			  USB_MIXER_S16, 1, "Master Playback Switch", NULL,
			  &elem);
	if (err < 0)
		return err;

	err = add_new_ctl(mixer, &usb_scarlett_ctl_master,
			  scarlett_ctl_resume, 0x0a, 0x02, 0,
			  USB_MIXER_S16, 1, "Master Playback Volume", NULL,
			  &elem);
	if (err < 0)
		return err;

	/* iterate through controls in info struct and create each one */
	for (i = 0; i < info->num_controls; i++) {
		ctl = &info->controls[i];

		switch (ctl->type) {
		case SCARLETT_OUTPUTS:
			err = add_output_ctls(mixer, ctl->num, ctl->name, info);
			if (err < 0)
				return err;
			break;
		case SCARLETT_SWITCH_IMPEDANCE:
			sprintf(mx, "Input %d Impedance Switch", ctl->num);
			err = add_new_ctl(mixer, &usb_scarlett_ctl_enum,
					  scarlett_ctl_enum_resume, 0x01,
					  0x09, ctl->num, USB_MIXER_S16, 1, mx,
					  &opt_impedance, &elem);
			if (err < 0)
				return err;
			break;
		case SCARLETT_SWITCH_PAD:
			sprintf(mx, "Input %d Pad Switch", ctl->num);
			err = add_new_ctl(mixer, &usb_scarlett_ctl_enum,
					  scarlett_ctl_enum_resume, 0x01,
					  0x0b, ctl->num, USB_MIXER_S16, 1, mx,
					  &opt_pad, &elem);
			if (err < 0)
				return err;
			break;
		case SCARLETT_SWITCH_GAIN:
			sprintf(mx, "Input %d Gain Switch", ctl->num);
			err = add_new_ctl(mixer, &usb_scarlett_ctl_enum,
					  scarlett_ctl_enum_resume, 0x01,
					  0x08, ctl->num, USB_MIXER_S16, 1, mx,
					  &opt_gain, &elem);
			if (err < 0)
				return err;
			break;
		}
	}

	return 0;
}

/*
 * Create and initialize a mixer for the Focusrite(R) Scarlett
 */
int snd_scarlett_controls_create(struct usb_mixer_interface *mixer)
{
	int err, i, o;
	char mx[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	const struct scarlett_device_info *info;
	struct usb_mixer_elem_info *elem;
	static char sample_rate_buffer[4] = { '\x80', '\xbb', '\x00', '\x00' };

	/* only use UAC_VERSION_2 */
	if (!mixer->protocol)
		return 0;

	switch (mixer->chip->usb_id) {
	case USB_ID(0x1235, 0x8012):
		info = &s6i6_info;
		break;
	case USB_ID(0x1235, 0x8002):
		info = &s8i6_info;
		break;
	case USB_ID(0x1235, 0x8004):
		info = &s18i6_info;
		break;
	case USB_ID(0x1235, 0x8014):
		info = &s18i8_info;
		break;
	case USB_ID(0x1235, 0x800c):
		info = &s18i20_info;
		break;
	default: /* device not (yet) supported */
		return -EINVAL;
	}

	/* generic function to create controls */
	err = scarlett_controls_create_generic(mixer, info);
	if (err < 0)
		return err;

	/* setup matrix controls */
	for (i = 0; i < info->matrix_in; i++) {
		snprintf(mx, sizeof(mx), "Matrix %02d Input Playback Route",
			 i+1);
		err = add_new_ctl(mixer, &usb_scarlett_ctl_dynamic_enum,
				  scarlett_ctl_enum_resume, 0x32,
				  0x06, i, USB_MIXER_S16, 1, mx,
				  &info->opt_matrix, &elem);
		if (err < 0)
			return err;

		for (o = 0; o < info->matrix_out; o++) {
			sprintf(mx, "Matrix %02d Mix %c Playback Volume", i+1,
				o+'A');
			err = add_new_ctl(mixer, &usb_scarlett_ctl,
					  scarlett_ctl_resume, 0x3c, 0x00,
					  (i << 3) + (o & 0x07), USB_MIXER_S16,
					  1, mx, NULL, &elem);
			if (err < 0)
				return err;

		}
	}

	for (i = 0; i < info->input_len; i++) {
		snprintf(mx, sizeof(mx), "Input Source %02d Capture Route",
			 i+1);
		err = add_new_ctl(mixer, &usb_scarlett_ctl_dynamic_enum,
				  scarlett_ctl_enum_resume, 0x34,
				  0x00, i, USB_MIXER_S16, 1, mx,
				  &info->opt_master, &elem);
		if (err < 0)
			return err;
	}

	/* val_len == 1 needed here */
	err = add_new_ctl(mixer, &usb_scarlett_ctl_enum,
			  scarlett_ctl_enum_resume, 0x28, 0x01, 0,
			  USB_MIXER_U8, 1, "Sample Clock Source",
			  &opt_clock, &elem);
	if (err < 0)
		return err;

	/* val_len == 1 and UAC2_CS_MEM */
	err = add_new_ctl(mixer, &usb_scarlett_ctl_sync, NULL, 0x3c, 0x00, 2,
			  USB_MIXER_U8, 1, "Sample Clock Sync Status",
			  &opt_sync, &elem);
	if (err < 0)
		return err;

	/* initialize sampling rate to 48000 */
	err = snd_usb_ctl_msg(mixer->chip->dev,
		usb_sndctrlpipe(mixer->chip->dev, 0), UAC2_CS_CUR,
		USB_RECIP_INTERFACE | USB_TYPE_CLASS |
		USB_DIR_OUT, 0x0100, snd_usb_ctrl_intf(mixer->chip) |
		(0x29 << 8), sample_rate_buffer, 4);
	if (err < 0)
		return err;

	return err;
}
