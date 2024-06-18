// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

struct timerlat_u_params {
	/* timerlat -> timerlat_u: user-space threads can keep running */
	int should_run;
	/* timerlat_u -> timerlat: all timerlat_u threads left, no reason to continue */
	int stopped_running;

	/* threads config */
	cpu_set_t *set;
	char *cgroup_name;
	struct sched_attr *sched_param;
};

void *timerlat_u_dispatcher(void *data);
