/*	$OpenBSD: athnvar.h,v 1.42 2021/04/15 18:25:43 stsp Exp $	*/

/*-
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
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

#ifdef notyet
#define ATHN_BT_COEXISTENCE	1
#endif

#ifdef ATHN_DEBUG
#define DPRINTF(x)	do { if (athn_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (athn_debug >= (n)) printf x; } while (0)
extern int athn_debug;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

#define LE_READ_4(p)	((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (p)[3] << 24)
#define LE_READ_2(p)	((p)[0] | (p)[1] << 8)

#define ATHN_RXBUFSZ	3872
#define ATHN_TXBUFSZ	4096

#define ATHN_NRXBUFS	64
#define ATHN_NTXBUFS	64	/* Shared between all Tx queues. */

struct athn_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsft;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_dbm_antsignal;
	uint8_t		wr_antenna;
} __packed;

#define ATHN_RX_RADIOTAP_PRESENT						\
	(1 << IEEE80211_RADIOTAP_TSFT |					\
	 1 << IEEE80211_RADIOTAP_FLAGS |				\
	 1 << IEEE80211_RADIOTAP_RATE |					\
	 1 << IEEE80211_RADIOTAP_CHANNEL |				\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL |			\
	 1 << IEEE80211_RADIOTAP_ANTENNA)

struct athn_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_rate;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define ATHN_TX_RADIOTAP_PRESENT						\
	(1 << IEEE80211_RADIOTAP_FLAGS |				\
	 1 << IEEE80211_RADIOTAP_RATE |					\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct athn_tx_buf {
	SIMPLEQ_ENTRY(athn_tx_buf)	bf_list;

	void				*bf_descs;
	bus_dmamap_t			bf_map;
	bus_addr_t			bf_daddr;

	struct mbuf			*bf_m;
	struct ieee80211_node		*bf_ni;
	int				bf_txmcs;
	int				bf_txflags;
#define ATHN_TXFLAG_PAPRD	(1 << 0)
#define ATHN_TXFLAG_CAB		(1 << 1) 
};

struct athn_txq {
	SIMPLEQ_HEAD(, athn_tx_buf)	head;
	void				*lastds;
	struct athn_tx_buf		*wait;
	int				queued;
};

struct athn_rx_buf {
	SIMPLEQ_ENTRY(athn_rx_buf)	bf_list;

	void				*bf_desc;
	bus_dmamap_t			bf_map;

	struct mbuf			*bf_m;
	bus_addr_t			bf_daddr;
};

struct athn_rxq {
	struct athn_rx_buf		*bf;

	void				*descs;
	void				*lastds;
	bus_dmamap_t			map;
	bus_dma_segment_t		seg;
	int				count;

	SIMPLEQ_HEAD(, athn_rx_buf)	head;
};

/* Software rate indexes. */
#define ATHN_RIDX_CCK1	0
#define ATHN_RIDX_CCK2	1
#define ATHN_RIDX_OFDM6	4
#define ATHN_RIDX_MCS0	12
#define ATHN_RIDX_MCS8	(ATHN_RIDX_MCS0 + 8)
#define ATHN_RIDX_MCS15	27
#define ATHN_RIDX_MAX	27
#define ATHN_MCS_MAX	15
#define ATHN_NUM_MCS	(ATHN_MCS_MAX + 1)
#define ATHN_IS_HT_RIDX(ridx)	((ridx) >= ATHN_RIDX_MCS0)
#define ATHN_IS_MIMO_RIDX(ridx)	((ridx) >= ATHN_RIDX_MCS8)

static const struct athn_rate {
	uint16_t	rate;		/* Rate in 500Kbps unit. */
	uint8_t		hwrate;		/* HW representation. */
	uint8_t		rspridx;	/* Control Response Frame rate index. */
	enum	ieee80211_phytype phy;
} athn_rates[] = {
	{    2, 0x1b, 0, IEEE80211_T_DS },
	{    4, 0x1a, 1, IEEE80211_T_DS },
	{   11, 0x19, 1, IEEE80211_T_DS },
	{   22, 0x18, 1, IEEE80211_T_DS },
	{   12, 0x0b, 4, IEEE80211_T_OFDM },
	{   18, 0x0f, 4, IEEE80211_T_OFDM },
	{   24, 0x0a, 6, IEEE80211_T_OFDM },
	{   36, 0x0e, 6, IEEE80211_T_OFDM },
	{   48, 0x09, 8, IEEE80211_T_OFDM },
	{   72, 0x0d, 8, IEEE80211_T_OFDM },
	{   96, 0x08, 8, IEEE80211_T_OFDM },
	{  108, 0x0c, 8, IEEE80211_T_OFDM },
	{   13, 0x80, 4, IEEE80211_T_OFDM },
	{   26, 0x81, 6, IEEE80211_T_OFDM },
	{   39, 0x82, 6, IEEE80211_T_OFDM },
	{   52, 0x83, 8, IEEE80211_T_OFDM },
	{   78, 0x84, 8, IEEE80211_T_OFDM },
	{  104, 0x85, 8, IEEE80211_T_OFDM },
	{  117, 0x86, 8, IEEE80211_T_OFDM },
	{  130, 0x87, 8, IEEE80211_T_OFDM },
	{   26, 0x88, 4, IEEE80211_T_OFDM },
	{   52, 0x89, 6, IEEE80211_T_OFDM },
	{   78, 0x8a, 8, IEEE80211_T_OFDM },
	{  104, 0x8b, 8, IEEE80211_T_OFDM },
	{  156, 0x8c, 8, IEEE80211_T_OFDM },
	{  208, 0x8d, 8, IEEE80211_T_OFDM },
	{  234, 0x8e, 8, IEEE80211_T_OFDM },
	{  260, 0x8f, 8, IEEE80211_T_OFDM }
};

struct athn_series {
	uint16_t	dur;
	uint8_t		hwrate;
};

struct athn_pier {
	uint8_t		fbin;
	const uint8_t	*pwr[AR_PD_GAINS_IN_MASK];
	const uint8_t	*vpd[AR_PD_GAINS_IN_MASK];
};

/*
 * Structures used to store initialization values.
 */
struct athn_ini {
	int		nregs;
	const uint16_t	*regs;
	const uint32_t	*vals_5g20;
	const uint32_t	*vals_5g40;
	const uint32_t	*vals_2g40;
	const uint32_t	*vals_2g20;
	int		ncmregs;
	const uint16_t	*cmregs;
	const uint32_t	*cmvals;
	int		nfastregs;
	const uint16_t	*fastregs;
	const uint32_t	*fastvals_5g20;
	const uint32_t	*fastvals_5g40;
};

struct athn_gain {
	int		nregs;
	const uint16_t	*regs;
	const uint32_t	*vals_5g;
	const uint32_t	*vals_2g;
};

struct athn_addac {
	int		nvals;
	const uint32_t	*vals;
};

struct athn_serdes {
	int		nvals;
	const uint32_t	*regs;
	const uint32_t	*vals;
};

/* Rx queue software indexes. */
#define ATHN_QID_LP		0
#define ATHN_QID_HP		1

/* Tx queue software indexes. */
#define ATHN_QID_AC_BE		0
#define ATHN_QID_PSPOLL		1
#define ATHN_QID_AC_BK		2
#define ATHN_QID_AC_VI		3
#define ATHN_QID_AC_VO		4
#define ATHN_QID_UAPSD		5
#define ATHN_QID_CAB		6
#define ATHN_QID_BEACON		7
#define ATHN_QID_COUNT		8

/* Map Access Category to Tx queue Id. */
static const uint8_t athn_ac2qid[EDCA_NUM_AC] = {
	ATHN_QID_AC_BE,	/* EDCA_AC_BE */
	ATHN_QID_AC_BK,	/* EDCA_AC_BK */
	ATHN_QID_AC_VI,	/* EDCA_AC_VI */
	ATHN_QID_AC_VO	/* EDCA_AC_VO */
};

static const uint8_t athn_5ghz_chans[] = {
	/* UNII 1. */
	36, 40, 44, 48,
	/* UNII 2. */
	52, 56, 60, 64,
	/* Middle band. */
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
	/* UNII 3. */
	149, 153, 157, 161, 165
};

/* Number of data bits per OFDM symbol for MCS[0-15]. */
/* See tables 20-29, 20-30, 20-33, 20-34. */
static const uint16_t ar_mcs_ndbps[][2] = {
	/* 20MHz  40MHz */
	{     26,    54 },	/* MCS0 */
	{     52,   108 },	/* MCS1 */
	{     78,   162 },	/* MCS2 */
	{    104,   216 },	/* MCS3 */
	{    156,   324 },	/* MCS4 */
	{    208,   432 },	/* MCS5 */
	{    234,   486 },	/* MCS6 */
	{    260,   540 },	/* MCS7 */
	{     26,   108 },	/* MCS8 */
	{     52,   216 },	/* MCS9 */
	{     78,   324 },	/* MCS10 */
	{    104,   432 },	/* MCS11 */
	{    156,   648 },	/* MCS12 */
	{    208,   864 },	/* MCS13 */
	{    234,   972 },	/* MCS14 */
	{    260,  1080 }	/* MCS15 */
};

#define ATHN_POWER_OFDM6	0
#define ATHN_POWER_OFDM9	1
#define ATHN_POWER_OFDM12	2
#define ATHN_POWER_OFDM18	3
#define ATHN_POWER_OFDM24	4
#define ATHN_POWER_OFDM36	5
#define ATHN_POWER_OFDM48	6
#define ATHN_POWER_OFDM54	7
#define ATHN_POWER_CCK1_LP	8
#define ATHN_POWER_CCK2_LP	9
#define ATHN_POWER_CCK2_SP	10
#define ATHN_POWER_CCK55_LP	11
#define ATHN_POWER_CCK55_SP	12
#define ATHN_POWER_CCK11_LP	13
#define ATHN_POWER_CCK11_SP	14
#define ATHN_POWER_XR		15
#define ATHN_POWER_HT20(mcs)	(16 + (mcs))
#define ATHN_POWER_HT40(mcs)	(40 + (mcs))
#define ATHN_POWER_CCK_DUP	64
#define ATHN_POWER_OFDM_DUP	65
#define ATHN_POWER_CCK_EXT	66
#define ATHN_POWER_OFDM_EXT	67
#define ATHN_POWER_COUNT	68

#define ATHN_NUM_LEGACY_RATES	IEEE80211_RATE_MAXSIZE
#define ATHN_NUM_RATES		(ATHN_NUM_LEGACY_RATES + ATHN_NUM_MCS)
struct athn_node {
	struct ieee80211_node		ni;
	struct ieee80211_amrr_node	amn;
	struct ieee80211_ra_node	rn;
	uint8_t				ridx[ATHN_NUM_RATES];
	uint8_t				fallback[ATHN_NUM_RATES];
	uint8_t				sta_index;
};

/*
 * Adaptive noise immunity state.
 */
#define ATHN_ANI_PERIOD		100
#define ATHN_ANI_RSSI_THR_HIGH	40
#define ATHN_ANI_RSSI_THR_LOW	7
struct athn_ani {
	uint8_t		noise_immunity_level;
	uint8_t		spur_immunity_level;
	uint8_t		firstep_level;
	uint8_t		ofdm_weak_signal;
	uint8_t		cck_weak_signal;

	uint32_t	listen_time;

	uint32_t	ofdm_trig_high;
	uint32_t	ofdm_trig_low;

	int32_t		cck_trig_high;
	int32_t		cck_trig_low;

	uint32_t	ofdm_phy_err_base;
	uint32_t	cck_phy_err_base;
	uint32_t	ofdm_phy_err_count;
	uint32_t	cck_phy_err_count;

	uint32_t	cyccnt;
	uint32_t	txfcnt;
	uint32_t	rxfcnt;
};

struct athn_iq_cal {
	uint32_t	pwr_meas_i;
	uint32_t	pwr_meas_q;
	int32_t		iq_corr_meas;
};

struct athn_adc_cal {
	uint32_t	pwr_meas_odd_i;
	uint32_t	pwr_meas_even_i;
	uint32_t	pwr_meas_odd_q;
	uint32_t	pwr_meas_even_q;
};

struct athn_calib {
	int			nsamples;
	struct athn_iq_cal	iq[AR_MAX_CHAINS];
	struct athn_adc_cal	adc_gain[AR_MAX_CHAINS];
	struct athn_adc_cal	adc_dc_offset[AR_MAX_CHAINS];
};

#define ATHN_NF_CAL_HIST_MAX	5

struct athn_softc;

struct athn_ops {
	/* Bus callbacks. */
	uint32_t	(*read)(struct athn_softc *, uint32_t);
	void		(*write)(struct athn_softc *, uint32_t, uint32_t);
	void		(*write_barrier)(struct athn_softc *);

	void	(*setup)(struct athn_softc *);
	void	(*set_txpower)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	void	(*spur_mitigate)(struct athn_softc *,
		    struct ieee80211_channel *, struct ieee80211_channel *);
	const struct ar_spur_chan *
		(*get_spur_chans)(struct athn_softc *, int);
	void	(*init_from_rom)(struct athn_softc *,
		    struct ieee80211_channel *, struct ieee80211_channel *);
	int	(*set_synth)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	int	(*read_rom_data)(struct athn_softc *, uint32_t, void *, int);
	const uint8_t *
		(*get_rom_template)(struct athn_softc *, uint8_t);
	void	(*swap_rom)(struct athn_softc *);
	void	(*olpc_init)(struct athn_softc *);
	void	(*olpc_temp_compensation)(struct athn_softc *);
	/* GPIO callbacks. */
	int	(*gpio_read)(struct athn_softc *, int);
	void	(*gpio_write)(struct athn_softc *, int, int);
	void	(*gpio_config_input)(struct athn_softc *, int);
	void	(*gpio_config_output)(struct athn_softc *, int, int);
	void	(*rfsilent_init)(struct athn_softc *);
	/* DMA callbacks. */
	int	(*dma_alloc)(struct athn_softc *);
	void	(*dma_free)(struct athn_softc *);
	void	(*rx_enable)(struct athn_softc *);
	int	(*intr)(struct athn_softc *);
	int	(*tx)(struct athn_softc *, struct mbuf *,
		    struct ieee80211_node *, int);
	/* PHY callbacks. */
	void	(*set_rf_mode)(struct athn_softc *,
		    struct ieee80211_channel *);
	int	(*rf_bus_request)(struct athn_softc *);
	void	(*rf_bus_release)(struct athn_softc *);
	void	(*set_phy)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	void	(*set_delta_slope)(struct athn_softc *,
		    struct ieee80211_channel *, struct ieee80211_channel *);
	void	(*enable_antenna_diversity)(struct athn_softc *);
	void	(*init_baseband)(struct athn_softc *);
	void	(*disable_phy)(struct athn_softc *);
	void	(*set_rxchains)(struct athn_softc *);
	void	(*noisefloor_calib)(struct athn_softc *);
	void	(*init_noisefloor_calib)(struct athn_softc *);
	int	(*get_noisefloor)(struct athn_softc *);
	void	(*apply_noisefloor)(struct athn_softc *);
	void	(*do_calib)(struct athn_softc *);
	void	(*next_calib)(struct athn_softc *);
	void	(*hw_init)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	void	(*get_paprd_masks)(struct athn_softc *sc,
		    struct ieee80211_channel *, uint32_t *, uint32_t *);
	/* ANI callbacks. */
	void	(*set_noise_immunity_level)(struct athn_softc *, int);
	void	(*enable_ofdm_weak_signal)(struct athn_softc *);
	void	(*disable_ofdm_weak_signal)(struct athn_softc *);
	void	(*set_cck_weak_signal)(struct athn_softc *, int);
	void	(*set_firstep_level)(struct athn_softc *, int);
	void	(*set_spur_immunity_level)(struct athn_softc *, int);
};

struct athn_softc {
	struct device			sc_dev;
	struct ieee80211com		sc_ic;

	int				(*sc_enable)(struct athn_softc *);
	void				(*sc_disable)(struct athn_softc *);
	void				(*sc_power)(struct athn_softc *, int);
	void				(*sc_disable_aspm)(struct athn_softc *);
	void				(*sc_enable_extsynch)(
					    struct athn_softc *);

	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);

	bus_dma_tag_t			sc_dmat;

	struct timeout			scan_to;
	struct timeout			calib_to;
	struct ieee80211_amrr		amrr;

	u_int				flags;
#define ATHN_FLAG_PCIE			(1 << 0)
#define ATHN_FLAG_USB			(1 << 1)
#define ATHN_FLAG_OLPC			(1 << 2)
#define ATHN_FLAG_PAPRD			(1 << 3)
#define ATHN_FLAG_FAST_PLL_CLOCK	(1 << 4)
#define ATHN_FLAG_RFSILENT		(1 << 5)
#define ATHN_FLAG_RFSILENT_REVERSED	(1 << 6)
#define ATHN_FLAG_BTCOEX2WIRE		(1 << 7)
#define ATHN_FLAG_BTCOEX3WIRE		(1 << 8)
/* Shortcut. */
#define ATHN_FLAG_BTCOEX	(ATHN_FLAG_BTCOEX2WIRE | ATHN_FLAG_BTCOEX3WIRE)
#define ATHN_FLAG_11A			(1 << 9)
#define ATHN_FLAG_11G			(1 << 10)
#define ATHN_FLAG_11N			(1 << 11)
#define ATHN_FLAG_AN_TOP2_FIXUP		(1 << 12)
#define ATHN_FLAG_NON_ENTERPRISE	(1 << 13)
#define ATHN_FLAG_3TREDUCE_CHAIN	(1 << 14)

	uint8_t				ngpiopins;
	int				led_pin;
	int				rfsilent_pin;
	int				led_state;
	uint32_t			isync;
	uint32_t			imask;

	uint16_t			mac_ver;
	uint8_t				mac_rev;
	uint8_t				rf_rev;
	uint16_t			eep_rev;

	uint8_t				txchainmask;
	uint8_t				rxchainmask;
	uint8_t				ntxchains;
	uint8_t				nrxchains;

	uint8_t				sup_calib_mask;
	uint8_t				cur_calib_mask;
#define ATHN_CAL_IQ		(1 << 0)
#define ATHN_CAL_ADC_GAIN	(1 << 1)
#define ATHN_CAL_ADC_DC		(1 << 2)
#define ATHN_CAL_TEMP		(1 << 3)

	struct ieee80211_channel	*curchan;
	struct ieee80211_channel	*curchanext;

	/* Open Loop Power Control. */
	int8_t				tx_gain_tbl[AR9280_TX_GAIN_TABLE_SIZE];
	int8_t				pdadc;
	int8_t				tcomp;
	int				olpc_ticks;
	int				iqcal_ticks;

	/* PA predistortion. */
	uint16_t			gain1[AR_MAX_CHAINS];
	uint32_t			txgain[AR9003_TX_GAIN_TABLE_SIZE];
	int16_t				pa_in[AR_MAX_CHAINS]
					     [AR9003_PAPRD_MEM_TAB_SIZE];
	int16_t				angle[AR_MAX_CHAINS]
					     [AR9003_PAPRD_MEM_TAB_SIZE];
	int32_t				trainpow;
	uint8_t				paprd_curchain;

	uint32_t			rwbuf[64];

	int				kc_entries;

	void				*eep;
	const void			*eep_def;
	uint32_t			eep_base;
	uint32_t			eep_size;

	struct athn_rxq			rxq[2];
	struct athn_txq			txq[31];

	void				*descs;
	bus_dmamap_t			map;
	bus_dma_segment_t		seg;
	SIMPLEQ_HEAD(, athn_tx_buf)	txbufs;
	struct athn_tx_buf		*bcnbuf;
	struct athn_tx_buf		txpool[ATHN_NTXBUFS];

	bus_dmamap_t			txsmap;
	bus_dma_segment_t		txsseg;
	void				*txsring;
	int				txscur;

	int				sc_if_flags;
	int				sc_tx_timer;

	const struct athn_ini		*ini;
	const struct athn_gain		*rx_gain;
	const struct athn_gain		*tx_gain;
	const struct athn_addac		*addac;
	const struct athn_serdes	*serdes;
	uint32_t			workaround;
	uint32_t			obs_off;
	uint32_t			gpio_input_en_off;

	struct athn_ops			ops;

	int				fixed_ridx;

	int16_t				cca_min_2g;
	int16_t				cca_max_2g;
	int16_t				cca_min_5g;
	int16_t				cca_max_5g;
	struct {
		int16_t	nf[AR_MAX_CHAINS];
		int16_t	nf_ext[AR_MAX_CHAINS];
	}				nf_hist[ATHN_NF_CAL_HIST_MAX];
	int				nf_hist_cur;
	int				nf_hist_nvalid;
	int16_t				nf_priv[AR_MAX_CHAINS];
	int16_t				nf_ext_priv[AR_MAX_CHAINS];
	int				nf_calib_pending;
	int				nf_calib_ticks;
	int				pa_calib_ticks;

	struct athn_calib		calib;
	struct athn_ani			ani;

#if NBPFILTER > 0
	caddr_t				sc_drvbpf;

	union {
		struct athn_rx_radiotap_header th;
		uint8_t pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_rxtapu;
#define sc_rxtap			sc_rxtapu.th
	int				sc_rxtap_len;

	union {
		struct athn_tx_radiotap_header th;
		uint8_t pad[IEEE80211_RADIOTAP_HDRLEN];
	} sc_txtapu;
#define sc_txtap			sc_txtapu.th
	int				sc_txtap_len;
#endif
};

extern int	athn_attach(struct athn_softc *);
extern void	athn_detach(struct athn_softc *);
extern void	athn_suspend(struct athn_softc *);
extern void	athn_wakeup(struct athn_softc *);
extern int	athn_intr(void *);
