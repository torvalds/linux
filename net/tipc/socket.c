/*
 * net/tipc/socket.c: TIPC socket API
 *
 * Copyright (c) 2001-2007, Ericsson AB
 * Copyright (c) 2004-2007, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <asm/semaphore.h>
#include <asm/string.h>
#include <asm/atomic.h>
#include <net/sock.h>

#include <linux/tipc.h>
#include <linux/tipc_config.h>
#include <net/tipc/tipc_msg.h>
#include <net/tipc/tipc_port.h>

#include "core.h"

#define SS_LISTENING	-1	/* socket is listening */
#define SS_READY	-2	/* socket is connectionless */

#define OVERLOAD_LIMIT_BASE    5000

struct tipc_sock {
	struct sock sk;
	struct tipc_port *p;
	struct semaphore sem;
};

#define tipc_sk(sk) ((struct tipc_sock*)sk)

static u32 dispatch(struct tipc_port *tport, struct sk_buff *buf);
static void wakeupdispatch(struct tipc_port *tport);

static struct proto_ops packet_ops;
static struct proto_ops stream_ops;
static struct proto_ops msg_ops;

static struct proto tipc_proto;

static int sockets_enabled = 0;

static atomic_t tipc_queue_size = ATOMIC_INIT(0);


/*
 * sock_lock(): Lock a port/socket pair. lock_sock() can
 * not be used here, since the same lock must protect ports
 * with non-socket interfaces.
 * See net.c for description of locking policy.
 */
static void sock_lock(struct tipc_sock* tsock)
{
	spin_lock_bh(tsock->p->lock);
}

/*
 * sock_unlock(): Unlock a port/socket pair
 */
static void sock_unlock(struct tipc_sock* tsock)
{
	spin_unlock_bh(tsock->p->lock);
}

/**
 * pollmask - determine the current set of poll() events for a socket
 * @sock: socket structure
 *
 * TIPC sets the returned events as follows:
 * a) POLLRDNORM and POLLIN are set if the socket's receive queue is non-empty
 *    or if a connection-oriented socket is does not have an active connection
 *    (i.e. a read operation will not block).
 * b) POLLOUT is set except when a socket's connection has been terminated
 *    (i.e. a write operation will not block).
 * c) POLLHUP is set when a socket's connection has been terminated.
 *
 * IMPORTANT: The fact that a read or write operation will not block does NOT
 * imply that the operation will succeed!
 *
 * Returns pollmask value
 */

static u32 pollmask(struct socket *sock)
{
	u32 mask;

	if ((skb_queue_len(&sock->sk->sk_receive_queue) != 0) ||
	    (sock->state == SS_UNCONNECTED) ||
	    (sock->state == SS_DISCONNECTING))
		mask = (POLLRDNORM | POLLIN);
	else
		mask = 0;

	if (sock->state == SS_DISCONNECTING)
		mask |= POLLHUP;
	else
		mask |= POLLOUT;

	return mask;
}


/**
 * advance_queue - discard first buffer in queue
 * @tsock: TIPC socket
 */

static void advance_queue(struct tipc_sock *tsock)
{
	sock_lock(tsock);
	buf_discard(skb_dequeue(&tsock->sk.sk_receive_queue));
	sock_unlock(tsock);
	atomic_dec(&tipc_queue_size);
}

/**
 * tipc_create - create a TIPC socket
 * @sock: pre-allocated socket structure
 * @protocol: protocol indicator (must be 0)
 *
 * This routine creates and attaches a 'struct sock' to the 'struct socket',
 * then create and attaches a TIPC port to the 'struct sock' part.
 *
 * Returns 0 on success, errno otherwise
 */
static int tipc_create(struct net *net, struct socket *sock, int protocol)
{
	struct tipc_sock *tsock;
	struct tipc_port *port;
	struct sock *sk;
	u32 ref;

	if (net != &init_net)
		return -EAFNOSUPPORT;

	if (unlikely(protocol != 0))
		return -EPROTONOSUPPORT;

	ref = tipc_createport_raw(NULL, &dispatch, &wakeupdispatch, TIPC_LOW_IMPORTANCE);
	if (unlikely(!ref))
		return -ENOMEM;

	sock->state = SS_UNCONNECTED;

	switch (sock->type) {
	case SOCK_STREAM:
		sock->ops = &stream_ops;
		break;
	case SOCK_SEQPACKET:
		sock->ops = &packet_ops;
		break;
	case SOCK_DGRAM:
		tipc_set_portunreliable(ref, 1);
		/* fall through */
	case SOCK_RDM:
		tipc_set_portunreturnable(ref, 1);
		sock->ops = &msg_ops;
		sock->state = SS_READY;
		break;
	default:
		tipc_deleteport(ref);
		return -EPROTOTYPE;
	}

	sk = sk_alloc(net, AF_TIPC, GFP_KERNEL, &tipc_proto);
	if (!sk) {
		tipc_deleteport(ref);
		return -ENOMEM;
	}

	sock_init_data(sock, sk);
	init_waitqueue_head(sk->sk_sleep);
	sk->sk_rcvtimeo = 8 * HZ;   /* default connect timeout = 8s */

	tsock = tipc_sk(sk);
	port = tipc_get_port(ref);

	tsock->p = port;
	port->usr_handle = tsock;

	init_MUTEX(&tsock->sem);

	dbg("sock_create: %x\n",tsock);

	atomic_inc(&tipc_user_count);

	return 0;
}

/**
 * release - destroy a TIPC socket
 * @sock: socket to destroy
 *
 * This routine cleans up any messages that are still queued on the socket.
 * For DGRAM and RDM socket types, all queued messages are rejected.
 * For SEQPACKET and STREAM socket types, the first message is rejected
 * and any others are discarded.  (If the first message on a STREAM socket
 * is partially-read, it is discarded and the next one is rejected instead.)
 *
 * NOTE: Rejected messages are not necessarily returned to the sender!  They
 * are returned or discarded according to the "destination droppable" setting
 * specified for the message by the sender.
 *
 * Returns 0 on success, errno otherwise
 */

static int release(struct socket *sock)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sock *sk = sock->sk;
	int res = TIPC_OK;
	struct sk_buff *buf;

	dbg("sock_delete: %x\n",tsock);
	if (!tsock)
		return 0;
	down(&tsock->sem);
	if (!sock->sk) {
		up(&tsock->sem);
		return 0;
	}

	/* Reject unreceived messages, unless no longer connected */

	while (sock->state != SS_DISCONNECTING) {
		sock_lock(tsock);
		buf = skb_dequeue(&sk->sk_receive_queue);
		if (!buf)
			tsock->p->usr_handle = NULL;
		sock_unlock(tsock);
		if (!buf)
			break;
		if (TIPC_SKB_CB(buf)->handle != msg_data(buf_msg(buf)))
			buf_discard(buf);
		else
			tipc_reject_msg(buf, TIPC_ERR_NO_PORT);
		atomic_dec(&tipc_queue_size);
	}

	/* Delete TIPC port */

	res = tipc_deleteport(tsock->p->ref);
	sock->sk = NULL;

	/* Discard any remaining messages */

	while ((buf = skb_dequeue(&sk->sk_receive_queue))) {
		buf_discard(buf);
		atomic_dec(&tipc_queue_size);
	}

	up(&tsock->sem);

	sock_put(sk);

	atomic_dec(&tipc_user_count);
	return res;
}

/**
 * bind - associate or disassocate TIPC name(s) with a socket
 * @sock: socket structure
 * @uaddr: socket address describing name(s) and desired operation
 * @uaddr_len: size of socket address data structure
 *
 * Name and name sequence binding is indicated using a positive scope value;
 * a negative scope value unbinds the specified name.  Specifying no name
 * (i.e. a socket address length of 0) unbinds all names from the socket.
 *
 * Returns 0 on success, errno otherwise
 */

static int bind(struct socket *sock, struct sockaddr *uaddr, int uaddr_len)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)uaddr;
	int res;

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	if (unlikely(!uaddr_len)) {
		res = tipc_withdraw(tsock->p->ref, 0, NULL);
		goto exit;
	}

	if (uaddr_len < sizeof(struct sockaddr_tipc)) {
		res = -EINVAL;
		goto exit;
	}

	if (addr->family != AF_TIPC) {
		res = -EAFNOSUPPORT;
		goto exit;
	}
	if (addr->addrtype == TIPC_ADDR_NAME)
		addr->addr.nameseq.upper = addr->addr.nameseq.lower;
	else if (addr->addrtype != TIPC_ADDR_NAMESEQ) {
		res = -EAFNOSUPPORT;
		goto exit;
	}

	if (addr->scope > 0)
		res = tipc_publish(tsock->p->ref, addr->scope,
				   &addr->addr.nameseq);
	else
		res = tipc_withdraw(tsock->p->ref, -addr->scope,
				    &addr->addr.nameseq);
exit:
	up(&tsock->sem);
	return res;
}

/**
 * get_name - get port ID of socket or peer socket
 * @sock: socket structure
 * @uaddr: area for returned socket address
 * @uaddr_len: area for returned length of socket address
 * @peer: 0 to obtain socket name, 1 to obtain peer socket name
 *
 * Returns 0 on success, errno otherwise
 */

static int get_name(struct socket *sock, struct sockaddr *uaddr,
		    int *uaddr_len, int peer)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)uaddr;
	u32 res;

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	*uaddr_len = sizeof(*addr);
	addr->addrtype = TIPC_ADDR_ID;
	addr->family = AF_TIPC;
	addr->scope = 0;
	if (peer)
		res = tipc_peer(tsock->p->ref, &addr->addr.id);
	else
		res = tipc_ownidentity(tsock->p->ref, &addr->addr.id);
	addr->addr.name.domain = 0;

	up(&tsock->sem);
	return res;
}

/**
 * poll - read and possibly block on pollmask
 * @file: file structure associated with the socket
 * @sock: socket for which to calculate the poll bits
 * @wait: ???
 *
 * Returns the pollmask
 */

static unsigned int poll(struct file *file, struct socket *sock,
			 poll_table *wait)
{
	poll_wait(file, sock->sk->sk_sleep, wait);
	/* NEED LOCK HERE? */
	return pollmask(sock);
}

/**
 * dest_name_check - verify user is permitted to send to specified port name
 * @dest: destination address
 * @m: descriptor for message to be sent
 *
 * Prevents restricted configuration commands from being issued by
 * unauthorized users.
 *
 * Returns 0 if permission is granted, otherwise errno
 */

static int dest_name_check(struct sockaddr_tipc *dest, struct msghdr *m)
{
	struct tipc_cfg_msg_hdr hdr;

	if (likely(dest->addr.name.name.type >= TIPC_RESERVED_TYPES))
		return 0;
	if (likely(dest->addr.name.name.type == TIPC_TOP_SRV))
		return 0;

	if (likely(dest->addr.name.name.type != TIPC_CFG_SRV))
		return -EACCES;

	if (copy_from_user(&hdr, m->msg_iov[0].iov_base, sizeof(hdr)))
		return -EFAULT;
	if ((ntohs(hdr.tcm_type) & 0xC000) && (!capable(CAP_NET_ADMIN)))
		return -EACCES;

	return 0;
}

/**
 * send_msg - send message in connectionless manner
 * @iocb: (unused)
 * @sock: socket structure
 * @m: message to send
 * @total_len: length of message
 *
 * Message must have an destination specified explicitly.
 * Used for SOCK_RDM and SOCK_DGRAM messages,
 * and for 'SYN' messages on SOCK_SEQPACKET and SOCK_STREAM connections.
 * (Note: 'SYN+' is prohibited on SOCK_STREAM.)
 *
 * Returns the number of bytes sent on success, or errno otherwise
 */

static int send_msg(struct kiocb *iocb, struct socket *sock,
		    struct msghdr *m, size_t total_len)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sockaddr_tipc *dest = (struct sockaddr_tipc *)m->msg_name;
	struct sk_buff *buf;
	int needs_conn;
	int res = -EINVAL;

	if (unlikely(!dest))
		return -EDESTADDRREQ;
	if (unlikely((m->msg_namelen < sizeof(*dest)) ||
		     (dest->family != AF_TIPC)))
		return -EINVAL;

	needs_conn = (sock->state != SS_READY);
	if (unlikely(needs_conn)) {
		if (sock->state == SS_LISTENING)
			return -EPIPE;
		if (sock->state != SS_UNCONNECTED)
			return -EISCONN;
		if ((tsock->p->published) ||
		    ((sock->type == SOCK_STREAM) && (total_len != 0)))
			return -EOPNOTSUPP;
		if (dest->addrtype == TIPC_ADDR_NAME) {
			tsock->p->conn_type = dest->addr.name.name.type;
			tsock->p->conn_instance = dest->addr.name.name.instance;
		}
	}

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	if (needs_conn) {

		/* Abort any pending connection attempts (very unlikely) */

		while ((buf = skb_dequeue(&sock->sk->sk_receive_queue))) {
			tipc_reject_msg(buf, TIPC_ERR_NO_PORT);
			atomic_dec(&tipc_queue_size);
		}

		sock->state = SS_CONNECTING;
	}

	do {
		if (dest->addrtype == TIPC_ADDR_NAME) {
			if ((res = dest_name_check(dest, m)))
				goto exit;
			res = tipc_send2name(tsock->p->ref,
					     &dest->addr.name.name,
					     dest->addr.name.domain,
					     m->msg_iovlen,
					     m->msg_iov);
		}
		else if (dest->addrtype == TIPC_ADDR_ID) {
			res = tipc_send2port(tsock->p->ref,
					     &dest->addr.id,
					     m->msg_iovlen,
					     m->msg_iov);
		}
		else if (dest->addrtype == TIPC_ADDR_MCAST) {
			if (needs_conn) {
				res = -EOPNOTSUPP;
				goto exit;
			}
			if ((res = dest_name_check(dest, m)))
				goto exit;
			res = tipc_multicast(tsock->p->ref,
					     &dest->addr.nameseq,
					     0,
					     m->msg_iovlen,
					     m->msg_iov);
		}
		if (likely(res != -ELINKCONG)) {
exit:
			up(&tsock->sem);
			return res;
		}
		if (m->msg_flags & MSG_DONTWAIT) {
			res = -EWOULDBLOCK;
			goto exit;
		}
		if (wait_event_interruptible(*sock->sk->sk_sleep,
					     !tsock->p->congested)) {
		    res = -ERESTARTSYS;
		    goto exit;
		}
	} while (1);
}

/**
 * send_packet - send a connection-oriented message
 * @iocb: (unused)
 * @sock: socket structure
 * @m: message to send
 * @total_len: length of message
 *
 * Used for SOCK_SEQPACKET messages and SOCK_STREAM data.
 *
 * Returns the number of bytes sent on success, or errno otherwise
 */

static int send_packet(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *m, size_t total_len)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sockaddr_tipc *dest = (struct sockaddr_tipc *)m->msg_name;
	int res;

	/* Handle implied connection establishment */

	if (unlikely(dest))
		return send_msg(iocb, sock, m, total_len);

	if (down_interruptible(&tsock->sem)) {
		return -ERESTARTSYS;
	}

	do {
		if (unlikely(sock->state != SS_CONNECTED)) {
			if (sock->state == SS_DISCONNECTING)
				res = -EPIPE;
			else
				res = -ENOTCONN;
			goto exit;
		}

		res = tipc_send(tsock->p->ref, m->msg_iovlen, m->msg_iov);
		if (likely(res != -ELINKCONG)) {
exit:
			up(&tsock->sem);
			return res;
		}
		if (m->msg_flags & MSG_DONTWAIT) {
			res = -EWOULDBLOCK;
			goto exit;
		}
		if (wait_event_interruptible(*sock->sk->sk_sleep,
					     !tsock->p->congested)) {
		    res = -ERESTARTSYS;
		    goto exit;
		}
	} while (1);
}

/**
 * send_stream - send stream-oriented data
 * @iocb: (unused)
 * @sock: socket structure
 * @m: data to send
 * @total_len: total length of data to be sent
 *
 * Used for SOCK_STREAM data.
 *
 * Returns the number of bytes sent on success (or partial success),
 * or errno if no data sent
 */


static int send_stream(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *m, size_t total_len)
{
	struct tipc_port *tport;
	struct msghdr my_msg;
	struct iovec my_iov;
	struct iovec *curr_iov;
	int curr_iovlen;
	char __user *curr_start;
	u32 hdr_size;
	int curr_left;
	int bytes_to_send;
	int bytes_sent;
	int res;

	/* Handle special cases where there is no connection */

	if (unlikely(sock->state != SS_CONNECTED)) {
		if (sock->state == SS_UNCONNECTED)
			return send_packet(iocb, sock, m, total_len);
		else if (sock->state == SS_DISCONNECTING)
			return -EPIPE;
		else
			return -ENOTCONN;
	}

	if (unlikely(m->msg_name))
		return -EISCONN;

	/*
	 * Send each iovec entry using one or more messages
	 *
	 * Note: This algorithm is good for the most likely case
	 * (i.e. one large iovec entry), but could be improved to pass sets
	 * of small iovec entries into send_packet().
	 */

	curr_iov = m->msg_iov;
	curr_iovlen = m->msg_iovlen;
	my_msg.msg_iov = &my_iov;
	my_msg.msg_iovlen = 1;
	my_msg.msg_flags = m->msg_flags;
	my_msg.msg_name = NULL;
	bytes_sent = 0;

	tport = tipc_sk(sock->sk)->p;
	hdr_size = msg_hdr_sz(&tport->phdr);

	while (curr_iovlen--) {
		curr_start = curr_iov->iov_base;
		curr_left = curr_iov->iov_len;

		while (curr_left) {
			bytes_to_send = tport->max_pkt - hdr_size;
			if (bytes_to_send > TIPC_MAX_USER_MSG_SIZE)
				bytes_to_send = TIPC_MAX_USER_MSG_SIZE;
			if (curr_left < bytes_to_send)
				bytes_to_send = curr_left;
			my_iov.iov_base = curr_start;
			my_iov.iov_len = bytes_to_send;
			if ((res = send_packet(iocb, sock, &my_msg, 0)) < 0) {
				if (bytes_sent != 0)
					res = bytes_sent;
				return res;
			}
			curr_left -= bytes_to_send;
			curr_start += bytes_to_send;
			bytes_sent += bytes_to_send;
		}

		curr_iov++;
	}

	return bytes_sent;
}

/**
 * auto_connect - complete connection setup to a remote port
 * @sock: socket structure
 * @tsock: TIPC-specific socket structure
 * @msg: peer's response message
 *
 * Returns 0 on success, errno otherwise
 */

static int auto_connect(struct socket *sock, struct tipc_sock *tsock,
			struct tipc_msg *msg)
{
	struct tipc_portid peer;

	if (msg_errcode(msg)) {
		sock->state = SS_DISCONNECTING;
		return -ECONNREFUSED;
	}

	peer.ref = msg_origport(msg);
	peer.node = msg_orignode(msg);
	tipc_connect2port(tsock->p->ref, &peer);
	tipc_set_portimportance(tsock->p->ref, msg_importance(msg));
	sock->state = SS_CONNECTED;
	return 0;
}

/**
 * set_orig_addr - capture sender's address for received message
 * @m: descriptor for message info
 * @msg: received message header
 *
 * Note: Address is not captured if not requested by receiver.
 */

static void set_orig_addr(struct msghdr *m, struct tipc_msg *msg)
{
	struct sockaddr_tipc *addr = (struct sockaddr_tipc *)m->msg_name;

	if (addr) {
		addr->family = AF_TIPC;
		addr->addrtype = TIPC_ADDR_ID;
		addr->addr.id.ref = msg_origport(msg);
		addr->addr.id.node = msg_orignode(msg);
		addr->addr.name.domain = 0;   	/* could leave uninitialized */
		addr->scope = 0;   		/* could leave uninitialized */
		m->msg_namelen = sizeof(struct sockaddr_tipc);
	}
}

/**
 * anc_data_recv - optionally capture ancillary data for received message
 * @m: descriptor for message info
 * @msg: received message header
 * @tport: TIPC port associated with message
 *
 * Note: Ancillary data is not captured if not requested by receiver.
 *
 * Returns 0 if successful, otherwise errno
 */

static int anc_data_recv(struct msghdr *m, struct tipc_msg *msg,
				struct tipc_port *tport)
{
	u32 anc_data[3];
	u32 err;
	u32 dest_type;
	int has_name;
	int res;

	if (likely(m->msg_controllen == 0))
		return 0;

	/* Optionally capture errored message object(s) */

	err = msg ? msg_errcode(msg) : 0;
	if (unlikely(err)) {
		anc_data[0] = err;
		anc_data[1] = msg_data_sz(msg);
		if ((res = put_cmsg(m, SOL_TIPC, TIPC_ERRINFO, 8, anc_data)))
			return res;
		if (anc_data[1] &&
		    (res = put_cmsg(m, SOL_TIPC, TIPC_RETDATA, anc_data[1],
				    msg_data(msg))))
			return res;
	}

	/* Optionally capture message destination object */

	dest_type = msg ? msg_type(msg) : TIPC_DIRECT_MSG;
	switch (dest_type) {
	case TIPC_NAMED_MSG:
		has_name = 1;
		anc_data[0] = msg_nametype(msg);
		anc_data[1] = msg_namelower(msg);
		anc_data[2] = msg_namelower(msg);
		break;
	case TIPC_MCAST_MSG:
		has_name = 1;
		anc_data[0] = msg_nametype(msg);
		anc_data[1] = msg_namelower(msg);
		anc_data[2] = msg_nameupper(msg);
		break;
	case TIPC_CONN_MSG:
		has_name = (tport->conn_type != 0);
		anc_data[0] = tport->conn_type;
		anc_data[1] = tport->conn_instance;
		anc_data[2] = tport->conn_instance;
		break;
	default:
		has_name = 0;
	}
	if (has_name &&
	    (res = put_cmsg(m, SOL_TIPC, TIPC_DESTNAME, 12, anc_data)))
		return res;

	return 0;
}

/**
 * recv_msg - receive packet-oriented message
 * @iocb: (unused)
 * @m: descriptor for message info
 * @buf_len: total size of user buffer area
 * @flags: receive flags
 *
 * Used for SOCK_DGRAM, SOCK_RDM, and SOCK_SEQPACKET messages.
 * If the complete message doesn't fit in user area, truncate it.
 *
 * Returns size of returned message data, errno otherwise
 */

static int recv_msg(struct kiocb *iocb, struct socket *sock,
		    struct msghdr *m, size_t buf_len, int flags)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sk_buff *buf;
	struct tipc_msg *msg;
	unsigned int q_len;
	unsigned int sz;
	u32 err;
	int res;

	/* Currently doesn't support receiving into multiple iovec entries */

	if (m->msg_iovlen != 1)
		return -EOPNOTSUPP;

	/* Catch invalid receive attempts */

	if (unlikely(!buf_len))
		return -EINVAL;

	if (sock->type == SOCK_SEQPACKET) {
		if (unlikely(sock->state == SS_UNCONNECTED))
			return -ENOTCONN;
		if (unlikely((sock->state == SS_DISCONNECTING) &&
			     (skb_queue_len(&sock->sk->sk_receive_queue) == 0)))
			return -ENOTCONN;
	}

	/* Look for a message in receive queue; wait if necessary */

	if (unlikely(down_interruptible(&tsock->sem)))
		return -ERESTARTSYS;

restart:
	if (unlikely((skb_queue_len(&sock->sk->sk_receive_queue) == 0) &&
		     (flags & MSG_DONTWAIT))) {
		res = -EWOULDBLOCK;
		goto exit;
	}

	if ((res = wait_event_interruptible(
		*sock->sk->sk_sleep,
		((q_len = skb_queue_len(&sock->sk->sk_receive_queue)) ||
		 (sock->state == SS_DISCONNECTING))) )) {
		goto exit;
	}

	/* Catch attempt to receive on an already terminated connection */
	/* [THIS CHECK MAY OVERLAP WITH AN EARLIER CHECK] */

	if (!q_len) {
		res = -ENOTCONN;
		goto exit;
	}

	/* Get access to first message in receive queue */

	buf = skb_peek(&sock->sk->sk_receive_queue);
	msg = buf_msg(buf);
	sz = msg_data_sz(msg);
	err = msg_errcode(msg);

	/* Complete connection setup for an implied connect */

	if (unlikely(sock->state == SS_CONNECTING)) {
		if ((res = auto_connect(sock, tsock, msg)))
			goto exit;
	}

	/* Discard an empty non-errored message & try again */

	if ((!sz) && (!err)) {
		advance_queue(tsock);
		goto restart;
	}

	/* Capture sender's address (optional) */

	set_orig_addr(m, msg);

	/* Capture ancillary data (optional) */

	if ((res = anc_data_recv(m, msg, tsock->p)))
		goto exit;

	/* Capture message data (if valid) & compute return value (always) */

	if (!err) {
		if (unlikely(buf_len < sz)) {
			sz = buf_len;
			m->msg_flags |= MSG_TRUNC;
		}
		if (unlikely(copy_to_user(m->msg_iov->iov_base, msg_data(msg),
					  sz))) {
			res = -EFAULT;
			goto exit;
		}
		res = sz;
	} else {
		if ((sock->state == SS_READY) ||
		    ((err == TIPC_CONN_SHUTDOWN) || m->msg_control))
			res = 0;
		else
			res = -ECONNRESET;
	}

	/* Consume received message (optional) */

	if (likely(!(flags & MSG_PEEK))) {
		if (unlikely(++tsock->p->conn_unacked >= TIPC_FLOW_CONTROL_WIN))
			tipc_acknowledge(tsock->p->ref, tsock->p->conn_unacked);
		advance_queue(tsock);
	}
exit:
	up(&tsock->sem);
	return res;
}

/**
 * recv_stream - receive stream-oriented data
 * @iocb: (unused)
 * @m: descriptor for message info
 * @buf_len: total size of user buffer area
 * @flags: receive flags
 *
 * Used for SOCK_STREAM messages only.  If not enough data is available
 * will optionally wait for more; never truncates data.
 *
 * Returns size of returned message data, errno otherwise
 */

static int recv_stream(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *m, size_t buf_len, int flags)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sk_buff *buf;
	struct tipc_msg *msg;
	unsigned int q_len;
	unsigned int sz;
	int sz_to_copy;
	int sz_copied = 0;
	int needed;
	char __user *crs = m->msg_iov->iov_base;
	unsigned char *buf_crs;
	u32 err;
	int res;

	/* Currently doesn't support receiving into multiple iovec entries */

	if (m->msg_iovlen != 1)
		return -EOPNOTSUPP;

	/* Catch invalid receive attempts */

	if (unlikely(!buf_len))
		return -EINVAL;

	if (unlikely(sock->state == SS_DISCONNECTING)) {
		if (skb_queue_len(&sock->sk->sk_receive_queue) == 0)
			return -ENOTCONN;
	} else if (unlikely(sock->state != SS_CONNECTED))
		return -ENOTCONN;

	/* Look for a message in receive queue; wait if necessary */

	if (unlikely(down_interruptible(&tsock->sem)))
		return -ERESTARTSYS;

restart:
	if (unlikely((skb_queue_len(&sock->sk->sk_receive_queue) == 0) &&
		     (flags & MSG_DONTWAIT))) {
		res = -EWOULDBLOCK;
		goto exit;
	}

	if ((res = wait_event_interruptible(
		*sock->sk->sk_sleep,
		((q_len = skb_queue_len(&sock->sk->sk_receive_queue)) ||
		 (sock->state == SS_DISCONNECTING))) )) {
		goto exit;
	}

	/* Catch attempt to receive on an already terminated connection */
	/* [THIS CHECK MAY OVERLAP WITH AN EARLIER CHECK] */

	if (!q_len) {
		res = -ENOTCONN;
		goto exit;
	}

	/* Get access to first message in receive queue */

	buf = skb_peek(&sock->sk->sk_receive_queue);
	msg = buf_msg(buf);
	sz = msg_data_sz(msg);
	err = msg_errcode(msg);

	/* Discard an empty non-errored message & try again */

	if ((!sz) && (!err)) {
		advance_queue(tsock);
		goto restart;
	}

	/* Optionally capture sender's address & ancillary data of first msg */

	if (sz_copied == 0) {
		set_orig_addr(m, msg);
		if ((res = anc_data_recv(m, msg, tsock->p)))
			goto exit;
	}

	/* Capture message data (if valid) & compute return value (always) */

	if (!err) {
		buf_crs = (unsigned char *)(TIPC_SKB_CB(buf)->handle);
		sz = skb_tail_pointer(buf) - buf_crs;

		needed = (buf_len - sz_copied);
		sz_to_copy = (sz <= needed) ? sz : needed;
		if (unlikely(copy_to_user(crs, buf_crs, sz_to_copy))) {
			res = -EFAULT;
			goto exit;
		}
		sz_copied += sz_to_copy;

		if (sz_to_copy < sz) {
			if (!(flags & MSG_PEEK))
				TIPC_SKB_CB(buf)->handle = buf_crs + sz_to_copy;
			goto exit;
		}

		crs += sz_to_copy;
	} else {
		if (sz_copied != 0)
			goto exit; /* can't add error msg to valid data */

		if ((err == TIPC_CONN_SHUTDOWN) || m->msg_control)
			res = 0;
		else
			res = -ECONNRESET;
	}

	/* Consume received message (optional) */

	if (likely(!(flags & MSG_PEEK))) {
		if (unlikely(++tsock->p->conn_unacked >= TIPC_FLOW_CONTROL_WIN))
			tipc_acknowledge(tsock->p->ref, tsock->p->conn_unacked);
		advance_queue(tsock);
	}

	/* Loop around if more data is required */

	if ((sz_copied < buf_len)    /* didn't get all requested data */
	    && (flags & MSG_WAITALL) /* ... and need to wait for more */
	    && (!(flags & MSG_PEEK)) /* ... and aren't just peeking at data */
	    && (!err)                /* ... and haven't reached a FIN */
	    )
		goto restart;

exit:
	up(&tsock->sem);
	return sz_copied ? sz_copied : res;
}

/**
 * queue_overloaded - test if queue overload condition exists
 * @queue_size: current size of queue
 * @base: nominal maximum size of queue
 * @msg: message to be added to queue
 *
 * Returns 1 if queue is currently overloaded, 0 otherwise
 */

static int queue_overloaded(u32 queue_size, u32 base, struct tipc_msg *msg)
{
	u32 threshold;
	u32 imp = msg_importance(msg);

	if (imp == TIPC_LOW_IMPORTANCE)
		threshold = base;
	else if (imp == TIPC_MEDIUM_IMPORTANCE)
		threshold = base * 2;
	else if (imp == TIPC_HIGH_IMPORTANCE)
		threshold = base * 100;
	else
		return 0;

	if (msg_connected(msg))
		threshold *= 4;

	return (queue_size > threshold);
}

/**
 * async_disconnect - wrapper function used to disconnect port
 * @portref: TIPC port reference (passed as pointer-sized value)
 */

static void async_disconnect(unsigned long portref)
{
	tipc_disconnect((u32)portref);
}

/**
 * dispatch - handle arriving message
 * @tport: TIPC port that received message
 * @buf: message
 *
 * Called with port locked.  Must not take socket lock to avoid deadlock risk.
 *
 * Returns TIPC error status code (TIPC_OK if message is not to be rejected)
 */

static u32 dispatch(struct tipc_port *tport, struct sk_buff *buf)
{
	struct tipc_msg *msg = buf_msg(buf);
	struct tipc_sock *tsock = (struct tipc_sock *)tport->usr_handle;
	struct socket *sock;
	u32 recv_q_len;

	/* Reject message if socket is closing */

	if (!tsock)
		return TIPC_ERR_NO_PORT;

	/* Reject message if it is wrong sort of message for socket */

	/*
	 * WOULD IT BE BETTER TO JUST DISCARD THESE MESSAGES INSTEAD?
	 * "NO PORT" ISN'T REALLY THE RIGHT ERROR CODE, AND THERE MAY
	 * BE SECURITY IMPLICATIONS INHERENT IN REJECTING INVALID TRAFFIC
	 */
	sock = tsock->sk.sk_socket;
	if (sock->state == SS_READY) {
		if (msg_connected(msg)) {
			msg_dbg(msg, "dispatch filter 1\n");
			return TIPC_ERR_NO_PORT;
		}
	} else {
		if (msg_mcast(msg)) {
			msg_dbg(msg, "dispatch filter 2\n");
			return TIPC_ERR_NO_PORT;
		}
		if (sock->state == SS_CONNECTED) {
			if (!msg_connected(msg)) {
				msg_dbg(msg, "dispatch filter 3\n");
				return TIPC_ERR_NO_PORT;
			}
		}
		else if (sock->state == SS_CONNECTING) {
			if (!msg_connected(msg) && (msg_errcode(msg) == 0)) {
				msg_dbg(msg, "dispatch filter 4\n");
				return TIPC_ERR_NO_PORT;
			}
		}
		else if (sock->state == SS_LISTENING) {
			if (msg_connected(msg) || msg_errcode(msg)) {
				msg_dbg(msg, "dispatch filter 5\n");
				return TIPC_ERR_NO_PORT;
			}
		}
		else if (sock->state == SS_DISCONNECTING) {
			msg_dbg(msg, "dispatch filter 6\n");
			return TIPC_ERR_NO_PORT;
		}
		else /* (sock->state == SS_UNCONNECTED) */ {
			if (msg_connected(msg) || msg_errcode(msg)) {
				msg_dbg(msg, "dispatch filter 7\n");
				return TIPC_ERR_NO_PORT;
			}
		}
	}

	/* Reject message if there isn't room to queue it */

	if (unlikely((u32)atomic_read(&tipc_queue_size) >
		     OVERLOAD_LIMIT_BASE)) {
		if (queue_overloaded(atomic_read(&tipc_queue_size),
				     OVERLOAD_LIMIT_BASE, msg))
			return TIPC_ERR_OVERLOAD;
	}
	recv_q_len = skb_queue_len(&tsock->sk.sk_receive_queue);
	if (unlikely(recv_q_len > (OVERLOAD_LIMIT_BASE / 2))) {
		if (queue_overloaded(recv_q_len,
				     OVERLOAD_LIMIT_BASE / 2, msg))
			return TIPC_ERR_OVERLOAD;
	}

	/* Initiate connection termination for an incoming 'FIN' */

	if (unlikely(msg_errcode(msg) && (sock->state == SS_CONNECTED))) {
		sock->state = SS_DISCONNECTING;
		/* Note: Use signal since port lock is already taken! */
		tipc_k_signal((Handler)async_disconnect, tport->ref);
	}

	/* Enqueue message (finally!) */

	msg_dbg(msg,"<DISP<: ");
	TIPC_SKB_CB(buf)->handle = msg_data(msg);
	atomic_inc(&tipc_queue_size);
	skb_queue_tail(&sock->sk->sk_receive_queue, buf);

	if (waitqueue_active(sock->sk->sk_sleep))
		wake_up_interruptible(sock->sk->sk_sleep);
	return TIPC_OK;
}

/**
 * wakeupdispatch - wake up port after congestion
 * @tport: port to wakeup
 *
 * Called with port lock on.
 */

static void wakeupdispatch(struct tipc_port *tport)
{
	struct tipc_sock *tsock = (struct tipc_sock *)tport->usr_handle;

	if (waitqueue_active(tsock->sk.sk_sleep))
		wake_up_interruptible(tsock->sk.sk_sleep);
}

/**
 * connect - establish a connection to another TIPC port
 * @sock: socket structure
 * @dest: socket address for destination port
 * @destlen: size of socket address data structure
 * @flags: (unused)
 *
 * Returns 0 on success, errno otherwise
 */

static int connect(struct socket *sock, struct sockaddr *dest, int destlen,
		   int flags)
{
   struct tipc_sock *tsock = tipc_sk(sock->sk);
   struct sockaddr_tipc *dst = (struct sockaddr_tipc *)dest;
   struct msghdr m = {NULL,};
   struct sk_buff *buf;
   struct tipc_msg *msg;
   int res;

   /* For now, TIPC does not allow use of connect() with DGRAM or RDM types */

   if (sock->state == SS_READY)
	   return -EOPNOTSUPP;

   /* Issue Posix-compliant error code if socket is in the wrong state */

   if (sock->state == SS_LISTENING)
	   return -EOPNOTSUPP;
   if (sock->state == SS_CONNECTING)
	   return -EALREADY;
   if (sock->state != SS_UNCONNECTED)
	   return -EISCONN;

   /*
    * Reject connection attempt using multicast address
    *
    * Note: send_msg() validates the rest of the address fields,
    *       so there's no need to do it here
    */

   if (dst->addrtype == TIPC_ADDR_MCAST)
	   return -EINVAL;

   /* Send a 'SYN-' to destination */

   m.msg_name = dest;
   m.msg_namelen = destlen;
   if ((res = send_msg(NULL, sock, &m, 0)) < 0) {
	   sock->state = SS_DISCONNECTING;
	   return res;
   }

   if (down_interruptible(&tsock->sem))
	   return -ERESTARTSYS;

   /* Wait for destination's 'ACK' response */

   res = wait_event_interruptible_timeout(*sock->sk->sk_sleep,
					  skb_queue_len(&sock->sk->sk_receive_queue),
					  sock->sk->sk_rcvtimeo);
   buf = skb_peek(&sock->sk->sk_receive_queue);
   if (res > 0) {
	   msg = buf_msg(buf);
	   res = auto_connect(sock, tsock, msg);
	   if (!res) {
		   if (!msg_data_sz(msg))
			   advance_queue(tsock);
	   }
   } else {
	   if (res == 0) {
		   res = -ETIMEDOUT;
	   } else
		   { /* leave "res" unchanged */ }
	   sock->state = SS_DISCONNECTING;
   }

   up(&tsock->sem);
   return res;
}

/**
 * listen - allow socket to listen for incoming connections
 * @sock: socket structure
 * @len: (unused)
 *
 * Returns 0 on success, errno otherwise
 */

static int listen(struct socket *sock, int len)
{
	/* REQUIRES SOCKET LOCKING OF SOME SORT? */

	if (sock->state == SS_READY)
		return -EOPNOTSUPP;
	if (sock->state != SS_UNCONNECTED)
		return -EINVAL;
	sock->state = SS_LISTENING;
	return 0;
}

/**
 * accept - wait for connection request
 * @sock: listening socket
 * @newsock: new socket that is to be connected
 * @flags: file-related flags associated with socket
 *
 * Returns 0 on success, errno otherwise
 */

static int accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	struct sk_buff *buf;
	int res = -EFAULT;

	if (sock->state == SS_READY)
		return -EOPNOTSUPP;
	if (sock->state != SS_LISTENING)
		return -EINVAL;

	if (unlikely((skb_queue_len(&sock->sk->sk_receive_queue) == 0) &&
		     (flags & O_NONBLOCK)))
		return -EWOULDBLOCK;

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	if (wait_event_interruptible(*sock->sk->sk_sleep,
				     skb_queue_len(&sock->sk->sk_receive_queue))) {
		res = -ERESTARTSYS;
		goto exit;
	}
	buf = skb_peek(&sock->sk->sk_receive_queue);

	res = tipc_create(sock->sk->sk_net, newsock, 0);
	if (!res) {
		struct tipc_sock *new_tsock = tipc_sk(newsock->sk);
		struct tipc_portid id;
		struct tipc_msg *msg = buf_msg(buf);
		u32 new_ref = new_tsock->p->ref;

		id.ref = msg_origport(msg);
		id.node = msg_orignode(msg);
		tipc_connect2port(new_ref, &id);
		newsock->state = SS_CONNECTED;

		tipc_set_portimportance(new_ref, msg_importance(msg));
		if (msg_named(msg)) {
			new_tsock->p->conn_type = msg_nametype(msg);
			new_tsock->p->conn_instance = msg_nameinst(msg);
		}

	       /*
		 * Respond to 'SYN-' by discarding it & returning 'ACK'-.
		 * Respond to 'SYN+' by queuing it on new socket.
		 */

		msg_dbg(msg,"<ACC<: ");
		if (!msg_data_sz(msg)) {
			struct msghdr m = {NULL,};

			send_packet(NULL, newsock, &m, 0);
			advance_queue(tsock);
		} else {
			sock_lock(tsock);
			skb_dequeue(&sock->sk->sk_receive_queue);
			sock_unlock(tsock);
			skb_queue_head(&newsock->sk->sk_receive_queue, buf);
		}
	}
exit:
	up(&tsock->sem);
	return res;
}

/**
 * shutdown - shutdown socket connection
 * @sock: socket structure
 * @how: direction to close (unused; always treated as read + write)
 *
 * Terminates connection (if necessary), then purges socket's receive queue.
 *
 * Returns 0 on success, errno otherwise
 */

static int shutdown(struct socket *sock, int how)
{
	struct tipc_sock* tsock = tipc_sk(sock->sk);
	struct sk_buff *buf;
	int res;

	/* Could return -EINVAL for an invalid "how", but why bother? */

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	sock_lock(tsock);

	switch (sock->state) {
	case SS_CONNECTED:

		/* Send 'FIN+' or 'FIN-' message to peer */

		sock_unlock(tsock);
restart:
		if ((buf = skb_dequeue(&sock->sk->sk_receive_queue))) {
			atomic_dec(&tipc_queue_size);
			if (TIPC_SKB_CB(buf)->handle != msg_data(buf_msg(buf))) {
				buf_discard(buf);
				goto restart;
			}
			tipc_reject_msg(buf, TIPC_CONN_SHUTDOWN);
		}
		else {
			tipc_shutdown(tsock->p->ref);
		}
		sock_lock(tsock);

		/* fall through */

	case SS_DISCONNECTING:

		/* Discard any unreceived messages */

		while ((buf = skb_dequeue(&sock->sk->sk_receive_queue))) {
			atomic_dec(&tipc_queue_size);
			buf_discard(buf);
		}
		tsock->p->conn_unacked = 0;

		/* fall through */

	case SS_CONNECTING:
		sock->state = SS_DISCONNECTING;
		res = 0;
		break;

	default:
		res = -ENOTCONN;
	}

	sock_unlock(tsock);

	up(&tsock->sem);
	return res;
}

/**
 * setsockopt - set socket option
 * @sock: socket structure
 * @lvl: option level
 * @opt: option identifier
 * @ov: pointer to new option value
 * @ol: length of option value
 *
 * For stream sockets only, accepts and ignores all IPPROTO_TCP options
 * (to ease compatibility).
 *
 * Returns 0 on success, errno otherwise
 */

static int setsockopt(struct socket *sock,
		      int lvl, int opt, char __user *ov, int ol)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	u32 value;
	int res;

	if ((lvl == IPPROTO_TCP) && (sock->type == SOCK_STREAM))
		return 0;
	if (lvl != SOL_TIPC)
		return -ENOPROTOOPT;
	if (ol < sizeof(value))
		return -EINVAL;
	if ((res = get_user(value, (u32 __user *)ov)))
		return res;

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	switch (opt) {
	case TIPC_IMPORTANCE:
		res = tipc_set_portimportance(tsock->p->ref, value);
		break;
	case TIPC_SRC_DROPPABLE:
		if (sock->type != SOCK_STREAM)
			res = tipc_set_portunreliable(tsock->p->ref, value);
		else
			res = -ENOPROTOOPT;
		break;
	case TIPC_DEST_DROPPABLE:
		res = tipc_set_portunreturnable(tsock->p->ref, value);
		break;
	case TIPC_CONN_TIMEOUT:
		sock->sk->sk_rcvtimeo = (value * HZ / 1000);
		break;
	default:
		res = -EINVAL;
	}

	up(&tsock->sem);
	return res;
}

/**
 * getsockopt - get socket option
 * @sock: socket structure
 * @lvl: option level
 * @opt: option identifier
 * @ov: receptacle for option value
 * @ol: receptacle for length of option value
 *
 * For stream sockets only, returns 0 length result for all IPPROTO_TCP options
 * (to ease compatibility).
 *
 * Returns 0 on success, errno otherwise
 */

static int getsockopt(struct socket *sock,
		      int lvl, int opt, char __user *ov, int __user *ol)
{
	struct tipc_sock *tsock = tipc_sk(sock->sk);
	int len;
	u32 value;
	int res;

	if ((lvl == IPPROTO_TCP) && (sock->type == SOCK_STREAM))
		return put_user(0, ol);
	if (lvl != SOL_TIPC)
		return -ENOPROTOOPT;
	if ((res = get_user(len, ol)))
		return res;

	if (down_interruptible(&tsock->sem))
		return -ERESTARTSYS;

	switch (opt) {
	case TIPC_IMPORTANCE:
		res = tipc_portimportance(tsock->p->ref, &value);
		break;
	case TIPC_SRC_DROPPABLE:
		res = tipc_portunreliable(tsock->p->ref, &value);
		break;
	case TIPC_DEST_DROPPABLE:
		res = tipc_portunreturnable(tsock->p->ref, &value);
		break;
	case TIPC_CONN_TIMEOUT:
		value = (sock->sk->sk_rcvtimeo * 1000) / HZ;
		break;
	default:
		res = -EINVAL;
	}

	if (res) {
		/* "get" failed */
	}
	else if (len < sizeof(value)) {
		res = -EINVAL;
	}
	else if ((res = copy_to_user(ov, &value, sizeof(value)))) {
		/* couldn't return value */
	}
	else {
		res = put_user(sizeof(value), ol);
	}

	up(&tsock->sem);
	return res;
}

/**
 * Protocol switches for the various types of TIPC sockets
 */

static struct proto_ops msg_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= release,
	.bind		= bind,
	.connect	= connect,
	.socketpair	= sock_no_socketpair,
	.accept		= accept,
	.getname	= get_name,
	.poll		= poll,
	.ioctl		= sock_no_ioctl,
	.listen		= listen,
	.shutdown	= shutdown,
	.setsockopt	= setsockopt,
	.getsockopt	= getsockopt,
	.sendmsg	= send_msg,
	.recvmsg	= recv_msg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage
};

static struct proto_ops packet_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= release,
	.bind		= bind,
	.connect	= connect,
	.socketpair	= sock_no_socketpair,
	.accept		= accept,
	.getname	= get_name,
	.poll		= poll,
	.ioctl		= sock_no_ioctl,
	.listen		= listen,
	.shutdown	= shutdown,
	.setsockopt	= setsockopt,
	.getsockopt	= getsockopt,
	.sendmsg	= send_packet,
	.recvmsg	= recv_msg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage
};

static struct proto_ops stream_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.release	= release,
	.bind		= bind,
	.connect	= connect,
	.socketpair	= sock_no_socketpair,
	.accept		= accept,
	.getname	= get_name,
	.poll		= poll,
	.ioctl		= sock_no_ioctl,
	.listen		= listen,
	.shutdown	= shutdown,
	.setsockopt	= setsockopt,
	.getsockopt	= getsockopt,
	.sendmsg	= send_stream,
	.recvmsg	= recv_stream,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage
};

static struct net_proto_family tipc_family_ops = {
	.owner 		= THIS_MODULE,
	.family		= AF_TIPC,
	.create		= tipc_create
};

static struct proto tipc_proto = {
	.name		= "TIPC",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct tipc_sock)
};

/**
 * tipc_socket_init - initialize TIPC socket interface
 *
 * Returns 0 on success, errno otherwise
 */
int tipc_socket_init(void)
{
	int res;

	res = proto_register(&tipc_proto, 1);
	if (res) {
		err("Failed to register TIPC protocol type\n");
		goto out;
	}

	res = sock_register(&tipc_family_ops);
	if (res) {
		err("Failed to register TIPC socket type\n");
		proto_unregister(&tipc_proto);
		goto out;
	}

	sockets_enabled = 1;
 out:
	return res;
}

/**
 * tipc_socket_stop - stop TIPC socket interface
 */
void tipc_socket_stop(void)
{
	if (!sockets_enabled)
		return;

	sockets_enabled = 0;
	sock_unregister(tipc_family_ops.family);
	proto_unregister(&tipc_proto);
}

