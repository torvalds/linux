/*
 * include/asm-sh/bus-sh.h
 *
 * Copyright (C) 2004 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_BUS_SH_H
#define __ASM_SH_BUS_SH_H

extern struct bus_type sh_bus_types[];

struct sh_dev {
	struct device	dev;
	char		*name;
	unsigned int	dev_id;
	unsigned int	bus_id;
	struct resource	res;
	void		*mapbase;
	unsigned int	irq[6];
	u64		*dma_mask;
};

#define to_sh_dev(d)	container_of((d), struct sh_dev, dev)

#define sh_get_drvdata(d)	dev_get_drvdata(&(d)->dev)
#define sh_set_drvdata(d,p)	dev_set_drvdata(&(d)->dev, (p))

struct sh_driver {
	struct device_driver	drv;
	unsigned int		dev_id;
	unsigned int		bus_id;
	int (*probe)(struct sh_dev *);
	int (*remove)(struct sh_dev *);
	int (*suspend)(struct sh_dev *, pm_message_t);
	int (*resume)(struct sh_dev *);
};

#define to_sh_driver(d)	container_of((d), struct sh_driver, drv)
#define sh_name(d)	((d)->dev.driver->name)

/*
 * Device ID numbers for bus types
 */
enum {
	SH_DEV_ID_USB_OHCI,
};

#define SH_NR_BUSES		1
#define SH_BUS_NAME_VIRT	"shbus"

enum {
	SH_BUS_VIRT,
};

/* arch/sh/kernel/cpu/bus.c */
extern int sh_device_register(struct sh_dev *dev);
extern void sh_device_unregister(struct sh_dev *dev);
extern int sh_driver_register(struct sh_driver *drv);
extern void sh_driver_unregister(struct sh_driver *drv);

#endif /* __ASM_SH_BUS_SH_H */

