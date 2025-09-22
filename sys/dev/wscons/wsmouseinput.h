/* $OpenBSD: wsmouseinput.h,v 1.15 2021/03/21 16:20:49 bru Exp $ */

/*
 * Copyright (c) 2015, 2016 Ulf Brosziewski
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * wsmouse input processing - private header
 */

#ifndef _WSMOUSEINPUT_H_
#define _WSMOUSEINPUT_H_

#ifdef _KERNEL

struct position {
	int x;
	int y;
	int dx;			/* unfiltered coordinate deltas */
	int dy;
	int acc_dx;		/* delta sums used for filtering */
	int acc_dy;
};

struct btn_state {
	u_int buttons;
	u_int sync;
};

struct motion_state {
	int dx;			/* mouse input, or filtered deltas */
	int dy;
	int dz;
	int dw;
	struct position pos;
	u_int sync;
};
#define SYNC_DELTAS		(1 << 0)
#define SYNC_X			(1 << 1)
#define SYNC_Y			(1 << 2)
#define SYNC_POSITION		(SYNC_X | SYNC_Y)

struct touch_state {
	int pressure;
	int contacts;
	int width;
	u_int sync;

	int min_pressure;
	int prev_contacts;
};
#define SYNC_PRESSURE		(1 << 0)
#define SYNC_CONTACTS		(1 << 1)
#define SYNC_TOUCH_WIDTH	(1 << 2)

struct mt_slot {
	struct position pos;
	int pressure;
	int id;		/* tracking ID */
};
#define MTS_TOUCH	0
#define MTS_X		1
#define MTS_Y		2
#define MTS_PRESSURE	3

#define MTS_SIZE	4

struct mt_state {
	/* the set of slots with active touches */
	u_int touches;
	/* the set of slots with unsynchronized state */
	u_int frame;

	int num_slots;
	struct mt_slot *slots;
	/* the sets of changes per slot axis */
	u_int sync[MTS_SIZE];

	int num_touches;

	/* pointer control */
	u_int ptr;
	u_int ptr_cycle;
	u_int prev_ptr;
	u_int ptr_mask;

	/* a buffer for the MT tracking function */
	int *matrix;
};


struct axis_filter {
	/* A scale factor in [*.12] fixed-point format */
	int scale;
	int rmdr;
	/* Invert coordinates. */
	int inv;
	/* Hysteresis limit, and weighted delta average. */
	int hysteresis;
	int avg;
	int avg_rmdr;
	/* A [*.12] coefficient for "magnitudes", used for deceleration. */
	int mag_scale;
	int dclr_rmdr;
	/* Ignore deltas that are greater than this limit. */
	int dmax;
};

struct interval {
	long avg;	/* average update interval in nanoseconds */
	long sum;
	int samples;
	struct timespec ts;
	int track;
};

struct wsmouseinput {
	u_int flags;

	struct btn_state btn;
	struct btn_state sbtn;	/* softbuttons */
	struct motion_state motion;
	struct touch_state touch;
	struct mt_state mt;

	struct { /* Parameters and state of various input filters. */
		struct axis_filter h;
		struct axis_filter v;

		int dclr;	/* deceleration threshold */
		int mag;	/* weighted average of delta magnitudes */
		u_int mode;	/* hysteresis type, smoothing factor */

		int ratio;	/* X/Y ratio */

		int swapxy;
		int tracking_maxdist;
		int pressure_lo;
		int pressure_hi;
	} filter;

	struct wsmousehw hw;
	struct interval intv;
	struct wstpad *tp;

	struct wseventvar **evar;
};
/* wsmouseinput.flags */
#define TPAD_COMPAT_MODE	(1 << 0)
#define TPAD_NATIVE_MODE	(1 << 1)
#define MT_TRACKING		(1 << 2)
#define REVERSE_SCROLLING	(1 << 3)
#define RESYNC			(1 << 16)
#define TRACK_INTERVAL		(1 << 17)
#define CONFIGURED		(1 << 18)
#define LOG_INPUT		(1 << 19)
#define LOG_EVENTS		(1 << 20)

/* filter.mode (bit 0-2: smoothing factor, bit 3-n: unused) */
#define SMOOTHING_MASK		7
#define FILTER_MODE_DEFAULT	0

struct evq_access {
	struct wseventvar *evar;
	struct timespec ts;
	int put;
	int result;
};
#define EVQ_RESULT_OVERFLOW	-1
#define EVQ_RESULT_NONE		0
#define EVQ_RESULT_SUCCESS	1


void wsmouse_evq_put(struct evq_access *, int, int);
void wsmouse_log_events(struct wsmouseinput *, struct evq_access *);
int wsmouse_hysteresis(struct wsmouseinput *, struct position *);
void wsmouse_input_reset(struct wsmouseinput *);
void wsmouse_input_cleanup(struct wsmouseinput *);

void wstpad_compat_convert(struct wsmouseinput *, struct evq_access *);
void wstpad_init_deceleration(struct wsmouseinput *);
int wstpad_configure(struct wsmouseinput *);
void wstpad_reset(struct wsmouseinput *);
void wstpad_cleanup(struct wsmouseinput *);

int wstpad_get_param(struct wsmouseinput *, int, int *);
int wstpad_set_param(struct wsmouseinput *, int, int);


#define FOREACHBIT(v, i) \
    for ((i) = ffs(v) - 1; (i) != -1; (i) = ffs((v) & (~1 << (i))) - 1)

#define DELTA_X_EV(input) ((input)->filter.swapxy ? \
    WSCONS_EVENT_MOUSE_DELTA_Y : WSCONS_EVENT_MOUSE_DELTA_X)
#define DELTA_Y_EV(input) ((input)->filter.swapxy ? \
    WSCONS_EVENT_MOUSE_DELTA_X : WSCONS_EVENT_MOUSE_DELTA_Y)
#define ABS_X_EV(input) ((input)->filter.swapxy ? \
    WSCONS_EVENT_MOUSE_ABSOLUTE_Y : WSCONS_EVENT_MOUSE_ABSOLUTE_X)
#define ABS_Y_EV(input) ((input)->filter.swapxy ? \
    WSCONS_EVENT_MOUSE_ABSOLUTE_X : WSCONS_EVENT_MOUSE_ABSOLUTE_Y)
#define DELTA_Z_EV	WSCONS_EVENT_MOUSE_DELTA_Z
#define DELTA_W_EV	WSCONS_EVENT_MOUSE_DELTA_W
#define VSCROLL_EV	WSCONS_EVENT_VSCROLL
#define HSCROLL_EV	WSCONS_EVENT_HSCROLL
#define ABS_Z_EV	WSCONS_EVENT_TOUCH_PRESSURE
#define ABS_W_EV	WSCONS_EVENT_TOUCH_CONTACTS
#define BTN_DOWN_EV	WSCONS_EVENT_MOUSE_DOWN
#define BTN_UP_EV	WSCONS_EVENT_MOUSE_UP
#define SYNC_EV		WSCONS_EVENT_SYNC

/* matrix size + buffer size for wsmouse_matching */
#define MATRIX_SIZE(slots) (((slots) + 6) * (slots) * sizeof(int))

#define IS_TOUCHPAD(input)			\
    ((input)->hw.hw_type == WSMOUSEHW_TOUCHPAD	\
    || (input)->hw.hw_type == WSMOUSEHW_CLICKPAD)

/* Extract a four-digit millisecond value from a timespec. */
#define LOGTIME(tsp) \
    ((int) (((tsp)->tv_sec % 10) * 1000 + ((tsp)->tv_nsec / 1000000)))

#define DEVNAME(input) ((char *) (input)	\
    - offsetof(struct wsmouse_softc, sc_input)	\
    + offsetof(struct device, dv_xname))

#endif /* _KERNEL */

#endif /* _WSMOUSEINPUT_H_ */
