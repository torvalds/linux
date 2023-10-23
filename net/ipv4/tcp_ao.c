// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP Authentication Option (TCP-AO).
 *		See RFC5925.
 *
 * Authors:	Dmitry Safonov <dima@arista.com>
 *		Francesco Ruggeri <fruggeri@arista.com>
 *		Salam Noureddine <noureddine@arista.com>
 */
#define pr_fmt(fmt) "TCP: " fmt

#include <crypto/hash.h>
#include <linux/inetdevice.h>
#include <linux/tcp.h>

#include <net/tcp.h>
#include <net/ipv6.h>

int tcp_ao_calc_traffic_key(struct tcp_ao_key *mkt, u8 *key, void *ctx,
			    unsigned int len, struct tcp_sigpool *hp)
{
	struct scatterlist sg;
	int ret;

	if (crypto_ahash_setkey(crypto_ahash_reqtfm(hp->req),
				mkt->key, mkt->keylen))
		goto clear_hash;

	ret = crypto_ahash_init(hp->req);
	if (ret)
		goto clear_hash;

	sg_init_one(&sg, ctx, len);
	ahash_request_set_crypt(hp->req, &sg, key, len);
	crypto_ahash_update(hp->req);

	ret = crypto_ahash_final(hp->req);
	if (ret)
		goto clear_hash;

	return 0;
clear_hash:
	memset(key, 0, tcp_ao_digest_size(mkt));
	return 1;
}

/* Optimized version of tcp_ao_do_lookup(): only for sockets for which
 * it's known that the keys in ao_info are matching peer's
 * family/address/VRF/etc.
 */
struct tcp_ao_key *tcp_ao_established_key(struct tcp_ao_info *ao,
					  int sndid, int rcvid)
{
	struct tcp_ao_key *key;

	hlist_for_each_entry_rcu(key, &ao->head, node) {
		if ((sndid >= 0 && key->sndid != sndid) ||
		    (rcvid >= 0 && key->rcvid != rcvid))
			continue;
		return key;
	}

	return NULL;
}

static int ipv4_prefix_cmp(const struct in_addr *addr1,
			   const struct in_addr *addr2,
			   unsigned int prefixlen)
{
	__be32 mask = inet_make_mask(prefixlen);
	__be32 a1 = addr1->s_addr & mask;
	__be32 a2 = addr2->s_addr & mask;

	if (a1 == a2)
		return 0;
	return memcmp(&a1, &a2, sizeof(a1));
}

static int __tcp_ao_key_cmp(const struct tcp_ao_key *key,
			    const union tcp_ao_addr *addr, u8 prefixlen,
			    int family, int sndid, int rcvid)
{
	if (sndid >= 0 && key->sndid != sndid)
		return (key->sndid > sndid) ? 1 : -1;
	if (rcvid >= 0 && key->rcvid != rcvid)
		return (key->rcvid > rcvid) ? 1 : -1;

	if (family == AF_UNSPEC)
		return 0;
	if (key->family != family)
		return (key->family > family) ? 1 : -1;

	if (family == AF_INET) {
		if (ntohl(key->addr.a4.s_addr) == INADDR_ANY)
			return 0;
		if (ntohl(addr->a4.s_addr) == INADDR_ANY)
			return 0;
		return ipv4_prefix_cmp(&key->addr.a4, &addr->a4, prefixlen);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (ipv6_addr_any(&key->addr.a6) || ipv6_addr_any(&addr->a6))
			return 0;
		if (ipv6_prefix_equal(&key->addr.a6, &addr->a6, prefixlen))
			return 0;
		return memcmp(&key->addr.a6, &addr->a6, sizeof(addr->a6));
#endif
	}
	return -1;
}

static int tcp_ao_key_cmp(const struct tcp_ao_key *key,
			  const union tcp_ao_addr *addr, u8 prefixlen,
			  int family, int sndid, int rcvid)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (family == AF_INET6 && ipv6_addr_v4mapped(&addr->a6)) {
		__be32 addr4 = addr->a6.s6_addr32[3];

		return __tcp_ao_key_cmp(key, (union tcp_ao_addr *)&addr4,
					prefixlen, AF_INET, sndid, rcvid);
	}
#endif
	return __tcp_ao_key_cmp(key, addr, prefixlen, family, sndid, rcvid);
}

static struct tcp_ao_key *__tcp_ao_do_lookup(const struct sock *sk,
		const union tcp_ao_addr *addr, int family, u8 prefix,
		int sndid, int rcvid)
{
	struct tcp_ao_key *key;
	struct tcp_ao_info *ao;

	ao = rcu_dereference_check(tcp_sk(sk)->ao_info,
				   lockdep_sock_is_held(sk));
	if (!ao)
		return NULL;

	hlist_for_each_entry_rcu(key, &ao->head, node) {
		u8 prefixlen = min(prefix, key->prefixlen);

		if (!tcp_ao_key_cmp(key, addr, prefixlen, family, sndid, rcvid))
			return key;
	}
	return NULL;
}

struct tcp_ao_key *tcp_ao_do_lookup(const struct sock *sk,
				    const union tcp_ao_addr *addr,
				    int family, int sndid, int rcvid)
{
	return __tcp_ao_do_lookup(sk, addr, family, U8_MAX, sndid, rcvid);
}

static struct tcp_ao_info *tcp_ao_alloc_info(gfp_t flags)
{
	struct tcp_ao_info *ao;

	ao = kzalloc(sizeof(*ao), flags);
	if (!ao)
		return NULL;
	INIT_HLIST_HEAD(&ao->head);
	refcount_set(&ao->refcnt, 1);

	return ao;
}

static void tcp_ao_link_mkt(struct tcp_ao_info *ao, struct tcp_ao_key *mkt)
{
	hlist_add_head_rcu(&mkt->node, &ao->head);
}

static struct tcp_ao_key *tcp_ao_copy_key(struct sock *sk,
					  struct tcp_ao_key *key)
{
	struct tcp_ao_key *new_key;

	new_key = sock_kmalloc(sk, tcp_ao_sizeof_key(key),
			       GFP_ATOMIC);
	if (!new_key)
		return NULL;

	*new_key = *key;
	INIT_HLIST_NODE(&new_key->node);
	tcp_sigpool_get(new_key->tcp_sigpool_id);

	return new_key;
}

static void tcp_ao_key_free_rcu(struct rcu_head *head)
{
	struct tcp_ao_key *key = container_of(head, struct tcp_ao_key, rcu);

	tcp_sigpool_release(key->tcp_sigpool_id);
	kfree_sensitive(key);
}

void tcp_ao_destroy_sock(struct sock *sk, bool twsk)
{
	struct tcp_ao_info *ao;
	struct tcp_ao_key *key;
	struct hlist_node *n;

	if (twsk) {
		ao = rcu_dereference_protected(tcp_twsk(sk)->ao_info, 1);
		tcp_twsk(sk)->ao_info = NULL;
	} else {
		ao = rcu_dereference_protected(tcp_sk(sk)->ao_info, 1);
		tcp_sk(sk)->ao_info = NULL;
	}

	if (!ao || !refcount_dec_and_test(&ao->refcnt))
		return;

	hlist_for_each_entry_safe(key, n, &ao->head, node) {
		hlist_del_rcu(&key->node);
		if (!twsk)
			atomic_sub(tcp_ao_sizeof_key(key), &sk->sk_omem_alloc);
		call_rcu(&key->rcu, tcp_ao_key_free_rcu);
	}

	kfree_rcu(ao, rcu);
}

void tcp_ao_time_wait(struct tcp_timewait_sock *tcptw, struct tcp_sock *tp)
{
	struct tcp_ao_info *ao_info = rcu_dereference_protected(tp->ao_info, 1);

	if (ao_info) {
		struct tcp_ao_key *key;
		struct hlist_node *n;
		int omem = 0;

		hlist_for_each_entry_safe(key, n, &ao_info->head, node) {
			omem += tcp_ao_sizeof_key(key);
		}

		refcount_inc(&ao_info->refcnt);
		atomic_sub(omem, &(((struct sock *)tp)->sk_omem_alloc));
		rcu_assign_pointer(tcptw->ao_info, ao_info);
	} else {
		tcptw->ao_info = NULL;
	}
}

/* 4 tuple and ISNs are expected in NBO */
static int tcp_v4_ao_calc_key(struct tcp_ao_key *mkt, u8 *key,
			      __be32 saddr, __be32 daddr,
			      __be16 sport, __be16 dport,
			      __be32 sisn,  __be32 disn)
{
	/* See RFC5926 3.1.1 */
	struct kdf_input_block {
		u8                      counter;
		u8                      label[6];
		struct tcp4_ao_context	ctx;
		__be16                  outlen;
	} __packed * tmp;
	struct tcp_sigpool hp;
	int err;

	err = tcp_sigpool_start(mkt->tcp_sigpool_id, &hp);
	if (err)
		return err;

	tmp = hp.scratch;
	tmp->counter	= 1;
	memcpy(tmp->label, "TCP-AO", 6);
	tmp->ctx.saddr	= saddr;
	tmp->ctx.daddr	= daddr;
	tmp->ctx.sport	= sport;
	tmp->ctx.dport	= dport;
	tmp->ctx.sisn	= sisn;
	tmp->ctx.disn	= disn;
	tmp->outlen	= htons(tcp_ao_digest_size(mkt) * 8); /* in bits */

	err = tcp_ao_calc_traffic_key(mkt, key, tmp, sizeof(*tmp), &hp);
	tcp_sigpool_end(&hp);

	return err;
}

int tcp_v4_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			  const struct sock *sk,
			  __be32 sisn, __be32 disn, bool send)
{
	if (send)
		return tcp_v4_ao_calc_key(mkt, key, sk->sk_rcv_saddr,
					  sk->sk_daddr, htons(sk->sk_num),
					  sk->sk_dport, sisn, disn);
	else
		return tcp_v4_ao_calc_key(mkt, key, sk->sk_daddr,
					  sk->sk_rcv_saddr, sk->sk_dport,
					  htons(sk->sk_num), disn, sisn);
}

static int tcp_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			      const struct sock *sk,
			      __be32 sisn, __be32 disn, bool send)
{
	if (mkt->family == AF_INET)
		return tcp_v4_ao_calc_key_sk(mkt, key, sk, sisn, disn, send);
#if IS_ENABLED(CONFIG_IPV6)
	else if (mkt->family == AF_INET6)
		return tcp_v6_ao_calc_key_sk(mkt, key, sk, sisn, disn, send);
#endif
	else
		return -EOPNOTSUPP;
}

int tcp_v4_ao_calc_key_rsk(struct tcp_ao_key *mkt, u8 *key,
			   struct request_sock *req)
{
	struct inet_request_sock *ireq = inet_rsk(req);

	return tcp_v4_ao_calc_key(mkt, key,
				  ireq->ir_loc_addr, ireq->ir_rmt_addr,
				  htons(ireq->ir_num), ireq->ir_rmt_port,
				  htonl(tcp_rsk(req)->snt_isn),
				  htonl(tcp_rsk(req)->rcv_isn));
}

static int tcp_v4_ao_calc_key_skb(struct tcp_ao_key *mkt, u8 *key,
				  const struct sk_buff *skb,
				  __be32 sisn, __be32 disn)
{
	const struct iphdr *iph = ip_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);

	return tcp_v4_ao_calc_key(mkt, key, iph->saddr, iph->daddr,
				  th->source, th->dest, sisn, disn);
}

static int tcp_ao_calc_key_skb(struct tcp_ao_key *mkt, u8 *key,
			       const struct sk_buff *skb,
			       __be32 sisn, __be32 disn, int family)
{
	if (family == AF_INET)
		return tcp_v4_ao_calc_key_skb(mkt, key, skb, sisn, disn);
#if IS_ENABLED(CONFIG_IPV6)
	else if (family == AF_INET6)
		return tcp_v6_ao_calc_key_skb(mkt, key, skb, sisn, disn);
#endif
	return -EAFNOSUPPORT;
}

static int tcp_v4_ao_hash_pseudoheader(struct tcp_sigpool *hp,
				       __be32 daddr, __be32 saddr,
				       int nbytes)
{
	struct tcp4_pseudohdr *bp;
	struct scatterlist sg;

	bp = hp->scratch;
	bp->saddr = saddr;
	bp->daddr = daddr;
	bp->pad = 0;
	bp->protocol = IPPROTO_TCP;
	bp->len = cpu_to_be16(nbytes);

	sg_init_one(&sg, bp, sizeof(*bp));
	ahash_request_set_crypt(hp->req, &sg, NULL, sizeof(*bp));
	return crypto_ahash_update(hp->req);
}

static int tcp_ao_hash_pseudoheader(unsigned short int family,
				    const struct sock *sk,
				    const struct sk_buff *skb,
				    struct tcp_sigpool *hp, int nbytes)
{
	const struct tcphdr *th = tcp_hdr(skb);

	/* TODO: Can we rely on checksum being zero to mean outbound pkt? */
	if (!th->check) {
		if (family == AF_INET)
			return tcp_v4_ao_hash_pseudoheader(hp, sk->sk_daddr,
					sk->sk_rcv_saddr, skb->len);
#if IS_ENABLED(CONFIG_IPV6)
		else if (family == AF_INET6)
			return tcp_v6_ao_hash_pseudoheader(hp, &sk->sk_v6_daddr,
					&sk->sk_v6_rcv_saddr, skb->len);
#endif
		else
			return -EAFNOSUPPORT;
	}

	if (family == AF_INET) {
		const struct iphdr *iph = ip_hdr(skb);

		return tcp_v4_ao_hash_pseudoheader(hp, iph->daddr,
				iph->saddr, skb->len);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (family == AF_INET6) {
		const struct ipv6hdr *iph = ipv6_hdr(skb);

		return tcp_v6_ao_hash_pseudoheader(hp, &iph->daddr,
				&iph->saddr, skb->len);
#endif
	}
	return -EAFNOSUPPORT;
}

/* tcp_ao_hash_sne(struct tcp_sigpool *hp)
 * @hp	- used for hashing
 * @sne - sne value
 */
static int tcp_ao_hash_sne(struct tcp_sigpool *hp, u32 sne)
{
	struct scatterlist sg;
	__be32 *bp;

	bp = (__be32 *)hp->scratch;
	*bp = htonl(sne);

	sg_init_one(&sg, bp, sizeof(*bp));
	ahash_request_set_crypt(hp->req, &sg, NULL, sizeof(*bp));
	return crypto_ahash_update(hp->req);
}

static int tcp_ao_hash_header(struct tcp_sigpool *hp,
			      const struct tcphdr *th,
			      bool exclude_options, u8 *hash,
			      int hash_offset, int hash_len)
{
	int err, len = th->doff << 2;
	struct scatterlist sg;
	u8 *hdr = hp->scratch;

	/* We are not allowed to change tcphdr, make a local copy */
	if (exclude_options) {
		len = sizeof(*th) + sizeof(struct tcp_ao_hdr) + hash_len;
		memcpy(hdr, th, sizeof(*th));
		memcpy(hdr + sizeof(*th),
		       (u8 *)th + hash_offset - sizeof(struct tcp_ao_hdr),
		       sizeof(struct tcp_ao_hdr));
		memset(hdr + sizeof(*th) + sizeof(struct tcp_ao_hdr),
		       0, hash_len);
		((struct tcphdr *)hdr)->check = 0;
	} else {
		len = th->doff << 2;
		memcpy(hdr, th, len);
		/* zero out tcp-ao hash */
		((struct tcphdr *)hdr)->check = 0;
		memset(hdr + hash_offset, 0, hash_len);
	}

	sg_init_one(&sg, hdr, len);
	ahash_request_set_crypt(hp->req, &sg, NULL, len);
	err = crypto_ahash_update(hp->req);
	WARN_ON_ONCE(err != 0);
	return err;
}

int tcp_ao_hash_hdr(unsigned short int family, char *ao_hash,
		    struct tcp_ao_key *key, const u8 *tkey,
		    const union tcp_ao_addr *daddr,
		    const union tcp_ao_addr *saddr,
		    const struct tcphdr *th, u32 sne)
{
	int tkey_len = tcp_ao_digest_size(key);
	int hash_offset = ao_hash - (char *)th;
	struct tcp_sigpool hp;
	void *hash_buf = NULL;

	hash_buf = kmalloc(tkey_len, GFP_ATOMIC);
	if (!hash_buf)
		goto clear_hash_noput;

	if (tcp_sigpool_start(key->tcp_sigpool_id, &hp))
		goto clear_hash_noput;

	if (crypto_ahash_setkey(crypto_ahash_reqtfm(hp.req), tkey, tkey_len))
		goto clear_hash;

	if (crypto_ahash_init(hp.req))
		goto clear_hash;

	if (tcp_ao_hash_sne(&hp, sne))
		goto clear_hash;
	if (family == AF_INET) {
		if (tcp_v4_ao_hash_pseudoheader(&hp, daddr->a4.s_addr,
						saddr->a4.s_addr, th->doff * 4))
			goto clear_hash;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (family == AF_INET6) {
		if (tcp_v6_ao_hash_pseudoheader(&hp, &daddr->a6,
						&saddr->a6, th->doff * 4))
			goto clear_hash;
#endif
	} else {
		WARN_ON_ONCE(1);
		goto clear_hash;
	}
	if (tcp_ao_hash_header(&hp, th, false,
			       ao_hash, hash_offset, tcp_ao_maclen(key)))
		goto clear_hash;
	ahash_request_set_crypt(hp.req, NULL, hash_buf, 0);
	if (crypto_ahash_final(hp.req))
		goto clear_hash;

	memcpy(ao_hash, hash_buf, tcp_ao_maclen(key));
	tcp_sigpool_end(&hp);
	kfree(hash_buf);
	return 0;

clear_hash:
	tcp_sigpool_end(&hp);
clear_hash_noput:
	memset(ao_hash, 0, tcp_ao_maclen(key));
	kfree(hash_buf);
	return 1;
}

int tcp_ao_hash_skb(unsigned short int family,
		    char *ao_hash, struct tcp_ao_key *key,
		    const struct sock *sk, const struct sk_buff *skb,
		    const u8 *tkey, int hash_offset, u32 sne)
{
	const struct tcphdr *th = tcp_hdr(skb);
	int tkey_len = tcp_ao_digest_size(key);
	struct tcp_sigpool hp;
	void *hash_buf = NULL;

	hash_buf = kmalloc(tkey_len, GFP_ATOMIC);
	if (!hash_buf)
		goto clear_hash_noput;

	if (tcp_sigpool_start(key->tcp_sigpool_id, &hp))
		goto clear_hash_noput;

	if (crypto_ahash_setkey(crypto_ahash_reqtfm(hp.req), tkey, tkey_len))
		goto clear_hash;

	/* For now use sha1 by default. Depends on alg in tcp_ao_key */
	if (crypto_ahash_init(hp.req))
		goto clear_hash;

	if (tcp_ao_hash_sne(&hp, sne))
		goto clear_hash;
	if (tcp_ao_hash_pseudoheader(family, sk, skb, &hp, skb->len))
		goto clear_hash;
	if (tcp_ao_hash_header(&hp, th, false,
			       ao_hash, hash_offset, tcp_ao_maclen(key)))
		goto clear_hash;
	if (tcp_sigpool_hash_skb_data(&hp, skb, th->doff << 2))
		goto clear_hash;
	ahash_request_set_crypt(hp.req, NULL, hash_buf, 0);
	if (crypto_ahash_final(hp.req))
		goto clear_hash;

	memcpy(ao_hash, hash_buf, tcp_ao_maclen(key));
	tcp_sigpool_end(&hp);
	kfree(hash_buf);
	return 0;

clear_hash:
	tcp_sigpool_end(&hp);
clear_hash_noput:
	memset(ao_hash, 0, tcp_ao_maclen(key));
	kfree(hash_buf);
	return 1;
}

int tcp_v4_ao_hash_skb(char *ao_hash, struct tcp_ao_key *key,
		       const struct sock *sk, const struct sk_buff *skb,
		       const u8 *tkey, int hash_offset, u32 sne)
{
	return tcp_ao_hash_skb(AF_INET, ao_hash, key, sk, skb,
			       tkey, hash_offset, sne);
}

struct tcp_ao_key *tcp_v4_ao_lookup_rsk(const struct sock *sk,
					struct request_sock *req,
					int sndid, int rcvid)
{
	union tcp_ao_addr *addr =
			(union tcp_ao_addr *)&inet_rsk(req)->ir_rmt_addr;

	return tcp_ao_do_lookup(sk, addr, AF_INET, sndid, rcvid);
}

struct tcp_ao_key *tcp_v4_ao_lookup(const struct sock *sk, struct sock *addr_sk,
				    int sndid, int rcvid)
{
	union tcp_ao_addr *addr = (union tcp_ao_addr *)&addr_sk->sk_daddr;

	return tcp_ao_do_lookup(sk, addr, AF_INET, sndid, rcvid);
}

int tcp_ao_prepare_reset(const struct sock *sk, struct sk_buff *skb,
			 const struct tcp_ao_hdr *aoh, int l3index,
			 struct tcp_ao_key **key, char **traffic_key,
			 bool *allocated_traffic_key, u8 *keyid, u32 *sne)
{
	const struct tcphdr *th = tcp_hdr(skb);
	struct tcp_ao_info *ao_info;

	*allocated_traffic_key = false;
	/* If there's no socket - than initial sisn/disn are unknown.
	 * Drop the segment. RFC5925 (7.7) advises to require graceful
	 * restart [RFC4724]. Alternatively, the RFC5925 advises to
	 * save/restore traffic keys before/after reboot.
	 * Linux TCP-AO support provides TCP_AO_ADD_KEY and TCP_AO_REPAIR
	 * options to restore a socket post-reboot.
	 */
	if (!sk)
		return -ENOTCONN;

	if ((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_NEW_SYN_RECV)) {
		unsigned int family = READ_ONCE(sk->sk_family);
		union tcp_ao_addr *addr;
		__be32 disn, sisn;

		if (sk->sk_state == TCP_NEW_SYN_RECV) {
			struct request_sock *req = inet_reqsk(sk);

			sisn = htonl(tcp_rsk(req)->rcv_isn);
			disn = htonl(tcp_rsk(req)->snt_isn);
			*sne = 0;
		} else {
			sisn = th->seq;
			disn = 0;
		}
		if (IS_ENABLED(CONFIG_IPV6) && family == AF_INET6)
			addr = (union tcp_md5_addr *)&ipv6_hdr(skb)->saddr;
		else
			addr = (union tcp_md5_addr *)&ip_hdr(skb)->saddr;
#if IS_ENABLED(CONFIG_IPV6)
		if (family == AF_INET6 && ipv6_addr_v4mapped(&sk->sk_v6_daddr))
			family = AF_INET;
#endif

		sk = sk_const_to_full_sk(sk);
		ao_info = rcu_dereference(tcp_sk(sk)->ao_info);
		if (!ao_info)
			return -ENOENT;
		*key = tcp_ao_do_lookup(sk, addr, family, -1, aoh->rnext_keyid);
		if (!*key)
			return -ENOENT;
		*traffic_key = kmalloc(tcp_ao_digest_size(*key), GFP_ATOMIC);
		if (!*traffic_key)
			return -ENOMEM;
		*allocated_traffic_key = true;
		if (tcp_ao_calc_key_skb(*key, *traffic_key, skb,
					sisn, disn, family))
			return -1;
		*keyid = (*key)->rcvid;
	} else {
		struct tcp_ao_key *rnext_key;

		if (sk->sk_state == TCP_TIME_WAIT)
			ao_info = rcu_dereference(tcp_twsk(sk)->ao_info);
		else
			ao_info = rcu_dereference(tcp_sk(sk)->ao_info);
		if (!ao_info)
			return -ENOENT;

		*key = tcp_ao_established_key(ao_info, aoh->rnext_keyid, -1);
		if (!*key)
			return -ENOENT;
		*traffic_key = snd_other_key(*key);
		rnext_key = READ_ONCE(ao_info->rnext_key);
		*keyid = rnext_key->rcvid;
		*sne = 0;
	}
	return 0;
}

int tcp_ao_transmit_skb(struct sock *sk, struct sk_buff *skb,
			struct tcp_ao_key *key, struct tcphdr *th,
			__u8 *hash_location)
{
	struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_ao_info *ao;
	void *tkey_buf = NULL;
	u8 *traffic_key;

	ao = rcu_dereference_protected(tcp_sk(sk)->ao_info,
				       lockdep_sock_is_held(sk));
	traffic_key = snd_other_key(key);
	if (unlikely(tcb->tcp_flags & TCPHDR_SYN)) {
		__be32 disn;

		if (!(tcb->tcp_flags & TCPHDR_ACK)) {
			disn = 0;
			tkey_buf = kmalloc(tcp_ao_digest_size(key), GFP_ATOMIC);
			if (!tkey_buf)
				return -ENOMEM;
			traffic_key = tkey_buf;
		} else {
			disn = ao->risn;
		}
		tp->af_specific->ao_calc_key_sk(key, traffic_key,
						sk, ao->lisn, disn, true);
	}
	tp->af_specific->calc_ao_hash(hash_location, key, sk, skb, traffic_key,
				      hash_location - (u8 *)th, 0);
	kfree(tkey_buf);
	return 0;
}

static struct tcp_ao_key *tcp_ao_inbound_lookup(unsigned short int family,
		const struct sock *sk, const struct sk_buff *skb,
		int sndid, int rcvid)
{
	if (family == AF_INET) {
		const struct iphdr *iph = ip_hdr(skb);

		return tcp_ao_do_lookup(sk, (union tcp_ao_addr *)&iph->saddr,
				AF_INET, sndid, rcvid);
	} else {
		const struct ipv6hdr *iph = ipv6_hdr(skb);

		return tcp_ao_do_lookup(sk, (union tcp_ao_addr *)&iph->saddr,
				AF_INET6, sndid, rcvid);
	}
}

void tcp_ao_syncookie(struct sock *sk, const struct sk_buff *skb,
		      struct tcp_request_sock *treq,
		      unsigned short int family)
{
	const struct tcphdr *th = tcp_hdr(skb);
	const struct tcp_ao_hdr *aoh;
	struct tcp_ao_key *key;

	treq->maclen = 0;

	if (tcp_parse_auth_options(th, NULL, &aoh) || !aoh)
		return;

	key = tcp_ao_inbound_lookup(family, sk, skb, -1, aoh->keyid);
	if (!key)
		/* Key not found, continue without TCP-AO */
		return;

	treq->ao_rcv_next = aoh->keyid;
	treq->ao_keyid = aoh->rnext_keyid;
	treq->maclen = tcp_ao_maclen(key);
}

static int tcp_ao_cache_traffic_keys(const struct sock *sk,
				     struct tcp_ao_info *ao,
				     struct tcp_ao_key *ao_key)
{
	u8 *traffic_key = snd_other_key(ao_key);
	int ret;

	ret = tcp_ao_calc_key_sk(ao_key, traffic_key, sk,
				 ao->lisn, ao->risn, true);
	if (ret)
		return ret;

	traffic_key = rcv_other_key(ao_key);
	ret = tcp_ao_calc_key_sk(ao_key, traffic_key, sk,
				 ao->lisn, ao->risn, false);
	return ret;
}

void tcp_ao_connect_init(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_ao_info *ao_info;
	union tcp_ao_addr *addr;
	struct tcp_ao_key *key;
	int family;

	ao_info = rcu_dereference_protected(tp->ao_info,
					    lockdep_sock_is_held(sk));
	if (!ao_info)
		return;

	/* Remove all keys that don't match the peer */
	family = sk->sk_family;
	if (family == AF_INET)
		addr = (union tcp_ao_addr *)&sk->sk_daddr;
#if IS_ENABLED(CONFIG_IPV6)
	else if (family == AF_INET6)
		addr = (union tcp_ao_addr *)&sk->sk_v6_daddr;
#endif
	else
		return;

	hlist_for_each_entry_rcu(key, &ao_info->head, node) {
		if (!tcp_ao_key_cmp(key, addr, key->prefixlen, family, -1, -1))
			continue;

		if (key == ao_info->current_key)
			ao_info->current_key = NULL;
		if (key == ao_info->rnext_key)
			ao_info->rnext_key = NULL;
		hlist_del_rcu(&key->node);
		atomic_sub(tcp_ao_sizeof_key(key), &sk->sk_omem_alloc);
		call_rcu(&key->rcu, tcp_ao_key_free_rcu);
	}

	key = tp->af_specific->ao_lookup(sk, sk, -1, -1);
	if (key) {
		/* if current_key or rnext_key were not provided,
		 * use the first key matching the peer
		 */
		if (!ao_info->current_key)
			ao_info->current_key = key;
		if (!ao_info->rnext_key)
			ao_info->rnext_key = key;
		tp->tcp_header_len += tcp_ao_len(key);

		ao_info->lisn = htonl(tp->write_seq);
	} else {
		/* Can't happen: tcp_connect() verifies that there's
		 * at least one tcp-ao key that matches the remote peer.
		 */
		WARN_ON_ONCE(1);
		rcu_assign_pointer(tp->ao_info, NULL);
		kfree(ao_info);
	}
}

void tcp_ao_established(struct sock *sk)
{
	struct tcp_ao_info *ao;
	struct tcp_ao_key *key;

	ao = rcu_dereference_protected(tcp_sk(sk)->ao_info,
				       lockdep_sock_is_held(sk));
	if (!ao)
		return;

	hlist_for_each_entry_rcu(key, &ao->head, node)
		tcp_ao_cache_traffic_keys(sk, ao, key);
}

void tcp_ao_finish_connect(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_ao_info *ao;
	struct tcp_ao_key *key;

	ao = rcu_dereference_protected(tcp_sk(sk)->ao_info,
				       lockdep_sock_is_held(sk));
	if (!ao)
		return;

	WRITE_ONCE(ao->risn, tcp_hdr(skb)->seq);

	hlist_for_each_entry_rcu(key, &ao->head, node)
		tcp_ao_cache_traffic_keys(sk, ao, key);
}

int tcp_ao_copy_all_matching(const struct sock *sk, struct sock *newsk,
			     struct request_sock *req, struct sk_buff *skb,
			     int family)
{
	struct tcp_ao_key *key, *new_key, *first_key;
	struct tcp_ao_info *new_ao, *ao;
	struct hlist_node *key_head;
	union tcp_ao_addr *addr;
	bool match = false;
	int ret = -ENOMEM;

	ao = rcu_dereference(tcp_sk(sk)->ao_info);
	if (!ao)
		return 0;

	/* New socket without TCP-AO on it */
	if (!tcp_rsk_used_ao(req))
		return 0;

	new_ao = tcp_ao_alloc_info(GFP_ATOMIC);
	if (!new_ao)
		return -ENOMEM;
	new_ao->lisn = htonl(tcp_rsk(req)->snt_isn);
	new_ao->risn = htonl(tcp_rsk(req)->rcv_isn);
	new_ao->ao_required = ao->ao_required;

	if (family == AF_INET) {
		addr = (union tcp_ao_addr *)&newsk->sk_daddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (family == AF_INET6) {
		addr = (union tcp_ao_addr *)&newsk->sk_v6_daddr;
#endif
	} else {
		ret = -EAFNOSUPPORT;
		goto free_ao;
	}

	hlist_for_each_entry_rcu(key, &ao->head, node) {
		if (tcp_ao_key_cmp(key, addr, key->prefixlen, family, -1, -1))
			continue;

		new_key = tcp_ao_copy_key(newsk, key);
		if (!new_key)
			goto free_and_exit;

		tcp_ao_cache_traffic_keys(newsk, new_ao, new_key);
		tcp_ao_link_mkt(new_ao, new_key);
		match = true;
	}

	if (!match) {
		/* RFC5925 (7.4.1) specifies that the TCP-AO status
		 * of a connection is determined on the initial SYN.
		 * At this point the connection was TCP-AO enabled, so
		 * it can't switch to being unsigned if peer's key
		 * disappears on the listening socket.
		 */
		ret = -EKEYREJECTED;
		goto free_and_exit;
	}

	key_head = rcu_dereference(hlist_first_rcu(&new_ao->head));
	first_key = hlist_entry_safe(key_head, struct tcp_ao_key, node);

	key = tcp_ao_established_key(new_ao, tcp_rsk(req)->ao_keyid, -1);
	if (key)
		new_ao->current_key = key;
	else
		new_ao->current_key = first_key;

	/* set rnext_key */
	key = tcp_ao_established_key(new_ao, -1, tcp_rsk(req)->ao_rcv_next);
	if (key)
		new_ao->rnext_key = key;
	else
		new_ao->rnext_key = first_key;

	sk_gso_disable(newsk);
	rcu_assign_pointer(tcp_sk(newsk)->ao_info, new_ao);

	return 0;

free_and_exit:
	hlist_for_each_entry_safe(key, key_head, &new_ao->head, node) {
		hlist_del(&key->node);
		tcp_sigpool_release(key->tcp_sigpool_id);
		atomic_sub(tcp_ao_sizeof_key(key), &newsk->sk_omem_alloc);
		kfree_sensitive(key);
	}
free_ao:
	kfree(new_ao);
	return ret;
}

static bool tcp_ao_can_set_current_rnext(struct sock *sk)
{
	/* There aren't current/rnext keys on TCP_LISTEN sockets */
	if (sk->sk_state == TCP_LISTEN)
		return false;
	return true;
}

static int tcp_ao_verify_ipv4(struct sock *sk, struct tcp_ao_add *cmd,
			      union tcp_ao_addr **addr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)&cmd->addr;
	struct inet_sock *inet = inet_sk(sk);

	if (sin->sin_family != AF_INET)
		return -EINVAL;

	/* Currently matching is not performed on port (or port ranges) */
	if (sin->sin_port != 0)
		return -EINVAL;

	/* Check prefix and trailing 0's in addr */
	if (cmd->prefix != 0) {
		__be32 mask;

		if (ntohl(sin->sin_addr.s_addr) == INADDR_ANY)
			return -EINVAL;
		if (cmd->prefix > 32)
			return -EINVAL;

		mask = inet_make_mask(cmd->prefix);
		if (sin->sin_addr.s_addr & ~mask)
			return -EINVAL;

		/* Check that MKT address is consistent with socket */
		if (ntohl(inet->inet_daddr) != INADDR_ANY &&
		    (inet->inet_daddr & mask) != sin->sin_addr.s_addr)
			return -EINVAL;
	} else {
		if (ntohl(sin->sin_addr.s_addr) != INADDR_ANY)
			return -EINVAL;
	}

	*addr = (union tcp_ao_addr *)&sin->sin_addr;
	return 0;
}

static int tcp_ao_parse_crypto(struct tcp_ao_add *cmd, struct tcp_ao_key *key)
{
	unsigned int syn_tcp_option_space;
	bool is_kdf_aes_128_cmac = false;
	struct crypto_ahash *tfm;
	struct tcp_sigpool hp;
	void *tmp_key = NULL;
	int err;

	/* RFC5926, 3.1.1.2. KDF_AES_128_CMAC */
	if (!strcmp("cmac(aes128)", cmd->alg_name)) {
		strscpy(cmd->alg_name, "cmac(aes)", sizeof(cmd->alg_name));
		is_kdf_aes_128_cmac = (cmd->keylen != 16);
		tmp_key = kmalloc(cmd->keylen, GFP_KERNEL);
		if (!tmp_key)
			return -ENOMEM;
	}

	key->maclen = cmd->maclen ?: 12; /* 12 is the default in RFC5925 */

	/* Check: maclen + tcp-ao header <= (MAX_TCP_OPTION_SPACE - mss
	 *					- tstamp - wscale - sackperm),
	 * see tcp_syn_options(), tcp_synack_options(), commit 33ad798c924b.
	 *
	 * In order to allow D-SACK with TCP-AO, the header size should be:
	 * (MAX_TCP_OPTION_SPACE - TCPOLEN_TSTAMP_ALIGNED
	 *			- TCPOLEN_SACK_BASE_ALIGNED
	 *			- 2 * TCPOLEN_SACK_PERBLOCK) = 8 (maclen = 4),
	 * see tcp_established_options().
	 *
	 * RFC5925, 2.2:
	 * Typical MACs are 96-128 bits (12-16 bytes), but any length
	 * that fits in the header of the segment being authenticated
	 * is allowed.
	 *
	 * RFC5925, 7.6:
	 * TCP-AO continues to consume 16 bytes in non-SYN segments,
	 * leaving a total of 24 bytes for other options, of which
	 * the timestamp consumes 10.  This leaves 14 bytes, of which 10
	 * are used for a single SACK block. When two SACK blocks are used,
	 * such as to handle D-SACK, a smaller TCP-AO MAC would be required
	 * to make room for the additional SACK block (i.e., to leave 18
	 * bytes for the D-SACK variant of the SACK option) [RFC2883].
	 * Note that D-SACK is not supportable in TCP MD5 in the presence
	 * of timestamps, because TCP MD5’s MAC length is fixed and too
	 * large to leave sufficient option space.
	 */
	syn_tcp_option_space = MAX_TCP_OPTION_SPACE;
	syn_tcp_option_space -= TCPOLEN_TSTAMP_ALIGNED;
	syn_tcp_option_space -= TCPOLEN_WSCALE_ALIGNED;
	syn_tcp_option_space -= TCPOLEN_SACKPERM_ALIGNED;
	if (tcp_ao_len(key) > syn_tcp_option_space) {
		err = -EMSGSIZE;
		goto err_kfree;
	}

	key->keylen = cmd->keylen;
	memcpy(key->key, cmd->key, cmd->keylen);

	err = tcp_sigpool_start(key->tcp_sigpool_id, &hp);
	if (err)
		goto err_kfree;

	tfm = crypto_ahash_reqtfm(hp.req);
	if (is_kdf_aes_128_cmac) {
		void *scratch = hp.scratch;
		struct scatterlist sg;

		memcpy(tmp_key, cmd->key, cmd->keylen);
		sg_init_one(&sg, tmp_key, cmd->keylen);

		/* Using zero-key of 16 bytes as described in RFC5926 */
		memset(scratch, 0, 16);
		err = crypto_ahash_setkey(tfm, scratch, 16);
		if (err)
			goto err_pool_end;

		err = crypto_ahash_init(hp.req);
		if (err)
			goto err_pool_end;

		ahash_request_set_crypt(hp.req, &sg, key->key, cmd->keylen);
		err = crypto_ahash_update(hp.req);
		if (err)
			goto err_pool_end;

		err |= crypto_ahash_final(hp.req);
		if (err)
			goto err_pool_end;
		key->keylen = 16;
	}

	err = crypto_ahash_setkey(tfm, key->key, key->keylen);
	if (err)
		goto err_pool_end;

	tcp_sigpool_end(&hp);
	kfree_sensitive(tmp_key);

	if (tcp_ao_maclen(key) > key->digest_size)
		return -EINVAL;

	return 0;

err_pool_end:
	tcp_sigpool_end(&hp);
err_kfree:
	kfree_sensitive(tmp_key);
	return err;
}

#if IS_ENABLED(CONFIG_IPV6)
static int tcp_ao_verify_ipv6(struct sock *sk, struct tcp_ao_add *cmd,
			      union tcp_ao_addr **paddr,
			      unsigned short int *family)
{
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&cmd->addr;
	struct in6_addr *addr = &sin6->sin6_addr;
	u8 prefix = cmd->prefix;

	if (sin6->sin6_family != AF_INET6)
		return -EINVAL;

	/* Currently matching is not performed on port (or port ranges) */
	if (sin6->sin6_port != 0)
		return -EINVAL;

	/* Check prefix and trailing 0's in addr */
	if (cmd->prefix != 0 && ipv6_addr_v4mapped(addr)) {
		__be32 addr4 = addr->s6_addr32[3];
		__be32 mask;

		if (prefix > 32 || ntohl(addr4) == INADDR_ANY)
			return -EINVAL;

		mask = inet_make_mask(prefix);
		if (addr4 & ~mask)
			return -EINVAL;

		/* Check that MKT address is consistent with socket */
		if (!ipv6_addr_any(&sk->sk_v6_daddr)) {
			__be32 daddr4 = sk->sk_v6_daddr.s6_addr32[3];

			if (!ipv6_addr_v4mapped(&sk->sk_v6_daddr))
				return -EINVAL;
			if ((daddr4 & mask) != addr4)
				return -EINVAL;
		}

		*paddr = (union tcp_ao_addr *)&addr->s6_addr32[3];
		*family = AF_INET;
		return 0;
	} else if (cmd->prefix != 0) {
		struct in6_addr pfx;

		if (ipv6_addr_any(addr) || prefix > 128)
			return -EINVAL;

		ipv6_addr_prefix(&pfx, addr, prefix);
		if (ipv6_addr_cmp(&pfx, addr))
			return -EINVAL;

		/* Check that MKT address is consistent with socket */
		if (!ipv6_addr_any(&sk->sk_v6_daddr) &&
		    !ipv6_prefix_equal(&sk->sk_v6_daddr, addr, prefix))

			return -EINVAL;
	} else {
		if (!ipv6_addr_any(addr))
			return -EINVAL;
	}

	*paddr = (union tcp_ao_addr *)addr;
	return 0;
}
#else
static int tcp_ao_verify_ipv6(struct sock *sk, struct tcp_ao_add *cmd,
			      union tcp_ao_addr **paddr,
			      unsigned short int *family)
{
	return -EOPNOTSUPP;
}
#endif

static struct tcp_ao_info *setsockopt_ao_info(struct sock *sk)
{
	if (sk_fullsock(sk)) {
		return rcu_dereference_protected(tcp_sk(sk)->ao_info,
						 lockdep_sock_is_held(sk));
	} else if (sk->sk_state == TCP_TIME_WAIT) {
		return rcu_dereference_protected(tcp_twsk(sk)->ao_info,
						 lockdep_sock_is_held(sk));
	}
	return ERR_PTR(-ESOCKTNOSUPPORT);
}

#define TCP_AO_KEYF_ALL		(0)

static struct tcp_ao_key *tcp_ao_key_alloc(struct sock *sk,
					   struct tcp_ao_add *cmd)
{
	const char *algo = cmd->alg_name;
	unsigned int digest_size;
	struct crypto_ahash *tfm;
	struct tcp_ao_key *key;
	struct tcp_sigpool hp;
	int err, pool_id;
	size_t size;

	/* Force null-termination of alg_name */
	cmd->alg_name[ARRAY_SIZE(cmd->alg_name) - 1] = '\0';

	/* RFC5926, 3.1.1.2. KDF_AES_128_CMAC */
	if (!strcmp("cmac(aes128)", algo))
		algo = "cmac(aes)";

	/* Full TCP header (th->doff << 2) should fit into scratch area,
	 * see tcp_ao_hash_header().
	 */
	pool_id = tcp_sigpool_alloc_ahash(algo, 60);
	if (pool_id < 0)
		return ERR_PTR(pool_id);

	err = tcp_sigpool_start(pool_id, &hp);
	if (err)
		goto err_free_pool;

	tfm = crypto_ahash_reqtfm(hp.req);
	if (crypto_ahash_alignmask(tfm) > TCP_AO_KEY_ALIGN) {
		err = -EOPNOTSUPP;
		goto err_pool_end;
	}
	digest_size = crypto_ahash_digestsize(tfm);
	tcp_sigpool_end(&hp);

	size = sizeof(struct tcp_ao_key) + (digest_size << 1);
	key = sock_kmalloc(sk, size, GFP_KERNEL);
	if (!key) {
		err = -ENOMEM;
		goto err_free_pool;
	}

	key->tcp_sigpool_id = pool_id;
	key->digest_size = digest_size;
	return key;

err_pool_end:
	tcp_sigpool_end(&hp);
err_free_pool:
	tcp_sigpool_release(pool_id);
	return ERR_PTR(err);
}

static int tcp_ao_add_cmd(struct sock *sk, unsigned short int family,
			  sockptr_t optval, int optlen)
{
	struct tcp_ao_info *ao_info;
	union tcp_ao_addr *addr;
	struct tcp_ao_key *key;
	struct tcp_ao_add cmd;
	bool first = false;
	int ret;

	if (optlen < sizeof(cmd))
		return -EINVAL;

	ret = copy_struct_from_sockptr(&cmd, sizeof(cmd), optval, optlen);
	if (ret)
		return ret;

	if (cmd.keylen > TCP_AO_MAXKEYLEN)
		return -EINVAL;

	if (cmd.reserved != 0 || cmd.reserved2 != 0)
		return -EINVAL;

	if (family == AF_INET)
		ret = tcp_ao_verify_ipv4(sk, &cmd, &addr);
	else
		ret = tcp_ao_verify_ipv6(sk, &cmd, &addr, &family);
	if (ret)
		return ret;

	if (cmd.keyflags & ~TCP_AO_KEYF_ALL)
		return -EINVAL;

	if (cmd.set_current || cmd.set_rnext) {
		if (!tcp_ao_can_set_current_rnext(sk))
			return -EINVAL;
	}

	/* Don't allow keys for peers that have a matching TCP-MD5 key */
	if (tcp_md5_do_lookup_any_l3index(sk, addr, family))
		return -EKEYREJECTED;

	ao_info = setsockopt_ao_info(sk);
	if (IS_ERR(ao_info))
		return PTR_ERR(ao_info);

	if (!ao_info) {
		ao_info = tcp_ao_alloc_info(GFP_KERNEL);
		if (!ao_info)
			return -ENOMEM;
		first = true;
	} else {
		/* Check that neither RecvID nor SendID match any
		 * existing key for the peer, RFC5925 3.1:
		 * > The IDs of MKTs MUST NOT overlap where their
		 * > TCP connection identifiers overlap.
		 */
		if (__tcp_ao_do_lookup(sk, addr, family,
				       cmd.prefix, -1, cmd.rcvid))
			return -EEXIST;
		if (__tcp_ao_do_lookup(sk, addr, family,
				       cmd.prefix, cmd.sndid, -1))
			return -EEXIST;
	}

	key = tcp_ao_key_alloc(sk, &cmd);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto err_free_ao;
	}

	INIT_HLIST_NODE(&key->node);
	memcpy(&key->addr, addr, (family == AF_INET) ? sizeof(struct in_addr) :
						       sizeof(struct in6_addr));
	key->prefixlen	= cmd.prefix;
	key->family	= family;
	key->keyflags	= cmd.keyflags;
	key->sndid	= cmd.sndid;
	key->rcvid	= cmd.rcvid;

	ret = tcp_ao_parse_crypto(&cmd, key);
	if (ret < 0)
		goto err_free_sock;

	/* Change this condition if we allow adding keys in states
	 * like close_wait, syn_sent or fin_wait...
	 */
	if (sk->sk_state == TCP_ESTABLISHED)
		tcp_ao_cache_traffic_keys(sk, ao_info, key);

	tcp_ao_link_mkt(ao_info, key);
	if (first) {
		sk_gso_disable(sk);
		rcu_assign_pointer(tcp_sk(sk)->ao_info, ao_info);
	}

	if (cmd.set_current)
		WRITE_ONCE(ao_info->current_key, key);
	if (cmd.set_rnext)
		WRITE_ONCE(ao_info->rnext_key, key);
	return 0;

err_free_sock:
	atomic_sub(tcp_ao_sizeof_key(key), &sk->sk_omem_alloc);
	tcp_sigpool_release(key->tcp_sigpool_id);
	kfree_sensitive(key);
err_free_ao:
	if (first)
		kfree(ao_info);
	return ret;
}

static int tcp_ao_delete_key(struct sock *sk, struct tcp_ao_info *ao_info,
			     struct tcp_ao_key *key,
			     struct tcp_ao_key *new_current,
			     struct tcp_ao_key *new_rnext)
{
	int err;

	hlist_del_rcu(&key->node);

	/* At this moment another CPU could have looked this key up
	 * while it was unlinked from the list. Wait for RCU grace period,
	 * after which the key is off-list and can't be looked up again;
	 * the rx path [just before RCU came] might have used it and set it
	 * as current_key (very unlikely).
	 */
	synchronize_rcu();
	if (new_current)
		WRITE_ONCE(ao_info->current_key, new_current);
	if (new_rnext)
		WRITE_ONCE(ao_info->rnext_key, new_rnext);

	if (unlikely(READ_ONCE(ao_info->current_key) == key ||
		     READ_ONCE(ao_info->rnext_key) == key)) {
		err = -EBUSY;
		goto add_key;
	}

	atomic_sub(tcp_ao_sizeof_key(key), &sk->sk_omem_alloc);
	call_rcu(&key->rcu, tcp_ao_key_free_rcu);

	return 0;
add_key:
	hlist_add_head_rcu(&key->node, &ao_info->head);
	return err;
}

static int tcp_ao_del_cmd(struct sock *sk, unsigned short int family,
			  sockptr_t optval, int optlen)
{
	struct tcp_ao_key *key, *new_current = NULL, *new_rnext = NULL;
	struct tcp_ao_info *ao_info;
	union tcp_ao_addr *addr;
	struct tcp_ao_del cmd;
	int addr_len;
	__u8 prefix;
	u16 port;
	int err;

	if (optlen < sizeof(cmd))
		return -EINVAL;

	err = copy_struct_from_sockptr(&cmd, sizeof(cmd), optval, optlen);
	if (err)
		return err;

	if (cmd.reserved != 0 || cmd.reserved2 != 0)
		return -EINVAL;

	if (cmd.set_current || cmd.set_rnext) {
		if (!tcp_ao_can_set_current_rnext(sk))
			return -EINVAL;
	}

	ao_info = setsockopt_ao_info(sk);
	if (IS_ERR(ao_info))
		return PTR_ERR(ao_info);
	if (!ao_info)
		return -ENOENT;

	/* For sockets in TCP_CLOSED it's possible set keys that aren't
	 * matching the future peer (address/VRF/etc),
	 * tcp_ao_connect_init() will choose a correct matching MKT
	 * if there's any.
	 */
	if (cmd.set_current) {
		new_current = tcp_ao_established_key(ao_info, cmd.current_key, -1);
		if (!new_current)
			return -ENOENT;
	}
	if (cmd.set_rnext) {
		new_rnext = tcp_ao_established_key(ao_info, -1, cmd.rnext);
		if (!new_rnext)
			return -ENOENT;
	}

	if (family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)&cmd.addr;

		addr = (union tcp_ao_addr *)&sin->sin_addr;
		addr_len = sizeof(struct in_addr);
		port = ntohs(sin->sin_port);
	} else {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&cmd.addr;
		struct in6_addr *addr6 = &sin6->sin6_addr;

		if (ipv6_addr_v4mapped(addr6)) {
			addr = (union tcp_ao_addr *)&addr6->s6_addr32[3];
			addr_len = sizeof(struct in_addr);
			family = AF_INET;
		} else {
			addr = (union tcp_ao_addr *)addr6;
			addr_len = sizeof(struct in6_addr);
		}
		port = ntohs(sin6->sin6_port);
	}
	prefix = cmd.prefix;

	/* Currently matching is not performed on port (or port ranges) */
	if (port != 0)
		return -EINVAL;

	/* We could choose random present key here for current/rnext
	 * but that's less predictable. Let's be strict and don't
	 * allow removing a key that's in use. RFC5925 doesn't
	 * specify how-to coordinate key removal, but says:
	 * "It is presumed that an MKT affecting a particular
	 * connection cannot be destroyed during an active connection"
	 */
	hlist_for_each_entry_rcu(key, &ao_info->head, node) {
		if (cmd.sndid != key->sndid ||
		    cmd.rcvid != key->rcvid)
			continue;

		if (family != key->family ||
		    prefix != key->prefixlen ||
		    memcmp(addr, &key->addr, addr_len))
			continue;

		if (key == new_current || key == new_rnext)
			continue;

		return tcp_ao_delete_key(sk, ao_info, key,
					  new_current, new_rnext);
	}
	return -ENOENT;
}

/* cmd.ao_required makes a socket TCP-AO only.
 * Don't allow any md5 keys for any l3intf on the socket together with it.
 * Restricting it early in setsockopt() removes a check for
 * ao_info->ao_required on inbound tcp segment fast-path.
 */
static int tcp_ao_required_verify(struct sock *sk)
{
#ifdef CONFIG_TCP_MD5SIG
	const struct tcp_md5sig_info *md5sig;

	if (!static_branch_unlikely(&tcp_md5_needed.key))
		return 0;

	md5sig = rcu_dereference_check(tcp_sk(sk)->md5sig_info,
				       lockdep_sock_is_held(sk));
	if (!md5sig)
		return 0;

	if (rcu_dereference_check(hlist_first_rcu(&md5sig->head),
				  lockdep_sock_is_held(sk)))
		return 1;
#endif
	return 0;
}

static int tcp_ao_info_cmd(struct sock *sk, unsigned short int family,
			   sockptr_t optval, int optlen)
{
	struct tcp_ao_key *new_current = NULL, *new_rnext = NULL;
	struct tcp_ao_info *ao_info;
	struct tcp_ao_info_opt cmd;
	bool first = false;
	int err;

	if (optlen < sizeof(cmd))
		return -EINVAL;

	err = copy_struct_from_sockptr(&cmd, sizeof(cmd), optval, optlen);
	if (err)
		return err;

	if (cmd.set_current || cmd.set_rnext) {
		if (!tcp_ao_can_set_current_rnext(sk))
			return -EINVAL;
	}

	if (cmd.reserved != 0)
		return -EINVAL;

	ao_info = setsockopt_ao_info(sk);
	if (IS_ERR(ao_info))
		return PTR_ERR(ao_info);
	if (!ao_info) {
		ao_info = tcp_ao_alloc_info(GFP_KERNEL);
		if (!ao_info)
			return -ENOMEM;
		first = true;
	}

	if (cmd.ao_required && tcp_ao_required_verify(sk))
		return -EKEYREJECTED;

	/* For sockets in TCP_CLOSED it's possible set keys that aren't
	 * matching the future peer (address/port/VRF/etc),
	 * tcp_ao_connect_init() will choose a correct matching MKT
	 * if there's any.
	 */
	if (cmd.set_current) {
		new_current = tcp_ao_established_key(ao_info, cmd.current_key, -1);
		if (!new_current) {
			err = -ENOENT;
			goto out;
		}
	}
	if (cmd.set_rnext) {
		new_rnext = tcp_ao_established_key(ao_info, -1, cmd.rnext);
		if (!new_rnext) {
			err = -ENOENT;
			goto out;
		}
	}

	ao_info->ao_required = cmd.ao_required;
	if (new_current)
		WRITE_ONCE(ao_info->current_key, new_current);
	if (new_rnext)
		WRITE_ONCE(ao_info->rnext_key, new_rnext);
	if (first) {
		sk_gso_disable(sk);
		rcu_assign_pointer(tcp_sk(sk)->ao_info, ao_info);
	}
	return 0;
out:
	if (first)
		kfree(ao_info);
	return err;
}

int tcp_parse_ao(struct sock *sk, int cmd, unsigned short int family,
		 sockptr_t optval, int optlen)
{
	if (WARN_ON_ONCE(family != AF_INET && family != AF_INET6))
		return -EAFNOSUPPORT;

	switch (cmd) {
	case TCP_AO_ADD_KEY:
		return tcp_ao_add_cmd(sk, family, optval, optlen);
	case TCP_AO_DEL_KEY:
		return tcp_ao_del_cmd(sk, family, optval, optlen);
	case TCP_AO_INFO:
		return tcp_ao_info_cmd(sk, family, optval, optlen);
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}
}

int tcp_v4_parse_ao(struct sock *sk, int cmd, sockptr_t optval, int optlen)
{
	return tcp_parse_ao(sk, cmd, AF_INET, optval, optlen);
}

