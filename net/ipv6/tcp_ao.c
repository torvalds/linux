// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP Authentication Option (TCP-AO).
 *		See RFC5925.
 *
 * Authors:	Dmitry Safonov <dima@arista.com>
 *		Francesco Ruggeri <fruggeri@arista.com>
 *		Salam Noureddine <noureddine@arista.com>
 */
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

static struct tcp_ao_key *tcp_v6_ao_do_lookup(const struct sock *sk,
					      const struct in6_addr *addr,
					      int sndid, int rcvid)
{
	return tcp_ao_do_lookup(sk, (union tcp_ao_addr *)addr, AF_INET6,
				sndid, rcvid);
}

struct tcp_ao_key *tcp_v6_ao_lookup(const struct sock *sk,
				    struct sock *addr_sk,
				    int sndid, int rcvid)
{
	struct in6_addr *addr = &addr_sk->sk_v6_daddr;

	return tcp_v6_ao_do_lookup(sk, addr, sndid, rcvid);
}

int tcp_v6_parse_ao(struct sock *sk, int cmd,
		    sockptr_t optval, int optlen)
{
	return tcp_parse_ao(sk, cmd, AF_INET6, optval, optlen);
}
