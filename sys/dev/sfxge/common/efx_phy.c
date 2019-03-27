/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2016 Solarflare Communications Inc.
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


#if EFSYS_OPT_SIENA
static const efx_phy_ops_t	__efx_phy_siena_ops = {
	siena_phy_power,		/* epo_power */
	NULL,				/* epo_reset */
	siena_phy_reconfigure,		/* epo_reconfigure */
	siena_phy_verify,		/* epo_verify */
	siena_phy_oui_get,		/* epo_oui_get */
	NULL,				/* epo_link_state_get */
#if EFSYS_OPT_PHY_STATS
	siena_phy_stats_update,		/* epo_stats_update */
#endif	/* EFSYS_OPT_PHY_STATS */
#if EFSYS_OPT_BIST
	NULL,				/* epo_bist_enable_offline */
	siena_phy_bist_start,		/* epo_bist_start */
	siena_phy_bist_poll,		/* epo_bist_poll */
	siena_phy_bist_stop,		/* epo_bist_stop */
#endif	/* EFSYS_OPT_BIST */
};
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
static const efx_phy_ops_t	__efx_phy_ef10_ops = {
	ef10_phy_power,			/* epo_power */
	NULL,				/* epo_reset */
	ef10_phy_reconfigure,		/* epo_reconfigure */
	ef10_phy_verify,		/* epo_verify */
	ef10_phy_oui_get,		/* epo_oui_get */
	ef10_phy_link_state_get,	/* epo_link_state_get */
#if EFSYS_OPT_PHY_STATS
	ef10_phy_stats_update,		/* epo_stats_update */
#endif	/* EFSYS_OPT_PHY_STATS */
#if EFSYS_OPT_BIST
	ef10_bist_enable_offline,	/* epo_bist_enable_offline */
	ef10_bist_start,		/* epo_bist_start */
	ef10_bist_poll,			/* epo_bist_poll */
	ef10_bist_stop,			/* epo_bist_stop */
#endif	/* EFSYS_OPT_BIST */
};
#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

	__checkReturn	efx_rc_t
efx_phy_probe(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	const efx_phy_ops_t *epop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	epp->ep_port = encp->enc_port;
	epp->ep_phy_type = encp->enc_phy_type;

	/* Hook in operations structure */
	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		epop = &__efx_phy_siena_ops;
		break;
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		epop = &__efx_phy_ef10_ops;
		break;
#endif	/* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		epop = &__efx_phy_ef10_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		epop = &__efx_phy_ef10_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD2 */

	default:
		rc = ENOTSUP;
		goto fail1;
	}

	epp->ep_epop = epop;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	epp->ep_port = 0;
	epp->ep_phy_type = 0;

	return (rc);
}

	__checkReturn	efx_rc_t
efx_phy_verify(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	return (epop->epo_verify(enp));
}

#if EFSYS_OPT_PHY_LED_CONTROL

	__checkReturn	efx_rc_t
efx_phy_led_set(
	__in		efx_nic_t *enp,
	__in		efx_phy_led_mode_t mode)
{
	efx_nic_cfg_t *encp = (&enp->en_nic_cfg);
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	uint32_t mask;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if (epp->ep_phy_led_mode == mode)
		goto done;

	mask = (1 << EFX_PHY_LED_DEFAULT);
	mask |= encp->enc_led_mask;

	if (!((1 << mode) & mask)) {
		rc = ENOTSUP;
		goto fail1;
	}

	EFSYS_ASSERT3U(mode, <, EFX_PHY_LED_NMODES);
	epp->ep_phy_led_mode = mode;

	if ((rc = epop->epo_reconfigure(enp)) != 0)
		goto fail2;

done:
	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}
#endif	/* EFSYS_OPT_PHY_LED_CONTROL */

			void
efx_phy_adv_cap_get(
	__in		efx_nic_t *enp,
	__in		uint32_t flag,
	__out		uint32_t *maskp)
{
	efx_port_t *epp = &(enp->en_port);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);

	switch (flag) {
	case EFX_PHY_CAP_CURRENT:
		*maskp = epp->ep_adv_cap_mask;
		break;
	case EFX_PHY_CAP_DEFAULT:
		*maskp = epp->ep_default_adv_cap_mask;
		break;
	case EFX_PHY_CAP_PERM:
		*maskp = epp->ep_phy_cap_mask;
		break;
	default:
		EFSYS_ASSERT(B_FALSE);
		*maskp = 0;
		break;
	}
}

	__checkReturn	efx_rc_t
efx_phy_adv_cap_set(
	__in		efx_nic_t *enp,
	__in		uint32_t mask)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	uint32_t old_mask;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if ((mask & ~epp->ep_phy_cap_mask) != 0) {
		rc = ENOTSUP;
		goto fail1;
	}

	if (epp->ep_adv_cap_mask == mask)
		goto done;

	old_mask = epp->ep_adv_cap_mask;
	epp->ep_adv_cap_mask = mask;

	if ((rc = epop->epo_reconfigure(enp)) != 0)
		goto fail2;

done:
	return (0);

fail2:
	EFSYS_PROBE(fail2);

	epp->ep_adv_cap_mask = old_mask;
	/* Reconfigure for robustness */
	if (epop->epo_reconfigure(enp) != 0) {
		/*
		 * We may have an inconsistent view of our advertised speed
		 * capabilities.
		 */
		EFSYS_ASSERT(0);
	}

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	void
efx_phy_lp_cap_get(
	__in		efx_nic_t *enp,
	__out		uint32_t *maskp)
{
	efx_port_t *epp = &(enp->en_port);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	*maskp = epp->ep_lp_cap_mask;
}

	__checkReturn	efx_rc_t
efx_phy_oui_get(
	__in		efx_nic_t *enp,
	__out		uint32_t *ouip)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	return (epop->epo_oui_get(enp, ouip));
}

			void
efx_phy_media_type_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_media_type_t *typep)
{
	efx_port_t *epp = &(enp->en_port);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if (epp->ep_module_type != EFX_PHY_MEDIA_INVALID)
		*typep = epp->ep_module_type;
	else
		*typep = epp->ep_fixed_port_type;
}

	__checkReturn		efx_rc_t
efx_phy_module_get_info(
	__in			efx_nic_t *enp,
	__in			uint8_t dev_addr,
	__in			size_t offset,
	__in			size_t len,
	__out_bcount(len)	uint8_t *data)
{
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT(data != NULL);

	if ((offset > EFX_PHY_MEDIA_INFO_MAX_OFFSET) ||
	    ((offset + len) > EFX_PHY_MEDIA_INFO_MAX_OFFSET)) {
		rc = EINVAL;
		goto fail1;
	}

	if ((rc = efx_mcdi_phy_module_get_info(enp, dev_addr,
	    offset, len, data)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_phy_fec_type_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_fec_type_t *typep)
{
	efx_rc_t rc;
	efx_phy_link_state_t epls;

	if ((rc = efx_phy_link_state_get(enp, &epls)) != 0)
		goto fail1;

	*typep = epls.epls_fec;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_phy_link_state_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_link_state_t *eplsp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);

	if (epop->epo_link_state_get == NULL) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = epop->epo_link_state_get(enp, eplsp)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#if EFSYS_OPT_PHY_STATS

#if EFSYS_OPT_NAMES

/* START MKCONFIG GENERATED PhyStatNamesBlock af9ffa24da3bc100 */
static const char * const __efx_phy_stat_name[] = {
	"oui",
	"pma_pmd_link_up",
	"pma_pmd_rx_fault",
	"pma_pmd_tx_fault",
	"pma_pmd_rev_a",
	"pma_pmd_rev_b",
	"pma_pmd_rev_c",
	"pma_pmd_rev_d",
	"pcs_link_up",
	"pcs_rx_fault",
	"pcs_tx_fault",
	"pcs_ber",
	"pcs_block_errors",
	"phy_xs_link_up",
	"phy_xs_rx_fault",
	"phy_xs_tx_fault",
	"phy_xs_align",
	"phy_xs_sync_a",
	"phy_xs_sync_b",
	"phy_xs_sync_c",
	"phy_xs_sync_d",
	"an_link_up",
	"an_master",
	"an_local_rx_ok",
	"an_remote_rx_ok",
	"cl22ext_link_up",
	"snr_a",
	"snr_b",
	"snr_c",
	"snr_d",
	"pma_pmd_signal_a",
	"pma_pmd_signal_b",
	"pma_pmd_signal_c",
	"pma_pmd_signal_d",
	"an_complete",
	"pma_pmd_rev_major",
	"pma_pmd_rev_minor",
	"pma_pmd_rev_micro",
	"pcs_fw_version_0",
	"pcs_fw_version_1",
	"pcs_fw_version_2",
	"pcs_fw_version_3",
	"pcs_fw_build_yy",
	"pcs_fw_build_mm",
	"pcs_fw_build_dd",
	"pcs_op_mode",
};

/* END MKCONFIG GENERATED PhyStatNamesBlock */

					const char *
efx_phy_stat_name(
	__in				efx_nic_t *enp,
	__in				efx_phy_stat_t type)
{
	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(type, <, EFX_PHY_NSTATS);

	return (__efx_phy_stat_name[type]);
}

#endif	/* EFSYS_OPT_NAMES */

	__checkReturn			efx_rc_t
efx_phy_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_PHY_NSTATS)	uint32_t *stat)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	return (epop->epo_stats_update(enp, esmp, stat));
}

#endif	/* EFSYS_OPT_PHY_STATS */


#if EFSYS_OPT_BIST

	__checkReturn		efx_rc_t
efx_bist_enable_offline(
	__in			efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	if (epop->epo_bist_enable_offline == NULL) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = epop->epo_bist_enable_offline(enp)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);

}

	__checkReturn		efx_rc_t
efx_bist_start(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT3U(type, !=, EFX_BIST_TYPE_UNKNOWN);
	EFSYS_ASSERT3U(type, <, EFX_BIST_TYPE_NTYPES);
	EFSYS_ASSERT3U(epp->ep_current_bist, ==, EFX_BIST_TYPE_UNKNOWN);

	if (epop->epo_bist_start == NULL) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = epop->epo_bist_start(enp, type)) != 0)
		goto fail2;

	epp->ep_current_bist = type;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
efx_bist_poll(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type,
	__out			efx_bist_result_t *resultp,
	__out_opt		uint32_t *value_maskp,
	__out_ecount_opt(count)	unsigned long *valuesp,
	__in			size_t count)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT3U(type, !=, EFX_BIST_TYPE_UNKNOWN);
	EFSYS_ASSERT3U(type, <, EFX_BIST_TYPE_NTYPES);
	EFSYS_ASSERT3U(epp->ep_current_bist, ==, type);

	EFSYS_ASSERT(epop->epo_bist_poll != NULL);
	if (epop->epo_bist_poll == NULL) {
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = epop->epo_bist_poll(enp, type, resultp, value_maskp,
	    valuesp, count)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
efx_bist_stop(
	__in		efx_nic_t *enp,
	__in		efx_bist_type_t type)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT3U(type, !=, EFX_BIST_TYPE_UNKNOWN);
	EFSYS_ASSERT3U(type, <, EFX_BIST_TYPE_NTYPES);
	EFSYS_ASSERT3U(epp->ep_current_bist, ==, type);

	EFSYS_ASSERT(epop->epo_bist_stop != NULL);

	if (epop->epo_bist_stop != NULL)
		epop->epo_bist_stop(enp, type);

	epp->ep_current_bist = EFX_BIST_TYPE_UNKNOWN;
}

#endif	/* EFSYS_OPT_BIST */
			void
efx_phy_unprobe(
	__in	efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	epp->ep_epop = NULL;

	epp->ep_adv_cap_mask = 0;

	epp->ep_port = 0;
	epp->ep_phy_type = 0;
}
