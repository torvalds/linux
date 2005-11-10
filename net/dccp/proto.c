/*
 *  net/dccp/proto.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/dccp.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/random.h>
#include <net/checksum.h>

#include <net/inet_common.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/sock.h>
#include <net/xfrm.h>

#include <asm/semaphore.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/dccp.h>

#include "ccid.h"
#include "dccp.h"

DEFINE_SNMP_STAT(struct dccp_mib, dccp_statistics) __read_mostly;

atomic_t dccp_orphan_count = ATOMIC_INIT(0);

static struct net_protocol dccp_protocol = {
	.handler	= dccp_v4_rcv,
	.err_handler	= dccp_v4_err,
};

const char *dccp_packet_name(const int type)
{
	static const char *dccp_packet_names[] = {
		[DCCP_PKT_REQUEST]  = "REQUEST",
		[DCCP_PKT_RESPONSE] = "RESPONSE",
		[DCCP_PKT_DATA]	    = "DATA",
		[DCCP_PKT_ACK]	    = "ACK",
		[DCCP_PKT_DATAACK]  = "DATAACK",
		[DCCP_PKT_CLOSEREQ] = "CLOSEREQ",
		[DCCP_PKT_CLOSE]    = "CLOSE",
		[DCCP_PKT_RESET]    = "RESET",
		[DCCP_PKT_SYNC]	    = "SYNC",
		[DCCP_PKT_SYNCACK]  = "SYNCACK",
	};

	if (type >= DCCP_NR_PKT_TYPES)
		return "INVALID";
	else
		return dccp_packet_names[type];
}

EXPORT_SYMBOL_GPL(dccp_packet_name);

const char *dccp_state_name(const int state)
{
	static char *dccp_state_names[] = {
	[DCCP_OPEN]	  = "OPEN",
	[DCCP_REQUESTING] = "REQUESTING",
	[DCCP_PARTOPEN]	  = "PARTOPEN",
	[DCCP_LISTEN]	  = "LISTEN",
	[DCCP_RESPOND]	  = "RESPOND",
	[DCCP_CLOSING]	  = "CLOSING",
	[DCCP_TIME_WAIT]  = "TIME_WAIT",
	[DCCP_CLOSED]	  = "CLOSED",
	};

	if (state >= DCCP_MAX_STATES)
		return "INVALID STATE!";
	else
		return dccp_state_names[state];
}

EXPORT_SYMBOL_GPL(dccp_state_name);

static inline int dccp_listen_start(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);

	dp->dccps_role = DCCP_ROLE_LISTEN;
	/*
	 * Apps need to use setsockopt(DCCP_SOCKOPT_SERVICE)
	 * before calling listen()
	 */
	if (dccp_service_not_initialized(sk))
		return -EPROTO;
	return inet_csk_listen_start(sk, TCP_SYNQ_HSIZE);
}

int dccp_disconnect(struct sock *sk, int flags)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet = inet_sk(sk);
	int err = 0;
	const int old_state = sk->sk_state;

	if (old_state != DCCP_CLOSED)
		dccp_set_state(sk, DCCP_CLOSED);

	/* ABORT function of RFC793 */
	if (old_state == DCCP_LISTEN) {
		inet_csk_listen_stop(sk);
	/* FIXME: do the active reset thing */
	} else if (old_state == DCCP_REQUESTING)
		sk->sk_err = ECONNRESET;

	dccp_clear_xmit_timers(sk);
	__skb_queue_purge(&sk->sk_receive_queue);
	if (sk->sk_send_head != NULL) {
		__kfree_skb(sk->sk_send_head);
		sk->sk_send_head = NULL;
	}

	inet->dport = 0;

	if (!(sk->sk_userlocks & SOCK_BINDADDR_LOCK))
		inet_reset_saddr(sk);

	sk->sk_shutdown = 0;
	sock_reset_flag(sk, SOCK_DONE);

	icsk->icsk_backoff = 0;
	inet_csk_delack_init(sk);
	__sk_dst_reset(sk);

	BUG_TRAP(!inet->num || icsk->icsk_bind_hash);

	sk->sk_error_report(sk);
	return err;
}

/*
 *	Wait for a DCCP event.
 *
 *	Note that we don't need to lock the socket, as the upper poll layers
 *	take care of normal races (between the test and the event) and we don't
 *	go look at any of the socket buffers directly.
 */
static unsigned int dccp_poll(struct file *file, struct socket *sock,
			      poll_table *wait)
{
	unsigned int mask;
	struct sock *sk = sock->sk;

	poll_wait(file, sk->sk_sleep, wait);
	if (sk->sk_state == DCCP_LISTEN)
		return inet_csk_listen_poll(sk);

	/* Socket is not locked. We are protected from async events
	   by poll logic and correct handling of state changes
	   made by another threads is impossible in any case.
	 */

	mask = 0;
	if (sk->sk_err)
		mask = POLLERR;

	if (sk->sk_shutdown == SHUTDOWN_MASK || sk->sk_state == DCCP_CLOSED)
		mask |= POLLHUP;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLIN | POLLRDNORM;

	/* Connected? */
	if ((1 << sk->sk_state) & ~(DCCPF_REQUESTING | DCCPF_RESPOND)) {
		if (atomic_read(&sk->sk_rmem_alloc) > 0)
			mask |= POLLIN | POLLRDNORM;

		if (!(sk->sk_shutdown & SEND_SHUTDOWN)) {
			if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk)) {
				mask |= POLLOUT | POLLWRNORM;
			} else {  /* send SIGIO later */
				set_bit(SOCK_ASYNC_NOSPACE,
					&sk->sk_socket->flags);
				set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);

				/* Race breaker. If space is freed after
				 * wspace test but before the flags are set,
				 * IO signal will be lost.
				 */
				if (sk_stream_wspace(sk) >= sk_stream_min_wspace(sk))
					mask |= POLLOUT | POLLWRNORM;
			}
		}
	}
	return mask;
}

int dccp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	dccp_pr_debug("entry\n");
	return -ENOIOCTLCMD;
}

static int dccp_setsockopt_service(struct sock *sk, const u32 service,
				   char __user *optval, int optlen)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_service_list *sl = NULL;

	if (service == DCCP_SERVICE_INVALID_VALUE || 
	    optlen > DCCP_SERVICE_LIST_MAX_LEN * sizeof(u32))
		return -EINVAL;

	if (optlen > sizeof(service)) {
		sl = kmalloc(optlen, GFP_KERNEL);
		if (sl == NULL)
			return -ENOMEM;

		sl->dccpsl_nr = optlen / sizeof(u32) - 1;
		if (copy_from_user(sl->dccpsl_list,
				   optval + sizeof(service),
				   optlen - sizeof(service)) ||
		    dccp_list_has_service(sl, DCCP_SERVICE_INVALID_VALUE)) {
			kfree(sl);
			return -EFAULT;
		}
	}

	lock_sock(sk);
	dp->dccps_service = service;

	kfree(dp->dccps_service_list);

	dp->dccps_service_list = sl;
	release_sock(sk);
	return 0;
}

int dccp_setsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int optlen)
{
	struct dccp_sock *dp;
	int err;
	int val;

	if (level != SOL_DCCP)
		return ip_setsockopt(sk, level, optname, optval, optlen);

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	if (optname == DCCP_SOCKOPT_SERVICE)
		return dccp_setsockopt_service(sk, val, optval, optlen);

	lock_sock(sk);
	dp = dccp_sk(sk);
	err = 0;

	switch (optname) {
	case DCCP_SOCKOPT_PACKET_SIZE:
		dp->dccps_packet_size = val;
		break;
	default:
		err = -ENOPROTOOPT;
		break;
	}
	
	release_sock(sk);
	return err;
}

static int dccp_getsockopt_service(struct sock *sk, int len,
				   u32 __user *optval,
				   int __user *optlen)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const struct dccp_service_list *sl;
	int err = -ENOENT, slen = 0, total_len = sizeof(u32);

	lock_sock(sk);
	if (dccp_service_not_initialized(sk))
		goto out;

	if ((sl = dp->dccps_service_list) != NULL) {
		slen = sl->dccpsl_nr * sizeof(u32);
		total_len += slen;
	}

	err = -EINVAL;
	if (total_len > len)
		goto out;

	err = 0;
	if (put_user(total_len, optlen) ||
	    put_user(dp->dccps_service, optval) ||
	    (sl != NULL && copy_to_user(optval + 1, sl->dccpsl_list, slen)))
		err = -EFAULT;
out:
	release_sock(sk);
	return err;
}

int dccp_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	struct dccp_sock *dp;
	int val, len;

	if (level != SOL_DCCP)
		return ip_getsockopt(sk, level, optname, optval, optlen);

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < sizeof(int))
		return -EINVAL;

	dp = dccp_sk(sk);

	switch (optname) {
	case DCCP_SOCKOPT_PACKET_SIZE:
		val = dp->dccps_packet_size;
		len = sizeof(dp->dccps_packet_size);
		break;
	case DCCP_SOCKOPT_SERVICE:
		return dccp_getsockopt_service(sk, len,
					       (u32 __user *)optval, optlen);
	case 128 ... 191:
		return ccid_hc_rx_getsockopt(dp->dccps_hc_rx_ccid, sk, optname,
					     len, (u32 __user *)optval, optlen);
	case 192 ... 255:
		return ccid_hc_tx_getsockopt(dp->dccps_hc_tx_ccid, sk, optname,
					     len, (u32 __user *)optval, optlen);
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen) || copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

int dccp_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		 size_t len)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const int flags = msg->msg_flags;
	const int noblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb;
	int rc, size;
	long timeo;

	if (len > dp->dccps_mss_cache)
		return -EMSGSIZE;

	lock_sock(sk);
	timeo = sock_sndtimeo(sk, noblock);

	/*
	 * We have to use sk_stream_wait_connect here to set sk_write_pending,
	 * so that the trick in dccp_rcv_request_sent_state_process.
	 */
	/* Wait for a connection to finish. */
	if ((1 << sk->sk_state) & ~(DCCPF_OPEN | DCCPF_PARTOPEN | DCCPF_CLOSING))
		if ((rc = sk_stream_wait_connect(sk, &timeo)) != 0)
			goto out_release;

	size = sk->sk_prot->max_header + len;
	release_sock(sk);
	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	lock_sock(sk);
	if (skb == NULL)
		goto out_release;

	skb_reserve(skb, sk->sk_prot->max_header);
	rc = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
	if (rc != 0)
		goto out_discard;

	rc = dccp_write_xmit(sk, skb, &timeo);
	/*
	 * XXX we don't use sk_write_queue, so just discard the packet.
	 *     Current plan however is to _use_ sk_write_queue with
	 *     an algorith similar to tcp_sendmsg, where the main difference
	 *     is that in DCCP we have to respect packet boundaries, so
	 *     no coalescing of skbs.
	 *
	 *     This bug was _quickly_ found & fixed by just looking at an OSTRA
	 *     generated callgraph 8) -acme
	 */
out_release:
	release_sock(sk);
	return rc ? : len;
out_discard:
	kfree_skb(skb);
	goto out_release;
}

int dccp_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		 size_t len, int nonblock, int flags, int *addr_len)
{
	const struct dccp_hdr *dh;
	long timeo;

	lock_sock(sk);

	if (sk->sk_state == DCCP_LISTEN) {
		len = -ENOTCONN;
		goto out;
	}

	timeo = sock_rcvtimeo(sk, nonblock);

	do {
		struct sk_buff *skb = skb_peek(&sk->sk_receive_queue);

		if (skb == NULL)
			goto verify_sock_status;

		dh = dccp_hdr(skb);

		if (dh->dccph_type == DCCP_PKT_DATA ||
		    dh->dccph_type == DCCP_PKT_DATAACK)
			goto found_ok_skb;

		if (dh->dccph_type == DCCP_PKT_RESET ||
		    dh->dccph_type == DCCP_PKT_CLOSE) {
			dccp_pr_debug("found fin ok!\n");
			len = 0;
			goto found_fin_ok;
		}
		dccp_pr_debug("packet_type=%s\n",
			      dccp_packet_name(dh->dccph_type));
		sk_eat_skb(sk, skb);
verify_sock_status:
		if (sock_flag(sk, SOCK_DONE)) {
			len = 0;
			break;
		}

		if (sk->sk_err) {
			len = sock_error(sk);
			break;
		}

		if (sk->sk_shutdown & RCV_SHUTDOWN) {
			len = 0;
			break;
		}

		if (sk->sk_state == DCCP_CLOSED) {
			if (!sock_flag(sk, SOCK_DONE)) {
				/* This occurs when user tries to read
				 * from never connected socket.
				 */
				len = -ENOTCONN;
				break;
			}
			len = 0;
			break;
		}

		if (!timeo) {
			len = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			len = sock_intr_errno(timeo);
			break;
		}

		sk_wait_data(sk, &timeo);
		continue;
	found_ok_skb:
		if (len > skb->len)
			len = skb->len;
		else if (len < skb->len)
			msg->msg_flags |= MSG_TRUNC;

		if (skb_copy_datagram_iovec(skb, 0, msg->msg_iov, len)) {
			/* Exception. Bailout! */
			len = -EFAULT;
			break;
		}
	found_fin_ok:
		if (!(flags & MSG_PEEK))
			sk_eat_skb(sk, skb);
		break;
	} while (1);
out:
	release_sock(sk);
	return len;
}

static int inet_dccp_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	unsigned char old_state;
	int err;

	lock_sock(sk);

	err = -EINVAL;
	if (sock->state != SS_UNCONNECTED || sock->type != SOCK_DCCP)
		goto out;

	old_state = sk->sk_state;
	if (!((1 << old_state) & (DCCPF_CLOSED | DCCPF_LISTEN)))
		goto out;

	/* Really, if the socket is already in listen state
	 * we can only allow the backlog to be adjusted.
	 */
	if (old_state != DCCP_LISTEN) {
		/*
		 * FIXME: here it probably should be sk->sk_prot->listen_start
		 * see tcp_listen_start
		 */
		err = dccp_listen_start(sk);
		if (err)
			goto out;
	}
	sk->sk_max_ack_backlog = backlog;
	err = 0;

out:
	release_sock(sk);
	return err;
}

static const unsigned char dccp_new_state[] = {
	/* current state:   new state:      action:	*/
	[0]		  = DCCP_CLOSED,
	[DCCP_OPEN] 	  = DCCP_CLOSING | DCCP_ACTION_FIN,
	[DCCP_REQUESTING] = DCCP_CLOSED,
	[DCCP_PARTOPEN]	  = DCCP_CLOSING | DCCP_ACTION_FIN,
	[DCCP_LISTEN]	  = DCCP_CLOSED,
	[DCCP_RESPOND]	  = DCCP_CLOSED,
	[DCCP_CLOSING]	  = DCCP_CLOSED,
	[DCCP_TIME_WAIT]  = DCCP_CLOSED,
	[DCCP_CLOSED]	  = DCCP_CLOSED,
};

static int dccp_close_state(struct sock *sk)
{
	const int next = dccp_new_state[sk->sk_state];
	const int ns = next & DCCP_STATE_MASK;

	if (ns != sk->sk_state)
		dccp_set_state(sk, ns);

	return next & DCCP_ACTION_FIN;
}

void dccp_close(struct sock *sk, long timeout)
{
	struct sk_buff *skb;

	lock_sock(sk);

	sk->sk_shutdown = SHUTDOWN_MASK;

	if (sk->sk_state == DCCP_LISTEN) {
		dccp_set_state(sk, DCCP_CLOSED);

		/* Special case. */
		inet_csk_listen_stop(sk);

		goto adjudge_to_death;
	}

	/*
	 * We need to flush the recv. buffs.  We do this only on the
	 * descriptor close, not protocol-sourced closes, because the
	  *reader process may not have drained the data yet!
	 */
	/* FIXME: check for unread data */
	while ((skb = __skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		__kfree_skb(skb);
	}

	if (sock_flag(sk, SOCK_LINGER) && !sk->sk_lingertime) {
		/* Check zero linger _after_ checking for unread data. */
		sk->sk_prot->disconnect(sk, 0);
	} else if (dccp_close_state(sk)) {
		dccp_send_close(sk, 1);
	}

	sk_stream_wait_close(sk, timeout);

adjudge_to_death:
	/*
	 * It is the last release_sock in its life. It will remove backlog.
	 */
	release_sock(sk);
	/*
	 * Now socket is owned by kernel and we acquire BH lock
	 * to finish close. No need to check for user refs.
	 */
	local_bh_disable();
	bh_lock_sock(sk);
	BUG_TRAP(!sock_owned_by_user(sk));

	sock_hold(sk);
	sock_orphan(sk);

	/*
	 * The last release_sock may have processed the CLOSE or RESET
	 * packet moving sock to CLOSED state, if not we have to fire
	 * the CLOSE/CLOSEREQ retransmission timer, see "8.3. Termination"
	 * in draft-ietf-dccp-spec-11. -acme
	 */
	if (sk->sk_state == DCCP_CLOSING) {
		/* FIXME: should start at 2 * RTT */
		/* Timer for repeating the CLOSE/CLOSEREQ until an answer. */
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
					  inet_csk(sk)->icsk_rto,
					  DCCP_RTO_MAX);
#if 0
		/* Yeah, we should use sk->sk_prot->orphan_count, etc */
		dccp_set_state(sk, DCCP_CLOSED);
#endif
	}

	atomic_inc(sk->sk_prot->orphan_count);
	if (sk->sk_state == DCCP_CLOSED)
		inet_csk_destroy_sock(sk);

	/* Otherwise, socket is reprieved until protocol close. */

	bh_unlock_sock(sk);
	local_bh_enable();
	sock_put(sk);
}

void dccp_shutdown(struct sock *sk, int how)
{
	dccp_pr_debug("entry\n");
}

static struct proto_ops inet_dccp_ops = {
	.family		= PF_INET,
	.owner		= THIS_MODULE,
	.release	= inet_release,
	.bind		= inet_bind,
	.connect	= inet_stream_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= inet_accept,
	.getname	= inet_getname,
	/* FIXME: work on tcp_poll to rename it to inet_csk_poll */
	.poll		= dccp_poll,
	.ioctl		= inet_ioctl,
	/* FIXME: work on inet_listen to rename it to sock_common_listen */
	.listen		= inet_dccp_listen,
	.shutdown	= inet_shutdown,
	.setsockopt	= sock_common_setsockopt,
	.getsockopt	= sock_common_getsockopt,
	.sendmsg	= inet_sendmsg,
	.recvmsg	= sock_common_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

extern struct net_proto_family inet_family_ops;

static struct inet_protosw dccp_v4_protosw = {
	.type		= SOCK_DCCP,
	.protocol	= IPPROTO_DCCP,
	.prot		= &dccp_v4_prot,
	.ops		= &inet_dccp_ops,
	.capability	= -1,
	.no_check	= 0,
	.flags		= 0,
};

/*
 * This is the global socket data structure used for responding to
 * the Out-of-the-blue (OOTB) packets. A control sock will be created
 * for this socket at the initialization time.
 */
struct socket *dccp_ctl_socket;

static char dccp_ctl_socket_err_msg[] __initdata =
	KERN_ERR "DCCP: Failed to create the control socket.\n";

static int __init dccp_ctl_sock_init(void)
{
	int rc = sock_create_kern(PF_INET, SOCK_DCCP, IPPROTO_DCCP,
				  &dccp_ctl_socket);
	if (rc < 0)
		printk(dccp_ctl_socket_err_msg);
	else {
		dccp_ctl_socket->sk->sk_allocation = GFP_ATOMIC;
		inet_sk(dccp_ctl_socket->sk)->uc_ttl = -1;

		/* Unhash it so that IP input processing does not even
		 * see it, we do not wish this socket to see incoming
		 * packets.
		 */
		dccp_ctl_socket->sk->sk_prot->unhash(dccp_ctl_socket->sk);
	}

	return rc;
}

#ifdef CONFIG_IP_DCCP_UNLOAD_HACK
void dccp_ctl_sock_exit(void)
{
	if (dccp_ctl_socket != NULL) {
		sock_release(dccp_ctl_socket);
		dccp_ctl_socket = NULL;
	}
}

EXPORT_SYMBOL_GPL(dccp_ctl_sock_exit);
#endif

static int __init init_dccp_v4_mibs(void)
{
	int rc = -ENOMEM;

	dccp_statistics[0] = alloc_percpu(struct dccp_mib);
	if (dccp_statistics[0] == NULL)
		goto out;

	dccp_statistics[1] = alloc_percpu(struct dccp_mib);
	if (dccp_statistics[1] == NULL)
		goto out_free_one;

	rc = 0;
out:
	return rc;
out_free_one:
	free_percpu(dccp_statistics[0]);
	dccp_statistics[0] = NULL;
	goto out;

}

static int thash_entries;
module_param(thash_entries, int, 0444);
MODULE_PARM_DESC(thash_entries, "Number of ehash buckets");

#ifdef CONFIG_IP_DCCP_DEBUG
int dccp_debug;
module_param(dccp_debug, int, 0444);
MODULE_PARM_DESC(dccp_debug, "Enable debug messages");
#endif

static int __init dccp_init(void)
{
	unsigned long goal;
	int ehash_order, bhash_order, i;
	int rc = proto_register(&dccp_v4_prot, 1);

	if (rc)
		goto out;

	dccp_hashinfo.bind_bucket_cachep =
		kmem_cache_create("dccp_bind_bucket",
				  sizeof(struct inet_bind_bucket), 0,
				  SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!dccp_hashinfo.bind_bucket_cachep)
		goto out_proto_unregister;

	/*
	 * Size and allocate the main established and bind bucket
	 * hash tables.
	 *
	 * The methodology is similar to that of the buffer cache.
	 */
	if (num_physpages >= (128 * 1024))
		goal = num_physpages >> (21 - PAGE_SHIFT);
	else
		goal = num_physpages >> (23 - PAGE_SHIFT);

	if (thash_entries)
		goal = (thash_entries *
			sizeof(struct inet_ehash_bucket)) >> PAGE_SHIFT;
	for (ehash_order = 0; (1UL << ehash_order) < goal; ehash_order++)
		;
	do {
		dccp_hashinfo.ehash_size = (1UL << ehash_order) * PAGE_SIZE /
					sizeof(struct inet_ehash_bucket);
		dccp_hashinfo.ehash_size >>= 1;
		while (dccp_hashinfo.ehash_size &
		       (dccp_hashinfo.ehash_size - 1))
			dccp_hashinfo.ehash_size--;
		dccp_hashinfo.ehash = (struct inet_ehash_bucket *)
			__get_free_pages(GFP_ATOMIC, ehash_order);
	} while (!dccp_hashinfo.ehash && --ehash_order > 0);

	if (!dccp_hashinfo.ehash) {
		printk(KERN_CRIT "Failed to allocate DCCP "
				 "established hash table\n");
		goto out_free_bind_bucket_cachep;
	}

	for (i = 0; i < (dccp_hashinfo.ehash_size << 1); i++) {
		rwlock_init(&dccp_hashinfo.ehash[i].lock);
		INIT_HLIST_HEAD(&dccp_hashinfo.ehash[i].chain);
	}

	bhash_order = ehash_order;

	do {
		dccp_hashinfo.bhash_size = (1UL << bhash_order) * PAGE_SIZE /
					sizeof(struct inet_bind_hashbucket);
		if ((dccp_hashinfo.bhash_size > (64 * 1024)) &&
		    bhash_order > 0)
			continue;
		dccp_hashinfo.bhash = (struct inet_bind_hashbucket *)
			__get_free_pages(GFP_ATOMIC, bhash_order);
	} while (!dccp_hashinfo.bhash && --bhash_order >= 0);

	if (!dccp_hashinfo.bhash) {
		printk(KERN_CRIT "Failed to allocate DCCP bind hash table\n");
		goto out_free_dccp_ehash;
	}

	for (i = 0; i < dccp_hashinfo.bhash_size; i++) {
		spin_lock_init(&dccp_hashinfo.bhash[i].lock);
		INIT_HLIST_HEAD(&dccp_hashinfo.bhash[i].chain);
	}

	if (init_dccp_v4_mibs())
		goto out_free_dccp_bhash;

	rc = -EAGAIN;
	if (inet_add_protocol(&dccp_protocol, IPPROTO_DCCP))
		goto out_free_dccp_v4_mibs;

	inet_register_protosw(&dccp_v4_protosw);

	rc = dccp_ctl_sock_init();
	if (rc)
		goto out_unregister_protosw;
out:
	return rc;
out_unregister_protosw:
	inet_unregister_protosw(&dccp_v4_protosw);
	inet_del_protocol(&dccp_protocol, IPPROTO_DCCP);
out_free_dccp_v4_mibs:
	free_percpu(dccp_statistics[0]);
	free_percpu(dccp_statistics[1]);
	dccp_statistics[0] = dccp_statistics[1] = NULL;
out_free_dccp_bhash:
	free_pages((unsigned long)dccp_hashinfo.bhash, bhash_order);
	dccp_hashinfo.bhash = NULL;
out_free_dccp_ehash:
	free_pages((unsigned long)dccp_hashinfo.ehash, ehash_order);
	dccp_hashinfo.ehash = NULL;
out_free_bind_bucket_cachep:
	kmem_cache_destroy(dccp_hashinfo.bind_bucket_cachep);
	dccp_hashinfo.bind_bucket_cachep = NULL;
out_proto_unregister:
	proto_unregister(&dccp_v4_prot);
	goto out;
}

static const char dccp_del_proto_err_msg[] __exitdata =
	KERN_ERR "can't remove dccp net_protocol\n";

static void __exit dccp_fini(void)
{
	inet_unregister_protosw(&dccp_v4_protosw);

	if (inet_del_protocol(&dccp_protocol, IPPROTO_DCCP) < 0)
		printk(dccp_del_proto_err_msg);

	free_percpu(dccp_statistics[0]);
	free_percpu(dccp_statistics[1]);
	free_pages((unsigned long)dccp_hashinfo.bhash,
		   get_order(dccp_hashinfo.bhash_size *
			     sizeof(struct inet_bind_hashbucket)));
	free_pages((unsigned long)dccp_hashinfo.ehash,
		   get_order(dccp_hashinfo.ehash_size *
			     sizeof(struct inet_ehash_bucket)));
	kmem_cache_destroy(dccp_hashinfo.bind_bucket_cachep);
	proto_unregister(&dccp_v4_prot);
}

module_init(dccp_init);
module_exit(dccp_fini);

/*
 * __stringify doesn't likes enums, so use SOCK_DCCP (6) and IPPROTO_DCCP (33)
 * values directly, Also cover the case where the protocol is not specified,
 * i.e. net-pf-PF_INET-proto-0-type-SOCK_DCCP
 */
MODULE_ALIAS("net-pf-" __stringify(PF_INET) "-proto-33-type-6");
MODULE_ALIAS("net-pf-" __stringify(PF_INET) "-proto-0-type-6");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnaldo Carvalho de Melo <acme@conectiva.com.br>");
MODULE_DESCRIPTION("DCCP - Datagram Congestion Controlled Protocol");
