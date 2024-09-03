// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/seq_file.h>
#include <net/ip.h>
#include <net/mptcp.h>
#include <net/snmp.h>
#include <net/net_namespace.h>

#include "mib.h"

static const struct snmp_mib mptcp_snmp_list[] = {
	SNMP_MIB_ITEM("MPCapableSYNRX", MPTCP_MIB_MPCAPABLEPASSIVE),
	SNMP_MIB_ITEM("MPCapableSYNTX", MPTCP_MIB_MPCAPABLEACTIVE),
	SNMP_MIB_ITEM("MPCapableSYNACKRX", MPTCP_MIB_MPCAPABLEACTIVEACK),
	SNMP_MIB_ITEM("MPCapableACKRX", MPTCP_MIB_MPCAPABLEPASSIVEACK),
	SNMP_MIB_ITEM("MPCapableFallbackACK", MPTCP_MIB_MPCAPABLEPASSIVEFALLBACK),
	SNMP_MIB_ITEM("MPCapableFallbackSYNACK", MPTCP_MIB_MPCAPABLEACTIVEFALLBACK),
	SNMP_MIB_ITEM("MPFallbackTokenInit", MPTCP_MIB_TOKENFALLBACKINIT),
	SNMP_MIB_ITEM("MPTCPRetrans", MPTCP_MIB_RETRANSSEGS),
	SNMP_MIB_ITEM("MPJoinNoTokenFound", MPTCP_MIB_JOINNOTOKEN),
	SNMP_MIB_ITEM("MPJoinSynRx", MPTCP_MIB_JOINSYNRX),
	SNMP_MIB_ITEM("MPJoinSynBackupRx", MPTCP_MIB_JOINSYNBACKUPRX),
	SNMP_MIB_ITEM("MPJoinSynAckRx", MPTCP_MIB_JOINSYNACKRX),
	SNMP_MIB_ITEM("MPJoinSynAckBackupRx", MPTCP_MIB_JOINSYNACKBACKUPRX),
	SNMP_MIB_ITEM("MPJoinSynAckHMacFailure", MPTCP_MIB_JOINSYNACKMAC),
	SNMP_MIB_ITEM("MPJoinAckRx", MPTCP_MIB_JOINACKRX),
	SNMP_MIB_ITEM("MPJoinAckHMacFailure", MPTCP_MIB_JOINACKMAC),
	SNMP_MIB_ITEM("MPJoinSynTx", MPTCP_MIB_JOINSYNTX),
	SNMP_MIB_ITEM("MPJoinSynTxCreatSkErr", MPTCP_MIB_JOINSYNTXCREATSKERR),
	SNMP_MIB_ITEM("MPJoinSynTxBindErr", MPTCP_MIB_JOINSYNTXBINDERR),
	SNMP_MIB_ITEM("MPJoinSynTxConnectErr", MPTCP_MIB_JOINSYNTXCONNECTERR),
	SNMP_MIB_ITEM("DSSNotMatching", MPTCP_MIB_DSSNOMATCH),
	SNMP_MIB_ITEM("InfiniteMapTx", MPTCP_MIB_INFINITEMAPTX),
	SNMP_MIB_ITEM("InfiniteMapRx", MPTCP_MIB_INFINITEMAPRX),
	SNMP_MIB_ITEM("DSSNoMatchTCP", MPTCP_MIB_DSSTCPMISMATCH),
	SNMP_MIB_ITEM("DataCsumErr", MPTCP_MIB_DATACSUMERR),
	SNMP_MIB_ITEM("OFOQueueTail", MPTCP_MIB_OFOQUEUETAIL),
	SNMP_MIB_ITEM("OFOQueue", MPTCP_MIB_OFOQUEUE),
	SNMP_MIB_ITEM("OFOMerge", MPTCP_MIB_OFOMERGE),
	SNMP_MIB_ITEM("NoDSSInWindow", MPTCP_MIB_NODSSWINDOW),
	SNMP_MIB_ITEM("DuplicateData", MPTCP_MIB_DUPDATA),
	SNMP_MIB_ITEM("AddAddr", MPTCP_MIB_ADDADDR),
	SNMP_MIB_ITEM("AddAddrTx", MPTCP_MIB_ADDADDRTX),
	SNMP_MIB_ITEM("AddAddrTxDrop", MPTCP_MIB_ADDADDRTXDROP),
	SNMP_MIB_ITEM("EchoAdd", MPTCP_MIB_ECHOADD),
	SNMP_MIB_ITEM("EchoAddTx", MPTCP_MIB_ECHOADDTX),
	SNMP_MIB_ITEM("EchoAddTxDrop", MPTCP_MIB_ECHOADDTXDROP),
	SNMP_MIB_ITEM("PortAdd", MPTCP_MIB_PORTADD),
	SNMP_MIB_ITEM("AddAddrDrop", MPTCP_MIB_ADDADDRDROP),
	SNMP_MIB_ITEM("MPJoinPortSynRx", MPTCP_MIB_JOINPORTSYNRX),
	SNMP_MIB_ITEM("MPJoinPortSynAckRx", MPTCP_MIB_JOINPORTSYNACKRX),
	SNMP_MIB_ITEM("MPJoinPortAckRx", MPTCP_MIB_JOINPORTACKRX),
	SNMP_MIB_ITEM("MismatchPortSynRx", MPTCP_MIB_MISMATCHPORTSYNRX),
	SNMP_MIB_ITEM("MismatchPortAckRx", MPTCP_MIB_MISMATCHPORTACKRX),
	SNMP_MIB_ITEM("RmAddr", MPTCP_MIB_RMADDR),
	SNMP_MIB_ITEM("RmAddrDrop", MPTCP_MIB_RMADDRDROP),
	SNMP_MIB_ITEM("RmAddrTx", MPTCP_MIB_RMADDRTX),
	SNMP_MIB_ITEM("RmAddrTxDrop", MPTCP_MIB_RMADDRTXDROP),
	SNMP_MIB_ITEM("RmSubflow", MPTCP_MIB_RMSUBFLOW),
	SNMP_MIB_ITEM("MPPrioTx", MPTCP_MIB_MPPRIOTX),
	SNMP_MIB_ITEM("MPPrioRx", MPTCP_MIB_MPPRIORX),
	SNMP_MIB_ITEM("MPFailTx", MPTCP_MIB_MPFAILTX),
	SNMP_MIB_ITEM("MPFailRx", MPTCP_MIB_MPFAILRX),
	SNMP_MIB_ITEM("MPFastcloseTx", MPTCP_MIB_MPFASTCLOSETX),
	SNMP_MIB_ITEM("MPFastcloseRx", MPTCP_MIB_MPFASTCLOSERX),
	SNMP_MIB_ITEM("MPRstTx", MPTCP_MIB_MPRSTTX),
	SNMP_MIB_ITEM("MPRstRx", MPTCP_MIB_MPRSTRX),
	SNMP_MIB_ITEM("RcvPruned", MPTCP_MIB_RCVPRUNED),
	SNMP_MIB_ITEM("SubflowStale", MPTCP_MIB_SUBFLOWSTALE),
	SNMP_MIB_ITEM("SubflowRecover", MPTCP_MIB_SUBFLOWRECOVER),
	SNMP_MIB_ITEM("SndWndShared", MPTCP_MIB_SNDWNDSHARED),
	SNMP_MIB_ITEM("RcvWndShared", MPTCP_MIB_RCVWNDSHARED),
	SNMP_MIB_ITEM("RcvWndConflictUpdate", MPTCP_MIB_RCVWNDCONFLICTUPDATE),
	SNMP_MIB_ITEM("RcvWndConflict", MPTCP_MIB_RCVWNDCONFLICT),
	SNMP_MIB_ITEM("MPCurrEstab", MPTCP_MIB_CURRESTAB),
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
	unsigned long sum[ARRAY_SIZE(mptcp_snmp_list) - 1];
	struct net *net = seq->private;
	int i;

	seq_puts(seq, "MPTcpExt:");
	for (i = 0; mptcp_snmp_list[i].name; i++)
		seq_printf(seq, " %s", mptcp_snmp_list[i].name);

	seq_puts(seq, "\nMPTcpExt:");

	memset(sum, 0, sizeof(sum));
	if (net->mib.mptcp_statistics)
		snmp_get_cpu_field_batch(sum, mptcp_snmp_list,
					 net->mib.mptcp_statistics);

	for (i = 0; mptcp_snmp_list[i].name; i++)
		seq_printf(seq, " %lu", sum[i]);

	seq_putc(seq, '\n');
}
