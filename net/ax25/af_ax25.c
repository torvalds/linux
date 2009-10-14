/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Darryl Miles G7LED (dlm@g7led.demon.co.uk)
 * Copyright (C) Steven Whitehouse GW7RRM (stevew@acm.org)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 * Copyright (C) Hans-Joachim Hetscher DD8NE (dd8ne@bnv-bamberg.de)
 * Copyright (C) Hans Alblas PE1AYX (hans@esrac.ele.tue.nl)
 * Copyright (C) Frederic Rible F1OAT (frible@teaser.fr)
 */
#include <linux/capability.h>
#include <linux/module.h>
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
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/netfilter.h>
#include <linux/sysctl.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <net/net_namespace.h>
#include <net/tcp_states.h>
#include <net/ip.h>
#include <net/arp.h>



HLIST_HEAD(ax25_list);
DEFINE_SPINLOCK(ax25_list_lock);

static const struct proto_ops ax25_proto_ops;

static void ax25_free_sock(struct sock *sk)
{
	ax25_cb_put(ax25_sk(sk));
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void ax25_cb_del(ax25_cb *ax25)
{
	if (!hlist_unhashed(&ax25->ax25_node)) {
		spin_lock_bh(&ax25_list_lock);
		hlist_del_init(&ax25->ax25_node);
		spin_unlock_bh(&ax25_list_lock);
		ax25_cb_put(ax25);
	}
}

/*
 *	Kill all bound sockets on a dropped device.
 */
static void ax25_kill_by_device(struct net_device *dev)
{
	ax25_dev *ax25_dev;
	ax25_cb *s;
	struct hlist_node *node;

	if ((ax25_dev = ax25_dev_ax25dev(dev)) == NULL)
		return;

	spin_lock_bh(&ax25_list_lock);
again:
	ax25_for_each(s, node, &ax25_list) {
		if (s->ax25_dev == ax25_dev) {
			s->ax25_dev = NULL;
			spin_unlock_bh(&ax25_list_lock);
			ax25_disconnect(s, ENETUNREACH);
			spin_lock_bh(&ax25_list_lock);

			/* The entry could have been deleted from the
			 * list meanwhile and thus the next pointer is
			 * no longer valid.  Play it safe and restart
			 * the scan.  Forward progress is ensured
			 * because we set s->ax25_dev to NULL and we
			 * are never passed a NULL 'dev' argument.
			 */
			goto again;
		}
	}
	spin_unlock_bh(&ax25_list_lock);
}

/*
 *	Handle device status changes.
 */
static int ax25_device_event(struct notifier_block *this, unsigned long event,
	void *ptr)
{
	struct net_device *dev = (struct net_device *)ptr;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	/* Reject non AX.25 devices */
	if (dev->type != ARPHRD_AX25)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		ax25_dev_device_up(dev);
		break;
	case NETDEV_DOWN:
		ax25_kill_by_device(dev);
		ax25_rt_device_down(dev);
		ax25_dev_device_down(dev);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
void ax25_cb_add(ax25_cb *ax25)
{
	spin_lock_bh(&ax25_list_lock);
	ax25_cb_hold(ax25);
	hlist_add_head(&ax25->ax25_node, &ax25_list);
	spin_unlock_bh(&ax25_list_lock);
}

/*
 *	Find a socket that wants to accept the SABM we have just
 *	received.
 */
struct sock *ax25_find_listener(ax25_address *addr, int digi,
	struct net_device *dev, int type)
{
	ax25_cb *s;
	struct hlist_node *node;

	spin_lock(&ax25_list_lock);
	ax25_for_each(s, node, &ax25_list) {
		if ((s->iamdigi && !digi) || (!s->iamdigi && digi))
			continue;
		if (s->sk && !ax25cmp(&s->source_addr, addr) &&
		    s->sk->sk_type == type && s->sk->sk_state == TCP_LISTEN) {
			/* If device is null we match any device */
			if (s->ax25_dev == NULL || s->ax25_dev->dev == dev) {
				sock_hold(s->sk);
				spin_unlock(&ax25_list_lock);
				return s->sk;
			}
		}
	}
	spin_unlock(&ax25_list_lock);

	return NULL;
}

/*
 *	Find an AX.25 socket given both ends.
 */
struct sock *ax25_get_socket(ax25_address *my_addr, ax25_address *dest_addr,
	int type)
{
	struct sock *sk = NULL;
	ax25_cb *s;
	struct hlist_node *node;

	spin_lock(&ax25_list_lock);
	ax25_for_each(s, node, &ax25_list) {
		if (s->sk && !ax25cmp(&s->source_addr, my_addr) &&
		    !ax25cmp(&s->dest_addr, dest_addr) &&
		    s->sk->sk_type == type) {
			sk = s->sk;
			sock_hold(sk);
			break;
		}
	}

	spin_unlock(&ax25_list_lock);

	return sk;
}

/*
 *	Find an AX.25 control block given both ends. It will only pick up
 *	floating AX.25 control blocks or non Raw socket bound control blocks.
 */
ax25_cb *ax25_find_cb(ax25_address *src_addr, ax25_address *dest_addr,
	ax25_digi *digi, struct net_device *dev)
{
	ax25_cb *s;
	struct hlist_node *node;

	spin_lock_bh(&ax25_list_lock);
	ax25_for_each(s, node, &ax25_list) {
		if (s->sk && s->sk->sk_type != SOCK_SEQPACKET)
			continue;
		if (s->ax25_dev == NULL)
			continue;
		if (ax25cmp(&s->source_addr, src_addr) == 0 && ax25cmp(&s->dest_addr, dest_addr) == 0 && s->ax25_dev->dev == dev) {
			if (digi != NULL && digi->ndigi != 0) {
				if (s->digipeat == NULL)
					continue;
				if (ax25digicmp(s->digipeat, digi) != 0)
					continue;
			} else {
				if (s->digipeat != NULL && s->digipeat->ndigi != 0)
					continue;
			}
			ax25_cb_hold(s);
			spin_unlock_bh(&ax25_list_lock);

			return s;
		}
	}
	spin_unlock_bh(&ax25_list_lock);

	return NULL;
}

EXPORT_SYMBOL(ax25_find_cb);

void ax25_send_to_raw(ax25_address *addr, struct sk_buff *skb, int proto)
{
	ax25_cb *s;
	struct sk_buff *copy;
	struct hlist_node *node;

	spin_lock(&ax25_list_lock);
	ax25_for_each(s, node, &ax25_list) {
		if (s->sk != NULL && ax25cmp(&s->source_addr, addr) == 0 &&
		    s->sk->sk_type == SOCK_RAW &&
		    s->sk->sk_protocol == proto &&
		    s->ax25_dev->dev == skb->dev &&
		    atomic_read(&s->sk->sk_rmem_alloc) <= s->sk->sk_rcvbuf) {
			if ((copy = skb_clone(skb, GFP_ATOMIC)) == NULL)
				continue;
			if (sock_queue_rcv_skb(s->sk, copy) != 0)
				kfree_skb(copy);
		}
	}
	spin_unlock(&ax25_list_lock);
}

/*
 *	Deferred destroy.
 */
void ax25_destroy_socket(ax25_cb *);

/*
 *	Handler for deferred kills.
 */
static void ax25_destroy_timer(unsigned long data)
{
	ax25_cb *ax25=(ax25_cb *)data;
	struct sock *sk;

	sk=ax25->sk;

	bh_lock_sock(sk);
	sock_hold(sk);
	ax25_destroy_socket(ax25);
	bh_unlock_sock(sk);
	sock_put(sk);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself
 *	against interrupt users but doesn't worry about being called during
 *	work. Once it is removed from the queue no interrupt or bottom half
 *	will touch it and we are (fairly 8-) ) safe.
 */
void ax25_destroy_socket(ax25_cb *ax25)
{
	struct sk_buff *skb;

	ax25_cb_del(ax25);

	ax25_stop_heartbeat(ax25);
	ax25_stop_t1timer(ax25);
	ax25_stop_t2timer(ax25);
	ax25_stop_t3timer(ax25);
	ax25_stop_idletimer(ax25);

	ax25_clear_queues(ax25);	/* Flush the queues */

	if (ax25->sk != NULL) {
		while ((skb = skb_dequeue(&ax25->sk->sk_receive_queue)) != NULL) {
			if (skb->sk != ax25->sk) {
				/* A pending connection */
				ax25_cb *sax25 = ax25_sk(skb->sk);

				/* Queue the unaccepted socket for death */
				sock_orphan(skb->sk);

				/* 9A4GL: hack to release unaccepted sockets */
				skb->sk->sk_state = TCP_LISTEN;

				ax25_start_heartbeat(sax25);
				sax25->state = AX25_STATE_0;
			}

			kfree_skb(skb);
		}
		skb_queue_purge(&ax25->sk->sk_write_queue);
	}

	if (ax25->sk != NULL) {
		if (sk_has_allocations(ax25->sk)) {
			/* Defer: outstanding buffers */
			setup_timer(&ax25->dtimer, ax25_destroy_timer,
					(unsigned long)ax25);
			ax25->dtimer.expires  = jiffies + 2 * HZ;
			add_timer(&ax25->dtimer);
		} else {
			struct sock *sk=ax25->sk;
			ax25->sk=NULL;
			sock_put(sk);
		}
	} else {
		ax25_cb_put(ax25);
	}
}

/*
 * dl1bke 960311: set parameters for existing AX.25 connections,
 *		  includes a KILL command to abort any connection.
 *		  VERY useful for debugging ;-)
 */
static int ax25_ctl_ioctl(const unsigned int cmd, void __user *arg)
{
	struct ax25_ctl_struct ax25_ctl;
	ax25_digi digi;
	ax25_dev *ax25_dev;
	ax25_cb *ax25;
	unsigned int k;
	int ret = 0;

	if (copy_from_user(&ax25_ctl, arg, sizeof(ax25_ctl)))
		return -EFAULT;

	if ((ax25_dev = ax25_addr_ax25dev(&ax25_ctl.port_addr)) == NULL)
		return -ENODEV;

	if (ax25_ctl.digi_count > AX25_MAX_DIGIS)
		return -EINVAL;

	if (ax25_ctl.arg > ULONG_MAX / HZ && ax25_ctl.cmd != AX25_KILL)
		return -EINVAL;

	digi.ndigi = ax25_ctl.digi_count;
	for (k = 0; k < digi.ndigi; k++)
		digi.calls[k] = ax25_ctl.digi_addr[k];

	if ((ax25 = ax25_find_cb(&ax25_ctl.source_addr, &ax25_ctl.dest_addr, &digi, ax25_dev->dev)) == NULL)
		return -ENOTCONN;

	switch (ax25_ctl.cmd) {
	case AX25_KILL:
		ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
#ifdef CONFIG_AX25_DAMA_SLAVE
		if (ax25_dev->dama.slave && ax25->ax25_dev->values[AX25_VALUES_PROTOCOL] == AX25_PROTO_DAMA_SLAVE)
			ax25_dama_off(ax25);
#endif
		ax25_disconnect(ax25, ENETRESET);
		break;

	case AX25_WINDOW:
		if (ax25->modulus == AX25_MODULUS) {
			if (ax25_ctl.arg < 1 || ax25_ctl.arg > 7)
				goto einval_put;
		} else {
			if (ax25_ctl.arg < 1 || ax25_ctl.arg > 63)
				goto einval_put;
		}
		ax25->window = ax25_ctl.arg;
		break;

	case AX25_T1:
		if (ax25_ctl.arg < 1)
			goto einval_put;
		ax25->rtt = (ax25_ctl.arg * HZ) / 2;
		ax25->t1  = ax25_ctl.arg * HZ;
		break;

	case AX25_T2:
		if (ax25_ctl.arg < 1)
			goto einval_put;
		ax25->t2 = ax25_ctl.arg * HZ;
		break;

	case AX25_N2:
		if (ax25_ctl.arg < 1 || ax25_ctl.arg > 31)
			goto einval_put;
		ax25->n2count = 0;
		ax25->n2 = ax25_ctl.arg;
		break;

	case AX25_T3:
		ax25->t3 = ax25_ctl.arg * HZ;
		break;

	case AX25_IDLE:
		ax25->idle = ax25_ctl.arg * 60 * HZ;
		break;

	case AX25_PACLEN:
		if (ax25_ctl.arg < 16 || ax25_ctl.arg > 65535)
			goto einval_put;
		ax25->paclen = ax25_ctl.arg;
		break;

	default:
		goto einval_put;
	  }

out_put:
	ax25_cb_put(ax25);
	return ret;

einval_put:
	ret = -EINVAL;
	goto out_put;
}

static void ax25_fillin_cb_from_dev(ax25_cb *ax25, ax25_dev *ax25_dev)
{
	ax25->rtt     = msecs_to_jiffies(ax25_dev->values[AX25_VALUES_T1]) / 2;
	ax25->t1      = msecs_to_jiffies(ax25_dev->values[AX25_VALUES_T1]);
	ax25->t2      = msecs_to_jiffies(ax25_dev->values[AX25_VALUES_T2]);
	ax25->t3      = msecs_to_jiffies(ax25_dev->values[AX25_VALUES_T3]);
	ax25->n2      = ax25_dev->values[AX25_VALUES_N2];
	ax25->paclen  = ax25_dev->values[AX25_VALUES_PACLEN];
	ax25->idle    = msecs_to_jiffies(ax25_dev->values[AX25_VALUES_IDLE]);
	ax25->backoff = ax25_dev->values[AX25_VALUES_BACKOFF];

	if (ax25_dev->values[AX25_VALUES_AXDEFMODE]) {
		ax25->modulus = AX25_EMODULUS;
		ax25->window  = ax25_dev->values[AX25_VALUES_EWINDOW];
	} else {
		ax25->modulus = AX25_MODULUS;
		ax25->window  = ax25_dev->values[AX25_VALUES_WINDOW];
	}
}

/*
 *	Fill in a created AX.25 created control block with the default
 *	values for a particular device.
 */
void ax25_fillin_cb(ax25_cb *ax25, ax25_dev *ax25_dev)
{
	ax25->ax25_dev = ax25_dev;

	if (ax25->ax25_dev != NULL) {
		ax25_fillin_cb_from_dev(ax25, ax25_dev);
		return;
	}

	/*
	 * No device, use kernel / AX.25 spec default values
	 */
	ax25->rtt     = msecs_to_jiffies(AX25_DEF_T1) / 2;
	ax25->t1      = msecs_to_jiffies(AX25_DEF_T1);
	ax25->t2      = msecs_to_jiffies(AX25_DEF_T2);
	ax25->t3      = msecs_to_jiffies(AX25_DEF_T3);
	ax25->n2      = AX25_DEF_N2;
	ax25->paclen  = AX25_DEF_PACLEN;
	ax25->idle    = msecs_to_jiffies(AX25_DEF_IDLE);
	ax25->backoff = AX25_DEF_BACKOFF;

	if (AX25_DEF_AXDEFMODE) {
		ax25->modulus = AX25_EMODULUS;
		ax25->window  = AX25_DEF_EWINDOW;
	} else {
		ax25->modulus = AX25_MODULUS;
		ax25->window  = AX25_DEF_WINDOW;
	}
}

/*
 * Create an empty AX.25 control block.
 */
ax25_cb *ax25_create_cb(void)
{
	ax25_cb *ax25;

	if ((ax25 = kzalloc(sizeof(*ax25), GFP_ATOMIC)) == NULL)
		return NULL;

	atomic_set(&ax25->refcount, 1);

	skb_queue_head_init(&ax25->write_queue);
	skb_queue_head_init(&ax25->frag_queue);
	skb_queue_head_init(&ax25->ack_queue);
	skb_queue_head_init(&ax25->reseq_queue);

	ax25_setup_timers(ax25);

	ax25_fillin_cb(ax25, NULL);

	ax25->state = AX25_STATE_0;

	return ax25;
}

/*
 *	Handling for system calls applied via the various interfaces to an
 *	AX25 socket object
 */

static int ax25_setsockopt(struct socket *sock, int level, int optname,
	char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	ax25_cb *ax25;
	struct net_device *dev;
	char devname[IFNAMSIZ];
	int opt, res = 0;

	if (level != SOL_AX25)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(opt, (int __user *)optval))
		return -EFAULT;

	lock_sock(sk);
	ax25 = ax25_sk(sk);

	switch (optname) {
	case AX25_WINDOW:
		if (ax25->modulus == AX25_MODULUS) {
			if (opt < 1 || opt > 7) {
				res = -EINVAL;
				break;
			}
		} else {
			if (opt < 1 || opt > 63) {
				res = -EINVAL;
				break;
			}
		}
		ax25->window = opt;
		break;

	case AX25_T1:
		if (opt < 1) {
			res = -EINVAL;
			break;
		}
		ax25->rtt = (opt * HZ) >> 1;
		ax25->t1  = opt * HZ;
		break;

	case AX25_T2:
		if (opt < 1) {
			res = -EINVAL;
			break;
		}
		ax25->t2 = opt * HZ;
		break;

	case AX25_N2:
		if (opt < 1 || opt > 31) {
			res = -EINVAL;
			break;
		}
		ax25->n2 = opt;
		break;

	case AX25_T3:
		if (opt < 1) {
			res = -EINVAL;
			break;
		}
		ax25->t3 = opt * HZ;
		break;

	case AX25_IDLE:
		if (opt < 0) {
			res = -EINVAL;
			break;
		}
		ax25->idle = opt * 60 * HZ;
		break;

	case AX25_BACKOFF:
		if (opt < 0 || opt > 2) {
			res = -EINVAL;
			break;
		}
		ax25->backoff = opt;
		break;

	case AX25_EXTSEQ:
		ax25->modulus = opt ? AX25_EMODULUS : AX25_MODULUS;
		break;

	case AX25_PIDINCL:
		ax25->pidincl = opt ? 1 : 0;
		break;

	case AX25_IAMDIGI:
		ax25->iamdigi = opt ? 1 : 0;
		break;

	case AX25_PACLEN:
		if (opt < 16 || opt > 65535) {
			res = -EINVAL;
			break;
		}
		ax25->paclen = opt;
		break;

	case SO_BINDTODEVICE:
		if (optlen > IFNAMSIZ)
			optlen = IFNAMSIZ;

		if (copy_from_user(devname, optval, optlen)) {
			res = -EFAULT;
			break;
		}

		if (sk->sk_type == SOCK_SEQPACKET &&
		   (sock->state != SS_UNCONNECTED ||
		    sk->sk_state == TCP_LISTEN)) {
			res = -EADDRNOTAVAIL;
			break;
		}

		dev = dev_get_by_name(&init_net, devname);
		if (!dev) {
			res = -ENODEV;
			break;
		}

		ax25->ax25_dev = ax25_dev_ax25dev(dev);
		ax25_fillin_cb(ax25, ax25->ax25_dev);
		dev_put(dev);
		break;

	default:
		res = -ENOPROTOOPT;
	}
	release_sock(sk);

	return res;
}

static int ax25_getsockopt(struct socket *sock, int level, int optname,
	char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	ax25_cb *ax25;
	struct ax25_dev *ax25_dev;
	char devname[IFNAMSIZ];
	void *valptr;
	int val = 0;
	int maxlen, length;

	if (level != SOL_AX25)
		return -ENOPROTOOPT;

	if (get_user(maxlen, optlen))
		return -EFAULT;

	if (maxlen < 1)
		return -EFAULT;

	valptr = (void *) &val;
	length = min_t(unsigned int, maxlen, sizeof(int));

	lock_sock(sk);
	ax25 = ax25_sk(sk);

	switch (optname) {
	case AX25_WINDOW:
		val = ax25->window;
		break;

	case AX25_T1:
		val = ax25->t1 / HZ;
		break;

	case AX25_T2:
		val = ax25->t2 / HZ;
		break;

	case AX25_N2:
		val = ax25->n2;
		break;

	case AX25_T3:
		val = ax25->t3 / HZ;
		break;

	case AX25_IDLE:
		val = ax25->idle / (60 * HZ);
		break;

	case AX25_BACKOFF:
		val = ax25->backoff;
		break;

	case AX25_EXTSEQ:
		val = (ax25->modulus == AX25_EMODULUS);
		break;

	case AX25_PIDINCL:
		val = ax25->pidincl;
		break;

	case AX25_IAMDIGI:
		val = ax25->iamdigi;
		break;

	case AX25_PACLEN:
		val = ax25->paclen;
		break;

	case SO_BINDTODEVICE:
		ax25_dev = ax25->ax25_dev;

		if (ax25_dev != NULL && ax25_dev->dev != NULL) {
			strlcpy(devname, ax25_dev->dev->name, sizeof(devname));
			length = strlen(devname) + 1;
		} else {
			*devname = '\0';
			length = 1;
		}

		valptr = (void *) devname;
		break;

	default:
		release_sock(sk);
		return -ENOPROTOOPT;
	}
	release_sock(sk);

	if (put_user(length, optlen))
		return -EFAULT;

	return copy_to_user(optval, valptr, length) ? -EFAULT : 0;
}

static int ax25_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int res = 0;

	lock_sock(sk);
	if (sk->sk_type == SOCK_SEQPACKET && sk->sk_state != TCP_LISTEN) {
		sk->sk_max_ack_backlog = backlog;
		sk->sk_state           = TCP_LISTEN;
		goto out;
	}
	res = -EOPNOTSUPP;

out:
	release_sock(sk);

	return res;
}

/*
 * XXX: when creating ax25_sock we should update the .obj_size setting
 * below.
 */
static struct proto ax25_proto = {
	.name	  = "AX25",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct sock),
};

static int ax25_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;
	ax25_cb *ax25;

	if (net != &init_net)
		return -EAFNOSUPPORT;

	switch (sock->type) {
	case SOCK_DGRAM:
		if (protocol == 0 || protocol == PF_AX25)
			protocol = AX25_P_TEXT;
		break;

	case SOCK_SEQPACKET:
		switch (protocol) {
		case 0:
		case PF_AX25:	/* For CLX */
			protocol = AX25_P_TEXT;
			break;
		case AX25_P_SEGMENT:
#ifdef CONFIG_INET
		case AX25_P_ARP:
		case AX25_P_IP:
#endif
#ifdef CONFIG_NETROM
		case AX25_P_NETROM:
#endif
#ifdef CONFIG_ROSE
		case AX25_P_ROSE:
#endif
			return -ESOCKTNOSUPPORT;
#ifdef CONFIG_NETROM_MODULE
		case AX25_P_NETROM:
			if (ax25_protocol_is_registered(AX25_P_NETROM))
				return -ESOCKTNOSUPPORT;
#endif
#ifdef CONFIG_ROSE_MODULE
		case AX25_P_ROSE:
			if (ax25_protocol_is_registered(AX25_P_ROSE))
				return -ESOCKTNOSUPPORT;
#endif
		default:
			break;
		}
		break;

	case SOCK_RAW:
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	sk = sk_alloc(net, PF_AX25, GFP_ATOMIC, &ax25_proto);
	if (sk == NULL)
		return -ENOMEM;

	ax25 = sk->sk_protinfo = ax25_create_cb();
	if (!ax25) {
		sk_free(sk);
		return -ENOMEM;
	}

	sock_init_data(sock, sk);

	sk->sk_destruct = ax25_free_sock;
	sock->ops    = &ax25_proto_ops;
	sk->sk_protocol = protocol;

	ax25->sk    = sk;

	return 0;
}

struct sock *ax25_make_new(struct sock *osk, struct ax25_dev *ax25_dev)
{
	struct sock *sk;
	ax25_cb *ax25, *oax25;

	sk = sk_alloc(sock_net(osk), PF_AX25, GFP_ATOMIC,	osk->sk_prot);
	if (sk == NULL)
		return NULL;

	if ((ax25 = ax25_create_cb()) == NULL) {
		sk_free(sk);
		return NULL;
	}

	switch (osk->sk_type) {
	case SOCK_DGRAM:
		break;
	case SOCK_SEQPACKET:
		break;
	default:
		sk_free(sk);
		ax25_cb_put(ax25);
		return NULL;
	}

	sock_init_data(NULL, sk);

	sk->sk_type     = osk->sk_type;
	sk->sk_priority = osk->sk_priority;
	sk->sk_protocol = osk->sk_protocol;
	sk->sk_rcvbuf   = osk->sk_rcvbuf;
	sk->sk_sndbuf   = osk->sk_sndbuf;
	sk->sk_state    = TCP_ESTABLISHED;
	sock_copy_flags(sk, osk);

	oax25 = ax25_sk(osk);

	ax25->modulus = oax25->modulus;
	ax25->backoff = oax25->backoff;
	ax25->pidincl = oax25->pidincl;
	ax25->iamdigi = oax25->iamdigi;
	ax25->rtt     = oax25->rtt;
	ax25->t1      = oax25->t1;
	ax25->t2      = oax25->t2;
	ax25->t3      = oax25->t3;
	ax25->n2      = oax25->n2;
	ax25->idle    = oax25->idle;
	ax25->paclen  = oax25->paclen;
	ax25->window  = oax25->window;

	ax25->ax25_dev    = ax25_dev;
	ax25->source_addr = oax25->source_addr;

	if (oax25->digipeat != NULL) {
		ax25->digipeat = kmemdup(oax25->digipeat, sizeof(ax25_digi),
					 GFP_ATOMIC);
		if (ax25->digipeat == NULL) {
			sk_free(sk);
			ax25_cb_put(ax25);
			return NULL;
		}
	}

	sk->sk_protinfo = ax25;
	sk->sk_destruct = ax25_free_sock;
	ax25->sk    = sk;

	return sk;
}

static int ax25_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	ax25_cb *ax25;

	if (sk == NULL)
		return 0;

	sock_hold(sk);
	sock_orphan(sk);
	lock_sock(sk);
	ax25 = ax25_sk(sk);

	if (sk->sk_type == SOCK_SEQPACKET) {
		switch (ax25->state) {
		case AX25_STATE_0:
			release_sock(sk);
			ax25_disconnect(ax25, 0);
			lock_sock(sk);
			ax25_destroy_socket(ax25);
			break;

		case AX25_STATE_1:
		case AX25_STATE_2:
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			release_sock(sk);
			ax25_disconnect(ax25, 0);
			lock_sock(sk);
			ax25_destroy_socket(ax25);
			break;

		case AX25_STATE_3:
		case AX25_STATE_4:
			ax25_clear_queues(ax25);
			ax25->n2count = 0;

			switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
			case AX25_PROTO_STD_SIMPLEX:
			case AX25_PROTO_STD_DUPLEX:
				ax25_send_control(ax25,
						  AX25_DISC,
						  AX25_POLLON,
						  AX25_COMMAND);
				ax25_stop_t2timer(ax25);
				ax25_stop_t3timer(ax25);
				ax25_stop_idletimer(ax25);
				break;
#ifdef CONFIG_AX25_DAMA_SLAVE
			case AX25_PROTO_DAMA_SLAVE:
				ax25_stop_t3timer(ax25);
				ax25_stop_idletimer(ax25);
				break;
#endif
			}
			ax25_calculate_t1(ax25);
			ax25_start_t1timer(ax25);
			ax25->state = AX25_STATE_2;
			sk->sk_state                = TCP_CLOSE;
			sk->sk_shutdown            |= SEND_SHUTDOWN;
			sk->sk_state_change(sk);
			sock_set_flag(sk, SOCK_DESTROY);
			break;

		default:
			break;
		}
	} else {
		sk->sk_state     = TCP_CLOSE;
		sk->sk_shutdown |= SEND_SHUTDOWN;
		sk->sk_state_change(sk);
		ax25_destroy_socket(ax25);
	}

	sock->sk   = NULL;
	release_sock(sk);
	sock_put(sk);

	return 0;
}

/*
 *	We support a funny extension here so you can (as root) give any callsign
 *	digipeated via a local address as source. This hack is obsolete now
 *	that we've implemented support for SO_BINDTODEVICE. It is however small
 *	and trivially backward compatible.
 */
static int ax25_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct full_sockaddr_ax25 *addr = (struct full_sockaddr_ax25 *)uaddr;
	ax25_dev *ax25_dev = NULL;
	ax25_uid_assoc *user;
	ax25_address call;
	ax25_cb *ax25;
	int err = 0;

	if (addr_len != sizeof(struct sockaddr_ax25) &&
	    addr_len != sizeof(struct full_sockaddr_ax25))
		/* support for old structure may go away some time
		 * ax25_bind(): uses old (6 digipeater) socket structure.
		 */
		if ((addr_len < sizeof(struct sockaddr_ax25) + sizeof(ax25_address) * 6) ||
		    (addr_len > sizeof(struct full_sockaddr_ax25)))
			return -EINVAL;

	if (addr->fsa_ax25.sax25_family != AF_AX25)
		return -EINVAL;

	user = ax25_findbyuid(current_euid());
	if (user) {
		call = user->call;
		ax25_uid_put(user);
	} else {
		if (ax25_uid_policy && !capable(CAP_NET_ADMIN))
			return -EACCES;

		call = addr->fsa_ax25.sax25_call;
	}

	lock_sock(sk);

	ax25 = ax25_sk(sk);
	if (!sock_flag(sk, SOCK_ZAPPED)) {
		err = -EINVAL;
		goto out;
	}

	ax25->source_addr = call;

	/*
	 * User already set interface with SO_BINDTODEVICE
	 */
	if (ax25->ax25_dev != NULL)
		goto done;

	if (addr_len > sizeof(struct sockaddr_ax25) && addr->fsa_ax25.sax25_ndigis == 1) {
		if (ax25cmp(&addr->fsa_digipeater[0], &null_ax25_address) != 0 &&
		    (ax25_dev = ax25_addr_ax25dev(&addr->fsa_digipeater[0])) == NULL) {
			err = -EADDRNOTAVAIL;
			goto out;
		}
	} else {
		if ((ax25_dev = ax25_addr_ax25dev(&addr->fsa_ax25.sax25_call)) == NULL) {
			err = -EADDRNOTAVAIL;
			goto out;
		}
	}

	if (ax25_dev != NULL)
		ax25_fillin_cb(ax25, ax25_dev);

done:
	ax25_cb_add(ax25);
	sock_reset_flag(sk, SOCK_ZAPPED);

out:
	release_sock(sk);

	return 0;
}

/*
 *	FIXME: nonblock behaviour looks like it may have a bug.
 */
static int __must_check ax25_connect(struct socket *sock,
	struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	ax25_cb *ax25 = ax25_sk(sk), *ax25t;
	struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)uaddr;
	ax25_digi *digi = NULL;
	int ct = 0, err = 0;

	/*
	 * some sanity checks. code further down depends on this
	 */

	if (addr_len == sizeof(struct sockaddr_ax25))
		/* support for this will go away in early 2.5.x
		 * ax25_connect(): uses obsolete socket structure
		 */
		;
	else if (addr_len != sizeof(struct full_sockaddr_ax25))
		/* support for old structure may go away some time
		 * ax25_connect(): uses old (6 digipeater) socket structure.
		 */
		if ((addr_len < sizeof(struct sockaddr_ax25) + sizeof(ax25_address) * 6) ||
		    (addr_len > sizeof(struct full_sockaddr_ax25)))
			return -EINVAL;


	if (fsa->fsa_ax25.sax25_family != AF_AX25)
		return -EINVAL;

	lock_sock(sk);

	/* deal with restarts */
	if (sock->state == SS_CONNECTING) {
		switch (sk->sk_state) {
		case TCP_SYN_SENT: /* still trying */
			err = -EINPROGRESS;
			goto out_release;

		case TCP_ESTABLISHED: /* connection established */
			sock->state = SS_CONNECTED;
			goto out_release;

		case TCP_CLOSE: /* connection refused */
			sock->state = SS_UNCONNECTED;
			err = -ECONNREFUSED;
			goto out_release;
		}
	}

	if (sk->sk_state == TCP_ESTABLISHED && sk->sk_type == SOCK_SEQPACKET) {
		err = -EISCONN;	/* No reconnect on a seqpacket socket */
		goto out_release;
	}

	sk->sk_state   = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	kfree(ax25->digipeat);
	ax25->digipeat = NULL;

	/*
	 *	Handle digi-peaters to be used.
	 */
	if (addr_len > sizeof(struct sockaddr_ax25) &&
	    fsa->fsa_ax25.sax25_ndigis != 0) {
		/* Valid number of digipeaters ? */
		if (fsa->fsa_ax25.sax25_ndigis < 1 || fsa->fsa_ax25.sax25_ndigis > AX25_MAX_DIGIS) {
			err = -EINVAL;
			goto out_release;
		}

		if ((digi = kmalloc(sizeof(ax25_digi), GFP_KERNEL)) == NULL) {
			err = -ENOBUFS;
			goto out_release;
		}

		digi->ndigi      = fsa->fsa_ax25.sax25_ndigis;
		digi->lastrepeat = -1;

		while (ct < fsa->fsa_ax25.sax25_ndigis) {
			if ((fsa->fsa_digipeater[ct].ax25_call[6] &
			     AX25_HBIT) && ax25->iamdigi) {
				digi->repeated[ct] = 1;
				digi->lastrepeat   = ct;
			} else {
				digi->repeated[ct] = 0;
			}
			digi->calls[ct] = fsa->fsa_digipeater[ct];
			ct++;
		}
	}

	/*
	 *	Must bind first - autobinding in this may or may not work. If
	 *	the socket is already bound, check to see if the device has
	 *	been filled in, error if it hasn't.
	 */
	if (sock_flag(sk, SOCK_ZAPPED)) {
		/* check if we can remove this feature. It is broken. */
		printk(KERN_WARNING "ax25_connect(): %s uses autobind, please contact jreuter@yaina.de\n",
			current->comm);
		if ((err = ax25_rt_autobind(ax25, &fsa->fsa_ax25.sax25_call)) < 0) {
			kfree(digi);
			goto out_release;
		}

		ax25_fillin_cb(ax25, ax25->ax25_dev);
		ax25_cb_add(ax25);
	} else {
		if (ax25->ax25_dev == NULL) {
			kfree(digi);
			err = -EHOSTUNREACH;
			goto out_release;
		}
	}

	if (sk->sk_type == SOCK_SEQPACKET &&
	    (ax25t=ax25_find_cb(&ax25->source_addr, &fsa->fsa_ax25.sax25_call, digi,
			 ax25->ax25_dev->dev))) {
		kfree(digi);
		err = -EADDRINUSE;		/* Already such a connection */
		ax25_cb_put(ax25t);
		goto out_release;
	}

	ax25->dest_addr = fsa->fsa_ax25.sax25_call;
	ax25->digipeat  = digi;

	/* First the easy one */
	if (sk->sk_type != SOCK_SEQPACKET) {
		sock->state = SS_CONNECTED;
		sk->sk_state   = TCP_ESTABLISHED;
		goto out_release;
	}

	/* Move to connecting socket, ax.25 lapb WAIT_UA.. */
	sock->state        = SS_CONNECTING;
	sk->sk_state          = TCP_SYN_SENT;

	switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
	case AX25_PROTO_STD_SIMPLEX:
	case AX25_PROTO_STD_DUPLEX:
		ax25_std_establish_data_link(ax25);
		break;

#ifdef CONFIG_AX25_DAMA_SLAVE
	case AX25_PROTO_DAMA_SLAVE:
		ax25->modulus = AX25_MODULUS;
		ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
		if (ax25->ax25_dev->dama.slave)
			ax25_ds_establish_data_link(ax25);
		else
			ax25_std_establish_data_link(ax25);
		break;
#endif
	}

	ax25->state = AX25_STATE_1;

	ax25_start_heartbeat(ax25);

	/* Now the loop */
	if (sk->sk_state != TCP_ESTABLISHED && (flags & O_NONBLOCK)) {
		err = -EINPROGRESS;
		goto out_release;
	}

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
		/* Not in ABM, not in WAIT_UA -> failed */
		sock->state = SS_UNCONNECTED;
		err = sock_error(sk);	/* Always set at this point */
		goto out_release;
	}

	sock->state = SS_CONNECTED;

	err = 0;
out_release:
	release_sock(sk);

	return err;
}

static int ax25_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sk_buff *skb;
	struct sock *newsk;
	DEFINE_WAIT(wait);
	struct sock *sk;
	int err = 0;

	if (sock->state != SS_UNCONNECTED)
		return -EINVAL;

	if ((sk = sock->sk) == NULL)
		return -EINVAL;

	lock_sock(sk);
	if (sk->sk_type != SOCK_SEQPACKET) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (sk->sk_state != TCP_LISTEN) {
		err = -EINVAL;
		goto out;
	}

	/*
	 *	The read queue this time is holding sockets ready to use
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
		goto out;

	newsk		 = skb->sk;
	sock_graft(newsk, newsock);

	/* Now attach up the new socket */
	kfree_skb(skb);
	sk->sk_ack_backlog--;
	newsock->state = SS_CONNECTED;

out:
	release_sock(sk);

	return err;
}

static int ax25_getname(struct socket *sock, struct sockaddr *uaddr,
	int *uaddr_len, int peer)
{
	struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)uaddr;
	struct sock *sk = sock->sk;
	unsigned char ndigi, i;
	ax25_cb *ax25;
	int err = 0;

	lock_sock(sk);
	ax25 = ax25_sk(sk);

	if (peer != 0) {
		if (sk->sk_state != TCP_ESTABLISHED) {
			err = -ENOTCONN;
			goto out;
		}

		fsa->fsa_ax25.sax25_family = AF_AX25;
		fsa->fsa_ax25.sax25_call   = ax25->dest_addr;
		fsa->fsa_ax25.sax25_ndigis = 0;

		if (ax25->digipeat != NULL) {
			ndigi = ax25->digipeat->ndigi;
			fsa->fsa_ax25.sax25_ndigis = ndigi;
			for (i = 0; i < ndigi; i++)
				fsa->fsa_digipeater[i] =
						ax25->digipeat->calls[i];
		}
	} else {
		fsa->fsa_ax25.sax25_family = AF_AX25;
		fsa->fsa_ax25.sax25_call   = ax25->source_addr;
		fsa->fsa_ax25.sax25_ndigis = 1;
		if (ax25->ax25_dev != NULL) {
			memcpy(&fsa->fsa_digipeater[0],
			       ax25->ax25_dev->dev->dev_addr, AX25_ADDR_LEN);
		} else {
			fsa->fsa_digipeater[0] = null_ax25_address;
		}
	}
	*uaddr_len = sizeof (struct full_sockaddr_ax25);

out:
	release_sock(sk);

	return err;
}

static int ax25_sendmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t len)
{
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)msg->msg_name;
	struct sock *sk = sock->sk;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	ax25_digi dtmp, *dp;
	ax25_cb *ax25;
	size_t size;
	int lv, err, addr_len = msg->msg_namelen;

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_EOR|MSG_CMSG_COMPAT))
		return -EINVAL;

	lock_sock(sk);
	ax25 = ax25_sk(sk);

	if (sock_flag(sk, SOCK_ZAPPED)) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		err = -EPIPE;
		goto out;
	}

	if (ax25->ax25_dev == NULL) {
		err = -ENETUNREACH;
		goto out;
	}

	if (len > ax25->ax25_dev->dev->mtu) {
		err = -EMSGSIZE;
		goto out;
	}

	if (usax != NULL) {
		if (usax->sax25_family != AF_AX25) {
			err = -EINVAL;
			goto out;
		}

		if (addr_len == sizeof(struct sockaddr_ax25))
			/* ax25_sendmsg(): uses obsolete socket structure */
			;
		else if (addr_len != sizeof(struct full_sockaddr_ax25))
			/* support for old structure may go away some time
			 * ax25_sendmsg(): uses old (6 digipeater)
			 * socket structure.
			 */
			if ((addr_len < sizeof(struct sockaddr_ax25) + sizeof(ax25_address) * 6) ||
			    (addr_len > sizeof(struct full_sockaddr_ax25))) {
				err = -EINVAL;
				goto out;
			}


		if (addr_len > sizeof(struct sockaddr_ax25) && usax->sax25_ndigis != 0) {
			int ct           = 0;
			struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)usax;

			/* Valid number of digipeaters ? */
			if (usax->sax25_ndigis < 1 || usax->sax25_ndigis > AX25_MAX_DIGIS) {
				err = -EINVAL;
				goto out;
			}

			dtmp.ndigi      = usax->sax25_ndigis;

			while (ct < usax->sax25_ndigis) {
				dtmp.repeated[ct] = 0;
				dtmp.calls[ct]    = fsa->fsa_digipeater[ct];
				ct++;
			}

			dtmp.lastrepeat = 0;
		}

		sax = *usax;
		if (sk->sk_type == SOCK_SEQPACKET &&
		    ax25cmp(&ax25->dest_addr, &sax.sax25_call)) {
			err = -EISCONN;
			goto out;
		}
		if (usax->sax25_ndigis == 0)
			dp = NULL;
		else
			dp = &dtmp;
	} else {
		/*
		 *	FIXME: 1003.1g - if the socket is like this because
		 *	it has become closed (not started closed) and is VC
		 *	we ought to SIGPIPE, EPIPE
		 */
		if (sk->sk_state != TCP_ESTABLISHED) {
			err = -ENOTCONN;
			goto out;
		}
		sax.sax25_family = AF_AX25;
		sax.sax25_call   = ax25->dest_addr;
		dp = ax25->digipeat;
	}

	/* Build a packet */
	SOCK_DEBUG(sk, "AX.25: sendto: Addresses built. Building packet.\n");

	/* Assume the worst case */
	size = len + ax25->ax25_dev->dev->hard_header_len;

	skb = sock_alloc_send_skb(sk, size, msg->msg_flags&MSG_DONTWAIT, &err);
	if (skb == NULL)
		goto out;

	skb_reserve(skb, size - len);

	SOCK_DEBUG(sk, "AX.25: Appending user data\n");

	/* User data follows immediately after the AX.25 data */
	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		err = -EFAULT;
		kfree_skb(skb);
		goto out;
	}

	skb_reset_network_header(skb);

	/* Add the PID if one is not supplied by the user in the skb */
	if (!ax25->pidincl)
		*skb_push(skb, 1) = sk->sk_protocol;

	SOCK_DEBUG(sk, "AX.25: Transmitting buffer\n");

	if (sk->sk_type == SOCK_SEQPACKET) {
		/* Connected mode sockets go via the LAPB machine */
		if (sk->sk_state != TCP_ESTABLISHED) {
			kfree_skb(skb);
			err = -ENOTCONN;
			goto out;
		}

		/* Shove it onto the queue and kick */
		ax25_output(ax25, ax25->paclen, skb);

		err = len;
		goto out;
	}

	skb_push(skb, 1 + ax25_addr_size(dp));

	SOCK_DEBUG(sk, "Building AX.25 Header (dp=%p).\n", dp);

	if (dp != NULL)
		SOCK_DEBUG(sk, "Num digipeaters=%d\n", dp->ndigi);

	/* Build an AX.25 header */
	lv = ax25_addr_build(skb->data, &ax25->source_addr, &sax.sax25_call,
			     dp, AX25_COMMAND, AX25_MODULUS);

	SOCK_DEBUG(sk, "Built header (%d bytes)\n",lv);

	skb_set_transport_header(skb, lv);

	SOCK_DEBUG(sk, "base=%p pos=%p\n",
		   skb->data, skb_transport_header(skb));

	*skb_transport_header(skb) = AX25_UI;

	/* Datagram frames go straight out of the door as UI */
	ax25_queue_xmit(skb, ax25->ax25_dev->dev);

	err = len;

out:
	release_sock(sk);

	return err;
}

static int ax25_recvmsg(struct kiocb *iocb, struct socket *sock,
	struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied;
	int err = 0;

	lock_sock(sk);
	/*
	 * 	This works for seqpacket too. The receiver has ordered the
	 *	queue for us! We do one quick check first though
	 */
	if (sk->sk_type == SOCK_SEQPACKET && sk->sk_state != TCP_ESTABLISHED) {
		err =  -ENOTCONN;
		goto out;
	}

	/* Now we can treat all alike */
	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &err);
	if (skb == NULL)
		goto out;

	if (!ax25_sk(sk)->pidincl)
		skb_pull(skb, 1);		/* Remove PID */

	skb_reset_transport_header(skb);
	copied = skb->len;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	if (msg->msg_namelen != 0) {
		struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)msg->msg_name;
		ax25_digi digi;
		ax25_address src;
		const unsigned char *mac = skb_mac_header(skb);

		ax25_addr_parse(mac + 1, skb->data - mac - 1, &src, NULL,
				&digi, NULL, NULL);
		sax->sax25_family = AF_AX25;
		/* We set this correctly, even though we may not let the
		   application know the digi calls further down (because it
		   did NOT ask to know them).  This could get political... **/
		sax->sax25_ndigis = digi.ndigi;
		sax->sax25_call   = src;

		if (sax->sax25_ndigis != 0) {
			int ct;
			struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)sax;

			for (ct = 0; ct < digi.ndigi; ct++)
				fsa->fsa_digipeater[ct] = digi.calls[ct];
		}
		msg->msg_namelen = sizeof(struct full_sockaddr_ax25);
	}

	skb_free_datagram(sk, skb);
	err = copied;

out:
	release_sock(sk);

	return err;
}

static int ax25_shutdown(struct socket *sk, int how)
{
	/* FIXME - generate DM and RNR states */
	return -EOPNOTSUPP;
}

static int ax25_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	void __user *argp = (void __user *)arg;
	int res = 0;

	lock_sock(sk);
	switch (cmd) {
	case TIOCOUTQ: {
		long amount;

		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		res = put_user(amount, (int __user *)argp);
		break;
	}

	case TIOCINQ: {
		struct sk_buff *skb;
		long amount = 0L;
		/* These two are safe on a single CPU system as only user tasks fiddle here */
		if ((skb = skb_peek(&sk->sk_receive_queue)) != NULL)
			amount = skb->len;
		res = put_user(amount, (int __user *) argp);
		break;
	}

	case SIOCGSTAMP:
		res = sock_get_timestamp(sk, argp);
		break;

	case SIOCGSTAMPNS:
		res = sock_get_timestampns(sk, argp);
		break;

	case SIOCAX25ADDUID:	/* Add a uid to the uid/call map table */
	case SIOCAX25DELUID:	/* Delete a uid from the uid/call map table */
	case SIOCAX25GETUID: {
		struct sockaddr_ax25 sax25;
		if (copy_from_user(&sax25, argp, sizeof(sax25))) {
			res = -EFAULT;
			break;
		}
		res = ax25_uid_ioctl(cmd, &sax25);
		break;
	}

	case SIOCAX25NOUID: {	/* Set the default policy (default/bar) */
		long amount;
		if (!capable(CAP_NET_ADMIN)) {
			res = -EPERM;
			break;
		}
		if (get_user(amount, (long __user *)argp)) {
			res = -EFAULT;
			break;
		}
		if (amount > AX25_NOUID_BLOCK) {
			res = -EINVAL;
			break;
		}
		ax25_uid_policy = amount;
		res = 0;
		break;
	}

	case SIOCADDRT:
	case SIOCDELRT:
	case SIOCAX25OPTRT:
		if (!capable(CAP_NET_ADMIN)) {
			res = -EPERM;
			break;
		}
		res = ax25_rt_ioctl(cmd, argp);
		break;

	case SIOCAX25CTLCON:
		if (!capable(CAP_NET_ADMIN)) {
			res = -EPERM;
			break;
		}
		res = ax25_ctl_ioctl(cmd, argp);
		break;

	case SIOCAX25GETINFO:
	case SIOCAX25GETINFOOLD: {
		ax25_cb *ax25 = ax25_sk(sk);
		struct ax25_info_struct ax25_info;

		ax25_info.t1        = ax25->t1   / HZ;
		ax25_info.t2        = ax25->t2   / HZ;
		ax25_info.t3        = ax25->t3   / HZ;
		ax25_info.idle      = ax25->idle / (60 * HZ);
		ax25_info.n2        = ax25->n2;
		ax25_info.t1timer   = ax25_display_timer(&ax25->t1timer)   / HZ;
		ax25_info.t2timer   = ax25_display_timer(&ax25->t2timer)   / HZ;
		ax25_info.t3timer   = ax25_display_timer(&ax25->t3timer)   / HZ;
		ax25_info.idletimer = ax25_display_timer(&ax25->idletimer) / (60 * HZ);
		ax25_info.n2count   = ax25->n2count;
		ax25_info.state     = ax25->state;
		ax25_info.rcv_q     = sk_rmem_alloc_get(sk);
		ax25_info.snd_q     = sk_wmem_alloc_get(sk);
		ax25_info.vs        = ax25->vs;
		ax25_info.vr        = ax25->vr;
		ax25_info.va        = ax25->va;
		ax25_info.vs_max    = ax25->vs; /* reserved */
		ax25_info.paclen    = ax25->paclen;
		ax25_info.window    = ax25->window;

		/* old structure? */
		if (cmd == SIOCAX25GETINFOOLD) {
			static int warned = 0;
			if (!warned) {
				printk(KERN_INFO "%s uses old SIOCAX25GETINFO\n",
					current->comm);
				warned=1;
			}

			if (copy_to_user(argp, &ax25_info, sizeof(struct ax25_info_struct_deprecated))) {
				res = -EFAULT;
				break;
			}
		} else {
			if (copy_to_user(argp, &ax25_info, sizeof(struct ax25_info_struct))) {
				res = -EINVAL;
				break;
			}
		}
		res = 0;
		break;
	}

	case SIOCAX25ADDFWD:
	case SIOCAX25DELFWD: {
		struct ax25_fwd_struct ax25_fwd;
		if (!capable(CAP_NET_ADMIN)) {
			res = -EPERM;
			break;
		}
		if (copy_from_user(&ax25_fwd, argp, sizeof(ax25_fwd))) {
			res = -EFAULT;
			break;
		}
		res = ax25_fwd_ioctl(cmd, &ax25_fwd);
		break;
	}

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
		res = -EINVAL;
		break;

	default:
		res = -ENOIOCTLCMD;
		break;
	}
	release_sock(sk);

	return res;
}

#ifdef CONFIG_PROC_FS

static void *ax25_info_start(struct seq_file *seq, loff_t *pos)
	__acquires(ax25_list_lock)
{
	struct ax25_cb *ax25;
	struct hlist_node *node;
	int i = 0;

	spin_lock_bh(&ax25_list_lock);
	ax25_for_each(ax25, node, &ax25_list) {
		if (i == *pos)
			return ax25;
		++i;
	}
	return NULL;
}

static void *ax25_info_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return hlist_entry( ((struct ax25_cb *)v)->ax25_node.next,
			    struct ax25_cb, ax25_node);
}

static void ax25_info_stop(struct seq_file *seq, void *v)
	__releases(ax25_list_lock)
{
	spin_unlock_bh(&ax25_list_lock);
}

static int ax25_info_show(struct seq_file *seq, void *v)
{
	ax25_cb *ax25 = v;
	char buf[11];
	int k;


	/*
	 * New format:
	 * magic dev src_addr dest_addr,digi1,digi2,.. st vs vr va t1 t1 t2 t2 t3 t3 idle idle n2 n2 rtt window paclen Snd-Q Rcv-Q inode
	 */

	seq_printf(seq, "%8.8lx %s %s%s ",
		   (long) ax25,
		   ax25->ax25_dev == NULL? "???" : ax25->ax25_dev->dev->name,
		   ax2asc(buf, &ax25->source_addr),
		   ax25->iamdigi? "*":"");
	seq_printf(seq, "%s", ax2asc(buf, &ax25->dest_addr));

	for (k=0; (ax25->digipeat != NULL) && (k < ax25->digipeat->ndigi); k++) {
		seq_printf(seq, ",%s%s",
			   ax2asc(buf, &ax25->digipeat->calls[k]),
			   ax25->digipeat->repeated[k]? "*":"");
	}

	seq_printf(seq, " %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %d %d",
		   ax25->state,
		   ax25->vs, ax25->vr, ax25->va,
		   ax25_display_timer(&ax25->t1timer) / HZ, ax25->t1 / HZ,
		   ax25_display_timer(&ax25->t2timer) / HZ, ax25->t2 / HZ,
		   ax25_display_timer(&ax25->t3timer) / HZ, ax25->t3 / HZ,
		   ax25_display_timer(&ax25->idletimer) / (60 * HZ),
		   ax25->idle / (60 * HZ),
		   ax25->n2count, ax25->n2,
		   ax25->rtt / HZ,
		   ax25->window,
		   ax25->paclen);

	if (ax25->sk != NULL) {
		seq_printf(seq, " %d %d %lu\n",
			   sk_wmem_alloc_get(ax25->sk),
			   sk_rmem_alloc_get(ax25->sk),
			   sock_i_ino(ax25->sk));
	} else {
		seq_puts(seq, " * * *\n");
	}
	return 0;
}

static const struct seq_operations ax25_info_seqops = {
	.start = ax25_info_start,
	.next = ax25_info_next,
	.stop = ax25_info_stop,
	.show = ax25_info_show,
};

static int ax25_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ax25_info_seqops);
}

static const struct file_operations ax25_info_fops = {
	.owner = THIS_MODULE,
	.open = ax25_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

#endif

static const struct net_proto_family ax25_family_ops = {
	.family =	PF_AX25,
	.create =	ax25_create,
	.owner	=	THIS_MODULE,
};

static const struct proto_ops ax25_proto_ops = {
	.family		= PF_AX25,
	.owner		= THIS_MODULE,
	.release	= ax25_release,
	.bind		= ax25_bind,
	.connect	= ax25_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= ax25_accept,
	.getname	= ax25_getname,
	.poll		= datagram_poll,
	.ioctl		= ax25_ioctl,
	.listen		= ax25_listen,
	.shutdown	= ax25_shutdown,
	.setsockopt	= ax25_setsockopt,
	.getsockopt	= ax25_getsockopt,
	.sendmsg	= ax25_sendmsg,
	.recvmsg	= ax25_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

/*
 *	Called by socket.c on kernel start up
 */
static struct packet_type ax25_packet_type __read_mostly = {
	.type	=	cpu_to_be16(ETH_P_AX25),
	.func	=	ax25_kiss_rcv,
};

static struct notifier_block ax25_dev_notifier = {
	.notifier_call =ax25_device_event,
};

static int __init ax25_init(void)
{
	int rc = proto_register(&ax25_proto, 0);

	if (rc != 0)
		goto out;

	sock_register(&ax25_family_ops);
	dev_add_pack(&ax25_packet_type);
	register_netdevice_notifier(&ax25_dev_notifier);
	ax25_register_sysctl();

	proc_net_fops_create(&init_net, "ax25_route", S_IRUGO, &ax25_route_fops);
	proc_net_fops_create(&init_net, "ax25", S_IRUGO, &ax25_info_fops);
	proc_net_fops_create(&init_net, "ax25_calls", S_IRUGO, &ax25_uid_fops);
out:
	return rc;
}
module_init(ax25_init);


MODULE_AUTHOR("Jonathan Naylor G4KLX <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The amateur radio AX.25 link layer protocol");
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_AX25);

static void __exit ax25_exit(void)
{
	proc_net_remove(&init_net, "ax25_route");
	proc_net_remove(&init_net, "ax25");
	proc_net_remove(&init_net, "ax25_calls");
	ax25_rt_free();
	ax25_uid_free();
	ax25_dev_free();

	ax25_unregister_sysctl();
	unregister_netdevice_notifier(&ax25_dev_notifier);

	dev_remove_pack(&ax25_packet_type);

	sock_unregister(PF_AX25);
	proto_unregister(&ax25_proto);
}
module_exit(ax25_exit);
