// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit userspace memory allocation resource management.
 */
#include <kunit/resource.h>
#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/mm.h>

struct kunit_vm_mmap_resource {
	unsigned long addr;
	size_t size;
};

/* vm_mmap() arguments */
struct kunit_vm_mmap_params {
	struct file *file;
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flag;
	unsigned long offset;
};

/* Create and attach a new mm if it doesn't already exist. */
static int kunit_attach_mm(void)
{
	struct mm_struct *mm;

	if (current->mm)
		return 0;

	/* arch_pick_mmap_layout() is only sane with MMU systems. */
	if (!IS_ENABLED(CONFIG_MMU))
		return -EINVAL;

	mm = mm_alloc();
	if (!mm)
		return -ENOMEM;

	/* Define the task size. */
	mm->task_size = TASK_SIZE;

	/* Make sure we can allocate new VMAs. */
	arch_pick_mmap_layout(mm, &current->signal->rlim[RLIMIT_STACK]);

	/* Attach the mm. It will be cleaned up when the process dies. */
	kthread_use_mm(mm);

	return 0;
}

static int kunit_vm_mmap_init(struct kunit_resource *res, void *context)
{
	struct kunit_vm_mmap_params *p = context;
	struct kunit_vm_mmap_resource vres;
	int ret;

	ret = kunit_attach_mm();
	if (ret)
		return ret;

	vres.size = p->len;
	vres.addr = vm_mmap(p->file, p->addr, p->len, p->prot, p->flag, p->offset);
	if (!vres.addr)
		return -ENOMEM;
	res->data = kmemdup(&vres, sizeof(vres), GFP_KERNEL);
	if (!res->data) {
		vm_munmap(vres.addr, vres.size);
		return -ENOMEM;
	}

	return 0;
}

static void kunit_vm_mmap_free(struct kunit_resource *res)
{
	struct kunit_vm_mmap_resource *vres = res->data;

	/*
	 * Since this is executed from the test monitoring process,
	 * the test's mm has already been torn down. We don't need
	 * to run vm_munmap(vres->addr, vres->size), only clean up
	 * the vres.
	 */

	kfree(vres);
	res->data = NULL;
}

unsigned long kunit_vm_mmap(struct kunit *test, struct file *file,
			    unsigned long addr, unsigned long len,
			    unsigned long prot, unsigned long flag,
			    unsigned long offset)
{
	struct kunit_vm_mmap_params params = {
		.file = file,
		.addr = addr,
		.len = len,
		.prot = prot,
		.flag = flag,
		.offset = offset,
	};
	struct kunit_vm_mmap_resource *vres;

	vres = kunit_alloc_resource(test,
				    kunit_vm_mmap_init,
				    kunit_vm_mmap_free,
				    GFP_KERNEL,
				    &params);
	if (vres)
		return vres->addr;
	return 0;
}
EXPORT_SYMBOL_GPL(kunit_vm_mmap);

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
