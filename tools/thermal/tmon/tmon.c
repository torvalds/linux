/*
 * tmon.c Thermal Monitor (TMON) main function and entry point
 *
 * Copyright (C) 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 or later as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Jacob Pan <jacob.jun.pan@linux.intel.com>
 *
 */

#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ncurses.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdarg.h>
#include <syslog.h>

#include "tmon.h"

unsigned long ticktime = 1; /* seconds */
unsigned long no_control = 1; /* monitoring only or use cooling device for
			       * temperature control.
			       */
double time_elapsed = 0.0;
unsigned long target_temp_user = 65; /* can be select by tui later */
int dialogue_on;
int tmon_exit;
static short	daemon_mode;
static int logging; /* for recording thermal data to a file */
static int debug_on;
FILE *tmon_log;
/*cooling device used for the PID controller */
char ctrl_cdev[CDEV_NAME_SIZE] = "None";
int target_thermal_zone; /* user selected target zone instance */
static void	start_daemon_mode(void);

pthread_t event_tid;
pthread_mutex_t input_lock;
void usage()
{
	printf("Usage: tmon [OPTION...]\n");
	printf("  -c, --control         cooling device in control\n");
	printf("  -d, --daemon          run as daemon, no TUI\n");
	printf("  -g, --debug           debug message in syslog\n");
	printf("  -h, --help            show this help message\n");
	printf("  -l, --log             log data to /var/tmp/tmon.log\n");
	printf("  -t, --time-interval   sampling time interval, > 1 sec.\n");
	printf("  -v, --version         show version\n");
	printf("  -z, --zone            target thermal zone id\n");

	exit(0);
}

void version()
{
	printf("TMON version %s\n", VERSION);
	exit(EXIT_SUCCESS);
}

static void tmon_cleanup(void)
{

	syslog(LOG_INFO, "TMON exit cleanup\n");
	fflush(stdout);
	refresh();
	if (tmon_log)
		fclose(tmon_log);
	if (event_tid) {
		pthread_mutex_lock(&input_lock);
		pthread_cancel(event_tid);
		pthread_mutex_unlock(&input_lock);
		pthread_mutex_destroy(&input_lock);
	}
	closelog();
	/* relax control knobs, undo throttling */
	set_ctrl_state(0);

	keypad(stdscr, FALSE);
	echo();
	nocbreak();
	close_windows();
	endwin();
	free_thermal_data();

	exit(1);
}


static void tmon_sig_handler(int sig)
{
	syslog(LOG_INFO, "TMON caught signal %d\n", sig);
	refresh();
	switch (sig) {
	case SIGTERM:
		printf("sigterm, exit and clean up\n");
		fflush(stdout);
		break;
	case SIGKILL:
		printf("sigkill, exit and clean up\n");
		fflush(stdout);
		break;
	case SIGINT:
		printf("ctrl-c, exit and clean up\n");
		fflush(stdout);
		break;
	default:
		break;
	}
	tmon_exit = true;
}


static void start_syslog(void)
{
	if (debug_on)
		setlogmask(LOG_UPTO(LOG_DEBUG));
	else
		setlogmask(LOG_UPTO(LOG_ERR));
	openlog("tmon.log", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);
	syslog(LOG_NOTICE, "TMON started by User %d", getuid());
}

static void prepare_logging(void)
{
	int i;
	struct stat logstat;

	if (!logging)
		return;
	/* open local data log file */
	tmon_log = fopen(TMON_LOG_FILE, "w+");
	if (!tmon_log) {
		syslog(LOG_ERR, "failed to open log file %s\n", TMON_LOG_FILE);
		return;
	}

	if (lstat(TMON_LOG_FILE, &logstat) < 0) {
		syslog(LOG_ERR, "Unable to stat log file %s\n", TMON_LOG_FILE);
		fclose(tmon_log);
		tmon_log = NULL;
		return;
	}

	/* The log file must be a regular file owned by us */
	if (S_ISLNK(logstat.st_mode)) {
		syslog(LOG_ERR, "Log file is a symlink.  Will not log\n");
		fclose(tmon_log);
		tmon_log = NULL;
		return;
	}

	if (logstat.st_uid != getuid()) {
		syslog(LOG_ERR, "We don't own the log file.  Not logging\n");
		fclose(tmon_log);
		tmon_log = NULL;
		return;
	}


	fprintf(tmon_log, "#----------- THERMAL SYSTEM CONFIG -------------\n");
	for (i = 0; i < ptdata.nr_tz_sensor; i++) {
		char binding_str[33]; /* size of long + 1 */
		int j;

		memset(binding_str, 0, sizeof(binding_str));
		for (j = 0; j < 32; j++)
			binding_str[j] = (ptdata.tzi[i].cdev_binding & 1<<j) ?
				'1' : '0';

		fprintf(tmon_log, "#thermal zone %s%02d cdevs binding: %32s\n",
			ptdata.tzi[i].type,
			ptdata.tzi[i].instance,
			binding_str);
		for (j = 0; j <	ptdata.tzi[i].nr_trip_pts; j++) {
			fprintf(tmon_log, "#\tTP%02d type:%s, temp:%lu\n", j,
				trip_type_name[ptdata.tzi[i].tp[j].type],
				ptdata.tzi[i].tp[j].temp);
		}

	}

	for (i = 0; i <	ptdata.nr_cooling_dev; i++)
		fprintf(tmon_log, "#cooling devices%02d: %s\n",
			i, ptdata.cdi[i].type);

	fprintf(tmon_log, "#---------- THERMAL DATA LOG STARTED -----------\n");
	fprintf(tmon_log, "Samples TargetTemp ");
	for (i = 0; i < ptdata.nr_tz_sensor; i++) {
		fprintf(tmon_log, "%s%d    ", ptdata.tzi[i].type,
			ptdata.tzi[i].instance);
	}
	for (i = 0; i <	ptdata.nr_cooling_dev; i++)
		fprintf(tmon_log, "%s%d ", ptdata.cdi[i].type,
			ptdata.cdi[i].instance);

	fprintf(tmon_log, "\n");
}

static struct option opts[] = {
	{ "control", 1, NULL, 'c' },
	{ "daemon", 0, NULL, 'd' },
	{ "time-interval", 1, NULL, 't' },
	{ "log", 0, NULL, 'l' },
	{ "help", 0, NULL, 'h' },
	{ "version", 0, NULL, 'v' },
	{ "debug", 0, NULL, 'g' },
	{ 0, 0, NULL, 0 }
};


int main(int argc, char **argv)
{
	int err = 0;
	int id2 = 0, c;
	double yk = 0.0; /* controller output */
	int target_tz_index;

	if (geteuid() != 0) {
		printf("TMON needs to be run as root\n");
		exit(EXIT_FAILURE);
	}

	while ((c = getopt_long(argc, argv, "c:dlht:vgz:", opts, &id2)) != -1) {
		switch (c) {
		case 'c':
			no_control = 0;
			strncpy(ctrl_cdev, optarg, CDEV_NAME_SIZE);
			break;
		case 'd':
			start_daemon_mode();
			printf("Run TMON in daemon mode\n");
			break;
		case 't':
			ticktime = strtod(optarg, NULL);
			if (ticktime < 1)
				ticktime = 1;
			break;
		case 'l':
			printf("Logging data to /var/tmp/tmon.log\n");
			logging = 1;
			break;
		case 'h':
			usage();
			break;
		case 'v':
			version();
			break;
		case 'g':
			debug_on = 1;
			break;
		case 'z':
			target_thermal_zone = strtod(optarg, NULL);
			break;
		default:
			break;
		}
	}
	if (pthread_mutex_init(&input_lock, NULL) != 0) {
		fprintf(stderr, "\n mutex init failed, exit\n");
		return 1;
	}
	start_syslog();
	if (signal(SIGINT, tmon_sig_handler) == SIG_ERR)
		syslog(LOG_DEBUG, "Cannot handle SIGINT\n");
	if (signal(SIGTERM, tmon_sig_handler) == SIG_ERR)
		syslog(LOG_DEBUG, "Cannot handle SIGINT\n");

	if (probe_thermal_sysfs()) {
		pthread_mutex_destroy(&input_lock);
		closelog();
		return -1;
	}
	initialize_curses();
	setup_windows();
	signal(SIGWINCH, resize_handler);
	show_title_bar();
	show_sensors_w();
	show_cooling_device();
	update_thermal_data();
	show_data_w();
	prepare_logging();
	init_thermal_controller();

	nodelay(stdscr, TRUE);
	err = pthread_create(&event_tid, NULL, &handle_tui_events, NULL);
	if (err != 0) {
		printf("\ncan't create thread :[%s]", strerror(err));
		tmon_cleanup();
		exit(EXIT_FAILURE);
	}

	/* validate range of user selected target zone, default to the first
	 * instance if out of range
	 */
	target_tz_index = zone_instance_to_index(target_thermal_zone);
	if (target_tz_index < 0) {
		target_thermal_zone = ptdata.tzi[0].instance;
		syslog(LOG_ERR, "target zone is not found, default to %d\n",
			target_thermal_zone);
	}
	while (1) {
		sleep(ticktime);
		show_title_bar();
		show_sensors_w();
		update_thermal_data();
		if (!dialogue_on) {
			show_data_w();
			show_cooling_device();
		}
		cur_thermal_record++;
		time_elapsed += ticktime;
		controller_handler(trec[0].temp[target_tz_index] / 1000,
				&yk);
		trec[0].pid_out_pct = yk;
		if (!dialogue_on)
			show_control_w();
		if (tmon_exit)
			break;
	}
	tmon_cleanup();
	return 0;
}

static void start_daemon_mode()
{
	daemon_mode = 1;
	/* fork */
	pid_t	sid, pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	} else if (pid > 0)
		/* kill parent */
		exit(EXIT_SUCCESS);

	/* disable TUI, it may not be necessary, but saves some resource */
	disable_tui();

	/* change the file mode mask */
	umask(S_IWGRP | S_IWOTH);

	/* new SID for the daemon process */
	sid = setsid();
	if (sid < 0)
		exit(EXIT_FAILURE);

	/* change working directory */
	if ((chdir("/")) < 0)
		exit(EXIT_FAILURE);


	sleep(10);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

}
