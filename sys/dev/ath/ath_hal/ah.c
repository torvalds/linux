/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * $FreeBSD$
 */
#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"
#include "ah_eeprom.h"			/* for 5ghz fast clock flag */

#include "ar5416/ar5416reg.h"		/* NB: includes ar5212reg.h */
#include "ar9003/ar9300_devid.h"

/* linker set of registered chips */
OS_SET_DECLARE(ah_chips, struct ath_hal_chip);
TAILQ_HEAD(, ath_hal_chip) ah_chip_list = TAILQ_HEAD_INITIALIZER(ah_chip_list);

int
ath_hal_add_chip(struct ath_hal_chip *ahc)
{

	TAILQ_INSERT_TAIL(&ah_chip_list, ahc, node);
	return (0);
}

int
ath_hal_remove_chip(struct ath_hal_chip *ahc)
{

	TAILQ_REMOVE(&ah_chip_list, ahc, node);
	return (0);
}

/*
 * Check the set of registered chips to see if any recognize
 * the device as one they can support.
 */
const char*
ath_hal_probe(uint16_t vendorid, uint16_t devid)
{
	struct ath_hal_chip * const *pchip;
	struct ath_hal_chip *pc;

	/* Linker set */
	OS_SET_FOREACH(pchip, ah_chips) {
		const char *name = (*pchip)->probe(vendorid, devid);
		if (name != AH_NULL)
			return name;
	}

	/* List */
	TAILQ_FOREACH(pc, &ah_chip_list, node) {
		const char *name = pc->probe(vendorid, devid);
		if (name != AH_NULL)
			return name;
	}

	return AH_NULL;
}

/*
 * Attach detects device chip revisions, initializes the hwLayer
 * function list, reads EEPROM information,
 * selects reset vectors, and performs a short self test.
 * Any failures will return an error that should cause a hardware
 * disable.
 */
struct ath_hal*
ath_hal_attach(uint16_t devid, HAL_SOFTC sc,
	HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
	HAL_OPS_CONFIG *ah_config,
	HAL_STATUS *error)
{
	struct ath_hal_chip * const *pchip;
	struct ath_hal_chip *pc;

	OS_SET_FOREACH(pchip, ah_chips) {
		struct ath_hal_chip *chip = *pchip;
		struct ath_hal *ah;

		/* XXX don't have vendorid, assume atheros one works */
		if (chip->probe(ATHEROS_VENDOR_ID, devid) == AH_NULL)
			continue;
		ah = chip->attach(devid, sc, st, sh, eepromdata, ah_config,
		    error);
		if (ah != AH_NULL) {
			/* copy back private state to public area */
			ah->ah_devid = AH_PRIVATE(ah)->ah_devid;
			ah->ah_subvendorid = AH_PRIVATE(ah)->ah_subvendorid;
			ah->ah_macVersion = AH_PRIVATE(ah)->ah_macVersion;
			ah->ah_macRev = AH_PRIVATE(ah)->ah_macRev;
			ah->ah_phyRev = AH_PRIVATE(ah)->ah_phyRev;
			ah->ah_analog5GhzRev = AH_PRIVATE(ah)->ah_analog5GhzRev;
			ah->ah_analog2GhzRev = AH_PRIVATE(ah)->ah_analog2GhzRev;
			return ah;
		}
	}

	/* List */
	TAILQ_FOREACH(pc, &ah_chip_list, node) {
		struct ath_hal_chip *chip = pc;
		struct ath_hal *ah;

		/* XXX don't have vendorid, assume atheros one works */
		if (chip->probe(ATHEROS_VENDOR_ID, devid) == AH_NULL)
			continue;
		ah = chip->attach(devid, sc, st, sh, eepromdata, ah_config,
		    error);
		if (ah != AH_NULL) {
			/* copy back private state to public area */
			ah->ah_devid = AH_PRIVATE(ah)->ah_devid;
			ah->ah_subvendorid = AH_PRIVATE(ah)->ah_subvendorid;
			ah->ah_macVersion = AH_PRIVATE(ah)->ah_macVersion;
			ah->ah_macRev = AH_PRIVATE(ah)->ah_macRev;
			ah->ah_phyRev = AH_PRIVATE(ah)->ah_phyRev;
			ah->ah_analog5GhzRev = AH_PRIVATE(ah)->ah_analog5GhzRev;
			ah->ah_analog2GhzRev = AH_PRIVATE(ah)->ah_analog2GhzRev;
			return ah;
		}
	}

	return AH_NULL;
}

const char *
ath_hal_mac_name(struct ath_hal *ah)
{
	switch (ah->ah_macVersion) {
	case AR_SREV_VERSION_CRETE:
	case AR_SREV_VERSION_MAUI_1:
		return "AR5210";
	case AR_SREV_VERSION_MAUI_2:
	case AR_SREV_VERSION_OAHU:
		return "AR5211";
	case AR_SREV_VERSION_VENICE:
		return "AR5212";
	case AR_SREV_VERSION_GRIFFIN:
		return "AR2413";
	case AR_SREV_VERSION_CONDOR:
		return "AR5424";
	case AR_SREV_VERSION_EAGLE:
		return "AR5413";
	case AR_SREV_VERSION_COBRA:
		return "AR2415";
	case AR_SREV_2425:	/* Swan */
		return "AR2425";
	case AR_SREV_2417:	/* Nala */
		return "AR2417";
	case AR_XSREV_VERSION_OWL_PCI:
		return "AR5416";
	case AR_XSREV_VERSION_OWL_PCIE:
		return "AR5418";
	case AR_XSREV_VERSION_HOWL:
		return "AR9130";
	case AR_XSREV_VERSION_SOWL:
		return "AR9160";
	case AR_XSREV_VERSION_MERLIN:
		if (AH_PRIVATE(ah)->ah_ispcie)
			return "AR9280";
		return "AR9220";
	case AR_XSREV_VERSION_KITE:
		return "AR9285";
	case AR_XSREV_VERSION_KIWI:
		if (AH_PRIVATE(ah)->ah_ispcie)
			return "AR9287";
		return "AR9227";
	case AR_SREV_VERSION_AR9380:
		if (ah->ah_macRev >= AR_SREV_REVISION_AR9580_10)
			return "AR9580";
		return "AR9380";
	case AR_SREV_VERSION_AR9460:
		return "AR9460";
	case AR_SREV_VERSION_AR9330:
		return "AR9330";
	case AR_SREV_VERSION_AR9340:
		return "AR9340";
	case AR_SREV_VERSION_QCA9550:
		return "QCA9550";
	case AR_SREV_VERSION_AR9485:
		return "AR9485";
	case AR_SREV_VERSION_QCA9565:
		return "QCA9565";
	case AR_SREV_VERSION_QCA9530:
		return "QCA9530";
	}
	return "????";
}

/*
 * Return the mask of available modes based on the hardware capabilities.
 */
u_int
ath_hal_getwirelessmodes(struct ath_hal*ah)
{
	return ath_hal_getWirelessModes(ah);
}

/* linker set of registered RF backends */
OS_SET_DECLARE(ah_rfs, struct ath_hal_rf);
TAILQ_HEAD(, ath_hal_rf) ah_rf_list = TAILQ_HEAD_INITIALIZER(ah_rf_list);

int
ath_hal_add_rf(struct ath_hal_rf *arf)
{

	TAILQ_INSERT_TAIL(&ah_rf_list, arf, node);
	return (0);
}

int
ath_hal_remove_rf(struct ath_hal_rf *arf)
{

	TAILQ_REMOVE(&ah_rf_list, arf, node);
	return (0);
}

/*
 * Check the set of registered RF backends to see if
 * any recognize the device as one they can support.
 */
struct ath_hal_rf *
ath_hal_rfprobe(struct ath_hal *ah, HAL_STATUS *ecode)
{
	struct ath_hal_rf * const *prf;
	struct ath_hal_rf * rf;

	OS_SET_FOREACH(prf, ah_rfs) {
		struct ath_hal_rf *rf = *prf;
		if (rf->probe(ah))
			return rf;
	}

	TAILQ_FOREACH(rf, &ah_rf_list, node) {
		if (rf->probe(ah))
			return rf;
	}
	*ecode = HAL_ENOTSUPP;
	return AH_NULL;
}

const char *
ath_hal_rf_name(struct ath_hal *ah)
{
	switch (ah->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR) {
	case 0:			/* 5210 */
		return "5110";	/* NB: made up */
	case AR_RAD5111_SREV_MAJOR:
	case AR_RAD5111_SREV_PROD:
		return "5111";
	case AR_RAD2111_SREV_MAJOR:
		return "2111";
	case AR_RAD5112_SREV_MAJOR:
	case AR_RAD5112_SREV_2_0:
	case AR_RAD5112_SREV_2_1:
		return "5112";
	case AR_RAD2112_SREV_MAJOR:
	case AR_RAD2112_SREV_2_0:
	case AR_RAD2112_SREV_2_1:
		return "2112";
	case AR_RAD2413_SREV_MAJOR:
		return "2413";
	case AR_RAD5413_SREV_MAJOR:
		return "5413";
	case AR_RAD2316_SREV_MAJOR:
		return "2316";
	case AR_RAD2317_SREV_MAJOR:
		return "2317";
	case AR_RAD5424_SREV_MAJOR:
		return "5424";

	case AR_RAD5133_SREV_MAJOR:
		return "5133";
	case AR_RAD2133_SREV_MAJOR:
		return "2133";
	case AR_RAD5122_SREV_MAJOR:
		return "5122";
	case AR_RAD2122_SREV_MAJOR:
		return "2122";
	}
	return "????";
}

/*
 * Poll the register looking for a specific value.
 */
HAL_BOOL
ath_hal_wait(struct ath_hal *ah, u_int reg, uint32_t mask, uint32_t val)
{
#define	AH_TIMEOUT	5000
	return ath_hal_waitfor(ah, reg, mask, val, AH_TIMEOUT);
#undef AH_TIMEOUT
}

HAL_BOOL
ath_hal_waitfor(struct ath_hal *ah, u_int reg, uint32_t mask, uint32_t val, uint32_t timeout)
{
	int i;

	for (i = 0; i < timeout; i++) {
		if ((OS_REG_READ(ah, reg) & mask) == val)
			return AH_TRUE;
		OS_DELAY(10);
	}
	HALDEBUG(ah, HAL_DEBUG_REGIO | HAL_DEBUG_PHYIO,
	    "%s: timeout on reg 0x%x: 0x%08x & 0x%08x != 0x%08x\n",
	    __func__, reg, OS_REG_READ(ah, reg), mask, val);
	return AH_FALSE;
}

/*
 * Reverse the bits starting at the low bit for a value of
 * bit_count in size
 */
uint32_t
ath_hal_reverseBits(uint32_t val, uint32_t n)
{
	uint32_t retval;
	int i;

	for (i = 0, retval = 0; i < n; i++) {
		retval = (retval << 1) | (val & 1);
		val >>= 1;
	}
	return retval;
}

/* 802.11n related timing definitions */

#define	OFDM_PLCP_BITS	22
#define	HT_L_STF	8
#define	HT_L_LTF	8
#define	HT_L_SIG	4
#define	HT_SIG		8
#define	HT_STF		4
#define	HT_LTF(n)	((n) * 4)

#define	HT_RC_2_MCS(_rc)	((_rc) & 0x1f)
#define	HT_RC_2_STREAMS(_rc)	((((_rc) & 0x78) >> 3) + 1)
#define	IS_HT_RATE(_rc)		( (_rc) & IEEE80211_RATE_MCS)

/*
 * Calculate the duration of a packet whether it is 11n or legacy.
 */
uint32_t
ath_hal_pkt_txtime(struct ath_hal *ah, const HAL_RATE_TABLE *rates, uint32_t frameLen,
    uint16_t rateix, HAL_BOOL isht40, HAL_BOOL shortPreamble,
    HAL_BOOL includeSifs)
{
	uint8_t rc;
	int numStreams;

	rc = rates->info[rateix].rateCode;

	/* Legacy rate? Return the old way */
	if (! IS_HT_RATE(rc))
		return ath_hal_computetxtime(ah, rates, frameLen, rateix,
		    shortPreamble, includeSifs);

	/* 11n frame - extract out the number of spatial streams */
	numStreams = HT_RC_2_STREAMS(rc);
	KASSERT(numStreams > 0 && numStreams <= 4,
	    ("number of spatial streams needs to be 1..3: MCS rate 0x%x!",
	    rateix));

	/* XXX TODO: Add SIFS */
	return ath_computedur_ht(frameLen, rc, numStreams, isht40,
	    shortPreamble);
}

static const uint16_t ht20_bps[32] = {
    26, 52, 78, 104, 156, 208, 234, 260,
    52, 104, 156, 208, 312, 416, 468, 520,
    78, 156, 234, 312, 468, 624, 702, 780,
    104, 208, 312, 416, 624, 832, 936, 1040
};
static const uint16_t ht40_bps[32] = {
    54, 108, 162, 216, 324, 432, 486, 540,
    108, 216, 324, 432, 648, 864, 972, 1080,
    162, 324, 486, 648, 972, 1296, 1458, 1620,
    216, 432, 648, 864, 1296, 1728, 1944, 2160
};

/*
 * Calculate the transmit duration of an 11n frame.
 */
uint32_t
ath_computedur_ht(uint32_t frameLen, uint16_t rate, int streams,
    HAL_BOOL isht40, HAL_BOOL isShortGI)
{
	uint32_t bitsPerSymbol, numBits, numSymbols, txTime;

	KASSERT(rate & IEEE80211_RATE_MCS, ("not mcs %d", rate));
	KASSERT((rate &~ IEEE80211_RATE_MCS) < 31, ("bad mcs 0x%x", rate));

	if (isht40)
		bitsPerSymbol = ht40_bps[HT_RC_2_MCS(rate)];
	else
		bitsPerSymbol = ht20_bps[HT_RC_2_MCS(rate)];
	numBits = OFDM_PLCP_BITS + (frameLen << 3);
	numSymbols = howmany(numBits, bitsPerSymbol);
	if (isShortGI)
		txTime = ((numSymbols * 18) + 4) / 5;   /* 3.6us */
	else
		txTime = numSymbols * 4;                /* 4us */
	return txTime + HT_L_STF + HT_L_LTF +
	    HT_L_SIG + HT_SIG + HT_STF + HT_LTF(streams);
}

/*
 * Compute the time to transmit a frame of length frameLen bytes
 * using the specified rate, phy, and short preamble setting.
 */
uint16_t
ath_hal_computetxtime(struct ath_hal *ah,
	const HAL_RATE_TABLE *rates, uint32_t frameLen, uint16_t rateix,
	HAL_BOOL shortPreamble, HAL_BOOL includeSifs)
{
	uint32_t bitsPerSymbol, numBits, numSymbols, phyTime, txTime;
	uint32_t kbps;

	/* Warn if this function is called for 11n rates; it should not be! */
	if (IS_HT_RATE(rates->info[rateix].rateCode))
		ath_hal_printf(ah, "%s: MCS rate? (index %d; hwrate 0x%x)\n",
		    __func__, rateix, rates->info[rateix].rateCode);

	kbps = rates->info[rateix].rateKbps;
	/*
	 * index can be invalid during dynamic Turbo transitions. 
	 * XXX
	 */
	if (kbps == 0)
		return 0;
	switch (rates->info[rateix].phy) {
	case IEEE80211_T_CCK:
		phyTime		= CCK_PREAMBLE_BITS + CCK_PLCP_BITS;
		if (shortPreamble && rates->info[rateix].shortPreamble)
			phyTime >>= 1;
		numBits		= frameLen << 3;
		txTime		= phyTime
				+ ((numBits * 1000)/kbps);
		if (includeSifs)
			txTime	+= CCK_SIFS_TIME;
		break;
	case IEEE80211_T_OFDM:
		bitsPerSymbol	= (kbps * OFDM_SYMBOL_TIME) / 1000;
		HALASSERT(bitsPerSymbol != 0);

		numBits		= OFDM_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= OFDM_PREAMBLE_TIME
				+ (numSymbols * OFDM_SYMBOL_TIME);
		if (includeSifs)
			txTime	+= OFDM_SIFS_TIME;
		break;
	case IEEE80211_T_OFDM_HALF:
		bitsPerSymbol	= (kbps * OFDM_HALF_SYMBOL_TIME) / 1000;
		HALASSERT(bitsPerSymbol != 0);

		numBits		= OFDM_HALF_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= OFDM_HALF_PREAMBLE_TIME
				+ (numSymbols * OFDM_HALF_SYMBOL_TIME);
		if (includeSifs)
			txTime	+= OFDM_HALF_SIFS_TIME;
		break;
	case IEEE80211_T_OFDM_QUARTER:
		bitsPerSymbol	= (kbps * OFDM_QUARTER_SYMBOL_TIME) / 1000;
		HALASSERT(bitsPerSymbol != 0);

		numBits		= OFDM_QUARTER_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= OFDM_QUARTER_PREAMBLE_TIME
				+ (numSymbols * OFDM_QUARTER_SYMBOL_TIME);
		if (includeSifs)
			txTime	+= OFDM_QUARTER_SIFS_TIME;
		break;
	case IEEE80211_T_TURBO:
		bitsPerSymbol	= (kbps * TURBO_SYMBOL_TIME) / 1000;
		HALASSERT(bitsPerSymbol != 0);

		numBits		= TURBO_PLCP_BITS + (frameLen << 3);
		numSymbols	= howmany(numBits, bitsPerSymbol);
		txTime		= TURBO_PREAMBLE_TIME
				+ (numSymbols * TURBO_SYMBOL_TIME);
		if (includeSifs)
			txTime	+= TURBO_SIFS_TIME;
		break;
	default:
		HALDEBUG(ah, HAL_DEBUG_PHYIO,
		    "%s: unknown phy %u (rate ix %u)\n",
		    __func__, rates->info[rateix].phy, rateix);
		txTime = 0;
		break;
	}
	return txTime;
}

int
ath_hal_get_curmode(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	/*
	 * Pick a default mode at bootup. A channel change is inevitable.
	 */
	if (!chan)
		return HAL_MODE_11NG_HT20;

	if (IEEE80211_IS_CHAN_TURBO(chan))
		return HAL_MODE_TURBO;

	/* check for NA_HT before plain A, since IS_CHAN_A includes NA_HT */
	if (IEEE80211_IS_CHAN_5GHZ(chan) && IEEE80211_IS_CHAN_HT20(chan))
		return HAL_MODE_11NA_HT20;
	if (IEEE80211_IS_CHAN_5GHZ(chan) && IEEE80211_IS_CHAN_HT40U(chan))
		return HAL_MODE_11NA_HT40PLUS;
	if (IEEE80211_IS_CHAN_5GHZ(chan) && IEEE80211_IS_CHAN_HT40D(chan))
		return HAL_MODE_11NA_HT40MINUS;
	if (IEEE80211_IS_CHAN_A(chan))
		return HAL_MODE_11A;

	/* check for NG_HT before plain G, since IS_CHAN_G includes NG_HT */
	if (IEEE80211_IS_CHAN_2GHZ(chan) && IEEE80211_IS_CHAN_HT20(chan))
		return HAL_MODE_11NG_HT20;
	if (IEEE80211_IS_CHAN_2GHZ(chan) && IEEE80211_IS_CHAN_HT40U(chan))
		return HAL_MODE_11NG_HT40PLUS;
	if (IEEE80211_IS_CHAN_2GHZ(chan) && IEEE80211_IS_CHAN_HT40D(chan))
		return HAL_MODE_11NG_HT40MINUS;

	/*
	 * XXX For FreeBSD, will this work correctly given the DYN
	 * chan mode (OFDM+CCK dynamic) ? We have pure-G versions DYN-BG..
	 */
	if (IEEE80211_IS_CHAN_G(chan))
		return HAL_MODE_11G;
	if (IEEE80211_IS_CHAN_B(chan))
		return HAL_MODE_11B;

	HALASSERT(0);
	return HAL_MODE_11NG_HT20;
}


typedef enum {
	WIRELESS_MODE_11a   = 0,
	WIRELESS_MODE_TURBO = 1,
	WIRELESS_MODE_11b   = 2,
	WIRELESS_MODE_11g   = 3,
	WIRELESS_MODE_108g  = 4,

	WIRELESS_MODE_MAX
} WIRELESS_MODE;

/*
 * XXX TODO: for some (?) chips, an 11b mode still runs at 11bg.
 * Maybe AR5211 has separate 11b and 11g only modes, so 11b is 22MHz
 * and 11g is 44MHz, but AR5416 and later run 11b in 11bg mode, right?
 */
static WIRELESS_MODE
ath_hal_chan2wmode(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	if (IEEE80211_IS_CHAN_B(chan))
		return WIRELESS_MODE_11b;
	if (IEEE80211_IS_CHAN_G(chan))
		return WIRELESS_MODE_11g;
	if (IEEE80211_IS_CHAN_108G(chan))
		return WIRELESS_MODE_108g;
	if (IEEE80211_IS_CHAN_TURBO(chan))
		return WIRELESS_MODE_TURBO;
	return WIRELESS_MODE_11a;
}

/*
 * Convert between microseconds and core system clocks.
 */
                                     /* 11a Turbo  11b  11g  108g */
static const uint8_t CLOCK_RATE[]  = { 40,  80,   22,  44,   88  };

#define	CLOCK_FAST_RATE_5GHZ_OFDM	44

u_int
ath_hal_mac_clks(struct ath_hal *ah, u_int usecs)
{
	const struct ieee80211_channel *c = AH_PRIVATE(ah)->ah_curchan;
	u_int clks;

	/* NB: ah_curchan may be null when called attach time */
	/* XXX merlin and later specific workaround - 5ghz fast clock is 44 */
	if (c != AH_NULL && IS_5GHZ_FAST_CLOCK_EN(ah, c)) {
		clks = usecs * CLOCK_FAST_RATE_5GHZ_OFDM;
		if (IEEE80211_IS_CHAN_HT40(c))
			clks <<= 1;
	} else if (c != AH_NULL) {
		clks = usecs * CLOCK_RATE[ath_hal_chan2wmode(ah, c)];
		if (IEEE80211_IS_CHAN_HT40(c))
			clks <<= 1;
	} else
		clks = usecs * CLOCK_RATE[WIRELESS_MODE_11b];

	/* Compensate for half/quarter rate */
	if (c != AH_NULL && IEEE80211_IS_CHAN_HALF(c))
		clks = clks / 2;
	else if (c != AH_NULL && IEEE80211_IS_CHAN_QUARTER(c))
		clks = clks / 4;

	return clks;
}

u_int
ath_hal_mac_usec(struct ath_hal *ah, u_int clks)
{
	uint64_t psec;

	psec = ath_hal_mac_psec(ah, clks);
	return (psec / 1000000);
}

/*
 * XXX TODO: half, quarter rates.
 */
uint64_t
ath_hal_mac_psec(struct ath_hal *ah, u_int clks)
{
	const struct ieee80211_channel *c = AH_PRIVATE(ah)->ah_curchan;
	uint64_t psec;

	/* NB: ah_curchan may be null when called attach time */
	/* XXX merlin and later specific workaround - 5ghz fast clock is 44 */
	if (c != AH_NULL && IS_5GHZ_FAST_CLOCK_EN(ah, c)) {
		psec = (clks * 1000000ULL) / CLOCK_FAST_RATE_5GHZ_OFDM;
		if (IEEE80211_IS_CHAN_HT40(c))
			psec >>= 1;
	} else if (c != AH_NULL) {
		psec = (clks * 1000000ULL) / CLOCK_RATE[ath_hal_chan2wmode(ah, c)];
		if (IEEE80211_IS_CHAN_HT40(c))
			psec >>= 1;
	} else
		psec = (clks * 1000000ULL) / CLOCK_RATE[WIRELESS_MODE_11b];
	return psec;
}

/*
 * Setup a h/w rate table's reverse lookup table and
 * fill in ack durations.  This routine is called for
 * each rate table returned through the ah_getRateTable
 * method.  The reverse lookup tables are assumed to be
 * initialized to zero (or at least the first entry).
 * We use this as a key that indicates whether or not
 * we've previously setup the reverse lookup table.
 *
 * XXX not reentrant, but shouldn't matter
 */
void
ath_hal_setupratetable(struct ath_hal *ah, HAL_RATE_TABLE *rt)
{
#define	N(a)	(sizeof(a)/sizeof(a[0]))
	int i;

	if (rt->rateCodeToIndex[0] != 0)	/* already setup */
		return;
	for (i = 0; i < N(rt->rateCodeToIndex); i++)
		rt->rateCodeToIndex[i] = (uint8_t) -1;
	for (i = 0; i < rt->rateCount; i++) {
		uint8_t code = rt->info[i].rateCode;
		uint8_t cix = rt->info[i].controlRate;

		HALASSERT(code < N(rt->rateCodeToIndex));
		rt->rateCodeToIndex[code] = i;
		HALASSERT((code | rt->info[i].shortPreamble) <
		    N(rt->rateCodeToIndex));
		rt->rateCodeToIndex[code | rt->info[i].shortPreamble] = i;
		/*
		 * XXX for 11g the control rate to use for 5.5 and 11 Mb/s
		 *     depends on whether they are marked as basic rates;
		 *     the static tables are setup with an 11b-compatible
		 *     2Mb/s rate which will work but is suboptimal
		 */
		rt->info[i].lpAckDuration = ath_hal_computetxtime(ah, rt,
			WLAN_CTRL_FRAME_SIZE, cix, AH_FALSE, AH_TRUE);
		rt->info[i].spAckDuration = ath_hal_computetxtime(ah, rt,
			WLAN_CTRL_FRAME_SIZE, cix, AH_TRUE, AH_TRUE);
	}
#undef N
}

HAL_STATUS
ath_hal_getcapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t *result)
{
	const HAL_CAPABILITIES *pCap = &AH_PRIVATE(ah)->ah_caps;

	switch (type) {
	case HAL_CAP_REG_DMN:		/* regulatory domain */
		*result = AH_PRIVATE(ah)->ah_currentRD;
		return HAL_OK;
	case HAL_CAP_DFS_DMN:		/* DFS Domain */
		*result = AH_PRIVATE(ah)->ah_dfsDomain;
		return HAL_OK;
	case HAL_CAP_CIPHER:		/* cipher handled in hardware */
	case HAL_CAP_TKIP_MIC:		/* handle TKIP MIC in hardware */
		return HAL_ENOTSUPP;
	case HAL_CAP_TKIP_SPLIT:	/* hardware TKIP uses split keys */
		return HAL_ENOTSUPP;
	case HAL_CAP_PHYCOUNTERS:	/* hardware PHY error counters */
		return pCap->halHwPhyCounterSupport ? HAL_OK : HAL_ENXIO;
	case HAL_CAP_WME_TKIPMIC:   /* hardware can do TKIP MIC when WMM is turned on */
		return HAL_ENOTSUPP;
	case HAL_CAP_DIVERSITY:		/* hardware supports fast diversity */
		return HAL_ENOTSUPP;
	case HAL_CAP_KEYCACHE_SIZE:	/* hardware key cache size */
		*result =  pCap->halKeyCacheSize;
		return HAL_OK;
	case HAL_CAP_NUM_TXQUEUES:	/* number of hardware tx queues */
		*result = pCap->halTotalQueues;
		return HAL_OK;
	case HAL_CAP_VEOL:		/* hardware supports virtual EOL */
		return pCap->halVEOLSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_PSPOLL:		/* hardware PS-Poll support works */
		return pCap->halPSPollBroken ? HAL_ENOTSUPP : HAL_OK;
	case HAL_CAP_COMPRESSION:
		return pCap->halCompressSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_BURST:
		return pCap->halBurstSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_FASTFRAME:
		return pCap->halFastFramesSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_DIAG:		/* hardware diagnostic support */
		*result = AH_PRIVATE(ah)->ah_diagreg;
		return HAL_OK;
	case HAL_CAP_TXPOW:		/* global tx power limit  */
		switch (capability) {
		case 0:			/* facility is supported */
			return HAL_OK;
		case 1:			/* current limit */
			*result = AH_PRIVATE(ah)->ah_powerLimit;
			return HAL_OK;
		case 2:			/* current max tx power */
			*result = AH_PRIVATE(ah)->ah_maxPowerLevel;
			return HAL_OK;
		case 3:			/* scale factor */
			*result = AH_PRIVATE(ah)->ah_tpScale;
			return HAL_OK;
		}
		return HAL_ENOTSUPP;
	case HAL_CAP_BSSIDMASK:		/* hardware supports bssid mask */
		return pCap->halBssIdMaskSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_MCAST_KEYSRCH:	/* multicast frame keycache search */
		return pCap->halMcastKeySrchSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_TSF_ADJUST:	/* hardware has beacon tsf adjust */
		return HAL_ENOTSUPP;
	case HAL_CAP_RFSILENT:		/* rfsilent support  */
		switch (capability) {
		case 0:			/* facility is supported */
			return pCap->halRfSilentSupport ? HAL_OK : HAL_ENOTSUPP;
		case 1:			/* current setting */
			return AH_PRIVATE(ah)->ah_rfkillEnabled ?
				HAL_OK : HAL_ENOTSUPP;
		case 2:			/* rfsilent config */
			*result = AH_PRIVATE(ah)->ah_rfsilent;
			return HAL_OK;
		}
		return HAL_ENOTSUPP;
	case HAL_CAP_11D:
		return HAL_OK;

	case HAL_CAP_HT:
		return pCap->halHTSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_GTXTO:
		return pCap->halGTTSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_FAST_CC:
		return pCap->halFastCCSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_TX_CHAINMASK:	/* mask of TX chains supported */
		*result = pCap->halTxChainMask;
		return HAL_OK;
	case HAL_CAP_RX_CHAINMASK:	/* mask of RX chains supported */
		*result = pCap->halRxChainMask;
		return HAL_OK;
	case HAL_CAP_NUM_GPIO_PINS:
		*result = pCap->halNumGpioPins;
		return HAL_OK;
	case HAL_CAP_CST:
		return pCap->halCSTSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_RTS_AGGR_LIMIT:
		*result = pCap->halRtsAggrLimit;
		return HAL_OK;
	case HAL_CAP_4ADDR_AGGR:
		return pCap->hal4AddrAggrSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_EXT_CHAN_DFS:
		return pCap->halExtChanDfsSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_RX_STBC:
		return pCap->halRxStbcSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_TX_STBC:
		return pCap->halTxStbcSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_COMBINED_RADAR_RSSI:
		return pCap->halUseCombinedRadarRssi ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_AUTO_SLEEP:
		return pCap->halAutoSleepSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_MBSSID_AGGR_SUPPORT:
		return pCap->halMbssidAggrSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_SPLIT_4KB_TRANS:	/* hardware handles descriptors straddling 4k page boundary */
		return pCap->hal4kbSplitTransSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_REG_FLAG:
		*result = AH_PRIVATE(ah)->ah_currentRDext;
		return HAL_OK;
	case HAL_CAP_ENHANCED_DMA_SUPPORT:
		return pCap->halEnhancedDmaSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_NUM_TXMAPS:
		*result = pCap->halNumTxMaps;
		return HAL_OK;
	case HAL_CAP_TXDESCLEN:
		*result = pCap->halTxDescLen;
		return HAL_OK;
	case HAL_CAP_TXSTATUSLEN:
		*result = pCap->halTxStatusLen;
		return HAL_OK;
	case HAL_CAP_RXSTATUSLEN:
		*result = pCap->halRxStatusLen;
		return HAL_OK;
	case HAL_CAP_RXFIFODEPTH:
		switch (capability) {
		case HAL_RX_QUEUE_HP:
			*result = pCap->halRxHpFifoDepth;
			return HAL_OK;
		case HAL_RX_QUEUE_LP:
			*result = pCap->halRxLpFifoDepth;
			return HAL_OK;
		default:
			return HAL_ENOTSUPP;
	}
	case HAL_CAP_RXBUFSIZE:
	case HAL_CAP_NUM_MR_RETRIES:
		*result = pCap->halNumMRRetries;
		return HAL_OK;
	case HAL_CAP_BT_COEX:
		return pCap->halBtCoexSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_SPECTRAL_SCAN:
		return pCap->halSpectralScanSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_HT20_SGI:
		return pCap->halHTSGI20Support ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_RXTSTAMP_PREC:	/* rx desc tstamp precision (bits) */
		*result = pCap->halRxTstampPrecision;
		return HAL_OK;
	case HAL_CAP_ANT_DIV_COMB:	/* AR9285/AR9485 LNA diversity */
		return pCap->halAntDivCombSupport ? HAL_OK  : HAL_ENOTSUPP;

	case HAL_CAP_ENHANCED_DFS_SUPPORT:
		return pCap->halEnhancedDfsSupport ? HAL_OK : HAL_ENOTSUPP;

	/* FreeBSD-specific entries for now */
	case HAL_CAP_RXORN_FATAL:	/* HAL_INT_RXORN treated as fatal  */
		return AH_PRIVATE(ah)->ah_rxornIsFatal ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_INTRMASK:		/* mask of supported interrupts */
		*result = pCap->halIntrMask;
		return HAL_OK;
	case HAL_CAP_BSSIDMATCH:	/* hardware has disable bssid match */
		return pCap->halBssidMatchSupport ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_STREAMS:		/* number of 11n spatial streams */
		switch (capability) {
		case 0:			/* TX */
			*result = pCap->halTxStreams;
			return HAL_OK;
		case 1:			/* RX */
			*result = pCap->halRxStreams;
			return HAL_OK;
		default:
			return HAL_ENOTSUPP;
		}
	case HAL_CAP_RXDESC_SELFLINK:	/* hardware supports self-linked final RX descriptors correctly */
		return pCap->halHasRxSelfLinkedTail ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_BB_READ_WAR:		/* Baseband read WAR */
		return pCap->halHasBBReadWar? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_SERIALISE_WAR:		/* PCI register serialisation */
		return pCap->halSerialiseRegWar ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_MFP:			/* Management frame protection setting */
		*result = pCap->halMfpSupport;
		return HAL_OK;
	case HAL_CAP_RX_LNA_MIXING:	/* Hardware uses an RX LNA mixer to map 2 antennas to a 1 stream receiver */
		return pCap->halRxUsingLnaMixing ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_DO_MYBEACON:	/* Hardware supports filtering my-beacons */
		return pCap->halRxDoMyBeacon ? HAL_OK : HAL_ENOTSUPP;
	case HAL_CAP_TXTSTAMP_PREC:	/* tx desc tstamp precision (bits) */
		*result = pCap->halTxTstampPrecision;
		return HAL_OK;
	default:
		return HAL_EINVAL;
	}
}

HAL_BOOL
ath_hal_setcapability(struct ath_hal *ah, HAL_CAPABILITY_TYPE type,
	uint32_t capability, uint32_t setting, HAL_STATUS *status)
{

	switch (type) {
	case HAL_CAP_TXPOW:
		switch (capability) {
		case 3:
			if (setting <= HAL_TP_SCALE_MIN) {
				AH_PRIVATE(ah)->ah_tpScale = setting;
				return AH_TRUE;
			}
			break;
		}
		break;
	case HAL_CAP_RFSILENT:		/* rfsilent support  */
		/*
		 * NB: allow even if halRfSilentSupport is false
		 *     in case the EEPROM is misprogrammed.
		 */
		switch (capability) {
		case 1:			/* current setting */
			AH_PRIVATE(ah)->ah_rfkillEnabled = (setting != 0);
			return AH_TRUE;
		case 2:			/* rfsilent config */
			/* XXX better done per-chip for validation? */
			AH_PRIVATE(ah)->ah_rfsilent = setting;
			return AH_TRUE;
		}
		break;
	case HAL_CAP_REG_DMN:		/* regulatory domain */
		AH_PRIVATE(ah)->ah_currentRD = setting;
		return AH_TRUE;
	case HAL_CAP_RXORN_FATAL:	/* HAL_INT_RXORN treated as fatal  */
		AH_PRIVATE(ah)->ah_rxornIsFatal = setting;
		return AH_TRUE;
	default:
		break;
	}
	if (status)
		*status = HAL_EINVAL;
	return AH_FALSE;
}

/* 
 * Common support for getDiagState method.
 */

static u_int
ath_hal_getregdump(struct ath_hal *ah, const HAL_REGRANGE *regs,
	void *dstbuf, int space)
{
	uint32_t *dp = dstbuf;
	int i;

	for (i = 0; space >= 2*sizeof(uint32_t); i++) {
		uint32_t r = regs[i].start;
		uint32_t e = regs[i].end;
		*dp++ = r;
		*dp++ = e;
		space -= 2*sizeof(uint32_t);
		do {
			*dp++ = OS_REG_READ(ah, r);
			r += sizeof(uint32_t);
			space -= sizeof(uint32_t);
		} while (r <= e && space >= sizeof(uint32_t));
	}
	return (char *) dp - (char *) dstbuf;
}
 
static void
ath_hal_setregs(struct ath_hal *ah, const HAL_REGWRITE *regs, int space)
{
	while (space >= sizeof(HAL_REGWRITE)) {
		OS_REG_WRITE(ah, regs->addr, regs->value);
		regs++, space -= sizeof(HAL_REGWRITE);
	}
}

HAL_BOOL
ath_hal_getdiagstate(struct ath_hal *ah, int request,
	const void *args, uint32_t argsize,
	void **result, uint32_t *resultsize)
{

	switch (request) {
	case HAL_DIAG_REVS:
		*result = &AH_PRIVATE(ah)->ah_devid;
		*resultsize = sizeof(HAL_REVS);
		return AH_TRUE;
	case HAL_DIAG_REGS:
		*resultsize = ath_hal_getregdump(ah, args, *result,*resultsize);
		return AH_TRUE;
	case HAL_DIAG_SETREGS:
		ath_hal_setregs(ah, args, argsize);
		*resultsize = 0;
		return AH_TRUE;
	case HAL_DIAG_FATALERR:
		*result = &AH_PRIVATE(ah)->ah_fatalState[0];
		*resultsize = sizeof(AH_PRIVATE(ah)->ah_fatalState);
		return AH_TRUE;
	case HAL_DIAG_EEREAD:
		if (argsize != sizeof(uint16_t))
			return AH_FALSE;
		if (!ath_hal_eepromRead(ah, *(const uint16_t *)args, *result))
			return AH_FALSE;
		*resultsize = sizeof(uint16_t);
		return AH_TRUE;
#ifdef AH_PRIVATE_DIAG
	case HAL_DIAG_SETKEY: {
		const HAL_DIAG_KEYVAL *dk;

		if (argsize != sizeof(HAL_DIAG_KEYVAL))
			return AH_FALSE;
		dk = (const HAL_DIAG_KEYVAL *)args;
		return ah->ah_setKeyCacheEntry(ah, dk->dk_keyix,
			&dk->dk_keyval, dk->dk_mac, dk->dk_xor);
	}
	case HAL_DIAG_RESETKEY:
		if (argsize != sizeof(uint16_t))
			return AH_FALSE;
		return ah->ah_resetKeyCacheEntry(ah, *(const uint16_t *)args);
#ifdef AH_SUPPORT_WRITE_EEPROM
	case HAL_DIAG_EEWRITE: {
		const HAL_DIAG_EEVAL *ee;
		if (argsize != sizeof(HAL_DIAG_EEVAL))
			return AH_FALSE;
		ee = (const HAL_DIAG_EEVAL *)args;
		return ath_hal_eepromWrite(ah, ee->ee_off, ee->ee_data);
	}
#endif /* AH_SUPPORT_WRITE_EEPROM */
#endif /* AH_PRIVATE_DIAG */
	case HAL_DIAG_11NCOMPAT:
		if (argsize == 0) {
			*resultsize = sizeof(uint32_t);
			*((uint32_t *)(*result)) =
				AH_PRIVATE(ah)->ah_11nCompat;
		} else if (argsize == sizeof(uint32_t)) {
			AH_PRIVATE(ah)->ah_11nCompat = *(const uint32_t *)args;
		} else
			return AH_FALSE;
		return AH_TRUE;
	case HAL_DIAG_CHANSURVEY:
		*result = &AH_PRIVATE(ah)->ah_chansurvey;
		*resultsize = sizeof(HAL_CHANNEL_SURVEY);
		return AH_TRUE;
	}
	return AH_FALSE;
}

/*
 * Set the properties of the tx queue with the parameters
 * from qInfo.
 */
HAL_BOOL
ath_hal_setTxQProps(struct ath_hal *ah,
	HAL_TX_QUEUE_INFO *qi, const HAL_TXQ_INFO *qInfo)
{
	uint32_t cw;

	if (qi->tqi_type == HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE,
		    "%s: inactive queue\n", __func__);
		return AH_FALSE;
	}
	/* XXX validate parameters */
	qi->tqi_ver = qInfo->tqi_ver;
	qi->tqi_subtype = qInfo->tqi_subtype;
	qi->tqi_qflags = qInfo->tqi_qflags;
	qi->tqi_priority = qInfo->tqi_priority;
	if (qInfo->tqi_aifs != HAL_TXQ_USEDEFAULT)
		qi->tqi_aifs = AH_MIN(qInfo->tqi_aifs, 255);
	else
		qi->tqi_aifs = INIT_AIFS;
	if (qInfo->tqi_cwmin != HAL_TXQ_USEDEFAULT) {
		cw = AH_MIN(qInfo->tqi_cwmin, 1024);
		/* make sure that the CWmin is of the form (2^n - 1) */
		qi->tqi_cwmin = 1;
		while (qi->tqi_cwmin < cw)
			qi->tqi_cwmin = (qi->tqi_cwmin << 1) | 1;
	} else
		qi->tqi_cwmin = qInfo->tqi_cwmin;
	if (qInfo->tqi_cwmax != HAL_TXQ_USEDEFAULT) {
		cw = AH_MIN(qInfo->tqi_cwmax, 1024);
		/* make sure that the CWmax is of the form (2^n - 1) */
		qi->tqi_cwmax = 1;
		while (qi->tqi_cwmax < cw)
			qi->tqi_cwmax = (qi->tqi_cwmax << 1) | 1;
	} else
		qi->tqi_cwmax = INIT_CWMAX;
	/* Set retry limit values */
	if (qInfo->tqi_shretry != 0)
		qi->tqi_shretry = AH_MIN(qInfo->tqi_shretry, 15);
	else
		qi->tqi_shretry = INIT_SH_RETRY;
	if (qInfo->tqi_lgretry != 0)
		qi->tqi_lgretry = AH_MIN(qInfo->tqi_lgretry, 15);
	else
		qi->tqi_lgretry = INIT_LG_RETRY;
	qi->tqi_cbrPeriod = qInfo->tqi_cbrPeriod;
	qi->tqi_cbrOverflowLimit = qInfo->tqi_cbrOverflowLimit;
	qi->tqi_burstTime = qInfo->tqi_burstTime;
	qi->tqi_readyTime = qInfo->tqi_readyTime;

	switch (qInfo->tqi_subtype) {
	case HAL_WME_UPSD:
		if (qi->tqi_type == HAL_TX_QUEUE_DATA)
			qi->tqi_intFlags = HAL_TXQ_USE_LOCKOUT_BKOFF_DIS;
		break;
	default:
		break;		/* NB: silence compiler */
	}
	return AH_TRUE;
}

HAL_BOOL
ath_hal_getTxQProps(struct ath_hal *ah,
	HAL_TXQ_INFO *qInfo, const HAL_TX_QUEUE_INFO *qi)
{
	if (qi->tqi_type == HAL_TX_QUEUE_INACTIVE) {
		HALDEBUG(ah, HAL_DEBUG_TXQUEUE,
		    "%s: inactive queue\n", __func__);
		return AH_FALSE;
	}

	qInfo->tqi_qflags = qi->tqi_qflags;
	qInfo->tqi_ver = qi->tqi_ver;
	qInfo->tqi_subtype = qi->tqi_subtype;
	qInfo->tqi_qflags = qi->tqi_qflags;
	qInfo->tqi_priority = qi->tqi_priority;
	qInfo->tqi_aifs = qi->tqi_aifs;
	qInfo->tqi_cwmin = qi->tqi_cwmin;
	qInfo->tqi_cwmax = qi->tqi_cwmax;
	qInfo->tqi_shretry = qi->tqi_shretry;
	qInfo->tqi_lgretry = qi->tqi_lgretry;
	qInfo->tqi_cbrPeriod = qi->tqi_cbrPeriod;
	qInfo->tqi_cbrOverflowLimit = qi->tqi_cbrOverflowLimit;
	qInfo->tqi_burstTime = qi->tqi_burstTime;
	qInfo->tqi_readyTime = qi->tqi_readyTime;
	return AH_TRUE;
}

                                     /* 11a Turbo  11b  11g  108g */
static const int16_t NOISE_FLOOR[] = { -96, -93,  -98, -96,  -93 };

/*
 * Read the current channel noise floor and return.
 * If nf cal hasn't finished, channel noise floor should be 0
 * and we return a nominal value based on band and frequency.
 *
 * NB: This is a private routine used by per-chip code to
 *     implement the ah_getChanNoise method.
 */
int16_t
ath_hal_getChanNoise(struct ath_hal *ah, const struct ieee80211_channel *chan)
{
	HAL_CHANNEL_INTERNAL *ichan;

	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		return 0;
	}
	if (ichan->rawNoiseFloor == 0) {
		WIRELESS_MODE mode = ath_hal_chan2wmode(ah, chan);

		HALASSERT(mode < WIRELESS_MODE_MAX);
		return NOISE_FLOOR[mode] + ath_hal_getNfAdjust(ah, ichan);
	} else
		return ichan->rawNoiseFloor + ichan->noiseFloorAdjust;
}

/*
 * Fetch the current setup of ctl/ext noise floor values.
 *
 * If the CHANNEL_MIMO_NF_VALID flag isn't set, the array is simply
 * populated with values from NOISE_FLOOR[] + ath_hal_getNfAdjust().
 *
 * The caller must supply ctl/ext NF arrays which are at least
 * AH_MAX_CHAINS entries long.
 */
int
ath_hal_get_mimo_chan_noise(struct ath_hal *ah,
    const struct ieee80211_channel *chan, int16_t *nf_ctl,
    int16_t *nf_ext)
{
	HAL_CHANNEL_INTERNAL *ichan;
	int i;

	ichan = ath_hal_checkchannel(ah, chan);
	if (ichan == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_NFCAL,
		    "%s: invalid channel %u/0x%x; no mapping\n",
		    __func__, chan->ic_freq, chan->ic_flags);
		for (i = 0; i < AH_MAX_CHAINS; i++) {
			nf_ctl[i] = nf_ext[i] = 0;
		}
		return 0;
	}

	/* Return 0 if there's no valid MIMO values (yet) */
	if (! (ichan->privFlags & CHANNEL_MIMO_NF_VALID)) {
		for (i = 0; i < AH_MAX_CHAINS; i++) {
			nf_ctl[i] = nf_ext[i] = 0;
		}
		return 0;
	}
	if (ichan->rawNoiseFloor == 0) {
		WIRELESS_MODE mode = ath_hal_chan2wmode(ah, chan);
		HALASSERT(mode < WIRELESS_MODE_MAX);
		/*
		 * See the comment below - this could cause issues for
		 * stations which have a very low RSSI, below the
		 * 'normalised' NF values in NOISE_FLOOR[].
		 */
		for (i = 0; i < AH_MAX_CHAINS; i++) {
			nf_ctl[i] = nf_ext[i] = NOISE_FLOOR[mode] +
			    ath_hal_getNfAdjust(ah, ichan);
		}
		return 1;
	} else {
		/*
		 * The value returned here from a MIMO radio is presumed to be
		 * "good enough" as a NF calculation. As RSSI values are calculated
		 * against this, an adjusted NF may be higher than the RSSI value
		 * returned from a vary weak station, resulting in an obscenely
		 * high signal strength calculation being returned.
		 *
		 * This should be re-evaluated at a later date, along with any
		 * signal strength calculations which are made. Quite likely the
		 * RSSI values will need to be adjusted to ensure the calculations
		 * don't "wrap" when RSSI is less than the "adjusted" NF value.
		 * ("Adjust" here is via ichan->noiseFloorAdjust.)
		 */
		for (i = 0; i < AH_MAX_CHAINS; i++) {
			nf_ctl[i] = ichan->noiseFloorCtl[i] + ath_hal_getNfAdjust(ah, ichan);
			nf_ext[i] = ichan->noiseFloorExt[i] + ath_hal_getNfAdjust(ah, ichan);
		}
		return 1;
	}
}

/*
 * Process all valid raw noise floors into the dBm noise floor values.
 * Though our device has no reference for a dBm noise floor, we perform
 * a relative minimization of NF's based on the lowest NF found across a
 * channel scan.
 */
void
ath_hal_process_noisefloor(struct ath_hal *ah)
{
	HAL_CHANNEL_INTERNAL *c;
	int16_t correct2, correct5;
	int16_t lowest2, lowest5;
	int i;

	/* 
	 * Find the lowest 2GHz and 5GHz noise floor values after adjusting
	 * for statistically recorded NF/channel deviation.
	 */
	correct2 = lowest2 = 0;
	correct5 = lowest5 = 0;
	for (i = 0; i < AH_PRIVATE(ah)->ah_nchan; i++) {
		WIRELESS_MODE mode;
		int16_t nf;

		c = &AH_PRIVATE(ah)->ah_channels[i];
		if (c->rawNoiseFloor >= 0)
			continue;
		/* XXX can't identify proper mode */
		mode = IS_CHAN_5GHZ(c) ? WIRELESS_MODE_11a : WIRELESS_MODE_11g;
		nf = c->rawNoiseFloor + NOISE_FLOOR[mode] +
			ath_hal_getNfAdjust(ah, c);
		if (IS_CHAN_5GHZ(c)) {
			if (nf < lowest5) { 
				lowest5 = nf;
				correct5 = NOISE_FLOOR[mode] -
				    (c->rawNoiseFloor + ath_hal_getNfAdjust(ah, c));
			}
		} else {
			if (nf < lowest2) { 
				lowest2 = nf;
				correct2 = NOISE_FLOOR[mode] -
				    (c->rawNoiseFloor + ath_hal_getNfAdjust(ah, c));
			}
		}
	}

	/* Correct the channels to reach the expected NF value */
	for (i = 0; i < AH_PRIVATE(ah)->ah_nchan; i++) {
		c = &AH_PRIVATE(ah)->ah_channels[i];
		if (c->rawNoiseFloor >= 0)
			continue;
		/* Apply correction factor */
		c->noiseFloorAdjust = ath_hal_getNfAdjust(ah, c) +
			(IS_CHAN_5GHZ(c) ? correct5 : correct2);
		HALDEBUG(ah, HAL_DEBUG_NFCAL, "%u raw nf %d adjust %d\n",
		    c->channel, c->rawNoiseFloor, c->noiseFloorAdjust);
	}
}

/*
 * INI support routines.
 */

int
ath_hal_ini_write(struct ath_hal *ah, const HAL_INI_ARRAY *ia,
	int col, int regWr)
{
	int r;

	HALASSERT(col < ia->cols);
	for (r = 0; r < ia->rows; r++) {
		OS_REG_WRITE(ah, HAL_INI_VAL(ia, r, 0),
		    HAL_INI_VAL(ia, r, col));

		/* Analog shift register delay seems needed for Merlin - PR kern/154220 */
		if (HAL_INI_VAL(ia, r, 0) >= 0x7800 && HAL_INI_VAL(ia, r, 0) < 0x7900)
			OS_DELAY(100);

		DMA_YIELD(regWr);
	}
	return regWr;
}

void
ath_hal_ini_bank_setup(uint32_t data[], const HAL_INI_ARRAY *ia, int col)
{
	int r;

	HALASSERT(col < ia->cols);
	for (r = 0; r < ia->rows; r++)
		data[r] = HAL_INI_VAL(ia, r, col);
}

int
ath_hal_ini_bank_write(struct ath_hal *ah, const HAL_INI_ARRAY *ia,
	const uint32_t data[], int regWr)
{
	int r;

	for (r = 0; r < ia->rows; r++) {
		OS_REG_WRITE(ah, HAL_INI_VAL(ia, r, 0), data[r]);
		DMA_YIELD(regWr);
	}
	return regWr;
}

/*
 * These are EEPROM board related routines which should likely live in
 * a helper library of some sort.
 */

/**************************************************************
 * ath_ee_getLowerUppderIndex
 *
 * Return indices surrounding the value in sorted integer lists.
 * Requirement: the input list must be monotonically increasing
 *     and populated up to the list size
 * Returns: match is set if an index in the array matches exactly
 *     or a the target is before or after the range of the array.
 */
HAL_BOOL
ath_ee_getLowerUpperIndex(uint8_t target, uint8_t *pList, uint16_t listSize,
                   uint16_t *indexL, uint16_t *indexR)
{
    uint16_t i;

    /*
     * Check first and last elements for beyond ordered array cases.
     */
    if (target <= pList[0]) {
        *indexL = *indexR = 0;
        return AH_TRUE;
    }
    if (target >= pList[listSize-1]) {
        *indexL = *indexR = (uint16_t)(listSize - 1);
        return AH_TRUE;
    }

    /* look for value being near or between 2 values in list */
    for (i = 0; i < listSize - 1; i++) {
        /*
         * If value is close to the current value of the list
         * then target is not between values, it is one of the values
         */
        if (pList[i] == target) {
            *indexL = *indexR = i;
            return AH_TRUE;
        }
        /*
         * Look for value being between current value and next value
         * if so return these 2 values
         */
        if (target < pList[i + 1]) {
            *indexL = i;
            *indexR = (uint16_t)(i + 1);
            return AH_FALSE;
        }
    }
    HALASSERT(0);
    *indexL = *indexR = 0;
    return AH_FALSE;
}

/**************************************************************
 * ath_ee_FillVpdTable
 *
 * Fill the Vpdlist for indices Pmax-Pmin
 * Note: pwrMin, pwrMax and Vpdlist are all in dBm * 4
 */
HAL_BOOL
ath_ee_FillVpdTable(uint8_t pwrMin, uint8_t pwrMax, uint8_t *pPwrList,
                   uint8_t *pVpdList, uint16_t numIntercepts, uint8_t *pRetVpdList)
{
    uint16_t  i, k;
    uint8_t   currPwr = pwrMin;
    uint16_t  idxL, idxR;

    HALASSERT(pwrMax > pwrMin);
    for (i = 0; i <= (pwrMax - pwrMin) / 2; i++) {
        ath_ee_getLowerUpperIndex(currPwr, pPwrList, numIntercepts,
                           &(idxL), &(idxR));
        if (idxR < 1)
            idxR = 1;           /* extrapolate below */
        if (idxL == numIntercepts - 1)
            idxL = (uint16_t)(numIntercepts - 2);   /* extrapolate above */
        if (pPwrList[idxL] == pPwrList[idxR])
            k = pVpdList[idxL];
        else
            k = (uint16_t)( ((currPwr - pPwrList[idxL]) * pVpdList[idxR] + (pPwrList[idxR] - currPwr) * pVpdList[idxL]) /
                  (pPwrList[idxR] - pPwrList[idxL]) );
        HALASSERT(k < 256);
        pRetVpdList[i] = (uint8_t)k;
        currPwr += 2;               /* half dB steps */
    }

    return AH_TRUE;
}

/**************************************************************************
 * ath_ee_interpolate
 *
 * Returns signed interpolated or the scaled up interpolated value
 */
int16_t
ath_ee_interpolate(uint16_t target, uint16_t srcLeft, uint16_t srcRight,
            int16_t targetLeft, int16_t targetRight)
{
    int16_t rv;

    if (srcRight == srcLeft) {
        rv = targetLeft;
    } else {
        rv = (int16_t)( ((target - srcLeft) * targetRight +
              (srcRight - target) * targetLeft) / (srcRight - srcLeft) );
    }
    return rv;
}

/*
 * Adjust the TSF.
 */
void
ath_hal_adjusttsf(struct ath_hal *ah, int32_t tsfdelta)
{
	/* XXX handle wrap/overflow */
	OS_REG_WRITE(ah, AR_TSF_L32, OS_REG_READ(ah, AR_TSF_L32) + tsfdelta);
}

/*
 * Enable or disable CCA.
 */
void
ath_hal_setcca(struct ath_hal *ah, int ena)
{
	/*
	 * NB: fill me in; this is not provided by default because disabling
	 *     CCA in most locales violates regulatory.
	 */
}

/*
 * Get CCA setting.
 *
 * XXX TODO: turn this and the above function into methods
 * in case there are chipset differences in handling CCA.
 */
int
ath_hal_getcca(struct ath_hal *ah)
{
	u_int32_t diag;
	if (ath_hal_getcapability(ah, HAL_CAP_DIAG, 0, &diag) != HAL_OK)
		return 1;
	return ((diag & 0x500000) == 0);
}

/*
 * Set the current state of self-generated ACK and RTS/CTS frames.
 *
 * For correct DFS operation, the device should not even /ACK/ frames
 * that are sent to it during CAC or CSA.
 */
void
ath_hal_set_dfs_cac_tx_quiet(struct ath_hal *ah, HAL_BOOL ena)
{

	if (ah->ah_setDfsCacTxQuiet == NULL)
		return;
	ah->ah_setDfsCacTxQuiet(ah, ena);
}

/*
 * This routine is only needed when supporting EEPROM-in-RAM setups
 * (eg embedded SoCs and on-board PCI/PCIe devices.)
 */
/* NB: This is in 16 bit words; not bytes */
/* XXX This doesn't belong here!  */
#define ATH_DATA_EEPROM_SIZE    2048

HAL_BOOL
ath_hal_EepromDataRead(struct ath_hal *ah, u_int off, uint16_t *data)
{
	if (ah->ah_eepromdata == AH_NULL) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: no eeprom data!\n", __func__);
		return AH_FALSE;
	}
	if (off > ATH_DATA_EEPROM_SIZE) {
		HALDEBUG(ah, HAL_DEBUG_ANY, "%s: offset %x > %x\n",
		    __func__, off, ATH_DATA_EEPROM_SIZE);
		return AH_FALSE;
	}
	(*data) = ah->ah_eepromdata[off];
	return AH_TRUE;
}

/*
 * Do a 2GHz specific MHz->IEEE based on the hardware
 * frequency.
 *
 * This is the unmapped frequency which is programmed into the hardware.
 */
int
ath_hal_mhz2ieee_2ghz(struct ath_hal *ah, int freq)
{

	if (freq == 2484)
		return 14;
	if (freq < 2484)
		return ((int) freq - 2407) / 5;
	else
		return 15 + ((freq - 2512) / 20);
}

/*
 * Clear the current survey data.
 *
 * This should be done during a channel change.
 */
void
ath_hal_survey_clear(struct ath_hal *ah)
{

	OS_MEMZERO(&AH_PRIVATE(ah)->ah_chansurvey,
	    sizeof(AH_PRIVATE(ah)->ah_chansurvey));
}

/*
 * Add a sample to the channel survey.
 */
void
ath_hal_survey_add_sample(struct ath_hal *ah, HAL_SURVEY_SAMPLE *hs)
{
	HAL_CHANNEL_SURVEY *cs;

	cs = &AH_PRIVATE(ah)->ah_chansurvey;

	OS_MEMCPY(&cs->samples[cs->cur_sample], hs, sizeof(*hs));
	cs->samples[cs->cur_sample].seq_num = cs->cur_seq;
	cs->cur_sample = (cs->cur_sample + 1) % CHANNEL_SURVEY_SAMPLE_COUNT;
	cs->cur_seq++;
}
