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
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/rose.h>

static int rose_create_facilities(unsigned char *buffer, struct rose_sock *rose);

/*
 *	This routine purges all of the queues of frames.
 */
void rose_clear_queues(struct sock *sk)
{
	skb_queue_purge(&sk->sk_write_queue);
	skb_queue_purge(&rose_sk(sk)->ack_queue);
}

/*
 * This routine purges the input queue of those frames that have been
 * acknowledged. This replaces the boxes labelled "V(a) <- N(r)" on the
 * SDL diagram.
 */
void rose_frames_acked(struct sock *sk, unsigned short nr)
{
	struct sk_buff *skb;
	struct rose_sock *rose = rose_sk(sk);

	/*
	 * Remove all the ack-ed frames from the ack queue.
	 */
	if (rose->va != nr) {
		while (skb_peek(&rose->ack_queue) != NULL && rose->va != nr) {
			skb = skb_dequeue(&rose->ack_queue);
			kfree_skb(skb);
			rose->va = (rose->va + 1) % ROSE_MODULUS;
		}
	}
}

void rose_requeue_frames(struct sock *sk)
{
	struct sk_buff *skb, *skb_prev = NULL;

	/*
	 * Requeue all the un-ack-ed frames on the output queue to be picked
	 * up by rose_kick. This arrangement handles the possibility of an
	 * empty output queue.
	 */
	while ((skb = skb_dequeue(&rose_sk(sk)->ack_queue)) != NULL) {
		if (skb_prev == NULL)
			skb_queue_head(&sk->sk_write_queue, skb);
		else
			skb_append(skb_prev, skb, &sk->sk_write_queue);
		skb_prev = skb;
	}
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int rose_validate_nr(struct sock *sk, unsigned short nr)
{
	struct rose_sock *rose = rose_sk(sk);
	unsigned short vc = rose->va;

	while (vc != rose->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % ROSE_MODULUS;
	}

	return nr == rose->vs;
}

/*
 *  This routine is called when the packet layer internally generates a
 *  control frame.
 */
void rose_write_internal(struct sock *sk, int frametype)
{
	struct rose_sock *rose = rose_sk(sk);
	struct sk_buff *skb;
	unsigned char  *dptr;
	unsigned char  lci1, lci2;
	char buffer[100];
	int len, faclen = 0;

	len = AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN + 1;

	switch (frametype) {
	case ROSE_CALL_REQUEST:
		len   += 1 + ROSE_ADDR_LEN + ROSE_ADDR_LEN;
		faclen = rose_create_facilities(buffer, rose);
		len   += faclen;
		break;
	case ROSE_CALL_ACCEPTED:
	case ROSE_CLEAR_REQUEST:
	case ROSE_RESET_REQUEST:
		len   += 2;
		break;
	}

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	/*
	 *	Space for AX.25 header and PID.
	 */
	skb_reserve(skb, AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + 1);

	dptr = skb_put(skb, skb_tailroom(skb));

	lci1 = (rose->lci >> 8) & 0x0F;
	lci2 = (rose->lci >> 0) & 0xFF;

	switch (frametype) {
	case ROSE_CALL_REQUEST:
		*dptr++ = ROSE_GFI | lci1;
		*dptr++ = lci2;
		*dptr++ = frametype;
		*dptr++ = ROSE_CALL_REQ_ADDR_LEN_VAL;
		memcpy(dptr, &rose->dest_addr,  ROSE_ADDR_LEN);
		dptr   += ROSE_ADDR_LEN;
		memcpy(dptr, &rose->source_addr, ROSE_ADDR_LEN);
		dptr   += ROSE_ADDR_LEN;
		memcpy(dptr, buffer, faclen);
		dptr   += faclen;
		break;

	case ROSE_CALL_ACCEPTED:
		*dptr++ = ROSE_GFI | lci1;
		*dptr++ = lci2;
		*dptr++ = frametype;
		*dptr++ = 0x00;		/* Address length */
		*dptr++ = 0;		/* Facilities length */
		break;

	case ROSE_CLEAR_REQUEST:
		*dptr++ = ROSE_GFI | lci1;
		*dptr++ = lci2;
		*dptr++ = frametype;
		*dptr++ = rose->cause;
		*dptr++ = rose->diagnostic;
		break;

	case ROSE_RESET_REQUEST:
		*dptr++ = ROSE_GFI | lci1;
		*dptr++ = lci2;
		*dptr++ = frametype;
		*dptr++ = ROSE_DTE_ORIGINATED;
		*dptr++ = 0;
		break;

	case ROSE_RR:
	case ROSE_RNR:
		*dptr++ = ROSE_GFI | lci1;
		*dptr++ = lci2;
		*dptr   = frametype;
		*dptr++ |= (rose->vr << 5) & 0xE0;
		break;

	case ROSE_CLEAR_CONFIRMATION:
	case ROSE_RESET_CONFIRMATION:
		*dptr++ = ROSE_GFI | lci1;
		*dptr++ = lci2;
		*dptr++  = frametype;
		break;

	default:
		printk(KERN_ERR "ROSE: rose_write_internal - invalid frametype %02X\n", frametype);
		kfree_skb(skb);
		return;
	}

	rose_transmit_link(skb, rose->neighbour);
}

int rose_decode(struct sk_buff *skb, int *ns, int *nr, int *q, int *d, int *m)
{
	unsigned char *frame;

	frame = skb->data;

	*ns = *nr = *q = *d = *m = 0;

	switch (frame[2]) {
	case ROSE_CALL_REQUEST:
	case ROSE_CALL_ACCEPTED:
	case ROSE_CLEAR_REQUEST:
	case ROSE_CLEAR_CONFIRMATION:
	case ROSE_RESET_REQUEST:
	case ROSE_RESET_CONFIRMATION:
		return frame[2];
	default:
		break;
	}

	if ((frame[2] & 0x1F) == ROSE_RR  ||
	    (frame[2] & 0x1F) == ROSE_RNR) {
		*nr = (frame[2] >> 5) & 0x07;
		return frame[2] & 0x1F;
	}

	if ((frame[2] & 0x01) == ROSE_DATA) {
		*q  = (frame[0] & ROSE_Q_BIT) == ROSE_Q_BIT;
		*d  = (frame[0] & ROSE_D_BIT) == ROSE_D_BIT;
		*m  = (frame[2] & ROSE_M_BIT) == ROSE_M_BIT;
		*nr = (frame[2] >> 5) & 0x07;
		*ns = (frame[2] >> 1) & 0x07;
		return ROSE_DATA;
	}

	return ROSE_ILLEGAL;
}

static int rose_parse_national(unsigned char *p, struct rose_facilities_struct *facilities, int len)
{
	unsigned char *pt;
	unsigned char l, lg, n = 0;
	int fac_national_digis_received = 0;

	do {
		switch (*p & 0xC0) {
		case 0x00:
			if (len < 2)
				return -1;
			p   += 2;
			n   += 2;
			len -= 2;
			break;

		case 0x40:
			if (len < 3)
				return -1;
			if (*p == FAC_NATIONAL_RAND)
				facilities->rand = ((p[1] << 8) & 0xFF00) + ((p[2] << 0) & 0x00FF);
			p   += 3;
			n   += 3;
			len -= 3;
			break;

		case 0x80:
			if (len < 4)
				return -1;
			p   += 4;
			n   += 4;
			len -= 4;
			break;

		case 0xC0:
			if (len < 2)
				return -1;
			l = p[1];
			if (len < 2 + l)
				return -1;
			if (*p == FAC_NATIONAL_DEST_DIGI) {
				if (!fac_national_digis_received) {
					if (l < AX25_ADDR_LEN)
						return -1;
					memcpy(&facilities->source_digis[0], p + 2, AX25_ADDR_LEN);
					facilities->source_ndigis = 1;
				}
			}
			else if (*p == FAC_NATIONAL_SRC_DIGI) {
				if (!fac_national_digis_received) {
					if (l < AX25_ADDR_LEN)
						return -1;
					memcpy(&facilities->dest_digis[0], p + 2, AX25_ADDR_LEN);
					facilities->dest_ndigis = 1;
				}
			}
			else if (*p == FAC_NATIONAL_FAIL_CALL) {
				if (l < AX25_ADDR_LEN)
					return -1;
				memcpy(&facilities->fail_call, p + 2, AX25_ADDR_LEN);
			}
			else if (*p == FAC_NATIONAL_FAIL_ADD) {
				if (l < 1 + ROSE_ADDR_LEN)
					return -1;
				memcpy(&facilities->fail_addr, p + 3, ROSE_ADDR_LEN);
			}
			else if (*p == FAC_NATIONAL_DIGIS) {
				if (l % AX25_ADDR_LEN)
					return -1;
				fac_national_digis_received = 1;
				facilities->source_ndigis = 0;
				facilities->dest_ndigis   = 0;
				for (pt = p + 2, lg = 0 ; lg < l ; pt += AX25_ADDR_LEN, lg += AX25_ADDR_LEN) {
					if (pt[6] & AX25_HBIT) {
						if (facilities->dest_ndigis >= ROSE_MAX_DIGIS)
							return -1;
						memcpy(&facilities->dest_digis[facilities->dest_ndigis++], pt, AX25_ADDR_LEN);
					} else {
						if (facilities->source_ndigis >= ROSE_MAX_DIGIS)
							return -1;
						memcpy(&facilities->source_digis[facilities->source_ndigis++], pt, AX25_ADDR_LEN);
					}
				}
			}
			p   += l + 2;
			n   += l + 2;
			len -= l + 2;
			break;
		}
	} while (*p != 0x00 && len > 0);

	return n;
}

static int rose_parse_ccitt(unsigned char *p, struct rose_facilities_struct *facilities, int len)
{
	unsigned char l, n = 0;
	char callsign[11];

	do {
		switch (*p & 0xC0) {
		case 0x00:
			if (len < 2)
				return -1;
			p   += 2;
			n   += 2;
			len -= 2;
			break;

		case 0x40:
			if (len < 3)
				return -1;
			p   += 3;
			n   += 3;
			len -= 3;
			break;

		case 0x80:
			if (len < 4)
				return -1;
			p   += 4;
			n   += 4;
			len -= 4;
			break;

		case 0xC0:
			if (len < 2)
				return -1;
			l = p[1];

			/* Prevent overflows*/
			if (l < 10 || l > 20)
				return -1;

			if (*p == FAC_CCITT_DEST_NSAP) {
				memcpy(&facilities->source_addr, p + 7, ROSE_ADDR_LEN);
				memcpy(callsign, p + 12,   l - 10);
				callsign[l - 10] = '\0';
				asc2ax(&facilities->source_call, callsign);
			}
			if (*p == FAC_CCITT_SRC_NSAP) {
				memcpy(&facilities->dest_addr, p + 7, ROSE_ADDR_LEN);
				memcpy(callsign, p + 12, l - 10);
				callsign[l - 10] = '\0';
				asc2ax(&facilities->dest_call, callsign);
			}
			p   += l + 2;
			n   += l + 2;
			len -= l + 2;
			break;
		}
	} while (*p != 0x00 && len > 0);

	return n;
}

int rose_parse_facilities(unsigned char *p, unsigned packet_len,
	struct rose_facilities_struct *facilities)
{
	int facilities_len, len;

	facilities_len = *p++;

	if (facilities_len == 0 || (unsigned)facilities_len > packet_len)
		return 0;

	while (facilities_len >= 3 && *p == 0x00) {
		facilities_len--;
		p++;

		switch (*p) {
		case FAC_NATIONAL:		/* National */
			len = rose_parse_national(p + 1, facilities, facilities_len - 1);
			break;

		case FAC_CCITT:		/* CCITT */
			len = rose_parse_ccitt(p + 1, facilities, facilities_len - 1);
			break;

		default:
			printk(KERN_DEBUG "ROSE: rose_parse_facilities - unknown facilities family %02X\n", *p);
			len = 1;
			break;
		}

		if (len < 0)
			return 0;
		if (WARN_ON(len >= facilities_len))
			return 0;
		facilities_len -= len + 1;
		p += len + 1;
	}

	return facilities_len == 0;
}

static int rose_create_facilities(unsigned char *buffer, struct rose_sock *rose)
{
	unsigned char *p = buffer + 1;
	char *callsign;
	char buf[11];
	int len, nb;

	/* National Facilities */
	if (rose->rand != 0 || rose->source_ndigis == 1 || rose->dest_ndigis == 1) {
		*p++ = 0x00;
		*p++ = FAC_NATIONAL;

		if (rose->rand != 0) {
			*p++ = FAC_NATIONAL_RAND;
			*p++ = (rose->rand >> 8) & 0xFF;
			*p++ = (rose->rand >> 0) & 0xFF;
		}

		/* Sent before older facilities */
		if ((rose->source_ndigis > 0) || (rose->dest_ndigis > 0)) {
			int maxdigi = 0;
			*p++ = FAC_NATIONAL_DIGIS;
			*p++ = AX25_ADDR_LEN * (rose->source_ndigis + rose->dest_ndigis);
			for (nb = 0 ; nb < rose->source_ndigis ; nb++) {
				if (++maxdigi >= ROSE_MAX_DIGIS)
					break;
				memcpy(p, &rose->source_digis[nb], AX25_ADDR_LEN);
				p[6] |= AX25_HBIT;
				p += AX25_ADDR_LEN;
			}
			for (nb = 0 ; nb < rose->dest_ndigis ; nb++) {
				if (++maxdigi >= ROSE_MAX_DIGIS)
					break;
				memcpy(p, &rose->dest_digis[nb], AX25_ADDR_LEN);
				p[6] &= ~AX25_HBIT;
				p += AX25_ADDR_LEN;
			}
		}

		/* For compatibility */
		if (rose->source_ndigis > 0) {
			*p++ = FAC_NATIONAL_SRC_DIGI;
			*p++ = AX25_ADDR_LEN;
			memcpy(p, &rose->source_digis[0], AX25_ADDR_LEN);
			p   += AX25_ADDR_LEN;
		}

		/* For compatibility */
		if (rose->dest_ndigis > 0) {
			*p++ = FAC_NATIONAL_DEST_DIGI;
			*p++ = AX25_ADDR_LEN;
			memcpy(p, &rose->dest_digis[0], AX25_ADDR_LEN);
			p   += AX25_ADDR_LEN;
		}
	}

	*p++ = 0x00;
	*p++ = FAC_CCITT;

	*p++ = FAC_CCITT_DEST_NSAP;

	callsign = ax2asc(buf, &rose->dest_call);

	*p++ = strlen(callsign) + 10;
	*p++ = (strlen(callsign) + 9) * 2;		/* ??? */

	*p++ = 0x47; *p++ = 0x00; *p++ = 0x11;
	*p++ = ROSE_ADDR_LEN * 2;
	memcpy(p, &rose->dest_addr, ROSE_ADDR_LEN);
	p   += ROSE_ADDR_LEN;

	memcpy(p, callsign, strlen(callsign));
	p   += strlen(callsign);

	*p++ = FAC_CCITT_SRC_NSAP;

	callsign = ax2asc(buf, &rose->source_call);

	*p++ = strlen(callsign) + 10;
	*p++ = (strlen(callsign) + 9) * 2;		/* ??? */

	*p++ = 0x47; *p++ = 0x00; *p++ = 0x11;
	*p++ = ROSE_ADDR_LEN * 2;
	memcpy(p, &rose->source_addr, ROSE_ADDR_LEN);
	p   += ROSE_ADDR_LEN;

	memcpy(p, callsign, strlen(callsign));
	p   += strlen(callsign);

	len       = p - buffer;
	buffer[0] = len - 1;

	return len;
}

void rose_disconnect(struct sock *sk, int reason, int cause, int diagnostic)
{
	struct rose_sock *rose = rose_sk(sk);

	rose_stop_timer(sk);
	rose_stop_idletimer(sk);

	rose_clear_queues(sk);

	rose->lci   = 0;
	rose->state = ROSE_STATE_0;

	if (cause != -1)
		rose->cause = cause;

	if (diagnostic != -1)
		rose->diagnostic = diagnostic;

	sk->sk_state     = TCP_CLOSE;
	sk->sk_err       = reason;
	sk->sk_shutdown |= SEND_SHUTDOWN;

	if (!sock_flag(sk, SOCK_DEAD)) {
		sk->sk_state_change(sk);
		sock_set_flag(sk, SOCK_DEAD);
	}
}
