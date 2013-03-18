#ifndef _LINUX_ACPI_GPIO_H_
#define _LINUX_ACPI_GPIO_H_

#include <linux/errno.h>
#include <linux/gpio.h>

#ifdef CONFIG_GPIO_ACPI

int acpi_get_gpio(char *path, int pin);
void acpi_gpiochip_request_interrupts(struct gpio_chip *chip);

#else /* CONFIG_GPIO_ACPI */

static inline int acpi_get_gpio(char *path, int pin)
{
	return -ENODEV;
}

static inline void acpi_gpiochip_request_interrupts(struct gpio_chip *chip) { }

#endif /* CONFIG_GPIO_ACPI */

#endif /* _LINUX_ACPI_GPIO_H_ */
