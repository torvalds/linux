/*-
 * Copyright (c) 2016 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * Copyright (c) 2015-2016 Vladimir Kondratyev <wulf@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_EVDEV_INPUT_H
#define	_EVDEV_INPUT_H

#ifndef __KERNEL__
#include <sys/ioccom.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#include "input-event-codes.h"

#define	input_event_sec		time.tv_sec
#define	input_event_usec	time.tv_usec

struct input_event {
	struct timeval	time;
	uint16_t	type;
	uint16_t	code;
	int32_t		value;
};

#define	EV_VERSION		0x010001

struct input_id {
	uint16_t	bustype;
	uint16_t	vendor;
	uint16_t	product;
	uint16_t	version;
};

struct input_absinfo {
	int32_t		value;
	int32_t		minimum;
	int32_t		maximum;
	int32_t		fuzz;
	int32_t		flat;
	int32_t		resolution;
};

#define	INPUT_KEYMAP_BY_INDEX	(1 << 0)

struct input_keymap_entry {
	uint8_t		flags;
	uint8_t		len;
	uint16_t	index;
	uint32_t	keycode;
	uint8_t		scancode[32];
};

#define	EVDEV_IOC_MAGIC	'E'
#define	EVIOCGVERSION		_IOR(EVDEV_IOC_MAGIC, 0x01, int)		/* get driver version */
#define	EVIOCGID		_IOR(EVDEV_IOC_MAGIC, 0x02, struct input_id)	/* get device ID */
#define	EVIOCGREP		_IOR(EVDEV_IOC_MAGIC, 0x03, unsigned int[2])	/* get repeat settings */
#define	EVIOCSREP		_IOW(EVDEV_IOC_MAGIC, 0x03, unsigned int[2])	/* set repeat settings */

#define	EVIOCGKEYCODE		_IOWR(EVDEV_IOC_MAGIC, 0x04, unsigned int[2])	/* get keycode */
#define	EVIOCGKEYCODE_V2	_IOWR(EVDEV_IOC_MAGIC, 0x04, struct input_keymap_entry)
#define	EVIOCSKEYCODE		_IOW(EVDEV_IOC_MAGIC, 0x04, unsigned int[2])	/* set keycode */
#define	EVIOCSKEYCODE_V2	_IOW(EVDEV_IOC_MAGIC, 0x04, struct input_keymap_entry)

#define	EVIOCGNAME(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x06, len)	/* get device name */
#define	EVIOCGPHYS(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x07, len)	/* get physical location */
#define	EVIOCGUNIQ(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x08, len)	/* get unique identifier */
#define	EVIOCGPROP(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x09, len)	/* get device properties */

#define	EVIOCGMTSLOTS(len)	_IOC(IOC_INOUT,	EVDEV_IOC_MAGIC, 0x0a, len)	/* get MT slots values */

#define	EVIOCGKEY(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x18, len)	/* get global key state */
#define	EVIOCGLED(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x19, len)	/* get all LEDs */
#define	EVIOCGSND(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x1a, len)	/* get all sounds status */
#define	EVIOCGSW(len)		_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x1b, len)	/* get all switch states */

#define	EVIOCGBIT(ev,len)	_IOC(IOC_OUT, EVDEV_IOC_MAGIC, 0x20 + (ev), len)	/* get event bits */
#define	EVIOCGABS(abs)		_IOR(EVDEV_IOC_MAGIC, 0x40 + (abs), struct input_absinfo)	/* get abs value/limits */
#define	EVIOCSABS(abs)		_IOW(EVDEV_IOC_MAGIC, 0xc0 + (abs), struct input_absinfo)	/* set abs value/limits */

#define	EVIOCSFF		_IOW(EVDEV_IOC_MAGIC, 0x80, struct ff_effect)	/* send a force effect to a force feedback device */
#define	EVIOCRMFF		_IOWINT(EVDEV_IOC_MAGIC, 0x81)			/* Erase a force effect */
#define	EVIOCGEFFECTS		_IOR(EVDEV_IOC_MAGIC, 0x84, int)		/* Report number of effects playable at the same time */

#define	EVIOCGRAB		_IOWINT(EVDEV_IOC_MAGIC, 0x90)			/* Grab/Release device */
#define	EVIOCREVOKE		_IOWINT(EVDEV_IOC_MAGIC, 0x91)			/* Revoke device access */

#define	EVIOCSCLOCKID		_IOW(EVDEV_IOC_MAGIC, 0xa0, int)		/* Set clockid to be used for timestamps */

/*
 * IDs.
 */

#define	ID_BUS			0
#define	ID_VENDOR		1
#define	ID_PRODUCT		2
#define	ID_VERSION		3

#define	BUS_PCI			0x01
#define	BUS_ISAPNP		0x02
#define	BUS_USB			0x03
#define	BUS_HIL			0x04
#define	BUS_BLUETOOTH		0x05
#define	BUS_VIRTUAL		0x06

#define	BUS_ISA			0x10
#define	BUS_I8042		0x11
#define	BUS_XTKBD		0x12
#define	BUS_RS232		0x13
#define	BUS_GAMEPORT		0x14
#define	BUS_PARPORT		0x15
#define	BUS_AMIGA		0x16
#define	BUS_ADB			0x17
#define	BUS_I2C			0x18
#define	BUS_HOST		0x19
#define	BUS_GSC			0x1A
#define	BUS_ATARI		0x1B
#define	BUS_SPI			0x1C
#define	BUS_RMI			0x1D
#define	BUS_CEC			0x1E
#define	BUS_INTEL_ISHTP		0x1F

/*
 * MT_TOOL types
 */
#define	MT_TOOL_FINGER		0
#define	MT_TOOL_PEN		1
#define	MT_TOOL_PALM		2
#define	MT_TOOL_MAX		2

/*
 * Values describing the status of a force-feedback effect
 */
#define	FF_STATUS_STOPPED	0x00
#define	FF_STATUS_PLAYING	0x01
#define	FF_STATUS_MAX		0x01

/* scheduling info for force feedback effect */
struct ff_replay {
	uint16_t	length;		/* length of the effect (ms) */
	uint16_t	delay;		/* delay before effect starts (ms) */
};

/* trigger for force feedback effect */
struct ff_trigger {
	uint16_t	button;		/* trigger button number */
	uint16_t	interval;	/* delay between re-triggers */
};

/* force feedback effect envelop */
struct ff_envelope {
	uint16_t	attack_length;	/* duration of the attach (ms) */
	uint16_t	attack_level;	/* level at the beginning (0x0000 - 0x7fff) */
	uint16_t	fade_length;	/* duratin of fade (ms) */
	uint16_t	fade_level;	/* level at the end of fade */
};

struct ff_constant_effect {
	int16_t			level;
	struct ff_envelope	envelope;
};

struct ff_ramp_effect {
	int16_t			start_level;
	int16_t			end_level;
	struct ff_envelope	envelope;
};

struct ff_condition_effect {
	/* maximum level when joystick moved to respective side */
	uint16_t	right_saturation;

	uint16_t	left_saturation;
	/* how fast force grows when joystick move to the respective side */
	int16_t		right_coeff;
	int16_t		left_coeff;

	uint16_t	deadband;	/* size of dead zone when no force is produced */
	int16_t		center;		/* center of dead zone */
};

/*
 * Force feedback periodic effect types
 */

#define	FF_SQUARE	0x58
#define	FF_TRIANGLE	0x59
#define	FF_SINE		0x5a
#define	FF_SAW_UP	0x5b
#define	FF_SAW_DOWN	0x5c
#define	FF_CUSTOM	0x5d

#define	FF_WAVEFORM_MIN	FF_SQUARE
#define	FF_WAVEFORM_MAX	FF_CUSTOM

struct ff_periodic_effect {
	uint16_t		waveform;
	uint16_t		period;		/* ms */
	int16_t			magnitude;	/* peak */
	int16_t			offset;		/* mean, roughly */
	uint16_t		phase;		/* horizontal shift */
	struct ff_envelope	envelope;
	uint32_t		custom_len;	/* FF_CUSTOM waveform only */
	int16_t			*custom_data;	/* FF_CUSTOM waveform only */
};

struct ff_rumble_effect {
	uint16_t	strong_magnitude;	/* magnitude of the heavy motor */
	uint16_t	weak_magnitude;		/* magnitude of the light motor */
};

/*
 * Force feedback effect types
 */

#define	FF_RUMBLE	0x50
#define	FF_PERIODIC	0x51
#define	FF_CONSTANT	0x52
#define	FF_SPRING	0x53
#define	FF_FRICTION	0x54
#define	FF_DAMPER	0x55
#define	FF_INERTIA	0x56
#define	FF_RAMP		0x57

#define	FF_EFFECT_MIN	FF_RUMBLE
#define	FF_EFFECT_MAX	FF_RAMP

struct ff_effect {
	uint16_t		type;
	int16_t			id;
	uint16_t		direction;	/* [0 .. 360) degrees -> [0 .. 0x10000) */
	struct ff_trigger	trigger;
	struct ff_replay	replay;

	union {
		struct ff_constant_effect	constant;
		struct ff_ramp_effect		ramp;
		struct ff_periodic_effect	periodic;
		struct ff_condition_effect	condition[2]; /* One for each axis */
		struct ff_rumble_effect		rumble;
	} u;
};

/*
 * force feedback device properties
 */

#define	FF_GAIN		0x60
#define	FF_AUTOCENTER	0x61

#define	FF_MAX		0x7f
#define	FF_CNT		(FF_MAX+1)

#endif /* _EVDEV_INPUT_H */
