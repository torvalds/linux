/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <dirent.h>

#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "parse.h"
#include "config.h"

/**
 * converts priority string to priority
 *
 * @param str string that represents a scheduler priority
 *
 * @retval priority
 * @retval SCHED_ERR when the priority doesn't exit
 **/

enum sched_prio string_to_prio(const char *str)
{
	if (strncasecmp("high", str, strlen(str)) == 0)
		return  SCHED_HIGH;
	else if (strncasecmp("default", str, strlen(str)) == 0)
		return SCHED_DEFAULT;
	else if (strncasecmp("low", str, strlen(str)) == 0)
		return SCHED_LOW;
	else
		return SCHED_ERR;
}

/**
 * create and open logfile
 *
 * @param dir directory in which the logfile should be created
 *
 * @retval logfile on success
 * @retval NULL when the file can't be created
 **/

FILE *prepare_output(const char *dirname)
{
	FILE *output = NULL;
	int len;
	char *filename, *filename_tmp;
	struct utsname sysdata;
	DIR *dir;

	dir = opendir(dirname);
	if (dir == NULL) {
		if (mkdir(dirname, 0755)) {
			perror("mkdir");
			fprintf(stderr, "error: Cannot create dir %s\n",
				dirname);
			return NULL;
		}
	}

	len = strlen(dirname) + 30;
	filename = malloc(sizeof(char) * len);
	if (!filename) {
		perror("malloc");
		goto out_dir;
	}

	if (uname(&sysdata) == 0) {
		len += strlen(sysdata.nodename) + strlen(sysdata.release);
		filename_tmp = realloc(filename, sizeof(*filename) * len);

		if (filename_tmp == NULL) {
			free(filename);
			perror("realloc");
			goto out_dir;
		}

		filename = filename_tmp;
		snprintf(filename, len - 1, "%s/benchmark_%s_%s_%li.log",
			dirname, sysdata.nodename, sysdata.release, time(NULL));
	} else {
		snprintf(filename, len - 1, "%s/benchmark_%li.log",
			dirname, time(NULL));
	}

	dprintf("logfilename: %s\n", filename);

	output = fopen(filename, "w+");
	if (output == NULL) {
		perror("fopen");
		fprintf(stderr, "error: unable to open logfile\n");
		goto out;
	}

	fprintf(stdout, "Logfile: %s\n", filename);

	fprintf(output, "#round load sleep performance powersave percentage\n");
out:
	free(filename);
out_dir:
	closedir(dir);
	return output;
}

/**
 * returns the default config
 *
 * @retval default config on success
 * @retval NULL when the output file can't be created
 **/

struct config *prepare_default_config()
{
	struct config *config = malloc(sizeof(struct config));

	dprintf("loading defaults\n");

	config->sleep = 500000;
	config->load = 500000;
	config->sleep_step = 500000;
	config->load_step = 500000;
	config->cycles = 5;
	config->rounds = 50;
	config->cpu = 0;
	config->prio = SCHED_HIGH;
	config->verbose = 0;
	strncpy(config->governor, "ondemand", 8);

	config->output = stdout;

#ifdef DEFAULT_CONFIG_FILE
	if (prepare_config(DEFAULT_CONFIG_FILE, config))
		return NULL;
#endif
	return config;
}

/**
 * parses config file and returns the config to the caller
 *
 * @param path config file name
 *
 * @retval 1 on error
 * @retval 0 on success
 **/

int prepare_config(const char *path, struct config *config)
{
	size_t len = 0;
	char opt[16], val[32], *line = NULL;
	FILE *configfile;

	if (config == NULL) {
		fprintf(stderr, "error: config is NULL\n");
		return 1;
	}

	configfile = fopen(path, "r");
	if (configfile == NULL) {
		perror("fopen");
		fprintf(stderr, "error: unable to read configfile\n");
		free(config);
		return 1;
	}

	while (getline(&line, &len, configfile) != -1) {
		if (line[0] == '#' || line[0] == ' ' || line[0] == '\n')
			continue;

		if (sscanf(line, "%14s = %30s", opt, val) < 2)
			continue;

		dprintf("parsing: %s -> %s\n", opt, val);

		if (strcmp("sleep", opt) == 0)
			sscanf(val, "%li", &config->sleep);

		else if (strcmp("load", opt) == 0)
			sscanf(val, "%li", &config->load);

		else if (strcmp("load_step", opt) == 0)
			sscanf(val, "%li", &config->load_step);

		else if (strcmp("sleep_step", opt) == 0)
			sscanf(val, "%li", &config->sleep_step);

		else if (strcmp("cycles", opt) == 0)
			sscanf(val, "%u", &config->cycles);

		else if (strcmp("rounds", opt) == 0)
			sscanf(val, "%u", &config->rounds);

		else if (strcmp("verbose", opt) == 0)
			sscanf(val, "%u", &config->verbose);

		else if (strcmp("output", opt) == 0)
			config->output = prepare_output(val); 

		else if (strcmp("cpu", opt) == 0)
			sscanf(val, "%u", &config->cpu);

		else if (strcmp("governor", opt) == 0) {
			strncpy(config->governor, val,
					sizeof(config->governor));
			config->governor[sizeof(config->governor) - 1] = '\0';
		}

		else if (strcmp("priority", opt) == 0) {
			if (string_to_prio(val) != SCHED_ERR)
				config->prio = string_to_prio(val);
		}
	}

	free(line);

	return 0;
}
