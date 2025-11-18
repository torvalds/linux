// SPDX-License-Identifier: GPL-2.0-or-later
/* SCTP kernel implementation
 * Copyright (c) 2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Sridhar Samudrala <sri@us.ibm.com>
 */

#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/export.h>
#include <net/sctp/sctp.h>
#include <net/ip.h> /* for snmp_fold_field */

static const struct snmp_mib sctp_snmp_list[] = {
	SNMP_MIB_ITEM("SctpCurrEstab", SCTP_MIB_CURRESTAB),
	SNMP_MIB_ITEM("SctpActiveEstabs", SCTP_MIB_ACTIVEESTABS),
	SNMP_MIB_ITEM("SctpPassiveEstabs", SCTP_MIB_PASSIVEESTABS),
	SNMP_MIB_ITEM("SctpAborteds", SCTP_MIB_ABORTEDS),
	SNMP_MIB_ITEM("SctpShutdowns", SCTP_MIB_SHUTDOWNS),
	SNMP_MIB_ITEM("SctpOutOfBlues", SCTP_MIB_OUTOFBLUES),
	SNMP_MIB_ITEM("SctpChecksumErrors", SCTP_MIB_CHECKSUMERRORS),
	SNMP_MIB_ITEM("SctpOutCtrlChunks", SCTP_MIB_OUTCTRLCHUNKS),
	SNMP_MIB_ITEM("SctpOutOrderChunks", SCTP_MIB_OUTORDERCHUNKS),
	SNMP_MIB_ITEM("SctpOutUnorderChunks", SCTP_MIB_OUTUNORDERCHUNKS),
	SNMP_MIB_ITEM("SctpInCtrlChunks", SCTP_MIB_INCTRLCHUNKS),
	SNMP_MIB_ITEM("SctpInOrderChunks", SCTP_MIB_INORDERCHUNKS),
	SNMP_MIB_ITEM("SctpInUnorderChunks", SCTP_MIB_INUNORDERCHUNKS),
	SNMP_MIB_ITEM("SctpFragUsrMsgs", SCTP_MIB_FRAGUSRMSGS),
	SNMP_MIB_ITEM("SctpReasmUsrMsgs", SCTP_MIB_REASMUSRMSGS),
	SNMP_MIB_ITEM("SctpOutSCTPPacks", SCTP_MIB_OUTSCTPPACKS),
	SNMP_MIB_ITEM("SctpInSCTPPacks", SCTP_MIB_INSCTPPACKS),
	SNMP_MIB_ITEM("SctpT1InitExpireds", SCTP_MIB_T1_INIT_EXPIREDS),
	SNMP_MIB_ITEM("SctpT1CookieExpireds", SCTP_MIB_T1_COOKIE_EXPIREDS),
	SNMP_MIB_ITEM("SctpT2ShutdownExpireds", SCTP_MIB_T2_SHUTDOWN_EXPIREDS),
	SNMP_MIB_ITEM("SctpT3RtxExpireds", SCTP_MIB_T3_RTX_EXPIREDS),
	SNMP_MIB_ITEM("SctpT4RtoExpireds", SCTP_MIB_T4_RTO_EXPIREDS),
	SNMP_MIB_ITEM("SctpT5ShutdownGuardExpireds", SCTP_MIB_T5_SHUTDOWN_GUARD_EXPIREDS),
	SNMP_MIB_ITEM("SctpDelaySackExpireds", SCTP_MIB_DELAY_SACK_EXPIREDS),
	SNMP_MIB_ITEM("SctpAutocloseExpireds", SCTP_MIB_AUTOCLOSE_EXPIREDS),
	SNMP_MIB_ITEM("SctpT3Retransmits", SCTP_MIB_T3_RETRANSMITS),
	SNMP_MIB_ITEM("SctpPmtudRetransmits", SCTP_MIB_PMTUD_RETRANSMITS),
	SNMP_MIB_ITEM("SctpFastRetransmits", SCTP_MIB_FAST_RETRANSMITS),
	SNMP_MIB_ITEM("SctpInPktSoftirq", SCTP_MIB_IN_PKT_SOFTIRQ),
	SNMP_MIB_ITEM("SctpInPktBacklog", SCTP_MIB_IN_PKT_BACKLOG),
	SNMP_MIB_ITEM("SctpInPktDiscards", SCTP_MIB_IN_PKT_DISCARDS),
	SNMP_MIB_ITEM("SctpInDataChunkDiscards", SCTP_MIB_IN_DATA_CHUNK_DISCARDS),
};

/* Display sctp snmp mib statistics(/proc/net/sctp/snmp). */
static int sctp_snmp_seq_show(struct seq_file *seq, void *v)
{
	unsigned long buff[ARRAY_SIZE(sctp_snmp_list)];
	const int cnt = ARRAY_SIZE(sctp_snmp_list);
	struct net *net = seq->private;
	int i;

	memset(buff, 0, sizeof(buff));

	snmp_get_cpu_field_batch_cnt(buff, sctp_snmp_list, cnt,
				     net->sctp.sctp_statistics);
	for (i = 0; i < cnt; i++)
		seq_printf(seq, "%-32s\t%ld\n", sctp_snmp_list[i].name,
						buff[i]);

	return 0;
}

/* Dump local addresses of an association/endpoint. */
static void sctp_seq_dump_local_addrs(struct seq_file *seq, struct sctp_ep_common *epb)
{
	struct sctp_association *asoc;
	struct sctp_sockaddr_entry *laddr;
	struct sctp_transport *peer;
	union sctp_addr *addr, *primary = NULL;
	struct sctp_af *af;

	if (epb->type == SCTP_EP_TYPE_ASSOCIATION) {
		asoc = sctp_assoc(epb);

		peer = asoc->peer.primary_path;
		if (unlikely(peer == NULL)) {
			WARN(1, "Association %p with NULL primary path!\n", asoc);
			return;
		}

		primary = &peer->saddr;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(laddr, &epb->bind_addr.address_list, list) {
		if (!laddr->valid)
			continue;

		addr = &laddr->a;
		af = sctp_get_af_specific(addr->sa.sa_family);
		if (primary && af->cmp_addr(addr, primary)) {
			seq_printf(seq, "*");
		}
		af->seq_dump_addr(seq, addr);
	}
	rcu_read_unlock();
}

/* Dump remote addresses of an association. */
static void sctp_seq_dump_remote_addrs(struct seq_file *seq, struct sctp_association *assoc)
{
	struct sctp_transport *transport;
	union sctp_addr *addr, *primary;
	struct sctp_af *af;

	primary = &assoc->peer.primary_addr;
	list_for_each_entry_rcu(transport, &assoc->peer.transport_addr_list,
			transports) {
		addr = &transport->ipaddr;

		af = sctp_get_af_specific(addr->sa.sa_family);
		if (af->cmp_addr(addr, primary)) {
			seq_printf(seq, "*");
		}
		af->seq_dump_addr(seq, addr);
	}
}

static void *sctp_eps_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= sctp_ep_hashsize)
		return NULL;

	if (*pos < 0)
		*pos = 0;

	if (*pos == 0)
		seq_printf(seq, " ENDPT     SOCK   STY SST HBKT LPORT   UID INODE LADDRS\n");

	return (void *)pos;
}

static void sctp_eps_seq_stop(struct seq_file *seq, void *v)
{
}


static void *sctp_eps_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	if (++*pos >= sctp_ep_hashsize)
		return NULL;

	return pos;
}


/* Display sctp endpoints (/proc/net/sctp/eps). */
static int sctp_eps_seq_show(struct seq_file *seq, void *v)
{
	struct sctp_hashbucket *head;
	struct sctp_endpoint *ep;
	struct sock *sk;
	int    hash = *(loff_t *)v;

	if (hash >= sctp_ep_hashsize)
		return -ENOMEM;

	head = &sctp_ep_hashtable[hash];
	read_lock_bh(&head->lock);
	sctp_for_each_hentry(ep, &head->chain) {
		sk = ep->base.sk;
		if (!net_eq(sock_net(sk), seq_file_net(seq)))
			continue;
		seq_printf(seq, "%8pK %8pK %-3d %-3d %-4d %-5d %5u %5lu ", ep, sk,
			   sctp_sk(sk)->type, sk->sk_state, hash,
			   ep->base.bind_addr.port,
			   from_kuid_munged(seq_user_ns(seq), sk_uid(sk)),
			   sock_i_ino(sk));

		sctp_seq_dump_local_addrs(seq, &ep->base);
		seq_printf(seq, "\n");
	}
	read_unlock_bh(&head->lock);

	return 0;
}

static const struct seq_operations sctp_eps_ops = {
	.start = sctp_eps_seq_start,
	.next  = sctp_eps_seq_next,
	.stop  = sctp_eps_seq_stop,
	.show  = sctp_eps_seq_show,
};

struct sctp_ht_iter {
	struct seq_net_private p;
	struct rhashtable_iter hti;
};

static void *sctp_transport_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct sctp_ht_iter *iter = seq->private;

	sctp_transport_walk_start(&iter->hti);

	return sctp_transport_get_idx(seq_file_net(seq), &iter->hti, *pos);
}

static void sctp_transport_seq_stop(struct seq_file *seq, void *v)
{
	struct sctp_ht_iter *iter = seq->private;

	if (v && v != SEQ_START_TOKEN) {
		struct sctp_transport *transport = v;

		sctp_transport_put(transport);
	}

	sctp_transport_walk_stop(&iter->hti);
}

static void *sctp_transport_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sctp_ht_iter *iter = seq->private;

	if (v && v != SEQ_START_TOKEN) {
		struct sctp_transport *transport = v;

		sctp_transport_put(transport);
	}

	++*pos;

	return sctp_transport_get_next(seq_file_net(seq), &iter->hti);
}

/* Display sctp associations (/proc/net/sctp/assocs). */
static int sctp_assocs_seq_show(struct seq_file *seq, void *v)
{
	struct sctp_transport *transport;
	struct sctp_association *assoc;
	struct sctp_ep_common *epb;
	struct sock *sk;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, " ASSOC     SOCK   STY SST ST HBKT "
				"ASSOC-ID TX_QUEUE RX_QUEUE UID INODE LPORT "
				"RPORT LADDRS <-> RADDRS "
				"HBINT INS OUTS MAXRT T1X T2X RTXC "
				"wmema wmemq sndbuf rcvbuf\n");
		return 0;
	}

	transport = (struct sctp_transport *)v;
	assoc = transport->asoc;
	epb = &assoc->base;
	sk = epb->sk;

	seq_printf(seq,
		   "%8pK %8pK %-3d %-3d %-2d %-4d "
		   "%4d %8d %8d %7u %5lu %-5d %5d ",
		   assoc, sk, sctp_sk(sk)->type, sk->sk_state,
		   assoc->state, 0,
		   assoc->assoc_id,
		   assoc->sndbuf_used,
		   atomic_read(&assoc->rmem_alloc),
		   from_kuid_munged(seq_user_ns(seq), sk_uid(sk)),
		   sock_i_ino(sk),
		   epb->bind_addr.port,
		   assoc->peer.port);
	seq_printf(seq, " ");
	sctp_seq_dump_local_addrs(seq, epb);
	seq_printf(seq, "<-> ");
	sctp_seq_dump_remote_addrs(seq, assoc);
	seq_printf(seq, "\t%8lu %5d %5d %4d %4d %4d %8d "
		   "%8d %8d %8d %8d",
		assoc->hbinterval, assoc->stream.incnt,
		assoc->stream.outcnt, assoc->max_retrans,
		assoc->init_retries, assoc->shutdown_retries,
		assoc->rtx_data_chunks,
		refcount_read(&sk->sk_wmem_alloc),
		READ_ONCE(sk->sk_wmem_queued),
		sk->sk_sndbuf,
		sk->sk_rcvbuf);
	seq_printf(seq, "\n");

	return 0;
}

static const struct seq_operations sctp_assoc_ops = {
	.start = sctp_transport_seq_start,
	.next  = sctp_transport_seq_next,
	.stop  = sctp_transport_seq_stop,
	.show  = sctp_assocs_seq_show,
};

static int sctp_remaddr_seq_show(struct seq_file *seq, void *v)
{
	struct sctp_association *assoc;
	struct sctp_transport *transport, *tsp;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "ADDR ASSOC_ID HB_ACT RTO MAX_PATH_RTX "
				"REM_ADDR_RTX START STATE\n");
		return 0;
	}

	transport = (struct sctp_transport *)v;
	assoc = transport->asoc;

	list_for_each_entry_rcu(tsp, &assoc->peer.transport_addr_list,
				transports) {
		/*
		 * The remote address (ADDR)
		 */
		tsp->af_specific->seq_dump_addr(seq, &tsp->ipaddr);
		seq_printf(seq, " ");
		/*
		 * The association ID (ASSOC_ID)
		 */
		seq_printf(seq, "%d ", tsp->asoc->assoc_id);

		/*
		 * If the Heartbeat is active (HB_ACT)
		 * Note: 1 = Active, 0 = Inactive
		 */
		seq_printf(seq, "%d ", timer_pending(&tsp->hb_timer));

		/*
		 * Retransmit time out (RTO)
		 */
		seq_printf(seq, "%lu ", tsp->rto);

		/*
		 * Maximum path retransmit count (PATH_MAX_RTX)
		 */
		seq_printf(seq, "%d ", tsp->pathmaxrxt);

		/*
		 * remote address retransmit count (REM_ADDR_RTX)
		 * Note: We don't have a way to tally this at the moment
		 * so lets just leave it as zero for the moment
		 */
		seq_puts(seq, "0 ");

		/*
		 * remote address start time (START).  This is also not
		 * currently implemented, but we can record it with a
		 * jiffies marker in a subsequent patch
		 */
		seq_puts(seq, "0 ");

		/*
		 * The current state of this destination. I.e.
		 * SCTP_ACTIVE, SCTP_INACTIVE, ...
		 */
		seq_printf(seq, "%d", tsp->state);

		seq_printf(seq, "\n");
	}

	return 0;
}

static const struct seq_operations sctp_remaddr_ops = {
	.start = sctp_transport_seq_start,
	.next  = sctp_transport_seq_next,
	.stop  = sctp_transport_seq_stop,
	.show  = sctp_remaddr_seq_show,
};

/* Set up the proc fs entry for the SCTP protocol. */
int __net_init sctp_proc_init(struct net *net)
{
	net->sctp.proc_net_sctp = proc_net_mkdir(net, "sctp", net->proc_net);
	if (!net->sctp.proc_net_sctp)
		return -ENOMEM;
	if (!proc_create_net_single("snmp", 0444, net->sctp.proc_net_sctp,
			 sctp_snmp_seq_show, NULL))
		goto cleanup;
	if (!proc_create_net("eps", 0444, net->sctp.proc_net_sctp,
			&sctp_eps_ops, sizeof(struct seq_net_private)))
		goto cleanup;
	if (!proc_create_net("assocs", 0444, net->sctp.proc_net_sctp,
			&sctp_assoc_ops, sizeof(struct sctp_ht_iter)))
		goto cleanup;
	if (!proc_create_net("remaddr", 0444, net->sctp.proc_net_sctp,
			&sctp_remaddr_ops, sizeof(struct sctp_ht_iter)))
		goto cleanup;
	return 0;

cleanup:
	remove_proc_subtree("sctp", net->proc_net);
	net->sctp.proc_net_sctp = NULL;
	return -ENOMEM;
}
