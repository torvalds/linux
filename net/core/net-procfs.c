// SPDX-License-Identifier: GPL-2.0
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/wext.h>
#include <net/hotdata.h>

#include "dev.h"

static void *dev_seq_from_index(struct seq_file *seq, loff_t *pos)
{
	unsigned long ifindex = *pos;
	struct net_device *dev;

	for_each_netdev_dump(seq_file_net(seq), dev, ifindex) {
		*pos = dev->ifindex;
		return dev;
	}
	return NULL;
}

static void *dev_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	if (!*pos)
		return SEQ_START_TOKEN;

	return dev_seq_from_index(seq, pos);
}

static void *dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return dev_seq_from_index(seq, pos);
}

static void dev_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static void dev_seq_printf_stats(struct seq_file *seq, struct net_device *dev)
{
	struct rtnl_link_stats64 temp;
	const struct rtnl_link_stats64 *stats = dev_get_stats(dev, &temp);

	seq_printf(seq, "%6s: %7llu %7llu %4llu %4llu %4llu %5llu %10llu %9llu "
		   "%8llu %7llu %4llu %4llu %4llu %5llu %7llu %10llu\n",
		   dev->name, stats->rx_bytes, stats->rx_packets,
		   stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors +
		    stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->rx_compressed, stats->multicast,
		   stats->tx_bytes, stats->tx_packets,
		   stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors +
		    stats->tx_aborted_errors +
		    stats->tx_window_errors +
		    stats->tx_heartbeat_errors,
		   stats->tx_compressed);
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized
 *	/proc/net interface to create /proc/net/dev
 */
static int dev_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Inter-|   Receive                            "
			      "                    |  Transmit\n"
			      " face |bytes    packets errs drop fifo frame "
			      "compressed multicast|bytes    packets errs "
			      "drop fifo colls carrier compressed\n");
	else
		dev_seq_printf_stats(seq, v);
	return 0;
}

static u32 softnet_input_pkt_queue_len(struct softnet_data *sd)
{
	return skb_queue_len_lockless(&sd->input_pkt_queue);
}

static u32 softnet_process_queue_len(struct softnet_data *sd)
{
	return skb_queue_len_lockless(&sd->process_queue);
}

static struct softnet_data *softnet_get_online(loff_t *pos)
{
	struct softnet_data *sd = NULL;

	while (*pos < nr_cpu_ids)
		if (cpu_online(*pos)) {
			sd = &per_cpu(softnet_data, *pos);
			break;
		} else
			++*pos;
	return sd;
}

static void *softnet_seq_start(struct seq_file *seq, loff_t *pos)
{
	return softnet_get_online(pos);
}

static void *softnet_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return softnet_get_online(pos);
}

static void softnet_seq_stop(struct seq_file *seq, void *v)
{
}

static int softnet_seq_show(struct seq_file *seq, void *v)
{
	struct softnet_data *sd = v;
	u32 input_qlen = softnet_input_pkt_queue_len(sd);
	u32 process_qlen = softnet_process_queue_len(sd);
	unsigned int flow_limit_count = 0;

#ifdef CONFIG_NET_FLOW_LIMIT
	struct sd_flow_limit *fl;

	rcu_read_lock();
	fl = rcu_dereference(sd->flow_limit);
	if (fl)
		flow_limit_count = fl->count;
	rcu_read_unlock();
#endif

	/* the index is the CPU id owing this sd. Since offline CPUs are not
	 * displayed, it would be othrwise not trivial for the user-space
	 * mapping the data a specific CPU
	 */
	seq_printf(seq,
		   "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x "
		   "%08x %08x\n",
		   sd->processed, atomic_read(&sd->dropped),
		   sd->time_squeeze, 0,
		   0, 0, 0, 0, /* was fastroute */
		   0,	/* was cpu_collision */
		   sd->received_rps, flow_limit_count,
		   input_qlen + process_qlen, (int)seq->index,
		   input_qlen, process_qlen);
	return 0;
}

static const struct seq_operations dev_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = dev_seq_show,
};

static const struct seq_operations softnet_seq_ops = {
	.start = softnet_seq_start,
	.next  = softnet_seq_next,
	.stop  = softnet_seq_stop,
	.show  = softnet_seq_show,
};

static void *ptype_get_idx(struct seq_file *seq, loff_t pos)
{
	struct list_head *ptype_list = NULL;
	struct packet_type *pt = NULL;
	struct net_device *dev;
	loff_t i = 0;
	int t;

	for_each_netdev_rcu(seq_file_net(seq), dev) {
		ptype_list = &dev->ptype_all;
		list_for_each_entry_rcu(pt, ptype_list, list) {
			if (i == pos)
				return pt;
			++i;
		}
	}

	list_for_each_entry_rcu(pt, &net_hotdata.ptype_all, list) {
		if (i == pos)
			return pt;
		++i;
	}

	for (t = 0; t < PTYPE_HASH_SIZE; t++) {
		list_for_each_entry_rcu(pt, &ptype_base[t], list) {
			if (i == pos)
				return pt;
			++i;
		}
	}
	return NULL;
}

static void *ptype_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	return *pos ? ptype_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *ptype_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net_device *dev;
	struct packet_type *pt;
	struct list_head *nxt;
	int hash;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ptype_get_idx(seq, 0);

	pt = v;
	nxt = pt->list.next;
	if (pt->dev) {
		if (nxt != &pt->dev->ptype_all)
			goto found;

		dev = pt->dev;
		for_each_netdev_continue_rcu(seq_file_net(seq), dev) {
			if (!list_empty(&dev->ptype_all)) {
				nxt = dev->ptype_all.next;
				goto found;
			}
		}

		nxt = net_hotdata.ptype_all.next;
		goto ptype_all;
	}

	if (pt->type == htons(ETH_P_ALL)) {
ptype_all:
		if (nxt != &net_hotdata.ptype_all)
			goto found;
		hash = 0;
		nxt = ptype_base[0].next;
	} else
		hash = ntohs(pt->type) & PTYPE_HASH_MASK;

	while (nxt == &ptype_base[hash]) {
		if (++hash >= PTYPE_HASH_SIZE)
			return NULL;
		nxt = ptype_base[hash].next;
	}
found:
	return list_entry(nxt, struct packet_type, list);
}

static void ptype_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int ptype_seq_show(struct seq_file *seq, void *v)
{
	struct packet_type *pt = v;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Type Device      Function\n");
	else if ((!pt->af_packet_net || net_eq(pt->af_packet_net, seq_file_net(seq))) &&
		 (!pt->dev || net_eq(dev_net(pt->dev), seq_file_net(seq)))) {
		if (pt->type == htons(ETH_P_ALL))
			seq_puts(seq, "ALL ");
		else
			seq_printf(seq, "%04x", ntohs(pt->type));

		seq_printf(seq, " %-8s %ps\n",
			   pt->dev ? pt->dev->name : "", pt->func);
	}

	return 0;
}

static const struct seq_operations ptype_seq_ops = {
	.start = ptype_seq_start,
	.next  = ptype_seq_next,
	.stop  = ptype_seq_stop,
	.show  = ptype_seq_show,
};

static int __net_init dev_proc_net_init(struct net *net)
{
	int rc = -ENOMEM;

	if (!proc_create_net("dev", 0444, net->proc_net, &dev_seq_ops,
			sizeof(struct seq_net_private)))
		goto out;
	if (!proc_create_seq("softnet_stat", 0444, net->proc_net,
			 &softnet_seq_ops))
		goto out_dev;
	if (!proc_create_net("ptype", 0444, net->proc_net, &ptype_seq_ops,
			sizeof(struct seq_net_private)))
		goto out_softnet;

	if (wext_proc_init(net))
		goto out_ptype;
	rc = 0;
out:
	return rc;
out_ptype:
	remove_proc_entry("ptype", net->proc_net);
out_softnet:
	remove_proc_entry("softnet_stat", net->proc_net);
out_dev:
	remove_proc_entry("dev", net->proc_net);
	goto out;
}

static void __net_exit dev_proc_net_exit(struct net *net)
{
	wext_proc_exit(net);

	remove_proc_entry("ptype", net->proc_net);
	remove_proc_entry("softnet_stat", net->proc_net);
	remove_proc_entry("dev", net->proc_net);
}

static struct pernet_operations __net_initdata dev_proc_ops = {
	.init = dev_proc_net_init,
	.exit = dev_proc_net_exit,
};

static int dev_mc_seq_show(struct seq_file *seq, void *v)
{
	struct netdev_hw_addr *ha;
	struct net_device *dev = v;

	if (v == SEQ_START_TOKEN)
		return 0;

	netif_addr_lock_bh(dev);
	netdev_for_each_mc_addr(ha, dev) {
		seq_printf(seq, "%-4d %-15s %-5d %-5d %*phN\n",
			   dev->ifindex, dev->name,
			   ha->refcount, ha->global_use,
			   (int)dev->addr_len, ha->addr);
	}
	netif_addr_unlock_bh(dev);
	return 0;
}

static const struct seq_operations dev_mc_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = dev_mc_seq_show,
};

static int __net_init dev_mc_net_init(struct net *net)
{
	if (!proc_create_net("dev_mcast", 0, net->proc_net, &dev_mc_seq_ops,
			sizeof(struct seq_net_private)))
		return -ENOMEM;
	return 0;
}

static void __net_exit dev_mc_net_exit(struct net *net)
{
	remove_proc_entry("dev_mcast", net->proc_net);
}

static struct pernet_operations __net_initdata dev_mc_net_ops = {
	.init = dev_mc_net_init,
	.exit = dev_mc_net_exit,
};

int __init dev_proc_init(void)
{
	int ret = register_pernet_subsys(&dev_proc_ops);
	if (!ret)
		return register_pernet_subsys(&dev_mc_net_ops);
	return ret;
}
