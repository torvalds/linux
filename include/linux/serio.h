#ifndef _SERIO_H
#define _SERIO_H

/*
 * Copyright (C) 1999-2002 Vojtech Pavlik
*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/ioctl.h>

#define SPIOCSTYPE	_IOW('q', 0x01, unsigned long)

#ifdef __KERNEL__

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct serio {
	void *port_data;

	char name[32];
	char phys[32];

	unsigned int manual_bind;

	struct serio_device_id id;

	spinlock_t lock;		/* protects critical sections from port's interrupt handler */

	int (*write)(struct serio *, unsigned char);
	int (*open)(struct serio *);
	void (*close)(struct serio *);
	int (*start)(struct serio *);
	void (*stop)(struct serio *);

	struct serio *parent, *child;

	struct serio_driver *drv;	/* accessed from interrupt, must be protected by serio->lock and serio->sem */
	struct semaphore drv_sem;	/* protects serio->drv so attributes can pin driver */

	struct device dev;
	unsigned int registered;	/* port has been fully registered with driver core */

	struct list_head node;
};
#define to_serio_port(d)	container_of(d, struct serio, dev)

struct serio_driver {
	void *private;
	char *description;

	struct serio_device_id *id_table;
	unsigned int manual_bind;

	void (*write_wakeup)(struct serio *);
	irqreturn_t (*interrupt)(struct serio *, unsigned char,
			unsigned int, struct pt_regs *);
	int  (*connect)(struct serio *, struct serio_driver *drv);
	int  (*reconnect)(struct serio *);
	void (*disconnect)(struct serio *);
	void (*cleanup)(struct serio *);

	struct device_driver driver;
};
#define to_serio_driver(d)	container_of(d, struct serio_driver, driver)

int serio_open(struct serio *serio, struct serio_driver *drv);
void serio_close(struct serio *serio);
void serio_rescan(struct serio *serio);
void serio_reconnect(struct serio *serio);
irqreturn_t serio_interrupt(struct serio *serio, unsigned char data, unsigned int flags, struct pt_regs *regs);

void __serio_register_port(struct serio *serio, struct module *owner);
static inline void serio_register_port(struct serio *serio)
{
	__serio_register_port(serio, THIS_MODULE);
}

void serio_unregister_port(struct serio *serio);
void serio_unregister_child_port(struct serio *serio);
void __serio_unregister_port_delayed(struct serio *serio, struct module *owner);
static inline void serio_unregister_port_delayed(struct serio *serio)
{
	__serio_unregister_port_delayed(serio, THIS_MODULE);
}

void __serio_register_driver(struct serio_driver *drv, struct module *owner);
static inline void serio_register_driver(struct serio_driver *drv)
{
	__serio_register_driver(drv, THIS_MODULE);
}

void serio_unregister_driver(struct serio_driver *drv);

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

static inline void serio_cleanup(struct serio *serio)
{
	if (serio->drv && serio->drv->cleanup)
		serio->drv->cleanup(serio);
}

/*
 * Use the following fucntions to manipulate serio's per-port
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
 * Use the following fucntions to protect critical sections in
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

/*
 * Use the following fucntions to pin serio's driver in process context
 */
static inline int serio_pin_driver(struct serio *serio)
{
	return down_interruptible(&serio->drv_sem);
}

static inline void serio_pin_driver_uninterruptible(struct serio *serio)
{
	down(&serio->drv_sem);
}

static inline void serio_unpin_driver(struct serio *serio)
{
	up(&serio->drv_sem);
}


#endif

/*
 * bit masks for use in "interrupt" flags (3rd argument)
 */
#define SERIO_TIMEOUT	1
#define SERIO_PARITY	2
#define SERIO_FRAME	4

/*
 * Serio types
 */
#define SERIO_XT	0x00
#define SERIO_8042	0x01
#define SERIO_RS232	0x02
#define SERIO_HIL_MLC	0x03
#define SERIO_PS_PSTHRU	0x05
#define SERIO_8042_XL	0x06

/*
 * Serio types
 */
#define SERIO_UNKNOWN	0x00
#define SERIO_MSC	0x01
#define SERIO_SUN	0x02
#define SERIO_MS	0x03
#define SERIO_MP	0x04
#define SERIO_MZ	0x05
#define SERIO_MZP	0x06
#define SERIO_MZPP	0x07
#define SERIO_VSXXXAA	0x08
#define SERIO_SUNKBD	0x10
#define SERIO_WARRIOR	0x18
#define SERIO_SPACEORB	0x19
#define SERIO_MAGELLAN	0x1a
#define SERIO_SPACEBALL	0x1b
#define SERIO_GUNZE	0x1c
#define SERIO_IFORCE	0x1d
#define SERIO_STINGER	0x1e
#define SERIO_NEWTON	0x1f
#define SERIO_STOWAWAY	0x20
#define SERIO_H3600	0x21
#define SERIO_PS2SER	0x22
#define SERIO_TWIDKBD	0x23
#define SERIO_TWIDJOY	0x24
#define SERIO_HIL	0x25
#define SERIO_SNES232	0x26
#define SERIO_SEMTECH	0x27
#define SERIO_LKKBD	0x28
#define SERIO_ELO	0x29
#define SERIO_MICROTOUCH	0x30

#endif
