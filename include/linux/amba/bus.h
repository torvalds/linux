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
	struct mutex		periphid_lock;
	unsigned int		cid;
	struct amba_cs_uci_id	uci;
	unsigned int		irq[AMBA_NR_IRQS];
	/*
	 * Driver name to force a match.  Do not set directly, because core
	 * frees it.  Use driver_set_override() to set or clear it.
	 */
	const char		*driver_override;
};

struct amba_driver {
	struct device_driver	drv;
	int			(*probe)(struct amba_device *, const struct amba_id *);
	void			(*remove)(struct amba_device *);
	void			(*shutdown)(struct amba_device *);
	const struct amba_id	*id_table;
	/*
	 * For most device drivers, no need to care about this flag as long as
	 * all DMAs are handled through the kernel DMA API. For some special
	 * ones, for example VFIO drivers, they know how to manage the DMA
	 * themselves and set this flag so that the IOMMU layer will allow them
	 * to setup and manage their own I/O address space.
	 */
	bool driver_managed_dma;
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
};

extern const struct bus_type amba_bustype;

#define to_amba_device(d)	container_of_const(d, struct amba_device, dev)

#define amba_get_drvdata(d)	dev_get_drvdata(&d->dev)
#define amba_set_drvdata(d,p)	dev_set_drvdata(&d->dev, p)

/*
 * use a macro to avoid include chaining to get THIS_MODULE
 */
#define amba_driver_register(drv) \
	__amba_driver_register(drv, THIS_MODULE)

#ifdef CONFIG_ARM_AMBA
int __amba_driver_register(struct amba_driver *, struct module *);
void amba_driver_unregister(struct amba_driver *);
#else
static inline int __amba_driver_register(struct amba_driver *drv,
					 struct module *owner)
{
	return -EINVAL;
}
static inline void amba_driver_unregister(struct amba_driver *drv)
{
}
#endif

struct amba_device *amba_device_alloc(const char *, resource_size_t, size_t);
void amba_device_put(struct amba_device *);
int amba_device_add(struct amba_device *, struct resource *);
int amba_device_register(struct amba_device *, struct resource *);
void amba_device_unregister(struct amba_device *);
int amba_request_regions(struct amba_device *, const char *);
void amba_release_regions(struct amba_device *);

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
