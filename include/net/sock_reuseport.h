#ifndef _SOCK_REUSEPORT_H
#define _SOCK_REUSEPORT_H

#include <linux/types.h>
#include <net/sock.h>

struct sock_reuseport {
	struct rcu_head		rcu;

	u16			max_socks;	/* length of socks */
	u16			num_socks;	/* elements in socks */
	struct sock		*socks[0];	/* array of sock pointers */
};

extern int reuseport_alloc(struct sock *sk);
extern int reuseport_add_sock(struct sock *sk, const struct sock *sk2);
extern void reuseport_detach_sock(struct sock *sk);
extern struct sock *reuseport_select_sock(struct sock *sk, u32 hash);

#endif  /* _SOCK_REUSEPORT_H */
