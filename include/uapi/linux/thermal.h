/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_THERMAL_H
#define _UAPI_LINUX_THERMAL_H

#define THERMAL_NAME_LENGTH	20

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

/* Adding event notification support elements */
#define THERMAL_GENL_FAMILY_NAME		"thermal"
#define THERMAL_GENL_VERSION			0x01
#define THERMAL_GENL_SAMPLING_GROUP_NAME	"sampling"
#define THERMAL_GENL_EVENT_GROUP_NAME		"event"

/* Attributes of thermal_genl_family */
enum thermal_genl_attr {
	THERMAL_GENL_ATTR_UNSPEC,
	THERMAL_GENL_ATTR_TZ,
	THERMAL_GENL_ATTR_TZ_ID,
	THERMAL_GENL_ATTR_TZ_TEMP,
	THERMAL_GENL_ATTR_TZ_TRIP,
	THERMAL_GENL_ATTR_TZ_TRIP_ID,
	THERMAL_GENL_ATTR_TZ_TRIP_TYPE,
	THERMAL_GENL_ATTR_TZ_TRIP_TEMP,
	THERMAL_GENL_ATTR_TZ_TRIP_HYST,
	THERMAL_GENL_ATTR_TZ_MODE,
	THERMAL_GENL_ATTR_TZ_NAME,
	THERMAL_GENL_ATTR_TZ_CDEV_WEIGHT,
	THERMAL_GENL_ATTR_TZ_GOV,
	THERMAL_GENL_ATTR_TZ_GOV_NAME,
	THERMAL_GENL_ATTR_CDEV,
	THERMAL_GENL_ATTR_CDEV_ID,
	THERMAL_GENL_ATTR_CDEV_CUR_STATE,
	THERMAL_GENL_ATTR_CDEV_MAX_STATE,
	THERMAL_GENL_ATTR_CDEV_NAME,
	THERMAL_GENL_ATTR_GOV_NAME,

	__THERMAL_GENL_ATTR_MAX,
};
#define THERMAL_GENL_ATTR_MAX (__THERMAL_GENL_ATTR_MAX - 1)

enum thermal_genl_sampling {
	THERMAL_GENL_SAMPLING_TEMP,
	__THERMAL_GENL_SAMPLING_MAX,
};
#define THERMAL_GENL_SAMPLING_MAX (__THERMAL_GENL_SAMPLING_MAX - 1)

/* Events of thermal_genl_family */
enum thermal_genl_event {
	THERMAL_GENL_EVENT_UNSPEC,
	THERMAL_GENL_EVENT_TZ_CREATE,		/* Thermal zone creation */
	THERMAL_GENL_EVENT_TZ_DELETE,		/* Thermal zone deletion */
	THERMAL_GENL_EVENT_TZ_DISABLE,		/* Thermal zone disabled */
	THERMAL_GENL_EVENT_TZ_ENABLE,		/* Thermal zone enabled */
	THERMAL_GENL_EVENT_TZ_TRIP_UP,		/* Trip point crossed the way up */
	THERMAL_GENL_EVENT_TZ_TRIP_DOWN,	/* Trip point crossed the way down */
	THERMAL_GENL_EVENT_TZ_TRIP_CHANGE,	/* Trip point changed */
	THERMAL_GENL_EVENT_TZ_TRIP_ADD,		/* Trip point added */
	THERMAL_GENL_EVENT_TZ_TRIP_DELETE,	/* Trip point deleted */
	THERMAL_GENL_EVENT_CDEV_ADD,		/* Cdev bound to the thermal zone */
	THERMAL_GENL_EVENT_CDEV_DELETE,		/* Cdev unbound */
	THERMAL_GENL_EVENT_CDEV_STATE_UPDATE,	/* Cdev state updated */
	THERMAL_GENL_EVENT_TZ_GOV_CHANGE,	/* Governor policy changed  */
	__THERMAL_GENL_EVENT_MAX,
};
#define THERMAL_GENL_EVENT_MAX (__THERMAL_GENL_EVENT_MAX - 1)

/* Commands supported by the thermal_genl_family */
enum thermal_genl_cmd {
	THERMAL_GENL_CMD_UNSPEC,
	THERMAL_GENL_CMD_TZ_GET_ID,	/* List of thermal zones id */
	THERMAL_GENL_CMD_TZ_GET_TRIP,	/* List of thermal trips */
	THERMAL_GENL_CMD_TZ_GET_TEMP,	/* Get the thermal zone temperature */
	THERMAL_GENL_CMD_TZ_GET_GOV,	/* Get the thermal zone governor */
	THERMAL_GENL_CMD_TZ_GET_MODE,	/* Get the thermal zone mode */
	THERMAL_GENL_CMD_CDEV_GET,	/* List of cdev id */
	__THERMAL_GENL_CMD_MAX,
};
#define THERMAL_GENL_CMD_MAX (__THERMAL_GENL_CMD_MAX - 1)

#endif /* _UAPI_LINUX_THERMAL_H */
