/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "efx.h"
#include "efx_impl.h"

	__checkReturn	efx_rc_t
efx_port_init(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_NIC);

	if (enp->en_mod_flags & EFX_MOD_PORT) {
		rc = EINVAL;
		goto fail1;
	}

	enp->en_mod_flags |= EFX_MOD_PORT;

	epp->ep_mac_type = EFX_MAC_INVALID;
	epp->ep_link_mode = EFX_LINK_UNKNOWN;
	epp->ep_mac_drain = B_TRUE;

	/* Configure the MAC */
	if ((rc = efx_mac_select(enp)) != 0)
		goto fail1;

	epp->ep_emop->emo_reconfigure(enp);

	/* Pick up current phy capababilities */
	(void) efx_port_poll(enp, NULL);

	/*
	 * Turn on the PHY if available, otherwise reset it, and
	 * reconfigure it with the current configuration.
	 */
	if (epop->epo_power != NULL) {
		if ((rc = epop->epo_power(enp, B_TRUE)) != 0)
			goto fail2;
	} else {
		if ((rc = epop->epo_reset(enp)) != 0)
			goto fail2;
	}

	EFSYS_ASSERT(enp->en_reset_flags & EFX_RESET_PHY);
	enp->en_reset_flags &= ~EFX_RESET_PHY;

	if ((rc = epop->epo_reconfigure(enp)) != 0)
		goto fail3;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	enp->en_mod_flags &= ~EFX_MOD_PORT;

	return (rc);
}

	__checkReturn	efx_rc_t
efx_port_poll(
	__in		efx_nic_t *enp,
	__out_opt	efx_link_mode_t	*link_modep)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_link_mode_t ignore_link_mode;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	EFSYS_ASSERT(emop != NULL);

	if (link_modep == NULL)
		link_modep = &ignore_link_mode;

	if ((rc = emop->emo_poll(enp, link_modep)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#if EFSYS_OPT_LOOPBACK

	__checkReturn	efx_rc_t
efx_port_loopback_set(
	__in		efx_nic_t *enp,
	__in		efx_link_mode_t link_mode,
	__in		efx_loopback_type_t loopback_type)
{
	efx_port_t *epp = &(enp->en_port);
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);
	EFSYS_ASSERT(emop != NULL);

	EFSYS_ASSERT(link_mode < EFX_LINK_NMODES);

	if (EFX_TEST_QWORD_BIT(encp->enc_loopback_types[link_mode],
		(int)loopback_type) == 0) {
		rc = ENOTSUP;
		goto fail1;
	}

	if (epp->ep_loopback_type == loopback_type &&
	    epp->ep_loopback_link_mode == link_mode)
		return (0);

	if ((rc = emop->emo_loopback_set(enp, link_mode, loopback_type)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#if EFSYS_OPT_NAMES

static const char * const __efx_loopback_type_name[] = {
	"OFF",
	"DATA",
	"GMAC",
	"XGMII",
	"XGXS",
	"XAUI",
	"GMII",
	"SGMII",
	"XGBR",
	"XFI",
	"XAUI_FAR",
	"GMII_FAR",
	"SGMII_FAR",
	"XFI_FAR",
	"GPHY",
	"PHY_XS",
	"PCS",
	"PMA_PMD",
	"XPORT",
	"XGMII_WS",
	"XAUI_WS",
	"XAUI_WS_FAR",
	"XAUI_WS_NEAR",
	"GMII_WS",
	"XFI_WS",
	"XFI_WS_FAR",
	"PHYXS_WS",
	"PMA_INT",
	"SD_NEAR",
	"SD_FAR",
	"PMA_INT_WS",
	"SD_FEP2_WS",
	"SD_FEP1_5_WS",
	"SD_FEP_WS",
	"SD_FES_WS",
	"AOE_INT_NEAR",
	"DATA_WS",
	"FORCE_EXT_LINK",
};

	__checkReturn	const char *
efx_loopback_type_name(
	__in		efx_nic_t *enp,
	__in		efx_loopback_type_t type)
{
	EFX_STATIC_ASSERT(EFX_ARRAY_SIZE(__efx_loopback_type_name) ==
	    EFX_LOOPBACK_NTYPES);

	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(type, <, EFX_LOOPBACK_NTYPES);

	return (__efx_loopback_type_name[type]);
}

#endif	/* EFSYS_OPT_NAMES */

#endif	/* EFSYS_OPT_LOOPBACK */

			void
efx_port_fini(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_NIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	EFSYS_ASSERT(epp->ep_mac_drain);

	epp->ep_emop = NULL;
	epp->ep_mac_type = EFX_MAC_INVALID;
	epp->ep_mac_drain = B_FALSE;

	/* Turn off the PHY */
	if (epop->epo_power != NULL)
		(void) epop->epo_power(enp, B_FALSE);

	enp->en_mod_flags &= ~EFX_MOD_PORT;
}
