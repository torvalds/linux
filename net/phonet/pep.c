// SPDX-License-Identifier: GPL-2.0-only
/*
 * File: pep.c
 *
 * Phonet pipe protocol end point socket
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author: Rémi Denis-Courmont
 */

#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <asm/ioctls.h>

#include <linux/phonet.h>
#include <linux/module.h>
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
 *  - sk_state, hlist: sock lock needed
 *  - listener: read only
 *  - pipe_handle: read only
 */

#define CREDITS_MAX	10
#define CREDITS_THR	7

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

static struct sk_buff *pep_alloc_skb(struct sock *sk, const void *payload,
					int len, gfp_t priority)
{
	struct sk_buff *skb = alloc_skb(MAX_PNPIPE_HEADER + len, priority);
	if (!skb)
		return NULL;
	skb_set_owner_w(skb, sk);

	skb_reserve(skb, MAX_PNPIPE_HEADER);
	__skb_put(skb, len);
	skb_copy_to_linear_data(skb, payload, len);
	__skb_push(skb, sizeof(struct pnpipehdr));
	skb_reset_transport_header(skb);
	return skb;
}

static int pep_reply(struct sock *sk, struct sk_buff *oskb, u8 code,
			const void *data, int len, gfp_t priority)
{
	const struct pnpipehdr *oph = pnp_hdr(oskb);
	struct pnpipehdr *ph;
	struct sk_buff *skb;
	struct sockaddr_pn peer;

	skb = pep_alloc_skb(sk, data, len, priority);
	if (!skb)
		return -ENOMEM;

	ph = pnp_hdr(skb);
	ph->utid = oph->utid;
	ph->message_id = oph->message_id + 1; /* REQ -> RESP */
	ph->pipe_handle = oph->pipe_handle;
	ph->error_code = code;

	pn_skb_get_src_sockaddr(oskb, &peer);
	return pn_skb_send(sk, skb, &peer);
}

static int pep_indicate(struct sock *sk, u8 id, u8 code,
			const void *data, int len, gfp_t priority)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *ph;
	struct sk_buff *skb;

	skb = pep_alloc_skb(sk, data, len, priority);
	if (!skb)
		return -ENOMEM;

	ph = pnp_hdr(skb);
	ph->utid = 0;
	ph->message_id = id;
	ph->pipe_handle = pn->pipe_handle;
	ph->error_code = code;
	return pn_skb_send(sk, skb, NULL);
}

#define PAD 0x00

static int pipe_handler_request(struct sock *sk, u8 id, u8 code,
				const void *data, int len)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *ph;
	struct sk_buff *skb;

	skb = pep_alloc_skb(sk, data, len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	ph = pnp_hdr(skb);
	ph->utid = id; /* whatever */
	ph->message_id = id;
	ph->pipe_handle = pn->pipe_handle;
	ph->error_code = code;
	return pn_skb_send(sk, skb, NULL);
}

static int pipe_handler_send_created_ind(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);
	u8 data[4] = {
		PN_PIPE_SB_NEGOTIATED_FC, pep_sb_size(2),
		pn->tx_fc, pn->rx_fc,
	};

	return pep_indicate(sk, PNS_PIPE_CREATED_IND, 1 /* sub-blocks */,
				data, 4, GFP_ATOMIC);
}

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

static int pep_reject_conn(struct sock *sk, struct sk_buff *skb, u8 code,
				gfp_t priority)
{
	static const u8 data[4] = { PAD, PAD, PAD, 0 /* sub-blocks */ };
	WARN_ON(code == PN_PIPE_NO_ERROR);
	return pep_reply(sk, skb, code, data, sizeof(data), priority);
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
	u8 data[4] = {
		oph->pep_type, /* PEP type */
		code, /* error code, at an unusual offset */
		PAD, PAD,
	};

	skb = pep_alloc_skb(sk, data, 4, priority);
	if (!skb)
		return -ENOMEM;

	ph = pnp_hdr(skb);
	ph->utid = oph->utid;
	ph->message_id = PNS_PEP_CTRL_RESP;
	ph->pipe_handle = oph->pipe_handle;
	ph->data0 = oph->data[0]; /* CTRL id */

	pn_skb_get_src_sockaddr(oskb, &dst);
	return pn_skb_send(sk, skb, &dst);
}

static int pipe_snd_status(struct sock *sk, u8 type, u8 status, gfp_t priority)
{
	u8 data[4] = { type, PAD, PAD, status };

	return pep_indicate(sk, PNS_PEP_STATUS_IND, PN_PEP_TYPE_COMMON,
				data, 4, priority);
}

/* Send our RX flow control information to the sender.
 * Socket must be locked. */
static void pipe_grant_credits(struct sock *sk, gfp_t priority)
{
	struct pep_sock *pn = pep_sk(sk);

	BUG_ON(sk->sk_state != TCP_ESTABLISHED);

	switch (pn->rx_fc) {
	case PN_LEGACY_FLOW_CONTROL: /* TODO */
		break;
	case PN_ONE_CREDIT_FLOW_CONTROL:
		if (pipe_snd_status(sk, PN_PEP_IND_FLOW_CONTROL,
					PEP_IND_READY, priority) == 0)
			pn->rx_credits = 1;
		break;
	case PN_MULTI_CREDIT_FLOW_CONTROL:
		if ((pn->rx_credits + CREDITS_THR) > CREDITS_MAX)
			break;
		if (pipe_snd_status(sk, PN_PEP_IND_ID_MCFC_GRANT_CREDITS,
					CREDITS_MAX - pn->rx_credits,
					priority) == 0)
			pn->rx_credits = CREDITS_MAX;
		break;
	}
}

static int pipe_rcv_status(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *hdr;
	int wake = 0;

	if (!pskb_may_pull(skb, sizeof(*hdr) + 4))
		return -EINVAL;

	hdr = pnp_hdr(skb);
	if (hdr->pep_type != PN_PEP_TYPE_COMMON) {
		net_dbg_ratelimited("Phonet unknown PEP type: %u\n",
				    (unsigned int)hdr->pep_type);
		return -EOPNOTSUPP;
	}

	switch (hdr->data[0]) {
	case PN_PEP_IND_FLOW_CONTROL:
		switch (pn->tx_fc) {
		case PN_LEGACY_FLOW_CONTROL:
			switch (hdr->data[3]) {
			case PEP_IND_BUSY:
				atomic_set(&pn->tx_credits, 0);
				break;
			case PEP_IND_READY:
				atomic_set(&pn->tx_credits, wake = 1);
				break;
			}
			break;
		case PN_ONE_CREDIT_FLOW_CONTROL:
			if (hdr->data[3] == PEP_IND_READY)
				atomic_set(&pn->tx_credits, wake = 1);
			break;
		}
		break;

	case PN_PEP_IND_ID_MCFC_GRANT_CREDITS:
		if (pn->tx_fc != PN_MULTI_CREDIT_FLOW_CONTROL)
			break;
		atomic_add(wake = hdr->data[3], &pn->tx_credits);
		break;

	default:
		net_dbg_ratelimited("Phonet unknown PEP indication: %u\n",
				    (unsigned int)hdr->data[0]);
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
	u8 n_sb = hdr->data0;

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
		pep_reject_conn(sk, skb, PN_PIPE_ERR_PEP_IN_USE, GFP_ATOMIC);
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
		fallthrough;
	case PNS_PEP_DISABLE_REQ:
		atomic_set(&pn->tx_credits, 0);
		pep_reply(sk, skb, PN_PIPE_NO_ERROR, NULL, 0, GFP_ATOMIC);
		break;

	case PNS_PEP_CTRL_REQ:
		if (skb_queue_len(&pn->ctrlreq_queue) >= PNPIPE_CTRLREQ_MAX) {
			sk_drops_inc(sk);
			break;
		}
		__skb_pull(skb, 4);
		queue = &pn->ctrlreq_queue;
		goto queue;

	case PNS_PIPE_ALIGNED_DATA:
		__skb_pull(skb, 1);
		fallthrough;
	case PNS_PIPE_DATA:
		__skb_pull(skb, 3); /* Pipe data header */
		if (!pn_flow_safe(pn->rx_fc)) {
			err = sock_queue_rcv_skb(sk, skb);
			if (!err)
				return NET_RX_SUCCESS;
			err = -ENOBUFS;
			break;
		}

		if (pn->rx_credits == 0) {
			sk_drops_inc(sk);
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
		fallthrough;
	case PNS_PIPE_RESET_IND:
		if (!pn->init_enable)
			break;
		fallthrough;
	case PNS_PIPE_ENABLED_IND:
		if (!pn_flow_safe(pn->tx_fc)) {
			atomic_set(&pn->tx_credits, 1);
			sk->sk_write_space(sk);
		}
		if (sk->sk_state == TCP_ESTABLISHED)
			break; /* Nothing to do */
		sk->sk_state = TCP_ESTABLISHED;
		pipe_grant_credits(sk, GFP_ATOMIC);
		break;

	case PNS_PIPE_DISABLED_IND:
		sk->sk_state = TCP_SYN_RECV;
		pn->rx_credits = 0;
		break;

	default:
		net_dbg_ratelimited("Phonet unknown PEP message: %u\n",
				    hdr->message_id);
		err = -EINVAL;
	}
out:
	kfree_skb(skb);
	return (err == -ENOBUFS) ? NET_RX_DROP : NET_RX_SUCCESS;

queue:
	skb->dev = NULL;
	skb_set_owner_r(skb, sk);
	skb_queue_tail(queue, skb);
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk);
	return NET_RX_SUCCESS;
}

/* Destroy connected sock. */
static void pipe_destruct(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&pn->ctrlreq_queue);
}

static u8 pipe_negotiate_fc(const u8 *fcs, unsigned int n)
{
	unsigned int i;
	u8 final_fc = PN_NO_FLOW_CONTROL;

	for (i = 0; i < n; i++) {
		u8 fc = fcs[i];

		if (fc > final_fc && fc < PN_MAX_FLOW_CONTROL)
			final_fc = fc;
	}
	return final_fc;
}

static int pep_connresp_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *hdr;
	u8 n_sb;

	if (!pskb_pull(skb, sizeof(*hdr) + 4))
		return -EINVAL;

	hdr = pnp_hdr(skb);
	if (hdr->error_code != PN_PIPE_NO_ERROR)
		return -ECONNREFUSED;

	/* Parse sub-blocks */
	n_sb = hdr->data[3];
	while (n_sb > 0) {
		u8 type, buf[6], len = sizeof(buf);
		const u8 *data = pep_get_sb(skb, &type, &len, buf);

		if (data == NULL)
			return -EINVAL;

		switch (type) {
		case PN_PIPE_SB_REQUIRED_FC_TX:
			if (len < 2 || len < data[0])
				break;
			pn->tx_fc = pipe_negotiate_fc(data + 2, len - 2);
			break;

		case PN_PIPE_SB_PREFERRED_FC_RX:
			if (len < 2 || len < data[0])
				break;
			pn->rx_fc = pipe_negotiate_fc(data + 2, len - 2);
			break;

		}
		n_sb--;
	}

	return pipe_handler_send_created_ind(sk);
}

static int pep_enableresp_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct pnpipehdr *hdr = pnp_hdr(skb);

	if (hdr->error_code != PN_PIPE_NO_ERROR)
		return -ECONNREFUSED;

	return pep_indicate(sk, PNS_PIPE_ENABLED_IND, 0 /* sub-blocks */,
		NULL, 0, GFP_ATOMIC);

}

static void pipe_start_flow_control(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	if (!pn_flow_safe(pn->tx_fc)) {
		atomic_set(&pn->tx_credits, 1);
		sk->sk_write_space(sk);
	}
	pipe_grant_credits(sk, GFP_ATOMIC);
}

/* Queue an skb to an actively connected sock.
 * Socket lock must be held. */
static int pipe_handler_do_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *hdr = pnp_hdr(skb);
	int err = NET_RX_SUCCESS;

	switch (hdr->message_id) {
	case PNS_PIPE_ALIGNED_DATA:
		__skb_pull(skb, 1);
		fallthrough;
	case PNS_PIPE_DATA:
		__skb_pull(skb, 3); /* Pipe data header */
		if (!pn_flow_safe(pn->rx_fc)) {
			err = sock_queue_rcv_skb(sk, skb);
			if (!err)
				return NET_RX_SUCCESS;
			err = NET_RX_DROP;
			break;
		}

		if (pn->rx_credits == 0) {
			sk_drops_inc(sk);
			err = NET_RX_DROP;
			break;
		}
		pn->rx_credits--;
		skb->dev = NULL;
		skb_set_owner_r(skb, sk);
		skb_queue_tail(&sk->sk_receive_queue, skb);
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_data_ready(sk);
		return NET_RX_SUCCESS;

	case PNS_PEP_CONNECT_RESP:
		if (sk->sk_state != TCP_SYN_SENT)
			break;
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_state_change(sk);
		if (pep_connresp_rcv(sk, skb)) {
			sk->sk_state = TCP_CLOSE_WAIT;
			break;
		}
		if (pn->init_enable == PN_PIPE_DISABLE)
			sk->sk_state = TCP_SYN_RECV;
		else {
			sk->sk_state = TCP_ESTABLISHED;
			pipe_start_flow_control(sk);
		}
		break;

	case PNS_PEP_ENABLE_RESP:
		if (sk->sk_state != TCP_SYN_SENT)
			break;

		if (pep_enableresp_rcv(sk, skb)) {
			sk->sk_state = TCP_CLOSE_WAIT;
			break;
		}

		sk->sk_state = TCP_ESTABLISHED;
		pipe_start_flow_control(sk);
		break;

	case PNS_PEP_DISCONNECT_RESP:
		/* sock should already be dead, nothing to do */
		break;

	case PNS_PEP_STATUS_IND:
		pipe_rcv_status(sk, skb);
		break;
	}
	kfree_skb(skb);
	return err;
}

/* Listening sock must be locked */
static struct sock *pep_find_pipe(const struct hlist_head *hlist,
					const struct sockaddr_pn *dst,
					u8 pipe_handle)
{
	struct sock *sknode;
	u16 dobj = pn_sockaddr_get_object(dst);

	sk_for_each(sknode, hlist) {
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

	switch (hdr->message_id) {
	case PNS_PEP_CONNECT_REQ:
		if (sk->sk_state != TCP_LISTEN || sk_acceptq_is_full(sk)) {
			pep_reject_conn(sk, skb, PN_PIPE_ERR_PEP_IN_USE,
					GFP_ATOMIC);
			break;
		}
		skb_queue_head(&sk->sk_receive_queue, skb);
		sk_acceptq_added(sk);
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_data_ready(sk);
		return NET_RX_SUCCESS;

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
		break;

	default:
		if ((1 << sk->sk_state)
				& ~(TCPF_CLOSE|TCPF_LISTEN|TCPF_CLOSE_WAIT))
			/* actively connected socket */
			return pipe_handler_do_rcv(sk, skb);
	}
drop:
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

static int pipe_do_remove(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);
	struct pnpipehdr *ph;
	struct sk_buff *skb;

	skb = pep_alloc_skb(sk, NULL, 0, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	ph = pnp_hdr(skb);
	ph->utid = 0;
	ph->message_id = PNS_PIPE_REMOVE_REQ;
	ph->pipe_handle = pn->pipe_handle;
	ph->data0 = PAD;
	return pn_skb_send(sk, skb, NULL);
}

/* associated socket ceases to exist */
static void pep_sock_close(struct sock *sk, long timeout)
{
	struct pep_sock *pn = pep_sk(sk);
	int ifindex = 0;

	sock_hold(sk); /* keep a reference after sk_common_release() */
	sk_common_release(sk);

	lock_sock(sk);
	if ((1 << sk->sk_state) & (TCPF_SYN_RECV|TCPF_ESTABLISHED)) {
		if (sk->sk_backlog_rcv == pipe_do_rcv)
			/* Forcefully remove dangling Phonet pipe */
			pipe_do_remove(sk);
		else
			pipe_handler_request(sk, PNS_PEP_DISCONNECT_REQ, PAD,
						NULL, 0);
	}
	sk->sk_state = TCP_CLOSE;

	ifindex = pn->ifindex;
	pn->ifindex = 0;
	release_sock(sk);

	if (ifindex)
		gprs_detach(sk);
	sock_put(sk);
}

static struct sock *pep_sock_accept(struct sock *sk,
				    struct proto_accept_arg *arg)
{
	struct pep_sock *pn = pep_sk(sk), *newpn;
	struct sock *newsk = NULL;
	struct sk_buff *skb;
	struct pnpipehdr *hdr;
	struct sockaddr_pn dst, src;
	int err;
	u16 peer_type;
	u8 pipe_handle, enabled, n_sb;
	u8 aligned = 0;

	skb = skb_recv_datagram(sk, (arg->flags & O_NONBLOCK) ? MSG_DONTWAIT : 0,
				&arg->err);
	if (!skb)
		return NULL;

	lock_sock(sk);
	if (sk->sk_state != TCP_LISTEN) {
		err = -EINVAL;
		goto drop;
	}
	sk_acceptq_removed(sk);

	err = -EPROTO;
	if (!pskb_may_pull(skb, sizeof(*hdr) + 4))
		goto drop;

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
		pep_reject_conn(sk, skb, PN_PIPE_ERR_INVALID_PARAM,
				GFP_KERNEL);
		goto drop;
	}
	peer_type = hdr->other_pep_type << 8;

	/* Parse sub-blocks (options) */
	n_sb = hdr->data[3];
	while (n_sb > 0) {
		u8 type, buf[1], len = sizeof(buf);
		const u8 *data = pep_get_sb(skb, &type, &len, buf);

		if (data == NULL)
			goto drop;
		switch (type) {
		case PN_PIPE_SB_CONNECT_REQ_PEP_SUB_TYPE:
			if (len < 1)
				goto drop;
			peer_type = (peer_type & 0xff00) | data[0];
			break;
		case PN_PIPE_SB_ALIGNED_DATA:
			aligned = data[0] != 0;
			break;
		}
		n_sb--;
	}

	/* Check for duplicate pipe handle */
	pn_skb_get_dst_sockaddr(skb, &dst);
	newsk = pep_find_pipe(&pn->hlist, &dst, pipe_handle);
	if (unlikely(newsk)) {
		__sock_put(newsk);
		newsk = NULL;
		pep_reject_conn(sk, skb, PN_PIPE_ERR_PEP_IN_USE, GFP_KERNEL);
		goto drop;
	}

	/* Create a new to-be-accepted sock */
	newsk = sk_alloc(sock_net(sk), PF_PHONET, GFP_KERNEL, sk->sk_prot,
			 arg->kern);
	if (!newsk) {
		pep_reject_conn(sk, skb, PN_PIPE_ERR_OVERLOAD, GFP_KERNEL);
		err = -ENOBUFS;
		goto drop;
	}

	sock_init_data(NULL, newsk);
	newsk->sk_state = TCP_SYN_RECV;
	newsk->sk_backlog_rcv = pipe_do_rcv;
	newsk->sk_protocol = sk->sk_protocol;
	newsk->sk_destruct = pipe_destruct;

	newpn = pep_sk(newsk);
	pn_skb_get_src_sockaddr(skb, &src);
	newpn->pn_sk.sobject = pn_sockaddr_get_object(&dst);
	newpn->pn_sk.dobject = pn_sockaddr_get_object(&src);
	newpn->pn_sk.resource = pn_sockaddr_get_resource(&dst);
	sock_hold(sk);
	newpn->listener = sk;
	skb_queue_head_init(&newpn->ctrlreq_queue);
	newpn->pipe_handle = pipe_handle;
	atomic_set(&newpn->tx_credits, 0);
	newpn->ifindex = 0;
	newpn->peer_type = peer_type;
	newpn->rx_credits = 0;
	newpn->rx_fc = newpn->tx_fc = PN_LEGACY_FLOW_CONTROL;
	newpn->init_enable = enabled;
	newpn->aligned = aligned;

	err = pep_accept_conn(newsk, skb);
	if (err) {
		__sock_put(sk);
		sock_put(newsk);
		newsk = NULL;
		goto drop;
	}
	sk_add_node(newsk, &pn->hlist);
drop:
	release_sock(sk);
	kfree_skb(skb);
	arg->err = err;
	return newsk;
}

static int pep_sock_connect(struct sock *sk, struct sockaddr *addr, int len)
{
	struct pep_sock *pn = pep_sk(sk);
	int err;
	u8 data[4] = { 0 /* sub-blocks */, PAD, PAD, PAD };

	if (pn->pipe_handle == PN_PIPE_INVALID_HANDLE)
		pn->pipe_handle = 1; /* anything but INVALID_HANDLE */

	err = pipe_handler_request(sk, PNS_PEP_CONNECT_REQ,
				pn->init_enable, data, 4);
	if (err) {
		pn->pipe_handle = PN_PIPE_INVALID_HANDLE;
		return err;
	}

	sk->sk_state = TCP_SYN_SENT;

	return 0;
}

static int pep_sock_enable(struct sock *sk, struct sockaddr *addr, int len)
{
	int err;

	err = pipe_handler_request(sk, PNS_PEP_ENABLE_REQ, PAD,
				NULL, 0);
	if (err)
		return err;

	sk->sk_state = TCP_SYN_SENT;

	return 0;
}

static unsigned int pep_first_packet_length(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);
	struct sk_buff_head *q;
	struct sk_buff *skb;
	unsigned int len = 0;
	bool found = false;

	if (sock_flag(sk, SOCK_URGINLINE)) {
		q = &pn->ctrlreq_queue;
		spin_lock_bh(&q->lock);
		skb = skb_peek(q);
		if (skb) {
			len = skb->len;
			found = true;
		}
		spin_unlock_bh(&q->lock);
	}

	if (likely(!found)) {
		q = &sk->sk_receive_queue;
		spin_lock_bh(&q->lock);
		skb = skb_peek(q);
		if (skb)
			len = skb->len;
		spin_unlock_bh(&q->lock);
	}

	return len;
}

static int pep_ioctl(struct sock *sk, int cmd, int *karg)
{
	struct pep_sock *pn = pep_sk(sk);
	int ret = -ENOIOCTLCMD;

	switch (cmd) {
	case SIOCINQ:
		if (sk->sk_state == TCP_LISTEN) {
			ret = -EINVAL;
			break;
		}

		*karg = pep_first_packet_length(sk);
		ret = 0;
		break;

	case SIOCPNENABLEPIPE:
		lock_sock(sk);
		if (sk->sk_state == TCP_SYN_SENT)
			ret =  -EBUSY;
		else if (sk->sk_state == TCP_ESTABLISHED)
			ret = -EISCONN;
		else if (!pn->pn_sk.sobject)
			ret = -EADDRNOTAVAIL;
		else
			ret = pep_sock_enable(sk, NULL, 0);
		release_sock(sk);
		break;
	}

	return ret;
}

static int pep_init(struct sock *sk)
{
	struct pep_sock *pn = pep_sk(sk);

	sk->sk_destruct = pipe_destruct;
	INIT_HLIST_HEAD(&pn->hlist);
	pn->listener = NULL;
	skb_queue_head_init(&pn->ctrlreq_queue);
	atomic_set(&pn->tx_credits, 0);
	pn->ifindex = 0;
	pn->peer_type = 0;
	pn->pipe_handle = PN_PIPE_INVALID_HANDLE;
	pn->rx_credits = 0;
	pn->rx_fc = pn->tx_fc = PN_LEGACY_FLOW_CONTROL;
	pn->init_enable = 1;
	pn->aligned = 0;
	return 0;
}

static int pep_setsockopt(struct sock *sk, int level, int optname,
			  sockptr_t optval, unsigned int optlen)
{
	struct pep_sock *pn = pep_sk(sk);
	int val = 0, err = 0;

	if (level != SOL_PNPIPE)
		return -ENOPROTOOPT;
	if (optlen >= sizeof(int)) {
		if (copy_from_sockptr(&val, optval, sizeof(int)))
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

	case PNPIPE_HANDLE:
		if ((sk->sk_state == TCP_CLOSE) &&
			(val >= 0) && (val < PN_PIPE_INVALID_HANDLE))
			pn->pipe_handle = val;
		else
			err = -EINVAL;
		break;

	case PNPIPE_INITSTATE:
		pn->init_enable = !!val;
		break;

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

	case PNPIPE_HANDLE:
		val = pn->pipe_handle;
		if (val == PN_PIPE_INVALID_HANDLE)
			return -EINVAL;
		break;

	case PNPIPE_INITSTATE:
		val = pn->init_enable;
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
	int err;

	if (pn_flow_safe(pn->tx_fc) &&
	    !atomic_add_unless(&pn->tx_credits, -1, 0)) {
		kfree_skb(skb);
		return -ENOBUFS;
	}

	skb_push(skb, 3 + pn->aligned);
	skb_reset_transport_header(skb);
	ph = pnp_hdr(skb);
	ph->utid = 0;
	if (pn->aligned) {
		ph->message_id = PNS_PIPE_ALIGNED_DATA;
		ph->data0 = 0; /* padding */
	} else
		ph->message_id = PNS_PIPE_DATA;
	ph->pipe_handle = pn->pipe_handle;
	err = pn_skb_send(sk, skb, NULL);

	if (err && pn_flow_safe(pn->tx_fc))
		atomic_inc(&pn->tx_credits);
	return err;

}

static int pep_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct pep_sock *pn = pep_sk(sk);
	struct sk_buff *skb;
	long timeo;
	int flags = msg->msg_flags;
	int err, done;

	if (len > USHRT_MAX)
		return -EMSGSIZE;

	if ((msg->msg_flags & ~(MSG_DONTWAIT|MSG_EOR|MSG_NOSIGNAL|
				MSG_CMSG_COMPAT)) ||
			!(msg->msg_flags & MSG_EOR))
		return -EOPNOTSUPP;

	skb = sock_alloc_send_skb(sk, MAX_PNPIPE_HEADER + len,
					flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	skb_reserve(skb, MAX_PHONET_HEADER + 3 + pn->aligned);
	err = memcpy_from_msg(skb_put(skb, len), msg, len);
	if (err < 0)
		goto outfree;

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
		DEFINE_WAIT_FUNC(wait, woken_wake_function);

		if (!timeo) {
			err = -EAGAIN;
			goto out;
		}
		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			goto out;
		}

		add_wait_queue(sk_sleep(sk), &wait);
		done = sk_wait_event(sk, &timeo, atomic_read(&pn->tx_credits), &wait);
		remove_wait_queue(sk_sleep(sk), &wait);

		if (sk->sk_state != TCP_ESTABLISHED)
			goto disabled;
	}

	err = pipe_skb_send(sk, skb);
	if (err >= 0)
		err = len; /* success! */
	skb = NULL;
out:
	release_sock(sk);
outfree:
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

	if (pep_sk(sk)->aligned)
		return pipe_skb_send(sk, skb);

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
		pipe_grant_credits(sk, GFP_ATOMIC);
	return skb;
}

static int pep_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		       int flags, int *addr_len)
{
	struct sk_buff *skb;
	int err;

	if (flags & ~(MSG_OOB|MSG_PEEK|MSG_TRUNC|MSG_DONTWAIT|MSG_WAITALL|
			MSG_NOSIGNAL|MSG_CMSG_COMPAT))
		return -EOPNOTSUPP;

	if (unlikely(1 << sk->sk_state & (TCPF_LISTEN | TCPF_CLOSE)))
		return -ENOTCONN;

	if ((flags & MSG_OOB) || sock_flag(sk, SOCK_URGINLINE)) {
		/* Dequeue and acknowledge control request */
		struct pep_sock *pn = pep_sk(sk);

		if (flags & MSG_PEEK)
			return -EOPNOTSUPP;
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

	skb = skb_recv_datagram(sk, flags, &err);
	lock_sock(sk);
	if (skb == NULL) {
		if (err == -ENOTCONN && sk->sk_state == TCP_CLOSE_WAIT)
			err = -ECONNRESET;
		release_sock(sk);
		return err;
	}

	if (sk->sk_state == TCP_ESTABLISHED)
		pipe_grant_credits(sk, GFP_KERNEL);
	release_sock(sk);
copy:
	msg->msg_flags |= MSG_EOR;
	if (skb->len > len)
		msg->msg_flags |= MSG_TRUNC;
	else
		len = skb->len;

	err = skb_copy_datagram_msg(skb, 0, msg, len);
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

	if (pn->listener != NULL) {
		skparent = pn->listener;
		pn->listener = NULL;
		release_sock(sk);

		pn = pep_sk(skparent);
		lock_sock(skparent);
		sk_del_node_init(sk);
		sk = skparent;
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
	.connect	= pep_sock_connect,
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

static const struct phonet_protocol pep_pn_proto = {
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
