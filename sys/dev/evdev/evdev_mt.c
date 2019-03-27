/*-
 * Copyright (c) 2016 Vladimir Kondratyev <wulf@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/evdev/evdev.h>
#include <dev/evdev/evdev_private.h>
#include <dev/evdev/input.h>

#ifdef DEBUG
#define	debugf(fmt, args...)	printf("evdev: " fmt "\n", ##args)
#else
#define	debugf(fmt, args...)
#endif

static uint16_t evdev_fngmap[] = {
	BTN_TOOL_FINGER,
	BTN_TOOL_DOUBLETAP,
	BTN_TOOL_TRIPLETAP,
	BTN_TOOL_QUADTAP,
	BTN_TOOL_QUINTTAP,
};

static uint16_t evdev_mtstmap[][2] = {
	{ ABS_MT_POSITION_X, ABS_X },
	{ ABS_MT_POSITION_Y, ABS_Y },
	{ ABS_MT_PRESSURE, ABS_PRESSURE },
	{ ABS_MT_TOUCH_MAJOR, ABS_TOOL_WIDTH },
};

struct evdev_mt_slot {
	uint64_t ev_report;
	int32_t ev_mt_states[MT_CNT];
};

struct evdev_mt {
	int32_t	ev_mt_last_reported_slot;
	struct evdev_mt_slot ev_mt_slots[];
};

void
evdev_mt_init(struct evdev_dev *evdev)
{
	int32_t slot, slots;

	slots = MAXIMAL_MT_SLOT(evdev) + 1;

	evdev->ev_mt = malloc(offsetof(struct evdev_mt, ev_mt_slots) +
	     sizeof(struct evdev_mt_slot) * slots, M_EVDEV, M_WAITOK | M_ZERO);

	/* Initialize multitouch protocol type B states */
	for (slot = 0; slot < slots; slot++) {
		/*
		 * .ev_report should not be initialized to initial value of
		 * report counter (0) as it brokes free slot detection in
		 * evdev_get_mt_slot_by_tracking_id. So initialize it to -1
		 */
		evdev->ev_mt->ev_mt_slots[slot] = (struct evdev_mt_slot) {
			.ev_report = 0xFFFFFFFFFFFFFFFFULL,
			.ev_mt_states[ABS_MT_INDEX(ABS_MT_TRACKING_ID)] = -1,
		};
	}

	if (bit_test(evdev->ev_flags, EVDEV_FLAG_MT_STCOMPAT))
		evdev_support_mt_compat(evdev);
}

void
evdev_mt_free(struct evdev_dev *evdev)
{

	free(evdev->ev_mt, M_EVDEV);
}

int32_t
evdev_get_last_mt_slot(struct evdev_dev *evdev)
{

	return (evdev->ev_mt->ev_mt_last_reported_slot);
}

void
evdev_set_last_mt_slot(struct evdev_dev *evdev, int32_t slot)
{

	evdev->ev_mt->ev_mt_slots[slot].ev_report = evdev->ev_report_count;
	evdev->ev_mt->ev_mt_last_reported_slot = slot;
}

inline int32_t
evdev_get_mt_value(struct evdev_dev *evdev, int32_t slot, int16_t code)
{

	return (evdev->ev_mt->
	    ev_mt_slots[slot].ev_mt_states[ABS_MT_INDEX(code)]);
}

inline void
evdev_set_mt_value(struct evdev_dev *evdev, int32_t slot, int16_t code,
    int32_t value)
{

	evdev->ev_mt->ev_mt_slots[slot].ev_mt_states[ABS_MT_INDEX(code)] =
	    value;
}

int32_t
evdev_get_mt_slot_by_tracking_id(struct evdev_dev *evdev, int32_t tracking_id)
{
	int32_t tr_id, slot, free_slot = -1;

	for (slot = 0; slot <= MAXIMAL_MT_SLOT(evdev); slot++) {
		tr_id = evdev_get_mt_value(evdev, slot, ABS_MT_TRACKING_ID);
		if (tr_id == tracking_id)
			return (slot);
		/*
		 * Its possible that slot will be reassigned in a place of just
		 * released one within the same report. To avoid this compare
		 * report counter with slot`s report number updated with each
		 * ABS_MT_TRACKING_ID change.
		 */
		if (free_slot == -1 && tr_id == -1 &&
		    evdev->ev_mt->ev_mt_slots[slot].ev_report !=
		    evdev->ev_report_count)
			free_slot = slot;
	}

	return (free_slot);
}

void
evdev_support_nfingers(struct evdev_dev *evdev, int32_t nfingers)
{
	int32_t i;

	for (i = 0; i < MIN(nitems(evdev_fngmap), nfingers); i++)
		evdev_support_key(evdev, evdev_fngmap[i]);
}

void
evdev_support_mt_compat(struct evdev_dev *evdev)
{
	int32_t i;

	if (evdev->ev_absinfo == NULL)
		return;

	evdev_support_event(evdev, EV_KEY);
	evdev_support_key(evdev, BTN_TOUCH);

	/* Touchscreens should not advertise tap tool capabilities */
	if (!bit_test(evdev->ev_prop_flags, INPUT_PROP_DIRECT))
		evdev_support_nfingers(evdev, MAXIMAL_MT_SLOT(evdev) + 1);

	/* Echo 0-th MT-slot as ST-slot */
	for (i = 0; i < nitems(evdev_mtstmap); i++)
		if (bit_test(evdev->ev_abs_flags, evdev_mtstmap[i][0]))
			evdev_support_abs(evdev, evdev_mtstmap[i][1],
			    evdev->ev_absinfo[evdev_mtstmap[i][0]].value,
			    evdev->ev_absinfo[evdev_mtstmap[i][0]].minimum,
			    evdev->ev_absinfo[evdev_mtstmap[i][0]].maximum,
			    evdev->ev_absinfo[evdev_mtstmap[i][0]].fuzz,
			    evdev->ev_absinfo[evdev_mtstmap[i][0]].flat,
			    evdev->ev_absinfo[evdev_mtstmap[i][0]].resolution);
}

static int32_t
evdev_count_fingers(struct evdev_dev *evdev)
{
	int32_t nfingers = 0, i;

	for (i = 0; i <= MAXIMAL_MT_SLOT(evdev); i++)
		if (evdev_get_mt_value(evdev, i, ABS_MT_TRACKING_ID) != -1)
			nfingers++;

	return (nfingers);
}

static void
evdev_send_nfingers(struct evdev_dev *evdev, int32_t nfingers)
{
	int32_t i;

	EVDEV_LOCK_ASSERT(evdev);

	if (nfingers > nitems(evdev_fngmap))
		nfingers = nitems(evdev_fngmap);

	for (i = 0; i < nitems(evdev_fngmap); i++)
		evdev_send_event(evdev, EV_KEY, evdev_fngmap[i],
		    nfingers == i + 1);
}

void
evdev_push_nfingers(struct evdev_dev *evdev, int32_t nfingers)
{

	EVDEV_ENTER(evdev);
	evdev_send_nfingers(evdev, nfingers);
	EVDEV_EXIT(evdev);
}

void
evdev_send_mt_compat(struct evdev_dev *evdev)
{
	int32_t nfingers, i;

	EVDEV_LOCK_ASSERT(evdev);

	nfingers = evdev_count_fingers(evdev);
	evdev_send_event(evdev, EV_KEY, BTN_TOUCH, nfingers > 0);

	if (evdev_get_mt_value(evdev, 0, ABS_MT_TRACKING_ID) != -1)
		/* Echo 0-th MT-slot as ST-slot */
		for (i = 0; i < nitems(evdev_mtstmap); i++)
			if (bit_test(evdev->ev_abs_flags, evdev_mtstmap[i][1]))
				evdev_send_event(evdev, EV_ABS,
				    evdev_mtstmap[i][1],
				    evdev_get_mt_value(evdev, 0,
				    evdev_mtstmap[i][0]));

	/* Touchscreens should not report tool taps */
	if (!bit_test(evdev->ev_prop_flags, INPUT_PROP_DIRECT))
		evdev_send_nfingers(evdev, nfingers);

	if (nfingers == 0)
		evdev_send_event(evdev, EV_ABS, ABS_PRESSURE, 0);
}

void
evdev_push_mt_compat(struct evdev_dev *evdev)
{

	EVDEV_ENTER(evdev);
	evdev_send_mt_compat(evdev);
	EVDEV_EXIT(evdev);
}

void
evdev_send_mt_autorel(struct evdev_dev *evdev)
{
	int32_t slot;

	EVDEV_LOCK_ASSERT(evdev);

	for (slot = 0; slot <= MAXIMAL_MT_SLOT(evdev); slot++) {
		if (evdev->ev_mt->ev_mt_slots[slot].ev_report !=
		    evdev->ev_report_count &&
		    evdev_get_mt_value(evdev, slot, ABS_MT_TRACKING_ID) != -1){
			evdev_send_event(evdev, EV_ABS, ABS_MT_SLOT, slot);
			evdev_send_event(evdev, EV_ABS, ABS_MT_TRACKING_ID,
			    -1);
		}
	}
}
