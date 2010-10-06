/* mm/ashmem.c
**
** Anonymous Shared Memory Subsystem, ashmem
**
** Copyright (C) 2008 Google, Inc.
**
** Robert Love <rlove@google.com>
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/security.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/personality.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/shmem_fs.h>
#include <linux/ashmem.h>

#define ASHMEM_NAME_PREFIX "dev/ashmem/"
#define ASHMEM_NAME_PREFIX_LEN (sizeof(ASHMEM_NAME_PREFIX) - 1)
#define ASHMEM_FULL_NAME_LEN (ASHMEM_NAME_LEN + ASHMEM_NAME_PREFIX_LEN)

/*
 * ashmem_area - anonymous shared memory area
 * Lifecycle: From our parent file's open() until its release()
 * Locking: Protected by `ashmem_mutex'
 * Big Note: Mappings do NOT pin this structure; it dies on close()
 */
struct ashmem_area {
	char name[ASHMEM_FULL_NAME_LEN];/* optional name for /proc/pid/maps */
	struct list_head unpinned_list;	/* list of all ashmem areas */
	struct file *file;		/* the shmem-based backing file */
	size_t size;			/* size of the mapping, in bytes */
	unsigned long prot_mask;	/* allowed prot bits, as vm_flags */
};

/*
 * ashmem_range - represents an interval of unpinned (evictable) pages
 * Lifecycle: From unpin to pin
 * Locking: Protected by `ashmem_mutex'
 */
struct ashmem_range {
	struct list_head lru;		/* entry in LRU list */
	struct list_head unpinned;	/* entry in its area's unpinned list */
	struct ashmem_area *asma;	/* associated area */
	size_t pgstart;			/* starting page, inclusive */
	size_t pgend;			/* ending page, inclusive */
	unsigned int purged;		/* ASHMEM_NOT or ASHMEM_WAS_PURGED */
};

/* LRU list of unpinned pages, protected by ashmem_mutex */
static LIST_HEAD(ashmem_lru_list);

/* Count of pages on our LRU list, protected by ashmem_mutex */
static unsigned long lru_count;

/*
 * ashmem_mutex - protects the list of and each individual ashmem_area
 *
 * Lock Ordering: ashmex_mutex -> i_mutex -> i_alloc_sem
 */
static DEFINE_MUTEX(ashmem_mutex);

static struct kmem_cache *ashmem_area_cachep __read_mostly;
static struct kmem_cache *ashmem_range_cachep __read_mostly;

#define range_size(range) \
  ((range)->pgend - (range)->pgstart + 1)

#define range_on_lru(range) \
  ((range)->purged == ASHMEM_NOT_PURGED)

#define page_range_subsumes_range(range, start, end) \
  (((range)->pgstart >= (start)) && ((range)->pgend <= (end)))

#define page_range_subsumed_by_range(range, start, end) \
  (((range)->pgstart <= (start)) && ((range)->pgend >= (end)))

#define page_in_range(range, page) \
 (((range)->pgstart <= (page)) && ((range)->pgend >= (page)))

#define page_range_in_range(range, start, end) \
  (page_in_range(range, start) || page_in_range(range, end) || \
   page_range_subsumes_range(range, start, end))

#define range_before_page(range, page) \
  ((range)->pgend < (page))

#define PROT_MASK		(PROT_EXEC | PROT_READ | PROT_WRITE)

static inline void lru_add(struct ashmem_range *range)
{
	list_add_tail(&range->lru, &ashmem_lru_list);
	lru_count += range_size(range);
}

static inline void lru_del(struct ashmem_range *range)
{
	list_del(&range->lru);
	lru_count -= range_size(range);
}

/*
 * range_alloc - allocate and initialize a new ashmem_range structure
 *
 * 'asma' - associated ashmem_area
 * 'prev_range' - the previous ashmem_range in the sorted asma->unpinned list
 * 'purged' - initial purge value (ASMEM_NOT_PURGED or ASHMEM_WAS_PURGED)
 * 'start' - starting page, inclusive
 * 'end' - ending page, inclusive
 *
 * Caller must hold ashmem_mutex.
 */
static int range_alloc(struct ashmem_area *asma,
		       struct ashmem_range *prev_range, unsigned int purged,
		       size_t start, size_t end)
{
	struct ashmem_range *range;

	range = kmem_cache_zalloc(ashmem_range_cachep, GFP_KERNEL);
	if (unlikely(!range))
		return -ENOMEM;

	range->asma = asma;
	range->pgstart = start;
	range->pgend = end;
	range->purged = purged;

	list_add_tail(&range->unpinned, &prev_range->unpinned);

	if (range_on_lru(range))
		lru_add(range);

	return 0;
}

static void range_del(struct ashmem_range *range)
{
	list_del(&range->unpinned);
	if (range_on_lru(range))
		lru_del(range);
	kmem_cache_free(ashmem_range_cachep, range);
}

/*
 * range_shrink - shrinks a range
 *
 * Caller must hold ashmem_mutex.
 */
static inline void range_shrink(struct ashmem_range *range,
				size_t start, size_t end)
{
	size_t pre = range_size(range);

	range->pgstart = start;
	range->pgend = end;

	if (range_on_lru(range))
		lru_count -= pre - range_size(range);
}

static int ashmem_open(struct inode *inode, struct file *file)
{
	struct ashmem_area *asma;
	int ret;

	ret = generic_file_open(inode, file);
	if (unlikely(ret))
		return ret;

	asma = kmem_cache_zalloc(ashmem_area_cachep, GFP_KERNEL);
	if (unlikely(!asma))
		return -ENOMEM;

	INIT_LIST_HEAD(&asma->unpinned_list);
	memcpy(asma->name, ASHMEM_NAME_PREFIX, ASHMEM_NAME_PREFIX_LEN);
	asma->prot_mask = PROT_MASK;
	file->private_data = asma;

	return 0;
}

static int ashmem_release(struct inode *ignored, struct file *file)
{
	struct ashmem_area *asma = file->private_data;
	struct ashmem_range *range, *next;

	mutex_lock(&ashmem_mutex);
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned)
		range_del(range);
	mutex_unlock(&ashmem_mutex);

	if (asma->file)
		fput(asma->file);
	kmem_cache_free(ashmem_area_cachep, asma);

	return 0;
}

static ssize_t ashmem_read(struct file *file, char __user *buf,
			   size_t len, loff_t *pos)
{
	struct ashmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* If size is not set, or set to 0, always return EOF. */
	if (asma->size == 0) {
		goto out;
        }

	if (!asma->file) {
		ret = -EBADF;
		goto out;
	}

	ret = asma->file->f_op->read(asma->file, buf, len, pos);
	if (ret < 0) {
		goto out;
	}

	/** Update backing file pos, since f_ops->read() doesn't */
	asma->file->f_pos = *pos;

out:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

static loff_t ashmem_llseek(struct file *file, loff_t offset, int origin)
{
	struct ashmem_area *asma = file->private_data;
	int ret;

	mutex_lock(&ashmem_mutex);

	if (asma->size == 0) {
		ret = -EINVAL;
		goto out;
	}

	if (!asma->file) {
		ret = -EBADF;
		goto out;
	}

	ret = asma->file->f_op->llseek(asma->file, offset, origin);
	if (ret < 0) {
		goto out;
	}

	/** Copy f_pos from backing file, since f_ops->llseek() sets it */
	file->f_pos = asma->file->f_pos;

out:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

static inline unsigned long
calc_vm_may_flags(unsigned long prot)
{
	return _calc_vm_trans(prot, PROT_READ,  VM_MAYREAD ) |
	       _calc_vm_trans(prot, PROT_WRITE, VM_MAYWRITE) |
	       _calc_vm_trans(prot, PROT_EXEC,  VM_MAYEXEC);
}

static int ashmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ashmem_area *asma = file->private_data;
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* user needs to SET_SIZE before mapping */
	if (unlikely(!asma->size)) {
		ret = -EINVAL;
		goto out;
	}

	/* requested protection bits must match our allowed protection mask */
	if (unlikely((vma->vm_flags & ~calc_vm_prot_bits(asma->prot_mask)) &
						calc_vm_prot_bits(PROT_MASK))) {
		ret = -EPERM;
		goto out;
	}
	vma->vm_flags &= ~calc_vm_may_flags(~asma->prot_mask);

	if (!asma->file) {
		char *name = ASHMEM_NAME_DEF;
		struct file *vmfile;

		if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0')
			name = asma->name;

		/* ... and allocate the backing shmem file */
		vmfile = shmem_file_setup(name, asma->size, vma->vm_flags);
		if (unlikely(IS_ERR(vmfile))) {
			ret = PTR_ERR(vmfile);
			goto out;
		}
		asma->file = vmfile;
	}
	get_file(asma->file);

	if (vma->vm_flags & VM_SHARED)
		shmem_set_file(vma, asma->file);
	else {
		if (vma->vm_file)
			fput(vma->vm_file);
		vma->vm_file = asma->file;
	}
	vma->vm_flags |= VM_CAN_NONLINEAR;

out:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

/*
 * ashmem_shrink - our cache shrinker, called from mm/vmscan.c :: shrink_slab
 *
 * 'nr_to_scan' is the number of objects (pages) to prune, or 0 to query how
 * many objects (pages) we have in total.
 *
 * 'gfp_mask' is the mask of the allocation that got us into this mess.
 *
 * Return value is the number of objects (pages) remaining, or -1 if we cannot
 * proceed without risk of deadlock (due to gfp_mask).
 *
 * We approximate LRU via least-recently-unpinned, jettisoning unpinned partial
 * chunks of ashmem regions LRU-wise one-at-a-time until we hit 'nr_to_scan'
 * pages freed.
 */
static int ashmem_shrink(struct shrinker *s, int nr_to_scan, gfp_t gfp_mask)
{
	struct ashmem_range *range, *next;

	/* We might recurse into filesystem code, so bail out if necessary */
	if (nr_to_scan && !(gfp_mask & __GFP_FS))
		return -1;
	if (!nr_to_scan)
		return lru_count;

	mutex_lock(&ashmem_mutex);
	list_for_each_entry_safe(range, next, &ashmem_lru_list, lru) {
		struct inode *inode = range->asma->file->f_dentry->d_inode;
		loff_t start = range->pgstart * PAGE_SIZE;
		loff_t end = (range->pgend + 1) * PAGE_SIZE - 1;

		vmtruncate_range(inode, start, end);
		range->purged = ASHMEM_WAS_PURGED;
		lru_del(range);

		nr_to_scan -= range_size(range);
		if (nr_to_scan <= 0)
			break;
	}
	mutex_unlock(&ashmem_mutex);

	return lru_count;
}

static struct shrinker ashmem_shrinker = {
	.shrink = ashmem_shrink,
	.seeks = DEFAULT_SEEKS * 4,
};

static int set_prot_mask(struct ashmem_area *asma, unsigned long prot)
{
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* the user can only remove, not add, protection bits */
	if (unlikely((asma->prot_mask & prot) != prot)) {
		ret = -EINVAL;
		goto out;
	}

	/* does the application expect PROT_READ to imply PROT_EXEC? */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		prot |= PROT_EXEC;

	asma->prot_mask = prot;

out:
	mutex_unlock(&ashmem_mutex);
	return ret;
}

static int set_name(struct ashmem_area *asma, void __user *name)
{
	int ret = 0;

	mutex_lock(&ashmem_mutex);

	/* cannot change an existing mapping's name */
	if (unlikely(asma->file)) {
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(copy_from_user(asma->name + ASHMEM_NAME_PREFIX_LEN,
				    name, ASHMEM_NAME_LEN)))
		ret = -EFAULT;
	asma->name[ASHMEM_FULL_NAME_LEN-1] = '\0';

out:
	mutex_unlock(&ashmem_mutex);

	return ret;
}

static int get_name(struct ashmem_area *asma, void __user *name)
{
	int ret = 0;

	mutex_lock(&ashmem_mutex);
	if (asma->name[ASHMEM_NAME_PREFIX_LEN] != '\0') {
		size_t len;

		/*
		 * Copying only `len', instead of ASHMEM_NAME_LEN, bytes
		 * prevents us from revealing one user's stack to another.
		 */
		len = strlen(asma->name + ASHMEM_NAME_PREFIX_LEN) + 1;
		if (unlikely(copy_to_user(name,
				asma->name + ASHMEM_NAME_PREFIX_LEN, len)))
			ret = -EFAULT;
	} else {
		if (unlikely(copy_to_user(name, ASHMEM_NAME_DEF,
					  sizeof(ASHMEM_NAME_DEF))))
			ret = -EFAULT;
	}
	mutex_unlock(&ashmem_mutex);

	return ret;
}

/*
 * ashmem_pin - pin the given ashmem region, returning whether it was
 * previously purged (ASHMEM_WAS_PURGED) or not (ASHMEM_NOT_PURGED).
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_pin(struct ashmem_area *asma, size_t pgstart, size_t pgend)
{
	struct ashmem_range *range, *next;
	int ret = ASHMEM_NOT_PURGED;

	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* moved past last applicable page; we can short circuit */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to pin pages that span multiple ranges,
		 * or to pin pages that aren't even unpinned, so this is messy.
		 *
		 * Four cases:
		 * 1. The requested range subsumes an existing range, so we
		 *    just remove the entire matching range.
		 * 2. The requested range overlaps the start of an existing
		 *    range, so we just update that range.
		 * 3. The requested range overlaps the end of an existing
		 *    range, so we just update that range.
		 * 4. The requested range punches a hole in an existing range,
		 *    so we have to update one side of the range and then
		 *    create a new range for the other side.
		 */
		if (page_range_in_range(range, pgstart, pgend)) {
			ret |= range->purged;

			/* Case #1: Easy. Just nuke the whole thing. */
			if (page_range_subsumes_range(range, pgstart, pgend)) {
				range_del(range);
				continue;
			}

			/* Case #2: We overlap from the start, so adjust it */
			if (range->pgstart >= pgstart) {
				range_shrink(range, pgend + 1, range->pgend);
				continue;
			}

			/* Case #3: We overlap from the rear, so adjust it */
			if (range->pgend <= pgend) {
				range_shrink(range, range->pgstart, pgstart-1);
				continue;
			}

			/*
			 * Case #4: We eat a chunk out of the middle. A bit
			 * more complicated, we allocate a new range for the
			 * second half and adjust the first chunk's endpoint.
			 */
			range_alloc(asma, range, range->purged,
				    pgend + 1, range->pgend);
			range_shrink(range, range->pgstart, pgstart - 1);
			break;
		}
	}

	return ret;
}

/*
 * ashmem_unpin - unpin the given range of pages. Returns zero on success.
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_unpin(struct ashmem_area *asma, size_t pgstart, size_t pgend)
{
	struct ashmem_range *range, *next;
	unsigned int purged = ASHMEM_NOT_PURGED;

restart:
	list_for_each_entry_safe(range, next, &asma->unpinned_list, unpinned) {
		/* short circuit: this is our insertion point */
		if (range_before_page(range, pgstart))
			break;

		/*
		 * The user can ask us to unpin pages that are already entirely
		 * or partially pinned. We handle those two cases here.
		 */
		if (page_range_subsumed_by_range(range, pgstart, pgend))
			return 0;
		if (page_range_in_range(range, pgstart, pgend)) {
			pgstart = min_t(size_t, range->pgstart, pgstart),
			pgend = max_t(size_t, range->pgend, pgend);
			purged |= range->purged;
			range_del(range);
			goto restart;
		}
	}

	return range_alloc(asma, range, purged, pgstart, pgend);
}

/*
 * ashmem_get_pin_status - Returns ASHMEM_IS_UNPINNED if _any_ pages in the
 * given interval are unpinned and ASHMEM_IS_PINNED otherwise.
 *
 * Caller must hold ashmem_mutex.
 */
static int ashmem_get_pin_status(struct ashmem_area *asma, size_t pgstart,
				 size_t pgend)
{
	struct ashmem_range *range;
	int ret = ASHMEM_IS_PINNED;

	list_for_each_entry(range, &asma->unpinned_list, unpinned) {
		if (range_before_page(range, pgstart))
			break;
		if (page_range_in_range(range, pgstart, pgend)) {
			ret = ASHMEM_IS_UNPINNED;
			break;
		}
	}

	return ret;
}

static int ashmem_pin_unpin(struct ashmem_area *asma, unsigned long cmd,
			    void __user *p)
{
	struct ashmem_pin pin;
	size_t pgstart, pgend;
	int ret = -EINVAL;

	if (unlikely(!asma->file))
		return -EINVAL;

	if (unlikely(copy_from_user(&pin, p, sizeof(pin))))
		return -EFAULT;

	/* per custom, you can pass zero for len to mean "everything onward" */
	if (!pin.len)
		pin.len = PAGE_ALIGN(asma->size) - pin.offset;

	if (unlikely((pin.offset | pin.len) & ~PAGE_MASK))
		return -EINVAL;

	if (unlikely(((__u32) -1) - pin.offset < pin.len))
		return -EINVAL;

	if (unlikely(PAGE_ALIGN(asma->size) < pin.offset + pin.len))
		return -EINVAL;

	pgstart = pin.offset / PAGE_SIZE;
	pgend = pgstart + (pin.len / PAGE_SIZE) - 1;

	mutex_lock(&ashmem_mutex);

	switch (cmd) {
	case ASHMEM_PIN:
		ret = ashmem_pin(asma, pgstart, pgend);
		break;
	case ASHMEM_UNPIN:
		ret = ashmem_unpin(asma, pgstart, pgend);
		break;
	case ASHMEM_GET_PIN_STATUS:
		ret = ashmem_get_pin_status(asma, pgstart, pgend);
		break;
	}

	mutex_unlock(&ashmem_mutex);

	return ret;
}

static long ashmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ashmem_area *asma = file->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		ret = set_name(asma, (void __user *) arg);
		break;
	case ASHMEM_GET_NAME:
		ret = get_name(asma, (void __user *) arg);
		break;
	case ASHMEM_SET_SIZE:
		ret = -EINVAL;
		if (!asma->file) {
			ret = 0;
			asma->size = (size_t) arg;
		}
		break;
	case ASHMEM_GET_SIZE:
		ret = asma->size;
		break;
	case ASHMEM_SET_PROT_MASK:
		ret = set_prot_mask(asma, arg);
		break;
	case ASHMEM_GET_PROT_MASK:
		ret = asma->prot_mask;
		break;
	case ASHMEM_PIN:
	case ASHMEM_UNPIN:
	case ASHMEM_GET_PIN_STATUS:
		ret = ashmem_pin_unpin(asma, cmd, (void __user *) arg);
		break;
	case ASHMEM_PURGE_ALL_CACHES:
		ret = -EPERM;
		if (capable(CAP_SYS_ADMIN)) {
			ret = ashmem_shrink(&ashmem_shrinker, 0, GFP_KERNEL);
			ashmem_shrink(&ashmem_shrinker, ret, GFP_KERNEL);
		}
		break;
	}

	return ret;
}

static struct file_operations ashmem_fops = {
	.owner = THIS_MODULE,
	.open = ashmem_open,
	.release = ashmem_release,
        .read = ashmem_read,
        .llseek = ashmem_llseek,
	.mmap = ashmem_mmap,
	.unlocked_ioctl = ashmem_ioctl,
	.compat_ioctl = ashmem_ioctl,
};

static struct miscdevice ashmem_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ashmem",
	.fops = &ashmem_fops,
};

static int __init ashmem_init(void)
{
	int ret;

	ashmem_area_cachep = kmem_cache_create("ashmem_area_cache",
					  sizeof(struct ashmem_area),
					  0, 0, NULL);
	if (unlikely(!ashmem_area_cachep)) {
		printk(KERN_ERR "ashmem: failed to create slab cache\n");
		return -ENOMEM;
	}

	ashmem_range_cachep = kmem_cache_create("ashmem_range_cache",
					  sizeof(struct ashmem_range),
					  0, 0, NULL);
	if (unlikely(!ashmem_range_cachep)) {
		printk(KERN_ERR "ashmem: failed to create slab cache\n");
		return -ENOMEM;
	}

	ret = misc_register(&ashmem_misc);
	if (unlikely(ret)) {
		printk(KERN_ERR "ashmem: failed to register misc device!\n");
		return ret;
	}

	register_shrinker(&ashmem_shrinker);

	printk(KERN_INFO "ashmem: initialized\n");

	return 0;
}

static void __exit ashmem_exit(void)
{
	int ret;

	unregister_shrinker(&ashmem_shrinker);

	ret = misc_deregister(&ashmem_misc);
	if (unlikely(ret))
		printk(KERN_ERR "ashmem: failed to unregister misc device!\n");

	kmem_cache_destroy(ashmem_range_cachep);
	kmem_cache_destroy(ashmem_area_cachep);

	printk(KERN_INFO "ashmem: unloaded\n");
}

module_init(ashmem_init);
module_exit(ashmem_exit);

MODULE_LICENSE("GPL");
