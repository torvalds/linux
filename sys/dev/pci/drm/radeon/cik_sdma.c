/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include <linux/firmware.h>

#include "radeon.h"
#include "radeon_ucode.h"
#include "radeon_asic.h"
#include "radeon_trace.h"
#include "cik.h"
#include "cikd.h"

/* sdma */
#define CIK_SDMA_UCODE_SIZE 1050
#define CIK_SDMA_UCODE_VERSION 64

/*
 * sDMA - System DMA
 * Starting with CIK, the GPU has new asynchronous
 * DMA engines.  These engines are used for compute
 * and gfx.  There are two DMA engines (SDMA0, SDMA1)
 * and each one supports 1 ring buffer used for gfx
 * and 2 queues used for compute.
 *
 * The programming model is very similar to the CP
 * (ring buffer, IBs, etc.), but sDMA has it's own
 * packet format that is different from the PM4 format
 * used by the CP. sDMA supports copying data, writing
 * embedded data, solid fills, and a number of other
 * things.  It also has support for tiling/detiling of
 * buffers.
 */

/**
 * cik_sdma_get_rptr - get the current read pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Get the current rptr from the hardware (CIK+).
 */
uint32_t cik_sdma_get_rptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	u32 rptr, reg;

	if (rdev->wb.enabled) {
		rptr = rdev->wb.wb[ring->rptr_offs/4];
	} else {
		if (ring->idx == R600_RING_TYPE_DMA_INDEX)
			reg = SDMA0_GFX_RB_RPTR + SDMA0_REGISTER_OFFSET;
		else
			reg = SDMA0_GFX_RB_RPTR + SDMA1_REGISTER_OFFSET;

		rptr = RREG32(reg);
	}

	return (rptr & 0x3fffc) >> 2;
}

/**
 * cik_sdma_get_wptr - get the current write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Get the current wptr from the hardware (CIK+).
 */
uint32_t cik_sdma_get_wptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	u32 reg;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		reg = SDMA0_GFX_RB_WPTR + SDMA0_REGISTER_OFFSET;
	else
		reg = SDMA0_GFX_RB_WPTR + SDMA1_REGISTER_OFFSET;

	return (RREG32(reg) & 0x3fffc) >> 2;
}

/**
 * cik_sdma_set_wptr - commit the write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Write the wptr back to the hardware (CIK+).
 */
void cik_sdma_set_wptr(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	u32 reg;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		reg = SDMA0_GFX_RB_WPTR + SDMA0_REGISTER_OFFSET;
	else
		reg = SDMA0_GFX_RB_WPTR + SDMA1_REGISTER_OFFSET;

	WREG32(reg, (ring->wptr << 2) & 0x3fffc);
	(void)RREG32(reg);
}

/**
 * cik_sdma_ring_ib_execute - Schedule an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (CIK).
 */
void cik_sdma_ring_ib_execute(struct radeon_device *rdev,
			      struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];
	u32 extra_bits = (ib->vm ? ib->vm->ids[ib->ring].id : 0) & 0xf;

	if (rdev->wb.enabled) {
		u32 next_rptr = ring->wptr + 5;
		while ((next_rptr & 7) != 4)
			next_rptr++;
		next_rptr += 4;
		radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
		radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
		radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr));
		radeon_ring_write(ring, 1); /* number of DWs to follow */
		radeon_ring_write(ring, next_rptr);
	}

	/* IB packet must end on a 8 DW boundary */
	while ((ring->wptr & 7) != 4)
		radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0));
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_INDIRECT_BUFFER, 0, extra_bits));
	radeon_ring_write(ring, ib->gpu_addr & 0xffffffe0); /* base must be 32 byte aligned */
	radeon_ring_write(ring, upper_32_bits(ib->gpu_addr));
	radeon_ring_write(ring, ib->length_dw);

}

/**
 * cik_sdma_hdp_flush_ring_emit - emit an hdp flush on the DMA ring
 *
 * @rdev: radeon_device pointer
 * @ridx: radeon ring index
 *
 * Emit an hdp flush packet on the requested DMA ring.
 */
static void cik_sdma_hdp_flush_ring_emit(struct radeon_device *rdev,
					 int ridx)
{
	struct radeon_ring *ring = &rdev->ring[ridx];
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(1) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(3)); /* == */
	u32 ref_and_mask;

	if (ridx == R600_RING_TYPE_DMA_INDEX)
		ref_and_mask = SDMA0;
	else
		ref_and_mask = SDMA1;

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	radeon_ring_write(ring, GPU_HDP_FLUSH_DONE);
	radeon_ring_write(ring, GPU_HDP_FLUSH_REQ);
	radeon_ring_write(ring, ref_and_mask); /* reference */
	radeon_ring_write(ring, ref_and_mask); /* mask */
	radeon_ring_write(ring, (0xfff << 16) | 10); /* retry count, poll interval */
}

/**
 * cik_sdma_fence_ring_emit - emit a fence on the DMA ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (CIK).
 */
void cik_sdma_fence_ring_emit(struct radeon_device *rdev,
			      struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* write the fence */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_FENCE, 0, 0));
	radeon_ring_write(ring, lower_32_bits(addr));
	radeon_ring_write(ring, upper_32_bits(addr));
	radeon_ring_write(ring, fence->seq);
	/* generate an interrupt */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_TRAP, 0, 0));
	/* flush HDP */
	cik_sdma_hdp_flush_ring_emit(rdev, fence->ring);
}

/**
 * cik_sdma_semaphore_ring_emit - emit a semaphore on the dma ring
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @semaphore: radeon semaphore object
 * @emit_wait: wait or signal semaphore
 *
 * Add a DMA semaphore packet to the ring wait on or signal
 * other rings (CIK).
 */
bool cik_sdma_semaphore_ring_emit(struct radeon_device *rdev,
				  struct radeon_ring *ring,
				  struct radeon_semaphore *semaphore,
				  bool emit_wait)
{
	u64 addr = semaphore->gpu_addr;
	u32 extra_bits = emit_wait ? 0 : SDMA_SEMAPHORE_EXTRA_S;

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SEMAPHORE, 0, extra_bits));
	radeon_ring_write(ring, addr & 0xfffffff8);
	radeon_ring_write(ring, upper_32_bits(addr));

	return true;
}

/**
 * cik_sdma_gfx_stop - stop the gfx async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the gfx async dma ring buffers (CIK).
 */
static void cik_sdma_gfx_stop(struct radeon_device *rdev)
{
	u32 rb_cntl, reg_offset;
	int i;

	if ((rdev->asic->copy.copy_ring_index == R600_RING_TYPE_DMA_INDEX) ||
	    (rdev->asic->copy.copy_ring_index == CAYMAN_RING_TYPE_DMA1_INDEX))
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);

	for (i = 0; i < 2; i++) {
		if (i == 0)
			reg_offset = SDMA0_REGISTER_OFFSET;
		else
			reg_offset = SDMA1_REGISTER_OFFSET;
		rb_cntl = RREG32(SDMA0_GFX_RB_CNTL + reg_offset);
		rb_cntl &= ~SDMA_RB_ENABLE;
		WREG32(SDMA0_GFX_RB_CNTL + reg_offset, rb_cntl);
		WREG32(SDMA0_GFX_IB_CNTL + reg_offset, 0);
	}
	rdev->ring[R600_RING_TYPE_DMA_INDEX].ready = false;
	rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX].ready = false;

	/* FIXME use something else than big hammer but after few days can not
	 * seem to find good combination so reset SDMA blocks as it seems we
	 * do not shut them down properly. This fix hibernation and does not
	 * affect suspend to ram.
	 */
	WREG32(SRBM_SOFT_RESET, SOFT_RESET_SDMA | SOFT_RESET_SDMA1);
	(void)RREG32(SRBM_SOFT_RESET);
	udelay(50);
	WREG32(SRBM_SOFT_RESET, 0);
	(void)RREG32(SRBM_SOFT_RESET);
}

/**
 * cik_sdma_rlc_stop - stop the compute async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the compute async dma queues (CIK).
 */
static void cik_sdma_rlc_stop(struct radeon_device *rdev)
{
	/* XXX todo */
}

/**
 * cik_sdma_ctx_switch_enable - enable/disable sdma engine preemption
 *
 * @rdev: radeon_device pointer
 * @enable: enable/disable preemption.
 *
 * Halt or unhalt the async dma engines (CIK).
 */
static void cik_sdma_ctx_switch_enable(struct radeon_device *rdev, bool enable)
{
	uint32_t reg_offset, value;
	int i;

	for (i = 0; i < 2; i++) {
		if (i == 0)
			reg_offset = SDMA0_REGISTER_OFFSET;
		else
			reg_offset = SDMA1_REGISTER_OFFSET;
		value = RREG32(SDMA0_CNTL + reg_offset);
		if (enable)
			value |= AUTO_CTXSW_ENABLE;
		else
			value &= ~AUTO_CTXSW_ENABLE;
		WREG32(SDMA0_CNTL + reg_offset, value);
	}
}

/**
 * cik_sdma_enable - stop the async dma engines
 *
 * @rdev: radeon_device pointer
 * @enable: enable/disable the DMA MEs.
 *
 * Halt or unhalt the async dma engines (CIK).
 */
void cik_sdma_enable(struct radeon_device *rdev, bool enable)
{
	u32 me_cntl, reg_offset;
	int i;

	if (!enable) {
		cik_sdma_gfx_stop(rdev);
		cik_sdma_rlc_stop(rdev);
	}

	for (i = 0; i < 2; i++) {
		if (i == 0)
			reg_offset = SDMA0_REGISTER_OFFSET;
		else
			reg_offset = SDMA1_REGISTER_OFFSET;
		me_cntl = RREG32(SDMA0_ME_CNTL + reg_offset);
		if (enable)
			me_cntl &= ~SDMA_HALT;
		else
			me_cntl |= SDMA_HALT;
		WREG32(SDMA0_ME_CNTL + reg_offset, me_cntl);
	}

	cik_sdma_ctx_switch_enable(rdev, enable);
}

/**
 * cik_sdma_gfx_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the gfx DMA ring buffers and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_gfx_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring;
	u32 rb_cntl, ib_cntl;
	u32 rb_bufsz;
	u32 reg_offset, wb_offset;
	int i, r;

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
			reg_offset = SDMA0_REGISTER_OFFSET;
			wb_offset = R600_WB_DMA_RPTR_OFFSET;
		} else {
			ring = &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX];
			reg_offset = SDMA1_REGISTER_OFFSET;
			wb_offset = CAYMAN_WB_DMA1_RPTR_OFFSET;
		}

		WREG32(SDMA0_SEM_INCOMPLETE_TIMER_CNTL + reg_offset, 0);
		WREG32(SDMA0_SEM_WAIT_FAIL_TIMER_CNTL + reg_offset, 0);

		/* Set ring buffer size in dwords */
		rb_bufsz = order_base_2(ring->ring_size / 4);
		rb_cntl = rb_bufsz << 1;
#ifdef __BIG_ENDIAN
		rb_cntl |= SDMA_RB_SWAP_ENABLE | SDMA_RPTR_WRITEBACK_SWAP_ENABLE;
#endif
		WREG32(SDMA0_GFX_RB_CNTL + reg_offset, rb_cntl);

		/* Initialize the ring buffer's read and write pointers */
		WREG32(SDMA0_GFX_RB_RPTR + reg_offset, 0);
		WREG32(SDMA0_GFX_RB_WPTR + reg_offset, 0);

		/* set the wb address whether it's enabled or not */
		WREG32(SDMA0_GFX_RB_RPTR_ADDR_HI + reg_offset,
		       upper_32_bits(rdev->wb.gpu_addr + wb_offset) & 0xFFFFFFFF);
		WREG32(SDMA0_GFX_RB_RPTR_ADDR_LO + reg_offset,
		       ((rdev->wb.gpu_addr + wb_offset) & 0xFFFFFFFC));

		if (rdev->wb.enabled)
			rb_cntl |= SDMA_RPTR_WRITEBACK_ENABLE;

		WREG32(SDMA0_GFX_RB_BASE + reg_offset, ring->gpu_addr >> 8);
		WREG32(SDMA0_GFX_RB_BASE_HI + reg_offset, ring->gpu_addr >> 40);

		ring->wptr = 0;
		WREG32(SDMA0_GFX_RB_WPTR + reg_offset, ring->wptr << 2);

		/* enable DMA RB */
		WREG32(SDMA0_GFX_RB_CNTL + reg_offset, rb_cntl | SDMA_RB_ENABLE);

		ib_cntl = SDMA_IB_ENABLE;
#ifdef __BIG_ENDIAN
		ib_cntl |= SDMA_IB_SWAP_ENABLE;
#endif
		/* enable DMA IBs */
		WREG32(SDMA0_GFX_IB_CNTL + reg_offset, ib_cntl);

		ring->ready = true;

		r = radeon_ring_test(rdev, ring->idx, ring);
		if (r) {
			ring->ready = false;
			return r;
		}
	}

	if ((rdev->asic->copy.copy_ring_index == R600_RING_TYPE_DMA_INDEX) ||
	    (rdev->asic->copy.copy_ring_index == CAYMAN_RING_TYPE_DMA1_INDEX))
		radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

/**
 * cik_sdma_rlc_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the compute DMA queues and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
static int cik_sdma_rlc_resume(struct radeon_device *rdev)
{
	/* XXX todo */
	return 0;
}

/**
 * cik_sdma_load_microcode - load the sDMA ME ucode
 *
 * @rdev: radeon_device pointer
 *
 * Loads the sDMA0/1 ucode.
 * Returns 0 for success, -EINVAL if the ucode is not available.
 */
static int cik_sdma_load_microcode(struct radeon_device *rdev)
{
	int i;

	if (!rdev->sdma_fw)
		return -EINVAL;

	/* halt the MEs */
	cik_sdma_enable(rdev, false);

	if (rdev->new_fw) {
		const struct sdma_firmware_header_v1_0 *hdr =
			(const struct sdma_firmware_header_v1_0 *)rdev->sdma_fw->data;
		const __le32 *fw_data;
		u32 fw_size;

		radeon_ucode_print_sdma_hdr(&hdr->header);

		/* sdma0 */
		fw_data = (const __le32 *)
			(rdev->sdma_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		WREG32(SDMA0_UCODE_ADDR + SDMA0_REGISTER_OFFSET, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(SDMA0_UCODE_DATA + SDMA0_REGISTER_OFFSET, le32_to_cpup(fw_data++));
		WREG32(SDMA0_UCODE_DATA + SDMA0_REGISTER_OFFSET, CIK_SDMA_UCODE_VERSION);

		/* sdma1 */
		fw_data = (const __le32 *)
			(rdev->sdma_fw->data + le32_to_cpu(hdr->header.ucode_array_offset_bytes));
		fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;
		WREG32(SDMA0_UCODE_ADDR + SDMA1_REGISTER_OFFSET, 0);
		for (i = 0; i < fw_size; i++)
			WREG32(SDMA0_UCODE_DATA + SDMA1_REGISTER_OFFSET, le32_to_cpup(fw_data++));
		WREG32(SDMA0_UCODE_DATA + SDMA1_REGISTER_OFFSET, CIK_SDMA_UCODE_VERSION);
	} else {
		const __be32 *fw_data;

		/* sdma0 */
		fw_data = (const __be32 *)rdev->sdma_fw->data;
		WREG32(SDMA0_UCODE_ADDR + SDMA0_REGISTER_OFFSET, 0);
		for (i = 0; i < CIK_SDMA_UCODE_SIZE; i++)
			WREG32(SDMA0_UCODE_DATA + SDMA0_REGISTER_OFFSET, be32_to_cpup(fw_data++));
		WREG32(SDMA0_UCODE_DATA + SDMA0_REGISTER_OFFSET, CIK_SDMA_UCODE_VERSION);

		/* sdma1 */
		fw_data = (const __be32 *)rdev->sdma_fw->data;
		WREG32(SDMA0_UCODE_ADDR + SDMA1_REGISTER_OFFSET, 0);
		for (i = 0; i < CIK_SDMA_UCODE_SIZE; i++)
			WREG32(SDMA0_UCODE_DATA + SDMA1_REGISTER_OFFSET, be32_to_cpup(fw_data++));
		WREG32(SDMA0_UCODE_DATA + SDMA1_REGISTER_OFFSET, CIK_SDMA_UCODE_VERSION);
	}

	WREG32(SDMA0_UCODE_ADDR + SDMA0_REGISTER_OFFSET, 0);
	WREG32(SDMA0_UCODE_ADDR + SDMA1_REGISTER_OFFSET, 0);
	return 0;
}

/**
 * cik_sdma_resume - setup and start the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Set up the DMA engines and enable them (CIK).
 * Returns 0 for success, error for failure.
 */
int cik_sdma_resume(struct radeon_device *rdev)
{
	int r;

	r = cik_sdma_load_microcode(rdev);
	if (r)
		return r;

	/* unhalt the MEs */
	cik_sdma_enable(rdev, true);

	/* start the gfx rings and rlc compute queues */
	r = cik_sdma_gfx_resume(rdev);
	if (r)
		return r;
	r = cik_sdma_rlc_resume(rdev);
	if (r)
		return r;

	return 0;
}

/**
 * cik_sdma_fini - tear down the async dma engines
 *
 * @rdev: radeon_device pointer
 *
 * Stop the async dma engines and free the rings (CIK).
 */
void cik_sdma_fini(struct radeon_device *rdev)
{
	/* halt the MEs */
	cik_sdma_enable(rdev, false);
	radeon_ring_fini(rdev, &rdev->ring[R600_RING_TYPE_DMA_INDEX]);
	radeon_ring_fini(rdev, &rdev->ring[CAYMAN_RING_TYPE_DMA1_INDEX]);
	/* XXX - compute dma queue tear down */
}

/**
 * cik_copy_dma - copy pages using the DMA engine
 *
 * @rdev: radeon_device pointer
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @num_gpu_pages: number of GPU pages to xfer
 * @resv: reservation object to sync to
 *
 * Copy GPU paging using the DMA engine (CIK).
 * Used by the radeon ttm implementation to move pages if
 * registered as the asic copy callback.
 */
struct radeon_fence *cik_copy_dma(struct radeon_device *rdev,
				  uint64_t src_offset, uint64_t dst_offset,
				  unsigned num_gpu_pages,
				  struct dma_resv *resv)
{
	struct radeon_fence *fence;
	struct radeon_sync sync;
	int ring_index = rdev->asic->copy.dma_ring_index;
	struct radeon_ring *ring = &rdev->ring[ring_index];
	u32 size_in_bytes, cur_size_in_bytes;
	int i, num_loops;
	int r = 0;

	radeon_sync_create(&sync);

	size_in_bytes = (num_gpu_pages << RADEON_GPU_PAGE_SHIFT);
	num_loops = DIV_ROUND_UP(size_in_bytes, 0x1fffff);
	r = radeon_ring_lock(rdev, ring, num_loops * 7 + 14);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		radeon_sync_free(rdev, &sync, NULL);
		return ERR_PTR(r);
	}

	radeon_sync_resv(rdev, &sync, resv, false);
	radeon_sync_rings(rdev, &sync, ring->idx);

	for (i = 0; i < num_loops; i++) {
		cur_size_in_bytes = size_in_bytes;
		if (cur_size_in_bytes > 0x1fffff)
			cur_size_in_bytes = 0x1fffff;
		size_in_bytes -= cur_size_in_bytes;
		radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_COPY, SDMA_COPY_SUB_OPCODE_LINEAR, 0));
		radeon_ring_write(ring, cur_size_in_bytes);
		radeon_ring_write(ring, 0); /* src/dst endian swap */
		radeon_ring_write(ring, lower_32_bits(src_offset));
		radeon_ring_write(ring, upper_32_bits(src_offset));
		radeon_ring_write(ring, lower_32_bits(dst_offset));
		radeon_ring_write(ring, upper_32_bits(dst_offset));
		src_offset += cur_size_in_bytes;
		dst_offset += cur_size_in_bytes;
	}

	r = radeon_fence_emit(rdev, &fence, ring->idx);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		radeon_sync_free(rdev, &sync, NULL);
		return ERR_PTR(r);
	}

	radeon_ring_unlock_commit(rdev, ring, false);
	radeon_sync_free(rdev, &sync, fence);

	return fence;
}

/**
 * cik_sdma_ring_test - simple async dma engine test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (CIK).
 * Returns 0 for success, error for failure.
 */
int cik_sdma_ring_test(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	unsigned i;
	int r;
	unsigned index;
	u32 tmp;
	u64 gpu_addr;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		index = R600_WB_DMA_RING_TEST_OFFSET;
	else
		index = CAYMAN_WB_DMA1_RING_TEST_OFFSET;

	gpu_addr = rdev->wb.gpu_addr + index;

	tmp = 0xCAFEDEAD;
	rdev->wb.wb[index/4] = cpu_to_le32(tmp);

	r = radeon_ring_lock(rdev, ring, 5);
	if (r) {
		DRM_ERROR("radeon: dma failed to lock ring %d (%d).\n", ring->idx, r);
		return r;
	}
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0));
	radeon_ring_write(ring, lower_32_bits(gpu_addr));
	radeon_ring_write(ring, upper_32_bits(gpu_addr));
	radeon_ring_write(ring, 1); /* number of DWs to follow */
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring, false);

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = le32_to_cpu(rdev->wb.wb[index/4]);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}

	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("radeon: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

/**
 * cik_sdma_ib_test - test an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (CIK).
 * Returns 0 on success, error on failure.
 */
int cik_sdma_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_ib ib;
	unsigned i;
	unsigned index;
	int r;
	u32 tmp = 0;
	u64 gpu_addr;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		index = R600_WB_DMA_RING_TEST_OFFSET;
	else
		index = CAYMAN_WB_DMA1_RING_TEST_OFFSET;

	gpu_addr = rdev->wb.gpu_addr + index;

	tmp = 0xCAFEDEAD;
	rdev->wb.wb[index/4] = cpu_to_le32(tmp);

	r = radeon_ib_get(rdev, ring->idx, &ib, NULL, 256);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}

	ib.ptr[0] = SDMA_PACKET(SDMA_OPCODE_WRITE, SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
	ib.ptr[1] = lower_32_bits(gpu_addr);
	ib.ptr[2] = upper_32_bits(gpu_addr);
	ib.ptr[3] = 1;
	ib.ptr[4] = 0xDEADBEEF;
	ib.length_dw = 5;

	r = radeon_ib_schedule(rdev, &ib, NULL, false);
	if (r) {
		radeon_ib_free(rdev, &ib);
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		return r;
	}
	r = radeon_fence_wait_timeout(ib.fence, false, usecs_to_jiffies(
		RADEON_USEC_IB_TEST_TIMEOUT));
	if (r < 0) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		return r;
	} else if (r == 0) {
		DRM_ERROR("radeon: fence wait timed out.\n");
		return -ETIMEDOUT;
	}
	r = 0;
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = le32_to_cpu(rdev->wb.wb[index/4]);
		if (tmp == 0xDEADBEEF)
			break;
		udelay(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n", ib.fence->ring, i);
	} else {
		DRM_ERROR("radeon: ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}
	radeon_ib_free(rdev, &ib);
	return r;
}

/**
 * cik_sdma_is_lockup - Check if the DMA engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the async DMA engine is locked up (CIK).
 * Returns true if the engine appears to be locked up, false if not.
 */
bool cik_sdma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = cik_gpu_check_soft_reset(rdev);
	u32 mask;

	if (ring->idx == R600_RING_TYPE_DMA_INDEX)
		mask = RADEON_RESET_DMA;
	else
		mask = RADEON_RESET_DMA1;

	if (!(reset_mask & mask)) {
		radeon_ring_lockup_update(rdev, ring);
		return false;
	}
	return radeon_ring_test_lockup(rdev, ring);
}

/**
 * cik_sdma_vm_copy_pages - update PTEs by copying them from the GART
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @src: src addr to copy from
 * @count: number of page entries to update
 *
 * Update PTEs by copying them from the GART using sDMA (CIK).
 */
void cik_sdma_vm_copy_pages(struct radeon_device *rdev,
			    struct radeon_ib *ib,
			    uint64_t pe, uint64_t src,
			    unsigned count)
{
	while (count) {
		unsigned bytes = count * 8;
		if (bytes > 0x1FFFF8)
			bytes = 0x1FFFF8;

		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_COPY,
			SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
		ib->ptr[ib->length_dw++] = bytes;
		ib->ptr[ib->length_dw++] = 0; /* src/dst endian swap */
		ib->ptr[ib->length_dw++] = lower_32_bits(src);
		ib->ptr[ib->length_dw++] = upper_32_bits(src);
		ib->ptr[ib->length_dw++] = lower_32_bits(pe);
		ib->ptr[ib->length_dw++] = upper_32_bits(pe);

		pe += bytes;
		src += bytes;
		count -= bytes / 8;
	}
}

/**
 * cik_sdma_vm_write_pages - update PTEs by writing them manually
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update PTEs by writing them manually using sDMA (CIK).
 */
void cik_sdma_vm_write_pages(struct radeon_device *rdev,
			     struct radeon_ib *ib,
			     uint64_t pe,
			     uint64_t addr, unsigned count,
			     uint32_t incr, uint32_t flags)
{
	uint64_t value;
	unsigned ndw;

	while (count) {
		ndw = count * 2;
		if (ndw > 0xFFFFE)
			ndw = 0xFFFFE;

		/* for non-physically contiguous pages (system) */
		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_WRITE,
			SDMA_WRITE_SUB_OPCODE_LINEAR, 0);
		ib->ptr[ib->length_dw++] = pe;
		ib->ptr[ib->length_dw++] = upper_32_bits(pe);
		ib->ptr[ib->length_dw++] = ndw;
		for (; ndw > 0; ndw -= 2, --count, pe += 8) {
			if (flags & R600_PTE_SYSTEM) {
				value = radeon_vm_map_gart(rdev, addr);
			} else if (flags & R600_PTE_VALID) {
				value = addr;
			} else {
				value = 0;
			}
			addr += incr;
			value |= flags;
			ib->ptr[ib->length_dw++] = value;
			ib->ptr[ib->length_dw++] = upper_32_bits(value);
		}
	}
}

/**
 * cik_sdma_vm_set_pages - update the page tables using sDMA
 *
 * @rdev: radeon_device pointer
 * @ib: indirect buffer to fill with commands
 * @pe: addr of the page entry
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: access flags
 *
 * Update the page tables using sDMA (CIK).
 */
void cik_sdma_vm_set_pages(struct radeon_device *rdev,
			   struct radeon_ib *ib,
			   uint64_t pe,
			   uint64_t addr, unsigned count,
			   uint32_t incr, uint32_t flags)
{
	uint64_t value;
	unsigned ndw;

	while (count) {
		ndw = count;
		if (ndw > 0x7FFFF)
			ndw = 0x7FFFF;

		if (flags & R600_PTE_VALID)
			value = addr;
		else
			value = 0;

		/* for physically contiguous pages (vram) */
		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_GENERATE_PTE_PDE, 0, 0);
		ib->ptr[ib->length_dw++] = pe; /* dst addr */
		ib->ptr[ib->length_dw++] = upper_32_bits(pe);
		ib->ptr[ib->length_dw++] = flags; /* mask */
		ib->ptr[ib->length_dw++] = 0;
		ib->ptr[ib->length_dw++] = value; /* value */
		ib->ptr[ib->length_dw++] = upper_32_bits(value);
		ib->ptr[ib->length_dw++] = incr; /* increment size */
		ib->ptr[ib->length_dw++] = 0;
		ib->ptr[ib->length_dw++] = ndw; /* number of entries */

		pe += ndw * 8;
		addr += ndw * incr;
		count -= ndw;
	}
}

/**
 * cik_sdma_vm_pad_ib - pad the IB to the required number of dw
 *
 * @ib: indirect buffer to fill with padding
 *
 */
void cik_sdma_vm_pad_ib(struct radeon_ib *ib)
{
	while (ib->length_dw & 0x7)
		ib->ptr[ib->length_dw++] = SDMA_PACKET(SDMA_OPCODE_NOP, 0, 0);
}

/*
 * cik_dma_vm_flush - cik vm flush using sDMA
 *
 * Update the page table base and flush the VM TLB
 * using sDMA (CIK).
 */
void cik_dma_vm_flush(struct radeon_device *rdev, struct radeon_ring *ring,
		      unsigned vm_id, uint64_t pd_addr)
{
	u32 extra_bits = (SDMA_POLL_REG_MEM_EXTRA_OP(0) |
			  SDMA_POLL_REG_MEM_EXTRA_FUNC(0)); /* always */

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	if (vm_id < 8) {
		radeon_ring_write(ring, (VM_CONTEXT0_PAGE_TABLE_BASE_ADDR + (vm_id << 2)) >> 2);
	} else {
		radeon_ring_write(ring, (VM_CONTEXT8_PAGE_TABLE_BASE_ADDR + ((vm_id - 8) << 2)) >> 2);
	}
	radeon_ring_write(ring, pd_addr >> 12);

	/* update SH_MEM_* regs */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, VMID(vm_id));

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_BASES >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_CONFIG >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_APE1_BASE >> 2);
	radeon_ring_write(ring, 1);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SH_MEM_APE1_LIMIT >> 2);
	radeon_ring_write(ring, 0);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, SRBM_GFX_CNTL >> 2);
	radeon_ring_write(ring, VMID(0));

	/* flush HDP */
	cik_sdma_hdp_flush_ring_emit(rdev, ring->idx);

	/* flush TLB */
	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_SRBM_WRITE, 0, 0xf000));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 1 << vm_id);

	radeon_ring_write(ring, SDMA_PACKET(SDMA_OPCODE_POLL_REG_MEM, 0, extra_bits));
	radeon_ring_write(ring, VM_INVALIDATE_REQUEST >> 2);
	radeon_ring_write(ring, 0);
	radeon_ring_write(ring, 0); /* reference */
	radeon_ring_write(ring, 0); /* mask */
	radeon_ring_write(ring, (0xfff << 16) | 10); /* retry count, poll interval */
}

