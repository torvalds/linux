/*
 * include/linux/usb/exynos_usb3_drd.h
 *
 * Copyright (c) 2012 Samsung Electronics Co. Ltd
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * Exynos SuperSpeed USB 3.0 DRD Controller global and OTG registers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_USB_EXYNOS_USB3_DRD_H
#define __LINUX_USB_EXYNOS_USB3_DRD_H

#define EXYNOS_USB3_XHCI_REG_START	0x0
#define EXYNOS_USB3_XHCI_REG_END	0x7FFF
#define EXYNOS_USB3_GLOB_REG_START	0xC100
#define EXYNOS_USB3_GLOB_REG_END	0xC6FF
#define EXYNOS_USB3_DEV_REG_START	0xC700
#define EXYNOS_USB3_DEV_REG_END		0xCBFF
#define EXYNOS_USB3_OTG_REG_START	0xCC00
#define EXYNOS_USB3_OTG_REG_END		0xCCFF

static inline void __orr32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) | val, ptr);
}

static inline void __bic32(void __iomem *ptr, u32 val)
{
	writel(readl(ptr) & ~val, ptr);
}

struct exynos_drd_core;

struct exynos_drd_core_ops {
	void (*core_init)(struct exynos_drd_core *core);
	void (*config)(struct exynos_drd_core *core);
	void (*change_mode)(struct exynos_drd_core *core, bool host);
	void (*phy_set)(struct exynos_drd_core *core);
	void (*phy_unset)(struct exynos_drd_core *core);
	void (*phy20_suspend)(struct exynos_drd_core *core, int suspend);
	void (*phy30_suspend)(struct exynos_drd_core *core, int suspend);
	void (*set_event_buff)(struct exynos_drd_core *core,
			       dma_addr_t event_buff_dma, int size);
	void (*events_enable)(struct exynos_drd_core *core, int on);
	u32 (*get_evntcount)(struct exynos_drd_core *core);
	void (*ack_evntcount)(struct exynos_drd_core *core, u32 val);
};

struct exynos_drd_core {
	u16				release;
	struct exynos_drd_core_ops	*ops;
	struct usb_otg			*otg;
};

struct exynos_drd_core *exynos_drd_bind(struct platform_device *child);
int exynos_drd_try_get(struct platform_device *child);
void exynos_drd_put(struct platform_device *child);
int exynos_drd_switch_id_event(struct platform_device *pdev, int state);
int exynos_drd_switch_vbus_event(struct platform_device *pdev, bool vbus_active);
#endif /* __LINUX_USB_EXYNOS_USB3_DRD_H */
