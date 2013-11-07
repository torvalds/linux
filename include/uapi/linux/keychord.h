/*
 *  Key chord input driver
 *
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
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

#ifndef _UAPI_LINUX_KEYCHORD_H_
#define _UAPI_LINUX_KEYCHORD_H_

#include <linux/input.h>

#define KEYCHORD_VERSION		1

/*
 * One or more input_keychord structs are written to /dev/keychord
 * at once to specify the list of keychords to monitor.
 * Reading /dev/keychord returns the id of a keychord when the
 * keychord combination is pressed.  A keychord is signalled when
 * all of the keys in the keycode list are in the pressed state.
 * The order in which the keys are pressed does not matter.
 * The keychord will not be signalled if keys not in the keycode
 * list are pressed.
 * Keychords will not be signalled on key release events.
 */
struct input_keychord {
	/* should be KEYCHORD_VERSION */
	__u16 version;
	/*
	 * client specified ID, returned from read()
	 * when this keychord is pressed.
	 */
	__u16 id;

	/* number of keycodes in this keychord */
	__u16 count;

	/* variable length array of keycodes */
	__u16 keycodes[];
};

#endif	/* _UAPI_LINUX_KEYCHORD_H_ */
