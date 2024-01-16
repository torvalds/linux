/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RESET_H_
#define _LINUX_RESET_H_

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>

struct device;
struct device_node;
struct reset_control;

/**
 * struct reset_control_bulk_data - Data used for bulk reset control operations.
 *
 * @id: reset control consumer ID
 * @rstc: struct reset_control * to store the associated reset control
 *
 * The reset APIs provide a series of reset_control_bulk_*() API calls as
 * a convenience to consumers which require multiple reset controls.
 * This structure is used to manage data for these calls.
 */
struct reset_control_bulk_data {
	const char			*id;
	struct reset_control		*rstc;
};

#ifdef CONFIG_RESET_CONTROLLER

int reset_control_reset(struct reset_control *rstc);
int reset_control_rearm(struct reset_control *rstc);
int reset_control_assert(struct reset_control *rstc);
int reset_control_deassert(struct reset_control *rstc);
int reset_control_status(struct reset_control *rstc);
int reset_control_acquire(struct reset_control *rstc);
void reset_control_release(struct reset_control *rstc);

int reset_control_bulk_reset(int num_rstcs, struct reset_control_bulk_data *rstcs);
int reset_control_bulk_assert(int num_rstcs, struct reset_control_bulk_data *rstcs);
int reset_control_bulk_deassert(int num_rstcs, struct reset_control_bulk_data *rstcs);
int reset_control_bulk_acquire(int num_rstcs, struct reset_control_bulk_data *rstcs);
void reset_control_bulk_release(int num_rstcs, struct reset_control_bulk_data *rstcs);

struct reset_control *__of_reset_control_get(struct device_node *node,
				     const char *id, int index, bool shared,
				     bool optional, bool acquired);
struct reset_control *__reset_control_get(struct device *dev, const char *id,
					  int index, bool shared,
					  bool optional, bool acquired);
void reset_control_put(struct reset_control *rstc);
int __reset_control_bulk_get(struct device *dev, int num_rstcs,
			     struct reset_control_bulk_data *rstcs,
			     bool shared, bool optional, bool acquired);
void reset_control_bulk_put(int num_rstcs, struct reset_control_bulk_data *rstcs);

int __device_reset(struct device *dev, bool optional);
struct reset_control *__devm_reset_control_get(struct device *dev,
				     const char *id, int index, bool shared,
				     bool optional, bool acquired);
int __devm_reset_control_bulk_get(struct device *dev, int num_rstcs,
				  struct reset_control_bulk_data *rstcs,
				  bool shared, bool optional, bool acquired);

struct reset_control *devm_reset_control_array_get(struct device *dev,
						   bool shared, bool optional);
struct reset_control *of_reset_control_array_get(struct device_node *np,
						 bool shared, bool optional,
						 bool acquired);

int reset_control_get_count(struct device *dev);

#else

static inline int reset_control_reset(struct reset_control *rstc)
{
	return 0;
}

static inline int reset_control_rearm(struct reset_control *rstc)
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

static inline int reset_control_acquire(struct reset_control *rstc)
{
	return 0;
}

static inline void reset_control_release(struct reset_control *rstc)
{
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
					bool optional, bool acquired)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *__reset_control_get(
					struct device *dev, const char *id,
					int index, bool shared, bool optional,
					bool acquired)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline int
reset_control_bulk_reset(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
	return 0;
}

static inline int
reset_control_bulk_assert(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
	return 0;
}

static inline int
reset_control_bulk_deassert(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
	return 0;
}

static inline int
reset_control_bulk_acquire(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
	return 0;
}

static inline void
reset_control_bulk_release(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
}

static inline int
__reset_control_bulk_get(struct device *dev, int num_rstcs,
			 struct reset_control_bulk_data *rstcs,
			 bool shared, bool optional, bool acquired)
{
	return optional ? 0 : -EOPNOTSUPP;
}

static inline void
reset_control_bulk_put(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
}

static inline struct reset_control *__devm_reset_control_get(
					struct device *dev, const char *id,
					int index, bool shared, bool optional,
					bool acquired)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline int
__devm_reset_control_bulk_get(struct device *dev, int num_rstcs,
			      struct reset_control_bulk_data *rstcs,
			      bool shared, bool optional, bool acquired)
{
	return optional ? 0 : -EOPNOTSUPP;
}

static inline struct reset_control *
devm_reset_control_array_get(struct device *dev, bool shared, bool optional)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *
of_reset_control_array_get(struct device_node *np, bool shared, bool optional,
			   bool acquired)
{
	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline int reset_control_get_count(struct device *dev)
{
	return -ENOENT;
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
 * If this function is called more than once for the same reset_control it will
 * return -EBUSY.
 *
 * See reset_control_get_shared() for details on shared references to
 * reset-controls.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *
__must_check reset_control_get_exclusive(struct device *dev, const char *id)
{
	return __reset_control_get(dev, id, 0, false, false, true);
}

/**
 * reset_control_bulk_get_exclusive - Lookup and obtain exclusive references to
 *                                    multiple reset controllers.
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Fills the rstcs array with pointers to exclusive reset controls and
 * returns 0, or an IS_ERR() condition containing errno.
 */
static inline int __must_check
reset_control_bulk_get_exclusive(struct device *dev, int num_rstcs,
				 struct reset_control_bulk_data *rstcs)
{
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, false, false, true);
}

/**
 * reset_control_get_exclusive_released - Lookup and obtain a temoprarily
 *                                        exclusive reference to a reset
 *                                        controller.
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 * reset-controls returned by this function must be acquired via
 * reset_control_acquire() before they can be used and should be released
 * via reset_control_release() afterwards.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *
__must_check reset_control_get_exclusive_released(struct device *dev,
						  const char *id)
{
	return __reset_control_get(dev, id, 0, false, false, false);
}

/**
 * reset_control_bulk_get_exclusive_released - Lookup and obtain temporarily
 *                                    exclusive references to multiple reset
 *                                    controllers.
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Fills the rstcs array with pointers to exclusive reset controls and
 * returns 0, or an IS_ERR() condition containing errno.
 * reset-controls returned by this function must be acquired via
 * reset_control_bulk_acquire() before they can be used and should be released
 * via reset_control_bulk_release() afterwards.
 */
static inline int __must_check
reset_control_bulk_get_exclusive_released(struct device *dev, int num_rstcs,
					  struct reset_control_bulk_data *rstcs)
{
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, false, false, false);
}

/**
 * reset_control_bulk_get_optional_exclusive_released - Lookup and obtain optional
 *                                    temporarily exclusive references to multiple
 *                                    reset controllers.
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Optional variant of reset_control_bulk_get_exclusive_released(). If the
 * requested reset is not specified in the device tree, this function returns 0
 * instead of an error and missing rtsc is set to NULL.
 *
 * See reset_control_bulk_get_exclusive_released() for more information.
 */
static inline int __must_check
reset_control_bulk_get_optional_exclusive_released(struct device *dev, int num_rstcs,
						   struct reset_control_bulk_data *rstcs)
{
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, false, true, false);
}

/**
 * reset_control_get_shared - Lookup and obtain a shared reference to a
 *                            reset controller.
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 * This function is intended for use with reset-controls which are shared
 * between hardware blocks.
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
	return __reset_control_get(dev, id, 0, true, false, false);
}

/**
 * reset_control_bulk_get_shared - Lookup and obtain shared references to
 *                                 multiple reset controllers.
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Fills the rstcs array with pointers to shared reset controls and
 * returns 0, or an IS_ERR() condition containing errno.
 */
static inline int __must_check
reset_control_bulk_get_shared(struct device *dev, int num_rstcs,
			      struct reset_control_bulk_data *rstcs)
{
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, true, false, false);
}

/**
 * reset_control_get_optional_exclusive - optional reset_control_get_exclusive()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Optional variant of reset_control_get_exclusive(). If the requested reset
 * is not specified in the device tree, this function returns NULL instead of
 * an error.
 *
 * See reset_control_get_exclusive() for more information.
 */
static inline struct reset_control *reset_control_get_optional_exclusive(
					struct device *dev, const char *id)
{
	return __reset_control_get(dev, id, 0, false, true, true);
}

/**
 * reset_control_bulk_get_optional_exclusive - optional
 *                                             reset_control_bulk_get_exclusive()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Optional variant of reset_control_bulk_get_exclusive(). If any of the
 * requested resets are not specified in the device tree, this function sets
 * them to NULL instead of returning an error.
 *
 * See reset_control_bulk_get_exclusive() for more information.
 */
static inline int __must_check
reset_control_bulk_get_optional_exclusive(struct device *dev, int num_rstcs,
					  struct reset_control_bulk_data *rstcs)
{
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, false, true, true);
}

/**
 * reset_control_get_optional_shared - optional reset_control_get_shared()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Optional variant of reset_control_get_shared(). If the requested reset
 * is not specified in the device tree, this function returns NULL instead of
 * an error.
 *
 * See reset_control_get_shared() for more information.
 */
static inline struct reset_control *reset_control_get_optional_shared(
					struct device *dev, const char *id)
{
	return __reset_control_get(dev, id, 0, true, true, false);
}

/**
 * reset_control_bulk_get_optional_shared - optional
 *                                             reset_control_bulk_get_shared()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Optional variant of reset_control_bulk_get_shared(). If the requested resets
 * are not specified in the device tree, this function sets them to NULL
 * instead of returning an error.
 *
 * See reset_control_bulk_get_shared() for more information.
 */
static inline int __must_check
reset_control_bulk_get_optional_shared(struct device *dev, int num_rstcs,
				       struct reset_control_bulk_data *rstcs)
{
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, true, true, false);
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
	return __of_reset_control_get(node, id, 0, false, false, true);
}

/**
 * of_reset_control_get_optional_exclusive - Lookup and obtain an optional exclusive
 *                                           reference to a reset controller.
 * @node: device to be reset by the controller
 * @id: reset line name
 *
 * Optional variant of of_reset_control_get_exclusive(). If the requested reset
 * is not specified in the device tree, this function returns NULL instead of
 * an error.
 *
 * Returns a struct reset_control or IS_ERR() condition containing errno.
 *
 * Use of id names is optional.
 */
static inline struct reset_control *of_reset_control_get_optional_exclusive(
				struct device_node *node, const char *id)
{
	return __of_reset_control_get(node, id, 0, false, true, true);
}

/**
 * of_reset_control_get_shared - Lookup and obtain a shared reference
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
	return __of_reset_control_get(node, id, 0, true, false, false);
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
	return __of_reset_control_get(node, NULL, index, false, false, true);
}

/**
 * of_reset_control_get_shared_by_index - Lookup and obtain a shared
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
	return __of_reset_control_get(node, NULL, index, true, false, false);
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
	return __devm_reset_control_get(dev, id, 0, false, false, true);
}

/**
 * devm_reset_control_bulk_get_exclusive - resource managed
 *                                         reset_control_bulk_get_exclusive()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_get_exclusive(). For reset controllers returned
 * from this function, reset_control_put() is called automatically on driver
 * detach.
 *
 * See reset_control_bulk_get_exclusive() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_exclusive(struct device *dev, int num_rstcs,
				      struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, false, false, true);
}

/**
 * devm_reset_control_get_exclusive_released - resource managed
 *                                             reset_control_get_exclusive_released()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_exclusive_released(). For reset controllers
 * returned from this function, reset_control_put() is called automatically on
 * driver detach.
 *
 * See reset_control_get_exclusive_released() for more information.
 */
static inline struct reset_control *
__must_check devm_reset_control_get_exclusive_released(struct device *dev,
						       const char *id)
{
	return __devm_reset_control_get(dev, id, 0, false, false, false);
}

/**
 * devm_reset_control_bulk_get_exclusive_released - resource managed
 *                                                  reset_control_bulk_get_exclusive_released()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_get_exclusive_released(). For reset controllers
 * returned from this function, reset_control_put() is called automatically on
 * driver detach.
 *
 * See reset_control_bulk_get_exclusive_released() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_exclusive_released(struct device *dev, int num_rstcs,
					       struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, false, false, false);
}

/**
 * devm_reset_control_get_optional_exclusive_released - resource managed
 *                                                      reset_control_get_optional_exclusive_released()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed-and-optional variant of reset_control_get_exclusive_released(). For
 * reset controllers returned from this function, reset_control_put() is called
 * automatically on driver detach.
 *
 * See reset_control_get_exclusive_released() for more information.
 */
static inline struct reset_control *
__must_check devm_reset_control_get_optional_exclusive_released(struct device *dev,
								const char *id)
{
	return __devm_reset_control_get(dev, id, 0, false, true, false);
}

/**
 * devm_reset_control_bulk_get_optional_exclusive_released - resource managed
 *                                                           reset_control_bulk_optional_get_exclusive_released()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_optional_get_exclusive_released(). For reset
 * controllers returned from this function, reset_control_put() is called
 * automatically on driver detach.
 *
 * See reset_control_bulk_optional_get_exclusive_released() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_optional_exclusive_released(struct device *dev, int num_rstcs,
							struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, false, true, false);
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
	return __devm_reset_control_get(dev, id, 0, true, false, false);
}

/**
 * devm_reset_control_bulk_get_shared - resource managed
 *                                      reset_control_bulk_get_shared()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_get_shared(). For reset controllers returned
 * from this function, reset_control_put() is called automatically on driver
 * detach.
 *
 * See reset_control_bulk_get_shared() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_shared(struct device *dev, int num_rstcs,
				   struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, true, false, false);
}

/**
 * devm_reset_control_get_optional_exclusive - resource managed
 *                                             reset_control_get_optional_exclusive()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_optional_exclusive(). For reset controllers
 * returned from this function, reset_control_put() is called automatically on
 * driver detach.
 *
 * See reset_control_get_optional_exclusive() for more information.
 */
static inline struct reset_control *devm_reset_control_get_optional_exclusive(
					struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, false, true, true);
}

/**
 * devm_reset_control_bulk_get_optional_exclusive - resource managed
 *                                                  reset_control_bulk_get_optional_exclusive()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_get_optional_exclusive(). For reset controllers
 * returned from this function, reset_control_put() is called automatically on
 * driver detach.
 *
 * See reset_control_bulk_get_optional_exclusive() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_optional_exclusive(struct device *dev, int num_rstcs,
					       struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, false, true, true);
}

/**
 * devm_reset_control_get_optional_shared - resource managed
 *                                          reset_control_get_optional_shared()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_optional_shared(). For reset controllers returned
 * from this function, reset_control_put() is called automatically on driver
 * detach.
 *
 * See reset_control_get_optional_shared() for more information.
 */
static inline struct reset_control *devm_reset_control_get_optional_shared(
					struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, true, true, false);
}

/**
 * devm_reset_control_bulk_get_optional_shared - resource managed
 *                                               reset_control_bulk_get_optional_shared()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_get_optional_shared(). For reset controllers
 * returned from this function, reset_control_put() is called automatically on
 * driver detach.
 *
 * See reset_control_bulk_get_optional_shared() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_optional_shared(struct device *dev, int num_rstcs,
					    struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, true, true, false);
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
	return __devm_reset_control_get(dev, NULL, index, false, false, true);
}

/**
 * devm_reset_control_get_shared_by_index - resource managed
 *                                          reset_control_get_shared
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
	return __devm_reset_control_get(dev, NULL, index, true, false, false);
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
	return of_reset_control_array_get(node, false, false, true);
}

static inline struct reset_control *
of_reset_control_array_get_exclusive_released(struct device_node *node)
{
	return of_reset_control_array_get(node, false, false, false);
}

static inline struct reset_control *
of_reset_control_array_get_shared(struct device_node *node)
{
	return of_reset_control_array_get(node, true, false, true);
}

static inline struct reset_control *
of_reset_control_array_get_optional_exclusive(struct device_node *node)
{
	return of_reset_control_array_get(node, false, true, true);
}

static inline struct reset_control *
of_reset_control_array_get_optional_shared(struct device_node *node)
{
	return of_reset_control_array_get(node, true, true, true);
}
#endif
