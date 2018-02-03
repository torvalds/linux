/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RESET_H_
#define _LINUX_RESET_H_

#include <linux/types.h>

struct device;
struct device_node;
struct reset_control;

#ifdef CONFIG_RESET_CONTROLLER

int reset_control_reset(struct reset_control *rstc);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);
int reset_control_status(struct reset_control *rstc);

struct reset_control *__of_reset_control_get(struct device_node *node,
				     const char *id, int index, bool shared,
				     bool optional);
struct reset_control *__reset_control_get(struct device *dev, const char *id,
					  int index, bool shared,
					  bool optional);
void reset_control_put(struct reset_control *rstc);
int __device_reset(struct device *dev, bool optional);
struct reset_control *__devm_reset_control_get(struct device *dev,
				     const char *id, int index, bool shared,
				     bool optional);

struct reset_control *devm_reset_control_array_get(struct device *dev,
						   bool shared, bool optional);
struct reset_control *of_reset_control_array_get(struct device_node *np,
						 bool shared, bool optional);

#else

static inline int reset_control_reset(struct reset_control *rstc)
{
	return 0;
}

static inline int reset_control_assert(struct reset_control *rstc)
{
	return 0;
}

static inline int reset_control_deassert(struct reset_control *rstc)
{
	return 0;
}

static inline int reset_control_status(struct reset_control *rstc)
{
	return 0;
}

static inline void reset_control_put(struct reset_control *rstc)
{
}

static inline int __device_reset(struct device *dev, bool optional)
{
	return optional ? 0 : -ENOTSUPP;
}

static inline struct reset_control *__of_reset_control_get(
					struct device_node *node,
					const char *id, int index, bool shared,
					bool optional)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *__reset_control_get(
					struct device *dev, const char *id,
					int index, bool shared, bool optional)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *__devm_reset_control_get(
					struct device *dev, const char *id,
					int index, bool shared, bool optional)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *
devm_reset_control_array_get(struct device *dev, bool shared, bool optional)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *
of_reset_control_array_get(struct device_node *np, bool shared, bool optional)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

#endif /* CONFIG_RESET_CONTROLLER */

static inline int __must_check device_reset(struct device *dev)
{
	return __device_reset(dev, false);
}

static inline int device_reset_optional(struct device *dev)
{
	return __device_reset(dev, true);
}

/**
 * reset_control_get_exclusive - Lookup and obtain an exclusive reference
 *                               to a reset controller.
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
static inline struct reset_control *
__must_check reset_control_get_exclusive(struct device *dev, const char *id)
{
	return __reset_control_get(dev, id, 0, false, false);
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
	return __reset_control_get(dev, id, 0, true, false);
}

static inline struct reset_control *reset_control_get_optional_exclusive(
					struct device *dev, const char *id)
{
	return __reset_control_get(dev, id, 0, false, true);
}

static inline struct reset_control *reset_control_get_optional_shared(
					struct device *dev, const char *id)
{
	return __reset_control_get(dev, id, 0, true, true);
}

/**
 * of_reset_control_get_exclusive - Lookup and obtain an exclusive reference
 *                                  to a reset controller.
 * @node: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *of_reset_control_get_exclusive(
				struct device_node *node, const char *id)
{
	return __of_reset_control_get(node, id, 0, false, false);
}

/**
 * of_reset_control_get_shared - Lookup and obtain an shared reference
 *                               to a reset controller.
 * @node: device to be reset by the controller
 * @id: reset line name
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
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *of_reset_control_get_shared(
				struct device_node *node, const char *id)
{
	return __of_reset_control_get(node, id, 0, true, false);
}

/**
 * of_reset_control_get_exclusive_by_index - Lookup and obtain an exclusive
 *                                           reference to a reset controller
 *                                           by index.
 * @node: device to be reset by the controller
 * @index: index of the reset controller
 *
 * This is to be used to perform a list of resets for a device or power domain
 * in whatever order. Returns a struct reset_control or IS_ERR() condition
 * containing errno.
 */
static inline struct reset_control *of_reset_control_get_exclusive_by_index(
					struct device_node *node, int index)
{
	return __of_reset_control_get(node, NULL, index, false, false);
}

/**
 * of_reset_control_get_shared_by_index - Lookup and obtain an shared
 *                                        reference to a reset controller
 *                                        by index.
 * @node: device to be reset by the controller
 * @index: index of the reset controller
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
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * This is to be used to perform a list of resets for a device or power domain
 * in whatever order. Returns a struct reset_control or IS_ERR() condition
 * containing errno.
 */
static inline struct reset_control *of_reset_control_get_shared_by_index(
					struct device_node *node, int index)
{
	return __of_reset_control_get(node, NULL, index, true, false);
}

/**
 * devm_reset_control_get_exclusive - resource managed
 *                                    reset_control_get_exclusive()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_exclusive(). For reset controllers returned
 * from this function, reset_control_put() is called automatically on driver
 * detach.
 *
 * See reset_control_get_exclusive() for more information.
 */
static inline struct reset_control *
__must_check devm_reset_control_get_exclusive(struct device *dev,
					      const char *id)
{
	return __devm_reset_control_get(dev, id, 0, false, false);
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
	return __devm_reset_control_get(dev, id, 0, true, false);
}

static inline struct reset_control *devm_reset_control_get_optional_exclusive(
					struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, false, true);
}

static inline struct reset_control *devm_reset_control_get_optional_shared(
					struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, true, true);
}

/**
 * devm_reset_control_get_exclusive_by_index - resource managed
 *                                             reset_control_get_exclusive()
 * @dev: device to be reset by the controller
 * @index: index of the reset controller
 *
 * Managed reset_control_get_exclusive(). For reset controllers returned from
 * this function, reset_control_put() is called automatically on driver
 * detach.
 *
 * See reset_control_get_exclusive() for more information.
 */
static inline struct reset_control *
devm_reset_control_get_exclusive_by_index(struct device *dev, int index)
{
	return __devm_reset_control_get(dev, NULL, index, false, false);
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
static inline struct reset_control *
devm_reset_control_get_shared_by_index(struct device *dev, int index)
{
	return __devm_reset_control_get(dev, NULL, index, true, false);
}

/*
 * TEMPORARY calls to use during transition:
 *
 *   of_reset_control_get() => of_reset_control_get_exclusive()
 *
 * These inline function calls will be removed once all consumers
 * have been moved over to the new explicit API.
 */
static inline struct reset_control *of_reset_control_get(
				struct device_node *node, const char *id)
{
	return of_reset_control_get_exclusive(node, id);
}

static inline struct reset_control *of_reset_control_get_by_index(
				struct device_node *node, int index)
{
	return of_reset_control_get_exclusive_by_index(node, index);
}

static inline struct reset_control *devm_reset_control_get(
				struct device *dev, const char *id)
{
	return devm_reset_control_get_exclusive(dev, id);
}

static inline struct reset_control *devm_reset_control_get_optional(
				struct device *dev, const char *id)
{
	return devm_reset_control_get_optional_exclusive(dev, id);

}

static inline struct reset_control *devm_reset_control_get_by_index(
				struct device *dev, int index)
{
	return devm_reset_control_get_exclusive_by_index(dev, index);
}

/*
 * APIs to manage a list of reset controllers
 */
static inline struct reset_control *
devm_reset_control_array_get_exclusive(struct device *dev)
{
	return devm_reset_control_array_get(dev, false, false);
}

static inline struct reset_control *
devm_reset_control_array_get_shared(struct device *dev)
{
	return devm_reset_control_array_get(dev, true, false);
}

static inline struct reset_control *
devm_reset_control_array_get_optional_exclusive(struct device *dev)
{
	return devm_reset_control_array_get(dev, false, true);
}

static inline struct reset_control *
devm_reset_control_array_get_optional_shared(struct device *dev)
{
	return devm_reset_control_array_get(dev, true, true);
}

static inline struct reset_control *
of_reset_control_array_get_exclusive(struct device_node *node)
{
	return of_reset_control_array_get(node, false, false);
}

static inline struct reset_control *
of_reset_control_array_get_shared(struct device_node *node)
{
	return of_reset_control_array_get(node, true, false);
}

static inline struct reset_control *
of_reset_control_array_get_optional_exclusive(struct device_node *node)
{
	return of_reset_control_array_get(node, false, true);
}

static inline struct reset_control *
of_reset_control_array_get_optional_shared(struct device_node *node)
{
	return of_reset_control_array_get(node, true, true);
}
#endif
