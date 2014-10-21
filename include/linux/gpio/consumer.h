#ifndef __LINUX_GPIO_CONSUMER_H
#define __LINUX_GPIO_CONSUMER_H

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/kernel.h>

struct device;

/**
 * Opaque descriptor for a GPIO. These are obtained using gpiod_get() and are
 * preferable to the old integer-based handles.
 *
 * Contrary to integers, a pointer to a gpio_desc is guaranteed to be valid
 * until the GPIO is released.
 */
struct gpio_desc;

#define GPIOD_FLAGS_BIT_DIR_SET		BIT(0)
#define GPIOD_FLAGS_BIT_DIR_OUT		BIT(1)
#define GPIOD_FLAGS_BIT_DIR_VAL		BIT(2)

/**
 * Optional flags that can be passed to one of gpiod_* to configure direction
 * and output value. These values cannot be OR'd.
 */
enum gpiod_flags {
	GPIOD_ASIS	= 0,
	GPIOD_IN	= GPIOD_FLAGS_BIT_DIR_SET,
	GPIOD_OUT_LOW	= GPIOD_FLAGS_BIT_DIR_SET | GPIOD_FLAGS_BIT_DIR_OUT,
	GPIOD_OUT_HIGH	= GPIOD_FLAGS_BIT_DIR_SET | GPIOD_FLAGS_BIT_DIR_OUT |
			  GPIOD_FLAGS_BIT_DIR_VAL,
};

#ifdef CONFIG_GPIOLIB

/* Acquire and dispose GPIOs */
struct gpio_desc *__must_check __gpiod_get(struct device *dev,
					 const char *con_id,
					 enum gpiod_flags flags);
struct gpio_desc *__must_check __gpiod_get_index(struct device *dev,
					       const char *con_id,
					       unsigned int idx,
					       enum gpiod_flags flags);
struct gpio_desc *__must_check __gpiod_get_optional(struct device *dev,
						  const char *con_id,
						  enum gpiod_flags flags);
struct gpio_desc *__must_check __gpiod_get_index_optional(struct device *dev,
							const char *con_id,
							unsigned int index,
							enum gpiod_flags flags);
void gpiod_put(struct gpio_desc *desc);

struct gpio_desc *__must_check __devm_gpiod_get(struct device *dev,
					      const char *con_id,
					      enum gpiod_flags flags);
struct gpio_desc *__must_check __devm_gpiod_get_index(struct device *dev,
						    const char *con_id,
						    unsigned int idx,
						    enum gpiod_flags flags);
struct gpio_desc *__must_check __devm_gpiod_get_optional(struct device *dev,
						       const char *con_id,
						       enum gpiod_flags flags);
struct gpio_desc *__must_check
__devm_gpiod_get_index_optional(struct device *dev, const char *con_id,
			      unsigned int index, enum gpiod_flags flags);
void devm_gpiod_put(struct device *dev, struct gpio_desc *desc);

int gpiod_get_direction(const struct gpio_desc *desc);
int gpiod_direction_input(struct gpio_desc *desc);
int gpiod_direction_output(struct gpio_desc *desc, int value);
int gpiod_direction_output_raw(struct gpio_desc *desc, int value);

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

/* Child properties interface */
struct fwnode_handle;

struct gpio_desc *fwnode_get_named_gpiod(struct fwnode_handle *fwnode,
					 const char *propname);
struct gpio_desc *devm_get_gpiod_from_child(struct device *dev,
					    struct fwnode_handle *child);
#else /* CONFIG_GPIOLIB */

static inline struct gpio_desc *__must_check __gpiod_get(struct device *dev,
						const char *con_id,
						enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}
static inline struct gpio_desc *__must_check
__gpiod_get_index(struct device *dev,
		  const char *con_id,
		  unsigned int idx,
		  enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_desc *__must_check
__gpiod_get_optional(struct device *dev, const char *con_id,
		     enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_desc *__must_check
__gpiod_get_index_optional(struct device *dev, const char *con_id,
			   unsigned int index, enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline void gpiod_put(struct gpio_desc *desc)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(1);
}

static inline struct gpio_desc *__must_check
__devm_gpiod_get(struct device *dev,
		 const char *con_id,
		 enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}
static inline
struct gpio_desc *__must_check
__devm_gpiod_get_index(struct device *dev,
		       const char *con_id,
		       unsigned int idx,
		       enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_desc *__must_check
__devm_gpiod_get_optional(struct device *dev, const char *con_id,
			  enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_desc *__must_check
__devm_gpiod_get_index_optional(struct device *dev, const char *con_id,
				unsigned int index, enum gpiod_flags flags)
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
static inline int gpiod_direction_output_raw(struct gpio_desc *desc, int value)
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

#endif /* CONFIG_GPIOLIB */

/*
 * Vararg-hacks! This is done to transition the kernel to always pass
 * the options flags argument to the below functions. During a transition
 * phase these vararg macros make both old-and-newstyle code compile,
 * but when all calls to the elder API are removed, these should go away
 * and the __gpiod_get() etc functions above be renamed just gpiod_get()
 * etc.
 */
#define __gpiod_get(dev, con_id, flags, ...) __gpiod_get(dev, con_id, flags)
#define gpiod_get(varargs...) __gpiod_get(varargs, 0)
#define __gpiod_get_index(dev, con_id, index, flags, ...)		\
	__gpiod_get_index(dev, con_id, index, flags)
#define gpiod_get_index(varargs...) __gpiod_get_index(varargs, 0)
#define __gpiod_get_optional(dev, con_id, flags, ...)			\
	__gpiod_get_optional(dev, con_id, flags)
#define gpiod_get_optional(varargs...) __gpiod_get_optional(varargs, 0)
#define __gpiod_get_index_optional(dev, con_id, index, flags, ...)	\
	__gpiod_get_index_optional(dev, con_id, index, flags)
#define gpiod_get_index_optional(varargs...)				\
	__gpiod_get_index_optional(varargs, 0)
#define __devm_gpiod_get(dev, con_id, flags, ...)			\
	__devm_gpiod_get(dev, con_id, flags)
#define devm_gpiod_get(varargs...) __devm_gpiod_get(varargs, 0)
#define __devm_gpiod_get_index(dev, con_id, index, flags, ...)		\
	__devm_gpiod_get_index(dev, con_id, index, flags)
#define devm_gpiod_get_index(varargs...) __devm_gpiod_get_index(varargs, 0)
#define __devm_gpiod_get_optional(dev, con_id, flags, ...)		\
	__devm_gpiod_get_optional(dev, con_id, flags)
#define devm_gpiod_get_optional(varargs...)				\
	__devm_gpiod_get_optional(varargs, 0)
#define __devm_gpiod_get_index_optional(dev, con_id, index, flags, ...)	\
	__devm_gpiod_get_index_optional(dev, con_id, index, flags)
#define devm_gpiod_get_index_optional(varargs...)			\
	__devm_gpiod_get_index_optional(varargs, 0)

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
