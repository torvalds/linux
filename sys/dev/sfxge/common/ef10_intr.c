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
ef10_intr_init(
	__in		efx_nic_t *enp,
	__in		efx_intr_type_t type,
	__in		efsys_mem_t *esmp)
{
	_NOTE(ARGUNUSED(enp, type, esmp))
	return (0);
}


			void
ef10_intr_enable(
	__in		efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
}


			void
ef10_intr_disable(
	__in		efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
}


			void
ef10_intr_disable_unlocked(
	__in		efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
}


static	__checkReturn	efx_rc_t
efx_mcdi_trigger_interrupt(
	__in		efx_nic_t *enp,
	__in		unsigned int level)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_TRIGGER_INTERRUPT_IN_LEN,
		MC_CMD_TRIGGER_INTERRUPT_OUT_LEN);
	efx_rc_t rc;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	if (level >= enp->en_nic_cfg.enc_intr_limit) {
		rc = EINVAL;
		goto fail1;
	}

	req.emr_cmd = MC_CMD_TRIGGER_INTERRUPT;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_TRIGGER_INTERRUPT_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_TRIGGER_INTERRUPT_OUT_LEN;

	MCDI_IN_SET_DWORD(req, TRIGGER_INTERRUPT_IN_INTR_LEVEL, level);

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

	__checkReturn	efx_rc_t
ef10_intr_trigger(
	__in		efx_nic_t *enp,
	__in		unsigned int level)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_rc_t rc;

	if (encp->enc_bug41750_workaround) {
		/*
		 * bug 41750: Test interrupts don't work on Greenport
		 * bug 50084: Test interrupts don't work on VFs
		 */
		rc = ENOTSUP;
		goto fail1;
	}

	if ((rc = efx_mcdi_trigger_interrupt(enp, level)) != 0)
		goto fail2;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
ef10_intr_status_line(
	__in		efx_nic_t *enp,
	__out		boolean_t *fatalp,
	__out		uint32_t *qmaskp)
{
	efx_dword_t dword;

	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	/* Read the queue mask and implicitly acknowledge the interrupt. */
	EFX_BAR_READD(enp, ER_DZ_BIU_INT_ISR_REG, &dword, B_FALSE);
	*qmaskp = EFX_DWORD_FIELD(dword, EFX_DWORD_0);

	EFSYS_PROBE1(qmask, uint32_t, *qmaskp);

	*fatalp = B_FALSE;
}

			void
ef10_intr_status_message(
	__in		efx_nic_t *enp,
	__in		unsigned int message,
	__out		boolean_t *fatalp)
{
	EFSYS_ASSERT(enp->en_family == EFX_FAMILY_HUNTINGTON ||
	    enp->en_family == EFX_FAMILY_MEDFORD ||
	    enp->en_family == EFX_FAMILY_MEDFORD2);

	_NOTE(ARGUNUSED(enp, message))

	/* EF10 fatal errors are reported via events */
	*fatalp = B_FALSE;
}

			void
ef10_intr_fatal(
	__in		efx_nic_t *enp)
{
	/* EF10 fatal errors are reported via events */
	_NOTE(ARGUNUSED(enp))
}

			void
ef10_intr_fini(
	__in		efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
}

#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
