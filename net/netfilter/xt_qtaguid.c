/*
 * Kernel iptables module to track stats for packets based on user tags.
 *
 * (C) 2011 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* TODO: support ipv6 for iface_stat */

#include <linux/file.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_qtaguid.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <linux/netfilter/xt_socket.h>
/* We only use the xt_socket funcs within a similar context to avoid unexpected
 * return values. */
#define XT_SOCKET_SUPPORTED_HOOKS \
	((1 << NF_INET_PRE_ROUTING) | (1 << NF_INET_LOCAL_IN))


/*---------------------------------------------------------------------------*/
/*
 * Tags:
 *
 * They represent what the data usage counters will be tracked against.
 * By default a tag is just based on the UID.
 * The UID is used as the base for policying, and can not be ignored.
 * So a tag will always at least represent a UID (uid_tag).
 *
 * A tag can be augmented with an "accounting tag" which is associated
 * with a UID.
 * User space can set the acct_tag portion of the tag which is then used
 * with sockets: all data belong to that socket will be counted against the
 * tag. The policing is then based on the tag's uid_tag portion,
 * and stats are collected for the acct_tag portion seperately.
 *
 * There could be
 * a:  {acct_tag=1, uid_tag=10003}
 * b:  {acct_tag=2, uid_tag=10003}
 * c:  {acct_tag=3, uid_tag=10003}
 * d:  {acct_tag=0, uid_tag=10003}
 * (a, b, and c represent tags associated with specific sockets.
 * d is for the totals for that uid, including all untagged traffic.
 * Typically d is used with policing/quota rules.
 *
 * We want tag_t big enough to distinguish uid_t and acct_tag.
 * It might become a struct if needed.
 * Nothing should be using it as an int.
 */
typedef uint64_t tag_t;  /* Only used via accessors */

static const char *iface_stat_procdirname = "iface_stat";
static struct proc_dir_entry *iface_stat_procdir;

enum ifs_tx_rx {
	IFS_TX,
	IFS_RX,
	IFS_MAX_DIRECTIONS
};

/* For now, TCP, UDP, the rest */
enum ifs_proto {
	IFS_TCP,
	IFS_UDP,
	IFS_PROTO_OTHER,
	IFS_MAX_PROTOS
};

struct byte_packet_counters {
	uint64_t bytes;
	uint64_t packets;
};

struct data_counters {
	struct byte_packet_counters bpc[IFS_MAX_DIRECTIONS][IFS_MAX_PROTOS];
};

struct tag_stat {
	struct rb_node node;
	tag_t tag;

	struct data_counters counters;
	/* If this tag is acct_tag based, we need to count against the
	 * matching parent uid_tag. */
	struct data_counters *parent_counters;
	struct proc_dir_entry *proc_ptr;
};

static LIST_HEAD(iface_stat_list);
static DEFINE_SPINLOCK(iface_stat_list_lock);

struct iface_stat {
	struct list_head list;
	char *ifname;
	uint64_t rx_bytes;
	uint64_t rx_packets;
	uint64_t tx_bytes;
	uint64_t tx_packets;
	bool active;
	struct proc_dir_entry *proc_ptr;

	struct rb_root tag_stat_tree;
	spinlock_t tag_stat_list_lock;
};


static struct rb_root sock_tag_tree = RB_ROOT;
static DEFINE_SPINLOCK(sock_tag_list_lock);

/*
 * Track tag that this socket is transferring data for, and not necesseraly
 * the uid that owns the socket.
 * This is the tag against which tag_stat.counters will be billed.
 */
struct sock_tag {
	struct rb_node node;
	struct sock *sk;
	tag_t tag;
};

static bool qtaguid_mt(const struct sk_buff *skb, struct xt_action_param *par);

/*----------------------------------------------*/
static inline int tag_compare(tag_t t1, tag_t t2)
{
	return t1 < t2 ? -1 : t1 == t2 ? 0 : 1;
}


static inline tag_t combine_atag_with_uid(tag_t acct_tag, uid_t uid)
{
	return acct_tag | uid;
}
static inline tag_t make_tag_from_uid(uid_t uid)
{
	return uid;
}
static inline uid_t get_uid_from_tag(tag_t tag)
{
	return tag & 0xFFFFFFFFULL;
}
static inline tag_t get_utag_from_tag(tag_t tag)
{
	return tag & 0xFFFFFFFFULL;
}
static inline tag_t get_atag_from_tag(tag_t tag)
{
	return tag & ~0xFFFFFFFFULL;
}

static inline bool valid_atag(tag_t tag)
{
	return !(tag & 0xFFFFFFFFULL);
}

static inline void dc_add_byte_packets(struct data_counters *counters,
				  enum ifs_tx_rx direction,
				  enum ifs_proto ifs_proto,
				  int bytes,
				  int packets)
{
	counters->bpc[direction][ifs_proto].bytes += bytes;
	counters->bpc[direction][ifs_proto].packets += packets;
}

static inline uint64_t dc_sum_bytes(struct data_counters *counters,
				    enum ifs_tx_rx direction)
{
	return counters->bpc[direction][IFS_TCP].bytes
		+ counters->bpc[direction][IFS_UDP].bytes
		+ counters->bpc[direction][IFS_PROTO_OTHER].bytes;
}

static struct tag_stat *tag_stat_tree_search(struct rb_root *root, tag_t tag)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct tag_stat *data = rb_entry(node, struct tag_stat, node);
		int result = tag_compare(tag, data->tag);
		pr_debug("qtaguid: tag_stat_tree_search(): tag=0x%llx"
			 " (uid=%d)\n",
			 data->tag,
			 get_uid_from_tag(data->tag));

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static void tag_stat_tree_insert(struct tag_stat *data, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct tag_stat *this = rb_entry(*new, struct tag_stat,
						 node);
		int result = tag_compare(data->tag, this->tag);
		pr_debug("qtaguid: tag_stat_tree_insert(): tag=0x%llx"
			 " (uid=%d)\n",
			 this->tag,
			 get_uid_from_tag(this->tag));
		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static struct sock_tag *sock_tag_tree_search(struct rb_root *root,
					     const struct sock *sk)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct sock_tag *data = rb_entry(node, struct sock_tag, node);
		ptrdiff_t result = sk - data->sk;
		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return data;
	}
	return NULL;
}

static void sock_tag_tree_insert(struct sock_tag *data, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		struct sock_tag *this = rb_entry(*new, struct sock_tag, node);
		ptrdiff_t result = data->sk - this->sk;
		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			BUG();
	}

	/* Add new node and rebalance tree. */
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, root);
}

static int read_proc_u64(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	uint64_t value;
	char *p = page;
	uint64_t *iface_entry = data;
	if (!data)
		return 0;

	value = *iface_entry;
	p += sprintf(p, "%llu\n", value);
	len = (p - page) - off;
	*eof = (len <= count) ? 1 : 0;
	*start = page + off;
	return len;
}

static int read_proc_bool(char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len;
	bool value;
	char *p = page;
	bool *bool_entry = data;
	if (!data)
		return 0;

	value = *bool_entry;
	p += sprintf(p, "%u\n", value);
	len = (p - page) - off;
	*eof = (len <= count) ? 1 : 0;
	*start = page + off;
	return len;
}

/* Find the entry for tracking the specified interface. */
static struct iface_stat *get_iface_stat(const char *ifname)
{
	unsigned long flags;
	struct iface_stat *iface_entry;
	if (!ifname)
		return NULL;

	spin_lock_irqsave(&iface_stat_list_lock, flags);
	list_for_each_entry(iface_entry, &iface_stat_list, list) {
		if (!strcmp(iface_entry->ifname, ifname))
			goto done;
	}
	iface_entry = NULL;
done:
	spin_unlock_irqrestore(&iface_stat_list_lock, flags);
	return iface_entry;
}

/*
 * Create a new entry for tracking the specified interface.
 * Do nothing if the entry already exists.
 * Called when an interface is configured with a valid IP address.
 */
void iface_stat_create(const struct net_device *net_dev)
{
	struct in_device *in_dev;
	unsigned long flags;
	struct iface_stat *new_iface;
	struct proc_dir_entry *proc_entry;
	const char *ifname;
	struct iface_stat *entry;
	__be32 ipaddr = 0;
	struct in_ifaddr *ifa = NULL;

	ASSERT_RTNL(); /* No need for separate locking */

	pr_debug("iface_stat: create(): netdev=%p->name=%s\n",
		 net_dev, net_dev ? net_dev->name : "");
	if (!net_dev) {
		pr_err("iface_stat: create(): no net dev!\n");
		return;
	}

	in_dev = __in_dev_get_rtnl(net_dev);
	if (!in_dev) {
		pr_err("iface_stat: create(): no inet dev!\n");
		return;
	}

	pr_debug("iface_stat: create(): in_dev=%p\n", in_dev);
	ifname = net_dev->name;
	pr_debug("iface_stat: create(): ifname=%p\n", ifname);
	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
		pr_debug("iface_stat: create(): for(): ifa=%p ifname=%p\n",
			 ifa, ifname);
		pr_debug("iface_stat: create(): ifname=%s ifa_label=%s\n",
			 ifname, ifa->ifa_label ? ifa->ifa_label : "(null)");
		if (ifa->ifa_label && !strcmp(ifname, ifa->ifa_label))
			break;
	}

	if (ifa) {
		ipaddr = ifa->ifa_local;
	} else {
		pr_err("iface_stat: create(): dev %s has no matching IP\n",
		       ifname);
		return;
	}

	entry = get_iface_stat(net_dev->name);
	if (entry != NULL) {
		pr_debug("iface_stat: create(): dev %s entry=%p\n", ifname,
			 entry);
		if (ipv4_is_loopback(ipaddr)) {
			entry->active = false;
			pr_debug("iface_stat: create(): disable tracking of "
				 "loopback dev %s\n", ifname);
		} else {
			entry->active = true;
			pr_debug("iface_stat: create(): enable tracking of "
				 "dev %s with ip=%pI4\n",
				 ifname, &ipaddr);
		}
		return;
	} else if (ipv4_is_loopback(ipaddr)) {
		pr_debug("iface_stat: create(): ignore loopback dev %s"
			 " ip=%pI4\n", ifname, &ipaddr);
		return;
	}

	new_iface = kmalloc(sizeof(*new_iface), GFP_KERNEL);
	if (new_iface == NULL) {
		pr_err("iface_stat: create(): failed to alloc iface_stat\n");
		return;
	}
	memset(new_iface, 0, sizeof(*new_iface));
	new_iface->ifname = kstrdup(ifname, GFP_KERNEL);
	if (new_iface->ifname == NULL) {
		pr_err("iface_stat: create(): failed to alloc ifname\n");
		kfree(new_iface);
		return;
	}
	spin_lock_init(&new_iface->tag_stat_list_lock);

	new_iface->active = true;

	new_iface->tag_stat_tree = RB_ROOT;
	spin_lock_irqsave(&iface_stat_list_lock, flags);
	list_add(&new_iface->list, &iface_stat_list);
	spin_unlock_irqrestore(&iface_stat_list_lock, flags);

	proc_entry = proc_mkdir(ifname, iface_stat_procdir);
	new_iface->proc_ptr = proc_entry;

	/* TODO: make root access only */
	create_proc_read_entry("tx_bytes", S_IRUGO, proc_entry,
			read_proc_u64, &new_iface->tx_bytes);
	create_proc_read_entry("rx_bytes", S_IRUGO, proc_entry,
			read_proc_u64, &new_iface->rx_bytes);
	create_proc_read_entry("tx_packets", S_IRUGO, proc_entry,
			read_proc_u64, &new_iface->tx_packets);
	create_proc_read_entry("rx_packets", S_IRUGO, proc_entry,
			read_proc_u64, &new_iface->rx_packets);
	create_proc_read_entry("active", S_IRUGO, proc_entry,
			read_proc_bool, &new_iface->active);

	pr_debug("iface_stat: create(): done entry=%p dev=%s ip=%pI4\n",
		 new_iface, ifname, &ipaddr);
}

static struct sock_tag *get_sock_stat_nl(const struct sock *sk)
{
	pr_debug("xt_qtaguid: get_sock_stat_nl(sk=%p)\n", sk);
	return sock_tag_tree_search(&sock_tag_tree, sk);
}

static struct sock_tag *get_sock_stat(const struct sock *sk)
{
	unsigned long flags;
	struct sock_tag *sock_tag_entry;
	pr_debug("xt_qtaguid: get_sock_stat(sk=%p)\n", sk);
	if (!sk)
		return NULL;
	spin_lock_irqsave(&sock_tag_list_lock, flags);
	sock_tag_entry = get_sock_stat_nl(sk);
	spin_unlock_irqrestore(&sock_tag_list_lock, flags);
	return sock_tag_entry;
}

static void
data_counters_update(struct data_counters *dc,  enum ifs_tx_rx direction,
		int proto, int bytes)
{
	switch (proto) {
	case IPPROTO_TCP:
		dc_add_byte_packets(dc, direction, IFS_TCP, bytes, 1);
		break;
	case IPPROTO_UDP:
		dc_add_byte_packets(dc, direction, IFS_UDP, bytes, 1);
		break;
	case IPPROTO_IP:
	default:
		dc_add_byte_packets(dc, direction, IFS_PROTO_OTHER, bytes, 1);
		break;
	}
}


/*
 * Update stats for the specified interface. Do nothing if the entry
 * does not exist (when a device was never configured with an IP address).
 * Called when an device is being unregistered.
 */
void iface_stat_update(struct net_device *dev)
{
	struct rtnl_link_stats64 dev_stats, *stats;
	struct iface_stat *entry;
	stats = dev_get_stats(dev, &dev_stats);
	ASSERT_RTNL();

	entry = get_iface_stat(dev->name);
	if (entry == NULL) {
		pr_debug("iface_stat: dev %s monitor not found\n", dev->name);
		return;
	}
	if (entry->active) {
		entry->tx_bytes += stats->tx_bytes;
		entry->tx_packets += stats->tx_packets;
		entry->rx_bytes += stats->rx_bytes;
		entry->rx_packets += stats->rx_packets;
		entry->active = false;
		pr_debug("iface_stat: Updating stats for "
			"dev %s which went down\n", dev->name);
	} else {
		pr_debug("iface_stat: Did not update stats for "
			"dev %s which went down\n", dev->name);
	}
}


static void tag_stat_update(struct tag_stat *tag_entry,
			enum ifs_tx_rx direction, int proto, int bytes)
{
	pr_debug("xt_qtaguid: tag_stat_update(tag=0x%llx (uid=%d) dir=%d "
		"proto=%d bytes=%d)\n",
		tag_entry->tag, get_uid_from_tag(tag_entry->tag), direction,
		proto, bytes);
	data_counters_update(&tag_entry->counters, direction, proto, bytes);
	if (tag_entry->parent_counters)
		data_counters_update(tag_entry->parent_counters, direction,
				proto, bytes);
}


/* Create a new entry for tracking the specified {acct_tag,uid_tag} within
 * the interface.
 * iface_entry->tag_stat_list_lock should be held. */
static struct tag_stat *create_if_tag_stat(struct iface_stat *iface_entry,
					   tag_t tag)
{
	struct tag_stat *new_tag_stat_entry = NULL;
	pr_debug("iface_stat: create_if_tag_stat(): ife=%p tag=0x%llx"
		 " (uid=%d)\n",
		 iface_entry, tag, get_uid_from_tag(tag));
	new_tag_stat_entry = kmalloc(sizeof(*new_tag_stat_entry), GFP_ATOMIC);
	if (!new_tag_stat_entry) {
		pr_err("iface_stat: failed to alloc new tag entry\n");
		goto done;
	}
	memset(new_tag_stat_entry, 0, sizeof(*new_tag_stat_entry));
	new_tag_stat_entry->tag = tag;
	tag_stat_tree_insert(new_tag_stat_entry, &iface_entry->tag_stat_tree);
done:
	return new_tag_stat_entry;
}

static struct iface_stat *get_iface_entry(const char *ifname)
{
	struct iface_stat *iface_entry;
	unsigned long flags;

	/* Find the entry for tracking the specified tag within the interface */
	if (ifname == NULL) {
		pr_info("iface_stat: NULL device name\n");
		return NULL;
	}


	/* Iterate over interfaces */
	spin_lock_irqsave(&iface_stat_list_lock, flags);
	list_for_each_entry(iface_entry, &iface_stat_list, list) {
		if (!strcmp(ifname, iface_entry->ifname))
			goto done;
	}
	iface_entry = NULL;
done:
	spin_unlock_irqrestore(&iface_stat_list_lock, flags);
	return iface_entry;
}

static void if_tag_stat_update(const char *ifname, uid_t uid,
			       const struct sock *sk, enum ifs_tx_rx direction,
			       int proto, int bytes)
{
	struct tag_stat *tag_stat_entry;
	tag_t tag, acct_tag;
	tag_t uid_tag;
	struct data_counters *uid_tag_counters;
	struct sock_tag *sock_tag_entry;
	struct iface_stat *iface_entry;
	unsigned long flags;
	struct tag_stat *new_tag_stat;
	pr_debug("xt_qtaguid: if_tag_stat_update(ifname=%s "
		"uid=%d sk=%p dir=%d proto=%d bytes=%d)\n",
		 ifname, uid, sk, direction, proto, bytes);


	iface_entry = get_iface_entry(ifname);
	if (!iface_entry) {
		pr_err("iface_stat: interface %s not found\n", ifname);
		return;
	}
	/* else { If the iface_entry becomes inactive, it is still ok
	 * to process the data. } */

	pr_debug("iface_stat: stat_update() got entry=%p\n", iface_entry);

	/* Look for a tagged sock.
	 * It will have an acct_uid. */
	sock_tag_entry = get_sock_stat(sk);
	if (sock_tag_entry) {
		tag = sock_tag_entry->tag;
		acct_tag = get_atag_from_tag(tag);
		uid_tag = get_utag_from_tag(tag);
	} else {
		uid_tag = make_tag_from_uid(uid);
		acct_tag = 0;
		tag = combine_atag_with_uid(acct_tag, uid);
	}
	pr_debug("iface_stat: stat_update(): looking for tag=0x%llx (uid=%d)"
		 " in ife=%p\n",
		 tag, get_uid_from_tag(tag), iface_entry);
	/* Loop over tag list under this interface for {acct_tag,uid_tag} */
	spin_lock_irqsave(&iface_entry->tag_stat_list_lock, flags);

	tag_stat_entry = tag_stat_tree_search(&iface_entry->tag_stat_tree,
					      tag);
	if (tag_stat_entry) {
		/* Updating the {acct_tag, uid_tag} entry handles both stats:
		 * {0, uid_tag} will also get updated. */
		tag_stat_update(tag_stat_entry, direction, proto, bytes);
		spin_unlock_irqrestore(&iface_entry->tag_stat_list_lock, flags);
		return;
	}

	/* Loop over tag list under this interface for {0,uid_tag} */
	tag_stat_entry = tag_stat_tree_search(&iface_entry->tag_stat_tree,
					      uid_tag);
	if (!tag_stat_entry) {
		/* Here: the base uid_tag did not exist */
		/*
		 * No parent counters. So
		 *  - No {0, uid_tag} stats and no {acc_tag, uid_tag} stats.
		 */
		new_tag_stat = create_if_tag_stat(iface_entry, uid_tag);
		uid_tag_counters = &new_tag_stat->counters;
	} else {
		uid_tag_counters = &tag_stat_entry->counters;
	}

	if (acct_tag) {
		new_tag_stat = create_if_tag_stat(iface_entry, tag);
		new_tag_stat->parent_counters = uid_tag_counters;
	}
	spin_unlock_irqrestore(&iface_entry->tag_stat_list_lock, flags);
	tag_stat_update(new_tag_stat, direction, proto, bytes);
}

static int iface_netdev_event_handler(struct notifier_block *nb,
				      unsigned long event, void *ptr) {
	struct net_device *dev = ptr;

	pr_debug("iface_stat: netdev_event(): ev=0x%lx netdev=%p->name=%s\n",
		 event, dev, dev ? dev->name : "");

	switch (event) {
	case NETDEV_UP:
	case NETDEV_REBOOT:
	case NETDEV_CHANGE:
	case NETDEV_REGISTER:  /* Most likely no IP */
	case NETDEV_CHANGEADDR:  /* MAC addr change */
	case NETDEV_CHANGENAME:
	case NETDEV_FEAT_CHANGE:  /* Might be usefull when cell type changes */
		iface_stat_create(dev);
		break;
	case NETDEV_UNREGISTER:
		iface_stat_update(dev);
		break;
	}
	return NOTIFY_DONE;
}

static int iface_inetaddr_event_handler(struct notifier_block *nb,
					unsigned long event, void *ptr) {

	struct in_ifaddr *ifa = ptr;
	struct in_device *in_dev = ifa->ifa_dev;
	struct net_device *dev = in_dev->dev;

	pr_debug("iface_stat: inetaddr_event(): ev=0x%lx netdev=%p->name=%s\n",
		 event, dev, dev ? dev->name : "");

	switch (event) {
	case NETDEV_UP:
		iface_stat_create(dev);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block iface_netdev_notifier_blk = {
	.notifier_call = iface_netdev_event_handler,
};

static struct notifier_block iface_inetaddr_notifier_blk = {
	.notifier_call = iface_inetaddr_event_handler,
};

static int __init iface_stat_init(struct proc_dir_entry *parent_procdir)
{
	int err;

	iface_stat_procdir = proc_mkdir(iface_stat_procdirname, parent_procdir);
	if (!iface_stat_procdir) {
		pr_err("iface_stat: failed to create proc entry\n");
		err = -1;
		goto err;
	}
	err = register_netdevice_notifier(&iface_netdev_notifier_blk);
	if (err) {
		pr_err("iface_stat: failed to register dev event handler\n");
		goto err_unreg_nd;
	}
	err = register_inetaddr_notifier(&iface_inetaddr_notifier_blk);
	if (err) {
		pr_err("iface_stat: failed to register dev event handler\n");
		goto err_zap_entry;
	}
	return 0;

err_unreg_nd:
	unregister_netdevice_notifier(&iface_netdev_notifier_blk);
err_zap_entry:
	remove_proc_entry(iface_stat_procdirname, parent_procdir);
err:
	return err;
}

static struct sock *qtaguid_find_sk(const struct sk_buff *skb,
				    struct xt_action_param *par)
{
	struct sock *sk;
	unsigned int hook_mask = (1 << par->hooknum);

	pr_debug("xt_qtaguid: find_sk(skb=%p) hooknum=%d family=%d\n", skb,
		 par->hooknum, par->family);

	/* Let's not abuse the the xt_socket_get*_sk(), or else it will
	 * return garbage SKs. */
	if (!(hook_mask & XT_SOCKET_SUPPORTED_HOOKS))
		return NULL;

	switch (par->family) {
	case NFPROTO_IPV6:
		sk = xt_socket_get6_sk(skb, par);
		break;
	case NFPROTO_IPV4:
		sk = xt_socket_get4_sk(skb, par);
		break;
	default:
		return NULL;
	}

	/* Seems to be issues on the file ptr for TCP_TIME_WAIT SKs.
	 * http://kerneltrap.org/mailarchive/linux-netdev/2010/10/21/6287959
	 * Not fixed in 3.0-r3 :(
	 */
	if (sk) {
		pr_debug("xt_qtaguid: %p->sk_proto=%u "
			 "->sk_state=%d\n", sk, sk->sk_protocol, sk->sk_state);
		if (sk->sk_state  == TCP_TIME_WAIT) {
			xt_socket_put_sk(sk);
			sk = NULL;
		}
	}
	return sk;
}

static void account_for_uid(const struct sk_buff *skb,
			    const struct sock *alternate_sk, uid_t uid,
			    struct xt_action_param *par)
{
	const struct net_device *el_dev;

	if (!skb->dev) {
		pr_debug("xt_qtaguid[%d]: no skb->dev\n", par->hooknum);
		el_dev = par->in ? : par->out;
	} else {
		const struct net_device *other_dev;
		el_dev = skb->dev;
		other_dev = par->in ? : par->out;
		if (el_dev != other_dev) {
			pr_debug("xt_qtaguid[%d]: skb->dev=%p %s vs "
				"par->(in/out)=%p %s\n",
				par->hooknum, el_dev, el_dev->name, other_dev,
				other_dev->name);
		}
	}

	if (unlikely(!el_dev)) {
		pr_info("xt_qtaguid[%d]: no par->in/out?!!\n", par->hooknum);
	} else if (unlikely(!el_dev->name)) {
		pr_info("xt_qtaguid[%d]: no dev->name?!!\n", par->hooknum);
	} else {
		pr_debug("xt_qtaguid[%d]: dev name=%s type=%d\n",
			par->hooknum,
			el_dev->name,
			el_dev->type);

		if_tag_stat_update(el_dev->name, uid,
				skb->sk ? skb->sk : alternate_sk,
				par->in ? IFS_RX : IFS_TX,
				ip_hdr(skb)->protocol, skb->len);
	}
}

static bool qtaguid_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_qtaguid_match_info *info = par->matchinfo;
	const struct file *filp;
	bool got_sock = false;
	struct sock *sk;
	uid_t sock_uid;
	bool res;
	pr_debug("xt_qtaguid[%d]: entered skb=%p par->in=%p/out=%p fam=%d\n",
		 par->hooknum, skb, par->in, par->out, par->family);
	if (skb == NULL) {
		res = (info->match ^ info->invert) == 0;
		goto ret_res;
	}

	sk = skb->sk;

	if (sk == NULL) {
		/*  A missing sk->sk_socket happens when packets are in-flight
		 * and the matching socket is already closed and gone.
		 */
		sk = qtaguid_find_sk(skb, par);
		/* If we got the socket from the find_sk(), we will need to put
		 * it back, as nf_tproxy_get_sock_v4() got it. */
		got_sock = sk;
	}
	pr_debug("xt_qtaguid[%d]: sk=%p got_sock=%d proto=%d\n",
		par->hooknum, sk, got_sock, ip_hdr(skb)->protocol);
	if (sk != NULL) {
		pr_debug("xt_qtaguid[%d]: sk=%p->sk_socket=%p->file=%p\n",
			par->hooknum, sk, sk->sk_socket,
			sk->sk_socket ? sk->sk_socket->file : (void *)-1LL);
		filp = sk->sk_socket ? sk->sk_socket->file : NULL;
		pr_debug("xt_qtaguid[%d]: filp...uid=%d\n",
			par->hooknum, filp ? filp->f_cred->fsuid : -1);
	}

	if (sk == NULL || sk->sk_socket == NULL) {
		/* Here, the qtaguid_find_sk() using connection tracking
		 * couldn't find the owner, so for now we just count them
		 * against the system. */
		/* TODO: unhack how to force just accounting.
		 * For now we only do iface stats when the uid-owner is not
		 * requested */
		if (!(info->match & XT_QTAGUID_UID))
			account_for_uid(skb, sk, 0, par);
		pr_debug("xt_qtaguid[%d]: leaving (sk?sk->sk_socket)=%p\n",
			par->hooknum,
			sk ? sk->sk_socket : NULL);
		res =  (info->match ^ info->invert) == 0;
		goto put_sock_ret_res;
	} else if (info->match & info->invert & XT_QTAGUID_SOCKET) {
		res = false;
		goto put_sock_ret_res;
	}
	filp = sk->sk_socket->file;
	if (filp == NULL) {
		pr_debug("xt_qtaguid[%d]: leaving filp=NULL\n", par->hooknum);
		res = ((info->match ^ info->invert) &
			(XT_QTAGUID_UID | XT_QTAGUID_GID)) == 0;
		goto put_sock_ret_res;
	}
	sock_uid = filp->f_cred->fsuid;
	/* TODO: unhack how to force just accounting.
	 * For now we only do iface stats when the uid-owner is not requested */
	if (!(info->match & XT_QTAGUID_UID))
		account_for_uid(skb, sk, sock_uid, par);

	/* The following two tests fail the match when:
	 *    id not in range AND no inverted condition requested
	 * or id     in range AND    inverted condition requested
	 * Thus (!a && b) || (a && !b) == a ^ b
	 */
	if (info->match & XT_QTAGUID_UID)
		if ((filp->f_cred->fsuid >= info->uid_min &&
		     filp->f_cred->fsuid <= info->uid_max) ^
		    !(info->invert & XT_QTAGUID_UID)) {
			pr_debug("xt_qtaguid[%d]: leaving uid not matching\n",
				 par->hooknum);
			res = false;
			goto put_sock_ret_res;
		}
	if (info->match & XT_QTAGUID_GID)
		if ((filp->f_cred->fsgid >= info->gid_min &&
				filp->f_cred->fsgid <= info->gid_max) ^
			!(info->invert & XT_QTAGUID_GID)) {
			pr_debug("xt_qtaguid[%d]: leaving gid not matching\n",
				par->hooknum);
			res = false;
			goto put_sock_ret_res;
		}

	pr_debug("xt_qtaguid[%d]: leaving matched\n", par->hooknum);
	res = true;

put_sock_ret_res:
	if (got_sock)
		xt_socket_put_sk(sk);
ret_res:
	pr_debug("xt_qtaguid[%d]: left %d\n", par->hooknum, res);
	return res;
}

/*
 * Procfs reader to get all active socket tags using style "1)" as described in
 * fs/proc/generic.c
 */
static int qtaguid_ctrl_proc_read(char *page, char **num_items_returned,
				  off_t items_to_skip, int char_count, int *eof,
				  void *data)
{
	char *outp = page;
	int len;
	unsigned long flags;
	uid_t uid;
	struct sock_tag *sock_tag_entry;
	struct rb_node *node;
	int item_index = 0;

	pr_debug("xt_qtaguid:proc ctrl page=%p off=%ld char_count=%d *eof=%d\n",
		page, items_to_skip, char_count, *eof);

	if (*eof)
		return 0;

	spin_lock_irqsave(&sock_tag_list_lock, flags);
	for (node = rb_first(&sock_tag_tree);
	     node;
	     node = rb_next(node)) {
		if (item_index++ < items_to_skip)
			continue;
		sock_tag_entry =  rb_entry(node, struct sock_tag, node);
		uid = get_uid_from_tag(sock_tag_entry->tag);
		pr_debug("xt_qtaguid: proc_read(): sk=%p tag=0x%llx (uid=%d)\n",
			sock_tag_entry->sk,
			sock_tag_entry->tag,
			uid);
		len = snprintf(outp, char_count,
			       "sock=%p tag=0x%llx (uid=%u)\n",
			       sock_tag_entry->sk, sock_tag_entry->tag, uid);
		if (len >= char_count) {
			spin_unlock_irqrestore(&sock_tag_list_lock, flags);
			*outp = '\0';
			return outp - page;
		}
		outp += len;
		char_count -= len;
		(*num_items_returned)++;
	}
	spin_unlock_irqrestore(&sock_tag_list_lock, flags);
	*eof = 1;
	return outp - page;
}

static int qtaguid_ctrl_parse(const char *input, int count)
{
	char cmd;
	int sock_fd = 0;
	uid_t uid = 0;
	tag_t acct_tag = 0;
	struct socket *el_socket;
	int res, argc;
	struct sock_tag *sock_tag_entry;
	unsigned long flags;

	pr_debug("xt_qtaguid: ctrl(%s): entered\n", input);
	/* Unassigned args will get defaulted later. */
	/* TODO: get acct_tag_str, keep a list of available tags for the
	 * uid, use num as acct_tag. */
	argc = sscanf(input, "%c %d %llu %u", &cmd, &sock_fd, &acct_tag, &uid);
	pr_debug("xt_qtaguid: ctrl(%s): argc=%d cmd=%c sock_fd=%d "
		"acct_tag=0x%llx uid=%u\n", input, argc, cmd, sock_fd,
		acct_tag, uid);

	/* Collect params for commands */
	switch (cmd) {
	case 't':
	case 'u':
		if (argc < 2) {
			res = -EINVAL;
			goto err;
		}
		el_socket = sockfd_lookup(sock_fd, &res);
		if (!el_socket) {
			pr_info("xt_qtaguid: ctrl(%s): failed to lookup"
				" sock_fd=%d err=%d\n", input, sock_fd, res);
			goto err;
		}
		spin_lock_irqsave(&sock_tag_list_lock, flags);
		/* TODO: optim: pass in the current_fsuid() to do lookups
		 * as look ups will always be initiated form the same uid. */
		sock_tag_entry = get_sock_stat_nl(el_socket->sk);
		if (!sock_tag_entry)
			spin_unlock_irqrestore(&sock_tag_list_lock, flags);
		break;
	default:
		res = -EINVAL;
		goto err;
	}

	/* Process commands */
	switch (cmd) {

	case 't':
		if (argc < 2) {
			res = -EINVAL;
			goto err_unlock;
		}
		if (argc < 3) {
			acct_tag = 0;
		} else if (!valid_atag(acct_tag)) {
			res = -EINVAL;
			goto err_unlock;
		}
		if (argc < 4)
			uid = current_fsuid();
		if (!sock_tag_entry) {
			sock_tag_entry = kmalloc(sizeof(*sock_tag_entry),
						GFP_KERNEL);
			if (!sock_tag_entry) {
				res = -ENOMEM;
				goto err;
			}
			memset(sock_tag_entry, 0, sizeof(*sock_tag_entry));
			sock_tag_entry->sk = el_socket->sk;
			/* TODO: check that uid==current_fsuid() except
			 * for special uid/gid. */
			sock_tag_entry->tag = combine_atag_with_uid(acct_tag,
								uid);
			spin_lock_irqsave(&sock_tag_list_lock, flags);
			sock_tag_tree_insert(sock_tag_entry, &sock_tag_tree);
			spin_unlock_irqrestore(&sock_tag_list_lock, flags);
		} else {
			/* Just update the acct_tag portion. */
			uid_t orig_uid = get_uid_from_tag(sock_tag_entry->tag);
			sock_tag_entry->tag = combine_atag_with_uid(acct_tag,
								orig_uid);
		}
		pr_debug("xt_qtaguid: tag: sock_tag_entry->sk=%p "
			"...->tag=0x%llx (uid=%u)\n",
			sock_tag_entry->sk, sock_tag_entry->tag,
			get_uid_from_tag(sock_tag_entry->tag));
		break;

	case 'u':
		if (!sock_tag_entry) {
			res = -EINVAL;
			goto err;
		}
		/* TODO: check that the uid==current_fsuid()
		 * except for special uid/gid. */
		rb_erase(&sock_tag_entry->node, &sock_tag_tree);
		spin_unlock_irqrestore(&sock_tag_list_lock, flags);
		kfree(sock_tag_entry);
		break;
	}

	/* All of the input has been processed */
	res = count;
	goto ok;

err_unlock:
	if (!sock_tag_entry)
		spin_unlock_irqrestore(&sock_tag_list_lock, flags);
err:
ok:
	pr_debug("xt_qtaguid: ctrl(%s): res=%d\n", input, res);
	return res;
}

#define MAX_QTAGUID_CTRL_INPUT_LEN 255
static int qtaguid_ctrl_proc_write(struct file *file, const char __user *buffer,
			unsigned long count, void *data)
{
	char input_buf[MAX_QTAGUID_CTRL_INPUT_LEN];

	if (count >= MAX_QTAGUID_CTRL_INPUT_LEN)
		return -EINVAL;

	if (copy_from_user(input_buf, buffer, count))
		return -EFAULT;

	input_buf[count] = '\0';
	return qtaguid_ctrl_parse(input_buf, count);
}

/*
 * Procfs reader to get all tag stats using style "1)" as described in
 * fs/proc/generic.c
 * Groups all protocols tx/rx bytes.
 */
static int qtaguid_stats_proc_read(char *page, char **num_items_returned,
				off_t items_to_skip, int char_count, int *eof,
				void *data)
{
	char *outp = page;
	int len;
	unsigned long flags, flags2;
	struct iface_stat *iface_entry;
	struct tag_stat *ts_entry;
	int item_index = 0;

	/* TODO: make root access only */

	pr_debug("xt_qtaguid:proc stats page=%p *num_items_returned=%p off=%ld "
		"char_count=%d *eof=%d\n", page, *num_items_returned,
		items_to_skip, char_count, *eof);

	if (*eof)
		return 0;

	if (!items_to_skip) {
		/* The idx is there to help debug when things go belly up. */
		len = snprintf(outp, char_count,
			"idx iface acct_tag_hex uid_tag_int rx_bytes "
			"tx_bytes\n");
		/* Don't advance the outp unless the whole line was printed */
		if (len >= char_count) {
			*outp = '\0';
			return outp - page;
		}
		outp += len;
		char_count -= len;
	}

	spin_lock_irqsave(&iface_stat_list_lock, flags);
	list_for_each_entry(iface_entry, &iface_stat_list, list) {
		struct rb_node *node;
		spin_lock_irqsave(&iface_entry->tag_stat_list_lock, flags2);
		for (node = rb_first(&iface_entry->tag_stat_tree);
		     node;
		     node = rb_next(node)) {
			ts_entry =  rb_entry(node, struct tag_stat, node);
			if (item_index++ < items_to_skip)
				continue;
			len = snprintf(outp, char_count,
				       "%d %s 0x%llx %u %llu %llu\n",
				       item_index,
				       iface_entry->ifname,
				       get_atag_from_tag(ts_entry->tag),
				       get_uid_from_tag(ts_entry->tag),
				       dc_sum_bytes(&ts_entry->counters,
						    IFS_RX),
				       dc_sum_bytes(&ts_entry->counters,
						    IFS_TX));
			if (len >= char_count) {
				spin_unlock_irqrestore(
					&iface_entry->tag_stat_list_lock,
					flags2);
				spin_unlock_irqrestore(
					&iface_stat_list_lock, flags);
				*outp = '\0';
				return outp - page;
			}
			outp += len;
			char_count -= len;
			(*num_items_returned)++;
		}
		spin_unlock_irqrestore(&iface_entry->tag_stat_list_lock,
				flags2);
	}
	spin_unlock_irqrestore(&iface_stat_list_lock, flags);

	*eof = 1;
	return outp - page;
}

/*------------------------------------------*/
static const char *module_procdirname = "xt_qtaguid";
static struct proc_dir_entry *xt_qtaguid_procdir;
static struct proc_dir_entry *xt_qtaguid_ctrl_file;
static struct proc_dir_entry *xt_qtaguid_stats_file;

static int __init qtaguid_proc_register(struct proc_dir_entry **res_procdir)
{
	int ret;
	*res_procdir = proc_mkdir(module_procdirname, init_net.proc_net);
	if (!*res_procdir) {
		pr_err("xt_qtaguid: failed to create proc/.../xt_qtaguid\n");
		ret = -ENOMEM;
		goto no_dir;
	}

	xt_qtaguid_ctrl_file = create_proc_entry("ctrl", 0666,
						*res_procdir);
	if (!xt_qtaguid_ctrl_file) {
		pr_err("xt_qtaguid: failed to create xt_qtaguid/ctrl "
			" file\n");
		ret = -ENOMEM;
		goto no_ctrl_entry;
	}
	xt_qtaguid_ctrl_file->read_proc = qtaguid_ctrl_proc_read;
	xt_qtaguid_ctrl_file->write_proc = qtaguid_ctrl_proc_write;

	xt_qtaguid_stats_file = create_proc_entry("stats", 0666,
						*res_procdir);
	if (!xt_qtaguid_stats_file) {
		pr_err("xt_qtaguid: failed to create xt_qtaguid/stats "
			"file\n");
		ret = -ENOMEM;
		goto no_stats_entry;
	}
	/*
	 * TODO: add extra read_proc for full stats with protocol
	 * breakout
	 */
	xt_qtaguid_stats_file->read_proc = qtaguid_stats_proc_read;
	/*
	 * TODO: add support counter hacking
	 * xt_qtaguid_stats_file->write_proc = qtaguid_stats_proc_write;
	 */
	return 0;

no_stats_entry:
	remove_proc_entry("ctrl", *res_procdir);
no_ctrl_entry:
	remove_proc_entry("xt_qtaguid", NULL);
no_dir:
	return ret;
}

static struct xt_match qtaguid_mt_reg __read_mostly = {
	/*
	 * This module masquerades as the "owner" module so that iptables
	 * tools can deal with it.
	 */
	.name       = "owner",
	.revision   = 1,
	.family     = NFPROTO_UNSPEC,
	.match      = qtaguid_mt,
	.matchsize  = sizeof(struct xt_qtaguid_match_info),
	.me         = THIS_MODULE,
};

static int __init qtaguid_mt_init(void)
{
	if (qtaguid_proc_register(&xt_qtaguid_procdir)
	    || iface_stat_init(xt_qtaguid_procdir)
	    || xt_register_match(&qtaguid_mt_reg))
		return -1;
	return 0;
}

/* TODO: allow unloading of the module.
 * For now stats are permanent.
 * Kconfig forces'y/n' and never an 'm'.
 */

module_init(qtaguid_mt_init);
MODULE_AUTHOR("jpa <jpa@google.com>");
MODULE_DESCRIPTION("Xtables: socket owner+tag matching and associated stats");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_owner");
MODULE_ALIAS("ip6t_owner");
MODULE_ALIAS("ipt_qtaguid");
MODULE_ALIAS("ip6t_qtaguid");
