/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/timer.h>
#include <net/ax25.h>
#include <linux/skbuff.h>
#include <net/rose.h>
#include <linux/init.h>

static struct sk_buff_head loopback_queue;
static struct timer_list loopback_timer;

static void rose_set_loopback_timer(void);

void rose_loopback_init(void)
{
	skb_queue_head_init(&loopback_queue);

	init_timer(&loopback_timer);
}

static int rose_loopback_running(void)
{
	return timer_pending(&loopback_timer);
}

int rose_loopback_queue(struct sk_buff *skb, struct rose_neigh *neigh)
{
	struct sk_buff *skbn;

	skbn = skb_clone(skb, GFP_ATOMIC);

	kfree_skb(skb);

	if (skbn != NULL) {
		skb_queue_tail(&loopback_queue, skbn);

		if (!rose_loopback_running())
			rose_set_loopback_timer();
	}

	return 1;
}

static void rose_loopback_timer(unsigned long);

static void rose_set_loopback_timer(void)
{
	del_timer(&loopback_timer);

	loopback_timer.data     = 0;
	loopback_timer.function = &rose_loopback_timer;
	loopback_timer.expires  = jiffies + 10;

	add_timer(&loopback_timer);
}

static void rose_loopback_timer(unsigned long param)
{
	struct sk_buff *skb;
	struct net_device *dev;
	rose_address *dest;
	struct sock *sk;
	unsigned short frametype;
	unsigned int lci_i, lci_o;

	while ((skb = skb_dequeue(&loopback_queue)) != NULL) {
		lci_i     = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);
		frametype = skb->data[2];
		dest      = (rose_address *)(skb->data + 4);
		lci_o     = ROSE_DEFAULT_MAXVC + 1 - lci_i;

		skb_reset_transport_header(skb);

		sk = rose_find_socket(lci_o, rose_loopback_neigh);
		if (sk) {
			if (rose_process_rx_frame(sk, skb) == 0)
				kfree_skb(skb);
			continue;
		}

		if (frametype == ROSE_CALL_REQUEST) {
			if ((dev = rose_dev_get(dest)) != NULL) {
				if (rose_rx_call_request(skb, dev, rose_loopback_neigh, lci_o) == 0)
					kfree_skb(skb);
			} else {
				kfree_skb(skb);
			}
		} else {
			kfree_skb(skb);
		}
	}
}

void __exit rose_loopback_clear(void)
{
	struct sk_buff *skb;

	del_timer(&loopback_timer);

	while ((skb = skb_dequeue(&loopback_queue)) != NULL) {
		skb->sk = NULL;
		kfree_skb(skb);
	}
}
