// SPDX-License-Identifier: GPL-2.0-or-later
/* Key management controls
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/key.h>
#include <linux/sysctl.h>
#include "internal.h"

static const int zero, one = 1, max = INT_MAX;

struct ctl_table key_sysctls[] = {
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
#ifdef CONFIG_PERSISTENT_KEYRINGS
	{
		.procname = "persistent_keyring_expiry",
		.data = &persistent_keyring_expiry,
		.maxlen = sizeof(unsigned),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = (void *) &zero,
		.extra2 = (void *) &max,
	},
#endif
	{ }
};
