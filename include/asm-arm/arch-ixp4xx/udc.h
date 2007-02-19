/*
 * linux/include/asm-arm/arch-ixp4xx/udc.h
 *
 */
#include <asm/mach/udc_pxa2xx.h>

extern void ixp4xx_set_udc_info(struct pxa2xx_udc_mach_info *info);

static inline int udc_gpio_to_irq(unsigned gpio)
{
	return 0;
}

static inline void udc_gpio_init_vbus(unsigned gpio)
{
}

static inline void udc_gpio_init_pullup(unsigned gpio)
{
}

static inline int udc_gpio_get(unsigned gpio)
{
	return 0;
}

static inline void udc_gpio_set(unsigned gpio, int is_on)
{
}

