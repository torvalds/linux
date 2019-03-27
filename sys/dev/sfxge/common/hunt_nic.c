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

#if EFSYS_OPT_HUNTINGTON

#include "ef10_tlv_layout.h"

static	__checkReturn	efx_rc_t
hunt_nic_get_required_pcie_bandwidth(
	__in		efx_nic_t *enp,
	__out		uint32_t *bandwidth_mbpsp)
{
	uint32_t port_modes;
	uint32_t bandwidth;
	efx_rc_t rc;

	/*
	 * On Huntington, the firmware may not give us the current port mode, so
	 * we need to go by the set of available port modes and assume the most
	 * capable mode is in use.
	 */

	if ((rc = efx_mcdi_get_port_modes(enp, &port_modes,
		    NULL, NULL)) != 0) {
		/* No port mode info available */
		bandwidth = 0;
		goto out;
	}

	if (port_modes & (1U << TLV_PORT_MODE_40G_40G)) {
		/*
		 * This needs the full PCIe bandwidth (and could use
		 * more) - roughly 64 Gbit/s for 8 lanes of Gen3.
		 */
		if ((rc = efx_nic_calculate_pcie_link_bandwidth(8,
			    EFX_PCIE_LINK_SPEED_GEN3, &bandwidth)) != 0)
			goto fail1;
	} else {
		if (port_modes & (1U << TLV_PORT_MODE_40G)) {
			bandwidth = 40000;
		} else if (port_modes & (1U << TLV_PORT_MODE_10G_10G_10G_10G)) {
			bandwidth = 4 * 10000;
		} else {
			/* Assume two 10G ports */
			bandwidth = 2 * 10000;
		}
	}

out:
	*bandwidth_mbpsp = bandwidth;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
hunt_board_cfg(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_port_t *epp = &(enp->en_port);
	uint32_t flags;
	uint32_t sysclk, dpcpu_clk;
	uint32_t bandwidth;
	efx_rc_t rc;

	/*
	 * Enable firmware workarounds for hardware errata.
	 * Expected responses are:
	 *  - 0 (zero):
	 *	Success: workaround enabled or disabled as requested.
	 *  - MC_CMD_ERR_ENOSYS (reported as ENOTSUP):
	 *	Firmware does not support the MC_CMD_WORKAROUND request.
	 *	(assume that the workaround is not supported).
	 *  - MC_CMD_ERR_ENOENT (reported as ENOENT):
	 *	Firmware does not support the requested workaround.
	 *  - MC_CMD_ERR_EPERM  (reported as EACCES):
	 *	Unprivileged function cannot enable/disable workarounds.
	 *
	 * See efx_mcdi_request_errcode() for MCDI error translations.
	 */

	/*
	 * If the bug35388 workaround is enabled, then use an indirect access
	 * method to avoid unsafe EVQ writes.
	 */
	rc = efx_mcdi_set_workaround(enp, MC_CMD_WORKAROUND_BUG35388, B_TRUE,
	    NULL);
	if ((rc == 0) || (rc == EACCES))
		encp->enc_bug35388_workaround = B_TRUE;
	else if ((rc == ENOTSUP) || (rc == ENOENT))
		encp->enc_bug35388_workaround = B_FALSE;
	else
		goto fail1;

	/*
	 * If the bug41750 workaround is enabled, then do not test interrupts,
	 * as the test will fail (seen with Greenport controllers).
	 */
	rc = efx_mcdi_set_workaround(enp, MC_CMD_WORKAROUND_BUG41750, B_TRUE,
	    NULL);
	if (rc == 0) {
		encp->enc_bug41750_workaround = B_TRUE;
	} else if (rc == EACCES) {
		/* Assume a controller with 40G ports needs the workaround. */
		if (epp->ep_default_adv_cap_mask & EFX_PHY_CAP_40000FDX)
			encp->enc_bug41750_workaround = B_TRUE;
		else
			encp->enc_bug41750_workaround = B_FALSE;
	} else if ((rc == ENOTSUP) || (rc == ENOENT)) {
		encp->enc_bug41750_workaround = B_FALSE;
	} else {
		goto fail2;
	}
	if (EFX_PCI_FUNCTION_IS_VF(encp)) {
		/* Interrupt testing does not work for VFs. See bug50084. */
		encp->enc_bug41750_workaround = B_TRUE;
	}

	/*
	 * If the bug26807 workaround is enabled, then firmware has enabled
	 * support for chained multicast filters. Firmware will reset (FLR)
	 * functions which have filters in the hardware filter table when the
	 * workaround is enabled/disabled.
	 *
	 * We must recheck if the workaround is enabled after inserting the
	 * first hardware filter, in case it has been changed since this check.
	 */
	rc = efx_mcdi_set_workaround(enp, MC_CMD_WORKAROUND_BUG26807,
	    B_TRUE, &flags);
	if (rc == 0) {
		encp->enc_bug26807_workaround = B_TRUE;
		if (flags & (1 << MC_CMD_WORKAROUND_EXT_OUT_FLR_DONE_LBN)) {
			/*
			 * Other functions had installed filters before the
			 * workaround was enabled, and they have been reset
			 * by firmware.
			 */
			EFSYS_PROBE(bug26807_workaround_flr_done);
			/* FIXME: bump MC warm boot count ? */
		}
	} else if (rc == EACCES) {
		/*
		 * Unprivileged functions cannot enable the workaround in older
		 * firmware.
		 */
		encp->enc_bug26807_workaround = B_FALSE;
	} else if ((rc == ENOTSUP) || (rc == ENOENT)) {
		encp->enc_bug26807_workaround = B_FALSE;
	} else {
		goto fail3;
	}

	/* Get clock frequencies (in MHz). */
	if ((rc = efx_mcdi_get_clock(enp, &sysclk, &dpcpu_clk)) != 0)
		goto fail4;

	/*
	 * The Huntington timer quantum is 1536 sysclk cycles, documented for
	 * the EV_TMR_VAL field of EV_TIMER_TBL. Scale for MHz and ns units.
	 */
	encp->enc_evq_timer_quantum_ns = 1536000UL / sysclk; /* 1536 cycles */
	if (encp->enc_bug35388_workaround) {
		encp->enc_evq_timer_max_us = (encp->enc_evq_timer_quantum_ns <<
		ERF_DD_EVQ_IND_TIMER_VAL_WIDTH) / 1000;
	} else {
		encp->enc_evq_timer_max_us = (encp->enc_evq_timer_quantum_ns <<
		FRF_CZ_TC_TIMER_VAL_WIDTH) / 1000;
	}

	encp->enc_bug61265_workaround = B_FALSE; /* Medford only */

	/* Checksums for TSO sends can be incorrect on Huntington. */
	encp->enc_bug61297_workaround = B_TRUE;

	/* Alignment for receive packet DMA buffers */
	encp->enc_rx_buf_align_start = 1;
	encp->enc_rx_buf_align_end = 64; /* RX DMA end padding */

	/*
	 * The workaround for bug35388 uses the top bit of transmit queue
	 * descriptor writes, preventing the use of 4096 descriptor TXQs.
	 */
	encp->enc_txq_max_ndescs = encp->enc_bug35388_workaround ? 2048 : 4096;

	EFX_STATIC_ASSERT(HUNT_PIOBUF_NBUFS <= EF10_MAX_PIOBUF_NBUFS);
	encp->enc_piobuf_limit = HUNT_PIOBUF_NBUFS;
	encp->enc_piobuf_size = HUNT_PIOBUF_SIZE;
	encp->enc_piobuf_min_alloc_size = HUNT_MIN_PIO_ALLOC_SIZE;

	if ((rc = hunt_nic_get_required_pcie_bandwidth(enp, &bandwidth)) != 0)
		goto fail5;
	encp->enc_required_pcie_bandwidth_mbps = bandwidth;

	/* All Huntington devices have a PCIe Gen3, 8 lane connector */
	encp->enc_max_pcie_link_gen = EFX_PCIE_LINK_SPEED_GEN3;

	return (0);

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


#endif	/* EFSYS_OPT_HUNTINGTON */
