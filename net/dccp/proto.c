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

DEFINE_SNMP_STAT(struct dccp_mib, dccp_statistics);

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
	dccp_sk(sk)->dccps_role = DCCP_ROLE_LISTEN;
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

int dccp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	dccp_pr_debug("entry\n");
	return -ENOIOCTLCMD;
}

int dccp_setsockopt(struct sock *sk, int level, int optname,
		    char *optval, int optlen)
{
	dccp_pr_debug("entry\n");

	if (level != SOL_DCCP)
		return ip_setsockopt(sk, level, optname, optval, optlen);

	return -EOPNOTSUPP;
}

int dccp_getsockopt(struct sock *sk, int level, int optname,
		    char *optval, int *optlen)
{
	dccp_pr_debug("entry\n");

	if (level != SOL_DCCP)
		return ip_getsockopt(sk, level, optname, optval, optlen);

	return -EOPNOTSUPP;
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

	timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);

	/*
	 * We have to use sk_stream_wait_connect here to set sk_write_pending,
	 * so that the trick in dccp_rcv_request_sent_state_process.
	 */
	/* Wait for a connection to finish. */
	if ((1 << sk->sk_state) & ~(DCCPF_OPEN | DCCPF_PARTOPEN | DCCPF_CLOSING))
		if ((rc = sk_stream_wait_connect(sk, &timeo)) != 0)
			goto out_err;

	size = sk->sk_prot->max_header + len;
	release_sock(sk);
	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	lock_sock(sk);

	if (skb == NULL)
		goto out_release;

	skb_reserve(skb, sk->sk_prot->max_header);
	rc = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
	if (rc == 0) {
		struct dccp_skb_cb *dcb = DCCP_SKB_CB(skb);
		const struct dccp_ackpkts *ap = dp->dccps_hc_rx_ackpkts;
		long delay; 

		/*
		 * XXX: This is just to match the Waikato tree CA interaction
		 * points, after the CCID3 code is stable and I have a better
		 * understanding of behaviour I'll change this to look more like
		 * TCP.
		 */
		while (1) {
			rc = ccid_hc_tx_send_packet(dp->dccps_hc_tx_ccid, sk,
						    skb, len, &delay);
			if (rc == 0)
				break;
			if (rc != -EAGAIN)
				goto out_discard;
			if (delay > timeo)
				goto out_discard;
			release_sock(sk);
			delay = schedule_timeout(delay);
			lock_sock(sk);
			timeo -= delay;
			if (signal_pending(current))
				goto out_interrupted;
			rc = -EPIPE;
			if (!(sk->sk_state == DCCP_PARTOPEN || sk->sk_state == DCCP_OPEN))
				goto out_discard;
		}

		if (sk->sk_state == DCCP_PARTOPEN) {
			/* See 8.1.5.  Handshake Completion */
			inet_csk_schedule_ack(sk);
			inet_csk_reset_xmit_timer(sk, ICSK_TIME_DACK, inet_csk(sk)->icsk_rto, TCP_RTO_MAX);
			dcb->dccpd_type = DCCP_PKT_DATAACK;
			/* FIXME: we really should have a dccps_ack_pending or use icsk */
		} else if (inet_csk_ack_scheduled(sk) ||
			   (dp->dccps_options.dccpo_send_ack_vector &&
			    ap->dccpap_buf_ackno != DCCP_MAX_SEQNO + 1 &&
			    ap->dccpap_ack_seqno == DCCP_MAX_SEQNO + 1))
			dcb->dccpd_type = DCCP_PKT_DATAACK;
		else
			dcb->dccpd_type = DCCP_PKT_DATA;
		dccp_transmit_skb(sk, skb);
		ccid_hc_tx_packet_sent(dp->dccps_hc_tx_ccid, sk, 0, len);
	} else {
out_discard:
		kfree_skb(skb);
	}
out_release:
	release_sock(sk);
	return rc ? : len;
out_err:
	rc = sk_stream_error(sk, flags, rc);
	goto out_release;
out_interrupted:
	rc = sock_intr_errno(timeo);
	goto out_discard;
}

EXPORT_SYMBOL(dccp_sendmsg);

int dccp_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		 size_t len, int nonblock, int flags, int *addr_len)
{
	const struct dccp_hdr *dh;
	int copied = 0;
	unsigned long used;
	int err;
	int target;		/* Read at least this many bytes */
	long timeo;

	lock_sock(sk);

	err = -ENOTCONN;
	if (sk->sk_state == DCCP_LISTEN)
		goto out;

	timeo = sock_rcvtimeo(sk, nonblock);

	/* Urgent data needs to be handled specially. */
	if (flags & MSG_OOB)
		goto recv_urg;

	/* FIXME */
#if 0
	seq = &tp->copied_seq;
	if (flags & MSG_PEEK) {
		peek_seq = tp->copied_seq;
		seq = &peek_seq;
	}
#endif

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	do {
		struct sk_buff *skb;
		u32 offset;

	/* FIXME */
#if 0
		/* Are we at urgent data? Stop if we have read anything or have SIGURG pending. */
		if (tp->urg_data && tp->urg_seq == *seq) {
			if (copied)
				break;
			if (signal_pending(current)) {
				copied = timeo ? sock_intr_errno(timeo) : -EAGAIN;
				break;
			}
		}
#endif

		/* Next get a buffer. */

		skb = skb_peek(&sk->sk_receive_queue);
		do {
			if (!skb)
				break;

			offset = 0;
			dh = dccp_hdr(skb);

			if (dh->dccph_type == DCCP_PKT_DATA ||
			    dh->dccph_type == DCCP_PKT_DATAACK)
				goto found_ok_skb;

			if (dh->dccph_type == DCCP_PKT_RESET ||
			    dh->dccph_type == DCCP_PKT_CLOSE) {
				dccp_pr_debug("found fin ok!\n");
				goto found_fin_ok;
			}
			dccp_pr_debug("packet_type=%s\n", dccp_packet_name(dh->dccph_type));
			BUG_TRAP(flags & MSG_PEEK);
			skb = skb->next;
		} while (skb != (struct sk_buff *)&sk->sk_receive_queue);

		/* Well, if we have backlog, try to process it now yet. */
		if (copied >= target && !sk->sk_backlog.tail)
			break;

		if (copied) {
			if (sk->sk_err ||
			    sk->sk_state == DCCP_CLOSED ||
			    (sk->sk_shutdown & RCV_SHUTDOWN) ||
			    !timeo ||
			    signal_pending(current) ||
			    (flags & MSG_PEEK))
				break;
		} else {
			if (sock_flag(sk, SOCK_DONE))
				break;

			if (sk->sk_err) {
				copied = sock_error(sk);
				break;
			}

			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			if (sk->sk_state == DCCP_CLOSED) {
				if (!sock_flag(sk, SOCK_DONE)) {
					/* This occurs when user tries to read
					 * from never connected socket.
					 */
					copied = -ENOTCONN;
					break;
				}
				break;
			}

			if (!timeo) {
				copied = -EAGAIN;
				break;
			}

			if (signal_pending(current)) {
				copied = sock_intr_errno(timeo);
				break;
			}
		}

		/* FIXME: cleanup_rbuf(sk, copied); */

		if (copied >= target) {
			/* Do not sleep, just process backlog. */
			release_sock(sk);
			lock_sock(sk);
		} else
			sk_wait_data(sk, &timeo);

		continue;

	found_ok_skb:
		/* Ok so how much can we use? */
		used = skb->len - offset;
		if (len < used)
			used = len;

		if (!(flags & MSG_TRUNC)) {
			err = skb_copy_datagram_iovec(skb, offset,
						      msg->msg_iov, used);
			if (err) {
				/* Exception. Bailout! */
				if (!copied)
					copied = -EFAULT;
				break;
			}
		}

		copied += used;
		len -= used;

		/* FIXME: tcp_rcv_space_adjust(sk); */

//skip_copy:
		if (used + offset < skb->len)
			continue;

		if (!(flags & MSG_PEEK))
			sk_eat_skb(sk, skb);
		continue;
	found_fin_ok:
		if (!(flags & MSG_PEEK))
			sk_eat_skb(sk, skb);
		break;
		
	} while (len > 0);

	/* According to UNIX98, msg_name/msg_namelen are ignored
	 * on connected socket. I was just happy when found this 8) --ANK
	 */

	/* Clean up data we have read: This will do ACK frames. */
	/* FIXME: cleanup_rbuf(sk, copied); */

	release_sock(sk);
	return copied;

out:
	release_sock(sk);
	return err;

recv_urg:
	/* FIXME: err = tcp_recv_urg(sk, timeo, msg, len, flags, addr_len); */
	goto out;
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
	/* current state:        new state:      action:	*/
	[0]			= DCCP_CLOSED,
	[DCCP_OPEN] 		= DCCP_CLOSING | DCCP_ACTION_FIN,
	[DCCP_REQUESTING] 	= DCCP_CLOSED,
	[DCCP_PARTOPEN]	= DCCP_CLOSING | DCCP_ACTION_FIN,
	[DCCP_LISTEN]		= DCCP_CLOSED,
	[DCCP_RESPOND] 	= DCCP_CLOSED,
	[DCCP_CLOSING]	= DCCP_CLOSED,
	[DCCP_TIME_WAIT] 	= DCCP_CLOSED,
	[DCCP_CLOSED] 	= DCCP_CLOSED,
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
		dccp_send_close(sk);
	}

	sk_stream_wait_close(sk, timeout);

adjudge_to_death:
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
						
	if (sk->sk_state != DCCP_CLOSED)
		dccp_set_state(sk, DCCP_CLOSED);

	atomic_inc(&dccp_orphan_count);
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

struct proto_ops inet_dccp_ops = {
	.family		= PF_INET,
	.owner		= THIS_MODULE,
	.release	= inet_release,
	.bind		= inet_bind,
	.connect	= inet_stream_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= inet_accept,
	.getname	= inet_getname,
	.poll		= sock_no_poll,
	.ioctl		= inet_ioctl,
	.listen		= inet_dccp_listen, /* FIXME: work on inet_listen to rename it to sock_common_listen */
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

static void __exit dccp_ctl_sock_exit(void)
{
	if (dccp_ctl_socket != NULL)
		sock_release(dccp_ctl_socket);
}

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

int dccp_debug;
module_param(dccp_debug, int, 0444);
MODULE_PARM_DESC(dccp_debug, "Enable debug messages");

static int __init dccp_init(void)
{
	unsigned long goal;
	int ehash_order, bhash_order, i;
	int rc = proto_register(&dccp_v4_prot, 1);

	if (rc)
		goto out;

	dccp_hashinfo.bind_bucket_cachep = kmem_cache_create("dccp_bind_bucket",
					       sizeof(struct inet_bind_bucket),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL, NULL);
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
		goal = (thash_entries * sizeof(struct inet_ehash_bucket)) >> PAGE_SHIFT;
	for (ehash_order = 0; (1UL << ehash_order) < goal; ehash_order++)
		;
	do {
		dccp_hashinfo.ehash_size = (1UL << ehash_order) * PAGE_SIZE /
					sizeof(struct inet_ehash_bucket);
		dccp_hashinfo.ehash_size >>= 1;
		while (dccp_hashinfo.ehash_size & (dccp_hashinfo.ehash_size - 1))
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
		if ((dccp_hashinfo.bhash_size > (64 * 1024)) && bhash_order > 0)
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
	dccp_ctl_sock_exit();

	inet_unregister_protosw(&dccp_v4_protosw);

	if (inet_del_protocol(&dccp_protocol, IPPROTO_DCCP) < 0)
		printk(dccp_del_proto_err_msg);

	/* Free the control endpoint.  */
	sock_release(dccp_ctl_socket);

	proto_unregister(&dccp_v4_prot);

	kmem_cache_destroy(dccp_hashinfo.bind_bucket_cachep);
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
