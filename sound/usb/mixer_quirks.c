/*
 *   USB Audio Driver for ALSA
 *
 *   Quirks and vendor-specific extensions for mixer interfaces
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   Many codes borrowed from audio.c by
 *	    Alan Cox (alan@lxorguk.ukuu.org.uk)
 *	    Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *   Audio Advantage Micro II support added by:
 *	    Przemek Rudy (prudy1@o2.pl)
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
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "usbaudio.h"
#include "mixer.h"
#include "mixer_quirks.h"
#include "mixer_scarlett.h"
#include "mixer_us16x08.h"
#include "helper.h"

extern struct snd_kcontrol_new *snd_usb_feature_unit_ctl;

struct std_mono_table {
	unsigned int unitid, control, cmask;
	int val_type;
	const char *name;
	snd_kcontrol_tlv_rw_t *tlv_callback;
};

/* This function allows for the creation of standard UAC controls.
 * See the quirks for M-Audio FTUs or Ebox-44.
 * If you don't want to set a TLV callback pass NULL.
 *
 * Since there doesn't seem to be a devices that needs a multichannel
 * version, we keep it mono for simplicity.
 */
static int snd_create_std_mono_ctl_offset(struct usb_mixer_interface *mixer,
				unsigned int unitid,
				unsigned int control,
				unsigned int cmask,
				int val_type,
				unsigned int idx_off,
				const char *name,
				snd_kcontrol_tlv_rw_t *tlv_callback)
{
	struct usb_mixer_elem_info *cval;
	struct snd_kcontrol *kctl;

	cval = kzalloc(sizeof(*cval), GFP_KERNEL);
	if (!cval)
		return -ENOMEM;

	snd_usb_mixer_elem_init_std(&cval->head, mixer, unitid);
	cval->val_type = val_type;
	cval->channels = 1;
	cval->control = control;
	cval->cmask = cmask;
	cval->idx_off = idx_off;

	/* get_min_max() is called only for integer volumes later,
	 * so provide a short-cut for booleans */
	cval->min = 0;
	cval->max = 1;
	cval->res = 0;
	cval->dBmin = 0;
	cval->dBmax = 0;

	/* Create control */
	kctl = snd_ctl_new1(snd_usb_feature_unit_ctl, cval);
	if (!kctl) {
		kfree(cval);
		return -ENOMEM;
	}

	/* Set name */
	snprintf(kctl->id.name, sizeof(kctl->id.name), name);
	kctl->private_free = snd_usb_mixer_elem_free;

	/* set TLV */
	if (tlv_callback) {
		kctl->tlv.c = tlv_callback;
		kctl->vd[0].access |=
			SNDRV_CTL_ELEM_ACCESS_TLV_READ |
			SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
	}
	/* Add control to mixer */
	return snd_usb_mixer_add_control(&cval->head, kctl);
}

static int snd_create_std_mono_ctl(struct usb_mixer_interface *mixer,
				unsigned int unitid,
				unsigned int control,
				unsigned int cmask,
				int val_type,
				const char *name,
				snd_kcontrol_tlv_rw_t *tlv_callback)
{
	return snd_create_std_mono_ctl_offset(mixer, unitid, control, cmask,
		val_type, 0 /* Offset */, name, tlv_callback);
}

/*
 * Create a set of standard UAC controls from a table
 */
static int snd_create_std_mono_table(struct usb_mixer_interface *mixer,
				struct std_mono_table *t)
{
	int err;

	while (t->name != NULL) {
		err = snd_create_std_mono_ctl(mixer, t->unitid, t->control,
				t->cmask, t->val_type, t->name, t->tlv_callback);
		if (err < 0)
			return err;
		t++;
	}

	return 0;
}

static int add_single_ctl_with_resume(struct usb_mixer_interface *mixer,
				      int id,
				      usb_mixer_elem_resume_func_t resume,
				      const struct snd_kcontrol_new *knew,
				      struct usb_mixer_elem_list **listp)
{
	struct usb_mixer_elem_list *list;
	struct snd_kcontrol *kctl;

	list = kzalloc(sizeof(*list), GFP_KERNEL);
	if (!list)
		return -ENOMEM;
	if (listp)
		*listp = list;
	list->mixer = mixer;
	list->id = id;
	list->resume = resume;
	kctl = snd_ctl_new1(knew, list);
	if (!kctl) {
		kfree(list);
		return -ENOMEM;
	}
	kctl->private_free = snd_usb_mixer_elem_free;
	return snd_usb_mixer_add_control(list, kctl);
}

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
	{ USB_ID(0x041e, 0x3042), 0, 1, 1, 1,  1,  0x000d }, /* Usb X-Fi S51 */
	{ USB_ID(0x041e, 0x30df), 0, 1, 1, 1,  1,  0x000d }, /* Usb X-Fi S51 Pro */
	{ USB_ID(0x041e, 0x3237), 0, 1, 1, 1,  1,  0x000d }, /* Usb X-Fi S51 Pro */
	{ USB_ID(0x041e, 0x3048), 2, 2, 6, 6,  2,  0x6e91 }, /* Toshiba SB0500 */
};

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
	mixer->rc_setup_packet->bRequest = UAC_GET_MEM;
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
	ucontrol->value.integer.value[0] = kcontrol->private_value >> 8;
	return 0;
}

static int snd_audigy2nx_led_update(struct usb_mixer_interface *mixer,
				    int value, int index)
{
	struct snd_usb_audio *chip = mixer->chip;
	int err;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;

	if (chip->usb_id == USB_ID(0x041e, 0x3042))
		err = snd_usb_ctl_msg(chip->dev,
			      usb_sndctrlpipe(chip->dev, 0), 0x24,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      !value, 0, NULL, 0);
	/* USB X-Fi S51 Pro */
	if (chip->usb_id == USB_ID(0x041e, 0x30df))
		err = snd_usb_ctl_msg(chip->dev,
			      usb_sndctrlpipe(chip->dev, 0), 0x24,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      !value, 0, NULL, 0);
	else
		err = snd_usb_ctl_msg(chip->dev,
			      usb_sndctrlpipe(chip->dev, 0), 0x24,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      value, index + 2, NULL, 0);
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_audigy2nx_led_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	struct usb_mixer_interface *mixer = list->mixer;
	int index = kcontrol->private_value & 0xff;
	unsigned int value = ucontrol->value.integer.value[0];
	int old_value = kcontrol->private_value >> 8;
	int err;

	if (value > 1)
		return -EINVAL;
	if (value == old_value)
		return 0;
	kcontrol->private_value = (value << 8) | index;
	err = snd_audigy2nx_led_update(mixer, value, index);
	return err < 0 ? err : 1;
}

static int snd_audigy2nx_led_resume(struct usb_mixer_elem_list *list)
{
	int priv_value = list->kctl->private_value;

	return snd_audigy2nx_led_update(list->mixer, priv_value >> 8,
					priv_value & 0xff);
}

/* name and private_value are set dynamically */
static const struct snd_kcontrol_new snd_audigy2nx_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = snd_audigy2nx_led_info,
	.get = snd_audigy2nx_led_get,
	.put = snd_audigy2nx_led_put,
};

static const char * const snd_audigy2nx_led_names[] = {
	"CMSS LED Switch",
	"Power LED Switch",
	"Dolby Digital LED Switch",
};

static int snd_audigy2nx_controls_create(struct usb_mixer_interface *mixer)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(snd_audigy2nx_led_names); ++i) {
		struct snd_kcontrol_new knew;

		/* USB X-Fi S51 doesn't have a CMSS LED */
		if ((mixer->chip->usb_id == USB_ID(0x041e, 0x3042)) && i == 0)
			continue;
		/* USB X-Fi S51 Pro doesn't have one either */
		if ((mixer->chip->usb_id == USB_ID(0x041e, 0x30df)) && i == 0)
			continue;
		if (i > 1 && /* Live24ext has 2 LEDs only */
			(mixer->chip->usb_id == USB_ID(0x041e, 0x3040) ||
			 mixer->chip->usb_id == USB_ID(0x041e, 0x3042) ||
			 mixer->chip->usb_id == USB_ID(0x041e, 0x30df) ||
			 mixer->chip->usb_id == USB_ID(0x041e, 0x3048)))
			break; 

		knew = snd_audigy2nx_control;
		knew.name = snd_audigy2nx_led_names[i];
		knew.private_value = (1 << 8) | i; /* LED on as default */
		err = add_single_ctl_with_resume(mixer, 0,
						 snd_audigy2nx_led_resume,
						 &knew, NULL);
		if (err < 0)
			return err;
	}
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
		err = snd_usb_lock_shutdown(mixer->chip);
		if (err < 0)
			return;
		err = snd_usb_ctl_msg(mixer->chip->dev,
				      usb_rcvctrlpipe(mixer->chip->dev, 0),
				      UAC_GET_MEM, USB_DIR_IN | USB_TYPE_CLASS |
				      USB_RECIP_INTERFACE, 0,
				      jacks[i].unitid << 8, buf, 3);
		snd_usb_unlock_shutdown(mixer->chip);
		if (err == 3 && (buf[0] == 3 || buf[0] == 6))
			snd_iprintf(buffer, "%02x %02x\n", buf[1], buf[2]);
		else
			snd_iprintf(buffer, "?\n");
	}
}

/* EMU0204 */
static int snd_emu0204_ch_switch_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = {"1/2", "3/4"};

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static int snd_emu0204_ch_switch_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = kcontrol->private_value;
	return 0;
}

static int snd_emu0204_ch_switch_update(struct usb_mixer_interface *mixer,
					int value)
{
	struct snd_usb_audio *chip = mixer->chip;
	int err;
	unsigned char buf[2];

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;

	buf[0] = 0x01;
	buf[1] = value ? 0x02 : 0x01;
	err = snd_usb_ctl_msg(chip->dev,
		      usb_sndctrlpipe(chip->dev, 0), UAC_SET_CUR,
		      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
		      0x0400, 0x0e00, buf, 2);
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_emu0204_ch_switch_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	struct usb_mixer_interface *mixer = list->mixer;
	unsigned int value = ucontrol->value.enumerated.item[0];
	int err;

	if (value > 1)
		return -EINVAL;

	if (value == kcontrol->private_value)
		return 0;

	kcontrol->private_value = value;
	err = snd_emu0204_ch_switch_update(mixer, value);
	return err < 0 ? err : 1;
}

static int snd_emu0204_ch_switch_resume(struct usb_mixer_elem_list *list)
{
	return snd_emu0204_ch_switch_update(list->mixer,
					    list->kctl->private_value);
}

static struct snd_kcontrol_new snd_emu0204_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Front Jack Channels",
	.info = snd_emu0204_ch_switch_info,
	.get = snd_emu0204_ch_switch_get,
	.put = snd_emu0204_ch_switch_put,
	.private_value = 0,
};

static int snd_emu0204_controls_create(struct usb_mixer_interface *mixer)
{
	return add_single_ctl_with_resume(mixer, 0,
					  snd_emu0204_ch_switch_resume,
					  &snd_emu0204_control, NULL);
}

/* ASUS Xonar U1 / U3 controls */

static int snd_xonar_u1_switch_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = !!(kcontrol->private_value & 0x02);
	return 0;
}

static int snd_xonar_u1_switch_update(struct usb_mixer_interface *mixer,
				      unsigned char status)
{
	struct snd_usb_audio *chip = mixer->chip;
	int err;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;
	err = snd_usb_ctl_msg(chip->dev,
			      usb_sndctrlpipe(chip->dev, 0), 0x08,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			      50, 0, &status, 1);
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_xonar_u1_switch_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	u8 old_status, new_status;
	int err;

	old_status = kcontrol->private_value;
	if (ucontrol->value.integer.value[0])
		new_status = old_status | 0x02;
	else
		new_status = old_status & ~0x02;
	if (new_status == old_status)
		return 0;

	kcontrol->private_value = new_status;
	err = snd_xonar_u1_switch_update(list->mixer, new_status);
	return err < 0 ? err : 1;
}

static int snd_xonar_u1_switch_resume(struct usb_mixer_elem_list *list)
{
	return snd_xonar_u1_switch_update(list->mixer,
					  list->kctl->private_value);
}

static struct snd_kcontrol_new snd_xonar_u1_output_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Playback Switch",
	.info = snd_ctl_boolean_mono_info,
	.get = snd_xonar_u1_switch_get,
	.put = snd_xonar_u1_switch_put,
	.private_value = 0x05,
};

static int snd_xonar_u1_controls_create(struct usb_mixer_interface *mixer)
{
	return add_single_ctl_with_resume(mixer, 0,
					  snd_xonar_u1_switch_resume,
					  &snd_xonar_u1_output_switch, NULL);
}

/* Digidesign Mbox 1 clock source switch (internal/spdif) */

static int snd_mbox1_switch_get(struct snd_kcontrol *kctl,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = kctl->private_value;
	return 0;
}

static int snd_mbox1_switch_update(struct usb_mixer_interface *mixer, int val)
{
	struct snd_usb_audio *chip = mixer->chip;
	int err;
	unsigned char buff[3];

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;

	/* Prepare for magic command to toggle clock source */
	err = snd_usb_ctl_msg(chip->dev,
				usb_rcvctrlpipe(chip->dev, 0), 0x81,
				USB_DIR_IN |
				USB_TYPE_CLASS |
				USB_RECIP_INTERFACE, 0x00, 0x500, buff, 1);
	if (err < 0)
		goto err;
	err = snd_usb_ctl_msg(chip->dev,
				usb_rcvctrlpipe(chip->dev, 0), 0x81,
				USB_DIR_IN |
				USB_TYPE_CLASS |
				USB_RECIP_ENDPOINT, 0x100, 0x81, buff, 3);
	if (err < 0)
		goto err;

	/* 2 possibilities:	Internal    -> send sample rate
	 *			S/PDIF sync -> send zeroes
	 * NB: Sample rate locked to 48kHz on purpose to
	 *     prevent user from resetting the sample rate
	 *     while S/PDIF sync is enabled and confusing
	 *     this configuration.
	 */
	if (val == 0) {
		buff[0] = 0x80;
		buff[1] = 0xbb;
		buff[2] = 0x00;
	} else {
		buff[0] = buff[1] = buff[2] = 0x00;
	}

	/* Send the magic command to toggle the clock source */
	err = snd_usb_ctl_msg(chip->dev,
				usb_sndctrlpipe(chip->dev, 0), 0x1,
				USB_TYPE_CLASS |
				USB_RECIP_ENDPOINT, 0x100, 0x81, buff, 3);
	if (err < 0)
		goto err;
	err = snd_usb_ctl_msg(chip->dev,
				usb_rcvctrlpipe(chip->dev, 0), 0x81,
				USB_DIR_IN |
				USB_TYPE_CLASS |
				USB_RECIP_ENDPOINT, 0x100, 0x81, buff, 3);
	if (err < 0)
		goto err;
	err = snd_usb_ctl_msg(chip->dev,
				usb_rcvctrlpipe(chip->dev, 0), 0x81,
				USB_DIR_IN |
				USB_TYPE_CLASS |
				USB_RECIP_ENDPOINT, 0x100, 0x2, buff, 3);
	if (err < 0)
		goto err;

err:
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_mbox1_switch_put(struct snd_kcontrol *kctl,
				struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kctl);
	struct usb_mixer_interface *mixer = list->mixer;
	int err;
	bool cur_val, new_val;

	cur_val = kctl->private_value;
	new_val = ucontrol->value.enumerated.item[0];
	if (cur_val == new_val)
		return 0;

	kctl->private_value = new_val;
	err = snd_mbox1_switch_update(mixer, new_val);
	return err < 0 ? err : 1;
}

static int snd_mbox1_switch_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[2] = {
		"Internal",
		"S/PDIF"
	};

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static int snd_mbox1_switch_resume(struct usb_mixer_elem_list *list)
{
	return snd_mbox1_switch_update(list->mixer, list->kctl->private_value);
}

static struct snd_kcontrol_new snd_mbox1_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Clock Source",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_mbox1_switch_info,
	.get = snd_mbox1_switch_get,
	.put = snd_mbox1_switch_put,
	.private_value = 0
};

static int snd_mbox1_create_sync_switch(struct usb_mixer_interface *mixer)
{
	return add_single_ctl_with_resume(mixer, 0,
					  snd_mbox1_switch_resume,
					  &snd_mbox1_switch, NULL);
}

/* Native Instruments device quirks */

#define _MAKE_NI_CONTROL(bRequest,wIndex) ((bRequest) << 16 | (wIndex))

static int snd_ni_control_init_val(struct usb_mixer_interface *mixer,
				   struct snd_kcontrol *kctl)
{
	struct usb_device *dev = mixer->chip->dev;
	unsigned int pval = kctl->private_value;
	u8 value;
	int err;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0),
			      (pval >> 16) & 0xff,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			      0, pval & 0xffff, &value, 1);
	if (err < 0) {
		dev_err(&dev->dev,
			"unable to issue vendor read request (ret = %d)", err);
		return err;
	}

	kctl->private_value |= (value << 24);
	return 0;
}

static int snd_nativeinstruments_control_get(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = kcontrol->private_value >> 24;
	return 0;
}

static int snd_ni_update_cur_val(struct usb_mixer_elem_list *list)
{
	struct snd_usb_audio *chip = list->mixer->chip;
	unsigned int pval = list->kctl->private_value;
	int err;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;
	err = usb_control_msg(chip->dev, usb_sndctrlpipe(chip->dev, 0),
			      (pval >> 16) & 0xff,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      pval >> 24, pval & 0xffff, NULL, 0, 1000);
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_nativeinstruments_control_put(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	u8 oldval = (kcontrol->private_value >> 24) & 0xff;
	u8 newval = ucontrol->value.integer.value[0];
	int err;

	if (oldval == newval)
		return 0;

	kcontrol->private_value &= ~(0xff << 24);
	kcontrol->private_value |= (unsigned int)newval << 24;
	err = snd_ni_update_cur_val(list);
	return err < 0 ? err : 1;
}

static struct snd_kcontrol_new snd_nativeinstruments_ta6_mixers[] = {
	{
		.name = "Direct Thru Channel A",
		.private_value = _MAKE_NI_CONTROL(0x01, 0x03),
	},
	{
		.name = "Direct Thru Channel B",
		.private_value = _MAKE_NI_CONTROL(0x01, 0x05),
	},
	{
		.name = "Phono Input Channel A",
		.private_value = _MAKE_NI_CONTROL(0x02, 0x03),
	},
	{
		.name = "Phono Input Channel B",
		.private_value = _MAKE_NI_CONTROL(0x02, 0x05),
	},
};

static struct snd_kcontrol_new snd_nativeinstruments_ta10_mixers[] = {
	{
		.name = "Direct Thru Channel A",
		.private_value = _MAKE_NI_CONTROL(0x01, 0x03),
	},
	{
		.name = "Direct Thru Channel B",
		.private_value = _MAKE_NI_CONTROL(0x01, 0x05),
	},
	{
		.name = "Direct Thru Channel C",
		.private_value = _MAKE_NI_CONTROL(0x01, 0x07),
	},
	{
		.name = "Direct Thru Channel D",
		.private_value = _MAKE_NI_CONTROL(0x01, 0x09),
	},
	{
		.name = "Phono Input Channel A",
		.private_value = _MAKE_NI_CONTROL(0x02, 0x03),
	},
	{
		.name = "Phono Input Channel B",
		.private_value = _MAKE_NI_CONTROL(0x02, 0x05),
	},
	{
		.name = "Phono Input Channel C",
		.private_value = _MAKE_NI_CONTROL(0x02, 0x07),
	},
	{
		.name = "Phono Input Channel D",
		.private_value = _MAKE_NI_CONTROL(0x02, 0x09),
	},
};

static int snd_nativeinstruments_create_mixer(struct usb_mixer_interface *mixer,
					      const struct snd_kcontrol_new *kc,
					      unsigned int count)
{
	int i, err = 0;
	struct snd_kcontrol_new template = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.get = snd_nativeinstruments_control_get,
		.put = snd_nativeinstruments_control_put,
		.info = snd_ctl_boolean_mono_info,
	};

	for (i = 0; i < count; i++) {
		struct usb_mixer_elem_list *list;

		template.name = kc[i].name;
		template.private_value = kc[i].private_value;

		err = add_single_ctl_with_resume(mixer, 0,
						 snd_ni_update_cur_val,
						 &template, &list);
		if (err < 0)
			break;
		snd_ni_control_init_val(mixer, list->kctl);
	}

	return err;
}

/* M-Audio FastTrack Ultra quirks */
/* FTU Effect switch (also used by C400/C600) */
static int snd_ftu_eff_switch_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	static const char *const texts[8] = {
		"Room 1", "Room 2", "Room 3", "Hall 1",
		"Hall 2", "Plate", "Delay", "Echo"
	};

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static int snd_ftu_eff_switch_init(struct usb_mixer_interface *mixer,
				   struct snd_kcontrol *kctl)
{
	struct usb_device *dev = mixer->chip->dev;
	unsigned int pval = kctl->private_value;
	int err;
	unsigned char value[2];

	value[0] = 0x00;
	value[1] = 0x00;

	err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC_GET_CUR,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
			      pval & 0xff00,
			      snd_usb_ctrl_intf(mixer->chip) | ((pval & 0xff) << 8),
			      value, 2);
	if (err < 0)
		return err;

	kctl->private_value |= value[0] << 24;
	return 0;
}

static int snd_ftu_eff_switch_get(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.enumerated.item[0] = kctl->private_value >> 24;
	return 0;
}

static int snd_ftu_eff_switch_update(struct usb_mixer_elem_list *list)
{
	struct snd_usb_audio *chip = list->mixer->chip;
	unsigned int pval = list->kctl->private_value;
	unsigned char value[2];
	int err;

	value[0] = pval >> 24;
	value[1] = 0;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;
	err = snd_usb_ctl_msg(chip->dev,
			      usb_sndctrlpipe(chip->dev, 0),
			      UAC_SET_CUR,
			      USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
			      pval & 0xff00,
			      snd_usb_ctrl_intf(chip) | ((pval & 0xff) << 8),
			      value, 2);
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_ftu_eff_switch_put(struct snd_kcontrol *kctl,
					struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kctl);
	unsigned int pval = list->kctl->private_value;
	int cur_val, err, new_val;

	cur_val = pval >> 24;
	new_val = ucontrol->value.enumerated.item[0];
	if (cur_val == new_val)
		return 0;

	kctl->private_value &= ~(0xff << 24);
	kctl->private_value |= new_val << 24;
	err = snd_ftu_eff_switch_update(list);
	return err < 0 ? err : 1;
}

static int snd_ftu_create_effect_switch(struct usb_mixer_interface *mixer,
	int validx, int bUnitID)
{
	static struct snd_kcontrol_new template = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Effect Program Switch",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ftu_eff_switch_info,
		.get = snd_ftu_eff_switch_get,
		.put = snd_ftu_eff_switch_put
	};
	struct usb_mixer_elem_list *list;
	int err;

	err = add_single_ctl_with_resume(mixer, bUnitID,
					 snd_ftu_eff_switch_update,
					 &template, &list);
	if (err < 0)
		return err;
	list->kctl->private_value = (validx << 8) | bUnitID;
	snd_ftu_eff_switch_init(mixer, list->kctl);
	return 0;
}

/* Create volume controls for FTU devices*/
static int snd_ftu_create_volume_ctls(struct usb_mixer_interface *mixer)
{
	char name[64];
	unsigned int control, cmask;
	int in, out, err;

	const unsigned int id = 5;
	const int val_type = USB_MIXER_S16;

	for (out = 0; out < 8; out++) {
		control = out + 1;
		for (in = 0; in < 8; in++) {
			cmask = 1 << in;
			snprintf(name, sizeof(name),
				"AIn%d - Out%d Capture Volume",
				in  + 1, out + 1);
			err = snd_create_std_mono_ctl(mixer, id, control,
							cmask, val_type, name,
							&snd_usb_mixer_vol_tlv);
			if (err < 0)
				return err;
		}
		for (in = 8; in < 16; in++) {
			cmask = 1 << in;
			snprintf(name, sizeof(name),
				"DIn%d - Out%d Playback Volume",
				in - 7, out + 1);
			err = snd_create_std_mono_ctl(mixer, id, control,
							cmask, val_type, name,
							&snd_usb_mixer_vol_tlv);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

/* This control needs a volume quirk, see mixer.c */
static int snd_ftu_create_effect_volume_ctl(struct usb_mixer_interface *mixer)
{
	static const char name[] = "Effect Volume";
	const unsigned int id = 6;
	const int val_type = USB_MIXER_U8;
	const unsigned int control = 2;
	const unsigned int cmask = 0;

	return snd_create_std_mono_ctl(mixer, id, control, cmask, val_type,
					name, snd_usb_mixer_vol_tlv);
}

/* This control needs a volume quirk, see mixer.c */
static int snd_ftu_create_effect_duration_ctl(struct usb_mixer_interface *mixer)
{
	static const char name[] = "Effect Duration";
	const unsigned int id = 6;
	const int val_type = USB_MIXER_S16;
	const unsigned int control = 3;
	const unsigned int cmask = 0;

	return snd_create_std_mono_ctl(mixer, id, control, cmask, val_type,
					name, snd_usb_mixer_vol_tlv);
}

/* This control needs a volume quirk, see mixer.c */
static int snd_ftu_create_effect_feedback_ctl(struct usb_mixer_interface *mixer)
{
	static const char name[] = "Effect Feedback Volume";
	const unsigned int id = 6;
	const int val_type = USB_MIXER_U8;
	const unsigned int control = 4;
	const unsigned int cmask = 0;

	return snd_create_std_mono_ctl(mixer, id, control, cmask, val_type,
					name, NULL);
}

static int snd_ftu_create_effect_return_ctls(struct usb_mixer_interface *mixer)
{
	unsigned int cmask;
	int err, ch;
	char name[48];

	const unsigned int id = 7;
	const int val_type = USB_MIXER_S16;
	const unsigned int control = 7;

	for (ch = 0; ch < 4; ++ch) {
		cmask = 1 << ch;
		snprintf(name, sizeof(name),
			"Effect Return %d Volume", ch + 1);
		err = snd_create_std_mono_ctl(mixer, id, control,
						cmask, val_type, name,
						snd_usb_mixer_vol_tlv);
		if (err < 0)
			return err;
	}

	return 0;
}

static int snd_ftu_create_effect_send_ctls(struct usb_mixer_interface *mixer)
{
	unsigned int  cmask;
	int err, ch;
	char name[48];

	const unsigned int id = 5;
	const int val_type = USB_MIXER_S16;
	const unsigned int control = 9;

	for (ch = 0; ch < 8; ++ch) {
		cmask = 1 << ch;
		snprintf(name, sizeof(name),
			"Effect Send AIn%d Volume", ch + 1);
		err = snd_create_std_mono_ctl(mixer, id, control, cmask,
						val_type, name,
						snd_usb_mixer_vol_tlv);
		if (err < 0)
			return err;
	}
	for (ch = 8; ch < 16; ++ch) {
		cmask = 1 << ch;
		snprintf(name, sizeof(name),
			"Effect Send DIn%d Volume", ch - 7);
		err = snd_create_std_mono_ctl(mixer, id, control, cmask,
						val_type, name,
						snd_usb_mixer_vol_tlv);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_ftu_create_mixer(struct usb_mixer_interface *mixer)
{
	int err;

	err = snd_ftu_create_volume_ctls(mixer);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_switch(mixer, 1, 6);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_volume_ctl(mixer);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_duration_ctl(mixer);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_feedback_ctl(mixer);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_return_ctls(mixer);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_send_ctls(mixer);
	if (err < 0)
		return err;

	return 0;
}

void snd_emuusb_set_samplerate(struct snd_usb_audio *chip,
			       unsigned char samplerate_id)
{
	struct usb_mixer_interface *mixer;
	struct usb_mixer_elem_info *cval;
	int unitid = 12; /* SamleRate ExtensionUnit ID */

	list_for_each_entry(mixer, &chip->mixer_list, list) {
		cval = (struct usb_mixer_elem_info *)mixer->id_elems[unitid];
		if (cval) {
			snd_usb_mixer_set_ctl_value(cval, UAC_SET_CUR,
						    cval->control << 8,
						    samplerate_id);
			snd_usb_mixer_notify_id(mixer, unitid);
		}
		break;
	}
}

/* M-Audio Fast Track C400/C600 */
/* C400/C600 volume controls, this control needs a volume quirk, see mixer.c */
static int snd_c400_create_vol_ctls(struct usb_mixer_interface *mixer)
{
	char name[64];
	unsigned int cmask, offset;
	int out, chan, err;
	int num_outs = 0;
	int num_ins = 0;

	const unsigned int id = 0x40;
	const int val_type = USB_MIXER_S16;
	const int control = 1;

	switch (mixer->chip->usb_id) {
	case USB_ID(0x0763, 0x2030):
		num_outs = 6;
		num_ins = 4;
		break;
	case USB_ID(0x0763, 0x2031):
		num_outs = 8;
		num_ins = 6;
		break;
	}

	for (chan = 0; chan < num_outs + num_ins; chan++) {
		for (out = 0; out < num_outs; out++) {
			if (chan < num_outs) {
				snprintf(name, sizeof(name),
					"PCM%d-Out%d Playback Volume",
					chan + 1, out + 1);
			} else {
				snprintf(name, sizeof(name),
					"In%d-Out%d Playback Volume",
					chan - num_outs + 1, out + 1);
			}

			cmask = (out == 0) ? 0 : 1 << (out - 1);
			offset = chan * num_outs;
			err = snd_create_std_mono_ctl_offset(mixer, id, control,
						cmask, val_type, offset, name,
						&snd_usb_mixer_vol_tlv);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

/* This control needs a volume quirk, see mixer.c */
static int snd_c400_create_effect_volume_ctl(struct usb_mixer_interface *mixer)
{
	static const char name[] = "Effect Volume";
	const unsigned int id = 0x43;
	const int val_type = USB_MIXER_U8;
	const unsigned int control = 3;
	const unsigned int cmask = 0;

	return snd_create_std_mono_ctl(mixer, id, control, cmask, val_type,
					name, snd_usb_mixer_vol_tlv);
}

/* This control needs a volume quirk, see mixer.c */
static int snd_c400_create_effect_duration_ctl(struct usb_mixer_interface *mixer)
{
	static const char name[] = "Effect Duration";
	const unsigned int id = 0x43;
	const int val_type = USB_MIXER_S16;
	const unsigned int control = 4;
	const unsigned int cmask = 0;

	return snd_create_std_mono_ctl(mixer, id, control, cmask, val_type,
					name, snd_usb_mixer_vol_tlv);
}

/* This control needs a volume quirk, see mixer.c */
static int snd_c400_create_effect_feedback_ctl(struct usb_mixer_interface *mixer)
{
	static const char name[] = "Effect Feedback Volume";
	const unsigned int id = 0x43;
	const int val_type = USB_MIXER_U8;
	const unsigned int control = 5;
	const unsigned int cmask = 0;

	return snd_create_std_mono_ctl(mixer, id, control, cmask, val_type,
					name, NULL);
}

static int snd_c400_create_effect_vol_ctls(struct usb_mixer_interface *mixer)
{
	char name[64];
	unsigned int cmask;
	int chan, err;
	int num_outs = 0;
	int num_ins = 0;

	const unsigned int id = 0x42;
	const int val_type = USB_MIXER_S16;
	const int control = 1;

	switch (mixer->chip->usb_id) {
	case USB_ID(0x0763, 0x2030):
		num_outs = 6;
		num_ins = 4;
		break;
	case USB_ID(0x0763, 0x2031):
		num_outs = 8;
		num_ins = 6;
		break;
	}

	for (chan = 0; chan < num_outs + num_ins; chan++) {
		if (chan < num_outs) {
			snprintf(name, sizeof(name),
				"Effect Send DOut%d",
				chan + 1);
		} else {
			snprintf(name, sizeof(name),
				"Effect Send AIn%d",
				chan - num_outs + 1);
		}

		cmask = (chan == 0) ? 0 : 1 << (chan - 1);
		err = snd_create_std_mono_ctl(mixer, id, control,
						cmask, val_type, name,
						&snd_usb_mixer_vol_tlv);
		if (err < 0)
			return err;
	}

	return 0;
}

static int snd_c400_create_effect_ret_vol_ctls(struct usb_mixer_interface *mixer)
{
	char name[64];
	unsigned int cmask;
	int chan, err;
	int num_outs = 0;
	int offset = 0;

	const unsigned int id = 0x40;
	const int val_type = USB_MIXER_S16;
	const int control = 1;

	switch (mixer->chip->usb_id) {
	case USB_ID(0x0763, 0x2030):
		num_outs = 6;
		offset = 0x3c;
		/* { 0x3c, 0x43, 0x3e, 0x45, 0x40, 0x47 } */
		break;
	case USB_ID(0x0763, 0x2031):
		num_outs = 8;
		offset = 0x70;
		/* { 0x70, 0x79, 0x72, 0x7b, 0x74, 0x7d, 0x76, 0x7f } */
		break;
	}

	for (chan = 0; chan < num_outs; chan++) {
		snprintf(name, sizeof(name),
			"Effect Return %d",
			chan + 1);

		cmask = (chan == 0) ? 0 :
			1 << (chan + (chan % 2) * num_outs - 1);
		err = snd_create_std_mono_ctl_offset(mixer, id, control,
						cmask, val_type, offset, name,
						&snd_usb_mixer_vol_tlv);
		if (err < 0)
			return err;
	}

	return 0;
}

static int snd_c400_create_mixer(struct usb_mixer_interface *mixer)
{
	int err;

	err = snd_c400_create_vol_ctls(mixer);
	if (err < 0)
		return err;

	err = snd_c400_create_effect_vol_ctls(mixer);
	if (err < 0)
		return err;

	err = snd_c400_create_effect_ret_vol_ctls(mixer);
	if (err < 0)
		return err;

	err = snd_ftu_create_effect_switch(mixer, 2, 0x43);
	if (err < 0)
		return err;

	err = snd_c400_create_effect_volume_ctl(mixer);
	if (err < 0)
		return err;

	err = snd_c400_create_effect_duration_ctl(mixer);
	if (err < 0)
		return err;

	err = snd_c400_create_effect_feedback_ctl(mixer);
	if (err < 0)
		return err;

	return 0;
}

/*
 * The mixer units for Ebox-44 are corrupt, and even where they
 * are valid they presents mono controls as L and R channels of
 * stereo. So we provide a good mixer here.
 */
static struct std_mono_table ebox44_table[] = {
	{
		.unitid = 4,
		.control = 1,
		.cmask = 0x0,
		.val_type = USB_MIXER_INV_BOOLEAN,
		.name = "Headphone Playback Switch"
	},
	{
		.unitid = 4,
		.control = 2,
		.cmask = 0x1,
		.val_type = USB_MIXER_S16,
		.name = "Headphone A Mix Playback Volume"
	},
	{
		.unitid = 4,
		.control = 2,
		.cmask = 0x2,
		.val_type = USB_MIXER_S16,
		.name = "Headphone B Mix Playback Volume"
	},

	{
		.unitid = 7,
		.control = 1,
		.cmask = 0x0,
		.val_type = USB_MIXER_INV_BOOLEAN,
		.name = "Output Playback Switch"
	},
	{
		.unitid = 7,
		.control = 2,
		.cmask = 0x1,
		.val_type = USB_MIXER_S16,
		.name = "Output A Playback Volume"
	},
	{
		.unitid = 7,
		.control = 2,
		.cmask = 0x2,
		.val_type = USB_MIXER_S16,
		.name = "Output B Playback Volume"
	},

	{
		.unitid = 10,
		.control = 1,
		.cmask = 0x0,
		.val_type = USB_MIXER_INV_BOOLEAN,
		.name = "Input Capture Switch"
	},
	{
		.unitid = 10,
		.control = 2,
		.cmask = 0x1,
		.val_type = USB_MIXER_S16,
		.name = "Input A Capture Volume"
	},
	{
		.unitid = 10,
		.control = 2,
		.cmask = 0x2,
		.val_type = USB_MIXER_S16,
		.name = "Input B Capture Volume"
	},

	{}
};

/* Audio Advantage Micro II findings:
 *
 * Mapping spdif AES bits to vendor register.bit:
 * AES0: [0 0 0 0 2.3 2.2 2.1 2.0] - default 0x00
 * AES1: [3.3 3.2.3.1.3.0 2.7 2.6 2.5 2.4] - default: 0x01
 * AES2: [0 0 0 0 0 0 0 0]
 * AES3: [0 0 0 0 0 0 x 0] - 'x' bit is set basing on standard usb request
 *                           (UAC_EP_CS_ATTR_SAMPLE_RATE) for Audio Devices
 *
 * power on values:
 * r2: 0x10
 * r3: 0x20 (b7 is zeroed just before playback (except IEC61937) and set
 *           just after it to 0xa0, presumably it disables/mutes some analog
 *           parts when there is no audio.)
 * r9: 0x28
 *
 * Optical transmitter on/off:
 * vendor register.bit: 9.1
 * 0 - on (0x28 register value)
 * 1 - off (0x2a register value)
 *
 */
static int snd_microii_spdif_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_microii_spdif_default_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	struct snd_usb_audio *chip = list->mixer->chip;
	int err;
	struct usb_interface *iface;
	struct usb_host_interface *alts;
	unsigned int ep;
	unsigned char data[3];
	int rate;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;

	ucontrol->value.iec958.status[0] = kcontrol->private_value & 0xff;
	ucontrol->value.iec958.status[1] = (kcontrol->private_value >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = 0x00;

	/* use known values for that card: interface#1 altsetting#1 */
	iface = usb_ifnum_to_if(chip->dev, 1);
	if (!iface || iface->num_altsetting < 2)
		return -EINVAL;
	alts = &iface->altsetting[1];
	if (get_iface_desc(alts)->bNumEndpoints < 1)
		return -EINVAL;
	ep = get_endpoint(alts, 0)->bEndpointAddress;

	err = snd_usb_ctl_msg(chip->dev,
			usb_rcvctrlpipe(chip->dev, 0),
			UAC_GET_CUR,
			USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_IN,
			UAC_EP_CS_ATTR_SAMPLE_RATE << 8,
			ep,
			data,
			sizeof(data));
	if (err < 0)
		goto end;

	rate = data[0] | (data[1] << 8) | (data[2] << 16);
	ucontrol->value.iec958.status[3] = (rate == 48000) ?
			IEC958_AES3_CON_FS_48000 : IEC958_AES3_CON_FS_44100;

	err = 0;
 end:
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_microii_spdif_default_update(struct usb_mixer_elem_list *list)
{
	struct snd_usb_audio *chip = list->mixer->chip;
	unsigned int pval = list->kctl->private_value;
	u8 reg;
	int err;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;

	reg = ((pval >> 4) & 0xf0) | (pval & 0x0f);
	err = snd_usb_ctl_msg(chip->dev,
			usb_sndctrlpipe(chip->dev, 0),
			UAC_SET_CUR,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			reg,
			2,
			NULL,
			0);
	if (err < 0)
		goto end;

	reg = (pval & IEC958_AES0_NONAUDIO) ? 0xa0 : 0x20;
	reg |= (pval >> 12) & 0x0f;
	err = snd_usb_ctl_msg(chip->dev,
			usb_sndctrlpipe(chip->dev, 0),
			UAC_SET_CUR,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			reg,
			3,
			NULL,
			0);
	if (err < 0)
		goto end;

 end:
	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_microii_spdif_default_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	unsigned int pval, pval_old;
	int err;

	pval = pval_old = kcontrol->private_value;
	pval &= 0xfffff0f0;
	pval |= (ucontrol->value.iec958.status[1] & 0x0f) << 8;
	pval |= (ucontrol->value.iec958.status[0] & 0x0f);

	pval &= 0xffff0fff;
	pval |= (ucontrol->value.iec958.status[1] & 0xf0) << 8;

	/* The frequency bits in AES3 cannot be set via register access. */

	/* Silently ignore any bits from the request that cannot be set. */

	if (pval == pval_old)
		return 0;

	kcontrol->private_value = pval;
	err = snd_microii_spdif_default_update(list);
	return err < 0 ? err : 1;
}

static int snd_microii_spdif_mask_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0x0f;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0x00;
	ucontrol->value.iec958.status[3] = 0x00;

	return 0;
}

static int snd_microii_spdif_switch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = !(kcontrol->private_value & 0x02);

	return 0;
}

static int snd_microii_spdif_switch_update(struct usb_mixer_elem_list *list)
{
	struct snd_usb_audio *chip = list->mixer->chip;
	u8 reg = list->kctl->private_value;
	int err;

	err = snd_usb_lock_shutdown(chip);
	if (err < 0)
		return err;

	err = snd_usb_ctl_msg(chip->dev,
			usb_sndctrlpipe(chip->dev, 0),
			UAC_SET_CUR,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_OTHER,
			reg,
			9,
			NULL,
			0);

	snd_usb_unlock_shutdown(chip);
	return err;
}

static int snd_microii_spdif_switch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct usb_mixer_elem_list *list = snd_kcontrol_chip(kcontrol);
	u8 reg;
	int err;

	reg = ucontrol->value.integer.value[0] ? 0x28 : 0x2a;
	if (reg != list->kctl->private_value)
		return 0;

	kcontrol->private_value = reg;
	err = snd_microii_spdif_switch_update(list);
	return err < 0 ? err : 1;
}

static struct snd_kcontrol_new snd_microii_mixer_spdif[] = {
	{
		.iface =    SNDRV_CTL_ELEM_IFACE_PCM,
		.name =     SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info =     snd_microii_spdif_info,
		.get =      snd_microii_spdif_default_get,
		.put =      snd_microii_spdif_default_put,
		.private_value = 0x00000100UL,/* reset value */
	},
	{
		.access =   SNDRV_CTL_ELEM_ACCESS_READ,
		.iface =    SNDRV_CTL_ELEM_IFACE_PCM,
		.name =     SNDRV_CTL_NAME_IEC958("", PLAYBACK, MASK),
		.info =     snd_microii_spdif_info,
		.get =      snd_microii_spdif_mask_get,
	},
	{
		.iface =    SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =     SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		.info =     snd_ctl_boolean_mono_info,
		.get =      snd_microii_spdif_switch_get,
		.put =      snd_microii_spdif_switch_put,
		.private_value = 0x00000028UL,/* reset value */
	}
};

static int snd_microii_controls_create(struct usb_mixer_interface *mixer)
{
	int err, i;
	static usb_mixer_elem_resume_func_t resume_funcs[] = {
		snd_microii_spdif_default_update,
		NULL,
		snd_microii_spdif_switch_update
	};

	for (i = 0; i < ARRAY_SIZE(snd_microii_mixer_spdif); ++i) {
		err = add_single_ctl_with_resume(mixer, 0,
						 resume_funcs[i],
						 &snd_microii_mixer_spdif[i],
						 NULL);
		if (err < 0)
			return err;
	}

	return 0;
}

int snd_usb_mixer_apply_create_quirk(struct usb_mixer_interface *mixer)
{
	int err = 0;
	struct snd_info_entry *entry;

	if ((err = snd_usb_soundblaster_remote_init(mixer)) < 0)
		return err;

	switch (mixer->chip->usb_id) {
	/* Tascam US-16x08 */
	case USB_ID(0x0644, 0x8047):
		err = snd_us16x08_controls_create(mixer);
		break;
	case USB_ID(0x041e, 0x3020):
	case USB_ID(0x041e, 0x3040):
	case USB_ID(0x041e, 0x3042):
	case USB_ID(0x041e, 0x30df):
	case USB_ID(0x041e, 0x3048):
		err = snd_audigy2nx_controls_create(mixer);
		if (err < 0)
			break;
		if (!snd_card_proc_new(mixer->chip->card, "audigy2nx", &entry))
			snd_info_set_text_ops(entry, mixer,
					      snd_audigy2nx_proc_read);
		break;

	/* EMU0204 */
	case USB_ID(0x041e, 0x3f19):
		err = snd_emu0204_controls_create(mixer);
		if (err < 0)
			break;
		break;

	case USB_ID(0x0763, 0x2030): /* M-Audio Fast Track C400 */
	case USB_ID(0x0763, 0x2031): /* M-Audio Fast Track C400 */
		err = snd_c400_create_mixer(mixer);
		break;

	case USB_ID(0x0763, 0x2080): /* M-Audio Fast Track Ultra */
	case USB_ID(0x0763, 0x2081): /* M-Audio Fast Track Ultra 8R */
		err = snd_ftu_create_mixer(mixer);
		break;

	case USB_ID(0x0b05, 0x1739): /* ASUS Xonar U1 */
	case USB_ID(0x0b05, 0x1743): /* ASUS Xonar U1 (2) */
	case USB_ID(0x0b05, 0x17a0): /* ASUS Xonar U3 */
		err = snd_xonar_u1_controls_create(mixer);
		break;

	case USB_ID(0x0d8c, 0x0103): /* Audio Advantage Micro II */
		err = snd_microii_controls_create(mixer);
		break;

	case USB_ID(0x0dba, 0x1000): /* Digidesign Mbox 1 */
		err = snd_mbox1_create_sync_switch(mixer);
		break;

	case USB_ID(0x17cc, 0x1011): /* Traktor Audio 6 */
		err = snd_nativeinstruments_create_mixer(mixer,
				snd_nativeinstruments_ta6_mixers,
				ARRAY_SIZE(snd_nativeinstruments_ta6_mixers));
		break;

	case USB_ID(0x17cc, 0x1021): /* Traktor Audio 10 */
		err = snd_nativeinstruments_create_mixer(mixer,
				snd_nativeinstruments_ta10_mixers,
				ARRAY_SIZE(snd_nativeinstruments_ta10_mixers));
		break;

	case USB_ID(0x200c, 0x1018): /* Electrix Ebox-44 */
		/* detection is disabled in mixer_maps.c */
		err = snd_create_std_mono_table(mixer, ebox44_table);
		break;

	case USB_ID(0x1235, 0x8012): /* Focusrite Scarlett 6i6 */
	case USB_ID(0x1235, 0x8002): /* Focusrite Scarlett 8i6 */
	case USB_ID(0x1235, 0x8004): /* Focusrite Scarlett 18i6 */
	case USB_ID(0x1235, 0x8014): /* Focusrite Scarlett 18i8 */
	case USB_ID(0x1235, 0x800c): /* Focusrite Scarlett 18i20 */
		err = snd_scarlett_controls_create(mixer);
		break;
	}

	return err;
}

void snd_usb_mixer_rc_memory_change(struct usb_mixer_interface *mixer,
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
		usb_audio_dbg(mixer->chip, "memory change in unknown unit %d\n", unitid);
		break;
	}
}

static void snd_dragonfly_quirk_db_scale(struct usb_mixer_interface *mixer,
					 struct usb_mixer_elem_info *cval,
					 struct snd_kcontrol *kctl)
{
	/* Approximation using 10 ranges based on output measurement on hw v1.2.
	 * This seems close to the cubic mapping e.g. alsamixer uses. */
	static const DECLARE_TLV_DB_RANGE(scale,
		 0,  1, TLV_DB_MINMAX_ITEM(-5300, -4970),
		 2,  5, TLV_DB_MINMAX_ITEM(-4710, -4160),
		 6,  7, TLV_DB_MINMAX_ITEM(-3884, -3710),
		 8, 14, TLV_DB_MINMAX_ITEM(-3443, -2560),
		15, 16, TLV_DB_MINMAX_ITEM(-2475, -2324),
		17, 19, TLV_DB_MINMAX_ITEM(-2228, -2031),
		20, 26, TLV_DB_MINMAX_ITEM(-1910, -1393),
		27, 31, TLV_DB_MINMAX_ITEM(-1322, -1032),
		32, 40, TLV_DB_MINMAX_ITEM(-968, -490),
		41, 50, TLV_DB_MINMAX_ITEM(-441, 0),
	);

	if (cval->min == 0 && cval->max == 50) {
		usb_audio_info(mixer->chip, "applying DragonFly dB scale quirk (0-50 variant)\n");
		kctl->tlv.p = scale;
		kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		kctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;

	} else if (cval->min == 0 && cval->max <= 1000) {
		/* Some other clearly broken DragonFly variant.
		 * At least a 0..53 variant (hw v1.0) exists.
		 */
		usb_audio_info(mixer->chip, "ignoring too narrow dB range on a DragonFly device");
		kctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
	}
}

void snd_usb_mixer_fu_apply_quirk(struct usb_mixer_interface *mixer,
				  struct usb_mixer_elem_info *cval, int unitid,
				  struct snd_kcontrol *kctl)
{
	switch (mixer->chip->usb_id) {
	case USB_ID(0x21b4, 0x0081): /* AudioQuest DragonFly */
		if (unitid == 7 && cval->control == UAC_FU_VOLUME)
			snd_dragonfly_quirk_db_scale(mixer, cval, kctl);
		break;
	/* lowest playback value is muted on C-Media devices */
	case USB_ID(0x0d8c, 0x000c):
	case USB_ID(0x0d8c, 0x0014):
		if (strstr(kctl->id.name, "Playback"))
			cval->min_mute = 1;
		break;
	}
}

