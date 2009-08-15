/*
 * Copyright (C) 2006, 2007 Eugene Konev <ejka@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __VLYNQ_H__
#define __VLYNQ_H__

#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>

#define VLYNQ_NUM_IRQS 32

struct vlynq_mapping {
	u32 size;
	u32 offset;
};

enum vlynq_divisor {
	vlynq_div_auto = 0,
	vlynq_ldiv1,
	vlynq_ldiv2,
	vlynq_ldiv3,
	vlynq_ldiv4,
	vlynq_ldiv5,
	vlynq_ldiv6,
	vlynq_ldiv7,
	vlynq_ldiv8,
	vlynq_rdiv1,
	vlynq_rdiv2,
	vlynq_rdiv3,
	vlynq_rdiv4,
	vlynq_rdiv5,
	vlynq_rdiv6,
	vlynq_rdiv7,
	vlynq_rdiv8,
	vlynq_div_external
};

struct vlynq_device_id {
	u32 id;
	enum vlynq_divisor divisor;
	unsigned long driver_data;
};

struct vlynq_regs;
struct vlynq_device {
	u32 id, dev_id;
	int local_irq;
	int remote_irq;
	enum vlynq_divisor divisor;
	u32 regs_start, regs_end;
	u32 mem_start, mem_end;
	u32 irq_start, irq_end;
	int irq;
	int enabled;
	struct vlynq_regs *local;
	struct vlynq_regs *remote;
	struct device dev;
};

struct vlynq_driver {
	char *name;
	struct vlynq_device_id *id_table;
	int (*probe)(struct vlynq_device *dev, struct vlynq_device_id *id);
	void (*remove)(struct vlynq_device *dev);
	struct device_driver driver;
};

struct plat_vlynq_ops {
	int (*on)(struct vlynq_device *dev);
	void (*off)(struct vlynq_device *dev);
};

static inline struct vlynq_driver *to_vlynq_driver(struct device_driver *drv)
{
	return container_of(drv, struct vlynq_driver, driver);
}

static inline struct vlynq_device *to_vlynq_device(struct device *device)
{
	return container_of(device, struct vlynq_device, dev);
}

extern struct bus_type vlynq_bus_type;

extern int __vlynq_register_driver(struct vlynq_driver *driver,
				   struct module *owner);

static inline int vlynq_register_driver(struct vlynq_driver *driver)
{
	return __vlynq_register_driver(driver, THIS_MODULE);
}

static inline void *vlynq_get_drvdata(struct vlynq_device *dev)
{
	return dev_get_drvdata(&dev->dev);
}

static inline void vlynq_set_drvdata(struct vlynq_device *dev, void *data)
{
	dev_set_drvdata(&dev->dev, data);
}

static inline u32 vlynq_mem_start(struct vlynq_device *dev)
{
	return dev->mem_start;
}

static inline u32 vlynq_mem_end(struct vlynq_device *dev)
{
	return dev->mem_end;
}

static inline u32 vlynq_mem_len(struct vlynq_device *dev)
{
	return dev->mem_end - dev->mem_start + 1;
}

static inline int vlynq_virq_to_irq(struct vlynq_device *dev, int virq)
{
	int irq = dev->irq_start + virq;
	if ((irq < dev->irq_start) || (irq > dev->irq_end))
		return -EINVAL;

	return irq;
}

static inline int vlynq_irq_to_virq(struct vlynq_device *dev, int irq)
{
	if ((irq < dev->irq_start) || (irq > dev->irq_end))
		return -EINVAL;

	return irq - dev->irq_start;
}

extern void vlynq_unregister_driver(struct vlynq_driver *driver);
extern int vlynq_enable_device(struct vlynq_device *dev);
extern void vlynq_disable_device(struct vlynq_device *dev);
extern int vlynq_set_local_mapping(struct vlynq_device *dev, u32 tx_offset,
				   struct vlynq_mapping *mapping);
extern int vlynq_set_remote_mapping(struct vlynq_device *dev, u32 tx_offset,
				    struct vlynq_mapping *mapping);
extern int vlynq_set_local_irq(struct vlynq_device *dev, int virq);
extern int vlynq_set_remote_irq(struct vlynq_device *dev, int virq);

#endif /* __VLYNQ_H__ */
