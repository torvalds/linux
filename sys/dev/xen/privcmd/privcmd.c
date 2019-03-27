/*
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/rman.h>
#include <sys/tree.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/bitset.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>

#include <machine/md_var.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/privcmd.h>
#include <xen/error.h>

MALLOC_DEFINE(M_PRIVCMD, "privcmd_dev", "Xen privcmd user-space device");

struct privcmd_map {
	vm_object_t mem;
	vm_size_t size;
	struct resource *pseudo_phys_res;
	int pseudo_phys_res_id;
	vm_paddr_t phys_base_addr;
	boolean_t mapped;
	BITSET_DEFINE_VAR() *err;
};

static d_ioctl_t     privcmd_ioctl;
static d_mmap_single_t	privcmd_mmap_single;

static struct cdevsw privcmd_devsw = {
	.d_version = D_VERSION,
	.d_ioctl = privcmd_ioctl,
	.d_mmap_single = privcmd_mmap_single,
	.d_name = "privcmd",
};

static int privcmd_pg_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color);
static void privcmd_pg_dtor(void *handle);
static int privcmd_pg_fault(vm_object_t object, vm_ooffset_t offset,
    int prot, vm_page_t *mres);

static struct cdev_pager_ops privcmd_pg_ops = {
	.cdev_pg_fault = privcmd_pg_fault,
	.cdev_pg_ctor =	privcmd_pg_ctor,
	.cdev_pg_dtor =	privcmd_pg_dtor,
};

static device_t privcmd_dev = NULL;

/*------------------------- Privcmd Pager functions --------------------------*/
static int
privcmd_pg_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{

	return (0);
}

static void
privcmd_pg_dtor(void *handle)
{
	struct xen_remove_from_physmap rm = { .domid = DOMID_SELF };
	struct privcmd_map *map = handle;
	int error;
	vm_size_t i;
	vm_page_t m;

	/*
	 * Remove the mappings from the used pages. This will remove the
	 * underlying p2m bindings in Xen second stage translation.
	 */
	if (map->mapped == true) {
		VM_OBJECT_WLOCK(map->mem);
retry:
		for (i = 0; i < map->size; i++) {
			m = vm_page_lookup(map->mem, i);
			if (m == NULL)
				continue;
			if (vm_page_sleep_if_busy(m, "pcmdum"))
				goto retry;
			cdev_pager_free_page(map->mem, m);
		}
		VM_OBJECT_WUNLOCK(map->mem);

		for (i = 0; i < map->size; i++) {
			rm.gpfn = atop(map->phys_base_addr) + i;
			HYPERVISOR_memory_op(XENMEM_remove_from_physmap, &rm);
		}
		free(map->err, M_PRIVCMD);
	}

	error = xenmem_free(privcmd_dev, map->pseudo_phys_res_id,
	    map->pseudo_phys_res);
	KASSERT(error == 0, ("Unable to release memory resource: %d", error));

	free(map, M_PRIVCMD);
}

static int
privcmd_pg_fault(vm_object_t object, vm_ooffset_t offset,
    int prot, vm_page_t *mres)
{
	struct privcmd_map *map = object->handle;
	vm_pindex_t pidx;
	vm_page_t page, oldm;

	if (map->mapped != true)
		return (VM_PAGER_FAIL);

	pidx = OFF_TO_IDX(offset);
	if (pidx >= map->size || BIT_ISSET(map->size, pidx, map->err))
		return (VM_PAGER_FAIL);

	page = PHYS_TO_VM_PAGE(map->phys_base_addr + offset);
	if (page == NULL)
		return (VM_PAGER_FAIL);

	KASSERT((page->flags & PG_FICTITIOUS) != 0,
	    ("not fictitious %p", page));
	KASSERT(page->wire_count == 1, ("wire_count not 1 %p", page));
	KASSERT(vm_page_busied(page) == 0, ("page %p is busy", page));

	if (*mres != NULL) {
		oldm = *mres;
		vm_page_lock(oldm);
		vm_page_free(oldm);
		vm_page_unlock(oldm);
		*mres = NULL;
	}

	vm_page_insert(page, object, pidx);
	page->valid = VM_PAGE_BITS_ALL;
	vm_page_xbusy(page);
	*mres = page;
	return (VM_PAGER_OK);
}

/*----------------------- Privcmd char device methods ------------------------*/
static int
privcmd_mmap_single(struct cdev *cdev, vm_ooffset_t *offset, vm_size_t size,
    vm_object_t *object, int nprot)
{
	struct privcmd_map *map;

	map = malloc(sizeof(*map), M_PRIVCMD, M_WAITOK | M_ZERO);

	map->size = OFF_TO_IDX(size);
	map->pseudo_phys_res_id = 0;

	map->pseudo_phys_res = xenmem_alloc(privcmd_dev,
	    &map->pseudo_phys_res_id, size);
	if (map->pseudo_phys_res == NULL) {
		free(map, M_PRIVCMD);
		return (ENOMEM);
	}

	map->phys_base_addr = rman_get_start(map->pseudo_phys_res);
	map->mem = cdev_pager_allocate(map, OBJT_MGTDEVICE, &privcmd_pg_ops,
	    size, nprot, *offset, NULL);
	if (map->mem == NULL) {
		xenmem_free(privcmd_dev, map->pseudo_phys_res_id,
		    map->pseudo_phys_res);
		free(map, M_PRIVCMD);
		return (ENOMEM);
	}

	*object = map->mem;

	return (0);
}

static int
privcmd_ioctl(struct cdev *dev, unsigned long cmd, caddr_t arg,
	      int mode, struct thread *td)
{
	int error, i;

	switch (cmd) {
	case IOCTL_PRIVCMD_HYPERCALL: {
		struct ioctl_privcmd_hypercall *hcall;

		hcall = (struct ioctl_privcmd_hypercall *)arg;
#ifdef __amd64__
		/*
		 * The hypervisor page table walker will refuse to access
		 * user-space pages if SMAP is enabled, so temporary disable it
		 * while performing the hypercall.
		 */
		if (cpu_stdext_feature & CPUID_STDEXT_SMAP)
			stac();
#endif
		error = privcmd_hypercall(hcall->op, hcall->arg[0],
		    hcall->arg[1], hcall->arg[2], hcall->arg[3], hcall->arg[4]);
#ifdef __amd64__
		if (cpu_stdext_feature & CPUID_STDEXT_SMAP)
			clac();
#endif
		if (error >= 0) {
			hcall->retval = error;
			error = 0;
		} else {
			error = xen_translate_error(error);
			hcall->retval = 0;
		}
		break;
	}
	case IOCTL_PRIVCMD_MMAPBATCH: {
		struct ioctl_privcmd_mmapbatch *mmap;
		vm_map_t map;
		vm_map_entry_t entry;
		vm_object_t mem;
		vm_pindex_t pindex;
		vm_prot_t prot;
		boolean_t wired;
		struct xen_add_to_physmap_range add;
		xen_ulong_t *idxs;
		xen_pfn_t *gpfns;
		int *errs, index;
		struct privcmd_map *umap;
		uint16_t num;

		mmap = (struct ioctl_privcmd_mmapbatch *)arg;

		if ((mmap->num == 0) ||
		    ((mmap->addr & PAGE_MASK) != 0)) {
			error = EINVAL;
			break;
		}

		map = &td->td_proc->p_vmspace->vm_map;
		error = vm_map_lookup(&map, mmap->addr, VM_PROT_NONE, &entry,
		    &mem, &pindex, &prot, &wired);
		if (error != KERN_SUCCESS) {
			error = EINVAL;
			break;
		}
		if ((entry->start != mmap->addr) ||
		    (entry->end != mmap->addr + (mmap->num * PAGE_SIZE))) {
			vm_map_lookup_done(map, entry);
			error = EINVAL;
			break;
		}
		vm_map_lookup_done(map, entry);
		if ((mem->type != OBJT_MGTDEVICE) ||
		    (mem->un_pager.devp.ops != &privcmd_pg_ops)) {
			error = EINVAL;
			break;
		}
		umap = mem->handle;

		add.domid = DOMID_SELF;
		add.space = XENMAPSPACE_gmfn_foreign;
		add.foreign_domid = mmap->dom;

		/*
		 * The 'size' field in the xen_add_to_physmap_range only
		 * allows for UINT16_MAX mappings in a single hypercall.
		 */
		num = MIN(mmap->num, UINT16_MAX);

		idxs = malloc(sizeof(*idxs) * num, M_PRIVCMD, M_WAITOK);
		gpfns = malloc(sizeof(*gpfns) * num, M_PRIVCMD, M_WAITOK);
		errs = malloc(sizeof(*errs) * num, M_PRIVCMD, M_WAITOK);

		set_xen_guest_handle(add.idxs, idxs);
		set_xen_guest_handle(add.gpfns, gpfns);
		set_xen_guest_handle(add.errs, errs);

		/* Allocate a bitset to store broken page mappings. */
		umap->err = BITSET_ALLOC(mmap->num, M_PRIVCMD,
		    M_WAITOK | M_ZERO);

		for (index = 0; index < mmap->num; index += num) {
			num = MIN(mmap->num - index, UINT16_MAX);
			add.size = num;

			error = copyin(&mmap->arr[index], idxs,
			    sizeof(idxs[0]) * num);
			if (error != 0)
				goto mmap_out;

			for (i = 0; i < num; i++)
				gpfns[i] = atop(umap->phys_base_addr +
				    (i + index) * PAGE_SIZE);

			bzero(errs, sizeof(*errs) * num);

			error = HYPERVISOR_memory_op(
			    XENMEM_add_to_physmap_range, &add);
			if (error != 0) {
				error = xen_translate_error(error);
				goto mmap_out;
			}

			for (i = 0; i < num; i++) {
				if (errs[i] != 0) {
					errs[i] = xen_translate_error(errs[i]);

					/* Mark the page as invalid. */
					BIT_SET(mmap->num, index + i,
					    umap->err);
				}
			}

			error = copyout(errs, &mmap->err[index],
			    sizeof(errs[0]) * num);
			if (error != 0)
				goto mmap_out;
		}

		umap->mapped = true;

mmap_out:
		free(idxs, M_PRIVCMD);
		free(gpfns, M_PRIVCMD);
		free(errs, M_PRIVCMD);
		if (!umap->mapped)
			free(umap->err, M_PRIVCMD);

		break;
	}

	default:
		error = ENOSYS;
		break;
	}

	return (error);
}

/*------------------ Private Device Attachment Functions  --------------------*/
static void
privcmd_identify(driver_t *driver, device_t parent)
{

	KASSERT(xen_domain(),
	    ("Trying to attach privcmd device on non Xen domain"));

	if (BUS_ADD_CHILD(parent, 0, "privcmd", 0) == NULL)
		panic("unable to attach privcmd user-space device");
}

static int
privcmd_probe(device_t dev)
{

	privcmd_dev = dev;
	device_set_desc(dev, "Xen privileged interface user-space device");
	return (BUS_PROBE_NOWILDCARD);
}

static int
privcmd_attach(device_t dev)
{

	make_dev_credf(MAKEDEV_ETERNAL, &privcmd_devsw, 0, NULL, UID_ROOT,
	    GID_WHEEL, 0600, "xen/privcmd");
	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t privcmd_methods[] = {
	DEVMETHOD(device_identify,	privcmd_identify),
	DEVMETHOD(device_probe,		privcmd_probe),
	DEVMETHOD(device_attach,	privcmd_attach),

	DEVMETHOD_END
};

static driver_t privcmd_driver = {
	"privcmd",
	privcmd_methods,
	0,
};

devclass_t privcmd_devclass;

DRIVER_MODULE(privcmd, xenpv, privcmd_driver, privcmd_devclass, 0, 0);
MODULE_DEPEND(privcmd, xenpv, 1, 1, 1);
