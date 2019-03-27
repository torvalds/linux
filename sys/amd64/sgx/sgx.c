/*-
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

/*
 * Design overview.
 *
 * The driver provides character device for mmap(2) and ioctl(2) system calls
 * allowing user to manage isolated compartments ("enclaves") in user VA space.
 *
 * The driver duties is EPC pages management, enclave management, user data
 * validation.
 *
 * This driver requires Intel SGX support from hardware.
 *
 * /dev/sgx:
 *    .mmap:
 *        sgx_mmap_single() allocates VM object with following pager
 *        operations:
 *              a) sgx_pg_ctor():
 *                  VM object constructor does nothing
 *              b) sgx_pg_dtor():
 *                  VM object destructor destroys the SGX enclave associated
 *                  with the object: it frees all the EPC pages allocated for
 *                  enclave and removes the enclave.
 *              c) sgx_pg_fault():
 *                  VM object fault handler does nothing
 *
 *    .ioctl:
 *        sgx_ioctl():
 *               a) SGX_IOC_ENCLAVE_CREATE
 *                   Adds Enclave SECS page: initial step of enclave creation.
 *               b) SGX_IOC_ENCLAVE_ADD_PAGE
 *                   Adds TCS, REG pages to the enclave.
 *               c) SGX_IOC_ENCLAVE_INIT
 *                   Finalizes enclave creation.
 *
 * Enclave lifecycle:
 *          .-- ECREATE  -- Add SECS page
 *   Kernel |   EADD     -- Add TCS, REG pages
 *    space |   EEXTEND  -- Measure the page (take unique hash)
 *    ENCLS |   EPA      -- Allocate version array page
 *          '-- EINIT    -- Finalize enclave creation
 *   User   .-- EENTER   -- Go to entry point of enclave
 *    space |   EEXIT    -- Exit back to main application
 *    ENCLU '-- ERESUME  -- Resume enclave execution (e.g. after exception)
 *  
 * Enclave lifecycle from driver point of view:
 *  1) User calls mmap() on /dev/sgx: we allocate a VM object
 *  2) User calls ioctl SGX_IOC_ENCLAVE_CREATE: we look for the VM object
 *     associated with user process created on step 1, create SECS physical
 *     page and store it in enclave's VM object queue by special index
 *     SGX_SECS_VM_OBJECT_INDEX.
 *  3) User calls ioctl SGX_IOC_ENCLAVE_ADD_PAGE: we look for enclave created
 *     on step 2, create TCS or REG physical page and map it to specified by
 *     user address of enclave VM object.
 *  4) User finalizes enclave creation with ioctl SGX_IOC_ENCLAVE_INIT call.
 *  5) User can freely enter to and exit from enclave using ENCLU instructions
 *     from userspace: the driver does nothing here.
 *  6) User proceed munmap(2) system call (or the process with enclave dies):
 *     we destroy the enclave associated with the object.
 *
 * EPC page types and their indexes in VM object queue:
 *   - PT_SECS index is special and equals SGX_SECS_VM_OBJECT_INDEX (-1);
 *   - PT_TCS and PT_REG indexes are specified by user in addr field of ioctl
 *     request data and determined as follows:
 *       pidx = OFF_TO_IDX(addp->addr - vmh->base);
 *   - PT_VA index is special, created for PT_REG, PT_TCS and PT_SECS pages
 *     and determined by formula:
 *       va_page_idx = - SGX_VA_PAGES_OFFS - (page_idx / SGX_VA_PAGE_SLOTS);
 *     PT_VA page can hold versions of up to 512 pages, and slot for each
 *     page in PT_VA page is determined as follows:
 *       va_slot_idx = page_idx % SGX_VA_PAGE_SLOTS;
 *   - PT_TRIM is unused.
 *
 * Locking:
 *    SGX ENCLS set of instructions have limitations on concurrency:
 *    some instructions can't be executed same time on different CPUs.
 *    We use sc->mtx_encls lock around them to prevent concurrent execution.
 *    sc->mtx lock is used to manage list of created enclaves and the state of
 *    SGX driver.
 *
 * Eviction of EPC pages:
 *    Eviction support is not implemented in this driver, however the driver
 *    manages VA (version array) pages: it allocates a VA slot for each EPC
 *    page. This will be required for eviction support in future.
 *    VA pages and slots are currently unused.
 *
 * Intel® 64 and IA-32 Architectures Software Developer's Manual
 * https://software.intel.com/en-us/articles/intel-sdm
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/vmem.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_phys.h>
#include <vm/vm_radix.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#include <machine/specialreg.h>
#include <machine/cpufunc.h>
#include <machine/sgx.h>
#include <machine/sgxreg.h>

#include <amd64/sgx/sgxvar.h>

#define	SGX_DEBUG
#undef	SGX_DEBUG

#ifdef	SGX_DEBUG
#define	dprintf(fmt, ...)	printf(fmt, ##__VA_ARGS__)
#else
#define	dprintf(fmt, ...)
#endif

static struct cdev_pager_ops sgx_pg_ops;
struct sgx_softc sgx_sc;

static int
sgx_get_epc_page(struct sgx_softc *sc, struct epc_page **epc)
{
	vmem_addr_t addr;
	int i;

	if (vmem_alloc(sc->vmem_epc, PAGE_SIZE, M_FIRSTFIT | M_NOWAIT,
	    &addr) == 0) {
		i = (addr - sc->epc_base) / PAGE_SIZE;
		*epc = &sc->epc_pages[i];
		return (0);
	}

	return (ENOMEM);
}

static void
sgx_put_epc_page(struct sgx_softc *sc, struct epc_page *epc)
{
	vmem_addr_t addr;

	if (epc == NULL)
		return;

	addr = (epc->index * PAGE_SIZE) + sc->epc_base;
	vmem_free(sc->vmem_epc, addr, PAGE_SIZE);
}

static int
sgx_va_slot_init_by_index(struct sgx_softc *sc, vm_object_t object,
    uint64_t idx)
{
	struct epc_page *epc;
	vm_page_t page;
	vm_page_t p;
	int ret;

	VM_OBJECT_ASSERT_WLOCKED(object);

	p = vm_page_lookup(object, idx);
	if (p == NULL) {
		ret = sgx_get_epc_page(sc, &epc);
		if (ret) {
			dprintf("%s: No free EPC pages available.\n",
			    __func__);
			return (ret);
		}

		mtx_lock(&sc->mtx_encls);
		sgx_epa((void *)epc->base);
		mtx_unlock(&sc->mtx_encls);

		page = PHYS_TO_VM_PAGE(epc->phys);

		vm_page_insert(page, object, idx);
		page->valid = VM_PAGE_BITS_ALL;
	}

	return (0);
}

static int
sgx_va_slot_init(struct sgx_softc *sc,
    struct sgx_enclave *enclave,
    uint64_t addr)
{
	vm_pindex_t pidx;
	uint64_t va_page_idx;
	uint64_t idx;
	vm_object_t object;
	int va_slot;
	int ret;

	object = enclave->object;

	VM_OBJECT_ASSERT_WLOCKED(object);

	pidx = OFF_TO_IDX(addr);

	va_slot = pidx % SGX_VA_PAGE_SLOTS;
	va_page_idx = pidx / SGX_VA_PAGE_SLOTS;
	idx = - SGX_VA_PAGES_OFFS - va_page_idx;

	ret = sgx_va_slot_init_by_index(sc, object, idx);

	return (ret);
}

static int
sgx_mem_find(struct sgx_softc *sc, uint64_t addr,
    vm_map_entry_t *entry0, vm_object_t *object0)
{
	vm_map_t map;
	vm_map_entry_t entry;
	vm_object_t object;

	map = &curproc->p_vmspace->vm_map;

	vm_map_lock_read(map);
	if (!vm_map_lookup_entry(map, addr, &entry)) {
		vm_map_unlock_read(map);
		dprintf("%s: Can't find enclave.\n", __func__);
		return (EINVAL);
	}

	object = entry->object.vm_object;
	if (object == NULL || object->handle == NULL) {
		vm_map_unlock_read(map);
		return (EINVAL);
	}

	if (object->type != OBJT_MGTDEVICE ||
	    object->un_pager.devp.ops != &sgx_pg_ops) {
		vm_map_unlock_read(map);
		return (EINVAL);
	}

	vm_object_reference(object);

	*object0 = object;
	*entry0 = entry;
	vm_map_unlock_read(map);

	return (0);
}

static int
sgx_enclave_find(struct sgx_softc *sc, uint64_t addr,
    struct sgx_enclave **encl)
{
	struct sgx_vm_handle *vmh;
	struct sgx_enclave *enclave;
	vm_map_entry_t entry;
	vm_object_t object;
	int ret;

	ret = sgx_mem_find(sc, addr, &entry, &object);
	if (ret)
		return (ret);

	vmh = object->handle;
	if (vmh == NULL) {
		vm_object_deallocate(object);
		return (EINVAL);
	}

	enclave = vmh->enclave;
	if (enclave == NULL || enclave->object == NULL) {
		vm_object_deallocate(object);
		return (EINVAL);
	}

	*encl = enclave;

	return (0);
}

static int
sgx_enclave_alloc(struct sgx_softc *sc, struct secs *secs,
    struct sgx_enclave **enclave0)
{
	struct sgx_enclave *enclave;

	enclave = malloc(sizeof(struct sgx_enclave),
	    M_SGX, M_WAITOK | M_ZERO);

	enclave->base = secs->base;
	enclave->size = secs->size;

	*enclave0 = enclave;

	return (0);
}

static void
sgx_epc_page_remove(struct sgx_softc *sc,
    struct epc_page *epc)
{

	mtx_lock(&sc->mtx_encls);
	sgx_eremove((void *)epc->base);
	mtx_unlock(&sc->mtx_encls);
}

static void
sgx_page_remove(struct sgx_softc *sc, vm_page_t p)
{
	struct epc_page *epc;
	vm_paddr_t pa;
	uint64_t offs;

	vm_page_lock(p);
	vm_page_remove(p);
	vm_page_unlock(p);

	dprintf("%s: p->pidx %ld\n", __func__, p->pindex);

	pa = VM_PAGE_TO_PHYS(p);
	epc = &sc->epc_pages[0];
	offs = (pa - epc->phys) / PAGE_SIZE;
	epc = &sc->epc_pages[offs];

	sgx_epc_page_remove(sc, epc);
	sgx_put_epc_page(sc, epc);
}

static void
sgx_enclave_remove(struct sgx_softc *sc,
    struct sgx_enclave *enclave)
{
	vm_object_t object;
	vm_page_t p, p_secs, p_next;

	mtx_lock(&sc->mtx);
	TAILQ_REMOVE(&sc->enclaves, enclave, next);
	mtx_unlock(&sc->mtx);

	object = enclave->object;

	VM_OBJECT_WLOCK(object);

	/*
	 * First remove all the pages except SECS,
	 * then remove SECS page.
	 */
	p_secs = NULL;
	TAILQ_FOREACH_SAFE(p, &object->memq, listq, p_next) {
		if (p->pindex == SGX_SECS_VM_OBJECT_INDEX) {
			p_secs = p;
			continue;
		}
		sgx_page_remove(sc, p);
	}
	/* Now remove SECS page */
	if (p_secs != NULL)
		sgx_page_remove(sc, p_secs);

	KASSERT(TAILQ_EMPTY(&object->memq) == 1, ("not empty"));
	KASSERT(object->resident_page_count == 0, ("count"));

	VM_OBJECT_WUNLOCK(object);
}

static int
sgx_measure_page(struct sgx_softc *sc, struct epc_page *secs,
    struct epc_page *epc, uint16_t mrmask)
{
	int i, j;
	int ret;

	mtx_lock(&sc->mtx_encls);

	for (i = 0, j = 1; i < PAGE_SIZE; i += 0x100, j <<= 1) {
		if (!(j & mrmask))
			continue;

		ret = sgx_eextend((void *)secs->base,
		    (void *)(epc->base + i));
		if (ret == SGX_EFAULT) {
			mtx_unlock(&sc->mtx_encls);
			return (ret);
		}
	}

	mtx_unlock(&sc->mtx_encls);

	return (0);
}

static int
sgx_secs_validate(struct sgx_softc *sc, struct secs *secs)
{
	struct secs_attr *attr;
	int i;

	if (secs->size == 0)
		return (EINVAL);

	/* BASEADDR must be naturally aligned on an SECS.SIZE boundary. */
	if (secs->base & (secs->size - 1))
		return (EINVAL);

	/* SECS.SIZE must be at least 2 pages. */
	if (secs->size < 2 * PAGE_SIZE)
		return (EINVAL);

	if ((secs->size & (secs->size - 1)) != 0)
		return (EINVAL);

	attr = &secs->attributes;

	if (attr->reserved1 != 0 ||
	    attr->reserved2 != 0 ||
	    attr->reserved3 != 0)
		return (EINVAL);

	for (i = 0; i < SECS_ATTR_RSV4_SIZE; i++)
		if (attr->reserved4[i])
			return (EINVAL);

	/*
	 * Intel® Software Guard Extensions Programming Reference
	 * 6.7.2 Relevant Fields in Various Data Structures
	 * 6.7.2.1 SECS.ATTRIBUTES.XFRM
	 * XFRM[1:0] must be set to 0x3.
	 */
	if ((attr->xfrm & 0x3) != 0x3)
		return (EINVAL);

	if (!attr->mode64bit)
		return (EINVAL);

	if (secs->size > sc->enclave_size_max)
		return (EINVAL);

	for (i = 0; i < SECS_RSV1_SIZE; i++)
		if (secs->reserved1[i])
			return (EINVAL);

	for (i = 0; i < SECS_RSV2_SIZE; i++)
		if (secs->reserved2[i])
			return (EINVAL);

	for (i = 0; i < SECS_RSV3_SIZE; i++)
		if (secs->reserved3[i])
			return (EINVAL);

	for (i = 0; i < SECS_RSV4_SIZE; i++)
		if (secs->reserved4[i])
			return (EINVAL);

	return (0);
}

static int
sgx_tcs_validate(struct tcs *tcs)
{
	int i;

	if ((tcs->flags) ||
	    (tcs->ossa & (PAGE_SIZE - 1)) ||
	    (tcs->ofsbasgx & (PAGE_SIZE - 1)) ||
	    (tcs->ogsbasgx & (PAGE_SIZE - 1)) ||
	    ((tcs->fslimit & 0xfff) != 0xfff) ||
	    ((tcs->gslimit & 0xfff) != 0xfff))
		return (EINVAL);

	for (i = 0; i < nitems(tcs->reserved3); i++)
		if (tcs->reserved3[i])
			return (EINVAL);

	return (0);
}

static void
sgx_tcs_dump(struct sgx_softc *sc, struct tcs *t)
{

	dprintf("t->flags %lx\n", t->flags);
	dprintf("t->ossa %lx\n", t->ossa);
	dprintf("t->cssa %x\n", t->cssa);
	dprintf("t->nssa %x\n", t->nssa);
	dprintf("t->oentry %lx\n", t->oentry);
	dprintf("t->ofsbasgx %lx\n", t->ofsbasgx);
	dprintf("t->ogsbasgx %lx\n", t->ogsbasgx);
	dprintf("t->fslimit %x\n", t->fslimit);
	dprintf("t->gslimit %x\n", t->gslimit);
}

static int
sgx_pg_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{
	struct sgx_vm_handle *vmh;

	vmh = handle;
	if (vmh == NULL) {
		dprintf("%s: vmh not found.\n", __func__);
		return (0);
	}

	dprintf("%s: vmh->base %lx foff 0x%lx size 0x%lx\n",
	    __func__, vmh->base, foff, size);

	return (0);
}

static void
sgx_pg_dtor(void *handle)
{
	struct sgx_vm_handle *vmh;
	struct sgx_softc *sc;

	vmh = handle;
	if (vmh == NULL) {
		dprintf("%s: vmh not found.\n", __func__);
		return;
	}

	sc = vmh->sc;
	if (sc == NULL) {
		dprintf("%s: sc is NULL\n", __func__);
		return;
	}

	if (vmh->enclave == NULL) {
		dprintf("%s: Enclave not found.\n", __func__);
		return;
	}

	sgx_enclave_remove(sc, vmh->enclave);

	free(vmh->enclave, M_SGX);
	free(vmh, M_SGX);
}

static int
sgx_pg_fault(vm_object_t object, vm_ooffset_t offset,
    int prot, vm_page_t *mres)
{

	/*
	 * The purpose of this trivial handler is to handle the race
	 * when user tries to access mmaped region before or during
	 * enclave creation ioctl calls.
	 */

	dprintf("%s: offset 0x%lx\n", __func__, offset);

	return (VM_PAGER_FAIL);
}

static struct cdev_pager_ops sgx_pg_ops = {
	.cdev_pg_ctor = sgx_pg_ctor,
	.cdev_pg_dtor = sgx_pg_dtor,
	.cdev_pg_fault = sgx_pg_fault,
};


static void
sgx_insert_epc_page_by_index(vm_page_t page, vm_object_t object,
    vm_pindex_t pidx)
{

	VM_OBJECT_ASSERT_WLOCKED(object);

	vm_page_insert(page, object, pidx);
	page->valid = VM_PAGE_BITS_ALL;
}

static void
sgx_insert_epc_page(struct sgx_enclave *enclave,
    struct epc_page *epc, uint64_t addr)
{
	vm_pindex_t pidx;
	vm_page_t page;

	VM_OBJECT_ASSERT_WLOCKED(enclave->object);

	pidx = OFF_TO_IDX(addr);
	page = PHYS_TO_VM_PAGE(epc->phys);

	sgx_insert_epc_page_by_index(page, enclave->object, pidx);
}

static int
sgx_ioctl_create(struct sgx_softc *sc, struct sgx_enclave_create *param)
{
	struct sgx_vm_handle *vmh;
	vm_map_entry_t entry;
	vm_page_t p;
	struct page_info pginfo;
	struct secinfo secinfo;
	struct sgx_enclave *enclave;
	struct epc_page *epc;
	struct secs *secs;
	vm_object_t object;
	vm_page_t page;
	int ret;

	epc = NULL;
	secs = NULL;
	enclave = NULL;
	object = NULL;

	/* SGX Enclave Control Structure (SECS) */
	secs = malloc(PAGE_SIZE, M_SGX, M_WAITOK | M_ZERO);
	ret = copyin((void *)param->src, secs, sizeof(struct secs));
	if (ret) {
		dprintf("%s: Can't copy SECS.\n", __func__);
		goto error;
	}

	ret = sgx_secs_validate(sc, secs);
	if (ret) {
		dprintf("%s: SECS validation failed.\n", __func__);
		goto error;
	}

	ret = sgx_mem_find(sc, secs->base, &entry, &object);
	if (ret) {
		dprintf("%s: Can't find vm_map.\n", __func__);
		goto error;
	}

	vmh = object->handle;
	if (!vmh) {
		dprintf("%s: Can't find vmh.\n", __func__);
		ret = ENXIO;
		goto error;
	}

	dprintf("%s: entry start %lx offset %lx\n",
	    __func__, entry->start, entry->offset);
	vmh->base = (entry->start - entry->offset);

	ret = sgx_enclave_alloc(sc, secs, &enclave);
	if (ret) {
		dprintf("%s: Can't alloc enclave.\n", __func__);
		goto error;
	}
	enclave->object = object;
	enclave->vmh = vmh;

	memset(&secinfo, 0, sizeof(struct secinfo));
	memset(&pginfo, 0, sizeof(struct page_info));
	pginfo.linaddr = 0;
	pginfo.srcpge = (uint64_t)secs;
	pginfo.secinfo = &secinfo;
	pginfo.secs = 0;

	ret = sgx_get_epc_page(sc, &epc);
	if (ret) {
		dprintf("%s: Failed to get free epc page.\n", __func__);
		goto error;
	}
	enclave->secs_epc_page = epc;

	VM_OBJECT_WLOCK(object);
	p = vm_page_lookup(object, SGX_SECS_VM_OBJECT_INDEX);
	if (p) {
		VM_OBJECT_WUNLOCK(object);
		/* SECS page already added. */
		ret = ENXIO;
		goto error;
	}

	ret = sgx_va_slot_init_by_index(sc, object,
	    - SGX_VA_PAGES_OFFS - SGX_SECS_VM_OBJECT_INDEX);
	if (ret) {
		VM_OBJECT_WUNLOCK(object);
		dprintf("%s: Can't init va slot.\n", __func__);
		goto error;
	}

	mtx_lock(&sc->mtx);
	if ((sc->state & SGX_STATE_RUNNING) == 0) {
		mtx_unlock(&sc->mtx);
		/* Remove VA page that was just created for SECS page. */
		p = vm_page_lookup(enclave->object,
		    - SGX_VA_PAGES_OFFS - SGX_SECS_VM_OBJECT_INDEX);
		sgx_page_remove(sc, p);
		VM_OBJECT_WUNLOCK(object);
		goto error;
	}
	mtx_lock(&sc->mtx_encls);
	ret = sgx_ecreate(&pginfo, (void *)epc->base);
	mtx_unlock(&sc->mtx_encls);
	if (ret == SGX_EFAULT) {
		dprintf("%s: gp fault\n", __func__);
		mtx_unlock(&sc->mtx);
		/* Remove VA page that was just created for SECS page. */
		p = vm_page_lookup(enclave->object,
		    - SGX_VA_PAGES_OFFS - SGX_SECS_VM_OBJECT_INDEX);
		sgx_page_remove(sc, p);
		VM_OBJECT_WUNLOCK(object);
		goto error;
	}

	TAILQ_INSERT_TAIL(&sc->enclaves, enclave, next);
	mtx_unlock(&sc->mtx);

	vmh->enclave = enclave;

	page = PHYS_TO_VM_PAGE(epc->phys);
	sgx_insert_epc_page_by_index(page, enclave->object,
	    SGX_SECS_VM_OBJECT_INDEX);

	VM_OBJECT_WUNLOCK(object);

	/* Release the reference. */
	vm_object_deallocate(object);

	free(secs, M_SGX);

	return (0);

error:
	free(secs, M_SGX);
	sgx_put_epc_page(sc, epc);
	free(enclave, M_SGX);
	vm_object_deallocate(object);

	return (ret);
}

static int
sgx_ioctl_add_page(struct sgx_softc *sc,
    struct sgx_enclave_add_page *addp)
{
	struct epc_page *secs_epc_page;
	struct sgx_enclave *enclave;
	struct sgx_vm_handle *vmh;
	struct epc_page *epc;
	struct page_info pginfo;
	struct secinfo secinfo;
	vm_object_t object;
	void *tmp_vaddr;
	uint64_t page_type;
	struct tcs *t;
	uint64_t addr;
	uint64_t pidx;
	vm_page_t p;
	int ret;

	tmp_vaddr = NULL;
	epc = NULL;
	object = NULL;

	/* Find and get reference to VM object. */
	ret = sgx_enclave_find(sc, addp->addr, &enclave);
	if (ret) {
		dprintf("%s: Failed to find enclave.\n", __func__);
		goto error;
	}

	object = enclave->object;
	KASSERT(object != NULL, ("vm object is NULL\n"));
	vmh = object->handle;

	ret = sgx_get_epc_page(sc, &epc);
	if (ret) {
		dprintf("%s: Failed to get free epc page.\n", __func__);
		goto error;
	}

	memset(&secinfo, 0, sizeof(struct secinfo));
	ret = copyin((void *)addp->secinfo, &secinfo,
	    sizeof(struct secinfo));
	if (ret) {
		dprintf("%s: Failed to copy secinfo.\n", __func__);
		goto error;
	}

	tmp_vaddr = malloc(PAGE_SIZE, M_SGX, M_WAITOK | M_ZERO);
	ret = copyin((void *)addp->src, tmp_vaddr, PAGE_SIZE);
	if (ret) {
		dprintf("%s: Failed to copy page.\n", __func__);
		goto error;
	}

	page_type = (secinfo.flags & SECINFO_FLAGS_PT_M) >>
	    SECINFO_FLAGS_PT_S;
	if (page_type != SGX_PT_TCS && page_type != SGX_PT_REG) {
		dprintf("%s: page can't be added.\n", __func__);
		goto error;
	}
	if (page_type == SGX_PT_TCS) {
		t = (struct tcs *)tmp_vaddr;
		ret = sgx_tcs_validate(t);
		if (ret) {
			dprintf("%s: TCS page validation failed.\n",
			    __func__);
			goto error;
		}
		sgx_tcs_dump(sc, t);
	}

	addr = (addp->addr - vmh->base);
	pidx = OFF_TO_IDX(addr);

	VM_OBJECT_WLOCK(object);
	p = vm_page_lookup(object, pidx);
	if (p) {
		VM_OBJECT_WUNLOCK(object);
		/* Page already added. */
		ret = ENXIO;
		goto error;
	}

	ret = sgx_va_slot_init(sc, enclave, addr);
	if (ret) {
		VM_OBJECT_WUNLOCK(object);
		dprintf("%s: Can't init va slot.\n", __func__);
		goto error;
	}

	secs_epc_page = enclave->secs_epc_page;
	memset(&pginfo, 0, sizeof(struct page_info));
	pginfo.linaddr = (uint64_t)addp->addr;
	pginfo.srcpge = (uint64_t)tmp_vaddr;
	pginfo.secinfo = &secinfo;
	pginfo.secs = (uint64_t)secs_epc_page->base;

	mtx_lock(&sc->mtx_encls);
	ret = sgx_eadd(&pginfo, (void *)epc->base);
	if (ret == SGX_EFAULT) {
		dprintf("%s: gp fault on eadd\n", __func__);
		mtx_unlock(&sc->mtx_encls);
		VM_OBJECT_WUNLOCK(object);
		goto error;
	}
	mtx_unlock(&sc->mtx_encls);

	ret = sgx_measure_page(sc, enclave->secs_epc_page, epc, addp->mrmask);
	if (ret == SGX_EFAULT) {
		dprintf("%s: gp fault on eextend\n", __func__);
		sgx_epc_page_remove(sc, epc);
		VM_OBJECT_WUNLOCK(object);
		goto error;
	}

	sgx_insert_epc_page(enclave, epc, addr);

	VM_OBJECT_WUNLOCK(object);

	/* Release the reference. */
	vm_object_deallocate(object);

	free(tmp_vaddr, M_SGX);

	return (0);

error:
	free(tmp_vaddr, M_SGX);
	sgx_put_epc_page(sc, epc);
	vm_object_deallocate(object);

	return (ret);
}

static int
sgx_ioctl_init(struct sgx_softc *sc, struct sgx_enclave_init *initp)
{
	struct epc_page *secs_epc_page;
	struct sgx_enclave *enclave;
	struct thread *td;
	void *tmp_vaddr;
	void *einittoken;
	void *sigstruct;
	vm_object_t object;
	int retry;
	int ret;

	td = curthread;
	tmp_vaddr = NULL;
	object = NULL;

	dprintf("%s: addr %lx, sigstruct %lx, einittoken %lx\n",
	    __func__, initp->addr, initp->sigstruct, initp->einittoken);

	/* Find and get reference to VM object. */
	ret = sgx_enclave_find(sc, initp->addr, &enclave);
	if (ret) {
		dprintf("%s: Failed to find enclave.\n", __func__);
		goto error;
	}

	object = enclave->object;

	tmp_vaddr = malloc(PAGE_SIZE, M_SGX, M_WAITOK | M_ZERO);
	sigstruct = tmp_vaddr;
	einittoken = (void *)((uint64_t)sigstruct + PAGE_SIZE / 2);

	ret = copyin((void *)initp->sigstruct, sigstruct,
	    SGX_SIGSTRUCT_SIZE);
	if (ret) {
		dprintf("%s: Failed to copy SIGSTRUCT page.\n", __func__);
		goto error;
	}

	ret = copyin((void *)initp->einittoken, einittoken,
	    SGX_EINITTOKEN_SIZE);
	if (ret) {
		dprintf("%s: Failed to copy EINITTOKEN page.\n", __func__);
		goto error;
	}

	secs_epc_page = enclave->secs_epc_page;
	retry = 16;
	do {
		mtx_lock(&sc->mtx_encls);
		ret = sgx_einit(sigstruct, (void *)secs_epc_page->base,
		    einittoken);
		mtx_unlock(&sc->mtx_encls);
		dprintf("%s: sgx_einit returned %d\n", __func__, ret);
	} while (ret == SGX_UNMASKED_EVENT && retry--);

	if (ret) {
		dprintf("%s: Failed init enclave: %d\n", __func__, ret);
		td->td_retval[0] = ret;
		ret = 0;
	}

error:
	free(tmp_vaddr, M_SGX);

	/* Release the reference. */
	vm_object_deallocate(object);

	return (ret);
}

static int
sgx_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{
	struct sgx_enclave_add_page *addp;
	struct sgx_enclave_create *param;
	struct sgx_enclave_init *initp;
	struct sgx_softc *sc;
	int ret;
	int len;

	sc = &sgx_sc;

	len = IOCPARM_LEN(cmd);

	dprintf("%s: cmd %lx, addr %lx, len %d\n",
	    __func__, cmd, (uint64_t)addr, len);

	if (len > SGX_IOCTL_MAX_DATA_LEN)
		return (EINVAL);

	switch (cmd) {
	case SGX_IOC_ENCLAVE_CREATE:
		param = (struct sgx_enclave_create *)addr;
		ret = sgx_ioctl_create(sc, param);
		break;
	case SGX_IOC_ENCLAVE_ADD_PAGE:
		addp = (struct sgx_enclave_add_page *)addr;
		ret = sgx_ioctl_add_page(sc, addp);
		break;
	case SGX_IOC_ENCLAVE_INIT:
		initp = (struct sgx_enclave_init *)addr;
		ret = sgx_ioctl_init(sc, initp);
		break;
	default:
		return (EINVAL);
	}

	return (ret);
}

static int
sgx_mmap_single(struct cdev *cdev, vm_ooffset_t *offset,
    vm_size_t mapsize, struct vm_object **objp, int nprot)
{
	struct sgx_vm_handle *vmh;
	struct sgx_softc *sc;

	sc = &sgx_sc;

	dprintf("%s: mapsize 0x%lx, offset %lx\n",
	    __func__, mapsize, *offset);

	vmh = malloc(sizeof(struct sgx_vm_handle),
	    M_SGX, M_WAITOK | M_ZERO);
	vmh->sc = sc;
	vmh->size = mapsize;
	vmh->mem = cdev_pager_allocate(vmh, OBJT_MGTDEVICE, &sgx_pg_ops,
	    mapsize, nprot, *offset, NULL);
	if (vmh->mem == NULL) {
		free(vmh, M_SGX);
		return (ENOMEM);
	}

	VM_OBJECT_WLOCK(vmh->mem);
	vm_object_set_flag(vmh->mem, OBJ_PG_DTOR);
	VM_OBJECT_WUNLOCK(vmh->mem);

	*objp = vmh->mem;

	return (0);
}

static struct cdevsw sgx_cdevsw = {
	.d_version =		D_VERSION,
	.d_ioctl =		sgx_ioctl,
	.d_mmap_single =	sgx_mmap_single,
	.d_name =		"Intel SGX",
};

static int
sgx_get_epc_area(struct sgx_softc *sc)
{
	vm_offset_t epc_base_vaddr;
	u_int cp[4];
	int error;
	int i;

	cpuid_count(SGX_CPUID, 0x2, cp);

	sc->epc_base = ((uint64_t)(cp[1] & 0xfffff) << 32) +
	    (cp[0] & 0xfffff000);
	sc->epc_size = ((uint64_t)(cp[3] & 0xfffff) << 32) +
	    (cp[2] & 0xfffff000);
	sc->npages = sc->epc_size / SGX_PAGE_SIZE;

	if (sc->epc_size == 0 || sc->epc_base == 0) {
		printf("%s: Incorrect EPC data: EPC base %lx, size %lu\n",
		    __func__, sc->epc_base, sc->epc_size);
		return (EINVAL);
	}

	if (cp[3] & 0xffff)
		sc->enclave_size_max = (1 << ((cp[3] >> 8) & 0xff));
	else
		sc->enclave_size_max = SGX_ENCL_SIZE_MAX_DEF;

	epc_base_vaddr = (vm_offset_t)pmap_mapdev_attr(sc->epc_base,
	    sc->epc_size, VM_MEMATTR_DEFAULT);

	sc->epc_pages = malloc(sizeof(struct epc_page) * sc->npages,
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->npages; i++) {
		sc->epc_pages[i].base = epc_base_vaddr + SGX_PAGE_SIZE * i;
		sc->epc_pages[i].phys = sc->epc_base + SGX_PAGE_SIZE * i;
		sc->epc_pages[i].index = i;
	}

	sc->vmem_epc = vmem_create("SGX EPC", sc->epc_base, sc->epc_size,
	    PAGE_SIZE, PAGE_SIZE, M_FIRSTFIT | M_WAITOK);
	if (sc->vmem_epc == NULL) {
		printf("%s: Can't create vmem arena.\n", __func__);
		free(sc->epc_pages, M_SGX);
		return (EINVAL);
	}

	error = vm_phys_fictitious_reg_range(sc->epc_base,
	    sc->epc_base + sc->epc_size, VM_MEMATTR_DEFAULT);
	if (error) {
		printf("%s: Can't register fictitious space.\n", __func__);
		free(sc->epc_pages, M_SGX);
		return (EINVAL);
	}

	return (0);
}

static void
sgx_put_epc_area(struct sgx_softc *sc)
{

	vm_phys_fictitious_unreg_range(sc->epc_base,
	    sc->epc_base + sc->epc_size);

	free(sc->epc_pages, M_SGX);
}

static int
sgx_load(void)
{
	struct sgx_softc *sc;
	int error;

	sc = &sgx_sc;

	if ((cpu_stdext_feature & CPUID_STDEXT_SGX) == 0)
		return (ENXIO);

	error = sgx_get_epc_area(sc);
	if (error) {
		printf("%s: Failed to get Processor Reserved Memory area.\n",
		    __func__);
		return (ENXIO);
	}

	mtx_init(&sc->mtx_encls, "SGX ENCLS", NULL, MTX_DEF);
	mtx_init(&sc->mtx, "SGX driver", NULL, MTX_DEF);

	TAILQ_INIT(&sc->enclaves);

	sc->sgx_cdev = make_dev(&sgx_cdevsw, 0, UID_ROOT, GID_WHEEL,
	    0600, "isgx");

	sc->state |= SGX_STATE_RUNNING;

	printf("SGX initialized: EPC base 0x%lx size %ld (%d pages)\n",
	    sc->epc_base, sc->epc_size, sc->npages);

	return (0);
}

static int
sgx_unload(void)
{
	struct sgx_softc *sc;

	sc = &sgx_sc;

	if ((sc->state & SGX_STATE_RUNNING) == 0)
		return (0);

	mtx_lock(&sc->mtx);
	if (!TAILQ_EMPTY(&sc->enclaves)) {
		mtx_unlock(&sc->mtx);
		return (EBUSY);
	}
	sc->state &= ~SGX_STATE_RUNNING;
	mtx_unlock(&sc->mtx);

	destroy_dev(sc->sgx_cdev);

	vmem_destroy(sc->vmem_epc);
	sgx_put_epc_area(sc);

	mtx_destroy(&sc->mtx_encls);
	mtx_destroy(&sc->mtx);

	return (0);
}

static int
sgx_handler(module_t mod, int what, void *arg)
{
	int error;

	switch (what) {
	case MOD_LOAD:
		error = sgx_load();
		break;
	case MOD_UNLOAD:
		error = sgx_unload();
		break;
	default:
		error = 0;
		break;
	}

	return (error);
}

static moduledata_t sgx_kmod = {
	"sgx",
	sgx_handler,
	NULL
};

DECLARE_MODULE(sgx, sgx_kmod, SI_SUB_LAST, SI_ORDER_ANY);
MODULE_VERSION(sgx, 1);
