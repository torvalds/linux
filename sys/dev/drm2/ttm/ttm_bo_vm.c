/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
/*
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * <kib@FreeBSD.org> under sponsorship from the FreeBSD Foundation.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <dev/drm2/drmP.h>
#include <dev/drm2/ttm/ttm_module.h>
#include <dev/drm2/ttm/ttm_bo_driver.h>
#include <dev/drm2/ttm/ttm_placement.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>

#define TTM_BO_VM_NUM_PREFAULT 16

RB_GENERATE(ttm_bo_device_buffer_objects, ttm_buffer_object, vm_rb,
    ttm_bo_cmp_rb_tree_items);

int
ttm_bo_cmp_rb_tree_items(struct ttm_buffer_object *a,
    struct ttm_buffer_object *b)
{

	if (a->vm_node->start < b->vm_node->start) {
		return (-1);
	} else if (a->vm_node->start > b->vm_node->start) {
		return (1);
	} else {
		return (0);
	}
}

static struct ttm_buffer_object *ttm_bo_vm_lookup_rb(struct ttm_bo_device *bdev,
						     unsigned long page_start,
						     unsigned long num_pages)
{
	unsigned long cur_offset;
	struct ttm_buffer_object *bo;
	struct ttm_buffer_object *best_bo = NULL;

	bo = RB_ROOT(&bdev->addr_space_rb);
	while (bo != NULL) {
		cur_offset = bo->vm_node->start;
		if (page_start >= cur_offset) {
			best_bo = bo;
			if (page_start == cur_offset)
				break;
			bo = RB_RIGHT(bo, vm_rb);
		} else
			bo = RB_LEFT(bo, vm_rb);
	}

	if (unlikely(best_bo == NULL))
		return NULL;

	if (unlikely((best_bo->vm_node->start + best_bo->num_pages) <
		     (page_start + num_pages)))
		return NULL;

	return best_bo;
}

static int
ttm_bo_vm_fault(vm_object_t vm_obj, vm_ooffset_t offset,
    int prot, vm_page_t *mres)
{

	struct ttm_buffer_object *bo = vm_obj->handle;
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_tt *ttm = NULL;
	vm_page_t m, m1;
	int ret;
	int retval = VM_PAGER_OK;
	struct ttm_mem_type_manager *man =
		&bdev->man[bo->mem.mem_type];

	vm_object_pip_add(vm_obj, 1);
	if (*mres != NULL) {
		vm_page_lock(*mres);
		vm_page_remove(*mres);
		vm_page_unlock(*mres);
	}
retry:
	VM_OBJECT_WUNLOCK(vm_obj);
	m = NULL;

reserve:
	ret = ttm_bo_reserve(bo, false, false, false, 0);
	if (unlikely(ret != 0)) {
		if (ret == -EBUSY) {
			kern_yield(PRI_USER);
			goto reserve;
		}
	}

	if (bdev->driver->fault_reserve_notify) {
		ret = bdev->driver->fault_reserve_notify(bo);
		switch (ret) {
		case 0:
			break;
		case -EBUSY:
		case -ERESTARTSYS:
		case -EINTR:
			kern_yield(PRI_USER);
			goto reserve;
		default:
			retval = VM_PAGER_ERROR;
			goto out_unlock;
		}
	}

	/*
	 * Wait for buffer data in transit, due to a pipelined
	 * move.
	 */

	mtx_lock(&bdev->fence_lock);
	if (test_bit(TTM_BO_PRIV_FLAG_MOVING, &bo->priv_flags)) {
		/*
		 * Here, the behavior differs between Linux and FreeBSD.
		 *
		 * On Linux, the wait is interruptible (3rd argument to
		 * ttm_bo_wait). There must be some mechanism to resume
		 * page fault handling, once the signal is processed.
		 *
		 * On FreeBSD, the wait is uninteruptible. This is not a
		 * problem as we can't end up with an unkillable process
		 * here, because the wait will eventually time out.
		 *
		 * An example of this situation is the Xorg process
		 * which uses SIGALRM internally. The signal could
		 * interrupt the wait, causing the page fault to fail
		 * and the process to receive SIGSEGV.
		 */
		ret = ttm_bo_wait(bo, false, false, false);
		mtx_unlock(&bdev->fence_lock);
		if (unlikely(ret != 0)) {
			retval = VM_PAGER_ERROR;
			goto out_unlock;
		}
	} else
		mtx_unlock(&bdev->fence_lock);

	ret = ttm_mem_io_lock(man, true);
	if (unlikely(ret != 0)) {
		retval = VM_PAGER_ERROR;
		goto out_unlock;
	}
	ret = ttm_mem_io_reserve_vm(bo);
	if (unlikely(ret != 0)) {
		retval = VM_PAGER_ERROR;
		goto out_io_unlock;
	}

	/*
	 * Strictly, we're not allowed to modify vma->vm_page_prot here,
	 * since the mmap_sem is only held in read mode. However, we
	 * modify only the caching bits of vma->vm_page_prot and
	 * consider those bits protected by
	 * the bo->mutex, as we should be the only writers.
	 * There shouldn't really be any readers of these bits except
	 * within vm_insert_mixed()? fork?
	 *
	 * TODO: Add a list of vmas to the bo, and change the
	 * vma->vm_page_prot when the object changes caching policy, with
	 * the correct locks held.
	 */
	if (!bo->mem.bus.is_iomem) {
		/* Allocate all page at once, most common usage */
		ttm = bo->ttm;
		if (ttm->bdev->driver->ttm_tt_populate(ttm)) {
			retval = VM_PAGER_ERROR;
			goto out_io_unlock;
		}
	}

	if (bo->mem.bus.is_iomem) {
		m = PHYS_TO_VM_PAGE(bo->mem.bus.base + bo->mem.bus.offset +
		    offset);
		KASSERT((m->flags & PG_FICTITIOUS) != 0,
		    ("physical address %#jx not fictitious",
		    (uintmax_t)(bo->mem.bus.base + bo->mem.bus.offset
		    + offset)));
		pmap_page_set_memattr(m, ttm_io_prot(bo->mem.placement));
	} else {
		ttm = bo->ttm;
		m = ttm->pages[OFF_TO_IDX(offset)];
		if (unlikely(!m)) {
			retval = VM_PAGER_ERROR;
			goto out_io_unlock;
		}
		pmap_page_set_memattr(m,
		    (bo->mem.placement & TTM_PL_FLAG_CACHED) ?
		    VM_MEMATTR_WRITE_BACK : ttm_io_prot(bo->mem.placement));
	}

	VM_OBJECT_WLOCK(vm_obj);
	if (vm_page_busied(m)) {
		vm_page_lock(m);
		VM_OBJECT_WUNLOCK(vm_obj);
		vm_page_busy_sleep(m, "ttmpbs", false);
		VM_OBJECT_WLOCK(vm_obj);
		ttm_mem_io_unlock(man);
		ttm_bo_unreserve(bo);
		goto retry;
	}
	m1 = vm_page_lookup(vm_obj, OFF_TO_IDX(offset));
	if (m1 == NULL) {
		if (vm_page_insert(m, vm_obj, OFF_TO_IDX(offset))) {
			VM_OBJECT_WUNLOCK(vm_obj);
			vm_wait(vm_obj);
			VM_OBJECT_WLOCK(vm_obj);
			ttm_mem_io_unlock(man);
			ttm_bo_unreserve(bo);
			goto retry;
		}
	} else {
		KASSERT(m == m1,
		    ("inconsistent insert bo %p m %p m1 %p offset %jx",
		    bo, m, m1, (uintmax_t)offset));
	}
	m->valid = VM_PAGE_BITS_ALL;
	vm_page_xbusy(m);
	if (*mres != NULL) {
		KASSERT(*mres != m, ("losing %p %p", *mres, m));
		vm_page_lock(*mres);
		vm_page_free(*mres);
		vm_page_unlock(*mres);
	}
	*mres = m;

out_io_unlock1:
	ttm_mem_io_unlock(man);
out_unlock1:
	ttm_bo_unreserve(bo);
	vm_object_pip_wakeup(vm_obj);
	return (retval);

out_io_unlock:
	VM_OBJECT_WLOCK(vm_obj);
	goto out_io_unlock1;

out_unlock:
	VM_OBJECT_WLOCK(vm_obj);
	goto out_unlock1;
}

static int
ttm_bo_vm_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
    vm_ooffset_t foff, struct ucred *cred, u_short *color)
{

	/*
	 * On Linux, a reference to the buffer object is acquired here.
	 * The reason is that this function is not called when the
	 * mmap() is initialized, but only when a process forks for
	 * instance. Therefore on Linux, the reference on the bo is
	 * acquired either in ttm_bo_mmap() or ttm_bo_vm_open(). It's
	 * then released in ttm_bo_vm_close().
	 *
	 * Here, this function is called during mmap() initialization.
	 * Thus, the reference acquired in ttm_bo_mmap_single() is
	 * sufficient.
	 */

	*color = 0;
	return (0);
}

static void
ttm_bo_vm_dtor(void *handle)
{
	struct ttm_buffer_object *bo = handle;

	ttm_bo_unref(&bo);
}

static struct cdev_pager_ops ttm_pager_ops = {
	.cdev_pg_fault = ttm_bo_vm_fault,
	.cdev_pg_ctor = ttm_bo_vm_ctor,
	.cdev_pg_dtor = ttm_bo_vm_dtor
};

int
ttm_bo_mmap_single(struct ttm_bo_device *bdev, vm_ooffset_t *offset, vm_size_t size,
    struct vm_object **obj_res, int nprot)
{
	struct ttm_bo_driver *driver;
	struct ttm_buffer_object *bo;
	struct vm_object *vm_obj;
	int ret;

	rw_wlock(&bdev->vm_lock);
	bo = ttm_bo_vm_lookup_rb(bdev, OFF_TO_IDX(*offset), OFF_TO_IDX(size));
	if (likely(bo != NULL))
		refcount_acquire(&bo->kref);
	rw_wunlock(&bdev->vm_lock);

	if (unlikely(bo == NULL)) {
		printf("[TTM] Could not find buffer object to map\n");
		return (-EINVAL);
	}

	driver = bo->bdev->driver;
	if (unlikely(!driver->verify_access)) {
		ret = -EPERM;
		goto out_unref;
	}
	ret = driver->verify_access(bo);
	if (unlikely(ret != 0))
		goto out_unref;

	vm_obj = cdev_pager_allocate(bo, OBJT_MGTDEVICE, &ttm_pager_ops,
	    size, nprot, 0, curthread->td_ucred);
	if (vm_obj == NULL) {
		ret = -EINVAL;
		goto out_unref;
	}
	/*
	 * Note: We're transferring the bo reference to vm_obj->handle here.
	 */
	*offset = 0;
	*obj_res = vm_obj;
	return 0;
out_unref:
	ttm_bo_unref(&bo);
	return ret;
}

void
ttm_bo_release_mmap(struct ttm_buffer_object *bo)
{
	vm_object_t vm_obj;
	vm_page_t m;
	int i;

	vm_obj = cdev_pager_lookup(bo);
	if (vm_obj == NULL)
		return;

	VM_OBJECT_WLOCK(vm_obj);
retry:
	for (i = 0; i < bo->num_pages; i++) {
		m = vm_page_lookup(vm_obj, i);
		if (m == NULL)
			continue;
		if (vm_page_sleep_if_busy(m, "ttm_unm"))
			goto retry;
		cdev_pager_free_page(vm_obj, m);
	}
	VM_OBJECT_WUNLOCK(vm_obj);

	vm_object_deallocate(vm_obj);
}

#if 0
int ttm_fbdev_mmap(struct vm_area_struct *vma, struct ttm_buffer_object *bo)
{
	if (vma->vm_pgoff != 0)
		return -EACCES;

	vma->vm_ops = &ttm_bo_vm_ops;
	vma->vm_private_data = ttm_bo_reference(bo);
	vma->vm_flags |= VM_IO | VM_MIXEDMAP | VM_DONTEXPAND;
	return 0;
}

ssize_t ttm_bo_io(struct ttm_bo_device *bdev, struct file *filp,
		  const char __user *wbuf, char __user *rbuf, size_t count,
		  loff_t *f_pos, bool write)
{
	struct ttm_buffer_object *bo;
	struct ttm_bo_driver *driver;
	struct ttm_bo_kmap_obj map;
	unsigned long dev_offset = (*f_pos >> PAGE_SHIFT);
	unsigned long kmap_offset;
	unsigned long kmap_end;
	unsigned long kmap_num;
	size_t io_size;
	unsigned int page_offset;
	char *virtual;
	int ret;
	bool no_wait = false;
	bool dummy;

	read_lock(&bdev->vm_lock);
	bo = ttm_bo_vm_lookup_rb(bdev, dev_offset, 1);
	if (likely(bo != NULL))
		ttm_bo_reference(bo);
	read_unlock(&bdev->vm_lock);

	if (unlikely(bo == NULL))
		return -EFAULT;

	driver = bo->bdev->driver;
	if (unlikely(!driver->verify_access)) {
		ret = -EPERM;
		goto out_unref;
	}

	ret = driver->verify_access(bo, filp);
	if (unlikely(ret != 0))
		goto out_unref;

	kmap_offset = dev_offset - bo->vm_node->start;
	if (unlikely(kmap_offset >= bo->num_pages)) {
		ret = -EFBIG;
		goto out_unref;
	}

	page_offset = *f_pos & ~PAGE_MASK;
	io_size = bo->num_pages - kmap_offset;
	io_size = (io_size << PAGE_SHIFT) - page_offset;
	if (count < io_size)
		io_size = count;

	kmap_end = (*f_pos + count - 1) >> PAGE_SHIFT;
	kmap_num = kmap_end - kmap_offset + 1;

	ret = ttm_bo_reserve(bo, true, no_wait, false, 0);

	switch (ret) {
	case 0:
		break;
	case -EBUSY:
		ret = -EAGAIN;
		goto out_unref;
	default:
		goto out_unref;
	}

	ret = ttm_bo_kmap(bo, kmap_offset, kmap_num, &map);
	if (unlikely(ret != 0)) {
		ttm_bo_unreserve(bo);
		goto out_unref;
	}

	virtual = ttm_kmap_obj_virtual(&map, &dummy);
	virtual += page_offset;

	if (write)
		ret = copy_from_user(virtual, wbuf, io_size);
	else
		ret = copy_to_user(rbuf, virtual, io_size);

	ttm_bo_kunmap(&map);
	ttm_bo_unreserve(bo);
	ttm_bo_unref(&bo);

	if (unlikely(ret != 0))
		return -EFBIG;

	*f_pos += io_size;

	return io_size;
out_unref:
	ttm_bo_unref(&bo);
	return ret;
}

ssize_t ttm_bo_fbdev_io(struct ttm_buffer_object *bo, const char __user *wbuf,
			char __user *rbuf, size_t count, loff_t *f_pos,
			bool write)
{
	struct ttm_bo_kmap_obj map;
	unsigned long kmap_offset;
	unsigned long kmap_end;
	unsigned long kmap_num;
	size_t io_size;
	unsigned int page_offset;
	char *virtual;
	int ret;
	bool no_wait = false;
	bool dummy;

	kmap_offset = (*f_pos >> PAGE_SHIFT);
	if (unlikely(kmap_offset >= bo->num_pages))
		return -EFBIG;

	page_offset = *f_pos & ~PAGE_MASK;
	io_size = bo->num_pages - kmap_offset;
	io_size = (io_size << PAGE_SHIFT) - page_offset;
	if (count < io_size)
		io_size = count;

	kmap_end = (*f_pos + count - 1) >> PAGE_SHIFT;
	kmap_num = kmap_end - kmap_offset + 1;

	ret = ttm_bo_reserve(bo, true, no_wait, false, 0);

	switch (ret) {
	case 0:
		break;
	case -EBUSY:
		return -EAGAIN;
	default:
		return ret;
	}

	ret = ttm_bo_kmap(bo, kmap_offset, kmap_num, &map);
	if (unlikely(ret != 0)) {
		ttm_bo_unreserve(bo);
		return ret;
	}

	virtual = ttm_kmap_obj_virtual(&map, &dummy);
	virtual += page_offset;

	if (write)
		ret = copy_from_user(virtual, wbuf, io_size);
	else
		ret = copy_to_user(rbuf, virtual, io_size);

	ttm_bo_kunmap(&map);
	ttm_bo_unreserve(bo);
	ttm_bo_unref(&bo);

	if (unlikely(ret != 0))
		return ret;

	*f_pos += io_size;

	return io_size;
}
#endif
