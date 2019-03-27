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

static	__checkReturn	efx_rc_t
siena_mac_multicast_list_set(
	__in		efx_nic_t *enp);

#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_SIENA
static const efx_mac_ops_t	__efx_mac_siena_ops = {
	siena_mac_poll,				/* emo_poll */
	siena_mac_up,				/* emo_up */
	siena_mac_reconfigure,			/* emo_addr_set */
	siena_mac_reconfigure,			/* emo_pdu_set */
	siena_mac_pdu_get,			/* emo_pdu_get */
	siena_mac_reconfigure,			/* emo_reconfigure */
	siena_mac_multicast_list_set,		/* emo_multicast_list_set */
	NULL,					/* emo_filter_set_default_rxq */
	NULL,				/* emo_filter_default_rxq_clear */
#if EFSYS_OPT_LOOPBACK
	siena_mac_loopback_set,			/* emo_loopback_set */
#endif	/* EFSYS_OPT_LOOPBACK */
#if EFSYS_OPT_MAC_STATS
	siena_mac_stats_get_mask,		/* emo_stats_get_mask */
	efx_mcdi_mac_stats_clear,		/* emo_stats_clear */
	efx_mcdi_mac_stats_upload,		/* emo_stats_upload */
	efx_mcdi_mac_stats_periodic,		/* emo_stats_periodic */
	siena_mac_stats_update			/* emo_stats_update */
#endif	/* EFSYS_OPT_MAC_STATS */
};
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
static const efx_mac_ops_t	__efx_mac_ef10_ops = {
	ef10_mac_poll,				/* emo_poll */
	ef10_mac_up,				/* emo_up */
	ef10_mac_addr_set,			/* emo_addr_set */
	ef10_mac_pdu_set,			/* emo_pdu_set */
	ef10_mac_pdu_get,			/* emo_pdu_get */
	ef10_mac_reconfigure,			/* emo_reconfigure */
	ef10_mac_multicast_list_set,		/* emo_multicast_list_set */
	ef10_mac_filter_default_rxq_set,	/* emo_filter_default_rxq_set */
	ef10_mac_filter_default_rxq_clear,
					/* emo_filter_default_rxq_clear */
#if EFSYS_OPT_LOOPBACK
	ef10_mac_loopback_set,			/* emo_loopback_set */
#endif	/* EFSYS_OPT_LOOPBACK */
#if EFSYS_OPT_MAC_STATS
	ef10_mac_stats_get_mask,		/* emo_stats_get_mask */
	efx_mcdi_mac_stats_clear,		/* emo_stats_clear */
	efx_mcdi_mac_stats_upload,		/* emo_stats_upload */
	efx_mcdi_mac_stats_periodic,		/* emo_stats_periodic */
	ef10_mac_stats_update			/* emo_stats_update */
#endif	/* EFSYS_OPT_MAC_STATS */
};
#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

	__checkReturn			efx_rc_t
efx_mac_pdu_set(
	__in				efx_nic_t *enp,
	__in				size_t pdu)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	uint32_t old_pdu;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);
	EFSYS_ASSERT(emop != NULL);

	if (pdu < EFX_MAC_PDU_MIN) {
		rc = EINVAL;
		goto fail1;
	}

	if (pdu > EFX_MAC_PDU_MAX) {
		rc = EINVAL;
		goto fail2;
	}

	old_pdu = epp->ep_mac_pdu;
	epp->ep_mac_pdu = (uint32_t)pdu;
	if ((rc = emop->emo_pdu_set(enp)) != 0)
		goto fail3;

	return (0);

fail3:
	EFSYS_PROBE(fail3);

	epp->ep_mac_pdu = old_pdu;

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mac_pdu_get(
	__in		efx_nic_t *enp,
	__out		size_t *pdu)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	if ((rc = emop->emo_pdu_get(enp, pdu)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_addr_set(
	__in				efx_nic_t *enp,
	__in				uint8_t *addr)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	uint8_t old_addr[6];
	uint32_t oui;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if (EFX_MAC_ADDR_IS_MULTICAST(addr)) {
		rc = EINVAL;
		goto fail1;
	}

	oui = addr[0] << 16 | addr[1] << 8 | addr[2];
	if (oui == 0x000000) {
		rc = EINVAL;
		goto fail2;
	}

	EFX_MAC_ADDR_COPY(old_addr, epp->ep_mac_addr);
	EFX_MAC_ADDR_COPY(epp->ep_mac_addr, addr);
	if ((rc = emop->emo_addr_set(enp)) != 0)
		goto fail3;

	return (0);

fail3:
	EFSYS_PROBE(fail3);

	EFX_MAC_ADDR_COPY(epp->ep_mac_addr, old_addr);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_filter_set(
	__in				efx_nic_t *enp,
	__in				boolean_t all_unicst,
	__in				boolean_t mulcst,
	__in				boolean_t all_mulcst,
	__in				boolean_t brdcst)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	boolean_t old_all_unicst;
	boolean_t old_mulcst;
	boolean_t old_all_mulcst;
	boolean_t old_brdcst;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	old_all_unicst = epp->ep_all_unicst;
	old_mulcst = epp->ep_mulcst;
	old_all_mulcst = epp->ep_all_mulcst;
	old_brdcst = epp->ep_brdcst;

	epp->ep_all_unicst = all_unicst;
	epp->ep_mulcst = mulcst;
	epp->ep_all_mulcst = all_mulcst;
	epp->ep_brdcst = brdcst;

	if ((rc = emop->emo_reconfigure(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	epp->ep_all_unicst = old_all_unicst;
	epp->ep_mulcst = old_mulcst;
	epp->ep_all_mulcst = old_all_mulcst;
	epp->ep_brdcst = old_brdcst;

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_drain(
	__in				efx_nic_t *enp,
	__in				boolean_t enabled)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);
	EFSYS_ASSERT(emop != NULL);

	if (epp->ep_mac_drain == enabled)
		return (0);

	epp->ep_mac_drain = enabled;

	if ((rc = emop->emo_reconfigure(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mac_up(
	__in		efx_nic_t *enp,
	__out		boolean_t *mac_upp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if ((rc = emop->emo_up(enp, mac_upp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_fcntl_set(
	__in				efx_nic_t *enp,
	__in				unsigned int fcntl,
	__in				boolean_t autoneg)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	const efx_phy_ops_t *epop = epp->ep_epop;
	unsigned int old_fcntl;
	boolean_t old_autoneg;
	unsigned int old_adv_cap;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if ((fcntl & ~(EFX_FCNTL_RESPOND | EFX_FCNTL_GENERATE)) != 0) {
		rc = EINVAL;
		goto fail1;
	}

	/*
	 * Ignore a request to set flow control auto-negotiation
	 * if the PHY doesn't support it.
	 */
	if (~epp->ep_phy_cap_mask & (1 << EFX_PHY_CAP_AN))
		autoneg = B_FALSE;

	old_fcntl = epp->ep_fcntl;
	old_autoneg = epp->ep_fcntl_autoneg;
	old_adv_cap = epp->ep_adv_cap_mask;

	epp->ep_fcntl = fcntl;
	epp->ep_fcntl_autoneg = autoneg;

	/*
	 * Always encode the flow control settings in the advertised
	 * capabilities even if we are not trying to auto-negotiate
	 * them and reconfigure both the PHY and the MAC.
	 */
	if (fcntl & EFX_FCNTL_RESPOND)
		epp->ep_adv_cap_mask |=    (1 << EFX_PHY_CAP_PAUSE |
					    1 << EFX_PHY_CAP_ASYM);
	else
		epp->ep_adv_cap_mask &=   ~(1 << EFX_PHY_CAP_PAUSE |
					    1 << EFX_PHY_CAP_ASYM);

	if (fcntl & EFX_FCNTL_GENERATE)
		epp->ep_adv_cap_mask ^= (1 << EFX_PHY_CAP_ASYM);

	if ((rc = epop->epo_reconfigure(enp)) != 0)
		goto fail2;

	if ((rc = emop->emo_reconfigure(enp)) != 0)
		goto fail3;

	return (0);

fail3:
	EFSYS_PROBE(fail3);

fail2:
	EFSYS_PROBE(fail2);

	epp->ep_fcntl = old_fcntl;
	epp->ep_fcntl_autoneg = old_autoneg;
	epp->ep_adv_cap_mask = old_adv_cap;

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
efx_mac_fcntl_get(
	__in		efx_nic_t *enp,
	__out		unsigned int *fcntl_wantedp,
	__out		unsigned int *fcntl_linkp)
{
	efx_port_t *epp = &(enp->en_port);
	unsigned int wanted = 0;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	/*
	 * Decode the requested flow control settings from the PHY
	 * advertised capabilities.
	 */
	if (epp->ep_adv_cap_mask & (1 << EFX_PHY_CAP_PAUSE))
		wanted = EFX_FCNTL_RESPOND | EFX_FCNTL_GENERATE;
	if (epp->ep_adv_cap_mask & (1 << EFX_PHY_CAP_ASYM))
		wanted ^= EFX_FCNTL_GENERATE;

	*fcntl_linkp = epp->ep_fcntl;
	*fcntl_wantedp = wanted;
}

	__checkReturn	efx_rc_t
efx_mac_multicast_list_set(
	__in				efx_nic_t *enp,
	__in_ecount(6*count)		uint8_t const *addrs,
	__in				int count)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	uint8_t	*old_mulcst_addr_list = NULL;
	uint32_t old_mulcst_addr_count;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if (count > EFX_MAC_MULTICAST_LIST_MAX) {
		rc = EINVAL;
		goto fail1;
	}

	old_mulcst_addr_count = epp->ep_mulcst_addr_count;
	if (old_mulcst_addr_count > 0) {
		/* Allocate memory to store old list (instead of using stack) */
		EFSYS_KMEM_ALLOC(enp->en_esip,
				old_mulcst_addr_count * EFX_MAC_ADDR_LEN,
				old_mulcst_addr_list);
		if (old_mulcst_addr_list == NULL) {
			rc = ENOMEM;
			goto fail2;
		}

		/* Save the old list in case we need to rollback */
		memcpy(old_mulcst_addr_list, epp->ep_mulcst_addr_list,
			old_mulcst_addr_count * EFX_MAC_ADDR_LEN);
	}

	/* Store the new list */
	memcpy(epp->ep_mulcst_addr_list, addrs,
		count * EFX_MAC_ADDR_LEN);
	epp->ep_mulcst_addr_count = count;

	if ((rc = emop->emo_multicast_list_set(enp)) != 0)
		goto fail3;

	if (old_mulcst_addr_count > 0) {
		EFSYS_KMEM_FREE(enp->en_esip,
				old_mulcst_addr_count * EFX_MAC_ADDR_LEN,
				old_mulcst_addr_list);
	}

	return (0);

fail3:
	EFSYS_PROBE(fail3);

	/* Restore original list on failure */
	epp->ep_mulcst_addr_count = old_mulcst_addr_count;
	if (old_mulcst_addr_count > 0) {
		memcpy(epp->ep_mulcst_addr_list, old_mulcst_addr_list,
			old_mulcst_addr_count * EFX_MAC_ADDR_LEN);

		EFSYS_KMEM_FREE(enp->en_esip,
				old_mulcst_addr_count * EFX_MAC_ADDR_LEN,
				old_mulcst_addr_list);
	}

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);

}

	__checkReturn	efx_rc_t
efx_mac_filter_default_rxq_set(
	__in		efx_nic_t *enp,
	__in		efx_rxq_t *erp,
	__in		boolean_t using_rss)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if (emop->emo_filter_default_rxq_set != NULL) {
		rc = emop->emo_filter_default_rxq_set(enp, erp, using_rss);
		if (rc != 0)
			goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
efx_mac_filter_default_rxq_clear(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	if (emop->emo_filter_default_rxq_clear != NULL)
		emop->emo_filter_default_rxq_clear(enp);
}


#if EFSYS_OPT_MAC_STATS

#if EFSYS_OPT_NAMES

/* START MKCONFIG GENERATED EfxMacStatNamesBlock 1a45a82fcfb30c1b */
static const char * const __efx_mac_stat_name[] = {
	"rx_octets",
	"rx_pkts",
	"rx_unicst_pkts",
	"rx_multicst_pkts",
	"rx_brdcst_pkts",
	"rx_pause_pkts",
	"rx_le_64_pkts",
	"rx_65_to_127_pkts",
	"rx_128_to_255_pkts",
	"rx_256_to_511_pkts",
	"rx_512_to_1023_pkts",
	"rx_1024_to_15xx_pkts",
	"rx_ge_15xx_pkts",
	"rx_errors",
	"rx_fcs_errors",
	"rx_drop_events",
	"rx_false_carrier_errors",
	"rx_symbol_errors",
	"rx_align_errors",
	"rx_internal_errors",
	"rx_jabber_pkts",
	"rx_lane0_char_err",
	"rx_lane1_char_err",
	"rx_lane2_char_err",
	"rx_lane3_char_err",
	"rx_lane0_disp_err",
	"rx_lane1_disp_err",
	"rx_lane2_disp_err",
	"rx_lane3_disp_err",
	"rx_match_fault",
	"rx_nodesc_drop_cnt",
	"tx_octets",
	"tx_pkts",
	"tx_unicst_pkts",
	"tx_multicst_pkts",
	"tx_brdcst_pkts",
	"tx_pause_pkts",
	"tx_le_64_pkts",
	"tx_65_to_127_pkts",
	"tx_128_to_255_pkts",
	"tx_256_to_511_pkts",
	"tx_512_to_1023_pkts",
	"tx_1024_to_15xx_pkts",
	"tx_ge_15xx_pkts",
	"tx_errors",
	"tx_sgl_col_pkts",
	"tx_mult_col_pkts",
	"tx_ex_col_pkts",
	"tx_late_col_pkts",
	"tx_def_pkts",
	"tx_ex_def_pkts",
	"pm_trunc_bb_overflow",
	"pm_discard_bb_overflow",
	"pm_trunc_vfifo_full",
	"pm_discard_vfifo_full",
	"pm_trunc_qbb",
	"pm_discard_qbb",
	"pm_discard_mapping",
	"rxdp_q_disabled_pkts",
	"rxdp_di_dropped_pkts",
	"rxdp_streaming_pkts",
	"rxdp_hlb_fetch",
	"rxdp_hlb_wait",
	"vadapter_rx_unicast_packets",
	"vadapter_rx_unicast_bytes",
	"vadapter_rx_multicast_packets",
	"vadapter_rx_multicast_bytes",
	"vadapter_rx_broadcast_packets",
	"vadapter_rx_broadcast_bytes",
	"vadapter_rx_bad_packets",
	"vadapter_rx_bad_bytes",
	"vadapter_rx_overflow",
	"vadapter_tx_unicast_packets",
	"vadapter_tx_unicast_bytes",
	"vadapter_tx_multicast_packets",
	"vadapter_tx_multicast_bytes",
	"vadapter_tx_broadcast_packets",
	"vadapter_tx_broadcast_bytes",
	"vadapter_tx_bad_packets",
	"vadapter_tx_bad_bytes",
	"vadapter_tx_overflow",
	"fec_uncorrected_errors",
	"fec_corrected_errors",
	"fec_corrected_symbols_lane0",
	"fec_corrected_symbols_lane1",
	"fec_corrected_symbols_lane2",
	"fec_corrected_symbols_lane3",
	"ctpio_vi_busy_fallback",
	"ctpio_long_write_success",
	"ctpio_missing_dbell_fail",
	"ctpio_overflow_fail",
	"ctpio_underflow_fail",
	"ctpio_timeout_fail",
	"ctpio_noncontig_wr_fail",
	"ctpio_frm_clobber_fail",
	"ctpio_invalid_wr_fail",
	"ctpio_vi_clobber_fallback",
	"ctpio_unqualified_fallback",
	"ctpio_runt_fallback",
	"ctpio_success",
	"ctpio_fallback",
	"ctpio_poison",
	"ctpio_erase",
	"rxdp_scatter_disabled_trunc",
	"rxdp_hlb_idle",
	"rxdp_hlb_timeout",
};
/* END MKCONFIG GENERATED EfxMacStatNamesBlock */

	__checkReturn			const char *
efx_mac_stat_name(
	__in				efx_nic_t *enp,
	__in				unsigned int id)
{
	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);

	EFSYS_ASSERT3U(id, <, EFX_MAC_NSTATS);
	return (__efx_mac_stat_name[id]);
}

#endif	/* EFSYS_OPT_NAMES */

static					efx_rc_t
efx_mac_stats_mask_add_range(
	__inout_bcount(mask_size)	uint32_t *maskp,
	__in				size_t mask_size,
	__in				const struct efx_mac_stats_range *rngp)
{
	unsigned int mask_npages = mask_size / sizeof (*maskp);
	unsigned int el;
	unsigned int el_min;
	unsigned int el_max;
	unsigned int low;
	unsigned int high;
	unsigned int width;
	efx_rc_t rc;

	if ((mask_npages * EFX_MAC_STATS_MASK_BITS_PER_PAGE) <=
	    (unsigned int)rngp->last) {
		rc = EINVAL;
		goto fail1;
	}

	EFSYS_ASSERT3U(rngp->first, <=, rngp->last);
	EFSYS_ASSERT3U(rngp->last, <, EFX_MAC_NSTATS);

	for (el = 0; el < mask_npages; ++el) {
		el_min = el * EFX_MAC_STATS_MASK_BITS_PER_PAGE;
		el_max =
		    el_min + (EFX_MAC_STATS_MASK_BITS_PER_PAGE - 1);
		if ((unsigned int)rngp->first > el_max ||
		    (unsigned int)rngp->last < el_min)
			continue;
		low = MAX((unsigned int)rngp->first, el_min);
		high = MIN((unsigned int)rngp->last, el_max);
		width = high - low + 1;
		maskp[el] |=
		    (width == EFX_MAC_STATS_MASK_BITS_PER_PAGE) ?
		    (~0ULL) : (((1ULL << width) - 1) << (low - el_min));
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

					efx_rc_t
efx_mac_stats_mask_add_ranges(
	__inout_bcount(mask_size)	uint32_t *maskp,
	__in				size_t mask_size,
	__in_ecount(rng_count)		const struct efx_mac_stats_range *rngp,
	__in				unsigned int rng_count)
{
	unsigned int i;
	efx_rc_t rc;

	for (i = 0; i < rng_count; ++i) {
		if ((rc = efx_mac_stats_mask_add_range(maskp, mask_size,
		    &rngp[i])) != 0)
			goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_stats_get_mask(
	__in				efx_nic_t *enp,
	__out_bcount(mask_size)		uint32_t *maskp,
	__in				size_t mask_size)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PROBE);
	EFSYS_ASSERT(maskp != NULL);
	EFSYS_ASSERT(mask_size % sizeof (maskp[0]) == 0);

	(void) memset(maskp, 0, mask_size);

	if ((rc = emop->emo_stats_get_mask(enp, maskp, mask_size)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_stats_clear(
	__in				efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);
	EFSYS_ASSERT(emop != NULL);

	if ((rc = emop->emo_stats_clear(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_stats_upload(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);
	EFSYS_ASSERT(emop != NULL);

	if ((rc = emop->emo_stats_upload(enp, esmp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
efx_mac_stats_periodic(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__in				uint16_t period_ms,
	__in				boolean_t events)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	EFSYS_ASSERT(emop != NULL);

	if (emop->emo_stats_periodic == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	if ((rc = emop->emo_stats_periodic(enp, esmp, period_ms, events)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


	__checkReturn			efx_rc_t
efx_mac_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MAC_NSTATS)	efsys_stat_t *essp,
	__inout_opt			uint32_t *generationp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);
	EFSYS_ASSERT(emop != NULL);

	rc = emop->emo_stats_update(enp, esmp, essp, generationp);

	return (rc);
}

#endif	/* EFSYS_OPT_MAC_STATS */

	__checkReturn			efx_rc_t
efx_mac_select(
	__in				efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_mac_type_t type = EFX_MAC_INVALID;
	const efx_mac_ops_t *emop;
	int rc = EINVAL;

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		emop = &__efx_mac_siena_ops;
		type = EFX_MAC_SIENA;
		break;
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		emop = &__efx_mac_ef10_ops;
		type = EFX_MAC_HUNTINGTON;
		break;
#endif /* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		emop = &__efx_mac_ef10_ops;
		type = EFX_MAC_MEDFORD;
		break;
#endif /* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		emop = &__efx_mac_ef10_ops;
		type = EFX_MAC_MEDFORD2;
		break;
#endif /* EFSYS_OPT_MEDFORD2 */

	default:
		rc = EINVAL;
		goto fail1;
	}

	EFSYS_ASSERT(type != EFX_MAC_INVALID);
	EFSYS_ASSERT3U(type, <, EFX_MAC_NTYPES);
	EFSYS_ASSERT(emop != NULL);

	epp->ep_emop = emop;
	epp->ep_mac_type = type;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


#if EFSYS_OPT_SIENA

#define	EFX_MAC_HASH_BITS	(1 << 8)

/* Compute the multicast hash as used on Falcon and Siena. */
static	void
siena_mac_multicast_hash_compute(
	__in_ecount(6*count)		uint8_t const *addrs,
	__in				int count,
	__out				efx_oword_t *hash_low,
	__out				efx_oword_t *hash_high)
{
	uint32_t crc, index;
	int i;

	EFSYS_ASSERT(hash_low != NULL);
	EFSYS_ASSERT(hash_high != NULL);

	EFX_ZERO_OWORD(*hash_low);
	EFX_ZERO_OWORD(*hash_high);

	for (i = 0; i < count; i++) {
		/* Calculate hash bucket (IEEE 802.3 CRC32 of the MAC addr) */
		crc = efx_crc32_calculate(0xffffffff, addrs, EFX_MAC_ADDR_LEN);
		index = crc % EFX_MAC_HASH_BITS;
		if (index < 128) {
			EFX_SET_OWORD_BIT(*hash_low, index);
		} else {
			EFX_SET_OWORD_BIT(*hash_high, index - 128);
		}

		addrs += EFX_MAC_ADDR_LEN;
	}
}

static	__checkReturn	efx_rc_t
siena_mac_multicast_list_set(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_oword_t old_hash[2];
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_PORT);

	memcpy(old_hash, epp->ep_multicst_hash, sizeof (old_hash));

	siena_mac_multicast_hash_compute(
	    epp->ep_mulcst_addr_list,
	    epp->ep_mulcst_addr_count,
	    &epp->ep_multicst_hash[0],
	    &epp->ep_multicst_hash[1]);

	if ((rc = emop->emo_reconfigure(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	memcpy(epp->ep_multicst_hash, old_hash, sizeof (old_hash));

	return (rc);
}

#endif /* EFSYS_OPT_SIENA */
