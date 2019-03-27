/*-
 * Copyright (c) 2014 Jakub Wojciech Klama <jceel@FreeBSD.org>
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

#ifndef	_DEV_EVDEV_EVDEV_H
#define	_DEV_EVDEV_EVDEV_H

#include <sys/types.h>
#include <sys/kbio.h>
#include <dev/evdev/input.h>
#include <dev/kbd/kbdreg.h>

#define	NAMELEN		80

struct evdev_dev;

typedef int (evdev_open_t)(struct evdev_dev *);
typedef int (evdev_close_t)(struct evdev_dev *);
typedef void (evdev_event_t)(struct evdev_dev *, uint16_t, uint16_t, int32_t);
typedef void (evdev_keycode_t)(struct evdev_dev *,
    struct input_keymap_entry *);

/*
 * Keyboard and mouse events recipient mask.
 * evdev_rcpt_mask variable should be respected by keyboard and mouse drivers
 * that are able to send events through both evdev and sysmouse/kbdmux
 * interfaces so user can choose prefered one to not receive one event twice.
 */
#define	EVDEV_RCPT_SYSMOUSE	(1<<0)
#define	EVDEV_RCPT_KBDMUX	(1<<1)
#define	EVDEV_RCPT_HW_MOUSE	(1<<2)
#define	EVDEV_RCPT_HW_KBD	(1<<3)
extern int evdev_rcpt_mask;
/*
 * Sysmouse protocol does not support horizontal wheel movement reporting.
 * To overcome this limitation different drivers use different sysmouse proto
 * extensions. Set kern.evdev.sysmouse_t_axis to tell sysmouse evdev driver
 * which protocol extension is used.
 * 0 - do not extract horizontal wheel movement (default).
 * 1 - ums(4) horizontal wheel encoding. T-axis is mapped to buttons 6 and 7
 * 2 - psm(4) wheels encoding: z = 1,-1 - vert. wheel, z = 2,-2 - horiz. wheel
 */
enum
{
	EVDEV_SYSMOUSE_T_AXIS_NONE = 0,
	EVDEV_SYSMOUSE_T_AXIS_UMS = 1,
	EVDEV_SYSMOUSE_T_AXIS_PSM = 2,
};
extern int evdev_sysmouse_t_axis;

#define	ABS_MT_FIRST	ABS_MT_TOUCH_MAJOR
#define	ABS_MT_LAST	ABS_MT_TOOL_Y
#define	ABS_IS_MT(x)	((x) >= ABS_MT_FIRST && (x) <= ABS_MT_LAST)
#define	ABS_MT_INDEX(x)	((x) - ABS_MT_FIRST)
#define	MT_CNT		(ABS_MT_INDEX(ABS_MT_LAST) + 1)
/* Multitouch protocol type A */
#define	MAX_MT_REPORTS	5
/* Multitouch protocol type B interface */
#define	MAX_MT_SLOTS	16

#define	EVDEV_FLAG_SOFTREPEAT	0x00	/* use evdev to repeat keys */
#define	EVDEV_FLAG_MT_STCOMPAT	0x01	/* autogenerate ST-compatible events
					 * for MT protocol type B reports */
#define	EVDEV_FLAG_MT_AUTOREL	0x02	/* Autorelease MT-slots not listed in
					 * current MT protocol type B report */
#define	EVDEV_FLAG_MAX		0x1F
#define	EVDEV_FLAG_CNT		(EVDEV_FLAG_MAX + 1)

struct evdev_methods
{
	evdev_open_t		*ev_open;
	evdev_close_t		*ev_close;
	evdev_event_t		*ev_event;
	evdev_keycode_t		*ev_get_keycode;
	evdev_keycode_t		*ev_set_keycode;
};

/* Input device interface: */
struct evdev_dev *evdev_alloc(void);
void evdev_free(struct evdev_dev *);
void evdev_set_name(struct evdev_dev *, const char *);
void evdev_set_id(struct evdev_dev *, uint16_t, uint16_t, uint16_t, uint16_t);
void evdev_set_phys(struct evdev_dev *, const char *);
void evdev_set_serial(struct evdev_dev *, const char *);
void evdev_set_methods(struct evdev_dev *, void *,
    const struct evdev_methods *);
int evdev_register(struct evdev_dev *);
int evdev_register_mtx(struct evdev_dev *, struct mtx *);
int evdev_unregister(struct evdev_dev *);
int evdev_push_event(struct evdev_dev *, uint16_t, uint16_t, int32_t);
void evdev_support_prop(struct evdev_dev *, uint16_t);
void evdev_support_event(struct evdev_dev *, uint16_t);
void evdev_support_key(struct evdev_dev *, uint16_t);
void evdev_support_rel(struct evdev_dev *, uint16_t);
void evdev_support_abs(struct evdev_dev *, uint16_t, int32_t, int32_t, int32_t,
   int32_t, int32_t, int32_t);
void evdev_support_msc(struct evdev_dev *, uint16_t);
void evdev_support_led(struct evdev_dev *, uint16_t);
void evdev_support_snd(struct evdev_dev *, uint16_t);
void evdev_support_sw(struct evdev_dev *, uint16_t);
void evdev_set_repeat_params(struct evdev_dev *, uint16_t, int);
int evdev_set_report_size(struct evdev_dev *, size_t);
void evdev_set_flag(struct evdev_dev *, uint16_t);
void *evdev_get_softc(struct evdev_dev *);

/* Multitouch related functions: */
int32_t evdev_get_mt_slot_by_tracking_id(struct evdev_dev *, int32_t);
void evdev_support_nfingers(struct evdev_dev *, int32_t);
void evdev_support_mt_compat(struct evdev_dev *);
void evdev_push_nfingers(struct evdev_dev *, int32_t);
void evdev_push_mt_compat(struct evdev_dev *);

/* Utility functions: */
uint16_t evdev_hid2key(int);
void evdev_support_all_known_keys(struct evdev_dev *);
uint16_t evdev_scancode2key(int *, int);
void evdev_push_mouse_btn(struct evdev_dev *, int);
void evdev_push_leds(struct evdev_dev *, int);
void evdev_push_repeats(struct evdev_dev *, keyboard_t *);

/* Event reporting shortcuts: */
static __inline int
evdev_sync(struct evdev_dev *evdev)
{

	return (evdev_push_event(evdev, EV_SYN, SYN_REPORT, 1));
}

static __inline int
evdev_mt_sync(struct evdev_dev *evdev)
{

	return (evdev_push_event(evdev, EV_SYN, SYN_MT_REPORT, 1));
}

static __inline int
evdev_push_key(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_KEY, code, value != 0));
}

static __inline int
evdev_push_rel(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_REL, code, value));
}

static __inline int
evdev_push_abs(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_ABS, code, value));
}

static __inline int
evdev_push_msc(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_MSC, code, value));
}

static __inline int
evdev_push_led(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_LED, code, value != 0));
}

static __inline int
evdev_push_snd(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_SND, code, value));
}

static __inline int
evdev_push_sw(struct evdev_dev *evdev, uint16_t code, int32_t value)
{

	return (evdev_push_event(evdev, EV_SW, code, value != 0));
}

#endif	/* _DEV_EVDEV_EVDEV_H */
