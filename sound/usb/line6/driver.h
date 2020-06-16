/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (grabner@icg.tugraz.at)
 */

#ifndef DRIVER_H
#define DRIVER_H

#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <sound/core.h>

#include "midi.h"

/* USB 1.1 speed configuration */
#define USB_LOW_INTERVALS_PER_SECOND 1000
#define USB_LOW_ISO_BUFFERS 2

/* USB 2.0+ speed configuration */
#define USB_HIGH_INTERVALS_PER_SECOND 8000
#define USB_HIGH_ISO_BUFFERS 16

/* Fallback USB interval and max packet size values */
#define LINE6_FALLBACK_INTERVAL 10
#define LINE6_FALLBACK_MAXPACKETSIZE 16

#define LINE6_TIMEOUT 1
#define LINE6_BUFSIZE_LISTEN 64
#define LINE6_MIDI_MESSAGE_MAXLEN 256

#define LINE6_RAW_MESSAGES_MAXCOUNT_ORDER 7
/* 4k packets are common, BUFSIZE * MAXCOUNT should be bigger... */
#define LINE6_RAW_MESSAGES_MAXCOUNT (1 << LINE6_RAW_MESSAGES_MAXCOUNT_ORDER)


#if LINE6_BUFSIZE_LISTEN > 65535
#error "Use dynamic fifo instead"
#endif

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

	unsigned int ctrl_if;
	unsigned int ep_ctrl_r;
	unsigned int ep_ctrl_w;
	unsigned int ep_audio_r;
	unsigned int ep_audio_w;
};

/* Capability bits */
enum {
	/* device supports settings parameter via USB */
	LINE6_CAP_CONTROL =	1 << 0,
	/* device supports PCM input/output via USB */
	LINE6_CAP_PCM =		1 << 1,
	/* device supports hardware monitoring */
	LINE6_CAP_HWMON =	1 << 2,
	/* device requires output data when input is read */
	LINE6_CAP_IN_NEEDS_OUT = 1 << 3,
	/* device uses raw MIDI via USB (data endpoints) */
	LINE6_CAP_CONTROL_MIDI = 1 << 4,
	/* device provides low-level information */
	LINE6_CAP_CONTROL_INFO = 1 << 5,
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

	/* Interval for data USB packets */
	int interval;
	/* ...for isochronous transfers framing */
	int intervals_per_second;

	/* Number of isochronous URBs used for frame transfers */
	int iso_buffers;

	/* Maximum size of data USB packet */
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

	/* URB for listening to POD data endpoint */
	struct urb *urb_listen;

	/* Buffer for incoming data from POD data endpoint */
	unsigned char *buffer_listen;

	/* Buffer for message to be processed, generated from MIDI layer */
	unsigned char *buffer_message;

	/* Length of message to be processed, generated from MIDI layer  */
	int message_length;

	/* Circular buffer for non-MIDI control messages */
	struct {
		struct mutex read_lock;
		wait_queue_head_t wait_queue;
		unsigned int active:1;
		unsigned int nonblock:1;
		STRUCT_KFIFO_REC_2(LINE6_BUFSIZE_LISTEN * LINE6_RAW_MESSAGES_MAXCOUNT)
			fifo;
	} messages;

	/* Work for delayed PCM startup */
	struct delayed_work startup_work;

	/* If MIDI is supported, buffer_message contains the pre-processed data;
	 * otherwise the data is only in urb_listen (buffer_incoming).
	 */
	void (*process_message)(struct usb_line6 *);
	void (*disconnect)(struct usb_line6 *line6);
	void (*startup)(struct usb_line6 *line6);
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
