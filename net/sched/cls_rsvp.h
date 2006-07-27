/*
 * net/sched/cls_rsvp.h	Template file for RSVPv[46] classifiers.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

/*
   Comparing to general packet classification problem,
   RSVP needs only sevaral relatively simple rules:

   * (dst, protocol) are always specified,
     so that we are able to hash them.
   * src may be exact, or may be wildcard, so that
     we can keep a hash table plus one wildcard entry.
   * source port (or flow label) is important only if src is given.

   IMPLEMENTATION.

   We use a two level hash table: The top level is keyed by
   destination address and protocol ID, every bucket contains a list
   of "rsvp sessions", identified by destination address, protocol and
   DPI(="Destination Port ID"): triple (key, mask, offset).

   Every bucket has a smaller hash table keyed by source address
   (cf. RSVP flowspec) and one wildcard entry for wildcard reservations.
   Every bucket is again a list of "RSVP flows", selected by
   source address and SPI(="Source Port ID" here rather than
   "security parameter index"): triple (key, mask, offset).


   NOTE 1. All the packets with IPv6 extension headers (but AH and ESP)
   and all fragmented packets go to the best-effort traffic class.


   NOTE 2. Two "port id"'s seems to be redundant, rfc2207 requires
   only one "Generalized Port Identifier". So that for classic
   ah, esp (and udp,tcp) both *pi should coincide or one of them
   should be wildcard.

   At first sight, this redundancy is just a waste of CPU
   resources. But DPI and SPI add the possibility to assign different
   priorities to GPIs. Look also at note 4 about tunnels below.


   NOTE 3. One complication is the case of tunneled packets.
   We implement it as following: if the first lookup
   matches a special session with "tunnelhdr" value not zero,
   flowid doesn't contain the true flow ID, but the tunnel ID (1...255).
   In this case, we pull tunnelhdr bytes and restart lookup
   with tunnel ID added to the list of keys. Simple and stupid 8)8)
   It's enough for PIMREG and IPIP.


   NOTE 4. Two GPIs make it possible to parse even GRE packets.
   F.e. DPI can select ETH_P_IP (and necessary flags to make
   tunnelhdr correct) in GRE protocol field and SPI matches
   GRE key. Is it not nice? 8)8)


   Well, as result, despite its simplicity, we get a pretty
   powerful classification engine.  */


struct rsvp_head
{
	u32			tmap[256/32];
	u32			hgenerator;
	u8			tgenerator;
	struct rsvp_session	*ht[256];
};

struct rsvp_session
{
	struct rsvp_session	*next;
	u32			dst[RSVP_DST_LEN];
	struct tc_rsvp_gpi 	dpi;
	u8			protocol;
	u8			tunnelid;
	/* 16 (src,sport) hash slots, and one wildcard source slot */
	struct rsvp_filter	*ht[16+1];
};


struct rsvp_filter
{
	struct rsvp_filter	*next;
	u32			src[RSVP_DST_LEN];
	struct tc_rsvp_gpi	spi;
	u8			tunnelhdr;

	struct tcf_result	res;
	struct tcf_exts		exts;

	u32			handle;
	struct rsvp_session	*sess;
};

static __inline__ unsigned hash_dst(u32 *dst, u8 protocol, u8 tunnelid)
{
	unsigned h = dst[RSVP_DST_LEN-1];
	h ^= h>>16;
	h ^= h>>8;
	return (h ^ protocol ^ tunnelid) & 0xFF;
}

static __inline__ unsigned hash_src(u32 *src)
{
	unsigned h = src[RSVP_DST_LEN-1];
	h ^= h>>16;
	h ^= h>>8;
	h ^= h>>4;
	return h & 0xF;
}

static struct tcf_ext_map rsvp_ext_map = {
	.police = TCA_RSVP_POLICE,
	.action = TCA_RSVP_ACT
};

#define RSVP_APPLY_RESULT()				\
{							\
	int r = tcf_exts_exec(skb, &f->exts, res);	\
	if (r < 0)					\
		continue;				\
	else if (r > 0)					\
		return r;				\
}
	
static int rsvp_classify(struct sk_buff *skb, struct tcf_proto *tp,
			 struct tcf_result *res)
{
	struct rsvp_session **sht = ((struct rsvp_head*)tp->root)->ht;
	struct rsvp_session *s;
	struct rsvp_filter *f;
	unsigned h1, h2;
	u32 *dst, *src;
	u8 protocol;
	u8 tunnelid = 0;
	u8 *xprt;
#if RSVP_DST_LEN == 4
	struct ipv6hdr *nhptr = skb->nh.ipv6h;
#else
	struct iphdr *nhptr = skb->nh.iph;
#endif

restart:

#if RSVP_DST_LEN == 4
	src = &nhptr->saddr.s6_addr32[0];
	dst = &nhptr->daddr.s6_addr32[0];
	protocol = nhptr->nexthdr;
	xprt = ((u8*)nhptr) + sizeof(struct ipv6hdr);
#else
	src = &nhptr->saddr;
	dst = &nhptr->daddr;
	protocol = nhptr->protocol;
	xprt = ((u8*)nhptr) + (nhptr->ihl<<2);
	if (nhptr->frag_off&__constant_htons(IP_MF|IP_OFFSET))
		return -1;
#endif

	h1 = hash_dst(dst, protocol, tunnelid);
	h2 = hash_src(src);

	for (s = sht[h1]; s; s = s->next) {
		if (dst[RSVP_DST_LEN-1] == s->dst[RSVP_DST_LEN-1] &&
		    protocol == s->protocol &&
		    !(s->dpi.mask & (*(u32*)(xprt+s->dpi.offset)^s->dpi.key))
#if RSVP_DST_LEN == 4
		    && dst[0] == s->dst[0]
		    && dst[1] == s->dst[1]
		    && dst[2] == s->dst[2]
#endif
		    && tunnelid == s->tunnelid) {

			for (f = s->ht[h2]; f; f = f->next) {
				if (src[RSVP_DST_LEN-1] == f->src[RSVP_DST_LEN-1] &&
				    !(f->spi.mask & (*(u32*)(xprt+f->spi.offset)^f->spi.key))
#if RSVP_DST_LEN == 4
				    && src[0] == f->src[0]
				    && src[1] == f->src[1]
				    && src[2] == f->src[2]
#endif
				    ) {
					*res = f->res;
					RSVP_APPLY_RESULT();

matched:
					if (f->tunnelhdr == 0)
						return 0;

					tunnelid = f->res.classid;
					nhptr = (void*)(xprt + f->tunnelhdr - sizeof(*nhptr));
					goto restart;
				}
			}

			/* And wildcard bucket... */
			for (f = s->ht[16]; f; f = f->next) {
				*res = f->res;
				RSVP_APPLY_RESULT();
				goto matched;
			}
			return -1;
		}
	}
	return -1;
}

static unsigned long rsvp_get(struct tcf_proto *tp, u32 handle)
{
	struct rsvp_session **sht = ((struct rsvp_head*)tp->root)->ht;
	struct rsvp_session *s;
	struct rsvp_filter *f;
	unsigned h1 = handle&0xFF;
	unsigned h2 = (handle>>8)&0xFF;

	if (h2 > 16)
		return 0;

	for (s = sht[h1]; s; s = s->next) {
		for (f = s->ht[h2]; f; f = f->next) {
			if (f->handle == handle)
				return (unsigned long)f;
		}
	}
	return 0;
}

static void rsvp_put(struct tcf_proto *tp, unsigned long f)
{
}

static int rsvp_init(struct tcf_proto *tp)
{
	struct rsvp_head *data;

	data = kzalloc(sizeof(struct rsvp_head), GFP_KERNEL);
	if (data) {
		tp->root = data;
		return 0;
	}
	return -ENOBUFS;
}

static inline void
rsvp_delete_filter(struct tcf_proto *tp, struct rsvp_filter *f)
{
	tcf_unbind_filter(tp, &f->res);
	tcf_exts_destroy(tp, &f->exts);
	kfree(f);
}

static void rsvp_destroy(struct tcf_proto *tp)
{
	struct rsvp_head *data = xchg(&tp->root, NULL);
	struct rsvp_session **sht;
	int h1, h2;

	if (data == NULL)
		return;

	sht = data->ht;

	for (h1=0; h1<256; h1++) {
		struct rsvp_session *s;

		while ((s = sht[h1]) != NULL) {
			sht[h1] = s->next;

			for (h2=0; h2<=16; h2++) {
				struct rsvp_filter *f;

				while ((f = s->ht[h2]) != NULL) {
					s->ht[h2] = f->next;
					rsvp_delete_filter(tp, f);
				}
			}
			kfree(s);
		}
	}
	kfree(data);
}

static int rsvp_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct rsvp_filter **fp, *f = (struct rsvp_filter*)arg;
	unsigned h = f->handle;
	struct rsvp_session **sp;
	struct rsvp_session *s = f->sess;
	int i;

	for (fp = &s->ht[(h>>8)&0xFF]; *fp; fp = &(*fp)->next) {
		if (*fp == f) {
			tcf_tree_lock(tp);
			*fp = f->next;
			tcf_tree_unlock(tp);
			rsvp_delete_filter(tp, f);

			/* Strip tree */

			for (i=0; i<=16; i++)
				if (s->ht[i])
					return 0;

			/* OK, session has no flows */
			for (sp = &((struct rsvp_head*)tp->root)->ht[h&0xFF];
			     *sp; sp = &(*sp)->next) {
				if (*sp == s) {
					tcf_tree_lock(tp);
					*sp = s->next;
					tcf_tree_unlock(tp);

					kfree(s);
					return 0;
				}
			}

			return 0;
		}
	}
	return 0;
}

static unsigned gen_handle(struct tcf_proto *tp, unsigned salt)
{
	struct rsvp_head *data = tp->root;
	int i = 0xFFFF;

	while (i-- > 0) {
		u32 h;
		if ((data->hgenerator += 0x10000) == 0)
			data->hgenerator = 0x10000;
		h = data->hgenerator|salt;
		if (rsvp_get(tp, h) == 0)
			return h;
	}
	return 0;
}

static int tunnel_bts(struct rsvp_head *data)
{
	int n = data->tgenerator>>5;
	u32 b = 1<<(data->tgenerator&0x1F);
	
	if (data->tmap[n]&b)
		return 0;
	data->tmap[n] |= b;
	return 1;
}

static void tunnel_recycle(struct rsvp_head *data)
{
	struct rsvp_session **sht = data->ht;
	u32 tmap[256/32];
	int h1, h2;

	memset(tmap, 0, sizeof(tmap));

	for (h1=0; h1<256; h1++) {
		struct rsvp_session *s;
		for (s = sht[h1]; s; s = s->next) {
			for (h2=0; h2<=16; h2++) {
				struct rsvp_filter *f;

				for (f = s->ht[h2]; f; f = f->next) {
					if (f->tunnelhdr == 0)
						continue;
					data->tgenerator = f->res.classid;
					tunnel_bts(data);
				}
			}
		}
	}

	memcpy(data->tmap, tmap, sizeof(tmap));
}

static u32 gen_tunnel(struct rsvp_head *data)
{
	int i, k;

	for (k=0; k<2; k++) {
		for (i=255; i>0; i--) {
			if (++data->tgenerator == 0)
				data->tgenerator = 1;
			if (tunnel_bts(data))
				return data->tgenerator;
		}
		tunnel_recycle(data);
	}
	return 0;
}

static int rsvp_change(struct tcf_proto *tp, unsigned long base,
		       u32 handle,
		       struct rtattr **tca,
		       unsigned long *arg)
{
	struct rsvp_head *data = tp->root;
	struct rsvp_filter *f, **fp;
	struct rsvp_session *s, **sp;
	struct tc_rsvp_pinfo *pinfo = NULL;
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_RSVP_MAX];
	struct tcf_exts e;
	unsigned h1, h2;
	u32 *dst;
	int err;

	if (opt == NULL)
		return handle ? -EINVAL : 0;

	if (rtattr_parse_nested(tb, TCA_RSVP_MAX, opt) < 0)
		return -EINVAL;

	err = tcf_exts_validate(tp, tb, tca[TCA_RATE-1], &e, &rsvp_ext_map);
	if (err < 0)
		return err;

	if ((f = (struct rsvp_filter*)*arg) != NULL) {
		/* Node exists: adjust only classid */

		if (f->handle != handle && handle)
			goto errout2;
		if (tb[TCA_RSVP_CLASSID-1]) {
			f->res.classid = *(u32*)RTA_DATA(tb[TCA_RSVP_CLASSID-1]);
			tcf_bind_filter(tp, &f->res, base);
		}

		tcf_exts_change(tp, &f->exts, &e);
		return 0;
	}

	/* Now more serious part... */
	err = -EINVAL;
	if (handle)
		goto errout2;
	if (tb[TCA_RSVP_DST-1] == NULL)
		goto errout2;

	err = -ENOBUFS;
	f = kzalloc(sizeof(struct rsvp_filter), GFP_KERNEL);
	if (f == NULL)
		goto errout2;

	h2 = 16;
	if (tb[TCA_RSVP_SRC-1]) {
		err = -EINVAL;
		if (RTA_PAYLOAD(tb[TCA_RSVP_SRC-1]) != sizeof(f->src))
			goto errout;
		memcpy(f->src, RTA_DATA(tb[TCA_RSVP_SRC-1]), sizeof(f->src));
		h2 = hash_src(f->src);
	}
	if (tb[TCA_RSVP_PINFO-1]) {
		err = -EINVAL;
		if (RTA_PAYLOAD(tb[TCA_RSVP_PINFO-1]) < sizeof(struct tc_rsvp_pinfo))
			goto errout;
		pinfo = RTA_DATA(tb[TCA_RSVP_PINFO-1]);
		f->spi = pinfo->spi;
		f->tunnelhdr = pinfo->tunnelhdr;
	}
	if (tb[TCA_RSVP_CLASSID-1]) {
		err = -EINVAL;
		if (RTA_PAYLOAD(tb[TCA_RSVP_CLASSID-1]) != 4)
			goto errout;
		f->res.classid = *(u32*)RTA_DATA(tb[TCA_RSVP_CLASSID-1]);
	}

	err = -EINVAL;
	if (RTA_PAYLOAD(tb[TCA_RSVP_DST-1]) != sizeof(f->src))
		goto errout;
	dst = RTA_DATA(tb[TCA_RSVP_DST-1]);
	h1 = hash_dst(dst, pinfo ? pinfo->protocol : 0, pinfo ? pinfo->tunnelid : 0);

	err = -ENOMEM;
	if ((f->handle = gen_handle(tp, h1 | (h2<<8))) == 0)
		goto errout;

	if (f->tunnelhdr) {
		err = -EINVAL;
		if (f->res.classid > 255)
			goto errout;

		err = -ENOMEM;
		if (f->res.classid == 0 &&
		    (f->res.classid = gen_tunnel(data)) == 0)
			goto errout;
	}

	for (sp = &data->ht[h1]; (s=*sp) != NULL; sp = &s->next) {
		if (dst[RSVP_DST_LEN-1] == s->dst[RSVP_DST_LEN-1] &&
		    pinfo && pinfo->protocol == s->protocol &&
		    memcmp(&pinfo->dpi, &s->dpi, sizeof(s->dpi)) == 0
#if RSVP_DST_LEN == 4
		    && dst[0] == s->dst[0]
		    && dst[1] == s->dst[1]
		    && dst[2] == s->dst[2]
#endif
		    && pinfo->tunnelid == s->tunnelid) {

insert:
			/* OK, we found appropriate session */

			fp = &s->ht[h2];

			f->sess = s;
			if (f->tunnelhdr == 0)
				tcf_bind_filter(tp, &f->res, base);

			tcf_exts_change(tp, &f->exts, &e);

			for (fp = &s->ht[h2]; *fp; fp = &(*fp)->next)
				if (((*fp)->spi.mask&f->spi.mask) != f->spi.mask)
					break;
			f->next = *fp;
			wmb();
			*fp = f;

			*arg = (unsigned long)f;
			return 0;
		}
	}

	/* No session found. Create new one. */

	err = -ENOBUFS;
	s = kzalloc(sizeof(struct rsvp_session), GFP_KERNEL);
	if (s == NULL)
		goto errout;
	memcpy(s->dst, dst, sizeof(s->dst));

	if (pinfo) {
		s->dpi = pinfo->dpi;
		s->protocol = pinfo->protocol;
		s->tunnelid = pinfo->tunnelid;
	}
	for (sp = &data->ht[h1]; *sp; sp = &(*sp)->next) {
		if (((*sp)->dpi.mask&s->dpi.mask) != s->dpi.mask)
			break;
	}
	s->next = *sp;
	wmb();
	*sp = s;
	
	goto insert;

errout:
	kfree(f);
errout2:
	tcf_exts_destroy(tp, &e);
	return err;
}

static void rsvp_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct rsvp_head *head = tp->root;
	unsigned h, h1;

	if (arg->stop)
		return;

	for (h = 0; h < 256; h++) {
		struct rsvp_session *s;

		for (s = head->ht[h]; s; s = s->next) {
			for (h1 = 0; h1 <= 16; h1++) {
				struct rsvp_filter *f;

				for (f = s->ht[h1]; f; f = f->next) {
					if (arg->count < arg->skip) {
						arg->count++;
						continue;
					}
					if (arg->fn(tp, (unsigned long)f, arg) < 0) {
						arg->stop = 1;
						return;
					}
					arg->count++;
				}
			}
		}
	}
}

static int rsvp_dump(struct tcf_proto *tp, unsigned long fh,
		     struct sk_buff *skb, struct tcmsg *t)
{
	struct rsvp_filter *f = (struct rsvp_filter*)fh;
	struct rsvp_session *s;
	unsigned char	 *b = skb->tail;
	struct rtattr *rta;
	struct tc_rsvp_pinfo pinfo;

	if (f == NULL)
		return skb->len;
	s = f->sess;

	t->tcm_handle = f->handle;


	rta = (struct rtattr*)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);

	RTA_PUT(skb, TCA_RSVP_DST, sizeof(s->dst), &s->dst);
	pinfo.dpi = s->dpi;
	pinfo.spi = f->spi;
	pinfo.protocol = s->protocol;
	pinfo.tunnelid = s->tunnelid;
	pinfo.tunnelhdr = f->tunnelhdr;
	pinfo.pad = 0;
	RTA_PUT(skb, TCA_RSVP_PINFO, sizeof(pinfo), &pinfo);
	if (f->res.classid)
		RTA_PUT(skb, TCA_RSVP_CLASSID, 4, &f->res.classid);
	if (((f->handle>>8)&0xFF) != 16)
		RTA_PUT(skb, TCA_RSVP_SRC, sizeof(f->src), f->src);

	if (tcf_exts_dump(skb, &f->exts, &rsvp_ext_map) < 0)
		goto rtattr_failure;

	rta->rta_len = skb->tail - b;

	if (tcf_exts_dump_stats(skb, &f->exts, &rsvp_ext_map) < 0)
		goto rtattr_failure;
	return skb->len;

rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tcf_proto_ops RSVP_OPS = {
	.next		=	NULL,
	.kind		=	RSVP_ID,
	.classify	=	rsvp_classify,
	.init		=	rsvp_init,
	.destroy	=	rsvp_destroy,
	.get		=	rsvp_get,
	.put		=	rsvp_put,
	.change		=	rsvp_change,
	.delete		=	rsvp_delete,
	.walk		=	rsvp_walk,
	.dump		=	rsvp_dump,
	.owner		=	THIS_MODULE,
};

static int __init init_rsvp(void)
{
	return register_tcf_proto_ops(&RSVP_OPS);
}

static void __exit exit_rsvp(void) 
{
	unregister_tcf_proto_ops(&RSVP_OPS);
}

module_init(init_rsvp)
module_exit(exit_rsvp)
