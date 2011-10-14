/*
 *	X.25 Packet Layer release 002
 *
 *	This is ALPHA test software. This code may break your machine,
 *	randomly fail to work with new releases, misbehave and/or generally
 *	screw up. It might even work.
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 *	X.25 002	Jonathan Naylor	Centralised disconnect handling.
 *					New timer architecture.
 *	2000-03-11	Henner Eisen	MSG_EOR handling more POSIX compliant.
 *	2000-03-22	Daniela Squassoni Allowed disabling/enabling of
 *					  facilities negotiation and increased
 *					  the throughput upper limit.
 *	2000-08-27	Arnaldo C. Melo s/suser/capable/ + micro cleanups
 *	2000-09-04	Henner Eisen	Set sock->state in x25_accept().
 *					Fixed x25_output() related skb leakage.
 *	2000-10-02	Henner Eisen	Made x25_kick() single threaded per socket.
 *	2000-10-27	Henner Eisen    MSG_DONTWAIT for fragment allocation.
 *	2000-11-14	Henner Eisen    Closing datalink from NETDEV_GOING_DOWN
 *	2002-10-06	Arnaldo C. Melo Get rid of cli/sti, move proc stuff to
 *					x25_proc.c, using seq_file
 *	2005-04-02	Shaun Pereira	Selective sub address matching
 *					with call user data
 *	2005-04-15	Shaun Pereira	Fast select with no restriction on
 *					response
 */

#include <linux/module.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/ctype.h>

#include <net/x25.h>
#include <net/compat.h>

int sysctl_x25_restart_request_timeout = X25_DEFAULT_T20;
int sysctl_x25_call_request_timeout    = X25_DEFAULT_T21;
int sysctl_x25_reset_request_timeout   = X25_DEFAULT_T22;
int sysctl_x25_clear_request_timeout   = X25_DEFAULT_T23;
int sysctl_x25_ack_holdback_timeout    = X25_DEFAULT_T2;
int sysctl_x25_forward                 = 0;

HLIST_HEAD(x25_list);
DEFINE_RWLOCK(x25_list_lock);

static const struct proto_ops x25_proto_ops;

static struct x25_address null_x25_address = {"               "};

#ifdef CONFIG_COMPAT
struct compat_x25_subscrip_struct {
	char device[200-sizeof(compat_ulong_t)];
	compat_ulong_t global_facil_mask;
	compat_uint_t extended;
};
#endif


int x25_parse_address_block(struct sk_buff *skb,
		struct x25_address *called_addr,
		struct x25_address *calling_addr)
{
	unsigned char len;
	int needed;
	int rc;

	if (skb->len < 1) {
		/* packet has no address block */
		rc = 0;
		goto empty;
	}

	len = *skb->data;
	needed = 1 + (len >> 4) + (len & 0x0f);

	if (skb->len < needed) {
		/* packet is too short to hold the addresses it claims
		   to hold */
		rc = -1;
		goto empty;
	}

	return x25_addr_ntoa(skb->data, called_addr, calling_addr);

empty:
	*called_addr->x25_addr = 0;
	*calling_addr->x25_addr = 0;

	return rc;
}


int x25_addr_ntoa(unsigned char *p, struct x25_address *called_addr,
		  struct x25_address *calling_addr)
{
	unsigned int called_len, calling_len;
	char *called, *calling;
	unsigned int i;

	called_len  = (*p >> 0) & 0x0F;
	calling_len = (*p >> 4) & 0x0F;

	called  = called_addr->x25_addr;
	calling = calling_addr->x25_addr;
	p++;

	for (i = 0; i < (called_len + calling_len); i++) {
		if (i < called_len) {
			if (i % 2 != 0) {
				*called++ = ((*p >> 0) & 0x0F) + '0';
				p++;
			} else {
				*called++ = ((*p >> 4) & 0x0F) + '0';
			}
		} else {
			if (i % 2 != 0) {
				*calling++ = ((*p >> 0) & 0x0F) + '0';
				p++;
			} else {
				*calling++ = ((*p >> 4) & 0x0F) + '0';
			}
		}
	}

	*called = *calling = '\0';

	return 1 + (called_len + calling_len + 1) / 2;
}

int x25_addr_aton(unsigned char *p, struct x25_address *called_addr,
		  struct x25_address *calling_addr)
{
	unsigned int called_len, calling_len;
	char *called, *calling;
	int i;

	called  = called_addr->x25_addr;
	calling = calling_addr->x25_addr;

	called_len  = strlen(called);
	calling_len = strlen(calling);

	*p++ = (calling_len << 4) | (called_len << 0);

	for (i = 0; i < (called_len + calling_len); i++) {
		if (i < called_len) {
			if (i % 2 != 0) {
				*p |= (*called++ - '0') << 0;
				p++;
			} else {
				*p = 0x00;
				*p |= (*called++ - '0') << 4;
			}
		} else {
			if (i % 2 != 0) {
				*p |= (*calling++ - '0') << 0;
				p++;
			} else {
				*p = 0x00;
				*p |= (*calling++ - '0') << 4;
			}
		}
	}

	return 1 + (called_len + calling_len + 1) / 2;
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void x25_remove_socket(struct sock *sk)
{
	write_lock_bh(&x25_list_lock);
	sk_del_node_init(sk);
	write_unlock_bh(&x25_list_lock);
}

/*
 *	Kill all bound sockets on a dropped device.
 */
static void x25_kill_by_device(struct net_device *dev)
{
	struct sock *s;
	struct hlist_node *node;

	write_lock_bh(&x25_list_lock);

	sk_for_each(s, node, &x25_list)
		if (x25_sk(s)->neighbour && x25_sk(s)->neighbour->dev == dev)
			x25_disconnect(s, ENETUNREACH, 0, 0);

	write_unlock_bh(&x25_list_lock);
}

/*
 *	Handle device status changes.
 */
static int x25_device_event(struct notifier_block *this, unsigned long event,
			    void *ptr)
{
	struct net_device *dev = ptr;
	struct x25_neigh *nb;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (dev->type == ARPHRD_X25
#if defined(CONFIG_LLC) || defined(CONFIG_LLC_MODULE)
	 || dev->type == ARPHRD_ETHER
#endif
	 ) {
		switch (event) {
			case NETDEV_UP:
				x25_link_device_up(dev);
				break;
			case NETDEV_GOING_DOWN:
				nb = x25_get_neigh(dev);
				if (nb) {
					x25_terminate_link(nb);
					x25_neigh_put(nb);
				}
				break;
			case NETDEV_DOWN:
				x25_kill_by_device(dev);
				x25_route_device_down(dev);
				x25_link_device_down(dev);
				break;
		}
	}

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void x25_insert_socket(struct sock *sk)
{
	write_lock_bh(&x25_list_lock);
	sk_add_node(sk, &x25_list);
	write_unlock_bh(&x25_list_lock);
}

/*
 *	Find a socket that wants to accept the Call Request we just
 *	received. Check the full list for an address/cud match.
 *	If no cuds match return the next_best thing, an address match.
 *	Note: if a listening socket has cud set it must only get calls
 *	with matching cud.
 */
static struct sock *x25_find_listener(struct x25_address *addr,
					struct sk_buff *skb)
{
	struct sock *s;
	struct sock *next_best;
	struct hlist_node *node;

	read_lock_bh(&x25_list_lock);
	next_best = NULL;

	sk_for_each(s, node, &x25_list)
		if ((!strcmp(addr->x25_addr,
			x25_sk(s)->source_addr.x25_addr) ||
				!strcmp(addr->x25_addr,
					null_x25_address.x25_addr)) &&
					s->sk_state == TCP_LISTEN) {
			/*
			 * Found a listening socket, now check the incoming
			 * call user data vs this sockets call user data
			 */
			if (x25_sk(s)->cudmatchlength > 0 &&
				skb->len >= x25_sk(s)->cudmatchlength) {
				if((memcmp(x25_sk(s)->calluserdata.cuddata,
					skb->data,
					x25_sk(s)->cudmatchlength)) == 0) {
					sock_hold(s);
					goto found;
				 }
			} else
				next_best = s;
		}
	if (next_best) {
		s = next_best;
		sock_hold(s);
		goto found;
	}
	s = NULL;
found:
	read_unlock_bh(&x25_list_lock);
	return s;
}

/*
 *	Find a connected X.25 socket given my LCI and neighbour.
 */
static struct sock *__x25_find_socket(unsigned int lci, struct x25_neigh *nb)
{
	struct sock *s;
	struct hlist_node *node;

	sk_for_each(s, node, &x25_list)
		if (x25_sk(s)->lci == lci && x25_sk(s)->neighbour == nb) {
			sock_hold(s);
			goto found;
		}
	s = NULL;
found:
	return s;
}

struct sock *x25_find_socket(unsigned int lci, struct x25_neigh *nb)
{
	struct sock *s;

	read_lock_bh(&x25_list_lock);
	s = __x25_find_socket(lci, nb);
	read_unlock_bh(&x25_list_lock);
	return s;
}

/*
 *	Find a unique LCI for a given device.
 */
static unsigned int x25_new_lci(struct x25_neigh *nb)
{
	unsigned int lci = 1;
	struct sock *sk;

	read_lock_bh(&x25_list_lock);

	while ((sk = __x25_find_socket(lci, nb)) != NULL) {
		sock_put(sk);
		if (++lci == 4096) {
			lci = 0;
			break;
		}
	}

	read_unlock_bh(&x25_list_lock);
	return lci;
}

/*
 *	Deferred destroy.
 */
static void __x25_destroy_socket(struct sock *);

/*
 *	handler for deferred kills.
 */
static void x25_destroy_timer(unsigned long data)
{
	x25_destroy_socket_from_timer((struct sock *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself
 *	against interrupt users but doesn't worry about being called during
 *	work. Once it is removed from the queue no interrupt or bottom half
 *	will touch it and we are (fairly 8-) ) safe.
 *	Not static as it's used by the timer
 */
static void __x25_destroy_socket(struct sock *sk)
{
	struct sk_buff *skb;

	x25_stop_heartbeat(sk);
	x25_stop_timer(sk);

	x25_remove_socket(sk);
	x25_clear_queues(sk);		/* Flush the queues */

	while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		if (skb->sk != sk) {		/* A pending connection */
			/*
			 * Queue the unaccepted socket for death
			 */
			skb->sk->sk_state = TCP_LISTEN;
			sock_set_flag(skb->sk, SOCK_DEAD);
			x25_start_heartbeat(skb->sk);
			x25_sk(skb->sk)->state = X25_STATE_0;
		}

		kfree_skb(skb);
	}

	if (sk_has_allocations(sk)) {
		/* Defer: outstanding buffers */
		sk->sk_timer.expires  = jiffies + 10 * HZ;
		sk->sk_timer.function = x25_destroy_timer;
		sk->sk_timer.data = (unsigned long)sk;
		add_timer(&sk->sk_timer);
	} else {
		/* drop last reference so sock_put will free */
		__sock_put(sk);
	}
}

void x25_destroy_socket_from_timer(struct sock *sk)
{
	sock_hold(sk);
	bh_lock_sock(sk);
	__x25_destroy_socket(sk);
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	X.25 socket object.
 */

static int x25_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	int opt;
	struct sock *sk = sock->sk;
	int rc = -ENOPROTOOPT;

	if (level != SOL_X25 || optname != X25_QBITINCL)
		goto out;

	rc = -EINVAL;
	if (optlen < sizeof(int))
		goto out;

	rc = -EFAULT;
	if (get_user(opt, (int __user *)optval))
		goto out;

	if (opt)
		set_bit(X25_Q_BIT_FLAG, &x25_sk(sk)->flags);
	else
		clear_bit(X25_Q_BIT_FLAG, &x25_sk(sk)->flags);
	rc = 0;
out:
	return rc;
}

static int x25_getsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	int val, len, rc = -ENOPROTOOPT;

	if (level != SOL_X25 || optname != X25_QBITINCL)
		goto out;

	rc = -EFAULT;
	if (get_user(len, optlen))
		goto out;

	len = min_t(unsigned int, len, sizeof(int));

	rc = -EINVAL;
	if (len < 0)
		goto out;

	rc = -EFAULT;
	if (put_user(len, optlen))
		goto out;

	val = test_bit(X25_Q_BIT_FLAG, &x25_sk(sk)->flags);
	rc = copy_to_user(optval, &val, len) ? -EFAULT : 0;
out:
	return rc;
}

static int x25_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int rc = -EOPNOTSUPP;

	lock_sock(sk);
	if (sk->sk_state != TCP_LISTEN) {
		memset(&x25_sk(sk)->dest_addr, 0, X25_ADDR_LEN);
		sk->sk_max_ack_backlog = backlog;
		sk->sk_state           = TCP_LISTEN;
		rc = 0;
	}
	release_sock(sk);

	return rc;
}

static struct proto x25_proto = {
	.name	  = "X25",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct x25_sock),
};

static struct sock *x25_alloc_socket(struct net *net)
{
	struct x25_sock *x25;
	struct sock *sk = sk_alloc(net, AF_X25, GFP_ATOMIC, &x25_proto);

	if (!sk)
		goto out;

	sock_init_data(NULL, sk);

	x25 = x25_sk(sk);
	skb_queue_head_init(&x25->ack_queue);
	skb_queue_head_init(&x25->fragment_queue);
	skb_queue_head_init(&x25->interrupt_in_queue);
	skb_queue_head_init(&x25->interrupt_out_queue);
out:
	return sk;
}

static int x25_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;
	struct x25_sock *x25;
	int rc = -EAFNOSUPPORT;

	if (!net_eq(net, &init_net))
		goto out;

	rc = -ESOCKTNOSUPPORT;
	if (sock->type != SOCK_SEQPACKET)
		goto out;

	rc = -EINVAL;
	if (protocol)
		goto out;

	rc = -ENOBUFS;
	if ((sk = x25_alloc_socket(net)) == NULL)
		goto out;

	x25 = x25_sk(sk);

	sock_init_data(sock, sk);

	x25_init_timers(sk);

	sock->ops    = &x25_proto_ops;
	sk->sk_protocol = protocol;
	sk->sk_backlog_rcv = x25_backlog_rcv;

	x25->t21   = sysctl_x25_call_request_timeout;
	x25->t22   = sysctl_x25_reset_request_timeout;
	x25->t23   = sysctl_x25_clear_request_timeout;
	x25->t2    = sysctl_x25_ack_holdback_timeout;
	x25->state = X25_STATE_0;
	x25->cudmatchlength = 0;
	set_bit(X25_ACCPT_APPRV_FLAG, &x25->flags);	/* normally no cud  */
							/* on call accept   */

	x25->facilities.winsize_in  = X25_DEFAULT_WINDOW_SIZE;
	x25->facilities.winsize_out = X25_DEFAULT_WINDOW_SIZE;
	x25->facilities.pacsize_in  = X25_DEFAULT_PACKET_SIZE;
	x25->facilities.pacsize_out = X25_DEFAULT_PACKET_SIZE;
	x25->facilities.throughput  = 0;	/* by default don't negotiate
						   throughput */
	x25->facilities.reverse     = X25_DEFAULT_REVERSE;
	x25->dte_facilities.calling_len = 0;
	x25->dte_facilities.called_len = 0;
	memset(x25->dte_facilities.called_ae, '\0',
			sizeof(x25->dte_facilities.called_ae));
	memset(x25->dte_facilities.calling_ae, '\0',
			sizeof(x25->dte_facilities.calling_ae));

	rc = 0;
out:
	return rc;
}

static struct sock *x25_make_new(struct sock *osk)
{
	struct sock *sk = NULL;
	struct x25_sock *x25, *ox25;

	if (osk->sk_type != SOCK_SEQPACKET)
		goto out;

	if ((sk = x25_alloc_socket(sock_net(osk))) == NULL)
		goto out;

	x25 = x25_sk(sk);

	sk->sk_type        = osk->sk_type;
	sk->sk_priority    = osk->sk_priority;
	sk->sk_protocol    = osk->sk_protocol;
	sk->sk_rcvbuf      = osk->sk_rcvbuf;
	sk->sk_sndbuf      = osk->sk_sndbuf;
	sk->sk_state       = TCP_ESTABLISHED;
	sk->sk_backlog_rcv = osk->sk_backlog_rcv;
	sock_copy_flags(sk, osk);

	ox25 = x25_sk(osk);
	x25->t21        = ox25->t21;
	x25->t22        = ox25->t22;
	x25->t23        = ox25->t23;
	x25->t2         = ox25->t2;
	x25->flags	= ox25->flags;
	x25->facilities = ox25->facilities;
	x25->dte_facilities = ox25->dte_facilities;
	x25->cudmatchlength = ox25->cudmatchlength;

	clear_bit(X25_INTERRUPT_FLAG, &x25->flags);
	x25_init_timers(sk);
out:
	return sk;
}

static int x25_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct x25_sock *x25;

	if (!sk)
		return 0;

	x25 = x25_sk(sk);

	sock_hold(sk);
	lock_sock(sk);
	switch (x25->state) {

		case X25_STATE_0:
		case X25_STATE_2:
			x25_disconnect(sk, 0, 0, 0);
			__x25_destroy_socket(sk);
			goto out;

		case X25_STATE_1:
		case X25_STATE_3:
		case X25_STATE_4:
			x25_clear_queues(sk);
			x25_write_internal(sk, X25_CLEAR_REQUEST);
			x25_start_t23timer(sk);
			x25->state = X25_STATE_2;
			sk->sk_state	= TCP_CLOSE;
			sk->sk_shutdown	|= SEND_SHUTDOWN;
			sk->sk_state_change(sk);
			sock_set_flag(sk, SOCK_DEAD);
			sock_set_flag(sk, SOCK_DESTROY);
			break;
	}

	sock_orphan(sk);
out:
	release_sock(sk);
	sock_put(sk);
	return 0;
}

static int x25_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_x25 *addr = (struct sockaddr_x25 *)uaddr;
	int len, i, rc = 0;

	if (!sock_flag(sk, SOCK_ZAPPED) ||
	    addr_len != sizeof(struct sockaddr_x25) ||
	    addr->sx25_family != AF_X25) {
		rc = -EINVAL;
		goto out;
	}

	len = strlen(addr->sx25_addr.x25_addr);
	for (i = 0; i < len; i++) {
		if (!isdigit(addr->sx25_addr.x25_addr[i])) {
			rc = -EINVAL;
			goto out;
		}
	}

	lock_sock(sk);
	x25_sk(sk)->source_addr = addr->sx25_addr;
	x25_insert_socket(sk);
	sock_reset_flag(sk, SOCK_ZAPPED);
	release_sock(sk);
	SOCK_DEBUG(sk, "x25_bind: socket is bound\n");
out:
	return rc;
}

static int x25_wait_for_connection_establishment(struct sock *sk)
{
	DECLARE_WAITQUEUE(wait, current);
	int rc;

	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		rc = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		rc = sock_error(sk);
		if (rc) {
			sk->sk_socket->state = SS_UNCONNECTED;
			break;
		}
		rc = 0;
		if (sk->sk_state != TCP_ESTABLISHED) {
			release_sock(sk);
			schedule();
			lock_sock(sk);
		} else
			break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return rc;
}

static int x25_connect(struct socket *sock, struct sockaddr *uaddr,
		       int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct x25_sock *x25 = x25_sk(sk);
	struct sockaddr_x25 *addr = (struct sockaddr_x25 *)uaddr;
	struct x25_route *rt;
	int rc = 0;

	lock_sock(sk);
	if (sk->sk_state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		goto out; /* Connect completed during a ERESTARTSYS event */
	}

	rc = -ECONNREFUSED;
	if (sk->sk_state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		goto out;
	}

	rc = -EISCONN;	/* No reconnect on a seqpacket socket */
	if (sk->sk_state == TCP_ESTABLISHED)
		goto out;

	sk->sk_state   = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	rc = -EINVAL;
	if (addr_len != sizeof(struct sockaddr_x25) ||
	    addr->sx25_family != AF_X25)
		goto out;

	rc = -ENETUNREACH;
	rt = x25_get_route(&addr->sx25_addr);
	if (!rt)
		goto out;

	x25->neighbour = x25_get_neigh(rt->dev);
	if (!x25->neighbour)
		goto out_put_route;

	x25_limit_facilities(&x25->facilities, x25->neighbour);

	x25->lci = x25_new_lci(x25->neighbour);
	if (!x25->lci)
		goto out_put_neigh;

	rc = -EINVAL;
	if (sock_flag(sk, SOCK_ZAPPED)) /* Must bind first - autobinding does not work */
		goto out_put_neigh;

	if (!strcmp(x25->source_addr.x25_addr, null_x25_address.x25_addr))
		memset(&x25->source_addr, '\0', X25_ADDR_LEN);

	x25->dest_addr = addr->sx25_addr;

	/* Move to connecting socket, start sending Connect Requests */
	sock->state   = SS_CONNECTING;
	sk->sk_state  = TCP_SYN_SENT;

	x25->state = X25_STATE_1;

	x25_write_internal(sk, X25_CALL_REQUEST);

	x25_start_heartbeat(sk);
	x25_start_t21timer(sk);

	/* Now the loop */
	rc = -EINPROGRESS;
	if (sk->sk_state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		goto out_put_neigh;

	rc = x25_wait_for_connection_establishment(sk);
	if (rc)
		goto out_put_neigh;

	sock->state = SS_CONNECTED;
	rc = 0;
out_put_neigh:
	if (rc)
		x25_neigh_put(x25->neighbour);
out_put_route:
	x25_route_put(rt);
out:
	release_sock(sk);
	return rc;
}

static int x25_wait_for_data(struct sock *sk, long timeout)
{
	DECLARE_WAITQUEUE(wait, current);
	int rc = 0;

	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			break;
		rc = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		rc = -EAGAIN;
		if (!timeout)
			break;
		rc = 0;
		if (skb_queue_empty(&sk->sk_receive_queue)) {
			release_sock(sk);
			timeout = schedule_timeout(timeout);
			lock_sock(sk);
		} else
			break;
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return rc;
}

static int x25_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk;
	struct sock *newsk;
	struct sk_buff *skb;
	int rc = -EINVAL;

	if (!sk)
		goto out;

	rc = -EOPNOTSUPP;
	if (sk->sk_type != SOCK_SEQPACKET)
		goto out;

	lock_sock(sk);
	rc = -EINVAL;
	if (sk->sk_state != TCP_LISTEN)
		goto out2;

	rc = x25_wait_for_data(sk, sk->sk_rcvtimeo);
	if (rc)
		goto out2;
	skb = skb_dequeue(&sk->sk_receive_queue);
	rc = -EINVAL;
	if (!skb->sk)
		goto out2;
	newsk		 = skb->sk;
	sock_graft(newsk, newsock);

	/* Now attach up the new socket */
	skb->sk = NULL;
	kfree_skb(skb);
	sk->sk_ack_backlog--;
	newsock->state = SS_CONNECTED;
	rc = 0;
out2:
	release_sock(sk);
out:
	return rc;
}

static int x25_getname(struct socket *sock, struct sockaddr *uaddr,
		       int *uaddr_len, int peer)
{
	struct sockaddr_x25 *sx25 = (struct sockaddr_x25 *)uaddr;
	struct sock *sk = sock->sk;
	struct x25_sock *x25 = x25_sk(sk);
	int rc = 0;

	if (peer) {
		if (sk->sk_state != TCP_ESTABLISHED) {
			rc = -ENOTCONN;
			goto out;
		}
		sx25->sx25_addr = x25->dest_addr;
	} else
		sx25->sx25_addr = x25->source_addr;

	sx25->sx25_family = AF_X25;
	*uaddr_len = sizeof(*sx25);

out:
	return rc;
}

int x25_rx_call_request(struct sk_buff *skb, struct x25_neigh *nb,
			unsigned int lci)
{
	struct sock *sk;
	struct sock *make;
	struct x25_sock *makex25;
	struct x25_address source_addr, dest_addr;
	struct x25_facilities facilities;
	struct x25_dte_facilities dte_facilities;
	int len, addr_len, rc;

	/*
	 *	Remove the LCI and frame type.
	 */
	skb_pull(skb, X25_STD_MIN_LEN);

	/*
	 *	Extract the X.25 addresses and convert them to ASCII strings,
	 *	and remove them.
	 *
	 *	Address block is mandatory in call request packets
	 */
	addr_len = x25_parse_address_block(skb, &source_addr, &dest_addr);
	if (addr_len <= 0)
		goto out_clear_request;
	skb_pull(skb, addr_len);

	/*
	 *	Get the length of the facilities, skip past them for the moment
	 *	get the call user data because this is needed to determine
	 *	the correct listener
	 *
	 *	Facilities length is mandatory in call request packets
	 */
	if (skb->len < 1)
		goto out_clear_request;
	len = skb->data[0] + 1;
	if (skb->len < len)
		goto out_clear_request;
	skb_pull(skb,len);

	/*
	 *	Find a listener for the particular address/cud pair.
	 */
	sk = x25_find_listener(&source_addr,skb);
	skb_push(skb,len);

	if (sk != NULL && sk_acceptq_is_full(sk)) {
		goto out_sock_put;
	}

	/*
	 *	We dont have any listeners for this incoming call.
	 *	Try forwarding it.
	 */
	if (sk == NULL) {
		skb_push(skb, addr_len + X25_STD_MIN_LEN);
		if (sysctl_x25_forward &&
				x25_forward_call(&dest_addr, nb, skb, lci) > 0)
		{
			/* Call was forwarded, dont process it any more */
			kfree_skb(skb);
			rc = 1;
			goto out;
		} else {
			/* No listeners, can't forward, clear the call */
			goto out_clear_request;
		}
	}

	/*
	 *	Try to reach a compromise on the requested facilities.
	 */
	len = x25_negotiate_facilities(skb, sk, &facilities, &dte_facilities);
	if (len == -1)
		goto out_sock_put;

	/*
	 * current neighbour/link might impose additional limits
	 * on certain facilties
	 */

	x25_limit_facilities(&facilities, nb);

	/*
	 *	Try to create a new socket.
	 */
	make = x25_make_new(sk);
	if (!make)
		goto out_sock_put;

	/*
	 *	Remove the facilities
	 */
	skb_pull(skb, len);

	skb->sk     = make;
	make->sk_state = TCP_ESTABLISHED;

	makex25 = x25_sk(make);
	makex25->lci           = lci;
	makex25->dest_addr     = dest_addr;
	makex25->source_addr   = source_addr;
	makex25->neighbour     = nb;
	makex25->facilities    = facilities;
	makex25->dte_facilities= dte_facilities;
	makex25->vc_facil_mask = x25_sk(sk)->vc_facil_mask;
	/* ensure no reverse facil on accept */
	makex25->vc_facil_mask &= ~X25_MASK_REVERSE;
	/* ensure no calling address extension on accept */
	makex25->vc_facil_mask &= ~X25_MASK_CALLING_AE;
	makex25->cudmatchlength = x25_sk(sk)->cudmatchlength;

	/* Normally all calls are accepted immediately */
	if (test_bit(X25_ACCPT_APPRV_FLAG, &makex25->flags)) {
		x25_write_internal(make, X25_CALL_ACCEPTED);
		makex25->state = X25_STATE_3;
	}

	/*
	 *	Incoming Call User Data.
	 */
	skb_copy_from_linear_data(skb, makex25->calluserdata.cuddata, skb->len);
	makex25->calluserdata.cudlength = skb->len;

	sk->sk_ack_backlog++;

	x25_insert_socket(make);

	skb_queue_head(&sk->sk_receive_queue, skb);

	x25_start_heartbeat(make);

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, skb->len);
	rc = 1;
	sock_put(sk);
out:
	return rc;
out_sock_put:
	sock_put(sk);
out_clear_request:
	rc = 0;
	x25_transmit_clear_request(nb, lci, 0x01);
	goto out;
}

static int x25_sendmsg(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct x25_sock *x25 = x25_sk(sk);
	struct sockaddr_x25 *usx25 = (struct sockaddr_x25 *)msg->msg_name;
	struct sockaddr_x25 sx25;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int noblock = msg->msg_flags & MSG_DONTWAIT;
	size_t size;
	int qbit = 0, rc = -EINVAL;

	lock_sock(sk);
	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_OOB|MSG_EOR|MSG_CMSG_COMPAT))
		goto out;

	/* we currently don't support segmented records at the user interface */
	if (!(msg->msg_flags & (MSG_EOR|MSG_OOB)))
		goto out;

	rc = -EADDRNOTAVAIL;
	if (sock_flag(sk, SOCK_ZAPPED))
		goto out;

	rc = -EPIPE;
	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		goto out;
	}

	rc = -ENETUNREACH;
	if (!x25->neighbour)
		goto out;

	if (usx25) {
		rc = -EINVAL;
		if (msg->msg_namelen < sizeof(sx25))
			goto out;
		memcpy(&sx25, usx25, sizeof(sx25));
		rc = -EISCONN;
		if (strcmp(x25->dest_addr.x25_addr, sx25.sx25_addr.x25_addr))
			goto out;
		rc = -EINVAL;
		if (sx25.sx25_family != AF_X25)
			goto out;
	} else {
		/*
		 *	FIXME 1003.1g - if the socket is like this because
		 *	it has become closed (not started closed) we ought
		 *	to SIGPIPE, EPIPE;
		 */
		rc = -ENOTCONN;
		if (sk->sk_state != TCP_ESTABLISHED)
			goto out;

		sx25.sx25_family = AF_X25;
		sx25.sx25_addr   = x25->dest_addr;
	}

	/* Sanity check the packet size */
	if (len > 65535) {
		rc = -EMSGSIZE;
		goto out;
	}

	SOCK_DEBUG(sk, "x25_sendmsg: sendto: Addresses built.\n");

	/* Build a packet */
	SOCK_DEBUG(sk, "x25_sendmsg: sendto: building packet.\n");

	if ((msg->msg_flags & MSG_OOB) && len > 32)
		len = 32;

	size = len + X25_MAX_L2_LEN + X25_EXT_MIN_LEN;

	release_sock(sk);
	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	lock_sock(sk);
	if (!skb)
		goto out;
	X25_SKB_CB(skb)->flags = msg->msg_flags;

	skb_reserve(skb, X25_MAX_L2_LEN + X25_EXT_MIN_LEN);

	/*
	 *	Put the data on the end
	 */
	SOCK_DEBUG(sk, "x25_sendmsg: Copying user data\n");

	skb_reset_transport_header(skb);
	skb_put(skb, len);

	rc = memcpy_fromiovec(skb_transport_header(skb), msg->msg_iov, len);
	if (rc)
		goto out_kfree_skb;

	/*
	 *	If the Q BIT Include socket option is in force, the first
	 *	byte of the user data is the logical value of the Q Bit.
	 */
	if (test_bit(X25_Q_BIT_FLAG, &x25->flags)) {
		qbit = skb->data[0];
		skb_pull(skb, 1);
	}

	/*
	 *	Push down the X.25 header
	 */
	SOCK_DEBUG(sk, "x25_sendmsg: Building X.25 Header.\n");

	if (msg->msg_flags & MSG_OOB) {
		if (x25->neighbour->extended) {
			asmptr    = skb_push(skb, X25_STD_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_EXTSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_INTERRUPT;
		} else {
			asmptr    = skb_push(skb, X25_STD_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_STDSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_INTERRUPT;
		}
	} else {
		if (x25->neighbour->extended) {
			/* Build an Extended X.25 header */
			asmptr    = skb_push(skb, X25_EXT_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_EXTSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_DATA;
			*asmptr++ = X25_DATA;
		} else {
			/* Build an Standard X.25 header */
			asmptr    = skb_push(skb, X25_STD_MIN_LEN);
			*asmptr++ = ((x25->lci >> 8) & 0x0F) | X25_GFI_STDSEQ;
			*asmptr++ = (x25->lci >> 0) & 0xFF;
			*asmptr++ = X25_DATA;
		}

		if (qbit)
			skb->data[0] |= X25_Q_BIT;
	}

	SOCK_DEBUG(sk, "x25_sendmsg: Built header.\n");
	SOCK_DEBUG(sk, "x25_sendmsg: Transmitting buffer\n");

	rc = -ENOTCONN;
	if (sk->sk_state != TCP_ESTABLISHED)
		goto out_kfree_skb;

	if (msg->msg_flags & MSG_OOB)
		skb_queue_tail(&x25->interrupt_out_queue, skb);
	else {
		rc = x25_output(sk, skb);
		len = rc;
		if (rc < 0)
			kfree_skb(skb);
		else if (test_bit(X25_Q_BIT_FLAG, &x25->flags))
			len++;
	}

	x25_kick(sk);
	rc = len;
out:
	release_sock(sk);
	return rc;
out_kfree_skb:
	kfree_skb(skb);
	goto out;
}


static int x25_recvmsg(struct kiocb *iocb, struct socket *sock,
		       struct msghdr *msg, size_t size,
		       int flags)
{
	struct sock *sk = sock->sk;
	struct x25_sock *x25 = x25_sk(sk);
	struct sockaddr_x25 *sx25 = (struct sockaddr_x25 *)msg->msg_name;
	size_t copied;
	int qbit;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int rc = -ENOTCONN;

	lock_sock(sk);
	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */
	if (sk->sk_state != TCP_ESTABLISHED)
		goto out;

	if (flags & MSG_OOB) {
		rc = -EINVAL;
		if (sock_flag(sk, SOCK_URGINLINE) ||
		    !skb_peek(&x25->interrupt_in_queue))
			goto out;

		skb = skb_dequeue(&x25->interrupt_in_queue);

		skb_pull(skb, X25_STD_MIN_LEN);

		/*
		 *	No Q bit information on Interrupt data.
		 */
		if (test_bit(X25_Q_BIT_FLAG, &x25->flags)) {
			asmptr  = skb_push(skb, 1);
			*asmptr = 0x00;
		}

		msg->msg_flags |= MSG_OOB;
	} else {
		/* Now we can treat all alike */
		release_sock(sk);
		skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
					flags & MSG_DONTWAIT, &rc);
		lock_sock(sk);
		if (!skb)
			goto out;

		qbit = (skb->data[0] & X25_Q_BIT) == X25_Q_BIT;

		skb_pull(skb, x25->neighbour->extended ?
				X25_EXT_MIN_LEN : X25_STD_MIN_LEN);

		if (test_bit(X25_Q_BIT_FLAG, &x25->flags)) {
			asmptr  = skb_push(skb, 1);
			*asmptr = qbit;
		}
	}

	skb_reset_transport_header(skb);
	copied = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	/* Currently, each datagram always contains a complete record */
	msg->msg_flags |= MSG_EOR;

	rc = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (rc)
		goto out_free_dgram;

	if (sx25) {
		sx25->sx25_family = AF_X25;
		sx25->sx25_addr   = x25->dest_addr;
	}

	msg->msg_namelen = sizeof(struct sockaddr_x25);

	x25_check_rbuf(sk);
	rc = copied;
out_free_dgram:
	skb_free_datagram(sk, skb);
out:
	release_sock(sk);
	return rc;
}


static int x25_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct x25_sock *x25 = x25_sk(sk);
	void __user *argp = (void __user *)arg;
	int rc;

	switch (cmd) {
		case TIOCOUTQ: {
			int amount;

			amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
			if (amount < 0)
				amount = 0;
			rc = put_user(amount, (unsigned int __user *)argp);
			break;
		}

		case TIOCINQ: {
			struct sk_buff *skb;
			int amount = 0;
			/*
			 * These two are safe on a single CPU system as
			 * only user tasks fiddle here
			 */
			lock_sock(sk);
			if ((skb = skb_peek(&sk->sk_receive_queue)) != NULL)
				amount = skb->len;
			release_sock(sk);
			rc = put_user(amount, (unsigned int __user *)argp);
			break;
		}

		case SIOCGSTAMP:
			rc = -EINVAL;
			if (sk)
				rc = sock_get_timestamp(sk,
						(struct timeval __user *)argp);
			break;
		case SIOCGSTAMPNS:
			rc = -EINVAL;
			if (sk)
				rc = sock_get_timestampns(sk,
						(struct timespec __user *)argp);
			break;
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCSIFMETRIC:
			rc = -EINVAL;
			break;
		case SIOCADDRT:
		case SIOCDELRT:
			rc = -EPERM;
			if (!capable(CAP_NET_ADMIN))
				break;
			rc = x25_route_ioctl(cmd, argp);
			break;
		case SIOCX25GSUBSCRIP:
			rc = x25_subscr_ioctl(cmd, argp);
			break;
		case SIOCX25SSUBSCRIP:
			rc = -EPERM;
			if (!capable(CAP_NET_ADMIN))
				break;
			rc = x25_subscr_ioctl(cmd, argp);
			break;
		case SIOCX25GFACILITIES: {
			lock_sock(sk);
			rc = copy_to_user(argp, &x25->facilities,
						sizeof(x25->facilities))
						? -EFAULT : 0;
			release_sock(sk);
			break;
		}

		case SIOCX25SFACILITIES: {
			struct x25_facilities facilities;
			rc = -EFAULT;
			if (copy_from_user(&facilities, argp,
					   sizeof(facilities)))
				break;
			rc = -EINVAL;
			lock_sock(sk);
			if (sk->sk_state != TCP_LISTEN &&
			    sk->sk_state != TCP_CLOSE)
				goto out_fac_release;
			if (facilities.pacsize_in < X25_PS16 ||
			    facilities.pacsize_in > X25_PS4096)
				goto out_fac_release;
			if (facilities.pacsize_out < X25_PS16 ||
			    facilities.pacsize_out > X25_PS4096)
				goto out_fac_release;
			if (facilities.winsize_in < 1 ||
			    facilities.winsize_in > 127)
				goto out_fac_release;
			if (facilities.throughput) {
				int out = facilities.throughput & 0xf0;
				int in  = facilities.throughput & 0x0f;
				if (!out)
					facilities.throughput |=
						X25_DEFAULT_THROUGHPUT << 4;
				else if (out < 0x30 || out > 0xD0)
					goto out_fac_release;
				if (!in)
					facilities.throughput |=
						X25_DEFAULT_THROUGHPUT;
				else if (in < 0x03 || in > 0x0D)
					goto out_fac_release;
			}
			if (facilities.reverse &&
				(facilities.reverse & 0x81) != 0x81)
				goto out_fac_release;
			x25->facilities = facilities;
			rc = 0;
out_fac_release:
			release_sock(sk);
			break;
		}

		case SIOCX25GDTEFACILITIES: {
			lock_sock(sk);
			rc = copy_to_user(argp, &x25->dte_facilities,
						sizeof(x25->dte_facilities));
			release_sock(sk);
			if (rc)
				rc = -EFAULT;
			break;
		}

		case SIOCX25SDTEFACILITIES: {
			struct x25_dte_facilities dtefacs;
			rc = -EFAULT;
			if (copy_from_user(&dtefacs, argp, sizeof(dtefacs)))
				break;
			rc = -EINVAL;
			lock_sock(sk);
			if (sk->sk_state != TCP_LISTEN &&
					sk->sk_state != TCP_CLOSE)
				goto out_dtefac_release;
			if (dtefacs.calling_len > X25_MAX_AE_LEN)
				goto out_dtefac_release;
			if (dtefacs.calling_ae == NULL)
				goto out_dtefac_release;
			if (dtefacs.called_len > X25_MAX_AE_LEN)
				goto out_dtefac_release;
			if (dtefacs.called_ae == NULL)
				goto out_dtefac_release;
			x25->dte_facilities = dtefacs;
			rc = 0;
out_dtefac_release:
			release_sock(sk);
			break;
		}

		case SIOCX25GCALLUSERDATA: {
			lock_sock(sk);
			rc = copy_to_user(argp, &x25->calluserdata,
					sizeof(x25->calluserdata))
					? -EFAULT : 0;
			release_sock(sk);
			break;
		}

		case SIOCX25SCALLUSERDATA: {
			struct x25_calluserdata calluserdata;

			rc = -EFAULT;
			if (copy_from_user(&calluserdata, argp,
					   sizeof(calluserdata)))
				break;
			rc = -EINVAL;
			if (calluserdata.cudlength > X25_MAX_CUD_LEN)
				break;
			lock_sock(sk);
			x25->calluserdata = calluserdata;
			release_sock(sk);
			rc = 0;
			break;
		}

		case SIOCX25GCAUSEDIAG: {
			lock_sock(sk);
			rc = copy_to_user(argp, &x25->causediag,
					sizeof(x25->causediag))
					? -EFAULT : 0;
			release_sock(sk);
			break;
		}

		case SIOCX25SCAUSEDIAG: {
			struct x25_causediag causediag;
			rc = -EFAULT;
			if (copy_from_user(&causediag, argp, sizeof(causediag)))
				break;
			lock_sock(sk);
			x25->causediag = causediag;
			release_sock(sk);
			rc = 0;
			break;

		}

		case SIOCX25SCUDMATCHLEN: {
			struct x25_subaddr sub_addr;
			rc = -EINVAL;
			lock_sock(sk);
			if(sk->sk_state != TCP_CLOSE)
				goto out_cud_release;
			rc = -EFAULT;
			if (copy_from_user(&sub_addr, argp,
					sizeof(sub_addr)))
				goto out_cud_release;
			rc = -EINVAL;
			if(sub_addr.cudmatchlength > X25_MAX_CUD_LEN)
				goto out_cud_release;
			x25->cudmatchlength = sub_addr.cudmatchlength;
			rc = 0;
out_cud_release:
			release_sock(sk);
			break;
		}

		case SIOCX25CALLACCPTAPPRV: {
			rc = -EINVAL;
			lock_sock(sk);
			if (sk->sk_state != TCP_CLOSE)
				break;
			clear_bit(X25_ACCPT_APPRV_FLAG, &x25->flags);
			release_sock(sk);
			rc = 0;
			break;
		}

		case SIOCX25SENDCALLACCPT:  {
			rc = -EINVAL;
			lock_sock(sk);
			if (sk->sk_state != TCP_ESTABLISHED)
				break;
			/* must call accptapprv above */
			if (test_bit(X25_ACCPT_APPRV_FLAG, &x25->flags))
				break;
			x25_write_internal(sk, X25_CALL_ACCEPTED);
			x25->state = X25_STATE_3;
			release_sock(sk);
			rc = 0;
			break;
		}

		default:
			rc = -ENOIOCTLCMD;
			break;
	}

	return rc;
}

static const struct net_proto_family x25_family_ops = {
	.family =	AF_X25,
	.create =	x25_create,
	.owner	=	THIS_MODULE,
};

#ifdef CONFIG_COMPAT
static int compat_x25_subscr_ioctl(unsigned int cmd,
		struct compat_x25_subscrip_struct __user *x25_subscr32)
{
	struct compat_x25_subscrip_struct x25_subscr;
	struct x25_neigh *nb;
	struct net_device *dev;
	int rc = -EINVAL;

	rc = -EFAULT;
	if (copy_from_user(&x25_subscr, x25_subscr32, sizeof(*x25_subscr32)))
		goto out;

	rc = -EINVAL;
	dev = x25_dev_get(x25_subscr.device);
	if (dev == NULL)
		goto out;

	nb = x25_get_neigh(dev);
	if (nb == NULL)
		goto out_dev_put;

	dev_put(dev);

	if (cmd == SIOCX25GSUBSCRIP) {
		read_lock_bh(&x25_neigh_list_lock);
		x25_subscr.extended = nb->extended;
		x25_subscr.global_facil_mask = nb->global_facil_mask;
		read_unlock_bh(&x25_neigh_list_lock);
		rc = copy_to_user(x25_subscr32, &x25_subscr,
				sizeof(*x25_subscr32)) ? -EFAULT : 0;
	} else {
		rc = -EINVAL;
		if (x25_subscr.extended == 0 || x25_subscr.extended == 1) {
			rc = 0;
			write_lock_bh(&x25_neigh_list_lock);
			nb->extended = x25_subscr.extended;
			nb->global_facil_mask = x25_subscr.global_facil_mask;
			write_unlock_bh(&x25_neigh_list_lock);
		}
	}
	x25_neigh_put(nb);
out:
	return rc;
out_dev_put:
	dev_put(dev);
	goto out;
}

static int compat_x25_ioctl(struct socket *sock, unsigned int cmd,
				unsigned long arg)
{
	void __user *argp = compat_ptr(arg);
	struct sock *sk = sock->sk;

	int rc = -ENOIOCTLCMD;

	switch(cmd) {
	case TIOCOUTQ:
	case TIOCINQ:
		rc = x25_ioctl(sock, cmd, (unsigned long)argp);
		break;
	case SIOCGSTAMP:
		rc = -EINVAL;
		if (sk)
			rc = compat_sock_get_timestamp(sk,
					(struct timeval __user*)argp);
		break;
	case SIOCGSTAMPNS:
		rc = -EINVAL;
		if (sk)
			rc = compat_sock_get_timestampns(sk,
					(struct timespec __user*)argp);
		break;
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
		rc = -EINVAL;
		break;
	case SIOCADDRT:
	case SIOCDELRT:
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		rc = x25_route_ioctl(cmd, argp);
		break;
	case SIOCX25GSUBSCRIP:
		rc = compat_x25_subscr_ioctl(cmd, argp);
		break;
	case SIOCX25SSUBSCRIP:
		rc = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		rc = compat_x25_subscr_ioctl(cmd, argp);
		break;
	case SIOCX25GFACILITIES:
	case SIOCX25SFACILITIES:
	case SIOCX25GDTEFACILITIES:
	case SIOCX25SDTEFACILITIES:
	case SIOCX25GCALLUSERDATA:
	case SIOCX25SCALLUSERDATA:
	case SIOCX25GCAUSEDIAG:
	case SIOCX25SCAUSEDIAG:
	case SIOCX25SCUDMATCHLEN:
	case SIOCX25CALLACCPTAPPRV:
	case SIOCX25SENDCALLACCPT:
		rc = x25_ioctl(sock, cmd, (unsigned long)argp);
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}
	return rc;
}
#endif

static const struct proto_ops x25_proto_ops = {
	.family =	AF_X25,
	.owner =	THIS_MODULE,
	.release =	x25_release,
	.bind =		x25_bind,
	.connect =	x25_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	x25_accept,
	.getname =	x25_getname,
	.poll =		datagram_poll,
	.ioctl =	x25_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_x25_ioctl,
#endif
	.listen =	x25_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	x25_setsockopt,
	.getsockopt =	x25_getsockopt,
	.sendmsg =	x25_sendmsg,
	.recvmsg =	x25_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

static struct packet_type x25_packet_type __read_mostly = {
	.type =	cpu_to_be16(ETH_P_X25),
	.func =	x25_lapb_receive_frame,
};

static struct notifier_block x25_dev_notifier = {
	.notifier_call = x25_device_event,
};

void x25_kill_by_neigh(struct x25_neigh *nb)
{
	struct sock *s;
	struct hlist_node *node;

	write_lock_bh(&x25_list_lock);

	sk_for_each(s, node, &x25_list)
		if (x25_sk(s)->neighbour == nb)
			x25_disconnect(s, ENETUNREACH, 0, 0);

	write_unlock_bh(&x25_list_lock);

	/* Remove any related forwards */
	x25_clear_forward_by_dev(nb->dev);
}

static int __init x25_init(void)
{
	int rc = proto_register(&x25_proto, 0);

	if (rc != 0)
		goto out;

	rc = sock_register(&x25_family_ops);
	if (rc != 0)
		goto out_proto;

	dev_add_pack(&x25_packet_type);

	rc = register_netdevice_notifier(&x25_dev_notifier);
	if (rc != 0)
		goto out_sock;

	printk(KERN_INFO "X.25 for Linux Version 0.2\n");

	x25_register_sysctl();
	rc = x25_proc_init();
	if (rc != 0)
		goto out_dev;
out:
	return rc;
out_dev:
	unregister_netdevice_notifier(&x25_dev_notifier);
out_sock:
	sock_unregister(AF_X25);
out_proto:
	proto_unregister(&x25_proto);
	goto out;
}
module_init(x25_init);

static void __exit x25_exit(void)
{
	x25_proc_exit();
	x25_link_free();
	x25_route_free();

	x25_unregister_sysctl();

	unregister_netdevice_notifier(&x25_dev_notifier);

	dev_remove_pack(&x25_packet_type);

	sock_unregister(AF_X25);
	proto_unregister(&x25_proto);
}
module_exit(x25_exit);

MODULE_AUTHOR("Jonathan Naylor <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The X.25 Packet Layer network layer protocol");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_X25);
