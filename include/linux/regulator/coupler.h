/* SPDX-License-Identifier: GPL-2.0 */
/*
 * coupler.h -- SoC Regulator support, coupler API.
 *
 * Regulator Coupler Interface.
 */

#ifndef __LINUX_REGULATOR_COUPLER_H_
#define __LINUX_REGULATOR_COUPLER_H_

#include <linux/kernel.h>
#include <linux/suspend.h>

struct regulator_coupler;
struct regulator_dev;

/**
 * struct regulator_coupler - customized regulator's coupler
 *
 * Regulator's coupler allows to customize coupling algorithm.
 *
 * @list: couplers list entry
 * @attach_regulator: Callback invoked on creation of a coupled regulator,
 *                    couples are unresolved at this point. The callee should
 *                    check that it could handle the regulator and return 0 on
 *                    success, -errno on failure and 1 if given regulator is
 *                    not suitable for this coupler (case of having multiple
 *                    regulators in a system). Callback shall be implemented.
 * @detach_regulator: Callback invoked on destruction of a coupled regulator.
 *                    This callback is optional and could be NULL.
 * @balance_voltage: Callback invoked when voltage of a coupled regulator is
 *                   changing. Called with all of the coupled rdev's being held
 *                   under "consumer lock". The callee should perform voltage
 *                   balancing, changing voltage of the coupled regulators as
 *                   needed. It's up to the coupler to verify the voltage
 *                   before changing it in hardware, i.e. coupler should
 *                   check consumer's min/max and etc. This callback is
 *                   optional and could be NULL, in which case a generic
 *                   voltage balancer will be used.
 */
struct regulator_coupler {
	struct list_head list;

	int (*attach_regulator)(struct regulator_coupler *coupler,
				struct regulator_dev *rdev);
	int (*detach_regulator)(struct regulator_coupler *coupler,
				struct regulator_dev *rdev);
	int (*balance_voltage)(struct regulator_coupler *coupler,
			       struct regulator_dev *rdev,
			       suspend_state_t state);
};

#ifdef CONFIG_REGULATOR
int regulator_coupler_register(struct regulator_coupler *coupler);
const char *rdev_get_name(struct regulator_dev *rdev);
int regulator_check_consumers(struct regulator_dev *rdev,
			      int *min_uV, int *max_uV,
			      suspend_state_t state);
int regulator_check_voltage(struct regulator_dev *rdev,
			    int *min_uV, int *max_uV);
int regulator_get_voltage_rdev(struct regulator_dev *rdev);
int regulator_set_voltage_rdev(struct regulator_dev *rdev,
			       int min_uV, int max_uV,
			       suspend_state_t state);
#else
static inline int regulator_coupler_register(struct regulator_coupler *coupler)
{
	return 0;
}
static inline const char *rdev_get_name(struct regulator_dev *rdev)
{
	return NULL;
}
static inline int regulator_check_consumers(struct regulator_dev *rdev,
					    int *min_uV, int *max_uV,
					    suspend_state_t state)
{
	return -EINVAL;
}
static inline int regulator_check_voltage(struct regulator_dev *rdev,
					  int *min_uV, int *max_uV)
{
	return -EINVAL;
}
static inline int regulator_get_voltage_rdev(struct regulator_dev *rdev)
{
	return -EINVAL;
}
static inline int regulator_set_voltage_rdev(struct regulator_dev *rdev,
					     int min_uV, int max_uV,
					     suspend_state_t state)
{
	return -EINVAL;
}
#endif

#endif
