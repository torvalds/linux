/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * driver.h -- SoC Regulator driver support.
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * Regulator Driver Interface.
 */

#ifndef __LINUX_REGULATOR_DRIVER_H_
#define __LINUX_REGULATOR_DRIVER_H_

#include <linux/device.h>
#include <linux/linear_range.h>
#include <linux/notifier.h>
#include <linux/regulator/consumer.h>
#include <linux/ww_mutex.h>

struct gpio_desc;
struct regmap;
struct regulator_dev;
struct regulator_config;
struct regulator_init_data;
struct regulator_enable_gpio;

enum regulator_status {
	REGULATOR_STATUS_OFF,
	REGULATOR_STATUS_ON,
	REGULATOR_STATUS_ERROR,
	/* fast/normal/idle/standby are flavors of "on" */
	REGULATOR_STATUS_FAST,
	REGULATOR_STATUS_NORMAL,
	REGULATOR_STATUS_IDLE,
	REGULATOR_STATUS_STANDBY,
	/* The regulator is enabled but not regulating */
	REGULATOR_STATUS_BYPASS,
	/* in case that any other status doesn't apply */
	REGULATOR_STATUS_UNDEFINED,
};

enum regulator_detection_severity {
	/* Hardware shut down voltage outputs if condition is detected */
	REGULATOR_SEVERITY_PROT,
	/* Hardware is probably damaged/inoperable */
	REGULATOR_SEVERITY_ERR,
	/* Hardware is still recoverable but recovery action must be taken */
	REGULATOR_SEVERITY_WARN,
};

/* Initialize struct linear_range for regulators */
#define REGULATOR_LINEAR_RANGE(_min_uV, _min_sel, _max_sel, _step_uV)	\
{									\
	.min		= _min_uV,					\
	.min_sel	= _min_sel,					\
	.max_sel	= _max_sel,					\
	.step		= _step_uV,					\
}

/**
 * struct regulator_ops - regulator operations.
 *
 * @enable: Configure the regulator as enabled.
 * @disable: Configure the regulator as disabled.
 * @is_enabled: Return 1 if the regulator is enabled, 0 if not.
 *		May also return negative errno.
 *
 * @set_voltage: Set the voltage for the regulator within the range specified.
 *               The driver should select the voltage closest to min_uV.
 * @set_voltage_sel: Set the voltage for the regulator using the specified
 *                   selector.
 * @map_voltage: Convert a voltage into a selector
 * @get_voltage: Return the currently configured voltage for the regulator;
 *                   return -ENOTRECOVERABLE if regulator can't be read at
 *                   bootup and hasn't been set yet.
 * @get_voltage_sel: Return the currently configured voltage selector for the
 *                   regulator; return -ENOTRECOVERABLE if regulator can't
 *                   be read at bootup and hasn't been set yet.
 * @list_voltage: Return one of the supported voltages, in microvolts; zero
 *	if the selector indicates a voltage that is unusable on this system;
 *	or negative errno.  Selectors range from zero to one less than
 *	regulator_desc.n_voltages.  Voltages may be reported in any order.
 *
 * @set_current_limit: Configure a limit for a current-limited regulator.
 *                     The driver should select the current closest to max_uA.
 * @get_current_limit: Get the configured limit for a current-limited regulator.
 * @set_input_current_limit: Configure an input limit.
 *
 * @set_over_current_protection: Support enabling of and setting limits for over
 *	current situation detection. Detection can be configured for three
 *	levels of severity.
 *
 *	- REGULATOR_SEVERITY_PROT should automatically shut down the regulator(s).
 *
 *	- REGULATOR_SEVERITY_ERR should indicate that over-current situation is
 *		  caused by an unrecoverable error but HW does not perform
 *		  automatic shut down.
 *
 *	- REGULATOR_SEVERITY_WARN should indicate situation where hardware is
 *		  still believed to not be damaged but that a board sepcific
 *		  recovery action is needed. If lim_uA is 0 the limit should not
 *		  be changed but the detection should just be enabled/disabled as
 *		  is requested.
 *
 * @set_over_voltage_protection: Support enabling of and setting limits for over
 *	voltage situation detection. Detection can be configured for same
 *	severities as over current protection. Units of uV.
 * @set_under_voltage_protection: Support enabling of and setting limits for
 *	under voltage situation detection. Detection can be configured for same
 *	severities as over current protection. Units of uV.
 * @set_thermal_protection: Support enabling of and setting limits for over
 *	temperature situation detection.Detection can be configured for same
 *	severities as over current protection. Units of degree Kelvin.
 *
 * @set_active_discharge: Set active discharge enable/disable of regulators.
 *
 * @set_mode: Set the configured operating mode for the regulator.
 * @get_mode: Get the configured operating mode for the regulator.
 * @get_error_flags: Get the current error(s) for the regulator.
 * @get_status: Return actual (not as-configured) status of regulator, as a
 *	REGULATOR_STATUS value (or negative errno)
 * @get_optimum_mode: Get the most efficient operating mode for the regulator
 *                    when running with the specified parameters.
 * @set_load: Set the load for the regulator.
 *
 * @set_bypass: Set the regulator in bypass mode.
 * @get_bypass: Get the regulator bypass mode state.
 *
 * @enable_time: Time taken for the regulator voltage output voltage to
 *               stabilise after being enabled, in microseconds.
 * @set_ramp_delay: Set the ramp delay for the regulator. The driver should
 *		select ramp delay equal to or less than(closest) ramp_delay.
 * @set_voltage_time: Time taken for the regulator voltage output voltage
 *               to stabilise after being set to a new value, in microseconds.
 *               The function receives the from and to voltage as input, it
 *               should return the worst case.
 * @set_voltage_time_sel: Time taken for the regulator voltage output voltage
 *               to stabilise after being set to a new value, in microseconds.
 *               The function receives the from and to voltage selector as
 *               input, it should return the worst case.
 * @set_soft_start: Enable soft start for the regulator.
 *
 * @set_suspend_voltage: Set the voltage for the regulator when the system
 *                       is suspended.
 * @set_suspend_enable: Mark the regulator as enabled when the system is
 *                      suspended.
 * @set_suspend_disable: Mark the regulator as disabled when the system is
 *                       suspended.
 * @set_suspend_mode: Set the operating mode for the regulator when the
 *                    system is suspended.
 * @resume: Resume operation of suspended regulator.
 * @set_pull_down: Configure the regulator to pull down when the regulator
 *		   is disabled.
 *
 * This struct describes regulator operations which can be implemented by
 * regulator chip drivers.
 */
struct regulator_ops {

	/* enumerate supported voltages */
	int (*list_voltage) (struct regulator_dev *, unsigned selector);

	/* get/set regulator voltage */
	int (*set_voltage) (struct regulator_dev *, int min_uV, int max_uV,
			    unsigned *selector);
	int (*map_voltage)(struct regulator_dev *, int min_uV, int max_uV);
	int (*set_voltage_sel) (struct regulator_dev *, unsigned selector);
	int (*get_voltage) (struct regulator_dev *);
	int (*get_voltage_sel) (struct regulator_dev *);

	/* get/set regulator current  */
	int (*set_current_limit) (struct regulator_dev *,
				 int min_uA, int max_uA);
	int (*get_current_limit) (struct regulator_dev *);

	int (*set_input_current_limit) (struct regulator_dev *, int lim_uA);
	int (*set_over_current_protection)(struct regulator_dev *, int lim_uA,
					   int severity, bool enable);
	int (*set_over_voltage_protection)(struct regulator_dev *, int lim_uV,
					   int severity, bool enable);
	int (*set_under_voltage_protection)(struct regulator_dev *, int lim_uV,
					    int severity, bool enable);
	int (*set_thermal_protection)(struct regulator_dev *, int lim,
				      int severity, bool enable);
	int (*set_active_discharge)(struct regulator_dev *, bool enable);

	/* enable/disable regulator */
	int (*enable) (struct regulator_dev *);
	int (*disable) (struct regulator_dev *);
	int (*is_enabled) (struct regulator_dev *);

	/* get/set regulator operating mode (defined in consumer.h) */
	int (*set_mode) (struct regulator_dev *, unsigned int mode);
	unsigned int (*get_mode) (struct regulator_dev *);

	/* retrieve current error flags on the regulator */
	int (*get_error_flags)(struct regulator_dev *, unsigned int *flags);

	/* Time taken to enable or set voltage on the regulator */
	int (*enable_time) (struct regulator_dev *);
	int (*set_ramp_delay) (struct regulator_dev *, int ramp_delay);
	int (*set_voltage_time) (struct regulator_dev *, int old_uV,
				 int new_uV);
	int (*set_voltage_time_sel) (struct regulator_dev *,
				     unsigned int old_selector,
				     unsigned int new_selector);

	int (*set_soft_start) (struct regulator_dev *);

	/* report regulator status ... most other accessors report
	 * control inputs, this reports results of combining inputs
	 * from Linux (and other sources) with the actual load.
	 * returns REGULATOR_STATUS_* or negative errno.
	 */
	int (*get_status)(struct regulator_dev *);

	/* get most efficient regulator operating mode for load */
	unsigned int (*get_optimum_mode) (struct regulator_dev *, int input_uV,
					  int output_uV, int load_uA);
	/* set the load on the regulator */
	int (*set_load)(struct regulator_dev *, int load_uA);

	/* control and report on bypass mode */
	int (*set_bypass)(struct regulator_dev *dev, bool enable);
	int (*get_bypass)(struct regulator_dev *dev, bool *enable);

	/* the operations below are for configuration of regulator state when
	 * its parent PMIC enters a global STANDBY/HIBERNATE state */

	/* set regulator suspend voltage */
	int (*set_suspend_voltage) (struct regulator_dev *, int uV);

	/* enable/disable regulator in suspend state */
	int (*set_suspend_enable) (struct regulator_dev *);
	int (*set_suspend_disable) (struct regulator_dev *);

	/* set regulator suspend operating mode (defined in consumer.h) */
	int (*set_suspend_mode) (struct regulator_dev *, unsigned int mode);

	int (*resume)(struct regulator_dev *rdev);

	int (*set_pull_down) (struct regulator_dev *);
};

/*
 * Regulators can either control voltage or current.
 */
enum regulator_type {
	REGULATOR_VOLTAGE,
	REGULATOR_CURRENT,
};

/**
 * struct regulator_desc - Static regulator descriptor
 *
 * Each regulator registered with the core is described with a
 * structure of this type and a struct regulator_config.  This
 * structure contains the non-varying parts of the regulator
 * description.
 *
 * @name: Identifying name for the regulator.
 * @supply_name: Identifying the regulator supply
 * @of_match: Name used to identify regulator in DT.
 * @of_match_full_name: A flag to indicate that the of_match string, if
 *			present, should be matched against the node full_name.
 * @regulators_node: Name of node containing regulator definitions in DT.
 * @of_parse_cb: Optional callback called only if of_match is present.
 *               Will be called for each regulator parsed from DT, during
 *               init_data parsing.
 *               The regulator_config passed as argument to the callback will
 *               be a copy of config passed to regulator_register, valid only
 *               for this particular call. Callback may freely change the
 *               config but it cannot store it for later usage.
 *               Callback should return 0 on success or negative ERRNO
 *               indicating failure.
 * @id: Numerical identifier for the regulator.
 * @ops: Regulator operations table.
 * @irq: Interrupt number for the regulator.
 * @type: Indicates if the regulator is a voltage or current regulator.
 * @owner: Module providing the regulator, used for refcounting.
 *
 * @continuous_voltage_range: Indicates if the regulator can set any
 *                            voltage within constrains range.
 * @n_voltages: Number of selectors available for ops.list_voltage().
 * @n_current_limits: Number of selectors available for current limits
 *
 * @min_uV: Voltage given by the lowest selector (if linear mapping)
 * @uV_step: Voltage increase with each selector (if linear mapping)
 * @linear_min_sel: Minimal selector for starting linear mapping
 * @fixed_uV: Fixed voltage of rails.
 * @ramp_delay: Time to settle down after voltage change (unit: uV/us)
 * @min_dropout_uV: The minimum dropout voltage this regulator can handle
 * @linear_ranges: A constant table of possible voltage ranges.
 * @linear_range_selectors: A constant table of voltage range selectors.
 *			    If pickable ranges are used each range must
 *			    have corresponding selector here.
 * @n_linear_ranges: Number of entries in the @linear_ranges (and in
 *		     linear_range_selectors if used) table(s).
 * @volt_table: Voltage mapping table (if table based mapping)
 * @curr_table: Current limit mapping table (if table based mapping)
 *
 * @vsel_range_reg: Register for range selector when using pickable ranges
 *		    and ``regulator_map_*_voltage_*_pickable`` functions.
 * @vsel_range_mask: Mask for register bitfield used for range selector
 * @vsel_reg: Register for selector when using ``regulator_map_*_voltage_*``
 * @vsel_mask: Mask for register bitfield used for selector
 * @vsel_step: Specify the resolution of selector stepping when setting
 *	       voltage. If 0, then no stepping is done (requested selector is
 *	       set directly), if >0 then the regulator API will ramp the
 *	       voltage up/down gradually each time increasing/decreasing the
 *	       selector by the specified step value.
 * @csel_reg: Register for current limit selector using regmap set_current_limit
 * @csel_mask: Mask for register bitfield used for current limit selector
 * @apply_reg: Register for initiate voltage change on the output when
 *                using regulator_set_voltage_sel_regmap
 * @apply_bit: Register bitfield used for initiate voltage change on the
 *                output when using regulator_set_voltage_sel_regmap
 * @enable_reg: Register for control when using regmap enable/disable ops
 * @enable_mask: Mask for control when using regmap enable/disable ops
 * @enable_val: Enabling value for control when using regmap enable/disable ops
 * @disable_val: Disabling value for control when using regmap enable/disable ops
 * @enable_is_inverted: A flag to indicate set enable_mask bits to disable
 *                      when using regulator_enable_regmap and friends APIs.
 * @bypass_reg: Register for control when using regmap set_bypass
 * @bypass_mask: Mask for control when using regmap set_bypass
 * @bypass_val_on: Enabling value for control when using regmap set_bypass
 * @bypass_val_off: Disabling value for control when using regmap set_bypass
 * @active_discharge_off: Enabling value for control when using regmap
 *			  set_active_discharge
 * @active_discharge_on: Disabling value for control when using regmap
 *			 set_active_discharge
 * @active_discharge_mask: Mask for control when using regmap
 *			   set_active_discharge
 * @active_discharge_reg: Register for control when using regmap
 *			  set_active_discharge
 * @soft_start_reg: Register for control when using regmap set_soft_start
 * @soft_start_mask: Mask for control when using regmap set_soft_start
 * @soft_start_val_on: Enabling value for control when using regmap
 *                     set_soft_start
 * @pull_down_reg: Register for control when using regmap set_pull_down
 * @pull_down_mask: Mask for control when using regmap set_pull_down
 * @pull_down_val_on: Enabling value for control when using regmap
 *                     set_pull_down
 *
 * @ramp_reg:		Register for controlling the regulator ramp-rate.
 * @ramp_mask:		Bitmask for the ramp-rate control register.
 * @ramp_delay_table:	Table for mapping the regulator ramp-rate values. Values
 *			should be given in units of V/S (uV/uS). See the
 *			regulator_set_ramp_delay_regmap().
 * @n_ramp_values:	number of elements at @ramp_delay_table.
 *
 * @enable_time: Time taken for initial enable of regulator (in uS).
 * @off_on_delay: guard time (in uS), before re-enabling a regulator
 *
 * @poll_enabled_time: The polling interval (in uS) to use while checking that
 *                     the regulator was actually enabled. Max upto enable_time.
 *
 * @of_map_mode: Maps a hardware mode defined in a DeviceTree to a standard mode
 */
struct regulator_desc {
	const char *name;
	const char *supply_name;
	const char *of_match;
	bool of_match_full_name;
	const char *regulators_node;
	int (*of_parse_cb)(struct device_node *,
			    const struct regulator_desc *,
			    struct regulator_config *);
	int id;
	unsigned int continuous_voltage_range:1;
	unsigned n_voltages;
	unsigned int n_current_limits;
	const struct regulator_ops *ops;
	int irq;
	enum regulator_type type;
	struct module *owner;

	unsigned int min_uV;
	unsigned int uV_step;
	unsigned int linear_min_sel;
	int fixed_uV;
	unsigned int ramp_delay;
	int min_dropout_uV;

	const struct linear_range *linear_ranges;
	const unsigned int *linear_range_selectors;

	int n_linear_ranges;

	const unsigned int *volt_table;
	const unsigned int *curr_table;

	unsigned int vsel_range_reg;
	unsigned int vsel_range_mask;
	unsigned int vsel_reg;
	unsigned int vsel_mask;
	unsigned int vsel_step;
	unsigned int csel_reg;
	unsigned int csel_mask;
	unsigned int apply_reg;
	unsigned int apply_bit;
	unsigned int enable_reg;
	unsigned int enable_mask;
	unsigned int enable_val;
	unsigned int disable_val;
	bool enable_is_inverted;
	unsigned int bypass_reg;
	unsigned int bypass_mask;
	unsigned int bypass_val_on;
	unsigned int bypass_val_off;
	unsigned int active_discharge_on;
	unsigned int active_discharge_off;
	unsigned int active_discharge_mask;
	unsigned int active_discharge_reg;
	unsigned int soft_start_reg;
	unsigned int soft_start_mask;
	unsigned int soft_start_val_on;
	unsigned int pull_down_reg;
	unsigned int pull_down_mask;
	unsigned int pull_down_val_on;
	unsigned int ramp_reg;
	unsigned int ramp_mask;
	const unsigned int *ramp_delay_table;
	unsigned int n_ramp_values;

	unsigned int enable_time;

	unsigned int off_on_delay;

	unsigned int poll_enabled_time;

	unsigned int (*of_map_mode)(unsigned int mode);
};

/**
 * struct regulator_config - Dynamic regulator descriptor
 *
 * Each regulator registered with the core is described with a
 * structure of this type and a struct regulator_desc.  This structure
 * contains the runtime variable parts of the regulator description.
 *
 * @dev: struct device for the regulator
 * @init_data: platform provided init data, passed through by driver
 * @driver_data: private regulator data
 * @of_node: OpenFirmware node to parse for device tree bindings (may be
 *           NULL).
 * @regmap: regmap to use for core regmap helpers if dev_get_regmap() is
 *          insufficient.
 * @ena_gpiod: GPIO controlling regulator enable.
 */
struct regulator_config {
	struct device *dev;
	const struct regulator_init_data *init_data;
	void *driver_data;
	struct device_node *of_node;
	struct regmap *regmap;

	struct gpio_desc *ena_gpiod;
};

/**
 * struct regulator_err_state - regulator error/notification status
 *
 * @rdev:		Regulator which status the struct indicates.
 * @notifs:		Events which have occurred on the regulator.
 * @errors:		Errors which are active on the regulator.
 * @possible_errs:	Errors which can be signaled (by given IRQ).
 */
struct regulator_err_state {
	struct regulator_dev *rdev;
	unsigned long notifs;
	unsigned long errors;
	int possible_errs;
};

/**
 * struct regulator_irq_data - regulator error/notification status data
 *
 * @states:	Status structs for each of the associated regulators.
 * @num_states:	Amount of associated regulators.
 * @data:	Driver data pointer given at regulator_irq_desc.
 * @opaque:	Value storage for IC driver. Core does not update this. ICs
 *		may want to store status register value here at map_event and
 *		compare contents at 'renable' callback to see if new problems
 *		have been added to status. If that is the case it may be
 *		desirable to return REGULATOR_ERROR_CLEARED and not
 *		REGULATOR_ERROR_ON to allow IRQ fire again and to generate
 *		notifications also for the new issues.
 *
 * This structure is passed to 'map_event' and 'renable' callbacks for
 * reporting regulator status to core.
 */
struct regulator_irq_data {
	struct regulator_err_state *states;
	int num_states;
	void *data;
	long opaque;
};

/**
 * struct regulator_irq_desc - notification sender for IRQ based events.
 *
 * @name:	The visible name for the IRQ
 * @fatal_cnt:	If this IRQ is used to signal HW damaging condition it may be
 *		best to shut-down regulator(s) or reboot the SOC if error
 *		handling is repeatedly failing. If fatal_cnt is given the IRQ
 *		handling is aborted if it fails for fatal_cnt times and die()
 *		callback (if populated) is called. If die() is not populated
 *		poweroff for the system is attempted in order to prevent any
 *		further damage.
 * @reread_ms:	The time which is waited before attempting to re-read status
 *		at the worker if IC reading fails. Immediate re-read is done
 *		if time is not specified.
 * @irq_off_ms:	The time which IRQ is kept disabled before re-evaluating the
 *		status for devices which keep IRQ disabled for duration of the
 *		error. If this is not given the IRQ is left enabled and renable
 *		is not called.
 * @skip_off:	If set to true the IRQ handler will attempt to check if any of
 *		the associated regulators are enabled prior to taking other
 *		actions. If no regulators are enabled and this is set to true
 *		a spurious IRQ is assumed and IRQ_NONE is returned.
 * @high_prio:	Boolean to indicate that high priority WQ should be used.
 * @data:	Driver private data pointer which will be passed as such to
 *		the renable, map_event and die callbacks in regulator_irq_data.
 * @die:	Protection callback. If IC status reading or recovery actions
 *		fail fatal_cnt times this callback is called or system is
 *		powered off. This callback should implement a final protection
 *		attempt like disabling the regulator. If protection succeeded
 *		die() may return 0. If anything else is returned the core
 *		assumes final protection failed and attempts to perform a
 *		poweroff as a last resort.
 * @map_event:	Driver callback to map IRQ status into regulator devices with
 *		events / errors. NOTE: callback MUST initialize both the
 *		errors and notifs for all rdevs which it signals having
 *		active events as core does not clean the map data.
 *		REGULATOR_FAILED_RETRY can be returned to indicate that the
 *		status reading from IC failed. If this is repeated for
 *		fatal_cnt times the core will call die() callback or power-off
 *		the system as a last resort to protect the HW.
 * @renable:	Optional callback to check status (if HW supports that) before
 *		re-enabling IRQ. If implemented this should clear the error
 *		flags so that errors fetched by regulator_get_error_flags()
 *		are updated. If callback is not implemented then errors are
 *		assumed to be cleared and IRQ is re-enabled.
 *		REGULATOR_FAILED_RETRY can be returned to
 *		indicate that the status reading from IC failed. If this is
 *		repeated for 'fatal_cnt' times the core will call die()
 *		callback or if die() is not populated then attempt to power-off
 *		the system as a last resort to protect the HW.
 *		Returning zero indicates that the problem in HW has been solved
 *		and IRQ will be re-enabled. Returning REGULATOR_ERROR_ON
 *		indicates the error condition is still active and keeps IRQ
 *		disabled. Please note that returning REGULATOR_ERROR_ON does
 *		not retrigger evaluating what events are active or resending
 *		notifications. If this is needed you probably want to return
 *		zero and allow IRQ to retrigger causing events to be
 *		re-evaluated and re-sent.
 *
 * This structure is used for registering regulator IRQ notification helper.
 */
struct regulator_irq_desc {
	const char *name;
	int fatal_cnt;
	int reread_ms;
	int irq_off_ms;
	bool skip_off;
	bool high_prio;
	void *data;

	int (*die)(struct regulator_irq_data *rid);
	int (*map_event)(int irq, struct regulator_irq_data *rid,
			  unsigned long *dev_mask);
	int (*renable)(struct regulator_irq_data *rid);
};

/*
 * Return values for regulator IRQ helpers.
 */
enum {
	REGULATOR_ERROR_CLEARED,
	REGULATOR_FAILED_RETRY,
	REGULATOR_ERROR_ON,
};

/*
 * struct coupling_desc
 *
 * Describes coupling of regulators. Each regulator should have
 * at least a pointer to itself in coupled_rdevs array.
 * When a new coupled regulator is resolved, n_resolved is
 * incremented.
 */
struct coupling_desc {
	struct regulator_dev **coupled_rdevs;
	struct regulator_coupler *coupler;
	int n_resolved;
	int n_coupled;
};

/*
 * struct regulator_dev
 *
 * Voltage / Current regulator class device. One for each
 * regulator.
 *
 * This should *not* be used directly by anything except the regulator
 * core and notification injection (which should take the mutex and do
 * no other direct access).
 */
struct regulator_dev {
	const struct regulator_desc *desc;
	int exclusive;
	u32 use_count;
	u32 open_count;
	u32 bypass_count;

	/* lists we belong to */
	struct list_head list; /* list of all regulators */

	/* lists we own */
	struct list_head consumer_list; /* consumers we supply */

	struct coupling_desc coupling_desc;

	struct blocking_notifier_head notifier;
	struct ww_mutex mutex; /* consumer lock */
	struct task_struct *mutex_owner;
	int ref_cnt;
	struct module *owner;
	struct device dev;
	struct regulation_constraints *constraints;
	struct regulator *supply;	/* for tree */
	const char *supply_name;
	struct regmap *regmap;

	struct delayed_work disable_work;

	void *reg_data;		/* regulator_dev data */

	struct dentry *debugfs;

	struct regulator_enable_gpio *ena_pin;
	unsigned int ena_gpio_state:1;

	unsigned int is_switch:1;

	/* time when this regulator was disabled last time */
	ktime_t last_off;
	int cached_err;
	bool use_cached_err;
	spinlock_t err_lock;
};

/*
 * Convert error flags to corresponding notifications.
 *
 * Can be used by drivers which use the notification helpers to
 * find out correct notification flags based on the error flags. Drivers
 * can avoid storing both supported notification and error flags which
 * may save few bytes.
 */
static inline int regulator_err2notif(int err)
{
	switch (err) {
	case REGULATOR_ERROR_UNDER_VOLTAGE:
		return REGULATOR_EVENT_UNDER_VOLTAGE;
	case REGULATOR_ERROR_OVER_CURRENT:
		return REGULATOR_EVENT_OVER_CURRENT;
	case REGULATOR_ERROR_REGULATION_OUT:
		return REGULATOR_EVENT_REGULATION_OUT;
	case REGULATOR_ERROR_FAIL:
		return REGULATOR_EVENT_FAIL;
	case REGULATOR_ERROR_OVER_TEMP:
		return REGULATOR_EVENT_OVER_TEMP;
	case REGULATOR_ERROR_UNDER_VOLTAGE_WARN:
		return REGULATOR_EVENT_UNDER_VOLTAGE_WARN;
	case REGULATOR_ERROR_OVER_CURRENT_WARN:
		return REGULATOR_EVENT_OVER_CURRENT_WARN;
	case REGULATOR_ERROR_OVER_VOLTAGE_WARN:
		return REGULATOR_EVENT_OVER_VOLTAGE_WARN;
	case REGULATOR_ERROR_OVER_TEMP_WARN:
		return REGULATOR_EVENT_OVER_TEMP_WARN;
	}
	return 0;
}


struct regulator_dev *
regulator_register(struct device *dev,
		   const struct regulator_desc *regulator_desc,
		   const struct regulator_config *config);
struct regulator_dev *
devm_regulator_register(struct device *dev,
			const struct regulator_desc *regulator_desc,
			const struct regulator_config *config);
void regulator_unregister(struct regulator_dev *rdev);

int regulator_notifier_call_chain(struct regulator_dev *rdev,
				  unsigned long event, void *data);
void *devm_regulator_irq_helper(struct device *dev,
				const struct regulator_irq_desc *d, int irq,
				int irq_flags, int common_errs,
				int *per_rdev_errs, struct regulator_dev **rdev,
				int rdev_amount);
void *regulator_irq_helper(struct device *dev,
			   const struct regulator_irq_desc *d, int irq,
			   int irq_flags, int common_errs, int *per_rdev_errs,
			   struct regulator_dev **rdev, int rdev_amount);
void regulator_irq_helper_cancel(void **handle);
int regulator_irq_map_event_simple(int irq, struct regulator_irq_data *rid,
				   unsigned long *dev_mask);

void *rdev_get_drvdata(struct regulator_dev *rdev);
struct device *rdev_get_dev(struct regulator_dev *rdev);
struct regmap *rdev_get_regmap(struct regulator_dev *rdev);
int rdev_get_id(struct regulator_dev *rdev);

int regulator_mode_to_status(unsigned int);

int regulator_list_voltage_linear(struct regulator_dev *rdev,
				  unsigned int selector);
int regulator_list_voltage_pickable_linear_range(struct regulator_dev *rdev,
						   unsigned int selector);
int regulator_list_voltage_linear_range(struct regulator_dev *rdev,
					unsigned int selector);
int regulator_list_voltage_table(struct regulator_dev *rdev,
				  unsigned int selector);
int regulator_map_voltage_linear(struct regulator_dev *rdev,
				  int min_uV, int max_uV);
int regulator_map_voltage_pickable_linear_range(struct regulator_dev *rdev,
						  int min_uV, int max_uV);
int regulator_map_voltage_linear_range(struct regulator_dev *rdev,
				       int min_uV, int max_uV);
int regulator_map_voltage_iterate(struct regulator_dev *rdev,
				  int min_uV, int max_uV);
int regulator_map_voltage_ascend(struct regulator_dev *rdev,
				  int min_uV, int max_uV);
int regulator_get_voltage_sel_pickable_regmap(struct regulator_dev *rdev);
int regulator_set_voltage_sel_pickable_regmap(struct regulator_dev *rdev,
						unsigned int sel);
int regulator_get_voltage_sel_regmap(struct regulator_dev *rdev);
int regulator_set_voltage_sel_regmap(struct regulator_dev *rdev, unsigned sel);
int regulator_is_enabled_regmap(struct regulator_dev *rdev);
int regulator_enable_regmap(struct regulator_dev *rdev);
int regulator_disable_regmap(struct regulator_dev *rdev);
int regulator_set_voltage_time_sel(struct regulator_dev *rdev,
				   unsigned int old_selector,
				   unsigned int new_selector);
int regulator_set_bypass_regmap(struct regulator_dev *rdev, bool enable);
int regulator_get_bypass_regmap(struct regulator_dev *rdev, bool *enable);
int regulator_set_soft_start_regmap(struct regulator_dev *rdev);
int regulator_set_pull_down_regmap(struct regulator_dev *rdev);

int regulator_set_active_discharge_regmap(struct regulator_dev *rdev,
					  bool enable);
int regulator_set_current_limit_regmap(struct regulator_dev *rdev,
				       int min_uA, int max_uA);
int regulator_get_current_limit_regmap(struct regulator_dev *rdev);
void *regulator_get_init_drvdata(struct regulator_init_data *reg_init_data);
int regulator_find_closest_bigger(unsigned int target, const unsigned int *table,
				  unsigned int num_sel, unsigned int *sel);
int regulator_set_ramp_delay_regmap(struct regulator_dev *rdev, int ramp_delay);
int regulator_sync_voltage_rdev(struct regulator_dev *rdev);

/*
 * Helper functions intended to be used by regulator drivers prior registering
 * their regulators.
 */
int regulator_desc_list_voltage_linear_range(const struct regulator_desc *desc,
					     unsigned int selector);

int regulator_desc_list_voltage_linear(const struct regulator_desc *desc,
				       unsigned int selector);

#ifdef CONFIG_REGULATOR
const char *rdev_get_name(struct regulator_dev *rdev);
#else
static inline const char *rdev_get_name(struct regulator_dev *rdev)
{
	return NULL;
}
#endif

#endif
