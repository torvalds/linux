/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2006-2007 Jiri Kosina
 */
/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */
#ifndef __HID_H
#define __HID_H


#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h> /* hid_device_id */
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <uapi/linux/hid.h>
#include <linux/hid_bpf.h>

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
#define HID_COLLECTION_NAMED_ARRAY	4

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
#define HID_UP_GENDEVCTRLS	0x00060000
#define HID_UP_KEYBOARD		0x00070000
#define HID_UP_LED		0x00080000
#define HID_UP_BUTTON		0x00090000
#define HID_UP_ORDINAL		0x000a0000
#define HID_UP_TELEPHONY	0x000b0000
#define HID_UP_CONSUMER		0x000c0000
#define HID_UP_DIGITIZER	0x000d0000
#define HID_UP_PID		0x000f0000
#define HID_UP_BATTERY		0x00850000
#define HID_UP_CAMERA		0x00900000
#define HID_UP_HPVENDOR         0xff7f0000
#define HID_UP_HPVENDOR2        0xff010000
#define HID_UP_MSVENDOR		0xff000000
#define HID_UP_CUSTOM		0x00ff0000
#define HID_UP_LOGIVENDOR	0xffbc0000
#define HID_UP_LOGIVENDOR2   0xff090000
#define HID_UP_LOGIVENDOR3   0xff430000
#define HID_UP_LNVENDOR		0xffa00000
#define HID_UP_SENSOR		0x00200000
#define HID_UP_ASUSVENDOR	0xff310000
#define HID_UP_GOOGLEVENDOR	0xffd10000

#define HID_USAGE		0x0000ffff

#define HID_GD_POINTER		0x00010001
#define HID_GD_MOUSE		0x00010002
#define HID_GD_JOYSTICK		0x00010004
#define HID_GD_GAMEPAD		0x00010005
#define HID_GD_KEYBOARD		0x00010006
#define HID_GD_KEYPAD		0x00010007
#define HID_GD_MULTIAXIS	0x00010008
/*
 * Microsoft Win8 Wireless Radio Controls extensions CA, see:
 * http://www.usb.org/developers/hidpage/HUTRR40RadioHIDUsagesFinal.pdf
 */
#define HID_GD_WIRELESS_RADIO_CTLS	0x0001000c
/*
 * System Multi-Axis, see:
 * http://www.usb.org/developers/hidpage/HUTRR62_-_Generic_Desktop_CA_for_System_Multi-Axis_Controllers.txt
 */
#define HID_GD_SYSTEM_MULTIAXIS	0x0001000e

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
#define HID_GD_RESOLUTION_MULTIPLIER	0x00010048
#define HID_GD_SYSTEM_CONTROL	0x00010080
#define HID_GD_UP		0x00010090
#define HID_GD_DOWN		0x00010091
#define HID_GD_RIGHT		0x00010092
#define HID_GD_LEFT		0x00010093
/* Microsoft Win8 Wireless Radio Controls CA usage codes */
#define HID_GD_RFKILL_BTN	0x000100c6
#define HID_GD_RFKILL_LED	0x000100c7
#define HID_GD_RFKILL_SWITCH	0x000100c8

#define HID_DC_BATTERYSTRENGTH	0x00060020

#define HID_CP_CONSUMER_CONTROL	0x000c0001
#define HID_CP_AC_PAN		0x000c0238

#define HID_DG_DIGITIZER	0x000d0001
#define HID_DG_PEN		0x000d0002
#define HID_DG_LIGHTPEN		0x000d0003
#define HID_DG_TOUCHSCREEN	0x000d0004
#define HID_DG_TOUCHPAD		0x000d0005
#define HID_DG_WHITEBOARD	0x000d0006
#define HID_DG_STYLUS		0x000d0020
#define HID_DG_PUCK		0x000d0021
#define HID_DG_FINGER		0x000d0022
#define HID_DG_TIPPRESSURE	0x000d0030
#define HID_DG_BARRELPRESSURE	0x000d0031
#define HID_DG_INRANGE		0x000d0032
#define HID_DG_TOUCH		0x000d0033
#define HID_DG_UNTOUCH		0x000d0034
#define HID_DG_TAP		0x000d0035
#define HID_DG_TRANSDUCER_INDEX	0x000d0038
#define HID_DG_TABLETFUNCTIONKEY	0x000d0039
#define HID_DG_PROGRAMCHANGEKEY	0x000d003a
#define HID_DG_BATTERYSTRENGTH	0x000d003b
#define HID_DG_INVERT		0x000d003c
#define HID_DG_TILT_X		0x000d003d
#define HID_DG_TILT_Y		0x000d003e
#define HID_DG_TWIST		0x000d0041
#define HID_DG_TIPSWITCH	0x000d0042
#define HID_DG_TIPSWITCH2	0x000d0043
#define HID_DG_BARRELSWITCH	0x000d0044
#define HID_DG_ERASER		0x000d0045
#define HID_DG_TABLETPICK	0x000d0046
#define HID_DG_PEN_COLOR			0x000d005c
#define HID_DG_PEN_LINE_WIDTH			0x000d005e
#define HID_DG_PEN_LINE_STYLE			0x000d0070
#define HID_DG_PEN_LINE_STYLE_INK		0x000d0072
#define HID_DG_PEN_LINE_STYLE_PENCIL		0x000d0073
#define HID_DG_PEN_LINE_STYLE_HIGHLIGHTER	0x000d0074
#define HID_DG_PEN_LINE_STYLE_CHISEL_MARKER	0x000d0075
#define HID_DG_PEN_LINE_STYLE_BRUSH		0x000d0076
#define HID_DG_PEN_LINE_STYLE_NO_PREFERENCE	0x000d0077

#define HID_CP_CONSUMERCONTROL	0x000c0001
#define HID_CP_NUMERICKEYPAD	0x000c0002
#define HID_CP_PROGRAMMABLEBUTTONS	0x000c0003
#define HID_CP_MICROPHONE	0x000c0004
#define HID_CP_HEADPHONE	0x000c0005
#define HID_CP_GRAPHICEQUALIZER	0x000c0006
#define HID_CP_FUNCTIONBUTTONS	0x000c0036
#define HID_CP_SELECTION	0x000c0080
#define HID_CP_MEDIASELECTION	0x000c0087
#define HID_CP_SELECTDISC	0x000c00ba
#define HID_CP_VOLUMEUP		0x000c00e9
#define HID_CP_VOLUMEDOWN	0x000c00ea
#define HID_CP_PLAYBACKSPEED	0x000c00f1
#define HID_CP_PROXIMITY	0x000c0109
#define HID_CP_SPEAKERSYSTEM	0x000c0160
#define HID_CP_CHANNELLEFT	0x000c0161
#define HID_CP_CHANNELRIGHT	0x000c0162
#define HID_CP_CHANNELCENTER	0x000c0163
#define HID_CP_CHANNELFRONT	0x000c0164
#define HID_CP_CHANNELCENTERFRONT	0x000c0165
#define HID_CP_CHANNELSIDE	0x000c0166
#define HID_CP_CHANNELSURROUND	0x000c0167
#define HID_CP_CHANNELLOWFREQUENCYENHANCEMENT	0x000c0168
#define HID_CP_CHANNELTOP	0x000c0169
#define HID_CP_CHANNELUNKNOWN	0x000c016a
#define HID_CP_APPLICATIONLAUNCHBUTTONS	0x000c0180
#define HID_CP_GENERICGUIAPPLICATIONCONTROLS	0x000c0200

#define HID_DG_DEVICECONFIG	0x000d000e
#define HID_DG_DEVICESETTINGS	0x000d0023
#define HID_DG_AZIMUTH		0x000d003f
#define HID_DG_CONFIDENCE	0x000d0047
#define HID_DG_WIDTH		0x000d0048
#define HID_DG_HEIGHT		0x000d0049
#define HID_DG_CONTACTID	0x000d0051
#define HID_DG_INPUTMODE	0x000d0052
#define HID_DG_DEVICEINDEX	0x000d0053
#define HID_DG_CONTACTCOUNT	0x000d0054
#define HID_DG_CONTACTMAX	0x000d0055
#define HID_DG_SCANTIME		0x000d0056
#define HID_DG_SURFACESWITCH	0x000d0057
#define HID_DG_BUTTONSWITCH	0x000d0058
#define HID_DG_BUTTONTYPE	0x000d0059
#define HID_DG_BARRELSWITCH2	0x000d005a
#define HID_DG_TOOLSERIALNUMBER	0x000d005b
#define HID_DG_LATENCYMODE	0x000d0060

#define HID_BAT_ABSOLUTESTATEOFCHARGE	0x00850065
#define HID_BAT_CHARGING		0x00850044

#define HID_VD_ASUS_CUSTOM_MEDIA_KEYS	0xff310076

/*
 * HID connect requests
 */

#define HID_CONNECT_HIDINPUT		BIT(0)
#define HID_CONNECT_HIDINPUT_FORCE	BIT(1)
#define HID_CONNECT_HIDRAW		BIT(2)
#define HID_CONNECT_HIDDEV		BIT(3)
#define HID_CONNECT_HIDDEV_FORCE	BIT(4)
#define HID_CONNECT_FF			BIT(5)
#define HID_CONNECT_DRIVER		BIT(6)
#define HID_CONNECT_DEFAULT	(HID_CONNECT_HIDINPUT|HID_CONNECT_HIDRAW| \
		HID_CONNECT_HIDDEV|HID_CONNECT_FF)

/*
 * HID device quirks.
 */

/*
 * Increase this if you need to configure more HID quirks at module load time
 */
#define MAX_USBHID_BOOT_QUIRKS 4

/* BIT(0) reserved for backward compatibility, was HID_QUIRK_INVERT */
#define HID_QUIRK_NOTOUCH			BIT(1)
#define HID_QUIRK_IGNORE			BIT(2)
#define HID_QUIRK_NOGET				BIT(3)
#define HID_QUIRK_HIDDEV_FORCE			BIT(4)
#define HID_QUIRK_BADPAD			BIT(5)
#define HID_QUIRK_MULTI_INPUT			BIT(6)
#define HID_QUIRK_HIDINPUT_FORCE		BIT(7)
/* BIT(8) reserved for backward compatibility, was HID_QUIRK_NO_EMPTY_INPUT */
/* BIT(9) reserved for backward compatibility, was NO_INIT_INPUT_REPORTS */
#define HID_QUIRK_ALWAYS_POLL			BIT(10)
#define HID_QUIRK_INPUT_PER_APP			BIT(11)
#define HID_QUIRK_X_INVERT			BIT(12)
#define HID_QUIRK_Y_INVERT			BIT(13)
#define HID_QUIRK_SKIP_OUTPUT_REPORTS		BIT(16)
#define HID_QUIRK_SKIP_OUTPUT_REPORT_ID		BIT(17)
#define HID_QUIRK_NO_OUTPUT_REPORTS_ON_INTR_EP	BIT(18)
#define HID_QUIRK_HAVE_SPECIAL_DRIVER		BIT(19)
#define HID_QUIRK_INCREMENT_USAGE_ON_DUPLICATE	BIT(20)
#define HID_QUIRK_FULLSPEED_INTERVAL		BIT(28)
#define HID_QUIRK_NO_INIT_REPORTS		BIT(29)
#define HID_QUIRK_NO_IGNORE			BIT(30)
#define HID_QUIRK_NO_INPUT_SYNC			BIT(31)

/*
 * HID device groups
 *
 * Note: HID_GROUP_ANY is declared in linux/mod_devicetable.h
 * and has a value of 0x0000
 */
#define HID_GROUP_GENERIC			0x0001
#define HID_GROUP_MULTITOUCH			0x0002
#define HID_GROUP_SENSOR_HUB			0x0003
#define HID_GROUP_MULTITOUCH_WIN_8		0x0004

/*
 * Vendor specific HID device groups
 */
#define HID_GROUP_RMI				0x0100
#define HID_GROUP_WACOM				0x0101
#define HID_GROUP_LOGITECH_DJ_DEVICE		0x0102
#define HID_GROUP_STEAM				0x0103
#define HID_GROUP_LOGITECH_27MHZ_DEVICE		0x0104
#define HID_GROUP_VIVALDI			0x0105

/*
 * HID protocol status
 */
#define HID_REPORT_PROTOCOL	1
#define HID_BOOT_PROTOCOL	0

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
	u8 usage_size[HID_MAX_USAGES]; /* usage size array */
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
	int parent_idx; /* device->collection */
	unsigned type;
	unsigned usage;
	unsigned level;
};

struct hid_usage {
	unsigned  hid;			/* hid usage code */
	unsigned  collection_index;	/* index into collection array */
	unsigned  usage_index;		/* index into usage array */
	__s8	  resolution_multiplier;/* Effective Resolution Multiplier
					   (HUT v1.12, 4.3.1), default: 1 */
	/* hidinput data */
	__s8	  wheel_factor;		/* 120/resolution_multiplier */
	__u16     code;			/* input driver code */
	__u8      type;			/* input driver type */
	__s8	  hat_min;		/* hat switch fun */
	__s8	  hat_max;		/* ditto */
	__s8	  hat_dir;		/* ditto */
	__s16	  wheel_accumulated;	/* hi-res wheel */
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
	__s32    *new_value;		/* newly read value(s) */
	__s32    *usages_priorities;	/* priority of each usage when reading the report
					 * bits 8-16 are reserved for hid-input usage
					 */
	__s32     logical_minimum;
	__s32     logical_maximum;
	__s32     physical_minimum;
	__s32     physical_maximum;
	__s32     unit_exponent;
	unsigned  unit;
	bool      ignored;		/* this field is ignored in this event */
	struct hid_report *report;	/* associated report */
	unsigned index;			/* index into report->field[] */
	/* hidinput data */
	struct hid_input *hidinput;	/* associated input structure */
	__u16 dpad;			/* dpad input code */
	unsigned int slot_idx;		/* slot index in a report */
};

#define HID_MAX_FIELDS 256

struct hid_field_entry {
	struct list_head list;
	struct hid_field *field;
	unsigned int index;
	__s32 priority;
};

struct hid_report {
	struct list_head list;
	struct list_head hidinput_list;
	struct list_head field_entry_list;		/* ordered list of input fields */
	unsigned int id;				/* id of this report */
	enum hid_report_type type;			/* report type */
	unsigned int application;			/* application usage for this report */
	struct hid_field *field[HID_MAX_FIELDS];	/* fields of the report */
	struct hid_field_entry *field_entries;		/* allocated memory of input field_entry */
	unsigned maxfield;				/* maximum valid field index */
	unsigned size;					/* size of the report (bits) */
	struct hid_device *device;			/* associated device */

	/* tool related state */
	bool tool_active;				/* whether the current tool is active */
	unsigned int tool;				/* BTN_TOOL_* */
};

#define HID_MAX_IDS 256

struct hid_report_enum {
	unsigned numbered;
	struct list_head report_list;
	struct hid_report *report_id_hash[HID_MAX_IDS];
};

#define HID_MIN_BUFFER_SIZE	64		/* make sure there is at least a packet size of space */
#define HID_MAX_BUFFER_SIZE	16384		/* 16kb */
#define HID_CONTROL_FIFO_SIZE	256		/* to init devices with >100 reports */
#define HID_OUTPUT_FIFO_SIZE	64

struct hid_control_fifo {
	unsigned char dir;
	struct hid_report *report;
	char *raw_report;
};

struct hid_output_fifo {
	struct hid_report *report;
	char *raw_report;
};

#define HID_CLAIMED_INPUT	BIT(0)
#define HID_CLAIMED_HIDDEV	BIT(1)
#define HID_CLAIMED_HIDRAW	BIT(2)
#define HID_CLAIMED_DRIVER	BIT(3)

#define HID_STAT_ADDED		BIT(0)
#define HID_STAT_PARSED		BIT(1)
#define HID_STAT_DUP_DETECTED	BIT(2)
#define HID_STAT_REPROBED	BIT(3)

struct hid_input {
	struct list_head list;
	struct hid_report *report;
	struct input_dev *input;
	const char *name;
	bool registered;
	struct list_head reports;	/* the list of reports */
	unsigned int application;	/* application usage for this input */
};

enum hid_type {
	HID_TYPE_OTHER = 0,
	HID_TYPE_USBMOUSE,
	HID_TYPE_USBNONE
};

enum hid_battery_status {
	HID_BATTERY_UNKNOWN = 0,
	HID_BATTERY_QUERIED,		/* Kernel explicitly queried battery strength */
	HID_BATTERY_REPORTED,		/* Device sent unsolicited battery strength report */
};

struct hid_driver;
struct hid_ll_driver;

struct hid_device {							/* device report descriptor */
	__u8 *dev_rdesc;
	unsigned dev_rsize;
	__u8 *rdesc;
	unsigned rsize;
	struct hid_collection *collection;				/* List of HID collections */
	unsigned collection_size;					/* Number of allocated hid_collections */
	unsigned maxcollection;						/* Number of parsed collections */
	unsigned maxapplication;					/* Number of applications */
	__u16 bus;							/* BUS ID */
	__u16 group;							/* Report group */
	__u32 vendor;							/* Vendor ID */
	__u32 product;							/* Product ID */
	__u32 version;							/* HID version */
	enum hid_type type;						/* device type (mouse, kbd, ...) */
	unsigned country;						/* HID country */
	struct hid_report_enum report_enum[HID_REPORT_TYPES];
	struct work_struct led_work;					/* delayed LED worker */

	struct semaphore driver_input_lock;				/* protects the current driver */
	struct device dev;						/* device */
	struct hid_driver *driver;

	const struct hid_ll_driver *ll_driver;
	struct mutex ll_open_lock;
	unsigned int ll_open_count;

#ifdef CONFIG_HID_BATTERY_STRENGTH
	/*
	 * Power supply information for HID devices which report
	 * battery strength. power_supply was successfully registered if
	 * battery is non-NULL.
	 */
	struct power_supply *battery;
	__s32 battery_capacity;
	__s32 battery_min;
	__s32 battery_max;
	__s32 battery_report_type;
	__s32 battery_report_id;
	__s32 battery_charge_status;
	enum hid_battery_status battery_status;
	bool battery_avoid_query;
	ktime_t battery_ratelimit_time;
#endif

	unsigned long status;						/* see STAT flags above */
	unsigned claimed;						/* Claimed by hidinput, hiddev? */
	unsigned quirks;						/* Various quirks the device can pull on us */
	unsigned initial_quirks;					/* Initial set of quirks supplied when creating device */
	bool io_started;						/* If IO has started */

	struct list_head inputs;					/* The list of inputs */
	void *hiddev;							/* The hiddev structure */
	void *hidraw;

	char name[128];							/* Device name */
	char phys[64];							/* Device physical location */
	char uniq[64];							/* Device unique identifier (serial #) */

	void *driver_data;

	/* temporary hid_ff handling (until moved to the drivers) */
	int (*ff_init)(struct hid_device *);

	/* hiddev event handler */
	int (*hiddev_connect)(struct hid_device *, unsigned int);
	void (*hiddev_disconnect)(struct hid_device *);
	void (*hiddev_hid_event) (struct hid_device *, struct hid_field *field,
				  struct hid_usage *, __s32);
	void (*hiddev_report_event) (struct hid_device *, struct hid_report *);

	/* debugging support via debugfs */
	unsigned short debug;
	struct dentry *debug_dir;
	struct dentry *debug_rdesc;
	struct dentry *debug_events;
	struct list_head debug_list;
	spinlock_t  debug_list_lock;
	wait_queue_head_t debug_wait;

	unsigned int id;						/* system unique id */

#ifdef CONFIG_BPF
	struct hid_bpf bpf;						/* hid-bpf data */
#endif /* CONFIG_BPF */
};

#define to_hid_device(pdev) \
	container_of(pdev, struct hid_device, dev)

static inline void *hid_get_drvdata(struct hid_device *hdev)
{
	return dev_get_drvdata(&hdev->dev);
}

static inline void hid_set_drvdata(struct hid_device *hdev, void *data)
{
	dev_set_drvdata(&hdev->dev, data);
}

#define HID_GLOBAL_STACK_SIZE 4
#define HID_COLLECTION_STACK_SIZE 4

#define HID_SCAN_FLAG_MT_WIN_8			BIT(0)
#define HID_SCAN_FLAG_VENDOR_SPECIFIC		BIT(1)
#define HID_SCAN_FLAG_GD_POINTER		BIT(2)

struct hid_parser {
	struct hid_global     global;
	struct hid_global     global_stack[HID_GLOBAL_STACK_SIZE];
	unsigned int          global_stack_ptr;
	struct hid_local      local;
	unsigned int         *collection_stack;
	unsigned int          collection_stack_ptr;
	unsigned int          collection_stack_size;
	struct hid_device    *device;
	unsigned int          scan_flags;
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

#define HID_DEVICE(b, g, ven, prod)					\
	.bus = (b), .group = (g), .vendor = (ven), .product = (prod)
#define HID_USB_DEVICE(ven, prod)				\
	.bus = BUS_USB, .vendor = (ven), .product = (prod)
#define HID_BLUETOOTH_DEVICE(ven, prod)					\
	.bus = BUS_BLUETOOTH, .vendor = (ven), .product = (prod)
#define HID_I2C_DEVICE(ven, prod)				\
	.bus = BUS_I2C, .vendor = (ven), .product = (prod)

#define HID_REPORT_ID(rep) \
	.report_type = (rep)
#define HID_USAGE_ID(uhid, utype, ucode) \
	.usage_hid = (uhid), .usage_type = (utype), .usage_code = (ucode)
/* we don't want to catch types and codes equal to 0 */
#define HID_TERMINATOR		(HID_ANY_ID - 1)

struct hid_report_id {
	__u32 report_type;
};
struct hid_usage_id {
	__u32 usage_hid;
	__u32 usage_type;
	__u32 usage_code;
};

/**
 * struct hid_driver
 * @name: driver name (e.g. "Footech_bar-wheel")
 * @id_table: which devices is this driver for (must be non-NULL for probe
 * 	      to be called)
 * @dyn_list: list of dynamically added device ids
 * @dyn_lock: lock protecting @dyn_list
 * @match: check if the given device is handled by this driver
 * @probe: new device inserted
 * @remove: device removed (NULL if not a hot-plug capable driver)
 * @report_table: on which reports to call raw_event (NULL means all)
 * @raw_event: if report in report_table, this hook is called (NULL means nop)
 * @usage_table: on which events to call event (NULL means all)
 * @event: if usage in usage_table, this hook is called (NULL means nop)
 * @report: this hook is called after parsing a report (NULL means nop)
 * @report_fixup: called before report descriptor parsing (NULL means nop)
 * @input_mapping: invoked on input registering before mapping an usage
 * @input_mapped: invoked on input registering after mapping an usage
 * @input_configured: invoked just before the device is registered
 * @feature_mapping: invoked on feature registering
 * @suspend: invoked on suspend (NULL means nop)
 * @resume: invoked on resume if device was not reset (NULL means nop)
 * @reset_resume: invoked on resume if device was reset (NULL means nop)
 *
 * probe should return -errno on error, or 0 on success. During probe,
 * input will not be passed to raw_event unless hid_device_io_start is
 * called.
 *
 * raw_event and event should return negative on error, any other value will
 * pass the event on to .event() typically return 0 for success.
 *
 * input_mapping shall return a negative value to completely ignore this usage
 * (e.g. doubled or invalid usage), zero to continue with parsing of this
 * usage by generic code (no special handling needed) or positive to skip
 * generic parsing (needed special handling which was done in the hook already)
 * input_mapped shall return negative to inform the layer that this usage
 * should not be considered for further processing or zero to notify that
 * no processing was performed and should be done in a generic manner
 * Both these functions may be NULL which means the same behavior as returning
 * zero from them.
 */
struct hid_driver {
	char *name;
	const struct hid_device_id *id_table;

	struct list_head dyn_list;
	spinlock_t dyn_lock;

	bool (*match)(struct hid_device *dev, bool ignore_special_driver);
	int (*probe)(struct hid_device *dev, const struct hid_device_id *id);
	void (*remove)(struct hid_device *dev);

	const struct hid_report_id *report_table;
	int (*raw_event)(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size);
	const struct hid_usage_id *usage_table;
	int (*event)(struct hid_device *hdev, struct hid_field *field,
			struct hid_usage *usage, __s32 value);
	void (*report)(struct hid_device *hdev, struct hid_report *report);

	__u8 *(*report_fixup)(struct hid_device *hdev, __u8 *buf,
			unsigned int *size);

	int (*input_mapping)(struct hid_device *hdev,
			struct hid_input *hidinput, struct hid_field *field,
			struct hid_usage *usage, unsigned long **bit, int *max);
	int (*input_mapped)(struct hid_device *hdev,
			struct hid_input *hidinput, struct hid_field *field,
			struct hid_usage *usage, unsigned long **bit, int *max);
	int (*input_configured)(struct hid_device *hdev,
				struct hid_input *hidinput);
	void (*feature_mapping)(struct hid_device *hdev,
			struct hid_field *field,
			struct hid_usage *usage);
#ifdef CONFIG_PM
	int (*suspend)(struct hid_device *hdev, pm_message_t message);
	int (*resume)(struct hid_device *hdev);
	int (*reset_resume)(struct hid_device *hdev);
#endif
/* private: */
	struct device_driver driver;
};

#define to_hid_driver(pdrv) \
	container_of(pdrv, struct hid_driver, driver)

/**
 * struct hid_ll_driver - low level driver callbacks
 * @start: called on probe to start the device
 * @stop: called on remove
 * @open: called by input layer on open
 * @close: called by input layer on close
 * @power: request underlying hardware to enter requested power mode
 * @parse: this method is called only once to parse the device data,
 *	   shouldn't allocate anything to not leak memory
 * @request: send report request to device (e.g. feature report)
 * @wait: wait for buffered io to complete (send/recv reports)
 * @raw_request: send raw report request to device (e.g. feature report)
 * @output_report: send output report to device
 * @idle: send idle request to device
 * @may_wakeup: return if device may act as a wakeup source during system-suspend
 * @max_buffer_size: over-ride maximum data buffer size (default: HID_MAX_BUFFER_SIZE)
 */
struct hid_ll_driver {
	int (*start)(struct hid_device *hdev);
	void (*stop)(struct hid_device *hdev);

	int (*open)(struct hid_device *hdev);
	void (*close)(struct hid_device *hdev);

	int (*power)(struct hid_device *hdev, int level);

	int (*parse)(struct hid_device *hdev);

	void (*request)(struct hid_device *hdev,
			struct hid_report *report, int reqtype);

	int (*wait)(struct hid_device *hdev);

	int (*raw_request) (struct hid_device *hdev, unsigned char reportnum,
			    __u8 *buf, size_t len, unsigned char rtype,
			    int reqtype);

	int (*output_report) (struct hid_device *hdev, __u8 *buf, size_t len);

	int (*idle)(struct hid_device *hdev, int report, int idle, int reqtype);
	bool (*may_wakeup)(struct hid_device *hdev);

	unsigned int max_buffer_size;
};

extern bool hid_is_usb(const struct hid_device *hdev);

#define	PM_HINT_FULLON	1<<5
#define PM_HINT_NORMAL	1<<1

/* Applications from HID Usage Tables 4/8/99 Version 1.1 */
/* We ignore a few input applications that are not widely used */
#define IS_INPUT_APPLICATION(a) \
		(((a >= HID_UP_GENDESK) && (a <= HID_GD_MULTIAXIS)) \
		|| ((a >= HID_DG_DIGITIZER) && (a <= HID_DG_WHITEBOARD)) \
		|| (a == HID_GD_SYSTEM_CONTROL) || (a == HID_CP_CONSUMER_CONTROL) \
		|| (a == HID_GD_WIRELESS_RADIO_CTLS))

/* HID core API */

extern bool hid_ignore(struct hid_device *);
extern int hid_add_device(struct hid_device *);
extern void hid_destroy_device(struct hid_device *);

extern struct bus_type hid_bus_type;

extern int __must_check __hid_register_driver(struct hid_driver *,
		struct module *, const char *mod_name);

/* use a define to avoid include chaining to get THIS_MODULE & friends */
#define hid_register_driver(driver) \
	__hid_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

extern void hid_unregister_driver(struct hid_driver *);

/**
 * module_hid_driver() - Helper macro for registering a HID driver
 * @__hid_driver: hid_driver struct
 *
 * Helper macro for HID drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_hid_driver(__hid_driver) \
	module_driver(__hid_driver, hid_register_driver, \
		      hid_unregister_driver)

extern void hidinput_hid_event(struct hid_device *, struct hid_field *, struct hid_usage *, __s32);
extern void hidinput_report_event(struct hid_device *hid, struct hid_report *report);
extern int hidinput_connect(struct hid_device *hid, unsigned int force);
extern void hidinput_disconnect(struct hid_device *);

int hid_set_field(struct hid_field *, unsigned, __s32);
int hid_input_report(struct hid_device *hid, enum hid_report_type type, u8 *data, u32 size,
		     int interrupt);
struct hid_field *hidinput_get_led_field(struct hid_device *hid);
unsigned int hidinput_count_leds(struct hid_device *hid);
__s32 hidinput_calc_abs_res(const struct hid_field *field, __u16 code);
void hid_output_report(struct hid_report *report, __u8 *data);
int __hid_request(struct hid_device *hid, struct hid_report *rep, enum hid_class_request reqtype);
u8 *hid_alloc_report_buf(struct hid_report *report, gfp_t flags);
struct hid_device *hid_allocate_device(void);
struct hid_report *hid_register_report(struct hid_device *device,
				       enum hid_report_type type, unsigned int id,
				       unsigned int application);
int hid_parse_report(struct hid_device *hid, __u8 *start, unsigned size);
struct hid_report *hid_validate_values(struct hid_device *hid,
				       enum hid_report_type type, unsigned int id,
				       unsigned int field_index,
				       unsigned int report_counts);

void hid_setup_resolution_multiplier(struct hid_device *hid);
int hid_open_report(struct hid_device *device);
int hid_check_keys_pressed(struct hid_device *hid);
int hid_connect(struct hid_device *hid, unsigned int connect_mask);
void hid_disconnect(struct hid_device *hid);
bool hid_match_one_id(const struct hid_device *hdev,
		      const struct hid_device_id *id);
const struct hid_device_id *hid_match_id(const struct hid_device *hdev,
					 const struct hid_device_id *id);
const struct hid_device_id *hid_match_device(struct hid_device *hdev,
					     struct hid_driver *hdrv);
bool hid_compare_device_paths(struct hid_device *hdev_a,
			      struct hid_device *hdev_b, char separator);
s32 hid_snto32(__u32 value, unsigned n);
__u32 hid_field_extract(const struct hid_device *hid, __u8 *report,
		     unsigned offset, unsigned n);

#ifdef CONFIG_PM
int hid_driver_suspend(struct hid_device *hdev, pm_message_t state);
int hid_driver_reset_resume(struct hid_device *hdev);
int hid_driver_resume(struct hid_device *hdev);
#else
static inline int hid_driver_suspend(struct hid_device *hdev, pm_message_t state) { return 0; }
static inline int hid_driver_reset_resume(struct hid_device *hdev) { return 0; }
static inline int hid_driver_resume(struct hid_device *hdev) { return 0; }
#endif

/**
 * hid_device_io_start - enable HID input during probe, remove
 *
 * @hid: the device
 *
 * This should only be called during probe or remove and only be
 * called by the thread calling probe or remove. It will allow
 * incoming packets to be delivered to the driver.
 */
static inline void hid_device_io_start(struct hid_device *hid) {
	if (hid->io_started) {
		dev_warn(&hid->dev, "io already started\n");
		return;
	}
	hid->io_started = true;
	up(&hid->driver_input_lock);
}

/**
 * hid_device_io_stop - disable HID input during probe, remove
 *
 * @hid: the device
 *
 * Should only be called after hid_device_io_start. It will prevent
 * incoming packets from going to the driver for the duration of
 * probe, remove. If called during probe, packets will still go to the
 * driver after probe is complete. This function should only be called
 * by the thread calling probe or remove.
 */
static inline void hid_device_io_stop(struct hid_device *hid) {
	if (!hid->io_started) {
		dev_warn(&hid->dev, "io already stopped\n");
		return;
	}
	hid->io_started = false;
	down(&hid->driver_input_lock);
}

/**
 * hid_map_usage - map usage input bits
 *
 * @hidinput: hidinput which we are interested in
 * @usage: usage to fill in
 * @bit: pointer to input->{}bit (out parameter)
 * @max: maximal valid usage->code to consider later (out parameter)
 * @type: input event type (EV_KEY, EV_REL, ...)
 * @c: code which corresponds to this usage and type
 *
 * The value pointed to by @bit will be set to NULL if either @type is
 * an unhandled event type, or if @c is out of range for @type. This
 * can be used as an error condition.
 */
static inline void hid_map_usage(struct hid_input *hidinput,
		struct hid_usage *usage, unsigned long **bit, int *max,
		__u8 type, unsigned int c)
{
	struct input_dev *input = hidinput->input;
	unsigned long *bmap = NULL;
	unsigned int limit = 0;

	switch (type) {
	case EV_ABS:
		bmap = input->absbit;
		limit = ABS_MAX;
		break;
	case EV_REL:
		bmap = input->relbit;
		limit = REL_MAX;
		break;
	case EV_KEY:
		bmap = input->keybit;
		limit = KEY_MAX;
		break;
	case EV_LED:
		bmap = input->ledbit;
		limit = LED_MAX;
		break;
	case EV_MSC:
		bmap = input->mscbit;
		limit = MSC_MAX;
		break;
	}

	if (unlikely(c > limit || !bmap)) {
		pr_warn_ratelimited("%s: Invalid code %d type %d\n",
				    input->name, c, type);
		*bit = NULL;
		return;
	}

	usage->type = type;
	usage->code = c;
	*max = limit;
	*bit = bmap;
}

/**
 * hid_map_usage_clear - map usage input bits and clear the input bit
 *
 * @hidinput: hidinput which we are interested in
 * @usage: usage to fill in
 * @bit: pointer to input->{}bit (out parameter)
 * @max: maximal valid usage->code to consider later (out parameter)
 * @type: input event type (EV_KEY, EV_REL, ...)
 * @c: code which corresponds to this usage and type
 *
 * The same as hid_map_usage, except the @c bit is also cleared in supported
 * bits (@bit).
 */
static inline void hid_map_usage_clear(struct hid_input *hidinput,
		struct hid_usage *usage, unsigned long **bit, int *max,
		__u8 type, __u16 c)
{
	hid_map_usage(hidinput, usage, bit, max, type, c);
	if (*bit)
		clear_bit(usage->code, *bit);
}

/**
 * hid_parse - parse HW reports
 *
 * @hdev: hid device
 *
 * Call this from probe after you set up the device (if needed). Your
 * report_fixup will be called (if non-NULL) after reading raw report from
 * device before passing it to hid layer for real parsing.
 */
static inline int __must_check hid_parse(struct hid_device *hdev)
{
	return hid_open_report(hdev);
}

int __must_check hid_hw_start(struct hid_device *hdev,
			      unsigned int connect_mask);
void hid_hw_stop(struct hid_device *hdev);
int __must_check hid_hw_open(struct hid_device *hdev);
void hid_hw_close(struct hid_device *hdev);
void hid_hw_request(struct hid_device *hdev,
		    struct hid_report *report, enum hid_class_request reqtype);
int hid_hw_raw_request(struct hid_device *hdev,
		       unsigned char reportnum, __u8 *buf,
		       size_t len, enum hid_report_type rtype,
		       enum hid_class_request reqtype);
int hid_hw_output_report(struct hid_device *hdev, __u8 *buf, size_t len);

/**
 * hid_hw_power - requests underlying HW to go into given power mode
 *
 * @hdev: hid device
 * @level: requested power level (one of %PM_HINT_* defines)
 *
 * This function requests underlying hardware to enter requested power
 * mode.
 */

static inline int hid_hw_power(struct hid_device *hdev, int level)
{
	return hdev->ll_driver->power ? hdev->ll_driver->power(hdev, level) : 0;
}


/**
 * hid_hw_idle - send idle request to device
 *
 * @hdev: hid device
 * @report: report to control
 * @idle: idle state
 * @reqtype: hid request type
 */
static inline int hid_hw_idle(struct hid_device *hdev, int report, int idle,
		enum hid_class_request reqtype)
{
	if (hdev->ll_driver->idle)
		return hdev->ll_driver->idle(hdev, report, idle, reqtype);

	return 0;
}

/**
 * hid_hw_may_wakeup - return if the hid device may act as a wakeup source during system-suspend
 *
 * @hdev: hid device
 */
static inline bool hid_hw_may_wakeup(struct hid_device *hdev)
{
	if (hdev->ll_driver->may_wakeup)
		return hdev->ll_driver->may_wakeup(hdev);

	if (hdev->dev.parent)
		return device_may_wakeup(hdev->dev.parent);

	return false;
}

/**
 * hid_hw_wait - wait for buffered io to complete
 *
 * @hdev: hid device
 */
static inline void hid_hw_wait(struct hid_device *hdev)
{
	if (hdev->ll_driver->wait)
		hdev->ll_driver->wait(hdev);
}

/**
 * hid_report_len - calculate the report length
 *
 * @report: the report we want to know the length
 */
static inline u32 hid_report_len(struct hid_report *report)
{
	return DIV_ROUND_UP(report->size, 8) + (report->id > 0);
}

int hid_report_raw_event(struct hid_device *hid, enum hid_report_type type, u8 *data, u32 size,
			 int interrupt);

/* HID quirks API */
unsigned long hid_lookup_quirk(const struct hid_device *hdev);
int hid_quirks_init(char **quirks_param, __u16 bus, int count);
void hid_quirks_exit(__u16 bus);

#ifdef CONFIG_HID_PID
int hid_pidff_init(struct hid_device *hid);
#else
#define hid_pidff_init NULL
#endif

#define dbg_hid(fmt, ...) pr_debug("%s: " fmt, __FILE__, ##__VA_ARGS__)

#define hid_err(hid, fmt, ...)				\
	dev_err(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_notice(hid, fmt, ...)			\
	dev_notice(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_warn(hid, fmt, ...)				\
	dev_warn(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_info(hid, fmt, ...)				\
	dev_info(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_dbg(hid, fmt, ...)				\
	dev_dbg(&(hid)->dev, fmt, ##__VA_ARGS__)

#define hid_err_once(hid, fmt, ...)			\
	dev_err_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_notice_once(hid, fmt, ...)			\
	dev_notice_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_warn_once(hid, fmt, ...)			\
	dev_warn_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_info_once(hid, fmt, ...)			\
	dev_info_once(&(hid)->dev, fmt, ##__VA_ARGS__)
#define hid_dbg_once(hid, fmt, ...)			\
	dev_dbg_once(&(hid)->dev, fmt, ##__VA_ARGS__)

#endif
