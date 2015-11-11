/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef DRIVER_H
#define DRIVER_H

#include <linux/spinlock.h>
#include <linux/usb.h>
#include <sound/core.h>

#include "midi.h"

#define USB_INTERVALS_PER_SECOND 1000

/* Fallback USB interval and max packet size values */
#define LINE6_FALLBACK_INTERVAL 10
#define LINE6_FALLBACK_MAXPACKETSIZE 16

#define LINE6_TIMEOUT 1
#define LINE6_BUFSIZE_LISTEN 32
#define LINE6_MESSAGE_MAXLEN 256

/*
	Line 6 MIDI control commands
*/
#define LINE6_PARAM_CHANGE   0xb0
#define LINE6_PROGRAM_CHANGE 0xc0
#define LINE6_SYSEX_BEGIN    0xf0
#define LINE6_SYSEX_END      0xf7
#define LINE6_RESET          0xff

/*
	MIDI channel for messages initiated by the host
	(and eventually echoed back by the device)
*/
#define LINE6_CHANNEL_HOST   0x00

/*
	MIDI channel for messages initiated by the device
*/
#define LINE6_CHANNEL_DEVICE 0x02

#define LINE6_CHANNEL_UNKNOWN 5	/* don't know yet what this is good for */

#define LINE6_CHANNEL_MASK 0x0f

#define CHECK_STARTUP_PROGRESS(x, n)	\
do {					\
	if ((x) >= (n))			\
		return;			\
	x = (n);			\
} while (0)

extern const unsigned char line6_midi_id[3];

static const int SYSEX_DATA_OFS = sizeof(line6_midi_id) + 3;
static const int SYSEX_EXTRA_SIZE = sizeof(line6_midi_id) + 4;

/*
	 Common properties of Line 6 devices.
*/
struct line6_properties {
	/* Card id string (maximum 16 characters).
	 * This can be used to address the device in ALSA programs as
	 * "default:CARD=<id>"
	 */
	const char *id;

	/* Card short name (maximum 32 characters) */
	const char *name;

	/* Bit vector defining this device's capabilities in line6usb driver */
	int capabilities;

	int altsetting;

	unsigned ep_ctrl_r;
	unsigned ep_ctrl_w;
	unsigned ep_audio_r;
	unsigned ep_audio_w;
};

/* Capability bits */
enum {
	/* device supports settings parameter via USB */
	LINE6_CAP_CONTROL =	1 << 0,
	/* device supports PCM input/output via USB */
	LINE6_CAP_PCM =		1 << 1,
	/* device support hardware monitoring */
	LINE6_CAP_HWMON =	1 << 2,
};

/*
	 Common data shared by all Line 6 devices.
	 Corresponds to a pair of USB endpoints.
*/
struct usb_line6 {
	/* USB device */
	struct usb_device *usbdev;

	/* Properties */
	const struct line6_properties *properties;

	/* Interval (ms) */
	int interval;

	/* Maximum size of USB packet */
	int max_packet_size;

	/* Device representing the USB interface */
	struct device *ifcdev;

	/* Line 6 sound card data structure.
	 * Each device has at least MIDI or PCM.
	 */
	struct snd_card *card;

	/* Line 6 PCM device data structure */
	struct snd_line6_pcm *line6pcm;

	/* Line 6 MIDI device data structure */
	struct snd_line6_midi *line6midi;

	/* URB for listening to PODxt Pro control endpoint */
	struct urb *urb_listen;

	/* Buffer for listening to PODxt Pro control endpoint */
	unsigned char *buffer_listen;

	/* Buffer for message to be processed */
	unsigned char *buffer_message;

	/* Length of message to be processed */
	int message_length;

	void (*process_message)(struct usb_line6 *);
	void (*disconnect)(struct usb_line6 *line6);
};

extern char *line6_alloc_sysex_buffer(struct usb_line6 *line6, int code1,
				      int code2, int size);
extern int line6_read_data(struct usb_line6 *line6, unsigned address,
			   void *data, unsigned datalen);
extern int line6_read_serial_number(struct usb_line6 *line6,
				    u32 *serial_number);
extern int line6_send_raw_message_async(struct usb_line6 *line6,
					const char *buffer, int size);
extern int line6_send_sysex_message(struct usb_line6 *line6,
				    const char *buffer, int size);
extern ssize_t line6_set_raw(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count);
extern void line6_start_timer(struct timer_list *timer, unsigned long msecs,
			      void (*function)(unsigned long),
			      unsigned long data);
extern int line6_version_request_async(struct usb_line6 *line6);
extern int line6_write_data(struct usb_line6 *line6, unsigned address,
			    void *data, unsigned datalen);

int line6_probe(struct usb_interface *interface,
		const struct usb_device_id *id,
		const char *driver_name,
		const struct line6_properties *properties,
		int (*private_init)(struct usb_line6 *, const struct usb_device_id *id),
		size_t data_size);

void line6_disconnect(struct usb_interface *interface);

#ifdef CONFIG_PM
int line6_suspend(struct usb_interface *interface, pm_message_t message);
int line6_resume(struct usb_interface *interface);
#endif

#endif
