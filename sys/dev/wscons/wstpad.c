/* $OpenBSD: wstpad.c,v 1.35 2025/01/30 08:53:29 mvs Exp $ */

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
 * touchpad input processing
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/timeout.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>
#include <dev/wscons/wseventvar.h>
#include <dev/wscons/wsmouseinput.h>

#define BTNMASK(n)		((n) > 0 && (n) <= 32 ? 1 << ((n) - 1) : 0)

#define LEFTBTN			BTNMASK(1)
#define MIDDLEBTN		BTNMASK(2)
#define RIGHTBTN		BTNMASK(3)

#define PRIMARYBTN LEFTBTN

#define PRIMARYBTN_CLICKED(tp) ((tp)->btns_sync & PRIMARYBTN & (tp)->btns)
#define PRIMARYBTN_RELEASED(tp) ((tp)->btns_sync & PRIMARYBTN & ~(tp)->btns)

#define IS_MT(tp) ((tp)->features & WSTPAD_MT)
#define DISABLE(tp) ((tp)->features & WSTPAD_DISABLE)

/*
 * Ratios to the height or width of the touchpad surface, in
 * [*.12] fixed-point format:
 */
#define V_EDGE_RATIO_DEFAULT	205
#define B_EDGE_RATIO_DEFAULT	410
#define T_EDGE_RATIO_DEFAULT	512
#define CENTER_RATIO_DEFAULT	512

#define TAP_MAXTIME_DEFAULT	180
#define TAP_CLICKTIME_DEFAULT	180
#define TAP_LOCKTIME_DEFAULT	0
#define TAP_BTNMAP_SIZE		3

#define CLICKDELAY_MS		20
#define FREEZE_MS		100
#define MATCHINTERVAL_MS	45
#define STOPINTERVAL_MS		55

#define MAG_LOW			(10 << 12)
#define MAG_MEDIUM		(18 << 12)

enum tpad_handlers {
	SOFTBUTTON_HDLR,
	TOPBUTTON_HDLR,
	TAP_HDLR,
	F2SCROLL_HDLR,
	EDGESCROLL_HDLR,
	CLICK_HDLR,
};

enum tap_state {
	TAP_DETECT,
	TAP_IGNORE,
	TAP_LIFTED,
	TAP_LOCKED,
	TAP_LOCKED_DRAG,
};

enum tpad_cmd {
	CLEAR_MOTION_DELTAS,
	SOFTBUTTON_DOWN,
	SOFTBUTTON_UP,
	TAPBUTTON_SYNC,
	TAPBUTTON_DOWN,
	TAPBUTTON_UP,
	VSCROLL,
	HSCROLL,
};

/*
 * tpad_touch.flags:
 */
#define L_EDGE			(1 << 0)
#define R_EDGE			(1 << 1)
#define T_EDGE			(1 << 2)
#define B_EDGE			(1 << 3)
#define THUMB 			(1 << 4)

#define EDGES (L_EDGE | R_EDGE | T_EDGE | B_EDGE)

/*
 * A touch is "centered" if it does not start and remain at the top
 * edge or one of the vertical edges.  Two-finger scrolling and tapping
 * require that at least one touch is centered.
 */
#define CENTERED(t) (((t)->flags & (L_EDGE | R_EDGE | T_EDGE)) == 0)

enum touchstates {
	TOUCH_NONE,
	TOUCH_BEGIN,
	TOUCH_UPDATE,
	TOUCH_END,
};

struct tpad_touch {
	u_int flags;
	enum touchstates state;
	int x;
	int y;
	int dir;
	struct timespec start;
	struct timespec match;
	struct position *pos;
	struct {
		int x;
		int y;
		struct timespec time;
	} orig;
};

/*
 * wstpad.features
 */
#define WSTPAD_SOFTBUTTONS	(1 << 0)
#define WSTPAD_SOFTMBTN		(1 << 1)
#define WSTPAD_TOPBUTTONS	(1 << 2)
#define WSTPAD_TWOFINGERSCROLL	(1 << 3)
#define WSTPAD_EDGESCROLL	(1 << 4)
#define WSTPAD_HORIZSCROLL	(1 << 5)
#define WSTPAD_SWAPSIDES	(1 << 6)
#define WSTPAD_DISABLE		(1 << 7)
#define WSTPAD_MTBUTTONS	(1 << 8)

#define WSTPAD_MT		(1U << 31)


struct wstpad {
	u_int features;
	u_int handlers;

	/*
	 * t always points into the tpad_touches array, which has at
	 * least one element. If there is more than one, t selects
	 * the pointer-controlling touch.
	 */
	struct tpad_touch *t;
	struct tpad_touch *tpad_touches;

	u_int mtcycle;
	u_int ignore;

	int contacts;
	int prev_contacts;
	u_int btns;
	u_int btns_sync;
	int ratio;

	struct timespec time;

	u_int freeze;
	struct timespec freeze_ts;

	/* edge coordinates */
	struct {
		int left;
		int right;
		int top;
		int bottom;
		int center;
		int center_left;
		int center_right;
		int low;
	} edge;

	struct {
		/* ratios to the surface width or height */
		int left_edge;
		int right_edge;
		int top_edge;
		int bottom_edge;
		int center_width;
		/* two-finger contacts */
		int f2pressure;
		int f2width;
		/* MTBUTTONS: distance limit for two-finger clicks */
		int mtbtn_maxdist;
	} params;

	/* handler state and configuration: */

	u_int softbutton;
	u_int sbtnswap;

	struct {
		enum tap_state state;
		int contacts;
		int valid;
		u_int pending;
		u_int button;
		int masked;
		int maxdist;
		struct timeout to;
		/* parameters: */
		struct timespec maxtime;
		int clicktime;
		int locktime;
		u_int btnmap[TAP_BTNMAP_SIZE];
	} tap;

	struct {
		int dz;
		int dw;
		int hdist;
		int vdist;
		int mag;
	} scroll;
};

static const struct timespec match_interval =
    { .tv_sec = 0, .tv_nsec = MATCHINTERVAL_MS * 1000000 };

static const struct timespec stop_interval =
    { .tv_sec = 0, .tv_nsec = STOPINTERVAL_MS * 1000000 };

/*
 * Coordinates in the wstpad struct are "normalized" device coordinates,
 * the orientation is left-to-right and upward.
 */
static inline int
normalize_abs(struct axis_filter *filter, int val)
{
	return (filter->inv ? filter->inv - val : val);
}

static inline int
normalize_rel(struct axis_filter *filter, int val)
{
	return (filter->inv ? -val : val);
}

/*
 * Directions of motion are represented by numbers in the range 0 - 11,
 * corresponding to clockwise counted circle sectors:
 *
 *              11 | 0
 *           10    |    1
 *          9      |      2
 *          -------+-------
 *          8      |      3
 *            7    |    4
 *               6 | 5
 *
 */
/* Tangent constants in [*.12] fixed-point format: */
#define TAN_DEG_60 7094
#define TAN_DEG_30 2365

#define NORTH(d) ((d) == 0 || (d) == 11)
#define SOUTH(d) ((d) == 5 || (d) == 6)
#define EAST(d) ((d) == 2 || (d) == 3)
#define WEST(d) ((d) == 8 || (d) == 9)

static inline int
direction(int dx, int dy, int ratio)
{
	int rdy, dir = -1;

	if (dx || dy) {
		rdy = abs(dy) * ratio;
		if (abs(dx) * TAN_DEG_60 < rdy)
			dir = 0;
		else if (abs(dx) * TAN_DEG_30 < rdy)
			dir = 1;
		else
			dir = 2;
		if ((dx < 0) != (dy < 0))
			dir = 5 - dir;
		if (dx < 0)
			dir += 6;
	}
	return dir;
}

static inline int
dircmp(int dir1, int dir2)
{
	int diff = abs(dir1 - dir2);
	return (diff <= 6 ? diff : 12 - diff);
}

/*
 * Update direction and timespec attributes for a touch.  They are used to
 * determine whether it is moving - or resting - stably.
 *
 * The callers pass touches from the current frame and the touches that are
 * no longer present in the update cycle to this function.  Even though this
 * ensures that pairs of zero deltas do not result from stale coordinates,
 * zero deltas do not reset the state immediately.  A short time span - the
 * "stop interval" - must pass before the state is cleared, which is
 * necessary because some touchpads report intermediate stops when a touch
 * is moving very slowly.
 */
void
wstpad_set_direction(struct wstpad *tp, struct tpad_touch *t, int dx, int dy)
{
	int dir;
	struct timespec ts;

	if (t->state != TOUCH_UPDATE) {
		t->dir = -1;
		memcpy(&t->start, &tp->time, sizeof(struct timespec));
		return;
	}

	dir = direction(dx, dy, tp->ratio);
	if (dir >= 0) {
		if (t->dir < 0 || dircmp(dir, t->dir) > 1) {
			memcpy(&t->start, &tp->time, sizeof(struct timespec));
		}
		t->dir = dir;
		memcpy(&t->match, &tp->time, sizeof(struct timespec));
	} else if (t->dir >= 0) {
		timespecsub(&tp->time, &t->match, &ts);
		if (timespeccmp(&ts, &stop_interval, >=)) {
			t->dir = -1;
			memcpy(&t->start, &t->match, sizeof(struct timespec));
		}
	}
}

/*
 * Make a rough, but quick estimation of the speed of a touch.  Its
 * distance to the previous position is scaled by factors derived
 * from the average update rate and the deceleration parameter
 * (filter.dclr).  The unit of the result is:
 *         (filter.dclr / 100) device units per millisecond
 *
 * Magnitudes are returned in [*.12] fixed-point format.  For purposes
 * of filtering, they are divided into medium and high speeds
 * (> MAG_MEDIUM), low speeds, and very low speeds (< MAG_LOW).
 *
 * The scale factors are not affected if deceleration is turned off.
 */
static inline int
magnitude(struct wsmouseinput *input, int dx, int dy)
{
	int h, v;

	h = abs(dx) * input->filter.h.mag_scale;
	v = abs(dy) * input->filter.v.mag_scale;
	/* Return an "alpha-max-plus-beta-min" approximation: */
	return (h >= v ? h + 3 * v / 8 : v + 3 * h / 8);
}

/*
 * Treat a touch as stable if it is moving at a medium or high speed,
 * if it is moving continuously, or if it has stopped for a certain
 * time span.
 */
int
wstpad_is_stable(struct wsmouseinput *input, struct tpad_touch *t)
{
	struct timespec ts;

	if (t->dir >= 0) {
		if (magnitude(input, t->pos->dx, t->pos->dy) > MAG_MEDIUM)
			return (1);
		timespecsub(&t->match, &t->start, &ts);
	} else {
		timespecsub(&input->tp->time, &t->start, &ts);
	}

	return (timespeccmp(&ts, &match_interval, >=));
}

/*
 * If a touch starts in an edge area, pointer movement will be
 * suppressed as long as it stays in that area.
 */
static inline u_int
edge_flags(struct wstpad *tp, int x, int y)
{
	u_int flags = 0;

	if (x < tp->edge.left)
		flags |= L_EDGE;
	else if (x >= tp->edge.right)
		flags |= R_EDGE;
	if (y < tp->edge.bottom)
		flags |= B_EDGE;
	else if (y >= tp->edge.top)
		flags |= T_EDGE;

	return (flags);
}

static inline struct tpad_touch *
get_2nd_touch(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	int slot;

	if (IS_MT(tp)) {
		slot = ffs(input->mt.touches & ~(input->mt.ptr | tp->ignore));
		if (slot)
			return &tp->tpad_touches[--slot];
	}
	return NULL;
}

/* Suppress pointer motion for a short period of time. */
static inline void
set_freeze_ts(struct wstpad *tp, int sec, int ms)
{
	tp->freeze_ts.tv_sec = sec;
	tp->freeze_ts.tv_nsec = ms * 1000000;
	timespecadd(&tp->time, &tp->freeze_ts, &tp->freeze_ts);
}


/* Return TRUE if two-finger- or edge-scrolling would be valid. */
int
wstpad_scroll_coords(struct wsmouseinput *input, int *dx, int *dy)
{
	struct wstpad *tp = input->tp;

	if (tp->contacts != tp->prev_contacts || tp->btns || tp->btns_sync) {
		tp->scroll.dz = 0;
		tp->scroll.dw = 0;
		return (0);
	}
	if ((input->motion.sync & SYNC_POSITION) == 0)
		return (0);
	/*
	 * Try to exclude accidental scroll events by checking whether the
	 * pointer-controlling touch is stable.  The check, which may cause
	 * a short delay, is only applied initially, a touch that stops and
	 * resumes scrolling is not affected.
	 */
	if (tp->scroll.dz || tp->scroll.dw || wstpad_is_stable(input, tp->t)) {
		*dx = normalize_rel(&input->filter.h, input->motion.pos.dx);
		*dy = normalize_rel(&input->filter.v, input->motion.pos.dy);
		return (*dx || *dy);
	}

	return (0);
}

void
wstpad_scroll(struct wstpad *tp, int dx, int dy, int mag, u_int *cmds)
{
	int dz, dw, n = 1;

	/*
	 * The function applies strong deceleration, but only to input with
	 * very low speeds.  A higher threshold might make applications
	 * without support for precision scrolling appear unresponsive.
	 */
	mag = tp->scroll.mag = imin(MAG_MEDIUM,
	    (mag + 3 * tp->scroll.mag) / 4);
	if (mag < MAG_LOW)
		n = (MAG_LOW - mag) / 4096 + 1;

	if (dy && tp->scroll.vdist) {
		if (tp->scroll.dw) {
			/*
			 * Before switching the axis, wstpad_scroll_coords()
			 * should check again whether the movement is stable.
			 */
			tp->scroll.dw = 0;
			return;
		}
		dz = -dy * 4096 / (tp->scroll.vdist * n);
		if (tp->scroll.dz) {
			if ((dy < 0) != (tp->scroll.dz > 0))
				tp->scroll.dz = -tp->scroll.dz;
			dz = (dz + 3 * tp->scroll.dz) / 4;
		}
		if (dz) {
			tp->scroll.dz = dz;
			*cmds |= 1 << VSCROLL;
		}

	} else if (dx && tp->scroll.hdist) {
		if (tp->scroll.dz) {
			tp->scroll.dz = 0;
			return;
		}
		dw = dx * 4096 / (tp->scroll.hdist * n);
		if (tp->scroll.dw) {
			if ((dx > 0) != (tp->scroll.dw > 0))
				tp->scroll.dw = -tp->scroll.dw;
			dw = (dw + 3 * tp->scroll.dw) / 4;
		}
		if (dw) {
			tp->scroll.dw = dw;
			*cmds |= 1 << HSCROLL;
		}
	}
}

void
wstpad_f2scroll(struct wsmouseinput *input, u_int *cmds)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t2;
	int dir, dx, dy, centered;

	if (tp->ignore == 0) {
		if (tp->contacts != 2)
			return;
	} else if (tp->contacts != 3 || (tp->ignore == input->mt.ptr)) {
		return;
	}

	if (!wstpad_scroll_coords(input, &dx, &dy))
		return;

	dir = tp->t->dir;
	if (!(NORTH(dir) || SOUTH(dir)))
		dy = 0;
	if (!(EAST(dir) || WEST(dir)))
		dx = 0;

	if (dx || dy) {
		centered = CENTERED(tp->t);
		if (IS_MT(tp)) {
			t2 = get_2nd_touch(input);
			if (t2 == NULL)
				return;
			dir = t2->dir;
			if ((dy > 0 && !NORTH(dir)) || (dy < 0 && !SOUTH(dir)))
				return;
			if ((dx > 0 && !EAST(dir)) || (dx < 0 && !WEST(dir)))
				return;
			if (!wstpad_is_stable(input, t2) &&
			    !(tp->scroll.dz || tp->scroll.dw))
				return;
			centered |= CENTERED(t2);
		}
		if (centered) {
			wstpad_scroll(tp, dx, dy,
			    magnitude(input, dx, dy), cmds);
			set_freeze_ts(tp, 0, FREEZE_MS);
		}
	}
}

void
wstpad_edgescroll(struct wsmouseinput *input, u_int *cmds)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t = tp->t;
	u_int v_edge, b_edge;
	int dx, dy;

	if (!wstpad_scroll_coords(input, &dx, &dy) || tp->contacts != 1)
		return;

	v_edge = (tp->features & WSTPAD_SWAPSIDES) ? L_EDGE : R_EDGE;
	b_edge = (tp->features & WSTPAD_HORIZSCROLL) ? B_EDGE : 0;

	if ((t->flags & v_edge) == 0)
		dy = 0;
	if ((t->flags & b_edge) == 0)
		dx = 0;

	if (dx || dy)
		wstpad_scroll(tp, dx, dy, magnitude(input, dx, dy), cmds);
}

static inline u_int
sbtn(struct wstpad *tp, int x, int y)
{
	if (y >= tp->edge.bottom)
		return (0);
	if ((tp->features & WSTPAD_SOFTMBTN)
	    && x >= tp->edge.center_left
	    && x < tp->edge.center_right)
		return (MIDDLEBTN);
	return ((x < tp->edge.center ? LEFTBTN : RIGHTBTN) ^ tp->sbtnswap);
}

static inline u_int
top_sbtn(struct wstpad *tp, int x, int y)
{
	if (y < tp->edge.top)
		return (0);
	if (x < tp->edge.center_left)
		return (LEFTBTN ^ tp->sbtnswap);
	return (x > tp->edge.center_right
	    ? (RIGHTBTN ^ tp->sbtnswap) : MIDDLEBTN);
}

u_int
wstpad_get_sbtn(struct wsmouseinput *input, int top)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t = tp->t;
	u_int btn;

	btn = 0;
	if (tp->contacts) {
		btn = top ? top_sbtn(tp, t->x, t->y) : sbtn(tp, t->x, t->y);
		/*
		 * If there is no middle-button area, but contacts in both
		 * halves of the edge zone, generate a middle-button event:
		 */
		if (btn && IS_MT(tp) && tp->contacts == 2
		    && !top && !(tp->features & WSTPAD_SOFTMBTN)) {
			if ((t = get_2nd_touch(input)) != NULL)
				btn |= sbtn(tp, t->x, t->y);
			if (btn == (LEFTBTN | RIGHTBTN))
				btn = MIDDLEBTN;
		}
	}
	return (btn != PRIMARYBTN ? btn : 0);
}

int
wstpad_mtbtn_contacts(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t;
	int dx, dy, dist, limit;

	if (tp->ignore != 0)
		return (tp->contacts - 1);

	if (tp->contacts == 2 && (t = get_2nd_touch(input)) != NULL) {
		dx = abs(t->x - tp->t->x) << 12;
		dy = abs(t->y - tp->t->y) * tp->ratio;
		dist = (dx >= dy ? dx + 3 * dy / 8 : dy + 3 * dx / 8);
		limit = tp->params.mtbtn_maxdist << 12;
		if (input->mt.ptr_mask != 0)
			limit = limit * 2 / 3;
		if (dist > limit)
			return (1);
	}
	return (tp->contacts);
}

u_int
wstpad_get_mtbtn(struct wsmouseinput *input)
{
	int contacts = wstpad_mtbtn_contacts(input);
	return (contacts == 2 ? RIGHTBTN : (contacts == 3 ? MIDDLEBTN : 0));
}


void
wstpad_softbuttons(struct wsmouseinput *input, u_int *cmds, int hdlr)
{
	struct wstpad *tp = input->tp;
	int top = (hdlr == TOPBUTTON_HDLR);

	if (tp->softbutton && PRIMARYBTN_RELEASED(tp)) {
		*cmds |= 1 << SOFTBUTTON_UP;
		return;
	}

	if (tp->softbutton == 0 && PRIMARYBTN_CLICKED(tp)) {
		tp->softbutton = ((tp->features & WSTPAD_MTBUTTONS)
		    ? wstpad_get_mtbtn(input) : wstpad_get_sbtn(input, top));
		if (tp->softbutton)
			*cmds |= 1 << SOFTBUTTON_DOWN;
	}
}

/* Check whether the duration of t is within the tap limit. */
int
wstpad_is_tap(struct wstpad *tp, struct tpad_touch *t)
{
	struct timespec ts;

	timespecsub(&tp->time, &t->orig.time, &ts);
	return (timespeccmp(&ts, &tp->tap.maxtime, <));
}

/*
 * At least one MT touch must remain close to its origin and end
 * in the main area.  The same conditions apply to one-finger taps
 * on single-touch devices.
 */
void
wstpad_tap_filter(struct wstpad *tp, struct tpad_touch *t)
{
	int dx, dy, dist = 0;

	if (IS_MT(tp) || tp->tap.contacts == 1) {
		dx = abs(t->x - t->orig.x) << 12;
		dy = abs(t->y - t->orig.y) * tp->ratio;
		dist = (dx >= dy ? dx + 3 * dy / 8 : dy + 3 * dx / 8);
	}
	tp->tap.valid = (CENTERED(t) && dist <= (tp->tap.maxdist << 12));
}


/*
 * Return the oldest touch in the TOUCH_END state, or NULL.
 */
struct tpad_touch *
wstpad_tap_touch(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *s, *t = NULL;
	u_int lifted;
	int slot;

	if (IS_MT(tp)) {
		lifted = (input->mt.sync[MTS_TOUCH] & ~input->mt.touches);
		FOREACHBIT(lifted, slot) {
			s = &tp->tpad_touches[slot];
			if (tp->tap.state == TAP_DETECT && !tp->tap.valid)
				wstpad_tap_filter(tp, s);
			if (t == NULL || timespeccmp(&t->orig.time,
			    &s->orig.time, >))
				t = s;
		}
	} else {
		if (tp->t->state == TOUCH_END) {
			t = tp->t;
			if (tp->tap.state == TAP_DETECT && !tp->tap.valid)
				wstpad_tap_filter(tp, t);
		}
	}

	return (t);
}

/* Determine the "tap button", keep track of whether a touch is masked. */
u_int
wstpad_tap_button(struct wstpad *tp)
{
	int n = tp->tap.contacts - tp->contacts - 1;

	tp->tap.masked = tp->contacts;

	return (n >= 0 && n < TAP_BTNMAP_SIZE ? tp->tap.btnmap[n] : 0);
}

/*
 * In the hold/drag state, do not mask touches if no masking was involved
 * in the preceding tap gesture.
 */
static inline int
tap_unmask(struct wstpad *tp)
{
	return ((tp->tap.button || tp->tap.pending) && tp->tap.masked == 0);
}

/*
 * In the default configuration, this handler maps one-, two-, and
 * three-finger taps to left-button, right-button, and middle-button
 * events, respectively.  Setting the LOCKTIME parameter enables
 * "locked drags", which are finished by a timeout or a tap-to-end
 * gesture.
 */
void
wstpad_tap(struct wsmouseinput *input, u_int *cmds)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t;
	int contacts, is_tap, slot, err = 0;

	/* Synchronize the button states, if necessary. */
	if (input->btn.sync)
		*cmds |= 1 << TAPBUTTON_SYNC;

	/*
	 * It is possible to produce a click within the tap timeout.
	 * Wait for a new touch before generating new button events.
	 */
	if (PRIMARYBTN_RELEASED(tp))
		tp->tap.contacts = 0;

	/* Reset the detection state whenever a new touch starts. */
	if (tp->contacts > tp->prev_contacts || (IS_MT(tp) &&
	    (input->mt.touches & input->mt.sync[MTS_TOUCH]))) {
		tp->tap.contacts = tp->contacts;
		tp->tap.valid = 0;
	}

	/*
	 * The filtered number of active touches excludes a masked
	 * touch if its duration exceeds the tap limit.
	 */
	contacts = tp->contacts;
	if ((slot = ffs(input->mt.ptr_mask) - 1) >= 0
	    && !wstpad_is_tap(tp, &tp->tpad_touches[slot])
	    && !tap_unmask(tp)) {
		contacts--;
	}

	switch (tp->tap.state) {
	case TAP_DETECT:
		/* Find the oldest touch in the TOUCH_END state. */
		t = wstpad_tap_touch(input);
		if (t) {
			is_tap = wstpad_is_tap(tp, t);
			if (is_tap && contacts == 0) {
				if (tp->tap.button)
					*cmds |= 1 << TAPBUTTON_UP;
				tp->tap.pending = (tp->tap.valid
				    ? wstpad_tap_button(tp) : 0);
				if (tp->tap.pending) {
					tp->tap.state = TAP_LIFTED;
					err = !timeout_add_msec(&tp->tap.to,
					    CLICKDELAY_MS);
				}
			} else if (!is_tap && tp->tap.locktime == 0) {
				if (contacts == 0 && tp->tap.button)
					*cmds |= 1 << TAPBUTTON_UP;
				else if (contacts)
					tp->tap.state = TAP_IGNORE;
			} else if (!is_tap && tp->tap.button) {
				if (contacts == 0) {
					tp->tap.state = TAP_LOCKED;
					err = !timeout_add_msec(&tp->tap.to,
					    tp->tap.locktime);
				} else {
					tp->tap.state = TAP_LOCKED_DRAG;
				}
			}
		}
		break;
	case TAP_IGNORE:
		if (contacts == 0) {
			tp->tap.state = TAP_DETECT;
			if (tp->tap.button)
				*cmds |= 1 << TAPBUTTON_UP;
		}
		break;
	case TAP_LIFTED:
		if (contacts) {
			timeout_del(&tp->tap.to);
			tp->tap.state = TAP_DETECT;
			if (tp->tap.pending)
				*cmds |= 1 << TAPBUTTON_DOWN;
		}
		break;
	case TAP_LOCKED:
		if (contacts) {
			timeout_del(&tp->tap.to);
			tp->tap.state = TAP_LOCKED_DRAG;
		}
		break;
	case TAP_LOCKED_DRAG:
		if (contacts == 0) {
			t = wstpad_tap_touch(input);
			if (t && wstpad_is_tap(tp, t)) {
				/* "tap-to-end" */
				*cmds |= 1 << TAPBUTTON_UP;
				tp->tap.state = TAP_DETECT;
			} else {
				tp->tap.state = TAP_LOCKED;
				err = !timeout_add_msec(&tp->tap.to,
				    tp->tap.locktime);
			}
		}
		break;
	}

	if (err) { /* Did timeout_add fail? */
		input->sbtn.buttons &= ~tp->tap.button;
		input->sbtn.sync |= tp->tap.button;
		tp->tap.pending = 0;
		tp->tap.button = 0;
		tp->tap.state = TAP_DETECT;
	}
}

int
wstpad_tap_sync(struct wsmouseinput *input) {
	struct wstpad *tp = input->tp;

	return ((tp->tap.button & (input->btn.buttons | tp->softbutton)) == 0
	    || (tp->tap.button == PRIMARYBTN && tp->softbutton));
}

void
wstpad_tap_timeout(void *p)
{
	struct wsmouseinput *input = p;
	struct wstpad *tp = input->tp;
	struct evq_access evq;
	u_int btn;
	int s, ev;

	s = spltty();
	evq.evar = *input->evar;
	if (evq.evar != NULL && tp != NULL) {
		ev = 0;
		if (tp->tap.pending) {
			tp->tap.button = tp->tap.pending;
			tp->tap.pending = 0;
			input->sbtn.buttons |= tp->tap.button;
			timeout_add_msec(&tp->tap.to, tp->tap.clicktime);
			if (wstpad_tap_sync(input)) {
				ev = BTN_DOWN_EV;
				btn = ffs(tp->tap.button) - 1;
			}
		} else {
			if (wstpad_tap_sync(input)) {
				ev = BTN_UP_EV;
				btn = ffs(tp->tap.button) - 1;
			}
			if (tp->tap.button != tp->softbutton)
				input->sbtn.buttons &= ~tp->tap.button;
			tp->tap.button = 0;
			tp->tap.state = TAP_DETECT;
		}
		if (ev) {
			mtx_enter(&evq.evar->ws_mtx);
			evq.put = evq.evar->ws_put;
			mtx_leave(&evq.evar->ws_mtx);
			evq.result = EVQ_RESULT_NONE;
			getnanotime(&evq.ts);
			wsmouse_evq_put(&evq, ev, btn);
			wsmouse_evq_put(&evq, SYNC_EV, 0);
			if (evq.result == EVQ_RESULT_SUCCESS) {
				if (input->flags & LOG_EVENTS) {
					wsmouse_log_events(input, &evq);
				}
				mtx_enter(&evq.evar->ws_mtx);
				evq.evar->ws_put = evq.put;
				mtx_leave(&evq.evar->ws_mtx);
				wsevent_wakeup(evq.evar);
			} else {
				input->sbtn.sync |= tp->tap.button;
			}
		}
	}
	splx(s);
}

/*
 * Suppress accidental pointer movements after a click on a clickpad.
 */
void
wstpad_click(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;

	if (tp->contacts == 1 &&
	    (PRIMARYBTN_CLICKED(tp) || PRIMARYBTN_RELEASED(tp)))
		set_freeze_ts(tp, 0, FREEZE_MS);
}

/* Translate the "command" bits into the sync-state of wsmouse. */
void
wstpad_cmds(struct wsmouseinput *input, u_int cmds)
{
	struct wstpad *tp = input->tp;
	int n;

	FOREACHBIT(cmds, n) {
		switch (n) {
		case CLEAR_MOTION_DELTAS:
			input->motion.dx = input->motion.dy = 0;
			if (input->motion.dz == 0 && input->motion.dw == 0)
				input->motion.sync &= ~SYNC_DELTAS;
			continue;
		case SOFTBUTTON_DOWN:
			input->btn.sync &= ~PRIMARYBTN;
			input->sbtn.buttons |= tp->softbutton;
			if (tp->softbutton != tp->tap.button)
				input->sbtn.sync |= tp->softbutton;
			continue;
		case SOFTBUTTON_UP:
			input->btn.sync &= ~PRIMARYBTN;
			if (tp->softbutton != tp->tap.button) {
				input->sbtn.buttons &= ~tp->softbutton;
				input->sbtn.sync |= tp->softbutton;
			}
			tp->softbutton = 0;
			continue;
		case TAPBUTTON_SYNC:
			if (tp->tap.button)
				input->btn.sync &= ~tp->tap.button;
			continue;
		case TAPBUTTON_DOWN:
			tp->tap.button = tp->tap.pending;
			tp->tap.pending = 0;
			input->sbtn.buttons |= tp->tap.button;
			if (wstpad_tap_sync(input))
				input->sbtn.sync |= tp->tap.button;
			continue;
		case TAPBUTTON_UP:
			if (tp->tap.button != tp->softbutton)
				input->sbtn.buttons &= ~tp->tap.button;
			if (wstpad_tap_sync(input))
				input->sbtn.sync |= tp->tap.button;
			tp->tap.button = 0;
			continue;
		case HSCROLL:
			input->motion.dw = tp->scroll.dw;
			input->motion.sync |= SYNC_DELTAS;
			continue;
		case VSCROLL:
			input->motion.dz = tp->scroll.dz;
			input->motion.sync |= SYNC_DELTAS;
			continue;
		default:
			printf("[wstpad] invalid cmd %d\n", n);
			break;
		}
	}
}


/*
 * Set the state of touches that have ended. TOUCH_END is a transitional
 * state and will be changed to TOUCH_NONE before process_input() returns.
 */
static inline void
clear_touchstates(struct wsmouseinput *input, enum touchstates state)
{
	u_int touches;
	int slot;

	touches = input->mt.sync[MTS_TOUCH] & ~input->mt.touches;
	FOREACHBIT(touches, slot)
		input->tp->tpad_touches[slot].state = state;
}

void
wstpad_mt_inputs(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t;
	int slot, dx, dy;
	u_int touches, inactive;

	/* TOUCH_BEGIN */
	touches = input->mt.touches & input->mt.sync[MTS_TOUCH];
	FOREACHBIT(touches, slot) {
		t = &tp->tpad_touches[slot];
		t->state = TOUCH_BEGIN;
		t->x = normalize_abs(&input->filter.h, t->pos->x);
		t->y = normalize_abs(&input->filter.v, t->pos->y);
		t->orig.x = t->x;
		t->orig.y = t->y;
		memcpy(&t->orig.time, &tp->time, sizeof(struct timespec));
		t->flags = edge_flags(tp, t->x, t->y);
		wstpad_set_direction(tp, t, 0, 0);
	}

	/* TOUCH_UPDATE */
	touches = input->mt.touches & input->mt.frame;
	if (touches & tp->mtcycle) {
		/*
		 * Slot data may be synchronized separately, in any order,
		 * or not at all if there is no delta.  Identify the touches
		 * without deltas.
		 */
		inactive = input->mt.touches & ~tp->mtcycle;
		tp->mtcycle = touches;
	} else {
		inactive = 0;
		tp->mtcycle |= touches;
	}
	touches = input->mt.touches & ~input->mt.sync[MTS_TOUCH];
	FOREACHBIT(touches, slot) {
		t = &tp->tpad_touches[slot];
		t->state = TOUCH_UPDATE;
		if ((1 << slot) & input->mt.frame) {
			dx = normalize_abs(&input->filter.h, t->pos->x) - t->x;
			t->x += dx;
			dy = normalize_abs(&input->filter.v, t->pos->y) - t->y;
			t->y += dy;
			t->flags &= (~EDGES | edge_flags(tp, t->x, t->y));
			if (wsmouse_hysteresis(input, t->pos))
				dx = dy = 0;
			wstpad_set_direction(tp, t, dx, dy);
		} else if ((1 << slot) & inactive) {
			wstpad_set_direction(tp, t, 0, 0);
		}
	}

	clear_touchstates(input, TOUCH_END);
}

/*
 * Identify "thumb" contacts in the bottom area.  The identification
 * has three stages:
 * 1. If exactly one of two or more touches is in the bottom area, it
 * is masked, which means it does not receive pointer control as long
 * as there are alternatives.  Once set, the mask will only be cleared
 * when the touch is released.
 * Tap detection ignores a masked touch if it does not participate in
 * a tap gesture.
 * 2. If the pointer-controlling touch is moving stably while a masked
 * touch in the bottom area is resting, or only moving minimally, the
 * pointer mask is copied to tp->ignore.  In this stage, the masked
 * touch does not block pointer movement, and it is ignored by
 * wstpad_f2scroll().
 * Decisions are made more or less immediately, there may be errors
 * in edge cases.  If a fast or long upward movement is detected,
 * tp->ignore is cleared.  There is no other transition from stage 2
 * to scrolling, or vice versa, for a pair of touches.
 * 3. If tp->ignore is set and the touch is resting, it is marked as
 * thumb, and it will be ignored until it ends.
 */
void
wstpad_mt_masks(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t;
	struct position *pos;
	u_int mask;
	int slot;

	tp->ignore &= input->mt.touches;

	if (tp->contacts < 2)
		return;

	if (tp->ignore) {
		slot = ffs(tp->ignore) - 1;
		t = &tp->tpad_touches[slot];
		if (t->flags & THUMB)
			return;
		if (t->dir < 0 && wstpad_is_stable(input, t)) {
			t->flags |= THUMB;
			return;
		}
		/* The edge.low area is a bit larger than the bottom area. */
		if (t->y >= tp->edge.low || (NORTH(t->dir) &&
		    magnitude(input, t->pos->dx, t->pos->dy) >= MAG_MEDIUM))
			tp->ignore = 0;
		return;
	}

	if (input->mt.ptr_mask == 0) {
		mask = ~0;
		FOREACHBIT(input->mt.touches, slot) {
			t = &tp->tpad_touches[slot];
			if (t->flags & B_EDGE) {
				mask &= (1 << slot);
				input->mt.ptr_mask = mask;
			}
		}
	}

	if ((input->mt.ptr_mask & ~input->mt.ptr)
	    && !(tp->scroll.dz || tp->scroll.dw)
	    && tp->t->dir >= 0
	    && wstpad_is_stable(input, tp->t)) {

		slot = ffs(input->mt.ptr_mask) - 1;
		t = &tp->tpad_touches[slot];

		if (t->y >= tp->edge.low)
			return;

		if (!wstpad_is_stable(input, t))
			return;

		/* Default hysteresis limits are low.  Make a strict check. */
		pos = tp->t->pos;
		if (abs(pos->acc_dx) < 3 * input->filter.h.hysteresis
		    && abs(pos->acc_dy) < 3 * input->filter.v.hysteresis)
			return;

		if (t->dir >= 0) {
			/* Treat t as thumb if it is slow while tp->t is fast. */
			if (magnitude(input, t->pos->dx, t->pos->dy) > MAG_LOW
			    || magnitude(input, pos->dx, pos->dy) < MAG_MEDIUM)
				return;
		}

		tp->ignore = input->mt.ptr_mask;
	}
}

void
wstpad_touch_inputs(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	struct tpad_touch *t;
	int slot, x, y, dx, dy;

	tp->btns = input->btn.buttons;
	tp->btns_sync = input->btn.sync;

	tp->prev_contacts = tp->contacts;
	tp->contacts = input->touch.contacts;

	if (tp->contacts == 1 &&
	    ((tp->params.f2width &&
	    input->touch.width >= tp->params.f2width)
	    || (tp->params.f2pressure &&
	    input->touch.pressure >= tp->params.f2pressure)))
		tp->contacts = 2;

	if (IS_MT(tp)) {
		wstpad_mt_inputs(input);
		if (input->mt.ptr) {
			slot = ffs(input->mt.ptr) - 1;
			tp->t = &tp->tpad_touches[slot];
		}
		wstpad_mt_masks(input);
	} else {
		t = tp->t;
		if (tp->contacts)
			t->state = (tp->prev_contacts ?
			    TOUCH_UPDATE : TOUCH_BEGIN);
		else
			t->state = (tp->prev_contacts ?
			    TOUCH_END : TOUCH_NONE);

		dx = dy = 0;
		x = normalize_abs(&input->filter.h, t->pos->x);
		y = normalize_abs(&input->filter.v, t->pos->y);
		if (t->state == TOUCH_BEGIN) {
			t->x = t->orig.x = x;
			t->y = t->orig.y = y;
			memcpy(&t->orig.time, &tp->time,
			    sizeof(struct timespec));
			t->flags = edge_flags(tp, x, y);
		} else if (input->motion.sync & SYNC_POSITION) {
			if (!wsmouse_hysteresis(input, t->pos)) {
				dx = x - t->x;
				dy = y - t->y;
			}
			t->x = x;
			t->y = y;
			t->flags &= (~EDGES | edge_flags(tp, x, y));
		}
		wstpad_set_direction(tp, t, dx, dy);
	}
}

static inline int
t2_ignore(struct wsmouseinput *input)
{
	/*
	 * If there are two touches, do not block pointer movement if they
	 * perform a click-and-drag action, or if the second touch is
	 * resting in the bottom area.
	 */
	return (input->tp->contacts == 2 && ((input->tp->btns & PRIMARYBTN)
	    || (input->tp->ignore & ~input->mt.ptr)));
}

void
wstpad_process_input(struct wsmouseinput *input, struct evq_access *evq)
{
	struct wstpad *tp = input->tp;
	u_int handlers, hdlr, cmds;

	memcpy(&tp->time, &evq->ts, sizeof(struct timespec));
	wstpad_touch_inputs(input);

	cmds = 0;
	handlers = tp->handlers;
	if (DISABLE(tp))
		handlers &= ((1 << TOPBUTTON_HDLR) | (1 << SOFTBUTTON_HDLR));

	FOREACHBIT(handlers, hdlr) {
		switch (hdlr) {
		case SOFTBUTTON_HDLR:
		case TOPBUTTON_HDLR:
			wstpad_softbuttons(input, &cmds, hdlr);
			continue;
		case TAP_HDLR:
			wstpad_tap(input, &cmds);
			continue;
		case F2SCROLL_HDLR:
			wstpad_f2scroll(input, &cmds);
			continue;
		case EDGESCROLL_HDLR:
			wstpad_edgescroll(input, &cmds);
			continue;
		case CLICK_HDLR:
			wstpad_click(input);
			continue;
		}
	}

	/* Check whether pointer movement should be blocked. */
	if (input->motion.dx || input->motion.dy) {
		if (DISABLE(tp)
		    || (tp->t->flags & tp->freeze)
		    || timespeccmp(&tp->time, &tp->freeze_ts, <)
		    || (tp->contacts > 1 && !t2_ignore(input))) {

			cmds |= 1 << CLEAR_MOTION_DELTAS;
		}
	}

	wstpad_cmds(input, cmds);

	if (IS_MT(tp))
		clear_touchstates(input, TOUCH_NONE);
}

/*
 * Try to determine the average interval between two updates. Various
 * conditions are checked in order to ensure that only valid samples enter
 * into the calculation. Above all, it is restricted to motion events
 * occurring when there is only one contact. MT devices may need more than
 * one packet to transmit their state if there are multiple touches, and
 * the update frequency may be higher in this case.
 */
void
wstpad_track_interval(struct wsmouseinput *input, struct timespec *time)
{
	static const struct timespec limit = { 0, 30 * 1000000L };
	struct timespec ts;
	int samples;

	if (input->motion.sync == 0
	    || (input->touch.sync & SYNC_CONTACTS)
	    || (input->touch.contacts > 1)) {
		input->intv.track = 0;
		return;
	}
	if (input->intv.track) {
		timespecsub(time, &input->intv.ts, &ts);
		if (timespeccmp(&ts, &limit, <)) {
			/* The unit of the sum is 4096 nanoseconds. */
			input->intv.sum += ts.tv_nsec >> 12;
			samples = ++input->intv.samples;
			/*
			 * Make the first calculation quickly and later
			 * a more reliable one:
			 */
			if (samples == 8) {
				input->intv.avg = input->intv.sum << 9;
				wstpad_init_deceleration(input);
			} else if (samples == 128) {
				input->intv.avg = input->intv.sum << 5;
				wstpad_init_deceleration(input);
				input->intv.samples = 0;
				input->intv.sum = 0;
				input->flags &= ~TRACK_INTERVAL;
			}
		}
	}
	memcpy(&input->intv.ts, time, sizeof(struct timespec));
	input->intv.track = 1;
}



/*
 * The default acceleration options of X don't work convincingly with
 * touchpads (the synaptics driver installs its own "acceleration
 * profile" and callback function). As a preliminary workaround, this
 * filter applies a simple deceleration scheme to small deltas, based
 * on the "magnitude" of the delta pair. A magnitude of 8 corresponds,
 * roughly, to a speed of (filter.dclr / 12.5) device units per milli-
 * second. If its magnitude is smaller than 7 a delta will be downscaled
 * by the factor 2/8, deltas with magnitudes from 7 to 11 by factors
 * ranging from 3/8 to 7/8.
 */
int
wstpad_decelerate(struct wsmouseinput *input, int *dx, int *dy)
{
	int mag, n, h, v;

	mag = magnitude(input, *dx, *dy);

	/* Don't change deceleration levels abruptly. */
	mag = (mag + 7 * input->filter.mag) / 8;
	/* Don't use arbitrarily high values. */
	input->filter.mag = imin(mag, 24 << 12);

	n = imax((mag >> 12) - 4, 2);
	if (n < 8) {
		/* Scale by (n / 8). */
		h = *dx * n + input->filter.h.dclr_rmdr;
		v = *dy * n + input->filter.v.dclr_rmdr;
		input->filter.h.dclr_rmdr = (h >= 0 ? h & 7 : -(-h & 7));
		input->filter.v.dclr_rmdr = (v >= 0 ? v & 7 : -(-v & 7));
		*dx = h / 8;
		*dy = v / 8;
		return (1);
	}
	return (0);
}

void
wstpad_filter(struct wsmouseinput *input)
{
	struct axis_filter *h = &input->filter.h;
	struct axis_filter *v = &input->filter.v;
	struct position *pos = &input->motion.pos;
	int strength = input->filter.mode & 7;
	int dx, dy;

	if (!(input->motion.sync & SYNC_POSITION)
	    || (h->dmax && (abs(pos->dx) > h->dmax))
	    || (v->dmax && (abs(pos->dy) > v->dmax))) {
		dx = dy = 0;
	} else {
		dx = pos->dx;
		dy = pos->dy;
	}

	if (wsmouse_hysteresis(input, pos))
		dx = dy = 0;

	if (input->filter.dclr && wstpad_decelerate(input, &dx, &dy))
		/* Strong smoothing may hamper the precision at low speeds. */
		strength = imin(strength, 2);

	if (strength) {
		if ((input->touch.sync & SYNC_CONTACTS)
		    || input->mt.ptr != input->mt.prev_ptr) {
			h->avg = v->avg = 0;
		}
		/* Use a weighted decaying average for smoothing. */
		dx = dx * (8 - strength) + h->avg * strength + h->avg_rmdr;
		dy = dy * (8 - strength) + v->avg * strength + v->avg_rmdr;
		h->avg_rmdr = (dx >= 0 ? dx & 7 : -(-dx & 7));
		v->avg_rmdr = (dy >= 0 ? dy & 7 : -(-dy & 7));
		dx = h->avg = dx / 8;
		dy = v->avg = dy / 8;
	}

	input->motion.dx = dx;
	input->motion.dy = dy;
}


/*
 * Compatibility-mode conversions. wstpad_filter transforms and filters
 * the coordinate inputs, extended functionality is provided by
 * wstpad_process_input.
 */
void
wstpad_compat_convert(struct wsmouseinput *input, struct evq_access *evq)
{
	if (input->flags & TRACK_INTERVAL)
		wstpad_track_interval(input, &evq->ts);

	wstpad_filter(input);

	if ((input->motion.dx || input->motion.dy)
	    && !(input->motion.sync & SYNC_DELTAS)) {
		input->motion.dz = input->motion.dw = 0;
		input->motion.sync |= SYNC_DELTAS;
	}

	if (input->tp != NULL)
		wstpad_process_input(input, evq);

	input->motion.sync &= ~SYNC_POSITION;
	input->touch.sync = 0;
}

int
wstpad_init(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	int i, slots;

	if (tp != NULL)
		return (0);

	input->tp = tp = malloc(sizeof(struct wstpad),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (tp == NULL)
		return (-1);

	slots = imax(input->mt.num_slots, 1);
	tp->tpad_touches = malloc(slots * sizeof(struct tpad_touch),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	if (tp->tpad_touches == NULL) {
		free(tp, M_DEVBUF, sizeof(struct wstpad));
		return (-1);
	}

	tp->t = &tp->tpad_touches[0];
	if (input->mt.num_slots) {
		tp->features |= WSTPAD_MT;
		for (i = 0; i < input->mt.num_slots; i++)
			tp->tpad_touches[i].pos = &input->mt.slots[i].pos;
	} else {
		tp->t->pos = &input->motion.pos;
	}

	timeout_set(&tp->tap.to, wstpad_tap_timeout, input);

	tp->ratio = input->filter.ratio;

	return (0);
}

/*
 * Integer square root (Halleck's method)
 *
 * An adaption of code from John B. Halleck (from
 * http://www.cc.utah.edu/~nahaj/factoring/code.html). This version is
 * used and published under the OpenBSD license terms with his permission.
 *
 * Cf. also Martin Guy's "Square root by abacus" method.
 */
static inline u_int
isqrt(u_int n)
{
	u_int root, sqbit;

	root = 0;
	sqbit = 1 << (sizeof(u_int) * 8 - 2);
	while (sqbit) {
		if (n >= (sqbit | root)) {
			n -= (sqbit | root);
			root = (root >> 1) | sqbit;
		} else {
			root >>= 1;
		}
		sqbit >>= 2;
	}
	return (root);
}

void
wstpad_init_deceleration(struct wsmouseinput *input)
{
	int n, dclr;

	if ((dclr = input->filter.dclr) == 0)
		return;

	dclr = imax(dclr, 4);

	/*
	 * For a standard update rate of about 80Hz, (dclr) units
	 * will be mapped to a magnitude of 8. If the average rate
	 * is significantly higher or lower, adjust the coefficient
	 * accordingly:
	 */
	if (input->intv.avg == 0) {
		n = 8;
	} else {
		n = 8 * 13000000 / input->intv.avg;
		n = imax(imin(n, 32), 4);
	}
	input->filter.h.mag_scale = (n << 12) / dclr;
	input->filter.v.mag_scale = (input->filter.ratio ?
	    n * input->filter.ratio : n << 12) / dclr;
	input->filter.h.dclr_rmdr = 0;
	input->filter.v.dclr_rmdr = 0;
	input->flags |= TRACK_INTERVAL;
}

int
wstpad_configure(struct wsmouseinput *input)
{
	struct wstpad *tp;
	int width, height, diag, offset, h_res, v_res, h_unit, v_unit, i;

	width = abs(input->hw.x_max - input->hw.x_min);
	height = abs(input->hw.y_max - input->hw.y_min);
	if (width == 0 || height == 0)
		return (-1);	/* We can't do anything. */

	if (input->tp == NULL && wstpad_init(input))
		return (-1);
	tp = input->tp;

	if (!(input->flags & CONFIGURED)) {
		/*
		 * The filter parameters are derived from the length of the
		 * diagonal in device units, with some magic constants which
		 * are partly adapted from libinput or synaptics code, or are
		 * based on tests and guess work.  The absolute resolution
		 * values might not be reliable, but if they are present the
	         * settings are adapted to the ratio.
		 */
		h_res = input->hw.h_res;
		v_res = input->hw.v_res;
		if (h_res == 0 || v_res == 0)
			h_res = v_res = 1;
		diag = isqrt(width * width + height * height);
		input->filter.h.scale = (imin(920, diag) << 12) / diag;
		input->filter.v.scale = input->filter.h.scale * h_res / v_res;
		h_unit = imax(diag / 280, 3);
		v_unit = imax((h_unit * v_res + h_res / 2) / h_res, 3);
		input->filter.h.hysteresis = h_unit;
		input->filter.v.hysteresis = v_unit;
		input->filter.mode = FILTER_MODE_DEFAULT;
		input->filter.dclr = h_unit - h_unit / 5;
		wstpad_init_deceleration(input);

		tp->features &= (WSTPAD_MT | WSTPAD_DISABLE);

		if (input->hw.contacts_max != 1)
			tp->features |= WSTPAD_TWOFINGERSCROLL;
		else
			tp->features |= WSTPAD_EDGESCROLL;

		if (input->hw.hw_type == WSMOUSEHW_CLICKPAD) {
			if (input->hw.type == WSMOUSE_TYPE_SYNAP_SBTN) {
				tp->features |= WSTPAD_TOPBUTTONS;
			} else {
				tp->features |= WSTPAD_SOFTBUTTONS;
				tp->features |= WSTPAD_SOFTMBTN;
			}
		}

		tp->params.left_edge = V_EDGE_RATIO_DEFAULT;
		tp->params.right_edge = V_EDGE_RATIO_DEFAULT;
		tp->params.bottom_edge = ((tp->features & WSTPAD_SOFTBUTTONS)
		    ? B_EDGE_RATIO_DEFAULT : 0);
		tp->params.top_edge = ((tp->features & WSTPAD_TOPBUTTONS)
		    ? T_EDGE_RATIO_DEFAULT : 0);
		tp->params.center_width = CENTER_RATIO_DEFAULT;

		tp->tap.maxtime.tv_nsec = TAP_MAXTIME_DEFAULT * 1000000;
		tp->tap.clicktime = TAP_CLICKTIME_DEFAULT;
		tp->tap.locktime = TAP_LOCKTIME_DEFAULT;

		tp->scroll.hdist = 4 * h_unit;
		tp->scroll.vdist = 4 * v_unit;
		tp->tap.maxdist = 4 * h_unit;

		if (IS_MT(tp) && h_res > 1 && v_res > 1 &&
		    input->hw.hw_type == WSMOUSEHW_CLICKPAD &&
		    (width + h_res / 2) / h_res > 100 &&
		    (height + v_res / 2) / v_res > 60) {
			tp->params.mtbtn_maxdist = h_res * 35;
		} else {
			tp->params.mtbtn_maxdist = -1; /* not available */
		}
	}

	/* A touch with a flag set in this mask does not move the pointer. */
	tp->freeze = EDGES;

	offset = width * tp->params.left_edge / 4096;
	tp->edge.left = (offset ? input->hw.x_min + offset : INT_MIN);
	offset = width * tp->params.right_edge / 4096;
	tp->edge.right = (offset ? input->hw.x_max - offset : INT_MAX);
	offset = height * tp->params.bottom_edge / 4096;
	tp->edge.bottom = (offset ? input->hw.y_min + offset : INT_MIN);
	tp->edge.low = tp->edge.bottom + offset / 2;
	offset = height * tp->params.top_edge / 4096;
	tp->edge.top = (offset ? input->hw.y_max - offset : INT_MAX);

	offset = width * abs(tp->params.center_width) / 8192;
	tp->edge.center = input->hw.x_min + width / 2;
	tp->edge.center_left = tp->edge.center - offset;
	tp->edge.center_right = tp->edge.center + offset;

	/*
	 * Make the MTBUTTONS configuration consistent.  A non-negative 'maxdist'
	 * value makes the feature visible in wsconsctl.  0-values are replaced
	 * by a default (one fourth of the length of the touchpad diagonal).
	 */
	if (tp->params.mtbtn_maxdist < 0) {
		tp->features &= ~WSTPAD_MTBUTTONS;
	} else if (tp->params.mtbtn_maxdist == 0) {
		diag = isqrt(width * width + height * height);
		tp->params.mtbtn_maxdist = diag / 4;
	}

	tp->handlers = 0;

	if (tp->features & (WSTPAD_SOFTBUTTONS | WSTPAD_MTBUTTONS))
		tp->handlers |= 1 << SOFTBUTTON_HDLR;
	if (tp->features & WSTPAD_TOPBUTTONS)
		tp->handlers |= 1 << TOPBUTTON_HDLR;
	if (tp->features & WSTPAD_TWOFINGERSCROLL)
		tp->handlers |= 1 << F2SCROLL_HDLR;
	else if (tp->features & WSTPAD_EDGESCROLL)
		tp->handlers |= 1 << EDGESCROLL_HDLR;

	for (i = 0; i < TAP_BTNMAP_SIZE; i++) {
		if (tp->tap.btnmap[i] == 0)
			continue;

		tp->tap.clicktime = imin(imax(tp->tap.clicktime, 80), 350);
		if (tp->tap.locktime)
			tp->tap.locktime =
			    imin(imax(tp->tap.locktime, 150), 5000);
		tp->handlers |= 1 << TAP_HDLR;
		break;
	}

	if (input->hw.hw_type == WSMOUSEHW_CLICKPAD)
		tp->handlers |= 1 << CLICK_HDLR;

	tp->sbtnswap = ((tp->features & WSTPAD_SWAPSIDES)
	    ? (LEFTBTN | RIGHTBTN) : 0);

	return (0);
}

void
wstpad_reset(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;

	if (tp != NULL) {
		timeout_del(&tp->tap.to);
		tp->tap.state = TAP_DETECT;
	}

	if (input->sbtn.buttons) {
		input->sbtn.sync = input->sbtn.buttons;
		input->sbtn.buttons = 0;
	}
}

void
wstpad_cleanup(struct wsmouseinput *input)
{
	struct wstpad *tp = input->tp;
	int slots;

	timeout_del(&tp->tap.to);
	slots = imax(input->mt.num_slots, 1);
	free(tp->tpad_touches, M_DEVBUF, slots * sizeof(struct tpad_touch));
	free(tp, M_DEVBUF, sizeof(struct wstpad));
	input->tp = NULL;
}

int
wstpad_set_param(struct wsmouseinput *input, int key, int val)
{
	struct wstpad *tp = input->tp;
	u_int flag;

	if (tp == NULL)
		return (EINVAL);

	switch (key) {
	case WSMOUSECFG_SOFTBUTTONS ... WSMOUSECFG_MTBUTTONS:
		switch (key) {
		case WSMOUSECFG_SOFTBUTTONS:
			flag = WSTPAD_SOFTBUTTONS;
			break;
		case WSMOUSECFG_SOFTMBTN:
			flag = WSTPAD_SOFTMBTN;
			break;
		case WSMOUSECFG_TOPBUTTONS:
			flag = WSTPAD_TOPBUTTONS;
			break;
		case WSMOUSECFG_TWOFINGERSCROLL:
			flag = WSTPAD_TWOFINGERSCROLL;
			break;
		case WSMOUSECFG_EDGESCROLL:
			flag = WSTPAD_EDGESCROLL;
			break;
		case WSMOUSECFG_HORIZSCROLL:
			flag = WSTPAD_HORIZSCROLL;
			break;
		case WSMOUSECFG_SWAPSIDES:
			flag = WSTPAD_SWAPSIDES;
			break;
		case WSMOUSECFG_DISABLE:
			flag = WSTPAD_DISABLE;
			break;
		case WSMOUSECFG_MTBUTTONS:
			flag = WSTPAD_MTBUTTONS;
			break;
		}
		if (val)
			tp->features |= flag;
		else
			tp->features &= ~flag;
		break;
	case WSMOUSECFG_LEFT_EDGE:
		tp->params.left_edge = val;
		break;
	case WSMOUSECFG_RIGHT_EDGE:
		tp->params.right_edge = val;
		break;
	case WSMOUSECFG_TOP_EDGE:
		tp->params.top_edge = val;
		break;
	case WSMOUSECFG_BOTTOM_EDGE:
		tp->params.bottom_edge = val;
		break;
	case WSMOUSECFG_CENTERWIDTH:
		tp->params.center_width = val;
		break;
	case WSMOUSECFG_HORIZSCROLLDIST:
		tp->scroll.hdist = val;
		break;
	case WSMOUSECFG_VERTSCROLLDIST:
		tp->scroll.vdist = val;
		break;
	case WSMOUSECFG_F2WIDTH:
		tp->params.f2width = val;
		break;
	case WSMOUSECFG_F2PRESSURE:
		tp->params.f2pressure = val;
		break;
	case WSMOUSECFG_TAP_MAXTIME:
		tp->tap.maxtime.tv_nsec = imin(val, 999) * 1000000;
		break;
	case WSMOUSECFG_TAP_CLICKTIME:
		tp->tap.clicktime = val;
		break;
	case WSMOUSECFG_TAP_LOCKTIME:
		tp->tap.locktime = val;
		break;
	case WSMOUSECFG_TAP_ONE_BTNMAP:
		tp->tap.btnmap[0] = BTNMASK(val);
		break;
	case WSMOUSECFG_TAP_TWO_BTNMAP:
		tp->tap.btnmap[1] = BTNMASK(val);
		break;
	case WSMOUSECFG_TAP_THREE_BTNMAP:
		tp->tap.btnmap[2] = BTNMASK(val);
		break;
	case WSMOUSECFG_MTBTN_MAXDIST:
		if (IS_MT(tp))
			tp->params.mtbtn_maxdist = val;
		break;
	default:
		return (ENOTSUP);
	}

	return (0);
}

int
wstpad_get_param(struct wsmouseinput *input, int key, int *pval)
{
	struct wstpad *tp = input->tp;
	u_int flag;

	if (tp == NULL)
		return (EINVAL);

	switch (key) {
	case WSMOUSECFG_SOFTBUTTONS ... WSMOUSECFG_MTBUTTONS:
		switch (key) {
		case WSMOUSECFG_SOFTBUTTONS:
			flag = WSTPAD_SOFTBUTTONS;
			break;
		case WSMOUSECFG_SOFTMBTN:
			flag = WSTPAD_SOFTMBTN;
			break;
		case WSMOUSECFG_TOPBUTTONS:
			flag = WSTPAD_TOPBUTTONS;
			break;
		case WSMOUSECFG_TWOFINGERSCROLL:
			flag = WSTPAD_TWOFINGERSCROLL;
			break;
		case WSMOUSECFG_EDGESCROLL:
			flag = WSTPAD_EDGESCROLL;
			break;
		case WSMOUSECFG_HORIZSCROLL:
			flag = WSTPAD_HORIZSCROLL;
			break;
		case WSMOUSECFG_SWAPSIDES:
			flag = WSTPAD_SWAPSIDES;
			break;
		case WSMOUSECFG_DISABLE:
			flag = WSTPAD_DISABLE;
			break;
		case WSMOUSECFG_MTBUTTONS:
			flag = WSTPAD_MTBUTTONS;
			break;
		}
		*pval = !!(tp->features & flag);
		break;
	case WSMOUSECFG_LEFT_EDGE:
		*pval = tp->params.left_edge;
		break;
	case WSMOUSECFG_RIGHT_EDGE:
		*pval = tp->params.right_edge;
		break;
	case WSMOUSECFG_TOP_EDGE:
		*pval = tp->params.top_edge;
		break;
	case WSMOUSECFG_BOTTOM_EDGE:
		*pval = tp->params.bottom_edge;
		break;
	case WSMOUSECFG_CENTERWIDTH:
		*pval = tp->params.center_width;
		break;
	case WSMOUSECFG_HORIZSCROLLDIST:
		*pval = tp->scroll.hdist;
		break;
	case WSMOUSECFG_VERTSCROLLDIST:
		*pval = tp->scroll.vdist;
		break;
	case WSMOUSECFG_F2WIDTH:
		*pval = tp->params.f2width;
		break;
	case WSMOUSECFG_F2PRESSURE:
		*pval = tp->params.f2pressure;
		break;
	case WSMOUSECFG_TAP_MAXTIME:
		*pval = tp->tap.maxtime.tv_nsec / 1000000;
		break;
	case WSMOUSECFG_TAP_CLICKTIME:
		*pval = tp->tap.clicktime;
		break;
	case WSMOUSECFG_TAP_LOCKTIME:
		*pval = tp->tap.locktime;
		break;
	case WSMOUSECFG_TAP_ONE_BTNMAP:
		*pval = ffs(tp->tap.btnmap[0]);
		break;
	case WSMOUSECFG_TAP_TWO_BTNMAP:
		*pval = ffs(tp->tap.btnmap[1]);
		break;
	case WSMOUSECFG_TAP_THREE_BTNMAP:
		*pval = ffs(tp->tap.btnmap[2]);
		break;
	case WSMOUSECFG_MTBTN_MAXDIST:
		*pval = tp->params.mtbtn_maxdist;
		break;
	default:
		return (ENOTSUP);
	}

	return (0);
}
