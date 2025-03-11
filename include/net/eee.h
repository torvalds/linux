/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _EEE_H
#define _EEE_H

#include <linux/types.h>

struct eee_config {
	u32 tx_lpi_timer;
	bool tx_lpi_enabled;
	bool eee_enabled;
};

static inline bool eeecfg_mac_can_tx_lpi(const struct eee_config *eeecfg)
{
	/* eee_enabled is the master on/off */
	return eeecfg->eee_enabled && eeecfg->tx_lpi_enabled;
}

static inline void eeecfg_to_eee(struct ethtool_keee *eee,
				 const struct eee_config *eeecfg)
{
	eee->tx_lpi_timer = eeecfg->tx_lpi_timer;
	eee->tx_lpi_enabled = eeecfg->tx_lpi_enabled;
	eee->eee_enabled = eeecfg->eee_enabled;
}

static inline void eee_to_eeecfg(struct eee_config *eeecfg,
				 const struct ethtool_keee *eee)
{
	eeecfg->tx_lpi_timer = eee->tx_lpi_timer;
	eeecfg->tx_lpi_enabled = eee->tx_lpi_enabled;
	eeecfg->eee_enabled = eee->eee_enabled;
}

#endif
