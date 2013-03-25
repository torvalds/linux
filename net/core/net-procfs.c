#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/wext.h>

#define BUCKET_SPACE (32 - NETDEV_HASHBITS - 1)

#define get_bucket(x) ((x) >> BUCKET_SPACE)
#define get_offset(x) ((x) & ((1 << BUCKET_SPACE) - 1))
#define set_bucket_offset(b, o) ((b) << BUCKET_SPACE | (o))

extern struct list_head ptype_all __read_mostly;
extern struct list_head ptype_base[PTYPE_HASH_SIZE] __read_mostly;

static inline struct net_device *dev_from_same_bucket(struct seq_file *seq, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	struct net_device *dev;
	struct hlist_head *h;
	unsigned int count = 0, offset = get_offset(*pos);

	h = &net->dev_name_head[get_bucket(*pos)];
	hlist_for_each_entry_rcu(dev, h, name_hlist) {
		if (++count == offset)
			return dev;
	}

	return NULL;
}

static inline struct net_device *dev_from_bucket(struct seq_file *seq, loff_t *pos)
{
	struct net_device *dev;
	unsigned int bucket;

	do {
		dev = dev_from_same_bucket(seq, pos);
		if (dev)
			return dev;

		bucket = get_bucket(*pos) + 1;
		*pos = set_bucket_offset(bucket, 1);
	} while (bucket < NETDEV_HASHENTRIES);

	return NULL;
}

/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */
static void *dev_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	rcu_read_lock();
	if (!*pos)
		return SEQ_START_TOKEN;

	if (get_bucket(*pos) >= NETDEV_HASHENTRIES)
		return NULL;

	return dev_from_bucket(seq, pos);
}

static void *dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return dev_from_bucket(seq, pos);
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

	seq_printf(seq, "%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		   sd->processed, sd->dropped, sd->time_squeeze, 0,
		   0, 0, 0, 0, /* was fastroute */
		   sd->cpu_collision, sd->received_rps);
	return 0;
}

static const struct seq_operations dev_seq_ops = {
	.start = dev_seq_start,
	.next  = dev_seq_next,
	.stop  = dev_seq_stop,
	.show  = dev_seq_show,
};

static int dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &dev_seq_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations dev_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = dev_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};

static const struct seq_operations softnet_seq_ops = {
	.start = softnet_seq_start,
	.next  = softnet_seq_next,
	.stop  = softnet_seq_stop,
	.show  = softnet_seq_show,
};

static int softnet_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &softnet_seq_ops);
}

static const struct file_operations softnet_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = softnet_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

static void *ptype_get_idx(loff_t pos)
{
	struct packet_type *pt = NULL;
	loff_t i = 0;
	int t;

	list_for_each_entry_rcu(pt, &ptype_all, list) {
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
	return *pos ? ptype_get_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *ptype_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct packet_type *pt;
	struct list_head *nxt;
	int hash;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ptype_get_idx(0);

	pt = v;
	nxt = pt->list.next;
	if (pt->type == htons(ETH_P_ALL)) {
		if (nxt != &ptype_all)
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
	else if (pt->dev == NULL || dev_net(pt->dev) == seq_file_net(seq)) {
		if (pt->type == htons(ETH_P_ALL))
			seq_puts(seq, "ALL ");
		else
			seq_printf(seq, "%04x", ntohs(pt->type));

		seq_printf(seq, " %-8s %pf\n",
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

static int ptype_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &ptype_seq_ops,
			sizeof(struct seq_net_private));
}

static const struct file_operations ptype_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = ptype_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};


static int __net_init dev_proc_net_init(struct net *net)
{
	int rc = -ENOMEM;

	if (!proc_create("dev", S_IRUGO, net->proc_net, &dev_seq_fops))
		goto out;
	if (!proc_create("softnet_stat", S_IRUGO, net->proc_net,
			 &softnet_seq_fops))
		goto out_dev;
	if (!proc_create("ptype", S_IRUGO, net->proc_net, &ptype_seq_fops))
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
		int i;

		seq_printf(seq, "%-4d %-15s %-5d %-5d ", dev->ifindex,
			   dev->name, ha->refcount, ha->global_use);

		for (i = 0; i < dev->addr_len; i++)
			seq_printf(seq, "%02x", ha->addr[i]);

		seq_putc(seq, '\n');
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

static int dev_mc_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &dev_mc_seq_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations dev_mc_seq_fops = {
	.owner	 = THIS_MODULE,
	.open    = dev_mc_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};

static int __net_init dev_mc_net_init(struct net *net)
{
	if (!proc_create("dev_mcast", 0, net->proc_net, &dev_mc_seq_fops))
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
