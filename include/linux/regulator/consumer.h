/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * consumer.h -- SoC Regulator consumer support.
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * Regulator Consumer Interface.
 *
 * A Power Management Regulator framework for SoC based devices.
 * Features:-
 *   o Voltage and current level control.
 *   o Operating mode control.
 *   o Regulator status.
 *   o sysfs entries for showing client devices and status
 *
 * EXPERIMENTAL FEATURES:
 *   Dynamic Regulator operating Mode Switching (DRMS) - allows regulators
 *   to use most efficient operating mode depending upon voltage and load and
 *   is transparent to client drivers.
 *
 *   e.g. Devices x,y,z share regulator r. Device x and y draw 20mA each during
 *   IO and 1mA at idle. Device z draws 100mA when under load and 5mA when
 *   idling. Regulator r has > 90% efficiency in NORMAL mode at loads > 100mA
 *   but this drops rapidly to 60% when below 100mA. Regulator r has > 90%
 *   efficiency in IDLE mode at loads < 10mA. Thus regulator r will operate
 *   in normal mode for loads > 10mA and in IDLE mode for load <= 10mA.
 */

#ifndef __LINUX_REGULATOR_CONSUMER_H_
#define __LINUX_REGULATOR_CONSUMER_H_

#include <linux/err.h>
#include <linux/suspend.h>
#include <regulator/regulator.h>

struct device;
struct notifier_block;
struct regmap;
struct regulator_dev;

/*
 * Regulator operating modes.
 *
 * Regulators can run in a variety of different operating modes depending on
 * output load. This allows further system power savings by selecting the
 * best (and most efficient) regulator mode for a desired load.
 *
 * Most drivers will only care about NORMAL. The modes below are generic and
 * will probably not match the naming convention of your regulator data sheet
 * but should match the use cases in the datasheet.
 *
 * In order of power efficiency (least efficient at top).
 *
 *  Mode       Description
 *  FAST       Regulator can handle fast changes in it's load.
 *             e.g. useful in CPU voltage & frequency scaling where
 *             load can quickly increase with CPU frequency increases.
 *
 *  NORMAL     Normal regulator power supply mode. Most drivers will
 *             use this mode.
 *
 *  IDLE       Regulator runs in a more efficient mode for light
 *             loads. Can be used for devices that have a low power
 *             requirement during periods of inactivity. This mode
 *             may be more noisy than NORMAL and may not be able
 *             to handle fast load switching.
 *
 *  STANDBY    Regulator runs in the most efficient mode for very
 *             light loads. Can be used by devices when they are
 *             in a sleep/standby state. This mode is likely to be
 *             the most noisy and may not be able to handle fast load
 *             switching.
 *
 * NOTE: Most regulators will only support a subset of these modes. Some
 * will only just support NORMAL.
 *
 * These modes can be OR'ed together to make up a mask of valid register modes.
 */

#define REGULATOR_MODE_INVALID			0x0
#define REGULATOR_MODE_FAST			0x1
#define REGULATOR_MODE_NORMAL			0x2
#define REGULATOR_MODE_IDLE			0x4
#define REGULATOR_MODE_STANDBY			0x8

/*
 * Regulator errors that can be queried using regulator_get_error_flags
 *
 * UNDER_VOLTAGE  Regulator output is under voltage.
 * OVER_CURRENT   Regulator output current is too high.
 * REGULATION_OUT Regulator output is out of regulation.
 * FAIL           Regulator output has failed.
 * OVER_TEMP      Regulator over temp.
 *
 * NOTE: These errors can be OR'ed together.
 */

#define REGULATOR_ERROR_UNDER_VOLTAGE		BIT(1)
#define REGULATOR_ERROR_OVER_CURRENT		BIT(2)
#define REGULATOR_ERROR_REGULATION_OUT		BIT(3)
#define REGULATOR_ERROR_FAIL			BIT(4)
#define REGULATOR_ERROR_OVER_TEMP		BIT(5)

#define REGULATOR_ERROR_UNDER_VOLTAGE_WARN	BIT(6)
#define REGULATOR_ERROR_OVER_CURRENT_WARN	BIT(7)
#define REGULATOR_ERROR_OVER_VOLTAGE_WARN	BIT(8)
#define REGULATOR_ERROR_OVER_TEMP_WARN		BIT(9)

/**
 * struct pre_voltage_change_data - Data sent with PRE_VOLTAGE_CHANGE event
 *
 * @old_uV: Current voltage before change.
 * @min_uV: Min voltage we'll change to.
 * @max_uV: Max voltage we'll change to.
 */
struct pre_voltage_change_data {
	unsigned long old_uV;
	unsigned long min_uV;
	unsigned long max_uV;
};

struct regulator;

/**
 * struct regulator_bulk_data - Data used for bulk regulator operations.
 *
 * @supply:       The name of the supply.  Initialised by the user before
 *                using the bulk regulator APIs.
 * @init_load_uA: After getting the regulator, regulator_set_load() will be
 *                called with this load.  Initialised by the user before
 *                using the bulk regulator APIs.
 * @consumer:     The regulator consumer for the supply.  This will be managed
 *                by the bulk API.
 *
 * The regulator APIs provide a series of regulator_bulk_() API calls as
 * a convenience to consumers which require multiple supplies.  This
 * structure is used to manage data for these calls.
 */
struct regulator_bulk_data {
	const char *supply;
	int init_load_uA;
	struct regulator *consumer;

	/* private: Internal use */
	int ret;
};

#if defined(CONFIG_REGULATOR)

/* regulator get and put */
struct regulator *__must_check regulator_get(struct device *dev,
					     const char *id);
struct regulator *__must_check devm_regulator_get(struct device *dev,
					     const char *id);
struct regulator *__must_check regulator_get_exclusive(struct device *dev,
						       const char *id);
struct regulator *__must_check devm_regulator_get_exclusive(struct device *dev,
							const char *id);
struct regulator *__must_check regulator_get_optional(struct device *dev,
						      const char *id);
struct regulator *__must_check devm_regulator_get_optional(struct device *dev,
							   const char *id);
int devm_regulator_get_enable(struct device *dev, const char *id);
int devm_regulator_get_enable_optional(struct device *dev, const char *id);
void regulator_put(struct regulator *regulator);
void devm_regulator_put(struct regulator *regulator);

int regulator_register_supply_alias(struct device *dev, const char *id,
				    struct device *alias_dev,
				    const char *alias_id);
void regulator_unregister_supply_alias(struct device *dev, const char *id);

int regulator_bulk_register_supply_alias(struct device *dev,
					 const char *const *id,
					 struct device *alias_dev,
					 const char *const *alias_id,
					 int num_id);
void regulator_bulk_unregister_supply_alias(struct device *dev,
					    const char * const *id, int num_id);

int devm_regulator_register_supply_alias(struct device *dev, const char *id,
					 struct device *alias_dev,
					 const char *alias_id);

int devm_regulator_bulk_register_supply_alias(struct device *dev,
					      const char *const *id,
					      struct device *alias_dev,
					      const char *const *alias_id,
					      int num_id);

/* regulator output control and status */
int __must_check regulator_enable(struct regulator *regulator);
int regulator_disable(struct regulator *regulator);
int regulator_force_disable(struct regulator *regulator);
int regulator_is_enabled(struct regulator *regulator);
int regulator_disable_deferred(struct regulator *regulator, int ms);

int __must_check regulator_bulk_get(struct device *dev, int num_consumers,
				    struct regulator_bulk_data *consumers);
int __must_check of_regulator_bulk_get_all(struct device *dev, struct device_node *np,
					   struct regulator_bulk_data **consumers);
int __must_check devm_regulator_bulk_get(struct device *dev, int num_consumers,
					 struct regulator_bulk_data *consumers);
void devm_regulator_bulk_put(struct regulator_bulk_data *consumers);
int __must_check devm_regulator_bulk_get_exclusive(struct device *dev, int num_consumers,
						   struct regulator_bulk_data *consumers);
int __must_check devm_regulator_bulk_get_const(
	struct device *dev, int num_consumers,
	const struct regulator_bulk_data *in_consumers,
	struct regulator_bulk_data **out_consumers);
int __must_check regulator_bulk_enable(int num_consumers,
				       struct regulator_bulk_data *consumers);
int devm_regulator_bulk_get_enable(struct device *dev, int num_consumers,
				   const char * const *id);
int regulator_bulk_disable(int num_consumers,
			   struct regulator_bulk_data *consumers);
int regulator_bulk_force_disable(int num_consumers,
			   struct regulator_bulk_data *consumers);
void regulator_bulk_free(int num_consumers,
			 struct regulator_bulk_data *consumers);

int regulator_count_voltages(struct regulator *regulator);
int regulator_list_voltage(struct regulator *regulator, unsigned selector);
int regulator_is_supported_voltage(struct regulator *regulator,
				   int min_uV, int max_uV);
unsigned int regulator_get_linear_step(struct regulator *regulator);
int regulator_set_voltage(struct regulator *regulator, int min_uV, int max_uV);
int regulator_set_voltage_time(struct regulator *regulator,
			       int old_uV, int new_uV);
int regulator_get_voltage(struct regulator *regulator);
int regulator_sync_voltage(struct regulator *regulator);
int regulator_set_current_limit(struct regulator *regulator,
			       int min_uA, int max_uA);
int regulator_get_current_limit(struct regulator *regulator);

int regulator_set_mode(struct regulator *regulator, unsigned int mode);
unsigned int regulator_get_mode(struct regulator *regulator);
int regulator_get_error_flags(struct regulator *regulator,
				unsigned int *flags);
int regulator_set_load(struct regulator *regulator, int load_uA);

int regulator_allow_bypass(struct regulator *regulator, bool allow);

struct regmap *regulator_get_regmap(struct regulator *regulator);
int regulator_get_hardware_vsel_register(struct regulator *regulator,
					 unsigned *vsel_reg,
					 unsigned *vsel_mask);
int regulator_list_hardware_vsel(struct regulator *regulator,
				 unsigned selector);

/* regulator notifier block */
int regulator_register_notifier(struct regulator *regulator,
			      struct notifier_block *nb);
int devm_regulator_register_notifier(struct regulator *regulator,
				     struct notifier_block *nb);
int regulator_unregister_notifier(struct regulator *regulator,
				struct notifier_block *nb);
void devm_regulator_unregister_notifier(struct regulator *regulator,
					struct notifier_block *nb);

/* regulator suspend */
int regulator_suspend_enable(struct regulator_dev *rdev,
			     suspend_state_t state);
int regulator_suspend_disable(struct regulator_dev *rdev,
			      suspend_state_t state);
int regulator_set_suspend_voltage(struct regulator *regulator, int min_uV,
				  int max_uV, suspend_state_t state);

/* driver data - core doesn't touch */
void *regulator_get_drvdata(struct regulator *regulator);
void regulator_set_drvdata(struct regulator *regulator, void *data);

/* misc helpers */

void regulator_bulk_set_supply_names(struct regulator_bulk_data *consumers,
				     const char *const *supply_names,
				     unsigned int num_supplies);

bool regulator_is_equal(struct regulator *reg1, struct regulator *reg2);

#else

/*
 * Make sure client drivers will still build on systems with no software
 * controllable voltage or current regulators.
 */
static inline struct regulator *__must_check regulator_get(struct device *dev,
	const char *id)
{
	/* Nothing except the stubbed out regulator API should be
	 * looking at the value except to check if it is an error
	 * value. Drivers are free to handle NULL specifically by
	 * skipping all regulator API calls, but they don't have to.
	 * Drivers which don't, should make sure they properly handle
	 * corner cases of the API, such as regulator_get_voltage()
	 * returning 0.
	 */
	return NULL;
}

static inline struct regulator *__must_check
devm_regulator_get(struct device *dev, const char *id)
{
	return NULL;
}

static inline struct regulator *__must_check
regulator_get_exclusive(struct device *dev, const char *id)
{
	return ERR_PTR(-ENODEV);
}

static inline struct regulator *__must_check
devm_regulator_get_exclusive(struct device *dev, const char *id)
{
	return ERR_PTR(-ENODEV);
}

static inline int devm_regulator_get_enable(struct device *dev, const char *id)
{
	return 0;
}

static inline int devm_regulator_get_enable_optional(struct device *dev,
						     const char *id)
{
	return -ENODEV;
}

static inline struct regulator *__must_check
regulator_get_optional(struct device *dev, const char *id)
{
	return ERR_PTR(-ENODEV);
}


static inline struct regulator *__must_check
devm_regulator_get_optional(struct device *dev, const char *id)
{
	return ERR_PTR(-ENODEV);
}

static inline void regulator_put(struct regulator *regulator)
{
}

static inline void devm_regulator_put(struct regulator *regulator)
{
}

static inline void devm_regulator_bulk_put(struct regulator_bulk_data *consumers)
{
}

static inline int regulator_register_supply_alias(struct device *dev,
						  const char *id,
						  struct device *alias_dev,
						  const char *alias_id)
{
	return 0;
}

static inline void regulator_unregister_supply_alias(struct device *dev,
						    const char *id)
{
}

static inline int regulator_bulk_register_supply_alias(struct device *dev,
						const char *const *id,
						struct device *alias_dev,
						const char * const *alias_id,
						int num_id)
{
	return 0;
}

static inline void regulator_bulk_unregister_supply_alias(struct device *dev,
						const char * const *id,
						int num_id)
{
}

static inline int devm_regulator_register_supply_alias(struct device *dev,
						       const char *id,
						       struct device *alias_dev,
						       const char *alias_id)
{
	return 0;
}

static inline int devm_regulator_bulk_register_supply_alias(struct device *dev,
						const char *const *id,
						struct device *alias_dev,
						const char *const *alias_id,
						int num_id)
{
	return 0;
}

static inline int regulator_enable(struct regulator *regulator)
{
	return 0;
}

static inline int regulator_disable(struct regulator *regulator)
{
	return 0;
}

static inline int regulator_force_disable(struct regulator *regulator)
{
	return 0;
}

static inline int regulator_disable_deferred(struct regulator *regulator,
					     int ms)
{
	return 0;
}

static inline int regulator_is_enabled(struct regulator *regulator)
{
	return 1;
}

static inline int regulator_bulk_get(struct device *dev,
				     int num_consumers,
				     struct regulator_bulk_data *consumers)
{
	return 0;
}

static inline int devm_regulator_bulk_get(struct device *dev, int num_consumers,
					  struct regulator_bulk_data *consumers)
{
	return 0;
}

static inline int of_regulator_bulk_get_all(struct device *dev, struct device_node *np,
					    struct regulator_bulk_data **consumers)
{
	return 0;
}

static inline int regulator_bulk_enable(int num_consumers,
					struct regulator_bulk_data *consumers)
{
	return 0;
}

static inline int devm_regulator_bulk_get_enable(struct device *dev,
						 int num_consumers,
						 const char * const *id)
{
	return 0;
}

static inline int regulator_bulk_disable(int num_consumers,
					 struct regulator_bulk_data *consumers)
{
	return 0;
}

static inline int regulator_bulk_force_disable(int num_consumers,
					struct regulator_bulk_data *consumers)
{
	return 0;
}

static inline void regulator_bulk_free(int num_consumers,
				       struct regulator_bulk_data *consumers)
{
}

static inline int regulator_set_voltage(struct regulator *regulator,
					int min_uV, int max_uV)
{
	return 0;
}

static inline int regulator_set_voltage_time(struct regulator *regulator,
					     int old_uV, int new_uV)
{
	return 0;
}

static inline int regulator_get_voltage(struct regulator *regulator)
{
	return -EINVAL;
}

static inline int regulator_sync_voltage(struct regulator *regulator)
{
	return -EINVAL;
}

static inline int regulator_is_supported_voltage(struct regulator *regulator,
				   int min_uV, int max_uV)
{
	return 0;
}

static inline unsigned int regulator_get_linear_step(struct regulator *regulator)
{
	return 0;
}

static inline int regulator_set_current_limit(struct regulator *regulator,
					     int min_uA, int max_uA)
{
	return 0;
}

static inline int regulator_get_current_limit(struct regulator *regulator)
{
	return 0;
}

static inline int regulator_set_mode(struct regulator *regulator,
	unsigned int mode)
{
	return 0;
}

static inline unsigned int regulator_get_mode(struct regulator *regulator)
{
	return REGULATOR_MODE_NORMAL;
}

static inline int regulator_get_error_flags(struct regulator *regulator,
					    unsigned int *flags)
{
	return -EINVAL;
}

static inline int regulator_set_load(struct regulator *regulator, int load_uA)
{
	return 0;
}

static inline int regulator_allow_bypass(struct regulator *regulator,
					 bool allow)
{
	return 0;
}

static inline struct regmap *regulator_get_regmap(struct regulator *regulator)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int regulator_get_hardware_vsel_register(struct regulator *regulator,
						       unsigned *vsel_reg,
						       unsigned *vsel_mask)
{
	return -EOPNOTSUPP;
}

static inline int regulator_list_hardware_vsel(struct regulator *regulator,
					       unsigned selector)
{
	return -EOPNOTSUPP;
}

static inline int regulator_register_notifier(struct regulator *regulator,
			      struct notifier_block *nb)
{
	return 0;
}

static inline int devm_regulator_register_notifier(struct regulator *regulator,
						   struct notifier_block *nb)
{
	return 0;
}

static inline int regulator_unregister_notifier(struct regulator *regulator,
				struct notifier_block *nb)
{
	return 0;
}

static inline int devm_regulator_unregister_notifier(struct regulator *regulator,
						     struct notifier_block *nb)
{
	return 0;
}

static inline int regulator_suspend_enable(struct regulator_dev *rdev,
					   suspend_state_t state)
{
	return -EINVAL;
}

static inline int regulator_suspend_disable(struct regulator_dev *rdev,
					    suspend_state_t state)
{
	return -EINVAL;
}

static inline int regulator_set_suspend_voltage(struct regulator *regulator,
						int min_uV, int max_uV,
						suspend_state_t state)
{
	return -EINVAL;
}

static inline void *regulator_get_drvdata(struct regulator *regulator)
{
	return NULL;
}

static inline void regulator_set_drvdata(struct regulator *regulator,
	void *data)
{
}

static inline int regulator_count_voltages(struct regulator *regulator)
{
	return 0;
}

static inline int regulator_list_voltage(struct regulator *regulator, unsigned selector)
{
	return -EINVAL;
}

static inline void
regulator_bulk_set_supply_names(struct regulator_bulk_data *consumers,
				const char *const *supply_names,
				unsigned int num_supplies)
{
}

static inline bool
regulator_is_equal(struct regulator *reg1, struct regulator *reg2)
{
	return false;
}
#endif

static inline int regulator_set_voltage_triplet(struct regulator *regulator,
						int min_uV, int target_uV,
						int max_uV)
{
	if (regulator_set_voltage(regulator, target_uV, max_uV) == 0)
		return 0;

	return regulator_set_voltage(regulator, min_uV, max_uV);
}

static inline int regulator_set_voltage_tol(struct regulator *regulator,
					    int new_uV, int tol_uV)
{
	if (regulator_set_voltage(regulator, new_uV, new_uV + tol_uV) == 0)
		return 0;
	else
		return regulator_set_voltage(regulator,
					     new_uV - tol_uV, new_uV + tol_uV);
}

static inline int regulator_is_supported_voltage_tol(struct regulator *regulator,
						     int target_uV, int tol_uV)
{
	return regulator_is_supported_voltage(regulator,
					      target_uV - tol_uV,
					      target_uV + tol_uV);
}

#endif
