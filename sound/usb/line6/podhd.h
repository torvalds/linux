/*
 * Line6 Pod HD
 *
 * Copyright (C) 2011 Stefan Hajnoczi <stefanha@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#ifndef PODHD_H
#define PODHD_H

#include <linux/usb.h>

#include "driver.h"

struct usb_line6_podhd {
	/**
		Generic Line6 USB data.
	*/
	struct usb_line6 line6;
};

extern int line6_podhd_init(struct usb_interface *interface,
			    struct usb_line6 *line6);

#endif /* PODHD_H */
