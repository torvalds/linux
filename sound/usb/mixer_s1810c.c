// SPDX-License-Identifier: GPL-2.0
/*
 * Presonus Studio 1810c driver for ALSA
 * Copyright (C) 2019 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Based on reverse engineering of the communication protocol
 * between the windows driver / Univeral Control (UC) program
 * and the device, through usbmon.
 *
 * For now this bypasses the mixer, with all channels split,
 * so that the software can mix with greater flexibility.
 * It also adds controls for the 4 buttons on the front of
 * the device.
 */

#include <linux/usb.h>
#include <linux/usb/audio-v2.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>

#include "usbaudio.h"
#include "mixer.h"
#include "mixer_quirks.h"
#include "helper.h"
#include "mixer_s1810c.h"

#define SC1810C_CMD_REQ	160
#define SC1810C_CMD_REQTYPE \
	(USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT)
#define SC1810C_CMD_F1		0x50617269
#define SC1810C_CMD_F2		0x14

/*
 * DISCLAIMER: These are just guesses based on the
 * dumps I got.
 *
 * It seems like a selects between
 * device (0), mixer (0x64) and output (0x65)
 *
 * For mixer (0x64):
 *  * b selects an input channel (see below).
 *  * c selects an output channel pair (see below).
 *  * d selects left (0) or right (1) of that pair.
 *  * e 0-> disconnect, 0x01000000-> connect,
 *	0x0109-> used for stereo-linking channels,
 *	e is also used for setting volume levels
 *	in which case b is also set so I guess
 *	this way it is possible to set the volume
 *	level from the specified input to the
 *	specified output.
 *
 * IN Channels:
 * 0  - 7  Mic/Inst/Line (Analog inputs)
 * 8  - 9  S/PDIF
 * 10 - 17 ADAT
 * 18 - 35 DAW (Inputs from the host)
 *
 * OUT Channels (pairs):
 * 0 -> Main out
 * 1 -> Line1/2
 * 2 -> Line3/4
 * 3 -> S/PDIF
 * 4 -> ADAT?
 *
 * For device (0):
 *  * b and c are not used, at least not on the
 *    dumps I got.
 *  * d sets the control id to be modified
 *    (see below).
 *  * e sets the setting for that control.
 *    (so for the switches I was interested
 *    in it's 0/1)
 *
 * For output (0x65):
 *   * b is the output channel (see above).
 *   * c is zero.
 *   * e I guess the same as with mixer except 0x0109
 *	 which I didn't see in my dumps.
 *
 * The two fixed fields have the same values for
 * mixer and output but a different set for device.
 */
struct s1810c_ctl_packet {
	u32 a;
	u32 b;
	u32 fixed1;
	u32 fixed2;
	u32 c;
	u32 d;
	u32 e;
};

#define SC1810C_CTL_LINE_SW	0
#define SC1810C_CTL_MUTE_SW	1
#define SC1810C_CTL_AB_SW	3
#define SC1810C_CTL_48V_SW	4

#define SC1810C_SET_STATE_REQ	161
#define SC1810C_SET_STATE_REQTYPE SC1810C_CMD_REQTYPE
#define SC1810C_SET_STATE_F1	0x64656D73
#define SC1810C_SET_STATE_F2	0xF4

#define SC1810C_GET_STATE_REQ	162
#define SC1810C_GET_STATE_REQTYPE \
	(USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN)
#define SC1810C_GET_STATE_F1	SC1810C_SET_STATE_F1
#define SC1810C_GET_STATE_F2	SC1810C_SET_STATE_F2

#define SC1810C_STATE_F1_IDX	2
#define SC1810C_STATE_F2_IDX	3

/*
 * This packet includes mixer volumes and
 * various other fields, it's an extended
 * version of ctl_packet, with a and b
 * being zero and different f1/f2.
 */
struct s1810c_state_packet {
	u32 fields[63];
};

#define SC1810C_STATE_48V_SW	58
#define SC1810C_STATE_LINE_SW	59
#define SC1810C_STATE_MUTE_SW	60
#define SC1810C_STATE_AB_SW	62

struct s1810_mixer_state {
	uint16_t seqnum;
	struct mutex usb_mutex;
	struct mutex data_mutex;
};

static int
snd_s1810c_send_ctl_packet(struct usb_device *dev, u32 a,
			   u32 b, u32 c, u32 d, u32 e)
{
	struct s1810c_ctl_packet pkt = { 0 };
	int ret = 0;

	pkt.fixed1 = SC1810C_CMD_F1;
	pkt.fixed2 = SC1810C_CMD_F2;

	pkt.a = a;
	pkt.b = b;
	pkt.c = c;
	pkt.d = d;
	/*
	 * Value for settings 0/1 for this
	 * output channel is always 0 (probably because
	 * there is no ADAT output on 1810c)
	 */
	pkt.e = (c == 4) ? 0 : e;

	ret = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0),
			      SC1810C_CMD_REQ,
			      SC1810C_CMD_REQTYPE, 0, 0, &pkt, sizeof(pkt));
	if (ret < 0) {
		dev_warn(&dev->dev, "could not send ctl packet\n");
		return ret;
	}
	return 0;
}

/*
 * When opening Universal Control the program periodically
 * sends and receives state packets for syncinc state between
 * the device and the host.
 *
 * Note that if we send only the request to get data back we'll
 * get an error, we need to first send an empty state packet and
 * then ask to receive a filled. Their seqnumbers must also match.
 */
static int
snd_sc1810c_get_status_field(struct usb_device *dev,
			     u32 *field, int field_idx, uint16_t *seqnum)
{
	struct s1810c_state_packet pkt_out = { { 0 } };
	struct s1810c_state_packet pkt_in = { { 0 } };
	int ret = 0;

	pkt_out.fields[SC1810C_STATE_F1_IDX] = SC1810C_SET_STATE_F1;
	pkt_out.fields[SC1810C_STATE_F2_IDX] = SC1810C_SET_STATE_F2;
	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			      SC1810C_SET_STATE_REQ,
			      SC1810C_SET_STATE_REQTYPE,
			      (*seqnum), 0, &pkt_out, sizeof(pkt_out));
	if (ret < 0) {
		dev_warn(&dev->dev, "could not send state packet (%d)\n", ret);
		return ret;
	}

	ret = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			      SC1810C_GET_STATE_REQ,
			      SC1810C_GET_STATE_REQTYPE,
			      (*seqnum), 0, &pkt_in, sizeof(pkt_in));
	if (ret < 0) {
		dev_warn(&dev->dev, "could not get state field %u (%d)\n",
			 field_idx, ret);
		return ret;
	}

	(*field) = pkt_in.fields[field_idx];
	(*seqnum)++;
	return 0;
}

/*
 * This is what I got when bypassing the mixer with
 * all channels split. I'm not 100% sure of what's going
 * on, I could probably clean this up based on my observations
 * but I prefer to keep the same behavior as the windows driver.
 */
static int snd_s1810c_init_mixer_maps(struct snd_usb_audio *chip)
{
	u32 a, b, c, e, n, off;
	struct usb_device *dev = chip->dev;

	/* Set initial volume levels ? */
	a = 0x64;
	e = 0xbc;
	for (n = 0; n < 2; n++) {
		off = n * 18;
		for (b = off, c = 0; b < 18 + off; b++) {
			/* This channel to all outputs ? */
			for (c = 0; c <= 8; c++) {
				snd_s1810c_send_ctl_packet(dev, a, b, c, 0, e);
				snd_s1810c_send_ctl_packet(dev, a, b, c, 1, e);
			}
			/* This channel to main output (again) */
			snd_s1810c_send_ctl_packet(dev, a, b, 0, 0, e);
			snd_s1810c_send_ctl_packet(dev, a, b, 0, 1, e);
		}
		/*
		 * I noticed on UC that DAW channels have different
		 * initial volumes, so this makes sense.
		 */
		e = 0xb53bf0;
	}

	/* Connect analog outputs ? */
	a = 0x65;
	e = 0x01000000;
	for (b = 1; b < 3; b++) {
		snd_s1810c_send_ctl_packet(dev, a, b, 0, 0, e);
		snd_s1810c_send_ctl_packet(dev, a, b, 0, 1, e);
	}
	snd_s1810c_send_ctl_packet(dev, a, 0, 0, 0, e);
	snd_s1810c_send_ctl_packet(dev, a, 0, 0, 1, e);

	/* Set initial volume levels for S/PDIF mappings ? */
	a = 0x64;
	e = 0xbc;
	c = 3;
	for (n = 0; n < 2; n++) {
		off = n * 18;
		for (b = off; b < 18 + off; b++) {
			snd_s1810c_send_ctl_packet(dev, a, b, c, 0, e);
			snd_s1810c_send_ctl_packet(dev, a, b, c, 1, e);
		}
		e = 0xb53bf0;
	}

	/* Connect S/PDIF output ? */
	a = 0x65;
	e = 0x01000000;
	snd_s1810c_send_ctl_packet(dev, a, 3, 0, 0, e);
	snd_s1810c_send_ctl_packet(dev, a, 3, 0, 1, e);

	/* Connect all outputs (again) ? */
	a = 0x65;
	e = 0x01000000;
	for (b = 0; b < 4; b++) {
		snd_s1810c_send_ctl_packet(dev, a, b, 0, 0, e);
		snd_s1810c_send_ctl_packet(dev, a, b, 0, 1, e);
	}

	/* Basic routing to get sound out of the device */
	a = 0x64;
	e = 0x01000000;
	for (c = 0; c < 4; c++) {
		for (b = 0; b < 36; b++) {
			if ((c == 0 && b == 18) ||	/* DAW1/2 -> Main */
			    (c == 1 && b == 20) ||	/* DAW3/4 -> Line3/4 */
			    (c == 2 && b == 22) ||	/* DAW4/5 -> Line5/6 */
			    (c == 3 && b == 24)) {	/* DAW5/6 -> S/PDIF */
				/* Left */
				snd_s1810c_send_ctl_packet(dev, a, b, c, 0, e);
				snd_s1810c_send_ctl_packet(dev, a, b, c, 1, 0);
				b++;
				/* Right */
				snd_s1810c_send_ctl_packet(dev, a, b, c, 0, 0);
				snd_s1810c_send_ctl_packet(dev, a, b, c, 1, e);
			} else {
				/* Leave the rest disconnected */
				snd_s1810c_send_ctl_packet(dev, a, b, c, 0, 0);
				snd_s1810c_send_ctl_packet(dev, a, b, c, 1, 0);
			}
		}
	}

	/* Set initial volume levels for S/PDIF (again) ? */
	a = 0x64;
	e = 0xbc;
	c = 3;
	for (n = 0; n < 2; n++) {
		off = n * 18;
		for (b = off; b < 18 + off; b++) {
			snd_s1810c_send_ctl_packet(dev, a, b, c, 0, e);
			snd_s1810c_send_ctl_packet(dev, a, b, c, 1, e);
		}
		e = 0xb53bf0;
	}

	/* Connect S/PDIF outputs (again) ? */
	a = 0x65;
	e = 0x01000000;
	snd_s1810c_send_ctl_packet(dev, a, 3, 0, 0, e);
	snd_s1810c_send_ctl_packet(dev, a, 3, 0, 1, e);

	/* Again ? */
	snd_s1810c_send_ctl_packet(dev, a, 3, 0, 0, e);
	snd_s1810c_send_ctl_packet(dev, a, 3, 0, 1, e);

	return 0;
}

/*
 * Sync state with the device and retrieve the requested field,
 * whose index is specified in (kctl->private_value & 0xFF),
 * from the received fields array.
 */
static int
snd_s1810c_get_switch_state(struct usb_mixer_interface *mixer,
			    struct snd_kcontrol *kctl, u32 *state)
{
	struct snd_usb_audio *chip = mixer->chip;
	struct s1810_mixer_state *private = mixer->private_data;
	u32 field = 0;
	u32 ctl_idx = (u32) (kctl->private_value & 0xFF);
	int ret = 0;

	mutex_lock(&private->usb_mutex);
	ret = snd_sc1810c_get_status_field(chip->dev, &field,
					   ctl_idx, &private->seqnum);
	if (ret < 0)
		goto unlock;

	*state = field;
 unlock:
	mutex_unlock(&private->usb_mutex);
	return ret ? ret : 0;
}

/*
 * Send a control packet to the device for the control id
 * specified in (kctl->private_value >> 8) with value
 * specified in (kctl->private_value >> 16).
 */
static int
snd_s1810c_set_switch_state(struct usb_mixer_interface *mixer,
			    struct snd_kcontrol *kctl)
{
	struct snd_usb_audio *chip = mixer->chip;
	struct s1810_mixer_state *private = mixer->private_data;
	u32 pval = (u32) kctl->private_value;
	u32 ctl_id = (pval >> 8) & 0xFF;
	u32 ctl_val = (pval >> 16) & 0x1;
	int ret = 0;

	mutex_lock(&private->usb_mutex);
	ret = snd_s1810c_send_ctl_packet(chip->dev, 0, 0, 0, ctl_id, ctl_val);
	mutex_unlock(&private->usb_mutex);
	return ret;
}

/* Generic get/set/init functions for switch controls */

static int
snd_s1810c_switch_get(struct snd_kcontrol *kctl,
		      struct snd_ctl_elem_value *ctl_elem)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *mixer = list->mixer;
	struct s1810_mixer_state *private = mixer->private_data;
	u32 pval = (u32) kctl->private_value;
	u32 ctl_idx = pval & 0xFF;
	u32 state = 0;
	int ret = 0;

	mutex_lock(&private->data_mutex);
	ret = snd_s1810c_get_switch_state(mixer, kctl, &state);
	if (ret < 0)
		goto unlock;

	switch (ctl_idx) {
	case SC1810C_STATE_LINE_SW:
	case SC1810C_STATE_AB_SW:
		ctl_elem->value.enumerated.item[0] = (int)state;
		break;
	default:
		ctl_elem->value.integer.value[0] = (long)state;
	}

 unlock:
	mutex_unlock(&private->data_mutex);
	return (ret < 0) ? ret : 0;
}

static int
snd_s1810c_switch_set(struct snd_kcontrol *kctl,
		      struct snd_ctl_elem_value *ctl_elem)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *mixer = list->mixer;
	struct s1810_mixer_state *private = mixer->private_data;
	u32 pval = (u32) kctl->private_value;
	u32 ctl_idx = pval & 0xFF;
	u32 curval = 0;
	u32 newval = 0;
	int ret = 0;

	mutex_lock(&private->data_mutex);
	ret = snd_s1810c_get_switch_state(mixer, kctl, &curval);
	if (ret < 0)
		goto unlock;

	switch (ctl_idx) {
	case SC1810C_STATE_LINE_SW:
	case SC1810C_STATE_AB_SW:
		newval = (u32) ctl_elem->value.enumerated.item[0];
		break;
	default:
		newval = (u32) ctl_elem->value.integer.value[0];
	}

	if (curval == newval)
		goto unlock;

	kctl->private_value &= ~(0x1 << 16);
	kctl->private_value |= (unsigned int)(newval & 0x1) << 16;
	ret = snd_s1810c_set_switch_state(mixer, kctl);

 unlock:
	mutex_unlock(&private->data_mutex);
	return (ret < 0) ? 0 : 1;
}

static int
snd_s1810c_switch_init(struct usb_mixer_interface *mixer,
		       const struct snd_kcontrol_new *new_kctl)
{
	struct snd_kcontrol *kctl;
	struct usb_mixer_elem_info *elem;

	elem = kzalloc(sizeof(struct usb_mixer_elem_info), GFP_KERNEL);
	if (!elem)
		return -ENOMEM;

	elem->head.mixer = mixer;
	elem->control = 0;
	elem->head.id = 0;
	elem->channels = 1;

	kctl = snd_ctl_new1(new_kctl, elem);
	if (!kctl) {
		kfree(elem);
		return -ENOMEM;
	}
	kctl->private_free = snd_usb_mixer_elem_free;

	return snd_usb_mixer_add_control(&elem->head, kctl);
}

static int
snd_s1810c_line_sw_info(struct snd_kcontrol *kctl,
			struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[2] = {
		"Preamp On (Mic/Inst)",
		"Preamp Off (Line in)"
	};

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static const struct snd_kcontrol_new snd_s1810c_line_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Line 1/2 Source Type",
	.info = snd_s1810c_line_sw_info,
	.get = snd_s1810c_switch_get,
	.put = snd_s1810c_switch_set,
	.private_value = (SC1810C_STATE_LINE_SW | SC1810C_CTL_LINE_SW << 8)
};

static const struct snd_kcontrol_new snd_s1810c_mute_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Mute Main Out Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_s1810c_switch_get,
	.put = snd_s1810c_switch_set,
	.private_value = (SC1810C_STATE_MUTE_SW | SC1810C_CTL_MUTE_SW << 8)
};

static const struct snd_kcontrol_new snd_s1810c_48v_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "48V Phantom Power On Mic Inputs Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_s1810c_switch_get,
	.put = snd_s1810c_switch_set,
	.private_value = (SC1810C_STATE_48V_SW | SC1810C_CTL_48V_SW << 8)
};

static int
snd_s1810c_ab_sw_info(struct snd_kcontrol *kctl,
		      struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[2] = {
		"1/2",
		"3/4"
	};

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static const struct snd_kcontrol_new snd_s1810c_ab_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Headphone 1 Source Route",
	.info = snd_s1810c_ab_sw_info,
	.get = snd_s1810c_switch_get,
	.put = snd_s1810c_switch_set,
	.private_value = (SC1810C_STATE_AB_SW | SC1810C_CTL_AB_SW << 8)
};

static void snd_sc1810_mixer_state_free(struct usb_mixer_interface *mixer)
{
	struct s1810_mixer_state *private = mixer->private_data;
	kfree(private);
	mixer->private_data = NULL;
}

/* Entry point, called from mixer_quirks.c */
int snd_sc1810_init_mixer(struct usb_mixer_interface *mixer)
{
	struct s1810_mixer_state *private = NULL;
	struct snd_usb_audio *chip = mixer->chip;
	struct usb_device *dev = chip->dev;
	int ret = 0;

	/* Run this only once */
	if (!list_empty(&chip->mixer_list))
		return 0;

	dev_info(&dev->dev,
		 "Presonus Studio 1810c, device_setup: %u\n", chip->setup);
	if (chip->setup == 1)
		dev_info(&dev->dev, "(8out/18in @ 48kHz)\n");
	else if (chip->setup == 2)
		dev_info(&dev->dev, "(6out/8in @ 192kHz)\n");
	else
		dev_info(&dev->dev, "(8out/14in @ 96kHz)\n");

	ret = snd_s1810c_init_mixer_maps(chip);
	if (ret < 0)
		return ret;

	private = kzalloc(sizeof(struct s1810_mixer_state), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	mutex_init(&private->usb_mutex);
	mutex_init(&private->data_mutex);

	mixer->private_data = private;
	mixer->private_free = snd_sc1810_mixer_state_free;

	private->seqnum = 1;

	ret = snd_s1810c_switch_init(mixer, &snd_s1810c_line_sw);
	if (ret < 0)
		return ret;

	ret = snd_s1810c_switch_init(mixer, &snd_s1810c_mute_sw);
	if (ret < 0)
		return ret;

	ret = snd_s1810c_switch_init(mixer, &snd_s1810c_48v_sw);
	if (ret < 0)
		return ret;

	ret = snd_s1810c_switch_init(mixer, &snd_s1810c_ab_sw);
	if (ret < 0)
		return ret;
	return ret;
}
