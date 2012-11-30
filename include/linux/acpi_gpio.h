#ifndef _LINUX_ACPI_GPIO_H_
#define _LINUX_ACPI_GPIO_H_

#include <linux/errno.h>

#ifdef CONFIG_GPIO_ACPI

int acpi_get_gpio(char *path, int pin);

#else /* CONFIG_GPIO_ACPI */

static inline int acpi_get_gpio(char *path, int pin)
{
	return -ENODEV;
}

#endif /* CONFIG_GPIO_ACPI */

#endif /* _LINUX_ACPI_GPIO_H_ */
