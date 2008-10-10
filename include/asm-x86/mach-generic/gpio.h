#ifndef ASM_X86__MACH_GENERIC__GPIO_H
#define ASM_X86__MACH_GENERIC__GPIO_H

int gpio_request(unsigned gpio, const char *label);
void gpio_free(unsigned gpio);
int gpio_direction_input(unsigned gpio);
int gpio_direction_output(unsigned gpio, int value);
int gpio_get_value(unsigned gpio);
void gpio_set_value(unsigned gpio, int value);
int gpio_to_irq(unsigned gpio);
int irq_to_gpio(unsigned irq);

#include <asm-generic/gpio.h>           /* cansleep wrappers */

#endif /* ASM_X86__MACH_GENERIC__GPIO_H */
