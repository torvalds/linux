// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xfrm_proc.c
 *
 * Copyright (C)2006-2007 USAGI/WIDE Project
 *
 * Authors:	Masahide NAKAMURA <nakam@linux-ipv6.org>
 */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <net/snmp.h>
#include <net/xfrm.h>

static const struct snmp_mib xfrm_mib_list[] = {
	SNMP_MIB_ITEM("XfrmInError", LINUX_MIB_XFRMINERROR),
	SNMP_MIB_ITEM("XfrmInBufferError", LINUX_MIB_XFRMINBUFFERERROR),
	SNMP_MIB_ITEM("XfrmInHdrError", LINUX_MIB_XFRMINHDRERROR),
	SNMP_MIB_ITEM("XfrmInNoStates", LINUX_MIB_XFRMINNOSTATES),
	SNMP_MIB_ITEM("XfrmInStateProtoError", LINUX_MIB_XFRMINSTATEPROTOERROR),
	SNMP_MIB_ITEM("XfrmInStateModeError", LINUX_MIB_XFRMINSTATEMODEERROR),
	SNMP_MIB_ITEM("XfrmInStateSeqError", LINUX_MIB_XFRMINSTATESEQERROR),
	SNMP_MIB_ITEM("XfrmInStateExpired", LINUX_MIB_XFRMINSTATEEXPIRED),
	SNMP_MIB_ITEM("XfrmInStateMismatch", LINUX_MIB_XFRMINSTATEMISMATCH),
	SNMP_MIB_ITEM("XfrmInStateInvalid", LINUX_MIB_XFRMINSTATEINVALID),
	SNMP_MIB_ITEM("XfrmInTmplMismatch", LINUX_MIB_XFRMINTMPLMISMATCH),
	SNMP_MIB_ITEM("XfrmInNoPols", LINUX_MIB_XFRMINNOPOLS),
	SNMP_MIB_ITEM("XfrmInPolBlock", LINUX_MIB_XFRMINPOLBLOCK),
	SNMP_MIB_ITEM("XfrmInPolError", LINUX_MIB_XFRMINPOLERROR),
	SNMP_MIB_ITEM("XfrmOutError", LINUX_MIB_XFRMOUTERROR),
	SNMP_MIB_ITEM("XfrmOutBundleGenError", LINUX_MIB_XFRMOUTBUNDLEGENERROR),
	SNMP_MIB_ITEM("XfrmOutBundleCheckError", LINUX_MIB_XFRMOUTBUNDLECHECKERROR),
	SNMP_MIB_ITEM("XfrmOutNoStates", LINUX_MIB_XFRMOUTNOSTATES),
	SNMP_MIB_ITEM("XfrmOutStateProtoError", LINUX_MIB_XFRMOUTSTATEPROTOERROR),
	SNMP_MIB_ITEM("XfrmOutStateModeError", LINUX_MIB_XFRMOUTSTATEMODEERROR),
	SNMP_MIB_ITEM("XfrmOutStateSeqError", LINUX_MIB_XFRMOUTSTATESEQERROR),
	SNMP_MIB_ITEM("XfrmOutStateExpired", LINUX_MIB_XFRMOUTSTATEEXPIRED),
	SNMP_MIB_ITEM("XfrmOutPolBlock", LINUX_MIB_XFRMOUTPOLBLOCK),
	SNMP_MIB_ITEM("XfrmOutPolDead", LINUX_MIB_XFRMOUTPOLDEAD),
	SNMP_MIB_ITEM("XfrmOutPolError", LINUX_MIB_XFRMOUTPOLERROR),
	SNMP_MIB_ITEM("XfrmFwdHdrError", LINUX_MIB_XFRMFWDHDRERROR),
	SNMP_MIB_ITEM("XfrmOutStateInvalid", LINUX_MIB_XFRMOUTSTATEINVALID),
	SNMP_MIB_ITEM("XfrmAcquireError", LINUX_MIB_XFRMACQUIREERROR),
	SNMP_MIB_SENTINEL
};

static int xfrm_statistics_seq_show(struct seq_file *seq, void *v)
{
	unsigned long buff[LINUX_MIB_XFRMMAX];
	struct net *net = seq->private;
	int i;

	memset(buff, 0, sizeof(unsigned long) * LINUX_MIB_XFRMMAX);

	xfrm_state_update_stats(net);
	snmp_get_cpu_field_batch(buff, xfrm_mib_list,
				 net->mib.xfrm_statistics);
	for (i = 0; xfrm_mib_list[i].name; i++)
		seq_printf(seq, "%-24s\t%lu\n", xfrm_mib_list[i].name,
						buff[i]);

	return 0;
}

int __net_init xfrm_proc_init(struct net *net)
{
	if (!proc_create_net_single("xfrm_stat", 0444, net->proc_net,
			 xfrm_statistics_seq_show, NULL))
		return -ENOMEM;
	return 0;
}

void xfrm_proc_fini(struct net *net)
{
	remove_proc_entry("xfrm_stat", net->proc_net);
}
