/*
 * Copyright (C) 2010 Samsung Electronics
 * Minkyu Kang <mk7.kang@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _FSA9480_H_
#define _FSA9480_H_

#define FSA9480_ATTACHED	1
#define FSA9480_DETACHED	0

struct fsa9480_platform_data {
	void (*cfg_gpio) (void);
	void (*usb_cb) (u8 attached);
	void (*uart_cb) (u8 attached);
	void (*charger_cb) (u8 attached);
	void (*jig_cb) (u8 attached);
	void (*reset_cb) (void);
	void (*usb_power) (u8 on);
	int wakeup;
};

#endif /* _FSA9480_H_ */
