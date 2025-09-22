/*	$OpenBSD: dcphy.c,v 1.26 2022/04/06 18:59:29 naddy Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: src/sys/dev/mii/dcphy.c,v 1.6 2000/10/05 17:36:14 wpaul Exp $
 */

/*
 * Pseudo-driver for internal NWAY support on DEC 21143 and workalike
 * controllers. Technically we're abusing the miibus code to handle
 * media selection and NWAY support here since there is no MII
 * interface. However the logical operations are roughly the same,
 * and the alternative is to create a fake MII interface in the driver,
 * which is harder to do.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

#include <dev/ic/dcreg.h>

#define DC_SETBIT(sc, reg, x)                           \
        CSR_WRITE_4(sc, reg,                            \
                CSR_READ_4(sc, reg) | x)

#define DC_CLRBIT(sc, reg, x)                           \
        CSR_WRITE_4(sc, reg,                            \
                CSR_READ_4(sc, reg) & ~x)

#define MIIF_AUTOTIMEOUT	0x0004

/*
 * This is the subsystem ID for the built-in 21143 ethernet
 * in several Compaq Presario systems. Apparently these are
 * 10Mbps only, so we need to treat them specially.
 */
#define COMPAQ_PRESARIO_ID	0xb0bb0e11

int	dcphy_match(struct device *, void *, void *);
void	dcphy_attach(struct device *, struct device *, void *);

const struct cfattach dcphy_ca = {
	sizeof(struct mii_softc), dcphy_match, dcphy_attach, mii_phy_detach
};

struct cfdriver dcphy_cd = {
	NULL, "dcphy", DV_DULL
};

int	dcphy_service(struct mii_softc *, struct mii_data *, int);
void	dcphy_status(struct mii_softc *);
int	dcphy_mii_phy_auto(struct mii_softc *, int);
void	dcphy_reset(struct mii_softc *);

const struct mii_phy_funcs dcphy_funcs = {
	dcphy_service, dcphy_status, dcphy_reset,
};

int
dcphy_match(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	/*
	 * The dc driver will report the 21143 vendor and device
	 * ID to let us know that it wants us to attach.
	 */
	if (MII_OUI(ma->mii_id1, ma->mii_id2) == MII_OUI_xxDEC &&
	    MII_MODEL(ma->mii_id2) == MII_MODEL_xxDEC_xxDC)
		return (10);

	return (0);
}

void
dcphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	struct dc_softc *dc_sc;

	printf(": internal PHY\n");
	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &dcphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	sc->mii_anegticks = 50;

	sc->mii_flags |= MIIF_NOISOLATE;

	dc_sc = mii->mii_ifp->if_softc;
	CSR_WRITE_4(dc_sc, DC_10BTSTAT, 0);
	CSR_WRITE_4(dc_sc, DC_10BTCTRL, 0);

#define	ADD(m, c)	ifmedia_add(&mii->mii_media, (m), (c), NULL)

	switch(dc_sc->dc_csid) {
	case COMPAQ_PRESARIO_ID:
		/* Example of how to only allow 10Mbps modes. */
		sc->mii_capabilities = BMSR_ANEG|BMSR_10TFDX|BMSR_10THDX;
		break;
	default:
		if (dc_sc->dc_pmode == DC_PMODE_SIA) {
			sc->mii_capabilities =
			    BMSR_ANEG|BMSR_10TFDX|BMSR_10THDX;
		} else {
			ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_TX, IFM_LOOP,
			    sc->mii_inst), BMCR_LOOP|BMCR_S100);

			sc->mii_capabilities =
			    BMSR_ANEG|BMSR_100TXFDX|BMSR_100TXHDX|
			    BMSR_10TFDX|BMSR_10THDX;
		}
		break;
	}

	if (dc_sc->dc_type == DC_TYPE_21145)
		sc->mii_capabilities = BMSR_10THDX;

	sc->mii_capabilities &= ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(sc);
#undef ADD
}

int
dcphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct dc_softc *dc_sc;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;
	u_int32_t mode;

	if ((sc->mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

	dc_sc = mii->mii_ifp->if_softc;

	switch (cmd) {
	case MII_POLLSTAT:
		/*
		 * If we're not polling our PHY instance, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);
		break;

	case MII_MEDIACHG:
		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		sc->mii_flags = 0;
		mii->mii_media_active = IFM_NONE;
		mode = CSR_READ_4(dc_sc, DC_NETCFG);
		mode &= ~(DC_NETCFG_FULLDUPLEX|DC_NETCFG_PORTSEL|
		    DC_NETCFG_PCS|DC_NETCFG_SCRAMBLER|DC_NETCFG_SPEEDSEL);

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			/*PHY_RESET(sc);*/
			sc->mii_flags &= ~MIIF_DOINGAUTO;
			(void) dcphy_mii_phy_auto(sc, 0);
			break;
		case IFM_100_TX:
			PHY_RESET(sc);
			DC_CLRBIT(dc_sc, DC_10BTCTRL, DC_TCTL_AUTONEGENBL);
			mode |= DC_NETCFG_PORTSEL|DC_NETCFG_PCS|
			    DC_NETCFG_SCRAMBLER;
			if ((ife->ifm_media & IFM_GMASK) == IFM_FDX)
				mode |= DC_NETCFG_FULLDUPLEX;
			else
				mode &= ~DC_NETCFG_FULLDUPLEX;
			CSR_WRITE_4(dc_sc, DC_NETCFG, mode);
			break;
		case IFM_10_T:
			DC_CLRBIT(dc_sc, DC_SIARESET, DC_SIA_RESET);
			DC_CLRBIT(dc_sc, DC_10BTCTRL, 0xFFFF);
			if ((ife->ifm_media & IFM_GMASK) == IFM_FDX)
				DC_SETBIT(dc_sc, DC_10BTCTRL, 0x7F3D);
			else
				DC_SETBIT(dc_sc, DC_10BTCTRL, 0x7F3F);
			DC_SETBIT(dc_sc, DC_SIARESET, DC_SIA_RESET);
			DC_CLRBIT(dc_sc, DC_10BTCTRL, DC_TCTL_AUTONEGENBL);
			mode &= ~DC_NETCFG_PORTSEL;
			mode |= DC_NETCFG_SPEEDSEL;
			if ((ife->ifm_media & IFM_GMASK) == IFM_FDX)
				mode |= DC_NETCFG_FULLDUPLEX;
			else
				mode &= ~DC_NETCFG_FULLDUPLEX;
			CSR_WRITE_4(dc_sc, DC_NETCFG, mode);
			break;
		default:
			return (EINVAL);
		}
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		/*
		 * Is the interface even up?
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return (0);

		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			break;

		reg = CSR_READ_4(dc_sc, DC_10BTSTAT);
		if (!(reg & DC_TSTAT_LS10) || !(reg & DC_TSTAT_LS100)) {
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * Only retry autonegotiation every mii_anegticks seconds.
		 *
		 * Otherwise, fall through to calling dcphy_status()
		 * since real Intel 21143 chips don't show valid link
		 * status until autonegotiation is switched off, and
		 * that only happens in dcphy_status().  Without this,
		 * successful autonegotiation is never recognised on
		 * these chips.
		 */
		if (++sc->mii_ticks <= sc->mii_anegticks)
			break;

		sc->mii_ticks = 0;
		sc->mii_flags &= ~MIIF_DOINGAUTO;
		dcphy_mii_phy_auto(sc, 0);

		break;
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
dcphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int reg, anlpar, tstat = 0;
	struct dc_softc *dc_sc;

	dc_sc = mii->mii_ifp->if_softc;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	reg = CSR_READ_4(dc_sc, DC_10BTSTAT);
	if (!(reg & DC_TSTAT_LS10) || !(reg & DC_TSTAT_LS100))
		mii->mii_media_status |= IFM_ACTIVE;

	if (CSR_READ_4(dc_sc, DC_10BTCTRL) & DC_TCTL_AUTONEGENBL) {
		/* Erg, still trying, I guess... */
		tstat = CSR_READ_4(dc_sc, DC_10BTSTAT);
		if ((tstat & DC_TSTAT_ANEGSTAT) != DC_ASTAT_AUTONEGCMP) {
			if ((DC_IS_MACRONIX(dc_sc) || DC_IS_PNICII(dc_sc)) &&
			    (tstat & DC_TSTAT_ANEGSTAT) == DC_ASTAT_DISABLE)
				goto skip;
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		if (tstat & DC_TSTAT_LP_CAN_NWAY) {
			anlpar = tstat >> 16;
			if (anlpar & ANLPAR_TX_FD &&
			    sc->mii_capabilities & BMSR_100TXFDX)
				mii->mii_media_active |= IFM_100_TX|IFM_FDX;
			else if (anlpar & ANLPAR_T4 &&
			    sc->mii_capabilities & BMSR_100T4)
				mii->mii_media_active |= IFM_100_T4|IFM_HDX;
			else if (anlpar & ANLPAR_TX &&
			    sc->mii_capabilities & BMSR_100TXHDX)
				mii->mii_media_active |= IFM_100_TX|IFM_HDX;
			else if (anlpar & ANLPAR_10_FD)
				mii->mii_media_active |= IFM_10_T|IFM_FDX;
			else if (anlpar & ANLPAR_10)
				mii->mii_media_active |= IFM_10_T|IFM_HDX;
			else
				mii->mii_media_active |= IFM_NONE;
			if (DC_IS_INTEL(dc_sc))
				DC_CLRBIT(dc_sc, DC_10BTCTRL,
				    DC_TCTL_AUTONEGENBL);
			return;
		}

		/*
		 * If the other side doesn't support NWAY, then the
		 * best we can do is determine if we have a 10Mbps or
		 * 100Mbps link. There's no way to know if the link
		 * is full or half duplex, so we default to half duplex
		 * and hope that the user is clever enough to manually
		 * change the media settings if we're wrong.
		 */
		if (!(reg & DC_TSTAT_LS100))
			mii->mii_media_active |= IFM_100_TX|IFM_HDX;
		else if (!(reg & DC_TSTAT_LS10))
			mii->mii_media_active |= IFM_10_T|IFM_HDX;
		else
			mii->mii_media_active |= IFM_NONE;
		if (DC_IS_INTEL(dc_sc))
			DC_CLRBIT(dc_sc, DC_10BTCTRL, DC_TCTL_AUTONEGENBL);
		return;
	}

skip:
	if (CSR_READ_4(dc_sc, DC_NETCFG) & DC_NETCFG_SPEEDSEL)
		mii->mii_media_active |= IFM_10_T;
	else
		mii->mii_media_active |= IFM_100_TX;

	if (CSR_READ_4(dc_sc, DC_NETCFG) & DC_NETCFG_FULLDUPLEX)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;
}

int
dcphy_mii_phy_auto(struct mii_softc *mii, int waitfor)
{
	int			i;
	struct dc_softc		*sc;

	sc = mii->mii_pdata->mii_ifp->if_softc;

	if ((mii->mii_flags & MIIF_DOINGAUTO) == 0) {
		DC_CLRBIT(sc, DC_NETCFG, DC_NETCFG_PORTSEL);
		DC_SETBIT(sc, DC_NETCFG, DC_NETCFG_FULLDUPLEX);
		DC_CLRBIT(sc, DC_SIARESET, DC_SIA_RESET);
		if (mii->mii_capabilities & BMSR_100TXHDX)
			CSR_WRITE_4(sc, DC_10BTCTRL, 0x3FFFF);
		else
			CSR_WRITE_4(sc, DC_10BTCTRL, 0xFFFF);
		DC_SETBIT(sc, DC_SIARESET, DC_SIA_RESET);
		DC_SETBIT(sc, DC_10BTCTRL, DC_TCTL_AUTONEGENBL);
		DC_SETBIT(sc, DC_10BTSTAT, DC_ASTAT_TXDISABLE);
	}

	if (waitfor) {
		/* Wait 500ms for it to complete. */
		for (i = 0; i < 500; i++) {
			if ((CSR_READ_4(sc, DC_10BTSTAT) & DC_TSTAT_ANEGSTAT)
			    == DC_ASTAT_AUTONEGCMP)
				return (0);
			DELAY(1000);
		}
		/*
		 * Don't need to worry about clearing MIIF_DOINGAUTO.
		 * If that's set, a timeout is pending, and it will
		 * clear the flag.
		 */
		return (EIO);
	}

	/*
	 * Just let it finish asynchronously.  This is for the benefit of
	 * the tick handler driving autonegotiation.  Don't want 500ms
	 * delays all the time while the system is running!
	 */
	if ((mii->mii_flags & MIIF_DOINGAUTO) == 0)
		mii->mii_flags |= MIIF_DOINGAUTO;

	return (EJUSTRETURN);
}

void
dcphy_reset(struct mii_softc *mii)
{
	struct dc_softc		*sc;

	sc = mii->mii_pdata->mii_ifp->if_softc;

	DC_CLRBIT(sc, DC_SIARESET, DC_SIA_RESET);
	DELAY(1000);
	DC_SETBIT(sc, DC_SIARESET, DC_SIA_RESET);

	return;
}
