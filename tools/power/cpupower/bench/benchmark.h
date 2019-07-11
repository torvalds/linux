/* SPDX-License-Identifier: GPL-2.0-or-later */
/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 */

/* load loop, this schould take about 1 to 2ms to complete */
#define ROUNDS(x) {unsigned int rcnt;			       \
		for (rcnt = 0; rcnt < x*1000; rcnt++) { \
			(void)(((int)(pow(rcnt, rcnt) * \
				      sqrt(rcnt*7230970)) ^ 7230716) ^ \
				      (int)atan2(rcnt, rcnt));	       \
		} }							\


void start_benchmark(struct config *config);
