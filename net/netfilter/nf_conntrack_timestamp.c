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

static const struct nf_ct_ext_type tstamp_extend = {
	.id	= NF_CT_EXT_TSTAMP,
};

void nf_conntrack_tstamp_pernet_init(struct net *net)
{
	net->ct.sysctl_tstamp = nf_ct_tstamp;
}

int nf_conntrack_tstamp_init(void)
{
	int ret;
	ret = nf_ct_extend_register(&tstamp_extend);
	if (ret < 0)
		pr_err("Unable to register extension\n");
	return ret;
}

void nf_conntrack_tstamp_fini(void)
{
	nf_ct_extend_unregister(&tstamp_extend);
}
