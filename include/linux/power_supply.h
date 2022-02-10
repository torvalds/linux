/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Universal power supply monitor class
 *
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 */

#ifndef __LINUX_POWER_SUPPLY_H__
#define __LINUX_POWER_SUPPLY_H__

#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>

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

/* What algorithm is the charger using? */
enum {
	POWER_SUPPLY_CHARGE_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_CHARGE_TYPE_NONE,
	POWER_SUPPLY_CHARGE_TYPE_TRICKLE,	/* slow speed */
	POWER_SUPPLY_CHARGE_TYPE_FAST,		/* fast speed */
	POWER_SUPPLY_CHARGE_TYPE_STANDARD,	/* normal speed */
	POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE,	/* dynamically adjusted speed */
	POWER_SUPPLY_CHARGE_TYPE_CUSTOM,	/* use CHARGE_CONTROL_* props */
	POWER_SUPPLY_CHARGE_TYPE_LONGLIFE,	/* slow speed, longer life */
};

enum {
	POWER_SUPPLY_HEALTH_UNKNOWN = 0,
	POWER_SUPPLY_HEALTH_GOOD,
	POWER_SUPPLY_HEALTH_OVERHEAT,
	POWER_SUPPLY_HEALTH_DEAD,
	POWER_SUPPLY_HEALTH_OVERVOLTAGE,
	POWER_SUPPLY_HEALTH_UNSPEC_FAILURE,
	POWER_SUPPLY_HEALTH_COLD,
	POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_OVERCURRENT,
	POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED,
	POWER_SUPPLY_HEALTH_WARM,
	POWER_SUPPLY_HEALTH_COOL,
	POWER_SUPPLY_HEALTH_HOT,
	POWER_SUPPLY_HEALTH_NO_BATTERY,
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

enum {
	POWER_SUPPLY_SCOPE_UNKNOWN = 0,
	POWER_SUPPLY_SCOPE_SYSTEM,
	POWER_SUPPLY_SCOPE_DEVICE,
};

enum power_supply_property {
	/* Properties of type `int' */
	POWER_SUPPLY_PROP_STATUS = 0,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_AUTHENTIC,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_BOOT,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_BOOT,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD, /* in percents! */
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD, /* in percents! */
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_INPUT_POWER_LIMIT,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_EMPTY,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_AVG,
	POWER_SUPPLY_PROP_CAPACITY, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_MAX,
	POWER_SUPPLY_PROP_TEMP_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TYPE, /* use power_supply.type instead */
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CALIBRATE,
	POWER_SUPPLY_PROP_MANUFACTURE_YEAR,
	POWER_SUPPLY_PROP_MANUFACTURE_MONTH,
	POWER_SUPPLY_PROP_MANUFACTURE_DAY,
	/* Properties of type `const char *' */
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

enum power_supply_type {
	POWER_SUPPLY_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_TYPE_BATTERY,
	POWER_SUPPLY_TYPE_UPS,
	POWER_SUPPLY_TYPE_MAINS,
	POWER_SUPPLY_TYPE_USB,			/* Standard Downstream Port */
	POWER_SUPPLY_TYPE_USB_DCP,		/* Dedicated Charging Port */
	POWER_SUPPLY_TYPE_USB_CDP,		/* Charging Downstream Port */
	POWER_SUPPLY_TYPE_USB_ACA,		/* Accessory Charger Adapters */
	POWER_SUPPLY_TYPE_USB_TYPE_C,		/* Type C Port */
	POWER_SUPPLY_TYPE_USB_PD,		/* Power Delivery Port */
	POWER_SUPPLY_TYPE_USB_PD_DRP,		/* PD Dual Role Port */
	POWER_SUPPLY_TYPE_APPLE_BRICK_ID,	/* Apple Charging Method */
	POWER_SUPPLY_TYPE_WIRELESS,		/* Wireless */
};

enum power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_USB_TYPE_SDP,		/* Standard Downstream Port */
	POWER_SUPPLY_USB_TYPE_DCP,		/* Dedicated Charging Port */
	POWER_SUPPLY_USB_TYPE_CDP,		/* Charging Downstream Port */
	POWER_SUPPLY_USB_TYPE_ACA,		/* Accessory Charger Adapters */
	POWER_SUPPLY_USB_TYPE_C,		/* Type C Port */
	POWER_SUPPLY_USB_TYPE_PD,		/* Power Delivery Port */
	POWER_SUPPLY_USB_TYPE_PD_DRP,		/* PD Dual Role Port */
	POWER_SUPPLY_USB_TYPE_PD_PPS,		/* PD Programmable Power Supply */
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,	/* Apple Charging Method */
};

enum power_supply_charge_behaviour {
	POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO = 0,
	POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE,
	POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE,
};

enum power_supply_notifier_events {
	PSY_EVENT_PROP_CHANGED,
};

union power_supply_propval {
	int intval;
	const char *strval;
};

struct device_node;
struct power_supply;

/* Run-time specific power supply configuration */
struct power_supply_config {
	struct device_node *of_node;
	struct fwnode_handle *fwnode;

	/* Driver private data */
	void *drv_data;

	/* Device specific sysfs attributes */
	const struct attribute_group **attr_grp;

	char **supplied_to;
	size_t num_supplicants;
};

/* Description of power supply */
struct power_supply_desc {
	const char *name;
	enum power_supply_type type;
	const enum power_supply_usb_type *usb_types;
	size_t num_usb_types;
	const enum power_supply_property *properties;
	size_t num_properties;

	/*
	 * Functions for drivers implementing power supply class.
	 * These shouldn't be called directly by other drivers for accessing
	 * this power supply. Instead use power_supply_*() functions (for
	 * example power_supply_get_property()).
	 */
	int (*get_property)(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val);
	int (*set_property)(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val);
	/*
	 * property_is_writeable() will be called during registration
	 * of power supply. If this happens during device probe then it must
	 * not access internal data of device (because probe did not end).
	 */
	int (*property_is_writeable)(struct power_supply *psy,
				     enum power_supply_property psp);
	void (*external_power_changed)(struct power_supply *psy);
	void (*set_charged)(struct power_supply *psy);

	/*
	 * Set if thermal zone should not be created for this power supply.
	 * For example for virtual supplies forwarding calls to actual
	 * sensors or other supplies.
	 */
	bool no_thermal;
	/* For APM emulation, think legacy userspace. */
	int use_for_apm;
};

struct power_supply {
	const struct power_supply_desc *desc;

	char **supplied_to;
	size_t num_supplicants;

	char **supplied_from;
	size_t num_supplies;
	struct device_node *of_node;

	/* Driver private data */
	void *drv_data;

	/* private */
	struct device dev;
	struct work_struct changed_work;
	struct delayed_work deferred_register_work;
	spinlock_t changed_lock;
	bool changed;
	bool initialized;
	bool removing;
	atomic_t use_cnt;
#ifdef CONFIG_THERMAL
	struct thermal_zone_device *tzd;
	struct thermal_cooling_device *tcd;
#endif

#ifdef CONFIG_LEDS_TRIGGERS
	struct led_trigger *charging_full_trig;
	char *charging_full_trig_name;
	struct led_trigger *charging_trig;
	char *charging_trig_name;
	struct led_trigger *full_trig;
	char *full_trig_name;
	struct led_trigger *online_trig;
	char *online_trig_name;
	struct led_trigger *charging_blink_full_solid_trig;
	char *charging_blink_full_solid_trig_name;
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

struct power_supply_battery_ocv_table {
	int ocv;	/* microVolts */
	int capacity;	/* percent */
};

struct power_supply_resistance_temp_table {
	int temp;	/* celsius */
	int resistance;	/* internal resistance percent */
};

#define POWER_SUPPLY_OCV_TEMP_MAX 20

/**
 * struct power_supply_battery_info - information about batteries
 * @technology: from the POWER_SUPPLY_TECHNOLOGY_* enum
 * @energy_full_design_uwh: energy content when fully charged in microwatt
 *   hours
 * @charge_full_design_uah: charge content when fully charged in microampere
 *   hours
 * @voltage_min_design_uv: minimum voltage across the poles when the battery
 *   is at minimum voltage level in microvolts. If the voltage drops below this
 *   level the battery will need precharging when using CC/CV charging.
 * @voltage_max_design_uv: voltage across the poles when the battery is fully
 *   charged in microvolts. This is the "nominal voltage" i.e. the voltage
 *   printed on the label of the battery.
 * @tricklecharge_current_ua: the tricklecharge current used when trickle
 *   charging the battery in microamperes. This is the charging phase when the
 *   battery is completely empty and we need to carefully trickle in some
 *   charge until we reach the precharging voltage.
 * @precharge_current_ua: current to use in the precharge phase in microamperes,
 *   the precharge rate is limited by limiting the current to this value.
 * @precharge_voltage_max_uv: the maximum voltage allowed when precharging in
 *   microvolts. When we pass this voltage we will nominally switch over to the
 *   CC (constant current) charging phase defined by constant_charge_current_ua
 *   and constant_charge_voltage_max_uv.
 * @charge_term_current_ua: when the current in the CV (constant voltage)
 *   charging phase drops below this value in microamperes the charging will
 *   terminate completely and not restart until the voltage over the battery
 *   poles reach charge_restart_voltage_uv unless we use maintenance charging.
 * @charge_restart_voltage_uv: when the battery has been fully charged by
 *   CC/CV charging and charging has been disabled, and the voltage subsequently
 *   drops below this value in microvolts, the charging will be restarted
 *   (typically using CV charging).
 * @overvoltage_limit_uv: If the voltage exceeds the nominal voltage
 *   voltage_max_design_uv and we reach this voltage level, all charging must
 *   stop and emergency procedures take place, such as shutting down the system
 *   in some cases.
 * @constant_charge_current_max_ua: current in microamperes to use in the CC
 *   (constant current) charging phase. The charging rate is limited
 *   by this current. This is the main charging phase and as the current is
 *   constant into the battery the voltage slowly ascends to
 *   constant_charge_voltage_max_uv.
 * @constant_charge_voltage_max_uv: voltage in microvolts signifying the end of
 *   the CC (constant current) charging phase and the beginning of the CV
 *   (constant voltage) charging phase.
 * @factory_internal_resistance_uohm: the internal resistance of the battery
 *   at fabrication time, expressed in microohms. This resistance will vary
 *   depending on the lifetime and charge of the battery, so this is just a
 *   nominal ballpark figure.
 * @ocv_temp: array indicating the open circuit voltage (OCV) capacity
 *   temperature indices. This is an array of temperatures in degrees Celsius
 *   indicating which capacity table to use for a certain temperature, since
 *   the capacity for reasons of chemistry will be different at different
 *   temperatures. Determining capacity is a multivariate problem and the
 *   temperature is the first variable we determine.
 * @temp_ambient_alert_min: the battery will go outside of operating conditions
 *   when the ambient temperature goes below this temperature in degrees
 *   Celsius.
 * @temp_ambient_alert_max: the battery will go outside of operating conditions
 *   when the ambient temperature goes above this temperature in degrees
 *   Celsius.
 * @temp_alert_min: the battery should issue an alert if the internal
 *   temperature goes below this temperature in degrees Celsius.
 * @temp_alert_max: the battery should issue an alert if the internal
 *   temperature goes above this temperature in degrees Celsius.
 * @temp_min: the battery will go outside of operating conditions when
 *   the internal temperature goes below this temperature in degrees Celsius.
 *   Normally this means the system should shut down.
 * @temp_max: the battery will go outside of operating conditions when
 *   the internal temperature goes above this temperature in degrees Celsius.
 *   Normally this means the system should shut down.
 * @ocv_table: for each entry in ocv_temp there is a corresponding entry in
 *   ocv_table and a size for each entry in ocv_table_size. These arrays
 *   determine the capacity in percent in relation to the voltage in microvolts
 *   at the indexed temperature.
 * @ocv_table_size: for each entry in ocv_temp this array is giving the size of
 *   each entry in the array of capacity arrays in ocv_table.
 * @resist_table: this is a table that correlates a battery temperature to the
 *   expected internal resistance at this temperature. The resistance is given
 *   as a percentage of factory_internal_resistance_uohm. Knowing the
 *   resistance of the battery is usually necessary for calculating the open
 *   circuit voltage (OCV) that is then used with the ocv_table to calculate
 *   the capacity of the battery. The resist_table must be ordered descending
 *   by temperature: highest temperature with lowest resistance first, lowest
 *   temperature with highest resistance last.
 * @resist_table_size: the number of items in the resist_table.
 *
 * This is the recommended struct to manage static battery parameters,
 * populated by power_supply_get_battery_info(). Most platform drivers should
 * use these for consistency.
 *
 * Its field names must correspond to elements in enum power_supply_property.
 * The default field value is -EINVAL.
 *
 * The charging parameters here assume a CC/CV charging scheme. This method
 * is most common with Lithium Ion batteries (other methods are possible) and
 * looks as follows:
 *
 * ^ Battery voltage
 * |                                               --- overvoltage_limit_uv
 * |
 * |                    ...................................................
 * |                 .. constant_charge_voltage_max_uv
 * |              ..
 * |             .
 * |            .
 * |           .
 * |          .
 * |         .
 * |     .. precharge_voltage_max_uv
 * |  ..
 * |. (trickle charging)
 * +------------------------------------------------------------------> time
 *
 * ^ Current into the battery
 * |
 * |      ............. constant_charge_current_max_ua
 * |      .            .
 * |      .             .
 * |      .              .
 * |      .               .
 * |      .                ..
 * |      .                  ....
 * |      .                       .....
 * |    ... precharge_current_ua       .......  charge_term_current_ua
 * |    .                                    .
 * |    .                                    .
 * |.... tricklecharge_current_ua            .
 * |                                         .
 * +-----------------------------------------------------------------> time
 *
 * These diagrams are synchronized on time and the voltage and current
 * follow each other.
 *
 * With CC/CV charging commence over time like this for an empty battery:
 *
 * 1. When the battery is completely empty it may need to be charged with
 *    an especially small current so that electrons just "trickle in",
 *    this is the tricklecharge_current_ua.
 *
 * 2. Next a small initial pre-charge current (precharge_current_ua)
 *    is applied if the voltage is below precharge_voltage_max_uv until we
 *    reach precharge_voltage_max_uv. CAUTION: in some texts this is referred
 *    to as "trickle charging" but the use in the Linux kernel is different
 *    see below!
 *
 * 3. Then the main charging current is applied, which is called the constant
 *    current (CC) phase. A current regulator is set up to allow
 *    constant_charge_current_max_ua of current to flow into the battery.
 *    The chemical reaction in the battery will make the voltage go up as
 *    charge goes into the battery. This current is applied until we reach
 *    the constant_charge_voltage_max_uv voltage.
 *
 * 4. At this voltage we switch over to the constant voltage (CV) phase. This
 *    means we allow current to go into the battery, but we keep the voltage
 *    fixed. This current will continue to charge the battery while keeping
 *    the voltage the same. A chemical reaction in the battery goes on
 *    storing energy without affecting the voltage. Over time the current
 *    will slowly drop and when we reach charge_term_current_ua we will
 *    end the constant voltage phase.
 *
 * After this the battery is fully charged, and if we do not support maintenance
 * charging, the charging will not restart until power dissipation makes the
 * voltage fall so that we reach charge_restart_voltage_uv and at this point
 * we restart charging at the appropriate phase, usually this will be inside
 * the CV phase.
 *
 * If we support maintenance charging the voltage is however kept high after
 * the CV phase with a very low current. This is meant to let the same charge
 * go in for usage while the charger is still connected, mainly for
 * dissipation for the power consuming entity while connected to the
 * charger.
 *
 * All charging MUST terminate if the overvoltage_limit_uv is ever reached.
 * Overcharging Lithium Ion cells can be DANGEROUS and lead to fire or
 * explosions.
 *
 * The power supply class itself doesn't use this struct as of now.
 */

struct power_supply_battery_info {
	unsigned int technology;
	int energy_full_design_uwh;
	int charge_full_design_uah;
	int voltage_min_design_uv;
	int voltage_max_design_uv;
	int tricklecharge_current_ua;
	int precharge_current_ua;
	int precharge_voltage_max_uv;
	int charge_term_current_ua;
	int charge_restart_voltage_uv;
	int overvoltage_limit_uv;
	int constant_charge_current_max_ua;
	int constant_charge_voltage_max_uv;
	int factory_internal_resistance_uohm;
	int ocv_temp[POWER_SUPPLY_OCV_TEMP_MAX];
	int temp_ambient_alert_min;
	int temp_ambient_alert_max;
	int temp_alert_min;
	int temp_alert_max;
	int temp_min;
	int temp_max;
	struct power_supply_battery_ocv_table *ocv_table[POWER_SUPPLY_OCV_TEMP_MAX];
	int ocv_table_size[POWER_SUPPLY_OCV_TEMP_MAX];
	struct power_supply_resistance_temp_table *resist_table;
	int resist_table_size;
};

extern struct atomic_notifier_head power_supply_notifier;
extern int power_supply_reg_notifier(struct notifier_block *nb);
extern void power_supply_unreg_notifier(struct notifier_block *nb);
#if IS_ENABLED(CONFIG_POWER_SUPPLY)
extern struct power_supply *power_supply_get_by_name(const char *name);
extern void power_supply_put(struct power_supply *psy);
#else
static inline void power_supply_put(struct power_supply *psy) {}
static inline struct power_supply *power_supply_get_by_name(const char *name)
{ return NULL; }
#endif
#ifdef CONFIG_OF
extern struct power_supply *power_supply_get_by_phandle(struct device_node *np,
							const char *property);
extern struct power_supply *devm_power_supply_get_by_phandle(
				    struct device *dev, const char *property);
#else /* !CONFIG_OF */
static inline struct power_supply *
power_supply_get_by_phandle(struct device_node *np, const char *property)
{ return NULL; }
static inline struct power_supply *
devm_power_supply_get_by_phandle(struct device *dev, const char *property)
{ return NULL; }
#endif /* CONFIG_OF */

extern int power_supply_get_battery_info(struct power_supply *psy,
					 struct power_supply_battery_info **info_out);
extern void power_supply_put_battery_info(struct power_supply *psy,
					  struct power_supply_battery_info *info);
extern int power_supply_ocv2cap_simple(struct power_supply_battery_ocv_table *table,
				       int table_len, int ocv);
extern struct power_supply_battery_ocv_table *
power_supply_find_ocv2cap_table(struct power_supply_battery_info *info,
				int temp, int *table_len);
extern int power_supply_batinfo_ocv2cap(struct power_supply_battery_info *info,
					int ocv, int temp);
extern int
power_supply_temp2resist_simple(struct power_supply_resistance_temp_table *table,
				int table_len, int temp);
extern void power_supply_changed(struct power_supply *psy);
extern int power_supply_am_i_supplied(struct power_supply *psy);
extern int power_supply_set_input_current_limit_from_supplier(
					 struct power_supply *psy);
extern int power_supply_set_battery_charged(struct power_supply *psy);

#ifdef CONFIG_POWER_SUPPLY
extern int power_supply_is_system_supplied(void);
#else
static inline int power_supply_is_system_supplied(void) { return -ENOSYS; }
#endif

extern int power_supply_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val);
#if IS_ENABLED(CONFIG_POWER_SUPPLY)
extern int power_supply_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val);
#else
static inline int power_supply_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val)
{ return 0; }
#endif
extern int power_supply_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp);
extern void power_supply_external_power_changed(struct power_supply *psy);

extern struct power_supply *__must_check
power_supply_register(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct power_supply *__must_check
power_supply_register_no_ws(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct power_supply *__must_check
devm_power_supply_register(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern struct power_supply *__must_check
devm_power_supply_register_no_ws(struct device *parent,
				 const struct power_supply_desc *desc,
				 const struct power_supply_config *cfg);
extern void power_supply_unregister(struct power_supply *psy);
extern int power_supply_powers(struct power_supply *psy, struct device *dev);

#define to_power_supply(device) container_of(device, struct power_supply, dev)

extern void *power_supply_get_drvdata(struct power_supply *psy);
/* For APM emulation, think legacy userspace. */
extern struct class *power_supply_class;

static inline bool power_supply_is_amp_property(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
	case POWER_SUPPLY_PROP_CHARGE_NOW:
	case POWER_SUPPLY_PROP_CHARGE_AVG:
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_PRECHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
	case POWER_SUPPLY_PROP_CURRENT_BOOT:
		return true;
	default:
		break;
	}

	return false;
}

static inline bool power_supply_is_watt_property(enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN:
	case POWER_SUPPLY_PROP_ENERGY_FULL:
	case POWER_SUPPLY_PROP_ENERGY_EMPTY:
	case POWER_SUPPLY_PROP_ENERGY_NOW:
	case POWER_SUPPLY_PROP_ENERGY_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
	case POWER_SUPPLY_PROP_VOLTAGE_BOOT:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_POWER_NOW:
		return true;
	default:
		break;
	}

	return false;
}

#ifdef CONFIG_POWER_SUPPLY_HWMON
int power_supply_add_hwmon_sysfs(struct power_supply *psy);
void power_supply_remove_hwmon_sysfs(struct power_supply *psy);
#else
static inline int power_supply_add_hwmon_sysfs(struct power_supply *psy)
{
	return 0;
}

static inline
void power_supply_remove_hwmon_sysfs(struct power_supply *psy) {}
#endif

#ifdef CONFIG_SYSFS
ssize_t power_supply_charge_behaviour_show(struct device *dev,
					   unsigned int available_behaviours,
					   enum power_supply_charge_behaviour behaviour,
					   char *buf);

int power_supply_charge_behaviour_parse(unsigned int available_behaviours, const char *buf);
#else
static inline
ssize_t power_supply_charge_behaviour_show(struct device *dev,
					   unsigned int available_behaviours,
					   enum power_supply_charge_behaviour behaviour,
					   char *buf)
{
	return -EOPNOTSUPP;
}

static inline int power_supply_charge_behaviour_parse(unsigned int available_behaviours,
						      const char *buf)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* __LINUX_POWER_SUPPLY_H__ */
