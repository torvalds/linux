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

#if EFSYS_OPT_QSTATS
#define	EFX_TX_QSTAT_INCR(_etp, _stat)					\
	do {								\
		(_etp)->et_stat[_stat]++;				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFX_TX_QSTAT_INCR(_etp, _stat)
#endif

#if EFSYS_OPT_SIENA

static	__checkReturn	efx_rc_t
siena_tx_init(
	__in		efx_nic_t *enp);

static			void
siena_tx_fini(
	__in		efx_nic_t *enp);

static	__checkReturn	efx_rc_t
siena_tx_qcreate(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		uint32_t id,
	__in		uint16_t flags,
	__in		efx_evq_t *eep,
	__in		efx_txq_t *etp,
	__out		unsigned int *addedp);

static		void
siena_tx_qdestroy(
	__in	efx_txq_t *etp);

static	__checkReturn		efx_rc_t
siena_tx_qpost(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_buffer_t *eb,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp);

static			void
siena_tx_qpush(
	__in	efx_txq_t *etp,
	__in	unsigned int added,
	__in	unsigned int pushed);

static	__checkReturn	efx_rc_t
siena_tx_qpace(
	__in		efx_txq_t *etp,
	__in		unsigned int ns);

static	__checkReturn	efx_rc_t
siena_tx_qflush(
	__in		efx_txq_t *etp);

static			void
siena_tx_qenable(
	__in	efx_txq_t *etp);

	__checkReturn		efx_rc_t
siena_tx_qdesc_post(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_desc_t *ed,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp);

	void
siena_tx_qdesc_dma_create(
	__in	efx_txq_t *etp,
	__in	efsys_dma_addr_t addr,
	__in	size_t size,
	__in	boolean_t eop,
	__out	efx_desc_t *edp);

#if EFSYS_OPT_QSTATS
static			void
siena_tx_qstats_update(
	__in				efx_txq_t *etp,
	__inout_ecount(TX_NQSTATS)	efsys_stat_t *stat);
#endif

#endif /* EFSYS_OPT_SIENA */


#if EFSYS_OPT_SIENA
static const efx_tx_ops_t	__efx_tx_siena_ops = {
	siena_tx_init,				/* etxo_init */
	siena_tx_fini,				/* etxo_fini */
	siena_tx_qcreate,			/* etxo_qcreate */
	siena_tx_qdestroy,			/* etxo_qdestroy */
	siena_tx_qpost,				/* etxo_qpost */
	siena_tx_qpush,				/* etxo_qpush */
	siena_tx_qpace,				/* etxo_qpace */
	siena_tx_qflush,			/* etxo_qflush */
	siena_tx_qenable,			/* etxo_qenable */
	NULL,					/* etxo_qpio_enable */
	NULL,					/* etxo_qpio_disable */
	NULL,					/* etxo_qpio_write */
	NULL,					/* etxo_qpio_post */
	siena_tx_qdesc_post,			/* etxo_qdesc_post */
	siena_tx_qdesc_dma_create,		/* etxo_qdesc_dma_create */
	NULL,					/* etxo_qdesc_tso_create */
	NULL,					/* etxo_qdesc_tso2_create */
	NULL,					/* etxo_qdesc_vlantci_create */
	NULL,					/* etxo_qdesc_checksum_create */
#if EFSYS_OPT_QSTATS
	siena_tx_qstats_update,			/* etxo_qstats_update */
#endif
};
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
static const efx_tx_ops_t	__efx_tx_hunt_ops = {
	ef10_tx_init,				/* etxo_init */
	ef10_tx_fini,				/* etxo_fini */
	ef10_tx_qcreate,			/* etxo_qcreate */
	ef10_tx_qdestroy,			/* etxo_qdestroy */
	ef10_tx_qpost,				/* etxo_qpost */
	ef10_tx_qpush,				/* etxo_qpush */
	ef10_tx_qpace,				/* etxo_qpace */
	ef10_tx_qflush,				/* etxo_qflush */
	ef10_tx_qenable,			/* etxo_qenable */
	ef10_tx_qpio_enable,			/* etxo_qpio_enable */
	ef10_tx_qpio_disable,			/* etxo_qpio_disable */
	ef10_tx_qpio_write,			/* etxo_qpio_write */
	ef10_tx_qpio_post,			/* etxo_qpio_post */
	ef10_tx_qdesc_post,			/* etxo_qdesc_post */
	ef10_tx_qdesc_dma_create,		/* etxo_qdesc_dma_create */
	ef10_tx_qdesc_tso_create,		/* etxo_qdesc_tso_create */
	ef10_tx_qdesc_tso2_create,		/* etxo_qdesc_tso2_create */
	ef10_tx_qdesc_vlantci_create,		/* etxo_qdesc_vlantci_create */
	ef10_tx_qdesc_checksum_create,		/* etxo_qdesc_checksum_create */
#if EFSYS_OPT_QSTATS
	ef10_tx_qstats_update,			/* etxo_qstats_update */
#endif
};
#endif /* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
static const efx_tx_ops_t	__efx_tx_medford_ops = {
	ef10_tx_init,				/* etxo_init */
	ef10_tx_fini,				/* etxo_fini */
	ef10_tx_qcreate,			/* etxo_qcreate */
	ef10_tx_qdestroy,			/* etxo_qdestroy */
	ef10_tx_qpost,				/* etxo_qpost */
	ef10_tx_qpush,				/* etxo_qpush */
	ef10_tx_qpace,				/* etxo_qpace */
	ef10_tx_qflush,				/* etxo_qflush */
	ef10_tx_qenable,			/* etxo_qenable */
	ef10_tx_qpio_enable,			/* etxo_qpio_enable */
	ef10_tx_qpio_disable,			/* etxo_qpio_disable */
	ef10_tx_qpio_write,			/* etxo_qpio_write */
	ef10_tx_qpio_post,			/* etxo_qpio_post */
	ef10_tx_qdesc_post,			/* etxo_qdesc_post */
	ef10_tx_qdesc_dma_create,		/* etxo_qdesc_dma_create */
	NULL,					/* etxo_qdesc_tso_create */
	ef10_tx_qdesc_tso2_create,		/* etxo_qdesc_tso2_create */
	ef10_tx_qdesc_vlantci_create,		/* etxo_qdesc_vlantci_create */
	ef10_tx_qdesc_checksum_create,		/* etxo_qdesc_checksum_create */
#if EFSYS_OPT_QSTATS
	ef10_tx_qstats_update,			/* etxo_qstats_update */
#endif
};
#endif /* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
static const efx_tx_ops_t	__efx_tx_medford2_ops = {
	ef10_tx_init,				/* etxo_init */
	ef10_tx_fini,				/* etxo_fini */
	ef10_tx_qcreate,			/* etxo_qcreate */
	ef10_tx_qdestroy,			/* etxo_qdestroy */
	ef10_tx_qpost,				/* etxo_qpost */
	ef10_tx_qpush,				/* etxo_qpush */
	ef10_tx_qpace,				/* etxo_qpace */
	ef10_tx_qflush,				/* etxo_qflush */
	ef10_tx_qenable,			/* etxo_qenable */
	ef10_tx_qpio_enable,			/* etxo_qpio_enable */
	ef10_tx_qpio_disable,			/* etxo_qpio_disable */
	ef10_tx_qpio_write,			/* etxo_qpio_write */
	ef10_tx_qpio_post,			/* etxo_qpio_post */
	ef10_tx_qdesc_post,			/* etxo_qdesc_post */
	ef10_tx_qdesc_dma_create,		/* etxo_qdesc_dma_create */
	NULL,					/* etxo_qdesc_tso_create */
	ef10_tx_qdesc_tso2_create,		/* etxo_qdesc_tso2_create */
	ef10_tx_qdesc_vlantci_create,		/* etxo_qdesc_vlantci_create */
	ef10_tx_qdesc_checksum_create,		/* etxo_qdesc_checksum_create */
#if EFSYS_OPT_QSTATS
	ef10_tx_qstats_update,			/* etxo_qstats_update */
#endif
};
#endif /* EFSYS_OPT_MEDFORD2 */


	__checkReturn	efx_rc_t
efx_tx_init(
	__in		efx_nic_t *enp)
{
	const efx_tx_ops_t *etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_NIC);

	if (!(enp->en_mod_flags & EFX_MOD_EV)) {
		rc = EINVAL;
		goto fail1;
	}

	if (enp->en_mod_flags & EFX_MOD_TX) {
		rc = EINVAL;
		goto fail2;
	}

	switch (enp->en_family) {
#if EFSYS_OPT_SIENA
	case EFX_FAMILY_SIENA:
		etxop = &__efx_tx_siena_ops;
		break;
#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_HUNTINGTON
	case EFX_FAMILY_HUNTINGTON:
		etxop = &__efx_tx_hunt_ops;
		break;
#endif /* EFSYS_OPT_HUNTINGTON */

#if EFSYS_OPT_MEDFORD
	case EFX_FAMILY_MEDFORD:
		etxop = &__efx_tx_medford_ops;
		break;
#endif /* EFSYS_OPT_MEDFORD */

#if EFSYS_OPT_MEDFORD2
	case EFX_FAMILY_MEDFORD2:
		etxop = &__efx_tx_medford2_ops;
		break;
#endif /* EFSYS_OPT_MEDFORD2 */

	default:
		EFSYS_ASSERT(0);
		rc = ENOTSUP;
		goto fail3;
	}

	EFSYS_ASSERT3U(enp->en_tx_qcount, ==, 0);

	if ((rc = etxop->etxo_init(enp)) != 0)
		goto fail4;

	enp->en_etxop = etxop;
	enp->en_mod_flags |= EFX_MOD_TX;
	return (0);

fail4:
	EFSYS_PROBE(fail4);
fail3:
	EFSYS_PROBE(fail3);
fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	enp->en_etxop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_TX;
	return (rc);
}

			void
efx_tx_fini(
	__in	efx_nic_t *enp)
{
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_NIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TX);
	EFSYS_ASSERT3U(enp->en_tx_qcount, ==, 0);

	etxop->etxo_fini(enp);

	enp->en_etxop = NULL;
	enp->en_mod_flags &= ~EFX_MOD_TX;
}

	__checkReturn	efx_rc_t
efx_tx_qcreate(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		uint32_t id,
	__in		uint16_t flags,
	__in		efx_evq_t *eep,
	__deref_out	efx_txq_t **etpp,
	__out		unsigned int *addedp)
{
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_txq_t *etp;
	efx_rc_t rc;

	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(enp->en_mod_flags, &, EFX_MOD_TX);

	EFSYS_ASSERT3U(enp->en_tx_qcount + 1, <,
	    enp->en_nic_cfg.enc_txq_limit);

	/* Allocate an TXQ object */
	EFSYS_KMEM_ALLOC(enp->en_esip, sizeof (efx_txq_t), etp);

	if (etp == NULL) {
		rc = ENOMEM;
		goto fail1;
	}

	etp->et_magic = EFX_TXQ_MAGIC;
	etp->et_enp = enp;
	etp->et_index = index;
	etp->et_mask = ndescs - 1;
	etp->et_esmp = esmp;

	/* Initial descriptor index may be modified by etxo_qcreate */
	*addedp = 0;

	if ((rc = etxop->etxo_qcreate(enp, index, label, esmp,
	    ndescs, id, flags, eep, etp, addedp)) != 0)
		goto fail2;

	enp->en_tx_qcount++;
	*etpp = etp;

	return (0);

fail2:
	EFSYS_PROBE(fail2);
	EFSYS_KMEM_FREE(enp->en_esip, sizeof (efx_txq_t), etp);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

		void
efx_tx_qdestroy(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	EFSYS_ASSERT(enp->en_tx_qcount != 0);
	--enp->en_tx_qcount;

	etxop->etxo_qdestroy(etp);

	/* Free the TXQ object */
	EFSYS_KMEM_FREE(enp->en_esip, sizeof (efx_txq_t), etp);
}

	__checkReturn		efx_rc_t
efx_tx_qpost(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_buffer_t *eb,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if ((rc = etxop->etxo_qpost(etp, eb, ndescs, completed, addedp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

			void
efx_tx_qpush(
	__in	efx_txq_t *etp,
	__in	unsigned int added,
	__in	unsigned int pushed)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	etxop->etxo_qpush(etp, added, pushed);
}

	__checkReturn	efx_rc_t
efx_tx_qpace(
	__in		efx_txq_t *etp,
	__in		unsigned int ns)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if ((rc = etxop->etxo_qpace(etp, ns)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

	__checkReturn	efx_rc_t
efx_tx_qflush(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if ((rc = etxop->etxo_qflush(etp)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

			void
efx_tx_qenable(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	etxop->etxo_qenable(etp);
}

	__checkReturn	efx_rc_t
efx_tx_qpio_enable(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if (~enp->en_features & EFX_FEATURE_PIO_BUFFERS) {
		rc = ENOTSUP;
		goto fail1;
	}
	if (etxop->etxo_qpio_enable == NULL) {
		rc = ENOTSUP;
		goto fail2;
	}
	if ((rc = etxop->etxo_qpio_enable(etp)) != 0)
		goto fail3;

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
efx_tx_qpio_disable(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if (etxop->etxo_qpio_disable != NULL)
		etxop->etxo_qpio_disable(etp);
}

	__checkReturn	efx_rc_t
efx_tx_qpio_write(
	__in			efx_txq_t *etp,
	__in_ecount(buf_length)	uint8_t *buffer,
	__in			size_t buf_length,
	__in			size_t pio_buf_offset)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if (etxop->etxo_qpio_write != NULL) {
		if ((rc = etxop->etxo_qpio_write(etp, buffer, buf_length,
						pio_buf_offset)) != 0)
			goto fail1;
		return (0);
	}

	return (ENOTSUP);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

	__checkReturn	efx_rc_t
efx_tx_qpio_post(
	__in			efx_txq_t *etp,
	__in			size_t pkt_length,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;
	efx_rc_t rc;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	if (etxop->etxo_qpio_post != NULL) {
		if ((rc = etxop->etxo_qpio_post(etp, pkt_length, completed,
						addedp)) != 0)
			goto fail1;
		return (0);
	}

	return (ENOTSUP);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

	__checkReturn		efx_rc_t
efx_tx_qdesc_post(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_desc_t *ed,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	return (etxop->etxo_qdesc_post(etp, ed, ndescs, completed, addedp));
}

	void
efx_tx_qdesc_dma_create(
	__in	efx_txq_t *etp,
	__in	efsys_dma_addr_t addr,
	__in	size_t size,
	__in	boolean_t eop,
	__out	efx_desc_t *edp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);
	EFSYS_ASSERT(etxop->etxo_qdesc_dma_create != NULL);

	etxop->etxo_qdesc_dma_create(etp, addr, size, eop, edp);
}

	void
efx_tx_qdesc_tso_create(
	__in	efx_txq_t *etp,
	__in	uint16_t ipv4_id,
	__in	uint32_t tcp_seq,
	__in	uint8_t  tcp_flags,
	__out	efx_desc_t *edp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);
	EFSYS_ASSERT(etxop->etxo_qdesc_tso_create != NULL);

	etxop->etxo_qdesc_tso_create(etp, ipv4_id, tcp_seq, tcp_flags, edp);
}

	void
efx_tx_qdesc_tso2_create(
	__in			efx_txq_t *etp,
	__in			uint16_t ipv4_id,
	__in			uint16_t outer_ipv4_id,
	__in			uint32_t tcp_seq,
	__in			uint16_t mss,
	__out_ecount(count)	efx_desc_t *edp,
	__in			int count)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);
	EFSYS_ASSERT(etxop->etxo_qdesc_tso2_create != NULL);

	etxop->etxo_qdesc_tso2_create(etp, ipv4_id, outer_ipv4_id,
	    tcp_seq, mss, edp, count);
}

	void
efx_tx_qdesc_vlantci_create(
	__in	efx_txq_t *etp,
	__in	uint16_t tci,
	__out	efx_desc_t *edp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);
	EFSYS_ASSERT(etxop->etxo_qdesc_vlantci_create != NULL);

	etxop->etxo_qdesc_vlantci_create(etp, tci, edp);
}

	void
efx_tx_qdesc_checksum_create(
	__in	efx_txq_t *etp,
	__in	uint16_t flags,
	__out	efx_desc_t *edp)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);
	EFSYS_ASSERT(etxop->etxo_qdesc_checksum_create != NULL);

	etxop->etxo_qdesc_checksum_create(etp, flags, edp);
}


#if EFSYS_OPT_QSTATS
			void
efx_tx_qstats_update(
	__in				efx_txq_t *etp,
	__inout_ecount(TX_NQSTATS)	efsys_stat_t *stat)
{
	efx_nic_t *enp = etp->et_enp;
	const efx_tx_ops_t *etxop = enp->en_etxop;

	EFSYS_ASSERT3U(etp->et_magic, ==, EFX_TXQ_MAGIC);

	etxop->etxo_qstats_update(etp, stat);
}
#endif


#if EFSYS_OPT_SIENA

static	__checkReturn	efx_rc_t
siena_tx_init(
	__in		efx_nic_t *enp)
{
	efx_oword_t oword;

	/*
	 * Disable the timer-based TX DMA backoff and allow TX DMA to be
	 * controlled by the RX FIFO fill level (although always allow a
	 * minimal trickle).
	 */
	EFX_BAR_READO(enp, FR_AZ_TX_RESERVED_REG, &oword);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_RX_SPACER, 0xfe);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_RX_SPACER_EN, 1);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_ONE_PKT_PER_Q, 1);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_PUSH_EN, 0);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_DIS_NON_IP_EV, 1);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_PREF_THRESHOLD, 2);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_PREF_WD_TMR, 0x3fffff);

	/*
	 * Filter all packets less than 14 bytes to avoid parsing
	 * errors.
	 */
	EFX_SET_OWORD_FIELD(oword, FRF_BZ_TX_FLUSH_MIN_LEN_EN, 1);
	EFX_BAR_WRITEO(enp, FR_AZ_TX_RESERVED_REG, &oword);

	/*
	 * Do not set TX_NO_EOP_DISC_EN, since it limits packets to 16
	 * descriptors (which is bad).
	 */
	EFX_BAR_READO(enp, FR_AZ_TX_CFG_REG, &oword);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_NO_EOP_DISC_EN, 0);
	EFX_BAR_WRITEO(enp, FR_AZ_TX_CFG_REG, &oword);

	return (0);
}

#define	EFX_TX_DESC(_etp, _addr, _size, _eop, _added)			\
	do {								\
		unsigned int id;					\
		size_t offset;						\
		efx_qword_t qword;					\
									\
		id = (_added)++ & (_etp)->et_mask;			\
		offset = id * sizeof (efx_qword_t);			\
									\
		EFSYS_PROBE5(tx_post, unsigned int, (_etp)->et_index,	\
		    unsigned int, id, efsys_dma_addr_t, (_addr),	\
		    size_t, (_size), boolean_t, (_eop));		\
									\
		EFX_POPULATE_QWORD_4(qword,				\
		    FSF_AZ_TX_KER_CONT, (_eop) ? 0 : 1,			\
		    FSF_AZ_TX_KER_BYTE_COUNT, (uint32_t)(_size),	\
		    FSF_AZ_TX_KER_BUF_ADDR_DW0,				\
		    (uint32_t)((_addr) & 0xffffffff),			\
		    FSF_AZ_TX_KER_BUF_ADDR_DW1,				\
		    (uint32_t)((_addr) >> 32));				\
		EFSYS_MEM_WRITEQ((_etp)->et_esmp, offset, &qword);	\
									\
		_NOTE(CONSTANTCONDITION)				\
	} while (B_FALSE)

static	__checkReturn		efx_rc_t
siena_tx_qpost(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_buffer_t *eb,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	unsigned int added = *addedp;
	unsigned int i;

	if (added - completed + ndescs > EFX_TXQ_LIMIT(etp->et_mask + 1))
		return (ENOSPC);

	for (i = 0; i < ndescs; i++) {
		efx_buffer_t *ebp = &eb[i];
		efsys_dma_addr_t start = ebp->eb_addr;
		size_t size = ebp->eb_size;
		efsys_dma_addr_t end = start + size;

		/*
		 * Fragments must not span 4k boundaries.
		 * Here it is a stricter requirement than the maximum length.
		 */
		EFSYS_ASSERT(P2ROUNDUP(start + 1,
		    etp->et_enp->en_nic_cfg.enc_tx_dma_desc_boundary) >= end);

		EFX_TX_DESC(etp, start, size, ebp->eb_eop, added);
	}

	EFX_TX_QSTAT_INCR(etp, TX_POST);

	*addedp = added;
	return (0);
}

static		void
siena_tx_qpush(
	__in	efx_txq_t *etp,
	__in	unsigned int added,
	__in	unsigned int pushed)
{
	efx_nic_t *enp = etp->et_enp;
	uint32_t wptr;
	efx_dword_t dword;
	efx_oword_t oword;

	/* Push the populated descriptors out */
	wptr = added & etp->et_mask;

	EFX_POPULATE_OWORD_1(oword, FRF_AZ_TX_DESC_WPTR, wptr);

	/* Only write the third DWORD */
	EFX_POPULATE_DWORD_1(dword,
	    EFX_DWORD_0, EFX_OWORD_FIELD(oword, EFX_DWORD_3));

	/* Guarantee ordering of memory (descriptors) and PIO (doorbell) */
	EFX_DMA_SYNC_QUEUE_FOR_DEVICE(etp->et_esmp, etp->et_mask + 1,
	    wptr, pushed & etp->et_mask);
	EFSYS_PIO_WRITE_BARRIER();
	EFX_BAR_TBL_WRITED3(enp, FR_BZ_TX_DESC_UPD_REGP0,
			    etp->et_index, &dword, B_FALSE);
}

#define	EFX_MAX_PACE_VALUE 20
#define	EFX_TX_PACE_CLOCK_BASE	104

static	__checkReturn	efx_rc_t
siena_tx_qpace(
	__in		efx_txq_t *etp,
	__in		unsigned int ns)
{
	efx_nic_t *enp = etp->et_enp;
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_oword_t oword;
	unsigned int pace_val;
	unsigned int timer_period;
	efx_rc_t rc;

	if (ns == 0) {
		pace_val = 0;
	} else {
		/*
		 * The pace_val to write into the table is s.t
		 * ns <= timer_period * (2 ^ pace_val)
		 */
		timer_period = EFX_TX_PACE_CLOCK_BASE / encp->enc_clk_mult;
		for (pace_val = 1; pace_val <= EFX_MAX_PACE_VALUE; pace_val++) {
			if ((timer_period << pace_val) >= ns)
				break;
		}
	}
	if (pace_val > EFX_MAX_PACE_VALUE) {
		rc = EINVAL;
		goto fail1;
	}

	/* Update the pacing table */
	EFX_POPULATE_OWORD_1(oword, FRF_AZ_TX_PACE, pace_val);
	EFX_BAR_TBL_WRITEO(enp, FR_AZ_TX_PACE_TBL, etp->et_index,
	    &oword, B_TRUE);

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

static	__checkReturn	efx_rc_t
siena_tx_qflush(
	__in		efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	efx_oword_t oword;
	uint32_t label;

	efx_tx_qpace(etp, 0);

	label = etp->et_index;

	/* Flush the queue */
	EFX_POPULATE_OWORD_2(oword, FRF_AZ_TX_FLUSH_DESCQ_CMD, 1,
	    FRF_AZ_TX_FLUSH_DESCQ, label);
	EFX_BAR_WRITEO(enp, FR_AZ_TX_FLUSH_DESCQ_REG, &oword);

	return (0);
}

static		void
siena_tx_qenable(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	efx_oword_t oword;

	EFX_BAR_TBL_READO(enp, FR_AZ_TX_DESC_PTR_TBL,
			    etp->et_index, &oword, B_TRUE);

	EFSYS_PROBE5(tx_descq_ptr, unsigned int, etp->et_index,
	    uint32_t, EFX_OWORD_FIELD(oword, EFX_DWORD_3),
	    uint32_t, EFX_OWORD_FIELD(oword, EFX_DWORD_2),
	    uint32_t, EFX_OWORD_FIELD(oword, EFX_DWORD_1),
	    uint32_t, EFX_OWORD_FIELD(oword, EFX_DWORD_0));

	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_DC_HW_RPTR, 0);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_DESCQ_HW_RPTR, 0);
	EFX_SET_OWORD_FIELD(oword, FRF_AZ_TX_DESCQ_EN, 1);

	EFX_BAR_TBL_WRITEO(enp, FR_AZ_TX_DESC_PTR_TBL,
			    etp->et_index, &oword, B_TRUE);
}

static	__checkReturn	efx_rc_t
siena_tx_qcreate(
	__in		efx_nic_t *enp,
	__in		unsigned int index,
	__in		unsigned int label,
	__in		efsys_mem_t *esmp,
	__in		size_t ndescs,
	__in		uint32_t id,
	__in		uint16_t flags,
	__in		efx_evq_t *eep,
	__in		efx_txq_t *etp,
	__out		unsigned int *addedp)
{
	efx_nic_cfg_t *encp = &(enp->en_nic_cfg);
	efx_oword_t oword;
	uint32_t size;
	uint16_t inner_csum;
	efx_rc_t rc;

	_NOTE(ARGUNUSED(esmp))

	EFX_STATIC_ASSERT(EFX_EV_TX_NLABELS ==
	    (1 << FRF_AZ_TX_DESCQ_LABEL_WIDTH));
	EFSYS_ASSERT3U(label, <, EFX_EV_TX_NLABELS);

	EFSYS_ASSERT(ISP2(encp->enc_txq_max_ndescs));
	EFX_STATIC_ASSERT(ISP2(EFX_TXQ_MINNDESCS));

	if (!ISP2(ndescs) ||
	    (ndescs < EFX_TXQ_MINNDESCS) || (ndescs > EFX_EVQ_MAXNEVS)) {
		rc = EINVAL;
		goto fail1;
	}
	if (index >= encp->enc_txq_limit) {
		rc = EINVAL;
		goto fail2;
	}
	for (size = 0;
	    (1 << size) <= (int)(encp->enc_txq_max_ndescs / EFX_TXQ_MINNDESCS);
	    size++)
		if ((1 << size) == (int)(ndescs / EFX_TXQ_MINNDESCS))
			break;
	if (id + (1 << size) >= encp->enc_buftbl_limit) {
		rc = EINVAL;
		goto fail3;
	}

	inner_csum = EFX_TXQ_CKSUM_INNER_IPV4 | EFX_TXQ_CKSUM_INNER_TCPUDP;
	if ((flags & inner_csum) != 0) {
		rc = EINVAL;
		goto fail4;
	}

	/* Set up the new descriptor queue */
	*addedp = 0;

	EFX_POPULATE_OWORD_6(oword,
	    FRF_AZ_TX_DESCQ_BUF_BASE_ID, id,
	    FRF_AZ_TX_DESCQ_EVQ_ID, eep->ee_index,
	    FRF_AZ_TX_DESCQ_OWNER_ID, 0,
	    FRF_AZ_TX_DESCQ_LABEL, label,
	    FRF_AZ_TX_DESCQ_SIZE, size,
	    FRF_AZ_TX_DESCQ_TYPE, 0);

	EFX_SET_OWORD_FIELD(oword, FRF_BZ_TX_NON_IP_DROP_DIS, 1);
	EFX_SET_OWORD_FIELD(oword, FRF_BZ_TX_IP_CHKSM_DIS,
	    (flags & EFX_TXQ_CKSUM_IPV4) ? 0 : 1);
	EFX_SET_OWORD_FIELD(oword, FRF_BZ_TX_TCP_CHKSM_DIS,
	    (flags & EFX_TXQ_CKSUM_TCPUDP) ? 0 : 1);

	EFX_BAR_TBL_WRITEO(enp, FR_AZ_TX_DESC_PTR_TBL,
	    etp->et_index, &oword, B_TRUE);

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

	__checkReturn		efx_rc_t
siena_tx_qdesc_post(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_desc_t *ed,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	unsigned int added = *addedp;
	unsigned int i;
	efx_rc_t rc;

	if (added - completed + ndescs > EFX_TXQ_LIMIT(etp->et_mask + 1)) {
		rc = ENOSPC;
		goto fail1;
	}

	for (i = 0; i < ndescs; i++) {
		efx_desc_t *edp = &ed[i];
		unsigned int id;
		size_t offset;

		id = added++ & etp->et_mask;
		offset = id * sizeof (efx_desc_t);

		EFSYS_MEM_WRITEQ(etp->et_esmp, offset, &edp->ed_eq);
	}

	EFSYS_PROBE3(tx_desc_post, unsigned int, etp->et_index,
		    unsigned int, added, unsigned int, ndescs);

	EFX_TX_QSTAT_INCR(etp, TX_POST);

	*addedp = added;
	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);
	return (rc);
}

	void
siena_tx_qdesc_dma_create(
	__in	efx_txq_t *etp,
	__in	efsys_dma_addr_t addr,
	__in	size_t size,
	__in	boolean_t eop,
	__out	efx_desc_t *edp)
{
	/*
	 * Fragments must not span 4k boundaries.
	 * Here it is a stricter requirement than the maximum length.
	 */
	EFSYS_ASSERT(P2ROUNDUP(addr + 1,
	    etp->et_enp->en_nic_cfg.enc_tx_dma_desc_boundary) >= addr + size);

	EFSYS_PROBE4(tx_desc_dma_create, unsigned int, etp->et_index,
		    efsys_dma_addr_t, addr,
		    size_t, size, boolean_t, eop);

	EFX_POPULATE_QWORD_4(edp->ed_eq,
			    FSF_AZ_TX_KER_CONT, eop ? 0 : 1,
			    FSF_AZ_TX_KER_BYTE_COUNT, (uint32_t)size,
			    FSF_AZ_TX_KER_BUF_ADDR_DW0,
			    (uint32_t)(addr & 0xffffffff),
			    FSF_AZ_TX_KER_BUF_ADDR_DW1,
			    (uint32_t)(addr >> 32));
}

#endif /* EFSYS_OPT_SIENA */

#if EFSYS_OPT_QSTATS
#if EFSYS_OPT_NAMES
/* START MKCONFIG GENERATED EfxTransmitQueueStatNamesBlock 2866874ecd7a363b */
static const char * const __efx_tx_qstat_name[] = {
	"post",
	"post_pio",
};
/* END MKCONFIG GENERATED EfxTransmitQueueStatNamesBlock */

		const char *
efx_tx_qstat_name(
	__in	efx_nic_t *enp,
	__in	unsigned int id)
{
	_NOTE(ARGUNUSED(enp))
	EFSYS_ASSERT3U(enp->en_magic, ==, EFX_NIC_MAGIC);
	EFSYS_ASSERT3U(id, <, TX_NQSTATS);

	return (__efx_tx_qstat_name[id]);
}
#endif	/* EFSYS_OPT_NAMES */
#endif /* EFSYS_OPT_QSTATS */

#if EFSYS_OPT_SIENA

#if EFSYS_OPT_QSTATS
static					void
siena_tx_qstats_update(
	__in				efx_txq_t *etp,
	__inout_ecount(TX_NQSTATS)	efsys_stat_t *stat)
{
	unsigned int id;

	for (id = 0; id < TX_NQSTATS; id++) {
		efsys_stat_t *essp = &stat[id];

		EFSYS_STAT_INCR(essp, etp->et_stat[id]);
		etp->et_stat[id] = 0;
	}
}
#endif	/* EFSYS_OPT_QSTATS */

static		void
siena_tx_qdestroy(
	__in	efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	efx_oword_t oword;

	/* Purge descriptor queue */
	EFX_ZERO_OWORD(oword);

	EFX_BAR_TBL_WRITEO(enp, FR_AZ_TX_DESC_PTR_TBL,
			    etp->et_index, &oword, B_TRUE);
}

static		void
siena_tx_fini(
	__in	efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
}

#endif /* EFSYS_OPT_SIENA */
