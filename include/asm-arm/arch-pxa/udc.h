/*
 * linux/include/asm-arm/arch-pxa/udc.h
 *
 * This supports machine-specific differences in how the PXA2xx
 * USB Device Controller (UDC) is wired.
 *
 */
#include <asm/mach/udc_pxa2xx.h>

extern void pxa_set_udc_info(struct pxa2xx_udc_mach_info *info);

static inline int udc_gpio_to_irq(unsigned gpio)
{
	return IRQ_GPIO(gpio & GPIO_MD_MASK_NR);
}

static inline void udc_gpio_init_vbus(unsigned gpio)
{
	pxa_gpio_mode((gpio & GPIO_MD_MASK_NR) | GPIO_IN);
}

static inline void udc_gpio_init_pullup(unsigned gpio)
{
	pxa_gpio_mode((gpio & GPIO_MD_MASK_NR) | GPIO_OUT | GPIO_DFLT_LOW);
}

static inline int udc_gpio_get(unsigned gpio)
{
	return (GPLR(gpio) & GPIO_bit(gpio)) != 0;
}

static inline void udc_gpio_set(unsigned gpio, int is_on)
{
	int mask = GPIO_bit(gpio);

	if (is_on)
		GPSR(gpio) = mask;
	else
		GPCR(gpio) = mask;
}

