/*
 *  include/linux/mmc/sdio_func.h
 *
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef MMC_SDIO_FUNC_H
#define MMC_SDIO_FUNC_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct mmc_card;
struct sdio_func;

typedef void (sdio_irq_handler_t)(struct sdio_func *);

/*
 * SDIO function CIS tuple (unknown to the core)
 */
struct sdio_func_tuple {
	struct sdio_func_tuple *next;
	unsigned char code;
	unsigned char size;
	unsigned char data[0];
};

/*
 * SDIO function devices
 */
struct sdio_func {
	struct mmc_card		*card;		/* the card this device belongs to */
	struct device		dev;		/* the device */
	sdio_irq_handler_t	*irq_handler;	/* IRQ callback */
	unsigned int		num;		/* function number */

	unsigned char		class;		/* standard interface class */
	unsigned short		vendor;		/* vendor id */
	unsigned short		device;		/* device id */

	unsigned short		blksize;	/* maximum block size */

	unsigned int		state;		/* function state */
#define SDIO_STATE_PRESENT	(1<<0)		/* present in sysfs */

	struct sdio_func_tuple *tuples;
};

#define sdio_func_present(f)	((f)->state & SDIO_STATE_PRESENT)

#define sdio_func_set_present(f) ((f)->state |= SDIO_STATE_PRESENT)

#define sdio_func_id(f)		((f)->dev.bus_id)

#define sdio_get_drvdata(f)	dev_get_drvdata(&(f)->dev)
#define sdio_set_drvdata(f,d)	dev_set_drvdata(&(f)->dev, d)

/*
 * SDIO function device driver
 */
struct sdio_driver {
	char *name;
	const struct sdio_device_id *id_table;

	int (*probe)(struct sdio_func *, const struct sdio_device_id *);
	void (*remove)(struct sdio_func *);

	struct device_driver drv;
};

/**
 * SDIO_DEVICE - macro used to describe a specific SDIO device
 * @vend: the 16 bit manufacturer code
 * @dev: the 16 bit function id
 *
 * This macro is used to create a struct sdio_device_id that matches a
 * specific device. The class field will be set to SDIO_ANY_ID.
 */
#define SDIO_DEVICE(vend,dev) \
	.class = SDIO_ANY_ID, \
	.vendor = (vend), .device = (dev)

/**
 * SDIO_DEVICE_CLASS - macro used to describe a specific SDIO device class
 * @dev_class: the 8 bit standard interface code
 *
 * This macro is used to create a struct sdio_device_id that matches a
 * specific standard SDIO function type.  The vendor and device fields will
 * be set to SDIO_ANY_ID.
 */
#define SDIO_DEVICE_CLASS(dev_class) \
	.class = (dev_class), \
	.vendor = SDIO_ANY_ID, .device = SDIO_ANY_ID

extern int sdio_register_driver(struct sdio_driver *);
extern void sdio_unregister_driver(struct sdio_driver *);

/*
 * SDIO I/O operations
 */
extern void sdio_claim_host(struct sdio_func *func);
extern void sdio_release_host(struct sdio_func *func);

extern int sdio_enable_func(struct sdio_func *func);
extern int sdio_disable_func(struct sdio_func *func);

extern int sdio_claim_irq(struct sdio_func *func, sdio_irq_handler_t *handler);
extern int sdio_release_irq(struct sdio_func *func);

extern unsigned char sdio_readb(struct sdio_func *func,
	unsigned int addr, int *err_ret);

extern void sdio_writeb(struct sdio_func *func, unsigned char b,
	unsigned int addr, int *err_ret);

#endif

