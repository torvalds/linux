/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2018 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_stack.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/sglist.h>
#include <sys/sleepqueue.h>
#include <sys/refcount.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <sys/rwlock.h>
#include <sys/mman.h>
#include <sys/stack.h>
#include <sys/user.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <machine/stdarg.h>

#if defined(__i386__) || defined(__amd64__)
#include <machine/md_var.h>
#endif

#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/file.h>
#include <linux/sysfs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/compat.h>
#include <linux/poll.h>
#include <linux/smp.h>

#if defined(__i386__) || defined(__amd64__)
#include <asm/smp.h>
#endif

SYSCTL_NODE(_compat, OID_AUTO, linuxkpi, CTLFLAG_RW, 0, "LinuxKPI parameters");

MALLOC_DEFINE(M_KMALLOC, "linux", "Linux kmalloc compat");

#include <linux/rbtree.h>
/* Undo Linux compat changes. */
#undef RB_ROOT
#undef file
#undef cdev
#define	RB_ROOT(head)	(head)->rbh_root

static void linux_cdev_deref(struct linux_cdev *ldev);
static struct vm_area_struct *linux_cdev_handle_find(void *handle);

struct kobject linux_class_root;
struct device linux_root_device;
struct class linux_class_misc;
struct list_head pci_drivers;
struct list_head pci_devices;
spinlock_t pci_lock;

unsigned long linux_timer_hz_mask;

int
panic_cmp(struct rb_node *one, struct rb_node *two)
{
	panic("no cmp");
}

RB_GENERATE(linux_root, rb_node, __entry, panic_cmp);

int
kobject_set_name_vargs(struct kobject *kobj, const char *fmt, va_list args)
{
	va_list tmp_va;
	int len;
	char *old;
	char *name;
	char dummy;

	old = kobj->name;

	if (old && fmt == NULL)
		return (0);

	/* compute length of string */
	va_copy(tmp_va, args);
	len = vsnprintf(&dummy, 0, fmt, tmp_va);
	va_end(tmp_va);

	/* account for zero termination */
	len++;

	/* check for error */
	if (len < 1)
		return (-EINVAL);

	/* allocate memory for string */
	name = kzalloc(len, GFP_KERNEL);
	if (name == NULL)
		return (-ENOMEM);
	vsnprintf(name, len, fmt, args);
	kobj->name = name;

	/* free old string */
	kfree(old);

	/* filter new string */
	for (; *name != '\0'; name++)
		if (*name == '/')
			*name = '!';
	return (0);
}

int
kobject_set_name(struct kobject *kobj, const char *fmt, ...)
{
	va_list args;
	int error;

	va_start(args, fmt);
	error = kobject_set_name_vargs(kobj, fmt, args);
	va_end(args);

	return (error);
}

static int
kobject_add_complete(struct kobject *kobj, struct kobject *parent)
{
	const struct kobj_type *t;
	int error;

	kobj->parent = parent;
	error = sysfs_create_dir(kobj);
	if (error == 0 && kobj->ktype && kobj->ktype->default_attrs) {
		struct attribute **attr;
		t = kobj->ktype;

		for (attr = t->default_attrs; *attr != NULL; attr++) {
			error = sysfs_create_file(kobj, *attr);
			if (error)
				break;
		}
		if (error)
			sysfs_remove_dir(kobj);

	}
	return (error);
}

int
kobject_add(struct kobject *kobj, struct kobject *parent, const char *fmt, ...)
{
	va_list args;
	int error;

	va_start(args, fmt);
	error = kobject_set_name_vargs(kobj, fmt, args);
	va_end(args);
	if (error)
		return (error);

	return kobject_add_complete(kobj, parent);
}

void
linux_kobject_release(struct kref *kref)
{
	struct kobject *kobj;
	char *name;

	kobj = container_of(kref, struct kobject, kref);
	sysfs_remove_dir(kobj);
	name = kobj->name;
	if (kobj->ktype && kobj->ktype->release)
		kobj->ktype->release(kobj);
	kfree(name);
}

static void
linux_kobject_kfree(struct kobject *kobj)
{
	kfree(kobj);
}

static void
linux_kobject_kfree_name(struct kobject *kobj)
{
	if (kobj) {
		kfree(kobj->name);
	}
}

const struct kobj_type linux_kfree_type = {
	.release = linux_kobject_kfree
};

static void
linux_device_release(struct device *dev)
{
	pr_debug("linux_device_release: %s\n", dev_name(dev));
	kfree(dev);
}

static ssize_t
linux_class_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct class_attribute *dattr;
	ssize_t error;

	dattr = container_of(attr, struct class_attribute, attr);
	error = -EIO;
	if (dattr->show)
		error = dattr->show(container_of(kobj, struct class, kobj),
		    dattr, buf);
	return (error);
}

static ssize_t
linux_class_store(struct kobject *kobj, struct attribute *attr, const char *buf,
    size_t count)
{
	struct class_attribute *dattr;
	ssize_t error;

	dattr = container_of(attr, struct class_attribute, attr);
	error = -EIO;
	if (dattr->store)
		error = dattr->store(container_of(kobj, struct class, kobj),
		    dattr, buf, count);
	return (error);
}

static void
linux_class_release(struct kobject *kobj)
{
	struct class *class;

	class = container_of(kobj, struct class, kobj);
	if (class->class_release)
		class->class_release(class);
}

static const struct sysfs_ops linux_class_sysfs = {
	.show  = linux_class_show,
	.store = linux_class_store,
};

const struct kobj_type linux_class_ktype = {
	.release = linux_class_release,
	.sysfs_ops = &linux_class_sysfs
};

static void
linux_dev_release(struct kobject *kobj)
{
	struct device *dev;

	dev = container_of(kobj, struct device, kobj);
	/* This is the precedence defined by linux. */
	if (dev->release)
		dev->release(dev);
	else if (dev->class && dev->class->dev_release)
		dev->class->dev_release(dev);
}

static ssize_t
linux_dev_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct device_attribute *dattr;
	ssize_t error;

	dattr = container_of(attr, struct device_attribute, attr);
	error = -EIO;
	if (dattr->show)
		error = dattr->show(container_of(kobj, struct device, kobj),
		    dattr, buf);
	return (error);
}

static ssize_t
linux_dev_store(struct kobject *kobj, struct attribute *attr, const char *buf,
    size_t count)
{
	struct device_attribute *dattr;
	ssize_t error;

	dattr = container_of(attr, struct device_attribute, attr);
	error = -EIO;
	if (dattr->store)
		error = dattr->store(container_of(kobj, struct device, kobj),
		    dattr, buf, count);
	return (error);
}

static const struct sysfs_ops linux_dev_sysfs = {
	.show  = linux_dev_show,
	.store = linux_dev_store,
};

const struct kobj_type linux_dev_ktype = {
	.release = linux_dev_release,
	.sysfs_ops = &linux_dev_sysfs
};

struct device *
device_create(struct class *class, struct device *parent, dev_t devt,
    void *drvdata, const char *fmt, ...)
{
	struct device *dev;
	va_list args;

	dev = kzalloc(sizeof(*dev), M_WAITOK);
	dev->parent = parent;
	dev->class = class;
	dev->devt = devt;
	dev->driver_data = drvdata;
	dev->release = linux_device_release;
	va_start(args, fmt);
	kobject_set_name_vargs(&dev->kobj, fmt, args);
	va_end(args);
	device_register(dev);

	return (dev);
}

int
kobject_init_and_add(struct kobject *kobj, const struct kobj_type *ktype,
    struct kobject *parent, const char *fmt, ...)
{
	va_list args;
	int error;

	kobject_init(kobj, ktype);
	kobj->ktype = ktype;
	kobj->parent = parent;
	kobj->name = NULL;

	va_start(args, fmt);
	error = kobject_set_name_vargs(kobj, fmt, args);
	va_end(args);
	if (error)
		return (error);
	return kobject_add_complete(kobj, parent);
}

static void
linux_kq_lock(void *arg)
{
	spinlock_t *s = arg;

	spin_lock(s);
}
static void
linux_kq_unlock(void *arg)
{
	spinlock_t *s = arg;

	spin_unlock(s);
}

static void
linux_kq_lock_owned(void *arg)
{
#ifdef INVARIANTS
	spinlock_t *s = arg;

	mtx_assert(&s->m, MA_OWNED);
#endif
}

static void
linux_kq_lock_unowned(void *arg)
{
#ifdef INVARIANTS
	spinlock_t *s = arg;

	mtx_assert(&s->m, MA_NOTOWNED);
#endif
}

static void
linux_file_kqfilter_poll(struct linux_file *, int);

struct linux_file *
linux_file_alloc(void)
{
	struct linux_file *filp;

	filp = kzalloc(sizeof(*filp), GFP_KERNEL);

	/* set initial refcount */
	filp->f_count = 1;

	/* setup fields needed by kqueue support */
	spin_lock_init(&filp->f_kqlock);
	knlist_init(&filp->f_selinfo.si_note, &filp->f_kqlock,
	    linux_kq_lock, linux_kq_unlock,
	    linux_kq_lock_owned, linux_kq_lock_unowned);

	return (filp);
}

void
linux_file_free(struct linux_file *filp)
{
	if (filp->_file == NULL) {
		if (filp->f_shmem != NULL)
			vm_object_deallocate(filp->f_shmem);
		kfree(filp);
	} else {
		/*
		 * The close method of the character device or file
		 * will free the linux_file structure:
		 */
		_fdrop(filp->_file, curthread);
	}
}

static int
linux_cdev_pager_fault(vm_object_t vm_obj, vm_ooffset_t offset, int prot,
    vm_page_t *mres)
{
	struct vm_area_struct *vmap;

	vmap = linux_cdev_handle_find(vm_obj->handle);

	MPASS(vmap != NULL);
	MPASS(vmap->vm_private_data == vm_obj->handle);

	if (likely(vmap->vm_ops != NULL && offset < vmap->vm_len)) {
		vm_paddr_t paddr = IDX_TO_OFF(vmap->vm_pfn) + offset;
		vm_page_t page;

		if (((*mres)->flags & PG_FICTITIOUS) != 0) {
			/*
			 * If the passed in result page is a fake
			 * page, update it with the new physical
			 * address.
			 */
			page = *mres;
			vm_page_updatefake(page, paddr, vm_obj->memattr);
		} else {
			/*
			 * Replace the passed in "mres" page with our
			 * own fake page and free up the all of the
			 * original pages.
			 */
			VM_OBJECT_WUNLOCK(vm_obj);
			page = vm_page_getfake(paddr, vm_obj->memattr);
			VM_OBJECT_WLOCK(vm_obj);

			vm_page_replace_checked(page, vm_obj,
			    (*mres)->pindex, *mres);

			vm_page_lock(*mres);
			vm_page_free(*mres);
			vm_page_unlock(*mres);
			*mres = page;
		}
		page->valid = VM_PAGE_BITS_ALL;
		return (VM_PAGER_OK);
	}
	return (VM_PAGER_FAIL);
}

static int
linux_cdev_pager_populate(vm_object_t vm_obj, vm_pindex_t pidx, int fault_type,
    vm_prot_t max_prot, vm_pindex_t *first, vm_pindex_t *last)
{
	struct vm_area_struct *vmap;
	int err;

	linux_set_current(curthread);

	/* get VM area structure */
	vmap = linux_cdev_handle_find(vm_obj->handle);
	MPASS(vmap != NULL);
	MPASS(vmap->vm_private_data == vm_obj->handle);

	VM_OBJECT_WUNLOCK(vm_obj);

	down_write(&vmap->vm_mm->mmap_sem);
	if (unlikely(vmap->vm_ops == NULL)) {
		err = VM_FAULT_SIGBUS;
	} else {
		struct vm_fault vmf;

		/* fill out VM fault structure */
		vmf.virtual_address = (void *)(uintptr_t)IDX_TO_OFF(pidx);
		vmf.flags = (fault_type & VM_PROT_WRITE) ? FAULT_FLAG_WRITE : 0;
		vmf.pgoff = 0;
		vmf.page = NULL;
		vmf.vma = vmap;

		vmap->vm_pfn_count = 0;
		vmap->vm_pfn_pcount = &vmap->vm_pfn_count;
		vmap->vm_obj = vm_obj;

		err = vmap->vm_ops->fault(vmap, &vmf);

		while (vmap->vm_pfn_count == 0 && err == VM_FAULT_NOPAGE) {
			kern_yield(PRI_USER);
			err = vmap->vm_ops->fault(vmap, &vmf);
		}
	}

	/* translate return code */
	switch (err) {
	case VM_FAULT_OOM:
		err = VM_PAGER_AGAIN;
		break;
	case VM_FAULT_SIGBUS:
		err = VM_PAGER_BAD;
		break;
	case VM_FAULT_NOPAGE:
		/*
		 * By contract the fault handler will return having
		 * busied all the pages itself. If pidx is already
		 * found in the object, it will simply xbusy the first
		 * page and return with vm_pfn_count set to 1.
		 */
		*first = vmap->vm_pfn_first;
		*last = *first + vmap->vm_pfn_count - 1;
		err = VM_PAGER_OK;
		break;
	default:
		err = VM_PAGER_ERROR;
		break;
	}
	up_write(&vmap->vm_mm->mmap_sem);
	VM_OBJECT_WLOCK(vm_obj);
	return (err);
}

static struct rwlock linux_vma_lock;
static TAILQ_HEAD(, vm_area_struct) linux_vma_head =
    TAILQ_HEAD_INITIALIZER(linux_vma_head);

static void
linux_cdev_handle_free(struct vm_area_struct *vmap)
{
	/* Drop reference on vm_file */
	if (vmap->vm_file != NULL)
		fput(vmap->vm_file);

	/* Drop reference on mm_struct */
	mmput(vmap->vm_mm);

	kfree(vmap);
}

static void
linux_cdev_handle_remove(struct vm_area_struct *vmap)
{
	rw_wlock(&linux_vma_lock);
	TAILQ_REMOVE(&linux_vma_head, vmap, vm_entry);
	rw_wunlock(&linux_vma_lock);
}

static struct vm_area_struct *
linux_cdev_handle_find(void *handle)
{
	struct vm_area_struct *vmap;

	rw_rlock(&linux_vma_lock);
	TAILQ_FOREACH(vmap, &linux_vma_head, vm_entry) {
		if (vmap->vm_private_data == handle)
			break;
	}
	rw_runlock(&linux_vma_lock);
	return (vmap);
}

static int
linux_cdev_pager_ctor(void *handle, vm_ooffset_t size, vm_prot_t prot,
		      vm_ooffset_t foff, struct ucred *cred, u_short *color)
{

	MPASS(linux_cdev_handle_find(handle) != NULL);
	*color = 0;
	return (0);
}

static void
linux_cdev_pager_dtor(void *handle)
{
	const struct vm_operations_struct *vm_ops;
	struct vm_area_struct *vmap;

	vmap = linux_cdev_handle_find(handle);
	MPASS(vmap != NULL);

	/*
	 * Remove handle before calling close operation to prevent
	 * other threads from reusing the handle pointer.
	 */
	linux_cdev_handle_remove(vmap);

	down_write(&vmap->vm_mm->mmap_sem);
	vm_ops = vmap->vm_ops;
	if (likely(vm_ops != NULL))
		vm_ops->close(vmap);
	up_write(&vmap->vm_mm->mmap_sem);

	linux_cdev_handle_free(vmap);
}

static struct cdev_pager_ops linux_cdev_pager_ops[2] = {
  {
	/* OBJT_MGTDEVICE */
	.cdev_pg_populate	= linux_cdev_pager_populate,
	.cdev_pg_ctor	= linux_cdev_pager_ctor,
	.cdev_pg_dtor	= linux_cdev_pager_dtor
  },
  {
	/* OBJT_DEVICE */
	.cdev_pg_fault	= linux_cdev_pager_fault,
	.cdev_pg_ctor	= linux_cdev_pager_ctor,
	.cdev_pg_dtor	= linux_cdev_pager_dtor
  },
};

int
zap_vma_ptes(struct vm_area_struct *vma, unsigned long address,
    unsigned long size)
{
	vm_object_t obj;
	vm_page_t m;

	obj = vma->vm_obj;
	if (obj == NULL || (obj->flags & OBJ_UNMANAGED) != 0)
		return (-ENOTSUP);
	VM_OBJECT_RLOCK(obj);
	for (m = vm_page_find_least(obj, OFF_TO_IDX(address));
	    m != NULL && m->pindex < OFF_TO_IDX(address + size);
	    m = TAILQ_NEXT(m, listq))
		pmap_remove_all(m);
	VM_OBJECT_RUNLOCK(obj);
	return (0);
}

static struct file_operations dummy_ldev_ops = {
	/* XXXKIB */
};

static struct linux_cdev dummy_ldev = {
	.ops = &dummy_ldev_ops,
};

#define	LDEV_SI_DTR	0x0001
#define	LDEV_SI_REF	0x0002

static void
linux_get_fop(struct linux_file *filp, const struct file_operations **fop,
    struct linux_cdev **dev)
{
	struct linux_cdev *ldev;
	u_int siref;

	ldev = filp->f_cdev;
	*fop = filp->f_op;
	if (ldev != NULL) {
		for (siref = ldev->siref;;) {
			if ((siref & LDEV_SI_DTR) != 0) {
				ldev = &dummy_ldev;
				siref = ldev->siref;
				*fop = ldev->ops;
				MPASS((ldev->siref & LDEV_SI_DTR) == 0);
			} else if (atomic_fcmpset_int(&ldev->siref, &siref,
			    siref + LDEV_SI_REF)) {
				break;
			}
		}
	}
	*dev = ldev;
}

static void
linux_drop_fop(struct linux_cdev *ldev)
{

	if (ldev == NULL)
		return;
	MPASS((ldev->siref & ~LDEV_SI_DTR) != 0);
	atomic_subtract_int(&ldev->siref, LDEV_SI_REF);
}

#define	OPW(fp,td,code) ({			\
	struct file *__fpop;			\
	__typeof(code) __retval;		\
						\
	__fpop = (td)->td_fpop;			\
	(td)->td_fpop = (fp);			\
	__retval = (code);			\
	(td)->td_fpop = __fpop;			\
	__retval;				\
})

static int
linux_dev_fdopen(struct cdev *dev, int fflags, struct thread *td,
    struct file *file)
{
	struct linux_cdev *ldev;
	struct linux_file *filp;
	const struct file_operations *fop;
	int error;

	ldev = dev->si_drv1;

	filp = linux_file_alloc();
	filp->f_dentry = &filp->f_dentry_store;
	filp->f_op = ldev->ops;
	filp->f_mode = file->f_flag;
	filp->f_flags = file->f_flag;
	filp->f_vnode = file->f_vnode;
	filp->_file = file;
	refcount_acquire(&ldev->refs);
	filp->f_cdev = ldev;

	linux_set_current(td);
	linux_get_fop(filp, &fop, &ldev);

	if (fop->open != NULL) {
		error = -fop->open(file->f_vnode, filp);
		if (error != 0) {
			linux_drop_fop(ldev);
			linux_cdev_deref(filp->f_cdev);
			kfree(filp);
			return (error);
		}
	}

	/* hold on to the vnode - used for fstat() */
	vhold(filp->f_vnode);

	/* release the file from devfs */
	finit(file, filp->f_mode, DTYPE_DEV, filp, &linuxfileops);
	linux_drop_fop(ldev);
	return (ENXIO);
}

#define	LINUX_IOCTL_MIN_PTR 0x10000UL
#define	LINUX_IOCTL_MAX_PTR (LINUX_IOCTL_MIN_PTR + IOCPARM_MAX)

static inline int
linux_remap_address(void **uaddr, size_t len)
{
	uintptr_t uaddr_val = (uintptr_t)(*uaddr);

	if (unlikely(uaddr_val >= LINUX_IOCTL_MIN_PTR &&
	    uaddr_val < LINUX_IOCTL_MAX_PTR)) {
		struct task_struct *pts = current;
		if (pts == NULL) {
			*uaddr = NULL;
			return (1);
		}

		/* compute data offset */
		uaddr_val -= LINUX_IOCTL_MIN_PTR;

		/* check that length is within bounds */
		if ((len > IOCPARM_MAX) ||
		    (uaddr_val + len) > pts->bsd_ioctl_len) {
			*uaddr = NULL;
			return (1);
		}

		/* re-add kernel buffer address */
		uaddr_val += (uintptr_t)pts->bsd_ioctl_data;

		/* update address location */
		*uaddr = (void *)uaddr_val;
		return (1);
	}
	return (0);
}

int
linux_copyin(const void *uaddr, void *kaddr, size_t len)
{
	if (linux_remap_address(__DECONST(void **, &uaddr), len)) {
		if (uaddr == NULL)
			return (-EFAULT);
		memcpy(kaddr, uaddr, len);
		return (0);
	}
	return (-copyin(uaddr, kaddr, len));
}

int
linux_copyout(const void *kaddr, void *uaddr, size_t len)
{
	if (linux_remap_address(&uaddr, len)) {
		if (uaddr == NULL)
			return (-EFAULT);
		memcpy(uaddr, kaddr, len);
		return (0);
	}
	return (-copyout(kaddr, uaddr, len));
}

size_t
linux_clear_user(void *_uaddr, size_t _len)
{
	uint8_t *uaddr = _uaddr;
	size_t len = _len;

	/* make sure uaddr is aligned before going into the fast loop */
	while (((uintptr_t)uaddr & 7) != 0 && len > 7) {
		if (subyte(uaddr, 0))
			return (_len);
		uaddr++;
		len--;
	}

	/* zero 8 bytes at a time */
	while (len > 7) {
#ifdef __LP64__
		if (suword64(uaddr, 0))
			return (_len);
#else
		if (suword32(uaddr, 0))
			return (_len);
		if (suword32(uaddr + 4, 0))
			return (_len);
#endif
		uaddr += 8;
		len -= 8;
	}

	/* zero fill end, if any */
	while (len > 0) {
		if (subyte(uaddr, 0))
			return (_len);
		uaddr++;
		len--;
	}
	return (0);
}

int
linux_access_ok(int rw, const void *uaddr, size_t len)
{
	uintptr_t saddr;
	uintptr_t eaddr;

	/* get start and end address */
	saddr = (uintptr_t)uaddr;
	eaddr = (uintptr_t)uaddr + len;

	/* verify addresses are valid for userspace */
	return ((saddr == eaddr) ||
	    (eaddr > saddr && eaddr <= VM_MAXUSER_ADDRESS));
}

/*
 * This function should return either EINTR or ERESTART depending on
 * the signal type sent to this thread:
 */
static int
linux_get_error(struct task_struct *task, int error)
{
	/* check for signal type interrupt code */
	if (error == EINTR || error == ERESTARTSYS || error == ERESTART) {
		error = -linux_schedule_get_interrupt_value(task);
		if (error == 0)
			error = EINTR;
	}
	return (error);
}

static int
linux_file_ioctl_sub(struct file *fp, struct linux_file *filp,
    const struct file_operations *fop, u_long cmd, caddr_t data,
    struct thread *td)
{
	struct task_struct *task = current;
	unsigned size;
	int error;

	size = IOCPARM_LEN(cmd);
	/* refer to logic in sys_ioctl() */
	if (size > 0) {
		/*
		 * Setup hint for linux_copyin() and linux_copyout().
		 *
		 * Background: Linux code expects a user-space address
		 * while FreeBSD supplies a kernel-space address.
		 */
		task->bsd_ioctl_data = data;
		task->bsd_ioctl_len = size;
		data = (void *)LINUX_IOCTL_MIN_PTR;
	} else {
		/* fetch user-space pointer */
		data = *(void **)data;
	}
#if defined(__amd64__)
	if (td->td_proc->p_elf_machine == EM_386) {
		/* try the compat IOCTL handler first */
		if (fop->compat_ioctl != NULL) {
			error = -OPW(fp, td, fop->compat_ioctl(filp,
			    cmd, (u_long)data));
		} else {
			error = ENOTTY;
		}

		/* fallback to the regular IOCTL handler, if any */
		if (error == ENOTTY && fop->unlocked_ioctl != NULL) {
			error = -OPW(fp, td, fop->unlocked_ioctl(filp,
			    cmd, (u_long)data));
		}
	} else
#endif
	{
		if (fop->unlocked_ioctl != NULL) {
			error = -OPW(fp, td, fop->unlocked_ioctl(filp,
			    cmd, (u_long)data));
		} else {
			error = ENOTTY;
		}
	}
	if (size > 0) {
		task->bsd_ioctl_data = NULL;
		task->bsd_ioctl_len = 0;
	}

	if (error == EWOULDBLOCK) {
		/* update kqfilter status, if any */
		linux_file_kqfilter_poll(filp,
		    LINUX_KQ_FLAG_HAS_READ | LINUX_KQ_FLAG_HAS_WRITE);
	} else {
		error = linux_get_error(task, error);
	}
	return (error);
}

#define	LINUX_POLL_TABLE_NORMAL ((poll_table *)1)

/*
 * This function atomically updates the poll wakeup state and returns
 * the previous state at the time of update.
 */
static uint8_t
linux_poll_wakeup_state(atomic_t *v, const uint8_t *pstate)
{
	int c, old;

	c = v->counter;

	while ((old = atomic_cmpxchg(v, c, pstate[c])) != c)
		c = old;

	return (c);
}


static int
linux_poll_wakeup_callback(wait_queue_t *wq, unsigned int wq_state, int flags, void *key)
{
	static const uint8_t state[LINUX_FWQ_STATE_MAX] = {
		[LINUX_FWQ_STATE_INIT] = LINUX_FWQ_STATE_INIT, /* NOP */
		[LINUX_FWQ_STATE_NOT_READY] = LINUX_FWQ_STATE_NOT_READY, /* NOP */
		[LINUX_FWQ_STATE_QUEUED] = LINUX_FWQ_STATE_READY,
		[LINUX_FWQ_STATE_READY] = LINUX_FWQ_STATE_READY, /* NOP */
	};
	struct linux_file *filp = container_of(wq, struct linux_file, f_wait_queue.wq);

	switch (linux_poll_wakeup_state(&filp->f_wait_queue.state, state)) {
	case LINUX_FWQ_STATE_QUEUED:
		linux_poll_wakeup(filp);
		return (1);
	default:
		return (0);
	}
}

void
linux_poll_wait(struct linux_file *filp, wait_queue_head_t *wqh, poll_table *p)
{
	static const uint8_t state[LINUX_FWQ_STATE_MAX] = {
		[LINUX_FWQ_STATE_INIT] = LINUX_FWQ_STATE_NOT_READY,
		[LINUX_FWQ_STATE_NOT_READY] = LINUX_FWQ_STATE_NOT_READY, /* NOP */
		[LINUX_FWQ_STATE_QUEUED] = LINUX_FWQ_STATE_QUEUED, /* NOP */
		[LINUX_FWQ_STATE_READY] = LINUX_FWQ_STATE_QUEUED,
	};

	/* check if we are called inside the select system call */
	if (p == LINUX_POLL_TABLE_NORMAL)
		selrecord(curthread, &filp->f_selinfo);

	switch (linux_poll_wakeup_state(&filp->f_wait_queue.state, state)) {
	case LINUX_FWQ_STATE_INIT:
		/* NOTE: file handles can only belong to one wait-queue */
		filp->f_wait_queue.wqh = wqh;
		filp->f_wait_queue.wq.func = &linux_poll_wakeup_callback;
		add_wait_queue(wqh, &filp->f_wait_queue.wq);
		atomic_set(&filp->f_wait_queue.state, LINUX_FWQ_STATE_QUEUED);
		break;
	default:
		break;
	}
}

static void
linux_poll_wait_dequeue(struct linux_file *filp)
{
	static const uint8_t state[LINUX_FWQ_STATE_MAX] = {
		[LINUX_FWQ_STATE_INIT] = LINUX_FWQ_STATE_INIT,	/* NOP */
		[LINUX_FWQ_STATE_NOT_READY] = LINUX_FWQ_STATE_INIT,
		[LINUX_FWQ_STATE_QUEUED] = LINUX_FWQ_STATE_INIT,
		[LINUX_FWQ_STATE_READY] = LINUX_FWQ_STATE_INIT,
	};

	seldrain(&filp->f_selinfo);

	switch (linux_poll_wakeup_state(&filp->f_wait_queue.state, state)) {
	case LINUX_FWQ_STATE_NOT_READY:
	case LINUX_FWQ_STATE_QUEUED:
	case LINUX_FWQ_STATE_READY:
		remove_wait_queue(filp->f_wait_queue.wqh, &filp->f_wait_queue.wq);
		break;
	default:
		break;
	}
}

void
linux_poll_wakeup(struct linux_file *filp)
{
	/* this function should be NULL-safe */
	if (filp == NULL)
		return;

	selwakeup(&filp->f_selinfo);

	spin_lock(&filp->f_kqlock);
	filp->f_kqflags |= LINUX_KQ_FLAG_NEED_READ |
	    LINUX_KQ_FLAG_NEED_WRITE;

	/* make sure the "knote" gets woken up */
	KNOTE_LOCKED(&filp->f_selinfo.si_note, 1);
	spin_unlock(&filp->f_kqlock);
}

static void
linux_file_kqfilter_detach(struct knote *kn)
{
	struct linux_file *filp = kn->kn_hook;

	spin_lock(&filp->f_kqlock);
	knlist_remove(&filp->f_selinfo.si_note, kn, 1);
	spin_unlock(&filp->f_kqlock);
}

static int
linux_file_kqfilter_read_event(struct knote *kn, long hint)
{
	struct linux_file *filp = kn->kn_hook;

	mtx_assert(&filp->f_kqlock.m, MA_OWNED);

	return ((filp->f_kqflags & LINUX_KQ_FLAG_NEED_READ) ? 1 : 0);
}

static int
linux_file_kqfilter_write_event(struct knote *kn, long hint)
{
	struct linux_file *filp = kn->kn_hook;

	mtx_assert(&filp->f_kqlock.m, MA_OWNED);

	return ((filp->f_kqflags & LINUX_KQ_FLAG_NEED_WRITE) ? 1 : 0);
}

static struct filterops linux_dev_kqfiltops_read = {
	.f_isfd = 1,
	.f_detach = linux_file_kqfilter_detach,
	.f_event = linux_file_kqfilter_read_event,
};

static struct filterops linux_dev_kqfiltops_write = {
	.f_isfd = 1,
	.f_detach = linux_file_kqfilter_detach,
	.f_event = linux_file_kqfilter_write_event,
};

static void
linux_file_kqfilter_poll(struct linux_file *filp, int kqflags)
{
	struct thread *td;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	int temp;

	if ((filp->f_kqflags & kqflags) == 0)
		return;

	td = curthread;

	linux_get_fop(filp, &fop, &ldev);
	/* get the latest polling state */
	temp = OPW(filp->_file, td, fop->poll(filp, NULL));
	linux_drop_fop(ldev);

	spin_lock(&filp->f_kqlock);
	/* clear kqflags */
	filp->f_kqflags &= ~(LINUX_KQ_FLAG_NEED_READ |
	    LINUX_KQ_FLAG_NEED_WRITE);
	/* update kqflags */
	if ((temp & (POLLIN | POLLOUT)) != 0) {
		if ((temp & POLLIN) != 0)
			filp->f_kqflags |= LINUX_KQ_FLAG_NEED_READ;
		if ((temp & POLLOUT) != 0)
			filp->f_kqflags |= LINUX_KQ_FLAG_NEED_WRITE;

		/* make sure the "knote" gets woken up */
		KNOTE_LOCKED(&filp->f_selinfo.si_note, 0);
	}
	spin_unlock(&filp->f_kqlock);
}

static int
linux_file_kqfilter(struct file *file, struct knote *kn)
{
	struct linux_file *filp;
	struct thread *td;
	int error;

	td = curthread;
	filp = (struct linux_file *)file->f_data;
	filp->f_flags = file->f_flag;
	if (filp->f_op->poll == NULL)
		return (EINVAL);

	spin_lock(&filp->f_kqlock);
	switch (kn->kn_filter) {
	case EVFILT_READ:
		filp->f_kqflags |= LINUX_KQ_FLAG_HAS_READ;
		kn->kn_fop = &linux_dev_kqfiltops_read;
		kn->kn_hook = filp;
		knlist_add(&filp->f_selinfo.si_note, kn, 1);
		error = 0;
		break;
	case EVFILT_WRITE:
		filp->f_kqflags |= LINUX_KQ_FLAG_HAS_WRITE;
		kn->kn_fop = &linux_dev_kqfiltops_write;
		kn->kn_hook = filp;
		knlist_add(&filp->f_selinfo.si_note, kn, 1);
		error = 0;
		break;
	default:
		error = EINVAL;
		break;
	}
	spin_unlock(&filp->f_kqlock);

	if (error == 0) {
		linux_set_current(td);

		/* update kqfilter status, if any */
		linux_file_kqfilter_poll(filp,
		    LINUX_KQ_FLAG_HAS_READ | LINUX_KQ_FLAG_HAS_WRITE);
	}
	return (error);
}

static int
linux_file_mmap_single(struct file *fp, const struct file_operations *fop,
    vm_ooffset_t *offset, vm_size_t size, struct vm_object **object,
    int nprot, struct thread *td)
{
	struct task_struct *task;
	struct vm_area_struct *vmap;
	struct mm_struct *mm;
	struct linux_file *filp;
	vm_memattr_t attr;
	int error;

	filp = (struct linux_file *)fp->f_data;
	filp->f_flags = fp->f_flag;

	if (fop->mmap == NULL)
		return (EOPNOTSUPP);

	linux_set_current(td);

	/*
	 * The same VM object might be shared by multiple processes
	 * and the mm_struct is usually freed when a process exits.
	 *
	 * The atomic reference below makes sure the mm_struct is
	 * available as long as the vmap is in the linux_vma_head.
	 */
	task = current;
	mm = task->mm;
	if (atomic_inc_not_zero(&mm->mm_users) == 0)
		return (EINVAL);

	vmap = kzalloc(sizeof(*vmap), GFP_KERNEL);
	vmap->vm_start = 0;
	vmap->vm_end = size;
	vmap->vm_pgoff = *offset / PAGE_SIZE;
	vmap->vm_pfn = 0;
	vmap->vm_flags = vmap->vm_page_prot = (nprot & VM_PROT_ALL);
	vmap->vm_ops = NULL;
	vmap->vm_file = get_file(filp);
	vmap->vm_mm = mm;

	if (unlikely(down_write_killable(&vmap->vm_mm->mmap_sem))) {
		error = linux_get_error(task, EINTR);
	} else {
		error = -OPW(fp, td, fop->mmap(filp, vmap));
		error = linux_get_error(task, error);
		up_write(&vmap->vm_mm->mmap_sem);
	}

	if (error != 0) {
		linux_cdev_handle_free(vmap);
		return (error);
	}

	attr = pgprot2cachemode(vmap->vm_page_prot);

	if (vmap->vm_ops != NULL) {
		struct vm_area_struct *ptr;
		void *vm_private_data;
		bool vm_no_fault;

		if (vmap->vm_ops->open == NULL ||
		    vmap->vm_ops->close == NULL ||
		    vmap->vm_private_data == NULL) {
			/* free allocated VM area struct */
			linux_cdev_handle_free(vmap);
			return (EINVAL);
		}

		vm_private_data = vmap->vm_private_data;

		rw_wlock(&linux_vma_lock);
		TAILQ_FOREACH(ptr, &linux_vma_head, vm_entry) {
			if (ptr->vm_private_data == vm_private_data)
				break;
		}
		/* check if there is an existing VM area struct */
		if (ptr != NULL) {
			/* check if the VM area structure is invalid */
			if (ptr->vm_ops == NULL ||
			    ptr->vm_ops->open == NULL ||
			    ptr->vm_ops->close == NULL) {
				error = ESTALE;
				vm_no_fault = 1;
			} else {
				error = EEXIST;
				vm_no_fault = (ptr->vm_ops->fault == NULL);
			}
		} else {
			/* insert VM area structure into list */
			TAILQ_INSERT_TAIL(&linux_vma_head, vmap, vm_entry);
			error = 0;
			vm_no_fault = (vmap->vm_ops->fault == NULL);
		}
		rw_wunlock(&linux_vma_lock);

		if (error != 0) {
			/* free allocated VM area struct */
			linux_cdev_handle_free(vmap);
			/* check for stale VM area struct */
			if (error != EEXIST)
				return (error);
		}

		/* check if there is no fault handler */
		if (vm_no_fault) {
			*object = cdev_pager_allocate(vm_private_data, OBJT_DEVICE,
			    &linux_cdev_pager_ops[1], size, nprot, *offset,
			    td->td_ucred);
		} else {
			*object = cdev_pager_allocate(vm_private_data, OBJT_MGTDEVICE,
			    &linux_cdev_pager_ops[0], size, nprot, *offset,
			    td->td_ucred);
		}

		/* check if allocating the VM object failed */
		if (*object == NULL) {
			if (error == 0) {
				/* remove VM area struct from list */
				linux_cdev_handle_remove(vmap);
				/* free allocated VM area struct */
				linux_cdev_handle_free(vmap);
			}
			return (EINVAL);
		}
	} else {
		struct sglist *sg;

		sg = sglist_alloc(1, M_WAITOK);
		sglist_append_phys(sg,
		    (vm_paddr_t)vmap->vm_pfn << PAGE_SHIFT, vmap->vm_len);

		*object = vm_pager_allocate(OBJT_SG, sg, vmap->vm_len,
		    nprot, 0, td->td_ucred);

		linux_cdev_handle_free(vmap);

		if (*object == NULL) {
			sglist_free(sg);
			return (EINVAL);
		}
	}

	if (attr != VM_MEMATTR_DEFAULT) {
		VM_OBJECT_WLOCK(*object);
		vm_object_set_memattr(*object, attr);
		VM_OBJECT_WUNLOCK(*object);
	}
	*offset = 0;
	return (0);
}

struct cdevsw linuxcdevsw = {
	.d_version = D_VERSION,
	.d_fdopen = linux_dev_fdopen,
	.d_name = "lkpidev",
};

static int
linux_file_read(struct file *file, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{
	struct linux_file *filp;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	ssize_t bytes;
	int error;

	error = 0;
	filp = (struct linux_file *)file->f_data;
	filp->f_flags = file->f_flag;
	/* XXX no support for I/O vectors currently */
	if (uio->uio_iovcnt != 1)
		return (EOPNOTSUPP);
	if (uio->uio_resid > DEVFS_IOSIZE_MAX)
		return (EINVAL);
	linux_set_current(td);
	linux_get_fop(filp, &fop, &ldev);
	if (fop->read != NULL) {
		bytes = OPW(file, td, fop->read(filp,
		    uio->uio_iov->iov_base,
		    uio->uio_iov->iov_len, &uio->uio_offset));
		if (bytes >= 0) {
			uio->uio_iov->iov_base =
			    ((uint8_t *)uio->uio_iov->iov_base) + bytes;
			uio->uio_iov->iov_len -= bytes;
			uio->uio_resid -= bytes;
		} else {
			error = linux_get_error(current, -bytes);
		}
	} else
		error = ENXIO;

	/* update kqfilter status, if any */
	linux_file_kqfilter_poll(filp, LINUX_KQ_FLAG_HAS_READ);
	linux_drop_fop(ldev);

	return (error);
}

static int
linux_file_write(struct file *file, struct uio *uio, struct ucred *active_cred,
    int flags, struct thread *td)
{
	struct linux_file *filp;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	ssize_t bytes;
	int error;

	filp = (struct linux_file *)file->f_data;
	filp->f_flags = file->f_flag;
	/* XXX no support for I/O vectors currently */
	if (uio->uio_iovcnt != 1)
		return (EOPNOTSUPP);
	if (uio->uio_resid > DEVFS_IOSIZE_MAX)
		return (EINVAL);
	linux_set_current(td);
	linux_get_fop(filp, &fop, &ldev);
	if (fop->write != NULL) {
		bytes = OPW(file, td, fop->write(filp,
		    uio->uio_iov->iov_base,
		    uio->uio_iov->iov_len, &uio->uio_offset));
		if (bytes >= 0) {
			uio->uio_iov->iov_base =
			    ((uint8_t *)uio->uio_iov->iov_base) + bytes;
			uio->uio_iov->iov_len -= bytes;
			uio->uio_resid -= bytes;
			error = 0;
		} else {
			error = linux_get_error(current, -bytes);
		}
	} else
		error = ENXIO;

	/* update kqfilter status, if any */
	linux_file_kqfilter_poll(filp, LINUX_KQ_FLAG_HAS_WRITE);

	linux_drop_fop(ldev);

	return (error);
}

static int
linux_file_poll(struct file *file, int events, struct ucred *active_cred,
    struct thread *td)
{
	struct linux_file *filp;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	int revents;

	filp = (struct linux_file *)file->f_data;
	filp->f_flags = file->f_flag;
	linux_set_current(td);
	linux_get_fop(filp, &fop, &ldev);
	if (fop->poll != NULL) {
		revents = OPW(file, td, fop->poll(filp,
		    LINUX_POLL_TABLE_NORMAL)) & events;
	} else {
		revents = 0;
	}
	linux_drop_fop(ldev);
	return (revents);
}

static int
linux_file_close(struct file *file, struct thread *td)
{
	struct linux_file *filp;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	int error;

	filp = (struct linux_file *)file->f_data;

	KASSERT(file_count(filp) == 0,
	    ("File refcount(%d) is not zero", file_count(filp)));

	error = 0;
	filp->f_flags = file->f_flag;
	linux_set_current(td);
	linux_poll_wait_dequeue(filp);
	linux_get_fop(filp, &fop, &ldev);
	if (fop->release != NULL)
		error = -OPW(file, td, fop->release(filp->f_vnode, filp));
	funsetown(&filp->f_sigio);
	if (filp->f_vnode != NULL)
		vdrop(filp->f_vnode);
	linux_drop_fop(ldev);
	if (filp->f_cdev != NULL)
		linux_cdev_deref(filp->f_cdev);
	kfree(filp);

	return (error);
}

static int
linux_file_ioctl(struct file *fp, u_long cmd, void *data, struct ucred *cred,
    struct thread *td)
{
	struct linux_file *filp;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	int error;

	error = 0;
	filp = (struct linux_file *)fp->f_data;
	filp->f_flags = fp->f_flag;
	linux_get_fop(filp, &fop, &ldev);

	linux_set_current(td);
	switch (cmd) {
	case FIONBIO:
		break;
	case FIOASYNC:
		if (fop->fasync == NULL)
			break;
		error = -OPW(fp, td, fop->fasync(0, filp, fp->f_flag & FASYNC));
		break;
	case FIOSETOWN:
		error = fsetown(*(int *)data, &filp->f_sigio);
		if (error == 0) {
			if (fop->fasync == NULL)
				break;
			error = -OPW(fp, td, fop->fasync(0, filp,
			    fp->f_flag & FASYNC));
		}
		break;
	case FIOGETOWN:
		*(int *)data = fgetown(&filp->f_sigio);
		break;
	default:
		error = linux_file_ioctl_sub(fp, filp, fop, cmd, data, td);
		break;
	}
	linux_drop_fop(ldev);
	return (error);
}

static int
linux_file_mmap_sub(struct thread *td, vm_size_t objsize, vm_prot_t prot,
    vm_prot_t *maxprotp, int *flagsp, struct file *fp,
    vm_ooffset_t *foff, const struct file_operations *fop, vm_object_t *objp)
{
	/*
	 * Character devices do not provide private mappings
	 * of any kind:
	 */
	if ((*maxprotp & VM_PROT_WRITE) == 0 &&
	    (prot & VM_PROT_WRITE) != 0)
		return (EACCES);
	if ((*flagsp & (MAP_PRIVATE | MAP_COPY)) != 0)
		return (EINVAL);

	return (linux_file_mmap_single(fp, fop, foff, objsize, objp,
	    (int)prot, td));
}

static int
linux_file_mmap(struct file *fp, vm_map_t map, vm_offset_t *addr, vm_size_t size,
    vm_prot_t prot, vm_prot_t cap_maxprot, int flags, vm_ooffset_t foff,
    struct thread *td)
{
	struct linux_file *filp;
	const struct file_operations *fop;
	struct linux_cdev *ldev;
	struct mount *mp;
	struct vnode *vp;
	vm_object_t object;
	vm_prot_t maxprot;
	int error;

	filp = (struct linux_file *)fp->f_data;

	vp = filp->f_vnode;
	if (vp == NULL)
		return (EOPNOTSUPP);

	/*
	 * Ensure that file and memory protections are
	 * compatible.
	 */
	mp = vp->v_mount;
	if (mp != NULL && (mp->mnt_flag & MNT_NOEXEC) != 0) {
		maxprot = VM_PROT_NONE;
		if ((prot & VM_PROT_EXECUTE) != 0)
			return (EACCES);
	} else
		maxprot = VM_PROT_EXECUTE;
	if ((fp->f_flag & FREAD) != 0)
		maxprot |= VM_PROT_READ;
	else if ((prot & VM_PROT_READ) != 0)
		return (EACCES);

	/*
	 * If we are sharing potential changes via MAP_SHARED and we
	 * are trying to get write permission although we opened it
	 * without asking for it, bail out.
	 *
	 * Note that most character devices always share mappings.
	 *
	 * Rely on linux_file_mmap_sub() to fail invalid MAP_PRIVATE
	 * requests rather than doing it here.
	 */
	if ((flags & MAP_SHARED) != 0) {
		if ((fp->f_flag & FWRITE) != 0)
			maxprot |= VM_PROT_WRITE;
		else if ((prot & VM_PROT_WRITE) != 0)
			return (EACCES);
	}
	maxprot &= cap_maxprot;

	linux_get_fop(filp, &fop, &ldev);
	error = linux_file_mmap_sub(td, size, prot, &maxprot, &flags, fp,
	    &foff, fop, &object);
	if (error != 0)
		goto out;

	error = vm_mmap_object(map, addr, size, prot, maxprot, flags, object,
	    foff, FALSE, td);
	if (error != 0)
		vm_object_deallocate(object);
out:
	linux_drop_fop(ldev);
	return (error);
}

static int
linux_file_stat(struct file *fp, struct stat *sb, struct ucred *active_cred,
    struct thread *td)
{
	struct linux_file *filp;
	struct vnode *vp;
	int error;

	filp = (struct linux_file *)fp->f_data;
	if (filp->f_vnode == NULL)
		return (EOPNOTSUPP);

	vp = filp->f_vnode;

	vn_lock(vp, LK_SHARED | LK_RETRY);
	error = vn_stat(vp, sb, td->td_ucred, NOCRED, td);
	VOP_UNLOCK(vp, 0);

	return (error);
}

static int
linux_file_fill_kinfo(struct file *fp, struct kinfo_file *kif,
    struct filedesc *fdp)
{
	struct linux_file *filp;
	struct vnode *vp;
	int error;

	filp = fp->f_data;
	vp = filp->f_vnode;
	if (vp == NULL) {
		error = 0;
		kif->kf_type = KF_TYPE_DEV;
	} else {
		vref(vp);
		FILEDESC_SUNLOCK(fdp);
		error = vn_fill_kinfo_vnode(vp, kif);
		vrele(vp);
		kif->kf_type = KF_TYPE_VNODE;
		FILEDESC_SLOCK(fdp);
	}
	return (error);
}

unsigned int
linux_iminor(struct inode *inode)
{
	struct linux_cdev *ldev;

	if (inode == NULL || inode->v_rdev == NULL ||
	    inode->v_rdev->si_devsw != &linuxcdevsw)
		return (-1U);
	ldev = inode->v_rdev->si_drv1;
	if (ldev == NULL)
		return (-1U);

	return (minor(ldev->dev));
}

struct fileops linuxfileops = {
	.fo_read = linux_file_read,
	.fo_write = linux_file_write,
	.fo_truncate = invfo_truncate,
	.fo_kqfilter = linux_file_kqfilter,
	.fo_stat = linux_file_stat,
	.fo_fill_kinfo = linux_file_fill_kinfo,
	.fo_poll = linux_file_poll,
	.fo_close = linux_file_close,
	.fo_ioctl = linux_file_ioctl,
	.fo_mmap = linux_file_mmap,
	.fo_chmod = invfo_chmod,
	.fo_chown = invfo_chown,
	.fo_sendfile = invfo_sendfile,
	.fo_flags = DFLAG_PASSABLE,
};

/*
 * Hash of vmmap addresses.  This is infrequently accessed and does not
 * need to be particularly large.  This is done because we must store the
 * caller's idea of the map size to properly unmap.
 */
struct vmmap {
	LIST_ENTRY(vmmap)	vm_next;
	void 			*vm_addr;
	unsigned long		vm_size;
};

struct vmmaphd {
	struct vmmap *lh_first;
};
#define	VMMAP_HASH_SIZE	64
#define	VMMAP_HASH_MASK	(VMMAP_HASH_SIZE - 1)
#define	VM_HASH(addr)	((uintptr_t)(addr) >> PAGE_SHIFT) & VMMAP_HASH_MASK
static struct vmmaphd vmmaphead[VMMAP_HASH_SIZE];
static struct mtx vmmaplock;

static void
vmmap_add(void *addr, unsigned long size)
{
	struct vmmap *vmmap;

	vmmap = kmalloc(sizeof(*vmmap), GFP_KERNEL);
	mtx_lock(&vmmaplock);
	vmmap->vm_size = size;
	vmmap->vm_addr = addr;
	LIST_INSERT_HEAD(&vmmaphead[VM_HASH(addr)], vmmap, vm_next);
	mtx_unlock(&vmmaplock);
}

static struct vmmap *
vmmap_remove(void *addr)
{
	struct vmmap *vmmap;

	mtx_lock(&vmmaplock);
	LIST_FOREACH(vmmap, &vmmaphead[VM_HASH(addr)], vm_next)
		if (vmmap->vm_addr == addr)
			break;
	if (vmmap)
		LIST_REMOVE(vmmap, vm_next);
	mtx_unlock(&vmmaplock);

	return (vmmap);
}

#if defined(__i386__) || defined(__amd64__) || defined(__powerpc__)
void *
_ioremap_attr(vm_paddr_t phys_addr, unsigned long size, int attr)
{
	void *addr;

	addr = pmap_mapdev_attr(phys_addr, size, attr);
	if (addr == NULL)
		return (NULL);
	vmmap_add(addr, size);

	return (addr);
}
#endif

void
iounmap(void *addr)
{
	struct vmmap *vmmap;

	vmmap = vmmap_remove(addr);
	if (vmmap == NULL)
		return;
#if defined(__i386__) || defined(__amd64__) || defined(__powerpc__)
	pmap_unmapdev((vm_offset_t)addr, vmmap->vm_size);
#endif
	kfree(vmmap);
}


void *
vmap(struct page **pages, unsigned int count, unsigned long flags, int prot)
{
	vm_offset_t off;
	size_t size;

	size = count * PAGE_SIZE;
	off = kva_alloc(size);
	if (off == 0)
		return (NULL);
	vmmap_add((void *)off, size);
	pmap_qenter(off, pages, count);

	return ((void *)off);
}

void
vunmap(void *addr)
{
	struct vmmap *vmmap;

	vmmap = vmmap_remove(addr);
	if (vmmap == NULL)
		return;
	pmap_qremove((vm_offset_t)addr, vmmap->vm_size / PAGE_SIZE);
	kva_free((vm_offset_t)addr, vmmap->vm_size);
	kfree(vmmap);
}

char *
kvasprintf(gfp_t gfp, const char *fmt, va_list ap)
{
	unsigned int len;
	char *p;
	va_list aq;

	va_copy(aq, ap);
	len = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);

	p = kmalloc(len + 1, gfp);
	if (p != NULL)
		vsnprintf(p, len + 1, fmt, ap);

	return (p);
}

char *
kasprintf(gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = kvasprintf(gfp, fmt, ap);
	va_end(ap);

	return (p);
}

static void
linux_timer_callback_wrapper(void *context)
{
	struct timer_list *timer;

	linux_set_current(curthread);

	timer = context;
	timer->function(timer->data);
}

void
mod_timer(struct timer_list *timer, int expires)
{

	timer->expires = expires;
	callout_reset(&timer->callout,
	    linux_timer_jiffies_until(expires),
	    &linux_timer_callback_wrapper, timer);
}

void
add_timer(struct timer_list *timer)
{

	callout_reset(&timer->callout,
	    linux_timer_jiffies_until(timer->expires),
	    &linux_timer_callback_wrapper, timer);
}

void
add_timer_on(struct timer_list *timer, int cpu)
{

	callout_reset_on(&timer->callout,
	    linux_timer_jiffies_until(timer->expires),
	    &linux_timer_callback_wrapper, timer, cpu);
}

static void
linux_timer_init(void *arg)
{

	/*
	 * Compute an internal HZ value which can divide 2**32 to
	 * avoid timer rounding problems when the tick value wraps
	 * around 2**32:
	 */
	linux_timer_hz_mask = 1;
	while (linux_timer_hz_mask < (unsigned long)hz)
		linux_timer_hz_mask *= 2;
	linux_timer_hz_mask--;
}
SYSINIT(linux_timer, SI_SUB_DRIVERS, SI_ORDER_FIRST, linux_timer_init, NULL);

void
linux_complete_common(struct completion *c, int all)
{
	int wakeup_swapper;

	sleepq_lock(c);
	if (all) {
		c->done = UINT_MAX;
		wakeup_swapper = sleepq_broadcast(c, SLEEPQ_SLEEP, 0, 0);
	} else {
		if (c->done != UINT_MAX)
			c->done++;
		wakeup_swapper = sleepq_signal(c, SLEEPQ_SLEEP, 0, 0);
	}
	sleepq_release(c);
	if (wakeup_swapper)
		kick_proc0();
}

/*
 * Indefinite wait for done != 0 with or without signals.
 */
int
linux_wait_for_common(struct completion *c, int flags)
{
	struct task_struct *task;
	int error;

	if (SCHEDULER_STOPPED())
		return (0);

	task = current;

	if (flags != 0)
		flags = SLEEPQ_INTERRUPTIBLE | SLEEPQ_SLEEP;
	else
		flags = SLEEPQ_SLEEP;
	error = 0;
	for (;;) {
		sleepq_lock(c);
		if (c->done)
			break;
		sleepq_add(c, NULL, "completion", flags, 0);
		if (flags & SLEEPQ_INTERRUPTIBLE) {
			DROP_GIANT();
			error = -sleepq_wait_sig(c, 0);
			PICKUP_GIANT();
			if (error != 0) {
				linux_schedule_save_interrupt_value(task, error);
				error = -ERESTARTSYS;
				goto intr;
			}
		} else {
			DROP_GIANT();
			sleepq_wait(c, 0);
			PICKUP_GIANT();
		}
	}
	if (c->done != UINT_MAX)
		c->done--;
	sleepq_release(c);

intr:
	return (error);
}

/*
 * Time limited wait for done != 0 with or without signals.
 */
int
linux_wait_for_timeout_common(struct completion *c, int timeout, int flags)
{
	struct task_struct *task;
	int end = jiffies + timeout;
	int error;

	if (SCHEDULER_STOPPED())
		return (0);

	task = current;

	if (flags != 0)
		flags = SLEEPQ_INTERRUPTIBLE | SLEEPQ_SLEEP;
	else
		flags = SLEEPQ_SLEEP;

	for (;;) {
		sleepq_lock(c);
		if (c->done)
			break;
		sleepq_add(c, NULL, "completion", flags, 0);
		sleepq_set_timeout(c, linux_timer_jiffies_until(end));

		DROP_GIANT();
		if (flags & SLEEPQ_INTERRUPTIBLE)
			error = -sleepq_timedwait_sig(c, 0);
		else
			error = -sleepq_timedwait(c, 0);
		PICKUP_GIANT();

		if (error != 0) {
			/* check for timeout */
			if (error == -EWOULDBLOCK) {
				error = 0;	/* timeout */
			} else {
				/* signal happened */
				linux_schedule_save_interrupt_value(task, error);
				error = -ERESTARTSYS;
			}
			goto done;
		}
	}
	if (c->done != UINT_MAX)
		c->done--;
	sleepq_release(c);

	/* return how many jiffies are left */
	error = linux_timer_jiffies_until(end);
done:
	return (error);
}

int
linux_try_wait_for_completion(struct completion *c)
{
	int isdone;

	sleepq_lock(c);
	isdone = (c->done != 0);
	if (c->done != 0 && c->done != UINT_MAX)
		c->done--;
	sleepq_release(c);
	return (isdone);
}

int
linux_completion_done(struct completion *c)
{
	int isdone;

	sleepq_lock(c);
	isdone = (c->done != 0);
	sleepq_release(c);
	return (isdone);
}

static void
linux_cdev_deref(struct linux_cdev *ldev)
{

	if (refcount_release(&ldev->refs))
		kfree(ldev);
}

static void
linux_cdev_release(struct kobject *kobj)
{
	struct linux_cdev *cdev;
	struct kobject *parent;

	cdev = container_of(kobj, struct linux_cdev, kobj);
	parent = kobj->parent;
	linux_destroy_dev(cdev);
	linux_cdev_deref(cdev);
	kobject_put(parent);
}

static void
linux_cdev_static_release(struct kobject *kobj)
{
	struct linux_cdev *cdev;
	struct kobject *parent;

	cdev = container_of(kobj, struct linux_cdev, kobj);
	parent = kobj->parent;
	linux_destroy_dev(cdev);
	kobject_put(parent);
}

void
linux_destroy_dev(struct linux_cdev *ldev)
{

	if (ldev->cdev == NULL)
		return;

	MPASS((ldev->siref & LDEV_SI_DTR) == 0);
	atomic_set_int(&ldev->siref, LDEV_SI_DTR);
	while ((atomic_load_int(&ldev->siref) & ~LDEV_SI_DTR) != 0)
		pause("ldevdtr", hz / 4);

	destroy_dev(ldev->cdev);
	ldev->cdev = NULL;
}

const struct kobj_type linux_cdev_ktype = {
	.release = linux_cdev_release,
};

const struct kobj_type linux_cdev_static_ktype = {
	.release = linux_cdev_static_release,
};

static void
linux_handle_ifnet_link_event(void *arg, struct ifnet *ifp, int linkstate)
{
	struct notifier_block *nb;

	nb = arg;
	if (linkstate == LINK_STATE_UP)
		nb->notifier_call(nb, NETDEV_UP, ifp);
	else
		nb->notifier_call(nb, NETDEV_DOWN, ifp);
}

static void
linux_handle_ifnet_arrival_event(void *arg, struct ifnet *ifp)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETDEV_REGISTER, ifp);
}

static void
linux_handle_ifnet_departure_event(void *arg, struct ifnet *ifp)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETDEV_UNREGISTER, ifp);
}

static void
linux_handle_iflladdr_event(void *arg, struct ifnet *ifp)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETDEV_CHANGEADDR, ifp);
}

static void
linux_handle_ifaddr_event(void *arg, struct ifnet *ifp)
{
	struct notifier_block *nb;

	nb = arg;
	nb->notifier_call(nb, NETDEV_CHANGEIFADDR, ifp);
}

int
register_netdevice_notifier(struct notifier_block *nb)
{

	nb->tags[NETDEV_UP] = EVENTHANDLER_REGISTER(
	    ifnet_link_event, linux_handle_ifnet_link_event, nb, 0);
	nb->tags[NETDEV_REGISTER] = EVENTHANDLER_REGISTER(
	    ifnet_arrival_event, linux_handle_ifnet_arrival_event, nb, 0);
	nb->tags[NETDEV_UNREGISTER] = EVENTHANDLER_REGISTER(
	    ifnet_departure_event, linux_handle_ifnet_departure_event, nb, 0);
	nb->tags[NETDEV_CHANGEADDR] = EVENTHANDLER_REGISTER(
	    iflladdr_event, linux_handle_iflladdr_event, nb, 0);

	return (0);
}

int
register_inetaddr_notifier(struct notifier_block *nb)
{

	nb->tags[NETDEV_CHANGEIFADDR] = EVENTHANDLER_REGISTER(
	    ifaddr_event, linux_handle_ifaddr_event, nb, 0);
	return (0);
}

int
unregister_netdevice_notifier(struct notifier_block *nb)
{

	EVENTHANDLER_DEREGISTER(ifnet_link_event,
	    nb->tags[NETDEV_UP]);
	EVENTHANDLER_DEREGISTER(ifnet_arrival_event,
	    nb->tags[NETDEV_REGISTER]);
	EVENTHANDLER_DEREGISTER(ifnet_departure_event,
	    nb->tags[NETDEV_UNREGISTER]);
	EVENTHANDLER_DEREGISTER(iflladdr_event,
	    nb->tags[NETDEV_CHANGEADDR]);

	return (0);
}

int
unregister_inetaddr_notifier(struct notifier_block *nb)
{

	EVENTHANDLER_DEREGISTER(ifaddr_event,
	    nb->tags[NETDEV_CHANGEIFADDR]);

	return (0);
}

struct list_sort_thunk {
	int (*cmp)(void *, struct list_head *, struct list_head *);
	void *priv;
};

static inline int
linux_le_cmp(void *priv, const void *d1, const void *d2)
{
	struct list_head *le1, *le2;
	struct list_sort_thunk *thunk;

	thunk = priv;
	le1 = *(__DECONST(struct list_head **, d1));
	le2 = *(__DECONST(struct list_head **, d2));
	return ((thunk->cmp)(thunk->priv, le1, le2));
}

void
list_sort(void *priv, struct list_head *head, int (*cmp)(void *priv,
    struct list_head *a, struct list_head *b))
{
	struct list_sort_thunk thunk;
	struct list_head **ar, *le;
	size_t count, i;

	count = 0;
	list_for_each(le, head)
		count++;
	ar = malloc(sizeof(struct list_head *) * count, M_KMALLOC, M_WAITOK);
	i = 0;
	list_for_each(le, head)
		ar[i++] = le;
	thunk.cmp = cmp;
	thunk.priv = priv;
	qsort_r(ar, count, sizeof(struct list_head *), &thunk, linux_le_cmp);
	INIT_LIST_HEAD(head);
	for (i = 0; i < count; i++)
		list_add_tail(ar[i], head);
	free(ar, M_KMALLOC);
}

void
linux_irq_handler(void *ent)
{
	struct irq_ent *irqe;

	linux_set_current(curthread);

	irqe = ent;
	irqe->handler(irqe->irq, irqe->arg);
}

#if defined(__i386__) || defined(__amd64__)
int
linux_wbinvd_on_all_cpus(void)
{

	pmap_invalidate_cache();
	return (0);
}
#endif

int
linux_on_each_cpu(void callback(void *), void *data)
{

	smp_rendezvous(smp_no_rendezvous_barrier, callback,
	    smp_no_rendezvous_barrier, data);
	return (0);
}

int
linux_in_atomic(void)
{

	return ((curthread->td_pflags & TDP_NOFAULTING) != 0);
}

struct linux_cdev *
linux_find_cdev(const char *name, unsigned major, unsigned minor)
{
	dev_t dev = MKDEV(major, minor);
	struct cdev *cdev;

	dev_lock();
	LIST_FOREACH(cdev, &linuxcdevsw.d_devs, si_list) {
		struct linux_cdev *ldev = cdev->si_drv1;
		if (ldev->dev == dev &&
		    strcmp(kobject_name(&ldev->kobj), name) == 0) {
			break;
		}
	}
	dev_unlock();

	return (cdev != NULL ? cdev->si_drv1 : NULL);
}

int
__register_chrdev(unsigned int major, unsigned int baseminor,
    unsigned int count, const char *name,
    const struct file_operations *fops)
{
	struct linux_cdev *cdev;
	int ret = 0;
	int i;

	for (i = baseminor; i < baseminor + count; i++) {
		cdev = cdev_alloc();
		cdev_init(cdev, fops);
		kobject_set_name(&cdev->kobj, name);

		ret = cdev_add(cdev, makedev(major, i), 1);
		if (ret != 0)
			break;
	}
	return (ret);
}

int
__register_chrdev_p(unsigned int major, unsigned int baseminor,
    unsigned int count, const char *name,
    const struct file_operations *fops, uid_t uid,
    gid_t gid, int mode)
{
	struct linux_cdev *cdev;
	int ret = 0;
	int i;

	for (i = baseminor; i < baseminor + count; i++) {
		cdev = cdev_alloc();
		cdev_init(cdev, fops);
		kobject_set_name(&cdev->kobj, name);

		ret = cdev_add_ext(cdev, makedev(major, i), uid, gid, mode);
		if (ret != 0)
			break;
	}
	return (ret);
}

void
__unregister_chrdev(unsigned int major, unsigned int baseminor,
    unsigned int count, const char *name)
{
	struct linux_cdev *cdevp;
	int i;

	for (i = baseminor; i < baseminor + count; i++) {
		cdevp = linux_find_cdev(name, major, i);
		if (cdevp != NULL)
			cdev_del(cdevp);
	}
}

void
linux_dump_stack(void)
{
#ifdef STACK
	struct stack st;

	stack_zero(&st);
	stack_save(&st);
	stack_print(&st);
#endif
}

#if defined(__i386__) || defined(__amd64__)
bool linux_cpu_has_clflush;
#endif

static void
linux_compat_init(void *arg)
{
	struct sysctl_oid *rootoid;
	int i;

#if defined(__i386__) || defined(__amd64__)
	linux_cpu_has_clflush = (cpu_feature & CPUID_CLFSH);
#endif
	rw_init(&linux_vma_lock, "lkpi-vma-lock");

	rootoid = SYSCTL_ADD_ROOT_NODE(NULL,
	    OID_AUTO, "sys", CTLFLAG_RD|CTLFLAG_MPSAFE, NULL, "sys");
	kobject_init(&linux_class_root, &linux_class_ktype);
	kobject_set_name(&linux_class_root, "class");
	linux_class_root.oidp = SYSCTL_ADD_NODE(NULL, SYSCTL_CHILDREN(rootoid),
	    OID_AUTO, "class", CTLFLAG_RD|CTLFLAG_MPSAFE, NULL, "class");
	kobject_init(&linux_root_device.kobj, &linux_dev_ktype);
	kobject_set_name(&linux_root_device.kobj, "device");
	linux_root_device.kobj.oidp = SYSCTL_ADD_NODE(NULL,
	    SYSCTL_CHILDREN(rootoid), OID_AUTO, "device", CTLFLAG_RD, NULL,
	    "device");
	linux_root_device.bsddev = root_bus;
	linux_class_misc.name = "misc";
	class_register(&linux_class_misc);
	INIT_LIST_HEAD(&pci_drivers);
	INIT_LIST_HEAD(&pci_devices);
	spin_lock_init(&pci_lock);
	mtx_init(&vmmaplock, "IO Map lock", NULL, MTX_DEF);
	for (i = 0; i < VMMAP_HASH_SIZE; i++)
		LIST_INIT(&vmmaphead[i]);
}
SYSINIT(linux_compat, SI_SUB_DRIVERS, SI_ORDER_SECOND, linux_compat_init, NULL);

static void
linux_compat_uninit(void *arg)
{
	linux_kobject_kfree_name(&linux_class_root);
	linux_kobject_kfree_name(&linux_root_device.kobj);
	linux_kobject_kfree_name(&linux_class_misc.kobj);

	mtx_destroy(&vmmaplock);
	spin_lock_destroy(&pci_lock);
	rw_destroy(&linux_vma_lock);
}
SYSUNINIT(linux_compat, SI_SUB_DRIVERS, SI_ORDER_SECOND, linux_compat_uninit, NULL);

/*
 * NOTE: Linux frequently uses "unsigned long" for pointer to integer
 * conversion and vice versa, where in FreeBSD "uintptr_t" would be
 * used. Assert these types have the same size, else some parts of the
 * LinuxKPI may not work like expected:
 */
CTASSERT(sizeof(unsigned long) == sizeof(uintptr_t));
