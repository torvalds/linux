/* SCTP kernel implementation
 * Copyright (c) 2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    Sridhar Samudrala <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
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
	SNMP_MIB_SENTINEL
};

/* Display sctp snmp mib statistics(/proc/net/sctp/snmp). */
static int sctp_snmp_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = seq->private;
	int i;

	for (i = 0; sctp_snmp_list[i].name != NULL; i++)
		seq_printf(seq, "%-32s\t%ld\n", sctp_snmp_list[i].name,
			   snmp_fold_field((void __percpu **)net->sctp.sctp_statistics,
				      sctp_snmp_list[i].entry));

	return 0;
}

/* Initialize the seq file operations for 'snmp' object. */
static int sctp_snmp_seq_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, sctp_snmp_seq_show);
}

static const struct file_operations sctp_snmp_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = sctp_snmp_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release_net,
};

/* Set up the proc fs entry for 'snmp' object. */
int __net_init sctp_snmp_proc_init(struct net *net)
{
	struct proc_dir_entry *p;

	p = proc_create("snmp", S_IRUGO, net->sctp.proc_net_sctp,
			&sctp_snmp_seq_fops);
	if (!p)
		return -ENOMEM;

	return 0;
}

/* Cleanup the proc fs entry for 'snmp' object. */
void sctp_snmp_proc_exit(struct net *net)
{
	remove_proc_entry("snmp", net->sctp.proc_net_sctp);
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
	rcu_read_lock();
	list_for_each_entry_rcu(transport, &assoc->peer.transport_addr_list,
			transports) {
		addr = &transport->ipaddr;
		if (transport->dead)
			continue;

		af = sctp_get_af_specific(addr->sa.sa_family);
		if (af->cmp_addr(addr, primary)) {
			seq_printf(seq, "*");
		}
		af->seq_dump_addr(seq, addr);
	}
	rcu_read_unlock();
}

static void * sctp_eps_seq_start(struct seq_file *seq, loff_t *pos)
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


static void * sctp_eps_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	if (++*pos >= sctp_ep_hashsize)
		return NULL;

	return pos;
}


/* Display sctp endpoints (/proc/net/sctp/eps). */
static int sctp_eps_seq_show(struct seq_file *seq, void *v)
{
	struct sctp_hashbucket *head;
	struct sctp_ep_common *epb;
	struct sctp_endpoint *ep;
	struct sock *sk;
	int    hash = *(loff_t *)v;

	if (hash >= sctp_ep_hashsize)
		return -ENOMEM;

	head = &sctp_ep_hashtable[hash];
	sctp_local_bh_disable();
	read_lock(&head->lock);
	sctp_for_each_hentry(epb, &head->chain) {
		ep = sctp_ep(epb);
		sk = epb->sk;
		if (!net_eq(sock_net(sk), seq_file_net(seq)))
			continue;
		seq_printf(seq, "%8pK %8pK %-3d %-3d %-4d %-5d %5d %5lu ", ep, sk,
			   sctp_sk(sk)->type, sk->sk_state, hash,
			   epb->bind_addr.port,
			   from_kuid_munged(seq_user_ns(seq), sock_i_uid(sk)),
			   sock_i_ino(sk));

		sctp_seq_dump_local_addrs(seq, epb);
		seq_printf(seq, "\n");
	}
	read_unlock(&head->lock);
	sctp_local_bh_enable();

	return 0;
}

static const struct seq_operations sctp_eps_ops = {
	.start = sctp_eps_seq_start,
	.next  = sctp_eps_seq_next,
	.stop  = sctp_eps_seq_stop,
	.show  = sctp_eps_seq_show,
};


/* Initialize the seq file operations for 'eps' object. */
static int sctp_eps_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &sctp_eps_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations sctp_eps_seq_fops = {
	.open	 = sctp_eps_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_net,
};

/* Set up the proc fs entry for 'eps' object. */
int __net_init sctp_eps_proc_init(struct net *net)
{
	struct proc_dir_entry *p;

	p = proc_create("eps", S_IRUGO, net->sctp.proc_net_sctp,
			&sctp_eps_seq_fops);
	if (!p)
		return -ENOMEM;

	return 0;
}

/* Cleanup the proc fs entry for 'eps' object. */
void sctp_eps_proc_exit(struct net *net)
{
	remove_proc_entry("eps", net->sctp.proc_net_sctp);
}


static void * sctp_assocs_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= sctp_assoc_hashsize)
		return NULL;

	if (*pos < 0)
		*pos = 0;

	if (*pos == 0)
		seq_printf(seq, " ASSOC     SOCK   STY SST ST HBKT "
				"ASSOC-ID TX_QUEUE RX_QUEUE UID INODE LPORT "
				"RPORT LADDRS <-> RADDRS "
				"HBINT INS OUTS MAXRT T1X T2X RTXC "
				"wmema wmemq sndbuf rcvbuf\n");

	return (void *)pos;
}

static void sctp_assocs_seq_stop(struct seq_file *seq, void *v)
{
}


static void * sctp_assocs_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	if (++*pos >= sctp_assoc_hashsize)
		return NULL;

	return pos;
}

/* Display sctp associations (/proc/net/sctp/assocs). */
static int sctp_assocs_seq_show(struct seq_file *seq, void *v)
{
	struct sctp_hashbucket *head;
	struct sctp_ep_common *epb;
	struct sctp_association *assoc;
	struct sock *sk;
	int    hash = *(loff_t *)v;

	if (hash >= sctp_assoc_hashsize)
		return -ENOMEM;

	head = &sctp_assoc_hashtable[hash];
	sctp_local_bh_disable();
	read_lock(&head->lock);
	sctp_for_each_hentry(epb, &head->chain) {
		assoc = sctp_assoc(epb);
		sk = epb->sk;
		if (!net_eq(sock_net(sk), seq_file_net(seq)))
			continue;
		seq_printf(seq,
			   "%8pK %8pK %-3d %-3d %-2d %-4d "
			   "%4d %8d %8d %7d %5lu %-5d %5d ",
			   assoc, sk, sctp_sk(sk)->type, sk->sk_state,
			   assoc->state, hash,
			   assoc->assoc_id,
			   assoc->sndbuf_used,
			   atomic_read(&assoc->rmem_alloc),
			   from_kuid_munged(seq_user_ns(seq), sock_i_uid(sk)),
			   sock_i_ino(sk),
			   epb->bind_addr.port,
			   assoc->peer.port);
		seq_printf(seq, " ");
		sctp_seq_dump_local_addrs(seq, epb);
		seq_printf(seq, "<-> ");
		sctp_seq_dump_remote_addrs(seq, assoc);
		seq_printf(seq, "\t%8lu %5d %5d %4d %4d %4d %8d "
			   "%8d %8d %8d %8d",
			assoc->hbinterval, assoc->c.sinit_max_instreams,
			assoc->c.sinit_num_ostreams, assoc->max_retrans,
			assoc->init_retries, assoc->shutdown_retries,
			assoc->rtx_data_chunks,
			atomic_read(&sk->sk_wmem_alloc),
			sk->sk_wmem_queued,
			sk->sk_sndbuf,
			sk->sk_rcvbuf);
		seq_printf(seq, "\n");
	}
	read_unlock(&head->lock);
	sctp_local_bh_enable();

	return 0;
}

static const struct seq_operations sctp_assoc_ops = {
	.start = sctp_assocs_seq_start,
	.next  = sctp_assocs_seq_next,
	.stop  = sctp_assocs_seq_stop,
	.show  = sctp_assocs_seq_show,
};

/* Initialize the seq file operations for 'assocs' object. */
static int sctp_assocs_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &sctp_assoc_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations sctp_assocs_seq_fops = {
	.open	 = sctp_assocs_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release_net,
};

/* Set up the proc fs entry for 'assocs' object. */
int __net_init sctp_assocs_proc_init(struct net *net)
{
	struct proc_dir_entry *p;

	p = proc_create("assocs", S_IRUGO, net->sctp.proc_net_sctp,
			&sctp_assocs_seq_fops);
	if (!p)
		return -ENOMEM;

	return 0;
}

/* Cleanup the proc fs entry for 'assocs' object. */
void sctp_assocs_proc_exit(struct net *net)
{
	remove_proc_entry("assocs", net->sctp.proc_net_sctp);
}

static void *sctp_remaddr_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= sctp_assoc_hashsize)
		return NULL;

	if (*pos < 0)
		*pos = 0;

	if (*pos == 0)
		seq_printf(seq, "ADDR ASSOC_ID HB_ACT RTO MAX_PATH_RTX "
				"REM_ADDR_RTX  START\n");

	return (void *)pos;
}

static void *sctp_remaddr_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	if (++*pos >= sctp_assoc_hashsize)
		return NULL;

	return pos;
}

static void sctp_remaddr_seq_stop(struct seq_file *seq, void *v)
{
}

static int sctp_remaddr_seq_show(struct seq_file *seq, void *v)
{
	struct sctp_hashbucket *head;
	struct sctp_ep_common *epb;
	struct sctp_association *assoc;
	struct sctp_transport *tsp;
	int    hash = *(loff_t *)v;

	if (hash >= sctp_assoc_hashsize)
		return -ENOMEM;

	head = &sctp_assoc_hashtable[hash];
	sctp_local_bh_disable();
	read_lock(&head->lock);
	rcu_read_lock();
	sctp_for_each_hentry(epb, &head->chain) {
		if (!net_eq(sock_net(epb->sk), seq_file_net(seq)))
			continue;
		assoc = sctp_assoc(epb);
		list_for_each_entry_rcu(tsp, &assoc->peer.transport_addr_list,
					transports) {
			if (tsp->dead)
				continue;

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
			seq_printf(seq, "0 ");

			/*
			 * remote address start time (START).  This is also not
			 * currently implemented, but we can record it with a
			 * jiffies marker in a subsequent patch
			 */
			seq_printf(seq, "0");

			seq_printf(seq, "\n");
		}
	}

	rcu_read_unlock();
	read_unlock(&head->lock);
	sctp_local_bh_enable();

	return 0;

}

static const struct seq_operations sctp_remaddr_ops = {
	.start = sctp_remaddr_seq_start,
	.next  = sctp_remaddr_seq_next,
	.stop  = sctp_remaddr_seq_stop,
	.show  = sctp_remaddr_seq_show,
};

/* Cleanup the proc fs entry for 'remaddr' object. */
void sctp_remaddr_proc_exit(struct net *net)
{
	remove_proc_entry("remaddr", net->sctp.proc_net_sctp);
}

static int sctp_remaddr_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &sctp_remaddr_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations sctp_remaddr_seq_fops = {
	.open = sctp_remaddr_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release_net,
};

int __net_init sctp_remaddr_proc_init(struct net *net)
{
	struct proc_dir_entry *p;

	p = proc_create("remaddr", S_IRUGO, net->sctp.proc_net_sctp,
			&sctp_remaddr_seq_fops);
	if (!p)
		return -ENOMEM;
	return 0;
}
