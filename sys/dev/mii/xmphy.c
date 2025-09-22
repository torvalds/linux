/*	$OpenBSD: xmphy.c,v 1.26 2024/06/26 01:40:49 jsg Exp $	*/

/*
 * Copyright (c) 2000
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
 * $FreeBSD: src/sys/dev/mii/xmphy.c,v 1.1 2000/04/22 01:58:18 wpaul Exp $
 */

/*
 * driver for the XaQti XMAC II's internal PHY. This is sort of
 * like a 10/100 PHY, except the only thing we're really autoselecting
 * here is full/half duplex. Speed is always 1000mbps.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

int xmphy_probe(struct device *, void *, void *);
void xmphy_attach(struct device *, struct device *, void *);

const struct cfattach xmphy_ca = {
	sizeof(struct mii_softc), xmphy_probe, xmphy_attach, mii_phy_detach
};

struct cfdriver xmphy_cd = {
	NULL, "xmphy", DV_DULL
};

int	xmphy_service(struct mii_softc *, struct mii_data *, int);
void	xmphy_status(struct mii_softc *);

int	xmphy_mii_phy_auto(struct mii_softc *);

const struct mii_phy_funcs xmphy_funcs = {
	xmphy_service, xmphy_status, mii_phy_reset,
};

static const struct mii_phydesc xmphys[] = {
	{ MII_OUI_xxXAQTI,	MII_MODEL_XAQTI_XMACII,
	  MII_STR_XAQTI_XMACII },

	{ MII_OUI_JATO,		MII_MODEL_JATO_BASEX,
	  MII_STR_JATO_BASEX },

	{ 0,			0,
	  NULL },
};

int
xmphy_probe(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, xmphys) != NULL)
		return (10);

	return (0);
}

void
xmphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, xmphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &xmphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	sc->mii_anegticks = MII_ANEGTICKS;

	sc->mii_flags |= MIIF_NOISOLATE;

#define	ADD(m, c)	ifmedia_add(&mii->mii_media, (m), (c), NULL)

	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_NONE, 0, sc->mii_inst),
	    BMCR_ISO);

	PHY_RESET(sc);

	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, 0, sc->mii_inst),
	    BMCR_FDX);
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_1000_SX, IFM_FDX, sc->mii_inst), 0);
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_AUTO, 0, sc->mii_inst), 0);

#undef ADD
}

int
xmphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	if ((sc->mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

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
		if (IFM_INST(ife->ifm_media) != sc->mii_inst) {
			reg = PHY_READ(sc, MII_BMCR);
			PHY_WRITE(sc, MII_BMCR, reg | BMCR_ISO);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			(void) xmphy_mii_phy_auto(sc);
			break;
		case IFM_1000_SX:
			PHY_RESET(sc);
			if ((ife->ifm_media & IFM_GMASK) == IFM_FDX) {
				PHY_WRITE(sc, MII_ANAR, ANAR_10_FD);
				PHY_WRITE(sc, MII_BMCR, BMCR_FDX);
			} else {
				PHY_WRITE(sc, MII_ANAR, ANAR_10);
				PHY_WRITE(sc, MII_BMCR, 0);
			}
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

                /*
                 * Check to see if we have link.  If we do, we don't
                 * need to restart the autonegotiation process.  Read
                 * the BMSR twice in case it's latched.
                 */
		reg = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
		if (reg & BMSR_LINK) {
			sc->mii_ticks = 0;
			break;
		}

		/*
		 * Only retry autonegotiation every mii_anegticks seconds.
		 */
		if (++sc->mii_ticks <= sc->mii_anegticks)
			break;

		sc->mii_ticks = 0;
		PHY_RESET(sc);

		xmphy_mii_phy_auto(sc);
		break;
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
xmphy_status(struct mii_softc *sc)
{
	struct mii_data *mii = sc->mii_pdata;
	int bmsr, bmcr, anlpar;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) | PHY_READ(sc, MII_BMSR);
	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	/* Do dummy read of extended status register. */
	bmcr = PHY_READ(sc, MII_EXTSR);

	bmcr = PHY_READ(sc, MII_BMCR);

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;


	if (bmcr & BMCR_AUTOEN) {
		if ((bmsr & BMSR_ACOMP) == 0) {
			if (bmsr & BMSR_LINK) {
				mii->mii_media_active |= IFM_1000_SX|IFM_HDX;
				return;
			}
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		mii->mii_media_active |= IFM_1000_SX;
		anlpar = PHY_READ(sc, MII_ANAR) & PHY_READ(sc, MII_ANLPAR);
		if (anlpar & ANLPAR_10_FD)
			mii->mii_media_active |= IFM_FDX;
		else
			mii->mii_media_active |= IFM_HDX;
		return;
	}

	mii->mii_media_active |= IFM_1000_SX;
	if (bmcr & BMCR_FDX)
		mii->mii_media_active |= IFM_FDX;
	else
		mii->mii_media_active |= IFM_HDX;

	return;
}


int
xmphy_mii_phy_auto(struct mii_softc *sc)
{
	int anar = 0;

	anar = PHY_READ(sc, MII_ANAR);
	anar |= ANAR_10_FD|ANAR_10;
	PHY_WRITE(sc, MII_ANAR, anar);
	DELAY(1000);
	PHY_WRITE(sc, MII_BMCR, BMCR_AUTOEN | BMCR_STARTNEG);

	return (EJUSTRETURN);
}
