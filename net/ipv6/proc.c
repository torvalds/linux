// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  This is very similar to the IPv4 version,
 *		except it reports the sockets in the INET6 address family.
 *
 * Authors:	David S. Miller (davem@caip.rutgers.edu)
 *		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 */
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stddef.h>
#include <linux/export.h>
#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/transp_v6.h>
#include <net/ipv6.h>

#define MAX4(a, b, c, d) \
	MAX_T(u32, MAX_T(u32, a, b), MAX_T(u32, c, d))
#define SNMP_MIB_MAX MAX4(UDP_MIB_MAX, TCP_MIB_MAX, \
			IPSTATS_MIB_MAX, ICMP_MIB_MAX)

static int sockstat6_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = seq->private;

	seq_printf(seq, "TCP6: inuse %d\n",
		       sock_prot_inuse_get(net, &tcpv6_prot));
	seq_printf(seq, "UDP6: inuse %d\n",
		       sock_prot_inuse_get(net, &udpv6_prot));
	seq_printf(seq, "UDPLITE6: inuse %d\n",
			sock_prot_inuse_get(net, &udplitev6_prot));
	seq_printf(seq, "RAW6: inuse %d\n",
		       sock_prot_inuse_get(net, &rawv6_prot));
	seq_printf(seq, "FRAG6: inuse %u memory %lu\n",
		   atomic_read(&net->ipv6.fqdir->rhashtable.nelems),
		   frag_mem_limit(net->ipv6.fqdir));
	return 0;
}

static const struct snmp_mib snmp6_ipstats_list[] = {
/* ipv6 mib according to RFC 2465 */
	SNMP_MIB_ITEM("Ip6InReceives", IPSTATS_MIB_INPKTS),
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
	SNMP_MIB_ITEM("Ip6InOctets", IPSTATS_MIB_INOCTETS),
	SNMP_MIB_ITEM("Ip6OutOctets", IPSTATS_MIB_OUTOCTETS),
	SNMP_MIB_ITEM("Ip6InMcastOctets", IPSTATS_MIB_INMCASTOCTETS),
	SNMP_MIB_ITEM("Ip6OutMcastOctets", IPSTATS_MIB_OUTMCASTOCTETS),
	SNMP_MIB_ITEM("Ip6InBcastOctets", IPSTATS_MIB_INBCASTOCTETS),
	SNMP_MIB_ITEM("Ip6OutBcastOctets", IPSTATS_MIB_OUTBCASTOCTETS),
	/* IPSTATS_MIB_CSUMERRORS is not relevant in IPv6 (no checksum) */
	SNMP_MIB_ITEM("Ip6InNoECTPkts", IPSTATS_MIB_NOECTPKTS),
	SNMP_MIB_ITEM("Ip6InECT1Pkts", IPSTATS_MIB_ECT1PKTS),
	SNMP_MIB_ITEM("Ip6InECT0Pkts", IPSTATS_MIB_ECT0PKTS),
	SNMP_MIB_ITEM("Ip6InCEPkts", IPSTATS_MIB_CEPKTS),
	SNMP_MIB_ITEM("Ip6OutTransmits", IPSTATS_MIB_OUTPKTS),
};

static const struct snmp_mib snmp6_icmp6_list[] = {
/* icmpv6 mib according to RFC 2466 */
	SNMP_MIB_ITEM("Icmp6InMsgs", ICMP6_MIB_INMSGS),
	SNMP_MIB_ITEM("Icmp6InErrors", ICMP6_MIB_INERRORS),
	SNMP_MIB_ITEM("Icmp6OutMsgs", ICMP6_MIB_OUTMSGS),
	SNMP_MIB_ITEM("Icmp6OutErrors", ICMP6_MIB_OUTERRORS),
	SNMP_MIB_ITEM("Icmp6InCsumErrors", ICMP6_MIB_CSUMERRORS),
/* ICMP6_MIB_RATELIMITHOST needs to be last, see snmp6_dev_seq_show(). */
	SNMP_MIB_ITEM("Icmp6OutRateLimitHost", ICMP6_MIB_RATELIMITHOST),
};

static const struct snmp_mib snmp6_udp6_list[] = {
	SNMP_MIB_ITEM("Udp6InDatagrams", UDP_MIB_INDATAGRAMS),
	SNMP_MIB_ITEM("Udp6NoPorts", UDP_MIB_NOPORTS),
	SNMP_MIB_ITEM("Udp6InErrors", UDP_MIB_INERRORS),
	SNMP_MIB_ITEM("Udp6OutDatagrams", UDP_MIB_OUTDATAGRAMS),
	SNMP_MIB_ITEM("Udp6RcvbufErrors", UDP_MIB_RCVBUFERRORS),
	SNMP_MIB_ITEM("Udp6SndbufErrors", UDP_MIB_SNDBUFERRORS),
	SNMP_MIB_ITEM("Udp6InCsumErrors", UDP_MIB_CSUMERRORS),
	SNMP_MIB_ITEM("Udp6IgnoredMulti", UDP_MIB_IGNOREDMULTI),
	SNMP_MIB_ITEM("Udp6MemErrors", UDP_MIB_MEMERRORS),
};

static const struct snmp_mib snmp6_udplite6_list[] = {
	SNMP_MIB_ITEM("UdpLite6InDatagrams", UDP_MIB_INDATAGRAMS),
	SNMP_MIB_ITEM("UdpLite6NoPorts", UDP_MIB_NOPORTS),
	SNMP_MIB_ITEM("UdpLite6InErrors", UDP_MIB_INERRORS),
	SNMP_MIB_ITEM("UdpLite6OutDatagrams", UDP_MIB_OUTDATAGRAMS),
	SNMP_MIB_ITEM("UdpLite6RcvbufErrors", UDP_MIB_RCVBUFERRORS),
	SNMP_MIB_ITEM("UdpLite6SndbufErrors", UDP_MIB_SNDBUFERRORS),
	SNMP_MIB_ITEM("UdpLite6InCsumErrors", UDP_MIB_CSUMERRORS),
	SNMP_MIB_ITEM("UdpLite6MemErrors", UDP_MIB_MEMERRORS),
};

static void snmp6_seq_show_icmpv6msg(struct seq_file *seq, atomic_long_t *smib)
{
	char name[32];
	int i;

	/* print by name -- deprecated items */
	for (i = 0; i < ICMP6MSG_MIB_MAX; i++) {
		const char *p = NULL;
		int icmptype;

#define CASE(TYP, STR) case TYP: p = STR; break;

		icmptype = i & 0xff;
		switch (icmptype) {
/* RFC 4293 v6 ICMPMsgStatsTable; named items for RFC 2466 compatibility */
		CASE(ICMPV6_DEST_UNREACH,	"DestUnreachs")
		CASE(ICMPV6_PKT_TOOBIG,		"PktTooBigs")
		CASE(ICMPV6_TIME_EXCEED,	"TimeExcds")
		CASE(ICMPV6_PARAMPROB,		"ParmProblems")
		CASE(ICMPV6_ECHO_REQUEST,	"Echos")
		CASE(ICMPV6_ECHO_REPLY,		"EchoReplies")
		CASE(ICMPV6_MGM_QUERY,		"GroupMembQueries")
		CASE(ICMPV6_MGM_REPORT,		"GroupMembResponses")
		CASE(ICMPV6_MGM_REDUCTION,	"GroupMembReductions")
		CASE(ICMPV6_MLD2_REPORT,	"MLDv2Reports")
		CASE(NDISC_ROUTER_ADVERTISEMENT, "RouterAdvertisements")
		CASE(NDISC_ROUTER_SOLICITATION, "RouterSolicits")
		CASE(NDISC_NEIGHBOUR_ADVERTISEMENT, "NeighborAdvertisements")
		CASE(NDISC_NEIGHBOUR_SOLICITATION, "NeighborSolicits")
		CASE(NDISC_REDIRECT,		"Redirects")
		}
#undef CASE
		if (!p)	/* don't print un-named types here */
			continue;
		snprintf(name, sizeof(name), "Icmp6%s%s",
			i & 0x100 ? "Out" : "In", p);
		seq_printf(seq, "%-32s\t%lu\n", name,
			   atomic_long_read(smib + i));
	}

	/* print by number (nonzero only) - ICMPMsgStat format */
	for (i = 0; i < ICMP6MSG_MIB_MAX; i++) {
		unsigned long val;

		val = atomic_long_read(smib + i);
		if (!val)
			continue;
		snprintf(name, sizeof(name), "Icmp6%sType%u",
			i & 0x100 ?  "Out" : "In", i & 0xff);
		seq_printf(seq, "%-32s\t%lu\n", name, val);
	}
}

/* can be called either with percpu mib (pcpumib != NULL),
 * or shared one (smib != NULL)
 */
static void snmp6_seq_show_item(struct seq_file *seq, void __percpu *pcpumib,
				atomic_long_t *smib,
				const struct snmp_mib *itemlist,
				int cnt)
{
	unsigned long buff[SNMP_MIB_MAX];
	int i;

	if (pcpumib) {
		memset(buff, 0, sizeof(unsigned long) * cnt);

		snmp_get_cpu_field_batch_cnt(buff, itemlist, cnt, pcpumib);
		for (i = 0; i < cnt; i++)
			seq_printf(seq, "%-32s\t%lu\n",
				   itemlist[i].name, buff[i]);
	} else {
		for (i = 0; i < cnt; i++)
			seq_printf(seq, "%-32s\t%lu\n", itemlist[i].name,
				   atomic_long_read(smib + itemlist[i].entry));
	}
}

static void snmp6_seq_show_item64(struct seq_file *seq, void __percpu *mib,
				  const struct snmp_mib *itemlist,
				  int cnt, size_t syncpoff)
{
	u64 buff64[SNMP_MIB_MAX];
	int i;

	memset(buff64, 0, sizeof(u64) * cnt);

	snmp_get_cpu_field64_batch_cnt(buff64, itemlist, cnt, mib, syncpoff);
	for (i = 0; i < cnt; i++)
		seq_printf(seq, "%-32s\t%llu\n", itemlist[i].name, buff64[i]);
}

static int snmp6_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = (struct net *)seq->private;

	snmp6_seq_show_item64(seq, net->mib.ipv6_statistics,
			      snmp6_ipstats_list,
			      ARRAY_SIZE(snmp6_ipstats_list),
			      offsetof(struct ipstats_mib, syncp));
	snmp6_seq_show_item(seq, net->mib.icmpv6_statistics,
			    NULL, snmp6_icmp6_list,
			    ARRAY_SIZE(snmp6_icmp6_list));
	snmp6_seq_show_icmpv6msg(seq, net->mib.icmpv6msg_statistics->mibs);
	snmp6_seq_show_item(seq, net->mib.udp_stats_in6,
			    NULL, snmp6_udp6_list,
			    ARRAY_SIZE(snmp6_udp6_list));
	snmp6_seq_show_item(seq, net->mib.udplite_stats_in6,
			    NULL, snmp6_udplite6_list,
			    ARRAY_SIZE(snmp6_udplite6_list));
	return 0;
}

static int snmp6_dev_seq_show(struct seq_file *seq, void *v)
{
	struct inet6_dev *idev = (struct inet6_dev *)seq->private;

	seq_printf(seq, "%-32s\t%u\n", "ifIndex", idev->dev->ifindex);
	snmp6_seq_show_item64(seq, idev->stats.ipv6,
			      snmp6_ipstats_list,
			      ARRAY_SIZE(snmp6_ipstats_list),
			      offsetof(struct ipstats_mib, syncp));

	/* Per idev icmp stats do not have ICMP6_MIB_RATELIMITHOST */
	snmp6_seq_show_item(seq, NULL, idev->stats.icmpv6dev->mibs,
			    snmp6_icmp6_list, ARRAY_SIZE(snmp6_icmp6_list) - 1);

	snmp6_seq_show_icmpv6msg(seq, idev->stats.icmpv6msgdev->mibs);
	return 0;
}

int snmp6_register_dev(struct inet6_dev *idev)
{
	struct proc_dir_entry *p;
	struct net *net;

	if (!idev || !idev->dev)
		return -EINVAL;

	net = dev_net(idev->dev);
	if (!net->mib.proc_net_devsnmp6)
		return -ENOENT;

	p = proc_create_single_data(idev->dev->name, 0444,
			net->mib.proc_net_devsnmp6, snmp6_dev_seq_show, idev);
	if (!p)
		return -ENOMEM;

	idev->stats.proc_dir_entry = p;
	return 0;
}

int snmp6_unregister_dev(struct inet6_dev *idev)
{
	struct net *net = dev_net(idev->dev);
	if (!net->mib.proc_net_devsnmp6)
		return -ENOENT;
	if (!idev->stats.proc_dir_entry)
		return -EINVAL;
	proc_remove(idev->stats.proc_dir_entry);
	idev->stats.proc_dir_entry = NULL;
	return 0;
}

static int __net_init ipv6_proc_init_net(struct net *net)
{
	if (!proc_create_net_single("sockstat6", 0444, net->proc_net,
			sockstat6_seq_show, NULL))
		return -ENOMEM;

	if (!proc_create_net_single("snmp6", 0444, net->proc_net,
			snmp6_seq_show, NULL))
		goto proc_snmp6_fail;

	net->mib.proc_net_devsnmp6 = proc_mkdir("dev_snmp6", net->proc_net);
	if (!net->mib.proc_net_devsnmp6)
		goto proc_dev_snmp6_fail;
	return 0;

proc_dev_snmp6_fail:
	remove_proc_entry("snmp6", net->proc_net);
proc_snmp6_fail:
	remove_proc_entry("sockstat6", net->proc_net);
	return -ENOMEM;
}

static void __net_exit ipv6_proc_exit_net(struct net *net)
{
	remove_proc_entry("sockstat6", net->proc_net);
	remove_proc_entry("dev_snmp6", net->proc_net);
	remove_proc_entry("snmp6", net->proc_net);
}

static struct pernet_operations ipv6_proc_ops = {
	.init = ipv6_proc_init_net,
	.exit = ipv6_proc_exit_net,
};

int __init ipv6_misc_proc_init(void)
{
	return register_pernet_subsys(&ipv6_proc_ops);
}

void ipv6_misc_proc_exit(void)
{
	unregister_pernet_subsys(&ipv6_proc_ops);
}
