/* $OpenBSD: wsmousevar.h,v 1.15 2017/06/18 13:21:48 bru Exp $ */
/* $NetBSD: wsmousevar.h,v 1.4 2000/01/08 02:57:24 takemura Exp $ */

/*
 * Copyright (c) 1996, 1997 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

#ifndef _WSMOUSEVAR_H_
#define _WSMOUSEVAR_H_

#ifdef _KERNEL

/*
 * WSMOUSE interfaces.
 */

/*
 * Mouse access functions (must be provided by all mice).
 *
 * There is a "void *" cookie provided by the mouse driver associated
 * with these functions, which is passed to them when they are invoked.
 */
struct wsmouse_accessops {
	int	(*enable)(void *);
	int	(*ioctl)(void *v, u_long cmd, caddr_t data, int flag,
		    struct proc *p);
	void	(*disable)(void *);
};

/*
 * Attachment information provided by wsmousedev devices when attaching
 * wsmouse units.
 */
struct wsmousedev_attach_args {
	const struct wsmouse_accessops *accessops;	/* access ops */
	void	*accesscookie;				/* access cookie */
};

#define	wsmousedevcf_mux	cf_loc[WSMOUSEDEVCF_MUX]

/*
 * Autoconfiguration helper functions.
 */
int	wsmousedevprint(void *, const char *);


/* Process standard mouse input. */
#define WSMOUSE_INPUT(sc_wsmousedev, btns, dx, dy, dz, dw)		\
	do {								\
		wsmouse_buttons((sc_wsmousedev), (btns));		\
		wsmouse_motion((sc_wsmousedev), (dx), (dy), (dz), (dw));\
		wsmouse_input_sync(sc_wsmousedev);			\
	} while (0)


/* Process standard touchpad input. */
#define WSMOUSE_TOUCH(sc_wsmousedev, btns, x, y, pressure, contacts)	\
	do {								\
		wsmouse_buttons((sc_wsmousedev), (btns));		\
		wsmouse_position((sc_wsmousedev), (x), (y));		\
		wsmouse_touch((sc_wsmousedev), (pressure), (contacts));	\
		wsmouse_input_sync(sc_wsmousedev);			\
	} while (0)


/*
 * Drivers for touchpads that don't report pressure values can pass
 * WSMOUSE_DEFAULT_PRESSURE to wsmouse_touch or wsmouse_mtstate.
 *
 * A pressure value of 0 signals that a touch has been released (coordinates
 * will be ignored). Based on its pressure argument, wsmouse_touch will
 * normalize the contact count (drivers for touch devices that don't
 * recognize multiple contacts can always pass 0 as contact count to
 * wsmouse_touch).
 */
/* Use a synaptics-compatible value. */
#define WSMOUSE_DEFAULT_PRESSURE	45


struct device;

/*
 * Type codes for wsmouse_set. REL_X/Y, MT_REL_X/Y, and TOUCH_WIDTH
 * cannot be reported by other functions. Please note that REL_X/Y
 * values are deltas to be applied to the absolute coordinates and
 * don't represent "pure" relative motion.
 */
enum wsmouseval {
	WSMOUSE_REL_X,
	WSMOUSE_ABS_X,
	WSMOUSE_REL_Y,
	WSMOUSE_ABS_Y,
	WSMOUSE_PRESSURE,
	WSMOUSE_CONTACTS,
	WSMOUSE_TOUCH_WIDTH,
	WSMOUSE_MT_REL_X,
	WSMOUSE_MT_ABS_X,
	WSMOUSE_MT_REL_Y,
	WSMOUSE_MT_ABS_Y,
	WSMOUSE_MT_PRESSURE
};

#define WSMOUSE_IS_MT_CODE(code) \
    ((code) >= WSMOUSE_MT_REL_X && (code) <= WSMOUSE_MT_PRESSURE)

struct mtpoint {
	int x;
	int y;
	int pressure;
	int slot;		/* An output field, set by wsmouse_mtframe. */
};

/* Report button state. */
void wsmouse_buttons(struct device *, u_int);

/* Report motion deltas (dx, dy, dz, dw). */
void wsmouse_motion(struct device *, int, int, int, int);

/* Report absolute coordinates (x, y). */
void wsmouse_position(struct device *, int, int);

/* Report (single-)touch input (pressure, contacts). */
void wsmouse_touch(struct device *, int, int);

/* Report slot-based multitouch input (slot, x, y, pressure). */
void wsmouse_mtstate(struct device *, int, int, int, int);

/* Report multitouch input (mtpoints, size). */
void wsmouse_mtframe(struct device *, struct mtpoint *, int);

/* Report a single value (type, value, aux). */
void wsmouse_set(struct device *, enum wsmouseval, int, int);

/* Assign or look up a slot number for a tracking ID (id). */
int wsmouse_id_to_slot(struct device *, int);


/* Synchronize (generate wscons events) */
void wsmouse_input_sync(struct device *);


/* Initialize MT structures (num_slots, tracking). */
int wsmouse_mt_init(struct device *, int, int);

#define WSMOUSE_MT_SLOTS_MAX 10
#define WSMOUSE_MT_INIT_TRACKING 1

/* Switch between compatibility mode and native mode. */
int wsmouse_set_mode(struct device *, int);

/* Read/Set parameter values. */
int wsmouse_get_params(struct device *, struct wsmouse_param *, u_int);
int wsmouse_set_params(struct device *, const struct wsmouse_param *, u_int);


enum wsmousehw_type {
	WSMOUSEHW_RAW,
	WSMOUSEHW_MOUSE,
	WSMOUSEHW_TOUCHPAD,
	WSMOUSEHW_CLICKPAD,
	WSMOUSEHW_TPANEL,
};

/*
 * wsmousehw.flags
 */
/* Invert Y-coordinates */
#define WSMOUSEHW_LR_DOWN 	(1 << 0)
/* Allocate the buffers for wsmouse_mtframe(). */
#define WSMOUSEHW_MT_TRACKING	(1 << 1)


/*
 * The more or less minimal hardware description for the default
 * configuration.
 *
 * Drivers that report coordinates with a downward orientation
 * must set the flag WSMOUSEHW_LR_DOWN. Drivers for MT hardware
 * must provide the number of slots. If they use wsmouse_mtframe(),
 * WSMOUSEHW_MT_TRACKING must be set.
 *
 * The resolution values are optional.
 */
struct wsmousehw {
	int type;		/* WSMOUSE_TYPE_*, cf. wsconsio.h */
	enum wsmousehw_type hw_type;
	int x_min;
	int x_max;
	int y_min;
	int y_max;
	int h_res;
	int v_res;

	int flags;
	int mt_slots;

	int contacts_max;	/* inclusive (not needed for MT touchpads) */
};

struct wsmousehw *wsmouse_get_hw(struct device*);

/* Configure the input context. */
int wsmouse_configure(struct device *, struct wsmouse_param *, u_int);

#endif /* _KERNEL */

#endif /* _WSMOUSEVAR_H_ */
