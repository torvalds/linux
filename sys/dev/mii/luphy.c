/*	$OpenBSD: luphy.c,v 1.7 2022/04/06 18:59:29 naddy Exp $	*/

/*-
 * Copyright (c) 2004 Marius Strobl
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver for the Lucent Technologies LU6612 Ethernet 10/100 PHY, which
 * is pin-compatible to the Quality Semiconductor QS6612 but uses a
 * different register layout. Sun Microsystems uses the LU6612 together
 * with their HME chip on a couple of bords originally designed for the
 * QS6612. At least on these boards none of the LU6612 must be isolated
 * otherwise the MII bus wedges and the other one (there can be a maximum
 * of two PHYs connected to the HME) no longer can communicate with the
 * HME (powering down the unused PHY etc. also doesn't help). This is
 * why we can't use the ukphy driver for the LU6612.
 * The datasheet for the LU6612 is no longer available on lucent.com or
 * www.agere.com (former Lucent Microelectronics Group) but you still
 * should be able to find it when searching for DS00355.pdf or LU6612.pdf.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

int	luphymatch(struct device *, void *, void *);
void	luphyattach(struct device *, struct device *, void *);

const struct cfattach luphy_ca = {
	sizeof(struct mii_softc), luphymatch, luphyattach, mii_phy_detach
};

struct cfdriver luphy_cd = {
	NULL, "luphy", DV_DULL
};

int	luphy_service(struct mii_softc *, struct mii_data *, int);

const struct mii_phy_funcs luphy_funcs = {
	luphy_service, ukphy_status, mii_phy_reset,
};

static const struct mii_phydesc luphys[] = {
	{ MII_OUI_LUCENT,		MII_MODEL_LUCENT_LU6612,
	  MII_STR_LUCENT_LU6612 },

	{ 0,			0,
	  NULL },
};

int
luphymatch(struct device *parent, void *match, void *aux)
{
	struct mii_attach_args *ma = aux;

	if (mii_phy_match(ma, luphys) != NULL)
		return (10);

	return (0);
}

void
luphyattach(struct device *parent, struct device *self, void *aux)
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	const struct mii_phydesc *mpd;

	mpd = mii_phy_match(ma, luphys);
	printf(": %s, rev. %d\n", mpd->mpd_name, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_funcs = &luphy_funcs;
	sc->mii_pdata = mii;
	sc->mii_flags = ma->mii_flags;

	sc->mii_flags |= MIIF_NOISOLATE;

	PHY_RESET(sc);

	sc->mii_capabilities =
	    PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(sc);
}

int
luphy_service(struct mii_softc *sc, struct mii_data *mii, int cmd)
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;

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
		 * just return. Isolating unused PHYs from the bus
		 * causes at least the MII bus of the HME to wedge.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_setmedia(sc);
		break;

       case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

		if (mii_phy_tick(sc) == EJUSTRETURN)
			return (0);
		break;

	case MII_DOWN:
		mii_phy_down(sc);
		return (0);
        }

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}
