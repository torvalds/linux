/* SPDX-License-Identifier: GPL-2.0-or-later */
/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 */

#include "parse.h"

long long get_time();

int set_cpufreq_governor(char *governor, unsigned int cpu);
int set_cpu_affinity(unsigned int cpu);
int set_process_priority(int priority);

void prepare_user(const struct config *config);
void prepare_system(const struct config *config);
