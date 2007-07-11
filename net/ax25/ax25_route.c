/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Alan Cox GW4PTS (alan@lxorguk.ukuu.org.uk)
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 * Copyright (C) Steven Whitehouse GW7RRM (stevew@acm.org)
 * Copyright (C) Joerg Reuter DL1BKE (jreuter@yaina.de)
 * Copyright (C) Hans-Joachim Hetscher DD8NE (dd8ne@bnv-bamberg.de)
 * Copyright (C) Frederic Rible F1OAT (frible@teaser.fr)
 */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/timer.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/seq_file.h>

static ax25_route *ax25_route_list;
static DEFINE_RWLOCK(ax25_route_lock);

void ax25_rt_device_down(struct net_device *dev)
{
	ax25_route *s, *t, *ax25_rt;

	write_lock(&ax25_route_lock);
	ax25_rt = ax25_route_list;
	while (ax25_rt != NULL) {
		s       = ax25_rt;
		ax25_rt = ax25_rt->next;

		if (s->dev == dev) {
			if (ax25_route_list == s) {
				ax25_route_list = s->next;
				kfree(s->digipeat);
				kfree(s);
			} else {
				for (t = ax25_route_list; t != NULL; t = t->next) {
					if (t->next == s) {
						t->next = s->next;
						kfree(s->digipeat);
						kfree(s);
						break;
					}
				}
			}
		}
	}
	write_unlock(&ax25_route_lock);
}

static int __must_check ax25_rt_add(struct ax25_routes_struct *route)
{
	ax25_route *ax25_rt;
	ax25_dev *ax25_dev;
	int i;

	if ((ax25_dev = ax25_addr_ax25dev(&route->port_addr)) == NULL)
		return -EINVAL;
	if (route->digi_count > AX25_MAX_DIGIS)
		return -EINVAL;

	write_lock(&ax25_route_lock);

	ax25_rt = ax25_route_list;
	while (ax25_rt != NULL) {
		if (ax25cmp(&ax25_rt->callsign, &route->dest_addr) == 0 &&
			    ax25_rt->dev == ax25_dev->dev) {
			kfree(ax25_rt->digipeat);
			ax25_rt->digipeat = NULL;
			if (route->digi_count != 0) {
				if ((ax25_rt->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
					write_unlock(&ax25_route_lock);
					return -ENOMEM;
				}
				ax25_rt->digipeat->lastrepeat = -1;
				ax25_rt->digipeat->ndigi      = route->digi_count;
				for (i = 0; i < route->digi_count; i++) {
					ax25_rt->digipeat->repeated[i] = 0;
					ax25_rt->digipeat->calls[i]    = route->digi_addr[i];
				}
			}
			write_unlock(&ax25_route_lock);
			return 0;
		}
		ax25_rt = ax25_rt->next;
	}

	if ((ax25_rt = kmalloc(sizeof(ax25_route), GFP_ATOMIC)) == NULL) {
		write_unlock(&ax25_route_lock);
		return -ENOMEM;
	}

	atomic_set(&ax25_rt->refcount, 1);
	ax25_rt->callsign     = route->dest_addr;
	ax25_rt->dev          = ax25_dev->dev;
	ax25_rt->digipeat     = NULL;
	ax25_rt->ip_mode      = ' ';
	if (route->digi_count != 0) {
		if ((ax25_rt->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			write_unlock(&ax25_route_lock);
			kfree(ax25_rt);
			return -ENOMEM;
		}
		ax25_rt->digipeat->lastrepeat = -1;
		ax25_rt->digipeat->ndigi      = route->digi_count;
		for (i = 0; i < route->digi_count; i++) {
			ax25_rt->digipeat->repeated[i] = 0;
			ax25_rt->digipeat->calls[i]    = route->digi_addr[i];
		}
	}
	ax25_rt->next   = ax25_route_list;
	ax25_route_list = ax25_rt;
	write_unlock(&ax25_route_lock);

	return 0;
}

void __ax25_put_route(ax25_route *ax25_rt)
{
	kfree(ax25_rt->digipeat);
	kfree(ax25_rt);
}

static int ax25_rt_del(struct ax25_routes_struct *route)
{
	ax25_route *s, *t, *ax25_rt;
	ax25_dev *ax25_dev;

	if ((ax25_dev = ax25_addr_ax25dev(&route->port_addr)) == NULL)
		return -EINVAL;

	write_lock(&ax25_route_lock);

	ax25_rt = ax25_route_list;
	while (ax25_rt != NULL) {
		s       = ax25_rt;
		ax25_rt = ax25_rt->next;
		if (s->dev == ax25_dev->dev &&
		    ax25cmp(&route->dest_addr, &s->callsign) == 0) {
			if (ax25_route_list == s) {
				ax25_route_list = s->next;
				ax25_put_route(s);
			} else {
				for (t = ax25_route_list; t != NULL; t = t->next) {
					if (t->next == s) {
						t->next = s->next;
						ax25_put_route(s);
						break;
					}
				}
			}
		}
	}
	write_unlock(&ax25_route_lock);

	return 0;
}

static int ax25_rt_opt(struct ax25_route_opt_struct *rt_option)
{
	ax25_route *ax25_rt;
	ax25_dev *ax25_dev;
	int err = 0;

	if ((ax25_dev = ax25_addr_ax25dev(&rt_option->port_addr)) == NULL)
		return -EINVAL;

	write_lock(&ax25_route_lock);

	ax25_rt = ax25_route_list;
	while (ax25_rt != NULL) {
		if (ax25_rt->dev == ax25_dev->dev &&
		    ax25cmp(&rt_option->dest_addr, &ax25_rt->callsign) == 0) {
			switch (rt_option->cmd) {
			case AX25_SET_RT_IPMODE:
				switch (rt_option->arg) {
				case ' ':
				case 'D':
				case 'V':
					ax25_rt->ip_mode = rt_option->arg;
					break;
				default:
					err = -EINVAL;
					goto out;
				}
				break;
			default:
				err = -EINVAL;
				goto out;
			}
		}
		ax25_rt = ax25_rt->next;
	}

out:
	write_unlock(&ax25_route_lock);
	return err;
}

int ax25_rt_ioctl(unsigned int cmd, void __user *arg)
{
	struct ax25_route_opt_struct rt_option;
	struct ax25_routes_struct route;

	switch (cmd) {
	case SIOCADDRT:
		if (copy_from_user(&route, arg, sizeof(route)))
			return -EFAULT;
		return ax25_rt_add(&route);

	case SIOCDELRT:
		if (copy_from_user(&route, arg, sizeof(route)))
			return -EFAULT;
		return ax25_rt_del(&route);

	case SIOCAX25OPTRT:
		if (copy_from_user(&rt_option, arg, sizeof(rt_option)))
			return -EFAULT;
		return ax25_rt_opt(&rt_option);

	default:
		return -EINVAL;
	}
}

#ifdef CONFIG_PROC_FS

static void *ax25_rt_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct ax25_route *ax25_rt;
	int i = 1;

	read_lock(&ax25_route_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (ax25_rt = ax25_route_list; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (i == *pos)
			return ax25_rt;
		++i;
	}

	return NULL;
}

static void *ax25_rt_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return (v == SEQ_START_TOKEN) ? ax25_route_list :
		((struct ax25_route *) v)->next;
}

static void ax25_rt_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&ax25_route_lock);
}

static int ax25_rt_seq_show(struct seq_file *seq, void *v)
{
	char buf[11];

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "callsign  dev  mode digipeaters\n");
	else {
		struct ax25_route *ax25_rt = v;
		const char *callsign;
		int i;

		if (ax25cmp(&ax25_rt->callsign, &null_ax25_address) == 0)
			callsign = "default";
		else
			callsign = ax2asc(buf, &ax25_rt->callsign);

		seq_printf(seq, "%-9s %-4s",
			callsign,
			ax25_rt->dev ? ax25_rt->dev->name : "???");

		switch (ax25_rt->ip_mode) {
		case 'V':
			seq_puts(seq, "   vc");
			break;
		case 'D':
			seq_puts(seq, "   dg");
			break;
		default:
			seq_puts(seq, "    *");
			break;
		}

		if (ax25_rt->digipeat != NULL)
			for (i = 0; i < ax25_rt->digipeat->ndigi; i++)
				seq_printf(seq, " %s",
				     ax2asc(buf, &ax25_rt->digipeat->calls[i]));

		seq_puts(seq, "\n");
	}
	return 0;
}

static const struct seq_operations ax25_rt_seqops = {
	.start = ax25_rt_seq_start,
	.next = ax25_rt_seq_next,
	.stop = ax25_rt_seq_stop,
	.show = ax25_rt_seq_show,
};

static int ax25_rt_info_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ax25_rt_seqops);
}

const struct file_operations ax25_route_fops = {
	.owner = THIS_MODULE,
	.open = ax25_rt_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

#endif

/*
 *	Find AX.25 route
 *
 *	Only routes with a reference count of zero can be destroyed.
 */
ax25_route *ax25_get_route(ax25_address *addr, struct net_device *dev)
{
	ax25_route *ax25_spe_rt = NULL;
	ax25_route *ax25_def_rt = NULL;
	ax25_route *ax25_rt;

	read_lock(&ax25_route_lock);
	/*
	 *	Bind to the physical interface we heard them on, or the default
	 *	route if none is found;
	 */
	for (ax25_rt = ax25_route_list; ax25_rt != NULL; ax25_rt = ax25_rt->next) {
		if (dev == NULL) {
			if (ax25cmp(&ax25_rt->callsign, addr) == 0 && ax25_rt->dev != NULL)
				ax25_spe_rt = ax25_rt;
			if (ax25cmp(&ax25_rt->callsign, &null_ax25_address) == 0 && ax25_rt->dev != NULL)
				ax25_def_rt = ax25_rt;
		} else {
			if (ax25cmp(&ax25_rt->callsign, addr) == 0 && ax25_rt->dev == dev)
				ax25_spe_rt = ax25_rt;
			if (ax25cmp(&ax25_rt->callsign, &null_ax25_address) == 0 && ax25_rt->dev == dev)
				ax25_def_rt = ax25_rt;
		}
	}

	ax25_rt = ax25_def_rt;
	if (ax25_spe_rt != NULL)
		ax25_rt = ax25_spe_rt;

	if (ax25_rt != NULL)
		ax25_hold_route(ax25_rt);

	read_unlock(&ax25_route_lock);

	return ax25_rt;
}

/*
 *	Adjust path: If you specify a default route and want to connect
 *      a target on the digipeater path but w/o having a special route
 *	set before, the path has to be truncated from your target on.
 */
static inline void ax25_adjust_path(ax25_address *addr, ax25_digi *digipeat)
{
	int k;

	for (k = 0; k < digipeat->ndigi; k++) {
		if (ax25cmp(addr, &digipeat->calls[k]) == 0)
			break;
	}

	digipeat->ndigi = k;
}


/*
 *	Find which interface to use.
 */
int ax25_rt_autobind(ax25_cb *ax25, ax25_address *addr)
{
	ax25_uid_assoc *user;
	ax25_route *ax25_rt;
	int err;

	if ((ax25_rt = ax25_get_route(addr, NULL)) == NULL)
		return -EHOSTUNREACH;

	if ((ax25->ax25_dev = ax25_dev_ax25dev(ax25_rt->dev)) == NULL) {
		err = -EHOSTUNREACH;
		goto put;
	}

	user = ax25_findbyuid(current->euid);
	if (user) {
		ax25->source_addr = user->call;
		ax25_uid_put(user);
	} else {
		if (ax25_uid_policy && !capable(CAP_NET_BIND_SERVICE)) {
			err = -EPERM;
			goto put;
		}
		ax25->source_addr = *(ax25_address *)ax25->ax25_dev->dev->dev_addr;
	}

	if (ax25_rt->digipeat != NULL) {
		ax25->digipeat = kmemdup(ax25_rt->digipeat, sizeof(ax25_digi),
					 GFP_ATOMIC);
		if (ax25->digipeat == NULL) {
			err = -ENOMEM;
			goto put;
		}
		ax25_adjust_path(addr, ax25->digipeat);
	}

	if (ax25->sk != NULL) {
		bh_lock_sock(ax25->sk);
		sock_reset_flag(ax25->sk, SOCK_ZAPPED);
		bh_unlock_sock(ax25->sk);
	}

put:
	ax25_put_route(ax25_rt);

	return 0;
}

struct sk_buff *ax25_rt_build_path(struct sk_buff *skb, ax25_address *src,
	ax25_address *dest, ax25_digi *digi)
{
	struct sk_buff *skbn;
	unsigned char *bp;
	int len;

	len = digi->ndigi * AX25_ADDR_LEN;

	if (skb_headroom(skb) < len) {
		if ((skbn = skb_realloc_headroom(skb, len)) == NULL) {
			printk(KERN_CRIT "AX.25: ax25_dg_build_path - out of memory\n");
			return NULL;
		}

		if (skb->sk != NULL)
			skb_set_owner_w(skbn, skb->sk);

		kfree_skb(skb);

		skb = skbn;
	}

	bp = skb_push(skb, len);

	ax25_addr_build(bp, src, dest, digi, AX25_COMMAND, AX25_MODULUS);

	return skb;
}

/*
 *	Free all memory associated with routing structures.
 */
void __exit ax25_rt_free(void)
{
	ax25_route *s, *ax25_rt = ax25_route_list;

	write_lock(&ax25_route_lock);
	while (ax25_rt != NULL) {
		s       = ax25_rt;
		ax25_rt = ax25_rt->next;

		kfree(s->digipeat);
		kfree(s);
	}
	write_unlock(&ax25_route_lock);
}
