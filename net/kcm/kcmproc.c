#include <linux/in.h>
#include <linux/inet.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/proc_fs.h>
#include <linux/rculist.h>
#include <linux/seq_file.h>
#include <linux/socket.h>
#include <net/inet_sock.h>
#include <net/kcm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/tcp.h>

#ifdef CONFIG_PROC_FS
struct kcm_seq_muxinfo {
	char				*name;
	const struct file_operations	*seq_fops;
	const struct seq_operations	seq_ops;
};

static struct kcm_mux *kcm_get_first(struct seq_file *seq)
{
	struct net *net = seq_file_net(seq);
	struct kcm_net *knet = net_generic(net, kcm_net_id);

	return list_first_or_null_rcu(&knet->mux_list,
				      struct kcm_mux, kcm_mux_list);
}

static struct kcm_mux *kcm_get_next(struct kcm_mux *mux)
{
	struct kcm_net *knet = mux->knet;

	return list_next_or_null_rcu(&knet->mux_list, &mux->kcm_mux_list,
				     struct kcm_mux, kcm_mux_list);
}

static struct kcm_mux *kcm_get_idx(struct seq_file *seq, loff_t pos)
{
	struct net *net = seq_file_net(seq);
	struct kcm_net *knet = net_generic(net, kcm_net_id);
	struct kcm_mux *m;

	list_for_each_entry_rcu(m, &knet->mux_list, kcm_mux_list) {
		if (!pos)
			return m;
		--pos;
	}
	return NULL;
}

static void *kcm_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	void *p;

	if (v == SEQ_START_TOKEN)
		p = kcm_get_first(seq);
	else
		p = kcm_get_next(v);
	++*pos;
	return p;
}

static void *kcm_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(rcu)
{
	rcu_read_lock();

	if (!*pos)
		return SEQ_START_TOKEN;
	else
		return kcm_get_idx(seq, *pos - 1);
}

static void kcm_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	rcu_read_unlock();
}

struct kcm_proc_mux_state {
	struct seq_net_private p;
	int idx;
};

static int kcm_seq_open(struct inode *inode, struct file *file)
{
	struct kcm_seq_muxinfo *muxinfo = PDE_DATA(inode);

	return seq_open_net(inode, file, &muxinfo->seq_ops,
			   sizeof(struct kcm_proc_mux_state));
}

static void kcm_format_mux_header(struct seq_file *seq)
{
	struct net *net = seq_file_net(seq);
	struct kcm_net *knet = net_generic(net, kcm_net_id);

	seq_printf(seq,
		   "*** KCM statistics (%d MUX) ****\n",
		   knet->count);

	seq_printf(seq,
		   "%-14s %-10s %-16s %-10s %-16s %-8s %-8s %-8s %-8s %s",
		   "Object",
		   "RX-Msgs",
		   "RX-Bytes",
		   "TX-Msgs",
		   "TX-Bytes",
		   "Recv-Q",
		   "Rmem",
		   "Send-Q",
		   "Smem",
		   "Status");

	/* XXX: pdsts header stuff here */
	seq_puts(seq, "\n");
}

static void kcm_format_sock(struct kcm_sock *kcm, struct seq_file *seq,
			    int i, int *len)
{
	seq_printf(seq,
		   "   kcm-%-7u %-10llu %-16llu %-10llu %-16llu %-8d %-8d %-8d %-8s ",
		   kcm->index,
		   kcm->stats.rx_msgs,
		   kcm->stats.rx_bytes,
		   kcm->stats.tx_msgs,
		   kcm->stats.tx_bytes,
		   kcm->sk.sk_receive_queue.qlen,
		   sk_rmem_alloc_get(&kcm->sk),
		   kcm->sk.sk_write_queue.qlen,
		   "-");

	if (kcm->tx_psock)
		seq_printf(seq, "Psck-%u ", kcm->tx_psock->index);

	if (kcm->tx_wait)
		seq_puts(seq, "TxWait ");

	if (kcm->tx_wait_more)
		seq_puts(seq, "WMore ");

	if (kcm->rx_wait)
		seq_puts(seq, "RxWait ");

	seq_puts(seq, "\n");
}

static void kcm_format_psock(struct kcm_psock *psock, struct seq_file *seq,
			     int i, int *len)
{
	seq_printf(seq,
		   "   psock-%-5u %-10llu %-16llu %-10llu %-16llu %-8d %-8d %-8d %-8d ",
		   psock->index,
		   psock->strp.stats.rx_msgs,
		   psock->strp.stats.rx_bytes,
		   psock->stats.tx_msgs,
		   psock->stats.tx_bytes,
		   psock->sk->sk_receive_queue.qlen,
		   atomic_read(&psock->sk->sk_rmem_alloc),
		   psock->sk->sk_write_queue.qlen,
		   atomic_read(&psock->sk->sk_wmem_alloc));

	if (psock->done)
		seq_puts(seq, "Done ");

	if (psock->tx_stopped)
		seq_puts(seq, "TxStop ");

	if (psock->strp.rx_stopped)
		seq_puts(seq, "RxStop ");

	if (psock->tx_kcm)
		seq_printf(seq, "Rsvd-%d ", psock->tx_kcm->index);

	if (!psock->strp.rx_paused && !psock->ready_rx_msg) {
		if (psock->sk->sk_receive_queue.qlen) {
			if (psock->strp.rx_need_bytes)
				seq_printf(seq, "RxWait=%u ",
					   psock->strp.rx_need_bytes);
			else
				seq_printf(seq, "RxWait ");
		}
	} else  {
		if (psock->strp.rx_paused)
			seq_puts(seq, "RxPause ");

		if (psock->ready_rx_msg)
			seq_puts(seq, "RdyRx ");
	}

	seq_puts(seq, "\n");
}

static void
kcm_format_mux(struct kcm_mux *mux, loff_t idx, struct seq_file *seq)
{
	int i, len;
	struct kcm_sock *kcm;
	struct kcm_psock *psock;

	/* mux information */
	seq_printf(seq,
		   "%-6s%-8s %-10llu %-16llu %-10llu %-16llu %-8s %-8s %-8s %-8s ",
		   "mux", "",
		   mux->stats.rx_msgs,
		   mux->stats.rx_bytes,
		   mux->stats.tx_msgs,
		   mux->stats.tx_bytes,
		   "-", "-", "-", "-");

	seq_printf(seq, "KCMs: %d, Psocks %d\n",
		   mux->kcm_socks_cnt, mux->psocks_cnt);

	/* kcm sock information */
	i = 0;
	spin_lock_bh(&mux->lock);
	list_for_each_entry(kcm, &mux->kcm_socks, kcm_sock_list) {
		kcm_format_sock(kcm, seq, i, &len);
		i++;
	}
	i = 0;
	list_for_each_entry(psock, &mux->psocks, psock_list) {
		kcm_format_psock(psock, seq, i, &len);
		i++;
	}
	spin_unlock_bh(&mux->lock);
}

static int kcm_seq_show(struct seq_file *seq, void *v)
{
	struct kcm_proc_mux_state *mux_state;

	mux_state = seq->private;
	if (v == SEQ_START_TOKEN) {
		mux_state->idx = 0;
		kcm_format_mux_header(seq);
	} else {
		kcm_format_mux(v, mux_state->idx, seq);
		mux_state->idx++;
	}
	return 0;
}

static const struct file_operations kcm_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= kcm_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
};

static struct kcm_seq_muxinfo kcm_seq_muxinfo = {
	.name		= "kcm",
	.seq_fops	= &kcm_seq_fops,
	.seq_ops	= {
		.show	= kcm_seq_show,
		.start	= kcm_seq_start,
		.next	= kcm_seq_next,
		.stop	= kcm_seq_stop,
	}
};

static int kcm_proc_register(struct net *net, struct kcm_seq_muxinfo *muxinfo)
{
	struct proc_dir_entry *p;
	int rc = 0;

	p = proc_create_data(muxinfo->name, S_IRUGO, net->proc_net,
			     muxinfo->seq_fops, muxinfo);
	if (!p)
		rc = -ENOMEM;
	return rc;
}
EXPORT_SYMBOL(kcm_proc_register);

static void kcm_proc_unregister(struct net *net,
				struct kcm_seq_muxinfo *muxinfo)
{
	remove_proc_entry(muxinfo->name, net->proc_net);
}
EXPORT_SYMBOL(kcm_proc_unregister);

static int kcm_stats_seq_show(struct seq_file *seq, void *v)
{
	struct kcm_psock_stats psock_stats;
	struct kcm_mux_stats mux_stats;
	struct strp_aggr_stats strp_stats;
	struct kcm_mux *mux;
	struct kcm_psock *psock;
	struct net *net = seq->private;
	struct kcm_net *knet = net_generic(net, kcm_net_id);

	memset(&mux_stats, 0, sizeof(mux_stats));
	memset(&psock_stats, 0, sizeof(psock_stats));
	memset(&strp_stats, 0, sizeof(strp_stats));

	mutex_lock(&knet->mutex);

	aggregate_mux_stats(&knet->aggregate_mux_stats, &mux_stats);
	aggregate_psock_stats(&knet->aggregate_psock_stats,
			      &psock_stats);
	aggregate_strp_stats(&knet->aggregate_strp_stats,
			     &strp_stats);

	list_for_each_entry_rcu(mux, &knet->mux_list, kcm_mux_list) {
		spin_lock_bh(&mux->lock);
		aggregate_mux_stats(&mux->stats, &mux_stats);
		aggregate_psock_stats(&mux->aggregate_psock_stats,
				      &psock_stats);
		aggregate_strp_stats(&mux->aggregate_strp_stats,
				     &strp_stats);
		list_for_each_entry(psock, &mux->psocks, psock_list) {
			aggregate_psock_stats(&psock->stats, &psock_stats);
			save_strp_stats(&psock->strp, &strp_stats);
		}

		spin_unlock_bh(&mux->lock);
	}

	mutex_unlock(&knet->mutex);

	seq_printf(seq,
		   "%-8s %-10s %-16s %-10s %-16s %-10s %-10s %-10s %-10s %-10s\n",
		   "MUX",
		   "RX-Msgs",
		   "RX-Bytes",
		   "TX-Msgs",
		   "TX-Bytes",
		   "TX-Retries",
		   "Attach",
		   "Unattach",
		   "UnattchRsvd",
		   "RX-RdyDrops");

	seq_printf(seq,
		   "%-8s %-10llu %-16llu %-10llu %-16llu %-10u %-10u %-10u %-10u %-10u\n",
		   "",
		   mux_stats.rx_msgs,
		   mux_stats.rx_bytes,
		   mux_stats.tx_msgs,
		   mux_stats.tx_bytes,
		   mux_stats.tx_retries,
		   mux_stats.psock_attach,
		   mux_stats.psock_unattach_rsvd,
		   mux_stats.psock_unattach,
		   mux_stats.rx_ready_drops);

	seq_printf(seq,
		   "%-8s %-10s %-16s %-10s %-16s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s %-10s\n",
		   "Psock",
		   "RX-Msgs",
		   "RX-Bytes",
		   "TX-Msgs",
		   "TX-Bytes",
		   "Reserved",
		   "Unreserved",
		   "RX-Aborts",
		   "RX-Intr",
		   "RX-Unrecov",
		   "RX-MemFail",
		   "RX-NeedMor",
		   "RX-BadLen",
		   "RX-TooBig",
		   "RX-Timeout",
		   "TX-Aborts");

	seq_printf(seq,
		   "%-8s %-10llu %-16llu %-10llu %-16llu %-10llu %-10llu %-10u %-10u %-10u %-10u %-10u %-10u %-10u %-10u %-10u\n",
		   "",
		   strp_stats.rx_msgs,
		   strp_stats.rx_bytes,
		   psock_stats.tx_msgs,
		   psock_stats.tx_bytes,
		   psock_stats.reserved,
		   psock_stats.unreserved,
		   strp_stats.rx_aborts,
		   strp_stats.rx_interrupted,
		   strp_stats.rx_unrecov_intr,
		   strp_stats.rx_mem_fail,
		   strp_stats.rx_need_more_hdr,
		   strp_stats.rx_bad_hdr_len,
		   strp_stats.rx_msg_too_big,
		   strp_stats.rx_msg_timeouts,
		   psock_stats.tx_aborts);

	return 0;
}

static int kcm_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, kcm_stats_seq_show);
}

static const struct file_operations kcm_stats_seq_fops = {
	.owner   = THIS_MODULE,
	.open    = kcm_stats_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release_net,
};

static int kcm_proc_init_net(struct net *net)
{
	int err;

	if (!proc_create("kcm_stats", S_IRUGO, net->proc_net,
			 &kcm_stats_seq_fops)) {
		err = -ENOMEM;
		goto out_kcm_stats;
	}

	err = kcm_proc_register(net, &kcm_seq_muxinfo);
	if (err)
		goto out_kcm;

	return 0;

out_kcm:
	remove_proc_entry("kcm_stats", net->proc_net);
out_kcm_stats:
	return err;
}

static void kcm_proc_exit_net(struct net *net)
{
	kcm_proc_unregister(net, &kcm_seq_muxinfo);
	remove_proc_entry("kcm_stats", net->proc_net);
}

static struct pernet_operations kcm_net_ops = {
	.init = kcm_proc_init_net,
	.exit = kcm_proc_exit_net,
};

int __init kcm_proc_init(void)
{
	return register_pernet_subsys(&kcm_net_ops);
}

void __exit kcm_proc_exit(void)
{
	unregister_pernet_subsys(&kcm_net_ops);
}

#endif /* CONFIG_PROC_FS */
