/* SPDX-License-Identifier: GPL-2.0-or-later */
/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 */

/* struct that holds the required config parameters */
struct config
{
	long sleep;		/* sleep time in µs */
	long load;		/* load time in µs */
	long sleep_step;	/* time value which changes the
				 * sleep time after every round in µs */
	long load_step;		/* time value which changes the
				 * load time after every round in µs */
	unsigned int cycles;	/* calculation cycles with the same sleep/load time */
	unsigned int rounds;	/* calculation rounds with iterated sleep/load time */
	unsigned int cpu;	/* cpu for which the affinity is set */
	char governor[15];	/* cpufreq governor */
	enum sched_prio		/* possible scheduler priorities */
	{
		SCHED_ERR = -1,
		SCHED_HIGH,
		SCHED_DEFAULT,
		SCHED_LOW
	} prio;

	unsigned int verbose;	/* verbose output */
	FILE *output;		/* logfile */
	char *output_filename;	/* logfile name, must be freed at the end
				   if output != NULL and output != stdout*/
};

enum sched_prio string_to_prio(const char *str);

FILE *prepare_output(const char *dir);

int prepare_config(const char *path, struct config *config);
struct config *prepare_default_config();

