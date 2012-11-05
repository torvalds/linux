/*
 *  include/linux/mfd/viperboard.h
 *
 *  Nano River Technologies viperboard definitions
 *
 *  (C) 2012 by Lemonage GmbH
 *  Author: Lars Poeschel <poeschel@lemonage.de>
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __MFD_VIPERBOARD_H__
#define __MFD_VIPERBOARD_H__

#include <linux/types.h>
#include <linux/usb.h>

#define VPRBRD_EP_OUT               0x02
#define VPRBRD_EP_IN                0x86

#define VPRBRD_I2C_MSG_LEN          512 /* max length of a msg on USB level */

#define VPRBRD_I2C_FREQ_6MHZ        1                        /*   6 MBit/s */
#define VPRBRD_I2C_FREQ_3MHZ        2                        /*   3 MBit/s */
#define VPRBRD_I2C_FREQ_1MHZ        3                        /*   1 MBit/s */
#define VPRBRD_I2C_FREQ_FAST        4                        /* 400 kbit/s */
#define VPRBRD_I2C_FREQ_400KHZ      VPRBRD_I2C_FREQ_FAST
#define VPRBRD_I2C_FREQ_200KHZ      5                        /* 200 kbit/s */
#define VPRBRD_I2C_FREQ_STD         6                        /* 100 kbit/s */
#define VPRBRD_I2C_FREQ_100KHZ      VPRBRD_I2C_FREQ_STD
#define VPRBRD_I2C_FREQ_10KHZ       7                        /*  10 kbit/s */

#define VPRBRD_I2C_CMD_WRITE        0x00
#define VPRBRD_I2C_CMD_READ         0x01
#define VPRBRD_I2C_CMD_ADDR         0x02

#define VPRBRD_USB_TYPE_OUT	    0x40
#define VPRBRD_USB_TYPE_IN	    0xc0
#define VPRBRD_USB_TIMEOUT_MS       100
#define VPRBRD_USB_REQUEST_MAJOR    0xea
#define VPRBRD_USB_REQUEST_MINOR    0xeb

struct vprbrd_i2c_write_hdr {
	u8 cmd;
	u16 addr;
	u8 len1;
	u8 len2;
	u8 last;
	u8 chan;
	u16 spi;
} __packed;

struct vprbrd_i2c_read_hdr {
	u8 cmd;
	u16 addr;
	u8 len0;
	u8 len1;
	u8 len2;
	u8 len3;
	u8 len4;
	u8 len5;
	u16 tf1;                        /* transfer 1 length */
	u16 tf2;                        /* transfer 2 length */
} __packed;

struct vprbrd_i2c_status {
	u8 unknown[11];
	u8 status;
} __packed;

struct vprbrd_i2c_write_msg {
	struct vprbrd_i2c_write_hdr header;
	u8 data[VPRBRD_I2C_MSG_LEN
		- sizeof(struct vprbrd_i2c_write_hdr)];
} __packed;

struct vprbrd_i2c_read_msg {
	struct vprbrd_i2c_read_hdr header;
	u8 data[VPRBRD_I2C_MSG_LEN
		- sizeof(struct vprbrd_i2c_read_hdr)];
} __packed;

struct vprbrd_i2c_addr_msg {
	u8 cmd;
	u8 addr;
	u8 unknown1;
	u16 len;
	u8 unknown2;
	u8 unknown3;
} __packed;

/* Structure to hold all device specific stuff */
struct vprbrd {
	struct usb_device *usb_dev; /* the usb device for this device */
	struct mutex lock;
	u8 buf[sizeof(struct vprbrd_i2c_write_msg)];
	struct platform_device pdev;
};

#endif /* __MFD_VIPERBOARD_H__ */
