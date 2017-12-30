/******************************************************************************
 * hypercall.h
 *
 * Linux-specific hypervisor handling.
 *
 * Stefano Stabellini <stefano.stabellini@eu.citrix.com>, Citrix, 2012
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _ASM_ARM_XEN_HYPERCALL_H
#define _ASM_ARM_XEN_HYPERCALL_H

#include <linux/bug.h>

#include <xen/interface/xen.h>
#include <xen/interface/sched.h>
#include <xen/interface/platform.h>

struct xen_dm_op_buf;

long privcmd_call(unsigned call, unsigned long a1,
		unsigned long a2, unsigned long a3,
		unsigned long a4, unsigned long a5);
int HYPERVISOR_xen_version(int cmd, void *arg);
int HYPERVISOR_console_io(int cmd, int count, char *str);
int HYPERVISOR_grant_table_op(unsigned int cmd, void *uop, unsigned int count);
int HYPERVISOR_sched_op(int cmd, void *arg);
int HYPERVISOR_event_channel_op(int cmd, void *arg);
unsigned long HYPERVISOR_hvm_op(int op, void *arg);
int HYPERVISOR_memory_op(unsigned int cmd, void *arg);
int HYPERVISOR_physdev_op(int cmd, void *arg);
int HYPERVISOR_vcpu_op(int cmd, int vcpuid, void *extra_args);
int HYPERVISOR_tmem_op(void *arg);
int HYPERVISOR_vm_assist(unsigned int cmd, unsigned int type);
int HYPERVISOR_dm_op(domid_t domid, unsigned int nr_bufs,
		     struct xen_dm_op_buf *bufs);
int HYPERVISOR_platform_op_raw(void *arg);
static inline int HYPERVISOR_platform_op(struct xen_platform_op *op)
{
	op->interface_version = XENPF_INTERFACE_VERSION;
	return HYPERVISOR_platform_op_raw(op);
}
int HYPERVISOR_multicall(struct multicall_entry *calls, uint32_t nr);

static inline int
HYPERVISOR_suspend(unsigned long start_info_mfn)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_suspend };

	/* start_info_mfn is unused on ARM */
	return HYPERVISOR_sched_op(SCHEDOP_shutdown, &r);
}

static inline void
MULTI_update_va_mapping(struct multicall_entry *mcl, unsigned long va,
			unsigned int new_val, unsigned long flags)
{
	BUG();
}

static inline void
MULTI_mmu_update(struct multicall_entry *mcl, struct mmu_update *req,
		 int count, int *success_count, domid_t domid)
{
	BUG();
}

#endif /* _ASM_ARM_XEN_HYPERCALL_H */
