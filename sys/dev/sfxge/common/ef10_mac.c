/*-
 * Copyright (c) 2012-2016 Solarflare Communications Inc.
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


#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

	__checkReturn	efx_rc_t
ef10_mac_poll(
	__in		efx_nic_t *enp,
	__out		efx_link_mode_t *link_modep)
{
	efx_port_t *epp = &(enp->en_port);
	ef10_link_state_t els;
	efx_rc_t rc;

	if ((rc = ef10_phy_get_link(enp, &els)) != 0)
		goto fail1;

	epp->ep_adv_cap_mask = els.epls.epls_adv_cap_mask;
	epp->ep_fcntl = els.epls.epls_fcntl;

	*link_modep = els.epls.epls_link_mode;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	*link_modep = EFX_LINK_UNKNOWN;

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_mac_up(
	__in		efx_nic_t *enp,
	__out		boolean_t *mac_upp)
{
	ef10_link_state_t els;
	efx_rc_t rc;

	/*
	 * Because EF10 doesn't *require* polling, we can't rely on
	 * ef10_mac_poll() being executed to populate epp->ep_mac_up.
	 */
	if ((rc = ef10_phy_get_link(enp, &els)) != 0)
		goto fail1;

	*mac_upp = els.els_mac_up;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * EF10 adapters use MC_CMD_VADAPTOR_SET_MAC to set the
 * MAC address; the address field in MC_CMD_SET_MAC has no
 * effect.
 * MC_CMD_VADAPTOR_SET_MAC requires mac-spoofing privilege and
 * the port to have no filters or queues active.
 */
static	__checkReturn	efx_rc_t
efx_mcdi_vadapter_set_mac(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_VADAPTOR_SET_MAC_IN_LEN,
		MC_CMD_VADAPTOR_SET_MAC_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_VADAPTOR_SET_MAC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_VADAPTOR_SET_MAC_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_VADAPTOR_SET_MAC_OUT_LEN;

	MCDI_IN_SET_DWORD(req, VADAPTOR_SET_MAC_IN_UPSTREAM_PORT_ID,
	    enp->en_vport_id);
	EFX_MAC_ADDR_COPY(MCDI_IN2(req, uint8_t, VADAPTOR_SET_MAC_IN_MACADDR),
	    epp->ep_mac_addr);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_mac_addr_set(
	__in		efx_nic_t *enp)
{
	efx_rc_t rc;

	if ((rc = efx_mcdi_vadapter_set_mac(enp)) != 0) {
		if (rc != ENOTSUP)
			goto fail1;

		/*
		 * Fallback for older Huntington firmware without Vadapter
		 * support.
		 */
		if ((rc = ef10_mac_reconfigure(enp)) != 0)
			goto fail2;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_mtu_set(
	__in		efx_nic_t *enp,
	__in		uint32_t mtu)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SET_MAC_EXT_IN_LEN,
		MC_CMD_SET_MAC_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_SET_MAC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_MAC_EXT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_MAC_OUT_LEN;

	/* Only configure the MTU in this call to MC_CMD_SET_MAC */
	MCDI_IN_SET_DWORD(req, SET_MAC_EXT_IN_MTU, mtu);
	MCDI_IN_POPULATE_DWORD_1(req, SET_MAC_EXT_IN_CONTROL,
			    SET_MAC_EXT_IN_CFG_MTU, 1);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
efx_mcdi_mtu_get(
	__in		efx_nic_t *enp,
	__out		size_t *mtu)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SET_MAC_EXT_IN_LEN,
		MC_CMD_SET_MAC_V2_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_SET_MAC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_MAC_EXT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_MAC_V2_OUT_LEN;

	/*
	 * With MC_CMD_SET_MAC_EXT_IN_CONTROL set to 0, this just queries the
	 * MTU.  This should always be supported on Medford, but it is not
	 * supported on older Huntington firmware.
	 */
	MCDI_IN_SET_DWORD(req, SET_MAC_EXT_IN_CONTROL, 0);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}
	if (req.emr_out_length_used < MC_CMD_SET_MAC_V2_OUT_MTU_OFST + 4) {
		rc = EMSGSIZE;
		goto fail2;
	}

	*mtu = MCDI_OUT_DWORD(req, SET_MAC_V2_OUT_MTU);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_mac_pdu_set(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_rc_t rc;

	if (encp->enc_enhanced_set_mac_supported) {
		if ((rc = efx_mcdi_mtu_set(enp, epp->ep_mac_pdu)) != 0)
			goto fail1;
	} else {
		/*
		 * Fallback for older Huntington firmware, which always
		 * configure all of the parameters to MC_CMD_SET_MAC. This isn't
		 * suitable for setting the MTU on unpriviliged functions.
		 */
		if ((rc = ef10_mac_reconfigure(enp)) != 0)
			goto fail2;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_mac_pdu_get(
	__in		efx_nic_t *enp,
	__out		size_t *pdu)
{
	efx_rc_t rc;

	if ((rc = efx_mcdi_mtu_get(enp, pdu)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

__checkReturn	efx_rc_t
ef10_mac_reconfigure(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SET_MAC_IN_LEN,
		MC_CMD_SET_MAC_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_SET_MAC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_MAC_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_MAC_OUT_LEN;

	MCDI_IN_SET_DWORD(req, SET_MAC_IN_MTU, epp->ep_mac_pdu);
	MCDI_IN_SET_DWORD(req, SET_MAC_IN_DRAIN, epp->ep_mac_drain ? 1 : 0);
	EFX_MAC_ADDR_COPY(MCDI_IN2(req, uint8_t, SET_MAC_IN_ADDR),
			    epp->ep_mac_addr);

	/*
	 * Note: The Huntington MAC does not support REJECT_BRDCST.
	 * The REJECT_UNCST flag will also prevent multicast traffic
	 * from reaching the filters. As Huntington filters drop any
	 * traffic that does not match a filter it is ok to leave the
	 * MAC running in promiscuous mode. See bug41141.
	 *
	 * FIXME: Does REJECT_UNCST behave the same way on Medford?
	 */
	MCDI_IN_POPULATE_DWORD_2(req, SET_MAC_IN_REJECT,
				    SET_MAC_IN_REJECT_UNCST, 0,
				    SET_MAC_IN_REJECT_BRDCST, 0);

	/*
	 * Flow control, whether it is auto-negotiated or not,
	 * is set via the PHY advertised capabilities.  When set to
	 * automatic the MAC will use the PHY settings to determine
	 * the flow control settings.
	 */
	MCDI_IN_SET_DWORD(req, SET_MAC_IN_FCNTL, MC_CMD_FCNTL_AUTO);

	/* Do not include the Ethernet frame checksum in RX packets */
	MCDI_IN_POPULATE_DWORD_1(req, SET_MAC_IN_FLAGS,
				    SET_MAC_IN_FLAG_INCLUDE_FCS, 0);

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		/*
		 * Unprivileged functions cannot control link state,
		 * but still need to configure filters.
		 */
		if (req.emr_rc != EACCES) {
			rc = req.emr_rc;
			goto fail1;
		}
	}

	/*
	 * Apply the filters for the MAC configuration.
	 * If the NIC isn't ready to accept filters this may
	 * return success without setting anything.
	 */
	rc = efx_filter_reconfigure(enp, epp->ep_mac_addr,
				    epp->ep_all_unicst, epp->ep_mulcst,
				    epp->ep_all_mulcst, epp->ep_brdcst,
				    epp->ep_mulcst_addr_list,
				    epp->ep_mulcst_addr_count);

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn			efx_rc_t
ef10_mac_multicast_list_set(
	__in				efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_mac_ops_t *emop = epp->ep_emop;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	if ((rc = emop->emo_reconfigure(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_mac_filter_default_rxq_set(
	__in		efx_nic_t *enp,
	__in		efx_rxq_t *erp,
	__in		boolean_t using_rss)
{
	efx_port_t *epp = &(enp->en_port);
	efx_rxq_t *old_rxq;
	boolean_t old_using_rss;
	efx_rc_t rc;

	ef10_filter_get_default_rxq(enp, &old_rxq, &old_using_rss);

	ef10_filter_default_rxq_set(enp, erp, using_rss);

	rc = efx_filter_reconfigure(enp, epp->ep_mac_addr,
				    epp->ep_all_unicst, epp->ep_mulcst,
				    epp->ep_all_mulcst, epp->ep_brdcst,
				    epp->ep_mulcst_addr_list,
				    epp->ep_mulcst_addr_count);

	if (rc != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	ef10_filter_default_rxq_set(enp, old_rxq, old_using_rss);

	return (rc);
}

			void
ef10_mac_filter_default_rxq_clear(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);

	ef10_filter_default_rxq_clear(enp);

	(void) efx_filter_reconfigure(enp, epp->ep_mac_addr,
				    epp->ep_all_unicst, epp->ep_mulcst,
				    epp->ep_all_mulcst, epp->ep_brdcst,
				    epp->ep_mulcst_addr_list,
				    epp->ep_mulcst_addr_count);
}


#if EFSYS_OPT_LOOPBACK

	__checkReturn	efx_rc_t
ef10_mac_loopback_set(
	__in		efx_nic_t *enp,
	__in		efx_link_mode_t link_mode,
	__in		efx_loopback_type_t loopback_type)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_loopback_type_t old_loopback_type;
	efx_link_mode_t old_loopback_link_mode;
	efx_rc_t rc;

	/* The PHY object handles this on EF10 */
	old_loopback_type = epp->ep_loopback_type;
	old_loopback_link_mode = epp->ep_loopback_link_mode;
	epp->ep_loopback_type = loopback_type;
	epp->ep_loopback_link_mode = link_mode;

	if ((rc = epop->epo_reconfigure(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	epp->ep_loopback_type = old_loopback_type;
	epp->ep_loopback_link_mode = old_loopback_link_mode;

	return (rc);
}

#endif	/* EFSYS_OPT_LOOPBACK */

#if EFSYS_OPT_MAC_STATS

	__checkReturn			efx_rc_t
ef10_mac_stats_get_mask(
	__in				efx_nic_t *enp,
	__inout_bcount(mask_size)	uint32_t *maskp,
	__in				size_t mask_size)
{
	const struct efx_mac_stats_range ef10_common[] = {
		{ EFX_MAC_RX_OCTETS, EFX_MAC_RX_GE_15XX_PKTS },
		{ EFX_MAC_RX_FCS_ERRORS, EFX_MAC_RX_DROP_EVENTS },
		{ EFX_MAC_RX_JABBER_PKTS, EFX_MAC_RX_JABBER_PKTS },
		{ EFX_MAC_RX_NODESC_DROP_CNT, EFX_MAC_TX_PAUSE_PKTS },
	};
	const struct efx_mac_stats_range ef10_tx_size_bins[] = {
		{ EFX_MAC_TX_LE_64_PKTS, EFX_MAC_TX_GE_15XX_PKTS },
	};
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_port_t *epp = &(enp->en_port);
	efx_rc_t rc;

	if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
	    ef10_common, EFX_ARRAY_SIZE(ef10_common))) != 0)
		goto fail1;

	if (epp->ep_phy_cap_mask & (1 << MC_CMD_PHY_CAP_40000FDX_LBN)) {
		const struct efx_mac_stats_range ef10_40g_extra[] = {
			{ EFX_MAC_RX_ALIGN_ERRORS, EFX_MAC_RX_ALIGN_ERRORS },
		};

		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_40g_extra, EFX_ARRAY_SIZE(ef10_40g_extra))) != 0)
			goto fail2;

		if (encp->enc_mac_stats_40g_tx_size_bins) {
			if ((rc = efx_mac_stats_mask_add_ranges(maskp,
			    mask_size, ef10_tx_size_bins,
			    EFX_ARRAY_SIZE(ef10_tx_size_bins))) != 0)
				goto fail3;
		}
	} else {
		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_tx_size_bins, EFX_ARRAY_SIZE(ef10_tx_size_bins))) != 0)
			goto fail4;
	}

	if (encp->enc_pm_and_rxdp_counters) {
		const struct efx_mac_stats_range ef10_pm_and_rxdp[] = {
			{ EFX_MAC_PM_TRUNC_BB_OVERFLOW, EFX_MAC_RXDP_HLB_WAIT },
		};

		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_pm_and_rxdp, EFX_ARRAY_SIZE(ef10_pm_and_rxdp))) != 0)
			goto fail5;
	}

	if (encp->enc_datapath_cap_evb) {
		const struct efx_mac_stats_range ef10_vadaptor[] = {
			{ EFX_MAC_VADAPTER_RX_UNICAST_PACKETS,
			    EFX_MAC_VADAPTER_TX_OVERFLOW },
		};

		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_vadaptor, EFX_ARRAY_SIZE(ef10_vadaptor))) != 0)
			goto fail6;
	}

	if (encp->enc_fec_counters) {
		const struct efx_mac_stats_range ef10_fec[] = {
			{ EFX_MAC_FEC_UNCORRECTED_ERRORS,
			    EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE3 },
		};
		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_fec, EFX_ARRAY_SIZE(ef10_fec))) != 0)
			goto fail7;
	}

	if (encp->enc_mac_stats_nstats >= MC_CMD_MAC_NSTATS_V4) {
		const struct efx_mac_stats_range ef10_rxdp_sdt[] = {
			{ EFX_MAC_RXDP_SCATTER_DISABLED_TRUNC,
			    EFX_MAC_RXDP_SCATTER_DISABLED_TRUNC },
		};

		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_rxdp_sdt, EFX_ARRAY_SIZE(ef10_rxdp_sdt))) != 0)
			goto fail8;
	}

	if (encp->enc_hlb_counters) {
		const struct efx_mac_stats_range ef10_hlb[] = {
			{ EFX_MAC_RXDP_HLB_IDLE, EFX_MAC_RXDP_HLB_TIMEOUT },
		};
		if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
		    ef10_hlb, EFX_ARRAY_SIZE(ef10_hlb))) != 0)
			goto fail9;
	}

	return (0);

fail9:
	EFSYS_PROBE(fail9);
fail8:
	EFSYS_PROBE(fail8);
fail7:
	EFSYS_PROBE(fail7);
fail6:
	EFSYS_PROBE(fail6);
fail5:
	EFSYS_PROBE(fail5);
fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#define	EF10_MAC_STAT_READ(_esmp, _field, _eqp)			\
	EFSYS_MEM_READQ((_esmp), (_field) * sizeof (efx_qword_t), _eqp)


	__checkReturn			efx_rc_t
ef10_mac_stats_update(
	__in				efx_nic_t *enp,
	__in				efsys_mem_t *esmp,
	__inout_ecount(EFX_MAC_NSTATS)	efsys_stat_t *stat,
	__inout_opt			uint32_t *generationp)
{
	const efx_nic_cfg_t *encp = &enp->en_nic_cfg;
	efx_qword_t generation_start;
	efx_qword_t generation_end;
	efx_qword_t value;
	efx_rc_t rc;

	/*
	 * The MAC_STATS contain start and end generation counters used to
	 * detect when the DMA buffer has been updated during stats decode.
	 * All stats counters are 64bit unsigned values.
	 *
	 * Siena-compatible MAC stats contain MC_CMD_MAC_NSTATS 64bit counters.
	 * The generation end counter is at index MC_CMD_MAC_GENERATION_END
	 * (same as MC_CMD_MAC_NSTATS-1).
	 *
	 * Medford2 and later use a larger DMA buffer: MAC_STATS_NUM_STATS from
	 * MC_CMD_GET_CAPABILITIES_V4_OUT reports the number of 64bit counters.
	 *
	 * Firmware writes the generation end counter as the last counter in the
	 * DMA buffer. Do not use MC_CMD_MAC_GENERATION_END, as that is only
	 * correct for legacy Siena-compatible MAC stats.
	 */

	if (encp->enc_mac_stats_nstats < MC_CMD_MAC_NSTATS) {
		/* MAC stats count too small for legacy MAC stats */
		rc = ENOSPC;
		goto fail1;
	}
	if (EFSYS_MEM_SIZE(esmp) <
	    (encp->enc_mac_stats_nstats * sizeof (efx_qword_t))) {
		/* DMA buffer too small */
		rc = ENOSPC;
		goto fail2;
	}

	/* Read END first so we don't race with the MC */
	EFSYS_DMA_SYNC_FOR_KERNEL(esmp, 0, EFSYS_MEM_SIZE(esmp));
	EF10_MAC_STAT_READ(esmp, (encp->enc_mac_stats_nstats - 1),
	    &generation_end);
	EFSYS_MEM_READ_BARRIER();

	/* TX */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_CONTROL_PKTS, &value);
	EFSYS_STAT_SUBR_QWORD(&(stat[EFX_MAC_TX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_PAUSE_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_PAUSE_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_UNICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_UNICST_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_MULTICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_MULTICST_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_BROADCAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_BRDCST_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_BYTES, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_OCTETS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_LT64_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_LE_64_PKTS]), &value);
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_64_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_TX_LE_64_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_65_TO_127_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_65_TO_127_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_128_TO_255_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_128_TO_255_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_256_TO_511_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_256_TO_511_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_512_TO_1023_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_512_TO_1023_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_1024_TO_15XX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_1024_TO_15XX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_15XX_TO_JUMBO_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_GE_15XX_PKTS]), &value);
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_GTJUMBO_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_TX_GE_15XX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_BAD_FCS_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_SINGLE_COLLISION_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_SGL_COL_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_MULTIPLE_COLLISION_PKTS,
			    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_MULT_COL_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_EXCESSIVE_COLLISION_PKTS,
			    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_EX_COL_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_LATE_COLLISION_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_LATE_COL_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_DEFERRED_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_DEF_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_EXCESSIVE_DEFERRED_PKTS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_EX_DEF_PKTS]), &value);

	/* RX */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_BYTES, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_OCTETS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_UNICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_UNICST_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_MULTICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_MULTICST_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_BROADCAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_BRDCST_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_PAUSE_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_PAUSE_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_UNDERSIZE_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_LE_64_PKTS]), &value);
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_64_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_RX_LE_64_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_65_TO_127_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_65_TO_127_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_128_TO_255_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_128_TO_255_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_256_TO_511_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_256_TO_511_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_512_TO_1023_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_512_TO_1023_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_1024_TO_15XX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_1024_TO_15XX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_15XX_TO_JUMBO_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_GE_15XX_PKTS]), &value);
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_GTJUMBO_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_RX_GE_15XX_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_BAD_FCS_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_FCS_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_OVERFLOW_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_DROP_EVENTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_FALSE_CARRIER_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_FALSE_CARRIER_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_SYMBOL_ERROR_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_SYMBOL_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_ALIGN_ERROR_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_ALIGN_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_INTERNAL_ERROR_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_INTERNAL_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_JABBER_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_JABBER_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES01_CHAR_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE0_CHAR_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE1_CHAR_ERR]),
			    &(value.eq_dword[1]));

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES23_CHAR_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE2_CHAR_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE3_CHAR_ERR]),
			    &(value.eq_dword[1]));

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES01_DISP_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE0_DISP_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE1_DISP_ERR]),
			    &(value.eq_dword[1]));

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES23_DISP_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE2_DISP_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE3_DISP_ERR]),
			    &(value.eq_dword[1]));

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_MATCH_FAULT, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_MATCH_FAULT]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_NODESC_DROPS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_NODESC_DROP_CNT]), &value);

	/* Packet memory (EF10 only) */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_TRUNC_BB_OVERFLOW, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_TRUNC_BB_OVERFLOW]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_DISCARD_BB_OVERFLOW, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_DISCARD_BB_OVERFLOW]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_TRUNC_VFIFO_FULL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_TRUNC_VFIFO_FULL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_DISCARD_VFIFO_FULL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_DISCARD_VFIFO_FULL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_TRUNC_QBB, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_TRUNC_QBB]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_DISCARD_QBB, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_DISCARD_QBB]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_PM_DISCARD_MAPPING, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_PM_DISCARD_MAPPING]), &value);

	/* RX datapath */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_Q_DISABLED_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_Q_DISABLED_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_DI_DROPPED_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_DI_DROPPED_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_STREAMING_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_STREAMING_PKTS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_HLB_FETCH_CONDITIONS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_HLB_FETCH]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_HLB_WAIT_CONDITIONS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_HLB_WAIT]), &value);


	/* VADAPTER RX */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_UNICAST_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_UNICAST_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_UNICAST_BYTES,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_UNICAST_BYTES]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_MULTICAST_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_MULTICAST_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_MULTICAST_BYTES,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_MULTICAST_BYTES]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_BROADCAST_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_BROADCAST_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_BROADCAST_BYTES,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_BROADCAST_BYTES]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_BAD_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_BAD_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_BAD_BYTES, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_BAD_BYTES]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_RX_OVERFLOW, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_RX_OVERFLOW]), &value);

	/* VADAPTER TX */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_UNICAST_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_UNICAST_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_UNICAST_BYTES,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_UNICAST_BYTES]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_MULTICAST_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_MULTICAST_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_MULTICAST_BYTES,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_MULTICAST_BYTES]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_BROADCAST_PACKETS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_BROADCAST_PACKETS]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_BROADCAST_BYTES,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_BROADCAST_BYTES]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_BAD_PACKETS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_BAD_PACKETS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_BAD_BYTES, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_BAD_BYTES]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_VADAPTER_TX_OVERFLOW, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_VADAPTER_TX_OVERFLOW]), &value);


	if (encp->enc_mac_stats_nstats < MC_CMD_MAC_NSTATS_V2)
		goto done;

	/* FEC */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_FEC_UNCORRECTED_ERRORS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_FEC_UNCORRECTED_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_FEC_CORRECTED_ERRORS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_FEC_CORRECTED_ERRORS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_FEC_CORRECTED_SYMBOLS_LANE0,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE0]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_FEC_CORRECTED_SYMBOLS_LANE1,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE1]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_FEC_CORRECTED_SYMBOLS_LANE2,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE2]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_FEC_CORRECTED_SYMBOLS_LANE3,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_FEC_CORRECTED_SYMBOLS_LANE3]),
	    &value);

	if (encp->enc_mac_stats_nstats < MC_CMD_MAC_NSTATS_V3)
		goto done;

	/* CTPIO exceptions */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_VI_BUSY_FALLBACK, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_VI_BUSY_FALLBACK]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_LONG_WRITE_SUCCESS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_LONG_WRITE_SUCCESS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_MISSING_DBELL_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_MISSING_DBELL_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_OVERFLOW_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_OVERFLOW_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_UNDERFLOW_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_UNDERFLOW_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_TIMEOUT_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_TIMEOUT_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_NONCONTIG_WR_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_NONCONTIG_WR_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_FRM_CLOBBER_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_FRM_CLOBBER_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_INVALID_WR_FAIL, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_INVALID_WR_FAIL]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_VI_CLOBBER_FALLBACK, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_VI_CLOBBER_FALLBACK]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_UNQUALIFIED_FALLBACK, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_UNQUALIFIED_FALLBACK]),
	    &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_RUNT_FALLBACK, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_RUNT_FALLBACK]), &value);

	/* CTPIO per-port stats */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_SUCCESS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_SUCCESS]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_FALLBACK, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_FALLBACK]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_POISON, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_POISON]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_CTPIO_ERASE, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_CTPIO_ERASE]), &value);

	if (encp->enc_mac_stats_nstats < MC_CMD_MAC_NSTATS_V4)
		goto done;

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_SCATTER_DISABLED_TRUNC,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_SCATTER_DISABLED_TRUNC]),
	    &value);

	/* Head-of-line blocking */
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_HLB_IDLE, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_HLB_IDLE]), &value);

	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_RXDP_HLB_TIMEOUT, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RXDP_HLB_TIMEOUT]), &value);

done:
	/* Read START generation counter */
	EFSYS_DMA_SYNC_FOR_KERNEL(esmp, 0, EFSYS_MEM_SIZE(esmp));
	EFSYS_MEM_READ_BARRIER();
	EF10_MAC_STAT_READ(esmp, MC_CMD_MAC_GENERATION_START,
			    &generation_start);

	/* Check that we didn't read the stats in the middle of a DMA */
	/* Not a good enough check ? */
	if (memcmp(&generation_start, &generation_end,
	    sizeof (generation_start)))
		return (EAGAIN);

	if (generationp)
		*generationp = EFX_QWORD_FIELD(generation_start, EFX_DWORD_0);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif	/* EFSYS_OPT_MAC_STATS */

#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
