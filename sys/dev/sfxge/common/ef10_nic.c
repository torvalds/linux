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
#if EFSYS_OPT_MON_MCDI
#include "mcdi_mon.h"
#endif

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2

#include "ef10_tlv_layout.h"

	__checkReturn	efx_rc_t
efx_mcdi_get_port_assignment(
	__in		efx_nic_t *enp,
	__out		uint32_t *portp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_PORT_ASSIGNMENT_IN_LEN,
		MC_CMD_GET_PORT_ASSIGNMENT_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	req.emr_cmd = MC_CMD_GET_PORT_ASSIGNMENT;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_PORT_ASSIGNMENT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_PORT_ASSIGNMENT_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_PORT_ASSIGNMENT_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	*portp = MCDI_OUT_DWORD(req, GET_PORT_ASSIGNMENT_OUT_PORT);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mcdi_get_port_modes(
	__in		efx_nic_t *enp,
	__out		uint32_t *modesp,
	__out_opt	uint32_t *current_modep,
	__out_opt	uint32_t *default_modep)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_PORT_MODES_IN_LEN,
		MC_CMD_GET_PORT_MODES_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	req.emr_cmd = MC_CMD_GET_PORT_MODES;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_PORT_MODES_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_PORT_MODES_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	/*
	 * Require only Modes and DefaultMode fields, unless the current mode
	 * was requested (CurrentMode field was added for Medford).
	 */
	if (req.emr_out_length_used <
	    MC_CMD_GET_PORT_MODES_OUT_CURRENT_MODE_OFST) {
		rc = EMSGSIZE;
		goto fail2;
	}
	if ((current_modep != NULL) && (req.emr_out_length_used <
	    MC_CMD_GET_PORT_MODES_OUT_CURRENT_MODE_OFST + 4)) {
		rc = EMSGSIZE;
		goto fail3;
	}

	*modesp = MCDI_OUT_DWORD(req, GET_PORT_MODES_OUT_MODES);

	if (current_modep != NULL) {
		*current_modep = MCDI_OUT_DWORD(req,
					    GET_PORT_MODES_OUT_CURRENT_MODE);
	}

	if (default_modep != NULL) {
		*default_modep = MCDI_OUT_DWORD(req,
					    GET_PORT_MODES_OUT_DEFAULT_MODE);
	}

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_nic_get_port_mode_bandwidth(
	__in		efx_nic_t *enp,
	__out		uint32_t *bandwidth_mbpsp)
{
	uint32_t port_modes;
	uint32_t current_mode;
	efx_port_t *epp = &(enp->en_port);

	uint32_t single_lane;
	uint32_t dual_lane;
	uint32_t quad_lane;
	uint32_t bandwidth;
	efx_rc_t rc;

	if ((rc = efx_mcdi_get_port_modes(enp, &port_modes,
				    &current_mode, NULL)) != 0) {
		/* No port mode info available. */
		goto fail1;
	}

	if (epp->ep_phy_cap_mask & (1 << EFX_PHY_CAP_25000FDX))
		single_lane = 25000;
	else
		single_lane = 10000;

	if (epp->ep_phy_cap_mask & (1 << EFX_PHY_CAP_50000FDX))
		dual_lane = 50000;
	else
		dual_lane = 20000;

	if (epp->ep_phy_cap_mask & (1 << EFX_PHY_CAP_100000FDX))
		quad_lane = 100000;
	else
		quad_lane = 40000;

	switch (current_mode) {
	case TLV_PORT_MODE_1x1_NA:			/* mode 0 */
		bandwidth = single_lane;
		break;
	case TLV_PORT_MODE_1x2_NA:			/* mode 10 */
	case TLV_PORT_MODE_NA_1x2:			/* mode 11 */
		bandwidth = dual_lane;
		break;
	case TLV_PORT_MODE_1x1_1x1:			/* mode 2 */
		bandwidth = single_lane + single_lane;
		break;
	case TLV_PORT_MODE_4x1_NA:			/* mode 4 */
	case TLV_PORT_MODE_NA_4x1:			/* mode 8 */
		bandwidth = 4 * single_lane;
		break;
	case TLV_PORT_MODE_2x1_2x1:			/* mode 5 */
		bandwidth = (2 * single_lane) + (2 * single_lane);
		break;
	case TLV_PORT_MODE_1x2_1x2:			/* mode 12 */
		bandwidth = dual_lane + dual_lane;
		break;
	case TLV_PORT_MODE_1x2_2x1:			/* mode 17 */
	case TLV_PORT_MODE_2x1_1x2:			/* mode 18 */
		bandwidth = dual_lane + (2 * single_lane);
		break;
	/* Legacy Medford-only mode. Do not use (see bug63270) */
	case TLV_PORT_MODE_10G_10G_10G_10G_Q1_Q2:	/* mode 9 */
		bandwidth = 4 * single_lane;
		break;
	case TLV_PORT_MODE_1x4_NA:			/* mode 1 */
	case TLV_PORT_MODE_NA_1x4:			/* mode 22 */
		bandwidth = quad_lane;
		break;
	case TLV_PORT_MODE_2x2_NA:			/* mode 13 */
	case TLV_PORT_MODE_NA_2x2:			/* mode 14 */
		bandwidth = 2 * dual_lane;
		break;
	case TLV_PORT_MODE_1x4_2x1:			/* mode 6 */
	case TLV_PORT_MODE_2x1_1x4:			/* mode 7 */
		bandwidth = quad_lane + (2 * single_lane);
		break;
	case TLV_PORT_MODE_1x4_1x2:			/* mode 15 */
	case TLV_PORT_MODE_1x2_1x4:			/* mode 16 */
		bandwidth = quad_lane + dual_lane;
		break;
	case TLV_PORT_MODE_1x4_1x4:			/* mode 3 */
		bandwidth = quad_lane + quad_lane;
		break;
	default:
		rc = EINVAL;
		goto fail2;
	}

	*bandwidth_mbpsp = bandwidth;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn		efx_rc_t
efx_mcdi_vadaptor_alloc(
	__in			efx_nic_t *enp,
	__in			uint32_t port_id)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_VADAPTOR_ALLOC_IN_LEN,
		MC_CMD_VADAPTOR_ALLOC_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_vport_id, ==, EVB_PORT_ID_NULL);

	req.emr_cmd = MC_CMD_VADAPTOR_ALLOC;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_VADAPTOR_ALLOC_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_VADAPTOR_ALLOC_OUT_LEN;

	MCDI_IN_SET_DWORD(req, VADAPTOR_ALLOC_IN_UPSTREAM_PORT_ID, port_id);
	MCDI_IN_POPULATE_DWORD_1(req, VADAPTOR_ALLOC_IN_FLAGS,
	    VADAPTOR_ALLOC_IN_FLAG_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED,
	    enp->en_nic_cfg.enc_allow_set_mac_with_installed_filters ? 1 : 0);

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
efx_mcdi_vadaptor_free(
	__in			efx_nic_t *enp,
	__in			uint32_t port_id)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_VADAPTOR_FREE_IN_LEN,
		MC_CMD_VADAPTOR_FREE_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_VADAPTOR_FREE;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_VADAPTOR_FREE_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_VADAPTOR_FREE_OUT_LEN;

	MCDI_IN_SET_DWORD(req, VADAPTOR_FREE_IN_UPSTREAM_PORT_ID, port_id);

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
efx_mcdi_get_mac_address_pf(
	__in			efx_nic_t *enp,
	__out_ecount_opt(6)	uint8_t mac_addrp[6])
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_MAC_ADDRESSES_IN_LEN,
		MC_CMD_GET_MAC_ADDRESSES_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	req.emr_cmd = MC_CMD_GET_MAC_ADDRESSES;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_MAC_ADDRESSES_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_MAC_ADDRESSES_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_MAC_ADDRESSES_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	if (MCDI_OUT_DWORD(req, GET_MAC_ADDRESSES_OUT_MAC_COUNT) < 1) {
		rc = ENOENT;
		goto fail3;
	}

	if (mac_addrp != NULL) {
		uint8_t *addrp;

		addrp = MCDI_OUT2(req, uint8_t,
		    GET_MAC_ADDRESSES_OUT_MAC_ADDR_BASE);

		EFX_MAC_ADDR_COPY(mac_addrp, addrp);
	}

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mcdi_get_mac_address_vf(
	__in			efx_nic_t *enp,
	__out_ecount_opt(6)	uint8_t mac_addrp[6])
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_VPORT_GET_MAC_ADDRESSES_IN_LEN,
		MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_LENMAX);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	req.emr_cmd = MC_CMD_VPORT_GET_MAC_ADDRESSES;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_VPORT_GET_MAC_ADDRESSES_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_LENMAX;

	MCDI_IN_SET_DWORD(req, VPORT_GET_MAC_ADDRESSES_IN_VPORT_ID,
	    EVB_PORT_ID_ASSIGNED);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used <
	    MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_LENMIN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	if (MCDI_OUT_DWORD(req,
		VPORT_GET_MAC_ADDRESSES_OUT_MACADDR_COUNT) < 1) {
		rc = ENOENT;
		goto fail3;
	}

	if (mac_addrp != NULL) {
		uint8_t *addrp;

		addrp = MCDI_OUT2(req, uint8_t,
		    VPORT_GET_MAC_ADDRESSES_OUT_MACADDR);

		EFX_MAC_ADDR_COPY(mac_addrp, addrp);
	}

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mcdi_get_clock(
	__in		efx_nic_t *enp,
	__out		uint32_t *sys_freqp,
	__out		uint32_t *dpcpu_freqp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_CLOCK_IN_LEN,
		MC_CMD_GET_CLOCK_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	req.emr_cmd = MC_CMD_GET_CLOCK;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_CLOCK_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_CLOCK_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_CLOCK_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	*sys_freqp = MCDI_OUT_DWORD(req, GET_CLOCK_OUT_SYS_FREQ);
	if (*sys_freqp == 0) {
		rc = EINVAL;
		goto fail3;
	}
	*dpcpu_freqp = MCDI_OUT_DWORD(req, GET_CLOCK_OUT_DPCPU_FREQ);
	if (*dpcpu_freqp == 0) {
		rc = EINVAL;
		goto fail4;
	}

	return (0);

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

	__checkReturn	efx_rc_t
efx_mcdi_get_rxdp_config(
	__in		efx_nic_t *enp,
	__out		uint32_t *end_paddingp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_RXDP_CONFIG_IN_LEN,
		MC_CMD_GET_RXDP_CONFIG_OUT_LEN);
	uint32_t end_padding;
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_GET_RXDP_CONFIG;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_RXDP_CONFIG_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_RXDP_CONFIG_OUT_LEN;

	efx_mcdi_execute(enp, &req);
	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (MCDI_OUT_DWORD_FIELD(req, GET_RXDP_CONFIG_OUT_DATA,
				    GET_RXDP_CONFIG_OUT_PAD_HOST_DMA) == 0) {
		/* RX DMA end padding is disabled */
		end_padding = 0;
	} else {
		switch (MCDI_OUT_DWORD_FIELD(req, GET_RXDP_CONFIG_OUT_DATA,
					    GET_RXDP_CONFIG_OUT_PAD_HOST_LEN)) {
		case MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_64:
			end_padding = 64;
			break;
		case MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_128:
			end_padding = 128;
			break;
		case MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_256:
			end_padding = 256;
			break;
		default:
			rc = ENOTSUP;
			goto fail2;
		}
	}

	*end_paddingp = end_padding;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mcdi_get_vector_cfg(
	__in		efx_nic_t *enp,
	__out_opt	uint32_t *vec_basep,
	__out_opt	uint32_t *pf_nvecp,
	__out_opt	uint32_t *vf_nvecp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_VECTOR_CFG_IN_LEN,
		MC_CMD_GET_VECTOR_CFG_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_GET_VECTOR_CFG;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_VECTOR_CFG_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_VECTOR_CFG_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_VECTOR_CFG_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	if (vec_basep != NULL)
		*vec_basep = MCDI_OUT_DWORD(req, GET_VECTOR_CFG_OUT_VEC_BASE);
	if (pf_nvecp != NULL)
		*pf_nvecp = MCDI_OUT_DWORD(req, GET_VECTOR_CFG_OUT_VECS_PER_PF);
	if (vf_nvecp != NULL)
		*vf_nvecp = MCDI_OUT_DWORD(req, GET_VECTOR_CFG_OUT_VECS_PER_VF);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_alloc_vis(
	__in		efx_nic_t *enp,
	__in		uint32_t min_vi_count,
	__in		uint32_t max_vi_count,
	__out		uint32_t *vi_basep,
	__out		uint32_t *vi_countp,
	__out		uint32_t *vi_shiftp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_ALLOC_VIS_IN_LEN,
		MC_CMD_ALLOC_VIS_EXT_OUT_LEN);
	efx_rc_t rc;

	if (vi_countp == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	req.emr_cmd = MC_CMD_ALLOC_VIS;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_ALLOC_VIS_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_ALLOC_VIS_EXT_OUT_LEN;

	MCDI_IN_SET_DWORD(req, ALLOC_VIS_IN_MIN_VI_COUNT, min_vi_count);
	MCDI_IN_SET_DWORD(req, ALLOC_VIS_IN_MAX_VI_COUNT, max_vi_count);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	if (req.emr_out_length_used < MC_CMD_ALLOC_VIS_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail3;
	}

	*vi_basep = MCDI_OUT_DWORD(req, ALLOC_VIS_OUT_VI_BASE);
	*vi_countp = MCDI_OUT_DWORD(req, ALLOC_VIS_OUT_VI_COUNT);

	/* Report VI_SHIFT if available (always zero for Huntington) */
	if (req.emr_out_length_used < MC_CMD_ALLOC_VIS_EXT_OUT_LEN)
		*vi_shiftp = 0;
	else
		*vi_shiftp = MCDI_OUT_DWORD(req, ALLOC_VIS_EXT_OUT_VI_SHIFT);

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


static	__checkReturn	efx_rc_t
efx_mcdi_free_vis(
	__in		efx_nic_t *enp)
{
	efx_mcdi_req_t req;
	efx_rc_t rc;

	EFX_STATIC_ASSERT(MC_CMD_FREE_VIS_IN_LEN == 0);
	EFX_STATIC_ASSERT(MC_CMD_FREE_VIS_OUT_LEN == 0);

	req.emr_cmd = MC_CMD_FREE_VIS;
	req.emr_in_buf = NULL;
	req.emr_in_length = 0;
	req.emr_out_buf = NULL;
	req.emr_out_length = 0;

	efx_mcdi_execute_quiet(enp, &req);

	/* Ignore ELREADY (no allocated VIs, so nothing to free) */
	if ((req.emr_rc != 0) && (req.emr_rc != EALREADY)) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


static	__checkReturn	efx_rc_t
efx_mcdi_alloc_piobuf(
	__in		efx_nic_t *enp,
	__out		efx_piobuf_handle_t *handlep)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_ALLOC_PIOBUF_IN_LEN,
		MC_CMD_ALLOC_PIOBUF_OUT_LEN);
	efx_rc_t rc;

	if (handlep == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	req.emr_cmd = MC_CMD_ALLOC_PIOBUF;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_ALLOC_PIOBUF_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_ALLOC_PIOBUF_OUT_LEN;

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	if (req.emr_out_length_used < MC_CMD_ALLOC_PIOBUF_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail3;
	}

	*handlep = MCDI_OUT_DWORD(req, ALLOC_PIOBUF_OUT_PIOBUF_HANDLE);

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_free_piobuf(
	__in		efx_nic_t *enp,
	__in		efx_piobuf_handle_t handle)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_FREE_PIOBUF_IN_LEN,
		MC_CMD_FREE_PIOBUF_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_FREE_PIOBUF;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_FREE_PIOBUF_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_FREE_PIOBUF_OUT_LEN;

	MCDI_IN_SET_DWORD(req, FREE_PIOBUF_IN_PIOBUF_HANDLE, handle);

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
efx_mcdi_link_piobuf(
	__in		efx_nic_t *enp,
	__in		uint32_t vi_index,
	__in		efx_piobuf_handle_t handle)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_LINK_PIOBUF_IN_LEN,
		MC_CMD_LINK_PIOBUF_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_LINK_PIOBUF;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_LINK_PIOBUF_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_LINK_PIOBUF_OUT_LEN;

	MCDI_IN_SET_DWORD(req, LINK_PIOBUF_IN_PIOBUF_HANDLE, handle);
	MCDI_IN_SET_DWORD(req, LINK_PIOBUF_IN_TXQ_INSTANCE, vi_index);

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

static	__checkReturn	efx_rc_t
efx_mcdi_unlink_piobuf(
	__in		efx_nic_t *enp,
	__in		uint32_t vi_index)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_UNLINK_PIOBUF_IN_LEN,
		MC_CMD_UNLINK_PIOBUF_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_UNLINK_PIOBUF;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_UNLINK_PIOBUF_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_UNLINK_PIOBUF_OUT_LEN;

	MCDI_IN_SET_DWORD(req, UNLINK_PIOBUF_IN_TXQ_INSTANCE, vi_index);

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static			void
ef10_nic_alloc_piobufs(
	__in		efx_nic_t *enp,
	__in		uint32_t max_piobuf_count)
{
	efx_piobuf_handle_t *handlep;
	unsigned int i;

	EFSYS_ASSERT3U(max_piobuf_count, <=,
	    EFX_ARRAY_SIZE(enp->en_arch.ef10.ena_piobuf_handle));

	enp->en_arch.ef10.ena_piobuf_count = 0;

	for (i = 0; i < max_piobuf_count; i++) {
		handlep = &enp->en_arch.ef10.ena_piobuf_handle[i];

		if (efx_mcdi_alloc_piobuf(enp, handlep) != 0)
			goto fail1;

		enp->en_arch.ef10.ena_pio_alloc_map[i] = 0;
		enp->en_arch.ef10.ena_piobuf_count++;
	}

	return;

fail1:
	for (i = 0; i < enp->en_arch.ef10.ena_piobuf_count; i++) {
		handlep = &enp->en_arch.ef10.ena_piobuf_handle[i];

		(void) efx_mcdi_free_piobuf(enp, *handlep);
		*handlep = EFX_PIOBUF_HANDLE_INVALID;
	}
	enp->en_arch.ef10.ena_piobuf_count = 0;
}


static			void
ef10_nic_free_piobufs(
	__in		efx_nic_t *enp)
{
	efx_piobuf_handle_t *handlep;
	unsigned int i;

	for (i = 0; i < enp->en_arch.ef10.ena_piobuf_count; i++) {
		handlep = &enp->en_arch.ef10.ena_piobuf_handle[i];

		(void) efx_mcdi_free_piobuf(enp, *handlep);
		*handlep = EFX_PIOBUF_HANDLE_INVALID;
	}
	enp->en_arch.ef10.ena_piobuf_count = 0;
}

/* Sub-allocate a block from a piobuf */
	__checkReturn	efx_rc_t
ef10_nic_pio_alloc(
	__inout		efx_nic_t *enp,
	__out		uint32_t *bufnump,
	__out		efx_piobuf_handle_t *handlep,
	__out		uint32_t *blknump,
	__out		uint32_t *offsetp,
	__out		size_t *sizep)
{
	efx_nic_cfg_t *encp = &enp->en_nic_cfg;
	efx_drv_cfg_t *edcp = &enp->en_drv_cfg;
	uint32_t blk_per_buf;
	uint32_t buf, blk;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);
	EFSYS_ASSERT(bufnump);
	EFSYS_ASSERT(handlep);
	EFSYS_ASSERT(blknump);
	EFSYS_ASSERT(offsetp);
	EFSYS_ASSERT(sizep);

	if ((edcp->edc_pio_alloc_size == 0) ||
	    (enp->en_arch.ef10.ena_piobuf_count == 0)) {
		rc = ENOMEM;
		goto fail1;
	}
	blk_per_buf = encp->enc_piobuf_size / edcp->edc_pio_alloc_size;

	for (buf = 0; buf < enp->en_arch.ef10.ena_piobuf_count; buf++) {
		uint32_t *map = &enp->en_arch.ef10.ena_pio_alloc_map[buf];

		if (~(*map) == 0)
			continue;

		EFSYS_ASSERT3U(blk_per_buf, <=, (8 * sizeof (*map)));
		for (blk = 0; blk < blk_per_buf; blk++) {
			if ((*map & (1u << blk)) == 0) {
				*map |= (1u << blk);
				goto done;
			}
		}
	}
	rc = ENOMEM;
	goto fail2;

done:
	*handlep = enp->en_arch.ef10.ena_piobuf_handle[buf];
	*bufnump = buf;
	*blknump = blk;
	*sizep = edcp->edc_pio_alloc_size;
	*offsetp = blk * (*sizep);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/* Free a piobuf sub-allocated block */
	__checkReturn	efx_rc_t
ef10_nic_pio_free(
	__inout		efx_nic_t *enp,
	__in		uint32_t bufnum,
	__in		uint32_t blknum)
{
	uint32_t *map;
	efx_rc_t rc;

	if ((bufnum >= enp->en_arch.ef10.ena_piobuf_count) ||
	    (blknum >= (8 * sizeof (*map)))) {
		rc = EINVAL;
		goto fail1;
	}

	map = &enp->en_arch.ef10.ena_pio_alloc_map[bufnum];
	if ((*map & (1u << blknum)) == 0) {
		rc = ENOENT;
		goto fail2;
	}
	*map &= ~(1u << blknum);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_nic_pio_link(
	__inout		efx_nic_t *enp,
	__in		uint32_t vi_index,
	__in		efx_piobuf_handle_t handle)
{
	return (efx_mcdi_link_piobuf(enp, vi_index, handle));
}

	__checkReturn	efx_rc_t
ef10_nic_pio_unlink(
	__inout		efx_nic_t *enp,
	__in		uint32_t vi_index)
{
	return (efx_mcdi_unlink_piobuf(enp, vi_index));
}

static	__checkReturn	efx_rc_t
ef10_mcdi_get_pf_count(
	__in		efx_nic_t *enp,
	__out		uint32_t *pf_countp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_PF_COUNT_IN_LEN,
		MC_CMD_GET_PF_COUNT_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_GET_PF_COUNT;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_PF_COUNT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_PF_COUNT_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_PF_COUNT_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	*pf_countp = *MCDI_OUT(req, uint8_t,
				MC_CMD_GET_PF_COUNT_OUT_PF_COUNT_OFST);

	EFSYS_ASSERT(*pf_countp != 0);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
ef10_get_datapath_caps(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_CAPABILITIES_IN_LEN,
		MC_CMD_GET_CAPABILITIES_V5_OUT_LEN);
	efx_rc_t rc;

	if ((rc = ef10_mcdi_get_pf_count(enp, &encp->enc_hw_pf_count)) != 0)
		goto fail1;


	req.emr_cmd = MC_CMD_GET_CAPABILITIES;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_CAPABILITIES_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_CAPABILITIES_V5_OUT_LEN;

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	if (req.emr_out_length_used < MC_CMD_GET_CAPABILITIES_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail3;
	}

#define	CAP_FLAGS1(_req, _flag)						\
	(MCDI_OUT_DWORD((_req), GET_CAPABILITIES_OUT_FLAGS1) &		\
	(1u << (MC_CMD_GET_CAPABILITIES_V2_OUT_ ## _flag ## _LBN)))

#define	CAP_FLAGS2(_req, _flag)						\
	(((_req).emr_out_length_used >= MC_CMD_GET_CAPABILITIES_V2_OUT_LEN) && \
	    (MCDI_OUT_DWORD((_req), GET_CAPABILITIES_V2_OUT_FLAGS2) &	\
	    (1u << (MC_CMD_GET_CAPABILITIES_V2_OUT_ ## _flag ## _LBN))))

	/*
	 * Huntington RXDP firmware inserts a 0 or 14 byte prefix.
	 * We only support the 14 byte prefix here.
	 */
	if (CAP_FLAGS1(req, RX_PREFIX_LEN_14) == 0) {
		rc = ENOTSUP;
		goto fail4;
	}
	encp->enc_rx_prefix_size = 14;

#if EFSYS_OPT_RX_SCALE
	/* Check if the firmware supports additional RSS modes */
	if (CAP_FLAGS1(req, ADDITIONAL_RSS_MODES))
		encp->enc_rx_scale_additional_modes_supported = B_TRUE;
	else
		encp->enc_rx_scale_additional_modes_supported = B_FALSE;
#endif /* EFSYS_OPT_RX_SCALE */

	/* Check if the firmware supports TSO */
	if (CAP_FLAGS1(req, TX_TSO))
		encp->enc_fw_assisted_tso_enabled = B_TRUE;
	else
		encp->enc_fw_assisted_tso_enabled = B_FALSE;

	/* Check if the firmware supports FATSOv2 */
	if (CAP_FLAGS2(req, TX_TSO_V2)) {
		encp->enc_fw_assisted_tso_v2_enabled = B_TRUE;
		encp->enc_fw_assisted_tso_v2_n_contexts = MCDI_OUT_WORD(req,
		    GET_CAPABILITIES_V2_OUT_TX_TSO_V2_N_CONTEXTS);
	} else {
		encp->enc_fw_assisted_tso_v2_enabled = B_FALSE;
		encp->enc_fw_assisted_tso_v2_n_contexts = 0;
	}

	/* Check if the firmware supports FATSOv2 encap */
	if (CAP_FLAGS2(req, TX_TSO_V2_ENCAP))
		encp->enc_fw_assisted_tso_v2_encap_enabled = B_TRUE;
	else
		encp->enc_fw_assisted_tso_v2_encap_enabled = B_FALSE;

	/* Check if the firmware has vadapter/vport/vswitch support */
	if (CAP_FLAGS1(req, EVB))
		encp->enc_datapath_cap_evb = B_TRUE;
	else
		encp->enc_datapath_cap_evb = B_FALSE;

	/* Check if the firmware supports VLAN insertion */
	if (CAP_FLAGS1(req, TX_VLAN_INSERTION))
		encp->enc_hw_tx_insert_vlan_enabled = B_TRUE;
	else
		encp->enc_hw_tx_insert_vlan_enabled = B_FALSE;

	/* Check if the firmware supports RX event batching */
	if (CAP_FLAGS1(req, RX_BATCHING))
		encp->enc_rx_batching_enabled = B_TRUE;
	else
		encp->enc_rx_batching_enabled = B_FALSE;

	/*
	 * Even if batching isn't reported as supported, we may still get
	 * batched events.
	 */
	encp->enc_rx_batch_max = 16;

	/* Check if the firmware supports disabling scatter on RXQs */
	if (CAP_FLAGS1(req, RX_DISABLE_SCATTER))
		encp->enc_rx_disable_scatter_supported = B_TRUE;
	else
		encp->enc_rx_disable_scatter_supported = B_FALSE;

	/* Check if the firmware supports packed stream mode */
	if (CAP_FLAGS1(req, RX_PACKED_STREAM))
		encp->enc_rx_packed_stream_supported = B_TRUE;
	else
		encp->enc_rx_packed_stream_supported = B_FALSE;

	/*
	 * Check if the firmware supports configurable buffer sizes
	 * for packed stream mode (otherwise buffer size is 1Mbyte)
	 */
	if (CAP_FLAGS1(req, RX_PACKED_STREAM_VAR_BUFFERS))
		encp->enc_rx_var_packed_stream_supported = B_TRUE;
	else
		encp->enc_rx_var_packed_stream_supported = B_FALSE;

	/* Check if the firmware supports equal stride super-buffer mode */
	if (CAP_FLAGS2(req, EQUAL_STRIDE_SUPER_BUFFER))
		encp->enc_rx_es_super_buffer_supported = B_TRUE;
	else
		encp->enc_rx_es_super_buffer_supported = B_FALSE;

	/* Check if the firmware supports FW subvariant w/o Tx checksumming */
	if (CAP_FLAGS2(req, FW_SUBVARIANT_NO_TX_CSUM))
		encp->enc_fw_subvariant_no_tx_csum_supported = B_TRUE;
	else
		encp->enc_fw_subvariant_no_tx_csum_supported = B_FALSE;

	/* Check if the firmware supports set mac with running filters */
	if (CAP_FLAGS1(req, VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED))
		encp->enc_allow_set_mac_with_installed_filters = B_TRUE;
	else
		encp->enc_allow_set_mac_with_installed_filters = B_FALSE;

	/*
	 * Check if firmware supports the extended MC_CMD_SET_MAC, which allows
	 * specifying which parameters to configure.
	 */
	if (CAP_FLAGS1(req, SET_MAC_ENHANCED))
		encp->enc_enhanced_set_mac_supported = B_TRUE;
	else
		encp->enc_enhanced_set_mac_supported = B_FALSE;

	/*
	 * Check if firmware supports version 2 of MC_CMD_INIT_EVQ, which allows
	 * us to let the firmware choose the settings to use on an EVQ.
	 */
	if (CAP_FLAGS2(req, INIT_EVQ_V2))
		encp->enc_init_evq_v2_supported = B_TRUE;
	else
		encp->enc_init_evq_v2_supported = B_FALSE;

	/*
	 * Check if firmware-verified NVRAM updates must be used.
	 *
	 * The firmware trusted installer requires all NVRAM updates to use
	 * version 2 of MC_CMD_NVRAM_UPDATE_START (to enable verified update)
	 * and version 2 of MC_CMD_NVRAM_UPDATE_FINISH (to verify the updated
	 * partition and report the result).
	 */
	if (CAP_FLAGS2(req, NVRAM_UPDATE_REPORT_VERIFY_RESULT))
		encp->enc_nvram_update_verify_result_supported = B_TRUE;
	else
		encp->enc_nvram_update_verify_result_supported = B_FALSE;

	/*
	 * Check if firmware provides packet memory and Rx datapath
	 * counters.
	 */
	if (CAP_FLAGS1(req, PM_AND_RXDP_COUNTERS))
		encp->enc_pm_and_rxdp_counters = B_TRUE;
	else
		encp->enc_pm_and_rxdp_counters = B_FALSE;

	/*
	 * Check if the 40G MAC hardware is capable of reporting
	 * statistics for Tx size bins.
	 */
	if (CAP_FLAGS2(req, MAC_STATS_40G_TX_SIZE_BINS))
		encp->enc_mac_stats_40g_tx_size_bins = B_TRUE;
	else
		encp->enc_mac_stats_40g_tx_size_bins = B_FALSE;

	/*
	 * Check if firmware supports VXLAN and NVGRE tunnels.
	 * The capability indicates Geneve protocol support as well.
	 */
	if (CAP_FLAGS1(req, VXLAN_NVGRE)) {
		encp->enc_tunnel_encapsulations_supported =
		    (1u << EFX_TUNNEL_PROTOCOL_VXLAN) |
		    (1u << EFX_TUNNEL_PROTOCOL_GENEVE) |
		    (1u << EFX_TUNNEL_PROTOCOL_NVGRE);

		EFX_STATIC_ASSERT(EFX_TUNNEL_MAXNENTRIES ==
		    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES_MAXNUM);
		encp->enc_tunnel_config_udp_entries_max =
		    EFX_TUNNEL_MAXNENTRIES;
	} else {
		encp->enc_tunnel_config_udp_entries_max = 0;
	}

	/*
	 * Check if firmware reports the VI window mode.
	 * Medford2 has a variable VI window size (8K, 16K or 64K).
	 * Medford and Huntington have a fixed 8K VI window size.
	 */
	if (req.emr_out_length_used >= MC_CMD_GET_CAPABILITIES_V3_OUT_LEN) {
		uint8_t mode =
		    MCDI_OUT_BYTE(req, GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE);

		switch (mode) {
		case MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_8K:
			encp->enc_vi_window_shift = EFX_VI_WINDOW_SHIFT_8K;
			break;
		case MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_16K:
			encp->enc_vi_window_shift = EFX_VI_WINDOW_SHIFT_16K;
			break;
		case MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_64K:
			encp->enc_vi_window_shift = EFX_VI_WINDOW_SHIFT_64K;
			break;
		default:
			encp->enc_vi_window_shift = EFX_VI_WINDOW_SHIFT_INVALID;
			break;
		}
	} else if ((enp->en_family == EFX_FAMILY_HUNTINGTON) ||
		    (enp->en_family == EFX_FAMILY_MEDFORD)) {
		/* Huntington and Medford have fixed 8K window size */
		encp->enc_vi_window_shift = EFX_VI_WINDOW_SHIFT_8K;
	} else {
		encp->enc_vi_window_shift = EFX_VI_WINDOW_SHIFT_INVALID;
	}

	/* Check if firmware supports extended MAC stats. */
	if (req.emr_out_length_used >= MC_CMD_GET_CAPABILITIES_V4_OUT_LEN) {
		/* Extended stats buffer supported */
		encp->enc_mac_stats_nstats = MCDI_OUT_WORD(req,
		    GET_CAPABILITIES_V4_OUT_MAC_STATS_NUM_STATS);
	} else {
		/* Use Siena-compatible legacy MAC stats */
		encp->enc_mac_stats_nstats = MC_CMD_MAC_NSTATS;
	}

	if (encp->enc_mac_stats_nstats >= MC_CMD_MAC_NSTATS_V2)
		encp->enc_fec_counters = B_TRUE;
	else
		encp->enc_fec_counters = B_FALSE;

	/* Check if the firmware provides head-of-line blocking counters */
	if (CAP_FLAGS2(req, RXDP_HLB_IDLE))
		encp->enc_hlb_counters = B_TRUE;
	else
		encp->enc_hlb_counters = B_FALSE;

#if EFSYS_OPT_RX_SCALE
	if (CAP_FLAGS1(req, RX_RSS_LIMITED)) {
		/* Only one exclusive RSS context is available per port. */
		encp->enc_rx_scale_max_exclusive_contexts = 1;

		switch (enp->en_family) {
		case EFX_FAMILY_MEDFORD2:
			encp->enc_rx_scale_hash_alg_mask =
			    (1U << EFX_RX_HASHALG_TOEPLITZ);
			break;

		case EFX_FAMILY_MEDFORD:
		case EFX_FAMILY_HUNTINGTON:
			/*
			 * Packed stream firmware variant maintains a
			 * non-standard algorithm for hash computation.
			 * It implies explicit XORing together
			 * source + destination IP addresses (or last
			 * four bytes in the case of IPv6) and using the
			 * resulting value as the input to a Toeplitz hash.
			 */
			encp->enc_rx_scale_hash_alg_mask =
			    (1U << EFX_RX_HASHALG_PACKED_STREAM);
			break;

		default:
			rc = EINVAL;
			goto fail5;
		}

		/* Port numbers cannot contribute to the hash value */
		encp->enc_rx_scale_l4_hash_supported = B_FALSE;
	} else {
		/*
		 * Maximum number of exclusive RSS contexts.
		 * EF10 hardware supports 64 in total, but 6 are reserved
		 * for shared contexts. They are a global resource so
		 * not all may be available.
		 */
		encp->enc_rx_scale_max_exclusive_contexts = 64 - 6;

		encp->enc_rx_scale_hash_alg_mask =
		    (1U << EFX_RX_HASHALG_TOEPLITZ);

		/*
		 * It is possible to use port numbers as
		 * the input data for hash computation.
		 */
		encp->enc_rx_scale_l4_hash_supported = B_TRUE;
	}
#endif /* EFSYS_OPT_RX_SCALE */

	/* Check if the firmware supports "FLAG" and "MARK" filter actions */
	if (CAP_FLAGS2(req, FILTER_ACTION_FLAG))
		encp->enc_filter_action_flag_supported = B_TRUE;
	else
		encp->enc_filter_action_flag_supported = B_FALSE;

	if (CAP_FLAGS2(req, FILTER_ACTION_MARK))
		encp->enc_filter_action_mark_supported = B_TRUE;
	else
		encp->enc_filter_action_mark_supported = B_FALSE;

	/* Get maximum supported value for "MARK" filter action */
	if (req.emr_out_length_used >= MC_CMD_GET_CAPABILITIES_V5_OUT_LEN)
		encp->enc_filter_action_mark_max = MCDI_OUT_DWORD(req,
		    GET_CAPABILITIES_V5_OUT_FILTER_ACTION_MARK_MAX);
	else
		encp->enc_filter_action_mark_max = 0;

#undef CAP_FLAGS1
#undef CAP_FLAGS2

	return (0);

#if EFSYS_OPT_RX_SCALE
fail5:
	EFSYS_PROBE(fail5);
#endif /* EFSYS_OPT_RX_SCALE */
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


#define	EF10_LEGACY_PF_PRIVILEGE_MASK					\
	(MC_CMD_PRIVILEGE_MASK_IN_GRP_ADMIN			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_LINK			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_ONLOAD			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_PTP			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_INSECURE_FILTERS		|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_MAC_SPOOFING		|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_UNICAST			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_BROADCAST			|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST		|	\
	MC_CMD_PRIVILEGE_MASK_IN_GRP_PROMISCUOUS)

#define	EF10_LEGACY_VF_PRIVILEGE_MASK	0


	__checkReturn		efx_rc_t
ef10_get_privilege_mask(
	__in			efx_nic_t *enp,
	__out			uint32_t *maskp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	uint32_t mask;
	efx_rc_t rc;

	if ((rc = efx_mcdi_privilege_mask(enp, encp->enc_pf, encp->enc_vf,
					    &mask)) != 0) {
		if (rc != ENOTSUP)
			goto fail1;

		/* Fallback for old firmware without privilege mask support */
		if (EFX_PCI_FUNCTION_IS_PF(encp)) {
			/* Assume PF has admin privilege */
			mask = EF10_LEGACY_PF_PRIVILEGE_MASK;
		} else {
			/* VF is always unprivileged by default */
			mask = EF10_LEGACY_VF_PRIVILEGE_MASK;
		}
	}

	*maskp = mask;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


#define	EFX_EXT_PORT_MAX	4
#define	EFX_EXT_PORT_NA		0xFF

/*
 * Table of mapping schemes from port number to external number.
 *
 * Each port number ultimately corresponds to a connector: either as part of
 * a cable assembly attached to a module inserted in an SFP+/QSFP+ cage on
 * the board, or fixed to the board (e.g. 10GBASE-T magjack on SFN5121T
 * "Salina"). In general:
 *
 * Port number (0-based)
 *     |
 *   port mapping (n:1)
 *     |
 *     v
 * External port number (1-based)
 *     |
 *   fixed (1:1) or cable assembly (1:m)
 *     |
 *     v
 * Connector
 *
 * The external numbering refers to the cages or magjacks on the board,
 * as visibly annotated on the board or back panel. This table describes
 * how to determine which external cage/magjack corresponds to the port
 * numbers used by the driver.
 *
 * The count of consecutive port numbers that map to each external number,
 * is determined by the chip family and the current port mode.
 *
 * For the Huntington family, the current port mode cannot be discovered,
 * but a single mapping is used by all modes for a given chip variant,
 * so the mapping used is instead the last match in the table to the full
 * set of port modes to which the NIC can be configured. Therefore the
 * ordering of entries in the mapping table is significant.
 */
static struct ef10_external_port_map_s {
	efx_family_t	family;
	uint32_t	modes_mask;
	uint8_t		base_port[EFX_EXT_PORT_MAX];
}	__ef10_external_port_mappings[] = {
	/*
	 * Modes used by Huntington family controllers where each port
	 * number maps to a separate cage.
	 * SFN7x22F (Torino):
	 *	port 0 -> cage 1
	 *	port 1 -> cage 2
	 * SFN7xx4F (Pavia):
	 *	port 0 -> cage 1
	 *	port 1 -> cage 2
	 *	port 2 -> cage 3
	 *	port 3 -> cage 4
	 */
	{
		EFX_FAMILY_HUNTINGTON,
		(1U << TLV_PORT_MODE_10G) |			/* mode 0 */
		(1U << TLV_PORT_MODE_10G_10G) |			/* mode 2 */
		(1U << TLV_PORT_MODE_10G_10G_10G_10G),		/* mode 4 */
		{ 0, 1, 2, 3 }
	},
	/*
	 * Modes which for Huntington identify a chip variant where 2
	 * adjacent port numbers map to each cage.
	 * SFN7x42Q (Monza):
	 *	port 0 -> cage 1
	 *	port 1 -> cage 1
	 *	port 2 -> cage 2
	 *	port 3 -> cage 2
	 */
	{
		EFX_FAMILY_HUNTINGTON,
		(1U << TLV_PORT_MODE_40G) |			/* mode 1 */
		(1U << TLV_PORT_MODE_40G_40G) |			/* mode 3 */
		(1U << TLV_PORT_MODE_40G_10G_10G) |		/* mode 6 */
		(1U << TLV_PORT_MODE_10G_10G_40G),		/* mode 7 */
		{ 0, 2, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford allocate each port number to a separate
	 * cage.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 2
	 *	port 2 -> cage 3
	 *	port 3 -> cage 4
	 */
	{
		EFX_FAMILY_MEDFORD,
		(1U << TLV_PORT_MODE_1x1_NA) |			/* mode 0 */
		(1U << TLV_PORT_MODE_1x1_1x1),			/* mode 2 */
		{ 0, 1, 2, 3 }
	},
	/*
	 * Modes that on Medford allocate 2 adjacent port numbers to each
	 * cage.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 1
	 *	port 2 -> cage 2
	 *	port 3 -> cage 2
	 */
	{
		EFX_FAMILY_MEDFORD,
		(1U << TLV_PORT_MODE_1x4_NA) |			/* mode 1 */
		(1U << TLV_PORT_MODE_1x4_1x4) |			/* mode 3 */
		(1U << TLV_PORT_MODE_1x4_2x1) |			/* mode 6 */
		(1U << TLV_PORT_MODE_2x1_1x4) |			/* mode 7 */
		/* Do not use 10G_10G_10G_10G_Q1_Q2 (see bug63270) */
		(1U << TLV_PORT_MODE_10G_10G_10G_10G_Q1_Q2),	/* mode 9 */
		{ 0, 2, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford allocate 4 adjacent port numbers to each
	 * connector, starting on cage 1.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 1
	 *	port 2 -> cage 1
	 *	port 3 -> cage 1
	 */
	{
		EFX_FAMILY_MEDFORD,
		(1U << TLV_PORT_MODE_2x1_2x1) |			/* mode 5 */
		/* Do not use 10G_10G_10G_10G_Q1 (see bug63270) */
		(1U << TLV_PORT_MODE_4x1_NA),			/* mode 4 */
		{ 0, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford allocate 4 adjacent port numbers to each
	 * connector, starting on cage 2.
	 *	port 0 -> cage 2
	 *	port 1 -> cage 2
	 *	port 2 -> cage 2
	 *	port 3 -> cage 2
	 */
	{
		EFX_FAMILY_MEDFORD,
		(1U << TLV_PORT_MODE_NA_4x1),			/* mode 8 */
		{ EFX_EXT_PORT_NA, 0, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford2 allocate each port number to a separate
	 * cage.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 2
	 *	port 2 -> cage 3
	 *	port 3 -> cage 4
	 */
	{
		EFX_FAMILY_MEDFORD2,
		(1U << TLV_PORT_MODE_1x1_NA) |			/* mode 0 */
		(1U << TLV_PORT_MODE_1x4_NA) |			/* mode 1 */
		(1U << TLV_PORT_MODE_1x1_1x1) |			/* mode 2 */
		(1U << TLV_PORT_MODE_1x2_NA) |			/* mode 10 */
		(1U << TLV_PORT_MODE_1x2_1x2) |			/* mode 12 */
		(1U << TLV_PORT_MODE_1x4_1x2) |			/* mode 15 */
		(1U << TLV_PORT_MODE_1x2_1x4),			/* mode 16 */
		{ 0, 1, 2, 3 }
	},
	/*
	 * Modes that on Medford2 allocate 1 port to cage 1 and the rest
	 * to cage 2.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 2
	 *	port 2 -> cage 2
	 */
	{
		EFX_FAMILY_MEDFORD2,
		(1U << TLV_PORT_MODE_1x2_2x1) |			/* mode 17 */
		(1U << TLV_PORT_MODE_1x4_2x1),			/* mode 6 */
		{ 0, 1, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford2 allocate 2 adjacent port numbers to each
	 * cage, starting on cage 1.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 1
	 *	port 2 -> cage 2
	 *	port 3 -> cage 2
	 */
	{
		EFX_FAMILY_MEDFORD2,
		(1U << TLV_PORT_MODE_1x4_1x4) |			/* mode 3 */
		(1U << TLV_PORT_MODE_2x1_2x1) |			/* mode 4 */
		(1U << TLV_PORT_MODE_1x4_2x1) |			/* mode 6 */
		(1U << TLV_PORT_MODE_2x1_1x4) |			/* mode 7 */
		(1U << TLV_PORT_MODE_2x2_NA) |			/* mode 13 */
		(1U << TLV_PORT_MODE_2x1_1x2),			/* mode 18 */
		{ 0, 2, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford2 allocate 2 adjacent port numbers to each
	 * cage, starting on cage 2.
	 *	port 0 -> cage 2
	 *	port 1 -> cage 2
	 */
	{
		EFX_FAMILY_MEDFORD2,
		(1U << TLV_PORT_MODE_NA_2x2),			/* mode 14 */
		{ EFX_EXT_PORT_NA, 0, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford2 allocate 4 adjacent port numbers to each
	 * connector, starting on cage 1.
	 *	port 0 -> cage 1
	 *	port 1 -> cage 1
	 *	port 2 -> cage 1
	 *	port 3 -> cage 1
	 */
	{
		EFX_FAMILY_MEDFORD2,
		(1U << TLV_PORT_MODE_4x1_NA),			/* mode 5 */
		{ 0, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
	/*
	 * Modes that on Medford2 allocate 4 adjacent port numbers to each
	 * connector, starting on cage 2.
	 *	port 0 -> cage 2
	 *	port 1 -> cage 2
	 *	port 2 -> cage 2
	 *	port 3 -> cage 2
	 */
	{
		EFX_FAMILY_MEDFORD2,
		(1U << TLV_PORT_MODE_NA_4x1) |			/* mode 8 */
		(1U << TLV_PORT_MODE_NA_1x2),			/* mode 11 */
		{ EFX_EXT_PORT_NA, 0, EFX_EXT_PORT_NA, EFX_EXT_PORT_NA }
	},
};

static	__checkReturn	efx_rc_t
ef10_external_port_mapping(
	__in		efx_nic_t *enp,
	__in		uint32_t port,
	__out		uint8_t *external_portp)
{
	efx_rc_t rc;
	int i;
	uint32_t port_modes;
	uint32_t matches;
	uint32_t current;
	struct ef10_external_port_map_s *mapp = NULL;
	int ext_index = port; /* Default 1-1 mapping */

	if ((rc = efx_mcdi_get_port_modes(enp, &port_modes, &current,
		    NULL)) != 0) {
		/*
		 * No current port mode information (i.e. Huntington)
		 * - infer mapping from available modes
		 */
		if ((rc = efx_mcdi_get_port_modes(enp,
			    &port_modes, NULL, NULL)) != 0) {
			/*
			 * No port mode information available
			 * - use default mapping
			 */
			goto out;
		}
	} else {
		/* Only need to scan the current mode */
		port_modes = 1 << current;
	}

	/*
	 * Infer the internal port -> external number mapping from
	 * the possible port modes for this NIC.
	 */
	for (i = 0; i < EFX_ARRAY_SIZE(__ef10_external_port_mappings); ++i) {
		struct ef10_external_port_map_s *eepmp =
		    &__ef10_external_port_mappings[i];
		if (eepmp->family != enp->en_family)
			continue;
		matches = (eepmp->modes_mask & port_modes);
		if (matches != 0) {
			/*
			 * Some modes match. For some Huntington boards
			 * there will be multiple matches. The mapping on the
			 * last match is used.
			 */
			mapp = eepmp;
			port_modes &= ~matches;
		}
	}

	if (port_modes != 0) {
		/* Some advertised modes are not supported */
		rc = ENOTSUP;
		goto fail1;
	}

out:
	if (mapp != NULL) {
		/*
		 * External ports are assigned a sequence of consecutive
		 * port numbers, so find the one with the closest base_port.
		 */
		uint32_t delta = EFX_EXT_PORT_NA;

		for (i = 0; i < EFX_EXT_PORT_MAX; i++) {
			uint32_t base = mapp->base_port[i];
			if ((base != EFX_EXT_PORT_NA) && (base <= port)) {
				if ((port - base) < delta) {
					delta = (port - base);
					ext_index = i;
				}
			}
		}
	}
	*external_portp = (uint8_t)(ext_index + 1);

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
ef10_nic_board_cfg(
	__in		efx_nic_t *enp)
{
	const efx_nic_ops_t *enop = enp->en_enop;
	efx_mcdi_iface_t *emip = &(enp->en_mcdi.em_emip);
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	ef10_link_state_t els;
	efx_port_t *epp = &(enp->en_port);
	uint32_t board_type = 0;
	uint32_t base, nvec;
	uint32_t port;
	uint32_t mask;
	uint32_t pf;
	uint32_t vf;
	uint8_t mac_addr[6] = { 0 };
	efx_rc_t rc;

	/* Get the (zero-based) MCDI port number */
	if ((rc = efx_mcdi_get_port_assignment(enp, &port)) != 0)
		goto fail1;

	/* EFX MCDI interface uses one-based port numbers */
	emip->emi_port = port + 1;

	if ((rc = ef10_external_port_mapping(enp, port,
		    &encp->enc_external_port)) != 0)
		goto fail2;

	/*
	 * Get PCIe function number from firmware (used for
	 * per-function privilege and dynamic config info).
	 *  - PCIe PF: pf = PF number, vf = 0xffff.
	 *  - PCIe VF: pf = parent PF, vf = VF number.
	 */
	if ((rc = efx_mcdi_get_function_info(enp, &pf, &vf)) != 0)
		goto fail3;

	encp->enc_pf = pf;
	encp->enc_vf = vf;

	/* MAC address for this function */
	if (EFX_PCI_FUNCTION_IS_PF(encp)) {
		rc = efx_mcdi_get_mac_address_pf(enp, mac_addr);
#if EFSYS_OPT_ALLOW_UNCONFIGURED_NIC
		/*
		 * Disable static config checking, ONLY for manufacturing test
		 * and setup at the factory, to allow the static config to be
		 * installed.
		 */
#else /* EFSYS_OPT_ALLOW_UNCONFIGURED_NIC */
		if ((rc == 0) && (mac_addr[0] & 0x02)) {
			/*
			 * If the static config does not include a global MAC
			 * address pool then the board may return a locally
			 * administered MAC address (this should only happen on
			 * incorrectly programmed boards).
			 */
			rc = EINVAL;
		}
#endif /* EFSYS_OPT_ALLOW_UNCONFIGURED_NIC */
	} else {
		rc = efx_mcdi_get_mac_address_vf(enp, mac_addr);
	}
	if (rc != 0)
		goto fail4;

	EFX_MAC_ADDR_COPY(encp->enc_mac_addr, mac_addr);

	/* Board configuration (legacy) */
	rc = efx_mcdi_get_board_cfg(enp, &board_type, NULL, NULL);
	if (rc != 0) {
		/* Unprivileged functions may not be able to read board cfg */
		if (rc == EACCES)
			board_type = 0;
		else
			goto fail5;
	}

	encp->enc_board_type = board_type;
	encp->enc_clk_mult = 1; /* not used for EF10 */

	/* Fill out fields in enp->en_port and enp->en_nic_cfg from MCDI */
	if ((rc = efx_mcdi_get_phy_cfg(enp)) != 0)
		goto fail6;

	/*
	 * Firmware with support for *_FEC capability bits does not
	 * report that the corresponding *_FEC_REQUESTED bits are supported.
	 * Add them here so that drivers understand that they are supported.
	 */
	if (epp->ep_phy_cap_mask & (1u << EFX_PHY_CAP_BASER_FEC))
		epp->ep_phy_cap_mask |=
		    (1u << EFX_PHY_CAP_BASER_FEC_REQUESTED);
	if (epp->ep_phy_cap_mask & (1u << EFX_PHY_CAP_RS_FEC))
		epp->ep_phy_cap_mask |=
		    (1u << EFX_PHY_CAP_RS_FEC_REQUESTED);
	if (epp->ep_phy_cap_mask & (1u << EFX_PHY_CAP_25G_BASER_FEC))
		epp->ep_phy_cap_mask |=
		    (1u << EFX_PHY_CAP_25G_BASER_FEC_REQUESTED);

	/* Obtain the default PHY advertised capabilities */
	if ((rc = ef10_phy_get_link(enp, &els)) != 0)
		goto fail7;
	epp->ep_default_adv_cap_mask = els.epls.epls_adv_cap_mask;
	epp->ep_adv_cap_mask = els.epls.epls_adv_cap_mask;

	/* Check capabilities of running datapath firmware */
	if ((rc = ef10_get_datapath_caps(enp)) != 0)
		goto fail8;

	/* Alignment for WPTR updates */
	encp->enc_rx_push_align = EF10_RX_WPTR_ALIGN;

	encp->enc_tx_dma_desc_size_max = EFX_MASK32(ESF_DZ_RX_KER_BYTE_CNT);
	/* No boundary crossing limits */
	encp->enc_tx_dma_desc_boundary = 0;

	/*
	 * Maximum number of bytes into the frame the TCP header can start for
	 * firmware assisted TSO to work.
	 */
	encp->enc_tx_tso_tcp_header_offset_limit = EF10_TCP_HEADER_OFFSET_LIMIT;

	/*
	 * Set resource limits for MC_CMD_ALLOC_VIS. Note that we cannot use
	 * MC_CMD_GET_RESOURCE_LIMITS here as that reports the available
	 * resources (allocated to this PCIe function), which is zero until
	 * after we have allocated VIs.
	 */
	encp->enc_evq_limit = 1024;
	encp->enc_rxq_limit = EFX_RXQ_LIMIT_TARGET;
	encp->enc_txq_limit = EFX_TXQ_LIMIT_TARGET;

	encp->enc_buftbl_limit = 0xFFFFFFFF;

	/* Get interrupt vector limits */
	if ((rc = efx_mcdi_get_vector_cfg(enp, &base, &nvec, NULL)) != 0) {
		if (EFX_PCI_FUNCTION_IS_PF(encp))
			goto fail9;

		/* Ignore error (cannot query vector limits from a VF). */
		base = 0;
		nvec = 1024;
	}
	encp->enc_intr_vec_base = base;
	encp->enc_intr_limit = nvec;

	/*
	 * Get the current privilege mask. Note that this may be modified
	 * dynamically, so this value is informational only. DO NOT use
	 * the privilege mask to check for sufficient privileges, as that
	 * can result in time-of-check/time-of-use bugs.
	 */
	if ((rc = ef10_get_privilege_mask(enp, &mask)) != 0)
		goto fail10;
	encp->enc_privilege_mask = mask;

	/* Get remaining controller-specific board config */
	if ((rc = enop->eno_board_cfg(enp)) != 0)
		if (rc != EACCES)
			goto fail11;

	return (0);

fail11:
	EFSYS_PROBE(fail11);
fail10:
	EFSYS_PROBE(fail10);
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

	__checkReturn	efx_rc_t
ef10_nic_probe(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_drv_cfg_t *edcp = &(enp->en_drv_cfg);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	/* Read and clear any assertion state */
	if ((rc = efx_mcdi_read_assertion(enp)) != 0)
		goto fail1;

	/* Exit the assertion handler */
	if ((rc = efx_mcdi_exit_assertion_handler(enp)) != 0)
		if (rc != EACCES)
			goto fail2;

	if ((rc = efx_mcdi_drv_attach(enp, B_TRUE)) != 0)
		goto fail3;

	if ((rc = ef10_nic_board_cfg(enp)) != 0)
		goto fail4;

	/*
	 * Set default driver config limits (based on board config).
	 *
	 * FIXME: For now allocate a fixed number of VIs which is likely to be
	 * sufficient and small enough to allow multiple functions on the same
	 * port.
	 */
	edcp->edc_min_vi_count = edcp->edc_max_vi_count =
	    MIN(128, MAX(encp->enc_rxq_limit, encp->enc_txq_limit));

	/* The client driver must configure and enable PIO buffer support */
	edcp->edc_max_piobuf_count = 0;
	edcp->edc_pio_alloc_size = 0;

#if EFSYS_OPT_MAC_STATS
	/* Wipe the MAC statistics */
	if ((rc = efx_mcdi_mac_stats_clear(enp)) != 0)
		goto fail5;
#endif

#if EFSYS_OPT_LOOPBACK
	if ((rc = efx_mcdi_get_loopback_modes(enp)) != 0)
		goto fail6;
#endif

#if EFSYS_OPT_MON_STATS
	if ((rc = mcdi_mon_cfg_build(enp)) != 0) {
		/* Unprivileged functions do not have access to sensors */
		if (rc != EACCES)
			goto fail7;
	}
#endif

	encp->enc_features = enp->en_features;

	return (0);

#if EFSYS_OPT_MON_STATS
fail7:
	EFSYS_PROBE(fail7);
#endif
#if EFSYS_OPT_LOOPBACK
fail6:
	EFSYS_PROBE(fail6);
#endif
#if EFSYS_OPT_MAC_STATS
fail5:
	EFSYS_PROBE(fail5);
#endif
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

	__checkReturn	efx_rc_t
ef10_nic_set_drv_limits(
	__inout		efx_nic_t *enp,
	__in		efx_drv_limits_t *edlp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_drv_cfg_t *edcp = &(enp->en_drv_cfg);
	uint32_t min_evq_count, max_evq_count;
	uint32_t min_rxq_count, max_rxq_count;
	uint32_t min_txq_count, max_txq_count;
	efx_rc_t rc;

	if (edlp == NULL) {
		rc = EINVAL;
		goto fail1;
	}

	/* Get minimum required and maximum usable VI limits */
	min_evq_count = MIN(edlp->edl_min_evq_count, encp->enc_evq_limit);
	min_rxq_count = MIN(edlp->edl_min_rxq_count, encp->enc_rxq_limit);
	min_txq_count = MIN(edlp->edl_min_txq_count, encp->enc_txq_limit);

	edcp->edc_min_vi_count =
	    MAX(min_evq_count, MAX(min_rxq_count, min_txq_count));

	max_evq_count = MIN(edlp->edl_max_evq_count, encp->enc_evq_limit);
	max_rxq_count = MIN(edlp->edl_max_rxq_count, encp->enc_rxq_limit);
	max_txq_count = MIN(edlp->edl_max_txq_count, encp->enc_txq_limit);

	edcp->edc_max_vi_count =
	    MAX(max_evq_count, MAX(max_rxq_count, max_txq_count));

	/*
	 * Check limits for sub-allocated piobuf blocks.
	 * PIO is optional, so don't fail if the limits are incorrect.
	 */
	if ((encp->enc_piobuf_size == 0) ||
	    (encp->enc_piobuf_limit == 0) ||
	    (edlp->edl_min_pio_alloc_size == 0) ||
	    (edlp->edl_min_pio_alloc_size > encp->enc_piobuf_size)) {
		/* Disable PIO */
		edcp->edc_max_piobuf_count = 0;
		edcp->edc_pio_alloc_size = 0;
	} else {
		uint32_t blk_size, blk_count, blks_per_piobuf;

		blk_size =
		    MAX(edlp->edl_min_pio_alloc_size,
			    encp->enc_piobuf_min_alloc_size);

		blks_per_piobuf = encp->enc_piobuf_size / blk_size;
		EFSYS_ASSERT3U(blks_per_piobuf, <=, 32);

		blk_count = (encp->enc_piobuf_limit * blks_per_piobuf);

		/* A zero max pio alloc count means unlimited */
		if ((edlp->edl_max_pio_alloc_count > 0) &&
		    (edlp->edl_max_pio_alloc_count < blk_count)) {
			blk_count = edlp->edl_max_pio_alloc_count;
		}

		edcp->edc_pio_alloc_size = blk_size;
		edcp->edc_max_piobuf_count =
		    (blk_count + (blks_per_piobuf - 1)) / blks_per_piobuf;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


	__checkReturn	efx_rc_t
ef10_nic_reset(
	__in		efx_nic_t *enp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_ENTITY_RESET_IN_LEN,
		MC_CMD_ENTITY_RESET_OUT_LEN);
	efx_rc_t rc;

	/* ef10_nic_reset() is called to recover from BADASSERT failures. */
	if ((rc = efx_mcdi_read_assertion(enp)) != 0)
		goto fail1;
	if ((rc = efx_mcdi_exit_assertion_handler(enp)) != 0)
		goto fail2;

	req.emr_cmd = MC_CMD_ENTITY_RESET;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_ENTITY_RESET_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_ENTITY_RESET_OUT_LEN;

	MCDI_IN_POPULATE_DWORD_1(req, ENTITY_RESET_IN_FLAG,
	    ENTITY_RESET_IN_FUNCTION_RESOURCE_RESET, 1);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail3;
	}

	/* Clear RX/TX DMA queue errors */
	enp->en_reset_flags &= ~(EFX_RESET_RXQ_ERR | EFX_RESET_TXQ_ERR);

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_nic_init(
	__in		efx_nic_t *enp)
{
	efx_drv_cfg_t *edcp = &(enp->en_drv_cfg);
	uint32_t min_vi_count, max_vi_count;
	uint32_t vi_count, vi_base, vi_shift;
	uint32_t i;
	uint32_t retry;
	uint32_t delay_us;
	uint32_t vi_window_size;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	/* Enable reporting of some events (e.g. link change) */
	if ((rc = efx_mcdi_log_ctrl(enp)) != 0)
		goto fail1;

	/* Allocate (optional) on-chip PIO buffers */
	ef10_nic_alloc_piobufs(enp, edcp->edc_max_piobuf_count);

	/*
	 * For best performance, PIO writes should use a write-combined
	 * (WC) memory mapping. Using a separate WC mapping for the PIO
	 * aperture of each VI would be a burden to drivers (and not
	 * possible if the host page size is >4Kbyte).
	 *
	 * To avoid this we use a single uncached (UC) mapping for VI
	 * register access, and a single WC mapping for extra VIs used
	 * for PIO writes.
	 *
	 * Each piobuf must be linked to a VI in the WC mapping, and to
	 * each VI that is using a sub-allocated block from the piobuf.
	 */
	min_vi_count = edcp->edc_min_vi_count;
	max_vi_count =
	    edcp->edc_max_vi_count + enp->en_arch.ef10.ena_piobuf_count;

	/* Ensure that the previously attached driver's VIs are freed */
	if ((rc = efx_mcdi_free_vis(enp)) != 0)
		goto fail2;

	/*
	 * Reserve VI resources (EVQ+RXQ+TXQ) for this PCIe function. If this
	 * fails then retrying the request for fewer VI resources may succeed.
	 */
	vi_count = 0;
	if ((rc = efx_mcdi_alloc_vis(enp, min_vi_count, max_vi_count,
		    &vi_base, &vi_count, &vi_shift)) != 0)
		goto fail3;

	EFSYS_PROBE2(vi_alloc, uint32_t, vi_base, uint32_t, vi_count);

	if (vi_count < min_vi_count) {
		rc = ENOMEM;
		goto fail4;
	}

	enp->en_arch.ef10.ena_vi_base = vi_base;
	enp->en_arch.ef10.ena_vi_count = vi_count;
	enp->en_arch.ef10.ena_vi_shift = vi_shift;

	if (vi_count < min_vi_count + enp->en_arch.ef10.ena_piobuf_count) {
		/* Not enough extra VIs to map piobufs */
		ef10_nic_free_piobufs(enp);
	}

	enp->en_arch.ef10.ena_pio_write_vi_base =
	    vi_count - enp->en_arch.ef10.ena_piobuf_count;

	EFSYS_ASSERT3U(enp->en_nic_cfg.enc_vi_window_shift, !=,
	    EFX_VI_WINDOW_SHIFT_INVALID);
	EFSYS_ASSERT3U(enp->en_nic_cfg.enc_vi_window_shift, <=,
	    EFX_VI_WINDOW_SHIFT_64K);
	vi_window_size = 1U << enp->en_nic_cfg.enc_vi_window_shift;

	/* Save UC memory mapping details */
	enp->en_arch.ef10.ena_uc_mem_map_offset = 0;
	if (enp->en_arch.ef10.ena_piobuf_count > 0) {
		enp->en_arch.ef10.ena_uc_mem_map_size =
		    (vi_window_size *
		    enp->en_arch.ef10.ena_pio_write_vi_base);
	} else {
		enp->en_arch.ef10.ena_uc_mem_map_size =
		    (vi_window_size *
		    enp->en_arch.ef10.ena_vi_count);
	}

	/* Save WC memory mapping details */
	enp->en_arch.ef10.ena_wc_mem_map_offset =
	    enp->en_arch.ef10.ena_uc_mem_map_offset +
	    enp->en_arch.ef10.ena_uc_mem_map_size;

	enp->en_arch.ef10.ena_wc_mem_map_size =
	    (vi_window_size *
	    enp->en_arch.ef10.ena_piobuf_count);

	/* Link piobufs to extra VIs in WC mapping */
	if (enp->en_arch.ef10.ena_piobuf_count > 0) {
		for (i = 0; i < enp->en_arch.ef10.ena_piobuf_count; i++) {
			rc = efx_mcdi_link_piobuf(enp,
			    enp->en_arch.ef10.ena_pio_write_vi_base + i,
			    enp->en_arch.ef10.ena_piobuf_handle[i]);
			if (rc != 0)
				break;
		}
	}

	/*
	 * Allocate a vAdaptor attached to our upstream vPort/pPort.
	 *
	 * On a VF, this may fail with MC_CMD_ERR_NO_EVB_PORT (ENOENT) if the PF
	 * driver has yet to bring up the EVB port. See bug 56147. In this case,
	 * retry the request several times after waiting a while. The wait time
	 * between retries starts small (10ms) and exponentially increases.
	 * Total wait time is a little over two seconds. Retry logic in the
	 * client driver may mean this whole loop is repeated if it continues to
	 * fail.
	 */
	retry = 0;
	delay_us = 10000;
	while ((rc = efx_mcdi_vadaptor_alloc(enp, EVB_PORT_ID_ASSIGNED)) != 0) {
		if (EFX_PCI_FUNCTION_IS_PF(&enp->en_nic_cfg) ||
		    (rc != ENOENT)) {
			/*
			 * Do not retry alloc for PF, or for other errors on
			 * a VF.
			 */
			goto fail5;
		}

		/* VF startup before PF is ready. Retry allocation. */
		if (retry > 5) {
			/* Too many attempts */
			rc = EINVAL;
			goto fail6;
		}
		EFSYS_PROBE1(mcdi_no_evb_port_retry, int, retry);
		EFSYS_SLEEP(delay_us);
		retry++;
		if (delay_us < 500000)
			delay_us <<= 2;
	}

	enp->en_vport_id = EVB_PORT_ID_ASSIGNED;
	enp->en_nic_cfg.enc_mcdi_max_payload_length = MCDI_CTL_SDU_LEN_MAX_V2;

	return (0);

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

	ef10_nic_free_piobufs(enp);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_nic_get_vi_pool(
	__in		efx_nic_t *enp,
	__out		uint32_t *vi_countp)
{
	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	/*
	 * Report VIs that the client driver can use.
	 * Do not include VIs used for PIO buffer writes.
	 */
	*vi_countp = enp->en_arch.ef10.ena_pio_write_vi_base;

	return (0);
}

	__checkReturn	efx_rc_t
ef10_nic_get_bar_region(
	__in		efx_nic_t *enp,
	__in		efx_nic_region_t region,
	__out		uint32_t *offsetp,
	__out		size_t *sizep)
{
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	/*
	 * TODO: Specify host memory mapping alignment and granularity
	 * in efx_drv_limits_t so that they can be taken into account
	 * when allocating extra VIs for PIO writes.
	 */
	switch (region) {
	case EFX_REGION_VI:
		/* UC mapped memory BAR region for VI registers */
		*offsetp = enp->en_arch.ef10.ena_uc_mem_map_offset;
		*sizep = enp->en_arch.ef10.ena_uc_mem_map_size;
		break;

	case EFX_REGION_PIO_WRITE_VI:
		/* WC mapped memory BAR region for piobuf writes */
		*offsetp = enp->en_arch.ef10.ena_wc_mem_map_offset;
		*sizep = enp->en_arch.ef10.ena_wc_mem_map_size;
		break;

	default:
		rc = EINVAL;
		goto fail1;
	}

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	boolean_t
ef10_nic_hw_unavailable(
	__in		efx_nic_t *enp)
{
	efx_dword_t dword;

	if (enp->en_reset_flags & EFX_RESET_HW_UNAVAIL)
		return (B_TRUE);

	EFX_BAR_READD(enp, ER_DZ_BIU_MC_SFT_STATUS_REG, &dword, B_FALSE);
	if (EFX_DWORD_FIELD(dword, EFX_DWORD_0) == 0xffffffff)
		goto unavail;

	return (B_FALSE);

unavail:
	ef10_nic_set_hw_unavailable(enp);

	return (B_TRUE);
}

			void
ef10_nic_set_hw_unavailable(
	__in		efx_nic_t *enp)
{
	EFSYS_PROBE(hw_unavail);
	enp->en_reset_flags |= EFX_RESET_HW_UNAVAIL;
}


			void
ef10_nic_fini(
	__in		efx_nic_t *enp)
{
	uint32_t i;
	efx_rc_t rc;

	(void) efx_mcdi_vadaptor_free(enp, enp->en_vport_id);
	enp->en_vport_id = 0;

	/* Unlink piobufs from extra VIs in WC mapping */
	if (enp->en_arch.ef10.ena_piobuf_count > 0) {
		for (i = 0; i < enp->en_arch.ef10.ena_piobuf_count; i++) {
			rc = efx_mcdi_unlink_piobuf(enp,
			    enp->en_arch.ef10.ena_pio_write_vi_base + i);
			if (rc != 0)
				break;
		}
	}

	ef10_nic_free_piobufs(enp);

	(void) efx_mcdi_free_vis(enp);
	enp->en_arch.ef10.ena_vi_count = 0;
}

			void
ef10_nic_unprobe(
	__in		efx_nic_t *enp)
{
#if EFSYS_OPT_MON_STATS
	mcdi_mon_cfg_free(enp);
#endif /* EFSYS_OPT_MON_STATS */
	(void) efx_mcdi_drv_attach(enp, B_FALSE);
}

#if EFSYS_OPT_DIAG

	__checkReturn	efx_rc_t
ef10_nic_register_test(
	__in		efx_nic_t *enp)
{
	efx_rc_t rc;

	/* FIXME */
	_NOTE(ARGUNUSED(enp))
	_NOTE(CONSTANTCONDITION)
	if (B_FALSE) {
		rc = ENOTSUP;
		goto fail1;
	}
	/* FIXME */

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

#endif	/* EFSYS_OPT_DIAG */

#if EFSYS_OPT_FW_SUBVARIANT_AWARE

	__checkReturn	efx_rc_t
efx_mcdi_get_nic_global(
	__in		efx_nic_t *enp,
	__in		uint32_t key,
	__out		uint32_t *valuep)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_NIC_GLOBAL_IN_LEN,
		MC_CMD_GET_NIC_GLOBAL_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_GET_NIC_GLOBAL;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_NIC_GLOBAL_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_NIC_GLOBAL_OUT_LEN;

	MCDI_IN_SET_DWORD(req, GET_NIC_GLOBAL_IN_KEY, key);

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used != MC_CMD_GET_NIC_GLOBAL_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	*valuep = MCDI_OUT_DWORD(req, GET_NIC_GLOBAL_OUT_VALUE);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
efx_mcdi_set_nic_global(
	__in		efx_nic_t *enp,
	__in		uint32_t key,
	__in		uint32_t value)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SET_NIC_GLOBAL_IN_LEN, 0);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_SET_NIC_GLOBAL;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_NIC_GLOBAL_IN_LEN;
	req.emr_out_buf = NULL;
	req.emr_out_length = 0;

	MCDI_IN_SET_DWORD(req, SET_NIC_GLOBAL_IN_KEY, key);
	MCDI_IN_SET_DWORD(req, SET_NIC_GLOBAL_IN_VALUE, value);

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

#endif	/* EFSYS_OPT_FW_SUBVARIANT_AWARE */

#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
