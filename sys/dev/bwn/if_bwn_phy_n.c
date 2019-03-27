/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This is the top-level N-PHY support for the Broadcom softmac driver.
 */

#include "opt_bwn.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/firmware.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/bwn/if_bwnreg.h>
#include <dev/bwn/if_bwnvar.h>

#include <dev/bwn/if_bwn_debug.h>
#include <dev/bwn/if_bwn_misc.h>
#include <dev/bwn/if_bwn_phy_n.h>

#ifdef	BWN_GPL_PHY
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_tables.h>
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_ppr.h>
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_core.h>
#endif

/*
 * This module is always compiled into the kernel, regardless of
 * whether the GPL PHY is enabled.  If the GPL PHY isn't enabled
 * then it'll just be stubs that will fail to attach.
 */

int
bwn_phy_n_attach(struct bwn_mac *mac)
{

#ifdef	BWN_GPL_PHY
	return bwn_nphy_op_allocate(mac);
#else
	device_printf(mac->mac_sc->sc_dev,
	    "%s: BWN_GPL_PHY not in kernel config; "
	    "no PHY-N support\n", __func__);
	return (ENXIO);
#endif
}

void
bwn_phy_n_detach(struct bwn_mac *mac)
{

#ifdef	BWN_GPL_PHY
	return bwn_nphy_op_free(mac);
#endif
}

int
bwn_phy_n_prepare_hw(struct bwn_mac *mac)
{

#ifdef	BWN_GPL_PHY
	return (bwn_nphy_op_prepare_structs(mac));
#else
	return (ENXIO);
#endif
}

void
bwn_phy_n_init_pre(struct bwn_mac *mac)
{

	/* XXX TODO */
}

int
bwn_phy_n_init(struct bwn_mac *mac)
{
#ifdef	BWN_GPL_PHY
	return bwn_nphy_op_init(mac);
#else
	return (ENXIO);
#endif
}

void
bwn_phy_n_exit(struct bwn_mac *mac)
{

	/* XXX TODO */
}

uint16_t
bwn_phy_n_read(struct bwn_mac *mac, uint16_t reg)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	return BWN_READ_2(mac, BWN_PHYDATA);
}

void
bwn_phy_n_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	BWN_WRITE_2(mac, BWN_PHYDATA, value);
}

uint16_t
bwn_phy_n_rf_read(struct bwn_mac *mac, uint16_t reg)
{

	/* Register 1 is a 32-bit register. */
	if (mac->mac_phy.rev < 7 && reg == 1) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: bad reg access\n", __func__);
	}

	if (mac->mac_phy.rev >= 7)
		reg |= 0x200;	 /* radio 0x2057 */
	else
		reg |= 0x100;

	BWN_WRITE_2(mac, BWN_RFCTL, reg);
	return BWN_READ_2(mac, BWN_RFDATALO);
}

void
bwn_phy_n_rf_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{

	/* Register 1 is a 32-bit register. */
	if (mac->mac_phy.rev < 7 && reg == 1) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: bad reg access\n", __func__);
	}

	BWN_WRITE_2(mac, BWN_RFCTL, reg);
	BWN_WRITE_2(mac, BWN_RFDATALO, value);
}

int
bwn_phy_n_hwpctl(struct bwn_mac *mac)
{

	return (0);
}

void
bwn_phy_n_rf_onoff(struct bwn_mac *mac, int on)
{
#ifdef	BWN_GPL_PHY
	bwn_nphy_op_software_rfkill(mac, on);
#endif
}

void
bwn_phy_n_switch_analog(struct bwn_mac *mac, int on)
{
#ifdef	BWN_GPL_PHY
	bwn_nphy_op_switch_analog(mac, on);
#endif
}

int
bwn_phy_n_switch_channel(struct bwn_mac *mac, uint32_t newchan)
{
#ifdef	BWN_GPL_PHY
	return bwn_nphy_op_switch_channel(mac, newchan);
#else
	return (ENXIO);
#endif
}

uint32_t
bwn_phy_n_get_default_chan(struct bwn_mac *mac)
{

	if (bwn_current_band(mac) == BWN_BAND_2G)
		return (1);
	return (36);
}

void
bwn_phy_n_set_antenna(struct bwn_mac *mac, int antenna)
{
	/* XXX TODO */
}

int
bwn_phy_n_im(struct bwn_mac *mac, int mode)
{
	/* XXX TODO */
	return (0);
}

bwn_txpwr_result_t
bwn_phy_n_recalc_txpwr(struct bwn_mac *mac, int ignore_tssi)
{
#ifdef	BWN_GPL_PHY
	return bwn_nphy_op_recalc_txpower(mac, ignore_tssi);
#else
	return (BWN_TXPWR_RES_DONE);
#endif
}

void
bwn_phy_n_set_txpwr(struct bwn_mac *mac)
{

}

void
bwn_phy_n_task_15s(struct bwn_mac *mac)
{

}

void
bwn_phy_n_task_60s(struct bwn_mac *mac)
{

}
