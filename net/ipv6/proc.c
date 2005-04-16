/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  This is very similar to the IPv4 version,
 *		except it reports the sockets in the INET6 address family.
 *
 * Version:	$Id: proc.c,v 1.17 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	David S. Miller (davem@caip.rutgers.edu)
 * 		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/transp_v6.h>
#include <net/ipv6.h>

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_net_devsnmp6;

static int fold_prot_inuse(struct proto *proto)
{
	int res = 0;
	int cpu;

	for (cpu=0; cpu<NR_CPUS; cpu++)
		res += proto->stats[cpu].inuse;

	return res;
}

static int sockstat6_seq_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "TCP6: inuse %d\n",
		       fold_prot_inuse(&tcpv6_prot));
	seq_printf(seq, "UDP6: inuse %d\n",
		       fold_prot_inuse(&udpv6_prot));
	seq_printf(seq, "RAW6: inuse %d\n",
		       fold_prot_inuse(&rawv6_prot));
	seq_printf(seq, "FRAG6: inuse %d memory %d\n",
		       ip6_frag_nqueues, atomic_read(&ip6_frag_mem));
	return 0;
}

static struct snmp_mib snmp6_ipstats_list[] = {
/* ipv6 mib according to RFC 2465 */
	SNMP_MIB_ITEM("Ip6InReceives", IPSTATS_MIB_INRECEIVES),
	SNMP_MIB_ITEM("Ip6InHdrErrors", IPSTATS_MIB_INHDRERRORS),
	SNMP_MIB_ITEM("Ip6InTooBigErrors", IPSTATS_MIB_INTOOBIGERRORS),
	SNMP_MIB_ITEM("Ip6InNoRoutes", IPSTATS_MIB_INNOROUTES),
	SNMP_MIB_ITEM("Ip6InAddrErrors", IPSTATS_MIB_INADDRERRORS),
	SNMP_MIB_ITEM("Ip6InUnknownProtos", IPSTATS_MIB_INUNKNOWNPROTOS),
	SNMP_MIB_ITEM("Ip6InTruncatedPkts", IPSTATS_MIB_INTRUNCATEDPKTS),
	SNMP_MIB_ITEM("Ip6InDiscards", IPSTATS_MIB_INDISCARDS),
	SNMP_MIB_ITEM("Ip6InDelivers", IPSTATS_MIB_INDELIVERS),
	SNMP_MIB_ITEM("Ip6OutForwDatagrams", IPSTATS_MIB_OUTFORWDATAGRAMS),
	SNMP_MIB_ITEM("Ip6OutRequests", IPSTATS_MIB_OUTREQUESTS),
	SNMP_MIB_ITEM("Ip6OutDiscards", IPSTATS_MIB_OUTDISCARDS),
	SNMP_MIB_ITEM("Ip6OutNoRoutes", IPSTATS_MIB_OUTNOROUTES),
	SNMP_MIB_ITEM("Ip6ReasmTimeout", IPSTATS_MIB_REASMTIMEOUT),
	SNMP_MIB_ITEM("Ip6ReasmReqds", IPSTATS_MIB_REASMREQDS),
	SNMP_MIB_ITEM("Ip6ReasmOKs", IPSTATS_MIB_REASMOKS),
	SNMP_MIB_ITEM("Ip6ReasmFails", IPSTATS_MIB_REASMFAILS),
	SNMP_MIB_ITEM("Ip6FragOKs", IPSTATS_MIB_FRAGOKS),
	SNMP_MIB_ITEM("Ip6FragFails", IPSTATS_MIB_FRAGFAILS),
	SNMP_MIB_ITEM("Ip6FragCreates", IPSTATS_MIB_FRAGCREATES),
	SNMP_MIB_ITEM("Ip6InMcastPkts", IPSTATS_MIB_INMCASTPKTS),
	SNMP_MIB_ITEM("Ip6OutMcastPkts", IPSTATS_MIB_OUTMCASTPKTS),
	SNMP_MIB_SENTINEL
};

static struct snmp_mib snmp6_icmp6_list[] = {
/* icmpv6 mib according to RFC 2466

   Exceptions:  {In|Out}AdminProhibs are removed, because I see
                no good reasons to account them separately
		of another dest.unreachs.
		OutErrs is zero identically.
		OutEchos too.
		OutRouterAdvertisements too.
		OutGroupMembQueries too.
 */
	SNMP_MIB_ITEM("Icmp6InMsgs", ICMP6_MIB_INMSGS),
	SNMP_MIB_ITEM("Icmp6InErrors", ICMP6_MIB_INERRORS),
	SNMP_MIB_ITEM("Icmp6InDestUnreachs", ICMP6_MIB_INDESTUNREACHS),
	SNMP_MIB_ITEM("Icmp6InPktTooBigs", ICMP6_MIB_INPKTTOOBIGS),
	SNMP_MIB_ITEM("Icmp6InTimeExcds", ICMP6_MIB_INTIMEEXCDS),
	SNMP_MIB_ITEM("Icmp6InParmProblems", ICMP6_MIB_INPARMPROBLEMS),
	SNMP_MIB_ITEM("Icmp6InEchos", ICMP6_MIB_INECHOS),
	SNMP_MIB_ITEM("Icmp6InEchoReplies", ICMP6_MIB_INECHOREPLIES),
	SNMP_MIB_ITEM("Icmp6InGroupMembQueries", ICMP6_MIB_INGROUPMEMBQUERIES),
	SNMP_MIB_ITEM("Icmp6InGroupMembResponses", ICMP6_MIB_INGROUPMEMBRESPONSES),
	SNMP_MIB_ITEM("Icmp6InGroupMembReductions", ICMP6_MIB_INGROUPMEMBREDUCTIONS),
	SNMP_MIB_ITEM("Icmp6InRouterSolicits", ICMP6_MIB_INROUTERSOLICITS),
	SNMP_MIB_ITEM("Icmp6InRouterAdvertisements", ICMP6_MIB_INROUTERADVERTISEMENTS),
	SNMP_MIB_ITEM("Icmp6InNeighborSolicits", ICMP6_MIB_INNEIGHBORSOLICITS),
	SNMP_MIB_ITEM("Icmp6InNeighborAdvertisements", ICMP6_MIB_INNEIGHBORADVERTISEMENTS),
	SNMP_MIB_ITEM("Icmp6InRedirects", ICMP6_MIB_INREDIRECTS),
	SNMP_MIB_ITEM("Icmp6OutMsgs", ICMP6_MIB_OUTMSGS),
	SNMP_MIB_ITEM("Icmp6OutDestUnreachs", ICMP6_MIB_OUTDESTUNREACHS),
	SNMP_MIB_ITEM("Icmp6OutPktTooBigs", ICMP6_MIB_OUTPKTTOOBIGS),
	SNMP_MIB_ITEM("Icmp6OutTimeExcds", ICMP6_MIB_OUTTIMEEXCDS),
	SNMP_MIB_ITEM("Icmp6OutParmProblems", ICMP6_MIB_OUTPARMPROBLEMS),
	SNMP_MIB_ITEM("Icmp6OutEchoReplies", ICMP6_MIB_OUTECHOREPLIES),
	SNMP_MIB_ITEM("Icmp6OutRouterSolicits", ICMP6_MIB_OUTROUTERSOLICITS),
	SNMP_MIB_ITEM("Icmp6OutNeighborSolicits", ICMP6_MIB_OUTNEIGHBORSOLICITS),
	SNMP_MIB_ITEM("Icmp6OutNeighborAdvertisements", ICMP6_MIB_OUTNEIGHBORADVERTISEMENTS),
	SNMP_MIB_ITEM("Icmp6OutRedirects", ICMP6_MIB_OUTREDIRECTS),
	SNMP_MIB_ITEM("Icmp6OutGroupMembResponses", ICMP6_MIB_OUTGROUPMEMBRESPONSES),
	SNMP_MIB_ITEM("Icmp6OutGroupMembReductions", ICMP6_MIB_OUTGROUPMEMBREDUCTIONS),
	SNMP_MIB_SENTINEL
};

static struct snmp_mib snmp6_udp6_list[] = {
	SNMP_MIB_ITEM("Udp6InDatagrams", UDP_MIB_INDATAGRAMS),
	SNMP_MIB_ITEM("Udp6NoPorts", UDP_MIB_NOPORTS),
	SNMP_MIB_ITEM("Udp6InErrors", UDP_MIB_INERRORS),
	SNMP_MIB_ITEM("Udp6OutDatagrams", UDP_MIB_OUTDATAGRAMS),
	SNMP_MIB_SENTINEL
};

static unsigned long
fold_field(void *mib[], int offt)
{
        unsigned long res = 0;
        int i;
 
        for (i = 0; i < NR_CPUS; i++) {
                if (!cpu_possible(i))
                        continue;
                res += *(((unsigned long *)per_cpu_ptr(mib[0], i)) + offt);
                res += *(((unsigned long *)per_cpu_ptr(mib[1], i)) + offt);
        }
        return res;
}

static inline void
snmp6_seq_show_item(struct seq_file *seq, void **mib, struct snmp_mib *itemlist)
{
	int i;
	for (i=0; itemlist[i].name; i++)
		seq_printf(seq, "%-32s\t%lu\n", itemlist[i].name, 
				fold_field(mib, itemlist[i].entry));
}

static int snmp6_seq_show(struct seq_file *seq, void *v)
{
	struct inet6_dev *idev = (struct inet6_dev *)seq->private;

	if (idev) {
		seq_printf(seq, "%-32s\t%u\n", "ifIndex", idev->dev->ifindex);
		snmp6_seq_show_item(seq, (void **)idev->stats.icmpv6, snmp6_icmp6_list);
	} else {
		snmp6_seq_show_item(seq, (void **)ipv6_statistics, snmp6_ipstats_list);
		snmp6_seq_show_item(seq, (void **)icmpv6_statistics, snmp6_icmp6_list);
		snmp6_seq_show_item(seq, (void **)udp_stats_in6, snmp6_udp6_list);
	}
	return 0;
}

static int sockstat6_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sockstat6_seq_show, NULL);
}

static struct file_operations sockstat6_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = sockstat6_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

static int snmp6_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, snmp6_seq_show, PDE(inode)->data);
}

static struct file_operations snmp6_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = snmp6_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

int snmp6_register_dev(struct inet6_dev *idev)
{
	struct proc_dir_entry *p;

	if (!idev || !idev->dev)
		return -EINVAL;

	if (!proc_net_devsnmp6)
		return -ENOENT;

	p = create_proc_entry(idev->dev->name, S_IRUGO, proc_net_devsnmp6);
	if (!p)
		return -ENOMEM;

	p->data = idev;
	p->proc_fops = &snmp6_seq_fops;

	idev->stats.proc_dir_entry = p;
	return 0;
}

int snmp6_unregister_dev(struct inet6_dev *idev)
{
	if (!proc_net_devsnmp6)
		return -ENOENT;
	if (!idev || !idev->stats.proc_dir_entry)
		return -EINVAL;
	remove_proc_entry(idev->stats.proc_dir_entry->name,
			  proc_net_devsnmp6);
	return 0;
}

int __init ipv6_misc_proc_init(void)
{
	int rc = 0;

	if (!proc_net_fops_create("snmp6", S_IRUGO, &snmp6_seq_fops))
		goto proc_snmp6_fail;

	proc_net_devsnmp6 = proc_mkdir("dev_snmp6", proc_net);
	if (!proc_net_devsnmp6)
		goto proc_dev_snmp6_fail;

	if (!proc_net_fops_create("sockstat6", S_IRUGO, &sockstat6_seq_fops))
		goto proc_sockstat6_fail;
out:
	return rc;

proc_sockstat6_fail:
	proc_net_remove("dev_snmp6");
proc_dev_snmp6_fail:
	proc_net_remove("snmp6");
proc_snmp6_fail:
	rc = -ENOMEM;
	goto out;
}

void ipv6_misc_proc_exit(void)
{
	proc_net_remove("sockstat6");
	proc_net_remove("dev_snmp6");
	proc_net_remove("snmp6");
}

#else	/* CONFIG_PROC_FS */


int snmp6_register_dev(struct inet6_dev *idev)
{
	return 0;
}

int snmp6_unregister_dev(struct inet6_dev *idev)
{
	return 0;
}
#endif	/* CONFIG_PROC_FS */

int snmp6_alloc_dev(struct inet6_dev *idev)
{
	int err = -ENOMEM;

	if (!idev || !idev->dev)
		return -EINVAL;

	if (snmp6_mib_init((void **)idev->stats.icmpv6, sizeof(struct icmpv6_mib),
			   __alignof__(struct icmpv6_mib)) < 0)
		goto err_icmp;

	return 0;

err_icmp:
	return err;
}

int snmp6_free_dev(struct inet6_dev *idev)
{
	snmp6_mib_free((void **)idev->stats.icmpv6);
	return 0;
}


