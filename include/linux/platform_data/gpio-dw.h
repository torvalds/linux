#include <linux/kernel.h>

#ifndef __DW_GPIO_PDATA_H__
#define __DW_GPIO_PDATA_H__

#define DW_GPIO_COMPATIBLE "snps,dw-gpio"

struct dw_gpio_platform_data {
	int bank_width;		/* Total width of all GPIO ports */
	int porta_width;	/* Width of port A, for IRQ purposes */
	u32 virtual_irq_start;	/* Start of virtual IRQ address */
};

#endif /* __DW_GPIO_PDATA_H__ */
