/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright Darryl Miles G7LED (dlm@g7led.demon.co.uk)
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/stat.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <net/netrom.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/ip.h>
#include <net/tcp_states.h>
#include <net/arp.h>
#include <linux/init.h>

static int nr_ndevs = 4;

int sysctl_netrom_default_path_quality            = NR_DEFAULT_QUAL;
int sysctl_netrom_obsolescence_count_initialiser  = NR_DEFAULT_OBS;
int sysctl_netrom_network_ttl_initialiser         = NR_DEFAULT_TTL;
int sysctl_netrom_transport_timeout               = NR_DEFAULT_T1;
int sysctl_netrom_transport_maximum_tries         = NR_DEFAULT_N2;
int sysctl_netrom_transport_acknowledge_delay     = NR_DEFAULT_T2;
int sysctl_netrom_transport_busy_delay            = NR_DEFAULT_T4;
int sysctl_netrom_transport_requested_window_size = NR_DEFAULT_WINDOW;
int sysctl_netrom_transport_no_activity_timeout   = NR_DEFAULT_IDLE;
int sysctl_netrom_routing_control                 = NR_DEFAULT_ROUTING;
int sysctl_netrom_link_fails_count                = NR_DEFAULT_FAILS;
int sysctl_netrom_reset_circuit                   = NR_DEFAULT_RESET;

static unsigned short circuit = 0x101;

static HLIST_HEAD(nr_list);
static DEFINE_SPINLOCK(nr_list_lock);

static const struct proto_ops nr_proto_ops;

/*
 * NETROM network devices are virtual network devices encapsulating NETROM
 * frames into AX.25 which will be sent through an AX.25 device, so form a
 * special "super class" of normal net devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key nr_netdev_xmit_lock_key;

/*
 *	Socket removal during an interrupt is now safe.
 */
static void nr_remove_socket(struct sock *sk)
{
	spin_lock_bh(&nr_list_lock);
	sk_del_node_init(sk);
	spin_unlock_bh(&nr_list_lock);
}

/*
 *	Kill all bound sockets on a dropped device.
 */
static void nr_kill_by_device(struct net_device *dev)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&nr_list_lock);
	sk_for_each(s, node, &nr_list)
		if (nr_sk(s)->device == dev)
			nr_disconnect(s, ENETUNREACH);
	spin_unlock_bh(&nr_list_lock);
}

/*
 *	Handle device status changes.
 */
static int nr_device_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;

	nr_kill_by_device(dev);
	nr_rt_device_down(dev);

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
static void nr_insert_socket(struct sock *sk)
{
	spin_lock_bh(&nr_list_lock);
	sk_add_node(sk, &nr_list);
	spin_unlock_bh(&nr_list_lock);
}

/*
 *	Find a socket that wants to accept the Connect Request we just
 *	received.
 */
static struct sock *nr_find_listener(ax25_address *addr)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&nr_list_lock);
	sk_for_each(s, node, &nr_list)
		if (!ax25cmp(&nr_sk(s)->source_addr, addr) &&
		    s->sk_state == TCP_LISTEN) {
			bh_lock_sock(s);
			goto found;
		}
	s = NULL;
found:
	spin_unlock_bh(&nr_list_lock);
	return s;
}

/*
 *	Find a connected NET/ROM socket given my circuit IDs.
 */
static struct sock *nr_find_socket(unsigned char index, unsigned char id)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&nr_list_lock);
	sk_for_each(s, node, &nr_list) {
		struct nr_sock *nr = nr_sk(s);

		if (nr->my_index == index && nr->my_id == id) {
			bh_lock_sock(s);
			goto found;
		}
	}
	s = NULL;
found:
	spin_unlock_bh(&nr_list_lock);
	return s;
}

/*
 *	Find a connected NET/ROM socket given their circuit IDs.
 */
static struct sock *nr_find_peer(unsigned char index, unsigned char id,
	ax25_address *dest)
{
	struct sock *s;
	struct hlist_node *node;

	spin_lock_bh(&nr_list_lock);
	sk_for_each(s, node, &nr_list) {
		struct nr_sock *nr = nr_sk(s);

		if (nr->your_index == index && nr->your_id == id &&
		    !ax25cmp(&nr->dest_addr, dest)) {
			bh_lock_sock(s);
			goto found;
		}
	}
	s = NULL;
found:
	spin_unlock_bh(&nr_list_lock);
	return s;
}

/*
 *	Find next free circuit ID.
 */
static unsigned short nr_find_next_circuit(void)
{
	unsigned short id = circuit;
	unsigned char i, j;
	struct sock *sk;

	for (;;) {
		i = id / 256;
		j = id % 256;

		if (i != 0 && j != 0) {
			if ((sk=nr_find_socket(i, j)) == NULL)
				break;
			bh_unlock_sock(sk);
		}

		id++;
	}

	return id;
}

/*
 *	Deferred destroy.
 */
void nr_destroy_socket(struct sock *);

/*
 *	Handler for deferred kills.
 */
static void nr_destroy_timer(unsigned long data)
{
	struct sock *sk=(struct sock *)data;
	bh_lock_sock(sk);
	sock_hold(sk);
	nr_destroy_socket(sk);
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself
 *	against interrupt users but doesn't worry about being called during
 *	work. Once it is removed from the queue no interrupt or bottom half
 *	will touch it and we are (fairly 8-) ) safe.
 */
void nr_destroy_socket(struct sock *sk)
{
	struct sk_buff *skb;

	nr_remove_socket(sk);

	nr_stop_heartbeat(sk);
	nr_stop_t1timer(sk);
	nr_stop_t2timer(sk);
	nr_stop_t4timer(sk);
	nr_stop_idletimer(sk);

	nr_clear_queues(sk);		/* Flush the queues */

	while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
		if (skb->sk != sk) { /* A pending connection */
			/* Queue the unaccepted socket for death */
			sock_set_flag(skb->sk, SOCK_DEAD);
			nr_start_heartbeat(skb->sk);
			nr_sk(skb->sk)->state = NR_STATE_0;
		}

		kfree_skb(skb);
	}

	if (atomic_read(&sk->sk_wmem_alloc) ||
	    atomic_read(&sk->sk_rmem_alloc)) {
		/* Defer: outstanding buffers */
		sk->sk_timer.function = nr_destroy_timer;
		sk->sk_timer.expires  = jiffies + 2 * HZ;
		add_timer(&sk->sk_timer);
	} else
		sock_put(sk);
}

/*
 *	Handling for system calls applied via the various interfaces to a
 *	NET/ROM socket object.
 */

static int nr_setsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	struct nr_sock *nr = nr_sk(sk);
	int opt;

	if (level != SOL_NETROM)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(opt, (int __user *)optval))
		return -EFAULT;

	switch (optname) {
	case NETROM_T1:
		if (opt < 1)
			return -EINVAL;
		nr->t1 = opt * HZ;
		return 0;

	case NETROM_T2:
		if (opt < 1)
			return -EINVAL;
		nr->t2 = opt * HZ;
		return 0;

	case NETROM_N2:
		if (opt < 1 || opt > 31)
			return -EINVAL;
		nr->n2 = opt;
		return 0;

	case NETROM_T4:
		if (opt < 1)
			return -EINVAL;
		nr->t4 = opt * HZ;
		return 0;

	case NETROM_IDLE:
		if (opt < 0)
			return -EINVAL;
		nr->idle = opt * 60 * HZ;
		return 0;

	default:
		return -ENOPROTOOPT;
	}
}

static int nr_getsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct nr_sock *nr = nr_sk(sk);
	int val = 0;
	int len;

	if (level != SOL_NETROM)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case NETROM_T1:
		val = nr->t1 / HZ;
		break;

	case NETROM_T2:
		val = nr->t2 / HZ;
		break;

	case NETROM_N2:
		val = nr->n2;
		break;

	case NETROM_T4:
		val = nr->t4 / HZ;
		break;

	case NETROM_IDLE:
		val = nr->idle / (60 * HZ);
		break;

	default:
		return -ENOPROTOOPT;
	}

	len = min_t(unsigned int, len, sizeof(int));

	if (put_user(len, optlen))
		return -EFAULT;

	return copy_to_user(optval, &val, len) ? -EFAULT : 0;
}

static int nr_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	lock_sock(sk);
	if (sk->sk_state != TCP_LISTEN) {
		memset(&nr_sk(sk)->user_addr, 0, AX25_ADDR_LEN);
		sk->sk_max_ack_backlog = backlog;
		sk->sk_state           = TCP_LISTEN;
		release_sock(sk);
		return 0;
	}
	release_sock(sk);

	return -EOPNOTSUPP;
}

static struct proto nr_proto = {
	.name	  = "NETROM",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct nr_sock),
};

static int nr_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct nr_sock *nr;

	if (sock->type != SOCK_SEQPACKET || protocol != 0)
		return -ESOCKTNOSUPPORT;

	if ((sk = sk_alloc(PF_NETROM, GFP_ATOMIC, &nr_proto, 1)) == NULL)
		return -ENOMEM;

	nr = nr_sk(sk);

	sock_init_data(sock, sk);

	sock->ops    = &nr_proto_ops;
	sk->sk_protocol = protocol;

	skb_queue_head_init(&nr->ack_queue);
	skb_queue_head_init(&nr->reseq_queue);
	skb_queue_head_init(&nr->frag_queue);

	nr_init_timers(sk);

	nr->t1     =
		msecs_to_jiffies(sysctl_netrom_transport_timeout);
	nr->t2     =
		msecs_to_jiffies(sysctl_netrom_transport_acknowledge_delay);
	nr->n2     =
		msecs_to_jiffies(sysctl_netrom_transport_maximum_tries);
	nr->t4     =
		msecs_to_jiffies(sysctl_netrom_transport_busy_delay);
	nr->idle   =
		msecs_to_jiffies(sysctl_netrom_transport_no_activity_timeout);
	nr->window = sysctl_netrom_transport_requested_window_size;

	nr->bpqext = 1;
	nr->state  = NR_STATE_0;

	return 0;
}

static struct sock *nr_make_new(struct sock *osk)
{
	struct sock *sk;
	struct nr_sock *nr, *onr;

	if (osk->sk_type != SOCK_SEQPACKET)
		return NULL;

	if ((sk = sk_alloc(PF_NETROM, GFP_ATOMIC, osk->sk_prot, 1)) == NULL)
		return NULL;

	nr = nr_sk(sk);

	sock_init_data(NULL, sk);

	sk->sk_type     = osk->sk_type;
	sk->sk_socket   = osk->sk_socket;
	sk->sk_priority = osk->sk_priority;
	sk->sk_protocol = osk->sk_protocol;
	sk->sk_rcvbuf   = osk->sk_rcvbuf;
	sk->sk_sndbuf   = osk->sk_sndbuf;
	sk->sk_state    = TCP_ESTABLISHED;
	sk->sk_sleep    = osk->sk_sleep;
	sock_copy_flags(sk, osk);

	skb_queue_head_init(&nr->ack_queue);
	skb_queue_head_init(&nr->reseq_queue);
	skb_queue_head_init(&nr->frag_queue);

	nr_init_timers(sk);

	onr = nr_sk(osk);

	nr->t1      = onr->t1;
	nr->t2      = onr->t2;
	nr->n2      = onr->n2;
	nr->t4      = onr->t4;
	nr->idle    = onr->idle;
	nr->window  = onr->window;

	nr->device  = onr->device;
	nr->bpqext  = onr->bpqext;

	return sk;
}

static int nr_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct nr_sock *nr;

	if (sk == NULL) return 0;

	sock_hold(sk);
	lock_sock(sk);
	nr = nr_sk(sk);

	switch (nr->state) {
	case NR_STATE_0:
	case NR_STATE_1:
	case NR_STATE_2:
		nr_disconnect(sk, 0);
		nr_destroy_socket(sk);
		break;

	case NR_STATE_3:
		nr_clear_queues(sk);
		nr->n2count = 0;
		nr_write_internal(sk, NR_DISCREQ);
		nr_start_t1timer(sk);
		nr_stop_t2timer(sk);
		nr_stop_t4timer(sk);
		nr_stop_idletimer(sk);
		nr->state    = NR_STATE_2;
		sk->sk_state    = TCP_CLOSE;
		sk->sk_shutdown |= SEND_SHUTDOWN;
		sk->sk_state_change(sk);
		sock_orphan(sk);
		sock_set_flag(sk, SOCK_DESTROY);
		sk->sk_socket   = NULL;
		break;

	default:
		sk->sk_socket = NULL;
		break;
	}

	sock->sk   = NULL;
	release_sock(sk);
	sock_put(sk);

	return 0;
}

static int nr_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct nr_sock *nr = nr_sk(sk);
	struct full_sockaddr_ax25 *addr = (struct full_sockaddr_ax25 *)uaddr;
	struct net_device *dev;
	ax25_uid_assoc *user;
	ax25_address *source;

	lock_sock(sk);
	if (!sock_flag(sk, SOCK_ZAPPED)) {
		release_sock(sk);
		return -EINVAL;
	}
	if (addr_len < sizeof(struct sockaddr_ax25) || addr_len > sizeof(struct full_sockaddr_ax25)) {
		release_sock(sk);
		return -EINVAL;
	}
	if (addr_len < (addr->fsa_ax25.sax25_ndigis * sizeof(ax25_address) + sizeof(struct sockaddr_ax25))) {
		release_sock(sk);
		return -EINVAL;
	}
	if (addr->fsa_ax25.sax25_family != AF_NETROM) {
		release_sock(sk);
		return -EINVAL;
	}
	if ((dev = nr_dev_get(&addr->fsa_ax25.sax25_call)) == NULL) {
		SOCK_DEBUG(sk, "NET/ROM: bind failed: invalid node callsign\n");
		release_sock(sk);
		return -EADDRNOTAVAIL;
	}

	/*
	 * Only the super user can set an arbitrary user callsign.
	 */
	if (addr->fsa_ax25.sax25_ndigis == 1) {
		if (!capable(CAP_NET_BIND_SERVICE)) {
			dev_put(dev);
			release_sock(sk);
			return -EACCES;
		}
		nr->user_addr   = addr->fsa_digipeater[0];
		nr->source_addr = addr->fsa_ax25.sax25_call;
	} else {
		source = &addr->fsa_ax25.sax25_call;

		user = ax25_findbyuid(current->euid);
		if (user) {
			nr->user_addr   = user->call;
			ax25_uid_put(user);
		} else {
			if (ax25_uid_policy && !capable(CAP_NET_BIND_SERVICE)) {
				release_sock(sk);
				dev_put(dev);
				return -EPERM;
			}
			nr->user_addr   = *source;
		}

		nr->source_addr = *source;
	}

	nr->device = dev;
	nr_insert_socket(sk);

	sock_reset_flag(sk, SOCK_ZAPPED);
	dev_put(dev);
	release_sock(sk);
	SOCK_DEBUG(sk, "NET/ROM: socket is bound\n");
	return 0;
}

static int nr_connect(struct socket *sock, struct sockaddr *uaddr,
	int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct nr_sock *nr = nr_sk(sk);
	struct sockaddr_ax25 *addr = (struct sockaddr_ax25 *)uaddr;
	ax25_address *source = NULL;
	ax25_uid_assoc *user;
	struct net_device *dev;
	int err = 0;

	lock_sock(sk);
	if (sk->sk_state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		goto out_release;	/* Connect completed during a ERESTARTSYS event */
	}

	if (sk->sk_state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		err = -ECONNREFUSED;
		goto out_release;
	}

	if (sk->sk_state == TCP_ESTABLISHED) {
		err = -EISCONN;	/* No reconnect on a seqpacket socket */
		goto out_release;
	}

	sk->sk_state   = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25)) {
		err = -EINVAL;
		goto out_release;
	}
	if (addr->sax25_family != AF_NETROM) {
		err = -EINVAL;
		goto out_release;
	}
	if (sock_flag(sk, SOCK_ZAPPED)) {	/* Must bind first - autobinding in this may or may not work */
		sock_reset_flag(sk, SOCK_ZAPPED);

		if ((dev = nr_dev_first()) == NULL) {
			err = -ENETUNREACH;
			goto out_release;
		}
		source = (ax25_address *)dev->dev_addr;

		user = ax25_findbyuid(current->euid);
		if (user) {
			nr->user_addr   = user->call;
			ax25_uid_put(user);
		} else {
			if (ax25_uid_policy && !capable(CAP_NET_ADMIN)) {
				dev_put(dev);
				err = -EPERM;
				goto out_release;
			}
			nr->user_addr   = *source;
		}

		nr->source_addr = *source;
		nr->device      = dev;

		dev_put(dev);
		nr_insert_socket(sk);		/* Finish the bind */
	}

	nr->dest_addr = addr->sax25_call;

	release_sock(sk);
	circuit = nr_find_next_circuit();
	lock_sock(sk);

	nr->my_index = circuit / 256;
	nr->my_id    = circuit % 256;

	circuit++;

	/* Move to connecting socket, start sending Connect Requests */
	sock->state  = SS_CONNECTING;
	sk->sk_state = TCP_SYN_SENT;

	nr_establish_data_link(sk);

	nr->state = NR_STATE_1;

	nr_start_heartbeat(sk);

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
			prepare_to_wait(sk->sk_sleep, &wait,
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
		finish_wait(sk->sk_sleep, &wait);
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

static int nr_accept(struct socket *sock, struct socket *newsock, int flags)
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
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
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
	finish_wait(sk->sk_sleep, &wait);
	if (err)
		goto out_release;

	newsk = skb->sk;
	newsk->sk_socket = newsock;
	newsk->sk_sleep = &newsock->wait;

	/* Now attach up the new socket */
	kfree_skb(skb);
	sk_acceptq_removed(sk);
	newsock->sk = newsk;

out_release:
	release_sock(sk);

	return err;
}

static int nr_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	struct full_sockaddr_ax25 *sax = (struct full_sockaddr_ax25 *)uaddr;
	struct sock *sk = sock->sk;
	struct nr_sock *nr = nr_sk(sk);

	lock_sock(sk);
	if (peer != 0) {
		if (sk->sk_state != TCP_ESTABLISHED) {
			release_sock(sk);
			return -ENOTCONN;
		}
		sax->fsa_ax25.sax25_family = AF_NETROM;
		sax->fsa_ax25.sax25_ndigis = 1;
		sax->fsa_ax25.sax25_call   = nr->user_addr;
		sax->fsa_digipeater[0]     = nr->dest_addr;
		*uaddr_len = sizeof(struct full_sockaddr_ax25);
	} else {
		sax->fsa_ax25.sax25_family = AF_NETROM;
		sax->fsa_ax25.sax25_ndigis = 0;
		sax->fsa_ax25.sax25_call   = nr->source_addr;
		*uaddr_len = sizeof(struct sockaddr_ax25);
	}
	release_sock(sk);

	return 0;
}

int nr_rx_frame(struct sk_buff *skb, struct net_device *dev)
{
	struct sock *sk;
	struct sock *make;
	struct nr_sock *nr_make;
	ax25_address *src, *dest, *user;
	unsigned short circuit_index, circuit_id;
	unsigned short peer_circuit_index, peer_circuit_id;
	unsigned short frametype, flags, window, timeout;
	int ret;

	skb->sk = NULL;		/* Initially we don't know who it's for */

	/*
	 *	skb->data points to the netrom frame start
	 */

	src  = (ax25_address *)(skb->data + 0);
	dest = (ax25_address *)(skb->data + 7);

	circuit_index      = skb->data[15];
	circuit_id         = skb->data[16];
	peer_circuit_index = skb->data[17];
	peer_circuit_id    = skb->data[18];
	frametype          = skb->data[19] & 0x0F;
	flags              = skb->data[19] & 0xF0;

	/*
	 * Check for an incoming IP over NET/ROM frame.
	 */
	if (frametype == NR_PROTOEXT &&
	    circuit_index == NR_PROTO_IP && circuit_id == NR_PROTO_IP) {
		skb_pull(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN);
		skb_reset_transport_header(skb);

		return nr_rx_ip(skb, dev);
	}

	/*
	 * Find an existing socket connection, based on circuit ID, if it's
	 * a Connect Request base it on their circuit ID.
	 *
	 * Circuit ID 0/0 is not valid but it could still be a "reset" for a
	 * circuit that no longer exists at the other end ...
	 */

	sk = NULL;

	if (circuit_index == 0 && circuit_id == 0) {
		if (frametype == NR_CONNACK && flags == NR_CHOKE_FLAG)
			sk = nr_find_peer(peer_circuit_index, peer_circuit_id, src);
	} else {
		if (frametype == NR_CONNREQ)
			sk = nr_find_peer(circuit_index, circuit_id, src);
		else
			sk = nr_find_socket(circuit_index, circuit_id);
	}

	if (sk != NULL) {
		skb_reset_transport_header(skb);

		if (frametype == NR_CONNACK && skb->len == 22)
			nr_sk(sk)->bpqext = 1;
		else
			nr_sk(sk)->bpqext = 0;

		ret = nr_process_rx_frame(sk, skb);
		bh_unlock_sock(sk);
		return ret;
	}

	/*
	 * Now it should be a CONNREQ.
	 */
	if (frametype != NR_CONNREQ) {
		/*
		 * Here it would be nice to be able to send a reset but
		 * NET/ROM doesn't have one.  We've tried to extend the protocol
		 * by sending NR_CONNACK | NR_CHOKE_FLAGS replies but that
		 * apparently kills BPQ boxes... :-(
		 * So now we try to follow the established behaviour of
		 * G8PZT's Xrouter which is sending packets with command type 7
		 * as an extension of the protocol.
		 */
		if (sysctl_netrom_reset_circuit &&
		    (frametype != NR_RESET || flags != 0))
			nr_transmit_reset(skb, 1);

		return 0;
	}

	sk = nr_find_listener(dest);

	user = (ax25_address *)(skb->data + 21);

	if (sk == NULL || sk_acceptq_is_full(sk) ||
	    (make = nr_make_new(sk)) == NULL) {
		nr_transmit_refusal(skb, 0);
		if (sk)
			bh_unlock_sock(sk);
		return 0;
	}

	window = skb->data[20];

	skb->sk             = make;
	make->sk_state	    = TCP_ESTABLISHED;

	/* Fill in his circuit details */
	nr_make = nr_sk(make);
	nr_make->source_addr = *dest;
	nr_make->dest_addr   = *src;
	nr_make->user_addr   = *user;

	nr_make->your_index  = circuit_index;
	nr_make->your_id     = circuit_id;

	bh_unlock_sock(sk);
	circuit = nr_find_next_circuit();
	bh_lock_sock(sk);

	nr_make->my_index    = circuit / 256;
	nr_make->my_id       = circuit % 256;

	circuit++;

	/* Window negotiation */
	if (window < nr_make->window)
		nr_make->window = window;

	/* L4 timeout negotiation */
	if (skb->len == 37) {
		timeout = skb->data[36] * 256 + skb->data[35];
		if (timeout * HZ < nr_make->t1)
			nr_make->t1 = timeout * HZ;
		nr_make->bpqext = 1;
	} else {
		nr_make->bpqext = 0;
	}

	nr_write_internal(make, NR_CONNACK);

	nr_make->condition = 0x00;
	nr_make->vs        = 0;
	nr_make->va        = 0;
	nr_make->vr        = 0;
	nr_make->vl        = 0;
	nr_make->state     = NR_STATE_3;
	sk_acceptq_added(sk);
	skb_queue_head(&sk->sk_receive_queue, skb);

	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_data_ready(sk, skb->len);

	bh_unlock_sock(sk);

	nr_insert_socket(make);

	nr_start_heartbeat(make);
	nr_start_idletimer(make);

	return 1;
}

static int nr_sendmsg(struct kiocb *iocb, struct socket *sock,
		      struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct nr_sock *nr = nr_sk(sk);
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)msg->msg_name;
	int err;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_EOR|MSG_CMSG_COMPAT))
		return -EINVAL;

	lock_sock(sk);
	if (sock_flag(sk, SOCK_ZAPPED)) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		err = -EPIPE;
		goto out;
	}

	if (nr->device == NULL) {
		err = -ENETUNREACH;
		goto out;
	}

	if (usax) {
		if (msg->msg_namelen < sizeof(sax)) {
			err = -EINVAL;
			goto out;
		}
		sax = *usax;
		if (ax25cmp(&nr->dest_addr, &sax.sax25_call) != 0) {
			err = -EISCONN;
			goto out;
		}
		if (sax.sax25_family != AF_NETROM) {
			err = -EINVAL;
			goto out;
		}
	} else {
		if (sk->sk_state != TCP_ESTABLISHED) {
			err = -ENOTCONN;
			goto out;
		}
		sax.sax25_family = AF_NETROM;
		sax.sax25_call   = nr->dest_addr;
	}

	SOCK_DEBUG(sk, "NET/ROM: sendto: Addresses built.\n");

	/* Build a packet */
	SOCK_DEBUG(sk, "NET/ROM: sendto: building packet.\n");
	size = len + NR_NETWORK_LEN + NR_TRANSPORT_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, msg->msg_flags & MSG_DONTWAIT, &err)) == NULL)
		goto out;

	skb_reserve(skb, size - len);
	skb_reset_transport_header(skb);

	/*
	 *	Push down the NET/ROM header
	 */

	asmptr = skb_push(skb, NR_TRANSPORT_LEN);
	SOCK_DEBUG(sk, "Building NET/ROM Header.\n");

	/* Build a NET/ROM Transport header */

	*asmptr++ = nr->your_index;
	*asmptr++ = nr->your_id;
	*asmptr++ = 0;		/* To be filled in later */
	*asmptr++ = 0;		/*      Ditto            */
	*asmptr++ = NR_INFO;
	SOCK_DEBUG(sk, "Built header.\n");

	/*
	 *	Put the data on the end
	 */
	skb_put(skb, len);

	SOCK_DEBUG(sk, "NET/ROM: Appending user data\n");

	/* User data follows immediately after the NET/ROM transport header */
	if (memcpy_fromiovec(skb_transport_header(skb), msg->msg_iov, len)) {
		kfree_skb(skb);
		err = -EFAULT;
		goto out;
	}

	SOCK_DEBUG(sk, "NET/ROM: Transmitting buffer\n");

	if (sk->sk_state != TCP_ESTABLISHED) {
		kfree_skb(skb);
		err = -ENOTCONN;
		goto out;
	}

	nr_output(sk, skb);	/* Shove it onto the queue */

	err = len;
out:
	release_sock(sk);
	return err;
}

static int nr_recvmsg(struct kiocb *iocb, struct socket *sock,
		      struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)msg->msg_name;
	size_t copied;
	struct sk_buff *skb;
	int er;

	/*
	 * This works for seqpacket too. The receiver has ordered the queue for
	 * us! We do one quick check first though
	 */

	lock_sock(sk);
	if (sk->sk_state != TCP_ESTABLISHED) {
		release_sock(sk);
		return -ENOTCONN;
	}

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, flags & MSG_DONTWAIT, &er)) == NULL) {
		release_sock(sk);
		return er;
	}

	skb_reset_transport_header(skb);
	copied     = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	if (sax != NULL) {
		sax->sax25_family = AF_NETROM;
		skb_copy_from_linear_data_offset(skb, 7, sax->sax25_call.ax25_call,
			      AX25_ADDR_LEN);
	}

	msg->msg_namelen = sizeof(*sax);

	skb_free_datagram(sk, skb);

	release_sock(sk);
	return copied;
}


static int nr_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *)arg;
	int ret;

	switch (cmd) {
	case TIOCOUTQ: {
		long amount;

		lock_sock(sk);
		amount = sk->sk_sndbuf - atomic_read(&sk->sk_wmem_alloc);
		if (amount < 0)
			amount = 0;
		release_sock(sk);
		return put_user(amount, (int __user *)argp);
	}

	case TIOCINQ: {
		struct sk_buff *skb;
		long amount = 0L;

		lock_sock(sk);
		/* These two are safe on a single CPU system as only user tasks fiddle here */
		if ((skb = skb_peek(&sk->sk_receive_queue)) != NULL)
			amount = skb->len;
		release_sock(sk);
		return put_user(amount, (int __user *)argp);
	}

	case SIOCGSTAMP:
		lock_sock(sk);
		ret = sock_get_timestamp(sk, argp);
		release_sock(sk);
		return ret;

	case SIOCGSTAMPNS:
		lock_sock(sk);
		ret = sock_get_timestampns(sk, argp);
		release_sock(sk);
		return ret;

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
	case SIOCNRDECOBS:
		if (!capable(CAP_NET_ADMIN)) return -EPERM;
		return nr_rt_ioctl(cmd, argp);

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

#ifdef CONFIG_PROC_FS

static void *nr_info_start(struct seq_file *seq, loff_t *pos)
{
	struct sock *s;
	struct hlist_node *node;
	int i = 1;

	spin_lock_bh(&nr_list_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	sk_for_each(s, node, &nr_list) {
		if (i == *pos)
			return s;
		++i;
	}
	return NULL;
}

static void *nr_info_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return (v == SEQ_START_TOKEN) ? sk_head(&nr_list)
		: sk_next((struct sock *)v);
}

static void nr_info_stop(struct seq_file *seq, void *v)
{
	spin_unlock_bh(&nr_list_lock);
}

static int nr_info_show(struct seq_file *seq, void *v)
{
	struct sock *s = v;
	struct net_device *dev;
	struct nr_sock *nr;
	const char *devname;
	char buf[11];

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
"user_addr dest_node src_node  dev    my  your  st  vs  vr  va    t1     t2     t4      idle   n2  wnd Snd-Q Rcv-Q inode\n");

	else {

		bh_lock_sock(s);
		nr = nr_sk(s);

		if ((dev = nr->device) == NULL)
			devname = "???";
		else
			devname = dev->name;

		seq_printf(seq, "%-9s ", ax2asc(buf, &nr->user_addr));
		seq_printf(seq, "%-9s ", ax2asc(buf, &nr->dest_addr));
		seq_printf(seq,
"%-9s %-3s  %02X/%02X %02X/%02X %2d %3d %3d %3d %3lu/%03lu %2lu/%02lu %3lu/%03lu %3lu/%03lu %2d/%02d %3d %5d %5d %ld\n",
			ax2asc(buf, &nr->source_addr),
			devname,
			nr->my_index,
			nr->my_id,
			nr->your_index,
			nr->your_id,
			nr->state,
			nr->vs,
			nr->vr,
			nr->va,
			ax25_display_timer(&nr->t1timer) / HZ,
			nr->t1 / HZ,
			ax25_display_timer(&nr->t2timer) / HZ,
			nr->t2 / HZ,
			ax25_display_timer(&nr->t4timer) / HZ,
			nr->t4 / HZ,
			ax25_display_timer(&nr->idletimer) / (60 * HZ),
			nr->idle / (60 * HZ),
			nr->n2count,
			nr->n2,
			nr->window,
			atomic_read(&s->sk_wmem_alloc),
			atomic_read(&s->sk_rmem_alloc),
			s->sk_socket ? SOCK_INODE(s->sk_socket)->i_ino : 0L);

		bh_unlock_sock(s);
	}
	return 0;
}

static struct seq_operations nr_info_seqops = {
	.start = nr_info_start,
	.next = nr_info_next,
	.stop = nr_info_stop,
	.show = nr_info_show,
};

static int nr_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &nr_info_seqops);
}

static const struct file_operations nr_info_fops = {
	.owner = THIS_MODULE,
	.open = nr_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif	/* CONFIG_PROC_FS */

static struct net_proto_family nr_family_ops = {
	.family		=	PF_NETROM,
	.create		=	nr_create,
	.owner		=	THIS_MODULE,
};

static const struct proto_ops nr_proto_ops = {
	.family		=	PF_NETROM,
	.owner		=	THIS_MODULE,
	.release	=	nr_release,
	.bind		=	nr_bind,
	.connect	=	nr_connect,
	.socketpair	=	sock_no_socketpair,
	.accept		=	nr_accept,
	.getname	=	nr_getname,
	.poll		=	datagram_poll,
	.ioctl		=	nr_ioctl,
	.listen		=	nr_listen,
	.shutdown	=	sock_no_shutdown,
	.setsockopt	=	nr_setsockopt,
	.getsockopt	=	nr_getsockopt,
	.sendmsg	=	nr_sendmsg,
	.recvmsg	=	nr_recvmsg,
	.mmap		=	sock_no_mmap,
	.sendpage	=	sock_no_sendpage,
};

static struct notifier_block nr_dev_notifier = {
	.notifier_call	=	nr_device_event,
};

static struct net_device **dev_nr;

static struct ax25_protocol nr_pid = {
	.pid	= AX25_P_NETROM,
	.func	= nr_route_frame
};

static struct ax25_linkfail nr_linkfail_notifier = {
	.func	= nr_link_failed,
};

static int __init nr_proto_init(void)
{
	int i;
	int rc = proto_register(&nr_proto, 0);

	if (rc != 0)
		goto out;

	if (nr_ndevs > 0x7fffffff/sizeof(struct net_device *)) {
		printk(KERN_ERR "NET/ROM: nr_proto_init - nr_ndevs parameter to large\n");
		return -1;
	}

	dev_nr = kzalloc(nr_ndevs * sizeof(struct net_device *), GFP_KERNEL);
	if (dev_nr == NULL) {
		printk(KERN_ERR "NET/ROM: nr_proto_init - unable to allocate device array\n");
		return -1;
	}

	for (i = 0; i < nr_ndevs; i++) {
		char name[IFNAMSIZ];
		struct net_device *dev;

		sprintf(name, "nr%d", i);
		dev = alloc_netdev(sizeof(struct nr_private), name, nr_setup);
		if (!dev) {
			printk(KERN_ERR "NET/ROM: nr_proto_init - unable to allocate device structure\n");
			goto fail;
		}

		dev->base_addr = i;
		if (register_netdev(dev)) {
			printk(KERN_ERR "NET/ROM: nr_proto_init - unable to register network device\n");
			free_netdev(dev);
			goto fail;
		}
		lockdep_set_class(&dev->_xmit_lock, &nr_netdev_xmit_lock_key);
		dev_nr[i] = dev;
	}

	if (sock_register(&nr_family_ops)) {
		printk(KERN_ERR "NET/ROM: nr_proto_init - unable to register socket family\n");
		goto fail;
	}

	register_netdevice_notifier(&nr_dev_notifier);

	ax25_register_pid(&nr_pid);
	ax25_linkfail_register(&nr_linkfail_notifier);

#ifdef CONFIG_SYSCTL
	nr_register_sysctl();
#endif

	nr_loopback_init();

	proc_net_fops_create("nr", S_IRUGO, &nr_info_fops);
	proc_net_fops_create("nr_neigh", S_IRUGO, &nr_neigh_fops);
	proc_net_fops_create("nr_nodes", S_IRUGO, &nr_nodes_fops);
out:
	return rc;
fail:
	while (--i >= 0) {
		unregister_netdev(dev_nr[i]);
		free_netdev(dev_nr[i]);
	}
	kfree(dev_nr);
	proto_unregister(&nr_proto);
	rc = -1;
	goto out;
}

module_init(nr_proto_init);

module_param(nr_ndevs, int, 0);
MODULE_PARM_DESC(nr_ndevs, "number of NET/ROM devices");

MODULE_AUTHOR("Jonathan Naylor G4KLX <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The amateur radio NET/ROM network and transport layer protocol");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_NETROM);

static void __exit nr_exit(void)
{
	int i;

	proc_net_remove("nr");
	proc_net_remove("nr_neigh");
	proc_net_remove("nr_nodes");
	nr_loopback_clear();

	nr_rt_free();

#ifdef CONFIG_SYSCTL
	nr_unregister_sysctl();
#endif

	ax25_linkfail_release(&nr_linkfail_notifier);
	ax25_protocol_release(AX25_P_NETROM);

	unregister_netdevice_notifier(&nr_dev_notifier);

	sock_unregister(PF_NETROM);

	for (i = 0; i < nr_ndevs; i++) {
		struct net_device *dev = dev_nr[i];
		if (dev) {
			unregister_netdev(dev);
			free_netdev(dev);
		}
	}

	kfree(dev_nr);
	proto_unregister(&nr_proto);
}
module_exit(nr_exit);
