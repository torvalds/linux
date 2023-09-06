/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 1999-2002 Vojtech Pavlik
 */
#ifndef _GAMEPORT_H
#define _GAMEPORT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <uapi/linux/gameport.h>

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

	struct list_head node;
};
#define to_gameport_port(d)	container_of(d, struct gameport, dev)

struct gameport_driver {
	const char *description;

	int (*connect)(struct gameport *, struct gameport_driver *drv);
	int (*reconnect)(struct gameport *);
	void (*disconnect)(struct gameport *);

	struct device_driver driver;

	bool ignore;
};
#define to_gameport_driver(d)	container_of(d, struct gameport_driver, driver)

int gameport_open(struct gameport *gameport, struct gameport_driver *drv, int mode);
void gameport_close(struct gameport *gameport);

#if IS_REACHABLE(CONFIG_GAMEPORT)

void __gameport_register_port(struct gameport *gameport, struct module *owner);
/* use a define to avoid include chaining to get THIS_MODULE */
#define gameport_register_port(gameport) \
	__gameport_register_port(gameport, THIS_MODULE)

void gameport_unregister_port(struct gameport *gameport);

__printf(2, 3)
void gameport_set_phys(struct gameport *gameport, const char *fmt, ...);

#else

static inline void gameport_register_port(struct gameport *gameport)
{
	return;
}

static inline void gameport_unregister_port(struct gameport *gameport)
{
	return;
}

static inline __printf(2, 3)
void gameport_set_phys(struct gameport *gameport, const char *fmt, ...)
{
	return;
}

#endif

static inline struct gameport *gameport_allocate_port(void)
{
	struct gameport *gameport = kzalloc(sizeof(struct gameport), GFP_KERNEL);

	return gameport;
}

static inline void gameport_free_port(struct gameport *gameport)
{
	kfree(gameport);
}

static inline void gameport_set_name(struct gameport *gameport, const char *name)
{
	strscpy(gameport->name, name, sizeof(gameport->name));
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

int __must_check __gameport_register_driver(struct gameport_driver *drv,
				struct module *owner, const char *mod_name);

/* use a define to avoid include chaining to get THIS_MODULE & friends */
#define gameport_register_driver(drv) \
	__gameport_register_driver(drv, THIS_MODULE, KBUILD_MODNAME)

void gameport_unregister_driver(struct gameport_driver *drv);

/**
 * module_gameport_driver() - Helper macro for registering a gameport driver
 * @__gameport_driver: gameport_driver struct
 *
 * Helper macro for gameport drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module may
 * only use this macro once, and calling it replaces module_init() and
 * module_exit().
 */
#define module_gameport_driver(__gameport_driver) \
	module_driver(__gameport_driver, gameport_register_driver, \
		       gameport_unregister_driver)


static inline void gameport_trigger(struct gameport *gameport)
{
	gameport->trigger(gameport);
}

static inline unsigned char gameport_read(struct gameport *gameport)
{
	return gameport->read(gameport);
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

#endif
