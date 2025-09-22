/* $OpenBSD: vmm.c,v 1.6 2025/09/14 15:52:28 mlarkin Exp $ */
/*
 * Copyright (c) 2014-2023 Mike Larkin <mlarkin@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/pool.h>
#include <sys/pledge.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_aobj.h>

#include <machine/vmmvar.h>

#include <dev/vmm/vmm.h>

struct vmm_softc *vmm_softc;
struct pool vm_pool;
struct pool vcpu_pool;

struct cfdriver vmm_cd = {
	NULL, "vmm", DV_DULL, CD_SKIPHIBERNATE
};

const struct cfattach vmm_ca = {
	sizeof(struct vmm_softc), vmm_probe, vmm_attach, NULL, vmm_activate
};

int
vmm_probe(struct device *parent, void *match, void *aux)
{
	const char **busname = (const char **)aux;

	if (strcmp(*busname, vmm_cd.cd_name) != 0)
		return (0);
	return (vmm_probe_machdep(parent, match, aux));
}

void
vmm_attach(struct device *parent, struct device *self, void *aux)
{
	struct vmm_softc *sc = (struct vmm_softc *)self;

	rw_init(&sc->sc_slock, "vmmslk");
	sc->sc_status = VMM_ACTIVE;
	refcnt_init(&sc->sc_refcnt);

	sc->vcpu_ct = 0;
	sc->vcpu_max = VMM_MAX_VCPUS;
	sc->vm_ct = 0;
	sc->vm_idx = 0;

	SLIST_INIT(&sc->vm_list);
	rw_init(&sc->vm_lock, "vm_list");

	pool_init(&vm_pool, sizeof(struct vm), 0, IPL_MPFLOOR, PR_WAITOK,
	    "vmpool", NULL);
	pool_init(&vcpu_pool, sizeof(struct vcpu), 64, IPL_MPFLOOR, PR_WAITOK,
	    "vcpupl", NULL);

	vmm_attach_machdep(parent, self, aux);

	vmm_softc = sc;
	printf("\n");
}

int
vmm_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_QUIESCE:
		/* Block device users as we're suspending operation. */
		rw_enter_write(&vmm_softc->sc_slock);
		KASSERT(vmm_softc->sc_status == VMM_ACTIVE);
		vmm_softc->sc_status = VMM_SUSPENDED;
		rw_exit_write(&vmm_softc->sc_slock);

		/* Wait for any device users to finish. */
		refcnt_finalize(&vmm_softc->sc_refcnt, "vmmsusp");

		vmm_activate_machdep(self, act);
		break;
	case DVACT_WAKEUP:
		vmm_activate_machdep(self, act);

		/* Set the device back to active. */
		rw_enter_write(&vmm_softc->sc_slock);
		KASSERT(vmm_softc->sc_status == VMM_SUSPENDED);
		refcnt_init(&vmm_softc->sc_refcnt);
		vmm_softc->sc_status = VMM_ACTIVE;
		rw_exit_write(&vmm_softc->sc_slock);

		/* Notify any waiting device users. */
		wakeup(&vmm_softc->sc_status);
		break;
	}

	return (0);
}

/*
 * vmmopen
 *
 * Called during open of /dev/vmm.
 *
 * Parameters:
 *  dev, flag, mode, p: These come from the character device and are
 *   all unused for this function
 *
 * Return values:
 *  ENODEV: if vmm(4) didn't attach or no supported CPUs detected
 *  0: successful open
 */
int
vmmopen(dev_t dev, int flag, int mode, struct proc *p)
{
	/* Don't allow open if we didn't attach */
	if (vmm_softc == NULL)
		return (ENODEV);

	/* Don't allow open if we didn't detect any supported CPUs */
	if (vmm_softc->mode == VMM_MODE_UNKNOWN)
		return (ENODEV);

	return 0;
}

/*
 * vmmclose
 *
 * Called when /dev/vmm is closed. Presently unused.
 */
int
vmmclose(dev_t dev, int flag, int mode, struct proc *p)
{
	return 0;
}

/*
 * vm_find
 *
 * Function to find an existing VM by its identifier.
 * Must be called under the global vm_lock.
 *
 * Parameters:
 *  id: The VM identifier.
 *  *res: A pointer to the VM or NULL if not found
 *
 * Return values:
 *  0: if successful
 *  ENOENT: if the VM defined by 'id' cannot be found
 *  EPERM: if the VM cannot be accessed by the current process
 */
int
vm_find(uint32_t id, struct vm **res)
{
	struct proc *p = curproc;
	struct vm *vm;
	int ret = ENOENT;

	*res = NULL;

	rw_enter_read(&vmm_softc->vm_lock);
	SLIST_FOREACH(vm, &vmm_softc->vm_list, vm_link) {
		if (vm->vm_id == id) {
			/*
			 * In the pledged VM process, only allow to find
			 * the VM that is running in the current process.
			 * The managing vmm parent process can lookup all
			 * all VMs and is indicated by PLEDGE_PROC.
			 */
			if (((p->p_pledge &
			    (PLEDGE_VMM | PLEDGE_PROC)) == PLEDGE_VMM) &&
			    (vm->vm_creator_pid != p->p_p->ps_pid))
				ret = EPERM;
			else {
				refcnt_take(&vm->vm_refcnt);
				*res = vm;
				ret = 0;
			}
			break;
		}
	}
	rw_exit_read(&vmm_softc->vm_lock);

	if (ret == EPERM)
		return (pledge_fail(p, EPERM, PLEDGE_VMM));
	return (ret);
}

/*
 * vmmioctl
 *
 * Main ioctl dispatch routine for /dev/vmm. Parses ioctl type and calls
 * appropriate lower level handler routine. Returns result to ioctl caller.
 */
int
vmmioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	int ret;

	KERNEL_UNLOCK();

	ret = rw_enter(&vmm_softc->sc_slock, RW_READ | RW_INTR);
	if (ret != 0)
		goto out;
	while (vmm_softc->sc_status != VMM_ACTIVE) {
		ret = rwsleep_nsec(&vmm_softc->sc_status, &vmm_softc->sc_slock,
		    PWAIT | PCATCH, "vmmresume", INFSLP);
		if (ret != 0) {
			rw_exit(&vmm_softc->sc_slock);
			goto out;
		}
	}
	refcnt_take(&vmm_softc->sc_refcnt);
	rw_exit(&vmm_softc->sc_slock);

	switch (cmd) {
	case VMM_IOC_CREATE:
		if ((ret = vmm_start()) != 0) {
			vmm_stop();
			break;
		}
		ret = vm_create((struct vm_create_params *)data, p);
		break;
	case VMM_IOC_RUN:
		ret = vm_run((struct vm_run_params *)data);
		break;
	case VMM_IOC_INFO:
		ret = vm_get_info((struct vm_info_params *)data);
		break;
	case VMM_IOC_TERM:
		ret = vm_terminate((struct vm_terminate_params *)data);
		break;
	case VMM_IOC_RESETCPU:
		ret = vm_resetcpu((struct vm_resetcpu_params *)data);
		break;
	case VMM_IOC_READREGS:
		ret = vm_rwregs((struct vm_rwregs_params *)data, 0);
		break;
	case VMM_IOC_WRITEREGS:
		ret = vm_rwregs((struct vm_rwregs_params *)data, 1);
		break;
	case VMM_IOC_READVMPARAMS:
		ret = vm_rwvmparams((struct vm_rwvmparams_params *)data, 0);
		break;
	case VMM_IOC_WRITEVMPARAMS:
		ret = vm_rwvmparams((struct vm_rwvmparams_params *)data, 1);
		break;
	case VMM_IOC_SHAREMEM:
		ret = vm_share_mem((struct vm_sharemem_params *)data, p);
		break;
	default:
		ret = vmmioctl_machdep(dev, cmd, data, flag, p);
		break;
	}

	refcnt_rele_wake(&vmm_softc->sc_refcnt);
out:
	KERNEL_LOCK();

	return (ret);
}

/*
 * pledge_ioctl_vmm
 *
 * Restrict the allowed ioctls in a pledged process context.
 * Is called from pledge_ioctl().
 */
int
pledge_ioctl_vmm(struct proc *p, long com)
{
	switch (com) {
	case VMM_IOC_CREATE:
	case VMM_IOC_INFO:
	case VMM_IOC_SHAREMEM:
		/* The "parent" process in vmd forks and manages VMs */
		if (p->p_pledge & PLEDGE_PROC)
			return (0);
		break;
	case VMM_IOC_TERM:
		/* XXX VM processes should only terminate themselves */
	case VMM_IOC_RUN:
	case VMM_IOC_RESETCPU:
	case VMM_IOC_READREGS:
	case VMM_IOC_WRITEREGS:
	case VMM_IOC_READVMPARAMS:
	case VMM_IOC_WRITEVMPARAMS:
		return (0);
	default:
		return pledge_ioctl_vmm_machdep(p, com);
	}

	return (EPERM);
}

/*
 * vm_find_vcpu
 *
 * Lookup VMM VCPU by ID number
 *
 * Parameters:
 *  vm: vm structure
 *  id: index id of vcpu
 *
 * Returns pointer to vcpu structure if successful, NULL otherwise
 */
struct vcpu *
vm_find_vcpu(struct vm *vm, uint32_t id)
{
	struct vcpu *vcpu;

	if (vm == NULL)
		return (NULL);

	SLIST_FOREACH(vcpu, &vm->vm_vcpu_list, vc_vcpu_link) {
		if (vcpu->vc_id == id)
			return (vcpu);
	}

	return (NULL);
}

/*
 * vm_create
 *
 * Creates the in-memory VMM structures for the VM defined by 'vcp'. The
 * parent of this VM shall be the process defined by 'p'.
 * This function does not start the VCPU(s) - see vm_start.
 *
 * Return Values:
 *  0: the create operation was successful
 *  ENOMEM: out of memory
 *  various other errors from vcpu_init/vm_impl_init
 */
int
vm_create(struct vm_create_params *vcp, struct proc *p)
{
	int i, ret;
	size_t memsize;
	struct vm *vm;
	struct vcpu *vcpu;
	struct uvm_object *uao;
	struct vm_mem_range *vmr;
	unsigned int uvmflags = 0;

	memsize = vm_create_check_mem_ranges(vcp);
	if (memsize == 0)
		return (EINVAL);

	/* XXX - support UP only (for now) */
	if (vcp->vcp_ncpus != 1)
		return (EINVAL);

	/* Bail early if we're already at vcpu capacity. */
	rw_enter_read(&vmm_softc->vm_lock);
	if (vmm_softc->vcpu_ct + vcp->vcp_ncpus > vmm_softc->vcpu_max) {
		DPRINTF("%s: maximum vcpus (%lu) reached\n", __func__,
		    vmm_softc->vcpu_max);
		rw_exit_read(&vmm_softc->vm_lock);
		return (ENOMEM);
	}
	rw_exit_read(&vmm_softc->vm_lock);

	/* Instantiate and configure the new vm. */
	vm = pool_get(&vm_pool, PR_WAITOK | PR_ZERO);

	/* Create the VM's identity. */
	vm->vm_creator_pid = p->p_p->ps_pid;
	strncpy(vm->vm_name, vcp->vcp_name, VMM_MAX_NAME_LEN - 1);

	/* Create the pmap for nested paging. */
	vm->vm_pmap = pmap_create();

	/* Initialize memory slots. */
	vm->vm_nmemranges = vcp->vcp_nmemranges;
	memcpy(vm->vm_memranges, vcp->vcp_memranges,
	    vm->vm_nmemranges * sizeof(vm->vm_memranges[0]));
	vm->vm_memory_size = memsize; /* Calculated above. */

	uvmflags = UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
	    MAP_INHERIT_NONE, MADV_NORMAL, UVM_FLAG_CONCEAL);
	for (i = 0; i < vm->vm_nmemranges; i++) {
		vmr = &vm->vm_memranges[i];
		if (vmr->vmr_type == VM_MEM_MMIO)
			continue;

		uao = NULL;
		uao = uao_create(vmr->vmr_size, UAO_FLAG_CANFAIL);
		if (uao == NULL) {
			printf("%s: failed to initialize memory slot\n",
			    __func__);
			vm_teardown(&vm);
			return (ENOMEM);
		}

		/* Map the UVM aobj into the process. It owns this reference. */
		ret = uvm_map(&p->p_vmspace->vm_map, &vmr->vmr_va,
		    vmr->vmr_size, uao, 0, 0, uvmflags);
		if (ret) {
			printf("%s: uvm_map failed: %d\n", __func__, ret);
			uao_detach(uao);
			vm_teardown(&vm);
			return (ENOMEM);
		}

		/* Make this mapping immutable so userland cannot change it. */
		ret = uvm_map_immutable(&p->p_vmspace->vm_map, vmr->vmr_va,
		    vmr->vmr_va + vmr->vmr_size, 1);
		if (ret) {
			printf("%s: uvm_map_immutable failed: %d\n", __func__,
			    ret);
			uvm_unmap(&p->p_vmspace->vm_map, vmr->vmr_va,
			    vmr->vmr_va + vmr->vmr_size);
			vm_teardown(&vm);
			return (ret);
		}

		uao_reference(uao);	/* Take a reference for vmm. */
		vm->vm_memory_slot[i] = uao;
	}

	if (vm_impl_init(vm, p)) {
		printf("failed to init arch-specific features for vm %p\n", vm);
		vm_teardown(&vm);
		return (ENOMEM);
	}

	vm->vm_vcpu_ct = 0;

	/* Initialize each VCPU defined in 'vcp' */
	SLIST_INIT(&vm->vm_vcpu_list);
	for (i = 0; i < vcp->vcp_ncpus; i++) {
		vcpu = pool_get(&vcpu_pool, PR_WAITOK | PR_ZERO);

		vcpu->vc_parent = vm;
		vcpu->vc_id = vm->vm_vcpu_ct;
		vm->vm_vcpu_ct++;
		if ((ret = vcpu_init(vcpu, vcp)) != 0) {
			printf("failed to init vcpu %d for vm %p\n", i, vm);
			vm_teardown(&vm);
			return (ret);
		}
		/* Publish vcpu to list, inheriting the reference. */
		SLIST_INSERT_HEAD(&vm->vm_vcpu_list, vcpu, vc_vcpu_link);
	}

	/* Attempt to register the vm now that it's configured. */
	rw_enter_write(&vmm_softc->vm_lock);

	if (vmm_softc->vcpu_ct + vm->vm_vcpu_ct > vmm_softc->vcpu_max) {
		/* Someone already took our capacity. */
		printf("%s: maximum vcpus (%lu) reached\n", __func__,
		    vmm_softc->vcpu_max);
		rw_exit_write(&vmm_softc->vm_lock);
		vm_teardown(&vm);
		return (ENOMEM);
	}

	/* Update the global index and identify the vm. */
	vmm_softc->vm_idx++;
	vm->vm_id = vmm_softc->vm_idx;
	vcp->vcp_id = vm->vm_id;

	/* Publish the vm into the list and update counts. */
	refcnt_init(&vm->vm_refcnt);
	SLIST_INSERT_HEAD(&vmm_softc->vm_list, vm, vm_link);
	vmm_softc->vm_ct++;
	vmm_softc->vcpu_ct += vm->vm_vcpu_ct;

	/* Update the userland process's view of guest memory. */
	memcpy(vcp->vcp_memranges, vm->vm_memranges,
	    vcp->vcp_nmemranges * sizeof(vcp->vcp_memranges[0]));

	rw_exit_write(&vmm_softc->vm_lock);

	return (0);
}

/*
 * vm_create_check_mem_ranges
 *
 * Make sure that the guest physical memory ranges given by the user process
 * do not overlap and are in ascending order.
 *
 * The last physical address may not exceed VMM_MAX_VM_MEM_SIZE.
 *
 * Return Values:
 *   The total memory size in bytes if the checks were successful
 *   0: One of the memory ranges was invalid or VMM_MAX_VM_MEM_SIZE was
 *   exceeded
 */
size_t
vm_create_check_mem_ranges(struct vm_create_params *vcp)
{
	size_t i, memsize = 0;
	struct vm_mem_range *vmr, *pvmr;
	const paddr_t maxgpa = VMM_MAX_VM_MEM_SIZE;

	if (vcp->vcp_nmemranges == 0 ||
	    vcp->vcp_nmemranges > VMM_MAX_MEM_RANGES) {
		DPRINTF("invalid number of guest memory ranges\n");
		return (0);
	}

	for (i = 0; i < vcp->vcp_nmemranges; i++) {
		vmr = &vcp->vcp_memranges[i];

		/* Only page-aligned addresses and sizes are permitted */
		if ((vmr->vmr_gpa & PAGE_MASK) || (vmr->vmr_va & PAGE_MASK) ||
		    (vmr->vmr_size & PAGE_MASK) || vmr->vmr_size == 0) {
			DPRINTF("memory range %zu is not page aligned\n", i);
			return (0);
		}

		/* Make sure that VMM_MAX_VM_MEM_SIZE is not exceeded */
		if (vmr->vmr_gpa >= maxgpa ||
		    vmr->vmr_size > maxgpa - vmr->vmr_gpa) {
			DPRINTF("exceeded max memory size\n");
			return (0);
		}

		/*
		 * Make sure that guest physical memory ranges do not overlap
		 * and that they are ascending.
		 */
		if (i > 0 && pvmr->vmr_gpa + pvmr->vmr_size > vmr->vmr_gpa) {
			DPRINTF("guest range %zu overlaps or !ascending\n", i);
			return (0);
		}

		/*
		 * No memory is mappable in MMIO ranges, so don't count towards
		 * the total guest memory size.
		 */
		if (vmr->vmr_type != VM_MEM_MMIO)
			memsize += vmr->vmr_size;
		pvmr = vmr;
	}

	return (memsize);
}

/*
 * vm_teardown
 *
 * Tears down (destroys) the vm indicated by 'vm'.
 *
 * Assumes the vm is already removed from the global vm list (or was never
 * added).
 *
 * Parameters:
 *  vm: vm to be torn down
 */
void
vm_teardown(struct vm **target)
{
	size_t i, nvcpu = 0;
	vaddr_t sva, eva;
	struct vcpu *vcpu, *tmp;
	struct vm *vm = *target;
	struct uvm_object *uao;

	KERNEL_ASSERT_UNLOCKED();

	/* Free VCPUs */
	SLIST_FOREACH_SAFE(vcpu, &vm->vm_vcpu_list, vc_vcpu_link, tmp) {
		SLIST_REMOVE(&vm->vm_vcpu_list, vcpu, vcpu, vc_vcpu_link);
		vcpu_deinit(vcpu);
		pool_put(&vcpu_pool, vcpu);
		nvcpu++;
	}

	/* Remove guest mappings from our nested page tables. */
	for (i = 0; i < vm->vm_nmemranges; i++) {
		sva = vm->vm_memranges[i].vmr_gpa;
		eva = sva + vm->vm_memranges[i].vmr_size - 1;
		pmap_remove(vm->vm_pmap, sva, eva);
	}

	/* Release UVM anon objects backing our guest memory. */
	for (i = 0; i < vm->vm_nmemranges; i++) {
		uao = vm->vm_memory_slot[i];
		vm->vm_memory_slot[i] = NULL;
		if (uao != NULL)
			uao_detach(uao);
	}

	/* At this point, no UVM-managed pages should reference our pmap. */
	pmap_destroy(vm->vm_pmap);
	vm->vm_pmap = NULL;

	pool_put(&vm_pool, vm);
	*target = NULL;
}

/*
 * vm_get_info
 *
 * Returns information about the VM indicated by 'vip'. The 'vip_size' field
 * in the 'vip' parameter is used to indicate the size of the caller's buffer.
 * If insufficient space exists in that buffer, the required size needed is
 * returned in vip_size and the number of VM information structures returned
 * in vip_info_count is set to 0. The caller should then try the ioctl again
 * after allocating a sufficiently large buffer.
 *
 * Parameters:
 *  vip: information structure identifying the VM to query
 *
 * Return values:
 *  0: the operation succeeded
 *  ENOMEM: memory allocation error during processing
 *  EFAULT: error copying data to user process
 */
int
vm_get_info(struct vm_info_params *vip)
{
	struct vm_info_result *out;
	struct vm *vm;
	struct vcpu *vcpu;
	int i = 0, j;
	size_t need, vm_ct;

	rw_enter_read(&vmm_softc->vm_lock);
	vm_ct = vmm_softc->vm_ct;
	rw_exit_read(&vmm_softc->vm_lock);

	need = vm_ct * sizeof(struct vm_info_result);
	if (vip->vip_size < need) {
		vip->vip_info_ct = 0;
		vip->vip_size = need;
		return (0);
	}

	out = malloc(need, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (out == NULL) {
		vip->vip_info_ct = 0;
		return (ENOMEM);
	}

	vip->vip_info_ct = vm_ct;

	rw_enter_read(&vmm_softc->vm_lock);
	SLIST_FOREACH(vm, &vmm_softc->vm_list, vm_link) {
		refcnt_take(&vm->vm_refcnt);

		out[i].vir_memory_size = vm->vm_memory_size;
		out[i].vir_used_size =
		    pmap_resident_count(vm->vm_pmap) * PAGE_SIZE;
		out[i].vir_ncpus = vm->vm_vcpu_ct;
		out[i].vir_id = vm->vm_id;
		out[i].vir_creator_pid = vm->vm_creator_pid;
		strlcpy(out[i].vir_name, vm->vm_name, VMM_MAX_NAME_LEN);

		for (j = 0; j < vm->vm_vcpu_ct; j++) {
			out[i].vir_vcpu_state[j] = VCPU_STATE_UNKNOWN;
			SLIST_FOREACH(vcpu, &vm->vm_vcpu_list,
			    vc_vcpu_link) {
				if (vcpu->vc_id == j)
					out[i].vir_vcpu_state[j] =
					    vcpu->vc_state;
			}
		}

		refcnt_rele_wake(&vm->vm_refcnt);
		i++;
		if (i == vm_ct)
			break;	/* Truncate to keep within bounds of 'out'. */
	}
	rw_exit_read(&vmm_softc->vm_lock);

	if (copyout(out, vip->vip_info, need) == EFAULT) {
		free(out, M_DEVBUF, need);
		return (EFAULT);
	}

	free(out, M_DEVBUF, need);
	return (0);
}

/*
 * vm_terminate
 *
 * Terminates the VM indicated by 'vtp'.
 *
 * Parameters:
 *  vtp: structure defining the VM to terminate
 *
 * Return values:
 *  0: the VM was terminated
 *  !0: the VM could not be located
 */
int
vm_terminate(struct vm_terminate_params *vtp)
{
	struct vm *vm;
	int error, nvcpu, vm_id;

	/*
	 * Find desired VM
	 */
	error = vm_find(vtp->vtp_vm_id, &vm);
	if (error)
		return (error);

	/* Pop the vm out of the global vm list. */
	rw_enter_write(&vmm_softc->vm_lock);
	SLIST_REMOVE(&vmm_softc->vm_list, vm, vm, vm_link);
	rw_exit_write(&vmm_softc->vm_lock);

	/* Drop the vm_list's reference to the vm. */
	if (refcnt_rele(&vm->vm_refcnt))
		panic("%s: vm %d(%p) vm_list refcnt drop was the last",
		    __func__, vm->vm_id, vm);

	/* Wait for our reference (taken from vm_find) is the last active. */
	refcnt_finalize(&vm->vm_refcnt, __func__);

	vm_id = vm->vm_id;
	nvcpu = vm->vm_vcpu_ct;

	vm_teardown(&vm);

	if (vm_id > 0) {
		rw_enter_write(&vmm_softc->vm_lock);
		vmm_softc->vm_ct--;
		vmm_softc->vcpu_ct -= nvcpu;
		if (vmm_softc->vm_ct < 1)
			vmm_stop();
		rw_exit_write(&vmm_softc->vm_lock);
	}

	return (0);
}

/*
 * vm_resetcpu
 *
 * Resets the vcpu defined in 'vrp' to power-on-init register state
 *
 * Parameters:
 *  vrp: ioctl structure defining the vcpu to reset (see vmmvar.h)
 *
 * Returns 0 if successful, or various error codes on failure:
 *  ENOENT if the VM id contained in 'vrp' refers to an unknown VM or
 *      if vrp describes an unknown vcpu for this VM
 *  EBUSY if the indicated VCPU is not stopped
 *  EIO if the indicated VCPU failed to reset
 */
int
vm_resetcpu(struct vm_resetcpu_params *vrp)
{
	struct vm *vm;
	struct vcpu *vcpu;
	int error, ret = 0;

	/* Find the desired VM */
	error = vm_find(vrp->vrp_vm_id, &vm);

	/* Not found? exit. */
	if (error != 0) {
		DPRINTF("%s: vm id %u not found\n", __func__,
		    vrp->vrp_vm_id);
		return (error);
	}

	vcpu = vm_find_vcpu(vm, vrp->vrp_vcpu_id);

	if (vcpu == NULL) {
		DPRINTF("%s: vcpu id %u of vm %u not found\n", __func__,
		    vrp->vrp_vcpu_id, vrp->vrp_vm_id);
		ret = ENOENT;
		goto out;
	}

	rw_enter_write(&vcpu->vc_lock);
	if (vcpu->vc_state != VCPU_STATE_STOPPED)
		ret = EBUSY;
	else {
		if (vcpu_reset_regs(vcpu, &vrp->vrp_init_state)) {
			printf("%s: failed\n", __func__);
#ifdef VMM_DEBUG
			dump_vcpu(vcpu);
#endif /* VMM_DEBUG */
			ret = EIO;
		}
	}
	rw_exit_write(&vcpu->vc_lock);
out:
	refcnt_rele_wake(&vm->vm_refcnt);

	return (ret);
}

/*
 * vcpu_must_stop
 *
 * Check if we need to (temporarily) stop running the VCPU for some reason,
 * such as:
 * - the VM was requested to terminate
 * - the proc running this VCPU has pending signals
 *
 * Parameters:
 *  vcpu: the VCPU to check
 *
 * Return values:
 *  1: the VM owning this VCPU should stop
 *  0: no stop is needed
 */
int
vcpu_must_stop(struct vcpu *vcpu)
{
	struct proc *p = curproc;

	if (vcpu->vc_state == VCPU_STATE_REQTERM)
		return (1);
	if (SIGPENDING(p) != 0)
		return (1);
	return (0);
}

/*
 * vm_share_mem
 *
 * Share a uvm mapping for the vm guest memory ranges into the calling process.
 *
 * Return values:
 *  0: if successful
 *  ENOENT: if the vm cannot be found by vm_find
 *  other errno on uvm_map or uvm_map_immutable failures
 */
int
vm_share_mem(struct vm_sharemem_params *vsp, struct proc *p)
{
	int ret = EINVAL, unmap = 0;
	size_t i, failed_uao = 0, n;
	struct vm *vm;
	struct vm_mem_range *src, *dst;
	struct uvm_object *uao;
	unsigned int uvmflags;

	ret = vm_find(vsp->vsp_vm_id, &vm);
	if (ret)
		return (ret);

	/* Check we have the expected number of ranges. */
	if (vm->vm_nmemranges != vsp->vsp_nmemranges)
		goto out;
	n = vm->vm_nmemranges;

	/* Check their types, sizes, and gpa's (implying page alignment). */
	for (i = 0; i < n; i++) {
		src = &vm->vm_memranges[i];
		dst = &vsp->vsp_memranges[i];

		/*
		 * The vm memranges were already checked during creation, so
		 * compare to them to confirm validity of mapping request.
		 */
		if (src->vmr_type != dst->vmr_type)
			goto out;
		if (src->vmr_gpa != dst->vmr_gpa)
			goto out;
		if (src->vmr_size != dst->vmr_size)
			goto out;

		/* The virtual addresses will be chosen by uvm_map(). */
		if (vsp->vsp_va[i] != 0)
			goto out;
	}

	/* Share each UVM aobj with the calling process. */
	uvmflags = UVM_MAPFLAG(PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE,
	    MAP_INHERIT_NONE, MADV_NORMAL, UVM_FLAG_CONCEAL);
	for (i = 0; i < n; i++) {
		dst = &vsp->vsp_memranges[i];
		if (dst->vmr_type == VM_MEM_MMIO)
			continue;

		uao = vm->vm_memory_slot[i];
		KASSERT(uao != NULL);

		ret = uvm_map(&p->p_p->ps_vmspace->vm_map, &vsp->vsp_va[i],
		    dst->vmr_size, uao, 0, 0, uvmflags);
		if (ret) {
			printf("%s: uvm_map failed: %d\n", __func__, ret);
			unmap = (i > 0) ? 1 : 0;
			failed_uao = i;
			goto out;
		}
		uao_reference(uao);	/* Add a reference for the process. */

		ret = uvm_map_immutable(&p->p_p->ps_vmspace->vm_map,
		    vsp->vsp_va[i], vsp->vsp_va[i] + dst->vmr_size, 1);
		if (ret) {
			printf("%s: uvm_map_immutable failed: %d\n",
			    __func__, ret);
			unmap = 1;
			failed_uao = i + 1;
			goto out;
		}
	}
	ret = 0;
out:
	if (unmap) {
		/* Unmap mapped aobjs, which drops the process's reference. */
		for (i = 0; i < failed_uao; i++) {
			dst = &vsp->vsp_memranges[i];
			uvm_unmap(&p->p_p->ps_vmspace->vm_map,
			    vsp->vsp_va[i], vsp->vsp_va[i] + dst->vmr_size);
		}
	}
	refcnt_rele_wake(&vm->vm_refcnt);
	return (ret);
}
