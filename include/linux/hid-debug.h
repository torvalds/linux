#ifndef __HID_DEBUG_H
#define __HID_DEBUG_H

/*
 *  Copyright (c) 2007	Jiri Kosina
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

#ifdef CONFIG_HID_DEBUG

void hid_dump_input(struct hid_usage *, __s32);
void hid_dump_device(struct hid_device *);
void hid_dump_field(struct hid_field *, int);
void hid_resolv_usage(unsigned);
void hid_resolv_event(__u8, __u16);

#else

#define hid_dump_input(a,b)     do { } while (0)
#define hid_dump_device(c)      do { } while (0)
#define hid_dump_field(a,b)     do { } while (0)
#define hid_resolv_usage(a)         do { } while (0)
#define hid_resolv_event(a,b)       do { } while (0)

#endif /* CONFIG_HID_DEBUG */


#endif

