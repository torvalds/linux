// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Pod HD
 *
 * Copyright (C) 2011 Stefan Hajnoczi <stefanha@gmail.com>
 * Copyright (C) 2015 Andrej Krutak <dev@andree.sk>
 * Copyright (C) 2017 Hans P. Moller <hmoller@uc.cl>
 */

#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>

#include "driver.h"
#include "pcm.h"

#define PODHD_STARTUP_DELAY 500

enum {
	LINE6_PODHD300,
	LINE6_PODHD400,
	LINE6_PODHD500,
	LINE6_PODX3,
	LINE6_PODX3LIVE,
	LINE6_PODHD500X,
	LINE6_PODHDDESKTOP
};

struct usb_line6_podhd {
	/* Generic Line 6 USB data */
	struct usb_line6 line6;

	/* Serial number of device */
	u32 serial_number;

	/* Firmware version */
	int firmware_version;

	/* Monitor level */
	int monitor_level;
};

#define line6_to_podhd(x)	container_of(x, struct usb_line6_podhd, line6)

static const struct snd_ratden podhd_ratden = {
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
	.bytes_per_channel = 3 /* SNDRV_PCM_FMTBIT_S24_3LE */
};

static struct line6_pcm_properties podx3_pcm_properties = {
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
				 /* 1+2: Main signal (out), 3+4: Tone 1,
				  * 5+6: Tone 2, 7+8: raw
				  */
				 .channels_min = 8,
				 .channels_max = 8,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 64,
				 .period_bytes_max = 8192,
				 .periods_min = 1,
				 .periods_max = 1024},
	.rates = {
			    .nrats = 1,
			    .rats = &podhd_ratden},
	.bytes_per_channel = 3 /* SNDRV_PCM_FMTBIT_S24_3LE */
};
static struct usb_driver podhd_driver;

static ssize_t serial_number_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct snd_card *card = dev_to_snd_card(dev);
	struct usb_line6_podhd *pod = card->private_data;

	return sysfs_emit(buf, "%u\n", pod->serial_number);
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct snd_card *card = dev_to_snd_card(dev);
	struct usb_line6_podhd *pod = card->private_data;

	return sysfs_emit(buf, "%06x\n", pod->firmware_version);
}

static DEVICE_ATTR_RO(firmware_version);
static DEVICE_ATTR_RO(serial_number);

static struct attribute *podhd_dev_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_serial_number.attr,
	NULL
};

static const struct attribute_group podhd_dev_attr_group = {
	.name = "podhd",
	.attrs = podhd_dev_attrs,
};

/*
 * POD X3 startup procedure.
 *
 * May be compatible with other POD HD's, since it's also similar to the
 * previous POD setup. In any case, it doesn't seem to be required for the
 * audio nor bulk interfaces to work.
 */

static int podhd_dev_start(struct usb_line6_podhd *pod)
{
	int ret;
	u8 init_bytes[8];
	int i;
	struct usb_device *usbdev = pod->line6.usbdev;

	ret = usb_control_msg_send(usbdev, 0,
					0x67, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
					0x11, 0,
					NULL, 0, LINE6_TIMEOUT, GFP_KERNEL);
	if (ret) {
		dev_err(pod->line6.ifcdev, "read request failed (error %d)\n", ret);
		goto exit;
	}

	/* NOTE: looks like some kind of ping message */
	ret = usb_control_msg_recv(usbdev, 0, 0x67,
					USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
					0x11, 0x0,
					init_bytes, 3, LINE6_TIMEOUT, GFP_KERNEL);
	if (ret) {
		dev_err(pod->line6.ifcdev,
			"receive length failed (error %d)\n", ret);
		goto exit;
	}

	pod->firmware_version =
		(init_bytes[0] << 16) | (init_bytes[1] << 8) | (init_bytes[2] << 0);

	for (i = 0; i <= 16; i++) {
		ret = line6_read_data(&pod->line6, 0xf000 + 0x08 * i, init_bytes, 8);
		if (ret < 0)
			goto exit;
	}

	ret = usb_control_msg_send(usbdev, 0,
					USB_REQ_SET_FEATURE,
					USB_TYPE_STANDARD | USB_RECIP_DEVICE | USB_DIR_OUT,
					1, 0,
					NULL, 0, LINE6_TIMEOUT, GFP_KERNEL);
exit:
	return ret;
}

static void podhd_startup(struct usb_line6 *line6)
{
	struct usb_line6_podhd *pod = line6_to_podhd(line6);

	podhd_dev_start(pod);
	line6_read_serial_number(&pod->line6, &pod->serial_number);
	if (snd_card_register(line6->card))
		dev_err(line6->ifcdev, "Failed to register POD HD card.\n");
}

static void podhd_disconnect(struct usb_line6 *line6)
{
	struct usb_line6_podhd *pod = line6_to_podhd(line6);

	if (pod->line6.properties->capabilities & LINE6_CAP_CONTROL_INFO) {
		struct usb_interface *intf;

		intf = usb_ifnum_to_if(line6->usbdev,
					pod->line6.properties->ctrl_if);
		if (intf)
			usb_driver_release_interface(&podhd_driver, intf);
	}
}

static const unsigned int float_zero_to_one_lookup[] = {
0x00000000, 0x3c23d70a, 0x3ca3d70a, 0x3cf5c28f, 0x3d23d70a, 0x3d4ccccd,
0x3d75c28f, 0x3d8f5c29, 0x3da3d70a, 0x3db851ec, 0x3dcccccd, 0x3de147ae,
0x3df5c28f, 0x3e051eb8, 0x3e0f5c29, 0x3e19999a, 0x3e23d70a, 0x3e2e147b,
0x3e3851ec, 0x3e428f5c, 0x3e4ccccd, 0x3e570a3d, 0x3e6147ae, 0x3e6b851f,
0x3e75c28f, 0x3e800000, 0x3e851eb8, 0x3e8a3d71, 0x3e8f5c29, 0x3e947ae1,
0x3e99999a, 0x3e9eb852, 0x3ea3d70a, 0x3ea8f5c3, 0x3eae147b, 0x3eb33333,
0x3eb851ec, 0x3ebd70a4, 0x3ec28f5c, 0x3ec7ae14, 0x3ecccccd, 0x3ed1eb85,
0x3ed70a3d, 0x3edc28f6, 0x3ee147ae, 0x3ee66666, 0x3eeb851f, 0x3ef0a3d7,
0x3ef5c28f, 0x3efae148, 0x3f000000, 0x3f028f5c, 0x3f051eb8, 0x3f07ae14,
0x3f0a3d71, 0x3f0ccccd, 0x3f0f5c29, 0x3f11eb85, 0x3f147ae1, 0x3f170a3d,
0x3f19999a, 0x3f1c28f6, 0x3f1eb852, 0x3f2147ae, 0x3f23d70a, 0x3f266666,
0x3f28f5c3, 0x3f2b851f, 0x3f2e147b, 0x3f30a3d7, 0x3f333333, 0x3f35c28f,
0x3f3851ec, 0x3f3ae148, 0x3f3d70a4, 0x3f400000, 0x3f428f5c, 0x3f451eb8,
0x3f47ae14, 0x3f4a3d71, 0x3f4ccccd, 0x3f4f5c29, 0x3f51eb85, 0x3f547ae1,
0x3f570a3d, 0x3f59999a, 0x3f5c28f6, 0x3f5eb852, 0x3f6147ae, 0x3f63d70a,
0x3f666666, 0x3f68f5c3, 0x3f6b851f, 0x3f6e147b, 0x3f70a3d7, 0x3f733333,
0x3f75c28f, 0x3f7851ec, 0x3f7ae148, 0x3f7d70a4, 0x3f800000
};

static void podhd_set_monitor_level(struct usb_line6_podhd *podhd, int value)
{
	unsigned int fl;
	static const unsigned char msg[16] = {
		/* Chunk is 0xc bytes (without first word) */
		0x0c, 0x00,
		/* First chunk in the message */
		0x01, 0x00,
		/* Message size is 2 4-byte words */
		0x02, 0x00,
		/* Unknown */
		0x04, 0x41,
		/* Unknown */
		0x04, 0x00, 0x13, 0x00,
		/* Volume, LE float32, 0.0 - 1.0 */
		0x00, 0x00, 0x00, 0x00
	};
	unsigned char *buf;

	buf = kmemdup(msg, sizeof(msg), GFP_KERNEL);
	if (!buf)
		return;

	if (value < 0)
		value = 0;

	if (value >= ARRAY_SIZE(float_zero_to_one_lookup))
		value = ARRAY_SIZE(float_zero_to_one_lookup) - 1;

	fl = float_zero_to_one_lookup[value];

	buf[12] = (fl >> 0) & 0xff;
	buf[13] = (fl >> 8) & 0xff;
	buf[14] = (fl >> 16) & 0xff;
	buf[15] = (fl >> 24) & 0xff;

	line6_send_raw_message(&podhd->line6, buf, sizeof(msg));
	kfree(buf);

	podhd->monitor_level = value;
}

/* control info callback */
static int snd_podhd_control_monitor_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	uinfo->value.integer.step = 1;
	return 0;
}

/* control get callback */
static int snd_podhd_control_monitor_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_podhd *podhd = line6_to_podhd(line6pcm->line6);

	ucontrol->value.integer.value[0] = podhd->monitor_level;
	return 0;
}

/* control put callback */
static int snd_podhd_control_monitor_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_podhd *podhd = line6_to_podhd(line6pcm->line6);

	if (ucontrol->value.integer.value[0] == podhd->monitor_level)
		return 0;

	podhd_set_monitor_level(podhd, ucontrol->value.integer.value[0]);
	return 1;
}

/* control definition */
static const struct snd_kcontrol_new podhd_control_monitor = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Monitor Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_podhd_control_monitor_info,
	.get = snd_podhd_control_monitor_get,
	.put = snd_podhd_control_monitor_put
};

/*
	Try to init POD HD device.
*/
static int podhd_init(struct usb_line6 *line6,
		      const struct usb_device_id *id)
{
	int err;
	struct usb_line6_podhd *pod = line6_to_podhd(line6);
	struct usb_interface *intf;

	line6->disconnect = podhd_disconnect;
	line6->startup = podhd_startup;

	if (pod->line6.properties->capabilities & LINE6_CAP_CONTROL) {
		/* claim the data interface */
		intf = usb_ifnum_to_if(line6->usbdev,
					pod->line6.properties->ctrl_if);
		if (!intf) {
			dev_err(pod->line6.ifcdev, "interface %d not found\n",
				pod->line6.properties->ctrl_if);
			return -ENODEV;
		}

		err = usb_driver_claim_interface(&podhd_driver, intf, NULL);
		if (err != 0) {
			dev_err(pod->line6.ifcdev, "can't claim interface %d, error %d\n",
				pod->line6.properties->ctrl_if, err);
			return err;
		}
	}

	if (pod->line6.properties->capabilities & LINE6_CAP_CONTROL_INFO) {
		/* create sysfs entries: */
		err = snd_card_add_dev_attr(line6->card, &podhd_dev_attr_group);
		if (err < 0)
			return err;
	}

	if (pod->line6.properties->capabilities & LINE6_CAP_PCM) {
		/* initialize PCM subsystem: */
		err = line6_init_pcm(line6,
			(id->driver_info == LINE6_PODX3 ||
			id->driver_info == LINE6_PODX3LIVE) ? &podx3_pcm_properties :
			&podhd_pcm_properties);
		if (err < 0)
			return err;
	}

	if (pod->line6.properties->capabilities & LINE6_CAP_HWMON_CTL) {
		podhd_set_monitor_level(pod, 100);
		err = snd_ctl_add(line6->card,
				  snd_ctl_new1(&podhd_control_monitor,
					       line6->line6pcm));
		if (err < 0)
			return err;
	}

	if (!(pod->line6.properties->capabilities & LINE6_CAP_CONTROL_INFO)) {
		/* register USB audio system directly */
		return snd_card_register(line6->card);
	}

	/* init device and delay registering */
	schedule_delayed_work(&line6->startup_work,
			      msecs_to_jiffies(PODHD_STARTUP_DELAY));
	return 0;
}

#define LINE6_DEVICE(prod) USB_DEVICE(0x0e41, prod)
#define LINE6_IF_NUM(prod, n) USB_DEVICE_INTERFACE_NUMBER(0x0e41, prod, n)

/* table of devices that work with this driver */
static const struct usb_device_id podhd_id_table[] = {
	/* TODO: no need to alloc data interfaces when only audio is used */
	{ LINE6_DEVICE(0x5057),    .driver_info = LINE6_PODHD300 },
	{ LINE6_DEVICE(0x5058),    .driver_info = LINE6_PODHD400 },
	{ LINE6_IF_NUM(0x414D, 0), .driver_info = LINE6_PODHD500 },
	{ LINE6_IF_NUM(0x414A, 0), .driver_info = LINE6_PODX3 },
	{ LINE6_IF_NUM(0x414B, 0), .driver_info = LINE6_PODX3LIVE },
	{ LINE6_IF_NUM(0x4159, 0), .driver_info = LINE6_PODHD500X },
	{ LINE6_IF_NUM(0x4156, 0), .driver_info = LINE6_PODHDDESKTOP },
	{}
};

MODULE_DEVICE_TABLE(usb, podhd_id_table);

static const struct line6_properties podhd_properties_table[] = {
	[LINE6_PODHD300] = {
		.id = "PODHD300",
		.name = "POD HD300",
		.capabilities	= LINE6_CAP_PCM
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
		.capabilities	= LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODHD500] = {
		.id = "PODHD500",
		.name = "POD HD500",
		.capabilities	= LINE6_CAP_PCM | LINE6_CAP_CONTROL
				| LINE6_CAP_HWMON | LINE6_CAP_HWMON_CTL,
		.altsetting = 1,
		.ctrl_if = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ep_audio_r = 0x86,
		.ep_audio_w = 0x02,
	},
	[LINE6_PODX3] = {
		.id = "PODX3",
		.name = "POD X3",
		.capabilities	= LINE6_CAP_CONTROL | LINE6_CAP_CONTROL_INFO
				| LINE6_CAP_PCM | LINE6_CAP_HWMON | LINE6_CAP_IN_NEEDS_OUT,
		.altsetting = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ctrl_if = 1,
		.ep_audio_r = 0x86,
		.ep_audio_w = 0x02,
	},
	[LINE6_PODX3LIVE] = {
		.id = "PODX3LIVE",
		.name = "POD X3 LIVE",
		.capabilities	= LINE6_CAP_CONTROL | LINE6_CAP_CONTROL_INFO
				| LINE6_CAP_PCM | LINE6_CAP_HWMON | LINE6_CAP_IN_NEEDS_OUT,
		.altsetting = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ctrl_if = 1,
		.ep_audio_r = 0x86,
		.ep_audio_w = 0x02,
	},
	[LINE6_PODHD500X] = {
		.id = "PODHD500X",
		.name = "POD HD500X",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_PCM | LINE6_CAP_HWMON,
		.altsetting = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ctrl_if = 1,
		.ep_audio_r = 0x86,
		.ep_audio_w = 0x02,
	},
	[LINE6_PODHDDESKTOP] = {
		.id = "PODHDDESKTOP",
		.name = "POD HDDESKTOP",
		.capabilities    = LINE6_CAP_CONTROL
			| LINE6_CAP_PCM | LINE6_CAP_HWMON,
		.altsetting = 1,
		.ep_ctrl_r = 0x81,
		.ep_ctrl_w = 0x01,
		.ctrl_if = 1,
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
			   podhd_init, sizeof(struct usb_line6_podhd));
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
