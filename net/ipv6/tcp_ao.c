// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP Authentication Option (TCP-AO).
 *		See RFC5925.
 *
 * Authors:	Dmitry Safonov <dima@arista.com>
 *		Francesco Ruggeri <fruggeri@arista.com>
 *		Salam Noureddine <noureddine@arista.com>
 */
#include <crypto/hash.h>
#include <linux/tcp.h>

#include <net/tcp.h>
#include <net/ipv6.h>

static int tcp_v6_ao_calc_key(struct tcp_ao_key *mkt, u8 *key,
			      const struct in6_addr *saddr,
			      const struct in6_addr *daddr,
			      __be16 sport, __be16 dport,
			      __be32 sisn, __be32 disn)
{
	struct kdf_input_block {
		u8			counter;
		u8			label[6];
		struct tcp6_ao_context	ctx;
		__be16			outlen;
	} __packed * tmp;
	struct tcp_sigpool hp;
	int err;

	err = tcp_sigpool_start(mkt->tcp_sigpool_id, &hp);
	if (err)
		return err;

	tmp = hp.scratch;
	tmp->counter	= 1;
	memcpy(tmp->label, "TCP-AO", 6);
	tmp->ctx.saddr	= *saddr;
	tmp->ctx.daddr	= *daddr;
	tmp->ctx.sport	= sport;
	tmp->ctx.dport	= dport;
	tmp->ctx.sisn	= sisn;
	tmp->ctx.disn	= disn;
	tmp->outlen	= htons(tcp_ao_digest_size(mkt) * 8); /* in bits */

	err = tcp_ao_calc_traffic_key(mkt, key, tmp, sizeof(*tmp), &hp);
	tcp_sigpool_end(&hp);

	return err;
}

int tcp_v6_ao_calc_key_skb(struct tcp_ao_key *mkt, u8 *key,
			   const struct sk_buff *skb,
			   __be32 sisn, __be32 disn)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct tcphdr *th = tcp_hdr(skb);

	return tcp_v6_ao_calc_key(mkt, key, &iph->saddr,
				  &iph->daddr, th->source,
				  th->dest, sisn, disn);
}

int tcp_v6_ao_calc_key_sk(struct tcp_ao_key *mkt, u8 *key,
			  const struct sock *sk, __be32 sisn,
			  __be32 disn, bool send)
{
	if (send)
		return tcp_v6_ao_calc_key(mkt, key, &sk->sk_v6_rcv_saddr,
					  &sk->sk_v6_daddr, htons(sk->sk_num),
					  sk->sk_dport, sisn, disn);
	else
		return tcp_v6_ao_calc_key(mkt, key, &sk->sk_v6_daddr,
					  &sk->sk_v6_rcv_saddr, sk->sk_dport,
					  htons(sk->sk_num), disn, sisn);
}

int tcp_v6_ao_calc_key_rsk(struct tcp_ao_key *mkt, u8 *key,
			   struct request_sock *req)
{
	struct inet_request_sock *ireq = inet_rsk(req);

	return tcp_v6_ao_calc_key(mkt, key,
			&ireq->ir_v6_loc_addr, &ireq->ir_v6_rmt_addr,
			htons(ireq->ir_num), ireq->ir_rmt_port,
			htonl(tcp_rsk(req)->snt_isn),
			htonl(tcp_rsk(req)->rcv_isn));
}

struct tcp_ao_key *tcp_v6_ao_lookup(const struct sock *sk,
				    struct sock *addr_sk,
				    int sndid, int rcvid)
{
	int l3index = l3mdev_master_ifindex_by_index(sock_net(sk),
						     addr_sk->sk_bound_dev_if);
	struct in6_addr *addr = &addr_sk->sk_v6_daddr;

	return tcp_ao_do_lookup(sk, l3index, (union tcp_ao_addr *)addr,
				AF_INET6, sndid, rcvid);
}

struct tcp_ao_key *tcp_v6_ao_lookup_rsk(const struct sock *sk,
					struct request_sock *req,
					int sndid, int rcvid)
{
	struct inet_request_sock *ireq = inet_rsk(req);
	struct in6_addr *addr = &ireq->ir_v6_rmt_addr;
	int l3index;

	l3index = l3mdev_master_ifindex_by_index(sock_net(sk), ireq->ir_iif);
	return tcp_ao_do_lookup(sk, l3index, (union tcp_ao_addr *)addr,
				AF_INET6, sndid, rcvid);
}

int tcp_v6_ao_hash_pseudoheader(struct tcp_sigpool *hp,
				const struct in6_addr *daddr,
				const struct in6_addr *saddr, int nbytes)
{
	struct tcp6_pseudohdr *bp;
	struct scatterlist sg;

	bp = hp->scratch;
	/* 1. TCP pseudo-header (RFC2460) */
	bp->saddr = *saddr;
	bp->daddr = *daddr;
	bp->len = cpu_to_be32(nbytes);
	bp->protocol = cpu_to_be32(IPPROTO_TCP);

	sg_init_one(&sg, bp, sizeof(*bp));
	ahash_request_set_crypt(hp->req, &sg, NULL, sizeof(*bp));
	return crypto_ahash_update(hp->req);
}

int tcp_v6_ao_hash_skb(char *ao_hash, struct tcp_ao_key *key,
		       const struct sock *sk, const struct sk_buff *skb,
		       const u8 *tkey, int hash_offset, u32 sne)
{
	return tcp_ao_hash_skb(AF_INET6, ao_hash, key, sk, skb, tkey,
			hash_offset, sne);
}

int tcp_v6_parse_ao(struct sock *sk, int cmd,
		    sockptr_t optval, int optlen)
{
	return tcp_parse_ao(sk, cmd, AF_INET6, optval, optlen);
}

int tcp_v6_ao_synack_hash(char *ao_hash, struct tcp_ao_key *ao_key,
			  struct request_sock *req, const struct sk_buff *skb,
			  int hash_offset, u32 sne)
{
	void *hash_buf = NULL;
	int err;

	hash_buf = kmalloc(tcp_ao_digest_size(ao_key), GFP_ATOMIC);
	if (!hash_buf)
		return -ENOMEM;

	err = tcp_v6_ao_calc_key_rsk(ao_key, hash_buf, req);
	if (err)
		goto out;

	err = tcp_ao_hash_skb(AF_INET6, ao_hash, ao_key, req_to_sk(req), skb,
			      hash_buf, hash_offset, sne);
out:
	kfree(hash_buf);
	return err;
}
