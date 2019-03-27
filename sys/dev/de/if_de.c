/*	$NetBSD: if_de.c,v 1.86 1999/06/01 19:17:59 thorpej Exp $	*/
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1994-1997 Matt Thomas (matt@3am-software.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Id: if_de.c,v 1.94 1997/07/03 16:55:07 thomas Exp
 */

/*
 * DEC 21040 PCI Ethernet Controller
 *
 * Written by Matt Thomas
 * BPF support code stolen directly from if_ec.c
 *
 *   This driver supports the DEC DE435 or any other PCI
 *   board which support 21040, 21041, or 21140 (mostly).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	TULIP_HDR_DATA

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/ktr.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/eventhandler.h>
#include <machine/bus.h>
#include <machine/bus_dma.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_dl.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <vm/vm.h>

#include <net/if_var.h>
#include <vm/pmap.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/de/dc21040reg.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

/*
 * Intel CPUs should use I/O mapped access.
 */
#if defined(__i386__)
#define	TULIP_IOMAPPED
#endif

#if 0
/* This enables KTR traces at KTR_DEV. */
#define	KTR_TULIP	KTR_DEV
#else
#define	KTR_TULIP	0
#endif

#if 0
/*
 * This turns on all sort of debugging stuff and make the
 * driver much larger.
 */
#define TULIP_DEBUG
#endif

#if 0
#define	TULIP_PERFSTATS
#endif

#define	TULIP_HZ	10

#include <dev/de/if_devar.h>

#define	SYNC_NONE	0
#define	SYNC_RX		1
#define	SYNC_TX		2

/*
 * This module supports
 *	the DEC 21040 PCI Ethernet Controller.
 *	the DEC 21041 PCI Ethernet Controller.
 *	the DEC 21140 PCI Fast Ethernet Controller.
 */
static void	tulip_addr_filter(tulip_softc_t * const sc);
static int	tulip_ifmedia_change(struct ifnet * const ifp);
static void	tulip_ifmedia_status(struct ifnet * const ifp,
		    struct ifmediareq *req);
static void	tulip_init(void *);
static void	tulip_init_locked(tulip_softc_t * const sc);
static void	tulip_intr_shared(void *arg);
static void	tulip_intr_normal(void *arg);
static void	tulip_mii_autonegotiate(tulip_softc_t * const sc,
		    const unsigned phyaddr);
static int	tulip_mii_map_abilities(tulip_softc_t * const sc,
		    unsigned abilities);
static tulip_media_t
		tulip_mii_phy_readspecific(tulip_softc_t * const sc);
static unsigned	tulip_mii_readreg(tulip_softc_t * const sc, unsigned devaddr,
		    unsigned regno);
static void	tulip_mii_writereg(tulip_softc_t * const sc, unsigned devaddr,
		    unsigned regno, unsigned data);
static void	tulip_reset(tulip_softc_t * const sc);
static void	tulip_rx_intr(tulip_softc_t * const sc);
static int	tulip_srom_decode(tulip_softc_t * const sc);
static void	tulip_start(struct ifnet *ifp);
static void	tulip_start_locked(tulip_softc_t * const sc);
static struct mbuf *
		tulip_txput(tulip_softc_t * const sc, struct mbuf *m);
static void	tulip_txput_setup(tulip_softc_t * const sc);
static void	tulip_watchdog(void *arg);
struct mbuf *	tulip_dequeue_mbuf(tulip_ringinfo_t *ri, tulip_descinfo_t *di,
		    int sync);
static void	tulip_dma_map_addr(void *, bus_dma_segment_t *, int, int);
static void	tulip_dma_map_rxbuf(void *, bus_dma_segment_t *, int,
		    bus_size_t, int);

static void
tulip_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
    bus_addr_t *paddr;

    if (error)
	return;

    paddr = arg;
    *paddr = segs->ds_addr;
}

static void
tulip_dma_map_rxbuf(void *arg, bus_dma_segment_t *segs, int nseg,
    bus_size_t mapsize, int error)
{
    tulip_desc_t *desc;

    if (error)
	return;

    desc = arg;
    KASSERT(nseg == 1, ("too many DMA segments"));
    KASSERT(segs[0].ds_len >= TULIP_RX_BUFLEN, ("receive buffer too small"));

    desc->d_addr1 = segs[0].ds_addr & 0xffffffff;
    desc->d_length1 = TULIP_RX_BUFLEN;
#ifdef not_needed
    /* These should already always be zero. */
    desc->d_addr2 = 0;
    desc->d_length2 = 0;
#endif
}

struct mbuf *
tulip_dequeue_mbuf(tulip_ringinfo_t *ri, tulip_descinfo_t *di, int sync)
{
    struct mbuf *m;

    m = di->di_mbuf;
    if (m != NULL) {
	switch (sync) {
	case SYNC_NONE:
	    break;
	case SYNC_RX:
	    TULIP_RXMAP_POSTSYNC(ri, di);
	    break;
	case SYNC_TX:
	    TULIP_TXMAP_POSTSYNC(ri, di);
	    break;
	default:
	    panic("bad sync flag: %d", sync);
	}
	bus_dmamap_unload(ri->ri_data_tag, *di->di_map);
	di->di_mbuf = NULL;
    }
    return (m);
}

static void
tulip_timeout_callback(void *arg)
{
    tulip_softc_t * const sc = arg;

    TULIP_PERFSTART(timeout)
    TULIP_LOCK_ASSERT(sc);

    sc->tulip_flags &= ~TULIP_TIMEOUTPENDING;
    sc->tulip_probe_timeout -= 1000 / TULIP_HZ;
    (sc->tulip_boardsw->bd_media_poll)(sc, TULIP_MEDIAPOLL_TIMER);

    TULIP_PERFEND(timeout);
}

static void
tulip_timeout(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    if (sc->tulip_flags & TULIP_TIMEOUTPENDING)
	return;
    sc->tulip_flags |= TULIP_TIMEOUTPENDING;
    callout_reset(&sc->tulip_callout, (hz + TULIP_HZ / 2) / TULIP_HZ,
	tulip_timeout_callback, sc);
}

static int
tulip_txprobe(tulip_softc_t * const sc)
{
    struct mbuf *m;
    u_char *enaddr;

    /*
     * Before we are sure this is the right media we need
     * to send a small packet to make sure there's carrier.
     * Strangely, BNC and AUI will "see" receive data if
     * either is connected so the transmit is the only way
     * to verify the connectivity.
     */
    TULIP_LOCK_ASSERT(sc);
    MGETHDR(m, M_NOWAIT, MT_DATA);
    if (m == NULL)
	return 0;
    /*
     * Construct a LLC TEST message which will point to ourselves.
     */
    if (sc->tulip_ifp->if_input != NULL)
	enaddr = IF_LLADDR(sc->tulip_ifp);
    else
	enaddr = sc->tulip_enaddr;
    bcopy(enaddr, mtod(m, struct ether_header *)->ether_dhost, ETHER_ADDR_LEN);
    bcopy(enaddr, mtod(m, struct ether_header *)->ether_shost, ETHER_ADDR_LEN);
    mtod(m, struct ether_header *)->ether_type = htons(3);
    mtod(m, unsigned char *)[14] = 0;
    mtod(m, unsigned char *)[15] = 0;
    mtod(m, unsigned char *)[16] = 0xE3;	/* LLC Class1 TEST (no poll) */
    m->m_len = m->m_pkthdr.len = sizeof(struct ether_header) + 3;
    /*
     * send it!
     */
    sc->tulip_cmdmode |= TULIP_CMD_TXRUN;
    sc->tulip_intrmask |= TULIP_STS_TXINTR;
    sc->tulip_flags |= TULIP_TXPROBE_ACTIVE;
    TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
    TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
    if ((m = tulip_txput(sc, m)) != NULL)
	m_freem(m);
    sc->tulip_probe.probe_txprobes++;
    return 1;
}

static void
tulip_media_set(tulip_softc_t * const sc, tulip_media_t media)
{
    const tulip_media_info_t *mi = sc->tulip_mediums[media];

    TULIP_LOCK_ASSERT(sc);
    if (mi == NULL)
	return;

    /*
     * If we are switching media, make sure we don't think there's
     * any stale RX activity
     */
    sc->tulip_flags &= ~TULIP_RXACT;
    if (mi->mi_type == TULIP_MEDIAINFO_SIA) {
	TULIP_CSR_WRITE(sc, csr_sia_connectivity, TULIP_SIACONN_RESET);
	TULIP_CSR_WRITE(sc, csr_sia_tx_rx,        mi->mi_sia_tx_rx);
	if (sc->tulip_features & TULIP_HAVE_SIAGP) {
	    TULIP_CSR_WRITE(sc, csr_sia_general,  mi->mi_sia_gp_control|mi->mi_sia_general);
	    DELAY(50);
	    TULIP_CSR_WRITE(sc, csr_sia_general,  mi->mi_sia_gp_data|mi->mi_sia_general);
	} else {
	    TULIP_CSR_WRITE(sc, csr_sia_general,  mi->mi_sia_general);
	}
	TULIP_CSR_WRITE(sc, csr_sia_connectivity, mi->mi_sia_connectivity);
    } else if (mi->mi_type == TULIP_MEDIAINFO_GPR) {
#define	TULIP_GPR_CMDBITS	(TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION|TULIP_CMD_SCRAMBLER|TULIP_CMD_TXTHRSHLDCTL)
	/*
	 * If the cmdmode bits don't match the currently operating mode,
	 * set the cmdmode appropriately and reset the chip.
	 */
	if (((mi->mi_cmdmode ^ TULIP_CSR_READ(sc, csr_command)) & TULIP_GPR_CMDBITS) != 0) {
	    sc->tulip_cmdmode &= ~TULIP_GPR_CMDBITS;
	    sc->tulip_cmdmode |= mi->mi_cmdmode;
	    tulip_reset(sc);
	}
	TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET|sc->tulip_gpinit);
	DELAY(10);
	TULIP_CSR_WRITE(sc, csr_gp, (u_int8_t) mi->mi_gpdata);
    } else if (mi->mi_type == TULIP_MEDIAINFO_SYM) {
	/*
	 * If the cmdmode bits don't match the currently operating mode,
	 * set the cmdmode appropriately and reset the chip.
	 */
	if (((mi->mi_cmdmode ^ TULIP_CSR_READ(sc, csr_command)) & TULIP_GPR_CMDBITS) != 0) {
	    sc->tulip_cmdmode &= ~TULIP_GPR_CMDBITS;
	    sc->tulip_cmdmode |= mi->mi_cmdmode;
	    tulip_reset(sc);
	}
	TULIP_CSR_WRITE(sc, csr_sia_general, mi->mi_gpcontrol);
	TULIP_CSR_WRITE(sc, csr_sia_general, mi->mi_gpdata);
    } else if (mi->mi_type == TULIP_MEDIAINFO_MII
	       && sc->tulip_probe_state != TULIP_PROBE_INACTIVE) {
	int idx;
	if (sc->tulip_features & TULIP_HAVE_SIAGP) {
	    const u_int8_t *dp;
	    dp = &sc->tulip_rombuf[mi->mi_reset_offset];
	    for (idx = 0; idx < mi->mi_reset_length; idx++, dp += 2) {
		DELAY(10);
		TULIP_CSR_WRITE(sc, csr_sia_general, (dp[0] + 256 * dp[1]) << 16);
	    }
	    sc->tulip_phyaddr = mi->mi_phyaddr;
	    dp = &sc->tulip_rombuf[mi->mi_gpr_offset];
	    for (idx = 0; idx < mi->mi_gpr_length; idx++, dp += 2) {
		DELAY(10);
		TULIP_CSR_WRITE(sc, csr_sia_general, (dp[0] + 256 * dp[1]) << 16);
	    }
	} else {
	    for (idx = 0; idx < mi->mi_reset_length; idx++) {
		DELAY(10);
		TULIP_CSR_WRITE(sc, csr_gp, sc->tulip_rombuf[mi->mi_reset_offset + idx]);
	    }
	    sc->tulip_phyaddr = mi->mi_phyaddr;
	    for (idx = 0; idx < mi->mi_gpr_length; idx++) {
		DELAY(10);
		TULIP_CSR_WRITE(sc, csr_gp, sc->tulip_rombuf[mi->mi_gpr_offset + idx]);
	    }
	}
	if (sc->tulip_flags & TULIP_TRYNWAY) {
	    tulip_mii_autonegotiate(sc, sc->tulip_phyaddr);
	} else if ((sc->tulip_flags & TULIP_DIDNWAY) == 0) {
	    u_int32_t data = tulip_mii_readreg(sc, sc->tulip_phyaddr, PHYREG_CONTROL);
	    data &= ~(PHYCTL_SELECT_100MB|PHYCTL_FULL_DUPLEX|PHYCTL_AUTONEG_ENABLE);
	    sc->tulip_flags &= ~TULIP_DIDNWAY;
	    if (TULIP_IS_MEDIA_FD(media))
		data |= PHYCTL_FULL_DUPLEX;
	    if (TULIP_IS_MEDIA_100MB(media))
		data |= PHYCTL_SELECT_100MB;
	    tulip_mii_writereg(sc, sc->tulip_phyaddr, PHYREG_CONTROL, data);
	}
    }
}

static void
tulip_linkup(tulip_softc_t * const sc, tulip_media_t media)
{
    TULIP_LOCK_ASSERT(sc);
    if ((sc->tulip_flags & TULIP_LINKUP) == 0)
	sc->tulip_flags |= TULIP_PRINTLINKUP;
    sc->tulip_flags |= TULIP_LINKUP;
    sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
#if 0 /* XXX how does with work with ifmedia? */
    if ((sc->tulip_flags & TULIP_DIDNWAY) == 0) {
	if (sc->tulip_ifp->if_flags & IFF_FULLDUPLEX) {
	    if (TULIP_CAN_MEDIA_FD(media)
		    && sc->tulip_mediums[TULIP_FD_MEDIA_OF(media)] != NULL)
		media = TULIP_FD_MEDIA_OF(media);
	} else {
	    if (TULIP_IS_MEDIA_FD(media)
		    && sc->tulip_mediums[TULIP_HD_MEDIA_OF(media)] != NULL)
		media = TULIP_HD_MEDIA_OF(media);
	}
    }
#endif
    if (sc->tulip_media != media) {
#ifdef TULIP_DEBUG
	sc->tulip_dbg.dbg_last_media = sc->tulip_media;
#endif
	sc->tulip_media = media;
	sc->tulip_flags |= TULIP_PRINTMEDIA;
	if (TULIP_IS_MEDIA_FD(sc->tulip_media)) {
	    sc->tulip_cmdmode |= TULIP_CMD_FULLDUPLEX;
	} else if (sc->tulip_chipid != TULIP_21041 || (sc->tulip_flags & TULIP_DIDNWAY) == 0) {
	    sc->tulip_cmdmode &= ~TULIP_CMD_FULLDUPLEX;
	}
    }
    /*
     * We could set probe_timeout to 0 but setting to 3000 puts this
     * in one central place and the only matters is tulip_link is
     * followed by a tulip_timeout.  Therefore setting it should not
     * result in aberrant behaviour.
     */
    sc->tulip_probe_timeout = 3000;
    sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
    sc->tulip_flags &= ~(TULIP_TXPROBE_ACTIVE|TULIP_TRYNWAY);
    if (sc->tulip_flags & TULIP_INRESET) {
	tulip_media_set(sc, sc->tulip_media);
    } else if (sc->tulip_probe_media != sc->tulip_media) {
	/*
	 * No reason to change media if we have the right media.
	 */
	tulip_reset(sc);
    }
    tulip_init_locked(sc);
}

static void
tulip_media_print(tulip_softc_t * const sc)
{

    TULIP_LOCK_ASSERT(sc);
    if ((sc->tulip_flags & TULIP_LINKUP) == 0)
	return;
    if (sc->tulip_flags & TULIP_PRINTMEDIA) {
	device_printf(sc->tulip_dev, "enabling %s port\n",
	    tulip_mediums[sc->tulip_media]);
	sc->tulip_flags &= ~(TULIP_PRINTMEDIA|TULIP_PRINTLINKUP);
    } else if (sc->tulip_flags & TULIP_PRINTLINKUP) {
	device_printf(sc->tulip_dev, "link up\n");
	sc->tulip_flags &= ~TULIP_PRINTLINKUP;
    }
}

#if defined(TULIP_DO_GPR_SENSE)
static tulip_media_t
tulip_21140_gpr_media_sense(tulip_softc_t * const sc)
{
    struct ifnet *ifp sc->tulip_ifp;
    tulip_media_t maybe_media = TULIP_MEDIA_UNKNOWN;
    tulip_media_t last_media = TULIP_MEDIA_UNKNOWN;
    tulip_media_t media;

    TULIP_LOCK_ASSERT(sc);

    /*
     * If one of the media blocks contained a default media flag,
     * use that.
     */
    for (media = TULIP_MEDIA_UNKNOWN; media < TULIP_MEDIA_MAX; media++) {
	const tulip_media_info_t *mi;
	/*
	 * Media is not supported (or is full-duplex).
	 */
	if ((mi = sc->tulip_mediums[media]) == NULL || TULIP_IS_MEDIA_FD(media))
	    continue;
	if (mi->mi_type != TULIP_MEDIAINFO_GPR)
	    continue;

	/*
	 * Remember the media is this is the "default" media.
	 */
	if (mi->mi_default && maybe_media == TULIP_MEDIA_UNKNOWN)
	    maybe_media = media;

	/*
	 * No activity mask?  Can't see if it is active if there's no mask.
	 */
	if (mi->mi_actmask == 0)
	    continue;

	/*
	 * Does the activity data match?
	 */
	if ((TULIP_CSR_READ(sc, csr_gp) & mi->mi_actmask) != mi->mi_actdata)
	    continue;

#if defined(TULIP_DEBUG)
	device_printf(sc->tulip_dev, "%s: %s: 0x%02x & 0x%02x == 0x%02x\n",
	    __func__, tulip_mediums[media], TULIP_CSR_READ(sc, csr_gp) & 0xFF,
	    mi->mi_actmask, mi->mi_actdata);
#endif
	/*
	 * It does!  If this is the first media we detected, then 
	 * remember this media.  If isn't the first, then there were
	 * multiple matches which we equate to no match (since we don't
	 * which to select (if any).
	 */
	if (last_media == TULIP_MEDIA_UNKNOWN) {
	    last_media = media;
	} else if (last_media != media) {
	    last_media = TULIP_MEDIA_UNKNOWN;
	}
    }
    return (last_media != TULIP_MEDIA_UNKNOWN) ? last_media : maybe_media;
}
#endif /* TULIP_DO_GPR_SENSE */

static tulip_link_status_t
tulip_media_link_monitor(tulip_softc_t * const sc)
{
    const tulip_media_info_t * const mi = sc->tulip_mediums[sc->tulip_media];
    tulip_link_status_t linkup = TULIP_LINK_DOWN;

    TULIP_LOCK_ASSERT(sc);
    if (mi == NULL) {
#if defined(DIAGNOSTIC) || defined(TULIP_DEBUG)
	panic("tulip_media_link_monitor: %s: botch at line %d\n",
	      tulip_mediums[sc->tulip_media],__LINE__);
#else
	return TULIP_LINK_UNKNOWN;
#endif
    }


    /*
     * Have we seen some packets?  If so, the link must be good.
     */
    if ((sc->tulip_flags & (TULIP_RXACT|TULIP_LINKUP)) == (TULIP_RXACT|TULIP_LINKUP)) {
	sc->tulip_flags &= ~TULIP_RXACT;
	sc->tulip_probe_timeout = 3000;
	return TULIP_LINK_UP;
    }

    sc->tulip_flags &= ~TULIP_RXACT;
    if (mi->mi_type == TULIP_MEDIAINFO_MII) {
	u_int32_t status;
	/*
	 * Read the PHY status register.
	 */
	status = tulip_mii_readreg(sc, sc->tulip_phyaddr, PHYREG_STATUS);
	if (status & PHYSTS_AUTONEG_DONE) {
	    /*
	     * If the PHY has completed autonegotiation, see the if the
	     * remote systems abilities have changed.  If so, upgrade or
	     * downgrade as appropriate.
	     */
	    u_int32_t abilities = tulip_mii_readreg(sc, sc->tulip_phyaddr, PHYREG_AUTONEG_ABILITIES);
	    abilities = (abilities << 6) & status;
	    if (abilities != sc->tulip_abilities) {
#if defined(TULIP_DEBUG)
		loudprintf("%s(phy%d): autonegotiation changed: 0x%04x -> 0x%04x\n",
			   ifp->if_xname, sc->tulip_phyaddr,
			   sc->tulip_abilities, abilities);
#endif
		if (tulip_mii_map_abilities(sc, abilities)) {
		    tulip_linkup(sc, sc->tulip_probe_media);
		    return TULIP_LINK_UP;
		}
		/*
		 * if we had selected media because of autonegotiation,
		 * we need to probe for the new media.
		 */
		sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
		if (sc->tulip_flags & TULIP_DIDNWAY)
		    return TULIP_LINK_DOWN;
	    }
	}
	/*
	 * The link is now up.  If was down, say its back up.
	 */
	if ((status & (PHYSTS_LINK_UP|PHYSTS_REMOTE_FAULT)) == PHYSTS_LINK_UP)
	    linkup = TULIP_LINK_UP;
    } else if (mi->mi_type == TULIP_MEDIAINFO_GPR) {
	/*
	 * No activity sensor?  Assume all's well.
	 */
	if (mi->mi_actmask == 0)
	    return TULIP_LINK_UNKNOWN;
	/*
	 * Does the activity data match?
	 */
	if ((TULIP_CSR_READ(sc, csr_gp) & mi->mi_actmask) == mi->mi_actdata)
	    linkup = TULIP_LINK_UP;
    } else if (mi->mi_type == TULIP_MEDIAINFO_SIA) {
	/*
	 * Assume non TP ok for now.
	 */
	if (!TULIP_IS_MEDIA_TP(sc->tulip_media))
	    return TULIP_LINK_UNKNOWN;
	if ((TULIP_CSR_READ(sc, csr_sia_status) & TULIP_SIASTS_LINKFAIL) == 0)
	    linkup = TULIP_LINK_UP;
#if defined(TULIP_DEBUG)
	if (sc->tulip_probe_timeout <= 0)
	    device_printf(sc->tulip_dev, "sia status = 0x%08x\n",
		    TULIP_CSR_READ(sc, csr_sia_status));
#endif
    } else if (mi->mi_type == TULIP_MEDIAINFO_SYM) {
	return TULIP_LINK_UNKNOWN;
    }
    /*
     * We will wait for 3 seconds until the link goes into suspect mode.
     */
    if (sc->tulip_flags & TULIP_LINKUP) {
	if (linkup == TULIP_LINK_UP)
	    sc->tulip_probe_timeout = 3000;
	if (sc->tulip_probe_timeout > 0)
	    return TULIP_LINK_UP;

	sc->tulip_flags &= ~TULIP_LINKUP;
	device_printf(sc->tulip_dev, "link down: cable problem?\n");
    }
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_link_downed++;
#endif
    return TULIP_LINK_DOWN;
}

static void
tulip_media_poll(tulip_softc_t * const sc, tulip_mediapoll_event_t event)
{

    TULIP_LOCK_ASSERT(sc);
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_events[event]++;
#endif
    if (sc->tulip_probe_state == TULIP_PROBE_INACTIVE
	    && event == TULIP_MEDIAPOLL_TIMER) {
	switch (tulip_media_link_monitor(sc)) {
	    case TULIP_LINK_DOWN: {
		/*
		 * Link Monitor failed.  Probe for new media.
		 */
		event = TULIP_MEDIAPOLL_LINKFAIL;
		break;
	    }
	    case TULIP_LINK_UP: {
		/*
		 * Check again soon.
		 */
		tulip_timeout(sc);
		return;
	    }
	    case TULIP_LINK_UNKNOWN: {
		/*
		 * We can't tell so don't bother.
		 */
		return;
	    }
	}
    }

    if (event == TULIP_MEDIAPOLL_LINKFAIL) {
	if (sc->tulip_probe_state == TULIP_PROBE_INACTIVE) {
	    if (TULIP_DO_AUTOSENSE(sc)) {
#if defined(TULIP_DEBUG)
		sc->tulip_dbg.dbg_link_failures++;
#endif
		sc->tulip_media = TULIP_MEDIA_UNKNOWN;
		if (sc->tulip_ifp->if_flags & IFF_UP)
		    tulip_reset(sc);	/* restart probe */
	    }
	    return;
	}
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_link_pollintrs++;
#endif
    }

    if (event == TULIP_MEDIAPOLL_START) {
	sc->tulip_ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	if (sc->tulip_probe_state != TULIP_PROBE_INACTIVE)
	    return;
	sc->tulip_probe_mediamask = 0;
	sc->tulip_probe_passes = 0;
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_media_probes++;
#endif
	/*
	 * If the SROM contained an explicit media to use, use it.
	 */
	sc->tulip_cmdmode &= ~(TULIP_CMD_RXRUN|TULIP_CMD_FULLDUPLEX);
	sc->tulip_flags |= TULIP_TRYNWAY|TULIP_PROBE1STPASS;
	sc->tulip_flags &= ~(TULIP_DIDNWAY|TULIP_PRINTMEDIA|TULIP_PRINTLINKUP);
	/*
	 * connidx is defaulted to a media_unknown type.
	 */
	sc->tulip_probe_media = tulip_srom_conninfo[sc->tulip_connidx].sc_media;
	if (sc->tulip_probe_media != TULIP_MEDIA_UNKNOWN) {
	    tulip_linkup(sc, sc->tulip_probe_media);
	    tulip_timeout(sc);
	    return;
	}

	if (sc->tulip_features & TULIP_HAVE_GPR) {
	    sc->tulip_probe_state = TULIP_PROBE_GPRTEST;
	    sc->tulip_probe_timeout = 2000;
	} else {
	    sc->tulip_probe_media = TULIP_MEDIA_MAX;
	    sc->tulip_probe_timeout = 0;
	    sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
	}
    }

    /*
     * Ignore txprobe failures or spurious callbacks.
     */
    if (event == TULIP_MEDIAPOLL_TXPROBE_FAILED
	    && sc->tulip_probe_state != TULIP_PROBE_MEDIATEST) {
	sc->tulip_flags &= ~TULIP_TXPROBE_ACTIVE;
	return;
    }

    /*
     * If we really transmitted a packet, then that's the media we'll use.
     */
    if (event == TULIP_MEDIAPOLL_TXPROBE_OK || event == TULIP_MEDIAPOLL_LINKPASS) {
	if (event == TULIP_MEDIAPOLL_LINKPASS) {
	    /* XXX Check media status just to be sure */
	    sc->tulip_probe_media = TULIP_MEDIA_10BASET;
#if defined(TULIP_DEBUG)
	} else {
	    sc->tulip_dbg.dbg_txprobes_ok[sc->tulip_probe_media]++;
#endif
	}
	tulip_linkup(sc, sc->tulip_probe_media);
	tulip_timeout(sc);
	return;
    }

    if (sc->tulip_probe_state == TULIP_PROBE_GPRTEST) {
#if defined(TULIP_DO_GPR_SENSE)
	/*
	 * Check for media via the general purpose register.
	 *
	 * Try to sense the media via the GPR.  If the same value
	 * occurs 3 times in a row then just use that.
	 */
	if (sc->tulip_probe_timeout > 0) {
	    tulip_media_t new_probe_media = tulip_21140_gpr_media_sense(sc);
#if defined(TULIP_DEBUG)
	    device_printf(sc->tulip_dev, "%s: gpr sensing = %s\n", __func__,
		   tulip_mediums[new_probe_media]);
#endif
	    if (new_probe_media != TULIP_MEDIA_UNKNOWN) {
		if (new_probe_media == sc->tulip_probe_media) {
		    if (--sc->tulip_probe_count == 0)
			tulip_linkup(sc, sc->tulip_probe_media);
		} else {
		    sc->tulip_probe_count = 10;
		}
	    }
	    sc->tulip_probe_media = new_probe_media;
	    tulip_timeout(sc);
	    return;
	}
#endif /* TULIP_DO_GPR_SENSE */
	/*
	 * Brute force.  We cycle through each of the media types
	 * and try to transmit a packet.
	 */
	sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
	sc->tulip_probe_media = TULIP_MEDIA_MAX;
	sc->tulip_probe_timeout = 0;
	tulip_timeout(sc);
	return;
    }

    if (sc->tulip_probe_state != TULIP_PROBE_MEDIATEST
	   && (sc->tulip_features & TULIP_HAVE_MII)) {
	tulip_media_t old_media = sc->tulip_probe_media;
	tulip_mii_autonegotiate(sc, sc->tulip_phyaddr);
	switch (sc->tulip_probe_state) {
	    case TULIP_PROBE_FAILED:
	    case TULIP_PROBE_MEDIATEST: {
		/*
		 * Try the next media.
		 */
		sc->tulip_probe_mediamask |= sc->tulip_mediums[sc->tulip_probe_media]->mi_mediamask;
		sc->tulip_probe_timeout = 0;
#ifdef notyet
		if (sc->tulip_probe_state == TULIP_PROBE_FAILED)
		    break;
		if (sc->tulip_probe_media != tulip_mii_phy_readspecific(sc))
		    break;
		sc->tulip_probe_timeout = TULIP_IS_MEDIA_TP(sc->tulip_probe_media) ? 2500 : 300;
#endif
		break;
	    }
	    case TULIP_PROBE_PHYAUTONEG: {
		return;
	    }
	    case TULIP_PROBE_INACTIVE: {
		/*
		 * Only probe if we autonegotiated a media that hasn't failed.
		 */
		sc->tulip_probe_timeout = 0;
		if (sc->tulip_probe_mediamask & TULIP_BIT(sc->tulip_probe_media)) {
		    sc->tulip_probe_media = old_media;
		    break;
		}
		tulip_linkup(sc, sc->tulip_probe_media);
		tulip_timeout(sc);
		return;
	    }
	    default: {
#if defined(DIAGNOSTIC) || defined(TULIP_DEBUG)
		panic("tulip_media_poll: botch at line %d\n", __LINE__);
#endif
		break;
	    }
	}
    }

    if (event == TULIP_MEDIAPOLL_TXPROBE_FAILED) {
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_txprobes_failed[sc->tulip_probe_media]++;
#endif
	sc->tulip_flags &= ~TULIP_TXPROBE_ACTIVE;
	return;
    }

    /*
     * switch to another media if we tried this one enough.
     */
    if (/* event == TULIP_MEDIAPOLL_TXPROBE_FAILED || */ sc->tulip_probe_timeout <= 0) {
#if defined(TULIP_DEBUG)
	if (sc->tulip_probe_media == TULIP_MEDIA_UNKNOWN) {
	    device_printf(sc->tulip_dev, "poll media unknown!\n");
	    sc->tulip_probe_media = TULIP_MEDIA_MAX;
	}
#endif
	/*
	 * Find the next media type to check for.  Full Duplex
	 * types are not allowed.
	 */
	do {
	    sc->tulip_probe_media -= 1;
	    if (sc->tulip_probe_media == TULIP_MEDIA_UNKNOWN) {
		if (++sc->tulip_probe_passes == 3) {
		    device_printf(sc->tulip_dev,
			"autosense failed: cable problem?\n");
		    if ((sc->tulip_ifp->if_flags & IFF_UP) == 0) {
			sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
			return;
		    }
		}
		sc->tulip_flags ^= TULIP_TRYNWAY;	/* XXX */
		sc->tulip_probe_mediamask = 0;
		sc->tulip_probe_media = TULIP_MEDIA_MAX - 1;
	    }
	} while (sc->tulip_mediums[sc->tulip_probe_media] == NULL
		 || (sc->tulip_probe_mediamask & TULIP_BIT(sc->tulip_probe_media))
		 || TULIP_IS_MEDIA_FD(sc->tulip_probe_media));

#if defined(TULIP_DEBUG)
	device_printf(sc->tulip_dev, "%s: probing %s\n",
	       event == TULIP_MEDIAPOLL_TXPROBE_FAILED ? "txprobe failed" : "timeout",
	       tulip_mediums[sc->tulip_probe_media]);
#endif
	sc->tulip_probe_timeout = TULIP_IS_MEDIA_TP(sc->tulip_probe_media) ? 2500 : 1000;
	sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
	sc->tulip_probe.probe_txprobes = 0;
	tulip_reset(sc);
	tulip_media_set(sc, sc->tulip_probe_media);
	sc->tulip_flags &= ~TULIP_TXPROBE_ACTIVE;
    }
    tulip_timeout(sc);

    /*
     * If this is hanging off a phy, we know are doing NWAY and we have
     * forced the phy to a specific speed.  Wait for link up before
     * before sending a packet.
     */
    switch (sc->tulip_mediums[sc->tulip_probe_media]->mi_type) {
	case TULIP_MEDIAINFO_MII: {
	    if (sc->tulip_probe_media != tulip_mii_phy_readspecific(sc))
		return;
	    break;
	}
	case TULIP_MEDIAINFO_SIA: {
	    if (TULIP_IS_MEDIA_TP(sc->tulip_probe_media)) {
		if (TULIP_CSR_READ(sc, csr_sia_status) & TULIP_SIASTS_LINKFAIL)
		    return;
		tulip_linkup(sc, sc->tulip_probe_media);
#ifdef notyet
		if (sc->tulip_features & TULIP_HAVE_MII)
		    tulip_timeout(sc);
#endif
		return;
	    }
	    break;
	}
	case TULIP_MEDIAINFO_RESET:
	case TULIP_MEDIAINFO_SYM:
	case TULIP_MEDIAINFO_NONE:
	case TULIP_MEDIAINFO_GPR: {
	    break;
	}
    }
    /*
     * Try to send a packet.
     */
    tulip_txprobe(sc);
}

static void
tulip_media_select(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    if (sc->tulip_features & TULIP_HAVE_GPR) {
	TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_PINSET|sc->tulip_gpinit);
	DELAY(10);
	TULIP_CSR_WRITE(sc, csr_gp, sc->tulip_gpdata);
    }
    /*
     * If this board has no media, just return
     */
    if (sc->tulip_features & TULIP_HAVE_NOMEDIA)
	return;

    if (sc->tulip_media == TULIP_MEDIA_UNKNOWN) {
	TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
	(*sc->tulip_boardsw->bd_media_poll)(sc, TULIP_MEDIAPOLL_START);
    } else {
	tulip_media_set(sc, sc->tulip_media);
    }
}

static void
tulip_21040_mediainfo_init(tulip_softc_t * const sc, tulip_media_t media)
{
    TULIP_LOCK_ASSERT(sc);
    sc->tulip_cmdmode |= TULIP_CMD_CAPTREFFCT|TULIP_CMD_THRSHLD160
	|TULIP_CMD_BACKOFFCTR;
    sc->tulip_ifp->if_baudrate = 10000000;

    if (media == TULIP_MEDIA_10BASET || media == TULIP_MEDIA_UNKNOWN) {
	TULIP_MEDIAINFO_SIA_INIT(sc, &sc->tulip_mediainfo[0], 21040, 10BASET);
	TULIP_MEDIAINFO_SIA_INIT(sc, &sc->tulip_mediainfo[1], 21040, 10BASET_FD);
	sc->tulip_intrmask |= TULIP_STS_LINKPASS|TULIP_STS_LINKFAIL;
    }

    if (media == TULIP_MEDIA_AUIBNC || media == TULIP_MEDIA_UNKNOWN) {
	TULIP_MEDIAINFO_SIA_INIT(sc, &sc->tulip_mediainfo[2], 21040, AUIBNC);
    }

    if (media == TULIP_MEDIA_UNKNOWN) {
	TULIP_MEDIAINFO_SIA_INIT(sc, &sc->tulip_mediainfo[3], 21040, EXTSIA);
    }
}

static void
tulip_21040_media_probe(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    tulip_21040_mediainfo_init(sc, TULIP_MEDIA_UNKNOWN);
    return;
}

static void
tulip_21040_10baset_only_media_probe(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    tulip_21040_mediainfo_init(sc, TULIP_MEDIA_10BASET);
    tulip_media_set(sc, TULIP_MEDIA_10BASET);
    sc->tulip_media = TULIP_MEDIA_10BASET;
}

static void
tulip_21040_10baset_only_media_select(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    sc->tulip_flags |= TULIP_LINKUP;
    if (sc->tulip_media == TULIP_MEDIA_10BASET_FD) {
	sc->tulip_cmdmode |= TULIP_CMD_FULLDUPLEX;
	sc->tulip_flags &= ~TULIP_SQETEST;
    } else {
	sc->tulip_cmdmode &= ~TULIP_CMD_FULLDUPLEX;
	sc->tulip_flags |= TULIP_SQETEST;
    }
    tulip_media_set(sc, sc->tulip_media);
}

static void
tulip_21040_auibnc_only_media_probe(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    tulip_21040_mediainfo_init(sc, TULIP_MEDIA_AUIBNC);
    sc->tulip_flags |= TULIP_SQETEST|TULIP_LINKUP;
    tulip_media_set(sc, TULIP_MEDIA_AUIBNC);
    sc->tulip_media = TULIP_MEDIA_AUIBNC;
}

static void
tulip_21040_auibnc_only_media_select(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    tulip_media_set(sc, TULIP_MEDIA_AUIBNC);
    sc->tulip_cmdmode &= ~TULIP_CMD_FULLDUPLEX;
}

static const tulip_boardsw_t tulip_21040_boardsw = {
    TULIP_21040_GENERIC,
    tulip_21040_media_probe,
    tulip_media_select,
    tulip_media_poll,
};

static const tulip_boardsw_t tulip_21040_10baset_only_boardsw = {
    TULIP_21040_GENERIC,
    tulip_21040_10baset_only_media_probe,
    tulip_21040_10baset_only_media_select,
    NULL,
};

static const tulip_boardsw_t tulip_21040_auibnc_only_boardsw = {
    TULIP_21040_GENERIC,
    tulip_21040_auibnc_only_media_probe,
    tulip_21040_auibnc_only_media_select,
    NULL,
};

static void
tulip_21041_mediainfo_init(tulip_softc_t * const sc)
{
    tulip_media_info_t * const mi = sc->tulip_mediainfo;

    TULIP_LOCK_ASSERT(sc);
#ifdef notyet
    if (sc->tulip_revinfo >= 0x20) {
	TULIP_MEDIAINFO_SIA_INIT(sc, &mi[0], 21041P2, 10BASET);
	TULIP_MEDIAINFO_SIA_INIT(sc, &mi[1], 21041P2, 10BASET_FD);
	TULIP_MEDIAINFO_SIA_INIT(sc, &mi[0], 21041P2, AUI);
	TULIP_MEDIAINFO_SIA_INIT(sc, &mi[1], 21041P2, BNC);
	return;
    }
#endif
    TULIP_MEDIAINFO_SIA_INIT(sc, &mi[0], 21041, 10BASET);
    TULIP_MEDIAINFO_SIA_INIT(sc, &mi[1], 21041, 10BASET_FD);
    TULIP_MEDIAINFO_SIA_INIT(sc, &mi[2], 21041, AUI);
    TULIP_MEDIAINFO_SIA_INIT(sc, &mi[3], 21041, BNC);
}

static void
tulip_21041_media_probe(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    sc->tulip_ifp->if_baudrate = 10000000;
    sc->tulip_cmdmode |= TULIP_CMD_CAPTREFFCT|TULIP_CMD_ENHCAPTEFFCT
	|TULIP_CMD_THRSHLD160|TULIP_CMD_BACKOFFCTR;
    sc->tulip_intrmask |= TULIP_STS_LINKPASS|TULIP_STS_LINKFAIL;
    tulip_21041_mediainfo_init(sc);
}

static void
tulip_21041_media_poll(tulip_softc_t * const sc,
    const tulip_mediapoll_event_t event)
{
    u_int32_t sia_status;

    TULIP_LOCK_ASSERT(sc);
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_events[event]++;
#endif

    if (event == TULIP_MEDIAPOLL_LINKFAIL) {
	if (sc->tulip_probe_state != TULIP_PROBE_INACTIVE
		|| !TULIP_DO_AUTOSENSE(sc))
	    return;
	sc->tulip_media = TULIP_MEDIA_UNKNOWN;
	tulip_reset(sc);	/* start probe */
	return;
    }

    /*
     * If we've been been asked to start a poll or link change interrupt
     * restart the probe (and reset the tulip to a known state).
     */
    if (event == TULIP_MEDIAPOLL_START) {
	sc->tulip_ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	sc->tulip_cmdmode &= ~(TULIP_CMD_FULLDUPLEX|TULIP_CMD_RXRUN);
#ifdef notyet
	if (sc->tulip_revinfo >= 0x20) {
	    sc->tulip_cmdmode |= TULIP_CMD_FULLDUPLEX;
	    sc->tulip_flags |= TULIP_DIDNWAY;
	}
#endif
	TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
	sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
	sc->tulip_probe_media = TULIP_MEDIA_10BASET;
	sc->tulip_probe_timeout = TULIP_21041_PROBE_10BASET_TIMEOUT;
	tulip_media_set(sc, TULIP_MEDIA_10BASET);
	tulip_timeout(sc);
	return;
    }

    if (sc->tulip_probe_state == TULIP_PROBE_INACTIVE)
	return;

    if (event == TULIP_MEDIAPOLL_TXPROBE_OK) {
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_txprobes_ok[sc->tulip_probe_media]++;
#endif
	tulip_linkup(sc, sc->tulip_probe_media);
	return;
    }

    sia_status = TULIP_CSR_READ(sc, csr_sia_status);
    TULIP_CSR_WRITE(sc, csr_sia_status, sia_status);
    if ((sia_status & TULIP_SIASTS_LINKFAIL) == 0) {
	if (sc->tulip_revinfo >= 0x20) {
	    if (sia_status & (PHYSTS_10BASET_FD << (16 - 6)))
		sc->tulip_probe_media = TULIP_MEDIA_10BASET_FD;
	}
	/*
	 * If the link has passed LinkPass, 10baseT is the
	 * proper media to use.
	 */
	tulip_linkup(sc, sc->tulip_probe_media);
	return;
    }

    /*
     * wait for up to 2.4 seconds for the link to reach pass state.
     * Only then start scanning the other media for activity.
     * choose media with receive activity over those without.
     */
    if (sc->tulip_probe_media == TULIP_MEDIA_10BASET) {
	if (event != TULIP_MEDIAPOLL_TIMER)
	    return;
	if (sc->tulip_probe_timeout > 0
		&& (sia_status & TULIP_SIASTS_OTHERRXACTIVITY) == 0) {
	    tulip_timeout(sc);
	    return;
	}
	sc->tulip_probe_timeout = TULIP_21041_PROBE_AUIBNC_TIMEOUT;
	sc->tulip_flags |= TULIP_WANTRXACT;
	if (sia_status & TULIP_SIASTS_OTHERRXACTIVITY) {
	    sc->tulip_probe_media = TULIP_MEDIA_BNC;
	} else {
	    sc->tulip_probe_media = TULIP_MEDIA_AUI;
	}
	tulip_media_set(sc, sc->tulip_probe_media);
	tulip_timeout(sc);
	return;
    }

    /*
     * If we failed, clear the txprobe active flag.
     */
    if (event == TULIP_MEDIAPOLL_TXPROBE_FAILED)
	sc->tulip_flags &= ~TULIP_TXPROBE_ACTIVE;


    if (event == TULIP_MEDIAPOLL_TIMER) {
	/*
	 * If we've received something, then that's our link!
	 */
	if (sc->tulip_flags & TULIP_RXACT) {
	    tulip_linkup(sc, sc->tulip_probe_media);
	    return;
	}
	/*
	 * if no txprobe active  
	 */
	if ((sc->tulip_flags & TULIP_TXPROBE_ACTIVE) == 0
		&& ((sc->tulip_flags & TULIP_WANTRXACT) == 0
		    || (sia_status & TULIP_SIASTS_RXACTIVITY))) {
	    sc->tulip_probe_timeout = TULIP_21041_PROBE_AUIBNC_TIMEOUT;
	    tulip_txprobe(sc);
	    tulip_timeout(sc);
	    return;
	}
	/*
	 * Take 2 passes through before deciding to not
	 * wait for receive activity.  Then take another
	 * two passes before spitting out a warning.
	 */
	if (sc->tulip_probe_timeout <= 0) {
	    if (sc->tulip_flags & TULIP_WANTRXACT) {
		sc->tulip_flags &= ~TULIP_WANTRXACT;
		sc->tulip_probe_timeout = TULIP_21041_PROBE_AUIBNC_TIMEOUT;
	    } else {
		device_printf(sc->tulip_dev,
		    "autosense failed: cable problem?\n");
		if ((sc->tulip_ifp->if_flags & IFF_UP) == 0) {
		    sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		    sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
		    return;
		}
	    }
	}
    }
    
    /*
     * Since this media failed to probe, try the other one.
     */
    sc->tulip_probe_timeout = TULIP_21041_PROBE_AUIBNC_TIMEOUT;
    if (sc->tulip_probe_media == TULIP_MEDIA_AUI) {
	sc->tulip_probe_media = TULIP_MEDIA_BNC;
    } else {
	sc->tulip_probe_media = TULIP_MEDIA_AUI;
    }
    tulip_media_set(sc, sc->tulip_probe_media);
    sc->tulip_flags &= ~TULIP_TXPROBE_ACTIVE;
    tulip_timeout(sc);
}

static const tulip_boardsw_t tulip_21041_boardsw = {
    TULIP_21041_GENERIC,
    tulip_21041_media_probe,
    tulip_media_select,
    tulip_21041_media_poll
};

static const tulip_phy_attr_t tulip_mii_phy_attrlist[] = {
    { 0x20005c00, 0,		/* 08-00-17 */
      {
	{ 0x19, 0x0040, 0x0040 },	/* 10TX */
	{ 0x19, 0x0040, 0x0000 },	/* 100TX */
      },
#if defined(TULIP_DEBUG)
      "NS DP83840",
#endif
    },
    { 0x0281F400, 0,		/* 00-A0-7D */
      {
	{ 0x12, 0x0010, 0x0000 },	/* 10T */
	{ },				/* 100TX */
	{ 0x12, 0x0010, 0x0010 },	/* 100T4 */
	{ 0x12, 0x0008, 0x0008 },	/* FULL_DUPLEX */
      },
#if defined(TULIP_DEBUG)
      "Seeq 80C240"
#endif
    },
#if 0
    { 0x0015F420, 0,	/* 00-A0-7D */
      {
	{ 0x12, 0x0010, 0x0000 },	/* 10T */
	{ },				/* 100TX */
	{ 0x12, 0x0010, 0x0010 },	/* 100T4 */
	{ 0x12, 0x0008, 0x0008 },	/* FULL_DUPLEX */
      },
#if defined(TULIP_DEBUG)
      "Broadcom BCM5000"
#endif
    },
#endif
    { 0x0281F400, 0,		/* 00-A0-BE */
      {
	{ 0x11, 0x8000, 0x0000 },	/* 10T */
	{ 0x11, 0x8000, 0x8000 },	/* 100TX */
	{ },				/* 100T4 */
	{ 0x11, 0x4000, 0x4000 },	/* FULL_DUPLEX */
      },
#if defined(TULIP_DEBUG)
      "ICS 1890"
#endif 
    },
    { 0 }
};

static tulip_media_t
tulip_mii_phy_readspecific(tulip_softc_t * const sc)
{
    const tulip_phy_attr_t *attr;
    u_int16_t data;
    u_int32_t id;
    unsigned idx = 0;
    static const tulip_media_t table[] = {
	TULIP_MEDIA_UNKNOWN,
	TULIP_MEDIA_10BASET,
	TULIP_MEDIA_100BASETX,
	TULIP_MEDIA_100BASET4,
	TULIP_MEDIA_UNKNOWN,
	TULIP_MEDIA_10BASET_FD,
	TULIP_MEDIA_100BASETX_FD,
	TULIP_MEDIA_UNKNOWN
    };

    TULIP_LOCK_ASSERT(sc);

    /*
     * Don't read phy specific registers if link is not up.
     */
    data = tulip_mii_readreg(sc, sc->tulip_phyaddr, PHYREG_STATUS);
    if ((data & (PHYSTS_LINK_UP|PHYSTS_EXTENDED_REGS)) != (PHYSTS_LINK_UP|PHYSTS_EXTENDED_REGS))
	return TULIP_MEDIA_UNKNOWN;

    id = (tulip_mii_readreg(sc, sc->tulip_phyaddr, PHYREG_IDLOW) << 16) |
	tulip_mii_readreg(sc, sc->tulip_phyaddr, PHYREG_IDHIGH);
    for (attr = tulip_mii_phy_attrlist;; attr++) {
	if (attr->attr_id == 0)
	    return TULIP_MEDIA_UNKNOWN;
	if ((id & ~0x0F) == attr->attr_id)
	    break;
    }

    if (attr->attr_modes[PHY_MODE_100TX].pm_regno) {
	const tulip_phy_modedata_t * const pm = &attr->attr_modes[PHY_MODE_100TX];
	data = tulip_mii_readreg(sc, sc->tulip_phyaddr, pm->pm_regno);
	if ((data & pm->pm_mask) == pm->pm_value)
	    idx = 2;
    }
    if (idx == 0 && attr->attr_modes[PHY_MODE_100T4].pm_regno) {
	const tulip_phy_modedata_t * const pm = &attr->attr_modes[PHY_MODE_100T4];
	data = tulip_mii_readreg(sc, sc->tulip_phyaddr, pm->pm_regno);
	if ((data & pm->pm_mask) == pm->pm_value)
	    idx = 3;
    }
    if (idx == 0 && attr->attr_modes[PHY_MODE_10T].pm_regno) {
	const tulip_phy_modedata_t * const pm = &attr->attr_modes[PHY_MODE_10T];
	data = tulip_mii_readreg(sc, sc->tulip_phyaddr, pm->pm_regno);
	if ((data & pm->pm_mask) == pm->pm_value)
	    idx = 1;
    } 
    if (idx != 0 && attr->attr_modes[PHY_MODE_FULLDUPLEX].pm_regno) {
	const tulip_phy_modedata_t * const pm = &attr->attr_modes[PHY_MODE_FULLDUPLEX];
	data = tulip_mii_readreg(sc, sc->tulip_phyaddr, pm->pm_regno);
	idx += ((data & pm->pm_mask) == pm->pm_value ? 4 : 0);
    }
    return table[idx];
}

static unsigned
tulip_mii_get_phyaddr(tulip_softc_t * const sc, unsigned offset)
{
    unsigned phyaddr;

    TULIP_LOCK_ASSERT(sc);
    for (phyaddr = 1; phyaddr < 32; phyaddr++) {
	unsigned status = tulip_mii_readreg(sc, phyaddr, PHYREG_STATUS);
	if (status == 0 || status == 0xFFFF || status < PHYSTS_10BASET)
	    continue;
	if (offset == 0)
	    return phyaddr;
	offset--;
    }
    if (offset == 0) {
	unsigned status = tulip_mii_readreg(sc, 0, PHYREG_STATUS);
	if (status == 0 || status == 0xFFFF || status < PHYSTS_10BASET)
	    return TULIP_MII_NOPHY;
	return 0;
    }
    return TULIP_MII_NOPHY;
}

static int
tulip_mii_map_abilities(tulip_softc_t * const sc, unsigned abilities)
{
    TULIP_LOCK_ASSERT(sc);
    sc->tulip_abilities = abilities;
    if (abilities & PHYSTS_100BASETX_FD) {
	sc->tulip_probe_media = TULIP_MEDIA_100BASETX_FD;
    } else if (abilities & PHYSTS_100BASET4) {
	sc->tulip_probe_media = TULIP_MEDIA_100BASET4;
    } else if (abilities & PHYSTS_100BASETX) {
	sc->tulip_probe_media = TULIP_MEDIA_100BASETX;
    } else if (abilities & PHYSTS_10BASET_FD) {
	sc->tulip_probe_media = TULIP_MEDIA_10BASET_FD;
    } else if (abilities & PHYSTS_10BASET) {
	sc->tulip_probe_media = TULIP_MEDIA_10BASET;
    } else {
	sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
	return 0;
    }
    sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
    return 1;
}

static void
tulip_mii_autonegotiate(tulip_softc_t * const sc, const unsigned phyaddr)
{
    struct ifnet *ifp = sc->tulip_ifp;

    TULIP_LOCK_ASSERT(sc);
    switch (sc->tulip_probe_state) {
        case TULIP_PROBE_MEDIATEST:
        case TULIP_PROBE_INACTIVE: {
	    sc->tulip_flags |= TULIP_DIDNWAY;
	    tulip_mii_writereg(sc, phyaddr, PHYREG_CONTROL, PHYCTL_RESET);
	    sc->tulip_probe_timeout = 3000;
	    sc->tulip_intrmask |= TULIP_STS_ABNRMLINTR|TULIP_STS_NORMALINTR;
	    sc->tulip_probe_state = TULIP_PROBE_PHYRESET;
	}
        /* FALLTHROUGH */
        case TULIP_PROBE_PHYRESET: {
	    u_int32_t status;
	    u_int32_t data = tulip_mii_readreg(sc, phyaddr, PHYREG_CONTROL);
	    if (data & PHYCTL_RESET) {
		if (sc->tulip_probe_timeout > 0) {
		    tulip_timeout(sc);
		    return;
		}
		printf("%s(phy%d): error: reset of PHY never completed!\n",
			   ifp->if_xname, phyaddr);
		sc->tulip_flags &= ~TULIP_TXPROBE_ACTIVE;
		sc->tulip_probe_state = TULIP_PROBE_FAILED;
		sc->tulip_ifp->if_flags &= ~IFF_UP;
		sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		return;
	    }
	    status = tulip_mii_readreg(sc, phyaddr, PHYREG_STATUS);
	    if ((status & PHYSTS_CAN_AUTONEG) == 0) {
#if defined(TULIP_DEBUG)
		loudprintf("%s(phy%d): autonegotiation disabled\n",
			   ifp->if_xname, phyaddr);
#endif
		sc->tulip_flags &= ~TULIP_DIDNWAY;
		sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
		return;
	    }
	    if (tulip_mii_readreg(sc, phyaddr, PHYREG_AUTONEG_ADVERTISEMENT) != ((status >> 6) | 0x01))
		tulip_mii_writereg(sc, phyaddr, PHYREG_AUTONEG_ADVERTISEMENT, (status >> 6) | 0x01);
	    tulip_mii_writereg(sc, phyaddr, PHYREG_CONTROL, data|PHYCTL_AUTONEG_RESTART|PHYCTL_AUTONEG_ENABLE);
	    data = tulip_mii_readreg(sc, phyaddr, PHYREG_CONTROL);
#if defined(TULIP_DEBUG)
	    if ((data & PHYCTL_AUTONEG_ENABLE) == 0)
		loudprintf("%s(phy%d): oops: enable autonegotiation failed: 0x%04x\n",
			   ifp->if_xname, phyaddr, data);
	    else
		loudprintf("%s(phy%d): autonegotiation restarted: 0x%04x\n",
			   ifp->if_xname, phyaddr, data);
	    sc->tulip_dbg.dbg_nway_starts++;
#endif
	    sc->tulip_probe_state = TULIP_PROBE_PHYAUTONEG;
	    sc->tulip_probe_timeout = 3000;
	}
        /* FALLTHROUGH */
        case TULIP_PROBE_PHYAUTONEG: {
	    u_int32_t status = tulip_mii_readreg(sc, phyaddr, PHYREG_STATUS);
	    u_int32_t data;
	    if ((status & PHYSTS_AUTONEG_DONE) == 0) {
		if (sc->tulip_probe_timeout > 0) {
		    tulip_timeout(sc);
		    return;
		}
#if defined(TULIP_DEBUG)
		loudprintf("%s(phy%d): autonegotiation timeout: sts=0x%04x, ctl=0x%04x\n",
			   ifp->if_xname, phyaddr, status,
			   tulip_mii_readreg(sc, phyaddr, PHYREG_CONTROL));
#endif
		sc->tulip_flags &= ~TULIP_DIDNWAY;
		sc->tulip_probe_state = TULIP_PROBE_MEDIATEST;
		return;
	    }
	    data = tulip_mii_readreg(sc, phyaddr, PHYREG_AUTONEG_ABILITIES);
#if defined(TULIP_DEBUG)
	    loudprintf("%s(phy%d): autonegotiation complete: 0x%04x\n",
		       ifp->if_xname, phyaddr, data);
#endif
	    data = (data << 6) & status;
	    if (!tulip_mii_map_abilities(sc, data))
		sc->tulip_flags &= ~TULIP_DIDNWAY;
	    return;
	}
	default: {
#if defined(DIAGNOSTIC)
	    panic("tulip_media_poll: botch at line %d\n", __LINE__);
#endif
	    break;
	}
    }
#if defined(TULIP_DEBUG)
    loudprintf("%s(phy%d): autonegotiation failure: state = %d\n",
	       ifp->if_xname, phyaddr, sc->tulip_probe_state);
	    sc->tulip_dbg.dbg_nway_failures++;
#endif
}

static void
tulip_2114x_media_preset(tulip_softc_t * const sc)
{
    const tulip_media_info_t *mi = NULL;
    tulip_media_t media = sc->tulip_media;

    TULIP_LOCK_ASSERT(sc);
    if (sc->tulip_probe_state == TULIP_PROBE_INACTIVE)
	media = sc->tulip_media;
    else
	media = sc->tulip_probe_media;
    
    sc->tulip_cmdmode &= ~TULIP_CMD_PORTSELECT;
    sc->tulip_flags &= ~TULIP_SQETEST;
    if (media != TULIP_MEDIA_UNKNOWN && media != TULIP_MEDIA_MAX) {
#if defined(TULIP_DEBUG)
	if (media < TULIP_MEDIA_MAX && sc->tulip_mediums[media] != NULL) {
#endif
	    mi = sc->tulip_mediums[media];
	    if (mi->mi_type == TULIP_MEDIAINFO_MII) {
		sc->tulip_cmdmode |= TULIP_CMD_PORTSELECT;
	    } else if (mi->mi_type == TULIP_MEDIAINFO_GPR
		       || mi->mi_type == TULIP_MEDIAINFO_SYM) {
		sc->tulip_cmdmode &= ~TULIP_GPR_CMDBITS;
		sc->tulip_cmdmode |= mi->mi_cmdmode;
	    } else if (mi->mi_type == TULIP_MEDIAINFO_SIA) {
		TULIP_CSR_WRITE(sc, csr_sia_connectivity, TULIP_SIACONN_RESET);
	    }
#if defined(TULIP_DEBUG)
	} else {
	    device_printf(sc->tulip_dev, "preset: bad media %d!\n", media);
	}
#endif
    }
    switch (media) {
	case TULIP_MEDIA_BNC:
	case TULIP_MEDIA_AUI:
	case TULIP_MEDIA_10BASET: {
	    sc->tulip_cmdmode &= ~TULIP_CMD_FULLDUPLEX;
	    sc->tulip_cmdmode |= TULIP_CMD_TXTHRSHLDCTL;
	    sc->tulip_ifp->if_baudrate = 10000000;
	    sc->tulip_flags |= TULIP_SQETEST;
	    break;
	}
	case TULIP_MEDIA_10BASET_FD: {
	    sc->tulip_cmdmode |= TULIP_CMD_FULLDUPLEX|TULIP_CMD_TXTHRSHLDCTL;
	    sc->tulip_ifp->if_baudrate = 10000000;
	    break;
	}
	case TULIP_MEDIA_100BASEFX:
	case TULIP_MEDIA_100BASET4:
	case TULIP_MEDIA_100BASETX: {
	    sc->tulip_cmdmode &= ~(TULIP_CMD_FULLDUPLEX|TULIP_CMD_TXTHRSHLDCTL);
	    sc->tulip_cmdmode |= TULIP_CMD_PORTSELECT;
	    sc->tulip_ifp->if_baudrate = 100000000;
	    break;
	}
	case TULIP_MEDIA_100BASEFX_FD:
	case TULIP_MEDIA_100BASETX_FD: {
	    sc->tulip_cmdmode |= TULIP_CMD_FULLDUPLEX|TULIP_CMD_PORTSELECT;
	    sc->tulip_cmdmode &= ~TULIP_CMD_TXTHRSHLDCTL;
	    sc->tulip_ifp->if_baudrate = 100000000;
	    break;
	}
	default: {
	    break;
	}
    }
    TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
}

/*
 ********************************************************************
 *  Start of 21140/21140A support which does not use the MII interface 
 */

static void
tulip_null_media_poll(tulip_softc_t * const sc, tulip_mediapoll_event_t event)
{
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_events[event]++;
#endif
#if defined(DIAGNOSTIC)
    device_printf(sc->tulip_dev, "botch(media_poll) at line %d\n", __LINE__);
#endif
}

static inline void
tulip_21140_mediainit(tulip_softc_t * const sc, tulip_media_info_t * const mip,
    tulip_media_t const media, unsigned gpdata, unsigned cmdmode)
{
    TULIP_LOCK_ASSERT(sc);
    sc->tulip_mediums[media] = mip;
    mip->mi_type = TULIP_MEDIAINFO_GPR;
    mip->mi_cmdmode = cmdmode;
    mip->mi_gpdata = gpdata;
}

static void
tulip_21140_evalboard_media_probe(tulip_softc_t * const sc)
{
    tulip_media_info_t *mip = sc->tulip_mediainfo;

    TULIP_LOCK_ASSERT(sc);
    sc->tulip_gpinit = TULIP_GP_EB_PINS;
    sc->tulip_gpdata = TULIP_GP_EB_INIT;
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_EB_PINS);
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_EB_INIT);
    TULIP_CSR_WRITE(sc, csr_command,
	TULIP_CSR_READ(sc, csr_command) | TULIP_CMD_PORTSELECT |
	TULIP_CMD_PCSFUNCTION | TULIP_CMD_SCRAMBLER | TULIP_CMD_MUSTBEONE);
    TULIP_CSR_WRITE(sc, csr_command,
	TULIP_CSR_READ(sc, csr_command) & ~TULIP_CMD_TXTHRSHLDCTL);
    DELAY(1000000);
    if ((TULIP_CSR_READ(sc, csr_gp) & TULIP_GP_EB_OK100) != 0) {
	sc->tulip_media = TULIP_MEDIA_10BASET;
    } else {
	sc->tulip_media = TULIP_MEDIA_100BASETX;
    }
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET,
			  TULIP_GP_EB_INIT,
			  TULIP_CMD_TXTHRSHLDCTL);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET_FD,
			  TULIP_GP_EB_INIT,
			  TULIP_CMD_TXTHRSHLDCTL|TULIP_CMD_FULLDUPLEX);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX,
			  TULIP_GP_EB_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX_FD,
			  TULIP_GP_EB_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER|TULIP_CMD_FULLDUPLEX);
}

static const tulip_boardsw_t tulip_21140_eb_boardsw = {
    TULIP_21140_DEC_EB,
    tulip_21140_evalboard_media_probe,
    tulip_media_select,
    tulip_null_media_poll,
    tulip_2114x_media_preset,
};

static void
tulip_21140_accton_media_probe(tulip_softc_t * const sc)
{
    tulip_media_info_t *mip = sc->tulip_mediainfo;
    unsigned gpdata;

    TULIP_LOCK_ASSERT(sc);
    sc->tulip_gpinit = TULIP_GP_EB_PINS;
    sc->tulip_gpdata = TULIP_GP_EB_INIT;
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_EB_PINS);
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_EB_INIT);
    TULIP_CSR_WRITE(sc, csr_command,
	TULIP_CSR_READ(sc, csr_command) | TULIP_CMD_PORTSELECT |
	TULIP_CMD_PCSFUNCTION | TULIP_CMD_SCRAMBLER | TULIP_CMD_MUSTBEONE);
    TULIP_CSR_WRITE(sc, csr_command,
	TULIP_CSR_READ(sc, csr_command) & ~TULIP_CMD_TXTHRSHLDCTL);
    DELAY(1000000);
    gpdata = TULIP_CSR_READ(sc, csr_gp);
    if ((gpdata & TULIP_GP_EN1207_UTP_INIT) == 0) {
	sc->tulip_media = TULIP_MEDIA_10BASET;
    } else {
	if ((gpdata & TULIP_GP_EN1207_BNC_INIT) == 0) {
		sc->tulip_media = TULIP_MEDIA_BNC;
        } else {
		sc->tulip_media = TULIP_MEDIA_100BASETX;
        }
    }
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_BNC,
			  TULIP_GP_EN1207_BNC_INIT,
			  TULIP_CMD_TXTHRSHLDCTL);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET,
			  TULIP_GP_EN1207_UTP_INIT,
			  TULIP_CMD_TXTHRSHLDCTL);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET_FD,
			  TULIP_GP_EN1207_UTP_INIT,
			  TULIP_CMD_TXTHRSHLDCTL|TULIP_CMD_FULLDUPLEX);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX,
			  TULIP_GP_EN1207_100_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX_FD,
			  TULIP_GP_EN1207_100_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER|TULIP_CMD_FULLDUPLEX);
}

static const tulip_boardsw_t tulip_21140_accton_boardsw = {
    TULIP_21140_EN1207,
    tulip_21140_accton_media_probe,
    tulip_media_select,
    tulip_null_media_poll,
    tulip_2114x_media_preset,
};

static void
tulip_21140_smc9332_media_probe(tulip_softc_t * const sc)
{
    tulip_media_info_t *mip = sc->tulip_mediainfo;
    int idx, cnt = 0;

    TULIP_LOCK_ASSERT(sc);
    TULIP_CSR_WRITE(sc, csr_command, TULIP_CMD_PORTSELECT|TULIP_CMD_MUSTBEONE);
    TULIP_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    DELAY(10);	/* Wait 10 microseconds (actually 50 PCI cycles but at 
		   33MHz that comes to two microseconds but wait a
		   bit longer anyways) */
    TULIP_CSR_WRITE(sc, csr_command, TULIP_CMD_PORTSELECT |
	TULIP_CMD_PCSFUNCTION | TULIP_CMD_SCRAMBLER | TULIP_CMD_MUSTBEONE);
    sc->tulip_gpinit = TULIP_GP_SMC_9332_PINS;
    sc->tulip_gpdata = TULIP_GP_SMC_9332_INIT;
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_SMC_9332_PINS|TULIP_GP_PINSET);
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_SMC_9332_INIT);
    DELAY(200000);
    for (idx = 1000; idx > 0; idx--) {
	u_int32_t csr = TULIP_CSR_READ(sc, csr_gp);
	if ((csr & (TULIP_GP_SMC_9332_OK10|TULIP_GP_SMC_9332_OK100)) == (TULIP_GP_SMC_9332_OK10|TULIP_GP_SMC_9332_OK100)) {
	    if (++cnt > 100)
		break;
	} else if ((csr & TULIP_GP_SMC_9332_OK10) == 0) {
	    break;
	} else {
	    cnt = 0;
	}
	DELAY(1000);
    }
    sc->tulip_media = cnt > 100 ? TULIP_MEDIA_100BASETX : TULIP_MEDIA_10BASET;
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX,
			  TULIP_GP_SMC_9332_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX_FD,
			  TULIP_GP_SMC_9332_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER|TULIP_CMD_FULLDUPLEX);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET,
			  TULIP_GP_SMC_9332_INIT,
			  TULIP_CMD_TXTHRSHLDCTL);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET_FD,
			  TULIP_GP_SMC_9332_INIT,
			  TULIP_CMD_TXTHRSHLDCTL|TULIP_CMD_FULLDUPLEX);
}
 
static const tulip_boardsw_t tulip_21140_smc9332_boardsw = {
    TULIP_21140_SMC_9332,
    tulip_21140_smc9332_media_probe,
    tulip_media_select,
    tulip_null_media_poll,
    tulip_2114x_media_preset,
};

static void
tulip_21140_cogent_em100_media_probe(tulip_softc_t * const sc)
{
    tulip_media_info_t *mip = sc->tulip_mediainfo;
    u_int32_t cmdmode = TULIP_CSR_READ(sc, csr_command);

    TULIP_LOCK_ASSERT(sc);
    sc->tulip_gpinit = TULIP_GP_EM100_PINS;
    sc->tulip_gpdata = TULIP_GP_EM100_INIT;
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_EM100_PINS);
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_EM100_INIT);

    cmdmode = TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION|TULIP_CMD_MUSTBEONE;
    cmdmode &= ~(TULIP_CMD_TXTHRSHLDCTL|TULIP_CMD_SCRAMBLER);
    if (sc->tulip_rombuf[32] == TULIP_COGENT_EM100FX_ID) {
	TULIP_CSR_WRITE(sc, csr_command, cmdmode);
	sc->tulip_media = TULIP_MEDIA_100BASEFX;

	tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASEFX,
			  TULIP_GP_EM100_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION);
	tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASEFX_FD,
			  TULIP_GP_EM100_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_FULLDUPLEX);
    } else {
	TULIP_CSR_WRITE(sc, csr_command, cmdmode|TULIP_CMD_SCRAMBLER);
	sc->tulip_media = TULIP_MEDIA_100BASETX;
	tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX,
			  TULIP_GP_EM100_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER);
	tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX_FD,
			  TULIP_GP_EM100_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER|TULIP_CMD_FULLDUPLEX);
    }
}

static const tulip_boardsw_t tulip_21140_cogent_em100_boardsw = {
    TULIP_21140_COGENT_EM100,
    tulip_21140_cogent_em100_media_probe,
    tulip_media_select,
    tulip_null_media_poll,
    tulip_2114x_media_preset
};

static void
tulip_21140_znyx_zx34x_media_probe(tulip_softc_t * const sc)
{
    tulip_media_info_t *mip = sc->tulip_mediainfo;
    int cnt10 = 0, cnt100 = 0, idx;

    TULIP_LOCK_ASSERT(sc);
    sc->tulip_gpinit = TULIP_GP_ZX34X_PINS;
    sc->tulip_gpdata = TULIP_GP_ZX34X_INIT;
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_ZX34X_PINS);
    TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_ZX34X_INIT);
    TULIP_CSR_WRITE(sc, csr_command,
	TULIP_CSR_READ(sc, csr_command) | TULIP_CMD_PORTSELECT |
	TULIP_CMD_PCSFUNCTION | TULIP_CMD_SCRAMBLER | TULIP_CMD_MUSTBEONE);
    TULIP_CSR_WRITE(sc, csr_command,
	TULIP_CSR_READ(sc, csr_command) & ~TULIP_CMD_TXTHRSHLDCTL);

    DELAY(200000);
    for (idx = 1000; idx > 0; idx--) {
	u_int32_t csr = TULIP_CSR_READ(sc, csr_gp);
	if ((csr & (TULIP_GP_ZX34X_LNKFAIL|TULIP_GP_ZX34X_SYMDET|TULIP_GP_ZX34X_SIGDET)) == (TULIP_GP_ZX34X_LNKFAIL|TULIP_GP_ZX34X_SYMDET|TULIP_GP_ZX34X_SIGDET)) {
	    if (++cnt100 > 100)
		break;
	} else if ((csr & TULIP_GP_ZX34X_LNKFAIL) == 0) {
	    if (++cnt10 > 100)
		break;
	} else {
	    cnt10 = 0;
	    cnt100 = 0;
	}
	DELAY(1000);
    }
    sc->tulip_media = cnt100 > 100 ? TULIP_MEDIA_100BASETX : TULIP_MEDIA_10BASET;
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET,
			  TULIP_GP_ZX34X_INIT,
			  TULIP_CMD_TXTHRSHLDCTL);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_10BASET_FD,
			  TULIP_GP_ZX34X_INIT,
			  TULIP_CMD_TXTHRSHLDCTL|TULIP_CMD_FULLDUPLEX);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX,
			  TULIP_GP_ZX34X_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER);
    tulip_21140_mediainit(sc, mip++, TULIP_MEDIA_100BASETX_FD,
			  TULIP_GP_ZX34X_INIT,
			  TULIP_CMD_PORTSELECT|TULIP_CMD_PCSFUNCTION
			      |TULIP_CMD_SCRAMBLER|TULIP_CMD_FULLDUPLEX);
}

static const tulip_boardsw_t tulip_21140_znyx_zx34x_boardsw = {
    TULIP_21140_ZNYX_ZX34X,
    tulip_21140_znyx_zx34x_media_probe,
    tulip_media_select,
    tulip_null_media_poll,
    tulip_2114x_media_preset,
};

static void
tulip_2114x_media_probe(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    sc->tulip_cmdmode |= TULIP_CMD_MUSTBEONE
	|TULIP_CMD_BACKOFFCTR|TULIP_CMD_THRSHLD72;
}

static const tulip_boardsw_t tulip_2114x_isv_boardsw = {
    TULIP_21140_ISV,
    tulip_2114x_media_probe,
    tulip_media_select,
    tulip_media_poll,
    tulip_2114x_media_preset,
};

/*
 * ******** END of chip-specific handlers. ***********
 */

/*
 * Code the read the SROM and MII bit streams (I2C)
 */
#define EMIT    do { TULIP_CSR_WRITE(sc, csr_srom_mii, csr); DELAY(1); } while (0)

static void
tulip_srom_idle(tulip_softc_t * const sc)
{
    unsigned bit, csr;
    
    csr  = SROMSEL ; EMIT;
    csr  = SROMSEL | SROMRD; EMIT;  
    csr ^= SROMCS; EMIT;
    csr ^= SROMCLKON; EMIT;

    /*
     * Write 25 cycles of 0 which will force the SROM to be idle.
     */
    for (bit = 3 + SROM_BITWIDTH + 16; bit > 0; bit--) {
        csr ^= SROMCLKOFF; EMIT;    /* clock low; data not valid */
        csr ^= SROMCLKON; EMIT;     /* clock high; data valid */
    }
    csr ^= SROMCLKOFF; EMIT;
    csr ^= SROMCS; EMIT;
    csr  = 0; EMIT;
}
     
static void
tulip_srom_read(tulip_softc_t * const sc)
{   
    unsigned idx; 
    const unsigned bitwidth = SROM_BITWIDTH;
    const unsigned cmdmask = (SROMCMD_RD << bitwidth);
    const unsigned msb = 1 << (bitwidth + 3 - 1);
    unsigned lastidx = (1 << bitwidth) - 1;

    tulip_srom_idle(sc);

    for (idx = 0; idx <= lastidx; idx++) {
        unsigned lastbit, data, bits, bit, csr;
	csr  = SROMSEL ;	        EMIT;
        csr  = SROMSEL | SROMRD;        EMIT;
        csr ^= SROMCSON;                EMIT;
        csr ^=            SROMCLKON;    EMIT;
    
        lastbit = 0;
        for (bits = idx|cmdmask, bit = bitwidth + 3; bit > 0; bit--, bits <<= 1) {
            const unsigned thisbit = bits & msb;
            csr ^= SROMCLKOFF; EMIT;    /* clock low; data not valid */
            if (thisbit != lastbit) {
                csr ^= SROMDOUT; EMIT;  /* clock low; invert data */
            } else {
		EMIT;
	    }
            csr ^= SROMCLKON; EMIT;     /* clock high; data valid */
            lastbit = thisbit;
        }
        csr ^= SROMCLKOFF; EMIT;

        for (data = 0, bits = 0; bits < 16; bits++) {
            data <<= 1;
            csr ^= SROMCLKON; EMIT;     /* clock high; data valid */ 
            data |= TULIP_CSR_READ(sc, csr_srom_mii) & SROMDIN ? 1 : 0;
            csr ^= SROMCLKOFF; EMIT;    /* clock low; data not valid */
        }
	sc->tulip_rombuf[idx*2] = data & 0xFF;
	sc->tulip_rombuf[idx*2+1] = data >> 8;
	csr  = SROMSEL | SROMRD; EMIT;
	csr  = 0; EMIT;
    }
    tulip_srom_idle(sc);
}

#define MII_EMIT    do { TULIP_CSR_WRITE(sc, csr_srom_mii, csr); DELAY(1); } while (0)

static void
tulip_mii_writebits(tulip_softc_t * const sc, unsigned data, unsigned bits)
{
    unsigned msb = 1 << (bits - 1);
    unsigned csr = TULIP_CSR_READ(sc, csr_srom_mii) & (MII_RD|MII_DOUT|MII_CLK);
    unsigned lastbit = (csr & MII_DOUT) ? msb : 0;

    TULIP_LOCK_ASSERT(sc);
    csr |= MII_WR; MII_EMIT;  		/* clock low; assert write */

    for (; bits > 0; bits--, data <<= 1) {
	const unsigned thisbit = data & msb;
	if (thisbit != lastbit) {
	    csr ^= MII_DOUT; MII_EMIT;  /* clock low; invert data */
	}
	csr ^= MII_CLKON; MII_EMIT;     /* clock high; data valid */
	lastbit = thisbit;
	csr ^= MII_CLKOFF; MII_EMIT;    /* clock low; data not valid */
    }
}

static void
tulip_mii_turnaround(tulip_softc_t * const sc, unsigned cmd)
{
    unsigned csr = TULIP_CSR_READ(sc, csr_srom_mii) & (MII_RD|MII_DOUT|MII_CLK);

    TULIP_LOCK_ASSERT(sc);
    if (cmd == MII_WRCMD) {
	csr |= MII_DOUT; MII_EMIT;	/* clock low; change data */
	csr ^= MII_CLKON; MII_EMIT;	/* clock high; data valid */
	csr ^= MII_CLKOFF; MII_EMIT;	/* clock low; data not valid */
	csr ^= MII_DOUT; MII_EMIT;	/* clock low; change data */
    } else {
	csr |= MII_RD; MII_EMIT;	/* clock low; switch to read */
    }
    csr ^= MII_CLKON; MII_EMIT;		/* clock high; data valid */
    csr ^= MII_CLKOFF; MII_EMIT;	/* clock low; data not valid */
}

static unsigned
tulip_mii_readbits(tulip_softc_t * const sc)
{
    unsigned data;
    unsigned csr = TULIP_CSR_READ(sc, csr_srom_mii) & (MII_RD|MII_DOUT|MII_CLK);
    int idx;

    TULIP_LOCK_ASSERT(sc);
    for (idx = 0, data = 0; idx < 16; idx++) {
	data <<= 1;	/* this is NOOP on the first pass through */
	csr ^= MII_CLKON; MII_EMIT;	/* clock high; data valid */
	if (TULIP_CSR_READ(sc, csr_srom_mii) & MII_DIN)
	    data |= 1;
	csr ^= MII_CLKOFF; MII_EMIT;	/* clock low; data not valid */
    }
    csr ^= MII_RD; MII_EMIT;		/* clock low; turn off read */

    return data;
}

static unsigned
tulip_mii_readreg(tulip_softc_t * const sc, unsigned devaddr, unsigned regno)
{
    unsigned csr = TULIP_CSR_READ(sc, csr_srom_mii) & (MII_RD|MII_DOUT|MII_CLK);
    unsigned data;

    TULIP_LOCK_ASSERT(sc);
    csr &= ~(MII_RD|MII_CLK); MII_EMIT;
    tulip_mii_writebits(sc, MII_PREAMBLE, 32);
    tulip_mii_writebits(sc, MII_RDCMD, 8);
    tulip_mii_writebits(sc, devaddr, 5);
    tulip_mii_writebits(sc, regno, 5);
    tulip_mii_turnaround(sc, MII_RDCMD);

    data = tulip_mii_readbits(sc);
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_phyregs[regno][0] = data;
    sc->tulip_dbg.dbg_phyregs[regno][1]++;
#endif
    return data;
}

static void
tulip_mii_writereg(tulip_softc_t * const sc, unsigned devaddr, unsigned regno,
    unsigned data)
{
    unsigned csr = TULIP_CSR_READ(sc, csr_srom_mii) & (MII_RD|MII_DOUT|MII_CLK);

    TULIP_LOCK_ASSERT(sc);
    csr &= ~(MII_RD|MII_CLK); MII_EMIT;
    tulip_mii_writebits(sc, MII_PREAMBLE, 32);
    tulip_mii_writebits(sc, MII_WRCMD, 8);
    tulip_mii_writebits(sc, devaddr, 5);
    tulip_mii_writebits(sc, regno, 5);
    tulip_mii_turnaround(sc, MII_WRCMD);
    tulip_mii_writebits(sc, data, 16);
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_phyregs[regno][2] = data;
    sc->tulip_dbg.dbg_phyregs[regno][3]++;
#endif
}

#define	tulip_mchash(mca)	(ether_crc32_le(mca, 6) & 0x1FF)
#define	tulip_srom_crcok(databuf)	( \
    ((ether_crc32_le(databuf, 126) & 0xFFFFU) ^ 0xFFFFU) == \
     ((databuf)[126] | ((databuf)[127] << 8)))

static void
tulip_identify_dec_nic(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "DEC ");
#define D0	4
    if (sc->tulip_chipid <= TULIP_21040)
	return;
    if (bcmp(sc->tulip_rombuf + 29, "DE500", 5) == 0
	|| bcmp(sc->tulip_rombuf + 29, "DE450", 5) == 0) {
	bcopy(sc->tulip_rombuf + 29, &sc->tulip_boardid[D0], 8);
	sc->tulip_boardid[D0+8] = ' ';
    }
#undef D0
}

static void
tulip_identify_znyx_nic(tulip_softc_t * const sc)
{
    unsigned id = 0;

    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "ZNYX ZX3XX ");
    if (sc->tulip_chipid == TULIP_21140 || sc->tulip_chipid == TULIP_21140A) {
	unsigned znyx_ptr;
	sc->tulip_boardid[8] = '4';
	znyx_ptr = sc->tulip_rombuf[124] + 256 * sc->tulip_rombuf[125];
	if (znyx_ptr < 26 || znyx_ptr > 116) {
	    sc->tulip_boardsw = &tulip_21140_znyx_zx34x_boardsw;
	    return;
	}
	/* ZX344 = 0010 .. 0013FF
	 */
	if (sc->tulip_rombuf[znyx_ptr] == 0x4A
		&& sc->tulip_rombuf[znyx_ptr + 1] == 0x52
		&& sc->tulip_rombuf[znyx_ptr + 2] == 0x01) {
	    id = sc->tulip_rombuf[znyx_ptr + 5] + 256 * sc->tulip_rombuf[znyx_ptr + 4];
	    if ((id >> 8) == (TULIP_ZNYX_ID_ZX342 >> 8)) {
		sc->tulip_boardid[9] = '2';
		if (id == TULIP_ZNYX_ID_ZX342B) {
		    sc->tulip_boardid[10] = 'B';
		    sc->tulip_boardid[11] = ' ';
		}
		sc->tulip_boardsw = &tulip_21140_znyx_zx34x_boardsw;
	    } else if (id == TULIP_ZNYX_ID_ZX344) {
		sc->tulip_boardid[10] = '4';
		sc->tulip_boardsw = &tulip_21140_znyx_zx34x_boardsw;
	    } else if (id == TULIP_ZNYX_ID_ZX345) {
		sc->tulip_boardid[9] = (sc->tulip_rombuf[19] > 1) ? '8' : '5';
	    } else if (id == TULIP_ZNYX_ID_ZX346) {
		sc->tulip_boardid[9] = '6';
	    } else if (id == TULIP_ZNYX_ID_ZX351) {
		sc->tulip_boardid[8] = '5';
		sc->tulip_boardid[9] = '1';
	    }
	}
	if (id == 0) {
	    /*
	     * Assume it's a ZX342...
	     */
	    sc->tulip_boardsw = &tulip_21140_znyx_zx34x_boardsw;
	}
	return;
    }
    sc->tulip_boardid[8] = '1';
    if (sc->tulip_chipid == TULIP_21041) {
	sc->tulip_boardid[10] = '1';
	return;
    }
    if (sc->tulip_rombuf[32] == 0x4A && sc->tulip_rombuf[33] == 0x52) {
	id = sc->tulip_rombuf[37] + 256 * sc->tulip_rombuf[36];
	if (id == TULIP_ZNYX_ID_ZX312T) {
	    sc->tulip_boardid[9] = '2';
	    sc->tulip_boardid[10] = 'T';
	    sc->tulip_boardid[11] = ' ';
	    sc->tulip_boardsw = &tulip_21040_10baset_only_boardsw;
	} else if (id == TULIP_ZNYX_ID_ZX314_INTA) {
	    sc->tulip_boardid[9] = '4';
	    sc->tulip_boardsw = &tulip_21040_10baset_only_boardsw;
	    sc->tulip_features |= TULIP_HAVE_SHAREDINTR|TULIP_HAVE_BASEROM;
	} else if (id == TULIP_ZNYX_ID_ZX314) {
	    sc->tulip_boardid[9] = '4';
	    sc->tulip_boardsw = &tulip_21040_10baset_only_boardsw;
	    sc->tulip_features |= TULIP_HAVE_BASEROM;
	} else if (id == TULIP_ZNYX_ID_ZX315_INTA) {
	    sc->tulip_boardid[9] = '5';
	    sc->tulip_features |= TULIP_HAVE_SHAREDINTR|TULIP_HAVE_BASEROM;
	} else if (id == TULIP_ZNYX_ID_ZX315) {
	    sc->tulip_boardid[9] = '5';
	    sc->tulip_features |= TULIP_HAVE_BASEROM;
	} else {
	    id = 0;
	}
    }		    
    if (id == 0) {
	if ((sc->tulip_enaddr[3] & ~3) == 0xF0 && (sc->tulip_enaddr[5] & 2) == 0) {
	    sc->tulip_boardid[9] = '4';
	    sc->tulip_boardsw = &tulip_21040_10baset_only_boardsw;
	    sc->tulip_features |= TULIP_HAVE_SHAREDINTR|TULIP_HAVE_BASEROM;
	} else if ((sc->tulip_enaddr[3] & ~3) == 0xF4 && (sc->tulip_enaddr[5] & 1) == 0) {
	    sc->tulip_boardid[9] = '5';
	    sc->tulip_boardsw = &tulip_21040_boardsw;
	    sc->tulip_features |= TULIP_HAVE_SHAREDINTR|TULIP_HAVE_BASEROM;
	} else if ((sc->tulip_enaddr[3] & ~3) == 0xEC) {
	    sc->tulip_boardid[9] = '2';
	    sc->tulip_boardsw = &tulip_21040_boardsw;
	}
    }
}

static void
tulip_identify_smc_nic(tulip_softc_t * const sc)
{
    u_int32_t id1, id2, ei;
    int auibnc = 0, utp = 0;
    char *cp;

    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "SMC ");
    if (sc->tulip_chipid == TULIP_21041)
	return;
    if (sc->tulip_chipid != TULIP_21040) {
	if (sc->tulip_boardsw != &tulip_2114x_isv_boardsw) {
	    strcpy(&sc->tulip_boardid[4], "9332DST ");
	    sc->tulip_boardsw = &tulip_21140_smc9332_boardsw;
	} else if (sc->tulip_features & (TULIP_HAVE_BASEROM|TULIP_HAVE_SLAVEDROM)) {
	    strcpy(&sc->tulip_boardid[4], "9334BDT ");
	} else {
	    strcpy(&sc->tulip_boardid[4], "9332BDT ");
	}
	return;
    }
    id1 = sc->tulip_rombuf[0x60] | (sc->tulip_rombuf[0x61] << 8);
    id2 = sc->tulip_rombuf[0x62] | (sc->tulip_rombuf[0x63] << 8);
    ei  = sc->tulip_rombuf[0x66] | (sc->tulip_rombuf[0x67] << 8);

    strcpy(&sc->tulip_boardid[4], "8432");
    cp = &sc->tulip_boardid[8];
    if ((id1 & 1) == 0)
	*cp++ = 'B', auibnc = 1;
    if ((id1 & 0xFF) > 0x32)
	*cp++ = 'T', utp = 1;
    if ((id1 & 0x4000) == 0)
	*cp++ = 'A', auibnc = 1;
    if (id2 == 0x15) {
	sc->tulip_boardid[7] = '4';
	*cp++ = '-';
	*cp++ = 'C';
	*cp++ = 'H';
	*cp++ = (ei ? '2' : '1');
    }
    *cp++ = ' ';
    *cp = '\0';
    if (utp && !auibnc)
	sc->tulip_boardsw = &tulip_21040_10baset_only_boardsw;
    else if (!utp && auibnc)
	sc->tulip_boardsw = &tulip_21040_auibnc_only_boardsw;
}

static void
tulip_identify_cogent_nic(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "Cogent ");
    if (sc->tulip_chipid == TULIP_21140 || sc->tulip_chipid == TULIP_21140A) {
	if (sc->tulip_rombuf[32] == TULIP_COGENT_EM100TX_ID) {
	    strcat(sc->tulip_boardid, "EM100TX ");
	    sc->tulip_boardsw = &tulip_21140_cogent_em100_boardsw;
#if defined(TULIP_COGENT_EM110TX_ID)
	} else if (sc->tulip_rombuf[32] == TULIP_COGENT_EM110TX_ID) {
	    strcat(sc->tulip_boardid, "EM110TX ");
	    sc->tulip_boardsw = &tulip_21140_cogent_em100_boardsw;
#endif
	} else if (sc->tulip_rombuf[32] == TULIP_COGENT_EM100FX_ID) {
	    strcat(sc->tulip_boardid, "EM100FX ");
	    sc->tulip_boardsw = &tulip_21140_cogent_em100_boardsw;
	}
	/*
	 * Magic number (0x24001109U) is the SubVendor (0x2400) and
	 * SubDevId (0x1109) for the ANA6944TX (EM440TX).
	 */
	if (*(u_int32_t *) sc->tulip_rombuf == 0x24001109U
		&& (sc->tulip_features & TULIP_HAVE_BASEROM)) {
	    /*
	     * Cogent (Adaptec) is still mapping all INTs to INTA of
	     * first 21140.  Dumb!  Dumb!
	     */
	    strcat(sc->tulip_boardid, "EM440TX ");
	    sc->tulip_features |= TULIP_HAVE_SHAREDINTR;
	}
    } else if (sc->tulip_chipid == TULIP_21040) {
	sc->tulip_features |= TULIP_HAVE_SHAREDINTR|TULIP_HAVE_BASEROM;
    }
}

static void
tulip_identify_accton_nic(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "ACCTON ");
    switch (sc->tulip_chipid) {
	case TULIP_21140A:
	    strcat(sc->tulip_boardid, "EN1207 ");
	    if (sc->tulip_boardsw != &tulip_2114x_isv_boardsw)
		sc->tulip_boardsw = &tulip_21140_accton_boardsw;
	    break;
	case TULIP_21140:
	    strcat(sc->tulip_boardid, "EN1207TX ");
	    if (sc->tulip_boardsw != &tulip_2114x_isv_boardsw)
		sc->tulip_boardsw = &tulip_21140_eb_boardsw;
            break;
        case TULIP_21040:
	    strcat(sc->tulip_boardid, "EN1203 ");
            sc->tulip_boardsw = &tulip_21040_boardsw;
            break;
        case TULIP_21041:
	    strcat(sc->tulip_boardid, "EN1203 ");
            sc->tulip_boardsw = &tulip_21041_boardsw;
            break;
	default:
            sc->tulip_boardsw = &tulip_2114x_isv_boardsw;
            break;
    }
}

static void
tulip_identify_asante_nic(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "Asante ");
    if ((sc->tulip_chipid == TULIP_21140 || sc->tulip_chipid == TULIP_21140A)
	    && sc->tulip_boardsw != &tulip_2114x_isv_boardsw) {
	tulip_media_info_t *mi = sc->tulip_mediainfo;
	int idx;
	/*
	 * The Asante Fast Ethernet doesn't always ship with a valid
	 * new format SROM.  So if isn't in the new format, we cheat
	 * set it up as if we had.
	 */

	sc->tulip_gpinit = TULIP_GP_ASANTE_PINS;
	sc->tulip_gpdata = 0;

	TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_ASANTE_PINS|TULIP_GP_PINSET);
	TULIP_CSR_WRITE(sc, csr_gp, TULIP_GP_ASANTE_PHYRESET);
	DELAY(100);
	TULIP_CSR_WRITE(sc, csr_gp, 0);

	mi->mi_type = TULIP_MEDIAINFO_MII;
	mi->mi_gpr_length = 0;
	mi->mi_gpr_offset = 0;
	mi->mi_reset_length = 0;
	mi->mi_reset_offset = 0;

	mi->mi_phyaddr = TULIP_MII_NOPHY;
	for (idx = 20; idx > 0 && mi->mi_phyaddr == TULIP_MII_NOPHY; idx--) {
	    DELAY(10000);
	    mi->mi_phyaddr = tulip_mii_get_phyaddr(sc, 0);
	}
	if (mi->mi_phyaddr == TULIP_MII_NOPHY) {
	    device_printf(sc->tulip_dev, "can't find phy 0\n");
	    return;
	}

	sc->tulip_features |= TULIP_HAVE_MII;
	mi->mi_capabilities  = PHYSTS_10BASET|PHYSTS_10BASET_FD|PHYSTS_100BASETX|PHYSTS_100BASETX_FD;
	mi->mi_advertisement = PHYSTS_10BASET|PHYSTS_10BASET_FD|PHYSTS_100BASETX|PHYSTS_100BASETX_FD;
	mi->mi_full_duplex   = PHYSTS_10BASET_FD|PHYSTS_100BASETX_FD;
	mi->mi_tx_threshold  = PHYSTS_10BASET|PHYSTS_10BASET_FD;
	TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASETX_FD);
	TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASETX);
	TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASET4);
	TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 10BASET_FD);
	TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 10BASET);
	mi->mi_phyid = (tulip_mii_readreg(sc, mi->mi_phyaddr, PHYREG_IDLOW) << 16) |
	    tulip_mii_readreg(sc, mi->mi_phyaddr, PHYREG_IDHIGH);

	sc->tulip_boardsw = &tulip_2114x_isv_boardsw;
    }
}

static void
tulip_identify_compex_nic(tulip_softc_t * const sc)
{
    TULIP_LOCK_ASSERT(sc);
    strcpy(sc->tulip_boardid, "COMPEX ");
    if (sc->tulip_chipid == TULIP_21140A) {
	int root_unit;
	tulip_softc_t *root_sc = NULL;

	strcat(sc->tulip_boardid, "400TX/PCI ");
	/*
	 * All 4 chips on these boards share an interrupt.  This code
	 * copied from tulip_read_macaddr.
	 */
	sc->tulip_features |= TULIP_HAVE_SHAREDINTR;
	for (root_unit = sc->tulip_unit - 1; root_unit >= 0; root_unit--) {
	    root_sc = tulips[root_unit];
	    if (root_sc == NULL
		|| !(root_sc->tulip_features & TULIP_HAVE_SLAVEDINTR))
		break;
	    root_sc = NULL;
	}
	if (root_sc != NULL
	    && root_sc->tulip_chipid == sc->tulip_chipid
	    && root_sc->tulip_pci_busno == sc->tulip_pci_busno) {
	    sc->tulip_features |= TULIP_HAVE_SLAVEDINTR;
	    sc->tulip_slaves = root_sc->tulip_slaves;
	    root_sc->tulip_slaves = sc;
	} else if(sc->tulip_features & TULIP_HAVE_SLAVEDINTR) {
	    printf("\nCannot find master device for %s interrupts",
		   sc->tulip_ifp->if_xname);
	}
    } else {
	strcat(sc->tulip_boardid, "unknown ");
    }
    /*      sc->tulip_boardsw = &tulip_21140_eb_boardsw; */
    return;
}

static int
tulip_srom_decode(tulip_softc_t * const sc)
{
    unsigned idx1, idx2, idx3;

    const tulip_srom_header_t *shp = (const tulip_srom_header_t *) &sc->tulip_rombuf[0];
    const tulip_srom_adapter_info_t *saip = (const tulip_srom_adapter_info_t *) (shp + 1);
    tulip_srom_media_t srom_media;
    tulip_media_info_t *mi = sc->tulip_mediainfo;
    const u_int8_t *dp;
    u_int32_t leaf_offset, blocks, data;

    TULIP_LOCK_ASSERT(sc);
    for (idx1 = 0; idx1 < shp->sh_adapter_count; idx1++, saip++) {
	if (shp->sh_adapter_count == 1)
	    break;
	if (saip->sai_device == sc->tulip_pci_devno)
	    break;
    }
    /*
     * Didn't find the right media block for this card.
     */
    if (idx1 == shp->sh_adapter_count)
	return 0;

    /*
     * Save the hardware address.
     */
    bcopy(shp->sh_ieee802_address, sc->tulip_enaddr, 6);
    /*
     * If this is a multiple port card, add the adapter index to the last
     * byte of the hardware address.  (if it isn't multiport, adding 0
     * won't hurt.
     */
    sc->tulip_enaddr[5] += idx1;

    leaf_offset = saip->sai_leaf_offset_lowbyte
	+ saip->sai_leaf_offset_highbyte * 256;
    dp = sc->tulip_rombuf + leaf_offset;
	
    sc->tulip_conntype = (tulip_srom_connection_t) (dp[0] + dp[1] * 256); dp += 2;

    for (idx2 = 0;; idx2++) {
	if (tulip_srom_conninfo[idx2].sc_type == sc->tulip_conntype
	        || tulip_srom_conninfo[idx2].sc_type == TULIP_SROM_CONNTYPE_NOT_USED)
	    break;
    }
    sc->tulip_connidx = idx2;

    if (sc->tulip_chipid == TULIP_21041) {
	blocks = *dp++;
	for (idx2 = 0; idx2 < blocks; idx2++) {
	    tulip_media_t media;
	    data = *dp++;
	    srom_media = (tulip_srom_media_t) (data & 0x3F);
	    for (idx3 = 0; tulip_srom_mediums[idx3].sm_type != TULIP_MEDIA_UNKNOWN; idx3++) {
		if (tulip_srom_mediums[idx3].sm_srom_type == srom_media)
		    break;
	    }
	    media = tulip_srom_mediums[idx3].sm_type;
	    if (media != TULIP_MEDIA_UNKNOWN) {
		if (data & TULIP_SROM_21041_EXTENDED) {
		    mi->mi_type = TULIP_MEDIAINFO_SIA;
		    sc->tulip_mediums[media] = mi;
		    mi->mi_sia_connectivity = dp[0] + dp[1] * 256;
		    mi->mi_sia_tx_rx        = dp[2] + dp[3] * 256;
		    mi->mi_sia_general      = dp[4] + dp[5] * 256;
		    mi++;
		} else {
		    switch (media) {
			case TULIP_MEDIA_BNC: {
			    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, BNC);
			    mi++;
			    break;
			}
			case TULIP_MEDIA_AUI: {
			    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, AUI);
			    mi++;
			    break;
			}
			case TULIP_MEDIA_10BASET: {
			    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, 10BASET);
			    mi++;
			    break;
			}
			case TULIP_MEDIA_10BASET_FD: {
			    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, 10BASET_FD);
			    mi++;
			    break;
			}
			default: {
			    break;
			}
		    }
		}
	    }
	    if (data & TULIP_SROM_21041_EXTENDED)	
		dp += 6;
	}
#ifdef notdef
	if (blocks == 0) {
	    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, BNC); mi++;
	    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, AUI); mi++;
	    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, 10BASET); mi++;
	    TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21041, 10BASET_FD); mi++;
	}
#endif
    } else {
	unsigned length, type;
	tulip_media_t gp_media = TULIP_MEDIA_UNKNOWN;
	if (sc->tulip_features & TULIP_HAVE_GPR)
	    sc->tulip_gpinit = *dp++;
	blocks = *dp++;
	for (idx2 = 0; idx2 < blocks; idx2++) {
	    const u_int8_t *ep;
	    if ((*dp & 0x80) == 0) {
		length = 4;
		type = 0;
	    } else {
		length = (*dp++ & 0x7f) - 1;
		type = *dp++ & 0x3f;
	    }
	    ep = dp + length;
	    switch (type & 0x3f) {
		case 0: {	/* 21140[A] GPR block */
		    tulip_media_t media;
		    srom_media = (tulip_srom_media_t)(dp[0] & 0x3f);
		    for (idx3 = 0; tulip_srom_mediums[idx3].sm_type != TULIP_MEDIA_UNKNOWN; idx3++) {
			if (tulip_srom_mediums[idx3].sm_srom_type == srom_media)
			    break;
		    }
		    media = tulip_srom_mediums[idx3].sm_type;
		    if (media == TULIP_MEDIA_UNKNOWN)
			break;
		    mi->mi_type = TULIP_MEDIAINFO_GPR;
		    sc->tulip_mediums[media] = mi;
		    mi->mi_gpdata = dp[1];
		    if (media > gp_media && !TULIP_IS_MEDIA_FD(media)) {
			sc->tulip_gpdata = mi->mi_gpdata;
			gp_media = media;
		    }
		    data = dp[2] + dp[3] * 256;
		    mi->mi_cmdmode = TULIP_SROM_2114X_CMDBITS(data);
		    if (data & TULIP_SROM_2114X_NOINDICATOR) {
			mi->mi_actmask = 0;
		    } else {
#if 0
			mi->mi_default = (data & TULIP_SROM_2114X_DEFAULT) != 0;
#endif
			mi->mi_actmask = TULIP_SROM_2114X_BITPOS(data);
			mi->mi_actdata = (data & TULIP_SROM_2114X_POLARITY) ? 0 : mi->mi_actmask;
		    }
		    mi++;
		    break;
		}
		case 1: {	/* 21140[A] MII block */
		    const unsigned phyno = *dp++;
		    mi->mi_type = TULIP_MEDIAINFO_MII;
		    mi->mi_gpr_length = *dp++;
		    mi->mi_gpr_offset = dp - sc->tulip_rombuf;
		    dp += mi->mi_gpr_length;
		    mi->mi_reset_length = *dp++;
		    mi->mi_reset_offset = dp - sc->tulip_rombuf;
		    dp += mi->mi_reset_length;

		    /*
		     * Before we probe for a PHY, use the GPR information
		     * to select it.  If we don't, it may be inaccessible.
		     */
		    TULIP_CSR_WRITE(sc, csr_gp, sc->tulip_gpinit|TULIP_GP_PINSET);
		    for (idx3 = 0; idx3 < mi->mi_reset_length; idx3++) {
			DELAY(10);
			TULIP_CSR_WRITE(sc, csr_gp, sc->tulip_rombuf[mi->mi_reset_offset + idx3]);
		    }
		    sc->tulip_phyaddr = mi->mi_phyaddr;
		    for (idx3 = 0; idx3 < mi->mi_gpr_length; idx3++) {
			DELAY(10);
			TULIP_CSR_WRITE(sc, csr_gp, sc->tulip_rombuf[mi->mi_gpr_offset + idx3]);
		    }

		    /*
		     * At least write something!
		     */
		    if (mi->mi_reset_length == 0 && mi->mi_gpr_length == 0)
			TULIP_CSR_WRITE(sc, csr_gp, 0);

		    mi->mi_phyaddr = TULIP_MII_NOPHY;
		    for (idx3 = 20; idx3 > 0 && mi->mi_phyaddr == TULIP_MII_NOPHY; idx3--) {
			DELAY(10000);
			mi->mi_phyaddr = tulip_mii_get_phyaddr(sc, phyno);
		    }
		    if (mi->mi_phyaddr == TULIP_MII_NOPHY) {
#if defined(TULIP_DEBUG)
			device_printf(sc->tulip_dev, "can't find phy %d\n",
			    phyno);
#endif
			break;
		    }
		    sc->tulip_features |= TULIP_HAVE_MII;
		    mi->mi_capabilities  = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_advertisement = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_full_duplex   = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_tx_threshold  = dp[0] + dp[1] * 256; dp += 2;
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASETX_FD);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASETX);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASET4);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 10BASET_FD);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 10BASET);
		    mi->mi_phyid = (tulip_mii_readreg(sc, mi->mi_phyaddr, PHYREG_IDLOW) << 16) |
			tulip_mii_readreg(sc, mi->mi_phyaddr, PHYREG_IDHIGH);
		    mi++;
		    break;
		}
		case 2: {	/* 2114[23] SIA block */
		    tulip_media_t media;
		    srom_media = (tulip_srom_media_t)(dp[0] & 0x3f);
		    for (idx3 = 0; tulip_srom_mediums[idx3].sm_type != TULIP_MEDIA_UNKNOWN; idx3++) {
			if (tulip_srom_mediums[idx3].sm_srom_type == srom_media)
			    break;
		    }
		    media = tulip_srom_mediums[idx3].sm_type;
		    if (media == TULIP_MEDIA_UNKNOWN)
			break;
		    mi->mi_type = TULIP_MEDIAINFO_SIA;
		    sc->tulip_mediums[media] = mi;
		    if (dp[0] & 0x40) {
			mi->mi_sia_connectivity = dp[1] + dp[2] * 256;
			mi->mi_sia_tx_rx        = dp[3] + dp[4] * 256;
			mi->mi_sia_general      = dp[5] + dp[6] * 256;
			dp += 6;
		    } else {
			switch (media) {
			    case TULIP_MEDIA_BNC: {
				TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21142, BNC);
				break;
			    }
			    case TULIP_MEDIA_AUI: {
				TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21142, AUI);
				break;
			    }
			    case TULIP_MEDIA_10BASET: {
				TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21142, 10BASET);
				sc->tulip_intrmask |= TULIP_STS_LINKPASS|TULIP_STS_LINKFAIL;
				break;
			    }
			    case TULIP_MEDIA_10BASET_FD: {
				TULIP_MEDIAINFO_SIA_INIT(sc, mi, 21142, 10BASET_FD);
				sc->tulip_intrmask |= TULIP_STS_LINKPASS|TULIP_STS_LINKFAIL;
				break;
			    }
			    default: {
				goto bad_media;
			    }
			}
		    }
		    mi->mi_sia_gp_control = (dp[1] + dp[2] * 256) << 16;
		    mi->mi_sia_gp_data    = (dp[3] + dp[4] * 256) << 16;
		    mi++;
		  bad_media:
		    break;
		}
		case 3: {	/* 2114[23] MII PHY block */
		    const unsigned phyno = *dp++;
		    const u_int8_t *dp0;
		    mi->mi_type = TULIP_MEDIAINFO_MII;
		    mi->mi_gpr_length = *dp++;
		    mi->mi_gpr_offset = dp - sc->tulip_rombuf;
		    dp += 2 * mi->mi_gpr_length;
		    mi->mi_reset_length = *dp++;
		    mi->mi_reset_offset = dp - sc->tulip_rombuf;
		    dp += 2 * mi->mi_reset_length;

		    dp0 = &sc->tulip_rombuf[mi->mi_reset_offset];
		    for (idx3 = 0; idx3 < mi->mi_reset_length; idx3++, dp0 += 2) {
			DELAY(10);
			TULIP_CSR_WRITE(sc, csr_sia_general, (dp0[0] + 256 * dp0[1]) << 16);
		    }
		    sc->tulip_phyaddr = mi->mi_phyaddr;
		    dp0 = &sc->tulip_rombuf[mi->mi_gpr_offset];
		    for (idx3 = 0; idx3 < mi->mi_gpr_length; idx3++, dp0 += 2) {
			DELAY(10);
			TULIP_CSR_WRITE(sc, csr_sia_general, (dp0[0] + 256 * dp0[1]) << 16);
		    }

		    if (mi->mi_reset_length == 0 && mi->mi_gpr_length == 0)
			TULIP_CSR_WRITE(sc, csr_sia_general, 0);

		    mi->mi_phyaddr = TULIP_MII_NOPHY;
		    for (idx3 = 20; idx3 > 0 && mi->mi_phyaddr == TULIP_MII_NOPHY; idx3--) {
			DELAY(10000);
			mi->mi_phyaddr = tulip_mii_get_phyaddr(sc, phyno);
		    }
		    if (mi->mi_phyaddr == TULIP_MII_NOPHY) {
#if defined(TULIP_DEBUG)
			device_printf(sc->tulip_dev, "can't find phy %d\n",
			       phyno);
#endif
			break;
		    }
		    sc->tulip_features |= TULIP_HAVE_MII;
		    mi->mi_capabilities  = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_advertisement = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_full_duplex   = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_tx_threshold  = dp[0] + dp[1] * 256; dp += 2;
		    mi->mi_mii_interrupt = dp[0] + dp[1] * 256; dp += 2;
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASETX_FD);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASETX);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 100BASET4);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 10BASET_FD);
		    TULIP_MEDIAINFO_ADD_CAPABILITY(sc, mi, 10BASET);
		    mi->mi_phyid = (tulip_mii_readreg(sc, mi->mi_phyaddr, PHYREG_IDLOW) << 16) |
			tulip_mii_readreg(sc, mi->mi_phyaddr, PHYREG_IDHIGH);
		    mi++;
		    break;
		}
		case 4: {	/* 21143 SYM block */
		    tulip_media_t media;
		    srom_media = (tulip_srom_media_t) dp[0];
		    for (idx3 = 0; tulip_srom_mediums[idx3].sm_type != TULIP_MEDIA_UNKNOWN; idx3++) {
			if (tulip_srom_mediums[idx3].sm_srom_type == srom_media)
			    break;
		    }
		    media = tulip_srom_mediums[idx3].sm_type;
		    if (media == TULIP_MEDIA_UNKNOWN)
			break;
		    mi->mi_type = TULIP_MEDIAINFO_SYM;
		    sc->tulip_mediums[media] = mi;
		    mi->mi_gpcontrol = (dp[1] + dp[2] * 256) << 16;
		    mi->mi_gpdata    = (dp[3] + dp[4] * 256) << 16;
		    data = dp[5] + dp[6] * 256;
		    mi->mi_cmdmode = TULIP_SROM_2114X_CMDBITS(data);
		    if (data & TULIP_SROM_2114X_NOINDICATOR) {
			mi->mi_actmask = 0;
		    } else {
			mi->mi_default = (data & TULIP_SROM_2114X_DEFAULT) != 0;
			mi->mi_actmask = TULIP_SROM_2114X_BITPOS(data);
			mi->mi_actdata = (data & TULIP_SROM_2114X_POLARITY) ? 0 : mi->mi_actmask;
		    }
		    if (TULIP_IS_MEDIA_TP(media))
			sc->tulip_intrmask |= TULIP_STS_LINKPASS|TULIP_STS_LINKFAIL;
		    mi++;
		    break;
		}
#if 0
		case 5: {	/* 21143 Reset block */
		    mi->mi_type = TULIP_MEDIAINFO_RESET;
		    mi->mi_reset_length = *dp++;
		    mi->mi_reset_offset = dp - sc->tulip_rombuf;
		    dp += 2 * mi->mi_reset_length;
		    mi++;
		    break;
		}
#endif
		default: {
		}
	    }
	    dp = ep;
	}
    }
    return mi - sc->tulip_mediainfo;
}

static const struct {
    void (*vendor_identify_nic)(tulip_softc_t * const sc);
    unsigned char vendor_oui[3];
} tulip_vendors[] = {
    { tulip_identify_dec_nic,		{ 0x08, 0x00, 0x2B } },
    { tulip_identify_dec_nic,		{ 0x00, 0x00, 0xF8 } },
    { tulip_identify_smc_nic,		{ 0x00, 0x00, 0xC0 } },
    { tulip_identify_smc_nic,		{ 0x00, 0xE0, 0x29 } },
    { tulip_identify_znyx_nic,		{ 0x00, 0xC0, 0x95 } },
    { tulip_identify_cogent_nic,	{ 0x00, 0x00, 0x92 } },
    { tulip_identify_asante_nic,	{ 0x00, 0x00, 0x94 } },
    { tulip_identify_cogent_nic,	{ 0x00, 0x00, 0xD1 } },
    { tulip_identify_accton_nic,	{ 0x00, 0x00, 0xE8 } },
    { tulip_identify_compex_nic,        { 0x00, 0x80, 0x48 } },
    { NULL }
};

/*
 * This deals with the vagaries of the address roms and the
 * brain-deadness that various vendors commit in using them.
 */
static int
tulip_read_macaddr(tulip_softc_t * const sc)
{
    unsigned cksum, rom_cksum, idx;
    u_int32_t csr;
    unsigned char tmpbuf[8];
    static const u_char testpat[] = { 0xFF, 0, 0x55, 0xAA, 0xFF, 0, 0x55, 0xAA };

    sc->tulip_connidx = TULIP_SROM_LASTCONNIDX;

    if (sc->tulip_chipid == TULIP_21040) {
	TULIP_CSR_WRITE(sc, csr_enetrom, 1);
	for (idx = 0; idx < sizeof(sc->tulip_rombuf); idx++) {
	    int cnt = 0;
	    while (((csr = TULIP_CSR_READ(sc, csr_enetrom)) & 0x80000000L) && cnt < 10000)
		cnt++;
	    sc->tulip_rombuf[idx] = csr & 0xFF;
	}
	sc->tulip_boardsw = &tulip_21040_boardsw;
    } else {
	if (sc->tulip_chipid == TULIP_21041) {
	    /*
	     * Thankfully all 21041's act the same.
	     */
	    sc->tulip_boardsw = &tulip_21041_boardsw;
	} else {
	    /*
	     * Assume all 21140 board are compatible with the
	     * DEC 10/100 evaluation board.  Not really valid but
	     * it's the best we can do until every one switches to
	     * the new SROM format.
	     */

	    sc->tulip_boardsw = &tulip_21140_eb_boardsw;
	}
	tulip_srom_read(sc);
	if (tulip_srom_crcok(sc->tulip_rombuf)) {
	    /*
	     * SROM CRC is valid therefore it must be in the
	     * new format.
	     */
	    sc->tulip_features |= TULIP_HAVE_ISVSROM|TULIP_HAVE_OKSROM;
	} else if (sc->tulip_rombuf[126] == 0xff && sc->tulip_rombuf[127] == 0xFF) {
	    /*
	     * No checksum is present.  See if the SROM id checks out;
	     * the first 18 bytes should be 0 followed by a 1 followed
	     * by the number of adapters (which we don't deal with yet).
	     */
	    for (idx = 0; idx < 18; idx++) {
		if (sc->tulip_rombuf[idx] != 0)
		    break;
	    }
	    if (idx == 18 && sc->tulip_rombuf[18] == 1 && sc->tulip_rombuf[19] != 0)
		sc->tulip_features |= TULIP_HAVE_ISVSROM;
	} else if (sc->tulip_chipid >= TULIP_21142) {
	    sc->tulip_features |= TULIP_HAVE_ISVSROM;
	    sc->tulip_boardsw = &tulip_2114x_isv_boardsw;
	}
	if ((sc->tulip_features & TULIP_HAVE_ISVSROM) && tulip_srom_decode(sc)) {
	    if (sc->tulip_chipid != TULIP_21041)
		sc->tulip_boardsw = &tulip_2114x_isv_boardsw;

	    /*
	     * If the SROM specifies more than one adapter, tag this as a
	     * BASE rom.
	     */
	    if (sc->tulip_rombuf[19] > 1)
		sc->tulip_features |= TULIP_HAVE_BASEROM;
	    if (sc->tulip_boardsw == NULL)
		return -6;
	    goto check_oui;
	}
    }


    if (bcmp(&sc->tulip_rombuf[0], &sc->tulip_rombuf[16], 8) != 0) {
	/*
	 * Some folks don't use the standard ethernet rom format
	 * but instead just put the address in the first 6 bytes
	 * of the rom and let the rest be all 0xffs.  (Can we say
	 * ZNYX?) (well sometimes they put in a checksum so we'll
	 * start at 8).
	 */
	for (idx = 8; idx < 32; idx++) {
	    if (sc->tulip_rombuf[idx] != 0xFF)
		return -4;
	}
	/*
	 * Make sure the address is not multicast or locally assigned
	 * that the OUI is not 00-00-00.
	 */
	if ((sc->tulip_rombuf[0] & 3) != 0)
	    return -4;
	if (sc->tulip_rombuf[0] == 0 && sc->tulip_rombuf[1] == 0
		&& sc->tulip_rombuf[2] == 0)
	    return -4;
	bcopy(sc->tulip_rombuf, sc->tulip_enaddr, 6);
	sc->tulip_features |= TULIP_HAVE_OKROM;
	goto check_oui;
    } else {
	/*
	 * A number of makers of multiport boards (ZNYX and Cogent)
	 * only put on one address ROM on their 21040 boards.  So
	 * if the ROM is all zeros (or all 0xFFs), look at the
	 * previous configured boards (as long as they are on the same
	 * PCI bus and the bus number is non-zero) until we find the
	 * master board with address ROM.  We then use its address ROM
	 * as the base for this board.  (we add our relative board
	 * to the last byte of its address).
	 */
	for (idx = 0; idx < sizeof(sc->tulip_rombuf); idx++) {
	    if (sc->tulip_rombuf[idx] != 0 && sc->tulip_rombuf[idx] != 0xFF)
		break;
	}
	if (idx == sizeof(sc->tulip_rombuf)) {
	    int root_unit;
	    tulip_softc_t *root_sc = NULL;
	    for (root_unit = sc->tulip_unit - 1; root_unit >= 0; root_unit--) {
		root_sc = tulips[root_unit];
		if (root_sc == NULL || (root_sc->tulip_features & (TULIP_HAVE_OKROM|TULIP_HAVE_SLAVEDROM)) == TULIP_HAVE_OKROM)
		    break;
		root_sc = NULL;
	    }
	    if (root_sc != NULL && (root_sc->tulip_features & TULIP_HAVE_BASEROM)
		    && root_sc->tulip_chipid == sc->tulip_chipid
		    && root_sc->tulip_pci_busno == sc->tulip_pci_busno) {
		sc->tulip_features |= TULIP_HAVE_SLAVEDROM;
		sc->tulip_boardsw = root_sc->tulip_boardsw;
		strcpy(sc->tulip_boardid, root_sc->tulip_boardid);
		if (sc->tulip_boardsw->bd_type == TULIP_21140_ISV) {
		    bcopy(root_sc->tulip_rombuf, sc->tulip_rombuf,
			  sizeof(sc->tulip_rombuf));
		    if (!tulip_srom_decode(sc))
			return -5;
		} else {
		    bcopy(root_sc->tulip_enaddr, sc->tulip_enaddr, 6);
		    sc->tulip_enaddr[5] += sc->tulip_unit - root_sc->tulip_unit;
		}
		/*
		 * Now for a truly disgusting kludge: all 4 21040s on
		 * the ZX314 share the same INTA line so the mapping
		 * setup by the BIOS on the PCI bridge is worthless.
		 * Rather than reprogramming the value in the config
		 * register, we will handle this internally.
		 */
		if (root_sc->tulip_features & TULIP_HAVE_SHAREDINTR) {
		    sc->tulip_slaves = root_sc->tulip_slaves;
		    root_sc->tulip_slaves = sc;
		    sc->tulip_features |= TULIP_HAVE_SLAVEDINTR;
		}
		return 0;
	    }
	}
    }

    /*
     * This is the standard DEC address ROM test.
     */

    if (bcmp(&sc->tulip_rombuf[24], testpat, 8) != 0)
	return -3;

    tmpbuf[0] = sc->tulip_rombuf[15]; tmpbuf[1] = sc->tulip_rombuf[14];
    tmpbuf[2] = sc->tulip_rombuf[13]; tmpbuf[3] = sc->tulip_rombuf[12];
    tmpbuf[4] = sc->tulip_rombuf[11]; tmpbuf[5] = sc->tulip_rombuf[10];
    tmpbuf[6] = sc->tulip_rombuf[9];  tmpbuf[7] = sc->tulip_rombuf[8];
    if (bcmp(&sc->tulip_rombuf[0], tmpbuf, 8) != 0)
	return -2;

    bcopy(sc->tulip_rombuf, sc->tulip_enaddr, 6);

    cksum = *(u_int16_t *) &sc->tulip_enaddr[0];
    cksum *= 2;
    if (cksum > 65535) cksum -= 65535;
    cksum += *(u_int16_t *) &sc->tulip_enaddr[2];
    if (cksum > 65535) cksum -= 65535;
    cksum *= 2;
    if (cksum > 65535) cksum -= 65535;
    cksum += *(u_int16_t *) &sc->tulip_enaddr[4];
    if (cksum >= 65535) cksum -= 65535;

    rom_cksum = *(u_int16_t *) &sc->tulip_rombuf[6];
	
    if (cksum != rom_cksum)
	return -1;

  check_oui:
    /*
     * Check for various boards based on OUI.  Did I say braindead?
     */
    for (idx = 0; tulip_vendors[idx].vendor_identify_nic != NULL; idx++) {
	if (bcmp(sc->tulip_enaddr, tulip_vendors[idx].vendor_oui, 3) == 0) {
	    (*tulip_vendors[idx].vendor_identify_nic)(sc);
	    break;
	}
    }

    sc->tulip_features |= TULIP_HAVE_OKROM;
    return 0;
}

static void
tulip_ifmedia_add(tulip_softc_t * const sc)
{
    tulip_media_t media;
    int medias = 0;

    TULIP_LOCK_ASSERT(sc);
    for (media = TULIP_MEDIA_UNKNOWN; media < TULIP_MEDIA_MAX; media++) {
	if (sc->tulip_mediums[media] != NULL) {
	    ifmedia_add(&sc->tulip_ifmedia, tulip_media_to_ifmedia[media],
			0, 0);
	    medias++;
	}
    }
    if (medias == 0) {
	sc->tulip_features |= TULIP_HAVE_NOMEDIA;
	ifmedia_add(&sc->tulip_ifmedia, IFM_ETHER | IFM_NONE, 0, 0);
	ifmedia_set(&sc->tulip_ifmedia, IFM_ETHER | IFM_NONE);
    } else if (sc->tulip_media == TULIP_MEDIA_UNKNOWN) {
	ifmedia_add(&sc->tulip_ifmedia, IFM_ETHER | IFM_AUTO, 0, 0);
	ifmedia_set(&sc->tulip_ifmedia, IFM_ETHER | IFM_AUTO);
    } else {
	ifmedia_set(&sc->tulip_ifmedia, tulip_media_to_ifmedia[sc->tulip_media]);
	sc->tulip_flags |= TULIP_PRINTMEDIA;
	tulip_linkup(sc, sc->tulip_media);
    }
}

static int
tulip_ifmedia_change(struct ifnet * const ifp)
{
    tulip_softc_t * const sc = (tulip_softc_t *)ifp->if_softc;

    TULIP_LOCK(sc);
    sc->tulip_flags |= TULIP_NEEDRESET;
    sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
    sc->tulip_media = TULIP_MEDIA_UNKNOWN;
    if (IFM_SUBTYPE(sc->tulip_ifmedia.ifm_media) != IFM_AUTO) {
	tulip_media_t media;
	for (media = TULIP_MEDIA_UNKNOWN; media < TULIP_MEDIA_MAX; media++) {
	    if (sc->tulip_mediums[media] != NULL
		&& sc->tulip_ifmedia.ifm_media == tulip_media_to_ifmedia[media]) {
		sc->tulip_flags |= TULIP_PRINTMEDIA;
		sc->tulip_flags &= ~TULIP_DIDNWAY;
		tulip_linkup(sc, media);
		TULIP_UNLOCK(sc);
		return 0;
	    }
	}
    }
    sc->tulip_flags &= ~(TULIP_TXPROBE_ACTIVE|TULIP_WANTRXACT);
    tulip_reset(sc);
    tulip_init_locked(sc);
    TULIP_UNLOCK(sc);
    return 0;
}

/*
 * Media status callback
 */
static void
tulip_ifmedia_status(struct ifnet * const ifp, struct ifmediareq *req)
{
    tulip_softc_t *sc = (tulip_softc_t *)ifp->if_softc;

    TULIP_LOCK(sc);
    if (sc->tulip_media == TULIP_MEDIA_UNKNOWN) {
	TULIP_UNLOCK(sc);
	return;
    }

    req->ifm_status = IFM_AVALID;
    if (sc->tulip_flags & TULIP_LINKUP)
	req->ifm_status |= IFM_ACTIVE;

    req->ifm_active = tulip_media_to_ifmedia[sc->tulip_media];
    TULIP_UNLOCK(sc);
}

static void
tulip_addr_filter(tulip_softc_t * const sc)
{
    struct ifmultiaddr *ifma;
    struct ifnet *ifp;
    u_char *addrp;
    u_int16_t eaddr[ETHER_ADDR_LEN/2];
    int multicnt;

    TULIP_LOCK_ASSERT(sc);
    sc->tulip_flags &= ~(TULIP_WANTHASHPERFECT|TULIP_WANTHASHONLY|TULIP_ALLMULTI);
    sc->tulip_flags |= TULIP_WANTSETUP|TULIP_WANTTXSTART;
    sc->tulip_cmdmode &= ~TULIP_CMD_RXRUN;
    sc->tulip_intrmask &= ~TULIP_STS_RXSTOPPED;
#if defined(IFF_ALLMULTI)    
    if (sc->tulip_ifp->if_flags & IFF_ALLMULTI)
	sc->tulip_flags |= TULIP_ALLMULTI ;
#endif

    multicnt = 0;
    ifp = sc->tulip_ifp;      
    if_maddr_rlock(ifp);

    /* Copy MAC address on stack to align. */
    if (ifp->if_input != NULL)
    	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
    else
	bcopy(sc->tulip_enaddr, eaddr, ETHER_ADDR_LEN);

    CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {

	    if (ifma->ifma_addr->sa_family == AF_LINK)
		multicnt++;
    }

    if (multicnt > 14) {
	u_int32_t *sp = sc->tulip_setupdata;
	unsigned hash;
	/*
	 * Some early passes of the 21140 have broken implementations of
	 * hash-perfect mode.  When we get too many multicasts for perfect
	 * filtering with these chips, we need to switch into hash-only
	 * mode (this is better than all-multicast on network with lots
	 * of multicast traffic).
	 */
	if (sc->tulip_features & TULIP_HAVE_BROKEN_HASH)
	    sc->tulip_flags |= TULIP_WANTHASHONLY;
	else
	    sc->tulip_flags |= TULIP_WANTHASHPERFECT;
	/*
	 * If we have more than 14 multicasts, we have
	 * go into hash perfect mode (512 bit multicast
	 * hash and one perfect hardware).
	 */
	bzero(sc->tulip_setupdata, sizeof(sc->tulip_setupdata));

	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {

		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		hash = tulip_mchash(LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		sp[hash >> 4] |= htole32(1 << (hash & 0xF));
	}
	/*
	 * No reason to use a hash if we are going to be
	 * receiving every multicast.
	 */
	if ((sc->tulip_flags & TULIP_ALLMULTI) == 0) {
	    hash = tulip_mchash(ifp->if_broadcastaddr);
	    sp[hash >> 4] |= htole32(1 << (hash & 0xF));
	    if (sc->tulip_flags & TULIP_WANTHASHONLY) {
		hash = tulip_mchash((caddr_t)eaddr);
		sp[hash >> 4] |= htole32(1 << (hash & 0xF));
	    } else {
		sp[39] = TULIP_SP_MAC(eaddr[0]); 
		sp[40] = TULIP_SP_MAC(eaddr[1]); 
		sp[41] = TULIP_SP_MAC(eaddr[2]);
	    }
	}
    }
    if ((sc->tulip_flags & (TULIP_WANTHASHPERFECT|TULIP_WANTHASHONLY)) == 0) {
	u_int32_t *sp = sc->tulip_setupdata;
	int idx = 0;
	if ((sc->tulip_flags & TULIP_ALLMULTI) == 0) {
	    /*
	     * Else can get perfect filtering for 16 addresses.
	     */
	    CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		    if (ifma->ifma_addr->sa_family != AF_LINK)
			    continue;
		    addrp = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
		    *sp++ = TULIP_SP_MAC(((u_int16_t *)addrp)[0]); 
		    *sp++ = TULIP_SP_MAC(((u_int16_t *)addrp)[1]); 
		    *sp++ = TULIP_SP_MAC(((u_int16_t *)addrp)[2]);
		    idx++;
	    }
	    /*
	     * Add the broadcast address.
	     */
	    idx++;
	    *sp++ = TULIP_SP_MAC(0xFFFF);
	    *sp++ = TULIP_SP_MAC(0xFFFF);
	    *sp++ = TULIP_SP_MAC(0xFFFF);
	}
	/*
	 * Pad the rest with our hardware address
	 */
	for (; idx < 16; idx++) {
	    *sp++ = TULIP_SP_MAC(eaddr[0]); 
	    *sp++ = TULIP_SP_MAC(eaddr[1]); 
	    *sp++ = TULIP_SP_MAC(eaddr[2]);
	}
    }
    if_maddr_runlock(ifp);
}

static void
tulip_reset(tulip_softc_t * const sc)
{
    tulip_ringinfo_t *ri;
    tulip_descinfo_t *di;
    struct mbuf *m;
    u_int32_t inreset = (sc->tulip_flags & TULIP_INRESET);

    TULIP_LOCK_ASSERT(sc);

    CTR1(KTR_TULIP, "tulip_reset: inreset %d", inreset);

    /*
     * Brilliant.  Simply brilliant.  When switching modes/speeds
     * on a 2114*, you need to set the appriopriate MII/PCS/SCL/PS
     * bits in CSR6 and then do a software reset to get the 21140
     * to properly reset its internal pathways to the right places.
     *   Grrrr.
     */
    if ((sc->tulip_flags & TULIP_DEVICEPROBE) == 0
	    && sc->tulip_boardsw->bd_media_preset != NULL)
	(*sc->tulip_boardsw->bd_media_preset)(sc);

    TULIP_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    DELAY(10);	/* Wait 10 microseconds (actually 50 PCI cycles but at 
		   33MHz that comes to two microseconds but wait a
		   bit longer anyways) */

    if (!inreset) {
	sc->tulip_flags |= TULIP_INRESET;
	sc->tulip_flags &= ~(TULIP_NEEDRESET|TULIP_RXBUFSLOW);
	sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
    }

    TULIP_CSR_WRITE(sc, csr_txlist, sc->tulip_txinfo.ri_dma_addr & 0xffffffff);
    TULIP_CSR_WRITE(sc, csr_rxlist, sc->tulip_rxinfo.ri_dma_addr & 0xffffffff);
    TULIP_CSR_WRITE(sc, csr_busmode,
		    (1 << (3 /*pci_max_burst_len*/ + 8))
		    |TULIP_BUSMODE_CACHE_ALIGN8
		    |TULIP_BUSMODE_READMULTIPLE
		    |(BYTE_ORDER != LITTLE_ENDIAN ?
		      TULIP_BUSMODE_DESC_BIGENDIAN : 0));

    sc->tulip_txtimer = 0;
    /*
     * Free all the mbufs that were on the transmit ring.
     */
    CTR0(KTR_TULIP, "tulip_reset: drain transmit ring");
    ri = &sc->tulip_txinfo;
    for (di = ri->ri_first; di < ri->ri_last; di++) {
	m = tulip_dequeue_mbuf(ri, di, SYNC_NONE);
	if (m != NULL)
	    m_freem(m);
	di->di_desc->d_status = 0;
    }

    ri->ri_nextin = ri->ri_nextout = ri->ri_first;
    ri->ri_free = ri->ri_max;
    TULIP_TXDESC_PRESYNC(ri);

    /*
     * We need to collect all the mbufs that were on the 
     * receive ring before we reinit it either to put
     * them back on or to know if we have to allocate
     * more.
     */
    CTR0(KTR_TULIP, "tulip_reset: drain receive ring");
    ri = &sc->tulip_rxinfo;
    ri->ri_nextin = ri->ri_nextout = ri->ri_first;
    ri->ri_free = ri->ri_max;
    for (di = ri->ri_first; di < ri->ri_last; di++) {
	di->di_desc->d_status = 0;
	di->di_desc->d_length1 = 0; di->di_desc->d_addr1 = 0;
	di->di_desc->d_length2 = 0; di->di_desc->d_addr2 = 0;
    }
    TULIP_RXDESC_PRESYNC(ri);
    for (di = ri->ri_first; di < ri->ri_last; di++) {
	m = tulip_dequeue_mbuf(ri, di, SYNC_NONE);
	if (m != NULL)
	    m_freem(m);
    }

    /*
     * If tulip_reset is being called recursively, exit quickly knowing
     * that when the outer tulip_reset returns all the right stuff will
     * have happened.
     */
    if (inreset)
	return;

    sc->tulip_intrmask |= TULIP_STS_NORMALINTR|TULIP_STS_RXINTR|TULIP_STS_TXINTR
	|TULIP_STS_ABNRMLINTR|TULIP_STS_SYSERROR|TULIP_STS_TXSTOPPED
	|TULIP_STS_TXUNDERFLOW|TULIP_STS_TXBABBLE
	|TULIP_STS_RXSTOPPED;

    if ((sc->tulip_flags & TULIP_DEVICEPROBE) == 0)
	(*sc->tulip_boardsw->bd_media_select)(sc);
#if defined(TULIP_DEBUG)
    if ((sc->tulip_flags & TULIP_NEEDRESET) == TULIP_NEEDRESET)
	device_printf(sc->tulip_dev,
	    "tulip_reset: additional reset needed?!?\n");
#endif
    if (bootverbose)
	    tulip_media_print(sc);
    if (sc->tulip_features & TULIP_HAVE_DUALSENSE)
	TULIP_CSR_WRITE(sc, csr_sia_status, TULIP_CSR_READ(sc, csr_sia_status));

    sc->tulip_flags &= ~(TULIP_DOINGSETUP|TULIP_WANTSETUP|TULIP_INRESET
			 |TULIP_RXACT);
}


static void
tulip_init(void *arg)
{
    tulip_softc_t *sc = (tulip_softc_t *)arg;

    TULIP_LOCK(sc);
    tulip_init_locked(sc);
    TULIP_UNLOCK(sc);
}

static void
tulip_init_locked(tulip_softc_t * const sc)
{
    CTR0(KTR_TULIP, "tulip_init_locked");
    if (sc->tulip_ifp->if_flags & IFF_UP) {
	if ((sc->tulip_ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
	    /* initialize the media */
	    CTR0(KTR_TULIP, "tulip_init_locked: up but not running, reset chip");
	    tulip_reset(sc);
	}
	tulip_addr_filter(sc);
	sc->tulip_ifp->if_drv_flags |= IFF_DRV_RUNNING;
	if (sc->tulip_ifp->if_flags & IFF_PROMISC) {
	    sc->tulip_flags |= TULIP_PROMISC;
	    sc->tulip_cmdmode |= TULIP_CMD_PROMISCUOUS;
	    sc->tulip_intrmask |= TULIP_STS_TXINTR;
	} else {
	    sc->tulip_flags &= ~TULIP_PROMISC;
	    sc->tulip_cmdmode &= ~TULIP_CMD_PROMISCUOUS;
	    if (sc->tulip_flags & TULIP_ALLMULTI) {
		sc->tulip_cmdmode |= TULIP_CMD_ALLMULTI;
	    } else {
		sc->tulip_cmdmode &= ~TULIP_CMD_ALLMULTI;
	    }
	}
	sc->tulip_cmdmode |= TULIP_CMD_TXRUN;
	if ((sc->tulip_flags & (TULIP_TXPROBE_ACTIVE|TULIP_WANTSETUP)) == 0) {
	    tulip_rx_intr(sc);
	    sc->tulip_cmdmode |= TULIP_CMD_RXRUN;
	    sc->tulip_intrmask |= TULIP_STS_RXSTOPPED;
	} else {
	    sc->tulip_ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	    sc->tulip_cmdmode &= ~TULIP_CMD_RXRUN;
	    sc->tulip_intrmask &= ~TULIP_STS_RXSTOPPED;
	}
	CTR2(KTR_TULIP, "tulip_init_locked: intr mask %08x  cmdmode %08x",
	    sc->tulip_intrmask, sc->tulip_cmdmode);
	TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
	TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
	CTR1(KTR_TULIP, "tulip_init_locked: status %08x\n",
	    TULIP_CSR_READ(sc, csr_status));
	if ((sc->tulip_flags & (TULIP_WANTSETUP|TULIP_TXPROBE_ACTIVE)) == TULIP_WANTSETUP)
	    tulip_txput_setup(sc);
	callout_reset(&sc->tulip_stat_timer, hz, tulip_watchdog, sc);
    } else {
	CTR0(KTR_TULIP, "tulip_init_locked: not up, reset chip");
	sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	tulip_reset(sc);
	tulip_addr_filter(sc);
	callout_stop(&sc->tulip_stat_timer);
    }
}

#define DESC_STATUS(di)	(((volatile tulip_desc_t *)((di)->di_desc))->d_status)
#define DESC_FLAG(di)	((di)->di_desc->d_flag)

static void
tulip_rx_intr(tulip_softc_t * const sc)
{
    TULIP_PERFSTART(rxintr)
    tulip_ringinfo_t * const ri = &sc->tulip_rxinfo;
    struct ifnet * const ifp = sc->tulip_ifp;
    int fillok = 1;
#if defined(TULIP_DEBUG)
    int cnt = 0;
#endif

    TULIP_LOCK_ASSERT(sc);
    CTR0(KTR_TULIP, "tulip_rx_intr: start");
    for (;;) {
	TULIP_PERFSTART(rxget)
	tulip_descinfo_t *eop = ri->ri_nextin, *dip;
	int total_len = 0, last_offset = 0;
	struct mbuf *ms = NULL, *me = NULL;
	int accept = 0;
	int error;

	if (fillok && (ri->ri_max - ri->ri_free) < TULIP_RXQ_TARGET)
	    goto queue_mbuf;

#if defined(TULIP_DEBUG)
	if (cnt == ri->ri_max)
	    break;
#endif
	/*
	 * If the TULIP has no descriptors, there can't be any receive
	 * descriptors to process.
 	 */
	if (eop == ri->ri_nextout)
	    break;

	/*
	 * 90% of the packets will fit in one descriptor.  So we optimize
	 * for that case.
	 */
	TULIP_RXDESC_POSTSYNC(ri);
	if ((DESC_STATUS(eop) & (TULIP_DSTS_OWNER|TULIP_DSTS_RxFIRSTDESC|TULIP_DSTS_RxLASTDESC)) == (TULIP_DSTS_RxFIRSTDESC|TULIP_DSTS_RxLASTDESC)) {
	    ms = tulip_dequeue_mbuf(ri, eop, SYNC_RX);
	    CTR2(KTR_TULIP,
		"tulip_rx_intr: single packet mbuf %p from descriptor %td", ms,
		eop - ri->ri_first);
	    me = ms;
	    ri->ri_free++;
	} else {
	    /*
	     * If still owned by the TULIP, don't touch it.
	     */
	    if (DESC_STATUS(eop) & TULIP_DSTS_OWNER)
		break;

	    /*
	     * It is possible (though improbable unless MCLBYTES < 1518) for
	     * a received packet to cross more than one receive descriptor.
	     * We first loop through the descriptor ring making sure we have
	     * received a complete packet.  If not, we bail until the next
	     * interrupt.
	     */
	    dip = eop;
	    while ((DESC_STATUS(eop) & TULIP_DSTS_RxLASTDESC) == 0) {
		if (++eop == ri->ri_last)
		    eop = ri->ri_first;
		TULIP_RXDESC_POSTSYNC(ri);
		if (eop == ri->ri_nextout || DESC_STATUS(eop) & TULIP_DSTS_OWNER) {
#if defined(TULIP_DEBUG)
		    sc->tulip_dbg.dbg_rxintrs++;
		    sc->tulip_dbg.dbg_rxpktsperintr[cnt]++;
#endif
		    TULIP_PERFEND(rxget);
		    TULIP_PERFEND(rxintr);
		    return;
		}
		total_len++;
	    }

	    /*
	     * Dequeue the first buffer for the start of the packet.  Hopefully
	     * this will be the only one we need to dequeue.  However, if the
	     * packet consumed multiple descriptors, then we need to dequeue
	     * those buffers and chain to the starting mbuf.  All buffers but
	     * the last buffer have the same length so we can set that now.
	     * (we add to last_offset instead of multiplying since we normally
	     * won't go into the loop and thereby saving ourselves from
	     * doing a multiplication by 0 in the normal case).
	     */
	    ms = tulip_dequeue_mbuf(ri, dip, SYNC_RX);
	    CTR2(KTR_TULIP,
		"tulip_rx_intr: start packet mbuf %p from descriptor %td", ms,
		dip - ri->ri_first);
	    ri->ri_free++;
	    for (me = ms; total_len > 0; total_len--) {
		me->m_len = TULIP_RX_BUFLEN;
		last_offset += TULIP_RX_BUFLEN;
		if (++dip == ri->ri_last)
		    dip = ri->ri_first;
		me->m_next = tulip_dequeue_mbuf(ri, dip, SYNC_RX);
		ri->ri_free++;
		me = me->m_next;
		CTR2(KTR_TULIP,
		    "tulip_rx_intr: cont packet mbuf %p from descriptor %td",
		    me, dip - ri->ri_first);
	    }
	    KASSERT(dip == eop, ("mismatched descinfo structs"));
	}

	/*
	 *  Now get the size of received packet (minus the CRC).
	 */
	total_len = ((DESC_STATUS(eop) >> 16) & 0x7FFF) - ETHER_CRC_LEN;
	if ((sc->tulip_flags & TULIP_RXIGNORE) == 0
	    && ((DESC_STATUS(eop) & TULIP_DSTS_ERRSUM) == 0)) {
	    me->m_len = total_len - last_offset;
	    sc->tulip_flags |= TULIP_RXACT;
	    accept = 1;
	    CTR1(KTR_TULIP, "tulip_rx_intr: good packet; length %d",
		total_len);
	} else {
	    CTR1(KTR_TULIP, "tulip_rx_intr: bad packet; status %08x",
		DESC_STATUS(eop));
	    if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	    if (DESC_STATUS(eop) & (TULIP_DSTS_RxBADLENGTH|TULIP_DSTS_RxOVERFLOW|TULIP_DSTS_RxWATCHDOG)) {
		sc->tulip_dot3stats.dot3StatsInternalMacReceiveErrors++;
	    } else {
#if defined(TULIP_VERBOSE)
		const char *error = NULL;
#endif
		if (DESC_STATUS(eop) & TULIP_DSTS_RxTOOLONG) {
		    sc->tulip_dot3stats.dot3StatsFrameTooLongs++;
#if defined(TULIP_VERBOSE)
		    error = "frame too long";
#endif
		}
		if (DESC_STATUS(eop) & TULIP_DSTS_RxBADCRC) {
		    if (DESC_STATUS(eop) & TULIP_DSTS_RxDRBBLBIT) {
			sc->tulip_dot3stats.dot3StatsAlignmentErrors++;
#if defined(TULIP_VERBOSE)
			error = "alignment error";
#endif
		    } else {
			sc->tulip_dot3stats.dot3StatsFCSErrors++;
#if defined(TULIP_VERBOSE)
			error = "bad crc";
#endif
		    }
		}
#if defined(TULIP_VERBOSE)
		if (error != NULL && (sc->tulip_flags & TULIP_NOMESSAGES) == 0) {
		    device_printf(sc->tulip_dev, "receive: %6D: %s\n",
			   mtod(ms, u_char *) + 6, ":",
			   error);
		    sc->tulip_flags |= TULIP_NOMESSAGES;
		}
#endif
	    }

	}
#if defined(TULIP_DEBUG)
	cnt++;
#endif
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if (++eop == ri->ri_last)
	    eop = ri->ri_first;
	ri->ri_nextin = eop;
      queue_mbuf:
	/*
	 * We have received a good packet that needs to be passed up the
	 * stack.
	 */
	if (accept) {
	    struct mbuf *m0;

	    KASSERT(ms != NULL, ("no packet to accept"));
#ifndef	__NO_STRICT_ALIGNMENT
	    /*
	     * Copy the data into a new mbuf that is properly aligned.  If
	     * we fail to allocate a new mbuf, then drop the packet.  We will
	     * reuse the same rx buffer ('ms') below for another packet
	     * regardless.
	     */
	    m0 = m_devget(mtod(ms, caddr_t), total_len, ETHER_ALIGN, ifp, NULL);
	    if (m0 == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		goto skip_input;
	    }
#else
	    /*
	     * Update the header for the mbuf referencing this receive
	     * buffer and pass it up the stack.  Allocate a new mbuf cluster
	     * to replace the one we just passed up the stack.
	     *
	     * Note that if this packet crossed multiple descriptors
	     * we don't even try to reallocate all the mbufs here.
	     * Instead we rely on the test at the beginning of
	     * the loop to refill for the extra consumed mbufs.
	     */
	    ms->m_pkthdr.len = total_len;
	    ms->m_pkthdr.rcvif = ifp;
	    m0 = ms;
	    ms = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
#endif
	    TULIP_UNLOCK(sc);
	    CTR1(KTR_TULIP, "tulip_rx_intr: passing %p to upper layer", m0);
	    (*ifp->if_input)(ifp, m0);
	    TULIP_LOCK(sc);
	} else if (ms == NULL)
	    /*
	     * If we are priming the TULIP with mbufs, then allocate
	     * a new cluster for the next descriptor.
	     */
	    ms = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);

#ifndef __NO_STRICT_ALIGNMENT
    skip_input:
#endif
	if (ms == NULL) {
	    /*
	     * Couldn't allocate a new buffer.  Don't bother 
	     * trying to replenish the receive queue.
	     */
	    fillok = 0;
	    sc->tulip_flags |= TULIP_RXBUFSLOW;
#if defined(TULIP_DEBUG)
	    sc->tulip_dbg.dbg_rxlowbufs++;
#endif
	    TULIP_PERFEND(rxget);
	    continue;
	}
	/*
	 * Now give the buffer(s) to the TULIP and save in our
	 * receive queue.
	 */
	do {
	    tulip_descinfo_t * const nextout = ri->ri_nextout;

	    M_ASSERTPKTHDR(ms);
	    KASSERT(ms->m_data == ms->m_ext.ext_buf,
		("rx mbuf data doesn't point to cluster"));	    
	    ms->m_len = ms->m_pkthdr.len = TULIP_RX_BUFLEN;
	    error = bus_dmamap_load_mbuf(ri->ri_data_tag, *nextout->di_map, ms,
		tulip_dma_map_rxbuf, nextout->di_desc, BUS_DMA_NOWAIT);
	    if (error) {
		device_printf(sc->tulip_dev,
		    "unable to load rx map, error = %d\n", error);
		panic("tulip_rx_intr");		/* XXX */
	    }
	    nextout->di_desc->d_status = TULIP_DSTS_OWNER;
	    KASSERT(nextout->di_mbuf == NULL, ("clobbering earlier rx mbuf"));
	    nextout->di_mbuf = ms;
	    CTR2(KTR_TULIP, "tulip_rx_intr: enqueued mbuf %p to descriptor %td",
		ms, nextout - ri->ri_first);
	    TULIP_RXDESC_POSTSYNC(ri);
	    if (++ri->ri_nextout == ri->ri_last)
		ri->ri_nextout = ri->ri_first;
	    ri->ri_free--;
	    me = ms->m_next;
	    ms->m_next = NULL;
	} while ((ms = me) != NULL);

	if ((ri->ri_max - ri->ri_free) >= TULIP_RXQ_TARGET)
	    sc->tulip_flags &= ~TULIP_RXBUFSLOW;
	TULIP_PERFEND(rxget);
    }

#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_rxintrs++;
    sc->tulip_dbg.dbg_rxpktsperintr[cnt]++;
#endif
    TULIP_PERFEND(rxintr);
}

static int
tulip_tx_intr(tulip_softc_t * const sc)
{
    TULIP_PERFSTART(txintr)    
    tulip_ringinfo_t * const ri = &sc->tulip_txinfo;
    struct mbuf *m;
    int xmits = 0;
    int descs = 0;

    CTR0(KTR_TULIP, "tulip_tx_intr: start");
    TULIP_LOCK_ASSERT(sc);
    while (ri->ri_free < ri->ri_max) {
	u_int32_t d_flag;

	TULIP_TXDESC_POSTSYNC(ri);
	if (DESC_STATUS(ri->ri_nextin) & TULIP_DSTS_OWNER)
	    break;

	ri->ri_free++;
	descs++;
	d_flag = DESC_FLAG(ri->ri_nextin);
	if (d_flag & TULIP_DFLAG_TxLASTSEG) {
	    if (d_flag & TULIP_DFLAG_TxSETUPPKT) {
		CTR2(KTR_TULIP,
		    "tulip_tx_intr: setup packet from descriptor %td: %08x",
		    ri->ri_nextin - ri->ri_first, DESC_STATUS(ri->ri_nextin));
		/*
		 * We've just finished processing a setup packet.
		 * Mark that we finished it.  If there's not
		 * another pending, startup the TULIP receiver.
		 * Make sure we ack the RXSTOPPED so we won't get
		 * an abormal interrupt indication.
		 */
		bus_dmamap_sync(sc->tulip_setup_tag, sc->tulip_setup_map,
		    BUS_DMASYNC_POSTWRITE);
		sc->tulip_flags &= ~(TULIP_DOINGSETUP|TULIP_HASHONLY);
		if (DESC_FLAG(ri->ri_nextin) & TULIP_DFLAG_TxINVRSFILT)
		    sc->tulip_flags |= TULIP_HASHONLY;
		if ((sc->tulip_flags & (TULIP_WANTSETUP|TULIP_TXPROBE_ACTIVE)) == 0) {
		    tulip_rx_intr(sc);
		    sc->tulip_cmdmode |= TULIP_CMD_RXRUN;
		    sc->tulip_intrmask |= TULIP_STS_RXSTOPPED;
		    CTR2(KTR_TULIP,
			"tulip_tx_intr: intr mask %08x  cmdmode %08x",
			sc->tulip_intrmask, sc->tulip_cmdmode);
		    TULIP_CSR_WRITE(sc, csr_status, TULIP_STS_RXSTOPPED);
		    TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
		    TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
		}
	    } else {
		const u_int32_t d_status = DESC_STATUS(ri->ri_nextin);

		m = tulip_dequeue_mbuf(ri, ri->ri_nextin, SYNC_TX);
		CTR2(KTR_TULIP,
		    "tulip_tx_intr: data packet %p from descriptor %td", m,
		    ri->ri_nextin - ri->ri_first);
		if (m != NULL) {
		    m_freem(m);
#if defined(TULIP_DEBUG)
		} else {
		    device_printf(sc->tulip_dev,
		        "tx_intr: failed to dequeue mbuf?!?\n");
#endif
		}
		if (sc->tulip_flags & TULIP_TXPROBE_ACTIVE) {
		    tulip_mediapoll_event_t event = TULIP_MEDIAPOLL_TXPROBE_OK;
		    if (d_status & (TULIP_DSTS_TxNOCARR|TULIP_DSTS_TxEXCCOLL)) {
#if defined(TULIP_DEBUG)
			if (d_status & TULIP_DSTS_TxNOCARR)
			    sc->tulip_dbg.dbg_txprobe_nocarr++;
			if (d_status & TULIP_DSTS_TxEXCCOLL)
			    sc->tulip_dbg.dbg_txprobe_exccoll++;
#endif
			event = TULIP_MEDIAPOLL_TXPROBE_FAILED;
		    }
		    (*sc->tulip_boardsw->bd_media_poll)(sc, event);
		    /*
		     * Escape from the loop before media poll has reset the TULIP!
		     */
		    break;
		} else {
		    xmits++;
		    if (d_status & TULIP_DSTS_ERRSUM) {
			CTR1(KTR_TULIP, "tulip_tx_intr: output error: %08x",
			    d_status);
			if_inc_counter(sc->tulip_ifp, IFCOUNTER_OERRORS, 1);
			if (d_status & TULIP_DSTS_TxEXCCOLL)
			    sc->tulip_dot3stats.dot3StatsExcessiveCollisions++;
			if (d_status & TULIP_DSTS_TxLATECOLL)
			    sc->tulip_dot3stats.dot3StatsLateCollisions++;
			if (d_status & (TULIP_DSTS_TxNOCARR|TULIP_DSTS_TxCARRLOSS))
			    sc->tulip_dot3stats.dot3StatsCarrierSenseErrors++;
			if (d_status & (TULIP_DSTS_TxUNDERFLOW|TULIP_DSTS_TxBABBLE))
			    sc->tulip_dot3stats.dot3StatsInternalMacTransmitErrors++;
			if (d_status & TULIP_DSTS_TxUNDERFLOW)
			    sc->tulip_dot3stats.dot3StatsInternalTransmitUnderflows++;
			if (d_status & TULIP_DSTS_TxBABBLE)
			    sc->tulip_dot3stats.dot3StatsInternalTransmitBabbles++;
		    } else {
			u_int32_t collisions = 
			    (d_status & TULIP_DSTS_TxCOLLMASK)
				>> TULIP_DSTS_V_TxCOLLCNT;

			CTR2(KTR_TULIP,
		    "tulip_tx_intr: output ok, collisions %d, status %08x",
			    collisions, d_status);
			if_inc_counter(sc->tulip_ifp, IFCOUNTER_COLLISIONS, collisions);
			if (collisions == 1)
			    sc->tulip_dot3stats.dot3StatsSingleCollisionFrames++;
			else if (collisions > 1)
			    sc->tulip_dot3stats.dot3StatsMultipleCollisionFrames++;
			else if (d_status & TULIP_DSTS_TxDEFERRED)
			    sc->tulip_dot3stats.dot3StatsDeferredTransmissions++;
			/*
			 * SQE is only valid for 10baseT/BNC/AUI when not
			 * running in full-duplex.  In order to speed up the
			 * test, the corresponding bit in tulip_flags needs to
			 * set as well to get us to count SQE Test Errors.
			 */
			if (d_status & TULIP_DSTS_TxNOHRTBT & sc->tulip_flags)
			    sc->tulip_dot3stats.dot3StatsSQETestErrors++;
		    }
		}
	    }
	}

	if (++ri->ri_nextin == ri->ri_last)
	    ri->ri_nextin = ri->ri_first;

	if ((sc->tulip_flags & TULIP_TXPROBE_ACTIVE) == 0)
	    sc->tulip_ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
    }
    /*
     * If nothing left to transmit, disable the timer.
     * Else if progress, reset the timer back to 2 ticks.
     */
    if (ri->ri_free == ri->ri_max || (sc->tulip_flags & TULIP_TXPROBE_ACTIVE))
	sc->tulip_txtimer = 0;
    else if (xmits > 0)
	sc->tulip_txtimer = TULIP_TXTIMER;
    if_inc_counter(sc->tulip_ifp, IFCOUNTER_OPACKETS, xmits);
    TULIP_PERFEND(txintr);
    return descs;
}

static void
tulip_print_abnormal_interrupt(tulip_softc_t * const sc, u_int32_t csr)
{
    const char * const *msgp = tulip_status_bits;
    const char *sep;
    u_int32_t mask;
    const char thrsh[] = "72|128\0\0\0" "96|256\0\0\0" "128|512\0\0" "160|1024";

    TULIP_LOCK_ASSERT(sc);
    csr &= (1 << (sizeof(tulip_status_bits)/sizeof(tulip_status_bits[0]))) - 1;
    device_printf(sc->tulip_dev, "abnormal interrupt:");
    for (sep = " ", mask = 1; mask <= csr; mask <<= 1, msgp++) {
	if ((csr & mask) && *msgp != NULL) {
	    printf("%s%s", sep, *msgp);
	    if (mask == TULIP_STS_TXUNDERFLOW && (sc->tulip_flags & TULIP_NEWTXTHRESH)) {
		sc->tulip_flags &= ~TULIP_NEWTXTHRESH;
		if (sc->tulip_cmdmode & TULIP_CMD_STOREFWD) {
		    printf(" (switching to store-and-forward mode)");
		} else {
		    printf(" (raising TX threshold to %s)",
			   &thrsh[9 * ((sc->tulip_cmdmode & TULIP_CMD_THRESHOLDCTL) >> 14)]);
		}
	    }
	    sep = ", ";
	}
    }
    printf("\n");
}

static void
tulip_intr_handler(tulip_softc_t * const sc)
{
    TULIP_PERFSTART(intr)
    u_int32_t csr;

    CTR0(KTR_TULIP, "tulip_intr_handler invoked");
    TULIP_LOCK_ASSERT(sc);
    while ((csr = TULIP_CSR_READ(sc, csr_status)) & sc->tulip_intrmask) {
	TULIP_CSR_WRITE(sc, csr_status, csr);

	if (csr & TULIP_STS_SYSERROR) {
	    sc->tulip_last_system_error = (csr & TULIP_STS_ERRORMASK) >> TULIP_STS_ERR_SHIFT;
	    if (sc->tulip_flags & TULIP_NOMESSAGES) {
		sc->tulip_flags |= TULIP_SYSTEMERROR;
	    } else {
		device_printf(sc->tulip_dev, "system error: %s\n",
		       tulip_system_errors[sc->tulip_last_system_error]);
	    }
	    sc->tulip_flags |= TULIP_NEEDRESET;
	    sc->tulip_system_errors++;
	    break;
	}
	if (csr & (TULIP_STS_LINKPASS|TULIP_STS_LINKFAIL) & sc->tulip_intrmask) {
#if defined(TULIP_DEBUG)
	    sc->tulip_dbg.dbg_link_intrs++;
#endif
	    if (sc->tulip_boardsw->bd_media_poll != NULL) {
		(*sc->tulip_boardsw->bd_media_poll)(sc, csr & TULIP_STS_LINKFAIL
						    ? TULIP_MEDIAPOLL_LINKFAIL
						    : TULIP_MEDIAPOLL_LINKPASS);
		csr &= ~TULIP_STS_ABNRMLINTR;
	    }
	    tulip_media_print(sc);
	}
	if (csr & (TULIP_STS_RXINTR|TULIP_STS_RXNOBUF)) {
	    u_int32_t misses = TULIP_CSR_READ(sc, csr_missed_frames);
	    if (csr & TULIP_STS_RXNOBUF)
		sc->tulip_dot3stats.dot3StatsMissedFrames += misses & 0xFFFF;
	    /*
	     * Pass 2.[012] of the 21140A-A[CDE] may hang and/or corrupt data
	     * on receive overflows.
	     */
	    if ((misses & 0x0FFE0000) && (sc->tulip_features & TULIP_HAVE_RXBADOVRFLW)) {
		sc->tulip_dot3stats.dot3StatsInternalMacReceiveErrors++;
		/*
		 * Stop the receiver process and spin until it's stopped.
		 * Tell rx_intr to drop the packets it dequeues.
		 */
		TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode & ~TULIP_CMD_RXRUN);
		while ((TULIP_CSR_READ(sc, csr_status) & TULIP_STS_RXSTOPPED) == 0)
		    ;
		TULIP_CSR_WRITE(sc, csr_status, TULIP_STS_RXSTOPPED);
		sc->tulip_flags |= TULIP_RXIGNORE;
	    }
	    tulip_rx_intr(sc);
	    if (sc->tulip_flags & TULIP_RXIGNORE) {
		/*
		 * Restart the receiver.
		 */
		sc->tulip_flags &= ~TULIP_RXIGNORE;
		TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
	    }
	}
	if (csr & TULIP_STS_ABNRMLINTR) {
	    u_int32_t tmp = csr & sc->tulip_intrmask
		& ~(TULIP_STS_NORMALINTR|TULIP_STS_ABNRMLINTR);
	    if (csr & TULIP_STS_TXUNDERFLOW) {
		if ((sc->tulip_cmdmode & TULIP_CMD_THRESHOLDCTL) != TULIP_CMD_THRSHLD160) {
		    sc->tulip_cmdmode += TULIP_CMD_THRSHLD96;
		    sc->tulip_flags |= TULIP_NEWTXTHRESH;
		} else if (sc->tulip_features & TULIP_HAVE_STOREFWD) {
		    sc->tulip_cmdmode |= TULIP_CMD_STOREFWD;
		    sc->tulip_flags |= TULIP_NEWTXTHRESH;
		}
	    }
	    if (sc->tulip_flags & TULIP_NOMESSAGES) {
		sc->tulip_statusbits |= tmp;
	    } else {
		tulip_print_abnormal_interrupt(sc, tmp);
		sc->tulip_flags |= TULIP_NOMESSAGES;
	    }
	    TULIP_CSR_WRITE(sc, csr_command, sc->tulip_cmdmode);
	}
	if (sc->tulip_flags & (TULIP_WANTTXSTART|TULIP_TXPROBE_ACTIVE|TULIP_DOINGSETUP|TULIP_PROMISC)) {
	    tulip_tx_intr(sc);
	    if ((sc->tulip_flags & TULIP_TXPROBE_ACTIVE) == 0)
		tulip_start_locked(sc);
	}
    }
    if (sc->tulip_flags & TULIP_NEEDRESET) {
	tulip_reset(sc);
	tulip_init_locked(sc);
    }
    TULIP_PERFEND(intr);
}

static void
tulip_intr_shared(void *arg)
{
    tulip_softc_t * sc = arg;

    for (; sc != NULL; sc = sc->tulip_slaves) {
	TULIP_LOCK(sc);
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_intrs++;
#endif
	tulip_intr_handler(sc);
	TULIP_UNLOCK(sc);
    }
}

static void
tulip_intr_normal(void *arg)
{
    tulip_softc_t * sc = (tulip_softc_t *) arg;

    TULIP_LOCK(sc);
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_intrs++;
#endif
    tulip_intr_handler(sc);
    TULIP_UNLOCK(sc);
}

static struct mbuf *
tulip_txput(tulip_softc_t * const sc, struct mbuf *m)
{
    TULIP_PERFSTART(txput)
    tulip_ringinfo_t * const ri = &sc->tulip_txinfo;
    tulip_descinfo_t *eop, *nextout;
    int segcnt, free;
    u_int32_t d_status;
    bus_dma_segment_t segs[TULIP_MAX_TXSEG];
    bus_dmamap_t *map;
    int error, nsegs;
    struct mbuf *m0;

    TULIP_LOCK_ASSERT(sc);
#if defined(TULIP_DEBUG)
    if ((sc->tulip_cmdmode & TULIP_CMD_TXRUN) == 0) {
	device_printf(sc->tulip_dev, "txput%s: tx not running\n",
	       (sc->tulip_flags & TULIP_TXPROBE_ACTIVE) ? "(probe)" : "");
	sc->tulip_flags |= TULIP_WANTTXSTART;
	sc->tulip_dbg.dbg_txput_finishes[0]++;
	goto finish;
    }
#endif

    /*
     * Now we try to fill in our transmit descriptors.  This is
     * a bit reminiscent of going on the Ark two by two
     * since each descriptor for the TULIP can describe
     * two buffers.  So we advance through packet filling
     * each of the two entries at a time to fill each
     * descriptor.  Clear the first and last segment bits
     * in each descriptor (actually just clear everything
     * but the end-of-ring or chain bits) to make sure
     * we don't get messed up by previously sent packets.
     *
     * We may fail to put the entire packet on the ring if
     * there is either not enough ring entries free or if the
     * packet has more than MAX_TXSEG segments.  In the former
     * case we will just wait for the ring to empty.  In the
     * latter case we have to recopy.
     */
#if defined(KTR) && KTR_TULIP
    segcnt = 1;
    m0 = m;
    while (m0->m_next != NULL) {
	    segcnt++;
	    m0 = m0->m_next;
    }
    CTR2(KTR_TULIP, "tulip_txput: sending packet %p (%d chunks)", m, segcnt);
#endif
    d_status = 0;
    eop = nextout = ri->ri_nextout;
    segcnt = 0;
    free = ri->ri_free;

    /*
     * Reclaim some tx descriptors if we are out since we need at least one
     * free descriptor so that we have a dma_map to load the mbuf.
     */
    if (free == 0) {
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_no_txmaps++;
#endif
	free += tulip_tx_intr(sc);
    }
    if (free == 0) {
	sc->tulip_flags |= TULIP_WANTTXSTART;
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_txput_finishes[1]++;
#endif
	goto finish;
    }
    error = bus_dmamap_load_mbuf_sg(ri->ri_data_tag, *eop->di_map, m, segs,
	&nsegs, BUS_DMA_NOWAIT);
    if (error != 0) {
	if (error == EFBIG) {
	    /*
	     * The packet exceeds the number of transmit buffer
	     * entries that we can use for one packet, so we have
	     * to recopy it into one mbuf and then try again.  If
	     * we can't recopy it, try again later.
	     */
	    m0 = m_defrag(m, M_NOWAIT);
	    if (m0 == NULL) {
		sc->tulip_flags |= TULIP_WANTTXSTART;
#if defined(TULIP_DEBUG)
		sc->tulip_dbg.dbg_txput_finishes[2]++;
#endif
		goto finish;
	    }
	    m = m0;
	    error = bus_dmamap_load_mbuf_sg(ri->ri_data_tag, *eop->di_map, m,
		segs, &nsegs, BUS_DMA_NOWAIT);
	}
	if (error != 0) {
	    device_printf(sc->tulip_dev,
	        "unable to load tx map, error = %d\n", error);
#if defined(TULIP_DEBUG)
	    sc->tulip_dbg.dbg_txput_finishes[3]++;
#endif
	    goto finish;
	}
    }
    CTR1(KTR_TULIP, "tulip_txput: nsegs %d", nsegs);

    /*
     * Each descriptor allows for up to 2 fragments since we don't use
     * the descriptor chaining mode in this driver.
     */
    if ((free -= (nsegs + 1) / 2) <= 0
	    /*
	     * See if there's any unclaimed space in the transmit ring.
	     */
	    && (free += tulip_tx_intr(sc)) <= 0) {
	/*
	 * There's no more room but since nothing
	 * has been committed at this point, just
	 * show output is active, put back the
	 * mbuf and return.
	 */
	sc->tulip_flags |= TULIP_WANTTXSTART;
#if defined(TULIP_DEBUG)
	sc->tulip_dbg.dbg_txput_finishes[4]++;
#endif
	bus_dmamap_unload(ri->ri_data_tag, *eop->di_map);
	goto finish;
    }
    for (; nsegs - segcnt > 1; segcnt += 2) {
	eop = nextout;
	eop->di_desc->d_flag   &= TULIP_DFLAG_ENDRING|TULIP_DFLAG_CHAIN;
	eop->di_desc->d_status  = d_status;
	eop->di_desc->d_addr1   = segs[segcnt].ds_addr & 0xffffffff;
	eop->di_desc->d_length1 = segs[segcnt].ds_len;
	eop->di_desc->d_addr2   = segs[segcnt+1].ds_addr & 0xffffffff;
	eop->di_desc->d_length2 = segs[segcnt+1].ds_len;
	d_status = TULIP_DSTS_OWNER;
	if (++nextout == ri->ri_last)
	    nextout = ri->ri_first;
    }
    if (segcnt < nsegs) {
	eop = nextout;
	eop->di_desc->d_flag   &= TULIP_DFLAG_ENDRING|TULIP_DFLAG_CHAIN;
	eop->di_desc->d_status  = d_status;
	eop->di_desc->d_addr1   = segs[segcnt].ds_addr & 0xffffffff;
	eop->di_desc->d_length1 = segs[segcnt].ds_len;
	eop->di_desc->d_addr2   = 0;
	eop->di_desc->d_length2 = 0;
	if (++nextout == ri->ri_last)
	    nextout = ri->ri_first;
    }

    /*
     * tulip_tx_intr() harvests the mbuf from the last descriptor in the
     * frame.  We just used the dmamap in the first descriptor for the
     * load operation however.  Thus, to let the tulip_dequeue_mbuf() call
     * in tulip_tx_intr() unload the correct dmamap, we swap the dmamap
     * pointers in the two descriptors if this is a multiple-descriptor
     * packet.
     */
    if (eop != ri->ri_nextout) {
	    map = eop->di_map;
	    eop->di_map = ri->ri_nextout->di_map;
	    ri->ri_nextout->di_map = map;
    }

    /*
     * bounce a copy to the bpf listener, if any.
     */
    if (!(sc->tulip_flags & TULIP_DEVICEPROBE))
	    BPF_MTAP(sc->tulip_ifp, m);

    /*
     * The descriptors have been filled in.  Now get ready
     * to transmit.
     */
    CTR3(KTR_TULIP, "tulip_txput: enqueued mbuf %p to descriptors %td - %td",
	m, ri->ri_nextout - ri->ri_first, eop - ri->ri_first);
    KASSERT(eop->di_mbuf == NULL, ("clobbering earlier tx mbuf"));
    eop->di_mbuf = m;
    TULIP_TXMAP_PRESYNC(ri, ri->ri_nextout);
    m = NULL;

    /*
     * Make sure the next descriptor after this packet is owned
     * by us since it may have been set up above if we ran out
     * of room in the ring.
     */
    nextout->di_desc->d_status = 0;
    TULIP_TXDESC_PRESYNC(ri);

    /*
     * Mark the last and first segments, indicate we want a transmit
     * complete interrupt, and tell it to transmit!
     */
    eop->di_desc->d_flag |= TULIP_DFLAG_TxLASTSEG|TULIP_DFLAG_TxWANTINTR;

    /*
     * Note that ri->ri_nextout is still the start of the packet
     * and until we set the OWNER bit, we can still back out of
     * everything we have done.
     */
    ri->ri_nextout->di_desc->d_flag |= TULIP_DFLAG_TxFIRSTSEG;
    TULIP_TXDESC_PRESYNC(ri);
    ri->ri_nextout->di_desc->d_status = TULIP_DSTS_OWNER;
    TULIP_TXDESC_PRESYNC(ri);

    /*
     * This advances the ring for us.
     */
    ri->ri_nextout = nextout;
    ri->ri_free = free;

    TULIP_PERFEND(txput);

    if (sc->tulip_flags & TULIP_TXPROBE_ACTIVE) {
	TULIP_CSR_WRITE(sc, csr_txpoll, 1);
	sc->tulip_ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	TULIP_PERFEND(txput);
	return NULL;
    }

    /*
     * switch back to the single queueing ifstart.
     */
    sc->tulip_flags &= ~TULIP_WANTTXSTART;
    if (sc->tulip_txtimer == 0)
	sc->tulip_txtimer = TULIP_TXTIMER;
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_txput_finishes[5]++;
#endif

    /*
     * If we want a txstart, there must be not enough space in the
     * transmit ring.  So we want to enable transmit done interrupts
     * so we can immediately reclaim some space.  When the transmit
     * interrupt is posted, the interrupt handler will call tx_intr
     * to reclaim space and then txstart (since WANTTXSTART is set).
     * txstart will move the packet into the transmit ring and clear
     * WANTTXSTART thereby causing TXINTR to be cleared.
     */
  finish:
#if defined(TULIP_DEBUG)
    sc->tulip_dbg.dbg_txput_finishes[6]++;
#endif
    if (sc->tulip_flags & (TULIP_WANTTXSTART|TULIP_DOINGSETUP)) {
	sc->tulip_ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	if ((sc->tulip_intrmask & TULIP_STS_TXINTR) == 0) {
	    sc->tulip_intrmask |= TULIP_STS_TXINTR;
	    TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
	}
    } else if ((sc->tulip_flags & TULIP_PROMISC) == 0) {
	if (sc->tulip_intrmask & TULIP_STS_TXINTR) {
	    sc->tulip_intrmask &= ~TULIP_STS_TXINTR;
	    TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
	}
    }
    TULIP_CSR_WRITE(sc, csr_txpoll, 1);
    TULIP_PERFEND(txput);
    return m;
}

static void
tulip_txput_setup(tulip_softc_t * const sc)
{
    tulip_ringinfo_t * const ri = &sc->tulip_txinfo;
    tulip_desc_t *nextout;

    TULIP_LOCK_ASSERT(sc);

    /*
     * We will transmit, at most, one setup packet per call to ifstart.
     */

#if defined(TULIP_DEBUG)
    if ((sc->tulip_cmdmode & TULIP_CMD_TXRUN) == 0) {
	device_printf(sc->tulip_dev, "txput_setup: tx not running\n");
	sc->tulip_flags |= TULIP_WANTTXSTART;
	return;
    }
#endif
    /*
     * Try to reclaim some free descriptors..
     */
    if (ri->ri_free < 2)
	tulip_tx_intr(sc);
    if ((sc->tulip_flags & TULIP_DOINGSETUP) || ri->ri_free == 1) {
	sc->tulip_flags |= TULIP_WANTTXSTART;
	return;
    }
    bcopy(sc->tulip_setupdata, sc->tulip_setupbuf,
	  sizeof(sc->tulip_setupdata));
    /*
     * Clear WANTSETUP and set DOINGSETUP.  Since we know that WANTSETUP is
     * set and DOINGSETUP is clear doing an XOR of the two will DTRT.
     */
    sc->tulip_flags ^= TULIP_WANTSETUP|TULIP_DOINGSETUP;
    ri->ri_free--;
    nextout = ri->ri_nextout->di_desc;
    nextout->d_flag &= TULIP_DFLAG_ENDRING|TULIP_DFLAG_CHAIN;
    nextout->d_flag |= TULIP_DFLAG_TxFIRSTSEG|TULIP_DFLAG_TxLASTSEG
	|TULIP_DFLAG_TxSETUPPKT|TULIP_DFLAG_TxWANTINTR;
    if (sc->tulip_flags & TULIP_WANTHASHPERFECT)
	nextout->d_flag |= TULIP_DFLAG_TxHASHFILT;
    else if (sc->tulip_flags & TULIP_WANTHASHONLY)
	nextout->d_flag |= TULIP_DFLAG_TxHASHFILT|TULIP_DFLAG_TxINVRSFILT;

    nextout->d_length2 = 0;
    nextout->d_addr2 = 0;
    nextout->d_length1 = sizeof(sc->tulip_setupdata);
    nextout->d_addr1 = sc->tulip_setup_dma_addr & 0xffffffff;
    bus_dmamap_sync(sc->tulip_setup_tag, sc->tulip_setup_map,
	BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
    TULIP_TXDESC_PRESYNC(ri);
    CTR1(KTR_TULIP, "tulip_txput_setup: using descriptor %td",
	ri->ri_nextout - ri->ri_first);

    /*
     * Advance the ring for the next transmit packet.
     */
    if (++ri->ri_nextout == ri->ri_last)
	ri->ri_nextout = ri->ri_first;

    /*
     * Make sure the next descriptor is owned by us since it
     * may have been set up above if we ran out of room in the
     * ring.
     */
    ri->ri_nextout->di_desc->d_status = 0;
    TULIP_TXDESC_PRESYNC(ri);
    nextout->d_status = TULIP_DSTS_OWNER;
    /*
     * Flush the ownwership of the current descriptor
     */
    TULIP_TXDESC_PRESYNC(ri);
    TULIP_CSR_WRITE(sc, csr_txpoll, 1);
    if ((sc->tulip_intrmask & TULIP_STS_TXINTR) == 0) {
	sc->tulip_intrmask |= TULIP_STS_TXINTR;
	TULIP_CSR_WRITE(sc, csr_intr, sc->tulip_intrmask);
    }
}

static int
tulip_ifioctl(struct ifnet * ifp, u_long cmd, caddr_t data)
{
    TULIP_PERFSTART(ifioctl)
    tulip_softc_t * const sc = (tulip_softc_t *)ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *) data;
    int error = 0;

    switch (cmd) {
	case SIOCSIFFLAGS: {
	    TULIP_LOCK(sc);
	    tulip_init_locked(sc);
	    TULIP_UNLOCK(sc);
	    break;
	}

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA: {
	    error = ifmedia_ioctl(ifp, ifr, &sc->tulip_ifmedia, cmd);
	    break;
	}

	case SIOCADDMULTI:
	case SIOCDELMULTI: {
	    /*
	     * Update multicast listeners
	     */
	    TULIP_LOCK(sc);
	    tulip_init_locked(sc);
	    TULIP_UNLOCK(sc);
	    error = 0;
	    break;
	}

	default: {
	    error = ether_ioctl(ifp, cmd, data);
	    break;
	}
    }

    TULIP_PERFEND(ifioctl);
    return error;
}

static void
tulip_start(struct ifnet * const ifp)
{
    TULIP_PERFSTART(ifstart)
    tulip_softc_t * const sc = (tulip_softc_t *)ifp->if_softc;

    TULIP_LOCK(sc);
    tulip_start_locked(sc);
    TULIP_UNLOCK(sc);

    TULIP_PERFEND(ifstart);
}

static void
tulip_start_locked(tulip_softc_t * const sc)
{
    struct mbuf *m;

    TULIP_LOCK_ASSERT(sc);

    CTR0(KTR_TULIP, "tulip_start_locked invoked");
    if ((sc->tulip_flags & (TULIP_WANTSETUP|TULIP_TXPROBE_ACTIVE)) == TULIP_WANTSETUP)
	tulip_txput_setup(sc);

    CTR1(KTR_TULIP, "tulip_start_locked: %d tx packets pending",
	sc->tulip_ifp->if_snd.ifq_len);
    while (!IFQ_DRV_IS_EMPTY(&sc->tulip_ifp->if_snd)) {
	IFQ_DRV_DEQUEUE(&sc->tulip_ifp->if_snd, m);
	if(m == NULL)
	    break;
	if ((m = tulip_txput(sc, m)) != NULL) {
	    IFQ_DRV_PREPEND(&sc->tulip_ifp->if_snd, m);
	    break;
	}
    }
}

static void
tulip_watchdog(void *arg)
{
    TULIP_PERFSTART(stat)
    tulip_softc_t *sc = arg;
#if defined(TULIP_DEBUG)
    u_int32_t rxintrs;
#endif

    TULIP_LOCK_ASSERT(sc);
    callout_reset(&sc->tulip_stat_timer, hz, tulip_watchdog, sc);    
#if defined(TULIP_DEBUG)
    rxintrs = sc->tulip_dbg.dbg_rxintrs - sc->tulip_dbg.dbg_last_rxintrs;
    if (rxintrs > sc->tulip_dbg.dbg_high_rxintrs_hz)
	sc->tulip_dbg.dbg_high_rxintrs_hz = rxintrs;
    sc->tulip_dbg.dbg_last_rxintrs = sc->tulip_dbg.dbg_rxintrs;
#endif /* TULIP_DEBUG */

    /*
     * These should be rare so do a bulk test up front so we can just skip
     * them if needed.
     */
    if (sc->tulip_flags & (TULIP_SYSTEMERROR|TULIP_RXBUFSLOW|TULIP_NOMESSAGES)) {
	/*
	 * If the number of receive buffer is low, try to refill
	 */
	if (sc->tulip_flags & TULIP_RXBUFSLOW)
	    tulip_rx_intr(sc);

	if (sc->tulip_flags & TULIP_SYSTEMERROR) {
	    if_printf(sc->tulip_ifp, "%d system errors: last was %s\n",
		   sc->tulip_system_errors,
		   tulip_system_errors[sc->tulip_last_system_error]);
	}
	if (sc->tulip_statusbits) {
	    tulip_print_abnormal_interrupt(sc, sc->tulip_statusbits);
	    sc->tulip_statusbits = 0;
	}

	sc->tulip_flags &= ~(TULIP_NOMESSAGES|TULIP_SYSTEMERROR);
    }

    if (sc->tulip_txtimer)
	tulip_tx_intr(sc);
    if (sc->tulip_txtimer && --sc->tulip_txtimer == 0) {
	if_printf(sc->tulip_ifp, "transmission timeout\n");
	if (TULIP_DO_AUTOSENSE(sc)) {
	    sc->tulip_media = TULIP_MEDIA_UNKNOWN;
	    sc->tulip_probe_state = TULIP_PROBE_INACTIVE;
	    sc->tulip_flags &= ~(TULIP_WANTRXACT|TULIP_LINKUP);
	}
	tulip_reset(sc);
	tulip_init_locked(sc);
    }

    TULIP_PERFEND(stat);
    TULIP_PERFMERGE(sc, perf_intr_cycles);
    TULIP_PERFMERGE(sc, perf_ifstart_cycles);
    TULIP_PERFMERGE(sc, perf_ifioctl_cycles);
    TULIP_PERFMERGE(sc, perf_stat_cycles);
    TULIP_PERFMERGE(sc, perf_timeout_cycles);
    TULIP_PERFMERGE(sc, perf_ifstart_one_cycles);
    TULIP_PERFMERGE(sc, perf_txput_cycles);
    TULIP_PERFMERGE(sc, perf_txintr_cycles);
    TULIP_PERFMERGE(sc, perf_rxintr_cycles);
    TULIP_PERFMERGE(sc, perf_rxget_cycles);
    TULIP_PERFMERGE(sc, perf_intr);
    TULIP_PERFMERGE(sc, perf_ifstart);
    TULIP_PERFMERGE(sc, perf_ifioctl);
    TULIP_PERFMERGE(sc, perf_stat);
    TULIP_PERFMERGE(sc, perf_timeout);
    TULIP_PERFMERGE(sc, perf_ifstart_one);
    TULIP_PERFMERGE(sc, perf_txput);
    TULIP_PERFMERGE(sc, perf_txintr);
    TULIP_PERFMERGE(sc, perf_rxintr);
    TULIP_PERFMERGE(sc, perf_rxget);
}

static void
tulip_attach(tulip_softc_t * const sc)
{
    struct ifnet *ifp;

    ifp = sc->tulip_ifp = if_alloc(IFT_ETHER);

    /* XXX: driver name/unit should be set some other way */
    if_initname(ifp, "de", sc->tulip_unit);
    ifp->if_softc = sc;
    ifp->if_flags = IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST;
    ifp->if_ioctl = tulip_ifioctl;
    ifp->if_start = tulip_start;
    ifp->if_init = tulip_init;
    IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
    ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
    IFQ_SET_READY(&ifp->if_snd);
  
    device_printf(sc->tulip_dev, "%s%s pass %d.%d%s\n",
	   sc->tulip_boardid,
	   tulip_chipdescs[sc->tulip_chipid],
	   (sc->tulip_revinfo & 0xF0) >> 4,
	   sc->tulip_revinfo & 0x0F,
	   (sc->tulip_features & (TULIP_HAVE_ISVSROM|TULIP_HAVE_OKSROM))
		 == TULIP_HAVE_ISVSROM ? " (invalid EESPROM checksum)" : "");

    TULIP_LOCK(sc);
    (*sc->tulip_boardsw->bd_media_probe)(sc);
    ifmedia_init(&sc->tulip_ifmedia, 0,
		 tulip_ifmedia_change,
		 tulip_ifmedia_status);
    tulip_ifmedia_add(sc);

    tulip_reset(sc);
    TULIP_UNLOCK(sc);

    ether_ifattach(sc->tulip_ifp, sc->tulip_enaddr);

    TULIP_LOCK(sc);
    sc->tulip_flags &= ~TULIP_DEVICEPROBE;
    TULIP_UNLOCK(sc);

    gone_by_fcp101_dev(sc->tulip_dev);
}

/* Release memory for a single descriptor ring. */
static void
tulip_busdma_freering(tulip_ringinfo_t *ri)
{
    int i;

    /* Release the DMA maps and tag for data buffers. */
    if (ri->ri_data_maps != NULL) {
	for (i = 0; i < ri->ri_max; i++) {
	    if (ri->ri_data_maps[i] != NULL) {
		bus_dmamap_destroy(ri->ri_data_tag, ri->ri_data_maps[i]);
		ri->ri_data_maps[i] = NULL;
	    }
	}
	free(ri->ri_data_maps, M_DEVBUF);
	ri->ri_data_maps = NULL;
    }
    if (ri->ri_data_tag != NULL) {
	bus_dma_tag_destroy(ri->ri_data_tag);
	ri->ri_data_tag = NULL;
    }

    /* Release the DMA memory and tag for the ring descriptors. */
    if (ri->ri_dma_addr != 0) {
	bus_dmamap_unload(ri->ri_ring_tag, ri->ri_ring_map);
	ri->ri_dma_addr = 0;
    }
    if (ri->ri_descs != NULL) {
	bus_dmamem_free(ri->ri_ring_tag, ri->ri_descs, ri->ri_ring_map);
	ri->ri_descs = NULL;
    }
    if (ri->ri_ring_tag != NULL) {
	bus_dma_tag_destroy(ri->ri_ring_tag);
	ri->ri_ring_tag = NULL;
    }
}

/* Allocate memory for a single descriptor ring. */
static int
tulip_busdma_allocring(device_t dev, tulip_softc_t * const sc, size_t count,
    bus_size_t align, int nsegs, tulip_ringinfo_t *ri, const char *name)
{
    size_t size;
    int error, i;

    /* First, setup a tag. */
    ri->ri_max = count;
    size = count * sizeof(tulip_desc_t);
    error = bus_dma_tag_create(bus_get_dma_tag(dev),
	32, 0, BUS_SPACE_MAXADDR_32BIT,
	BUS_SPACE_MAXADDR, NULL, NULL, size, 1, size, 0, NULL, NULL,
	&ri->ri_ring_tag);
    if (error) {
	device_printf(dev, "failed to allocate %s descriptor ring dma tag\n",
	    name);
	return (error);
    }

    /* Next, allocate memory for the descriptors. */
    error = bus_dmamem_alloc(ri->ri_ring_tag, (void **)&ri->ri_descs,
	BUS_DMA_NOWAIT | BUS_DMA_ZERO, &ri->ri_ring_map);
    if (error) {
	device_printf(dev, "failed to allocate memory for %s descriptor ring\n",
	    name);
	return (error);
    }

    /* Map the descriptors. */
    error = bus_dmamap_load(ri->ri_ring_tag, ri->ri_ring_map, ri->ri_descs,
	size, tulip_dma_map_addr, &ri->ri_dma_addr, BUS_DMA_NOWAIT);
    if (error) {
	device_printf(dev, "failed to get dma address for %s descriptor ring\n",
	    name);
	return (error);
    }

    /* Allocate a tag for the data buffers. */
    error = bus_dma_tag_create(bus_get_dma_tag(dev), align, 0,
	BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	MCLBYTES * nsegs, nsegs, MCLBYTES, 0, NULL, NULL, &ri->ri_data_tag);
    if (error) {
	device_printf(dev, "failed to allocate %s buffer dma tag\n", name);
	return (error);
    }

    /* Allocate maps for the data buffers. */
    ri->ri_data_maps = malloc(sizeof(bus_dmamap_t) * count, M_DEVBUF,
	M_WAITOK | M_ZERO);
    for (i = 0; i < count; i++) {
    	error = bus_dmamap_create(ri->ri_data_tag, 0, &ri->ri_data_maps[i]);
	if (error) {
	    device_printf(dev, "failed to create map for %s buffer %d\n",
		name, i);
	    return (error);
	}
    }

    return (0);
}

/* Release busdma maps, tags, and memory. */
static void
tulip_busdma_cleanup(tulip_softc_t * const sc)
{

    /* Release resources for the setup descriptor. */
    if (sc->tulip_setup_dma_addr != 0) {
	bus_dmamap_unload(sc->tulip_setup_tag, sc->tulip_setup_map);
	sc->tulip_setup_dma_addr = 0;
    }
    if (sc->tulip_setupbuf != NULL) {
	bus_dmamem_free(sc->tulip_setup_tag, sc->tulip_setupbuf,
	    sc->tulip_setup_map);
	sc->tulip_setupbuf = NULL;
    }
    if (sc->tulip_setup_tag != NULL) {
	bus_dma_tag_destroy(sc->tulip_setup_tag);
	sc->tulip_setup_tag = NULL;
    }

    /* Release the transmit ring. */
    tulip_busdma_freering(&sc->tulip_txinfo);

    /* Release the receive ring. */
    tulip_busdma_freering(&sc->tulip_rxinfo);
}

static int
tulip_busdma_init(device_t dev, tulip_softc_t * const sc)
{
    int error;

    /*
     * Allocate space and dmamap for transmit ring.
     */
    error = tulip_busdma_allocring(dev, sc, TULIP_TXDESCS, 1, TULIP_MAX_TXSEG,
	&sc->tulip_txinfo, "transmit");
    if (error)
	return (error);

    /*
     * Allocate space and dmamap for receive ring.  We tell bus_dma that
     * we can map MCLBYTES so that it will accept a full MCLBYTES cluster,
     * but we will only map the first TULIP_RX_BUFLEN bytes.  This is not
     * a waste in practice though as an ethernet frame can easily fit
     * in TULIP_RX_BUFLEN bytes.
     */
    error = tulip_busdma_allocring(dev, sc, TULIP_RXDESCS, 4, 1,
	&sc->tulip_rxinfo, "receive");
    if (error)
	return (error);

    /*
     * Allocate a DMA tag, memory, and map for setup descriptor
     */
    error = bus_dma_tag_create(bus_get_dma_tag(dev), 32, 0,
	BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	sizeof(sc->tulip_setupdata), 1, sizeof(sc->tulip_setupdata), 0,
	NULL, NULL, &sc->tulip_setup_tag);
    if (error) {
	device_printf(dev, "failed to allocate setup descriptor dma tag\n");
	return (error);
    }
    error = bus_dmamem_alloc(sc->tulip_setup_tag, (void **)&sc->tulip_setupbuf,
	BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->tulip_setup_map);
    if (error) {
	device_printf(dev, "failed to allocate memory for setup descriptor\n");
	return (error);
    }
    error = bus_dmamap_load(sc->tulip_setup_tag, sc->tulip_setup_map,
	sc->tulip_setupbuf, sizeof(sc->tulip_setupdata), 
	tulip_dma_map_addr, &sc->tulip_setup_dma_addr, BUS_DMA_NOWAIT);
    if (error) {
	device_printf(dev, "failed to get dma address for setup descriptor\n");
	return (error);
    }

    return error;
}

static void
tulip_initcsrs(tulip_softc_t * const sc, tulip_csrptr_t csr_base,
    size_t csr_size)
{
    sc->tulip_csrs.csr_busmode		= csr_base +  0 * csr_size;
    sc->tulip_csrs.csr_txpoll		= csr_base +  1 * csr_size;
    sc->tulip_csrs.csr_rxpoll		= csr_base +  2 * csr_size;
    sc->tulip_csrs.csr_rxlist		= csr_base +  3 * csr_size;
    sc->tulip_csrs.csr_txlist		= csr_base +  4 * csr_size;
    sc->tulip_csrs.csr_status		= csr_base +  5 * csr_size;
    sc->tulip_csrs.csr_command		= csr_base +  6 * csr_size;
    sc->tulip_csrs.csr_intr		= csr_base +  7 * csr_size;
    sc->tulip_csrs.csr_missed_frames	= csr_base +  8 * csr_size;
    sc->tulip_csrs.csr_9		= csr_base +  9 * csr_size;
    sc->tulip_csrs.csr_10		= csr_base + 10 * csr_size;
    sc->tulip_csrs.csr_11		= csr_base + 11 * csr_size;
    sc->tulip_csrs.csr_12		= csr_base + 12 * csr_size;
    sc->tulip_csrs.csr_13		= csr_base + 13 * csr_size;
    sc->tulip_csrs.csr_14		= csr_base + 14 * csr_size;
    sc->tulip_csrs.csr_15		= csr_base + 15 * csr_size;
}

static int
tulip_initring(
    device_t dev,
    tulip_softc_t * const sc,
    tulip_ringinfo_t * const ri,
    int ndescs)
{
    int i;

    ri->ri_descinfo = malloc(sizeof(tulip_descinfo_t) * ndescs, M_DEVBUF,
	M_WAITOK | M_ZERO);
    for (i = 0; i < ndescs; i++) {
	ri->ri_descinfo[i].di_desc = &ri->ri_descs[i];
	ri->ri_descinfo[i].di_map = &ri->ri_data_maps[i];
    }
    ri->ri_first = ri->ri_descinfo;
    ri->ri_max = ndescs;
    ri->ri_last = ri->ri_first + ri->ri_max;
    bzero(ri->ri_descs, sizeof(tulip_desc_t) * ri->ri_max);
    ri->ri_last[-1].di_desc->d_flag = TULIP_DFLAG_ENDRING;
    return (0);
}

/*
 * This is the PCI configuration support.
 */

#define	PCI_CBIO	PCIR_BAR(0)	/* Configuration Base IO Address */
#define	PCI_CBMA	PCIR_BAR(1)	/* Configuration Base Memory Address */
#define	PCI_CFDA	0x40	/* Configuration Driver Area */

static int
tulip_pci_probe(device_t dev)
{
    const char *name = NULL;

    if (pci_get_vendor(dev) != DEC_VENDORID)
	return ENXIO;

    /*
     * Some LanMedia WAN cards use the Tulip chip, but they have
     * their own driver, and we should not recognize them
     */
    if (pci_get_subvendor(dev) == 0x1376)
	return ENXIO;

    switch (pci_get_device(dev)) {
    case CHIPID_21040:
	name = "Digital 21040 Ethernet";
	break;
    case CHIPID_21041:
	name = "Digital 21041 Ethernet";
	break;
    case CHIPID_21140:
	if (pci_get_revid(dev) >= 0x20)
	    name = "Digital 21140A Fast Ethernet";
	else
	    name = "Digital 21140 Fast Ethernet";
	break;
    case CHIPID_21142:
	if (pci_get_revid(dev) >= 0x20)
	    name = "Digital 21143 Fast Ethernet";
	else
	    name = "Digital 21142 Fast Ethernet";
	break;
    }
    if (name) {
	device_set_desc(dev, name);
	return BUS_PROBE_LOW_PRIORITY;
    }
    return ENXIO;
}

static int
tulip_shutdown(device_t dev)
{
    tulip_softc_t * const sc = device_get_softc(dev);
    TULIP_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    DELAY(10);	/* Wait 10 microseconds (actually 50 PCI cycles but at 
		   33MHz that comes to two microseconds but wait a
		   bit longer anyways) */
    return 0;
}

static int
tulip_pci_attach(device_t dev)
{
    tulip_softc_t *sc;
    int retval, idx;
    u_int32_t revinfo, cfdainfo;
    unsigned csroffset = TULIP_PCI_CSROFFSET;
    unsigned csrsize = TULIP_PCI_CSRSIZE;
    tulip_csrptr_t csr_base;
    tulip_chipid_t chipid = TULIP_CHIPID_UNKNOWN;
    struct resource *res;
    int rid, unit;

    unit = device_get_unit(dev);

    if (unit >= TULIP_MAX_DEVICES) {
	device_printf(dev, "not configured; limit of %d reached or exceeded\n",
	       TULIP_MAX_DEVICES);
	return ENXIO;
    }

    revinfo  = pci_get_revid(dev);
    cfdainfo = pci_read_config(dev, PCI_CFDA, 4);

    /* turn busmaster on in case BIOS doesn't set it */
    pci_enable_busmaster(dev);

    if (pci_get_vendor(dev) == DEC_VENDORID) {
	if (pci_get_device(dev) == CHIPID_21040)
		chipid = TULIP_21040;
	else if (pci_get_device(dev) == CHIPID_21041)
		chipid = TULIP_21041;
	else if (pci_get_device(dev) == CHIPID_21140)
		chipid = (revinfo >= 0x20) ? TULIP_21140A : TULIP_21140;
	else if (pci_get_device(dev) == CHIPID_21142)
		chipid = (revinfo >= 0x20) ? TULIP_21143 : TULIP_21142;
    }
    if (chipid == TULIP_CHIPID_UNKNOWN)
	return ENXIO;

    if (chipid == TULIP_21040 && revinfo < 0x20) {
	device_printf(dev,
	    "not configured; 21040 pass 2.0 required (%d.%d found)\n",
	    revinfo >> 4, revinfo & 0x0f);
	return ENXIO;
    } else if (chipid == TULIP_21140 && revinfo < 0x11) {
	device_printf(dev,
	    "not configured; 21140 pass 1.1 required (%d.%d found)\n",
	    revinfo >> 4, revinfo & 0x0f);
	return ENXIO;
    }

    sc = device_get_softc(dev);
    sc->tulip_dev = dev;
    sc->tulip_pci_busno = pci_get_bus(dev);
    sc->tulip_pci_devno = pci_get_slot(dev);
    sc->tulip_chipid = chipid;
    sc->tulip_flags |= TULIP_DEVICEPROBE;
    if (chipid == TULIP_21140 || chipid == TULIP_21140A)
	sc->tulip_features |= TULIP_HAVE_GPR|TULIP_HAVE_STOREFWD;
    if (chipid == TULIP_21140A && revinfo <= 0x22)
	sc->tulip_features |= TULIP_HAVE_RXBADOVRFLW;
    if (chipid == TULIP_21140)
	sc->tulip_features |= TULIP_HAVE_BROKEN_HASH;
    if (chipid != TULIP_21040 && chipid != TULIP_21140)
	sc->tulip_features |= TULIP_HAVE_POWERMGMT;
    if (chipid == TULIP_21041 || chipid == TULIP_21142 || chipid == TULIP_21143) {
	sc->tulip_features |= TULIP_HAVE_DUALSENSE;
	if (chipid != TULIP_21041 || revinfo >= 0x20)
	    sc->tulip_features |= TULIP_HAVE_SIANWAY;
	if (chipid != TULIP_21041)
	    sc->tulip_features |= TULIP_HAVE_SIAGP|TULIP_HAVE_RXBADOVRFLW|TULIP_HAVE_STOREFWD;
	if (chipid != TULIP_21041 && revinfo >= 0x20)
	    sc->tulip_features |= TULIP_HAVE_SIA100;
    }

    if (sc->tulip_features & TULIP_HAVE_POWERMGMT
	    && (cfdainfo & (TULIP_CFDA_SLEEP|TULIP_CFDA_SNOOZE))) {
	cfdainfo &= ~(TULIP_CFDA_SLEEP|TULIP_CFDA_SNOOZE);
	pci_write_config(dev, PCI_CFDA, cfdainfo, 4);
	DELAY(11*1000);
    }

    sc->tulip_unit = unit;
    sc->tulip_revinfo = revinfo;
#if defined(TULIP_IOMAPPED)
    rid = PCI_CBIO;
    res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE);
#else
    rid = PCI_CBMA;
    res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
#endif
    if (!res)
	return ENXIO;
    sc->tulip_csrs_bst = rman_get_bustag(res);
    sc->tulip_csrs_bsh = rman_get_bushandle(res);
    csr_base = 0;

    mtx_init(TULIP_MUTEX(sc), MTX_NETWORK_LOCK, device_get_nameunit(dev),
	MTX_DEF);
    callout_init_mtx(&sc->tulip_callout, TULIP_MUTEX(sc), 0);
    callout_init_mtx(&sc->tulip_stat_timer, TULIP_MUTEX(sc), 0);
    tulips[unit] = sc;

    tulip_initcsrs(sc, csr_base + csroffset, csrsize);

    if ((retval = tulip_busdma_init(dev, sc)) != 0) {
	device_printf(dev, "error initing bus_dma: %d\n", retval);
	tulip_busdma_cleanup(sc);
	mtx_destroy(TULIP_MUTEX(sc));
	return ENXIO;
    }

    retval = tulip_initring(dev, sc, &sc->tulip_rxinfo, TULIP_RXDESCS);
    if (retval == 0)
	retval = tulip_initring(dev, sc, &sc->tulip_txinfo, TULIP_TXDESCS);
    if (retval) {
	tulip_busdma_cleanup(sc);
	mtx_destroy(TULIP_MUTEX(sc));
	return retval;
    }

    /*
     * Make sure there won't be any interrupts or such...
     */
    TULIP_CSR_WRITE(sc, csr_busmode, TULIP_BUSMODE_SWRESET);
    DELAY(100);	/* Wait 10 microseconds (actually 50 PCI cycles but at 
		   33MHz that comes to two microseconds but wait a
		   bit longer anyways) */

    TULIP_LOCK(sc);
    retval = tulip_read_macaddr(sc);
    TULIP_UNLOCK(sc);
    if (retval < 0) {
	device_printf(dev, "can't read ENET ROM (why=%d) (", retval);
	for (idx = 0; idx < 32; idx++)
	    printf("%02x", sc->tulip_rombuf[idx]);
	printf("\n");
	device_printf(dev, "%s%s pass %d.%d\n",
	       sc->tulip_boardid, tulip_chipdescs[sc->tulip_chipid],
	       (sc->tulip_revinfo & 0xF0) >> 4, sc->tulip_revinfo & 0x0F);
	device_printf(dev, "address unknown\n");
    } else {
	void (*intr_rtn)(void *) = tulip_intr_normal;

	if (sc->tulip_features & TULIP_HAVE_SHAREDINTR)
	    intr_rtn = tulip_intr_shared;

	tulip_attach(sc);

	/* Setup interrupt last. */
	if ((sc->tulip_features & TULIP_HAVE_SLAVEDINTR) == 0) {
	    void *ih;

	    rid = 0;
	    res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					 RF_SHAREABLE | RF_ACTIVE);
	    if (res == NULL || bus_setup_intr(dev, res, INTR_TYPE_NET |
                                              INTR_MPSAFE, NULL, intr_rtn, sc, &ih)) {
		device_printf(dev, "couldn't map interrupt\n");
		tulip_busdma_cleanup(sc);
		ether_ifdetach(sc->tulip_ifp);
		if_free(sc->tulip_ifp);
		mtx_destroy(TULIP_MUTEX(sc));
		return ENXIO;
	    }
	}
    }
    return 0;
}

static device_method_t tulip_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	tulip_pci_probe),
    DEVMETHOD(device_attach,	tulip_pci_attach),
    DEVMETHOD(device_shutdown,	tulip_shutdown),
    { 0, 0 }
};

static driver_t tulip_pci_driver = {
    "de",
    tulip_pci_methods,
    sizeof(tulip_softc_t),
};

static devclass_t tulip_devclass;

DRIVER_MODULE(de, pci, tulip_pci_driver, tulip_devclass, 0, 0);

#ifdef DDB
void	tulip_dumpring(int unit, int ring);
void	tulip_dumpdesc(int unit, int ring, int desc);
void	tulip_status(int unit);

void
tulip_dumpring(int unit, int ring)
{
    tulip_softc_t *sc;
    tulip_ringinfo_t *ri;
    tulip_descinfo_t *di;

    if (unit < 0 || unit >= TULIP_MAX_DEVICES) {
	db_printf("invalid unit %d\n", unit);
	return;
    }
    sc = tulips[unit];
    if (sc == NULL) {
	db_printf("unit %d not present\n", unit);
	return;
    }

    switch (ring) {
    case 0:
	db_printf("receive ring:\n");
	ri = &sc->tulip_rxinfo;
	break;
    case 1:
	db_printf("transmit ring:\n");
	ri = &sc->tulip_txinfo;
	break;
    default:
	db_printf("invalid ring %d\n", ring);
	return;
    }

    db_printf(" nextin: %td, nextout: %td, max: %d, free: %d\n",
	ri->ri_nextin - ri->ri_first, ri->ri_nextout - ri->ri_first,
	ri->ri_max, ri->ri_free);
    for (di = ri->ri_first; di != ri->ri_last; di++) {
	if (di->di_mbuf != NULL)
	    db_printf(" descriptor %td: mbuf %p\n", di - ri->ri_first,
		di->di_mbuf);
	else if (di->di_desc->d_flag & TULIP_DFLAG_TxSETUPPKT)
	    db_printf(" descriptor %td: setup packet\n", di - ri->ri_first);
    }
}

void
tulip_dumpdesc(int unit, int ring, int desc)
{
    tulip_softc_t *sc;
    tulip_ringinfo_t *ri;
    tulip_descinfo_t *di;
    char *s;

    if (unit < 0 || unit >= TULIP_MAX_DEVICES) {
	db_printf("invalid unit %d\n", unit);
	return;
    }
    sc = tulips[unit];
    if (sc == NULL) {
	db_printf("unit %d not present\n", unit);
	return;
    }

    switch (ring) {
    case 0:
	s = "receive";
	ri = &sc->tulip_rxinfo;
	break;
    case 1:
	s = "transmit";
	ri = &sc->tulip_txinfo;
	break;
    default:
	db_printf("invalid ring %d\n", ring);
	return;
    }

    if (desc < 0 || desc >= ri->ri_max) {
	db_printf("invalid descriptor %d\n", desc);
	return;
    }

    db_printf("%s descriptor %d:\n", s, desc);
    di = &ri->ri_first[desc];
    db_printf(" mbuf: %p\n", di->di_mbuf);
    db_printf(" status: %08x  flag: %03x\n", di->di_desc->d_status,
	di->di_desc->d_flag);
    db_printf("  addr1: %08x  len1: %03x\n", di->di_desc->d_addr1,
	di->di_desc->d_length1);
    db_printf("  addr2: %08x  len2: %03x\n", di->di_desc->d_addr2,
	di->di_desc->d_length2);
}
#endif
