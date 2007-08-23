/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

static struct ax25_protocol *protocol_list;
static DEFINE_RWLOCK(protocol_list_lock);

static HLIST_HEAD(ax25_linkfail_list);
static DEFINE_SPINLOCK(linkfail_lock);

static struct listen_struct {
	struct listen_struct *next;
	ax25_address  callsign;
	struct net_device *dev;
} *listen_list = NULL;
static DEFINE_SPINLOCK(listen_lock);

/*
 * Do not register the internal protocols AX25_P_TEXT, AX25_P_SEGMENT,
 * AX25_P_IP or AX25_P_ARP ...
 */
void ax25_register_pid(struct ax25_protocol *ap)
{
	write_lock_bh(&protocol_list_lock);
	ap->next = protocol_list;
	protocol_list = ap;
	write_unlock_bh(&protocol_list_lock);
}

EXPORT_SYMBOL_GPL(ax25_register_pid);

void ax25_protocol_release(unsigned int pid)
{
	struct ax25_protocol *s, *protocol;

	write_lock_bh(&protocol_list_lock);
	protocol = protocol_list;
	if (protocol == NULL) {
		write_unlock_bh(&protocol_list_lock);
		return;
	}

	if (protocol->pid == pid) {
		protocol_list = protocol->next;
		write_unlock_bh(&protocol_list_lock);
		return;
	}

	while (protocol != NULL && protocol->next != NULL) {
		if (protocol->next->pid == pid) {
			s = protocol->next;
			protocol->next = protocol->next->next;
			write_unlock_bh(&protocol_list_lock);
			return;
		}

		protocol = protocol->next;
	}
	write_unlock_bh(&protocol_list_lock);
}

EXPORT_SYMBOL(ax25_protocol_release);

void ax25_linkfail_register(struct ax25_linkfail *lf)
{
	spin_lock_bh(&linkfail_lock);
	hlist_add_head(&lf->lf_node, &ax25_linkfail_list);
	spin_unlock_bh(&linkfail_lock);
}

EXPORT_SYMBOL(ax25_linkfail_register);

void ax25_linkfail_release(struct ax25_linkfail *lf)
{
	spin_lock_bh(&linkfail_lock);
	hlist_del_init(&lf->lf_node);
	spin_unlock_bh(&linkfail_lock);
}

EXPORT_SYMBOL(ax25_linkfail_release);

int ax25_listen_register(ax25_address *callsign, struct net_device *dev)
{
	struct listen_struct *listen;

	if (ax25_listen_mine(callsign, dev))
		return 0;

	if ((listen = kmalloc(sizeof(*listen), GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	listen->callsign = *callsign;
	listen->dev      = dev;

	spin_lock_bh(&listen_lock);
	listen->next = listen_list;
	listen_list  = listen;
	spin_unlock_bh(&listen_lock);

	return 0;
}

EXPORT_SYMBOL(ax25_listen_register);

void ax25_listen_release(ax25_address *callsign, struct net_device *dev)
{
	struct listen_struct *s, *listen;

	spin_lock_bh(&listen_lock);
	listen = listen_list;
	if (listen == NULL) {
		spin_unlock_bh(&listen_lock);
		return;
	}

	if (ax25cmp(&listen->callsign, callsign) == 0 && listen->dev == dev) {
		listen_list = listen->next;
		spin_unlock_bh(&listen_lock);
		kfree(listen);
		return;
	}

	while (listen != NULL && listen->next != NULL) {
		if (ax25cmp(&listen->next->callsign, callsign) == 0 && listen->next->dev == dev) {
			s = listen->next;
			listen->next = listen->next->next;
			spin_unlock_bh(&listen_lock);
			kfree(s);
			return;
		}

		listen = listen->next;
	}
	spin_unlock_bh(&listen_lock);
}

EXPORT_SYMBOL(ax25_listen_release);

int (*ax25_protocol_function(unsigned int pid))(struct sk_buff *, ax25_cb *)
{
	int (*res)(struct sk_buff *, ax25_cb *) = NULL;
	struct ax25_protocol *protocol;

	read_lock(&protocol_list_lock);
	for (protocol = protocol_list; protocol != NULL; protocol = protocol->next)
		if (protocol->pid == pid) {
			res = protocol->func;
			break;
		}
	read_unlock(&protocol_list_lock);

	return res;
}

int ax25_listen_mine(ax25_address *callsign, struct net_device *dev)
{
	struct listen_struct *listen;

	spin_lock_bh(&listen_lock);
	for (listen = listen_list; listen != NULL; listen = listen->next)
		if (ax25cmp(&listen->callsign, callsign) == 0 &&
		    (listen->dev == dev || listen->dev == NULL)) {
			spin_unlock_bh(&listen_lock);
			return 1;
	}
	spin_unlock_bh(&listen_lock);

	return 0;
}

void ax25_link_failed(ax25_cb *ax25, int reason)
{
	struct ax25_linkfail *lf;
	struct hlist_node *node;

	spin_lock_bh(&linkfail_lock);
	hlist_for_each_entry(lf, node, &ax25_linkfail_list, lf_node)
		lf->func(ax25, reason);
	spin_unlock_bh(&linkfail_lock);
}

int ax25_protocol_is_registered(unsigned int pid)
{
	struct ax25_protocol *protocol;
	int res = 0;

	read_lock_bh(&protocol_list_lock);
	for (protocol = protocol_list; protocol != NULL; protocol = protocol->next)
		if (protocol->pid == pid) {
			res = 1;
			break;
		}
	read_unlock_bh(&protocol_list_lock);

	return res;
}
