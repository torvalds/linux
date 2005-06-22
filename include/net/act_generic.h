/*
 * include/net/act_generic.h
 *
*/
#ifndef _NET_ACT_GENERIC_H
#define _NET_ACT_GENERIC_H
static inline int tcf_defact_release(struct tcf_defact *p, int bind)
{
	int ret = 0;
	if (p) {
		if (bind) {
			p->bindcnt--;
		}
		p->refcnt--;
		if (p->bindcnt <= 0 && p->refcnt <= 0) {
			kfree(p->defdata);
			tcf_hash_destroy(p);
			ret = 1;
		}
	}
	return ret;
}

static inline int
alloc_defdata(struct tcf_defact *p, u32 datalen, void *defdata)
{
	p->defdata = kmalloc(datalen, GFP_KERNEL);
	if (p->defdata == NULL)
		return -ENOMEM;
	p->datalen = datalen;
	memcpy(p->defdata, defdata, datalen);
	return 0;
}

static inline int
realloc_defdata(struct tcf_defact *p, u32 datalen, void *defdata)
{
	/* safer to be just brute force for now */
	kfree(p->defdata);
	return alloc_defdata(p, datalen, defdata);
}

static inline int
tcf_defact_init(struct rtattr *rta, struct rtattr *est,
		struct tc_action *a, int ovr, int bind)
{
	struct rtattr *tb[TCA_DEF_MAX];
	struct tc_defact *parm;
	struct tcf_defact *p;
	void *defdata;
	u32 datalen = 0;
	int ret = 0;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_DEF_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_DEF_PARMS - 1] == NULL || 
	    RTA_PAYLOAD(tb[TCA_DEF_PARMS - 1]) < sizeof(*parm))
		return -EINVAL;

	parm = RTA_DATA(tb[TCA_DEF_PARMS - 1]);
	defdata = RTA_DATA(tb[TCA_DEF_DATA - 1]);
	if (defdata == NULL)
		return -EINVAL;

	datalen = RTA_PAYLOAD(tb[TCA_DEF_DATA - 1]);
	if (datalen <= 0)
		return -EINVAL;

	p = tcf_hash_check(parm->index, a, ovr, bind);
	if (p == NULL) {
		p = tcf_hash_create(parm->index, est, a, sizeof(*p), ovr, bind);
		if (p == NULL)
			return -ENOMEM;

		ret = alloc_defdata(p, datalen, defdata);
		if (ret < 0) {
			kfree(p);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_defact_release(p, bind);
			return -EEXIST;
		}
		realloc_defdata(p, datalen, defdata);
	}

	spin_lock_bh(&p->lock);
	p->action = parm->action;
	spin_unlock_bh(&p->lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(p);
	return ret;
}

static inline int tcf_defact_cleanup(struct tc_action *a, int bind)
{
	struct tcf_defact *p = PRIV(a, defact);

	if (p != NULL)
		return tcf_defact_release(p, bind);
	return 0;
}

static inline int
tcf_defact_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb->tail;
	struct tc_defact opt;
	struct tcf_defact *p = PRIV(a, defact);
	struct tcf_t t;

	opt.index = p->index;
	opt.refcnt = p->refcnt - ref;
	opt.bindcnt = p->bindcnt - bind;
	opt.action = p->action;
	RTA_PUT(skb, TCA_DEF_PARMS, sizeof(opt), &opt);
	RTA_PUT(skb, TCA_DEF_DATA, p->datalen, p->defdata);
	t.install = jiffies_to_clock_t(jiffies - p->tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - p->tm.lastuse);
	t.expires = jiffies_to_clock_t(p->tm.expires);
	RTA_PUT(skb, TCA_DEF_TM, sizeof(t), &t);
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

#define tca_use_default_ops \
	.dump           =       tcf_defact_dump, \
	.cleanup        =       tcf_defact_cleanup, \
	.init           =       tcf_defact_init, \
	.walk           =       tcf_generic_walker, \

#define tca_use_default_defines(name) \
	static u32 idx_gen; \
	static struct tcf_defact *tcf_##name_ht[MY_TAB_SIZE]; \
	static DEFINE_RWLOCK(##name_lock);
#endif /* _NET_ACT_GENERIC_H */
