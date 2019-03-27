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
siena_intr_init(
	__in		efx_nic_t *enp,
	__in		efx_intr_type_t type,
	__in		efsys_mem_t *esmp);

static			void
siena_intr_enable(
	__in		efx_nic_t *enp);

static			void
siena_intr_disable(
	__in		efx_nic_t *enp);

static			void
siena_intr_disable_unlocked(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
siena_intr_trigger(
	__in		efx_nic_t *enp,
	__in		unsigned int level);

static			void
siena_intr_fini(
	__in		efx_nic_t *enp);

static			void
siena_intr_status_line(
	__in		efx_nic_t *enp,
	__out		boolean_t *fatalp,
	__out		uint32_t *qmaskp);

static			void
siena_intr_status_message(
	__in		efx_nic_t *enp,
	__in		unsigned int message,
	__out		boolean_t *fatalp);

static			void
siena_intr_fatal(
	__in		efx_nic_t *enp);

static	__checkReturn	boolean_t
siena_intr_check_fatal(
	__in		efx_nic_t *enp);


#endif /* EFSYS_OPT_SIENA */


#if EFSYS_OPT_SIENA
static const efx_intr_ops_t	__efx_intr_siena_ops = {
	siena_intr_init,		/* eio_init */
	siena_intr_enable,		/* eio_enable */
	siena_intr_disable,		/* eio_disable */
	siena_intr_disable_unlocked,	/* eio_disable_unlocked */
	siena_intr_trigger,		/* eio_trigger */
	siena_intr_status_line,		/* eio_status_line */
	siena_intr_status_message,	/* eio_status_message */
	siena_intr_fatal,		/* eio_fatal */
	siena_intr_fini,		/* eio_fini */
};
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2
static const efx_intr_ops_t	__efx_intr_ef10_ops = {
	ef10_intr_init,			/* eio_init */
	ef10_intr_enable,		/* eio_enable */
	ef10_intr_disable,		/* eio_disable */
	ef10_intr_disable_unlocked,	/* eio_disable_unlocked */
	ef10_intr_trigger,		/* eio_trigger */
	ef10_intr_status_line,		/* eio_status_line */
	ef10_intr_status_message,	/* eio_status_message */
	ef10_intr_fatal,		/* eio_fatal */
	ef10_intr_fini,			/* eio_fini */
};
#endif	/* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */

	__checkReturn	efx_rc_t
efx_intr_init(
	__in		efx_nic_t *enp,
	__in		efx_intr_type_t type,
	__in_opt	efsys_mem_t *esmp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_NIC);

	if (enp->en_mod_flags & EFX_MOD_INTR) {
		rc = EINVAL;
		goto fail1;
	}

	eip->ei_esmp = esmp;
	eip->ei_type = type;
	eip->ei_level = 0;

	enp->en_mod_flags |= EFX_MOD_INTR;

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		eiop = &__efx_intr_siena_ops;
		break;
#endif	/* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		eiop = &__efx_intr_ef10_ops;
		break;
#endif	/* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		eiop = &__efx_intr_ef10_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		eiop = &__efx_intr_ef10_ops;
		break;
#endif	/* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(B_FALSE);
		rc = ENOTSUP;
		goto fail2;
	}

	if ((rc = eiop->eio_init(enp, type, esmp)) != 0)
		goto fail3;

	eip->ei_eiop = eiop;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

		void
efx_intr_fini(
	__in	efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_NIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_fini(enp);

	enp->en_mod_flags &= ~EFX_MOD_INTR;
}

			void
efx_intr_enable(
	__in		efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_enable(enp);
}

			void
efx_intr_disable(
	__in		efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_disable(enp);
}

			void
efx_intr_disable_unlocked(
	__in		efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_disable_unlocked(enp);
}


	__checkReturn	efx_rc_t
efx_intr_trigger(
	__in		efx_nic_t *enp,
	__in		unsigned int level)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	return (eiop->eio_trigger(enp, level));
}

			void
efx_intr_status_line(
	__in		efx_nic_t *enp,
	__out		boolean_t *fatalp,
	__out		uint32_t *qmaskp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_status_line(enp, fatalp, qmaskp);
}

			void
efx_intr_status_message(
	__in		efx_nic_t *enp,
	__in		unsigned int message,
	__out		boolean_t *fatalp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_status_message(enp, message, fatalp);
}

		void
efx_intr_fatal(
	__in	efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	const efx_intr_ops_t *eiop = eip->ei_eiop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	eiop->eio_fatal(enp);
}


/* ************************************************************************* */
/* ************************************************************************* */
/* ************************************************************************* */

#if EFSYS_OPT_SIENA

static	__checkReturn	efx_rc_t
siena_intr_init(
	__in		efx_nic_t *enp,
	__in		efx_intr_type_t type,
	__in		efsys_mem_t *esmp)
{
	efx_intr_t *eip = &(enp->en_intr);
	efx_oword_t oword;
	efx_rc_t rc;

	if ((esmp == NULL) || (EFSYS_MEM_SIZE(esmp) < EFX_INTR_SIZE)) {
		rc = EINVAL;
		goto fail1;
	}

	/*
	 * bug17213 workaround.
	 *
	 * Under legacy interrupts, don't share a level between fatal
	 * interrupts and event queue interrupts. Under MSI-X, they
	 * must share, or we won't get an interrupt.
	 */
	if (enp->en_family == EFX_FAMILY_SIENA &&
	    eip->ei_type == EFX_INTR_LINE)
		eip->ei_level = 0x1f;
	else
		eip->ei_level = 0;

	/* Enable all the genuinely fatal interrupts */
	EFX_SET_OWORD(oword);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_ILL_ADR_INT_KER_EN, 0);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_RBUF_OWN_INT_KER_EN, 0);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TBUF_OWN_INT_KER_EN, 0);
	if (enp->en_family >= EFX_FAMILY_SIENA)
		EFX_SET_OWORD_FIELD(oword, FRF_CZ_SRAM_PERR_INT_P_KER_EN, 0);
	EFX_BAR_WRITEO(enp, FR_AZ_FATAL_INTR_REG_KER, &oword);

	/* Set up the interrupt address register */
	EFX_POPULATE_OWORD_3(oword,
	    FRF_AZ_NORM_INT_VEC_DIS_KER, (type == EFX_INTR_MESSAGE) ? 1 : 0,
	    FRF_AZ_INT_ADR_KER_DW0, EFSYS_MEM_ADDR(esmp) & 0xffffffff,
	    FRF_AZ_INT_ADR_KER_DW1, EFSYS_MEM_ADDR(esmp) >> 32);
	EFX_BAR_WRITEO(enp, FR_AZ_INT_ADR_REG_KER, &oword);

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static			void
siena_intr_enable(
	__in		efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	efx_oword_t oword;

	EFX_BAR_READO(enp, FR_AZ_INT_EN_REG_KER, &oword);

	EFX_SET_OWORD_FIELD(oword, FRF_AZ_KER_INT_LEVE_SEL, eip->ei_level);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_DRV_INT_EN_KER, 1);
	EFX_BAR_WRITEO(enp, FR_AZ_INT_EN_REG_KER, &oword);
}

static			void
siena_intr_disable(
	__in		efx_nic_t *enp)
{
	efx_oword_t oword;

	EFX_BAR_READO(enp, FR_AZ_INT_EN_REG_KER, &oword);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_DRV_INT_EN_KER, 0);
	EFX_BAR_WRITEO(enp, FR_AZ_INT_EN_REG_KER, &oword);

	EFSYS_SPIN(10);
}

static			void
siena_intr_disable_unlocked(
	__in		efx_nic_t *enp)
{
	efx_oword_t oword;

	EFSYS_BAR_READO(enp->en_esbp, FR_AZ_INT_EN_REG_KER_OFST,
			&oword, B_FALSE);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_DRV_INT_EN_KER, 0);
	EFSYS_BAR_WRITEO(enp->en_esbp, FR_AZ_INT_EN_REG_KER_OFST,
	    &oword, B_FALSE);
}

static	__checkReturn	efx_rc_t
siena_intr_trigger(
	__in		efx_nic_t *enp,
	__in		unsigned int level)
{
	efx_intr_t *eip = &(enp->en_intr);
	efx_oword_t oword;
	unsigned int count;
	uint32_t sel;
	efx_rc_t rc;

	/* bug16757: No event queues can be initialized */
	EFSYS_ASSERT(!(enp->en_mod_flags & EFX_MOD_EV));

	if (level >= EFX_NINTR_SIENA) {
		rc = EINVAL;
		goto fail1;
	}

	if (level > EFX_MASK32(FRF_AZ_KER_INT_LEVE_SEL))
		return (ENOTSUP); /* avoid EFSYS_PROBE() */

	sel = level;

	/* Trigger a test interrupt */
	EFX_BAR_READO(enp, FR_AZ_INT_EN_REG_KER, &oword);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_KER_INT_LEVE_SEL, sel);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_KER_INT_KER, 1);
	EFX_BAR_WRITEO(enp, FR_AZ_INT_EN_REG_KER, &oword);

	/*
	 * Wait up to 100ms for the interrupt to be raised before restoring
	 * KER_INT_LEVE_SEL. Ignore a failure to raise (the caller will
	 * observe this soon enough anyway), but always reset KER_INT_LEVE_SEL
	 */
	count = 0;
	do {
		EFSYS_SPIN(100);	/* 100us */

		EFX_BAR_READO(enp, FR_AZ_INT_EN_REG_KER, &oword);
	} while (EFX_OWORD_FIELD(oword, FRF_AZ_KER_INT_KER) && ++count < 1000);

	EFX_SET_OWORD_FIELD(oword, FRF_AZ_KER_INT_LEVE_SEL, eip->ei_level);
	EFX_BAR_WRITEO(enp, FR_AZ_INT_EN_REG_KER, &oword);

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	boolean_t
siena_intr_check_fatal(
	__in		efx_nic_t *enp)
{
	efx_intr_t *eip = &(enp->en_intr);
	efsys_mem_t *esmp = eip->ei_esmp;
	efx_oword_t oword;

	/* Read the syndrome */
	EFSYS_MEM_READO(esmp, 0, &oword);

	if (EFX_OWORD_FIELD(oword, FSF_AZ_NET_IVEC_FATAL_INT) != 0) {
		EFSYS_PROBE(fatal);

		/* Clear the fatal interrupt condition */
		EFX_SET_OWORD_FIELD(oword, FSF_AZ_NET_IVEC_FATAL_INT, 0);
		EFSYS_MEM_WRITEO(esmp, 0, &oword);

		return (B_TRUE);
	}

	return (B_FALSE);
}

static			void
siena_intr_status_line(
	__in		efx_nic_t *enp,
	__out		boolean_t *fatalp,
	__out		uint32_t *qmaskp)
{
	efx_intr_t *eip = &(enp->en_intr);
	efx_dword_t dword;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	/*
	 * Read the queue mask and implicitly acknowledge the
	 * interrupt.
	 */
	EFX_BAR_READD(enp, FR_BZ_INT_ISR0_REG, &dword, B_FALSE);
	*qmaskp = EFX_DWORD_FIELD(dword, EFX_DWORD_0);

	EFSYS_PROBE1(qmask, uint32_t, *qmaskp);

	if (*qmaskp & (1U << eip->ei_level))
		*fatalp = siena_intr_check_fatal(enp);
	else
		*fatalp = B_FALSE;
}

static			void
siena_intr_status_message(
	__in		efx_nic_t *enp,
	__in		unsigned int message,
	__out		boolean_t *fatalp)
{
	efx_intr_t *eip = &(enp->en_intr);

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_INTR);

	if (message == eip->ei_level)
		*fatalp = siena_intr_check_fatal(enp);
	else
		*fatalp = B_FALSE;
}


static		void
siena_intr_fatal(
	__in	efx_nic_t *enp)
{
#if EFSYS_OPT_DECODE_INTR_FATAL
	efx_oword_t fatal;
	efx_oword_t mem_per;

	EFX_BAR_READO(enp, FR_AZ_FATAL_INTR_REG_KER, &fatal);
	EFX_ZERO_OWORD(mem_per);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_SRM_PERR_INT_KER) != 0 ||
	    EFX_OWORD_FIELD(fatal, FRF_AZ_MEM_PERR_INT_KER) != 0)
		EFX_BAR_READO(enp, FR_AZ_MEM_STAT_REG, &mem_per);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_SRAM_OOB_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_SRAM_OOB, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_BUFID_DC_OOB_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_BUFID_DC_OOB, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_MEM_PERR_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_MEM_PERR,
		    EFX_OWORD_FIELD(mem_per, EFX_DWORD_0),
		    EFX_OWORD_FIELD(mem_per, EFX_DWORD_1));

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_RBUF_OWN_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_RBUF_OWN, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_TBUF_OWN_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_TBUF_OWN, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_RDESCQ_OWN_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_RDESQ_OWN, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_TDESCQ_OWN_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_TDESQ_OWN, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_EVQ_OWN_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_EVQ_OWN, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_EVF_OFLO_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_EVFF_OFLO, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_ILL_ADR_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_ILL_ADDR, 0, 0);

	if (EFX_OWORD_FIELD(fatal, FRF_AZ_SRM_PERR_INT_KER) != 0)
		EFSYS_ERR(enp->en_esip, EFX_ERR_SRAM_PERR,
		    EFX_OWORD_FIELD(mem_per, EFX_DWORD_0),
		    EFX_OWORD_FIELD(mem_per, EFX_DWORD_1));
#else
	EFSYS_ASSERT(0);
#endif
}

static		void
siena_intr_fini(
	__in	efx_nic_t *enp)
{
	efx_oword_t oword;

	/* Clear the interrupt address register */
	EFX_ZERO_OWORD(oword);
	EFX_BAR_WRITEO(enp, FR_AZ_INT_ADR_REG_KER, &oword);
}

#endif /* EFSYS_OPT_SIENA */
