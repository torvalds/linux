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
#ifndef _ATH_AH_INTERAL_H_
#define _ATH_AH_INTERAL_H_
/*
 * Atheros Device Hardware Access Layer (HAL).
 *
 * Internal definitions.
 */
#define	AH_NULL	0
#define	AH_MIN(a,b)	((a)<(b)?(a):(b))
#define	AH_MAX(a,b)	((a)>(b)?(a):(b))

#include <net80211/_ieee80211.h>
#include <sys/queue.h>			/* XXX for reasons */

#ifndef NBBY
#define	NBBY	8			/* number of bits/byte */
#endif

#ifndef roundup
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))  /* to any y */
#endif
#ifndef howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif

#ifndef offsetof
#define	offsetof(type, field)	((size_t)(&((type *)0)->field))
#endif

typedef struct {
	uint32_t	start;		/* first register */
	uint32_t	end;		/* ending register or zero */
} HAL_REGRANGE;

typedef struct {
	uint32_t	addr;		/* regiser address/offset */
	uint32_t	value;		/* value to write */
} HAL_REGWRITE;

/*
 * Transmit power scale factor.
 *
 * NB: This is not public because we want to discourage the use of
 *     scaling; folks should use the tx power limit interface.
 */
typedef enum {
	HAL_TP_SCALE_MAX	= 0,		/* no scaling (default) */
	HAL_TP_SCALE_50		= 1,		/* 50% of max (-3 dBm) */
	HAL_TP_SCALE_25		= 2,		/* 25% of max (-6 dBm) */
	HAL_TP_SCALE_12		= 3,		/* 12% of max (-9 dBm) */
	HAL_TP_SCALE_MIN	= 4,		/* min, but still on */
} HAL_TP_SCALE;

typedef enum {
 	HAL_CAP_RADAR		= 0,		/* Radar capability */
 	HAL_CAP_AR		= 1,		/* AR capability */
} HAL_PHYDIAG_CAPS;

/*
 * Enable/disable strong signal fast diversity
 */
#define	HAL_CAP_STRONG_DIV		2

/*
 * Each chip or class of chips registers to offer support.
 *
 * Compiled-in versions will include a linker set to iterate through the
 * linked in code.
 *
 * Modules will have to register HAL backends separately.
 */
struct ath_hal_chip {
	const char	*name;
	const char	*(*probe)(uint16_t vendorid, uint16_t devid);
	struct ath_hal	*(*attach)(uint16_t devid, HAL_SOFTC,
			    HAL_BUS_TAG, HAL_BUS_HANDLE, uint16_t *eepromdata,
			    HAL_OPS_CONFIG *ah,
			    HAL_STATUS *error);
	TAILQ_ENTRY(ath_hal_chip) node;
};
#ifndef AH_CHIP
#define	AH_CHIP(_name, _probe, _attach)				\
struct ath_hal_chip _name##_chip = {				\
	.name		= #_name,				\
	.probe		= _probe,				\
	.attach		= _attach,				\
};								\
OS_DATA_SET(ah_chips, _name##_chip)
#endif

/*
 * Each RF backend registers to offer support; this is mostly
 * used by multi-chip 5212 solutions.  Single-chip solutions
 * have a fixed idea about which RF to use.
 *
 * Compiled in versions will include this linker set to iterate through
 * the linked in code.
 *
 * Modules will have to register RF backends separately.
 */
struct ath_hal_rf {
	const char	*name;
	HAL_BOOL	(*probe)(struct ath_hal *ah);
	HAL_BOOL	(*attach)(struct ath_hal *ah, HAL_STATUS *ecode);
	TAILQ_ENTRY(ath_hal_rf) node;
};
#ifndef AH_RF
#define	AH_RF(_name, _probe, _attach)				\
struct ath_hal_rf _name##_rf = {				\
	.name		= __STRING(_name),			\
	.probe		= _probe,				\
	.attach		= _attach,				\
};								\
OS_DATA_SET(ah_rfs, _name##_rf)
#endif

struct ath_hal_rf *ath_hal_rfprobe(struct ath_hal *ah, HAL_STATUS *ecode);

/*
 * Maximum number of internal channels.  Entries are per unique
 * frequency so this might be need to be increased to handle all
 * usage cases; typically no more than 32 are really needed but
 * dynamically allocating the data structures is a bit painful
 * right now.
 */
#ifndef AH_MAXCHAN
#define	AH_MAXCHAN	128
#endif

#define	HAL_NF_CAL_HIST_LEN_FULL	5
#define	HAL_NF_CAL_HIST_LEN_SMALL	1
#define	HAL_NUM_NF_READINGS		6	/* 3 chains * (ctl + ext) */
#define	HAL_NF_LOAD_DELAY		1000

/*
 * PER_CHAN doesn't work for now, as it looks like the device layer
 * has to pre-populate the per-channel list with nominal values.
 */
//#define	ATH_NF_PER_CHAN		1

typedef struct {
    u_int8_t    curr_index;
    int8_t      invalidNFcount; /* TO DO: REMOVE THIS! */
    int16_t     priv_nf[HAL_NUM_NF_READINGS];
} HAL_NFCAL_BASE;

typedef struct {
    HAL_NFCAL_BASE base;
    int16_t     nf_cal_buffer[HAL_NF_CAL_HIST_LEN_FULL][HAL_NUM_NF_READINGS];
} HAL_NFCAL_HIST_FULL;

typedef struct {
    HAL_NFCAL_BASE base;
    int16_t     nf_cal_buffer[HAL_NF_CAL_HIST_LEN_SMALL][HAL_NUM_NF_READINGS];
} HAL_NFCAL_HIST_SMALL;

#ifdef	ATH_NF_PER_CHAN
typedef HAL_NFCAL_HIST_FULL HAL_CHAN_NFCAL_HIST;
#define	AH_HOME_CHAN_NFCAL_HIST(ah, ichan) (ichan ? &ichan->nf_cal_hist: NULL)
#else
typedef HAL_NFCAL_HIST_SMALL HAL_CHAN_NFCAL_HIST;
#define	AH_HOME_CHAN_NFCAL_HIST(ah, ichan) (&AH_PRIVATE(ah)->nf_cal_hist)
#endif	/* ATH_NF_PER_CHAN */

/*
 * Internal per-channel state.  These are found
 * using ic_devdata in the ieee80211_channel.
 */
typedef struct {
	uint16_t	channel;	/* h/w frequency, NB: may be mapped */
	uint8_t		privFlags;
#define	CHANNEL_IQVALID		0x01	/* IQ calibration valid */
#define	CHANNEL_ANI_INIT	0x02	/* ANI state initialized */
#define	CHANNEL_ANI_SETUP	0x04	/* ANI state setup */
#define	CHANNEL_MIMO_NF_VALID	0x04	/* Mimo NF values are valid */
	uint8_t		calValid;	/* bitmask of cal types */
	int8_t		iCoff;
	int8_t		qCoff;
	int16_t		rawNoiseFloor;
	int16_t		noiseFloorAdjust;
	int16_t		noiseFloorCtl[AH_MAX_CHAINS];
	int16_t		noiseFloorExt[AH_MAX_CHAINS];
	uint16_t	mainSpur;	/* cached spur value for this channel */

	/*XXX TODO: make these part of privFlags */
	uint8_t  paprd_done:1,           /* 1: PAPRD DONE, 0: PAPRD Cal not done */
	       paprd_table_write_done:1; /* 1: DONE, 0: Cal data write not done */
	int		one_time_cals_done;
	HAL_CHAN_NFCAL_HIST nf_cal_hist;
} HAL_CHANNEL_INTERNAL;

/* channel requires noise floor check */
#define	CHANNEL_NFCREQUIRED	IEEE80211_CHAN_PRIV0

/* all full-width channels */
#define	IEEE80211_CHAN_ALLFULL \
	(IEEE80211_CHAN_ALL - (IEEE80211_CHAN_HALF | IEEE80211_CHAN_QUARTER))
#define	IEEE80211_CHAN_ALLTURBOFULL \
	(IEEE80211_CHAN_ALLTURBO - \
	 (IEEE80211_CHAN_HALF | IEEE80211_CHAN_QUARTER))

typedef struct {
	uint32_t	halChanSpreadSupport 		: 1,
			halSleepAfterBeaconBroken	: 1,
			halCompressSupport		: 1,
			halBurstSupport			: 1,
			halFastFramesSupport		: 1,
			halChapTuningSupport		: 1,
			halTurboGSupport		: 1,
			halTurboPrimeSupport		: 1,
			halMicAesCcmSupport		: 1,
			halMicCkipSupport		: 1,
			halMicTkipSupport		: 1,
			halTkipMicTxRxKeySupport	: 1,
			halCipherAesCcmSupport		: 1,
			halCipherCkipSupport		: 1,
			halCipherTkipSupport		: 1,
			halPSPollBroken			: 1,
			halVEOLSupport			: 1,
			halBssIdMaskSupport		: 1,
			halMcastKeySrchSupport		: 1,
			halTsfAddSupport		: 1,
			halChanHalfRate			: 1,
			halChanQuarterRate		: 1,
			halHTSupport			: 1,
			halHTSGI20Support		: 1,
			halRfSilentSupport		: 1,
			halHwPhyCounterSupport		: 1,
			halWowSupport			: 1,
			halWowMatchPatternExact		: 1,
			halAutoSleepSupport		: 1,
			halFastCCSupport		: 1,
			halBtCoexSupport		: 1;
	uint32_t	halRxStbcSupport		: 1,
			halTxStbcSupport		: 1,
			halGTTSupport			: 1,
			halCSTSupport			: 1,
			halRifsRxSupport		: 1,
			halRifsTxSupport		: 1,
			hal4AddrAggrSupport		: 1,
			halExtChanDfsSupport		: 1,
			halUseCombinedRadarRssi		: 1,
			halForcePpmSupport		: 1,
			halEnhancedPmSupport		: 1,
			halEnhancedDfsSupport		: 1,
			halMbssidAggrSupport		: 1,
			halBssidMatchSupport		: 1,
			hal4kbSplitTransSupport		: 1,
			halHasRxSelfLinkedTail		: 1,
			halSupportsFastClock5GHz	: 1,
			halHasBBReadWar			: 1,
			halSerialiseRegWar		: 1,
			halMciSupport			: 1,
			halRxTxAbortSupport		: 1,
			halPaprdEnabled			: 1,
			halHasUapsdSupport		: 1,
			halWpsPushButtonSupport		: 1,
			halBtCoexApsmWar		: 1,
			halGenTimerSupport		: 1,
			halLDPCSupport			: 1,
			halHwBeaconProcSupport		: 1,
			halEnhancedDmaSupport		: 1;
	uint32_t	halIsrRacSupport		: 1,
			halApmEnable			: 1,
			halIntrMitigation		: 1,
			hal49GhzSupport			: 1,
			halAntDivCombSupport		: 1,
			halAntDivCombSupportOrg		: 1,
			halRadioRetentionSupport	: 1,
			halSpectralScanSupport		: 1,
			halRxUsingLnaMixing		: 1,
			halRxDoMyBeacon			: 1,
			halHwUapsdTrig			: 1;

	uint32_t	halWirelessModes;
	uint16_t	halTotalQueues;
	uint16_t	halKeyCacheSize;
	uint16_t	halLow5GhzChan, halHigh5GhzChan;
	uint16_t	halLow2GhzChan, halHigh2GhzChan;
	int		halTxTstampPrecision;
	int		halRxTstampPrecision;
	int		halRtsAggrLimit;
	uint8_t		halTxChainMask;
	uint8_t		halRxChainMask;
	uint8_t		halNumGpioPins;
	uint8_t		halNumAntCfg2GHz;
	uint8_t		halNumAntCfg5GHz;
	uint32_t	halIntrMask;
	uint8_t		halTxStreams;
	uint8_t		halRxStreams;
	HAL_MFP_OPT_T	halMfpSupport;

	/* AR9300 HAL porting capabilities */
	int		hal_paprd_enabled;
	int		hal_pcie_lcr_offset;
	int		hal_pcie_lcr_extsync_en;
	int		halNumTxMaps;
	int		halTxDescLen;
	int		halTxStatusLen;
	int		halRxStatusLen;
	int		halRxHpFifoDepth;
	int		halRxLpFifoDepth;
	uint32_t	halRegCap;		/* XXX needed? */
	int		halNumMRRetries;
	int		hal_ani_poll_interval;
	int		hal_channel_switch_time_usec;
} HAL_CAPABILITIES;

struct regDomain;

/*
 * Definitions for ah_flags in ath_hal_private
 */
#define		AH_USE_EEPROM	0x1
#define		AH_IS_HB63	0x2

/*
 * The ``private area'' follows immediately after the ``public area''
 * in the data structure returned by ath_hal_attach.  Private data are
 * used by device-independent code such as the regulatory domain support.
 * In general, code within the HAL should never depend on data in the
 * public area.  Instead any public data needed internally should be
 * shadowed here.
 *
 * When declaring a device-specific ath_hal data structure this structure
 * is assumed to at the front; e.g.
 *
 *	struct ath_hal_5212 {
 *		struct ath_hal_private	ah_priv;
 *		...
 *	};
 *
 * It might be better to manage the method pointers in this structure
 * using an indirect pointer to a read-only data structure but this would
 * disallow class-style method overriding.
 */
struct ath_hal_private {
	struct ath_hal	h;			/* public area */

	/* NB: all methods go first to simplify initialization */
	HAL_BOOL	(*ah_getChannelEdges)(struct ath_hal*,
				uint16_t channelFlags,
				uint16_t *lowChannel, uint16_t *highChannel);
	u_int		(*ah_getWirelessModes)(struct ath_hal*);
	HAL_BOOL	(*ah_eepromRead)(struct ath_hal *, u_int off,
				uint16_t *data);
	HAL_BOOL	(*ah_eepromWrite)(struct ath_hal *, u_int off,
				uint16_t data);
	HAL_BOOL	(*ah_getChipPowerLimits)(struct ath_hal *,
				struct ieee80211_channel *);
	int16_t		(*ah_getNfAdjust)(struct ath_hal *,
				const HAL_CHANNEL_INTERNAL*);
	void		(*ah_getNoiseFloor)(struct ath_hal *,
				int16_t nfarray[]);

	void		*ah_eeprom;		/* opaque EEPROM state */
	uint16_t	ah_eeversion;		/* EEPROM version */
	void		(*ah_eepromDetach)(struct ath_hal *);
	HAL_STATUS	(*ah_eepromGet)(struct ath_hal *, int, void *);
	HAL_STATUS	(*ah_eepromSet)(struct ath_hal *, int, int);
	uint16_t	(*ah_getSpurChan)(struct ath_hal *, int, HAL_BOOL);
	HAL_BOOL	(*ah_eepromDiag)(struct ath_hal *, int request,
			    const void *args, uint32_t argsize,
			    void **result, uint32_t *resultsize);

	/*
	 * Device revision information.
	 */
	uint16_t	ah_devid;		/* PCI device ID */
	uint16_t	ah_subvendorid;		/* PCI subvendor ID */
	uint32_t	ah_macVersion;		/* MAC version id */
	uint16_t	ah_macRev;		/* MAC revision */
	uint16_t	ah_phyRev;		/* PHY revision */
	uint16_t	ah_analog5GhzRev;	/* 2GHz radio revision */
	uint16_t	ah_analog2GhzRev;	/* 5GHz radio revision */
	uint32_t	ah_flags;		/* misc flags */
	uint8_t		ah_ispcie;		/* PCIE, special treatment */
	uint8_t		ah_devType;		/* card type - CB, PCI, PCIe */

	HAL_OPMODE	ah_opmode;		/* operating mode from reset */
	const struct ieee80211_channel *ah_curchan;/* operating channel */
	HAL_CAPABILITIES ah_caps;		/* device capabilities */
	uint32_t	ah_diagreg;		/* user-specified AR_DIAG_SW */
	int16_t		ah_powerLimit;		/* tx power cap */
	uint16_t	ah_maxPowerLevel;	/* calculated max tx power */
	u_int		ah_tpScale;		/* tx power scale factor */
	u_int16_t	ah_extraTxPow;		/* low rates extra-txpower */
	uint32_t	ah_11nCompat;		/* 11n compat controls */

	/*
	 * State for regulatory domain handling.
	 */
	HAL_REG_DOMAIN	ah_currentRD;		/* EEPROM regulatory domain */
	HAL_REG_DOMAIN	ah_currentRDext;	/* EEPROM extended regdomain flags */
	HAL_DFS_DOMAIN	ah_dfsDomain;		/* current DFS domain */
	HAL_CHANNEL_INTERNAL ah_channels[AH_MAXCHAN]; /* private chan state */
	u_int		ah_nchan;		/* valid items in ah_channels */
	const struct regDomain *ah_rd2GHz;	/* reg state for 2G band */
	const struct regDomain *ah_rd5GHz;	/* reg state for 5G band */

	uint8_t    	ah_coverageClass;   	/* coverage class */
	/*
	 * RF Silent handling; setup according to the EEPROM.
	 */
	uint16_t	ah_rfsilent;		/* GPIO pin + polarity */
	HAL_BOOL	ah_rfkillEnabled;	/* enable/disable RfKill */
	/*
	 * Diagnostic support for discriminating HIUERR reports.
	 */
	uint32_t	ah_fatalState[6];	/* AR_ISR+shadow regs */
	int		ah_rxornIsFatal;	/* how to treat HAL_INT_RXORN */

	/* Only used if ATH_NF_PER_CHAN is defined */
	HAL_NFCAL_HIST_FULL	nf_cal_hist;

	/*
	 * Channel survey history - current channel only.
	 */
	 HAL_CHANNEL_SURVEY	ah_chansurvey;	/* channel survey */
};

#define	AH_PRIVATE(_ah)	((struct ath_hal_private *)(_ah))

#define	ath_hal_getChannelEdges(_ah, _cf, _lc, _hc) \
	AH_PRIVATE(_ah)->ah_getChannelEdges(_ah, _cf, _lc, _hc)
#define	ath_hal_getWirelessModes(_ah) \
	AH_PRIVATE(_ah)->ah_getWirelessModes(_ah)
#define	ath_hal_eepromRead(_ah, _off, _data) \
	AH_PRIVATE(_ah)->ah_eepromRead(_ah, _off, _data)
#define	ath_hal_eepromWrite(_ah, _off, _data) \
	AH_PRIVATE(_ah)->ah_eepromWrite(_ah, _off, _data)
#define	ath_hal_gpioCfgOutput(_ah, _gpio, _type) \
	(_ah)->ah_gpioCfgOutput(_ah, _gpio, _type)
#define	ath_hal_gpioCfgInput(_ah, _gpio) \
	(_ah)->ah_gpioCfgInput(_ah, _gpio)
#define	ath_hal_gpioGet(_ah, _gpio) \
	(_ah)->ah_gpioGet(_ah, _gpio)
#define	ath_hal_gpioSet(_ah, _gpio, _val) \
	(_ah)->ah_gpioSet(_ah, _gpio, _val)
#define	ath_hal_gpioSetIntr(_ah, _gpio, _ilevel) \
	(_ah)->ah_gpioSetIntr(_ah, _gpio, _ilevel)
#define	ath_hal_getpowerlimits(_ah, _chan) \
	AH_PRIVATE(_ah)->ah_getChipPowerLimits(_ah, _chan)
#define ath_hal_getNfAdjust(_ah, _c) \
	AH_PRIVATE(_ah)->ah_getNfAdjust(_ah, _c)
#define	ath_hal_getNoiseFloor(_ah, _nfArray) \
	AH_PRIVATE(_ah)->ah_getNoiseFloor(_ah, _nfArray)
#define	ath_hal_configPCIE(_ah, _reset, _poweroff) \
	(_ah)->ah_configPCIE(_ah, _reset, _poweroff)
#define	ath_hal_disablePCIE(_ah) \
	(_ah)->ah_disablePCIE(_ah)
#define	ath_hal_setInterrupts(_ah, _mask) \
	(_ah)->ah_setInterrupts(_ah, _mask)

#define ath_hal_isrfkillenabled(_ah)  \
    (ath_hal_getcapability(_ah, HAL_CAP_RFSILENT, 1, AH_NULL) == HAL_OK)
#define ath_hal_enable_rfkill(_ah, _v) \
    ath_hal_setcapability(_ah, HAL_CAP_RFSILENT, 1, _v, AH_NULL)
#define ath_hal_hasrfkill_int(_ah)  \
    (ath_hal_getcapability(_ah, HAL_CAP_RFSILENT, 3, AH_NULL) == HAL_OK)

#define	ath_hal_eepromDetach(_ah) do {				\
	if (AH_PRIVATE(_ah)->ah_eepromDetach != AH_NULL)	\
		AH_PRIVATE(_ah)->ah_eepromDetach(_ah);		\
} while (0)
#define	ath_hal_eepromGet(_ah, _param, _val) \
	AH_PRIVATE(_ah)->ah_eepromGet(_ah, _param, _val)
#define	ath_hal_eepromSet(_ah, _param, _val) \
	AH_PRIVATE(_ah)->ah_eepromSet(_ah, _param, _val)
#define	ath_hal_eepromGetFlag(_ah, _param) \
	(AH_PRIVATE(_ah)->ah_eepromGet(_ah, _param, AH_NULL) == HAL_OK)
#define ath_hal_getSpurChan(_ah, _ix, _is2G) \
	AH_PRIVATE(_ah)->ah_getSpurChan(_ah, _ix, _is2G)
#define	ath_hal_eepromDiag(_ah, _request, _a, _asize, _r, _rsize) \
	AH_PRIVATE(_ah)->ah_eepromDiag(_ah, _request, _a, _asize,  _r, _rsize)

#ifndef _NET_IF_IEEE80211_H_
/*
 * Stuff that would naturally come from _ieee80211.h
 */
#define	IEEE80211_ADDR_LEN		6

#define	IEEE80211_WEP_IVLEN			3	/* 24bit */
#define	IEEE80211_WEP_KIDLEN			1	/* 1 octet */
#define	IEEE80211_WEP_CRCLEN			4	/* CRC-32 */

#define	IEEE80211_CRC_LEN			4

#define	IEEE80211_MAX_LEN			(2300 + IEEE80211_CRC_LEN + \
    (IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN + IEEE80211_WEP_CRCLEN))
#endif /* _NET_IF_IEEE80211_H_ */

#define HAL_TXQ_USE_LOCKOUT_BKOFF_DIS	0x00000001

#define INIT_AIFS		2
#define INIT_CWMIN		15
#define INIT_CWMIN_11B		31
#define INIT_CWMAX		1023
#define INIT_SH_RETRY		10
#define INIT_LG_RETRY		10
#define INIT_SSH_RETRY		32
#define INIT_SLG_RETRY		32

typedef struct {
	uint32_t	tqi_ver;		/* HAL TXQ verson */
	HAL_TX_QUEUE	tqi_type;		/* hw queue type*/
	HAL_TX_QUEUE_SUBTYPE tqi_subtype;	/* queue subtype, if applicable */
	HAL_TX_QUEUE_FLAGS tqi_qflags;		/* queue flags */
	uint32_t	tqi_priority;
	uint32_t	tqi_aifs;		/* aifs */
	uint32_t	tqi_cwmin;		/* cwMin */
	uint32_t	tqi_cwmax;		/* cwMax */
	uint16_t	tqi_shretry;		/* frame short retry limit */
	uint16_t	tqi_lgretry;		/* frame long retry limit */
	uint32_t	tqi_cbrPeriod;
	uint32_t	tqi_cbrOverflowLimit;
	uint32_t	tqi_burstTime;
	uint32_t	tqi_readyTime;
	uint32_t	tqi_physCompBuf;
	uint32_t	tqi_intFlags;		/* flags for internal use */
} HAL_TX_QUEUE_INFO;

extern	HAL_BOOL ath_hal_setTxQProps(struct ath_hal *ah,
		HAL_TX_QUEUE_INFO *qi, const HAL_TXQ_INFO *qInfo);
extern	HAL_BOOL ath_hal_getTxQProps(struct ath_hal *ah,
		HAL_TXQ_INFO *qInfo, const HAL_TX_QUEUE_INFO *qi);

#define	HAL_SPUR_VAL_MASK		0x3FFF
#define	HAL_SPUR_CHAN_WIDTH		87
#define	HAL_BIN_WIDTH_BASE_100HZ	3125
#define	HAL_BIN_WIDTH_TURBO_100HZ	6250
#define	HAL_MAX_BINS_ALLOWED		28

#define	IS_CHAN_5GHZ(_c)	((_c)->channel > 4900)
#define	IS_CHAN_2GHZ(_c)	(!IS_CHAN_5GHZ(_c))

#define	IS_CHAN_IN_PUBLIC_SAFETY_BAND(_c) ((_c) > 4940 && (_c) < 4990)

/*
 * Deduce if the host cpu has big- or litt-endian byte order.
 */
static __inline__ int
isBigEndian(void)
{
	union {
		int32_t i;
		char c[4];
	} u;
	u.i = 1;
	return (u.c[0] == 0);
}

/* unalligned little endian access */     
#define LE_READ_2(p)							\
	((uint16_t)							\
	 ((((const uint8_t *)(p))[0]    ) | (((const uint8_t *)(p))[1]<< 8)))
#define LE_READ_4(p)							\
	((uint32_t)							\
	 ((((const uint8_t *)(p))[0]    ) | (((const uint8_t *)(p))[1]<< 8) |\
	  (((const uint8_t *)(p))[2]<<16) | (((const uint8_t *)(p))[3]<<24)))

/*
 * Register manipulation macros that expect bit field defines
 * to follow the convention that an _S suffix is appended for
 * a shift count, while the field mask has no suffix.
 */
#define	SM(_v, _f)	(((_v) << _f##_S) & (_f))
#define	MS(_v, _f)	(((_v) & (_f)) >> _f##_S)
#define OS_REG_RMW(_a, _r, _set, _clr)    \
	OS_REG_WRITE(_a, _r, (OS_REG_READ(_a, _r) & ~(_clr)) | (_set))
#define	OS_REG_RMW_FIELD(_a, _r, _f, _v) \
	OS_REG_WRITE(_a, _r, \
		(OS_REG_READ(_a, _r) &~ (_f)) | (((_v) << _f##_S) & (_f)))
#define	OS_REG_SET_BIT(_a, _r, _f) \
	OS_REG_WRITE(_a, _r, OS_REG_READ(_a, _r) | (_f))
#define	OS_REG_CLR_BIT(_a, _r, _f) \
	OS_REG_WRITE(_a, _r, OS_REG_READ(_a, _r) &~ (_f))
#define OS_REG_IS_BIT_SET(_a, _r, _f) \
	    ((OS_REG_READ(_a, _r) & (_f)) != 0)
#define	OS_REG_RMW_FIELD_ALT(_a, _r, _f, _v) \
	    OS_REG_WRITE(_a, _r, \
	    (OS_REG_READ(_a, _r) &~(_f<<_f##_S)) | \
	    (((_v) << _f##_S) & (_f<<_f##_S)))
#define	OS_REG_READ_FIELD(_a, _r, _f) \
	    (((OS_REG_READ(_a, _r) & _f) >> _f##_S))
#define	OS_REG_READ_FIELD_ALT(_a, _r, _f) \
	    ((OS_REG_READ(_a, _r) >> (_f##_S))&(_f))

/* Analog register writes may require a delay between each one (eg Merlin?) */
#define	OS_A_REG_RMW_FIELD(_a, _r, _f, _v) \
	do { OS_REG_WRITE(_a, _r, (OS_REG_READ(_a, _r) &~ (_f)) | \
	    (((_v) << _f##_S) & (_f))) ; OS_DELAY(100); } while (0)
#define	OS_A_REG_WRITE(_a, _r, _v) \
	do { OS_REG_WRITE(_a, _r, _v); OS_DELAY(100); } while (0)

/* wait for the register contents to have the specified value */
extern	HAL_BOOL ath_hal_wait(struct ath_hal *, u_int reg,
		uint32_t mask, uint32_t val);
extern	HAL_BOOL ath_hal_waitfor(struct ath_hal *, u_int reg,
		uint32_t mask, uint32_t val, uint32_t timeout);

/* return the first n bits in val reversed */
extern	uint32_t ath_hal_reverseBits(uint32_t val, uint32_t n);

/* printf interfaces */
extern	void ath_hal_printf(struct ath_hal *, const char*, ...)
		__printflike(2,3);
extern	void ath_hal_vprintf(struct ath_hal *, const char*, __va_list)
		__printflike(2, 0);
extern	const char* ath_hal_ether_sprintf(const uint8_t *mac);

/* allocate and free memory */
extern	void *ath_hal_malloc(size_t);
extern	void ath_hal_free(void *);

/* common debugging interfaces */
#ifdef AH_DEBUG
#include "ah_debug.h"
extern	int ath_hal_debug;	/* Global debug flags */

/*
 * The typecast is purely because some callers will pass in
 * AH_NULL directly rather than using a NULL ath_hal pointer.
 */
#define	HALDEBUG(_ah, __m, ...) \
	do {							\
		if ((__m) == HAL_DEBUG_UNMASKABLE ||		\
		    ath_hal_debug & (__m) ||			\
		    ((_ah) != NULL &&				\
		      ((struct ath_hal *) (_ah))->ah_config.ah_debug & (__m))) {	\
			DO_HALDEBUG((_ah), (__m), __VA_ARGS__);	\
		}						\
	} while(0);

extern	void DO_HALDEBUG(struct ath_hal *ah, u_int mask, const char* fmt, ...)
	__printflike(3,4);
#else
#define HALDEBUG(_ah, __m, ...)
#endif /* AH_DEBUG */

/*
 * Register logging definitions shared with ardecode.
 */
#include "ah_decode.h"

/*
 * Common assertion interface.  Note: it is a bad idea to generate
 * an assertion failure for any recoverable event.  Instead catch
 * the violation and, if possible, fix it up or recover from it; either
 * with an error return value or a diagnostic messages.  System software
 * does not panic unless the situation is hopeless.
 */
#ifdef AH_ASSERT
extern	void ath_hal_assert_failed(const char* filename,
		int lineno, const char* msg);

#define	HALASSERT(_x) do {					\
	if (!(_x)) {						\
		ath_hal_assert_failed(__FILE__, __LINE__, #_x);	\
	}							\
} while (0)
#else
#define	HALASSERT(_x)
#endif /* AH_ASSERT */

/* 
 * Regulatory domain support.
 */

/*
 * Return the max allowed antenna gain and apply any regulatory
 * domain specific changes.
 */
u_int	ath_hal_getantennareduction(struct ath_hal *ah,
	    const struct ieee80211_channel *chan, u_int twiceGain);

/*
 * Return the test group for the specific channel based on
 * the current regulatory setup.
 */
u_int	ath_hal_getctl(struct ath_hal *, const struct ieee80211_channel *);

/*
 * Map a public channel definition to the corresponding
 * internal data structure.  This implicitly specifies
 * whether or not the specified channel is ok to use
 * based on the current regulatory domain constraints.
 */
#ifndef AH_DEBUG
static OS_INLINE HAL_CHANNEL_INTERNAL *
ath_hal_checkchannel(struct ath_hal *ah, const struct ieee80211_channel *c)
{
	HAL_CHANNEL_INTERNAL *cc;

	HALASSERT(c->ic_devdata < AH_PRIVATE(ah)->ah_nchan);
	cc = &AH_PRIVATE(ah)->ah_channels[c->ic_devdata];
	HALASSERT(c->ic_freq == cc->channel || IEEE80211_IS_CHAN_GSM(c));
	return cc;
}
#else
/* NB: non-inline version that checks state */
HAL_CHANNEL_INTERNAL *ath_hal_checkchannel(struct ath_hal *,
		const struct ieee80211_channel *);
#endif /* AH_DEBUG */

/*
 * Return the h/w frequency for a channel.  This may be
 * different from ic_freq if this is a GSM device that
 * takes 2.4GHz frequencies and down-converts them.
 */
static OS_INLINE uint16_t
ath_hal_gethwchannel(struct ath_hal *ah, const struct ieee80211_channel *c)
{
	return ath_hal_checkchannel(ah, c)->channel;
}

/*
 * Generic get/set capability support.  Each chip overrides
 * this routine to support chip-specific capabilities.
 */
extern	HAL_STATUS ath_hal_getcapability(struct ath_hal *ah,
		HAL_CAPABILITY_TYPE type, uint32_t capability,
		uint32_t *result);
extern	HAL_BOOL ath_hal_setcapability(struct ath_hal *ah,
		HAL_CAPABILITY_TYPE type, uint32_t capability,
		uint32_t setting, HAL_STATUS *status);

/* The diagnostic codes used to be internally defined here -adrian */
#include "ah_diagcodes.h"

/*
 * The AR5416 and later HALs have MAC and baseband hang checking.
 */
typedef struct {
	uint32_t hang_reg_offset;
	uint32_t hang_val;
	uint32_t hang_mask;
	uint32_t hang_offset;
} hal_hw_hang_check_t;

typedef struct {
	uint32_t dma_dbg_3;
	uint32_t dma_dbg_4;
	uint32_t dma_dbg_5;
	uint32_t dma_dbg_6;
} mac_dbg_regs_t;

typedef enum {
	dcu_chain_state		= 0x1,
	dcu_complete_state	= 0x2,
	qcu_state		= 0x4,
	qcu_fsp_ok		= 0x8,
	qcu_fsp_state		= 0x10,
	qcu_stitch_state	= 0x20,
	qcu_fetch_state		= 0x40,
	qcu_complete_state	= 0x80
} hal_mac_hangs_t;

typedef struct {
	int states;
	uint8_t dcu_chain_state;
	uint8_t dcu_complete_state;
	uint8_t qcu_state;
	uint8_t qcu_fsp_ok;
	uint8_t qcu_fsp_state;
	uint8_t qcu_stitch_state;
	uint8_t qcu_fetch_state;
	uint8_t qcu_complete_state;
} hal_mac_hang_check_t;

enum {
    HAL_BB_HANG_DFS		= 0x0001,
    HAL_BB_HANG_RIFS		= 0x0002,
    HAL_BB_HANG_RX_CLEAR	= 0x0004,
    HAL_BB_HANG_UNKNOWN		= 0x0080,

    HAL_MAC_HANG_SIG1		= 0x0100,
    HAL_MAC_HANG_SIG2		= 0x0200,
    HAL_MAC_HANG_UNKNOWN	= 0x8000,

    HAL_BB_HANGS = HAL_BB_HANG_DFS
		 | HAL_BB_HANG_RIFS
		 | HAL_BB_HANG_RX_CLEAR
		 | HAL_BB_HANG_UNKNOWN,
    HAL_MAC_HANGS = HAL_MAC_HANG_SIG1
		 | HAL_MAC_HANG_SIG2
		 | HAL_MAC_HANG_UNKNOWN,
};

/* Merge these with above */
typedef enum hal_hw_hangs {
    HAL_DFS_BB_HANG_WAR          = 0x1,
    HAL_RIFS_BB_HANG_WAR         = 0x2,
    HAL_RX_STUCK_LOW_BB_HANG_WAR = 0x4,
    HAL_MAC_HANG_WAR             = 0x8,
    HAL_PHYRESTART_CLR_WAR       = 0x10,
    HAL_MAC_HANG_DETECTED        = 0x40000000,
    HAL_BB_HANG_DETECTED         = 0x80000000
} hal_hw_hangs_t;

/*
 * Device revision information.
 */
typedef struct {
	uint16_t	ah_devid;		/* PCI device ID */
	uint16_t	ah_subvendorid;		/* PCI subvendor ID */
	uint32_t	ah_macVersion;		/* MAC version id */
	uint16_t	ah_macRev;		/* MAC revision */
	uint16_t	ah_phyRev;		/* PHY revision */
	uint16_t	ah_analog5GhzRev;	/* 2GHz radio revision */
	uint16_t	ah_analog2GhzRev;	/* 5GHz radio revision */
} HAL_REVS;

/*
 * Argument payload for HAL_DIAG_SETKEY.
 */
typedef struct {
	HAL_KEYVAL	dk_keyval;
	uint16_t	dk_keyix;	/* key index */
	uint8_t		dk_mac[IEEE80211_ADDR_LEN];
	int		dk_xor;		/* XOR key data */
} HAL_DIAG_KEYVAL;

/*
 * Argument payload for HAL_DIAG_EEWRITE.
 */
typedef struct {
	uint16_t	ee_off;		/* eeprom offset */
	uint16_t	ee_data;	/* write data */
} HAL_DIAG_EEVAL;


typedef struct {
	u_int offset;		/* reg offset */
	uint32_t val;		/* reg value  */
} HAL_DIAG_REGVAL;

/*
 * 11n compatibility tweaks.
 */
#define	HAL_DIAG_11N_SERVICES	0x00000003
#define	HAL_DIAG_11N_SERVICES_S	0
#define	HAL_DIAG_11N_TXSTOMP	0x0000000c
#define	HAL_DIAG_11N_TXSTOMP_S	2

typedef struct {
	int		maxNoiseImmunityLevel;	/* [0..4] */
	int		totalSizeDesired[5];
	int		coarseHigh[5];
	int		coarseLow[5];
	int		firpwr[5];

	int		maxSpurImmunityLevel;	/* [0..7] */
	int		cycPwrThr1[8];

	int		maxFirstepLevel;	/* [0..2] */
	int		firstep[3];

	uint32_t	ofdmTrigHigh;
	uint32_t	ofdmTrigLow;
	int32_t		cckTrigHigh;
	int32_t		cckTrigLow;
	int32_t		rssiThrLow;
	int32_t		rssiThrHigh;

	int		period;			/* update listen period */
} HAL_ANI_PARAMS;

extern	HAL_BOOL ath_hal_getdiagstate(struct ath_hal *ah, int request,
			const void *args, uint32_t argsize,
			void **result, uint32_t *resultsize);

/*
 * Setup a h/w rate table for use.
 */
extern	void ath_hal_setupratetable(struct ath_hal *ah, HAL_RATE_TABLE *rt);

/*
 * Common routine for implementing getChanNoise api.
 */
int16_t	ath_hal_getChanNoise(struct ath_hal *, const struct ieee80211_channel *);

/*
 * Initialization support.
 */
typedef struct {
	const uint32_t	*data;
	int		rows, cols;
} HAL_INI_ARRAY;

#define	HAL_INI_INIT(_ia, _data, _cols) do {			\
	(_ia)->data = (const uint32_t *)(_data);		\
	(_ia)->rows = sizeof(_data) / sizeof((_data)[0]);	\
	(_ia)->cols = (_cols);					\
} while (0)
#define	HAL_INI_VAL(_ia, _r, _c) \
	((_ia)->data[((_r)*(_ia)->cols) + (_c)])

/*
 * OS_DELAY() does a PIO READ on the PCI bus which allows
 * other cards' DMA reads to complete in the middle of our reset.
 */
#define DMA_YIELD(x) do {		\
	if ((++(x) % 64) == 0)		\
		OS_DELAY(1);		\
} while (0)

#define HAL_INI_WRITE_ARRAY(ah, regArray, col, regWr) do {             	\
	int r;								\
	for (r = 0; r < N(regArray); r++) {				\
		OS_REG_WRITE(ah, (regArray)[r][0], (regArray)[r][col]);	\
		DMA_YIELD(regWr);					\
	}								\
} while (0)

#define HAL_INI_WRITE_BANK(ah, regArray, bankData, regWr) do {		\
	int r;								\
	for (r = 0; r < N(regArray); r++) {				\
		OS_REG_WRITE(ah, (regArray)[r][0], (bankData)[r]);	\
		DMA_YIELD(regWr);					\
	}								\
} while (0)

extern	int ath_hal_ini_write(struct ath_hal *ah, const HAL_INI_ARRAY *ia,
		int col, int regWr);
extern	void ath_hal_ini_bank_setup(uint32_t data[], const HAL_INI_ARRAY *ia,
		int col);
extern	int ath_hal_ini_bank_write(struct ath_hal *ah, const HAL_INI_ARRAY *ia,
		const uint32_t data[], int regWr);

#define	CCK_SIFS_TIME		10
#define	CCK_PREAMBLE_BITS	144
#define	CCK_PLCP_BITS		48

#define	OFDM_SIFS_TIME		16
#define	OFDM_PREAMBLE_TIME	20
#define	OFDM_PLCP_BITS		22
#define	OFDM_SYMBOL_TIME	4

#define	OFDM_HALF_SIFS_TIME	32
#define	OFDM_HALF_PREAMBLE_TIME	40
#define	OFDM_HALF_PLCP_BITS	22
#define	OFDM_HALF_SYMBOL_TIME	8

#define	OFDM_QUARTER_SIFS_TIME 		64
#define	OFDM_QUARTER_PREAMBLE_TIME	80
#define	OFDM_QUARTER_PLCP_BITS		22
#define	OFDM_QUARTER_SYMBOL_TIME	16

#define	TURBO_SIFS_TIME		8
#define	TURBO_PREAMBLE_TIME	14
#define	TURBO_PLCP_BITS		22
#define	TURBO_SYMBOL_TIME	4

#define	WLAN_CTRL_FRAME_SIZE	(2+2+6+4)	/* ACK+FCS */

/* Generic EEPROM board value functions */
extern	HAL_BOOL ath_ee_getLowerUpperIndex(uint8_t target, uint8_t *pList,
	uint16_t listSize, uint16_t *indexL, uint16_t *indexR);
extern	HAL_BOOL ath_ee_FillVpdTable(uint8_t pwrMin, uint8_t pwrMax,
	uint8_t *pPwrList, uint8_t *pVpdList, uint16_t numIntercepts,
	uint8_t *pRetVpdList);
extern	int16_t ath_ee_interpolate(uint16_t target, uint16_t srcLeft,
	uint16_t srcRight, int16_t targetLeft, int16_t targetRight);

/* Whether 5ghz fast clock is needed */
/*
 * The chipset (Merlin, AR9300/later) should set the capability flag below;
 * this flag simply says that the hardware can do it, not that the EEPROM
 * says it can.
 *
 * Merlin 2.0/2.1 chips with an EEPROM version > 16 do 5ghz fast clock
 *   if the relevant eeprom flag is set.
 * Merlin 2.0/2.1 chips with an EEPROM version <= 16 do 5ghz fast clock
 *   by default.
 */
#define	IS_5GHZ_FAST_CLOCK_EN(_ah, _c) \
	(IEEE80211_IS_CHAN_5GHZ(_c) && \
	 AH_PRIVATE((_ah))->ah_caps.halSupportsFastClock5GHz && \
	ath_hal_eepromGetFlag((_ah), AR_EEP_FSTCLK_5G))

/*
 * Fetch the maximum regulatory domain power for the given channel
 * in 1/2dBm steps.
 */
static inline int
ath_hal_get_twice_max_regpower(struct ath_hal_private *ahp,
    const HAL_CHANNEL_INTERNAL *ichan, const struct ieee80211_channel *chan)
{
	struct ath_hal *ah = &ahp->h;

	if (! chan) {
		ath_hal_printf(ah, "%s: called with chan=NULL!\n", __func__);
		return (0);
	}
	return (chan->ic_maxpower);
}

/*
 * Get the maximum antenna gain allowed, in 1/2dBm steps.
 */
static inline int
ath_hal_getantennaallowed(struct ath_hal *ah,
    const struct ieee80211_channel *chan)
{

	if (! chan)
		return (0);

	return (chan->ic_maxantgain);
}

/*
 * Map the given 2GHz channel to an IEEE number.
 */
extern	int ath_hal_mhz2ieee_2ghz(struct ath_hal *, int freq);

/*
 * Clear the channel survey data.
 */
extern	void ath_hal_survey_clear(struct ath_hal *ah);

/*
 * Add a sample to the channel survey data.
 */
extern	void ath_hal_survey_add_sample(struct ath_hal *ah,
	    HAL_SURVEY_SAMPLE *hs);

/*
 * Chip registration - for modules.
 */
extern	int ath_hal_add_chip(struct ath_hal_chip *ahc);
extern	int ath_hal_remove_chip(struct ath_hal_chip *ahc);
extern	int ath_hal_add_rf(struct ath_hal_rf *arf);
extern	int ath_hal_remove_rf(struct ath_hal_rf *arf);

#endif /* _ATH_AH_INTERAL_H_ */
