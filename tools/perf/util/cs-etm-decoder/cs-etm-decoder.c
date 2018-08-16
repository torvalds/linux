// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015-2018 Linaro Limited.
 *
 * Author: Tor Jeremiassen <tor@ti.com>
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/err.h>
#include <linux/list.h>
#include <stdlib.h>
#include <opencsd/c_api/opencsd_c_api.h>
#include <opencsd/etmv4/trc_pkt_types_etmv4.h>
#include <opencsd/ocsd_if_types.h>

#include "cs-etm.h"
#include "cs-etm-decoder.h"
#include "intlist.h"
#include "util.h"

#define MAX_BUFFER 1024

/* use raw logging */
#ifdef CS_DEBUG_RAW
#define CS_LOG_RAW_FRAMES
#ifdef CS_RAW_PACKED
#define CS_RAW_DEBUG_FLAGS (OCSD_DFRMTR_UNPACKED_RAW_OUT | \
			    OCSD_DFRMTR_PACKED_RAW_OUT)
#else
#define CS_RAW_DEBUG_FLAGS (OCSD_DFRMTR_UNPACKED_RAW_OUT)
#endif
#endif

#define CS_ETM_INVAL_ADDR	0xdeadbeefdeadbeefUL

struct cs_etm_decoder {
	void *data;
	void (*packet_printer)(const char *msg);
	bool trace_on;
	dcd_tree_handle_t dcd_tree;
	cs_etm_mem_cb_type mem_access;
	ocsd_datapath_resp_t prev_return;
	u32 packet_count;
	u32 head;
	u32 tail;
	struct cs_etm_packet packet_buffer[MAX_BUFFER];
};

static u32
cs_etm_decoder__mem_access(const void *context,
			   const ocsd_vaddr_t address,
			   const ocsd_mem_space_acc_t mem_space __maybe_unused,
			   const u32 req_size,
			   u8 *buffer)
{
	struct cs_etm_decoder *decoder = (struct cs_etm_decoder *) context;

	return decoder->mem_access(decoder->data,
				   address,
				   req_size,
				   buffer);
}

int cs_etm_decoder__add_mem_access_cb(struct cs_etm_decoder *decoder,
				      u64 start, u64 end,
				      cs_etm_mem_cb_type cb_func)
{
	decoder->mem_access = cb_func;

	if (ocsd_dt_add_callback_mem_acc(decoder->dcd_tree, start, end,
					 OCSD_MEM_SPACE_ANY,
					 cs_etm_decoder__mem_access, decoder))
		return -1;

	return 0;
}

int cs_etm_decoder__reset(struct cs_etm_decoder *decoder)
{
	ocsd_datapath_resp_t dp_ret;

	decoder->prev_return = OCSD_RESP_CONT;

	dp_ret = ocsd_dt_process_data(decoder->dcd_tree, OCSD_OP_RESET,
				      0, 0, NULL, NULL);
	if (OCSD_DATA_RESP_IS_FATAL(dp_ret))
		return -1;

	return 0;
}

int cs_etm_decoder__get_packet(struct cs_etm_decoder *decoder,
			       struct cs_etm_packet *packet)
{
	if (!decoder || !packet)
		return -EINVAL;

	/* Nothing to do, might as well just return */
	if (decoder->packet_count == 0)
		return 0;
	/*
	 * The queueing process in function cs_etm_decoder__buffer_packet()
	 * increments the tail *before* using it.  This is somewhat counter
	 * intuitive but it has the advantage of centralizing tail management
	 * at a single location.  Because of that we need to follow the same
	 * heuristic with the head, i.e we increment it before using its
	 * value.  Otherwise the first element of the packet queue is not
	 * used.
	 */
	decoder->head = (decoder->head + 1) & (MAX_BUFFER - 1);

	*packet = decoder->packet_buffer[decoder->head];

	decoder->packet_count--;

	return 1;
}

static void cs_etm_decoder__gen_etmv4_config(struct cs_etm_trace_params *params,
					     ocsd_etmv4_cfg *config)
{
	config->reg_configr = params->etmv4.reg_configr;
	config->reg_traceidr = params->etmv4.reg_traceidr;
	config->reg_idr0 = params->etmv4.reg_idr0;
	config->reg_idr1 = params->etmv4.reg_idr1;
	config->reg_idr2 = params->etmv4.reg_idr2;
	config->reg_idr8 = params->etmv4.reg_idr8;
	config->reg_idr9 = 0;
	config->reg_idr10 = 0;
	config->reg_idr11 = 0;
	config->reg_idr12 = 0;
	config->reg_idr13 = 0;
	config->arch_ver = ARCH_V8;
	config->core_prof = profile_CortexA;
}

static void cs_etm_decoder__print_str_cb(const void *p_context,
					 const char *msg,
					 const int str_len)
{
	if (p_context && str_len)
		((struct cs_etm_decoder *)p_context)->packet_printer(msg);
}

static int
cs_etm_decoder__init_def_logger_printing(struct cs_etm_decoder_params *d_params,
					 struct cs_etm_decoder *decoder)
{
	int ret = 0;

	if (d_params->packet_printer == NULL)
		return -1;

	decoder->packet_printer = d_params->packet_printer;

	/*
	 * Set up a library default logger to process any printers
	 * (packet/raw frame) we add later.
	 */
	ret = ocsd_def_errlog_init(OCSD_ERR_SEV_ERROR, 1);
	if (ret != 0)
		return -1;

	/* no stdout / err / file output */
	ret = ocsd_def_errlog_config_output(C_API_MSGLOGOUT_FLG_NONE, NULL);
	if (ret != 0)
		return -1;

	/*
	 * Set the string CB for the default logger, passes strings to
	 * perf print logger.
	 */
	ret = ocsd_def_errlog_set_strprint_cb(decoder->dcd_tree,
					      (void *)decoder,
					      cs_etm_decoder__print_str_cb);
	if (ret != 0)
		ret = -1;

	return 0;
}

#ifdef CS_LOG_RAW_FRAMES
static void
cs_etm_decoder__init_raw_frame_logging(struct cs_etm_decoder_params *d_params,
				       struct cs_etm_decoder *decoder)
{
	/* Only log these during a --dump operation */
	if (d_params->operation == CS_ETM_OPERATION_PRINT) {
		/* set up a library default logger to process the
		 *  raw frame printer we add later
		 */
		ocsd_def_errlog_init(OCSD_ERR_SEV_ERROR, 1);

		/* no stdout / err / file output */
		ocsd_def_errlog_config_output(C_API_MSGLOGOUT_FLG_NONE, NULL);

		/* set the string CB for the default logger,
		 * passes strings to perf print logger.
		 */
		ocsd_def_errlog_set_strprint_cb(decoder->dcd_tree,
						(void *)decoder,
						cs_etm_decoder__print_str_cb);

		/* use the built in library printer for the raw frames */
		ocsd_dt_set_raw_frame_printer(decoder->dcd_tree,
					      CS_RAW_DEBUG_FLAGS);
	}
}
#else
static void
cs_etm_decoder__init_raw_frame_logging(
		struct cs_etm_decoder_params *d_params __maybe_unused,
		struct cs_etm_decoder *decoder __maybe_unused)
{
}
#endif

static int cs_etm_decoder__create_packet_printer(struct cs_etm_decoder *decoder,
						 const char *decoder_name,
						 void *trace_config)
{
	u8 csid;

	if (ocsd_dt_create_decoder(decoder->dcd_tree, decoder_name,
				   OCSD_CREATE_FLG_PACKET_PROC,
				   trace_config, &csid))
		return -1;

	if (ocsd_dt_set_pkt_protocol_printer(decoder->dcd_tree, csid, 0))
		return -1;

	return 0;
}

static int
cs_etm_decoder__create_etm_packet_printer(struct cs_etm_trace_params *t_params,
					  struct cs_etm_decoder *decoder)
{
	const char *decoder_name;
	ocsd_etmv4_cfg trace_config_etmv4;
	void *trace_config;

	switch (t_params->protocol) {
	case CS_ETM_PROTO_ETMV4i:
		cs_etm_decoder__gen_etmv4_config(t_params, &trace_config_etmv4);
		decoder_name = OCSD_BUILTIN_DCD_ETMV4I;
		trace_config = &trace_config_etmv4;
		break;
	default:
		return -1;
	}

	return cs_etm_decoder__create_packet_printer(decoder,
						     decoder_name,
						     trace_config);
}

static void cs_etm_decoder__clear_buffer(struct cs_etm_decoder *decoder)
{
	int i;

	decoder->head = 0;
	decoder->tail = 0;
	decoder->packet_count = 0;
	for (i = 0; i < MAX_BUFFER; i++) {
		decoder->packet_buffer[i].start_addr = CS_ETM_INVAL_ADDR;
		decoder->packet_buffer[i].end_addr = CS_ETM_INVAL_ADDR;
		decoder->packet_buffer[i].last_instr_taken_branch = false;
		decoder->packet_buffer[i].exc = false;
		decoder->packet_buffer[i].exc_ret = false;
		decoder->packet_buffer[i].cpu = INT_MIN;
	}
}

static ocsd_datapath_resp_t
cs_etm_decoder__buffer_packet(struct cs_etm_decoder *decoder,
			      const u8 trace_chan_id,
			      enum cs_etm_sample_type sample_type)
{
	u32 et = 0;
	struct int_node *inode = NULL;

	if (decoder->packet_count >= MAX_BUFFER - 1)
		return OCSD_RESP_FATAL_SYS_ERR;

	/* Search the RB tree for the cpu associated with this traceID */
	inode = intlist__find(traceid_list, trace_chan_id);
	if (!inode)
		return OCSD_RESP_FATAL_SYS_ERR;

	et = decoder->tail;
	et = (et + 1) & (MAX_BUFFER - 1);
	decoder->tail = et;
	decoder->packet_count++;

	decoder->packet_buffer[et].sample_type = sample_type;
	decoder->packet_buffer[et].exc = false;
	decoder->packet_buffer[et].exc_ret = false;
	decoder->packet_buffer[et].cpu = *((int *)inode->priv);
	decoder->packet_buffer[et].start_addr = CS_ETM_INVAL_ADDR;
	decoder->packet_buffer[et].end_addr = CS_ETM_INVAL_ADDR;

	if (decoder->packet_count == MAX_BUFFER - 1)
		return OCSD_RESP_WAIT;

	return OCSD_RESP_CONT;
}

static ocsd_datapath_resp_t
cs_etm_decoder__buffer_range(struct cs_etm_decoder *decoder,
			     const ocsd_generic_trace_elem *elem,
			     const uint8_t trace_chan_id)
{
	int ret = 0;
	struct cs_etm_packet *packet;

	ret = cs_etm_decoder__buffer_packet(decoder, trace_chan_id,
					    CS_ETM_RANGE);
	if (ret != OCSD_RESP_CONT && ret != OCSD_RESP_WAIT)
		return ret;

	packet = &decoder->packet_buffer[decoder->tail];

	packet->start_addr = elem->st_addr;
	packet->end_addr = elem->en_addr;
	switch (elem->last_i_type) {
	case OCSD_INSTR_BR:
	case OCSD_INSTR_BR_INDIRECT:
		packet->last_instr_taken_branch = elem->last_instr_exec;
		break;
	case OCSD_INSTR_ISB:
	case OCSD_INSTR_DSB_DMB:
	case OCSD_INSTR_OTHER:
	default:
		packet->last_instr_taken_branch = false;
		break;
	}

	return ret;
}

static ocsd_datapath_resp_t
cs_etm_decoder__buffer_trace_on(struct cs_etm_decoder *decoder,
				const uint8_t trace_chan_id)
{
	return cs_etm_decoder__buffer_packet(decoder, trace_chan_id,
					     CS_ETM_TRACE_ON);
}

static ocsd_datapath_resp_t cs_etm_decoder__gen_trace_elem_printer(
				const void *context,
				const ocsd_trc_index_t indx __maybe_unused,
				const u8 trace_chan_id __maybe_unused,
				const ocsd_generic_trace_elem *elem)
{
	ocsd_datapath_resp_t resp = OCSD_RESP_CONT;
	struct cs_etm_decoder *decoder = (struct cs_etm_decoder *) context;

	switch (elem->elem_type) {
	case OCSD_GEN_TRC_ELEM_UNKNOWN:
		break;
	case OCSD_GEN_TRC_ELEM_NO_SYNC:
		decoder->trace_on = false;
		break;
	case OCSD_GEN_TRC_ELEM_TRACE_ON:
		resp = cs_etm_decoder__buffer_trace_on(decoder,
						       trace_chan_id);
		decoder->trace_on = true;
		break;
	case OCSD_GEN_TRC_ELEM_INSTR_RANGE:
		resp = cs_etm_decoder__buffer_range(decoder, elem,
						    trace_chan_id);
		break;
	case OCSD_GEN_TRC_ELEM_EXCEPTION:
		decoder->packet_buffer[decoder->tail].exc = true;
		break;
	case OCSD_GEN_TRC_ELEM_EXCEPTION_RET:
		decoder->packet_buffer[decoder->tail].exc_ret = true;
		break;
	case OCSD_GEN_TRC_ELEM_PE_CONTEXT:
	case OCSD_GEN_TRC_ELEM_EO_TRACE:
	case OCSD_GEN_TRC_ELEM_ADDR_NACC:
	case OCSD_GEN_TRC_ELEM_TIMESTAMP:
	case OCSD_GEN_TRC_ELEM_CYCLE_COUNT:
	case OCSD_GEN_TRC_ELEM_ADDR_UNKNOWN:
	case OCSD_GEN_TRC_ELEM_EVENT:
	case OCSD_GEN_TRC_ELEM_SWTRACE:
	case OCSD_GEN_TRC_ELEM_CUSTOM:
	default:
		break;
	}

	return resp;
}

static int cs_etm_decoder__create_etm_packet_decoder(
					struct cs_etm_trace_params *t_params,
					struct cs_etm_decoder *decoder)
{
	const char *decoder_name;
	ocsd_etmv4_cfg trace_config_etmv4;
	void *trace_config;
	u8 csid;

	switch (t_params->protocol) {
	case CS_ETM_PROTO_ETMV4i:
		cs_etm_decoder__gen_etmv4_config(t_params, &trace_config_etmv4);
		decoder_name = OCSD_BUILTIN_DCD_ETMV4I;
		trace_config = &trace_config_etmv4;
		break;
	default:
		return -1;
	}

	if (ocsd_dt_create_decoder(decoder->dcd_tree,
				     decoder_name,
				     OCSD_CREATE_FLG_FULL_DECODER,
				     trace_config, &csid))
		return -1;

	if (ocsd_dt_set_gen_elem_outfn(decoder->dcd_tree,
				       cs_etm_decoder__gen_trace_elem_printer,
				       decoder))
		return -1;

	return 0;
}

static int
cs_etm_decoder__create_etm_decoder(struct cs_etm_decoder_params *d_params,
				   struct cs_etm_trace_params *t_params,
				   struct cs_etm_decoder *decoder)
{
	if (d_params->operation == CS_ETM_OPERATION_PRINT)
		return cs_etm_decoder__create_etm_packet_printer(t_params,
								 decoder);
	else if (d_params->operation == CS_ETM_OPERATION_DECODE)
		return cs_etm_decoder__create_etm_packet_decoder(t_params,
								 decoder);

	return -1;
}

struct cs_etm_decoder *
cs_etm_decoder__new(int num_cpu, struct cs_etm_decoder_params *d_params,
		    struct cs_etm_trace_params t_params[])
{
	struct cs_etm_decoder *decoder;
	ocsd_dcd_tree_src_t format;
	u32 flags;
	int i, ret;

	if ((!t_params) || (!d_params))
		return NULL;

	decoder = zalloc(sizeof(*decoder));

	if (!decoder)
		return NULL;

	decoder->data = d_params->data;
	decoder->prev_return = OCSD_RESP_CONT;
	cs_etm_decoder__clear_buffer(decoder);
	format = (d_params->formatted ? OCSD_TRC_SRC_FRAME_FORMATTED :
					 OCSD_TRC_SRC_SINGLE);
	flags = 0;
	flags |= (d_params->fsyncs ? OCSD_DFRMTR_HAS_FSYNCS : 0);
	flags |= (d_params->hsyncs ? OCSD_DFRMTR_HAS_HSYNCS : 0);
	flags |= (d_params->frame_aligned ? OCSD_DFRMTR_FRAME_MEM_ALIGN : 0);

	/*
	 * Drivers may add barrier frames when used with perf, set up to
	 * handle this. Barriers const of FSYNC packet repeated 4 times.
	 */
	flags |= OCSD_DFRMTR_RESET_ON_4X_FSYNC;

	/* Create decode tree for the data source */
	decoder->dcd_tree = ocsd_create_dcd_tree(format, flags);

	if (decoder->dcd_tree == 0)
		goto err_free_decoder;

	/* init library print logging support */
	ret = cs_etm_decoder__init_def_logger_printing(d_params, decoder);
	if (ret != 0)
		goto err_free_decoder_tree;

	/* init raw frame logging if required */
	cs_etm_decoder__init_raw_frame_logging(d_params, decoder);

	for (i = 0; i < num_cpu; i++) {
		ret = cs_etm_decoder__create_etm_decoder(d_params,
							 &t_params[i],
							 decoder);
		if (ret != 0)
			goto err_free_decoder_tree;
	}

	return decoder;

err_free_decoder_tree:
	ocsd_destroy_dcd_tree(decoder->dcd_tree);
err_free_decoder:
	free(decoder);
	return NULL;
}

int cs_etm_decoder__process_data_block(struct cs_etm_decoder *decoder,
				       u64 indx, const u8 *buf,
				       size_t len, size_t *consumed)
{
	int ret = 0;
	ocsd_datapath_resp_t cur = OCSD_RESP_CONT;
	ocsd_datapath_resp_t prev_return = decoder->prev_return;
	size_t processed = 0;
	u32 count;

	while (processed < len) {
		if (OCSD_DATA_RESP_IS_WAIT(prev_return)) {
			cur = ocsd_dt_process_data(decoder->dcd_tree,
						   OCSD_OP_FLUSH,
						   0,
						   0,
						   NULL,
						   NULL);
		} else if (OCSD_DATA_RESP_IS_CONT(prev_return)) {
			cur = ocsd_dt_process_data(decoder->dcd_tree,
						   OCSD_OP_DATA,
						   indx + processed,
						   len - processed,
						   &buf[processed],
						   &count);
			processed += count;
		} else {
			ret = -EINVAL;
			break;
		}

		/*
		 * Return to the input code if the packet buffer is full.
		 * Flushing will get done once the packet buffer has been
		 * processed.
		 */
		if (OCSD_DATA_RESP_IS_WAIT(cur))
			break;

		prev_return = cur;
	}

	decoder->prev_return = cur;
	*consumed = processed;

	return ret;
}

void cs_etm_decoder__free(struct cs_etm_decoder *decoder)
{
	if (!decoder)
		return;

	ocsd_destroy_dcd_tree(decoder->dcd_tree);
	decoder->dcd_tree = NULL;
	free(decoder);
}
