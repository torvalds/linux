/*
 * Sysctl operations for Coda filesystem
 * Original version: (C) 1996 P. Braam and M. Callahan
 * Rewritten for Linux 2.1. (C) 1997 Carnegie Mellon University
 * 
 * Carnegie Mellon encourages users to contribute improvements to
 * the Coda project. Contact Peter Braam (coda@cs.cmu.edu).
 */

#include <linux/sysctl.h>

#include "coda_int.h"

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *fs_table_header;

static ctl_table coda_table[] = {
	{
		.procname	= "timeout",
		.data		= &coda_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "hard",
		.data		= &coda_hard,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "fake_statfs",
		.data		= &coda_fake_statfs,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= proc_dointvec
	},
	{}
};

static ctl_table fs_table[] = {
	{
		.procname	= "coda",
		.mode		= 0555,
		.child		= coda_table
	},
	{}
};

void coda_sysctl_init(void)
{
	if ( !fs_table_header )
		fs_table_header = register_sysctl_table(fs_table);
}

void coda_sysctl_clean(void)
{
	if ( fs_table_header ) {
		unregister_sysctl_table(fs_table_header);
		fs_table_header = NULL;
	}
}

#else
void coda_sysctl_init(void)
{
}

void coda_sysctl_clean(void)
{
}
#endif
