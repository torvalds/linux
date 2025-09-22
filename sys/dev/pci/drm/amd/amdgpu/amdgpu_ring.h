/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Authors: Christian König
 */
#ifndef __AMDGPU_RING_H__
#define __AMDGPU_RING_H__

#include <drm/amdgpu_drm.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_print.h>
#include <drm/drm_suballoc.h>

struct amdgpu_device;
struct amdgpu_ring;
struct amdgpu_ib;
struct amdgpu_cs_parser;
struct amdgpu_job;
struct amdgpu_vm;

/* max number of rings */
#define AMDGPU_MAX_RINGS		124
#define AMDGPU_MAX_HWIP_RINGS		64
#define AMDGPU_MAX_GFX_RINGS		2
#define AMDGPU_MAX_SW_GFX_RINGS         2
#define AMDGPU_MAX_COMPUTE_RINGS	8
#define AMDGPU_MAX_VCE_RINGS		3
#define AMDGPU_MAX_UVD_ENC_RINGS	2
#define AMDGPU_MAX_VPE_RINGS		2

enum amdgpu_ring_priority_level {
	AMDGPU_RING_PRIO_0,
	AMDGPU_RING_PRIO_1,
	AMDGPU_RING_PRIO_DEFAULT = 1,
	AMDGPU_RING_PRIO_2,
	AMDGPU_RING_PRIO_MAX
};

/* some special values for the owner field */
#define AMDGPU_FENCE_OWNER_UNDEFINED	((void *)0ul)
#define AMDGPU_FENCE_OWNER_VM		((void *)1ul)
#define AMDGPU_FENCE_OWNER_KFD		((void *)2ul)

#define AMDGPU_FENCE_FLAG_64BIT         (1 << 0)
#define AMDGPU_FENCE_FLAG_INT           (1 << 1)
#define AMDGPU_FENCE_FLAG_TC_WB_ONLY    (1 << 2)
#define AMDGPU_FENCE_FLAG_EXEC          (1 << 3)

#define to_amdgpu_ring(s) container_of((s), struct amdgpu_ring, sched)

#define AMDGPU_IB_POOL_SIZE	(1024 * 1024)

enum amdgpu_ring_type {
	AMDGPU_RING_TYPE_GFX		= AMDGPU_HW_IP_GFX,
	AMDGPU_RING_TYPE_COMPUTE	= AMDGPU_HW_IP_COMPUTE,
	AMDGPU_RING_TYPE_SDMA		= AMDGPU_HW_IP_DMA,
	AMDGPU_RING_TYPE_UVD		= AMDGPU_HW_IP_UVD,
	AMDGPU_RING_TYPE_VCE		= AMDGPU_HW_IP_VCE,
	AMDGPU_RING_TYPE_UVD_ENC	= AMDGPU_HW_IP_UVD_ENC,
	AMDGPU_RING_TYPE_VCN_DEC	= AMDGPU_HW_IP_VCN_DEC,
	AMDGPU_RING_TYPE_VCN_ENC	= AMDGPU_HW_IP_VCN_ENC,
	AMDGPU_RING_TYPE_VCN_JPEG	= AMDGPU_HW_IP_VCN_JPEG,
	AMDGPU_RING_TYPE_VPE		= AMDGPU_HW_IP_VPE,
	AMDGPU_RING_TYPE_KIQ,
	AMDGPU_RING_TYPE_MES,
	AMDGPU_RING_TYPE_UMSCH_MM,
};

enum amdgpu_ib_pool_type {
	/* Normal submissions to the top of the pipeline. */
	AMDGPU_IB_POOL_DELAYED,
	/* Immediate submissions to the bottom of the pipeline. */
	AMDGPU_IB_POOL_IMMEDIATE,
	/* Direct submission to the ring buffer during init and reset. */
	AMDGPU_IB_POOL_DIRECT,

	AMDGPU_IB_POOL_MAX
};

struct amdgpu_ib {
	struct drm_suballoc		*sa_bo;
	uint32_t			length_dw;
	uint64_t			gpu_addr;
	uint32_t			*ptr;
	uint32_t			flags;
};

struct amdgpu_sched {
	u32				num_scheds;
	struct drm_gpu_scheduler	*sched[AMDGPU_MAX_HWIP_RINGS];
};

/*
 * Fences.
 */
struct amdgpu_fence_driver {
	uint64_t			gpu_addr;
	volatile uint32_t		*cpu_addr;
	/* sync_seq is protected by ring emission lock */
	uint32_t			sync_seq;
	atomic_t			last_seq;
	bool				initialized;
	struct amdgpu_irq_src		*irq_src;
	unsigned			irq_type;
	struct timeout			fallback_timer;
	unsigned			num_fences_mask;
	spinlock_t			lock;
	struct dma_fence		**fences;
};

/*
 * Fences mark an event in the GPUs pipeline and are used
 * for GPU/CPU synchronization.  When the fence is written,
 * it is expected that all buffers associated with that fence
 * are no longer in use by the associated ring on the GPU and
 * that the relevant GPU caches have been flushed.
 */

struct amdgpu_fence {
	struct dma_fence base;

	/* RB, DMA, etc. */
	struct amdgpu_ring		*ring;
	ktime_t				start_timestamp;
};

extern const struct drm_sched_backend_ops amdgpu_sched_ops;

void amdgpu_fence_driver_clear_job_fences(struct amdgpu_ring *ring);
void amdgpu_fence_driver_set_error(struct amdgpu_ring *ring, int error);
void amdgpu_fence_driver_force_completion(struct amdgpu_ring *ring);

int amdgpu_fence_driver_init_ring(struct amdgpu_ring *ring);
int amdgpu_fence_driver_start_ring(struct amdgpu_ring *ring,
				   struct amdgpu_irq_src *irq_src,
				   unsigned irq_type);
void amdgpu_fence_driver_hw_init(struct amdgpu_device *adev);
void amdgpu_fence_driver_hw_fini(struct amdgpu_device *adev);
int amdgpu_fence_driver_sw_init(struct amdgpu_device *adev);
void amdgpu_fence_driver_sw_fini(struct amdgpu_device *adev);
int amdgpu_fence_emit(struct amdgpu_ring *ring, struct dma_fence **fence, struct amdgpu_job *job,
		      unsigned flags);
int amdgpu_fence_emit_polling(struct amdgpu_ring *ring, uint32_t *s,
			      uint32_t timeout);
bool amdgpu_fence_process(struct amdgpu_ring *ring);
int amdgpu_fence_wait_empty(struct amdgpu_ring *ring);
signed long amdgpu_fence_wait_polling(struct amdgpu_ring *ring,
				      uint32_t wait_seq,
				      signed long timeout);
unsigned amdgpu_fence_count_emitted(struct amdgpu_ring *ring);

void amdgpu_fence_driver_isr_toggle(struct amdgpu_device *adev, bool stop);

u64 amdgpu_fence_last_unsignaled_time_us(struct amdgpu_ring *ring);
void amdgpu_fence_update_start_timestamp(struct amdgpu_ring *ring, uint32_t seq,
					 ktime_t timestamp);

/*
 * Rings.
 */

/* provided by hw blocks that expose a ring buffer for commands */
struct amdgpu_ring_funcs {
	enum amdgpu_ring_type	type;
	uint32_t		align_mask;
	u32			nop;
	bool			support_64bit_ptrs;
	bool			no_user_fence;
	bool			secure_submission_supported;
	unsigned		extra_dw;

	/* ring read/write ptr handling */
	u64 (*get_rptr)(struct amdgpu_ring *ring);
	u64 (*get_wptr)(struct amdgpu_ring *ring);
	void (*set_wptr)(struct amdgpu_ring *ring);
	/* validating and patching of IBs */
	int (*parse_cs)(struct amdgpu_cs_parser *p,
			struct amdgpu_job *job,
			struct amdgpu_ib *ib);
	int (*patch_cs_in_place)(struct amdgpu_cs_parser *p,
				 struct amdgpu_job *job,
				 struct amdgpu_ib *ib);
	/* constants to calculate how many DW are needed for an emit */
	unsigned emit_frame_size;
	unsigned emit_ib_size;
	/* command emit functions */
	void (*emit_ib)(struct amdgpu_ring *ring,
			struct amdgpu_job *job,
			struct amdgpu_ib *ib,
			uint32_t flags);
	void (*emit_fence)(struct amdgpu_ring *ring, uint64_t addr,
			   uint64_t seq, unsigned flags);
	void (*emit_pipeline_sync)(struct amdgpu_ring *ring);
	void (*emit_vm_flush)(struct amdgpu_ring *ring, unsigned vmid,
			      uint64_t pd_addr);
	void (*emit_hdp_flush)(struct amdgpu_ring *ring);
	void (*emit_gds_switch)(struct amdgpu_ring *ring, uint32_t vmid,
				uint32_t gds_base, uint32_t gds_size,
				uint32_t gws_base, uint32_t gws_size,
				uint32_t oa_base, uint32_t oa_size);
	/* testing functions */
	int (*test_ring)(struct amdgpu_ring *ring);
	int (*test_ib)(struct amdgpu_ring *ring, long timeout);
	/* insert NOP packets */
	void (*insert_nop)(struct amdgpu_ring *ring, uint32_t count);
	void (*insert_start)(struct amdgpu_ring *ring);
	void (*insert_end)(struct amdgpu_ring *ring);
	/* pad the indirect buffer to the necessary number of dw */
	void (*pad_ib)(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
	unsigned (*init_cond_exec)(struct amdgpu_ring *ring, uint64_t addr);
	/* note usage for clock and power gating */
	void (*begin_use)(struct amdgpu_ring *ring);
	void (*end_use)(struct amdgpu_ring *ring);
	void (*emit_switch_buffer) (struct amdgpu_ring *ring);
	void (*emit_cntxcntl) (struct amdgpu_ring *ring, uint32_t flags);
	void (*emit_gfx_shadow)(struct amdgpu_ring *ring, u64 shadow_va, u64 csa_va,
				u64 gds_va, bool init_shadow, int vmid);
	void (*emit_rreg)(struct amdgpu_ring *ring, uint32_t reg,
			  uint32_t reg_val_offs);
	void (*emit_wreg)(struct amdgpu_ring *ring, uint32_t reg, uint32_t val);
	void (*emit_reg_wait)(struct amdgpu_ring *ring, uint32_t reg,
			      uint32_t val, uint32_t mask);
	void (*emit_reg_write_reg_wait)(struct amdgpu_ring *ring,
					uint32_t reg0, uint32_t reg1,
					uint32_t ref, uint32_t mask);
	void (*emit_frame_cntl)(struct amdgpu_ring *ring, bool start,
				bool secure);
	/* Try to soft recover the ring to make the fence signal */
	void (*soft_recovery)(struct amdgpu_ring *ring, unsigned vmid);
	int (*preempt_ib)(struct amdgpu_ring *ring);
	void (*emit_mem_sync)(struct amdgpu_ring *ring);
	void (*emit_wave_limit)(struct amdgpu_ring *ring, bool enable);
	void (*patch_cntl)(struct amdgpu_ring *ring, unsigned offset);
	void (*patch_ce)(struct amdgpu_ring *ring, unsigned offset);
	void (*patch_de)(struct amdgpu_ring *ring, unsigned offset);
	int (*reset)(struct amdgpu_ring *ring, unsigned int vmid);
	void (*emit_cleaner_shader)(struct amdgpu_ring *ring);
};

struct amdgpu_ring {
	struct amdgpu_device		*adev;
	const struct amdgpu_ring_funcs	*funcs;
	struct amdgpu_fence_driver	fence_drv;
	struct drm_gpu_scheduler	sched;

	struct amdgpu_bo	*ring_obj;
	volatile uint32_t	*ring;
	unsigned		rptr_offs;
	u64			rptr_gpu_addr;
	volatile u32		*rptr_cpu_addr;
	u64			wptr;
	u64			wptr_old;
	unsigned		ring_size;
	unsigned		max_dw;
	int			count_dw;
	uint64_t		gpu_addr;
	uint64_t		ptr_mask;
	uint32_t		buf_mask;
	u32			idx;
	u32			xcc_id;
	u32			xcp_id;
	u32			me;
	u32			pipe;
	u32			queue;
	struct amdgpu_bo	*mqd_obj;
	uint64_t                mqd_gpu_addr;
	void                    *mqd_ptr;
	unsigned                mqd_size;
	uint64_t                eop_gpu_addr;
	u32			doorbell_index;
	bool			use_doorbell;
	bool			use_pollmem;
	unsigned		wptr_offs;
	u64			wptr_gpu_addr;
	volatile u32		*wptr_cpu_addr;
	unsigned		fence_offs;
	u64			fence_gpu_addr;
	volatile u32		*fence_cpu_addr;
	uint64_t		current_ctx;
	char			name[16];
	u32                     trail_seq;
	unsigned		trail_fence_offs;
	u64			trail_fence_gpu_addr;
	volatile u32		*trail_fence_cpu_addr;
	unsigned		cond_exe_offs;
	u64			cond_exe_gpu_addr;
	volatile u32		*cond_exe_cpu_addr;
	unsigned int		set_q_mode_offs;
	volatile u32		*set_q_mode_ptr;
	u64			set_q_mode_token;
	unsigned		vm_hub;
	unsigned		vm_inv_eng;
	struct dma_fence	*vmid_wait;
	bool			has_compute_vm_bug;
	bool			no_scheduler;
	int			hw_prio;
	unsigned 		num_hw_submission;
	atomic_t		*sched_score;

	/* used for mes */
	bool			is_mes_queue;
	uint32_t		hw_queue_id;
	struct amdgpu_mes_ctx_data *mes_ctx;

	bool            is_sw_ring;
	unsigned int    entry_index;

};

#define amdgpu_ring_parse_cs(r, p, job, ib) ((r)->funcs->parse_cs((p), (job), (ib)))
#define amdgpu_ring_patch_cs_in_place(r, p, job, ib) ((r)->funcs->patch_cs_in_place((p), (job), (ib)))
#define amdgpu_ring_test_ring(r) (r)->funcs->test_ring((r))
#define amdgpu_ring_test_ib(r, t) ((r)->funcs->test_ib ? (r)->funcs->test_ib((r), (t)) : 0)
#define amdgpu_ring_get_rptr(r) (r)->funcs->get_rptr((r))
#define amdgpu_ring_get_wptr(r) (r)->funcs->get_wptr((r))
#define amdgpu_ring_set_wptr(r) (r)->funcs->set_wptr((r))
#define amdgpu_ring_emit_ib(r, job, ib, flags) ((r)->funcs->emit_ib((r), (job), (ib), (flags)))
#define amdgpu_ring_emit_pipeline_sync(r) (r)->funcs->emit_pipeline_sync((r))
#define amdgpu_ring_emit_vm_flush(r, vmid, addr) (r)->funcs->emit_vm_flush((r), (vmid), (addr))
#define amdgpu_ring_emit_fence(r, addr, seq, flags) (r)->funcs->emit_fence((r), (addr), (seq), (flags))
#define amdgpu_ring_emit_gds_switch(r, v, db, ds, wb, ws, ab, as) (r)->funcs->emit_gds_switch((r), (v), (db), (ds), (wb), (ws), (ab), (as))
#define amdgpu_ring_emit_hdp_flush(r) (r)->funcs->emit_hdp_flush((r))
#define amdgpu_ring_emit_switch_buffer(r) (r)->funcs->emit_switch_buffer((r))
#define amdgpu_ring_emit_cntxcntl(r, d) (r)->funcs->emit_cntxcntl((r), (d))
#define amdgpu_ring_emit_gfx_shadow(r, s, c, g, i, v) ((r)->funcs->emit_gfx_shadow((r), (s), (c), (g), (i), (v)))
#define amdgpu_ring_emit_rreg(r, d, o) (r)->funcs->emit_rreg((r), (d), (o))
#define amdgpu_ring_emit_wreg(r, d, v) (r)->funcs->emit_wreg((r), (d), (v))
#define amdgpu_ring_emit_reg_wait(r, d, v, m) (r)->funcs->emit_reg_wait((r), (d), (v), (m))
#define amdgpu_ring_emit_reg_write_reg_wait(r, d0, d1, v, m) (r)->funcs->emit_reg_write_reg_wait((r), (d0), (d1), (v), (m))
#define amdgpu_ring_emit_frame_cntl(r, b, s) (r)->funcs->emit_frame_cntl((r), (b), (s))
#define amdgpu_ring_pad_ib(r, ib) ((r)->funcs->pad_ib((r), (ib)))
#define amdgpu_ring_init_cond_exec(r, a) (r)->funcs->init_cond_exec((r), (a))
#define amdgpu_ring_preempt_ib(r) (r)->funcs->preempt_ib(r)
#define amdgpu_ring_patch_cntl(r, o) ((r)->funcs->patch_cntl((r), (o)))
#define amdgpu_ring_patch_ce(r, o) ((r)->funcs->patch_ce((r), (o)))
#define amdgpu_ring_patch_de(r, o) ((r)->funcs->patch_de((r), (o)))
#define amdgpu_ring_reset(r, v) (r)->funcs->reset((r), (v))

unsigned int amdgpu_ring_max_ibs(enum amdgpu_ring_type type);
int amdgpu_ring_alloc(struct amdgpu_ring *ring, unsigned ndw);
void amdgpu_ring_ib_begin(struct amdgpu_ring *ring);
void amdgpu_ring_ib_end(struct amdgpu_ring *ring);
void amdgpu_ring_ib_on_emit_cntl(struct amdgpu_ring *ring);
void amdgpu_ring_ib_on_emit_ce(struct amdgpu_ring *ring);
void amdgpu_ring_ib_on_emit_de(struct amdgpu_ring *ring);

void amdgpu_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count);
void amdgpu_ring_generic_pad_ib(struct amdgpu_ring *ring, struct amdgpu_ib *ib);
void amdgpu_ring_commit(struct amdgpu_ring *ring);
void amdgpu_ring_undo(struct amdgpu_ring *ring);
int amdgpu_ring_init(struct amdgpu_device *adev, struct amdgpu_ring *ring,
		     unsigned int max_dw, struct amdgpu_irq_src *irq_src,
		     unsigned int irq_type, unsigned int hw_prio,
		     atomic_t *sched_score);
void amdgpu_ring_fini(struct amdgpu_ring *ring);
void amdgpu_ring_emit_reg_write_reg_wait_helper(struct amdgpu_ring *ring,
						uint32_t reg0, uint32_t val0,
						uint32_t reg1, uint32_t val1);
bool amdgpu_ring_soft_recovery(struct amdgpu_ring *ring, unsigned int vmid,
			       struct dma_fence *fence);

static inline void amdgpu_ring_set_preempt_cond_exec(struct amdgpu_ring *ring,
							bool cond_exec)
{
	*ring->cond_exe_cpu_addr = cond_exec;
}

static inline void amdgpu_ring_clear_ring(struct amdgpu_ring *ring)
{
	int i = 0;
	while (i <= ring->buf_mask)
		ring->ring[i++] = ring->funcs->nop;

}

static inline void amdgpu_ring_write(struct amdgpu_ring *ring, uint32_t v)
{
	if (ring->count_dw <= 0)
		DRM_ERROR("amdgpu: writing more dwords to the ring than expected!\n");
	ring->ring[ring->wptr++ & ring->buf_mask] = v;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw--;
}

static inline void amdgpu_ring_write_multiple(struct amdgpu_ring *ring,
					      void *src, int count_dw)
{
	unsigned occupied, chunk1, chunk2;
	void *dst;

	if (unlikely(ring->count_dw < count_dw))
		DRM_ERROR("amdgpu: writing more dwords to the ring than expected!\n");

	occupied = ring->wptr & ring->buf_mask;
	dst = (void *)&ring->ring[occupied];
	chunk1 = ring->buf_mask + 1 - occupied;
	chunk1 = (chunk1 >= count_dw) ? count_dw : chunk1;
	chunk2 = count_dw - chunk1;
	chunk1 <<= 2;
	chunk2 <<= 2;

	if (chunk1)
		memcpy(dst, src, chunk1);

	if (chunk2) {
		src += chunk1;
		dst = (void *)ring->ring;
		memcpy(dst, src, chunk2);
	}

	ring->wptr += count_dw;
	ring->wptr &= ring->ptr_mask;
	ring->count_dw -= count_dw;
}

/**
 * amdgpu_ring_patch_cond_exec - patch dw count of conditional execute
 * @ring: amdgpu_ring structure
 * @offset: offset returned by amdgpu_ring_init_cond_exec
 *
 * Calculate the dw count and patch it into a cond_exec command.
 */
static inline void amdgpu_ring_patch_cond_exec(struct amdgpu_ring *ring,
					       unsigned int offset)
{
	unsigned cur;

	if (!ring->funcs->init_cond_exec)
		return;

	WARN_ON(offset > ring->buf_mask);
	WARN_ON(ring->ring[offset] != 0);

	cur = (ring->wptr - 1) & ring->buf_mask;
	if (cur < offset)
		cur += ring->ring_size >> 2;
	ring->ring[offset] = cur - offset;
}

#define amdgpu_mes_ctx_get_offs_gpu_addr(ring, offset)			\
	(ring->is_mes_queue && ring->mes_ctx ?				\
	 (ring->mes_ctx->meta_data_gpu_addr + offset) : 0)

#define amdgpu_mes_ctx_get_offs_cpu_addr(ring, offset)			\
	(ring->is_mes_queue && ring->mes_ctx ?				\
	 (void *)((uint8_t *)(ring->mes_ctx->meta_data_ptr) + offset) : \
	 NULL)

int amdgpu_ring_test_helper(struct amdgpu_ring *ring);

void amdgpu_debugfs_ring_init(struct amdgpu_device *adev,
			      struct amdgpu_ring *ring);

int amdgpu_ring_init_mqd(struct amdgpu_ring *ring);

static inline u32 amdgpu_ib_get_value(struct amdgpu_ib *ib, int idx)
{
	return ib->ptr[idx];
}

static inline void amdgpu_ib_set_value(struct amdgpu_ib *ib, int idx,
				       uint32_t value)
{
	ib->ptr[idx] = value;
}

int amdgpu_ib_get(struct amdgpu_device *adev, struct amdgpu_vm *vm,
		  unsigned size,
		  enum amdgpu_ib_pool_type pool,
		  struct amdgpu_ib *ib);
void amdgpu_ib_free(struct amdgpu_device *adev, struct amdgpu_ib *ib,
		    struct dma_fence *f);
int amdgpu_ib_schedule(struct amdgpu_ring *ring, unsigned num_ibs,
		       struct amdgpu_ib *ibs, struct amdgpu_job *job,
		       struct dma_fence **f);
int amdgpu_ib_pool_init(struct amdgpu_device *adev);
void amdgpu_ib_pool_fini(struct amdgpu_device *adev);
int amdgpu_ib_ring_tests(struct amdgpu_device *adev);
bool amdgpu_ring_sched_ready(struct amdgpu_ring *ring);
#endif
