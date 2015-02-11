/*
 * Line 6 Pod HD
 *
 * Copyright (C) 2011 Stefan Hajnoczi <stefanha@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>

#include "driver.h"
#include "pcm.h"

enum {
	LINE6_PODHD300,
	LINE6_PODHD400,
	LINE6_PODHD500_0,
	LINE6_PODHD500_1,
};

#define PODHD_BYTES_PER_FRAME 6	/* 24bit audio (stereo) */

static struct snd_ratden podhd_ratden = {
	.num_min = 48000,
	.num_max = 48000,
	.num_step = 1,
	.den = 1,
};

static struct line6_pcm_properties podhd_pcm_properties = {
	.playback_hw = {
				  .info = (SNDRV_PCM_INFO_MMAP |
					   SNDRV_PCM_INFO_INTERLEAVED |
					   SNDRV_PCM_INFO_BLOCK_TRANSFER |
					   SNDRV_PCM_INFO_MMAP_VALID |
					   SNDRV_PCM_INFO_PAUSE |
					   SNDRV_PCM_INFO_SYNC_START),
				  .formats = SNDRV_PCM_FMTBIT_S24_3LE,
				  .rates = SNDRV_PCM_RATE_48000,
				  .rate_min = 48000,
				  .rate_max = 48000,
				  .channels_min = 2,
				  .channels_max = 2,
				  .buffer_bytes_max = 60000,
				  .period_bytes_min = 64,
				  .period_bytes_max = 8192,
				  .periods_min = 1,
				  .periods_max = 1024},
	.capture_hw = {
				 .info = (SNDRV_PCM_INFO_MMAP |
					  SNDRV_PCM_INFO_INTERLEAVED |
					  SNDRV_PCM_INFO_BLOCK_TRANSFER |
					  SNDRV_PCM_INFO_MMAP_VALID |
					  SNDRV_PCM_INFO_SYNC_START),
				 .formats = SNDRV_PCM_FMTBIT_S24_3LE,
				 .rates = SNDRV_PCM_RATE_48000,
				 .rate_min = 48000,
				 .rate_max = 48000,
				 .channels_min = 2,
				 .channels_max = 2,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 64,
				 .period_bytes_max = 8192,
				 .periods_min = 1,
				 .periods_max = 1024},
	.rates = {
			    .nrats = 1,
			    .rats = &podhd_ratden},
	.bytes_per_frame = PODHD_BYTES_PER_FRAME
};

/*
	Try to init POD HD device.
*/
static int podhd_init(struct usb_line6 *line6,
		      const struct usb_device_id *id)
{
	int err;

	/* initialize MIDI subsystem: */
	err = line6_init_midi(line6);
	if (err < 0)
		return err;

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &podhd_pcm_properties);
	if (err < 0)
		return err;

	/* register USB audio system: */
	return snd_card_register(line6->card);
}

#define LINE6_DEVICE(prod) USB_DEVICE(0x0e41, prod)
#define LINE6_IF_NUM(prod, n) USB_DEVICE_INTERFACE_NUMBER(0x0e41, prod, n)

/* table of devices that work with this driver */
static const struct usb_device_id podhd_id_table[] = {
	{ LINE6_DEVICE(0x5057),    .driver_info = LINE6_PODHD300 },
	{ LINE6_DEVICE(0x5058),    .driver_info = LINE6_PODHD400 },
	{ LINE6_IF_NUM(0x414D, 0), .driver_info = LINE6_PODHD500_0 },
	{ LINE6_IF_NUM(0x414D, 1), .driver_info = LINE6_PODHD500_1 },
	{}
};

MODULE_DEVICE_TABLE(usb, podhd_id_table);

static const struct line6_properties podhd_properties_table[] = {
	[LINE6_PODHD300] = {
		.id = "PODHD300",
		.name = "POD HD300",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODHD400] = {
		.id = "PODHD400",
		.name = "POD HD400",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODHD500_0] = {
		.id = "PODHD500",
		.name = "POD HD500",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ep_audio_r = 0x86,
		.ep_audio_w = 0x02,
	},
	[LINE6_PODHD500_1] = {
		.id = "PODHD500",
		.name = "POD HD500",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ep_audio_r = 0x86,
		.ep_audio_w = 0x02,
	},
};

/*
	Probe USB device.
*/
static int podhd_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	return line6_probe(interface, id, "Line6-PODHD",
			   &podhd_properties_table[id->driver_info],
			   podhd_init, sizeof(struct usb_line6));
}

static struct usb_driver podhd_driver = {
	.name = KBUILD_MODNAME,
	.probe = podhd_probe,
	.disconnect = line6_disconnect,
#ifdef CONFIG_PM
	.suspend = line6_suspend,
	.resume = line6_resume,
	.reset_resume = line6_resume,
#endif
	.id_table = podhd_id_table,
};

module_usb_driver(podhd_driver);

MODULE_DESCRIPTION("Line 6 PODHD USB driver");
MODULE_LICENSE("GPL");
