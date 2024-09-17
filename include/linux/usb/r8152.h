/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 2020 Realtek Semiconductor Corp. All rights reserved.
 */

#ifndef	__LINUX_R8152_H
#define __LINUX_R8152_H

#define RTL8152_REQT_READ		0xc0
#define RTL8152_REQT_WRITE		0x40
#define RTL8152_REQ_GET_REGS		0x05
#define RTL8152_REQ_SET_REGS		0x05

#define BYTE_EN_DWORD			0xff
#define BYTE_EN_WORD			0x33
#define BYTE_EN_BYTE			0x11
#define BYTE_EN_SIX_BYTES		0x3f
#define BYTE_EN_START_MASK		0x0f
#define BYTE_EN_END_MASK		0xf0

#define MCU_TYPE_PLA			0x0100
#define MCU_TYPE_USB			0x0000

/* Define these values to match your device */
#define VENDOR_ID_REALTEK		0x0bda
#define VENDOR_ID_MICROSOFT		0x045e
#define VENDOR_ID_SAMSUNG		0x04e8
#define VENDOR_ID_LENOVO		0x17ef
#define VENDOR_ID_LINKSYS		0x13b1
#define VENDOR_ID_NVIDIA		0x0955
#define VENDOR_ID_TPLINK		0x2357
#define VENDOR_ID_DLINK			0x2001
#define VENDOR_ID_ASUS			0x0b05

#if IS_REACHABLE(CONFIG_USB_RTL8152)
extern u8 rtl8152_get_version(struct usb_interface *intf);
#endif

#endif /* __LINUX_R8152_H */
