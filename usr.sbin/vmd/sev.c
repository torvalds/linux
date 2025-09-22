/*	$OpenBSD: sev.c,v 1.7 2025/06/04 08:21:29 bluhm Exp $	*/

/*
 * Copyright (c) 2023-2025 Hans-Joerg Hoexer <hshoexer@genua.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>	/* roundup */

#include <crypto/xform.h>
#include <dev/ic/pspvar.h>

#include <string.h>

#include "vmd.h"

extern struct vmd_vm	*current_vm;

/*
 * Prepare guest to use SEV.
 *
 * This asks the PSP to create a new crypto context including a
 * memory encryption key and assign a handle to the context.
 *
 * When the PSP driver psp(4) attaches, it initializes the platform.
 * If this fails for whatever reason we can not run a guest using SEV.
 */
int
sev_init(struct vmd_vm *vm)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params	*vcp = &vmc->vmc_params;
	uint32_t		 handle;
	uint16_t		 pstate;
	uint8_t			 gstate;

	if (!vcp->vcp_sev)
		return (0);

	if (psp_get_pstate(&pstate, NULL, NULL, NULL, NULL)) {
		log_warnx("%s: failed to get platform state", __func__);
		return (-1);
	}
	if (pstate == PSP_PSTATE_UNINIT) {
		log_warnx("%s: platform uninitialized", __func__);
		return (-1);
	}

	if (psp_launch_start(&handle, vcp->vcp_seves) < 0) {
		log_warnx("%s: launch failed", __func__);
		return (-1);
	}
	vm->vm_sev_handle = handle;

	if (psp_get_gstate(vm->vm_sev_handle, NULL, NULL, &gstate)) {
		log_warnx("%s: failed to get guest state", __func__);
		return (-1);
	}
	if (gstate != PSP_GSTATE_LUPDATE) {
		log_warnx("%s: invalid guest state: 0x%hx", __func__, gstate);
		return (-1);
	}

	return (0);
}

/*
 * Record memory segments to be encrypted for SEV.
 */
int
sev_register_encryption(vaddr_t addr, size_t size)
{
	struct vmop_create_params *vmc;
	struct vm_create_params *vcp;
	struct vm_mem_range	*vmr;
	size_t			 off;
	int			 i;

	vmc = &current_vm->vm_params;
	vcp = &vmc->vmc_params;

	if (!vcp->vcp_sev)
		return (0);

	if (size == 0)
		return (0);

	/* Adjust address and size to be aligend to AES_XTS_BLOCKSIZE. */
	if (addr & (AES_XTS_BLOCKSIZE - 1)) {
		size += (addr & (AES_XTS_BLOCKSIZE - 1));
		addr &= ~(AES_XTS_BLOCKSIZE - 1);
	}

	vmr = find_gpa_range(&current_vm->vm_params.vmc_params, addr, size);
	if (vmr == NULL) {
		log_warnx("%s: failed - invalid memory range addr = 0x%lx, "
		    "len = 0x%zx", __func__, addr, size);
		return (-1);
	}
	if (current_vm->vm_sev_nmemsegments ==
	    nitems(current_vm->vm_sev_memsegments)) {
		log_warnx("%s: failed - out of SEV memory segments", __func__);
		return (-1);
	}
	i = current_vm->vm_sev_nmemsegments++;

	off = addr - vmr->vmr_gpa;

	current_vm->vm_sev_memsegments[i].vmr_va = vmr->vmr_va + off;
	current_vm->vm_sev_memsegments[i].vmr_size = size;
	current_vm->vm_sev_memsegments[i].vmr_gpa = vmr->vmr_gpa + off;

	log_debug("%s: i %d addr 0x%lx size 0x%lx vmr_va 0x%lx vmr_gpa 0x%lx "
	    "vmr_size 0x%lx", __func__, i, addr, size,
	    current_vm->vm_sev_memsegments[i].vmr_va,
	    current_vm->vm_sev_memsegments[i].vmr_gpa,
	    current_vm->vm_sev_memsegments[i].vmr_size);

	return (0);
}

/*
 * Encrypt and measure previously recorded memroy segments.
 *
 * This encrypts the memory initially used by the guest.  This
 * includes the kernel or BIOS image, initial stack, boot arguments
 * and page tables.
 *
 * We also ask the PSP to provide a measurement.  However, right
 * now we can not really verify it.
 */
int
sev_encrypt_memory(struct vmd_vm *vm)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params *vcp = &vmc->vmc_params;
	struct vm_mem_range	*vmr;
	size_t			 i;

	if (!vcp->vcp_sev)
		return (0);

	for (i = 0; i < vm->vm_sev_nmemsegments; i++) {
		vmr = &vm->vm_sev_memsegments[i];

		/* tell PSP to encrypt this range */
		if (psp_launch_update(vm->vm_sev_handle, vmr->vmr_va,
		    roundup(vmr->vmr_size, AES_XTS_BLOCKSIZE))) {
			log_warnx("%s: failed to launch update page "
			    "%zu:0x%lx", __func__, i, vmr->vmr_va);
			return (-1);
		}

		log_debug("%s: encrypted %zu:0x%lx size 0x%lx", __func__, i,
		    vmr->vmr_va, vmr->vmr_size);
	}

	return (0);
}


/*
 * Activate a guest's SEV crypto state.
 */
int
sev_activate(struct vmd_vm *vm, int vcpu_id)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params *vcp = &vmc->vmc_params;
	uint8_t			 gstate;

	if (!vcp->vcp_sev)
		return (0);

	if (psp_df_flush() ||
	    psp_activate(vm->vm_sev_handle, vm->vm_sev_asid[vcpu_id])) {
		log_warnx("%s: failed to activate guest: 0x%x:0x%x", __func__,
		    vm->vm_sev_handle, vm->vm_sev_asid[vcpu_id]);
		return (-1);
	}

	if (psp_get_gstate(vm->vm_sev_handle, NULL, NULL, &gstate)) {
		log_warnx("%s: failed to get guest state", __func__);
		return (-1);
	}
	if (gstate != PSP_GSTATE_LUPDATE) {
		log_warnx("%s: invalid guest state: 0x%hx", __func__, gstate);
		return (-1);
	}

	return (0);
}


int
sev_encrypt_state(struct vmd_vm *vm, int vcpu_id)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params *vcp = &vmc->vmc_params;

	if (!vcp->vcp_seves)
		return (0);

	if (psp_encrypt_state(vm->vm_sev_handle, vm->vm_sev_asid[vcpu_id],
	    vcp->vcp_id, vcpu_id)) {
		log_warnx("%s: failed to encrypt state: 0x%x 0x%x 0x%0x 0x%0x",
		    __func__, vm->vm_sev_handle, vm->vm_sev_asid[vcpu_id],
		    vm->vm_vmid, vcpu_id);
		return (-1);
	}

	return (0);
}

int
sev_launch_finalize(struct vmd_vm *vm)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params *vcp = &vmc->vmc_params;
	uint8_t		gstate;

	if (!vcp->vcp_sev)
		return (0);

	if (psp_launch_measure(vm->vm_sev_handle)) {
		log_warnx("%s: failed to launch measure", __func__);
		return (-1);
	}
	if (psp_launch_finish(vm->vm_sev_handle)) {
		log_warnx("%s: failed to launch finish", __func__);
		return (-1);
	}

	if (psp_get_gstate(vm->vm_sev_handle, NULL, NULL, &gstate)) {
		log_warnx("%s: failed to get guest state", __func__);
		return (-1);
	}
	if (gstate != PSP_GSTATE_RUNNING) {
		log_warnx("%s: invalid guest state: 0x%hx", __func__, gstate);
		return (-1);
	}

	return (0);
}

/*
 * Deactivate and decommission a guest's SEV crypto state.
 */
int
sev_shutdown(struct vmd_vm *vm)
{
	struct vmop_create_params *vmc = &vm->vm_params;
	struct vm_create_params *vcp = &vmc->vmc_params;

	if (!vcp->vcp_sev)
		return (0);

	if (psp_guest_shutdown(vm->vm_sev_handle)) {
		log_warnx("failed to deactivate guest");
		return (-1);
	}
	vm->vm_sev_handle = 0;

	return (0);
}
