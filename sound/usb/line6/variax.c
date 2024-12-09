// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (line6@grabner-graz.at)
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <sound/core.h>

#include "driver.h"

#define VARIAX_STARTUP_DELAY1 1000
#define VARIAX_STARTUP_DELAY3 100
#define VARIAX_STARTUP_DELAY4 100

/*
	Stages of Variax startup procedure
*/
enum {
	VARIAX_STARTUP_VERSIONREQ,
	VARIAX_STARTUP_ACTIVATE,
	VARIAX_STARTUP_SETUP,
};

enum {
	LINE6_PODXTLIVE_VARIAX,
	LINE6_VARIAX
};

struct usb_line6_variax {
	/* Generic Line 6 USB data */
	struct usb_line6 line6;

	/* Buffer for activation code */
	unsigned char *buffer_activate;

	/* Current progress in startup procedure */
	int startup_progress;
};

#define line6_to_variax(x)	container_of(x, struct usb_line6_variax, line6)

#define VARIAX_OFFSET_ACTIVATE 7

/*
	This message is sent by the device during initialization and identifies
	the connected guitar version.
*/
static const char variax_init_version[] = {
	0xf0, 0x7e, 0x7f, 0x06, 0x02, 0x00, 0x01, 0x0c,
	0x07, 0x00, 0x00, 0x00
};

/*
	This message is the last one sent by the device during initialization.
*/
static const char variax_init_done[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x6b
};

static const char variax_activate[] = {
	0xf0, 0x00, 0x01, 0x0c, 0x07, 0x00, 0x2a, 0x01,
	0xf7
};

static void variax_activate_async(struct usb_line6_variax *variax, int a)
{
	variax->buffer_activate[VARIAX_OFFSET_ACTIVATE] = a;
	line6_send_raw_message_async(&variax->line6, variax->buffer_activate,
				     sizeof(variax_activate));
}

/*
	Variax startup procedure.
	This is a sequence of functions with special requirements (e.g., must
	not run immediately after initialization, must not run in interrupt
	context). After the last one has finished, the device is ready to use.
*/

static void variax_startup(struct usb_line6 *line6)
{
	struct usb_line6_variax *variax = line6_to_variax(line6);

	switch (variax->startup_progress) {
	case VARIAX_STARTUP_VERSIONREQ:
		/* repeat request until getting the response */
		schedule_delayed_work(&line6->startup_work,
				      msecs_to_jiffies(VARIAX_STARTUP_DELAY1));
		/* request firmware version: */
		line6_version_request_async(line6);
		break;
	case VARIAX_STARTUP_ACTIVATE:
		/* activate device: */
		variax_activate_async(variax, 1);
		variax->startup_progress = VARIAX_STARTUP_SETUP;
		schedule_delayed_work(&line6->startup_work,
				      msecs_to_jiffies(VARIAX_STARTUP_DELAY4));
		break;
	case VARIAX_STARTUP_SETUP:
		/* ALSA audio interface: */
		snd_card_register(variax->line6.card);
		break;
	}
}

/*
	Process a completely received message.
*/
static void line6_variax_process_message(struct usb_line6 *line6)
{
	struct usb_line6_variax *variax = line6_to_variax(line6);
	const unsigned char *buf = variax->line6.buffer_message;

	switch (buf[0]) {
	case LINE6_RESET:
		dev_info(variax->line6.ifcdev, "VARIAX reset\n");
		break;

	case LINE6_SYSEX_BEGIN:
		if (memcmp(buf + 1, variax_init_version + 1,
			   sizeof(variax_init_version) - 1) == 0) {
			if (variax->startup_progress >= VARIAX_STARTUP_ACTIVATE)
				break;
			variax->startup_progress = VARIAX_STARTUP_ACTIVATE;
			cancel_delayed_work(&line6->startup_work);
			schedule_delayed_work(&line6->startup_work,
					      msecs_to_jiffies(VARIAX_STARTUP_DELAY3));
		} else if (memcmp(buf + 1, variax_init_done + 1,
				  sizeof(variax_init_done) - 1) == 0) {
			/* notify of complete initialization: */
			if (variax->startup_progress >= VARIAX_STARTUP_SETUP)
				break;
			cancel_delayed_work(&line6->startup_work);
			schedule_delayed_work(&line6->startup_work, 0);
		}
		break;
	}
}

/*
	Variax destructor.
*/
static void line6_variax_disconnect(struct usb_line6 *line6)
{
	struct usb_line6_variax *variax = line6_to_variax(line6);

	kfree(variax->buffer_activate);
}

/*
	 Try to init workbench device.
*/
static int variax_init(struct usb_line6 *line6,
		       const struct usb_device_id *id)
{
	struct usb_line6_variax *variax = line6_to_variax(line6);

	line6->process_message = line6_variax_process_message;
	line6->disconnect = line6_variax_disconnect;
	line6->startup = variax_startup;

	/* initialize USB buffers: */
	variax->buffer_activate = kmemdup(variax_activate,
					  sizeof(variax_activate), GFP_KERNEL);

	if (variax->buffer_activate == NULL)
		return -ENOMEM;

	/* initiate startup procedure: */
	schedule_delayed_work(&line6->startup_work,
			      msecs_to_jiffies(VARIAX_STARTUP_DELAY1));
	return 0;
}

#define LINE6_DEVICE(prod) USB_DEVICE(0x0e41, prod)
#define LINE6_IF_NUM(prod, n) USB_DEVICE_INTERFACE_NUMBER(0x0e41, prod, n)

/* table of devices that work with this driver */
static const struct usb_device_id variax_id_table[] = {
	{ LINE6_IF_NUM(0x4650, 1), .driver_info = LINE6_PODXTLIVE_VARIAX },
	{ LINE6_DEVICE(0x534d),    .driver_info = LINE6_VARIAX },
	{}
};

MODULE_DEVICE_TABLE(usb, variax_id_table);

static const struct line6_properties variax_properties_table[] = {
	[LINE6_PODXTLIVE_VARIAX] = {
		.id = "PODxtLive",
		.name = "PODxt Live",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI,
		.altsetting = 1,
		.ep_ctrl_r = 0x86,
		.ep_ctrl_w = 0x05,
		.ep_audio_r = 0x82,
		.ep_audio_w = 0x01,
	},
	[LINE6_VARIAX] = {
		.id = "Variax",
		.name = "Variax Workbench",
		.capabilities	= LINE6_CAP_CONTROL
				| LINE6_CAP_CONTROL_MIDI,
		.altsetting = 1,
		.ep_ctrl_r = 0x82,
		.ep_ctrl_w = 0x01,
		/* no audio channel */
	}
};

/*
	Probe USB device.
*/
static int variax_probe(struct usb_interface *interface,
			const struct usb_device_id *id)
{
	return line6_probe(interface, id, "Line6-Variax",
			   &variax_properties_table[id->driver_info],
			   variax_init, sizeof(struct usb_line6_variax));
}

static struct usb_driver variax_driver = {
	.name = KBUILD_MODNAME,
	.probe = variax_probe,
	.disconnect = line6_disconnect,
#ifdef CONFIG_PM
	.suspend = line6_suspend,
	.resume = line6_resume,
	.reset_resume = line6_resume,
#endif
	.id_table = variax_id_table,
};

module_usb_driver(variax_driver);

MODULE_DESCRIPTION("Variax Workbench USB driver");
MODULE_LICENSE("GPL");
