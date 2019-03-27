/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define NDIS_DEFAULT_NODENAME	"FreeBSD NDIS node"
#define NDIS_NODENAME_LEN	32

/* For setting/getting OIDs from userspace. */

struct ndis_oid_data {
	uint32_t		oid;
	uint32_t		len;
#ifdef notdef
	uint8_t			data[1];
#endif
};

struct ndis_pci_type {
	uint16_t		ndis_vid;
	uint16_t		ndis_did;
	uint32_t		ndis_subsys;
	char			*ndis_name;
};

struct ndis_pccard_type {
	const char		*ndis_vid;
	const char		*ndis_did;
	char			*ndis_name;
};

struct ndis_usb_type {
	uint16_t		ndis_vid;
	uint16_t		ndis_did;
	char			*ndis_name;
};

struct ndis_shmem {
	list_entry		ndis_list;
	bus_dma_tag_t		ndis_stag;
	bus_dmamap_t		ndis_smap;
	void			*ndis_saddr;
	ndis_physaddr		ndis_paddr;
};

struct ndis_cfglist {
	ndis_cfg		ndis_cfg;
	struct sysctl_oid	*ndis_oid;
        TAILQ_ENTRY(ndis_cfglist)	link;
};

/*
 * Helper struct to make parsing information
 * elements easier.
 */
struct ndis_ie {
	uint8_t		ni_oui[3];
	uint8_t		ni_val;
};

TAILQ_HEAD(nch, ndis_cfglist);

#define NDIS_INITIALIZED(sc)	(sc->ndis_block->nmb_devicectx != NULL)

#define NDIS_TXPKTS 64
#define NDIS_INC(x)		\
	(x)->ndis_txidx = ((x)->ndis_txidx + 1) % (x)->ndis_maxpkts


#define NDIS_EVENTS 4
#define NDIS_EVTINC(x)	(x) = ((x) + 1) % NDIS_EVENTS

struct ndis_evt {
	uint32_t		ne_sts;
	uint32_t		ne_len;
	char			*ne_buf;
};

struct ndis_vap {
	struct ieee80211vap	vap;

	int			(*newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
};
#define	NDIS_VAP(vap)	((struct ndis_vap *)(vap))

#define	NDISUSB_CONFIG_NO			0
#define	NDISUSB_IFACE_INDEX			0
/* XXX at USB2 there's no USBD_NO_TIMEOUT macro anymore  */
#define	NDISUSB_NO_TIMEOUT			0
#define	NDISUSB_INTR_TIMEOUT			1000
#define	NDISUSB_TX_TIMEOUT			10000
struct ndisusb_xfer;
struct ndisusb_ep {
	struct usb_xfer	*ne_xfer[1];
	list_entry		ne_active;
	list_entry		ne_pending;
	kspin_lock		ne_lock;
	uint8_t			ne_dirin;
};
struct ndisusb_xfer {
	struct ndisusb_ep	*nx_ep;
	void			*nx_priv;
	uint8_t			*nx_urbbuf;
	uint32_t		nx_urbactlen;
	uint32_t		nx_urblen;
	uint8_t			nx_shortxfer;
	list_entry		nx_next;
};
struct ndisusb_xferdone {
	struct ndisusb_xfer	*nd_xfer;
	usb_error_t		nd_status;
	list_entry		nd_donelist;
};

struct ndisusb_task {
	unsigned		nt_type;
#define	NDISUSB_TASK_TSTART	0
#define	NDISUSB_TASK_IRPCANCEL	1
#define	NDISUSB_TASK_VENDOR	2
	void			*nt_ctx;
	list_entry		nt_tasklist;
};

struct ndis_softc {
#define        NDISUSB_GET_IFNET(ndis_softc) ( (ndis_softc)->ndis_80211 ? NULL : (ndis_softc)->ifp )
	u_int			ndis_80211:1,
				ndis_link:1,
				ndis_running:1;
	union {
		struct {		/* Ethernet */
			struct ifnet		*ifp;
			struct ifmedia		ifmedia;
			int			ndis_if_flags;
		};
		struct {		/* Wireless */
			struct ieee80211com	ndis_ic;
			struct callout		ndis_scan_callout;
			int	(*ndis_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);
		};
	};
	u_long			ndis_hwassist;
	uint32_t		ndis_v4tx;
	uint32_t		ndis_v4rx;
	bus_space_handle_t	ndis_bhandle;
	bus_space_tag_t		ndis_btag;
	void			*ndis_intrhand;
	struct resource		*ndis_irq;
	struct resource		*ndis_res;
	struct resource		*ndis_res_io;
	int			ndis_io_rid;
	struct resource		*ndis_res_mem;
	int			ndis_mem_rid;
	struct resource		*ndis_res_altmem;
	int			ndis_altmem_rid;
	struct resource		*ndis_res_am;	/* attribute mem (pccard) */
	int			ndis_am_rid;
	struct resource		*ndis_res_cm;	/* common mem (pccard) */
	struct resource_list	ndis_rl;
	int			ndis_rescnt;
	struct mtx		ndis_mtx;
	uint8_t			ndis_irql;
	device_t		ndis_dev;
	int			ndis_unit;
	ndis_miniport_block	*ndis_block;
	ndis_miniport_characteristics	*ndis_chars;
	interface_type		ndis_type;
	struct callout		ndis_stat_callout;
	int			ndis_maxpkts;
	ndis_oid		*ndis_oids;
	int			ndis_oidcnt;
	int			ndis_txidx;
	int			ndis_txpending;
	ndis_packet		**ndis_txarray;
	ndis_handle		ndis_txpool;
	int			ndis_sc;
	ndis_cfg		*ndis_regvals;
	struct nch		ndis_cfglist_head;
	uint32_t		ndis_sts;
	uint32_t		ndis_filter;
	int			ndis_skip;
	int			ndis_devidx;
	interface_type		ndis_iftype;
	driver_object		*ndis_dobj;
	io_workitem		*ndis_tickitem;
	io_workitem		*ndis_startitem;
	io_workitem		*ndis_resetitem;
	io_workitem		*ndis_inputitem;
	kdpc			ndis_rxdpc;
	bus_dma_tag_t		ndis_parent_tag;
	list_entry		ndis_shlist;
	bus_dma_tag_t		ndis_mtag;
	bus_dma_tag_t		ndis_ttag;
	bus_dmamap_t		*ndis_mmaps;
	bus_dmamap_t		*ndis_tmaps;
	int			ndis_mmapcnt;
	struct ndis_evt		ndis_evt[NDIS_EVENTS];
	int			ndis_evtpidx;
	int			ndis_evtcidx;
	struct mbufq		ndis_rxqueue;
	kspin_lock		ndis_rxlock;

	int			ndis_tx_timer;
	int			ndis_hang_timer;

	struct usb_device	*ndisusb_dev;
	struct mtx		ndisusb_mtx;
	struct ndisusb_ep	ndisusb_dread_ep;
	struct ndisusb_ep	ndisusb_dwrite_ep;
#define	NDISUSB_GET_ENDPT(addr) \
	((UE_GET_DIR(addr) >> 7) | (UE_GET_ADDR(addr) << 1))
#define	NDISUSB_ENDPT_MAX	((UE_ADDR + 1) * 2)
	struct ndisusb_ep	ndisusb_ep[NDISUSB_ENDPT_MAX];
	io_workitem		*ndisusb_xferdoneitem;
	list_entry		ndisusb_xferdonelist;
	kspin_lock		ndisusb_xferdonelock;
	io_workitem		*ndisusb_taskitem;
	list_entry		ndisusb_tasklist;
	kspin_lock		ndisusb_tasklock;
	int			ndisusb_status;
#define NDISUSB_STATUS_DETACH	0x1
#define	NDISUSB_STATUS_SETUP_EP	0x2
};

#define	NDIS_LOCK(_sc)		mtx_lock(&(_sc)->ndis_mtx)
#define	NDIS_UNLOCK(_sc)	mtx_unlock(&(_sc)->ndis_mtx)
#define	NDIS_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->ndis_mtx, t)
#define	NDISUSB_LOCK(_sc)	mtx_lock(&(_sc)->ndisusb_mtx)
#define	NDISUSB_UNLOCK(_sc)	mtx_unlock(&(_sc)->ndisusb_mtx)
#define	NDISUSB_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->ndisusb_mtx, t)

