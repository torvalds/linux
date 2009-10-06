#ifndef __HID_DEBUG_H
#define __HID_DEBUG_H

/*
 *  Copyright (c) 2007-2009	Jiri Kosina
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
 */

#define HID_DEBUG_BUFSIZE 512

#ifdef CONFIG_DEBUG_FS

void hid_dump_input(struct hid_device *, struct hid_usage *, __s32);
void hid_dump_device(struct hid_device *, struct seq_file *);
void hid_dump_field(struct hid_field *, int, struct seq_file *);
char *hid_resolv_usage(unsigned, struct seq_file *);
void hid_debug_register(struct hid_device *, const char *);
void hid_debug_unregister(struct hid_device *);
void hid_debug_init(void);
void hid_debug_exit(void);
void hid_debug_event(struct hid_device *, char *);


struct hid_debug_list {
	char *hid_debug_buf;
	int head;
	int tail;
	struct fasync_struct *fasync;
	struct hid_device *hdev;
	struct list_head node;
	struct mutex read_mutex;
};

#else

#define hid_dump_input(a,b,c)		do { } while (0)
#define hid_dump_device(a,b)		do { } while (0)
#define hid_dump_field(a,b,c)		do { } while (0)
#define hid_resolv_usage(a,b)		do { } while (0)
#define hid_debug_register(a, b)	do { } while (0)
#define hid_debug_unregister(a)		do { } while (0)
#define hid_debug_init()		do { } while (0)
#define hid_debug_exit()		do { } while (0)
#define hid_debug_event(a,b)		do { } while (0)

#endif

#endif

