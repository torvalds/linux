#ifndef _GAMEPORT_H
#define _GAMEPORT_H

/*
 *  Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifdef __KERNEL__
#include <asm/io.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/timer.h>

struct gameport {

	void *port_data;	/* Private pointer for gameport drivers */
	char name[32];
	char phys[32];

	int io;
	int speed;
	int fuzz;

	void (*trigger)(struct gameport *);
	unsigned char (*read)(struct gameport *);
	int (*cooked_read)(struct gameport *, int *, int *);
	int (*calibrate)(struct gameport *, int *, int *);
	int (*open)(struct gameport *, int);
	void (*close)(struct gameport *);

	struct timer_list poll_timer;
	unsigned int poll_interval;	/* in msecs */
	spinlock_t timer_lock;
	unsigned int poll_cnt;
	void (*poll_handler)(struct gameport *);

	struct gameport *parent, *child;

	struct gameport_driver *drv;
	struct mutex drv_mutex;		/* protects serio->drv so attributes can pin driver */

	struct device dev;
	unsigned int registered;	/* port has been fully registered with driver core */

	struct list_head node;
};
#define to_gameport_port(d)	container_of(d, struct gameport, dev)

struct gameport_driver {

	void *private;
	char *description;

	int (*connect)(struct gameport *, struct gameport_driver *drv);
	int (*reconnect)(struct gameport *);
	void (*disconnect)(struct gameport *);

	struct device_driver driver;

	unsigned int ignore;
};
#define to_gameport_driver(d)	container_of(d, struct gameport_driver, driver)

int gameport_open(struct gameport *gameport, struct gameport_driver *drv, int mode);
void gameport_close(struct gameport *gameport);
void gameport_rescan(struct gameport *gameport);

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))

void __gameport_register_port(struct gameport *gameport, struct module *owner);
static inline void gameport_register_port(struct gameport *gameport)
{
	__gameport_register_port(gameport, THIS_MODULE);
}

void gameport_unregister_port(struct gameport *gameport);

void gameport_set_phys(struct gameport *gameport, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

#else

static inline void gameport_register_port(struct gameport *gameport)
{
	return;
}

static inline void gameport_unregister_port(struct gameport *gameport)
{
	return;
}

static inline void gameport_set_phys(struct gameport *gameport,
				     const char *fmt, ...)
{
	return;
}

#endif

static inline struct gameport *gameport_allocate_port(void)
{
	struct gameport *gameport = kcalloc(1, sizeof(struct gameport), GFP_KERNEL);

	return gameport;
}

static inline void gameport_free_port(struct gameport *gameport)
{
	kfree(gameport);
}

static inline void gameport_set_name(struct gameport *gameport, const char *name)
{
	strlcpy(gameport->name, name, sizeof(gameport->name));
}

/*
 * Use the following functions to manipulate gameport's per-port
 * driver-specific data.
 */
static inline void *gameport_get_drvdata(struct gameport *gameport)
{
	return dev_get_drvdata(&gameport->dev);
}

static inline void gameport_set_drvdata(struct gameport *gameport, void *data)
{
	dev_set_drvdata(&gameport->dev, data);
}

/*
 * Use the following functions to pin gameport's driver in process context
 */
static inline int gameport_pin_driver(struct gameport *gameport)
{
	return mutex_lock_interruptible(&gameport->drv_mutex);
}

static inline void gameport_unpin_driver(struct gameport *gameport)
{
	mutex_unlock(&gameport->drv_mutex);
}

void __gameport_register_driver(struct gameport_driver *drv, struct module *owner);
static inline void gameport_register_driver(struct gameport_driver *drv)
{
	__gameport_register_driver(drv, THIS_MODULE);
}

void gameport_unregister_driver(struct gameport_driver *drv);

#endif /* __KERNEL__ */

#define GAMEPORT_MODE_DISABLED		0
#define GAMEPORT_MODE_RAW		1
#define GAMEPORT_MODE_COOKED		2

#define GAMEPORT_ID_VENDOR_ANALOG	0x0001
#define GAMEPORT_ID_VENDOR_MADCATZ	0x0002
#define GAMEPORT_ID_VENDOR_LOGITECH	0x0003
#define GAMEPORT_ID_VENDOR_CREATIVE	0x0004
#define GAMEPORT_ID_VENDOR_GENIUS	0x0005
#define GAMEPORT_ID_VENDOR_INTERACT	0x0006
#define GAMEPORT_ID_VENDOR_MICROSOFT	0x0007
#define GAMEPORT_ID_VENDOR_THRUSTMASTER	0x0008
#define GAMEPORT_ID_VENDOR_GRAVIS	0x0009
#define GAMEPORT_ID_VENDOR_GUILLEMOT	0x000a

#ifdef __KERNEL__

static inline void gameport_trigger(struct gameport *gameport)
{
	if (gameport->trigger)
		gameport->trigger(gameport);
	else
		outb(0xff, gameport->io);
}

static inline unsigned char gameport_read(struct gameport *gameport)
{
	if (gameport->read)
		return gameport->read(gameport);
	else
		return inb(gameport->io);
}

static inline int gameport_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	if (gameport->cooked_read)
		return gameport->cooked_read(gameport, axes, buttons);
	else
		return -1;
}

static inline int gameport_calibrate(struct gameport *gameport, int *axes, int *max)
{
	if (gameport->calibrate)
		return gameport->calibrate(gameport, axes, max);
	else
		return -1;
}

static inline int gameport_time(struct gameport *gameport, int time)
{
	return (time * gameport->speed) / 1000;
}

static inline void gameport_set_poll_handler(struct gameport *gameport, void (*handler)(struct gameport *))
{
	gameport->poll_handler = handler;
}

static inline void gameport_set_poll_interval(struct gameport *gameport, unsigned int msecs)
{
	gameport->poll_interval = msecs;
}

void gameport_start_polling(struct gameport *gameport);
void gameport_stop_polling(struct gameport *gameport);

#endif /* __KERNEL__ */
#endif
