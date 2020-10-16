// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/seq_file.h>
#include <net/ip.h>
#include <net/mptcp.h>
#include <net/snmp.h>
#include <net/net_namespace.h>

#include "mib.h"

static const struct snmp_mib mptcp_snmp_list[] = {
	SNMP_MIB_ITEM("MPCapableSYNRX", MPTCP_MIB_MPCAPABLEPASSIVE),
	SNMP_MIB_ITEM("MPCapableACKRX", MPTCP_MIB_MPCAPABLEPASSIVEACK),
	SNMP_MIB_ITEM("MPCapableFallbackACK", MPTCP_MIB_MPCAPABLEPASSIVEFALLBACK),
	SNMP_MIB_ITEM("MPCapableFallbackSYNACK", MPTCP_MIB_MPCAPABLEACTIVEFALLBACK),
	SNMP_MIB_ITEM("MPTCPRetrans", MPTCP_MIB_RETRANSSEGS),
	SNMP_MIB_ITEM("MPJoinNoTokenFound", MPTCP_MIB_JOINNOTOKEN),
	SNMP_MIB_ITEM("MPJoinSynRx", MPTCP_MIB_JOINSYNRX),
	SNMP_MIB_ITEM("MPJoinSynAckRx", MPTCP_MIB_JOINSYNACKRX),
	SNMP_MIB_ITEM("MPJoinSynAckHMacFailure", MPTCP_MIB_JOINSYNACKMAC),
	SNMP_MIB_ITEM("MPJoinAckRx", MPTCP_MIB_JOINACKRX),
	SNMP_MIB_ITEM("MPJoinAckHMacFailure", MPTCP_MIB_JOINACKMAC),
	SNMP_MIB_ITEM("DSSNotMatching", MPTCP_MIB_DSSNOMATCH),
	SNMP_MIB_ITEM("InfiniteMapRx", MPTCP_MIB_INFINITEMAPRX),
	SNMP_MIB_ITEM("OFOQueueTail", MPTCP_MIB_OFOQUEUETAIL),
	SNMP_MIB_ITEM("OFOQueue", MPTCP_MIB_OFOQUEUE),
	SNMP_MIB_ITEM("OFOMerge", MPTCP_MIB_OFOMERGE),
	SNMP_MIB_ITEM("NoDSSInWindow", MPTCP_MIB_NODSSWINDOW),
	SNMP_MIB_ITEM("DuplicateData", MPTCP_MIB_DUPDATA),
	SNMP_MIB_ITEM("AddAddr", MPTCP_MIB_ADDADDR),
	SNMP_MIB_ITEM("EchoAdd", MPTCP_MIB_ECHOADD),
	SNMP_MIB_ITEM("RmAddr", MPTCP_MIB_RMADDR),
	SNMP_MIB_ITEM("RmSubflow", MPTCP_MIB_RMSUBFLOW),
	SNMP_MIB_SENTINEL
};

/* mptcp_mib_alloc - allocate percpu mib counters
 *
 * These are allocated when the first mptcp socket is created so
 * we do not waste percpu memory if mptcp isn't in use.
 */
bool mptcp_mib_alloc(struct net *net)
{
	struct mptcp_mib __percpu *mib = alloc_percpu(struct mptcp_mib);

	if (!mib)
		return false;

	if (cmpxchg(&net->mib.mptcp_statistics, NULL, mib))
		free_percpu(mib);

	return true;
}

void mptcp_seq_show(struct seq_file *seq)
{
	struct net *net = seq->private;
	int i;

	seq_puts(seq, "MPTcpExt:");
	for (i = 0; mptcp_snmp_list[i].name; i++)
		seq_printf(seq, " %s", mptcp_snmp_list[i].name);

	seq_puts(seq, "\nMPTcpExt:");

	if (!net->mib.mptcp_statistics) {
		for (i = 0; mptcp_snmp_list[i].name; i++)
			seq_puts(seq, " 0");

		return;
	}

	for (i = 0; mptcp_snmp_list[i].name; i++)
		seq_printf(seq, " %lu",
			   snmp_fold_field(net->mib.mptcp_statistics,
					   mptcp_snmp_list[i].entry));
	seq_putc(seq, '\n');
}
