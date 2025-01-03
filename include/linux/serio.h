/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 1999-2002 Vojtech Pavlik
 */
#ifndef _SERIO_H
#define _SERIO_H


#include <linux/cleanup.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <uapi/linux/serio.h>

extern const struct bus_type serio_bus;

struct serio {
	void *port_data;

	char name[32];
	char phys[32];
	char firmware_id[128];

	bool manual_bind;

	struct serio_device_id id;

	/* Protects critical sections from port's interrupt handler */
	spinlock_t lock;

	int (*write)(struct serio *, unsigned char);
	int (*open)(struct serio *);
	void (*close)(struct serio *);
	int (*start)(struct serio *);
	void (*stop)(struct serio *);

	struct serio *parent;
	/* Entry in parent->children list */
	struct list_head child_node;
	struct list_head children;
	/* Level of nesting in serio hierarchy */
	unsigned int depth;

	/*
	 * serio->drv is accessed from interrupt handlers; when modifying
	 * caller should acquire serio->drv_mutex and serio->lock.
	 */
	struct serio_driver *drv;
	/* Protects serio->drv so attributes can pin current driver */
	struct mutex drv_mutex;

	struct device dev;

	struct list_head node;

	/*
	 * For use by PS/2 layer when several ports share hardware and
	 * may get indigestion when exposed to concurrent access (i8042).
	 */
	struct mutex *ps2_cmd_mutex;
};
#define to_serio_port(d)	container_of(d, struct serio, dev)

struct serio_driver {
	const char *description;

	const struct serio_device_id *id_table;
	bool manual_bind;

	void (*write_wakeup)(struct serio *);
	irqreturn_t (*interrupt)(struct serio *, unsigned char, unsigned int);
	int  (*connect)(struct serio *, struct serio_driver *drv);
	int  (*reconnect)(struct serio *);
	int  (*fast_reconnect)(struct serio *);
	void (*disconnect)(struct serio *);
	void (*cleanup)(struct serio *);

	struct device_driver driver;
};
#define to_serio_driver(d)	container_of_const(d, struct serio_driver, driver)

int serio_open(struct serio *serio, struct serio_driver *drv);
void serio_close(struct serio *serio);
void serio_rescan(struct serio *serio);
void serio_reconnect(struct serio *serio);
irqreturn_t serio_interrupt(struct serio *serio, unsigned char data, unsigned int flags);

void __serio_register_port(struct serio *serio, struct module *owner);

/* use a define to avoid include chaining to get THIS_MODULE */
#define serio_register_port(serio) \
	__serio_register_port(serio, THIS_MODULE)

void serio_unregister_port(struct serio *serio);
void serio_unregister_child_port(struct serio *serio);

int __must_check __serio_register_driver(struct serio_driver *drv,
				struct module *owner, const char *mod_name);

/* use a define to avoid include chaining to get THIS_MODULE & friends */
#define serio_register_driver(drv) \
	__serio_register_driver(drv, THIS_MODULE, KBUILD_MODNAME)

void serio_unregister_driver(struct serio_driver *drv);

/**
 * module_serio_driver() - Helper macro for registering a serio driver
 * @__serio_driver: serio_driver struct
 *
 * Helper macro for serio drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module
 * may only use this macro once, and calling it replaces module_init()
 * and module_exit().
 */
#define module_serio_driver(__serio_driver) \
	module_driver(__serio_driver, serio_register_driver, \
		       serio_unregister_driver)

static inline int serio_write(struct serio *serio, unsigned char data)
{
	if (serio->write)
		return serio->write(serio, data);
	else
		return -1;
}

static inline void serio_drv_write_wakeup(struct serio *serio)
{
	if (serio->drv && serio->drv->write_wakeup)
		serio->drv->write_wakeup(serio);
}

/*
 * Use the following functions to manipulate serio's per-port
 * driver-specific data.
 */
static inline void *serio_get_drvdata(struct serio *serio)
{
	return dev_get_drvdata(&serio->dev);
}

static inline void serio_set_drvdata(struct serio *serio, void *data)
{
	dev_set_drvdata(&serio->dev, data);
}

/*
 * Use the following functions to protect critical sections in
 * driver code from port's interrupt handler
 */
static inline void serio_pause_rx(struct serio *serio)
{
	spin_lock_irq(&serio->lock);
}

static inline void serio_continue_rx(struct serio *serio)
{
	spin_unlock_irq(&serio->lock);
}

DEFINE_GUARD(serio_pause_rx, struct serio *, serio_pause_rx(_T), serio_continue_rx(_T))

#endif
