/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Aleksandr Rybalko <ray@ddteam.net>
 * Copyright (c) 2009-2010 Alexander Egorenkov <egorenar@gmail.com>
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
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
 *
 * $FreeBSD$
 */

#ifndef _IF_RTVAR_H_
#define	_IF_RTVAR_H_

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/taskqueue.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include "opt_if_rt.h"

#define	RT_SOFTC_LOCK(sc)		mtx_lock(&(sc)->lock)
#define	RT_SOFTC_UNLOCK(sc)		mtx_unlock(&(sc)->lock)
#define	RT_SOFTC_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->lock, MA_OWNED)

#define	RT_SOFTC_TX_RING_LOCK(ring)		mtx_lock(&(ring)->lock)
#define	RT_SOFTC_TX_RING_UNLOCK(ring)		mtx_unlock(&(ring)->lock)
#define	RT_SOFTC_TX_RING_ASSERT_LOCKED(ring)	\
		    mtx_assert(&(ring)->lock, MA_OWNED)

#define	RT_SOFTC_TX_RING_COUNT		4
#define	RT_SOFTC_RX_RING_COUNT		4

#ifndef IF_RT_RING_DATA_COUNT
#define	IF_RT_RING_DATA_COUNT	128
#endif

#define	RT_SOFTC_RX_RING_DATA_COUNT	IF_RT_RING_DATA_COUNT

#define	RT_SOFTC_MAX_SCATTER		10

#define	RT_SOFTC_TX_RING_DATA_COUNT	(IF_RT_RING_DATA_COUNT/4)
#define	RT_SOFTC_TX_RING_DESC_COUNT				\
	(RT_SOFTC_TX_RING_DATA_COUNT * RT_SOFTC_MAX_SCATTER)

#define	RT_TXDESC_SDL1_BURST		(1 << 15)
#define	RT_TXDESC_SDL1_LASTSEG		(1 << 14)
#define	RT_TXDESC_SDL0_DDONE		(1 << 15)
#define	RT_TXDESC_SDL0_LASTSEG		(1 << 14)
struct rt_txdesc
{
	uint32_t sdp0;
	uint16_t sdl1;
	uint16_t sdl0;
	uint32_t sdp1;
	uint8_t vid;
#define	TXDSCR_INS_VLAN_TAG	0x80
#define	TXDSCR_VLAN_PRIO_MASK	0x70
#define	TXDSCR_VLAN_IDX_MASK	0x0f
	uint8_t	pppoe;
#define	TXDSCR_USR_DEF_FLD	0x80
#define	TXDSCR_INS_PPPOE_HDR	0x10
#define	TXDSCR_PPPOE_SID_MASK	0x0f
	uint8_t qn;
#define	TXDSCR_QUEUE_MASK	0x07
	uint8_t	dst;
#define	TXDSCR_IP_CSUM_GEN	0x80
#define	TXDSCR_UDP_CSUM_GEN	0x40
#define	TXDSCR_TCP_CSUM_GEN	0x20
#define	TXDSCR_DST_PORT_MASK	0x07
#define	TXDSCR_DST_PORT_CPU	0x00
#define	TXDSCR_DST_PORT_GDMA1	0x01
#define	TXDSCR_DST_PORT_GDMA2	0x02
#define	TXDSCR_DST_PORT_PPE	0x06
#define	TXDSCR_DST_PORT_DISC	0x07
} __packed;

#define	RT_RXDESC_SDL0_DDONE		(1 << 15)

#define RT305X_RXD_SRC_L4_CSUM_FAIL	(1 << 28)
#define RT305X_RXD_SRC_IP_CSUM_FAIL	(1 << 29)
#define MT7620_RXD_SRC_L4_CSUM_FAIL	(1 << 22)
#define MT7620_RXD_SRC_IP_CSUM_FAIL	(1 << 25)
#define MT7621_RXD_SRC_L4_CSUM_FAIL	(1 << 23)
#define MT7621_RXD_SRC_IP_CSUM_FAIL	(1 << 26)

struct rt_rxdesc
{
	uint32_t sdp0;
	uint16_t sdl1;
	uint16_t sdl0;
	uint32_t sdp1;
#if 0
	uint16_t foe;
#define	RXDSXR_FOE_ENTRY_VALID		0x40
#define	RXDSXR_FOE_ENTRY_MASK		0x3f
	uint8_t ai;
#define	RXDSXR_AI_COU_REASON		0xff
#define	RXDSXR_AI_PARSER_RSLT_MASK	0xff
	uint8_t src;
#define	RXDSXR_SRC_IPFVLD		0x80
#define	RXDSXR_SRC_L4FVLD		0x40
#define	RXDSXR_SRC_IP_CSUM_FAIL	0x20
#define	RXDSXR_SRC_L4_CSUM_FAIL	0x10
#define	RXDSXR_SRC_AIS			0x08
#define	RXDSXR_SRC_PORT_MASK		0x07
#endif
	uint32_t word3;
} __packed;

struct rt_softc_rx_data
{
	bus_dmamap_t dma_map;
	struct mbuf *m;
};

struct rt_softc_rx_ring
{
	bus_dma_tag_t desc_dma_tag;
	bus_dmamap_t desc_dma_map;
	bus_addr_t desc_phys_addr;
	struct rt_rxdesc *desc;
	bus_dma_tag_t data_dma_tag;
	bus_dmamap_t spare_dma_map;
	struct rt_softc_rx_data data[RT_SOFTC_RX_RING_DATA_COUNT];
	int cur;
	int qid;
};

struct rt_softc_tx_data
{
	bus_dmamap_t dma_map;
	struct mbuf *m;
};

struct rt_softc_tx_ring
{
	struct mtx lock;
	bus_dma_tag_t desc_dma_tag;
	bus_dmamap_t desc_dma_map;
	bus_addr_t desc_phys_addr;
	struct rt_txdesc *desc;
	int desc_queued;
	int desc_cur;
	int desc_next;
	bus_dma_tag_t seg0_dma_tag;
	bus_dmamap_t seg0_dma_map;
	bus_addr_t seg0_phys_addr;
	uint8_t *seg0;
	bus_dma_tag_t data_dma_tag;
	struct rt_softc_tx_data data[RT_SOFTC_TX_RING_DATA_COUNT];
	int data_queued;
	int data_cur;
	int data_next;
	int qid;
};

struct rt_softc
{
	device_t 	 dev;
	struct mtx 	 lock;
	uint32_t 	 flags;

	int		 mem_rid;
	struct resource	*mem;
	int		 irq_rid;
	struct resource *irq;
	void		*irqh;

	bus_space_tag_t	 bst;
	bus_space_handle_t bsh;

	struct ifnet	*ifp;
	int 		 if_flags;
	struct ifmedia	 rt_ifmedia;

	uint32_t	 mac_rev;
	uint8_t		 mac_addr[ETHER_ADDR_LEN];
	device_t	 rt_miibus;

	uint32_t	 intr_enable_mask;
	uint32_t	 intr_disable_mask;
	uint32_t	 intr_pending_mask;

	struct task	 rx_done_task;
	int		 rx_process_limit;
	struct task	 tx_done_task;
	struct task	 periodic_task;
	struct callout	 periodic_ch;
	unsigned long	 periodic_round;
	struct taskqueue *taskqueue;

	struct rt_softc_rx_ring rx_ring[RT_SOFTC_RX_RING_COUNT];
	struct rt_softc_tx_ring tx_ring[RT_SOFTC_TX_RING_COUNT];
	int		 tx_ring_mgtqid;

	struct callout	 tx_watchdog_ch;
	int		 tx_timer;

	/* statistic counters */
	unsigned long	 interrupts;
	unsigned long	 tx_coherent_interrupts;
	unsigned long	 rx_coherent_interrupts;
	unsigned long	 rx_interrupts[RT_SOFTC_RX_RING_COUNT];
	unsigned long	 rx_delay_interrupts;
	unsigned long	 tx_interrupts[RT_SOFTC_TX_RING_COUNT];
	unsigned long	 tx_delay_interrupts;
	unsigned long	 tx_data_queue_full[RT_SOFTC_TX_RING_COUNT];
	unsigned long	 tx_watchdog_timeouts;
	unsigned long	 tx_defrag_packets;
	unsigned long	 no_tx_desc_avail;
	unsigned long	 rx_mbuf_alloc_errors;
	unsigned long	 rx_mbuf_dmamap_errors;
	unsigned long	 tx_queue_not_empty[2];

	unsigned long	 rx_bytes;
	unsigned long	 rx_packets;
	unsigned long	 rx_crc_err;
	unsigned long	 rx_phy_err;
	unsigned long	 rx_dup_packets;
	unsigned long	 rx_fifo_overflows;
	unsigned long	 rx_short_err;
	unsigned long	 rx_long_err;
	unsigned long	 tx_bytes;
	unsigned long	 tx_packets;
	unsigned long	 tx_skip;
	unsigned long	 tx_collision;

	int		 phy_addr;

#ifdef IF_RT_DEBUG
	int		 debug;
#endif

        uint32_t        rt_chipid;
        /* chip specific registers config */
        int		rx_ring_count;
	uint32_t	csum_fail_l4;
	uint32_t	csum_fail_ip;
        uint32_t	int_rx_done_mask;
        uint32_t	int_tx_done_mask;
        uint32_t        delay_int_cfg;
        uint32_t        fe_int_status;
        uint32_t        fe_int_enable;
        uint32_t        pdma_glo_cfg;
        uint32_t        pdma_rst_idx;
	uint32_t	gdma1_base;
        uint32_t        tx_base_ptr[RT_SOFTC_TX_RING_COUNT];
        uint32_t        tx_max_cnt[RT_SOFTC_TX_RING_COUNT];
        uint32_t        tx_ctx_idx[RT_SOFTC_TX_RING_COUNT];
        uint32_t        tx_dtx_idx[RT_SOFTC_TX_RING_COUNT];
        uint32_t        rx_base_ptr[RT_SOFTC_RX_RING_COUNT];
        uint32_t        rx_max_cnt[RT_SOFTC_RX_RING_COUNT];
        uint32_t        rx_calc_idx[RT_SOFTC_RX_RING_COUNT];
        uint32_t        rx_drx_idx[RT_SOFTC_RX_RING_COUNT];
};

#ifdef IF_RT_DEBUG
enum
{
	RT_DEBUG_RX = 0x00000001,
	RT_DEBUG_TX = 0x00000002,
	RT_DEBUG_INTR = 0x00000004,
	RT_DEBUG_STATE = 0x00000008,
	RT_DEBUG_STATS = 0x00000010,
	RT_DEBUG_PERIODIC = 0x00000020,
	RT_DEBUG_WATCHDOG = 0x00000040,
	RT_DEBUG_ANY = 0xffffffff
};

#define	RT_DPRINTF(sc, m, fmt, ...)		\
	do { if ((sc)->debug & (m)) 		\
	    device_printf(sc->dev, fmt, ## __VA_ARGS__); } while (0)
#else
#define	RT_DPRINTF(sc, m, fmt, ...)
#endif /* #ifdef IF_RT_DEBUG */

#endif /* #ifndef _IF_RTVAR_H_ */
