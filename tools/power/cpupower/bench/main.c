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
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include "config.h"
#include "system.h"
#include "benchmark.h"

static struct option long_options[] =
{
	{"output",	1,	0,	'o'},
	{"sleep",	1,	0,	's'},
	{"load",	1,	0,	'l'},
	{"verbose",	0,	0,	'v'},
	{"cpu",		1,	0,	'c'},
	{"governor",	1,	0,	'g'},
	{"prio",	1,	0,	'p'},
	{"file",	1,	0,	'f'},
	{"cycles",	1,	0,	'n'},
	{"rounds",	1,	0,	'r'},
	{"load-step",	1,	0,	'x'},
	{"sleep-step",	1,	0,	'y'},
	{"help",	0,	0,	'h'},
	{0, 0, 0, 0}
};

/*******************************************************************
 usage
*******************************************************************/

void usage() 
{
	printf("usage: ./bench\n");
	printf("Options:\n");
	printf(" -l, --load=<long int>\t\tinitial load time in us\n");
	printf(" -s, --sleep=<long int>\t\tinitial sleep time in us\n");
	printf(" -x, --load-step=<long int>\ttime to be added to load time, in us\n");
	printf(" -y, --sleep-step=<long int>\ttime to be added to sleep time, in us\n");
	printf(" -c, --cpu=<cpu #>\t\t\tCPU Nr. to use, starting at 0\n");
	printf(" -p, --prio=<priority>\t\t\tscheduler priority, HIGH, LOW or DEFAULT\n");
	printf(" -g, --governor=<governor>\t\tcpufreq governor to test\n");
	printf(" -n, --cycles=<int>\t\t\tload/sleep cycles\n");
	printf(" -r, --rounds<int>\t\t\tload/sleep rounds\n");
	printf(" -f, --file=<configfile>\t\tconfig file to use\n");
	printf(" -o, --output=<dir>\t\t\toutput path. Filename will be OUTPUTPATH/benchmark_TIMESTAMP.log\n");
	printf(" -v, --verbose\t\t\t\tverbose output on/off\n");
	printf(" -h, --help\t\t\t\tPrint this help screen\n");
	exit (1);
}

/*******************************************************************
 main
*******************************************************************/

int main(int argc, char **argv)
{
	int c;
	int option_index = 0;
	struct config *config = NULL;

	config = prepare_default_config();

	if (config == NULL)
		return EXIT_FAILURE;

	while (1) {
		c = getopt_long (argc, argv, "hg:o:s:l:vc:p:f:n:r:x:y:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'o':
			if (config->output != NULL)
				fclose(config->output);

			config->output = prepare_output(optarg);

			if (config->output == NULL)
				return EXIT_FAILURE;

			dprintf("user output path -> %s\n", optarg);
			break;
		case 's':
			sscanf(optarg, "%li", &config->sleep);
			dprintf("user sleep time -> %s\n", optarg);
			break;
		case 'l':
			sscanf(optarg, "%li", &config->load);
			dprintf("user load time -> %s\n", optarg);
			break;
		case 'c':
			sscanf(optarg, "%u", &config->cpu);
			dprintf("user cpu -> %s\n", optarg);
			break;
		case 'g':
			strncpy(config->governor, optarg, 14);
			dprintf("user governor -> %s\n", optarg);
			break;
		case 'p':
			if (string_to_prio(optarg) != SCHED_ERR) {
				config->prio = string_to_prio(optarg);
				dprintf("user prio -> %s\n", optarg);
			} else {
				if (config != NULL) {
					if (config->output != NULL)
						fclose(config->output);
					free(config);
				}
				usage();
			}
			break;
		case 'n':
			sscanf(optarg, "%u", &config->cycles);
			dprintf("user cycles -> %s\n", optarg);
			break;
		case 'r':
			sscanf(optarg, "%u", &config->rounds);
			dprintf("user rounds -> %s\n", optarg);
			break;
		case 'x':
			sscanf(optarg, "%li", &config->load_step);
			dprintf("user load_step -> %s\n", optarg);
			break;
		case 'y':
			sscanf(optarg, "%li", &config->sleep_step);
			dprintf("user sleep_step -> %s\n", optarg);
			break;
		case 'f':
			if (prepare_config(optarg, config))
				return EXIT_FAILURE;
			break;
		case 'v':
			config->verbose = 1;
			dprintf("verbose output enabled\n");
			break;
		case 'h':
		case '?':
		default:
			if (config != NULL) {
				if (config->output != NULL)
					fclose(config->output);
				free(config);
			}
			usage();
		}
	}

	if (config->verbose) {
		printf("starting benchmark with parameters:\n");
		printf("config:\n\t"
		       "sleep=%li\n\t"
		       "load=%li\n\t"
		       "sleep_step=%li\n\t"
		       "load_step=%li\n\t"
		       "cpu=%u\n\t"
		       "cycles=%u\n\t"
		       "rounds=%u\n\t"
		       "governor=%s\n\n",
		       config->sleep,
		       config->load,
		       config->sleep_step,
		       config->load_step,
		       config->cpu,
		       config->cycles,
		       config->rounds,
		       config->governor);
	}

	prepare_user(config);
	prepare_system(config);
	start_benchmark(config);

	if (config->output != stdout)
		fclose(config->output);

	free(config);

	return EXIT_SUCCESS;
}

