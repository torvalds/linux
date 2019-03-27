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

			void
siena_sram_init(
	__in		efx_nic_t *enp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_oword_t oword;
	uint32_t rx_base, tx_base;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	rx_base = encp->enc_buftbl_limit;
	tx_base = rx_base + (encp->enc_rxq_limit *
	    EFX_RXQ_DC_NDESCS(EFX_RXQ_DC_SIZE));

	/* Initialize the transmit descriptor cache */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_SRM_TX_DC_BASE_ADR, tx_base);
	EFX_BAR_WRITEO(enp, FR_AZ_SRM_TX_DC_CFG_REG, &oword);

	EFX_POPULATE_OWORD_1(oword, FRF_AZ_TX_DC_SIZE, EFX_TXQ_DC_SIZE);
	EFX_BAR_WRITEO(enp, FR_AZ_TX_DC_CFG_REG, &oword);

	/* Initialize the receive descriptor cache */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_SRM_RX_DC_BASE_ADR, rx_base);
	EFX_BAR_WRITEO(enp, FR_AZ_SRM_RX_DC_CFG_REG, &oword);

	EFX_POPULATE_OWORD_1(oword, FRF_AZ_RX_DC_SIZE, EFX_RXQ_DC_SIZE);
	EFX_BAR_WRITEO(enp, FR_AZ_RX_DC_CFG_REG, &oword);

	/* Set receive descriptor pre-fetch low water mark */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_RX_DC_PF_LWM, 56);
	EFX_BAR_WRITEO(enp, FR_AZ_RX_DC_PF_WM_REG, &oword);

	/* Set the event queue to use for SRAM updates */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_SRM_UPD_EVQ_ID, 0);
	EFX_BAR_WRITEO(enp, FR_AZ_SRM_UPD_EVQ_REG, &oword);
}

#if EFSYS_OPT_DIAG

	__checkReturn	efx_rc_t
siena_sram_test(
	__in		efx_nic_t *enp,
	__in		efx_sram_pattern_fn_t func)
{
	efx_oword_t oword;
	efx_qword_t qword;
	efx_qword_t verify;
	size_t rows;
	unsigned int wptr;
	unsigned int rptr;
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_SIENA);

	/* Reconfigure into HALF buffer table mode */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_BUF_TBL_MODE, 0);
	EFX_BAR_WRITEO(enp, FR_AZ_BUF_TBL_CFG_REG, &oword);

	/*
	 * Move the descriptor caches up to the top of SRAM, and test
	 * all of SRAM below them. We only miss out one row here.
	 */
	rows = SIENA_SRAM_ROWS - 1;
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_SRM_RX_DC_BASE_ADR, rows);
	EFX_BAR_WRITEO(enp, FR_AZ_SRM_RX_DC_CFG_REG, &oword);

	EFX_POPULATE_OWORD_1(oword, FRF_AZ_SRM_TX_DC_BASE_ADR, rows + 1);
	EFX_BAR_WRITEO(enp, FR_AZ_SRM_TX_DC_CFG_REG, &oword);

	/*
	 * Write the pattern through BUF_HALF_TBL. Write
	 * in 64 entry batches, waiting 1us in between each batch
	 * to guarantee not to overflow the SRAM fifo
	 */
	for (wptr = 0, rptr = 0; wptr < rows; ++wptr) {
		func(wptr, B_FALSE, &qword);
		EFX_BAR_TBL_WRITEQ(enp, FR_AZ_BUF_HALF_TBL, wptr, &qword);

		if ((wptr - rptr) < 64 && wptr < rows - 1)
			continue;

		EFSYS_SPIN(1);

		for (; rptr <= wptr; ++rptr) {
			func(rptr, B_FALSE, &qword);
			EFX_BAR_TBL_READQ(enp, FR_AZ_BUF_HALF_TBL, rptr,
			    &verify);

			if (!EFX_QWORD_IS_EQUAL(verify, qword)) {
				rc = EFAULT;
				goto fail1;
			}
		}
	}

	/* And do the same negated */
	for (wptr = 0, rptr = 0; wptr < rows; ++wptr) {
		func(wptr, B_TRUE, &qword);
		EFX_BAR_TBL_WRITEQ(enp, FR_AZ_BUF_HALF_TBL, wptr, &qword);

		if ((wptr - rptr) < 64 && wptr < rows - 1)
			continue;

		EFSYS_SPIN(1);

		for (; rptr <= wptr; ++rptr) {
			func(rptr, B_TRUE, &qword);
			EFX_BAR_TBL_READQ(enp, FR_AZ_BUF_HALF_TBL, rptr,
			    &verify);

			if (!EFX_QWORD_IS_EQUAL(verify, qword)) {
				rc = EFAULT;
				goto fail2;
			}
		}
	}

	/* Restore back to FULL buffer table mode */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_BUF_TBL_MODE, 1);
	EFX_BAR_WRITEO(enp, FR_AZ_BUF_TBL_CFG_REG, &oword);

	/*
	 * We don't need to reconfigure SRAM again because the API
	 * requires efx_nic_fini() to be called after an sram test.
	 */
	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	/* Restore back to FULL buffer table mode */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_BUF_TBL_MODE, 1);
	EFX_BAR_WRITEO(enp, FR_AZ_BUF_TBL_CFG_REG, &oword);

	return (rc);
}

#endif	/* EFSYS_OPT_DIAG */

#endif	/* EFSYS_OPT_SIENA */
