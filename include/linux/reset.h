#ifndef _LINUX_RESET_H_
#define _LINUX_RESET_H_

#include <linux/device.h>

struct reset_control;

#ifdef CONFIG_RESET_CONTROLLER

int reset_control_reset(struct reset_control *rstc);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);
int reset_control_status(struct reset_control *rstc);

struct reset_control *__of_reset_control_get(struct device_node *node,
				     const char *id, int index, int shared);
void reset_control_put(struct reset_control *rstc);
struct reset_control *__devm_reset_control_get(struct device *dev,
				     const char *id, int index, int shared);

int __must_check device_reset(struct device *dev);

static inline int device_reset_optional(struct device *dev)
{
	return device_reset(dev);
}

#else

static inline int reset_control_reset(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline int reset_control_assert(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline int reset_control_deassert(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline int reset_control_status(struct reset_control *rstc)
{
	WARN_ON(1);
	return 0;
}

static inline void reset_control_put(struct reset_control *rstc)
{
	WARN_ON(1);
}

static inline int __must_check device_reset(struct device *dev)
{
	WARN_ON(1);
	return -ENOTSUPP;
}

static inline int device_reset_optional(struct device *dev)
{
	return -ENOTSUPP;
}

static inline struct reset_control *__of_reset_control_get(
					struct device_node *node,
					const char *id, int index, int shared)
{
	return ERR_PTR(-EINVAL);
}

static inline struct reset_control *__devm_reset_control_get(
					struct device *dev,
					const char *id, int index, int shared)
{
	return ERR_PTR(-EINVAL);
}

#endif /* CONFIG_RESET_CONTROLLER */

/**
 * reset_control_get - Lookup and obtain an exclusive reference to a
 *                     reset controller.
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 * If this function is called more then once for the same reset_control it will
 * return -EBUSY.
 *
 * See reset_control_get_shared for details on shared references to
 * reset-controls.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *__must_check reset_control_get(
					struct device *dev, const char *id)
{
#ifndef CONFIG_RESET_CONTROLLER
	WARN_ON(1);
#endif
	return __of_reset_control_get(dev ? dev->of_node : NULL, id, 0, 0);
}

static inline struct reset_control *reset_control_get_optional(
					struct device *dev, const char *id)
{
	return __of_reset_control_get(dev ? dev->of_node : NULL, id, 0, 0);
}

/**
 * reset_control_get_shared - Lookup and obtain a shared reference to a
 *                            reset controller.
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 * This function is intended for use with reset-controls which are shared
 * between hardware-blocks.
 *
 * When a reset-control is shared, the behavior of reset_control_assert /
 * deassert is changed, the reset-core will keep track of a deassert_count
 * and only (re-)assert the reset after reset_control_assert has been called
 * as many times as reset_control_deassert was called. Also see the remark
 * about shared reset-controls in the reset_control_assert docs.
 *
 * Calling reset_control_assert without first calling reset_control_deassert
 * is not allowed on a shared reset control. Calling reset_control_reset is
 * also not allowed on a shared reset control.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *reset_control_get_shared(
					struct device *dev, const char *id)
{
	return __of_reset_control_get(dev ? dev->of_node : NULL, id, 0, 1);
}

/**
 * of_reset_control_get - Lookup and obtain an exclusive reference to a
 *                        reset controller.
 * @node: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *of_reset_control_get(
				struct device_node *node, const char *id)
{
	return __of_reset_control_get(node, id, 0, 0);
}

/**
 * of_reset_control_get_by_index - Lookup and obtain an exclusive reference to
 *                                 a reset controller by index.
 * @node: device to be reset by the controller
 * @index: index of the reset controller
 *
 * This is to be used to perform a list of resets for a device or power domain
 * in whatever order. Returns a struct reset_control or IS_ERR() condition
 * containing errno.
 */
static inline struct reset_control *of_reset_control_get_by_index(
					struct device_node *node, int index)
{
	return __of_reset_control_get(node, NULL, index, 0);
}

/**
 * devm_reset_control_get - resource managed reset_control_get()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get(). For reset controllers returned from this
 * function, reset_control_put() is called automatically on driver detach.
 * See reset_control_get() for more information.
 */
static inline struct reset_control *__must_check devm_reset_control_get(
					struct device *dev, const char *id)
{
#ifndef CONFIG_RESET_CONTROLLER
	WARN_ON(1);
#endif
	return __devm_reset_control_get(dev, id, 0, 0);
}

static inline struct reset_control *devm_reset_control_get_optional(
					struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, 0);
}

/**
 * devm_reset_control_get_by_index - resource managed reset_control_get
 * @dev: device to be reset by the controller
 * @index: index of the reset controller
 *
 * Managed reset_control_get(). For reset controllers returned from this
 * function, reset_control_put() is called automatically on driver detach.
 * See reset_control_get() for more information.
 */
static inline struct reset_control *devm_reset_control_get_by_index(
					struct device *dev, int index)
{
	return __devm_reset_control_get(dev, NULL, index, 0);
}

/**
 * devm_reset_control_get_shared - resource managed reset_control_get_shared()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_shared(). For reset controllers returned from
 * this function, reset_control_put() is called automatically on driver detach.
 * See reset_control_get_shared() for more information.
 */
static inline struct reset_control *devm_reset_control_get_shared(
					struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, 1);
}

/**
 * devm_reset_control_get_shared_by_index - resource managed
 * reset_control_get_shared
 * @dev: device to be reset by the controller
 * @index: index of the reset controller
 *
 * Managed reset_control_get_shared(). For reset controllers returned from
 * this function, reset_control_put() is called automatically on driver detach.
 * See reset_control_get_shared() for more information.
 */
static inline struct reset_control *devm_reset_control_get_shared_by_index(
					struct device *dev, int index)
{
	return __devm_reset_control_get(dev, NULL, index, 1);
}

#endif
