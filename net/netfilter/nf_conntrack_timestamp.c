// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) 2010 Pablo Neira Ayuso <pablo@netfilter.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/netfilter.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_timestamp.h>

static bool nf_ct_tstamp __read_mostly;

module_param_named(tstamp, nf_ct_tstamp, bool, 0644);
MODULE_PARM_DESC(tstamp, "Enable connection tracking flow timestamping.");

void nf_conntrack_tstamp_pernet_init(struct net *net)
{
	net->ct.sysctl_tstamp = nf_ct_tstamp;
}
