/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Marvell Semiconductor, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#ifndef _MWL_HAL_H_
#define	_MWL_HAL_H_
/*
 * Hardware Access Layer for Marvell Wireless Devices.
 */

#define MWL_MBSS_SUPPORT		/* enable multi-bss support */

/*
 * Define total number of TX queues in the shared memory.
 * This count includes the EDCA queues, Block Ack queues, and HCCA queues
 * In addition to this, there could be a management packet queue some 
 * time in the future
 */
#define MWL_NUM_EDCA_QUEUES	4
#define MWL_NUM_HCCA_QUEUES	0
#define MWL_NUM_BA_QUEUES	0
#define MWL_NUM_MGMT_QUEUES	0
#define MWL_NUM_ACK_QUEUES	0
#define MWL_NUM_TX_QUEUES \
	(MWL_NUM_EDCA_QUEUES + MWL_NUM_HCCA_QUEUES + MWL_NUM_BA_QUEUES + \
	 MWL_NUM_MGMT_QUEUES + MWL_NUM_ACK_QUEUES)
#define MWL_MAX_RXWCB_QUEUES	1

#define MWL_MAX_SUPPORTED_RATES	12
#define MWL_MAX_SUPPORTED_MCS	32

typedef enum {
	MWL_HAL_OK
} MWL_HAL_STATUS;

/*
 * Transmit queue assignment.
 */
enum {
	MWL_WME_AC_BK	= 0,		/* background access category */
	MWL_WME_AC_BE	= 1, 		/* best effort access category*/
	MWL_WME_AC_VI	= 2,		/* video access category */
	MWL_WME_AC_VO	= 3,		/* voice access category */
};

struct mwl_hal {
	bus_space_handle_t mh_ioh;	/* BAR 1 copied from softc */
	bus_space_tag_t	mh_iot;
	uint32_t	mh_imask;	/* interrupt mask */
	/* remainder is opaque to driver */
};
struct mwl_hal *mwl_hal_attach(device_t dev, uint16_t devid,
    bus_space_handle_t ioh, bus_space_tag_t iot, bus_dma_tag_t tag);
void	mwl_hal_detach(struct mwl_hal *);

/*
 * Query whether multi-bss support is available/enabled.
 */
int	mwl_hal_ismbsscapable(struct mwl_hal *);

typedef enum {
	MWL_HAL_AP,
	MWL_HAL_STA,			/* infrastructure mode */
	MWL_HAL_IBSS			/* ibss/adhoc mode */
} MWL_HAL_BSSTYPE;
struct mwl_hal_vap;

struct mwl_hal_vap *mwl_hal_newvap(struct mwl_hal *, MWL_HAL_BSSTYPE,
    const uint8_t mac[6]);
void	mwl_hal_delvap(struct mwl_hal_vap *);

enum {
	MWL_HAL_DEBUG_SENDCMD	= 0x00000001,
	MWL_HAL_DEBUG_CMDDONE	= 0x00000002,
	MWL_HAL_DEBUG_IGNHANG	= 0x00000004,
};
void	mwl_hal_setdebug(struct mwl_hal *, int);
int	mwl_hal_getdebug(struct mwl_hal *);

typedef struct {
	uint16_t freqLow, freqHigh;
	int nchannels;
	struct mwl_hal_channel {
		uint16_t freq;		/* channel center */
		uint8_t ieee;		/* channel number */
		int8_t maxTxPow;	/* max tx power (dBm) */
		uint8_t targetPowers[4];/* target powers (dBm) */
#define	MWL_HAL_MAXCHAN	40
	} channels[MWL_HAL_MAXCHAN];
} MWL_HAL_CHANNELINFO;
int	mwl_hal_getchannelinfo(struct mwl_hal *, int band, int chw,
	    const MWL_HAL_CHANNELINFO **);

/*
 * Return the current ISR setting and clear the cause.
 */
static __inline void
mwl_hal_getisr(struct mwl_hal *mh, uint32_t *status)
{
#define MACREG_REG_A2H_INTERRUPT_CAUSE      	0x00000C30 // (From ARM to host)
#define MACREG_REG_INT_CODE                 0x00000C14
	uint32_t cause;

	cause = bus_space_read_4(mh->mh_iot, mh->mh_ioh,
			MACREG_REG_A2H_INTERRUPT_CAUSE);
	if (cause == 0xffffffff) {	/* card removed */
		cause = 0;
	} else if (cause != 0) {
		/* clear cause bits */
		bus_space_write_4(mh->mh_iot, mh->mh_ioh,
			MACREG_REG_A2H_INTERRUPT_CAUSE, cause &~ mh->mh_imask);
		(void) bus_space_read_4(mh->mh_iot, mh->mh_ioh,
				MACREG_REG_INT_CODE);
		cause &= mh->mh_imask;
	}
	*status = cause;
#undef MACREG_REG_INT_CODE
#undef MACREG_REG_A2H_INTERRUPT_CAUSE
}

void	mwl_hal_intrset(struct mwl_hal *mh, uint32_t mask);

/*
 * Kick the firmware to tell it there are new tx descriptors
 * for processing.  The driver says what h/w q has work in
 * case the f/w ever gets smarter.
 */
static __inline void
mwl_hal_txstart(struct mwl_hal *mh, int qnum)
{
#define MACREG_REG_H2A_INTERRUPT_EVENTS     	0x00000C18 // (From host to ARM)
#define MACREG_H2ARIC_BIT_PPA_READY         0x00000001 // bit 0
#define MACREG_REG_INT_CODE                 0x00000C14

	bus_space_write_4(mh->mh_iot, mh->mh_ioh,
		MACREG_REG_H2A_INTERRUPT_EVENTS, MACREG_H2ARIC_BIT_PPA_READY);
	(void) bus_space_read_4(mh->mh_iot, mh->mh_ioh, MACREG_REG_INT_CODE);
#undef MACREG_REG_INT_CODE
#undef MACREG_H2ARIC_BIT_PPA_READY
#undef MACREG_REG_H2A_INTERRUPT_EVENTS
}

void	mwl_hal_cmddone(struct mwl_hal *mh);

typedef struct {
    uint32_t	FreqBand : 6,
#define MWL_FREQ_BAND_2DOT4GHZ	0x1 
#define MWL_FREQ_BAND_5GHZ     	0x4
		ChnlWidth: 5,
#define MWL_CH_10_MHz_WIDTH  	0x1
#define MWL_CH_20_MHz_WIDTH  	0x2
#define MWL_CH_40_MHz_WIDTH  	0x4
		ExtChnlOffset: 2,
#define MWL_EXT_CH_NONE		0x0
#define MWL_EXT_CH_ABOVE_CTRL_CH 0x1
#define MWL_EXT_CH_BELOW_CTRL_CH 0x3
			 : 19;		/* reserved */
} MWL_HAL_CHANNEL_FLAGS;

typedef struct {
    uint32_t	channel;
    MWL_HAL_CHANNEL_FLAGS channelFlags;
} MWL_HAL_CHANNEL;

/*
 * Get Hardware/Firmware capabilities.
 */
struct mwl_hal_hwspec {
	uint8_t    hwVersion;		/* version of the HW */
	uint8_t    hostInterface;	/* host interface */
	uint16_t   maxNumWCB;		/* max # of WCB FW handles */
	uint16_t   maxNumMCAddr;	/* max # of mcast addresses FW handles*/
	uint16_t   maxNumTxWcb;		/* max # of tx descs per WCB */
	uint8_t    macAddr[6];		/* MAC address programmed in HW */
	uint16_t   regionCode;		/* EEPROM region code */
	uint16_t   numAntennas;		/* Number of antenna used */
	uint32_t   fwReleaseNumber;	/* firmware release number */
	uint32_t   wcbBase0;
	uint32_t   rxDescRead;
	uint32_t   rxDescWrite;
	uint32_t   ulFwAwakeCookie;
	uint32_t   wcbBase[MWL_NUM_TX_QUEUES - MWL_NUM_ACK_QUEUES];
};
int	mwl_hal_gethwspecs(struct mwl_hal *mh, struct mwl_hal_hwspec *);

/*
 * Supply tx/rx dma-related settings to the firmware.
 */
struct mwl_hal_txrxdma {
	uint32_t   maxNumWCB;		/* max # of WCB FW handles */
	uint32_t   maxNumTxWcb;		/* max # of tx descs per WCB */
	uint32_t   rxDescRead;
	uint32_t   rxDescWrite;
	uint32_t   wcbBase[MWL_NUM_TX_QUEUES - MWL_NUM_ACK_QUEUES];
};
int	mwl_hal_sethwdma(struct mwl_hal *mh, const struct mwl_hal_txrxdma *);

/*
 * Get Hardware Statistics.
 *
 * Items marked with ! are deprecated and not ever updated.  In
 * some cases this is because work has been moved to the host (e.g.
 * rx defragmentation).
 */
struct mwl_hal_hwstats {
	uint32_t	TxRetrySuccesses;	/* tx success w/ 1 retry */
	uint32_t	TxMultipleRetrySuccesses;/* tx success w/ >1 retry */
	uint32_t	TxFailures;		/* tx fail due to no ACK */
	uint32_t	RTSSuccesses;		/* CTS rx'd for RTS */
	uint32_t	RTSFailures;		/* CTS not rx'd for RTS */
	uint32_t	AckFailures;		/* same as TxFailures */
	uint32_t	RxDuplicateFrames;	/* rx discard for dup seqno */
	uint32_t	FCSErrorCount;		/* rx discard for bad FCS */
	uint32_t	TxWatchDogTimeouts;	/* MAC tx hang (f/w recovery) */
	uint32_t	RxOverflows;		/* no f/w buffer for rx data */
	uint32_t	RxFragErrors;		/* !rx fail due to defrag */
	uint32_t	RxMemErrors;		/* out of mem or desc corrupted
						   in some way */
	uint32_t	RxPointerErrors;	/* MAC internal ptr problem */
	uint32_t	TxUnderflows;		/* !tx underflow on dma */
	uint32_t	TxDone;			/* MAC tx ops completed
						   (possibly w/ error) */
	uint32_t	TxDoneBufTryPut;	/* ! */
	uint32_t	TxDoneBufPut;		/* same as TxDone */
	uint32_t	Wait4TxBuf;		/* !no f/w buf avail when
						    supplied a tx descriptor */
	uint32_t	TxAttempts;		/* tx descriptors processed */
	uint32_t	TxSuccesses;		/* tx attempts successful */ 
	uint32_t	TxFragments;		/* tx with fragmentation */
	uint32_t	TxMulticasts;		/* tx multicast frames */
	uint32_t	RxNonCtlPkts;		/* rx non-control frames */
	uint32_t	RxMulticasts;		/* rx multicast frames */
	uint32_t	RxUndecryptableFrames;	/* rx failed due to crypto */
	uint32_t 	RxICVErrors;		/* rx failed due to ICV check */
	uint32_t	RxExcludedFrames;	/* rx discarded, e.g. bssid */
};
int	mwl_hal_gethwstats(struct mwl_hal *mh, struct mwl_hal_hwstats *);

/*
 * Set HT Guard Interval.
 *
 * GIType = 0:	enable long and short GI
 * GIType = 1:	enable short GI
 * GIType = 2:	enable long GI
 */
int	mwl_hal_sethtgi(struct mwl_hal_vap *, int GIType);

/*
 * Set Radio Configuration.
 *
 * onoff != 0 turns radio on; otherwise off.
 * if radio is enabled, the preamble is set too.
 */
typedef enum {
	WL_LONG_PREAMBLE = 1,
	WL_SHORT_PREAMBLE = 3,
	WL_AUTO_PREAMBLE = 5,
} MWL_HAL_PREAMBLE;
int	mwl_hal_setradio(struct mwl_hal *mh, int onoff, MWL_HAL_PREAMBLE preamble);

/*
 * Set Antenna Configuration (legacy operation).
 *
 * The RX antenna can be selected using the bitmask
 * ant (bit 0 = antenna 1, bit 1 = antenna 2, etc.)
 * (diversity?XXX)
 */
typedef enum {
	WL_ANTENNATYPE_RX = 1,
	WL_ANTENNATYPE_TX = 2,
} MWL_HAL_ANTENNA;
int	mwl_hal_setantenna(struct mwl_hal *mh, MWL_HAL_ANTENNA dirSet, int ant);

/*
 * Set the threshold for using RTS on TX.
 */
int	mwl_hal_setrtsthreshold(struct mwl_hal_vap *, int threshold);

/*
 * Set the adapter to operate in infrastructure mode.
 */
int	mwl_hal_setinframode(struct mwl_hal_vap *);

/*
 * Set Radar Detection Configuration.
 */
typedef enum {
	DR_DFS_DISABLE			= 0,
	DR_CHK_CHANNEL_AVAILABLE_START	= 1,
	DR_CHK_CHANNEL_AVAILABLE_STOP	= 2,
	DR_IN_SERVICE_MONITOR_START	= 3
} MWL_HAL_RADAR;
int	mwl_hal_setradardetection(struct mwl_hal *mh, MWL_HAL_RADAR action);
/*
 * Set the region code that selects the radar bin'ing agorithm.
 */
int	mwl_hal_setregioncode(struct mwl_hal *mh, int regionCode);

/*
 * Initiate an 802.11h-based channel switch.  The CSA ie
 * is included in the next beacon(s) using the specified
 * information and the firmware counts down until switch
 * time after which it notifies the driver by delivering
 * an interrupt with MACREG_A2HRIC_BIT_CHAN_SWITCH set in
 * the cause register.
 */
int	mwl_hal_setchannelswitchie(struct mwl_hal *,
	   const MWL_HAL_CHANNEL *nextchan, uint32_t mode, uint32_t count);

/*
 * Set regdomain code (IEEE SKU).
 */
enum {
	DOMAIN_CODE_FCC		= 0x10,	/* USA */
	DOMAIN_CODE_IC		= 0x20,	/* Canda */
	DOMAIN_CODE_ETSI	= 0x30,	/* Europe */
	DOMAIN_CODE_SPAIN	= 0x31,	/* Spain */
	DOMAIN_CODE_FRANCE	= 0x32,	/* France */
	DOMAIN_CODE_ETSI_131	= 0x130,/* ETSI w/ 1.3.1 radar type */
	DOMAIN_CODE_MKK		= 0x40,	/* Japan */
	DOMAIN_CODE_MKK2	= 0x41,	/* Japan w/ 10MHz chan spacing */
	DOMAIN_CODE_DGT		= 0x80,	/* Taiwan */
	DOMAIN_CODE_AUS		= 0x81,	/* Australia */
};

/*
 * Transmit rate control.  Rate codes with bit 0x80 set are
 * interpreted as MCS codes (this limits us to 0-127).  The
 * transmit rate can be set to a single fixed rate or can
 * be configured to start at an initial rate and drop based
 * on retry counts.
 */
typedef enum {
	RATE_AUTO	= 0,	/* rate selected by firmware */
	RATE_FIXED	= 2,	/* rate fixed */
	RATE_FIXED_DROP	= 1,	/* rate starts fixed but may drop */
} MWL_HAL_TXRATE_HANDLING;

typedef struct {
	uint8_t	McastRate;	/* rate for multicast frames */
#define	RATE_MCS	0x80	/* rate is an MCS index */
	uint8_t	MgtRate;	/* rate for management frames */
	struct {
	    uint8_t TryCount;	/* try this many times */
	    uint8_t Rate;	/* use this tx rate */
	} RateSeries[4];	/* rate series */
} MWL_HAL_TXRATE;

int	mwl_hal_settxrate(struct mwl_hal_vap *,
	    MWL_HAL_TXRATE_HANDLING handling, const MWL_HAL_TXRATE *rate);
/* NB: hack for setting rates while scanning */
int	mwl_hal_settxrate_auto(struct mwl_hal *, const MWL_HAL_TXRATE *rate);

/*
 * Set the Slot Time Configuration.
 * NB: usecs must either be 9 or 20 for now.
 */
int	mwl_hal_setslottime(struct mwl_hal *mh, int usecs);

/*
 * Adjust current transmit power settings according to powerLevel.
 * This translates to low/medium/high use of the current tx power rate tables.
 */
int	mwl_hal_adjusttxpower(struct mwl_hal *, uint32_t powerLevel);
/*
 * Set the transmit power for the specified channel; the power
 * is taken from the calibration data and capped according to
 * the specified max tx power (in dBm).
 */
int	mwl_hal_settxpower(struct mwl_hal *, const MWL_HAL_CHANNEL *,
	    uint8_t maxtxpow);

/*
 * Set the Multicast Address Filter.
 * A packed array addresses is specified.
 */
#define	MWL_HAL_MCAST_MAX	32
int	mwl_hal_setmcast(struct mwl_hal *mh, int nmc, const uint8_t macs[]);

/*
 * Crypto Configuration.
 */
typedef struct {
    uint16_t  pad;
    uint16_t  keyTypeId;
#define KEY_TYPE_ID_WEP		0
#define KEY_TYPE_ID_TKIP	1
#define KEY_TYPE_ID_AES		2	/* AES-CCMP */
    uint32_t  keyFlags;
#define KEY_FLAG_INUSE		0x00000001	/* indicate key is in use */
#define KEY_FLAG_RXGROUPKEY	0x00000002	/* Group key for RX only */
#define KEY_FLAG_TXGROUPKEY	0x00000004	/* Group key for TX */
#define KEY_FLAG_PAIRWISE	0x00000008	/* pairwise */
#define KEY_FLAG_RXONLY		0x00000010	/* only used for RX */
#define KEY_FLAG_AUTHENTICATOR	0x00000020	/* Key is for Authenticator */
#define KEY_FLAG_TSC_VALID	0x00000040	/* Sequence counters valid */
#define KEY_FLAG_WEP_TXKEY	0x01000000	/* Tx key for WEP */
#define KEY_FLAG_MICKEY_VALID	0x02000000	/* Tx/Rx MIC keys are valid */
    uint32_t  keyIndex; 	/* for WEP only; actual key index */
    uint16_t  keyLen;		/* key size in bytes */
    union {			/* key material, keyLen gives size */
	uint8_t	wep[16];	/* enough for 128 bits */
	uint8_t	aes[16];
	struct {
	    /* NB: group or pairwise key is determined by keyFlags */
	    uint8_t keyMaterial[16];
	    uint8_t txMic[8];
	    uint8_t rxMic[8];
	    struct {
	        uint16_t low;
		uint32_t high;
	    } rsc;
	    struct {
	        uint16_t low;
		uint32_t high;
	    } tsc;
	} __packed tkip;
    }__packed key;
} __packed MWL_HAL_KEYVAL;

/*
 * Plumb a unicast/group key.  The mac address identifies
 * the station, use the broadcast address for group keys.
 */
int	mwl_hal_keyset(struct mwl_hal_vap *, const MWL_HAL_KEYVAL *kv,
		const uint8_t mac[6]);

/*
 * Plumb a unicast/group key.  The mac address identifies
 * the station, use the broadcast address for group keys.
 */
int	mwl_hal_keyreset(struct mwl_hal_vap *, const MWL_HAL_KEYVAL *kv,
		const uint8_t mac[6]);

/*
 * Set the MAC address.
 */
int	mwl_hal_setmac(struct mwl_hal_vap *, const uint8_t addr[6]);

/*
 * Set the beacon frame contents.  The firmware will modify the
 * frame only to add CSA and WME ie's and to fill in dynamic fields
 * such as the sequence #..
 */
int	mwl_hal_setbeacon(struct mwl_hal_vap *, const void *, size_t);

/*
 * Handle power save operation for AP operation when offloaded to
 * the host (SET_HW_SPEC_HOST_POWERSAVE).  mwl_hal_setbss_powersave
 * informs the firmware whether 1+ associated stations are in power
 * save mode (it will then buffer mcast traffic). mwl_hal_setsta_powersave
 * specifies a change in power save state for an associated station.
 */
int	mwl_hal_setpowersave_bss(struct mwl_hal_vap *, uint8_t nsta);
int	mwl_hal_setpowersave_sta(struct mwl_hal_vap *, uint16_t aid, int ena);

/*
 * Set Association Configuration for station operation.
 */
int	mwl_hal_setassocid(struct mwl_hal_vap *, const uint8_t bssId[6],
	    uint16_t assocId);

/*
 * Set the current channel.
 */
int	mwl_hal_setchannel(struct mwl_hal *mh, const MWL_HAL_CHANNEL *c);

/*
 * A-MPDU Block Ack (BA) stream support.  There are several
 * streams that the driver must multiplex.  Once assigned
 * to a station the driver queues frames to a corresponding
 * transmit queue and the firmware handles all the work.
 *
 * XXX no way to find out how many streams are supported
 */
typedef struct {
	void	*data[2];	/* opaque data */
	int	txq;
} MWL_HAL_BASTREAM;

const MWL_HAL_BASTREAM *mwl_hal_bastream_alloc(struct mwl_hal_vap *,
	    int ba_type, const uint8_t Macaddr[6], uint8_t Tid,
	    uint8_t ParamInfo, void *, void *);
const MWL_HAL_BASTREAM *mwl_hal_bastream_lookup(struct mwl_hal *mh, int s);
int	mwl_hal_bastream_create(struct mwl_hal_vap *, const MWL_HAL_BASTREAM *,
	    int BarThrs, int WindowSize, uint16_t seqno);
int	mwl_hal_bastream_destroy(struct mwl_hal *mh, const MWL_HAL_BASTREAM *);
int	mwl_hal_getwatchdogbitmap(struct mwl_hal *mh, uint8_t bitmap[1]);
int	mwl_hal_bastream_get_seqno(struct mwl_hal *mh, const MWL_HAL_BASTREAM *,
	    const uint8_t Macaddr[6], uint16_t *pseqno);
/* for sysctl hookup for debugging */
void	mwl_hal_setbastreams(struct mwl_hal *mh, int mask);
int	mwl_hal_getbastreams(struct mwl_hal *mh);

/*
 * Set/get A-MPDU aggregation parameters.
 */
int	mwl_hal_setaggampduratemode(struct mwl_hal *, int mode, int thresh);
int	mwl_hal_getaggampduratemode(struct mwl_hal *, int *mode, int *thresh);

/*
 * Inform the firmware of a new association station.
 * The address is the MAC address of the peer station.
 * The AID is supplied sans the 0xc000 bits.  The station
 * ID is defined by the caller.  The peer information must
 * be supplied.
 *
 * NB: All values are in host byte order; any byte swapping
 *     is handled by the hal.
 */
typedef struct {
	uint32_t LegacyRateBitMap;
	uint32_t HTRateBitMap;
	uint16_t CapInfo;
	uint16_t HTCapabilitiesInfo;
	uint8_t	MacHTParamInfo;
	uint8_t	Rev;
	struct {
	    uint8_t ControlChan;
	    uint8_t AddChan;
	    uint8_t OpMode;
	    uint8_t stbc;
	} __packed AddHtInfo;
} __packed MWL_HAL_PEERINFO;
int	mwl_hal_newstation(struct mwl_hal_vap *, const uint8_t addr[6],
	   uint16_t aid, uint16_t sid, const MWL_HAL_PEERINFO *,
	   int isQosSta, int wmeInfo);
int	mwl_hal_delstation(struct mwl_hal_vap *, const uint8_t addr[6]);

/*
 * Prod the firmware to age packets on station power
 * save queues and reap frames on the tx aggregation q's.
 */
int	mwl_hal_setkeepalive(struct mwl_hal *mh);

typedef enum {
	AP_MODE_B_ONLY = 1,
	AP_MODE_G_ONLY = 2,
	AP_MODE_MIXED = 3,
	AP_MODE_N_ONLY = 4,
	AP_MODE_BandN = 5,
	AP_MODE_GandN = 6,
	AP_MODE_BandGandN = 7,
	AP_MODE_A_ONLY = 8,
	AP_MODE_AandG = 10,
	AP_MODE_AandN = 12,
} MWL_HAL_APMODE;
int	mwl_hal_setapmode(struct mwl_hal_vap *, MWL_HAL_APMODE);

/*
 * Enable/disable firmware operation.  mwl_hal_start is
 * also used to sync state updates, e.g. beacon frame
 * reconstruction after content changes.
 */
int	mwl_hal_stop(struct mwl_hal_vap *);
int	mwl_hal_start(struct mwl_hal_vap *);

/*
 * Add/Remove station from Power Save TIM handling.
 *
 * If set is non-zero the AID is enabled, if zero it is removed.
 */
int	mwl_hal_updatetim(struct mwl_hal_vap *, uint16_t aid, int set);

/*
 * Enable/disable 11g protection use.  This call specifies
 * the ERP information element flags to use.
 */
int	mwl_hal_setgprot(struct mwl_hal *, int);

/*
 * Enable/disable WMM support.
 */
int	mwl_hal_setwmm(struct mwl_hal *mh, int onoff);

/*
 * Configure WMM EDCA parameters for the specified h/w ring.
 */
int	mwl_hal_setedcaparams(struct mwl_hal *mh, uint8_t qnum,
	   uint32_t CWmin, uint32_t CWmax, uint8_t AIFSN,  uint16_t TXOPLimit);

/*
 * Configure rate adaptation for indooor/outdoor operation.
 * XXX wtf?
 */
int	mwl_hal_setrateadaptmode(struct mwl_hal *mh, uint16_t mode);

typedef enum {
	CSMODE_CONSERVATIVE = 0,
	CSMODE_AGGRESSIVE = 1,
	CSMODE_AUTO_ENA = 2,
	CSMODE_AUTO_DIS = 3,
} MWL_HAL_CSMODE;
int	mwl_hal_setcsmode(struct mwl_hal *mh, MWL_HAL_CSMODE csmode);

/*
 * Configure 11n protection on/off.
 */
typedef enum {
	HTPROTECT_NONE	 = 0,		/* disable */
	HTPROTECT_OPT	 = 1,		/* optional */
	HTPROTECT_HT20	 = 2,		/* protect only HT20 */
	HTPROTECT_HT2040 = 3,		/* protect HT20/40 */
	HTPROTECT_AUTO	 = 4,		/* automatic */
}  MWL_HAL_HTPROTECT;
int	mwl_hal_setnprot(struct mwl_hal_vap *, MWL_HAL_HTPROTECT mode);
/*
 * Configure 11n protection mechanism for when protection is enabled.
 */
int	mwl_hal_setnprotmode(struct mwl_hal_vap *, uint8_t mode);

/*
 * Enable/disable Marvell "turbo mode"".
 */
int	mwl_hal_setoptimizationlevel(struct mwl_hal *mh, int onoff);

/*
 * Set MIMO Power Save handling for a station; the enable and mode
 * values come directly from the Action frame.
 */
int	mwl_hal_setmimops(struct mwl_hal *mh, const uint8_t addr[6],
	    uint8_t enable, uint8_t mode);

/*
 * Retrieve the region/country code from the EEPROM.
 */
int	mwl_hal_getregioncode(struct mwl_hal *mh, uint8_t *countryCode);
int	mwl_hal_GetBeacon(struct mwl_hal *mh, uint8_t *pBcn, uint16_t *pLen);
int	mwl_hal_SetRifs(struct mwl_hal *mh, uint8_t QNum);

/*
 * Set/get promiscuous mode.
 */
int	mwl_hal_setpromisc(struct mwl_hal *, int ena);
int	mwl_hal_getpromisc(struct mwl_hal *);

/*
 * Enable/disable CF-End use.
 */
int	mwl_hal_setcfend(struct mwl_hal *, int ena);

/*
 * Enable/disable sta-mode DWDS use/operation.
 */
int	mwl_hal_setdwds(struct mwl_hal *, int ena);

/*
 * Diagnostic interface.  This is an open-ended interface that
 * is opaque to applications.  Diagnostic programs use this to
 * retrieve internal data structures, etc.  There is no guarantee
 * that calling conventions for calls other than MWL_DIAG_REVS
 * are stable between HAL releases; a diagnostic application must
 * use the HAL revision information to deal with ABI/API differences.
 */
int	mwl_hal_getdiagstate(struct mwl_hal *mh, int request,
		const void *args, uint32_t argsize,
		void **result, uint32_t *resultsize);

int	mwl_hal_fwload(struct mwl_hal *mh, void *fwargs);
#endif /* _MWL_HAL_H_ */
