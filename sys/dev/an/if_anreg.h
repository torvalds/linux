/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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

#define AN_TIMEOUT	65536

/* Default network name: <empty string> */
#define AN_DEFAULT_NETNAME	""

/* The nodename must be less than 16 bytes */
#define AN_DEFAULT_NODENAME	"FreeBSD"

#define AN_DEFAULT_IBSS		"FreeBSD IBSS"

/*
 * register space access macros
 */
#define CSR_WRITE_2(sc, reg, val)	bus_write_2(sc->port_res, reg, val)

#define CSR_READ_2(sc, reg)		bus_read_2(sc->port_res, reg)

#define CSR_WRITE_1(sc, reg, val)	bus_write_1(sc->port_res, reg, val)

#define CSR_READ_1(sc, reg)		bus_read_1(sc->port_res, reg)

/*
 * memory space access macros
 */
#define CSR_MEM_WRITE_2(sc, reg, val)	bus_write_2(sc->mem_res, reg, val)

#define CSR_MEM_READ_2(sc, reg)		bus_read_2(sc->mem_res, reg)

#define CSR_MEM_WRITE_1(sc, reg, val)	bus_write_1(sc->mem_res, reg, val)

#define CSR_MEM_READ_1(sc, reg)		bus_read_1(sc->mem_res, reg)

/*
 * aux. memory space access macros
 */
#define CSR_MEM_AUX_WRITE_4(sc, reg, val)	\
	bus_write_4(sc->mem_aux_res, reg, val)

#define CSR_MEM_AUX_READ_4(sc, reg)		\
	bus_read_4(sc->mem_aux_res, reg)

#define CSR_MEM_AUX_WRITE_1(sc, reg, val)	\
	bus_write_1(sc->mem_aux_res, reg, val)

#define CSR_MEM_AUX_READ_1(sc, reg)		\
	bus_read_1(sc->mem_aux_res, reg)

/*
 * Size of Aironet I/O space.
 */
#define AN_IOSIZ		0x40

/*
 * Size of aux. memory space ... probably not needed DJA 
 */
#define AN_AUX_MEM_SIZE		(256 * 1024)

/*
 * Hermes register definitions and what little I know about them.
 */

/* Hermes command/status registers. */
#define AN_COMMAND(x)		(x ? 0x00 : 0x00)
#define AN_PARAM0(x)		(x ? 0x04 : 0x02)
#define AN_PARAM1(x)		(x ? 0x08 : 0x04)
#define AN_PARAM2(x)		(x ? 0x0c : 0x06)
#define AN_STATUS(x)		(x ? 0x10 : 0x08)
#define AN_RESP0(x)		(x ? 0x14 : 0x0A)
#define AN_RESP1(x)		(x ? 0x18 : 0x0C)
#define AN_RESP2(x)		(x ? 0x1c : 0x0E)
#define AN_LINKSTAT(x)		(x ? 0x20 : 0x10)

/* Command register */
#define AN_CMD_BUSY		0x8000 /* busy bit */
#define AN_CMD_NO_ACK		0x0080 /* don't acknowledge command */
#define AN_CMD_CODE_MASK	0x003F
#define AN_CMD_QUAL_MASK	0x7F00

/* Command codes */
#define AN_CMD_NOOP		0x0000 /* no-op */
#define AN_CMD_ENABLE		0x0001 /* enable */
#define AN_CMD_DISABLE		0x0002 /* disable */
#define AN_CMD_FORCE_SYNCLOSS	0x0003 /* force loss of sync */
#define AN_CMD_FW_RESTART	0x0004 /* firmware restart */
#define AN_CMD_HOST_SLEEP	0x0005
#define AN_CMD_MAGIC_PKT	0x0006
#define AN_CMD_READCFG		0x0008
#define AN_CMD_SET_MODE		0x0009
#define AN_CMD_ALLOC_MEM	0x000A /* allocate NIC memory */
#define AN_CMD_TX		0x000B /* transmit */
#define AN_CMD_DEALLOC_MEM	0x000C
#define AN_CMD_NOOP2		0x0010
#define AN_CMD_ALLOC_DESC	0x0020
#define AN_CMD_ACCESS		0x0021
#define AN_CMD_ALLOC_BUF	0x0028
#define AN_CMD_PSP_NODES	0x0030
#define AN_CMD_SET_PHYREG	0x003E
#define AN_CMD_TX_TEST		0x003F
#define AN_CMD_SLEEP		0x0085
#define AN_CMD_SAVECFG		0x0108

/*
 * MPI 350 DMA descriptor information
 */
#define AN_DESCRIPTOR_TX	0x01
#define AN_DESCRIPTOR_RX	0x02
#define AN_DESCRIPTOR_TXCMP	0x04
#define AN_DESCRIPTOR_HOSTWRITE	0x08
#define AN_DESCRIPTOR_HOSTREAD	0x10
#define AN_DESCRIPTOR_HOSTRW	0x20

#define AN_MAX_RX_DESC 1
#define AN_MAX_TX_DESC 1
#define AN_HOSTBUFSIZ 1840

struct an_card_rid_desc
{
	unsigned	an_rid:16;
	unsigned	an_len:15;
	unsigned	an_valid:1;
	u_int64_t	an_phys;
};

struct an_card_rx_desc
{
	unsigned	an_ctrl:15;
	unsigned	an_done:1;
	unsigned	an_len:15;
	unsigned	an_valid:1;
	u_int64_t	an_phys;
};

struct an_card_tx_desc
{
	unsigned	an_offset:15;
	unsigned	an_eoc:1;
	unsigned	an_len:15;
	unsigned	an_valid:1;
	u_int64_t	an_phys;
};

#define AN_RID_BUFFER_SIZE	AN_MAX_DATALEN
#define AN_RX_BUFFER_SIZE	AN_HOSTBUFSIZ
#define AN_TX_BUFFER_SIZE	AN_HOSTBUFSIZ
/*#define AN_HOST_DESC_OFFSET	0xC sort of works */
#define AN_HOST_DESC_OFFSET	0x800
#define AN_RX_DESC_OFFSET  (AN_HOST_DESC_OFFSET + \
    sizeof(struct an_card_rid_desc))
#define AN_TX_DESC_OFFSET (AN_RX_DESC_OFFSET + \
    (AN_MAX_RX_DESC * sizeof(struct an_card_rx_desc)))

struct an_command {
	u_int16_t	an_cmd;
	u_int16_t	an_parm0;
	u_int16_t	an_parm1;
	u_int16_t	an_parm2;
};

struct an_reply {
	u_int16_t	an_status;
	u_int16_t	an_resp0;
	u_int16_t	an_resp1;
	u_int16_t	an_resp2;
};

/*
 * Reclaim qualifier bit, applicable to the
 * TX command.
 */
#define AN_RECLAIM		0x0100 /* reclaim NIC memory */

/*
 * ACCESS command qualifier bits.
 */
#define AN_ACCESS_READ		0x0000
#define AN_ACCESS_WRITE		0x0100

/*
 * PROGRAM command qualifier bits.
 */
#define AN_PROGRAM_DISABLE	0x0000
#define AN_PROGRAM_ENABLE_RAM	0x0100
#define AN_PROGRAM_ENABLE_NVRAM	0x0200
#define AN_PROGRAM_NVRAM	0x0300

/* Status register values */
#define AN_STAT_CMD_CODE	0x003F
#define AN_STAT_CMD_RESULT	0x7F00

/* Linkstat register */
#define AN_LINKSTAT_ASSOCIATED		0x0400
#define AN_LINKSTAT_AUTHFAIL		0x0300
#define AN_LINKSTAT_ASSOC_FAIL		0x8400
#define AN_LINKSTAT_DISASSOC		0x8200
#define AN_LINKSTAT_DEAUTH		0x8100
#define AN_LINKSTAT_SYNCLOST_TSF	0x8004
#define AN_LINKSTAT_SYNCLOST_HOSTREQ	0x8003
#define AN_LINKSTAT_SYNCLOST_AVGRETRY	0x8002
#define AN_LINKSTAT_SYNCLOST_MAXRETRY	0x8001
#define AN_LINKSTAT_SYNCLOST_MISSBEACON	0x8000

/* memory handle management registers */
#define AN_RX_FID		0x20
#define AN_ALLOC_FID		0x22
#define AN_TX_CMP_FID(x)	(x ? 0x1a : 0x24)

/*
 * Buffer Access Path (BAP) registers.
 * These are I/O channels. I believe you can use each one for
 * any desired purpose independently of the other. In general
 * though, we use BAP1 for reading and writing LTV records and
 * reading received data frames, and BAP0 for writing transmit
 * frames. This is a convention though, not a rule.
 */
#define AN_SEL0			0x18
#define AN_SEL1			0x1A
#define AN_OFF0			0x1C
#define AN_OFF1			0x1E
#define AN_DATA0		0x36
#define AN_DATA1		0x38
#define AN_BAP0			AN_DATA0
#define AN_BAP1			AN_DATA1

#define AN_OFF_BUSY		0x8000
#define AN_OFF_ERR		0x4000
#define AN_OFF_DONE		0x2000
#define AN_OFF_DATAOFF		0x0FFF

/* Event registers */
#define AN_EVENT_STAT(x)	(x ? 0x60 : 0x30)	/* Event status */
#define AN_INT_EN(x)		(x ? 0x64 : 0x32)	/* Interrupt enable/
							   disable */
#define AN_EVENT_ACK(x)		(x ? 0x68 : 0x34)	/* Ack event */

/* Events */
#define AN_EV_CLR_STUCK_BUSY	0x4000	/* clear stuck busy bit */
#define AN_EV_WAKEREQUEST	0x2000	/* awaken from PSP mode */
#define AN_EV_MIC		0x1000	/* Message Integrity Check*/
#define AN_EV_AWAKE		0x0100	/* station woke up from PSP mode*/
#define AN_EV_LINKSTAT		0x0080	/* link status available */
#define AN_EV_CMD		0x0010	/* command completed */
#define AN_EV_ALLOC		0x0008	/* async alloc/reclaim completed */
#define AN_EV_TX_CPY		0x0400
#define AN_EV_TX_EXC		0x0004	/* async xmit completed with failure */
#define	AN_EV_TX		0x0002	/* async xmit completed successfully */
#define AN_EV_RX		0x0001	/* async rx completed */

#define AN_INTRS(x)	\
	( x ? (AN_EV_RX|AN_EV_TX|AN_EV_TX_EXC|AN_EV_TX_CPY|AN_EV_ALLOC \
	       |AN_EV_LINKSTAT|AN_EV_MIC) \
	  : \
	      (AN_EV_RX|AN_EV_TX|AN_EV_TX_EXC|AN_EV_ALLOC \
	       |AN_EV_LINKSTAT|AN_EV_MIC) \
	      )

/* Host software registers */
#define AN_SW0(x)		(x ? 0x50 : 0x28)
#define AN_SW1(x)		(x ? 0x54 : 0x2A)
#define AN_SW2(x)		(x ? 0x58 : 0x2C)
#define AN_SW3(x)		(x ? 0x5c : 0x2E)

#define AN_CNTL			0x14

#define AN_CNTL_AUX_ENA		0xC000
#define AN_CNTL_AUX_ENA_STAT	0xC000
#define AN_CNTL_AUX_DIS_STAT	0x0000
#define AN_CNTL_AUX_ENA_CNTL	0x8000
#define AN_CNTL_AUX_DIS_CNTL	0x4000

#define AN_AUX_PAGE		0x3A
#define AN_AUX_OFFSET		0x3C
#define AN_AUX_DATA		0x3E

/*
 * Length, Type, Value (LTV) record definitions and RID values.
 */
struct an_ltv_gen {
	u_int16_t		an_len;
	u_int16_t		an_type;
	u_int16_t		an_val;
};

#define AN_DEF_SSID_LEN		7
#define AN_DEF_SSID		"tsunami"

#define AN_RXGAP_MAX	8

/*
 * Transmit frame structure.
 */
struct an_txframe {
	u_int32_t		an_tx_sw;		/* 0x00 */
	u_int16_t		an_tx_status;		/* 0x04 */
	u_int16_t		an_tx_payload_len;	/* 0x06 */
	u_int16_t		an_tx_ctl;		/* 0x08 */
	u_int16_t		an_tx_assoc_id;		/* 0x0A */
	u_int16_t		an_tx_retry;		/* 0x0C */
	u_int8_t		an_tx_assoc_cnt;	/* 0x0E */
	u_int8_t		an_tx_rate;		/* 0x0F */
	u_int8_t		an_tx_max_long_retries;	/* 0x10 */
	u_int8_t		an_tx_max_short_retries; /*0x11 */
	u_int8_t		an_rsvd0[2];		/* 0x12 */
	u_int16_t		an_frame_ctl;		/* 0x14 */
	u_int16_t		an_duration;		/* 0x16 */
	u_int8_t		an_addr1[6];		/* 0x18 */
	u_int8_t		an_addr2[6];		/* 0x1E */
	u_int8_t		an_addr3[6];		/* 0x24 */
	u_int16_t		an_seq_ctl;		/* 0x2A */
	u_int8_t		an_addr4[6];		/* 0x2C */
	u_int8_t		an_gaplen;		/* 0x32 */
} __packed;

struct an_rxframe_802_3 {
        u_int16_t		an_rx_802_3_status;     /* 0x34 */
	u_int16_t		an_rx_802_3_payload_len;/* 0x36 */
	u_int8_t		an_rx_dst_addr[6];      /* 0x38 */
	u_int8_t		an_rx_src_addr[6];      /* 0x3E */
};
#define AN_RXGAP_MAX	8


struct an_txframe_802_3 {
/*
 * Transmit 802.3 header structure.
 */
        u_int16_t		an_tx_802_3_status;     /* 0x34 */
	u_int16_t		an_tx_802_3_payload_len;/* 0x36 */
	u_int8_t		an_tx_dst_addr[6];      /* 0x38 */
	u_int8_t		an_tx_src_addr[6];      /* 0x3E */
};

#define AN_TXSTAT_EXCESS_RETRY	0x0002
#define AN_TXSTAT_LIFE_EXCEEDED	0x0004
#define AN_TXSTAT_AID_FAIL	0x0008
#define AN_TXSTAT_MAC_DISABLED	0x0010
#define AN_TXSTAT_ASSOC_LOST	0x0020

#define AN_TXCTL_RSVD		0x0001
#define AN_TXCTL_TXOK_INTR	0x0002
#define AN_TXCTL_TXERR_INTR	0x0004
#define AN_TXCTL_HEADER_TYPE	0x0008
#define AN_TXCTL_PAYLOAD_TYPE	0x0010
#define AN_TXCTL_NORELEASE	0x0020
#define AN_TXCTL_NORETRIES	0x0040
#define AN_TXCTL_CLEAR_AID	0x0080
#define AN_TXCTL_STRICT_ORDER	0x0100
#define AN_TXCTL_USE_RTS	0x0200

#define AN_HEADERTYPE_8023	0x0000
#define AN_HEADERTYPE_80211	0x0008

#define AN_PAYLOADTYPE_ETHER	0x0000
#define AN_PAYLOADTYPE_LLC	0x0010

#define AN_TXCTL_80211		(AN_HEADERTYPE_80211|AN_PAYLOADTYPE_LLC)

#define AN_TXCTL_8023		(AN_HEADERTYPE_8023|AN_PAYLOADTYPE_ETHER)

/*
 * Additions to transmit control bits for MPI350
 */
#define	AN_TXCTL_HW(x)	\
	( x ? (AN_TXCTL_NORELEASE) \
	  : \
	      (AN_TXCTL_TXOK_INTR|AN_TXCTL_TXERR_INTR|AN_TXCTL_NORELEASE) \
	      )

#define AN_TXGAP_80211		0
#define AN_TXGAP_8023		0

struct an_802_3_hdr {
	u_int16_t		an_8023_status;
	u_int16_t		an_8023_payload_len;
	u_int8_t		an_8023_dst_addr[6];
	u_int8_t		an_8023_src_addr[6];
	u_int16_t		an_8023_dat[3];	/* SNAP header */
	u_int16_t		an_8023_type;
};

struct an_snap_hdr {
	u_int16_t		an_snap_dat[3];	/* SNAP header */
	u_int16_t		an_snap_type;
};

struct an_dma_alloc {
	u_int32_t		an_dma_paddr;
	caddr_t			an_dma_vaddr;
	bus_dmamap_t		an_dma_map;
	bus_dma_segment_t	an_dma_seg;
	bus_size_t		an_dma_size;
	int			an_dma_nseg;
};

#define AN_TX_RING_CNT		4
#define AN_INC(x, y)		(x) = (x + 1) % y

struct an_tx_ring_data {
	u_int16_t		an_tx_fids[AN_TX_RING_CNT];
	u_int16_t		an_tx_ring[AN_TX_RING_CNT];
	int			an_tx_prod;
	int			an_tx_cons;
	int			an_tx_empty;
};

struct an_softc	{
	struct ifnet		*an_ifp;

	int	port_rid;	/* resource id for port range */
	struct resource* port_res; /* resource for port range */
	int     mem_rid;	/* resource id for memory range */
        int     mem_used;	/* nonzero if memory used */
	struct resource* mem_res; /* resource for memory range */
	int     mem_aux_rid;	/* resource id for memory range */
        int     mem_aux_used;	/* nonzero if memory used */
	struct resource* mem_aux_res; /* resource for memory range */
	int	irq_rid;	/* resource id for irq */
	void*	irq_handle;	/* handle for irq handler */
	struct resource* irq_res; /* resource for irq */

	bus_space_handle_t	an_mem_aux_bhandle;
	bus_space_tag_t		an_mem_aux_btag;
	bus_dma_tag_t		an_dtag;
	struct an_ltv_genconfig	an_config;
	struct an_ltv_caps	an_caps;
	struct an_ltv_ssidlist_new	an_ssidlist;
	struct an_ltv_aplist	an_aplist;
        struct an_ltv_key	an_temp_keys[4];
	int			an_tx_rate;
	int			an_rxmode;
	int			an_gone;
	int			an_if_flags;
	u_int8_t		an_txbuf[1536];
	struct an_tx_ring_data	an_rdata;
	struct an_ltv_stats	an_stats;
	struct an_ltv_status	an_status;
	u_int8_t		an_associated;
#ifdef ANCACHE
	int			an_sigitems;
	struct an_sigcache	an_sigcache[MAXANCACHE];
	int			an_nextitem;
	int			an_have_rssimap;
	struct an_ltv_rssi_map	an_rssimap;
#endif
	struct callout		an_stat_ch;
	struct mtx		an_mtx;
	device_t		an_dev;
	struct ifmedia		an_ifmedia;
	int		        an_monitor;
	int		        an_was_monitor;
	int			an_timer;
	u_char			buf_802_11[MCLBYTES];
	struct an_req		areq;
	unsigned short*		an_flash_buffer;
	int			mpi350;
	struct an_dma_alloc	an_rid_buffer;
	struct an_dma_alloc	an_rx_buffer[AN_MAX_RX_DESC];
	struct an_dma_alloc	an_tx_buffer[AN_MAX_TX_DESC];
};

#define AN_LOCK(_sc)		mtx_lock(&(_sc)->an_mtx)
#define AN_UNLOCK(_sc)		mtx_unlock(&(_sc)->an_mtx)
#define AN_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->an_mtx, MA_OWNED)

void	an_release_resources	(device_t);
int	an_alloc_port		(device_t, int, int);
int	an_alloc_memory		(device_t, int, int);
int	an_alloc_aux_memory	(device_t, int, int);
int	an_alloc_irq		(device_t, int, int);
int	an_probe	(device_t);
int	an_shutdown	(device_t);
void	an_resume	(device_t);
int	an_attach		(struct an_softc *, int);
int	an_detach	(device_t);
void    an_stop		(struct an_softc *);

driver_intr_t	an_intr;

#define AN_802_3_OFFSET		0x2E
#define AN_802_11_OFFSET	0x44
#define AN_802_11_OFFSET_RAW	0x3C

#define AN_STAT_BADCRC		0x0001
#define AN_STAT_UNDECRYPTABLE	0x0002
#define AN_STAT_ERRSTAT		0x0003
#define AN_STAT_MAC_PORT	0x0700
#define AN_STAT_1042		0x2000	/* RFC1042 encoded */
#define AN_STAT_TUNNEL		0x4000	/* Bridge-tunnel encoded */
#define AN_STAT_WMP_MSG		0x6000	/* WaveLAN-II management protocol */
#define AN_RXSTAT_MSG_TYPE	0xE000

#define AN_ENC_TX_802_3		0x00
#define AN_ENC_TX_802_11	0x11
#define AN_ENC_TX_E_II		0x0E

#define AN_ENC_TX_1042		0x00
#define AN_ENC_TX_TUNNEL	0xF8

#define AN_TXCNTL_MACPORT	0x00FF
#define AN_TXCNTL_STRUCTTYPE	0xFF00

/*
 * SNAP (sub-network access protocol) constants for transmission
 * of IP datagrams over IEEE 802 networks, taken from RFC1042.
 * We need these for the LLC/SNAP header fields in the TX/RX frame
 * structure.
 */
#define AN_SNAP_K1		0xaa	/* assigned global SAP for SNAP */
#define AN_SNAP_K2		0x00
#define AN_SNAP_CONTROL		0x03	/* unnumbered information format */
#define AN_SNAP_WORD0		(AN_SNAP_K1 | (AN_SNAP_K1 << 8))
#define AN_SNAP_WORD1		(AN_SNAP_K2 | (AN_SNAP_CONTROL << 8))
#define AN_SNAPHDR_LEN		0x6
