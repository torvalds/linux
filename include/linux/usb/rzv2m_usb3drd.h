/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RZV2M_USB3DRD_H
#define __RZV2M_USB3DRD_H

#include <linux/types.h>

struct rzv2m_usb3drd {
	void __iomem *reg;
	int drd_irq;
	struct device *dev;
	struct reset_control *drd_rstc;
};

#if IS_ENABLED(CONFIG_USB_RZV2M_USB3DRD)
void rzv2m_usb3drd_reset(struct device *dev, bool host);
#else
static inline void rzv2m_usb3drd_reset(struct device *dev, bool host) { }
#endif

#endif /* __RZV2M_USB3DRD_H */
