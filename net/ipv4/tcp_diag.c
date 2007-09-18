/*
 * tcp_diag.c	Module for monitoring TCP transport protocols sockets.
 *
 * Version:	$Id: tcp_diag.c,v 1.3 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/inet_diag.h>

#include <linux/tcp.h>

#include <net/tcp.h>

static void tcp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			      void *_info)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct tcp_info *info = _info;

	if (sk->sk_state == TCP_LISTEN) {
		r->idiag_rqueue = sk->sk_ack_backlog;
		r->idiag_wqueue = sk->sk_max_ack_backlog;
	} else {
		r->idiag_rqueue = tp->rcv_nxt - tp->copied_seq;
		r->idiag_wqueue = tp->write_seq - tp->snd_una;
	}
	if (info != NULL)
		tcp_get_info(sk, info);
}

static struct inet_diag_handler tcp_diag_handler = {
	.idiag_hashinfo	 = &tcp_hashinfo,
	.idiag_get_info	 = tcp_diag_get_info,
	.idiag_type	 = TCPDIAG_GETSOCK,
	.idiag_info_size = sizeof(struct tcp_info),
};

static int __init tcp_diag_init(void)
{
	return inet_diag_register(&tcp_diag_handler);
}

static void __exit tcp_diag_exit(void)
{
	inet_diag_unregister(&tcp_diag_handler);
}

module_init(tcp_diag_init);
module_exit(tcp_diag_exit);
MODULE_LICENSE("GPL");
