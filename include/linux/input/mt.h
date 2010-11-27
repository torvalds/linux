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

/**
 * struct input_mt_slot - represents the state of an input MT slot
 * @abs: holds current values of ABS_MT axes for this slot
 */
struct input_mt_slot {
	int abs[ABS_MT_LAST - ABS_MT_FIRST + 1];
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

int input_mt_create_slots(struct input_dev *dev, unsigned int num_slots);
void input_mt_destroy_slots(struct input_dev *dev);

static inline void input_mt_slot(struct input_dev *dev, int slot)
{
	input_event(dev, EV_ABS, ABS_MT_SLOT, slot);
}

#endif
