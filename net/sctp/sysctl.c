/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2002, 2004
 * Copyright (c) 2002 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * Sysctl related interfaces for SCTP.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    Mingqin Liu           <liuming@us.ibm.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Ardelle Fan           <ardelle.fan@intel.com>
 *    Ryan Layer            <rmlayer@us.ibm.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <net/sctp/structs.h>
#include <net/sctp/sctp.h>
#include <linux/sysctl.h>

static int zero = 0;
static int one = 1;
static int timer_max = 86400000; /* ms in one day */
static int int_max = INT_MAX;
static int sack_timer_min = 1;
static int sack_timer_max = 500;
static int addr_scope_max = 3; /* check sctp_scope_policy_t in include/net/sctp/constants.h for max entries */
static int rwnd_scale_max = 16;
static unsigned long max_autoclose_min = 0;
static unsigned long max_autoclose_max =
	(MAX_SCHEDULE_TIMEOUT / HZ > UINT_MAX)
	? UINT_MAX : MAX_SCHEDULE_TIMEOUT / HZ;

extern long sysctl_sctp_mem[3];
extern int sysctl_sctp_rmem[3];
extern int sysctl_sctp_wmem[3];

static int proc_sctp_do_hmac_alg(ctl_table *ctl,
				int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos);
static int proc_sctp_do_auth(struct ctl_table *ctl, int write,
			     void __user *buffer, size_t *lenp,
			     loff_t *ppos);

static ctl_table sctp_table[] = {
	{
		.procname	= "sctp_mem",
		.data		= &sysctl_sctp_mem,
		.maxlen		= sizeof(sysctl_sctp_mem),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax
	},
	{
		.procname	= "sctp_rmem",
		.data		= &sysctl_sctp_rmem,
		.maxlen		= sizeof(sysctl_sctp_rmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sctp_wmem",
		.data		= &sysctl_sctp_wmem,
		.maxlen		= sizeof(sysctl_sctp_wmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},

	{ /* sentinel */ }
};

static ctl_table sctp_net_table[] = {
	{
		.procname	= "rto_initial",
		.data		= &init_net.sctp.rto_initial,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "rto_min",
		.data		= &init_net.sctp.rto_min,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "rto_max",
		.data		= &init_net.sctp.rto_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "rto_alpha_exp_divisor",
		.data		= &init_net.sctp.rto_alpha,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rto_beta_exp_divisor",
		.data		= &init_net.sctp.rto_beta,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "max_burst",
		.data		= &init_net.sctp.max_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &int_max
	},
	{
		.procname	= "cookie_preserve_enable",
		.data		= &init_net.sctp.cookie_preserve_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "cookie_hmac_alg",
		.maxlen		= 8,
		.mode		= 0644,
		.proc_handler	= proc_sctp_do_hmac_alg,
	},
	{
		.procname	= "valid_cookie_life",
		.data		= &init_net.sctp.valid_cookie_life,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "sack_timeout",
		.data		= &init_net.sctp.sack_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &sack_timer_min,
		.extra2         = &sack_timer_max,
	},
	{
		.procname	= "hb_interval",
		.data		= &init_net.sctp.hb_interval,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "association_max_retrans",
		.data		= &init_net.sctp.max_retrans_association,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.procname	= "path_max_retrans",
		.data		= &init_net.sctp.max_retrans_path,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.procname	= "max_init_retransmits",
		.data		= &init_net.sctp.max_retrans_init,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.procname	= "pf_retrans",
		.data		= &init_net.sctp.pf_retrans,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &int_max
	},
	{
		.procname	= "sndbuf_policy",
		.data		= &init_net.sctp.sndbuf_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rcvbuf_policy",
		.data		= &init_net.sctp.rcvbuf_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "default_auto_asconf",
		.data		= &init_net.sctp.default_auto_asconf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "addip_enable",
		.data		= &init_net.sctp.addip_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "addip_noauth_enable",
		.data		= &init_net.sctp.addip_noauth,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "prsctp_enable",
		.data		= &init_net.sctp.prsctp_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "auth_enable",
		.data		= &init_net.sctp.auth_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_sctp_do_auth,
	},
	{
		.procname	= "addr_scope_policy",
		.data		= &init_net.sctp.scope_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &addr_scope_max,
	},
	{
		.procname	= "rwnd_update_shift",
		.data		= &init_net.sctp.rwnd_upd_shift,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &rwnd_scale_max,
	},
	{
		.procname	= "max_autoclose",
		.data		= &init_net.sctp.max_autoclose,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.extra1		= &max_autoclose_min,
		.extra2		= &max_autoclose_max,
	},

	{ /* sentinel */ }
};

static int proc_sctp_do_hmac_alg(ctl_table *ctl,
				int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	struct net *net = current->nsproxy->net_ns;
	char tmp[8];
	ctl_table tbl;
	int ret;
	int changed = 0;
	char *none = "none";

	memset(&tbl, 0, sizeof(struct ctl_table));

	if (write) {
		tbl.data = tmp;
		tbl.maxlen = 8;
	} else {
		tbl.data = net->sctp.sctp_hmac_alg ? : none;
		tbl.maxlen = strlen(tbl.data);
	}
		ret = proc_dostring(&tbl, write, buffer, lenp, ppos);

	if (write) {
#ifdef CONFIG_CRYPTO_MD5
		if (!strncmp(tmp, "md5", 3)) {
			net->sctp.sctp_hmac_alg = "md5";
			changed = 1;
		}
#endif
#ifdef CONFIG_CRYPTO_SHA1
		if (!strncmp(tmp, "sha1", 4)) {
			net->sctp.sctp_hmac_alg = "sha1";
			changed = 1;
		}
#endif
		if (!strncmp(tmp, "none", 4)) {
			net->sctp.sctp_hmac_alg = NULL;
			changed = 1;
		}

		if (!changed)
			ret = -EINVAL;
	}

	return ret;
}

static int proc_sctp_do_auth(struct ctl_table *ctl, int write,
			     void __user *buffer, size_t *lenp,
			     loff_t *ppos)
{
	struct net *net = current->nsproxy->net_ns;
	struct ctl_table tbl;
	int new_value, ret;

	memset(&tbl, 0, sizeof(struct ctl_table));
	tbl.maxlen = sizeof(unsigned int);

	if (write)
		tbl.data = &new_value;
	else
		tbl.data = &net->sctp.auth_enable;

	ret = proc_dointvec(&tbl, write, buffer, lenp, ppos);
	if (write && ret == 0) {
		struct sock *sk = net->sctp.ctl_sock;

		net->sctp.auth_enable = new_value;
		/* Update the value in the control socket */
		lock_sock(sk);
		sctp_sk(sk)->ep->auth_enable = new_value;
		release_sock(sk);
	}

	return ret;
}

int sctp_sysctl_net_register(struct net *net)
{
	struct ctl_table *table;
	int i;

	table = kmemdup(sctp_net_table, sizeof(sctp_net_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	for (i = 0; table[i].data; i++)
		table[i].data += (char *)(&net->sctp) - (char *)&init_net.sctp;

	net->sctp.sysctl_header = register_net_sysctl(net, "net/sctp", table);
	return 0;
}

void sctp_sysctl_net_unregister(struct net *net)
{
	struct ctl_table *table;

	table = net->sctp.sysctl_header->ctl_table_arg;
	unregister_net_sysctl_table(net->sctp.sysctl_header);
	kfree(table);
}

static struct ctl_table_header * sctp_sysctl_header;

/* Sysctl registration.  */
void sctp_sysctl_register(void)
{
	sctp_sysctl_header = register_net_sysctl(&init_net, "net/sctp", sctp_table);
}

/* Sysctl deregistration.  */
void sctp_sysctl_unregister(void)
{
	unregister_net_sysctl_table(sctp_sysctl_header);
}
