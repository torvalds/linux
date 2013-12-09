/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Terry Dawson VK2KTJ (terry@animats.net)
 * Copyright (C) Tomi Manninen OH2BNS (oh2bns@sral.fi)
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/stat.h>
#include <net/net_namespace.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/rose.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/tcp_states.h>
#include <net/ip.h>
#include <net/arp.h>

static int rose_ndevs = 10;

int sysctl_rose_restart_request_timeout = ROSE_DEFAULT_T0;
int sysctl_rose_call_request_timeout    = ROSE_DEFAULT_T1;
int sysctl_rose_reset_request_timeout   = ROSE_DEFAULT_T2;
int sysctl_rose_clear_request_timeout   = ROSE_DEFAULT_T3;
int sysctl_rose_no_activity_timeout     = ROSE_DEFAULT_IDLE;
int sysctl_rose_ack_hold_back_timeout   = ROSE_DEFAULT_HB;
int sysctl_rose_routing_control         = ROSE_DEFAULT_ROUTING;
int sysctl_rose_link_fail_timeout       = ROSE_DEFAULT_FAIL_TIMEOUT;
int sysctl_rose_maximum_vcs             = ROSE_DEFAULT_MAXVC;
int sysctl_rose_window_size             = ROSE_DEFAULT_WINDOW_SIZE;

static HLIST_HEAD(rose_list);
static DEFINE_SPINLOCK(rose_list_lock);

static const struct proto_ops rose_proto_ops;

ax25_address rose_callsign;

/*
 * ROSE network devices are virtual network devices encapsulating ROSE
 * frames into AX.25 which will be sent through an AX.25 device, so form a
 * special "super class" of normal net devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key rose_netdev_xmit_lock_key;
static struct lock_class_key rose_netdev_addr_lock_key;

static void rose_set_lockdep_one(struct net_device *dev,
				 struct netdev_queue *txq,
				 void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock, &rose_netdev_xmit_lock_key);
}

static void rose_set_lockdep_key(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock, &rose_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, rose_set_lockdep_one, NULL);
}

/*
 *	Convert a ROSE address into text.
 */
char *rose2asc(char *buf, const rose_address *addr)
{
	if (addr->rose_addr[0] == 0x00 && addr->rose_addr[1] == 0x00 &&
	    addr->rose_addr[2] == 0x00 && addr->rose_addr[3] == 0x00 &&
	    addr->rose_addr[4] == 0x00) {
		strcpy(buf, "*");
	} else {
		sprintf(buf, "%02X%02X%02X%02X%02X", addr->rose_addr[0] & 0xFF,
						addr->rose_addr[1] & 0xFF,
						addr->rose_addr[2] & 0xFF,
						addr->rose_addr[3] & 0xFF,
						addr->rose_addr[4] & 0xFF);
	}

	return buf;
}

/*
 *	Compare two ROSE addresses, 0 == equal.
 */
int rosecmp(rose_address *addr1, rose_address *addr2)
{
	int i;

	for (i = 0; i < 5; i++)
		if (addr1->rose_addr[i] != addr2->rose_addr[i])
			return 1;

	return 0;
}

/*
 *	Compare two ROSE addresses for only mask digits, 0 == equal.
 */
int rosecmpm(rose_address *addr1, rose_address *addr2, unsigned short mask)
{
	unsigned int i, j;

	if (mask > 10)
		return 1;

	for (i = 0; i < mask; i++) {
		j = i / 2;

		if ((i % 2) != 0) {
			if ((addr1->rose_addr[j] & 0x0F) != (addr2->rose_addr[j] & 0x0F))
				return 1;
		} else {
			if ((addr1->rose_addr[j] & 0xF0) != (addr2->rose_addr[j] & 0xF0))
				return 1;
		}
	}

	return 0;
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void rose_remove_socket(struct sock *sk)
{
	spin_lock_bh(&rose_list_lock);
	sk_del_node_init(sk);
	spin_unlock_bh(&rose_list_lock);
}

/*
 *	Kill all bound sockets on a broken link layer connection to a
 *	particular neighbour.
 */
void rose_kill_by_neigh(struct rose_neigh *neigh)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&rose_list_lock);
	sk_for_each(s, node, &rose_list) {
		struct rose_sock *rose = rose_sk(s);

		if (rose->neighbour == neigh) {
			rose_disconnect(s, ENETUNREACH, ROSE_OUT_OF_ORDER, 0);
			rose->neighbour->use--;
			rose->neighbour = NULL;
		}
	}
	spin_unlock_bh(&rose_list_lock);
}

/*
 *	Kill all bound sockets on a dropped device.
 */
static void rose_kill_by_device(struct net_device *dev)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&rose_list_lock);
	sk_for_each(s, node, &rose_list) {
		struct rose_sock *rose = rose_sk(s);

		if (rose->device == dev) {
			rose_disconnect(s, ENETUNREACH, ROSE_OUT_OF_ORDER, 0);
			rose->neighbour->use--;
			rose->device = NULL;
		}
	}
	spin_unlock_bh(&rose_list_lock);
}

/*
 *	Handle device status changes.
 */
static int rose_device_event(struct notifier_block *this, unsigned long event,
	void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;

	switch (dev->type) {
	case ARPHRD_ROSE:
		rose_kill_by_device(dev);
		break;
	case ARPHRD_AX25:
		rose_link_device_down(dev);
		rose_rt_device_down(dev);
		break;
	}

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void rose_insert_socket(struct sock *sk)
{

	spin_lock_bh(&rose_list_lock);
	sk_add_node(sk, &rose_list);
	spin_unlock_bh(&rose_list_lock);
}

/*
 *	Find a socket that wants to accept the Call Request we just
 *	received.
 */
static struct sock *rose_find_listener(rose_address *addr, ax25_address *call)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&rose_list_lock);
	sk_for_each(s, node, &rose_list) {
		struct rose_sock *rose = rose_sk(s);

		if (!rosecmp(&rose->source_addr, addr) &&
		    !ax25cmp(&rose->source_call, call) &&
		    !rose->source_ndigis && s->sk_state == TCP_LISTEN)
			goto found;
	}

	sk_for_each(s, node, &rose_list) {
		struct rose_sock *rose = rose_sk(s);

		if (!rosecmp(&rose->source_addr, addr) &&
		    !ax25cmp(&rose->source_call, &null_ax25_address) &&
		    s->sk_state == TCP_LISTEN)
			goto found;
	}
	s = NULL;
found:
	spin_unlock_bh(&rose_list_lock);
	return s;
}

/*
 *	Find a connected ROSE socket given my LCI and device.
 */
struct sock *rose_find_socket(unsigned int lci, struct rose_neigh *neigh)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&rose_list_lock);
	sk_for_each(s, node, &rose_list) {
		struct rose_sock *rose = rose_sk(s);

		if (rose->lci == lci && rose->neighbour == neigh)
			goto found;
	}
	s = NULL;
found:
	spin_unlock_bh(&rose_list_lock);
	return s;
}

/*
 *	Find a unique LCI for a given device.
 */
unsigned int rose_new_lci(struct rose_neigh *neigh)
{
	int lci;

	if (neigh->dce_mode) {
		for (lci = 1; lci <= sysctl_rose_maximum_vcs; lci++)
			if (rose_find_socket(lci, neigh) == NULL && rose_route_free_lci(lci, neigh) == NULL)
				return lci;
	} else {
		for (lci = sysctl_rose_maximum_vcs; lci > 0; lci--)
			if (rose_find_socket(lci, neigh) == NULL && rose_route_free_lci(lci, neigh) == NULL)
				return lci;
	}

	return 0;
}

/*
 *	Deferred destroy.
 */
void rose_destroy_socket(struct sock *);

/*
 *	Handler for deferred kills.
 */
static void rose_destroy_timer(unsigned long data)
{
	rose_destroy_socket((struct sock *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself
 *	against interrupt users but doesn't worry about being called during
 *	work.  Once it is removed from the queue no interrupt or bottom half
 *	will touch it and we are (fairly 8-) ) safe.
 */
void rose_destroy_socket(struct sock *sk)
{
	struct sk_buff *skb;

	rose_remove_socket(sk);
	rose_stop_heartbeat(sk);
	rose_stop_idletimer(sk);
	rose_stop_timer(sk);

	rose_clear_queues(sk);		/* Flush the queues */

	while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		if (skb->sk != sk) {	/* A pending connection */
			/* Queue the unaccepted socket for death */
			sock_set_flag(skb->sk, SOCK_DEAD);
			rose_start_heartbeat(skb->sk);
			rose_sk(skb->sk)->state = ROSE_STATE_0;
		}

		kfree_skb(skb);
	}

	if (sk_has_allocations(sk)) {
		/* Defer: outstanding buffers */
		setup_timer(&sk->sk_timer, rose_destroy_timer,
				(unsigned long)sk);
		sk->sk_timer.expires  = jiffies + 10 * HZ;
		add_timer(&sk->sk_timer);
	} else
		sock_put(sk);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	ROSE socket object.
 */

static int rose_setsockopt(struct socket *sock, int level, int optname,
	char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	int opt;

	if (level != SOL_ROSE)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(opt, (int __user *)optval))
		return -EFAULT;

	switch (optname) {
	case ROSE_DEFER:
		rose->defer = opt ? 1 : 0;
		return 0;

	case ROSE_T1:
		if (opt < 1)
			return -EINVAL;
		rose->t1 = opt * HZ;
		return 0;

	case ROSE_T2:
		if (opt < 1)
			return -EINVAL;
		rose->t2 = opt * HZ;
		return 0;

	case ROSE_T3:
		if (opt < 1)
			return -EINVAL;
		rose->t3 = opt * HZ;
		return 0;

	case ROSE_HOLDBACK:
		if (opt < 1)
			return -EINVAL;
		rose->hb = opt * HZ;
		return 0;

	case ROSE_IDLE:
		if (opt < 0)
			return -EINVAL;
		rose->idle = opt * 60 * HZ;
		return 0;

	case ROSE_QBITINCL:
		rose->qbitincl = opt ? 1 : 0;
		return 0;

	default:
		return -ENOPROTOOPT;
	}
}

static int rose_getsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	int val = 0;
	int len;

	if (level != SOL_ROSE)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case ROSE_DEFER:
		val = rose->defer;
		break;

	case ROSE_T1:
		val = rose->t1 / HZ;
		break;

	case ROSE_T2:
		val = rose->t2 / HZ;
		break;

	case ROSE_T3:
		val = rose->t3 / HZ;
		break;

	case ROSE_HOLDBACK:
		val = rose->hb / HZ;
		break;

	case ROSE_IDLE:
		val = rose->idle / (60 * HZ);
		break;

	case ROSE_QBITINCL:
		val = rose->qbitincl;
		break;

	default:
		return -ENOPROTOOPT;
	}

	len = min_t(unsigned int, len, sizeof(int));

	if (put_user(len, optlen))
		return -EFAULT;

	return copy_to_user(optval, &val, len) ? -EFAULT : 0;
}

static int rose_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	if (sk->sk_state != TCP_LISTEN) {
		struct rose_sock *rose = rose_sk(sk);

		rose->dest_ndigis = 0;
		memset(&rose->dest_addr, 0, ROSE_ADDR_LEN);
		memset(&rose->dest_call, 0, AX25_ADDR_LEN);
		memset(rose->dest_digis, 0, AX25_ADDR_LEN * ROSE_MAX_DIGIS);
		sk->sk_max_ack_backlog = backlog;
		sk->sk_state           = TCP_LISTEN;
		return 0;
	}

	return -EOPNOTSUPP;
}

static struct proto rose_proto = {
	.name	  = "ROSE",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct rose_sock),
};

static int rose_create(struct net *net, struct socket *sock, int protocol,
		       int kern)
{
	struct sock *sk;
	struct rose_sock *rose;

	if (!net_eq(net, &init_net))
		return -EAFNOSUPPORT;

	if (sock->type != SOCK_SEQPACKET || protocol != 0)
		return -ESOCKTNOSUPPORT;

	sk = sk_alloc(net, PF_ROSE, GFP_ATOMIC, &rose_proto);
	if (sk == NULL)
		return -ENOMEM;

	rose = rose_sk(sk);

	sock_init_data(sock, sk);

	skb_queue_head_init(&rose->ack_queue);
#ifdef M_BIT
	skb_queue_head_init(&rose->frag_queue);
	rose->fraglen    = 0;
#endif

	sock->ops    = &rose_proto_ops;
	sk->sk_protocol = protocol;

	init_timer(&rose->timer);
	init_timer(&rose->idletimer);

	rose->t1   = msecs_to_jiffies(sysctl_rose_call_request_timeout);
	rose->t2   = msecs_to_jiffies(sysctl_rose_reset_request_timeout);
	rose->t3   = msecs_to_jiffies(sysctl_rose_clear_request_timeout);
	rose->hb   = msecs_to_jiffies(sysctl_rose_ack_hold_back_timeout);
	rose->idle = msecs_to_jiffies(sysctl_rose_no_activity_timeout);

	rose->state = ROSE_STATE_0;

	return 0;
}

static struct sock *rose_make_new(struct sock *osk)
{
	struct sock *sk;
	struct rose_sock *rose, *orose;

	if (osk->sk_type != SOCK_SEQPACKET)
		return NULL;

	sk = sk_alloc(sock_net(osk), PF_ROSE, GFP_ATOMIC, &rose_proto);
	if (sk == NULL)
		return NULL;

	rose = rose_sk(sk);

	sock_init_data(NULL, sk);

	skb_queue_head_init(&rose->ack_queue);
#ifdef M_BIT
	skb_queue_head_init(&rose->frag_queue);
	rose->fraglen  = 0;
#endif

	sk->sk_type     = osk->sk_type;
	sk->sk_priority = osk->sk_priority;
	sk->sk_protocol = osk->sk_protocol;
	sk->sk_rcvbuf   = osk->sk_rcvbuf;
	sk->sk_sndbuf   = osk->sk_sndbuf;
	sk->sk_state    = TCP_ESTABLISHED;
	sock_copy_flags(sk, osk);

	init_timer(&rose->timer);
	init_timer(&rose->idletimer);

	orose		= rose_sk(osk);
	rose->t1	= orose->t1;
	rose->t2	= orose->t2;
	rose->t3	= orose->t3;
	rose->hb	= orose->hb;
	rose->idle	= orose->idle;
	rose->defer	= orose->defer;
	rose->device	= orose->device;
	rose->qbitincl	= orose->qbitincl;

	return sk;
}

static int rose_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose;

	if (sk == NULL) return 0;

	sock_hold(sk);
	sock_orphan(sk);
	lock_sock(sk);
	rose = rose_sk(sk);

	switch (rose->state) {
	case ROSE_STATE_0:
		release_sock(sk);
		rose_disconnect(sk, 0, -1, -1);
		lock_sock(sk);
		rose_destroy_socket(sk);
		break;

	case ROSE_STATE_2:
		rose->neighbour->use--;
		release_sock(sk);
		rose_disconnect(sk, 0, -1, -1);
		lock_sock(sk);
		rose_destroy_socket(sk);
		break;

	case ROSE_STATE_1:
	case ROSE_STATE_3:
	case ROSE_STATE_4:
	case ROSE_STATE_5:
		rose_clear_queues(sk);
		rose_stop_idletimer(sk);
		rose_write_internal(sk, ROSE_CLEAR_REQUEST);
		rose_start_t3timer(sk);
		rose->state  = ROSE_STATE_2;
		sk->sk_state    = TCP_CLOSE;
		sk->sk_shutdown |= SEND_SHUTDOWN;
		sk->sk_state_change(sk);
		sock_set_flag(sk, SOCK_DEAD);
		sock_set_flag(sk, SOCK_DESTROY);
		break;

	default:
		break;
	}

	sock->sk = NULL;
	release_sock(sk);
	sock_put(sk);

	return 0;
}

static int rose_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	struct sockaddr_rose *addr = (struct sockaddr_rose *)uaddr;
	struct net_device *dev;
	ax25_address *source;
	ax25_uid_assoc *user;
	int n;

	if (!sock_flag(sk, SOCK_ZAPPED))
		return -EINVAL;

	if (addr_len != sizeof(struct sockaddr_rose) && addr_len != sizeof(struct full_sockaddr_rose))
		return -EINVAL;

	if (addr->srose_family != AF_ROSE)
		return -EINVAL;

	if (addr_len == sizeof(struct sockaddr_rose) && addr->srose_ndigis > 1)
		return -EINVAL;

	if ((unsigned int) addr->srose_ndigis > ROSE_MAX_DIGIS)
		return -EINVAL;

	if ((dev = rose_dev_get(&addr->srose_addr)) == NULL)
		return -EADDRNOTAVAIL;

	source = &addr->srose_call;

	user = ax25_findbyuid(current_euid());
	if (user) {
		rose->source_call = user->call;
		ax25_uid_put(user);
	} else {
		if (ax25_uid_policy && !capable(CAP_NET_BIND_SERVICE))
			return -EACCES;
		rose->source_call   = *source;
	}

	rose->source_addr   = addr->srose_addr;
	rose->device        = dev;
	rose->source_ndigis = addr->srose_ndigis;

	if (addr_len == sizeof(struct full_sockaddr_rose)) {
		struct full_sockaddr_rose *full_addr = (struct full_sockaddr_rose *)uaddr;
		for (n = 0 ; n < addr->srose_ndigis ; n++)
			rose->source_digis[n] = full_addr->srose_digis[n];
	} else {
		if (rose->source_ndigis == 1) {
			rose->source_digis[0] = addr->srose_digi;
		}
	}

	rose_insert_socket(sk);

	sock_reset_flag(sk, SOCK_ZAPPED);

	return 0;
}

static int rose_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	struct sockaddr_rose *addr = (struct sockaddr_rose *)uaddr;
	unsigned char cause, diagnostic;
	struct net_device *dev;
	ax25_uid_assoc *user;
	int n, err = 0;

	if (addr_len != sizeof(struct sockaddr_rose) && addr_len != sizeof(struct full_sockaddr_rose))
		return -EINVAL;

	if (addr->srose_family != AF_ROSE)
		return -EINVAL;

	if (addr_len == sizeof(struct sockaddr_rose) && addr->srose_ndigis > 1)
		return -EINVAL;

	if ((unsigned int) addr->srose_ndigis > ROSE_MAX_DIGIS)
		return -EINVAL;

	/* Source + Destination digis should not exceed ROSE_MAX_DIGIS */
	if ((rose->source_ndigis + addr->srose_ndigis) > ROSE_MAX_DIGIS)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		/* Connect completed during a ERESTARTSYS event */
		sock->state = SS_CONNECTED;
		goto out_release;
	}

	if (sk->sk_state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		err = -ECONNREFUSED;
		goto out_release;
	}

	if (sk->sk_state == TCP_ESTABLISHED) {
		/* No reconnect on a seqpacket socket */
		err = -EISCONN;
		goto out_release;
	}

	sk->sk_state   = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	rose->neighbour = rose_get_neigh(&addr->srose_addr, &cause,
					 &diagnostic, 0);
	if (!rose->neighbour) {
		err = -ENETUNREACH;
		goto out_release;
	}

	rose->lci = rose_new_lci(rose->neighbour);
	if (!rose->lci) {
		err = -ENETUNREACH;
		goto out_release;
	}

	if (sock_flag(sk, SOCK_ZAPPED)) {	/* Must bind first - autobinding in this may or may not work */
		sock_reset_flag(sk, SOCK_ZAPPED);

		if ((dev = rose_dev_first()) == NULL) {
			err = -ENETUNREACH;
			goto out_release;
		}

		user = ax25_findbyuid(current_euid());
		if (!user) {
			err = -EINVAL;
			goto out_release;
		}

		memcpy(&rose->source_addr, dev->dev_addr, ROSE_ADDR_LEN);
		rose->source_call = user->call;
		rose->device      = dev;
		ax25_uid_put(user);

		rose_insert_socket(sk);		/* Finish the bind */
	}
	rose->dest_addr   = addr->srose_addr;
	rose->dest_call   = addr->srose_call;
	rose->rand        = ((long)rose & 0xFFFF) + rose->lci;
	rose->dest_ndigis = addr->srose_ndigis;

	if (addr_len == sizeof(struct full_sockaddr_rose)) {
		struct full_sockaddr_rose *full_addr = (struct full_sockaddr_rose *)uaddr;
		for (n = 0 ; n < addr->srose_ndigis ; n++)
			rose->dest_digis[n] = full_addr->srose_digis[n];
	} else {
		if (rose->dest_ndigis == 1) {
			rose->dest_digis[0] = addr->srose_digi;
		}
	}

	/* Move to connecting socket, start sending Connect Requests */
	sock->state   = SS_CONNECTING;
	sk->sk_state     = TCP_SYN_SENT;

	rose->state = ROSE_STATE_1;

	rose->neighbour->use++;

	rose_write_internal(sk, ROSE_CALL_REQUEST);
	rose_start_heartbeat(sk);
	rose_start_t1timer(sk);

	/* Now the loop */
	if (sk->sk_state != TCP_ESTABLISHED && (flags & O_NONBLOCK)) {
		err = -EINPROGRESS;
		goto out_release;
	}

	/*
	 * A Connect Ack with Choke or timeout or failed routing will go to
	 * closed.
	 */
	if (sk->sk_state == TCP_SYN_SENT) {
		DEFINE_WAIT(wait);

		for (;;) {
			prepare_to_wait(sk_sleep(sk), &wait,
					TASK_INTERRUPTIBLE);
			if (sk->sk_state != TCP_SYN_SENT)
				break;
			if (!signal_pending(current)) {
				release_sock(sk);
				schedule();
				lock_sock(sk);
				continue;
			}
			err = -ERESTARTSYS;
			break;
		}
		finish_wait(sk_sleep(sk), &wait);

		if (err)
			goto out_release;
	}

	if (sk->sk_state != TCP_ESTABLISHED) {
		sock->state = SS_UNCONNECTED;
		err = sock_error(sk);	/* Always set at this point */
		goto out_release;
	}

	sock->state = SS_CONNECTED;

out_release:
	release_sock(sk);

	return err;
}

static int rose_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sk_buff *skb;
	struct sock *newsk;
	DEFINE_WAIT(wait);
	struct sock *sk;
	int err = 0;

	if ((sk = sock->sk) == NULL)
		return -EINVAL;

	lock_sock(sk);
	if (sk->sk_type != SOCK_SEQPACKET) {
		err = -EOPNOTSUPP;
		goto out_release;
	}

	if (sk->sk_state != TCP_LISTEN) {
		err = -EINVAL;
		goto out_release;
	}

	/*
	 *	The write queue this time is holding sockets ready to use
	 *	hooked into the SABM we saved
	 */
	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

		skb = skb_dequeue(&sk->sk_receive_queue);
		if (skb)
			break;

		if (flags & O_NONBLOCK) {
			err = -EWOULDBLOCK;
			break;
		}
		if (!signal_pending(current)) {
			release_sock(sk);
			schedule();
			lock_sock(sk);
			continue;
		}
		err = -ERESTARTSYS;
		break;
	}
	finish_wait(sk_sleep(sk), &wait);
	if (err)
		goto out_release;

	newsk = skb->sk;
	sock_graft(newsk, newsock);

	/* Now attach up the new socket */
	skb->sk = NULL;
	kfree_skb(skb);
	sk->sk_ack_backlog--;

out_release:
	release_sock(sk);

	return err;
}

static int rose_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	struct full_sockaddr_rose *srose = (struct full_sockaddr_rose *)uaddr;
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	int n;

	memset(srose, 0, sizeof(*srose));
	if (peer != 0) {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -ENOTCONN;
		srose->srose_family = AF_ROSE;
		srose->srose_addr   = rose->dest_addr;
		srose->srose_call   = rose->dest_call;
		srose->srose_ndigis = rose->dest_ndigis;
		for (n = 0; n < rose->dest_ndigis; n++)
			srose->srose_digis[n] = rose->dest_digis[n];
	} else {
		srose->srose_family = AF_ROSE;
		srose->srose_addr   = rose->source_addr;
		srose->srose_call   = rose->source_call;
		srose->srose_ndigis = rose->source_ndigis;
		for (n = 0; n < rose->source_ndigis; n++)
			srose->srose_digis[n] = rose->source_digis[n];
	}

	*uaddr_len = sizeof(struct full_sockaddr_rose);
	return 0;
}

int rose_rx_call_request(struct sk_buff *skb, struct net_device *dev, struct rose_neigh *neigh, unsigned int lci)
{
	struct sock *sk;
	struct sock *make;
	struct rose_sock *make_rose;
	struct rose_facilities_struct facilities;
	int n;

	skb->sk = NULL;		/* Initially we don't know who it's for */

	/*
	 *	skb->data points to the rose frame start
	 */
	memset(&facilities, 0x00, sizeof(struct rose_facilities_struct));

	if (!rose_parse_facilities(skb->data + ROSE_CALL_REQ_FACILITIES_OFF,
				   skb->len - ROSE_CALL_REQ_FACILITIES_OFF,
				   &facilities)) {
		rose_transmit_clear_request(neigh, lci, ROSE_INVALID_FACILITY, 76);
		return 0;
	}

	sk = rose_find_listener(&facilities.source_addr, &facilities.source_call);

	/*
	 * We can't accept the Call Request.
	 */
	if (sk == NULL || sk_acceptq_is_full(sk) ||
	    (make = rose_make_new(sk)) == NULL) {
		rose_transmit_clear_request(neigh, lci, ROSE_NETWORK_CONGESTION, 120);
		return 0;
	}

	skb->sk     = make;
	make->sk_state = TCP_ESTABLISHED;
	make_rose = rose_sk(make);

	make_rose->lci           = lci;
	make_rose->dest_addr     = facilities.dest_addr;
	make_rose->dest_call     = facilities.dest_call;
	make_rose->dest_ndigis   = facilities.dest_ndigis;
	for (n = 0 ; n < facilities.dest_ndigis ; n++)
		make_rose->dest_digis[n] = facilities.dest_digis[n];
	make_rose->source_addr   = facilities.source_addr;
	make_rose->source_call   = facilities.source_call;
	make_rose->source_ndigis = facilities.source_ndigis;
	for (n = 0 ; n < facilities.source_ndigis ; n++)
		make_rose->source_digis[n]= facilities.source_digis[n];
	make_rose->neighbour     = neigh;
	make_rose->device        = dev;
	make_rose->facilities    = facilities;

	make_rose->neighbour->use++;

	if (rose_sk(sk)->defer) {
		make_rose->state = ROSE_STATE_5;
	} else {
		rose_write_internal(make, ROSE_CALL_ACCEPTED);
		make_rose->state = ROSE_STATE_3;
		rose_start_idletimer(make);
	}

	make_rose->condition = 0x00;
	make_rose->vs        = 0;
	make_rose->va        = 0;
	make_rose->vr        = 0;
	make_rose->vl        = 0;
	sk->sk_ack_backlog++;

	rose_insert_socket(make);

	skb_queue_head(&sk->sk_receive_queue, skb);

	rose_start_heartbeat(make);

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, skb->len);

	return 1;
}

static int rose_sendmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	struct sockaddr_rose *usrose = (struct sockaddr_rose *)msg->msg_name;
	int err;
	struct full_sockaddr_rose srose;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int n, size, qbit = 0;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_EOR|MSG_CMSG_COMPAT))
		return -EINVAL;

	if (sock_flag(sk, SOCK_ZAPPED))
		return -EADDRNOTAVAIL;

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		return -EPIPE;
	}

	if (rose->neighbour == NULL || rose->device == NULL)
		return -ENETUNREACH;

	if (usrose != NULL) {
		if (msg->msg_namelen != sizeof(struct sockaddr_rose) && msg->msg_namelen != sizeof(struct full_sockaddr_rose))
			return -EINVAL;
		memset(&srose, 0, sizeof(struct full_sockaddr_rose));
		memcpy(&srose, usrose, msg->msg_namelen);
		if (rosecmp(&rose->dest_addr, &srose.srose_addr) != 0 ||
		    ax25cmp(&rose->dest_call, &srose.srose_call) != 0)
			return -EISCONN;
		if (srose.srose_ndigis != rose->dest_ndigis)
			return -EISCONN;
		if (srose.srose_ndigis == rose->dest_ndigis) {
			for (n = 0 ; n < srose.srose_ndigis ; n++)
				if (ax25cmp(&rose->dest_digis[n],
					    &srose.srose_digis[n]))
					return -EISCONN;
		}
		if (srose.srose_family != AF_ROSE)
			return -EINVAL;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -ENOTCONN;

		srose.srose_family = AF_ROSE;
		srose.srose_addr   = rose->dest_addr;
		srose.srose_call   = rose->dest_call;
		srose.srose_ndigis = rose->dest_ndigis;
		for (n = 0 ; n < rose->dest_ndigis ; n++)
			srose.srose_digis[n] = rose->dest_digis[n];
	}

	/* Build a packet */
	/* Sanity check the packet size */
	if (len > 65535)
		return -EMSGSIZE;

	size = len + AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, msg->msg_flags & MSG_DONTWAIT, &err)) == NULL)
		return err;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN);

	/*
	 *	Put the data on the end
	 */

	skb_reset_transport_header(skb);
	skb_put(skb, len);

	err = memcpy_fromiovec(skb_transport_header(skb), msg->msg_iov, len);
	if (err) {
		kfree_skb(skb);
		return err;
	}

	/*
	 *	If the Q BIT Include socket option is in force, the first
	 *	byte of the user data is the logical value of the Q Bit.
	 */
	if (rose->qbitincl) {
		qbit = skb->data[0];
		skb_pull(skb, 1);
	}

	/*
	 *	Push down the ROSE header
	 */
	asmptr = skb_push(skb, ROSE_MIN_LEN);

	/* Build a ROSE Network header */
	asmptr[0] = ((rose->lci >> 8) & 0x0F) | ROSE_GFI;
	asmptr[1] = (rose->lci >> 0) & 0xFF;
	asmptr[2] = ROSE_DATA;

	if (qbit)
		asmptr[0] |= ROSE_Q_BIT;

	if (sk->sk_state != TCP_ESTABLISHED) {
		kfree_skb(skb);
		return -ENOTCONN;
	}

#ifdef M_BIT
#define ROSE_PACLEN (256-ROSE_MIN_LEN)
	if (skb->len - ROSE_MIN_LEN > ROSE_PACLEN) {
		unsigned char header[ROSE_MIN_LEN];
		struct sk_buff *skbn;
		int frontlen;
		int lg;

		/* Save a copy of the Header */
		skb_copy_from_linear_data(skb, header, ROSE_MIN_LEN);
		skb_pull(skb, ROSE_MIN_LEN);

		frontlen = skb_headroom(skb);

		while (skb->len > 0) {
			if ((skbn = sock_alloc_send_skb(sk, frontlen + ROSE_PACLEN, 0, &err)) == NULL) {
				kfree_skb(skb);
				return err;
			}

			skbn->sk   = sk;
			skbn->free = 1;
			skbn->arp  = 1;

			skb_reserve(skbn, frontlen);

			lg = (ROSE_PACLEN > skb->len) ? skb->len : ROSE_PACLEN;

			/* Copy the user data */
			skb_copy_from_linear_data(skb, skb_put(skbn, lg), lg);
			skb_pull(skb, lg);

			/* Duplicate the Header */
			skb_push(skbn, ROSE_MIN_LEN);
			skb_copy_to_linear_data(skbn, header, ROSE_MIN_LEN);

			if (skb->len > 0)
				skbn->data[2] |= M_BIT;

			skb_queue_tail(&sk->sk_write_queue, skbn); /* Throw it on the queue */
		}

		skb->free = 1;
		kfree_skb(skb);
	} else {
		skb_queue_tail(&sk->sk_write_queue, skb);		/* Throw it on the queue */
	}
#else
	skb_queue_tail(&sk->sk_write_queue, skb);	/* Shove it onto the queue */
#endif

	rose_kick(sk);

	return len;
}


static int rose_recvmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	size_t copied;
	unsigned char *asmptr;
	struct sk_buff *skb;
	int n, er, qbit;

	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */
	if (sk->sk_state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, flags & MSG_DONTWAIT, &er)) == NULL)
		return er;

	qbit = (skb->data[0] & ROSE_Q_BIT) == ROSE_Q_BIT;

	skb_pull(skb, ROSE_MIN_LEN);

	if (rose->qbitincl) {
		asmptr  = skb_push(skb, 1);
		*asmptr = qbit;
	}

	skb_reset_transport_header(skb);
	copied     = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	if (msg->msg_name) {
		struct sockaddr_rose *srose;

		memset(msg->msg_name, 0, sizeof(struct full_sockaddr_rose));
		srose = msg->msg_name;
		srose->srose_family = AF_ROSE;
		srose->srose_addr   = rose->dest_addr;
		srose->srose_call   = rose->dest_call;
		srose->srose_ndigis = rose->dest_ndigis;
		if (msg->msg_namelen >= sizeof(struct full_sockaddr_rose)) {
			struct full_sockaddr_rose *full_srose = (struct full_sockaddr_rose *)msg->msg_name;
			for (n = 0 ; n < rose->dest_ndigis ; n++)
				full_srose->srose_digis[n] = rose->dest_digis[n];
			msg->msg_namelen = sizeof(struct full_sockaddr_rose);
		} else {
			if (rose->dest_ndigis >= 1) {
				srose->srose_ndigis = 1;
				srose->srose_digi = rose->dest_digis[0];
			}
			msg->msg_namelen = sizeof(struct sockaddr_rose);
		}
	}

	skb_free_datagram(sk, skb);

	return copied;
}


static int rose_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct rose_sock *rose = rose_sk(sk);
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case TIOCOUTQ: {
		long amount;

		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		return put_user(amount, (unsigned int __user *) argp);
	}

	case TIOCINQ: {
		struct sk_buff *skb;
		long amount = 0L;
		/* These two are safe on a single CPU system as only user tasks fiddle here */
		if ((skb = skb_peek(&sk->sk_receive_queue)) != NULL)
			amount = skb->len;
		return put_user(amount, (unsigned int __user *) argp);
	}

	case SIOCGSTAMP:
		return sock_get_timestamp(sk, (struct timeval __user *) argp);

	case SIOCGSTAMPNS:
		return sock_get_timestampns(sk, (struct timespec __user *) argp);

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
		return -EINVAL;

	case SIOCADDRT:
	case SIOCDELRT:
	case SIOCRSCLRRT:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		return rose_rt_ioctl(cmd, argp);

	case SIOCRSGCAUSE: {
		struct rose_cause_struct rose_cause;
		rose_cause.cause      = rose->cause;
		rose_cause.diagnostic = rose->diagnostic;
		return copy_to_user(argp, &rose_cause, sizeof(struct rose_cause_struct)) ? -EFAULT : 0;
	}

	case SIOCRSSCAUSE: {
		struct rose_cause_struct rose_cause;
		if (copy_from_user(&rose_cause, argp, sizeof(struct rose_cause_struct)))
			return -EFAULT;
		rose->cause      = rose_cause.cause;
		rose->diagnostic = rose_cause.diagnostic;
		return 0;
	}

	case SIOCRSSL2CALL:
		if (!capable(CAP_NET_ADMIN)) return -EPERM;
		if (ax25cmp(&rose_callsign, &null_ax25_address) != 0)
			ax25_listen_release(&rose_callsign, NULL);
		if (copy_from_user(&rose_callsign, argp, sizeof(ax25_address)))
			return -EFAULT;
		if (ax25cmp(&rose_callsign, &null_ax25_address) != 0)
			return ax25_listen_register(&rose_callsign, NULL);

		return 0;

	case SIOCRSGL2CALL:
		return copy_to_user(argp, &rose_callsign, sizeof(ax25_address)) ? -EFAULT : 0;

	case SIOCRSACCEPT:
		if (rose->state == ROSE_STATE_5) {
			rose_write_internal(sk, ROSE_CALL_ACCEPTED);
			rose_start_idletimer(sk);
			rose->condition = 0x00;
			rose->vs        = 0;
			rose->va        = 0;
			rose->vr        = 0;
			rose->vl        = 0;
			rose->state     = ROSE_STATE_3;
		}
		return 0;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

#ifdef CONFIG_PROC_FS
static void *rose_info_start(struct seq_file *seq, loff_t *pos)
	__acquires(rose_list_lock)
{
	spin_lock_bh(&rose_list_lock);
	return seq_hlist_start_head(&rose_list, *pos);
}

static void *rose_info_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return seq_hlist_next(v, &rose_list, pos);
}

static void rose_info_stop(struct seq_file *seq, void *v)
	__releases(rose_list_lock)
{
	spin_unlock_bh(&rose_list_lock);
}

static int rose_info_show(struct seq_file *seq, void *v)
{
	char buf[11], rsbuf[11];

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "dest_addr  dest_call src_addr   src_call  dev   lci neigh st vs vr va   t  t1  t2  t3  hb    idle Snd-Q Rcv-Q inode\n");

	else {
		struct sock *s = sk_entry(v);
		struct rose_sock *rose = rose_sk(s);
		const char *devname, *callsign;
		const struct net_device *dev = rose->device;

		if (!dev)
			devname = "???";
		else
			devname = dev->name;

		seq_printf(seq, "%-10s %-9s ",
			   rose2asc(rsbuf, &rose->dest_addr),
			   ax2asc(buf, &rose->dest_call));

		if (ax25cmp(&rose->source_call, &null_ax25_address) == 0)
			callsign = "??????-?";
		else
			callsign = ax2asc(buf, &rose->source_call);

		seq_printf(seq,
			   "%-10s %-9s %-5s %3.3X %05d  %d  %d  %d  %d %3lu %3lu %3lu %3lu %3lu %3lu/%03lu %5d %5d %ld\n",
			rose2asc(rsbuf, &rose->source_addr),
			callsign,
			devname,
			rose->lci & 0x0FFF,
			(rose->neighbour) ? rose->neighbour->number : 0,
			rose->state,
			rose->vs,
			rose->vr,
			rose->va,
			ax25_display_timer(&rose->timer) / HZ,
			rose->t1 / HZ,
			rose->t2 / HZ,
			rose->t3 / HZ,
			rose->hb / HZ,
			ax25_display_timer(&rose->idletimer) / (60 * HZ),
			rose->idle / (60 * HZ),
			sk_wmem_alloc_get(s),
			sk_rmem_alloc_get(s),
			s->sk_socket ? SOCK_INODE(s->sk_socket)->i_ino : 0L);
	}

	return 0;
}

static const struct seq_operations rose_info_seqops = {
	.start = rose_info_start,
	.next = rose_info_next,
	.stop = rose_info_stop,
	.show = rose_info_show,
};

static int rose_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &rose_info_seqops);
}

static const struct file_operations rose_info_fops = {
	.owner = THIS_MODULE,
	.open = rose_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif	/* CONFIG_PROC_FS */

static const struct net_proto_family rose_family_ops = {
	.family		=	PF_ROSE,
	.create		=	rose_create,
	.owner		=	THIS_MODULE,
};

static const struct proto_ops rose_proto_ops = {
	.family		=	PF_ROSE,
	.owner		=	THIS_MODULE,
	.release	=	rose_release,
	.bind		=	rose_bind,
	.connect	=	rose_connect,
	.socketpair	=	sock_no_socketpair,
	.accept		=	rose_accept,
	.getname	=	rose_getname,
	.poll		=	datagram_poll,
	.ioctl		=	rose_ioctl,
	.listen		=	rose_listen,
	.shutdown	=	sock_no_shutdown,
	.setsockopt	=	rose_setsockopt,
	.getsockopt	=	rose_getsockopt,
	.sendmsg	=	rose_sendmsg,
	.recvmsg	=	rose_recvmsg,
	.mmap		=	sock_no_mmap,
	.sendpage	=	sock_no_sendpage,
};

static struct notifier_block rose_dev_notifier = {
	.notifier_call	=	rose_device_event,
};

static struct net_device **dev_rose;

static struct ax25_protocol rose_pid = {
	.pid	= AX25_P_ROSE,
	.func	= rose_route_frame
};

static struct ax25_linkfail rose_linkfail_notifier = {
	.func	= rose_link_failed
};

static int __init rose_proto_init(void)
{
	int i;
	int rc;

	if (rose_ndevs > 0x7FFFFFFF/sizeof(struct net_device *)) {
		printk(KERN_ERR "ROSE: rose_proto_init - rose_ndevs parameter to large\n");
		rc = -EINVAL;
		goto out;
	}

	rc = proto_register(&rose_proto, 0);
	if (rc != 0)
		goto out;

	rose_callsign = null_ax25_address;

	dev_rose = kzalloc(rose_ndevs * sizeof(struct net_device *), GFP_KERNEL);
	if (dev_rose == NULL) {
		printk(KERN_ERR "ROSE: rose_proto_init - unable to allocate device structure\n");
		rc = -ENOMEM;
		goto out_proto_unregister;
	}

	for (i = 0; i < rose_ndevs; i++) {
		struct net_device *dev;
		char name[IFNAMSIZ];

		sprintf(name, "rose%d", i);
		dev = alloc_netdev(0, name, rose_setup);
		if (!dev) {
			printk(KERN_ERR "ROSE: rose_proto_init - unable to allocate memory\n");
			rc = -ENOMEM;
			goto fail;
		}
		rc = register_netdev(dev);
		if (rc) {
			printk(KERN_ERR "ROSE: netdevice registration failed\n");
			free_netdev(dev);
			goto fail;
		}
		rose_set_lockdep_key(dev);
		dev_rose[i] = dev;
	}

	sock_register(&rose_family_ops);
	register_netdevice_notifier(&rose_dev_notifier);

	ax25_register_pid(&rose_pid);
	ax25_linkfail_register(&rose_linkfail_notifier);

#ifdef CONFIG_SYSCTL
	rose_register_sysctl();
#endif
	rose_loopback_init();

	rose_add_loopback_neigh();

	proc_net_fops_create(&init_net, "rose", S_IRUGO, &rose_info_fops);
	proc_net_fops_create(&init_net, "rose_neigh", S_IRUGO, &rose_neigh_fops);
	proc_net_fops_create(&init_net, "rose_nodes", S_IRUGO, &rose_nodes_fops);
	proc_net_fops_create(&init_net, "rose_routes", S_IRUGO, &rose_routes_fops);
out:
	return rc;
fail:
	while (--i >= 0) {
		unregister_netdev(dev_rose[i]);
		free_netdev(dev_rose[i]);
	}
	kfree(dev_rose);
out_proto_unregister:
	proto_unregister(&rose_proto);
	goto out;
}
module_init(rose_proto_init);

module_param(rose_ndevs, int, 0);
MODULE_PARM_DESC(rose_ndevs, "number of ROSE devices");

MODULE_AUTHOR("Jonathan Naylor G4KLX <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The amateur radio ROSE network layer protocol");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_ROSE);

static void __exit rose_exit(void)
{
	int i;

	proc_net_remove(&init_net, "rose");
	proc_net_remove(&init_net, "rose_neigh");
	proc_net_remove(&init_net, "rose_nodes");
	proc_net_remove(&init_net, "rose_routes");
	rose_loopback_clear();

	rose_rt_free();

	ax25_protocol_release(AX25_P_ROSE);
	ax25_linkfail_release(&rose_linkfail_notifier);

	if (ax25cmp(&rose_callsign, &null_ax25_address) != 0)
		ax25_listen_release(&rose_callsign, NULL);

#ifdef CONFIG_SYSCTL
	rose_unregister_sysctl();
#endif
	unregister_netdevice_notifier(&rose_dev_notifier);

	sock_unregister(PF_ROSE);

	for (i = 0; i < rose_ndevs; i++) {
		struct net_device *dev = dev_rose[i];

		if (dev) {
			unregister_netdev(dev);
			free_netdev(dev);
		}
	}

	kfree(dev_rose);
	proto_unregister(&rose_proto);
}

module_exit(rose_exit);
