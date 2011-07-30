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
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/netfilter.h>
#include <net/rose.h>

static void rose_ftimer_expiry(unsigned long);
static void rose_t0timer_expiry(unsigned long);

static void rose_transmit_restart_confirmation(struct rose_neigh *neigh);
static void rose_transmit_restart_request(struct rose_neigh *neigh);

void rose_start_ftimer(struct rose_neigh *neigh)
{
	del_timer(&neigh->ftimer);

	neigh->ftimer.data     = (unsigned long)neigh;
	neigh->ftimer.function = &rose_ftimer_expiry;
	neigh->ftimer.expires  =
		jiffies + msecs_to_jiffies(sysctl_rose_link_fail_timeout);

	add_timer(&neigh->ftimer);
}

static void rose_start_t0timer(struct rose_neigh *neigh)
{
	del_timer(&neigh->t0timer);

	neigh->t0timer.data     = (unsigned long)neigh;
	neigh->t0timer.function = &rose_t0timer_expiry;
	neigh->t0timer.expires  =
		jiffies + msecs_to_jiffies(sysctl_rose_restart_request_timeout);

	add_timer(&neigh->t0timer);
}

void rose_stop_ftimer(struct rose_neigh *neigh)
{
	del_timer(&neigh->ftimer);
}

void rose_stop_t0timer(struct rose_neigh *neigh)
{
	del_timer(&neigh->t0timer);
}

int rose_ftimer_running(struct rose_neigh *neigh)
{
	return timer_pending(&neigh->ftimer);
}

static int rose_t0timer_running(struct rose_neigh *neigh)
{
	return timer_pending(&neigh->t0timer);
}

static void rose_ftimer_expiry(unsigned long param)
{
}

static void rose_t0timer_expiry(unsigned long param)
{
	struct rose_neigh *neigh = (struct rose_neigh *)param;

	rose_transmit_restart_request(neigh);

	neigh->dce_mode = 0;

	rose_start_t0timer(neigh);
}

/*
 *	Interface to ax25_send_frame. Changes my level 2 callsign depending
 *	on whether we have a global ROSE callsign or use the default port
 *	callsign.
 */
static int rose_send_frame(struct sk_buff *skb, struct rose_neigh *neigh)
{
	ax25_address *rose_call;

	if (ax25cmp(&rose_callsign, &null_ax25_address) == 0)
		rose_call = (ax25_address *)neigh->dev->dev_addr;
	else
		rose_call = &rose_callsign;

	neigh->ax25 = ax25_send_frame(skb, 260, rose_call, &neigh->callsign, neigh->digipeat, neigh->dev);

	return (neigh->ax25 != NULL);
}

/*
 *	Interface to ax25_link_up. Changes my level 2 callsign depending
 *	on whether we have a global ROSE callsign or use the default port
 *	callsign.
 */
static int rose_link_up(struct rose_neigh *neigh)
{
	ax25_address *rose_call;

	if (ax25cmp(&rose_callsign, &null_ax25_address) == 0)
		rose_call = (ax25_address *)neigh->dev->dev_addr;
	else
		rose_call = &rose_callsign;

	neigh->ax25 = ax25_find_cb(rose_call, &neigh->callsign, neigh->digipeat, neigh->dev);

	return (neigh->ax25 != NULL);
}

/*
 *	This handles all restart and diagnostic frames.
 */
void rose_link_rx_restart(struct sk_buff *skb, struct rose_neigh *neigh, unsigned short frametype)
{
	struct sk_buff *skbn;

	switch (frametype) {
	case ROSE_RESTART_REQUEST:
		rose_stop_t0timer(neigh);
		neigh->restarted = 1;
		neigh->dce_mode  = (skb->data[3] == ROSE_DTE_ORIGINATED);
		rose_transmit_restart_confirmation(neigh);
		break;

	case ROSE_RESTART_CONFIRMATION:
		rose_stop_t0timer(neigh);
		neigh->restarted = 1;
		break;

	case ROSE_DIAGNOSTIC:
		printk(KERN_WARNING "ROSE: received diagnostic #%d - %02X %02X %02X\n", skb->data[3], skb->data[4], skb->data[5], skb->data[6]);
		break;

	default:
		printk(KERN_WARNING "ROSE: received unknown %02X with LCI 000\n", frametype);
		break;
	}

	if (neigh->restarted) {
		while ((skbn = skb_dequeue(&neigh->queue)) != NULL)
			if (!rose_send_frame(skbn, neigh))
				kfree_skb(skbn);
	}
}

/*
 *	This routine is called when a Restart Request is needed
 */
static void rose_transmit_restart_request(struct rose_neigh *neigh)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 3;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 3);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ROSE_GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_RESTART_REQUEST;
	*dptr++ = ROSE_DTE_ORIGINATED;
	*dptr++ = 0;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb);
}

/*
 * This routine is called when a Restart Confirmation is needed
 */
static void rose_transmit_restart_confirmation(struct rose_neigh *neigh)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 1;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 1);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ROSE_GFI;
	*dptr++ = 0x00;
	*dptr++ = ROSE_RESTART_CONFIRMATION;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb);
}

/*
 * This routine is called when a Clear Request is needed outside of the context
 * of a connected socket.
 */
void rose_transmit_clear_request(struct rose_neigh *neigh, unsigned int lci, unsigned char cause, unsigned char diagnostic)
{
	struct sk_buff *skb;
	unsigned char *dptr;
	int len;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 3;

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN);

	dptr = skb_put(skb, ROSE_MIN_LEN + 3);

	*dptr++ = AX25_P_ROSE;
	*dptr++ = ((lci >> 8) & 0x0F) | ROSE_GFI;
	*dptr++ = ((lci >> 0) & 0xFF);
	*dptr++ = ROSE_CLEAR_REQUEST;
	*dptr++ = cause;
	*dptr++ = diagnostic;

	if (!rose_send_frame(skb, neigh))
		kfree_skb(skb);
}

void rose_transmit_link(struct sk_buff *skb, struct rose_neigh *neigh)
{
	unsigned char *dptr;

#if 0
	if (call_fw_firewall(PF_ROSE, skb->dev, skb->data, NULL, &skb) != FW_ACCEPT) {
		kfree_skb(skb);
		return;
	}
#endif

	if (neigh->loopback) {
		rose_loopback_queue(skb, neigh);
		return;
	}

	if (!rose_link_up(neigh))
		neigh->restarted = 0;

	dptr = skb_push(skb, 1);
	*dptr++ = AX25_P_ROSE;

	if (neigh->restarted) {
		if (!rose_send_frame(skb, neigh))
			kfree_skb(skb);
	} else {
		skb_queue_tail(&neigh->queue, skb);

		if (!rose_t0timer_running(neigh)) {
			rose_transmit_restart_request(neigh);
			neigh->dce_mode = 0;
			rose_start_t0timer(neigh);
		}
	}
}
