/*	$OpenBSD: rtwnvar.h,v 1.16 2023/04/28 01:24:14 kevlo Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
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

/* Operations provided by bus-specific attachment drivers. */
struct rtwn_ops {
	void		*cookie; /* Attachment driver's private data. */

	uint8_t		(*read_1)(void *, uint16_t);
	uint16_t	(*read_2)(void *, uint16_t);
	uint32_t	(*read_4)(void *, uint16_t);
	void		(*write_1)(void *, uint16_t, uint8_t);
	void		(*write_2)(void *, uint16_t, uint16_t);
	void		(*write_4)(void *, uint16_t, uint32_t);
	int		(*tx)(void *, struct mbuf *, struct ieee80211_node *);
	int		(*power_on)(void *);
	int		(*dma_init)(void *);
	int		(*fw_loadpage)(void *, int, uint8_t *, int);
	int		(*load_firmware)(void *, u_char **fw, size_t *);
	void		(*aggr_init)(void *);
	void		(*mac_init)(void *);
	void		(*bb_init)(void *);
	int		(*alloc_buffers)(void *);
	int		(*init)(void *);
	void		(*stop)(void *);
	int		(*is_oactive)(void *);
	void		(*next_calib)(void *);
	void		(*cancel_calib)(void *);
	void		(*next_scan)(void *);
	void		(*cancel_scan)(void *);
	void		(*wait_async)(void *);
};

#define RTWN_LED_LINK	0
#define RTWN_LED_DATA	1

#define RTWN_92C_INT_ENABLE (R92C_IMR_ROK | R92C_IMR_VODOK | R92C_IMR_VIDOK | \
			R92C_IMR_BEDOK | R92C_IMR_BKDOK | R92C_IMR_MGNTDOK | \
			R92C_IMR_HIGHDOK | R92C_IMR_BDOK | R92C_IMR_RDU | \
			R92C_IMR_RXFOVW)
#define RTWN_88E_INT_ENABLE (R88E_HIMR_PSTIMEOUT | R88E_HIMR_HSISR_IND_ON_INT | \
			R88E_HIMR_C2HCMD | R88E_HIMR_ROK | R88E_HIMR_VODOK | \
			R88E_HIMR_VIDOK | R88E_HIMR_BEDOK | R88E_HIMR_BKDOK | \
			R88E_HIMR_MGNTDOK | R88E_HIMR_HIGHDOK | R88E_HIMR_RDU)

struct rtwn_softc {
	/* sc_ops must be initialized by the attachment driver! */
	struct rtwn_ops			sc_ops;

	struct device			*sc_pdev;
	struct ieee80211com		sc_ic;
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);
	struct task			init_task;
	int				ac2idx[EDCA_NUM_AC];
	uint32_t			sc_flags;
#define RTWN_FLAG_CCK_HIPWR		0x01
#define RTWN_FLAG_BUSY			0x02
#define RTWN_FLAG_FORCE_RAID_11B	0x04
#define RTWN_FLAG_EXT_HDR		0x08

	uint32_t		chip;
#define RTWN_CHIP_92C		0x00000001
#define RTWN_CHIP_92C_1T2R	0x00000002
#define RTWN_CHIP_UMC		0x00000004
#define RTWN_CHIP_UMC_A_CUT	0x00000008
#define RTWN_CHIP_88C		0x00000010
#define RTWN_CHIP_88E		0x00000020
#define RTWN_CHIP_92E		0x00000040
#define RTWN_CHIP_23A		0x00000080
#define RTWN_CHIP_23B		0x00000100
#define RTWN_CHIP_88F		0x00000200

#define RTWN_CHIP_PCI		0x40000000
#define RTWN_CHIP_USB		0x80000000

	uint8_t				board_type;
	uint8_t				crystal_cap;
	uint8_t				regulatory;
	uint8_t				pa_setting;
	int				avg_pwdb;
	int				thcal_state;
	int				thcal_lctemp;
	int				ntxchains;
	int				nrxchains;
	int				ledlink;

	int				sc_tx_timer;
	int				fwcur;
	union {
		struct r92c_rom		r92c_rom;
		struct r92e_rom		r92e_rom;
		struct r88e_rom		r88e_rom;
		struct r88f_rom		r88f_rom;
		struct r23a_rom		r23a_rom;
	} u;
#define sc_r92c_rom	u.r92c_rom
#define sc_r92e_rom	u.r92e_rom
#define sc_r88e_rom	u.r88e_rom
#define sc_r88f_rom	u.r88f_rom
#define sc_r23a_rom	u.r23a_rom

	uint32_t			rf_chnlbw[R92C_MAX_CHAINS];
};

int		rtwn_attach(struct device *, struct rtwn_softc *);
int		rtwn_detach(struct rtwn_softc *, int);
int		rtwn_activate(struct rtwn_softc *, int);
int8_t		rtwn_get_rssi(struct rtwn_softc *, int, void *);
void		rtwn_update_avgrssi(struct rtwn_softc *, int, int8_t);
void		rtwn_calib(struct rtwn_softc *);
void		rtwn_next_scan(struct rtwn_softc *);
int		rtwn_newstate(struct ieee80211com *, enum ieee80211_state, int);
void		rtwn_updateslot(struct ieee80211com *);
void		rtwn_updateedca(struct ieee80211com *);
int		rtwn_set_key(struct ieee80211com *, struct ieee80211_node *,
		    struct ieee80211_key *);
void		rtwn_delete_key(struct ieee80211com *,
		    struct ieee80211_node *, struct ieee80211_key *);
int		rtwn_ioctl(struct ifnet *, u_long, caddr_t);
void		rtwn_start(struct ifnet *);
void		rtwn_fw_reset(struct rtwn_softc *);
