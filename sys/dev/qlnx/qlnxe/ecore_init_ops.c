/*
 * Copyright (c) 2017-2018 Cavium, Inc. 
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * File : ecore_init_ops.c
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* include the precompiled configuration values - only once */
#include "bcm_osal.h"
#include "ecore_hsi_common.h"
#include "ecore.h"
#include "ecore_hw.h"
#include "ecore_status.h"
#include "ecore_rt_defs.h"
#include "ecore_init_fw_funcs.h"

#ifndef CONFIG_ECORE_BINARY_FW
#ifdef CONFIG_ECORE_ZIPPED_FW
#include "ecore_init_values_zipped.h"
#else
#include "ecore_init_values.h"
#endif
#endif

#include "ecore_iro_values.h"
#include "ecore_sriov.h"
#include "ecore_gtt_values.h"
#include "reg_addr.h"
#include "ecore_init_ops.h"

#define ECORE_INIT_MAX_POLL_COUNT	100
#define ECORE_INIT_POLL_PERIOD_US	500

void ecore_init_iro_array(struct ecore_dev *p_dev)
{
	p_dev->iro_arr = iro_arr;
}

/* Runtime configuration helpers */
void ecore_init_clear_rt_data(struct ecore_hwfn *p_hwfn)
{
	int i;

	for (i = 0; i < RUNTIME_ARRAY_SIZE; i++)
		p_hwfn->rt_data.b_valid[i] = false;
}

void ecore_init_store_rt_reg(struct ecore_hwfn *p_hwfn,
			     u32 rt_offset, u32 val)
{
	if (rt_offset >= RUNTIME_ARRAY_SIZE) {
		DP_ERR(p_hwfn,
		       "Avoid storing %u in rt_data at index %u since RUNTIME_ARRAY_SIZE is %u!\n",
		       val, rt_offset, RUNTIME_ARRAY_SIZE);
		return;
	}

	p_hwfn->rt_data.init_val[rt_offset] = val;
	p_hwfn->rt_data.b_valid[rt_offset] = true;
}

void ecore_init_store_rt_agg(struct ecore_hwfn *p_hwfn,
			     u32 rt_offset, u32 *p_val,
			     osal_size_t size)
{
	osal_size_t i;

	if ((rt_offset + size - 1) >= RUNTIME_ARRAY_SIZE) {
		DP_ERR(p_hwfn,
		       "Avoid storing values in rt_data at indices %u-%u since RUNTIME_ARRAY_SIZE is %u!\n",
		       rt_offset, (u32)(rt_offset + size - 1),
		       RUNTIME_ARRAY_SIZE);
		return;
	}

	for (i = 0; i < size / sizeof(u32); i++) {
		p_hwfn->rt_data.init_val[rt_offset + i] = p_val[i];
		p_hwfn->rt_data.b_valid[rt_offset + i] = true;

	}
}

static enum _ecore_status_t ecore_init_rt(struct ecore_hwfn *p_hwfn,
					  struct ecore_ptt *p_ptt,
					  u32 addr,
					  u16 rt_offset,
					  u16 size,
					  bool b_must_dmae)
{
	u32 *p_init_val = &p_hwfn->rt_data.init_val[rt_offset];
	bool *p_valid = &p_hwfn->rt_data.b_valid[rt_offset];
	u16 i, segment;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	/* Since not all RT entries are initialized, go over the RT and
	 * for each segment of initialized values use DMA.
	 */
	for (i = 0; i < size; i++) {
		if (!p_valid[i])
			continue;

		/* In case there isn't any wide-bus configuration here,
		 * simply write the data instead of using dmae.
		 */
		if (!b_must_dmae) {
			ecore_wr(p_hwfn, p_ptt, addr + (i << 2),
				 p_init_val[i]);
			continue;
		}

		/* Start of a new segment */
		for (segment = 1; i + segment < size; segment++)
			if (!p_valid[i + segment])
				break;

		rc = ecore_dmae_host2grc(p_hwfn, p_ptt,
					 (osal_uintptr_t)(p_init_val + i),
					 addr + (i << 2), segment,
					 OSAL_NULL /* default parameters */);
		if (rc != ECORE_SUCCESS)
			return rc;

		/* Jump over the entire segment, including invalid entry */
		i += segment;
	}

	return rc;
}

enum _ecore_status_t ecore_init_alloc(struct ecore_hwfn *p_hwfn)
{
	struct ecore_rt_data *rt_data = &p_hwfn->rt_data;

	if (IS_VF(p_hwfn->p_dev))
		return ECORE_SUCCESS;

	rt_data->b_valid = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
				       sizeof(bool) * RUNTIME_ARRAY_SIZE);
	if (!rt_data->b_valid)
		return ECORE_NOMEM;

	rt_data->init_val = OSAL_ZALLOC(p_hwfn->p_dev, GFP_KERNEL,
					sizeof(u32) * RUNTIME_ARRAY_SIZE);
	if (!rt_data->init_val) {
		OSAL_FREE(p_hwfn->p_dev, rt_data->b_valid);
		rt_data->b_valid = OSAL_NULL;
		return ECORE_NOMEM;
	}

	return ECORE_SUCCESS;
}

void ecore_init_free(struct ecore_hwfn *p_hwfn)
{
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->rt_data.init_val);
	p_hwfn->rt_data.init_val = OSAL_NULL;
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->rt_data.b_valid);
	p_hwfn->rt_data.b_valid = OSAL_NULL;
}

static enum _ecore_status_t ecore_init_array_dmae(struct ecore_hwfn *p_hwfn,
				  struct ecore_ptt *p_ptt,
				  u32 addr, u32 dmae_data_offset,
				  u32 size, const u32 *p_buf,
				  bool b_must_dmae, bool b_can_dmae)
{
	enum _ecore_status_t rc	= ECORE_SUCCESS;

	/* Perform DMAE only for lengthy enough sections or for wide-bus */
#ifndef ASIC_ONLY
	if ((CHIP_REV_IS_SLOW(p_hwfn->p_dev) && (size < 16)) ||
	    !b_can_dmae || (!b_must_dmae && (size < 16))) {
#else
	if (!b_can_dmae || (!b_must_dmae && (size < 16))) {
#endif
		const u32 *data = p_buf + dmae_data_offset;
		u32 i;

		for (i = 0; i < size; i++)
			ecore_wr(p_hwfn, p_ptt, addr + (i << 2), data[i]);
	} else {
		rc = ecore_dmae_host2grc(p_hwfn, p_ptt,
					 (osal_uintptr_t)(p_buf +
							  dmae_data_offset),
					 addr, size,
					 OSAL_NULL /* default parameters */);
	}

	return rc;
}

static enum _ecore_status_t ecore_init_fill_dmae(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 u32 addr, u32 fill_count)
{
	static u32 zero_buffer[DMAE_MAX_RW_SIZE];
	struct ecore_dmae_params params;

	OSAL_MEMSET(zero_buffer, 0, sizeof(u32) * DMAE_MAX_RW_SIZE);

	OSAL_MEMSET(&params, 0, sizeof(params));
	params.flags = ECORE_DMAE_FLAG_RW_REPL_SRC;
	return ecore_dmae_host2grc(p_hwfn, p_ptt,
				   (osal_uintptr_t)(&(zero_buffer[0])),
				   addr, fill_count, &params);
}

static void ecore_init_fill(struct ecore_hwfn *p_hwfn,
			    struct ecore_ptt *p_ptt,
			    u32 addr, u32 fill, u32 fill_count)
{
	u32 i;

	for (i = 0; i < fill_count; i++, addr += sizeof(u32))
		ecore_wr(p_hwfn, p_ptt, addr, fill);
}


static enum _ecore_status_t ecore_init_cmd_array(struct ecore_hwfn *p_hwfn,
						 struct ecore_ptt *p_ptt,
						 struct init_write_op *cmd,
						 bool b_must_dmae,
						 bool b_can_dmae)
{
	u32 dmae_array_offset = OSAL_LE32_TO_CPU(cmd->args.array_offset);
	u32 data = OSAL_LE32_TO_CPU(cmd->data);
	u32 addr = GET_FIELD(data, INIT_WRITE_OP_ADDRESS) << 2;
#ifdef CONFIG_ECORE_ZIPPED_FW
	u32 offset, output_len, input_len, max_size;
#endif
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	union init_array_hdr *hdr;
	const u32 *array_data;
	enum _ecore_status_t rc = ECORE_SUCCESS;
	u32 size;

	array_data = p_dev->fw_data->arr_data;

	hdr = (union init_array_hdr *) (array_data +
					dmae_array_offset);
	data = OSAL_LE32_TO_CPU(hdr->raw.data);
	switch (GET_FIELD(data, INIT_ARRAY_RAW_HDR_TYPE)) {
	case INIT_ARR_ZIPPED:
#ifdef CONFIG_ECORE_ZIPPED_FW
		offset = dmae_array_offset + 1;
		input_len = GET_FIELD(data,
				      INIT_ARRAY_ZIPPED_HDR_ZIPPED_SIZE);
		max_size = MAX_ZIPPED_SIZE * 4;
		OSAL_MEMSET(p_hwfn->unzip_buf, 0, max_size);

		output_len = OSAL_UNZIP_DATA(p_hwfn, input_len,
					     (u8 *)&array_data[offset],
					     max_size, (u8 *)p_hwfn->unzip_buf);
		if (output_len) {
			rc = ecore_init_array_dmae(p_hwfn, p_ptt, addr, 0,
						   output_len,
						   p_hwfn->unzip_buf,
						   b_must_dmae, b_can_dmae);
		} else {
			DP_NOTICE(p_hwfn, true,
				  "Failed to unzip dmae data\n");
			rc = ECORE_INVAL;
		}
#else
		DP_NOTICE(p_hwfn, true,
			  "Using zipped firmware without config enabled\n");
		rc = ECORE_INVAL;
#endif
		break;
	case INIT_ARR_PATTERN:
	{
		u32 repeats = GET_FIELD(data,
					INIT_ARRAY_PATTERN_HDR_REPETITIONS);
		u32 i;

		size = GET_FIELD(data,
				 INIT_ARRAY_PATTERN_HDR_PATTERN_SIZE);

		for (i = 0; i < repeats; i++, addr += size << 2) {
			rc = ecore_init_array_dmae(p_hwfn, p_ptt, addr,
						   dmae_array_offset + 1,
						   size, array_data,
						   b_must_dmae, b_can_dmae);
			if (rc)
				break;
		}
		break;
	}
	case INIT_ARR_STANDARD:
		size = GET_FIELD(data,
				 INIT_ARRAY_STANDARD_HDR_SIZE);
		rc = ecore_init_array_dmae(p_hwfn, p_ptt, addr,
					   dmae_array_offset + 1,
					   size, array_data,
					   b_must_dmae, b_can_dmae);
		break;
	}

	return rc;
}

/* init_ops write command */
static enum _ecore_status_t ecore_init_cmd_wr(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      struct init_write_op *p_cmd,
					      bool b_can_dmae)
{
	u32 data = OSAL_LE32_TO_CPU(p_cmd->data);
	bool b_must_dmae = GET_FIELD(data, INIT_WRITE_OP_WIDE_BUS);
	u32 addr = GET_FIELD(data, INIT_WRITE_OP_ADDRESS) << 2;
	enum _ecore_status_t rc	= ECORE_SUCCESS;

	/* Sanitize */
	if (b_must_dmae && !b_can_dmae) {
		DP_NOTICE(p_hwfn, true,
			  "Need to write to %08x for Wide-bus but DMAE isn't allowed\n",
			  addr);
		return ECORE_INVAL;
	}

	switch (GET_FIELD(data, INIT_WRITE_OP_SOURCE)) {
	case INIT_SRC_INLINE:
		data = OSAL_LE32_TO_CPU(p_cmd->args.inline_val);
		ecore_wr(p_hwfn, p_ptt, addr, data);
		break;
	case INIT_SRC_ZEROS:
		data = OSAL_LE32_TO_CPU(p_cmd->args.zeros_count);
		if (b_must_dmae || (b_can_dmae && (data >= 64)))
			rc = ecore_init_fill_dmae(p_hwfn, p_ptt, addr, data);
		else
			ecore_init_fill(p_hwfn, p_ptt, addr, 0, data);
		break;
	case INIT_SRC_ARRAY:
		rc = ecore_init_cmd_array(p_hwfn, p_ptt, p_cmd,
					  b_must_dmae, b_can_dmae);
		break;
	case INIT_SRC_RUNTIME:
		rc = ecore_init_rt(p_hwfn, p_ptt, addr,
				   OSAL_LE16_TO_CPU(p_cmd->args.runtime.offset),
				   OSAL_LE16_TO_CPU(p_cmd->args.runtime.size),
				   b_must_dmae);
		break;
	}

	return rc;
}

static OSAL_INLINE bool comp_eq(u32 val, u32 expected_val)
{
	return (val == expected_val);
}

static OSAL_INLINE bool comp_and(u32 val, u32 expected_val)
{
	return (val & expected_val) == expected_val;
}

static OSAL_INLINE bool comp_or(u32 val, u32 expected_val)
{
	return (val | expected_val) > 0;
}

/* init_ops read/poll commands */
static void ecore_init_cmd_rd(struct ecore_hwfn *p_hwfn,
			      struct ecore_ptt *p_ptt,
			      struct init_read_op *cmd)
{
	bool (*comp_check)(u32 val, u32 expected_val);
	u32 delay = ECORE_INIT_POLL_PERIOD_US, val;
	u32 data, addr, poll;
	int i;

	data = OSAL_LE32_TO_CPU(cmd->op_data);
	addr = GET_FIELD(data, INIT_READ_OP_ADDRESS) << 2;
	poll = GET_FIELD(data, INIT_READ_OP_POLL_TYPE);

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_EMUL(p_hwfn->p_dev))
		delay *= 100;
#endif

	val = ecore_rd(p_hwfn, p_ptt, addr);

	if (poll == INIT_POLL_NONE)
		return;

	switch (poll) {
	case INIT_POLL_EQ:
		comp_check = comp_eq;
		break;
	case INIT_POLL_OR:
		comp_check = comp_or;
		break;
	case INIT_POLL_AND:
		comp_check = comp_and;
		break;
	default:
		DP_ERR(p_hwfn, "Invalid poll comparison type %08x\n",
		       cmd->op_data);
		return;
	}

	data = OSAL_LE32_TO_CPU(cmd->expected_val);
	for (i = 0;
	     i < ECORE_INIT_MAX_POLL_COUNT && !comp_check(val, data);
	     i++) {
		OSAL_UDELAY(delay);
		val = ecore_rd(p_hwfn, p_ptt, addr);
	}

	if (i == ECORE_INIT_MAX_POLL_COUNT)
		DP_ERR(p_hwfn, "Timeout when polling reg: 0x%08x [ Waiting-for: %08x Got: %08x (comparison %08x)]\n",
		       addr,
		       OSAL_LE32_TO_CPU(cmd->expected_val), val,
		       OSAL_LE32_TO_CPU(cmd->op_data));
}

/* init_ops callbacks entry point */
static enum _ecore_status_t ecore_init_cmd_cb(struct ecore_hwfn *p_hwfn,
					      struct ecore_ptt *p_ptt,
					      struct init_callback_op *p_cmd)
{
	enum _ecore_status_t rc;

	switch (p_cmd->callback_id) {
	case DMAE_READY_CB:
		rc = ecore_dmae_sanity(p_hwfn, p_ptt, "engine_phase");
		break;
	default:
		DP_NOTICE(p_hwfn, false, "Unexpected init op callback ID %d\n",
			  p_cmd->callback_id);
		return ECORE_INVAL;
	}

	return rc;
}

static u8 ecore_init_cmd_mode_match(struct ecore_hwfn *p_hwfn,
				    u16 *p_offset, int modes)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	const u8 *modes_tree_buf;
	u8 arg1, arg2, tree_val;

	modes_tree_buf = p_dev->fw_data->modes_tree_buf;
	tree_val = modes_tree_buf[(*p_offset)++];
	switch(tree_val) {
	case INIT_MODE_OP_NOT:
		return ecore_init_cmd_mode_match(p_hwfn, p_offset, modes) ^ 1;
	case INIT_MODE_OP_OR:
		arg1 = ecore_init_cmd_mode_match(p_hwfn, p_offset, modes);
		arg2 = ecore_init_cmd_mode_match(p_hwfn, p_offset, modes);
		return arg1 | arg2;
	case INIT_MODE_OP_AND:
		arg1 = ecore_init_cmd_mode_match(p_hwfn, p_offset, modes);
		arg2 = ecore_init_cmd_mode_match(p_hwfn, p_offset, modes);
		return arg1 & arg2;
	default:
		tree_val -= MAX_INIT_MODE_OPS;
		return (modes & (1 << tree_val)) ? 1 : 0;
	}
}

static u32 ecore_init_cmd_mode(struct ecore_hwfn *p_hwfn,
			       struct init_if_mode_op *p_cmd, int modes)
{
	u16 offset = OSAL_LE16_TO_CPU(p_cmd->modes_buf_offset);

	if (ecore_init_cmd_mode_match(p_hwfn, &offset, modes))
		return 0;
	else
		return GET_FIELD(OSAL_LE32_TO_CPU(p_cmd->op_data),
				 INIT_IF_MODE_OP_CMD_OFFSET);
}

static u32 ecore_init_cmd_phase(struct init_if_phase_op *p_cmd,
				u32 phase, u32 phase_id)
{
	u32 data = OSAL_LE32_TO_CPU(p_cmd->phase_data);
	u32 op_data = OSAL_LE32_TO_CPU(p_cmd->op_data);

	if (!(GET_FIELD(data, INIT_IF_PHASE_OP_PHASE) == phase &&
	      (GET_FIELD(data, INIT_IF_PHASE_OP_PHASE_ID) == ANY_PHASE_ID ||
	       GET_FIELD(data, INIT_IF_PHASE_OP_PHASE_ID) == phase_id)))
		return GET_FIELD(op_data, INIT_IF_PHASE_OP_CMD_OFFSET);
	else
		return 0;
}

enum _ecore_status_t ecore_init_run(struct ecore_hwfn *p_hwfn,
				    struct ecore_ptt *p_ptt,
				    int phase,
				    int phase_id,
				    int modes)
{
	struct ecore_dev *p_dev = p_hwfn->p_dev;
	u32 cmd_num, num_init_ops;
	union init_op *init_ops;
	bool b_dmae = false;
	enum _ecore_status_t rc = ECORE_SUCCESS;

	num_init_ops = p_dev->fw_data->init_ops_size;
	init_ops = p_dev->fw_data->init_ops;

#ifdef CONFIG_ECORE_ZIPPED_FW
	p_hwfn->unzip_buf = OSAL_ZALLOC(p_hwfn->p_dev, GFP_ATOMIC,
					MAX_ZIPPED_SIZE * 4);
	if (!p_hwfn->unzip_buf) {
		DP_NOTICE(p_hwfn, true, "Failed to allocate unzip buffer\n");
		return ECORE_NOMEM;
	}
#endif

	for (cmd_num = 0; cmd_num < num_init_ops; cmd_num++) {
		union init_op *cmd = &init_ops[cmd_num];
		u32 data = OSAL_LE32_TO_CPU(cmd->raw.op_data);

		switch (GET_FIELD(data, INIT_CALLBACK_OP_OP)) {
		case INIT_OP_WRITE:
			rc = ecore_init_cmd_wr(p_hwfn, p_ptt, &cmd->write,
					       b_dmae);
			break;

		case INIT_OP_READ:
			ecore_init_cmd_rd(p_hwfn, p_ptt, &cmd->read);
			break;

		case INIT_OP_IF_MODE:
			cmd_num += ecore_init_cmd_mode(p_hwfn, &cmd->if_mode,
						       modes);
			break;
		case INIT_OP_IF_PHASE:
			cmd_num += ecore_init_cmd_phase(&cmd->if_phase, phase,
							phase_id);
			b_dmae = GET_FIELD(data,
					   INIT_IF_PHASE_OP_DMAE_ENABLE);
			break;
		case INIT_OP_DELAY:
			/* ecore_init_run is always invoked from
			 * sleep-able context
			 */
			OSAL_UDELAY(cmd->delay.delay);
			break;

		case INIT_OP_CALLBACK:
			rc = ecore_init_cmd_cb(p_hwfn, p_ptt, &cmd->callback);
			break;
		}

		if (rc)
			break;
	}
#ifdef CONFIG_ECORE_ZIPPED_FW
	OSAL_FREE(p_hwfn->p_dev, p_hwfn->unzip_buf);
	p_hwfn->unzip_buf = OSAL_NULL;
#endif
	return rc;
}

void ecore_gtt_init(struct ecore_hwfn *p_hwfn,
		    struct ecore_ptt *p_ptt)
{
	u32 gtt_base;
	u32 i;

#ifndef ASIC_ONLY
	if (CHIP_REV_IS_SLOW(p_hwfn->p_dev)) {
		/* This is done by MFW on ASIC; regardless, this should only
		 * be done once per chip [i.e., common]. Implementation is
		 * not too bright, but it should work on the simple FPGA/EMUL
		 * scenarios.
		 */
		static bool initialized = false;
		int poll_cnt = 500;
		u32 val;

		/* initialize PTT/GTT (poll for completion) */
		if (!initialized) {
			ecore_wr(p_hwfn, p_ptt,
				 PGLUE_B_REG_START_INIT_PTT_GTT, 1);
			initialized = true;
		}

		do {
			/* ptt might be overrided by HW until this is done */
			OSAL_UDELAY(10);
			ecore_ptt_invalidate(p_hwfn);
			val = ecore_rd(p_hwfn, p_ptt,
				       PGLUE_B_REG_INIT_DONE_PTT_GTT);
		} while ((val != 1) && --poll_cnt);

		if (!poll_cnt)
			DP_ERR(p_hwfn, "PGLUE_B_REG_INIT_DONE didn't complete\n");
	}
#endif

	/* Set the global windows */
	gtt_base = PXP_PF_WINDOW_ADMIN_START + PXP_PF_WINDOW_ADMIN_GLOBAL_START;

	for (i = 0; i < OSAL_ARRAY_SIZE(pxp_global_win); i++)
		if (pxp_global_win[i])
			REG_WR(p_hwfn, gtt_base + i * PXP_GLOBAL_ENTRY_SIZE,
			       pxp_global_win[i]);
}

enum _ecore_status_t ecore_init_fw_data(struct ecore_dev *p_dev,
#ifdef CONFIG_ECORE_BINARY_FW
					const u8 *fw_data)
#else
					const u8 OSAL_UNUSED *fw_data)
#endif
{
	struct ecore_fw_data *fw = p_dev->fw_data;

#ifdef CONFIG_ECORE_BINARY_FW
	struct bin_buffer_hdr *buf_hdr;
	u32 offset, len;

	if (!fw_data) {
		DP_NOTICE(p_dev, true, "Invalid fw data\n");
		return ECORE_INVAL;
	}

	buf_hdr = (struct bin_buffer_hdr *)fw_data;

	offset = buf_hdr[BIN_BUF_INIT_FW_VER_INFO].offset;
	fw->fw_ver_info = (struct fw_ver_info *)(fw_data + offset);

	offset = buf_hdr[BIN_BUF_INIT_CMD].offset;
	fw->init_ops = (union init_op *)(fw_data + offset);

	offset = buf_hdr[BIN_BUF_INIT_VAL].offset;
	fw->arr_data = (u32 *)(fw_data + offset);

	offset = buf_hdr[BIN_BUF_INIT_MODE_TREE].offset;
	fw->modes_tree_buf = (u8 *)(fw_data + offset);
	len = buf_hdr[BIN_BUF_INIT_CMD].length;
	fw->init_ops_size = len / sizeof(struct init_raw_op);
#else
	fw->init_ops = (union init_op *)init_ops;
	fw->arr_data = (u32 *)init_val;
	fw->modes_tree_buf = (u8 *)modes_tree_buf;
	fw->init_ops_size = init_ops_size;
#endif

	return ECORE_SUCCESS;
}
