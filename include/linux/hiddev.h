/*
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */
#ifndef _HIDDEV_H
#define _HIDDEV_H

#include <uapi/linux/hiddev.h>


/*
 * In-kernel definitions.
 */

struct hiddev {
	int minor;
	int exist;
	int open;
	struct mutex existancelock;
	wait_queue_head_t wait;
	struct hid_device *hid;
	struct list_head list;
	spinlock_t list_lock;
	bool initialized;
};

struct hid_device;
struct hid_usage;
struct hid_field;
struct hid_report;

#ifdef CONFIG_USB_HIDDEV
int hiddev_connect(struct hid_device *hid, unsigned int force);
void hiddev_disconnect(struct hid_device *);
void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value);
void hiddev_report_event(struct hid_device *hid, struct hid_report *report);
#else
static inline int hiddev_connect(struct hid_device *hid,
		unsigned int force)
{ return -1; }
static inline void hiddev_disconnect(struct hid_device *hid) { }
static inline void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value) { }
static inline void hiddev_report_event(struct hid_device *hid, struct hid_report *report) { }
#endif

#endif
