/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * machine.h -- SoC Regulator support, machine/board driver API.
 *
 * Copyright (C) 2007, 2008 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * Regulator Machine/Board Interface.
 */

#ifndef __LINUX_REGULATOR_MACHINE_H_
#define __LINUX_REGULATOR_MACHINE_H_

#include <linux/regulator/consumer.h>
#include <linux/suspend.h>

struct regulator;

/*
 * Regulator operation constraint flags. These flags are used to enable
 * certain regulator operations and can be OR'ed together.
 *
 * VOLTAGE:  Regulator output voltage can be changed by software on this
 *           board/machine.
 * CURRENT:  Regulator output current can be changed by software on this
 *           board/machine.
 * MODE:     Regulator operating mode can be changed by software on this
 *           board/machine.
 * STATUS:   Regulator can be enabled and disabled.
 * DRMS:     Dynamic Regulator Mode Switching is enabled for this regulator.
 * BYPASS:   Regulator can be put into bypass mode
 */

#define REGULATOR_CHANGE_VOLTAGE	0x1
#define REGULATOR_CHANGE_CURRENT	0x2
#define REGULATOR_CHANGE_MODE		0x4
#define REGULATOR_CHANGE_STATUS		0x8
#define REGULATOR_CHANGE_DRMS		0x10
#define REGULATOR_CHANGE_BYPASS		0x20

/*
 * operations in suspend mode
 * DO_NOTHING_IN_SUSPEND - the default value
 * DISABLE_IN_SUSPEND	- turn off regulator in suspend states
 * ENABLE_IN_SUSPEND	- keep regulator on in suspend states
 */
#define DO_NOTHING_IN_SUSPEND	0
#define DISABLE_IN_SUSPEND	1
#define ENABLE_IN_SUSPEND	2

/*
 * Default time window (in milliseconds) following a critical under-voltage
 * event during which less critical actions can be safely carried out by the
 * system.
 */
#define REGULATOR_DEF_UV_LESS_CRITICAL_WINDOW_MS	10

/* Regulator active discharge flags */
enum regulator_active_discharge {
	REGULATOR_ACTIVE_DISCHARGE_DEFAULT,
	REGULATOR_ACTIVE_DISCHARGE_DISABLE,
	REGULATOR_ACTIVE_DISCHARGE_ENABLE,
};

/**
 * struct regulator_state - regulator state during low power system states
 *
 * This describes a regulators state during a system wide low power
 * state.  One of enabled or disabled must be set for the
 * configuration to be applied.
 *
 * @uV: Default operating voltage during suspend, it can be adjusted
 *	among <min_uV, max_uV>.
 * @min_uV: Minimum suspend voltage may be set.
 * @max_uV: Maximum suspend voltage may be set.
 * @mode: Operating mode during suspend.
 * @enabled: operations during suspend.
 *	     - DO_NOTHING_IN_SUSPEND
 *	     - DISABLE_IN_SUSPEND
 *	     - ENABLE_IN_SUSPEND
 * @changeable: Is this state can be switched between enabled/disabled,
 */
struct regulator_state {
	int uV;
	int min_uV;
	int max_uV;
	unsigned int mode;
	int enabled;
	bool changeable;
};

#define REGULATOR_NOTIF_LIMIT_DISABLE -1
#define REGULATOR_NOTIF_LIMIT_ENABLE -2
struct notification_limit {
	int prot;
	int err;
	int warn;
};

/**
 * struct regulation_constraints - regulator operating constraints.
 *
 * This struct describes regulator and board/machine specific constraints.
 *
 * @name: Descriptive name for the constraints, used for display purposes.
 *
 * @min_uV: Smallest voltage consumers may set.
 * @max_uV: Largest voltage consumers may set.
 * @uV_offset: Offset applied to voltages from consumer to compensate for
 *             voltage drops.
 *
 * @min_uA: Smallest current consumers may set.
 * @max_uA: Largest current consumers may set.
 * @ilim_uA: Maximum input current.
 * @pw_budget_mW: Power budget for the regulator in mW.
 * @system_load: Load that isn't captured by any consumer requests.
 *
 * @over_curr_limits:		Limits for acting on over current.
 * @over_voltage_limits:	Limits for acting on over voltage.
 * @under_voltage_limits:	Limits for acting on under voltage.
 * @temp_limits:		Limits for acting on over temperature.
 *
 * @max_spread: Max possible spread between coupled regulators
 * @max_uV_step: Max possible step change in voltage
 * @valid_modes_mask: Mask of modes which may be configured by consumers.
 * @valid_ops_mask: Operations which may be performed by consumers.
 *
 * @always_on: Set if the regulator should never be disabled.
 * @boot_on: Set if the regulator is enabled when the system is initially
 *           started.  If the regulator is not enabled by the hardware or
 *           bootloader then it will be enabled when the constraints are
 *           applied.
 * @apply_uV: Apply the voltage constraint when initialising.
 * @ramp_disable: Disable ramp delay when initialising or when setting voltage.
 * @soft_start: Enable soft start so that voltage ramps slowly.
 * @pull_down: Enable pull down when regulator is disabled.
 * @system_critical: Set if the regulator is critical to system stability or
 *                   functionality.
 * @over_current_protection: Auto disable on over current event.
 *
 * @over_current_detection: Configure over current limits.
 * @over_voltage_detection: Configure over voltage limits.
 * @under_voltage_detection: Configure under voltage limits.
 * @over_temp_detection: Configure over temperature limits.
 *
 * @input_uV: Input voltage for regulator when supplied by another regulator.
 *
 * @state_disk: State for regulator when system is suspended in disk mode.
 * @state_mem: State for regulator when system is suspended in mem mode.
 * @state_standby: State for regulator when system is suspended in standby
 *                 mode.
 * @initial_state: Suspend state to set by default.
 * @initial_mode: Mode to set at startup.
 * @ramp_delay: Time to settle down after voltage change (unit: uV/us)
 * @settling_time: Time to settle down after voltage change when voltage
 *		   change is non-linear (unit: microseconds).
 * @settling_time_up: Time to settle down after voltage increase when voltage
 *		      change is non-linear (unit: microseconds).
 * @settling_time_down : Time to settle down after voltage decrease when
 *			 voltage change is non-linear (unit: microseconds).
 * @active_discharge: Enable/disable active discharge. The enum
 *		      regulator_active_discharge values are used for
 *		      initialisation.
 * @enable_time: Turn-on time of the rails (unit: microseconds)
 * @uv_less_critical_window_ms: Specifies the time window (in milliseconds)
 *                              following a critical under-voltage (UV) event
 *                              during which less critical actions can be
 *                              safely carried out by the system (for example
 *                              logging). After this time window more critical
 *                              actions should be done (for example prevent
 *                              HW damage).
 */
struct regulation_constraints {

	const char *name;

	/* voltage output range (inclusive) - for voltage control */
	int min_uV;
	int max_uV;

	int uV_offset;

	/* current output range (inclusive) - for current control */
	int min_uA;
	int max_uA;
	int ilim_uA;

	int pw_budget_mW;
	int system_load;

	/* used for coupled regulators */
	u32 *max_spread;

	/* used for changing voltage in steps */
	int max_uV_step;

	/* valid regulator operating modes for this machine */
	unsigned int valid_modes_mask;

	/* valid operations for regulator on this machine */
	unsigned int valid_ops_mask;

	/* regulator input voltage - only if supply is another regulator */
	int input_uV;

	/* regulator suspend states for global PMIC STANDBY/HIBERNATE */
	struct regulator_state state_disk;
	struct regulator_state state_mem;
	struct regulator_state state_standby;
	struct notification_limit over_curr_limits;
	struct notification_limit over_voltage_limits;
	struct notification_limit under_voltage_limits;
	struct notification_limit temp_limits;
	suspend_state_t initial_state; /* suspend state to set at init */

	/* mode to set on startup */
	unsigned int initial_mode;

	unsigned int ramp_delay;
	unsigned int settling_time;
	unsigned int settling_time_up;
	unsigned int settling_time_down;
	unsigned int enable_time;
	unsigned int uv_less_critical_window_ms;

	unsigned int active_discharge;

	/* constraint flags */
	unsigned always_on:1;	/* regulator never off when system is on */
	unsigned boot_on:1;	/* bootloader/firmware enabled regulator */
	unsigned apply_uV:1;	/* apply uV constraint if min == max */
	unsigned ramp_disable:1; /* disable ramp delay */
	unsigned soft_start:1;	/* ramp voltage slowly */
	unsigned pull_down:1;	/* pull down resistor when regulator off */
	unsigned system_critical:1;	/* critical to system stability */
	unsigned over_current_protection:1; /* auto disable on over current */
	unsigned over_current_detection:1; /* notify on over current */
	unsigned over_voltage_detection:1; /* notify on over voltage */
	unsigned under_voltage_detection:1; /* notify on under voltage */
	unsigned over_temp_detection:1; /* notify on over temperature */
};

/**
 * struct regulator_consumer_supply - supply -> device mapping
 *
 * This maps a supply name to a device. Use of dev_name allows support for
 * buses which make struct device available late such as I2C.
 *
 * @dev_name: Result of dev_name() for the consumer.
 * @supply: Name for the supply.
 */
struct regulator_consumer_supply {
	const char *dev_name;   /* dev_name() for consumer */
	const char *supply;	/* consumer supply - e.g. "vcc" */
};

/* Initialize struct regulator_consumer_supply */
#define REGULATOR_SUPPLY(_name, _dev_name)			\
{								\
	.supply		= _name,				\
	.dev_name	= _dev_name,				\
}

/**
 * struct regulator_init_data - regulator platform initialisation data.
 *
 * Initialisation constraints, our supply and consumers supplies.
 *
 * @supply_regulator: Parent regulator.  Specified using the regulator name
 *                    as it appears in the name field in sysfs, which can
 *                    be explicitly set using the constraints field 'name'.
 *
 * @constraints: Constraints.  These must be specified for the regulator to
 *               be usable.
 * @num_consumer_supplies: Number of consumer device supplies.
 * @consumer_supplies: Consumer device supply configuration.
 * @driver_data: Data passed to regulator_init.
 */
struct regulator_init_data {
	const char *supply_regulator;        /* or NULL for system supply */

	struct regulation_constraints constraints;

	int num_consumer_supplies;
	struct regulator_consumer_supply *consumer_supplies;

	/* optional regulator machine specific data */
	void *driver_data;	/* core does not touch this */
};

#ifdef CONFIG_REGULATOR
void regulator_has_full_constraints(void);
#else
static inline void regulator_has_full_constraints(void)
{
}
#endif

static inline int regulator_suspend_prepare(suspend_state_t state)
{
	return 0;
}
static inline int regulator_suspend_finish(void)
{
	return 0;
}

#endif
