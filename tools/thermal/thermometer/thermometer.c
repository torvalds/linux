// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <linux/thermal.h>

#include <libconfig.h>
#include "thermal-tools.h"

#define CLASS_THERMAL "/sys/class/thermal"

enum {
	THERMOMETER_SUCCESS = 0,
	THERMOMETER_OPTION_ERROR,
	THERMOMETER_LOG_ERROR,
	THERMOMETER_CONFIG_ERROR,
	THERMOMETER_TIME_ERROR,
	THERMOMETER_INIT_ERROR,
	THERMOMETER_RUNTIME_ERROR
};

struct options {
	int loglvl;
	int logopt;
	int overwrite;
	int duration;
	const char *config;
	char postfix[PATH_MAX];
	char output[PATH_MAX];
};

struct tz_regex {
	regex_t regex;
	int polling;
};

struct configuration {
	struct tz_regex *tz_regex;
	int nr_tz_regex;

};

struct tz {
	FILE *file_out;
	int fd_temp;
	int fd_timer;
	int polling;
	const char *name;
};

struct thermometer {
	struct tz *tz;
	int nr_tz;
};

static struct tz_regex *configuration_tz_match(const char *expr,
					       struct configuration *config)
{
	int i;

	for (i = 0; i < config->nr_tz_regex; i++) {

		if (!regexec(&config->tz_regex[i].regex, expr, 0, NULL, 0))
			return &config->tz_regex[i];
	}

	return NULL;
}

static int configuration_default_init(struct configuration *config)
{
	config->tz_regex = realloc(config->tz_regex, sizeof(*config->tz_regex) *
				   (config->nr_tz_regex + 1));

	if (regcomp(&config->tz_regex[config->nr_tz_regex].regex, ".*",
		    REG_NOSUB | REG_EXTENDED)) {
		ERROR("Invalid regular expression\n");
		return -1;
	}

	config->tz_regex[config->nr_tz_regex].polling = 250;
	config->nr_tz_regex = 1;

	return 0;
}

static int configuration_init(const char *path, struct configuration *config)
{
	config_t cfg;

	config_setting_t *tz;
	int i, length;

	if (path && access(path, F_OK)) {
		ERROR("'%s' is not accessible\n", path);
		return -1;
	}

	if (!path && !config->nr_tz_regex) {
		INFO("No thermal zones configured, using wildcard for all of them\n");
		return configuration_default_init(config);
	}

	config_init(&cfg);

	if (!config_read_file(&cfg, path)) {
		ERROR("Failed to parse %s:%d - %s\n", config_error_file(&cfg),
		      config_error_line(&cfg), config_error_text(&cfg));

		return -1;
	}

	tz = config_lookup(&cfg, "thermal-zones");
	if (!tz) {
		ERROR("No thermal zone configured to be monitored\n");
		return -1;
	}

	length = config_setting_length(tz);

	INFO("Found %d thermal zone(s) regular expression\n", length);

	for (i = 0; i < length; i++) {

		config_setting_t *node;
		const char *name;
		int polling;

		node = config_setting_get_elem(tz, i);
		if (!node) {
			ERROR("Missing node name '%d'\n", i);
			return -1;
		}

		if (!config_setting_lookup_string(node, "name", &name)) {
			ERROR("Thermal zone name not found\n");
			return -1;
		}

		if (!config_setting_lookup_int(node, "polling", &polling)) {
			ERROR("Polling value not found");
			return -1;
		}

		config->tz_regex = realloc(config->tz_regex, sizeof(*config->tz_regex) *
					(config->nr_tz_regex + 1));

		if (regcomp(&config->tz_regex[config->nr_tz_regex].regex, name,
			    REG_NOSUB | REG_EXTENDED)) {
			ERROR("Invalid regular expression '%s'\n", name);
			continue;
		}

		config->tz_regex[config->nr_tz_regex].polling = polling;
		config->nr_tz_regex++;

		INFO("Thermal zone regular expression '%s' with polling %d\n",
		     name, polling);
	}

	return 0;
}

static void usage(const char *cmd)
{
	printf("%s Version: %s\n", cmd, VERSION);
	printf("Usage: %s [options]\n", cmd);
	printf("\t-h, --help\t\tthis help\n");
	printf("\t-o, --output <dir>\toutput directory for temperature capture\n");
	printf("\t-c, --config <file>\tconfiguration file\n");
	printf("\t-d, --duration <seconds>\tcapture duration\n");
	printf("\t-l, --loglevel <level>\tlog level: ");
	printf("DEBUG, INFO, NOTICE, WARN, ERROR\n");
	printf("\t-p, --postfix <string>\tpostfix to be happened at the end of the files\n");
	printf("\t-s, --syslog\t\toutput to syslog\n");
	printf("\t-w, --overwrite\t\toverwrite the temperature capture files if they exist\n");
	printf("\n");
	exit(0);
}

static int options_init(int argc, char *argv[], struct options *options)
{
	int opt;
	time_t now = time(NULL);

	struct option long_options[] = {
		{ "help",	no_argument, NULL, 'h' },
		{ "config",	required_argument, NULL, 'c' },
		{ "duration",	required_argument, NULL, 'd' },
		{ "loglevel",	required_argument, NULL, 'l' },
		{ "postfix",	required_argument, NULL, 'p' },
		{ "output",	required_argument, NULL, 'o' },
		{ "syslog",	required_argument, NULL, 's' },
		{ "overwrite",	no_argument, NULL, 'w' },
		{ 0, 0, 0, 0 }
	};

	strftime(options->postfix, sizeof(options->postfix),
		 "-%Y-%m-%d_%H:%M:%S", gmtime(&now));

	while (1) {

		int optindex = 0;

		opt = getopt_long(argc, argv, "ho:c:d:l:p:sw", long_options, &optindex);
		if (opt == -1)
			break;

		switch (opt) {
		case 'c':
			options->config = optarg;
			break;
		case 'd':
			options->duration = atoi(optarg) * 1000;
			break;
		case 'l':
			options->loglvl = log_str2level(optarg);
			break;
		case 'h':
			usage(basename(argv[0]));
			break;
		case 'p':
			strcpy(options->postfix, optarg);
			break;
		case 'o':
			strcpy(options->output, optarg);
			break;
		case 's':
			options->logopt = TO_SYSLOG;
			break;
		case 'w':
			options->overwrite = 1;
			break;
		default: /* '?' */
			ERROR("Usage: %s --help\n", argv[0]);
			return -1;
		}
	}

	return 0;
}

static int thermometer_add_tz(const char *path, const char *name, int polling,
			      struct thermometer *thermometer)
{
	int fd;
	char tz_path[PATH_MAX];
	struct tz *tz;

	sprintf(tz_path, CLASS_THERMAL"/%s/temp", path);

	fd = open(tz_path, O_RDONLY);
	if (fd < 0) {
		ERROR("Failed to open '%s': %m\n", tz_path);
		return -1;
	}

	tz = realloc(thermometer->tz, sizeof(*thermometer->tz) * (thermometer->nr_tz + 1));
	if (!tz) {
		ERROR("Failed to allocate thermometer->tz\n");
		return -1;
	}

	thermometer->tz = tz;
	thermometer->tz[thermometer->nr_tz].fd_temp = fd;
	thermometer->tz[thermometer->nr_tz].name = strdup(name);
	thermometer->tz[thermometer->nr_tz].polling = polling;
	thermometer->nr_tz++;

	INFO("Added thermal zone '%s->%s (polling:%d)'\n", path, name, polling);

	return 0;
}

static int thermometer_init(struct configuration *config,
			    struct thermometer *thermometer)
{
	DIR *dir;
	struct dirent *dirent;
	struct tz_regex *tz_regex;
	const char *tz_dirname = "thermal_zone";

	if (mainloop_init()) {
		ERROR("Failed to start mainloop\n");
		return -1;
	}

	dir = opendir(CLASS_THERMAL);
	if (!dir) {
		ERROR("failed to open '%s'\n", CLASS_THERMAL);
		return -1;
	}

	while ((dirent = readdir(dir))) {
		char tz_type[THERMAL_NAME_LENGTH];
		char tz_path[PATH_MAX];
		FILE *tz_file;

		if (strncmp(dirent->d_name, tz_dirname, strlen(tz_dirname)))
			continue;

		sprintf(tz_path, CLASS_THERMAL"/%s/type", dirent->d_name);

		tz_file = fopen(tz_path, "r");
		if (!tz_file) {
			ERROR("Failed to open '%s': %m", tz_path);
			continue;
		}

		fscanf(tz_file, "%s", tz_type);

		fclose(tz_file);

		tz_regex = configuration_tz_match(tz_type, config);
		if (!tz_regex)
			continue;

		if (thermometer_add_tz(dirent->d_name, tz_type,
				       tz_regex->polling, thermometer))
			continue;
	}

	closedir(dir);

	return 0;
}

static int timer_temperature_callback(int fd, void *arg)
{
	struct tz *tz = arg;
	char buf[16] = { 0 };

	pread(tz->fd_temp, buf, sizeof(buf), 0);

	fprintf(tz->file_out, "%ld %s", getuptimeofday_ms(), buf);

	read(fd, buf, sizeof(buf));

	return 0;
}

static int thermometer_start(struct thermometer *thermometer,
			     struct options *options)
{
	struct itimerspec timer_it = { 0 };
	char *path;
	FILE *f;
	int i;

	INFO("Capturing %d thermal zone(s) temperature...\n", thermometer->nr_tz);

	if (access(options->output, F_OK) && mkdir(options->output, 0700)) {
		ERROR("Failed to create directory '%s'\n", options->output);
		return -1;
	}

	for (i = 0; i < thermometer->nr_tz; i++) {

		asprintf(&path, "%s/%s%s", options->output,
			 thermometer->tz[i].name, options->postfix);

		if (!options->overwrite && !access(path, F_OK)) {
			ERROR("'%s' already exists\n", path);
			return -1;
		}

		f = fopen(path, "w");
		if (!f) {
			ERROR("Failed to create '%s':%m\n", path);
			return -1;
		}

		fprintf(f, "timestamp(ms) %s(°mC)\n", thermometer->tz[i].name);

		thermometer->tz[i].file_out = f;

		DEBUG("Created '%s' file for thermal zone '%s'\n", path, thermometer->tz[i].name);

		/*
		 * Create polling timer
		 */
		thermometer->tz[i].fd_timer = timerfd_create(CLOCK_MONOTONIC, 0);
		if (thermometer->tz[i].fd_timer < 0) {
			ERROR("Failed to create timer for '%s': %m\n",
			      thermometer->tz[i].name);
			return -1;
		}

		DEBUG("Watching '%s' every %d ms\n",
		      thermometer->tz[i].name, thermometer->tz[i].polling);

		timer_it.it_interval = timer_it.it_value =
			msec_to_timespec(thermometer->tz[i].polling);

		if (timerfd_settime(thermometer->tz[i].fd_timer, 0,
				    &timer_it, NULL) < 0)
			return -1;

		if (mainloop_add(thermometer->tz[i].fd_timer,
				 timer_temperature_callback,
				 &thermometer->tz[i]))
			return -1;
	}

	return 0;
}

static int thermometer_execute(int argc, char *argv[], char *const envp[], pid_t *pid)
{
	if (!argc)
		return 0;

	*pid = fork();
	if (*pid < 0) {
		ERROR("Failed to fork process: %m");
		return -1;
	}

	if (!(*pid)) {
		execvpe(argv[0], argv, envp);
		exit(1);
	}

	return 0;
}

static int kill_process(__maybe_unused int fd, void *arg)
{
	pid_t pid = *(pid_t *)arg;

	if (kill(pid, SIGTERM))
		ERROR("Failed to send SIGTERM signal to '%d': %p\n", pid);
	else if (waitpid(pid, NULL, 0))
		ERROR("Failed to wait pid '%d': %p\n", pid);

	mainloop_exit();

	return 0;
}

static int exit_mainloop(__maybe_unused int fd, __maybe_unused void *arg)
{
	mainloop_exit();
	return 0;
}

static int thermometer_wait(struct options *options, pid_t pid)
{
	int fd;
	sigset_t mask;

	/*
	 * If there is a duration specified, we will exit the mainloop
	 * and gracefully close all the files which will flush the
	 * file system cache
	 */
	if (options->duration) {
		struct itimerspec timer_it = { 0 };

		timer_it.it_value = msec_to_timespec(options->duration);

		fd = timerfd_create(CLOCK_MONOTONIC, 0);
		if (fd < 0) {
			ERROR("Failed to create duration timer: %m\n");
			return -1;
		}

		if (timerfd_settime(fd, 0, &timer_it, NULL)) {
			ERROR("Failed to set timer time: %m\n");
			return -1;
		}

		if (mainloop_add(fd, pid < 0 ? exit_mainloop : kill_process, &pid)) {
			ERROR("Failed to set timer exit mainloop callback\n");
			return -1;
		}
	}

	/*
	 * We want to catch any keyboard interrupt, as well as child
	 * signals if any in order to exit properly
	 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGCHLD);

	if (sigprocmask(SIG_BLOCK, &mask, NULL)) {
		ERROR("Failed to set sigprocmask: %m\n");
		return -1;
	}

	fd = signalfd(-1, &mask, 0);
	if (fd < 0) {
		ERROR("Failed to set the signalfd: %m\n");
		return -1;
	}

	if (mainloop_add(fd, exit_mainloop, NULL)) {
		ERROR("Failed to set timer exit mainloop callback\n");
		return -1;
	}

	return mainloop(-1);
}

static int thermometer_stop(struct thermometer *thermometer)
{
	int i;

	INFO("Closing/flushing output files\n");

	for (i = 0; i < thermometer->nr_tz; i++)
		fclose(thermometer->tz[i].file_out);

	return 0;
}

int main(int argc, char *argv[], char *const envp[])
{
	struct options options = {
		.loglvl = LOG_DEBUG,
		.logopt = TO_STDOUT,
		.output = ".",
	};
	struct configuration config = { 0 };
	struct thermometer thermometer = { 0 };

	pid_t pid = -1;

	if (options_init(argc, argv, &options))
		return THERMOMETER_OPTION_ERROR;

	if (log_init(options.loglvl, argv[0], options.logopt))
		return THERMOMETER_LOG_ERROR;

	if (configuration_init(options.config, &config))
		return THERMOMETER_CONFIG_ERROR;

	if (uptimeofday_init())
		return THERMOMETER_TIME_ERROR;

	if (thermometer_init(&config, &thermometer))
		return THERMOMETER_INIT_ERROR;

	if (thermometer_start(&thermometer, &options))
		return THERMOMETER_RUNTIME_ERROR;

	if (thermometer_execute(argc - optind, &argv[optind], envp, &pid))
		return THERMOMETER_RUNTIME_ERROR;

	if (thermometer_wait(&options, pid))
		return THERMOMETER_RUNTIME_ERROR;

	if (thermometer_stop(&thermometer))
		return THERMOMETER_RUNTIME_ERROR;

	return THERMOMETER_SUCCESS;
}
