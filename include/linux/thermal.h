/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  thermal.h  ($Revision: 0 $)
 *
 *  Copyright (C) 2008  Intel Corp
 *  Copyright (C) 2008  Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008  Sujith Thomas <sujith.thomas@intel.com>
 */

#ifndef __THERMAL_H__
#define __THERMAL_H__

#include <linux/of.h>
#include <linux/idr.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <uapi/linux/thermal.h>

/* invalid cooling state */
#define THERMAL_CSTATE_INVALID -1UL

/* No upper/lower limit requirement */
#define THERMAL_NO_LIMIT	((u32)~0)

/* Default weight of a bound cooling device */
#define THERMAL_WEIGHT_DEFAULT 0

/* use value, which < 0K, to indicate an invalid/uninitialized temperature */
#define THERMAL_TEMP_INVALID	-274000

struct thermal_zone_device;
struct thermal_cooling_device;
struct thermal_instance;
struct thermal_debugfs;
struct thermal_attr;

enum thermal_trend {
	THERMAL_TREND_STABLE, /* temperature is stable */
	THERMAL_TREND_RAISING, /* temperature is raising */
	THERMAL_TREND_DROPPING, /* temperature is dropping */
};

/* Thermal notification reason */
enum thermal_notify_event {
	THERMAL_EVENT_UNSPECIFIED, /* Unspecified event */
	THERMAL_EVENT_TEMP_SAMPLE, /* New Temperature sample */
	THERMAL_TRIP_VIOLATED, /* TRIP Point violation */
	THERMAL_TRIP_CHANGED, /* TRIP Point temperature changed */
	THERMAL_DEVICE_DOWN, /* Thermal device is down */
	THERMAL_DEVICE_UP, /* Thermal device is up after a down event */
	THERMAL_DEVICE_POWER_CAPABILITY_CHANGED, /* power capability changed */
	THERMAL_TABLE_CHANGED, /* Thermal table(s) changed */
	THERMAL_EVENT_KEEP_ALIVE, /* Request for user space handler to respond */
	THERMAL_TZ_BIND_CDEV, /* Cooling dev is bind to the thermal zone */
	THERMAL_TZ_UNBIND_CDEV, /* Cooling dev is unbind from the thermal zone */
	THERMAL_INSTANCE_WEIGHT_CHANGED, /* Thermal instance weight changed */
	THERMAL_TZ_RESUME, /* Thermal zone is resuming after system sleep */
};

/**
 * struct thermal_trip - representation of a point in temperature domain
 * @temperature: temperature value in miliCelsius
 * @hysteresis: relative hysteresis in miliCelsius
 * @type: trip point type
 * @priv: pointer to driver data associated with this trip
 * @flags: flags representing binary properties of the trip
 */
struct thermal_trip {
	int temperature;
	int hysteresis;
	enum thermal_trip_type type;
	u8 flags;
	void *priv;
};

#define THERMAL_TRIP_FLAG_RW_TEMP	BIT(0)
#define THERMAL_TRIP_FLAG_RW_HYST	BIT(1)

#define THERMAL_TRIP_FLAG_RW	(THERMAL_TRIP_FLAG_RW_TEMP | \
				 THERMAL_TRIP_FLAG_RW_HYST)

#define THERMAL_TRIP_PRIV_TO_INT(_val_)	(uintptr_t)(_val_)
#define THERMAL_INT_TO_TRIP_PRIV(_val_)	(void *)(uintptr_t)(_val_)

struct thermal_zone_device;

struct cooling_spec {
	unsigned long upper;	/* Highest cooling state  */
	unsigned long lower;	/* Lowest cooling state  */
	unsigned int weight;	/* Cooling device weight */
};

struct thermal_zone_device_ops {
	bool (*should_bind) (struct thermal_zone_device *,
			     const struct thermal_trip *,
			     struct thermal_cooling_device *,
			     struct cooling_spec *);
	int (*get_temp) (struct thermal_zone_device *, int *);
	int (*set_trips) (struct thermal_zone_device *, int, int);
	int (*change_mode) (struct thermal_zone_device *,
		enum thermal_device_mode);
	int (*set_trip_temp) (struct thermal_zone_device *,
			      const struct thermal_trip *, int);
	int (*get_crit_temp) (struct thermal_zone_device *, int *);
	int (*set_emul_temp) (struct thermal_zone_device *, int);
	int (*get_trend) (struct thermal_zone_device *,
			  const struct thermal_trip *, enum thermal_trend *);
	void (*hot)(struct thermal_zone_device *);
	void (*critical)(struct thermal_zone_device *);
};

struct thermal_cooling_device_ops {
	int (*get_max_state) (struct thermal_cooling_device *, unsigned long *);
	int (*get_cur_state) (struct thermal_cooling_device *, unsigned long *);
	int (*set_cur_state) (struct thermal_cooling_device *, unsigned long);
	int (*get_requested_power)(struct thermal_cooling_device *, u32 *);
	int (*state2power)(struct thermal_cooling_device *, unsigned long, u32 *);
	int (*power2state)(struct thermal_cooling_device *, u32, unsigned long *);
};

struct thermal_cooling_device {
	int id;
	const char *type;
	unsigned long max_state;
	struct device device;
	struct device_node *np;
	void *devdata;
	void *stats;
	const struct thermal_cooling_device_ops *ops;
	bool updated; /* true if the cooling device does not need update */
	struct mutex lock; /* protect thermal_instances list */
	struct list_head thermal_instances;
	struct list_head node;
#ifdef CONFIG_THERMAL_DEBUGFS
	struct thermal_debugfs *debugfs;
#endif
};

/* Structure to define Thermal Zone parameters */
struct thermal_zone_params {
	const char *governor_name;

	/*
	 * a boolean to indicate if the thermal to hwmon sysfs interface
	 * is required. when no_hwmon == false, a hwmon sysfs interface
	 * will be created. when no_hwmon == true, nothing will be done
	 */
	bool no_hwmon;

	/*
	 * Sustainable power (heat) that this thermal zone can dissipate in
	 * mW
	 */
	u32 sustainable_power;

	/*
	 * Proportional parameter of the PID controller when
	 * overshooting (i.e., when temperature is below the target)
	 */
	s32 k_po;

	/*
	 * Proportional parameter of the PID controller when
	 * undershooting
	 */
	s32 k_pu;

	/* Integral parameter of the PID controller */
	s32 k_i;

	/* Derivative parameter of the PID controller */
	s32 k_d;

	/* threshold below which the error is no longer accumulated */
	s32 integral_cutoff;

	/*
	 * @slope:	slope of a linear temperature adjustment curve.
	 * 		Used by thermal zone drivers.
	 */
	int slope;
	/*
	 * @offset:	offset of a linear temperature adjustment curve.
	 * 		Used by thermal zone drivers (default 0).
	 */
	int offset;
};

/* Function declarations */
#ifdef CONFIG_THERMAL_OF
struct thermal_zone_device *devm_thermal_of_zone_register(struct device *dev, int id, void *data,
							  const struct thermal_zone_device_ops *ops);

void devm_thermal_of_zone_unregister(struct device *dev, struct thermal_zone_device *tz);

#else

static inline
struct thermal_zone_device *devm_thermal_of_zone_register(struct device *dev, int id, void *data,
							  const struct thermal_zone_device_ops *ops)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline void devm_thermal_of_zone_unregister(struct device *dev,
						   struct thermal_zone_device *tz)
{
}
#endif

int for_each_thermal_trip(struct thermal_zone_device *tz,
			  int (*cb)(struct thermal_trip *, void *),
			  void *data);
int thermal_zone_for_each_trip(struct thermal_zone_device *tz,
			       int (*cb)(struct thermal_trip *, void *),
			       void *data);
void thermal_zone_set_trip_temp(struct thermal_zone_device *tz,
				struct thermal_trip *trip, int temp);

int thermal_zone_get_crit_temp(struct thermal_zone_device *tz, int *temp);

#ifdef CONFIG_THERMAL
struct thermal_zone_device *thermal_zone_device_register_with_trips(
					const char *type,
					const struct thermal_trip *trips,
					int num_trips, void *devdata,
					const struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp,
					unsigned int passive_delay,
					unsigned int polling_delay);

struct thermal_zone_device *thermal_tripless_zone_device_register(
					const char *type,
					void *devdata,
					const struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp);

void thermal_zone_device_unregister(struct thermal_zone_device *tz);

void *thermal_zone_device_priv(struct thermal_zone_device *tzd);
const char *thermal_zone_device_type(struct thermal_zone_device *tzd);
int thermal_zone_device_id(struct thermal_zone_device *tzd);
struct device *thermal_zone_device(struct thermal_zone_device *tzd);

void thermal_zone_device_update(struct thermal_zone_device *,
				enum thermal_notify_event);

struct thermal_cooling_device *thermal_cooling_device_register(const char *,
		void *, const struct thermal_cooling_device_ops *);
struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_node *np, const char *, void *,
				   const struct thermal_cooling_device_ops *);
struct thermal_cooling_device *
devm_thermal_of_cooling_device_register(struct device *dev,
				struct device_node *np,
				const char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops);
void thermal_cooling_device_update(struct thermal_cooling_device *);
void thermal_cooling_device_unregister(struct thermal_cooling_device *);
struct thermal_zone_device *thermal_zone_get_zone_by_name(const char *name);
int thermal_zone_get_temp(struct thermal_zone_device *tz, int *temp);
int thermal_zone_get_slope(struct thermal_zone_device *tz);
int thermal_zone_get_offset(struct thermal_zone_device *tz);
bool thermal_trip_is_bound_to_cdev(struct thermal_zone_device *tz,
				   const struct thermal_trip *trip,
				   struct thermal_cooling_device *cdev);

int thermal_zone_device_enable(struct thermal_zone_device *tz);
int thermal_zone_device_disable(struct thermal_zone_device *tz);
void thermal_zone_device_critical(struct thermal_zone_device *tz);
#else
static inline struct thermal_zone_device *thermal_zone_device_register_with_trips(
					const char *type,
					const struct thermal_trip *trips,
					int num_trips, void *devdata,
					const struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp,
					int passive_delay, int polling_delay)
{ return ERR_PTR(-ENODEV); }

static inline struct thermal_zone_device *thermal_tripless_zone_device_register(
					const char *type,
					void *devdata,
					struct thermal_zone_device_ops *ops,
					const struct thermal_zone_params *tzp)
{ return ERR_PTR(-ENODEV); }

static inline void thermal_zone_device_unregister(struct thermal_zone_device *tz)
{ }

static inline struct thermal_cooling_device *
thermal_cooling_device_register(const char *type, void *devdata,
	const struct thermal_cooling_device_ops *ops)
{ return ERR_PTR(-ENODEV); }
static inline struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_node *np,
	const char *type, void *devdata,
	const struct thermal_cooling_device_ops *ops)
{ return ERR_PTR(-ENODEV); }
static inline struct thermal_cooling_device *
devm_thermal_of_cooling_device_register(struct device *dev,
				struct device_node *np,
				const char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	return ERR_PTR(-ENODEV);
}
static inline void thermal_cooling_device_unregister(
	struct thermal_cooling_device *cdev)
{ }
static inline struct thermal_zone_device *thermal_zone_get_zone_by_name(
		const char *name)
{ return ERR_PTR(-ENODEV); }
static inline int thermal_zone_get_temp(
		struct thermal_zone_device *tz, int *temp)
{ return -ENODEV; }
static inline int thermal_zone_get_slope(
		struct thermal_zone_device *tz)
{ return -ENODEV; }
static inline int thermal_zone_get_offset(
		struct thermal_zone_device *tz)
{ return -ENODEV; }

static inline void *thermal_zone_device_priv(struct thermal_zone_device *tz)
{
	return NULL;
}

static inline const char *thermal_zone_device_type(struct thermal_zone_device *tzd)
{
	return NULL;
}

static inline int thermal_zone_device_id(struct thermal_zone_device *tzd)
{
	return -ENODEV;
}

static inline int thermal_zone_device_enable(struct thermal_zone_device *tz)
{ return -ENODEV; }

static inline int thermal_zone_device_disable(struct thermal_zone_device *tz)
{ return -ENODEV; }
#endif /* CONFIG_THERMAL */

#endif /* __THERMAL_H__ */
