/* Public domain. */

#ifndef _LINUX_GPIO_CONSUMER_H
#define _LINUX_GPIO_CONSUMER_H

struct device;
struct gpio_desc;

#define GPIOD_IN		0x0001
#define GPIOD_OUT_HIGH		0x0002

struct gpio_desc *devm_gpiod_get_optional(struct device *, const char *, int);
int	gpiod_get_value_cansleep(const struct gpio_desc *);

static inline int
gpiod_to_irq(const struct gpio_desc *desc)
{
	return 42;
}

#endif
