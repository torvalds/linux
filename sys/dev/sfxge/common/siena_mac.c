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

#if EFSYS_OPT_SIENA

	__checkReturn	efx_rc_t
siena_mac_poll(
	__in		efx_nic_t *enp,
	__out		efx_link_mode_t *link_modep)
{
	efx_port_t *epp = &(enp->en_port);
	siena_link_state_t sls;
	efx_rc_t rc;

	if ((rc = siena_phy_get_link(enp, &sls)) != 0)
		goto fail1;

	epp->ep_adv_cap_mask = sls.sls_adv_cap_mask;
	epp->ep_fcntl = sls.sls_fcntl;

	*link_modep = sls.sls_link_mode;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	*link_modep = EFX_LINK_UNKNOWN;

	return (rc);
}

	__checkReturn	efx_rc_t
siena_mac_up(
	__in		efx_nic_t *enp,
	__out		boolean_t *mac_upp)
{
	siena_link_state_t sls;
	efx_rc_t rc;

	/*
	 * Because Siena doesn't *require* polling, we can't rely on
	 * siena_mac_poll() being executed to populate epp->ep_mac_up.
	 */
	if ((rc = siena_phy_get_link(enp, &sls)) != 0)
		goto fail1;

	*mac_upp = sls.sls_mac_up;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
siena_mac_reconfigure(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_oword_t multicast_hash[2];
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload,
		MAX(MC_CMD_SET_MAC_IN_LEN, MC_CMD_SET_MCAST_HASH_IN_LEN),
		MAX(MC_CMD_SET_MAC_OUT_LEN, MC_CMD_SET_MCAST_HASH_OUT_LEN));

	unsigned int fcntl;
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
	MCDI_IN_POPULATE_DWORD_2(req, SET_MAC_IN_REJECT,
			    SET_MAC_IN_REJECT_UNCST, !epp->ep_all_unicst,
			    SET_MAC_IN_REJECT_BRDCST, !epp->ep_brdcst);

	if (epp->ep_fcntl_autoneg)
		/* efx_fcntl_set() has already set the phy capabilities */
		fcntl = MC_CMD_FCNTL_AUTO;
	else if (epp->ep_fcntl & EFX_FCNTL_RESPOND)
		fcntl = (epp->ep_fcntl & EFX_FCNTL_GENERATE)
			? MC_CMD_FCNTL_BIDIR
			: MC_CMD_FCNTL_RESPOND;
	else
		fcntl = MC_CMD_FCNTL_OFF;

	MCDI_IN_SET_DWORD(req, SET_MAC_IN_FCNTL, fcntl);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	/* Push multicast hash */

	if (epp->ep_all_mulcst) {
		/* A hash matching all multicast is all 1s */
		EFX_SET_OWORD(multicast_hash[0]);
		EFX_SET_OWORD(multicast_hash[1]);
	} else if (epp->ep_mulcst) {
		/* Use the hash set by the multicast list */
		multicast_hash[0] = epp->ep_multicst_hash[0];
		multicast_hash[1] = epp->ep_multicst_hash[1];
	} else {
		/* A hash matching no traffic is simply 0 */
		EFX_ZERO_OWORD(multicast_hash[0]);
		EFX_ZERO_OWORD(multicast_hash[1]);
	}

	/*
	 * Broadcast packets go through the multicast hash filter.
	 * The IEEE 802.3 CRC32 of the broadcast address is 0xbe2612ff
	 * so we always add bit 0xff to the mask (bit 0x7f in the
	 * second octword).
	 */
	if (epp->ep_brdcst) {
		/*
		 * NOTE: due to constant folding, some of this evaluates
		 * to null expressions, giving E_EXPR_NULL_EFFECT during
		 * lint on Illumos.  No good way to fix this without
		 * explicit coding the individual word/bit setting.
		 * So just suppress lint for this one line.
		 */
		/* LINTED */
		EFX_SET_OWORD_BIT(multicast_hash[1], 0x7f);
	}

	(void) memset(payload, 0, sizeof (payload));
	req.emr_cmd = MC_CMD_SET_MCAST_HASH;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_MCAST_HASH_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_MCAST_HASH_OUT_LEN;

	memcpy(MCDI_IN2(req, uint8_t, SET_MCAST_HASH_IN_HASH0),
	    multicast_hash, sizeof (multicast_hash));

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#if EFSYS_OPT_LOOPBACK

	__checkReturn	efx_rc_t
siena_mac_loopback_set(
	__in		efx_nic_t *enp,
	__in		efx_link_mode_t link_mode,
	__in		efx_loopback_type_t loopback_type)
{
	efx_port_t *epp = &(enp->en_port);
	const efx_phy_ops_t *epop = epp->ep_epop;
	efx_loopback_type_t old_loopback_type;
	efx_link_mode_t old_loopback_link_mode;
	efx_rc_t rc;

	/* The PHY object handles this on Siena */
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
siena_mac_stats_get_mask(
	__in				efx_nic_t *enp,
	__inout_bcount(mask_size)	uint32_t *maskp,
	__in				size_t mask_size)
{
	const struct efx_mac_stats_range siena_stats[] = {
		{ EFX_MAC_RX_OCTETS, EFX_MAC_RX_GE_15XX_PKTS },
		/* EFX_MAC_RX_ERRORS is not supported */
		{ EFX_MAC_RX_FCS_ERRORS, EFX_MAC_TX_EX_DEF_PKTS },
	};
	efx_rc_t rc;

	_NOTE(ARGUNUSED(enp))

	if ((rc = efx_mac_stats_mask_add_ranges(maskp, mask_size,
	    siena_stats, EFX_ARRAY_SIZE(siena_stats))) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#define	SIENA_MAC_STAT_READ(_esmp, _field, _eqp)			\
	EFSYS_MEM_READQ((_esmp), (_field) * sizeof (efx_qword_t), _eqp)

	__checkReturn			efx_rc_t
siena_mac_stats_update(
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

	if (encp->enc_mac_stats_nstats < MC_CMD_MAC_NSTATS) {
		/* MAC stats count too small */
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
	SIENA_MAC_STAT_READ(esmp, (encp->enc_mac_stats_nstats - 1),
	    &generation_end);
	EFSYS_MEM_READ_BARRIER();

	/* TX */
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_PKTS]), &value);
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_CONTROL_PKTS, &value);
	EFSYS_STAT_SUBR_QWORD(&(stat[EFX_MAC_TX_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_PAUSE_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_PAUSE_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_UNICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_UNICST_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_MULTICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_MULTICST_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_BROADCAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_BRDCST_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_BYTES, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_OCTETS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_LT64_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_LE_64_PKTS]), &value);
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_64_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_TX_LE_64_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_65_TO_127_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_65_TO_127_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_128_TO_255_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_128_TO_255_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_256_TO_511_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_256_TO_511_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_512_TO_1023_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_512_TO_1023_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_1024_TO_15XX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_1024_TO_15XX_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_15XX_TO_JUMBO_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_GE_15XX_PKTS]), &value);
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_GTJUMBO_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_TX_GE_15XX_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_BAD_FCS_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_ERRORS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_SINGLE_COLLISION_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_SGL_COL_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_MULTIPLE_COLLISION_PKTS,
			    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_MULT_COL_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_EXCESSIVE_COLLISION_PKTS,
			    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_EX_COL_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_LATE_COLLISION_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_LATE_COL_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_DEFERRED_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_DEF_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_TX_EXCESSIVE_DEFERRED_PKTS,
	    &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_TX_EX_DEF_PKTS]), &value);

	/* RX */
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_BYTES, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_OCTETS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_UNICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_UNICST_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_MULTICAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_MULTICST_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_BROADCAST_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_BRDCST_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_PAUSE_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_PAUSE_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_UNDERSIZE_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_LE_64_PKTS]), &value);
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_64_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_RX_LE_64_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_65_TO_127_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_65_TO_127_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_128_TO_255_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_128_TO_255_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_256_TO_511_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_256_TO_511_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_512_TO_1023_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_512_TO_1023_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_1024_TO_15XX_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_1024_TO_15XX_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_15XX_TO_JUMBO_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_GE_15XX_PKTS]), &value);
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_GTJUMBO_PKTS, &value);
	EFSYS_STAT_INCR_QWORD(&(stat[EFX_MAC_RX_GE_15XX_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_BAD_FCS_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_FCS_ERRORS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_OVERFLOW_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_DROP_EVENTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_FALSE_CARRIER_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_FALSE_CARRIER_ERRORS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_SYMBOL_ERROR_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_SYMBOL_ERRORS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_ALIGN_ERROR_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_ALIGN_ERRORS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_INTERNAL_ERROR_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_INTERNAL_ERRORS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_JABBER_PKTS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_JABBER_PKTS]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES01_CHAR_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE0_CHAR_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE1_CHAR_ERR]),
			    &(value.eq_dword[1]));

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES23_CHAR_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE2_CHAR_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE3_CHAR_ERR]),
			    &(value.eq_dword[1]));

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES01_DISP_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE0_DISP_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE1_DISP_ERR]),
			    &(value.eq_dword[1]));

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_LANES23_DISP_ERR, &value);
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE2_DISP_ERR]),
			    &(value.eq_dword[0]));
	EFSYS_STAT_SET_DWORD(&(stat[EFX_MAC_RX_LANE3_DISP_ERR]),
			    &(value.eq_dword[1]));

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_MATCH_FAULT, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_MATCH_FAULT]), &value);

	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_RX_NODESC_DROPS, &value);
	EFSYS_STAT_SET_QWORD(&(stat[EFX_MAC_RX_NODESC_DROP_CNT]), &value);

	EFSYS_DMA_SYNC_FOR_KERNEL(esmp, 0, EFSYS_MEM_SIZE(esmp));
	EFSYS_MEM_READ_BARRIER();
	SIENA_MAC_STAT_READ(esmp, MC_CMD_MAC_GENERATION_START,
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

	__checkReturn		efx_rc_t
siena_mac_pdu_get(
	__in		efx_nic_t *enp,
	__out		size_t *pdu)
{
	return (ENOTSUP);
}

#endif	/* EFSYS_OPT_SIENA */
