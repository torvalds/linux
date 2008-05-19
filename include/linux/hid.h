#ifndef __HID_H
#define __HID_H

/*
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006-2007 Jiri Kosina
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
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

/*
 * USB HID (Human Interface Device) interface class code
 */

#define USB_INTERFACE_CLASS_HID		3

/*
 * USB HID interface subclass and protocol codes
 */

#define USB_INTERFACE_SUBCLASS_BOOT	1
#define USB_INTERFACE_PROTOCOL_KEYBOARD	1
#define USB_INTERFACE_PROTOCOL_MOUSE	2

/*
 * HID class requests
 */

#define HID_REQ_GET_REPORT		0x01
#define HID_REQ_GET_IDLE		0x02
#define HID_REQ_GET_PROTOCOL		0x03
#define HID_REQ_SET_REPORT		0x09
#define HID_REQ_SET_IDLE		0x0A
#define HID_REQ_SET_PROTOCOL		0x0B

/*
 * HID class descriptor types
 */

#define HID_DT_HID			(USB_TYPE_CLASS | 0x01)
#define HID_DT_REPORT			(USB_TYPE_CLASS | 0x02)
#define HID_DT_PHYSICAL			(USB_TYPE_CLASS | 0x03)

#define HID_MAX_DESCRIPTOR_SIZE		4096

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/input.h>

/*
 * We parse each description item into this structure. Short items data
 * values are expanded to 32-bit signed int, long items contain a pointer
 * into the data area.
 */

struct hid_item {
	unsigned  format;
	__u8      size;
	__u8      type;
	__u8      tag;
	union {
	    __u8   u8;
	    __s8   s8;
	    __u16  u16;
	    __s16  s16;
	    __u32  u32;
	    __s32  s32;
	    __u8  *longdata;
	} data;
};

/*
 * HID report item format
 */

#define HID_ITEM_FORMAT_SHORT	0
#define HID_ITEM_FORMAT_LONG	1

/*
 * Special tag indicating long items
 */

#define HID_ITEM_TAG_LONG	15

/*
 * HID report descriptor item type (prefix bit 2,3)
 */

#define HID_ITEM_TYPE_MAIN		0
#define HID_ITEM_TYPE_GLOBAL		1
#define HID_ITEM_TYPE_LOCAL		2
#define HID_ITEM_TYPE_RESERVED		3

/*
 * HID report descriptor main item tags
 */

#define HID_MAIN_ITEM_TAG_INPUT			8
#define HID_MAIN_ITEM_TAG_OUTPUT		9
#define HID_MAIN_ITEM_TAG_FEATURE		11
#define HID_MAIN_ITEM_TAG_BEGIN_COLLECTION	10
#define HID_MAIN_ITEM_TAG_END_COLLECTION	12

/*
 * HID report descriptor main item contents
 */

#define HID_MAIN_ITEM_CONSTANT		0x001
#define HID_MAIN_ITEM_VARIABLE		0x002
#define HID_MAIN_ITEM_RELATIVE		0x004
#define HID_MAIN_ITEM_WRAP		0x008
#define HID_MAIN_ITEM_NONLINEAR		0x010
#define HID_MAIN_ITEM_NO_PREFERRED	0x020
#define HID_MAIN_ITEM_NULL_STATE	0x040
#define HID_MAIN_ITEM_VOLATILE		0x080
#define HID_MAIN_ITEM_BUFFERED_BYTE	0x100

/*
 * HID report descriptor collection item types
 */

#define HID_COLLECTION_PHYSICAL		0
#define HID_COLLECTION_APPLICATION	1
#define HID_COLLECTION_LOGICAL		2

/*
 * HID report descriptor global item tags
 */

#define HID_GLOBAL_ITEM_TAG_USAGE_PAGE		0
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM	1
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM	2
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM	3
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM	4
#define HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT	5
#define HID_GLOBAL_ITEM_TAG_UNIT		6
#define HID_GLOBAL_ITEM_TAG_REPORT_SIZE		7
#define HID_GLOBAL_ITEM_TAG_REPORT_ID		8
#define HID_GLOBAL_ITEM_TAG_REPORT_COUNT	9
#define HID_GLOBAL_ITEM_TAG_PUSH		10
#define HID_GLOBAL_ITEM_TAG_POP			11

/*
 * HID report descriptor local item tags
 */

#define HID_LOCAL_ITEM_TAG_USAGE		0
#define HID_LOCAL_ITEM_TAG_USAGE_MINIMUM	1
#define HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM	2
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_INDEX	3
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_MINIMUM	4
#define HID_LOCAL_ITEM_TAG_DESIGNATOR_MAXIMUM	5
#define HID_LOCAL_ITEM_TAG_STRING_INDEX		7
#define HID_LOCAL_ITEM_TAG_STRING_MINIMUM	8
#define HID_LOCAL_ITEM_TAG_STRING_MAXIMUM	9
#define HID_LOCAL_ITEM_TAG_DELIMITER		10

/*
 * HID usage tables
 */

#define HID_USAGE_PAGE		0xffff0000

#define HID_UP_UNDEFINED	0x00000000
#define HID_UP_GENDESK		0x00010000
#define HID_UP_SIMULATION	0x00020000
#define HID_UP_KEYBOARD		0x00070000
#define HID_UP_LED		0x00080000
#define HID_UP_BUTTON		0x00090000
#define HID_UP_ORDINAL		0x000a0000
#define HID_UP_CONSUMER		0x000c0000
#define HID_UP_DIGITIZER	0x000d0000
#define HID_UP_PID		0x000f0000
#define HID_UP_HPVENDOR         0xff7f0000
#define HID_UP_MSVENDOR		0xff000000
#define HID_UP_CUSTOM		0x00ff0000
#define HID_UP_LOGIVENDOR	0xffbc0000

#define HID_USAGE		0x0000ffff

#define HID_GD_POINTER		0x00010001
#define HID_GD_MOUSE		0x00010002
#define HID_GD_JOYSTICK		0x00010004
#define HID_GD_GAMEPAD		0x00010005
#define HID_GD_KEYBOARD		0x00010006
#define HID_GD_KEYPAD		0x00010007
#define HID_GD_MULTIAXIS	0x00010008
#define HID_GD_X		0x00010030
#define HID_GD_Y		0x00010031
#define HID_GD_Z		0x00010032
#define HID_GD_RX		0x00010033
#define HID_GD_RY		0x00010034
#define HID_GD_RZ		0x00010035
#define HID_GD_SLIDER		0x00010036
#define HID_GD_DIAL		0x00010037
#define HID_GD_WHEEL		0x00010038
#define HID_GD_HATSWITCH	0x00010039
#define HID_GD_BUFFER		0x0001003a
#define HID_GD_BYTECOUNT	0x0001003b
#define HID_GD_MOTION		0x0001003c
#define HID_GD_START		0x0001003d
#define HID_GD_SELECT		0x0001003e
#define HID_GD_VX		0x00010040
#define HID_GD_VY		0x00010041
#define HID_GD_VZ		0x00010042
#define HID_GD_VBRX		0x00010043
#define HID_GD_VBRY		0x00010044
#define HID_GD_VBRZ		0x00010045
#define HID_GD_VNO		0x00010046
#define HID_GD_FEATURE		0x00010047
#define HID_GD_UP		0x00010090
#define HID_GD_DOWN		0x00010091
#define HID_GD_RIGHT		0x00010092
#define HID_GD_LEFT		0x00010093

/*
 * HID report types --- Ouch! HID spec says 1 2 3!
 */

#define HID_INPUT_REPORT	0
#define HID_OUTPUT_REPORT	1
#define HID_FEATURE_REPORT	2

/*
 * HID device quirks.
 */

/* 
 * Increase this if you need to configure more HID quirks at module load time
 */
#define MAX_USBHID_BOOT_QUIRKS 4

#define HID_QUIRK_INVERT			0x00000001
#define HID_QUIRK_NOTOUCH			0x00000002
#define HID_QUIRK_IGNORE			0x00000004
#define HID_QUIRK_NOGET				0x00000008
#define HID_QUIRK_HIDDEV			0x00000010
#define HID_QUIRK_BADPAD			0x00000020
#define HID_QUIRK_MULTI_INPUT			0x00000040
#define HID_QUIRK_2WHEEL_MOUSE_HACK_7		0x00000080
#define HID_QUIRK_2WHEEL_MOUSE_HACK_5		0x00000100
#define HID_QUIRK_2WHEEL_MOUSE_HACK_ON		0x00000200
#define HID_QUIRK_MIGHTYMOUSE			0x00000400
#define HID_QUIRK_APPLE_HAS_FN			0x00000800
#define HID_QUIRK_APPLE_FN_ON			0x00001000
#define HID_QUIRK_INVERT_HWHEEL			0x00002000
#define HID_QUIRK_APPLE_ISO_KEYBOARD		0x00004000
#define HID_QUIRK_BAD_RELATIVE_KEYS		0x00008000
#define HID_QUIRK_SKIP_OUTPUT_REPORTS		0x00010000
#define HID_QUIRK_IGNORE_MOUSE			0x00020000
#define HID_QUIRK_SONY_PS3_CONTROLLER		0x00040000
#define HID_QUIRK_DUPLICATE_USAGES		0x00080000
#define HID_QUIRK_RESET_LEDS			0x00100000
#define HID_QUIRK_HIDINPUT			0x00200000
#define HID_QUIRK_LOGITECH_IGNORE_DOUBLED_WHEEL	0x00400000
#define HID_QUIRK_LOGITECH_EXPANDED_KEYMAP	0x00800000
#define HID_QUIRK_IGNORE_HIDINPUT		0x01000000
#define HID_QUIRK_2WHEEL_MOUSE_HACK_B8		0x02000000
#define HID_QUIRK_HWHEEL_WHEEL_INVERT		0x04000000
#define HID_QUIRK_MICROSOFT_KEYS		0x08000000
#define HID_QUIRK_FULLSPEED_INTERVAL		0x10000000
#define HID_QUIRK_APPLE_NUMLOCK_EMULATION	0x20000000

/*
 * Separate quirks for runtime report descriptor fixup
 */

#define HID_QUIRK_RDESC_CYMOTION		0x00000001
#define HID_QUIRK_RDESC_LOGITECH		0x00000002
#define HID_QUIRK_RDESC_SWAPPED_MIN_MAX		0x00000004
#define HID_QUIRK_RDESC_PETALYNX		0x00000008
#define HID_QUIRK_RDESC_MACBOOK_JIS		0x00000010
#define HID_QUIRK_RDESC_BUTTON_CONSUMER		0x00000020
#define HID_QUIRK_RDESC_SAMSUNG_REMOTE		0x00000040
#define HID_QUIRK_RDESC_MICROSOFT_RECV_1028	0x00000080
#define HID_QUIRK_RDESC_SUNPLUS_WDESKTOP	0x00000100

/*
 * This is the global environment of the parser. This information is
 * persistent for main-items. The global environment can be saved and
 * restored with PUSH/POP statements.
 */

struct hid_global {
	unsigned usage_page;
	__s32    logical_minimum;
	__s32    logical_maximum;
	__s32    physical_minimum;
	__s32    physical_maximum;
	__s32    unit_exponent;
	unsigned unit;
	unsigned report_id;
	unsigned report_size;
	unsigned report_count;
};

/*
 * This is the local environment. It is persistent up the next main-item.
 */

#define HID_MAX_USAGES			12288
#define HID_DEFAULT_NUM_COLLECTIONS	16

struct hid_local {
	unsigned usage[HID_MAX_USAGES]; /* usage array */
	unsigned collection_index[HID_MAX_USAGES]; /* collection index array */
	unsigned usage_index;
	unsigned usage_minimum;
	unsigned delimiter_depth;
	unsigned delimiter_branch;
};

/*
 * This is the collection stack. We climb up the stack to determine
 * application and function of each field.
 */

struct hid_collection {
	unsigned type;
	unsigned usage;
	unsigned level;
};

struct hid_usage {
	unsigned  hid;			/* hid usage code */
	unsigned  collection_index;	/* index into collection array */
	/* hidinput data */
	__u16     code;			/* input driver code */
	__u8      type;			/* input driver type */
	__s8	  hat_min;		/* hat switch fun */
	__s8	  hat_max;		/* ditto */
	__s8	  hat_dir;		/* ditto */
};

struct hid_input;

struct hid_field {
	unsigned  physical;		/* physical usage for this field */
	unsigned  logical;		/* logical usage for this field */
	unsigned  application;		/* application usage for this field */
	struct hid_usage *usage;	/* usage table for this function */
	unsigned  maxusage;		/* maximum usage index */
	unsigned  flags;		/* main-item flags (i.e. volatile,array,constant) */
	unsigned  report_offset;	/* bit offset in the report */
	unsigned  report_size;		/* size of this field in the report */
	unsigned  report_count;		/* number of this field in the report */
	unsigned  report_type;		/* (input,output,feature) */
	__s32    *value;		/* last known value(s) */
	__s32     logical_minimum;
	__s32     logical_maximum;
	__s32     physical_minimum;
	__s32     physical_maximum;
	__s32     unit_exponent;
	unsigned  unit;
	struct hid_report *report;	/* associated report */
	unsigned index;			/* index into report->field[] */
	/* hidinput data */
	struct hid_input *hidinput;	/* associated input structure */
	__u16 dpad;			/* dpad input code */
};

#define HID_MAX_FIELDS 64

struct hid_report {
	struct list_head list;
	unsigned id;					/* id of this report */
	unsigned type;					/* report type */
	struct hid_field *field[HID_MAX_FIELDS];	/* fields of the report */
	unsigned maxfield;				/* maximum valid field index */
	unsigned size;					/* size of the report (bits) */
	struct hid_device *device;			/* associated device */
};

struct hid_report_enum {
	unsigned numbered;
	struct list_head report_list;
	struct hid_report *report_id_hash[256];
};

#define HID_REPORT_TYPES 3

#define HID_MIN_BUFFER_SIZE	64		/* make sure there is at least a packet size of space */
#define HID_MAX_BUFFER_SIZE	4096		/* 4kb */
#define HID_CONTROL_FIFO_SIZE	256		/* to init devices with >100 reports */
#define HID_OUTPUT_FIFO_SIZE	64

struct hid_control_fifo {
	unsigned char dir;
	struct hid_report *report;
};

#define HID_CLAIMED_INPUT	1
#define HID_CLAIMED_HIDDEV	2
#define HID_CLAIMED_HIDRAW	4

#define HID_CTRL_RUNNING	1
#define HID_OUT_RUNNING		2
#define HID_IN_RUNNING		3
#define HID_RESET_PENDING	4
#define HID_SUSPENDED		5
#define HID_CLEAR_HALT		6
#define HID_DISCONNECTED	7

struct hid_input {
	struct list_head list;
	struct hid_report *report;
	struct input_dev *input;
};

struct hid_device {							/* device report descriptor */
	 __u8 *rdesc;
	unsigned rsize;
	struct hid_collection *collection;				/* List of HID collections */
	unsigned collection_size;					/* Number of allocated hid_collections */
	unsigned maxcollection;						/* Number of parsed collections */
	unsigned maxapplication;					/* Number of applications */
	unsigned short bus;                                             /* BUS ID */
	unsigned short vendor;                                          /* Vendor ID */
	unsigned short product;                                         /* Product ID */
	unsigned version;						/* HID version */
	unsigned country;						/* HID country */
	struct hid_report_enum report_enum[HID_REPORT_TYPES];

	struct device *dev;						/* device */

	unsigned claimed;						/* Claimed by hidinput, hiddev? */
	unsigned quirks;						/* Various quirks the device can pull on us */

	struct list_head inputs;					/* The list of inputs */
	void *hiddev;							/* The hiddev structure */
	void *hidraw;
	int minor;							/* Hiddev minor number */

	int open;							/* is the device open by anyone? */
	char name[128];							/* Device name */
	char phys[64];							/* Device physical location */
	char uniq[64];							/* Device unique identifier (serial #) */

	void *driver_data;

	__s32 delayed_value;						/* For A4 Tech mice hwheel quirk */

	/* device-specific function pointers */
	int (*hidinput_input_event) (struct input_dev *, unsigned int, unsigned int, int);
	int (*hid_open) (struct hid_device *);
	void (*hid_close) (struct hid_device *);

	/* hiddev event handler */
	void (*hiddev_hid_event) (struct hid_device *, struct hid_field *field,
				  struct hid_usage *, __s32);
	void (*hiddev_report_event) (struct hid_device *, struct hid_report *);

	/* handler for raw output data, used by hidraw */
	int (*hid_output_raw_report) (struct hid_device *, __u8 *, size_t);
#ifdef CONFIG_USB_HIDINPUT_POWERBOOK
	unsigned long apple_pressed_fn[BITS_TO_LONGS(KEY_CNT)];
	unsigned long pb_pressed_numlock[BITS_TO_LONGS(KEY_CNT)];
#endif
};

#define HID_GLOBAL_STACK_SIZE 4
#define HID_COLLECTION_STACK_SIZE 4

struct hid_parser {
	struct hid_global     global;
	struct hid_global     global_stack[HID_GLOBAL_STACK_SIZE];
	unsigned              global_stack_ptr;
	struct hid_local      local;
	unsigned              collection_stack[HID_COLLECTION_STACK_SIZE];
	unsigned              collection_stack_ptr;
	struct hid_device    *device;
};

struct hid_class_descriptor {
	__u8  bDescriptorType;
	__le16 wDescriptorLength;
} __attribute__ ((packed));

struct hid_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__le16 bcdHID;
	__u8  bCountryCode;
	__u8  bNumDescriptors;

	struct hid_class_descriptor desc[1];
} __attribute__ ((packed));

/* Applications from HID Usage Tables 4/8/99 Version 1.1 */
/* We ignore a few input applications that are not widely used */
#define IS_INPUT_APPLICATION(a) (((a >= 0x00010000) && (a <= 0x00010008)) || (a == 0x00010080) || (a == 0x000c0001))

/* HID core API */

#ifdef CONFIG_HID_DEBUG
extern int hid_debug;
#endif

extern void hidinput_hid_event(struct hid_device *, struct hid_field *, struct hid_usage *, __s32);
extern void hidinput_report_event(struct hid_device *hid, struct hid_report *report);
extern int hidinput_connect(struct hid_device *);
extern void hidinput_disconnect(struct hid_device *);

int hid_set_field(struct hid_field *, unsigned, __s32);
int hid_input_report(struct hid_device *, int type, u8 *, int, int);
int hidinput_find_field(struct hid_device *hid, unsigned int type, unsigned int code, struct hid_field **field);
int hidinput_mapping_quirks(struct hid_usage *, struct input_dev *, unsigned long **, int *);
int hidinput_event_quirks(struct hid_device *, struct hid_field *, struct hid_usage *, __s32);
int hidinput_apple_event(struct hid_device *, struct input_dev *, struct hid_usage *, __s32);
void hid_output_report(struct hid_report *report, __u8 *data);
void hid_free_device(struct hid_device *device);
struct hid_device *hid_parse_report(__u8 *start, unsigned size);

/* HID quirks API */
u32 usbhid_lookup_quirk(const u16 idVendor, const u16 idProduct);
int usbhid_quirks_init(char **quirks_param);
void usbhid_quirks_exit(void);
void usbhid_fixup_report_descriptor(const u16, const u16, char *, unsigned, char **);

#ifdef CONFIG_HID_FF
int hid_ff_init(struct hid_device *hid);

int hid_lgff_init(struct hid_device *hid);
int hid_lg2ff_init(struct hid_device *hid);
int hid_plff_init(struct hid_device *hid);
int hid_tmff_init(struct hid_device *hid);
int hid_zpff_init(struct hid_device *hid);
#ifdef CONFIG_HID_PID
int hid_pidff_init(struct hid_device *hid);
#else
static inline int hid_pidff_init(struct hid_device *hid) { return -ENODEV; }
#endif

#else
static inline int hid_ff_init(struct hid_device *hid) { return -1; }
#endif

#ifdef CONFIG_HID_DEBUG
#define dbg_hid(format, arg...) if (hid_debug) \
				printk(KERN_DEBUG "%s: " format ,\
				__FILE__ , ## arg)
#define dbg_hid_line(format, arg...) if (hid_debug) \
				printk(format, ## arg)
#else
static inline int __attribute__((format(printf, 1, 2)))
dbg_hid(const char *fmt, ...)
{
	return 0;
}
#define dbg_hid_line dbg_hid
#endif

#define err_hid(format, arg...) printk(KERN_ERR "%s: " format "\n" , \
		__FILE__ , ## arg)
#endif
#endif

