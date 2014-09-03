/*
 *  thermal.h  ($Revision: 0 $)
 *
 *  Copyright (C) 2008  Intel Corp
 *  Copyright (C) 2008  Zhang Rui <rui.zhang@intel.com>
 *  Copyright (C) 2008  Sujith Thomas <sujith.thomas@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef __THERMAL_H__
#define __THERMAL_H__

#include <linux/of.h>
#include <linux/idr.h>
#include <linux/device.h>
#include <linux/workqueue.h>

#define THERMAL_TRIPS_NONE	-1
#define THERMAL_MAX_TRIPS	12
#define THERMAL_NAME_LENGTH	20

/* invalid cooling state */
#define THERMAL_CSTATE_INVALID -1UL

/* No upper/lower limit requirement */
#define THERMAL_NO_LIMIT	THERMAL_CSTATE_INVALID

/* Unit conversion macros */
#define KELVIN_TO_CELSIUS(t)	(long)(((long)t-2732 >= 0) ?	\
				((long)t-2732+5)/10 : ((long)t-2732-5)/10)
#define CELSIUS_TO_KELVIN(t)	((t)*10+2732)

/* Adding event notification support elements */
#define THERMAL_GENL_FAMILY_NAME                "thermal_event"
#define THERMAL_GENL_VERSION                    0x01
#define THERMAL_GENL_MCAST_GROUP_NAME           "thermal_mc_grp"

/* Default Thermal Governor */
#if defined(CONFIG_THERMAL_DEFAULT_GOV_STEP_WISE)
#define DEFAULT_THERMAL_GOVERNOR       "step_wise"
#elif defined(CONFIG_THERMAL_DEFAULT_GOV_FAIR_SHARE)
#define DEFAULT_THERMAL_GOVERNOR       "fair_share"
#elif defined(CONFIG_THERMAL_DEFAULT_GOV_USER_SPACE)
#define DEFAULT_THERMAL_GOVERNOR       "user_space"
#endif

struct thermal_zone_device;
struct thermal_cooling_device;

enum thermal_device_mode {
	THERMAL_DEVICE_DISABLED = 0,
	THERMAL_DEVICE_ENABLED,
};

enum thermal_trip_type {
	THERMAL_TRIP_ACTIVE = 0,
	THERMAL_TRIP_PASSIVE,
	THERMAL_TRIP_HOT,
	THERMAL_TRIP_CRITICAL,
};

enum thermal_trend {
	THERMAL_TREND_STABLE, /* temperature is stable */
	THERMAL_TREND_RAISING, /* temperature is raising */
	THERMAL_TREND_DROPPING, /* temperature is dropping */
	THERMAL_TREND_RAISE_FULL, /* apply highest cooling action */
	THERMAL_TREND_DROP_FULL, /* apply lowest cooling action */
};

/* Events supported by Thermal Netlink */
enum events {
	THERMAL_AUX0,
	THERMAL_AUX1,
	THERMAL_CRITICAL,
	THERMAL_DEV_FAULT,
};

/* attributes of thermal_genl_family */
enum {
	THERMAL_GENL_ATTR_UNSPEC,
	THERMAL_GENL_ATTR_EVENT,
	__THERMAL_GENL_ATTR_MAX,
};
#define THERMAL_GENL_ATTR_MAX (__THERMAL_GENL_ATTR_MAX - 1)

/* commands supported by the thermal_genl_family */
enum {
	THERMAL_GENL_CMD_UNSPEC,
	THERMAL_GENL_CMD_EVENT,
	__THERMAL_GENL_CMD_MAX,
};
#define THERMAL_GENL_CMD_MAX (__THERMAL_GENL_CMD_MAX - 1)

struct thermal_zone_device_ops {
	int (*bind) (struct thermal_zone_device *,
		     struct thermal_cooling_device *);
	int (*unbind) (struct thermal_zone_device *,
		       struct thermal_cooling_device *);
	int (*get_temp) (struct thermal_zone_device *, unsigned long *);
	int (*get_mode) (struct thermal_zone_device *,
			 enum thermal_device_mode *);
	int (*set_mode) (struct thermal_zone_device *,
		enum thermal_device_mode);
	int (*get_trip_type) (struct thermal_zone_device *, int,
		enum thermal_trip_type *);
	int (*get_trip_temp) (struct thermal_zone_device *, int,
			      unsigned long *);
	int (*set_trip_temp) (struct thermal_zone_device *, int,
			      unsigned long);
	int (*get_trip_hyst) (struct thermal_zone_device *, int,
			      unsigned long *);
	int (*set_trip_hyst) (struct thermal_zone_device *, int,
			      unsigned long);
	int (*get_crit_temp) (struct thermal_zone_device *, unsigned long *);
	int (*set_emul_temp) (struct thermal_zone_device *, unsigned long);
	int (*get_trend) (struct thermal_zone_device *, int,
			  enum thermal_trend *);
	int (*notify) (struct thermal_zone_device *, int,
		       enum thermal_trip_type);
};

struct thermal_cooling_device_ops {
	int (*get_max_state) (struct thermal_cooling_device *, unsigned long *);
	int (*get_cur_state) (struct thermal_cooling_device *, unsigned long *);
	int (*set_cur_state) (struct thermal_cooling_device *, unsigned long);
};

struct thermal_cooling_device {
	int id;
	char type[THERMAL_NAME_LENGTH];
	struct device device;
	struct device_node *np;
	void *devdata;
	const struct thermal_cooling_device_ops *ops;
	bool updated; /* true if the cooling device does not need update */
	struct mutex lock; /* protect thermal_instances list */
	struct list_head thermal_instances;
	struct list_head node;
};

struct thermal_attr {
	struct device_attribute attr;
	char name[THERMAL_NAME_LENGTH];
};

/**
 * struct thermal_zone_device - structure for a thermal zone
 * @id:		unique id number for each thermal zone
 * @type:	the thermal zone device type
 * @device:	&struct device for this thermal zone
 * @trip_temp_attrs:	attributes for trip points for sysfs: trip temperature
 * @trip_type_attrs:	attributes for trip points for sysfs: trip type
 * @trip_hyst_attrs:	attributes for trip points for sysfs: trip hysteresis
 * @devdata:	private pointer for device private data
 * @trips:	number of trip points the thermal zone supports
 * @passive_delay:	number of milliseconds to wait between polls when
 *			performing passive cooling.  Currenty only used by the
 *			step-wise governor
 * @polling_delay:	number of milliseconds to wait between polls when
 *			checking whether trip points have been crossed (0 for
 *			interrupt driven systems)
 * @temperature:	current temperature.  This is only for core code,
 *			drivers should use thermal_zone_get_temp() to get the
 *			current temperature
 * @last_temperature:	previous temperature read
 * @emul_temperature:	emulated temperature when using CONFIG_THERMAL_EMULATION
 * @passive:		1 if you've crossed a passive trip point, 0 otherwise.
 *			Currenty only used by the step-wise governor.
 * @forced_passive:	If > 0, temperature at which to switch on all ACPI
 *			processor cooling devices.  Currently only used by the
 *			step-wise governor.
 * @ops:	operations this &thermal_zone_device supports
 * @tzp:	thermal zone parameters
 * @governor:	pointer to the governor for this thermal zone
 * @thermal_instances:	list of &struct thermal_instance of this thermal zone
 * @idr:	&struct idr to generate unique id for this zone's cooling
 *		devices
 * @lock:	lock to protect thermal_instances list
 * @node:	node in thermal_tz_list (in thermal_core.c)
 * @poll_queue:	delayed work for polling
 */
struct thermal_zone_device {
	int id;
	char type[THERMAL_NAME_LENGTH];
	struct device device;
	struct thermal_attr *trip_temp_attrs;
	struct thermal_attr *trip_type_attrs;
	struct thermal_attr *trip_hyst_attrs;
	void *devdata;
	int trips;
	int passive_delay;
	int polling_delay;
	int temperature;
	int last_temperature;
	int emul_temperature;
	int passive;
	unsigned int forced_passive;
	struct thermal_zone_device_ops *ops;
	const struct thermal_zone_params *tzp;
	struct thermal_governor *governor;
	struct list_head thermal_instances;
	struct idr idr;
	struct mutex lock;
	struct list_head node;
	struct delayed_work poll_queue;
};

/**
 * struct thermal_governor - structure that holds thermal governor information
 * @name:	name of the governor
 * @throttle:	callback called for every trip point even if temperature is
 *		below the trip point temperature
 * @governor_list:	node in thermal_governor_list (in thermal_core.c)
 */
struct thermal_governor {
	char name[THERMAL_NAME_LENGTH];
	int (*throttle)(struct thermal_zone_device *tz, int trip);
	struct list_head	governor_list;
};

/* Structure that holds binding parameters for a zone */
struct thermal_bind_params {
	struct thermal_cooling_device *cdev;

	/*
	 * This is a measure of 'how effectively these devices can
	 * cool 'this' thermal zone. The shall be determined by platform
	 * characterization. This is on a 'percentage' scale.
	 * See Documentation/thermal/sysfs-api.txt for more information.
	 */
	int weight;

	/*
	 * This is a bit mask that gives the binding relation between this
	 * thermal zone and cdev, for a particular trip point.
	 * See Documentation/thermal/sysfs-api.txt for more information.
	 */
	int trip_mask;

	/*
	 * This is an array of cooling state limits. Must have exactly
	 * 2 * thermal_zone.number_of_trip_points. It is an array consisting
	 * of tuples <lower-state upper-state> of state limits. Each trip
	 * will be associated with one state limit tuple when binding.
	 * A NULL pointer means <THERMAL_NO_LIMITS THERMAL_NO_LIMITS>
	 * on all trips.
	 */
	unsigned long *binding_limits;
	int (*match) (struct thermal_zone_device *tz,
			struct thermal_cooling_device *cdev);
};

/* Structure to define Thermal Zone parameters */
struct thermal_zone_params {
	char governor_name[THERMAL_NAME_LENGTH];

	/*
	 * a boolean to indicate if the thermal to hwmon sysfs interface
	 * is required. when no_hwmon == false, a hwmon sysfs interface
	 * will be created. when no_hwmon == true, nothing will be done
	 */
	bool no_hwmon;

	int num_tbps;	/* Number of tbp entries */
	struct thermal_bind_params *tbp;
};

struct thermal_genl_event {
	u32 orig;
	enum events event;
};

/* Function declarations */
#ifdef CONFIG_THERMAL_OF
struct thermal_zone_device *
thermal_zone_of_sensor_register(struct device *dev, int id,
				void *data, int (*get_temp)(void *, long *),
				int (*get_trend)(void *, long *));
void thermal_zone_of_sensor_unregister(struct device *dev,
				       struct thermal_zone_device *tz);
#else
static inline struct thermal_zone_device *
thermal_zone_of_sensor_register(struct device *dev, int id,
				void *data, int (*get_temp)(void *, long *),
				int (*get_trend)(void *, long *))
{
	return NULL;
}

static inline
void thermal_zone_of_sensor_unregister(struct device *dev,
				       struct thermal_zone_device *tz)
{
}

#endif
struct thermal_zone_device *thermal_zone_device_register(const char *, int, int,
		void *, struct thermal_zone_device_ops *,
		const struct thermal_zone_params *, int, int);
void thermal_zone_device_unregister(struct thermal_zone_device *);

int thermal_zone_bind_cooling_device(struct thermal_zone_device *, int,
				     struct thermal_cooling_device *,
				     unsigned long, unsigned long);
int thermal_zone_unbind_cooling_device(struct thermal_zone_device *, int,
				       struct thermal_cooling_device *);
void thermal_zone_device_update(struct thermal_zone_device *);

struct thermal_cooling_device *thermal_cooling_device_register(char *, void *,
		const struct thermal_cooling_device_ops *);
struct thermal_cooling_device *
thermal_of_cooling_device_register(struct device_node *np, char *, void *,
				   const struct thermal_cooling_device_ops *);
void thermal_cooling_device_unregister(struct thermal_cooling_device *);
struct thermal_zone_device *thermal_zone_get_zone_by_name(const char *name);
int thermal_zone_get_temp(struct thermal_zone_device *tz, unsigned long *temp);

int get_tz_trend(struct thermal_zone_device *, int);
struct thermal_instance *get_thermal_instance(struct thermal_zone_device *,
		struct thermal_cooling_device *, int);
void thermal_cdev_update(struct thermal_cooling_device *);
void thermal_notify_framework(struct thermal_zone_device *, int);

#ifdef CONFIG_NET
extern int thermal_generate_netlink_event(struct thermal_zone_device *tz,
						enum events event);
#else
static inline int thermal_generate_netlink_event(struct thermal_zone_device *tz,
						enum events event)
{
	return 0;
}
#endif

#endif /* __THERMAL_H__ */
