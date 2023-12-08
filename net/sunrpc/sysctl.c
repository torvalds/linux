// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/net/sunrpc/sysctl.c
 *
 * Sysctl interface to sunrpc module.
 *
 * I would prefer to register the sunrpc table below sys/net, but that's
 * impossible at the moment.
 */

#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/module.h>

#include <linux/uaccess.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/svc_xprt.h>

#include "netns.h"

/*
 * Declare the debug flags here
 */
unsigned int	rpc_debug;
EXPORT_SYMBOL_GPL(rpc_debug);

unsigned int	nfs_debug;
EXPORT_SYMBOL_GPL(nfs_debug);

unsigned int	nfsd_debug;
EXPORT_SYMBOL_GPL(nfsd_debug);

unsigned int	nlm_debug;
EXPORT_SYMBOL_GPL(nlm_debug);

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)

static struct ctl_table_header *sunrpc_table_header;
static struct ctl_table sunrpc_table[];

void
rpc_register_sysctl(void)
{
	if (!sunrpc_table_header)
		sunrpc_table_header = register_sysctl_table(sunrpc_table);
}

void
rpc_unregister_sysctl(void)
{
	if (sunrpc_table_header) {
		unregister_sysctl_table(sunrpc_table_header);
		sunrpc_table_header = NULL;
	}
}

static int proc_do_xprt(struct ctl_table *table, int write,
			void *buffer, size_t *lenp, loff_t *ppos)
{
	char tmpbuf[256];
	ssize_t len;

	if (write || *ppos) {
		*lenp = 0;
		return 0;
	}
	len = svc_print_xprts(tmpbuf, sizeof(tmpbuf));
	len = memory_read_from_buffer(buffer, *lenp, ppos, tmpbuf, len);

	if (len < 0) {
		*lenp = 0;
		return -EINVAL;
	}
	*lenp = len;
	return 0;
}

static int
proc_dodebug(struct ctl_table *table, int write, void *buffer, size_t *lenp,
	     loff_t *ppos)
{
	char		tmpbuf[20], *s = NULL;
	char *p;
	unsigned int	value;
	size_t		left, len;

	if ((*ppos && !write) || !*lenp) {
		*lenp = 0;
		return 0;
	}

	left = *lenp;

	if (write) {
		p = buffer;
		while (left && isspace(*p)) {
			left--;
			p++;
		}
		if (!left)
			goto done;

		if (left > sizeof(tmpbuf) - 1)
			return -EINVAL;
		memcpy(tmpbuf, p, left);
		tmpbuf[left] = '\0';

		value = simple_strtol(tmpbuf, &s, 0);
		if (s) {
			left -= (s - tmpbuf);
			if (left && !isspace(*s))
				return -EINVAL;
			while (left && isspace(*s)) {
				left--;
				s++;
			}
		} else
			left = 0;
		*(unsigned int *) table->data = value;
		/* Display the RPC tasks on writing to rpc_debug */
		if (strcmp(table->procname, "rpc_debug") == 0)
			rpc_show_tasks(&init_net);
	} else {
		len = sprintf(tmpbuf, "0x%04x", *(unsigned int *) table->data);
		if (len > left)
			len = left;
		memcpy(buffer, tmpbuf, len);
		if ((left -= len) > 0) {
			*((char *)buffer + len) = '\n';
			left--;
		}
	}

done:
	*lenp -= left;
	*ppos += *lenp;
	return 0;
}


static struct ctl_table debug_table[] = {
	{
		.procname	= "rpc_debug",
		.data		= &rpc_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dodebug
	},
	{
		.procname	= "nfs_debug",
		.data		= &nfs_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dodebug
	},
	{
		.procname	= "nfsd_debug",
		.data		= &nfsd_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dodebug
	},
	{
		.procname	= "nlm_debug",
		.data		= &nlm_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dodebug
	},
	{
		.procname	= "transports",
		.maxlen		= 256,
		.mode		= 0444,
		.proc_handler	= proc_do_xprt,
	},
	{ }
};

static struct ctl_table sunrpc_table[] = {
	{
		.procname	= "sunrpc",
		.mode		= 0555,
		.child		= debug_table
	},
	{ }
};

#endif
