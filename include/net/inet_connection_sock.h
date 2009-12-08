/*
 * NET		Generic infrastructure for INET connection oriented protocols.
 *
 *		Definitions for inet_connection_sock 
 *
 * Authors:	Many people, see the TCP sources
 *
 * 		From code originally in TCP
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _INET_CONNECTION_SOCK_H
#define _INET_CONNECTION_SOCK_H

#include <linux/compiler.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/poll.h>

#include <net/inet_sock.h>
#include <net/request_sock.h>

#define INET_CSK_DEBUG 1

/* Cancel timers, when they are not required. */
#undef INET_CSK_CLEAR_TIMERS

struct inet_bind_bucket;
struct tcp_congestion_ops;

/*
 * Pointers to address related TCP functions
 * (i.e. things that depend on the address family)
 */
struct inet_connection_sock_af_ops {
	int	    (*queue_xmit)(struct sk_buff *skb, int ipfragok);
	void	    (*send_check)(struct sock *sk, int len,
				  struct sk_buff *skb);
	int	    (*rebuild_header)(struct sock *sk);
	int	    (*conn_request)(struct sock *sk, struct sk_buff *skb);
	struct sock *(*syn_recv_sock)(struct sock *sk, struct sk_buff *skb,
				      struct request_sock *req,
				      struct dst_entry *dst);
	int	    (*remember_stamp)(struct sock *sk);
	u16	    net_header_len;
	u16	    sockaddr_len;
	int	    (*setsockopt)(struct sock *sk, int level, int optname, 
				  char __user *optval, unsigned int optlen);
	int	    (*getsockopt)(struct sock *sk, int level, int optname, 
				  char __user *optval, int __user *optlen);
#ifdef CONFIG_COMPAT
	int	    (*compat_setsockopt)(struct sock *sk,
				int level, int optname,
				char __user *optval, unsigned int optlen);
	int	    (*compat_getsockopt)(struct sock *sk,
				int level, int optname,
				char __user *optval, int __user *optlen);
#endif
	void	    (*addr2sockaddr)(struct sock *sk, struct sockaddr *);
	int	    (*bind_conflict)(const struct sock *sk,
				     const struct inet_bind_bucket *tb);
};

/** inet_connection_sock - INET connection oriented sock
 *
 * @icsk_accept_queue:	   FIFO of established children 
 * @icsk_bind_hash:	   Bind node
 * @icsk_timeout:	   Timeout
 * @icsk_retransmit_timer: Resend (no ack)
 * @icsk_rto:		   Retransmit timeout
 * @icsk_pmtu_cookie	   Last pmtu seen by socket
 * @icsk_ca_ops		   Pluggable congestion control hook
 * @icsk_af_ops		   Operations which are AF_INET{4,6} specific
 * @icsk_ca_state:	   Congestion control state
 * @icsk_retransmits:	   Number of unrecovered [RTO] timeouts
 * @icsk_pending:	   Scheduled timer event
 * @icsk_backoff:	   Backoff
 * @icsk_syn_retries:      Number of allowed SYN (or equivalent) retries
 * @icsk_probes_out:	   unanswered 0 window probes
 * @icsk_ext_hdr_len:	   Network protocol overhead (IP/IPv6 options)
 * @icsk_ack:		   Delayed ACK control data
 * @icsk_mtup;		   MTU probing control data
 */
struct inet_connection_sock {
	/* inet_sock has to be the first member! */
	struct inet_sock	  icsk_inet;
	struct request_sock_queue icsk_accept_queue;
	struct inet_bind_bucket	  *icsk_bind_hash;
	unsigned long		  icsk_timeout;
 	struct timer_list	  icsk_retransmit_timer;
 	struct timer_list	  icsk_delack_timer;
	__u32			  icsk_rto;
	__u32			  icsk_pmtu_cookie;
	const struct tcp_congestion_ops *icsk_ca_ops;
	const struct inet_connection_sock_af_ops *icsk_af_ops;
	unsigned int		  (*icsk_sync_mss)(struct sock *sk, u32 pmtu);
	__u8			  icsk_ca_state;
	__u8			  icsk_retransmits;
	__u8			  icsk_pending;
	__u8			  icsk_backoff;
	__u8			  icsk_syn_retries;
	__u8			  icsk_probes_out;
	__u16			  icsk_ext_hdr_len;
	struct {
		__u8		  pending;	 /* ACK is pending			   */
		__u8		  quick;	 /* Scheduled number of quick acks	   */
		__u8		  pingpong;	 /* The session is interactive		   */
		__u8		  blocked;	 /* Delayed ACK was blocked by socket lock */
		__u32		  ato;		 /* Predicted tick of soft clock	   */
		unsigned long	  timeout;	 /* Currently scheduled timeout		   */
		__u32		  lrcvtime;	 /* timestamp of last received data packet */
		__u16		  last_seg_size; /* Size of last incoming segment	   */
		__u16		  rcv_mss;	 /* MSS used for delayed ACK decisions	   */ 
	} icsk_ack;
	struct {
		int		  enabled;

		/* Range of MTUs to search */
		int		  search_high;
		int		  search_low;

		/* Information on the current probe. */
		int		  probe_size;
	} icsk_mtup;
	u32			  icsk_ca_priv[16];
#define ICSK_CA_PRIV_SIZE	(16 * sizeof(u32))
};

#define ICSK_TIME_RETRANS	1	/* Retransmit timer */
#define ICSK_TIME_DACK		2	/* Delayed ack timer */
#define ICSK_TIME_PROBE0	3	/* Zero window probe timer */
#define ICSK_TIME_KEEPOPEN	4	/* Keepalive timer */

static inline struct inet_connection_sock *inet_csk(const struct sock *sk)
{
	return (struct inet_connection_sock *)sk;
}

static inline void *inet_csk_ca(const struct sock *sk)
{
	return (void *)inet_csk(sk)->icsk_ca_priv;
}

extern struct sock *inet_csk_clone(struct sock *sk,
				   const struct request_sock *req,
				   const gfp_t priority);

enum inet_csk_ack_state_t {
	ICSK_ACK_SCHED	= 1,
	ICSK_ACK_TIMER  = 2,
	ICSK_ACK_PUSHED = 4,
	ICSK_ACK_PUSHED2 = 8
};

extern void inet_csk_init_xmit_timers(struct sock *sk,
				      void (*retransmit_handler)(unsigned long),
				      void (*delack_handler)(unsigned long),
				      void (*keepalive_handler)(unsigned long));
extern void inet_csk_clear_xmit_timers(struct sock *sk);

static inline void inet_csk_schedule_ack(struct sock *sk)
{
	inet_csk(sk)->icsk_ack.pending |= ICSK_ACK_SCHED;
}

static inline int inet_csk_ack_scheduled(const struct sock *sk)
{
	return inet_csk(sk)->icsk_ack.pending & ICSK_ACK_SCHED;
}

static inline void inet_csk_delack_init(struct sock *sk)
{
	memset(&inet_csk(sk)->icsk_ack, 0, sizeof(inet_csk(sk)->icsk_ack));
}

extern void inet_csk_delete_keepalive_timer(struct sock *sk);
extern void inet_csk_reset_keepalive_timer(struct sock *sk, unsigned long timeout);

#ifdef INET_CSK_DEBUG
extern const char inet_csk_timer_bug_msg[];
#endif

static inline void inet_csk_clear_xmit_timer(struct sock *sk, const int what)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	
	if (what == ICSK_TIME_RETRANS || what == ICSK_TIME_PROBE0) {
		icsk->icsk_pending = 0;
#ifdef INET_CSK_CLEAR_TIMERS
		sk_stop_timer(sk, &icsk->icsk_retransmit_timer);
#endif
	} else if (what == ICSK_TIME_DACK) {
		icsk->icsk_ack.blocked = icsk->icsk_ack.pending = 0;
#ifdef INET_CSK_CLEAR_TIMERS
		sk_stop_timer(sk, &icsk->icsk_delack_timer);
#endif
	}
#ifdef INET_CSK_DEBUG
	else {
		pr_debug("%s", inet_csk_timer_bug_msg);
	}
#endif
}

/*
 *	Reset the retransmission timer
 */
static inline void inet_csk_reset_xmit_timer(struct sock *sk, const int what,
					     unsigned long when,
					     const unsigned long max_when)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	if (when > max_when) {
#ifdef INET_CSK_DEBUG
		pr_debug("reset_xmit_timer: sk=%p %d when=0x%lx, caller=%p\n",
			 sk, what, when, current_text_addr());
#endif
		when = max_when;
	}

	if (what == ICSK_TIME_RETRANS || what == ICSK_TIME_PROBE0) {
		icsk->icsk_pending = what;
		icsk->icsk_timeout = jiffies + when;
		sk_reset_timer(sk, &icsk->icsk_retransmit_timer, icsk->icsk_timeout);
	} else if (what == ICSK_TIME_DACK) {
		icsk->icsk_ack.pending |= ICSK_ACK_TIMER;
		icsk->icsk_ack.timeout = jiffies + when;
		sk_reset_timer(sk, &icsk->icsk_delack_timer, icsk->icsk_ack.timeout);
	}
#ifdef INET_CSK_DEBUG
	else {
		pr_debug("%s", inet_csk_timer_bug_msg);
	}
#endif
}

extern struct sock *inet_csk_accept(struct sock *sk, int flags, int *err);

extern struct request_sock *inet_csk_search_req(const struct sock *sk,
						struct request_sock ***prevp,
						const __be16 rport,
						const __be32 raddr,
						const __be32 laddr);
extern int inet_csk_bind_conflict(const struct sock *sk,
				  const struct inet_bind_bucket *tb);
extern int inet_csk_get_port(struct sock *sk, unsigned short snum);

extern struct dst_entry* inet_csk_route_req(struct sock *sk,
					    const struct request_sock *req);

static inline void inet_csk_reqsk_queue_add(struct sock *sk,
					    struct request_sock *req,
					    struct sock *child)
{
	reqsk_queue_add(&inet_csk(sk)->icsk_accept_queue, req, sk, child);
}

extern void inet_csk_reqsk_queue_hash_add(struct sock *sk,
					  struct request_sock *req,
					  unsigned long timeout);

static inline void inet_csk_reqsk_queue_removed(struct sock *sk,
						struct request_sock *req)
{
	if (reqsk_queue_removed(&inet_csk(sk)->icsk_accept_queue, req) == 0)
		inet_csk_delete_keepalive_timer(sk);
}

static inline void inet_csk_reqsk_queue_added(struct sock *sk,
					      const unsigned long timeout)
{
	if (reqsk_queue_added(&inet_csk(sk)->icsk_accept_queue) == 0)
		inet_csk_reset_keepalive_timer(sk, timeout);
}

static inline int inet_csk_reqsk_queue_len(const struct sock *sk)
{
	return reqsk_queue_len(&inet_csk(sk)->icsk_accept_queue);
}

static inline int inet_csk_reqsk_queue_young(const struct sock *sk)
{
	return reqsk_queue_len_young(&inet_csk(sk)->icsk_accept_queue);
}

static inline int inet_csk_reqsk_queue_is_full(const struct sock *sk)
{
	return reqsk_queue_is_full(&inet_csk(sk)->icsk_accept_queue);
}

static inline void inet_csk_reqsk_queue_unlink(struct sock *sk,
					       struct request_sock *req,
					       struct request_sock **prev)
{
	reqsk_queue_unlink(&inet_csk(sk)->icsk_accept_queue, req, prev);
}

static inline void inet_csk_reqsk_queue_drop(struct sock *sk,
					     struct request_sock *req,
					     struct request_sock **prev)
{
	inet_csk_reqsk_queue_unlink(sk, req, prev);
	inet_csk_reqsk_queue_removed(sk, req);
	reqsk_free(req);
}

extern void inet_csk_reqsk_queue_prune(struct sock *parent,
				       const unsigned long interval,
				       const unsigned long timeout,
				       const unsigned long max_rto);

extern void inet_csk_destroy_sock(struct sock *sk);

/*
 * LISTEN is a special case for poll..
 */
static inline unsigned int inet_csk_listen_poll(const struct sock *sk)
{
	return !reqsk_queue_empty(&inet_csk(sk)->icsk_accept_queue) ?
			(POLLIN | POLLRDNORM) : 0;
}

extern int  inet_csk_listen_start(struct sock *sk, const int nr_table_entries);
extern void inet_csk_listen_stop(struct sock *sk);

extern void inet_csk_addr2sockaddr(struct sock *sk, struct sockaddr *uaddr);

extern int inet_csk_compat_getsockopt(struct sock *sk, int level, int optname,
				      char __user *optval, int __user *optlen);
extern int inet_csk_compat_setsockopt(struct sock *sk, int level, int optname,
				      char __user *optval, unsigned int optlen);
#endif /* _INET_CONNECTION_SOCK_H */
