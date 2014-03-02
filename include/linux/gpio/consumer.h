#ifndef __LINUX_GPIO_CONSUMER_H
#define __LINUX_GPIO_CONSUMER_H

#include <linux/err.h>
#include <linux/kernel.h>

struct device;
struct gpio_chip;

/**
 * Opaque descriptor for a GPIO. These are obtained using gpiod_get() and are
 * preferable to the old integer-based handles.
 *
 * Contrary to integers, a pointer to a gpio_desc is guaranteed to be valid
 * until the GPIO is released.
 */
struct gpio_desc;

#ifdef CONFIG_GPIOLIB

/* Acquire and dispose GPIOs */
struct gpio_desc *__must_check gpiod_get(struct device *dev,
					 const char *con_id);
struct gpio_desc *__must_check gpiod_get_index(struct device *dev,
					       const char *con_id,
					       unsigned int idx);
void gpiod_put(struct gpio_desc *desc);

struct gpio_desc *__must_check devm_gpiod_get(struct device *dev,
					      const char *con_id);
struct gpio_desc *__must_check devm_gpiod_get_index(struct device *dev,
						    const char *con_id,
						    unsigned int idx);
void devm_gpiod_put(struct device *dev, struct gpio_desc *desc);

int gpiod_get_direction(const struct gpio_desc *desc);
int gpiod_direction_input(struct gpio_desc *desc);
int gpiod_direction_output(struct gpio_desc *desc, int value);

/* Value get/set from non-sleeping context */
int gpiod_get_value(const struct gpio_desc *desc);
void gpiod_set_value(struct gpio_desc *desc, int value);
int gpiod_get_raw_value(const struct gpio_desc *desc);
void gpiod_set_raw_value(struct gpio_desc *desc, int value);

/* Value get/set from sleeping context */
int gpiod_get_value_cansleep(const struct gpio_desc *desc);
void gpiod_set_value_cansleep(struct gpio_desc *desc, int value);
int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc);
void gpiod_set_raw_value_cansleep(struct gpio_desc *desc, int value);

int gpiod_set_debounce(struct gpio_desc *desc, unsigned debounce);

int gpiod_is_active_low(const struct gpio_desc *desc);
int gpiod_cansleep(const struct gpio_desc *desc);

int gpiod_to_irq(const struct gpio_desc *desc);

/* Convert between the old gpio_ and new gpiod_ interfaces */
struct gpio_desc *gpio_to_desc(unsigned gpio);
int desc_to_gpio(const struct gpio_desc *desc);
struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc);

#else /* CONFIG_GPIOLIB */

static inline struct gpio_desc *__must_check gpiod_get(struct device *dev,
						       const char *con_id)
{
	return ERR_PTR(-ENOSYS);
}
static inline struct gpio_desc *__must_check gpiod_get_index(struct device *dev,
							     const char *con_id,
							     unsigned int idx)
{
	return ERR_PTR(-ENOSYS);
}
static inline void gpiod_put(struct gpio_desc *desc)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline struct gpio_desc *__must_check devm_gpiod_get(struct device *dev,
							    const char *con_id)
{
	return ERR_PTR(-ENOSYS);
}
static inline
struct gpio_desc *__must_check devm_gpiod_get_index(struct device *dev,
						    const char *con_id,
						    unsigned int idx)
{
	return ERR_PTR(-ENOSYS);
}
static inline void devm_gpiod_put(struct device *dev, struct gpio_desc *desc)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(1);
}


static inline int gpiod_get_direction(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -ENOSYS;
}
static inline int gpiod_direction_input(struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -ENOSYS;
}
static inline int gpiod_direction_output(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -ENOSYS;
}


static inline int gpiod_get_value(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return 0;
}
static inline void gpiod_set_value(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
}
static inline int gpiod_get_raw_value(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return 0;
}
static inline void gpiod_set_raw_value(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline int gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return 0;
}
static inline void gpiod_set_value_cansleep(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
}
static inline int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return 0;
}
static inline void gpiod_set_raw_value_cansleep(struct gpio_desc *desc,
						int value)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline int gpiod_set_debounce(struct gpio_desc *desc, unsigned debounce)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -ENOSYS;
}

static inline int gpiod_is_active_low(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return 0;
}
static inline int gpiod_cansleep(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return 0;
}

static inline int gpiod_to_irq(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -EINVAL;
}

static inline struct gpio_desc *gpio_to_desc(unsigned gpio)
{
	return ERR_PTR(-EINVAL);
}
static inline int desc_to_gpio(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return -EINVAL;
}
static inline struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(1);
	return ERR_PTR(-ENODEV);
}


#endif /* CONFIG_GPIOLIB */

#if IS_ENABLED(CONFIG_GPIOLIB) && IS_ENABLED(CONFIG_GPIO_SYSFS)

int gpiod_export(struct gpio_desc *desc, bool direction_may_change);
int gpiod_export_link(struct device *dev, const char *name,
		      struct gpio_desc *desc);
int gpiod_sysfs_set_active_low(struct gpio_desc *desc, int value);
void gpiod_unexport(struct gpio_desc *desc);

#else  /* CONFIG_GPIOLIB && CONFIG_GPIO_SYSFS */

static inline int gpiod_export(struct gpio_desc *desc,
			       bool direction_may_change)
{
	return -ENOSYS;
}

static inline int gpiod_export_link(struct device *dev, const char *name,
				    struct gpio_desc *desc)
{
	return -ENOSYS;
}

static inline int gpiod_sysfs_set_active_low(struct gpio_desc *desc, int value)
{
	return -ENOSYS;
}

static inline void gpiod_unexport(struct gpio_desc *desc)
{
}

#endif /* CONFIG_GPIOLIB && CONFIG_GPIO_SYSFS */

#endif
