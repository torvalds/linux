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
 */

#include "amdgpu_vm.h"
#include "amdgpu_object.h"
#include "amdgpu_trace.h"

/**
 * amdgpu_vm_cpu_map_table - make sure new PDs/PTs are kmapped
 *
 * @table: newly allocated or validated PD/PT
 */
static int amdgpu_vm_cpu_map_table(struct amdgpu_bo_vm *table)
{
	table->bo.flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	return amdgpu_bo_kmap(&table->bo, NULL);
}

/**
 * amdgpu_vm_cpu_prepare - prepare page table update with the CPU
 *
 * @p: see amdgpu_vm_update_params definition
 * @sync: sync obj with fences to wait on
 *
 * Returns:
 * Negativ errno, 0 for success.
 */
static int amdgpu_vm_cpu_prepare(struct amdgpu_vm_update_params *p,
				 struct amdgpu_sync *sync)
{
	if (!sync)
		return 0;

	return amdgpu_sync_wait(sync, true);
}

/**
 * amdgpu_vm_cpu_update - helper to update page tables via CPU
 *
 * @p: see amdgpu_vm_update_params definition
 * @vmbo: PD/PT to update
 * @pe: byte offset of the PDE/PTE, relative to start of PDB/PTB
 * @addr: dst addr to write into pe
 * @count: number of page entries to update
 * @incr: increase next addr by incr bytes
 * @flags: hw access flags
 *
 * Write count number of PT/PD entries directly.
 */
static int amdgpu_vm_cpu_update(struct amdgpu_vm_update_params *p,
				struct amdgpu_bo_vm *vmbo, uint64_t pe,
				uint64_t addr, unsigned count, uint32_t incr,
				uint64_t flags)
{
	unsigned int i;
	uint64_t value;
	long r;

	r = dma_resv_wait_timeout(vmbo->bo.tbo.base.resv, DMA_RESV_USAGE_KERNEL,
				  true, MAX_SCHEDULE_TIMEOUT);
	if (r < 0)
		return r;

	pe += (unsigned long)amdgpu_bo_kptr(&vmbo->bo);

	trace_amdgpu_vm_set_ptes(pe, addr, count, incr, flags, p->immediate);

	for (i = 0; i < count; i++) {
		value = p->pages_addr ?
			amdgpu_vm_map_gart(p->pages_addr, addr) :
			addr;
		amdgpu_gmc_set_pte_pde(p->adev, (void *)(uintptr_t)pe,
				       i, value, flags);
		addr += incr;
	}
	return 0;
}

/**
 * amdgpu_vm_cpu_commit - commit page table update to the HW
 *
 * @p: see amdgpu_vm_update_params definition
 * @fence: unused
 *
 * Make sure that the hardware sees the page table updates.
 */
static int amdgpu_vm_cpu_commit(struct amdgpu_vm_update_params *p,
				struct dma_fence **fence)
{
	if (p->needs_flush)
		atomic64_inc(&p->vm->tlb_seq);

	mb();
	amdgpu_device_flush_hdp(p->adev, NULL);
	return 0;
}

const struct amdgpu_vm_update_funcs amdgpu_vm_cpu_funcs = {
	.map_table = amdgpu_vm_cpu_map_table,
	.prepare = amdgpu_vm_cpu_prepare,
	.update = amdgpu_vm_cpu_update,
	.commit = amdgpu_vm_cpu_commit
};
