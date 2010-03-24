/*
 * Remote Controller core header
 *
 * Copyright (C) 2009-2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
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
#include <linux/kfifo.h>
#include <linux/time.h>
#include <linux/timer.h>

extern int ir_core_debug;
#define IR_dprintk(level, fmt, arg...)	if (ir_core_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt , __func__, ## arg)

#define IR_TYPE_UNKNOWN	0
#define IR_TYPE_RC5	(1  << 0)	/* Philips RC5 protocol */
#define IR_TYPE_PD	(1  << 1)	/* Pulse distance encoded IR */
#define IR_TYPE_NEC	(1  << 2)
#define IR_TYPE_OTHER	(((u64)1) << 63l)

enum raw_event_type {
	IR_SPACE	= (1 << 0),
	IR_PULSE	= (1 << 1),
	IR_START_EVENT	= (1 << 2),
	IR_STOP_EVENT	= (1 << 3),
};

struct ir_scancode {
	u16	scancode;
	u32	keycode;
};

struct ir_scancode_table {
	struct ir_scancode	*scan;
	int			size;
	u64			ir_type;
	char			*name;
	spinlock_t		lock;
};

struct ir_dev_props {
	unsigned long allowed_protos;
	void 		*priv;
	int (*change_protocol)(void *priv, u64 ir_type);
};

struct ir_raw_event {
	struct timespec		delta;	/* Time spent before event */
	enum raw_event_type	type;	/* event type */
};

struct ir_raw_event_ctrl {
	struct kfifo			kfifo;		/* fifo for the pulse/space events */
	struct timespec			last_event;	/* when last event occurred */
	struct timer_list		timer_keyup;	/* timer for key release */
};

struct ir_input_dev {
	struct device			dev;		/* device */
	char				*driver_name;	/* Name of the driver module */
	struct ir_scancode_table	rc_tab;		/* scan/key table */
	unsigned long			devno;		/* device number */
	const struct ir_dev_props	*props;		/* Device properties */
	struct ir_raw_event_ctrl	*raw;		/* for raw pulse/space events */

	/* key info - needed by IR keycode handlers */
	u32				keycode;	/* linux key code */
	int				keypressed;	/* current state */
};

struct ir_raw_handler {
	struct list_head list;

	int (*decode)(struct input_dev *input_dev,
		      struct ir_raw_event *evs,
		      int len);
};

#define to_ir_input_dev(_attr) container_of(_attr, struct ir_input_dev, attr)

/* Routines from ir-keytable.c */

u32 ir_g_keycode_from_table(struct input_dev *input_dev,
			    u32 scancode);
void ir_keyup(struct input_dev *dev);
void ir_keydown(struct input_dev *dev, int scancode);
int ir_input_register(struct input_dev *dev,
		      const struct ir_scancode_table *ir_codes,
		      const struct ir_dev_props *props,
		      const char *driver_name);
void ir_input_unregister(struct input_dev *input_dev);

/* Routines from ir-sysfs.c */

int ir_register_class(struct input_dev *input_dev);
void ir_unregister_class(struct input_dev *input_dev);

/* Routines from ir-raw-event.c */
int ir_raw_event_register(struct input_dev *input_dev);
void ir_raw_event_unregister(struct input_dev *input_dev);
int ir_raw_event_store(struct input_dev *input_dev, enum raw_event_type type);
int ir_raw_event_handle(struct input_dev *input_dev);
int ir_raw_handler_register(struct ir_raw_handler *ir_raw_handler);
void ir_raw_handler_unregister(struct ir_raw_handler *ir_raw_handler);

#ifdef MODULE
void ir_raw_init(void);
#else
#define ir_raw_init() 0
#endif

/* from ir-nec-decoder.c */
#ifdef CONFIG_IR_NEC_DECODER_MODULE
#define load_nec_decode()	request_module("ir-nec-decoder")
#else
#define load_nec_decode()	0
#endif

#endif /* _IR_CORE */
