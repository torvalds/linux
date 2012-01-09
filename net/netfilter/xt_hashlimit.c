/*
 *	xt_hashlimit - Netfilter module to limit the number of packets per time
 *	separately for each hashbucket (sourceip/sourceport/dstip/dstport)
 *
 *	(C) 2003-2004 by Harald Welte <laforge@netfilter.org>
 *	Copyright © CC Computer Consultants GmbH, 2007 - 2008
 *
 * Development of this code was funded by Astaro AG, http://www.astaro.com/
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/ip.h>
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
#include <linux/ipv6.h>
#include <net/ipv6.h>
#endif

#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/xt_hashlimit.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: per hash-bucket rate-limit match");
MODULE_ALIAS("ipt_hashlimit");
MODULE_ALIAS("ip6t_hashlimit");

struct hashlimit_net {
	struct hlist_head	htables;
	struct proc_dir_entry	*ipt_hashlimit;
	struct proc_dir_entry	*ip6t_hashlimit;
};

static int hashlimit_net_id;
static inline struct hashlimit_net *hashlimit_pernet(struct net *net)
{
	return net_generic(net, hashlimit_net_id);
}

/* need to declare this at the top */
static const struct file_operations dl_file_ops;

/* hash table crap */
struct dsthash_dst {
	union {
		struct {
			__be32 src;
			__be32 dst;
		} ip;
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
		struct {
			__be32 src[4];
			__be32 dst[4];
		} ip6;
#endif
	};
	__be16 src_port;
	__be16 dst_port;
};

struct dsthash_ent {
	/* static / read-only parts in the beginning */
	struct hlist_node node;
	struct dsthash_dst dst;

	/* modified structure members in the end */
	spinlock_t lock;
	unsigned long expires;		/* precalculated expiry time */
	struct {
		unsigned long prev;	/* last modification */
		u_int32_t credit;
		u_int32_t credit_cap, cost;
	} rateinfo;
	struct rcu_head rcu;
};

struct xt_hashlimit_htable {
	struct hlist_node node;		/* global list of all htables */
	int use;
	u_int8_t family;
	bool rnd_initialized;

	struct hashlimit_cfg1 cfg;	/* config */

	/* used internally */
	spinlock_t lock;		/* lock for list_head */
	u_int32_t rnd;			/* random seed for hash */
	unsigned int count;		/* number entries in table */
	struct timer_list timer;	/* timer for gc */

	/* seq_file stuff */
	struct proc_dir_entry *pde;
	struct net *net;

	struct hlist_head hash[0];	/* hashtable itself */
};

static DEFINE_MUTEX(hashlimit_mutex);	/* protects htables list */
static struct kmem_cache *hashlimit_cachep __read_mostly;

static inline bool dst_cmp(const struct dsthash_ent *ent,
			   const struct dsthash_dst *b)
{
	return !memcmp(&ent->dst, b, sizeof(ent->dst));
}

static u_int32_t
hash_dst(const struct xt_hashlimit_htable *ht, const struct dsthash_dst *dst)
{
	u_int32_t hash = jhash2((const u32 *)dst,
				sizeof(*dst)/sizeof(u32),
				ht->rnd);
	/*
	 * Instead of returning hash % ht->cfg.size (implying a divide)
	 * we return the high 32 bits of the (hash * ht->cfg.size) that will
	 * give results between [0 and cfg.size-1] and same hash distribution,
	 * but using a multiply, less expensive than a divide
	 */
	return ((u64)hash * ht->cfg.size) >> 32;
}

static struct dsthash_ent *
dsthash_find(const struct xt_hashlimit_htable *ht,
	     const struct dsthash_dst *dst)
{
	struct dsthash_ent *ent;
	struct hlist_node *pos;
	u_int32_t hash = hash_dst(ht, dst);

	if (!hlist_empty(&ht->hash[hash])) {
		hlist_for_each_entry_rcu(ent, pos, &ht->hash[hash], node)
			if (dst_cmp(ent, dst)) {
				spin_lock(&ent->lock);
				return ent;
			}
	}
	return NULL;
}

/* allocate dsthash_ent, initialize dst, put in htable and lock it */
static struct dsthash_ent *
dsthash_alloc_init(struct xt_hashlimit_htable *ht,
		   const struct dsthash_dst *dst)
{
	struct dsthash_ent *ent;

	spin_lock(&ht->lock);
	/* initialize hash with random val at the time we allocate
	 * the first hashtable entry */
	if (unlikely(!ht->rnd_initialized)) {
		get_random_bytes(&ht->rnd, sizeof(ht->rnd));
		ht->rnd_initialized = true;
	}

	if (ht->cfg.max && ht->count >= ht->cfg.max) {
		/* FIXME: do something. question is what.. */
		if (net_ratelimit())
			pr_err("max count of %u reached\n", ht->cfg.max);
		ent = NULL;
	} else
		ent = kmem_cache_alloc(hashlimit_cachep, GFP_ATOMIC);
	if (ent) {
		memcpy(&ent->dst, dst, sizeof(ent->dst));
		spin_lock_init(&ent->lock);

		spin_lock(&ent->lock);
		hlist_add_head_rcu(&ent->node, &ht->hash[hash_dst(ht, dst)]);
		ht->count++;
	}
	spin_unlock(&ht->lock);
	return ent;
}

static void dsthash_free_rcu(struct rcu_head *head)
{
	struct dsthash_ent *ent = container_of(head, struct dsthash_ent, rcu);

	kmem_cache_free(hashlimit_cachep, ent);
}

static inline void
dsthash_free(struct xt_hashlimit_htable *ht, struct dsthash_ent *ent)
{
	hlist_del_rcu(&ent->node);
	call_rcu_bh(&ent->rcu, dsthash_free_rcu);
	ht->count--;
}
static void htable_gc(unsigned long htlong);

static int htable_create(struct net *net, struct xt_hashlimit_mtinfo1 *minfo,
			 u_int8_t family)
{
	struct hashlimit_net *hashlimit_net = hashlimit_pernet(net);
	struct xt_hashlimit_htable *hinfo;
	unsigned int size;
	unsigned int i;

	if (minfo->cfg.size) {
		size = minfo->cfg.size;
	} else {
		size = (totalram_pages << PAGE_SHIFT) / 16384 /
		       sizeof(struct list_head);
		if (totalram_pages > 1024 * 1024 * 1024 / PAGE_SIZE)
			size = 8192;
		if (size < 16)
			size = 16;
	}
	/* FIXME: don't use vmalloc() here or anywhere else -HW */
	hinfo = vmalloc(sizeof(struct xt_hashlimit_htable) +
	                sizeof(struct list_head) * size);
	if (hinfo == NULL)
		return -ENOMEM;
	minfo->hinfo = hinfo;

	/* copy match config into hashtable config */
	memcpy(&hinfo->cfg, &minfo->cfg, sizeof(hinfo->cfg));
	hinfo->cfg.size = size;
	if (hinfo->cfg.max == 0)
		hinfo->cfg.max = 8 * hinfo->cfg.size;
	else if (hinfo->cfg.max < hinfo->cfg.size)
		hinfo->cfg.max = hinfo->cfg.size;

	for (i = 0; i < hinfo->cfg.size; i++)
		INIT_HLIST_HEAD(&hinfo->hash[i]);

	hinfo->use = 1;
	hinfo->count = 0;
	hinfo->family = family;
	hinfo->rnd_initialized = false;
	spin_lock_init(&hinfo->lock);

	hinfo->pde = proc_create_data(minfo->name, 0,
		(family == NFPROTO_IPV4) ?
		hashlimit_net->ipt_hashlimit : hashlimit_net->ip6t_hashlimit,
		&dl_file_ops, hinfo);
	if (hinfo->pde == NULL) {
		vfree(hinfo);
		return -ENOMEM;
	}
	hinfo->net = net;

	setup_timer(&hinfo->timer, htable_gc, (unsigned long)hinfo);
	hinfo->timer.expires = jiffies + msecs_to_jiffies(hinfo->cfg.gc_interval);
	add_timer(&hinfo->timer);

	hlist_add_head(&hinfo->node, &hashlimit_net->htables);

	return 0;
}

static bool select_all(const struct xt_hashlimit_htable *ht,
		       const struct dsthash_ent *he)
{
	return 1;
}

static bool select_gc(const struct xt_hashlimit_htable *ht,
		      const struct dsthash_ent *he)
{
	return time_after_eq(jiffies, he->expires);
}

static void htable_selective_cleanup(struct xt_hashlimit_htable *ht,
			bool (*select)(const struct xt_hashlimit_htable *ht,
				      const struct dsthash_ent *he))
{
	unsigned int i;

	/* lock hash table and iterate over it */
	spin_lock_bh(&ht->lock);
	for (i = 0; i < ht->cfg.size; i++) {
		struct dsthash_ent *dh;
		struct hlist_node *pos, *n;
		hlist_for_each_entry_safe(dh, pos, n, &ht->hash[i], node) {
			if ((*select)(ht, dh))
				dsthash_free(ht, dh);
		}
	}
	spin_unlock_bh(&ht->lock);
}

/* hash table garbage collector, run by timer */
static void htable_gc(unsigned long htlong)
{
	struct xt_hashlimit_htable *ht = (struct xt_hashlimit_htable *)htlong;

	htable_selective_cleanup(ht, select_gc);

	/* re-add the timer accordingly */
	ht->timer.expires = jiffies + msecs_to_jiffies(ht->cfg.gc_interval);
	add_timer(&ht->timer);
}

static void htable_destroy(struct xt_hashlimit_htable *hinfo)
{
	struct hashlimit_net *hashlimit_net = hashlimit_pernet(hinfo->net);
	struct proc_dir_entry *parent;

	del_timer_sync(&hinfo->timer);

	if (hinfo->family == NFPROTO_IPV4)
		parent = hashlimit_net->ipt_hashlimit;
	else
		parent = hashlimit_net->ip6t_hashlimit;
	remove_proc_entry(hinfo->pde->name, parent);
	htable_selective_cleanup(hinfo, select_all);
	vfree(hinfo);
}

static struct xt_hashlimit_htable *htable_find_get(struct net *net,
						   const char *name,
						   u_int8_t family)
{
	struct hashlimit_net *hashlimit_net = hashlimit_pernet(net);
	struct xt_hashlimit_htable *hinfo;
	struct hlist_node *pos;

	hlist_for_each_entry(hinfo, pos, &hashlimit_net->htables, node) {
		if (!strcmp(name, hinfo->pde->name) &&
		    hinfo->family == family) {
			hinfo->use++;
			return hinfo;
		}
	}
	return NULL;
}

static void htable_put(struct xt_hashlimit_htable *hinfo)
{
	mutex_lock(&hashlimit_mutex);
	if (--hinfo->use == 0) {
		hlist_del(&hinfo->node);
		htable_destroy(hinfo);
	}
	mutex_unlock(&hashlimit_mutex);
}

/* The algorithm used is the Simple Token Bucket Filter (TBF)
 * see net/sched/sch_tbf.c in the linux source tree
 */

/* Rusty: This is my (non-mathematically-inclined) understanding of
   this algorithm.  The `average rate' in jiffies becomes your initial
   amount of credit `credit' and the most credit you can ever have
   `credit_cap'.  The `peak rate' becomes the cost of passing the
   test, `cost'.

   `prev' tracks the last packet hit: you gain one credit per jiffy.
   If you get credit balance more than this, the extra credit is
   discarded.  Every time the match passes, you lose `cost' credits;
   if you don't have that many, the test fails.

   See Alexey's formal explanation in net/sched/sch_tbf.c.

   To get the maximum range, we multiply by this factor (ie. you get N
   credits per jiffy).  We want to allow a rate as low as 1 per day
   (slowest userspace tool allows), which means
   CREDITS_PER_JIFFY*HZ*60*60*24 < 2^32 ie.
*/
#define MAX_CPJ (0xFFFFFFFF / (HZ*60*60*24))

/* Repeated shift and or gives us all 1s, final shift and add 1 gives
 * us the power of 2 below the theoretical max, so GCC simply does a
 * shift. */
#define _POW2_BELOW2(x) ((x)|((x)>>1))
#define _POW2_BELOW4(x) (_POW2_BELOW2(x)|_POW2_BELOW2((x)>>2))
#define _POW2_BELOW8(x) (_POW2_BELOW4(x)|_POW2_BELOW4((x)>>4))
#define _POW2_BELOW16(x) (_POW2_BELOW8(x)|_POW2_BELOW8((x)>>8))
#define _POW2_BELOW32(x) (_POW2_BELOW16(x)|_POW2_BELOW16((x)>>16))
#define POW2_BELOW32(x) ((_POW2_BELOW32(x)>>1) + 1)

#define CREDITS_PER_JIFFY POW2_BELOW32(MAX_CPJ)

/* Precision saver. */
static inline u_int32_t
user2credits(u_int32_t user)
{
	/* If multiplying would overflow... */
	if (user > 0xFFFFFFFF / (HZ*CREDITS_PER_JIFFY))
		/* Divide first. */
		return (user / XT_HASHLIMIT_SCALE) * HZ * CREDITS_PER_JIFFY;

	return (user * HZ * CREDITS_PER_JIFFY) / XT_HASHLIMIT_SCALE;
}

static inline void rateinfo_recalc(struct dsthash_ent *dh, unsigned long now)
{
	dh->rateinfo.credit += (now - dh->rateinfo.prev) * CREDITS_PER_JIFFY;
	if (dh->rateinfo.credit > dh->rateinfo.credit_cap)
		dh->rateinfo.credit = dh->rateinfo.credit_cap;
	dh->rateinfo.prev = now;
}

static inline __be32 maskl(__be32 a, unsigned int l)
{
	return l ? htonl(ntohl(a) & ~0 << (32 - l)) : 0;
}

#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
static void hashlimit_ipv6_mask(__be32 *i, unsigned int p)
{
	switch (p) {
	case 0 ... 31:
		i[0] = maskl(i[0], p);
		i[1] = i[2] = i[3] = 0;
		break;
	case 32 ... 63:
		i[1] = maskl(i[1], p - 32);
		i[2] = i[3] = 0;
		break;
	case 64 ... 95:
		i[2] = maskl(i[2], p - 64);
		i[3] = 0;
		break;
	case 96 ... 127:
		i[3] = maskl(i[3], p - 96);
		break;
	case 128:
		break;
	}
}
#endif

static int
hashlimit_init_dst(const struct xt_hashlimit_htable *hinfo,
		   struct dsthash_dst *dst,
		   const struct sk_buff *skb, unsigned int protoff)
{
	__be16 _ports[2], *ports;
	u8 nexthdr;
	__be16 frag_off;
	int poff;

	memset(dst, 0, sizeof(*dst));

	switch (hinfo->family) {
	case NFPROTO_IPV4:
		if (hinfo->cfg.mode & XT_HASHLIMIT_HASH_DIP)
			dst->ip.dst = maskl(ip_hdr(skb)->daddr,
			              hinfo->cfg.dstmask);
		if (hinfo->cfg.mode & XT_HASHLIMIT_HASH_SIP)
			dst->ip.src = maskl(ip_hdr(skb)->saddr,
			              hinfo->cfg.srcmask);

		if (!(hinfo->cfg.mode &
		      (XT_HASHLIMIT_HASH_DPT | XT_HASHLIMIT_HASH_SPT)))
			return 0;
		nexthdr = ip_hdr(skb)->protocol;
		break;
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	case NFPROTO_IPV6:
		if (hinfo->cfg.mode & XT_HASHLIMIT_HASH_DIP) {
			memcpy(&dst->ip6.dst, &ipv6_hdr(skb)->daddr,
			       sizeof(dst->ip6.dst));
			hashlimit_ipv6_mask(dst->ip6.dst, hinfo->cfg.dstmask);
		}
		if (hinfo->cfg.mode & XT_HASHLIMIT_HASH_SIP) {
			memcpy(&dst->ip6.src, &ipv6_hdr(skb)->saddr,
			       sizeof(dst->ip6.src));
			hashlimit_ipv6_mask(dst->ip6.src, hinfo->cfg.srcmask);
		}

		if (!(hinfo->cfg.mode &
		      (XT_HASHLIMIT_HASH_DPT | XT_HASHLIMIT_HASH_SPT)))
			return 0;
		nexthdr = ipv6_hdr(skb)->nexthdr;
		protoff = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr), &nexthdr, &frag_off);
		if ((int)protoff < 0)
			return -1;
		break;
#endif
	default:
		BUG();
		return 0;
	}

	poff = proto_ports_offset(nexthdr);
	if (poff >= 0) {
		ports = skb_header_pointer(skb, protoff + poff, sizeof(_ports),
					   &_ports);
	} else {
		_ports[0] = _ports[1] = 0;
		ports = _ports;
	}
	if (!ports)
		return -1;
	if (hinfo->cfg.mode & XT_HASHLIMIT_HASH_SPT)
		dst->src_port = ports[0];
	if (hinfo->cfg.mode & XT_HASHLIMIT_HASH_DPT)
		dst->dst_port = ports[1];
	return 0;
}

static bool
hashlimit_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_hashlimit_mtinfo1 *info = par->matchinfo;
	struct xt_hashlimit_htable *hinfo = info->hinfo;
	unsigned long now = jiffies;
	struct dsthash_ent *dh;
	struct dsthash_dst dst;

	if (hashlimit_init_dst(hinfo, &dst, skb, par->thoff) < 0)
		goto hotdrop;

	rcu_read_lock_bh();
	dh = dsthash_find(hinfo, &dst);
	if (dh == NULL) {
		dh = dsthash_alloc_init(hinfo, &dst);
		if (dh == NULL) {
			rcu_read_unlock_bh();
			goto hotdrop;
		}
		dh->expires = jiffies + msecs_to_jiffies(hinfo->cfg.expire);
		dh->rateinfo.prev = jiffies;
		dh->rateinfo.credit = user2credits(hinfo->cfg.avg *
		                      hinfo->cfg.burst);
		dh->rateinfo.credit_cap = user2credits(hinfo->cfg.avg *
		                          hinfo->cfg.burst);
		dh->rateinfo.cost = user2credits(hinfo->cfg.avg);
	} else {
		/* update expiration timeout */
		dh->expires = now + msecs_to_jiffies(hinfo->cfg.expire);
		rateinfo_recalc(dh, now);
	}

	if (dh->rateinfo.credit >= dh->rateinfo.cost) {
		/* below the limit */
		dh->rateinfo.credit -= dh->rateinfo.cost;
		spin_unlock(&dh->lock);
		rcu_read_unlock_bh();
		return !(info->cfg.mode & XT_HASHLIMIT_INVERT);
	}

	spin_unlock(&dh->lock);
	rcu_read_unlock_bh();
	/* default match is underlimit - so over the limit, we need to invert */
	return info->cfg.mode & XT_HASHLIMIT_INVERT;

 hotdrop:
	par->hotdrop = true;
	return false;
}

static int hashlimit_mt_check(const struct xt_mtchk_param *par)
{
	struct net *net = par->net;
	struct xt_hashlimit_mtinfo1 *info = par->matchinfo;
	int ret;

	/* Check for overflow. */
	if (info->cfg.burst == 0 ||
	    user2credits(info->cfg.avg * info->cfg.burst) <
	    user2credits(info->cfg.avg)) {
		pr_info("overflow, try lower: %u/%u\n",
			info->cfg.avg, info->cfg.burst);
		return -ERANGE;
	}
	if (info->cfg.gc_interval == 0 || info->cfg.expire == 0)
		return -EINVAL;
	if (info->name[sizeof(info->name)-1] != '\0')
		return -EINVAL;
	if (par->family == NFPROTO_IPV4) {
		if (info->cfg.srcmask > 32 || info->cfg.dstmask > 32)
			return -EINVAL;
	} else {
		if (info->cfg.srcmask > 128 || info->cfg.dstmask > 128)
			return -EINVAL;
	}

	mutex_lock(&hashlimit_mutex);
	info->hinfo = htable_find_get(net, info->name, par->family);
	if (info->hinfo == NULL) {
		ret = htable_create(net, info, par->family);
		if (ret < 0) {
			mutex_unlock(&hashlimit_mutex);
			return ret;
		}
	}
	mutex_unlock(&hashlimit_mutex);
	return 0;
}

static void hashlimit_mt_destroy(const struct xt_mtdtor_param *par)
{
	const struct xt_hashlimit_mtinfo1 *info = par->matchinfo;

	htable_put(info->hinfo);
}

static struct xt_match hashlimit_mt_reg[] __read_mostly = {
	{
		.name           = "hashlimit",
		.revision       = 1,
		.family         = NFPROTO_IPV4,
		.match          = hashlimit_mt,
		.matchsize      = sizeof(struct xt_hashlimit_mtinfo1),
		.checkentry     = hashlimit_mt_check,
		.destroy        = hashlimit_mt_destroy,
		.me             = THIS_MODULE,
	},
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	{
		.name           = "hashlimit",
		.revision       = 1,
		.family         = NFPROTO_IPV6,
		.match          = hashlimit_mt,
		.matchsize      = sizeof(struct xt_hashlimit_mtinfo1),
		.checkentry     = hashlimit_mt_check,
		.destroy        = hashlimit_mt_destroy,
		.me             = THIS_MODULE,
	},
#endif
};

/* PROC stuff */
static void *dl_seq_start(struct seq_file *s, loff_t *pos)
	__acquires(htable->lock)
{
	struct xt_hashlimit_htable *htable = s->private;
	unsigned int *bucket;

	spin_lock_bh(&htable->lock);
	if (*pos >= htable->cfg.size)
		return NULL;

	bucket = kmalloc(sizeof(unsigned int), GFP_ATOMIC);
	if (!bucket)
		return ERR_PTR(-ENOMEM);

	*bucket = *pos;
	return bucket;
}

static void *dl_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct xt_hashlimit_htable *htable = s->private;
	unsigned int *bucket = (unsigned int *)v;

	*pos = ++(*bucket);
	if (*pos >= htable->cfg.size) {
		kfree(v);
		return NULL;
	}
	return bucket;
}

static void dl_seq_stop(struct seq_file *s, void *v)
	__releases(htable->lock)
{
	struct xt_hashlimit_htable *htable = s->private;
	unsigned int *bucket = (unsigned int *)v;

	if (!IS_ERR(bucket))
		kfree(bucket);
	spin_unlock_bh(&htable->lock);
}

static int dl_seq_real_show(struct dsthash_ent *ent, u_int8_t family,
				   struct seq_file *s)
{
	int res;

	spin_lock(&ent->lock);
	/* recalculate to show accurate numbers */
	rateinfo_recalc(ent, jiffies);

	switch (family) {
	case NFPROTO_IPV4:
		res = seq_printf(s, "%ld %pI4:%u->%pI4:%u %u %u %u\n",
				 (long)(ent->expires - jiffies)/HZ,
				 &ent->dst.ip.src,
				 ntohs(ent->dst.src_port),
				 &ent->dst.ip.dst,
				 ntohs(ent->dst.dst_port),
				 ent->rateinfo.credit, ent->rateinfo.credit_cap,
				 ent->rateinfo.cost);
		break;
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	case NFPROTO_IPV6:
		res = seq_printf(s, "%ld %pI6:%u->%pI6:%u %u %u %u\n",
				 (long)(ent->expires - jiffies)/HZ,
				 &ent->dst.ip6.src,
				 ntohs(ent->dst.src_port),
				 &ent->dst.ip6.dst,
				 ntohs(ent->dst.dst_port),
				 ent->rateinfo.credit, ent->rateinfo.credit_cap,
				 ent->rateinfo.cost);
		break;
#endif
	default:
		BUG();
		res = 0;
	}
	spin_unlock(&ent->lock);
	return res;
}

static int dl_seq_show(struct seq_file *s, void *v)
{
	struct xt_hashlimit_htable *htable = s->private;
	unsigned int *bucket = (unsigned int *)v;
	struct dsthash_ent *ent;
	struct hlist_node *pos;

	if (!hlist_empty(&htable->hash[*bucket])) {
		hlist_for_each_entry(ent, pos, &htable->hash[*bucket], node)
			if (dl_seq_real_show(ent, htable->family, s))
				return -1;
	}
	return 0;
}

static const struct seq_operations dl_seq_ops = {
	.start = dl_seq_start,
	.next  = dl_seq_next,
	.stop  = dl_seq_stop,
	.show  = dl_seq_show
};

static int dl_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &dl_seq_ops);

	if (!ret) {
		struct seq_file *sf = file->private_data;
		sf->private = PDE(inode)->data;
	}
	return ret;
}

static const struct file_operations dl_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dl_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static int __net_init hashlimit_proc_net_init(struct net *net)
{
	struct hashlimit_net *hashlimit_net = hashlimit_pernet(net);

	hashlimit_net->ipt_hashlimit = proc_mkdir("ipt_hashlimit", net->proc_net);
	if (!hashlimit_net->ipt_hashlimit)
		return -ENOMEM;
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	hashlimit_net->ip6t_hashlimit = proc_mkdir("ip6t_hashlimit", net->proc_net);
	if (!hashlimit_net->ip6t_hashlimit) {
		proc_net_remove(net, "ipt_hashlimit");
		return -ENOMEM;
	}
#endif
	return 0;
}

static void __net_exit hashlimit_proc_net_exit(struct net *net)
{
	proc_net_remove(net, "ipt_hashlimit");
#if IS_ENABLED(CONFIG_IP6_NF_IPTABLES)
	proc_net_remove(net, "ip6t_hashlimit");
#endif
}

static int __net_init hashlimit_net_init(struct net *net)
{
	struct hashlimit_net *hashlimit_net = hashlimit_pernet(net);

	INIT_HLIST_HEAD(&hashlimit_net->htables);
	return hashlimit_proc_net_init(net);
}

static void __net_exit hashlimit_net_exit(struct net *net)
{
	struct hashlimit_net *hashlimit_net = hashlimit_pernet(net);

	BUG_ON(!hlist_empty(&hashlimit_net->htables));
	hashlimit_proc_net_exit(net);
}

static struct pernet_operations hashlimit_net_ops = {
	.init	= hashlimit_net_init,
	.exit	= hashlimit_net_exit,
	.id	= &hashlimit_net_id,
	.size	= sizeof(struct hashlimit_net),
};

static int __init hashlimit_mt_init(void)
{
	int err;

	err = register_pernet_subsys(&hashlimit_net_ops);
	if (err < 0)
		return err;
	err = xt_register_matches(hashlimit_mt_reg,
	      ARRAY_SIZE(hashlimit_mt_reg));
	if (err < 0)
		goto err1;

	err = -ENOMEM;
	hashlimit_cachep = kmem_cache_create("xt_hashlimit",
					    sizeof(struct dsthash_ent), 0, 0,
					    NULL);
	if (!hashlimit_cachep) {
		pr_warning("unable to create slab cache\n");
		goto err2;
	}
	return 0;

err2:
	xt_unregister_matches(hashlimit_mt_reg, ARRAY_SIZE(hashlimit_mt_reg));
err1:
	unregister_pernet_subsys(&hashlimit_net_ops);
	return err;

}

static void __exit hashlimit_mt_exit(void)
{
	xt_unregister_matches(hashlimit_mt_reg, ARRAY_SIZE(hashlimit_mt_reg));
	unregister_pernet_subsys(&hashlimit_net_ops);

	rcu_barrier_bh();
	kmem_cache_destroy(hashlimit_cachep);
}

module_init(hashlimit_mt_init);
module_exit(hashlimit_mt_exit);
