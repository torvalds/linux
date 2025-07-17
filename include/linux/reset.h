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

#define RESET_CONTROL_FLAGS_BIT_SHARED		BIT(0)	/* not exclusive */
#define RESET_CONTROL_FLAGS_BIT_OPTIONAL	BIT(1)
#define RESET_CONTROL_FLAGS_BIT_ACQUIRED	BIT(2)	/* iff exclusive, not released */
#define RESET_CONTROL_FLAGS_BIT_DEASSERTED	BIT(3)

/**
 * enum reset_control_flags - Flags that can be passed to the reset_control_get functions
 *                    to determine the type of reset control.
 *                    These values cannot be OR'd.
 *
 * @RESET_CONTROL_EXCLUSIVE:				exclusive, acquired,
 * @RESET_CONTROL_EXCLUSIVE_DEASSERTED:			exclusive, acquired, deasserted
 * @RESET_CONTROL_EXCLUSIVE_RELEASED:			exclusive, released,
 * @RESET_CONTROL_SHARED:				shared
 * @RESET_CONTROL_SHARED_DEASSERTED:			shared, deasserted
 * @RESET_CONTROL_OPTIONAL_EXCLUSIVE:			optional, exclusive, acquired
 * @RESET_CONTROL_OPTIONAL_EXCLUSIVE_DEASSERTED:	optional, exclusive, acquired, deasserted
 * @RESET_CONTROL_OPTIONAL_EXCLUSIVE_RELEASED:		optional, exclusive, released
 * @RESET_CONTROL_OPTIONAL_SHARED:			optional, shared
 * @RESET_CONTROL_OPTIONAL_SHARED_DEASSERTED:		optional, shared, deasserted
 */
enum reset_control_flags {
	RESET_CONTROL_EXCLUSIVE				= RESET_CONTROL_FLAGS_BIT_ACQUIRED,
	RESET_CONTROL_EXCLUSIVE_DEASSERTED		= RESET_CONTROL_FLAGS_BIT_ACQUIRED |
							  RESET_CONTROL_FLAGS_BIT_DEASSERTED,
	RESET_CONTROL_EXCLUSIVE_RELEASED		= 0,
	RESET_CONTROL_SHARED				= RESET_CONTROL_FLAGS_BIT_SHARED,
	RESET_CONTROL_SHARED_DEASSERTED			= RESET_CONTROL_FLAGS_BIT_SHARED |
							  RESET_CONTROL_FLAGS_BIT_DEASSERTED,
	RESET_CONTROL_OPTIONAL_EXCLUSIVE		= RESET_CONTROL_FLAGS_BIT_OPTIONAL |
							  RESET_CONTROL_FLAGS_BIT_ACQUIRED,
	RESET_CONTROL_OPTIONAL_EXCLUSIVE_DEASSERTED	= RESET_CONTROL_FLAGS_BIT_OPTIONAL |
							  RESET_CONTROL_FLAGS_BIT_ACQUIRED |
							  RESET_CONTROL_FLAGS_BIT_DEASSERTED,
	RESET_CONTROL_OPTIONAL_EXCLUSIVE_RELEASED	= RESET_CONTROL_FLAGS_BIT_OPTIONAL,
	RESET_CONTROL_OPTIONAL_SHARED			= RESET_CONTROL_FLAGS_BIT_OPTIONAL |
							  RESET_CONTROL_FLAGS_BIT_SHARED,
	RESET_CONTROL_OPTIONAL_SHARED_DEASSERTED	= RESET_CONTROL_FLAGS_BIT_OPTIONAL |
							  RESET_CONTROL_FLAGS_BIT_SHARED |
							  RESET_CONTROL_FLAGS_BIT_DEASSERTED,
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
				     const char *id, int index, enum reset_control_flags flags);
struct reset_control *__reset_control_get(struct device *dev, const char *id,
					  int index, enum reset_control_flags flags);
void reset_control_put(struct reset_control *rstc);
int __reset_control_bulk_get(struct device *dev, int num_rstcs,
			     struct reset_control_bulk_data *rstcs,
			     enum reset_control_flags flags);
void reset_control_bulk_put(int num_rstcs, struct reset_control_bulk_data *rstcs);

int __device_reset(struct device *dev, bool optional);
struct reset_control *__devm_reset_control_get(struct device *dev,
				     const char *id, int index, enum reset_control_flags flags);
int __devm_reset_control_bulk_get(struct device *dev, int num_rstcs,
				  struct reset_control_bulk_data *rstcs,
				  enum reset_control_flags flags);

struct reset_control *devm_reset_control_array_get(struct device *dev,
						   enum reset_control_flags flags);
struct reset_control *of_reset_control_array_get(struct device_node *np, enum reset_control_flags);

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
					const char *id, int index, enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *__reset_control_get(
					struct device *dev, const char *id,
					int index, enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

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
			 enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

	return optional ? 0 : -EOPNOTSUPP;
}

static inline void
reset_control_bulk_put(int num_rstcs, struct reset_control_bulk_data *rstcs)
{
}

static inline struct reset_control *__devm_reset_control_get(
					struct device *dev, const char *id,
					int index, enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline int
__devm_reset_control_bulk_get(struct device *dev, int num_rstcs,
			      struct reset_control_bulk_data *rstcs,
			      enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

	return optional ? 0 : -EOPNOTSUPP;
}

static inline struct reset_control *
devm_reset_control_array_get(struct device *dev, enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

	return optional ? NULL : ERR_PTR(-ENOTSUPP);
}

static inline struct reset_control *
of_reset_control_array_get(struct device_node *np, enum reset_control_flags flags)
{
	bool optional = flags & RESET_CONTROL_FLAGS_BIT_OPTIONAL;

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
	return __reset_control_get(dev, id, 0, RESET_CONTROL_EXCLUSIVE);
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
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_EXCLUSIVE);
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
	return __reset_control_get(dev, id, 0, RESET_CONTROL_EXCLUSIVE_RELEASED);
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
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_EXCLUSIVE_RELEASED);
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
	return __reset_control_bulk_get(dev, num_rstcs, rstcs,
					RESET_CONTROL_OPTIONAL_EXCLUSIVE_RELEASED);
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
	return __reset_control_get(dev, id, 0, RESET_CONTROL_SHARED);
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
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_SHARED);
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
	return __reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_EXCLUSIVE);
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
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_OPTIONAL_EXCLUSIVE);
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
	return __reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_SHARED);
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
	return __reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_OPTIONAL_SHARED);
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
	return __of_reset_control_get(node, id, 0, RESET_CONTROL_EXCLUSIVE);
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
	return __of_reset_control_get(node, id, 0, RESET_CONTROL_OPTIONAL_EXCLUSIVE);
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
	return __of_reset_control_get(node, id, 0, RESET_CONTROL_SHARED);
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
	return __of_reset_control_get(node, NULL, index, RESET_CONTROL_EXCLUSIVE);
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
	return __of_reset_control_get(node, NULL, index, RESET_CONTROL_SHARED);
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
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_EXCLUSIVE);
}

/**
 * devm_reset_control_get_exclusive_deasserted - resource managed
 *                                    reset_control_get_exclusive() +
 *                                    reset_control_deassert()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_exclusive() + reset_control_deassert(). For reset
 * controllers returned from this function, reset_control_assert() +
 * reset_control_put() is called automatically on driver detach.
 *
 * See reset_control_get_exclusive() for more information.
 */
static inline struct reset_control * __must_check
devm_reset_control_get_exclusive_deasserted(struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_EXCLUSIVE_DEASSERTED);
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
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs,
					     RESET_CONTROL_EXCLUSIVE);
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
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_EXCLUSIVE_RELEASED);
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
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs,
					     RESET_CONTROL_EXCLUSIVE_RELEASED);
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
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_EXCLUSIVE_RELEASED);
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
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs,
					     RESET_CONTROL_OPTIONAL_EXCLUSIVE_RELEASED);
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
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_SHARED);
}

/**
 * devm_reset_control_get_shared_deasserted - resource managed
 *                                            reset_control_get_shared() +
 *                                            reset_control_deassert()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_shared() + reset_control_deassert(). For reset
 * controllers returned from this function, reset_control_assert() +
 * reset_control_put() is called automatically on driver detach.
 *
 * See devm_reset_control_get_shared() for more information.
 */
static inline struct reset_control * __must_check
devm_reset_control_get_shared_deasserted(struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_SHARED_DEASSERTED);
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
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_SHARED);
}

/**
 * devm_reset_control_bulk_get_shared_deasserted - resource managed
 *                                                 reset_control_bulk_get_shared() +
 *                                                 reset_control_bulk_deassert()
 * @dev: device to be reset by the controller
 * @num_rstcs: number of entries in rstcs array
 * @rstcs: array of struct reset_control_bulk_data with reset line names set
 *
 * Managed reset_control_bulk_get_shared() + reset_control_bulk_deassert(). For
 * reset controllers returned from this function, reset_control_bulk_assert() +
 * reset_control_bulk_put() are called automatically on driver detach.
 *
 * See devm_reset_control_bulk_get_shared() for more information.
 */
static inline int __must_check
devm_reset_control_bulk_get_shared_deasserted(struct device *dev, int num_rstcs,
					      struct reset_control_bulk_data *rstcs)
{
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs,
					     RESET_CONTROL_SHARED_DEASSERTED);
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
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_EXCLUSIVE);
}

/**
 * devm_reset_control_get_optional_exclusive_deasserted - resource managed
 *                                                        reset_control_get_optional_exclusive() +
 *                                                        reset_control_deassert()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_optional_exclusive() + reset_control_deassert().
 * For reset controllers returned from this function, reset_control_assert() +
 * reset_control_put() is called automatically on driver detach.
 *
 * See devm_reset_control_get_optional_exclusive() for more information.
 */
static inline struct reset_control *
devm_reset_control_get_optional_exclusive_deasserted(struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_EXCLUSIVE_DEASSERTED);
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
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs,
					     RESET_CONTROL_OPTIONAL_EXCLUSIVE);
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
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_SHARED);
}

/**
 * devm_reset_control_get_optional_shared_deasserted - resource managed
 *                                                     reset_control_get_optional_shared() +
 *                                                     reset_control_deassert()
 * @dev: device to be reset by the controller
 * @id: reset line name
 *
 * Managed reset_control_get_optional_shared() + reset_control_deassert(). For
 * reset controllers returned from this function, reset_control_assert() +
 * reset_control_put() is called automatically on driver detach.
 *
 * See devm_reset_control_get_optional_shared() for more information.
 */
static inline struct reset_control *
devm_reset_control_get_optional_shared_deasserted(struct device *dev, const char *id)
{
	return __devm_reset_control_get(dev, id, 0, RESET_CONTROL_OPTIONAL_SHARED_DEASSERTED);
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
	return __devm_reset_control_bulk_get(dev, num_rstcs, rstcs, RESET_CONTROL_OPTIONAL_SHARED);
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
	return __devm_reset_control_get(dev, NULL, index, RESET_CONTROL_EXCLUSIVE);
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
	return __devm_reset_control_get(dev, NULL, index, RESET_CONTROL_SHARED);
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
	return devm_reset_control_array_get(dev, RESET_CONTROL_EXCLUSIVE);
}

static inline struct reset_control *
devm_reset_control_array_get_exclusive_released(struct device *dev)
{
	return devm_reset_control_array_get(dev, RESET_CONTROL_EXCLUSIVE_RELEASED);
}

static inline struct reset_control *
devm_reset_control_array_get_shared(struct device *dev)
{
	return devm_reset_control_array_get(dev, RESET_CONTROL_SHARED);
}

static inline struct reset_control *
devm_reset_control_array_get_optional_exclusive(struct device *dev)
{
	return devm_reset_control_array_get(dev, RESET_CONTROL_OPTIONAL_EXCLUSIVE);
}

static inline struct reset_control *
devm_reset_control_array_get_optional_shared(struct device *dev)
{
	return devm_reset_control_array_get(dev, RESET_CONTROL_OPTIONAL_SHARED);
}

static inline struct reset_control *
of_reset_control_array_get_exclusive(struct device_node *node)
{
	return of_reset_control_array_get(node, RESET_CONTROL_EXCLUSIVE);
}

static inline struct reset_control *
of_reset_control_array_get_exclusive_released(struct device_node *node)
{
	return of_reset_control_array_get(node, RESET_CONTROL_EXCLUSIVE_RELEASED);
}

static inline struct reset_control *
of_reset_control_array_get_shared(struct device_node *node)
{
	return of_reset_control_array_get(node, RESET_CONTROL_SHARED);
}

static inline struct reset_control *
of_reset_control_array_get_optional_exclusive(struct device_node *node)
{
	return of_reset_control_array_get(node, RESET_CONTROL_OPTIONAL_EXCLUSIVE);
}

static inline struct reset_control *
of_reset_control_array_get_optional_shared(struct device_node *node)
{
	return of_reset_control_array_get(node, RESET_CONTROL_OPTIONAL_SHARED);
}
#endif
