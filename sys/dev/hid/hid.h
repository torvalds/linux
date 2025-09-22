/*	$OpenBSD: hid.h,v 1.12 2025/07/21 21:46:40 bru Exp $ */
/*	$NetBSD: hid.h,v 1.8 2002/07/11 21:14:25 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/hid.h,v 1.7 1999/11/17 22:33:40 n_hibma Exp $ */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _HIDHID_H_
#define _HIDHID_H_

#ifdef _KERNEL

enum hid_kind {
	hid_input,
	hid_output,
	hid_feature,
	hid_collection,
	hid_endcollection,
	hid_all
};

struct hid_location {
	u_int32_t size;
	u_int32_t count;
	u_int32_t pos;
};

struct hid_item {
	/* Global */
	int32_t _usage_page;
	int32_t logical_minimum;
	int32_t logical_maximum;
	int32_t physical_minimum;
	int32_t physical_maximum;
	int32_t unit_exponent;
	int32_t unit;
	int32_t report_ID;
	/* Local */
	int32_t usage;
	int32_t usage_minimum;
	int32_t usage_maximum;
	int32_t designator_index;
	int32_t designator_minimum;
	int32_t designator_maximum;
	int32_t string_index;
	int32_t string_minimum;
	int32_t string_maximum;
	int32_t set_delimiter;
	/* Misc */
	int32_t collection;
	int collevel;
	enum hid_kind kind;
	u_int32_t flags;
	/* Location */
	struct hid_location loc;
	/* */
	struct hid_item *next;
};

struct	hid_data *hid_start_parse(const void *, int, enum hid_kind);
void	hid_end_parse(struct hid_data *);
int	hid_get_item(struct hid_data *, struct hid_item *);
int	hid_report_size(const void *, int, enum hid_kind, uint8_t);
int	hid_locate(const void *, int, int32_t, uint8_t, enum hid_kind,
	    struct hid_location *, uint32_t *);
int32_t	hid_get_data(const uint8_t *buf, int, struct hid_location *);
uint32_t hid_get_udata(const uint8_t *buf, int, struct hid_location *);
int	hid_is_collection(const void *, int, uint8_t, int32_t);
struct hid_data *hid_get_collection_data(const void *, int, int32_t, uint32_t);
int	hid_get_id_of_collection(const void *, int, int32_t, uint32_t);
int	hid_find_report(const void *, int len, enum hid_kind, int32_t,
	    int, int32_t *, int32_t *);

#endif /* _KERNEL */

/* Usage pages */
#define HUP_UNDEFINED		0x0000
#define HUP_GENERIC_DESKTOP	0x0001
#define HUP_SIMULATION		0x0002
#define HUP_VR_CONTROLS		0x0003
#define HUP_SPORTS_CONTROLS	0x0004
#define HUP_GAMING_CONTROLS	0x0005
#define HUP_KEYBOARD		0x0007
#define HUP_LED			0x0008
#define HUP_BUTTON		0x0009
#define HUP_ORDINALS		0x000a
#define HUP_TELEPHONY		0x000b
#define HUP_CONSUMER		0x000c
#define HUP_DIGITIZERS		0x000d
#define HUP_PHYSICAL_IFACE	0x000e
#define HUP_UNICODE		0x0010
#define HUP_ALPHANUM_DISPLAY	0x0014
#define HUP_MONITOR		0x0080
#define HUP_MONITOR_ENUM_VAL	0x0081
#define HUP_VESA_VC		0x0082
#define HUP_VESA_CMD		0x0083
#define HUP_POWER		0x0084
#define HUP_BATTERY		0x0085
#define HUP_BARCODE_SCANNER	0x008b
#define HUP_SCALE		0x008c
#define HUP_CAMERA_CONTROL	0x0090
#define HUP_ARCADE		0x0091
#define HUP_VENDOR		0x00ff
#define HUP_FIDO		0xf1d0
#define HUP_MICROSOFT		0xff00
/* XXX compat */
#define HUP_APPLE		0x00ff
#define HUP_WACOM		0xff00

/* Usages, Power Device */
#define HUP_INAME		0x0001
#define HUP_PRESENT_STATUS	0x0002
#define HUP_CHANGED_STATUS	0x0003
#define HUP_UPS			0x0004
#define HUP_POWER_SUPPLY	0x0005
#define HUP_BATTERY_SYSTEM	0x0010
#define HUP_BATTERY_SYSTEM_ID	0x0011
#define HUP_PD_BATTERY		0x0012
#define HUP_BATTERY_ID		0x0013
#define HUP_CHARGER		0x0014
#define HUP_CHARGER_ID		0x0015
#define HUP_POWER_CONVERTER	0x0016
#define HUP_POWER_CONVERTER_ID	0x0017
#define HUP_OUTLET_SYSTEM	0x0018
#define HUP_OUTLET_SYSTEM_ID	0x0019
#define HUP_INPUT		0x001a
#define HUP_INPUT_ID		0x001b
#define HUP_OUTPUT		0x001c
#define HUP_OUTPUT_ID		0x001d
#define HUP_FLOW		0x001e
#define HUP_FLOW_ID		0x001f
#define HUP_OUTLET		0x0020
#define HUP_OUTLET_ID		0x0021
#define HUP_GANG		0x0022
#define HUP_GANG_ID		0x0023
#define HUP_POWER_SUMMARY	0x0024
#define HUP_POWER_SUMMARY_ID	0x0025
#define HUP_VOLTAGE		0x0030
#define HUP_CURRENT		0x0031
#define HUP_FREQUENCY		0x0032
#define HUP_APPARENT_POWER	0x0033
#define HUP_ACTIVE_POWER	0x0034
#define HUP_PERCENT_LOAD	0x0035
#define HUP_TEMPERATURE		0x0036
#define HUP_HUMIDITY		0x0037
#define HUP_BADCOUNT		0x0038
#define HUP_CONFIG_VOLTAGE	0x0040
#define HUP_CONFIG_CURRENT	0x0041
#define HUP_CONFIG_FREQUENCY	0x0042
#define HUP_CONFIG_APP_POWER	0x0043
#define HUP_CONFIG_ACT_POWER	0x0044
#define HUP_CONFIG_PERCENT_LOAD	0x0045
#define HUP_CONFIG_TEMPERATURE	0x0046
#define HUP_CONFIG_HUMIDITY	0x0047
#define HUP_SWITCHON_CONTROL	0x0050
#define HUP_SWITCHOFF_CONTROL	0x0051
#define HUP_TOGGLE_CONTROL	0x0052
#define HUP_LOW_VOLT_TRANSF	0x0053
#define HUP_HIGH_VOLT_TRANSF	0x0054
#define HUP_DELAYBEFORE_REBOOT	0x0055
#define HUP_DELAYBEFORE_STARTUP	0x0056
#define HUP_DELAYBEFORE_SHUTDWN	0x0057
#define HUP_TEST		0x0058
#define HUP_MODULE_RESET	0x0059
#define HUP_AUDIBLE_ALRM_CTL	0x005a
#define HUP_PRESENT		0x0060
#define HUP_GOOD		0x0061
#define HUP_INTERNAL_FAILURE	0x0062
#define HUP_PD_VOLT_OUTOF_RANGE	0x0063
#define HUP_FREQ_OUTOFRANGE	0x0064
#define HUP_OVERLOAD		0x0065
#define HUP_OVERCHARGED		0x0066
#define HUP_OVERTEMPERATURE	0x0067
#define HUP_SHUTDOWN_REQUESTED	0x0068
#define HUP_SHUTDOWN_IMMINENT	0x0069
#define HUP_SWITCH_ON_OFF	0x006b
#define HUP_SWITCHABLE		0x006c
#define HUP_USED		0x006d
#define HUP_BOOST		0x006e
#define HUP_BUCK		0x006f
#define HUP_INITIALIZED		0x0070
#define HUP_TESTED		0x0071
#define HUP_AWAITING_POWER	0x0072
#define HUP_COMMUNICATION_LOST	0x0073
#define HUP_IMANUFACTURER	0x00fd
#define HUP_IPRODUCT		0x00fe
#define HUP_ISERIALNUMBER	0x00ff

/* Usages, Battery */
#define HUB_SMB_BATTERY_MODE	0x0001
#define HUB_SMB_BATTERY_STATUS	0x0002
#define HUB_SMB_ALARM_WARNING	0x0003
#define HUB_SMB_CHARGER_MODE	0x0004
#define HUB_SMB_CHARGER_STATUS	0x0005
#define HUB_SMB_CHARGER_SPECINF	0x0006
#define HUB_SMB_SELECTR_STATE	0x0007
#define HUB_SMB_SELECTR_PRESETS	0x0008
#define HUB_SMB_SELECTR_INFO	0x0009
#define HUB_SMB_OPT_MFGFUNC1	0x0010
#define HUB_SMB_OPT_MFGFUNC2	0x0011
#define HUB_SMB_OPT_MFGFUNC3	0x0012
#define HUB_SMB_OPT_MFGFUNC4	0x0013
#define HUB_SMB_OPT_MFGFUNC5	0x0014
#define HUB_CONNECTIONTOSMBUS	0x0015
#define HUB_OUTPUT_CONNECTION	0x0016
#define HUB_CHARGER_CONNECTION	0x0017
#define HUB_BATTERY_INSERTION	0x0018
#define HUB_USENEXT		0x0019
#define HUB_OKTOUSE		0x001a
#define HUB_BATTERY_SUPPORTED	0x001b
#define HUB_SELECTOR_REVISION	0x001c
#define HUB_CHARGING_INDICATOR	0x001d
#define HUB_MANUFACTURER_ACCESS	0x0028
#define HUB_REM_CAPACITY_LIM	0x0029
#define HUB_REM_TIME_LIM	0x002a
#define HUB_ATRATE		0x002b
#define HUB_CAPACITY_MODE	0x002c
#define HUB_BCAST_TO_CHARGER	0x002d
#define HUB_PRIMARY_BATTERY	0x002e
#define HUB_CHANGE_CONTROLLER	0x002f
#define HUB_TERMINATE_CHARGE	0x0040
#define HUB_TERMINATE_DISCHARGE	0x0041
#define HUB_BELOW_REM_CAP_LIM	0x0042
#define HUB_REM_TIME_LIM_EXP	0x0043
#define HUB_CHARGING		0x0044
#define HUB_DISCHARGING		0x0045
#define HUB_FULLY_CHARGED	0x0046
#define HUB_FULLY_DISCHARGED	0x0047
#define HUB_CONDITIONING_FLAG	0x0048
#define HUB_ATRATE_OK		0x0049
#define HUB_SMB_ERROR_CODE	0x004a
#define HUB_NEED_REPLACEMENT	0x004b
#define HUB_ATRATE_TIMETOFULL	0x0060
#define HUB_ATRATE_TIMETOEMPTY	0x0061
#define HUB_AVERAGE_CURRENT	0x0062
#define HUB_MAXERROR		0x0063
#define HUB_REL_STATEOF_CHARGE	0x0064
#define HUB_ABS_STATEOF_CHARGE	0x0065
#define HUB_REM_CAPACITY	0x0066
#define HUB_FULLCHARGE_CAPACITY	0x0067
#define HUB_RUNTIMETO_EMPTY	0x0068
#define HUB_AVERAGETIMETO_EMPTY	0x0069
#define HUB_AVERAGETIMETO_FULL	0x006a
#define HUB_CYCLECOUNT		0x006b
#define HUB_BATTPACKMODEL_LEVEL	0x0080
#define HUB_INTERNAL_CHARGE_CTL	0x0081
#define HUB_PRIMARY_BATTERY_SUP	0x0082
#define HUB_DESIGN_CAPACITY	0x0083
#define HUB_SPECIFICATION_INFO	0x0084
#define HUB_MANUFACTURER_DATE	0x0085
#define HUB_SERIAL_NUMBER	0x0086
#define HUB_IMANUFACTURERNAME	0x0087
#define HUB_IDEVICENAME		0x0088
#define HUB_IDEVICECHEMISTERY	0x0089
#define HUB_MANUFACTURERDATA	0x008a
#define HUB_RECHARGABLE		0x008b
#define HUB_WARN_CAPACITY_LIM	0x008c
#define HUB_CAPACITY_GRANUL1	0x008d
#define HUB_CAPACITY_GRANUL2	0x008e
#define HUB_IOEM_INFORMATION	0x008f
#define HUB_INHIBIT_CHARGE	0x00c0
#define HUB_ENABLE_POLLING	0x00c1
#define HUB_RESTORE_TO_ZERO	0x00c2
#define HUB_AC_PRESENT		0x00d0
#define HUB_BATTERY_PRESENT	0x00d1
#define HUB_POWER_FAIL		0x00d2
#define HUB_ALARM_INHIBITED	0x00d3
#define HUB_THERMISTOR_UNDRANGE	0x00d4
#define HUB_THERMISTOR_HOT	0x00d5
#define HUB_THERMISTOR_COLD	0x00d6
#define HUB_THERMISTOR_OVERANGE	0x00d7
#define HUB_BS_VOLT_OUTOF_RANGE	0x00d8
#define HUB_BS_CURR_OUTOF_RANGE	0x00d9
#define HUB_BS_CURR_NOT_REGULTD	0x00da
#define HUB_BS_VOLT_NOT_REGULTD	0x00db
#define HUB_MASTER_MODE		0x00dc
#define HUB_CHARGER_SELECTR_SUP	0x00f0
#define HUB_CHARGER_SPEC	0x00f1
#define HUB_LEVEL2		0x00f2
#define HUB_LEVEL3		0x00f3

/* Usages, generic desktop */
#define HUG_POINTER		0x0001
#define HUG_MOUSE		0x0002
#define HUG_FN_KEY		0x0003
#define HUG_JOYSTICK		0x0004
#define HUG_GAME_PAD		0x0005
#define HUG_KEYBOARD		0x0006
#define HUG_KEYPAD		0x0007
#define HUG_X			0x0030
#define HUG_Y			0x0031
#define HUG_Z			0x0032
#define HUG_RX			0x0033
#define HUG_RY			0x0034
#define HUG_RZ			0x0035
#define HUG_SLIDER		0x0036
#define HUG_DIAL		0x0037
#define HUG_WHEEL		0x0038
#define HUG_HAT_SWITCH		0x0039
#define HUG_COUNTED_BUFFER	0x003a
#define HUG_BYTE_COUNT		0x003b
#define HUG_MOTION_WAKEUP	0x003c
#define HUG_VX			0x0040
#define HUG_VY			0x0041
#define HUG_VZ			0x0042
#define HUG_VBRX		0x0043
#define HUG_VBRY		0x0044
#define HUG_VBRZ		0x0045
#define HUG_VNO			0x0046
#define HUG_TWHEEL		0x0048
#define HUG_SYSTEM_CONTROL	0x0080
#define HUG_SYSTEM_POWER_DOWN	0x0081
#define HUG_SYSTEM_SLEEP	0x0082
#define HUG_SYSTEM_WAKEUP	0x0083
#define HUG_SYSTEM_CONTEXT_MENU	0x0084
#define HUG_SYSTEM_MAIN_MENU	0x0085
#define HUG_SYSTEM_APP_MENU	0x0086
#define HUG_SYSTEM_MENU_HELP	0x0087
#define HUG_SYSTEM_MENU_EXIT	0x0088
#define HUG_SYSTEM_MENU_SELECT	0x0089
#define HUG_SYSTEM_MENU_RIGHT	0x008a
#define HUG_SYSTEM_MENU_LEFT	0x008b
#define HUG_SYSTEM_MENU_UP	0x008c
#define HUG_SYSTEM_MENU_DOWN	0x008d

/* Usages, Digitizers */
#define HUD_UNDEFINED		0x0000
#define HUD_DIGITIZER		0x0001
#define HUD_PEN			0x0002
#define HUD_TOUCHSCREEN		0x0004
#define HUD_TOUCHPAD		0x0005
#define HUD_CONFIG		0x000e
#define HUD_STYLUS		0x0020
#define HUD_FINGER		0x0022
#define HUD_TIP_PRESSURE	0x0030
#define HUD_BARREL_PRESSURE	0x0031
#define HUD_IN_RANGE		0x0032
#define HUD_TOUCH		0x0033
#define HUD_UNTOUCH		0x0034
#define HUD_TAP			0x0035
#define HUD_QUALITY		0x0036
#define HUD_DATA_VALID		0x0037
#define HUD_TRANSDUCER_INDEX	0x0038
#define HUD_TABLET_FKEYS	0x0039
#define HUD_PROGRAM_CHANGE_KEYS	0x003a
#define HUD_BATTERY_STRENGTH	0x003b
#define HUD_INVERT		0x003c
#define HUD_X_TILT		0x003d
#define HUD_Y_TILT		0x003e
#define HUD_AZIMUTH		0x003f
#define HUD_ALTITUDE		0x0040
#define HUD_TWIST		0x0041
#define HUD_TIP_SWITCH		0x0042
#define HUD_SEC_TIP_SWITCH	0x0043
#define HUD_BARREL_SWITCH	0x0044
#define HUD_ERASER		0x0045
#define HUD_TABLET_PICK		0x0046
#define HUD_CONFIDENCE		0x0047
#define HUD_WIDTH		0x0048
#define HUD_HEIGHT		0x0049
#define HUD_CONTACTID		0x0051
#define HUD_INPUT_MODE		0x0052
#define HUD_DEVICE_INDEX	0x0053
#define HUD_CONTACTCOUNT	0x0054
#define HUD_CONTACT_MAX		0x0055
#define HUD_SCAN_TIME		0x0056
#define HUD_BUTTON_TYPE		0x0059
#define HUD_SECONDARY_BARREL_SWITCH	0x005A
#define HUD_WACOM_X		0x0130
#define HUD_WACOM_Y		0x0131
#define HUD_WACOM_DISTANCE		0x0132
#define HUD_WACOM_PAD_BUTTONS00		0x0910
#define HUD_WACOM_BATTERY		0x1013

/* Usages, LED */
#define HUL_NUM_LOCK		0x0001
#define HUL_CAPS_LOCK		0x0002
#define HUL_SCROLL_LOCK		0x0003
#define HUL_COMPOSE		0x0004
#define HUL_KANA		0x0005

/* Usages, Consumer */
#define HUC_CONTROL		0x0001
#define HUC_TRACK_NEXT		0x00b5
#define HUC_TRACK_PREV		0x00b6
#define HUC_STOP		0x00b7
#define HUC_PLAY_PAUSE		0x00cd
#define HUC_VOLUME		0x00e0
#define HUC_MUTE		0x00e2
#define HUC_VOL_INC		0x00e9
#define HUC_VOL_DEC		0x00ea
#define HUC_AC_PAN		0x0238

/* Usages, FIDO */
#define HUF_U2FHID		0x0001
#define HUF_RAW_IN_DATA_REPORT	0x0020
#define HUF_RAW_OUT_DATA_REPORT	0x0021

#define HID_USAGE2(p, u) (((p) << 16) | u)
#define HID_GET_USAGE(u) ((u) & 0xffff)
#define HID_GET_USAGE_PAGE(u) (((u) >> 16) & 0xffff)

#define HCOLL_PHYSICAL		0
#define HCOLL_APPLICATION	1
#define HCOLL_LOGICAL		2

/* Bits in the input/output/feature items */
#define HIO_CONST	0x001
#define HIO_VARIABLE	0x002
#define HIO_RELATIVE	0x004
#define HIO_WRAP	0x008
#define HIO_NONLINEAR	0x010
#define HIO_NOPREF	0x020
#define HIO_NULLSTATE	0x040
#define HIO_VOLATILE	0x080
#define HIO_BUFBYTES	0x100

/* Valid values for the country codes */
#define	HCC_UNDEFINED	0x00
#define	HCC_MAX		0x23

#endif /* _HIDHID_H_ */
