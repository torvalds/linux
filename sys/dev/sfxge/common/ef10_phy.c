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

static			void
mcdi_phy_decode_cap(
	__in		uint32_t mcdi_cap,
	__out		uint32_t *maskp)
{
	uint32_t mask;

#define	CHECK_CAP(_cap) \
	EFX_STATIC_ASSERT(EFX_PHY_CAP_##_cap == MC_CMD_PHY_CAP_##_cap##_LBN)

	CHECK_CAP(10HDX);
	CHECK_CAP(10FDX);
	CHECK_CAP(100HDX);
	CHECK_CAP(100FDX);
	CHECK_CAP(1000HDX);
	CHECK_CAP(1000FDX);
	CHECK_CAP(10000FDX);
	CHECK_CAP(25000FDX);
	CHECK_CAP(40000FDX);
	CHECK_CAP(50000FDX);
	CHECK_CAP(100000FDX);
	CHECK_CAP(PAUSE);
	CHECK_CAP(ASYM);
	CHECK_CAP(AN);
	CHECK_CAP(DDM);
	CHECK_CAP(BASER_FEC);
	CHECK_CAP(BASER_FEC_REQUESTED);
	CHECK_CAP(RS_FEC);
	CHECK_CAP(RS_FEC_REQUESTED);
	CHECK_CAP(25G_BASER_FEC);
	CHECK_CAP(25G_BASER_FEC_REQUESTED);
#undef CHECK_CAP

	mask = 0;
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_10HDX_LBN))
		mask |= (1 << EFX_PHY_CAP_10HDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_10FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_10FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_100HDX_LBN))
		mask |= (1 << EFX_PHY_CAP_100HDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_100FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_100FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_1000HDX_LBN))
		mask |= (1 << EFX_PHY_CAP_1000HDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_1000FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_1000FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_10000FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_10000FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_25000FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_25000FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_40000FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_40000FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_50000FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_50000FDX);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_100000FDX_LBN))
		mask |= (1 << EFX_PHY_CAP_100000FDX);

	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_PAUSE_LBN))
		mask |= (1 << EFX_PHY_CAP_PAUSE);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_ASYM_LBN))
		mask |= (1 << EFX_PHY_CAP_ASYM);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_AN_LBN))
		mask |= (1 << EFX_PHY_CAP_AN);

	/* FEC caps (supported on Medford2 and later) */
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_BASER_FEC_LBN))
		mask |= (1 << EFX_PHY_CAP_BASER_FEC);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_BASER_FEC_REQUESTED_LBN))
		mask |= (1 << EFX_PHY_CAP_BASER_FEC_REQUESTED);

	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_RS_FEC_LBN))
		mask |= (1 << EFX_PHY_CAP_RS_FEC);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_RS_FEC_REQUESTED_LBN))
		mask |= (1 << EFX_PHY_CAP_RS_FEC_REQUESTED);

	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_LBN))
		mask |= (1 << EFX_PHY_CAP_25G_BASER_FEC);
	if (mcdi_cap & (1 << MC_CMD_PHY_CAP_25G_BASER_FEC_REQUESTED_LBN))
		mask |= (1 << EFX_PHY_CAP_25G_BASER_FEC_REQUESTED);

	*maskp = mask;
}

static			void
mcdi_phy_decode_link_mode(
	__in		efx_nic_t *enp,
	__in		uint32_t link_flags,
	__in		unsigned int speed,
	__in		unsigned int fcntl,
	__in		uint32_t fec,
	__out		efx_link_mode_t *link_modep,
	__out		unsigned int *fcntlp,
	__out		efx_phy_fec_type_t *fecp)
{
	boolean_t fd = !!(link_flags &
		    (1 << MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN));
	boolean_t up = !!(link_flags &
		    (1 << MC_CMD_GET_LINK_OUT_LINK_UP_LBN));

	_NOTE(ARGUNUSED(enp))

	if (!up)
		*link_modep = EFX_LINK_DOWN;
	else if (speed == 100000 && fd)
		*link_modep = EFX_LINK_100000FDX;
	else if (speed == 50000 && fd)
		*link_modep = EFX_LINK_50000FDX;
	else if (speed == 40000 && fd)
		*link_modep = EFX_LINK_40000FDX;
	else if (speed == 25000 && fd)
		*link_modep = EFX_LINK_25000FDX;
	else if (speed == 10000 && fd)
		*link_modep = EFX_LINK_10000FDX;
	else if (speed == 1000)
		*link_modep = fd ? EFX_LINK_1000FDX : EFX_LINK_1000HDX;
	else if (speed == 100)
		*link_modep = fd ? EFX_LINK_100FDX : EFX_LINK_100HDX;
	else if (speed == 10)
		*link_modep = fd ? EFX_LINK_10FDX : EFX_LINK_10HDX;
	else
		*link_modep = EFX_LINK_UNKNOWN;

	if (fcntl == MC_CMD_FCNTL_OFF)
		*fcntlp = 0;
	else if (fcntl == MC_CMD_FCNTL_RESPOND)
		*fcntlp = EFX_FCNTL_RESPOND;
	else if (fcntl == MC_CMD_FCNTL_GENERATE)
		*fcntlp = EFX_FCNTL_GENERATE;
	else if (fcntl == MC_CMD_FCNTL_BIDIR)
		*fcntlp = EFX_FCNTL_RESPOND | EFX_FCNTL_GENERATE;
	else {
		EFSYS_PROBE1(mc_pcol_error, int, fcntl);
		*fcntlp = 0;
	}

	switch (fec) {
	case MC_CMD_FEC_NONE:
		*fecp = EFX_PHY_FEC_NONE;
		break;
	case MC_CMD_FEC_BASER:
		*fecp = EFX_PHY_FEC_BASER;
		break;
	case MC_CMD_FEC_RS:
		*fecp = EFX_PHY_FEC_RS;
		break;
	default:
		EFSYS_PROBE1(mc_pcol_error, int, fec);
		*fecp = EFX_PHY_FEC_NONE;
		break;
	}
}


			void
ef10_phy_link_ev(
	__in		efx_nic_t *enp,
	__in		efx_qword_t *eqp,
	__out		efx_link_mode_t *link_modep)
{
	efx_port_t *epp = &(enp->en_port);
	unsigned int link_flags;
	unsigned int speed;
	unsigned int fcntl;
	efx_phy_fec_type_t fec = MC_CMD_FEC_NONE;
	efx_link_mode_t link_mode;
	uint32_t lp_cap_mask;

	/*
	 * Convert the LINKCHANGE speed enumeration into mbit/s, in the
	 * same way as GET_LINK encodes the speed
	 */
	switch (MCDI_EV_FIELD(eqp, LINKCHANGE_SPEED)) {
	case MCDI_EVENT_LINKCHANGE_SPEED_100M:
		speed = 100;
		break;
	case MCDI_EVENT_LINKCHANGE_SPEED_1G:
		speed = 1000;
		break;
	case MCDI_EVENT_LINKCHANGE_SPEED_10G:
		speed = 10000;
		break;
	case MCDI_EVENT_LINKCHANGE_SPEED_25G:
		speed = 25000;
		break;
	case MCDI_EVENT_LINKCHANGE_SPEED_40G:
		speed = 40000;
		break;
	case MCDI_EVENT_LINKCHANGE_SPEED_50G:
		speed = 50000;
		break;
	case MCDI_EVENT_LINKCHANGE_SPEED_100G:
		speed = 100000;
		break;
	default:
		speed = 0;
		break;
	}

	link_flags = MCDI_EV_FIELD(eqp, LINKCHANGE_LINK_FLAGS);
	mcdi_phy_decode_link_mode(enp, link_flags, speed,
				    MCDI_EV_FIELD(eqp, LINKCHANGE_FCNTL),
				    MC_CMD_FEC_NONE, &link_mode,
				    &fcntl, &fec);
	mcdi_phy_decode_cap(MCDI_EV_FIELD(eqp, LINKCHANGE_LP_CAP),
			    &lp_cap_mask);

	/*
	 * It's safe to update ep_lp_cap_mask without the driver's port lock
	 * because presumably any concurrently running efx_port_poll() is
	 * only going to arrive at the same value.
	 *
	 * ep_fcntl has two meanings. It's either the link common fcntl
	 * (if the PHY supports AN), or it's the forced link state. If
	 * the former, it's safe to update the value for the same reason as
	 * for ep_lp_cap_mask. If the latter, then just ignore the value,
	 * because we can race with efx_mac_fcntl_set().
	 */
	epp->ep_lp_cap_mask = lp_cap_mask;
	epp->ep_fcntl = fcntl;

	*link_modep = link_mode;
}

	__checkReturn	efx_rc_t
ef10_phy_power(
	__in		efx_nic_t *enp,
	__in		boolean_t power)
{
	efx_rc_t rc;

	if (!power)
		return (0);

	/* Check if the PHY is a zombie */
	if ((rc = ef10_phy_verify(enp)) != 0)
		goto fail1;

	enp->en_reset_flags |= EFX_RESET_PHY;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_phy_get_link(
	__in		efx_nic_t *enp,
	__out		ef10_link_state_t *elsp)
{
	efx_mcdi_req_t req;
	uint32_t fec;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_LINK_IN_LEN,
		MC_CMD_GET_LINK_OUT_V2_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_GET_LINK;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_LINK_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_LINK_OUT_V2_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_LINK_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	mcdi_phy_decode_cap(MCDI_OUT_DWORD(req, GET_LINK_OUT_CAP),
			    &elsp->epls.epls_adv_cap_mask);
	mcdi_phy_decode_cap(MCDI_OUT_DWORD(req, GET_LINK_OUT_LP_CAP),
			    &elsp->epls.epls_lp_cap_mask);

	if (req.emr_out_length_used < MC_CMD_GET_LINK_OUT_V2_LEN)
		fec = MC_CMD_FEC_NONE;
	else
		fec = MCDI_OUT_DWORD(req, GET_LINK_OUT_V2_FEC_TYPE);

	mcdi_phy_decode_link_mode(enp, MCDI_OUT_DWORD(req, GET_LINK_OUT_FLAGS),
			    MCDI_OUT_DWORD(req, GET_LINK_OUT_LINK_SPEED),
			    MCDI_OUT_DWORD(req, GET_LINK_OUT_FCNTL),
			    fec, &elsp->epls.epls_link_mode,
			    &elsp->epls.epls_fcntl, &elsp->epls.epls_fec);

	if (req.emr_out_length_used < MC_CMD_GET_LINK_OUT_V2_LEN) {
		elsp->epls.epls_ld_cap_mask = 0;
	} else {
		mcdi_phy_decode_cap(MCDI_OUT_DWORD(req, GET_LINK_OUT_V2_LD_CAP),
				    &elsp->epls.epls_ld_cap_mask);
	}


#if EFSYS_OPT_LOOPBACK
	/*
	 * MC_CMD_LOOPBACK and EFX_LOOPBACK names are equivalent, so use the
	 * MCDI value directly. Agreement is checked in efx_loopback_mask().
	 */
	elsp->els_loopback = MCDI_OUT_DWORD(req, GET_LINK_OUT_LOOPBACK_MODE);
#endif	/* EFSYS_OPT_LOOPBACK */

	elsp->els_mac_up = MCDI_OUT_DWORD(req, GET_LINK_OUT_MAC_FAULT) == 0;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_phy_reconfigure(
	__in		efx_nic_t *enp)
{
	efx_port_t *epp = &(enp->en_port);
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_SET_LINK_IN_LEN,
		MC_CMD_SET_LINK_OUT_LEN);
	uint32_t cap_mask;
#if EFSYS_OPT_PHY_LED_CONTROL
	unsigned int led_mode;
#endif
	unsigned int speed;
	boolean_t supported;
	efx_rc_t rc;

	if ((rc = efx_mcdi_link_control_supported(enp, &supported)) != 0)
		goto fail1;
	if (supported == B_FALSE)
		goto out;

	req.emr_cmd = MC_CMD_SET_LINK;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_LINK_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_LINK_OUT_LEN;

	cap_mask = epp->ep_adv_cap_mask;
	MCDI_IN_POPULATE_DWORD_10(req, SET_LINK_IN_CAP,
		PHY_CAP_10HDX, (cap_mask >> EFX_PHY_CAP_10HDX) & 0x1,
		PHY_CAP_10FDX, (cap_mask >> EFX_PHY_CAP_10FDX) & 0x1,
		PHY_CAP_100HDX, (cap_mask >> EFX_PHY_CAP_100HDX) & 0x1,
		PHY_CAP_100FDX, (cap_mask >> EFX_PHY_CAP_100FDX) & 0x1,
		PHY_CAP_1000HDX, (cap_mask >> EFX_PHY_CAP_1000HDX) & 0x1,
		PHY_CAP_1000FDX, (cap_mask >> EFX_PHY_CAP_1000FDX) & 0x1,
		PHY_CAP_10000FDX, (cap_mask >> EFX_PHY_CAP_10000FDX) & 0x1,
		PHY_CAP_PAUSE, (cap_mask >> EFX_PHY_CAP_PAUSE) & 0x1,
		PHY_CAP_ASYM, (cap_mask >> EFX_PHY_CAP_ASYM) & 0x1,
		PHY_CAP_AN, (cap_mask >> EFX_PHY_CAP_AN) & 0x1);
	/* Too many fields for for POPULATE macros, so insert this afterwards */
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_25000FDX, (cap_mask >> EFX_PHY_CAP_25000FDX) & 0x1);
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_40000FDX, (cap_mask >> EFX_PHY_CAP_40000FDX) & 0x1);
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_50000FDX, (cap_mask >> EFX_PHY_CAP_50000FDX) & 0x1);
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_100000FDX, (cap_mask >> EFX_PHY_CAP_100000FDX) & 0x1);

	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_BASER_FEC, (cap_mask >> EFX_PHY_CAP_BASER_FEC) & 0x1);
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_BASER_FEC_REQUESTED,
	    (cap_mask >> EFX_PHY_CAP_BASER_FEC_REQUESTED) & 0x1);

	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_RS_FEC, (cap_mask >> EFX_PHY_CAP_RS_FEC) & 0x1);
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_RS_FEC_REQUESTED,
	    (cap_mask >> EFX_PHY_CAP_RS_FEC_REQUESTED) & 0x1);

	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_25G_BASER_FEC,
	    (cap_mask >> EFX_PHY_CAP_25G_BASER_FEC) & 0x1);
	MCDI_IN_SET_DWORD_FIELD(req, SET_LINK_IN_CAP,
	    PHY_CAP_25G_BASER_FEC_REQUESTED,
	    (cap_mask >> EFX_PHY_CAP_25G_BASER_FEC_REQUESTED) & 0x1);

#if EFSYS_OPT_LOOPBACK
	MCDI_IN_SET_DWORD(req, SET_LINK_IN_LOOPBACK_MODE,
		    epp->ep_loopback_type);
	switch (epp->ep_loopback_link_mode) {
	case EFX_LINK_100FDX:
		speed = 100;
		break;
	case EFX_LINK_1000FDX:
		speed = 1000;
		break;
	case EFX_LINK_10000FDX:
		speed = 10000;
		break;
	case EFX_LINK_25000FDX:
		speed = 25000;
		break;
	case EFX_LINK_40000FDX:
		speed = 40000;
		break;
	case EFX_LINK_50000FDX:
		speed = 50000;
		break;
	case EFX_LINK_100000FDX:
		speed = 100000;
		break;
	default:
		speed = 0;
	}
#else
	MCDI_IN_SET_DWORD(req, SET_LINK_IN_LOOPBACK_MODE, MC_CMD_LOOPBACK_NONE);
	speed = 0;
#endif	/* EFSYS_OPT_LOOPBACK */
	MCDI_IN_SET_DWORD(req, SET_LINK_IN_LOOPBACK_SPEED, speed);

#if EFSYS_OPT_PHY_FLAGS
	MCDI_IN_SET_DWORD(req, SET_LINK_IN_FLAGS, epp->ep_phy_flags);
#else
	MCDI_IN_SET_DWORD(req, SET_LINK_IN_FLAGS, 0);
#endif	/* EFSYS_OPT_PHY_FLAGS */

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail2;
	}

	/* And set the blink mode */
	(void) memset(payload, 0, sizeof (payload));
	req.emr_cmd = MC_CMD_SET_ID_LED;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_SET_ID_LED_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_SET_ID_LED_OUT_LEN;

#if EFSYS_OPT_PHY_LED_CONTROL
	switch (epp->ep_phy_led_mode) {
	case EFX_PHY_LED_DEFAULT:
		led_mode = MC_CMD_LED_DEFAULT;
		break;
	case EFX_PHY_LED_OFF:
		led_mode = MC_CMD_LED_OFF;
		break;
	case EFX_PHY_LED_ON:
		led_mode = MC_CMD_LED_ON;
		break;
	default:
		EFSYS_ASSERT(0);
		led_mode = MC_CMD_LED_DEFAULT;
	}

	MCDI_IN_SET_DWORD(req, SET_ID_LED_IN_STATE, led_mode);
#else
	MCDI_IN_SET_DWORD(req, SET_ID_LED_IN_STATE, MC_CMD_LED_DEFAULT);
#endif	/* EFSYS_OPT_PHY_LED_CONTROL */

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail3;
	}
out:
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
ef10_phy_verify(
	__in		efx_nic_t *enp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_GET_PHY_STATE_IN_LEN,
		MC_CMD_GET_PHY_STATE_OUT_LEN);
	uint32_t state;
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_GET_PHY_STATE;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_GET_PHY_STATE_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_GET_PHY_STATE_OUT_LEN;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_GET_PHY_STATE_OUT_LEN) {
		rc = EMSGSIZE;
		goto fail2;
	}

	state = MCDI_OUT_DWORD(req, GET_PHY_STATE_OUT_STATE);
	if (state != MC_CMD_PHY_STATE_OK) {
		if (state != MC_CMD_PHY_STATE_ZOMBIE)
			EFSYS_PROBE1(mc_pcol_error, int, state);
		rc = ENOTACTIVE;
		goto fail3;
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
ef10_phy_oui_get(
	__in		efx_nic_t *enp,
	__out		uint32_t *ouip)
{
	_NOTE(ARGUNUSED(enp, ouip))

	return (ENOTSUP);
}

	__checkReturn	efx_rc_t
ef10_phy_link_state_get(
	__in		efx_nic_t *enp,
	__out		efx_phy_link_state_t  *eplsp)
{
	efx_rc_t rc;
	ef10_link_state_t els;

	/* Obtain the active link state */
	if ((rc = ef10_phy_get_link(enp, &els)) != 0)
		goto fail1;

	*eplsp = els.epls;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}


#if EFSYS_OPT_PHY_STATS

	__checkReturn				efx_rc_t
ef10_phy_stats_update(
	__in					efx_nic_t *enp,
	__in					efsys_mem_t *esmp,
	__inout_ecount(EFX_PHY_NSTATS)		uint32_t *stat)
{
	/* TBD: no stats support in firmware yet */
	_NOTE(ARGUNUSED(enp, esmp))
	memset(stat, 0, EFX_PHY_NSTATS * sizeof (*stat));

	return (0);
}

#endif	/* EFSYS_OPT_PHY_STATS */

#if EFSYS_OPT_BIST

	__checkReturn		efx_rc_t
ef10_bist_enable_offline(
	__in			efx_nic_t *enp)
{
	efx_rc_t rc;

	if ((rc = efx_mcdi_bist_enable_offline(enp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_bist_start(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type)
{
	efx_rc_t rc;

	if ((rc = efx_mcdi_bist_start(enp, type)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_bist_poll(
	__in			efx_nic_t *enp,
	__in			efx_bist_type_t type,
	__out			efx_bist_result_t *resultp,
	__out_opt __drv_when(count > 0, __notnull)
	uint32_t *value_maskp,
	__out_ecount_opt(count)	__drv_when(count > 0, __notnull)
	unsigned long *valuesp,
	__in			size_t count)
{
	/*
	 * MCDI_CTL_SDU_LEN_MAX_V1 is large enough cover all BIST results,
	 * whilst not wasting stack.
	 */
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_POLL_BIST_IN_LEN,
		MCDI_CTL_SDU_LEN_MAX_V1);
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_mcdi_req_t req;
	uint32_t value_mask = 0;
	uint32_t result;
	efx_rc_t rc;

	EFX_STATIC_ASSERT(MC_CMD_POLL_BIST_OUT_LEN <=
	    MCDI_CTL_SDU_LEN_MAX_V1);
	EFX_STATIC_ASSERT(MC_CMD_POLL_BIST_OUT_SFT9001_LEN <=
	    MCDI_CTL_SDU_LEN_MAX_V1);
	EFX_STATIC_ASSERT(MC_CMD_POLL_BIST_OUT_MRSFP_LEN <=
	    MCDI_CTL_SDU_LEN_MAX_V1);
	EFX_STATIC_ASSERT(MC_CMD_POLL_BIST_OUT_MEM_LEN <=
	    MCDI_CTL_SDU_LEN_MAX_V1);

	_NOTE(ARGUNUSED(type))

	req.emr_cmd = MC_CMD_POLL_BIST;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_POLL_BIST_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MCDI_CTL_SDU_LEN_MAX_V1;

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	if (req.emr_out_length_used < MC_CMD_POLL_BIST_OUT_RESULT_OFST + 4) {
		rc = EMSGSIZE;
		goto fail2;
	}

	if (count > 0)
		(void) memset(valuesp, '\0', count * sizeof (unsigned long));

	result = MCDI_OUT_DWORD(req, POLL_BIST_OUT_RESULT);

	if (result == MC_CMD_POLL_BIST_FAILED &&
	    req.emr_out_length >= MC_CMD_POLL_BIST_OUT_MEM_LEN &&
	    count > EFX_BIST_MEM_ECC_FATAL) {
		if (valuesp != NULL) {
			valuesp[EFX_BIST_MEM_TEST] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_TEST);
			valuesp[EFX_BIST_MEM_ADDR] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_ADDR);
			valuesp[EFX_BIST_MEM_BUS] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_BUS);
			valuesp[EFX_BIST_MEM_EXPECT] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_EXPECT);
			valuesp[EFX_BIST_MEM_ACTUAL] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_ACTUAL);
			valuesp[EFX_BIST_MEM_ECC] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_ECC);
			valuesp[EFX_BIST_MEM_ECC_PARITY] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_ECC_PARITY);
			valuesp[EFX_BIST_MEM_ECC_FATAL] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MEM_ECC_FATAL);
		}
		value_mask |= (1 << EFX_BIST_MEM_TEST) |
		    (1 << EFX_BIST_MEM_ADDR) |
		    (1 << EFX_BIST_MEM_BUS) |
		    (1 << EFX_BIST_MEM_EXPECT) |
		    (1 << EFX_BIST_MEM_ACTUAL) |
		    (1 << EFX_BIST_MEM_ECC) |
		    (1 << EFX_BIST_MEM_ECC_PARITY) |
		    (1 << EFX_BIST_MEM_ECC_FATAL);
	} else if (result == MC_CMD_POLL_BIST_FAILED &&
	    encp->enc_phy_type == EFX_PHY_XFI_FARMI &&
	    req.emr_out_length >= MC_CMD_POLL_BIST_OUT_MRSFP_LEN &&
	    count > EFX_BIST_FAULT_CODE) {
		if (valuesp != NULL)
			valuesp[EFX_BIST_FAULT_CODE] =
			    MCDI_OUT_DWORD(req, POLL_BIST_OUT_MRSFP_TEST);
		value_mask |= 1 << EFX_BIST_FAULT_CODE;
	}

	if (value_maskp != NULL)
		*value_maskp = value_mask;

	EFSYS_ASSERT(resultp != NULL);
	if (result == MC_CMD_POLL_BIST_RUNNING)
		*resultp = EFX_BIST_RESULT_RUNNING;
	else if (result == MC_CMD_POLL_BIST_PASSED)
		*resultp = EFX_BIST_RESULT_PASSED;
	else
		*resultp = EFX_BIST_RESULT_FAILED;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
ef10_bist_stop(
	__in		efx_nic_t *enp,
	__in		efx_bist_type_t type)
{
	/* There is no way to stop BIST on EF10. */
	_NOTE(ARGUNUSED(enp, type))
}

#endif	/* EFSYS_OPT_BIST */

#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
