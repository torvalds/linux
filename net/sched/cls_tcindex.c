/*
 * net/sched/cls_tcindex.c	Packet classifier for skb->tc_index
 *
 * Written 1998,1999 by Werner Almesberger, EPFL ICA
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/act_api.h>
#include <net/pkt_cls.h>
#include <net/route.h>


/*
 * Not quite sure if we need all the xchgs Alexey uses when accessing things.
 * Can always add them later ... :)
 */

/*
 * Passing parameters to the root seems to be done more awkwardly than really
 * necessary. At least, u32 doesn't seem to use such dirty hacks. To be
 * verified. FIXME.
 */

#define PERFECT_HASH_THRESHOLD	64	/* use perfect hash if not bigger */
#define DEFAULT_HASH_SIZE	64	/* optimized for diffserv */


#if 1 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#if 0 /* data */
#define D2PRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define D2PRINTK(format,args...)
#endif


#define	PRIV(tp)	((struct tcindex_data *) (tp)->root)


struct tcindex_filter_result {
	struct tcf_exts		exts;
	struct tcf_result	res;
};

struct tcindex_filter {
	u16 key;
	struct tcindex_filter_result result;
	struct tcindex_filter *next;
};


struct tcindex_data {
	struct tcindex_filter_result *perfect; /* perfect hash; NULL if none */
	struct tcindex_filter **h; /* imperfect hash; only used if !perfect;
				      NULL if unused */
	u16 mask;		/* AND key with mask */
	int shift;		/* shift ANDed key to the right */
	int hash;		/* hash table size; 0 if undefined */
	int alloc_hash;		/* allocated size */
	int fall_through;	/* 0: only classify if explicit match */
};

static struct tcf_ext_map tcindex_ext_map = {
	.police = TCA_TCINDEX_POLICE,
	.action = TCA_TCINDEX_ACT
};

static inline int
tcindex_filter_is_set(struct tcindex_filter_result *r)
{
	return tcf_exts_is_predicative(&r->exts) || r->res.classid;
}

static struct tcindex_filter_result *
tcindex_lookup(struct tcindex_data *p, u16 key)
{
	struct tcindex_filter *f;

	if (p->perfect)
		return tcindex_filter_is_set(p->perfect + key) ?
			p->perfect + key : NULL;
	else if (p->h) {
		for (f = p->h[key % p->hash]; f; f = f->next)
			if (f->key == key)
				return &f->result;
	}

	return NULL;
}


static int tcindex_classify(struct sk_buff *skb, struct tcf_proto *tp,
			    struct tcf_result *res)
{
	struct tcindex_data *p = PRIV(tp);
	struct tcindex_filter_result *f;
	int key = (skb->tc_index & p->mask) >> p->shift;

	D2PRINTK("tcindex_classify(skb %p,tp %p,res %p),p %p\n",skb,tp,res,p);

	f = tcindex_lookup(p, key);
	if (!f) {
		if (!p->fall_through)
			return -1;
		res->classid = TC_H_MAKE(TC_H_MAJ(tp->q->handle), key);
		res->class = 0;
		D2PRINTK("alg 0x%x\n",res->classid);
		return 0;
	}
	*res = f->res;
	D2PRINTK("map 0x%x\n",res->classid);

	return tcf_exts_exec(skb, &f->exts, res);
}


static unsigned long tcindex_get(struct tcf_proto *tp, u32 handle)
{
	struct tcindex_data *p = PRIV(tp);
	struct tcindex_filter_result *r;

	DPRINTK("tcindex_get(tp %p,handle 0x%08x)\n",tp,handle);
	if (p->perfect && handle >= p->alloc_hash)
		return 0;
	r = tcindex_lookup(p, handle);
	return r && tcindex_filter_is_set(r) ? (unsigned long) r : 0UL;
}


static void tcindex_put(struct tcf_proto *tp, unsigned long f)
{
	DPRINTK("tcindex_put(tp %p,f 0x%lx)\n",tp,f);
}


static int tcindex_init(struct tcf_proto *tp)
{
	struct tcindex_data *p;

	DPRINTK("tcindex_init(tp %p)\n",tp);
	p = kmalloc(sizeof(struct tcindex_data),GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	memset(p, 0, sizeof(*p));
	p->mask = 0xffff;
	p->hash = DEFAULT_HASH_SIZE;
	p->fall_through = 1;

	tp->root = p;
	return 0;
}


static int
__tcindex_delete(struct tcf_proto *tp, unsigned long arg, int lock)
{
	struct tcindex_data *p = PRIV(tp);
	struct tcindex_filter_result *r = (struct tcindex_filter_result *) arg;
	struct tcindex_filter *f = NULL;

	DPRINTK("tcindex_delete(tp %p,arg 0x%lx),p %p,f %p\n",tp,arg,p,f);
	if (p->perfect) {
		if (!r->res.class)
			return -ENOENT;
	} else {
		int i;
		struct tcindex_filter **walk = NULL;

		for (i = 0; i < p->hash; i++)
			for (walk = p->h+i; *walk; walk = &(*walk)->next)
				if (&(*walk)->result == r)
					goto found;
		return -ENOENT;

found:
		f = *walk;
		if (lock)
			tcf_tree_lock(tp);
		*walk = f->next;
		if (lock)
			tcf_tree_unlock(tp);
	}
	tcf_unbind_filter(tp, &r->res);
	tcf_exts_destroy(tp, &r->exts);
	if (f)
		kfree(f);
	return 0;
}

static int tcindex_delete(struct tcf_proto *tp, unsigned long arg)
{
	return __tcindex_delete(tp, arg, 1);
}

static inline int
valid_perfect_hash(struct tcindex_data *p)
{
	return  p->hash > (p->mask >> p->shift);
}

static int
tcindex_set_parms(struct tcf_proto *tp, unsigned long base, u32 handle,
		  struct tcindex_data *p, struct tcindex_filter_result *r,
		  struct rtattr **tb, struct rtattr *est)
{
	int err, balloc = 0;
	struct tcindex_filter_result new_filter_result, *old_r = r;
	struct tcindex_filter_result cr;
	struct tcindex_data cp;
	struct tcindex_filter *f = NULL; /* make gcc behave */
	struct tcf_exts e;

	err = tcf_exts_validate(tp, tb, est, &e, &tcindex_ext_map);
	if (err < 0)
		return err;
	
	memcpy(&cp, p, sizeof(cp));
	memset(&new_filter_result, 0, sizeof(new_filter_result));

	if (old_r)
		memcpy(&cr, r, sizeof(cr));
	else
		memset(&cr, 0, sizeof(cr));

	err = -EINVAL;
	if (tb[TCA_TCINDEX_HASH-1]) {
		if (RTA_PAYLOAD(tb[TCA_TCINDEX_HASH-1]) < sizeof(u32))
			goto errout;
		cp.hash = *(u32 *) RTA_DATA(tb[TCA_TCINDEX_HASH-1]);
	}

	if (tb[TCA_TCINDEX_MASK-1]) {
		if (RTA_PAYLOAD(tb[TCA_TCINDEX_MASK-1]) < sizeof(u16))
			goto errout;
		cp.mask = *(u16 *) RTA_DATA(tb[TCA_TCINDEX_MASK-1]);
	}

	if (tb[TCA_TCINDEX_SHIFT-1]) {
		if (RTA_PAYLOAD(tb[TCA_TCINDEX_SHIFT-1]) < sizeof(u16))
			goto errout;
		cp.shift = *(u16 *) RTA_DATA(tb[TCA_TCINDEX_SHIFT-1]);
	}

	err = -EBUSY;
	/* Hash already allocated, make sure that we still meet the
	 * requirements for the allocated hash.
	 */
	if (cp.perfect) {
		if (!valid_perfect_hash(&cp) ||
		    cp.hash > cp.alloc_hash)
			goto errout;
	} else if (cp.h && cp.hash != cp.alloc_hash)
		goto errout;

	err = -EINVAL;
	if (tb[TCA_TCINDEX_FALL_THROUGH-1]) {
		if (RTA_PAYLOAD(tb[TCA_TCINDEX_FALL_THROUGH-1]) < sizeof(u32))
			goto errout;
		cp.fall_through =
			*(u32 *) RTA_DATA(tb[TCA_TCINDEX_FALL_THROUGH-1]);
	}

	if (!cp.hash) {
		/* Hash not specified, use perfect hash if the upper limit
		 * of the hashing index is below the threshold.
		 */
		if ((cp.mask >> cp.shift) < PERFECT_HASH_THRESHOLD)
			cp.hash = (cp.mask >> cp.shift)+1;
		else
			cp.hash = DEFAULT_HASH_SIZE;
	}

	if (!cp.perfect && !cp.h)
		cp.alloc_hash = cp.hash;

	/* Note: this could be as restrictive as if (handle & ~(mask >> shift))
	 * but then, we'd fail handles that may become valid after some future
	 * mask change. While this is extremely unlikely to ever matter,
	 * the check below is safer (and also more backwards-compatible).
	 */
	if (cp.perfect || valid_perfect_hash(&cp))
		if (handle >= cp.alloc_hash)
			goto errout;


	err = -ENOMEM;
	if (!cp.perfect && !cp.h) {
		if (valid_perfect_hash(&cp)) {
			cp.perfect = kmalloc(cp.hash * sizeof(*r), GFP_KERNEL);
			if (!cp.perfect)
				goto errout;
			memset(cp.perfect, 0, cp.hash * sizeof(*r));
			balloc = 1;
		} else {
			cp.h = kmalloc(cp.hash * sizeof(f), GFP_KERNEL);
			if (!cp.h)
				goto errout;
			memset(cp.h, 0, cp.hash * sizeof(f));
			balloc = 2;
		}
	}

	if (cp.perfect)
		r = cp.perfect + handle;
	else
		r = tcindex_lookup(&cp, handle) ? : &new_filter_result;

	if (r == &new_filter_result) {
		f = kmalloc(sizeof(*f), GFP_KERNEL);
		if (!f)
			goto errout_alloc;
		memset(f, 0, sizeof(*f));
 	}

	if (tb[TCA_TCINDEX_CLASSID-1]) {
		cr.res.classid = *(u32 *) RTA_DATA(tb[TCA_TCINDEX_CLASSID-1]);
		tcf_bind_filter(tp, &cr.res, base);
 	}

	tcf_exts_change(tp, &cr.exts, &e);

	tcf_tree_lock(tp);
	if (old_r && old_r != r)
		memset(old_r, 0, sizeof(*old_r));

	memcpy(p, &cp, sizeof(cp));
	memcpy(r, &cr, sizeof(cr));

	if (r == &new_filter_result) {
		struct tcindex_filter **fp;

		f->key = handle;
		f->result = new_filter_result;
		f->next = NULL;
		for (fp = p->h+(handle % p->hash); *fp; fp = &(*fp)->next)
			/* nothing */;
		*fp = f;
 	}
	tcf_tree_unlock(tp);

	return 0;

errout_alloc:
	if (balloc == 1)
		kfree(cp.perfect);
	else if (balloc == 2)
		kfree(cp.h);
errout:
	tcf_exts_destroy(tp, &e);
	return err;
}

static int
tcindex_change(struct tcf_proto *tp, unsigned long base, u32 handle,
	       struct rtattr **tca, unsigned long *arg)
{
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_TCINDEX_MAX];
	struct tcindex_data *p = PRIV(tp);
	struct tcindex_filter_result *r = (struct tcindex_filter_result *) *arg;

	DPRINTK("tcindex_change(tp %p,handle 0x%08x,tca %p,arg %p),opt %p,"
	    "p %p,r %p,*arg 0x%lx\n",
	    tp, handle, tca, arg, opt, p, r, arg ? *arg : 0L);

	if (!opt)
		return 0;

	if (rtattr_parse_nested(tb, TCA_TCINDEX_MAX, opt) < 0)
		return -EINVAL;

	return tcindex_set_parms(tp, base, handle, p, r, tb, tca[TCA_RATE-1]);
}


static void tcindex_walk(struct tcf_proto *tp, struct tcf_walker *walker)
{
	struct tcindex_data *p = PRIV(tp);
	struct tcindex_filter *f,*next;
	int i;

	DPRINTK("tcindex_walk(tp %p,walker %p),p %p\n",tp,walker,p);
	if (p->perfect) {
		for (i = 0; i < p->hash; i++) {
			if (!p->perfect[i].res.class)
				continue;
			if (walker->count >= walker->skip) {
				if (walker->fn(tp,
				    (unsigned long) (p->perfect+i), walker)
				     < 0) {
					walker->stop = 1;
					return;
				}
			}
			walker->count++;
		}
	}
	if (!p->h)
		return;
	for (i = 0; i < p->hash; i++) {
		for (f = p->h[i]; f; f = next) {
			next = f->next;
			if (walker->count >= walker->skip) {
				if (walker->fn(tp,(unsigned long) &f->result,
				    walker) < 0) {
					walker->stop = 1;
					return;
				}
			}
			walker->count++;
		}
	}
}


static int tcindex_destroy_element(struct tcf_proto *tp,
    unsigned long arg, struct tcf_walker *walker)
{
	return __tcindex_delete(tp, arg, 0);
}


static void tcindex_destroy(struct tcf_proto *tp)
{
	struct tcindex_data *p = PRIV(tp);
	struct tcf_walker walker;

	DPRINTK("tcindex_destroy(tp %p),p %p\n",tp,p);
	walker.count = 0;
	walker.skip = 0;
	walker.fn = &tcindex_destroy_element;
	tcindex_walk(tp,&walker);
	if (p->perfect)
		kfree(p->perfect);
	if (p->h)
		kfree(p->h);
	kfree(p);
	tp->root = NULL;
}


static int tcindex_dump(struct tcf_proto *tp, unsigned long fh,
    struct sk_buff *skb, struct tcmsg *t)
{
	struct tcindex_data *p = PRIV(tp);
	struct tcindex_filter_result *r = (struct tcindex_filter_result *) fh;
	unsigned char *b = skb->tail;
	struct rtattr *rta;

	DPRINTK("tcindex_dump(tp %p,fh 0x%lx,skb %p,t %p),p %p,r %p,b %p\n",
	    tp,fh,skb,t,p,r,b);
	DPRINTK("p->perfect %p p->h %p\n",p->perfect,p->h);
	rta = (struct rtattr *) b;
	RTA_PUT(skb,TCA_OPTIONS,0,NULL);
	if (!fh) {
		t->tcm_handle = ~0; /* whatever ... */
		RTA_PUT(skb,TCA_TCINDEX_HASH,sizeof(p->hash),&p->hash);
		RTA_PUT(skb,TCA_TCINDEX_MASK,sizeof(p->mask),&p->mask);
		RTA_PUT(skb,TCA_TCINDEX_SHIFT,sizeof(p->shift),&p->shift);
		RTA_PUT(skb,TCA_TCINDEX_FALL_THROUGH,sizeof(p->fall_through),
		    &p->fall_through);
		rta->rta_len = skb->tail-b;
	} else {
		if (p->perfect) {
			t->tcm_handle = r-p->perfect;
		} else {
			struct tcindex_filter *f;
			int i;

			t->tcm_handle = 0;
			for (i = 0; !t->tcm_handle && i < p->hash; i++) {
				for (f = p->h[i]; !t->tcm_handle && f;
				     f = f->next) {
					if (&f->result == r)
						t->tcm_handle = f->key;
				}
			}
		}
		DPRINTK("handle = %d\n",t->tcm_handle);
		if (r->res.class)
			RTA_PUT(skb, TCA_TCINDEX_CLASSID, 4, &r->res.classid);

		if (tcf_exts_dump(skb, &r->exts, &tcindex_ext_map) < 0)
			goto rtattr_failure;
		rta->rta_len = skb->tail-b;

		if (tcf_exts_dump_stats(skb, &r->exts, &tcindex_ext_map) < 0)
			goto rtattr_failure;
	}
	
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tcf_proto_ops cls_tcindex_ops = {
	.next		=	NULL,
	.kind		=	"tcindex",
	.classify	=	tcindex_classify,
	.init		=	tcindex_init,
	.destroy	=	tcindex_destroy,
	.get		=	tcindex_get,
	.put		=	tcindex_put,
	.change		=	tcindex_change,
	.delete		=	tcindex_delete,
	.walk		=	tcindex_walk,
	.dump		=	tcindex_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_tcindex(void)
{
	return register_tcf_proto_ops(&cls_tcindex_ops);
}

static void __exit exit_tcindex(void) 
{
	unregister_tcf_proto_ops(&cls_tcindex_ops);
}

module_init(init_tcindex)
module_exit(exit_tcindex)
MODULE_LICENSE("GPL");
