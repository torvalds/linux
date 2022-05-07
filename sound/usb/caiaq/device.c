// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * caiaq.c: ALSA driver for caiaq/NativeInstruments devices
 *
 *   Copyright (c) 2007 Daniel Mack <daniel@caiaq.de>
 *                      Karsten Wiese <fzu@wemgehoertderstaat.de>
*/

#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/usb.h>
#include <sound/initval.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "device.h"
#include "audio.h"
#include "midi.h"
#include "control.h"
#include "input.h"

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("caiaq USB audio");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Native Instruments,RigKontrol2},"
			 "{Native Instruments,RigKontrol3},"
			 "{Native Instruments,Kore Controller},"
			 "{Native Instruments,Kore Controller 2},"
			 "{Native Instruments,Audio Kontrol 1},"
			 "{Native Instruments,Audio 2 DJ},"
			 "{Native Instruments,Audio 4 DJ},"
			 "{Native Instruments,Audio 8 DJ},"
			 "{Native Instruments,Traktor Audio 2},"
			 "{Native Instruments,Session I/O},"
			 "{Native Instruments,GuitarRig mobile},"
			 "{Native Instruments,Traktor Kontrol X1},"
			 "{Native Instruments,Traktor Kontrol S4},"
			 "{Native Instruments,Maschine Controller}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char* id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the caiaq sound device");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the caiaq soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable the caiaq soundcard.");

enum {
	SAMPLERATE_44100	= 0,
	SAMPLERATE_48000	= 1,
	SAMPLERATE_96000	= 2,
	SAMPLERATE_192000	= 3,
	SAMPLERATE_88200	= 4,
	SAMPLERATE_INVALID	= 0xff
};

enum {
	DEPTH_NONE	= 0,
	DEPTH_16	= 1,
	DEPTH_24	= 2,
	DEPTH_32	= 3
};

static const struct usb_device_id snd_usb_id_table[] = {
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	USB_VID_NATIVEINSTRUMENTS,
		.idProduct =	USB_PID_RIGKONTROL2
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	USB_VID_NATIVEINSTRUMENTS,
		.idProduct =	USB_PID_RIGKONTROL3
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	USB_VID_NATIVEINSTRUMENTS,
		.idProduct =	USB_PID_KORECONTROLLER
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	USB_VID_NATIVEINSTRUMENTS,
		.idProduct =	USB_PID_KORECONTROLLER2
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_AK1
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_AUDIO8DJ
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_SESSIONIO
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_GUITARRIGMOBILE
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_AUDIO4DJ
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_AUDIO2DJ
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_TRAKTORKONTROLX1
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_TRAKTORKONTROLS4
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_TRAKTORAUDIO2
	},
	{
		.match_flags =  USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =     USB_VID_NATIVEINSTRUMENTS,
		.idProduct =    USB_PID_MASCHINECONTROLLER
	},
	{ /* terminator */ }
};

static void usb_ep1_command_reply_dispatch (struct urb* urb)
{
	int ret;
	struct device *dev = &urb->dev->dev;
	struct snd_usb_caiaqdev *cdev = urb->context;
	unsigned char *buf = urb->transfer_buffer;

	if (urb->status || !cdev) {
		dev_warn(dev, "received EP1 urb->status = %i\n", urb->status);
		return;
	}

	switch(buf[0]) {
	case EP1_CMD_GET_DEVICE_INFO:
	 	memcpy(&cdev->spec, buf+1, sizeof(struct caiaq_device_spec));
		cdev->spec.fw_version = le16_to_cpu(cdev->spec.fw_version);
		dev_dbg(dev, "device spec (firmware %d): audio: %d in, %d out, "
			"MIDI: %d in, %d out, data alignment %d\n",
			cdev->spec.fw_version,
			cdev->spec.num_analog_audio_in,
			cdev->spec.num_analog_audio_out,
			cdev->spec.num_midi_in,
			cdev->spec.num_midi_out,
			cdev->spec.data_alignment);

		cdev->spec_received++;
		wake_up(&cdev->ep1_wait_queue);
		break;
	case EP1_CMD_AUDIO_PARAMS:
		cdev->audio_parm_answer = buf[1];
		wake_up(&cdev->ep1_wait_queue);
		break;
	case EP1_CMD_MIDI_READ:
		snd_usb_caiaq_midi_handle_input(cdev, buf[1], buf + 3, buf[2]);
		break;
	case EP1_CMD_READ_IO:
		if (cdev->chip.usb_id ==
			USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ)) {
			if (urb->actual_length > sizeof(cdev->control_state))
				urb->actual_length = sizeof(cdev->control_state);
			memcpy(cdev->control_state, buf + 1, urb->actual_length);
			wake_up(&cdev->ep1_wait_queue);
			break;
		}
#ifdef CONFIG_SND_USB_CAIAQ_INPUT
		fallthrough;
	case EP1_CMD_READ_ERP:
	case EP1_CMD_READ_ANALOG:
		snd_usb_caiaq_input_dispatch(cdev, buf, urb->actual_length);
#endif
		break;
	}

	cdev->ep1_in_urb.actual_length = 0;
	ret = usb_submit_urb(&cdev->ep1_in_urb, GFP_ATOMIC);
	if (ret < 0)
		dev_err(dev, "unable to submit urb. OOM!?\n");
}

int snd_usb_caiaq_send_command(struct snd_usb_caiaqdev *cdev,
			       unsigned char command,
			       const unsigned char *buffer,
			       int len)
{
	int actual_len;
	struct usb_device *usb_dev = cdev->chip.dev;

	if (!usb_dev)
		return -EIO;

	if (len > EP1_BUFSIZE - 1)
		len = EP1_BUFSIZE - 1;

	if (buffer && len > 0)
		memcpy(cdev->ep1_out_buf+1, buffer, len);

	cdev->ep1_out_buf[0] = command;
	return usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, 1),
			   cdev->ep1_out_buf, len+1, &actual_len, 200);
}

int snd_usb_caiaq_send_command_bank(struct snd_usb_caiaqdev *cdev,
			       unsigned char command,
			       unsigned char bank,
			       const unsigned char *buffer,
			       int len)
{
	int actual_len;
	struct usb_device *usb_dev = cdev->chip.dev;

	if (!usb_dev)
		return -EIO;

	if (len > EP1_BUFSIZE - 2)
		len = EP1_BUFSIZE - 2;

	if (buffer && len > 0)
		memcpy(cdev->ep1_out_buf+2, buffer, len);

	cdev->ep1_out_buf[0] = command;
	cdev->ep1_out_buf[1] = bank;

	return usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, 1),
			   cdev->ep1_out_buf, len+2, &actual_len, 200);
}

int snd_usb_caiaq_set_audio_params (struct snd_usb_caiaqdev *cdev,
		   		    int rate, int depth, int bpp)
{
	int ret;
	char tmp[5];
	struct device *dev = caiaqdev_to_dev(cdev);

	switch (rate) {
	case 44100:	tmp[0] = SAMPLERATE_44100;   break;
	case 48000:	tmp[0] = SAMPLERATE_48000;   break;
	case 88200:	tmp[0] = SAMPLERATE_88200;   break;
	case 96000:	tmp[0] = SAMPLERATE_96000;   break;
	case 192000:	tmp[0] = SAMPLERATE_192000;  break;
	default:	return -EINVAL;
	}

	switch (depth) {
	case 16:	tmp[1] = DEPTH_16;   break;
	case 24:	tmp[1] = DEPTH_24;   break;
	default:	return -EINVAL;
	}

	tmp[2] = bpp & 0xff;
	tmp[3] = bpp >> 8;
	tmp[4] = 1; /* packets per microframe */

	dev_dbg(dev, "setting audio params: %d Hz, %d bits, %d bpp\n",
		rate, depth, bpp);

	cdev->audio_parm_answer = -1;
	ret = snd_usb_caiaq_send_command(cdev, EP1_CMD_AUDIO_PARAMS,
					 tmp, sizeof(tmp));

	if (ret)
		return ret;

	if (!wait_event_timeout(cdev->ep1_wait_queue,
	    cdev->audio_parm_answer >= 0, HZ))
		return -EPIPE;

	if (cdev->audio_parm_answer != 1)
		dev_dbg(dev, "unable to set the device's audio params\n");
	else
		cdev->bpp = bpp;

	return cdev->audio_parm_answer == 1 ? 0 : -EINVAL;
}

int snd_usb_caiaq_set_auto_msg(struct snd_usb_caiaqdev *cdev,
			       int digital, int analog, int erp)
{
	char tmp[3] = { digital, analog, erp };
	return snd_usb_caiaq_send_command(cdev, EP1_CMD_AUTO_MSG,
					  tmp, sizeof(tmp));
}

static void setup_card(struct snd_usb_caiaqdev *cdev)
{
	int ret;
	char val[4];
	struct device *dev = caiaqdev_to_dev(cdev);

	/* device-specific startup specials */
	switch (cdev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		/* RigKontrol2 - display centered dash ('-') */
		val[0] = 0x00;
		val[1] = 0x00;
		val[2] = 0x01;
		snd_usb_caiaq_send_command(cdev, EP1_CMD_WRITE_IO, val, 3);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
		/* RigKontrol2 - display two centered dashes ('--') */
		val[0] = 0x00;
		val[1] = 0x40;
		val[2] = 0x40;
		val[3] = 0x00;
		snd_usb_caiaq_send_command(cdev, EP1_CMD_WRITE_IO, val, 4);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
		/* Audio Kontrol 1 - make USB-LED stop blinking */
		val[0] = 0x00;
		snd_usb_caiaq_send_command(cdev, EP1_CMD_WRITE_IO, val, 1);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ):
		/* Audio 8 DJ - trigger read of current settings */
		cdev->control_state[0] = 0xff;
		snd_usb_caiaq_set_auto_msg(cdev, 1, 0, 0);
		snd_usb_caiaq_send_command(cdev, EP1_CMD_READ_IO, NULL, 0);

		if (!wait_event_timeout(cdev->ep1_wait_queue,
					cdev->control_state[0] != 0xff, HZ))
			return;

		/* fix up some defaults */
		if ((cdev->control_state[1] != 2) ||
		    (cdev->control_state[2] != 3) ||
		    (cdev->control_state[4] != 2)) {
			cdev->control_state[1] = 2;
			cdev->control_state[2] = 3;
			cdev->control_state[4] = 2;
			snd_usb_caiaq_send_command(cdev,
				EP1_CMD_WRITE_IO, cdev->control_state, 6);
		}

		break;
	}

	if (cdev->spec.num_analog_audio_out +
	    cdev->spec.num_analog_audio_in +
	    cdev->spec.num_digital_audio_out +
	    cdev->spec.num_digital_audio_in > 0) {
		ret = snd_usb_caiaq_audio_init(cdev);
		if (ret < 0)
			dev_err(dev, "Unable to set up audio system (ret=%d)\n", ret);
	}

	if (cdev->spec.num_midi_in +
	    cdev->spec.num_midi_out > 0) {
		ret = snd_usb_caiaq_midi_init(cdev);
		if (ret < 0)
			dev_err(dev, "Unable to set up MIDI system (ret=%d)\n", ret);
	}

#ifdef CONFIG_SND_USB_CAIAQ_INPUT
	ret = snd_usb_caiaq_input_init(cdev);
	if (ret < 0)
		dev_err(dev, "Unable to set up input system (ret=%d)\n", ret);
#endif

	/* finally, register the card and all its sub-instances */
	ret = snd_card_register(cdev->chip.card);
	if (ret < 0) {
		dev_err(dev, "snd_card_register() returned %d\n", ret);
		snd_card_free(cdev->chip.card);
	}

	ret = snd_usb_caiaq_control_init(cdev);
	if (ret < 0)
		dev_err(dev, "Unable to set up control system (ret=%d)\n", ret);
}

static int create_card(struct usb_device *usb_dev,
		       struct usb_interface *intf,
		       struct snd_card **cardp)
{
	int devnum;
	int err;
	struct snd_card *card;
	struct snd_usb_caiaqdev *cdev;

	for (devnum = 0; devnum < SNDRV_CARDS; devnum++)
		if (enable[devnum])
			break;

	if (devnum >= SNDRV_CARDS)
		return -ENODEV;

	err = snd_card_new(&intf->dev,
			   index[devnum], id[devnum], THIS_MODULE,
			   sizeof(struct snd_usb_caiaqdev), &card);
	if (err < 0)
		return err;

	cdev = caiaqdev(card);
	cdev->chip.dev = usb_dev;
	cdev->chip.card = card;
	cdev->chip.usb_id = USB_ID(le16_to_cpu(usb_dev->descriptor.idVendor),
				  le16_to_cpu(usb_dev->descriptor.idProduct));
	spin_lock_init(&cdev->spinlock);

	*cardp = card;
	return 0;
}

static int init_card(struct snd_usb_caiaqdev *cdev)
{
	char *c, usbpath[32];
	struct usb_device *usb_dev = cdev->chip.dev;
	struct snd_card *card = cdev->chip.card;
	struct device *dev = caiaqdev_to_dev(cdev);
	int err, len;

	if (usb_set_interface(usb_dev, 0, 1) != 0) {
		dev_err(dev, "can't set alt interface.\n");
		return -EIO;
	}

	usb_init_urb(&cdev->ep1_in_urb);
	usb_init_urb(&cdev->midi_out_urb);

	usb_fill_bulk_urb(&cdev->ep1_in_urb, usb_dev,
			  usb_rcvbulkpipe(usb_dev, 0x1),
			  cdev->ep1_in_buf, EP1_BUFSIZE,
			  usb_ep1_command_reply_dispatch, cdev);

	usb_fill_bulk_urb(&cdev->midi_out_urb, usb_dev,
			  usb_sndbulkpipe(usb_dev, 0x1),
			  cdev->midi_out_buf, EP1_BUFSIZE,
			  snd_usb_caiaq_midi_output_done, cdev);

	/* sanity checks of EPs before actually submitting */
	if (usb_urb_ep_type_check(&cdev->ep1_in_urb) ||
	    usb_urb_ep_type_check(&cdev->midi_out_urb)) {
		dev_err(dev, "invalid EPs\n");
		return -EINVAL;
	}

	init_waitqueue_head(&cdev->ep1_wait_queue);
	init_waitqueue_head(&cdev->prepare_wait_queue);

	if (usb_submit_urb(&cdev->ep1_in_urb, GFP_KERNEL) != 0)
		return -EIO;

	err = snd_usb_caiaq_send_command(cdev, EP1_CMD_GET_DEVICE_INFO, NULL, 0);
	if (err)
		goto err_kill_urb;

	if (!wait_event_timeout(cdev->ep1_wait_queue, cdev->spec_received, HZ)) {
		err = -ENODEV;
		goto err_kill_urb;
	}

	usb_string(usb_dev, usb_dev->descriptor.iManufacturer,
		   cdev->vendor_name, CAIAQ_USB_STR_LEN);

	usb_string(usb_dev, usb_dev->descriptor.iProduct,
		   cdev->product_name, CAIAQ_USB_STR_LEN);

	strlcpy(card->driver, MODNAME, sizeof(card->driver));
	strlcpy(card->shortname, cdev->product_name, sizeof(card->shortname));
	strlcpy(card->mixername, cdev->product_name, sizeof(card->mixername));

	/* if the id was not passed as module option, fill it with a shortened
	 * version of the product string which does not contain any
	 * whitespaces */

	if (*card->id == '\0') {
		char id[sizeof(card->id)];

		memset(id, 0, sizeof(id));

		for (c = card->shortname, len = 0;
			*c && len < sizeof(card->id); c++)
			if (*c != ' ')
				id[len++] = *c;

		snd_card_set_id(card, id);
	}

	usb_make_path(usb_dev, usbpath, sizeof(usbpath));
	snprintf(card->longname, sizeof(card->longname), "%s %s (%s)",
		       cdev->vendor_name, cdev->product_name, usbpath);

	setup_card(cdev);
	return 0;

 err_kill_urb:
	usb_kill_urb(&cdev->ep1_in_urb);
	return err;
}

static int snd_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	int ret;
	struct snd_card *card = NULL;
	struct usb_device *usb_dev = interface_to_usbdev(intf);

	ret = create_card(usb_dev, intf, &card);

	if (ret < 0)
		return ret;

	usb_set_intfdata(intf, card);
	ret = init_card(caiaqdev(card));
	if (ret < 0) {
		dev_err(&usb_dev->dev, "unable to init card! (ret=%d)\n", ret);
		snd_card_free(card);
		return ret;
	}

	return 0;
}

static void snd_disconnect(struct usb_interface *intf)
{
	struct snd_card *card = usb_get_intfdata(intf);
	struct device *dev = intf->usb_dev;
	struct snd_usb_caiaqdev *cdev;

	if (!card)
		return;

	cdev = caiaqdev(card);
	dev_dbg(dev, "%s(%p)\n", __func__, intf);

	snd_card_disconnect(card);

#ifdef CONFIG_SND_USB_CAIAQ_INPUT
	snd_usb_caiaq_input_free(cdev);
#endif
	snd_usb_caiaq_audio_free(cdev);

	usb_kill_urb(&cdev->ep1_in_urb);
	usb_kill_urb(&cdev->midi_out_urb);

	snd_card_free(card);
	usb_reset_device(interface_to_usbdev(intf));
}


MODULE_DEVICE_TABLE(usb, snd_usb_id_table);
static struct usb_driver snd_usb_driver = {
	.name 		= MODNAME,
	.probe 		= snd_probe,
	.disconnect	= snd_disconnect,
	.id_table 	= snd_usb_id_table,
};

module_usb_driver(snd_usb_driver);
