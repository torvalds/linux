/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@FreeBSD.org>
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

/*
 * This implements some very basic bluetooth coexistence methods for
 * the ath(4) hardware.
 */
#include "opt_ath.h"
#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/errno.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/bus.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>		/* XXX for ether_sprintf */

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/if_ath_btcoex.h>
#include <dev/ath/if_ath_btcoex_mci.h>

MALLOC_DECLARE(M_ATHDEV);

/*
 * Initial AR9285 / (WB195) bluetooth coexistence settings,
 * just for experimentation.
 *
 * Return 0 for OK; errno for error.
 *
 * XXX TODO: There needs to be a PCIe workaround to disable ASPM if
 * bluetooth coexistence is enabled.
 */
static int
ath_btcoex_cfg_wb195(struct ath_softc *sc)
{
	HAL_BT_COEX_INFO btinfo;
	HAL_BT_COEX_CONFIG btconfig;
	struct ath_hal *ah = sc->sc_ah;

	if (! ath_hal_btcoex_supported(ah))
		return (EINVAL);

	bzero(&btinfo, sizeof(btinfo));
	bzero(&btconfig, sizeof(btconfig));

	device_printf(sc->sc_dev, "Enabling WB195 BTCOEX\n");

	btinfo.bt_module = HAL_BT_MODULE_JANUS;
	btinfo.bt_coex_config = HAL_BT_COEX_CFG_3WIRE;
	/*
	 * These are the three GPIO pins hooked up between the AR9285 and
	 * the AR3011.
	 */
	btinfo.bt_gpio_bt_active = 6;
	btinfo.bt_gpio_bt_priority = 7;
	btinfo.bt_gpio_wlan_active = 5;
	btinfo.bt_active_polarity = 1;	/* XXX not used */
	btinfo.bt_single_ant = 1;	/* 1 antenna on ar9285 ? */
	btinfo.bt_isolation = 0;	/* in dB, not used */

	ath_hal_btcoex_set_info(ah, &btinfo);

	btconfig.bt_time_extend = 0;
	btconfig.bt_txstate_extend = 1;	/* true */
	btconfig.bt_txframe_extend = 1;	/* true */
	btconfig.bt_mode = HAL_BT_COEX_MODE_SLOTTED;
	btconfig.bt_quiet_collision = 1;	/* true */
	btconfig.bt_rxclear_polarity = 1;	/* true */
	btconfig.bt_priority_time = 2;
	btconfig.bt_first_slot_time = 5;
	btconfig.bt_hold_rxclear = 1;	/* true */

	ath_hal_btcoex_set_config(ah, &btconfig);

	/*
	 * Enable antenna diversity.
	 */
	ath_hal_btcoex_set_parameter(ah, HAL_BT_COEX_ANTENNA_DIVERSITY, 1);

	return (0);
}

/*
 * Initial AR9485 / (WB225) bluetooth coexistence settings,
 * just for experimentation.
 *
 * Return 0 for OK; errno for error.
 */
static int
ath_btcoex_cfg_wb225(struct ath_softc *sc)
{
	HAL_BT_COEX_INFO btinfo;
	HAL_BT_COEX_CONFIG btconfig;
	struct ath_hal *ah = sc->sc_ah;

	if (! ath_hal_btcoex_supported(ah))
		return (EINVAL);

	bzero(&btinfo, sizeof(btinfo));
	bzero(&btconfig, sizeof(btconfig));

	device_printf(sc->sc_dev, "Enabling WB225 BTCOEX\n");

	btinfo.bt_module = HAL_BT_MODULE_JANUS;	/* XXX not used? */
	btinfo.bt_coex_config = HAL_BT_COEX_CFG_3WIRE;
	/*
	 * These are the three GPIO pins hooked up between the AR9485 and
	 * the bluetooth module.
	 */
	btinfo.bt_gpio_bt_active = 4;
	btinfo.bt_gpio_bt_priority = 8;
	btinfo.bt_gpio_wlan_active = 5;

	btinfo.bt_active_polarity = 1;	/* XXX not used */
	btinfo.bt_single_ant = 1;	/* 1 antenna on ar9285 ? */
	btinfo.bt_isolation = 0;	/* in dB, not used */

	ath_hal_btcoex_set_info(ah, &btinfo);

	btconfig.bt_time_extend = 0;
	btconfig.bt_txstate_extend = 1;	/* true */
	btconfig.bt_txframe_extend = 1;	/* true */
	btconfig.bt_mode = HAL_BT_COEX_MODE_SLOTTED;
	btconfig.bt_quiet_collision = 1;	/* true */
	btconfig.bt_rxclear_polarity = 1;	/* true */
	btconfig.bt_priority_time = 2;
	btconfig.bt_first_slot_time = 5;
	btconfig.bt_hold_rxclear = 1;	/* true */

	ath_hal_btcoex_set_config(ah, &btconfig);

	/*
	 * Enable antenna diversity.
	 */
	ath_hal_btcoex_set_parameter(ah, HAL_BT_COEX_ANTENNA_DIVERSITY, 1);

	return (0);
}

static int
ath_btcoex_cfg_mci(struct ath_softc *sc, uint32_t mci_cfg, int do_btdiv)
{
	HAL_BT_COEX_INFO btinfo;
	HAL_BT_COEX_CONFIG btconfig;
	struct ath_hal *ah = sc->sc_ah;

	if (! ath_hal_btcoex_supported(ah))
		return (EINVAL);

	bzero(&btinfo, sizeof(btinfo));
	bzero(&btconfig, sizeof(btconfig));

	sc->sc_ah->ah_config.ath_hal_mci_config = mci_cfg;

	if (ath_btcoex_mci_attach(sc) != 0) {
		device_printf(sc->sc_dev, "Failed to setup btcoex\n");
		return (EINVAL);
	}

	btinfo.bt_module = HAL_BT_MODULE_JANUS;	/* XXX not used? */
	btinfo.bt_coex_config = HAL_BT_COEX_CFG_MCI;

	/*
	 * MCI uses a completely different interface to speak
	 * to the bluetooth module - it's a command based
	 * thing over a serial line, rather than
	 * state pins to/from the bluetooth module.
	 *
	 * So, the GPIO configuration, polarity, etc
	 * doesn't matter on MCI devices; it's just
	 * completely ignored by the HAL.
	 */
	btinfo.bt_gpio_bt_active = 4;
	btinfo.bt_gpio_bt_priority = 8;
	btinfo.bt_gpio_wlan_active = 5;

	btinfo.bt_active_polarity = 1;	/* XXX not used */
	btinfo.bt_single_ant = 0;	/* 2 antenna on WB335 */
	btinfo.bt_isolation = 0;	/* in dB, not used */

	ath_hal_btcoex_set_info(ah, &btinfo);

	btconfig.bt_time_extend = 0;
	btconfig.bt_txstate_extend = 1;	/* true */
	btconfig.bt_txframe_extend = 1;	/* true */
	btconfig.bt_mode = HAL_BT_COEX_MODE_SLOTTED;
	btconfig.bt_quiet_collision = 1;	/* true */
	btconfig.bt_rxclear_polarity = 1;	/* true */
	btconfig.bt_priority_time = 2;
	btconfig.bt_first_slot_time = 5;
	btconfig.bt_hold_rxclear = 1;	/* true */

	ath_hal_btcoex_set_config(ah, &btconfig);

	/* Enable */
	ath_hal_btcoex_enable(sc->sc_ah);

	/* Stomp */
	ath_hal_btcoex_set_weights(ah, HAL_BT_COEX_STOMP_NONE);

	/*
	 * Enable antenna diversity.
	 */
	ath_hal_btcoex_set_parameter(ah, HAL_BT_COEX_ANTENNA_DIVERSITY,
	    do_btdiv);

	return (0);
}

/*
 * Initial AR9462 / (WB222) bluetooth coexistence settings.
 *
 * Return 0 for OK; errno for error.
 */
static int
ath_btcoex_cfg_wb222(struct ath_softc *sc)
{

	device_printf(sc->sc_dev, "Enabling WB222 BTCOEX\n");
	/* XXX from ath9k */
	return (ath_btcoex_cfg_mci(sc, 0x2201, 1));
}

/*
 * Initial QCA9565 / (WB335B) bluetooth coexistence settings.
 *
 * Return 0 for OK; errno for error.
 */
static int
ath_btcoex_cfg_wb335b(struct ath_softc *sc)
{
	uint32_t flags;
	int do_btdiv = 0;

	/* ath9k default */
	flags = 0xa4c1;

	/* 1-ant and 2-ant AR9565 */
	/*
	 * XXX TODO: ensure these actually make it down to the
	 * HAL correctly!
	 */
	if (sc->sc_pci_devinfo & ATH_PCI_AR9565_1ANT) {
		flags &= ~ATH_MCI_CONFIG_ANT_ARCH;
		flags |= ATH_MCI_ANT_ARCH_1_ANT_PA_LNA_SHARED <<
		    ATH_MCI_CONFIG_ANT_ARCH_S;
	} else if (sc->sc_pci_devinfo & ATH_PCI_AR9565_2ANT) {
		flags &= ~ATH_MCI_CONFIG_ANT_ARCH;
		flags |= ATH_MCI_ANT_ARCH_2_ANT_PA_LNA_NON_SHARED <<
		    ATH_MCI_CONFIG_ANT_ARCH_S;
	}

	if (sc->sc_pci_devinfo & ATH_PCI_BT_ANT_DIV) {
		do_btdiv = 1;
	}

	device_printf(sc->sc_dev, "Enabling WB335 BTCOEX\n");
	/* XXX from ath9k */
	return (ath_btcoex_cfg_mci(sc, flags, do_btdiv));
}

#if 0
/*
 * When using bluetooth coexistence, ASPM needs to be disabled
 * otherwise the sleeping interferes with the bluetooth (USB)
 * operation and the MAC sleep/wakeup hardware.
 *
 * The PCIe powersave routine also needs to not be called
 * by the driver during suspend/resume, else things will get
 * a little odd.  Check Linux ath9k for more details.
 */
static int
ath_btcoex_aspm_wb195(struct ath_softc *sc)
{

	/* XXX TODO: clear device ASPM L0S and L1 */
	/* XXX TODO: clear _parent_ ASPM L0S and L1 */
}
#endif

/*
 * Methods which are required
 */

/*
 * Attach btcoex to the given interface
 */
int
ath_btcoex_attach(struct ath_softc *sc)
{
	int ret;
	struct ath_hal *ah = sc->sc_ah;
	const char *profname;

	/*
	 * No chipset bluetooth coexistence? Then do nothing.
	 */
	if (! ath_hal_btcoex_supported(ah))
		return (0);

	/*
	 * Look at the hints to determine which bluetooth
	 * profile to configure.
	 */
	ret = resource_string_value(device_get_name(sc->sc_dev),
	    device_get_unit(sc->sc_dev),
	    "btcoex_profile",
	    &profname);
	if (ret != 0) {
		/* nothing to do */
		return (0);
	}

	if (strncmp(profname, "wb195", 5) == 0) {
		ret = ath_btcoex_cfg_wb195(sc);
	} else if (strncmp(profname, "wb222", 5) == 0) {
		ret = ath_btcoex_cfg_wb222(sc);
	} else if (strncmp(profname, "wb225", 5) == 0) {
		ret = ath_btcoex_cfg_wb225(sc);
	} else if (strncmp(profname, "wb335", 5) == 0) {
		ret = ath_btcoex_cfg_wb335b(sc);
	} else {
		return (0);
	}

	/*
	 * Propagate up failure from the actual attach phase.
	 */
	if (ret != 0)
		return (ret);

	return (0);
}

/*
 * Detach btcoex from the given interface
 */
int
ath_btcoex_detach(struct ath_softc *sc)
{
	if (sc->sc_btcoex_mci) {
		ath_btcoex_mci_detach(sc);
	}

	return (0);
}

/*
 * Configure or disable bluetooth coexistence on the given channel.
 *
 * For AR9285/AR9287/AR9485, we'll never see a 5GHz channel, so we just
 * assume bluetooth coexistence is always on.
 *
 * For AR9462, we may see a 5GHz channel; bluetooth coexistence should
 * not be enabled on those channels.
 */
int
ath_btcoex_enable(struct ath_softc *sc, const struct ieee80211_channel *chan)
{
	if (sc->sc_btcoex_mci) {
		ath_btcoex_mci_enable(sc, chan);
	}

	return (0);
}

/*
 * Handle ioctl requests from the diagnostic interface.
 *
 * The initial part of this code resembles ath_ioctl_diag();
 * it's likely a good idea to reduce duplication between
 * these two routines.
 */
int
ath_btcoex_ioctl(struct ath_softc *sc, struct ath_diag *ad)
{
	unsigned int id = ad->ad_id & ATH_DIAG_ID;
	void *indata = NULL;
	void *outdata = NULL;
	u_int32_t insize = ad->ad_in_size;
	u_int32_t outsize = ad->ad_out_size;
	int error = 0;
//	int val;

	if (ad->ad_id & ATH_DIAG_IN) {
		/*
		 * Copy in data.
		 */
		indata = malloc(insize, M_TEMP, M_NOWAIT);
		if (indata == NULL) {
			error = ENOMEM;
			goto bad;
		}
		error = copyin(ad->ad_in_data, indata, insize);
		if (error)
			goto bad;
	}
	if (ad->ad_id & ATH_DIAG_DYN) {
		/*
		 * Allocate a buffer for the results (otherwise the HAL
		 * returns a pointer to a buffer where we can read the
		 * results).  Note that we depend on the HAL leaving this
		 * pointer for us to use below in reclaiming the buffer;
		 * may want to be more defensive.
		 */
		outdata = malloc(outsize, M_TEMP, M_NOWAIT | M_ZERO);
		if (outdata == NULL) {
			error = ENOMEM;
			goto bad;
		}
	}
	switch (id) {
		default:
			error = EINVAL;
			goto bad;
	}
	if (outsize < ad->ad_out_size)
		ad->ad_out_size = outsize;
	if (outdata && copyout(outdata, ad->ad_out_data, ad->ad_out_size))
		error = EFAULT;
bad:
	if ((ad->ad_id & ATH_DIAG_IN) && indata != NULL)
		free(indata, M_TEMP);
	if ((ad->ad_id & ATH_DIAG_DYN) && outdata != NULL)
		free(outdata, M_TEMP);
	return (error);
}

