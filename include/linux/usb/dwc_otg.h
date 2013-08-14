#ifndef _LINUX_DWC_OTG_H
#define _LINUX_DWC_OTG_H

#include <linux/platform_device.h>

#define DWC_OTG_OF_COMPATIBLE	"snps,dwc2"

extern u64 dwc_otg_dmamask;

/*
 * This is the platform device platform_data structure
 */
struct plat_dwc_otg {
	__iomem void *base;
	unsigned int irq;
};

#endif
