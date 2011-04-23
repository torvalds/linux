/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Firmware loader
 *
 * Currently not working for all devices. To be able to use the device
 * in linux, it is also possible to let the windows driver upload the firmware.
 * For that, start the computer in windows and reboot.
 * As long as the device is connected to the power supply, no firmware reload
 * needs to be performed.
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Version:	0.3.0
 * Copyright:	(C) Torsten Schenk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/firmware.h>

#include "firmware.h"
#include "chip.h"

MODULE_FIRMWARE("6fire/dmx6firel2.ihx");
MODULE_FIRMWARE("6fire/dmx6fireap.ihx");
MODULE_FIRMWARE("6fire/dmx6firecf.bin");

enum {
	FPGA_BUFSIZE = 512, FPGA_EP = 2
};

static const u8 BIT_REVERSE_TABLE[256] = {
	0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50,
	0xd0, 0x30, 0xb0, 0x70, 0xf0, 0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8,
	0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8, 0x04,
	0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4,
	0x34, 0xb4, 0x74, 0xf4, 0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c,
	0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc, 0x02, 0x82,
	0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32,
	0xb2, 0x72, 0xf2, 0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
	0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa, 0x06, 0x86, 0x46,
	0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6,
	0x76, 0xf6, 0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e,
	0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe, 0x01, 0x81, 0x41, 0xc1,
	0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71,
	0xf1, 0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99,
	0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9, 0x05, 0x85, 0x45, 0xc5, 0x25,
	0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
	0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d,
	0xdd, 0x3d, 0xbd, 0x7d, 0xfd, 0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3,
	0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3, 0x0b,
	0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb,
	0x3b, 0xbb, 0x7b, 0xfb, 0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67,
	0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7, 0x0f, 0x8f,
	0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f,
	0xbf, 0x7f, 0xff };

/*
 * wMaxPacketSize of pcm endpoints.
 * keep synced with rates_in_packet_size and rates_out_packet_size in pcm.c
 * fpp: frames per isopacket
 *
 * CAUTION: keep sizeof <= buffer[] in usb6fire_fw_init
 */
static const u8 ep_w_max_packet_size[] = {
	0xe4, 0x00, 0xe4, 0x00, /* alt 1: 228 EP2 and EP6 (7 fpp) */
	0xa4, 0x01, 0xa4, 0x01, /* alt 2: 420 EP2 and EP6 (13 fpp)*/
	0x94, 0x01, 0x5c, 0x02  /* alt 3: 404 EP2 and 604 EP6 (25 fpp) */
};

struct ihex_record {
	u16 address;
	u8 len;
	u8 data[256];
	char error; /* true if an error occurred parsing this record */

	u8 max_len; /* maximum record length in whole ihex */

	/* private */
	const char *txt_data;
	unsigned int txt_length;
	unsigned int txt_offset; /* current position in txt_data */
};

static u8 usb6fire_fw_ihex_nibble(const u8 n)
{
	if (n >= '0' && n <= '9')
		return n - '0';
	else if (n >= 'A' && n <= 'F')
		return n - ('A' - 10);
	else if (n >= 'a' && n <= 'f')
		return n - ('a' - 10);
	return 0;
}

static u8 usb6fire_fw_ihex_hex(const u8 *data, u8 *crc)
{
	u8 val = (usb6fire_fw_ihex_nibble(data[0]) << 4) |
			usb6fire_fw_ihex_nibble(data[1]);
	*crc += val;
	return val;
}

/*
 * returns true if record is available, false otherwise.
 * iff an error occurred, false will be returned and record->error will be true.
 */
static bool usb6fire_fw_ihex_next_record(struct ihex_record *record)
{
	u8 crc = 0;
	u8 type;
	int i;

	record->error = false;

	/* find begin of record (marked by a colon) */
	while (record->txt_offset < record->txt_length
			&& record->txt_data[record->txt_offset] != ':')
		record->txt_offset++;
	if (record->txt_offset == record->txt_length)
		return false;

	/* number of characters needed for len, addr and type entries */
	record->txt_offset++;
	if (record->txt_offset + 8 > record->txt_length) {
		record->error = true;
		return false;
	}

	record->len = usb6fire_fw_ihex_hex(record->txt_data +
			record->txt_offset, &crc);
	record->txt_offset += 2;
	record->address = usb6fire_fw_ihex_hex(record->txt_data +
			record->txt_offset, &crc) << 8;
	record->txt_offset += 2;
	record->address |= usb6fire_fw_ihex_hex(record->txt_data +
			record->txt_offset, &crc);
	record->txt_offset += 2;
	type = usb6fire_fw_ihex_hex(record->txt_data +
			record->txt_offset, &crc);
	record->txt_offset += 2;

	/* number of characters needed for data and crc entries */
	if (record->txt_offset + 2 * (record->len + 1) > record->txt_length) {
		record->error = true;
		return false;
	}
	for (i = 0; i < record->len; i++) {
		record->data[i] = usb6fire_fw_ihex_hex(record->txt_data
				+ record->txt_offset, &crc);
		record->txt_offset += 2;
	}
	usb6fire_fw_ihex_hex(record->txt_data + record->txt_offset, &crc);
	if (crc) {
		record->error = true;
		return false;
	}

	if (type == 1 || !record->len) /* eof */
		return false;
	else if (type == 0)
		return true;
	else {
		record->error = true;
		return false;
	}
}

static int usb6fire_fw_ihex_init(const struct firmware *fw,
		struct ihex_record *record)
{
	record->txt_data = fw->data;
	record->txt_length = fw->size;
	record->txt_offset = 0;
	record->max_len = 0;
	/* read all records, if loop ends, record->error indicates,
	 * whether ihex is valid. */
	while (usb6fire_fw_ihex_next_record(record))
		record->max_len = max(record->len, record->max_len);
	if (record->error)
		return -EINVAL;
	record->txt_offset = 0;
	return 0;
}

static int usb6fire_fw_ezusb_write(struct usb_device *device,
		int type, int value, char *data, int len)
{
	int ret;

	ret = usb_control_msg(device, usb_sndctrlpipe(device, 0), type,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			value, 0, data, len, HZ);
	if (ret < 0)
		return ret;
	else if (ret != len)
		return -EIO;
	return 0;
}

static int usb6fire_fw_ezusb_read(struct usb_device *device,
		int type, int value, char *data, int len)
{
	int ret = usb_control_msg(device, usb_rcvctrlpipe(device, 0), type,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, value,
			0, data, len, HZ);
	if (ret < 0)
		return ret;
	else if (ret != len)
		return -EIO;
	return 0;
}

static int usb6fire_fw_fpga_write(struct usb_device *device,
		char *data, int len)
{
	int actual_len;
	int ret;

	ret = usb_bulk_msg(device, usb_sndbulkpipe(device, FPGA_EP), data, len,
			&actual_len, HZ);
	if (ret < 0)
		return ret;
	else if (actual_len != len)
		return -EIO;
	return 0;
}

static int usb6fire_fw_ezusb_upload(
		struct usb_interface *intf, const char *fwname,
		unsigned int postaddr, u8 *postdata, unsigned int postlen)
{
	int ret;
	u8 data;
	struct usb_device *device = interface_to_usbdev(intf);
	const struct firmware *fw = 0;
	struct ihex_record *rec = kmalloc(sizeof(struct ihex_record),
			GFP_KERNEL);

	if (!rec)
		return -ENOMEM;

	ret = request_firmware(&fw, fwname, &device->dev);
	if (ret < 0) {
		kfree(rec);
		snd_printk(KERN_ERR PREFIX "error requesting ezusb "
				"firmware %s.\n", fwname);
		return ret;
	}
	ret = usb6fire_fw_ihex_init(fw, rec);
	if (ret < 0) {
		kfree(rec);
		snd_printk(KERN_ERR PREFIX "error validating ezusb "
				"firmware %s.\n", fwname);
		return ret;
	}
	/* upload firmware image */
	data = 0x01; /* stop ezusb cpu */
	ret = usb6fire_fw_ezusb_write(device, 0xa0, 0xe600, &data, 1);
	if (ret < 0) {
		kfree(rec);
		release_firmware(fw);
		snd_printk(KERN_ERR PREFIX "unable to upload ezusb "
				"firmware %s: begin message.\n", fwname);
		return ret;
	}

	while (usb6fire_fw_ihex_next_record(rec)) { /* write firmware */
		ret = usb6fire_fw_ezusb_write(device, 0xa0, rec->address,
				rec->data, rec->len);
		if (ret < 0) {
			kfree(rec);
			release_firmware(fw);
			snd_printk(KERN_ERR PREFIX "unable to upload ezusb "
					"firmware %s: data urb.\n", fwname);
			return ret;
		}
	}

	release_firmware(fw);
	kfree(rec);
	if (postdata) { /* write data after firmware has been uploaded */
		ret = usb6fire_fw_ezusb_write(device, 0xa0, postaddr,
				postdata, postlen);
		if (ret < 0) {
			snd_printk(KERN_ERR PREFIX "unable to upload ezusb "
					"firmware %s: post urb.\n", fwname);
			return ret;
		}
	}

	data = 0x00; /* resume ezusb cpu */
	ret = usb6fire_fw_ezusb_write(device, 0xa0, 0xe600, &data, 1);
	if (ret < 0) {
		release_firmware(fw);
		snd_printk(KERN_ERR PREFIX "unable to upload ezusb "
				"firmware %s: end message.\n", fwname);
		return ret;
	}
	return 0;
}

static int usb6fire_fw_fpga_upload(
		struct usb_interface *intf, const char *fwname)
{
	int ret;
	int i;
	struct usb_device *device = interface_to_usbdev(intf);
	u8 *buffer = kmalloc(FPGA_BUFSIZE, GFP_KERNEL);
	const char *c;
	const char *end;
	const struct firmware *fw;

	if (!buffer)
		return -ENOMEM;

	ret = request_firmware(&fw, fwname, &device->dev);
	if (ret < 0) {
		snd_printk(KERN_ERR PREFIX "unable to get fpga firmware %s.\n",
				fwname);
		kfree(buffer);
		return -EIO;
	}

	c = fw->data;
	end = fw->data + fw->size;

	ret = usb6fire_fw_ezusb_write(device, 8, 0, NULL, 0);
	if (ret < 0) {
		kfree(buffer);
		release_firmware(fw);
		snd_printk(KERN_ERR PREFIX "unable to upload fpga firmware: "
				"begin urb.\n");
		return ret;
	}

	while (c != end) {
		for (i = 0; c != end && i < FPGA_BUFSIZE; i++, c++)
			buffer[i] = BIT_REVERSE_TABLE[(u8) *c];

		ret = usb6fire_fw_fpga_write(device, buffer, i);
		if (ret < 0) {
			release_firmware(fw);
			kfree(buffer);
			snd_printk(KERN_ERR PREFIX "unable to upload fpga "
					"firmware: fw urb.\n");
			return ret;
		}
	}
	release_firmware(fw);
	kfree(buffer);

	ret = usb6fire_fw_ezusb_write(device, 9, 0, NULL, 0);
	if (ret < 0) {
		snd_printk(KERN_ERR PREFIX "unable to upload fpga firmware: "
				"end urb.\n");
		return ret;
	}
	return 0;
}

int usb6fire_fw_init(struct usb_interface *intf)
{
	int i;
	int ret;
	struct usb_device *device = interface_to_usbdev(intf);
	/* buffer: 8 receiving bytes from device and
	 * sizeof(EP_W_MAX_PACKET_SIZE) bytes for non-const copy */
	u8 buffer[12];

	ret = usb6fire_fw_ezusb_read(device, 1, 0, buffer, 8);
	if (ret < 0) {
		snd_printk(KERN_ERR PREFIX "unable to receive device "
				"firmware state.\n");
		return ret;
	}
	if (buffer[0] != 0xeb || buffer[1] != 0xaa || buffer[2] != 0x55
			|| buffer[4] != 0x03 || buffer[5] != 0x01 || buffer[7]
			!= 0x00) {
		snd_printk(KERN_ERR PREFIX "unknown device firmware state "
				"received from device: ");
		for (i = 0; i < 8; i++)
			snd_printk("%02x ", buffer[i]);
		snd_printk("\n");
		return -EIO;
	}
	/* do we need fpga loader ezusb firmware? */
	if (buffer[3] == 0x01 && buffer[6] == 0x19) {
		ret = usb6fire_fw_ezusb_upload(intf,
				"6fire/dmx6firel2.ihx", 0, NULL, 0);
		if (ret < 0)
			return ret;
		return FW_NOT_READY;
	}
	/* do we need fpga firmware and application ezusb firmware? */
	else if (buffer[3] == 0x02 && buffer[6] == 0x0b) {
		ret = usb6fire_fw_fpga_upload(intf, "6fire/dmx6firecf.bin");
		if (ret < 0)
			return ret;
		memcpy(buffer, ep_w_max_packet_size,
				sizeof(ep_w_max_packet_size));
		ret = usb6fire_fw_ezusb_upload(intf, "6fire/dmx6fireap.ihx",
				0x0003,	buffer, sizeof(ep_w_max_packet_size));
		if (ret < 0)
			return ret;
		return FW_NOT_READY;
	}
	/* all fw loaded? */
	else if (buffer[3] == 0x03 && buffer[6] == 0x0b)
		return 0;
	/* unknown data? */
	else {
		snd_printk(KERN_ERR PREFIX "unknown device firmware state "
				"received from device: ");
		for (i = 0; i < 8; i++)
			snd_printk("%02x ", buffer[i]);
		snd_printk("\n");
		return -EIO;
	}
	return 0;
}

