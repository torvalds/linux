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
#include <media/rc-map.h>

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
	unsigned int		size;	/* Max number of entries */
	unsigned int		len;	/* Used number of entries */
	unsigned int		alloc;	/* Size of *scan in bytes */
	u64			ir_type;
	char			*name;
	spinlock_t		lock;
};

struct rc_keymap {
	struct list_head	 list;
	struct ir_scancode_table map;
};

struct ir_dev_props {
	unsigned long allowed_protos;
	void 		*priv;
	int (*change_protocol)(void *priv, u64 ir_type);
	int (*open)(void *priv);
	void (*close)(void *priv);
};

struct ir_raw_event {
	struct timespec		delta;	/* Time spent before event */
	enum raw_event_type	type;	/* event type */
};

struct ir_raw_event_ctrl {
	struct kfifo			kfifo;		/* fifo for the pulse/space events */
	struct timespec			last_event;	/* when last event occurred */
};

struct ir_input_dev {
	struct device			dev;		/* device */
	char				*driver_name;	/* Name of the driver module */
	struct ir_scancode_table	rc_tab;		/* scan/key table */
	unsigned long			devno;		/* device number */
	const struct ir_dev_props	*props;		/* Device properties */
	struct ir_raw_event_ctrl	*raw;		/* for raw pulse/space events */
	struct input_dev		*input_dev;	/* the input device associated with this device */

	/* key info - needed by IR keycode handlers */
	spinlock_t			keylock;	/* protects the below members */
	bool				keypressed;	/* current state */
	unsigned long			keyup_jiffies;	/* when should the current keypress be released? */
	struct timer_list		timer_keyup;	/* timer for releasing a keypress */
	u32				last_keycode;	/* keycode of last command */
	u32				last_scancode;	/* scancode of last command */
	u8				last_toggle;	/* toggle of last command */
};

struct ir_raw_handler {
	struct list_head list;

	int (*decode)(struct input_dev *input_dev,
		      struct ir_raw_event *evs,
		      int len);
	int (*raw_register)(struct input_dev *input_dev);
	int (*raw_unregister)(struct input_dev *input_dev);
};

#define to_ir_input_dev(_attr) container_of(_attr, struct ir_input_dev, attr)

/* Routines from rc-map.c */

int ir_register_map(struct rc_keymap *map);
void ir_unregister_map(struct rc_keymap *map);
struct ir_scancode_table *get_rc_map(const char *name);
void rc_map_init(void);

/* Routines from ir-keytable.c */

u32 ir_g_keycode_from_table(struct input_dev *input_dev,
			    u32 scancode);
void ir_repeat(struct input_dev *dev);
void ir_keydown(struct input_dev *dev, int scancode, u8 toggle);
int __ir_input_register(struct input_dev *dev,
		      const struct ir_scancode_table *ir_codes,
		      const struct ir_dev_props *props,
		      const char *driver_name);

static inline int ir_input_register(struct input_dev *dev,
		      const char *map_name,
		      const struct ir_dev_props *props,
		      const char *driver_name) {
	struct ir_scancode_table *ir_codes;
	struct ir_input_dev *ir_dev;
	int rc;

	if (!map_name)
		return -EINVAL;

	ir_codes = get_rc_map(map_name);
	if (!ir_codes)
		return -EINVAL;

	rc = __ir_input_register(dev, ir_codes, props, driver_name);
	if (rc < 0)
		return -EINVAL;

	ir_dev = input_get_drvdata(dev);

	if (!rc && ir_dev->props && ir_dev->props->change_protocol)
		rc = ir_dev->props->change_protocol(ir_dev->props->priv,
						    ir_codes->ir_type);

	return rc;
}

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
void ir_raw_init(void);

/* from ir-nec-decoder.c */
#ifdef CONFIG_IR_NEC_DECODER_MODULE
#define load_nec_decode()	request_module("ir-nec-decoder")
#else
#define load_nec_decode()	0
#endif

#endif /* _IR_CORE */
