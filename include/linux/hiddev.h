#ifndef _HIDDEV_H
#define _HIDDEV_H

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

/*
 * The event structure itself
 */

struct hiddev_event {
	unsigned hid;
	signed int value;
};

struct hiddev_devinfo {
	__u32 bustype;
	__u32 busnum;
	__u32 devnum;
	__u32 ifnum;
	__s16 vendor;
	__s16 product;
	__s16 version;
	__u32 num_applications;
};

struct hiddev_collection_info {
	__u32 index;
	__u32 type;
	__u32 usage;
	__u32 level;
};

#define HID_STRING_SIZE 256
struct hiddev_string_descriptor {
	__s32 index;
	char value[HID_STRING_SIZE];
};

struct hiddev_report_info {
	__u32 report_type;
	__u32 report_id;
	__u32 num_fields;
};

/* To do a GUSAGE/SUSAGE, fill in at least usage_code,  report_type and 
 * report_id.  Set report_id to REPORT_ID_UNKNOWN if the rest of the fields 
 * are unknown.  Otherwise use a usage_ref struct filled in from a previous 
 * successful GUSAGE call to save time.  To actually send a value to the
 * device, perform a SUSAGE first, followed by a SREPORT.  An INITREPORT or a
 * GREPORT isn't necessary for a GUSAGE to return valid data.
 */
#define HID_REPORT_ID_UNKNOWN 0xffffffff
#define HID_REPORT_ID_FIRST   0x00000100
#define HID_REPORT_ID_NEXT    0x00000200
#define HID_REPORT_ID_MASK    0x000000ff
#define HID_REPORT_ID_MAX     0x000000ff

#define HID_REPORT_TYPE_INPUT	1
#define HID_REPORT_TYPE_OUTPUT	2
#define HID_REPORT_TYPE_FEATURE	3
#define HID_REPORT_TYPE_MIN     1
#define HID_REPORT_TYPE_MAX     3

struct hiddev_field_info {
	__u32 report_type;
	__u32 report_id;
	__u32 field_index;
	__u32 maxusage;
	__u32 flags;
	__u32 physical;		/* physical usage for this field */
	__u32 logical;		/* logical usage for this field */
	__u32 application;		/* application usage for this field */
	__s32 logical_minimum;
	__s32 logical_maximum;
	__s32 physical_minimum;
	__s32 physical_maximum;
	__u32 unit_exponent;
	__u32 unit;
};

/* Fill in report_type, report_id and field_index to get the information on a
 * field.
 */
#define HID_FIELD_CONSTANT		0x001
#define HID_FIELD_VARIABLE		0x002
#define HID_FIELD_RELATIVE		0x004
#define HID_FIELD_WRAP			0x008	
#define HID_FIELD_NONLINEAR		0x010
#define HID_FIELD_NO_PREFERRED		0x020
#define HID_FIELD_NULL_STATE		0x040
#define HID_FIELD_VOLATILE		0x080
#define HID_FIELD_BUFFERED_BYTE		0x100

struct hiddev_usage_ref {
	__u32 report_type;
	__u32 report_id;
	__u32 field_index;
	__u32 usage_index;
	__u32 usage_code;
	__s32 value;
};

/* hiddev_usage_ref_multi is used for sending multiple bytes to a control.
 * It really manifests itself as setting the value of consecutive usages */
#define HID_MAX_MULTI_USAGES 1024
struct hiddev_usage_ref_multi {
	struct hiddev_usage_ref uref;
	__u32 num_values;
	__s32 values[HID_MAX_MULTI_USAGES];
};

/* FIELD_INDEX_NONE is returned in read() data from the kernel when flags
 * is set to (HIDDEV_FLAG_UREF | HIDDEV_FLAG_REPORT) and a new report has
 * been sent by the device 
 */
#define HID_FIELD_INDEX_NONE 0xffffffff

/*
 * Protocol version.
 */

#define HID_VERSION		0x010004

/*
 * IOCTLs (0x00 - 0x7f)
 */

#define HIDIOCGVERSION		_IOR('H', 0x01, int)
#define HIDIOCAPPLICATION	_IO('H', 0x02)
#define HIDIOCGDEVINFO		_IOR('H', 0x03, struct hiddev_devinfo)
#define HIDIOCGSTRING		_IOR('H', 0x04, struct hiddev_string_descriptor)
#define HIDIOCINITREPORT	_IO('H', 0x05)
#define HIDIOCGNAME(len)	_IOC(_IOC_READ, 'H', 0x06, len)
#define HIDIOCGREPORT		_IOW('H', 0x07, struct hiddev_report_info)
#define HIDIOCSREPORT		_IOW('H', 0x08, struct hiddev_report_info)
#define HIDIOCGREPORTINFO	_IOWR('H', 0x09, struct hiddev_report_info)
#define HIDIOCGFIELDINFO	_IOWR('H', 0x0A, struct hiddev_field_info)
#define HIDIOCGUSAGE		_IOWR('H', 0x0B, struct hiddev_usage_ref)
#define HIDIOCSUSAGE		_IOW('H', 0x0C, struct hiddev_usage_ref)
#define HIDIOCGUCODE		_IOWR('H', 0x0D, struct hiddev_usage_ref)
#define HIDIOCGFLAG		_IOR('H', 0x0E, int)
#define HIDIOCSFLAG		_IOW('H', 0x0F, int)
#define HIDIOCGCOLLECTIONINDEX	_IOW('H', 0x10, struct hiddev_usage_ref)
#define HIDIOCGCOLLECTIONINFO	_IOWR('H', 0x11, struct hiddev_collection_info)
#define HIDIOCGPHYS(len)	_IOC(_IOC_READ, 'H', 0x12, len)

/* For writing/reading to multiple/consecutive usages */
#define HIDIOCGUSAGES		_IOWR('H', 0x13, struct hiddev_usage_ref_multi)
#define HIDIOCSUSAGES		_IOW('H', 0x14, struct hiddev_usage_ref_multi)

/* 
 * Flags to be used in HIDIOCSFLAG
 */
#define HIDDEV_FLAG_UREF	0x1
#define HIDDEV_FLAG_REPORT	0x2
#define HIDDEV_FLAGS		0x3

/* To traverse the input report descriptor info for a HID device, perform the 
 * following:
 *
 *  rinfo.report_type = HID_REPORT_TYPE_INPUT;
 *  rinfo.report_id = HID_REPORT_ID_FIRST;
 *  ret = ioctl(fd, HIDIOCGREPORTINFO, &rinfo);
 *
 *  while (ret >= 0) {
 *      for (i = 0; i < rinfo.num_fields; i++) { 
 *	    finfo.report_type = rinfo.report_type;
 *          finfo.report_id = rinfo.report_id;
 *          finfo.field_index = i;
 *          ioctl(fd, HIDIOCGFIELDINFO, &finfo);
 *          for (j = 0; j < finfo.maxusage; j++) {
 *              uref.field_index = i;
 *		uref.usage_index = j;
 *		ioctl(fd, HIDIOCGUCODE, &uref);
 *		ioctl(fd, HIDIOCGUSAGE, &uref);
 *          }
 *	}
 *	rinfo.report_id |= HID_REPORT_ID_NEXT;
 *	ret = ioctl(fd, HIDIOCGREPORTINFO, &rinfo);
 *  }
 */


#ifdef __KERNEL__

/*
 * In-kernel definitions.
 */

struct hid_device;
struct hid_usage;
struct hid_field;
struct hid_report;

#ifdef CONFIG_USB_HIDDEV
int hiddev_connect(struct hid_device *);
void hiddev_disconnect(struct hid_device *);
void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value);
void hiddev_report_event(struct hid_device *hid, struct hid_report *report);
int __init hiddev_init(void);
void hiddev_exit(void);
#else
static inline int hiddev_connect(struct hid_device *hid) { return -1; }
static inline void hiddev_disconnect(struct hid_device *hid) { }
static inline void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value) { }
static inline void hiddev_report_event(struct hid_device *hid, struct hid_report *report) { }
static inline int hiddev_init(void) { return 0; }
static inline void hiddev_exit(void) { }
#endif

#endif
#endif
