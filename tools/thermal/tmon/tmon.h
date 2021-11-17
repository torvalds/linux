/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * tmon.h contains data structures and constants used by TMON
 *
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * Author Name Jacob Pan <jacob.jun.pan@linux.intel.com>
 */

#ifndef TMON_H
#define TMON_H

#define MAX_DISP_TEMP 125
#define MAX_CTRL_TEMP 105
#define MIN_CTRL_TEMP 40
#define MAX_NR_TZONE 16
#define MAX_NR_CDEV 32
#define MAX_NR_TRIP 16
#define MAX_NR_CDEV_TRIP 12 /* number of cooling devices that can bind
			     * to a thermal zone trip.
			     */
#define MAX_TEMP_KC 140000
/* starting char position to draw sensor data, such as tz names
 * trip point list, etc.
 */
#define DATA_LEFT_ALIGN 10
#define NR_LINES_TZDATA 1
#define TMON_LOG_FILE "/var/tmp/tmon.log"

extern unsigned long ticktime;
extern double time_elapsed;
extern unsigned long target_temp_user;
extern int dialogue_on;
extern char ctrl_cdev[];
extern pthread_mutex_t input_lock;
extern int tmon_exit;
extern int target_thermal_zone;
/* use fixed size record to simplify data processing and transfer
 * TBD: more info to be added, e.g. programmable trip point data.
*/
struct thermal_data_record {
	struct timeval tv;
	unsigned long temp[MAX_NR_TZONE];
	double pid_out_pct;
};

struct cdev_info {
	char type[64];
	int instance;
	unsigned long max_state;
	unsigned long cur_state;
	unsigned long flag;
};

enum trip_type {
	THERMAL_TRIP_CRITICAL,
	THERMAL_TRIP_HOT,
	THERMAL_TRIP_PASSIVE,
	THERMAL_TRIP_ACTIVE,
	NR_THERMAL_TRIP_TYPE,
};

struct trip_point {
	enum trip_type type;
	unsigned long temp;
	unsigned long hysteresis;
	int attribute; /* programmability etc. */
};

/* thermal zone configuration information, binding with cooling devices could
 * change at runtime.
 */
struct tz_info {
	char type[256]; /* e.g. acpitz */
	int instance;
	int passive; /* active zone has passive node to force passive mode */
	int nr_cdev; /* number of cooling device binded */
	int nr_trip_pts;
	struct trip_point tp[MAX_NR_TRIP];
	unsigned long cdev_binding; /* bitmap for attached cdevs */
	/* cdev bind trip points, allow one cdev bind to multiple trips */
	unsigned long trip_binding[MAX_NR_CDEV];
};

struct tmon_platform_data {
	int nr_tz_sensor;
	int nr_cooling_dev;
	/* keep track of instance ids since there might be gaps */
	int max_tz_instance;
	int max_cdev_instance;
	struct tz_info *tzi;
	struct cdev_info *cdi;
};

struct control_ops {
	void (*set_ratio)(unsigned long ratio);
	unsigned long (*get_ratio)(unsigned long ratio);

};

enum cdev_types {
	CDEV_TYPE_PROC,
	CDEV_TYPE_FAN,
	CDEV_TYPE_MEM,
	CDEV_TYPE_NR,
};

/* REVISIT: the idea is to group sensors if possible, e.g. on intel mid
 * we have "skin0", "skin1", "sys", "msicdie"
 * on DPTF enabled systems, we might have PCH, TSKN, TAMB, etc.
 */
enum tzone_types {
	TZONE_TYPE_ACPI,
	TZONE_TYPE_PCH,
	TZONE_TYPE_NR,
};

/* limit the output of PID controller adjustment */
#define LIMIT_HIGH (95)
#define LIMIT_LOW  (2)

struct pid_params {
	double kp;  /* Controller gain from Dialog Box */
	double ki;  /* Time-constant for I action from Dialog Box */
	double kd;  /* Time-constant for D action from Dialog Box */
	double ts;
	double k_lpf;

	double t_target;
	double y_k;
};

extern int init_thermal_controller(void);
extern void controller_handler(const double xk, double *yk);

extern struct tmon_platform_data ptdata;
extern struct pid_params p_param;

extern FILE *tmon_log;
extern int cur_thermal_record; /* index to the trec array */
extern struct thermal_data_record trec[];
extern const char *trip_type_name[];
extern unsigned long no_control;

extern void initialize_curses(void);
extern void show_controller_stats(char *line);
extern void show_title_bar(void);
extern void setup_windows(void);
extern void disable_tui(void);
extern void show_sensors_w(void);
extern void show_data_w(void);
extern void write_status_bar(int x, char *line);
extern void show_control_w();

extern void show_cooling_device(void);
extern void show_dialogue(void);
extern int update_thermal_data(void);

extern int probe_thermal_sysfs(void);
extern void free_thermal_data(void);
extern	void resize_handler(int sig);
extern void set_ctrl_state(unsigned long state);
extern void get_ctrl_state(unsigned long *state);
extern void *handle_tui_events(void *arg);
extern int sysfs_set_ulong(char *path, char *filename, unsigned long val);
extern int zone_instance_to_index(int zone_inst);
extern void close_windows(void);

#define PT_COLOR_DEFAULT    1
#define PT_COLOR_HEADER_BAR 2
#define PT_COLOR_ERROR      3
#define PT_COLOR_RED        4
#define PT_COLOR_YELLOW     5
#define PT_COLOR_GREEN      6
#define PT_COLOR_BRIGHT     7
#define PT_COLOR_BLUE	    8

/* each thermal zone uses 12 chars, 8 for name, 2 for instance, 2 space
 * also used to list trip points in forms of AAAC, which represents
 * A: Active
 * C: Critical
 */
#define TZONE_RECORD_SIZE 12
#define TZ_LEFT_ALIGN 32
#define CDEV_NAME_SIZE 20
#define CDEV_FLAG_IN_CONTROL (1 << 0)

/* dialogue box starts */
#define DIAG_X 48
#define DIAG_Y 8
#define THERMAL_SYSFS "/sys/class/thermal"
#define CDEV "cooling_device"
#define TZONE "thermal_zone"
#define TDATA_LEFT 16
#endif /* TMON_H */
