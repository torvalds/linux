#ifndef _INPUT_MT_H
#define _INPUT_MT_H

/*
 * Input Multitouch Library
 *
 * Copyright (c) 2010 Henrik Rydberg
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input.h>

#define TRKID_MAX	0xffff

/**
 * struct input_mt_slot - represents the state of an input MT slot
 * @abs: holds current values of ABS_MT axes for this slot
 */
struct input_mt_slot {
	int abs[ABS_MT_LAST - ABS_MT_FIRST + 1];
};

/**
 * struct input_mt - state of tracked contacts
 * @trkid: stores MT tracking ID for the next contact
 * @num_slots: number of MT slots the device uses
 * @slot: MT slot currently being transmitted
 * @flags: input_mt operation flags
 * @slots: array of slots holding current values of tracked contacts
 */
struct input_mt {
	int trkid;
	int num_slots;
	int slot;
	unsigned int flags;
	struct input_mt_slot slots[];
};

static inline void input_mt_set_value(struct input_mt_slot *slot,
				      unsigned code, int value)
{
	slot->abs[code - ABS_MT_FIRST] = value;
}

static inline int input_mt_get_value(const struct input_mt_slot *slot,
				     unsigned code)
{
	return slot->abs[code - ABS_MT_FIRST];
}

int input_mt_init_slots(struct input_dev *dev, unsigned int num_slots,
			unsigned int flags);
void input_mt_destroy_slots(struct input_dev *dev);

static inline int input_mt_new_trkid(struct input_mt *mt)
{
	return mt->trkid++ & TRKID_MAX;
}

static inline void input_mt_slot(struct input_dev *dev, int slot)
{
	input_event(dev, EV_ABS, ABS_MT_SLOT, slot);
}

static inline bool input_is_mt_value(int axis)
{
	return axis >= ABS_MT_FIRST && axis <= ABS_MT_LAST;
}

static inline bool input_is_mt_axis(int axis)
{
	return axis == ABS_MT_SLOT || input_is_mt_value(axis);
}

void input_mt_report_slot_state(struct input_dev *dev,
				unsigned int tool_type, bool active);

void input_mt_report_finger_count(struct input_dev *dev, int count);
void input_mt_report_pointer_emulation(struct input_dev *dev, bool use_count);

#endif
