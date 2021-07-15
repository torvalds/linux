/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/include/amba/bus.h
 *
 *  This device type deals with ARM PrimeCells and anything else that
 *  presents a proper CID (0xB105F00D) at the end of the I/O register
 *  region or that is derived from a PrimeCell.
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 */
#ifndef ASMARM_AMBA_H
#define ASMARM_AMBA_H

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/err.h>
#include <linux/resource.h>
#include <linux/regulator/consumer.h>
#include <linux/android_kabi.h>

#define AMBA_NR_IRQS	9
#define AMBA_CID	0xb105f00d
#define CORESIGHT_CID	0xb105900d

/*
 * CoreSight Architecture specification updates the ID specification
 * for components on the AMBA bus. (ARM IHI 0029E)
 *
 * Bits 15:12 of the CID are the device class.
 *
 * Class 0xF remains for PrimeCell and legacy components. (AMBA_CID above)
 * Class 0x9 defines the component as CoreSight (CORESIGHT_CID above)
 * Class 0x0, 0x1, 0xB, 0xE define components that do not have driver support
 * at present.
 * Class 0x2-0x8,0xA and 0xD-0xD are presently reserved.
 *
 * Remaining CID bits stay as 0xb105-00d
 */

/**
 * Class 0x9 components use additional values to form a Unique Component
 * Identifier (UCI), where peripheral ID values are identical for different
 * components. Passed to the amba bus code from the component driver via
 * the amba_id->data pointer.
 * @devarch	: coresight devarch register value
 * @devarch_mask: mask bits used for matching. 0 indicates UCI not used.
 * @devtype	: coresight device type value
 * @data	: additional driver data. As we have usurped the original
 *		pointer some devices may still need additional data
 */
struct amba_cs_uci_id {
	unsigned int devarch;
	unsigned int devarch_mask;
	unsigned int devtype;
	void *data;
};

/* define offsets for registers used by UCI */
#define UCI_REG_DEVTYPE_OFFSET	0xFCC
#define UCI_REG_DEVARCH_OFFSET	0xFBC

struct clk;

struct amba_device {
	struct device		dev;
	struct resource		res;
	struct clk		*pclk;
	struct device_dma_parameters dma_parms;
	unsigned int		periphid;
	unsigned int		cid;
	struct amba_cs_uci_id	uci;
	unsigned int		irq[AMBA_NR_IRQS];
	char			*driver_override;

	ANDROID_KABI_RESERVE(1);
};

struct amba_driver {
	struct device_driver	drv;
	int			(*probe)(struct amba_device *, const struct amba_id *);
	void			(*remove)(struct amba_device *);
	void			(*shutdown)(struct amba_device *);
	const struct amba_id	*id_table;

	ANDROID_KABI_RESERVE(1);
};

/*
 * Constants for the designer field of the Peripheral ID register. When bit 7
 * is set to '1', bits [6:0] should be the JEP106 manufacturer identity code.
 */
enum amba_vendor {
	AMBA_VENDOR_ARM = 0x41,
	AMBA_VENDOR_ST = 0x80,
	AMBA_VENDOR_QCOM = 0x51,
	AMBA_VENDOR_LSI = 0xb6,
	AMBA_VENDOR_LINUX = 0xfe,	/* This value is not official */
};

/* This is used to generate pseudo-ID for AMBA device */
#define AMBA_LINUX_ID(conf, rev, part) \
	(((conf) & 0xff) << 24 | ((rev) & 0xf) << 20 | \
	AMBA_VENDOR_LINUX << 12 | ((part) & 0xfff))

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

static inline int amba_pclk_enable(struct amba_device *dev)
{
	return clk_enable(dev->pclk);
}

static inline void amba_pclk_disable(struct amba_device *dev)
{
	clk_disable(dev->pclk);
}

static inline int amba_pclk_prepare(struct amba_device *dev)
{
	return clk_prepare(dev->pclk);
}

static inline void amba_pclk_unprepare(struct amba_device *dev)
{
	clk_unprepare(dev->pclk);
}

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

/*
 * builtin_amba_driver() - Helper macro for drivers that don't do anything
 * special in driver initcall.  This eliminates a lot of boilerplate.  Each
 * driver may only use this macro once, and calling it replaces the instance
 * device_initcall().
 */
#define builtin_amba_driver(__amba_drv) \
	builtin_driver(__amba_drv, amba_driver_register)

#endif
