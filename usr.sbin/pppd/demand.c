/*	$OpenBSD: demand.c,v 1.13 2024/08/10 05:32:28 jsg Exp $	*/

/*
 * demand.c - Support routines for demand-dialling.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#ifdef PPP_FILTER
#include <net/if.h>
#include <net/bpf.h>
#include <pcap.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>

#include "pppd.h"
#include "fsm.h"
#include "ipcp.h"
#include "lcp.h"

char *frame;
int framelen;
int framemax;
int escape_flag;
int flush_flag;
int fcs;

struct packet {
    int length;
    struct packet *next;
    unsigned char data[1];
};

struct packet *pend_q;
struct packet *pend_qtail;

static int active_packet(unsigned char *, int);

/*
 * demand_conf - configure the interface for doing dial-on-demand.
 */
void
demand_conf(void)
{
    int i;
    struct protent *protp;

/*    framemax = lcp_allowoptions[0].mru;
    if (framemax < PPP_MRU) */
	framemax = PPP_MRU;
    framemax += PPP_HDRLEN + PPP_FCSLEN;
    frame = malloc(framemax);
    if (frame == NULL)
	novm("demand frame");
    framelen = 0;
    pend_q = NULL;
    escape_flag = 0;
    flush_flag = 0;
    fcs = PPP_INITFCS;

    ppp_send_config(0, PPP_MRU, (u_int32_t) 0, 0, 0);
    ppp_recv_config(0, PPP_MRU, (u_int32_t) 0, 0, 0);

#ifdef PPP_FILTER
    set_filters(&pass_filter, &active_filter);
#endif

    /*
     * Call the demand_conf procedure for each protocol that's got one.
     */
    for (i = 0; (protp = protocols[i]) != NULL; ++i)
	if (protp->enabled_flag && protp->demand_conf != NULL)
	    if (!((*protp->demand_conf)(0)))
		die(1);
}

/*
 * demand_drop - set each network protocol to discard packets
 * without an error.
 */
void
demand_drop(void)
{
    struct packet *pkt, *nextpkt;
    int i;
    struct protent *protp;

    for (i = 0; (protp = protocols[i]) != NULL; ++i)
        if (protp->enabled_flag && protp->demand_conf != NULL)
            sifnpmode(0, protp->protocol & ~0x8000, NPMODE_DROP);
    get_loop_output();

    /* discard all saved packets */
    for (pkt = pend_q; pkt != NULL; pkt = nextpkt) {
        nextpkt = pkt->next;
        free(pkt);
    }
    pend_q = NULL;
    framelen = 0;
    flush_flag = 0;
    escape_flag = 0;
    fcs = PPP_INITFCS;
}

/*
 * demand_unblock - set each enabled network protocol to pass packets.
 */
void
demand_unblock(void)
{
    int i;
    struct protent *protp;

    for (i = 0; (protp = protocols[i]) != NULL; ++i)
	if (protp->enabled_flag && protp->demand_conf != NULL)
	    sifnpmode(0, protp->protocol & ~0x8000, NPMODE_PASS);
}

/*
 * FCS lookup table as calculated by genfcstab.
 */
static u_short fcstab[256] = {
	0x0000,	0x1189,	0x2312,	0x329b,	0x4624,	0x57ad,	0x6536,	0x74bf,
	0x8c48,	0x9dc1,	0xaf5a,	0xbed3,	0xca6c,	0xdbe5,	0xe97e,	0xf8f7,
	0x1081,	0x0108,	0x3393,	0x221a,	0x56a5,	0x472c,	0x75b7,	0x643e,
	0x9cc9,	0x8d40,	0xbfdb,	0xae52,	0xdaed,	0xcb64,	0xf9ff,	0xe876,
	0x2102,	0x308b,	0x0210,	0x1399,	0x6726,	0x76af,	0x4434,	0x55bd,
	0xad4a,	0xbcc3,	0x8e58,	0x9fd1,	0xeb6e,	0xfae7,	0xc87c,	0xd9f5,
	0x3183,	0x200a,	0x1291,	0x0318,	0x77a7,	0x662e,	0x54b5,	0x453c,
	0xbdcb,	0xac42,	0x9ed9,	0x8f50,	0xfbef,	0xea66,	0xd8fd,	0xc974,
	0x4204,	0x538d,	0x6116,	0x709f,	0x0420,	0x15a9,	0x2732,	0x36bb,
	0xce4c,	0xdfc5,	0xed5e,	0xfcd7,	0x8868,	0x99e1,	0xab7a,	0xbaf3,
	0x5285,	0x430c,	0x7197,	0x601e,	0x14a1,	0x0528,	0x37b3,	0x263a,
	0xdecd,	0xcf44,	0xfddf,	0xec56,	0x98e9,	0x8960,	0xbbfb,	0xaa72,
	0x6306,	0x728f,	0x4014,	0x519d,	0x2522,	0x34ab,	0x0630,	0x17b9,
	0xef4e,	0xfec7,	0xcc5c,	0xddd5,	0xa96a,	0xb8e3,	0x8a78,	0x9bf1,
	0x7387,	0x620e,	0x5095,	0x411c,	0x35a3,	0x242a,	0x16b1,	0x0738,
	0xffcf,	0xee46,	0xdcdd,	0xcd54,	0xb9eb,	0xa862,	0x9af9,	0x8b70,
	0x8408,	0x9581,	0xa71a,	0xb693,	0xc22c,	0xd3a5,	0xe13e,	0xf0b7,
	0x0840,	0x19c9,	0x2b52,	0x3adb,	0x4e64,	0x5fed,	0x6d76,	0x7cff,
	0x9489,	0x8500,	0xb79b,	0xa612,	0xd2ad,	0xc324,	0xf1bf,	0xe036,
	0x18c1,	0x0948,	0x3bd3,	0x2a5a,	0x5ee5,	0x4f6c,	0x7df7,	0x6c7e,
	0xa50a,	0xb483,	0x8618,	0x9791,	0xe32e,	0xf2a7,	0xc03c,	0xd1b5,
	0x2942,	0x38cb,	0x0a50,	0x1bd9,	0x6f66,	0x7eef,	0x4c74,	0x5dfd,
	0xb58b,	0xa402,	0x9699,	0x8710,	0xf3af,	0xe226,	0xd0bd,	0xc134,
	0x39c3,	0x284a,	0x1ad1,	0x0b58,	0x7fe7,	0x6e6e,	0x5cf5,	0x4d7c,
	0xc60c,	0xd785,	0xe51e,	0xf497,	0x8028,	0x91a1,	0xa33a,	0xb2b3,
	0x4a44,	0x5bcd,	0x6956,	0x78df,	0x0c60,	0x1de9,	0x2f72,	0x3efb,
	0xd68d,	0xc704,	0xf59f,	0xe416,	0x90a9,	0x8120,	0xb3bb,	0xa232,
	0x5ac5,	0x4b4c,	0x79d7,	0x685e,	0x1ce1,	0x0d68,	0x3ff3,	0x2e7a,
	0xe70e,	0xf687,	0xc41c,	0xd595,	0xa12a,	0xb0a3,	0x8238,	0x93b1,
	0x6b46,	0x7acf,	0x4854,	0x59dd,	0x2d62,	0x3ceb,	0x0e70,	0x1ff9,
	0xf78f,	0xe606,	0xd49d,	0xc514,	0xb1ab,	0xa022,	0x92b9,	0x8330,
	0x7bc7,	0x6a4e,	0x58d5,	0x495c,	0x3de3,	0x2c6a,	0x1ef1,	0x0f78
};

/*
 * loop_chars - process characters received from the loopback.
 * Calls loop_frame when a complete frame has been accumulated.
 * Return value is 1 if we need to bring up the link, 0 otherwise.
 */
int
loop_chars(unsigned char *p, int n)
{
    int c, rv;

    rv = 0;
    for (; n > 0; --n) {
	c = *p++;
	if (c == PPP_FLAG) {
	    if (!escape_flag && !flush_flag
		&& framelen > 2 && fcs == PPP_GOODFCS) {
		framelen -= 2;
		if (loop_frame(frame, framelen))
		    rv = 1;
	    }
	    framelen = 0;
	    flush_flag = 0;
	    escape_flag = 0;
	    fcs = PPP_INITFCS;
	    continue;
	}
	if (flush_flag)
	    continue;
	if (escape_flag) {
	    c ^= PPP_TRANS;
	    escape_flag = 0;
	} else if (c == PPP_ESCAPE) {
	    escape_flag = 1;
	    continue;
	}
	if (framelen >= framemax) {
	    flush_flag = 1;
	    continue;
	}
	frame[framelen++] = c;
	fcs = PPP_FCS(fcs, c);
    }
    return rv;
}

/*
 * loop_frame - given a frame obtained from the loopback,
 * decide whether to bring up the link or not, and, if we want
 * to transmit this frame later, put it on the pending queue.
 * Return value is 1 if we need to bring up the link, 0 otherwise.
 * We assume that the kernel driver has already applied the
 * pass_filter, so we won't get packets it rejected.
 * We apply the active_filter to see if we want this packet to
 * bring up the link.
 */
int
loop_frame(unsigned char *frame, int len)
{
    struct packet *pkt;

    /* log_packet(frame, len, "from loop: ", LOG_DEBUG); */
    if (len < PPP_HDRLEN)
	return 0;
    if ((PPP_PROTOCOL(frame) & 0x8000) != 0)
	return 0;		/* shouldn't get any of these anyway */
    if (!active_packet(frame, len))
	return 0;

    pkt = (struct packet *) malloc(sizeof(struct packet) + len);
    if (pkt != NULL) {
	pkt->length = len;
	pkt->next = NULL;
	memcpy(pkt->data, frame, len);
	if (pend_q == NULL)
	    pend_q = pkt;
	else
	    pend_qtail->next = pkt;
	pend_qtail = pkt;
    }
    return 1;
}

/*
 * demand_rexmit - Resend all those frames which we got via the
 * loopback, now that the real serial link is up.
 */
void
demand_rexmit(int proto)
{
    struct packet *pkt, *prev, *nextpkt;

    prev = NULL;
    pkt = pend_q;
    pend_q = NULL;
    for (; pkt != NULL; pkt = nextpkt) {
	nextpkt = pkt->next;
	if (PPP_PROTOCOL(pkt->data) == proto) {
	    output(0, pkt->data, pkt->length);
	    free(pkt);
	} else {
	    if (prev == NULL)
		pend_q = pkt;
	    else
		prev->next = pkt;
	    prev = pkt;
	}
    }
    pend_qtail = prev;
    if (prev != NULL)
	prev->next = NULL;
}

/*
 * Scan a packet to decide whether it is an "active" packet,
 * that is, whether it is worth bringing up the link for.
 */
static int
active_packet(unsigned char *p, int len)
{
    int proto, i;
    struct protent *protp;

    if (len < PPP_HDRLEN)
	return 0;
    proto = PPP_PROTOCOL(p);
#ifdef PPP_FILTER
    if (active_filter.bf_len != 0
	&& bpf_filter(active_filter.bf_insns, frame, len, len) == 0)
	return 0;
#endif
    for (i = 0; (protp = protocols[i]) != NULL; ++i) {
	if (protp->protocol < 0xC000 && (protp->protocol & ~0x8000) == proto) {
	    if (!protp->enabled_flag)
		return 0;
	    if (protp->active_pkt == NULL)
		return 1;
	    return (*protp->active_pkt)(p, len);
	}
    }
    return 0;			/* not a supported protocol !!?? */
}
