/*
 *  linux/include/amba/bus.h
 *
 *  This device type deals with ARM PrimeCells and anything else that
 *  presents a proper CID (0xB105F00D) at the end of the I/O register
 *  region or that is derived from a PrimeCell.
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ASMARM_AMBA_H
#define ASMARM_AMBA_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/err.h>
#include <linux/resource.h>
#include <linux/regulator/consumer.h>

#ifdef CONFIG_ARCH_SOCFPGA
#define AMBA_NR_IRQS	8
#else
#define AMBA_NR_IRQS	2
#endif
#define AMBA_CID	0xb105f00d

struct clk;

struct amba_device {
	struct device		dev;
	struct resource		res;
	struct clk		*pclk;
	u64			dma_mask;
	unsigned int		periphid;
	unsigned int		irq[AMBA_NR_IRQS];
};

struct amba_driver {
	struct device_driver	drv;
	int			(*probe)(struct amba_device *, const struct amba_id *);
	int			(*remove)(struct amba_device *);
	void			(*shutdown)(struct amba_device *);
	int			(*suspend)(struct amba_device *, pm_message_t);
	int			(*resume)(struct amba_device *);
	const struct amba_id	*id_table;
};

enum amba_vendor {
	AMBA_VENDOR_ARM = 0x41,
	AMBA_VENDOR_ST = 0x80,
};

extern struct bus_type amba_bustype;

#define to_amba_device(d)	container_of(d, struct amba_device, dev)

#define amba_get_drvdata(d)	dev_get_drvdata(&d->dev)
#define amba_set_drvdata(d,p)	dev_set_drvdata(&d->dev, p)

int amba_driver_register(struct amba_driver *);
void amba_driver_unregister(struct amba_driver *);
struct amba_device *amba_device_alloc(const char *, resource_size_t, size_t);
void amba_device_put(struct amba_device *);
int amba_device_add(struct amba_device *, struct resource *);
int amba_device_register(struct amba_device *, struct resource *);
struct amba_device *amba_apb_device_add(struct device *parent, const char *name,
					resource_size_t base, size_t size,
					int irq1, int irq2, void *pdata,
					unsigned int periphid);
struct amba_device *amba_ahb_device_add(struct device *parent, const char *name,
					resource_size_t base, size_t size,
					int irq1, int irq2, void *pdata,
					unsigned int periphid);
struct amba_device *
amba_apb_device_add_res(struct device *parent, const char *name,
			resource_size_t base, size_t size, int irq1,
			int irq2, void *pdata, unsigned int periphid,
			struct resource *resbase);
struct amba_device *
amba_ahb_device_add_res(struct device *parent, const char *name,
			resource_size_t base, size_t size, int irq1,
			int irq2, void *pdata, unsigned int periphid,
			struct resource *resbase);
void amba_device_unregister(struct amba_device *);
struct amba_device *amba_find_device(const char *, struct device *, unsigned int, unsigned int);
int amba_request_regions(struct amba_device *, const char *);
void amba_release_regions(struct amba_device *);

#define amba_pclk_enable(d)	\
	(IS_ERR((d)->pclk) ? 0 : clk_enable((d)->pclk))

#define amba_pclk_disable(d)	\
	do { if (!IS_ERR((d)->pclk)) clk_disable((d)->pclk); } while (0)

/* Some drivers don't use the struct amba_device */
#define AMBA_CONFIG_BITS(a) (((a) >> 24) & 0xff)
#define AMBA_REV_BITS(a) (((a) >> 20) & 0x0f)
#define AMBA_MANF_BITS(a) (((a) >> 12) & 0xff)
#define AMBA_PART_BITS(a) ((a) & 0xfff)

#define amba_config(d)	AMBA_CONFIG_BITS((d)->periphid)
#define amba_rev(d)	AMBA_REV_BITS((d)->periphid)
#define amba_manf(d)	AMBA_MANF_BITS((d)->periphid)
#define amba_part(d)	AMBA_PART_BITS((d)->periphid)

#define __AMBA_DEV(busid, data, mask)				\
	{							\
		.coherent_dma_mask = mask,			\
		.init_name = busid,				\
		.platform_data = data,				\
	}

/*
 * APB devices do not themselves have the ability to address memory,
 * so DMA masks should be zero (much like USB peripheral devices.)
 * The DMA controller DMA masks should be used instead (much like
 * USB host controllers in conventional PCs.)
 */
#define AMBA_APB_DEVICE(name, busid, id, base, irqs, data)	\
struct amba_device name##_device = {				\
	.dev = __AMBA_DEV(busid, data, 0),			\
	.res = DEFINE_RES_MEM(base, SZ_4K),			\
	.irq = irqs,						\
	.periphid = id,						\
}

/*
 * AHB devices are DMA capable, so set their DMA masks
 */
#define AMBA_AHB_DEVICE(name, busid, id, base, irqs, data)	\
struct amba_device name##_device = {				\
	.dev = __AMBA_DEV(busid, data, ~0ULL),			\
	.res = DEFINE_RES_MEM(base, SZ_4K),			\
	.dma_mask = ~0ULL,					\
	.irq = irqs,						\
	.periphid = id,						\
}

/*
 * module_amba_driver() - Helper macro for drivers that don't do anything
 * special in module init/exit.  This eliminates a lot of boilerplate.  Each
 * module may only use this macro once, and calling it replaces module_init()
 * and module_exit()
 */
#define module_amba_driver(__amba_drv) \
	module_driver(__amba_drv, amba_driver_register, amba_driver_unregister)

#endif
