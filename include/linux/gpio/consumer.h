/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GPIO_CONSUMER_H
#define __LINUX_GPIO_CONSUMER_H

#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/compiler_types.h>
#include <linux/err.h>

struct device;

/**
 * Opaque descriptor for a GPIO. These are obtained using gpiod_get() and are
 * preferable to the old integer-based handles.
 *
 * Contrary to integers, a pointer to a gpio_desc is guaranteed to be valid
 * until the GPIO is released.
 */
struct gpio_desc;

/**
 * Opaque descriptor for a structure of GPIO array attributes.  This structure
 * is attached to struct gpiod_descs obtained from gpiod_get_array() and can be
 * passed back to get/set array functions in order to activate fast processing
 * path if applicable.
 */
struct gpio_array;

/**
 * Struct containing an array of descriptors that can be obtained using
 * gpiod_get_array().
 */
struct gpio_descs {
	struct gpio_array *info;
	unsigned int ndescs;
	struct gpio_desc *desc[];
};

#define GPIOD_FLAGS_BIT_DIR_SET		BIT(0)
#define GPIOD_FLAGS_BIT_DIR_OUT		BIT(1)
#define GPIOD_FLAGS_BIT_DIR_VAL		BIT(2)
#define GPIOD_FLAGS_BIT_OPEN_DRAIN	BIT(3)
#define GPIOD_FLAGS_BIT_NONEXCLUSIVE	BIT(4)

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
	GPIOD_OUT_LOW_OPEN_DRAIN = GPIOD_OUT_LOW | GPIOD_FLAGS_BIT_OPEN_DRAIN,
	GPIOD_OUT_HIGH_OPEN_DRAIN = GPIOD_OUT_HIGH | GPIOD_FLAGS_BIT_OPEN_DRAIN,
};

#ifdef CONFIG_GPIOLIB

/* Return the number of GPIOs associated with a device / function */
int gpiod_count(struct device *dev, const char *con_id);

/* Acquire and dispose GPIOs */
struct gpio_desc *__must_check gpiod_get(struct device *dev,
					 const char *con_id,
					 enum gpiod_flags flags);
struct gpio_desc *__must_check gpiod_get_index(struct device *dev,
					       const char *con_id,
					       unsigned int idx,
					       enum gpiod_flags flags);
struct gpio_desc *__must_check gpiod_get_optional(struct device *dev,
						  const char *con_id,
						  enum gpiod_flags flags);
struct gpio_desc *__must_check gpiod_get_index_optional(struct device *dev,
							const char *con_id,
							unsigned int index,
							enum gpiod_flags flags);
struct gpio_descs *__must_check gpiod_get_array(struct device *dev,
						const char *con_id,
						enum gpiod_flags flags);
struct gpio_descs *__must_check gpiod_get_array_optional(struct device *dev,
							const char *con_id,
							enum gpiod_flags flags);
void gpiod_put(struct gpio_desc *desc);
void gpiod_put_array(struct gpio_descs *descs);

struct gpio_desc *__must_check devm_gpiod_get(struct device *dev,
					      const char *con_id,
					      enum gpiod_flags flags);
struct gpio_desc *__must_check devm_gpiod_get_index(struct device *dev,
						    const char *con_id,
						    unsigned int idx,
						    enum gpiod_flags flags);
struct gpio_desc *__must_check devm_gpiod_get_optional(struct device *dev,
						       const char *con_id,
						       enum gpiod_flags flags);
struct gpio_desc *__must_check
devm_gpiod_get_index_optional(struct device *dev, const char *con_id,
			      unsigned int index, enum gpiod_flags flags);
struct gpio_descs *__must_check devm_gpiod_get_array(struct device *dev,
						     const char *con_id,
						     enum gpiod_flags flags);
struct gpio_descs *__must_check
devm_gpiod_get_array_optional(struct device *dev, const char *con_id,
			      enum gpiod_flags flags);
void devm_gpiod_put(struct device *dev, struct gpio_desc *desc);
void devm_gpiod_unhinge(struct device *dev, struct gpio_desc *desc);
void devm_gpiod_put_array(struct device *dev, struct gpio_descs *descs);

int gpiod_get_direction(struct gpio_desc *desc);
int gpiod_direction_input(struct gpio_desc *desc);
int gpiod_direction_output(struct gpio_desc *desc, int value);
int gpiod_direction_output_raw(struct gpio_desc *desc, int value);

/* Value get/set from non-sleeping context */
int gpiod_get_value(const struct gpio_desc *desc);
int gpiod_get_array_value(unsigned int array_size,
			  struct gpio_desc **desc_array,
			  struct gpio_array *array_info,
			  unsigned long *value_bitmap);
void gpiod_set_value(struct gpio_desc *desc, int value);
int gpiod_set_array_value(unsigned int array_size,
			  struct gpio_desc **desc_array,
			  struct gpio_array *array_info,
			  unsigned long *value_bitmap);
int gpiod_get_raw_value(const struct gpio_desc *desc);
int gpiod_get_raw_array_value(unsigned int array_size,
			      struct gpio_desc **desc_array,
			      struct gpio_array *array_info,
			      unsigned long *value_bitmap);
void gpiod_set_raw_value(struct gpio_desc *desc, int value);
int gpiod_set_raw_array_value(unsigned int array_size,
			      struct gpio_desc **desc_array,
			      struct gpio_array *array_info,
			      unsigned long *value_bitmap);

/* Value get/set from sleeping context */
int gpiod_get_value_cansleep(const struct gpio_desc *desc);
int gpiod_get_array_value_cansleep(unsigned int array_size,
				   struct gpio_desc **desc_array,
				   struct gpio_array *array_info,
				   unsigned long *value_bitmap);
void gpiod_set_value_cansleep(struct gpio_desc *desc, int value);
int gpiod_set_array_value_cansleep(unsigned int array_size,
				   struct gpio_desc **desc_array,
				   struct gpio_array *array_info,
				   unsigned long *value_bitmap);
int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc);
int gpiod_get_raw_array_value_cansleep(unsigned int array_size,
				       struct gpio_desc **desc_array,
				       struct gpio_array *array_info,
				       unsigned long *value_bitmap);
void gpiod_set_raw_value_cansleep(struct gpio_desc *desc, int value);
int gpiod_set_raw_array_value_cansleep(unsigned int array_size,
				       struct gpio_desc **desc_array,
				       struct gpio_array *array_info,
				       unsigned long *value_bitmap);

int gpiod_set_config(struct gpio_desc *desc, unsigned long config);
int gpiod_set_debounce(struct gpio_desc *desc, unsigned int debounce);
int gpiod_set_transitory(struct gpio_desc *desc, bool transitory);
void gpiod_toggle_active_low(struct gpio_desc *desc);

int gpiod_is_active_low(const struct gpio_desc *desc);
int gpiod_cansleep(const struct gpio_desc *desc);

int gpiod_to_irq(const struct gpio_desc *desc);
int gpiod_set_consumer_name(struct gpio_desc *desc, const char *name);

/* Convert between the old gpio_ and new gpiod_ interfaces */
struct gpio_desc *gpio_to_desc(unsigned gpio);
int desc_to_gpio(const struct gpio_desc *desc);

/* Child properties interface */
struct fwnode_handle;

struct gpio_desc *fwnode_get_named_gpiod(struct fwnode_handle *fwnode,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label);
struct gpio_desc *fwnode_gpiod_get_index(struct fwnode_handle *fwnode,
					 const char *con_id, int index,
					 enum gpiod_flags flags,
					 const char *label);
struct gpio_desc *devm_fwnode_gpiod_get_index(struct device *dev,
					      struct fwnode_handle *child,
					      const char *con_id, int index,
					      enum gpiod_flags flags,
					      const char *label);

#else /* CONFIG_GPIOLIB */

#include <linux/kernel.h>

static inline int gpiod_count(struct device *dev, const char *con_id)
{
	return 0;
}

static inline struct gpio_desc *__must_check gpiod_get(struct device *dev,
						       const char *con_id,
						       enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}
static inline struct gpio_desc *__must_check
gpiod_get_index(struct device *dev,
		const char *con_id,
		unsigned int idx,
		enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_desc *__must_check
gpiod_get_optional(struct device *dev, const char *con_id,
		   enum gpiod_flags flags)
{
	return NULL;
}

static inline struct gpio_desc *__must_check
gpiod_get_index_optional(struct device *dev, const char *con_id,
			 unsigned int index, enum gpiod_flags flags)
{
	return NULL;
}

static inline struct gpio_descs *__must_check
gpiod_get_array(struct device *dev, const char *con_id,
		enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_descs *__must_check
gpiod_get_array_optional(struct device *dev, const char *con_id,
			 enum gpiod_flags flags)
{
	return NULL;
}

static inline void gpiod_put(struct gpio_desc *desc)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(desc);
}

static inline void devm_gpiod_unhinge(struct device *dev,
				      struct gpio_desc *desc)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(desc);
}

static inline void gpiod_put_array(struct gpio_descs *descs)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(descs);
}

static inline struct gpio_desc *__must_check
devm_gpiod_get(struct device *dev,
		 const char *con_id,
		 enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}
static inline
struct gpio_desc *__must_check
devm_gpiod_get_index(struct device *dev,
		       const char *con_id,
		       unsigned int idx,
		       enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_desc *__must_check
devm_gpiod_get_optional(struct device *dev, const char *con_id,
			  enum gpiod_flags flags)
{
	return NULL;
}

static inline struct gpio_desc *__must_check
devm_gpiod_get_index_optional(struct device *dev, const char *con_id,
				unsigned int index, enum gpiod_flags flags)
{
	return NULL;
}

static inline struct gpio_descs *__must_check
devm_gpiod_get_array(struct device *dev, const char *con_id,
		     enum gpiod_flags flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct gpio_descs *__must_check
devm_gpiod_get_array_optional(struct device *dev, const char *con_id,
			      enum gpiod_flags flags)
{
	return NULL;
}

static inline void devm_gpiod_put(struct device *dev, struct gpio_desc *desc)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(desc);
}

static inline void devm_gpiod_put_array(struct device *dev,
					struct gpio_descs *descs)
{
	might_sleep();

	/* GPIO can never have been requested */
	WARN_ON(descs);
}


static inline int gpiod_get_direction(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}
static inline int gpiod_direction_input(struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}
static inline int gpiod_direction_output(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}
static inline int gpiod_direction_output_raw(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}


static inline int gpiod_get_value(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return 0;
}
static inline int gpiod_get_array_value(unsigned int array_size,
					struct gpio_desc **desc_array,
					struct gpio_array *array_info,
					unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}
static inline void gpiod_set_value(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
}
static inline int gpiod_set_array_value(unsigned int array_size,
					struct gpio_desc **desc_array,
					struct gpio_array *array_info,
					unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}
static inline int gpiod_get_raw_value(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return 0;
}
static inline int gpiod_get_raw_array_value(unsigned int array_size,
					    struct gpio_desc **desc_array,
					    struct gpio_array *array_info,
					    unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}
static inline void gpiod_set_raw_value(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
}
static inline int gpiod_set_raw_array_value(unsigned int array_size,
					    struct gpio_desc **desc_array,
					    struct gpio_array *array_info,
					    unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}

static inline int gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return 0;
}
static inline int gpiod_get_array_value_cansleep(unsigned int array_size,
				     struct gpio_desc **desc_array,
				     struct gpio_array *array_info,
				     unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}
static inline void gpiod_set_value_cansleep(struct gpio_desc *desc, int value)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
}
static inline int gpiod_set_array_value_cansleep(unsigned int array_size,
					    struct gpio_desc **desc_array,
					    struct gpio_array *array_info,
					    unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}
static inline int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return 0;
}
static inline int gpiod_get_raw_array_value_cansleep(unsigned int array_size,
					       struct gpio_desc **desc_array,
					       struct gpio_array *array_info,
					       unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}
static inline void gpiod_set_raw_value_cansleep(struct gpio_desc *desc,
						int value)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
}
static inline int gpiod_set_raw_array_value_cansleep(unsigned int array_size,
						struct gpio_desc **desc_array,
						struct gpio_array *array_info,
						unsigned long *value_bitmap)
{
	/* GPIO can never have been requested */
	WARN_ON(desc_array);
	return 0;
}

static inline int gpiod_set_config(struct gpio_desc *desc, unsigned long config)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}

static inline int gpiod_set_debounce(struct gpio_desc *desc, unsigned int debounce)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}

static inline int gpiod_set_transitory(struct gpio_desc *desc, bool transitory)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -ENOSYS;
}

static inline void gpiod_toggle_active_low(struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
}

static inline int gpiod_is_active_low(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return 0;
}
static inline int gpiod_cansleep(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return 0;
}

static inline int gpiod_to_irq(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -EINVAL;
}

static inline int gpiod_set_consumer_name(struct gpio_desc *desc,
					  const char *name)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -EINVAL;
}

static inline struct gpio_desc *gpio_to_desc(unsigned gpio)
{
	return NULL;
}

static inline int desc_to_gpio(const struct gpio_desc *desc)
{
	/* GPIO can never have been requested */
	WARN_ON(desc);
	return -EINVAL;
}

/* Child properties interface */
struct fwnode_handle;

static inline
struct gpio_desc *fwnode_get_named_gpiod(struct fwnode_handle *fwnode,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label)
{
	return ERR_PTR(-ENOSYS);
}

static inline
struct gpio_desc *fwnode_gpiod_get_index(struct fwnode_handle *fwnode,
					 const char *con_id, int index,
					 enum gpiod_flags flags,
					 const char *label)
{
	return ERR_PTR(-ENOSYS);
}

static inline
struct gpio_desc *devm_fwnode_gpiod_get_index(struct device *dev,
					      struct fwnode_handle *fwnode,
					      const char *con_id, int index,
					      enum gpiod_flags flags,
					      const char *label)
{
	return ERR_PTR(-ENOSYS);
}

#endif /* CONFIG_GPIOLIB */

static inline
struct gpio_desc *devm_fwnode_gpiod_get(struct device *dev,
					struct fwnode_handle *fwnode,
					const char *con_id,
					enum gpiod_flags flags,
					const char *label)
{
	return devm_fwnode_gpiod_get_index(dev, fwnode, con_id, 0,
					   flags, label);
}

static inline
struct gpio_desc *devm_fwnode_get_index_gpiod_from_child(struct device *dev,
						const char *con_id, int index,
						struct fwnode_handle *child,
						enum gpiod_flags flags,
						const char *label)
{
	return devm_fwnode_gpiod_get_index(dev, child, con_id, index,
					   flags, label);
}

static inline
struct gpio_desc *devm_fwnode_get_gpiod_from_child(struct device *dev,
						   const char *con_id,
						   struct fwnode_handle *child,
						   enum gpiod_flags flags,
						   const char *label)
{
	return devm_fwnode_gpiod_get_index(dev, child, con_id, 0, flags, label);
}

#if IS_ENABLED(CONFIG_GPIOLIB) && IS_ENABLED(CONFIG_OF_GPIO)
struct device_node;

struct gpio_desc *gpiod_get_from_of_node(struct device_node *node,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label);

#else  /* CONFIG_GPIOLIB && CONFIG_OF_GPIO */

struct device_node;

static inline
struct gpio_desc *gpiod_get_from_of_node(struct device_node *node,
					 const char *propname, int index,
					 enum gpiod_flags dflags,
					 const char *label)
{
	return ERR_PTR(-ENOSYS);
}

#endif /* CONFIG_GPIOLIB && CONFIG_OF_GPIO */

#ifdef CONFIG_GPIOLIB
struct device_node;

struct gpio_desc *devm_gpiod_get_from_of_node(struct device *dev,
					      struct device_node *node,
					      const char *propname, int index,
					      enum gpiod_flags dflags,
					      const char *label);

#else  /* CONFIG_GPIOLIB */

struct device_node;

static inline
struct gpio_desc *devm_gpiod_get_from_of_node(struct device *dev,
					      struct device_node *node,
					      const char *propname, int index,
					      enum gpiod_flags dflags,
					      const char *label)
{
	return ERR_PTR(-ENOSYS);
}

#endif /* CONFIG_GPIOLIB */

struct acpi_gpio_params {
	unsigned int crs_entry_index;
	unsigned int line_index;
	bool active_low;
};

struct acpi_gpio_mapping {
	const char *name;
	const struct acpi_gpio_params *data;
	unsigned int size;

/* Ignore IoRestriction field */
#define ACPI_GPIO_QUIRK_NO_IO_RESTRICTION	BIT(0)
/*
 * When ACPI GPIO mapping table is in use the index parameter inside it
 * refers to the GPIO resource in _CRS method. That index has no
 * distinction of actual type of the resource. When consumer wants to
 * get GpioIo type explicitly, this quirk may be used.
 */
#define ACPI_GPIO_QUIRK_ONLY_GPIOIO		BIT(1)

	unsigned int quirks;
};

#if IS_ENABLED(CONFIG_GPIOLIB) && IS_ENABLED(CONFIG_ACPI)

struct acpi_device;

int acpi_dev_add_driver_gpios(struct acpi_device *adev,
			      const struct acpi_gpio_mapping *gpios);
void acpi_dev_remove_driver_gpios(struct acpi_device *adev);

int devm_acpi_dev_add_driver_gpios(struct device *dev,
				   const struct acpi_gpio_mapping *gpios);
void devm_acpi_dev_remove_driver_gpios(struct device *dev);

#else  /* CONFIG_GPIOLIB && CONFIG_ACPI */

struct acpi_device;

static inline int acpi_dev_add_driver_gpios(struct acpi_device *adev,
			      const struct acpi_gpio_mapping *gpios)
{
	return -ENXIO;
}
static inline void acpi_dev_remove_driver_gpios(struct acpi_device *adev) {}

static inline int devm_acpi_dev_add_driver_gpios(struct device *dev,
			      const struct acpi_gpio_mapping *gpios)
{
	return -ENXIO;
}
static inline void devm_acpi_dev_remove_driver_gpios(struct device *dev) {}

#endif /* CONFIG_GPIOLIB && CONFIG_ACPI */


#if IS_ENABLED(CONFIG_GPIOLIB) && IS_ENABLED(CONFIG_GPIO_SYSFS)

int gpiod_export(struct gpio_desc *desc, bool direction_may_change);
int gpiod_export_link(struct device *dev, const char *name,
		      struct gpio_desc *desc);
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

static inline void gpiod_unexport(struct gpio_desc *desc)
{
}

#endif /* CONFIG_GPIOLIB && CONFIG_GPIO_SYSFS */

#endif
