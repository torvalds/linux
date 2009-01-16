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
#endif

static ctl_table coda_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "timeout",
		.data		= &coda_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "hard",
		.data		= &coda_hard,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "fake_statfs",
		.data		= &coda_fake_statfs,
		.maxlen		= sizeof(int),
		.mode		= 0600,
		.proc_handler	= &proc_dointvec
	},
	{}
};

#ifdef CONFIG_SYSCTL
static ctl_table fs_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "coda",
		.mode		= 0555,
		.child		= coda_table
	},
	{}
};
#endif

void coda_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	if ( !fs_table_header )
		fs_table_header = register_sysctl_table(fs_table);
#endif
}

void coda_sysctl_clean(void)
{
#ifdef CONFIG_SYSCTL
	if ( fs_table_header ) {
		unregister_sysctl_table(fs_table_header);
		fs_table_header = NULL;
	}
#endif
}
