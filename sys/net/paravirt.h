/*
 * Copyright (C) 2013 Luigi Rizzo. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef NET_PARAVIRT_H
#define NET_PARAVIRT_H

 /*
  * $FreeBSD$
  *
 Support for virtio-like communication between host (H) and guest (G) NICs.

 THIS IS EXPERIMENTAL CODE AND SUBJECT TO CHANGE.

 The guest allocates the shared Communication Status Block (csb) and
 write its physical address at CSBAL and CSBAH (data is little endian).
 csb->csb_on enables the mode. If disabled, the device acts a regular one.

 Notifications for tx and rx are exchanged without vm exits
 if possible. In particular (only mentioning csb mode below),
 the following actions are performed. In the description below,
 "double check" means verifying again the condition that caused
 the previous action, and reverting the action if the condition has
 changed. The condition typically depends on a variable set by the
 other party, and the double check is done to avoid races. E.g.

	// start with A=0
    again:
	// do something
	if ( cond(C) ) { // C is written by the other side
	    A = 1;
	    // barrier
	    if ( !cond(C) ) {
		A = 0;
		goto again;
	    }
	}

 TX: start from idle:
    H starts with host_need_txkick=1 when the I/O thread bh is idle. Upon new
    transmissions, G always updates guest_tdt.  If host_need_txkick == 1,
    G also writes to the TDT, which acts as a kick to H (so pending
    writes are always dispatched to H as soon as possible.)

 TX: active state:
    On the kick (TDT write) H sets host_need_txkick == 0 (if not
    done already by G), and starts an I/O thread trying to consume
    packets from TDH to guest_tdt, periodically refreshing host_tdh
    and TDH.  When host_tdh == guest_tdt, H sets host_need_txkick=1,
    and then does the "double check" for race avoidance.

 TX: G runs out of buffers
    XXX there are two mechanisms, one boolean (using guest_need_txkick)
    and one with a threshold (using guest_txkick_at). They are mutually
    exclusive.
    BOOLEAN: when G has no space, it sets guest_need_txkick=1 and does
        the double check. If H finds guest_need_txkick== 1 on a write
        to TDH, it also generates an interrupt.
    THRESHOLD: G sets guest_txkick_at to the TDH value for which it
	wants to receive an interrupt. When H detects that TDH moves
	across guest_txkick_at, it generates an interrupt.
	This second mechanism reduces the number of interrupts and
	TDT writes on the transmit side when the host is too slow.

 RX: start from idle
    G starts with guest_need_rxkick = 1 when the receive ring is empty.
    As packets arrive, H updates host_rdh (and RDH) and also generates an
    interrupt when guest_need_rxkick == 1 (so incoming packets are
    always reported to G as soon as possible, apart from interrupt
    moderation delays). It also tracks guest_rdt for new buffers.

 RX: active state
    As the interrupt arrives, G sets guest_need_rxkick = 0 and starts
    draining packets from the receive ring, while updating guest_rdt
    When G runs out of packets it sets guest_need_rxkick=1 and does the
    double check.

 RX: H runs out of buffers
    XXX there are two mechanisms, one boolean (using host_need_rxkick)
    and one with a threshold (using host_xxkick_at). They are mutually
    exclusive.
    BOOLEAN: when H has no space, it sets host_need_rxkick=1 and does the
	double check. If G finds host_need_rxkick==1 on updating guest_rdt,
        it also writes to RDT causing a kick to H.
    THRESHOLD: H sets host_rxkick_at to the RDT value for which it wants
	to receive a kick. When G detects that guest_rdt moves across
	host_rxkick_at, it writes to RDT thus generates a kick.
	This second mechanism reduces the number of kicks and
        RDT writes on the receive side when the guest is too slow and
	would free only a few buffers at a time.

 */
struct paravirt_csb {
    /* XXX revise the layout to minimize cache bounces.
     * Usage is described as follows:
     * 	[GH][RW][+-0]	guest/host reads/writes frequently/rarely/almost never
     */
    /* these are (mostly) written by the guest */
    uint32_t guest_tdt;            /* GW+ HR+ pkt to transmit */
    uint32_t guest_need_txkick;    /* GW- HR+ G ran out of tx bufs, request kick */
    uint32_t guest_need_rxkick;    /* GW- HR+ G ran out of rx pkts, request kick  */
    uint32_t guest_csb_on;         /* GW- HR+ enable paravirtual mode */
    uint32_t guest_rdt;            /* GW+ HR+ rx buffers available */
    uint32_t guest_txkick_at;      /* GW- HR+ tx ring pos. where G expects an intr */
    uint32_t guest_use_msix;        /* GW0 HR0 guest uses MSI-X interrupts. */
    uint32_t pad[9];

    /* these are (mostly) written by the host */
    uint32_t host_tdh;             /* GR0 HW- shadow register, mostly unused */
    uint32_t host_need_txkick;     /* GR+ HW- start the iothread */
    uint32_t host_txcycles_lim;    /* GW- HR- how much to spin before  sleep.
				    * set by the guest */
    uint32_t host_txcycles;        /* GR0 HW- counter, but no need to be exported */
    uint32_t host_rdh;             /* GR0 HW- shadow register, mostly unused */
    uint32_t host_need_rxkick;     /* GR+ HW- flush rx queued packets */
    uint32_t host_isr;             /* GR* HW* shadow copy of ISR */
    uint32_t host_rxkick_at;       /* GR+ HW- rx ring pos where H expects a kick */
    uint32_t vnet_ring_high;	/* Vnet ring physical address high. */
    uint32_t vnet_ring_low;	/* Vnet ring physical address low. */
};

#define NET_PARAVIRT_CSB_SIZE   4096
#define NET_PARAVIRT_NONE   (~((uint32_t)0))

#ifdef	QEMU_PCI_H

/*
 * API functions only available within QEMU
 */

void paravirt_configure_csb(struct paravirt_csb** csb, uint32_t csbbal,
			uint32_t csbbah, QEMUBH* tx_bh, AddressSpace *as);

#endif /* QEMU_PCI_H */

#endif /* NET_PARAVIRT_H */
