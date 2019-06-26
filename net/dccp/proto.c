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
#include <linux/slab.h>
#include <net/checksum.h>

#include <net/inet_sock.h>
#include <net/inet_common.h>
#include <net/sock.h>
#include <net/xfrm.h>

#include <asm/ioctls.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/poll.h>

#include "ccid.h"
#include "dccp.h"
#include "feat.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

DEFINE_SNMP_STAT(struct dccp_mib, dccp_statistics) __read_mostly;

EXPORT_SYMBOL_GPL(dccp_statistics);

struct percpu_counter dccp_orphan_count;
EXPORT_SYMBOL_GPL(dccp_orphan_count);

struct inet_hashinfo dccp_hashinfo;
EXPORT_SYMBOL_GPL(dccp_hashinfo);

/* the maximum queue length for tx in packets. 0 is no limit */
int sysctl_dccp_tx_qlen __read_mostly = 5;

#ifdef CONFIG_IP_DCCP_DEBUG
static const char *dccp_state_name(const int state)
{
	static const char *const dccp_state_names[] = {
	[DCCP_OPEN]		= "OPEN",
	[DCCP_REQUESTING]	= "REQUESTING",
	[DCCP_PARTOPEN]		= "PARTOPEN",
	[DCCP_LISTEN]		= "LISTEN",
	[DCCP_RESPOND]		= "RESPOND",
	[DCCP_CLOSING]		= "CLOSING",
	[DCCP_ACTIVE_CLOSEREQ]	= "CLOSEREQ",
	[DCCP_PASSIVE_CLOSE]	= "PASSIVE_CLOSE",
	[DCCP_PASSIVE_CLOSEREQ]	= "PASSIVE_CLOSEREQ",
	[DCCP_TIME_WAIT]	= "TIME_WAIT",
	[DCCP_CLOSED]		= "CLOSED",
	};

	if (state >= DCCP_MAX_STATES)
		return "INVALID STATE!";
	else
		return dccp_state_names[state];
}
#endif

void dccp_set_state(struct sock *sk, const int state)
{
	const int oldstate = sk->sk_state;

	dccp_pr_debug("%s(%p)  %s  -->  %s\n", dccp_role(sk), sk,
		      dccp_state_name(oldstate), dccp_state_name(state));
	WARN_ON(state == oldstate);

	switch (state) {
	case DCCP_OPEN:
		if (oldstate != DCCP_OPEN)
			DCCP_INC_STATS(DCCP_MIB_CURRESTAB);
		/* Client retransmits all Confirm options until entering OPEN */
		if (oldstate == DCCP_PARTOPEN)
			dccp_feat_list_purge(&dccp_sk(sk)->dccps_featneg);
		break;

	case DCCP_CLOSED:
		if (oldstate == DCCP_OPEN || oldstate == DCCP_ACTIVE_CLOSEREQ ||
		    oldstate == DCCP_CLOSING)
			DCCP_INC_STATS(DCCP_MIB_ESTABRESETS);

		sk->sk_prot->unhash(sk);
		if (inet_csk(sk)->icsk_bind_hash != NULL &&
		    !(sk->sk_userlocks & SOCK_BINDPORT_LOCK))
			inet_put_port(sk);
		/* fall through */
	default:
		if (oldstate == DCCP_OPEN)
			DCCP_DEC_STATS(DCCP_MIB_CURRESTAB);
	}

	/* Change state AFTER socket is unhashed to avoid closed
	 * socket sitting in hash tables.
	 */
	inet_sk_set_state(sk, state);
}

EXPORT_SYMBOL_GPL(dccp_set_state);

static void dccp_finish_passive_close(struct sock *sk)
{
	switch (sk->sk_state) {
	case DCCP_PASSIVE_CLOSE:
		/* Node (client or server) has received Close packet. */
		dccp_send_reset(sk, DCCP_RESET_CODE_CLOSED);
		dccp_set_state(sk, DCCP_CLOSED);
		break;
	case DCCP_PASSIVE_CLOSEREQ:
		/*
		 * Client received CloseReq. We set the `active' flag so that
		 * dccp_send_close() retransmits the Close as per RFC 4340, 8.3.
		 */
		dccp_send_close(sk, 1);
		dccp_set_state(sk, DCCP_CLOSING);
	}
}

void dccp_done(struct sock *sk)
{
	dccp_set_state(sk, DCCP_CLOSED);
	dccp_clear_xmit_timers(sk);

	sk->sk_shutdown = SHUTDOWN_MASK;

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);
	else
		inet_csk_destroy_sock(sk);
}

EXPORT_SYMBOL_GPL(dccp_done);

const char *dccp_packet_name(const int type)
{
	static const char *const dccp_packet_names[] = {
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

static void dccp_sk_destruct(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);

	ccid_hc_tx_delete(dp->dccps_hc_tx_ccid, sk);
	dp->dccps_hc_tx_ccid = NULL;
	inet_sock_destruct(sk);
}

int dccp_init_sock(struct sock *sk, const __u8 ctl_sock_initialized)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct inet_connection_sock *icsk = inet_csk(sk);

	icsk->icsk_rto		= DCCP_TIMEOUT_INIT;
	icsk->icsk_syn_retries	= sysctl_dccp_request_retries;
	sk->sk_state		= DCCP_CLOSED;
	sk->sk_write_space	= dccp_write_space;
	sk->sk_destruct		= dccp_sk_destruct;
	icsk->icsk_sync_mss	= dccp_sync_mss;
	dp->dccps_mss_cache	= 536;
	dp->dccps_rate_last	= jiffies;
	dp->dccps_role		= DCCP_ROLE_UNDEFINED;
	dp->dccps_service	= DCCP_SERVICE_CODE_IS_ABSENT;
	dp->dccps_tx_qlen	= sysctl_dccp_tx_qlen;

	dccp_init_xmit_timers(sk);

	INIT_LIST_HEAD(&dp->dccps_featneg);
	/* control socket doesn't need feat nego */
	if (likely(ctl_sock_initialized))
		return dccp_feat_init(sk);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_init_sock);

void dccp_destroy_sock(struct sock *sk)
{
	struct dccp_sock *dp = dccp_sk(sk);

	__skb_queue_purge(&sk->sk_write_queue);
	if (sk->sk_send_head != NULL) {
		kfree_skb(sk->sk_send_head);
		sk->sk_send_head = NULL;
	}

	/* Clean up a referenced DCCP bind bucket. */
	if (inet_csk(sk)->icsk_bind_hash != NULL)
		inet_put_port(sk);

	kfree(dp->dccps_service_list);
	dp->dccps_service_list = NULL;

	if (dp->dccps_hc_rx_ackvec != NULL) {
		dccp_ackvec_free(dp->dccps_hc_rx_ackvec);
		dp->dccps_hc_rx_ackvec = NULL;
	}
	ccid_hc_rx_delete(dp->dccps_hc_rx_ccid, sk);
	dp->dccps_hc_rx_ccid = NULL;

	/* clean up feature negotiation state */
	dccp_feat_list_purge(&dp->dccps_featneg);
}

EXPORT_SYMBOL_GPL(dccp_destroy_sock);

static inline int dccp_listen_start(struct sock *sk, int backlog)
{
	struct dccp_sock *dp = dccp_sk(sk);

	dp->dccps_role = DCCP_ROLE_LISTEN;
	/* do not start to listen if feature negotiation setup fails */
	if (dccp_feat_finalise_settings(dp))
		return -EPROTO;
	return inet_csk_listen_start(sk, backlog);
}

static inline int dccp_need_reset(int state)
{
	return state != DCCP_CLOSED && state != DCCP_LISTEN &&
	       state != DCCP_REQUESTING;
}

int dccp_disconnect(struct sock *sk, int flags)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct dccp_sock *dp = dccp_sk(sk);
	const int old_state = sk->sk_state;

	if (old_state != DCCP_CLOSED)
		dccp_set_state(sk, DCCP_CLOSED);

	/*
	 * This corresponds to the ABORT function of RFC793, sec. 3.8
	 * TCP uses a RST segment, DCCP a Reset packet with Code 2, "Aborted".
	 */
	if (old_state == DCCP_LISTEN) {
		inet_csk_listen_stop(sk);
	} else if (dccp_need_reset(old_state)) {
		dccp_send_reset(sk, DCCP_RESET_CODE_ABORTED);
		sk->sk_err = ECONNRESET;
	} else if (old_state == DCCP_REQUESTING)
		sk->sk_err = ECONNRESET;

	dccp_clear_xmit_timers(sk);
	ccid_hc_rx_delete(dp->dccps_hc_rx_ccid, sk);
	dp->dccps_hc_rx_ccid = NULL;

	__skb_queue_purge(&sk->sk_receive_queue);
	__skb_queue_purge(&sk->sk_write_queue);
	if (sk->sk_send_head != NULL) {
		__kfree_skb(sk->sk_send_head);
		sk->sk_send_head = NULL;
	}

	inet->inet_dport = 0;

	if (!(sk->sk_userlocks & SOCK_BINDADDR_LOCK))
		inet_reset_saddr(sk);

	sk->sk_shutdown = 0;
	sock_reset_flag(sk, SOCK_DONE);

	icsk->icsk_backoff = 0;
	inet_csk_delack_init(sk);
	__sk_dst_reset(sk);

	WARN_ON(inet->inet_num && !icsk->icsk_bind_hash);

	sk->sk_error_report(sk);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_disconnect);

/*
 *	Wait for a DCCP event.
 *
 *	Note that we don't need to lock the socket, as the upper poll layers
 *	take care of normal races (between the test and the event) and we don't
 *	go look at any of the socket buffers directly.
 */
__poll_t dccp_poll(struct file *file, struct socket *sock,
		       poll_table *wait)
{
	__poll_t mask;
	struct sock *sk = sock->sk;

	sock_poll_wait(file, sock, wait);
	if (sk->sk_state == DCCP_LISTEN)
		return inet_csk_listen_poll(sk);

	/* Socket is not locked. We are protected from async events
	   by poll logic and correct handling of state changes
	   made by another threads is impossible in any case.
	 */

	mask = 0;
	if (sk->sk_err)
		mask = EPOLLERR;

	if (sk->sk_shutdown == SHUTDOWN_MASK || sk->sk_state == DCCP_CLOSED)
		mask |= EPOLLHUP;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLIN | EPOLLRDNORM | EPOLLRDHUP;

	/* Connected? */
	if ((1 << sk->sk_state) & ~(DCCPF_REQUESTING | DCCPF_RESPOND)) {
		if (atomic_read(&sk->sk_rmem_alloc) > 0)
			mask |= EPOLLIN | EPOLLRDNORM;

		if (!(sk->sk_shutdown & SEND_SHUTDOWN)) {
			if (sk_stream_is_writeable(sk)) {
				mask |= EPOLLOUT | EPOLLWRNORM;
			} else {  /* send SIGIO later */
				sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);
				set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);

				/* Race breaker. If space is freed after
				 * wspace test but before the flags are set,
				 * IO signal will be lost.
				 */
				if (sk_stream_is_writeable(sk))
					mask |= EPOLLOUT | EPOLLWRNORM;
			}
		}
	}
	return mask;
}

EXPORT_SYMBOL_GPL(dccp_poll);

int dccp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int rc = -ENOTCONN;

	lock_sock(sk);

	if (sk->sk_state == DCCP_LISTEN)
		goto out;

	switch (cmd) {
	case SIOCINQ: {
		struct sk_buff *skb;
		unsigned long amount = 0;

		skb = skb_peek(&sk->sk_receive_queue);
		if (skb != NULL) {
			/*
			 * We will only return the amount of this packet since
			 * that is all that will be read.
			 */
			amount = skb->len;
		}
		rc = put_user(amount, (int __user *)arg);
	}
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}
out:
	release_sock(sk);
	return rc;
}

EXPORT_SYMBOL_GPL(dccp_ioctl);

static int dccp_setsockopt_service(struct sock *sk, const __be32 service,
				   char __user *optval, unsigned int optlen)
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

static int dccp_setsockopt_cscov(struct sock *sk, int cscov, bool rx)
{
	u8 *list, len;
	int i, rc;

	if (cscov < 0 || cscov > 15)
		return -EINVAL;
	/*
	 * Populate a list of permissible values, in the range cscov...15. This
	 * is necessary since feature negotiation of single values only works if
	 * both sides incidentally choose the same value. Since the list starts
	 * lowest-value first, negotiation will pick the smallest shared value.
	 */
	if (cscov == 0)
		return 0;
	len = 16 - cscov;

	list = kmalloc(len, GFP_KERNEL);
	if (list == NULL)
		return -ENOBUFS;

	for (i = 0; i < len; i++)
		list[i] = cscov++;

	rc = dccp_feat_register_sp(sk, DCCPF_MIN_CSUM_COVER, rx, list, len);

	if (rc == 0) {
		if (rx)
			dccp_sk(sk)->dccps_pcrlen = cscov;
		else
			dccp_sk(sk)->dccps_pcslen = cscov;
	}
	kfree(list);
	return rc;
}

static int dccp_setsockopt_ccid(struct sock *sk, int type,
				char __user *optval, unsigned int optlen)
{
	u8 *val;
	int rc = 0;

	if (optlen < 1 || optlen > DCCP_FEAT_MAX_SP_VALS)
		return -EINVAL;

	val = memdup_user(optval, optlen);
	if (IS_ERR(val))
		return PTR_ERR(val);

	lock_sock(sk);
	if (type == DCCP_SOCKOPT_TX_CCID || type == DCCP_SOCKOPT_CCID)
		rc = dccp_feat_register_sp(sk, DCCPF_CCID, 1, val, optlen);

	if (!rc && (type == DCCP_SOCKOPT_RX_CCID || type == DCCP_SOCKOPT_CCID))
		rc = dccp_feat_register_sp(sk, DCCPF_CCID, 0, val, optlen);
	release_sock(sk);

	kfree(val);
	return rc;
}

static int do_dccp_setsockopt(struct sock *sk, int level, int optname,
		char __user *optval, unsigned int optlen)
{
	struct dccp_sock *dp = dccp_sk(sk);
	int val, err = 0;

	switch (optname) {
	case DCCP_SOCKOPT_PACKET_SIZE:
		DCCP_WARN("sockopt(PACKET_SIZE) is deprecated: fix your app\n");
		return 0;
	case DCCP_SOCKOPT_CHANGE_L:
	case DCCP_SOCKOPT_CHANGE_R:
		DCCP_WARN("sockopt(CHANGE_L/R) is deprecated: fix your app\n");
		return 0;
	case DCCP_SOCKOPT_CCID:
	case DCCP_SOCKOPT_RX_CCID:
	case DCCP_SOCKOPT_TX_CCID:
		return dccp_setsockopt_ccid(sk, optname, optval, optlen);
	}

	if (optlen < (int)sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	if (optname == DCCP_SOCKOPT_SERVICE)
		return dccp_setsockopt_service(sk, val, optval, optlen);

	lock_sock(sk);
	switch (optname) {
	case DCCP_SOCKOPT_SERVER_TIMEWAIT:
		if (dp->dccps_role != DCCP_ROLE_SERVER)
			err = -EOPNOTSUPP;
		else
			dp->dccps_server_timewait = (val != 0);
		break;
	case DCCP_SOCKOPT_SEND_CSCOV:
		err = dccp_setsockopt_cscov(sk, val, false);
		break;
	case DCCP_SOCKOPT_RECV_CSCOV:
		err = dccp_setsockopt_cscov(sk, val, true);
		break;
	case DCCP_SOCKOPT_QPOLICY_ID:
		if (sk->sk_state != DCCP_CLOSED)
			err = -EISCONN;
		else if (val < 0 || val >= DCCPQ_POLICY_MAX)
			err = -EINVAL;
		else
			dp->dccps_qpolicy = val;
		break;
	case DCCP_SOCKOPT_QPOLICY_TXQLEN:
		if (val < 0)
			err = -EINVAL;
		else
			dp->dccps_tx_qlen = val;
		break;
	default:
		err = -ENOPROTOOPT;
		break;
	}
	release_sock(sk);

	return err;
}

int dccp_setsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, unsigned int optlen)
{
	if (level != SOL_DCCP)
		return inet_csk(sk)->icsk_af_ops->setsockopt(sk, level,
							     optname, optval,
							     optlen);
	return do_dccp_setsockopt(sk, level, optname, optval, optlen);
}

EXPORT_SYMBOL_GPL(dccp_setsockopt);

#ifdef CONFIG_COMPAT
int compat_dccp_setsockopt(struct sock *sk, int level, int optname,
			   char __user *optval, unsigned int optlen)
{
	if (level != SOL_DCCP)
		return inet_csk_compat_setsockopt(sk, level, optname,
						  optval, optlen);
	return do_dccp_setsockopt(sk, level, optname, optval, optlen);
}

EXPORT_SYMBOL_GPL(compat_dccp_setsockopt);
#endif

static int dccp_getsockopt_service(struct sock *sk, int len,
				   __be32 __user *optval,
				   int __user *optlen)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const struct dccp_service_list *sl;
	int err = -ENOENT, slen = 0, total_len = sizeof(u32);

	lock_sock(sk);
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

static int do_dccp_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	struct dccp_sock *dp;
	int val, len;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < (int)sizeof(int))
		return -EINVAL;

	dp = dccp_sk(sk);

	switch (optname) {
	case DCCP_SOCKOPT_PACKET_SIZE:
		DCCP_WARN("sockopt(PACKET_SIZE) is deprecated: fix your app\n");
		return 0;
	case DCCP_SOCKOPT_SERVICE:
		return dccp_getsockopt_service(sk, len,
					       (__be32 __user *)optval, optlen);
	case DCCP_SOCKOPT_GET_CUR_MPS:
		val = dp->dccps_mss_cache;
		break;
	case DCCP_SOCKOPT_AVAILABLE_CCIDS:
		return ccid_getsockopt_builtin_ccids(sk, len, optval, optlen);
	case DCCP_SOCKOPT_TX_CCID:
		val = ccid_get_current_tx_ccid(dp);
		if (val < 0)
			return -ENOPROTOOPT;
		break;
	case DCCP_SOCKOPT_RX_CCID:
		val = ccid_get_current_rx_ccid(dp);
		if (val < 0)
			return -ENOPROTOOPT;
		break;
	case DCCP_SOCKOPT_SERVER_TIMEWAIT:
		val = dp->dccps_server_timewait;
		break;
	case DCCP_SOCKOPT_SEND_CSCOV:
		val = dp->dccps_pcslen;
		break;
	case DCCP_SOCKOPT_RECV_CSCOV:
		val = dp->dccps_pcrlen;
		break;
	case DCCP_SOCKOPT_QPOLICY_ID:
		val = dp->dccps_qpolicy;
		break;
	case DCCP_SOCKOPT_QPOLICY_TXQLEN:
		val = dp->dccps_tx_qlen;
		break;
	case 128 ... 191:
		return ccid_hc_rx_getsockopt(dp->dccps_hc_rx_ccid, sk, optname,
					     len, (u32 __user *)optval, optlen);
	case 192 ... 255:
		return ccid_hc_tx_getsockopt(dp->dccps_hc_tx_ccid, sk, optname,
					     len, (u32 __user *)optval, optlen);
	default:
		return -ENOPROTOOPT;
	}

	len = sizeof(val);
	if (put_user(len, optlen) || copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

int dccp_getsockopt(struct sock *sk, int level, int optname,
		    char __user *optval, int __user *optlen)
{
	if (level != SOL_DCCP)
		return inet_csk(sk)->icsk_af_ops->getsockopt(sk, level,
							     optname, optval,
							     optlen);
	return do_dccp_getsockopt(sk, level, optname, optval, optlen);
}

EXPORT_SYMBOL_GPL(dccp_getsockopt);

#ifdef CONFIG_COMPAT
int compat_dccp_getsockopt(struct sock *sk, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	if (level != SOL_DCCP)
		return inet_csk_compat_getsockopt(sk, level, optname,
						  optval, optlen);
	return do_dccp_getsockopt(sk, level, optname, optval, optlen);
}

EXPORT_SYMBOL_GPL(compat_dccp_getsockopt);
#endif

static int dccp_msghdr_parse(struct msghdr *msg, struct sk_buff *skb)
{
	struct cmsghdr *cmsg;

	/*
	 * Assign an (opaque) qpolicy priority value to skb->priority.
	 *
	 * We are overloading this skb field for use with the qpolicy subystem.
	 * The skb->priority is normally used for the SO_PRIORITY option, which
	 * is initialised from sk_priority. Since the assignment of sk_priority
	 * to skb->priority happens later (on layer 3), we overload this field
	 * for use with queueing priorities as long as the skb is on layer 4.
	 * The default priority value (if nothing is set) is 0.
	 */
	skb->priority = 0;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;

		if (cmsg->cmsg_level != SOL_DCCP)
			continue;

		if (cmsg->cmsg_type <= DCCP_SCM_QPOLICY_MAX &&
		    !dccp_qpolicy_param_ok(skb->sk, cmsg->cmsg_type))
			return -EINVAL;

		switch (cmsg->cmsg_type) {
		case DCCP_SCM_PRIORITY:
			if (cmsg->cmsg_len != CMSG_LEN(sizeof(__u32)))
				return -EINVAL;
			skb->priority = *(__u32 *)CMSG_DATA(cmsg);
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

int dccp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	const struct dccp_sock *dp = dccp_sk(sk);
	const int flags = msg->msg_flags;
	const int noblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb;
	int rc, size;
	long timeo;

	trace_dccp_probe(sk, len);

	if (len > dp->dccps_mss_cache)
		return -EMSGSIZE;

	lock_sock(sk);

	if (dccp_qpolicy_full(sk)) {
		rc = -EAGAIN;
		goto out_release;
	}

	timeo = sock_sndtimeo(sk, noblock);

	/*
	 * We have to use sk_stream_wait_connect here to set sk_write_pending,
	 * so that the trick in dccp_rcv_request_sent_state_process.
	 */
	/* Wait for a connection to finish. */
	if ((1 << sk->sk_state) & ~(DCCPF_OPEN | DCCPF_PARTOPEN))
		if ((rc = sk_stream_wait_connect(sk, &timeo)) != 0)
			goto out_release;

	size = sk->sk_prot->max_header + len;
	release_sock(sk);
	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	lock_sock(sk);
	if (skb == NULL)
		goto out_release;

	if (sk->sk_state == DCCP_CLOSED) {
		rc = -ENOTCONN;
		goto out_discard;
	}

	skb_reserve(skb, sk->sk_prot->max_header);
	rc = memcpy_from_msg(skb_put(skb, len), msg, len);
	if (rc != 0)
		goto out_discard;

	rc = dccp_msghdr_parse(msg, skb);
	if (rc != 0)
		goto out_discard;

	dccp_qpolicy_push(sk, skb);
	/*
	 * The xmit_timer is set if the TX CCID is rate-based and will expire
	 * when congestion control permits to release further packets into the
	 * network. Window-based CCIDs do not use this timer.
	 */
	if (!timer_pending(&dp->dccps_xmit_timer))
		dccp_write_xmit(sk);
out_release:
	release_sock(sk);
	return rc ? : len;
out_discard:
	kfree_skb(skb);
	goto out_release;
}

EXPORT_SYMBOL_GPL(dccp_sendmsg);

int dccp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int nonblock,
		 int flags, int *addr_len)
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

		switch (dh->dccph_type) {
		case DCCP_PKT_DATA:
		case DCCP_PKT_DATAACK:
			goto found_ok_skb;

		case DCCP_PKT_CLOSE:
		case DCCP_PKT_CLOSEREQ:
			if (!(flags & MSG_PEEK))
				dccp_finish_passive_close(sk);
			/* fall through */
		case DCCP_PKT_RESET:
			dccp_pr_debug("found fin (%s) ok!\n",
				      dccp_packet_name(dh->dccph_type));
			len = 0;
			goto found_fin_ok;
		default:
			dccp_pr_debug("packet_type=%s\n",
				      dccp_packet_name(dh->dccph_type));
			sk_eat_skb(sk, skb);
		}
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

		sk_wait_data(sk, &timeo, NULL);
		continue;
	found_ok_skb:
		if (len > skb->len)
			len = skb->len;
		else if (len < skb->len)
			msg->msg_flags |= MSG_TRUNC;

		if (skb_copy_datagram_msg(skb, 0, msg, len)) {
			/* Exception. Bailout! */
			len = -EFAULT;
			break;
		}
		if (flags & MSG_TRUNC)
			len = skb->len;
	found_fin_ok:
		if (!(flags & MSG_PEEK))
			sk_eat_skb(sk, skb);
		break;
	} while (1);
out:
	release_sock(sk);
	return len;
}

EXPORT_SYMBOL_GPL(dccp_recvmsg);

int inet_dccp_listen(struct socket *sock, int backlog)
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

	sk->sk_max_ack_backlog = backlog;
	/* Really, if the socket is already in listen state
	 * we can only allow the backlog to be adjusted.
	 */
	if (old_state != DCCP_LISTEN) {
		/*
		 * FIXME: here it probably should be sk->sk_prot->listen_start
		 * see tcp_listen_start
		 */
		err = dccp_listen_start(sk, backlog);
		if (err)
			goto out;
	}
	err = 0;

out:
	release_sock(sk);
	return err;
}

EXPORT_SYMBOL_GPL(inet_dccp_listen);

static void dccp_terminate_connection(struct sock *sk)
{
	u8 next_state = DCCP_CLOSED;

	switch (sk->sk_state) {
	case DCCP_PASSIVE_CLOSE:
	case DCCP_PASSIVE_CLOSEREQ:
		dccp_finish_passive_close(sk);
		break;
	case DCCP_PARTOPEN:
		dccp_pr_debug("Stop PARTOPEN timer (%p)\n", sk);
		inet_csk_clear_xmit_timer(sk, ICSK_TIME_DACK);
		/* fall through */
	case DCCP_OPEN:
		dccp_send_close(sk, 1);

		if (dccp_sk(sk)->dccps_role == DCCP_ROLE_SERVER &&
		    !dccp_sk(sk)->dccps_server_timewait)
			next_state = DCCP_ACTIVE_CLOSEREQ;
		else
			next_state = DCCP_CLOSING;
		/* fall through */
	default:
		dccp_set_state(sk, next_state);
	}
}

void dccp_close(struct sock *sk, long timeout)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct sk_buff *skb;
	u32 data_was_unread = 0;
	int state;

	lock_sock(sk);

	sk->sk_shutdown = SHUTDOWN_MASK;

	if (sk->sk_state == DCCP_LISTEN) {
		dccp_set_state(sk, DCCP_CLOSED);

		/* Special case. */
		inet_csk_listen_stop(sk);

		goto adjudge_to_death;
	}

	sk_stop_timer(sk, &dp->dccps_xmit_timer);

	/*
	 * We need to flush the recv. buffs.  We do this only on the
	 * descriptor close, not protocol-sourced closes, because the
	  *reader process may not have drained the data yet!
	 */
	while ((skb = __skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		data_was_unread += skb->len;
		__kfree_skb(skb);
	}

	/* If socket has been already reset kill it. */
	if (sk->sk_state == DCCP_CLOSED)
		goto adjudge_to_death;

	if (data_was_unread) {
		/* Unread data was tossed, send an appropriate Reset Code */
		DCCP_WARN("ABORT with %u bytes unread\n", data_was_unread);
		dccp_send_reset(sk, DCCP_RESET_CODE_ABORTED);
		dccp_set_state(sk, DCCP_CLOSED);
	} else if (sock_flag(sk, SOCK_LINGER) && !sk->sk_lingertime) {
		/* Check zero linger _after_ checking for unread data. */
		sk->sk_prot->disconnect(sk, 0);
	} else if (sk->sk_state != DCCP_CLOSED) {
		/*
		 * Normal connection termination. May need to wait if there are
		 * still packets in the TX queue that are delayed by the CCID.
		 */
		dccp_flush_write_queue(sk, &timeout);
		dccp_terminate_connection(sk);
	}

	/*
	 * Flush write queue. This may be necessary in several cases:
	 * - we have been closed by the peer but still have application data;
	 * - abortive termination (unread data or zero linger time),
	 * - normal termination but queue could not be flushed within time limit
	 */
	__skb_queue_purge(&sk->sk_write_queue);

	sk_stream_wait_close(sk, timeout);

adjudge_to_death:
	state = sk->sk_state;
	sock_hold(sk);
	sock_orphan(sk);

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
	WARN_ON(sock_owned_by_user(sk));

	percpu_counter_inc(sk->sk_prot->orphan_count);

	/* Have we already been destroyed by a softirq or backlog? */
	if (state != DCCP_CLOSED && sk->sk_state == DCCP_CLOSED)
		goto out;

	if (sk->sk_state == DCCP_CLOSED)
		inet_csk_destroy_sock(sk);

	/* Otherwise, socket is reprieved until protocol close. */

out:
	bh_unlock_sock(sk);
	local_bh_enable();
	sock_put(sk);
}

EXPORT_SYMBOL_GPL(dccp_close);

void dccp_shutdown(struct sock *sk, int how)
{
	dccp_pr_debug("called shutdown(%x)\n", how);
}

EXPORT_SYMBOL_GPL(dccp_shutdown);

static inline int __init dccp_mib_init(void)
{
	dccp_statistics = alloc_percpu(struct dccp_mib);
	if (!dccp_statistics)
		return -ENOMEM;
	return 0;
}

static inline void dccp_mib_exit(void)
{
	free_percpu(dccp_statistics);
}

static int thash_entries;
module_param(thash_entries, int, 0444);
MODULE_PARM_DESC(thash_entries, "Number of ehash buckets");

#ifdef CONFIG_IP_DCCP_DEBUG
bool dccp_debug;
module_param(dccp_debug, bool, 0644);
MODULE_PARM_DESC(dccp_debug, "Enable debug messages");

EXPORT_SYMBOL_GPL(dccp_debug);
#endif

static int __init dccp_init(void)
{
	unsigned long goal;
	unsigned long nr_pages = totalram_pages();
	int ehash_order, bhash_order, i;
	int rc;

	BUILD_BUG_ON(sizeof(struct dccp_skb_cb) >
		     FIELD_SIZEOF(struct sk_buff, cb));
	rc = percpu_counter_init(&dccp_orphan_count, 0, GFP_KERNEL);
	if (rc)
		goto out_fail;
	inet_hashinfo_init(&dccp_hashinfo);
	rc = inet_hashinfo2_init_mod(&dccp_hashinfo);
	if (rc)
		goto out_fail;
	rc = -ENOBUFS;
	dccp_hashinfo.bind_bucket_cachep =
		kmem_cache_create("dccp_bind_bucket",
				  sizeof(struct inet_bind_bucket), 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!dccp_hashinfo.bind_bucket_cachep)
		goto out_free_percpu;

	/*
	 * Size and allocate the main established and bind bucket
	 * hash tables.
	 *
	 * The methodology is similar to that of the buffer cache.
	 */
	if (nr_pages >= (128 * 1024))
		goal = nr_pages >> (21 - PAGE_SHIFT);
	else
		goal = nr_pages >> (23 - PAGE_SHIFT);

	if (thash_entries)
		goal = (thash_entries *
			sizeof(struct inet_ehash_bucket)) >> PAGE_SHIFT;
	for (ehash_order = 0; (1UL << ehash_order) < goal; ehash_order++)
		;
	do {
		unsigned long hash_size = (1UL << ehash_order) * PAGE_SIZE /
					sizeof(struct inet_ehash_bucket);

		while (hash_size & (hash_size - 1))
			hash_size--;
		dccp_hashinfo.ehash_mask = hash_size - 1;
		dccp_hashinfo.ehash = (struct inet_ehash_bucket *)
			__get_free_pages(GFP_ATOMIC|__GFP_NOWARN, ehash_order);
	} while (!dccp_hashinfo.ehash && --ehash_order > 0);

	if (!dccp_hashinfo.ehash) {
		DCCP_CRIT("Failed to allocate DCCP established hash table");
		goto out_free_bind_bucket_cachep;
	}

	for (i = 0; i <= dccp_hashinfo.ehash_mask; i++)
		INIT_HLIST_NULLS_HEAD(&dccp_hashinfo.ehash[i].chain, i);

	if (inet_ehash_locks_alloc(&dccp_hashinfo))
			goto out_free_dccp_ehash;

	bhash_order = ehash_order;

	do {
		dccp_hashinfo.bhash_size = (1UL << bhash_order) * PAGE_SIZE /
					sizeof(struct inet_bind_hashbucket);
		if ((dccp_hashinfo.bhash_size > (64 * 1024)) &&
		    bhash_order > 0)
			continue;
		dccp_hashinfo.bhash = (struct inet_bind_hashbucket *)
			__get_free_pages(GFP_ATOMIC|__GFP_NOWARN, bhash_order);
	} while (!dccp_hashinfo.bhash && --bhash_order >= 0);

	if (!dccp_hashinfo.bhash) {
		DCCP_CRIT("Failed to allocate DCCP bind hash table");
		goto out_free_dccp_locks;
	}

	for (i = 0; i < dccp_hashinfo.bhash_size; i++) {
		spin_lock_init(&dccp_hashinfo.bhash[i].lock);
		INIT_HLIST_HEAD(&dccp_hashinfo.bhash[i].chain);
	}

	rc = dccp_mib_init();
	if (rc)
		goto out_free_dccp_bhash;

	rc = dccp_ackvec_init();
	if (rc)
		goto out_free_dccp_mib;

	rc = dccp_sysctl_init();
	if (rc)
		goto out_ackvec_exit;

	rc = ccid_initialize_builtins();
	if (rc)
		goto out_sysctl_exit;

	dccp_timestamping_init();

	return 0;

out_sysctl_exit:
	dccp_sysctl_exit();
out_ackvec_exit:
	dccp_ackvec_exit();
out_free_dccp_mib:
	dccp_mib_exit();
out_free_dccp_bhash:
	free_pages((unsigned long)dccp_hashinfo.bhash, bhash_order);
out_free_dccp_locks:
	inet_ehash_locks_free(&dccp_hashinfo);
out_free_dccp_ehash:
	free_pages((unsigned long)dccp_hashinfo.ehash, ehash_order);
out_free_bind_bucket_cachep:
	kmem_cache_destroy(dccp_hashinfo.bind_bucket_cachep);
out_free_percpu:
	percpu_counter_destroy(&dccp_orphan_count);
out_fail:
	dccp_hashinfo.bhash = NULL;
	dccp_hashinfo.ehash = NULL;
	dccp_hashinfo.bind_bucket_cachep = NULL;
	return rc;
}

static void __exit dccp_fini(void)
{
	ccid_cleanup_builtins();
	dccp_mib_exit();
	free_pages((unsigned long)dccp_hashinfo.bhash,
		   get_order(dccp_hashinfo.bhash_size *
			     sizeof(struct inet_bind_hashbucket)));
	free_pages((unsigned long)dccp_hashinfo.ehash,
		   get_order((dccp_hashinfo.ehash_mask + 1) *
			     sizeof(struct inet_ehash_bucket)));
	inet_ehash_locks_free(&dccp_hashinfo);
	kmem_cache_destroy(dccp_hashinfo.bind_bucket_cachep);
	dccp_ackvec_exit();
	dccp_sysctl_exit();
	percpu_counter_destroy(&dccp_orphan_count);
}

module_init(dccp_init);
module_exit(dccp_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnaldo Carvalho de Melo <acme@conectiva.com.br>");
MODULE_DESCRIPTION("DCCP - Datagram Congestion Controlled Protocol");
