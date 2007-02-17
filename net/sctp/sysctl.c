/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2002, 2004
 * Copyright (c) 2002 Intel Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * Sysctl related interfaces for SCTP.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
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
static long sack_timer_min = 1;
static long sack_timer_max = 500;

static ctl_table sctp_table[] = {
	{
		.ctl_name	= NET_SCTP_RTO_INITIAL,
		.procname	= "rto_initial",
		.data		= &sctp_rto_initial,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.ctl_name	= NET_SCTP_RTO_MIN,
		.procname	= "rto_min",
		.data		= &sctp_rto_min,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.ctl_name	= NET_SCTP_RTO_MAX,
		.procname	= "rto_max",
		.data		= &sctp_rto_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.ctl_name	= NET_SCTP_VALID_COOKIE_LIFE,
		.procname	= "valid_cookie_life",
		.data		= &sctp_valid_cookie_life,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.ctl_name	= NET_SCTP_MAX_BURST,
		.procname	= "max_burst",
		.data		= &sctp_max_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &int_max
	},
	{
		.ctl_name	= NET_SCTP_ASSOCIATION_MAX_RETRANS,
		.procname	= "association_max_retrans",
		.data		= &sctp_max_retrans_association,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.ctl_name	= NET_SCTP_SNDBUF_POLICY,
		.procname	= "sndbuf_policy",
		.data		= &sctp_sndbuf_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_RCVBUF_POLICY,
		.procname	= "rcvbuf_policy",
		.data		= &sctp_rcvbuf_policy,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_PATH_MAX_RETRANS,
		.procname	= "path_max_retrans",
		.data		= &sctp_max_retrans_path,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.ctl_name	= NET_SCTP_MAX_INIT_RETRANSMITS,
		.procname	= "max_init_retransmits",
		.data		= &sctp_max_retrans_init,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &one,
		.extra2		= &int_max
	},
	{
		.ctl_name	= NET_SCTP_HB_INTERVAL,
		.procname	= "hb_interval",
		.data		= &sctp_hb_interval,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1         = &one,
		.extra2         = &timer_max
	},
	{
		.ctl_name	= NET_SCTP_PRESERVE_ENABLE,
		.procname	= "cookie_preserve_enable",
		.data		= &sctp_cookie_preserve_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_RTO_ALPHA,
		.procname	= "rto_alpha_exp_divisor",
		.data		= &sctp_rto_alpha,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_RTO_BETA,
		.procname	= "rto_beta_exp_divisor",
		.data		= &sctp_rto_beta,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_ADDIP_ENABLE,
		.procname	= "addip_enable",
		.data		= &sctp_addip_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_PRSCTP_ENABLE,
		.procname	= "prsctp_enable",
		.data		= &sctp_prsctp_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec
	},
	{
		.ctl_name	= NET_SCTP_SACK_TIMEOUT,
		.procname	= "sack_timeout",
		.data		= &sctp_sack_timeout,
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1         = &sack_timer_min,
		.extra2         = &sack_timer_max,
	},
	{ .ctl_name = 0 }
};

static ctl_table sctp_net_table[] = {
	{
		.ctl_name	= NET_SCTP,
		.procname	= "sctp",
		.mode		= 0555,
		.child		= sctp_table
	},
	{ .ctl_name = 0 }
};

static ctl_table sctp_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= sctp_net_table
	},
	{ .ctl_name = 0 }
};

static struct ctl_table_header * sctp_sysctl_header;

/* Sysctl registration.  */
void sctp_sysctl_register(void)
{
	sctp_sysctl_header = register_sysctl_table(sctp_root_table);
}

/* Sysctl deregistration.  */
void sctp_sysctl_unregister(void)
{
	unregister_sysctl_table(sctp_sysctl_header);
}
