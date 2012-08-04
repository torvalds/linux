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

static ctl_table sctp_table[] = {
	{
		.procname	= "rto_initial",
		.data		= &sctp_rto_initial,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "rto_min",
		.data		= &sctp_rto_min,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "rto_max",
		.data		= &sctp_rto_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "valid_cookie_life",
		.data		= &sctp_valid_cookie_life,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "max_burst",
		.data		= &sctp_max_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &int_max
	},
	{
		.procname	= "association_max_retrans",
		.data		= &sctp_max_retrans_association,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.procname	= "sndbuf_policy",
		.data		= &sctp_sndbuf_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rcvbuf_policy",
		.data		= &sctp_rcvbuf_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "path_max_retrans",
		.data		= &sctp_max_retrans_path,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.procname	= "pf_retrans",
		.data		= &sctp_pf_retrans,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &int_max
	},
	{
		.procname	= "max_init_retransmits",
		.data		= &sctp_max_retrans_init,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.procname	= "hb_interval",
		.data		= &sctp_hb_interval,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.procname	= "cookie_preserve_enable",
		.data		= &sctp_cookie_preserve_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rto_alpha_exp_divisor",
		.data		= &sctp_rto_alpha,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "rto_beta_exp_divisor",
		.data		= &sctp_rto_beta,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "addip_enable",
		.data		= &sctp_addip_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "default_auto_asconf",
		.data		= &sctp_default_auto_asconf,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "prsctp_enable",
		.data		= &sctp_prsctp_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sack_timeout",
		.data		= &sctp_sack_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = &sack_timer_min,
		.extra2         = &sack_timer_max,
	},
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
	{
		.procname	= "auth_enable",
		.data		= &sctp_auth_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "addip_noauth_enable",
		.data		= &sctp_addip_noauth,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "addr_scope_policy",
		.data		= &sctp_scope_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &addr_scope_max,
	},
	{
		.procname	= "rwnd_update_shift",
		.data		= &sctp_rwnd_upd_shift,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &rwnd_scale_max,
	},
	{
		.procname	= "max_autoclose",
		.data		= &sctp_max_autoclose,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.extra1		= &max_autoclose_min,
		.extra2		= &max_autoclose_max,
	},

	{ /* sentinel */ }
};

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
