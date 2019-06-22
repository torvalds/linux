/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NET		Generic infrastructure for Network protocols.
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */
#ifndef _TIMEWAIT_SOCK_H
#define _TIMEWAIT_SOCK_H

#include <linux/slab.h>
#include <linux/bug.h>
#include <net/sock.h>

struct timewait_sock_ops {
	struct kmem_cache	*twsk_slab;
	char		*twsk_slab_name;
	unsigned int	twsk_obj_size;
	int		(*twsk_unique)(struct sock *sk,
				       struct sock *sktw, void *twp);
	void		(*twsk_destructor)(struct sock *sk);
};

static inline int twsk_unique(struct sock *sk, struct sock *sktw, void *twp)
{
	if (sk->sk_prot->twsk_prot->twsk_unique != NULL)
		return sk->sk_prot->twsk_prot->twsk_unique(sk, sktw, twp);
	return 0;
}

static inline void twsk_destructor(struct sock *sk)
{
	if (sk->sk_prot->twsk_prot->twsk_destructor != NULL)
		sk->sk_prot->twsk_prot->twsk_destructor(sk);
}

#endif /* _TIMEWAIT_SOCK_H */
