/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_ESPINTCP_H
#define _NET_ESPINTCP_H

#include <net/strparser.h>
#include <linux/skmsg.h>

void __init espintcp_init(void);

int espintcp_push_skb(struct sock *sk, struct sk_buff *skb);
int espintcp_queue_out(struct sock *sk, struct sk_buff *skb);
bool tcp_is_ulp_esp(struct sock *sk);

struct espintcp_msg {
	struct sk_buff *skb;
	struct sk_msg skmsg;
	int offset;
	int len;
};

struct espintcp_ctx {
	struct strparser strp;
	struct sk_buff_head ike_queue;
	struct sk_buff_head out_queue;
	struct espintcp_msg partial;
	void (*saved_data_ready)(struct sock *sk);
	void (*saved_write_space)(struct sock *sk);
	void (*saved_destruct)(struct sock *sk);
	struct work_struct work;
	bool tx_running;
};

static inline struct espintcp_ctx *espintcp_getctx(const struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	/* RCU is only needed for diag */
	return (__force void *)icsk->icsk_ulp_data;
}
#endif
