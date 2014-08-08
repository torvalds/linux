/*
 * include/linux/keycombo.h - platform data structure for keycombo driver
 *
 * Copyright (C) 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_KEYCOMBO_H
#define _LINUX_KEYCOMBO_H

#define KEYCOMBO_NAME "keycombo"

/*
 * if key_down_fn and key_up_fn are both present, you are guaranteed that
 * key_down_fn will return before key_up_fn is called, and that key_up_fn
 * is called iff key_down_fn is called.
 */
struct keycombo_platform_data {
	void (*key_down_fn)(void *);
	void (*key_up_fn)(void *);
	void *priv;
	int key_down_delay; /* Time in ms */
	int *keys_up;
	int keys_down[]; /* 0 terminated */
};

#endif /* _LINUX_KEYCOMBO_H */
