/*
 * Copyright (C) 2010 - 2011 Goodix, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef 	_LINUX_GOODIX_GT8110_TOUCH_H
#define		_LINUX_GOODIX_GT8110_TOUCH_H

struct goodix_8110_platform_data {
	uint32_t version;	/* Use this entry for panels with */
	int reset;
	int irq_pin;
        int power_control;
        int mode_check_pin;
        int (*hw_init) (void);
        int (*hw_exit) (void);
};

#endif /* _LINUX_GOODIX_TOUCH_H */

