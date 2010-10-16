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

#include <linux/spinlock.h>
#include <linux/kfifo.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <media/rc-map.h>

extern int ir_core_debug;
#define IR_dprintk(level, fmt, arg...)	if (ir_core_debug >= level) \
	printk(KERN_DEBUG "%s: " fmt , __func__, ## arg)

enum rc_driver_type {
	RC_DRIVER_SCANCODE = 0,	/* Driver or hardware generates a scancode */
	RC_DRIVER_IR_RAW,	/* Needs a Infra-Red pulse/space decoder */
};

/**
 * struct ir_dev_props - Allow caller drivers to set special properties
 * @driver_type: specifies if the driver or hardware have already a decoder,
 *	or if it needs to use the IR raw event decoders to produce a scancode
 * @allowed_protos: bitmask with the supported IR_TYPE_* protocols
 * @scanmask: some hardware decoders are not capable of providing the full
 *	scancode to the application. As this is a hardware limit, we can't do
 *	anything with it. Yet, as the same keycode table can be used with other
 *	devices, a mask is provided to allow its usage. Drivers should generally
 *	leave this field in blank
 * @timeout: optional time after which device stops sending data
 * @min_timeout: minimum timeout supported by device
 * @max_timeout: maximum timeout supported by device
 * @rx_resolution : resolution (in ns) of input sampler
 * @tx_resolution: resolution (in ns) of output sampler
 * @priv: driver-specific data, to be used on the callbacks
 * @change_protocol: allow changing the protocol used on hardware decoders
 * @open: callback to allow drivers to enable polling/irq when IR input device
 *	is opened.
 * @close: callback to allow drivers to disable polling/irq when IR input device
 *	is opened.
 * @s_tx_mask: set transmitter mask (for devices with multiple tx outputs)
 * @s_tx_carrier: set transmit carrier frequency
 * @s_tx_duty_cycle: set transmit duty cycle (0% - 100%)
 * @s_rx_carrier: inform driver about carrier it is expected to handle
 * @tx_ir: transmit IR
 * @s_idle: optional: enable/disable hardware idle mode, upon which,
	device doesn't interrupt host until it sees IR pulses
 * @s_learning_mode: enable wide band receiver used for learning
 * @s_carrier_report: enable carrier reports
 */
struct ir_dev_props {
	enum rc_driver_type	driver_type;
	unsigned long		allowed_protos;
	u32			scanmask;

	u32			timeout;
	u32			min_timeout;
	u32			max_timeout;

	u32			rx_resolution;
	u32			tx_resolution;

	void			*priv;
	int			(*change_protocol)(void *priv, u64 ir_type);
	int			(*open)(void *priv);
	void			(*close)(void *priv);
	int			(*s_tx_mask)(void *priv, u32 mask);
	int			(*s_tx_carrier)(void *priv, u32 carrier);
	int			(*s_tx_duty_cycle)(void *priv, u32 duty_cycle);
	int			(*s_rx_carrier_range)(void *priv, u32 min, u32 max);
	int			(*tx_ir)(void *priv, int *txbuf, u32 n);
	void			(*s_idle)(void *priv, bool enable);
	int			(*s_learning_mode)(void *priv, int enable);
	int			(*s_carrier_report) (void *priv, int enable);
};

struct ir_input_dev {
	struct device			dev;		/* device */
	char				*driver_name;	/* Name of the driver module */
	struct ir_scancode_table	rc_tab;		/* scan/key table */
	unsigned long			devno;		/* device number */
	struct ir_dev_props		*props;		/* Device properties */
	struct ir_raw_event_ctrl	*raw;		/* for raw pulse/space events */
	struct input_dev		*input_dev;	/* the input device associated with this device */
	bool				idle;

	/* key info - needed by IR keycode handlers */
	spinlock_t			keylock;	/* protects the below members */
	bool				keypressed;	/* current state */
	unsigned long			keyup_jiffies;	/* when should the current keypress be released? */
	struct timer_list		timer_keyup;	/* timer for releasing a keypress */
	u32				last_keycode;	/* keycode of last command */
	u32				last_scancode;	/* scancode of last command */
	u8				last_toggle;	/* toggle of last command */
};

enum raw_event_type {
	IR_SPACE        = (1 << 0),
	IR_PULSE        = (1 << 1),
	IR_START_EVENT  = (1 << 2),
	IR_STOP_EVENT   = (1 << 3),
};

#define to_ir_input_dev(_attr) container_of(_attr, struct ir_input_dev, attr)

/* From ir-keytable.c */
int __ir_input_register(struct input_dev *dev,
		      const struct ir_scancode_table *ir_codes,
		      struct ir_dev_props *props,
		      const char *driver_name);

static inline int ir_input_register(struct input_dev *dev,
		      const char *map_name,
		      struct ir_dev_props *props,
		      const char *driver_name) {
	struct ir_scancode_table *ir_codes;
	struct ir_input_dev *ir_dev;
	int rc;

	if (!map_name)
		return -EINVAL;

	ir_codes = get_rc_map(map_name);
	if (!ir_codes) {
		ir_codes = get_rc_map(RC_MAP_EMPTY);

		if (!ir_codes)
			return -EINVAL;
	}

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

void ir_repeat(struct input_dev *dev);
void ir_keydown(struct input_dev *dev, int scancode, u8 toggle);
void ir_keyup(struct ir_input_dev *ir);
u32 ir_g_keycode_from_table(struct input_dev *input_dev, u32 scancode);

/* From ir-raw-event.c */

struct ir_raw_event {
	union {
		u32             duration;

		struct {
			u32     carrier;
			u8      duty_cycle;
		};
	};

	unsigned                pulse:1;
	unsigned                reset:1;
	unsigned                timeout:1;
	unsigned                carrier_report:1;
};

#define DEFINE_IR_RAW_EVENT(event) \
	struct ir_raw_event event = { \
		{ .duration = 0 } , \
		.pulse = 0, \
		.reset = 0, \
		.timeout = 0, \
		.carrier_report = 0 }

static inline void init_ir_raw_event(struct ir_raw_event *ev)
{
	memset(ev, 0, sizeof(*ev));
}

#define IR_MAX_DURATION         0xFFFFFFFF      /* a bit more than 4 seconds */

void ir_raw_event_handle(struct input_dev *input_dev);
int ir_raw_event_store(struct input_dev *input_dev, struct ir_raw_event *ev);
int ir_raw_event_store_edge(struct input_dev *input_dev, enum raw_event_type type);
int ir_raw_event_store_with_filter(struct input_dev *input_dev,
				struct ir_raw_event *ev);
void ir_raw_event_set_idle(struct input_dev *input_dev, bool idle);

static inline void ir_raw_event_reset(struct input_dev *input_dev)
{
	DEFINE_IR_RAW_EVENT(ev);
	ev.reset = true;

	ir_raw_event_store(input_dev, &ev);
	ir_raw_event_handle(input_dev);
}

#endif /* _IR_CORE */
