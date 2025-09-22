/*	$OpenBSD: fxpvar.h,v 1.38 2022/01/09 05:42:38 jsg Exp $	*/
/*	$NetBSD: if_fxpvar.h,v 1.1 1997/06/05 02:01:58 thorpej Exp $	*/

/*                  
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *              
 * Modifications to support NetBSD:
 * Copyright (c) 1997 Jason R. Thorpe.  All rights reserved.
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
 *      Id: if_fxpvar.h,v 1.6 1998/08/02 00:29:15 dg Exp
 */

/*
 * Misc. definitions for the Intel EtherExpress Pro/100B PCI Fast
 * Ethernet driver
 */

/*
 * Number of transmit control blocks. This determines the number
 * of transmit buffers that can be chained in the CB list.
 * This must be a power of two.
 */
#define FXP_NTXCB	128

/*
 * Minimum and maximum number of receive frame area buffers. 
 */
#define FXP_NRFABUFS_MIN	4
#define FXP_NRFABUFS_MAX	64	/* These are large so choose wisely. */

/*
 * Default maximum time, in microseconds, that an interrupt may be delayed
 * in an attempt to coalesce interrupts.  This is only effective if the Intel
 * microcode is loaded.
 */
#ifndef FXP_INT_DELAY
#define FXP_INT_DELAY 128 
#endif

/*
 * Default number of packets that will be bundled, before an interrupt is
 * generated.  This is only effective if the Intel microcode is loaded.
 * This is not present in all microcode revisions.
 */
#ifndef FXP_BUNDLE_MAX
#define FXP_BUNDLE_MAX 16
#endif

/* 
 * Bit-mask describing minimum size frame that will be bundled.
 * This is only effective if the Intel microcode is loaded.
 * This is not present in all microcode revisions. Disabled by default,
 * to reduce receiving immediately interrupts from all frames with size less
 * than 128 bytes.
 */
#ifndef FXP_MIN_SIZE_MASK
#define FXP_MIN_SIZE_MASK 0xFFFF
#endif

/*
 * NOTE: Elements are ordered for optimal cacheline behavior, and NOT
 *	 for functional grouping.
 */

struct fxp_txsw {
	struct fxp_txsw *tx_next;
	struct mbuf *tx_mbuf;
	bus_dmamap_t tx_map;
	bus_addr_t tx_off;
	struct fxp_cb_tx *tx_cb;
};

struct fxp_ctrl {
	struct fxp_cb_tx tx_cb[FXP_NTXCB];
	struct fxp_stats stats;
	union {
		struct fxp_cb_mcs mcs;
		struct fxp_cb_ias ias;
		struct fxp_cb_config cfg;
		struct fxp_cb_ucode code;
	} u;
};

struct fxp_softc {
	struct device sc_dev;		/* generic device structures */
	void *sc_ih;			/* interrupt handler cookie */
	bus_space_tag_t sc_st;		/* bus space tag */
	bus_space_handle_t sc_sh;	/* bus space handle */
	bus_dma_tag_t sc_dmat;		/* bus dma tag */
	struct arpcom sc_arpcom;	/* per-interface network data */
	struct mii_data sc_mii;		/* MII media information */
	struct mbuf *rfa_headm;		/* first mbuf in receive frame area */
	struct mbuf *rfa_tailm;		/* last mbuf in receive frame area */
	int sc_flags;			/* misc. flags */
#define	FXPF_MWI_ENABLE		0x10	/* enable use of PCI MWI command */
#define	FXPF_DISABLE_STANDBY	0x20	/* currently need to work-around */
#define	FXPF_UCODELOADED	0x40    /* ucode load already attempted */
#define	FXPF_NOUCODE		0x80	/* no ucode for this chip */
#define	FXPF_RECV_WORKAROUND	0x100	/* receiver lock-up workaround */
	struct timeout stats_update_to; /* Pointer to timeout structure */
	int rx_idle_secs;		/* # of seconds RX has been idle */
	struct fxp_cb_tx *cbl_base;	/* base of TxCB list */
	int phy_primary_addr;		/* address of primary PHY */
	int phy_primary_device;		/* device type of primary PHY */
	int phy_10Mbps_only;		/* PHY is 10Mbps-only device */
	int eeprom_size;		/* size of serial EEPROM */
	int rx_bufs;			/* how many rx buffers allocated? */
	struct fxp_txsw txs[FXP_NTXCB];
	struct fxp_txsw *sc_cbt_cons, *sc_cbt_prod, *sc_cbt_prev;
	int sc_cbt_cnt;
	bus_dmamap_t tx_cb_map;
	bus_dma_segment_t sc_cb_seg;
	int sc_cb_nseg;
	struct fxp_ctrl *sc_ctrl;
	bus_dmamap_t sc_rxmaps[FXP_NRFABUFS_MAX];
	int sc_rxfree;
	u_int32_t sc_revision;		/* chip revision */ 
	u_int16_t sc_int_delay;		/* interrupt delay value for ucode */
	u_int16_t sc_bundle_max;	/* max # frames per interrupt (ucode) */
	u_int16_t sc_min_size_mask;	/* bit-mask describing the minimum
					 * size of frame that will be bundled */

	u_int32_t		*sc_ucodebuf;
	size_t			sc_ucodelen;
};

/* Macros to ease CSR access. */
#define	CSR_READ_2(sc, reg)						\
	bus_space_read_2((sc)->sc_st, (sc)->sc_sh, (reg))
#define	CSR_READ_4(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))
#define	CSR_WRITE_2(sc, reg, val)					\
	bus_space_write_2((sc)->sc_st, (sc)->sc_sh, (reg), (val))
#define	CSR_WRITE_4(sc, reg, val)					\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

extern int fxp_intr(void *);
extern int fxp_attach(struct fxp_softc *, const char *);
void fxp_detach(struct fxp_softc *);
void fxp_init(void *);
void fxp_stop(struct fxp_softc *, int, int);
int fxp_activate(struct device *, int);
void fxp_wakeup(struct fxp_softc *);

#define	FXP_RXMAP_GET(sc)	((sc)->sc_rxmaps[(sc)->sc_rxfree++])
#define	FXP_RXMAP_PUT(sc,map)	((sc)->sc_rxmaps[--(sc)->sc_rxfree] = (map))

#define	FXP_TXCB_SYNC(sc, txs, p)					\
    bus_dmamap_sync((sc)->sc_dmat, (sc)->tx_cb_map, (txs)->tx_off,	\
	sizeof(struct fxp_cb_tx), (p))

#define	FXP_MCS_SYNC(sc, p)						\
    bus_dmamap_sync((sc)->sc_dmat, (sc)->tx_cb_map,			\
	offsetof(struct fxp_ctrl, u.mcs), sizeof(struct fxp_cb_mcs), (p))

#define	FXP_IAS_SYNC(sc, p)						\
    bus_dmamap_sync((sc)->sc_dmat, (sc)->tx_cb_map,			\
	offsetof(struct fxp_ctrl, u.ias), sizeof(struct fxp_cb_ias), (p))

#define	FXP_CFG_SYNC(sc, p)						\
    bus_dmamap_sync((sc)->sc_dmat, (sc)->tx_cb_map,			\
	offsetof(struct fxp_ctrl, u.cfg), sizeof(struct fxp_cb_config), (p))

#define FXP_UCODE_SYNC(sc, p)						\
    bus_dmamap_sync((sc)->sc_dmat, (sc)->tx_cb_map,			\
	offsetof(struct fxp_ctrl, u.code), sizeof(struct fxp_cb_ucode), (p))

#define	FXP_STATS_SYNC(sc, p)						\
    bus_dmamap_sync((sc)->sc_dmat, (sc)->tx_cb_map,			\
	offsetof(struct fxp_ctrl, stats), sizeof(struct fxp_stats), (p)) 

#define	FXP_MBUF_SYNC(sc, m, p)						\
    bus_dmamap_sync((sc)->sc_dmat, (m), 0, (m)->dm_mapsize, (p))
