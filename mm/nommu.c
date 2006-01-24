/*
 *  linux/mm/nommu.c
 *
 *  Replacement code for mm functions to support CPU's that don't
 *  have any form of memory management unit (thus no virtual memory).
 *
 *  See Documentation/nommu-mmap.txt
 *
 *  Copyright (c) 2004-2005 David Howells <dhowells@redhat.com>
 *  Copyright (c) 2000-2003 David McCullough <davidm@snapgear.com>
 *  Copyright (c) 2000-2001 D Jeff Dionne <jeff@uClinux.org>
 *  Copyright (c) 2002      Greg Ungerer <gerg@snapgear.com>
 */

#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ptrace.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/syscalls.h>

#include <asm/uaccess.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

void *high_memory;
struct page *mem_map;
unsigned long max_mapnr;
unsigned long num_physpages;
unsigned long askedalloc, realalloc;
atomic_t vm_committed_space = ATOMIC_INIT(0);
int sysctl_overcommit_memory = OVERCOMMIT_GUESS; /* heuristic overcommit */
int sysctl_overcommit_ratio = 50; /* default is 50% */
int sysctl_max_map_count = DEFAULT_MAX_MAP_COUNT;
int heap_stack_gap = 0;

EXPORT_SYMBOL(mem_map);
EXPORT_SYMBOL(__vm_enough_memory);

/* list of shareable VMAs */
struct rb_root nommu_vma_tree = RB_ROOT;
DECLARE_RWSEM(nommu_vma_sem);

struct vm_operations_struct generic_file_vm_ops = {
};

EXPORT_SYMBOL(vmalloc);
EXPORT_SYMBOL(vfree);
EXPORT_SYMBOL(vmalloc_to_page);
EXPORT_SYMBOL(vmalloc_32);

/*
 * Handle all mappings that got truncated by a "truncate()"
 * system call.
 *
 * NOTE! We have to be ready to update the memory sharing
 * between the file and the memory map for a potential last
 * incomplete page.  Ugly, but necessary.
 */
int vmtruncate(struct inode *inode, loff_t offset)
{
	struct address_space *mapping = inode->i_mapping;
	unsigned long limit;

	if (inode->i_size < offset)
		goto do_expand;
	i_size_write(inode, offset);

	truncate_inode_pages(mapping, offset);
	goto out_truncate;

do_expand:
	limit = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;
	if (limit != RLIM_INFINITY && offset > limit)
		goto out_sig;
	if (offset > inode->i_sb->s_maxbytes)
		goto out;
	i_size_write(inode, offset);

out_truncate:
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	return 0;
out_sig:
	send_sig(SIGXFSZ, current, 0);
out:
	return -EFBIG;
}

EXPORT_SYMBOL(vmtruncate);

/*
 * Return the total memory allocated for this pointer, not
 * just what the caller asked for.
 *
 * Doesn't have to be accurate, i.e. may have races.
 */
unsigned int kobjsize(const void *objp)
{
	struct page *page;

	if (!objp || !((page = virt_to_page(objp))))
		return 0;

	if (PageSlab(page))
		return ksize(objp);

	BUG_ON(page->index < 0);
	BUG_ON(page->index >= MAX_ORDER);

	return (PAGE_SIZE << page->index);
}

/*
 * The nommu dodgy version :-)
 */
int get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
	unsigned long start, int len, int write, int force,
	struct page **pages, struct vm_area_struct **vmas)
{
	int i;
	static struct vm_area_struct dummy_vma;

	for (i = 0; i < len; i++) {
		if (pages) {
			pages[i] = virt_to_page(start);
			if (pages[i])
				page_cache_get(pages[i]);
		}
		if (vmas)
			vmas[i] = &dummy_vma;
		start += PAGE_SIZE;
	}
	return(i);
}

EXPORT_SYMBOL(get_user_pages);

DEFINE_RWLOCK(vmlist_lock);
struct vm_struct *vmlist;

void vfree(void *addr)
{
	kfree(addr);
}

void *__vmalloc(unsigned long size, gfp_t gfp_mask, pgprot_t prot)
{
	/*
	 * kmalloc doesn't like __GFP_HIGHMEM for some reason
	 */
	return kmalloc(size, gfp_mask & ~__GFP_HIGHMEM);
}

struct page * vmalloc_to_page(void *addr)
{
	return virt_to_page(addr);
}

unsigned long vmalloc_to_pfn(void *addr)
{
	return page_to_pfn(virt_to_page(addr));
}


long vread(char *buf, char *addr, unsigned long count)
{
	memcpy(buf, addr, count);
	return count;
}

long vwrite(char *buf, char *addr, unsigned long count)
{
	/* Don't allow overflow */
	if ((unsigned long) addr + count < count)
		count = -(unsigned long) addr;

	memcpy(addr, buf, count);
	return(count);
}

/*
 *	vmalloc  -  allocate virtually continguos memory
 *
 *	@size:		allocation size
 *
 *	Allocate enough pages to cover @size from the page level
 *	allocator and map them into continguos kernel virtual space.
 *
 *	For tight cotrol over page level allocator and protection flags
 *	use __vmalloc() instead.
 */
void *vmalloc(unsigned long size)
{
       return __vmalloc(size, GFP_KERNEL | __GFP_HIGHMEM, PAGE_KERNEL);
}

/*
 *	vmalloc_32  -  allocate virtually continguos memory (32bit addressable)
 *
 *	@size:		allocation size
 *
 *	Allocate enough 32bit PA addressable pages to cover @size from the
 *	page level allocator and map them into continguos kernel virtual space.
 */
void *vmalloc_32(unsigned long size)
{
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL);
}

void *vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot)
{
	BUG();
	return NULL;
}

void vunmap(void *addr)
{
	BUG();
}

/*
 *  sys_brk() for the most part doesn't need the global kernel
 *  lock, except when an application is doing something nasty
 *  like trying to un-brk an area that has already been mapped
 *  to a regular file.  in this case, the unmapping will need
 *  to invoke file system routines that need the global lock.
 */
asmlinkage unsigned long sys_brk(unsigned long brk)
{
	struct mm_struct *mm = current->mm;

	if (brk < mm->start_brk || brk > mm->context.end_brk)
		return mm->brk;

	if (mm->brk == brk)
		return mm->brk;

	/*
	 * Always allow shrinking brk
	 */
	if (brk <= mm->brk) {
		mm->brk = brk;
		return brk;
	}

	/*
	 * Ok, looks good - let it rip.
	 */
	return mm->brk = brk;
}

#ifdef DEBUG
static void show_process_blocks(void)
{
	struct vm_list_struct *vml;

	printk("Process blocks %d:", current->pid);

	for (vml = &current->mm->context.vmlist; vml; vml = vml->next) {
		printk(" %p: %p", vml, vml->vma);
		if (vml->vma)
			printk(" (%d @%lx #%d)",
			       kobjsize((void *) vml->vma->vm_start),
			       vml->vma->vm_start,
			       atomic_read(&vml->vma->vm_usage));
		printk(vml->next ? " ->" : ".\n");
	}
}
#endif /* DEBUG */

static inline struct vm_area_struct *find_nommu_vma(unsigned long start)
{
	struct vm_area_struct *vma;
	struct rb_node *n = nommu_vma_tree.rb_node;

	while (n) {
		vma = rb_entry(n, struct vm_area_struct, vm_rb);

		if (start < vma->vm_start)
			n = n->rb_left;
		else if (start > vma->vm_start)
			n = n->rb_right;
		else
			return vma;
	}

	return NULL;
}

static void add_nommu_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *pvma;
	struct address_space *mapping;
	struct rb_node **p = &nommu_vma_tree.rb_node;
	struct rb_node *parent = NULL;

	/* add the VMA to the mapping */
	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;

		flush_dcache_mmap_lock(mapping);
		vma_prio_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}

	/* add the VMA to the master list */
	while (*p) {
		parent = *p;
		pvma = rb_entry(parent, struct vm_area_struct, vm_rb);

		if (vma->vm_start < pvma->vm_start) {
			p = &(*p)->rb_left;
		}
		else if (vma->vm_start > pvma->vm_start) {
			p = &(*p)->rb_right;
		}
		else {
			/* mappings are at the same address - this can only
			 * happen for shared-mem chardevs and shared file
			 * mappings backed by ramfs/tmpfs */
			BUG_ON(!(pvma->vm_flags & VM_SHARED));

			if (vma < pvma)
				p = &(*p)->rb_left;
			else if (vma > pvma)
				p = &(*p)->rb_right;
			else
				BUG();
		}
	}

	rb_link_node(&vma->vm_rb, parent, p);
	rb_insert_color(&vma->vm_rb, &nommu_vma_tree);
}

static void delete_nommu_vma(struct vm_area_struct *vma)
{
	struct address_space *mapping;

	/* remove the VMA from the mapping */
	if (vma->vm_file) {
		mapping = vma->vm_file->f_mapping;

		flush_dcache_mmap_lock(mapping);
		vma_prio_tree_remove(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}

	/* remove from the master list */
	rb_erase(&vma->vm_rb, &nommu_vma_tree);
}

/*
 * determine whether a mapping should be permitted and, if so, what sort of
 * mapping we're capable of supporting
 */
static int validate_mmap_request(struct file *file,
				 unsigned long addr,
				 unsigned long len,
				 unsigned long prot,
				 unsigned long flags,
				 unsigned long pgoff,
				 unsigned long *_capabilities)
{
	unsigned long capabilities;
	unsigned long reqprot = prot;
	int ret;

	/* do the simple checks first */
	if (flags & MAP_FIXED || addr) {
		printk(KERN_DEBUG
		       "%d: Can't do fixed-address/overlay mmap of RAM\n",
		       current->pid);
		return -EINVAL;
	}

	if ((flags & MAP_TYPE) != MAP_PRIVATE &&
	    (flags & MAP_TYPE) != MAP_SHARED)
		return -EINVAL;

	if (PAGE_ALIGN(len) == 0)
		return addr;

	if (len > TASK_SIZE)
		return -EINVAL;

	/* offset overflow? */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EINVAL;

	if (file) {
		/* validate file mapping requests */
		struct address_space *mapping;

		/* files must support mmap */
		if (!file->f_op || !file->f_op->mmap)
			return -ENODEV;

		/* work out if what we've got could possibly be shared
		 * - we support chardevs that provide their own "memory"
		 * - we support files/blockdevs that are memory backed
		 */
		mapping = file->f_mapping;
		if (!mapping)
			mapping = file->f_dentry->d_inode->i_mapping;

		capabilities = 0;
		if (mapping && mapping->backing_dev_info)
			capabilities = mapping->backing_dev_info->capabilities;

		if (!capabilities) {
			/* no explicit capabilities set, so assume some
			 * defaults */
			switch (file->f_dentry->d_inode->i_mode & S_IFMT) {
			case S_IFREG:
			case S_IFBLK:
				capabilities = BDI_CAP_MAP_COPY;
				break;

			case S_IFCHR:
				capabilities =
					BDI_CAP_MAP_DIRECT |
					BDI_CAP_READ_MAP |
					BDI_CAP_WRITE_MAP;
				break;

			default:
				return -EINVAL;
			}
		}

		/* eliminate any capabilities that we can't support on this
		 * device */
		if (!file->f_op->get_unmapped_area)
			capabilities &= ~BDI_CAP_MAP_DIRECT;
		if (!file->f_op->read)
			capabilities &= ~BDI_CAP_MAP_COPY;

		if (flags & MAP_SHARED) {
			/* do checks for writing, appending and locking */
			if ((prot & PROT_WRITE) &&
			    !(file->f_mode & FMODE_WRITE))
				return -EACCES;

			if (IS_APPEND(file->f_dentry->d_inode) &&
			    (file->f_mode & FMODE_WRITE))
				return -EACCES;

			if (locks_verify_locked(file->f_dentry->d_inode))
				return -EAGAIN;

			if (!(capabilities & BDI_CAP_MAP_DIRECT))
				return -ENODEV;

			if (((prot & PROT_READ)  && !(capabilities & BDI_CAP_READ_MAP))  ||
			    ((prot & PROT_WRITE) && !(capabilities & BDI_CAP_WRITE_MAP)) ||
			    ((prot & PROT_EXEC)  && !(capabilities & BDI_CAP_EXEC_MAP))
			    ) {
				printk("MAP_SHARED not completely supported on !MMU\n");
				return -EINVAL;
			}

			/* we mustn't privatise shared mappings */
			capabilities &= ~BDI_CAP_MAP_COPY;
		}
		else {
			/* we're going to read the file into private memory we
			 * allocate */
			if (!(capabilities & BDI_CAP_MAP_COPY))
				return -ENODEV;

			/* we don't permit a private writable mapping to be
			 * shared with the backing device */
			if (prot & PROT_WRITE)
				capabilities &= ~BDI_CAP_MAP_DIRECT;
		}

		/* handle executable mappings and implied executable
		 * mappings */
		if (file->f_vfsmnt->mnt_flags & MNT_NOEXEC) {
			if (prot & PROT_EXEC)
				return -EPERM;
		}
		else if ((prot & PROT_READ) && !(prot & PROT_EXEC)) {
			/* handle implication of PROT_EXEC by PROT_READ */
			if (current->personality & READ_IMPLIES_EXEC) {
				if (capabilities & BDI_CAP_EXEC_MAP)
					prot |= PROT_EXEC;
			}
		}
		else if ((prot & PROT_READ) &&
			 (prot & PROT_EXEC) &&
			 !(capabilities & BDI_CAP_EXEC_MAP)
			 ) {
			/* backing file is not executable, try to copy */
			capabilities &= ~BDI_CAP_MAP_DIRECT;
		}
	}
	else {
		/* anonymous mappings are always memory backed and can be
		 * privately mapped
		 */
		capabilities = BDI_CAP_MAP_COPY;

		/* handle PROT_EXEC implication by PROT_READ */
		if ((prot & PROT_READ) &&
		    (current->personality & READ_IMPLIES_EXEC))
			prot |= PROT_EXEC;
	}

	/* allow the security API to have its say */
	ret = security_file_mmap(file, reqprot, prot, flags);
	if (ret < 0)
		return ret;

	/* looks okay */
	*_capabilities = capabilities;
	return 0;
}

/*
 * we've determined that we can make the mapping, now translate what we
 * now know into VMA flags
 */
static unsigned long determine_vm_flags(struct file *file,
					unsigned long prot,
					unsigned long flags,
					unsigned long capabilities)
{
	unsigned long vm_flags;

	vm_flags = calc_vm_prot_bits(prot) | calc_vm_flag_bits(flags);
	vm_flags |= VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;
	/* vm_flags |= mm->def_flags; */

	if (!(capabilities & BDI_CAP_MAP_DIRECT)) {
		/* attempt to share read-only copies of mapped file chunks */
		if (file && !(prot & PROT_WRITE))
			vm_flags |= VM_MAYSHARE;
	}
	else {
		/* overlay a shareable mapping on the backing device or inode
		 * if possible - used for chardevs, ramfs/tmpfs/shmfs and
		 * romfs/cramfs */
		if (flags & MAP_SHARED)
			vm_flags |= VM_MAYSHARE | VM_SHARED;
		else if ((((vm_flags & capabilities) ^ vm_flags) & BDI_CAP_VMFLAGS) == 0)
			vm_flags |= VM_MAYSHARE;
	}

	/* refuse to let anyone share private mappings with this process if
	 * it's being traced - otherwise breakpoints set in it may interfere
	 * with another untraced process
	 */
	if ((flags & MAP_PRIVATE) && (current->ptrace & PT_PTRACED))
		vm_flags &= ~VM_MAYSHARE;

	return vm_flags;
}

/*
 * set up a shared mapping on a file
 */
static int do_mmap_shared_file(struct vm_area_struct *vma, unsigned long len)
{
	int ret;

	ret = vma->vm_file->f_op->mmap(vma->vm_file, vma);
	if (ret != -ENOSYS)
		return ret;

	/* getting an ENOSYS error indicates that direct mmap isn't
	 * possible (as opposed to tried but failed) so we'll fall
	 * through to making a private copy of the data and mapping
	 * that if we can */
	return -ENODEV;
}

/*
 * set up a private mapping or an anonymous shared mapping
 */
static int do_mmap_private(struct vm_area_struct *vma, unsigned long len)
{
	void *base;
	int ret;

	/* invoke the file's mapping function so that it can keep track of
	 * shared mappings on devices or memory
	 * - VM_MAYSHARE will be set if it may attempt to share
	 */
	if (vma->vm_file) {
		ret = vma->vm_file->f_op->mmap(vma->vm_file, vma);
		if (ret != -ENOSYS) {
			/* shouldn't return success if we're not sharing */
			BUG_ON(ret == 0 && !(vma->vm_flags & VM_MAYSHARE));
			return ret; /* success or a real error */
		}

		/* getting an ENOSYS error indicates that direct mmap isn't
		 * possible (as opposed to tried but failed) so we'll try to
		 * make a private copy of the data and map that instead */
	}

	/* allocate some memory to hold the mapping
	 * - note that this may not return a page-aligned address if the object
	 *   we're allocating is smaller than a page
	 */
	base = kmalloc(len, GFP_KERNEL);
	if (!base)
		goto enomem;

	vma->vm_start = (unsigned long) base;
	vma->vm_end = vma->vm_start + len;
	vma->vm_flags |= VM_MAPPED_COPY;

#ifdef WARN_ON_SLACK
	if (len + WARN_ON_SLACK <= kobjsize(result))
		printk("Allocation of %lu bytes from process %d has %lu bytes of slack\n",
		       len, current->pid, kobjsize(result) - len);
#endif

	if (vma->vm_file) {
		/* read the contents of a file into the copy */
		mm_segment_t old_fs;
		loff_t fpos;

		fpos = vma->vm_pgoff;
		fpos <<= PAGE_SHIFT;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = vma->vm_file->f_op->read(vma->vm_file, base, len, &fpos);
		set_fs(old_fs);

		if (ret < 0)
			goto error_free;

		/* clear the last little bit */
		if (ret < len)
			memset(base + ret, 0, len - ret);

	} else {
		/* if it's an anonymous mapping, then just clear it */
		memset(base, 0, len);
	}

	return 0;

error_free:
	kfree(base);
	vma->vm_start = 0;
	return ret;

enomem:
	printk("Allocation of length %lu from process %d failed\n",
	       len, current->pid);
	show_free_areas();
	return -ENOMEM;
}

/*
 * handle mapping creation for uClinux
 */
unsigned long do_mmap_pgoff(struct file *file,
			    unsigned long addr,
			    unsigned long len,
			    unsigned long prot,
			    unsigned long flags,
			    unsigned long pgoff)
{
	struct vm_list_struct *vml = NULL;
	struct vm_area_struct *vma = NULL;
	struct rb_node *rb;
	unsigned long capabilities, vm_flags;
	void *result;
	int ret;

	/* decide whether we should attempt the mapping, and if so what sort of
	 * mapping */
	ret = validate_mmap_request(file, addr, len, prot, flags, pgoff,
				    &capabilities);
	if (ret < 0)
		return ret;

	/* we've determined that we can make the mapping, now translate what we
	 * now know into VMA flags */
	vm_flags = determine_vm_flags(file, prot, flags, capabilities);

	/* we're going to need to record the mapping if it works */
	vml = kmalloc(sizeof(struct vm_list_struct), GFP_KERNEL);
	if (!vml)
		goto error_getting_vml;
	memset(vml, 0, sizeof(*vml));

	down_write(&nommu_vma_sem);

	/* if we want to share, we need to check for VMAs created by other
	 * mmap() calls that overlap with our proposed mapping
	 * - we can only share with an exact match on most regular files
	 * - shared mappings on character devices and memory backed files are
	 *   permitted to overlap inexactly as far as we are concerned for in
	 *   these cases, sharing is handled in the driver or filesystem rather
	 *   than here
	 */
	if (vm_flags & VM_MAYSHARE) {
		unsigned long pglen = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		unsigned long vmpglen;

		for (rb = rb_first(&nommu_vma_tree); rb; rb = rb_next(rb)) {
			vma = rb_entry(rb, struct vm_area_struct, vm_rb);

			if (!(vma->vm_flags & VM_MAYSHARE))
				continue;

			/* search for overlapping mappings on the same file */
			if (vma->vm_file->f_dentry->d_inode != file->f_dentry->d_inode)
				continue;

			if (vma->vm_pgoff >= pgoff + pglen)
				continue;

			vmpglen = vma->vm_end - vma->vm_start + PAGE_SIZE - 1;
			vmpglen >>= PAGE_SHIFT;
			if (pgoff >= vma->vm_pgoff + vmpglen)
				continue;

			/* handle inexactly overlapping matches between mappings */
			if (vma->vm_pgoff != pgoff || vmpglen != pglen) {
				if (!(capabilities & BDI_CAP_MAP_DIRECT))
					goto sharing_violation;
				continue;
			}

			/* we've found a VMA we can share */
			atomic_inc(&vma->vm_usage);

			vml->vma = vma;
			result = (void *) vma->vm_start;
			goto shared;
		}

		vma = NULL;

		/* obtain the address at which to make a shared mapping
		 * - this is the hook for quasi-memory character devices to
		 *   tell us the location of a shared mapping
		 */
		if (file && file->f_op->get_unmapped_area) {
			addr = file->f_op->get_unmapped_area(file, addr, len,
							     pgoff, flags);
			if (IS_ERR((void *) addr)) {
				ret = addr;
				if (ret != (unsigned long) -ENOSYS)
					goto error;

				/* the driver refused to tell us where to site
				 * the mapping so we'll have to attempt to copy
				 * it */
				ret = (unsigned long) -ENODEV;
				if (!(capabilities & BDI_CAP_MAP_COPY))
					goto error;

				capabilities &= ~BDI_CAP_MAP_DIRECT;
			}
		}
	}

	/* we're going to need a VMA struct as well */
	vma = kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
	if (!vma)
		goto error_getting_vma;

	memset(vma, 0, sizeof(*vma));
	INIT_LIST_HEAD(&vma->anon_vma_node);
	atomic_set(&vma->vm_usage, 1);
	if (file)
		get_file(file);
	vma->vm_file	= file;
	vma->vm_flags	= vm_flags;
	vma->vm_start	= addr;
	vma->vm_end	= addr + len;
	vma->vm_pgoff	= pgoff;

	vml->vma = vma;

	/* set up the mapping */
	if (file && vma->vm_flags & VM_SHARED)
		ret = do_mmap_shared_file(vma, len);
	else
		ret = do_mmap_private(vma, len);
	if (ret < 0)
		goto error;

	/* okay... we have a mapping; now we have to register it */
	result = (void *) vma->vm_start;

	if (vma->vm_flags & VM_MAPPED_COPY) {
		realalloc += kobjsize(result);
		askedalloc += len;
	}

	realalloc += kobjsize(vma);
	askedalloc += sizeof(*vma);

	current->mm->total_vm += len >> PAGE_SHIFT;

	add_nommu_vma(vma);

 shared:
	realalloc += kobjsize(vml);
	askedalloc += sizeof(*vml);

	vml->next = current->mm->context.vmlist;
	current->mm->context.vmlist = vml;

	up_write(&nommu_vma_sem);

	if (prot & PROT_EXEC)
		flush_icache_range((unsigned long) result,
				   (unsigned long) result + len);

#ifdef DEBUG
	printk("do_mmap:\n");
	show_process_blocks();
#endif

	return (unsigned long) result;

 error:
	up_write(&nommu_vma_sem);
	kfree(vml);
	if (vma) {
		fput(vma->vm_file);
		kfree(vma);
	}
	return ret;

 sharing_violation:
	up_write(&nommu_vma_sem);
	printk("Attempt to share mismatched mappings\n");
	kfree(vml);
	return -EINVAL;

 error_getting_vma:
	up_write(&nommu_vma_sem);
	kfree(vml);
	printk("Allocation of vma for %lu byte allocation from process %d failed\n",
	       len, current->pid);
	show_free_areas();
	return -ENOMEM;

 error_getting_vml:
	printk("Allocation of vml for %lu byte allocation from process %d failed\n",
	       len, current->pid);
	show_free_areas();
	return -ENOMEM;
}

/*
 * handle mapping disposal for uClinux
 */
static void put_vma(struct vm_area_struct *vma)
{
	if (vma) {
		down_write(&nommu_vma_sem);

		if (atomic_dec_and_test(&vma->vm_usage)) {
			delete_nommu_vma(vma);

			if (vma->vm_ops && vma->vm_ops->close)
				vma->vm_ops->close(vma);

			/* IO memory and memory shared directly out of the pagecache from
			 * ramfs/tmpfs mustn't be released here */
			if (vma->vm_flags & VM_MAPPED_COPY) {
				realalloc -= kobjsize((void *) vma->vm_start);
				askedalloc -= vma->vm_end - vma->vm_start;
				kfree((void *) vma->vm_start);
			}

			realalloc -= kobjsize(vma);
			askedalloc -= sizeof(*vma);

			if (vma->vm_file)
				fput(vma->vm_file);
			kfree(vma);
		}

		up_write(&nommu_vma_sem);
	}
}

int do_munmap(struct mm_struct *mm, unsigned long addr, size_t len)
{
	struct vm_list_struct *vml, **parent;
	unsigned long end = addr + len;

#ifdef DEBUG
	printk("do_munmap:\n");
#endif

	for (parent = &mm->context.vmlist; *parent; parent = &(*parent)->next)
		if ((*parent)->vma->vm_start == addr &&
		    ((len == 0) || ((*parent)->vma->vm_end == end)))
			goto found;

	printk("munmap of non-mmaped memory by process %d (%s): %p\n",
	       current->pid, current->comm, (void *) addr);
	return -EINVAL;

 found:
	vml = *parent;

	put_vma(vml->vma);

	*parent = vml->next;
	realalloc -= kobjsize(vml);
	askedalloc -= sizeof(*vml);
	kfree(vml);

	update_hiwater_vm(mm);
	mm->total_vm -= len >> PAGE_SHIFT;

#ifdef DEBUG
	show_process_blocks();
#endif

	return 0;
}

/* Release all mmaps. */
void exit_mmap(struct mm_struct * mm)
{
	struct vm_list_struct *tmp;

	if (mm) {
#ifdef DEBUG
		printk("Exit_mmap:\n");
#endif

		mm->total_vm = 0;

		while ((tmp = mm->context.vmlist)) {
			mm->context.vmlist = tmp->next;
			put_vma(tmp->vma);

			realalloc -= kobjsize(tmp);
			askedalloc -= sizeof(*tmp);
			kfree(tmp);
		}

#ifdef DEBUG
		show_process_blocks();
#endif
	}
}

asmlinkage long sys_munmap(unsigned long addr, size_t len)
{
	int ret;
	struct mm_struct *mm = current->mm;

	down_write(&mm->mmap_sem);
	ret = do_munmap(mm, addr, len);
	up_write(&mm->mmap_sem);
	return ret;
}

unsigned long do_brk(unsigned long addr, unsigned long len)
{
	return -ENOMEM;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 *
 * MREMAP_FIXED option added 5-Dec-1999 by Benjamin LaHaise
 * This option implies MREMAP_MAYMOVE.
 *
 * on uClinux, we only permit changing a mapping's size, and only as long as it stays within the
 * hole allocated by the kmalloc() call in do_mmap_pgoff() and the block is not shareable
 */
unsigned long do_mremap(unsigned long addr,
			unsigned long old_len, unsigned long new_len,
			unsigned long flags, unsigned long new_addr)
{
	struct vm_list_struct *vml = NULL;

	/* insanity checks first */
	if (new_len == 0)
		return (unsigned long) -EINVAL;

	if (flags & MREMAP_FIXED && new_addr != addr)
		return (unsigned long) -EINVAL;

	for (vml = current->mm->context.vmlist; vml; vml = vml->next)
		if (vml->vma->vm_start == addr)
			goto found;

	return (unsigned long) -EINVAL;

 found:
	if (vml->vma->vm_end != vml->vma->vm_start + old_len)
		return (unsigned long) -EFAULT;

	if (vml->vma->vm_flags & VM_MAYSHARE)
		return (unsigned long) -EPERM;

	if (new_len > kobjsize((void *) addr))
		return (unsigned long) -ENOMEM;

	/* all checks complete - do it */
	vml->vma->vm_end = vml->vma->vm_start + new_len;

	askedalloc -= old_len;
	askedalloc += new_len;

	return vml->vma->vm_start;
}

/*
 * Look up the first VMA which satisfies  addr < vm_end,  NULL if none
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_list_struct *vml;

	for (vml = mm->context.vmlist; vml; vml = vml->next)
		if (addr >= vml->vma->vm_start && addr < vml->vma->vm_end)
			return vml->vma;

	return NULL;
}

EXPORT_SYMBOL(find_vma);

struct page *follow_page(struct vm_area_struct *vma, unsigned long address,
			unsigned int foll_flags)
{
	return NULL;
}

struct vm_area_struct *find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	return NULL;
}

int remap_pfn_range(struct vm_area_struct *vma, unsigned long from,
		unsigned long to, unsigned long size, pgprot_t prot)
{
	vma->vm_start = vma->vm_pgoff << PAGE_SHIFT;
	return 0;
}

void swap_unplug_io_fn(struct backing_dev_info *bdi, struct page *page)
{
}

unsigned long arch_get_unmapped_area(struct file *file, unsigned long addr,
	unsigned long len, unsigned long pgoff, unsigned long flags)
{
	return -ENOMEM;
}

void arch_unmap_area(struct mm_struct *mm, unsigned long addr)
{
}

void unmap_mapping_range(struct address_space *mapping,
			 loff_t const holebegin, loff_t const holelen,
			 int even_cows)
{
}

/*
 * Check that a process has enough memory to allocate a new virtual
 * mapping. 0 means there is enough memory for the allocation to
 * succeed and -ENOMEM implies there is not.
 *
 * We currently support three overcommit policies, which are set via the
 * vm.overcommit_memory sysctl.  See Documentation/vm/overcommit-accounting
 *
 * Strict overcommit modes added 2002 Feb 26 by Alan Cox.
 * Additional code 2002 Jul 20 by Robert Love.
 *
 * cap_sys_admin is 1 if the process has admin privileges, 0 otherwise.
 *
 * Note this is a helper function intended to be used by LSMs which
 * wish to use this logic.
 */
int __vm_enough_memory(long pages, int cap_sys_admin)
{
	unsigned long free, allowed;

	vm_acct_memory(pages);

	/*
	 * Sometimes we want to use more memory than we have
	 */
	if (sysctl_overcommit_memory == OVERCOMMIT_ALWAYS)
		return 0;

	if (sysctl_overcommit_memory == OVERCOMMIT_GUESS) {
		unsigned long n;

		free = get_page_cache_size();
		free += nr_swap_pages;

		/*
		 * Any slabs which are created with the
		 * SLAB_RECLAIM_ACCOUNT flag claim to have contents
		 * which are reclaimable, under pressure.  The dentry
		 * cache and most inode caches should fall into this
		 */
		free += atomic_read(&slab_reclaim_pages);

		/*
		 * Leave the last 3% for root
		 */
		if (!cap_sys_admin)
			free -= free / 32;

		if (free > pages)
			return 0;

		/*
		 * nr_free_pages() is very expensive on large systems,
		 * only call if we're about to fail.
		 */
		n = nr_free_pages();
		if (!cap_sys_admin)
			n -= n / 32;
		free += n;

		if (free > pages)
			return 0;
		vm_unacct_memory(pages);
		return -ENOMEM;
	}

	allowed = totalram_pages * sysctl_overcommit_ratio / 100;
	/*
	 * Leave the last 3% for root
	 */
	if (!cap_sys_admin)
		allowed -= allowed / 32;
	allowed += total_swap_pages;

	/* Don't let a single process grow too big:
	   leave 3% of the size of this process for other processes */
	allowed -= current->mm->total_vm / 32;

	/*
	 * cast `allowed' as a signed long because vm_committed_space
	 * sometimes has a negative value
	 */
	if (atomic_read(&vm_committed_space) < (long)allowed)
		return 0;

	vm_unacct_memory(pages);

	return -ENOMEM;
}

int in_gate_area_no_task(unsigned long addr)
{
	return 0;
}

struct page *filemap_nopage(struct vm_area_struct *area,
			unsigned long address, int *type)
{
	BUG();
	return NULL;
}
