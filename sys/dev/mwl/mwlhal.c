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

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <sys/linker.h>
#include <sys/firmware.h>

#include <machine/bus.h>

#include <dev/mwl/mwlhal.h>
#include <dev/mwl/mwlreg.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <dev/mwl/mwldiag.h>

#define	MWLHAL_DEBUG			/* debug msgs */

typedef enum {
    WL_ANTENNAMODE_RX = 0xffff,
    WL_ANTENNAMODE_TX = 2,
} wlantennamode_e;

typedef enum {
    WL_TX_POWERLEVEL_LOW = 5,
    WL_TX_POWERLEVEL_MEDIUM = 10,
    WL_TX_POWERLEVEL_HIGH = 15,
} wltxpowerlevel_e;

#define	MWL_CMDBUF_SIZE	0x4000		/* size of f/w command buffer */
#define	MWL_BASTREAMS_MAX	7	/* max BA streams (NB: fw >3.3.5.9) */
#define	MWL_BAQID_MAX		8	/* max BA Q id's (NB: fw >3.3.5.9) */
#define	MWL_MBSS_AP_MAX		8	/* max ap vap's */
#define	MWL_MBSS_STA_MAX	24	/* max station/client vap's */
#define	MWL_MBSS_MAX	(MWL_MBSS_AP_MAX+MWL_MBSS_STA_MAX)

/*
 * BA stream -> queue ID mapping
 *
 * The first 2 streams map to h/w; the remaining streams are
 * implemented in firmware.
 */
static const int ba2qid[MWL_BASTREAMS_MAX] = {
	5, 6				/* h/w supported */
#if MWL_BASTREAMS_MAX == 7
	, 7, 0, 1, 2, 3 		/* f/w supported */
#endif
};
static int qid2ba[MWL_BAQID_MAX];

#define	IEEE80211_ADDR_LEN	6	/* XXX */
#define	IEEE80211_ADDR_COPY(_dst, _src) \
	memcpy(_dst, _src, IEEE80211_ADDR_LEN)
#define	IEEE80211_ADDR_EQ(_dst, _src) \
	(memcmp(_dst, _src, IEEE80211_ADDR_LEN) == 0)

#define	_CMD_SETUP(pCmd, type, cmd) do {				\
	pCmd = (type *)&mh->mh_cmdbuf[0];				\
	memset(pCmd, 0, sizeof(type));					\
	pCmd->CmdHdr.Cmd = htole16(cmd);				\
	pCmd->CmdHdr.Length = htole16(sizeof(type));			\
} while (0)

#define	_VCMD_SETUP(vap, pCmd, type, cmd) do {				\
	_CMD_SETUP(pCmd, type, cmd);					\
	pCmd->CmdHdr.MacId = vap->macid;				\
} while (0)

#define	PWTAGETRATETABLE20M	14*4
#define	PWTAGETRATETABLE40M	9*4
#define	PWTAGETRATETABLE20M_5G	35*4
#define	PWTAGETRATETABLE40M_5G	16*4

struct mwl_hal_bastream {
	MWL_HAL_BASTREAM public;	/* public state */
	uint8_t	stream;			/* stream # */
	uint8_t	setup;			/* f/w cmd sent */
	uint8_t ba_policy;		/* direct/delayed BA policy */
	uint8_t	tid;
	uint8_t	paraminfo;
	uint8_t macaddr[IEEE80211_ADDR_LEN];
};

struct mwl_hal_priv;

struct mwl_hal_vap {
	struct mwl_hal_priv *mh;	/* back pointer */
	uint16_t bss_type;		/* f/w type */
	uint8_t vap_type;		/* MWL_HAL_BSSTYPE */
	uint8_t	macid;			/* for passing to f/w */
	uint8_t flags;
#define	MVF_RUNNING	0x01		/* BSS_START issued */
#define	MVF_STATION	0x02		/* sta db entry created */
	uint8_t mac[IEEE80211_ADDR_LEN];/* mac address */
};
#define	MWLVAP(_vap)	((_vap)->mh)

/*
 * Per-device state.  We allocate a single cmd buffer for
 * submitting operations to the firmware.  Access to this
 * buffer (and the f/w) are single-threaded.  At present
 * we spin waiting for cmds to complete which is bad.  Not
 * sure if it's possible to submit multiple requests or
 * control when we get cmd done interrupts.  There's no
 * documentation and no example code to indicate what can
 * or cannot be done so all we can do right now is follow the
 * linux driver logic.  This falls apart when the f/w fails;
 * the system comes to a crawl as we spin waiting for operations
 * to finish.
 */
struct mwl_hal_priv {
	struct mwl_hal	public;		/* public area */
	device_t	mh_dev;
	char		mh_mtxname[12];
	struct mtx	mh_mtx;
	bus_dma_tag_t	mh_dmat;	/* bus DMA tag for cmd buffer */
	bus_dma_segment_t mh_seg;	/* segment for cmd buffer */
	bus_dmamap_t	mh_dmamap;	/* DMA map for cmd buffer */
	uint16_t	*mh_cmdbuf;	/* f/w cmd buffer */
	bus_addr_t	mh_cmdaddr;	/* physaddr of cmd buffer */
	int		mh_flags;
#define	MHF_CALDATA	0x0001		/* cal data retrieved */
#define	MHF_FWHANG	0x0002		/* fw appears hung */
#define	MHF_MBSS	0x0004		/* mbss enabled */
	struct mwl_hal_vap mh_vaps[MWL_MBSS_MAX+1];
	int		mh_bastreams;	/* bit mask of available BA streams */
	int		mh_regioncode;	/* XXX last region code sent to fw */
	struct mwl_hal_bastream mh_streams[MWL_BASTREAMS_MAX];
	int		mh_debug;
	MWL_HAL_CHANNELINFO mh_20M;
	MWL_HAL_CHANNELINFO mh_40M;
	MWL_HAL_CHANNELINFO mh_20M_5G;
	MWL_HAL_CHANNELINFO mh_40M_5G;
	int		mh_SDRAMSIZE_Addr;
	uint32_t	mh_RTSSuccesses;/* cumulative stats for read-on-clear */
	uint32_t	mh_RTSFailures;
	uint32_t	mh_RxDuplicateFrames;
	uint32_t	mh_FCSErrorCount;
	MWL_DIAG_REVS	mh_revs;
};
#define	MWLPRIV(_mh)	((struct mwl_hal_priv *)(_mh))

static int mwl_hal_setmac_locked(struct mwl_hal_vap *,
	const uint8_t addr[IEEE80211_ADDR_LEN]);
static int mwlExecuteCmd(struct mwl_hal_priv *, unsigned short cmd);
static int mwlGetPwrCalTable(struct mwl_hal_priv *);
#ifdef MWLHAL_DEBUG
static const char *mwlcmdname(int cmd);
static void dumpresult(struct mwl_hal_priv *, int showresult);
#endif /* MWLHAL_DEBUG */

SYSCTL_DECL(_hw_mwl);
static SYSCTL_NODE(_hw_mwl, OID_AUTO, hal, CTLFLAG_RD, 0,
    "Marvell HAL parameters");

static __inline void
MWL_HAL_LOCK(struct mwl_hal_priv *mh)
{
	mtx_lock(&mh->mh_mtx);
}

static __inline void
MWL_HAL_LOCK_ASSERT(struct mwl_hal_priv *mh)
{
	mtx_assert(&mh->mh_mtx, MA_OWNED);
}

static __inline void
MWL_HAL_UNLOCK(struct mwl_hal_priv *mh)
{
	mtx_unlock(&mh->mh_mtx);
}

static __inline uint32_t
RD4(struct mwl_hal_priv *mh, bus_size_t off)
{
	return bus_space_read_4(mh->public.mh_iot, mh->public.mh_ioh, off);
}

static __inline void
WR4(struct mwl_hal_priv *mh, bus_size_t off, uint32_t val)
{
	bus_space_write_4(mh->public.mh_iot, mh->public.mh_ioh, off, val);
}

static void
mwl_hal_load_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *paddr = (bus_addr_t*) arg;
	KASSERT(error == 0, ("error %u on bus_dma callback", error));
	*paddr = segs->ds_addr;
}

/*
 * Setup for communication with the device.  We allocate
 * a command buffer and map it for bus dma use.  The pci
 * device id is used to identify whether the device has
 * SRAM on it (in which case f/w download must include a
 * memory controller reset).  All bus i/o operations happen
 * in BAR 1; the driver passes in the tag and handle we need.
 */
struct mwl_hal *
mwl_hal_attach(device_t dev, uint16_t devid,
    bus_space_handle_t ioh, bus_space_tag_t iot, bus_dma_tag_t tag)
{
	struct mwl_hal_priv *mh;
	struct mwl_hal_vap *hvap;
	int error, i;

	mh = malloc(sizeof(struct mwl_hal_priv), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (mh == NULL)
		return NULL;
	mh->mh_dev = dev;
	mh->public.mh_ioh = ioh;
	mh->public.mh_iot = iot;
	for (i = 0; i < MWL_BASTREAMS_MAX; i++) {
		mh->mh_streams[i].public.txq = ba2qid[i];
		mh->mh_streams[i].stream = i;
		/* construct back-mapping while we're at it */
		if (mh->mh_streams[i].public.txq < MWL_BAQID_MAX)
			qid2ba[mh->mh_streams[i].public.txq] = i;
		else
			device_printf(dev, "unexpected BA tx qid %d for "
			    "stream %d\n", mh->mh_streams[i].public.txq, i);
	}
	/* setup constant portion of vap state */
	/* XXX should get max ap/client vap's from f/w */
	i = 0;
	hvap = &mh->mh_vaps[i];
	hvap->vap_type = MWL_HAL_AP;
	hvap->bss_type = htole16(WL_MAC_TYPE_PRIMARY_AP);
	hvap->macid = 0;
	for (i++; i < MWL_MBSS_AP_MAX; i++) {
		hvap = &mh->mh_vaps[i];
		hvap->vap_type = MWL_HAL_AP;
		hvap->bss_type = htole16(WL_MAC_TYPE_SECONDARY_AP);
		hvap->macid = i;
	}
	hvap = &mh->mh_vaps[i];
	hvap->vap_type = MWL_HAL_STA;
	hvap->bss_type = htole16(WL_MAC_TYPE_PRIMARY_CLIENT);
	hvap->macid = i;
	for (i++; i < MWL_MBSS_MAX; i++) {
		hvap = &mh->mh_vaps[i];
		hvap->vap_type = MWL_HAL_STA;
		hvap->bss_type = htole16(WL_MAC_TYPE_SECONDARY_CLIENT);
		hvap->macid = i;
	}
	mh->mh_revs.mh_devid = devid;
	snprintf(mh->mh_mtxname, sizeof(mh->mh_mtxname),
	    "%s_hal", device_get_nameunit(dev));
	mtx_init(&mh->mh_mtx, mh->mh_mtxname, NULL, MTX_DEF);

	/*
	 * Allocate the command buffer and map into the address
	 * space of the h/w.  We request "coherent" memory which
	 * will be uncached on some architectures.
	 */
	error = bus_dma_tag_create(tag,		/* parent */
		       PAGE_SIZE, 0,		/* alignment, bounds */
		       BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		       BUS_SPACE_MAXADDR,	/* highaddr */
		       NULL, NULL,		/* filter, filterarg */
		       MWL_CMDBUF_SIZE,		/* maxsize */
		       1,			/* nsegments */
		       MWL_CMDBUF_SIZE,		/* maxsegsize */
		       BUS_DMA_ALLOCNOW,	/* flags */
		       NULL,			/* lockfunc */
		       NULL,			/* lockarg */
		       &mh->mh_dmat);
	if (error != 0) {
		device_printf(dev, "unable to allocate memory for cmd tag, "
			"error %u\n", error);
		goto fail0;
	}

	/* allocate descriptors */
	error = bus_dmamem_alloc(mh->mh_dmat, (void**) &mh->mh_cmdbuf,
				 BUS_DMA_NOWAIT | BUS_DMA_COHERENT, 
				 &mh->mh_dmamap);
	if (error != 0) {
		device_printf(dev, "unable to allocate memory for cmd buffer, "
			"error %u\n", error);
		goto fail1;
	}

	error = bus_dmamap_load(mh->mh_dmat, mh->mh_dmamap,
				mh->mh_cmdbuf, MWL_CMDBUF_SIZE,
				mwl_hal_load_cb, &mh->mh_cmdaddr,
				BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(dev, "unable to load cmd buffer, error %u\n",
			error);
		goto fail2;
	}

	/*
	 * Some cards have SDRAM.  When loading firmware we need
	 * to reset the SDRAM controller prior to doing this.
	 * When the SDRAMSIZE is non-zero we do that work in
	 * mwl_hal_fwload.
	 */
	switch (devid) {
	case 0x2a02:		/* CB82 */
	case 0x2a03:		/* CB85 */
	case 0x2a08:		/* MC85_B1 */
	case 0x2a0b:		/* CB85AP */
	case 0x2a24:
		mh->mh_SDRAMSIZE_Addr = 0x40fe70b7;	/* 8M SDRAM */
		break;
	case 0x2a04:		/* MC85 */
		mh->mh_SDRAMSIZE_Addr = 0x40fc70b7;	/* 16M SDRAM */
		break;
	default:
		break;
	}
	return &mh->public;
fail2:
	bus_dmamem_free(mh->mh_dmat, mh->mh_cmdbuf, mh->mh_dmamap);
fail1:
	bus_dma_tag_destroy(mh->mh_dmat);
fail0:
	mtx_destroy(&mh->mh_mtx);
	free(mh, M_DEVBUF);
	return NULL;
}

void
mwl_hal_detach(struct mwl_hal *mh0)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);

	bus_dmamem_free(mh->mh_dmat, mh->mh_cmdbuf, mh->mh_dmamap);
	bus_dma_tag_destroy(mh->mh_dmat);
	mtx_destroy(&mh->mh_mtx);
	free(mh, M_DEVBUF);
}

/*
 * Reset internal state after a firmware download.
 */
static int
mwlResetHalState(struct mwl_hal_priv *mh)
{
	int i;

	/* XXX get from f/w */
	mh->mh_bastreams = (1<<MWL_BASTREAMS_MAX)-1;
	for (i = 0; i < MWL_MBSS_MAX; i++)
		mh->mh_vaps[i].mh = NULL;
	/*
	 * Clear cumulative stats.
	 */
	mh->mh_RTSSuccesses = 0;
	mh->mh_RTSFailures = 0;
	mh->mh_RxDuplicateFrames = 0;
	mh->mh_FCSErrorCount = 0;
	/*
	 * Fetch cal data for later use.
	 * XXX may want to fetch other stuff too.
	 */
	/* XXX check return */
	if ((mh->mh_flags & MHF_CALDATA) == 0)
		mwlGetPwrCalTable(mh);
	return 0;
}

struct mwl_hal_vap *
mwl_hal_newvap(struct mwl_hal *mh0, MWL_HAL_BSSTYPE type,
	const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	struct mwl_hal_vap *vap;
	int i;

	MWL_HAL_LOCK(mh);
	/* NB: could optimize but not worth it w/ max 32 bss */
	for (i = 0; i < MWL_MBSS_MAX; i++) {
		vap = &mh->mh_vaps[i];
		if (vap->vap_type == type && vap->mh == NULL) {
			vap->mh = mh;
			mwl_hal_setmac_locked(vap, mac);
			break;
		}
	}
	MWL_HAL_UNLOCK(mh);
	return (i < MWL_MBSS_MAX) ? vap : NULL;
}

void
mwl_hal_delvap(struct mwl_hal_vap *vap)
{
	/* NB: locking not needed for single write */
	vap->mh = NULL;
}

/*
 * Manipulate the debug mask.  Note debug
 * msgs are only provided when this code is
 * compiled with MWLHAL_DEBUG defined.
 */

void
mwl_hal_setdebug(struct mwl_hal *mh, int debug)
{
	MWLPRIV(mh)->mh_debug = debug;
}

int
mwl_hal_getdebug(struct mwl_hal *mh)
{
	return MWLPRIV(mh)->mh_debug;
}

void
mwl_hal_setbastreams(struct mwl_hal *mh, int mask)
{
	MWLPRIV(mh)->mh_bastreams = mask & ((1<<MWL_BASTREAMS_MAX)-1);
}

int
mwl_hal_getbastreams(struct mwl_hal *mh)
{
	return MWLPRIV(mh)->mh_bastreams;
}

int
mwl_hal_ismbsscapable(struct mwl_hal *mh)
{
	return (MWLPRIV(mh)->mh_flags & MHF_MBSS) != 0;
}

#if 0
/* XXX inlined */
/*
 * Return the current ISR setting and clear the cause.
 * XXX maybe make inline
 */
void
mwl_hal_getisr(struct mwl_hal *mh0, uint32_t *status)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	uint32_t cause;

	cause = RD4(mh, MACREG_REG_A2H_INTERRUPT_CAUSE);
	if (cause == 0xffffffff) {	/* card removed */
device_printf(mh->mh_dev, "%s: cause 0x%x\n", __func__, cause);
		cause = 0;
	} else if (cause != 0) {
		/* clear cause bits */
		WR4(mh, MACREG_REG_A2H_INTERRUPT_CAUSE,
		    cause &~ mh->public.mh_imask);
		RD4(mh, MACREG_REG_INT_CODE);	/* XXX flush write? */
	}
	*status = cause;
}
#endif

/*
 * Set the interrupt mask.
 */
void
mwl_hal_intrset(struct mwl_hal *mh0, uint32_t mask)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);

	WR4(mh, MACREG_REG_A2H_INTERRUPT_MASK, 0);
	RD4(mh, MACREG_REG_INT_CODE);

	mh->public.mh_imask = mask;
	WR4(mh, MACREG_REG_A2H_INTERRUPT_MASK, mask);
	RD4(mh, MACREG_REG_INT_CODE);
}

#if 0
/* XXX inlined */
/*
 * Kick the firmware to tell it there are new tx descriptors
 * for processing.  The driver says what h/w q has work in
 * case the f/w ever gets smarter.
 */
void
mwl_hal_txstart(struct mwl_hal *mh0, int qnum)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	uint32_t dummy;

	WR4(mh, MACREG_REG_H2A_INTERRUPT_EVENTS, MACREG_H2ARIC_BIT_PPA_READY);
	dummy = RD4(mh, MACREG_REG_INT_CODE);
}
#endif

/*
 * Callback from the driver on a cmd done interrupt.
 * Nothing to do right now as we spin waiting for
 * cmd completion.
 */
void
mwl_hal_cmddone(struct mwl_hal *mh0)
{
#if 0
	struct mwl_hal_priv *mh = MWLPRIV(mh0);

	if (mh->mh_debug & MWL_HAL_DEBUG_CMDDONE) {
		device_printf(mh->mh_dev, "cmd done interrupt:\n");
		dumpresult(mh, 1);
	}
#endif
}

/*
 * Return "hw specs".  Note this must be the first
 * cmd MUST be done after a firmware download or the
 * f/w will lockup.
 * XXX move into the hal so driver doesn't need to be responsible
 */
int
mwl_hal_gethwspecs(struct mwl_hal *mh0, struct mwl_hal_hwspec *hw)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_GET_HW_SPEC *pCmd;
	int retval, minrev;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_GET_HW_SPEC, HostCmd_CMD_GET_HW_SPEC);
	memset(&pCmd->PermanentAddr[0], 0xff, IEEE80211_ADDR_LEN);
	pCmd->ulFwAwakeCookie = htole32((unsigned int)mh->mh_cmdaddr+2048);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_GET_HW_SPEC);
	if (retval == 0) {
		IEEE80211_ADDR_COPY(hw->macAddr, pCmd->PermanentAddr);
		hw->wcbBase[0] = le32toh(pCmd->WcbBase0) & 0x0000ffff;
		hw->wcbBase[1] = le32toh(pCmd->WcbBase1[0]) & 0x0000ffff;
		hw->wcbBase[2] = le32toh(pCmd->WcbBase1[1]) & 0x0000ffff;
		hw->wcbBase[3] = le32toh(pCmd->WcbBase1[2]) & 0x0000ffff;
		hw->rxDescRead = le32toh(pCmd->RxPdRdPtr)& 0x0000ffff;
		hw->rxDescWrite = le32toh(pCmd->RxPdWrPtr)& 0x0000ffff;
		hw->regionCode = le16toh(pCmd->RegionCode) & 0x00ff;
		hw->fwReleaseNumber = le32toh(pCmd->FWReleaseNumber);
		hw->maxNumWCB = le16toh(pCmd->NumOfWCB);
		hw->maxNumMCAddr = le16toh(pCmd->NumOfMCastAddr);
		hw->numAntennas = le16toh(pCmd->NumberOfAntenna);
		hw->hwVersion = pCmd->Version;
		hw->hostInterface = pCmd->HostIf;

		mh->mh_revs.mh_macRev = hw->hwVersion;		/* XXX */
		mh->mh_revs.mh_phyRev = hw->hostInterface;	/* XXX */

		minrev = ((hw->fwReleaseNumber) >> 16) & 0xff;
		if (minrev >= 4) {
			/* starting with 3.4.x.x s/w BA streams supported */
			mh->mh_bastreams &= (1<<MWL_BASTREAMS_MAX)-1;
		} else
			mh->mh_bastreams &= (1<<2)-1;
	}
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Inform the f/w about location of the tx/rx dma data structures
 * and related state.  This cmd must be done immediately after a
 * mwl_hal_gethwspecs call or the f/w will lockup.
 */
int
mwl_hal_sethwdma(struct mwl_hal *mh0, const struct mwl_hal_txrxdma *dma)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_SET_HW_SPEC *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_SET_HW_SPEC, HostCmd_CMD_SET_HW_SPEC);
	pCmd->WcbBase[0] = htole32(dma->wcbBase[0]);
	pCmd->WcbBase[1] = htole32(dma->wcbBase[1]);
	pCmd->WcbBase[2] = htole32(dma->wcbBase[2]);
	pCmd->WcbBase[3] = htole32(dma->wcbBase[3]);
	pCmd->TxWcbNumPerQueue = htole32(dma->maxNumTxWcb);
	pCmd->NumTxQueues = htole32(dma->maxNumWCB);
	pCmd->TotalRxWcb = htole32(1);		/* XXX */
	pCmd->RxPdWrPtr = htole32(dma->rxDescRead);
	pCmd->Flags = htole32(SET_HW_SPEC_HOSTFORM_BEACON
#ifdef MWL_HOST_PS_SUPPORT
		    | SET_HW_SPEC_HOST_POWERSAVE
#endif
		    | SET_HW_SPEC_HOSTFORM_PROBERESP);
	/* disable multi-bss operation for A1-A4 parts */
	if (mh->mh_revs.mh_macRev < 5)
		pCmd->Flags |= htole32(SET_HW_SPEC_DISABLEMBSS);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_HW_SPEC);
	if (retval == 0) {
		if (pCmd->Flags & htole32(SET_HW_SPEC_DISABLEMBSS))
			mh->mh_flags &= ~MHF_MBSS;
		else
			mh->mh_flags |= MHF_MBSS;
	}
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Retrieve statistics from the f/w.
 * XXX should be in memory shared w/ driver
 */
int
mwl_hal_gethwstats(struct mwl_hal *mh0, struct mwl_hal_hwstats *stats)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_802_11_GET_STAT *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_802_11_GET_STAT,
		HostCmd_CMD_802_11_GET_STAT);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_GET_STAT);
	if (retval == 0) {
		const uint32_t *sp = (const uint32_t *)&pCmd->TxRetrySuccesses;
		uint32_t *dp = (uint32_t *)&stats->TxRetrySuccesses;
		int i;

		for (i = 0; i < sizeof(*stats)/sizeof(uint32_t); i++)
			dp[i] = le32toh(sp[i]);
		/*
		 * Update stats not returned by f/w but available
		 * through public registers.  Note these registers
		 * are "clear on read" so we maintain cumulative data.
		 * XXX register defines
		 */
		mh->mh_RTSSuccesses += RD4(mh, 0xa834);
		mh->mh_RTSFailures += RD4(mh, 0xa830);
		mh->mh_RxDuplicateFrames += RD4(mh, 0xa84c);
		mh->mh_FCSErrorCount += RD4(mh, 0xa840);
	}
	MWL_HAL_UNLOCK(mh);

	stats->RTSSuccesses = mh->mh_RTSSuccesses;
	stats->RTSFailures = mh->mh_RTSFailures;
	stats->RxDuplicateFrames = mh->mh_RxDuplicateFrames;
	stats->FCSErrorCount = mh->mh_FCSErrorCount;
	return retval;
}

/*
 * Set HT guard interval handling.
 * Takes effect immediately.
 */
int
mwl_hal_sethtgi(struct mwl_hal_vap *vap, int GIType)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_HT_GUARD_INTERVAL *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_HT_GUARD_INTERVAL,
		HostCmd_CMD_HT_GUARD_INTERVAL);
	pCmd->Action = htole32(HostCmd_ACT_GEN_SET);

	if (GIType == 0) {
		pCmd->GIType = htole32(GI_TYPE_LONG);
	} else if (GIType == 1) {
		pCmd->GIType = htole32(GI_TYPE_LONG | GI_TYPE_SHORT);
	} else {
		pCmd->GIType = htole32(GI_TYPE_LONG);
	}

	retval = mwlExecuteCmd(mh, HostCmd_CMD_HT_GUARD_INTERVAL);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Configure radio.
 * Takes effect immediately.
 * XXX preamble installed after set fixed rate cmd
 */
int
mwl_hal_setradio(struct mwl_hal *mh0, int onoff, MWL_HAL_PREAMBLE preamble)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_802_11_RADIO_CONTROL *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_802_11_RADIO_CONTROL,
		HostCmd_CMD_802_11_RADIO_CONTROL);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET);
	if (onoff == 0)
		pCmd->Control = 0;
	else
		pCmd->Control = htole16(preamble);
	pCmd->RadioOn = htole16(onoff);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_RADIO_CONTROL);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Configure antenna use.
 * Takes effect immediately.
 * XXX tx antenna setting ignored
 * XXX rx antenna setting should always be 3 (for now)
 */
int
mwl_hal_setantenna(struct mwl_hal *mh0, MWL_HAL_ANTENNA dirSet, int ant)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_802_11_RF_ANTENNA *pCmd;
	int retval;

	if (!(dirSet == WL_ANTENNATYPE_RX || dirSet == WL_ANTENNATYPE_TX))
		return EINVAL;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_802_11_RF_ANTENNA,
		HostCmd_CMD_802_11_RF_ANTENNA);
	pCmd->Action = htole16(dirSet);
	if (ant == 0)			/* default to all/both antennae */
		ant = 3;
	pCmd->AntennaMode = htole16(ant);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_RF_ANTENNA);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Set packet size threshold for implicit use of RTS.
 * Takes effect immediately.
 * XXX packet length > threshold =>'s RTS
 */
int
mwl_hal_setrtsthreshold(struct mwl_hal_vap *vap, int threshold)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_DS_802_11_RTS_THSD *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_DS_802_11_RTS_THSD,
		HostCmd_CMD_802_11_RTS_THSD);
	pCmd->Action  = htole16(HostCmd_ACT_GEN_SET);
	pCmd->Threshold = htole16(threshold);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_RTS_THSD);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Enable sta-mode operation (disables beacon frame xmit).
 */
int
mwl_hal_setinframode(struct mwl_hal_vap *vap)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_INFRA_MODE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_INFRA_MODE,
		HostCmd_CMD_SET_INFRA_MODE);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_INFRA_MODE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Configure radar detection in support of 802.11h.
 */
int
mwl_hal_setradardetection(struct mwl_hal *mh0, MWL_HAL_RADAR action)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_802_11h_Detect_Radar *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_802_11h_Detect_Radar,
		HostCmd_CMD_802_11H_DETECT_RADAR);
	pCmd->CmdHdr.Length = htole16(sizeof(HostCmd_802_11h_Detect_Radar));
	pCmd->Action = htole16(action);
	if (mh->mh_regioncode == DOMAIN_CODE_ETSI_131)
		pCmd->RadarTypeCode = htole16(131);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11H_DETECT_RADAR);
	MWL_HAL_UNLOCK(mh);
	return retval;
} 

/*
 * Convert public channel flags definition to a
 * value suitable for feeding to the firmware.
 * Note this includes byte swapping.
 */
static uint32_t
cvtChannelFlags(const MWL_HAL_CHANNEL *chan)
{
	uint32_t w;

	/*
	 * NB: f/w only understands FREQ_BAND_5GHZ, supplying the more
	 *     precise band info causes it to lockup (sometimes).
	 */
	w = (chan->channelFlags.FreqBand == MWL_FREQ_BAND_2DOT4GHZ) ?
		FREQ_BAND_2DOT4GHZ : FREQ_BAND_5GHZ;
	switch (chan->channelFlags.ChnlWidth) {
	case MWL_CH_10_MHz_WIDTH:
		w |= CH_10_MHz_WIDTH;
		break;
	case MWL_CH_20_MHz_WIDTH:
		w |= CH_20_MHz_WIDTH;
		break;
	case MWL_CH_40_MHz_WIDTH:
	default:
		w |= CH_40_MHz_WIDTH;
		break;
	}
	switch (chan->channelFlags.ExtChnlOffset) {
	case MWL_EXT_CH_NONE:
		w |= EXT_CH_NONE;
		break;
	case MWL_EXT_CH_ABOVE_CTRL_CH:
		w |= EXT_CH_ABOVE_CTRL_CH;
		break;
	case MWL_EXT_CH_BELOW_CTRL_CH:
		w |= EXT_CH_BELOW_CTRL_CH;
		break;
	}
	return htole32(w);
}

/*
 * Start a channel switch announcement countdown.  The IE
 * in the beacon frame is allowed to go out and the firmware
 * counts down and notifies the host when it's time to switch
 * channels.
 */
int
mwl_hal_setchannelswitchie(struct mwl_hal *mh0,
	const MWL_HAL_CHANNEL *nextchan, uint32_t mode, uint32_t count)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_SET_SWITCH_CHANNEL *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_SET_SWITCH_CHANNEL,
		HostCmd_CMD_SET_SWITCH_CHANNEL);
	pCmd->Next11hChannel = htole32(nextchan->channel);
	pCmd->Mode = htole32(mode);
	pCmd->InitialCount = htole32(count+1);
	pCmd->ChannelFlags = cvtChannelFlags(nextchan);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_SWITCH_CHANNEL);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Set the region code that selects the radar bin'ing agorithm.
 */
int
mwl_hal_setregioncode(struct mwl_hal *mh0, int regionCode)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_SET_REGIONCODE_INFO *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_SET_REGIONCODE_INFO,
		HostCmd_CMD_SET_REGION_CODE);
	/* XXX map pseudo-codes to fw codes */
	switch (regionCode) {
	case DOMAIN_CODE_ETSI_131:
		pCmd->regionCode = htole16(DOMAIN_CODE_ETSI);
		break;
	default:
		pCmd->regionCode = htole16(regionCode);
		break;
	}

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_REGION_CODE);
	if (retval == 0)
		mh->mh_regioncode = regionCode;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

#define	RATEVAL(r)	((r) &~ RATE_MCS)
#define	RATETYPE(r)	(((r) & RATE_MCS) ? HT_RATE_TYPE : LEGACY_RATE_TYPE)

int
mwl_hal_settxrate(struct mwl_hal_vap *vap, MWL_HAL_TXRATE_HANDLING handling,
	const MWL_HAL_TXRATE *rate)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_USE_FIXED_RATE *pCmd;
	FIXED_RATE_ENTRY *fp;
	int retval, i, n;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_USE_FIXED_RATE,
		HostCmd_CMD_SET_FIXED_RATE);

	pCmd->MulticastRate = RATEVAL(rate->McastRate);
	pCmd->MultiRateTxType = RATETYPE(rate->McastRate);
	/* NB: no rate type field */
	pCmd->ManagementRate = RATEVAL(rate->MgtRate);
	memset(pCmd->FixedRateTable, 0, sizeof(pCmd->FixedRateTable));
	if (handling == RATE_FIXED) {
		pCmd->Action = htole32(HostCmd_ACT_GEN_SET);
		pCmd->AllowRateDrop = htole32(FIXED_RATE_WITHOUT_AUTORATE_DROP);
		fp = pCmd->FixedRateTable;
		fp->FixedRate =
		    htole32(RATEVAL(rate->RateSeries[0].Rate));
		fp->FixRateTypeFlags.FixRateType =
		    htole32(RATETYPE(rate->RateSeries[0].Rate));
		pCmd->EntryCount = htole32(1);
	} else if (handling == RATE_FIXED_DROP) {
		pCmd->Action = htole32(HostCmd_ACT_GEN_SET);
		pCmd->AllowRateDrop = htole32(FIXED_RATE_WITH_AUTO_RATE_DROP);
		n = 0;
		fp = pCmd->FixedRateTable;
		for (i = 0; i < 4; i++) {
			if (rate->RateSeries[0].TryCount == 0)
				break;
			fp->FixRateTypeFlags.FixRateType =
			    htole32(RATETYPE(rate->RateSeries[i].Rate));
			fp->FixedRate =
			    htole32(RATEVAL(rate->RateSeries[i].Rate));
			fp->FixRateTypeFlags.RetryCountValid =
			    htole32(RETRY_COUNT_VALID);
			fp->RetryCount =
			    htole32(rate->RateSeries[i].TryCount-1);
			n++;
		}
		pCmd->EntryCount = htole32(n);
	} else 
		pCmd->Action = htole32(HostCmd_ACT_NOT_USE_FIXED_RATE);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_FIXED_RATE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_settxrate_auto(struct mwl_hal *mh0, const MWL_HAL_TXRATE *rate)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_USE_FIXED_RATE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_USE_FIXED_RATE,
		HostCmd_CMD_SET_FIXED_RATE);

	pCmd->MulticastRate = RATEVAL(rate->McastRate);
	pCmd->MultiRateTxType = RATETYPE(rate->McastRate);
	/* NB: no rate type field */
	pCmd->ManagementRate = RATEVAL(rate->MgtRate);
	memset(pCmd->FixedRateTable, 0, sizeof(pCmd->FixedRateTable));
	pCmd->Action = htole32(HostCmd_ACT_NOT_USE_FIXED_RATE);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_FIXED_RATE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

#undef RATEVAL
#undef RATETYPE

int
mwl_hal_setslottime(struct mwl_hal *mh0, int usecs)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_SLOT *pCmd;
	int retval;

	if (usecs != 9 && usecs != 20)
		return EINVAL;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_SLOT,
	    HostCmd_CMD_802_11_SET_SLOT);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET);
	pCmd->Slot = (usecs == 9 ? 1 : 0);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_SET_SLOT);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_adjusttxpower(struct mwl_hal *mh0, uint32_t level)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_802_11_RF_TX_POWER *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_802_11_RF_TX_POWER,
	    HostCmd_CMD_802_11_RF_TX_POWER);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET);

	if (level < 30) {
		pCmd->SupportTxPowerLevel = htole16(WL_TX_POWERLEVEL_LOW);
	} else if (level >= 30 && level < 60) {
		pCmd->SupportTxPowerLevel = htole16(WL_TX_POWERLEVEL_MEDIUM);
	} else {
		pCmd->SupportTxPowerLevel = htole16(WL_TX_POWERLEVEL_HIGH);
	}

	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_RF_TX_POWER);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

static const struct mwl_hal_channel *
findchannel(const struct mwl_hal_priv *mh, const MWL_HAL_CHANNEL *c)
{
	const struct mwl_hal_channel *hc;
	const MWL_HAL_CHANNELINFO *ci;
	int chan = c->channel, i;

	if (c->channelFlags.FreqBand == MWL_FREQ_BAND_2DOT4GHZ) {
		i = chan - 1;
		if (c->channelFlags.ChnlWidth == MWL_CH_40_MHz_WIDTH) {
			ci = &mh->mh_40M;
			if (c->channelFlags.ExtChnlOffset == MWL_EXT_CH_BELOW_CTRL_CH)
				i -= 4;
		} else
			ci = &mh->mh_20M;
		/* 2.4G channel table is directly indexed */
		hc = ((unsigned)i < ci->nchannels) ? &ci->channels[i] : NULL;
	} else if (c->channelFlags.FreqBand == MWL_FREQ_BAND_5GHZ) {
		if (c->channelFlags.ChnlWidth == MWL_CH_40_MHz_WIDTH) {
			ci = &mh->mh_40M_5G;
			if (c->channelFlags.ExtChnlOffset == MWL_EXT_CH_BELOW_CTRL_CH)
				chan -= 4;
		} else
			ci = &mh->mh_20M_5G;
		/* 5GHz channel table is sparse and must be searched */
		for (i = 0; i < ci->nchannels; i++)
			if (ci->channels[i].ieee == chan)
				break;
		hc = (i < ci->nchannels) ? &ci->channels[i] : NULL;
	} else
		hc = NULL;
	return hc;
}

int
mwl_hal_settxpower(struct mwl_hal *mh0, const MWL_HAL_CHANNEL *c, uint8_t maxtxpow)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_802_11_RF_TX_POWER *pCmd;
	const struct mwl_hal_channel *hc;
	int i, retval;

	hc = findchannel(mh, c);
	if (hc == NULL) {
		/* XXX temp while testing */
		device_printf(mh->mh_dev,
		    "%s: no cal data for channel %u band %u width %u ext %u\n",
		    __func__, c->channel, c->channelFlags.FreqBand,
		    c->channelFlags.ChnlWidth, c->channelFlags.ExtChnlOffset);
		return EINVAL;
	}

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_802_11_RF_TX_POWER,
	    HostCmd_CMD_802_11_RF_TX_POWER);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET_LIST);
	i = 0;
	/* NB: 5Ghz cal data have the channel # in [0]; don't truncate */
	if (c->channelFlags.FreqBand == MWL_FREQ_BAND_5GHZ)
		pCmd->PowerLevelList[i++] = htole16(hc->targetPowers[0]);
	for (; i < 4; i++) {
		uint16_t pow = hc->targetPowers[i];
		if (pow > maxtxpow)
			pow = maxtxpow;
		pCmd->PowerLevelList[i] = htole16(pow);
	}
	retval = mwlExecuteCmd(mh, HostCmd_CMD_802_11_RF_TX_POWER);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_getchannelinfo(struct mwl_hal *mh0, int band, int chw,
	const MWL_HAL_CHANNELINFO **ci)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);

	switch (band) {
	case MWL_FREQ_BAND_2DOT4GHZ:
		*ci = (chw == MWL_CH_20_MHz_WIDTH) ? &mh->mh_20M : &mh->mh_40M;
		break;
	case MWL_FREQ_BAND_5GHZ:
		*ci = (chw == MWL_CH_20_MHz_WIDTH) ?
		     &mh->mh_20M_5G : &mh->mh_40M_5G;
		break;
	default:
		return EINVAL;
	}
	return ((*ci)->freqLow == (*ci)->freqHigh) ? EINVAL : 0;
}

int
mwl_hal_setmcast(struct mwl_hal *mh0, int nmc, const uint8_t macs[])
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_MAC_MULTICAST_ADR *pCmd;
	int retval;

	if (nmc > MWL_HAL_MCAST_MAX)
		return EINVAL;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_MAC_MULTICAST_ADR,
		HostCmd_CMD_MAC_MULTICAST_ADR);
	memcpy(pCmd->MACList, macs, nmc*IEEE80211_ADDR_LEN);
	pCmd->NumOfAdrs = htole16(nmc);
	pCmd->Action = htole16(0xffff);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_MAC_MULTICAST_ADR);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_keyset(struct mwl_hal_vap *vap, const MWL_HAL_KEYVAL *kv,
	const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_UPDATE_ENCRYPTION_SET_KEY *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_UPDATE_ENCRYPTION_SET_KEY,
		HostCmd_CMD_UPDATE_ENCRYPTION);
	if (kv->keyFlags & (KEY_FLAG_TXGROUPKEY|KEY_FLAG_RXGROUPKEY))
		pCmd->ActionType = htole32(EncrActionTypeSetGroupKey);
	else
		pCmd->ActionType = htole32(EncrActionTypeSetKey);
	pCmd->KeyParam.Length = htole16(sizeof(pCmd->KeyParam));
	pCmd->KeyParam.KeyTypeId = htole16(kv->keyTypeId);
	pCmd->KeyParam.KeyInfo = htole32(kv->keyFlags);
	pCmd->KeyParam.KeyIndex = htole32(kv->keyIndex);
	/* NB: includes TKIP MIC keys */
	memcpy(&pCmd->KeyParam.Key, &kv->key, kv->keyLen);
	switch (kv->keyTypeId) {
	case KEY_TYPE_ID_WEP:
		pCmd->KeyParam.KeyLen = htole16(kv->keyLen);
		break;
	case KEY_TYPE_ID_TKIP:
		pCmd->KeyParam.KeyLen = htole16(sizeof(TKIP_TYPE_KEY));
		pCmd->KeyParam.Key.TkipKey.TkipRsc.low =
			htole16(kv->key.tkip.rsc.low);
		pCmd->KeyParam.Key.TkipKey.TkipRsc.high =
			htole32(kv->key.tkip.rsc.high);
		pCmd->KeyParam.Key.TkipKey.TkipTsc.low =
			htole16(kv->key.tkip.tsc.low);
		pCmd->KeyParam.Key.TkipKey.TkipTsc.high =
			htole32(kv->key.tkip.tsc.high);
		break;
	case KEY_TYPE_ID_AES:
		pCmd->KeyParam.KeyLen = htole16(sizeof(AES_TYPE_KEY));
		break;
	}
#ifdef MWL_MBSS_SUPPORT
	IEEE80211_ADDR_COPY(pCmd->KeyParam.Macaddr, mac);
#else
	IEEE80211_ADDR_COPY(pCmd->Macaddr, mac);
#endif
	retval = mwlExecuteCmd(mh, HostCmd_CMD_UPDATE_ENCRYPTION);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_keyreset(struct mwl_hal_vap *vap, const MWL_HAL_KEYVAL *kv, const uint8_t mac[IEEE80211_ADDR_LEN])
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_UPDATE_ENCRYPTION_SET_KEY *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_UPDATE_ENCRYPTION_SET_KEY,
		HostCmd_CMD_UPDATE_ENCRYPTION);
	pCmd->ActionType = htole16(EncrActionTypeRemoveKey);
	pCmd->KeyParam.Length = htole16(sizeof(pCmd->KeyParam));
	pCmd->KeyParam.KeyTypeId = htole16(kv->keyTypeId);
	pCmd->KeyParam.KeyInfo = htole32(kv->keyFlags);
	pCmd->KeyParam.KeyIndex = htole32(kv->keyIndex);
#ifdef MWL_MBSS_SUPPORT
	IEEE80211_ADDR_COPY(pCmd->KeyParam.Macaddr, mac);
#else
	IEEE80211_ADDR_COPY(pCmd->Macaddr, mac);
#endif
	retval = mwlExecuteCmd(mh, HostCmd_CMD_UPDATE_ENCRYPTION);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

static int
mwl_hal_setmac_locked(struct mwl_hal_vap *vap,
	const uint8_t addr[IEEE80211_ADDR_LEN])
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_DS_SET_MAC *pCmd;

	_VCMD_SETUP(vap, pCmd, HostCmd_DS_SET_MAC, HostCmd_CMD_SET_MAC_ADDR);
	IEEE80211_ADDR_COPY(&pCmd->MacAddr[0], addr);
#ifdef MWL_MBSS_SUPPORT
	pCmd->MacType = vap->bss_type;		/* NB: already byte swapped */
	IEEE80211_ADDR_COPY(vap->mac, addr);	/* XXX do only if success */
#endif
	return mwlExecuteCmd(mh, HostCmd_CMD_SET_MAC_ADDR);
}

int
mwl_hal_setmac(struct mwl_hal_vap *vap, const uint8_t addr[IEEE80211_ADDR_LEN])
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	int retval;

	MWL_HAL_LOCK(mh);
	retval = mwl_hal_setmac_locked(vap, addr);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setbeacon(struct mwl_hal_vap *vap, const void *frame, size_t frameLen)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_DS_SET_BEACON *pCmd;
	int retval;

	/* XXX verify frameLen fits */
	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_DS_SET_BEACON, HostCmd_CMD_SET_BEACON);
	/* XXX override _VCMD_SETUP */
	pCmd->CmdHdr.Length = htole16(sizeof(HostCmd_DS_SET_BEACON)-1+frameLen);
	pCmd->FrmBodyLen = htole16(frameLen);
	memcpy(pCmd->FrmBody, frame, frameLen);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_BEACON);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setpowersave_bss(struct mwl_hal_vap *vap, uint8_t nsta)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_SET_POWERSAVESTATION *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_SET_POWERSAVESTATION,
		HostCmd_CMD_SET_POWERSAVESTATION);
	pCmd->NumberOfPowersave = nsta;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_POWERSAVESTATION);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setpowersave_sta(struct mwl_hal_vap *vap, uint16_t aid, int ena)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_SET_TIM *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_SET_TIM, HostCmd_CMD_SET_TIM);
	pCmd->Aid = htole16(aid);
	pCmd->Set = htole32(ena);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_TIM);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setassocid(struct mwl_hal_vap *vap,
	const uint8_t bssId[IEEE80211_ADDR_LEN], uint16_t assocId)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_AID *pCmd = (HostCmd_FW_SET_AID *) &mh->mh_cmdbuf[0];
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_AID, HostCmd_CMD_SET_AID);
	pCmd->AssocID = htole16(assocId);
	IEEE80211_ADDR_COPY(&pCmd->MacAddr[0], bssId);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_AID);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setchannel(struct mwl_hal *mh0, const MWL_HAL_CHANNEL *chan)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_RF_CHANNEL *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_RF_CHANNEL, HostCmd_CMD_SET_RF_CHANNEL);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET);
	pCmd->CurrentChannel = chan->channel;
	pCmd->ChannelFlags = cvtChannelFlags(chan);	/* NB: byte-swapped */

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_RF_CHANNEL);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

static int
bastream_check_available(struct mwl_hal_vap *vap, int qid,
	const uint8_t Macaddr[IEEE80211_ADDR_LEN],
	uint8_t Tid, uint8_t ParamInfo)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_BASTREAM *pCmd;
	int retval;

	MWL_HAL_LOCK_ASSERT(mh);

	_VCMD_SETUP(vap, pCmd, HostCmd_FW_BASTREAM, HostCmd_CMD_BASTREAM);
	pCmd->ActionType = htole32(BaCheckCreateStream);
	pCmd->BaInfo.CreateParams.BarThrs = htole32(63);
	pCmd->BaInfo.CreateParams.WindowSize = htole32(64); 
	pCmd->BaInfo.CreateParams.IdleThrs = htole32(0x22000);
	IEEE80211_ADDR_COPY(&pCmd->BaInfo.CreateParams.PeerMacAddr[0], Macaddr);
	pCmd->BaInfo.CreateParams.DialogToken = 10;
	pCmd->BaInfo.CreateParams.Tid = Tid;
	pCmd->BaInfo.CreateParams.QueueId = qid;
	pCmd->BaInfo.CreateParams.ParamInfo = (uint8_t) ParamInfo;
#if 0
	cvtBAFlags(&pCmd->BaInfo.CreateParams.Flags, sp->ba_policy, 0);
#else
	pCmd->BaInfo.CreateParams.Flags =
			  htole32(BASTREAM_FLAG_IMMEDIATE_TYPE)
			| htole32(BASTREAM_FLAG_DIRECTION_UPSTREAM)
			;
#endif

	retval = mwlExecuteCmd(mh, HostCmd_CMD_BASTREAM);
	if (retval == 0) {
		/*
		 * NB: BA stream create may fail when the stream is
		 * h/w backed under some (as yet not understood) conditions.
		 * Check the result code to catch this.
		 */
		if (le16toh(pCmd->CmdHdr.Result) != HostCmd_RESULT_OK)
			retval = EIO;
	}
	return retval;
}

const MWL_HAL_BASTREAM *
mwl_hal_bastream_alloc(struct mwl_hal_vap *vap, int ba_policy,
	const uint8_t Macaddr[IEEE80211_ADDR_LEN],
	uint8_t Tid, uint8_t ParamInfo, void *a1, void *a2)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	struct mwl_hal_bastream *sp;
	int s;

	MWL_HAL_LOCK(mh);
	if (mh->mh_bastreams == 0) {
		/* no streams available */
		MWL_HAL_UNLOCK(mh);
		return NULL;
	}
	for (s = 0; (mh->mh_bastreams & (1<<s)) == 0; s++)
		;
	if (bastream_check_available(vap, s, Macaddr, Tid, ParamInfo)) {
		MWL_HAL_UNLOCK(mh);
		return NULL;
	}
	sp = &mh->mh_streams[s];
	mh->mh_bastreams &= ~(1<<s);
	sp->public.data[0] = a1;
	sp->public.data[1] = a2;
	IEEE80211_ADDR_COPY(sp->macaddr, Macaddr);
	sp->tid = Tid;
	sp->paraminfo = ParamInfo;
	sp->setup = 0;
	sp->ba_policy = ba_policy;
	MWL_HAL_UNLOCK(mh);
	return &sp->public;
}

const MWL_HAL_BASTREAM *
mwl_hal_bastream_lookup(struct mwl_hal *mh0, int s)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);

	if (!(0 <= s && s < MWL_BASTREAMS_MAX))
		return NULL;
	if (mh->mh_bastreams & (1<<s))
		return NULL;
	return &mh->mh_streams[s].public;
}

#ifndef	__DECONST
#define	__DECONST(type, var)	((type)(uintptr_t)(const void *)(var))
#endif

int
mwl_hal_bastream_create(struct mwl_hal_vap *vap,
	const MWL_HAL_BASTREAM *s, int BarThrs, int WindowSize, uint16_t seqno)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	struct mwl_hal_bastream *sp = __DECONST(struct mwl_hal_bastream *, s);
	HostCmd_FW_BASTREAM *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_BASTREAM, HostCmd_CMD_BASTREAM);
	pCmd->ActionType = htole32(BaCreateStream);
	pCmd->BaInfo.CreateParams.BarThrs = htole32(BarThrs);
	pCmd->BaInfo.CreateParams.WindowSize = htole32(WindowSize);
	pCmd->BaInfo.CreateParams.IdleThrs = htole32(0x22000);
	IEEE80211_ADDR_COPY(&pCmd->BaInfo.CreateParams.PeerMacAddr[0],
	    sp->macaddr);
	/* XXX proxy STA */
	memset(&pCmd->BaInfo.CreateParams.StaSrcMacAddr, 0, IEEE80211_ADDR_LEN);
#if 0
	pCmd->BaInfo.CreateParams.DialogToken = DialogToken;
#else
	pCmd->BaInfo.CreateParams.DialogToken = 10;
#endif
	pCmd->BaInfo.CreateParams.Tid = sp->tid;
	pCmd->BaInfo.CreateParams.QueueId = sp->stream;
	pCmd->BaInfo.CreateParams.ParamInfo = sp->paraminfo;
	/* NB: ResetSeqNo known to be zero */
	pCmd->BaInfo.CreateParams.StartSeqNo = htole16(seqno);
#if 0
	cvtBAFlags(&pCmd->BaInfo.CreateParams.Flags, sp->ba_policy, 0);
#else
	pCmd->BaInfo.CreateParams.Flags =
			  htole32(BASTREAM_FLAG_IMMEDIATE_TYPE)
			| htole32(BASTREAM_FLAG_DIRECTION_UPSTREAM)
			;
#endif

	retval = mwlExecuteCmd(mh, HostCmd_CMD_BASTREAM);
	if (retval == 0) {
		/*
		 * NB: BA stream create may fail when the stream is
		 * h/w backed under some (as yet not understood) conditions.
		 * Check the result code to catch this.
		 */
		if (le16toh(pCmd->CmdHdr.Result) != HostCmd_RESULT_OK)
			retval = EIO;
		else
			sp->setup = 1;
	}
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_bastream_destroy(struct mwl_hal *mh0, const MWL_HAL_BASTREAM *s)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	struct mwl_hal_bastream *sp = __DECONST(struct mwl_hal_bastream *, s);
	HostCmd_FW_BASTREAM *pCmd;
	int retval;

	if (sp->stream >= MWL_BASTREAMS_MAX) {
		/* XXX */
		return EINVAL;
	}
	MWL_HAL_LOCK(mh);
	if (sp->setup) {
		_CMD_SETUP(pCmd, HostCmd_FW_BASTREAM, HostCmd_CMD_BASTREAM);
		pCmd->ActionType = htole32(BaDestroyStream);
		pCmd->BaInfo.DestroyParams.FwBaContext.Context =
		    htole32(sp->stream);

		retval = mwlExecuteCmd(mh, HostCmd_CMD_BASTREAM);
	} else
		retval = 0;
	/* NB: always reclaim stream */
	mh->mh_bastreams |= 1<<sp->stream;
	sp->public.data[0] = NULL;
	sp->public.data[1] = NULL;
	sp->setup = 0;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_bastream_get_seqno(struct mwl_hal *mh0,
	const MWL_HAL_BASTREAM *s, const uint8_t Macaddr[IEEE80211_ADDR_LEN],
	uint16_t *pseqno)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	struct mwl_hal_bastream *sp = __DECONST(struct mwl_hal_bastream *, s);
	HostCmd_GET_SEQNO *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_GET_SEQNO, HostCmd_CMD_GET_SEQNO);
	IEEE80211_ADDR_COPY(pCmd->MacAddr, Macaddr);
	pCmd->TID = sp->tid;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_GET_SEQNO);
	if (retval == 0)
		*pseqno = le16toh(pCmd->SeqNo);
	MWL_HAL_UNLOCK(mh);
	return retval;
}	

int
mwl_hal_getwatchdogbitmap(struct mwl_hal *mh0, uint8_t bitmap[1])
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_GET_WATCHDOG_BITMAP *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_GET_WATCHDOG_BITMAP,
		HostCmd_CMD_GET_WATCHDOG_BITMAP);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_GET_WATCHDOG_BITMAP);
	if (retval == 0) {
		bitmap[0] = pCmd->Watchdogbitmap;
		/* fw returns qid, map it to BA stream */
		if (bitmap[0] < MWL_BAQID_MAX)
			bitmap[0] = qid2ba[bitmap[0]];
	}
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Configure aggressive Ampdu rate mode.
 */
int
mwl_hal_setaggampduratemode(struct mwl_hal *mh0, int mode, int threshold)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_AMPDU_RETRY_RATEDROP_MODE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_AMPDU_RETRY_RATEDROP_MODE,
		HostCmd_CMD_AMPDU_RETRY_RATEDROP_MODE);
	pCmd->Action = htole16(1);
	pCmd->Option = htole32(mode);
	pCmd->Threshold = htole32(threshold);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_AMPDU_RETRY_RATEDROP_MODE);
	MWL_HAL_UNLOCK(mh);   
	return retval;
}

int
mwl_hal_getaggampduratemode(struct mwl_hal *mh0, int *mode, int *threshold)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_AMPDU_RETRY_RATEDROP_MODE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_AMPDU_RETRY_RATEDROP_MODE,
		HostCmd_CMD_AMPDU_RETRY_RATEDROP_MODE);
	pCmd->Action = htole16(0);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_AMPDU_RETRY_RATEDROP_MODE);
	MWL_HAL_UNLOCK(mh);   
	*mode =  le32toh(pCmd->Option);
	*threshold = le32toh(pCmd->Threshold);
	return retval;
}

/*
 * Set CFEND status Enable/Disable
 */
int
mwl_hal_setcfend(struct mwl_hal *mh0, int ena)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_CFEND_ENABLE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_CFEND_ENABLE,
		HostCmd_CMD_CFEND_ENABLE);
	pCmd->Enable = htole32(ena);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_CFEND_ENABLE);
	MWL_HAL_UNLOCK(mh); 
	return retval;
}

int
mwl_hal_setdwds(struct mwl_hal *mh0, int ena)
{
	HostCmd_DWDS_ENABLE *pCmd;
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
   	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DWDS_ENABLE, HostCmd_CMD_DWDS_ENABLE);
	pCmd->Enable = htole32(ena);
	retval = mwlExecuteCmd(mh, HostCmd_CMD_DWDS_ENABLE);
  	MWL_HAL_UNLOCK(mh);
	return retval;
}

static void
cvtPeerInfo(PeerInfo_t *to, const MWL_HAL_PEERINFO *from)
{
	to->LegacyRateBitMap = htole32(from->LegacyRateBitMap);
	to->HTRateBitMap = htole32(from->HTRateBitMap);
	to->CapInfo = htole16(from->CapInfo);
	to->HTCapabilitiesInfo = htole16(from->HTCapabilitiesInfo);
	to->MacHTParamInfo = from->MacHTParamInfo;
	to->AddHtInfo.ControlChan = from->AddHtInfo.ControlChan;
	to->AddHtInfo.AddChan = from->AddHtInfo.AddChan;
	to->AddHtInfo.OpMode = htole16(from->AddHtInfo.OpMode);
	to->AddHtInfo.stbc = htole16(from->AddHtInfo.stbc);
}

/* XXX station id must be in [0..63] */
int
mwl_hal_newstation(struct mwl_hal_vap *vap,
	const uint8_t addr[IEEE80211_ADDR_LEN], uint16_t aid, uint16_t sid, 
	const MWL_HAL_PEERINFO *peer, int isQosSta, int wmeInfo)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_NEW_STN *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_NEW_STN, HostCmd_CMD_SET_NEW_STN);
	pCmd->AID = htole16(aid);
	pCmd->StnId = htole16(sid);
	pCmd->Action = htole16(0);	/* SET */
	if (peer != NULL) {
		/* NB: must fix up byte order */
		cvtPeerInfo(&pCmd->PeerInfo, peer);
	}
	IEEE80211_ADDR_COPY(&pCmd->MacAddr[0], addr);
	pCmd->Qosinfo = wmeInfo;
	pCmd->isQosSta = (isQosSta != 0);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_NEW_STN);
	if (retval == 0 && IEEE80211_ADDR_EQ(vap->mac, addr))
		vap->flags |= MVF_STATION;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_delstation(struct mwl_hal_vap *vap,
	const uint8_t addr[IEEE80211_ADDR_LEN])
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_NEW_STN *pCmd;
	int retval, islocal;

	MWL_HAL_LOCK(mh);
	islocal = IEEE80211_ADDR_EQ(vap->mac, addr);
	if (!islocal || (vap->flags & MVF_STATION)) {
		_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_NEW_STN,
		    HostCmd_CMD_SET_NEW_STN);
		pCmd->Action = htole16(2);	/* REMOVE */
		IEEE80211_ADDR_COPY(&pCmd->MacAddr[0], addr);
		retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_NEW_STN);
		if (islocal)
			vap->flags &= ~MVF_STATION;
	} else
		retval = 0;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Prod the firmware to age packets on station power
 * save queues and reap frames on the tx aggregation q's.
 */
int
mwl_hal_setkeepalive(struct mwl_hal *mh0)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_KEEP_ALIVE_TICK *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_KEEP_ALIVE_TICK,
		HostCmd_CMD_SET_KEEP_ALIVE);
	/*
	 * NB: tick must be 0 to prod the f/w;
	 *     a non-zero value is a noop.
	 */
	pCmd->tick = 0;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_KEEP_ALIVE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setapmode(struct mwl_hal_vap *vap, MWL_HAL_APMODE ApMode)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_APMODE *pCmd;
	int retval;

	/* XXX validate ApMode? */

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_APMODE, HostCmd_CMD_SET_APMODE);
	pCmd->ApMode = ApMode;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_APMODE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_stop(struct mwl_hal_vap *vap)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_DS_BSS_START *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	if (vap->flags & MVF_RUNNING) {
		_VCMD_SETUP(vap, pCmd, HostCmd_DS_BSS_START,
		    HostCmd_CMD_BSS_START);
		pCmd->Enable = htole32(HostCmd_ACT_GEN_OFF);
		retval = mwlExecuteCmd(mh, HostCmd_CMD_BSS_START);
	} else
		retval = 0;
	/* NB: mark !running regardless */
	vap->flags &= ~MVF_RUNNING;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_start(struct mwl_hal_vap *vap)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_DS_BSS_START *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_DS_BSS_START, HostCmd_CMD_BSS_START);
	pCmd->Enable = htole32(HostCmd_ACT_GEN_ON);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_BSS_START);
	if (retval == 0)
		vap->flags |= MVF_RUNNING;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setgprot(struct mwl_hal *mh0, int prot)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_G_PROTECT_FLAG *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_G_PROTECT_FLAG,
		HostCmd_CMD_SET_G_PROTECT_FLAG);
	pCmd->GProtectFlag  = htole32(prot);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_G_PROTECT_FLAG);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setwmm(struct mwl_hal *mh0, int onoff)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SetWMMMode *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SetWMMMode,
		HostCmd_CMD_SET_WMM_MODE);
	pCmd->Action = htole16(onoff);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_WMM_MODE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setedcaparams(struct mwl_hal *mh0, uint8_t qnum,
	uint32_t CWmin, uint32_t CWmax, uint8_t AIFSN,  uint16_t TXOPLimit)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_EDCA_PARAMS *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_EDCA_PARAMS,
		HostCmd_CMD_SET_EDCA_PARAMS);
	/*
	 * NB: CWmin and CWmax are always set.
	 *     TxOpLimit is set if bit 0x2 is marked in Action
	 *     AIFSN is set if bit 0x4 is marked in Action
	 */
	pCmd->Action = htole16(0xffff);	/* NB: set everything */
	pCmd->TxOP = htole16(TXOPLimit);
	pCmd->CWMax = htole32(CWmax);
	pCmd->CWMin = htole32(CWmin);
	pCmd->AIFSN = AIFSN;
	pCmd->TxQNum = qnum;		/* XXX check */

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_EDCA_PARAMS);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/* XXX 0 = indoor, 1 = outdoor */
int
mwl_hal_setrateadaptmode(struct mwl_hal *mh0, uint16_t mode)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_SET_RATE_ADAPT_MODE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_SET_RATE_ADAPT_MODE,
		HostCmd_CMD_SET_RATE_ADAPT_MODE);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET);
	pCmd->RateAdaptMode = htole16(mode);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_RATE_ADAPT_MODE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setcsmode(struct mwl_hal *mh0, MWL_HAL_CSMODE csmode)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_DS_SET_LINKADAPT_CS_MODE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_SET_LINKADAPT_CS_MODE,
		HostCmd_CMD_SET_LINKADAPT_CS_MODE);
	pCmd->Action = htole16(HostCmd_ACT_GEN_SET);
	pCmd->CSMode = htole16(csmode);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_LINKADAPT_CS_MODE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setnprot(struct mwl_hal_vap *vap, MWL_HAL_HTPROTECT mode)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_N_PROTECT_FLAG *pCmd;
	int retval;

	/* XXX validate mode */
	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_N_PROTECT_FLAG,
		HostCmd_CMD_SET_N_PROTECT_FLAG);
	pCmd->NProtectFlag  = htole32(mode);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_N_PROTECT_FLAG);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setnprotmode(struct mwl_hal_vap *vap, uint8_t mode)
{
	struct mwl_hal_priv *mh = MWLVAP(vap);
	HostCmd_FW_SET_N_PROTECT_OPMODE *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_VCMD_SETUP(vap, pCmd, HostCmd_FW_SET_N_PROTECT_OPMODE,
		HostCmd_CMD_SET_N_PROTECT_OPMODE);
	pCmd->NProtectOpMode = mode;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_N_PROTECT_OPMODE);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setoptimizationlevel(struct mwl_hal *mh0, int level)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_OPTIMIZATION_LEVEL *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_OPTIMIZATION_LEVEL,
		HostCmd_CMD_SET_OPTIMIZATION_LEVEL);
	pCmd->OptLevel = level;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_OPTIMIZATION_LEVEL);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setmimops(struct mwl_hal *mh0, const uint8_t addr[IEEE80211_ADDR_LEN],
	uint8_t enable, uint8_t mode)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_MIMOPSHT *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_MIMOPSHT, HostCmd_CMD_SET_MIMOPSHT);
	IEEE80211_ADDR_COPY(pCmd->Addr, addr);
	pCmd->Enable = enable;
	pCmd->Mode = mode;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_MIMOPSHT);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

static int
mwlGetCalTable(struct mwl_hal_priv *mh, uint8_t annex, uint8_t index)
{
	HostCmd_FW_GET_CALTABLE *pCmd;
	int retval;

	MWL_HAL_LOCK_ASSERT(mh);

	_CMD_SETUP(pCmd, HostCmd_FW_GET_CALTABLE, HostCmd_CMD_GET_CALTABLE);
	pCmd->annex = annex;
	pCmd->index = index;
	memset(pCmd->calTbl, 0, sizeof(pCmd->calTbl));

	retval = mwlExecuteCmd(mh, HostCmd_CMD_GET_CALTABLE);
	if (retval == 0 &&
	    pCmd->calTbl[0] != annex && annex != 0 && annex != 255)
		retval = EIO;
	return retval;
}							  

/*
 * Calculate the max tx power from the channel's cal data.
 */
static void
setmaxtxpow(struct mwl_hal_channel *hc, int i, int maxix)
{
	hc->maxTxPow = hc->targetPowers[i];
	for (i++; i < maxix; i++)
		if (hc->targetPowers[i] > hc->maxTxPow)
			hc->maxTxPow = hc->targetPowers[i];
}

/*
 * Construct channel info for 5GHz channels from cal data.
 */
static void
get5Ghz(MWL_HAL_CHANNELINFO *ci, const uint8_t table[], int len)
{
	int i, j, f, l, h;

	l = 32000;
	h = 0;
	j = 0;
	for (i = 0; i < len; i += 4) {
		struct mwl_hal_channel *hc;

		if (table[i] == 0)
			continue;
		f = 5000 + 5*table[i];
		if (f < l)
			l = f;
		if (f > h)
			h = f;
		hc = &ci->channels[j];
		hc->freq = f;
		hc->ieee = table[i];
		memcpy(hc->targetPowers, &table[i], 4);
		setmaxtxpow(hc, 1, 4);		/* NB: col 1 is the freq, skip*/
		j++;
	}
	ci->nchannels = j;
	ci->freqLow = (l == 32000) ? 0 : l;
	ci->freqHigh = h;
}

static uint16_t
ieee2mhz(int chan)
{
	if (chan == 14)
		return 2484;
	if (chan < 14)
		return 2407 + chan*5;
	return 2512 + (chan-15)*20;
}

/*
 * Construct channel info for 2.4GHz channels from cal data.
 */
static void
get2Ghz(MWL_HAL_CHANNELINFO *ci, const uint8_t table[], int len)
{
	int i, j;

	j = 0;
	for (i = 0; i < len; i += 4) {
		struct mwl_hal_channel *hc = &ci->channels[j];
		hc->ieee = 1+j;
		hc->freq = ieee2mhz(1+j);
		memcpy(hc->targetPowers, &table[i], 4);
		setmaxtxpow(hc, 0, 4);
		j++;
	}
	ci->nchannels = j;
	ci->freqLow = ieee2mhz(1);
	ci->freqHigh = ieee2mhz(j);
}

#undef DUMPCALDATA
#ifdef DUMPCALDATA
static void
dumpcaldata(const char *name, const uint8_t *table, int n)
{
	int i;
	printf("\n%s:\n", name);
	for (i = 0; i < n; i += 4)
		printf("[%2d] %3d %3d %3d %3d\n", i/4, table[i+0], table[i+1], table[i+2], table[i+3]);
}
#endif

static int
mwlGetPwrCalTable(struct mwl_hal_priv *mh)
{
	const uint8_t *data;
	MWL_HAL_CHANNELINFO *ci;
	int len;

	MWL_HAL_LOCK(mh);
	/* NB: we hold the lock so it's ok to use cmdbuf */
	data = ((const HostCmd_FW_GET_CALTABLE *) mh->mh_cmdbuf)->calTbl;
	if (mwlGetCalTable(mh, 33, 0) == 0) {
		len = (data[2] | (data[3] << 8)) - 12;
		if (len > PWTAGETRATETABLE20M)
			len = PWTAGETRATETABLE20M;
#ifdef DUMPCALDATA
dumpcaldata("2.4G 20M", &data[12], len);/*XXX*/
#endif
		get2Ghz(&mh->mh_20M, &data[12], len);
	}
	if (mwlGetCalTable(mh, 34, 0) == 0) {
		len = (data[2] | (data[3] << 8)) - 12;
		if (len > PWTAGETRATETABLE40M)
			len = PWTAGETRATETABLE40M;
#ifdef DUMPCALDATA
dumpcaldata("2.4G 40M", &data[12], len);/*XXX*/
#endif
		ci = &mh->mh_40M;
		get2Ghz(ci, &data[12], len);
	}
	if (mwlGetCalTable(mh, 35, 0) == 0) {
		len = (data[2] | (data[3] << 8)) - 20;
		if (len > PWTAGETRATETABLE20M_5G)
			len = PWTAGETRATETABLE20M_5G;
#ifdef DUMPCALDATA
dumpcaldata("5G 20M", &data[20], len);/*XXX*/
#endif
		get5Ghz(&mh->mh_20M_5G, &data[20], len);
	}
	if (mwlGetCalTable(mh, 36, 0) == 0) {
		len = (data[2] | (data[3] << 8)) - 20;
		if (len > PWTAGETRATETABLE40M_5G)
			len = PWTAGETRATETABLE40M_5G;
#ifdef DUMPCALDATA
dumpcaldata("5G 40M", &data[20], len);/*XXX*/
#endif
		ci = &mh->mh_40M_5G;
		get5Ghz(ci, &data[20], len);
	}
	mh->mh_flags |= MHF_CALDATA;
	MWL_HAL_UNLOCK(mh);
	return 0;
}

int
mwl_hal_getregioncode(struct mwl_hal *mh0, uint8_t *countryCode)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	int retval;

	MWL_HAL_LOCK(mh);
	retval = mwlGetCalTable(mh, 0, 0);
	if (retval == 0) {
		const HostCmd_FW_GET_CALTABLE *pCmd =
		    (const HostCmd_FW_GET_CALTABLE *) mh->mh_cmdbuf;
		*countryCode = pCmd->calTbl[16];
	}
	MWL_HAL_UNLOCK(mh);
	return retval;
}

int
mwl_hal_setpromisc(struct mwl_hal *mh0, int ena)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	uint32_t v;

	MWL_HAL_LOCK(mh);
	v = RD4(mh, MACREG_REG_PROMISCUOUS);
	WR4(mh, MACREG_REG_PROMISCUOUS, ena ? v | 1 : v &~ 1);
	MWL_HAL_UNLOCK(mh);
	return 0;
}

int
mwl_hal_getpromisc(struct mwl_hal *mh0)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	uint32_t v;

	MWL_HAL_LOCK(mh);
	v = RD4(mh, MACREG_REG_PROMISCUOUS);
	MWL_HAL_UNLOCK(mh);
	return (v & 1) != 0;
}

int
mwl_hal_GetBeacon(struct mwl_hal *mh0, uint8_t *pBcn, uint16_t *pLen)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_GET_BEACON *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_GET_BEACON, HostCmd_CMD_GET_BEACON);
	pCmd->Bcnlen = htole16(0);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_GET_BEACON);
	if (retval == 0) {
		/* XXX bounds check */
		memcpy(pBcn, &pCmd->Bcn, pCmd->Bcnlen);
		*pLen = pCmd->Bcnlen;
	}
	MWL_HAL_UNLOCK(mh);
	return retval;
}	

int
mwl_hal_SetRifs(struct mwl_hal *mh0, uint8_t QNum)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	HostCmd_FW_SET_RIFS  *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_FW_SET_RIFS, HostCmd_CMD_SET_RIFS);
	pCmd->QNum = QNum;

	retval = mwlExecuteCmd(mh, HostCmd_CMD_SET_RIFS);
	MWL_HAL_UNLOCK(mh);
	return retval;
}

/*
 * Diagnostic api's for set/get registers.
 */

static int
getRFReg(struct mwl_hal_priv *mh, int flag, uint32_t reg, uint32_t *val)
{
	HostCmd_DS_RF_REG_ACCESS *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_RF_REG_ACCESS, HostCmd_CMD_RF_REG_ACCESS);
	pCmd->Offset =  htole16(reg);
	pCmd->Action = htole16(flag);
	pCmd->Value = htole32(*val);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_RF_REG_ACCESS);
	if (retval == 0)
		*val = pCmd->Value;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

static int
getBBReg(struct mwl_hal_priv *mh, int flag, uint32_t reg, uint32_t *val)
{
	HostCmd_DS_BBP_REG_ACCESS *pCmd;
	int retval;

	MWL_HAL_LOCK(mh);
	_CMD_SETUP(pCmd, HostCmd_DS_BBP_REG_ACCESS, HostCmd_CMD_BBP_REG_ACCESS);
	pCmd->Offset =  htole16(reg);
	pCmd->Action = htole16(flag);
	pCmd->Value = htole32(*val);

	retval = mwlExecuteCmd(mh, HostCmd_CMD_BBP_REG_ACCESS);
	if (retval == 0)
		*val = pCmd->Value;
	MWL_HAL_UNLOCK(mh);
	return retval;
}

static u_int
mwl_hal_getregdump(struct mwl_hal_priv *mh, const MWL_DIAG_REGRANGE *regs,
	void *dstbuf, int space)
{
	uint32_t *dp = dstbuf;
	int i;

	for (i = 0; space >= 2*sizeof(uint32_t); i++) {
		u_int r = regs[i].start;
		u_int e = regs[i].end;
		*dp++ = (r<<16) | e;
		space -= sizeof(uint32_t);
		do {
			if (MWL_DIAG_ISMAC(r))
				*dp = RD4(mh, r);
			else if (MWL_DIAG_ISBB(r))
				getBBReg(mh, HostCmd_ACT_GEN_READ,
				    r - MWL_DIAG_BASE_BB, dp);
			else if (MWL_DIAG_ISRF(r))
				getRFReg(mh, HostCmd_ACT_GEN_READ,
				    r - MWL_DIAG_BASE_RF, dp);
			else if (r < 0x1000 || r == MACREG_REG_FW_PRESENT)
				*dp = RD4(mh, r);
			else
				*dp = 0xffffffff;
			dp++;
			r += sizeof(uint32_t);
			space -= sizeof(uint32_t);
		} while (r <= e && space >= sizeof(uint32_t));
	}
	return (char *) dp - (char *) dstbuf;
}

int
mwl_hal_getdiagstate(struct mwl_hal *mh0, int request,
	const void *args, uint32_t argsize,
	void **result, uint32_t *resultsize)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);

	switch (request) {
	case MWL_DIAG_CMD_REVS:
		*result = &mh->mh_revs;
		*resultsize = sizeof(mh->mh_revs);
		return 1;
	case MWL_DIAG_CMD_REGS:
		*resultsize = mwl_hal_getregdump(mh, args, *result, *resultsize);
		return 1;
	case MWL_DIAG_CMD_HOSTCMD: {
		FWCmdHdr *pCmd = (FWCmdHdr *) &mh->mh_cmdbuf[0];
		int retval;

		MWL_HAL_LOCK(mh);
		memcpy(pCmd, args, argsize);
		retval = mwlExecuteCmd(mh, le16toh(pCmd->Cmd));
		*result = (*resultsize != 0) ? pCmd : NULL;
		MWL_HAL_UNLOCK(mh);
		return (retval == 0);
	}
	case MWL_DIAG_CMD_FWLOAD:
		if (mwl_hal_fwload(mh0, __DECONST(void *, args))) {
			device_printf(mh->mh_dev, "problem loading fw image\n");
			return 0;
		}
		return 1;
	}
	return 0;
}

/*
 * Low level firmware cmd block handshake support.
 */

static void
mwlSendCmd(struct mwl_hal_priv *mh)
{
	uint32_t dummy;

	bus_dmamap_sync(mh->mh_dmat, mh->mh_dmamap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	WR4(mh, MACREG_REG_GEN_PTR, mh->mh_cmdaddr);
	dummy = RD4(mh, MACREG_REG_INT_CODE);

	WR4(mh, MACREG_REG_H2A_INTERRUPT_EVENTS, MACREG_H2ARIC_BIT_DOOR_BELL);
}

static int
mwlWaitForCmdComplete(struct mwl_hal_priv *mh, uint16_t cmdCode)
{
#define MAX_WAIT_FW_COMPLETE_ITERATIONS 10000
	int i;

	for (i = 0; i < MAX_WAIT_FW_COMPLETE_ITERATIONS; i++) {
		if (mh->mh_cmdbuf[0] == le16toh(cmdCode))
			return 1;
		DELAY(1*1000);
	}
	return 0;
#undef MAX_WAIT_FW_COMPLETE_ITERATIONS
}

static int
mwlExecuteCmd(struct mwl_hal_priv *mh, unsigned short cmd)
{

	MWL_HAL_LOCK_ASSERT(mh);

	if ((mh->mh_flags & MHF_FWHANG) &&
	    (mh->mh_debug & MWL_HAL_DEBUG_IGNHANG) == 0) {
#ifdef MWLHAL_DEBUG
		device_printf(mh->mh_dev, "firmware hung, skipping cmd %s\n",
			mwlcmdname(cmd));
#else
		device_printf(mh->mh_dev, "firmware hung, skipping cmd 0x%x\n",
			cmd);
#endif
		return ENXIO;
	}
	if (RD4(mh,  MACREG_REG_INT_CODE) == 0xffffffff) {
		device_printf(mh->mh_dev, "%s: device not present!\n",
		    __func__);
		return EIO;
	}
#ifdef MWLHAL_DEBUG
	if (mh->mh_debug & MWL_HAL_DEBUG_SENDCMD)
		dumpresult(mh, 0);
#endif
	mwlSendCmd(mh);
	if (!mwlWaitForCmdComplete(mh, 0x8000 | cmd)) {
#ifdef MWLHAL_DEBUG
		device_printf(mh->mh_dev,
		    "timeout waiting for f/w cmd %s\n", mwlcmdname(cmd));
#else
		device_printf(mh->mh_dev,
		    "timeout waiting for f/w cmd 0x%x\n", cmd);
#endif
		mh->mh_flags |= MHF_FWHANG;
		return ETIMEDOUT;
	}
	bus_dmamap_sync(mh->mh_dmat, mh->mh_dmamap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
#ifdef MWLHAL_DEBUG
	if (mh->mh_debug & MWL_HAL_DEBUG_CMDDONE)
		dumpresult(mh, 1);
#endif
	return 0;
}

/*
 * Firmware download support.
 */
#define FW_DOWNLOAD_BLOCK_SIZE	256  
#define FW_CHECK_USECS		(5*1000)	/* 5ms */
#define FW_MAX_NUM_CHECKS	200  

#if 0
/* XXX read f/w from file */
#include <dev/mwl/mwlbootfw.h>
#include <dev/mwl/mwl88W8363fw.h>
#endif

static void
mwlFwReset(struct mwl_hal_priv *mh)
{
	if (RD4(mh,  MACREG_REG_INT_CODE) == 0xffffffff) {
		device_printf(mh->mh_dev, "%s: device not present!\n",
		    __func__);
		return;
	}
	WR4(mh, MACREG_REG_H2A_INTERRUPT_EVENTS, ISR_RESET);
	mh->mh_flags &= ~MHF_FWHANG;
}

static void
mwlTriggerPciCmd(struct mwl_hal_priv *mh)
{
	uint32_t dummy;

	bus_dmamap_sync(mh->mh_dmat, mh->mh_dmamap, BUS_DMASYNC_PREWRITE);

	WR4(mh, MACREG_REG_GEN_PTR, mh->mh_cmdaddr);
	dummy = RD4(mh, MACREG_REG_INT_CODE);

	WR4(mh, MACREG_REG_INT_CODE, 0x00);
	dummy = RD4(mh, MACREG_REG_INT_CODE);

	WR4(mh, MACREG_REG_H2A_INTERRUPT_EVENTS, MACREG_H2ARIC_BIT_DOOR_BELL);
	dummy = RD4(mh, MACREG_REG_INT_CODE);
}

static int
mwlWaitFor(struct mwl_hal_priv *mh, uint32_t val)
{
	int i;

	for (i = 0; i < FW_MAX_NUM_CHECKS; i++) {
		DELAY(FW_CHECK_USECS);
		if (RD4(mh, MACREG_REG_INT_CODE) == val)
			return 1;
	}
	return 0;
}

/*
 * Firmware block xmit when talking to the boot-rom.
 */
static int
mwlSendBlock(struct mwl_hal_priv *mh, int bsize, const void *data, size_t dsize)
{
	mh->mh_cmdbuf[0] = htole16(HostCmd_CMD_CODE_DNLD);
	mh->mh_cmdbuf[1] = htole16(bsize);
	memcpy(&mh->mh_cmdbuf[4], data , dsize);
	mwlTriggerPciCmd(mh);
	/* XXX 2000 vs 200 */
	if (mwlWaitFor(mh, MACREG_INT_CODE_CMD_FINISHED)) {
		WR4(mh, MACREG_REG_INT_CODE, 0);
		return 1;
	}
	device_printf(mh->mh_dev,
	    "%s: timeout waiting for CMD_FINISHED, INT_CODE 0x%x\n",
	    __func__, RD4(mh, MACREG_REG_INT_CODE));
	return 0;
}

/*
 * Firmware block xmit when talking to the 1st-stage loader.
 */
static int
mwlSendBlock2(struct mwl_hal_priv *mh, const void *data, size_t dsize)
{
	memcpy(&mh->mh_cmdbuf[0], data, dsize);
	mwlTriggerPciCmd(mh);
	if (mwlWaitFor(mh, MACREG_INT_CODE_CMD_FINISHED)) {
		WR4(mh, MACREG_REG_INT_CODE, 0);
		return 1;
	}
	device_printf(mh->mh_dev,
	    "%s: timeout waiting for CMD_FINISHED, INT_CODE 0x%x\n",
	    __func__, RD4(mh, MACREG_REG_INT_CODE));
	return 0;
}

static void
mwlPokeSdramController(struct mwl_hal_priv *mh, int SDRAMSIZE_Addr)
{
	/** Set up sdram controller for superflyv2 **/
	WR4(mh, 0x00006014, 0x33);
	WR4(mh, 0x00006018, 0xa3a2632);
	WR4(mh, 0x00006010, SDRAMSIZE_Addr);
}

int
mwl_hal_fwload(struct mwl_hal *mh0, void *fwargs)
{
	struct mwl_hal_priv *mh = MWLPRIV(mh0);
	const char *fwname = "mw88W8363fw";
	const char *fwbootname = "mwlboot";
	const struct firmware *fwboot = NULL;
	const struct firmware *fw;
	/* XXX get from firmware header */
	uint32_t FwReadySignature = HostCmd_SOFTAP_FWRDY_SIGNATURE;
	uint32_t OpMode = HostCmd_SOFTAP_MODE;
	const uint8_t *fp, *ep;
	const uint8_t *fmdata;
	uint32_t blocksize, nbytes, fmsize;
	int i, error, ntries;

	fw = firmware_get(fwname);
	if (fw == NULL) {
		device_printf(mh->mh_dev,
		    "could not load firmware image %s\n", fwname);
		return ENXIO;
	}
	fmdata = fw->data;
	fmsize = fw->datasize;
	if (fmsize < 4) {
		device_printf(mh->mh_dev, "firmware image %s too small\n",
		    fwname);
		error = ENXIO;
		goto bad2;
	}
	if (fmdata[0] == 0x01 && fmdata[1] == 0x00 &&
	    fmdata[2] == 0x00 && fmdata[3] == 0x00) {
		/*
		 * 2-stage load, get the boot firmware.
		 */
		fwboot = firmware_get(fwbootname);
		if (fwboot == NULL) {
			device_printf(mh->mh_dev,
			    "could not load firmware image %s\n", fwbootname);
			error = ENXIO;
			goto bad2;
		}
	} else
		fwboot = NULL;

	mwlFwReset(mh);

	WR4(mh, MACREG_REG_A2H_INTERRUPT_CLEAR_SEL, MACREG_A2HRIC_BIT_MASK);
	WR4(mh, MACREG_REG_A2H_INTERRUPT_CAUSE, 0x00);
	WR4(mh, MACREG_REG_A2H_INTERRUPT_MASK, 0x00);
	WR4(mh, MACREG_REG_A2H_INTERRUPT_STATUS_MASK, MACREG_A2HRIC_BIT_MASK);
	if (mh->mh_SDRAMSIZE_Addr != 0) {
		/** Set up sdram controller for superflyv2 **/
		mwlPokeSdramController(mh, mh->mh_SDRAMSIZE_Addr);
	}
	device_printf(mh->mh_dev, "load %s firmware image (%u bytes)\n",
	    fwname, fmsize);
	if (fwboot != NULL) {
		/*
		 * Do 2-stage load.  The 1st stage loader is setup
		 * with the bootrom loader then we load the real
		 * image using a different handshake. With this
		 * mechanism the firmware is segmented into chunks
		 * that have a CRC.  If a chunk is incorrect we'll
		 * be told to retransmit.
		 */
		/* XXX assumes hlpimage fits in a block */
		/* NB: zero size block indicates download is finished */
		if (!mwlSendBlock(mh, fwboot->datasize, fwboot->data, fwboot->datasize) ||
		    !mwlSendBlock(mh, 0, NULL, 0)) {
			error = ETIMEDOUT;
			goto bad;
		}
		DELAY(200*FW_CHECK_USECS);
		if (mh->mh_SDRAMSIZE_Addr != 0) {
			/** Set up sdram controller for superflyv2 **/
			mwlPokeSdramController(mh, mh->mh_SDRAMSIZE_Addr);
		}
		nbytes = ntries = 0;		/* NB: silence compiler */
		for (fp = fmdata, ep = fp + fmsize; fp < ep; ) {
			WR4(mh, MACREG_REG_INT_CODE, 0);
			blocksize = RD4(mh, MACREG_REG_SCRATCH);
			if (blocksize == 0)	/* download complete */
				break;
			if (blocksize > 0x00000c00) {
				error = EINVAL;
				goto bad;
			}
			if ((blocksize & 0x1) == 0) {
				/* block successfully downloaded, advance */
				fp += nbytes;
				ntries = 0;
			} else {
				if (++ntries > 2) {
					/*
					 * Guard against f/w telling us to
					 * retry infinitely.
					 */
					error = ELOOP;
					goto bad;
				}
				/* clear NAK bit/flag */
				blocksize &= ~0x1;
			}
			if (blocksize > ep - fp) {
				/* XXX this should not happen, what to do? */
				blocksize = ep - fp;
			}
			nbytes = blocksize;
			if (!mwlSendBlock2(mh, fp, nbytes)) {
				error = ETIMEDOUT;
				goto bad;
			}
		}
	} else {
		for (fp = fmdata, ep = fp + fmsize; fp < ep;) {
			nbytes = ep - fp;
			if (nbytes > FW_DOWNLOAD_BLOCK_SIZE)
				nbytes = FW_DOWNLOAD_BLOCK_SIZE;
			if (!mwlSendBlock(mh, FW_DOWNLOAD_BLOCK_SIZE, fp, nbytes)) {
				error = EIO;
				goto bad;
			}
			fp += nbytes;
		}
	}
	/* done with firmware... */
	if (fwboot != NULL)
		firmware_put(fwboot, FIRMWARE_UNLOAD);
	firmware_put(fw, FIRMWARE_UNLOAD);
	/*
	 * Wait for firmware to startup; we monitor the
	 * INT_CODE register waiting for a signature to
	 * written back indicating it's ready to go.
	 */
	mh->mh_cmdbuf[1] = 0;
	/*
	 * XXX WAR for mfg fw download
	 */
	if (OpMode != HostCmd_STA_MODE)
		mwlTriggerPciCmd(mh);
	for (i = 0; i < FW_MAX_NUM_CHECKS; i++) {
		WR4(mh, MACREG_REG_GEN_PTR, OpMode);
		DELAY(FW_CHECK_USECS);
		if (RD4(mh, MACREG_REG_INT_CODE) == FwReadySignature) {
			WR4(mh, MACREG_REG_INT_CODE, 0x00);
			return mwlResetHalState(mh);
		}
	}
	return ETIMEDOUT;
bad:
	mwlFwReset(mh);
bad2:
	/* done with firmware... */
	if (fwboot != NULL)
		firmware_put(fwboot, FIRMWARE_UNLOAD);
	firmware_put(fw, FIRMWARE_UNLOAD);
	return error;
}

#ifdef MWLHAL_DEBUG
static const char *
mwlcmdname(int cmd)
{
	static char buf[12];
#define	CMD(x)	case HostCmd_CMD_##x: return #x
	switch (cmd) {
	CMD(CODE_DNLD);
	CMD(GET_HW_SPEC);
	CMD(SET_HW_SPEC);
	CMD(MAC_MULTICAST_ADR);
	CMD(802_11_GET_STAT);
	CMD(MAC_REG_ACCESS);
	CMD(BBP_REG_ACCESS);
	CMD(RF_REG_ACCESS);
	CMD(802_11_RADIO_CONTROL);
	CMD(802_11_RF_TX_POWER);
	CMD(802_11_RF_ANTENNA);
	CMD(SET_BEACON);
	CMD(SET_RF_CHANNEL);
	CMD(SET_AID);
	CMD(SET_INFRA_MODE);
	CMD(SET_G_PROTECT_FLAG);
	CMD(802_11_RTS_THSD);
	CMD(802_11_SET_SLOT);
	CMD(SET_EDCA_PARAMS);
	CMD(802_11H_DETECT_RADAR);
	CMD(SET_WMM_MODE);
	CMD(HT_GUARD_INTERVAL);
	CMD(SET_FIXED_RATE);
	CMD(SET_LINKADAPT_CS_MODE);
	CMD(SET_MAC_ADDR);
	CMD(SET_RATE_ADAPT_MODE);
	CMD(BSS_START);
	CMD(SET_NEW_STN);
	CMD(SET_KEEP_ALIVE);
	CMD(SET_APMODE);
	CMD(SET_SWITCH_CHANNEL);
	CMD(UPDATE_ENCRYPTION);
	CMD(BASTREAM);
	CMD(SET_RIFS);
	CMD(SET_N_PROTECT_FLAG);
	CMD(SET_N_PROTECT_OPMODE);
	CMD(SET_OPTIMIZATION_LEVEL);
	CMD(GET_CALTABLE);
	CMD(SET_MIMOPSHT);
	CMD(GET_BEACON);
	CMD(SET_REGION_CODE);
	CMD(SET_POWERSAVESTATION);
	CMD(SET_TIM);
	CMD(GET_TIM);
	CMD(GET_SEQNO);
	CMD(DWDS_ENABLE);
	CMD(AMPDU_RETRY_RATEDROP_MODE);
	CMD(CFEND_ENABLE);
	}
	snprintf(buf, sizeof(buf), "0x%x", cmd);
	return buf;
#undef CMD
}

static void
dumpresult(struct mwl_hal_priv *mh, int showresult)
{
	const FWCmdHdr *h = (const FWCmdHdr *)mh->mh_cmdbuf;
	const uint8_t *cp;
	int len, i;

	len = le16toh(h->Length);
#ifdef MWL_MBSS_SUPPORT
	device_printf(mh->mh_dev, "Cmd %s Length %d SeqNum %d MacId %d",
	    mwlcmdname(le16toh(h->Cmd) &~ 0x8000), len, h->SeqNum, h->MacId);
#else
	device_printf(mh->mh_dev, "Cmd %s Length %d SeqNum %d",
	    mwlcmdname(le16toh(h->Cmd) &~ 0x8000), len, le16toh(h->SeqNum));
#endif
	if (showresult) {
		const char *results[] =
		    { "OK", "ERROR", "NOT_SUPPORT", "PENDING", "BUSY",
		      "PARTIAL_DATA" };
		int result = le16toh(h->Result);

		if (result <= HostCmd_RESULT_PARTIAL_DATA)
			printf(" Result %s", results[result]);
		else
			printf(" Result %d", result);
	}
	cp = (const uint8_t *)h;
	for (i = 0; i < len; i++) {
		if ((i % 16) == 0)
			printf("\n%02x", cp[i]);
		else
			printf(" %02x", cp[i]);
	}
	printf("\n");
}
#endif /* MWLHAL_DEBUG */
