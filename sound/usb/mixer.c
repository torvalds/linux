// SPDX-License-Identifier: GPL-2.0-or-later
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
 */

/*
 * TODOs, for both the mixer and the streaming interfaces:
 *
 *  - support for UAC2 effect units
 *  - support for graphical equalizers
 *  - RANGE and MEM set commands (UAC2)
 *  - RANGE and MEM interrupt dispatchers (UAC2)
 *  - audio channel clustering (UAC2)
 *  - audio sample rate converter units (UAC2)
 *  - proper handling of clock multipliers (UAC2)
 *  - dispatch clock change notifications (UAC2)
 *  	- stop PCM streams which use a clock that became invalid
 *  	- stop PCM streams which use a clock selector that has changed
 *  	- parse available sample rates again when clock sources changed
 */

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/usb/audio-v2.h>
#include <linux/usb/audio-v3.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "usbaudio.h"
#include "mixer.h"
#include "helper.h"
#include "mixer_quirks.h"
#include "power.h"

#define MAX_ID_ELEMS	256

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
	DECLARE_BITMAP(unitbitmap, MAX_ID_ELEMS);
	DECLARE_BITMAP(termbitmap, MAX_ID_ELEMS);
	struct usb_audio_term oterm;
	const struct usbmix_name_map *map;
	const struct usbmix_selector_map *selector_map;
};

/*E-mu 0202/0404/0204 eXtension Unit(XU) control*/
enum {
	USB_XU_CLOCK_RATE 		= 0xe301,
	USB_XU_CLOCK_SOURCE		= 0xe302,
	USB_XU_DIGITAL_IO_STATUS	= 0xe303,
	USB_XU_DEVICE_OPTIONS		= 0xe304,
	USB_XU_DIRECT_MONITORING	= 0xe305,
	USB_XU_METERING			= 0xe306
};
enum {
	USB_XU_CLOCK_SOURCE_SELECTOR = 0x02,	/* clock source*/
	USB_XU_CLOCK_RATE_SELECTOR = 0x03,	/* clock rate */
	USB_XU_DIGITAL_FORMAT_SELECTOR = 0x01,	/* the spdif format */
	USB_XU_SOFT_LIMIT_SELECTOR = 0x03	/* soft limiter */
};

/*
 * manual mapping of mixer names
 * if the mixer topology is too complicated and the parsed names are
 * ambiguous, add the entries in usbmixer_maps.c.
 */
#include "mixer_maps.c"

static const struct usbmix_name_map *
find_map(const struct usbmix_name_map *p, int unitid, int control)
{
	if (!p)
		return NULL;

	for (; p->id; p++) {
		if (p->id == unitid &&
		    (!control || !p->control || control == p->control))
			return p;
	}
	return NULL;
}

/* get the mapped name if the unit matches */
static int
check_mapped_name(const struct usbmix_name_map *p, char *buf, int buflen)
{
	if (!p || !p->name)
		return 0;

	buflen--;
	return strlcpy(buf, p->name, buflen);
}

/* ignore the error value if ignore_ctl_error flag is set */
#define filter_error(cval, err) \
	((cval)->head.mixer->ignore_ctl_error ? 0 : (err))

/* check whether the control should be ignored */
static inline int
check_ignored_ctl(const struct usbmix_name_map *p)
{
	if (!p || p->name || p->dB)
		return 0;
	return 1;
}

/* dB mapping */
static inline void check_mapped_dB(const struct usbmix_name_map *p,
				   struct usb_mixer_elem_info *cval)
{
	if (p && p->dB) {
		cval->dBmin = p->dB->min;
		cval->dBmax = p->dB->max;
		cval->initialized = 1;
	}
}

/* get the mapped selector source name */
static int check_mapped_selector_name(struct mixer_build *state, int unitid,
				      int index, char *buf, int buflen)
{
	const struct usbmix_selector_map *p;

	if (!state->selector_map)
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
static void *find_audio_control_unit(struct mixer_build *state,
				     unsigned char unit)
{
	/* we just parse the header */
	struct uac_feature_unit_descriptor *hdr = NULL;

	while ((hdr = snd_usb_find_desc(state->buffer, state->buflen, hdr,
					USB_DT_CS_INTERFACE)) != NULL) {
		if (hdr->bLength >= 4 &&
		    hdr->bDescriptorSubtype >= UAC_INPUT_TERMINAL &&
		    hdr->bDescriptorSubtype <= UAC3_SAMPLE_RATE_CONVERTER &&
		    hdr->bUnitID == unit)
			return hdr;
	}

	return NULL;
}

/*
 * copy a string with the given id
 */
static int snd_usb_copy_string_desc(struct snd_usb_audio *chip,
				    int index, char *buf, int maxlen)
{
	int len = usb_string(chip->dev, index, buf, maxlen - 1);

	if (len < 0)
		return 0;

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
	if (!cval->res)
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
	if (!cval->res)
		cval->res = 1;
	val *= cval->res;
	val += cval->min;
	if (val > cval->max)
		return cval->max;
	return val;
}

static int uac2_ctl_value_size(int val_type)
{
	switch (val_type) {
	case USB_MIXER_S32:
	case USB_MIXER_U32:
		return 4;
	case USB_MIXER_S16:
	case USB_MIXER_U16:
		return 2;
	default:
		return 1;
	}
	return 0; /* unreachable */
}


/*
 * retrieve a mixer value
 */

static int get_ctl_value_v1(struct usb_mixer_elem_info *cval, int request,
			    int validx, int *value_ret)
{
	struct snd_usb_audio *chip = cval->head.mixer->chip;
	unsigned char buf[2];
	int val_len = cval->val_type >= USB_MIXER_S16 ? 2 : 1;
	int timeout = 10;
	int idx = 0, err;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return -EIO;

	while (timeout-- > 0) {
		idx = snd_usb_ctrl_intf(chip) | (cval->head.id << 8);
		err = snd_usb_ctl_msg(chip->dev, usb_rcvctrlpipe(chip->dev, 0), request,
				      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				      validx, idx, buf, val_len);
		if (err >= val_len) {
			*value_ret = convert_signed_value(cval, snd_usb_combine_bytes(buf, val_len));
			err = 0;
			goto out;
		} else if (err == -ETIMEDOUT) {
			goto out;
		}
	}
	usb_audio_dbg(chip,
		"cannot get ctl value: req = %#x, wValue = %#x, wIndex = %#x, type = %d\n",
		request, validx, idx, cval->val_type);
	err = -EINVAL;

 out:
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int get_ctl_value_v2(struct usb_mixer_elem_info *cval, int request,
			    int validx, int *value_ret)
{
	struct snd_usb_audio *chip = cval->head.mixer->chip;
	/* enough space for one range */
	unsigned char buf[sizeof(__u16) + 3 * sizeof(__u32)];
	unsigned char *val;
	int idx = 0, ret, val_size, size;
	__u8 bRequest;

	val_size = uac2_ctl_value_size(cval->val_type);

	if (request == UAC_GET_CUR) {
		bRequest = UAC2_CS_CUR;
		size = val_size;
	} else {
		bRequest = UAC2_CS_RANGE;
		size = sizeof(__u16) + 3 * val_size;
	}

	memset(buf, 0, sizeof(buf));

	ret = snd_usb_lock_shutdown(chip) ? -EIO : 0;
	if (ret)
		goto error;

	idx = snd_usb_ctrl_intf(chip) | (cval->head.id << 8);
	ret = snd_usb_ctl_msg(chip->dev, usb_rcvctrlpipe(chip->dev, 0), bRequest,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			      validx, idx, buf, size);
	snd_usb_unlock_shutdown(chip);

	if (ret < 0) {
error:
		usb_audio_err(chip,
			"cannot get ctl value: req = %#x, wValue = %#x, wIndex = %#x, type = %d\n",
			request, validx, idx, cval->val_type);
		return ret;
	}

	/* FIXME: how should we handle multiple triplets here? */

	switch (request) {
	case UAC_GET_CUR:
		val = buf;
		break;
	case UAC_GET_MIN:
		val = buf + sizeof(__u16);
		break;
	case UAC_GET_MAX:
		val = buf + sizeof(__u16) + val_size;
		break;
	case UAC_GET_RES:
		val = buf + sizeof(__u16) + val_size * 2;
		break;
	default:
		return -EINVAL;
	}

	*value_ret = convert_signed_value(cval,
					  snd_usb_combine_bytes(val, val_size));

	return 0;
}

static int get_ctl_value(struct usb_mixer_elem_info *cval, int request,
			 int validx, int *value_ret)
{
	validx += cval->idx_off;

	return (cval->head.mixer->protocol == UAC_VERSION_1) ?
		get_ctl_value_v1(cval, request, validx, value_ret) :
		get_ctl_value_v2(cval, request, validx, value_ret);
}

static int get_cur_ctl_value(struct usb_mixer_elem_info *cval,
			     int validx, int *value)
{
	return get_ctl_value(cval, UAC_GET_CUR, validx, value);
}

/* channel = 0: master, 1 = first channel */
static inline int get_cur_mix_raw(struct usb_mixer_elem_info *cval,
				  int channel, int *value)
{
	return get_ctl_value(cval, UAC_GET_CUR,
			     (cval->control << 8) | channel,
			     value);
}

int snd_usb_get_cur_mix_value(struct usb_mixer_elem_info *cval,
			     int channel, int index, int *value)
{
	int err;

	if (cval->cached & (1 << channel)) {
		*value = cval->cache_val[index];
		return 0;
	}
	err = get_cur_mix_raw(cval, channel, value);
	if (err < 0) {
		if (!cval->head.mixer->ignore_ctl_error)
			usb_audio_dbg(cval->head.mixer->chip,
				"cannot get current value for control %d ch %d: err = %d\n",
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

int snd_usb_mixer_set_ctl_value(struct usb_mixer_elem_info *cval,
				int request, int validx, int value_set)
{
	struct snd_usb_audio *chip = cval->head.mixer->chip;
	unsigned char buf[4];
	int idx = 0, val_len, err, timeout = 10;

	validx += cval->idx_off;


	if (cval->head.mixer->protocol == UAC_VERSION_1) {
		val_len = cval->val_type >= USB_MIXER_S16 ? 2 : 1;
	} else { /* UAC_VERSION_2/3 */
		val_len = uac2_ctl_value_size(cval->val_type);

		/* FIXME */
		if (request != UAC_SET_CUR) {
			usb_audio_dbg(chip, "RANGE setting not yet supported\n");
			return -EINVAL;
		}

		request = UAC2_CS_CUR;
	}

	value_set = convert_bytes_value(cval, value_set);
	buf[0] = value_set & 0xff;
	buf[1] = (value_set >> 8) & 0xff;
	buf[2] = (value_set >> 16) & 0xff;
	buf[3] = (value_set >> 24) & 0xff;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return -EIO;

	while (timeout-- > 0) {
		idx = snd_usb_ctrl_intf(chip) | (cval->head.id << 8);
		err = snd_usb_ctl_msg(chip->dev,
				      usb_sndctrlpipe(chip->dev, 0), request,
				      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				      validx, idx, buf, val_len);
		if (err >= 0) {
			err = 0;
			goto out;
		} else if (err == -ETIMEDOUT) {
			goto out;
		}
	}
	usb_audio_dbg(chip, "cannot set ctl value: req = %#x, wValue = %#x, wIndex = %#x, type = %d, data = %#x/%#x\n",
		      request, validx, idx, cval->val_type, buf[0], buf[1]);
	err = -EINVAL;

 out:
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int set_cur_ctl_value(struct usb_mixer_elem_info *cval,
			     int validx, int value)
{
	return snd_usb_mixer_set_ctl_value(cval, UAC_SET_CUR, validx, value);
}

int snd_usb_set_cur_mix_value(struct usb_mixer_elem_info *cval, int channel,
			     int index, int value)
{
	int err;
	unsigned int read_only = (channel == 0) ?
		cval->master_readonly :
		cval->ch_readonly & (1 << (channel - 1));

	if (read_only) {
		usb_audio_dbg(cval->head.mixer->chip,
			      "%s(): channel %d of control %d is read_only\n",
			    __func__, channel, cval->control);
		return 0;
	}

	err = snd_usb_mixer_set_ctl_value(cval,
					  UAC_SET_CUR, (cval->control << 8) | channel,
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
int snd_usb_mixer_vol_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			 unsigned int size, unsigned int __user *_tlv)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	DECLARE_TLV_DB_MINMAX(scale, 0, 0);

	if (size < sizeof(scale))
		return -ENOMEM;
	if (cval->min_mute)
		scale[0] = SNDRV_CTL_TLVT_DB_MINMAX_MUTE;
	scale[2] = cval->dBmin;
	scale[3] = cval->dBmax;
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
static int check_matrix_bitmap(unsigned char *bmap,
			       int ich, int och, int num_outs)
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

int snd_usb_mixer_add_control(struct usb_mixer_elem_list *list,
			      struct snd_kcontrol *kctl)
{
	struct usb_mixer_interface *mixer = list->mixer;
	int err;

	while (snd_ctl_find_id(mixer->chip->card, &kctl->id))
		kctl->id.index++;
	err = snd_ctl_add(mixer->chip->card, kctl);
	if (err < 0) {
		usb_audio_dbg(mixer->chip, "cannot add control (err = %d)\n",
			      err);
		return err;
	}
	list->kctl = kctl;
	list->next_id_elem = mixer->id_elems[list->id];
	mixer->id_elems[list->id] = list;
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

static int get_term_name(struct snd_usb_audio *chip, struct usb_audio_term *iterm,
			 unsigned char *name, int maxlen, int term_only)
{
	struct iterm_name_combo *names;
	int len;

	if (iterm->name) {
		len = snd_usb_copy_string_desc(chip, iterm->name,
						name, maxlen);
		if (len)
			return len;
	}

	/* virtual type - not a real terminal */
	if (iterm->type >> 16) {
		if (term_only)
			return 0;
		switch (iterm->type >> 16) {
		case UAC3_SELECTOR_UNIT:
			strcpy(name, "Selector");
			return 8;
		case UAC3_PROCESSING_UNIT:
			strcpy(name, "Process Unit");
			return 12;
		case UAC3_EXTENSION_UNIT:
			strcpy(name, "Ext Unit");
			return 8;
		case UAC3_MIXER_UNIT:
			strcpy(name, "Mixer");
			return 5;
		default:
			return sprintf(name, "Unit %d", iterm->id);
		}
	}

	switch (iterm->type & 0xff00) {
	case 0x0100:
		strcpy(name, "PCM");
		return 3;
	case 0x0200:
		strcpy(name, "Mic");
		return 3;
	case 0x0400:
		strcpy(name, "Headset");
		return 7;
	case 0x0500:
		strcpy(name, "Phone");
		return 5;
	}

	for (names = iterm_names; names->type; names++) {
		if (names->type == iterm->type) {
			strcpy(name, names->name);
			return strlen(names->name);
		}
	}

	return 0;
}

/*
 * Get logical cluster information for UAC3 devices.
 */
static int get_cluster_channels_v3(struct mixer_build *state, unsigned int cluster_id)
{
	struct uac3_cluster_header_descriptor c_header;
	int err;

	err = snd_usb_ctl_msg(state->chip->dev,
			usb_rcvctrlpipe(state->chip->dev, 0),
			UAC3_CS_REQ_HIGH_CAPABILITY_DESCRIPTOR,
			USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			cluster_id,
			snd_usb_ctrl_intf(state->chip),
			&c_header, sizeof(c_header));
	if (err < 0)
		goto error;
	if (err != sizeof(c_header)) {
		err = -EIO;
		goto error;
	}

	return c_header.bNrChannels;

error:
	usb_audio_err(state->chip, "cannot request logical cluster ID: %d (err: %d)\n", cluster_id, err);
	return err;
}

/*
 * Get number of channels for a Mixer Unit.
 */
static int uac_mixer_unit_get_channels(struct mixer_build *state,
				       struct uac_mixer_unit_descriptor *desc)
{
	int mu_channels;

	switch (state->mixer->protocol) {
	case UAC_VERSION_1:
	case UAC_VERSION_2:
	default:
		if (desc->bLength < sizeof(*desc) + desc->bNrInPins + 1)
			return 0; /* no bmControls -> skip */
		mu_channels = uac_mixer_unit_bNrChannels(desc);
		break;
	case UAC_VERSION_3:
		mu_channels = get_cluster_channels_v3(state,
				uac3_mixer_unit_wClusterDescrID(desc));
		break;
	}

	return mu_channels;
}

/*
 * parse the source unit recursively until it reaches to a terminal
 * or a branched unit.
 */
static int __check_input_term(struct mixer_build *state, int id,
			    struct usb_audio_term *term)
{
	int protocol = state->mixer->protocol;
	int err;
	void *p1;
	unsigned char *hdr;

	memset(term, 0, sizeof(*term));
	for (;;) {
		/* a loop in the terminal chain? */
		if (test_and_set_bit(id, state->termbitmap))
			return -EINVAL;

		p1 = find_audio_control_unit(state, id);
		if (!p1)
			break;
		if (!snd_usb_validate_audio_desc(p1, protocol))
			break; /* bad descriptor */

		hdr = p1;
		term->id = id;

		if (protocol == UAC_VERSION_1 || protocol == UAC_VERSION_2) {
			switch (hdr[2]) {
			case UAC_INPUT_TERMINAL:
				if (protocol == UAC_VERSION_1) {
					struct uac_input_terminal_descriptor *d = p1;

					term->type = le16_to_cpu(d->wTerminalType);
					term->channels = d->bNrChannels;
					term->chconfig = le16_to_cpu(d->wChannelConfig);
					term->name = d->iTerminal;
				} else { /* UAC_VERSION_2 */
					struct uac2_input_terminal_descriptor *d = p1;

					/* call recursively to verify that the
					 * referenced clock entity is valid */
					err = __check_input_term(state, d->bCSourceID, term);
					if (err < 0)
						return err;

					/* save input term properties after recursion,
					 * to ensure they are not overriden by the
					 * recursion calls */
					term->id = id;
					term->type = le16_to_cpu(d->wTerminalType);
					term->channels = d->bNrChannels;
					term->chconfig = le32_to_cpu(d->bmChannelConfig);
					term->name = d->iTerminal;
				}
				return 0;
			case UAC_FEATURE_UNIT: {
				/* the header is the same for v1 and v2 */
				struct uac_feature_unit_descriptor *d = p1;

				id = d->bSourceID;
				break; /* continue to parse */
			}
			case UAC_MIXER_UNIT: {
				struct uac_mixer_unit_descriptor *d = p1;

				term->type = UAC3_MIXER_UNIT << 16; /* virtual type */
				term->channels = uac_mixer_unit_bNrChannels(d);
				term->chconfig = uac_mixer_unit_wChannelConfig(d, protocol);
				term->name = uac_mixer_unit_iMixer(d);
				return 0;
			}
			case UAC_SELECTOR_UNIT:
			case UAC2_CLOCK_SELECTOR: {
				struct uac_selector_unit_descriptor *d = p1;
				/* call recursively to retrieve the channel info */
				err = __check_input_term(state, d->baSourceID[0], term);
				if (err < 0)
					return err;
				term->type = UAC3_SELECTOR_UNIT << 16; /* virtual type */
				term->id = id;
				term->name = uac_selector_unit_iSelector(d);
				return 0;
			}
			case UAC1_PROCESSING_UNIT:
			/* UAC2_EFFECT_UNIT */
				if (protocol == UAC_VERSION_1)
					term->type = UAC3_PROCESSING_UNIT << 16; /* virtual type */
				else /* UAC_VERSION_2 */
					term->type = UAC3_EFFECT_UNIT << 16; /* virtual type */
				/* fall through */
			case UAC1_EXTENSION_UNIT:
			/* UAC2_PROCESSING_UNIT_V2 */
				if (protocol == UAC_VERSION_1 && !term->type)
					term->type = UAC3_EXTENSION_UNIT << 16; /* virtual type */
				else if (protocol == UAC_VERSION_2 && !term->type)
					term->type = UAC3_PROCESSING_UNIT << 16; /* virtual type */
				/* fall through */
			case UAC2_EXTENSION_UNIT_V2: {
				struct uac_processing_unit_descriptor *d = p1;

				if (protocol == UAC_VERSION_2 &&
					hdr[2] == UAC2_EFFECT_UNIT) {
					/* UAC2/UAC1 unit IDs overlap here in an
					 * uncompatible way. Ignore this unit for now.
					 */
					return 0;
				}

				if (d->bNrInPins) {
					id = d->baSourceID[0];
					break; /* continue to parse */
				}
				if (!term->type)
					term->type = UAC3_EXTENSION_UNIT << 16; /* virtual type */

				term->channels = uac_processing_unit_bNrChannels(d);
				term->chconfig = uac_processing_unit_wChannelConfig(d, protocol);
				term->name = uac_processing_unit_iProcessing(d, protocol);
				return 0;
			}
			case UAC2_CLOCK_SOURCE: {
				struct uac_clock_source_descriptor *d = p1;

				term->type = UAC3_CLOCK_SOURCE << 16; /* virtual type */
				term->id = id;
				term->name = d->iClockSource;
				return 0;
			}
			default:
				return -ENODEV;
			}
		} else { /* UAC_VERSION_3 */
			switch (hdr[2]) {
			case UAC_INPUT_TERMINAL: {
				struct uac3_input_terminal_descriptor *d = p1;

				/* call recursively to verify that the
				 * referenced clock entity is valid */
				err = __check_input_term(state, d->bCSourceID, term);
				if (err < 0)
					return err;

				/* save input term properties after recursion,
				 * to ensure they are not overriden by the
				 * recursion calls */
				term->id = id;
				term->type = le16_to_cpu(d->wTerminalType);

				err = get_cluster_channels_v3(state, le16_to_cpu(d->wClusterDescrID));
				if (err < 0)
					return err;
				term->channels = err;

				/* REVISIT: UAC3 IT doesn't have channels cfg */
				term->chconfig = 0;

				term->name = le16_to_cpu(d->wTerminalDescrStr);
				return 0;
			}
			case UAC3_FEATURE_UNIT: {
				struct uac3_feature_unit_descriptor *d = p1;

				id = d->bSourceID;
				break; /* continue to parse */
			}
			case UAC3_CLOCK_SOURCE: {
				struct uac3_clock_source_descriptor *d = p1;

				term->type = UAC3_CLOCK_SOURCE << 16; /* virtual type */
				term->id = id;
				term->name = le16_to_cpu(d->wClockSourceStr);
				return 0;
			}
			case UAC3_MIXER_UNIT: {
				struct uac_mixer_unit_descriptor *d = p1;

				err = uac_mixer_unit_get_channels(state, d);
				if (err <= 0)
					return err;

				term->channels = err;
				term->type = UAC3_MIXER_UNIT << 16; /* virtual type */

				return 0;
			}
			case UAC3_SELECTOR_UNIT:
			case UAC3_CLOCK_SELECTOR: {
				struct uac_selector_unit_descriptor *d = p1;
				/* call recursively to retrieve the channel info */
				err = __check_input_term(state, d->baSourceID[0], term);
				if (err < 0)
					return err;
				term->type = UAC3_SELECTOR_UNIT << 16; /* virtual type */
				term->id = id;
				term->name = 0; /* TODO: UAC3 Class-specific strings */

				return 0;
			}
			case UAC3_PROCESSING_UNIT: {
				struct uac_processing_unit_descriptor *d = p1;

				if (!d->bNrInPins)
					return -EINVAL;

				/* call recursively to retrieve the channel info */
				err = __check_input_term(state, d->baSourceID[0], term);
				if (err < 0)
					return err;

				term->type = UAC3_PROCESSING_UNIT << 16; /* virtual type */
				term->id = id;
				term->name = 0; /* TODO: UAC3 Class-specific strings */

				return 0;
			}
			default:
				return -ENODEV;
			}
		}
	}
	return -ENODEV;
}


static int check_input_term(struct mixer_build *state, int id,
			    struct usb_audio_term *term)
{
	memset(term, 0, sizeof(*term));
	memset(state->termbitmap, 0, sizeof(state->termbitmap));
	return __check_input_term(state, id, term);
}

/*
 * Feature Unit
 */

/* feature unit control information */
struct usb_feature_control_info {
	int control;
	const char *name;
	int type;	/* data type for uac1 */
	int type_uac2;	/* data type for uac2 if different from uac1, else -1 */
};

static struct usb_feature_control_info audio_feature_info[] = {
	{ UAC_FU_MUTE,			"Mute",			USB_MIXER_INV_BOOLEAN, -1 },
	{ UAC_FU_VOLUME,		"Volume",		USB_MIXER_S16, -1 },
	{ UAC_FU_BASS,			"Tone Control - Bass",	USB_MIXER_S8, -1 },
	{ UAC_FU_MID,			"Tone Control - Mid",	USB_MIXER_S8, -1 },
	{ UAC_FU_TREBLE,		"Tone Control - Treble", USB_MIXER_S8, -1 },
	{ UAC_FU_GRAPHIC_EQUALIZER,	"Graphic Equalizer",	USB_MIXER_S8, -1 }, /* FIXME: not implemented yet */
	{ UAC_FU_AUTOMATIC_GAIN,	"Auto Gain Control",	USB_MIXER_BOOLEAN, -1 },
	{ UAC_FU_DELAY,			"Delay Control",	USB_MIXER_U16, USB_MIXER_U32 },
	{ UAC_FU_BASS_BOOST,		"Bass Boost",		USB_MIXER_BOOLEAN, -1 },
	{ UAC_FU_LOUDNESS,		"Loudness",		USB_MIXER_BOOLEAN, -1 },
	/* UAC2 specific */
	{ UAC2_FU_INPUT_GAIN,		"Input Gain Control",	USB_MIXER_S16, -1 },
	{ UAC2_FU_INPUT_GAIN_PAD,	"Input Gain Pad Control", USB_MIXER_S16, -1 },
	{ UAC2_FU_PHASE_INVERTER,	 "Phase Inverter Control", USB_MIXER_BOOLEAN, -1 },
};

static void usb_mixer_elem_info_free(struct usb_mixer_elem_info *cval)
{
	kfree(cval);
}

/* private_free callback */
void snd_usb_mixer_elem_free(struct snd_kcontrol *kctl)
{
	usb_mixer_elem_info_free(kctl->private_data);
	kctl->private_data = NULL;
}

/*
 * interface to ALSA control for feature/mixer units
 */

/* volume control quirks */
static void volume_control_quirks(struct usb_mixer_elem_info *cval,
				  struct snd_kcontrol *kctl)
{
	struct snd_usb_audio *chip = cval->head.mixer->chip;
	switch (chip->usb_id) {
	case USB_ID(0x0763, 0x2030): /* M-Audio Fast Track C400 */
	case USB_ID(0x0763, 0x2031): /* M-Audio Fast Track C600 */
		if (strcmp(kctl->id.name, "Effect Duration") == 0) {
			cval->min = 0x0000;
			cval->max = 0xffff;
			cval->res = 0x00e6;
			break;
		}
		if (strcmp(kctl->id.name, "Effect Volume") == 0 ||
		    strcmp(kctl->id.name, "Effect Feedback Volume") == 0) {
			cval->min = 0x00;
			cval->max = 0xff;
			break;
		}
		if (strstr(kctl->id.name, "Effect Return") != NULL) {
			cval->min = 0xb706;
			cval->max = 0xff7b;
			cval->res = 0x0073;
			break;
		}
		if ((strstr(kctl->id.name, "Playback Volume") != NULL) ||
			(strstr(kctl->id.name, "Effect Send") != NULL)) {
			cval->min = 0xb5fb; /* -73 dB = 0xb6ff */
			cval->max = 0xfcfe;
			cval->res = 0x0073;
		}
		break;

	case USB_ID(0x0763, 0x2081): /* M-Audio Fast Track Ultra 8R */
	case USB_ID(0x0763, 0x2080): /* M-Audio Fast Track Ultra */
		if (strcmp(kctl->id.name, "Effect Duration") == 0) {
			usb_audio_info(chip,
				       "set quirk for FTU Effect Duration\n");
			cval->min = 0x0000;
			cval->max = 0x7f00;
			cval->res = 0x0100;
			break;
		}
		if (strcmp(kctl->id.name, "Effect Volume") == 0 ||
		    strcmp(kctl->id.name, "Effect Feedback Volume") == 0) {
			usb_audio_info(chip,
				       "set quirks for FTU Effect Feedback/Volume\n");
			cval->min = 0x00;
			cval->max = 0x7f;
			break;
		}
		break;

	case USB_ID(0x0d8c, 0x0103):
		if (!strcmp(kctl->id.name, "PCM Playback Volume")) {
			usb_audio_info(chip,
				 "set volume quirk for CM102-A+/102S+\n");
			cval->min = -256;
		}
		break;

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
			usb_audio_info(chip,
				 "set volume quirk for UDA1321/N101 chip\n");
			cval->max = -256;
		}
		break;

	case USB_ID(0x046d, 0x09a4):
		if (!strcmp(kctl->id.name, "Mic Capture Volume")) {
			usb_audio_info(chip,
				"set volume quirk for QuickCam E3500\n");
			cval->min = 6080;
			cval->max = 8768;
			cval->res = 192;
		}
		break;

	case USB_ID(0x046d, 0x0807): /* Logitech Webcam C500 */
	case USB_ID(0x046d, 0x0808):
	case USB_ID(0x046d, 0x0809):
	case USB_ID(0x046d, 0x0819): /* Logitech Webcam C210 */
	case USB_ID(0x046d, 0x081b): /* HD Webcam c310 */
	case USB_ID(0x046d, 0x081d): /* HD Webcam c510 */
	case USB_ID(0x046d, 0x0825): /* HD Webcam c270 */
	case USB_ID(0x046d, 0x0826): /* HD Webcam c525 */
	case USB_ID(0x046d, 0x08ca): /* Logitech Quickcam Fusion */
	case USB_ID(0x046d, 0x0991):
	case USB_ID(0x046d, 0x09a2): /* QuickCam Communicate Deluxe/S7500 */
	/* Most audio usb devices lie about volume resolution.
	 * Most Logitech webcams have res = 384.
	 * Probably there is some logitech magic behind this number --fishor
	 */
		if (!strcmp(kctl->id.name, "Mic Capture Volume")) {
			usb_audio_info(chip,
				"set resolution quirk: cval->res = 384\n");
			cval->res = 384;
		}
		break;
	}
}

/*
 * retrieve the minimum and maximum values for the specified control
 */
static int get_min_max_with_quirks(struct usb_mixer_elem_info *cval,
				   int default_min, struct snd_kcontrol *kctl)
{
	/* for failsafe */
	cval->min = default_min;
	cval->max = cval->min + 1;
	cval->res = 1;
	cval->dBmin = cval->dBmax = 0;

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
		if (get_ctl_value(cval, UAC_GET_MAX, (cval->control << 8) | minchn, &cval->max) < 0 ||
		    get_ctl_value(cval, UAC_GET_MIN, (cval->control << 8) | minchn, &cval->min) < 0) {
			usb_audio_err(cval->head.mixer->chip,
				      "%d:%d: cannot get min/max values for control %d (id %d)\n",
				   cval->head.id, snd_usb_ctrl_intf(cval->head.mixer->chip),
							       cval->control, cval->head.id);
			return -EINVAL;
		}
		if (get_ctl_value(cval, UAC_GET_RES,
				  (cval->control << 8) | minchn,
				  &cval->res) < 0) {
			cval->res = 1;
		} else {
			int last_valid_res = cval->res;

			while (cval->res > 1) {
				if (snd_usb_mixer_set_ctl_value(cval, UAC_SET_RES,
								(cval->control << 8) | minchn,
								cval->res / 2) < 0)
					break;
				cval->res /= 2;
			}
			if (get_ctl_value(cval, UAC_GET_RES,
					  (cval->control << 8) | minchn, &cval->res) < 0)
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
				    snd_usb_set_cur_mix_value(cval, minchn, 0, test) ||
				    get_cur_mix_raw(cval, minchn, &check)) {
					cval->res = last_valid_res;
					break;
				}
				if (test == check)
					break;
				cval->res *= 2;
			}
			snd_usb_set_cur_mix_value(cval, minchn, 0, saved);
		}

		cval->initialized = 1;
	}

	if (kctl)
		volume_control_quirks(cval, kctl);

	/* USB descriptions contain the dB scale in 1/256 dB unit
	 * while ALSA TLV contains in 1/100 dB unit
	 */
	cval->dBmin = (convert_signed_value(cval, cval->min) * 100) / 256;
	cval->dBmax = (convert_signed_value(cval, cval->max) * 100) / 256;
	if (cval->dBmin > cval->dBmax) {
		/* something is wrong; assume it's either from/to 0dB */
		if (cval->dBmin < 0)
			cval->dBmax = 0;
		else if (cval->dBmin > 0)
			cval->dBmin = 0;
		if (cval->dBmin > cval->dBmax) {
			/* totally crap, return an error */
			return -EINVAL;
		}
	}

	return 0;
}

#define get_min_max(cval, def)	get_min_max_with_quirks(cval, def, NULL)

/* get a feature/mixer unit info */
static int mixer_ctl_feature_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
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
		if (!cval->initialized) {
			get_min_max_with_quirks(cval, 0, kcontrol);
			if (cval->initialized && cval->dBmin >= cval->dBmax) {
				kcontrol->vd[0].access &= 
					~(SNDRV_CTL_ELEM_ACCESS_TLV_READ |
					  SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK);
				snd_ctl_notify(cval->head.mixer->chip->card,
					       SNDRV_CTL_EVENT_MASK_INFO,
					       &kcontrol->id);
			}
		}
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max =
			(cval->max - cval->min + cval->res - 1) / cval->res;
	}
	return 0;
}

/* get the current value from feature/mixer unit */
static int mixer_ctl_feature_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int c, cnt, val, err;

	ucontrol->value.integer.value[0] = cval->min;
	if (cval->cmask) {
		cnt = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (!(cval->cmask & (1 << c)))
				continue;
			err = snd_usb_get_cur_mix_value(cval, c + 1, cnt, &val);
			if (err < 0)
				return filter_error(cval, err);
			val = get_relative_value(cval, val);
			ucontrol->value.integer.value[cnt] = val;
			cnt++;
		}
		return 0;
	} else {
		/* master channel */
		err = snd_usb_get_cur_mix_value(cval, 0, 0, &val);
		if (err < 0)
			return filter_error(cval, err);
		val = get_relative_value(cval, val);
		ucontrol->value.integer.value[0] = val;
	}
	return 0;
}

/* put the current value to feature/mixer unit */
static int mixer_ctl_feature_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int c, cnt, val, oval, err;
	int changed = 0;

	if (cval->cmask) {
		cnt = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (!(cval->cmask & (1 << c)))
				continue;
			err = snd_usb_get_cur_mix_value(cval, c + 1, cnt, &oval);
			if (err < 0)
				return filter_error(cval, err);
			val = ucontrol->value.integer.value[cnt];
			val = get_abs_value(cval, val);
			if (oval != val) {
				snd_usb_set_cur_mix_value(cval, c + 1, cnt, val);
				changed = 1;
			}
			cnt++;
		}
	} else {
		/* master channel */
		err = snd_usb_get_cur_mix_value(cval, 0, 0, &oval);
		if (err < 0)
			return filter_error(cval, err);
		val = ucontrol->value.integer.value[0];
		val = get_abs_value(cval, val);
		if (val != oval) {
			snd_usb_set_cur_mix_value(cval, 0, 0, val);
			changed = 1;
		}
	}
	return changed;
}

/* get the boolean value from the master channel of a UAC control */
static int mixer_ctl_master_bool_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, err;

	err = snd_usb_get_cur_mix_value(cval, 0, 0, &val);
	if (err < 0)
		return filter_error(cval, err);
	val = (val != 0);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

/* get the connectors status and report it as boolean type */
static int mixer_ctl_connector_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	struct snd_usb_audio *chip = cval->head.mixer->chip;
	int idx = 0, validx, ret, val;

	validx = cval->control << 8 | 0;

	ret = snd_usb_lock_shutdown(chip) ? -EIO : 0;
	if (ret)
		goto error;

	idx = snd_usb_ctrl_intf(chip) | (cval->head.id << 8);
	if (cval->head.mixer->protocol == UAC_VERSION_2) {
		struct uac2_connectors_ctl_blk uac2_conn;

		ret = snd_usb_ctl_msg(chip->dev, usb_rcvctrlpipe(chip->dev, 0), UAC2_CS_CUR,
				      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				      validx, idx, &uac2_conn, sizeof(uac2_conn));
		val = !!uac2_conn.bNrChannels;
	} else { /* UAC_VERSION_3 */
		struct uac3_insertion_ctl_blk uac3_conn;

		ret = snd_usb_ctl_msg(chip->dev, usb_rcvctrlpipe(chip->dev, 0), UAC2_CS_CUR,
				      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				      validx, idx, &uac3_conn, sizeof(uac3_conn));
		val = !!uac3_conn.bmConInserted;
	}

	snd_usb_unlock_shutdown(chip);

	if (ret < 0) {
error:
		usb_audio_err(chip,
			"cannot get connectors status: req = %#x, wValue = %#x, wIndex = %#x, type = %d\n",
			UAC_GET_CUR, validx, idx, cval->val_type);
		return ret;
	}

	ucontrol->value.integer.value[0] = val;
	return 0;
}

static struct snd_kcontrol_new usb_feature_unit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later manually */
	.info = mixer_ctl_feature_info,
	.get = mixer_ctl_feature_get,
	.put = mixer_ctl_feature_put,
};

/* the read-only variant */
static const struct snd_kcontrol_new usb_feature_unit_ctl_ro = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later manually */
	.info = mixer_ctl_feature_info,
	.get = mixer_ctl_feature_get,
	.put = NULL,
};

/*
 * A control which shows the boolean value from reading a UAC control on
 * the master channel.
 */
static struct snd_kcontrol_new usb_bool_master_control_ctl_ro = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "", /* will be filled later manually */
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_ctl_boolean_mono_info,
	.get = mixer_ctl_master_bool_get,
	.put = NULL,
};

static const struct snd_kcontrol_new usb_connector_ctl_ro = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "", /* will be filled later manually */
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_ctl_boolean_mono_info,
	.get = mixer_ctl_connector_get,
	.put = NULL,
};

/*
 * This symbol is exported in order to allow the mixer quirks to
 * hook up to the standard feature unit control mechanism
 */
struct snd_kcontrol_new *snd_usb_feature_unit_ctl = &usb_feature_unit_ctl;

/*
 * build a feature control
 */
static size_t append_ctl_name(struct snd_kcontrol *kctl, const char *str)
{
	return strlcat(kctl->id.name, str, sizeof(kctl->id.name));
}

/*
 * A lot of headsets/headphones have a "Speaker" mixer. Make sure we
 * rename it to "Headphone". We determine if something is a headphone
 * similar to how udev determines form factor.
 */
static void check_no_speaker_on_headset(struct snd_kcontrol *kctl,
					struct snd_card *card)
{
	const char *names_to_check[] = {
		"Headset", "headset", "Headphone", "headphone", NULL};
	const char **s;
	bool found = false;

	if (strcmp("Speaker", kctl->id.name))
		return;

	for (s = names_to_check; *s; s++)
		if (strstr(card->shortname, *s)) {
			found = true;
			break;
		}

	if (!found)
		return;

	strlcpy(kctl->id.name, "Headphone", sizeof(kctl->id.name));
}

static struct usb_feature_control_info *get_feature_control_info(int control)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(audio_feature_info); ++i) {
		if (audio_feature_info[i].control == control)
			return &audio_feature_info[i];
	}
	return NULL;
}

static void __build_feature_ctl(struct usb_mixer_interface *mixer,
				const struct usbmix_name_map *imap,
				unsigned int ctl_mask, int control,
				struct usb_audio_term *iterm,
				struct usb_audio_term *oterm,
				int unitid, int nameid, int readonly_mask)
{
	struct usb_feature_control_info *ctl_info;
	unsigned int len = 0;
	int mapped_name = 0;
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *cval;
	const struct usbmix_name_map *map;
	unsigned int range;

	if (control == UAC_FU_GRAPHIC_EQUALIZER) {
		/* FIXME: not supported yet */
		return;
	}

	map = find_map(imap, unitid, control);
	if (check_ignored_ctl(map))
		return;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (!cval)
		return;
	snd_usb_mixer_elem_init_std(&cval->head, mixer, unitid);
	cval->control = control;
	cval->cmask = ctl_mask;

	ctl_info = get_feature_control_info(control);
	if (!ctl_info) {
		usb_mixer_elem_info_free(cval);
		return;
	}
	if (mixer->protocol == UAC_VERSION_1)
		cval->val_type = ctl_info->type;
	else /* UAC_VERSION_2 */
		cval->val_type = ctl_info->type_uac2 >= 0 ?
			ctl_info->type_uac2 : ctl_info->type;

	if (ctl_mask == 0) {
		cval->channels = 1;	/* master channel */
		cval->master_readonly = readonly_mask;
	} else {
		int i, c = 0;
		for (i = 0; i < 16; i++)
			if (ctl_mask & (1 << i))
				c++;
		cval->channels = c;
		cval->ch_readonly = readonly_mask;
	}

	/*
	 * If all channels in the mask are marked read-only, make the control
	 * read-only. snd_usb_set_cur_mix_value() will check the mask again and won't
	 * issue write commands to read-only channels.
	 */
	if (cval->channels == readonly_mask)
		kctl = snd_ctl_new1(&usb_feature_unit_ctl_ro, cval);
	else
		kctl = snd_ctl_new1(&usb_feature_unit_ctl, cval);

	if (!kctl) {
		usb_audio_err(mixer->chip, "cannot malloc kcontrol\n");
		usb_mixer_elem_info_free(cval);
		return;
	}
	kctl->private_free = snd_usb_mixer_elem_free;

	len = check_mapped_name(map, kctl->id.name, sizeof(kctl->id.name));
	mapped_name = len != 0;
	if (!len && nameid)
		len = snd_usb_copy_string_desc(mixer->chip, nameid,
				kctl->id.name, sizeof(kctl->id.name));

	switch (control) {
	case UAC_FU_MUTE:
	case UAC_FU_VOLUME:
		/*
		 * determine the control name.  the rule is:
		 * - if a name id is given in descriptor, use it.
		 * - if the connected input can be determined, then use the name
		 *   of terminal type.
		 * - if the connected output can be determined, use it.
		 * - otherwise, anonymous name.
		 */
		if (!len) {
			if (iterm)
				len = get_term_name(mixer->chip, iterm,
						    kctl->id.name,
						    sizeof(kctl->id.name), 1);
			if (!len && oterm)
				len = get_term_name(mixer->chip, oterm,
						    kctl->id.name,
						    sizeof(kctl->id.name), 1);
			if (!len)
				snprintf(kctl->id.name, sizeof(kctl->id.name),
					 "Feature %d", unitid);
		}

		if (!mapped_name)
			check_no_speaker_on_headset(kctl, mixer->chip->card);

		/*
		 * determine the stream direction:
		 * if the connected output is USB stream, then it's likely a
		 * capture stream.  otherwise it should be playback (hopefully :)
		 */
		if (!mapped_name && oterm && !(oterm->type >> 16)) {
			if ((oterm->type & 0xff00) == 0x0100)
				append_ctl_name(kctl, " Capture");
			else
				append_ctl_name(kctl, " Playback");
		}
		append_ctl_name(kctl, control == UAC_FU_MUTE ?
				" Switch" : " Volume");
		break;
	default:
		if (!len)
			strlcpy(kctl->id.name, audio_feature_info[control-1].name,
				sizeof(kctl->id.name));
		break;
	}

	/* get min/max values */
	get_min_max_with_quirks(cval, 0, kctl);

	if (control == UAC_FU_VOLUME) {
		check_mapped_dB(map, cval);
		if (cval->dBmin < cval->dBmax || !cval->initialized) {
			kctl->tlv.c = snd_usb_mixer_vol_tlv;
			kctl->vd[0].access |=
				SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
		}
	}

	snd_usb_mixer_fu_apply_quirk(mixer, cval, unitid, kctl);

	range = (cval->max - cval->min) / cval->res;
	/*
	 * Are there devices with volume range more than 255? I use a bit more
	 * to be sure. 384 is a resolution magic number found on Logitech
	 * devices. It will definitively catch all buggy Logitech devices.
	 */
	if (range > 384) {
		usb_audio_warn(mixer->chip,
			       "Warning! Unlikely big volume range (=%u), cval->res is probably wrong.",
			       range);
		usb_audio_warn(mixer->chip,
			       "[%d] FU [%s] ch = %d, val = %d/%d/%d",
			       cval->head.id, kctl->id.name, cval->channels,
			       cval->min, cval->max, cval->res);
	}

	usb_audio_dbg(mixer->chip, "[%d] FU [%s] ch = %d, val = %d/%d/%d\n",
		      cval->head.id, kctl->id.name, cval->channels,
		      cval->min, cval->max, cval->res);
	snd_usb_mixer_add_control(&cval->head, kctl);
}

static void build_feature_ctl(struct mixer_build *state, void *raw_desc,
			      unsigned int ctl_mask, int control,
			      struct usb_audio_term *iterm, int unitid,
			      int readonly_mask)
{
	struct uac_feature_unit_descriptor *desc = raw_desc;
	int nameid = uac_feature_unit_iFeature(desc);

	__build_feature_ctl(state->mixer, state->map, ctl_mask, control,
			iterm, &state->oterm, unitid, nameid, readonly_mask);
}

static void build_feature_ctl_badd(struct usb_mixer_interface *mixer,
			      unsigned int ctl_mask, int control, int unitid,
			      const struct usbmix_name_map *badd_map)
{
	__build_feature_ctl(mixer, badd_map, ctl_mask, control,
			NULL, NULL, unitid, 0, 0);
}

static void get_connector_control_name(struct usb_mixer_interface *mixer,
				       struct usb_audio_term *term,
				       bool is_input, char *name, int name_size)
{
	int name_len = get_term_name(mixer->chip, term, name, name_size, 0);

	if (name_len == 0)
		strlcpy(name, "Unknown", name_size);

	/*
	 *  sound/core/ctljack.c has a convention of naming jack controls
	 * by ending in " Jack".  Make it slightly more useful by
	 * indicating Input or Output after the terminal name.
	 */
	if (is_input)
		strlcat(name, " - Input Jack", name_size);
	else
		strlcat(name, " - Output Jack", name_size);
}

/* Build a mixer control for a UAC connector control (jack-detect) */
static void build_connector_control(struct usb_mixer_interface *mixer,
				    struct usb_audio_term *term, bool is_input)
{
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *cval;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (!cval)
		return;
	snd_usb_mixer_elem_init_std(&cval->head, mixer, term->id);
	/*
	 * UAC2: The first byte from reading the UAC2_TE_CONNECTOR control returns the
	 * number of channels connected.
	 *
	 * UAC3: The first byte specifies size of bitmap for the inserted controls. The
	 * following byte(s) specifies which connectors are inserted.
	 *
	 * This boolean ctl will simply report if any channels are connected
	 * or not.
	 */
	if (mixer->protocol == UAC_VERSION_2)
		cval->control = UAC2_TE_CONNECTOR;
	else /* UAC_VERSION_3 */
		cval->control = UAC3_TE_INSERTION;

	cval->val_type = USB_MIXER_BOOLEAN;
	cval->channels = 1; /* report true if any channel is connected */
	cval->min = 0;
	cval->max = 1;
	kctl = snd_ctl_new1(&usb_connector_ctl_ro, cval);
	if (!kctl) {
		usb_audio_err(mixer->chip, "cannot malloc kcontrol\n");
		usb_mixer_elem_info_free(cval);
		return;
	}
	get_connector_control_name(mixer, term, is_input, kctl->id.name,
				   sizeof(kctl->id.name));
	kctl->private_free = snd_usb_mixer_elem_free;
	snd_usb_mixer_add_control(&cval->head, kctl);
}

static int parse_clock_source_unit(struct mixer_build *state, int unitid,
				   void *_ftr)
{
	struct uac_clock_source_descriptor *hdr = _ftr;
	struct usb_mixer_elem_info *cval;
	struct snd_kcontrol *kctl;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	int ret;

	if (state->mixer->protocol != UAC_VERSION_2)
		return -EINVAL;

	/*
	 * The only property of this unit we are interested in is the
	 * clock source validity. If that isn't readable, just bail out.
	 */
	if (!uac_v2v3_control_is_readable(hdr->bmControls,
				      UAC2_CS_CONTROL_CLOCK_VALID))
		return 0;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (!cval)
		return -ENOMEM;

	snd_usb_mixer_elem_init_std(&cval->head, state->mixer, hdr->bClockID);

	cval->min = 0;
	cval->max = 1;
	cval->channels = 1;
	cval->val_type = USB_MIXER_BOOLEAN;
	cval->control = UAC2_CS_CONTROL_CLOCK_VALID;

	cval->master_readonly = 1;
	/* From UAC2 5.2.5.1.2 "Only the get request is supported." */
	kctl = snd_ctl_new1(&usb_bool_master_control_ctl_ro, cval);

	if (!kctl) {
		usb_mixer_elem_info_free(cval);
		return -ENOMEM;
	}

	kctl->private_free = snd_usb_mixer_elem_free;
	ret = snd_usb_copy_string_desc(state->chip, hdr->iClockSource,
				       name, sizeof(name));
	if (ret > 0)
		snprintf(kctl->id.name, sizeof(kctl->id.name),
			 "%s Validity", name);
	else
		snprintf(kctl->id.name, sizeof(kctl->id.name),
			 "Clock Source %d Validity", hdr->bClockID);

	return snd_usb_mixer_add_control(&cval->head, kctl);
}

/*
 * parse a feature unit
 *
 * most of controls are defined here.
 */
static int parse_audio_feature_unit(struct mixer_build *state, int unitid,
				    void *_ftr)
{
	int channels, i, j;
	struct usb_audio_term iterm;
	unsigned int master_bits;
	int err, csize;
	struct uac_feature_unit_descriptor *hdr = _ftr;
	__u8 *bmaControls;

	if (state->mixer->protocol == UAC_VERSION_1) {
		csize = hdr->bControlSize;
		channels = (hdr->bLength - 7) / csize - 1;
		bmaControls = hdr->bmaControls;
	} else if (state->mixer->protocol == UAC_VERSION_2) {
		struct uac2_feature_unit_descriptor *ftr = _ftr;
		csize = 4;
		channels = (hdr->bLength - 6) / 4 - 1;
		bmaControls = ftr->bmaControls;
	} else { /* UAC_VERSION_3 */
		struct uac3_feature_unit_descriptor *ftr = _ftr;

		csize = 4;
		channels = (ftr->bLength - 7) / 4 - 1;
		bmaControls = ftr->bmaControls;
	}

	/* parse the source unit */
	err = parse_audio_unit(state, hdr->bSourceID);
	if (err < 0)
		return err;

	/* determine the input source type and name */
	err = check_input_term(state, hdr->bSourceID, &iterm);
	if (err < 0)
		return err;

	master_bits = snd_usb_combine_bytes(bmaControls, csize);
	/* master configuration quirks */
	switch (state->chip->usb_id) {
	case USB_ID(0x08bb, 0x2702):
		usb_audio_info(state->chip,
			       "usbmixer: master volume quirk for PCM2702 chip\n");
		/* disable non-functional volume control */
		master_bits &= ~UAC_CONTROL_BIT(UAC_FU_VOLUME);
		break;
	case USB_ID(0x1130, 0xf211):
		usb_audio_info(state->chip,
			       "usbmixer: volume control quirk for Tenx TP6911 Audio Headset\n");
		/* disable non-functional volume control */
		channels = 0;
		break;

	}

	if (state->mixer->protocol == UAC_VERSION_1) {
		/* check all control types */
		for (i = 0; i < 10; i++) {
			unsigned int ch_bits = 0;
			int control = audio_feature_info[i].control;

			for (j = 0; j < channels; j++) {
				unsigned int mask;

				mask = snd_usb_combine_bytes(bmaControls +
							     csize * (j+1), csize);
				if (mask & (1 << i))
					ch_bits |= (1 << j);
			}
			/* audio class v1 controls are never read-only */

			/*
			 * The first channel must be set
			 * (for ease of programming).
			 */
			if (ch_bits & 1)
				build_feature_ctl(state, _ftr, ch_bits, control,
						  &iterm, unitid, 0);
			if (master_bits & (1 << i))
				build_feature_ctl(state, _ftr, 0, control,
						  &iterm, unitid, 0);
		}
	} else { /* UAC_VERSION_2/3 */
		for (i = 0; i < ARRAY_SIZE(audio_feature_info); i++) {
			unsigned int ch_bits = 0;
			unsigned int ch_read_only = 0;
			int control = audio_feature_info[i].control;

			for (j = 0; j < channels; j++) {
				unsigned int mask;

				mask = snd_usb_combine_bytes(bmaControls +
							     csize * (j+1), csize);
				if (uac_v2v3_control_is_readable(mask, control)) {
					ch_bits |= (1 << j);
					if (!uac_v2v3_control_is_writeable(mask, control))
						ch_read_only |= (1 << j);
				}
			}

			/*
			 * NOTE: build_feature_ctl() will mark the control
			 * read-only if all channels are marked read-only in
			 * the descriptors. Otherwise, the control will be
			 * reported as writeable, but the driver will not
			 * actually issue a write command for read-only
			 * channels.
			 */

			/*
			 * The first channel must be set
			 * (for ease of programming).
			 */
			if (ch_bits & 1)
				build_feature_ctl(state, _ftr, ch_bits, control,
						  &iterm, unitid, ch_read_only);
			if (uac_v2v3_control_is_readable(master_bits, control))
				build_feature_ctl(state, _ftr, 0, control,
						  &iterm, unitid,
						  !uac_v2v3_control_is_writeable(master_bits,
										 control));
		}
	}

	return 0;
}

/*
 * Mixer Unit
 */

/* check whether the given in/out overflows bmMixerControls matrix */
static bool mixer_bitmap_overflow(struct uac_mixer_unit_descriptor *desc,
				  int protocol, int num_ins, int num_outs)
{
	u8 *hdr = (u8 *)desc;
	u8 *c = uac_mixer_unit_bmControls(desc, protocol);
	size_t rest; /* remaining bytes after bmMixerControls */

	switch (protocol) {
	case UAC_VERSION_1:
	default:
		rest = 1; /* iMixer */
		break;
	case UAC_VERSION_2:
		rest = 2; /* bmControls + iMixer */
		break;
	case UAC_VERSION_3:
		rest = 6; /* bmControls + wMixerDescrStr */
		break;
	}

	/* overflow? */
	return c + (num_ins * num_outs + 7) / 8 + rest > hdr + hdr[0];
}

/*
 * build a mixer unit control
 *
 * the callbacks are identical with feature unit.
 * input channel number (zero based) is given in control field instead.
 */
static void build_mixer_unit_ctl(struct mixer_build *state,
				 struct uac_mixer_unit_descriptor *desc,
				 int in_pin, int in_ch, int num_outs,
				 int unitid, struct usb_audio_term *iterm)
{
	struct usb_mixer_elem_info *cval;
	unsigned int i, len;
	struct snd_kcontrol *kctl;
	const struct usbmix_name_map *map;

	map = find_map(state->map, unitid, 0);
	if (check_ignored_ctl(map))
		return;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (!cval)
		return;

	snd_usb_mixer_elem_init_std(&cval->head, state->mixer, unitid);
	cval->control = in_ch + 1; /* based on 1 */
	cval->val_type = USB_MIXER_S16;
	for (i = 0; i < num_outs; i++) {
		__u8 *c = uac_mixer_unit_bmControls(desc, state->mixer->protocol);

		if (check_matrix_bitmap(c, in_ch, i, num_outs)) {
			cval->cmask |= (1 << i);
			cval->channels++;
		}
	}

	/* get min/max values */
	get_min_max(cval, 0);

	kctl = snd_ctl_new1(&usb_feature_unit_ctl, cval);
	if (!kctl) {
		usb_audio_err(state->chip, "cannot malloc kcontrol\n");
		usb_mixer_elem_info_free(cval);
		return;
	}
	kctl->private_free = snd_usb_mixer_elem_free;

	len = check_mapped_name(map, kctl->id.name, sizeof(kctl->id.name));
	if (!len)
		len = get_term_name(state->chip, iterm, kctl->id.name,
				    sizeof(kctl->id.name), 0);
	if (!len)
		len = sprintf(kctl->id.name, "Mixer Source %d", in_ch + 1);
	append_ctl_name(kctl, " Volume");

	usb_audio_dbg(state->chip, "[%d] MU [%s] ch = %d, val = %d/%d\n",
		    cval->head.id, kctl->id.name, cval->channels, cval->min, cval->max);
	snd_usb_mixer_add_control(&cval->head, kctl);
}

static int parse_audio_input_terminal(struct mixer_build *state, int unitid,
				      void *raw_desc)
{
	struct usb_audio_term iterm;
	unsigned int control, bmctls, term_id;

	if (state->mixer->protocol == UAC_VERSION_2) {
		struct uac2_input_terminal_descriptor *d_v2 = raw_desc;
		control = UAC2_TE_CONNECTOR;
		term_id = d_v2->bTerminalID;
		bmctls = le16_to_cpu(d_v2->bmControls);
	} else if (state->mixer->protocol == UAC_VERSION_3) {
		struct uac3_input_terminal_descriptor *d_v3 = raw_desc;
		control = UAC3_TE_INSERTION;
		term_id = d_v3->bTerminalID;
		bmctls = le32_to_cpu(d_v3->bmControls);
	} else {
		return 0; /* UAC1. No Insertion control */
	}

	check_input_term(state, term_id, &iterm);

	/* Check for jack detection. */
	if (uac_v2v3_control_is_readable(bmctls, control))
		build_connector_control(state->mixer, &iterm, true);

	return 0;
}

/*
 * parse a mixer unit
 */
static int parse_audio_mixer_unit(struct mixer_build *state, int unitid,
				  void *raw_desc)
{
	struct uac_mixer_unit_descriptor *desc = raw_desc;
	struct usb_audio_term iterm;
	int input_pins, num_ins, num_outs;
	int pin, ich, err;

	err = uac_mixer_unit_get_channels(state, desc);
	if (err < 0) {
		usb_audio_err(state->chip,
			      "invalid MIXER UNIT descriptor %d\n",
			      unitid);
		return err;
	}

	num_outs = err;
	input_pins = desc->bNrInPins;

	num_ins = 0;
	ich = 0;
	for (pin = 0; pin < input_pins; pin++) {
		err = parse_audio_unit(state, desc->baSourceID[pin]);
		if (err < 0)
			continue;
		/* no bmControls field (e.g. Maya44) -> ignore */
		if (!num_outs)
			continue;
		err = check_input_term(state, desc->baSourceID[pin], &iterm);
		if (err < 0)
			return err;
		num_ins += iterm.channels;
		if (mixer_bitmap_overflow(desc, state->mixer->protocol,
					  num_ins, num_outs))
			break;
		for (; ich < num_ins; ich++) {
			int och, ich_has_controls = 0;

			for (och = 0; och < num_outs; och++) {
				__u8 *c = uac_mixer_unit_bmControls(desc,
						state->mixer->protocol);

				if (check_matrix_bitmap(c, ich, och, num_outs)) {
					ich_has_controls = 1;
					break;
				}
			}
			if (ich_has_controls)
				build_mixer_unit_ctl(state, desc, pin, ich, num_outs,
						     unitid, &iterm);
		}
	}
	return 0;
}

/*
 * Processing Unit / Extension Unit
 */

/* get callback for processing/extension unit */
static int mixer_ctl_procunit_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int err, val;

	err = get_cur_ctl_value(cval, cval->control << 8, &val);
	if (err < 0) {
		ucontrol->value.integer.value[0] = cval->min;
		return filter_error(cval, err);
	}
	val = get_relative_value(cval, val);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

/* put callback for processing/extension unit */
static int mixer_ctl_procunit_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, oval, err;

	err = get_cur_ctl_value(cval, cval->control << 8, &oval);
	if (err < 0)
		return filter_error(cval, err);
	val = ucontrol->value.integer.value[0];
	val = get_abs_value(cval, val);
	if (val != oval) {
		set_cur_ctl_value(cval, cval->control << 8, val);
		return 1;
	}
	return 0;
}

/* alsa control interface for processing/extension unit */
static const struct snd_kcontrol_new mixer_procunit_ctl = {
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

static struct procunit_value_info undefined_proc_info[] = {
	{ 0x00, "Control Undefined", 0 },
	{ 0 }
};

static struct procunit_value_info updown_proc_info[] = {
	{ UAC_UD_ENABLE, "Switch", USB_MIXER_BOOLEAN },
	{ UAC_UD_MODE_SELECT, "Mode Select", USB_MIXER_U8, 1 },
	{ 0 }
};
static struct procunit_value_info prologic_proc_info[] = {
	{ UAC_DP_ENABLE, "Switch", USB_MIXER_BOOLEAN },
	{ UAC_DP_MODE_SELECT, "Mode Select", USB_MIXER_U8, 1 },
	{ 0 }
};
static struct procunit_value_info threed_enh_proc_info[] = {
	{ UAC_3D_ENABLE, "Switch", USB_MIXER_BOOLEAN },
	{ UAC_3D_SPACE, "Spaciousness", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info reverb_proc_info[] = {
	{ UAC_REVERB_ENABLE, "Switch", USB_MIXER_BOOLEAN },
	{ UAC_REVERB_LEVEL, "Level", USB_MIXER_U8 },
	{ UAC_REVERB_TIME, "Time", USB_MIXER_U16 },
	{ UAC_REVERB_FEEDBACK, "Feedback", USB_MIXER_U8 },
	{ 0 }
};
static struct procunit_value_info chorus_proc_info[] = {
	{ UAC_CHORUS_ENABLE, "Switch", USB_MIXER_BOOLEAN },
	{ UAC_CHORUS_LEVEL, "Level", USB_MIXER_U8 },
	{ UAC_CHORUS_RATE, "Rate", USB_MIXER_U16 },
	{ UAC_CHORUS_DEPTH, "Depth", USB_MIXER_U16 },
	{ 0 }
};
static struct procunit_value_info dcr_proc_info[] = {
	{ UAC_DCR_ENABLE, "Switch", USB_MIXER_BOOLEAN },
	{ UAC_DCR_RATE, "Ratio", USB_MIXER_U16 },
	{ UAC_DCR_MAXAMPL, "Max Amp", USB_MIXER_S16 },
	{ UAC_DCR_THRESHOLD, "Threshold", USB_MIXER_S16 },
	{ UAC_DCR_ATTACK_TIME, "Attack Time", USB_MIXER_U16 },
	{ UAC_DCR_RELEASE_TIME, "Release Time", USB_MIXER_U16 },
	{ 0 }
};

static struct procunit_info procunits[] = {
	{ UAC_PROCESS_UP_DOWNMIX, "Up Down", updown_proc_info },
	{ UAC_PROCESS_DOLBY_PROLOGIC, "Dolby Prologic", prologic_proc_info },
	{ UAC_PROCESS_STEREO_EXTENDER, "3D Stereo Extender", threed_enh_proc_info },
	{ UAC_PROCESS_REVERB, "Reverb", reverb_proc_info },
	{ UAC_PROCESS_CHORUS, "Chorus", chorus_proc_info },
	{ UAC_PROCESS_DYN_RANGE_COMP, "DCR", dcr_proc_info },
	{ 0 },
};

static struct procunit_value_info uac3_updown_proc_info[] = {
	{ UAC3_UD_MODE_SELECT, "Mode Select", USB_MIXER_U8, 1 },
	{ 0 }
};
static struct procunit_value_info uac3_stereo_ext_proc_info[] = {
	{ UAC3_EXT_WIDTH_CONTROL, "Width Control", USB_MIXER_U8 },
	{ 0 }
};

static struct procunit_info uac3_procunits[] = {
	{ UAC3_PROCESS_UP_DOWNMIX, "Up Down", uac3_updown_proc_info },
	{ UAC3_PROCESS_STEREO_EXTENDER, "3D Stereo Extender", uac3_stereo_ext_proc_info },
	{ UAC3_PROCESS_MULTI_FUNCTION, "Multi-Function", undefined_proc_info },
	{ 0 },
};

/*
 * predefined data for extension units
 */
static struct procunit_value_info clock_rate_xu_info[] = {
	{ USB_XU_CLOCK_RATE_SELECTOR, "Selector", USB_MIXER_U8, 0 },
	{ 0 }
};
static struct procunit_value_info clock_source_xu_info[] = {
	{ USB_XU_CLOCK_SOURCE_SELECTOR, "External", USB_MIXER_BOOLEAN },
	{ 0 }
};
static struct procunit_value_info spdif_format_xu_info[] = {
	{ USB_XU_DIGITAL_FORMAT_SELECTOR, "SPDIF/AC3", USB_MIXER_BOOLEAN },
	{ 0 }
};
static struct procunit_value_info soft_limit_xu_info[] = {
	{ USB_XU_SOFT_LIMIT_SELECTOR, " ", USB_MIXER_BOOLEAN },
	{ 0 }
};
static struct procunit_info extunits[] = {
	{ USB_XU_CLOCK_RATE, "Clock rate", clock_rate_xu_info },
	{ USB_XU_CLOCK_SOURCE, "DigitalIn CLK source", clock_source_xu_info },
	{ USB_XU_DIGITAL_IO_STATUS, "DigitalOut format:", spdif_format_xu_info },
	{ USB_XU_DEVICE_OPTIONS, "AnalogueIn Soft Limit", soft_limit_xu_info },
	{ 0 }
};

/*
 * build a processing/extension unit
 */
static int build_audio_procunit(struct mixer_build *state, int unitid,
				void *raw_desc, struct procunit_info *list,
				bool extension_unit)
{
	struct uac_processing_unit_descriptor *desc = raw_desc;
	int num_ins;
	struct usb_mixer_elem_info *cval;
	struct snd_kcontrol *kctl;
	int i, err, nameid, type, len;
	struct procunit_info *info;
	struct procunit_value_info *valinfo;
	const struct usbmix_name_map *map;
	static struct procunit_value_info default_value_info[] = {
		{ 0x01, "Switch", USB_MIXER_BOOLEAN },
		{ 0 }
	};
	static struct procunit_info default_info = {
		0, NULL, default_value_info
	};
	const char *name = extension_unit ?
		"Extension Unit" : "Processing Unit";

	num_ins = desc->bNrInPins;
	for (i = 0; i < num_ins; i++) {
		err = parse_audio_unit(state, desc->baSourceID[i]);
		if (err < 0)
			return err;
	}

	type = le16_to_cpu(desc->wProcessType);
	for (info = list; info && info->type; info++)
		if (info->type == type)
			break;
	if (!info || !info->type)
		info = &default_info;

	for (valinfo = info->values; valinfo->control; valinfo++) {
		__u8 *controls = uac_processing_unit_bmControls(desc, state->mixer->protocol);

		if (state->mixer->protocol == UAC_VERSION_1) {
			if (!(controls[valinfo->control / 8] &
					(1 << ((valinfo->control % 8) - 1))))
				continue;
		} else { /* UAC_VERSION_2/3 */
			if (!uac_v2v3_control_is_readable(controls[valinfo->control / 8],
							  valinfo->control))
				continue;
		}

		map = find_map(state->map, unitid, valinfo->control);
		if (check_ignored_ctl(map))
			continue;
		cval = kzalloc(sizeof(*cval), GFP_KERNEL);
		if (!cval)
			return -ENOMEM;
		snd_usb_mixer_elem_init_std(&cval->head, state->mixer, unitid);
		cval->control = valinfo->control;
		cval->val_type = valinfo->val_type;
		cval->channels = 1;

		if (state->mixer->protocol > UAC_VERSION_1 &&
		    !uac_v2v3_control_is_writeable(controls[valinfo->control / 8],
						   valinfo->control))
			cval->master_readonly = 1;

		/* get min/max values */
		switch (type) {
		case UAC_PROCESS_UP_DOWNMIX: {
			bool mode_sel = false;

			switch (state->mixer->protocol) {
			case UAC_VERSION_1:
			case UAC_VERSION_2:
			default:
				if (cval->control == UAC_UD_MODE_SELECT)
					mode_sel = true;
				break;
			case UAC_VERSION_3:
				if (cval->control == UAC3_UD_MODE_SELECT)
					mode_sel = true;
				break;
			}

			if (mode_sel) {
				__u8 *control_spec = uac_processing_unit_specific(desc,
								state->mixer->protocol);
				cval->min = 1;
				cval->max = control_spec[0];
				cval->res = 1;
				cval->initialized = 1;
				break;
			}

			get_min_max(cval, valinfo->min_value);
			break;
		}
		case USB_XU_CLOCK_RATE:
			/*
			 * E-Mu USB 0404/0202/TrackerPre/0204
			 * samplerate control quirk
			 */
			cval->min = 0;
			cval->max = 5;
			cval->res = 1;
			cval->initialized = 1;
			break;
		default:
			get_min_max(cval, valinfo->min_value);
			break;
		}

		kctl = snd_ctl_new1(&mixer_procunit_ctl, cval);
		if (!kctl) {
			usb_mixer_elem_info_free(cval);
			return -ENOMEM;
		}
		kctl->private_free = snd_usb_mixer_elem_free;

		if (check_mapped_name(map, kctl->id.name, sizeof(kctl->id.name))) {
			/* nothing */ ;
		} else if (info->name) {
			strlcpy(kctl->id.name, info->name, sizeof(kctl->id.name));
		} else {
			if (extension_unit)
				nameid = uac_extension_unit_iExtension(desc, state->mixer->protocol);
			else
				nameid = uac_processing_unit_iProcessing(desc, state->mixer->protocol);
			len = 0;
			if (nameid)
				len = snd_usb_copy_string_desc(state->chip,
							       nameid,
							       kctl->id.name,
							       sizeof(kctl->id.name));
			if (!len)
				strlcpy(kctl->id.name, name, sizeof(kctl->id.name));
		}
		append_ctl_name(kctl, " ");
		append_ctl_name(kctl, valinfo->suffix);

		usb_audio_dbg(state->chip,
			      "[%d] PU [%s] ch = %d, val = %d/%d\n",
			      cval->head.id, kctl->id.name, cval->channels,
			      cval->min, cval->max);

		err = snd_usb_mixer_add_control(&cval->head, kctl);
		if (err < 0)
			return err;
	}
	return 0;
}

static int parse_audio_processing_unit(struct mixer_build *state, int unitid,
				       void *raw_desc)
{
	switch (state->mixer->protocol) {
	case UAC_VERSION_1:
	case UAC_VERSION_2:
	default:
		return build_audio_procunit(state, unitid, raw_desc,
					    procunits, false);
	case UAC_VERSION_3:
		return build_audio_procunit(state, unitid, raw_desc,
					    uac3_procunits, false);
	}
}

static int parse_audio_extension_unit(struct mixer_build *state, int unitid,
				      void *raw_desc)
{
	/*
	 * Note that we parse extension units with processing unit descriptors.
	 * That's ok as the layout is the same.
	 */
	return build_audio_procunit(state, unitid, raw_desc, extunits, true);
}

/*
 * Selector Unit
 */

/*
 * info callback for selector unit
 * use an enumerator type for routing
 */
static int mixer_ctl_selector_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	const char **itemlist = (const char **)kcontrol->private_value;

	if (snd_BUG_ON(!itemlist))
		return -EINVAL;
	return snd_ctl_enum_info(uinfo, 1, cval->max, itemlist);
}

/* get callback for selector unit */
static int mixer_ctl_selector_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, err;

	err = get_cur_ctl_value(cval, cval->control << 8, &val);
	if (err < 0) {
		ucontrol->value.enumerated.item[0] = 0;
		return filter_error(cval, err);
	}
	val = get_relative_value(cval, val);
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

/* put callback for selector unit */
static int mixer_ctl_selector_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_info *cval = kcontrol->private_data;
	int val, oval, err;

	err = get_cur_ctl_value(cval, cval->control << 8, &oval);
	if (err < 0)
		return filter_error(cval, err);
	val = ucontrol->value.enumerated.item[0];
	val = get_abs_value(cval, val);
	if (val != oval) {
		set_cur_ctl_value(cval, cval->control << 8, val);
		return 1;
	}
	return 0;
}

/* alsa control interface for selector unit */
static const struct snd_kcontrol_new mixer_selectunit_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "", /* will be filled later */
	.info = mixer_ctl_selector_info,
	.get = mixer_ctl_selector_get,
	.put = mixer_ctl_selector_put,
};

/*
 * private free callback.
 * free both private_data and private_value
 */
static void usb_mixer_selector_elem_free(struct snd_kcontrol *kctl)
{
	int i, num_ins = 0;

	if (kctl->private_data) {
		struct usb_mixer_elem_info *cval = kctl->private_data;
		num_ins = cval->max;
		usb_mixer_elem_info_free(cval);
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
static int parse_audio_selector_unit(struct mixer_build *state, int unitid,
				     void *raw_desc)
{
	struct uac_selector_unit_descriptor *desc = raw_desc;
	unsigned int i, nameid, len;
	int err;
	struct usb_mixer_elem_info *cval;
	struct snd_kcontrol *kctl;
	const struct usbmix_name_map *map;
	char **namelist;

	for (i = 0; i < desc->bNrInPins; i++) {
		err = parse_audio_unit(state, desc->baSourceID[i]);
		if (err < 0)
			return err;
	}

	if (desc->bNrInPins == 1) /* only one ? nonsense! */
		return 0;

	map = find_map(state->map, unitid, 0);
	if (check_ignored_ctl(map))
		return 0;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (!cval)
		return -ENOMEM;
	snd_usb_mixer_elem_init_std(&cval->head, state->mixer, unitid);
	cval->val_type = USB_MIXER_U8;
	cval->channels = 1;
	cval->min = 1;
	cval->max = desc->bNrInPins;
	cval->res = 1;
	cval->initialized = 1;

	switch (state->mixer->protocol) {
	case UAC_VERSION_1:
	default:
		cval->control = 0;
		break;
	case UAC_VERSION_2:
	case UAC_VERSION_3:
		if (desc->bDescriptorSubtype == UAC2_CLOCK_SELECTOR ||
		    desc->bDescriptorSubtype == UAC3_CLOCK_SELECTOR)
			cval->control = UAC2_CX_CLOCK_SELECTOR;
		else /* UAC2/3_SELECTOR_UNIT */
			cval->control = UAC2_SU_SELECTOR;
		break;
	}

	namelist = kcalloc(desc->bNrInPins, sizeof(char *), GFP_KERNEL);
	if (!namelist) {
		err = -ENOMEM;
		goto error_cval;
	}
#define MAX_ITEM_NAME_LEN	64
	for (i = 0; i < desc->bNrInPins; i++) {
		struct usb_audio_term iterm;
		len = 0;
		namelist[i] = kmalloc(MAX_ITEM_NAME_LEN, GFP_KERNEL);
		if (!namelist[i]) {
			err = -ENOMEM;
			goto error_name;
		}
		len = check_mapped_selector_name(state, unitid, i, namelist[i],
						 MAX_ITEM_NAME_LEN);
		if (! len && check_input_term(state, desc->baSourceID[i], &iterm) >= 0)
			len = get_term_name(state->chip, &iterm, namelist[i],
					    MAX_ITEM_NAME_LEN, 0);
		if (! len)
			sprintf(namelist[i], "Input %u", i);
	}

	kctl = snd_ctl_new1(&mixer_selectunit_ctl, cval);
	if (! kctl) {
		usb_audio_err(state->chip, "cannot malloc kcontrol\n");
		err = -ENOMEM;
		goto error_name;
		return -ENOMEM;
	}
	kctl->private_value = (unsigned long)namelist;
	kctl->private_free = usb_mixer_selector_elem_free;

	/* check the static mapping table at first */
	len = check_mapped_name(map, kctl->id.name, sizeof(kctl->id.name));
	if (!len) {
		/* no mapping ? */
		switch (state->mixer->protocol) {
		case UAC_VERSION_1:
		case UAC_VERSION_2:
		default:
		/* if iSelector is given, use it */
			nameid = uac_selector_unit_iSelector(desc);
			if (nameid)
				len = snd_usb_copy_string_desc(state->chip,
							nameid, kctl->id.name,
							sizeof(kctl->id.name));
			break;
		case UAC_VERSION_3:
			/* TODO: Class-Specific strings not yet supported */
			break;
		}

		/* ... or pick up the terminal name at next */
		if (!len)
			len = get_term_name(state->chip, &state->oterm,
				    kctl->id.name, sizeof(kctl->id.name), 0);
		/* ... or use the fixed string "USB" as the last resort */
		if (!len)
			strlcpy(kctl->id.name, "USB", sizeof(kctl->id.name));

		/* and add the proper suffix */
		if (desc->bDescriptorSubtype == UAC2_CLOCK_SELECTOR ||
		    desc->bDescriptorSubtype == UAC3_CLOCK_SELECTOR)
			append_ctl_name(kctl, " Clock Source");
		else if ((state->oterm.type & 0xff00) == 0x0100)
			append_ctl_name(kctl, " Capture Source");
		else
			append_ctl_name(kctl, " Playback Source");
	}

	usb_audio_dbg(state->chip, "[%d] SU [%s] items = %d\n",
		    cval->head.id, kctl->id.name, desc->bNrInPins);
	return snd_usb_mixer_add_control(&cval->head, kctl);

 error_name:
	for (i = 0; i < desc->bNrInPins; i++)
		kfree(namelist[i]);
	kfree(namelist);
 error_cval:
	usb_mixer_elem_info_free(cval);
	return err;
}

/*
 * parse an audio unit recursively
 */

static int parse_audio_unit(struct mixer_build *state, int unitid)
{
	unsigned char *p1;
	int protocol = state->mixer->protocol;

	if (test_and_set_bit(unitid, state->unitbitmap))
		return 0; /* the unit already visited */

	p1 = find_audio_control_unit(state, unitid);
	if (!p1) {
		usb_audio_err(state->chip, "unit %d not found!\n", unitid);
		return -EINVAL;
	}

	if (!snd_usb_validate_audio_desc(p1, protocol)) {
		usb_audio_dbg(state->chip, "invalid unit %d\n", unitid);
		return 0; /* skip invalid unit */
	}

#define PTYPE(a, b)	((a) << 8 | (b))
	switch (PTYPE(protocol, p1[2])) {
	case PTYPE(UAC_VERSION_1, UAC_INPUT_TERMINAL):
	case PTYPE(UAC_VERSION_2, UAC_INPUT_TERMINAL):
	case PTYPE(UAC_VERSION_3, UAC_INPUT_TERMINAL):
		return parse_audio_input_terminal(state, unitid, p1);
	case PTYPE(UAC_VERSION_1, UAC_MIXER_UNIT):
	case PTYPE(UAC_VERSION_2, UAC_MIXER_UNIT):
	case PTYPE(UAC_VERSION_3, UAC3_MIXER_UNIT):
		return parse_audio_mixer_unit(state, unitid, p1);
	case PTYPE(UAC_VERSION_2, UAC2_CLOCK_SOURCE):
	case PTYPE(UAC_VERSION_3, UAC3_CLOCK_SOURCE):
		return parse_clock_source_unit(state, unitid, p1);
	case PTYPE(UAC_VERSION_1, UAC_SELECTOR_UNIT):
	case PTYPE(UAC_VERSION_2, UAC_SELECTOR_UNIT):
	case PTYPE(UAC_VERSION_3, UAC3_SELECTOR_UNIT):
	case PTYPE(UAC_VERSION_2, UAC2_CLOCK_SELECTOR):
	case PTYPE(UAC_VERSION_3, UAC3_CLOCK_SELECTOR):
		return parse_audio_selector_unit(state, unitid, p1);
	case PTYPE(UAC_VERSION_1, UAC_FEATURE_UNIT):
	case PTYPE(UAC_VERSION_2, UAC_FEATURE_UNIT):
	case PTYPE(UAC_VERSION_3, UAC3_FEATURE_UNIT):
		return parse_audio_feature_unit(state, unitid, p1);
	case PTYPE(UAC_VERSION_1, UAC1_PROCESSING_UNIT):
	case PTYPE(UAC_VERSION_2, UAC2_PROCESSING_UNIT_V2):
	case PTYPE(UAC_VERSION_3, UAC3_PROCESSING_UNIT):
		return parse_audio_processing_unit(state, unitid, p1);
	case PTYPE(UAC_VERSION_1, UAC1_EXTENSION_UNIT):
	case PTYPE(UAC_VERSION_2, UAC2_EXTENSION_UNIT_V2):
	case PTYPE(UAC_VERSION_3, UAC3_EXTENSION_UNIT):
		return parse_audio_extension_unit(state, unitid, p1);
	case PTYPE(UAC_VERSION_2, UAC2_EFFECT_UNIT):
	case PTYPE(UAC_VERSION_3, UAC3_EFFECT_UNIT):
		return 0; /* FIXME - effect units not implemented yet */
	default:
		usb_audio_err(state->chip,
			      "unit %u: unexpected type 0x%02x\n",
			      unitid, p1[2]);
		return -EINVAL;
	}
}

static void snd_usb_mixer_free(struct usb_mixer_interface *mixer)
{
	/* kill pending URBs */
	snd_usb_mixer_disconnect(mixer);

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

/* UAC3 predefined channels configuration */
struct uac3_badd_profile {
	int subclass;
	const char *name;
	int c_chmask;	/* capture channels mask */
	int p_chmask;	/* playback channels mask */
	int st_chmask;	/* side tone mixing channel mask */
};

static struct uac3_badd_profile uac3_badd_profiles[] = {
	{
		/*
		 * BAIF, BAOF or combination of both
		 * IN: Mono or Stereo cfg, Mono alt possible
		 * OUT: Mono or Stereo cfg, Mono alt possible
		 */
		.subclass = UAC3_FUNCTION_SUBCLASS_GENERIC_IO,
		.name = "GENERIC IO",
		.c_chmask = -1,		/* dynamic channels */
		.p_chmask = -1,		/* dynamic channels */
	},
	{
		/* BAOF; Stereo only cfg, Mono alt possible */
		.subclass = UAC3_FUNCTION_SUBCLASS_HEADPHONE,
		.name = "HEADPHONE",
		.p_chmask = 3,
	},
	{
		/* BAOF; Mono or Stereo cfg, Mono alt possible */
		.subclass = UAC3_FUNCTION_SUBCLASS_SPEAKER,
		.name = "SPEAKER",
		.p_chmask = -1,		/* dynamic channels */
	},
	{
		/* BAIF; Mono or Stereo cfg, Mono alt possible */
		.subclass = UAC3_FUNCTION_SUBCLASS_MICROPHONE,
		.name = "MICROPHONE",
		.c_chmask = -1,		/* dynamic channels */
	},
	{
		/*
		 * BAIOF topology
		 * IN: Mono only
		 * OUT: Mono or Stereo cfg, Mono alt possible
		 */
		.subclass = UAC3_FUNCTION_SUBCLASS_HEADSET,
		.name = "HEADSET",
		.c_chmask = 1,
		.p_chmask = -1,		/* dynamic channels */
		.st_chmask = 1,
	},
	{
		/* BAIOF; IN: Mono only; OUT: Stereo only, Mono alt possible */
		.subclass = UAC3_FUNCTION_SUBCLASS_HEADSET_ADAPTER,
		.name = "HEADSET ADAPTER",
		.c_chmask = 1,
		.p_chmask = 3,
		.st_chmask = 1,
	},
	{
		/* BAIF + BAOF; IN: Mono only; OUT: Mono only */
		.subclass = UAC3_FUNCTION_SUBCLASS_SPEAKERPHONE,
		.name = "SPEAKERPHONE",
		.c_chmask = 1,
		.p_chmask = 1,
	},
	{ 0 } /* terminator */
};

static bool uac3_badd_func_has_valid_channels(struct usb_mixer_interface *mixer,
					      struct uac3_badd_profile *f,
					      int c_chmask, int p_chmask)
{
	/*
	 * If both playback/capture channels are dynamic, make sure
	 * at least one channel is present
	 */
	if (f->c_chmask < 0 && f->p_chmask < 0) {
		if (!c_chmask && !p_chmask) {
			usb_audio_warn(mixer->chip, "BAAD %s: no channels?",
				       f->name);
			return false;
		}
		return true;
	}

	if ((f->c_chmask < 0 && !c_chmask) ||
	    (f->c_chmask >= 0 && f->c_chmask != c_chmask)) {
		usb_audio_warn(mixer->chip, "BAAD %s c_chmask mismatch",
			       f->name);
		return false;
	}
	if ((f->p_chmask < 0 && !p_chmask) ||
	    (f->p_chmask >= 0 && f->p_chmask != p_chmask)) {
		usb_audio_warn(mixer->chip, "BAAD %s p_chmask mismatch",
			       f->name);
		return false;
	}
	return true;
}

/*
 * create mixer controls for UAC3 BADD profiles
 *
 * UAC3 BADD device doesn't contain CS descriptors thus we will guess everything
 *
 * BADD device may contain Mixer Unit, which doesn't have any controls, skip it
 */
static int snd_usb_mixer_controls_badd(struct usb_mixer_interface *mixer,
				       int ctrlif)
{
	struct usb_device *dev = mixer->chip->dev;
	struct usb_interface_assoc_descriptor *assoc;
	int badd_profile = mixer->chip->badd_profile;
	struct uac3_badd_profile *f;
	const struct usbmix_ctl_map *map;
	int p_chmask = 0, c_chmask = 0, st_chmask = 0;
	int i;

	assoc = usb_ifnum_to_if(dev, ctrlif)->intf_assoc;

	/* Detect BADD capture/playback channels from AS EP descriptors */
	for (i = 0; i < assoc->bInterfaceCount; i++) {
		int intf = assoc->bFirstInterface + i;

		struct usb_interface *iface;
		struct usb_host_interface *alts;
		struct usb_interface_descriptor *altsd;
		unsigned int maxpacksize;
		char dir_in;
		int chmask, num;

		if (intf == ctrlif)
			continue;

		iface = usb_ifnum_to_if(dev, intf);
		num = iface->num_altsetting;

		if (num < 2)
			return -EINVAL;

		/*
		 * The number of Channels in an AudioStreaming interface
		 * and the audio sample bit resolution (16 bits or 24
		 * bits) can be derived from the wMaxPacketSize field in
		 * the Standard AS Audio Data Endpoint descriptor in
		 * Alternate Setting 1
		 */
		alts = &iface->altsetting[1];
		altsd = get_iface_desc(alts);

		if (altsd->bNumEndpoints < 1)
			return -EINVAL;

		/* check direction */
		dir_in = (get_endpoint(alts, 0)->bEndpointAddress & USB_DIR_IN);
		maxpacksize = le16_to_cpu(get_endpoint(alts, 0)->wMaxPacketSize);

		switch (maxpacksize) {
		default:
			usb_audio_err(mixer->chip,
				"incorrect wMaxPacketSize 0x%x for BADD profile\n",
				maxpacksize);
			return -EINVAL;
		case UAC3_BADD_EP_MAXPSIZE_SYNC_MONO_16:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_MONO_16:
		case UAC3_BADD_EP_MAXPSIZE_SYNC_MONO_24:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_MONO_24:
			chmask = 1;
			break;
		case UAC3_BADD_EP_MAXPSIZE_SYNC_STEREO_16:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_STEREO_16:
		case UAC3_BADD_EP_MAXPSIZE_SYNC_STEREO_24:
		case UAC3_BADD_EP_MAXPSIZE_ASYNC_STEREO_24:
			chmask = 3;
			break;
		}

		if (dir_in)
			c_chmask = chmask;
		else
			p_chmask = chmask;
	}

	usb_audio_dbg(mixer->chip,
		"UAC3 BADD profile 0x%x: detected c_chmask=%d p_chmask=%d\n",
		badd_profile, c_chmask, p_chmask);

	/* check the mapping table */
	for (map = uac3_badd_usbmix_ctl_maps; map->id; map++) {
		if (map->id == badd_profile)
			break;
	}

	if (!map->id)
		return -EINVAL;

	for (f = uac3_badd_profiles; f->name; f++) {
		if (badd_profile == f->subclass)
			break;
	}
	if (!f->name)
		return -EINVAL;
	if (!uac3_badd_func_has_valid_channels(mixer, f, c_chmask, p_chmask))
		return -EINVAL;
	st_chmask = f->st_chmask;

	/* Playback */
	if (p_chmask) {
		/* Master channel, always writable */
		build_feature_ctl_badd(mixer, 0, UAC_FU_MUTE,
				       UAC3_BADD_FU_ID2, map->map);
		/* Mono/Stereo volume channels, always writable */
		build_feature_ctl_badd(mixer, p_chmask, UAC_FU_VOLUME,
				       UAC3_BADD_FU_ID2, map->map);
	}

	/* Capture */
	if (c_chmask) {
		/* Master channel, always writable */
		build_feature_ctl_badd(mixer, 0, UAC_FU_MUTE,
				       UAC3_BADD_FU_ID5, map->map);
		/* Mono/Stereo volume channels, always writable */
		build_feature_ctl_badd(mixer, c_chmask, UAC_FU_VOLUME,
				       UAC3_BADD_FU_ID5, map->map);
	}

	/* Side tone-mixing */
	if (st_chmask) {
		/* Master channel, always writable */
		build_feature_ctl_badd(mixer, 0, UAC_FU_MUTE,
				       UAC3_BADD_FU_ID7, map->map);
		/* Mono volume channel, always writable */
		build_feature_ctl_badd(mixer, 1, UAC_FU_VOLUME,
				       UAC3_BADD_FU_ID7, map->map);
	}

	/* Insertion Control */
	if (f->subclass == UAC3_FUNCTION_SUBCLASS_HEADSET_ADAPTER) {
		struct usb_audio_term iterm, oterm;

		/* Input Term - Insertion control */
		memset(&iterm, 0, sizeof(iterm));
		iterm.id = UAC3_BADD_IT_ID4;
		iterm.type = UAC_BIDIR_TERMINAL_HEADSET;
		build_connector_control(mixer, &iterm, true);

		/* Output Term - Insertion control */
		memset(&oterm, 0, sizeof(oterm));
		oterm.id = UAC3_BADD_OT_ID3;
		oterm.type = UAC_BIDIR_TERMINAL_HEADSET;
		build_connector_control(mixer, &oterm, false);
	}

	return 0;
}

/*
 * create mixer controls
 *
 * walk through all UAC_OUTPUT_TERMINAL descriptors to search for mixers
 */
static int snd_usb_mixer_controls(struct usb_mixer_interface *mixer)
{
	struct mixer_build state;
	int err;
	const struct usbmix_ctl_map *map;
	void *p;

	memset(&state, 0, sizeof(state));
	state.chip = mixer->chip;
	state.mixer = mixer;
	state.buffer = mixer->hostif->extra;
	state.buflen = mixer->hostif->extralen;

	/* check the mapping table */
	for (map = usbmix_ctl_maps; map->id; map++) {
		if (map->id == state.chip->usb_id) {
			state.map = map->map;
			state.selector_map = map->selector_map;
			mixer->ignore_ctl_error = map->ignore_ctl_error;
			break;
		}
	}

	p = NULL;
	while ((p = snd_usb_find_csint_desc(mixer->hostif->extra,
					    mixer->hostif->extralen,
					    p, UAC_OUTPUT_TERMINAL)) != NULL) {
		if (!snd_usb_validate_audio_desc(p, mixer->protocol))
			continue; /* skip invalid descriptor */

		if (mixer->protocol == UAC_VERSION_1) {
			struct uac1_output_terminal_descriptor *desc = p;

			/* mark terminal ID as visited */
			set_bit(desc->bTerminalID, state.unitbitmap);
			state.oterm.id = desc->bTerminalID;
			state.oterm.type = le16_to_cpu(desc->wTerminalType);
			state.oterm.name = desc->iTerminal;
			err = parse_audio_unit(&state, desc->bSourceID);
			if (err < 0 && err != -EINVAL)
				return err;
		} else if (mixer->protocol == UAC_VERSION_2) {
			struct uac2_output_terminal_descriptor *desc = p;

			/* mark terminal ID as visited */
			set_bit(desc->bTerminalID, state.unitbitmap);
			state.oterm.id = desc->bTerminalID;
			state.oterm.type = le16_to_cpu(desc->wTerminalType);
			state.oterm.name = desc->iTerminal;
			err = parse_audio_unit(&state, desc->bSourceID);
			if (err < 0 && err != -EINVAL)
				return err;

			/*
			 * For UAC2, use the same approach to also add the
			 * clock selectors
			 */
			err = parse_audio_unit(&state, desc->bCSourceID);
			if (err < 0 && err != -EINVAL)
				return err;

			if (uac_v2v3_control_is_readable(le16_to_cpu(desc->bmControls),
							 UAC2_TE_CONNECTOR)) {
				build_connector_control(state.mixer, &state.oterm,
							false);
			}
		} else {  /* UAC_VERSION_3 */
			struct uac3_output_terminal_descriptor *desc = p;

			/* mark terminal ID as visited */
			set_bit(desc->bTerminalID, state.unitbitmap);
			state.oterm.id = desc->bTerminalID;
			state.oterm.type = le16_to_cpu(desc->wTerminalType);
			state.oterm.name = le16_to_cpu(desc->wTerminalDescrStr);
			err = parse_audio_unit(&state, desc->bSourceID);
			if (err < 0 && err != -EINVAL)
				return err;

			/*
			 * For UAC3, use the same approach to also add the
			 * clock selectors
			 */
			err = parse_audio_unit(&state, desc->bCSourceID);
			if (err < 0 && err != -EINVAL)
				return err;

			if (uac_v2v3_control_is_readable(le32_to_cpu(desc->bmControls),
							 UAC3_TE_INSERTION)) {
				build_connector_control(state.mixer, &state.oterm,
							false);
			}
		}
	}

	return 0;
}

void snd_usb_mixer_notify_id(struct usb_mixer_interface *mixer, int unitid)
{
	struct usb_mixer_elem_list *list;

	for_each_mixer_elem(list, mixer, unitid) {
		struct usb_mixer_elem_info *info =
			mixer_elem_list_to_info(list);
		/* invalidate cache, so the value is read from the device */
		info->cached = 0;
		snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &list->kctl->id);
	}
}

static void snd_usb_mixer_dump_cval(struct snd_info_buffer *buffer,
				    struct usb_mixer_elem_list *list)
{
	struct usb_mixer_elem_info *cval = mixer_elem_list_to_info(list);
	static char *val_types[] = {"BOOLEAN", "INV_BOOLEAN",
				    "S8", "U8", "S16", "U16"};
	snd_iprintf(buffer, "    Info: id=%i, control=%i, cmask=0x%x, "
			    "channels=%i, type=\"%s\"\n", cval->head.id,
			    cval->control, cval->cmask, cval->channels,
			    val_types[cval->val_type]);
	snd_iprintf(buffer, "    Volume: min=%i, max=%i, dBmin=%i, dBmax=%i\n",
			    cval->min, cval->max, cval->dBmin, cval->dBmax);
}

static void snd_usb_mixer_proc_read(struct snd_info_entry *entry,
				    struct snd_info_buffer *buffer)
{
	struct snd_usb_audio *chip = entry->private_data;
	struct usb_mixer_interface *mixer;
	struct usb_mixer_elem_list *list;
	int unitid;

	list_for_each_entry(mixer, &chip->mixer_list, list) {
		snd_iprintf(buffer,
			"USB Mixer: usb_id=0x%08x, ctrlif=%i, ctlerr=%i\n",
				chip->usb_id, snd_usb_ctrl_intf(chip),
				mixer->ignore_ctl_error);
		snd_iprintf(buffer, "Card: %s\n", chip->card->longname);
		for (unitid = 0; unitid < MAX_ID_ELEMS; unitid++) {
			for_each_mixer_elem(list, mixer, unitid) {
				snd_iprintf(buffer, "  Unit: %i\n", list->id);
				if (list->kctl)
					snd_iprintf(buffer,
						    "    Control: name=\"%s\", index=%i\n",
						    list->kctl->id.name,
						    list->kctl->id.index);
				if (list->dump)
					list->dump(buffer, list);
			}
		}
	}
}

static void snd_usb_mixer_interrupt_v2(struct usb_mixer_interface *mixer,
				       int attribute, int value, int index)
{
	struct usb_mixer_elem_list *list;
	__u8 unitid = (index >> 8) & 0xff;
	__u8 control = (value >> 8) & 0xff;
	__u8 channel = value & 0xff;
	unsigned int count = 0;

	if (channel >= MAX_CHANNELS) {
		usb_audio_dbg(mixer->chip,
			"%s(): bogus channel number %d\n",
			__func__, channel);
		return;
	}

	for_each_mixer_elem(list, mixer, unitid)
		count++;

	if (count == 0)
		return;

	for_each_mixer_elem(list, mixer, unitid) {
		struct usb_mixer_elem_info *info;

		if (!list->kctl)
			continue;

		info = mixer_elem_list_to_info(list);
		if (count > 1 && info->control != control)
			continue;

		switch (attribute) {
		case UAC2_CS_CUR:
			/* invalidate cache, so the value is read from the device */
			if (channel)
				info->cached &= ~(1 << channel);
			else /* master channel */
				info->cached = 0;

			snd_ctl_notify(mixer->chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &info->head.kctl->id);
			break;

		case UAC2_CS_RANGE:
			/* TODO */
			break;

		case UAC2_CS_MEM:
			/* TODO */
			break;

		default:
			usb_audio_dbg(mixer->chip,
				"unknown attribute %d in interrupt\n",
				attribute);
			break;
		} /* switch */
	}
}

static void snd_usb_mixer_interrupt(struct urb *urb)
{
	struct usb_mixer_interface *mixer = urb->context;
	int len = urb->actual_length;
	int ustatus = urb->status;

	if (ustatus != 0)
		goto requeue;

	if (mixer->protocol == UAC_VERSION_1) {
		struct uac1_status_word *status;

		for (status = urb->transfer_buffer;
		     len >= sizeof(*status);
		     len -= sizeof(*status), status++) {
			dev_dbg(&urb->dev->dev, "status interrupt: %02x %02x\n",
						status->bStatusType,
						status->bOriginator);

			/* ignore any notifications not from the control interface */
			if ((status->bStatusType & UAC1_STATUS_TYPE_ORIG_MASK) !=
				UAC1_STATUS_TYPE_ORIG_AUDIO_CONTROL_IF)
				continue;

			if (status->bStatusType & UAC1_STATUS_TYPE_MEM_CHANGED)
				snd_usb_mixer_rc_memory_change(mixer, status->bOriginator);
			else
				snd_usb_mixer_notify_id(mixer, status->bOriginator);
		}
	} else { /* UAC_VERSION_2 */
		struct uac2_interrupt_data_msg *msg;

		for (msg = urb->transfer_buffer;
		     len >= sizeof(*msg);
		     len -= sizeof(*msg), msg++) {
			/* drop vendor specific and endpoint requests */
			if ((msg->bInfo & UAC2_INTERRUPT_DATA_MSG_VENDOR) ||
			    (msg->bInfo & UAC2_INTERRUPT_DATA_MSG_EP))
				continue;

			snd_usb_mixer_interrupt_v2(mixer, msg->bAttribute,
						   le16_to_cpu(msg->wValue),
						   le16_to_cpu(msg->wIndex));
		}
	}

requeue:
	if (ustatus != -ENOENT &&
	    ustatus != -ECONNRESET &&
	    ustatus != -ESHUTDOWN) {
		urb->dev = mixer->chip->dev;
		usb_submit_urb(urb, GFP_ATOMIC);
	}
}

/* create the handler for the optional status interrupt endpoint */
static int snd_usb_mixer_status_create(struct usb_mixer_interface *mixer)
{
	struct usb_endpoint_descriptor *ep;
	void *transfer_buffer;
	int buffer_length;
	unsigned int epnum;

	/* we need one interrupt input endpoint */
	if (get_iface_desc(mixer->hostif)->bNumEndpoints < 1)
		return 0;
	ep = get_endpoint(mixer->hostif, 0);
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
			 snd_usb_mixer_interrupt, mixer, ep->bInterval);
	usb_submit_urb(mixer->urb, GFP_KERNEL);
	return 0;
}

static int keep_iface_ctl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_interface *mixer = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = mixer->chip->keep_iface;
	return 0;
}

static int keep_iface_ctl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_interface *mixer = snd_kcontrol_chip(kcontrol);
	bool keep_iface = !!ucontrol->value.integer.value[0];

	if (mixer->chip->keep_iface == keep_iface)
		return 0;
	mixer->chip->keep_iface = keep_iface;
	return 1;
}

static const struct snd_kcontrol_new keep_iface_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_CARD,
	.name = "Keep Interface",
	.info = snd_ctl_boolean_mono_info,
	.get = keep_iface_ctl_get,
	.put = keep_iface_ctl_put,
};

static int create_keep_iface_ctl(struct usb_mixer_interface *mixer)
{
	struct snd_kcontrol *kctl = snd_ctl_new1(&keep_iface_ctl, mixer);

	/* need only one control per card */
	if (snd_ctl_find_id(mixer->chip->card, &kctl->id)) {
		snd_ctl_free_one(kctl);
		return 0;
	}

	return snd_ctl_add(mixer->chip->card, kctl);
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
	mixer->ignore_ctl_error = ignore_error;
	mixer->id_elems = kcalloc(MAX_ID_ELEMS, sizeof(*mixer->id_elems),
				  GFP_KERNEL);
	if (!mixer->id_elems) {
		kfree(mixer);
		return -ENOMEM;
	}

	mixer->hostif = &usb_ifnum_to_if(chip->dev, ctrlif)->altsetting[0];
	switch (get_iface_desc(mixer->hostif)->bInterfaceProtocol) {
	case UAC_VERSION_1:
	default:
		mixer->protocol = UAC_VERSION_1;
		break;
	case UAC_VERSION_2:
		mixer->protocol = UAC_VERSION_2;
		break;
	case UAC_VERSION_3:
		mixer->protocol = UAC_VERSION_3;
		break;
	}

	if (mixer->protocol == UAC_VERSION_3 &&
			chip->badd_profile >= UAC3_FUNCTION_SUBCLASS_GENERIC_IO) {
		err = snd_usb_mixer_controls_badd(mixer, ctrlif);
		if (err < 0)
			goto _error;
	} else {
		err = snd_usb_mixer_controls(mixer);
		if (err < 0)
			goto _error;
	}

	err = snd_usb_mixer_status_create(mixer);
	if (err < 0)
		goto _error;

	err = create_keep_iface_ctl(mixer);
	if (err < 0)
		goto _error;

	err = snd_usb_mixer_apply_create_quirk(mixer);
	if (err < 0)
		goto _error;

	err = snd_device_new(chip->card, SNDRV_DEV_CODEC, mixer, &dev_ops);
	if (err < 0)
		goto _error;

	if (list_empty(&chip->mixer_list))
		snd_card_ro_proc_new(chip->card, "usbmixer", chip,
				     snd_usb_mixer_proc_read);

	list_add(&mixer->list, &chip->mixer_list);
	return 0;

_error:
	snd_usb_mixer_free(mixer);
	return err;
}

void snd_usb_mixer_disconnect(struct usb_mixer_interface *mixer)
{
	if (mixer->disconnected)
		return;
	if (mixer->urb)
		usb_kill_urb(mixer->urb);
	if (mixer->rc_urb)
		usb_kill_urb(mixer->rc_urb);
	mixer->disconnected = true;
}

#ifdef CONFIG_PM
/* stop any bus activity of a mixer */
static void snd_usb_mixer_inactivate(struct usb_mixer_interface *mixer)
{
	usb_kill_urb(mixer->urb);
	usb_kill_urb(mixer->rc_urb);
}

static int snd_usb_mixer_activate(struct usb_mixer_interface *mixer)
{
	int err;

	if (mixer->urb) {
		err = usb_submit_urb(mixer->urb, GFP_NOIO);
		if (err < 0)
			return err;
	}

	return 0;
}

int snd_usb_mixer_suspend(struct usb_mixer_interface *mixer)
{
	snd_usb_mixer_inactivate(mixer);
	return 0;
}

static int restore_mixer_value(struct usb_mixer_elem_list *list)
{
	struct usb_mixer_elem_info *cval = mixer_elem_list_to_info(list);
	int c, err, idx;

	if (cval->cmask) {
		idx = 0;
		for (c = 0; c < MAX_CHANNELS; c++) {
			if (!(cval->cmask & (1 << c)))
				continue;
			if (cval->cached & (1 << (c + 1))) {
				err = snd_usb_set_cur_mix_value(cval, c + 1, idx,
							cval->cache_val[idx]);
				if (err < 0)
					return err;
			}
			idx++;
		}
	} else {
		/* master */
		if (cval->cached) {
			err = snd_usb_set_cur_mix_value(cval, 0, 0, *cval->cache_val);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

int snd_usb_mixer_resume(struct usb_mixer_interface *mixer, bool reset_resume)
{
	struct usb_mixer_elem_list *list;
	int id, err;

	if (reset_resume) {
		/* restore cached mixer values */
		for (id = 0; id < MAX_ID_ELEMS; id++) {
			for_each_mixer_elem(list, mixer, id) {
				if (list->resume) {
					err = list->resume(list);
					if (err < 0)
						return err;
				}
			}
		}
	}

	snd_usb_mixer_resume_quirk(mixer);

	return snd_usb_mixer_activate(mixer);
}
#endif

void snd_usb_mixer_elem_init_std(struct usb_mixer_elem_list *list,
				 struct usb_mixer_interface *mixer,
				 int unitid)
{
	list->mixer = mixer;
	list->id = unitid;
	list->dump = snd_usb_mixer_dump_cval;
#ifdef CONFIG_PM
	list->resume = restore_mixer_value;
#endif
}
