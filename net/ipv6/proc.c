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
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/transp_v6.h>
#include <net/ipv6.h>

static struct proc_dir_entry *proc_net_devsnmp6;

static int sockstat6_seq_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "TCP6: inuse %d\n",
		       sock_prot_inuse_get(&tcpv6_prot));
	seq_printf(seq, "UDP6: inuse %d\n",
		       sock_prot_inuse_get(&udpv6_prot));
	seq_printf(seq, "UDPLITE6: inuse %d\n",
			sock_prot_inuse_get(&udplitev6_prot));
	seq_printf(seq, "RAW6: inuse %d\n",
		       sock_prot_inuse_get(&rawv6_prot));
	seq_printf(seq, "FRAG6: inuse %d memory %d\n",
		       ip6_frag_nqueues(&init_net), ip6_frag_mem(&init_net));
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
/* icmpv6 mib according to RFC 2466 */
	SNMP_MIB_ITEM("Icmp6InMsgs", ICMP6_MIB_INMSGS),
	SNMP_MIB_ITEM("Icmp6InErrors", ICMP6_MIB_INERRORS),
	SNMP_MIB_ITEM("Icmp6OutMsgs", ICMP6_MIB_OUTMSGS),
	SNMP_MIB_SENTINEL
};

/* RFC 4293 v6 ICMPMsgStatsTable; named items for RFC 2466 compatibility */
static char *icmp6type2name[256] = {
	[ICMPV6_DEST_UNREACH] = "DestUnreachs",
	[ICMPV6_PKT_TOOBIG] = "PktTooBigs",
	[ICMPV6_TIME_EXCEED] = "TimeExcds",
	[ICMPV6_PARAMPROB] = "ParmProblems",
	[ICMPV6_ECHO_REQUEST] = "Echos",
	[ICMPV6_ECHO_REPLY] = "EchoReplies",
	[ICMPV6_MGM_QUERY] = "GroupMembQueries",
	[ICMPV6_MGM_REPORT] = "GroupMembResponses",
	[ICMPV6_MGM_REDUCTION] = "GroupMembReductions",
	[ICMPV6_MLD2_REPORT] = "MLDv2Reports",
	[NDISC_ROUTER_ADVERTISEMENT] = "RouterAdvertisements",
	[NDISC_ROUTER_SOLICITATION] = "RouterSolicits",
	[NDISC_NEIGHBOUR_ADVERTISEMENT] = "NeighborAdvertisements",
	[NDISC_NEIGHBOUR_SOLICITATION] = "NeighborSolicits",
	[NDISC_REDIRECT] = "Redirects",
};


static struct snmp_mib snmp6_udp6_list[] = {
	SNMP_MIB_ITEM("Udp6InDatagrams", UDP_MIB_INDATAGRAMS),
	SNMP_MIB_ITEM("Udp6NoPorts", UDP_MIB_NOPORTS),
	SNMP_MIB_ITEM("Udp6InErrors", UDP_MIB_INERRORS),
	SNMP_MIB_ITEM("Udp6OutDatagrams", UDP_MIB_OUTDATAGRAMS),
	SNMP_MIB_SENTINEL
};

static struct snmp_mib snmp6_udplite6_list[] = {
	SNMP_MIB_ITEM("UdpLite6InDatagrams", UDP_MIB_INDATAGRAMS),
	SNMP_MIB_ITEM("UdpLite6NoPorts", UDP_MIB_NOPORTS),
	SNMP_MIB_ITEM("UdpLite6InErrors", UDP_MIB_INERRORS),
	SNMP_MIB_ITEM("UdpLite6OutDatagrams", UDP_MIB_OUTDATAGRAMS),
	SNMP_MIB_SENTINEL
};

static void snmp6_seq_show_icmpv6msg(struct seq_file *seq, void **mib)
{
	static char name[32];
	int i;

	/* print by name -- deprecated items */
	for (i = 0; i < ICMP6MSG_MIB_MAX; i++) {
		int icmptype;
		char *p;

		icmptype = i & 0xff;
		p = icmp6type2name[icmptype];
		if (!p)	/* don't print un-named types here */
			continue;
		(void) snprintf(name, sizeof(name)-1, "Icmp6%s%s",
			i & 0x100 ? "Out" : "In", p);
		seq_printf(seq, "%-32s\t%lu\n", name,
			snmp_fold_field(mib, i));
	}

	/* print by number (nonzero only) - ICMPMsgStat format */
	for (i = 0; i < ICMP6MSG_MIB_MAX; i++) {
		unsigned long val;

		val = snmp_fold_field(mib, i);
		if (!val)
			continue;
		(void) snprintf(name, sizeof(name)-1, "Icmp6%sType%u",
			i & 0x100 ?  "Out" : "In", i & 0xff);
		seq_printf(seq, "%-32s\t%lu\n", name, val);
	}
	return;
}

static inline void
snmp6_seq_show_item(struct seq_file *seq, void **mib, struct snmp_mib *itemlist)
{
	int i;
	for (i=0; itemlist[i].name; i++)
		seq_printf(seq, "%-32s\t%lu\n", itemlist[i].name,
			   snmp_fold_field(mib, itemlist[i].entry));
}

static int snmp6_seq_show(struct seq_file *seq, void *v)
{
	struct inet6_dev *idev = (struct inet6_dev *)seq->private;

	if (idev) {
		seq_printf(seq, "%-32s\t%u\n", "ifIndex", idev->dev->ifindex);
		snmp6_seq_show_item(seq, (void **)idev->stats.ipv6, snmp6_ipstats_list);
		snmp6_seq_show_item(seq, (void **)idev->stats.icmpv6, snmp6_icmp6_list);
		snmp6_seq_show_icmpv6msg(seq, (void **)idev->stats.icmpv6msg);
	} else {
		snmp6_seq_show_item(seq, (void **)ipv6_statistics, snmp6_ipstats_list);
		snmp6_seq_show_item(seq, (void **)icmpv6_statistics, snmp6_icmp6_list);
		snmp6_seq_show_icmpv6msg(seq, (void **)icmpv6msg_statistics);
		snmp6_seq_show_item(seq, (void **)udp_stats_in6, snmp6_udp6_list);
		snmp6_seq_show_item(seq, (void **)udplite_stats_in6, snmp6_udplite6_list);
	}
	return 0;
}

static int sockstat6_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sockstat6_seq_show, NULL);
}

static const struct file_operations sockstat6_seq_fops = {
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

static const struct file_operations snmp6_seq_fops = {
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

	p = proc_create(idev->dev->name, S_IRUGO,
			proc_net_devsnmp6, &snmp6_seq_fops);
	if (!p)
		return -ENOMEM;

	p->data = idev;

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
	idev->stats.proc_dir_entry = NULL;
	return 0;
}

int __init ipv6_misc_proc_init(void)
{
	int rc = 0;

	if (!proc_net_fops_create(&init_net, "snmp6", S_IRUGO, &snmp6_seq_fops))
		goto proc_snmp6_fail;

	proc_net_devsnmp6 = proc_mkdir("dev_snmp6", init_net.proc_net);
	if (!proc_net_devsnmp6)
		goto proc_dev_snmp6_fail;

	if (!proc_net_fops_create(&init_net, "sockstat6", S_IRUGO, &sockstat6_seq_fops))
		goto proc_sockstat6_fail;
out:
	return rc;

proc_sockstat6_fail:
	proc_net_remove(&init_net, "dev_snmp6");
proc_dev_snmp6_fail:
	proc_net_remove(&init_net, "snmp6");
proc_snmp6_fail:
	rc = -ENOMEM;
	goto out;
}

void ipv6_misc_proc_exit(void)
{
	proc_net_remove(&init_net, "sockstat6");
	proc_net_remove(&init_net, "dev_snmp6");
	proc_net_remove(&init_net, "snmp6");
}

