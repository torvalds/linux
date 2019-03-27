/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/bhnd/bhnd.h>

#include <dev/bwn/if_bwnvar.h>

#include "if_bwn_phy_n_sprom.h"

#include "bhnd_nvram_map.h"


/* Core power NVRAM variables, indexed by D11 core unit number */
static const struct bwn_nphy_power_vars {
	const char *itt2ga;
	const char *itt5ga;
	const char *maxp2ga;
	const char *pa2ga;
	const char *pa5ga;
} bwn_nphy_power_vars[BWN_NPHY_NUM_CORE_PWR] = {
#define	BHND_POWER_NVAR(_idx)					\
	{ BHND_NVAR_ITT2GA ## _idx, BHND_NVAR_ITT5GA ## _idx,	\
	  BHND_NVAR_MAXP2GA ## _idx, BHND_NVAR_PA2GA ## _idx,	\
	  BHND_NVAR_PA5GA ## _idx }
	BHND_POWER_NVAR(0),
	BHND_POWER_NVAR(1),
	BHND_POWER_NVAR(2),
	BHND_POWER_NVAR(3)
#undef BHND_POWER_NVAR
};

static int
bwn_nphy_get_core_power_info_r11(struct bwn_softc *sc,
    const struct bwn_nphy_power_vars *v, struct bwn_phy_n_core_pwr_info *c)
{
	int16_t	pa5ga[12];
	int	error;

	/* BHND_NVAR_PA2GA[core] */
	error = bhnd_nvram_getvar_array(sc->sc_dev, v->pa2ga, c->pa_2g,
	    sizeof(c->pa_2g), BHND_NVRAM_TYPE_INT16);
	if (error)
		return (error);

	/* 
	 * BHND_NVAR_PA5GA
	 * 
	 * The NVRAM variable is defined as a single pa5ga[12] array; we have
	 * to split this into pa_5gl[4], pa_5g[4], and pa_5gh[4] for use
	 * by bwn(4);
	 */
	_Static_assert(nitems(pa5ga) == nitems(c->pa_5g) + nitems(c->pa_5gh) +
	    nitems(c->pa_5gl), "cannot split pa5ga into pa_5gl/pa_5g/pa_5gh");

	error = bhnd_nvram_getvar_array(sc->sc_dev, v->pa5ga, pa5ga,
	    sizeof(pa5ga), BHND_NVRAM_TYPE_INT16);
	if (error)
		return (error);

	memcpy(c->pa_5gl, &pa5ga[0], sizeof(c->pa_5gl));
	memcpy(c->pa_5g, &pa5ga[4], sizeof(c->pa_5g));
	memcpy(c->pa_5gh, &pa5ga[8], sizeof(c->pa_5gh));
	return (0);
}

static int
bwn_nphy_get_core_power_info_r4_r10(struct bwn_softc *sc,
    const struct bwn_nphy_power_vars *v, struct bwn_phy_n_core_pwr_info *c)
{
	int error;

	/* BHND_NVAR_ITT2GA[core] */
	error = bhnd_nvram_getvar_uint8(sc->sc_dev, v->itt2ga, &c->itssi_2g);
	if (error)
		return (error);

	/* BHND_NVAR_ITT5GA[core] */
	error = bhnd_nvram_getvar_uint8(sc->sc_dev, v->itt5ga, &c->itssi_5g);
	if (error)
		return (error);

	return (0);
}

/*
 * siba_sprom_get_core_power_info()
 *
 * Referenced by:
 *   bwn_nphy_tx_power_ctl_setup()
 *   bwn_ppr_load_max_from_sprom()
 */
int
bwn_nphy_get_core_power_info(struct bwn_mac *mac, int core,
    struct bwn_phy_n_core_pwr_info *c)
{
	struct bwn_softc			*sc;
	const struct bwn_nphy_power_vars	*v;
	uint8_t					 sromrev;
	int					 error;

	sc = mac->mac_sc;

	if (core < 0 || core >= nitems(bwn_nphy_power_vars))
		return (EINVAL);

	sromrev = sc->sc_board_info.board_srom_rev;
	if (sromrev < 4)
		return (ENXIO);

	v = &bwn_nphy_power_vars[core];

	/* Any power variables not found in NVRAM (or returning a
	 * shorter array for a particular NVRAM revision) should be zero
	 * initialized */
	memset(c, 0x0, sizeof(*c));

	/* Populate SPROM revision-independent values */
	error = bhnd_nvram_getvar_uint8(sc->sc_dev, v->maxp2ga, &c->maxpwr_2g);
	if (error)
		return (error);

	/* Populate SPROM revision-specific values */
	if (sromrev >= 4 && sromrev <= 10)
		return (bwn_nphy_get_core_power_info_r4_r10(sc, v, c));
	else
		return (bwn_nphy_get_core_power_info_r11(sc, v, c));
}
