/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#include <drm/drm_drv.h> /* for drm_dmamem */

#include <linux/dma-mapping.h>

#include "amdgpu.h"
#include "amdgpu_ih.h"

/**
 * amdgpu_ih_ring_init - initialize the IH state
 *
 * @adev: amdgpu_device pointer
 * @ih: ih ring to initialize
 * @ring_size: ring size to allocate
 * @use_bus_addr: true when we can use dma_alloc_coherent
 *
 * Initializes the IH state and allocates a buffer
 * for the IH ring buffer.
 * Returns 0 for success, errors for failure.
 */
int amdgpu_ih_ring_init(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih,
			unsigned ring_size, bool use_bus_addr)
{
	u32 rb_bufsz;
	int r;
	struct drm_dmamem *dmah;
	int flags = 0;

	/* Align ring size */
	rb_bufsz = order_base_2(ring_size / 4);
	ring_size = (1 << rb_bufsz) * 4;
	ih->ring_size = ring_size;
	ih->ptr_mask = ih->ring_size - 1;
	ih->rptr = 0;
	ih->use_bus_addr = use_bus_addr;

	if (use_bus_addr) {
		dma_addr_t dma_addr;

		if (ih->ring)
			return 0;

		/* add 8 bytes for the rptr/wptr shadows and
		 * add them to the end of the ring allocation.
		 */
#ifdef __linux__
		ih->ring = dma_alloc_coherent(adev->dev, ih->ring_size + 8,
					      &dma_addr, GFP_KERNEL);
		if (ih->ring == NULL)
			return -ENOMEM;
#else
		dmah = drm_dmamem_alloc(adev->dmat,
		    ih->ring_size + 8,
		    PAGE_SIZE, 1,
		    ih->ring_size + 8, flags, 0);
		if (dmah == NULL)
			return -ENOMEM;
		ih->dmah = dmah;
		dma_addr = dmah->map->dm_segs[0].ds_addr;
		ih->ring = (volatile uint32_t *)dmah->kva;
#endif

		ih->gpu_addr = dma_addr;
		ih->wptr_addr = dma_addr + ih->ring_size;
		ih->wptr_cpu = &ih->ring[ih->ring_size / 4];
		ih->rptr_addr = dma_addr + ih->ring_size + 4;
		ih->rptr_cpu = &ih->ring[(ih->ring_size / 4) + 1];
	} else {
		unsigned wptr_offs, rptr_offs;

		r = amdgpu_device_wb_get(adev, &wptr_offs);
		if (r)
			return r;

		r = amdgpu_device_wb_get(adev, &rptr_offs);
		if (r) {
			amdgpu_device_wb_free(adev, wptr_offs);
			return r;
		}

		r = amdgpu_bo_create_kernel(adev, ih->ring_size, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_GTT,
					    &ih->ring_obj, &ih->gpu_addr,
					    (void **)&ih->ring);
		if (r) {
			amdgpu_device_wb_free(adev, rptr_offs);
			amdgpu_device_wb_free(adev, wptr_offs);
			return r;
		}

		ih->wptr_addr = adev->wb.gpu_addr + wptr_offs * 4;
		ih->wptr_cpu = &adev->wb.wb[wptr_offs];
		ih->rptr_addr = adev->wb.gpu_addr + rptr_offs * 4;
		ih->rptr_cpu = &adev->wb.wb[rptr_offs];
	}

	init_waitqueue_head(&ih->wait_process);
	return 0;
}

/**
 * amdgpu_ih_ring_fini - tear down the IH state
 *
 * @adev: amdgpu_device pointer
 * @ih: ih ring to tear down
 *
 * Tears down the IH state and frees buffer
 * used for the IH ring buffer.
 */
void amdgpu_ih_ring_fini(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih)
{

	if (!ih->ring)
		return;

	if (ih->use_bus_addr) {

		/* add 8 bytes for the rptr/wptr shadows and
		 * add them to the end of the ring allocation.
		 */
#ifdef __linux__
		dma_free_coherent(adev->dev, ih->ring_size + 8,
				  (void *)ih->ring, ih->gpu_addr);
#else
		drm_dmamem_free(adev->dmat, ih->dmah);
#endif
		ih->ring = NULL;
	} else {
		amdgpu_bo_free_kernel(&ih->ring_obj, &ih->gpu_addr,
				      (void **)&ih->ring);
		amdgpu_device_wb_free(adev, (ih->wptr_addr - ih->gpu_addr) / 4);
		amdgpu_device_wb_free(adev, (ih->rptr_addr - ih->gpu_addr) / 4);
	}
}

/**
 * amdgpu_ih_ring_write - write IV to the ring buffer
 *
 * @adev: amdgpu_device pointer
 * @ih: ih ring to write to
 * @iv: the iv to write
 * @num_dw: size of the iv in dw
 *
 * Writes an IV to the ring buffer using the CPU and increment the wptr.
 * Used for testing and delegating IVs to a software ring.
 */
void amdgpu_ih_ring_write(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih,
			  const uint32_t *iv, unsigned int num_dw)
{
	uint32_t wptr = le32_to_cpu(*ih->wptr_cpu) >> 2;
	unsigned int i;

	for (i = 0; i < num_dw; ++i)
	        ih->ring[wptr++] = cpu_to_le32(iv[i]);

	wptr <<= 2;
	wptr &= ih->ptr_mask;

	/* Only commit the new wptr if we don't overflow */
	if (wptr != READ_ONCE(ih->rptr)) {
		wmb();
		WRITE_ONCE(*ih->wptr_cpu, cpu_to_le32(wptr));
	} else if (adev->irq.retry_cam_enabled) {
		dev_warn_once(adev->dev, "IH soft ring buffer overflow 0x%X, 0x%X\n",
			      wptr, ih->rptr);
	}
}

/**
 * amdgpu_ih_wait_on_checkpoint_process_ts - wait to process IVs up to checkpoint
 *
 * @adev: amdgpu_device pointer
 * @ih: ih ring to process
 *
 * Used to ensure ring has processed IVs up to the checkpoint write pointer.
 */
int amdgpu_ih_wait_on_checkpoint_process_ts(struct amdgpu_device *adev,
					struct amdgpu_ih_ring *ih)
{
	uint32_t checkpoint_wptr;
	uint64_t checkpoint_ts;
	long timeout = HZ;

	if (!ih->enabled || adev->shutdown)
		return -ENODEV;

	checkpoint_wptr = amdgpu_ih_get_wptr(adev, ih);
	/* Order wptr with ring data. */
	rmb();
	checkpoint_ts = amdgpu_ih_decode_iv_ts(adev, ih, checkpoint_wptr, -1);

	return wait_event_interruptible_timeout(ih->wait_process,
		    amdgpu_ih_ts_after(checkpoint_ts, ih->processed_timestamp) ||
		    ih->rptr == amdgpu_ih_get_wptr(adev, ih), timeout);
}

/**
 * amdgpu_ih_process - interrupt handler
 *
 * @adev: amdgpu_device pointer
 * @ih: ih ring to process
 *
 * Interrupt hander (VI), walk the IH ring.
 * Returns irq process return code.
 */
int amdgpu_ih_process(struct amdgpu_device *adev, struct amdgpu_ih_ring *ih)
{
	unsigned int count;
	u32 wptr;

	if (!ih->enabled || adev->shutdown)
		return IRQ_NONE;

	wptr = amdgpu_ih_get_wptr(adev, ih);

restart_ih:
	count  = AMDGPU_IH_MAX_NUM_IVS;
	DRM_DEBUG("%s: rptr %d, wptr %d\n", __func__, ih->rptr, wptr);

	/* Order reading of wptr vs. reading of IH ring data */
	rmb();

	while (ih->rptr != wptr && --count) {
		amdgpu_irq_dispatch(adev, ih);
		ih->rptr &= ih->ptr_mask;
	}

	amdgpu_ih_set_rptr(adev, ih);
	wake_up_all(&ih->wait_process);

	/* make sure wptr hasn't changed while processing */
	wptr = amdgpu_ih_get_wptr(adev, ih);
	if (wptr != ih->rptr)
		goto restart_ih;

	return IRQ_HANDLED;
}

/**
 * amdgpu_ih_decode_iv_helper - decode an interrupt vector
 *
 * @adev: amdgpu_device pointer
 * @ih: ih ring to process
 * @entry: IV entry
 *
 * Decodes the interrupt vector at the current rptr
 * position and also advance the position for Vega10
 * and later GPUs.
 */
void amdgpu_ih_decode_iv_helper(struct amdgpu_device *adev,
				struct amdgpu_ih_ring *ih,
				struct amdgpu_iv_entry *entry)
{
	/* wptr/rptr are in bytes! */
	u32 ring_index = ih->rptr >> 2;
	uint32_t dw[8];

	dw[0] = le32_to_cpu(ih->ring[ring_index + 0]);
	dw[1] = le32_to_cpu(ih->ring[ring_index + 1]);
	dw[2] = le32_to_cpu(ih->ring[ring_index + 2]);
	dw[3] = le32_to_cpu(ih->ring[ring_index + 3]);
	dw[4] = le32_to_cpu(ih->ring[ring_index + 4]);
	dw[5] = le32_to_cpu(ih->ring[ring_index + 5]);
	dw[6] = le32_to_cpu(ih->ring[ring_index + 6]);
	dw[7] = le32_to_cpu(ih->ring[ring_index + 7]);

	entry->client_id = dw[0] & 0xff;
	entry->src_id = (dw[0] >> 8) & 0xff;
	entry->ring_id = (dw[0] >> 16) & 0xff;
	entry->vmid = (dw[0] >> 24) & 0xf;
	entry->vmid_src = (dw[0] >> 31);
	entry->timestamp = dw[1] | ((u64)(dw[2] & 0xffff) << 32);
	entry->timestamp_src = dw[2] >> 31;
	entry->pasid = dw[3] & 0xffff;
	entry->node_id = (dw[3] >> 16) & 0xff;
	entry->src_data[0] = dw[4];
	entry->src_data[1] = dw[5];
	entry->src_data[2] = dw[6];
	entry->src_data[3] = dw[7];

	/* wptr/rptr are in bytes! */
	ih->rptr += 32;
}

uint64_t amdgpu_ih_decode_iv_ts_helper(struct amdgpu_ih_ring *ih, u32 rptr,
				       signed int offset)
{
	uint32_t iv_size = 32;
	uint32_t ring_index;
	uint32_t dw1, dw2;

	rptr += iv_size * offset;
	ring_index = (rptr & ih->ptr_mask) >> 2;

	dw1 = le32_to_cpu(ih->ring[ring_index + 1]);
	dw2 = le32_to_cpu(ih->ring[ring_index + 2]);
	return dw1 | ((u64)(dw2 & 0xffff) << 32);
}
