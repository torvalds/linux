// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/timer.h>
#include <net/ax25.h>
#include <linux/skbuff.h>
#include <net/rose.h>
#include <linux/init.h>

static struct sk_buff_head loopback_queue;
#define ROSE_LOOPBACK_LIMIT 1000
static struct timer_list loopback_timer;

static void rose_set_loopback_timer(void);
static void rose_loopback_timer(struct timer_list *unused);

void rose_loopback_init(void)
{
	skb_queue_head_init(&loopback_queue);

	timer_setup(&loopback_timer, rose_loopback_timer, 0);
}

static int rose_loopback_running(void)
{
	return timer_pending(&loopback_timer);
}

int rose_loopback_queue(struct sk_buff *skb, struct rose_neigh *neigh)
{
	struct sk_buff *skbn = NULL;

	if (skb_queue_len(&loopback_queue) < ROSE_LOOPBACK_LIMIT)
		skbn = skb_clone(skb, GFP_ATOMIC);

	if (skbn) {
		consume_skb(skb);
		skb_queue_tail(&loopback_queue, skbn);

		if (!rose_loopback_running())
			rose_set_loopback_timer();
	} else {
		kfree_skb(skb);
	}

	return 1;
}

static void rose_set_loopback_timer(void)
{
	mod_timer(&loopback_timer, jiffies + 10);
}

static void rose_loopback_timer(struct timer_list *unused)
{
	struct sk_buff *skb;
	struct net_device *dev;
	rose_address *dest;
	struct sock *sk;
	unsigned short frametype;
	unsigned int lci_i, lci_o;
	int count;

	for (count = 0; count < ROSE_LOOPBACK_LIMIT; count++) {
		skb = skb_dequeue(&loopback_queue);
		if (!skb)
			return;
		if (skb->len < ROSE_MIN_LEN) {
			kfree_skb(skb);
			continue;
		}
		lci_i     = ((skb->data[0] << 8) & 0xF00) + ((skb->data[1] << 0) & 0x0FF);
		frametype = skb->data[2];
		if (frametype == ROSE_CALL_REQUEST &&
		    (skb->len <= ROSE_CALL_REQ_FACILITIES_OFF ||
		     skb->data[ROSE_CALL_REQ_ADDR_LEN_OFF] !=
		     ROSE_CALL_REQ_ADDR_LEN_VAL)) {
			kfree_skb(skb);
			continue;
		}
		dest      = (rose_address *)(skb->data + ROSE_CALL_REQ_DEST_ADDR_OFF);
		lci_o     = ROSE_DEFAULT_MAXVC + 1 - lci_i;

		skb_reset_transport_header(skb);

		sk = rose_find_socket(lci_o, rose_loopback_neigh);
		if (sk) {
			if (rose_process_rx_frame(sk, skb) == 0)
				kfree_skb(skb);
			continue;
		}

		if (frametype == ROSE_CALL_REQUEST) {
			if (!rose_loopback_neigh->dev) {
				kfree_skb(skb);
				continue;
			}

			dev = rose_dev_get(dest);
			if (!dev) {
				kfree_skb(skb);
				continue;
			}

			if (rose_rx_call_request(skb, dev, rose_loopback_neigh, lci_o) == 0) {
				dev_put(dev);
				kfree_skb(skb);
			}
		} else {
			kfree_skb(skb);
		}
	}
	if (!skb_queue_empty(&loopback_queue))
		mod_timer(&loopback_timer, jiffies + 1);
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
