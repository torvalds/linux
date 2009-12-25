/*
 * Remote Controller core header
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _IR_CORE
#define _IR_CORE

#include <linux/input.h>
#include <linux/spinlock.h>

extern int ir_core_debug;
#define IR_dprintk(level, fmt, arg...)	if (ir_core_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt , __func__, ## arg)

enum ir_type {
	IR_TYPE_UNKNOWN	= 0,
	IR_TYPE_RC5	= 1,
	IR_TYPE_PD	= 2,		 /* Pulse distance encoded IR */
	IR_TYPE_NEC	= 3,
	IR_TYPE_OTHER	= 99,
};

struct ir_scancode {
	u16	scancode;
	u32	keycode;
};

struct ir_scancode_table {
	struct ir_scancode	*scan;
	int			size;
	enum			ir_type ir_type;
	spinlock_t		lock;
};

struct ir_input_dev {
	struct input_dev		*dev;
	struct ir_scancode_table	rc_tab;
};

/* Routines from ir-keytable.c */

u32 ir_g_keycode_from_table(struct input_dev *input_dev,
			    u32 scancode);

int ir_set_keycode_table(struct input_dev *input_dev,
			 struct ir_scancode_table *rc_tab);

int ir_roundup_tablesize(int n_elems);
int ir_input_register(struct input_dev *dev,
		      struct ir_scancode_table *ir_codes);
void ir_input_unregister(struct input_dev *input_dev);

#endif
