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

#ifndef __AMDGPU_VCE_H__
#define __AMDGPU_VCE_H__

#define AMDGPU_MAX_VCE_HANDLES	16
#define AMDGPU_VCE_FIRMWARE_OFFSET 256

#define AMDGPU_VCE_HARVEST_VCE0 (1 << 0)
#define AMDGPU_VCE_HARVEST_VCE1 (1 << 1)

#define AMDGPU_VCE_FW_53_45	((53 << 24) | (45 << 16))

struct amdgpu_vce {
	struct amdgpu_bo	*vcpu_bo;
	uint64_t		gpu_addr;
	void			*cpu_addr;
	void			*saved_bo;
	unsigned		fw_version;
	unsigned		fb_version;
	atomic_t		handles[AMDGPU_MAX_VCE_HANDLES];
	struct drm_file		*filp[AMDGPU_MAX_VCE_HANDLES];
	uint32_t		img_size[AMDGPU_MAX_VCE_HANDLES];
	struct delayed_work	idle_work;
	struct rwlock		idle_mutex;
	const struct firmware	*fw;	/* VCE firmware */
	struct amdgpu_ring	ring[AMDGPU_MAX_VCE_RINGS];
	struct amdgpu_irq_src	irq;
	unsigned		harvest_config;
	struct drm_sched_entity	entity;
	uint32_t                srbm_soft_reset;
	unsigned		num_rings;
};

int amdgpu_vce_sw_init(struct amdgpu_device *adev, unsigned long size);
int amdgpu_vce_sw_fini(struct amdgpu_device *adev);
int amdgpu_vce_entity_init(struct amdgpu_device *adev, struct amdgpu_ring *ring);
int amdgpu_vce_suspend(struct amdgpu_device *adev);
int amdgpu_vce_resume(struct amdgpu_device *adev);
void amdgpu_vce_free_handles(struct amdgpu_device *adev, struct drm_file *filp);
int amdgpu_vce_ring_parse_cs(struct amdgpu_cs_parser *p, struct amdgpu_job *job,
			     struct amdgpu_ib *ib);
int amdgpu_vce_ring_parse_cs_vm(struct amdgpu_cs_parser *p,
				struct amdgpu_job *job,
				struct amdgpu_ib *ib);
void amdgpu_vce_ring_emit_ib(struct amdgpu_ring *ring, struct amdgpu_job *job,
				struct amdgpu_ib *ib, uint32_t flags);
void amdgpu_vce_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				unsigned flags);
int amdgpu_vce_ring_test_ring(struct amdgpu_ring *ring);
int amdgpu_vce_ring_test_ib(struct amdgpu_ring *ring, long timeout);
void amdgpu_vce_ring_begin_use(struct amdgpu_ring *ring);
void amdgpu_vce_ring_end_use(struct amdgpu_ring *ring);
unsigned amdgpu_vce_ring_get_emit_ib_size(struct amdgpu_ring *ring);
unsigned amdgpu_vce_ring_get_dma_frame_size(struct amdgpu_ring *ring);
enum amdgpu_ring_priority_level amdgpu_vce_get_ring_prio(int ring);

#endif
