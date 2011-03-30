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
#include <unistd.h>
#include <math.h>

#include "config.h"
#include "system.h"
#include "benchmark.h"

/* Print out progress if we log into a file */
#define show_progress(total_time, progress_time)	\
if (config->output != stdout) {				\
	fprintf(stdout, "Progress: %02lu %%\r",		\
		(progress_time * 100) / total_time);	\
	fflush(stdout);					\
}

/**
 * compute how many rounds of calculation we should do
 * to get the given load time
 *
 * @param load aimed load time in µs
 *
 * @retval rounds of calculation
 **/

unsigned int calculate_timespace(long load, struct config *config)
{
	int i;
	long long now, then;
	unsigned int estimated = GAUGECOUNT;
	unsigned int rounds = 0;
	unsigned int timed = 0;

	if (config->verbose)
		printf("calibrating load of %lius, please wait...\n", load);

	/* get the initial calculation time for a specific number of rounds */
	now = get_time();
	ROUNDS(estimated);
	then = get_time();

	timed = (unsigned int)(then - now);

	/* approximation of the wanted load time by comparing with the
	 * initial calculation time */ 
	for (i= 0; i < 4; i++)
	{
		rounds = (unsigned int)(load * estimated / timed);
		dprintf("calibrating with %u rounds\n", rounds);
		now = get_time();
		ROUNDS(rounds);
		then = get_time();

		timed = (unsigned int)(then - now);
		estimated = rounds;
	}
	if (config->verbose)
		printf("calibration done\n");

	return estimated;
}

/**
 * benchmark
 * generates a specific sleep an load time with the performance
 * governor and compares the used time for same calculations done
 * with the configured powersave governor
 *
 * @param config config values for the benchmark
 *
 **/

void start_benchmark(struct config *config)
{
	unsigned int _round, cycle;
	long long now, then;
	long sleep_time = 0, load_time = 0;
	long performance_time = 0, powersave_time = 0;
	unsigned int calculations;
	unsigned long total_time = 0, progress_time = 0;

	sleep_time = config->sleep;
	load_time = config->load;

	/* For the progress bar */
	for (_round=1; _round <= config->rounds; _round++)
		total_time += _round * (config->sleep + config->load);
	total_time *= 2; /* powersave and performance cycles */

	for (_round=0; _round < config->rounds; _round++) {
		performance_time = 0LL;
		powersave_time = 0LL;

		show_progress(total_time, progress_time);

		/* set the cpufreq governor to "performance" which disables
		 * P-State switching. */
		if (set_cpufreq_governor("performance", config->cpu) != 0)
			return;

		/* calibrate the calculation time. the resulting calculation
		 * _rounds should produce a load which matches the configured
		 * load time */
		calculations = calculate_timespace(load_time, config);

		if (config->verbose)
			printf("_round %i: doing %u cycles with %u calculations"
			       " for %lius\n", _round + 1, config->cycles,
			       calculations, load_time);

		fprintf(config->output, "%u %li %li ",
			_round, load_time, sleep_time);

		if (config->verbose) {
			printf("avarage: %lius, rps:%li\n", load_time / calculations, 1000000 * calculations / load_time);
		}

		/* do some sleep/load cycles with the performance governor */
		for (cycle = 0; cycle < config->cycles; cycle++) {
			now = get_time();
			usleep(sleep_time);
			ROUNDS(calculations);
			then = get_time();
			performance_time += then - now - sleep_time;
			if (config->verbose)
				printf("performance cycle took %lius, sleep: %lius, load: %lius, rounds: %u\n",
					(long)(then - now), sleep_time, load_time, calculations);
		}
		fprintf(config->output, "%li ", performance_time / config->cycles);

		progress_time += sleep_time + load_time;
		show_progress(total_time, progress_time);

		/* set the powersave governor which activates P-State switching
		 * again */
		if (set_cpufreq_governor(config->governor, config->cpu) != 0)
			return;

		/* again, do some sleep/load cycles with the powersave governor */
		for (cycle = 0; cycle < config->cycles; cycle++) {
			now = get_time();
			usleep(sleep_time);
			ROUNDS(calculations);
			then = get_time();
			powersave_time += then - now - sleep_time;
			if (config->verbose)
				printf("powersave cycle took %lius, sleep: %lius, load: %lius, rounds: %u\n",
					(long)(then - now), sleep_time, load_time, calculations);
		}

		progress_time += sleep_time + load_time;

		/* compare the avarage sleep/load cycles  */
		fprintf(config->output, "%li ", powersave_time / config->cycles);
		fprintf(config->output, "%.3f\n", performance_time * 100.0 / powersave_time);
		fflush(config->output);

		if (config->verbose)
			printf("performance is at %.2f%%\n", performance_time * 100.0 / powersave_time);

		sleep_time += config->sleep_step;
		load_time += config->load_step;
	}
}

