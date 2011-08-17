#ifndef __LINUX_GPIO_H
#define __LINUX_GPIO_H

/* see Documentation/gpio.txt */

/* make these flag values available regardless of GPIO kconfig options */
#define GPIOF_DIR_OUT	(0 << 0)
#define GPIOF_DIR_IN	(1 << 0)

#define GPIOF_INIT_LOW	(0 << 1)
#define GPIOF_INIT_HIGH	(1 << 1)

#define GPIOF_IN		(GPIOF_DIR_IN)
#define GPIOF_OUT_INIT_LOW	(GPIOF_DIR_OUT | GPIOF_INIT_LOW)
#define GPIOF_OUT_INIT_HIGH	(GPIOF_DIR_OUT | GPIOF_INIT_HIGH)

#ifdef CONFIG_GENERIC_GPIO
#include <asm/gpio.h>

#else

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>

struct device;
struct gpio;
struct gpio_chip;

/*
 * Some platforms don't support the GPIO programming interface.
 *
 * In case some driver uses it anyway (it should normally have
 * depended on GENERIC_GPIO), these routines help the compiler
 * optimize out much GPIO-related code ... or trigger a runtime
 * warning when something is wrongly called.
 */

static inline bool gpio_is_valid(int number)
{
	return false;
}

static inline int gpio_request(unsigned gpio, const char *label)
{
	return -ENOSYS;
}

static inline int gpio_request_one(unsigned gpio,
					unsigned long flags, const char *label)
{
	return -ENOSYS;
}

static inline int gpio_request_array(const struct gpio *array, size_t num)
{
	return -ENOSYS;
}

static inline void gpio_free(unsigned gpio)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline void gpio_free_array(const struct gpio *array, size_t num)
{
	might_sleep();

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

static inline int gpio_set_debounce(unsigned gpio, unsigned debounce)
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

static inline int gpio_export(unsigned gpio, bool direction_may_change)
{
	/* GPIO can never have been requested or set as {in,out}put */
	WARN_ON(1);
	return -EINVAL;
}

static inline int gpio_export_link(struct device *dev, const char *name,
				unsigned gpio)
{
	/* GPIO can never have been exported */
	WARN_ON(1);
	return -EINVAL;
}

static inline int gpio_sysfs_set_active_low(unsigned gpio, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -EINVAL;
}

static inline void gpio_unexport(unsigned gpio)
{
	/* GPIO can never have been exported */
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
