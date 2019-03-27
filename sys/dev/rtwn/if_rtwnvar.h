/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef IF_RTWNVAR_H
#define IF_RTWNVAR_H

#include "opt_rtwn.h"

#define RTWN_TX_DESC_SIZE	64

#define RTWN_BCN_MAX_SIZE	512
#define RTWN_CAM_ENTRY_LIMIT	64

#define RTWN_MACID_BC		1	/* Broadcast. */
#define RTWN_MACID_UNDEFINED	0x7fff
#define RTWN_MACID_VALID 	0x8000
#define RTWN_MACID_LIMIT	128

#define RTWN_TX_TIMEOUT		5000	/* ms */
#define RTWN_MAX_EPOUT		4
#define RTWN_PORT_COUNT		2

#define RTWN_LED_LINK		0
#define RTWN_LED_DATA		1

struct rtwn_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint64_t	wr_tsft;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	int8_t		wr_dbm_antsignal;
	int8_t		wr_dbm_antnoise;
} __packed __aligned(8);

#define RTWN_RX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_TSFT |			\
	 1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_RATE |			\
	 1 << IEEE80211_RADIOTAP_CHANNEL |		\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL |	\
	 1 << IEEE80211_RADIOTAP_DBM_ANTNOISE)

struct rtwn_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint8_t		wt_pad;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RTWN_TX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |		\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

struct rtwn_tx_buf {
	uint8_t		txd[RTWN_TX_DESC_SIZE];
} __attribute__((aligned(4)));

#define RTWN_PHY_STATUS_SIZE	32
struct rtwn_tx_phystat {
	uint32_t	phydw[RTWN_PHY_STATUS_SIZE / sizeof(uint32_t)];
};


struct rtwn_softc;

union sec_param {
	struct ieee80211_key	key;
	int			macid;
};

#define CMD_FUNC_PROTO		void (*func)(struct rtwn_softc *, \
				    union sec_param *)

struct rtwn_cmdq {
	union sec_param		data;
	CMD_FUNC_PROTO;
};
#define RTWN_CMDQ_SIZE		16

struct rtwn_node {
	struct ieee80211_node	ni;	/* must be the first */
	int			id;

	struct rtwn_tx_phystat	last_physt;
	int			avg_pwdb;
};
#define RTWN_NODE(ni)		((struct rtwn_node *)(ni))

struct rtwn_vap {
	struct ieee80211vap	vap;
	int			id;
#define RTWN_VAP_ID_INVALID	-1
	int			curr_mode;

	struct rtwn_tx_buf	bcn_desc;
	struct mbuf		*bcn_mbuf;
	struct timeout_task	tx_beacon_csa;

	struct callout		tsf_sync_adhoc;
	struct task		tsf_sync_adhoc_task;

	const struct ieee80211_key	*keys[IEEE80211_WEP_NKID];

	int			(*newstate)(struct ieee80211vap *,
				    enum ieee80211_state, int);
	void			(*recv_mgmt)(struct ieee80211_node *,
				    struct mbuf *, int,
				    const struct ieee80211_rx_stats *,
				    int, int);
};
#define	RTWN_VAP(vap)		((struct rtwn_vap *)(vap))

/*
 * Rx data types.
 */
enum {
	RTWN_RX_DATA,
	RTWN_RX_TX_REPORT,
	RTWN_RX_OTHER
};

/*
 * Firmware reset reasons.
 */
enum {
	RTWN_FW_RESET_DOWNLOAD,
	RTWN_FW_RESET_CHECKSUM,
	RTWN_FW_RESET_SHUTDOWN
};

/*
 * Rate control algorithm selection.
 */
enum {
	RTWN_RATECTL_NONE,
	RTWN_RATECTL_NET80211,
	RTWN_RATECTL_FW,
	RTWN_RATECTL_MAX
};

/*
 * Control h/w crypto usage.
 */
enum {
	RTWN_CRYPTO_SW,
	RTWN_CRYPTO_PAIR,
	RTWN_CRYPTO_FULL,
	RTWN_CRYPTO_MAX,
};

struct rtwn_softc {
	struct ieee80211com	sc_ic;
	struct mbufq		sc_snd;
	device_t		sc_dev;

#if 1
	int			sc_ht40;
#endif
	uint32_t		sc_debug;
	int			sc_hwcrypto;
	int			sc_ratectl_sysctl;
	int			sc_ratectl;

	uint8_t			sc_detached;
	uint8_t			sc_flags;
/* Device flags */
#define RTWN_FLAG_CCK_HIPWR	0x01
#define RTWN_FLAG_EXT_HDR	0x02
#define RTWN_FLAG_CAM_FIXED	0x04
/* Driver state */
#define RTWN_STARTED		0x08
#define RTWN_RUNNING		0x10
#define RTWN_FW_LOADED		0x20
#define RTWN_TEMP_MEASURED	0x40
#define RTWN_RCR_LOCKED		0x80

#define RTWN_CHIP_HAS_BCNQ1(_sc)	\
	((_sc)->bcn_status_reg[0] != (_sc)->bcn_status_reg[1])

	void			*sc_priv;
	const char		*name;
	int			sc_ant;

	struct rtwn_tx_phystat	last_physt;
	uint8_t			thcal_temp;
	int			cur_bcnq_id;

	int			nvaps;
	int			ap_vaps;
	int			bcn_vaps;
	int			mon_vaps;

	int			vaps_running;
	int			monvaps_running;

	uint16_t		next_rom_addr;
	uint8_t			keys_bmap[howmany(RTWN_CAM_ENTRY_LIMIT, NBBY)];

	struct rtwn_vap		*vaps[RTWN_PORT_COUNT];
	struct ieee80211_node	*node_list[RTWN_MACID_LIMIT];
	struct mtx		nt_mtx;

	struct callout		sc_calib_to;
	struct callout		sc_pwrmode_init;
#ifndef D4054
	struct callout		sc_watchdog_to;
	int			sc_tx_timer;
#endif

	struct mtx		sc_mtx;

	struct rtwn_cmdq	cmdq[RTWN_CMDQ_SIZE];
	struct mtx		cmdq_mtx;
	struct task		cmdq_task;
	uint8_t			cmdq_first;
	uint8_t			cmdq_last;

	struct wmeParams	cap_wmeParams[WME_NUM_AC];

	struct rtwn_rx_radiotap_header	sc_rxtap;
	struct rtwn_tx_radiotap_header	sc_txtap;

	int			ntxchains;
	int			nrxchains;

	int			ledlink;
	uint8_t			thermal_meter;

	int			sc_tx_n_active;
	uint8_t			qfullmsk;

	/* Firmware-specific */
	const char		*fwname;
	uint16_t		fwver;
	uint16_t		fwsig;
	int			fwcur;

	void		(*sc_node_free)(struct ieee80211_node *);
	void		(*sc_scan_curchan)(struct ieee80211_scan_state *,
			    unsigned long);

	/* Interface-specific. */
	int		(*sc_write_1)(struct rtwn_softc *, uint16_t,
			    uint8_t);
	int		(*sc_write_2)(struct rtwn_softc *, uint16_t,
			    uint16_t);
	int		(*sc_write_4)(struct rtwn_softc *, uint16_t,
			    uint32_t);
	uint8_t		(*sc_read_1)(struct rtwn_softc *, uint16_t);
	uint16_t	(*sc_read_2)(struct rtwn_softc *, uint16_t);
	uint32_t	(*sc_read_4)(struct rtwn_softc *, uint16_t);
	/* XXX eliminate */
	void		(*sc_delay)(struct rtwn_softc *, int);
	int		(*sc_tx_start)(struct rtwn_softc *,
			    struct ieee80211_node *, struct mbuf *, uint8_t *,
			    uint8_t, int);
	void		(*sc_start_xfers)(struct rtwn_softc *);
	void		(*sc_reset_lists)(struct rtwn_softc *,
			    struct ieee80211vap *);
	void		(*sc_abort_xfers)(struct rtwn_softc *);
	int		(*sc_fw_write_block)(struct rtwn_softc *,
			    const uint8_t *, uint16_t, int);
	uint16_t	(*sc_get_qmap)(struct rtwn_softc *);
	void		(*sc_set_desc_addr)(struct rtwn_softc *);
	void		(*sc_drop_incorrect_tx)(struct rtwn_softc *);
	void		(*sc_beacon_update_begin)(struct rtwn_softc *,
			    struct ieee80211vap *);
	void		(*sc_beacon_update_end)(struct rtwn_softc *,
			    struct ieee80211vap *);
	void		(*sc_beacon_unload)(struct rtwn_softc *, int);

	/* XXX drop checks for PCIe? */
	int		bcn_check_interval;

	/* Device-specific. */
	uint32_t	(*sc_rf_read)(struct rtwn_softc *, int, uint8_t);
	void		(*sc_rf_write)(struct rtwn_softc *, int, uint8_t,
			    uint32_t);
	int		(*sc_check_condition)(struct rtwn_softc *,
			    const uint8_t[]);
	void		(*sc_efuse_postread)(struct rtwn_softc *);
	void		(*sc_parse_rom)(struct rtwn_softc *, uint8_t *);
	void		(*sc_set_led)(struct rtwn_softc *, int, int);
	int		(*sc_power_on)(struct rtwn_softc *);
	void		(*sc_power_off)(struct rtwn_softc *);
#ifndef RTWN_WITHOUT_UCODE
	void		(*sc_fw_reset)(struct rtwn_softc *, int);
	void		(*sc_fw_download_enable)(struct rtwn_softc *, int);
#endif
	int		(*sc_llt_init)(struct rtwn_softc *);
	int		(*sc_set_page_size)(struct rtwn_softc *);
	void		(*sc_lc_calib)(struct rtwn_softc *);
	void		(*sc_iq_calib)(struct rtwn_softc *);
	void		(*sc_read_chipid_vendor)(struct rtwn_softc *,
			    uint32_t);
	void		(*sc_adj_devcaps)(struct rtwn_softc *);
	void		(*sc_vap_preattach)(struct rtwn_softc *,
			    struct ieee80211vap *);
	void		(*sc_postattach)(struct rtwn_softc *);
	void		(*sc_detach_private)(struct rtwn_softc *);
	void		(*sc_fill_tx_desc)(struct rtwn_softc *,
			    struct ieee80211_node *, struct mbuf *,
			    void *, uint8_t, int);
	void		(*sc_fill_tx_desc_raw)(struct rtwn_softc *,
			    struct ieee80211_node *, struct mbuf *,
			    void *, const struct ieee80211_bpf_params *);
	void		(*sc_fill_tx_desc_null)(struct rtwn_softc *,
			    void *, int, int, int);
	void		(*sc_dump_tx_desc)(struct rtwn_softc *, const void *);
	uint8_t		(*sc_tx_radiotap_flags)(const void *);
	uint8_t		(*sc_rx_radiotap_flags)(const void *);
	void		(*sc_beacon_init)(struct rtwn_softc *, void *, int);
	void		(*sc_beacon_enable)(struct rtwn_softc *, int, int);
	void		(*sc_beacon_set_rate)(void *, int);
	void		(*sc_beacon_select)(struct rtwn_softc *, int);
	void		(*sc_set_chan)(struct rtwn_softc *,
			    struct ieee80211_channel *);
	void		(*sc_set_media_status)(struct rtwn_softc *, int);
#ifndef RTWN_WITHOUT_UCODE
	int		(*sc_set_rsvd_page)(struct rtwn_softc *, int, int,
			    int);
	int		(*sc_set_pwrmode)(struct rtwn_softc *,
			    struct ieee80211vap *, int);
	void		(*sc_set_rssi)(struct rtwn_softc *);
#endif
	void		(*sc_get_rx_stats)(struct rtwn_softc *,
			    struct ieee80211_rx_stats *, const void *,
			    const void *);
	int8_t		(*sc_get_rssi_cck)(struct rtwn_softc *, void *);
	int8_t		(*sc_get_rssi_ofdm)(struct rtwn_softc *, void *);
	int		(*sc_classify_intr)(struct rtwn_softc *, void *, int);
	void		(*sc_handle_tx_report)(struct rtwn_softc *, uint8_t *,
			    int);
	void		(*sc_handle_c2h_report)(struct rtwn_softc *,
			    uint8_t *, int);
	int		(*sc_check_frame)(struct rtwn_softc *, struct mbuf *);
	void		(*sc_temp_measure)(struct rtwn_softc *);
	uint8_t		(*sc_temp_read)(struct rtwn_softc *);
	void		(*sc_init_tx_agg)(struct rtwn_softc *);
	void		(*sc_init_rx_agg)(struct rtwn_softc *);
	void		(*sc_init_intr)(struct rtwn_softc *);
	void		(*sc_init_ampdu)(struct rtwn_softc *);
	void		(*sc_init_edca)(struct rtwn_softc *);
	void		(*sc_init_bb)(struct rtwn_softc *);
	void		(*sc_init_rf)(struct rtwn_softc *);
	void		(*sc_init_antsel)(struct rtwn_softc *);
	void		(*sc_post_init)(struct rtwn_softc *);
	int		(*sc_init_bcnq1_boundary)(struct rtwn_softc *);

	const uint8_t			*chan_list_5ghz[3];
	int				chan_num_5ghz[3];

	const struct rtwn_mac_prog	*mac_prog;
	int				mac_size;
	const struct rtwn_bb_prog	*bb_prog;
	int				bb_size;
	const struct rtwn_agc_prog	*agc_prog;
	int				agc_size;
	const struct rtwn_rf_prog	*rf_prog;

	int				page_count;
	int				pktbuf_count;

	int				ackto;

	int				npubqpages;
	int				nhqpages;
	int				nnqpages;
	int				nlqpages;
	int				page_size;

	int				txdesc_len;
	int				efuse_maxlen;
	int				efuse_maplen;

	uint16_t			rx_dma_size;

	int				macid_limit;
	int				cam_entry_limit;
	int				fwsize_limit;
	int				temp_delta;

	uint16_t			bcn_status_reg[RTWN_PORT_COUNT];
	uint32_t			rcr;	/* Rx filter */
};
MALLOC_DECLARE(M_RTWN_PRIV);

#define	RTWN_LOCK(sc)			mtx_lock(&(sc)->sc_mtx)
#define	RTWN_UNLOCK(sc)		mtx_unlock(&(sc)->sc_mtx)
#define	RTWN_ASSERT_LOCKED(sc)		mtx_assert(&(sc)->sc_mtx, MA_OWNED)

#define RTWN_CMDQ_LOCK_INIT(sc) \
	mtx_init(&(sc)->cmdq_mtx, "cmdq lock", NULL, MTX_DEF)
#define RTWN_CMDQ_LOCK(sc)		mtx_lock(&(sc)->cmdq_mtx)
#define RTWN_CMDQ_UNLOCK(sc)		mtx_unlock(&(sc)->cmdq_mtx)
#define RTWN_CMDQ_LOCK_INITIALIZED(sc)	mtx_initialized(&(sc)->cmdq_mtx)
#define RTWN_CMDQ_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->cmdq_mtx)

#define RTWN_NT_LOCK_INIT(sc) \
	mtx_init(&(sc)->nt_mtx, "node table lock", NULL, MTX_DEF)
#define RTWN_NT_LOCK(sc)		mtx_lock(&(sc)->nt_mtx)
#define RTWN_NT_UNLOCK(sc)		mtx_unlock(&(sc)->nt_mtx)
#define RTWN_NT_LOCK_INITIALIZED(sc)	mtx_initialized(&(sc)->nt_mtx)
#define RTWN_NT_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->nt_mtx)


void	rtwn_sysctlattach(struct rtwn_softc *);

int	rtwn_attach(struct rtwn_softc *);
void	rtwn_detach(struct rtwn_softc *);
void	rtwn_resume(struct rtwn_softc *);
void	rtwn_suspend(struct rtwn_softc *);


/* Interface-specific. */
#define rtwn_write_1(_sc, _addr, _val) \
	(((_sc)->sc_write_1)((_sc), (_addr), (_val)))
#define rtwn_write_2(_sc, _addr, _val) \
	(((_sc)->sc_write_2)((_sc), (_addr), (_val)))
#define rtwn_write_4(_sc, _addr, _val) \
	(((_sc)->sc_write_4)((_sc), (_addr), (_val)))
#define rtwn_read_1(_sc, _addr) \
	(((_sc)->sc_read_1)((_sc), (_addr)))
#define rtwn_read_2(_sc, _addr) \
	(((_sc)->sc_read_2)((_sc), (_addr)))
#define rtwn_read_4(_sc, _addr) \
	(((_sc)->sc_read_4)((_sc), (_addr)))
#define rtwn_delay(_sc, _usec) \
	(((_sc)->sc_delay)((_sc), (_usec)))
#define rtwn_tx_start(_sc, _ni, _m, _desc, _type, _id) \
	(((_sc)->sc_tx_start)((_sc), (_ni), (_m), (_desc), (_type), (_id)))
#define rtwn_start_xfers(_sc) \
	(((_sc)->sc_start_xfers)((_sc)))
#define rtwn_reset_lists(_sc, _vap) \
	(((_sc)->sc_reset_lists)((_sc), (_vap)))
#define rtwn_abort_xfers(_sc) \
	(((_sc)->sc_abort_xfers)((_sc)))
#define rtwn_fw_write_block(_sc, _buf, _reg, _len) \
	(((_sc)->sc_fw_write_block)((_sc), (_buf), (_reg), (_len)))
#define rtwn_get_qmap(_sc) \
	(((_sc)->sc_get_qmap)((_sc)))
#define rtwn_set_desc_addr(_sc) \
	(((_sc)->sc_set_desc_addr)((_sc)))
#define rtwn_drop_incorrect_tx(_sc) \
	(((_sc)->sc_drop_incorrect_tx)((_sc)))
#define rtwn_beacon_update_begin(_sc, _vap) \
	(((_sc)->sc_beacon_update_begin)((_sc), (_vap)))
#define rtwn_beacon_update_end(_sc, _vap) \
	(((_sc)->sc_beacon_update_end)((_sc), (_vap)))
#define rtwn_beacon_unload(_sc, _id) \
	(((_sc)->sc_beacon_unload)((_sc), (_id)))

/* Aliases. */
#define	rtwn_bb_write		rtwn_write_4
#define	rtwn_bb_read		rtwn_read_4
#define	rtwn_bb_setbits		rtwn_setbits_4

/* Device-specific. */
#define rtwn_rf_read(_sc, _chain, _addr) \
	(((_sc)->sc_rf_read)((_sc), (_chain), (_addr)))
#define rtwn_rf_write(_sc, _chain, _addr, _val) \
	(((_sc)->sc_rf_write)((_sc), (_chain), (_addr), (_val)))
#define rtwn_check_condition(_sc, _cond) \
	(((_sc)->sc_check_condition)((_sc), (_cond)))
#define rtwn_efuse_postread(_sc) \
	(((_sc)->sc_efuse_postread)((_sc)))
#define rtwn_parse_rom(_sc, _rom) \
	(((_sc)->sc_parse_rom)((_sc), (_rom)))
#define rtwn_set_led(_sc, _led, _on) \
	(((_sc)->sc_set_led)((_sc), (_led), (_on)))
#define rtwn_get_rx_stats(_sc, _rxs, _desc, _physt) \
	(((_sc)->sc_get_rx_stats((_sc), (_rxs), (_desc), (_physt))))
#define rtwn_get_rssi_cck(_sc, _physt) \
	(((_sc)->sc_get_rssi_cck)((_sc), (_physt)))
#define rtwn_get_rssi_ofdm(_sc, _physt) \
	(((_sc)->sc_get_rssi_ofdm)((_sc), (_physt)))
#define rtwn_power_on(_sc) \
	(((_sc)->sc_power_on)((_sc)))
#define rtwn_power_off(_sc) \
	(((_sc)->sc_power_off)((_sc)))
#ifndef RTWN_WITHOUT_UCODE
#define rtwn_fw_reset(_sc, _reason) \
	(((_sc)->sc_fw_reset)((_sc), (_reason)))
#define rtwn_fw_download_enable(_sc, _enable) \
	(((_sc)->sc_fw_download_enable)((_sc), (_enable)))
#endif
#define rtwn_llt_init(_sc) \
	(((_sc)->sc_llt_init)((_sc)))
#define rtwn_set_page_size(_sc) \
	(((_sc)->sc_set_page_size)((_sc)))
#define rtwn_lc_calib(_sc) \
	(((_sc)->sc_lc_calib)((_sc)))
#define rtwn_iq_calib(_sc) \
	(((_sc)->sc_iq_calib)((_sc)))
#define rtwn_read_chipid_vendor(_sc, _reg) \
	(((_sc)->sc_read_chipid_vendor)((_sc), (_reg)))
#define rtwn_adj_devcaps(_sc) \
	(((_sc)->sc_adj_devcaps)((_sc)))
#define rtwn_vap_preattach(_sc, _vap) \
	(((_sc)->sc_vap_preattach)((_sc), (_vap)))
#define rtwn_postattach(_sc) \
	(((_sc)->sc_postattach)((_sc)))
#define rtwn_detach_private(_sc) \
	(((_sc)->sc_detach_private)((_sc)))
#define rtwn_fill_tx_desc(_sc, _ni, _m, \
	    _buf, _ridx, _maxretry) \
	(((_sc)->sc_fill_tx_desc)((_sc), (_ni), \
	    (_m), (_buf), (_ridx), (_maxretry)))
#define rtwn_fill_tx_desc_raw(_sc, _ni, _m, \
	    _buf, _params) \
	(((_sc)->sc_fill_tx_desc_raw)((_sc), (_ni), \
	    (_m), (_buf), (_params)))
#define rtwn_fill_tx_desc_null(_sc, _buf, _11b, _qos, _id) \
	(((_sc)->sc_fill_tx_desc_null)((_sc), \
	    (_buf), (_11b), (_qos), (_id)))
#define rtwn_dump_tx_desc(_sc, _desc) \
	(((_sc)->sc_dump_tx_desc)((_sc), (_desc)))
#define rtwn_tx_radiotap_flags(_sc, _buf) \
	(((_sc)->sc_tx_radiotap_flags)((_buf)))
#define rtwn_rx_radiotap_flags(_sc, _buf) \
	(((_sc)->sc_rx_radiotap_flags)((_buf)))
#define rtwn_set_chan(_sc, _c) \
	(((_sc)->sc_set_chan)((_sc), (_c)))
#ifndef RTWN_WITHOUT_UCODE
#define rtwn_set_rsvd_page(_sc, _resp, _null, _qos_null) \
	(((_sc)->sc_set_rsvd_page)((_sc), \
	    (_resp), (_null), (_qos_null)))
#define rtwn_set_pwrmode(_sc, _vap, _off) \
	(((_sc)->sc_set_pwrmode)((_sc), (_vap), (_off)))
#define rtwn_set_rssi(_sc) \
	(((_sc)->sc_set_rssi)((_sc)))
#endif
#define rtwn_classify_intr(_sc, _buf, _len) \
	(((_sc)->sc_classify_intr)((_sc), (_buf), (_len)))
#define rtwn_handle_tx_report(_sc, _buf, _len) \
	(((_sc)->sc_handle_tx_report)((_sc), (_buf), (_len)))
#define rtwn_handle_c2h_report(_sc, _buf, _len) \
	(((_sc)->sc_handle_c2h_report)((_sc), (_buf), (_len)))
#define rtwn_check_frame(_sc, _m) \
	(((_sc)->sc_check_frame)((_sc), (_m)))
#define rtwn_beacon_init(_sc, _buf, _id) \
	(((_sc)->sc_beacon_init)((_sc), (_buf), (_id)))
#define rtwn_beacon_enable(_sc, _id, _enable) \
	(((_sc)->sc_beacon_enable)((_sc), (_id), (_enable)))
#define rtwn_beacon_set_rate(_sc, _buf, _is5ghz) \
	(((_sc)->sc_beacon_set_rate)((_buf), (_is5ghz)))
#define rtwn_beacon_select(_sc, _id) \
	(((_sc)->sc_beacon_select)((_sc), (_id)))
#define rtwn_temp_measure(_sc) \
	(((_sc)->sc_temp_measure)((_sc)))
#define rtwn_temp_read(_sc) \
	(((_sc)->sc_temp_read)((_sc)))
#define rtwn_init_tx_agg(_sc) \
	(((_sc)->sc_init_tx_agg)((_sc)))
#define rtwn_init_rx_agg(_sc) \
	(((_sc)->sc_init_rx_agg)((_sc)))
#define rtwn_init_intr(_sc) \
	(((_sc)->sc_init_intr)((_sc)))
#define rtwn_init_ampdu(_sc) \
	(((_sc)->sc_init_ampdu)((_sc)))
#define rtwn_init_edca(_sc) \
	(((_sc)->sc_init_edca)((_sc)))
#define rtwn_init_bb(_sc) \
	(((_sc)->sc_init_bb)((_sc)))
#define rtwn_init_rf(_sc) \
	(((_sc)->sc_init_rf)((_sc)))
#define rtwn_init_antsel(_sc) \
	(((_sc)->sc_init_antsel)((_sc)))
#define rtwn_post_init(_sc) \
	(((_sc)->sc_post_init)((_sc)))
#define rtwn_init_bcnq1_boundary(_sc) \
	(((_sc)->sc_init_bcnq1_boundary)((_sc)))


/*
 * Methods to access subfields in registers.
 */
static __inline int
rtwn_setbits_1(struct rtwn_softc *sc, uint16_t addr, uint8_t clr,
    uint8_t set)
{
	return (rtwn_write_1(sc, addr,
	    (rtwn_read_1(sc, addr) & ~clr) | set));
}

static __inline int
rtwn_setbits_1_shift(struct rtwn_softc *sc, uint16_t addr, uint32_t clr,
    uint32_t set, int shift)
{
	return (rtwn_setbits_1(sc, addr + shift, clr >> shift * NBBY,
	    set >> shift * NBBY));
}

static __inline int
rtwn_setbits_2(struct rtwn_softc *sc, uint16_t addr, uint16_t clr,
    uint16_t set)
{
	return (rtwn_write_2(sc, addr,
	    (rtwn_read_2(sc, addr) & ~clr) | set));
}

static __inline int
rtwn_setbits_4(struct rtwn_softc *sc, uint16_t addr, uint32_t clr,
    uint32_t set)
{
	return (rtwn_write_4(sc, addr,
	    (rtwn_read_4(sc, addr) & ~clr) | set));
}

static __inline void
rtwn_rf_setbits(struct rtwn_softc *sc, int chain, uint8_t addr,
    uint32_t clr, uint32_t set)
{
	rtwn_rf_write(sc, chain, addr,
	    (rtwn_rf_read(sc, chain, addr) & ~clr) | set);
}

#endif	/* IF_RTWNVAR_H */
