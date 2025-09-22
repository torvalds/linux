/*	$OpenBSD: mlphy.c,v 1.6 2022/04/06 18:59:29 naddy Exp $	*/

/*-
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul <at> ctr.columbia.edu>.  All rights reserved.
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
 */

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center, and by Frank van der Linden.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 */

/*
 * Micro Linear 6692 PHY
 *
 * The Micro Linear 6692 is a strange beast, and dealing with it using
 * this code framework is tricky. The 6692 is actually a 100Mbps-only
 * device, which means that a second PHY is required to support 10Mbps
 * modes. However, even though the 6692 does not support 10Mbps modes,
 * it can still advertise them when performing autonegotiation. If a
 * 10Mbps mode is negotiated, we must program the registers of the
 * companion PHY accordingly in addition to programming the registers
 * of the 6692.
 *
 * This device also does not have vendor/device ID registers.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#define ML_STATE_AUTO_SELF      1
#define ML_STATE_AUTO_OTHER     2

struct mlphy_softc {
	struct mii_softc	 ml_mii;
	struct device		*ml_dev;
	int			 ml_state;
	int			 ml_linked;
};

int	mlphy_probe(struct device *, void *, void *);
void	mlphy_attach(struct device *, struct device *, void *);

const struct cfattach mlphy_ca = {
	sizeof(struct mii_softc), mlphy_probe, mlphy_attach, mii_phy_detach
};

struct cfdriver mlphy_cd = {
	NULL, "mlphy", DV_DULL
};

void    mlphy_reset(struct mii_softc *);
int	mlphy_service(struct mii_softc *, struct mii_data *, int);
void	mlphy_status(struct mii_softc *);

const struct mii_phy_funcs mlphy_funcs = {
	mlphy_service, mlphy_status, mlphy_reset,
};

int
mlphy_probe(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args  *ma = aux;

	/*
	 * Micro Linear PHY reports oui == 0 model == 0
	 */
	if (MII_OUI(ma->mii_id1, ma->mii_id2) != 0 ||
	    MII_MODEL(ma->mii_id2) != 0)
		return (0);
	/*
	 * Make sure the parent is a `tl'. So far, I have only
	 * encountered the 6692 on an Olicom card with a ThunderLAN
	 * controller chip.
	 */
	if (strcmp(parent->dv_cfdata->cf_driver->cd_name, "tl") != 0)
		return (0);

	return (10);
}

void
mlphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct mlphy_softc *msc = (struct mlphy_softc *)self;
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;

	printf(": ML6692 100baseTX PHY\n");

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &mlphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;
	msc->ml_dev = parent; 

	PHY_RESET(sc);

	sc->mii_capabilities = PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
#define ADD(m, c)	ifmedia_add(&mii->mii_media, (m), (c), NULL)
	ADD(IFM_MAKEWORD(IFM_ETHER, IFM_100_TX, IFM_LOOP, sc->mii_inst),
	   MII_MEDIA_100_TX);
	ma->mii_capmask = ~sc->mii_capabilities;
#undef ADD
	if(sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(sc);
}

int
mlphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	struct mii_softc *other = NULL;
	struct mlphy_softc *msc = (struct mlphy_softc *)sc;
	int other_inst, reg;

	LIST_FOREACH(other, &mii->mii_phys, mii_list)
		if (other != sc)
			break;

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
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			msc->ml_state = ML_STATE_AUTO_SELF;         
			if (other != NULL) {
				mii_phy_reset(other);
				PHY_WRITE(other, MII_BMCR, BMCR_ISO);
                       	}
			(void)mii_phy_auto(sc, 0);
			msc->ml_linked = 0;
			break;
		case IFM_10_T:
			/*
		 	 * For 10baseT modes, reset and program the
		 	 * companion PHY (of any), then setup ourselves
			 * to match. This will put us in pass-through
		 	 * mode and let the companion PHY do all the
		 	 * work.   
		 	 * BMCR data is stored in the ifmedia entry.
		 	 */
			if (other != NULL) {
				mii_phy_reset(other);
				PHY_WRITE(other, MII_BMCR, ife->ifm_data);
			}
			mii_phy_setmedia(sc);
			msc->ml_state = 0;
			break;
		case IFM_100_TX:
			/*
		 	 * For 100baseTX modes, reset and isolate the
		 	 * companion PHY (if any), then setup ourselves
		 	 * accordingly.
		 	 *
		 	 * BMCR data is stored in the ifmedia entry.
		 	 */
			if (other != NULL) {
				mii_phy_reset(other);
				PHY_WRITE(other, MII_BMCR, BMCR_ISO);
                       	}
			mii_phy_setmedia(sc);
			msc->ml_state = 0;
			break;
		default:
			return (EINVAL);
		}
		break;
	case MII_TICK:
		/*
		 * If interface is not up, don't do anything
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
		 * If we're in a 10Mbps mode, check the link of the
		 * 10Mbps PHY. Sometimes the Micro Linear PHY's
		 * linkstat bit will clear while the linkstat bit of
		 * the companion PHY will remain set.
		 */
		if (msc->ml_state == ML_STATE_AUTO_OTHER) {
			reg = PHY_READ(other, MII_BMSR) |
			    PHY_READ(other, MII_BMSR);
		} else {
			reg = PHY_READ(sc, MII_BMSR) |
			    PHY_READ(sc, MII_BMSR);
                }

		if (reg & BMSR_LINK) {
			if (!msc->ml_linked) {
				msc->ml_linked = 1;
				mlphy_status(sc);
			}
			sc->mii_ticks = 0;
			break;
		}
		/*
		 * Only retry autonegotiation every 5 seconds.
		 */
		if (++sc->mii_ticks <= MII_ANEGTICKS)
			break;

		sc->mii_ticks = 0;
		msc->ml_linked = 0;
		mii->mii_media_active = IFM_NONE;
		mii_phy_reset(sc);
		msc->ml_state = ML_STATE_AUTO_SELF;
		if (other != NULL) {
			mii_phy_reset(other);
			PHY_WRITE(other, MII_BMCR, BMCR_ISO);  
		}
		mii_phy_auto(sc, 0);
		break;

	case MII_DOWN:
		mii_phy_down(sc);
		return (0);
	}

	/* Update the media status. */
	if (msc->ml_state == ML_STATE_AUTO_OTHER && other != NULL) {
		other_inst = other->mii_inst;
		other->mii_inst = sc->mii_inst;
		if (IFM_INST(ife->ifm_media) == other->mii_inst)
			(void) PHY_SERVICE(other, mii, MII_POLLSTAT);
		other->mii_inst = other_inst;
		sc->mii_media_active = other->mii_media_active;
		sc->mii_media_status = other->mii_media_status;
	} else
		ukphy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
mlphy_reset(struct mii_softc *sc)
{
	int reg;

	mii_phy_reset(sc);
	reg = PHY_READ(sc, MII_BMCR);
	reg &= ~BMCR_AUTOEN;
	PHY_WRITE(sc, MII_BMCR, reg);
}

/*
 * If we negotiate a 10Mbps mode, we need to check for an alternate
 * PHY and make sure it's enabled and set correctly.
 */
void
mlphy_status(struct mii_softc *sc)
{
	struct mlphy_softc *msc = (struct mlphy_softc *)sc;
	struct mii_data *mii = sc->mii_pdata;
	struct mii_softc *other = NULL;

	/* See if there's another PHY on the bus with us. */
	LIST_FOREACH(other, &mii->mii_phys, mii_list)
		if (other != sc)
			break;

	ukphy_status(sc);

	if (IFM_SUBTYPE(mii->mii_media_active) != IFM_10_T) {
		msc->ml_state = ML_STATE_AUTO_SELF;
		if (other != NULL) {
			mii_phy_reset(other);
			PHY_WRITE(other, MII_BMCR, BMCR_ISO);
		}
	}

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_10_T) {
		msc->ml_state = ML_STATE_AUTO_OTHER;
		mlphy_reset(&msc->ml_mii);
		PHY_WRITE(&msc->ml_mii, MII_BMCR, BMCR_ISO);
		if (other != NULL) {
			mii_phy_reset(other);
			mii_phy_auto(other, 1);
		}
	}
}
