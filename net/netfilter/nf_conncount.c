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

#define CONNCOUNT_GC_MAX_NODES	8
#define MAX_KEYLEN		5

/* we will save the tuples of all connections we care about */
struct nf_conncount_tuple {
	struct list_head		node;
	struct nf_conntrack_tuple	tuple;
	struct nf_conntrack_zone	zone;
	int				cpu;
	u32				jiffies32;
};

struct nf_conncount_rb {
	struct rb_node node;
	struct nf_conncount_list list;
	u32 key[MAX_KEYLEN];
	struct rcu_head rcu_head;
};

static spinlock_t nf_conncount_locks[CONNCOUNT_SLOTS] __cacheline_aligned_in_smp;

struct nf_conncount_data {
	unsigned int keylen;
	struct rb_root root[CONNCOUNT_SLOTS];
	struct net *net;
	struct work_struct gc_work;
	unsigned long pending_trees[BITS_TO_LONGS(CONNCOUNT_SLOTS)];
	unsigned int gc_tree;
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

static void conn_free(struct nf_conncount_list *list,
		      struct nf_conncount_tuple *conn)
{
	lockdep_assert_held(&list->list_lock);

	list->count--;
	list_del(&conn->node);

	kmem_cache_free(conncount_conn_cachep, conn);
}

static const struct nf_conntrack_tuple_hash *
find_or_evict(struct net *net, struct nf_conncount_list *list,
	      struct nf_conncount_tuple *conn)
{
	const struct nf_conntrack_tuple_hash *found;
	unsigned long a, b;
	int cpu = raw_smp_processor_id();
	u32 age;

	found = nf_conntrack_find_get(net, &conn->zone, &conn->tuple);
	if (found)
		return found;
	b = conn->jiffies32;
	a = (u32)jiffies;

	/* conn might have been added just before by another cpu and
	 * might still be unconfirmed.  In this case, nf_conntrack_find()
	 * returns no result.  Thus only evict if this cpu added the
	 * stale entry or if the entry is older than two jiffies.
	 */
	age = a - b;
	if (conn->cpu == cpu || age >= 2) {
		conn_free(list, conn);
		return ERR_PTR(-ENOENT);
	}

	return ERR_PTR(-EAGAIN);
}

static int __nf_conncount_add(struct net *net,
			      struct nf_conncount_list *list,
			      const struct nf_conntrack_tuple *tuple,
			      const struct nf_conntrack_zone *zone)
{
	const struct nf_conntrack_tuple_hash *found;
	struct nf_conncount_tuple *conn, *conn_n;
	struct nf_conn *found_ct;
	unsigned int collect = 0;

	/* check the saved connections */
	list_for_each_entry_safe(conn, conn_n, &list->head, node) {
		if (collect > CONNCOUNT_GC_MAX_NODES)
			break;

		found = find_or_evict(net, list, conn);
		if (IS_ERR(found)) {
			/* Not found, but might be about to be confirmed */
			if (PTR_ERR(found) == -EAGAIN) {
				if (nf_ct_tuple_equal(&conn->tuple, tuple) &&
				    nf_ct_zone_id(&conn->zone, conn->zone.dir) ==
				    nf_ct_zone_id(zone, zone->dir))
					return 0; /* already exists */
			} else {
				collect++;
			}
			continue;
		}

		found_ct = nf_ct_tuplehash_to_ctrack(found);

		if (nf_ct_tuple_equal(&conn->tuple, tuple) &&
		    nf_ct_zone_equal(found_ct, zone, zone->dir)) {
			/*
			 * We should not see tuples twice unless someone hooks
			 * this into a table without "-p tcp --syn".
			 *
			 * Attempt to avoid a re-add in this case.
			 */
			nf_ct_put(found_ct);
			return 0;
		} else if (already_closed(found_ct)) {
			/*
			 * we do not care about connections which are
			 * closed already -> ditch it
			 */
			nf_ct_put(found_ct);
			conn_free(list, conn);
			collect++;
			continue;
		}

		nf_ct_put(found_ct);
	}

	if (WARN_ON_ONCE(list->count > INT_MAX))
		return -EOVERFLOW;

	conn = kmem_cache_alloc(conncount_conn_cachep, GFP_ATOMIC);
	if (conn == NULL)
		return -ENOMEM;

	conn->tuple = *tuple;
	conn->zone = *zone;
	conn->cpu = raw_smp_processor_id();
	conn->jiffies32 = (u32)jiffies;
	list_add_tail(&conn->node, &list->head);
	list->count++;
	return 0;
}

int nf_conncount_add(struct net *net,
		     struct nf_conncount_list *list,
		     const struct nf_conntrack_tuple *tuple,
		     const struct nf_conntrack_zone *zone)
{
	int ret;

	/* check the saved connections */
	spin_lock_bh(&list->list_lock);
	ret = __nf_conncount_add(net, list, tuple, zone);
	spin_unlock_bh(&list->list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(nf_conncount_add);

void nf_conncount_list_init(struct nf_conncount_list *list)
{
	spin_lock_init(&list->list_lock);
	INIT_LIST_HEAD(&list->head);
	list->count = 0;
}
EXPORT_SYMBOL_GPL(nf_conncount_list_init);

/* Return true if the list is empty. Must be called with BH disabled. */
bool nf_conncount_gc_list(struct net *net,
			  struct nf_conncount_list *list)
{
	const struct nf_conntrack_tuple_hash *found;
	struct nf_conncount_tuple *conn, *conn_n;
	struct nf_conn *found_ct;
	unsigned int collected = 0;
	bool ret = false;

	/* don't bother if other cpu is already doing GC */
	if (!spin_trylock(&list->list_lock))
		return false;

	list_for_each_entry_safe(conn, conn_n, &list->head, node) {
		found = find_or_evict(net, list, conn);
		if (IS_ERR(found)) {
			if (PTR_ERR(found) == -ENOENT)
				collected++;
			continue;
		}

		found_ct = nf_ct_tuplehash_to_ctrack(found);
		if (already_closed(found_ct)) {
			/*
			 * we do not care about connections which are
			 * closed already -> ditch it
			 */
			nf_ct_put(found_ct);
			conn_free(list, conn);
			collected++;
			continue;
		}

		nf_ct_put(found_ct);
		if (collected > CONNCOUNT_GC_MAX_NODES)
			break;
	}

	if (!list->count)
		ret = true;
	spin_unlock(&list->list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(nf_conncount_gc_list);

static void __tree_nodes_free(struct rcu_head *h)
{
	struct nf_conncount_rb *rbconn;

	rbconn = container_of(h, struct nf_conncount_rb, rcu_head);
	kmem_cache_free(conncount_rb_cachep, rbconn);
}

/* caller must hold tree nf_conncount_locks[] lock */
static void tree_nodes_free(struct rb_root *root,
			    struct nf_conncount_rb *gc_nodes[],
			    unsigned int gc_count)
{
	struct nf_conncount_rb *rbconn;

	while (gc_count) {
		rbconn = gc_nodes[--gc_count];
		spin_lock(&rbconn->list.list_lock);
		if (!rbconn->list.count) {
			rb_erase(&rbconn->node, root);
			call_rcu(&rbconn->rcu_head, __tree_nodes_free);
		}
		spin_unlock(&rbconn->list.list_lock);
	}
}

static void schedule_gc_worker(struct nf_conncount_data *data, int tree)
{
	set_bit(tree, data->pending_trees);
	schedule_work(&data->gc_work);
}

static unsigned int
insert_tree(struct net *net,
	    struct nf_conncount_data *data,
	    struct rb_root *root,
	    unsigned int hash,
	    const u32 *key,
	    const struct nf_conntrack_tuple *tuple,
	    const struct nf_conntrack_zone *zone)
{
	struct nf_conncount_rb *gc_nodes[CONNCOUNT_GC_MAX_NODES];
	struct rb_node **rbnode, *parent;
	struct nf_conncount_rb *rbconn;
	struct nf_conncount_tuple *conn;
	unsigned int count = 0, gc_count = 0;
	u8 keylen = data->keylen;
	bool do_gc = true;

	spin_lock_bh(&nf_conncount_locks[hash]);
restart:
	parent = NULL;
	rbnode = &(root->rb_node);
	while (*rbnode) {
		int diff;
		rbconn = rb_entry(*rbnode, struct nf_conncount_rb, node);

		parent = *rbnode;
		diff = key_diff(key, rbconn->key, keylen);
		if (diff < 0) {
			rbnode = &((*rbnode)->rb_left);
		} else if (diff > 0) {
			rbnode = &((*rbnode)->rb_right);
		} else {
			int ret;

			ret = nf_conncount_add(net, &rbconn->list, tuple, zone);
			if (ret)
				count = 0; /* hotdrop */
			else
				count = rbconn->list.count;
			tree_nodes_free(root, gc_nodes, gc_count);
			goto out_unlock;
		}

		if (gc_count >= ARRAY_SIZE(gc_nodes))
			continue;

		if (do_gc && nf_conncount_gc_list(net, &rbconn->list))
			gc_nodes[gc_count++] = rbconn;
	}

	if (gc_count) {
		tree_nodes_free(root, gc_nodes, gc_count);
		schedule_gc_worker(data, hash);
		gc_count = 0;
		do_gc = false;
		goto restart;
	}

	/* expected case: match, insert new node */
	rbconn = kmem_cache_alloc(conncount_rb_cachep, GFP_ATOMIC);
	if (rbconn == NULL)
		goto out_unlock;

	conn = kmem_cache_alloc(conncount_conn_cachep, GFP_ATOMIC);
	if (conn == NULL) {
		kmem_cache_free(conncount_rb_cachep, rbconn);
		goto out_unlock;
	}

	conn->tuple = *tuple;
	conn->zone = *zone;
	memcpy(rbconn->key, key, sizeof(u32) * keylen);

	nf_conncount_list_init(&rbconn->list);
	list_add(&conn->node, &rbconn->list.head);
	count = 1;
	rbconn->list.count = count;

	rb_link_node_rcu(&rbconn->node, parent, rbnode);
	rb_insert_color(&rbconn->node, root);
out_unlock:
	spin_unlock_bh(&nf_conncount_locks[hash]);
	return count;
}

static unsigned int
count_tree(struct net *net,
	   struct nf_conncount_data *data,
	   const u32 *key,
	   const struct nf_conntrack_tuple *tuple,
	   const struct nf_conntrack_zone *zone)
{
	struct rb_root *root;
	struct rb_node *parent;
	struct nf_conncount_rb *rbconn;
	unsigned int hash;
	u8 keylen = data->keylen;

	hash = jhash2(key, data->keylen, conncount_rnd) % CONNCOUNT_SLOTS;
	root = &data->root[hash];

	parent = rcu_dereference_raw(root->rb_node);
	while (parent) {
		int diff;

		rbconn = rb_entry(parent, struct nf_conncount_rb, node);

		diff = key_diff(key, rbconn->key, keylen);
		if (diff < 0) {
			parent = rcu_dereference_raw(parent->rb_left);
		} else if (diff > 0) {
			parent = rcu_dereference_raw(parent->rb_right);
		} else {
			int ret;

			if (!tuple) {
				nf_conncount_gc_list(net, &rbconn->list);
				return rbconn->list.count;
			}

			spin_lock_bh(&rbconn->list.list_lock);
			/* Node might be about to be free'd.
			 * We need to defer to insert_tree() in this case.
			 */
			if (rbconn->list.count == 0) {
				spin_unlock_bh(&rbconn->list.list_lock);
				break;
			}

			/* same source network -> be counted! */
			ret = __nf_conncount_add(net, &rbconn->list, tuple, zone);
			spin_unlock_bh(&rbconn->list.list_lock);
			if (ret)
				return 0; /* hotdrop */
			else
				return rbconn->list.count;
		}
	}

	if (!tuple)
		return 0;

	return insert_tree(net, data, root, hash, key, tuple, zone);
}

static void tree_gc_worker(struct work_struct *work)
{
	struct nf_conncount_data *data = container_of(work, struct nf_conncount_data, gc_work);
	struct nf_conncount_rb *gc_nodes[CONNCOUNT_GC_MAX_NODES], *rbconn;
	struct rb_root *root;
	struct rb_node *node;
	unsigned int tree, next_tree, gc_count = 0;

	tree = data->gc_tree % CONNCOUNT_SLOTS;
	root = &data->root[tree];

	local_bh_disable();
	rcu_read_lock();
	for (node = rb_first(root); node != NULL; node = rb_next(node)) {
		rbconn = rb_entry(node, struct nf_conncount_rb, node);
		if (nf_conncount_gc_list(data->net, &rbconn->list))
			gc_count++;
	}
	rcu_read_unlock();
	local_bh_enable();

	cond_resched();

	spin_lock_bh(&nf_conncount_locks[tree]);
	if (gc_count < ARRAY_SIZE(gc_nodes))
		goto next; /* do not bother */

	gc_count = 0;
	node = rb_first(root);
	while (node != NULL) {
		rbconn = rb_entry(node, struct nf_conncount_rb, node);
		node = rb_next(node);

		if (rbconn->list.count > 0)
			continue;

		gc_nodes[gc_count++] = rbconn;
		if (gc_count >= ARRAY_SIZE(gc_nodes)) {
			tree_nodes_free(root, gc_nodes, gc_count);
			gc_count = 0;
		}
	}

	tree_nodes_free(root, gc_nodes, gc_count);
next:
	clear_bit(tree, data->pending_trees);

	next_tree = (tree + 1) % CONNCOUNT_SLOTS;
	next_tree = find_next_bit(data->pending_trees, CONNCOUNT_SLOTS, next_tree);

	if (next_tree < CONNCOUNT_SLOTS) {
		data->gc_tree = next_tree;
		schedule_work(work);
	}

	spin_unlock_bh(&nf_conncount_locks[tree]);
}

/* Count and return number of conntrack entries in 'net' with particular 'key'.
 * If 'tuple' is not null, insert it into the accounting data structure.
 * Call with RCU read lock.
 */
unsigned int nf_conncount_count(struct net *net,
				struct nf_conncount_data *data,
				const u32 *key,
				const struct nf_conntrack_tuple *tuple,
				const struct nf_conntrack_zone *zone)
{
	return count_tree(net, data, key, tuple, zone);
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
	data->net = net;
	INIT_WORK(&data->gc_work, tree_gc_worker);

	return data;
}
EXPORT_SYMBOL_GPL(nf_conncount_init);

void nf_conncount_cache_free(struct nf_conncount_list *list)
{
	struct nf_conncount_tuple *conn, *conn_n;

	list_for_each_entry_safe(conn, conn_n, &list->head, node)
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

		nf_conncount_cache_free(&rbconn->list);

		kmem_cache_free(conncount_rb_cachep, rbconn);
	}
}

void nf_conncount_destroy(struct net *net, unsigned int family,
			  struct nf_conncount_data *data)
{
	unsigned int i;

	cancel_work_sync(&data->gc_work);
	nf_ct_netns_put(net, family);

	for (i = 0; i < ARRAY_SIZE(data->root); ++i)
		destroy_tree(&data->root[i]);

	kfree(data);
}
EXPORT_SYMBOL_GPL(nf_conncount_destroy);

static int __init nf_conncount_modinit(void)
{
	int i;

	for (i = 0; i < CONNCOUNT_SLOTS; ++i)
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
