/*	$OpenBSD: if_ipwvar.h,v 1.28 2016/09/05 10:17:30 tedu Exp $	*/

/*-
 * Copyright (c) 2004-2006
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct ipw_firmware {
	u_char	*data;
	size_t	size;
	u_char	*main;
	size_t	main_size;
	u_char	*ucode;
	size_t	ucode_size;
};

struct ipw_soft_bd {
	struct ipw_bd	*bd;
	int		type;
#define IPW_SBD_TYPE_NOASSOC	0
#define IPW_SBD_TYPE_COMMAND	1
#define IPW_SBD_TYPE_HEADER	2
#define IPW_SBD_TYPE_DATA	3
	void		*priv;
};

struct ipw_soft_hdr {
	struct ipw_hdr			hdr;
	bus_dmamap_t			map;
	SLIST_ENTRY(ipw_soft_hdr)	next;
};

struct ipw_soft_buf {
	struct mbuf			*m;
	struct ieee80211_node		*ni;
	bus_dmamap_t			map;
	SLIST_ENTRY(ipw_soft_buf)	next;
};

struct ipw_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_antsignal;
} __packed;

#define IPW_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL))

struct ipw_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define IPW_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

#define IPW_MAX_NSEG	1

struct ipw_softc {
	struct device			sc_dev;

	struct ieee80211com		sc_ic;
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);

	struct rwlock			sc_rwlock;

	bus_space_tag_t			sc_st;
	bus_space_handle_t		sc_sh;
	void 				*sc_ih;
	pci_chipset_tag_t		sc_pct;
	pcitag_t			sc_pcitag;
	bus_size_t			sc_sz;

	int				sc_tx_timer;

	bus_dma_tag_t			sc_dmat;

	bus_dmamap_t			tbd_map;
	bus_dmamap_t			rbd_map;
	bus_dmamap_t			status_map;
	bus_dmamap_t			cmd_map;

	bus_dma_segment_t		tbd_seg;
	bus_dma_segment_t		rbd_seg;
	bus_dma_segment_t		status_seg;
	bus_dma_segment_t		cmd_seg;

	struct ipw_bd			*tbd_list;
	struct ipw_bd			*rbd_list;
	struct ipw_status		*status_list;

	struct ipw_cmd			cmd;
	struct ipw_soft_bd		stbd_list[IPW_NTBD];
	struct ipw_soft_buf		tx_sbuf_list[IPW_NDATA];
	struct ipw_soft_hdr		shdr_list[IPW_NDATA];
	struct ipw_soft_bd		srbd_list[IPW_NRBD];
	struct ipw_soft_buf		rx_sbuf_list[IPW_NRBD];

	struct task			sc_scantask;
	struct task			sc_authandassoctask;

	SLIST_HEAD(, ipw_soft_hdr)	free_shdr;
	SLIST_HEAD(, ipw_soft_buf)	free_sbuf;

	uint32_t			table1_base;
	uint32_t			table2_base;

	uint32_t			txcur;
	uint32_t			txold;
	uint32_t			rxcur;
	int				txfree;

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;

	union {
		struct ipw_rx_radiotap_header th;
		uint8_t	pad[64];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct ipw_tx_radiotap_header th;
		uint8_t	pad[64];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int				sc_txtap_len;
#endif
};
