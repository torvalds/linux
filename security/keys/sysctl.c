/* Key management controls
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/key.h>
#include <linux/sysctl.h>
#include "internal.h"

static const int zero, one = 1, max = INT_MAX;

ctl_table key_sysctls[] = {
	{
		.procname = "maxkeys",
		.data = &key_quota_maxkeys,
		.maxlen = sizeof(unsigned),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (void *) &one,
		.extra2 = (void *) &max,
	},
	{
		.procname = "maxbytes",
		.data = &key_quota_maxbytes,
		.maxlen = sizeof(unsigned),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (void *) &one,
		.extra2 = (void *) &max,
	},
	{
		.procname = "root_maxkeys",
		.data = &key_quota_root_maxkeys,
		.maxlen = sizeof(unsigned),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (void *) &one,
		.extra2 = (void *) &max,
	},
	{
		.procname = "root_maxbytes",
		.data = &key_quota_root_maxbytes,
		.maxlen = sizeof(unsigned),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (void *) &one,
		.extra2 = (void *) &max,
	},
	{
		.procname = "gc_delay",
		.data = &key_gc_delay,
		.maxlen = sizeof(unsigned),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (void *) &zero,
		.extra2 = (void *) &max,
	},
	{ }
};
