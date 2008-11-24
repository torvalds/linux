/*****************************************************************************
 *  File: drivers/usb/misc/vstusb.h
 *
 *  Purpose: Support for the bulk USB Vernier Spectrophotometers
 *
 *  Author:     EQware Engineering, Inc.
 *              Oregon City, OR, USA 97045
 *
 *  Copyright:  2007, 2008
 *              Vernier Software & Technology
 *              Beaverton, OR, USA 97005
 *
 *  Web:        www.vernier.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *****************************************************************************/
/*****************************************************************************
 *
 *  The vstusb module is a standard usb 'client' driver running on top of the
 *  standard usb host controller stack.
 *
 *  In general, vstusb supports standard bulk usb pipes.  It supports multiple
 *  devices and multiple pipes per device.
 *
 *  The vstusb driver supports two interfaces:
 *  1 - ioctl SEND_PIPE/RECV_PIPE - a general bulk write/read msg
 *  	interface to any pipe with timeout support;
 *  2 - standard read/write with ioctl config - offers standard read/write
 *  	interface with ioctl configured pipes and timeouts.
 *
 *  Both interfaces can be signal from other process and will abort its i/o
 *  operation.
 *
 *  A timeout of 0 means NO timeout.  The user can still terminate the read via
 *  signal.
 *
 *  If using multiple threads with this driver, the user should ensure that
 *  any reads, writes, or ioctls are complete before closing the device.
 *  Changing read/write timeouts or pipes takes effect on next read/write.
 *
 *****************************************************************************/

struct vstusb_args {
	union {
		/* this struct is used for IOCTL_VSTUSB_SEND_PIPE,	*
		 * IOCTL_VSTUSB_RECV_PIPE, and read()/write() fops	*/
		struct {
			void __user	*buffer;
			size_t          count;
			unsigned int    timeout_ms;
			int             pipe;
		};

		/* this one is used for IOCTL_VSTUSB_CONFIG_RW  	*/
		struct {
			int rd_pipe;
			int rd_timeout_ms;
			int wr_pipe;
			int wr_timeout_ms;
		};
	};
};

#define VST_IOC_MAGIC 'L'
#define VST_IOC_FIRST 0x20
#define IOCTL_VSTUSB_SEND_PIPE	_IO(VST_IOC_MAGIC, VST_IOC_FIRST)
#define IOCTL_VSTUSB_RECV_PIPE	_IO(VST_IOC_MAGIC, VST_IOC_FIRST + 1)
#define IOCTL_VSTUSB_CONFIG_RW	_IO(VST_IOC_MAGIC, VST_IOC_FIRST + 2)
