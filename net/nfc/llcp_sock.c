/*
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define pr_fmt(fmt) "llcp: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>

#include "nfc.h"
#include "llcp.h"

static int sock_wait_state(struct sock *sk, int state, unsigned long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	pr_debug("sk %p", sk);

	add_wait_queue(sk_sleep(sk), &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (sk->sk_state != state) {
		if (!timeo) {
			err = -EINPROGRESS;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		set_current_state(TASK_INTERRUPTIBLE);

		err = sock_error(sk);
		if (err)
			break;
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
}

static struct proto llcp_sock_proto = {
	.name     = "NFC_LLCP",
	.owner    = THIS_MODULE,
	.obj_size = sizeof(struct nfc_llcp_sock),
};

static int llcp_sock_bind(struct socket *sock, struct sockaddr *addr, int alen)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	struct nfc_llcp_local *local;
	struct nfc_dev *dev;
	struct sockaddr_nfc_llcp llcp_addr;
	int len, ret = 0;

	if (!addr || addr->sa_family != AF_NFC)
		return -EINVAL;

	pr_debug("sk %p addr %p family %d\n", sk, addr, addr->sa_family);

	memset(&llcp_addr, 0, sizeof(llcp_addr));
	len = min_t(unsigned int, sizeof(llcp_addr), alen);
	memcpy(&llcp_addr, addr, len);

	/* This is going to be a listening socket, dsap must be 0 */
	if (llcp_addr.dsap != 0)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != LLCP_CLOSED) {
		ret = -EBADFD;
		goto error;
	}

	dev = nfc_get_device(llcp_addr.dev_idx);
	if (dev == NULL) {
		ret = -ENODEV;
		goto error;
	}

	local = nfc_llcp_find_local(dev);
	if (local == NULL) {
		ret = -ENODEV;
		goto put_dev;
	}

	llcp_sock->dev = dev;
	llcp_sock->local = nfc_llcp_local_get(local);
	llcp_sock->nfc_protocol = llcp_addr.nfc_protocol;
	llcp_sock->service_name_len = min_t(unsigned int,
					    llcp_addr.service_name_len,
					    NFC_LLCP_MAX_SERVICE_NAME);
	llcp_sock->service_name = kmemdup(llcp_addr.service_name,
					  llcp_sock->service_name_len,
					  GFP_KERNEL);

	llcp_sock->ssap = nfc_llcp_get_sdp_ssap(local, llcp_sock);
	if (llcp_sock->ssap == LLCP_SAP_MAX) {
		ret = -EADDRINUSE;
		goto put_dev;
	}

	llcp_sock->reserved_ssap = llcp_sock->ssap;

	nfc_llcp_sock_link(&local->sockets, sk);

	pr_debug("Socket bound to SAP %d\n", llcp_sock->ssap);

	sk->sk_state = LLCP_BOUND;

put_dev:
	nfc_put_device(dev);

error:
	release_sock(sk);
	return ret;
}

static int llcp_raw_sock_bind(struct socket *sock, struct sockaddr *addr,
			      int alen)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	struct nfc_llcp_local *local;
	struct nfc_dev *dev;
	struct sockaddr_nfc_llcp llcp_addr;
	int len, ret = 0;

	if (!addr || addr->sa_family != AF_NFC)
		return -EINVAL;

	pr_debug("sk %p addr %p family %d\n", sk, addr, addr->sa_family);

	memset(&llcp_addr, 0, sizeof(llcp_addr));
	len = min_t(unsigned int, sizeof(llcp_addr), alen);
	memcpy(&llcp_addr, addr, len);

	lock_sock(sk);

	if (sk->sk_state != LLCP_CLOSED) {
		ret = -EBADFD;
		goto error;
	}

	dev = nfc_get_device(llcp_addr.dev_idx);
	if (dev == NULL) {
		ret = -ENODEV;
		goto error;
	}

	local = nfc_llcp_find_local(dev);
	if (local == NULL) {
		ret = -ENODEV;
		goto put_dev;
	}

	llcp_sock->dev = dev;
	llcp_sock->local = nfc_llcp_local_get(local);
	llcp_sock->nfc_protocol = llcp_addr.nfc_protocol;

	nfc_llcp_sock_link(&local->raw_sockets, sk);

	sk->sk_state = LLCP_BOUND;

put_dev:
	nfc_put_device(dev);

error:
	release_sock(sk);
	return ret;
}

static int llcp_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int ret = 0;

	pr_debug("sk %p backlog %d\n", sk, backlog);

	lock_sock(sk);

	if ((sock->type != SOCK_SEQPACKET && sock->type != SOCK_STREAM) ||
	    sk->sk_state != LLCP_BOUND) {
		ret = -EBADFD;
		goto error;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;

	pr_debug("Socket listening\n");
	sk->sk_state = LLCP_LISTEN;

error:
	release_sock(sk);

	return ret;
}

static int nfc_llcp_setsockopt(struct socket *sock, int level, int optname,
			       char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	u32 opt;
	int err = 0;

	pr_debug("%p optname %d\n", sk, optname);

	if (level != SOL_NFC)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case NFC_LLCP_RW:
		if (sk->sk_state == LLCP_CONNECTED ||
		    sk->sk_state == LLCP_BOUND ||
		    sk->sk_state == LLCP_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt > LLCP_MAX_RW) {
			err = -EINVAL;
			break;
		}

		llcp_sock->rw = (u8) opt;

		break;

	case NFC_LLCP_MIUX:
		if (sk->sk_state == LLCP_CONNECTED ||
		    sk->sk_state == LLCP_BOUND ||
		    sk->sk_state == LLCP_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt > LLCP_MAX_MIUX) {
			err = -EINVAL;
			break;
		}

		llcp_sock->miux = cpu_to_be16((u16) opt);

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);

	pr_debug("%p rw %d miux %d\n", llcp_sock,
		 llcp_sock->rw, llcp_sock->miux);

	return err;
}

static int nfc_llcp_getsockopt(struct socket *sock, int level, int optname,
			       char __user *optval, int __user *optlen)
{
	struct nfc_llcp_local *local;
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	int len, err = 0;
	u16 miux, remote_miu;
	u8 rw;

	pr_debug("%p optname %d\n", sk, optname);

	if (level != SOL_NFC)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	local = llcp_sock->local;
	if (!local)
		return -ENODEV;

	len = min_t(u32, len, sizeof(u32));

	lock_sock(sk);

	switch (optname) {
	case NFC_LLCP_RW:
		rw = llcp_sock->rw > LLCP_MAX_RW ? local->rw : llcp_sock->rw;
		if (put_user(rw, (u32 __user *) optval))
			err = -EFAULT;

		break;

	case NFC_LLCP_MIUX:
		miux = be16_to_cpu(llcp_sock->miux) > LLCP_MAX_MIUX ?
			be16_to_cpu(local->miux) : be16_to_cpu(llcp_sock->miux);

		if (put_user(miux, (u32 __user *) optval))
			err = -EFAULT;

		break;

	case NFC_LLCP_REMOTE_MIU:
		remote_miu = llcp_sock->remote_miu > LLCP_MAX_MIU ?
				local->remote_miu : llcp_sock->remote_miu;

		if (put_user(remote_miu, (u32 __user *) optval))
			err = -EFAULT;

		break;

	case NFC_LLCP_REMOTE_LTO:
		if (put_user(local->remote_lto / 10, (u32 __user *) optval))
			err = -EFAULT;

		break;

	case NFC_LLCP_REMOTE_RW:
		if (put_user(llcp_sock->remote_rw, (u32 __user *) optval))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);

	if (put_user(len, optlen))
		return -EFAULT;

	return err;
}

void nfc_llcp_accept_unlink(struct sock *sk)
{
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);

	pr_debug("state %d\n", sk->sk_state);

	list_del_init(&llcp_sock->accept_queue);
	sk_acceptq_removed(llcp_sock->parent);
	llcp_sock->parent = NULL;

	sock_put(sk);
}

void nfc_llcp_accept_enqueue(struct sock *parent, struct sock *sk)
{
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	struct nfc_llcp_sock *llcp_sock_parent = nfc_llcp_sock(parent);

	/* Lock will be free from unlink */
	sock_hold(sk);

	list_add_tail(&llcp_sock->accept_queue,
		      &llcp_sock_parent->accept_queue);
	llcp_sock->parent = parent;
	sk_acceptq_added(parent);
}

struct sock *nfc_llcp_accept_dequeue(struct sock *parent,
				     struct socket *newsock)
{
	struct nfc_llcp_sock *lsk, *n, *llcp_parent;
	struct sock *sk;

	llcp_parent = nfc_llcp_sock(parent);

	list_for_each_entry_safe(lsk, n, &llcp_parent->accept_queue,
				 accept_queue) {
		sk = &lsk->sk;
		lock_sock(sk);

		if (sk->sk_state == LLCP_CLOSED) {
			release_sock(sk);
			nfc_llcp_accept_unlink(sk);
			continue;
		}

		if (sk->sk_state == LLCP_CONNECTED || !newsock) {
			list_del_init(&lsk->accept_queue);
			sock_put(sk);

			if (newsock)
				sock_graft(sk, newsock);

			release_sock(sk);

			pr_debug("Returning sk state %d\n", sk->sk_state);

			sk_acceptq_removed(parent);

			return sk;
		}

		release_sock(sk);
	}

	return NULL;
}

static int llcp_sock_accept(struct socket *sock, struct socket *newsock,
			    int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *new_sk;
	long timeo;
	int ret = 0;

	pr_debug("parent %p\n", sk);

	lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	if (sk->sk_state != LLCP_LISTEN) {
		ret = -EBADFD;
		goto error;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	/* Wait for an incoming connection. */
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	while (!(new_sk = nfc_llcp_accept_dequeue(sk, newsock))) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!timeo) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);

	if (ret)
		goto error;

	newsock->state = SS_CONNECTED;

	pr_debug("new socket %p\n", new_sk);

error:
	release_sock(sk);

	return ret;
}

static int llcp_sock_getname(struct socket *sock, struct sockaddr *uaddr,
			     int *len, int peer)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	DECLARE_SOCKADDR(struct sockaddr_nfc_llcp *, llcp_addr, uaddr);

	if (llcp_sock == NULL || llcp_sock->dev == NULL)
		return -EBADFD;

	pr_debug("%p %d %d %d\n", sk, llcp_sock->target_idx,
		 llcp_sock->dsap, llcp_sock->ssap);

	memset(llcp_addr, 0, sizeof(*llcp_addr));
	*len = sizeof(struct sockaddr_nfc_llcp);

	llcp_addr->sa_family = AF_NFC;
	llcp_addr->dev_idx = llcp_sock->dev->idx;
	llcp_addr->target_idx = llcp_sock->target_idx;
	llcp_addr->nfc_protocol = llcp_sock->nfc_protocol;
	llcp_addr->dsap = llcp_sock->dsap;
	llcp_addr->ssap = llcp_sock->ssap;
	llcp_addr->service_name_len = llcp_sock->service_name_len;
	memcpy(llcp_addr->service_name, llcp_sock->service_name,
	       llcp_addr->service_name_len);

	return 0;
}

static inline unsigned int llcp_accept_poll(struct sock *parent)
{
	struct nfc_llcp_sock *llcp_sock, *n, *parent_sock;
	struct sock *sk;

	parent_sock = nfc_llcp_sock(parent);

	list_for_each_entry_safe(llcp_sock, n, &parent_sock->accept_queue,
				 accept_queue) {
		sk = &llcp_sock->sk;

		if (sk->sk_state == LLCP_CONNECTED)
			return POLLIN | POLLRDNORM;
	}

	return 0;
}

static unsigned int llcp_sock_poll(struct file *file, struct socket *sock,
				   poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask = 0;

	pr_debug("%p\n", sk);

	sock_poll_wait(file, sk_sleep(sk), wait);

	if (sk->sk_state == LLCP_LISTEN)
		return llcp_accept_poll(sk);

	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR |
			(sock_flag(sk, SOCK_SELECT_ERR_QUEUE) ? POLLPRI : 0);

	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == LLCP_CLOSED)
		mask |= POLLHUP;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP | POLLIN | POLLRDNORM;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	pr_debug("mask 0x%x\n", mask);

	return mask;
}

static int llcp_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_local *local;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	int err = 0;

	if (!sk)
		return 0;

	pr_debug("%p\n", sk);

	local = llcp_sock->local;
	if (local == NULL) {
		err = -ENODEV;
		goto out;
	}

	lock_sock(sk);

	/* Send a DISC */
	if (sk->sk_state == LLCP_CONNECTED)
		nfc_llcp_send_disconnect(llcp_sock);

	if (sk->sk_state == LLCP_LISTEN) {
		struct nfc_llcp_sock *lsk, *n;
		struct sock *accept_sk;

		list_for_each_entry_safe(lsk, n, &llcp_sock->accept_queue,
					 accept_queue) {
			accept_sk = &lsk->sk;
			lock_sock(accept_sk);

			nfc_llcp_send_disconnect(lsk);
			nfc_llcp_accept_unlink(accept_sk);

			release_sock(accept_sk);
		}
	}

	if (llcp_sock->reserved_ssap < LLCP_SAP_MAX)
		nfc_llcp_put_ssap(llcp_sock->local, llcp_sock->ssap);

	release_sock(sk);

	/* Keep this sock alive and therefore do not remove it from the sockets
	 * list until the DISC PDU has been actually sent. Otherwise we would
	 * reply with DM PDUs before sending the DISC one.
	 */
	if (sk->sk_state == LLCP_DISCONNECTING)
		return err;

	if (sock->type == SOCK_RAW)
		nfc_llcp_sock_unlink(&local->raw_sockets, sk);
	else
		nfc_llcp_sock_unlink(&local->sockets, sk);

out:
	sock_orphan(sk);
	sock_put(sk);

	return err;
}

static int llcp_sock_connect(struct socket *sock, struct sockaddr *_addr,
			     int len, int flags)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	struct sockaddr_nfc_llcp *addr = (struct sockaddr_nfc_llcp *)_addr;
	struct nfc_dev *dev;
	struct nfc_llcp_local *local;
	int ret = 0;

	pr_debug("sock %p sk %p flags 0x%x\n", sock, sk, flags);

	if (!addr || len < sizeof(struct sockaddr_nfc) ||
	    addr->sa_family != AF_NFC)
		return -EINVAL;

	if (addr->service_name_len == 0 && addr->dsap == 0)
		return -EINVAL;

	pr_debug("addr dev_idx=%u target_idx=%u protocol=%u\n", addr->dev_idx,
		 addr->target_idx, addr->nfc_protocol);

	lock_sock(sk);

	if (sk->sk_state == LLCP_CONNECTED) {
		ret = -EISCONN;
		goto error;
	}

	dev = nfc_get_device(addr->dev_idx);
	if (dev == NULL) {
		ret = -ENODEV;
		goto error;
	}

	local = nfc_llcp_find_local(dev);
	if (local == NULL) {
		ret = -ENODEV;
		goto put_dev;
	}

	device_lock(&dev->dev);
	if (dev->dep_link_up == false) {
		ret = -ENOLINK;
		device_unlock(&dev->dev);
		goto put_dev;
	}
	device_unlock(&dev->dev);

	if (local->rf_mode == NFC_RF_INITIATOR &&
	    addr->target_idx != local->target_idx) {
		ret = -ENOLINK;
		goto put_dev;
	}

	llcp_sock->dev = dev;
	llcp_sock->local = nfc_llcp_local_get(local);
	llcp_sock->remote_miu = llcp_sock->local->remote_miu;
	llcp_sock->ssap = nfc_llcp_get_local_ssap(local);
	if (llcp_sock->ssap == LLCP_SAP_MAX) {
		ret = -ENOMEM;
		goto put_dev;
	}

	llcp_sock->reserved_ssap = llcp_sock->ssap;

	if (addr->service_name_len == 0)
		llcp_sock->dsap = addr->dsap;
	else
		llcp_sock->dsap = LLCP_SAP_SDP;
	llcp_sock->nfc_protocol = addr->nfc_protocol;
	llcp_sock->service_name_len = min_t(unsigned int,
					    addr->service_name_len,
					    NFC_LLCP_MAX_SERVICE_NAME);
	llcp_sock->service_name = kmemdup(addr->service_name,
					  llcp_sock->service_name_len,
					  GFP_KERNEL);

	nfc_llcp_sock_link(&local->connecting_sockets, sk);

	ret = nfc_llcp_send_connect(llcp_sock);
	if (ret)
		goto sock_unlink;

	ret = sock_wait_state(sk, LLCP_CONNECTED,
			      sock_sndtimeo(sk, flags & O_NONBLOCK));
	if (ret)
		goto sock_unlink;

	release_sock(sk);

	return 0;

sock_unlink:
	nfc_llcp_put_ssap(local, llcp_sock->ssap);

	nfc_llcp_sock_unlink(&local->connecting_sockets, sk);

put_dev:
	nfc_put_device(dev);

error:
	release_sock(sk);
	return ret;
}

static int llcp_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			     struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);
	int ret;

	pr_debug("sock %p sk %p", sock, sk);

	ret = sock_error(sk);
	if (ret)
		return ret;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	lock_sock(sk);

	if (sk->sk_type == SOCK_DGRAM) {
		struct sockaddr_nfc_llcp *addr =
			(struct sockaddr_nfc_llcp *)msg->msg_name;

		if (msg->msg_namelen < sizeof(*addr)) {
			release_sock(sk);
			return -EINVAL;
		}

		release_sock(sk);

		return nfc_llcp_send_ui_frame(llcp_sock, addr->dsap, addr->ssap,
					      msg, len);
	}

	if (sk->sk_state != LLCP_CONNECTED) {
		release_sock(sk);
		return -ENOTCONN;
	}

	release_sock(sk);

	return nfc_llcp_send_i_frame(llcp_sock, msg, len);
}

static int llcp_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
			     struct msghdr *msg, size_t len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	unsigned int copied, rlen;
	struct sk_buff *skb, *cskb;
	int err = 0;

	pr_debug("%p %zu\n", sk, len);

	msg->msg_namelen = 0;

	lock_sock(sk);

	if (sk->sk_state == LLCP_CLOSED &&
	    skb_queue_empty(&sk->sk_receive_queue)) {
		release_sock(sk);
		return 0;
	}

	release_sock(sk);

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb) {
		pr_err("Recv datagram failed state %d %d %d",
		       sk->sk_state, err, sock_error(sk));

		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return 0;

		return err;
	}

	rlen = skb->len;		/* real length of skb */
	copied = min_t(unsigned int, rlen, len);

	cskb = skb;
	if (skb_copy_datagram_iovec(cskb, 0, msg->msg_iov, copied)) {
		if (!(flags & MSG_PEEK))
			skb_queue_head(&sk->sk_receive_queue, skb);
		return -EFAULT;
	}

	sock_recv_timestamp(msg, sk, skb);

	if (sk->sk_type == SOCK_DGRAM && msg->msg_name) {
		struct nfc_llcp_ui_cb *ui_cb = nfc_llcp_ui_skb_cb(skb);
		struct sockaddr_nfc_llcp *sockaddr =
			(struct sockaddr_nfc_llcp *) msg->msg_name;

		msg->msg_namelen = sizeof(struct sockaddr_nfc_llcp);

		pr_debug("Datagram socket %d %d\n", ui_cb->dsap, ui_cb->ssap);

		memset(sockaddr, 0, sizeof(*sockaddr));
		sockaddr->sa_family = AF_NFC;
		sockaddr->nfc_protocol = NFC_PROTO_NFC_DEP;
		sockaddr->dsap = ui_cb->dsap;
		sockaddr->ssap = ui_cb->ssap;
	}

	/* Mark read part of skb as used */
	if (!(flags & MSG_PEEK)) {

		/* SOCK_STREAM: re-queue skb if it contains unreceived data */
		if (sk->sk_type == SOCK_STREAM ||
		    sk->sk_type == SOCK_DGRAM ||
		    sk->sk_type == SOCK_RAW) {
			skb_pull(skb, copied);
			if (skb->len) {
				skb_queue_head(&sk->sk_receive_queue, skb);
				goto done;
			}
		}

		kfree_skb(skb);
	}

	/* XXX Queue backlogged skbs */

done:
	/* SOCK_SEQPACKET: return real length if MSG_TRUNC is set */
	if (sk->sk_type == SOCK_SEQPACKET && (flags & MSG_TRUNC))
		copied = rlen;

	return copied;
}

static const struct proto_ops llcp_sock_ops = {
	.family         = PF_NFC,
	.owner          = THIS_MODULE,
	.bind           = llcp_sock_bind,
	.connect        = llcp_sock_connect,
	.release        = llcp_sock_release,
	.socketpair     = sock_no_socketpair,
	.accept         = llcp_sock_accept,
	.getname        = llcp_sock_getname,
	.poll           = llcp_sock_poll,
	.ioctl          = sock_no_ioctl,
	.listen         = llcp_sock_listen,
	.shutdown       = sock_no_shutdown,
	.setsockopt     = nfc_llcp_setsockopt,
	.getsockopt     = nfc_llcp_getsockopt,
	.sendmsg        = llcp_sock_sendmsg,
	.recvmsg        = llcp_sock_recvmsg,
	.mmap           = sock_no_mmap,
};

static const struct proto_ops llcp_rawsock_ops = {
	.family         = PF_NFC,
	.owner          = THIS_MODULE,
	.bind           = llcp_raw_sock_bind,
	.connect        = sock_no_connect,
	.release        = llcp_sock_release,
	.socketpair     = sock_no_socketpair,
	.accept         = sock_no_accept,
	.getname        = llcp_sock_getname,
	.poll           = llcp_sock_poll,
	.ioctl          = sock_no_ioctl,
	.listen         = sock_no_listen,
	.shutdown       = sock_no_shutdown,
	.setsockopt     = sock_no_setsockopt,
	.getsockopt     = sock_no_getsockopt,
	.sendmsg        = sock_no_sendmsg,
	.recvmsg        = llcp_sock_recvmsg,
	.mmap           = sock_no_mmap,
};

static void llcp_sock_destruct(struct sock *sk)
{
	struct nfc_llcp_sock *llcp_sock = nfc_llcp_sock(sk);

	pr_debug("%p\n", sk);

	if (sk->sk_state == LLCP_CONNECTED)
		nfc_put_device(llcp_sock->dev);

	skb_queue_purge(&sk->sk_receive_queue);

	nfc_llcp_sock_free(llcp_sock);

	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Freeing alive NFC LLCP socket %p\n", sk);
		return;
	}
}

struct sock *nfc_llcp_sock_alloc(struct socket *sock, int type, gfp_t gfp)
{
	struct sock *sk;
	struct nfc_llcp_sock *llcp_sock;

	sk = sk_alloc(&init_net, PF_NFC, gfp, &llcp_sock_proto);
	if (!sk)
		return NULL;

	llcp_sock = nfc_llcp_sock(sk);

	sock_init_data(sock, sk);
	sk->sk_state = LLCP_CLOSED;
	sk->sk_protocol = NFC_SOCKPROTO_LLCP;
	sk->sk_type = type;
	sk->sk_destruct = llcp_sock_destruct;

	llcp_sock->ssap = 0;
	llcp_sock->dsap = LLCP_SAP_SDP;
	llcp_sock->rw = LLCP_MAX_RW + 1;
	llcp_sock->miux = cpu_to_be16(LLCP_MAX_MIUX + 1);
	llcp_sock->send_n = llcp_sock->send_ack_n = 0;
	llcp_sock->recv_n = llcp_sock->recv_ack_n = 0;
	llcp_sock->remote_ready = 1;
	llcp_sock->reserved_ssap = LLCP_SAP_MAX;
	nfc_llcp_socket_remote_param_init(llcp_sock);
	skb_queue_head_init(&llcp_sock->tx_queue);
	skb_queue_head_init(&llcp_sock->tx_pending_queue);
	INIT_LIST_HEAD(&llcp_sock->accept_queue);

	if (sock != NULL)
		sock->state = SS_UNCONNECTED;

	return sk;
}

void nfc_llcp_sock_free(struct nfc_llcp_sock *sock)
{
	kfree(sock->service_name);

	skb_queue_purge(&sock->tx_queue);
	skb_queue_purge(&sock->tx_pending_queue);

	list_del_init(&sock->accept_queue);

	sock->parent = NULL;

	nfc_llcp_local_put(sock->local);
}

static int llcp_sock_create(struct net *net, struct socket *sock,
			    const struct nfc_protocol *nfc_proto)
{
	struct sock *sk;

	pr_debug("%p\n", sock);

	if (sock->type != SOCK_STREAM &&
	    sock->type != SOCK_DGRAM &&
	    sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	if (sock->type == SOCK_RAW)
		sock->ops = &llcp_rawsock_ops;
	else
		sock->ops = &llcp_sock_ops;

	sk = nfc_llcp_sock_alloc(sock, sock->type, GFP_ATOMIC);
	if (sk == NULL)
		return -ENOMEM;

	return 0;
}

static const struct nfc_protocol llcp_nfc_proto = {
	.id	  = NFC_SOCKPROTO_LLCP,
	.proto    = &llcp_sock_proto,
	.owner    = THIS_MODULE,
	.create   = llcp_sock_create
};

int __init nfc_llcp_sock_init(void)
{
	return nfc_proto_register(&llcp_nfc_proto);
}

void nfc_llcp_sock_exit(void)
{
	nfc_proto_unregister(&llcp_nfc_proto);
}
