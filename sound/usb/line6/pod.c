// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (line6@grabner-graz.at)
 */

#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/usb.h>

#include <sound/core.h>
#include <sound/control.h>

#include "capture.h"
#include "driver.h"
#include "playback.h"

/*
	Locate name in binary program dump
*/
#define	POD_NAME_OFFSET 0
#define	POD_NAME_LENGTH 16

/*
	Other constants
*/
#define POD_CONTROL_SIZE 0x80
#define POD_BUFSIZE_DUMPREQ 7
#define POD_STARTUP_DELAY 1000

/*
	Stages of POD startup procedure
*/
enum {
	POD_STARTUP_VERSIONREQ,
	POD_STARTUP_SETUP,
	POD_STARTUP_DONE,
};

enum {
	LINE6_BASSPODXT,
	LINE6_BASSPODXTLIVE,
	LINE6_BASSPODXTPRO,
	LINE6_POCKETPOD,
	LINE6_PODXT,
	LINE6_PODXTLIVE_POD,
	LINE6_PODXTPRO,
};

struct usb_line6_pod {
	/* Generic Line 6 USB data */
	struct usb_line6 line6;

	/* Instrument monitor level */
	int monitor_level;

	/* Current progress in startup procedure */
	int startup_progress;

	/* Serial number of device */
	u32 serial_number;

	/* Firmware version (x 100) */
	int firmware_version;

	/* Device ID */
	int device_id;
};

#define line6_to_pod(x)		container_of(x, struct usb_line6_pod, line6)

#define POD_SYSEX_CODE 3

/* *INDENT-OFF* */

enum {
	POD_SYSEX_SAVE      = 0x24,
	POD_SYSEX_SYSTEM    = 0x56,
	POD_SYSEX_SYSTEMREQ = 0x57,
	/* POD_SYSEX_UPDATE    = 0x6c, */  /* software update! */
	POD_SYSEX_STORE     = 0x71,
	POD_SYSEX_FINISH    = 0x72,
	POD_SYSEX_DUMPMEM   = 0x73,
	POD_SYSEX_DUMP      = 0x74,
	POD_SYSEX_DUMPREQ   = 0x75

	/* dumps entire internal memory of PODxt Pro */
	/* POD_SYSEX_DUMPMEM2  = 0x76 */
};

enum {
	POD_MONITOR_LEVEL  = 0x04,
	POD_SYSTEM_INVALID = 0x10000
};

/* *INDENT-ON* */

enum {
	POD_DUMP_MEMORY = 2
};

enum {
	POD_BUSY_READ,
	POD_BUSY_WRITE,
	POD_CHANNEL_DIRTY,
	POD_SAVE_PRESSED,
	POD_BUSY_MIDISEND
};

static const struct snd_ratden pod_ratden = {
	.num_min = 78125,
	.num_max = 78125,
	.num_step = 1,
	.den = 2
};

static struct line6_pcm_properties pod_pcm_properties = {
	.playback_hw = {
				  .info = (SNDRV_PCM_INFO_MMAP |
					   SNDRV_PCM_INFO_INTERLEAVED |
					   SNDRV_PCM_INFO_BLOCK_TRANSFER |
					   SNDRV_PCM_INFO_MMAP_VALID |
					   SNDRV_PCM_INFO_PAUSE |
					   SNDRV_PCM_INFO_SYNC_START),
				  .formats = SNDRV_PCM_FMTBIT_S24_3LE,
				  .rates = SNDRV_PCM_RATE_KNOT,
				  .rate_min = 39062,
				  .rate_max = 39063,
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
				 .rates = SNDRV_PCM_RATE_KNOT,
				 .rate_min = 39062,
				 .rate_max = 39063,
				 .channels_min = 2,
				 .channels_max = 2,
				 .buffer_bytes_max = 60000,
				 .period_bytes_min = 64,
				 .period_bytes_max = 8192,
				 .periods_min = 1,
				 .periods_max = 1024},
	.rates = {
			    .nrats = 1,
			    .rats = &pod_ratden},
	.bytes_per_channel = 3 /* SNDRV_PCM_FMTBIT_S24_3LE */
};


static const char pod_version_header[] = {
	0xf0, 0x7e, 0x7f, 0x06, 0x02
};

static char *pod_alloc_sysex_buffer(struct usb_line6_pod *pod, int code,
				    int size)
{
	return line6_alloc_sysex_buffer(&pod->line6, POD_SYSEX_CODE, code,
					size);
}

/*
	Process a completely received message.
*/
static void line6_pod_process_message(struct usb_line6 *line6)
{
	struct usb_line6_pod *pod = line6_to_pod(line6);
	const unsigned char *buf = pod->line6.buffer_message;

	if (memcmp(buf, pod_version_header, sizeof(pod_version_header)) == 0) {
		pod->firmware_version = buf[13] * 100 + buf[14] * 10 + buf[15];
		pod->device_id = ((int)buf[8] << 16) | ((int)buf[9] << 8) |
				 (int) buf[10];
		if (pod->startup_progress == POD_STARTUP_VERSIONREQ) {
			pod->startup_progress = POD_STARTUP_SETUP;
			schedule_delayed_work(&line6->startup_work, 0);
		}
		return;
	}

	/* Only look for sysex messages from this device */
	if (buf[0] != (LINE6_SYSEX_BEGIN | LINE6_CHANNEL_DEVICE) &&
	    buf[0] != (LINE6_SYSEX_BEGIN | LINE6_CHANNEL_UNKNOWN)) {
		return;
	}
	if (memcmp(buf + 1, line6_midi_id, sizeof(line6_midi_id)) != 0)
		return;

	if (buf[5] == POD_SYSEX_SYSTEM && buf[6] == POD_MONITOR_LEVEL) {
		short value = ((int)buf[7] << 12) | ((int)buf[8] << 8) |
			      ((int)buf[9] << 4) | (int)buf[10];
		pod->monitor_level = value;
	}
}

/*
	Send system parameter (from integer).
*/
static int pod_set_system_param_int(struct usb_line6_pod *pod, int value,
				    int code)
{
	char *sysex;
	static const int size = 5;

	sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_SYSTEM, size);
	if (!sysex)
		return -ENOMEM;
	sysex[SYSEX_DATA_OFS] = code;
	sysex[SYSEX_DATA_OFS + 1] = (value >> 12) & 0x0f;
	sysex[SYSEX_DATA_OFS + 2] = (value >> 8) & 0x0f;
	sysex[SYSEX_DATA_OFS + 3] = (value >> 4) & 0x0f;
	sysex[SYSEX_DATA_OFS + 4] = (value) & 0x0f;
	line6_send_sysex_message(&pod->line6, sysex, size);
	kfree(sysex);
	return 0;
}

/*
	"read" request on "serial_number" special file.
*/
static ssize_t serial_number_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct snd_card *card = dev_to_snd_card(dev);
	struct usb_line6_pod *pod = card->private_data;

	return sysfs_emit(buf, "%u\n", pod->serial_number);
}

/*
	"read" request on "firmware_version" special file.
*/
static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct snd_card *card = dev_to_snd_card(dev);
	struct usb_line6_pod *pod = card->private_data;

	return sysfs_emit(buf, "%d.%02d\n", pod->firmware_version / 100,
			  pod->firmware_version % 100);
}

/*
	"read" request on "device_id" special file.
*/
static ssize_t device_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct snd_card *card = dev_to_snd_card(dev);
	struct usb_line6_pod *pod = card->private_data;

	return sysfs_emit(buf, "%d\n", pod->device_id);
}

/*
	POD startup procedure.
	This is a sequence of functions with special requirements (e.g., must
	not run immediately after initialization, must not run in interrupt
	context). After the last one has finished, the device is ready to use.
*/

static void pod_startup(struct usb_line6 *line6)
{
	struct usb_line6_pod *pod = line6_to_pod(line6);

	switch (pod->startup_progress) {
	case POD_STARTUP_VERSIONREQ:
		/* request firmware version: */
		line6_version_request_async(line6);
		break;
	case POD_STARTUP_SETUP:
		/* serial number: */
		line6_read_serial_number(&pod->line6, &pod->serial_number);

		/* ALSA audio interface: */
		if (snd_card_register(line6->card))
			dev_err(line6->ifcdev, "Failed to register POD card.\n");
		pod->startup_progress = POD_STARTUP_DONE;
		break;
	default:
		break;
	}
}

/* POD special files: */
static DEVICE_ATTR_RO(device_id);
static DEVICE_ATTR_RO(firmware_version);
static DEVICE_ATTR_RO(serial_number);

static struct attribute *pod_dev_attrs[] = {
	&dev_attr_device_id.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_serial_number.attr,
	NULL
};

static const struct attribute_group pod_dev_attr_group = {
	.name = "pod",
	.attrs = pod_dev_attrs,
};

/* control info callback */
static int snd_pod_control_monitor_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 65535;
	return 0;
}

/* control get callback */
static int snd_pod_control_monitor_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_pod *pod = line6_to_pod(line6pcm->line6);

	ucontrol->value.integer.value[0] = pod->monitor_level;
	return 0;
}

/* control put callback */
static int snd_pod_control_monitor_put(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_line6_pcm *line6pcm = snd_kcontrol_chip(kcontrol);
	struct usb_line6_pod *pod = line6_to_pod(line6pcm->line6);

	if (ucontrol->value.integer.value[0] == pod->monitor_level)
		return 0;

	pod->monitor_level = ucontrol->value.integer.value[0];
	pod_set_system_param_int(pod, ucontrol->value.integer.value[0],
				 POD_MONITOR_LEVEL);
	return 1;
}

/* control definition */
static const struct snd_kcontrol_new pod_control_monitor = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Monitor Playback Volume",
	.index = 0,
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = snd_pod_control_monitor_info,
	.get = snd_pod_control_monitor_get,
	.put = snd_pod_control_monitor_put
};

/*
	 Try to init POD device.
*/
static int pod_init(struct usb_line6 *line6,
		    const struct usb_device_id *id)
{
	int err;
	struct usb_line6_pod *pod = line6_to_pod(line6);

	line6->process_message = line6_pod_process_message;
	line6->startup = pod_startup;

	/* create sysfs entries: */
	err = snd_card_add_dev_attr(line6->card, &pod_dev_attr_group);
	if (err < 0)
		return err;

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &pod_pcm_properties);
	if (err < 0)
		return err;

	/* register monitor control: */
	err = snd_ctl_add(line6->card,
			  snd_ctl_new1(&pod_control_monitor, line6->line6pcm));
	if (err < 0)
		return err;

	/*
	   When the sound card is registered at this point, the PODxt Live
	   displays "Invalid Code Error 07", so we do it later in the event
	   handler.
	 */

	if (pod->line6.properties->capabilities & LINE6_CAP_CONTROL) {
		pod->monitor_level = POD_SYSTEM_INVALID;

		/* initiate startup procedure: */
		schedule_delayed_work(&line6->startup_work,
				      msecs_to_jiffies(POD_STARTUP_DELAY));
	}

	return 0;
}

#define LINE6_DEVICE(prod) USB_DEVICE(0x0e41, prod)
#define LINE6_IF_NUM(prod, n) USB_DEVICE_INTERFACE_NUMBER(0x0e41, prod, n)

/* table of devices that work with this driver */
static const struct usb_device_id pod_id_table[] = {
	{ LINE6_DEVICE(0x4250),    .driver_info = LINE6_BASSPODXT },
	{ LINE6_DEVICE(0x4642),    .driver_info = LINE6_BASSPODXTLIVE },
	{ LINE6_DEVICE(0x4252),    .driver_info = LINE6_BASSPODXTPRO },
	{ LINE6_IF_NUM(0x5051, 1), .driver_info = LINE6_POCKETPOD },
	{ LINE6_DEVICE(0x5044),    .driver_info = LINE6_PODXT },
	{ LINE6_IF_NUM(0x4650, 0), .driver_info = LINE6_PODXTLIVE_POD },
	{ LINE6_DEVICE(0x5050),    .driver_info = LINE6_PODXTPRO },
	{}
};

MODULE_DEVICE_TABLE(usb, pod_id_table);

static const struct line6_properties pod_properties_table[] = {
	[LINE6_BASSPODXT] = {
		.id = "BassPODxt",
		.name = "BassPODxt",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_BASSPODXTLIVE] = {
		.id = "BassPODxtLive",
		.name = "BassPODxt Live",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 1,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_BASSPODXTPRO] = {
		.id = "BassPODxtPro",
		.name = "BassPODxt Pro",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_POCKETPOD] = {
		.id = "PocketPOD",
		.name = "Pocket POD",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI,
		.altsetting = 0,
		.ep_ctrl_r = 0x82,
		.ep_ctrl_w = 0x02,
		/* no audio channel */
	},
	[LINE6_PODXT] = {
		.id = "PODxt",
		.name = "PODxt",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODXTLIVE_POD] = {
		.id = "PODxtLive",
		.name = "PODxt Live",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 1,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_PODXTPRO] = {
		.id = "PODxtPro",
		.name = "PODxt Pro",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI
				| LINE6_CAP_PCM
				| LINE6_CAP_HWMON,
		.altsetting = 5,
		.ep_ctrl_r = 0x84,
		.ep_ctrl_w = 0x03,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
};

/*
	Probe USB device.
*/
static int pod_probe(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	return line6_probe(interface, id, "Line6-POD",
			   &pod_properties_table[id->driver_info],
			   pod_init, sizeof(struct usb_line6_pod));
}

static struct usb_driver pod_driver = {
	.name = KBUILD_MODNAME,
	.probe = pod_probe,
	.disconnect = line6_disconnect,
#ifdef CONFIG_PM
	.suspend = line6_suspend,
	.resume = line6_resume,
	.reset_resume = line6_resume,
#endif
	.id_table = pod_id_table,
};

module_usb_driver(pod_driver);

MODULE_DESCRIPTION("Line 6 POD USB driver");
MODULE_LICENSE("GPL");
