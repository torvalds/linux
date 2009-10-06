/*
 *   (Tentative) USB Audio Driver for ALSA
 *
 *   Mixer control part
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "usbaudio.h"

/*
 */

/* ignore error from controls - for debugging */
/* #define IGNORE_CTL_ERROR */

/*
 * Sound Blaster remote control configuration
 *
 * format of remote control data:
 * Extigy:       xx 00
 * Audigy 2 NX:  06 80 xx 00 00 00
 * Live! 24-bit: 06 80 xx yy 22 83
 */
static const struct rc_config {
	u32 usb_id;
	u8  offset;
	u8  length;
	u8  packet_length;
	u8  min_packet_length; /* minimum accepted length of the URB result */
	u8  mute_mixer_id;
	u32 mute_code;
} rc_configs[] = {
	{ USB_ID(0x041e, 0x3000), 0, 1, 2, 1,  18, 0x0013 }, /* Extigy       */
	{ USB_ID(0x041e, 0x3020), 2, 1, 6, 6,  18, 0x0013 }, /* Audigy 2 NX  */
	{ USB_ID(0x041e, 0x3040), 2, 2, 6, 6,  2,  0x6e91 }, /* Live! 24-bit */
	{ USB_ID(0x041e, 0x3048), 2, 2, 6, 6,  2,  0x6e91 }, /* Toshiba SB0500 */
};

struct usb_mixer_interface {
	struct snd_usb_audio *chip;
	unsigned int ctrlif;
	struct list_head list;
	unsigned int ignore_ctl_error;
	struct urb *urb;
	struct usb_mixer_elem_info **id_elems; /* array[256], indexed by unit id */

	/* Sound Blaster remote control stuff */
	const struct rc_config *rc_cfg;
	u32 rc_code;
	wait_queue_head_t rc_waitq;
	struct urb *rc_urb;
	struct usb_ctrlrequest *rc_setup_packet;
	u8 rc_buffer[6];

	u8 audigy2nx_leds[3];
	u8 xonar_u1_status;
};


struct usb_audio_term {
	int id;
	int type;
	int channels;
	unsigned int chconfig;
	int name;
};

struct usbmix_name_map;

struct mixer_build {
	struct snd_usb_audio *chip;
	struct usb_mixer_interface *mixer;
	unsigned char *buffer;
	unsigned int buflen;
	DECLARE_BITMAP(unitbitmap, 256);
	struct usb_audio_term oterm;
	const struct usbmix_name_map *map;
	const struct usbmix_selector_map *selector_map;
};

#define MAX_CHANNELS	10	/* max logical channels */

struct usb_mixer_elem_info {
	struct usb_mixer_interface *mixer;
	struct usb_mixer_elem_info *next_id_elem; /* list of controls with same id */
	struct snd_ctl_elem_id *elem_id;
	unsigned int id;
	unsigned int control;	/* CS or ICN (high byte) */
	unsigned int cmask; /* channel mask bitmap: 0 = master */
	int channels;
	int val_type;
	int min, max, res;
	int cached;
	int cache_val[MAX_CHANNELS];
	u8 initialized;
};


enum {
	USB_FEATURE_NONE = 0,
	USB_FEATURE_MUTE = 1,
	USB_FEATURE_VOLUME,
	USB_FEATURE_BASS,
	USB_FEATURE_MID,
	USB_FEATURE_TREBLE,
	USB_FEATURE_GEQ,
	USB_FEATURE_AGC,
	USB_FEATURE_DELAY,
	USB_FEATURE_BASSBOOST,
	USB_FEATURE_LOUDNESS
};

enum {
	USB_MIXER_BOOLEAN,
	USB_MIXER_INV_BOOLEAN,
	USB_MIXER_S8,
	USB_MIXER_U8,
	USB_MIXER_S16,
	USB_MIXER_U16,
};

enum {
	USB_PROC_UPDOWN = 1,
	USB_PROC_UPDOWN_SWITCH = 1,
	USB_PROC_UPDOWN_MODE_SEL = 2,

	USB_PROC_PROLOGIC = 2,
	USB_PROC_PROLOGIC_SWITCH = 1,
	USB_PROC_PROLOGIC_MODE_SEL = 2,

	USB_PROC_3DENH = 3,
	USB_PROC_3DENH_SWITCH = 1,
	USB_PROC_3DENH_SPACE = 2,

	USB_PROC_REVERB = 4,
	USB_PROC_REVERB_SWITCH = 1,
	USB_PROC_REVERB_LEVEL = 2,
	USB_PROC_REVERB_TIME = 3,
	USB_PROC_REVERB_DELAY = 4,

	USB_PROC_CHORUS = 5,
	USB_PROC_CHORUS_SWITCH = 1,
	USB_PROC_CHORUS_LEVEL = 2,
	USB_PROC_CHORUS_RATE = 3,
	USB_PROC_CHORUS_DEPTH = 4,

	USB_PROC_DCR = 6,
	USB_PROC_DCR_SWITCH = 1,
	USB_PROC_DCR_RATIO = 2,
	USB_PROC_DCR_MAX_AMP = 3,
	USB_PROC_DCR_THRESHOLD = 4,
	USB_PROC_DCR_ATTACK = 5,
	USB_PROC_DCR_RELEASE = 6,
};


/*
 * manual mapping of mixer names
 * if the mixer topology is too complicated and the parsed names are
 * ambiguous, add the entries in usbmixer_maps.c.
 */
#include "usbmixer_maps.c"

/* get the mapped name if the unit matches */
static int check_mapped_name(struct mixer_build *state, int unitid, int control, char *buf, int buflen)
{
	const struct usbmix_name_map *p;

	if (! state->map)
		return 0;

	for (p = state->map; p->id; p++) {
		if (p->id == unitid && p->name &&
		    (! control || ! p->control || control == p->control)) {
			buflen--;
			return strlcpy(buf, p->name, buflen);
		}
	}
	return 0;
}

/* check whether the control should be ignored */
static int check_ignored_ctl(struct mixer_build *state, int unitid, int control)
{
	const struct usbmix_name_map *p;

	if (! state->map)
		return 0;
	for (p = state->map; p->id; p++) {
		if (p->id == unitid && ! p->name &&
		    (! control || ! p->control || control == p->control)) {
			/*
			printk(KERN_DEBUG "ignored control %d:%d\n",
			       unitid, control);
			*/
			return 1;
		}
	}
	return 0;
}

/* get the mapped selector source name */
static int check_mapped_selector_name(struct mixer_build *state, int unitid,
				      int index, char *buf, int buflen)
{
	const struct usbmix_selector_map *p;

	if (! state->selector_map)
		return 0;
	for (p = state->selector_map; p->id; p++) {
		if (p->id == unitid && index < p->count)
			return strlcpy(buf, p->names[index], buflen);
	}
	return 0;
}

/*
 * find an audio control unit with the given unit id
 */
static void *find_audio_control_unit(struct mixer_build *state, unsigned char unit)
{
	unsigned char *p;

	p = NULL;
	while ((p = snd_usb_find_desc(state->buffer, state->buflen, p,
				      USB_DT_CS_INTERFACE)) != NULL) {
		if (p[0] >= 4 && p[2] >= INPUT_TERMINAL && p[2] <= EXTENSION_UNIT && p[3] == unit)
			return p;
	}
	return NULL;
}


/*
 * copy a string with the given id
 */
static int snd_usb_copy_string_desc(struct mixer_build *state, int index, char *buf, int maxlen)
{
	int len = usb_string(state->chip->dev, index, buf, maxlen - 1);
	buf[len] = 0;
	return len;
}

/*
 * convert from the byte/word on usb descriptor to the zero-based integer
 */
static int convert_signed_value(struct usb_mixer_elem_info *cval, int val)
{
	switch (cval->val_type) {
	case USB_MIXER_BOOLEAN:
		return !!val;
	case USB_MIXER_INV_BOOLEAN:
		return !val;
	case USB_MIXER_U8:
		val &= 0xff;
		break;
	case USB_MIXER_S8:
		val &= 0xff;
		if (val >= 0x80)
			val -= 0x100;
		break;
	case USB_MIXER_U16:
		val &= 0xffff;
		break;
	case USB_MIXER_S16:
		val &= 0xffff;
		if (val >= 0x8000)
			val -= 0x10000;
		break;
	}
	return val;
}

/*
 * convert from the zero-based int to the byte/word for usb descriptor
 */
static int convert_bytes_value(struct usb_mixer_elem_info *cval, int val)
{
	switch (cval->val_type) {
	case USB_MIXER_BOOLEAN:
		return !!val;
	case USB_MIXER_INV_BOOLEAN:
		return !val;
	case USB_MIXER_S8:
	case USB_MIXER_U8:
		return val & 0xff;
	case USB_MIXER_S16:
	case USB_MIXER_U16:
		return val & 0xffff;
	}
	return 0; /* not reached */
}

static int get_relative_value(struct usb_mixer_elem_info *cval, int val)
{
	if (! cval->res)
		cval->res = 1;
	if (val < cval->min)
		return 0;
	else if (val >= cval->max)
		return (cval->max - cval->min + cval->res - 1) / cval->res;
	else
		return (val - cval->min) / cval->res;
}

static int get_abs_value(struct usb_mixer_elem_info *cval, int val)
{
	if (val < 0)
		return cval->min;
	if (! cval->res)
		cval->res = 1;
	val *= cval->res;
	val += cval->min;
	if (val > cval->max)
		return cval->max;
	return val;
}


/*
 * retrieve a mixer value
 */

static int get_ctl_value(struct usb_mixer_elem_info *cval, int request, int validx, int *value_ret)
{
	unsigned char buf[2];
	int val_len = cval->val_type >= USB_MIXER_S16 ? 2 : 1;
	int timeout = 10;

	while (timeout-- > 0) {
		if (snd_usb_ctl_msg(cval->mixer->chip->dev,
				    usb_rcvctrlpipe(cval->mixer->chip->dev, 0),
				    request,
				    USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    validx, cval->mixer->ctrlif | (cval->id << 8),
				    buf, val_len, 100) >= val_len) {
			*value_ret = convert_signed_value(cval, snd_usb_combine_bytes(buf, val_len));
			return 0;
		}
	}
	snd_printdd(KERN_ERR "cannot get ctl value: req = %#x, wValue = %#x, wIndex = %#x, type = %d\n",
		    request, validx, cval->mixer->ctrlif | (cval->id << 8), cval->val_type);
	return -EINVAL;
}

static int get_cur_ctl_value(struct usb_mixer_elem_info *cval, int validx, int *value)
{
	return get_ctl_value(cval, GET_CUR, validx, value);
}

/* channel = 0: master, 1 = first channel */
static inline int get_cur_mix_raw(struct usb_mixer_elem_info *cval,
				  int channel, int *value)
{
	return get_ctl_value(cval, GET_CUR, (cval->control << 8) | channel, value);
}

static int get_cur_mix_value(struct usb_mixer_elem_info *cval,
			     int channel, int index, int *value)
{
	int err;

	if (cval->cached & (1 << channel)) {
		*value = cval->cache_val[index];
		return 0;
	}
	err = get_cur_mix_raw(cval, channel, value);
	if (err < 0) {
		if (!cval->mixer->ignore_ctl_error)
			snd_printd(KERN_ERR "cannot get current value for "
				   "control %d ch %d: err = %d\n",
				   cval->control, channel, err);
		return err;
	}
	cval->cached |= 1 << channel;
	cval->cache_val[index] = *value;
	return 0;
}


/*
 * set a mixer value
 */

static int set_ctl_value(struct usb_mixer_elem_info *cval, int request, int validx, int value_set)
{
	unsigned char buf[2];
	int val_len = cval->val_type >= USB_MIXER_S16 ? 2 : 1;
	int timeout = 10;

	value_set = convert_bytes_value(cval, value_set);
	buf[0] = value_set & 0xff;
	buf[1] = (value_set >> 8) & 0xff;
	while (timeout-- > 0)
		if (snd_usb_ctl_msg(cval->mixer->chip->dev,
				    usb_sndctrlpipe(cval->mixer->chip->dev, 0),
				    request,
				    USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    validx, cval->mixer->ctrlif | (cval->id << 8),
				    buf, val_len, 100) >= 0)
			return 0;
	snd_printdd(KERN_ERR "cannot set ctl value: req = %#x, wValue = %#x, wIndex = %#x, type = %d, data = %#x/%#x\n",
		    request, validx, cval->mixer->ctrlif | (cval->id << 8), cval->val_type, buf[0], buf[1]);
	return -EINVAL;
}

static int set_cur_ctl_value(struct usb_mixer_elem_info *cval, int validx, int value)
{
	return set_ctl_value(cval, SET_CUR, validx, value);
}

static int set_cur_mix_value(struct usb_mixer_elem_info *cval, int channel,
			     int index, int value)
{
	int err;
	err = set_ctl_value(cval, SET_CUR, (cval->control << 8) | channel,
			    value);
	if (err < 0)
		return err;
	cval->cached |= 1 << channel;
	cval->cache_val[index] = value;
	return 0;
}

/*
 * TLV callback for mixer volume controls
 */
static int mixer_vol_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			 unsigned int size, unsigned int __user *_tlv)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	DECLARE_TLV_DB_MINMAX(scale, 0, 0);

	if (size < sizeof(scale))
		return -ENOMEM;
	/* USB descriptions contain the dB scale in 1/256 dB unit
	 * while ALSA TLV contains in 1/100 dB unit
	 */
	scale[2] = (convert_signed_value(cval, cval->min) * 100) / 256;
	scale[3] = (convert_signed_value(cval, cval->max) * 100) / 256;
	if (scale[3] <= scale[2]) {
		/* something is wrong; assume it's either from/to 0dB */
		if (scale[2] < 0)
			scale[3] = 0;
		else if (scale[2] > 0)
			scale[2] = 0;
		else /* totally crap, return an error */
			return -EINVAL;
	}
	if (copy_to_user(_tlv, scale, sizeof(scale)))
		return -EFAULT;
	return 0;
}

/*
 * parser routines begin here...
 */

static int parse_audio_unit(struct mixer_build *state, int unitid);


/*
 * check if the input/output channel routing is enabled on the given bitmap.
 * used for mixer unit parser
 */
static int check_matrix_bitmap(unsigned char *bmap, int ich, int och, int num_outs)
{
	int idx = ich * num_outs + och;
	return bmap[idx >> 3] & (0x80 >> (idx & 7));
}


/*
 * add an alsa control element
 * search and increment the index until an empty slot is found.
 *
 * if failed, give up and free the control instance.
 */

static int add_control_to_empty(struct mixer_build *state, struct snd_kcontrol *kctl)
{
	struct usb_mixer_elem_info *cval = kctl->private_data;
	int err;

	while (snd_ctl_find_id(state->chip->card, &kctl->id))
		kctl->id.index++;
	if ((err = snd_ctl_add(state->chip->card, kctl)) < 0) {
		snd_printd(KERN_ERR "cannot add control (err = %d)\n", err);
		return err;
	}
	cval->elem_id = &kctl->id;
	cval->next_id_elem = state->mixer->id_elems[cval->id];
	state->mixer->id_elems[cval->id] = cval;
	return 0;
}


/*
 * get a terminal name string
 */

static struct iterm_name_combo {
	int type;
	char *name;
} iterm_names[] = {
	{ 0x0300, "Output" },
	{ 0x0301, "Speaker" },
	{ 0x0302, "Headphone" },
	{ 0x0303, "HMD Audio" },
	{ 0x0304, "Desktop Speaker" },
	{ 0x0305, "Room Speaker" },
	{ 0x0306, "Com Speaker" },
	{ 0x0307, "LFE" },
	{ 0x0600, "External In" },
	{ 0x0601, "Analog In" },
	{ 0x0602, "Digital In" },
	{ 0x0603, "Line" },
	{ 0x0604, "Legacy In" },
	{ 0x0605, "IEC958 In" },
	{ 0x0606, "1394 DA Stream" },
	{ 0x0607, "1394 DV Stream" },
	{ 0x0700, "Embedded" },
	{ 0x0701, "Noise Source" },
	{ 0x0702, "Equalization Noise" },
	{ 0x0703, "CD" },
	{ 0x0704, "DAT" },
	{ 0x0705, "DCC" },
	{ 0x0706, "MiniDisk" },
	{ 0x0707, "Analog Tape" },
	{ 0x0708, "Phonograph" },
	{ 0x0709, "VCR Audio" },
	{ 0x070a, "Video Disk Audio" },
	{ 0x070b, "DVD Audio" },
	{ 0x070c, "TV Tuner Audio" },
	{ 0x070d, "Satellite Rec Audio" },
	{ 0x070e, "Cable Tuner Audio" },
	{ 0x070f, "DSS Audio" },
	{ 0x0710, "Radio Receiver" },
	{ 0x0711, "Radio Transmitter" },
	{ 0x0712, "Multi-Track Recorder" },
	{ 0x0713, "Synthesizer" },
	{ 0 },
};

static int get_term_name(struct mixer_build *state, struct usb_audio_term *iterm,
			 unsigned char *name, int maxlen, int term_only)
{
	struct iterm_name_combo *names;

	if (iterm->name)
		return snd_usb_copy_string_desc(state, iterm->name, name, maxlen);

	/* virtual type - not a real terminal */
	if (iterm->type >> 16) {
		if (term_only)
			return 0;
		switch (iterm->type >> 16) {
		case SELECTOR_UNIT:
			strcpy(name, "Selector"); return 8;
		case PROCESSING_UNIT:
			strcpy(name, "Process Unit"); return 12;
		case EXTENSION_UNIT:
			strcpy(name, "Ext Unit"); return 8;
		case MIXER_UNIT:
			strcpy(name, "Mixer"); return 5;
		default:
			return sprintf(name, "Unit %d", iterm->id);
		}
	}

	switch (iterm->type & 0xff00) {
	case 0x0100:
		strcpy(name, "PCM"); return 3;
	case 0x0200:
		strcpy(name, "Mic"); return 3;
	case 0x0400:
		strcpy(name, "Headset"); return 7;
	case 0x0500:
		strcpy(name, "Phone"); return 5;
	}

	for (names = iterm_names; names->type; names++)
		if (names->type == iterm->type) {
			strcpy(name, names->name);
			return strlen(names->name);
		}
	return 0;
}


/*
 * parse the source unit recursively until it reaches to a terminal
 * or a branched unit.
 */
static int check_input_term(struct mixer_build *state, int id, struct usb_audio_term *term)
{
	unsigned char *p1;

	memset(term, 0, sizeof(*term));
	while ((p1 = find_audio_control_unit(state, id)) != NULL) {
		term->id = id;
		switch (p1[2]) {
		case INPUT_TERMINAL:
			term->type = combine_word(p1 + 4);
			term->channels = p1[7];
			term->chconfig = combine_word(p1 + 8);
			term->name = p1[11];
			return 0;
		case FEATURE_UNIT:
			id = p1[4];
			break; /* continue to parse */
		case MIXER_UNIT:
			term->type = p1[2] << 16; /* virtual type */
			term->channels = p1[5 + p1[4]];
			term->chconfig = combine_word(p1 + 6 + p1[4]);
			term->name = p1[p1[0] - 1];
			return 0;
		case SELECTOR_UNIT:
			/* call recursively to retrieve the channel info */
			if (check_input_term(state, p1[5], term) < 0)
				return -ENODEV;
			term->type = p1[2] << 16; /* virtual type */
			term->id = id;
			term->name = p1[9 + p1[0] - 1];
			return 0;
		case PROCESSING_UNIT:
		case EXTENSION_UNIT:
			if (p1[6] == 1) {
				id = p1[7];
				break; /* continue to parse */
			}
			term->type = p1[2] << 16; /* virtual type */
			term->channels = p1[7 + p1[6]];
			term->chconfig = combine_word(p1 + 8 + p1[6]);
			term->name = p1[12 + p1[6] + p1[11 + p1[6]]];
			return 0;
		default:
			return -ENODEV;
		}
	}
	return -ENODEV;
}


/*
 * Feature Unit
 */

/* feature unit control information */
struct usb_feature_control_info {
	const char *name;
	unsigned int type;	/* control type (mute, volume, etc.) */
};

static struct usb_feature_control_info audio_feature_info[] = {
	{ "Mute",		USB_MIXER_INV_BOOLEAN },
	{ "Volume",		USB_MIXER_S16 },
	{ "Tone Control - Bass",	USB_MIXER_S8 },
	{ "Tone Control - Mid",		USB_MIXER_S8 },
	{ "Tone Control - Treble",	USB_MIXER_S8 },
	{ "Graphic Equalizer",		USB_MIXER_S8 }, /* FIXME: not implemeted yet */
	{ "Auto Gain Control",	USB_MIXER_BOOLEAN },
	{ "Delay Control",	USB_MIXER_U16 },
	{ "Bass Boost",		USB_MIXER_BOOLEAN },
	{ "Loudness",		USB_MIXER_BOOLEAN },
};


/* private_free callback */
static void usb_mixer_elem_free(struct snd_kcontrol *kctl)
{
	kfree(kctl->private_data);
	kctl->private_data = NULL;
}


/*
 * interface to ALSA control for feature/mixer units
 */

/*
 * retrieve the minimum and maximum values for the specified control
 */
static int get_min_max(struct usb_mixer_elem_info *cval, int default_min)
{
	/* for failsafe */
	cval->min = default_min;
	cval->max = cval->min + 1;
	cval->res = 1;

	if (cval->val_type == USB_MIXER_BOOLEAN ||
	    cval->val_type == USB_MIXER_INV_BOOLEAN) {
		cval->initialized = 1;
	} else {
		int minchn = 0;
		if (cval->cmask) {
			int i;
			for (i = 0; i < MAX_CHANNELS; i++)
				if (cval->cmask & (1 << i)) {
					minchn = i + 1;
					break;
				}
		}
		if (get_ctl_value(cval, GET_MAX, (cval->control << 8) | minchn, &cval->max) < 0 ||
		    get_ctl_value(cval, GET_MIN, (cval->control << 8) | minchn, &cval->min) < 0) {
			snd_printd(KERN_ERR "%d:%d: cannot get min/max values for control %d (id %d)\n",
				   cval->id, cval->mixer->ctrlif, cval->control, cval->id);
			return -EINVAL;
		}
		if (get_ctl_value(cval, GET_RES, (cval->control << 8) | minchn, &cval->res) < 0) {
			cval->res = 1;
		} else {
			int last_valid_res = cval->res;

			while (cval->res > 1) {
				if (set_ctl_value(cval, SET_RES, (cval->control << 8) | minchn, cval->res / 2) < 0)
					break;
				cval->res /= 2;
			}
			if (get_ctl_value(cval, GET_RES, (cval->control << 8) | minchn, &cval->res) < 0)
				cval->res = last_valid_res;
		}
		if (cval->res == 0)
			cval->res = 1;

		/* Additional checks for the proper resolution
		 *
		 * Some devices report smaller resolutions than actually
		 * reacting.  They don't return errors but simply clip
		 * to the lower aligned value.
		 */
		if (cval->min + cval->res < cval->max) {
			int last_valid_res = cval->res;
			int saved, test, check;
			get_cur_mix_raw(cval, minchn, &saved);
			for (;;) {
				test = saved;
				if (test < cval->max)
					test += cval->res;
				else
					test -= cval->res;
				if (test < cval->min || test > cval->max ||
				    set_cur_mix_value(cval, minchn, 0, test) ||
				    get_cur_mix_raw(cval, minchn, &check)) {
					cval->res = last_valid_res;
					break;
				}
				if (test == check)
					break;
				cval->res *= 2;
			}
			set_cur_mix_value(cval, minchn, 0, saved);
		}

		cval->initialized = 1;
	}
	return 0;
}


/* get a feature/mixer unit info */
static int mixer_ctl_feature_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;

	if (cval->val_type == USB_MIXER_BOOLEAN ||
	    cval->val_type == USB_MIXER_INV_BOOLEAN)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = cval->channels;
	if (cval->val_type == USB_MIXER_BOOLEAN ||
	    cval->val_type == USB_MIXER_INV_BOOLEAN) {
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
	} else {
		if (! cval->initialized)
			get_min_max(cval,  0);
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max =
			(cval->max - cval->min + cval->res - 1) / cval->res;
	}
	return 0;
}

/* get the current value from feature/mixer unit */
static int mixer_ctl_feature_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int c, cnt, val, err;

	ucontrol->value.integer.value[0] = cval->min;
	if (cval->cmask) {
		cnt = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (!(cval->cmask & (1 << c)))
				continue;
			err = get_cur_mix_value(cval, c + 1, cnt, &val);
			if (err < 0)
				return cval->mixer->ignore_ctl_error ? 0 : err;
			val = get_relative_value(cval, val);
			ucontrol->value.integer.value[cnt] = val;
			cnt++;
		}
		return 0;
	} else {
		/* master channel */
		err = get_cur_mix_value(cval, 0, 0, &val);
		if (err < 0)
			return cval->mixer->ignore_ctl_error ? 0 : err;
		val = get_relative_value(cval, val);
		ucontrol->value.integer.value[0] = val;
	}
	return 0;
}

/* put the current value to feature/mixer unit */
static int mixer_ctl_feature_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int c, cnt, val, oval, err;
	int changed = 0;

	if (cval->cmask) {
		cnt = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (!(cval->cmask & (1 << c)))
				continue;
			err = get_cur_mix_value(cval, c + 1, cnt, &oval);
			if (err < 0)
				return cval->mixer->ignore_ctl_error ? 0 : err;
			val = ucontrol->value.integer.value[cnt];
			val = get_abs_value(cval, val);
			if (oval != val) {
				set_cur_mix_value(cval, c + 1, cnt, val);
				changed = 1;
			}
			cnt++;
		}
	} else {
		/* master channel */
		err = get_cur_mix_value(cval, 0, 0, &oval);
		if (err < 0)
			return cval->mixer->ignore_ctl_error ? 0 : err;
		val = ucontrol->value.integer.value[0];
		val = get_abs_value(cval, val);
		if (val != oval) {
			set_cur_mix_value(cval, 0, 0, val);
			changed = 1;
		}
	}
	return changed;
}

static struct snd_kcontrol_new usb_feature_unit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later manually */
	.info = mixer_ctl_feature_info,
	.get = mixer_ctl_feature_get,
	.put = mixer_ctl_feature_put,
};


/*
 * build a feature control
 */

static size_t append_ctl_name(struct snd_kcontrol *kctl, const char *str)
{
	return strlcat(kctl->id.name, str, sizeof(kctl->id.name));
}

static void build_feature_ctl(struct mixer_build *state, unsigned char *desc,
			      unsigned int ctl_mask, int control,
			      struct usb_audio_term *iterm, int unitid)
{
	unsigned int len = 0;
	int mapped_name = 0;
	int nameid = desc[desc[0] - 1];
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *cval;

	control++; /* change from zero-based to 1-based value */

	if (control == USB_FEATURE_GEQ) {
		/* FIXME: not supported yet */
		return;
	}

	if (check_ignored_ctl(state, unitid, control))
		return;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (! cval) {
		snd_printk(KERN_ERR "cannot malloc kcontrol\n");
		return;
	}
	cval->mixer = state->mixer;
	cval->id = unitid;
	cval->control = control;
	cval->cmask = ctl_mask;
	cval->val_type = audio_feature_info[control-1].type;
	if (ctl_mask == 0)
		cval->channels = 1;	/* master channel */
	else {
		int i, c = 0;
		for (i = 0; i < 16; i++)
			if (ctl_mask & (1 << i))
				c++;
		cval->channels = c;
	}

	/* get min/max values */
	get_min_max(cval, 0);

	kctl = snd_ctl_new1(&usb_feature_unit_ctl, cval);
	if (! kctl) {
		snd_printk(KERN_ERR "cannot malloc kcontrol\n");
		kfree(cval);
		return;
	}
	kctl->private_free = usb_mixer_elem_free;

	len = check_mapped_name(state, unitid, control, kctl->id.name, sizeof(kctl->id.name));
	mapped_name = len != 0;
	if (! len && nameid)
		len = snd_usb_copy_string_desc(state, nameid, kctl->id.name, sizeof(kctl->id.name));

	switch (control) {
	case USB_FEATURE_MUTE:
	case USB_FEATURE_VOLUME:
		/* determine the control name.  the rule is:
		 * - if a name id is given in descriptor, use it.
		 * - if the connected input can be determined, then use the name
		 *   of terminal type.
		 * - if the connected output can be determined, use it.
		 * - otherwise, anonymous name.
		 */
		if (! len) {
			len = get_term_name(state, iterm, kctl->id.name, sizeof(kctl->id.name), 1);
			if (! len)
				len = get_term_name(state, &state->oterm, kctl->id.name, sizeof(kctl->id.name), 1);
			if (! len)
				len = snprintf(kctl->id.name, sizeof(kctl->id.name),
					       "Feature %d", unitid);
		}
		/* determine the stream direction:
		 * if the connected output is USB stream, then it's likely a
		 * capture stream.  otherwise it should be playback (hopefully :)
		 */
		if (! mapped_name && ! (state->oterm.type >> 16)) {
			if ((state->oterm.type & 0xff00) == 0x0100) {
				len = append_ctl_name(kctl, " Capture");
			} else {
				len = append_ctl_name(kctl, " Playback");
			}
		}
		append_ctl_name(kctl, control == USB_FEATURE_MUTE ?
				" Switch" : " Volume");
		if (control == USB_FEATURE_VOLUME) {
			kctl->tlv.c = mixer_vol_tlv;
			kctl->vd[0].access |= 
				SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
		}
		break;

	default:
		if (! len)
			strlcpy(kctl->id.name, audio_feature_info[control-1].name,
				sizeof(kctl->id.name));
		break;
	}

	/* volume control quirks */
	switch (state->chip->usb_id) {
	case USB_ID(0x0471, 0x0101):
	case USB_ID(0x0471, 0x0104):
	case USB_ID(0x0471, 0x0105):
	case USB_ID(0x0672, 0x1041):
	/* quirk for UDA1321/N101.
	 * note that detection between firmware 2.1.1.7 (N101)
	 * and later 2.1.1.21 is not very clear from datasheets.
	 * I hope that the min value is -15360 for newer firmware --jk
	 */
		if (!strcmp(kctl->id.name, "PCM Playback Volume") &&
		    cval->min == -15616) {
			snd_printk(KERN_INFO
				 "set volume quirk for UDA1321/N101 chip\n");
			cval->max = -256;
		}
		break;

	case USB_ID(0x046d, 0x09a4):
		if (!strcmp(kctl->id.name, "Mic Capture Volume")) {
			snd_printk(KERN_INFO
				"set volume quirk for QuickCam E3500\n");
			cval->min = 6080;
			cval->max = 8768;
			cval->res = 192;
		}
		break;

	}

	snd_printdd(KERN_INFO "[%d] FU [%s] ch = %d, val = %d/%d/%d\n",
		    cval->id, kctl->id.name, cval->channels, cval->min, cval->max, cval->res);
	add_control_to_empty(state, kctl);
}



/*
 * parse a feature unit
 *
 * most of controlls are defined here.
 */
static int parse_audio_feature_unit(struct mixer_build *state, int unitid, unsigned char *ftr)
{
	int channels, i, j;
	struct usb_audio_term iterm;
	unsigned int master_bits, first_ch_bits;
	int err, csize;

	if (ftr[0] < 7 || ! (csize = ftr[5]) || ftr[0] < 7 + csize) {
		snd_printk(KERN_ERR "usbaudio: unit %u: invalid FEATURE_UNIT descriptor\n", unitid);
		return -EINVAL;
	}

	/* parse the source unit */
	if ((err = parse_audio_unit(state, ftr[4])) < 0)
		return err;

	/* determine the input source type and name */
	if (check_input_term(state, ftr[4], &iterm) < 0)
		return -EINVAL;

	channels = (ftr[0] - 7) / csize - 1;

	master_bits = snd_usb_combine_bytes(ftr + 6, csize);
	if (channels > 0)
		first_ch_bits = snd_usb_combine_bytes(ftr + 6 + csize, csize);
	else
		first_ch_bits = 0;
	/* check all control types */
	for (i = 0; i < 10; i++) {
		unsigned int ch_bits = 0;
		for (j = 0; j < channels; j++) {
			unsigned int mask = snd_usb_combine_bytes(ftr + 6 + csize * (j+1), csize);
			if (mask & (1 << i))
				ch_bits |= (1 << j);
		}
		if (ch_bits & 1) /* the first channel must be set (for ease of programming) */
			build_feature_ctl(state, ftr, ch_bits, i, &iterm, unitid);
		if (master_bits & (1 << i))
			build_feature_ctl(state, ftr, 0, i, &iterm, unitid);
	}

	return 0;
}


/*
 * Mixer Unit
 */

/*
 * build a mixer unit control
 *
 * the callbacks are identical with feature unit.
 * input channel number (zero based) is given in control field instead.
 */

static void build_mixer_unit_ctl(struct mixer_build *state, unsigned char *desc,
				 int in_pin, int in_ch, int unitid,
				 struct usb_audio_term *iterm)
{
	struct usb_mixer_elem_info *cval;
	unsigned int input_pins = desc[4];
	unsigned int num_outs = desc[5 + input_pins];
	unsigned int i, len;
	struct snd_kcontrol *kctl;

	if (check_ignored_ctl(state, unitid, 0))
		return;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (! cval)
		return;

	cval->mixer = state->mixer;
	cval->id = unitid;
	cval->control = in_ch + 1; /* based on 1 */
	cval->val_type = USB_MIXER_S16;
	for (i = 0; i < num_outs; i++) {
		if (check_matrix_bitmap(desc + 9 + input_pins, in_ch, i, num_outs)) {
			cval->cmask |= (1 << i);
			cval->channels++;
		}
	}

	/* get min/max values */
	get_min_max(cval, 0);

	kctl = snd_ctl_new1(&usb_feature_unit_ctl, cval);
	if (! kctl) {
		snd_printk(KERN_ERR "cannot malloc kcontrol\n");
		kfree(cval);
		return;
	}
	kctl->private_free = usb_mixer_elem_free;

	len = check_mapped_name(state, unitid, 0, kctl->id.name, sizeof(kctl->id.name));
	if (! len)
		len = get_term_name(state, iterm, kctl->id.name, sizeof(kctl->id.name), 0);
	if (! len)
		len = sprintf(kctl->id.name, "Mixer Source %d", in_ch + 1);
	append_ctl_name(kctl, " Volume");

	snd_printdd(KERN_INFO "[%d] MU [%s] ch = %d, val = %d/%d\n",
		    cval->id, kctl->id.name, cval->channels, cval->min, cval->max);
	add_control_to_empty(state, kctl);
}


/*
 * parse a mixer unit
 */
static int parse_audio_mixer_unit(struct mixer_build *state, int unitid, unsigned char *desc)
{
	struct usb_audio_term iterm;
	int input_pins, num_ins, num_outs;
	int pin, ich, err;

	if (desc[0] < 11 || ! (input_pins = desc[4]) || ! (num_outs = desc[5 + input_pins])) {
		snd_printk(KERN_ERR "invalid MIXER UNIT descriptor %d\n", unitid);
		return -EINVAL;
	}
	/* no bmControls field (e.g. Maya44) -> ignore */
	if (desc[0] <= 10 + input_pins) {
		snd_printdd(KERN_INFO "MU %d has no bmControls field\n", unitid);
		return 0;
	}

	num_ins = 0;
	ich = 0;
	for (pin = 0; pin < input_pins; pin++) {
		err = parse_audio_unit(state, desc[5 + pin]);
		if (err < 0)
			return err;
		err = check_input_term(state, desc[5 + pin], &iterm);
		if (err < 0)
			return err;
		num_ins += iterm.channels;
		for (; ich < num_ins; ++ich) {
			int och, ich_has_controls = 0;

			for (och = 0; och < num_outs; ++och) {
				if (check_matrix_bitmap(desc + 9 + input_pins,
							ich, och, num_outs)) {
					ich_has_controls = 1;
					break;
				}
			}
			if (ich_has_controls)
				build_mixer_unit_ctl(state, desc, pin, ich,
						     unitid, &iterm);
		}
	}
	return 0;
}


/*
 * Processing Unit / Extension Unit
 */

/* get callback for processing/extension unit */
static int mixer_ctl_procunit_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int err, val;

	err = get_cur_ctl_value(cval, cval->control << 8, &val);
	if (err < 0 && cval->mixer->ignore_ctl_error) {
		ucontrol->value.integer.value[0] = cval->min;
		return 0;
	}
	if (err < 0)
		return err;
	val = get_relative_value(cval, val);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

/* put callback for processing/extension unit */
static int mixer_ctl_procunit_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, oval, err;

	err = get_cur_ctl_value(cval, cval->control << 8, &oval);
	if (err < 0) {
		if (cval->mixer->ignore_ctl_error)
			return 0;
		return err;
	}
	val = ucontrol->value.integer.value[0];
	val = get_abs_value(cval, val);
	if (val != oval) {
		set_cur_ctl_value(cval, cval->control << 8, val);
		return 1;
	}
	return 0;
}

/* alsa control interface for processing/extension unit */
static struct snd_kcontrol_new mixer_procunit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later */
	.info = mixer_ctl_feature_info,
	.get = mixer_ctl_procunit_get,
	.put = mixer_ctl_procunit_put,
};


/*
 * predefined data for processing units
 */
struct procunit_value_info {
	int control;
	char *suffix;
	int val_type;
	int min_value;
};

struct procunit_info {
	int type;
	char *name;
	struct procunit_value_info *values;
};

static struct procunit_value_info updown_proc_info[] = {
	{ USB_PROC_UPDOWN_SWITCH, "Switch", USB_MIXER_BOOLEAN },
	{ USB_PROC_UPDOWN_MODE_SEL, "Mode Select", USB_MIXER_U8, 1 },
	{ 0 }
};
static struct procunit_value_info prologic_proc_info[] = {
	{ USB_PROC_PROLOGIC_SWITCH, "Switch", USB_MIXER_BOOLEAN },
	{ USB_PROC_PROLOGIC_MODE_SEL, "Mode Select", USB_MIXER_U8, 1 },
	{ 0 }
};
static struct procunit_value_info threed_enh_proc_info[] = {
	{ USB_PROC_3DENH_SWITCH, "Switch", USB_MIXER_BOOLEAN },
	{ USB_PROC_3DENH_SPACE, "Spaciousness", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info reverb_proc_info[] = {
	{ USB_PROC_REVERB_SWITCH, "Switch", USB_MIXER_BOOLEAN },
	{ USB_PROC_REVERB_LEVEL, "Level", USB_MIXER_U8 },
	{ USB_PROC_REVERB_TIME, "Time", USB_MIXER_U16 },
	{ USB_PROC_REVERB_DELAY, "Delay", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info chorus_proc_info[] = {
	{ USB_PROC_CHORUS_SWITCH, "Switch", USB_MIXER_BOOLEAN },
	{ USB_PROC_CHORUS_LEVEL, "Level", USB_MIXER_U8 },
	{ USB_PROC_CHORUS_RATE, "Rate", USB_MIXER_U16 },
	{ USB_PROC_CHORUS_DEPTH, "Depth", USB_MIXER_U16 },
	{ 0 }
};
static struct procunit_value_info dcr_proc_info[] = {
	{ USB_PROC_DCR_SWITCH, "Switch", USB_MIXER_BOOLEAN },
	{ USB_PROC_DCR_RATIO, "Ratio", USB_MIXER_U16 },
	{ USB_PROC_DCR_MAX_AMP, "Max Amp", USB_MIXER_S16 },
	{ USB_PROC_DCR_THRESHOLD, "Threshold", USB_MIXER_S16 },
	{ USB_PROC_DCR_ATTACK, "Attack Time", USB_MIXER_U16 },
	{ USB_PROC_DCR_RELEASE, "Release Time", USB_MIXER_U16 },
	{ 0 }
};

static struct procunit_info procunits[] = {
	{ USB_PROC_UPDOWN, "Up Down", updown_proc_info },
	{ USB_PROC_PROLOGIC, "Dolby Prologic", prologic_proc_info },
	{ USB_PROC_3DENH, "3D Stereo Extender", threed_enh_proc_info },
	{ USB_PROC_REVERB, "Reverb", reverb_proc_info },
	{ USB_PROC_CHORUS, "Chorus", chorus_proc_info },
	{ USB_PROC_DCR, "DCR", dcr_proc_info },
	{ 0 },
};

/*
 * build a processing/extension unit
 */
static int build_audio_procunit(struct mixer_build *state, int unitid, unsigned char *dsc, struct procunit_info *list, char *name)
{
	int num_ins = dsc[6];
	struct usb_mixer_elem_info *cval;
	struct snd_kcontrol *kctl;
	int i, err, nameid, type, len;
	struct procunit_info *info;
	struct procunit_value_info *valinfo;
	static struct procunit_value_info default_value_info[] = {
		{ 0x01, "Switch", USB_MIXER_BOOLEAN },
		{ 0 }
	};
	static struct procunit_info default_info = {
		0, NULL, default_value_info
	};

	if (dsc[0] < 13 || dsc[0] < 13 + num_ins || dsc[0] < num_ins + dsc[11 + num_ins]) {
		snd_printk(KERN_ERR "invalid %s descriptor (id %d)\n", name, unitid);
		return -EINVAL;
	}

	for (i = 0; i < num_ins; i++) {
		if ((err = parse_audio_unit(state, dsc[7 + i])) < 0)
			return err;
	}

	type = combine_word(&dsc[4]);
	for (info = list; info && info->type; info++)
		if (info->type == type)
			break;
	if (! info || ! info->type)
		info = &default_info;

	for (valinfo = info->values; valinfo->control; valinfo++) {
		/* FIXME: bitmap might be longer than 8bit */
		if (! (dsc[12 + num_ins] & (1 << (valinfo->control - 1))))
			continue;
		if (check_ignored_ctl(state, unitid, valinfo->control))
			continue;
		cval = kzalloc(sizeof(*cval), GFP_KERNEL);
		if (! cval) {
			snd_printk(KERN_ERR "cannot malloc kcontrol\n");
			return -ENOMEM;
		}
		cval->mixer = state->mixer;
		cval->id = unitid;
		cval->control = valinfo->control;
		cval->val_type = valinfo->val_type;
		cval->channels = 1;

		/* get min/max values */
		if (type == USB_PROC_UPDOWN && cval->control == USB_PROC_UPDOWN_MODE_SEL) {
			/* FIXME: hard-coded */
			cval->min = 1;
			cval->max = dsc[15];
			cval->res = 1;
			cval->initialized = 1;
		} else
			get_min_max(cval, valinfo->min_value);

		kctl = snd_ctl_new1(&mixer_procunit_ctl, cval);
		if (! kctl) {
			snd_printk(KERN_ERR "cannot malloc kcontrol\n");
			kfree(cval);
			return -ENOMEM;
		}
		kctl->private_free = usb_mixer_elem_free;

		if (check_mapped_name(state, unitid, cval->control, kctl->id.name, sizeof(kctl->id.name)))
			;
		else if (info->name)
			strlcpy(kctl->id.name, info->name, sizeof(kctl->id.name));
		else {
			nameid = dsc[12 + num_ins + dsc[11 + num_ins]];
			len = 0;
			if (nameid)
				len = snd_usb_copy_string_desc(state, nameid, kctl->id.name, sizeof(kctl->id.name));
			if (! len)
				strlcpy(kctl->id.name, name, sizeof(kctl->id.name));
		}
		append_ctl_name(kctl, " ");
		append_ctl_name(kctl, valinfo->suffix);

		snd_printdd(KERN_INFO "[%d] PU [%s] ch = %d, val = %d/%d\n",
			    cval->id, kctl->id.name, cval->channels, cval->min, cval->max);
		if ((err = add_control_to_empty(state, kctl)) < 0)
			return err;
	}
	return 0;
}


static int parse_audio_processing_unit(struct mixer_build *state, int unitid, unsigned char *desc)
{
	return build_audio_procunit(state, unitid, desc, procunits, "Processing Unit");
}

static int parse_audio_extension_unit(struct mixer_build *state, int unitid, unsigned char *desc)
{
	return build_audio_procunit(state, unitid, desc, NULL, "Extension Unit");
}


/*
 * Selector Unit
 */

/* info callback for selector unit
 * use an enumerator type for routing
 */
static int mixer_ctl_selector_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	char **itemlist = (char **)kcontrol->private_value;

	if (snd_BUG_ON(!itemlist))
		return -EINVAL;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = cval->max;
	if ((int)uinfo->value.enumerated.item >= cval->max)
		uinfo->value.enumerated.item = cval->max - 1;
	strcpy(uinfo->value.enumerated.name, itemlist[uinfo->value.enumerated.item]);
	return 0;
}

/* get callback for selector unit */
static int mixer_ctl_selector_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, err;

	err = get_cur_ctl_value(cval, 0, &val);
	if (err < 0) {
		if (cval->mixer->ignore_ctl_error) {
			ucontrol->value.enumerated.item[0] = 0;
			return 0;
		}
		return err;
	}
	val = get_relative_value(cval, val);
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

/* put callback for selector unit */
static int mixer_ctl_selector_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, oval, err;

	err = get_cur_ctl_value(cval, 0, &oval);
	if (err < 0) {
		if (cval->mixer->ignore_ctl_error)
			return 0;
		return err;
	}
	val = ucontrol->value.enumerated.item[0];
	val = get_abs_value(cval, val);
	if (val != oval) {
		set_cur_ctl_value(cval, 0, val);
		return 1;
	}
	return 0;
}

/* alsa control interface for selector unit */
static struct snd_kcontrol_new mixer_selectunit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later */
	.info = mixer_ctl_selector_info,
	.get = mixer_ctl_selector_get,
	.put = mixer_ctl_selector_put,
};


/* private free callback.
 * free both private_data and private_value
 */
static void usb_mixer_selector_elem_free(struct snd_kcontrol *kctl)
{
	int i, num_ins = 0;

	if (kctl->private_data) {
		struct usb_mixer_elem_info *cval = kctl->private_data;
		num_ins = cval->max;
		kfree(cval);
		kctl->private_data = NULL;
	}
	if (kctl->private_value) {
		char **itemlist = (char **)kctl->private_value;
		for (i = 0; i < num_ins; i++)
			kfree(itemlist[i]);
		kfree(itemlist);
		kctl->private_value = 0;
	}
}

/*
 * parse a selector unit
 */
static int parse_audio_selector_unit(struct mixer_build *state, int unitid, unsigned char *desc)
{
	unsigned int num_ins = desc[4];
	unsigned int i, nameid, len;
	int err;
	struct usb_mixer_elem_info *cval;
	struct snd_kcontrol *kctl;
	char **namelist;

	if (! num_ins || desc[0] < 5 + num_ins) {
		snd_printk(KERN_ERR "invalid SELECTOR UNIT descriptor %d\n", unitid);
		return -EINVAL;
	}

	for (i = 0; i < num_ins; i++) {
		if ((err = parse_audio_unit(state, desc[5 + i])) < 0)
			return err;
	}

	if (num_ins == 1) /* only one ? nonsense! */
		return 0;

	if (check_ignored_ctl(state, unitid, 0))
		return 0;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (! cval) {
		snd_printk(KERN_ERR "cannot malloc kcontrol\n");
		return -ENOMEM;
	}
	cval->mixer = state->mixer;
	cval->id = unitid;
	cval->val_type = USB_MIXER_U8;
	cval->channels = 1;
	cval->min = 1;
	cval->max = num_ins;
	cval->res = 1;
	cval->initialized = 1;

	namelist = kmalloc(sizeof(char *) * num_ins, GFP_KERNEL);
	if (! namelist) {
		snd_printk(KERN_ERR "cannot malloc\n");
		kfree(cval);
		return -ENOMEM;
	}
#define MAX_ITEM_NAME_LEN	64
	for (i = 0; i < num_ins; i++) {
		struct usb_audio_term iterm;
		len = 0;
		namelist[i] = kmalloc(MAX_ITEM_NAME_LEN, GFP_KERNEL);
		if (! namelist[i]) {
			snd_printk(KERN_ERR "cannot malloc\n");
			while (i--)
				kfree(namelist[i]);
			kfree(namelist);
			kfree(cval);
			return -ENOMEM;
		}
		len = check_mapped_selector_name(state, unitid, i, namelist[i],
						 MAX_ITEM_NAME_LEN);
		if (! len && check_input_term(state, desc[5 + i], &iterm) >= 0)
			len = get_term_name(state, &iterm, namelist[i], MAX_ITEM_NAME_LEN, 0);
		if (! len)
			sprintf(namelist[i], "Input %d", i);
	}

	kctl = snd_ctl_new1(&mixer_selectunit_ctl, cval);
	if (! kctl) {
		snd_printk(KERN_ERR "cannot malloc kcontrol\n");
		kfree(namelist);
		kfree(cval);
		return -ENOMEM;
	}
	kctl->private_value = (unsigned long)namelist;
	kctl->private_free = usb_mixer_selector_elem_free;

	nameid = desc[desc[0] - 1];
	len = check_mapped_name(state, unitid, 0, kctl->id.name, sizeof(kctl->id.name));
	if (len)
		;
	else if (nameid)
		snd_usb_copy_string_desc(state, nameid, kctl->id.name, sizeof(kctl->id.name));
	else {
		len = get_term_name(state, &state->oterm,
				    kctl->id.name, sizeof(kctl->id.name), 0);
		if (! len)
			strlcpy(kctl->id.name, "USB", sizeof(kctl->id.name));

		if ((state->oterm.type & 0xff00) == 0x0100)
			append_ctl_name(kctl, " Capture Source");
		else
			append_ctl_name(kctl, " Playback Source");
	}

	snd_printdd(KERN_INFO "[%d] SU [%s] items = %d\n",
		    cval->id, kctl->id.name, num_ins);
	if ((err = add_control_to_empty(state, kctl)) < 0)
		return err;

	return 0;
}


/*
 * parse an audio unit recursively
 */

static int parse_audio_unit(struct mixer_build *state, int unitid)
{
	unsigned char *p1;

	if (test_and_set_bit(unitid, state->unitbitmap))
		return 0; /* the unit already visited */

	p1 = find_audio_control_unit(state, unitid);
	if (!p1) {
		snd_printk(KERN_ERR "usbaudio: unit %d not found!\n", unitid);
		return -EINVAL;
	}

	switch (p1[2]) {
	case INPUT_TERMINAL:
		return 0; /* NOP */
	case MIXER_UNIT:
		return parse_audio_mixer_unit(state, unitid, p1);
	case SELECTOR_UNIT:
		return parse_audio_selector_unit(state, unitid, p1);
	case FEATURE_UNIT:
		return parse_audio_feature_unit(state, unitid, p1);
	case PROCESSING_UNIT:
		return parse_audio_processing_unit(state, unitid, p1);
	case EXTENSION_UNIT:
		return parse_audio_extension_unit(state, unitid, p1);
	default:
		snd_printk(KERN_ERR "usbaudio: unit %u: unexpected type 0x%02x\n", unitid, p1[2]);
		return -EINVAL;
	}
}

static void snd_usb_mixer_free(struct usb_mixer_interface *mixer)
{
	kfree(mixer->id_elems);
	if (mixer->urb) {
		kfree(mixer->urb->transfer_buffer);
		usb_free_urb(mixer->urb);
	}
	usb_free_urb(mixer->rc_urb);
	kfree(mixer->rc_setup_packet);
	kfree(mixer);
}

static int snd_usb_mixer_dev_free(struct snd_device *device)
{
	struct usb_mixer_interface *mixer = device->device_data;
	snd_usb_mixer_free(mixer);
	return 0;
}

/*
 * create mixer controls
 *
 * walk through all OUTPUT_TERMINAL descriptors to search for mixers
 */
static int snd_usb_mixer_controls(struct usb_mixer_interface *mixer)
{
	unsigned char *desc;
	struct mixer_build state;
	int err;
	const struct usbmix_ctl_map *map;
	struct usb_host_interface *hostif;

	hostif = &usb_ifnum_to_if(mixer->chip->dev, mixer->ctrlif)->altsetting[0];
	memset(&state, 0, sizeof(state));
	state.chip = mixer->chip;
	state.mixer = mixer;
	state.buffer = hostif->extra;
	state.buflen = hostif->extralen;

	/* check the mapping table */
	for (map = usbmix_ctl_maps; map->id; map++) {
		if (map->id == state.chip->usb_id) {
			state.map = map->map;
			state.selector_map = map->selector_map;
			mixer->ignore_ctl_error = map->ignore_ctl_error;
			break;
		}
	}

	desc = NULL;
	while ((desc = snd_usb_find_csint_desc(hostif->extra, hostif->extralen, desc, OUTPUT_TERMINAL)) != NULL) {
		if (desc[0] < 9)
			continue; /* invalid descriptor? */
		set_bit(desc[3], state.unitbitmap);  /* mark terminal ID as visited */
		state.oterm.id = desc[3];
		state.oterm.type = combine_word(&desc[4]);
		state.oterm.name = desc[8];
		err = parse_audio_unit(&state, desc[7]);
		if (err < 0)
			return err;
	}
	return 0;
}

static void snd_usb_mixer_notify_id(struct usb_mixer_interface *mixer,
				    int unitid)
{
	struct usb_mixer_elem_info *info;

	for (info = mixer->id_elems[unitid]; info; info = info->next_id_elem)
		snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       info->elem_id);
}

static void snd_usb_mixer_memory_change(struct usb_mixer_interface *mixer,
					int unitid)
{
	if (!mixer->rc_cfg)
		return;
	/* unit ids specific to Extigy/Audigy 2 NX: */
	switch (unitid) {
	case 0: /* remote control */
		mixer->rc_urb->dev = mixer->chip->dev;
		usb_submit_urb(mixer->rc_urb, GFP_ATOMIC);
		break;
	case 4: /* digital in jack */
	case 7: /* line in jacks */
	case 19: /* speaker out jacks */
	case 20: /* headphones out jack */
		break;
	/* live24ext: 4 = line-in jack */
	case 3:	/* hp-out jack (may actuate Mute) */
		if (mixer->chip->usb_id == USB_ID(0x041e, 0x3040) ||
		    mixer->chip->usb_id == USB_ID(0x041e, 0x3048))
			snd_usb_mixer_notify_id(mixer, mixer->rc_cfg->mute_mixer_id);
		break;
	default:
		snd_printd(KERN_DEBUG "memory change in unknown unit %d\n", unitid);
		break;
	}
}

static void snd_usb_mixer_status_complete(struct urb *urb)
{
	struct usb_mixer_interface *mixer = urb->context;

	if (urb->status == 0) {
		u8 *buf = urb->transfer_buffer;
		int i;

		for (i = urb->actual_length; i >= 2; buf += 2, i -= 2) {
			snd_printd(KERN_DEBUG "status interrupt: %02x %02x\n",
				   buf[0], buf[1]);
			/* ignore any notifications not from the control interface */
			if ((buf[0] & 0x0f) != 0)
				continue;
			if (!(buf[0] & 0x40))
				snd_usb_mixer_notify_id(mixer, buf[1]);
			else
				snd_usb_mixer_memory_change(mixer, buf[1]);
		}
	}
	if (urb->status != -ENOENT && urb->status != -ECONNRESET) {
		urb->dev = mixer->chip->dev;
		usb_submit_urb(urb, GFP_ATOMIC);
	}
}

/* create the handler for the optional status interrupt endpoint */
static int snd_usb_mixer_status_create(struct usb_mixer_interface *mixer)
{
	struct usb_host_interface *hostif;
	struct usb_endpoint_descriptor *ep;
	void *transfer_buffer;
	int buffer_length;
	unsigned int epnum;

	hostif = &usb_ifnum_to_if(mixer->chip->dev, mixer->ctrlif)->altsetting[0];
	/* we need one interrupt input endpoint */
	if (get_iface_desc(hostif)->bNumEndpoints < 1)
		return 0;
	ep = get_endpoint(hostif, 0);
	if (!usb_endpoint_dir_in(ep) || !usb_endpoint_xfer_int(ep))
		return 0;

	epnum = usb_endpoint_num(ep);
	buffer_length = le16_to_cpu(ep->wMaxPacketSize);
	transfer_buffer = kmalloc(buffer_length, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;
	mixer->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!mixer->urb) {
		kfree(transfer_buffer);
		return -ENOMEM;
	}
	usb_fill_int_urb(mixer->urb, mixer->chip->dev,
			 usb_rcvintpipe(mixer->chip->dev, epnum),
			 transfer_buffer, buffer_length,
			 snd_usb_mixer_status_complete, mixer, ep->bInterval);
	usb_submit_urb(mixer->urb, GFP_KERNEL);
	return 0;
}

static void snd_usb_soundblaster_remote_complete(struct urb *urb)
{
	struct usb_mixer_interface *mixer = urb->context;
	const struct rc_config *rc = mixer->rc_cfg;
	u32 code;

	if (urb->status < 0 || urb->actual_length < rc->min_packet_length)
		return;

	code = mixer->rc_buffer[rc->offset];
	if (rc->length == 2)
		code |= mixer->rc_buffer[rc->offset + 1] << 8;

	/* the Mute button actually changes the mixer control */
	if (code == rc->mute_code)
		snd_usb_mixer_notify_id(mixer, rc->mute_mixer_id);
	mixer->rc_code = code;
	wmb();
	wake_up(&mixer->rc_waitq);
}

static long snd_usb_sbrc_hwdep_read(struct snd_hwdep *hw, char __user *buf,
				     long count, loff_t *offset)
{
	struct usb_mixer_interface *mixer = hw->private_data;
	int err;
	u32 rc_code;

	if (count != 1 && count != 4)
		return -EINVAL;
	err = wait_event_interruptible(mixer->rc_waitq,
				       (rc_code = xchg(&mixer->rc_code, 0)) != 0);
	if (err == 0) {
		if (count == 1)
			err = put_user(rc_code, buf);
		else
			err = put_user(rc_code, (u32 __user *)buf);
	}
	return err < 0 ? err : count;
}

static unsigned int snd_usb_sbrc_hwdep_poll(struct snd_hwdep *hw, struct file *file,
					    poll_table *wait)
{
	struct usb_mixer_interface *mixer = hw->private_data;

	poll_wait(file, &mixer->rc_waitq, wait);
	return mixer->rc_code ? POLLIN | POLLRDNORM : 0;
}

static int snd_usb_soundblaster_remote_init(struct usb_mixer_interface *mixer)
{
	struct snd_hwdep *hwdep;
	int err, len, i;

	for (i = 0; i < ARRAY_SIZE(rc_configs); ++i)
		if (rc_configs[i].usb_id == mixer->chip->usb_id)
			break;
	if (i >= ARRAY_SIZE(rc_configs))
		return 0;
	mixer->rc_cfg = &rc_configs[i];

	len = mixer->rc_cfg->packet_length;
	
	init_waitqueue_head(&mixer->rc_waitq);
	err = snd_hwdep_new(mixer->chip->card, "SB remote control", 0, &hwdep);
	if (err < 0)
		return err;
	snprintf(hwdep->name, sizeof(hwdep->name),
		 "%s remote control", mixer->chip->card->shortname);
	hwdep->iface = SNDRV_HWDEP_IFACE_SB_RC;
	hwdep->private_data = mixer;
	hwdep->ops.read = snd_usb_sbrc_hwdep_read;
	hwdep->ops.poll = snd_usb_sbrc_hwdep_poll;
	hwdep->exclusive = 1;

	mixer->rc_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!mixer->rc_urb)
		return -ENOMEM;
	mixer->rc_setup_packet = kmalloc(sizeof(*mixer->rc_setup_packet), GFP_KERNEL);
	if (!mixer->rc_setup_packet) {
		usb_free_urb(mixer->rc_urb);
		mixer->rc_urb = NULL;
		return -ENOMEM;
	}
	mixer->rc_setup_packet->bRequestType =
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	mixer->rc_setup_packet->bRequest = GET_MEM;
	mixer->rc_setup_packet->wValue = cpu_to_le16(0);
	mixer->rc_setup_packet->wIndex = cpu_to_le16(0);
	mixer->rc_setup_packet->wLength = cpu_to_le16(len);
	usb_fill_control_urb(mixer->rc_urb, mixer->chip->dev,
			     usb_rcvctrlpipe(mixer->chip->dev, 0),
			     (u8*)mixer->rc_setup_packet, mixer->rc_buffer, len,
			     snd_usb_soundblaster_remote_complete, mixer);
	return 0;
}

#define snd_audigy2nx_led_info		snd_ctl_boolean_mono_info

static int snd_audigy2nx_led_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_interface *mixer = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;

	ucontrol->value.integer.value[0] = mixer->audigy2nx_leds[index];
	return 0;
}

static int snd_audigy2nx_led_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_interface *mixer = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];
	int err, changed;

	if (value > 1)
		return -EINVAL;
	changed = value != mixer->audigy2nx_leds[index];
	err = snd_usb_ctl_msg(mixer->chip->dev,
			      usb_sndctrlpipe(mixer->chip->dev, 0), 0x24,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      value, index + 2, NULL, 0, 100);
	if (err < 0)
		return err;
	mixer->audigy2nx_leds[index] = value;
	return changed;
}

static struct snd_kcontrol_new snd_audigy2nx_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "CMSS LED Switch",
		.info = snd_audigy2nx_led_info,
		.get = snd_audigy2nx_led_get,
		.put = snd_audigy2nx_led_put,
		.private_value = 0,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Power LED Switch",
		.info = snd_audigy2nx_led_info,
		.get = snd_audigy2nx_led_get,
		.put = snd_audigy2nx_led_put,
		.private_value = 1,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Dolby Digital LED Switch",
		.info = snd_audigy2nx_led_info,
		.get = snd_audigy2nx_led_get,
		.put = snd_audigy2nx_led_put,
		.private_value = 2,
	},
};

static int snd_audigy2nx_controls_create(struct usb_mixer_interface *mixer)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(snd_audigy2nx_controls); ++i) {
		if (i > 1 && /* Live24ext has 2 LEDs only */
			(mixer->chip->usb_id == USB_ID(0x041e, 0x3040) ||
			 mixer->chip->usb_id == USB_ID(0x041e, 0x3048)))
			break; 
		err = snd_ctl_add(mixer->chip->card,
				  snd_ctl_new1(&snd_audigy2nx_controls[i], mixer));
		if (err < 0)
			return err;
	}
	mixer->audigy2nx_leds[1] = 1; /* Power LED is on by default */
	return 0;
}

static void snd_audigy2nx_proc_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	static const struct sb_jack {
		int unitid;
		const char *name;
	}  jacks_audigy2nx[] = {
		{4,  "dig in "},
		{7,  "line in"},
		{19, "spk out"},
		{20, "hph out"},
		{-1, NULL}
	}, jacks_live24ext[] = {
		{4,  "line in"}, /* &1=Line, &2=Mic*/
		{3,  "hph out"}, /* headphones */
		{0,  "RC     "}, /* last command, 6 bytes see rc_config above */
		{-1, NULL}
	};
	const struct sb_jack *jacks;
	struct usb_mixer_interface *mixer = entry->private_data;
	int i, err;
	u8 buf[3];

	snd_iprintf(buffer, "%s jacks\n\n", mixer->chip->card->shortname);
	if (mixer->chip->usb_id == USB_ID(0x041e, 0x3020))
		jacks = jacks_audigy2nx;
	else if (mixer->chip->usb_id == USB_ID(0x041e, 0x3040) ||
		 mixer->chip->usb_id == USB_ID(0x041e, 0x3048))
		jacks = jacks_live24ext;
	else
		return;

	for (i = 0; jacks[i].name; ++i) {
		snd_iprintf(buffer, "%s: ", jacks[i].name);
		err = snd_usb_ctl_msg(mixer->chip->dev,
				      usb_rcvctrlpipe(mixer->chip->dev, 0),
				      GET_MEM, USB_DIR_IN | USB_TYPE_CLASS |
				      USB_RECIP_INTERFACE, 0,
				      jacks[i].unitid << 8, buf, 3, 100);
		if (err == 3 && (buf[0] == 3 || buf[0] == 6))
			snd_iprintf(buffer, "%02x %02x\n", buf[1], buf[2]);
		else
			snd_iprintf(buffer, "?\n");
	}
}

static int snd_xonar_u1_switch_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_interface *mixer = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = !!(mixer->xonar_u1_status & 0x02);
	return 0;
}

static int snd_xonar_u1_switch_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_interface *mixer = snd_kcontrol_chip(kcontrol);
	u8 old_status, new_status;
	int err, changed;

	old_status = mixer->xonar_u1_status;
	if (ucontrol->value.integer.value[0])
		new_status = old_status | 0x02;
	else
		new_status = old_status & ~0x02;
	changed = new_status != old_status;
	err = snd_usb_ctl_msg(mixer->chip->dev,
			      usb_sndctrlpipe(mixer->chip->dev, 0), 0x08,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      50, 0, &new_status, 1, 100);
	if (err < 0)
		return err;
	mixer->xonar_u1_status = new_status;
	return changed;
}

static struct snd_kcontrol_new snd_xonar_u1_output_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Playback Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_xonar_u1_switch_get,
	.put = snd_xonar_u1_switch_put,
};

static int snd_xonar_u1_controls_create(struct usb_mixer_interface *mixer)
{
	int err;

	err = snd_ctl_add(mixer->chip->card,
			  snd_ctl_new1(&snd_xonar_u1_output_switch, mixer));
	if (err < 0)
		return err;
	mixer->xonar_u1_status = 0x05;
	return 0;
}

int snd_usb_create_mixer(struct snd_usb_audio *chip, int ctrlif,
			 int ignore_error)
{
	static struct snd_device_ops dev_ops = {
		.dev_free = snd_usb_mixer_dev_free
	};
	struct usb_mixer_interface *mixer;
	int err;

	strcpy(chip->card->mixername, "USB Mixer");

	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;
	mixer->chip = chip;
	mixer->ctrlif = ctrlif;
	mixer->ignore_ctl_error = ignore_error;
	mixer->id_elems = kcalloc(256, sizeof(*mixer->id_elems), GFP_KERNEL);
	if (!mixer->id_elems) {
		kfree(mixer);
		return -ENOMEM;
	}

	if ((err = snd_usb_mixer_controls(mixer)) < 0 ||
	    (err = snd_usb_mixer_status_create(mixer)) < 0)
		goto _error;

	if ((err = snd_usb_soundblaster_remote_init(mixer)) < 0)
		goto _error;

	if (mixer->chip->usb_id == USB_ID(0x041e, 0x3020) ||
	    mixer->chip->usb_id == USB_ID(0x041e, 0x3040) ||
	    mixer->chip->usb_id == USB_ID(0x041e, 0x3048)) {
		struct snd_info_entry *entry;

		if ((err = snd_audigy2nx_controls_create(mixer)) < 0)
			goto _error;
		if (!snd_card_proc_new(chip->card, "audigy2nx", &entry))
			snd_info_set_text_ops(entry, mixer,
					      snd_audigy2nx_proc_read);
	}

	if (mixer->chip->usb_id == USB_ID(0x0b05, 0x1739) ||
	    mixer->chip->usb_id == USB_ID(0x0b05, 0x1743)) {
		err = snd_xonar_u1_controls_create(mixer);
		if (err < 0)
			goto _error;
	}

	err = snd_device_new(chip->card, SNDRV_DEV_LOWLEVEL, mixer, &dev_ops);
	if (err < 0)
		goto _error;
	list_add(&mixer->list, &chip->mixer_list);
	return 0;

_error:
	snd_usb_mixer_free(mixer);
	return err;
}

void snd_usb_mixer_disconnect(struct list_head *p)
{
	struct usb_mixer_interface *mixer;
	
	mixer = list_entry(p, struct usb_mixer_interface, list);
	usb_kill_urb(mixer->urb);
	usb_kill_urb(mixer->rc_urb);
}
