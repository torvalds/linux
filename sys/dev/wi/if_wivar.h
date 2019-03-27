/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2002 M Warner Losh <imp@freebsd.org>.
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

/*
 * Encryption controls. We can enable or disable encryption as
 * well as specify up to 4 encryption keys. We can also specify
 * which of the four keys will be used for transmit encryption.
 */
#define WI_RID_ENCRYPTION	0xFC20
#define WI_RID_AUTHTYPE		0xFC21
#define WI_RID_DEFLT_CRYPT_KEYS	0xFCB0
#define WI_RID_TX_CRYPT_KEY	0xFCB1
#define WI_RID_WEP_AVAIL	0xFD4F
#define WI_RID_P2_TX_CRYPT_KEY	0xFC23
#define WI_RID_P2_CRYPT_KEY0	0xFC24
#define WI_RID_P2_CRYPT_KEY1	0xFC25
#define WI_RID_MICROWAVE_OVEN	0xFC25
#define WI_RID_P2_CRYPT_KEY2	0xFC26
#define WI_RID_P2_CRYPT_KEY3	0xFC27
#define WI_RID_P2_ENCRYPTION	0xFC28
#define WI_RID_ROAMING_MODE	0xFC2D
#define WI_RID_CUR_TX_RATE	0xFD44 /* current TX rate */

#define	WI_MAX_AID		256	/* max stations for ap operation */

struct wi_vap {
	struct ieee80211vap	wv_vap;

	void		(*wv_recv_mgmt)(struct ieee80211_node *, struct mbuf *,
			    int, const struct ieee80211_rx_stats *rxs, int, int);
	int		(*wv_newstate)(struct ieee80211vap *,
			    enum ieee80211_state, int);
};
#define	WI_VAP(vap)		((struct wi_vap *)(vap))

struct wi_softc	{
	struct ieee80211com	sc_ic;
	struct mbufq		sc_snd;
	device_t		sc_dev;
	struct mtx		sc_mtx;
	struct callout		sc_watchdog;
	int			sc_unit;
	int			wi_gone;
	int			sc_enabled;
	int			sc_reset;
	int			sc_firmware_type;
#define WI_NOTYPE	0
#define	WI_LUCENT	1
#define	WI_INTERSIL	2
#define	WI_SYMBOL	3
	int			sc_pri_firmware_ver;	/* Primary firmware */
	int			sc_sta_firmware_ver;	/* Station firmware */
	unsigned int		sc_nic_id;		/* Type of NIC */
	char *			sc_nic_name;

	int			wi_bus_type;	/* Bus attachment type */
	struct resource *	local;
	int			local_rid;
	struct resource *	iobase;
	int			iobase_rid;
	struct resource *	irq;
	int			irq_rid;
	struct resource *	mem;
	int			mem_rid;
	bus_space_handle_t	wi_localhandle;
	bus_space_tag_t		wi_localtag;
	bus_space_handle_t	wi_bhandle;
	bus_space_tag_t		wi_btag;
	bus_space_handle_t	wi_bmemhandle;
	bus_space_tag_t		wi_bmemtag;
	void *			wi_intrhand;
	struct ieee80211_channel *wi_channel;
	int			wi_io_addr;
	int			wi_cmd_count;

	int			sc_flags;
	int			sc_bap_id;
	int			sc_bap_off;

	int			sc_porttype;
	u_int16_t		sc_portnum;
	u_int16_t		sc_encryption;
	u_int16_t		sc_monitor_port;
	u_int16_t		sc_chanmask;

	/* RSSI interpretation */
	u_int16_t		sc_min_rssi;	/* clamp sc_min_rssi < RSSI */
	u_int16_t		sc_max_rssi;	/* clamp RSSI < sc_max_rssi */
	u_int16_t		sc_dbm_offset;	/* dBm ~ RSSI - sc_dbm_offset */

	int			sc_buflen;		/* TX buffer size */
	int			sc_ntxbuf;
#define	WI_NTXBUF	3
	struct {
		int		d_fid;
		int		d_len;
	}			sc_txd[WI_NTXBUF];	/* TX buffers */
	int			sc_txnext;		/* index of next TX */
	int			sc_txcur;		/* index of current TX*/
	int			sc_tx_timer;

	struct wi_counters	sc_stats;
	u_int16_t		sc_ibss_port;

	struct timeval		sc_last_syn;
	int			sc_false_syns;

	u_int16_t		sc_txbuf[IEEE80211_MAX_LEN/2];

	struct wi_tx_radiotap_header sc_tx_th;
	struct wi_rx_radiotap_header sc_rx_th;
};

/* maximum consecutive false change-of-BSSID indications */
#define	WI_MAX_FALSE_SYNS		10	

#define	WI_FLAGS_HAS_ENHSECURITY	0x0001
#define	WI_FLAGS_HAS_WPASUPPORT		0x0002
#define	WI_FLAGS_HAS_ROAMING		0x0020
#define	WI_FLAGS_HAS_FRAGTHR		0x0200
#define	WI_FLAGS_HAS_DBMADJUST		0x0400
#define	WI_FLAGS_RUNNING		0x0800
#define	WI_FLAGS_PROMISC		0x1000

struct wi_card_ident {
	u_int16_t	card_id;
	char		*card_name;
	u_int8_t	firm_type;
};

#define	WI_PRISM_MIN_RSSI	0x1b
#define	WI_PRISM_MAX_RSSI	0x9a
#define	WI_PRISM_DBM_OFFSET	100 /* XXX */

#define	WI_LUCENT_MIN_RSSI	47
#define	WI_LUCENT_MAX_RSSI	138
#define	WI_LUCENT_DBM_OFFSET	149

#define	WI_RSSI_TO_DBM(sc, rssi) (MIN((sc)->sc_max_rssi, \
    MAX((sc)->sc_min_rssi, (rssi))) - (sc)->sc_dbm_offset)

#define	WI_LOCK(_sc) 		mtx_lock(&(_sc)->sc_mtx)
#define	WI_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	WI_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

int	wi_attach(device_t);
int	wi_detach(device_t);
int	wi_shutdown(device_t);
int	wi_alloc(device_t, int);
void	wi_free(device_t);
extern devclass_t wi_devclass;
void	wi_intr(void *);
int	wi_mgmt_xmit(struct wi_softc *, caddr_t, int);
void	wi_stop(struct wi_softc *, int);
void	wi_init(struct wi_softc *);
