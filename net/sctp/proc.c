/* SCTP kernel reference Implementation
 * Copyright (c) 2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
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
#include <net/sctp/sctp.h>

static struct snmp_mib sctp_snmp_list[] = {
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

/* Return the current value of a particular entry in the mib by adding its
 * per cpu counters.
 */
static unsigned long
fold_field(void *mib[], int nr)
{
	unsigned long res = 0;
	int i;

	for_each_possible_cpu(i) {
		res +=
		    *((unsigned long *) (((void *) per_cpu_ptr(mib[0], i)) +
					 sizeof (unsigned long) * nr));
		res +=
		    *((unsigned long *) (((void *) per_cpu_ptr(mib[1], i)) +
					 sizeof (unsigned long) * nr));
	}
	return res;
}

/* Display sctp snmp mib statistics(/proc/net/sctp/snmp). */
static int sctp_snmp_seq_show(struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; sctp_snmp_list[i].name != NULL; i++)
		seq_printf(seq, "%-32s\t%ld\n", sctp_snmp_list[i].name,
			   fold_field((void **)sctp_statistics,
				      sctp_snmp_list[i].entry));

	return 0;
}

/* Initialize the seq file operations for 'snmp' object. */
static int sctp_snmp_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sctp_snmp_seq_show, NULL);
}

static const struct file_operations sctp_snmp_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = sctp_snmp_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

/* Set up the proc fs entry for 'snmp' object. */
int __init sctp_snmp_proc_init(void)
{
	struct proc_dir_entry *p;

	p = create_proc_entry("snmp", S_IRUGO, proc_net_sctp);
	if (!p)
		return -ENOMEM;

	p->proc_fops = &sctp_snmp_seq_fops;

	return 0;
}

/* Cleanup the proc fs entry for 'snmp' object. */
void sctp_snmp_proc_exit(void)
{
	remove_proc_entry("snmp", proc_net_sctp);
}

/* Dump local addresses of an association/endpoint. */
static void sctp_seq_dump_local_addrs(struct seq_file *seq, struct sctp_ep_common *epb)
{
	struct list_head *pos;
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

	list_for_each(pos, &epb->bind_addr.address_list) {
		laddr = list_entry(pos, struct sctp_sockaddr_entry, list);
		addr = &laddr->a;
		af = sctp_get_af_specific(addr->sa.sa_family);
		if (primary && af->cmp_addr(addr, primary)) {
			seq_printf(seq, "*");
		}
		af->seq_dump_addr(seq, addr);
	}
}

/* Dump remote addresses of an association. */
static void sctp_seq_dump_remote_addrs(struct seq_file *seq, struct sctp_association *assoc)
{
	struct list_head *pos;
	struct sctp_transport *transport;
	union sctp_addr *addr, *primary;
	struct sctp_af *af;

	primary = &assoc->peer.primary_addr;
	list_for_each(pos, &assoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);
		addr = &transport->ipaddr;
		af = sctp_get_af_specific(addr->sa.sa_family);
		if (af->cmp_addr(addr, primary)) {
			seq_printf(seq, "*");
		}
		af->seq_dump_addr(seq, addr);
	}
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
	return;
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
	struct hlist_node *node;
	int    hash = *(loff_t *)v;

	if (hash >= sctp_ep_hashsize)
		return -ENOMEM;

	head = &sctp_ep_hashtable[hash];
	sctp_local_bh_disable();
	read_lock(&head->lock);
	sctp_for_each_hentry(epb, node, &head->chain) {
		ep = sctp_ep(epb);
		sk = epb->sk;
		seq_printf(seq, "%8p %8p %-3d %-3d %-4d %-5d %5d %5lu ", ep, sk,
			   sctp_sk(sk)->type, sk->sk_state, hash,
			   epb->bind_addr.port,
			   sock_i_uid(sk), sock_i_ino(sk));

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
	return seq_open(file, &sctp_eps_ops);
}

static const struct file_operations sctp_eps_seq_fops = {
	.open	 = sctp_eps_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/* Set up the proc fs entry for 'eps' object. */
int __init sctp_eps_proc_init(void)
{
	struct proc_dir_entry *p;

	p = create_proc_entry("eps", S_IRUGO, proc_net_sctp);
	if (!p)
		return -ENOMEM;

	p->proc_fops = &sctp_eps_seq_fops;

	return 0;
}

/* Cleanup the proc fs entry for 'eps' object. */
void sctp_eps_proc_exit(void)
{
	remove_proc_entry("eps", proc_net_sctp);
}


static void * sctp_assocs_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos >= sctp_assoc_hashsize)
		return NULL;

	if (*pos < 0)
		*pos = 0;

	if (*pos == 0)
		seq_printf(seq, " ASSOC     SOCK   STY SST ST HBKT ASSOC-ID TX_QUEUE RX_QUEUE UID INODE LPORT "
				"RPORT LADDRS <-> RADDRS\n");

	return (void *)pos;
}

static void sctp_assocs_seq_stop(struct seq_file *seq, void *v)
{
	return;
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
	struct hlist_node *node;
	int    hash = *(loff_t *)v;

	if (hash >= sctp_assoc_hashsize)
		return -ENOMEM;

	head = &sctp_assoc_hashtable[hash];
	sctp_local_bh_disable();
	read_lock(&head->lock);
	sctp_for_each_hentry(epb, node, &head->chain) {
		assoc = sctp_assoc(epb);
		sk = epb->sk;
		seq_printf(seq,
			   "%8p %8p %-3d %-3d %-2d %-4d %4d %8d %8d %7d %5lu %-5d %5d ",
			   assoc, sk, sctp_sk(sk)->type, sk->sk_state,
			   assoc->state, hash, assoc->assoc_id,
			   assoc->sndbuf_used,
			   atomic_read(&assoc->rmem_alloc),
			   sock_i_uid(sk), sock_i_ino(sk),
			   epb->bind_addr.port,
			   assoc->peer.port);

		seq_printf(seq, " ");
		sctp_seq_dump_local_addrs(seq, epb);
		seq_printf(seq, "<-> ");
		sctp_seq_dump_remote_addrs(seq, assoc);
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
	return seq_open(file, &sctp_assoc_ops);
}

static const struct file_operations sctp_assocs_seq_fops = {
	.open	 = sctp_assocs_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/* Set up the proc fs entry for 'assocs' object. */
int __init sctp_assocs_proc_init(void)
{
	struct proc_dir_entry *p;

	p = create_proc_entry("assocs", S_IRUGO, proc_net_sctp);
	if (!p)
		return -ENOMEM;

	p->proc_fops = &sctp_assocs_seq_fops;

	return 0;
}

/* Cleanup the proc fs entry for 'assocs' object. */
void sctp_assocs_proc_exit(void)
{
	remove_proc_entry("assocs", proc_net_sctp);
}
