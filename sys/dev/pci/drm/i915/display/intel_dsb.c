// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsb.h"
#include "intel_dsb_buffer.h"
#include "intel_dsb_regs.h"
#include "intel_vblank.h"
#include "intel_vrr.h"
#include "skl_watermark.h"

#define CACHELINE_BYTES 64

struct intel_dsb {
	enum intel_dsb_id id;

	struct intel_dsb_buffer dsb_buf;
	struct intel_crtc *crtc;

	/*
	 * maximum number of dwords the buffer will hold.
	 */
	unsigned int size;

	/*
	 * free_pos will point the first free dword and
	 * help in calculating tail of command buffer.
	 */
	unsigned int free_pos;

	/*
	 * ins_start_offset will help to store start dword of the dsb
	 * instuction and help in identifying the batch of auto-increment
	 * register.
	 */
	unsigned int ins_start_offset;

	u32 chicken;
	int hw_dewake_scanline;
};

/**
 * DOC: DSB
 *
 * A DSB (Display State Buffer) is a queue of MMIO instructions in the memory
 * which can be offloaded to DSB HW in Display Controller. DSB HW is a DMA
 * engine that can be programmed to download the DSB from memory.
 * It allows driver to batch submit display HW programming. This helps to
 * reduce loading time and CPU activity, thereby making the context switch
 * faster. DSB Support added from Gen12 Intel graphics based platform.
 *
 * DSB's can access only the pipe, plane, and transcoder Data Island Packet
 * registers.
 *
 * DSB HW can support only register writes (both indexed and direct MMIO
 * writes). There are no registers reads possible with DSB HW engine.
 */

/* DSB opcodes. */
#define DSB_OPCODE_SHIFT		24
#define DSB_OPCODE_NOOP			0x0
#define DSB_OPCODE_MMIO_WRITE		0x1
#define   DSB_BYTE_EN			0xf
#define   DSB_BYTE_EN_SHIFT		20
#define   DSB_REG_VALUE_MASK		0xfffff
#define DSB_OPCODE_WAIT_USEC		0x2
#define DSB_OPCODE_WAIT_SCANLINE	0x3
#define DSB_OPCODE_WAIT_VBLANKS		0x4
#define DSB_OPCODE_WAIT_DSL_IN		0x5
#define DSB_OPCODE_WAIT_DSL_OUT		0x6
#define   DSB_SCANLINE_UPPER_SHIFT	20
#define   DSB_SCANLINE_LOWER_SHIFT	0
#define DSB_OPCODE_INTERRUPT		0x7
#define DSB_OPCODE_INDEXED_WRITE	0x9
/* see DSB_REG_VALUE_MASK */
#define DSB_OPCODE_POLL			0xA
/* see DSB_REG_VALUE_MASK */

static bool pre_commit_is_vrr_active(struct intel_atomic_state *state,
				     struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/* VRR will be enabled afterwards, if necessary */
	if (intel_crtc_needs_modeset(new_crtc_state))
		return false;

	/* VRR will have been disabled during intel_pre_plane_update() */
	return old_crtc_state->vrr.enable && !intel_crtc_vrr_disabling(state, crtc);
}

static const struct intel_crtc_state *
pre_commit_crtc_state(struct intel_atomic_state *state,
		      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);

	/*
	 * During fastsets/etc. the transcoder is still
	 * running with the old timings at this point.
	 */
	if (intel_crtc_needs_modeset(new_crtc_state))
		return new_crtc_state;
	else
		return old_crtc_state;
}

static int dsb_vtotal(struct intel_atomic_state *state,
		      struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state = pre_commit_crtc_state(state, crtc);

	if (pre_commit_is_vrr_active(state, crtc))
		return crtc_state->vrr.vmax;
	else
		return intel_mode_vtotal(&crtc_state->hw.adjusted_mode);
}

static int dsb_dewake_scanline_start(struct intel_atomic_state *state,
				     struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state = pre_commit_crtc_state(state, crtc);
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	unsigned int latency = skl_watermark_max_latency(i915, 0);

	return intel_mode_vdisplay(&crtc_state->hw.adjusted_mode) -
		intel_usecs_to_scanlines(&crtc_state->hw.adjusted_mode, latency);
}

static int dsb_dewake_scanline_end(struct intel_atomic_state *state,
				   struct intel_crtc *crtc)
{
	const struct intel_crtc_state *crtc_state = pre_commit_crtc_state(state, crtc);

	return intel_mode_vdisplay(&crtc_state->hw.adjusted_mode);
}

static int dsb_scanline_to_hw(struct intel_atomic_state *state,
			      struct intel_crtc *crtc, int scanline)
{
	const struct intel_crtc_state *crtc_state = pre_commit_crtc_state(state, crtc);
	int vtotal = dsb_vtotal(state, crtc);

	return (scanline + vtotal - intel_crtc_scanline_offset(crtc_state)) % vtotal;
}

static u32 dsb_chicken(struct intel_atomic_state *state,
		       struct intel_crtc *crtc)
{
	if (pre_commit_is_vrr_active(state, crtc))
		return DSB_SKIP_WAITS_EN |
			DSB_CTRL_WAIT_SAFE_WINDOW |
			DSB_CTRL_NO_WAIT_VBLANK |
			DSB_INST_WAIT_SAFE_WINDOW |
			DSB_INST_NO_WAIT_VBLANK;
	else
		return DSB_SKIP_WAITS_EN;
}

static bool assert_dsb_has_room(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct intel_display *display = to_intel_display(crtc->base.dev);

	/* each instruction is 2 dwords */
	return !drm_WARN(display->drm, dsb->free_pos > dsb->size - 2,
			 "[CRTC:%d:%s] DSB %d buffer overflow\n",
			 crtc->base.base.id, crtc->base.name, dsb->id);
}

static void intel_dsb_dump(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct intel_display *display = to_intel_display(crtc->base.dev);
	int i;

	drm_dbg_kms(display->drm, "[CRTC:%d:%s] DSB %d commands {\n",
		    crtc->base.base.id, crtc->base.name, dsb->id);
	for (i = 0; i < ALIGN(dsb->free_pos, 64 / 4); i += 4)
		drm_dbg_kms(display->drm,
			    " 0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n", i * 4,
			    intel_dsb_buffer_read(&dsb->dsb_buf, i),
			    intel_dsb_buffer_read(&dsb->dsb_buf, i + 1),
			    intel_dsb_buffer_read(&dsb->dsb_buf, i + 2),
			    intel_dsb_buffer_read(&dsb->dsb_buf, i + 3));
	drm_dbg_kms(display->drm, "}\n");
}

static bool is_dsb_busy(struct intel_display *display, enum pipe pipe,
			enum intel_dsb_id dsb_id)
{
	return intel_de_read_fw(display, DSB_CTRL(pipe, dsb_id)) & DSB_STATUS_BUSY;
}

static void intel_dsb_emit(struct intel_dsb *dsb, u32 ldw, u32 udw)
{
	if (!assert_dsb_has_room(dsb))
		return;

	/* Every instruction should be 8 byte aligned. */
	dsb->free_pos = ALIGN(dsb->free_pos, 2);

	dsb->ins_start_offset = dsb->free_pos;

	intel_dsb_buffer_write(&dsb->dsb_buf, dsb->free_pos++, ldw);
	intel_dsb_buffer_write(&dsb->dsb_buf, dsb->free_pos++, udw);
}

static bool intel_dsb_prev_ins_is_write(struct intel_dsb *dsb,
					u32 opcode, i915_reg_t reg)
{
	u32 prev_opcode, prev_reg;

	/*
	 * Nothing emitted yet? Must check before looking
	 * at the actual data since i915_gem_object_create_internal()
	 * does *not* give you zeroed memory!
	 */
	if (dsb->free_pos == 0)
		return false;

	prev_opcode = intel_dsb_buffer_read(&dsb->dsb_buf,
					    dsb->ins_start_offset + 1) & ~DSB_REG_VALUE_MASK;
	prev_reg =  intel_dsb_buffer_read(&dsb->dsb_buf,
					  dsb->ins_start_offset + 1) & DSB_REG_VALUE_MASK;

	return prev_opcode == opcode && prev_reg == i915_mmio_reg_offset(reg);
}

static bool intel_dsb_prev_ins_is_mmio_write(struct intel_dsb *dsb, i915_reg_t reg)
{
	/* only full byte-enables can be converted to indexed writes */
	return intel_dsb_prev_ins_is_write(dsb,
					   DSB_OPCODE_MMIO_WRITE << DSB_OPCODE_SHIFT |
					   DSB_BYTE_EN << DSB_BYTE_EN_SHIFT,
					   reg);
}

static bool intel_dsb_prev_ins_is_indexed_write(struct intel_dsb *dsb, i915_reg_t reg)
{
	return intel_dsb_prev_ins_is_write(dsb,
					   DSB_OPCODE_INDEXED_WRITE << DSB_OPCODE_SHIFT,
					   reg);
}

/**
 * intel_dsb_reg_write() - Emit register wriite to the DSB context
 * @dsb: DSB context
 * @reg: register address.
 * @val: value.
 *
 * This function is used for writing register-value pair in command
 * buffer of DSB.
 */
void intel_dsb_reg_write(struct intel_dsb *dsb,
			 i915_reg_t reg, u32 val)
{
	u32 old_val;

	/*
	 * For example the buffer will look like below for 3 dwords for auto
	 * increment register:
	 * +--------------------------------------------------------+
	 * | size = 3 | offset &| value1 | value2 | value3 | zero   |
	 * |          | opcode  |        |        |        |        |
	 * +--------------------------------------------------------+
	 * +          +         +        +        +        +        +
	 * 0          4         8        12       16       20       24
	 * Byte
	 *
	 * As every instruction is 8 byte aligned the index of dsb instruction
	 * will start always from even number while dealing with u32 array. If
	 * we are writing odd no of dwords, Zeros will be added in the end for
	 * padding.
	 */
	if (!intel_dsb_prev_ins_is_mmio_write(dsb, reg) &&
	    !intel_dsb_prev_ins_is_indexed_write(dsb, reg)) {
		intel_dsb_emit(dsb, val,
			       (DSB_OPCODE_MMIO_WRITE << DSB_OPCODE_SHIFT) |
			       (DSB_BYTE_EN << DSB_BYTE_EN_SHIFT) |
			       i915_mmio_reg_offset(reg));
	} else {
		if (!assert_dsb_has_room(dsb))
			return;

		/* convert to indexed write? */
		if (intel_dsb_prev_ins_is_mmio_write(dsb, reg)) {
			u32 prev_val = intel_dsb_buffer_read(&dsb->dsb_buf,
							     dsb->ins_start_offset + 0);

			intel_dsb_buffer_write(&dsb->dsb_buf,
					       dsb->ins_start_offset + 0, 1); /* count */
			intel_dsb_buffer_write(&dsb->dsb_buf, dsb->ins_start_offset + 1,
					       (DSB_OPCODE_INDEXED_WRITE << DSB_OPCODE_SHIFT) |
					       i915_mmio_reg_offset(reg));
			intel_dsb_buffer_write(&dsb->dsb_buf, dsb->ins_start_offset + 2, prev_val);

			dsb->free_pos++;
		}

		intel_dsb_buffer_write(&dsb->dsb_buf, dsb->free_pos++, val);
		/* Update the count */
		old_val = intel_dsb_buffer_read(&dsb->dsb_buf, dsb->ins_start_offset);
		intel_dsb_buffer_write(&dsb->dsb_buf, dsb->ins_start_offset, old_val + 1);

		/* if number of data words is odd, then the last dword should be 0.*/
		if (dsb->free_pos & 0x1)
			intel_dsb_buffer_write(&dsb->dsb_buf, dsb->free_pos, 0);
	}
}

static u32 intel_dsb_mask_to_byte_en(u32 mask)
{
	return (!!(mask & 0xff000000) << 3 |
		!!(mask & 0x00ff0000) << 2 |
		!!(mask & 0x0000ff00) << 1 |
		!!(mask & 0x000000ff) << 0);
}

/* Note: mask implemented via byte enables! */
void intel_dsb_reg_write_masked(struct intel_dsb *dsb,
				i915_reg_t reg, u32 mask, u32 val)
{
	intel_dsb_emit(dsb, val,
		       (DSB_OPCODE_MMIO_WRITE << DSB_OPCODE_SHIFT) |
		       (intel_dsb_mask_to_byte_en(mask) << DSB_BYTE_EN_SHIFT) |
		       i915_mmio_reg_offset(reg));
}

void intel_dsb_noop(struct intel_dsb *dsb, int count)
{
	int i;

	for (i = 0; i < count; i++)
		intel_dsb_emit(dsb, 0,
			       DSB_OPCODE_NOOP << DSB_OPCODE_SHIFT);
}

void intel_dsb_nonpost_start(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	enum pipe pipe = crtc->pipe;

	intel_dsb_reg_write_masked(dsb, DSB_CTRL(pipe, dsb->id),
				   DSB_NON_POSTED, DSB_NON_POSTED);
	intel_dsb_noop(dsb, 4);
}

void intel_dsb_nonpost_end(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	enum pipe pipe = crtc->pipe;

	intel_dsb_reg_write_masked(dsb, DSB_CTRL(pipe, dsb->id),
				   DSB_NON_POSTED, 0);
	intel_dsb_noop(dsb, 4);
}

static void intel_dsb_emit_wait_dsl(struct intel_dsb *dsb,
				    u32 opcode, int lower, int upper)
{
	u64 window = ((u64)upper << DSB_SCANLINE_UPPER_SHIFT) |
		((u64)lower << DSB_SCANLINE_LOWER_SHIFT);

	intel_dsb_emit(dsb, lower_32_bits(window),
		       (opcode << DSB_OPCODE_SHIFT) |
		       upper_32_bits(window));
}

static void intel_dsb_wait_dsl(struct intel_atomic_state *state,
			       struct intel_dsb *dsb,
			       int lower_in, int upper_in,
			       int lower_out, int upper_out)
{
	struct intel_crtc *crtc = dsb->crtc;

	lower_in = dsb_scanline_to_hw(state, crtc, lower_in);
	upper_in = dsb_scanline_to_hw(state, crtc, upper_in);

	lower_out = dsb_scanline_to_hw(state, crtc, lower_out);
	upper_out = dsb_scanline_to_hw(state, crtc, upper_out);

	if (upper_in >= lower_in)
		intel_dsb_emit_wait_dsl(dsb, DSB_OPCODE_WAIT_DSL_IN,
					lower_in, upper_in);
	else if (upper_out >= lower_out)
		intel_dsb_emit_wait_dsl(dsb, DSB_OPCODE_WAIT_DSL_OUT,
					lower_out, upper_out);
	else
		drm_WARN_ON(crtc->base.dev, 1); /* assert_dsl_ok() should have caught it already */
}

static void assert_dsl_ok(struct intel_atomic_state *state,
			  struct intel_dsb *dsb,
			  int start, int end)
{
	struct intel_crtc *crtc = dsb->crtc;
	int vtotal = dsb_vtotal(state, crtc);

	/*
	 * Waiting for the entire frame doesn't make sense,
	 * (IN==don't wait, OUT=wait forever).
	 */
	drm_WARN(crtc->base.dev, (end - start + vtotal) % vtotal == vtotal - 1,
		 "[CRTC:%d:%s] DSB %d bad scanline window wait: %d-%d (vt=%d)\n",
		 crtc->base.base.id, crtc->base.name, dsb->id,
		 start, end, vtotal);
}

void intel_dsb_wait_scanline_in(struct intel_atomic_state *state,
				struct intel_dsb *dsb,
				int start, int end)
{
	assert_dsl_ok(state, dsb, start, end);

	intel_dsb_wait_dsl(state, dsb,
			   start, end,
			   end + 1, start - 1);
}

void intel_dsb_wait_scanline_out(struct intel_atomic_state *state,
				 struct intel_dsb *dsb,
				 int start, int end)
{
	assert_dsl_ok(state, dsb, start, end);

	intel_dsb_wait_dsl(state, dsb,
			   end + 1, start - 1,
			   start, end);
}

static void intel_dsb_align_tail(struct intel_dsb *dsb)
{
	u32 aligned_tail, tail;

	tail = dsb->free_pos * 4;
	aligned_tail = ALIGN(tail, CACHELINE_BYTES);

	if (aligned_tail > tail)
		intel_dsb_buffer_memset(&dsb->dsb_buf, dsb->free_pos, 0,
					aligned_tail - tail);

	dsb->free_pos = aligned_tail / 4;
}

void intel_dsb_finish(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;

	/*
	 * DSB_FORCE_DEWAKE remains active even after DSB is
	 * disabled, so make sure to clear it (if set during
	 * intel_dsb_commit()). And clear DSB_ENABLE_DEWAKE as
	 * well for good measure.
	 */
	intel_dsb_reg_write(dsb, DSB_PMCTRL(crtc->pipe, dsb->id), 0);
	intel_dsb_reg_write_masked(dsb, DSB_PMCTRL_2(crtc->pipe, dsb->id),
				   DSB_FORCE_DEWAKE, 0);

	intel_dsb_align_tail(dsb);

	intel_dsb_buffer_flush_map(&dsb->dsb_buf);
}

static u32 dsb_error_int_status(struct intel_display *display)
{
	u32 errors;

	errors = DSB_GTT_FAULT_INT_STATUS |
		DSB_RSPTIMEOUT_INT_STATUS |
		DSB_POLL_ERR_INT_STATUS;

	/*
	 * All the non-existing status bits operate as
	 * normal r/w bits, so any attempt to clear them
	 * will just end up setting them. Never do that so
	 * we won't mistake them for actual error interrupts.
	 */
	if (DISPLAY_VER(display) >= 14)
		errors |= DSB_ATS_FAULT_INT_STATUS;

	return errors;
}

static u32 dsb_error_int_en(struct intel_display *display)
{
	u32 errors;

	errors = DSB_GTT_FAULT_INT_EN |
		DSB_RSPTIMEOUT_INT_EN |
		DSB_POLL_ERR_INT_EN;

	if (DISPLAY_VER(display) >= 14)
		errors |= DSB_ATS_FAULT_INT_EN;

	return errors;
}

static void _intel_dsb_chain(struct intel_atomic_state *state,
			     struct intel_dsb *dsb,
			     struct intel_dsb *chained_dsb,
			     u32 ctrl)
{
	struct intel_display *display = to_intel_display(state->base.dev);
	struct intel_crtc *crtc = dsb->crtc;
	enum pipe pipe = crtc->pipe;
	u32 tail;

	if (drm_WARN_ON(display->drm, dsb->id == chained_dsb->id))
		return;

	tail = chained_dsb->free_pos * 4;
	if (drm_WARN_ON(display->drm, !IS_ALIGNED(tail, CACHELINE_BYTES)))
		return;

	intel_dsb_reg_write(dsb, DSB_CTRL(pipe, chained_dsb->id),
			    ctrl | DSB_ENABLE);

	intel_dsb_reg_write(dsb, DSB_CHICKEN(pipe, chained_dsb->id),
			    dsb_chicken(state, crtc));

	intel_dsb_reg_write(dsb, DSB_INTERRUPT(pipe, chained_dsb->id),
			    dsb_error_int_status(display) | DSB_PROG_INT_STATUS |
			    dsb_error_int_en(display));

	if (ctrl & DSB_WAIT_FOR_VBLANK) {
		int dewake_scanline = dsb_dewake_scanline_start(state, crtc);
		int hw_dewake_scanline = dsb_scanline_to_hw(state, crtc, dewake_scanline);

		intel_dsb_reg_write(dsb, DSB_PMCTRL(pipe, chained_dsb->id),
				    DSB_ENABLE_DEWAKE |
				    DSB_SCANLINE_FOR_DEWAKE(hw_dewake_scanline));
	}

	intel_dsb_reg_write(dsb, DSB_HEAD(pipe, chained_dsb->id),
			    intel_dsb_buffer_ggtt_offset(&chained_dsb->dsb_buf));

	intel_dsb_reg_write(dsb, DSB_TAIL(pipe, chained_dsb->id),
			    intel_dsb_buffer_ggtt_offset(&chained_dsb->dsb_buf) + tail);

	if (ctrl & DSB_WAIT_FOR_VBLANK) {
		/*
		 * Keep DEwake alive via the first DSB, in
		 * case we're already past dewake_scanline,
		 * and thus DSB_ENABLE_DEWAKE on the second
		 * DSB won't do its job.
		 */
		intel_dsb_reg_write_masked(dsb, DSB_PMCTRL_2(pipe, dsb->id),
					   DSB_FORCE_DEWAKE, DSB_FORCE_DEWAKE);

		intel_dsb_wait_scanline_out(state, dsb,
					    dsb_dewake_scanline_start(state, crtc),
					    dsb_dewake_scanline_end(state, crtc));
	}
}

void intel_dsb_chain(struct intel_atomic_state *state,
		     struct intel_dsb *dsb,
		     struct intel_dsb *chained_dsb,
		     bool wait_for_vblank)
{
	_intel_dsb_chain(state, dsb, chained_dsb,
			 wait_for_vblank ? DSB_WAIT_FOR_VBLANK : 0);
}

static void _intel_dsb_commit(struct intel_dsb *dsb, u32 ctrl,
			      int hw_dewake_scanline)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct intel_display *display = to_intel_display(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tail;

	tail = dsb->free_pos * 4;
	if (drm_WARN_ON(display->drm, !IS_ALIGNED(tail, CACHELINE_BYTES)))
		return;

	if (is_dsb_busy(display, pipe, dsb->id)) {
		drm_err(display->drm, "[CRTC:%d:%s] DSB %d is busy\n",
			crtc->base.base.id, crtc->base.name, dsb->id);
		return;
	}

	intel_de_write_fw(display, DSB_CTRL(pipe, dsb->id),
			  ctrl | DSB_ENABLE);

	intel_de_write_fw(display, DSB_CHICKEN(pipe, dsb->id),
			  dsb->chicken);

	intel_de_write_fw(display, DSB_INTERRUPT(pipe, dsb->id),
			  dsb_error_int_status(display) | DSB_PROG_INT_STATUS |
			  dsb_error_int_en(display));

	intel_de_write_fw(display, DSB_HEAD(pipe, dsb->id),
			  intel_dsb_buffer_ggtt_offset(&dsb->dsb_buf));

	if (hw_dewake_scanline >= 0) {
		int diff, position;

		intel_de_write_fw(display, DSB_PMCTRL(pipe, dsb->id),
				  DSB_ENABLE_DEWAKE |
				  DSB_SCANLINE_FOR_DEWAKE(hw_dewake_scanline));

		/*
		 * Force DEwake immediately if we're already past
		 * or close to racing past the target scanline.
		 */
		position = intel_de_read_fw(display, PIPEDSL(display, pipe)) & PIPEDSL_LINE_MASK;

		diff = hw_dewake_scanline - position;
		intel_de_write_fw(display, DSB_PMCTRL_2(pipe, dsb->id),
				  (diff >= 0 && diff < 5 ? DSB_FORCE_DEWAKE : 0) |
				  DSB_BLOCK_DEWAKE_EXTENSION);
	}

	intel_de_write_fw(display, DSB_TAIL(pipe, dsb->id),
			  intel_dsb_buffer_ggtt_offset(&dsb->dsb_buf) + tail);
}

/**
 * intel_dsb_commit() - Trigger workload execution of DSB.
 * @dsb: DSB context
 * @wait_for_vblank: wait for vblank before executing
 *
 * This function is used to do actual write to hardware using DSB.
 */
void intel_dsb_commit(struct intel_dsb *dsb,
		      bool wait_for_vblank)
{
	_intel_dsb_commit(dsb,
			  wait_for_vblank ? DSB_WAIT_FOR_VBLANK : 0,
			  wait_for_vblank ? dsb->hw_dewake_scanline : -1);
}

void intel_dsb_wait(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct intel_display *display = to_intel_display(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (wait_for(!is_dsb_busy(display, pipe, dsb->id), 1)) {
		u32 offset = intel_dsb_buffer_ggtt_offset(&dsb->dsb_buf);

		intel_de_write_fw(display, DSB_CTRL(pipe, dsb->id),
				  DSB_ENABLE | DSB_HALT);

		drm_err(display->drm,
			"[CRTC:%d:%s] DSB %d timed out waiting for idle (current head=0x%x, head=0x%x, tail=0x%x)\n",
			crtc->base.base.id, crtc->base.name, dsb->id,
			intel_de_read_fw(display, DSB_CURRENT_HEAD(pipe, dsb->id)) - offset,
			intel_de_read_fw(display, DSB_HEAD(pipe, dsb->id)) - offset,
			intel_de_read_fw(display, DSB_TAIL(pipe, dsb->id)) - offset);

		intel_dsb_dump(dsb);
	}

	/* Attempt to reset it */
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;
	intel_de_write_fw(display, DSB_CTRL(pipe, dsb->id), 0);

	intel_de_write_fw(display, DSB_INTERRUPT(pipe, dsb->id),
			  dsb_error_int_status(display) | DSB_PROG_INT_STATUS);
}

/**
 * intel_dsb_prepare() - Allocate, pin and map the DSB command buffer.
 * @state: the atomic state
 * @crtc: the CRTC
 * @dsb_id: the DSB engine to use
 * @max_cmds: number of commands we need to fit into command buffer
 *
 * This function prepare the command buffer which is used to store dsb
 * instructions with data.
 *
 * Returns:
 * DSB context, NULL on failure
 */
struct intel_dsb *intel_dsb_prepare(struct intel_atomic_state *state,
				    struct intel_crtc *crtc,
				    enum intel_dsb_id dsb_id,
				    unsigned int max_cmds)
{
	struct drm_i915_private *i915 = to_i915(state->base.dev);
	intel_wakeref_t wakeref;
	struct intel_dsb *dsb;
	unsigned int size;

	if (!HAS_DSB(i915))
		return NULL;

	if (!i915->display.params.enable_dsb)
		return NULL;

	/* TODO: DSB is broken in Xe KMD, so disabling it until fixed */
	if (!IS_ENABLED(I915))
		return NULL;

	dsb = kzalloc(sizeof(*dsb), GFP_KERNEL);
	if (!dsb)
		goto out;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	/* ~1 qword per instruction, full cachelines */
	size = ALIGN(max_cmds * 8, CACHELINE_BYTES);

	if (!intel_dsb_buffer_create(crtc, &dsb->dsb_buf, size))
		goto out_put_rpm;

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	dsb->id = dsb_id;
	dsb->crtc = crtc;
	dsb->size = size / 4; /* in dwords */
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;

	dsb->chicken = dsb_chicken(state, crtc);
	dsb->hw_dewake_scanline =
		dsb_scanline_to_hw(state, crtc, dsb_dewake_scanline_start(state, crtc));

	return dsb;

out_put_rpm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	kfree(dsb);
out:
	drm_info_once(&i915->drm,
		      "[CRTC:%d:%s] DSB %d queue setup failed, will fallback to MMIO for display HW programming\n",
		      crtc->base.base.id, crtc->base.name, dsb_id);

	return NULL;
}

/**
 * intel_dsb_cleanup() - To cleanup DSB context.
 * @dsb: DSB context
 *
 * This function cleanup the DSB context by unpinning and releasing
 * the VMA object associated with it.
 */
void intel_dsb_cleanup(struct intel_dsb *dsb)
{
	intel_dsb_buffer_cleanup(&dsb->dsb_buf);
	kfree(dsb);
}

void intel_dsb_irq_handler(struct intel_display *display,
			   enum pipe pipe, enum intel_dsb_id dsb_id)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(to_i915(display->drm), pipe);
	u32 tmp, errors;

	tmp = intel_de_read_fw(display, DSB_INTERRUPT(pipe, dsb_id));
	intel_de_write_fw(display, DSB_INTERRUPT(pipe, dsb_id), tmp);

	errors = tmp & dsb_error_int_status(display);
	if (errors)
		drm_err(display->drm, "[CRTC:%d:%s] DSB %d error interrupt: 0x%x\n",
			crtc->base.base.id, crtc->base.name, dsb_id, errors);
}
