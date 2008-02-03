/*
 * xfrm_proc.c
 *
 * Copyright (C)2006-2007 USAGI/WIDE Project
 *
 * Authors:	Masahide NAKAMURA <nakam@linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/snmp.h>
#include <net/xfrm.h>

static struct snmp_mib xfrm_mib_list[] = {
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
	SNMP_MIB_SENTINEL
};

static unsigned long
fold_field(void *mib[], int offt)
{
        unsigned long res = 0;
        int i;

        for_each_possible_cpu(i) {
                res += *(((unsigned long *)per_cpu_ptr(mib[0], i)) + offt);
                res += *(((unsigned long *)per_cpu_ptr(mib[1], i)) + offt);
        }
        return res;
}

static int xfrm_statistics_seq_show(struct seq_file *seq, void *v)
{
	int i;
	for (i=0; xfrm_mib_list[i].name; i++)
		seq_printf(seq, "%-24s\t%lu\n", xfrm_mib_list[i].name,
			   fold_field((void **)xfrm_statistics,
				      xfrm_mib_list[i].entry));
	return 0;
}

static int xfrm_statistics_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, xfrm_statistics_seq_show, NULL);
}

static struct file_operations xfrm_statistics_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = xfrm_statistics_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

int __init xfrm_proc_init(void)
{
	int rc = 0;

	if (!proc_net_fops_create(&init_net, "xfrm_stat", S_IRUGO,
				  &xfrm_statistics_seq_fops))
		goto stat_fail;

 out:
	return rc;

 stat_fail:
	rc = -ENOMEM;
	goto out;
}
