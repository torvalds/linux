/*
 * NET		Generic infrastructure for Network protocols.
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _TIMEWAIT_SOCK_H
#define _TIMEWAIT_SOCK_H

#include <linux/slab.h>
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
	BUG_ON(sk == NULL);
	BUG_ON(sk->sk_prot == NULL);
	BUG_ON(sk->sk_prot->twsk_prot == NULL);
	if (sk->sk_prot->twsk_prot->twsk_destructor != NULL)
		sk->sk_prot->twsk_prot->twsk_destructor(sk);
}

#endif /* _TIMEWAIT_SOCK_H */
