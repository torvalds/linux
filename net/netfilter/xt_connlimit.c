/*
 * netfilter module to limit the number of parallel tcp
 * connections per IP address.
 *   (c) 2000 Gerd Knorr <kraxel@bytesex.org>
 *   Nov 2002: Martin Bene <martin.bene@icomedias.com>:
 *		only ignore TIME_WAIT or gone connections
 *   (C) CC Computer Consultants GmbH, 2007
 *
 * based on ...
 *
 * Kernel module to match connection tracking information.
 * GPL (C) 1999  Rusty Russell (rusty@rustcorp.com.au).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/netfilter/nf_conntrack_tcp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connlimit.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>

#define CONNLIMIT_SLOTS		256U

#ifdef CONFIG_LOCKDEP
#define CONNLIMIT_LOCK_SLOTS	8U
#else
#define CONNLIMIT_LOCK_SLOTS	256U
#endif

#define CONNLIMIT_GC_MAX_NODES	8

/* we will save the tuples of all connections we care about */
struct xt_connlimit_conn {
	struct hlist_node		node;
	struct nf_conntrack_tuple	tuple;
	union nf_inet_addr		addr;
};

struct xt_connlimit_rb {
	struct rb_node node;
	struct hlist_head hhead; /* connections/hosts in same subnet */
	union nf_inet_addr addr; /* search key */
};

static spinlock_t xt_connlimit_locks[CONNLIMIT_LOCK_SLOTS] __cacheline_aligned_in_smp;

struct xt_connlimit_data {
	struct rb_root climit_root4[CONNLIMIT_SLOTS];
	struct rb_root climit_root6[CONNLIMIT_SLOTS];
};

static u_int32_t connlimit_rnd __read_mostly;
static struct kmem_cache *connlimit_rb_cachep __read_mostly;
static struct kmem_cache *connlimit_conn_cachep __read_mostly;

static inline unsigned int connlimit_iphash(__be32 addr)
{
	return jhash_1word((__force __u32)addr,
			    connlimit_rnd) % CONNLIMIT_SLOTS;
}

static inline unsigned int
connlimit_iphash6(const union nf_inet_addr *addr,
                  const union nf_inet_addr *mask)
{
	union nf_inet_addr res;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(addr->ip6); ++i)
		res.ip6[i] = addr->ip6[i] & mask->ip6[i];

	return jhash2((u32 *)res.ip6, ARRAY_SIZE(res.ip6),
		       connlimit_rnd) % CONNLIMIT_SLOTS;
}

static inline bool already_closed(const struct nf_conn *conn)
{
	if (nf_ct_protonum(conn) == IPPROTO_TCP)
		return conn->proto.tcp.state == TCP_CONNTRACK_TIME_WAIT ||
		       conn->proto.tcp.state == TCP_CONNTRACK_CLOSE;
	else
		return 0;
}

static int
same_source_net(const union nf_inet_addr *addr,
		const union nf_inet_addr *mask,
		const union nf_inet_addr *u3, u_int8_t family)
{
	if (family == NFPROTO_IPV4) {
		return ntohl(addr->ip & mask->ip) -
		       ntohl(u3->ip & mask->ip);
	} else {
		union nf_inet_addr lh, rh;
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(addr->ip6); ++i) {
			lh.ip6[i] = addr->ip6[i] & mask->ip6[i];
			rh.ip6[i] = u3->ip6[i] & mask->ip6[i];
		}

		return memcmp(&lh.ip6, &rh.ip6, sizeof(lh.ip6));
	}
}

static bool add_hlist(struct hlist_head *head,
		      const struct nf_conntrack_tuple *tuple,
		      const union nf_inet_addr *addr)
{
	struct xt_connlimit_conn *conn;

	conn = kmem_cache_alloc(connlimit_conn_cachep, GFP_ATOMIC);
	if (conn == NULL)
		return false;
	conn->tuple = *tuple;
	conn->addr = *addr;
	hlist_add_head(&conn->node, head);
	return true;
}

static unsigned int check_hlist(struct net *net,
				struct hlist_head *head,
				const struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_zone *zone,
				bool *addit)
{
	const struct nf_conntrack_tuple_hash *found;
	struct xt_connlimit_conn *conn;
	struct hlist_node *n;
	struct nf_conn *found_ct;
	unsigned int length = 0;

	*addit = true;
	rcu_read_lock();

	/* check the saved connections */
	hlist_for_each_entry_safe(conn, n, head, node) {
		found = nf_conntrack_find_get(net, zone, &conn->tuple);
		if (found == NULL) {
			hlist_del(&conn->node);
			kmem_cache_free(connlimit_conn_cachep, conn);
			continue;
		}

		found_ct = nf_ct_tuplehash_to_ctrack(found);

		if (nf_ct_tuple_equal(&conn->tuple, tuple)) {
			/*
			 * Just to be sure we have it only once in the list.
			 * We should not see tuples twice unless someone hooks
			 * this into a table without "-p tcp --syn".
			 */
			*addit = false;
		} else if (already_closed(found_ct)) {
			/*
			 * we do not care about connections which are
			 * closed already -> ditch it
			 */
			nf_ct_put(found_ct);
			hlist_del(&conn->node);
			kmem_cache_free(connlimit_conn_cachep, conn);
			continue;
		}

		nf_ct_put(found_ct);
		length++;
	}

	rcu_read_unlock();

	return length;
}

static void tree_nodes_free(struct rb_root *root,
			    struct xt_connlimit_rb *gc_nodes[],
			    unsigned int gc_count)
{
	struct xt_connlimit_rb *rbconn;

	while (gc_count) {
		rbconn = gc_nodes[--gc_count];
		rb_erase(&rbconn->node, root);
		kmem_cache_free(connlimit_rb_cachep, rbconn);
	}
}

static unsigned int
count_tree(struct net *net, struct rb_root *root,
	   const struct nf_conntrack_tuple *tuple,
	   const union nf_inet_addr *addr, const union nf_inet_addr *mask,
	   u8 family, const struct nf_conntrack_zone *zone)
{
	struct xt_connlimit_rb *gc_nodes[CONNLIMIT_GC_MAX_NODES];
	struct rb_node **rbnode, *parent;
	struct xt_connlimit_rb *rbconn;
	struct xt_connlimit_conn *conn;
	unsigned int gc_count;
	bool no_gc = false;

 restart:
	gc_count = 0;
	parent = NULL;
	rbnode = &(root->rb_node);
	while (*rbnode) {
		int diff;
		bool addit;

		rbconn = container_of(*rbnode, struct xt_connlimit_rb, node);

		parent = *rbnode;
		diff = same_source_net(addr, mask, &rbconn->addr, family);
		if (diff < 0) {
			rbnode = &((*rbnode)->rb_left);
		} else if (diff > 0) {
			rbnode = &((*rbnode)->rb_right);
		} else {
			/* same source network -> be counted! */
			unsigned int count;
			count = check_hlist(net, &rbconn->hhead, tuple, zone, &addit);

			tree_nodes_free(root, gc_nodes, gc_count);
			if (!addit)
				return count;

			if (!add_hlist(&rbconn->hhead, tuple, addr))
				return 0; /* hotdrop */

			return count + 1;
		}

		if (no_gc || gc_count >= ARRAY_SIZE(gc_nodes))
			continue;

		/* only used for GC on hhead, retval and 'addit' ignored */
		check_hlist(net, &rbconn->hhead, tuple, zone, &addit);
		if (hlist_empty(&rbconn->hhead))
			gc_nodes[gc_count++] = rbconn;
	}

	if (gc_count) {
		no_gc = true;
		tree_nodes_free(root, gc_nodes, gc_count);
		/* tree_node_free before new allocation permits
		 * allocator to re-use newly free'd object.
		 *
		 * This is a rare event; in most cases we will find
		 * existing node to re-use. (or gc_count is 0).
		 */
		goto restart;
	}

	/* no match, need to insert new node */
	rbconn = kmem_cache_alloc(connlimit_rb_cachep, GFP_ATOMIC);
	if (rbconn == NULL)
		return 0;

	conn = kmem_cache_alloc(connlimit_conn_cachep, GFP_ATOMIC);
	if (conn == NULL) {
		kmem_cache_free(connlimit_rb_cachep, rbconn);
		return 0;
	}

	conn->tuple = *tuple;
	conn->addr = *addr;
	rbconn->addr = *addr;

	INIT_HLIST_HEAD(&rbconn->hhead);
	hlist_add_head(&conn->node, &rbconn->hhead);

	rb_link_node(&rbconn->node, parent, rbnode);
	rb_insert_color(&rbconn->node, root);
	return 1;
}

static int count_them(struct net *net,
		      struct xt_connlimit_data *data,
		      const struct nf_conntrack_tuple *tuple,
		      const union nf_inet_addr *addr,
		      const union nf_inet_addr *mask,
		      u_int8_t family,
		      const struct nf_conntrack_zone *zone)
{
	struct rb_root *root;
	int count;
	u32 hash;

	if (family == NFPROTO_IPV6) {
		hash = connlimit_iphash6(addr, mask);
		root = &data->climit_root6[hash];
	} else {
		hash = connlimit_iphash(addr->ip & mask->ip);
		root = &data->climit_root4[hash];
	}

	spin_lock_bh(&xt_connlimit_locks[hash % CONNLIMIT_LOCK_SLOTS]);

	count = count_tree(net, root, tuple, addr, mask, family, zone);

	spin_unlock_bh(&xt_connlimit_locks[hash % CONNLIMIT_LOCK_SLOTS]);

	return count;
}

static bool
connlimit_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct net *net = xt_net(par);
	const struct xt_connlimit_info *info = par->matchinfo;
	union nf_inet_addr addr;
	struct nf_conntrack_tuple tuple;
	const struct nf_conntrack_tuple *tuple_ptr = &tuple;
	const struct nf_conntrack_zone *zone = &nf_ct_zone_dflt;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int connections;

	ct = nf_ct_get(skb, &ctinfo);
	if (ct != NULL) {
		tuple_ptr = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
		zone = nf_ct_zone(ct);
	} else if (!nf_ct_get_tuplepr(skb, skb_network_offset(skb),
				      xt_family(par), net, &tuple)) {
		goto hotdrop;
	}

	if (xt_family(par) == NFPROTO_IPV6) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);
		memcpy(&addr.ip6, (info->flags & XT_CONNLIMIT_DADDR) ?
		       &iph->daddr : &iph->saddr, sizeof(addr.ip6));
	} else {
		const struct iphdr *iph = ip_hdr(skb);
		addr.ip = (info->flags & XT_CONNLIMIT_DADDR) ?
			  iph->daddr : iph->saddr;
	}

	connections = count_them(net, info->data, tuple_ptr, &addr,
	                         &info->mask, xt_family(par), zone);
	if (connections == 0)
		/* kmalloc failed, drop it entirely */
		goto hotdrop;

	return (connections > info->limit) ^
	       !!(info->flags & XT_CONNLIMIT_INVERT);

 hotdrop:
	par->hotdrop = true;
	return false;
}

static int connlimit_mt_check(const struct xt_mtchk_param *par)
{
	struct xt_connlimit_info *info = par->matchinfo;
	unsigned int i;
	int ret;

	net_get_random_once(&connlimit_rnd, sizeof(connlimit_rnd));

	ret = nf_ct_netns_get(par->net, par->family);
	if (ret < 0) {
		pr_info("cannot load conntrack support for "
			"address family %u\n", par->family);
		return ret;
	}

	/* init private data */
	info->data = kmalloc(sizeof(struct xt_connlimit_data), GFP_KERNEL);
	if (info->data == NULL) {
		nf_ct_netns_put(par->net, par->family);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(info->data->climit_root4); ++i)
		info->data->climit_root4[i] = RB_ROOT;
	for (i = 0; i < ARRAY_SIZE(info->data->climit_root6); ++i)
		info->data->climit_root6[i] = RB_ROOT;

	return 0;
}

static void destroy_tree(struct rb_root *r)
{
	struct xt_connlimit_conn *conn;
	struct xt_connlimit_rb *rbconn;
	struct hlist_node *n;
	struct rb_node *node;

	while ((node = rb_first(r)) != NULL) {
		rbconn = container_of(node, struct xt_connlimit_rb, node);

		rb_erase(node, r);

		hlist_for_each_entry_safe(conn, n, &rbconn->hhead, node)
			kmem_cache_free(connlimit_conn_cachep, conn);

		kmem_cache_free(connlimit_rb_cachep, rbconn);
	}
}

static void connlimit_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_connlimit_info *info = par->matchinfo;
	unsigned int i;

	nf_ct_netns_put(par->net, par->family);

	for (i = 0; i < ARRAY_SIZE(info->data->climit_root4); ++i)
		destroy_tree(&info->data->climit_root4[i]);
	for (i = 0; i < ARRAY_SIZE(info->data->climit_root6); ++i)
		destroy_tree(&info->data->climit_root6[i]);

	kfree(info->data);
}

static struct xt_match connlimit_mt_reg __read_mostly = {
	.name       = "connlimit",
	.revision   = 1,
	.family     = NFPROTO_UNSPEC,
	.checkentry = connlimit_mt_check,
	.match      = connlimit_mt,
	.matchsize  = sizeof(struct xt_connlimit_info),
	.destroy    = connlimit_mt_destroy,
	.me         = THIS_MODULE,
};

static int __init connlimit_mt_init(void)
{
	int ret, i;

	BUILD_BUG_ON(CONNLIMIT_LOCK_SLOTS > CONNLIMIT_SLOTS);
	BUILD_BUG_ON((CONNLIMIT_SLOTS % CONNLIMIT_LOCK_SLOTS) != 0);

	for (i = 0; i < CONNLIMIT_LOCK_SLOTS; ++i)
		spin_lock_init(&xt_connlimit_locks[i]);

	connlimit_conn_cachep = kmem_cache_create("xt_connlimit_conn",
					   sizeof(struct xt_connlimit_conn),
					   0, 0, NULL);
	if (!connlimit_conn_cachep)
		return -ENOMEM;

	connlimit_rb_cachep = kmem_cache_create("xt_connlimit_rb",
					   sizeof(struct xt_connlimit_rb),
					   0, 0, NULL);
	if (!connlimit_rb_cachep) {
		kmem_cache_destroy(connlimit_conn_cachep);
		return -ENOMEM;
	}
	ret = xt_register_match(&connlimit_mt_reg);
	if (ret != 0) {
		kmem_cache_destroy(connlimit_conn_cachep);
		kmem_cache_destroy(connlimit_rb_cachep);
	}
	return ret;
}

static void __exit connlimit_mt_exit(void)
{
	xt_unregister_match(&connlimit_mt_reg);
	kmem_cache_destroy(connlimit_conn_cachep);
	kmem_cache_destroy(connlimit_rb_cachep);
}

module_init(connlimit_mt_init);
module_exit(connlimit_mt_exit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: Number of connections matching");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_connlimit");
MODULE_ALIAS("ip6t_connlimit");
