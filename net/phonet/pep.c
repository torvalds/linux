/*
 * File: pep.c
 *
 * Phonet pipe protocol end point socket
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author: Rémi Denis-Courmont <remi.denis-courmont@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/socket.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <asm/ioctls.h>

#include <linux/phonet.h>
#include <net/phonet/phonet.h>
#include <net/phonet/pep.h>
#include <net/phonet/gprs.h>

/* sk_state values:
 * TCP_CLOSE		sock not in use yet
 * TCP_CLOSE_WAIT	disconnected pipe
 * TCP_LISTEN		listening pipe endpoint
 * TCP_SYN_RECV		connected pipe in disabled state
 * TCP_ESTABLISHED	connected pipe in enabled state
 *
 * pep_sock locking:
 *  - sk_state, ackq, hlist: sock lock needed
 *  - listener: read only
 *  - pipe_handle: read only
 */

#define CREDITS_MAX	10
#define CREDITS_THR	7

static const struct sockaddr_pn pipe_srv = {
	.spn_family = AF_PHONET,
	.spn_resource = 0xD9, /* pipe service */
};

#define pep_sb_size(s) (((s) + 5) & ~3) /* 2-bytes head, 32-bits aligned */

/* Get the next TLV sub-block. */
static unsigned char *pep_get_sb(struct sk_buff *skb, u8 *ptype, u8 *plen,
					void *buf)
{
	void *data = NULL;
	struct {
		u8 sb_type;
		u8 sb_len;
	} *ph, h;
	int buflen = *plen;

	ph = skb_header_pointer(skb, 0, 2, &h);
	if (ph == NULL || ph->sb_len < 2 || !pskb_may_pull(skb, ph->sb_len))
		return NULL;
	ph->sb_len -= 2;
	*ptype = ph->sb_type;
	*plen = ph->sb_len;

	if (buflen > ph->sb_len)
		buflen = ph->sb_len;
	data = skb_header_pointer(skb, 2, buflen, buf);
	__skb_pull(skb, 2 + ph->sb_len);
	return data;
}

static int pep_reply(struct sock *sk, struct sk_buff *oskb,
			u8 code, const void *data, int len, gfp_t priority)
{
	const struct pnpipehdr *oph = pnp_hdr(oskb);
	struct pnpipehdr *ph;
	struct sk_buff *skb;

	skb = alloc_skb(MAX_PNPIPE_HEADER + len, priority);
	if (!skb)
		return -ENOMEM;
	skb_set_owner_w(skb, sk);

	skb_reserve(skb, MAX_PNPIPE_HEADER);
	__skb_put(skb, len);
	skb_copy_to_linear_data(skb, data, len);
	__skb_push(skb, sizeof(*ph));
	skb_reset_transport_header(skb);
	ph = pnp_hdr(skb);
	ph->utid = oph->utid;
	ph->message_id = oph->message_id + 1; /* REQ -> RESP */
	ph->pipe_handle = oph->pipe_handle;
	ph->error_code = code;

	return pn_skb_send(sk, skb, &pipe_srv);
}

#define PAD 0x00
static int pep_accept_conn(struct sock *sk, struct sk_buff *skb)
{
	static const u8 data[20] = {
		PAD, PAD, PAD, 2 /* sub-blocks */,
		PN_PIPE_SB_REQUIRED_FC_TX, pep_sb_size(5), 3, PAD,
			PN_MULTI_CREDIT_FLOW_CONTROL,
			PN_ONE_CREDIT_FLOW_CONTROL,
			PN_LEGACY_FLOW_CONTROL,
			PAD,
		PN_PIPE_SB_PREFERRED_FC_RX, pep_sb_size(5), 3, PAD,
			PN_MULTI_CREDIT_FLOW_CONTROL,
			PN_ONE_CREDIT_FLOW_CONTROL,
			PN_LEGACY_FLOW_CONTROL,
			PAD,
	};

	might_sleep();
	return pep_reply(sk, skb, PN_PIPE_NO_ERROR, data, sizeof(data),
				GFP_KERNEL);
}

static int pep_reject_conn(struct sock *sk, struct sk_buff *skb, u8 code)
{
	static const u8 data[4] = { PAD, PAD, PAD, 0 /* sub-blocks */ };
	WARN_ON(code == PN_PIPE_NO_ERROR);
	return pep_reply(sk, skb, code, data, sizeof(data), GFP_ATOMIC);
}

/* Control requests are not sent by the pipe service and have a specific
 * message format. */
static int pep_ctrlreq_error(struct sock *sk, struct sk_buff *oskb, u8 code,
				gfp_t priority)
{
	const struct pnpipehdr *oph = pnp_hdr(oskb);
	struct sk_buff *skb;
	struct pnpipehdr *ph;
	struct sockaddr_pn dst;

	skb = alloc_skb(MAX_PNPIPE_HEADER + 4, priority);
	if (!skb)
		return -ENOMEM;
	skb_set_owner_w(skb, sk);

	skb_reserve(skb, MAX_PHONET_HEADER);
	ph = (struct pnpipehdr *)skb_put(skb, sizeof(*ph) + 4);

	ph->utid = oph->utid;
	ph->message_id = PNS_PEP_CTRL_RESP;
	ph->pipe_handle = oph->pipe_handle;
	ph->data[0] = oph->data[1]; /* CTRL id */
	ph->data[1] = oph->data[0]; /* PEP type */
	ph->data[2] = code; /* error code, at an usual offset */
	ph->data[3] = PAD;
	ph->data[4] = PAD;

	pn_skb_get_src_sockaddr(oskb, &dst);
	return pn_skb_send(sk, skb, &dst);
}

static int pipe_snd_status(struct sock *sk, u8 type, u8 status, gfp_t priority)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *ph;
	struct sk_buff *skb;

	skb = alloc_skb(MAX_PNPIPE_HEADER + 4, priority);
	if (!skb)
		return -ENOMEM;
	skb_set_owner_w(skb, sk);

	skb_reserve(skb, MAX_PNPIPE_HEADER + 4);
	__skb_push(skb, sizeof(*ph) + 4);
	skb_reset_transport_header(skb);
	ph = pnp_hdr(skb);
	ph->utid = 0;
	ph->message_id = PNS_PEP_STATUS_IND;
	ph->pipe_handle = pn->pipe_handle;
	ph->pep_type = PN_PEP_TYPE_COMMON;
	ph->data[1] = type;
	ph->data[2] = PAD;
	ph->data[3] = PAD;
	ph->data[4] = status;

	return pn_skb_send(sk, skb, &pipe_srv);
}

/* Send our RX flow control information to the sender.
 * Socket must be locked. */
static void pipe_grant_credits(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	BUG_ON(sk->sk_state != TCP_ESTABLISHED);

	switch (pn->rx_fc) {
	case PN_LEGACY_FLOW_CONTROL: /* TODO */
		break;
	case PN_ONE_CREDIT_FLOW_CONTROL:
		pipe_snd_status(sk, PN_PEP_IND_FLOW_CONTROL,
				PEP_IND_READY, GFP_ATOMIC);
		pn->rx_credits = 1;
		break;
	case PN_MULTI_CREDIT_FLOW_CONTROL:
		if ((pn->rx_credits + CREDITS_THR) > CREDITS_MAX)
			break;
		if (pipe_snd_status(sk, PN_PEP_IND_ID_MCFC_GRANT_CREDITS,
					CREDITS_MAX - pn->rx_credits,
					GFP_ATOMIC) == 0)
			pn->rx_credits = CREDITS_MAX;
		break;
	}
}

static int pipe_rcv_status(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *hdr = pnp_hdr(skb);
	int wake = 0;

	if (!pskb_may_pull(skb, sizeof(*hdr) + 4))
		return -EINVAL;

	if (hdr->data[0] != PN_PEP_TYPE_COMMON) {
		LIMIT_NETDEBUG(KERN_DEBUG"Phonet unknown PEP type: %u\n",
				(unsigned)hdr->data[0]);
		return -EOPNOTSUPP;
	}

	switch (hdr->data[1]) {
	case PN_PEP_IND_FLOW_CONTROL:
		switch (pn->tx_fc) {
		case PN_LEGACY_FLOW_CONTROL:
			switch (hdr->data[4]) {
			case PEP_IND_BUSY:
				atomic_set(&pn->tx_credits, 0);
				break;
			case PEP_IND_READY:
				atomic_set(&pn->tx_credits, wake = 1);
				break;
			}
			break;
		case PN_ONE_CREDIT_FLOW_CONTROL:
			if (hdr->data[4] == PEP_IND_READY)
				atomic_set(&pn->tx_credits, wake = 1);
			break;
		}
		break;

	case PN_PEP_IND_ID_MCFC_GRANT_CREDITS:
		if (pn->tx_fc != PN_MULTI_CREDIT_FLOW_CONTROL)
			break;
		atomic_add(wake = hdr->data[4], &pn->tx_credits);
		break;

	default:
		LIMIT_NETDEBUG(KERN_DEBUG"Phonet unknown PEP indication: %u\n",
				(unsigned)hdr->data[1]);
		return -EOPNOTSUPP;
	}
	if (wake)
		sk->sk_write_space(sk);
	return 0;
}

static int pipe_rcv_created(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *hdr = pnp_hdr(skb);
	u8 n_sb = hdr->data[0];

	pn->rx_fc = pn->tx_fc = PN_LEGACY_FLOW_CONTROL;
	__skb_pull(skb, sizeof(*hdr));
	while (n_sb > 0) {
		u8 type, buf[2], len = sizeof(buf);
		u8 *data = pep_get_sb(skb, &type, &len, buf);

		if (data == NULL)
			return -EINVAL;
		switch (type) {
		case PN_PIPE_SB_NEGOTIATED_FC:
			if (len < 2 || (data[0] | data[1]) > 3)
				break;
			pn->tx_fc = data[0] & 3;
			pn->rx_fc = data[1] & 3;
			break;
		}
		n_sb--;
	}
	return 0;
}

/* Queue an skb to a connected sock.
 * Socket lock must be held. */
static int pipe_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *hdr = pnp_hdr(skb);
	struct sk_buff_head *queue;
	int err = 0;

	BUG_ON(sk->sk_state == TCP_CLOSE_WAIT);

	switch (hdr->message_id) {
	case PNS_PEP_CONNECT_REQ:
		pep_reject_conn(sk, skb, PN_PIPE_ERR_PEP_IN_USE);
		break;

	case PNS_PEP_DISCONNECT_REQ:
		pep_reply(sk, skb, PN_PIPE_NO_ERROR, NULL, 0, GFP_ATOMIC);
		sk->sk_state = TCP_CLOSE_WAIT;
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_state_change(sk);
		break;

	case PNS_PEP_ENABLE_REQ:
		/* Wait for PNS_PIPE_(ENABLED|REDIRECTED)_IND */
		pep_reply(sk, skb, PN_PIPE_NO_ERROR, NULL, 0, GFP_ATOMIC);
		break;

	case PNS_PEP_RESET_REQ:
		switch (hdr->state_after_reset) {
		case PN_PIPE_DISABLE:
			pn->init_enable = 0;
			break;
		case PN_PIPE_ENABLE:
			pn->init_enable = 1;
			break;
		default: /* not allowed to send an error here!? */
			err = -EINVAL;
			goto out;
		}
		/* fall through */
	case PNS_PEP_DISABLE_REQ:
		atomic_set(&pn->tx_credits, 0);
		pep_reply(sk, skb, PN_PIPE_NO_ERROR, NULL, 0, GFP_ATOMIC);
		break;

	case PNS_PEP_CTRL_REQ:
		if (skb_queue_len(&pn->ctrlreq_queue) >= PNPIPE_CTRLREQ_MAX) {
			atomic_inc(&sk->sk_drops);
			break;
		}
		__skb_pull(skb, 4);
		queue = &pn->ctrlreq_queue;
		goto queue;

	case PNS_PIPE_DATA:
		__skb_pull(skb, 3); /* Pipe data header */
		if (!pn_flow_safe(pn->rx_fc)) {
			err = sock_queue_rcv_skb(sk, skb);
			if (!err)
				return 0;
			if (err == -ENOMEM)
				atomic_inc(&sk->sk_drops);
			break;
		}

		if (pn->rx_credits == 0) {
			atomic_inc(&sk->sk_drops);
			err = -ENOBUFS;
			break;
		}
		pn->rx_credits--;
		queue = &sk->sk_receive_queue;
		goto queue;

	case PNS_PEP_STATUS_IND:
		pipe_rcv_status(sk, skb);
		break;

	case PNS_PIPE_REDIRECTED_IND:
		err = pipe_rcv_created(sk, skb);
		break;

	case PNS_PIPE_CREATED_IND:
		err = pipe_rcv_created(sk, skb);
		if (err)
			break;
		/* fall through */
	case PNS_PIPE_RESET_IND:
		if (!pn->init_enable)
			break;
		/* fall through */
	case PNS_PIPE_ENABLED_IND:
		if (!pn_flow_safe(pn->tx_fc)) {
			atomic_set(&pn->tx_credits, 1);
			sk->sk_write_space(sk);
		}
		if (sk->sk_state == TCP_ESTABLISHED)
			break; /* Nothing to do */
		sk->sk_state = TCP_ESTABLISHED;
		pipe_grant_credits(sk);
		break;

	case PNS_PIPE_DISABLED_IND:
		sk->sk_state = TCP_SYN_RECV;
		pn->rx_credits = 0;
		break;

	default:
		LIMIT_NETDEBUG(KERN_DEBUG"Phonet unknown PEP message: %u\n",
				hdr->message_id);
		err = -EINVAL;
	}
out:
	kfree_skb(skb);
	return err;

queue:
	skb->dev = NULL;
	skb_set_owner_r(skb, sk);
	err = skb->len;
	skb_queue_tail(queue, skb);
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, err);
	return 0;
}

/* Destroy connected sock. */
static void pipe_destruct(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&pn->ctrlreq_queue);
}

static int pep_connreq_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct sock *newsk;
	struct pep_sock *newpn, *pn = pep_sk(sk);
	struct pnpipehdr *hdr;
	struct sockaddr_pn dst;
	u16 peer_type;
	u8 pipe_handle, enabled, n_sb;

	if (!pskb_pull(skb, sizeof(*hdr) + 4))
		return -EINVAL;

	hdr = pnp_hdr(skb);
	pipe_handle = hdr->pipe_handle;
	switch (hdr->state_after_connect) {
	case PN_PIPE_DISABLE:
		enabled = 0;
		break;
	case PN_PIPE_ENABLE:
		enabled = 1;
		break;
	default:
		pep_reject_conn(sk, skb, PN_PIPE_ERR_INVALID_PARAM);
		return -EINVAL;
	}
	peer_type = hdr->other_pep_type << 8;

	if (unlikely(sk->sk_state != TCP_LISTEN) || sk_acceptq_is_full(sk)) {
		pep_reject_conn(sk, skb, PN_PIPE_ERR_PEP_IN_USE);
		return -ENOBUFS;
	}

	/* Parse sub-blocks (options) */
	n_sb = hdr->data[4];
	while (n_sb > 0) {
		u8 type, buf[1], len = sizeof(buf);
		const u8 *data = pep_get_sb(skb, &type, &len, buf);

		if (data == NULL)
			return -EINVAL;
		switch (type) {
		case PN_PIPE_SB_CONNECT_REQ_PEP_SUB_TYPE:
			if (len < 1)
				return -EINVAL;
			peer_type = (peer_type & 0xff00) | data[0];
			break;
		}
		n_sb--;
	}

	skb = skb_clone(skb, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	/* Create a new to-be-accepted sock */
	newsk = sk_alloc(sock_net(sk), PF_PHONET, GFP_ATOMIC, sk->sk_prot);
	if (!newsk) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	sock_init_data(NULL, newsk);
	newsk->sk_state = TCP_SYN_RECV;
	newsk->sk_backlog_rcv = pipe_do_rcv;
	newsk->sk_protocol = sk->sk_protocol;
	newsk->sk_destruct = pipe_destruct;

	newpn = pep_sk(newsk);
	pn_skb_get_dst_sockaddr(skb, &dst);
	newpn->pn_sk.sobject = pn_sockaddr_get_object(&dst);
	newpn->pn_sk.resource = pn->pn_sk.resource;
	skb_queue_head_init(&newpn->ctrlreq_queue);
	newpn->pipe_handle = pipe_handle;
	atomic_set(&newpn->tx_credits, 0);
	newpn->peer_type = peer_type;
	newpn->rx_credits = 0;
	newpn->rx_fc = newpn->tx_fc = PN_LEGACY_FLOW_CONTROL;
	newpn->init_enable = enabled;

	BUG_ON(!skb_queue_empty(&newsk->sk_receive_queue));
	skb_queue_head(&newsk->sk_receive_queue, skb);
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, 0);

	sk_acceptq_added(sk);
	sk_add_node(newsk, &pn->ackq);
	return 0;
}

/* Listening sock must be locked */
static struct sock *pep_find_pipe(const struct hlist_head *hlist,
					const struct sockaddr_pn *dst,
					u8 pipe_handle)
{
	struct hlist_node *node;
	struct sock *sknode;
	u16 dobj = pn_sockaddr_get_object(dst);

	sk_for_each(sknode, node, hlist) {
		struct pep_sock *pnnode = pep_sk(sknode);

		/* Ports match, but addresses might not: */
		if (pnnode->pn_sk.sobject != dobj)
			continue;
		if (pnnode->pipe_handle != pipe_handle)
			continue;
		if (sknode->sk_state == TCP_CLOSE_WAIT)
			continue;

		sock_hold(sknode);
		return sknode;
	}
	return NULL;
}

/*
 * Deliver an skb to a listening sock.
 * Socket lock must be held.
 * We then queue the skb to the right connected sock (if any).
 */
static int pep_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct sock *sknode;
	struct pnpipehdr *hdr;
	struct sockaddr_pn dst;
	int err = NET_RX_SUCCESS;
	u8 pipe_handle;

	if (!pskb_may_pull(skb, sizeof(*hdr)))
		goto drop;

	hdr = pnp_hdr(skb);
	pipe_handle = hdr->pipe_handle;
	if (pipe_handle == PN_PIPE_INVALID_HANDLE)
		goto drop;

	pn_skb_get_dst_sockaddr(skb, &dst);

	/* Look for an existing pipe handle */
	sknode = pep_find_pipe(&pn->hlist, &dst, pipe_handle);
	if (sknode)
		return sk_receive_skb(sknode, skb, 1);

	/* Look for a pipe handle pending accept */
	sknode = pep_find_pipe(&pn->ackq, &dst, pipe_handle);
	if (sknode) {
		sock_put(sknode);
		if (net_ratelimit())
			printk(KERN_WARNING"Phonet unconnected PEP ignored");
		err = NET_RX_DROP;
		goto drop;
	}

	switch (hdr->message_id) {
	case PNS_PEP_CONNECT_REQ:
		err = pep_connreq_rcv(sk, skb);
		break;

	case PNS_PEP_DISCONNECT_REQ:
		pep_reply(sk, skb, PN_PIPE_NO_ERROR, NULL, 0, GFP_ATOMIC);
		break;

	case PNS_PEP_CTRL_REQ:
		pep_ctrlreq_error(sk, skb, PN_PIPE_INVALID_HANDLE, GFP_ATOMIC);
		break;

	case PNS_PEP_RESET_REQ:
	case PNS_PEP_ENABLE_REQ:
	case PNS_PEP_DISABLE_REQ:
		/* invalid handle is not even allowed here! */
	default:
		err = NET_RX_DROP;
	}
drop:
	kfree_skb(skb);
	return err;
}

/* associated socket ceases to exist */
static void pep_sock_close(struct sock *sk, long timeout)
{
	struct pep_sock *pn = pep_sk(sk);
	int ifindex = 0;

	sk_common_release(sk);

	lock_sock(sk);
	if (sk->sk_state == TCP_LISTEN) {
		/* Destroy the listen queue */
		struct sock *sknode;
		struct hlist_node *p, *n;

		sk_for_each_safe(sknode, p, n, &pn->ackq)
			sk_del_node_init(sknode);
		sk->sk_state = TCP_CLOSE;
	}
	ifindex = pn->ifindex;
	pn->ifindex = 0;
	release_sock(sk);

	if (ifindex)
		gprs_detach(sk);
}

static int pep_wait_connreq(struct sock *sk, int noblock)
{
	struct task_struct *tsk = current;
	struct pep_sock *pn = pep_sk(sk);
	long timeo = sock_rcvtimeo(sk, noblock);

	for (;;) {
		DEFINE_WAIT(wait);

		if (sk->sk_state != TCP_LISTEN)
			return -EINVAL;
		if (!hlist_empty(&pn->ackq))
			break;
		if (!timeo)
			return -EWOULDBLOCK;
		if (signal_pending(tsk))
			return sock_intr_errno(timeo);

		prepare_to_wait_exclusive(&sk->sk_socket->wait, &wait,
						TASK_INTERRUPTIBLE);
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		finish_wait(&sk->sk_socket->wait, &wait);
	}

	return 0;
}

static struct sock *pep_sock_accept(struct sock *sk, int flags, int *errp)
{
	struct pep_sock *pn = pep_sk(sk);
	struct sock *newsk = NULL;
	struct sk_buff *oskb;
	int err;

	lock_sock(sk);
	err = pep_wait_connreq(sk, flags & O_NONBLOCK);
	if (err)
		goto out;

	newsk = __sk_head(&pn->ackq);

	oskb = skb_dequeue(&newsk->sk_receive_queue);
	err = pep_accept_conn(newsk, oskb);
	if (err) {
		skb_queue_head(&newsk->sk_receive_queue, oskb);
		newsk = NULL;
		goto out;
	}

	sock_hold(sk);
	pep_sk(newsk)->listener = sk;

	sock_hold(newsk);
	sk_del_node_init(newsk);
	sk_acceptq_removed(sk);
	sk_add_node(newsk, &pn->hlist);
	__sock_put(newsk);

out:
	release_sock(sk);
	*errp = err;
	return newsk;
}

static int pep_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	struct pep_sock *pn = pep_sk(sk);
	int answ;

	switch (cmd) {
	case SIOCINQ:
		if (sk->sk_state == TCP_LISTEN)
			return -EINVAL;

		lock_sock(sk);
		if (sock_flag(sk, SOCK_URGINLINE)
		 && !skb_queue_empty(&pn->ctrlreq_queue))
			answ = skb_peek(&pn->ctrlreq_queue)->len;
		else if (!skb_queue_empty(&sk->sk_receive_queue))
			answ = skb_peek(&sk->sk_receive_queue)->len;
		else
			answ = 0;
		release_sock(sk);
		return put_user(answ, (int __user *)arg);
	}

	return -ENOIOCTLCMD;
}

static int pep_init(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	INIT_HLIST_HEAD(&pn->ackq);
	INIT_HLIST_HEAD(&pn->hlist);
	skb_queue_head_init(&pn->ctrlreq_queue);
	pn->pipe_handle = PN_PIPE_INVALID_HANDLE;
	return 0;
}

static int pep_setsockopt(struct sock *sk, int level, int optname,
				char __user *optval, unsigned int optlen)
{
	struct pep_sock *pn = pep_sk(sk);
	int val = 0, err = 0;

	if (level != SOL_PNPIPE)
		return -ENOPROTOOPT;
	if (optlen >= sizeof(int)) {
		if (get_user(val, (int __user *) optval))
			return -EFAULT;
	}

	lock_sock(sk);
	switch (optname) {
	case PNPIPE_ENCAP:
		if (val && val != PNPIPE_ENCAP_IP) {
			err = -EINVAL;
			break;
		}
		if (!pn->ifindex == !val)
			break; /* Nothing to do! */
		if (!capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}
		if (val) {
			release_sock(sk);
			err = gprs_attach(sk);
			if (err > 0) {
				pn->ifindex = err;
				err = 0;
			}
		} else {
			pn->ifindex = 0;
			release_sock(sk);
			gprs_detach(sk);
			err = 0;
		}
		goto out_norel;
	default:
		err = -ENOPROTOOPT;
	}
	release_sock(sk);

out_norel:
	return err;
}

static int pep_getsockopt(struct sock *sk, int level, int optname,
				char __user *optval, int __user *optlen)
{
	struct pep_sock *pn = pep_sk(sk);
	int len, val;

	if (level != SOL_PNPIPE)
		return -ENOPROTOOPT;
	if (get_user(len, optlen))
		return -EFAULT;

	switch (optname) {
	case PNPIPE_ENCAP:
		val = pn->ifindex ? PNPIPE_ENCAP_IP : PNPIPE_ENCAP_NONE;
		break;
	case PNPIPE_IFINDEX:
		val = pn->ifindex;
		break;
	default:
		return -ENOPROTOOPT;
	}

	len = min_t(unsigned int, sizeof(int), len);
	if (put_user(len, optlen))
		return -EFAULT;
	if (put_user(val, (int __user *) optval))
		return -EFAULT;
	return 0;
}

static int pipe_skb_send(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *ph;

	if (pn_flow_safe(pn->tx_fc) &&
	    !atomic_add_unless(&pn->tx_credits, -1, 0)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	skb_push(skb, 3);
	skb_reset_transport_header(skb);
	ph = pnp_hdr(skb);
	ph->utid = 0;
	ph->message_id = PNS_PIPE_DATA;
	ph->pipe_handle = pn->pipe_handle;

	return pn_skb_send(sk, skb, &pipe_srv);
}

static int pep_sendmsg(struct kiocb *iocb, struct sock *sk,
			struct msghdr *msg, size_t len)
{
	struct pep_sock *pn = pep_sk(sk);
	struct sk_buff *skb = NULL;
	long timeo;
	int flags = msg->msg_flags;
	int err, done;

	if (msg->msg_flags & MSG_OOB || !(msg->msg_flags & MSG_EOR))
		return -EOPNOTSUPP;

	lock_sock(sk);
	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	if ((1 << sk->sk_state) & (TCPF_LISTEN|TCPF_CLOSE)) {
		err = -ENOTCONN;
		goto out;
	}
	if (sk->sk_state != TCP_ESTABLISHED) {
		/* Wait until the pipe gets to enabled state */
disabled:
		err = sk_stream_wait_connect(sk, &timeo);
		if (err)
			goto out;

		if (sk->sk_state == TCP_CLOSE_WAIT) {
			err = -ECONNRESET;
			goto out;
		}
	}
	BUG_ON(sk->sk_state != TCP_ESTABLISHED);

	/* Wait until flow control allows TX */
	done = atomic_read(&pn->tx_credits);
	while (!done) {
		DEFINE_WAIT(wait);

		if (!timeo) {
			err = -EAGAIN;
			goto out;
		}
		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			goto out;
		}

		prepare_to_wait(&sk->sk_socket->wait, &wait,
				TASK_INTERRUPTIBLE);
		done = sk_wait_event(sk, &timeo, atomic_read(&pn->tx_credits));
		finish_wait(&sk->sk_socket->wait, &wait);

		if (sk->sk_state != TCP_ESTABLISHED)
			goto disabled;
	}

	if (!skb) {
		skb = sock_alloc_send_skb(sk, MAX_PNPIPE_HEADER + len,
						flags & MSG_DONTWAIT, &err);
		if (skb == NULL)
			goto out;
		skb_reserve(skb, MAX_PHONET_HEADER + 3);

		if (sk->sk_state != TCP_ESTABLISHED ||
		    !atomic_read(&pn->tx_credits))
			goto disabled; /* sock_alloc_send_skb might sleep */
	}

	err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
	if (err < 0)
		goto out;

	err = pipe_skb_send(sk, skb);
	if (err >= 0)
		err = len; /* success! */
	skb = NULL;
out:
	release_sock(sk);
	kfree_skb(skb);
	return err;
}

int pep_writeable(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	return atomic_read(&pn->tx_credits);
}

int pep_write(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *rskb, *fs;
	int flen = 0;

	rskb = alloc_skb(MAX_PNPIPE_HEADER, GFP_ATOMIC);
	if (!rskb) {
		kfree_skb(skb);
		return -ENOMEM;
	}
	skb_shinfo(rskb)->frag_list = skb;
	rskb->len += skb->len;
	rskb->data_len += rskb->len;
	rskb->truesize += rskb->len;

	/* Avoid nested fragments */
	skb_walk_frags(skb, fs)
		flen += fs->len;
	skb->next = skb_shinfo(skb)->frag_list;
	skb_frag_list_init(skb);
	skb->len -= flen;
	skb->data_len -= flen;
	skb->truesize -= flen;

	skb_reserve(rskb, MAX_PHONET_HEADER + 3);
	return pipe_skb_send(sk, rskb);
}

struct sk_buff *pep_read(struct sock *sk)
{
	struct sk_buff *skb = skb_dequeue(&sk->sk_receive_queue);

	if (sk->sk_state == TCP_ESTABLISHED)
		pipe_grant_credits(sk);
	return skb;
}

static int pep_recvmsg(struct kiocb *iocb, struct sock *sk,
			struct msghdr *msg, size_t len, int noblock,
			int flags, int *addr_len)
{
	struct sk_buff *skb;
	int err;

	if (unlikely(1 << sk->sk_state & (TCPF_LISTEN | TCPF_CLOSE)))
		return -ENOTCONN;

	if ((flags & MSG_OOB) || sock_flag(sk, SOCK_URGINLINE)) {
		/* Dequeue and acknowledge control request */
		struct pep_sock *pn = pep_sk(sk);

		skb = skb_dequeue(&pn->ctrlreq_queue);
		if (skb) {
			pep_ctrlreq_error(sk, skb, PN_PIPE_NO_ERROR,
						GFP_KERNEL);
			msg->msg_flags |= MSG_OOB;
			goto copy;
		}
		if (flags & MSG_OOB)
			return -EINVAL;
	}

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	lock_sock(sk);
	if (skb == NULL) {
		if (err == -ENOTCONN && sk->sk_state == TCP_CLOSE_WAIT)
			err = -ECONNRESET;
		release_sock(sk);
		return err;
	}

	if (sk->sk_state == TCP_ESTABLISHED)
		pipe_grant_credits(sk);
	release_sock(sk);
copy:
	msg->msg_flags |= MSG_EOR;
	if (skb->len > len)
		msg->msg_flags |= MSG_TRUNC;
	else
		len = skb->len;

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, len);
	if (!err)
		err = (flags & MSG_TRUNC) ? skb->len : len;

	skb_free_datagram(sk, skb);
	return err;
}

static void pep_sock_unhash(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);
	struct sock *skparent = NULL;

	lock_sock(sk);
	if ((1 << sk->sk_state) & ~(TCPF_CLOSE|TCPF_LISTEN)) {
		skparent = pn->listener;
		sk_del_node_init(sk);
		release_sock(sk);

		sk = skparent;
		pn = pep_sk(skparent);
		lock_sock(sk);
	}
	/* Unhash a listening sock only when it is closed
	 * and all of its active connected pipes are closed. */
	if (hlist_empty(&pn->hlist))
		pn_sock_unhash(&pn->pn_sk.sk);
	release_sock(sk);

	if (skparent)
		sock_put(skparent);
}

static struct proto pep_proto = {
	.close		= pep_sock_close,
	.accept		= pep_sock_accept,
	.ioctl		= pep_ioctl,
	.init		= pep_init,
	.setsockopt	= pep_setsockopt,
	.getsockopt	= pep_getsockopt,
	.sendmsg	= pep_sendmsg,
	.recvmsg	= pep_recvmsg,
	.backlog_rcv	= pep_do_rcv,
	.hash		= pn_sock_hash,
	.unhash		= pep_sock_unhash,
	.get_port	= pn_sock_get_port,
	.obj_size	= sizeof(struct pep_sock),
	.owner		= THIS_MODULE,
	.name		= "PNPIPE",
};

static struct phonet_protocol pep_pn_proto = {
	.ops		= &phonet_stream_ops,
	.prot		= &pep_proto,
	.sock_type	= SOCK_SEQPACKET,
};

static int __init pep_register(void)
{
	return phonet_proto_register(PN_PROTO_PIPE, &pep_pn_proto);
}

static void __exit pep_unregister(void)
{
	phonet_proto_unregister(PN_PROTO_PIPE, &pep_pn_proto);
}

module_init(pep_register);
module_exit(pep_unregister);
MODULE_AUTHOR("Remi Denis-Courmont, Nokia");
MODULE_DESCRIPTION("Phonet pipe protocol");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_PHONET, PN_PROTO_PIPE);
