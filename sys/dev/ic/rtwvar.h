/*	$OpenBSD: rtwvar.h,v 1.33 2024/05/13 01:15:50 jsg Exp $	*/
/*	$NetBSD: rtwvar.h,v 1.10 2004/12/26 22:37:57 mycroft Exp $	*/

/*-
 * Copyright (c) 2004, 2005 David Young.  All rights reserved.
 *
 * Driver for the Realtek RTL8180 802.11 MAC/BBP by David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_RTWVAR_H_
#define	_DEV_IC_RTWVAR_H_

#include <sys/device.h>
#include <sys/queue.h>
#include <sys/timeout.h>

#ifdef RTW_DEBUG
#define	RTW_DEBUG_TUNE		0x0000001
#define	RTW_DEBUG_PKTFILT	0x0000002
#define	RTW_DEBUG_XMIT		0x0000004
#define	RTW_DEBUG_XMIT_DESC	0x0000008
#define	RTW_DEBUG_NODE		0x0000010
#define	RTW_DEBUG_PWR		0x0000020
#define	RTW_DEBUG_ATTACH	0x0000040
#define	RTW_DEBUG_REGDUMP	0x0000080
#define	RTW_DEBUG_ACCESS	0x0000100
#define	RTW_DEBUG_RESET		0x0000200
#define	RTW_DEBUG_INIT		0x0000400
#define	RTW_DEBUG_IOSTATE	0x0000800
#define	RTW_DEBUG_RECV		0x0001000
#define	RTW_DEBUG_RECV_DESC	0x0002000
#define	RTW_DEBUG_IO_KICK	0x0004000
#define	RTW_DEBUG_INTR		0x0008000
#define	RTW_DEBUG_PHY		0x0010000
#define	RTW_DEBUG_PHYIO		0x0020000
#define	RTW_DEBUG_PHYBITIO	0x0040000
#define	RTW_DEBUG_TIMEOUT	0x0080000
#define	RTW_DEBUG_BUGS		0x0100000
#define	RTW_DEBUG_BEACON	0x0200000
#define	RTW_DEBUG_LED		0x0400000
#define	RTW_DEBUG_KEY		0x0800000
#define	RTW_DEBUG_XMIT_RSRC	0x1000000
#define	RTW_DEBUG_OACTIVE	0x2000000
#define	RTW_DEBUG_MAX		0x3ffffff

extern int rtw_debug;
#define RTW_DPRINTF(__flags, __x)	\
	if ((rtw_debug & (__flags)) != 0) printf __x
#define	DPRINTF(__sc, __flags, __x)				\
	if (((__sc)->sc_ic.ic_if.if_flags & IFF_DEBUG) != 0)	\
		RTW_DPRINTF(__flags, __x)
#else /* RTW_DEBUG */
#define RTW_DPRINTF(__flags, __x)
#define	DPRINTF(__sc, __flags, __x)
#endif /* RTW_DEBUG */

#define	KASSERT2(__cond, __msg)		\
	do {				\
		if (!(__cond))		\
			panic __msg ;	\
	} while (0)

enum rtw_locale {
	RTW_LOCALE_USA = 0,
	RTW_LOCALE_EUROPE,
	RTW_LOCALE_JAPAN,
	RTW_LOCALE_UNKNOWN
};

#define	RTW_RFCHIPID_RESERVED	0x00
#define	RTW_RFCHIPID_INTERSIL	0x01
#define	RTW_RFCHIPID_RFMD2948	0x02
#define	RTW_RFCHIPID_PHILIPS	0x03
#define	RTW_RFCHIPID_MAXIM2820	0x04
#define	RTW_RFCHIPID_GCT	0x05
#define	RTW_RFCHIPID_RFMD2958	0x06
#define	RTW_RFCHIPID_MAXIM2822	0x07
#define	RTW_RFCHIPID_MAXIM2825	0x08
#define	RTW_RFCHIPID_RTL8225	0x09
#define	RTW_RFCHIPID_RTL8255	0x0a

/* sc_flags */
#define RTW_F_ENABLED		0x00000001	/* chip is enabled */
#define RTW_F_DIGPHY		0x00000002	/* digital PHY */
#define RTW_F_DFLANTB		0x00000004	/* B antenna is default */
#define RTW_F_RTL8185		0x00000008	/* RTL8185 or newer */
#define RTW_F_ANTDIV		0x00000010	/* h/w antenna diversity */
#define RTW_F_9356SROM		0x00000020	/* 93c56 SROM */
#define RTW_F_SLEEP		0x00000040	/* chip is asleep */
#define RTW_F_INVALID		0x00000080	/* chip is absent */
	/* all PHY flags */
#define RTW_F_ALLPHY		(RTW_F_DIGPHY|RTW_F_DFLANTB|RTW_F_ANTDIV)

enum rtw_access {
	RTW_ACCESS_NONE = 0,
	RTW_ACCESS_CONFIG = 1,
	RTW_ACCESS_ANAPARM = 2
};

struct rtw_regs {
	bus_space_tag_t		r_bt;
	bus_space_handle_t	r_bh;
	enum rtw_access		r_access;
	void			*r_priv;

	/* bus independent I/O callbacks */
	u_int8_t	(*r_read8)(void *, u_int32_t);
	u_int16_t	(*r_read16)(void *, u_int32_t);
	u_int32_t	(*r_read32)(void *, u_int32_t);
	void		(*r_write8)(void *, u_int32_t, u_int8_t);
	void		(*r_write16)(void *, u_int32_t, u_int16_t);
	void		(*r_write32)(void *, u_int32_t, u_int32_t);
	void		(*r_barrier)(void *, u_int32_t, u_int32_t, int);
};

#define RTW_SR_GET(sr, ofs) \
    (((sr)->sr_content[(ofs)/2] >> (((ofs) % 2 == 0) ? 0 : 8)) & 0xff)

#define RTW_SR_GET16(sr, ofs) \
    (RTW_SR_GET((sr), (ofs)) | (RTW_SR_GET((sr), (ofs) + 1) << 8))

struct rtw_srom {
	u_int16_t		*sr_content;
	u_int16_t		sr_size;
};

struct rtw_rxsoft {
	struct mbuf			*rs_mbuf;
	bus_dmamap_t			rs_dmamap;
};

struct rtw_duration {
	uint16_t	d_rts_dur;
	uint16_t	d_data_dur;
	uint16_t	d_plcp_len;
	uint8_t		d_residue;	/* unused octets in time slot */
};

struct rtw_txsoft {
	SIMPLEQ_ENTRY(rtw_txsoft)	ts_q;
	struct mbuf			*ts_mbuf;
	bus_dmamap_t			ts_dmamap;
	struct ieee80211_node		*ts_ni;	/* destination node */
	u_int				ts_first;	/* 1st hw descriptor */
	u_int				ts_last;	/* last hw descriptor */
	struct rtw_duration		ts_d0;
	struct rtw_duration		ts_dn;
};

#define RTW_NTXPRI	4	/* number of Tx priorities */
#define RTW_TXPRILO	0
#define RTW_TXPRIMD	1
#define RTW_TXPRIHI	2
#define RTW_TXPRIBCN	3	/* beacon priority */

#define RTW_MAXPKTSEGS		64	/* max 64 segments per Tx packet */

/* Note well: the descriptor rings must begin on RTW_DESC_ALIGNMENT
 * boundaries.  I allocate them consecutively from one buffer, so
 * just round up.
 */
#define RTW_TXQLENLO	64	/* low-priority queue length */
#define RTW_TXQLENMD	64	/* medium-priority */
#define RTW_TXQLENHI	64	/* high-priority */
#define RTW_TXQLENBCN	8	/* beacon */

#define RTW_NTXDESCLO	RTW_TXQLENLO
#define RTW_NTXDESCMD	RTW_TXQLENMD
#define RTW_NTXDESCHI	RTW_TXQLENHI
#define RTW_NTXDESCBCN	RTW_TXQLENBCN

#define RTW_NTXDESCTOTAL	(RTW_NTXDESCLO + RTW_NTXDESCMD + \
				 RTW_NTXDESCHI + RTW_NTXDESCBCN)

#define RTW_RXQLEN	64

struct rtw_rxdesc_blk {
	struct rtw_rxdesc	*rdb_desc;
	u_int			rdb_next;
	u_int			rdb_ndesc;
	bus_dma_tag_t		rdb_dmat;
	bus_dmamap_t		rdb_dmamap;
};

struct rtw_txdesc_blk {
	u_int			tdb_ndesc;
	u_int			tdb_next;
	u_int			tdb_nfree;
	bus_dma_tag_t		tdb_dmat;
	bus_dmamap_t		tdb_dmamap;
	bus_addr_t		tdb_physbase;
	bus_addr_t		tdb_ofs;
	bus_size_t		tdb_basereg;
	uint32_t		tdb_base;

	struct rtw_txdesc	*tdb_desc;
};

#define RTW_NEXT_IDX(__htc, __idx)	(((__idx) + 1) % (__htc)->tdb_ndesc)

#define RTW_NEXT_DESC(__htc, __idx) \
    ((__htc)->tdb_physbase + \
     sizeof(struct rtw_txdesc) * RTW_NEXT_IDX((__htc), (__idx)))

SIMPLEQ_HEAD(rtw_txq, rtw_txsoft);

struct rtw_txsoft_blk {
	/* dirty/free s/w descriptors */
	struct rtw_txq		tsb_dirtyq;
	struct rtw_txq		tsb_freeq;
	u_int			tsb_ndesc;
	int			tsb_tx_timer;
	struct rtw_txsoft	*tsb_desc;
	u_int8_t			tsb_poll;
};

struct rtw_descs {
	struct rtw_txdesc	hd_txlo[RTW_NTXDESCLO];
	struct rtw_txdesc	hd_txmd[RTW_NTXDESCMD];
	struct rtw_txdesc	hd_txhi[RTW_NTXDESCMD];
	struct rtw_rxdesc	hd_rx[RTW_RXQLEN];
	struct rtw_txdesc	hd_bcn[RTW_NTXDESCBCN];
};
#define RTW_DESC_OFFSET(ring, i)	offsetof(struct rtw_descs, ring[i])
#define RTW_RING_OFFSET(ring)		RTW_DESC_OFFSET(ring, 0)
#define RTW_RING_BASE(sc, ring)		((sc)->sc_desc_physaddr + \
					 RTW_RING_OFFSET(ring))

/* Radio capture format for RTL8180. */

#define RTW_RX_RADIOTAP_PRESENT					\
	((1 << IEEE80211_RADIOTAP_TSFT)			|	\
	 (1 << IEEE80211_RADIOTAP_FLAGS)		|	\
	 (1 << IEEE80211_RADIOTAP_RATE)			|	\
	 (1 << IEEE80211_RADIOTAP_CHANNEL)		|	\
	 (1 << IEEE80211_RADIOTAP_LOCK_QUALITY)		|	\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL)		|	\
	 0)

struct rtw_rx_radiotap_header {
	struct ieee80211_radiotap_header	rr_ihdr;
	u_int64_t				rr_tsft;
	u_int8_t				rr_flags;
	u_int8_t				rr_rate;
	u_int16_t				rr_chan_freq;
	u_int16_t				rr_chan_flags;
	u_int16_t				rr_barker_lock;
	u_int8_t				rr_antsignal;
} __packed;

#define RTW_TX_RADIOTAP_PRESENT				\
	((1 << IEEE80211_RADIOTAP_FLAGS)	|	\
	 (1 << IEEE80211_RADIOTAP_RATE)		|	\
	 (1 << IEEE80211_RADIOTAP_CHANNEL)	|	\
	 0)

struct rtw_tx_radiotap_header {
	struct ieee80211_radiotap_header	rt_ihdr;
	u_int8_t				rt_flags;
	u_int8_t				rt_rate;
	u_int16_t				rt_chan_freq;
	u_int16_t				rt_chan_flags;
} __packed;

struct rtw_mtbl {
	int			(*mt_newstate)(struct ieee80211com *,
					enum ieee80211_state, int);
	void			(*mt_recv_mgmt)(struct ieee80211com *,
				    struct mbuf *, struct ieee80211_node *,
				    struct ieee80211_rxinfo *, int);
	struct ieee80211_node	*(*mt_node_alloc)(struct ieee80211com *);
	void			(*mt_node_free)(struct ieee80211com *,
					struct ieee80211_node *);
};

enum rtw_pwrstate { RTW_OFF = 0, RTW_SLEEP, RTW_ON };

struct rtw_phy {
	struct rtw_rf	*p_rf;
	struct rtw_regs	*p_regs;
};

struct rtw_bbpset {
	u_int	bb_antatten;
	u_int	bb_chestlim;
	u_int	bb_chsqlim;
	u_int	bb_ifagcdet;
	u_int	bb_ifagcini;
	u_int	bb_ifagclimit;
	u_int	bb_lnadet;
	u_int	bb_sys1;
	u_int	bb_sys2;
	u_int	bb_sys3;
	u_int	bb_trl;
	u_int	bb_txagc;
};

typedef int (*rtw_rf_write_t)(struct rtw_regs *, int, u_int,
    u_int32_t);

typedef void (*rtw_pwrstate_t)(struct rtw_regs *, enum rtw_pwrstate, int, int);

union rtw_keys {
	u_int8_t	rk_keys[4][16];
	u_int32_t	rk_words[16];
};

#define	RTW_LED_SLOW_MSEC	500
#define	RTW_LED_FAST_MSEC	100

struct rtw_led_state {
#define	RTW_LED0	0x1
#define	RTW_LED1	0x2
	u_int8_t	ls_slowblink:2;
	u_int8_t	ls_actblink:2;
	u_int8_t	ls_default:2;
	u_int8_t	ls_state;
	u_int8_t	ls_event;
#define	RTW_LED_S_RX	0x1
#define	RTW_LED_S_TX	0x2
#define	RTW_LED_S_SLOW	0x4
	struct timeout	ls_slow_ch;
	struct timeout	ls_fast_ch;
};

struct rtw_softc {
	struct device		sc_dev;
	struct ieee80211com	sc_ic;
	struct rtw_regs		sc_regs;
	bus_dma_tag_t		sc_dmat;
	u_int32_t		sc_flags;

	int			sc_rfchipid;
	enum rtw_locale		sc_locale;
	u_int8_t		sc_phydelay;
	struct rtw_bbpset       sc_bbpset;

	/* s/w Tx/Rx descriptors */
	struct rtw_txsoft_blk	sc_txsoft_blk[RTW_NTXPRI];
	struct rtw_txdesc_blk	sc_txdesc_blk[RTW_NTXPRI];

	struct rtw_rxsoft	sc_rxsoft[RTW_RXQLEN];
	struct rtw_rxdesc_blk	sc_rxdesc_blk;

	struct rtw_descs	*sc_descs;

	bus_dma_segment_t	sc_desc_segs;
	int			sc_desc_nsegs;
	bus_dmamap_t		sc_desc_dmamap;
#define	sc_desc_physaddr sc_desc_dmamap->dm_segs[0].ds_addr

	struct rtw_srom		sc_srom;

	enum rtw_pwrstate	sc_pwrstate;

	rtw_pwrstate_t		sc_pwrstate_cb;

	u_int16_t		sc_inten;
	int			(*sc_rf_init)(struct rtw_softc *, u_int,
				    u_int8_t, enum rtw_pwrstate);
	int			(*sc_rf_pwrstate)(struct rtw_softc *,
				    enum rtw_pwrstate);
	int			(*sc_rf_tune)(struct rtw_softc *, u_int);
	int			(*sc_rf_txpower)(struct rtw_softc *, u_int8_t);

	/* interrupt acknowledge hook */
	void			(*sc_intr_ack)(struct rtw_regs *);

	int			(*sc_enable)(struct rtw_softc *);
	void			(*sc_disable)(struct rtw_softc *);
	void			(*sc_power)(struct rtw_softc *, int);
	struct rtw_mtbl		sc_mtbl;

	caddr_t			sc_radiobpf;

	struct timeval		sc_last_beacon;
	struct timeout		sc_scan_to;
	u_int			sc_cur_chan;

	u_int32_t		sc_tsfth;	/* most significant TSFT bits */
	u_int32_t		sc_rcr;		/* RTW_RCR */
	u_int8_t		sc_csthr;	/* carrier-sense threshold */

	int			sc_do_tick;	/* indicate 1s ticks */
	struct timeval		sc_tick0;	/* first tick */

	u_int8_t		sc_rev;		/* PCI/Cardbus revision */

	u_int32_t		sc_anaparm[2];	/* RTW_ANAPARM_? registers */

	union {
		struct rtw_rx_radiotap_header	tap;
		u_int8_t			pad[64];
	} sc_rxtapu;
	union {
		struct rtw_tx_radiotap_header	tap;
		u_int8_t			pad[64];
	} sc_txtapu;
	union rtw_keys		sc_keys;
	int			sc_txkey;
	struct mbuf_queue	sc_beaconq;
	struct rtw_led_state	sc_led_state;
	u_int			sc_hwverid;
};

#define	sc_if		sc_ic.ic_if
#define sc_rxtap	sc_rxtapu.tap
#define sc_txtap	sc_txtapu.tap

void rtw_txdac_enable(struct rtw_softc *, int);
void rtw_anaparm_enable(struct rtw_regs *, int);
void rtw_config0123_enable(struct rtw_regs *, int);
void rtw_continuous_tx_enable(struct rtw_softc *, int);
void rtw_set_access(struct rtw_regs *, enum rtw_access);

void rtw_attach(struct rtw_softc *);
int rtw_detach(struct rtw_softc *);
int rtw_intr(void *);

void rtw_disable(struct rtw_softc *);
int rtw_enable(struct rtw_softc *);

int rtw_activate(struct device *, int);

const char *rtw_pwrstate_string(enum rtw_pwrstate);

#endif /* _DEV_IC_RTWVAR_H_ */
