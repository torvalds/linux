/*
 * Copyright (c) 2006, 2018 Oracle and/or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/in.h>
#include <linux/ipv6.h>
#include <linux/poll.h>
#include <net/sock.h>

#include "rds.h"

/* this is just used for stats gathering :/ */
static DEFINE_SPINLOCK(rds_sock_lock);
static unsigned long rds_sock_count;
static LIST_HEAD(rds_sock_list);
DECLARE_WAIT_QUEUE_HEAD(rds_poll_waitq);

/*
 * This is called as the final descriptor referencing this socket is closed.
 * We have to unbind the socket so that another socket can be bound to the
 * address it was using.
 *
 * We have to be careful about racing with the incoming path.  sock_orphan()
 * sets SOCK_DEAD and we use that as an indicator to the rx path that new
 * messages shouldn't be queued.
 */
static int rds_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct rds_sock *rs;

	if (!sk)
		goto out;

	rs = rds_sk_to_rs(sk);

	sock_orphan(sk);
	/* Note - rds_clear_recv_queue grabs rs_recv_lock, so
	 * that ensures the recv path has completed messing
	 * with the socket. */
	rds_clear_recv_queue(rs);
	rds_cong_remove_socket(rs);

	rds_remove_bound(rs);

	rds_send_drop_to(rs, NULL);
	rds_rdma_drop_keys(rs);
	rds_notify_queue_get(rs, NULL);
	rds_notify_msg_zcopy_purge(&rs->rs_zcookie_queue);

	spin_lock_bh(&rds_sock_lock);
	list_del_init(&rs->rs_item);
	rds_sock_count--;
	spin_unlock_bh(&rds_sock_lock);

	rds_trans_put(rs->rs_transport);

	sock->sk = NULL;
	sock_put(sk);
out:
	return 0;
}

/*
 * Careful not to race with rds_release -> sock_orphan which clears sk_sleep.
 * _bh() isn't OK here, we're called from interrupt handlers.  It's probably OK
 * to wake the waitqueue after sk_sleep is clear as we hold a sock ref, but
 * this seems more conservative.
 * NB - normally, one would use sk_callback_lock for this, but we can
 * get here from interrupts, whereas the network code grabs sk_callback_lock
 * with _lock_bh only - so relying on sk_callback_lock introduces livelocks.
 */
void rds_wake_sk_sleep(struct rds_sock *rs)
{
	unsigned long flags;

	read_lock_irqsave(&rs->rs_recv_lock, flags);
	__rds_wake_sk_sleep(rds_rs_to_sk(rs));
	read_unlock_irqrestore(&rs->rs_recv_lock, flags);
}

static int rds_getname(struct socket *sock, struct sockaddr *uaddr,
		       int peer)
{
	struct rds_sock *rs = rds_sk_to_rs(sock->sk);
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	int uaddr_len;

	/* racey, don't care */
	if (peer) {
		if (ipv6_addr_any(&rs->rs_conn_addr))
			return -ENOTCONN;

		if (ipv6_addr_v4mapped(&rs->rs_conn_addr)) {
			sin = (struct sockaddr_in *)uaddr;
			memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
			sin->sin_family = AF_INET;
			sin->sin_port = rs->rs_conn_port;
			sin->sin_addr.s_addr = rs->rs_conn_addr_v4;
			uaddr_len = sizeof(*sin);
		} else {
			sin6 = (struct sockaddr_in6 *)uaddr;
			sin6->sin6_family = AF_INET6;
			sin6->sin6_port = rs->rs_conn_port;
			sin6->sin6_addr = rs->rs_conn_addr;
			sin6->sin6_flowinfo = 0;
			/* scope_id is the same as in the bound address. */
			sin6->sin6_scope_id = rs->rs_bound_scope_id;
			uaddr_len = sizeof(*sin6);
		}
	} else {
		/* If socket is not yet bound and the socket is connected,
		 * set the return address family to be the same as the
		 * connected address, but with 0 address value.  If it is not
		 * connected, set the family to be AF_UNSPEC (value 0) and
		 * the address size to be that of an IPv4 address.
		 */
		if (ipv6_addr_any(&rs->rs_bound_addr)) {
			if (ipv6_addr_any(&rs->rs_conn_addr)) {
				sin = (struct sockaddr_in *)uaddr;
				memset(sin, 0, sizeof(*sin));
				sin->sin_family = AF_UNSPEC;
				return sizeof(*sin);
			}

#if IS_ENABLED(CONFIG_IPV6)
			if (!(ipv6_addr_type(&rs->rs_conn_addr) &
			      IPV6_ADDR_MAPPED)) {
				sin6 = (struct sockaddr_in6 *)uaddr;
				memset(sin6, 0, sizeof(*sin6));
				sin6->sin6_family = AF_INET6;
				return sizeof(*sin6);
			}
#endif

			sin = (struct sockaddr_in *)uaddr;
			memset(sin, 0, sizeof(*sin));
			sin->sin_family = AF_INET;
			return sizeof(*sin);
		}
		if (ipv6_addr_v4mapped(&rs->rs_bound_addr)) {
			sin = (struct sockaddr_in *)uaddr;
			memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
			sin->sin_family = AF_INET;
			sin->sin_port = rs->rs_bound_port;
			sin->sin_addr.s_addr = rs->rs_bound_addr_v4;
			uaddr_len = sizeof(*sin);
		} else {
			sin6 = (struct sockaddr_in6 *)uaddr;
			sin6->sin6_family = AF_INET6;
			sin6->sin6_port = rs->rs_bound_port;
			sin6->sin6_addr = rs->rs_bound_addr;
			sin6->sin6_flowinfo = 0;
			sin6->sin6_scope_id = rs->rs_bound_scope_id;
			uaddr_len = sizeof(*sin6);
		}
	}

	return uaddr_len;
}

/*
 * RDS' poll is without a doubt the least intuitive part of the interface,
 * as EPOLLIN and EPOLLOUT do not behave entirely as you would expect from
 * a network protocol.
 *
 * EPOLLIN is asserted if
 *  -	there is data on the receive queue.
 *  -	to signal that a previously congested destination may have become
 *	uncongested
 *  -	A notification has been queued to the socket (this can be a congestion
 *	update, or a RDMA completion, or a MSG_ZEROCOPY completion).
 *
 * EPOLLOUT is asserted if there is room on the send queue. This does not mean
 * however, that the next sendmsg() call will succeed. If the application tries
 * to send to a congested destination, the system call may still fail (and
 * return ENOBUFS).
 */
static __poll_t rds_poll(struct file *file, struct socket *sock,
			     poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	__poll_t mask = 0;
	unsigned long flags;

	poll_wait(file, sk_sleep(sk), wait);

	if (rs->rs_seen_congestion)
		poll_wait(file, &rds_poll_waitq, wait);

	read_lock_irqsave(&rs->rs_recv_lock, flags);
	if (!rs->rs_cong_monitor) {
		/* When a congestion map was updated, we signal EPOLLIN for
		 * "historical" reasons. Applications can also poll for
		 * WRBAND instead. */
		if (rds_cong_updated_since(&rs->rs_cong_track))
			mask |= (EPOLLIN | EPOLLRDNORM | EPOLLWRBAND);
	} else {
		spin_lock(&rs->rs_lock);
		if (rs->rs_cong_notify)
			mask |= (EPOLLIN | EPOLLRDNORM);
		spin_unlock(&rs->rs_lock);
	}
	if (!list_empty(&rs->rs_recv_queue) ||
	    !list_empty(&rs->rs_notify_queue) ||
	    !list_empty(&rs->rs_zcookie_queue.zcookie_head))
		mask |= (EPOLLIN | EPOLLRDNORM);
	if (rs->rs_snd_bytes < rds_sk_sndbuf(rs))
		mask |= (EPOLLOUT | EPOLLWRNORM);
	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;
	read_unlock_irqrestore(&rs->rs_recv_lock, flags);

	/* clear state any time we wake a seen-congested socket */
	if (mask)
		rs->rs_seen_congestion = 0;

	return mask;
}

static int rds_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int rds_cancel_sent_to(struct rds_sock *rs, char __user *optval,
			      int len)
{
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	int ret = 0;

	/* racing with another thread binding seems ok here */
	if (ipv6_addr_any(&rs->rs_bound_addr)) {
		ret = -ENOTCONN; /* XXX not a great errno */
		goto out;
	}

	if (len < sizeof(struct sockaddr_in)) {
		ret = -EINVAL;
		goto out;
	} else if (len < sizeof(struct sockaddr_in6)) {
		/* Assume IPv4 */
		if (copy_from_user(&sin, optval, sizeof(struct sockaddr_in))) {
			ret = -EFAULT;
			goto out;
		}
		ipv6_addr_set_v4mapped(sin.sin_addr.s_addr, &sin6.sin6_addr);
		sin6.sin6_port = sin.sin_port;
	} else {
		if (copy_from_user(&sin6, optval,
				   sizeof(struct sockaddr_in6))) {
			ret = -EFAULT;
			goto out;
		}
	}

	rds_send_drop_to(rs, &sin6);
out:
	return ret;
}

static int rds_set_bool_option(unsigned char *optvar, char __user *optval,
			       int optlen)
{
	int value;

	if (optlen < sizeof(int))
		return -EINVAL;
	if (get_user(value, (int __user *) optval))
		return -EFAULT;
	*optvar = !!value;
	return 0;
}

static int rds_cong_monitor(struct rds_sock *rs, char __user *optval,
			    int optlen)
{
	int ret;

	ret = rds_set_bool_option(&rs->rs_cong_monitor, optval, optlen);
	if (ret == 0) {
		if (rs->rs_cong_monitor) {
			rds_cong_add_socket(rs);
		} else {
			rds_cong_remove_socket(rs);
			rs->rs_cong_mask = 0;
			rs->rs_cong_notify = 0;
		}
	}
	return ret;
}

static int rds_set_transport(struct rds_sock *rs, char __user *optval,
			     int optlen)
{
	int t_type;

	if (rs->rs_transport)
		return -EOPNOTSUPP; /* previously attached to transport */

	if (optlen != sizeof(int))
		return -EINVAL;

	if (copy_from_user(&t_type, (int __user *)optval, sizeof(t_type)))
		return -EFAULT;

	if (t_type < 0 || t_type >= RDS_TRANS_COUNT)
		return -EINVAL;

	rs->rs_transport = rds_trans_get(t_type);

	return rs->rs_transport ? 0 : -ENOPROTOOPT;
}

static int rds_enable_recvtstamp(struct sock *sk, char __user *optval,
				 int optlen, int optname)
{
	int val, valbool;

	if (optlen != sizeof(int))
		return -EFAULT;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	valbool = val ? 1 : 0;

	if (optname == SO_TIMESTAMP_NEW)
		sock_set_flag(sk, SOCK_TSTAMP_NEW);

	if (valbool)
		sock_set_flag(sk, SOCK_RCVTSTAMP);
	else
		sock_reset_flag(sk, SOCK_RCVTSTAMP);

	return 0;
}

static int rds_recv_track_latency(struct rds_sock *rs, char __user *optval,
				  int optlen)
{
	struct rds_rx_trace_so trace;
	int i;

	if (optlen != sizeof(struct rds_rx_trace_so))
		return -EFAULT;

	if (copy_from_user(&trace, optval, sizeof(trace)))
		return -EFAULT;

	if (trace.rx_traces > RDS_MSG_RX_DGRAM_TRACE_MAX)
		return -EFAULT;

	rs->rs_rx_traces = trace.rx_traces;
	for (i = 0; i < rs->rs_rx_traces; i++) {
		if (trace.rx_trace_pos[i] > RDS_MSG_RX_DGRAM_TRACE_MAX) {
			rs->rs_rx_traces = 0;
			return -EFAULT;
		}
		rs->rs_rx_trace[i] = trace.rx_trace_pos[i];
	}

	return 0;
}

static int rds_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct rds_sock *rs = rds_sk_to_rs(sock->sk);
	int ret;

	if (level != SOL_RDS) {
		ret = -ENOPROTOOPT;
		goto out;
	}

	switch (optname) {
	case RDS_CANCEL_SENT_TO:
		ret = rds_cancel_sent_to(rs, optval, optlen);
		break;
	case RDS_GET_MR:
		ret = rds_get_mr(rs, optval, optlen);
		break;
	case RDS_GET_MR_FOR_DEST:
		ret = rds_get_mr_for_dest(rs, optval, optlen);
		break;
	case RDS_FREE_MR:
		ret = rds_free_mr(rs, optval, optlen);
		break;
	case RDS_RECVERR:
		ret = rds_set_bool_option(&rs->rs_recverr, optval, optlen);
		break;
	case RDS_CONG_MONITOR:
		ret = rds_cong_monitor(rs, optval, optlen);
		break;
	case SO_RDS_TRANSPORT:
		lock_sock(sock->sk);
		ret = rds_set_transport(rs, optval, optlen);
		release_sock(sock->sk);
		break;
	case SO_TIMESTAMP_OLD:
	case SO_TIMESTAMP_NEW:
		lock_sock(sock->sk);
		ret = rds_enable_recvtstamp(sock->sk, optval, optlen, optname);
		release_sock(sock->sk);
		break;
	case SO_RDS_MSG_RXPATH_LATENCY:
		ret = rds_recv_track_latency(rs, optval, optlen);
		break;
	default:
		ret = -ENOPROTOOPT;
	}
out:
	return ret;
}

static int rds_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct rds_sock *rs = rds_sk_to_rs(sock->sk);
	int ret = -ENOPROTOOPT, len;
	int trans;

	if (level != SOL_RDS)
		goto out;

	if (get_user(len, optlen)) {
		ret = -EFAULT;
		goto out;
	}

	switch (optname) {
	case RDS_INFO_FIRST ... RDS_INFO_LAST:
		ret = rds_info_getsockopt(sock, optname, optval,
					  optlen);
		break;

	case RDS_RECVERR:
		if (len < sizeof(int))
			ret = -EINVAL;
		else
		if (put_user(rs->rs_recverr, (int __user *) optval) ||
		    put_user(sizeof(int), optlen))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	case SO_RDS_TRANSPORT:
		if (len < sizeof(int)) {
			ret = -EINVAL;
			break;
		}
		trans = (rs->rs_transport ? rs->rs_transport->t_type :
			 RDS_TRANS_NONE); /* unbound */
		if (put_user(trans, (int __user *)optval) ||
		    put_user(sizeof(int), optlen))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	default:
		break;
	}

out:
	return ret;

}

static int rds_connect(struct socket *sock, struct sockaddr *uaddr,
		       int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_in *sin;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	int ret = 0;

	lock_sock(sk);

	switch (uaddr->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)uaddr;
		if (addr_len < sizeof(struct sockaddr_in)) {
			ret = -EINVAL;
			break;
		}
		if (sin->sin_addr.s_addr == htonl(INADDR_ANY)) {
			ret = -EDESTADDRREQ;
			break;
		}
		if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr)) ||
		    sin->sin_addr.s_addr == htonl(INADDR_BROADCAST)) {
			ret = -EINVAL;
			break;
		}
		ipv6_addr_set_v4mapped(sin->sin_addr.s_addr, &rs->rs_conn_addr);
		rs->rs_conn_port = sin->sin_port;
		break;

#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6: {
		struct sockaddr_in6 *sin6;
		int addr_type;

		sin6 = (struct sockaddr_in6 *)uaddr;
		if (addr_len < sizeof(struct sockaddr_in6)) {
			ret = -EINVAL;
			break;
		}
		addr_type = ipv6_addr_type(&sin6->sin6_addr);
		if (!(addr_type & IPV6_ADDR_UNICAST)) {
			__be32 addr4;

			if (!(addr_type & IPV6_ADDR_MAPPED)) {
				ret = -EPROTOTYPE;
				break;
			}

			/* It is a mapped address.  Need to do some sanity
			 * checks.
			 */
			addr4 = sin6->sin6_addr.s6_addr32[3];
			if (addr4 == htonl(INADDR_ANY) ||
			    addr4 == htonl(INADDR_BROADCAST) ||
			    IN_MULTICAST(ntohl(addr4))) {
				ret = -EPROTOTYPE;
				break;
			}
		}

		if (addr_type & IPV6_ADDR_LINKLOCAL) {
			/* If socket is arleady bound to a link local address,
			 * the peer address must be on the same link.
			 */
			if (sin6->sin6_scope_id == 0 ||
			    (!ipv6_addr_any(&rs->rs_bound_addr) &&
			     rs->rs_bound_scope_id &&
			     sin6->sin6_scope_id != rs->rs_bound_scope_id)) {
				ret = -EINVAL;
				break;
			}
			/* Remember the connected address scope ID.  It will
			 * be checked against the binding local address when
			 * the socket is bound.
			 */
			rs->rs_bound_scope_id = sin6->sin6_scope_id;
		}
		rs->rs_conn_addr = sin6->sin6_addr;
		rs->rs_conn_port = sin6->sin6_port;
		break;
	}
#endif

	default:
		ret = -EAFNOSUPPORT;
		break;
	}

	release_sock(sk);
	return ret;
}

static struct proto rds_proto = {
	.name	  = "RDS",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct rds_sock),
};

static const struct proto_ops rds_proto_ops = {
	.family =	AF_RDS,
	.owner =	THIS_MODULE,
	.release =	rds_release,
	.bind =		rds_bind,
	.connect =	rds_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	rds_getname,
	.poll =		rds_poll,
	.ioctl =	rds_ioctl,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	rds_setsockopt,
	.getsockopt =	rds_getsockopt,
	.sendmsg =	rds_sendmsg,
	.recvmsg =	rds_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

static void rds_sock_destruct(struct sock *sk)
{
	struct rds_sock *rs = rds_sk_to_rs(sk);

	WARN_ON((&rs->rs_item != rs->rs_item.next ||
		 &rs->rs_item != rs->rs_item.prev));
}

static int __rds_create(struct socket *sock, struct sock *sk, int protocol)
{
	struct rds_sock *rs;

	sock_init_data(sock, sk);
	sock->ops		= &rds_proto_ops;
	sk->sk_protocol		= protocol;
	sk->sk_destruct		= rds_sock_destruct;

	rs = rds_sk_to_rs(sk);
	spin_lock_init(&rs->rs_lock);
	rwlock_init(&rs->rs_recv_lock);
	INIT_LIST_HEAD(&rs->rs_send_queue);
	INIT_LIST_HEAD(&rs->rs_recv_queue);
	INIT_LIST_HEAD(&rs->rs_notify_queue);
	INIT_LIST_HEAD(&rs->rs_cong_list);
	rds_message_zcopy_queue_init(&rs->rs_zcookie_queue);
	spin_lock_init(&rs->rs_rdma_lock);
	rs->rs_rdma_keys = RB_ROOT;
	rs->rs_rx_traces = 0;

	spin_lock_bh(&rds_sock_lock);
	list_add_tail(&rs->rs_item, &rds_sock_list);
	rds_sock_count++;
	spin_unlock_bh(&rds_sock_lock);

	return 0;
}

static int rds_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;

	if (sock->type != SOCK_SEQPACKET || protocol)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(net, AF_RDS, GFP_ATOMIC, &rds_proto, kern);
	if (!sk)
		return -ENOMEM;

	return __rds_create(sock, sk, protocol);
}

void rds_sock_addref(struct rds_sock *rs)
{
	sock_hold(rds_rs_to_sk(rs));
}

void rds_sock_put(struct rds_sock *rs)
{
	sock_put(rds_rs_to_sk(rs));
}

static const struct net_proto_family rds_family_ops = {
	.family =	AF_RDS,
	.create =	rds_create,
	.owner	=	THIS_MODULE,
};

static void rds_sock_inc_info(struct socket *sock, unsigned int len,
			      struct rds_info_iterator *iter,
			      struct rds_info_lengths *lens)
{
	struct rds_sock *rs;
	struct rds_incoming *inc;
	unsigned int total = 0;

	len /= sizeof(struct rds_info_message);

	spin_lock_bh(&rds_sock_lock);

	list_for_each_entry(rs, &rds_sock_list, rs_item) {
		read_lock(&rs->rs_recv_lock);

		/* XXX too lazy to maintain counts.. */
		list_for_each_entry(inc, &rs->rs_recv_queue, i_item) {
			total++;
			if (total <= len)
				rds_inc_info_copy(inc, iter,
						  inc->i_saddr.s6_addr32[3],
						  rs->rs_bound_addr_v4,
						  1);
		}

		read_unlock(&rs->rs_recv_lock);
	}

	spin_unlock_bh(&rds_sock_lock);

	lens->nr = total;
	lens->each = sizeof(struct rds_info_message);
}

static void rds_sock_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens)
{
	struct rds_info_socket sinfo;
	struct rds_sock *rs;

	len /= sizeof(struct rds_info_socket);

	spin_lock_bh(&rds_sock_lock);

	if (len < rds_sock_count)
		goto out;

	list_for_each_entry(rs, &rds_sock_list, rs_item) {
		sinfo.sndbuf = rds_sk_sndbuf(rs);
		sinfo.rcvbuf = rds_sk_rcvbuf(rs);
		sinfo.bound_addr = rs->rs_bound_addr_v4;
		sinfo.connected_addr = rs->rs_conn_addr_v4;
		sinfo.bound_port = rs->rs_bound_port;
		sinfo.connected_port = rs->rs_conn_port;
		sinfo.inum = sock_i_ino(rds_rs_to_sk(rs));

		rds_info_copy(iter, &sinfo, sizeof(sinfo));
	}

out:
	lens->nr = rds_sock_count;
	lens->each = sizeof(struct rds_info_socket);

	spin_unlock_bh(&rds_sock_lock);
}

static void rds_exit(void)
{
	sock_unregister(rds_family_ops.family);
	proto_unregister(&rds_proto);
	rds_conn_exit();
	rds_cong_exit();
	rds_sysctl_exit();
	rds_threads_exit();
	rds_stats_exit();
	rds_page_exit();
	rds_bind_lock_destroy();
	rds_info_deregister_func(RDS_INFO_SOCKETS, rds_sock_info);
	rds_info_deregister_func(RDS_INFO_RECV_MESSAGES, rds_sock_inc_info);
}
module_exit(rds_exit);

u32 rds_gen_num;

static int rds_init(void)
{
	int ret;

	net_get_random_once(&rds_gen_num, sizeof(rds_gen_num));

	ret = rds_bind_lock_init();
	if (ret)
		goto out;

	ret = rds_conn_init();
	if (ret)
		goto out_bind;

	ret = rds_threads_init();
	if (ret)
		goto out_conn;
	ret = rds_sysctl_init();
	if (ret)
		goto out_threads;
	ret = rds_stats_init();
	if (ret)
		goto out_sysctl;
	ret = proto_register(&rds_proto, 1);
	if (ret)
		goto out_stats;
	ret = sock_register(&rds_family_ops);
	if (ret)
		goto out_proto;

	rds_info_register_func(RDS_INFO_SOCKETS, rds_sock_info);
	rds_info_register_func(RDS_INFO_RECV_MESSAGES, rds_sock_inc_info);

	goto out;

out_proto:
	proto_unregister(&rds_proto);
out_stats:
	rds_stats_exit();
out_sysctl:
	rds_sysctl_exit();
out_threads:
	rds_threads_exit();
out_conn:
	rds_conn_exit();
	rds_cong_exit();
	rds_page_exit();
out_bind:
	rds_bind_lock_destroy();
out:
	return ret;
}
module_init(rds_init);

#define DRV_VERSION     "4.0"
#define DRV_RELDATE     "Feb 12, 2009"

MODULE_AUTHOR("Oracle Corporation <rds-devel@oss.oracle.com>");
MODULE_DESCRIPTION("RDS: Reliable Datagram Sockets"
		   " v" DRV_VERSION " (" DRV_RELDATE ")");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS_NETPROTO(PF_RDS);
