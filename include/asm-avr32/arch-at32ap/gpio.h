#ifndef __ASM_AVR32_ARCH_GPIO_H
#define __ASM_AVR32_ARCH_GPIO_H

#include <linux/compiler.h>
#include <asm/irq.h>


/* Arch-neutral GPIO API */
int __must_check gpio_request(unsigned int gpio, const char *label);
void gpio_free(unsigned int gpio);

int gpio_direction_input(unsigned int gpio);
int gpio_direction_output(unsigned int gpio, int value);
int gpio_get_value(unsigned int gpio);
void gpio_set_value(unsigned int gpio, int value);

static inline int gpio_to_irq(unsigned int gpio)
{
	return gpio + GPIO_IRQ_BASE;
}

static inline int irq_to_gpio(unsigned int irq)
{
	return irq - GPIO_IRQ_BASE;
}

#endif /* __ASM_AVR32_ARCH_GPIO_H */
