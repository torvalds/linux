/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Marvell Semiconductor, Inc.
 * Copyright (c) 2007 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Weongyo Jeong <weongyo@freebsd.org>
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

#ifndef _DEV_MALOHAL_H
#define _DEV_MALOHAL_H

#define MALO_NUM_TX_QUEUES			1
#define MALO_MAX_TXWCB_QUEUES			MALO_NUM_TX_QUEUES

/* size of f/w command buffer */
#define	MALO_CMDBUF_SIZE			0x4000

#define MALO_FW_CHECK_USECS			(5 * 1000)	/* 5ms */
#define MALO_FW_MAX_NUM_CHECKS			200  

/*
 * Calibration data builtin to the firmware.  The firmware image
 * has a single set of calibration tables that we retrieve right
 * after download.  This can be overriden by the driver (e.g. for
 * a different regdomain and/or tx power setup).
 */
struct malo_hal_caldata {
	/* pt is short for `power target'.  */
#define	MALO_PWTAGETRATETABLE20M		(14 * 4)
	uint8_t	pt_ratetable_20m[MALO_PWTAGETRATETABLE20M];
};

/*
 * Get Hardware/Firmware capabilities.
 */
struct malo_hal_hwspec {
	uint8_t		hwversion;		/* version of the HW */
	uint8_t		hostinterface;	/* host interface */
	uint16_t	maxnum_wcb;		/* max # of WCB FW handles */
	/* max # of mcast addresses FW handles*/
	uint16_t	maxnum_mcaddr;
	uint16_t	maxnum_tx_wcb;	/* max # of tx descs per WCB */
	/* MAC address programmed in HW */
	uint8_t		macaddr[6];
	uint16_t	regioncode;		/* EEPROM region code */
	uint16_t	num_antenna;	/* Number of antenna used */
	uint32_t	fw_releasenum;	/* firmware release number */
	uint32_t	wcbbase0;
	uint32_t	rxdesc_read;
	uint32_t	rxdesc_write;
	uint32_t	ul_fw_awakecookie;
	uint32_t	wcbbase[4];
};

/*
 * Supply tx/rx dma-related settings to the firmware.
 */
struct malo_hal_txrxdma {
	uint32_t	maxnum_wcb;		/* max # of WCB FW handles */
	uint32_t	maxnum_txwcb;		/* max # of tx descs per WCB */
	uint32_t	rxdesc_read;
	uint32_t	rxdesc_write;
	uint32_t	wcbbase[4];
};

/*
 * Get Hardware Statistics.
 *
 * Items marked with ! are deprecated and not ever updated.  In
 * some cases this is because work has been moved to the host (e.g.
 * rx defragmentation).
 *
 * XXX low/up cases.
 */
struct malo_hal_hwstats {
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

/*
 * Set Antenna Configuration (legacy operation).
 *
 * The RX antenna can be selected using the bitmask
 * ant (bit 0 = antenna 1, bit 1 = antenna 2, etc.)
 * (diversity?XXX)
 */
enum malo_hal_antenna {
	MHA_ANTENNATYPE_RX	= 1,
	MHA_ANTENNATYPE_TX	= 2,
};

/*
 * Set Radio Configuration.
 *
 * onoff != 0 turns radio on; otherwise off.
 * if radio is enabled, the preamble is set too.
 */
enum malo_hal_preamble {
	MHP_LONG_PREAMBLE	= 1,
	MHP_SHORT_PREAMBLE	= 3,
	MHP_AUTO_PREAMBLE	= 5,
};

struct malo_hal_channel_flags {
	uint32_t		freqband : 6,
#define MALO_FREQ_BAND_2DOT4GHZ	0x1 
				: 26;		/* reserved */
};

struct malo_hal_channel {
	uint32_t		channel;
	struct malo_hal_channel_flags flags;
};

struct malo_hal_txrate {
	uint8_t			mcastrate;	/* rate for multicast frames */
	uint8_t			mgtrate;	/* rate for management frames */
	struct {
		uint8_t		trycount;	/* try this many times */
		uint8_t		rate;		/* use this tx rate */
	} rateseries[4];			/* rate series */
};

struct malo_hal {
	device_t		mh_dev;

	bus_space_handle_t	mh_ioh;		/* BAR 1 copied from softc */
	bus_space_tag_t		mh_iot;
	uint32_t		mh_imask;	/* interrupt mask */
	int			mh_flags;
#define	MHF_CALDATA		0x0001		/* cal data retrieved */
#define	MHF_FWHANG		0x0002		/* fw appears hung */

	char			mh_mtxname[12];
	struct mtx		mh_mtx;
	bus_dma_tag_t		mh_dmat;	/* bus DMA tag for cmd buffer */
	bus_dmamap_t		mh_dmamap;	/* DMA map for cmd buffer */
	uint16_t		*mh_cmdbuf;	/* f/w cmd buffer */
	bus_addr_t		mh_cmdaddr;	/* physaddr of cmd buffer */

	struct malo_hal_caldata	mh_caldata;

	int			mh_debug;
#define MALO_HAL_DEBUG_SENDCMD	0x00000001
#define MALO_HAL_DEBUG_CMDDONE	0x00000002
#define MALO_HAL_DEBUG_IGNHANG	0X00000004
};

#define MALO_HAL_LOCK(mh)		mtx_lock(&mh->mh_mtx)
#define MALO_HAL_LOCK_ASSERT(mh)	mtx_assert(&mh->mh_mtx, MA_OWNED)
#define MALO_HAL_UNLOCK(mh)		mtx_unlock(&mh->mh_mtx)

struct malo_hal *malo_hal_attach(device_t, uint16_t,
	    bus_space_handle_t, bus_space_tag_t,
	    bus_dma_tag_t);
int	malo_hal_fwload(struct malo_hal *, char *, char *);
int	malo_hal_gethwspecs(struct malo_hal *,
	    struct malo_hal_hwspec *);
void	malo_hal_detach(struct malo_hal *);
void	malo_hal_intrset(struct malo_hal *, uint32_t);
int	malo_hal_setantenna(struct malo_hal *,
	    enum malo_hal_antenna, int);
int	malo_hal_setradio(struct malo_hal *, int,
	    enum malo_hal_preamble);
int	malo_hal_setchannel(struct malo_hal *,
	    const struct malo_hal_channel *);
int	malo_hal_setmaxtxpwr(struct malo_hal *, uint16_t);
int	malo_hal_settxpower(struct malo_hal *, const struct malo_hal_channel *);
int	malo_hal_setpromisc(struct malo_hal *, int);
int	malo_hal_setassocid(struct malo_hal *,
	    const uint8_t[], uint16_t);
void	malo_hal_txstart(struct malo_hal *, int);
void	malo_hal_getisr(struct malo_hal *, uint32_t *);
void	malo_hal_cmddone(struct malo_hal *);
int	malo_hal_prescan(struct malo_hal *);
int	malo_hal_postscan(struct malo_hal *, uint8_t *, uint8_t);
int	malo_hal_set_slot(struct malo_hal *, int);
int	malo_hal_set_rate(struct malo_hal *, uint16_t, uint8_t);
int	malo_hal_setmcast(struct malo_hal *, int, const uint8_t[]);

#endif
