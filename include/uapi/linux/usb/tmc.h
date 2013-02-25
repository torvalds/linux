/*
 * Copyright (C) 2007 Stefan Kopp, Gechingen, Germany
 * Copyright (C) 2008 Novell, Inc.
 * Copyright (C) 2008 Greg Kroah-Hartman <gregkh@suse.de>
 *
 * This file holds USB constants defined by the USB Device Class
 * Definition for Test and Measurement devices published by the USB-IF.
 *
 * It also has the ioctl definitions for the usbtmc kernel driver that
 * userspace needs to know about.
 */

#ifndef __LINUX_USB_TMC_H
#define __LINUX_USB_TMC_H

/* USB TMC status values */
#define USBTMC_STATUS_SUCCESS				0x01
#define USBTMC_STATUS_PENDING				0x02
#define USBTMC_STATUS_FAILED				0x80
#define USBTMC_STATUS_TRANSFER_NOT_IN_PROGRESS		0x81
#define USBTMC_STATUS_SPLIT_NOT_IN_PROGRESS		0x82
#define USBTMC_STATUS_SPLIT_IN_PROGRESS			0x83

/* USB TMC requests values */
#define USBTMC_REQUEST_INITIATE_ABORT_BULK_OUT		1
#define USBTMC_REQUEST_CHECK_ABORT_BULK_OUT_STATUS	2
#define USBTMC_REQUEST_INITIATE_ABORT_BULK_IN		3
#define USBTMC_REQUEST_CHECK_ABORT_BULK_IN_STATUS	4
#define USBTMC_REQUEST_INITIATE_CLEAR			5
#define USBTMC_REQUEST_CHECK_CLEAR_STATUS		6
#define USBTMC_REQUEST_GET_CAPABILITIES			7
#define USBTMC_REQUEST_INDICATOR_PULSE			64

/* Request values for USBTMC driver's ioctl entry point */
#define USBTMC_IOC_NR			91
#define USBTMC_IOCTL_INDICATOR_PULSE	_IO(USBTMC_IOC_NR, 1)
#define USBTMC_IOCTL_CLEAR		_IO(USBTMC_IOC_NR, 2)
#define USBTMC_IOCTL_ABORT_BULK_OUT	_IO(USBTMC_IOC_NR, 3)
#define USBTMC_IOCTL_ABORT_BULK_IN	_IO(USBTMC_IOC_NR, 4)
#define USBTMC_IOCTL_CLEAR_OUT_HALT	_IO(USBTMC_IOC_NR, 6)
#define USBTMC_IOCTL_CLEAR_IN_HALT	_IO(USBTMC_IOC_NR, 7)

#endif
