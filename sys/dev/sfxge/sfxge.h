/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 *
 * $FreeBSD$
 */

#ifndef _SFXGE_H
#define	_SFXGE_H

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <vm/uma.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include "sfxge_ioc.h"

/*
 * Debugging
 */
#if 0
#define	DBGPRINT(dev, fmt, args...) \
	device_printf(dev, "%s: " fmt "\n", __func__, ## args)
#else
#define	DBGPRINT(dev, fmt, args...)
#endif

/*
 * Backward-compatibility
 */
#ifndef CACHE_LINE_SIZE
/* This should be right on most machines the driver will be used on, and
 * we needn't care too much about wasting a few KB per interface.
 */
#define	CACHE_LINE_SIZE 128
#endif

#ifndef IFCAP_LINKSTATE
#define	IFCAP_LINKSTATE 0
#endif

#ifndef IFCAP_VLAN_HWTSO
#define	IFCAP_VLAN_HWTSO 0
#endif

#ifndef IFM_10G_T
#define	IFM_10G_T IFM_UNKNOWN
#endif

#ifndef IFM_10G_KX4
#define	IFM_10G_KX4 IFM_10G_CX4
#endif

#ifndef IFM_40G_CR4
#define	IFM_40G_CR4 IFM_UNKNOWN
#endif

#if (__FreeBSD_version >= 800501 && __FreeBSD_version < 900000) || \
	__FreeBSD_version >= 900003
#define	SFXGE_HAVE_DESCRIBE_INTR
#endif

#ifdef IFM_ETH_RXPAUSE
#define	SFXGE_HAVE_PAUSE_MEDIAOPTS
#endif

#ifndef CTLTYPE_U64
#define	CTLTYPE_U64 CTLTYPE_QUAD
#endif

#include "sfxge_rx.h"
#include "sfxge_tx.h"

#define	ROUNDUP_POW_OF_TWO(_n)	(1ULL << flsl((_n) - 1))

#define	SFXGE_IP_ALIGN	2

#define	SFXGE_ETHERTYPE_LOOPBACK	0x9000	/* Xerox loopback */


#define	SFXGE_MAGIC_RESERVED		0x8000

#define	SFXGE_MAGIC_DMAQ_LABEL_WIDTH	6
#define	SFXGE_MAGIC_DMAQ_LABEL_MASK \
	((1 << SFXGE_MAGIC_DMAQ_LABEL_WIDTH) - 1)

enum sfxge_sw_ev {
	SFXGE_SW_EV_RX_QFLUSH_DONE = 1,
	SFXGE_SW_EV_RX_QFLUSH_FAILED,
	SFXGE_SW_EV_RX_QREFILL,
	SFXGE_SW_EV_TX_QFLUSH_DONE,
};

#define	SFXGE_SW_EV_MAGIC(_sw_ev) \
	(SFXGE_MAGIC_RESERVED | ((_sw_ev) << SFXGE_MAGIC_DMAQ_LABEL_WIDTH))

static inline uint16_t
sfxge_sw_ev_mk_magic(enum sfxge_sw_ev sw_ev, unsigned int label)
{
	KASSERT((label & SFXGE_MAGIC_DMAQ_LABEL_MASK) == label,
	    ("(label & SFXGE_MAGIC_DMAQ_LABEL_MASK) != label"));
	return SFXGE_SW_EV_MAGIC(sw_ev) | label;
}

static inline uint16_t
sfxge_sw_ev_rxq_magic(enum sfxge_sw_ev sw_ev, struct sfxge_rxq *rxq)
{
	return sfxge_sw_ev_mk_magic(sw_ev, 0);
}

static inline uint16_t
sfxge_sw_ev_txq_magic(enum sfxge_sw_ev sw_ev, struct sfxge_txq *txq)
{
	return sfxge_sw_ev_mk_magic(sw_ev, txq->type);
}

enum sfxge_evq_state {
	SFXGE_EVQ_UNINITIALIZED = 0,
	SFXGE_EVQ_INITIALIZED,
	SFXGE_EVQ_STARTING,
	SFXGE_EVQ_STARTED
};

#define	SFXGE_EV_BATCH	16384

#define	SFXGE_STATS_UPDATE_PERIOD_MS	1000

struct sfxge_evq {
	/* Structure members below are sorted by usage order */
	struct sfxge_softc	*sc;
	struct mtx		lock;
	unsigned int		index;
	enum sfxge_evq_state	init_state;
	efsys_mem_t		mem;
	efx_evq_t		*common;
	unsigned int		read_ptr;
	boolean_t		exception;
	unsigned int		rx_done;
	unsigned int		tx_done;

	/* Linked list of TX queues with completions to process */
	struct sfxge_txq	*txq;
	struct sfxge_txq	**txqs;

	/* Structure members not used on event processing path */
	unsigned int		buf_base_id;
	unsigned int		entries;
	char			lock_name[SFXGE_LOCK_NAME_MAX];
#if EFSYS_OPT_QSTATS
	clock_t			stats_update_time;
	uint64_t		stats[EV_NQSTATS];
#endif
} __aligned(CACHE_LINE_SIZE);

#define	SFXGE_NDESCS	1024
#define	SFXGE_MODERATION	30

enum sfxge_intr_state {
	SFXGE_INTR_UNINITIALIZED = 0,
	SFXGE_INTR_INITIALIZED,
	SFXGE_INTR_TESTING,
	SFXGE_INTR_STARTED
};

struct sfxge_intr_hdl {
	int			eih_rid;
	void			*eih_tag;
	struct resource		*eih_res;
};

struct sfxge_intr {
	enum sfxge_intr_state	state;
	struct resource		*msix_res;
	struct sfxge_intr_hdl	*table;
	int			n_alloc;
	int			type;
	efsys_mem_t		status;
	uint32_t		zero_count;
};

enum sfxge_mcdi_state {
	SFXGE_MCDI_UNINITIALIZED = 0,
	SFXGE_MCDI_INITIALIZED,
	SFXGE_MCDI_BUSY,
	SFXGE_MCDI_COMPLETED
};

struct sfxge_mcdi {
	struct mtx		lock;
	efsys_mem_t		mem;
	enum sfxge_mcdi_state	state;
	efx_mcdi_transport_t	transport;

	/* Only used in debugging output */
	char			lock_name[SFXGE_LOCK_NAME_MAX];
};

struct sfxge_hw_stats {
	clock_t			update_time;
	efsys_mem_t		dma_buf;
	void			*decode_buf;
};

enum sfxge_port_state {
	SFXGE_PORT_UNINITIALIZED = 0,
	SFXGE_PORT_INITIALIZED,
	SFXGE_PORT_STARTED
};

struct sfxge_port {
	struct sfxge_softc	*sc;
	struct mtx		lock;
	enum sfxge_port_state	init_state;
#ifndef SFXGE_HAVE_PAUSE_MEDIAOPTS
	unsigned int		wanted_fc;
#endif
	struct sfxge_hw_stats	phy_stats;
	struct sfxge_hw_stats	mac_stats;
	uint16_t		stats_update_period_ms;
	efx_link_mode_t		link_mode;
	uint8_t			mcast_addrs[EFX_MAC_MULTICAST_LIST_MAX *
					    EFX_MAC_ADDR_LEN];
	unsigned int		mcast_count;

	/* Only used in debugging output */
	char			lock_name[SFXGE_LOCK_NAME_MAX];
};

enum sfxge_softc_state {
	SFXGE_UNINITIALIZED = 0,
	SFXGE_INITIALIZED,
	SFXGE_REGISTERED,
	SFXGE_STARTED
};

struct sfxge_softc {
	device_t			dev;
	struct sx			softc_lock;
	char				softc_lock_name[SFXGE_LOCK_NAME_MAX];
	enum sfxge_softc_state		init_state;
	struct ifnet			*ifnet;
	unsigned int			if_flags;
	struct sysctl_oid		*stats_node;
#if EFSYS_OPT_QSTATS
	struct sysctl_oid		*evqs_stats_node;
#endif
	struct sysctl_oid		*txqs_node;

	struct task			task_reset;

	efx_family_t			family;
	unsigned int			mem_bar;

	caddr_t				vpd_data;
	size_t				vpd_size;
	efx_nic_t			*enp;
	efsys_lock_t			enp_lock;

	boolean_t			txq_dynamic_cksum_toggle_supported;

	unsigned int			rxq_entries;
	unsigned int			txq_entries;

	bus_dma_tag_t			parent_dma_tag;
	efsys_bar_t			bar;

	struct sfxge_intr		intr;
	struct sfxge_mcdi		mcdi;
	struct sfxge_port		port;
	uint32_t			buffer_table_next;

	struct sfxge_evq		*evq[SFXGE_RX_SCALE_MAX];
	unsigned int			ev_moderation;
#if EFSYS_OPT_QSTATS
	clock_t				ev_stats_update_time;
	uint64_t			ev_stats[EV_NQSTATS];
#endif

	unsigned int			max_rss_channels;
	struct sfxge_rxq		*rxq[SFXGE_RX_SCALE_MAX];
	unsigned int			rx_indir_table[EFX_RSS_TBL_SIZE];

	struct sfxge_txq		*txq[SFXGE_TXQ_NTYPES + SFXGE_RX_SCALE_MAX];

	struct ifmedia			media;

	size_t				rx_prefix_size;
	size_t				rx_buffer_size;
	size_t				rx_buffer_align;
	int				rx_cluster_size;

	unsigned int			evq_max;
	unsigned int			evq_count;
	unsigned int			rxq_count;
	unsigned int			txq_count;

	unsigned int			tso_fw_assisted;
#define	SFXGE_FATSOV1	(1 << 0)
#define	SFXGE_FATSOV2	(1 << 1)

#if EFSYS_OPT_MCDI_LOGGING
	int				mcdi_logging;
#endif
};

#define	SFXGE_LINK_UP(sc) \
	((sc)->port.link_mode != EFX_LINK_DOWN && \
	 (sc)->port.link_mode != EFX_LINK_UNKNOWN)
#define	SFXGE_RUNNING(sc) ((sc)->ifnet->if_drv_flags & IFF_DRV_RUNNING)

#define	SFXGE_PARAM(_name)	"hw.sfxge." #_name

SYSCTL_DECL(_hw_sfxge);

/*
 * From sfxge.c.
 */
extern void sfxge_schedule_reset(struct sfxge_softc *sc);
extern void sfxge_sram_buf_tbl_alloc(struct sfxge_softc *sc, size_t n,
				     uint32_t *idp);

/*
 * From sfxge_dma.c.
 */
extern int sfxge_dma_init(struct sfxge_softc *sc);
extern void sfxge_dma_fini(struct sfxge_softc *sc);
extern int sfxge_dma_alloc(struct sfxge_softc *sc, bus_size_t len,
			   efsys_mem_t *esmp);
extern void sfxge_dma_free(efsys_mem_t *esmp);
extern int sfxge_dma_map_sg_collapse(bus_dma_tag_t tag, bus_dmamap_t map,
				     struct mbuf **mp,
				     bus_dma_segment_t *segs,
				     int *nsegs, int maxsegs);

/*
 * From sfxge_ev.c.
 */
extern int sfxge_ev_init(struct sfxge_softc *sc);
extern void sfxge_ev_fini(struct sfxge_softc *sc);
extern int sfxge_ev_start(struct sfxge_softc *sc);
extern void sfxge_ev_stop(struct sfxge_softc *sc);
extern int sfxge_ev_qpoll(struct sfxge_evq *evq);

/*
 * From sfxge_intr.c.
 */
extern int sfxge_intr_init(struct sfxge_softc *sc);
extern void sfxge_intr_fini(struct sfxge_softc *sc);
extern int sfxge_intr_start(struct sfxge_softc *sc);
extern void sfxge_intr_stop(struct sfxge_softc *sc);

/*
 * From sfxge_mcdi.c.
 */
extern int sfxge_mcdi_init(struct sfxge_softc *sc);
extern void sfxge_mcdi_fini(struct sfxge_softc *sc);
extern int sfxge_mcdi_ioctl(struct sfxge_softc *sc, sfxge_ioc_t *ip);

/*
 * From sfxge_nvram.c.
 */
extern int sfxge_nvram_ioctl(struct sfxge_softc *sc, sfxge_ioc_t *ip);

/*
 * From sfxge_port.c.
 */
extern int sfxge_port_init(struct sfxge_softc *sc);
extern void sfxge_port_fini(struct sfxge_softc *sc);
extern int sfxge_port_start(struct sfxge_softc *sc);
extern void sfxge_port_stop(struct sfxge_softc *sc);
extern void sfxge_mac_link_update(struct sfxge_softc *sc,
				  efx_link_mode_t mode);
extern int sfxge_mac_filter_set(struct sfxge_softc *sc);
extern int sfxge_port_ifmedia_init(struct sfxge_softc *sc);
extern uint64_t sfxge_get_counter(struct ifnet *ifp, ift_counter c);

#define	SFXGE_MAX_MTU (9 * 1024)

#define	SFXGE_ADAPTER_LOCK_INIT(_sc, _ifname)				\
	do {								\
		struct sfxge_softc *__sc = (_sc);			\
									\
		snprintf((__sc)->softc_lock_name,			\
			 sizeof((__sc)->softc_lock_name),		\
			 "%s:softc", (_ifname));			\
		sx_init(&(__sc)->softc_lock, (__sc)->softc_lock_name);	\
	} while (B_FALSE)
#define	SFXGE_ADAPTER_LOCK_DESTROY(_sc)					\
	sx_destroy(&(_sc)->softc_lock)
#define	SFXGE_ADAPTER_LOCK(_sc)						\
	sx_xlock(&(_sc)->softc_lock)
#define	SFXGE_ADAPTER_UNLOCK(_sc)					\
	sx_xunlock(&(_sc)->softc_lock)
#define	SFXGE_ADAPTER_LOCK_ASSERT_OWNED(_sc)				\
	sx_assert(&(_sc)->softc_lock, LA_XLOCKED)

#define	SFXGE_PORT_LOCK_INIT(_port, _ifname)				\
	do {								\
		struct sfxge_port *__port = (_port);			\
									\
		snprintf((__port)->lock_name,				\
			 sizeof((__port)->lock_name),			\
			 "%s:port", (_ifname));				\
		mtx_init(&(__port)->lock, (__port)->lock_name,		\
			 NULL, MTX_DEF);				\
	} while (B_FALSE)
#define	SFXGE_PORT_LOCK_DESTROY(_port)					\
	mtx_destroy(&(_port)->lock)
#define	SFXGE_PORT_LOCK(_port)						\
	mtx_lock(&(_port)->lock)
#define	SFXGE_PORT_UNLOCK(_port)					\
	mtx_unlock(&(_port)->lock)
#define	SFXGE_PORT_LOCK_ASSERT_OWNED(_port)				\
	mtx_assert(&(_port)->lock, MA_OWNED)

#define	SFXGE_MCDI_LOCK_INIT(_mcdi, _ifname)				\
	do {								\
		struct sfxge_mcdi  *__mcdi = (_mcdi);			\
									\
		snprintf((__mcdi)->lock_name,				\
			 sizeof((__mcdi)->lock_name),			\
			 "%s:mcdi", (_ifname));				\
		mtx_init(&(__mcdi)->lock, (__mcdi)->lock_name,		\
			 NULL, MTX_DEF);				\
	} while (B_FALSE)
#define	SFXGE_MCDI_LOCK_DESTROY(_mcdi)					\
	mtx_destroy(&(_mcdi)->lock)
#define	SFXGE_MCDI_LOCK(_mcdi)						\
	mtx_lock(&(_mcdi)->lock)
#define	SFXGE_MCDI_UNLOCK(_mcdi)					\
	mtx_unlock(&(_mcdi)->lock)
#define	SFXGE_MCDI_LOCK_ASSERT_OWNED(_mcdi)				\
	mtx_assert(&(_mcdi)->lock, MA_OWNED)

#define	SFXGE_EVQ_LOCK_INIT(_evq, _ifname, _evq_index)			\
	do {								\
		struct sfxge_evq  *__evq = (_evq);			\
									\
		snprintf((__evq)->lock_name,				\
			 sizeof((__evq)->lock_name),			\
			 "%s:evq%u", (_ifname), (_evq_index));		\
		mtx_init(&(__evq)->lock, (__evq)->lock_name,		\
			 NULL, MTX_DEF);				\
	} while (B_FALSE)
#define	SFXGE_EVQ_LOCK_DESTROY(_evq)					\
	mtx_destroy(&(_evq)->lock)
#define	SFXGE_EVQ_LOCK(_evq)						\
	mtx_lock(&(_evq)->lock)
#define	SFXGE_EVQ_UNLOCK(_evq)						\
	mtx_unlock(&(_evq)->lock)
#define	SFXGE_EVQ_LOCK_ASSERT_OWNED(_evq)				\
	mtx_assert(&(_evq)->lock, MA_OWNED)

#endif /* _SFXGE_H */
