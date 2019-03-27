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

#if EFSYS_OPT_QSTATS
#define	EFX_TX_QSTAT_INCR(_etp, _stat)					\
	do {								\
		(_etp)->et_stat[_stat]++;				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)
#else
#define	EFX_TX_QSTAT_INCR(_etp, _stat)
#endif

static	__checkReturn	efx_rc_t
efx_mcdi_init_txq(
	__in		efx_nic_t *enp,
	__in		uint32_t ndescs,
	__in		uint32_t target_evq,
	__in		uint32_t label,
	__in		uint32_t instance,
	__in		uint16_t flags,
	__in		efsys_mem_t *esmp)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_INIT_TXQ_IN_LEN(EFX_TXQ_MAX_BUFS),
		MC_CMD_INIT_TXQ_OUT_LEN);
	efx_qword_t *dma_addr;
	uint64_t addr;
	int npages;
	int i;
	efx_rc_t rc;

	EFSYS_ASSERT(EFX_TXQ_MAX_BUFS >=
	    EFX_TXQ_NBUFS(enp->en_nic_cfg.enc_txq_max_ndescs));

	if ((esmp == NULL) || (EFSYS_MEM_SIZE(esmp) < EFX_TXQ_SIZE(ndescs))) {
		rc = EINVAL;
		goto fail1;
	}

	npages = EFX_TXQ_NBUFS(ndescs);
	if (MC_CMD_INIT_TXQ_IN_LEN(npages) > sizeof (payload)) {
		rc = EINVAL;
		goto fail2;
	}

	req.emr_cmd = MC_CMD_INIT_TXQ;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_INIT_TXQ_IN_LEN(npages);
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_INIT_TXQ_OUT_LEN;

	MCDI_IN_SET_DWORD(req, INIT_TXQ_IN_SIZE, ndescs);
	MCDI_IN_SET_DWORD(req, INIT_TXQ_IN_TARGET_EVQ, target_evq);
	MCDI_IN_SET_DWORD(req, INIT_TXQ_IN_LABEL, label);
	MCDI_IN_SET_DWORD(req, INIT_TXQ_IN_INSTANCE, instance);

	MCDI_IN_POPULATE_DWORD_9(req, INIT_TXQ_IN_FLAGS,
	    INIT_TXQ_IN_FLAG_BUFF_MODE, 0,
	    INIT_TXQ_IN_FLAG_IP_CSUM_DIS,
	    (flags & EFX_TXQ_CKSUM_IPV4) ? 0 : 1,
	    INIT_TXQ_IN_FLAG_TCP_CSUM_DIS,
	    (flags & EFX_TXQ_CKSUM_TCPUDP) ? 0 : 1,
	    INIT_TXQ_EXT_IN_FLAG_INNER_IP_CSUM_EN,
	    (flags & EFX_TXQ_CKSUM_INNER_IPV4) ? 1 : 0,
	    INIT_TXQ_EXT_IN_FLAG_INNER_TCP_CSUM_EN,
	    (flags & EFX_TXQ_CKSUM_INNER_TCPUDP) ? 1 : 0,
	    INIT_TXQ_EXT_IN_FLAG_TSOV2_EN, (flags & EFX_TXQ_FATSOV2) ? 1 : 0,
	    INIT_TXQ_IN_FLAG_TCP_UDP_ONLY, 0,
	    INIT_TXQ_IN_CRC_MODE, 0,
	    INIT_TXQ_IN_FLAG_TIMESTAMP, 0);

	MCDI_IN_SET_DWORD(req, INIT_TXQ_IN_OWNER_ID, 0);
	MCDI_IN_SET_DWORD(req, INIT_TXQ_IN_PORT_ID, EVB_PORT_ID_ASSIGNED);

	dma_addr = MCDI_IN2(req, efx_qword_t, INIT_TXQ_IN_DMA_ADDR);
	addr = EFSYS_MEM_ADDR(esmp);

	for (i = 0; i < npages; i++) {
		EFX_POPULATE_QWORD_2(*dma_addr,
		    EFX_DWORD_1, (uint32_t)(addr >> 32),
		    EFX_DWORD_0, (uint32_t)(addr & 0xffffffff));

		dma_addr++;
		addr += EFX_BUF_SIZE;
	}

	efx_mcdi_execute(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
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

static	__checkReturn	efx_rc_t
efx_mcdi_fini_txq(
	__in		efx_nic_t *enp,
	__in		uint32_t instance)
{
	efx_mcdi_req_t req;
	EFX_MCDI_DECLARE_BUF(payload, MC_CMD_FINI_TXQ_IN_LEN,
		MC_CMD_FINI_TXQ_OUT_LEN);
	efx_rc_t rc;

	req.emr_cmd = MC_CMD_FINI_TXQ;
	req.emr_in_buf = payload;
	req.emr_in_length = MC_CMD_FINI_TXQ_IN_LEN;
	req.emr_out_buf = payload;
	req.emr_out_length = MC_CMD_FINI_TXQ_OUT_LEN;

	MCDI_IN_SET_DWORD(req, FINI_TXQ_IN_INSTANCE, instance);

	efx_mcdi_execute_quiet(enp, &req);

	if (req.emr_rc != 0) {
		rc = req.emr_rc;
		goto fail1;
	}

	return (0);

fail1:
	/*
	 * EALREADY is not an error, but indicates that the MC has rebooted and
	 * that the TXQ has already been destroyed.
	 */
	if (rc != EALREADY)
		EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_tx_init(
	__in		efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
	return (0);
}

			void
ef10_tx_fini(
	__in		efx_nic_t *enp)
{
	_NOTE(ARGUNUSED(enp))
}

	__checkReturn	efx_rc_t
ef10_tx_qcreate(
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
	efx_nic_cfg_t *encp = &enp->en_nic_cfg;
	uint16_t inner_csum;
	efx_desc_t desc;
	efx_rc_t rc;

	_NOTE(ARGUNUSED(id))

	inner_csum = EFX_TXQ_CKSUM_INNER_IPV4 | EFX_TXQ_CKSUM_INNER_TCPUDP;
	if (((flags & inner_csum) != 0) &&
	    (encp->enc_tunnel_encapsulations_supported == 0)) {
		rc = EINVAL;
		goto fail1;
	}

	if ((rc = efx_mcdi_init_txq(enp, ndescs, eep->ee_index, label, index,
	    flags, esmp)) != 0)
		goto fail2;

	/*
	 * A previous user of this TX queue may have written a descriptor to the
	 * TX push collector, but not pushed the doorbell (e.g. after a crash).
	 * The next doorbell write would then push the stale descriptor.
	 *
	 * Ensure the (per network port) TX push collector is cleared by writing
	 * a no-op TX option descriptor. See bug29981 for details.
	 */
	*addedp = 1;
	ef10_tx_qdesc_checksum_create(etp, flags, &desc);

	EFSYS_MEM_WRITEQ(etp->et_esmp, 0, &desc.ed_eq);
	ef10_tx_qpush(etp, *addedp, 0);

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

		void
ef10_tx_qdestroy(
	__in	efx_txq_t *etp)
{
	/* FIXME */
	_NOTE(ARGUNUSED(etp))
	/* FIXME */
}

	__checkReturn	efx_rc_t
ef10_tx_qpio_enable(
	__in		efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	efx_piobuf_handle_t handle;
	efx_rc_t rc;

	if (etp->et_pio_size != 0) {
		rc = EALREADY;
		goto fail1;
	}

	/* Sub-allocate a PIO block from a piobuf */
	if ((rc = ef10_nic_pio_alloc(enp,
		    &etp->et_pio_bufnum,
		    &handle,
		    &etp->et_pio_blknum,
		    &etp->et_pio_offset,
		    &etp->et_pio_size)) != 0) {
		goto fail2;
	}
	EFSYS_ASSERT3U(etp->et_pio_size, !=, 0);

	/* Link the piobuf to this TXQ */
	if ((rc = ef10_nic_pio_link(enp, etp->et_index, handle)) != 0) {
		goto fail3;
	}

	/*
	 * et_pio_offset is the offset of the sub-allocated block within the
	 * hardware PIO buffer. It is used as the buffer address in the PIO
	 * option descriptor.
	 *
	 * et_pio_write_offset is the offset of the sub-allocated block from the
	 * start of the write-combined memory mapping, and is used for writing
	 * data into the PIO buffer.
	 */
	etp->et_pio_write_offset =
	    (etp->et_pio_bufnum * ER_DZ_TX_PIOBUF_STEP) +
	    ER_DZ_TX_PIOBUF_OFST + etp->et_pio_offset;

	return (0);

fail3:
	EFSYS_PROBE(fail3);
	(void) ef10_nic_pio_free(enp, etp->et_pio_bufnum, etp->et_pio_blknum);
fail2:
	EFSYS_PROBE(fail2);
	etp->et_pio_size = 0;
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
ef10_tx_qpio_disable(
	__in		efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;

	if (etp->et_pio_size != 0) {
		/* Unlink the piobuf from this TXQ */
		if (ef10_nic_pio_unlink(enp, etp->et_index) != 0)
			return;

		/* Free the sub-allocated PIO block */
		(void) ef10_nic_pio_free(enp, etp->et_pio_bufnum,
		    etp->et_pio_blknum);
		etp->et_pio_size = 0;
		etp->et_pio_write_offset = 0;
	}
}

	__checkReturn	efx_rc_t
ef10_tx_qpio_write(
	__in			efx_txq_t *etp,
	__in_ecount(length)	uint8_t *buffer,
	__in			size_t length,
	__in			size_t offset)
{
	efx_nic_t *enp = etp->et_enp;
	efsys_bar_t *esbp = enp->en_esbp;
	uint32_t write_offset;
	uint32_t write_offset_limit;
	efx_qword_t *eqp;
	efx_rc_t rc;

	EFSYS_ASSERT(length % sizeof (efx_qword_t) == 0);

	if (etp->et_pio_size == 0) {
		rc = ENOENT;
		goto fail1;
	}
	if (offset + length > etp->et_pio_size)	{
		rc = ENOSPC;
		goto fail2;
	}

	/*
	 * Writes to PIO buffers must be 64 bit aligned, and multiples of
	 * 64 bits.
	 */
	write_offset = etp->et_pio_write_offset + offset;
	write_offset_limit = write_offset + length;
	eqp = (efx_qword_t *)buffer;
	while (write_offset < write_offset_limit) {
		EFSYS_BAR_WC_WRITEQ(esbp, write_offset, eqp);
		eqp++;
		write_offset += sizeof (efx_qword_t);
	}

	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_tx_qpio_post(
	__in			efx_txq_t *etp,
	__in			size_t pkt_length,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	efx_qword_t pio_desc;
	unsigned int id;
	size_t offset;
	unsigned int added = *addedp;
	efx_rc_t rc;


	if (added - completed + 1 > EFX_TXQ_LIMIT(etp->et_mask + 1)) {
		rc = ENOSPC;
		goto fail1;
	}

	if (etp->et_pio_size == 0) {
		rc = ENOENT;
		goto fail2;
	}

	id = added++ & etp->et_mask;
	offset = id * sizeof (efx_qword_t);

	EFSYS_PROBE4(tx_pio_post, unsigned int, etp->et_index,
		    unsigned int, id, uint32_t, etp->et_pio_offset,
		    size_t, pkt_length);

	EFX_POPULATE_QWORD_5(pio_desc,
			ESF_DZ_TX_DESC_IS_OPT, 1,
			ESF_DZ_TX_OPTION_TYPE, 1,
			ESF_DZ_TX_PIO_CONT, 0,
			ESF_DZ_TX_PIO_BYTE_CNT, pkt_length,
			ESF_DZ_TX_PIO_BUF_ADDR, etp->et_pio_offset);

	EFSYS_MEM_WRITEQ(etp->et_esmp, offset, &pio_desc);

	EFX_TX_QSTAT_INCR(etp, TX_POST_PIO);

	*addedp = added;
	return (0);

fail2:
	EFSYS_PROBE(fail2);
fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn		efx_rc_t
ef10_tx_qpost(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_buffer_t *eb,
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
		efx_buffer_t *ebp = &eb[i];
		efsys_dma_addr_t addr = ebp->eb_addr;
		size_t size = ebp->eb_size;
		boolean_t eop = ebp->eb_eop;
		unsigned int id;
		size_t offset;
		efx_qword_t qword;

		/* No limitations on boundary crossing */
		EFSYS_ASSERT(size <=
		    etp->et_enp->en_nic_cfg.enc_tx_dma_desc_size_max);

		id = added++ & etp->et_mask;
		offset = id * sizeof (efx_qword_t);

		EFSYS_PROBE5(tx_post, unsigned int, etp->et_index,
		    unsigned int, id, efsys_dma_addr_t, addr,
		    size_t, size, boolean_t, eop);

		EFX_POPULATE_QWORD_5(qword,
		    ESF_DZ_TX_KER_TYPE, 0,
		    ESF_DZ_TX_KER_CONT, (eop) ? 0 : 1,
		    ESF_DZ_TX_KER_BYTE_CNT, (uint32_t)(size),
		    ESF_DZ_TX_KER_BUF_ADDR_DW0, (uint32_t)(addr & 0xffffffff),
		    ESF_DZ_TX_KER_BUF_ADDR_DW1, (uint32_t)(addr >> 32));

		EFSYS_MEM_WRITEQ(etp->et_esmp, offset, &qword);
	}

	EFX_TX_QSTAT_INCR(etp, TX_POST);

	*addedp = added;
	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

/*
 * This improves performance by, when possible, pushing a TX descriptor at the
 * same time as the doorbell. The descriptor must be added to the TXQ, so that
 * can be used if the hardware decides not to use the pushed descriptor.
 */
			void
ef10_tx_qpush(
	__in		efx_txq_t *etp,
	__in		unsigned int added,
	__in		unsigned int pushed)
{
	efx_nic_t *enp = etp->et_enp;
	unsigned int wptr;
	unsigned int id;
	size_t offset;
	efx_qword_t desc;
	efx_oword_t oword;

	wptr = added & etp->et_mask;
	id = pushed & etp->et_mask;
	offset = id * sizeof (efx_qword_t);

	EFSYS_MEM_READQ(etp->et_esmp, offset, &desc);

	/*
	 * SF Bug 65776: TSO option descriptors cannot be pushed if pacer bypass
	 * is enabled on the event queue this transmit queue is attached to.
	 *
	 * To ensure the code is safe, it is easiest to simply test the type of
	 * the descriptor to push, and only push it is if it not a TSO option
	 * descriptor.
	 */
	if ((EFX_QWORD_FIELD(desc, ESF_DZ_TX_DESC_IS_OPT) != 1) ||
	    (EFX_QWORD_FIELD(desc, ESF_DZ_TX_OPTION_TYPE) !=
	    ESE_DZ_TX_OPTION_DESC_TSO)) {
		/* Push the descriptor and update the wptr. */
		EFX_POPULATE_OWORD_3(oword, ERF_DZ_TX_DESC_WPTR, wptr,
		    ERF_DZ_TX_DESC_HWORD, EFX_QWORD_FIELD(desc, EFX_DWORD_1),
		    ERF_DZ_TX_DESC_LWORD, EFX_QWORD_FIELD(desc, EFX_DWORD_0));

		/* Ensure ordering of memory (descriptors) and PIO (doorbell) */
		EFX_DMA_SYNC_QUEUE_FOR_DEVICE(etp->et_esmp, etp->et_mask + 1,
					    wptr, id);
		EFSYS_PIO_WRITE_BARRIER();
		EFX_BAR_VI_DOORBELL_WRITEO(enp, ER_DZ_TX_DESC_UPD_REG,
		    etp->et_index, &oword);
	} else {
		efx_dword_t dword;

		/*
		 * Only update the wptr. This is signalled to the hardware by
		 * only writing one DWORD of the doorbell register.
		 */
		EFX_POPULATE_OWORD_1(oword, ERF_DZ_TX_DESC_WPTR, wptr);
		dword = oword.eo_dword[2];

		/* Ensure ordering of memory (descriptors) and PIO (doorbell) */
		EFX_DMA_SYNC_QUEUE_FOR_DEVICE(etp->et_esmp, etp->et_mask + 1,
					    wptr, id);
		EFSYS_PIO_WRITE_BARRIER();
		EFX_BAR_VI_WRITED2(enp, ER_DZ_TX_DESC_UPD_REG,
		    etp->et_index, &dword, B_FALSE);
	}
}

	__checkReturn		efx_rc_t
ef10_tx_qdesc_post(
	__in			efx_txq_t *etp,
	__in_ecount(ndescs)	efx_desc_t *ed,
	__in			unsigned int ndescs,
	__in			unsigned int completed,
	__inout			unsigned int *addedp)
{
	unsigned int added = *addedp;
	unsigned int i;

	if (added - completed + ndescs > EFX_TXQ_LIMIT(etp->et_mask + 1))
		return (ENOSPC);

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
}

	void
ef10_tx_qdesc_dma_create(
	__in	efx_txq_t *etp,
	__in	efsys_dma_addr_t addr,
	__in	size_t size,
	__in	boolean_t eop,
	__out	efx_desc_t *edp)
{
	_NOTE(ARGUNUSED(etp))

	/* No limitations on boundary crossing */
	EFSYS_ASSERT(size <= etp->et_enp->en_nic_cfg.enc_tx_dma_desc_size_max);

	EFSYS_PROBE4(tx_desc_dma_create, unsigned int, etp->et_index,
		    efsys_dma_addr_t, addr,
		    size_t, size, boolean_t, eop);

	EFX_POPULATE_QWORD_5(edp->ed_eq,
		    ESF_DZ_TX_KER_TYPE, 0,
		    ESF_DZ_TX_KER_CONT, (eop) ? 0 : 1,
		    ESF_DZ_TX_KER_BYTE_CNT, (uint32_t)(size),
		    ESF_DZ_TX_KER_BUF_ADDR_DW0, (uint32_t)(addr & 0xffffffff),
		    ESF_DZ_TX_KER_BUF_ADDR_DW1, (uint32_t)(addr >> 32));
}

	void
ef10_tx_qdesc_tso_create(
	__in	efx_txq_t *etp,
	__in	uint16_t ipv4_id,
	__in	uint32_t tcp_seq,
	__in	uint8_t  tcp_flags,
	__out	efx_desc_t *edp)
{
	_NOTE(ARGUNUSED(etp))

	EFSYS_PROBE4(tx_desc_tso_create, unsigned int, etp->et_index,
		    uint16_t, ipv4_id, uint32_t, tcp_seq,
		    uint8_t, tcp_flags);

	EFX_POPULATE_QWORD_5(edp->ed_eq,
			    ESF_DZ_TX_DESC_IS_OPT, 1,
			    ESF_DZ_TX_OPTION_TYPE,
			    ESE_DZ_TX_OPTION_DESC_TSO,
			    ESF_DZ_TX_TSO_TCP_FLAGS, tcp_flags,
			    ESF_DZ_TX_TSO_IP_ID, ipv4_id,
			    ESF_DZ_TX_TSO_TCP_SEQNO, tcp_seq);
}

	void
ef10_tx_qdesc_tso2_create(
	__in			efx_txq_t *etp,
	__in			uint16_t ipv4_id,
	__in			uint16_t outer_ipv4_id,
	__in			uint32_t tcp_seq,
	__in			uint16_t tcp_mss,
	__out_ecount(count)	efx_desc_t *edp,
	__in			int count)
{
	_NOTE(ARGUNUSED(etp, count))

	EFSYS_PROBE4(tx_desc_tso2_create, unsigned int, etp->et_index,
		    uint16_t, ipv4_id, uint32_t, tcp_seq,
		    uint16_t, tcp_mss);

	EFSYS_ASSERT(count >= EFX_TX_FATSOV2_OPT_NDESCS);

	EFX_POPULATE_QWORD_5(edp[0].ed_eq,
			    ESF_DZ_TX_DESC_IS_OPT, 1,
			    ESF_DZ_TX_OPTION_TYPE,
			    ESE_DZ_TX_OPTION_DESC_TSO,
			    ESF_DZ_TX_TSO_OPTION_TYPE,
			    ESE_DZ_TX_TSO_OPTION_DESC_FATSO2A,
			    ESF_DZ_TX_TSO_IP_ID, ipv4_id,
			    ESF_DZ_TX_TSO_TCP_SEQNO, tcp_seq);
	EFX_POPULATE_QWORD_5(edp[1].ed_eq,
			    ESF_DZ_TX_DESC_IS_OPT, 1,
			    ESF_DZ_TX_OPTION_TYPE,
			    ESE_DZ_TX_OPTION_DESC_TSO,
			    ESF_DZ_TX_TSO_OPTION_TYPE,
			    ESE_DZ_TX_TSO_OPTION_DESC_FATSO2B,
			    ESF_DZ_TX_TSO_TCP_MSS, tcp_mss,
			    ESF_DZ_TX_TSO_OUTER_IPID, outer_ipv4_id);
}

	void
ef10_tx_qdesc_vlantci_create(
	__in	efx_txq_t *etp,
	__in	uint16_t  tci,
	__out	efx_desc_t *edp)
{
	_NOTE(ARGUNUSED(etp))

	EFSYS_PROBE2(tx_desc_vlantci_create, unsigned int, etp->et_index,
		    uint16_t, tci);

	EFX_POPULATE_QWORD_4(edp->ed_eq,
			    ESF_DZ_TX_DESC_IS_OPT, 1,
			    ESF_DZ_TX_OPTION_TYPE,
			    ESE_DZ_TX_OPTION_DESC_VLAN,
			    ESF_DZ_TX_VLAN_OP, tci ? 1 : 0,
			    ESF_DZ_TX_VLAN_TAG1, tci);
}

	void
ef10_tx_qdesc_checksum_create(
	__in	efx_txq_t *etp,
	__in	uint16_t flags,
	__out	efx_desc_t *edp)
{
	_NOTE(ARGUNUSED(etp));

	EFSYS_PROBE2(tx_desc_checksum_create, unsigned int, etp->et_index,
		    uint32_t, flags);

	EFX_POPULATE_QWORD_6(edp->ed_eq,
	    ESF_DZ_TX_DESC_IS_OPT, 1,
	    ESF_DZ_TX_OPTION_TYPE, ESE_DZ_TX_OPTION_DESC_CRC_CSUM,
	    ESF_DZ_TX_OPTION_UDP_TCP_CSUM,
	    (flags & EFX_TXQ_CKSUM_TCPUDP) ? 1 : 0,
	    ESF_DZ_TX_OPTION_IP_CSUM,
	    (flags & EFX_TXQ_CKSUM_IPV4) ? 1 : 0,
	    ESF_DZ_TX_OPTION_INNER_UDP_TCP_CSUM,
	    (flags & EFX_TXQ_CKSUM_INNER_TCPUDP) ? 1 : 0,
	    ESF_DZ_TX_OPTION_INNER_IP_CSUM,
	    (flags & EFX_TXQ_CKSUM_INNER_IPV4) ? 1 : 0);
}


	__checkReturn	efx_rc_t
ef10_tx_qpace(
	__in		efx_txq_t *etp,
	__in		unsigned int ns)
{
	efx_rc_t rc;

	/* FIXME */
	_NOTE(ARGUNUSED(etp, ns))
	_NOTE(CONSTANTCONDITION)
	if (B_FALSE) {
		rc = ENOTSUP;
		goto fail1;
	}
	/* FIXME */

	return (0);

fail1:
	/*
	 * EALREADY is not an error, but indicates that the MC has rebooted and
	 * that the TXQ has already been destroyed. Callers need to know that
	 * the TXQ flush has completed to avoid waiting until timeout for a
	 * flush done event that will not be delivered.
	 */
	if (rc != EALREADY)
		EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

	__checkReturn	efx_rc_t
ef10_tx_qflush(
	__in		efx_txq_t *etp)
{
	efx_nic_t *enp = etp->et_enp;
	efx_rc_t rc;

	if ((rc = efx_mcdi_fini_txq(enp, etp->et_index)) != 0)
		goto fail1;

	return (0);

fail1:
	EFSYS_PROBE1(fail1, efx_rc_t, rc);

	return (rc);
}

			void
ef10_tx_qenable(
	__in		efx_txq_t *etp)
{
	/* FIXME */
	_NOTE(ARGUNUSED(etp))
	/* FIXME */
}

#if EFSYS_OPT_QSTATS
			void
ef10_tx_qstats_update(
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

#endif /* EFSYS_OPT_QSTATS */

#endif /* EFSYS_OPT_HUNTINGTON || EFSYS_OPT_MEDFORD || EFSYS_OPT_MEDFORD2 */
