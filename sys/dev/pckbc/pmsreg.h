/* $OpenBSD: pmsreg.h,v 1.18 2020/03/18 22:38:10 bru Exp $ */
/* $NetBSD: psmreg.h,v 1.1 1998/03/22 15:41:28 drochner Exp $ */

#ifndef SYS_DEV_PCKBC_PMSREG_H
#define SYS_DEV_PCKBC_PMSREG_H

/* mouse commands */
#define PMS_SET_SCALE11		0xe6	/* set scaling 1:1 */
#define PMS_SET_SCALE21		0xe7	/* set scaling 2:1 */
#define PMS_SET_RES		0xe8	/* set resolution (0..3) */
#define PMS_SEND_DEV_STATUS	0xe9	/* status request */
#define PMS_SET_STREAM_MODE	0xea
#define PMS_SEND_DEV_DATA	0xeb	/* read data */
#define PMS_RESET_WRAP_MODE	0xec
#define PMS_SET_WRAP_MODE	0xed
#define PMS_SET_REMOTE_MODE	0xf0
#define PMS_SEND_DEV_ID		0xf2	/* read device type */
#define PMS_SET_SAMPLE		0xf3	/* set sampling rate */
#define PMS_DEV_ENABLE		0xf4	/* mouse on */
#define PMS_DEV_DISABLE		0xf5	/* mouse off */
#define PMS_SET_DEFAULTS	0xf6
#define PMS_RESEND		0xfe
#define PMS_RESET		0xff	/* reset */

#define PMS_RSTDONE		0xaa

/* PS/2 mouse data packet */
#define PMS_PS2_BUTTONSMASK	0x07
#define PMS_PS2_BUTTON1		0x01	/* left */
#define PMS_PS2_BUTTON2		0x04	/* middle */
#define PMS_PS2_BUTTON3		0x02	/* right */
#define PMS_PS2_XNEG		0x10
#define PMS_PS2_YNEG		0x20

#define PMS_INTELLI_MAGIC1	200
#define PMS_INTELLI_MAGIC2	100
#define PMS_INTELLI_MAGIC3	80
#define PMS_INTELLI_ID		0x03

#define PMS_ALPS_MAGIC1		0
#define PMS_ALPS_MAGIC2		0
#define PMS_ALPS_MAGIC3_1	10
#define PMS_ALPS_MAGIC3_2	80
#define PMS_ALPS_MAGIC3_3	100

#define PMS_ELANTECH_MAGIC1	0x3c
#define PMS_ELANTECH_MAGIC2	0x03
#define PMS_ELANTECH_MAGIC3_1	0xc8
#define PMS_ELANTECH_MAGIC3_2	0x00

/*
 * Checking for almost-standard PS/2 packet
 * Note: ALPS devices never signal overflow condition
 */
#define PMS_ALPS_PS2_MASK			0xc8
#define PMS_ALPS_PS2_VALID			0x08

/* Checking for interleaved packet */
#define PMS_ALPS_INTERLEAVED_MASK		0xcf
#define PMS_ALPS_INTERLEAVED_VALID		0x0f

/* Checking for non first byte */
#define PMS_ALPS_MASK				0x80
#define PMS_ALPS_VALID				0x00

/* Synaptics queries */
#define SYNAPTICS_QUE_IDENTIFY			0x00
#define SYNAPTICS_QUE_MODES			0x01
#define SYNAPTICS_QUE_CAPABILITIES		0x02
#define SYNAPTICS_QUE_MODEL			0x03
#define SYNAPTICS_QUE_SERIAL_NUMBER_PREFIX	0x06
#define SYNAPTICS_QUE_SERIAL_NUMBER_SUFFIX	0x07
#define SYNAPTICS_QUE_RESOLUTION		0x08
#define SYNAPTICS_QUE_EXT_MODEL			0x09
#define SYNAPTICS_QUE_EXT_CAPABILITIES		0x0c
#define SYNAPTICS_QUE_EXT_MAX_COORDS		0x0d
#define SYNAPTICS_QUE_EXT_MIN_COORDS		0x0f
#define SYNAPTICS_QUE_EXT2_CAPABILITIES		0x10

#define SYNAPTICS_CMD_SET_MODE			0x14
#define SYNAPTICS_CMD_SEND_CLIENT		0x28
#define SYNAPTICS_CMD_SET_ADV_GESTURE_MODE	0xc8

/* Identify */
#define SYNAPTICS_ID_MODEL(id)			(((id) >>  4) & 0x0f)
#define SYNAPTICS_ID_MINOR(id)			(((id) >> 16) & 0xff)
#define SYNAPTICS_ID_MAJOR(id)			((id) & 0x0f)
#define SYNAPTICS_ID_FULL(id) \
	(SYNAPTICS_ID_MAJOR(id) << 8 | SYNAPTICS_ID_MINOR(id))
#define SYNAPTICS_ID_MAGIC			0x47

/* Modes bits */
#define SYNAPTICS_EXT2_CAP			(1 << 17)
#define SYNAPTICS_ABSOLUTE_MODE			(1 << 7)
#define SYNAPTICS_HIGH_RATE			(1 << 6)
#define SYNAPTICS_SLEEP_MODE			(1 << 3)
#define SYNAPTICS_DISABLE_GESTURE		(1 << 2)
#define SYNAPTICS_FOUR_BYTE_CLIENT		(1 << 1)
#define SYNAPTICS_W_MODE			(1 << 0)

/* Capability bits */
#define SYNAPTICS_CAP_EXTENDED			(1 << 23)
#define SYNAPTICS_CAP_EXTENDED_QUERIES(c)	(((c) >> 20) & 0x07)
#define SYNAPTICS_CAP_MIDDLE_BUTTON		(1 << 18)
#define SYNAPTICS_CAP_PASSTHROUGH		(1 << 7)
#define SYNAPTICS_CAP_SLEEP			(1 << 4)
#define SYNAPTICS_CAP_FOUR_BUTTON		(1 << 3)
#define SYNAPTICS_CAP_BALLISTICS		(1 << 2)
#define SYNAPTICS_CAP_MULTIFINGER		(1 << 1)
#define SYNAPTICS_CAP_PALMDETECT		(1 << 0)

/* Model ID bits */
#define SYNAPTICS_MODEL_ROT180			(1 << 23)
#define SYNAPTICS_MODEL_PORTRAIT		(1 << 22)
#define SYNAPTICS_MODEL_SENSOR(m)		(((m) >> 16) & 0x3f)
#define SYNAPTICS_MODEL_HARDWARE(m)		(((m) >> 9) & 0x7f)
#define SYNAPTICS_MODEL_NEWABS			(1 << 7)
#define SYNAPTICS_MODEL_PEN			(1 << 6)
#define SYNAPTICS_MODEL_SIMPLC			(1 << 5)
#define SYNAPTICS_MODEL_GEOMETRY(m)		((m) & 0x0f)

/* Resolutions */
#define SYNAPTICS_RESOLUTION_VALID		(1 << 15)
#define SYNAPTICS_RESOLUTION_X(r)		(((r) >> 16) & 0xff)
#define SYNAPTICS_RESOLUTION_Y(r)		((r) & 0xff)

/* Extended Model ID bits */
#define SYNAPTICS_EXT_MODEL_LIGHTCONTROL	(1 << 22)
#define SYNAPTICS_EXT_MODEL_PEAKDETECT		(1 << 21)
#define SYNAPTICS_EXT_MODEL_VWHEEL		(1 << 19)
#define SYNAPTICS_EXT_MODEL_EW_MODE		(1 << 18)
#define SYNAPTICS_EXT_MODEL_HSCROLL		(1 << 17)
#define SYNAPTICS_EXT_MODEL_VSCROLL		(1 << 16)
#define SYNAPTICS_EXT_MODEL_BUTTONS(em)		((em >> 12) & 0x0f)
#define SYNAPTICS_EXT_MODEL_SENSOR(em)		((em >> 10) & 0x03)
#define SYNAPTICS_EXT_MODEL_PRODUCT(em)		((em) & 0xff)

/* Extended Capability bits */
#define SYNAPTICS_EXT_CAP_CLICKPAD		(1 << 20)
#define SYNAPTICS_EXT_CAP_ADV_GESTURE		(1 << 19)
#define SYNAPTICS_EXT_CAP_MAX_COORDS		(1 << 17)
#define SYNAPTICS_EXT_CAP_MIN_COORDS		(1 << 13)
#define SYNAPTICS_EXT_CAP_REPORTS_V		(1 << 11)
#define SYNAPTICS_EXT_CAP_CLICKPAD_2BTN		(1 << 8)

#define SYNAPTICS_SUPPORTS_AGM(extcaps) ((extcaps) & \
    (SYNAPTICS_EXT_CAP_ADV_GESTURE | SYNAPTICS_EXT_CAP_REPORTS_V))

/* Coordinate Limits */
#define SYNAPTICS_X_LIMIT(d)			((((d) & 0xff0000) >> 11) | \
						 (((d) & 0xf00) >> 7))
#define SYNAPTICS_Y_LIMIT(d)			((((d) & 0xff) << 5) | \
						 (((d) & 0xf000) >> 11))

/* Extended Capability 2 */
#define SYNAPTICS_EXT2_CAP_BUTTONS_STICK	(1 << 16)

/* Typical bezel limit */
#define SYNAPTICS_XMIN_BEZEL			1472
#define SYNAPTICS_XMAX_BEZEL			5472
#define SYNAPTICS_YMIN_BEZEL			1408
#define SYNAPTICS_YMAX_BEZEL			4448

#define ALPS_XMIN_BEZEL				0
#define ALPS_XMAX_BEZEL				1023
#define ALPS_YMIN_BEZEL				0
#define ALPS_YMAX_BEZEL				767

#define ALPS_XSEC_BEZEL				768
#define ALPS_YSEC_BEZEL				512

#define ALPS_Z_MAGIC				127

/* ALPS "gesture" and "finger" bits */
#define ALPS_TAP				0x01
#define ALPS_DRAG				0x03

/* Elantech queries */
#define ELANTECH_QUE_FW_ID			0
#define ELANTECH_QUE_FW_VER			1
#define ELANTECH_QUE_CAPABILITIES		2
#define ELANTECH_QUE_SAMPLE			3
#define ELANTECH_QUE_RESOLUTION			4

/* Elantech capabilities */
#define ELANTECH_CAP_HAS_ROCKER			4
#define ELANTECH_CAP_TRACKPOINT			0x80

#define ELANTECH_PS2_CUSTOM_COMMAND		0xf8

#define ELANTECH_CMD_READ_REG			0x10
#define ELANTECH_CMD_WRITE_REG			0x11
#define ELANTECH_CMD_READ_WRITE_REG		0x00

#define ELANTECH_ABSOLUTE_MODE			0x04

/* Hardware version 1 has hard-coded axis range values.
 * X axis range is 0 to 576, Y axis range is 0 to 384.
 * Edge offset accounts for bezel around the touchpad. */
#define ELANTECH_V1_EDGE_OFFSET	32
#define ELANTECH_V1_X_MIN	(0 + ELANTECH_V1_EDGE_OFFSET)
#define ELANTECH_V1_X_MAX	(576 - ELANTECH_V1_EDGE_OFFSET)
#define ELANTECH_V1_Y_MIN	(0 + ELANTECH_V1_EDGE_OFFSET)
#define ELANTECH_V1_Y_MAX	(384 - ELANTECH_V1_EDGE_OFFSET)

/* Older hardware version 2 variants lack ID query capability. */
#define ELANTECH_V2_X_MAX	1152
#define ELANTECH_V2_Y_MAX	768

/* V4 */
#define ELANTECH_MAX_FINGERS			5
#define ELANTECH_V4_WEIGHT_VALUE		5

#define ELANTECH_V4_PKT_STATUS			0
#define ELANTECH_V4_PKT_HEAD			0x01
#define ELANTECH_V4_PKT_MOTION			0x02

/* V3 and V4 may be coupled with trackpoints, pms supports them for V4. */
#define ELANTECH_PKT_TRACKPOINT			0x06

#endif /* SYS_DEV_PCKBC_PMSREG_H */
