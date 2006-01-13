/* iptables match extension to limit the number of packets per second
 * seperately for each hashbucket (sourceip/sourceport/dstip/dstport)
 *
 * (C) 2003-2004 by Harald Welte <laforge@netfilter.org>
 *
 * $Id: ipt_hashlimit.c 3244 2004-10-20 16:24:29Z laforge@netfilter.org $
 *
 * Development of this code was funded by Astaro AG, http://www.astaro.com/
 *
 * based on ipt_limit.c by:
 * Jérôme de Vivie	<devivie@info.enserb.u-bordeaux.fr>
 * Hervé Eychenne	<eychenne@info.enserb.u-bordeaux.fr>
 * Rusty Russell	<rusty@rustcorp.com.au>
 *
 * The general idea is to create a hash table for every dstip and have a
 * seperate limit counter per tuple.  This way you can do something like 'limit
 * the number of syn packets for each of my internal addresses.
 *
 * Ideally this would just be implemented as a general 'hash' match, which would
 * allow us to attach any iptables target to it's hash buckets.  But this is
 * not possible in the current iptables architecture.  As always, pkttables for
 * 2.7.x will help ;)
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/sctp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_hashlimit.h>

/* FIXME: this is just for IP_NF_ASSERRT */
#include <linux/netfilter_ipv4/ip_conntrack.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables match for limiting per hash-bucket");

/* need to declare this at the top */
static struct proc_dir_entry *hashlimit_procdir;
static struct file_operations dl_file_ops;

/* hash table crap */

struct dsthash_dst {
	u_int32_t src_ip;
	u_int32_t dst_ip;
	/* ports have to be consecutive !!! */
	u_int16_t src_port;
	u_int16_t dst_port;
};

struct dsthash_ent {
	/* static / read-only parts in the beginning */
	struct hlist_node node;
	struct dsthash_dst dst;

	/* modified structure members in the end */
	unsigned long expires;		/* precalculated expiry time */
	struct {
		unsigned long prev;	/* last modification */
		u_int32_t credit;
		u_int32_t credit_cap, cost;
	} rateinfo;
};

struct ipt_hashlimit_htable {
	struct hlist_node node;		/* global list of all htables */
	atomic_t use;

	struct hashlimit_cfg cfg;	/* config */

	/* used internally */
	spinlock_t lock;		/* lock for list_head */
	u_int32_t rnd;			/* random seed for hash */
	struct timer_list timer;	/* timer for gc */
	atomic_t count;			/* number entries in table */

	/* seq_file stuff */
	struct proc_dir_entry *pde;

	struct hlist_head hash[0];	/* hashtable itself */
};

static DEFINE_SPINLOCK(hashlimit_lock);	/* protects htables list */
static DECLARE_MUTEX(hlimit_mutex);	/* additional checkentry protection */
static HLIST_HEAD(hashlimit_htables);
static kmem_cache_t *hashlimit_cachep __read_mostly;

static inline int dst_cmp(const struct dsthash_ent *ent, struct dsthash_dst *b)
{
	return (ent->dst.dst_ip == b->dst_ip 
		&& ent->dst.dst_port == b->dst_port
		&& ent->dst.src_port == b->src_port
		&& ent->dst.src_ip == b->src_ip);
}

static inline u_int32_t
hash_dst(const struct ipt_hashlimit_htable *ht, const struct dsthash_dst *dst)
{
	return (jhash_3words(dst->dst_ip, (dst->dst_port<<16 | dst->src_port), 
			     dst->src_ip, ht->rnd) % ht->cfg.size);
}

static inline struct dsthash_ent *
__dsthash_find(const struct ipt_hashlimit_htable *ht, struct dsthash_dst *dst)
{
	struct dsthash_ent *ent;
	struct hlist_node *pos;
	u_int32_t hash = hash_dst(ht, dst);

	if (!hlist_empty(&ht->hash[hash]))
		hlist_for_each_entry(ent, pos, &ht->hash[hash], node) {
			if (dst_cmp(ent, dst)) {
				return ent;
			}
		}
	
	return NULL;
}

/* allocate dsthash_ent, initialize dst, put in htable and lock it */
static struct dsthash_ent *
__dsthash_alloc_init(struct ipt_hashlimit_htable *ht, struct dsthash_dst *dst)
{
	struct dsthash_ent *ent;

	/* initialize hash with random val at the time we allocate
	 * the first hashtable entry */
	if (!ht->rnd)
		get_random_bytes(&ht->rnd, 4);

	if (ht->cfg.max &&
	    atomic_read(&ht->count) >= ht->cfg.max) {
		/* FIXME: do something. question is what.. */
		if (net_ratelimit())
			printk(KERN_WARNING 
				"ipt_hashlimit: max count of %u reached\n", 
				ht->cfg.max);
		return NULL;
	}

	ent = kmem_cache_alloc(hashlimit_cachep, GFP_ATOMIC);
	if (!ent) {
		if (net_ratelimit())
			printk(KERN_ERR 
				"ipt_hashlimit: can't allocate dsthash_ent\n");
		return NULL;
	}

	atomic_inc(&ht->count);

	ent->dst.dst_ip = dst->dst_ip;
	ent->dst.dst_port = dst->dst_port;
	ent->dst.src_ip = dst->src_ip;
	ent->dst.src_port = dst->src_port;

	hlist_add_head(&ent->node, &ht->hash[hash_dst(ht, dst)]);

	return ent;
}

static inline void 
__dsthash_free(struct ipt_hashlimit_htable *ht, struct dsthash_ent *ent)
{
	hlist_del(&ent->node);
	kmem_cache_free(hashlimit_cachep, ent);
	atomic_dec(&ht->count);
}
static void htable_gc(unsigned long htlong);

static int htable_create(struct ipt_hashlimit_info *minfo)
{
	int i;
	unsigned int size;
	struct ipt_hashlimit_htable *hinfo;

	if (minfo->cfg.size)
		size = minfo->cfg.size;
	else {
		size = (((num_physpages << PAGE_SHIFT) / 16384)
			 / sizeof(struct list_head));
		if (num_physpages > (1024 * 1024 * 1024 / PAGE_SIZE))
			size = 8192;
		if (size < 16)
			size = 16;
	}
	/* FIXME: don't use vmalloc() here or anywhere else -HW */
	hinfo = vmalloc(sizeof(struct ipt_hashlimit_htable)
			+ (sizeof(struct list_head) * size));
	if (!hinfo) {
		printk(KERN_ERR "ipt_hashlimit: Unable to create hashtable\n");
		return -1;
	}
	minfo->hinfo = hinfo;

	/* copy match config into hashtable config */
	memcpy(&hinfo->cfg, &minfo->cfg, sizeof(hinfo->cfg));
	hinfo->cfg.size = size;
	if (!hinfo->cfg.max)
		hinfo->cfg.max = 8 * hinfo->cfg.size;
	else if (hinfo->cfg.max < hinfo->cfg.size)
		hinfo->cfg.max = hinfo->cfg.size;

	for (i = 0; i < hinfo->cfg.size; i++)
		INIT_HLIST_HEAD(&hinfo->hash[i]);

	atomic_set(&hinfo->count, 0);
	atomic_set(&hinfo->use, 1);
	hinfo->rnd = 0;
	spin_lock_init(&hinfo->lock);
	hinfo->pde = create_proc_entry(minfo->name, 0, hashlimit_procdir);
	if (!hinfo->pde) {
		vfree(hinfo);
		return -1;
	}
	hinfo->pde->proc_fops = &dl_file_ops;
	hinfo->pde->data = hinfo;

	init_timer(&hinfo->timer);
	hinfo->timer.expires = jiffies + msecs_to_jiffies(hinfo->cfg.gc_interval);
	hinfo->timer.data = (unsigned long )hinfo;
	hinfo->timer.function = htable_gc;
	add_timer(&hinfo->timer);

	spin_lock_bh(&hashlimit_lock);
	hlist_add_head(&hinfo->node, &hashlimit_htables);
	spin_unlock_bh(&hashlimit_lock);

	return 0;
}

static int select_all(struct ipt_hashlimit_htable *ht, struct dsthash_ent *he)
{
	return 1;
}

static int select_gc(struct ipt_hashlimit_htable *ht, struct dsthash_ent *he)
{
	return (jiffies >= he->expires);
}

static void htable_selective_cleanup(struct ipt_hashlimit_htable *ht,
		 		int (*select)(struct ipt_hashlimit_htable *ht, 
					      struct dsthash_ent *he))
{
	int i;

	IP_NF_ASSERT(ht->cfg.size && ht->cfg.max);

	/* lock hash table and iterate over it */
	spin_lock_bh(&ht->lock);
	for (i = 0; i < ht->cfg.size; i++) {
		struct dsthash_ent *dh;
		struct hlist_node *pos, *n;
		hlist_for_each_entry_safe(dh, pos, n, &ht->hash[i], node) {
			if ((*select)(ht, dh))
				__dsthash_free(ht, dh);
		}
	}
	spin_unlock_bh(&ht->lock);
}

/* hash table garbage collector, run by timer */
static void htable_gc(unsigned long htlong)
{
	struct ipt_hashlimit_htable *ht = (struct ipt_hashlimit_htable *)htlong;

	htable_selective_cleanup(ht, select_gc);

	/* re-add the timer accordingly */
	ht->timer.expires = jiffies + msecs_to_jiffies(ht->cfg.gc_interval);
	add_timer(&ht->timer);
}

static void htable_destroy(struct ipt_hashlimit_htable *hinfo)
{
	/* remove timer, if it is pending */
	if (timer_pending(&hinfo->timer))
		del_timer(&hinfo->timer);

	/* remove proc entry */
	remove_proc_entry(hinfo->pde->name, hashlimit_procdir);

	htable_selective_cleanup(hinfo, select_all);
	vfree(hinfo);
}

static struct ipt_hashlimit_htable *htable_find_get(char *name)
{
	struct ipt_hashlimit_htable *hinfo;
	struct hlist_node *pos;

	spin_lock_bh(&hashlimit_lock);
	hlist_for_each_entry(hinfo, pos, &hashlimit_htables, node) {
		if (!strcmp(name, hinfo->pde->name)) {
			atomic_inc(&hinfo->use);
			spin_unlock_bh(&hashlimit_lock);
			return hinfo;
		}
	}
	spin_unlock_bh(&hashlimit_lock);

	return NULL;
}

static void htable_put(struct ipt_hashlimit_htable *hinfo)
{
	if (atomic_dec_and_test(&hinfo->use)) {
		spin_lock_bh(&hashlimit_lock);
		hlist_del(&hinfo->node);
		spin_unlock_bh(&hashlimit_lock);
		htable_destroy(hinfo);
	}
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
		return (user / IPT_HASHLIMIT_SCALE) * HZ * CREDITS_PER_JIFFY;

	return (user * HZ * CREDITS_PER_JIFFY) / IPT_HASHLIMIT_SCALE;
}

static inline void rateinfo_recalc(struct dsthash_ent *dh, unsigned long now)
{
	dh->rateinfo.credit += (now - xchg(&dh->rateinfo.prev, now)) 
					* CREDITS_PER_JIFFY;
	if (dh->rateinfo.credit > dh->rateinfo.credit_cap)
		dh->rateinfo.credit = dh->rateinfo.credit_cap;
}

static inline int get_ports(const struct sk_buff *skb, int offset, 
			    u16 ports[2])
{
	union {
		struct tcphdr th;
		struct udphdr uh;
		sctp_sctphdr_t sctph;
	} hdr_u, *ptr_u;

	/* Must not be a fragment. */
	if (offset)
		return 1;

	/* Must be big enough to read ports (both UDP and TCP have
	   them at the start). */
	ptr_u = skb_header_pointer(skb, skb->nh.iph->ihl*4, 8, &hdr_u); 
	if (!ptr_u)
		return 1;

	switch (skb->nh.iph->protocol) {
		case IPPROTO_TCP:
			ports[0] = ptr_u->th.source;
			ports[1] = ptr_u->th.dest;
			break;
		case IPPROTO_UDP:
			ports[0] = ptr_u->uh.source;
			ports[1] = ptr_u->uh.dest;
			break;
		case IPPROTO_SCTP:
			ports[0] = ptr_u->sctph.source;
			ports[1] = ptr_u->sctph.dest;
			break;
		default:
			/* all other protocols don't supprot per-port hash
			 * buckets */
			ports[0] = ports[1] = 0;
			break;
	}

	return 0;
}


static int
hashlimit_match(const struct sk_buff *skb,
		const struct net_device *in,
		const struct net_device *out,
		const void *matchinfo,
		int offset,
		unsigned int protoff,
		int *hotdrop)
{
	struct ipt_hashlimit_info *r = 
		((struct ipt_hashlimit_info *)matchinfo)->u.master;
	struct ipt_hashlimit_htable *hinfo = r->hinfo;
	unsigned long now = jiffies;
	struct dsthash_ent *dh;
	struct dsthash_dst dst;

	/* build 'dst' according to hinfo->cfg and current packet */
	memset(&dst, 0, sizeof(dst));
	if (hinfo->cfg.mode & IPT_HASHLIMIT_HASH_DIP)
		dst.dst_ip = skb->nh.iph->daddr;
	if (hinfo->cfg.mode & IPT_HASHLIMIT_HASH_SIP)
		dst.src_ip = skb->nh.iph->saddr;
	if (hinfo->cfg.mode & IPT_HASHLIMIT_HASH_DPT
	    ||hinfo->cfg.mode & IPT_HASHLIMIT_HASH_SPT) {
		u_int16_t ports[2];
		if (get_ports(skb, offset, ports)) {
			/* We've been asked to examine this packet, and we
		 	  can't.  Hence, no choice but to drop. */
			*hotdrop = 1;
			return 0;
		}
		if (hinfo->cfg.mode & IPT_HASHLIMIT_HASH_SPT)
			dst.src_port = ports[0];
		if (hinfo->cfg.mode & IPT_HASHLIMIT_HASH_DPT)
			dst.dst_port = ports[1];
	} 

	spin_lock_bh(&hinfo->lock);
	dh = __dsthash_find(hinfo, &dst);
	if (!dh) {
		dh = __dsthash_alloc_init(hinfo, &dst);

		if (!dh) {
			/* enomem... don't match == DROP */
			if (net_ratelimit())
				printk(KERN_ERR "%s: ENOMEM\n", __FUNCTION__);
			spin_unlock_bh(&hinfo->lock);
			return 0;
		}

		dh->expires = jiffies + msecs_to_jiffies(hinfo->cfg.expire);

		dh->rateinfo.prev = jiffies;
		dh->rateinfo.credit = user2credits(hinfo->cfg.avg * 
							hinfo->cfg.burst);
		dh->rateinfo.credit_cap = user2credits(hinfo->cfg.avg * 
							hinfo->cfg.burst);
		dh->rateinfo.cost = user2credits(hinfo->cfg.avg);

		spin_unlock_bh(&hinfo->lock);
		return 1;
	}

	/* update expiration timeout */
	dh->expires = now + msecs_to_jiffies(hinfo->cfg.expire);

	rateinfo_recalc(dh, now);
	if (dh->rateinfo.credit >= dh->rateinfo.cost) {
		/* We're underlimit. */
		dh->rateinfo.credit -= dh->rateinfo.cost;
		spin_unlock_bh(&hinfo->lock);
		return 1;
	}

       	spin_unlock_bh(&hinfo->lock);

	/* default case: we're overlimit, thus don't match */
	return 0;
}

static int
hashlimit_checkentry(const char *tablename,
		     const void *inf,
		     void *matchinfo,
		     unsigned int matchsize,
		     unsigned int hook_mask)
{
	struct ipt_hashlimit_info *r = matchinfo;

	if (matchsize != IPT_ALIGN(sizeof(struct ipt_hashlimit_info)))
		return 0;

	/* Check for overflow. */
	if (r->cfg.burst == 0
	    || user2credits(r->cfg.avg * r->cfg.burst) < 
	    				user2credits(r->cfg.avg)) {
		printk(KERN_ERR "ipt_hashlimit: Overflow, try lower: %u/%u\n",
		       r->cfg.avg, r->cfg.burst);
		return 0;
	}

	if (r->cfg.mode == 0 
	    || r->cfg.mode > (IPT_HASHLIMIT_HASH_DPT
		          |IPT_HASHLIMIT_HASH_DIP
			  |IPT_HASHLIMIT_HASH_SIP
			  |IPT_HASHLIMIT_HASH_SPT))
		return 0;

	if (!r->cfg.gc_interval)
		return 0;
	
	if (!r->cfg.expire)
		return 0;

	/* This is the best we've got: We cannot release and re-grab lock,
	 * since checkentry() is called before ip_tables.c grabs ipt_mutex.  
	 * We also cannot grab the hashtable spinlock, since htable_create will 
	 * call vmalloc, and that can sleep.  And we cannot just re-search
	 * the list of htable's in htable_create(), since then we would
	 * create duplicate proc files. -HW */
	down(&hlimit_mutex);
	r->hinfo = htable_find_get(r->name);
	if (!r->hinfo && (htable_create(r) != 0)) {
		up(&hlimit_mutex);
		return 0;
	}
	up(&hlimit_mutex);

	/* Ugly hack: For SMP, we only want to use one set */
	r->u.master = r;

	return 1;
}

static void
hashlimit_destroy(void *matchinfo, unsigned int matchsize)
{
	struct ipt_hashlimit_info *r = (struct ipt_hashlimit_info *) matchinfo;

	htable_put(r->hinfo);
}

static struct ipt_match ipt_hashlimit = { 
	.name = "hashlimit", 
	.match = hashlimit_match, 
	.checkentry = hashlimit_checkentry, 
	.destroy = hashlimit_destroy,
	.me = THIS_MODULE 
};

/* PROC stuff */

static void *dl_seq_start(struct seq_file *s, loff_t *pos)
{
	struct proc_dir_entry *pde = s->private;
	struct ipt_hashlimit_htable *htable = pde->data;
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
	struct proc_dir_entry *pde = s->private;
	struct ipt_hashlimit_htable *htable = pde->data;
	unsigned int *bucket = (unsigned int *)v;

	*pos = ++(*bucket);
	if (*pos >= htable->cfg.size) {
		kfree(v);
		return NULL;
	}
	return bucket;
}

static void dl_seq_stop(struct seq_file *s, void *v)
{
	struct proc_dir_entry *pde = s->private;
	struct ipt_hashlimit_htable *htable = pde->data;
	unsigned int *bucket = (unsigned int *)v;

	kfree(bucket);

	spin_unlock_bh(&htable->lock);
}

static inline int dl_seq_real_show(struct dsthash_ent *ent, struct seq_file *s)
{
	/* recalculate to show accurate numbers */
	rateinfo_recalc(ent, jiffies);

	return seq_printf(s, "%ld %u.%u.%u.%u:%u->%u.%u.%u.%u:%u %u %u %u\n",
			(long)(ent->expires - jiffies)/HZ,
			NIPQUAD(ent->dst.src_ip), ntohs(ent->dst.src_port),
			NIPQUAD(ent->dst.dst_ip), ntohs(ent->dst.dst_port),
			ent->rateinfo.credit, ent->rateinfo.credit_cap,
			ent->rateinfo.cost);
}

static int dl_seq_show(struct seq_file *s, void *v)
{
	struct proc_dir_entry *pde = s->private;
	struct ipt_hashlimit_htable *htable = pde->data;
	unsigned int *bucket = (unsigned int *)v;
	struct dsthash_ent *ent;
	struct hlist_node *pos;

	if (!hlist_empty(&htable->hash[*bucket]))
		hlist_for_each_entry(ent, pos, &htable->hash[*bucket], node) {
			if (dl_seq_real_show(ent, s)) {
				/* buffer was filled and unable to print that tuple */
				return 1;
			}
		}
	
	return 0;
}

static struct seq_operations dl_seq_ops = {
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
		sf->private = PDE(inode);
	}
	return ret;
}

static struct file_operations dl_file_ops = {
	.owner   = THIS_MODULE,
	.open    = dl_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static int init_or_fini(int fini)
{
	int ret = 0;

	if (fini)
		goto cleanup;

	if (ipt_register_match(&ipt_hashlimit)) {
		ret = -EINVAL;
		goto cleanup_nothing;
	}

	hashlimit_cachep = kmem_cache_create("ipt_hashlimit",
					    sizeof(struct dsthash_ent), 0,
					    0, NULL, NULL);
	if (!hashlimit_cachep) {
		printk(KERN_ERR "Unable to create ipt_hashlimit slab cache\n");
		ret = -ENOMEM;
		goto cleanup_unreg_match;
	}

	hashlimit_procdir = proc_mkdir("ipt_hashlimit", proc_net);
	if (!hashlimit_procdir) {
		printk(KERN_ERR "Unable to create proc dir entry\n");
		ret = -ENOMEM;
		goto cleanup_free_slab;
	}

	return ret;

cleanup:
	remove_proc_entry("ipt_hashlimit", proc_net);
cleanup_free_slab:
	kmem_cache_destroy(hashlimit_cachep);
cleanup_unreg_match:
	ipt_unregister_match(&ipt_hashlimit);
cleanup_nothing:
	return ret;
	
}

static int __init init(void)
{
	return init_or_fini(0);
}

static void __exit fini(void)
{
	init_or_fini(1);
}

module_init(init);
module_exit(fini);
