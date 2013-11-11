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

