// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/fuse/fuse_sysctl.c
 *
 * Sysctl interface to fuse parameters
 */
#include <linux/sysctl.h>

#include "fuse_i.h"

static struct ctl_table_header *fuse_table_header;

/* Bound by fuse_init_out max_pages, which is a u16 */
static unsigned int sysctl_fuse_max_pages_limit = 65535;

/*
 * fuse_init_out request timeouts are u16.
 * This goes up to ~18 hours, which is plenty for a timeout.
 */
static unsigned int sysctl_fuse_req_timeout_limit = 65535;

static const struct ctl_table fuse_sysctl_table[] = {
	{
		.procname	= "max_pages_limit",
		.data		= &fuse_max_pages_limit,
		.maxlen		= sizeof(fuse_max_pages_limit),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ONE,
		.extra2		= &sysctl_fuse_max_pages_limit,
	},
	{
		.procname	= "default_request_timeout",
		.data		= &fuse_default_req_timeout,
		.maxlen		= sizeof(fuse_default_req_timeout),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &sysctl_fuse_req_timeout_limit,
	},
	{
		.procname	= "max_request_timeout",
		.data		= &fuse_max_req_timeout,
		.maxlen		= sizeof(fuse_max_req_timeout),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= &sysctl_fuse_req_timeout_limit,
	},
};

int fuse_sysctl_register(void)
{
	fuse_table_header = register_sysctl("fs/fuse", fuse_sysctl_table);
	if (!fuse_table_header)
		return -ENOMEM;
	return 0;
}

void fuse_sysctl_unregister(void)
{
	unregister_sysctl_table(fuse_table_header);
	fuse_table_header = NULL;
}
