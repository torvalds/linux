/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Userspace driver support for the LED subsystem
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _UAPI__ULEDS_H_
#define _UAPI__ULEDS_H_

#define LED_MAX_NAME_SIZE	64

struct uleds_user_dev {
	char name[LED_MAX_NAME_SIZE];
	int max_brightness;
};

#endif /* _UAPI__ULEDS_H_ */
