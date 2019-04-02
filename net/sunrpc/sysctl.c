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
 * Declare the de flags here
 */
unsigned int	rpc_de;
EXPORT_SYMBOL_GPL(rpc_de);

unsigned int	nfs_de;
EXPORT_SYMBOL_GPL(nfs_de);

unsigned int	nfsd_de;
EXPORT_SYMBOL_GPL(nfsd_de);

unsigned int	nlm_de;
EXPORT_SYMBOL_GPL(nlm_de);

#if IS_ENABLED(CONFIG_SUNRPC_DE)

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
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char tmpbuf[256];
	size_t len;

	if ((*ppos && !write) || !*lenp) {
		*lenp = 0;
		return 0;
	}
	len = svc_print_xprts(tmpbuf, sizeof(tmpbuf));
	return simple_read_from_buffer(buffer, *lenp, ppos, tmpbuf, len);
}

static int
proc_dode(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char		tmpbuf[20], c, *s = NULL;
	char __user *p;
	unsigned int	value;
	size_t		left, len;

	if ((*ppos && !write) || !*lenp) {
		*lenp = 0;
		return 0;
	}

	left = *lenp;

	if (write) {
		if (!access_ok(buffer, left))
			return -EFAULT;
		p = buffer;
		while (left && __get_user(c, p) >= 0 && isspace(c))
			left--, p++;
		if (!left)
			goto done;

		if (left > sizeof(tmpbuf) - 1)
			return -EINVAL;
		if (copy_from_user(tmpbuf, p, left))
			return -EFAULT;
		tmpbuf[left] = '\0';

		value = simple_strtol(tmpbuf, &s, 0);
		if (s) {
			left -= (s - tmpbuf);
			if (left && !isspace(*s))
				return -EINVAL;
			while (left && isspace(*s))
				left--, s++;
		} else
			left = 0;
		*(unsigned int *) table->data = value;
		/* Display the RPC tasks on writing to rpc_de */
		if (strcmp(table->procname, "rpc_de") == 0)
			rpc_show_tasks(&init_net);
	} else {
		len = sprintf(tmpbuf, "0x%04x", *(unsigned int *) table->data);
		if (len > left)
			len = left;
		if (copy_to_user(buffer, tmpbuf, len))
			return -EFAULT;
		if ((left -= len) > 0) {
			if (put_user('\n', (char __user *)buffer + len))
				return -EFAULT;
			left--;
		}
	}

done:
	*lenp -= left;
	*ppos += *lenp;
	return 0;
}


static struct ctl_table de_table[] = {
	{
		.procname	= "rpc_de",
		.data		= &rpc_de,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dode
	},
	{
		.procname	= "nfs_de",
		.data		= &nfs_de,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dode
	},
	{
		.procname	= "nfsd_de",
		.data		= &nfsd_de,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dode
	},
	{
		.procname	= "nlm_de",
		.data		= &nlm_de,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dode
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
		.child		= de_table
	},
	{ }
};

#endif
