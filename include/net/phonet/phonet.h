/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * File: af_phonet.h
 *
 * Phonet sockets kernel definitions
 *
 * Copyright (C) 2008 Nokia Corporation.
 */

#ifndef AF_PHONET_H
#define AF_PHONET_H

#include <linux/phonet.h>
#include <linux/skbuff.h>
#include <net/sock.h>

/*
 * The lower layers may not require more space, ever. Make sure it's
 * enough.
 */
#define MAX_PHONET_HEADER	(8 + MAX_HEADER)

/*
 * Every Phonet* socket has this structure first in its
 * protocol-specific structure under name c.
 */
struct pn_sock {
	struct sock	sk;
	u16		sobject;
	u16		dobject;
	u8		resource;
};

static inline struct pn_sock *pn_sk(struct sock *sk)
{
	return (struct pn_sock *)sk;
}

extern const struct proto_ops phonet_dgram_ops;

void pn_sock_init(void);
struct sock *pn_find_sock_by_sa(struct net *net, const struct sockaddr_pn *sa);
void pn_deliver_sock_broadcast(struct net *net, struct sk_buff *skb);
void phonet_get_local_port_range(int *min, int *max);
int pn_sock_hash(struct sock *sk);
void pn_sock_unhash(struct sock *sk);
int pn_sock_get_port(struct sock *sk, unsigned short sport);

struct sock *pn_find_sock_by_res(struct net *net, u8 res);
int pn_sock_bind_res(struct sock *sock, u8 res);
int pn_sock_unbind_res(struct sock *sk, u8 res);
void pn_sock_unbind_all_res(struct sock *sk);

int pn_skb_send(struct sock *sk, struct sk_buff *skb,
		const struct sockaddr_pn *target);

static inline struct phonethdr *pn_hdr(struct sk_buff *skb)
{
	return (struct phonethdr *)skb_network_header(skb);
}

static inline struct phonetmsg *pn_msg(struct sk_buff *skb)
{
	return (struct phonetmsg *)skb_transport_header(skb);
}

/*
 * Get the other party's sockaddr from received skb. The skb begins
 * with a Phonet header.
 */
static inline
void pn_skb_get_src_sockaddr(struct sk_buff *skb, struct sockaddr_pn *sa)
{
	struct phonethdr *ph = pn_hdr(skb);
	u16 obj = pn_object(ph->pn_sdev, ph->pn_sobj);

	sa->spn_family = AF_PHONET;
	pn_sockaddr_set_object(sa, obj);
	pn_sockaddr_set_resource(sa, ph->pn_res);
	memset(sa->spn_zero, 0, sizeof(sa->spn_zero));
}

static inline
void pn_skb_get_dst_sockaddr(struct sk_buff *skb, struct sockaddr_pn *sa)
{
	struct phonethdr *ph = pn_hdr(skb);
	u16 obj = pn_object(ph->pn_rdev, ph->pn_robj);

	sa->spn_family = AF_PHONET;
	pn_sockaddr_set_object(sa, obj);
	pn_sockaddr_set_resource(sa, ph->pn_res);
	memset(sa->spn_zero, 0, sizeof(sa->spn_zero));
}

/* Protocols in Phonet protocol family. */
struct phonet_protocol {
	const struct proto_ops	*ops;
	struct proto		*prot;
	int			sock_type;
};

int phonet_proto_register(unsigned int protocol,
		const struct phonet_protocol *pp);
void phonet_proto_unregister(unsigned int protocol,
		const struct phonet_protocol *pp);

int phonet_sysctl_init(void);
void phonet_sysctl_exit(void);
int isi_register(void);
void isi_unregister(void);

static inline bool sk_is_phonet(struct sock *sk)
{
	return sk->sk_family == PF_PHONET;
}

static inline int phonet_sk_ioctl(struct sock *sk, unsigned int cmd,
				  void __user *arg)
{
	int karg;

	switch (cmd) {
	case SIOCPNADDRESOURCE:
	case SIOCPNDELRESOURCE:
		if (get_user(karg, (int __user *)arg))
			return -EFAULT;

		return sk->sk_prot->ioctl(sk, cmd, &karg);
	}
	/* A positive return value means that the ioctl was not processed */
	return 1;
}
#endif
