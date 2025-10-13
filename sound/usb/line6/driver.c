// SPDX-License-Identifier: GPL-2.0-only
/*
 * Line 6 Linux USB driver
 *
 * Copyright (C) 2004-2010 Markus Grabner (line6@grabner-graz.at)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/hwdep.h>

#include "capture.h"
#include "driver.h"
#include "midi.h"
#include "playback.h"

#define DRIVER_AUTHOR  "Markus Grabner <line6@grabner-graz.at>"
#define DRIVER_DESC    "Line 6 USB Driver"

/*
	This is Line 6's MIDI manufacturer ID.
*/
const unsigned char line6_midi_id[3] = {
	0x00, 0x01, 0x0c
};
EXPORT_SYMBOL_GPL(line6_midi_id);

/*
	Code to request version of POD, Variax interface
	(and maybe other devices).
*/
static const char line6_request_version[] = {
	0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7
};

/*
	 Class for asynchronous messages.
*/
struct message {
	struct usb_line6 *line6;
	const char *buffer;
	int size;
	int done;
};

/*
	Forward declarations.
*/
static void line6_data_received(struct urb *urb);
static int line6_send_raw_message_async_part(struct message *msg,
					     struct urb *urb);

/*
	Start to listen on endpoint.
*/
static int line6_start_listen(struct usb_line6 *line6)
{
	int err;

	if (line6->properties->capabilities & LINE6_CAP_CONTROL_MIDI) {
		usb_fill_int_urb(line6->urb_listen, line6->usbdev,
			usb_rcvintpipe(line6->usbdev, line6->properties->ep_ctrl_r),
			line6->buffer_listen, LINE6_BUFSIZE_LISTEN,
			line6_data_received, line6, line6->interval);
	} else {
		usb_fill_bulk_urb(line6->urb_listen, line6->usbdev,
			usb_rcvbulkpipe(line6->usbdev, line6->properties->ep_ctrl_r),
			line6->buffer_listen, LINE6_BUFSIZE_LISTEN,
			line6_data_received, line6);
	}

	/* sanity checks of EP before actually submitting */
	if (usb_urb_ep_type_check(line6->urb_listen)) {
		dev_err(line6->ifcdev, "invalid control EP\n");
		return -EINVAL;
	}

	line6->urb_listen->actual_length = 0;
	err = usb_submit_urb(line6->urb_listen, GFP_ATOMIC);
	return err;
}

/*
	Stop listening on endpoint.
*/
static void line6_stop_listen(struct usb_line6 *line6)
{
	usb_kill_urb(line6->urb_listen);
}

/*
	Send raw message in pieces of wMaxPacketSize bytes.
*/
int line6_send_raw_message(struct usb_line6 *line6, const char *buffer,
				  int size)
{
	int i, done = 0;
	const struct line6_properties *properties = line6->properties;

	for (i = 0; i < size; i += line6->max_packet_size) {
		int partial;
		const char *frag_buf = buffer + i;
		int frag_size = min(line6->max_packet_size, size - i);
		int retval;

		if (properties->capabilities & LINE6_CAP_CONTROL_MIDI) {
			retval = usb_interrupt_msg(line6->usbdev,
						usb_sndintpipe(line6->usbdev, properties->ep_ctrl_w),
						(char *)frag_buf, frag_size,
						&partial, LINE6_TIMEOUT);
		} else {
			retval = usb_bulk_msg(line6->usbdev,
						usb_sndbulkpipe(line6->usbdev, properties->ep_ctrl_w),
						(char *)frag_buf, frag_size,
						&partial, LINE6_TIMEOUT);
		}

		if (retval) {
			dev_err(line6->ifcdev,
				"usb_bulk_msg failed (%d)\n", retval);
			break;
		}

		done += frag_size;
	}

	return done;
}
EXPORT_SYMBOL_GPL(line6_send_raw_message);

/*
	Notification of completion of asynchronous request transmission.
*/
static void line6_async_request_sent(struct urb *urb)
{
	struct message *msg = (struct message *)urb->context;

	if (msg->done >= msg->size) {
		usb_free_urb(urb);
		kfree(msg);
	} else
		line6_send_raw_message_async_part(msg, urb);
}

/*
	Asynchronously send part of a raw message.
*/
static int line6_send_raw_message_async_part(struct message *msg,
					     struct urb *urb)
{
	int retval;
	struct usb_line6 *line6 = msg->line6;
	int done = msg->done;
	int bytes = min(msg->size - done, line6->max_packet_size);

	if (line6->properties->capabilities & LINE6_CAP_CONTROL_MIDI) {
		usb_fill_int_urb(urb, line6->usbdev,
			usb_sndintpipe(line6->usbdev, line6->properties->ep_ctrl_w),
			(char *)msg->buffer + done, bytes,
			line6_async_request_sent, msg, line6->interval);
	} else {
		usb_fill_bulk_urb(urb, line6->usbdev,
			usb_sndbulkpipe(line6->usbdev, line6->properties->ep_ctrl_w),
			(char *)msg->buffer + done, bytes,
			line6_async_request_sent, msg);
	}

	msg->done += bytes;

	/* sanity checks of EP before actually submitting */
	retval = usb_urb_ep_type_check(urb);
	if (retval < 0)
		goto error;

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval < 0)
		goto error;

	return 0;

 error:
	dev_err(line6->ifcdev, "%s: usb_submit_urb failed (%d)\n",
		__func__, retval);
	usb_free_urb(urb);
	kfree(msg);
	return retval;
}

/*
	Asynchronously send raw message.
*/
int line6_send_raw_message_async(struct usb_line6 *line6, const char *buffer,
				 int size)
{
	struct message *msg;
	struct urb *urb;

	/* create message: */
	msg = kzalloc(sizeof(struct message), GFP_ATOMIC);
	if (msg == NULL)
		return -ENOMEM;

	/* create URB: */
	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (urb == NULL) {
		kfree(msg);
		return -ENOMEM;
	}

	/* set message data: */
	msg->line6 = line6;
	msg->buffer = buffer;
	msg->size = size;
	msg->done = 0;

	/* start sending: */
	return line6_send_raw_message_async_part(msg, urb);
}
EXPORT_SYMBOL_GPL(line6_send_raw_message_async);

/*
	Send asynchronous device version request.
*/
int line6_version_request_async(struct usb_line6 *line6)
{
	char *buffer;
	int retval;

	buffer = kmemdup(line6_request_version,
			sizeof(line6_request_version), GFP_ATOMIC);
	if (buffer == NULL)
		return -ENOMEM;

	retval = line6_send_raw_message_async(line6, buffer,
					      sizeof(line6_request_version));
	kfree(buffer);
	return retval;
}
EXPORT_SYMBOL_GPL(line6_version_request_async);

/*
	Send sysex message in pieces of wMaxPacketSize bytes.
*/
int line6_send_sysex_message(struct usb_line6 *line6, const char *buffer,
			     int size)
{
	return line6_send_raw_message(line6, buffer,
				      size + SYSEX_EXTRA_SIZE) -
	    SYSEX_EXTRA_SIZE;
}
EXPORT_SYMBOL_GPL(line6_send_sysex_message);

/*
	Allocate buffer for sysex message and prepare header.
	@param code sysex message code
	@param size number of bytes between code and sysex end
*/
char *line6_alloc_sysex_buffer(struct usb_line6 *line6, int code1, int code2,
			       int size)
{
	char *buffer = kmalloc(size + SYSEX_EXTRA_SIZE, GFP_ATOMIC);

	if (!buffer)
		return NULL;

	buffer[0] = LINE6_SYSEX_BEGIN;
	memcpy(buffer + 1, line6_midi_id, sizeof(line6_midi_id));
	buffer[sizeof(line6_midi_id) + 1] = code1;
	buffer[sizeof(line6_midi_id) + 2] = code2;
	buffer[sizeof(line6_midi_id) + 3 + size] = LINE6_SYSEX_END;
	return buffer;
}
EXPORT_SYMBOL_GPL(line6_alloc_sysex_buffer);

/*
	Notification of data received from the Line 6 device.
*/
static void line6_data_received(struct urb *urb)
{
	struct usb_line6 *line6 = (struct usb_line6 *)urb->context;
	struct midi_buffer *mb = &line6->line6midi->midibuf_in;
	int done;

	if (urb->status == -ESHUTDOWN)
		return;

	if (line6->properties->capabilities & LINE6_CAP_CONTROL_MIDI) {
		scoped_guard(spinlock_irqsave, &line6->line6midi->lock) {
			done =
				line6_midibuf_write(mb, urb->transfer_buffer, urb->actual_length);

			if (done < urb->actual_length) {
				line6_midibuf_ignore(mb, done);
				dev_dbg(line6->ifcdev, "%d %d buffer overflow - message skipped\n",
					done, urb->actual_length);
			}
		}

		for (;;) {
			scoped_guard(spinlock_irqsave, &line6->line6midi->lock) {
				done =
					line6_midibuf_read(mb, line6->buffer_message,
							   LINE6_MIDI_MESSAGE_MAXLEN,
							   LINE6_MIDIBUF_READ_RX);
			}

			if (done <= 0)
				break;

			line6->message_length = done;
			line6_midi_receive(line6, line6->buffer_message, done);

			if (line6->process_message)
				line6->process_message(line6);
		}
	} else {
		line6->buffer_message = urb->transfer_buffer;
		line6->message_length = urb->actual_length;
		if (line6->process_message)
			line6->process_message(line6);
		line6->buffer_message = NULL;
	}

	line6_start_listen(line6);
}

#define LINE6_READ_WRITE_STATUS_DELAY 2  /* milliseconds */
#define LINE6_READ_WRITE_MAX_RETRIES 50

/*
	Read data from device.
*/
int line6_read_data(struct usb_line6 *line6, unsigned address, void *data,
		    unsigned datalen)
{
	struct usb_device *usbdev = line6->usbdev;
	int ret;
	u8 len;
	unsigned count;

	if (address > 0xffff || datalen > 0xff)
		return -EINVAL;

	/* query the serial number: */
	ret = usb_control_msg_send(usbdev, 0, 0x67,
				   USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
				   (datalen << 8) | 0x21, address, NULL, 0,
				   LINE6_TIMEOUT, GFP_KERNEL);
	if (ret) {
		dev_err(line6->ifcdev, "read request failed (error %d)\n", ret);
		goto exit;
	}

	/* Wait for data length. We'll get 0xff until length arrives. */
	for (count = 0; count < LINE6_READ_WRITE_MAX_RETRIES; count++) {
		mdelay(LINE6_READ_WRITE_STATUS_DELAY);

		ret = usb_control_msg_recv(usbdev, 0, 0x67,
					   USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
					   0x0012, 0x0000, &len, 1,
					   LINE6_TIMEOUT, GFP_KERNEL);
		if (ret) {
			dev_err(line6->ifcdev,
				"receive length failed (error %d)\n", ret);
			goto exit;
		}

		if (len != 0xff)
			break;
	}

	ret = -EIO;
	if (len == 0xff) {
		dev_err(line6->ifcdev, "read failed after %d retries\n",
			count);
		goto exit;
	} else if (len != datalen) {
		/* should be equal or something went wrong */
		dev_err(line6->ifcdev,
			"length mismatch (expected %d, got %d)\n",
			(int)datalen, len);
		goto exit;
	}

	/* receive the result: */
	ret = usb_control_msg_recv(usbdev, 0, 0x67,
				   USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
				   0x0013, 0x0000, data, datalen, LINE6_TIMEOUT,
				   GFP_KERNEL);
	if (ret)
		dev_err(line6->ifcdev, "read failed (error %d)\n", ret);

exit:
	return ret;
}
EXPORT_SYMBOL_GPL(line6_read_data);

/*
	Write data to device.
*/
int line6_write_data(struct usb_line6 *line6, unsigned address, void *data,
		     unsigned datalen)
{
	struct usb_device *usbdev = line6->usbdev;
	int ret;
	unsigned char *status;
	int count;

	if (address > 0xffff || datalen > 0xffff)
		return -EINVAL;

	status = kmalloc(1, GFP_KERNEL);
	if (!status)
		return -ENOMEM;

	ret = usb_control_msg_send(usbdev, 0, 0x67,
				   USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
				   0x0022, address, data, datalen, LINE6_TIMEOUT,
				   GFP_KERNEL);
	if (ret) {
		dev_err(line6->ifcdev,
			"write request failed (error %d)\n", ret);
		goto exit;
	}

	for (count = 0; count < LINE6_READ_WRITE_MAX_RETRIES; count++) {
		mdelay(LINE6_READ_WRITE_STATUS_DELAY);

		ret = usb_control_msg_recv(usbdev, 0, 0x67,
					   USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
					   0x0012, 0x0000, status, 1, LINE6_TIMEOUT,
					   GFP_KERNEL);
		if (ret) {
			dev_err(line6->ifcdev,
				"receiving status failed (error %d)\n", ret);
			goto exit;
		}

		if (*status != 0xff)
			break;
	}

	if (*status == 0xff) {
		dev_err(line6->ifcdev, "write failed after %d retries\n",
			count);
		ret = -EIO;
	} else if (*status != 0) {
		dev_err(line6->ifcdev, "write failed (error %d)\n", ret);
		ret = -EIO;
	}
exit:
	kfree(status);
	return ret;
}
EXPORT_SYMBOL_GPL(line6_write_data);

/*
	Read Line 6 device serial number.
	(POD, TonePort, GuitarPort)
*/
int line6_read_serial_number(struct usb_line6 *line6, u32 *serial_number)
{
	return line6_read_data(line6, 0x80d0, serial_number,
			       sizeof(*serial_number));
}
EXPORT_SYMBOL_GPL(line6_read_serial_number);

/*
	Card destructor.
*/
static void line6_destruct(struct snd_card *card)
{
	struct usb_line6 *line6 = card->private_data;
	struct usb_device *usbdev = line6->usbdev;

	/* Free buffer memory first. We cannot depend on the existence of private
	 * data from the (podhd) module, it may be gone already during this call
	 */
	kfree(line6->buffer_message);

	kfree(line6->buffer_listen);

	/* then free URBs: */
	usb_free_urb(line6->urb_listen);
	line6->urb_listen = NULL;

	/* decrement reference counters: */
	usb_put_dev(usbdev);
}

static void line6_get_usb_properties(struct usb_line6 *line6)
{
	struct usb_device *usbdev = line6->usbdev;
	const struct line6_properties *properties = line6->properties;
	int pipe;
	struct usb_host_endpoint *ep = NULL;

	if (properties->capabilities & LINE6_CAP_CONTROL) {
		if (properties->capabilities & LINE6_CAP_CONTROL_MIDI) {
			pipe = usb_rcvintpipe(line6->usbdev,
				line6->properties->ep_ctrl_r);
		} else {
			pipe = usb_rcvbulkpipe(line6->usbdev,
				line6->properties->ep_ctrl_r);
		}
		ep = usbdev->ep_in[usb_pipeendpoint(pipe)];
	}

	/* Control data transfer properties */
	if (ep) {
		line6->interval = ep->desc.bInterval;
		line6->max_packet_size = le16_to_cpu(ep->desc.wMaxPacketSize);
	} else {
		if (properties->capabilities & LINE6_CAP_CONTROL) {
			dev_err(line6->ifcdev,
				"endpoint not available, using fallback values");
		}
		line6->interval = LINE6_FALLBACK_INTERVAL;
		line6->max_packet_size = LINE6_FALLBACK_MAXPACKETSIZE;
	}

	/* Isochronous transfer properties */
	if (usbdev->speed == USB_SPEED_LOW) {
		line6->intervals_per_second = USB_LOW_INTERVALS_PER_SECOND;
		line6->iso_buffers = USB_LOW_ISO_BUFFERS;
	} else {
		line6->intervals_per_second = USB_HIGH_INTERVALS_PER_SECOND;
		line6->iso_buffers = USB_HIGH_ISO_BUFFERS;
	}
}

/* Enable buffering of incoming messages, flush the buffer */
static int line6_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	struct usb_line6 *line6 = hw->private_data;

	/* NOTE: hwdep layer provides atomicity here */

	line6->messages.active = 1;
	line6->messages.nonblock = file->f_flags & O_NONBLOCK ? 1 : 0;

	return 0;
}

/* Stop buffering */
static int line6_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	struct usb_line6 *line6 = hw->private_data;

	line6->messages.active = 0;

	return 0;
}

/* Read from circular buffer, return to user */
static long
line6_hwdep_read(struct snd_hwdep *hwdep, char __user *buf, long count,
					loff_t *offset)
{
	struct usb_line6 *line6 = hwdep->private_data;
	long rv = 0;
	unsigned int out_count;

	if (mutex_lock_interruptible(&line6->messages.read_lock))
		return -ERESTARTSYS;

	while (kfifo_len(&line6->messages.fifo) == 0) {
		mutex_unlock(&line6->messages.read_lock);

		if (line6->messages.nonblock)
			return -EAGAIN;

		rv = wait_event_interruptible(
			line6->messages.wait_queue,
			kfifo_len(&line6->messages.fifo) != 0);
		if (rv < 0)
			return rv;

		if (mutex_lock_interruptible(&line6->messages.read_lock))
			return -ERESTARTSYS;
	}

	if (kfifo_peek_len(&line6->messages.fifo) > count) {
		/* Buffer too small; allow re-read of the current item... */
		rv = -EINVAL;
	} else {
		rv = kfifo_to_user(&line6->messages.fifo, buf, count, &out_count);
		if (rv == 0)
			rv = out_count;
	}

	mutex_unlock(&line6->messages.read_lock);
	return rv;
}

/* Write directly (no buffering) to device by user*/
static long
line6_hwdep_write(struct snd_hwdep *hwdep, const char __user *data, long count,
					loff_t *offset)
{
	struct usb_line6 *line6 = hwdep->private_data;
	int rv;
	char *data_copy;

	if (count > line6->max_packet_size * LINE6_RAW_MESSAGES_MAXCOUNT) {
		/* This is an arbitrary limit - still better than nothing... */
		return -EINVAL;
	}

	data_copy = memdup_user(data, count);
	if (IS_ERR(data_copy))
		return PTR_ERR(data_copy);

	rv = line6_send_raw_message(line6, data_copy, count);

	kfree(data_copy);
	return rv;
}

static __poll_t
line6_hwdep_poll(struct snd_hwdep *hwdep, struct file *file, poll_table *wait)
{
	struct usb_line6 *line6 = hwdep->private_data;

	poll_wait(file, &line6->messages.wait_queue, wait);

	guard(mutex)(&line6->messages.read_lock);
	return kfifo_len(&line6->messages.fifo) == 0 ? 0 : EPOLLIN | EPOLLRDNORM;
}

static const struct snd_hwdep_ops hwdep_ops = {
	.open    = line6_hwdep_open,
	.release = line6_hwdep_release,
	.read    = line6_hwdep_read,
	.write   = line6_hwdep_write,
	.poll    = line6_hwdep_poll,
};

/* Insert into circular buffer */
static void line6_hwdep_push_message(struct usb_line6 *line6)
{
	if (!line6->messages.active)
		return;

	if (kfifo_avail(&line6->messages.fifo) >= line6->message_length) {
		/* No race condition here, there's only one writer */
		kfifo_in(&line6->messages.fifo,
			line6->buffer_message, line6->message_length);
	} /* else TODO: signal overflow */

	wake_up_interruptible(&line6->messages.wait_queue);
}

static int line6_hwdep_init(struct usb_line6 *line6)
{
	int err;
	struct snd_hwdep *hwdep;

	/* TODO: usb_driver_claim_interface(); */
	line6->process_message = line6_hwdep_push_message;
	line6->messages.active = 0;
	init_waitqueue_head(&line6->messages.wait_queue);
	mutex_init(&line6->messages.read_lock);
	INIT_KFIFO(line6->messages.fifo);

	err = snd_hwdep_new(line6->card, "config", 0, &hwdep);
	if (err < 0)
		goto end;
	strscpy(hwdep->name, "config");
	hwdep->iface = SNDRV_HWDEP_IFACE_LINE6;
	hwdep->ops = hwdep_ops;
	hwdep->private_data = line6;
	hwdep->exclusive = true;

end:
	return err;
}

static int line6_init_cap_control(struct usb_line6 *line6)
{
	int ret;

	/* initialize USB buffers: */
	line6->buffer_listen = kzalloc(LINE6_BUFSIZE_LISTEN, GFP_KERNEL);
	if (!line6->buffer_listen)
		return -ENOMEM;

	line6->urb_listen = usb_alloc_urb(0, GFP_KERNEL);
	if (!line6->urb_listen)
		return -ENOMEM;

	if (line6->properties->capabilities & LINE6_CAP_CONTROL_MIDI) {
		line6->buffer_message = kzalloc(LINE6_MIDI_MESSAGE_MAXLEN, GFP_KERNEL);
		if (!line6->buffer_message)
			return -ENOMEM;

		ret = line6_init_midi(line6);
		if (ret < 0)
			return ret;
	} else {
		ret = line6_hwdep_init(line6);
		if (ret < 0)
			return ret;
	}

	ret = line6_start_listen(line6);
	if (ret < 0) {
		dev_err(line6->ifcdev, "cannot start listening: %d\n", ret);
		return ret;
	}

	return 0;
}

static void line6_startup_work(struct work_struct *work)
{
	struct usb_line6 *line6 =
		container_of(work, struct usb_line6, startup_work.work);

	if (line6->startup)
		line6->startup(line6);
}

/*
	Probe USB device.
*/
int line6_probe(struct usb_interface *interface,
		const struct usb_device_id *id,
		const char *driver_name,
		const struct line6_properties *properties,
		int (*private_init)(struct usb_line6 *, const struct usb_device_id *id),
		size_t data_size)
{
	struct usb_device *usbdev = interface_to_usbdev(interface);
	struct snd_card *card;
	struct usb_line6 *line6;
	int interface_number;
	int ret;

	if (WARN_ON(data_size < sizeof(*line6)))
		return -EINVAL;

	/* we don't handle multiple configurations */
	if (usbdev->descriptor.bNumConfigurations != 1)
		return -ENODEV;

	ret = snd_card_new(&interface->dev,
			   SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			   THIS_MODULE, data_size, &card);
	if (ret < 0)
		return ret;

	/* store basic data: */
	line6 = card->private_data;
	line6->card = card;
	line6->properties = properties;
	line6->usbdev = usbdev;
	line6->ifcdev = &interface->dev;
	INIT_DELAYED_WORK(&line6->startup_work, line6_startup_work);

	strscpy(card->id, properties->id);
	strscpy(card->driver, driver_name);
	strscpy(card->shortname, properties->name);
	sprintf(card->longname, "Line 6 %s at USB %s", properties->name,
		dev_name(line6->ifcdev));
	card->private_free = line6_destruct;

	usb_set_intfdata(interface, line6);

	/* increment reference counters: */
	usb_get_dev(usbdev);

	/* initialize device info: */
	dev_info(&interface->dev, "Line 6 %s found\n", properties->name);

	/* query interface number */
	interface_number = interface->cur_altsetting->desc.bInterfaceNumber;

	/* TODO reserves the bus bandwidth even without actual transfer */
	ret = usb_set_interface(usbdev, interface_number,
				properties->altsetting);
	if (ret < 0) {
		dev_err(&interface->dev, "set_interface failed\n");
		goto error;
	}

	line6_get_usb_properties(line6);

	if (properties->capabilities & LINE6_CAP_CONTROL) {
		ret = line6_init_cap_control(line6);
		if (ret < 0)
			goto error;
	}

	/* initialize device data based on device: */
	ret = private_init(line6, id);
	if (ret < 0)
		goto error;

	/* creation of additional special files should go here */

	dev_info(&interface->dev, "Line 6 %s now attached\n",
		 properties->name);

	return 0;

 error:
	/* we can call disconnect callback here because no close-sync is
	 * needed yet at this point
	 */
	line6_disconnect(interface);
	return ret;
}
EXPORT_SYMBOL_GPL(line6_probe);

/*
	Line 6 device disconnected.
*/
void line6_disconnect(struct usb_interface *interface)
{
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	struct usb_device *usbdev = interface_to_usbdev(interface);

	if (!line6)
		return;

	if (WARN_ON(usbdev != line6->usbdev))
		return;

	cancel_delayed_work_sync(&line6->startup_work);

	if (line6->urb_listen != NULL)
		line6_stop_listen(line6);

	snd_card_disconnect(line6->card);
	if (line6->line6pcm)
		line6_pcm_disconnect(line6->line6pcm);
	if (line6->disconnect)
		line6->disconnect(line6);

	dev_info(&interface->dev, "Line 6 %s now disconnected\n",
		 line6->properties->name);

	/* make sure the device isn't destructed twice: */
	usb_set_intfdata(interface, NULL);

	snd_card_free_when_closed(line6->card);
}
EXPORT_SYMBOL_GPL(line6_disconnect);

#ifdef CONFIG_PM

/*
	Suspend Line 6 device.
*/
int line6_suspend(struct usb_interface *interface, pm_message_t message)
{
	struct usb_line6 *line6 = usb_get_intfdata(interface);
	struct snd_line6_pcm *line6pcm = line6->line6pcm;

	snd_power_change_state(line6->card, SNDRV_CTL_POWER_D3hot);

	if (line6->properties->capabilities & LINE6_CAP_CONTROL)
		line6_stop_listen(line6);

	if (line6pcm != NULL)
		line6pcm->flags = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(line6_suspend);

/*
	Resume Line 6 device.
*/
int line6_resume(struct usb_interface *interface)
{
	struct usb_line6 *line6 = usb_get_intfdata(interface);

	if (line6->properties->capabilities & LINE6_CAP_CONTROL)
		line6_start_listen(line6);

	snd_power_change_state(line6->card, SNDRV_CTL_POWER_D0);
	return 0;
}
EXPORT_SYMBOL_GPL(line6_resume);

#endif /* CONFIG_PM */

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

