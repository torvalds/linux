/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Marvell Semiconductor, Inc.
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Weongyo Jeong <weongyo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

/*
 * Definitions for the Marvell 88W8335 Wireless LAN controller.
 */
#ifndef _DEV_MALO_H
#define _DEV_MALO_H

#include <net80211/ieee80211_radiotap.h>
#include <dev/malo/if_malohal.h>
#include <dev/malo/if_maloioctl.h>

#ifndef MALO_TXBUF
#define MALO_TXBUF		256	/* number of TX descriptors/buffers */
#endif
#ifndef MALO_RXBUF
#define MALO_RXBUF		256	/* number of RX descriptors/buffers */
#endif

#define	MALO_TXDESC		1	/* max tx descriptors/segments */

#define	MALO_RXSIZE		PAGE_SIZE
#define	MALO_RSSI_DUMMY_MARKER	127
#define	MALO_RSSI_EP_MULTIPLIER	(1<<7)	/* pow2 to optimize out * and / */

#define MALO_REG_INT_CODE			0x00000C14
/* From host to ARM */
#define MALO_REG_H2A_INTERRUPT_EVENTS		0x00000C18

/* bit definitions for MALO_REG_H2A_INTERRUPT_CAUSE */
#define MALO_H2ARIC_BIT_PPA_READY		0x00000001
#define MALO_H2ARIC_BIT_DOOR_BELL		0x00000002 /* bit 1 */
#define MALO_H2ARIC_BIT_PS			0x00000004
#define MALO_H2ARIC_BIT_PSPOLL			0x00000008 /* bit 3 */

/* From ARM to host */
#define MALO_REG_A2H_INTERRUPT_CAUSE		0x00000C30
#define MALO_REG_A2H_INTERRUPT_MASK		0x00000C34
#define MALO_REG_A2H_INTERRUPT_CLEAR_SEL	0x00000C38
#define MALO_REG_A2H_INTERRUPT_STATUS_MASK	0x00000C3C

/* bit definitions for MALO_REG_A2H_INTERRUPT_CAUSE */
#define MALO_A2HRIC_BIT_TX_DONE			0x00000001	/* bit 0 */
#define MALO_A2HRIC_BIT_RX_RDY			0x00000002	/* bit 1 */
#define MALO_A2HRIC_BIT_OPC_DONE		0x00000004
#define MALO_A2HRIC_BIT_MAC_EVENT		0x00000008
#define MALO_A2HRIC_BIT_RX_PROBLEM		0x00000010
#define MALO_A2HRIC_BIT_RADIO_OFF		0x00000020	/* bit 5 */
#define MALO_A2HRIC_BIT_RADIO_ON		0x00000040
#define MALO_A2HRIC_BIT_RADAR_DETECT		0x00000080
#define MALO_A2HRIC_BIT_ICV_ERROR		0x00000100
#define MALO_A2HRIC_BIT_MIC_ERROR		0x00000200	/* bit 9 */
#define MALO_A2HRIC_BIT_QUEUE_EMPTY		0x00000400
#define MALO_A2HRIC_BIT_QUEUE_FULL		0x00000800
#define MALO_A2HRIC_BIT_CHAN_SWITCH		0x00001000
#define MALO_A2HRIC_BIT_TX_WATCHDOG		0x00002000
#define MALO_A2HRIC_BIT_BA_WATCHDOG		0x00004000

#define MALO_ISR_SRC_BITS			\
	(MALO_A2HRIC_BIT_RX_RDY |		\
	 MALO_A2HRIC_BIT_TX_DONE |		\
	 MALO_A2HRIC_BIT_OPC_DONE |		\
	 MALO_A2HRIC_BIT_MAC_EVENT |		\
	 MALO_A2HRIC_BIT_MIC_ERROR |		\
	 MALO_A2HRIC_BIT_ICV_ERROR |		\
	 MALO_A2HRIC_BIT_RADAR_DETECT |		\
	 MALO_A2HRIC_BIT_CHAN_SWITCH |		\
	 MALO_A2HRIC_BIT_TX_WATCHDOG |		\
	 MALO_A2HRIC_BIT_QUEUE_EMPTY)
#define MALO_ISR_RESET				(1<<15)

#define MALO_A2HRIC_BIT_MASK			MALO_ISR_SRC_BITS

/* map to 0x80000000 on BAR1  */
#define MALO_REG_GEN_PTR			0x00000C10
#define MALO_REG_INT_CODE			0x00000C14
#define MALO_REG_SCRATCH			0x00000C40

/*
 * define OpMode for SoftAP/Station mode
 *
 * the following mode signature has to be written to PCI scratch register#0
 * right after successfully downloading the last block of firmware and
 * before waiting for firmware ready signature
 */
#define MALO_HOSTCMD_STA_MODE			0x5A
#define MALO_HOSTCMD_STA_FWRDY_SIGNATURE	0xF0F1F2F4

/*
 * 16 bit host command code
 */
#define MALO_HOSTCMD_NONE			0x0000
#define MALO_HOSTCMD_CODE_DNLD			0x0001
#define MALO_HOSTCMD_GET_HW_SPEC		0x0003
#define MALO_HOSTCMD_SET_HW_SPEC		0x0004
#define MALO_HOSTCMD_MAC_MULTICAST_ADR		0x0010
#define MALO_HOSTCMD_SET_WEPKEY			0x0013
#define MALO_HOSTCMD_802_11_RADIO_CONTROL	0x001c
#define MALO_HOSTCMD_802_11_RF_TX_POWER		0x001e
#define MALO_HOSTCMD_802_11_RF_ANTENNA		0x0020
#define MALO_HOSTCMD_SET_PRE_SCAN		0x0107
#define MALO_HOSTCMD_SET_POST_SCAN		0x0108
#define MALO_HOSTCMD_SET_RF_CHANNEL		0x010a
#define MALO_HOSTCMD_SET_AID			0x010d
#define MALO_HOSTCMD_SET_RATE			0x0110
#define MALO_HOSTCMD_SET_SLOT			0x0114
/* define DFS lab commands  */
#define MALO_HOSTCMD_SET_FIXED_RATE		0x0126 
#define MALO_HOSTCMD_SET_REGION_POWER		0x0128
#define MALO_HOSTCMD_GET_CALTABLE		0x1134

/*
 * definition of action or option for each command.
 */
/* define general purpose action  */
#define MALO_HOSTCMD_ACT_GEN_GET		0x0000
#define MALO_HOSTCMD_ACT_GEN_SET		0x0001
#define MALO_HOSTCMD_ACT_GEN_SET_LIST		0x0002

/* define action or option for HostCmd_FW_USE_FIXED_RATE */
#define MALO_HOSTCMD_ACT_USE_FIXED_RATE		0x0001
#define MALO_HOSTCMD_ACT_NOT_USE_FIXED_RATE	0x0002

/* INT code register event definition  */
#define MALO_INT_CODE_CMD_FINISHED		0x00000005

struct malo_cmd_header {
	uint16_t		cmd;
	uint16_t		length;
	uint16_t		seqnum;
	uint16_t		result; 
} __packed;  

struct malo_cmd_caltable {
	struct malo_cmd_header	cmdhdr;
	uint8_t			annex; 
	uint8_t			index;
	uint8_t			len;
	uint8_t			reserverd; 
#define MALO_CAL_TBL_SIZE	160
	uint8_t			caltbl[MALO_CAL_TBL_SIZE];
} __packed;

struct malo_cmd_get_hwspec {
	struct malo_cmd_header	cmdhdr;
	u_int8_t		version;	/* version of the HW  */
	u_int8_t		hostif;		/* host interface  */
	/* Max. number of WCB FW can handle  */
	u_int16_t		num_wcb;
	/* MaxNbr of MC addresses FW can handle */
	u_int16_t		num_mcastaddr;
	/* MAC address programmed in HW */
	u_int8_t		permaddr[6];
	u_int16_t		regioncode;
	/* Number of antenna used */
	u_int16_t		num_antenna;
	/* 4 byte of FW release number */
	u_int32_t		fw_releasenum;
	u_int32_t		wcbbase0;
	u_int32_t		rxpdwr_ptr;
	u_int32_t		rxpdrd_ptr;
	u_int32_t		ul_fw_awakecookie;
	u_int32_t		wcbbase1;
	u_int32_t		wcbbase2;
	u_int32_t		wcbbase3;
} __packed;

struct malo_cmd_set_hwspec {
	struct malo_cmd_header	cmdhdr;
	uint8_t			version;	/* HW revision */
	uint8_t			hostif;		/* Host interface */
	/* Max. number of Multicast address FW can handle */
	uint16_t		num_mcastaddr;
	uint8_t			permaddr[6];	/* MAC address */
	uint16_t		regioncode;	/* Region Code */
	/* 4 byte of FW release number */
	uint32_t		fwreleasenum;
	/* Firmware awake cookie */
	uint32_t		ul_fw_awakecookie;
	/* Device capabilities (see above) */
	uint32_t		devicecaps;
	uint32_t		rxpdwrptr;	/* Rx shared memory queue  */
	/* # TX queues in WcbBase array */
	uint32_t		num_txqueues;
	/* TX WCB Rings */
	uint32_t		wcbbase[MALO_MAX_TXWCB_QUEUES];
	uint32_t		flags;
	uint32_t		txwcbnum_per_queue;
	uint32_t		total_rxwcb;
} __packed;

/* DS 802.11 */
struct malo_cmd_rf_antenna {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;
	/* Number of antennas or 0xffff (diversity)  */
	uint16_t		mode;
} __packed;

struct malo_cmd_radio_control {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;                   
	/*
	 * bit 0 : 1 = on, 0 = off
	 * bit 1 : 1 = long, 0 = short
	 * bit 2 : 1 = auto, 0 = fix
	 */
	uint16_t		control;
	uint16_t		radio_on;
} __packed;

struct malo_cmd_fw_set_wmmmode {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;	/* 0 -> unset, 1 -> set  */
} __packed;

struct malo_cmd_fw_set_rf_channel {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;
	uint8_t			cur_channel;	/* channel # */
} __packed;

#define MALO_TX_POWER_LEVEL_TOTAL	8
struct malo_cmd_rf_tx_power {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;
	uint16_t		support_txpower_level;
	uint16_t		current_txpower_level;
	uint16_t		reserved;
	uint16_t		power_levellist[MALO_TX_POWER_LEVEL_TOTAL];
} __packed;

struct malo_fixrate_flag {
	/* lower rate after the retry count.  0 = legacy, 1 = HT  */
	uint32_t		type;
	/* 0: retry count is not valid, 1: use retry count specified  */
	uint32_t		retrycount_valid;
} __packed;

struct malo_fixed_rate_entry {
	struct malo_fixrate_flag typeflags;
	/* legacy rate(not index) or an MCS code.  */
	uint32_t		fixedrate;
	uint32_t		retrycount;
} __packed;

struct malo_cmd_fw_use_fixed_rate {
	struct malo_cmd_header	cmdhdr;
	/*
	 * MALO_HOSTCMD_ACT_GEN_GET	0x0000
	 * MALO_HOSTCMD_ACT_GEN_SET	0x0001
	 * MALO_HOSTCMD_ACT_NOT_USE_FIXED_RATE	0x0002
	 */
	uint32_t		action;
	/* use fixed rate specified but firmware can drop to  */
	uint32_t		allowratedrop;
	uint32_t		entrycount;
	struct malo_fixed_rate_entry fixedrate_table[4];
	uint8_t			multicast_rate;
	uint8_t			multirate_txtype;
	uint8_t			management_rate;
} __packed;

#define MALO_RATE_INDEX_MAX_ARRAY		14

struct malo_cmd_fw_set_aid {
	struct malo_cmd_header	cmdhdr;
	uint16_t		associd;
	uint8_t			macaddr[6];	/* AP's Mac Address(BSSID) */
	uint32_t		gprotection;
	uint8_t			aprates[MALO_RATE_INDEX_MAX_ARRAY];
} __packed;

struct malo_cmd_prescan {
	struct malo_cmd_header	cmdhdr;
} __packed;

struct malo_cmd_postscan {
	struct malo_cmd_header	cmdhdr;
	uint32_t		isibss;
	uint8_t			bssid[6];
} __packed;

struct malo_cmd_fw_setslot {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;
	/* slot = 0 if regular, slot = 1 if short.  */
	uint8_t			slot;
};

struct malo_cmd_set_rate {
	struct malo_cmd_header	cmdhdr;
	uint8_t			dataratetype;
	uint8_t			rateindex;
	uint8_t			aprates[14];
} __packed;

struct malo_cmd_wepkey {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;
	uint8_t			len;
	uint8_t			flags;
	uint16_t		index;
	uint8_t			value[IEEE80211_KEYBUF_SIZE];
	uint8_t			txmickey[IEEE80211_WEP_MICLEN];
	uint8_t			rxmickey[IEEE80211_WEP_MICLEN];
	uint64_t		rxseqctr;
	uint64_t		txseqctr;
} __packed;

struct malo_cmd_mcast {
	struct malo_cmd_header	cmdhdr;
	uint16_t		action;
	uint16_t		numaddr;
#define	MALO_HAL_MCAST_MAX	32
	uint8_t			maclist[6*32];
} __packed;

/*
 * DMA state for tx/rx descriptors.
 */

/*
 * Common "base class" for tx/rx descriptor resources
 * allocated using the bus dma api.
 */
struct malo_descdma {
	const char*		dd_name;
	void			*dd_desc;	/* descriptors */
	bus_addr_t		dd_desc_paddr;	/* physical addr of dd_desc */
	bus_size_t		dd_desc_len;	/* size of dd_desc */
	bus_dma_segment_t	dd_dseg;
	int			dd_dnseg;	/* number of segments */
	bus_dma_tag_t		dd_dmat;	/* bus DMA tag */
	bus_dmamap_t		dd_dmamap;	/* DMA map for descriptors */
	void			*dd_bufptr;	/* associated buffers */
};

/*
 * Hardware tx/rx descriptors.
 *
 * NB: tx descriptor size must match f/w expected size
 * because f/w prefetch's the next descriptor linearly
 * and doesn't chase the next pointer.
 */
struct malo_txdesc {
	uint32_t		status;
#define	MALO_TXD_STATUS_IDLE			0x00000000
#define	MALO_TXD_STATUS_USED			0x00000001 
#define	MALO_TXD_STATUS_OK			0x00000001
#define	MALO_TXD_STATUS_OK_RETRY		0x00000002
#define	MALO_TXD_STATUS_OK_MORE_RETRY		0x00000004
#define	MALO_TXD_STATUS_MULTICAST_TX		0x00000008
#define	MALO_TXD_STATUS_BROADCAST_TX		0x00000010
#define	MALO_TXD_STATUS_FAILED_LINK_ERROR	0x00000020
#define	MALO_TXD_STATUS_FAILED_EXCEED_LIMIT	0x00000040
#define	MALO_TXD_STATUS_FAILED_XRETRY	MALO_TXD_STATUS_FAILED_EXCEED_LIMIT
#define	MALO_TXD_STATUS_FAILED_AGING		0x00000080
#define	MALO_TXD_STATUS_FW_OWNED		0x80000000
	uint8_t			datarate;
	uint8_t			txpriority;
	uint16_t		qosctrl;
	uint32_t		pktptr;
	uint16_t		pktlen;
	uint8_t			destaddr[6];
	uint32_t		physnext;
	uint32_t		sap_pktinfo;
	uint16_t		format;
#define	MALO_TXD_FORMAT		0x0001	/* frame format/rate */
#define	MALO_TXD_FORMAT_LEGACY	0x0000	/* legacy rate frame */
#define	MALO_TXD_RATE		0x01f8	/* tx rate (legacy)/ MCS */
#define	MALO_TXD_RATE_S		3
/* NB: 3 is reserved */
#define	MALO_TXD_ANTENNA	0x1800	/* antenna select */
#define	MALO_TXD_ANTENNA_S	11
	uint16_t		pad;	/* align to 4-byte boundary */
} __packed;

#define	MALO_TXDESC_SYNC(txq, ds, how) do {				\
	bus_dmamap_sync((txq)->dma.dd_dmat, (txq)->dma.dd_dmamap, how);	\
} while(0)

struct malo_rxdesc {
	uint8_t		rxcontrol;	/* control element */
#define	MALO_RXD_CTRL_DRIVER_OWN		0x00
#define	MALO_RXD_CTRL_OS_OWN			0x04
#define	MALO_RXD_CTRL_DMA_OWN			0x80
	uint8_t		snr;		/* signal to noise ratio */
	uint8_t		status;		/* status field w/ USED bit */
#define	MALO_RXD_STATUS_IDLE			0x00
#define	MALO_RXD_STATUS_OK			0x01
#define	MALO_RXD_STATUS_MULTICAST_RX		0x02
#define	MALO_RXD_STATUS_BROADCAST_RX		0x04
#define	MALO_RXD_STATUS_FRAGMENT_RX		0x08
#define	MALO_RXD_STATUS_GENERAL_DECRYPT_ERR	0xff
#define	MALO_RXD_STATUS_DECRYPT_ERR_MASK	0x80
#define	MALO_RXD_STATUS_TKIP_MIC_DECRYPT_ERR	0x02
#define	MALO_RXD_STATUS_WEP_ICV_DECRYPT_ERR	0x04
#define	MALO_RXD_STATUS_TKIP_ICV_DECRYPT_ERR	0x08
	uint8_t		channel;	/* channel # pkt received on */
	uint16_t	pktlen;		/* total length of received data */
	uint8_t		nf;		/* noise floor */
	uint8_t		rate;		/* received data rate */
	uint32_t	physbuffdata;	/* physical address of payload data */
	uint32_t	physnext;	/* physical address of next RX desc */ 
	uint16_t	qosctrl;	/* received QosCtrl field variable */
	uint16_t	htsig2;		/* like name states */
} __packed;

#define	MALO_RXDESC_SYNC(sc, ds, how) do {				\
	bus_dmamap_sync((sc)->malo_rxdma.dd_dmat,			\
	    (sc)->malo_rxdma.dd_dmamap, how);				\
} while (0)

struct malo_rxbuf {
	STAILQ_ENTRY(malo_rxbuf) bf_list;
	void			*bf_desc;	/* h/w descriptor */
	bus_addr_t		bf_daddr;	/* physical addr of desc */
	bus_dmamap_t		bf_dmamap;
	bus_addr_t		bf_data;	/* physical addr of rx data */
	struct mbuf		*bf_m;		/* jumbo mbuf */
};
typedef STAILQ_HEAD(, malo_rxbuf) malo_rxbufhead;

/*
 * Software backed version of tx/rx descriptors.  We keep
 * the software state out of the h/w descriptor structure
 * so that may be allocated in uncached memory w/o paying
 * performance hit.
 */
struct malo_txbuf {
	STAILQ_ENTRY(malo_txbuf) bf_list;
	void			*bf_desc;	/* h/w descriptor */
	bus_addr_t		bf_daddr;	/* physical addr of desc */
	bus_dmamap_t		bf_dmamap;	/* DMA map for descriptors */
	int			bf_nseg;
	bus_dma_segment_t	bf_segs[MALO_TXDESC];
	struct mbuf		*bf_m;
	struct ieee80211_node	*bf_node;
	struct malo_txq		*bf_txq;	/* backpointer to tx q/ring */
};
typedef STAILQ_HEAD(, malo_txbuf) malo_txbufhead;

/*
 * TX/RX ring definitions.  There are 4 tx rings, one
 * per AC, and 1 rx ring.  Note carefully that transmit
 * descriptors are treated as a contiguous chunk and the
 * firmware pre-fetches descriptors.  This means that we
 * must preserve order when moving descriptors between
 * the active+free lists; otherwise we may stall transmit.
 */
struct malo_txq {
	struct malo_descdma	dma;		/* bus dma resources */
	struct mtx		lock;		/* tx q lock */
	char			name[12];	/* e.g. "malo0_txq4" */
	int			qnum;		/* f/w q number */
	int			txpri;		/* f/w tx priority */
	int			nfree;		/* # buffers on free list */
	malo_txbufhead		free;		/* queue of free buffers */
	malo_txbufhead		active;		/* queue of active buffers */
};

#define	MALO_TXQ_LOCK_INIT(_sc, _tq) do { \
	snprintf((_tq)->name, sizeof((_tq)->name), "%s_txq%u", \
		device_get_nameunit((_sc)->malo_dev), (_tq)->qnum); \
	mtx_init(&(_tq)->lock, (_tq)->name, NULL, MTX_DEF); \
} while (0)
#define	MALO_TXQ_LOCK_DESTROY(_tq)	mtx_destroy(&(_tq)->lock)
#define	MALO_TXQ_LOCK(_tq)		mtx_lock(&(_tq)->lock)
#define	MALO_TXQ_UNLOCK(_tq)		mtx_unlock(&(_tq)->lock)
#define	MALO_TXQ_LOCK_ASSERT(_tq)	mtx_assert(&(_tq)->lock, MA_OWNED)

/*
 * Each packet has fixed front matter: a 2-byte length
 * of the payload, followed by a 4-address 802.11 header
 * (regardless of the actual header and always w/o any
 * QoS header).  The payload then follows.
 */
struct malo_txrec {
	uint16_t fwlen;
	struct ieee80211_frame_addr4 wh;
} __packed;

struct malo_vap {
	struct ieee80211vap malo_vap;
	int			(*malo_newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
};
#define	MALO_VAP(vap)	((struct malo_vap *)(vap))

struct malo_softc {
	struct ieee80211com	malo_ic;
	struct mbufq		malo_snd;
	device_t		malo_dev;
	struct mtx		malo_mtx;	/* master lock (recursive) */
	struct taskqueue	*malo_tq;	/* private task queue */

	bus_dma_tag_t		malo_dmat;	/* bus DMA tag */
	bus_space_handle_t	malo_io0h;	/* BAR 0 */
	bus_space_tag_t		malo_io0t;
	bus_space_handle_t	malo_io1h;	/* BAR 1 */
	bus_space_tag_t		malo_io1t;

	unsigned int		malo_invalid: 1,/* disable hardware accesses */
				malo_recvsetup: 1,	/* recv setup */
				malo_fixedrate: 1,	/* use fixed tx rate */
				malo_running: 1;

	struct malo_hal		*malo_mh;	/* h/w access layer */
	struct malo_hal_hwspec	malo_hwspecs;	/* h/w capabilities */
	struct malo_hal_txrxdma	malo_hwdma;	/* h/w dma setup */
	uint32_t		malo_imask;	/* interrupt mask copy */
	struct malo_hal_channel	malo_curchan;
	u_int16_t		malo_rxantenna;	/* rx antenna */
	u_int16_t		malo_txantenna;	/* tx antenna */

	struct malo_descdma	malo_rxdma;	/* rx bus dma resources */
	malo_rxbufhead		malo_rxbuf;	/* rx buffers */
	struct malo_rxbuf	*malo_rxnext;	/* next rx buffer to process */
	struct task		malo_rxtask;	/* rx int processing */

	struct malo_txq		malo_txq[MALO_NUM_TX_QUEUES];
	struct task		malo_txtask;	/* tx int processing */
	struct callout	malo_watchdog_timer;
	int			malo_timer;

	struct malo_tx_radiotap_header malo_tx_th;
	struct malo_rx_radiotap_header malo_rx_th;

	struct malo_stats	malo_stats;	/* interface statistics */
	int			malo_debug;
};

#define	MALO_LOCK_INIT(_sc) \
	mtx_init(&(_sc)->malo_mtx, device_get_nameunit((_sc)->malo_dev), \
		 NULL, MTX_DEF | MTX_RECURSE)
#define	MALO_LOCK_DESTROY(_sc)		mtx_destroy(&(_sc)->malo_mtx)
#define	MALO_LOCK(_sc)			mtx_lock(&(_sc)->malo_mtx)
#define	MALO_UNLOCK(_sc)		mtx_unlock(&(_sc)->malo_mtx)
#define	MALO_LOCK_ASSERT(_sc)		mtx_assert(&(_sc)->malo_mtx, MA_OWNED)

#define	MALO_RXFREE_INIT(_sc)						\
	mtx_init(&(_sc)->malo_rxlock, device_get_nameunit((_sc)->malo_dev), \
		 NULL, MTX_DEF)
#define	MALO_RXFREE_DESTROY(_sc)	mtx_destroy(&(_sc)->malo_rxlock)
#define	MALO_RXFREE_LOCK(_sc)		mtx_lock(&(_sc)->malo_rxlock)
#define	MALO_RXFREE_UNLOCK(_sc)		mtx_unlock(&(_sc)->malo_rxlock)
#define	MALO_RXFREE_ASSERT(_sc)		mtx_assert(&(_sc)->malo_rxlock, \
	MA_OWNED)

int	malo_attach(uint16_t, struct malo_softc *);
int	malo_intr(void *);
int	malo_detach(struct malo_softc *);
void	malo_shutdown(struct malo_softc *);
void	malo_suspend(struct malo_softc *);
void	malo_resume(struct malo_softc *);

#endif
