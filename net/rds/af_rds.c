/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
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
#include <linux/in.h>
#include <linux/poll.h>
#include <net/sock.h>

#include "rds.h"
#include "rdma.h"

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
	unsigned long flags;

	if (sk == NULL)
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

	spin_lock_irqsave(&rds_sock_lock, flags);
	list_del_init(&rs->rs_item);
	rds_sock_count--;
	spin_unlock_irqrestore(&rds_sock_lock, flags);

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
		       int *uaddr_len, int peer)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;
	struct rds_sock *rs = rds_sk_to_rs(sock->sk);

	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));

	/* racey, don't care */
	if (peer) {
		if (!rs->rs_conn_addr)
			return -ENOTCONN;

		sin->sin_port = rs->rs_conn_port;
		sin->sin_addr.s_addr = rs->rs_conn_addr;
	} else {
		sin->sin_port = rs->rs_bound_port;
		sin->sin_addr.s_addr = rs->rs_bound_addr;
	}

	sin->sin_family = AF_INET;

	*uaddr_len = sizeof(*sin);
	return 0;
}

/*
 * RDS' poll is without a doubt the least intuitive part of the interface,
 * as POLLIN and POLLOUT do not behave entirely as you would expect from
 * a network protocol.
 *
 * POLLIN is asserted if
 *  -	there is data on the receive queue.
 *  -	to signal that a previously congested destination may have become
 *	uncongested
 *  -	A notification has been queued to the socket (this can be a congestion
 *	update, or a RDMA completion).
 *
 * POLLOUT is asserted if there is room on the send queue. This does not mean
 * however, that the next sendmsg() call will succeed. If the application tries
 * to send to a congested destination, the system call may still fail (and
 * return ENOBUFS).
 */
static unsigned int rds_poll(struct file *file, struct socket *sock,
			     poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	unsigned int mask = 0;
	unsigned long flags;

	poll_wait(file, sk->sk_sleep, wait);

	poll_wait(file, &rds_poll_waitq, wait);

	read_lock_irqsave(&rs->rs_recv_lock, flags);
	if (!rs->rs_cong_monitor) {
		/* When a congestion map was updated, we signal POLLIN for
		 * "historical" reasons. Applications can also poll for
		 * WRBAND instead. */
		if (rds_cong_updated_since(&rs->rs_cong_track))
			mask |= (POLLIN | POLLRDNORM | POLLWRBAND);
	} else {
		spin_lock(&rs->rs_lock);
		if (rs->rs_cong_notify)
			mask |= (POLLIN | POLLRDNORM);
		spin_unlock(&rs->rs_lock);
	}
	if (!list_empty(&rs->rs_recv_queue) ||
	    !list_empty(&rs->rs_notify_queue))
		mask |= (POLLIN | POLLRDNORM);
	if (rs->rs_snd_bytes < rds_sk_sndbuf(rs))
		mask |= (POLLOUT | POLLWRNORM);
	read_unlock_irqrestore(&rs->rs_recv_lock, flags);

	return mask;
}

static int rds_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int rds_cancel_sent_to(struct rds_sock *rs, char __user *optval,
			      int len)
{
	struct sockaddr_in sin;
	int ret = 0;

	/* racing with another thread binding seems ok here */
	if (rs->rs_bound_addr == 0) {
		ret = -ENOTCONN; /* XXX not a great errno */
		goto out;
	}

	if (len < sizeof(struct sockaddr_in)) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(&sin, optval, sizeof(sin))) {
		ret = -EFAULT;
		goto out;
	}

	rds_send_drop_to(rs, &sin);
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
	struct sockaddr_in *sin = (struct sockaddr_in *)uaddr;
	struct rds_sock *rs = rds_sk_to_rs(sk);
	int ret = 0;

	lock_sock(sk);

	if (addr_len != sizeof(struct sockaddr_in)) {
		ret = -EINVAL;
		goto out;
	}

	if (sin->sin_family != AF_INET) {
		ret = -EAFNOSUPPORT;
		goto out;
	}

	if (sin->sin_addr.s_addr == htonl(INADDR_ANY)) {
		ret = -EDESTADDRREQ;
		goto out;
	}

	rs->rs_conn_addr = sin->sin_addr.s_addr;
	rs->rs_conn_port = sin->sin_port;

out:
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

static int __rds_create(struct socket *sock, struct sock *sk, int protocol)
{
	unsigned long flags;
	struct rds_sock *rs;

	sock_init_data(sock, sk);
	sock->ops		= &rds_proto_ops;
	sk->sk_protocol		= protocol;

	rs = rds_sk_to_rs(sk);
	spin_lock_init(&rs->rs_lock);
	rwlock_init(&rs->rs_recv_lock);
	INIT_LIST_HEAD(&rs->rs_send_queue);
	INIT_LIST_HEAD(&rs->rs_recv_queue);
	INIT_LIST_HEAD(&rs->rs_notify_queue);
	INIT_LIST_HEAD(&rs->rs_cong_list);
	spin_lock_init(&rs->rs_rdma_lock);
	rs->rs_rdma_keys = RB_ROOT;

	spin_lock_irqsave(&rds_sock_lock, flags);
	list_add_tail(&rs->rs_item, &rds_sock_list);
	rds_sock_count++;
	spin_unlock_irqrestore(&rds_sock_lock, flags);

	return 0;
}

static int rds_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;

	if (sock->type != SOCK_SEQPACKET || protocol)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(net, AF_RDS, GFP_ATOMIC, &rds_proto);
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
	struct sock *sk;
	struct rds_incoming *inc;
	unsigned long flags;
	unsigned int total = 0;

	len /= sizeof(struct rds_info_message);

	spin_lock_irqsave(&rds_sock_lock, flags);

	list_for_each_entry(rs, &rds_sock_list, rs_item) {
		sk = rds_rs_to_sk(rs);
		read_lock(&rs->rs_recv_lock);

		/* XXX too lazy to maintain counts.. */
		list_for_each_entry(inc, &rs->rs_recv_queue, i_item) {
			total++;
			if (total <= len)
				rds_inc_info_copy(inc, iter, inc->i_saddr,
						  rs->rs_bound_addr, 1);
		}

		read_unlock(&rs->rs_recv_lock);
	}

	spin_unlock_irqrestore(&rds_sock_lock, flags);

	lens->nr = total;
	lens->each = sizeof(struct rds_info_message);
}

static void rds_sock_info(struct socket *sock, unsigned int len,
			  struct rds_info_iterator *iter,
			  struct rds_info_lengths *lens)
{
	struct rds_info_socket sinfo;
	struct rds_sock *rs;
	unsigned long flags;

	len /= sizeof(struct rds_info_socket);

	spin_lock_irqsave(&rds_sock_lock, flags);

	if (len < rds_sock_count)
		goto out;

	list_for_each_entry(rs, &rds_sock_list, rs_item) {
		sinfo.sndbuf = rds_sk_sndbuf(rs);
		sinfo.rcvbuf = rds_sk_rcvbuf(rs);
		sinfo.bound_addr = rs->rs_bound_addr;
		sinfo.connected_addr = rs->rs_conn_addr;
		sinfo.bound_port = rs->rs_bound_port;
		sinfo.connected_port = rs->rs_conn_port;
		sinfo.inum = sock_i_ino(rds_rs_to_sk(rs));

		rds_info_copy(iter, &sinfo, sizeof(sinfo));
	}

out:
	lens->nr = rds_sock_count;
	lens->each = sizeof(struct rds_info_socket);

	spin_unlock_irqrestore(&rds_sock_lock, flags);
}

static void __exit rds_exit(void)
{
	sock_unregister(rds_family_ops.family);
	proto_unregister(&rds_proto);
	rds_conn_exit();
	rds_cong_exit();
	rds_sysctl_exit();
	rds_threads_exit();
	rds_stats_exit();
	rds_page_exit();
	rds_info_deregister_func(RDS_INFO_SOCKETS, rds_sock_info);
	rds_info_deregister_func(RDS_INFO_RECV_MESSAGES, rds_sock_inc_info);
}
module_exit(rds_exit);

static int __init rds_init(void)
{
	int ret;

	ret = rds_conn_init();
	if (ret)
		goto out;
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
