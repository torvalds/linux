/*
 *  Universal power supply monitor class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 *
 *  You may use this code as per GPL version 2
 */

#ifndef __LINUX_POWER_SUPPLY_H__
#define __LINUX_POWER_SUPPLY_H__

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/leds.h>

/*
 * All voltages, currents, charges, energies, time and temperatures in uV,
 * µA, µAh, µWh, seconds and tenths of degree Celsius unless otherwise
 * stated. It's driver's job to convert its raw values to units in which
 * this class operates.
 */

/*
 * For systems where the charger determines the maximum battery capacity
 * the min and max fields should be used to present these values to user
 * space. Unused/unknown fields will not appear in sysfs.
 */

enum {
	POWER_SUPPLY_STATUS_UNKNOWN = 0,
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_DISCHARGING,
	POWER_SUPPLY_STATUS_NOT_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
};

enum {
	POWER_SUPPLY_CHARGE_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_CHARGE_TYPE_NONE,
	POWER_SUPPLY_CHARGE_TYPE_TRICKLE,
	POWER_SUPPLY_CHARGE_TYPE_FAST,
};

enum {
	POWER_SUPPLY_HEALTH_UNKNOWN = 0,
	POWER_SUPPLY_HEALTH_GOOD,
	POWER_SUPPLY_HEALTH_OVERHEAT,
	POWER_SUPPLY_HEALTH_DEAD,
	POWER_SUPPLY_HEALTH_OVERVOLTAGE,
	POWER_SUPPLY_HEALTH_UNSPEC_FAILURE,
	POWER_SUPPLY_HEALTH_COLD,
};

enum {
	POWER_SUPPLY_TECHNOLOGY_UNKNOWN = 0,
	POWER_SUPPLY_TECHNOLOGY_NiMH,
	POWER_SUPPLY_TECHNOLOGY_LION,
	POWER_SUPPLY_TECHNOLOGY_LIPO,
	POWER_SUPPLY_TECHNOLOGY_LiFe,
	POWER_SUPPLY_TECHNOLOGY_NiCd,
	POWER_SUPPLY_TECHNOLOGY_LiMn,
};

enum {
	POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN = 0,
	POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,
	POWER_SUPPLY_CAPACITY_LEVEL_LOW,
	POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,
	POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
	POWER_SUPPLY_CAPACITY_LEVEL_FULL,
};

enum power_supply_property {
	/* Properties of type `int' */
	POWER_SUPPLY_PROP_STATUS = 0,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_EMPTY,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_AVG,
	POWER_SUPPLY_PROP_CAPACITY, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	/* Properties of type `const char *' */
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

enum power_supply_type {
	POWER_SUPPLY_TYPE_BATTERY = 0,
	POWER_SUPPLY_TYPE_UPS,
	POWER_SUPPLY_TYPE_MAINS,
	POWER_SUPPLY_TYPE_USB,
};

union power_supply_propval {
	int intval;
	const char *strval;
};

struct power_supply {
	const char *name;
	enum power_supply_type type;
	enum power_supply_property *properties;
	size_t num_properties;

	char **supplied_to;
	size_t num_supplicants;

	int (*get_property)(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val);
	void (*external_power_changed)(struct power_supply *psy);
	void (*set_charged)(struct power_supply *psy);

	/* For APM emulation, think legacy userspace. */
	int use_for_apm;

	/* private */
	struct device *dev;
	struct work_struct changed_work;

#ifdef CONFIG_LEDS_TRIGGERS
	struct led_trigger *charging_full_trig;
	char *charging_full_trig_name;
	struct led_trigger *charging_trig;
	char *charging_trig_name;
	struct led_trigger *full_trig;
	char *full_trig_name;
	struct led_trigger *online_trig;
	char *online_trig_name;
#endif
};

/*
 * This is recommended structure to specify static power supply parameters.
 * Generic one, parametrizable for different power supplies. Power supply
 * class itself does not use it, but that's what implementing most platform
 * drivers, should try reuse for consistency.
 */

struct power_supply_info {
	const char *name;
	int technology;
	int voltage_max_design;
	int voltage_min_design;
	int charge_full_design;
	int charge_empty_design;
	int energy_full_design;
	int energy_empty_design;
	int use_for_apm;
};

extern struct power_supply *power_supply_get_by_name(char *name);
extern void power_supply_changed(struct power_supply *psy);
extern int power_supply_am_i_supplied(struct power_supply *psy);
extern int power_supply_set_battery_charged(struct power_supply *psy);

#if defined(CONFIG_POWER_SUPPLY) || defined(CONFIG_POWER_SUPPLY_MODULE)
extern int power_supply_is_system_supplied(void);
#else
static inline int power_supply_is_system_supplied(void) { return -ENOSYS; }
#endif

extern int power_supply_register(struct device *parent,
				 struct power_supply *psy);
extern void power_supply_unregister(struct power_supply *psy);

/* For APM emulation, think legacy userspace. */
extern struct class *power_supply_class;

#endif /* __LINUX_POWER_SUPPLY_H__ */
