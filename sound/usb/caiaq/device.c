/*
 * caiaq.c: ALSA driver for caiaq/NativeInstruments devices
 *
 *   Copyright (c) 2007 Daniel Mack <daniel@caiaq.de>
 *                      Karsten Wiese <fzu@wemgehoertderstaat.de>
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

#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
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
MODULE_DESCRIPTION("caiaq USB audio, version 1.3.17");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Native Instruments, RigKontrol2},"
			 "{Native Instruments, RigKontrol3},"
			 "{Native Instruments, Kore Controller},"
			 "{Native Instruments, Kore Controller 2},"
			 "{Native Instruments, Audio Kontrol 1},"
			 "{Native Instruments, Audio 4 DJ},"
			 "{Native Instruments, Audio 8 DJ},"
			 "{Native Instruments, Session I/O},"
			 "{Native Instruments, GuitarRig mobile}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char* id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable this card */
static int snd_card_used[SNDRV_CARDS];

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

static struct usb_device_id snd_usb_id_table[] = {
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
	{ /* terminator */ }
};

static void usb_ep1_command_reply_dispatch (struct urb* urb)
{
	int ret;
	struct snd_usb_caiaqdev *dev = urb->context;
	unsigned char *buf = urb->transfer_buffer;

	if (urb->status || !dev) {
		log("received EP1 urb->status = %i\n", urb->status);
		return;
	}

	switch(buf[0]) {
	case EP1_CMD_GET_DEVICE_INFO:
	 	memcpy(&dev->spec, buf+1, sizeof(struct caiaq_device_spec));
		dev->spec.fw_version = le16_to_cpu(dev->spec.fw_version);
		debug("device spec (firmware %d): audio: %d in, %d out, "
			"MIDI: %d in, %d out, data alignment %d\n",
			dev->spec.fw_version,
			dev->spec.num_analog_audio_in,
			dev->spec.num_analog_audio_out,
			dev->spec.num_midi_in,
			dev->spec.num_midi_out,
			dev->spec.data_alignment);

		dev->spec_received++;
		wake_up(&dev->ep1_wait_queue);
		break;
	case EP1_CMD_AUDIO_PARAMS:
		dev->audio_parm_answer = buf[1];
		wake_up(&dev->ep1_wait_queue);
		break;
	case EP1_CMD_MIDI_READ:
		snd_usb_caiaq_midi_handle_input(dev, buf[1], buf + 3, buf[2]);
		break;
	case EP1_CMD_READ_IO:
		if (dev->chip.usb_id ==
			USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ)) {
			if (urb->actual_length > sizeof(dev->control_state))
				urb->actual_length = sizeof(dev->control_state);
			memcpy(dev->control_state, buf + 1, urb->actual_length);
			wake_up(&dev->ep1_wait_queue);
			break;
		}
#ifdef CONFIG_SND_USB_CAIAQ_INPUT
	case EP1_CMD_READ_ERP:
	case EP1_CMD_READ_ANALOG:
		snd_usb_caiaq_input_dispatch(dev, buf, urb->actual_length);
#endif
		break;
	}

	dev->ep1_in_urb.actual_length = 0;
	ret = usb_submit_urb(&dev->ep1_in_urb, GFP_ATOMIC);
	if (ret < 0)
		log("unable to submit urb. OOM!?\n");
}

int snd_usb_caiaq_send_command(struct snd_usb_caiaqdev *dev,
			       unsigned char command,
			       const unsigned char *buffer,
			       int len)
{
	int actual_len;
	struct usb_device *usb_dev = dev->chip.dev;

	if (!usb_dev)
		return -EIO;

	if (len > EP1_BUFSIZE - 1)
		len = EP1_BUFSIZE - 1;

	if (buffer && len > 0)
		memcpy(dev->ep1_out_buf+1, buffer, len);

	dev->ep1_out_buf[0] = command;
	return usb_bulk_msg(usb_dev, usb_sndbulkpipe(usb_dev, 1),
			   dev->ep1_out_buf, len+1, &actual_len, 200);
}

int snd_usb_caiaq_set_audio_params (struct snd_usb_caiaqdev *dev,
		   		    int rate, int depth, int bpp)
{
	int ret;
	char tmp[5];

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

	debug("setting audio params: %d Hz, %d bits, %d bpp\n",
		rate, depth, bpp);

	dev->audio_parm_answer = -1;
	ret = snd_usb_caiaq_send_command(dev, EP1_CMD_AUDIO_PARAMS,
					 tmp, sizeof(tmp));

	if (ret)
		return ret;

	if (!wait_event_timeout(dev->ep1_wait_queue,
	    dev->audio_parm_answer >= 0, HZ))
		return -EPIPE;

	if (dev->audio_parm_answer != 1)
		debug("unable to set the device's audio params\n");
	else
		dev->bpp = bpp;

	return dev->audio_parm_answer == 1 ? 0 : -EINVAL;
}

int snd_usb_caiaq_set_auto_msg(struct snd_usb_caiaqdev *dev,
			       int digital, int analog, int erp)
{
	char tmp[3] = { digital, analog, erp };
	return snd_usb_caiaq_send_command(dev, EP1_CMD_AUTO_MSG,
					  tmp, sizeof(tmp));
}

static void __devinit setup_card(struct snd_usb_caiaqdev *dev)
{
	int ret;
	char val[4];

	/* device-specific startup specials */
	switch (dev->chip.usb_id) {
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL2):
		/* RigKontrol2 - display centered dash ('-') */
		val[0] = 0x00;
		val[1] = 0x00;
		val[2] = 0x01;
		snd_usb_caiaq_send_command(dev, EP1_CMD_WRITE_IO, val, 3);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_RIGKONTROL3):
		/* RigKontrol2 - display two centered dashes ('--') */
		val[0] = 0x00;
		val[1] = 0x40;
		val[2] = 0x40;
		val[3] = 0x00;
		snd_usb_caiaq_send_command(dev, EP1_CMD_WRITE_IO, val, 4);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AK1):
		/* Audio Kontrol 1 - make USB-LED stop blinking */
		val[0] = 0x00;
		snd_usb_caiaq_send_command(dev, EP1_CMD_WRITE_IO, val, 1);
		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO8DJ):
		/* Audio 8 DJ - trigger read of current settings */
		dev->control_state[0] = 0xff;
		snd_usb_caiaq_set_auto_msg(dev, 1, 0, 0);
		snd_usb_caiaq_send_command(dev, EP1_CMD_READ_IO, NULL, 0);

		if (!wait_event_timeout(dev->ep1_wait_queue,
					dev->control_state[0] != 0xff, HZ))
			return;

		/* fix up some defaults */
		if ((dev->control_state[1] != 2) ||
		    (dev->control_state[2] != 3) ||
		    (dev->control_state[4] != 2)) {
			dev->control_state[1] = 2;
			dev->control_state[2] = 3;
			dev->control_state[4] = 2;
			snd_usb_caiaq_send_command(dev,
				EP1_CMD_WRITE_IO, dev->control_state, 6);
		}

		break;
	case USB_ID(USB_VID_NATIVEINSTRUMENTS, USB_PID_AUDIO4DJ):
		/* Audio 4 DJ - default input mode to phono */
		dev->control_state[0] = 2;
		snd_usb_caiaq_send_command(dev, EP1_CMD_WRITE_IO,
			dev->control_state, 1);
		break;
	}

	if (dev->spec.num_analog_audio_out +
	    dev->spec.num_analog_audio_in +
	    dev->spec.num_digital_audio_out +
	    dev->spec.num_digital_audio_in > 0) {
		ret = snd_usb_caiaq_audio_init(dev);
		if (ret < 0)
			log("Unable to set up audio system (ret=%d)\n", ret);
	}

	if (dev->spec.num_midi_in +
	    dev->spec.num_midi_out > 0) {
		ret = snd_usb_caiaq_midi_init(dev);
		if (ret < 0)
			log("Unable to set up MIDI system (ret=%d)\n", ret);
	}

#ifdef CONFIG_SND_USB_CAIAQ_INPUT
	ret = snd_usb_caiaq_input_init(dev);
	if (ret < 0)
		log("Unable to set up input system (ret=%d)\n", ret);
#endif

	/* finally, register the card and all its sub-instances */
	ret = snd_card_register(dev->chip.card);
	if (ret < 0) {
		log("snd_card_register() returned %d\n", ret);
		snd_card_free(dev->chip.card);
	}

	ret = snd_usb_caiaq_control_init(dev);
	if (ret < 0)
		log("Unable to set up control system (ret=%d)\n", ret);
}

static int create_card(struct usb_device* usb_dev, struct snd_card **cardp)
{
	int devnum;
	int err;
	struct snd_card *card;
	struct snd_usb_caiaqdev *dev;

	for (devnum = 0; devnum < SNDRV_CARDS; devnum++)
		if (enable[devnum] && !snd_card_used[devnum])
			break;

	if (devnum >= SNDRV_CARDS)
		return -ENODEV;

	err = snd_card_create(index[devnum], id[devnum], THIS_MODULE,
			      sizeof(struct snd_usb_caiaqdev), &card);
	if (err < 0)
		return err;

	dev = caiaqdev(card);
	dev->chip.dev = usb_dev;
	dev->chip.card = card;
	dev->chip.usb_id = USB_ID(le16_to_cpu(usb_dev->descriptor.idVendor),
				  le16_to_cpu(usb_dev->descriptor.idProduct));
	spin_lock_init(&dev->spinlock);
	snd_card_set_dev(card, &usb_dev->dev);

	*cardp = card;
	return 0;
}

static int __devinit init_card(struct snd_usb_caiaqdev *dev)
{
	char *c, usbpath[32];
	struct usb_device *usb_dev = dev->chip.dev;
	struct snd_card *card = dev->chip.card;
	int err, len;

	if (usb_set_interface(usb_dev, 0, 1) != 0) {
		log("can't set alt interface.\n");
		return -EIO;
	}

	usb_init_urb(&dev->ep1_in_urb);
	usb_init_urb(&dev->midi_out_urb);

	usb_fill_bulk_urb(&dev->ep1_in_urb, usb_dev,
			  usb_rcvbulkpipe(usb_dev, 0x1),
			  dev->ep1_in_buf, EP1_BUFSIZE,
			  usb_ep1_command_reply_dispatch, dev);

	usb_fill_bulk_urb(&dev->midi_out_urb, usb_dev,
			  usb_sndbulkpipe(usb_dev, 0x1),
			  dev->midi_out_buf, EP1_BUFSIZE,
			  snd_usb_caiaq_midi_output_done, dev);

	init_waitqueue_head(&dev->ep1_wait_queue);
	init_waitqueue_head(&dev->prepare_wait_queue);

	if (usb_submit_urb(&dev->ep1_in_urb, GFP_KERNEL) != 0)
		return -EIO;

	err = snd_usb_caiaq_send_command(dev, EP1_CMD_GET_DEVICE_INFO, NULL, 0);
	if (err)
		return err;

	if (!wait_event_timeout(dev->ep1_wait_queue, dev->spec_received, HZ))
		return -ENODEV;

	usb_string(usb_dev, usb_dev->descriptor.iManufacturer,
		   dev->vendor_name, CAIAQ_USB_STR_LEN);

	usb_string(usb_dev, usb_dev->descriptor.iProduct,
		   dev->product_name, CAIAQ_USB_STR_LEN);

	strlcpy(card->driver, MODNAME, sizeof(card->driver));
	strlcpy(card->shortname, dev->product_name, sizeof(card->shortname));
	strlcpy(card->mixername, dev->product_name, sizeof(card->mixername));

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
	snprintf(card->longname, sizeof(card->longname),
		       "%s %s (%s)",
		       dev->vendor_name, dev->product_name, usbpath);

	setup_card(dev);
	return 0;
}

static int __devinit snd_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	int ret;
	struct snd_card *card;
	struct usb_device *device = interface_to_usbdev(intf);

	ret = create_card(device, &card);

	if (ret < 0)
		return ret;

	usb_set_intfdata(intf, card);
	ret = init_card(caiaqdev(card));
	if (ret < 0) {
		log("unable to init card! (ret=%d)\n", ret);
		snd_card_free(card);
		return ret;
	}

	return 0;
}

static void snd_disconnect(struct usb_interface *intf)
{
	struct snd_usb_caiaqdev *dev;
	struct snd_card *card = usb_get_intfdata(intf);

	debug("%s(%p)\n", __func__, intf);

	if (!card)
		return;

	dev = caiaqdev(card);
	snd_card_disconnect(card);

#ifdef CONFIG_SND_USB_CAIAQ_INPUT
	snd_usb_caiaq_input_free(dev);
#endif
	snd_usb_caiaq_audio_free(dev);

	usb_kill_urb(&dev->ep1_in_urb);
	usb_kill_urb(&dev->midi_out_urb);

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

static int __init snd_module_init(void)
{
	return usb_register(&snd_usb_driver);
}

static void __exit snd_module_exit(void)
{
	usb_deregister(&snd_usb_driver);
}

module_init(snd_module_init)
module_exit(snd_module_exit)

