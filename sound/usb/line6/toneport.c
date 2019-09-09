// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *                         Emil Myhrman (emil.myhrman@gmail.com)
 */

#include <linux/wait.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <sound/core.h>
#include <sound/control.h>

#include "capture.h"
#include "driver.h"
#include "playback.h"

enum line6_device_type {
	LINE6_GUITARPORT,
	LINE6_PODSTUDIO_GX,
	LINE6_PODSTUDIO_UX1,
	LINE6_PODSTUDIO_UX2,
	LINE6_TONEPORT_GX,
	LINE6_TONEPORT_UX1,
	LINE6_TONEPORT_UX2,
};

struct usb_line6_toneport;

struct toneport_led {
	struct led_classdev dev;
	char name[64];
	struct usb_line6_toneport *toneport;
	bool registered;
};

struct usb_line6_toneport {
	/* Generic Line 6 USB data */
	struct usb_line6 line6;

	/* Source selector */
	int source;

	/* Serial number of device */
	u32 serial_number;

	/* Firmware version (x 100) */
	u8 firmware_version;

	/* Device type */
	enum line6_device_type type;

	/* LED instances */
	struct toneport_led leds[2];
};

#define line6_to_toneport(x) container_of(x, struct usb_line6_toneport, line6)

static int toneport_send_cmd(struct usb_device *usbdev, int cmd1, int cmd2);

#define TONEPORT_PCM_DELAY 1

static struct snd_ratden toneport_ratden = {
	.num_min = 44100,
	.num_max = 44100,
	.num_step = 1,
	.den = 1
};

static struct line6_pcm_properties toneport_pcm_properties = {
	.playback_hw = {
				  .info = (SNDRV_PCM_INFO_MMAP |
					   SNDRV_PCM_INFO_INTERLEAVED |
					   SNDRV_PCM_INFO_BLOCK_TRANSFER |
					   SNDRV_PCM_INFO_MMAP_VALID |
					   SNDRV_PCM_INFO_PAUSE |
					   SNDRV_PCM_INFO_SYNC_START),
				  .formats = SNDRV_PCM_FMTBIT_S16_LE,
				  .rates = SNDRV_PCM_RATE_KNOT,
				  .rate_min = 44100,
				  .rate_max = 44100,
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
				 .formats = SNDRV_PCM_FMTBIT_S16_LE,
				 .rates = SNDRV_PCM_RATE_KNOT,
				 .rate_min = 44100,
				 .rate_max = 44100,
				 .channels_min = 2,
				 .channels_max = 2,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 64,
				 .period_bytes_max = 8192,
				 .periods_min = 1,
				 .periods_max = 1024},
	.rates = {
			    .nrats = 1,
			    .rats = &toneport_ratden},
	.bytes_per_channel = 2
};

static const struct {
	const char *name;
	int code;
} toneport_source_info[] = {
	{"Microphone", 0x0a01},
	{"Line", 0x0801},
	{"Instrument", 0x0b01},
	{"Inst & Mic", 0x0901}
};

static int toneport_send_cmd(struct usb_device *usbdev, int cmd1, int cmd2)
{
	int ret;

	ret = usb_control_msg(usbdev, usb_sndctrlpipe(usbdev, 0), 0x67,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      cmd1, cmd2, NULL, 0, LINE6_TIMEOUT * HZ);

	if (ret < 0) {
		dev_err(&usbdev->dev, "send failed (error %d)\n", ret);
		return ret;
	}

	return 0;
}

/* monitor info callback */
static int snd_toneport_monitor_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 256;
	return 0;
}

/* monitor get callback */
static int snd_toneport_monitor_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = line6pcm->volume_monitor;
	return 0;
}

/* monitor put callback */
static int snd_toneport_monitor_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	int err;

	if (ucontrol->value.integer.value[0] == line6pcm->volume_monitor)
		return 0;

	line6pcm->volume_monitor = ucontrol->value.integer.value[0];

	if (line6pcm->volume_monitor > 0) {
		err = line6_pcm_acquire(line6pcm, LINE6_STREAM_MONITOR, true);
		if (err < 0) {
			line6pcm->volume_monitor = 0;
			line6_pcm_release(line6pcm, LINE6_STREAM_MONITOR);
			return err;
		}
	} else {
		line6_pcm_release(line6pcm, LINE6_STREAM_MONITOR);
	}

	return 1;
}

/* source info callback */
static int snd_toneport_source_info(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	const int size = ARRAY_SIZE(toneport_source_info);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = size;

	if (uinfo->value.enumerated.item >= size)
		uinfo->value.enumerated.item = size - 1;

	strcpy(uinfo->value.enumerated.name,
	       toneport_source_info[uinfo->value.enumerated.item].name);

	return 0;
}

/* source get callback */
static int snd_toneport_source_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_toneport *toneport = line6_to_toneport(line6pcm->line6);

	ucontrol->value.enumerated.item[0] = toneport->source;
	return 0;
}

/* source put callback */
static int snd_toneport_source_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_toneport *toneport = line6_to_toneport(line6pcm->line6);
	unsigned int source;

	source = ucontrol->value.enumerated.item[0];
	if (source >= ARRAY_SIZE(toneport_source_info))
		return -EINVAL;
	if (source == toneport->source)
		return 0;

	toneport->source = source;
	toneport_send_cmd(toneport->line6.usbdev,
			  toneport_source_info[source].code, 0x0000);
	return 1;
}

static void toneport_startup(struct usb_line6 *line6)
{
	line6_pcm_acquire(line6->line6pcm, LINE6_STREAM_MONITOR, true);
}

/* control definition */
static const struct snd_kcontrol_new toneport_control_monitor = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Monitor Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_toneport_monitor_info,
	.get = snd_toneport_monitor_get,
	.put = snd_toneport_monitor_put
};

/* source selector definition */
static const struct snd_kcontrol_new toneport_control_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Capture Source",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_toneport_source_info,
	.get = snd_toneport_source_get,
	.put = snd_toneport_source_put
};

/*
	For the led on Guitarport.
	Brightness goes from 0x00 to 0x26. Set a value above this to have led
	blink.
	(void cmd_0x02(byte red, byte green)
*/

static bool toneport_has_led(struct usb_line6_toneport *toneport)
{
	switch (toneport->type) {
	case LINE6_GUITARPORT:
	case LINE6_TONEPORT_GX:
	/* add your device here if you are missing support for the LEDs */
		return true;

	default:
		return false;
	}
}

static const char * const toneport_led_colors[2] = { "red", "green" };
static const int toneport_led_init_vals[2] = { 0x00, 0x26 };

static void toneport_update_led(struct usb_line6_toneport *toneport)
{
	toneport_send_cmd(toneport->line6.usbdev,
			  (toneport->leds[0].dev.brightness << 8) | 0x0002,
			  toneport->leds[1].dev.brightness);
}

static void toneport_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct toneport_led *leds =
		container_of(led_cdev, struct toneport_led, dev);
	toneport_update_led(leds->toneport);
}

static int toneport_init_leds(struct usb_line6_toneport *toneport)
{
	struct device *dev = &toneport->line6.usbdev->dev;
	int i, err;

	for (i = 0; i < 2; i++) {
		struct toneport_led *led = &toneport->leds[i];
		struct led_classdev *leddev = &led->dev;

		led->toneport = toneport;
		snprintf(led->name, sizeof(led->name), "%s::%s",
			 dev_name(dev), toneport_led_colors[i]);
		leddev->name = led->name;
		leddev->brightness = toneport_led_init_vals[i];
		leddev->max_brightness = 0x26;
		leddev->brightness_set = toneport_led_brightness_set;
		err = led_classdev_register(dev, leddev);
		if (err)
			return err;
		led->registered = true;
	}

	return 0;
}

static void toneport_remove_leds(struct usb_line6_toneport *toneport)
{
	struct toneport_led *led;
	int i;

	for (i = 0; i < 2; i++) {
		led = &toneport->leds[i];
		if (!led->registered)
			break;
		led_classdev_unregister(&led->dev);
		led->registered = false;
	}
}

static bool toneport_has_source_select(struct usb_line6_toneport *toneport)
{
	switch (toneport->type) {
	case LINE6_TONEPORT_UX1:
	case LINE6_TONEPORT_UX2:
	case LINE6_PODSTUDIO_UX1:
	case LINE6_PODSTUDIO_UX2:
		return true;

	default:
		return false;
	}
}

/*
	Setup Toneport device.
*/
static int toneport_setup(struct usb_line6_toneport *toneport)
{
	u32 *ticks;
	struct usb_line6 *line6 = &toneport->line6;
	struct usb_device *usbdev = line6->usbdev;

	ticks = kmalloc(sizeof(*ticks), GFP_KERNEL);
	if (!ticks)
		return -ENOMEM;

	/* sync time on device with host: */
	/* note: 32-bit timestamps overflow in year 2106 */
	*ticks = (u32)ktime_get_real_seconds();
	line6_write_data(line6, 0x80c6, ticks, 4);
	kfree(ticks);

	/* enable device: */
	toneport_send_cmd(usbdev, 0x0301, 0x0000);

	/* initialize source select: */
	if (toneport_has_source_select(toneport))
		toneport_send_cmd(usbdev,
				  toneport_source_info[toneport->source].code,
				  0x0000);

	if (toneport_has_led(toneport))
		toneport_update_led(toneport);

	schedule_delayed_work(&toneport->line6.startup_work,
			      msecs_to_jiffies(TONEPORT_PCM_DELAY * 1000));
	return 0;
}

/*
	Toneport device disconnected.
*/
static void line6_toneport_disconnect(struct usb_line6 *line6)
{
	struct usb_line6_toneport *toneport = line6_to_toneport(line6);

	if (toneport_has_led(toneport))
		toneport_remove_leds(toneport);
}


/*
	 Try to init Toneport device.
*/
static int toneport_init(struct usb_line6 *line6,
			 const struct usb_device_id *id)
{
	int err;
	struct usb_line6_toneport *toneport = line6_to_toneport(line6);

	toneport->type = id->driver_info;

	line6->disconnect = line6_toneport_disconnect;
	line6->startup = toneport_startup;

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &toneport_pcm_properties);
	if (err < 0)
		return err;

	/* register monitor control: */
	err = snd_ctl_add(line6->card,
			  snd_ctl_new1(&toneport_control_monitor,
				       line6->line6pcm));
	if (err < 0)
		return err;

	/* register source select control: */
	if (toneport_has_source_select(toneport)) {
		err =
		    snd_ctl_add(line6->card,
				snd_ctl_new1(&toneport_control_source,
					     line6->line6pcm));
		if (err < 0)
			return err;
	}

	line6_read_serial_number(line6, &toneport->serial_number);
	line6_read_data(line6, 0x80c2, &toneport->firmware_version, 1);

	if (toneport_has_led(toneport)) {
		err = toneport_init_leds(toneport);
		if (err < 0)
			return err;
	}

	err = toneport_setup(toneport);
	if (err)
		return err;

	/* register audio system: */
	return snd_card_register(line6->card);
}

#ifdef CONFIG_PM
/*
	Resume Toneport device after reset.
*/
static int toneport_reset_resume(struct usb_interface *interface)
{
	int err;

	err = toneport_setup(usb_get_intfdata(interface));
	if (err)
		return err;
	return line6_resume(interface);
}
#endif

#define LINE6_DEVICE(prod) USB_DEVICE(0x0e41, prod)
#define LINE6_IF_NUM(prod, n) USB_DEVICE_INTERFACE_NUMBER(0x0e41, prod, n)

/* table of devices that work with this driver */
static const struct usb_device_id toneport_id_table[] = {
	{ LINE6_DEVICE(0x4750),    .driver_info = LINE6_GUITARPORT },
	{ LINE6_DEVICE(0x4153),    .driver_info = LINE6_PODSTUDIO_GX },
	{ LINE6_DEVICE(0x4150),    .driver_info = LINE6_PODSTUDIO_UX1 },
	{ LINE6_IF_NUM(0x4151, 0), .driver_info = LINE6_PODSTUDIO_UX2 },
	{ LINE6_DEVICE(0x4147),    .driver_info = LINE6_TONEPORT_GX },
	{ LINE6_DEVICE(0x4141),    .driver_info = LINE6_TONEPORT_UX1 },
	{ LINE6_IF_NUM(0x4142, 0), .driver_info = LINE6_TONEPORT_UX2 },
	{}
};

MODULE_DEVICE_TABLE(usb, toneport_id_table);

static const struct line6_properties toneport_properties_table[] = {
	[LINE6_GUITARPORT] = {
		.id = "GuitarPort",
		.name = "GuitarPort",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* 1..4 seem to be ok */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODSTUDIO_GX] = {
		.id = "PODStudioGX",
		.name = "POD Studio GX",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* 1..4 seem to be ok */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODSTUDIO_UX1] = {
		.id = "PODStudioUX1",
		.name = "POD Studio UX1",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* 1..4 seem to be ok */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODSTUDIO_UX2] = {
		.id = "PODStudioUX2",
		.name = "POD Studio UX2",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* defaults to 44.1kHz, 16-bit */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_TONEPORT_GX] = {
		.id = "TonePortGX",
		.name = "TonePort GX",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* 1..4 seem to be ok */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_TONEPORT_UX1] = {
		.id = "TonePortUX1",
		.name = "TonePort UX1",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* 1..4 seem to be ok */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_TONEPORT_UX2] = {
		.id = "TonePortUX2",
		.name = "TonePort UX2",
		.capabilities	= LINE6_CAP_PCM,
		.altsetting = 2,  /* defaults to 44.1kHz, 16-bit */
		/* no control channel */
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
};

/*
	Probe USB device.
*/
static int toneport_probe(struct usb_interface *interface,
			  const struct usb_device_id *id)
{
	return line6_probe(interface, id, "Line6-TonePort",
			   &toneport_properties_table[id->driver_info],
			   toneport_init, sizeof(struct usb_line6_toneport));
}

static struct usb_driver toneport_driver = {
	.name = KBUILD_MODNAME,
	.probe = toneport_probe,
	.disconnect = line6_disconnect,
#ifdef CONFIG_PM
	.suspend = line6_suspend,
	.resume = line6_resume,
	.reset_resume = toneport_reset_resume,
#endif
	.id_table = toneport_id_table,
};

module_usb_driver(toneport_driver);

MODULE_DESCRIPTION("TonePort USB driver");
MODULE_LICENSE("GPL");
