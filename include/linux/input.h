#ifndef _INPUT_H
#define _INPUT_H

/*
 * Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifdef __KERNEL__
#include <linux/time.h>
#include <linux/list.h>
#else
#include <sys/time.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#endif

/*
 * The event structure itself
 */

struct input_event {
	struct timeval time;
	__u16 type;
	__u16 code;
	__s32 value;
};

/*
 * Protocol version.
 */

#define EV_VERSION		0x010000

/*
 * IOCTLs (0x00 - 0x7f)
 */

struct input_id {
	__u16 bustype;
	__u16 vendor;
	__u16 product;
	__u16 version;
};

struct input_absinfo {
	__s32 value;
	__s32 minimum;
	__s32 maximum;
	__s32 fuzz;
	__s32 flat;
};

#define EVIOCGVERSION		_IOR('E', 0x01, int)			/* get driver version */
#define EVIOCGID		_IOR('E', 0x02, struct input_id)	/* get device ID */
#define EVIOCGKEYCODE		_IOR('E', 0x04, int[2])			/* get keycode */
#define EVIOCSKEYCODE		_IOW('E', 0x04, int[2])			/* set keycode */

#define EVIOCGNAME(len)		_IOC(_IOC_READ, 'E', 0x06, len)		/* get device name */
#define EVIOCGPHYS(len)		_IOC(_IOC_READ, 'E', 0x07, len)		/* get physical location */
#define EVIOCGUNIQ(len)		_IOC(_IOC_READ, 'E', 0x08, len)		/* get unique identifier */

#define EVIOCGKEY(len)		_IOC(_IOC_READ, 'E', 0x18, len)		/* get global keystate */
#define EVIOCGLED(len)		_IOC(_IOC_READ, 'E', 0x19, len)		/* get all LEDs */
#define EVIOCGSND(len)		_IOC(_IOC_READ, 'E', 0x1a, len)		/* get all sounds status */
#define EVIOCGSW(len)		_IOC(_IOC_READ, 'E', 0x1b, len)		/* get all switch states */

#define EVIOCGBIT(ev,len)	_IOC(_IOC_READ, 'E', 0x20 + ev, len)	/* get event bits */
#define EVIOCGABS(abs)		_IOR('E', 0x40 + abs, struct input_absinfo)		/* get abs value/limits */
#define EVIOCSABS(abs)		_IOW('E', 0xc0 + abs, struct input_absinfo)		/* set abs value/limits */

#define EVIOCSFF		_IOC(_IOC_WRITE, 'E', 0x80, sizeof(struct ff_effect))	/* send a force effect to a force feedback device */
#define EVIOCRMFF		_IOW('E', 0x81, int)			/* Erase a force effect */
#define EVIOCGEFFECTS		_IOR('E', 0x84, int)			/* Report number of effects playable at the same time */

#define EVIOCGRAB		_IOW('E', 0x90, int)			/* Grab/Release device */

/*
 * Event types
 */

#define EV_SYN			0x00
#define EV_KEY			0x01
#define EV_REL			0x02
#define EV_ABS			0x03
#define EV_MSC			0x04
#define EV_SW			0x05
#define EV_LED			0x11
#define EV_SND			0x12
#define EV_REP			0x14
#define EV_FF			0x15
#define EV_PWR			0x16
#define EV_FF_STATUS		0x17
#define EV_MAX			0x1f

/*
 * Synchronization events.
 */

#define SYN_REPORT		0
#define SYN_CONFIG		1

/*
 * Keys and buttons
 */

#define KEY_RESERVED		0
#define KEY_ESC			1
#define KEY_1			2
#define KEY_2			3
#define KEY_3			4
#define KEY_4			5
#define KEY_5			6
#define KEY_6			7
#define KEY_7			8
#define KEY_8			9
#define KEY_9			10
#define KEY_0			11
#define KEY_MINUS		12
#define KEY_EQUAL		13
#define KEY_BACKSPACE		14
#define KEY_TAB			15
#define KEY_Q			16
#define KEY_W			17
#define KEY_E			18
#define KEY_R			19
#define KEY_T			20
#define KEY_Y			21
#define KEY_U			22
#define KEY_I			23
#define KEY_O			24
#define KEY_P			25
#define KEY_LEFTBRACE		26
#define KEY_RIGHTBRACE		27
#define KEY_ENTER		28
#define KEY_LEFTCTRL		29
#define KEY_A			30
#define KEY_S			31
#define KEY_D			32
#define KEY_F			33
#define KEY_G			34
#define KEY_H			35
#define KEY_J			36
#define KEY_K			37
#define KEY_L			38
#define KEY_SEMICOLON		39
#define KEY_APOSTROPHE		40
#define KEY_GRAVE		41
#define KEY_LEFTSHIFT		42
#define KEY_BACKSLASH		43
#define KEY_Z			44
#define KEY_X			45
#define KEY_C			46
#define KEY_V			47
#define KEY_B			48
#define KEY_N			49
#define KEY_M			50
#define KEY_COMMA		51
#define KEY_DOT			52
#define KEY_SLASH		53
#define KEY_RIGHTSHIFT		54
#define KEY_KPASTERISK		55
#define KEY_LEFTALT		56
#define KEY_SPACE		57
#define KEY_CAPSLOCK		58
#define KEY_F1			59
#define KEY_F2			60
#define KEY_F3			61
#define KEY_F4			62
#define KEY_F5			63
#define KEY_F6			64
#define KEY_F7			65
#define KEY_F8			66
#define KEY_F9			67
#define KEY_F10			68
#define KEY_NUMLOCK		69
#define KEY_SCROLLLOCK		70
#define KEY_KP7			71
#define KEY_KP8			72
#define KEY_KP9			73
#define KEY_KPMINUS		74
#define KEY_KP4			75
#define KEY_KP5			76
#define KEY_KP6			77
#define KEY_KPPLUS		78
#define KEY_KP1			79
#define KEY_KP2			80
#define KEY_KP3			81
#define KEY_KP0			82
#define KEY_KPDOT		83

#define KEY_ZENKAKUHANKAKU	85
#define KEY_102ND		86
#define KEY_F11			87
#define KEY_F12			88
#define KEY_RO			89
#define KEY_KATAKANA		90
#define KEY_HIRAGANA		91
#define KEY_HENKAN		92
#define KEY_KATAKANAHIRAGANA	93
#define KEY_MUHENKAN		94
#define KEY_KPJPCOMMA		95
#define KEY_KPENTER		96
#define KEY_RIGHTCTRL		97
#define KEY_KPSLASH		98
#define KEY_SYSRQ		99
#define KEY_RIGHTALT		100
#define KEY_LINEFEED		101
#define KEY_HOME		102
#define KEY_UP			103
#define KEY_PAGEUP		104
#define KEY_LEFT		105
#define KEY_RIGHT		106
#define KEY_END			107
#define KEY_DOWN		108
#define KEY_PAGEDOWN		109
#define KEY_INSERT		110
#define KEY_DELETE		111
#define KEY_MACRO		112
#define KEY_MUTE		113
#define KEY_VOLUMEDOWN		114
#define KEY_VOLUMEUP		115
#define KEY_POWER		116
#define KEY_KPEQUAL		117
#define KEY_KPPLUSMINUS		118
#define KEY_PAUSE		119

#define KEY_KPCOMMA		121
#define KEY_HANGUEL		122
#define KEY_HANJA		123
#define KEY_YEN			124
#define KEY_LEFTMETA		125
#define KEY_RIGHTMETA		126
#define KEY_COMPOSE		127

#define KEY_STOP		128
#define KEY_AGAIN		129
#define KEY_PROPS		130
#define KEY_UNDO		131
#define KEY_FRONT		132
#define KEY_COPY		133
#define KEY_OPEN		134
#define KEY_PASTE		135
#define KEY_FIND		136
#define KEY_CUT			137
#define KEY_HELP		138
#define KEY_MENU		139
#define KEY_CALC		140
#define KEY_SETUP		141
#define KEY_SLEEP		142
#define KEY_WAKEUP		143
#define KEY_FILE		144
#define KEY_SENDFILE		145
#define KEY_DELETEFILE		146
#define KEY_XFER		147
#define KEY_PROG1		148
#define KEY_PROG2		149
#define KEY_WWW			150
#define KEY_MSDOS		151
#define KEY_COFFEE		152
#define KEY_DIRECTION		153
#define KEY_CYCLEWINDOWS	154
#define KEY_MAIL		155
#define KEY_BOOKMARKS		156
#define KEY_COMPUTER		157
#define KEY_BACK		158
#define KEY_FORWARD		159
#define KEY_CLOSECD		160
#define KEY_EJECTCD		161
#define KEY_EJECTCLOSECD	162
#define KEY_NEXTSONG		163
#define KEY_PLAYPAUSE		164
#define KEY_PREVIOUSSONG	165
#define KEY_STOPCD		166
#define KEY_RECORD		167
#define KEY_REWIND		168
#define KEY_PHONE		169
#define KEY_ISO			170
#define KEY_CONFIG		171
#define KEY_HOMEPAGE		172
#define KEY_REFRESH		173
#define KEY_EXIT		174
#define KEY_MOVE		175
#define KEY_EDIT		176
#define KEY_SCROLLUP		177
#define KEY_SCROLLDOWN		178
#define KEY_KPLEFTPAREN		179
#define KEY_KPRIGHTPAREN	180
#define KEY_NEW			181
#define KEY_REDO		182

#define KEY_F13			183
#define KEY_F14			184
#define KEY_F15			185
#define KEY_F16			186
#define KEY_F17			187
#define KEY_F18			188
#define KEY_F19			189
#define KEY_F20			190
#define KEY_F21			191
#define KEY_F22			192
#define KEY_F23			193
#define KEY_F24			194

#define KEY_PLAYCD		200
#define KEY_PAUSECD		201
#define KEY_PROG3		202
#define KEY_PROG4		203
#define KEY_SUSPEND		205
#define KEY_CLOSE		206
#define KEY_PLAY		207
#define KEY_FASTFORWARD		208
#define KEY_BASSBOOST		209
#define KEY_PRINT		210
#define KEY_HP			211
#define KEY_CAMERA		212
#define KEY_SOUND		213
#define KEY_QUESTION		214
#define KEY_EMAIL		215
#define KEY_CHAT		216
#define KEY_SEARCH		217
#define KEY_CONNECT		218
#define KEY_FINANCE		219
#define KEY_SPORT		220
#define KEY_SHOP		221
#define KEY_ALTERASE		222
#define KEY_CANCEL		223
#define KEY_BRIGHTNESSDOWN	224
#define KEY_BRIGHTNESSUP	225
#define KEY_MEDIA		226

#define KEY_SWITCHVIDEOMODE	227
#define KEY_KBDILLUMTOGGLE	228
#define KEY_KBDILLUMDOWN	229
#define KEY_KBDILLUMUP		230

#define KEY_SEND		231
#define KEY_REPLY		232
#define KEY_FORWARDMAIL		233
#define KEY_SAVE		234
#define KEY_DOCUMENTS		235

#define KEY_UNKNOWN		240

#define BTN_MISC		0x100
#define BTN_0			0x100
#define BTN_1			0x101
#define BTN_2			0x102
#define BTN_3			0x103
#define BTN_4			0x104
#define BTN_5			0x105
#define BTN_6			0x106
#define BTN_7			0x107
#define BTN_8			0x108
#define BTN_9			0x109

#define BTN_MOUSE		0x110
#define BTN_LEFT		0x110
#define BTN_RIGHT		0x111
#define BTN_MIDDLE		0x112
#define BTN_SIDE		0x113
#define BTN_EXTRA		0x114
#define BTN_FORWARD		0x115
#define BTN_BACK		0x116
#define BTN_TASK		0x117

#define BTN_JOYSTICK		0x120
#define BTN_TRIGGER		0x120
#define BTN_THUMB		0x121
#define BTN_THUMB2		0x122
#define BTN_TOP			0x123
#define BTN_TOP2		0x124
#define BTN_PINKIE		0x125
#define BTN_BASE		0x126
#define BTN_BASE2		0x127
#define BTN_BASE3		0x128
#define BTN_BASE4		0x129
#define BTN_BASE5		0x12a
#define BTN_BASE6		0x12b
#define BTN_DEAD		0x12f

#define BTN_GAMEPAD		0x130
#define BTN_A			0x130
#define BTN_B			0x131
#define BTN_C			0x132
#define BTN_X			0x133
#define BTN_Y			0x134
#define BTN_Z			0x135
#define BTN_TL			0x136
#define BTN_TR			0x137
#define BTN_TL2			0x138
#define BTN_TR2			0x139
#define BTN_SELECT		0x13a
#define BTN_START		0x13b
#define BTN_MODE		0x13c
#define BTN_THUMBL		0x13d
#define BTN_THUMBR		0x13e

#define BTN_DIGI		0x140
#define BTN_TOOL_PEN		0x140
#define BTN_TOOL_RUBBER		0x141
#define BTN_TOOL_BRUSH		0x142
#define BTN_TOOL_PENCIL		0x143
#define BTN_TOOL_AIRBRUSH	0x144
#define BTN_TOOL_FINGER		0x145
#define BTN_TOOL_MOUSE		0x146
#define BTN_TOOL_LENS		0x147
#define BTN_TOUCH		0x14a
#define BTN_STYLUS		0x14b
#define BTN_STYLUS2		0x14c
#define BTN_TOOL_DOUBLETAP	0x14d
#define BTN_TOOL_TRIPLETAP	0x14e

#define BTN_WHEEL		0x150
#define BTN_GEAR_DOWN		0x150
#define BTN_GEAR_UP		0x151

#define KEY_OK			0x160
#define KEY_SELECT 		0x161
#define KEY_GOTO		0x162
#define KEY_CLEAR		0x163
#define KEY_POWER2		0x164
#define KEY_OPTION		0x165
#define KEY_INFO		0x166
#define KEY_TIME		0x167
#define KEY_VENDOR		0x168
#define KEY_ARCHIVE		0x169
#define KEY_PROGRAM		0x16a
#define KEY_CHANNEL		0x16b
#define KEY_FAVORITES		0x16c
#define KEY_EPG			0x16d
#define KEY_PVR			0x16e
#define KEY_MHP			0x16f
#define KEY_LANGUAGE		0x170
#define KEY_TITLE		0x171
#define KEY_SUBTITLE		0x172
#define KEY_ANGLE		0x173
#define KEY_ZOOM		0x174
#define KEY_MODE		0x175
#define KEY_KEYBOARD		0x176
#define KEY_SCREEN		0x177
#define KEY_PC			0x178
#define KEY_TV			0x179
#define KEY_TV2			0x17a
#define KEY_VCR			0x17b
#define KEY_VCR2		0x17c
#define KEY_SAT			0x17d
#define KEY_SAT2		0x17e
#define KEY_CD			0x17f
#define KEY_TAPE		0x180
#define KEY_RADIO		0x181
#define KEY_TUNER		0x182
#define KEY_PLAYER		0x183
#define KEY_TEXT		0x184
#define KEY_DVD			0x185
#define KEY_AUX			0x186
#define KEY_MP3			0x187
#define KEY_AUDIO		0x188
#define KEY_VIDEO		0x189
#define KEY_DIRECTORY		0x18a
#define KEY_LIST		0x18b
#define KEY_MEMO		0x18c
#define KEY_CALENDAR		0x18d
#define KEY_RED			0x18e
#define KEY_GREEN		0x18f
#define KEY_YELLOW		0x190
#define KEY_BLUE		0x191
#define KEY_CHANNELUP		0x192
#define KEY_CHANNELDOWN		0x193
#define KEY_FIRST		0x194
#define KEY_LAST		0x195
#define KEY_AB			0x196
#define KEY_NEXT		0x197
#define KEY_RESTART		0x198
#define KEY_SLOW		0x199
#define KEY_SHUFFLE		0x19a
#define KEY_BREAK		0x19b
#define KEY_PREVIOUS		0x19c
#define KEY_DIGITS		0x19d
#define KEY_TEEN		0x19e
#define KEY_TWEN		0x19f

#define KEY_DEL_EOL		0x1c0
#define KEY_DEL_EOS		0x1c1
#define KEY_INS_LINE		0x1c2
#define KEY_DEL_LINE		0x1c3

#define KEY_FN			0x1d0
#define KEY_FN_ESC		0x1d1
#define KEY_FN_F1		0x1d2
#define KEY_FN_F2		0x1d3
#define KEY_FN_F3		0x1d4
#define KEY_FN_F4		0x1d5
#define KEY_FN_F5		0x1d6
#define KEY_FN_F6		0x1d7
#define KEY_FN_F7		0x1d8
#define KEY_FN_F8		0x1d9
#define KEY_FN_F9		0x1da
#define KEY_FN_F10		0x1db
#define KEY_FN_F11		0x1dc
#define KEY_FN_F12		0x1dd
#define KEY_FN_1		0x1de
#define KEY_FN_2		0x1df
#define KEY_FN_D		0x1e0
#define KEY_FN_E		0x1e1
#define KEY_FN_F		0x1e2
#define KEY_FN_S		0x1e3
#define KEY_FN_B		0x1e4

#define KEY_MAX			0x1ff

/*
 * Relative axes
 */

#define REL_X			0x00
#define REL_Y			0x01
#define REL_Z			0x02
#define REL_RX			0x03
#define REL_RY			0x04
#define REL_RZ			0x05
#define REL_HWHEEL		0x06
#define REL_DIAL		0x07
#define REL_WHEEL		0x08
#define REL_MISC		0x09
#define REL_MAX			0x0f

/*
 * Absolute axes
 */

#define ABS_X			0x00
#define ABS_Y			0x01
#define ABS_Z			0x02
#define ABS_RX			0x03
#define ABS_RY			0x04
#define ABS_RZ			0x05
#define ABS_THROTTLE		0x06
#define ABS_RUDDER		0x07
#define ABS_WHEEL		0x08
#define ABS_GAS			0x09
#define ABS_BRAKE		0x0a
#define ABS_HAT0X		0x10
#define ABS_HAT0Y		0x11
#define ABS_HAT1X		0x12
#define ABS_HAT1Y		0x13
#define ABS_HAT2X		0x14
#define ABS_HAT2Y		0x15
#define ABS_HAT3X		0x16
#define ABS_HAT3Y		0x17
#define ABS_PRESSURE		0x18
#define ABS_DISTANCE		0x19
#define ABS_TILT_X		0x1a
#define ABS_TILT_Y		0x1b
#define ABS_TOOL_WIDTH		0x1c
#define ABS_VOLUME		0x20
#define ABS_MISC		0x28
#define ABS_MAX			0x3f

/*
 * Switch events
 */

#define SW_0		0x00
#define SW_1		0x01
#define SW_2		0x02
#define SW_3		0x03
#define SW_4		0x04
#define SW_5		0x05
#define SW_6		0x06
#define SW_7		0x07
#define SW_MAX		0x0f

/*
 * Misc events
 */

#define MSC_SERIAL		0x00
#define MSC_PULSELED		0x01
#define MSC_GESTURE		0x02
#define MSC_RAW			0x03
#define MSC_SCAN		0x04
#define MSC_MAX			0x07

/*
 * LEDs
 */

#define LED_NUML		0x00
#define LED_CAPSL		0x01
#define LED_SCROLLL		0x02
#define LED_COMPOSE		0x03
#define LED_KANA		0x04
#define LED_SLEEP		0x05
#define LED_SUSPEND		0x06
#define LED_MUTE		0x07
#define LED_MISC		0x08
#define LED_MAIL		0x09
#define LED_CHARGING		0x0a
#define LED_MAX			0x0f

/*
 * Autorepeat values
 */

#define REP_DELAY		0x00
#define REP_PERIOD		0x01
#define REP_MAX			0x01

/*
 * Sounds
 */

#define SND_CLICK		0x00
#define SND_BELL		0x01
#define SND_TONE		0x02
#define SND_MAX			0x07

/*
 * IDs.
 */

#define ID_BUS			0
#define ID_VENDOR		1
#define ID_PRODUCT		2
#define ID_VERSION		3

#define BUS_PCI			0x01
#define BUS_ISAPNP		0x02
#define BUS_USB			0x03
#define BUS_HIL			0x04
#define BUS_BLUETOOTH		0x05

#define BUS_ISA			0x10
#define BUS_I8042		0x11
#define BUS_XTKBD		0x12
#define BUS_RS232		0x13
#define BUS_GAMEPORT		0x14
#define BUS_PARPORT		0x15
#define BUS_AMIGA		0x16
#define BUS_ADB			0x17
#define BUS_I2C			0x18
#define BUS_HOST		0x19

/*
 * Values describing the status of an effect
 */
#define FF_STATUS_STOPPED	0x00
#define FF_STATUS_PLAYING	0x01
#define FF_STATUS_MAX		0x01

/*
 * Structures used in ioctls to upload effects to a device
 * The first structures are not passed directly by using ioctls.
 * They are sub-structures of the actually sent structure (called ff_effect)
 */

struct ff_replay {
	__u16 length; /* Duration of an effect in ms. All other times are also expressed in ms */
	__u16 delay;  /* Time to wait before to start playing an effect */
};

struct ff_trigger {
	__u16 button;   /* Number of button triggering an effect */
	__u16 interval; /* Time to wait before an effect can be re-triggered (ms) */
};

struct ff_envelope {
	__u16 attack_length;	/* Duration of attack (ms) */
	__u16 attack_level;	/* Level at beginning of attack */
	__u16 fade_length;	/* Duration of fade (ms) */
	__u16 fade_level;	/* Level at end of fade */
};

/* FF_CONSTANT */
struct ff_constant_effect {
	__s16 level;	    /* Strength of effect. Negative values are OK */
	struct ff_envelope envelope;
};

/* FF_RAMP */
struct ff_ramp_effect {
	__s16 start_level;
	__s16 end_level;
	struct ff_envelope envelope;
};

/* FF_SPRING of FF_FRICTION */
struct ff_condition_effect {
	__u16 right_saturation; /* Max level when joystick is on the right */
	__u16 left_saturation;  /* Max level when joystick in on the left */

	__s16 right_coeff;	/* Indicates how fast the force grows when the
				   joystick moves to the right */
	__s16 left_coeff;	/* Same for left side */

	__u16 deadband;	/* Size of area where no force is produced */
	__s16 center;	/* Position of dead zone */

};

/* FF_PERIODIC */
struct ff_periodic_effect {
	__u16 waveform;	/* Kind of wave (sine, square...) */
	__u16 period;	/* in ms */
	__s16 magnitude;	/* Peak value */
	__s16 offset;	/* Mean value of wave (roughly) */
	__u16 phase;		/* 'Horizontal' shift */

	struct ff_envelope envelope;

/* Only used if waveform == FF_CUSTOM */
	__u32 custom_len;	/* Number of samples */
	__s16 *custom_data;	/* Buffer of samples */
/* Note: the data pointed by custom_data is copied by the driver. You can
 * therefore dispose of the memory after the upload/update */
};

/* FF_RUMBLE */
/* Some rumble pads have two motors of different weight.
   strong_magnitude represents the magnitude of the vibration generated
   by the heavy motor.
*/
struct ff_rumble_effect {
	__u16 strong_magnitude;  /* Magnitude of the heavy motor */
	__u16 weak_magnitude;    /* Magnitude of the light one */
};

/*
 * Structure sent through ioctl from the application to the driver
 */
struct ff_effect {
	__u16 type;
/* Following field denotes the unique id assigned to an effect.
 * If user sets if to -1, a new effect is created, and its id is returned in the same field
 * Else, the user sets it to the effect id it wants to update.
 */
	__s16 id;

	__u16 direction;	/* Direction. 0 deg -> 0x0000 (down)
					     90 deg -> 0x4000 (left)
					    180 deg -> 0x8000 (up)
					    270 deg -> 0xC000 (right)
				*/

	struct ff_trigger trigger;
	struct ff_replay replay;

	union {
		struct ff_constant_effect constant;
		struct ff_ramp_effect ramp;
		struct ff_periodic_effect periodic;
		struct ff_condition_effect condition[2]; /* One for each axis */
		struct ff_rumble_effect rumble;
	} u;
};

/*
 * Force feedback effect types
 */

#define FF_RUMBLE	0x50
#define FF_PERIODIC	0x51
#define FF_CONSTANT	0x52
#define FF_SPRING	0x53
#define FF_FRICTION	0x54
#define FF_DAMPER	0x55
#define FF_INERTIA	0x56
#define FF_RAMP		0x57

/*
 * Force feedback periodic effect types
 */

#define FF_SQUARE	0x58
#define FF_TRIANGLE	0x59
#define FF_SINE		0x5a
#define FF_SAW_UP	0x5b
#define FF_SAW_DOWN	0x5c
#define FF_CUSTOM	0x5d

/*
 * Set ff device properties
 */

#define FF_GAIN		0x60
#define FF_AUTOCENTER	0x61

#define FF_MAX		0x7f

#ifdef __KERNEL__

/*
 * In-kernel definitions.
 */

#include <linux/fs.h>
#include <linux/timer.h>

#define NBITS(x) (((x)/BITS_PER_LONG)+1)
#define BIT(x)	(1UL<<((x)%BITS_PER_LONG))
#define LONG(x) ((x)/BITS_PER_LONG)

#define INPUT_KEYCODE(dev, scancode) ((dev->keycodesize == 1) ? ((u8*)dev->keycode)[scancode] : \
	((dev->keycodesize == 2) ? ((u16*)dev->keycode)[scancode] : (((u32*)dev->keycode)[scancode])))

#define SET_INPUT_KEYCODE(dev, scancode, val)			\
		({	unsigned __old;				\
		switch (dev->keycodesize) {			\
			case 1: {				\
				u8 *k = (u8 *)dev->keycode;	\
				__old = k[scancode];		\
				k[scancode] = val;		\
				break;				\
			}					\
			case 2: {				\
				u16 *k = (u16 *)dev->keycode;	\
				__old = k[scancode];		\
				k[scancode] = val;		\
				break;				\
			}					\
			default: {				\
				u32 *k = (u32 *)dev->keycode;	\
				__old = k[scancode];		\
				k[scancode] = val;		\
				break;				\
			}					\
		}						\
		__old; })

struct input_dev {

	void *private;

	const char *name;
	const char *phys;
	const char *uniq;
	struct input_id id;

	unsigned long evbit[NBITS(EV_MAX)];
	unsigned long keybit[NBITS(KEY_MAX)];
	unsigned long relbit[NBITS(REL_MAX)];
	unsigned long absbit[NBITS(ABS_MAX)];
	unsigned long mscbit[NBITS(MSC_MAX)];
	unsigned long ledbit[NBITS(LED_MAX)];
	unsigned long sndbit[NBITS(SND_MAX)];
	unsigned long ffbit[NBITS(FF_MAX)];
	unsigned long swbit[NBITS(SW_MAX)];
	int ff_effects_max;

	unsigned int keycodemax;
	unsigned int keycodesize;
	void *keycode;

	unsigned int repeat_key;
	struct timer_list timer;

	struct pt_regs *regs;
	int state;

	int sync;

	int abs[ABS_MAX + 1];
	int rep[REP_MAX + 1];

	unsigned long key[NBITS(KEY_MAX)];
	unsigned long led[NBITS(LED_MAX)];
	unsigned long snd[NBITS(SND_MAX)];
	unsigned long sw[NBITS(SW_MAX)];

	int absmax[ABS_MAX + 1];
	int absmin[ABS_MAX + 1];
	int absfuzz[ABS_MAX + 1];
	int absflat[ABS_MAX + 1];

	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);
	int (*accept)(struct input_dev *dev, struct file *file);
	int (*flush)(struct input_dev *dev, struct file *file);
	int (*event)(struct input_dev *dev, unsigned int type, unsigned int code, int value);
	int (*upload_effect)(struct input_dev *dev, struct ff_effect *effect);
	int (*erase_effect)(struct input_dev *dev, int effect_id);

	struct input_handle *grab;

	struct semaphore sem;	/* serializes open and close operations */
	unsigned int users;

	struct device *dev;

	struct list_head	h_list;
	struct list_head	node;
};

/*
 * Structure for hotplug & device<->driver matching.
 */

#define INPUT_DEVICE_ID_MATCH_BUS	1
#define INPUT_DEVICE_ID_MATCH_VENDOR	2
#define INPUT_DEVICE_ID_MATCH_PRODUCT	4
#define INPUT_DEVICE_ID_MATCH_VERSION	8

#define INPUT_DEVICE_ID_MATCH_EVBIT	0x010
#define INPUT_DEVICE_ID_MATCH_KEYBIT	0x020
#define INPUT_DEVICE_ID_MATCH_RELBIT	0x040
#define INPUT_DEVICE_ID_MATCH_ABSBIT	0x080
#define INPUT_DEVICE_ID_MATCH_MSCIT	0x100
#define INPUT_DEVICE_ID_MATCH_LEDBIT	0x200
#define INPUT_DEVICE_ID_MATCH_SNDBIT	0x400
#define INPUT_DEVICE_ID_MATCH_FFBIT	0x800
#define INPUT_DEVICE_ID_MATCH_SWBIT	0x1000

#define INPUT_DEVICE_ID_MATCH_DEVICE\
	(INPUT_DEVICE_ID_MATCH_BUS | INPUT_DEVICE_ID_MATCH_VENDOR | INPUT_DEVICE_ID_MATCH_PRODUCT)
#define INPUT_DEVICE_ID_MATCH_DEVICE_AND_VERSION\
	(INPUT_DEVICE_ID_MATCH_DEVICE | INPUT_DEVICE_ID_MATCH_VERSION)

struct input_device_id {

	unsigned long flags;

	struct input_id id;

	unsigned long evbit[NBITS(EV_MAX)];
	unsigned long keybit[NBITS(KEY_MAX)];
	unsigned long relbit[NBITS(REL_MAX)];
	unsigned long absbit[NBITS(ABS_MAX)];
	unsigned long mscbit[NBITS(MSC_MAX)];
	unsigned long ledbit[NBITS(LED_MAX)];
	unsigned long sndbit[NBITS(SND_MAX)];
	unsigned long ffbit[NBITS(FF_MAX)];
	unsigned long swbit[NBITS(SW_MAX)];

	unsigned long driver_info;
};

struct input_handle;

struct input_handler {

	void *private;

	void (*event)(struct input_handle *handle, unsigned int type, unsigned int code, int value);
	struct input_handle* (*connect)(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id);
	void (*disconnect)(struct input_handle *handle);

	struct file_operations *fops;
	int minor;
	char *name;

	struct input_device_id *id_table;
	struct input_device_id *blacklist;

	struct list_head	h_list;
	struct list_head	node;
};

struct input_handle {

	void *private;

	int open;
	char *name;

	struct input_dev *dev;
	struct input_handler *handler;

	struct list_head	d_node;
	struct list_head	h_node;
};

#define to_dev(n) container_of(n,struct input_dev,node)
#define to_handler(n) container_of(n,struct input_handler,node);
#define to_handle(n) container_of(n,struct input_handle,d_node)
#define to_handle_h(n) container_of(n,struct input_handle,h_node)

static inline void init_input_dev(struct input_dev *dev)
{
	INIT_LIST_HEAD(&dev->h_list);
	INIT_LIST_HEAD(&dev->node);
}

void input_register_device(struct input_dev *);
void input_unregister_device(struct input_dev *);

void input_register_handler(struct input_handler *);
void input_unregister_handler(struct input_handler *);

int input_grab_device(struct input_handle *);
void input_release_device(struct input_handle *);

int input_open_device(struct input_handle *);
void input_close_device(struct input_handle *);

int input_accept_process(struct input_handle *handle, struct file *file);
int input_flush_device(struct input_handle* handle, struct file* file);

void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value);

static inline void input_report_key(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_KEY, code, !!value);
}

static inline void input_report_rel(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_REL, code, value);
}

static inline void input_report_abs(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_ABS, code, value);
}

static inline void input_report_ff(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_FF, code, value);
}

static inline void input_report_ff_status(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_FF_STATUS, code, value);
}

static inline void input_report_switch(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_SW, code, !!value);
}

static inline void input_regs(struct input_dev *dev, struct pt_regs *regs)
{
	dev->regs = regs;
}

static inline void input_sync(struct input_dev *dev)
{
	input_event(dev, EV_SYN, SYN_REPORT, 0);
	dev->regs = NULL;
}

static inline void input_set_abs_params(struct input_dev *dev, int axis, int min, int max, int fuzz, int flat)
{
	dev->absmin[axis] = min;
	dev->absmax[axis] = max;
	dev->absfuzz[axis] = fuzz;
	dev->absflat[axis] = flat;

	dev->absbit[LONG(axis)] |= BIT(axis);
}

extern struct class *input_class;

#endif
#endif
