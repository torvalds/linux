/*	$OpenBSD: ar5xxx.h,v 1.60 2017/08/25 12:17:27 tb Exp $	*/

/*
 * Copyright (c) 2004, 2005, 2006, 2007 Reyk Floeter <reyk@openbsd.org>
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

/*
 * HAL interface for Atheros Wireless LAN devices.
 *
 * ar5k is a free replacement of the binary-only HAL used by some drivers
 * for Atheros chipsets. While using a different ABI, it tries to be
 * source-compatible with the original (non-free) HAL interface.
 *
 * Many thanks to various contributors who supported the development of
 * ar5k with hard work and useful information. And, of course, for all the
 * people who encouraged me to continue this work which has been based
 * on my initial approach found on http://team.vantronix.net/ar5k/.
 */

#ifndef _AR5K_H
#define _AR5K_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>

/*
 * Possible chipsets (could appear in different combinations)
 */

enum ar5k_version {
	AR5K_AR5210	= 0,
	AR5K_AR5211	= 1,
	AR5K_AR5212	= 2,
};

enum ar5k_radio {
	AR5K_AR5110	= 0,
	AR5K_AR5111	= 1,
	AR5K_AR5112	= 2,
	AR5K_AR2413	= 3,
	AR5K_AR5413	= 4,
	AR5K_AR2425	= 5
};

/*
 * Generic definitions
 */

typedef enum {
	AH_FALSE = 0,
	AH_TRUE,
} HAL_BOOL;

typedef enum {
	HAL_MODE_11A = 0x001,
	HAL_MODE_TURBO = 0x002,
	HAL_MODE_11B = 0x004,
	HAL_MODE_PUREG = 0x008,
	HAL_MODE_11G = 0x010,
	HAL_MODE_108G = 0x020,
	HAL_MODE_XR = 0x040,
	HAL_MODE_ALL = 0xfff
} HAL_MODE;

typedef enum {
	HAL_ANT_VARIABLE = 0,
	HAL_ANT_FIXED_A = 1,
	HAL_ANT_FIXED_B	= 2,
	HAL_ANT_MAX = 3,
} HAL_ANT_SETTING;

typedef enum ieee80211_opmode HAL_OPMODE;

#define	HAL_M_STA	IEEE80211_M_STA
#define HAL_M_IBSS	IEEE80211_M_IBSS
#define HAL_M_HOSTAP	IEEE80211_M_HOSTAP
#define HAL_M_MONITOR	IEEE80211_M_MONITOR

typedef int HAL_STATUS;

#define HAL_OK		0
#define HAL_EINPROGRESS EINPROGRESS

#define AR5K_MAX_RSSI	64

/*
 * TX queues
 */

typedef enum {
	HAL_TX_QUEUE_INACTIVE = 0,
	HAL_TX_QUEUE_DATA,
	HAL_TX_QUEUE_BEACON,
	HAL_TX_QUEUE_CAB,
	HAL_TX_QUEUE_PSPOLL,
} HAL_TX_QUEUE;

#define HAL_NUM_TX_QUEUES	10

typedef enum {
	HAL_TX_QUEUE_ID_DATA_MIN = 0,
	HAL_TX_QUEUE_ID_DATA_MAX = 6,
	HAL_TX_QUEUE_ID_PSPOLL = 7,
	HAL_TX_QUEUE_ID_BEACON = 8,
	HAL_TX_QUEUE_ID_CAB = 9,
} HAL_TX_QUEUE_ID;

typedef enum {
	HAL_WME_AC_BK = 0,
	HAL_WME_AC_BE = 1,
	HAL_WME_AC_VI = 2,
	HAL_WME_AC_VO = 3,
	HAL_WME_UPSD = 4,
} HAL_TX_QUEUE_SUBTYPE;

#define AR5K_TXQ_FLAG_TXINT_ENABLE		0x0001
#define AR5K_TXQ_FLAG_TXDESCINT_ENABLE		0x0002
#define AR5K_TXQ_FLAG_BACKOFF_DISABLE		0x0004
#define AR5K_TXQ_FLAG_COMPRESSION_ENABLE	0x0008
#define AR5K_TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE	0x0010
#define AR5K_TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE	0x0020
#define AR5K_TXQ_FLAG_POST_FR_BKOFF_DIS         0x0040

typedef struct {
	u_int32_t		tqi_ver;
	HAL_TX_QUEUE		tqi_type;
	HAL_TX_QUEUE_SUBTYPE	tqi_subtype;
	u_int16_t		tqi_flags;
	u_int32_t		tqi_priority;
	u_int32_t		tqi_aifs;
	int32_t			tqi_cw_min;
	int32_t			tqi_cw_max;
	u_int32_t		tqi_cbr_period;
	u_int32_t		tqi_cbr_overflow_limit;
	u_int32_t		tqi_burst_time;
	u_int32_t		tqi_ready_time;
} HAL_TXQ_INFO;

typedef enum {
	HAL_PKT_TYPE_NORMAL = 0,
	HAL_PKT_TYPE_ATIM = 1,
	HAL_PKT_TYPE_PSPOLL = 2,
	HAL_PKT_TYPE_BEACON = 3,
	HAL_PKT_TYPE_PROBE_RESP = 4,
	HAL_PKT_TYPE_PIFS = 5,
} HAL_PKT_TYPE;

/*
 * Used to compute TX times
 */

#define AR5K_CCK_SIFS_TIME		10
#define AR5K_CCK_PREAMBLE_BITS		144
#define AR5K_CCK_PLCP_BITS		48
#define AR5K_CCK_NUM_BITS(_frmlen) (_frmlen << 3)
#define AR5K_CCK_PHY_TIME(_sp) (_sp ?					\
	((AR5K_CCK_PREAMBLE_BITS + AR5K_CCK_PLCP_BITS) >> 1) :		\
	(AR5K_CCK_PREAMBLE_BITS + AR5K_CCK_PLCP_BITS))
#define AR5K_CCK_TX_TIME(_kbps, _frmlen, _sp)				\
	AR5K_CCK_PHY_TIME(_sp) +					\
	((AR5K_CCK_NUM_BITS(_frmlen) * 1000) / _kbps) +			\
	AR5K_CCK_SIFS_TIME

#define AR5K_OFDM_SIFS_TIME		16
#define AR5K_OFDM_PREAMBLE_TIME	20
#define AR5K_OFDM_PLCP_BITS		22
#define AR5K_OFDM_SYMBOL_TIME		4
#define AR5K_OFDM_NUM_BITS(_frmlen) (AR5K_OFDM_PLCP_BITS + (_frmlen << 3))
#define AR5K_OFDM_NUM_BITS_PER_SYM(_kbps) ((_kbps *			\
	AR5K_OFDM_SYMBOL_TIME) / 1000)
#define AR5K_OFDM_NUM_BITS(_frmlen) (AR5K_OFDM_PLCP_BITS + (_frmlen << 3))
#define AR5K_OFDM_NUM_SYMBOLS(_kbps, _frmlen)				\
	howmany(AR5K_OFDM_NUM_BITS(_frmlen), AR5K_OFDM_NUM_BITS_PER_SYM(_kbps))
#define AR5K_OFDM_TX_TIME(_kbps, _frmlen)				\
	AR5K_OFDM_PREAMBLE_TIME + AR5K_OFDM_SIFS_TIME +			\
	(AR5K_OFDM_NUM_SYMBOLS(_kbps, _frmlen) * AR5K_OFDM_SYMBOL_TIME)

#define AR5K_TURBO_SIFS_TIME		8
#define AR5K_TURBO_PREAMBLE_TIME	14
#define AR5K_TURBO_PLCP_BITS		22
#define AR5K_TURBO_SYMBOL_TIME		4
#define AR5K_TURBO_NUM_BITS(_frmlen) (AR5K_TURBO_PLCP_BITS + (_frmlen << 3))
#define AR5K_TURBO_NUM_BITS_PER_SYM(_kbps) (((_kbps << 1) *		\
	AR5K_TURBO_SYMBOL_TIME) / 1000)
#define AR5K_TURBO_NUM_BITS(_frmlen) (AR5K_TURBO_PLCP_BITS + (_frmlen << 3))
#define AR5K_TURBO_NUM_SYMBOLS(_kbps, _frmlen)				\
	howmany(AR5K_TURBO_NUM_BITS(_frmlen),				\
	AR5K_TURBO_NUM_BITS_PER_SYM(_kbps))
#define AR5K_TURBO_TX_TIME(_kbps, _frmlen)				\
	AR5K_TURBO_PREAMBLE_TIME + AR5K_TURBO_SIFS_TIME +		\
	(AR5K_TURBO_NUM_SYMBOLS(_kbps, _frmlen) * AR5K_TURBO_SYMBOL_TIME)

#define AR5K_XR_SIFS_TIME		16
#define AR5K_XR_PLCP_BITS		22
#define AR5K_XR_SYMBOL_TIME		4
#define AR5K_XR_PREAMBLE_TIME(_kbps) (((_kbps) < 1000) ? 173 : 76)
#define AR5K_XR_NUM_BITS_PER_SYM(_kbps) ((_kbps *			\
	AR5K_XR_SYMBOL_TIME) / 1000)
#define AR5K_XR_NUM_BITS(_frmlen) (AR5K_XR_PLCP_BITS + (_frmlen << 3))
#define AR5K_XR_NUM_SYMBOLS(_kbps, _frmlen)				\
	howmany(AR5K_XR_NUM_BITS(_frmlen), AR5K_XR_NUM_BITS_PER_SYM(_kbps))
#define AR5K_XR_TX_TIME(_kbps, _frmlen)					\
	AR5K_XR_PREAMBLE_TIME(_kbps) + AR5K_XR_SIFS_TIME +		\
	(AR5K_XR_NUM_SYMBOLS(_kbps, _frmlen) * AR5K_XR_SYMBOL_TIME)

/*
 * RX definitions
 */

#define HAL_RX_FILTER_UCAST	0x00000001
#define HAL_RX_FILTER_MCAST	0x00000002
#define HAL_RX_FILTER_BCAST	0x00000004
#define HAL_RX_FILTER_CONTROL	0x00000008
#define HAL_RX_FILTER_BEACON	0x00000010
#define HAL_RX_FILTER_PROM	0x00000020
#define HAL_RX_FILTER_PROBEREQ	0x00000080
#define HAL_RX_FILTER_PHYERR	0x00000100
#define HAL_RX_FILTER_PHYRADAR	0x00000200

typedef struct {
	u_int32_t	ackrcv_bad;
	u_int32_t	rts_bad;
	u_int32_t	rts_good;
	u_int32_t	fcs_bad;
	u_int32_t	beacons;
} HAL_MIB_STATS;

/*
 * Beacon/AP definitions
 */

#define HAL_BEACON_PERIOD	0x0000ffff
#define HAL_BEACON_ENA		0x00800000
#define HAL_BEACON_RESET_TSF	0x01000000

typedef struct {
	u_int32_t	bs_next_beacon;
	u_int32_t	bs_next_dtim;
	u_int32_t	bs_interval;
	u_int8_t	bs_dtim_period;
	u_int8_t	bs_cfp_period;
	u_int16_t	bs_cfp_max_duration;
	u_int16_t	bs_cfp_du_remain;
	u_int16_t	bs_tim_offset;
	u_int16_t	bs_sleep_duration;
	u_int16_t	bs_bmiss_threshold;

#define bs_nexttbtt		bs_next_beacon
#define bs_intval		bs_interval
#define bs_nextdtim		bs_next_dtim
#define bs_bmissthreshold	bs_bmiss_threshold
#define bs_sleepduration	bs_sleep_duration
#define bs_dtimperiod		bs_dtim_period

} HAL_BEACON_STATE;

/*
 * Power management
 */

typedef enum {
	HAL_PM_UNDEFINED = 0,
	HAL_PM_AUTO,
	HAL_PM_AWAKE,
	HAL_PM_FULL_SLEEP,
	HAL_PM_NETWORK_SLEEP,
} HAL_POWER_MODE;

/*
 * Weak wireless crypto definitions (use IPsec/WLSec/...)
 */

typedef enum {
	HAL_CIPHER_WEP = 0,
	HAL_CIPHER_AES_CCM,
	HAL_CIPHER_CKIP,
} HAL_CIPHER;

#define AR5K_KEYVAL_LENGTH_40	5
#define AR5K_KEYVAL_LENGTH_104	13
#define AR5K_KEYVAL_LENGTH_128	16
#define AR5K_KEYVAL_LENGTH_MAX	AR5K_KEYVAL_LENGTH_128

typedef struct {
	int		wk_len;
	u_int8_t	wk_key[AR5K_KEYVAL_LENGTH_MAX];
} HAL_KEYVAL;

#define AR5K_ASSERT_ENTRY(_e, _s) do {					\
	if (_e >= _s)							\
		return (AH_FALSE);					\
} while (0)

/*
 * PHY
 */

#define AR5K_MAX_RATES	32

typedef struct {
	u_int8_t	valid;
	u_int8_t	phy;
	u_int16_t	rateKbps;
	u_int8_t	rateCode;
	u_int8_t	shortPreamble;
	u_int8_t	dot11Rate;
	u_int8_t	controlRate;

#define r_valid			valid
#define r_phy			phy
#define r_rate_kbps		rateKbps
#define r_rate_code		rateCode
#define r_short_preamble	shortPreamble
#define r_dot11_rate		dot11Rate
#define r_control_rate		controlRate

} HAL_RATE;

typedef struct {
	u_int16_t	rateCount;
	u_int8_t	rateCodeToIndex[AR5K_MAX_RATES];
	HAL_RATE	info[AR5K_MAX_RATES];

#define rt_rate_count		rateCount
#define rt_rate_code_index	rateCodeToIndex
#define rt_info			info

} HAL_RATE_TABLE;

#define AR5K_RATES_11A { 8, {						\
	255, 255, 255, 255, 255, 255, 255, 255, 6, 4, 2, 0,		\
	7, 5, 3, 1, 255, 255, 255, 255, 255, 255, 255, 255,		\
	255, 255, 255, 255, 255, 255, 255, 255 }, {			\
	{ 1, IEEE80211_T_OFDM, 6000, 11, 0, 140, 0 },			\
	{ 1, IEEE80211_T_OFDM, 9000, 15, 0, 18, 0 },			\
	{ 1, IEEE80211_T_OFDM, 12000, 10, 0, 152, 2 },			\
	{ 1, IEEE80211_T_OFDM, 18000, 14, 0, 36, 2 },			\
	{ 1, IEEE80211_T_OFDM, 24000, 9, 0, 176, 4 },			\
	{ 1, IEEE80211_T_OFDM, 36000, 13, 0, 72, 4 },			\
	{ 1, IEEE80211_T_OFDM, 48000, 8, 0, 96, 4 },			\
	{ 1, IEEE80211_T_OFDM, 54000, 12, 0, 108, 4 } }			\
}

#define AR5K_RATES_11B { 4, {						\
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,	\
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,	\
	3, 2, 1, 0, 255, 255, 255, 255 }, {				\
	{ 1, IEEE80211_T_CCK, 1000, 27, 0x00, 130, 0 },			\
	{ 1, IEEE80211_T_CCK, 2000, 26, 0x04, 132, 1 },			\
	{ 1, IEEE80211_T_CCK, 5500, 25, 0x04, 139, 1 },			\
	{ 1, IEEE80211_T_CCK, 11000, 24, 0x04, 150, 1 } }		\
}

#define AR5K_RATES_11G { 12, {						\
	255, 255, 255, 255, 255, 255, 255, 255, 10, 8, 6, 4,		\
	11, 9, 7, 5, 255, 255, 255, 255, 255, 255, 255, 255,		\
	3, 2, 1, 0, 255, 255, 255, 255 }, {				\
	{ 1, IEEE80211_T_CCK, 1000, 27, 0x00, 130, 0 },			\
	{ 1, IEEE80211_T_CCK, 2000, 26, 0x04, 132, 1 },			\
	{ 1, IEEE80211_T_CCK, 5500, 25, 0x04, 139, 2 },			\
	{ 1, IEEE80211_T_CCK, 11000, 24, 0x04, 150, 3 },		\
	{ 0, IEEE80211_T_OFDM, 6000, 11, 0, 12, 4 },			\
	{ 0, IEEE80211_T_OFDM, 9000, 15, 0, 18, 4 },			\
	{ 1, IEEE80211_T_OFDM, 12000, 10, 0, 24, 6 },			\
	{ 1, IEEE80211_T_OFDM, 18000, 14, 0, 36, 6 },			\
	{ 1, IEEE80211_T_OFDM, 24000, 9, 0, 48, 8 },			\
	{ 1, IEEE80211_T_OFDM, 36000, 13, 0, 72, 8 },			\
	{ 1, IEEE80211_T_OFDM, 48000, 8, 0, 96, 8 },			\
	{ 1, IEEE80211_T_OFDM, 54000, 12, 0, 108, 8 } }			\
}

#define AR5K_RATES_XR { 12, {						\
	255, 3, 1, 255, 255, 255, 2, 0, 10, 8, 6, 4,			\
	11, 9, 7, 5, 255, 255, 255, 255, 255, 255, 255, 255,		\
	255, 255, 255, 255, 255, 255, 255, 255 }, {			\
	{ 1, IEEE80211_T_XR, 500, 7, 0, 129, 0 },			\
	{ 1, IEEE80211_T_XR, 1000, 2, 0, 139, 1 },			\
	{ 1, IEEE80211_T_XR, 2000, 6, 0, 150, 2 },			\
	{ 1, IEEE80211_T_XR, 3000, 1, 0, 150, 3 },			\
	{ 1, IEEE80211_T_OFDM, 6000, 11, 0, 140, 4 },			\
	{ 1, IEEE80211_T_OFDM, 9000, 15, 0, 18, 4 },			\
	{ 1, IEEE80211_T_OFDM, 12000, 10, 0, 152, 6 },			\
	{ 1, IEEE80211_T_OFDM, 18000, 14, 0, 36, 6 },			\
	{ 1, IEEE80211_T_OFDM, 24000, 9, 0, 176, 8 },			\
	{ 1, IEEE80211_T_OFDM, 36000, 13, 0, 72, 8 },			\
	{ 1, IEEE80211_T_OFDM, 48000, 8, 0, 96, 8 },			\
	{ 1, IEEE80211_T_OFDM, 54000, 12, 0, 108, 8 } }			\
}

typedef enum {
	HAL_RFGAIN_INACTIVE = 0,
	HAL_RFGAIN_READ_REQUESTED,
	HAL_RFGAIN_NEED_CHANGE,
} HAL_RFGAIN;

typedef struct {
	u_int16_t	channel; /* MHz */
	u_int16_t	channelFlags;

#define c_channel	channel
#define c_channel_flags	channelFlags

} HAL_CHANNEL;

#define HAL_SLOT_TIME_9		396
#define HAL_SLOT_TIME_20	880
#define HAL_SLOT_TIME_MAX	0xffff

#define CHANNEL_A	(IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_OFDM)
#define CHANNEL_B	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_CCK)
#define CHANNEL_G	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_DYN)
#define CHANNEL_PUREG	(IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_OFDM)
#define CHANNEL_XR	(CHANNEL_A | IEEE80211_CHAN_XR)
#define CHANNEL_MODES	\
	(CHANNEL_A | CHANNEL_B | CHANNEL_G | CHANNEL_PUREG | CHANNEL_XR)

typedef enum {
	HAL_CHIP_5GHZ = IEEE80211_CHAN_5GHZ,
	HAL_CHIP_2GHZ = IEEE80211_CHAN_2GHZ
} HAL_CHIP;

/*
 * The following structure will be used to map 2GHz channels to
 * 5GHz Atheros channels.
 */

struct ar5k_athchan_2ghz {
	u_int32_t	a2_flags;
	u_int16_t	a2_athchan;
};

/*
 * Regulation stuff
 */

typedef enum ieee80211_countrycode HAL_CTRY_CODE;

/*
 * HAL interrupt abstraction
 */

#define HAL_INT_RX	0x00000001
#define HAL_INT_RXDESC	0x00000002
#define HAL_INT_RXNOFRM	0x00000008
#define HAL_INT_RXEOL	0x00000010
#define HAL_INT_RXORN	0x00000020
#define HAL_INT_TX	0x00000040
#define HAL_INT_TXDESC	0x00000080
#define HAL_INT_TXURN	0x00000800
#define HAL_INT_MIB	0x00001000
#define HAL_INT_RXPHY	0x00004000
#define HAL_INT_RXKCM	0x00008000
#define HAL_INT_SWBA	0x00010000
#define HAL_INT_BMISS	0x00040000
#define HAL_INT_BNR	0x00100000
#define HAL_INT_GPIO	0x01000000
#define HAL_INT_FATAL	0x40000000
#define HAL_INT_GLOBAL	0x80000000
#define HAL_INT_NOCARD	0xffffffff
#define HAL_INT_COMMON	(						\
	HAL_INT_RXNOFRM | HAL_INT_RXDESC | HAL_INT_RXEOL |		\
	HAL_INT_RXORN | HAL_INT_TXURN | HAL_INT_TXDESC |		\
	HAL_INT_MIB | HAL_INT_RXPHY | HAL_INT_RXKCM |			\
	HAL_INT_SWBA | HAL_INT_BMISS | HAL_INT_GPIO			\
)

typedef u_int32_t HAL_INT;

/*
 * LED states
 */

typedef enum ieee80211_state HAL_LED_STATE;

#define HAL_LED_INIT	IEEE80211_S_INIT
#define HAL_LED_SCAN	IEEE80211_S_SCAN
#define HAL_LED_AUTH	IEEE80211_S_AUTH
#define HAL_LED_ASSOC	IEEE80211_S_ASSOC
#define HAL_LED_RUN	IEEE80211_S_RUN

/* GPIO-controlled software LED */
#define AR5K_SOFTLED_PIN	0
#define AR5K_SOFTLED_ON		0
#define AR5K_SOFTLED_OFF	1

/*
 * Gain settings
 */

#define	AR5K_GAIN_CRN_FIX_BITS_5111	4
#define	AR5K_GAIN_CRN_FIX_BITS_5112	7
#define	AR5K_GAIN_CRN_MAX_FIX_BITS	AR5K_GAIN_CRN_FIX_BITS_5112
#define	AR5K_GAIN_DYN_ADJUST_HI_MARGIN	15
#define	AR5K_GAIN_DYN_ADJUST_LO_MARGIN	20
#define	AR5K_GAIN_CCK_PROBE_CORR	5
#define	AR5K_GAIN_CCK_OFDM_GAIN_DELTA	15
#define	AR5K_GAIN_STEP_COUNT		10
#define AR5K_GAIN_PARAM_TX_CLIP		0
#define AR5K_GAIN_PARAM_PD_90		1
#define AR5K_GAIN_PARAM_PD_84		2
#define AR5K_GAIN_PARAM_GAIN_SEL	3
#define AR5K_GAIN_PARAM_MIX_ORN		0
#define AR5K_GAIN_PARAM_PD_138		1
#define AR5K_GAIN_PARAM_PD_137		2
#define AR5K_GAIN_PARAM_PD_136		3
#define AR5K_GAIN_PARAM_PD_132		4
#define AR5K_GAIN_PARAM_PD_131		5
#define AR5K_GAIN_PARAM_PD_130		6
#define AR5K_GAIN_CHECK_ADJUST(_g)					\
	((_g)->g_current <= (_g)->g_low || (_g)->g_current >= (_g)->g_high)

struct ar5k_gain_opt_step {
	int16_t				gos_param[AR5K_GAIN_CRN_MAX_FIX_BITS];
	int32_t				gos_gain;
};

struct ar5k_gain_opt {
	u_int32_t			go_default;
	u_int32_t			go_steps_count;
	const struct ar5k_gain_opt_step	go_step[AR5K_GAIN_STEP_COUNT];
};

struct ar5k_gain {
	u_int32_t			g_step_idx;
	u_int32_t			g_current;
	u_int32_t			g_target;
	u_int32_t			g_low;
	u_int32_t			g_high;
	u_int32_t			g_f_corr;
	u_int32_t			g_active;
	const struct ar5k_gain_opt_step	*g_step;
};

#define AR5K_AR5111_GAIN_OPT	{					\
	4,								\
	9,								\
	{								\
		{ { 4, 1, 1, 1 }, 6 },					\
		{ { 4, 0, 1, 1 }, 4 },					\
		{ { 3, 1, 1, 1 }, 3 },					\
		{ { 4, 0, 0, 1 }, 1 },					\
		{ { 4, 1, 1, 0 }, 0 },					\
		{ { 4, 0, 1, 0 }, -2 },					\
		{ { 3, 1, 1, 0 }, -3 },					\
		{ { 4, 0, 0, 0 }, -4 },					\
		{ { 2, 1, 1, 0 }, -6 }					\
	}								\
}

#define AR5K_AR5112_GAIN_OPT	{					\
	1,								\
	8,								\
	{								\
		{ { 3, 0, 0, 0, 0, 0, 0 }, 6 },				\
		{ { 2, 0, 0, 0, 0, 0, 0 }, 0 },				\
		{ { 1, 0, 0, 0, 0, 0, 0 }, -3 },			\
		{ { 0, 0, 0, 0, 0, 0, 0 }, -6 },			\
		{ { 0, 1, 1, 0, 0, 0, 0 }, -8 },			\
		{ { 0, 1, 1, 0, 1, 1, 0 }, -10 },			\
		{ { 0, 1, 0, 1, 1, 1, 0 }, -13 },			\
		{ { 0, 1, 0, 1, 1, 0, 1 }, -16 },			\
	}								\
}

/*
 * Common ar5xxx EEPROM data registers
 */

#define AR5K_EEPROM_MAGIC		0x003d
#define AR5K_EEPROM_MAGIC_VALUE		0x5aa5
#define AR5K_EEPROM_PROTECT		0x003f
#define AR5K_EEPROM_PROTECT_RD_0_31	0x0001
#define AR5K_EEPROM_PROTECT_WR_0_31	0x0002
#define AR5K_EEPROM_PROTECT_RD_32_63	0x0004
#define AR5K_EEPROM_PROTECT_WR_32_63	0x0008
#define AR5K_EEPROM_PROTECT_RD_64_127	0x0010
#define AR5K_EEPROM_PROTECT_WR_64_127	0x0020
#define AR5K_EEPROM_PROTECT_RD_128_191	0x0040
#define AR5K_EEPROM_PROTECT_WR_128_191	0x0080
#define AR5K_EEPROM_PROTECT_RD_192_207	0x0100
#define AR5K_EEPROM_PROTECT_WR_192_207	0x0200
#define AR5K_EEPROM_PROTECT_RD_208_223	0x0400
#define AR5K_EEPROM_PROTECT_WR_208_223	0x0800
#define AR5K_EEPROM_PROTECT_RD_224_239	0x1000
#define AR5K_EEPROM_PROTECT_WR_224_239	0x2000
#define AR5K_EEPROM_PROTECT_RD_240_255	0x4000
#define AR5K_EEPROM_PROTECT_WR_240_255	0x8000
#define AR5K_EEPROM_REG_DOMAIN		0x00bf
#define AR5K_EEPROM_INFO_BASE		0x00c0
#define AR5K_EEPROM_INFO_MAX						\
	(0x400 - AR5K_EEPROM_INFO_BASE)
#define AR5K_EEPROM_INFO_CKSUM		0xffff
#define AR5K_EEPROM_INFO(_n)		(AR5K_EEPROM_INFO_BASE + (_n))

#define AR5K_EEPROM_VERSION		AR5K_EEPROM_INFO(1)
#define AR5K_EEPROM_VERSION_3_0		0x3000
#define AR5K_EEPROM_VERSION_3_1		0x3001
#define AR5K_EEPROM_VERSION_3_2		0x3002
#define AR5K_EEPROM_VERSION_3_3		0x3003
#define AR5K_EEPROM_VERSION_3_4		0x3004
#define AR5K_EEPROM_VERSION_4_0		0x4000
#define AR5K_EEPROM_VERSION_4_1		0x4001
#define AR5K_EEPROM_VERSION_4_2		0x4002
#define AR5K_EEPROM_VERSION_4_3		0x4003
#define AR5K_EEPROM_VERSION_4_6		0x4006
#define AR5K_EEPROM_VERSION_4_7		0x3007

#define AR5K_EEPROM_MODE_11A	0
#define AR5K_EEPROM_MODE_11B	1
#define AR5K_EEPROM_MODE_11G	2

#define AR5K_EEPROM_HDR			AR5K_EEPROM_INFO(2)
#define AR5K_EEPROM_HDR_11A(_v)		(((_v) >> AR5K_EEPROM_MODE_11A) & 0x1)
#define AR5K_EEPROM_HDR_11B(_v)		(((_v) >> AR5K_EEPROM_MODE_11B) & 0x1)
#define AR5K_EEPROM_HDR_11G(_v)		(((_v) >> AR5K_EEPROM_MODE_11G) & 0x1)
#define AR5K_EEPROM_HDR_T_2GHZ_DIS(_v)	(((_v) >> 3) & 0x1)
#define AR5K_EEPROM_HDR_T_5GHZ_DBM(_v)	(((_v) >> 4) & 0x7f)
#define AR5K_EEPROM_HDR_DEVICE(_v)	(((_v) >> 11) & 0x7)
#define AR5K_EEPROM_HDR_T_5GHZ_DIS(_v)	(((_v) >> 15) & 0x1)
#define AR5K_EEPROM_HDR_RFKILL(_v)	(((_v) >> 14) & 0x1)

#define AR5K_EEPROM_RFKILL_GPIO_SEL	0x0000001c
#define AR5K_EEPROM_RFKILL_GPIO_SEL_S	2
#define AR5K_EEPROM_RFKILL_POLARITY	0x00000002
#define AR5K_EEPROM_RFKILL_POLARITY_S	1

/* Newer EEPROMs are using a different offset */
#define AR5K_EEPROM_OFF(_v, _v3_0, _v3_3)					\
	(((_v) >= AR5K_EEPROM_VERSION_3_3) ? _v3_3 : _v3_0)

#define AR5K_EEPROM_ANT_GAIN(_v)	AR5K_EEPROM_OFF(_v, 0x00c4, 0x00c3)
#define AR5K_EEPROM_ANT_GAIN_5GHZ(_v)	((int8_t)(((_v) >> 8) & 0xff))
#define AR5K_EEPROM_ANT_GAIN_2GHZ(_v)	((int8_t)((_v) & 0xff))

#define AR5K_EEPROM_MODES_11A(_v)	AR5K_EEPROM_OFF(_v, 0x00c5, 0x00d4)
#define AR5K_EEPROM_MODES_11B(_v)	AR5K_EEPROM_OFF(_v, 0x00d0, 0x00f2)
#define AR5K_EEPROM_MODES_11G(_v)	AR5K_EEPROM_OFF(_v, 0x00da, 0x010d)
#define AR5K_EEPROM_CTL(_v)		AR5K_EEPROM_OFF(_v, 0x00e4, 0x0128)

/* Since 3.1 */
#define AR5K_EEPROM_OBDB0_2GHZ	0x00ec
#define AR5K_EEPROM_OBDB1_2GHZ	0x00ed

/* Misc values available since EEPROM 4.0 */
#define AR5K_EEPROM_MISC0		0x00c4
#define AR5K_EEPROM_EARSTART(_v)	((_v) & 0xfff)
#define AR5K_EEPROM_EEMAP(_v)		(((_v) >> 14) & 0x3)
#define AR5K_EEPROM_MISC1		0x00c5
#define AR5K_EEPROM_TARGET_PWRSTART(_v)	((_v) & 0xfff)
#define AR5K_EEPROM_HAS32KHZCRYSTAL(_v)	(((_v) >> 14) & 0x1)

/* Some EEPROM defines */
#define AR5K_EEPROM_EEP_SCALE		100
#define AR5K_EEPROM_EEP_DELTA		10
#define AR5K_EEPROM_N_MODES		3
#define AR5K_EEPROM_N_5GHZ_CHAN		10
#define AR5K_EEPROM_N_2GHZ_CHAN		3
#define AR5K_EEPROM_MAX_CHAN		10
#define AR5K_EEPROM_N_PCDAC		11
#define AR5K_EEPROM_N_TEST_FREQ		8
#define AR5K_EEPROM_N_EDGES		8
#define AR5K_EEPROM_N_INTERCEPTS	11
#define AR5K_EEPROM_FREQ_M(_v)		AR5K_EEPROM_OFF(_v, 0x7f, 0xff)
#define AR5K_EEPROM_PCDAC_M		0x3f
#define AR5K_EEPROM_PCDAC_START		1
#define AR5K_EEPROM_PCDAC_STOP		63
#define AR5K_EEPROM_PCDAC_STEP		1
#define AR5K_EEPROM_NON_EDGE_M		0x40
#define AR5K_EEPROM_CHANNEL_POWER	8
#define AR5K_EEPROM_N_OBDB		4
#define AR5K_EEPROM_OBDB_DIS		0xffff
#define AR5K_EEPROM_CHANNEL_DIS		0xff
#define AR5K_EEPROM_SCALE_OC_DELTA(_x)	(((_x) * 2) / 10)
#define AR5K_EEPROM_N_CTLS(_v)		AR5K_EEPROM_OFF(_v, 16, 32)
#define AR5K_EEPROM_MAX_CTLS		32
#define AR5K_EEPROM_N_XPD_PER_CHANNEL	4
#define AR5K_EEPROM_N_XPD0_POINTS	4
#define AR5K_EEPROM_N_XPD3_POINTS	3
#define AR5K_EEPROM_N_INTERCEPT_10_2GHZ	35
#define AR5K_EEPROM_N_INTERCEPT_10_5GHZ	55
#define AR5K_EEPROM_POWER_M		0x3f
#define AR5K_EEPROM_POWER_MIN		0
#define AR5K_EEPROM_POWER_MAX		3150
#define AR5K_EEPROM_POWER_STEP		50
#define AR5K_EEPROM_POWER_TABLE_SIZE	64
#define AR5K_EEPROM_N_POWER_LOC_11B	4
#define AR5K_EEPROM_N_POWER_LOC_11G	6
#define AR5K_EEPROM_I_GAIN		10
#define AR5K_EEPROM_CCK_OFDM_DELTA	15
#define AR5K_EEPROM_N_IQ_CAL		2

struct ar5k_eeprom_info {
	u_int16_t	ee_magic;
	u_int16_t	ee_protect;
	u_int16_t	ee_regdomain;
	u_int16_t	ee_version;
	u_int16_t	ee_header;
	u_int16_t	ee_ant_gain;
	u_int16_t	ee_misc0;
	u_int16_t	ee_misc1;
	u_int16_t	ee_cck_ofdm_gain_delta;
	u_int16_t	ee_cck_ofdm_power_delta;
	u_int16_t	ee_scaled_cck_delta;
	u_int16_t	ee_tx_clip;
	u_int16_t	ee_pwd_84;
	u_int16_t	ee_pwd_90;
	u_int16_t	ee_gain_select;

	u_int16_t	ee_i_cal[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_q_cal[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_fixed_bias[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_xr_power[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_switch_settling[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_ant_tx_rx[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_ant_control[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_PCDAC];
	u_int16_t	ee_ob[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_OBDB];
	u_int16_t	ee_db[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_OBDB];
	u_int16_t	ee_tx_end2xlna_enable[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_tx_end2xpa_disable[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_tx_frm2xpa_enable[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_thr_62[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_xlna_gain[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_xpd[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_x_gain[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_i_gain[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_margin_tx_rx[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_false_detect[AR5K_EEPROM_N_MODES];
	u_int16_t	ee_cal_pier[AR5K_EEPROM_N_MODES][AR5K_EEPROM_N_2GHZ_CHAN];
	u_int16_t	ee_channel[AR5K_EEPROM_N_MODES][AR5K_EEPROM_MAX_CHAN];

	u_int16_t	ee_ctls;
	u_int16_t	ee_ctl[AR5K_EEPROM_MAX_CTLS];

	int16_t		ee_noise_floor_thr[AR5K_EEPROM_N_MODES];
	int8_t		ee_adc_desired_size[AR5K_EEPROM_N_MODES];
	int8_t		ee_pga_desired_size[AR5K_EEPROM_N_MODES];
};

/*
 * Chipset capabilities
 */

typedef struct {
	/*
	 * Supported PHY modes
	 * (ie. IEEE80211_CHAN_A, IEEE80211_CHAN_B, ...)
	 */
	u_int16_t	cap_mode;

	/*
	 * Frequency range (without regulation restrictions)
	 */
	struct {
		u_int16_t	range_2ghz_min;
		u_int16_t	range_2ghz_max;
		u_int16_t	range_5ghz_min;
		u_int16_t	range_5ghz_max;
	} cap_range;

	/*
	 * Active regulation domain settings
	 */
	struct {
		ieee80211_regdomain_t	reg_current;
		ieee80211_regdomain_t	reg_hw;
	} cap_regdomain;

	/*
	 * Values stored in the EEPROM (some of them...)
	 */
	struct ar5k_eeprom_info	cap_eeprom;

	/*
	 * Queue information
	 */
	struct {
		u_int8_t	q_tx_num;
	} cap_queues;
} ar5k_capabilities_t;

/*
 * TX power and TPC settings
 */

#define AR5K_TXPOWER_OFDM(_r, _v)	(				\
	((0 & 1) << ((_v) + 6)) |					\
	(((hal->ah_txpower.txp_rates[(_r)]) & 0x3f) << (_v))		\
)

#define AR5K_TXPOWER_CCK(_r, _v)	(				\
	(hal->ah_txpower.txp_rates[(_r)] & 0x3f) << (_v)		\
)

/*
 * Atheros descriptor definitions
 */

struct ath_tx_status {
	u_int16_t	ts_seqnum;
	u_int16_t	ts_tstamp;
	u_int8_t	ts_status;
	u_int8_t	ts_rate;
	int8_t		ts_rssi;
	u_int8_t	ts_shortretry;
	u_int8_t	ts_longretry;
	u_int8_t	ts_virtcol;
	u_int8_t	ts_antenna;
};

#define HAL_TXSTAT_ALTRATE	0x80
#define HAL_TXERR_XRETRY	0x01
#define HAL_TXERR_FILT		0x02
#define HAL_TXERR_FIFO		0x04

struct ath_rx_status {
	u_int16_t	rs_datalen;
	u_int16_t	rs_tstamp;
	u_int8_t	rs_status;
	u_int8_t	rs_phyerr;
	int8_t		rs_rssi;
	u_int8_t	rs_keyix;
	u_int8_t	rs_rate;
	u_int8_t	rs_antenna;
	u_int8_t	rs_more;
};

#define HAL_RXERR_CRC		0x01
#define HAL_RXERR_PHY		0x02
#define HAL_RXERR_FIFO		0x04
#define HAL_RXERR_DECRYPT	0x08
#define HAL_RXERR_MIC		0x10
#define HAL_RXKEYIX_INVALID	((u_int8_t) - 1)
#define HAL_TXKEYIX_INVALID	((u_int32_t) - 1)

#define HAL_PHYERR_UNDERRUN		0x00
#define HAL_PHYERR_TIMING		0x01
#define HAL_PHYERR_PARITY		0x02
#define HAL_PHYERR_RATE			0x03
#define HAL_PHYERR_LENGTH		0x04
#define HAL_PHYERR_RADAR		0x05
#define HAL_PHYERR_SERVICE		0x06
#define HAL_PHYERR_TOR			0x07
#define HAL_PHYERR_OFDM_TIMING		0x11
#define HAL_PHYERR_OFDM_SIGNAL_PARITY	0x12
#define HAL_PHYERR_OFDM_RATE_ILLEGAL	0x13
#define HAL_PHYERR_OFDM_LENGTH_ILLEGAL	0x14
#define HAL_PHYERR_OFDM_POWER_DROP	0x15
#define HAL_PHYERR_OFDM_SERVICE		0x16
#define HAL_PHYERR_OFDM_RESTART		0x17
#define HAL_PHYERR_CCK_TIMING		0x19
#define HAL_PHYERR_CCK_HEADER_CRC	0x1a
#define HAL_PHYERR_CCK_RATE_ILLEGAL	0x1b
#define HAL_PHYERR_CCK_SERVICE		0x1e
#define HAL_PHYERR_CCK_RESTART		0x1f

struct ath_desc {
	u_int32_t	ds_link;
	u_int32_t	ds_data;
	u_int32_t	ds_ctl0;
	u_int32_t	ds_ctl1;
	u_int32_t	ds_hw[4];

	union {
		struct ath_rx_status rx;
		struct ath_tx_status tx;
	} ds_us;

#define ds_rxstat ds_us.rx
#define ds_txstat ds_us.tx

} __packed;

#define HAL_RXDESC_INTREQ	0x0020

#define HAL_TXDESC_CLRDMASK	0x0001
#define HAL_TXDESC_NOACK	0x0002
#define HAL_TXDESC_RTSENA	0x0004
#define HAL_TXDESC_CTSENA	0x0008
#define HAL_TXDESC_INTREQ	0x0010
#define HAL_TXDESC_VEOL		0x0020

/*
 * Hardware abstraction layer structure
 */

#define AR5K_HAL_FUNCTION(_hal, _n, _f)	(_hal)->ah_##_f = ar5k_##_n##_##_f
#define AR5K_HAL_FUNCTIONS(_t, _n, _a) \
	_t const HAL_RATE_TABLE *(_a _n##_get_rate_table)(struct ath_hal *, \
	    u_int mode); \
	_t void (_a _n##_detach)(struct ath_hal *); \
	/* Reset functions */ \
	_t HAL_BOOL (_a _n##_reset)(struct ath_hal *, HAL_OPMODE, \
	    HAL_CHANNEL *, HAL_BOOL change_channel, HAL_STATUS *status); \
	_t void (_a _n##_set_opmode)(struct ath_hal *); \
	_t HAL_BOOL (_a _n##_calibrate)(struct ath_hal*, \
	    HAL_CHANNEL *); \
	/* Transmit functions */ \
	_t HAL_BOOL (_a _n##_update_tx_triglevel)(struct ath_hal*, \
	    HAL_BOOL level); \
	_t int (_a _n##_setup_tx_queue)(struct ath_hal *, HAL_TX_QUEUE, \
	    const HAL_TXQ_INFO *); \
	_t HAL_BOOL (_a _n##_setup_tx_queueprops)(struct ath_hal *, int queue, \
	    const HAL_TXQ_INFO *); \
	_t HAL_BOOL (_a _n##_release_tx_queue)(struct ath_hal *, u_int queue); \
	_t HAL_BOOL (_a _n##_reset_tx_queue)(struct ath_hal *, u_int queue); \
	_t u_int32_t (_a _n##_get_tx_buf)(struct ath_hal *, u_int queue); \
	_t HAL_BOOL (_a _n##_put_tx_buf)(struct ath_hal *, u_int, \
	    u_int32_t phys_addr); \
	_t HAL_BOOL (_a _n##_tx_start)(struct ath_hal *, u_int queue); \
	_t HAL_BOOL (_a _n##_stop_tx_dma)(struct ath_hal *, u_int queue); \
	_t HAL_BOOL (_a _n##_setup_tx_desc)(struct ath_hal *, \
	    struct ath_desc *, \
	    u_int packet_length, u_int header_length, HAL_PKT_TYPE type, \
	    u_int txPower, u_int tx_rate0, u_int tx_tries0, u_int key_index, \
	    u_int antenna_mode, u_int flags, u_int rtscts_rate, \
	    u_int rtscts_duration); \
	_t HAL_BOOL (_a _n##_setup_xtx_desc)(struct ath_hal *, \
	    struct ath_desc *, \
	    u_int tx_rate1, u_int tx_tries1, u_int tx_rate2, u_int tx_tries2, \
	    u_int tx_rate3, u_int tx_tries3); \
	_t HAL_BOOL (_a _n##_fill_tx_desc)(struct ath_hal *, \
	    struct ath_desc *, \
	    u_int segLen, HAL_BOOL firstSeg, HAL_BOOL lastSeg); \
	_t HAL_STATUS (_a _n##_proc_tx_desc)(struct ath_hal *, \
	    struct ath_desc *); \
	_t HAL_BOOL (_a _n##_has_veol)(struct ath_hal *); \
	/* Receive Functions */ \
	_t u_int32_t (_a _n##_get_rx_buf)(struct ath_hal*); \
	_t void (_a _n##_put_rx_buf)(struct ath_hal*, u_int32_t rxdp); \
	_t void (_a _n##_start_rx)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_stop_rx_dma)(struct ath_hal*); \
	_t void (_a _n##_start_rx_pcu)(struct ath_hal*); \
	_t void (_a _n##_stop_pcu_recv)(struct ath_hal*); \
	_t void (_a _n##_set_mcast_filter)(struct ath_hal*, \
	    u_int32_t filter0, u_int32_t filter1); \
	_t HAL_BOOL (_a _n##_set_mcast_filterindex)(struct ath_hal*, \
	    u_int32_t index); \
	_t HAL_BOOL (_a _n##_clear_mcast_filter_idx)(struct ath_hal*, \
	    u_int32_t index); \
	_t u_int32_t (_a _n##_get_rx_filter)(struct ath_hal*); \
	_t void (_a _n##_set_rx_filter)(struct ath_hal*, u_int32_t); \
	_t HAL_BOOL (_a _n##_setup_rx_desc)(struct ath_hal *, \
	    struct ath_desc *, u_int32_t size, u_int flags); \
	_t HAL_STATUS (_a _n##_proc_rx_desc)(struct ath_hal *, \
	    struct ath_desc *, u_int32_t phyAddr, struct ath_desc *next); \
	_t void (_a _n##_set_rx_signal)(struct ath_hal *); \
	/* Misc Functions */ \
	_t void (_a _n##_dump_state)(struct ath_hal *); \
	_t HAL_BOOL (_a _n##_get_diag_state)(struct ath_hal *, int, void **, \
	    u_int *); \
	_t void (_a _n##_get_lladdr)(struct ath_hal *, u_int8_t *); \
	_t HAL_BOOL (_a _n##_set_lladdr)(struct ath_hal *, \
	    const u_int8_t*); \
	_t HAL_BOOL (_a _n##_set_regdomain)(struct ath_hal*, \
	    u_int16_t, HAL_STATUS *); \
	_t void (_a _n##_set_ledstate)(struct ath_hal*, HAL_LED_STATE); \
	_t void (_a _n##_set_associd)(struct ath_hal*, \
	    const u_int8_t *bssid, u_int16_t assocId, u_int16_t timOffset); \
	_t HAL_BOOL (_a _n##_set_gpio_output)(struct ath_hal *, \
	    u_int32_t gpio); \
	_t HAL_BOOL (_a _n##_set_gpio_input)(struct ath_hal *, \
	    u_int32_t gpio); \
	_t u_int32_t (_a _n##_get_gpio)(struct ath_hal *, u_int32_t gpio); \
	_t HAL_BOOL (_a _n##_set_gpio)(struct ath_hal *, u_int32_t gpio, \
	    u_int32_t val); \
	_t void (_a _n##_set_gpio_intr)(struct ath_hal*, u_int, u_int32_t); \
	_t u_int32_t (_a _n##_get_tsf32)(struct ath_hal*); \
	_t u_int64_t (_a _n##_get_tsf64)(struct ath_hal*); \
	_t void (_a _n##_reset_tsf)(struct ath_hal*); \
	_t u_int16_t (_a _n##_get_regdomain)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_detect_card_present)(struct ath_hal*); \
	_t void (_a _n##_update_mib_counters)(struct ath_hal*, \
	    HAL_MIB_STATS*); \
	_t HAL_BOOL (_a _n##_is_cipher_supported)(struct ath_hal*, \
	    HAL_CIPHER); \
	_t HAL_RFGAIN (_a _n##_get_rf_gain)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_set_slot_time)(struct ath_hal*, u_int);	\
	_t u_int (_a _n##_get_slot_time)(struct ath_hal*);		\
	_t HAL_BOOL (_a _n##_set_ack_timeout)(struct ath_hal *, u_int); \
	_t u_int (_a _n##_get_ack_timeout)(struct ath_hal*);		\
	_t HAL_BOOL (_a _n##_set_cts_timeout)(struct ath_hal*, u_int);	\
	_t u_int (_a _n##_get_cts_timeout)(struct ath_hal*);		\
	/* Key Cache Functions */ \
	_t u_int32_t (_a _n##_get_keycache_size)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_reset_key)(struct ath_hal*, \
	    u_int16_t); \
	_t HAL_BOOL (_a _n##_is_key_valid)(struct ath_hal *, \
	    u_int16_t); \
	_t HAL_BOOL (_a _n##_set_key)(struct ath_hal*, u_int16_t, \
	    const HAL_KEYVAL *, const u_int8_t *, int);	\
	_t HAL_BOOL (_a _n##_set_key_lladdr)(struct ath_hal*, \
	    u_int16_t, const u_int8_t *); \
	_t HAL_BOOL (_a _n##_softcrypto)(struct ath_hal *, HAL_BOOL); \
	/* Power Management Functions */ \
	_t HAL_BOOL (_a _n##_set_power)(struct ath_hal*, \
	    HAL_POWER_MODE mode, \
	    HAL_BOOL set_chip, u_int16_t sleep_duration); \
	_t HAL_POWER_MODE (_a _n##_get_power_mode)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_query_pspoll_support)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_init_pspoll)(struct ath_hal*); \
	_t HAL_BOOL (_a _n##_enable_pspoll)(struct ath_hal *, u_int8_t *, \
	    u_int16_t); \
	_t HAL_BOOL (_a _n##_disable_pspoll)(struct ath_hal *); \
	/* Beacon Management Functions */ \
	_t void (_a _n##_init_beacon)(struct ath_hal *, u_int32_t nexttbtt, \
	    u_int32_t intval); \
	_t void (_a _n##_set_beacon_timers)(struct ath_hal *, \
	    const HAL_BEACON_STATE *, u_int32_t tsf, u_int32_t dtimCount, \
	    u_int32_t cfpCcount); \
	_t void (_a _n##_reset_beacon)(struct ath_hal *); \
	_t HAL_BOOL (_a _n##_wait_for_beacon)(struct ath_hal *, \
	    bus_addr_t); \
	/* Interrupt functions */ \
	_t HAL_BOOL (_a _n##_is_intr_pending)(struct ath_hal *); \
	_t HAL_BOOL (_a _n##_get_isr)(struct ath_hal *, \
	    u_int32_t *); \
	_t u_int32_t (_a _n##_get_intr)(struct ath_hal *); \
	_t HAL_INT (_a _n##_set_intr)(struct ath_hal *, HAL_INT); \
	/* Chipset functions (ar5k-specific, non-HAL) */ \
	_t HAL_BOOL (_a _n##_get_capabilities)(struct ath_hal *); \
	_t void (_a _n##_radar_alert)(struct ath_hal *, HAL_BOOL enable); \
	_t HAL_BOOL (_a _n##_eeprom_is_busy)(struct ath_hal *); \
	_t int (_a _n##_eeprom_read)(struct ath_hal *, u_int32_t offset, \
	    u_int16_t *data); \
	_t int (_a _n##_eeprom_write)(struct ath_hal *, u_int32_t offset, \
	    u_int16_t data); \
	/* Unused functions */ \
	_t HAL_BOOL (_a _n##_get_tx_queueprops)(struct ath_hal *, int, \
	    HAL_TXQ_INFO *); \
	_t u_int32_t  (_a _n##_num_tx_pending)(struct ath_hal *, u_int); \
	_t HAL_BOOL (_a _n##_phy_disable)(struct ath_hal *); \
	_t HAL_BOOL (_a _n##_set_txpower_limit)(struct ath_hal *, u_int); \
	_t void (_a _n##_set_def_antenna)(struct ath_hal *, u_int); \
	_t u_int  (_a _n ##_get_def_antenna)(struct ath_hal *); \
	_t HAL_BOOL (_a _n##_set_bssid_mask)(struct ath_hal *, \
	    const u_int8_t*);

#define AR5K_MAX_GPIO		10
#define AR5K_MAX_RF_BANKS	8

struct ath_hal {
	u_int32_t		ah_magic;
	u_int32_t		ah_abi;
	u_int16_t		ah_device;
	u_int16_t		ah_sub_vendor;

	void			*ah_sc;
	bus_space_tag_t		ah_st;
	bus_space_handle_t	ah_sh;

	HAL_INT			ah_imr;

	HAL_OPMODE		ah_op_mode;
	HAL_POWER_MODE		ah_power_mode;
	HAL_CHANNEL		ah_current_channel;
	HAL_BOOL		ah_calibration;
	HAL_BOOL		ah_running;
	HAL_BOOL		ah_single_chip;
	HAL_BOOL		ah_pci_express;
	HAL_RFGAIN		ah_rf_gain;

	int			ah_chanoff;

	HAL_RATE_TABLE		ah_rt_11a;
	HAL_RATE_TABLE		ah_rt_11b;
	HAL_RATE_TABLE		ah_rt_11g;
	HAL_RATE_TABLE		ah_rt_xr;

	u_int32_t		ah_mac_srev;
	u_int16_t		ah_mac_version;
	u_int16_t		ah_mac_revision;
	u_int16_t		ah_phy_revision;
	u_int16_t		ah_radio_5ghz_revision;
	u_int16_t		ah_radio_2ghz_revision;

	enum ar5k_version	ah_version;
	enum ar5k_radio		ah_radio;

	u_int32_t		ah_phy;
	u_int32_t		ah_phy_spending;

	HAL_BOOL		ah_5ghz;
	HAL_BOOL		ah_2ghz;

#define ah_regdomain		ah_capabilities.cap_regdomain.reg_current
#define ah_regdomain_hw		ah_capabilities.cap_regdomain.reg_hw
#define ah_modes		ah_capabilities.cap_mode
#define ah_ee_version		ah_capabilities.cap_eeprom.ee_version

	u_int32_t		ah_atim_window;
	u_int32_t		ah_aifs;
	u_int32_t		ah_cw_min;
	u_int32_t		ah_cw_max;
	HAL_BOOL		ah_software_retry;
	u_int32_t		ah_limit_tx_retries;

	u_int32_t		ah_antenna[AR5K_EEPROM_N_MODES][HAL_ANT_MAX];
	HAL_BOOL		ah_ant_diversity;

	u_int8_t		ah_sta_id[IEEE80211_ADDR_LEN];
	u_int8_t		ah_bssid[IEEE80211_ADDR_LEN];

	u_int32_t		ah_gpio[AR5K_MAX_GPIO];
	int			ah_gpio_npins;

	ar5k_capabilities_t	ah_capabilities;

	HAL_TXQ_INFO		ah_txq[HAL_NUM_TX_QUEUES];
	u_int32_t		ah_txq_interrupts;

	u_int32_t		*ah_rf_banks;
	size_t			ah_rf_banks_size;
	struct ar5k_gain	ah_gain;
	u_int32_t		ah_offset[AR5K_MAX_RF_BANKS];

	struct {
		u_int16_t	txp_pcdac[AR5K_EEPROM_POWER_TABLE_SIZE];
		u_int16_t	txp_rates[AR5K_MAX_RATES];
		int16_t		txp_min, txp_max;
		HAL_BOOL	txp_tpc;
		int16_t		txp_ofdm;
	} ah_txpower;

	struct {
		HAL_BOOL	r_enabled;
		int		r_last_alert;
		HAL_CHANNEL	r_last_channel;
	} ah_radar;

	/*
	 * Function pointers
	 */
	AR5K_HAL_FUNCTIONS(, ah, *);
};

/*
 * Common silicon revision/version values
 */
enum ar5k_srev_type {
	AR5K_VERSION_VER,
	AR5K_VERSION_REV,
	AR5K_VERSION_RAD,
	AR5K_VERSION_DEV,
};

struct ar5k_srev_name {
	const char		*sr_name;
	enum ar5k_srev_type	sr_type;
	u_int			sr_val;
};

#define AR5K_SREV_NAME	{						\
	{ "5210",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5210 },	\
	{ "5311",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311 },	\
	{ "5311a",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311A },\
	{ "5311b",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311B },\
	{ "5211",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5211 },	\
	{ "5212",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5212 },	\
	{ "5213",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5213 },	\
	{ "5213A",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5213A },\
	{ "2413",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2413 },\
	{ "2414",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2414 },\
	{ "2424",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2424 },\
	{ "5424",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5424 },\
	{ "5413",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5413 },\
	{ "5414",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5414 },\
	{ "5416",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5416 },\
	{ "5418",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5418 },\
	{ "2425",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2425 },\
	{ "xxxx",	AR5K_VERSION_VER,	AR5K_SREV_UNKNOWN },	\
	{ "5110",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5110 },	\
	{ "5111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5111 },	\
	{ "2111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2111 },	\
	{ "5112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112 },	\
	{ "5112a",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112A },	\
	{ "2112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112 },	\
	{ "2112a",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112A },	\
	{ "2413",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_SC0 },	\
	{ "2414",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_SC1 },	\
	{ "5424",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_SC2 },	\
	{ "xxxx",	AR5K_VERSION_RAD,	AR5K_SREV_UNKNOWN },	\
	{ "2413",	AR5K_VERSION_DEV,	AR5K_DEVID_AR2413 },	\
	{ "5413",	AR5K_VERSION_DEV,	AR5K_DEVID_AR5413 },	\
	{ "5424",	AR5K_VERSION_DEV,	AR5K_DEVID_AR5424 },	\
	{ "xxxx",	AR5K_VERSION_DEV,	AR5K_SREV_UNKNOWN }	\
}
	/* XXX: ar5k_printver() needs AR5K_SREV_UNKNOWN in the last member. */

#define AR5K_SREV_UNKNOWN	0xffff

#define AR5K_SREV_VER_AR5210	0x00
#define AR5K_SREV_VER_AR5311	0x10
#define AR5K_SREV_VER_AR5311A	0x20
#define AR5K_SREV_VER_AR5311B	0x30
#define AR5K_SREV_VER_AR5211	0x40
#define AR5K_SREV_VER_AR5212	0x50
#define AR5K_SREV_VER_AR5213	0x55
#define AR5K_SREV_VER_AR5213A	0x59
#define AR5K_SREV_VER_AR2413	0x78
#define AR5K_SREV_VER_AR2414	0x79
#define AR5K_SREV_VER_AR2424	0xa0	/* PCI-Express */
#define AR5K_SREV_VER_AR5424	0xa3	/* PCI-Express */
#define AR5K_SREV_VER_AR5413	0xa4
#define AR5K_SREV_VER_AR5414	0xa5
#define AR5K_SREV_VER_AR5416	0xc0	/* PCI-Express */
#define AR5K_SREV_VER_AR5418	0xca	/* PCI-Express */
#define AR5K_SREV_VER_AR2425	0xe2	/* PCI-Express */
#define AR5K_SREV_VER_UNSUPP	0xff

#define AR5K_SREV_RAD_5110	0x00
#define AR5K_SREV_RAD_5111	0x10
#define AR5K_SREV_RAD_5111A	0x15
#define AR5K_SREV_RAD_2111	0x20
#define AR5K_SREV_RAD_5112	0x30
#define AR5K_SREV_RAD_5112A	0x35
#define AR5K_SREV_RAD_2112	0x40
#define AR5K_SREV_RAD_2112A	0x45
#define AR5K_SREV_RAD_SC0	0x56
#define AR5K_SREV_RAD_SC1	0x63
#define AR5K_SREV_RAD_SC2	0xa2
#define AR5K_SREV_RAD_5133	0xc0
#define AR5K_SREV_RAD_UNSUPP	0xff

#define AR5K_DEVID_AR2413	0x001a
#define AR5K_DEVID_AR5413	0x001b
#define AR5K_DEVID_AR5424	0x001c

/*
 * Misc defines
 */

#define HAL_ABI_VERSION		0x04090901 /* YYMMDDnn */

#define AR5K_PRINTF(fmt, ...)	printf("%s: " fmt, __func__, ##__VA_ARGS__)
#define AR5K_PRINT(fmt)		printf("%s: " fmt, __func__)
#ifdef AR5K_DEBUG
#define AR5K_TRACE		printf("%s:%d\n", __func__, __LINE__)
#else
#define AR5K_TRACE
#endif
#define AR5K_DELAY(_n)		delay(_n)

typedef struct ath_hal * (ar5k_attach_t)
	(u_int16_t, void *, bus_space_tag_t, bus_space_handle_t, HAL_STATUS *);
typedef HAL_BOOL (ar5k_rfgain_t)
	(struct ath_hal *, HAL_CHANNEL *, u_int);

/*
 * Some tuneable values (these should be changeable by the user)
 */

#define AR5K_TUNE_DMA_BEACON_RESP		2
#define AR5K_TUNE_SW_BEACON_RESP		10
#define AR5K_TUNE_ADDITIONAL_SWBA_BACKOFF	0
#define AR5K_TUNE_RADAR_ALERT			AH_FALSE
#define AR5K_TUNE_MIN_TX_FIFO_THRES		1
#define AR5K_TUNE_MAX_TX_FIFO_THRES		((IEEE80211_MAX_LEN / 64) + 1)
#define AR5K_TUNE_RSSI_THRES			1792
#define AR5K_TUNE_REGISTER_TIMEOUT		20000
#define AR5K_TUNE_REGISTER_DWELL_TIME		20000
#define AR5K_TUNE_BEACON_INTERVAL		100
#define AR5K_TUNE_AIFS				2
#define AR5K_TUNE_AIFS_11B			2
#define AR5K_TUNE_AIFS_XR			0
#define AR5K_TUNE_CWMIN				15
#define AR5K_TUNE_CWMIN_11B			31
#define AR5K_TUNE_CWMIN_XR			3
#define AR5K_TUNE_CWMAX				1023
#define AR5K_TUNE_CWMAX_11B			1023
#define AR5K_TUNE_CWMAX_XR			7
#define AR5K_TUNE_NOISE_FLOOR			-72
#define AR5K_TUNE_MAX_TXPOWER			60
#define AR5K_TUNE_DEFAULT_TXPOWER		30
#define AR5K_TUNE_TPC_TXPOWER			AH_TRUE
#define AR5K_TUNE_ANT_DIVERSITY			AH_TRUE

/* Default regulation domain if stored value EEPROM value is invalid */
#define AR5K_TUNE_REGDOMAIN	DMN_FCC2_FCCA	/* Canada */

/*
 * Common initial register values
 */

#define AR5K_INIT_MODE				(			\
	IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_DYN			\
)
#define AR5K_INIT_TX_LATENCY			502
#define AR5K_INIT_USEC				39
#define AR5K_INIT_USEC_TURBO			79
#define AR5K_INIT_USEC_32			31
#define AR5K_INIT_CARR_SENSE_EN			1
#define AR5K_INIT_PROG_IFS			920
#define AR5K_INIT_PROG_IFS_TURBO		960
#define AR5K_INIT_EIFS				3440
#define AR5K_INIT_EIFS_TURBO			6880
#define AR5K_INIT_SLOT_TIME			396
#define AR5K_INIT_SLOT_TIME_TURBO		480
#define AR5K_INIT_ACK_CTS_TIMEOUT		1024
#define AR5K_INIT_ACK_CTS_TIMEOUT_TURBO		0x08000800
#define AR5K_INIT_SIFS				560
#define AR5K_INIT_SIFS_TURBO			480
#define AR5K_INIT_SH_RETRY			10
#define AR5K_INIT_LG_RETRY			AR5K_INIT_SH_RETRY
#define AR5K_INIT_SSH_RETRY			32
#define AR5K_INIT_SLG_RETRY			AR5K_INIT_SSH_RETRY
#define AR5K_INIT_TX_RETRY			10
#define AR5K_INIT_TOPS				8
#define AR5K_INIT_RXNOFRM			8
#define AR5K_INIT_RPGTO				0
#define AR5K_INIT_TXNOFRM			0
#define AR5K_INIT_BEACON_PERIOD			65535
#define AR5K_INIT_TIM_OFFSET			0
#define AR5K_INIT_BEACON_EN			0
#define AR5K_INIT_RESET_TSF			0
#define AR5K_INIT_TRANSMIT_LATENCY		(			\
	(AR5K_INIT_TX_LATENCY << 14) | (AR5K_INIT_USEC_32 << 7) |	\
	(AR5K_INIT_USEC)						\
)
#define AR5K_INIT_TRANSMIT_LATENCY_TURBO	(			\
	(AR5K_INIT_TX_LATENCY << 14) | (AR5K_INIT_USEC_32 << 7) |	\
	(AR5K_INIT_USEC_TURBO)						\
)
#define AR5K_INIT_PROTO_TIME_CNTRL		(			\
	(AR5K_INIT_CARR_SENSE_EN << 26) | (AR5K_INIT_EIFS << 12) |	\
	(AR5K_INIT_PROG_IFS)						\
)
#define AR5K_INIT_PROTO_TIME_CNTRL_TURBO	(			\
	(AR5K_INIT_CARR_SENSE_EN << 26) | (AR5K_INIT_EIFS_TURBO << 12) |\
	(AR5K_INIT_PROG_IFS_TURBO)					\
)
#define AR5K_INIT_BEACON_CONTROL		(			\
	(AR5K_INIT_RESET_TSF << 24) | (AR5K_INIT_BEACON_EN << 23) |	\
	(AR5K_INIT_TIM_OFFSET << 16) | (AR5K_INIT_BEACON_PERIOD)	\
)

/*
 * AR5k register access
 */

#define AR5K_REG_WRITE(_reg, _val)					\
	bus_space_write_4(hal->ah_st, hal->ah_sh, (_reg), (_val))
#define AR5K_REG_READ(_reg)						\
	bus_space_read_4(hal->ah_st, hal->ah_sh, (_reg))

#define AR5K_REG_SM(_val, _flags)					\
	(((uint32_t)(_val) << _flags##_S) & (_flags))
#define AR5K_REG_MS(_val, _flags)					\
	(((uint32_t)(_val) & (_flags)) >> _flags##_S)
#define AR5K_REG_WRITE_BITS(_reg, _flags, _val)				\
	AR5K_REG_WRITE(_reg, (AR5K_REG_READ(_reg) &~ (_flags)) |	\
	    (((_val) << _flags##_S) & (_flags)))
#define AR5K_REG_MASKED_BITS(_reg, _flags, _mask)			\
	AR5K_REG_WRITE(_reg, (AR5K_REG_READ(_reg) & (_mask)) | (_flags))
#define AR5K_REG_ENABLE_BITS(_reg, _flags)				\
	AR5K_REG_WRITE(_reg, AR5K_REG_READ(_reg) | (_flags))
#define AR5K_REG_DISABLE_BITS(_reg, _flags)				\
	AR5K_REG_WRITE(_reg, AR5K_REG_READ(_reg) &~ (_flags))

#define AR5K_PHY_WRITE(_reg, _val)					\
	AR5K_REG_WRITE(hal->ah_phy + ((_reg) << 2), _val)
#define AR5K_PHY_READ(_reg)						\
	AR5K_REG_READ(hal->ah_phy + ((_reg) << 2))

#define AR5K_REG_WAIT(_i)						\
	if (_i % 64)							\
		AR5K_DELAY(1);

#define AR5K_EEPROM_READ(_o, _v)	{				\
	if ((ret = hal->ah_eeprom_read(hal, (_o),			\
		 &(_v))) != 0)						\
		return (ret);						\
}
#define AR5K_EEPROM_READ_HDR(_o, _v)					\
	AR5K_EEPROM_READ(_o, hal->ah_capabilities.cap_eeprom._v);	\

/* Read status of selected queue */
#define AR5K_REG_READ_Q(_reg, _queue)					\
	(AR5K_REG_READ(_reg) & (1 << _queue))				\

#define AR5K_REG_WRITE_Q(_reg, _queue)					\
	AR5K_REG_WRITE(_reg, (1 << _queue))

#define AR5K_Q_ENABLE_BITS(_reg, _queue) do {				\
	_reg |= 1 << _queue;						\
} while (0)

#define AR5K_Q_DISABLE_BITS(_reg, _queue) do {				\
	_reg &= ~(1 << _queue);						\
} while (0)

#define AR5K_LOW_ID(_a)		(					\
	(_a)[0] | (_a)[1] << 8 | (_a)[2] << 16 | (_a)[3] << 24		\
)
#define AR5K_HIGH_ID(_a)	((_a)[4] | (_a)[5] << 8)

/*
 * Unaligned little endian access
 */

#define AR5K_LE_READ_2(_p)						\
	(((const u_int8_t *)(_p))[0] | (((const u_int8_t *)(_p))[1] << 8))
#define AR5K_LE_READ_4(_p) \
	(((const u_int8_t *)(_p))[0] |					\
	(((const u_int8_t *)(_p))[1] << 8) |				\
	(((const u_int8_t *)(_p))[2] << 16) |				\
	(((const u_int8_t *)(_p))[3] << 24))
#define AR5K_LE_WRITE_2(_p, _val) \
	((((u_int8_t *)(_p))[0] = ((u_int32_t)(_val) & 0xff)),		\
	(((u_int8_t *)(_p))[1] = (((u_int32_t)(_val) >> 8) & 0xff)))
#define AR5K_LE_WRITE_4(_p, _val)					\
	((((u_int8_t *)(_p))[0] = ((u_int32_t)(_val) & 0xff)),		\
	(((u_int8_t *)(_p))[1] = (((u_int32_t)(_val) >> 8) & 0xff)),	\
	(((u_int8_t *)(_p))[2] = (((u_int32_t)(_val) >> 16) & 0xff)),	\
	(((u_int8_t *)(_p))[3] = (((u_int32_t)(_val) >> 24) & 0xff)))

/*
 * Initial register values
 */

struct ar5k_ini {
	u_int16_t	ini_register;
	u_int32_t	ini_value;

	enum {
		AR5K_INI_WRITE = 0,
		AR5K_INI_READ = 1,
	} ini_mode;
};

#define AR5K_PCU_MIN		0x8000
#define AR5K_PCU_MAX		0x8fff

#define AR5K_INI_VAL_11A	0
#define AR5K_INI_VAL_11A_TURBO	1
#define AR5K_INI_VAL_11B	2
#define AR5K_INI_VAL_11G	3
#define AR5K_INI_VAL_11G_TURBO	4
#define AR5K_INI_VAL_XR		0
#define AR5K_INI_VAL_MAX	5

struct ar5k_mode {
	u_int16_t	mode_register;
	u_int32_t	mode_value[AR5K_INI_VAL_MAX];
};

#define AR5K_INI_PHY_5111	0
#define AR5K_INI_PHY_5112	1
#define AR5K_INI_PHY_511X	1

#define AR5K_AR5111_INI_RF_MAX_BANKS    AR5K_MAX_RF_BANKS
#define AR5K_AR5112_INI_RF_MAX_BANKS	AR5K_MAX_RF_BANKS

struct ar5k_ini_rf {
	u_int8_t	rf_bank;
	u_int16_t	rf_register;
	u_int32_t	rf_value[5];
};

#define AR5K_AR5111_INI_RF	{						\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00380000, 0x00380000, 0x00380000, 0x00380000, 0x00380000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 0, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x000000c0, 0x00000080, 0x00000080 } },	\
	{ 0, 0x989c,								\
	    { 0x000400f9, 0x000400f9, 0x000400ff, 0x000400fd, 0x000400fd } },	\
	{ 0, 0x98d4,								\
	    { 0x00000000, 0x00000000, 0x00000004, 0x00000004, 0x00000004 } },	\
	{ 1, 0x98d4,								\
	    { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },	\
	{ 2, 0x98d4,								\
	    { 0x00000010, 0x00000014, 0x00000010, 0x00000010, 0x00000014 } },	\
	{ 3, 0x98d8,								\
	    { 0x00601068, 0x00601068, 0x00601068, 0x00601068, 0x00601068 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x10000000, 0x10000000, 0x10000000, 0x10000000, 0x10000000 } },	\
	{ 6, 0x989c,								\
	    { 0x04000000, 0x04000000, 0x04000000, 0x04000000, 0x04000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x0a000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x003800c0, 0x00380080, 0x023800c0, 0x003800c0, 0x003800c0 } },	\
	{ 6, 0x989c,								\
	    { 0x00020006, 0x00020006, 0x00000006, 0x00020006, 0x00020006 } },	\
	{ 6, 0x989c,								\
	    { 0x00000089, 0x00000089, 0x00000089, 0x00000089, 0x00000089 } },	\
	{ 6, 0x989c,								\
	    { 0x000000a0, 0x000000a0, 0x000000a0, 0x000000a0, 0x000000a0 } },	\
	{ 6, 0x989c,								\
	    { 0x00040007, 0x00040007, 0x00040007, 0x00040007, 0x00040007 } },	\
	{ 6, 0x98d4,								\
	    { 0x0000001a, 0x0000001a, 0x0000001a, 0x0000001a, 0x0000001a } },	\
	{ 7, 0x989c,								\
	    { 0x00000040, 0x00000048, 0x00000040, 0x00000040, 0x00000040 } },	\
	{ 7, 0x989c,								\
	    { 0x00000010, 0x00000010, 0x00000010, 0x00000010, 0x00000010 } },	\
	{ 7, 0x989c,								\
	    { 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008 } },	\
	{ 7, 0x989c,								\
	    { 0x0000004f, 0x0000004f, 0x0000004f, 0x0000004f, 0x0000004f } },	\
	{ 7, 0x989c,								\
	    { 0x000000f1, 0x000000f1, 0x00000061, 0x000000f1, 0x000000f1 } },	\
	{ 7, 0x989c,								\
	    { 0x0000904f, 0x0000904f, 0x0000904c, 0x0000904f, 0x0000904f } },	\
	{ 7, 0x989c,								\
	    { 0x0000125a, 0x0000125a, 0x0000129a, 0x0000125a, 0x0000125a } },	\
	{ 7, 0x98cc,								\
	    { 0x0000000e, 0x0000000e, 0x0000000f, 0x0000000e, 0x0000000e } },	\
}

#define AR5K_AR5112_INI_RF	{						\
	{ 1, 0x98d4,								\
	    { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },	\
	{ 2, 0x98d0,								\
	    { 0x03060408, 0x03070408, 0x03060408, 0x03060408, 0x03070408 } },	\
	{ 3, 0x98dc,								\
	    { 0x00a0c0c0, 0x00a0c0c0, 0x00e0c0c0, 0x00e0c0c0, 0x00e0c0c0 } },	\
	{ 6, 0x989c,								\
	    { 0x00a00000, 0x00a00000, 0x00a00000, 0x00a00000, 0x00a00000 } },	\
	{ 6, 0x989c,								\
	    { 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00660000, 0x00660000, 0x00660000, 0x00660000, 0x00660000 } },	\
	{ 6, 0x989c,								\
	    { 0x00db0000, 0x00db0000, 0x00db0000, 0x00db0000, 0x00db0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00f10000, 0x00f10000, 0x00f10000, 0x00f10000, 0x00f10000 } },	\
	{ 6, 0x989c,								\
	    { 0x00120000, 0x00120000, 0x00120000, 0x00120000, 0x00120000 } },	\
	{ 6, 0x989c,								\
	    { 0x00120000, 0x00120000, 0x00120000, 0x00120000, 0x00120000 } },	\
	{ 6, 0x989c,								\
	    { 0x00730000, 0x00730000, 0x00730000, 0x00730000, 0x00730000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x008b0000, 0x008b0000, 0x008b0000, 0x008b0000, 0x008b0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00600000, 0x00600000, 0x00600000, 0x00600000, 0x00600000 } },	\
	{ 6, 0x989c,								\
	    { 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00840000, 0x00840000, 0x00840000, 0x00840000, 0x00840000 } },	\
	{ 6, 0x989c,								\
	    { 0x00640000, 0x00640000, 0x00640000, 0x00640000, 0x00640000 } },	\
	{ 6, 0x989c,								\
	    { 0x00200000, 0x00200000, 0x00200000, 0x00200000, 0x00200000 } },	\
	{ 6, 0x989c,								\
	    { 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0x00240000 } },	\
	{ 6, 0x989c,								\
	    { 0x00250000, 0x00250000, 0x00250000, 0x00250000, 0x00250000 } },	\
	{ 6, 0x989c,								\
	    { 0x00110000, 0x00110000, 0x00110000, 0x00110000, 0x00110000 } },	\
	{ 6, 0x989c,								\
	    { 0x00110000, 0x00110000, 0x00110000, 0x00110000, 0x00110000 } },	\
	{ 6, 0x989c,								\
	    { 0x00510000, 0x00510000, 0x00510000, 0x00510000, 0x00510000 } },	\
	{ 6, 0x989c,								\
	    { 0x1c040000, 0x1c040000, 0x1c040000, 0x1c040000, 0x1c040000 } },	\
	{ 6, 0x989c,								\
	    { 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000, 0x000a0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00a10000, 0x00a10000, 0x00a10000, 0x00a10000, 0x00a10000 } },	\
	{ 6, 0x989c,								\
	    { 0x00400000, 0x00400000, 0x00400000, 0x00400000, 0x00400000 } },	\
	{ 6, 0x989c,								\
	    { 0x03090000, 0x03090000, 0x03090000, 0x03090000, 0x03090000 } },	\
	{ 6, 0x989c,								\
	    { 0x06000000, 0x06000000, 0x06000000, 0x06000000, 0x06000000 } },	\
	{ 6, 0x989c,								\
	    { 0x000000b0, 0x000000b0, 0x000000a8, 0x000000a8, 0x000000a8 } },	\
	{ 6, 0x989c,								\
	    { 0x0000002e, 0x0000002e, 0x0000002e, 0x0000002e, 0x0000002e } },	\
	{ 6, 0x989c,								\
	    { 0x006c4a41, 0x006c4a41, 0x006c4af1, 0x006c4a61, 0x006c4a61 } },	\
	{ 6, 0x989c,								\
	    { 0x0050892a, 0x0050892a, 0x0050892b, 0x0050892b, 0x0050892b } },	\
	{ 6, 0x989c,								\
	    { 0x00842400, 0x00842400, 0x00842400, 0x00842400, 0x00842400 } },	\
	{ 6, 0x989c,								\
	    { 0x00c69200, 0x00c69200, 0x00c69200, 0x00c69200, 0x00c69200 } },	\
	{ 6, 0x98d0,								\
	    { 0x0002000c, 0x0002000c, 0x0002000c, 0x0002000c, 0x0002000c } },	\
	{ 7, 0x989c,								\
	    { 0x00000094, 0x00000094, 0x00000094, 0x00000094, 0x00000094 } },	\
	{ 7, 0x989c,								\
	    { 0x00000091, 0x00000091, 0x00000091, 0x00000091, 0x00000091 } },	\
	{ 7, 0x989c,								\
	    { 0x0000000a, 0x0000000a, 0x00000012, 0x00000012, 0x00000012 } },	\
	{ 7, 0x989c,								\
	    { 0x00000080, 0x00000080, 0x00000080, 0x00000080, 0x00000080 } },	\
	{ 7, 0x989c,								\
	    { 0x000000c1, 0x000000c1, 0x000000c1, 0x000000c1, 0x000000c1 } },	\
	{ 7, 0x989c,								\
	    { 0x00000060, 0x00000060, 0x00000060, 0x00000060, 0x00000060 } },	\
	{ 7, 0x989c,								\
	    { 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0 } },	\
	{ 7, 0x989c,								\
	    { 0x00000022, 0x00000022, 0x00000022, 0x00000022, 0x00000022 } },	\
	{ 7, 0x989c,								\
	    { 0x00000092, 0x00000092, 0x00000092, 0x00000092, 0x00000092 } },	\
	{ 7, 0x989c,								\
	    { 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4 } },	\
	{ 7, 0x989c,								\
	    { 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc } },	\
	{ 7, 0x989c,								\
	    { 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c } },	\
	{ 7, 0x98c4,								\
	    { 0x00000003, 0x00000003, 0x00000003, 0x00000003, 0x00000003 } },	\
}

#define AR5K_AR5112A_INI_RF	{						\
	{ 1, 0x98d4,								\
	    { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },	\
	{ 2, 0x98d0,								\
	    { 0x03060408, 0x03070408, 0x03060408, 0x03060408, 0x03070408 } },	\
	{ 3, 0x98dc,								\
	    { 0x00a0c0c0, 0x00a0c0c0, 0x00e0c0c0, 0x00e0c0c0, 0x00e0c0c0 } },	\
	{ 6, 0x989c,								\
	    { 0x0f000000, 0x0f000000, 0x0f000000, 0x0f000000, 0x0f000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00800000, 0x00800000, 0x00800000, 0x00800000, 0x00800000 } },	\
	{ 6, 0x989c,								\
	    { 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00010000, 0x00010000, 0x00010000, 0x00010000, 0x00010000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00180000, 0x00180000, 0x00180000, 0x00180000, 0x00180000 } },	\
	{ 6, 0x989c,								\
	    { 0x00600000, 0x00600000, 0x006e0000, 0x006e0000, 0x006e0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00c70000, 0x00c70000, 0x00c70000, 0x00c70000, 0x00c70000 } },	\
	{ 6, 0x989c,								\
	    { 0x004b0000, 0x004b0000, 0x004b0000, 0x004b0000, 0x004b0000 } },	\
	{ 6, 0x989c,								\
	    { 0x04480000, 0x04480000, 0x04480000, 0x04480000, 0x04480000 } },	\
	{ 6, 0x989c,								\
	    { 0x00220000, 0x00220000, 0x00220000, 0x00220000, 0x00220000 } },	\
	{ 6, 0x989c,								\
	    { 0x00e40000, 0x00e40000, 0x00e40000, 0x00e40000, 0x00e40000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00fc0000, 0x00fc0000, 0x00fc0000, 0x00fc0000, 0x00fc0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x043f0000, 0x043f0000, 0x043f0000, 0x043f0000, 0x043f0000 } },	\
	{ 6, 0x989c,								\
	    { 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000, 0x000c0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00190000, 0x00190000, 0x00190000, 0x00190000, 0x00190000 } },	\
	{ 6, 0x989c,								\
	    { 0x00240000, 0x00240000, 0x00240000, 0x00240000, 0x00240000 } },	\
	{ 6, 0x989c,								\
	    { 0x00b40000, 0x00b40000, 0x00b40000, 0x00b40000, 0x00b40000 } },	\
	{ 6, 0x989c,								\
	    { 0x00990000, 0x00990000, 0x00990000, 0x00990000, 0x00990000 } },	\
	{ 6, 0x989c,								\
	    { 0x00500000, 0x00500000, 0x00500000, 0x00500000, 0x00500000 } },	\
	{ 6, 0x989c,								\
	    { 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000, 0x002a0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00120000, 0x00120000, 0x00120000, 0x00120000, 0x00120000 } },	\
	{ 6, 0x989c,								\
	    { 0xc0320000, 0xc0320000, 0xc0320000, 0xc0320000, 0xc0320000 } },	\
	{ 6, 0x989c,								\
	    { 0x01740000, 0x01740000, 0x01740000, 0x01740000, 0x01740000 } },	\
	{ 6, 0x989c,								\
	    { 0x00110000, 0x00110000, 0x00110000, 0x00110000, 0x00110000 } },	\
	{ 6, 0x989c,								\
	    { 0x86280000, 0x86280000, 0x86280000, 0x86280000, 0x86280000 } },	\
	{ 6, 0x989c,								\
	    { 0x31840000, 0x31840000, 0x31840000, 0x31840000, 0x31840000 } },	\
	{ 6, 0x989c,								\
	    { 0x00020080, 0x00020080, 0x00020080, 0x00020080, 0x00020080 } },	\
	{ 6, 0x989c,								\
	    { 0x00080009, 0x00080009, 0x00080009, 0x00080009, 0x00080009 } },	\
	{ 6, 0x989c,								\
	    { 0x00000003, 0x00000003, 0x00000003, 0x00000003, 0x00000003 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x000000b2, 0x000000b2, 0x000000b2, 0x000000b2, 0x000000b2 } },	\
	{ 6, 0x989c,								\
	    { 0x00b02084, 0x00b02084, 0x00b02084, 0x00b02084, 0x00b02084 } },	\
	{ 6, 0x989c,								\
	    { 0x004125a4, 0x004125a4, 0x004125a4, 0x004125a4, 0x004125a4 } },	\
	{ 6, 0x989c,								\
	    { 0x00119220, 0x00119220, 0x00119220, 0x00119220, 0x00119220 } },	\
	{ 6, 0x989c,								\
	    { 0x001a4800, 0x001a4800, 0x001a4800, 0x001a4800, 0x001a4800 } },	\
	{ 6, 0x98d8,								\
	    { 0x000b0230, 0x000b0230, 0x000b0230, 0x000b0230, 0x000b0230 } },	\
	{ 7, 0x989c,								\
	    { 0x00000094, 0x00000094, 0x00000094, 0x00000094, 0x00000094 } },	\
	{ 7, 0x989c,								\
	    { 0x00000091, 0x00000091, 0x00000091, 0x00000091, 0x00000091 } },	\
	{ 7, 0x989c,								\
	    { 0x00000012, 0x00000012, 0x00000012, 0x00000012, 0x00000012 } },	\
	{ 7, 0x989c,								\
	    { 0x00000080, 0x00000080, 0x00000080, 0x00000080, 0x00000080 } },	\
	{ 7, 0x989c,								\
	    { 0x000000d9, 0x000000d9, 0x000000d9, 0x000000d9, 0x000000d9 } },	\
	{ 7, 0x989c,								\
	    { 0x00000060, 0x00000060, 0x00000060, 0x00000060, 0x00000060 } },	\
	{ 7, 0x989c,								\
	    { 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0, 0x000000f0 } },	\
	{ 7, 0x989c,								\
	    { 0x000000a2, 0x000000a2, 0x000000a2, 0x000000a2, 0x000000a2 } },	\
	{ 7, 0x989c,								\
	    { 0x00000052, 0x00000052, 0x00000052, 0x00000052, 0x00000052 } },	\
	{ 7, 0x989c,								\
	    { 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4, 0x000000d4 } },	\
	{ 7, 0x989c,								\
	    { 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc, 0x000014cc } },	\
	{ 7, 0x989c,								\
	    { 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c, 0x0000048c } },	\
	{ 7, 0x98c4,								\
	    { 0x00000003, 0x00000003, 0x00000003, 0x00000003, 0x00000003 } },	\
}

#define AR5K_AR5413_INI_RF	{						\
	{ 1, 0x98d4,								\
	    { 0x00000020, 0x00000020, 0x00000020, 0x00000020, 0x00000020 } },	\
	{ 2, 0x98d0,								\
	    { 0x00000008, 0x00000008, 0x00000008, 0x00000008, 0x00000008 } },	\
	{ 3, 0x98dc,								\
	    { 0x00a000c0, 0x00a000c0, 0x00e000c0, 0x00e000c0, 0x00e000c0 } },	\
	{ 6, 0x989c,								\
	    { 0x33000000, 0x33000000, 0x33000000, 0x33000000, 0x33000000 } },	\
	{ 6, 0x989c,								\
	    { 0x01000000, 0x01000000, 0x01000000, 0x01000000, 0x01000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x1f000000, 0x1f000000, 0x1f000000, 0x1f000000, 0x1f000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00b80000, 0x00b80000, 0x00b80000, 0x00b80000, 0x00b80000 } },	\
	{ 6, 0x989c,								\
	    { 0x00b70000, 0x00b70000, 0x00b70000, 0x00b70000, 0x00b70000 } },	\
	{ 6, 0x989c,								\
	    { 0x00840000, 0x00840000, 0x00840000, 0x00840000, 0x00840000 } },	\
	{ 6, 0x989c,								\
	    { 0x00980000, 0x00980000, 0x00980000, 0x00980000, 0x00980000 } },	\
	{ 6, 0x989c,								\
	    { 0x00c00000, 0x00c00000, 0x00c00000, 0x00c00000, 0x00c00000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000, 0x00ff0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00d70000, 0x00d70000, 0x00d70000, 0x00d70000, 0x00d70000 } },	\
	{ 6, 0x989c,								\
	    { 0x00610000, 0x00610000, 0x00610000, 0x00610000, 0x00610000 } },	\
	{ 6, 0x989c,								\
	    { 0x00fe0000, 0x00fe0000, 0x00fe0000, 0x00fe0000, 0x00fe0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00de0000, 0x00de0000, 0x00de0000, 0x00de0000, 0x00de0000 } },	\
	{ 6, 0x989c,								\
	    { 0x007f0000, 0x007f0000, 0x007f0000, 0x007f0000, 0x007f0000 } },	\
	{ 6, 0x989c,								\
	    { 0x043d0000, 0x043d0000, 0x043d0000, 0x043d0000, 0x043d0000 } },	\
	{ 6, 0x989c,								\
	    { 0x00770000, 0x00770000, 0x00770000, 0x00770000, 0x00770000 } },	\
	{ 6, 0x989c,								\
	    { 0x00440000, 0x00440000, 0x00440000, 0x00440000, 0x00440000 } },	\
	{ 6, 0x989c,								\
	    { 0x00980000, 0x00980000, 0x00980000, 0x00980000, 0x00980000 } },	\
	{ 6, 0x989c,								\
	    { 0x00100080, 0x00100080, 0x00100080, 0x00100080, 0x00100080 } },	\
	{ 6, 0x989c,								\
	    { 0x0005c034, 0x0005c034, 0x0005c034, 0x0005c034, 0x0005c034 } },	\
	{ 6, 0x989c,								\
	    { 0x003100f0, 0x003100f0, 0x003100f0, 0x003100f0, 0x003100f0 } },	\
	{ 6, 0x989c,								\
	    { 0x000c011f, 0x000c011f, 0x000c011f, 0x000c011f, 0x000c011f } },	\
	{ 6, 0x989c,								\
	    { 0x00510040, 0x00510040, 0x005100a0, 0x005100a0, 0x005100a0 } },	\
	{ 6, 0x989c,								\
	    { 0x0050006a, 0x0050006a, 0x005000dd, 0x005000dd, 0x005000dd } },	\
	{ 6, 0x989c,								\
	    { 0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x00004044, 0x00004044, 0x00004044, 0x00004044, 0x00004044 } },	\
	{ 6, 0x989c,								\
	    { 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 } },	\
	{ 6, 0x989c,								\
	    { 0x000060c0, 0x000060c0, 0x000060c0, 0x000060c0, 0x000060c0 } },	\
	{ 6, 0x989c,								\
	    { 0x00002c00, 0x00002c00, 0x00003600, 0x00003600, 0x00003600 } },	\
	{ 6, 0x98c8,								\
	    { 0x00000403, 0x00000403, 0x00040403, 0x00040403, 0x00040403 } },	\
	{ 7, 0x989c,								\
	    { 0x00006400, 0x00006400, 0x00006400, 0x00006400, 0x00006400 } },	\
	{ 7, 0x989c,								\
	    { 0x00000800, 0x00000800, 0x00000800, 0x00000800, 0x00000800 } },	\
	{ 7, 0x98cc,								\
	    { 0x0000000e, 0x0000000e, 0x0000000e, 0x0000000e, 0x0000000e } },	\
}

#define AR5K_AR2413_INI_RF	{						\
	{ 1, 0x98d4, { 0, 0, 0x00000020, 0x00000020, 0x00000020 } },		\
	{ 2, 0x98d0, { 0, 0, 0x02001408, 0x02001408, 0x02001408 } },		\
	{ 3, 0x98dc, { 0, 0, 0x00e020c0, 0x00e020c0, 0x00e020c0 } },		\
	{ 6, 0x989c, { 0, 0, 0xf0000000, 0xf0000000, 0xf0000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x03000000, 0x03000000, 0x03000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x40400000, 0x40400000, 0x40400000 } },		\
	{ 6, 0x989c, { 0, 0, 0x65050000, 0x65050000, 0x65050000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00420000, 0x00420000, 0x00420000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00b50000, 0x00b50000, 0x00b50000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00030000, 0x00030000, 0x00030000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00f70000, 0x00f70000, 0x00f70000 } },		\
	{ 6, 0x989c, { 0, 0, 0x009d0000, 0x009d0000, 0x009d0000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00220000, 0x00220000, 0x00220000 } },		\
	{ 6, 0x989c, { 0, 0, 0x04220000, 0x04220000, 0x04220000 } },		\
	{ 6, 0x989c, { 0, 0, 0x00230018, 0x00230018, 0x00230018 } },		\
	{ 6, 0x989c, { 0, 0, 0x00280050, 0x00280050, 0x00280050 } },		\
	{ 6, 0x989c, { 0, 0, 0x005000c3, 0x005000c3, 0x005000c3 } },		\
	{ 6, 0x989c, { 0, 0, 0x0004007f, 0x0004007f, 0x0004007f } },		\
	{ 6, 0x989c, { 0, 0, 0x00000458, 0x00000458, 0x00000458 } },		\
	{ 6, 0x989c, { 0, 0, 0x00000000, 0x00000000, 0x00000000 } },		\
	{ 6, 0x989c, { 0, 0, 0x0000c000, 0x0000c000, 0x0000c000 } },		\
	{ 6, 0x98d8, { 0, 0, 0x00400230, 0x00400230, 0x00400230 } },		\
	{ 7, 0x989c, { 0, 0, 0x00006400, 0x00006400, 0x00006400 } },		\
	{ 7, 0x989c, { 0, 0, 0x00000800, 0x00000800, 0x00000800 } },		\
	{ 7, 0x98cc, { 0, 0, 0x0000000e, 0x0000000e, 0x0000000e } },		\
}

#define AR5K_AR2425_INI_RF	{						\
	{ 1, 0x98d4, { 0, 0, 0, 0x00000020, 0x00000020 } },			\
	{ 2, 0x98d0, { 0, 0, 0, 0x02001408, 0x02001408 } },			\
	{ 3, 0x98dc, { 0, 0, 0, 0x00e020c0, 0x00e020c0 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x10000000, 0x10000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x002a0000, 0x002a0000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00100000, 0x00100000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00020000, 0x00020000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00730000, 0x00730000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00f80000, 0x00f80000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00e70000, 0x00e70000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00140000, 0x00140000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00910040, 0x00910040 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x0007001a, 0x0007001a } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00410000, 0x00410000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00810060, 0x00810060 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00020803, 0x00020803 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00000000, 0x00000000 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00001660, 0x00001660 } },			\
	{ 6, 0x989c, { 0, 0, 0, 0x00001688, 0x00001688 } },			\
	{ 6, 0x98c4, { 0, 0, 0, 0x00000001, 0x00000001 } },			\
	{ 7, 0x989c, { 0, 0, 0, 0x00006400, 0x00006400 } },			\
	{ 7, 0x989c, { 0, 0, 0, 0x00000800, 0x00000800 } },			\
	{ 7, 0x98cc, { 0, 0, 0, 0x0000000e, 0x0000000e } }			\
}

struct ar5k_ini_rfgain {
	u_int16_t	rfg_register;
	u_int32_t	rfg_value[2];

#define AR5K_INI_RFGAIN_5GHZ	0
#define AR5K_INI_RFGAIN_2GHZ	1
#define AR5K_INI_RFGAIN(_n)	(0x9a00 + ((_n) << 2))
};

#define AR5K_AR5111_INI_RFGAIN	{				\
	{ AR5K_INI_RFGAIN(0),	{ 0x000001a9, 0x00000000 } },	\
	{ AR5K_INI_RFGAIN(1),	{ 0x000001e9, 0x00000040 } },	\
	{ AR5K_INI_RFGAIN(2),	{ 0x00000029, 0x00000080 } },	\
	{ AR5K_INI_RFGAIN(3),	{ 0x00000069, 0x00000150 } },	\
	{ AR5K_INI_RFGAIN(4),	{ 0x00000199, 0x00000190 } },	\
	{ AR5K_INI_RFGAIN(5),	{ 0x000001d9, 0x000001d0 } },	\
	{ AR5K_INI_RFGAIN(6),	{ 0x00000019, 0x00000010 } },	\
	{ AR5K_INI_RFGAIN(7),	{ 0x00000059, 0x00000044 } },	\
	{ AR5K_INI_RFGAIN(8),	{ 0x00000099, 0x00000084 } },	\
	{ AR5K_INI_RFGAIN(9),	{ 0x000001a5, 0x00000148 } },	\
	{ AR5K_INI_RFGAIN(10),	{ 0x000001e5, 0x00000188 } },	\
	{ AR5K_INI_RFGAIN(11),	{ 0x00000025, 0x000001c8 } },	\
	{ AR5K_INI_RFGAIN(12),	{ 0x000001c8, 0x00000014 } },	\
	{ AR5K_INI_RFGAIN(13),	{ 0x00000008, 0x00000042 } },	\
	{ AR5K_INI_RFGAIN(14),	{ 0x00000048, 0x00000082 } },	\
	{ AR5K_INI_RFGAIN(15),	{ 0x00000088, 0x00000178 } },	\
	{ AR5K_INI_RFGAIN(16),	{ 0x00000198, 0x000001b8 } },	\
	{ AR5K_INI_RFGAIN(17),	{ 0x000001d8, 0x000001f8 } },	\
	{ AR5K_INI_RFGAIN(18),	{ 0x00000018, 0x00000012 } },	\
	{ AR5K_INI_RFGAIN(19),	{ 0x00000058, 0x00000052 } },	\
	{ AR5K_INI_RFGAIN(20),	{ 0x00000098, 0x00000092 } },	\
	{ AR5K_INI_RFGAIN(21),	{ 0x000001a4, 0x0000017c } },	\
	{ AR5K_INI_RFGAIN(22),	{ 0x000001e4, 0x000001bc } },	\
	{ AR5K_INI_RFGAIN(23),	{ 0x00000024, 0x000001fc } },	\
	{ AR5K_INI_RFGAIN(24),	{ 0x00000064, 0x0000000a } },	\
	{ AR5K_INI_RFGAIN(25),	{ 0x000000a4, 0x0000004a } },	\
	{ AR5K_INI_RFGAIN(26),	{ 0x000000e4, 0x0000008a } },	\
	{ AR5K_INI_RFGAIN(27),	{ 0x0000010a, 0x0000015a } },	\
	{ AR5K_INI_RFGAIN(28),	{ 0x0000014a, 0x0000019a } },	\
	{ AR5K_INI_RFGAIN(29),	{ 0x0000018a, 0x000001da } },	\
	{ AR5K_INI_RFGAIN(30),	{ 0x000001ca, 0x0000000e } },	\
	{ AR5K_INI_RFGAIN(31),	{ 0x0000000a, 0x0000004e } },	\
	{ AR5K_INI_RFGAIN(32),	{ 0x0000004a, 0x0000008e } },	\
	{ AR5K_INI_RFGAIN(33),	{ 0x0000008a, 0x0000015e } },	\
	{ AR5K_INI_RFGAIN(34),	{ 0x000001ba, 0x0000019e } },	\
	{ AR5K_INI_RFGAIN(35),	{ 0x000001fa, 0x000001de } },	\
	{ AR5K_INI_RFGAIN(36),	{ 0x0000003a, 0x00000009 } },	\
	{ AR5K_INI_RFGAIN(37),	{ 0x0000007a, 0x00000049 } },	\
	{ AR5K_INI_RFGAIN(38),	{ 0x00000186, 0x00000089 } },	\
	{ AR5K_INI_RFGAIN(39),	{ 0x000001c6, 0x00000179 } },	\
	{ AR5K_INI_RFGAIN(40),	{ 0x00000006, 0x000001b9 } },	\
	{ AR5K_INI_RFGAIN(41),	{ 0x00000046, 0x000001f9 } },	\
	{ AR5K_INI_RFGAIN(42),	{ 0x00000086, 0x00000039 } },	\
	{ AR5K_INI_RFGAIN(43),	{ 0x000000c6, 0x00000079 } },	\
	{ AR5K_INI_RFGAIN(44),	{ 0x000000c6, 0x000000b9 } },	\
	{ AR5K_INI_RFGAIN(45),	{ 0x000000c6, 0x000001bd } },	\
	{ AR5K_INI_RFGAIN(46),	{ 0x000000c6, 0x000001fd } },	\
	{ AR5K_INI_RFGAIN(47),	{ 0x000000c6, 0x0000003d } },	\
	{ AR5K_INI_RFGAIN(48),	{ 0x000000c6, 0x0000007d } },	\
	{ AR5K_INI_RFGAIN(49),	{ 0x000000c6, 0x000000bd } },	\
	{ AR5K_INI_RFGAIN(50),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(51),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(52),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(53),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(54),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(55),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(56),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(57),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(58),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(59),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(60),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(61),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(62),	{ 0x000000c6, 0x000000fd } },	\
	{ AR5K_INI_RFGAIN(63),	{ 0x000000c6, 0x000000fd } }	\
}

#define AR5K_AR5112_INI_RFGAIN	{				\
	{ AR5K_INI_RFGAIN(0),	{ 0x00000007, 0x00000007 } },	\
	{ AR5K_INI_RFGAIN(1),	{ 0x00000047, 0x00000047 } },	\
	{ AR5K_INI_RFGAIN(2),	{ 0x00000087, 0x00000087 } },	\
	{ AR5K_INI_RFGAIN(3),	{ 0x000001a0, 0x000001a0 } },	\
	{ AR5K_INI_RFGAIN(4),	{ 0x000001e0, 0x000001e0 } },	\
	{ AR5K_INI_RFGAIN(5),	{ 0x00000020, 0x00000020 } },	\
	{ AR5K_INI_RFGAIN(6),	{ 0x00000060, 0x00000060 } },	\
	{ AR5K_INI_RFGAIN(7),	{ 0x000001a1, 0x000001a1 } },	\
	{ AR5K_INI_RFGAIN(8),	{ 0x000001e1, 0x000001e1 } },	\
	{ AR5K_INI_RFGAIN(9),	{ 0x00000021, 0x00000021 } },	\
	{ AR5K_INI_RFGAIN(10),	{ 0x00000061, 0x00000061 } },	\
	{ AR5K_INI_RFGAIN(11),	{ 0x00000162, 0x00000162 } },	\
	{ AR5K_INI_RFGAIN(12),	{ 0x000001a2, 0x000001a2 } },	\
	{ AR5K_INI_RFGAIN(13),	{ 0x000001e2, 0x000001e2 } },	\
	{ AR5K_INI_RFGAIN(14),	{ 0x00000022, 0x00000022 } },	\
	{ AR5K_INI_RFGAIN(15),	{ 0x00000062, 0x00000062 } },	\
	{ AR5K_INI_RFGAIN(16),	{ 0x00000163, 0x00000163 } },	\
	{ AR5K_INI_RFGAIN(17),	{ 0x000001a3, 0x000001a3 } },	\
	{ AR5K_INI_RFGAIN(18),	{ 0x000001e3, 0x000001e3 } },	\
	{ AR5K_INI_RFGAIN(19),	{ 0x00000023, 0x00000023 } },	\
	{ AR5K_INI_RFGAIN(20),	{ 0x00000063, 0x00000063 } },	\
	{ AR5K_INI_RFGAIN(21),	{ 0x00000184, 0x00000184 } },	\
	{ AR5K_INI_RFGAIN(22),	{ 0x000001c4, 0x000001c4 } },	\
	{ AR5K_INI_RFGAIN(23),	{ 0x00000004, 0x00000004 } },	\
	{ AR5K_INI_RFGAIN(24),	{ 0x000001ea, 0x0000000b } },	\
	{ AR5K_INI_RFGAIN(25),	{ 0x0000002a, 0x0000004b } },	\
	{ AR5K_INI_RFGAIN(26),	{ 0x0000006a, 0x0000008b } },	\
	{ AR5K_INI_RFGAIN(27),	{ 0x000000aa, 0x000001ac } },	\
	{ AR5K_INI_RFGAIN(28),	{ 0x000001ab, 0x000001ec } },	\
	{ AR5K_INI_RFGAIN(29),	{ 0x000001eb, 0x0000002c } },	\
	{ AR5K_INI_RFGAIN(30),	{ 0x0000002b, 0x00000012 } },	\
	{ AR5K_INI_RFGAIN(31),	{ 0x0000006b, 0x00000052 } },	\
	{ AR5K_INI_RFGAIN(32),	{ 0x000000ab, 0x00000092 } },	\
	{ AR5K_INI_RFGAIN(33),	{ 0x000001ac, 0x00000193 } },	\
	{ AR5K_INI_RFGAIN(34),	{ 0x000001ec, 0x000001d3 } },	\
	{ AR5K_INI_RFGAIN(35),	{ 0x0000002c, 0x00000013 } },	\
	{ AR5K_INI_RFGAIN(36),	{ 0x0000003a, 0x00000053 } },	\
	{ AR5K_INI_RFGAIN(37),	{ 0x0000007a, 0x00000093 } },	\
	{ AR5K_INI_RFGAIN(38),	{ 0x000000ba, 0x00000194 } },	\
	{ AR5K_INI_RFGAIN(39),	{ 0x000001bb, 0x000001d4 } },	\
	{ AR5K_INI_RFGAIN(40),	{ 0x000001fb, 0x00000014 } },	\
	{ AR5K_INI_RFGAIN(41),	{ 0x0000003b, 0x0000003a } },	\
	{ AR5K_INI_RFGAIN(42),	{ 0x0000007b, 0x0000007a } },	\
	{ AR5K_INI_RFGAIN(43),	{ 0x000000bb, 0x000000ba } },	\
	{ AR5K_INI_RFGAIN(44),	{ 0x000001bc, 0x000001bb } },	\
	{ AR5K_INI_RFGAIN(45),	{ 0x000001fc, 0x000001fb } },	\
	{ AR5K_INI_RFGAIN(46),	{ 0x0000003c, 0x0000003b } },	\
	{ AR5K_INI_RFGAIN(47),	{ 0x0000007c, 0x0000007b } },	\
	{ AR5K_INI_RFGAIN(48),	{ 0x000000bc, 0x000000bb } },	\
	{ AR5K_INI_RFGAIN(49),	{ 0x000000fc, 0x000001bc } },	\
	{ AR5K_INI_RFGAIN(50),	{ 0x000000fc, 0x000001fc } },	\
	{ AR5K_INI_RFGAIN(51),	{ 0x000000fc, 0x0000003c } },	\
	{ AR5K_INI_RFGAIN(52),	{ 0x000000fc, 0x0000007c } },	\
	{ AR5K_INI_RFGAIN(53),	{ 0x000000fc, 0x000000bc } },	\
	{ AR5K_INI_RFGAIN(54),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(55),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(56),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(57),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(58),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(59),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(60),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(61),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(62),	{ 0x000000fc, 0x000000fc } },	\
	{ AR5K_INI_RFGAIN(63),	{ 0x000000fc, 0x000000fc } },	\
}

#define AR5K_AR5413_INI_RFGAIN	{				\
	{ AR5K_INI_RFGAIN(0),	{ 0x00000000, 0x00000000 } },	\
	{ AR5K_INI_RFGAIN(1),	{ 0x00000040, 0x00000040 } },	\
	{ AR5K_INI_RFGAIN(2),	{ 0x00000080, 0x00000080 } },	\
	{ AR5K_INI_RFGAIN(3),	{ 0x000001a1, 0x00000161 } },	\
	{ AR5K_INI_RFGAIN(4),	{ 0x000001e1, 0x000001a1 } },	\
	{ AR5K_INI_RFGAIN(5),	{ 0x00000021, 0x000001e1 } },	\
	{ AR5K_INI_RFGAIN(6),	{ 0x00000061, 0x00000021 } },	\
	{ AR5K_INI_RFGAIN(7),	{ 0x00000188, 0x00000061 } },	\
	{ AR5K_INI_RFGAIN(8),	{ 0x000001c8, 0x00000188 } },	\
	{ AR5K_INI_RFGAIN(9),	{ 0x00000008, 0x000001c8 } },	\
	{ AR5K_INI_RFGAIN(10),	{ 0x00000048, 0x00000008 } },	\
	{ AR5K_INI_RFGAIN(11),	{ 0x00000088, 0x00000048 } },	\
	{ AR5K_INI_RFGAIN(12),	{ 0x000001a9, 0x00000088 } },	\
	{ AR5K_INI_RFGAIN(13),	{ 0x000001e9, 0x00000169 } },	\
	{ AR5K_INI_RFGAIN(14),	{ 0x00000029, 0x000001a9 } },	\
	{ AR5K_INI_RFGAIN(15),	{ 0x00000069, 0x000001e9 } },	\
	{ AR5K_INI_RFGAIN(16),	{ 0x000001d0, 0x00000029 } },	\
	{ AR5K_INI_RFGAIN(17),	{ 0x00000010, 0x00000069 } },	\
	{ AR5K_INI_RFGAIN(18),	{ 0x00000050, 0x00000190 } },	\
	{ AR5K_INI_RFGAIN(19),	{ 0x00000090, 0x000001d0 } },	\
	{ AR5K_INI_RFGAIN(20),	{ 0x000001b1, 0x00000010 } },	\
	{ AR5K_INI_RFGAIN(21),	{ 0x000001f1, 0x00000050 } },	\
	{ AR5K_INI_RFGAIN(22),	{ 0x00000031, 0x00000090 } },	\
	{ AR5K_INI_RFGAIN(23),	{ 0x00000071, 0x00000171 } },	\
	{ AR5K_INI_RFGAIN(24),	{ 0x000001b8, 0x000001b1 } },	\
	{ AR5K_INI_RFGAIN(25),	{ 0x000001f8, 0x000001f1 } },	\
	{ AR5K_INI_RFGAIN(26),	{ 0x00000038, 0x00000031 } },	\
	{ AR5K_INI_RFGAIN(27),	{ 0x00000078, 0x00000071 } },	\
	{ AR5K_INI_RFGAIN(28),	{ 0x00000199, 0x00000198 } },	\
	{ AR5K_INI_RFGAIN(29),	{ 0x000001d9, 0x000001d8 } },	\
	{ AR5K_INI_RFGAIN(30),	{ 0x00000019, 0x00000018 } },	\
	{ AR5K_INI_RFGAIN(31),	{ 0x00000059, 0x00000058 } },	\
	{ AR5K_INI_RFGAIN(32),	{ 0x00000099, 0x00000098 } },	\
	{ AR5K_INI_RFGAIN(33),	{ 0x000000d9, 0x00000179 } },	\
	{ AR5K_INI_RFGAIN(34),	{ 0x000000f9, 0x000001b9 } },	\
	{ AR5K_INI_RFGAIN(35),	{ 0x000000f9, 0x000001f9 } },	\
	{ AR5K_INI_RFGAIN(36),	{ 0x000000f9, 0x00000039 } },	\
	{ AR5K_INI_RFGAIN(37),	{ 0x000000f9, 0x00000079 } },	\
	{ AR5K_INI_RFGAIN(38),	{ 0x000000f9, 0x000000b9 } },	\
	{ AR5K_INI_RFGAIN(39),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(40),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(41),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(42),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(43),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(44),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(45),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(46),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(47),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(48),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(49),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(50),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(51),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(52),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(53),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(54),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(55),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(56),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(57),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(58),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(59),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(60),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(61),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(62),	{ 0x000000f9, 0x000000f9 } },	\
	{ AR5K_INI_RFGAIN(63),	{ 0x000000f9, 0x000000f9 } }	\
}

#define AR5K_AR2413_INI_RFGAIN	{				\
	{ AR5K_INI_RFGAIN(0),	{ 0, 0x00000000 } },		\
	{ AR5K_INI_RFGAIN(1),	{ 0, 0x00000040 } },		\
	{ AR5K_INI_RFGAIN(2),	{ 0, 0x00000080 } },		\
	{ AR5K_INI_RFGAIN(3),	{ 0, 0x00000181 } },		\
	{ AR5K_INI_RFGAIN(4),	{ 0, 0x000001c1 } },		\
	{ AR5K_INI_RFGAIN(5),	{ 0, 0x00000001 } },		\
	{ AR5K_INI_RFGAIN(6),	{ 0, 0x00000041 } },		\
	{ AR5K_INI_RFGAIN(7),	{ 0, 0x00000081 } },		\
	{ AR5K_INI_RFGAIN(8),	{ 0, 0x00000168 } },		\
	{ AR5K_INI_RFGAIN(9),	{ 0, 0x000001a8 } },		\
	{ AR5K_INI_RFGAIN(10),	{ 0, 0x000001e8 } },		\
	{ AR5K_INI_RFGAIN(11),	{ 0, 0x00000028 } },		\
	{ AR5K_INI_RFGAIN(12),	{ 0, 0x00000068 } },		\
	{ AR5K_INI_RFGAIN(13),	{ 0, 0x00000189 } },		\
	{ AR5K_INI_RFGAIN(14),	{ 0, 0x000001c9 } },		\
	{ AR5K_INI_RFGAIN(15),	{ 0, 0x00000009 } },		\
	{ AR5K_INI_RFGAIN(16),	{ 0, 0x00000049 } },		\
	{ AR5K_INI_RFGAIN(17),	{ 0, 0x00000089 } },		\
	{ AR5K_INI_RFGAIN(18),	{ 0, 0x00000190 } },		\
	{ AR5K_INI_RFGAIN(19),	{ 0, 0x000001d0 } },		\
	{ AR5K_INI_RFGAIN(20),	{ 0, 0x00000010 } },		\
	{ AR5K_INI_RFGAIN(21),	{ 0, 0x00000050 } },		\
	{ AR5K_INI_RFGAIN(22),	{ 0, 0x00000090 } },		\
	{ AR5K_INI_RFGAIN(23),	{ 0, 0x00000191 } },		\
	{ AR5K_INI_RFGAIN(24),	{ 0, 0x000001d1 } },		\
	{ AR5K_INI_RFGAIN(25),	{ 0, 0x00000011 } },		\
	{ AR5K_INI_RFGAIN(26),	{ 0, 0x00000051 } },		\
	{ AR5K_INI_RFGAIN(27),	{ 0, 0x00000091 } },		\
	{ AR5K_INI_RFGAIN(28),	{ 0, 0x00000178 } },		\
	{ AR5K_INI_RFGAIN(29),	{ 0, 0x000001b8 } },		\
	{ AR5K_INI_RFGAIN(30),	{ 0, 0x000001f8 } },		\
	{ AR5K_INI_RFGAIN(31),	{ 0, 0x00000038 } },		\
	{ AR5K_INI_RFGAIN(32),	{ 0, 0x00000078 } },		\
	{ AR5K_INI_RFGAIN(33),	{ 0, 0x00000199 } },		\
	{ AR5K_INI_RFGAIN(34),	{ 0, 0x000001d9 } },		\
	{ AR5K_INI_RFGAIN(35),	{ 0, 0x00000019 } },		\
	{ AR5K_INI_RFGAIN(36),	{ 0, 0x00000059 } },		\
	{ AR5K_INI_RFGAIN(37),	{ 0, 0x00000099 } },		\
	{ AR5K_INI_RFGAIN(38),	{ 0, 0x000000d9 } },		\
	{ AR5K_INI_RFGAIN(39),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(40),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(41),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(42),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(43),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(44),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(45),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(46),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(47),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(48),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(49),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(50),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(51),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(52),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(53),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(54),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(55),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(56),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(57),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(58),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(59),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(60),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(61),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(62),	{ 0, 0x000000f9 } },		\
	{ AR5K_INI_RFGAIN(63),	{ 0, 0x000000f9 } },		\
}

/*
 * Prototypes
 */

__BEGIN_DECLS

const char		*ath_hal_probe(u_int16_t, u_int16_t);

struct ath_hal		*ath_hal_attach(u_int16_t, void *, bus_space_tag_t,
    bus_space_handle_t, u_int, HAL_STATUS *);

u_int16_t		 ath_hal_computetxtime(struct ath_hal *,
    const HAL_RATE_TABLE *, u_int32_t, u_int16_t, HAL_BOOL);

HAL_BOOL		 ath_hal_init_channels(struct ath_hal *, HAL_CHANNEL *,
    u_int, u_int *, u_int16_t, HAL_BOOL, HAL_BOOL);

const char		*ar5k_printver(enum ar5k_srev_type, u_int32_t);
void			 ar5k_radar_alert(struct ath_hal *);
ieee80211_regdomain_t	 ar5k_regdomain_to_ieee(u_int16_t);
u_int16_t		 ar5k_regdomain_from_ieee(ieee80211_regdomain_t);
u_int16_t		 ar5k_get_regdomain(struct ath_hal *);

u_int32_t		 ar5k_bitswap(u_int32_t, u_int);
u_int			 ar5k_clocktoh(u_int);
u_int			 ar5k_htoclock(u_int);
void			 ar5k_rt_copy(HAL_RATE_TABLE *, const HAL_RATE_TABLE *);

HAL_BOOL		 ar5k_register_timeout(struct ath_hal *, u_int32_t,
    u_int32_t, u_int32_t, HAL_BOOL);

int			 ar5k_eeprom_init(struct ath_hal *);
int			 ar5k_eeprom_read_mac(struct ath_hal *, u_int8_t *);
HAL_BOOL		 ar5k_eeprom_regulation_domain(struct ath_hal *,
    HAL_BOOL, ieee80211_regdomain_t *);

HAL_BOOL		 ar5k_channel(struct ath_hal *, HAL_CHANNEL *);
HAL_BOOL		 ar5k_rfregs(struct ath_hal *, HAL_CHANNEL *, u_int);
u_int32_t		 ar5k_rfregs_gainf_corr(struct ath_hal *);
HAL_BOOL		 ar5k_rfregs_gain_readback(struct ath_hal *);
int32_t			 ar5k_rfregs_gain_adjust(struct ath_hal *);
HAL_BOOL		 ar5k_rfgain(struct ath_hal *, u_int);

void			 ar5k_txpower_table(struct ath_hal *, HAL_CHANNEL *,
    int16_t);

void			 ar5k_write_ini(struct ath_hal *,
			    const struct ar5k_ini *, size_t, HAL_BOOL);
void			 ar5k_write_mode(struct ath_hal *,
			    const struct ar5k_mode *, size_t, u_int);

__END_DECLS

#endif /* _AR5K_H */
