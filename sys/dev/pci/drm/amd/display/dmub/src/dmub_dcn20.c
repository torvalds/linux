/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "../dmub_srv.h"
#include "dmub_reg.h"
#include "dmub_dcn20.h"

#include "dcn/dcn_2_0_0_offset.h"
#include "dcn/dcn_2_0_0_sh_mask.h"
#include "soc15_hw_ip.h"
#include "vega10_ip_offset.h"

#define BASE_INNER(seg) DCN_BASE__INST0_SEG##seg
#define CTX dmub
#define REGS dmub->regs

/* Registers. */

const struct dmub_srv_common_regs dmub_srv_dcn20_regs = {
#define DMUB_SR(reg) REG_OFFSET(reg),
	{
		DMUB_COMMON_REGS()
		DMCUB_INTERNAL_REGS()
	},
#undef DMUB_SR

#define DMUB_SF(reg, field) FD_MASK(reg, field),
	{ DMUB_COMMON_FIELDS() },
#undef DMUB_SF

#define DMUB_SF(reg, field) FD_SHIFT(reg, field),
	{ DMUB_COMMON_FIELDS() },
#undef DMUB_SF
};

/* Shared functions. */

static void dmub_dcn20_get_fb_base_offset(struct dmub_srv *dmub,
					  uint64_t *fb_base,
					  uint64_t *fb_offset)
{
	uint32_t tmp;

	if (dmub->fb_base || dmub->fb_offset) {
		*fb_base = dmub->fb_base;
		*fb_offset = dmub->fb_offset;
		return;
	}

	REG_GET(DCN_VM_FB_LOCATION_BASE, FB_BASE, &tmp);
	*fb_base = (uint64_t)tmp << 24;

	REG_GET(DCN_VM_FB_OFFSET, FB_OFFSET, &tmp);
	*fb_offset = (uint64_t)tmp << 24;
}

static inline void dmub_dcn20_translate_addr(const union dmub_addr *addr_in,
					     uint64_t fb_base,
					     uint64_t fb_offset,
					     union dmub_addr *addr_out)
{
	addr_out->quad_part = addr_in->quad_part - fb_base + fb_offset;
}

bool dmub_dcn20_use_cached_inbox(struct dmub_srv *dmub)
{
	/* Cached inbox is not supported in this fw version range */
	return !(dmub->fw_version >= DMUB_FW_VERSION(1, 0, 0) &&
		 dmub->fw_version <= DMUB_FW_VERSION(1, 10, 0));
}

void dmub_dcn20_reset(struct dmub_srv *dmub)
{
	union dmub_gpint_data_register cmd;
	const uint32_t timeout = 30;
	uint32_t in_reset, scratch, i;

	REG_GET(DMCUB_CNTL, DMCUB_SOFT_RESET, &in_reset);

	if (in_reset == 0) {
		cmd.bits.status = 1;
		cmd.bits.command_code = DMUB_GPINT__STOP_FW;
		cmd.bits.param = 0;

		dmub->hw_funcs.set_gpint(dmub, cmd);

		/**
		 * Timeout covers both the ACK and the wait
		 * for remaining work to finish.
		 *
		 * This is mostly bound by the PHY disable sequence.
		 * Each register check will be greater than 1us, so
		 * don't bother using udelay.
		 */

		for (i = 0; i < timeout; ++i) {
			if (dmub->hw_funcs.is_gpint_acked(dmub, cmd))
				break;
		}

		for (i = 0; i < timeout; ++i) {
			scratch = dmub->hw_funcs.get_gpint_response(dmub);
			if (scratch == DMUB_GPINT__STOP_FW_RESPONSE)
				break;
		}

		/* Clear the GPINT command manually so we don't reset again. */
		cmd.all = 0;
		dmub->hw_funcs.set_gpint(dmub, cmd);

		/* Force reset in case we timed out, DMCUB is likely hung. */
	}

	REG_UPDATE(DMCUB_CNTL, DMCUB_SOFT_RESET, 1);
	REG_UPDATE(DMCUB_CNTL, DMCUB_ENABLE, 0);
	REG_UPDATE(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET, 1);
	REG_WRITE(DMCUB_INBOX1_RPTR, 0);
	REG_WRITE(DMCUB_INBOX1_WPTR, 0);
	REG_WRITE(DMCUB_OUTBOX1_RPTR, 0);
	REG_WRITE(DMCUB_OUTBOX1_WPTR, 0);
	REG_WRITE(DMCUB_SCRATCH0, 0);
}

void dmub_dcn20_reset_release(struct dmub_srv *dmub)
{
	REG_UPDATE(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET, 0);
	REG_WRITE(DMCUB_SCRATCH15, dmub->psp_version & 0x001100FF);
	REG_UPDATE_2(DMCUB_CNTL, DMCUB_ENABLE, 1, DMCUB_TRACEPORT_EN, 1);
	REG_UPDATE(DMCUB_CNTL, DMCUB_SOFT_RESET, 0);
}

void dmub_dcn20_backdoor_load(struct dmub_srv *dmub,
			      const struct dmub_window *cw0,
			      const struct dmub_window *cw1)
{
	union dmub_addr offset;
	uint64_t fb_base, fb_offset;

	dmub_dcn20_get_fb_base_offset(dmub, &fb_base, &fb_offset);

	REG_UPDATE(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 1);
	REG_UPDATE_2(DMCUB_MEM_CNTL, DMCUB_MEM_READ_SPACE, 0x3,
		     DMCUB_MEM_WRITE_SPACE, 0x3);

	dmub_dcn20_translate_addr(&cw0->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW0_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW0_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW0_BASE_ADDRESS, cw0->region.base);
	REG_SET_2(DMCUB_REGION3_CW0_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW0_TOP_ADDRESS, cw0->region.top,
		  DMCUB_REGION3_CW0_ENABLE, 1);

	dmub_dcn20_translate_addr(&cw1->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW1_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW1_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW1_BASE_ADDRESS, cw1->region.base);
	REG_SET_2(DMCUB_REGION3_CW1_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW1_TOP_ADDRESS, cw1->region.top,
		  DMCUB_REGION3_CW1_ENABLE, 1);

	REG_UPDATE_2(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 0, DMCUB_MEM_UNIT_ID,
		     0x20);
}

void dmub_dcn20_setup_windows(struct dmub_srv *dmub,
			      const struct dmub_window *cw2,
			      const struct dmub_window *cw3,
			      const struct dmub_window *cw4,
			      const struct dmub_window *cw5,
			      const struct dmub_window *cw6,
			      const struct dmub_window *region6)
{
	union dmub_addr offset;
	uint64_t fb_base, fb_offset;

	dmub_dcn20_get_fb_base_offset(dmub, &fb_base, &fb_offset);

	if (cw2->region.base != cw2->region.top) {
		dmub_dcn20_translate_addr(&cw2->offset, fb_base, fb_offset,
					  &offset);

		REG_WRITE(DMCUB_REGION3_CW2_OFFSET, offset.u.low_part);
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET_HIGH, offset.u.high_part);
		REG_WRITE(DMCUB_REGION3_CW2_BASE_ADDRESS, cw2->region.base);
		REG_SET_2(DMCUB_REGION3_CW2_TOP_ADDRESS, 0,
			  DMCUB_REGION3_CW2_TOP_ADDRESS, cw2->region.top,
			  DMCUB_REGION3_CW2_ENABLE, 1);
	} else {
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET, 0);
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET_HIGH, 0);
		REG_WRITE(DMCUB_REGION3_CW2_BASE_ADDRESS, 0);
		REG_WRITE(DMCUB_REGION3_CW2_TOP_ADDRESS, 0);
	}

	dmub_dcn20_translate_addr(&cw3->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW3_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW3_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW3_BASE_ADDRESS, cw3->region.base);
	REG_SET_2(DMCUB_REGION3_CW3_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW3_TOP_ADDRESS, cw3->region.top,
		  DMCUB_REGION3_CW3_ENABLE, 1);

	/* TODO: Move this to CW4. */
	dmub_dcn20_translate_addr(&cw4->offset, fb_base, fb_offset, &offset);

	/* New firmware can support CW4. */
	if (dmub_dcn20_use_cached_inbox(dmub)) {
		REG_WRITE(DMCUB_REGION3_CW4_OFFSET, offset.u.low_part);
		REG_WRITE(DMCUB_REGION3_CW4_OFFSET_HIGH, offset.u.high_part);
		REG_WRITE(DMCUB_REGION3_CW4_BASE_ADDRESS, cw4->region.base);
		REG_SET_2(DMCUB_REGION3_CW4_TOP_ADDRESS, 0,
			  DMCUB_REGION3_CW4_TOP_ADDRESS, cw4->region.top,
			  DMCUB_REGION3_CW4_ENABLE, 1);
	} else {
		REG_WRITE(DMCUB_REGION4_OFFSET, offset.u.low_part);
		REG_WRITE(DMCUB_REGION4_OFFSET_HIGH, offset.u.high_part);
		REG_SET_2(DMCUB_REGION4_TOP_ADDRESS, 0,
			  DMCUB_REGION4_TOP_ADDRESS,
			  cw4->region.top - cw4->region.base - 1,
			  DMCUB_REGION4_ENABLE, 1);
	}

	dmub_dcn20_translate_addr(&cw5->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW5_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW5_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW5_BASE_ADDRESS, cw5->region.base);
	REG_SET_2(DMCUB_REGION3_CW5_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW5_TOP_ADDRESS, cw5->region.top,
		  DMCUB_REGION3_CW5_ENABLE, 1);

	REG_WRITE(DMCUB_REGION5_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION5_OFFSET_HIGH, offset.u.high_part);
	REG_SET_2(DMCUB_REGION5_TOP_ADDRESS, 0,
		  DMCUB_REGION5_TOP_ADDRESS,
		  cw5->region.top - cw5->region.base - 1,
		  DMCUB_REGION5_ENABLE, 1);

	dmub_dcn20_translate_addr(&cw6->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW6_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW6_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW6_BASE_ADDRESS, cw6->region.base);
	REG_SET_2(DMCUB_REGION3_CW6_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW6_TOP_ADDRESS, cw6->region.top,
		  DMCUB_REGION3_CW6_ENABLE, 1);
}

void dmub_dcn20_setup_mailbox(struct dmub_srv *dmub,
			      const struct dmub_region *inbox1)
{
	/* New firmware can support CW4 for the inbox. */
	if (dmub_dcn20_use_cached_inbox(dmub))
		REG_WRITE(DMCUB_INBOX1_BASE_ADDRESS, inbox1->base);
	else
		REG_WRITE(DMCUB_INBOX1_BASE_ADDRESS, 0x80000000);

	REG_WRITE(DMCUB_INBOX1_SIZE, inbox1->top - inbox1->base);
}

uint32_t dmub_dcn20_get_inbox1_wptr(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_INBOX1_WPTR);
}

uint32_t dmub_dcn20_get_inbox1_rptr(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_INBOX1_RPTR);
}

void dmub_dcn20_set_inbox1_wptr(struct dmub_srv *dmub, uint32_t wptr_offset)
{
	REG_WRITE(DMCUB_INBOX1_WPTR, wptr_offset);
}

void dmub_dcn20_setup_out_mailbox(struct dmub_srv *dmub,
			      const struct dmub_region *outbox1)
{
	/* New firmware can support CW4 for the outbox. */
	if (dmub_dcn20_use_cached_inbox(dmub))
		REG_WRITE(DMCUB_OUTBOX1_BASE_ADDRESS, outbox1->base);
	else
		REG_WRITE(DMCUB_OUTBOX1_BASE_ADDRESS, 0x80002000);

	REG_WRITE(DMCUB_OUTBOX1_SIZE, outbox1->top - outbox1->base);
}

uint32_t dmub_dcn20_get_outbox1_wptr(struct dmub_srv *dmub)
{
	/**
	 * outbox1 wptr register is accessed without locks (dal & dc)
	 * and to be called only by dmub_srv_stat_get_notification()
	 */
	return REG_READ(DMCUB_OUTBOX1_WPTR);
}

void dmub_dcn20_set_outbox1_rptr(struct dmub_srv *dmub, uint32_t rptr_offset)
{
	/**
	 * outbox1 rptr register is accessed without locks (dal & dc)
	 * and to be called only by dmub_srv_stat_get_notification()
	 */
	REG_WRITE(DMCUB_OUTBOX1_RPTR, rptr_offset);
}

void dmub_dcn20_setup_outbox0(struct dmub_srv *dmub,
			      const struct dmub_region *outbox0)
{
	REG_WRITE(DMCUB_OUTBOX0_BASE_ADDRESS, outbox0->base);

	REG_WRITE(DMCUB_OUTBOX0_SIZE, outbox0->top - outbox0->base);
}

uint32_t dmub_dcn20_get_outbox0_wptr(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_OUTBOX0_WPTR);
}

void dmub_dcn20_set_outbox0_rptr(struct dmub_srv *dmub, uint32_t rptr_offset)
{
	REG_WRITE(DMCUB_OUTBOX0_RPTR, rptr_offset);
}

bool dmub_dcn20_is_hw_init(struct dmub_srv *dmub)
{
	uint32_t is_hw_init;

	REG_GET(DMCUB_CNTL, DMCUB_ENABLE, &is_hw_init);

	return is_hw_init != 0;
}

bool dmub_dcn20_is_supported(struct dmub_srv *dmub)
{
	uint32_t supported = 0;

	REG_GET(CC_DC_PIPE_DIS, DC_DMCUB_ENABLE, &supported);

	return supported;
}

void dmub_dcn20_set_gpint(struct dmub_srv *dmub,
			  union dmub_gpint_data_register reg)
{
	REG_WRITE(DMCUB_GPINT_DATAIN1, reg.all);
}

bool dmub_dcn20_is_gpint_acked(struct dmub_srv *dmub,
			       union dmub_gpint_data_register reg)
{
	union dmub_gpint_data_register test;

	reg.bits.status = 0;
	test.all = REG_READ(DMCUB_GPINT_DATAIN1);

	return test.all == reg.all;
}

uint32_t dmub_dcn20_get_gpint_response(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_SCRATCH7);
}

union dmub_fw_boot_status dmub_dcn20_get_fw_boot_status(struct dmub_srv *dmub)
{
	union dmub_fw_boot_status status;

	status.all = REG_READ(DMCUB_SCRATCH0);
	return status;
}

void dmub_dcn20_enable_dmub_boot_options(struct dmub_srv *dmub, const struct dmub_srv_hw_params *params)
{
	union dmub_fw_boot_options boot_options = {0};

	REG_WRITE(DMCUB_SCRATCH14, boot_options.all);
}

void dmub_dcn20_skip_dmub_panel_power_sequence(struct dmub_srv *dmub, bool skip)
{
	union dmub_fw_boot_options boot_options;
	boot_options.all = REG_READ(DMCUB_SCRATCH14);
	boot_options.bits.skip_phy_init_panel_sequence = skip;
	REG_WRITE(DMCUB_SCRATCH14, boot_options.all);
}

uint32_t dmub_dcn20_get_current_time(struct dmub_srv *dmub)
{
	return REG_READ(DMCUB_TIMER_CURRENT);
}

void dmub_dcn20_get_diagnostic_data(struct dmub_srv *dmub, struct dmub_diagnostic_data *diag_data)
{
	uint32_t is_dmub_enabled, is_soft_reset, is_sec_reset;
	uint32_t is_traceport_enabled, is_cw0_enabled, is_cw6_enabled;

	if (!dmub || !diag_data)
		return;

	memset(diag_data, 0, sizeof(*diag_data));

	diag_data->dmcub_version = dmub->fw_version;

	diag_data->scratch[0] = REG_READ(DMCUB_SCRATCH0);
	diag_data->scratch[1] = REG_READ(DMCUB_SCRATCH1);
	diag_data->scratch[2] = REG_READ(DMCUB_SCRATCH2);
	diag_data->scratch[3] = REG_READ(DMCUB_SCRATCH3);
	diag_data->scratch[4] = REG_READ(DMCUB_SCRATCH4);
	diag_data->scratch[5] = REG_READ(DMCUB_SCRATCH5);
	diag_data->scratch[6] = REG_READ(DMCUB_SCRATCH6);
	diag_data->scratch[7] = REG_READ(DMCUB_SCRATCH7);
	diag_data->scratch[8] = REG_READ(DMCUB_SCRATCH8);
	diag_data->scratch[9] = REG_READ(DMCUB_SCRATCH9);
	diag_data->scratch[10] = REG_READ(DMCUB_SCRATCH10);
	diag_data->scratch[11] = REG_READ(DMCUB_SCRATCH11);
	diag_data->scratch[12] = REG_READ(DMCUB_SCRATCH12);
	diag_data->scratch[13] = REG_READ(DMCUB_SCRATCH13);
	diag_data->scratch[14] = REG_READ(DMCUB_SCRATCH14);
	diag_data->scratch[15] = REG_READ(DMCUB_SCRATCH15);

	diag_data->undefined_address_fault_addr = REG_READ(DMCUB_UNDEFINED_ADDRESS_FAULT_ADDR);
	diag_data->inst_fetch_fault_addr = REG_READ(DMCUB_INST_FETCH_FAULT_ADDR);
	diag_data->data_write_fault_addr = REG_READ(DMCUB_DATA_WRITE_FAULT_ADDR);

	diag_data->inbox1_rptr = REG_READ(DMCUB_INBOX1_RPTR);
	diag_data->inbox1_wptr = REG_READ(DMCUB_INBOX1_WPTR);
	diag_data->inbox1_size = REG_READ(DMCUB_INBOX1_SIZE);

	diag_data->inbox0_rptr = REG_READ(DMCUB_INBOX0_RPTR);
	diag_data->inbox0_wptr = REG_READ(DMCUB_INBOX0_WPTR);
	diag_data->inbox0_size = REG_READ(DMCUB_INBOX0_SIZE);

	REG_GET(DMCUB_CNTL, DMCUB_ENABLE, &is_dmub_enabled);
	diag_data->is_dmcub_enabled = is_dmub_enabled;

	REG_GET(DMCUB_CNTL, DMCUB_SOFT_RESET, &is_soft_reset);
	diag_data->is_dmcub_soft_reset = is_soft_reset;

	REG_GET(DMCUB_SEC_CNTL, DMCUB_SEC_RESET_STATUS, &is_sec_reset);
	diag_data->is_dmcub_secure_reset = is_sec_reset;

	REG_GET(DMCUB_CNTL, DMCUB_TRACEPORT_EN, &is_traceport_enabled);
	diag_data->is_traceport_en  = is_traceport_enabled;

	REG_GET(DMCUB_REGION3_CW0_TOP_ADDRESS, DMCUB_REGION3_CW0_ENABLE, &is_cw0_enabled);
	diag_data->is_cw0_enabled = is_cw0_enabled;

	REG_GET(DMCUB_REGION3_CW6_TOP_ADDRESS, DMCUB_REGION3_CW6_ENABLE, &is_cw6_enabled);
	diag_data->is_cw6_enabled = is_cw6_enabled;
	diag_data->timeout_info = dmub->debug;
}
