#ifndef __LINUX_GPIO_H
#define __LINUX_GPIO_H

/* see Documentation/gpio.txt */

#ifdef CONFIG_GENERIC_GPIO
#include <asm/gpio.h>

#else

/*
 * Some platforms don't support the GPIO programming interface.
 *
 * In case some driver uses it anyway (it should normally have
 * depended on GENERIC_GPIO), these routines help the compiler
 * optimize out much GPIO-related code ... or trigger a runtime
 * warning when something is wrongly called.
 */

static inline int gpio_is_valid(int number)
{
	return 0;
}

static inline int gpio_request(unsigned gpio, const char *label)
{
	return -ENOSYS;
}

static inline void gpio_free(unsigned gpio)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline int gpio_direction_input(unsigned gpio)
{
	return -ENOSYS;
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	return -ENOSYS;
}

static inline int gpio_get_value(unsigned gpio)
{
	/* GPIO can never have been requested or set as {in,out}put */
	WARN_ON(1);
	return 0;
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	/* GPIO can never have been requested or set as output */
	WARN_ON(1);
}

static inline int gpio_cansleep(unsigned gpio)
{
	/* GPIO can never have been requested or set as {in,out}put */
	WARN_ON(1);
	return 0;
}

static inline int gpio_get_value_cansleep(unsigned gpio)
{
	/* GPIO can never have been requested or set as {in,out}put */
	WARN_ON(1);
	return 0;
}

static inline void gpio_set_value_cansleep(unsigned gpio, int value)
{
	/* GPIO can never have been requested or set as output */
	WARN_ON(1);
}

static inline int gpio_to_irq(unsigned gpio)
{
	/* GPIO can never have been requested or set as input */
	WARN_ON(1);
	return -EINVAL;
}

static inline int irq_to_gpio(unsigned irq)
{
	/* irq can never have been returned from gpio_to_irq() */
	WARN_ON(1);
	return -EINVAL;
}

#endif

#endif /* __LINUX_GPIO_H */
