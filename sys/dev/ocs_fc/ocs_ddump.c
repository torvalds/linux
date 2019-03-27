/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/**
 * @file
 * generate FC ddump
 *
 */

#include "ocs.h"
#include "ocs_ddump.h"

#define DEFAULT_SAVED_DUMP_SIZE		(4*1024*1024)

void hw_queue_ddump(ocs_textbuf_t *textbuf, ocs_hw_t *hw);

/**
 * @brief Generate sli4 queue ddump
 *
 * Generates sli4 queue ddump data
 *
 * @param textbuf pointer to text buffer
 * @param name name of SLI4 queue
 * @param hw pointer HW context
 * @param q pointer to SLI4 queues array
 * @param q_count count of SLI4 queues
 * @param qentries number of SLI4 queue entries to dump
 *
 * @return none
 */

static void
ocs_ddump_sli4_queue(ocs_textbuf_t *textbuf, const char *name, ocs_hw_t *hw, sli4_queue_t *q, uint32_t q_count, uint32_t qentries)
{
	uint32_t i;

	for (i = 0; i < q_count; i ++, q ++) {
		ocs_ddump_section(textbuf, name, i);
		ocs_ddump_value(textbuf, "index", "%d", q->index);
		ocs_ddump_value(textbuf, "size", "%d", q->size);
		ocs_ddump_value(textbuf, "length", "%d", q->length);
		ocs_ddump_value(textbuf, "n_posted", "%d", q->n_posted);
		ocs_ddump_value(textbuf, "id", "%d", q->id);
		ocs_ddump_value(textbuf, "type", "%d", q->type);
		ocs_ddump_value(textbuf, "proc_limit", "%d", q->proc_limit);
		ocs_ddump_value(textbuf, "posted_limit", "%d", q->posted_limit);
		ocs_ddump_value(textbuf, "max_num_processed", "%d", q->max_num_processed);
		ocs_ddump_value(textbuf, "max_process_time", "%ld", (unsigned long)q->max_process_time);
		ocs_ddump_value(textbuf, "virt_addr", "%p", q->dma.virt);
		ocs_ddump_value(textbuf, "phys_addr", "%lx", (unsigned long)q->dma.phys);

		/* queue-specific information */
		switch (q->type) {
		case SLI_QTYPE_MQ:
			ocs_ddump_value(textbuf, "r_idx", "%d", q->u.r_idx);
			break;
		case SLI_QTYPE_CQ:
			ocs_ddump_value(textbuf, "is_mq", "%d", q->u.flag.is_mq);
			break;
		case SLI_QTYPE_WQ:
			break;
		case SLI_QTYPE_RQ: {
			uint32_t i;
			uint32_t j;
			uint32_t rqe_count = 0;
			hw_rq_t *rq;

			ocs_ddump_value(textbuf, "is_hdr", "%d", q->u.flag.is_hdr);
			ocs_ddump_value(textbuf, "rq_batch", "%d", q->u.flag.rq_batch);

			/* loop through RQ tracker to see how many RQEs were produced */
			for (i = 0; i < hw->hw_rq_count; i++) {
				rq = hw->hw_rq[i];
				for (j = 0; j < rq->entry_count; j++) {
					if (rq->rq_tracker[j] != NULL) {
						rqe_count++;
					}
				}
			}
			ocs_ddump_value(textbuf, "rqes_produced", "%d", rqe_count);
			break;
		}
		}
		ocs_ddump_queue_entries(textbuf, q->dma.virt, q->size, q->length,
					((q->type == SLI_QTYPE_MQ) ? q->u.r_idx : q->index),
					qentries);
		ocs_ddump_endsection(textbuf, name, i);
	}
}


/**
 * @brief Generate SLI4 ddump
 *
 * Generates sli4 ddump
 *
 * @param textbuf pointer to text buffer
 * @param sli4 pointer SLI context
 * @param qtype SLI4 queue type
 *
 * @return none
 */

static void
ocs_ddump_sli_q_fields(ocs_textbuf_t *textbuf, sli4_t *sli4, sli4_qtype_e qtype)
{
	char * q_desc;

	switch(qtype) {
	case SLI_QTYPE_EQ: q_desc = "EQ"; break;
	case SLI_QTYPE_CQ: q_desc = "CQ"; break;
	case SLI_QTYPE_MQ: q_desc = "MQ"; break;
	case SLI_QTYPE_WQ: q_desc = "WQ"; break;
	case SLI_QTYPE_RQ: q_desc = "RQ"; break;
	default: q_desc = "unknown"; break;
	}

	ocs_ddump_section(textbuf, q_desc, qtype);

	ocs_ddump_value(textbuf, "max_qcount", "%d", sli4->config.max_qcount[qtype]);
	ocs_ddump_value(textbuf, "max_qentries", "%d", sli4->config.max_qentries[qtype]);
	ocs_ddump_value(textbuf, "qpage_count", "%d", sli4->config.qpage_count[qtype]);
	ocs_ddump_endsection(textbuf, q_desc, qtype);
}


/**
 * @brief Generate SLI4 ddump
 *
 * Generates sli4 ddump
 *
 * @param textbuf pointer to text buffer
 * @param sli4 pointer SLI context
 *
 * @return none
 */

static void
ocs_ddump_sli(ocs_textbuf_t *textbuf, sli4_t *sli4)
{
	sli4_sgl_chaining_params_t *cparams = &sli4->config.sgl_chaining_params;
	const char *p;

	ocs_ddump_section(textbuf, "sli4", 0);

	ocs_ddump_value(textbuf, "sli_rev", "%d", sli4->sli_rev);
	ocs_ddump_value(textbuf, "sli_family", "%d", sli4->sli_family);
	ocs_ddump_value(textbuf, "if_type", "%d", sli4->if_type);

	switch(sli4->asic_type) {
	case SLI4_ASIC_TYPE_BE3:	p = "BE3"; break;
	case SLI4_ASIC_TYPE_SKYHAWK:	p = "Skyhawk"; break;
	case SLI4_ASIC_TYPE_LANCER:	p = "Lancer"; break;
	case SLI4_ASIC_TYPE_LANCERG6:	p = "LancerG6"; break;
	default:			p = "unknown"; break;
	}
	ocs_ddump_value(textbuf, "asic_type", "%s", p);

	switch(sli4->asic_rev) {
	case SLI4_ASIC_REV_FPGA:	p = "fpga"; break;
	case SLI4_ASIC_REV_A0:		p = "A0"; break;
	case SLI4_ASIC_REV_A1:		p = "A1"; break;
	case SLI4_ASIC_REV_A2:		p = "A2"; break;
	case SLI4_ASIC_REV_A3:		p = "A3"; break;
	case SLI4_ASIC_REV_B0:		p = "B0"; break;
	case SLI4_ASIC_REV_C0:		p = "C0"; break;
	case SLI4_ASIC_REV_D0:		p = "D0"; break;
	default:			p = "unknown"; break;
	}
	ocs_ddump_value(textbuf, "asic_rev", "%s", p);

	ocs_ddump_value(textbuf, "e_d_tov", "%d", sli4->config.e_d_tov);
	ocs_ddump_value(textbuf, "r_a_tov", "%d", sli4->config.r_a_tov);
	ocs_ddump_value(textbuf, "link_module_type", "%d", sli4->config.link_module_type);
	ocs_ddump_value(textbuf, "rq_batch", "%d", sli4->config.rq_batch);
	ocs_ddump_value(textbuf, "topology", "%d", sli4->config.topology);
	ocs_ddump_value(textbuf, "wwpn", "%02x%02x%02x%02x%02x%02x%02x%02x",
			 sli4->config.wwpn[0],
			 sli4->config.wwpn[1],
			 sli4->config.wwpn[2],
			 sli4->config.wwpn[3],
			 sli4->config.wwpn[4],
			 sli4->config.wwpn[5],
			 sli4->config.wwpn[6],
			 sli4->config.wwpn[7]);
	ocs_ddump_value(textbuf, "wwnn", "%02x%02x%02x%02x%02x%02x%02x%02x",
			 sli4->config.wwnn[0],
			 sli4->config.wwnn[1],
			 sli4->config.wwnn[2],
			 sli4->config.wwnn[3],
			 sli4->config.wwnn[4],
			 sli4->config.wwnn[5],
			 sli4->config.wwnn[6],
			 sli4->config.wwnn[7]);
	ocs_ddump_value(textbuf, "fw_rev0", "%d", sli4->config.fw_rev[0]);
	ocs_ddump_value(textbuf, "fw_rev1", "%d", sli4->config.fw_rev[1]);
	ocs_ddump_value(textbuf, "fw_name0", "%s", (char*)sli4->config.fw_name[0]);
	ocs_ddump_value(textbuf, "fw_name1", "%s", (char*)sli4->config.fw_name[1]);
	ocs_ddump_value(textbuf, "hw_rev0", "%x", sli4->config.hw_rev[0]);
	ocs_ddump_value(textbuf, "hw_rev1", "%x", sli4->config.hw_rev[1]);
	ocs_ddump_value(textbuf, "hw_rev2", "%x", sli4->config.hw_rev[2]);
	ocs_ddump_value(textbuf, "sge_supported_length", "%x", sli4->config.sge_supported_length);
	ocs_ddump_value(textbuf, "sgl_page_sizes", "%x", sli4->config.sgl_page_sizes);
	ocs_ddump_value(textbuf, "max_sgl_pages", "%x", sli4->config.max_sgl_pages);
	ocs_ddump_value(textbuf, "high_login_mode", "%x", sli4->config.high_login_mode);
	ocs_ddump_value(textbuf, "sgl_pre_registered", "%x", sli4->config.sgl_pre_registered);
	ocs_ddump_value(textbuf, "sgl_pre_registration_required", "%x", sli4->config.sgl_pre_registration_required);

	ocs_ddump_value(textbuf, "sgl_chaining_capable", "%x", cparams->chaining_capable);
	ocs_ddump_value(textbuf, "frag_num_field_offset", "%x", cparams->frag_num_field_offset);
	ocs_ddump_value(textbuf, "frag_num_field_mask", "%016llx", (unsigned long long)cparams->frag_num_field_mask);
	ocs_ddump_value(textbuf, "sgl_index_field_offset", "%x", cparams->sgl_index_field_offset);
	ocs_ddump_value(textbuf, "sgl_index_field_mask", "%016llx", (unsigned long long)cparams->sgl_index_field_mask);
	ocs_ddump_value(textbuf, "chain_sge_initial_value_lo", "%x", cparams->chain_sge_initial_value_lo);
	ocs_ddump_value(textbuf, "chain_sge_initial_value_hi", "%x", cparams->chain_sge_initial_value_hi);

	ocs_ddump_value(textbuf, "max_vfi", "%d", sli_get_max_rsrc(sli4, SLI_RSRC_FCOE_VFI));
	ocs_ddump_value(textbuf, "max_vpi", "%d", sli_get_max_rsrc(sli4, SLI_RSRC_FCOE_VPI));
	ocs_ddump_value(textbuf, "max_rpi", "%d", sli_get_max_rsrc(sli4, SLI_RSRC_FCOE_RPI));
	ocs_ddump_value(textbuf, "max_xri", "%d", sli_get_max_rsrc(sli4, SLI_RSRC_FCOE_XRI));
	ocs_ddump_value(textbuf, "max_fcfi", "%d", sli_get_max_rsrc(sli4, SLI_RSRC_FCOE_FCFI));

	ocs_ddump_sli_q_fields(textbuf, sli4, SLI_QTYPE_EQ);
	ocs_ddump_sli_q_fields(textbuf, sli4, SLI_QTYPE_CQ);
	ocs_ddump_sli_q_fields(textbuf, sli4, SLI_QTYPE_MQ);
	ocs_ddump_sli_q_fields(textbuf, sli4, SLI_QTYPE_WQ);
	ocs_ddump_sli_q_fields(textbuf, sli4, SLI_QTYPE_RQ);

	ocs_ddump_endsection(textbuf, "sli4", 0);
}


/**
 * @brief Dump HW IO
 *
 * Dump HW IO
 *
 * @param textbuf pointer to text buffer
 * @param io pointer to HW IO object
 *
 * @return none
 */

static void
ocs_ddump_hw_io(ocs_textbuf_t *textbuf, ocs_hw_io_t *io)
{
	ocs_assert(io);

	ocs_ddump_section(textbuf, "hw_io", io->indicator);

	ocs_ddump_value(textbuf, "state", "%d", io->state);
	ocs_ddump_value(textbuf, "xri", "0x%x", io->indicator);
	ocs_ddump_value(textbuf, "tag", "0x%x", io->reqtag);
	ocs_ddump_value(textbuf, "abort_reqtag", "0x%x", io->abort_reqtag);
	ocs_ddump_value(textbuf, "ref_count", "%d", ocs_ref_read_count(&io->ref));

	/* just to make it obvious, display abort bit from tag */
	ocs_ddump_value(textbuf, "abort", "0x%x", io->abort_in_progress);
	ocs_ddump_value(textbuf, "wq_index", "%d", (io->wq == NULL ? 0xffff : io->wq->instance));
	ocs_ddump_value(textbuf, "type", "%d", io->type);
	ocs_ddump_value(textbuf, "xbusy", "%d", io->xbusy);
	ocs_ddump_value(textbuf, "active_wqe_link", "%d", ocs_list_on_list(&io->wqe_link));
	ocs_ddump_value(textbuf, "def_sgl_count", "%d", io->def_sgl_count);
	ocs_ddump_value(textbuf, "n_sge", "%d", io->n_sge);
	ocs_ddump_value(textbuf, "has_ovfl_sgl", "%s", (io->ovfl_sgl != NULL ? "TRUE" : "FALSE"));
	ocs_ddump_value(textbuf, "has_ovfl_io", "%s", (io->ovfl_io != NULL ? "TRUE" : "FALSE"));

	ocs_ddump_endsection(textbuf, "hw_io", io->indicator);
}

#if defined(OCS_DEBUG_QUEUE_HISTORY)

/**
 * @brief Generate queue history ddump
 *
 * @param textbuf pointer to text buffer
 * @param q_hist Pointer to queue history object.
 */
static void
ocs_ddump_queue_history(ocs_textbuf_t *textbuf, ocs_hw_q_hist_t *q_hist)
{
	uint32_t x;

	ocs_ddump_section(textbuf, "q_hist", 0);
	ocs_ddump_value(textbuf, "count", "%ld", OCS_Q_HIST_SIZE);
	ocs_ddump_value(textbuf, "index", "%d", q_hist->q_hist_index);

	if (q_hist->q_hist == NULL) {
		ocs_ddump_section(textbuf, "history", 0);
		ocs_textbuf_printf(textbuf, "No history available\n");
		ocs_ddump_endsection(textbuf, "history", 0);
		ocs_ddump_endsection(textbuf, "q_hist", 0);
		return;
	}

	/* start from last entry and go backwards */
	ocs_textbuf_printf(textbuf, "<history>\n");
	ocs_textbuf_printf(textbuf, "(newest first):\n");

	ocs_lock(&q_hist->q_hist_lock);
	x = ocs_queue_history_prev_index(q_hist->q_hist_index);
	do {
		int i;
		ocs_q_hist_ftr_t ftr;
		uint32_t mask;


		/* footer's mask indicates what words were captured */
		ftr.word = q_hist->q_hist[x];
		mask = ftr.s.mask;
		i = 0;

		/* if we've encountered a mask of 0, must be done */
		if (mask == 0) {
			break;
		}

		/* display entry type */
		ocs_textbuf_printf(textbuf, "%s:\n",
				   ocs_queue_history_type_name(ftr.s.type));

		if (ocs_queue_history_timestamp_enabled()) {
			uint64_t tsc_value;
			x = ocs_queue_history_prev_index(x);
			tsc_value = ((q_hist->q_hist[x]) & 0x00000000FFFFFFFFull);
			x = ocs_queue_history_prev_index(x);
			tsc_value |= (((uint64_t)q_hist->q_hist[x] << 32) & 0xFFFFFFFF00000000ull);
			ocs_textbuf_printf(textbuf, " t: %" PRIu64 "\n", tsc_value);
		}

		if (ocs_queue_history_q_info_enabled()) {
			if (ftr.s.type == OCS_Q_HIST_TYPE_CWQE ||
			    ftr.s.type == OCS_Q_HIST_TYPE_CXABT ||
			    ftr.s.type == OCS_Q_HIST_TYPE_WQE) {
				x = ocs_queue_history_prev_index(x);
				ocs_textbuf_printf(textbuf, " qid=0x%x idx=0x%x\n",
						   ((q_hist->q_hist[x] >> 16) & 0xFFFF),
						   ((q_hist->q_hist[x] >> 0) & 0xFFFF));
			}
		}

		while (mask) {
			if ((mask & 1) && (x != q_hist->q_hist_index)){
				/* get next word */
				x = ocs_queue_history_prev_index(x);
				ocs_textbuf_printf(textbuf, " [%d]=%x\n",
						   i, q_hist->q_hist[x]);
			}
			mask = (mask >> 1UL);
			i++;
		}

		/* go backwards to next element */
		x = ocs_queue_history_prev_index(x);
	} while (x != ocs_queue_history_prev_index(q_hist->q_hist_index));
	ocs_unlock(&q_hist->q_hist_lock);

	ocs_textbuf_printf(textbuf, "</history>\n");
	ocs_ddump_endsection(textbuf, "q_hist", 0);
}
#endif

/**
 * @brief Generate hw ddump
 *
 * Generates hw ddump
 *
 * @param textbuf pointer to text buffer
 * @param hw pointer HW context
 * @param flags ddump flags
 * @param qentries number of qentries to dump
 *
 * @return none
 */

static void
ocs_ddump_hw(ocs_textbuf_t *textbuf, ocs_hw_t *hw, uint32_t flags, uint32_t qentries)
{
	ocs_t *ocs = hw->os;
	uint32_t cnt = 0;
	ocs_hw_io_t *io = NULL;
	uint32_t i;
	uint32_t j;
	uint32_t max_rpi = sli_get_max_rsrc(&hw->sli, SLI_RSRC_FCOE_RPI);

	ocs_assert(ocs);

	ocs_ddump_section(textbuf, "hw", ocs->instance_index);

	/* device specific information */
	switch(hw->sli.if_type) {
	case 0:
		ocs_ddump_value(textbuf, "uerr_mask_hi", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_UERR_MASK_HI));
		ocs_ddump_value(textbuf, "uerr_mask_lo", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_UERR_MASK_LO));
		ocs_ddump_value(textbuf, "uerr_status_hi", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_UERR_STATUS_HI));
		ocs_ddump_value(textbuf, "uerr_status_lo", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_UERR_STATUS_LO));
		break;
	case 2:
		ocs_ddump_value(textbuf, "sliport_status", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_SLIPORT_STATUS));
		ocs_ddump_value(textbuf, "sliport_error1", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_SLIPORT_ERROR1));
		ocs_ddump_value(textbuf, "sliport_error2", "%08x",
				 sli_reg_read(&hw->sli, SLI4_REG_SLIPORT_ERROR2));
		break;
	}

	ocs_ddump_value(textbuf, "link_status", "%d", hw->link.status);
	ocs_ddump_value(textbuf, "link_speed", "%d", hw->link.speed);
	ocs_ddump_value(textbuf, "link_topology", "%d", hw->link.topology);
	ocs_ddump_value(textbuf, "state", "%d", hw->state);
	ocs_ddump_value(textbuf, "io_alloc_failed_count", "%d", ocs_atomic_read(&hw->io_alloc_failed_count));
	ocs_ddump_value(textbuf, "n_io", "%d", hw->config.n_io);

	ocs_ddump_value(textbuf, "queue_topology", "%s", hw->config.queue_topology);
	ocs_ddump_value(textbuf, "rq_selection_policy", "%d", hw->config.rq_selection_policy);
	ocs_ddump_value(textbuf, "rr_quanta", "%d", hw->config.rr_quanta);
	for (i = 0; i < ARRAY_SIZE(hw->config.filter_def); i++) {
		ocs_ddump_value(textbuf, "filter_def", "%08X", hw->config.filter_def[i]);
	}
	ocs_ddump_value(textbuf, "n_eq", "%d", hw->eq_count);
	ocs_ddump_value(textbuf, "n_cq", "%d", hw->cq_count);
	ocs_ddump_value(textbuf, "n_mq", "%d", hw->mq_count);
	ocs_ddump_value(textbuf, "n_rq", "%d", hw->rq_count);
	ocs_ddump_value(textbuf, "n_wq", "%d", hw->wq_count);
	ocs_ddump_value(textbuf, "n_sgl", "%d", hw->config.n_sgl);

	ocs_ddump_sli(textbuf, &hw->sli);

	ocs_ddump_sli4_queue(textbuf, "wq", hw, hw->wq, hw->wq_count,
			((flags & OCS_DDUMP_FLAGS_WQES) ? qentries : 0));
	ocs_ddump_sli4_queue(textbuf, "rq", hw, hw->rq, hw->rq_count,
			((flags & OCS_DDUMP_FLAGS_RQES) ? qentries : 0));
	ocs_ddump_sli4_queue(textbuf, "mq", hw, hw->mq, hw->mq_count,
			((flags & OCS_DDUMP_FLAGS_MQES) ? qentries : 0));
	ocs_ddump_sli4_queue(textbuf, "cq", hw, hw->cq, hw->cq_count,
			((flags & OCS_DDUMP_FLAGS_CQES) ? qentries : 0));
	ocs_ddump_sli4_queue(textbuf, "eq", hw, hw->eq, hw->eq_count,
			((flags & OCS_DDUMP_FLAGS_EQES) ? qentries : 0));

	/* dump the IO quarantine list */
	for (i = 0; i < hw->wq_count; i++) {
		ocs_ddump_section(textbuf, "io_quarantine", i);
		ocs_ddump_value(textbuf, "quarantine_index", "%d", hw->hw_wq[i]->quarantine_info.quarantine_index);
		for (j = 0; j < OCS_HW_QUARANTINE_QUEUE_DEPTH; j++) {
			if (hw->hw_wq[i]->quarantine_info.quarantine_ios[j] != NULL) {
				ocs_ddump_hw_io(textbuf, hw->hw_wq[i]->quarantine_info.quarantine_ios[j]);
			}
		}
		ocs_ddump_endsection(textbuf, "io_quarantine", i);
	}

	ocs_ddump_section(textbuf, "workaround", ocs->instance_index);
	ocs_ddump_value(textbuf, "fwrev", "%08llx", (unsigned long long)hw->workaround.fwrev);
	ocs_ddump_endsection(textbuf, "workaround", ocs->instance_index);

	ocs_lock(&hw->io_lock);
		ocs_ddump_section(textbuf, "io_inuse", ocs->instance_index);
		ocs_list_foreach(&hw->io_inuse, io) {
			ocs_ddump_hw_io(textbuf, io);
		}
		ocs_ddump_endsection(textbuf, "io_inuse", ocs->instance_index);

		ocs_ddump_section(textbuf, "io_wait_free", ocs->instance_index);
		ocs_list_foreach(&hw->io_wait_free, io) {
			ocs_ddump_hw_io(textbuf, io);
		}
		ocs_ddump_endsection(textbuf, "io_wait_free", ocs->instance_index);
		ocs_ddump_section(textbuf, "io_free", ocs->instance_index);
		ocs_list_foreach(&hw->io_free, io) {
			if (io->xbusy) {
				/* only display free ios if they're active */
				ocs_ddump_hw_io(textbuf, io);
			}
			cnt++;
		}
		ocs_ddump_endsection(textbuf, "io_free", ocs->instance_index);
		ocs_ddump_value(textbuf, "ios_free", "%d", cnt);

	ocs_ddump_value(textbuf, "sec_hio_wait_count", "%d", hw->sec_hio_wait_count);
	ocs_unlock(&hw->io_lock);

	/* now check the IOs not in a list; i.e. sequence coalescing xris */
	ocs_ddump_section(textbuf, "port_owned_ios", ocs->instance_index);
	for (i = 0; i < hw->config.n_io; i++) {
		io = hw->io[i];
		if (!io)
			continue;

		if (ocs_hw_is_xri_port_owned(hw, io->indicator)) {
			if (ocs_ref_read_count(&io->ref)) {
				/* only display free ios if they're active */
				ocs_ddump_hw_io(textbuf, io);
			}
		}
	}
	ocs_ddump_endsection(textbuf, "port_owned_ios", ocs->instance_index);

	ocs_textbuf_printf(textbuf, "<rpi_ref>");
	for (i = 0; i < max_rpi; i++) {
		if (ocs_atomic_read(&hw->rpi_ref[i].rpi_attached) ||
			ocs_atomic_read(&hw->rpi_ref[i].rpi_count) ) {
			ocs_textbuf_printf(textbuf, "[%d] att=%d cnt=%d\n", i,
				ocs_atomic_read(&hw->rpi_ref[i].rpi_attached),
				ocs_atomic_read(&hw->rpi_ref[i].rpi_count));
		}
	}
	ocs_textbuf_printf(textbuf, "</rpi_ref>");

	for (i = 0; i < hw->wq_count; i++) {
		ocs_ddump_value(textbuf, "wq_submit", "%d", hw->tcmd_wq_submit[i]);
	}
	for (i = 0; i < hw->wq_count; i++) {
		ocs_ddump_value(textbuf, "wq_complete", "%d", hw->tcmd_wq_complete[i]);
	}

	hw_queue_ddump(textbuf, hw);

	ocs_ddump_endsection(textbuf, "hw", ocs->instance_index);

}

void
hw_queue_ddump(ocs_textbuf_t *textbuf, ocs_hw_t *hw)
{
	hw_eq_t *eq;
	hw_cq_t *cq;
	hw_q_t *q;
	hw_mq_t *mq;
	hw_wq_t *wq;
	hw_rq_t *rq;

	ocs_ddump_section(textbuf, "hw_queue", 0);
	ocs_list_foreach(&hw->eq_list, eq) {
		ocs_ddump_section(textbuf, "eq", eq->instance);
		ocs_ddump_value(textbuf, "queue-id", "%d", eq->queue->id);
		OCS_STAT(ocs_ddump_value(textbuf, "use_count", "%d", eq->use_count));
		ocs_list_foreach(&eq->cq_list, cq) {
			ocs_ddump_section(textbuf, "cq", cq->instance);
			ocs_ddump_value(textbuf, "queue-id", "%d", cq->queue->id);
			OCS_STAT(ocs_ddump_value(textbuf, "use_count", "%d", cq->use_count));
			ocs_list_foreach(&cq->q_list, q) {
				switch(q->type) {
				case SLI_QTYPE_MQ:
					mq = (hw_mq_t *) q;
					ocs_ddump_section(textbuf, "mq", mq->instance);
					ocs_ddump_value(textbuf, "queue-id", "%d", mq->queue->id);
					OCS_STAT(ocs_ddump_value(textbuf, "use_count", "%d", mq->use_count));
					ocs_ddump_endsection(textbuf, "mq", mq->instance);
					break;
				case SLI_QTYPE_WQ:
					wq = (hw_wq_t *) q;
					ocs_ddump_section(textbuf, "wq", wq->instance);
					ocs_ddump_value(textbuf, "queue-id", "%d", wq->queue->id);
					OCS_STAT(ocs_ddump_value(textbuf, "use_count", "%d", wq->use_count));
					ocs_ddump_value(textbuf, "wqec_count", "%d", wq->wqec_count);
					ocs_ddump_value(textbuf, "free_count", "%d", wq->free_count);
					OCS_STAT(ocs_ddump_value(textbuf, "wq_pending_count", "%d",
								 wq->wq_pending_count));
					ocs_ddump_endsection(textbuf, "wq", wq->instance);
					break;
				case SLI_QTYPE_RQ:
					rq = (hw_rq_t *) q;
					ocs_ddump_section(textbuf, "rq", rq->instance);
					OCS_STAT(ocs_ddump_value(textbuf, "use_count", "%d", rq->use_count));
					ocs_ddump_value(textbuf, "filter_mask", "%d", rq->filter_mask);
					if (rq->hdr != NULL) {
						ocs_ddump_value(textbuf, "hdr-id", "%d", rq->hdr->id);
						OCS_STAT(ocs_ddump_value(textbuf, "hdr_use_count", "%d", rq->hdr_use_count));
					}
					if (rq->first_burst != NULL) {
						OCS_STAT(ocs_ddump_value(textbuf, "fb-id", "%d", rq->first_burst->id));
						OCS_STAT(ocs_ddump_value(textbuf, "fb_use_count", "%d", rq->fb_use_count));
					}
					if (rq->data != NULL) {
						OCS_STAT(ocs_ddump_value(textbuf, "payload-id", "%d", rq->data->id));
						OCS_STAT(ocs_ddump_value(textbuf, "payload_use_count", "%d", rq->payload_use_count));
					}
					ocs_ddump_endsection(textbuf, "rq", rq->instance);
					break;
				default:
					break;
				}
			}
			ocs_ddump_endsection(textbuf, "cq", cq->instance);
		}
		ocs_ddump_endsection(textbuf, "eq", eq->instance);
	}
	ocs_ddump_endsection(textbuf, "hw_queue", 0);
}

/**
 * @brief Initiate ddump
 *
 * Traverses the ocs/domain/port/node/io data structures to generate a driver
 * dump.
 *
 * @param ocs pointer to device context
 * @param textbuf pointer to text buffer
 * @param flags ddump flags
 * @param qentries number of queue entries to dump
 *
 * @return Returns 0 on success, or a negative value on failure.
 */

int
ocs_ddump(ocs_t *ocs, ocs_textbuf_t *textbuf, uint32_t flags, uint32_t qentries)
{
	ocs_xport_t *xport = ocs->xport;
	ocs_domain_t *domain;
	uint32_t instance;
	ocs_vport_spec_t *vport;
	ocs_io_t *io;
	int retval = 0;
	uint32_t i;

	ocs_ddump_startfile(textbuf);

	ocs_ddump_section(textbuf, "ocs", ocs->instance_index);

	ocs_ddump_section(textbuf, "ocs_os", ocs->instance_index);
#ifdef OCS_ENABLE_NUMA_SUPPORT
	ocs_ddump_value(textbuf, "numa_node", "%d", ocs->ocs_os.numa_node);
#endif
	ocs_ddump_endsection(textbuf, "ocs_os", ocs->instance_index);

	ocs_ddump_value(textbuf, "drv_name", "%s", DRV_NAME);
	ocs_ddump_value(textbuf, "drv_version", "%s", DRV_VERSION);
	ocs_ddump_value(textbuf, "display_name", "%s", ocs->display_name);
	ocs_ddump_value(textbuf, "enable_ini", "%d", ocs->enable_ini);
	ocs_ddump_value(textbuf, "enable_tgt", "%d", ocs->enable_tgt);
	ocs_ddump_value(textbuf, "nodes_count", "%d", xport->nodes_count);
	ocs_ddump_value(textbuf, "enable_hlm", "%d", ocs->enable_hlm);
	ocs_ddump_value(textbuf, "hlm_group_size", "%d", ocs->hlm_group_size);
	ocs_ddump_value(textbuf, "auto_xfer_rdy_size", "%d", ocs->auto_xfer_rdy_size);
	ocs_ddump_value(textbuf, "io_alloc_failed_count", "%d", ocs_atomic_read(&xport->io_alloc_failed_count));
	ocs_ddump_value(textbuf, "io_active_count", "%d", ocs_atomic_read(&xport->io_active_count));
	ocs_ddump_value(textbuf, "io_pending_count", "%d", ocs_atomic_read(&xport->io_pending_count));
	ocs_ddump_value(textbuf, "io_total_alloc", "%d", ocs_atomic_read(&xport->io_total_alloc));
	ocs_ddump_value(textbuf, "io_total_free", "%d", ocs_atomic_read(&xport->io_total_free));
	ocs_ddump_value(textbuf, "io_total_pending", "%d", ocs_atomic_read(&xport->io_total_pending));
	ocs_ddump_value(textbuf, "io_pending_recursing", "%d", ocs_atomic_read(&xport->io_pending_recursing));
	ocs_ddump_value(textbuf, "max_isr_time_msec", "%d", ocs->max_isr_time_msec);
	for (i = 0; i < SLI4_MAX_FCFI; i++) {
		ocs_lock(&xport->fcfi[i].pend_frames_lock);
		if (!ocs_list_empty(&xport->fcfi[i].pend_frames)) {
			ocs_hw_sequence_t *frame;
			ocs_ddump_section(textbuf, "pending_frames", i);
			ocs_ddump_value(textbuf, "hold_frames", "%d", xport->fcfi[i].hold_frames);
			ocs_list_foreach(&xport->fcfi[i].pend_frames, frame) {
				fc_header_t *hdr;
				char buf[128];

				hdr = frame->header->dma.virt;
				ocs_snprintf(buf, sizeof(buf), "%02x/%04x/%04x len %zu",
				 hdr->r_ctl, ocs_be16toh(hdr->ox_id), ocs_be16toh(hdr->rx_id),
				 frame->payload->dma.len);
				ocs_ddump_value(textbuf, "frame", "%s", buf);
			}
			ocs_ddump_endsection(textbuf, "pending_frames", i);
		}
		ocs_unlock(&xport->fcfi[i].pend_frames_lock);
	}

	ocs_lock(&xport->io_pending_lock);
		ocs_ddump_section(textbuf, "io_pending_list", ocs->instance_index);
		ocs_list_foreach(&xport->io_pending_list, io) {
			ocs_ddump_io(textbuf, io);
		}
		ocs_ddump_endsection(textbuf, "io_pending_list", ocs->instance_index);
	ocs_unlock(&xport->io_pending_lock);

#if defined(ENABLE_LOCK_DEBUG)
	/* Dump the lock list */
	ocs_ddump_section(textbuf, "locks", 0);
	ocs_lock(&ocs->ocs_os.locklist_lock); {
		ocs_lock_t *l;
		uint32_t idx = 0;
		ocs_list_foreach(&ocs->ocs_os.locklist, l) {
			ocs_ddump_section(textbuf, "lock", idx);
			ocs_ddump_value(textbuf, "name", "%s", l->name);
			ocs_ddump_value(textbuf, "inuse", "%d", l->inuse);
			ocs_ddump_value(textbuf, "caller", "%p", l->caller[0]);
			ocs_ddump_value(textbuf, "pid", "%08x", l->pid.l);
			ocs_ddump_endsection(textbuf, "lock", idx);
			idx++;
		}
	} ocs_unlock(&ocs->ocs_os.locklist_lock);
	ocs_ddump_endsection(textbuf, "locks", 0);
#endif

	/* Dump any pending vports */
	if (ocs_device_lock_try(ocs) != TRUE) {
		/* Didn't get the lock */
		return -1;
	}
		instance = 0;
		ocs_list_foreach(&xport->vport_list, vport) {
			ocs_ddump_section(textbuf, "vport_spec", instance);
			ocs_ddump_value(textbuf, "domain_instance", "%d", vport->domain_instance);
			ocs_ddump_value(textbuf, "wwnn", "%llx", (unsigned long long)vport->wwnn);
			ocs_ddump_value(textbuf, "wwpn", "%llx", (unsigned long long)vport->wwpn);
			ocs_ddump_value(textbuf, "fc_id", "0x%x", vport->fc_id);
			ocs_ddump_value(textbuf, "enable_tgt", "%d", vport->enable_tgt);
			ocs_ddump_value(textbuf, "enable_ini", "%d" PRIx64, vport->enable_ini);
			ocs_ddump_endsection(textbuf, "vport_spec", instance ++);
		}
	ocs_device_unlock(ocs);

	/* Dump target and initiator private data */
	ocs_scsi_ini_ddump(textbuf, OCS_SCSI_DDUMP_DEVICE, ocs);
	ocs_scsi_tgt_ddump(textbuf, OCS_SCSI_DDUMP_DEVICE, ocs);

	ocs_ddump_hw(textbuf, &ocs->hw, flags, qentries);

	if (ocs_device_lock_try(ocs) != TRUE) {
		/* Didn't get the lock */
		return -1;
	}
		/* Here the device lock is held */
		ocs_list_foreach(&ocs->domain_list, domain) {
			retval = ocs_ddump_domain(textbuf, domain);
			if (retval != 0) {
				break;
			}
		}

		/* Dump ramlog */
		ocs_ddump_ramlog(textbuf, ocs->ramlog);
	ocs_device_unlock(ocs);

#if !defined(OCS_DEBUG_QUEUE_HISTORY)
	ocs_ddump_section(textbuf, "q_hist", ocs->instance_index);
	ocs_textbuf_printf(textbuf, "<history>\n");
	ocs_textbuf_printf(textbuf, "No history available\n");
	ocs_textbuf_printf(textbuf, "</history>\n");
	ocs_ddump_endsection(textbuf, "q_hist", ocs->instance_index);
#else
	ocs_ddump_queue_history(textbuf, &ocs->hw.q_hist);
#endif

#if defined(OCS_DEBUG_MEMORY)
	ocs_memory_allocated_ddump(textbuf);
#endif

	ocs_ddump_endsection(textbuf, "ocs", ocs->instance_index);

	ocs_ddump_endfile(textbuf);

	return retval;
}

/**
 * @brief Capture and save ddump
 *
 * Captures and saves a ddump to the ocs_t structure to save the
 * current state. The goal of this function is to save a ddump
 * as soon as an issue is encountered. The saved ddump will be
 * kept until the user reads it.
 *
 * @param ocs pointer to device context
 * @param flags ddump flags
 * @param qentries number of queue entries to dump
 *
 * @return 0 if ddump was saved; > 0 of one already exists; < 0
 * error
 */

int32_t
ocs_save_ddump(ocs_t *ocs, uint32_t flags, uint32_t qentries)
{
	if (ocs_textbuf_get_written(&ocs->ddump_saved) > 0) {
		ocs_log_debug(ocs, "Saved ddump already exists\n");
		return 1;
	}

	if (!ocs_textbuf_initialized(&ocs->ddump_saved)) {
		ocs_log_err(ocs, "Saved ddump not allocated\n");
		return -1;
	}

	ocs_log_debug(ocs, "Saving ddump\n");
	ocs_ddump(ocs, &ocs->ddump_saved, flags, qentries);
	ocs_log_debug(ocs, "Saved ddump: %d bytes written\n", ocs_textbuf_get_written(&ocs->ddump_saved));
	return 0;
}

/**
 * @brief Capture and save ddump for all OCS instances
 *
 * Calls ocs_save_ddump() for each OCS instance.
 *
 * @param flags ddump flags
 * @param qentries number of queue entries to dump
 * @param alloc_flag allocate dump buffer if not already allocated
 *
 * @return 0 if ddump was saved; > 0 of one already exists; < 0
 * error
 */

int32_t
ocs_save_ddump_all(uint32_t flags, uint32_t qentries, uint32_t alloc_flag)
{
	ocs_t *ocs;
	uint32_t i;
	int32_t rc = 0;

	for (i = 0; (ocs = ocs_get_instance(i)) != NULL; i++) {
		if (alloc_flag && (!ocs_textbuf_initialized(&ocs->ddump_saved))) {
			rc = ocs_textbuf_alloc(ocs, &ocs->ddump_saved, DEFAULT_SAVED_DUMP_SIZE);
			if (rc) {
				break;
			}
		}

		rc = ocs_save_ddump(ocs, flags, qentries);
		if (rc < 0) {
			break;
		}
	}
	return rc;
}

/**
 * @brief Clear saved ddump
 *
 * Clears saved ddump to make room for next one.
 *
 * @param ocs pointer to device context
 *
 * @return 0 if ddump was cleared; > 0 no saved ddump found
 */

int32_t
ocs_clear_saved_ddump(ocs_t *ocs)
{
	/* if there's a saved ddump, copy to newly allocated textbuf */
	if (ocs_textbuf_get_written(&ocs->ddump_saved)) {
		ocs_log_debug(ocs, "saved ddump cleared\n");
		ocs_textbuf_reset(&ocs->ddump_saved);
		return 0;
	} else {
		ocs_log_debug(ocs, "no saved ddump found\n");
		return 1;
	}
}

