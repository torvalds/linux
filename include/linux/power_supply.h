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
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/types.h>

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
	POWER_SUPPLY_CHARGE_TYPE_TAPER,
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
	POWER_SUPPLY_HEALTH_WARM,
	POWER_SUPPLY_HEALTH_COOL,
	POWER_SUPPLY_HEALTH_HOT,
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

enum {
	POWER_SUPPLY_DP_DM_UNKNOWN = 0,
	POWER_SUPPLY_DP_DM_PREPARE = 1,
	POWER_SUPPLY_DP_DM_UNPREPARE = 2,
	POWER_SUPPLY_DP_DM_CONFIRMED_HVDCP3 = 3,
	POWER_SUPPLY_DP_DM_DP_PULSE = 4,
	POWER_SUPPLY_DP_DM_DM_PULSE = 5,
	POWER_SUPPLY_DP_DM_DP0P6_DMF = 6,
	POWER_SUPPLY_DP_DM_DP0P6_DM3P3 = 7,
	POWER_SUPPLY_DP_DM_DPF_DMF = 8,
	POWER_SUPPLY_DP_DM_DPR_DMR = 9,
	POWER_SUPPLY_DP_DM_HVDCP3_SUPPORTED = 10,
	POWER_SUPPLY_DP_DM_ICL_DOWN = 11,
	POWER_SUPPLY_DP_DM_ICL_UP = 12,
	POWER_SUPPLY_DP_DM_FORCE_5V = 13,
	POWER_SUPPLY_DP_DM_FORCE_9V = 14,
	POWER_SUPPLY_DP_DM_FORCE_12V = 15,
};

enum {
	POWER_SUPPLY_PL_NONE,
	POWER_SUPPLY_PL_USBIN_USBIN,
	POWER_SUPPLY_PL_USBIN_USBIN_EXT,
	POWER_SUPPLY_PL_USBMID_USBMID,
};

enum {
	POWER_SUPPLY_CHARGER_SEC_NONE = 0,
	POWER_SUPPLY_CHARGER_SEC_CP,
	POWER_SUPPLY_CHARGER_SEC_PL,
	POWER_SUPPLY_CHARGER_SEC_CP_PL,
};

enum {
	POWER_SUPPLY_CP_NONE = 0,
	POWER_SUPPLY_CP_HVDCP3,
	POWER_SUPPLY_CP_PPS,
	POWER_SUPPLY_CP_WIRELESS,
};

enum {
	POWER_SUPPLY_CONNECTOR_TYPEC,
	POWER_SUPPLY_CONNECTOR_MICRO_USB,
};

enum {
	POWER_SUPPLY_PL_STACKED_BATFET,
	POWER_SUPPLY_PL_NON_STACKED_BATFET,
};

enum {
	POWER_SUPPLY_PD_INACTIVE = 0,
	POWER_SUPPLY_PD_ACTIVE,
	POWER_SUPPLY_PD_PPS_ACTIVE,
};

enum {
	POWER_SUPPLY_QC_CTM_DISABLE = BIT(0),
	POWER_SUPPLY_QC_THERMAL_BALANCE_DISABLE = BIT(1),
	POWER_SUPPLY_QC_INOV_THERMAL_DISABLE = BIT(2),
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
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_EMPTY,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_AVG,
	POWER_SUPPLY_PROP_CAPACITY, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN, /* in percents! */
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX, /* in percents! */
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
	/* Local extensions */
	POWER_SUPPLY_PROP_USB_HC,
	POWER_SUPPLY_PROP_USB_OTG,
	POWER_SUPPLY_PROP_CHARGE_ENABLED,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_CHARGE_NOW_RAW,
	POWER_SUPPLY_PROP_CHARGE_NOW_ERROR,
	POWER_SUPPLY_PROP_CAPACITY_RAW,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_STEP_CHARGING_STEP,
	POWER_SUPPLY_PROP_PIN_ENABLED,
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
	POWER_SUPPLY_PROP_VCHG_LOOP_DBC_BYPASS,
	POWER_SUPPLY_PROP_CHARGE_COUNTER_SHADOW,
	POWER_SUPPLY_PROP_HI_POWER,
	POWER_SUPPLY_PROP_LOW_POWER,
	POWER_SUPPLY_PROP_COOL_TEMP,
	POWER_SUPPLY_PROP_WARM_TEMP,
	POWER_SUPPLY_PROP_COLD_TEMP,
	POWER_SUPPLY_PROP_HOT_TEMP,
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL,
	POWER_SUPPLY_PROP_RESISTANCE,
	POWER_SUPPLY_PROP_RESISTANCE_CAPACITIVE,
	POWER_SUPPLY_PROP_RESISTANCE_ID, /* in Ohms */
	POWER_SUPPLY_PROP_RESISTANCE_NOW,
	POWER_SUPPLY_PROP_FLASH_CURRENT_MAX,
	POWER_SUPPLY_PROP_UPDATE_NOW,
	POWER_SUPPLY_PROP_ESR_COUNT,
	POWER_SUPPLY_PROP_BUCK_FREQ,
	POWER_SUPPLY_PROP_BOOST_CURRENT,
	POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_FLASH_ACTIVE,
	POWER_SUPPLY_PROP_FLASH_TRIGGER,
	POWER_SUPPLY_PROP_FORCE_TLIM,
	POWER_SUPPLY_PROP_DP_DM,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
	POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE,
	POWER_SUPPLY_PROP_CURRENT_QNOVO,
	POWER_SUPPLY_PROP_VOLTAGE_QNOVO,
	POWER_SUPPLY_PROP_RERUN_AICL,
	POWER_SUPPLY_PROP_CYCLE_COUNT_ID,
	POWER_SUPPLY_PROP_SAFETY_TIMER_EXPIRED,
	POWER_SUPPLY_PROP_RESTRICTED_CHARGING,
	POWER_SUPPLY_PROP_CURRENT_CAPABILITY,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION, /* 0: N/C, 1: CC1, 2: CC2 */
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_TYPEC_SRC_RP,
	POWER_SUPPLY_PROP_PD_ALLOWED,
	POWER_SUPPLY_PROP_PD_ACTIVE,
	POWER_SUPPLY_PROP_PD_IN_HARD_RESET,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED,
	POWER_SUPPLY_PROP_CHARGER_TEMP,
	POWER_SUPPLY_PROP_CHARGER_TEMP_MAX,
	POWER_SUPPLY_PROP_PARALLEL_DISABLE,
	POWER_SUPPLY_PROP_PE_START,
	POWER_SUPPLY_PROP_SOC_REPORTING_READY,
	POWER_SUPPLY_PROP_DEBUG_BATTERY,
	POWER_SUPPLY_PROP_FCC_DELTA,
	POWER_SUPPLY_PROP_ICL_REDUCTION,
	POWER_SUPPLY_PROP_PARALLEL_MODE,
	POWER_SUPPLY_PROP_DIE_HEALTH,
	POWER_SUPPLY_PROP_CONNECTOR_HEALTH,
	POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
	POWER_SUPPLY_PROP_HW_CURRENT_MAX,
	POWER_SUPPLY_PROP_PR_SWAP,
	POWER_SUPPLY_PROP_CC_STEP,
	POWER_SUPPLY_PROP_CC_STEP_SEL,
	POWER_SUPPLY_PROP_SW_JEITA_ENABLED,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_SDP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONNECTOR_TYPE,
	POWER_SUPPLY_PROP_PARALLEL_BATFET_MODE,
	POWER_SUPPLY_PROP_PARALLEL_FCC_MAX,
	POWER_SUPPLY_PROP_MIN_ICL,
	POWER_SUPPLY_PROP_MOISTURE_DETECTED,
	POWER_SUPPLY_PROP_BATT_PROFILE_VERSION,
	POWER_SUPPLY_PROP_BATT_FULL_CURRENT,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED,
	POWER_SUPPLY_PROP_SMB_EN_MODE,
	POWER_SUPPLY_PROP_SMB_EN_REASON,
	POWER_SUPPLY_PROP_ESR_ACTUAL,
	POWER_SUPPLY_PROP_ESR_NOMINAL,
	POWER_SUPPLY_PROP_SOH,
	POWER_SUPPLY_PROP_CLEAR_SOH,
	POWER_SUPPLY_PROP_FORCE_RECHARGE,
	POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE,
	POWER_SUPPLY_PROP_TOGGLE_STAT,
	POWER_SUPPLY_PROP_MAIN_FCC_MAX,
	POWER_SUPPLY_PROP_FG_RESET,
	POWER_SUPPLY_PROP_QC_OPTI_DISABLE,
	POWER_SUPPLY_PROP_CC_SOC,
	POWER_SUPPLY_PROP_BATT_AGE_LEVEL,
	POWER_SUPPLY_PROP_SCALE_MODE_EN,
	POWER_SUPPLY_PROP_VOLTAGE_VPH,
	/* Charge pump properties */
	POWER_SUPPLY_PROP_CP_STATUS1,
	POWER_SUPPLY_PROP_CP_STATUS2,
	POWER_SUPPLY_PROP_CP_ENABLE,
	POWER_SUPPLY_PROP_CP_SWITCHER_EN,
	POWER_SUPPLY_PROP_CP_DIE_TEMP,
	POWER_SUPPLY_PROP_CP_ISNS,
	POWER_SUPPLY_PROP_CP_TOGGLE_SWITCHER,
	POWER_SUPPLY_PROP_CP_IRQ_STATUS,
	POWER_SUPPLY_PROP_CP_ILIM,
	/* Local extensions of type int64_t */
	POWER_SUPPLY_PROP_CHARGE_COUNTER_EXT,
	/* Properties of type `const char *' */
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_BATTERY_TYPE,
	POWER_SUPPLY_PROP_CYCLE_COUNTS,
	/*
	 * Add local extensions for properties with string values between
	 * MODEL_NAME and SERIAL_NUMBER. Don't add below SERIAL_NUMBER.
	 */
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
	POWER_SUPPLY_TYPE_USB_HVDCP,		/* High Voltage DCP */
	POWER_SUPPLY_TYPE_USB_HVDCP_3,		/* Efficient High Voltage DCP */
	POWER_SUPPLY_TYPE_WIRELESS,		/* Accessory Charger Adapters */
	POWER_SUPPLY_TYPE_USB_FLOAT,		/* Floating charger */
	POWER_SUPPLY_TYPE_BMS,			/* Battery Monitor System */
	POWER_SUPPLY_TYPE_PARALLEL,		/* Parallel Path */
	POWER_SUPPLY_TYPE_MAIN,			/* Main Path */
	POWER_SUPPLY_TYPE_WIPOWER,		/* Wipower */
	POWER_SUPPLY_TYPE_UFP,			/* Type-C UFP */
	POWER_SUPPLY_TYPE_DFP,			/* Type-C DFP */
	POWER_SUPPLY_TYPE_CHARGE_PUMP,		/* Charge Pump */
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

/* Indicates USB Type-C CC connection status */
enum power_supply_typec_mode {
	POWER_SUPPLY_TYPEC_NONE,

	/* Acting as source */
	POWER_SUPPLY_TYPEC_SINK,		/* Rd only */
	POWER_SUPPLY_TYPEC_SINK_POWERED_CABLE,	/* Rd/Ra */
	POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY,/* Rd/Rd */
	POWER_SUPPLY_TYPEC_SINK_AUDIO_ADAPTER,	/* Ra/Ra */
	POWER_SUPPLY_TYPEC_POWERED_CABLE_ONLY,	/* Ra only */

	/* Acting as sink */
	POWER_SUPPLY_TYPEC_SOURCE_DEFAULT,	/* Rp default */
	POWER_SUPPLY_TYPEC_SOURCE_MEDIUM,	/* Rp 1.5A */
	POWER_SUPPLY_TYPEC_SOURCE_HIGH,		/* Rp 3A */
	POWER_SUPPLY_TYPEC_NON_COMPLIANT,
};

enum power_supply_typec_src_rp {
	POWER_SUPPLY_TYPEC_SRC_RP_STD,
	POWER_SUPPLY_TYPEC_SRC_RP_1P5A,
	POWER_SUPPLY_TYPEC_SRC_RP_3A
};

enum power_supply_typec_power_role {
	POWER_SUPPLY_TYPEC_PR_NONE,		/* CC lines in high-Z */
	POWER_SUPPLY_TYPEC_PR_DUAL,
	POWER_SUPPLY_TYPEC_PR_SINK,
	POWER_SUPPLY_TYPEC_PR_SOURCE,
};

enum power_supply_notifier_events {
	PSY_EVENT_PROP_CHANGED,
};

union power_supply_propval {
	int intval;
	const char *strval;
	int64_t int64val;
};

struct device_node;
struct power_supply;

/* Run-time specific power supply configuration */
struct power_supply_config {
	struct device_node *of_node;
	struct fwnode_handle *fwnode;

	/* Driver private data */
	void *drv_data;

	char **supplied_to;
	size_t num_supplicants;
};

/* Description of power supply */
struct power_supply_desc {
	const char *name;
	enum power_supply_type type;
	enum power_supply_usb_type *usb_types;
	size_t num_usb_types;
	enum power_supply_property *properties;
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

/*
 * This is the recommended struct to manage static battery parameters,
 * populated by power_supply_get_battery_info(). Most platform drivers should
 * use these for consistency.
 * Its field names must correspond to elements in enum power_supply_property.
 * The default field value is -EINVAL.
 * Power supply class itself doesn't use this.
 */

struct power_supply_battery_info {
	int energy_full_design_uwh;	    /* microWatt-hours */
	int charge_full_design_uah;	    /* microAmp-hours */
	int voltage_min_design_uv;	    /* microVolts */
	int precharge_current_ua;	    /* microAmps */
	int charge_term_current_ua;	    /* microAmps */
	int constant_charge_current_max_ua; /* microAmps */
	int constant_charge_voltage_max_uv; /* microVolts */
};

extern struct atomic_notifier_head power_supply_notifier;
extern int power_supply_reg_notifier(struct notifier_block *nb);
extern void power_supply_unreg_notifier(struct notifier_block *nb);
extern struct power_supply *power_supply_get_by_name(const char *name);
extern void power_supply_put(struct power_supply *psy);
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
					 struct power_supply_battery_info *info);
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
extern int power_supply_set_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    const union power_supply_propval *val);
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
		return 1;
	default:
		break;
	}

	return 0;
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
		return 1;
	default:
		break;
	}

	return 0;
}

#endif /* __LINUX_POWER_SUPPLY_H__ */
