/*
 * count the number of connections matching an arbitrary key.
 *
 * (C) 2017 Red Hat GmbH
 * Author: Florian Westphal <fw@strlen.de>
 *
 * split from xt_connlimit.c:
 *   (c) 2000 Gerd Knorr <kraxel@bytesex.org>
 *   Nov 2002: Martin Bene <martin.bene@icomedias.com>:
 *		only ignore TIME_WAIT or gone connections
 *   (C) CC Computer Consultants GmbH, 2007
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
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_count.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_zones.h>

#define CONNCOUNT_SLOTS		256U

#ifdef CONFIG_LOCKDEP
#define CONNCOUNT_LOCK_SLOTS	8U
#else
#define CONNCOUNT_LOCK_SLOTS	256U
#endif

#define CONNCOUNT_GC_MAX_NODES	8
#define MAX_KEYLEN		5

/* we will save the tuples of all connections we care about */
struct nf_conncount_tuple {
	struct hlist_node		node;
	struct nf_conntrack_tuple	tuple;
	struct nf_conntrack_zone	zone;
};

struct nf_conncount_rb {
	struct rb_node node;
	struct hlist_head hhead; /* connections/hosts in same subnet */
	u32 key[MAX_KEYLEN];
};

static spinlock_t nf_conncount_locks[CONNCOUNT_LOCK_SLOTS] __cacheline_aligned_in_smp;

struct nf_conncount_data {
	unsigned int keylen;
	struct rb_root root[CONNCOUNT_SLOTS];
};

static u_int32_t conncount_rnd __read_mostly;
static struct kmem_cache *conncount_rb_cachep __read_mostly;
static struct kmem_cache *conncount_conn_cachep __read_mostly;

static inline bool already_closed(const struct nf_conn *conn)
{
	if (nf_ct_protonum(conn) == IPPROTO_TCP)
		return conn->proto.tcp.state == TCP_CONNTRACK_TIME_WAIT ||
		       conn->proto.tcp.state == TCP_CONNTRACK_CLOSE;
	else
		return false;
}

static int key_diff(const u32 *a, const u32 *b, unsigned int klen)
{
	return memcmp(a, b, klen * sizeof(u32));
}

bool nf_conncount_add(struct hlist_head *head,
		      const struct nf_conntrack_tuple *tuple,
		      const struct nf_conntrack_zone *zone)
{
	struct nf_conncount_tuple *conn;

	conn = kmem_cache_alloc(conncount_conn_cachep, GFP_ATOMIC);
	if (conn == NULL)
		return false;
	conn->tuple = *tuple;
	conn->zone = *zone;
	hlist_add_head(&conn->node, head);
	return true;
}
EXPORT_SYMBOL_GPL(nf_conncount_add);

unsigned int nf_conncount_lookup(struct net *net, struct hlist_head *head,
				 const struct nf_conntrack_tuple *tuple,
				 const struct nf_conntrack_zone *zone,
				 bool *addit)
{
	const struct nf_conntrack_tuple_hash *found;
	struct nf_conncount_tuple *conn;
	struct hlist_node *n;
	struct nf_conn *found_ct;
	unsigned int length = 0;

	*addit = tuple ? true : false;

	/* check the saved connections */
	hlist_for_each_entry_safe(conn, n, head, node) {
		found = nf_conntrack_find_get(net, &conn->zone, &conn->tuple);
		if (found == NULL) {
			hlist_del(&conn->node);
			kmem_cache_free(conncount_conn_cachep, conn);
			continue;
		}

		found_ct = nf_ct_tuplehash_to_ctrack(found);

		if (tuple && nf_ct_tuple_equal(&conn->tuple, tuple) &&
		    nf_ct_zone_equal(found_ct, zone, zone->dir)) {
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
			kmem_cache_free(conncount_conn_cachep, conn);
			continue;
		}

		nf_ct_put(found_ct);
		length++;
	}

	return length;
}
EXPORT_SYMBOL_GPL(nf_conncount_lookup);

static void tree_nodes_free(struct rb_root *root,
			    struct nf_conncount_rb *gc_nodes[],
			    unsigned int gc_count)
{
	struct nf_conncount_rb *rbconn;

	while (gc_count) {
		rbconn = gc_nodes[--gc_count];
		rb_erase(&rbconn->node, root);
		kmem_cache_free(conncount_rb_cachep, rbconn);
	}
}

static unsigned int
count_tree(struct net *net, struct rb_root *root,
	   const u32 *key, u8 keylen,
	   const struct nf_conntrack_tuple *tuple,
	   const struct nf_conntrack_zone *zone)
{
	struct nf_conncount_rb *gc_nodes[CONNCOUNT_GC_MAX_NODES];
	struct rb_node **rbnode, *parent;
	struct nf_conncount_rb *rbconn;
	struct nf_conncount_tuple *conn;
	unsigned int gc_count;
	bool no_gc = false;

 restart:
	gc_count = 0;
	parent = NULL;
	rbnode = &(root->rb_node);
	while (*rbnode) {
		int diff;
		bool addit;

		rbconn = rb_entry(*rbnode, struct nf_conncount_rb, node);

		parent = *rbnode;
		diff = key_diff(key, rbconn->key, keylen);
		if (diff < 0) {
			rbnode = &((*rbnode)->rb_left);
		} else if (diff > 0) {
			rbnode = &((*rbnode)->rb_right);
		} else {
			/* same source network -> be counted! */
			unsigned int count;

			count = nf_conncount_lookup(net, &rbconn->hhead, tuple,
						    zone, &addit);

			tree_nodes_free(root, gc_nodes, gc_count);
			if (!addit)
				return count;

			if (!nf_conncount_add(&rbconn->hhead, tuple, zone))
				return 0; /* hotdrop */

			return count + 1;
		}

		if (no_gc || gc_count >= ARRAY_SIZE(gc_nodes))
			continue;

		/* only used for GC on hhead, retval and 'addit' ignored */
		nf_conncount_lookup(net, &rbconn->hhead, tuple, zone, &addit);
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

	if (!tuple)
		return 0;

	/* no match, need to insert new node */
	rbconn = kmem_cache_alloc(conncount_rb_cachep, GFP_ATOMIC);
	if (rbconn == NULL)
		return 0;

	conn = kmem_cache_alloc(conncount_conn_cachep, GFP_ATOMIC);
	if (conn == NULL) {
		kmem_cache_free(conncount_rb_cachep, rbconn);
		return 0;
	}

	conn->tuple = *tuple;
	conn->zone = *zone;
	memcpy(rbconn->key, key, sizeof(u32) * keylen);

	INIT_HLIST_HEAD(&rbconn->hhead);
	hlist_add_head(&conn->node, &rbconn->hhead);

	rb_link_node(&rbconn->node, parent, rbnode);
	rb_insert_color(&rbconn->node, root);
	return 1;
}

/* Count and return number of conntrack entries in 'net' with particular 'key'.
 * If 'tuple' is not null, insert it into the accounting data structure.
 */
unsigned int nf_conncount_count(struct net *net,
				struct nf_conncount_data *data,
				const u32 *key,
				const struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_zone *zone)
{
	struct rb_root *root;
	int count;
	u32 hash;

	hash = jhash2(key, data->keylen, conncount_rnd) % CONNCOUNT_SLOTS;
	root = &data->root[hash];

	spin_lock_bh(&nf_conncount_locks[hash % CONNCOUNT_LOCK_SLOTS]);

	count = count_tree(net, root, key, data->keylen, tuple, zone);

	spin_unlock_bh(&nf_conncount_locks[hash % CONNCOUNT_LOCK_SLOTS]);

	return count;
}
EXPORT_SYMBOL_GPL(nf_conncount_count);

struct nf_conncount_data *nf_conncount_init(struct net *net, unsigned int family,
					    unsigned int keylen)
{
	struct nf_conncount_data *data;
	int ret, i;

	if (keylen % sizeof(u32) ||
	    keylen / sizeof(u32) > MAX_KEYLEN ||
	    keylen == 0)
		return ERR_PTR(-EINVAL);

	net_get_random_once(&conncount_rnd, sizeof(conncount_rnd));

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	ret = nf_ct_netns_get(net, family);
	if (ret < 0) {
		kfree(data);
		return ERR_PTR(ret);
	}

	for (i = 0; i < ARRAY_SIZE(data->root); ++i)
		data->root[i] = RB_ROOT;

	data->keylen = keylen / sizeof(u32);

	return data;
}
EXPORT_SYMBOL_GPL(nf_conncount_init);

void nf_conncount_cache_free(struct hlist_head *hhead)
{
	struct nf_conncount_tuple *conn;
	struct hlist_node *n;

	hlist_for_each_entry_safe(conn, n, hhead, node)
		kmem_cache_free(conncount_conn_cachep, conn);
}
EXPORT_SYMBOL_GPL(nf_conncount_cache_free);

static void destroy_tree(struct rb_root *r)
{
	struct nf_conncount_rb *rbconn;
	struct rb_node *node;

	while ((node = rb_first(r)) != NULL) {
		rbconn = rb_entry(node, struct nf_conncount_rb, node);

		rb_erase(node, r);

		nf_conncount_cache_free(&rbconn->hhead);

		kmem_cache_free(conncount_rb_cachep, rbconn);
	}
}

void nf_conncount_destroy(struct net *net, unsigned int family,
			  struct nf_conncount_data *data)
{
	unsigned int i;

	nf_ct_netns_put(net, family);

	for (i = 0; i < ARRAY_SIZE(data->root); ++i)
		destroy_tree(&data->root[i]);

	kfree(data);
}
EXPORT_SYMBOL_GPL(nf_conncount_destroy);

static int __init nf_conncount_modinit(void)
{
	int i;

	BUILD_BUG_ON(CONNCOUNT_LOCK_SLOTS > CONNCOUNT_SLOTS);
	BUILD_BUG_ON((CONNCOUNT_SLOTS % CONNCOUNT_LOCK_SLOTS) != 0);

	for (i = 0; i < CONNCOUNT_LOCK_SLOTS; ++i)
		spin_lock_init(&nf_conncount_locks[i]);

	conncount_conn_cachep = kmem_cache_create("nf_conncount_tuple",
					   sizeof(struct nf_conncount_tuple),
					   0, 0, NULL);
	if (!conncount_conn_cachep)
		return -ENOMEM;

	conncount_rb_cachep = kmem_cache_create("nf_conncount_rb",
					   sizeof(struct nf_conncount_rb),
					   0, 0, NULL);
	if (!conncount_rb_cachep) {
		kmem_cache_destroy(conncount_conn_cachep);
		return -ENOMEM;
	}

	return 0;
}

static void __exit nf_conncount_modexit(void)
{
	kmem_cache_destroy(conncount_conn_cachep);
	kmem_cache_destroy(conncount_rb_cachep);
}

module_init(nf_conncount_modinit);
module_exit(nf_conncount_modexit);
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_DESCRIPTION("netfilter: count number of connections matching a key");
MODULE_LICENSE("GPL");
