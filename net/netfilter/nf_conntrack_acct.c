// SPDX-License-Identifier: GPL-2.0-only
/* Accounting handling for netfilter. */

/*
 * (C) 2008 Krzysztof Piotr Oledzki <ole@ans.pl>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/netfilter.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/export.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_acct.h>

static bool nf_ct_acct __read_mostly;

module_param_named(acct, nf_ct_acct, bool, 0644);
MODULE_PARM_DESC(acct, "Enable connection tracking flow accounting.");

void nf_conntrack_acct_pernet_init(struct net *net)
{
	net->ct.sysctl_acct = nf_ct_acct;
}
