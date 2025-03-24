/* SPDX-License-Identifier: LGPL-2.1+ */
/* Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org> */
#ifndef __LIBTHERMAL_H
#define __LIBTHERMAL_H

#include <linux/thermal.h>
#include <sys/types.h>

#ifndef LIBTHERMAL_API
#define LIBTHERMAL_API __attribute__((visibility("default")))
#endif

#ifndef THERMAL_THRESHOLD_WAY_UP
#define THERMAL_THRESHOLD_WAY_UP 0x1
#endif

#ifndef THERMAL_THRESHOLD_WAY_DOWN
#define THERMAL_THRESHOLD_WAY_DOWN 0x2
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct thermal_sampling_ops {
	int (*tz_temp)(int tz_id, int temp, void *arg);
};

struct thermal_events_ops {
	int (*tz_create)(const char *name, int tz_id, void *arg);
	int (*tz_delete)(int tz_id, void *arg);
	int (*tz_enable)(int tz_id, void *arg);
	int (*tz_disable)(int tz_id, void *arg);
	int (*trip_high)(int tz_id, int trip_id, int temp, void *arg);
	int (*trip_low)(int tz_id, int trip_id, int temp, void *arg);
	int (*trip_add)(int tz_id, int trip_id, int type, int temp, int hyst, void *arg);
	int (*trip_change)(int tz_id, int trip_id, int type, int temp, int hyst, void *arg);
	int (*trip_delete)(int tz_id, int trip_id, void *arg);
	int (*cdev_add)(const char *name, int cdev_id, int max_state, void *arg);
	int (*cdev_delete)(int cdev_id, void *arg);
	int (*cdev_update)(int cdev_id, int cur_state, void *arg);
	int (*gov_change)(int tz_id, const char *gov_name, void *arg);
	int (*threshold_add)(int tz_id, int temperature, int direction, void *arg);
	int (*threshold_delete)(int tz_id, int temperature, int direction, void *arg);
	int (*threshold_flush)(int tz_id, void *arg);
	int (*threshold_up)(int tz_id, int temp, int prev_temp, void *arg);
	int (*threshold_down)(int tz_id, int temp, int prev_temp, void *arg);
};

struct thermal_ops {
	struct thermal_sampling_ops sampling;
	struct thermal_events_ops events;
};

struct thermal_trip {
	int id;
	int type;
	int temp;
	int hyst;
};

struct thermal_threshold {
	int temperature;
	int direction;
};

struct thermal_zone {
	int id;
	int temp;
	char name[THERMAL_NAME_LENGTH];
	char governor[THERMAL_NAME_LENGTH];
	struct thermal_trip *trip;
	struct thermal_threshold *thresholds;
};

struct thermal_cdev {
	int id;
	char name[THERMAL_NAME_LENGTH];
	int max_state;
	int min_state;
	int cur_state;
};

typedef enum {
	THERMAL_ERROR = -1,
	THERMAL_SUCCESS = 0,
} thermal_error_t;

struct thermal_handler;

typedef int (*cb_tz_t)(struct thermal_zone *, void *);

typedef int (*cb_tt_t)(struct thermal_trip *, void *);

typedef int (*cb_tc_t)(struct thermal_cdev *, void *);

typedef int (*cb_th_t)(struct thermal_threshold *, void *);

LIBTHERMAL_API int for_each_thermal_zone(struct thermal_zone *tz, cb_tz_t cb, void *arg);

LIBTHERMAL_API int for_each_thermal_trip(struct thermal_trip *tt, cb_tt_t cb, void *arg);

LIBTHERMAL_API int for_each_thermal_cdev(struct thermal_cdev *cdev, cb_tc_t cb, void *arg);

LIBTHERMAL_API int for_each_thermal_threshold(struct thermal_threshold *th, cb_th_t cb, void *arg);

LIBTHERMAL_API struct thermal_zone *thermal_zone_find_by_name(struct thermal_zone *tz,
							      const char *name);

LIBTHERMAL_API struct thermal_zone *thermal_zone_find_by_id(struct thermal_zone *tz, int id);

LIBTHERMAL_API struct thermal_zone *thermal_zone_discover(struct thermal_handler *th);

LIBTHERMAL_API struct thermal_handler *thermal_init(struct thermal_ops *ops);

LIBTHERMAL_API void thermal_exit(struct thermal_handler *th);

/*
 * Netlink thermal events
 */
LIBTHERMAL_API thermal_error_t thermal_events_exit(struct thermal_handler *th);

LIBTHERMAL_API thermal_error_t thermal_events_init(struct thermal_handler *th);

LIBTHERMAL_API thermal_error_t thermal_events_handle(struct thermal_handler *th, void *arg);

LIBTHERMAL_API int thermal_events_fd(struct thermal_handler *th);

/*
 * Netlink thermal commands
 */
LIBTHERMAL_API thermal_error_t thermal_cmd_exit(struct thermal_handler *th);

LIBTHERMAL_API thermal_error_t thermal_cmd_init(struct thermal_handler *th);

LIBTHERMAL_API thermal_error_t thermal_cmd_get_tz(struct thermal_handler *th,
						  struct thermal_zone **tz);

LIBTHERMAL_API thermal_error_t thermal_cmd_get_cdev(struct thermal_handler *th,
						    struct thermal_cdev **tc);

LIBTHERMAL_API thermal_error_t thermal_cmd_get_trip(struct thermal_handler *th,
						    struct thermal_zone *tz);

LIBTHERMAL_API thermal_error_t thermal_cmd_get_governor(struct thermal_handler *th,
							struct thermal_zone *tz);

LIBTHERMAL_API thermal_error_t thermal_cmd_get_temp(struct thermal_handler *th,
						    struct thermal_zone *tz);

LIBTHERMAL_API thermal_error_t thermal_cmd_threshold_get(struct thermal_handler *th,
							 struct thermal_zone *tz);

LIBTHERMAL_API thermal_error_t thermal_cmd_threshold_add(struct thermal_handler *th,
                                                         struct thermal_zone *tz,
                                                         int temperature,
                                                         int direction);

LIBTHERMAL_API thermal_error_t thermal_cmd_threshold_delete(struct thermal_handler *th,
                                                            struct thermal_zone *tz,
                                                            int temperature,
                                                            int direction);

LIBTHERMAL_API thermal_error_t thermal_cmd_threshold_flush(struct thermal_handler *th,
                                                           struct thermal_zone *tz);

/*
 * Netlink thermal samples
 */
LIBTHERMAL_API thermal_error_t thermal_sampling_exit(struct thermal_handler *th);

LIBTHERMAL_API thermal_error_t thermal_sampling_init(struct thermal_handler *th);

LIBTHERMAL_API thermal_error_t thermal_sampling_handle(struct thermal_handler *th, void *arg);

LIBTHERMAL_API int thermal_sampling_fd(struct thermal_handler *th);

#endif /* __LIBTHERMAL_H */

#ifdef __cplusplus
}
#endif
