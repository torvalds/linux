/*
 * kexec.c - kexec system call
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#define pr_fmt(fmt)	"kexec: " fmt

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/kexec.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/ioport.h>
#include <linux/hardirq.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/utsname.h>
#include <linux/numa.h>
#include <linux/suspend.h>
#include <linux/device.h>
#include <linux/freezer.h>
#include <linux/pm.h>
#include <linux/cpu.h>
#include <linux/console.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>
#include <linux/syscore_ops.h>
#include <linux/compiler.h>
#include <linux/hugetlb.h>

#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/sections.h>

#include <crypto/hash.h>
#include <crypto/sha.h>

/* Per cpu memory for storing cpu states in case of system crash. */
note_buf_t __percpu *crash_notes;

/* vmcoreinfo stuff */
static unsigned char vmcoreinfo_data[VMCOREINFO_BYTES];
u32 vmcoreinfo_note[VMCOREINFO_NOTE_SIZE/4];
size_t vmcoreinfo_size;
size_t vmcoreinfo_max_size = sizeof(vmcoreinfo_data);

/* Flag to indicate we are going to kexec a new kernel */
bool kexec_in_progress = false;

/*
 * Declare these symbols weak so that if architecture provides a purgatory,
 * these will be overridden.
 */
char __weak kexec_purgatory[0];
size_t __weak kexec_purgatory_size = 0;

#ifdef CONFIG_KEXEC_FILE
static int kexec_calculate_store_digests(struct kimage *image);
#endif

/* Location of the reserved area for the crash kernel */
struct resource crashk_res = {
	.name  = "Crash kernel",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM
};
struct resource crashk_low_res = {
	.name  = "Crash kernel",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_MEM
};

int kexec_should_crash(struct task_struct *p)
{
	if (in_interrupt() || !p->pid || is_global_init(p) || panic_on_oops)
		return 1;
	return 0;
}

/*
 * When kexec transitions to the new kernel there is a one-to-one
 * mapping between physical and virtual addresses.  On processors
 * where you can disable the MMU this is trivial, and easy.  For
 * others it is still a simple predictable page table to setup.
 *
 * In that environment kexec copies the new kernel to its final
 * resting place.  This means I can only support memory whose
 * physical address can fit in an unsigned long.  In particular
 * addresses where (pfn << PAGE_SHIFT) > ULONG_MAX cannot be handled.
 * If the assembly stub has more restrictive requirements
 * KEXEC_SOURCE_MEMORY_LIMIT and KEXEC_DEST_MEMORY_LIMIT can be
 * defined more restrictively in <asm/kexec.h>.
 *
 * The code for the transition from the current kernel to the
 * the new kernel is placed in the control_code_buffer, whose size
 * is given by KEXEC_CONTROL_PAGE_SIZE.  In the best case only a single
 * page of memory is necessary, but some architectures require more.
 * Because this memory must be identity mapped in the transition from
 * virtual to physical addresses it must live in the range
 * 0 - TASK_SIZE, as only the user space mappings are arbitrarily
 * modifiable.
 *
 * The assembly stub in the control code buffer is passed a linked list
 * of descriptor pages detailing the source pages of the new kernel,
 * and the destination addresses of those source pages.  As this data
 * structure is not used in the context of the current OS, it must
 * be self-contained.
 *
 * The code has been made to work with highmem pages and will use a
 * destination page in its final resting place (if it happens
 * to allocate it).  The end product of this is that most of the
 * physical address space, and most of RAM can be used.
 *
 * Future directions include:
 *  - allocating a page table with the control code buffer identity
 *    mapped, to simplify machine_kexec and make kexec_on_panic more
 *    reliable.
 */

/*
 * KIMAGE_NO_DEST is an impossible destination address..., for
 * allocating pages whose destination address we do not care about.
 */
#define KIMAGE_NO_DEST (-1UL)

static int kimage_is_destination_range(struct kimage *image,
				       unsigned long start, unsigned long end);
static struct page *kimage_alloc_page(struct kimage *image,
				       gfp_t gfp_mask,
				       unsigned long dest);

static int copy_user_segment_list(struct kimage *image,
				  unsigned long nr_segments,
				  struct kexec_segment __user *segments)
{
	int ret;
	size_t segment_bytes;

	/* Read in the segments */
	image->nr_segments = nr_segments;
	segment_bytes = nr_segments * sizeof(*segments);
	ret = copy_from_user(image->segment, segments, segment_bytes);
	if (ret)
		ret = -EFAULT;

	return ret;
}

static int sanity_check_segment_list(struct kimage *image)
{
	int result, i;
	unsigned long nr_segments = image->nr_segments;

	/*
	 * Verify we have good destination addresses.  The caller is
	 * responsible for making certain we don't attempt to load
	 * the new image into invalid or reserved areas of RAM.  This
	 * just verifies it is an address we can use.
	 *
	 * Since the kernel does everything in page size chunks ensure
	 * the destination addresses are page aligned.  Too many
	 * special cases crop of when we don't do this.  The most
	 * insidious is getting overlapping destination addresses
	 * simply because addresses are changed to page size
	 * granularity.
	 */
	result = -EADDRNOTAVAIL;
	for (i = 0; i < nr_segments; i++) {
		unsigned long mstart, mend;

		mstart = image->segment[i].mem;
		mend   = mstart + image->segment[i].memsz;
		if ((mstart & ~PAGE_MASK) || (mend & ~PAGE_MASK))
			return result;
		if (mend >= KEXEC_DESTINATION_MEMORY_LIMIT)
			return result;
	}

	/* Verify our destination addresses do not overlap.
	 * If we alloed overlapping destination addresses
	 * through very weird things can happen with no
	 * easy explanation as one segment stops on another.
	 */
	result = -EINVAL;
	for (i = 0; i < nr_segments; i++) {
		unsigned long mstart, mend;
		unsigned long j;

		mstart = image->segment[i].mem;
		mend   = mstart + image->segment[i].memsz;
		for (j = 0; j < i; j++) {
			unsigned long pstart, pend;
			pstart = image->segment[j].mem;
			pend   = pstart + image->segment[j].memsz;
			/* Do the segments overlap ? */
			if ((mend > pstart) && (mstart < pend))
				return result;
		}
	}

	/* Ensure our buffer sizes are strictly less than
	 * our memory sizes.  This should always be the case,
	 * and it is easier to check up front than to be surprised
	 * later on.
	 */
	result = -EINVAL;
	for (i = 0; i < nr_segments; i++) {
		if (image->segment[i].bufsz > image->segment[i].memsz)
			return result;
	}

	/*
	 * Verify we have good destination addresses.  Normally
	 * the caller is responsible for making certain we don't
	 * attempt to load the new image into invalid or reserved
	 * areas of RAM.  But crash kernels are preloaded into a
	 * reserved area of ram.  We must ensure the addresses
	 * are in the reserved area otherwise preloading the
	 * kernel could corrupt things.
	 */

	if (image->type == KEXEC_TYPE_CRASH) {
		result = -EADDRNOTAVAIL;
		for (i = 0; i < nr_segments; i++) {
			unsigned long mstart, mend;

			mstart = image->segment[i].mem;
			mend = mstart + image->segment[i].memsz - 1;
			/* Ensure we are within the crash kernel limits */
			if ((mstart < crashk_res.start) ||
			    (mend > crashk_res.end))
				return result;
		}
	}

	return 0;
}

static struct kimage *do_kimage_alloc_init(void)
{
	struct kimage *image;

	/* Allocate a controlling structure */
	image = kzalloc(sizeof(*image), GFP_KERNEL);
	if (!image)
		return NULL;

	image->head = 0;
	image->entry = &image->head;
	image->last_entry = &image->head;
	image->control_page = ~0; /* By default this does not apply */
	image->type = KEXEC_TYPE_DEFAULT;

	/* Initialize the list of control pages */
	INIT_LIST_HEAD(&image->control_pages);

	/* Initialize the list of destination pages */
	INIT_LIST_HEAD(&image->dest_pages);

	/* Initialize the list of unusable pages */
	INIT_LIST_HEAD(&image->unusable_pages);

	return image;
}

static void kimage_free_page_list(struct list_head *list);

static int kimage_alloc_init(struct kimage **rimage, unsigned long entry,
			     unsigned long nr_segments,
			     struct kexec_segment __user *segments,
			     unsigned long flags)
{
	int ret;
	struct kimage *image;
	bool kexec_on_panic = flags & KEXEC_ON_CRASH;

	if (kexec_on_panic) {
		/* Verify we have a valid entry point */
		if ((entry < crashk_res.start) || (entry > crashk_res.end))
			return -EADDRNOTAVAIL;
	}

	/* Allocate and initialize a controlling structure */
	image = do_kimage_alloc_init();
	if (!image)
		return -ENOMEM;

	image->start = entry;

	ret = copy_user_segment_list(image, nr_segments, segments);
	if (ret)
		goto out_free_image;

	ret = sanity_check_segment_list(image);
	if (ret)
		goto out_free_image;

	 /* Enable the special crash kernel control page allocation policy. */
	if (kexec_on_panic) {
		image->control_page = crashk_res.start;
		image->type = KEXEC_TYPE_CRASH;
	}

	/*
	 * Find a location for the control code buffer, and add it
	 * the vector of segments so that it's pages will also be
	 * counted as destination pages.
	 */
	ret = -ENOMEM;
	image->control_code_page = kimage_alloc_control_pages(image,
					   get_order(KEXEC_CONTROL_PAGE_SIZE));
	if (!image->control_code_page) {
		pr_err("Could not allocate control_code_buffer\n");
		goto out_free_image;
	}

	if (!kexec_on_panic) {
		image->swap_page = kimage_alloc_control_pages(image, 0);
		if (!image->swap_page) {
			pr_err("Could not allocate swap buffer\n");
			goto out_free_control_pages;
		}
	}

	*rimage = image;
	return 0;
out_free_control_pages:
	kimage_free_page_list(&image->control_pages);
out_free_image:
	kfree(image);
	return ret;
}

#ifdef CONFIG_KEXEC_FILE
static int copy_file_from_fd(int fd, void **buf, unsigned long *buf_len)
{
	struct fd f = fdget(fd);
	int ret;
	struct kstat stat;
	loff_t pos;
	ssize_t bytes = 0;

	if (!f.file)
		return -EBADF;

	ret = vfs_getattr(&f.file->f_path, &stat);
	if (ret)
		goto out;

	if (stat.size > INT_MAX) {
		ret = -EFBIG;
		goto out;
	}

	/* Don't hand 0 to vmalloc, it whines. */
	if (stat.size == 0) {
		ret = -EINVAL;
		goto out;
	}

	*buf = vmalloc(stat.size);
	if (!*buf) {
		ret = -ENOMEM;
		goto out;
	}

	pos = 0;
	while (pos < stat.size) {
		bytes = kernel_read(f.file, pos, (char *)(*buf) + pos,
				    stat.size - pos);
		if (bytes < 0) {
			vfree(*buf);
			ret = bytes;
			goto out;
		}

		if (bytes == 0)
			break;
		pos += bytes;
	}

	if (pos != stat.size) {
		ret = -EBADF;
		vfree(*buf);
		goto out;
	}

	*buf_len = pos;
out:
	fdput(f);
	return ret;
}

/* Architectures can provide this probe function */
int __weak arch_kexec_kernel_image_probe(struct kimage *image, void *buf,
					 unsigned long buf_len)
{
	return -ENOEXEC;
}

void * __weak arch_kexec_kernel_image_load(struct kimage *image)
{
	return ERR_PTR(-ENOEXEC);
}

void __weak arch_kimage_file_post_load_cleanup(struct kimage *image)
{
}

int __weak arch_kexec_kernel_verify_sig(struct kimage *image, void *buf,
					unsigned long buf_len)
{
	return -EKEYREJECTED;
}

/* Apply relocations of type RELA */
int __weak
arch_kexec_apply_relocations_add(const Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
				 unsigned int relsec)
{
	pr_err("RELA relocation unsupported.\n");
	return -ENOEXEC;
}

/* Apply relocations of type REL */
int __weak
arch_kexec_apply_relocations(const Elf_Ehdr *ehdr, Elf_Shdr *sechdrs,
			     unsigned int relsec)
{
	pr_err("REL relocation unsupported.\n");
	return -ENOEXEC;
}

/*
 * Free up memory used by kernel, initrd, and comand line. This is temporary
 * memory allocation which is not needed any more after these buffers have
 * been loaded into separate segments and have been copied elsewhere.
 */
static void kimage_file_post_load_cleanup(struct kimage *image)
{
	struct purgatory_info *pi = &image->purgatory_info;

	vfree(image->kernel_buf);
	image->kernel_buf = NULL;

	vfree(image->initrd_buf);
	image->initrd_buf = NULL;

	kfree(image->cmdline_buf);
	image->cmdline_buf = NULL;

	vfree(pi->purgatory_buf);
	pi->purgatory_buf = NULL;

	vfree(pi->sechdrs);
	pi->sechdrs = NULL;

	/* See if architecture has anything to cleanup post load */
	arch_kimage_file_post_load_cleanup(image);

	/*
	 * Above call should have called into bootloader to free up
	 * any data stored in kimage->image_loader_data. It should
	 * be ok now to free it up.
	 */
	kfree(image->image_loader_data);
	image->image_loader_data = NULL;
}

/*
 * In file mode list of segments is prepared by kernel. Copy relevant
 * data from user space, do error checking, prepare segment list
 */
static int
kimage_file_prepare_segments(struct kimage *image, int kernel_fd, int initrd_fd,
			     const char __user *cmdline_ptr,
			     unsigned long cmdline_len, unsigned flags)
{
	int ret = 0;
	void *ldata;

	ret = copy_file_from_fd(kernel_fd, &image->kernel_buf,
				&image->kernel_buf_len);
	if (ret)
		return ret;

	/* Call arch image probe handlers */
	ret = arch_kexec_kernel_image_probe(image, image->kernel_buf,
					    image->kernel_buf_len);

	if (ret)
		goto out;

#ifdef CONFIG_KEXEC_VERIFY_SIG
	ret = arch_kexec_kernel_verify_sig(image, image->kernel_buf,
					   image->kernel_buf_len);
	if (ret) {
		pr_debug("kernel signature verification failed.\n");
		goto out;
	}
	pr_debug("kernel signature verification successful.\n");
#endif
	/* It is possible that there no initramfs is being loaded */
	if (!(flags & KEXEC_FILE_NO_INITRAMFS)) {
		ret = copy_file_from_fd(initrd_fd, &image->initrd_buf,
					&image->initrd_buf_len);
		if (ret)
			goto out;
	}

	if (cmdline_len) {
		image->cmdline_buf = kzalloc(cmdline_len, GFP_KERNEL);
		if (!image->cmdline_buf) {
			ret = -ENOMEM;
			goto out;
		}

		ret = copy_from_user(image->cmdline_buf, cmdline_ptr,
				     cmdline_len);
		if (ret) {
			ret = -EFAULT;
			goto out;
		}

		image->cmdline_buf_len = cmdline_len;

		/* command line should be a string with last byte null */
		if (image->cmdline_buf[cmdline_len - 1] != '\0') {
			ret = -EINVAL;
			goto out;
		}
	}

	/* Call arch image load handlers */
	ldata = arch_kexec_kernel_image_load(image);

	if (IS_ERR(ldata)) {
		ret = PTR_ERR(ldata);
		goto out;
	}

	image->image_loader_data = ldata;
out:
	/* In case of error, free up all allocated memory in this function */
	if (ret)
		kimage_file_post_load_cleanup(image);
	return ret;
}

static int
kimage_file_alloc_init(struct kimage **rimage, int kernel_fd,
		       int initrd_fd, const char __user *cmdline_ptr,
		       unsigned long cmdline_len, unsigned long flags)
{
	int ret;
	struct kimage *image;
	bool kexec_on_panic = flags & KEXEC_FILE_ON_CRASH;

	image = do_kimage_alloc_init();
	if (!image)
		return -ENOMEM;

	image->file_mode = 1;

	if (kexec_on_panic) {
		/* Enable special crash kernel control page alloc policy. */
		image->control_page = crashk_res.start;
		image->type = KEXEC_TYPE_CRASH;
	}

	ret = kimage_file_prepare_segments(image, kernel_fd, initrd_fd,
					   cmdline_ptr, cmdline_len, flags);
	if (ret)
		goto out_free_image;

	ret = sanity_check_segment_list(image);
	if (ret)
		goto out_free_post_load_bufs;

	ret = -ENOMEM;
	image->control_code_page = kimage_alloc_control_pages(image,
					   get_order(KEXEC_CONTROL_PAGE_SIZE));
	if (!image->control_code_page) {
		pr_err("Could not allocate control_code_buffer\n");
		goto out_free_post_load_bufs;
	}

	if (!kexec_on_panic) {
		image->swap_page = kimage_alloc_control_pages(image, 0);
		if (!image->swap_page) {
			pr_err(KERN_ERR "Could not allocate swap buffer\n");
			goto out_free_control_pages;
		}
	}

	*rimage = image;
	return 0;
out_free_control_pages:
	kimage_free_page_list(&image->control_pages);
out_free_post_load_bufs:
	kimage_file_post_load_cleanup(image);
out_free_image:
	kfree(image);
	return ret;
}
#else /* CONFIG_KEXEC_FILE */
static inline void kimage_file_post_load_cleanup(struct kimage *image) { }
#endif /* CONFIG_KEXEC_FILE */

static int kimage_is_destination_range(struct kimage *image,
					unsigned long start,
					unsigned long end)
{
	unsigned long i;

	for (i = 0; i < image->nr_segments; i++) {
		unsigned long mstart, mend;

		mstart = image->segment[i].mem;
		mend = mstart + image->segment[i].memsz;
		if ((end > mstart) && (start < mend))
			return 1;
	}

	return 0;
}

static struct page *kimage_alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	struct page *pages;

	pages = alloc_pages(gfp_mask, order);
	if (pages) {
		unsigned int count, i;
		pages->mapping = NULL;
		set_page_private(pages, order);
		count = 1 << order;
		for (i = 0; i < count; i++)
			SetPageReserved(pages + i);
	}

	return pages;
}

static void kimage_free_pages(struct page *page)
{
	unsigned int order, count, i;

	order = page_private(page);
	count = 1 << order;
	for (i = 0; i < count; i++)
		ClearPageReserved(page + i);
	__free_pages(page, order);
}

static void kimage_free_page_list(struct list_head *list)
{
	struct list_head *pos, *next;

	list_for_each_safe(pos, next, list) {
		struct page *page;

		page = list_entry(pos, struct page, lru);
		list_del(&page->lru);
		kimage_free_pages(page);
	}
}

static struct page *kimage_alloc_normal_control_pages(struct kimage *image,
							unsigned int order)
{
	/* Control pages are special, they are the intermediaries
	 * that are needed while we copy the rest of the pages
	 * to their final resting place.  As such they must
	 * not conflict with either the destination addresses
	 * or memory the kernel is already using.
	 *
	 * The only case where we really need more than one of
	 * these are for architectures where we cannot disable
	 * the MMU and must instead generate an identity mapped
	 * page table for all of the memory.
	 *
	 * At worst this runs in O(N) of the image size.
	 */
	struct list_head extra_pages;
	struct page *pages;
	unsigned int count;

	count = 1 << order;
	INIT_LIST_HEAD(&extra_pages);

	/* Loop while I can allocate a page and the page allocated
	 * is a destination page.
	 */
	do {
		unsigned long pfn, epfn, addr, eaddr;

		pages = kimage_alloc_pages(GFP_KERNEL, order);
		if (!pages)
			break;
		pfn   = page_to_pfn(pages);
		epfn  = pfn + count;
		addr  = pfn << PAGE_SHIFT;
		eaddr = epfn << PAGE_SHIFT;
		if ((epfn >= (KEXEC_CONTROL_MEMORY_LIMIT >> PAGE_SHIFT)) ||
			      kimage_is_destination_range(image, addr, eaddr)) {
			list_add(&pages->lru, &extra_pages);
			pages = NULL;
		}
	} while (!pages);

	if (pages) {
		/* Remember the allocated page... */
		list_add(&pages->lru, &image->control_pages);

		/* Because the page is already in it's destination
		 * location we will never allocate another page at
		 * that address.  Therefore kimage_alloc_pages
		 * will not return it (again) and we don't need
		 * to give it an entry in image->segment[].
		 */
	}
	/* Deal with the destination pages I have inadvertently allocated.
	 *
	 * Ideally I would convert multi-page allocations into single
	 * page allocations, and add everything to image->dest_pages.
	 *
	 * For now it is simpler to just free the pages.
	 */
	kimage_free_page_list(&extra_pages);

	return pages;
}

static struct page *kimage_alloc_crash_control_pages(struct kimage *image,
						      unsigned int order)
{
	/* Control pages are special, they are the intermediaries
	 * that are needed while we copy the rest of the pages
	 * to their final resting place.  As such they must
	 * not conflict with either the destination addresses
	 * or memory the kernel is already using.
	 *
	 * Control pages are also the only pags we must allocate
	 * when loading a crash kernel.  All of the other pages
	 * are specified by the segments and we just memcpy
	 * into them directly.
	 *
	 * The only case where we really need more than one of
	 * these are for architectures where we cannot disable
	 * the MMU and must instead generate an identity mapped
	 * page table for all of the memory.
	 *
	 * Given the low demand this implements a very simple
	 * allocator that finds the first hole of the appropriate
	 * size in the reserved memory region, and allocates all
	 * of the memory up to and including the hole.
	 */
	unsigned long hole_start, hole_end, size;
	struct page *pages;

	pages = NULL;
	size = (1 << order) << PAGE_SHIFT;
	hole_start = (image->control_page + (size - 1)) & ~(size - 1);
	hole_end   = hole_start + size - 1;
	while (hole_end <= crashk_res.end) {
		unsigned long i;

		if (hole_end > KEXEC_CRASH_CONTROL_MEMORY_LIMIT)
			break;
		/* See if I overlap any of the segments */
		for (i = 0; i < image->nr_segments; i++) {
			unsigned long mstart, mend;

			mstart = image->segment[i].mem;
			mend   = mstart + image->segment[i].memsz - 1;
			if ((hole_end >= mstart) && (hole_start <= mend)) {
				/* Advance the hole to the end of the segment */
				hole_start = (mend + (size - 1)) & ~(size - 1);
				hole_end   = hole_start + size - 1;
				break;
			}
		}
		/* If I don't overlap any segments I have found my hole! */
		if (i == image->nr_segments) {
			pages = pfn_to_page(hole_start >> PAGE_SHIFT);
			break;
		}
	}
	if (pages)
		image->control_page = hole_end;

	return pages;
}


struct page *kimage_alloc_control_pages(struct kimage *image,
					 unsigned int order)
{
	struct page *pages = NULL;

	switch (image->type) {
	case KEXEC_TYPE_DEFAULT:
		pages = kimage_alloc_normal_control_pages(image, order);
		break;
	case KEXEC_TYPE_CRASH:
		pages = kimage_alloc_crash_control_pages(image, order);
		break;
	}

	return pages;
}

static int kimage_add_entry(struct kimage *image, kimage_entry_t entry)
{
	if (*image->entry != 0)
		image->entry++;

	if (image->entry == image->last_entry) {
		kimage_entry_t *ind_page;
		struct page *page;

		page = kimage_alloc_page(image, GFP_KERNEL, KIMAGE_NO_DEST);
		if (!page)
			return -ENOMEM;

		ind_page = page_address(page);
		*image->entry = virt_to_phys(ind_page) | IND_INDIRECTION;
		image->entry = ind_page;
		image->last_entry = ind_page +
				      ((PAGE_SIZE/sizeof(kimage_entry_t)) - 1);
	}
	*image->entry = entry;
	image->entry++;
	*image->entry = 0;

	return 0;
}

static int kimage_set_destination(struct kimage *image,
				   unsigned long destination)
{
	int result;

	destination &= PAGE_MASK;
	result = kimage_add_entry(image, destination | IND_DESTINATION);
	if (result == 0)
		image->destination = destination;

	return result;
}


static int kimage_add_page(struct kimage *image, unsigned long page)
{
	int result;

	page &= PAGE_MASK;
	result = kimage_add_entry(image, page | IND_SOURCE);
	if (result == 0)
		image->destination += PAGE_SIZE;

	return result;
}


static void kimage_free_extra_pages(struct kimage *image)
{
	/* Walk through and free any extra destination pages I may have */
	kimage_free_page_list(&image->dest_pages);

	/* Walk through and free any unusable pages I have cached */
	kimage_free_page_list(&image->unusable_pages);

}
static void kimage_terminate(struct kimage *image)
{
	if (*image->entry != 0)
		image->entry++;

	*image->entry = IND_DONE;
}

#define for_each_kimage_entry(image, ptr, entry) \
	for (ptr = &image->head; (entry = *ptr) && !(entry & IND_DONE); \
		ptr = (entry & IND_INDIRECTION) ? \
			phys_to_virt((entry & PAGE_MASK)) : ptr + 1)

static void kimage_free_entry(kimage_entry_t entry)
{
	struct page *page;

	page = pfn_to_page(entry >> PAGE_SHIFT);
	kimage_free_pages(page);
}

static void kimage_free(struct kimage *image)
{
	kimage_entry_t *ptr, entry;
	kimage_entry_t ind = 0;

	if (!image)
		return;

	kimage_free_extra_pages(image);
	for_each_kimage_entry(image, ptr, entry) {
		if (entry & IND_INDIRECTION) {
			/* Free the previous indirection page */
			if (ind & IND_INDIRECTION)
				kimage_free_entry(ind);
			/* Save this indirection page until we are
			 * done with it.
			 */
			ind = entry;
		} else if (entry & IND_SOURCE)
			kimage_free_entry(entry);
	}
	/* Free the final indirection page */
	if (ind & IND_INDIRECTION)
		kimage_free_entry(ind);

	/* Handle any machine specific cleanup */
	machine_kexec_cleanup(image);

	/* Free the kexec control pages... */
	kimage_free_page_list(&image->control_pages);

	/*
	 * Free up any temporary buffers allocated. This might hit if
	 * error occurred much later after buffer allocation.
	 */
	if (image->file_mode)
		kimage_file_post_load_cleanup(image);

	kfree(image);
}

static kimage_entry_t *kimage_dst_used(struct kimage *image,
					unsigned long page)
{
	kimage_entry_t *ptr, entry;
	unsigned long destination = 0;

	for_each_kimage_entry(image, ptr, entry) {
		if (entry & IND_DESTINATION)
			destination = entry & PAGE_MASK;
		else if (entry & IND_SOURCE) {
			if (page == destination)
				return ptr;
			destination += PAGE_SIZE;
		}
	}

	return NULL;
}

static struct page *kimage_alloc_page(struct kimage *image,
					gfp_t gfp_mask,
					unsigned long destination)
{
	/*
	 * Here we implement safeguards to ensure that a source page
	 * is not copied to its destination page before the data on
	 * the destination page is no longer useful.
	 *
	 * To do this we maintain the invariant that a source page is
	 * either its own destination page, or it is not a
	 * destination page at all.
	 *
	 * That is slightly stronger than required, but the proof
	 * that no problems will not occur is trivial, and the
	 * implementation is simply to verify.
	 *
	 * When allocating all pages normally this algorithm will run
	 * in O(N) time, but in the worst case it will run in O(N^2)
	 * time.   If the runtime is a problem the data structures can
	 * be fixed.
	 */
	struct page *page;
	unsigned long addr;

	/*
	 * Walk through the list of destination pages, and see if I
	 * have a match.
	 */
	list_for_each_entry(page, &image->dest_pages, lru) {
		addr = page_to_pfn(page) << PAGE_SHIFT;
		if (addr == destination) {
			list_del(&page->lru);
			return page;
		}
	}
	page = NULL;
	while (1) {
		kimage_entry_t *old;

		/* Allocate a page, if we run out of memory give up */
		page = kimage_alloc_pages(gfp_mask, 0);
		if (!page)
			return NULL;
		/* If the page cannot be used file it away */
		if (page_to_pfn(page) >
				(KEXEC_SOURCE_MEMORY_LIMIT >> PAGE_SHIFT)) {
			list_add(&page->lru, &image->unusable_pages);
			continue;
		}
		addr = page_to_pfn(page) << PAGE_SHIFT;

		/* If it is the destination page we want use it */
		if (addr == destination)
			break;

		/* If the page is not a destination page use it */
		if (!kimage_is_destination_range(image, addr,
						  addr + PAGE_SIZE))
			break;

		/*
		 * I know that the page is someones destination page.
		 * See if there is already a source page for this
		 * destination page.  And if so swap the source pages.
		 */
		old = kimage_dst_used(image, addr);
		if (old) {
			/* If so move it */
			unsigned long old_addr;
			struct page *old_page;

			old_addr = *old & PAGE_MASK;
			old_page = pfn_to_page(old_addr >> PAGE_SHIFT);
			copy_highpage(page, old_page);
			*old = addr | (*old & ~PAGE_MASK);

			/* The old page I have found cannot be a
			 * destination page, so return it if it's
			 * gfp_flags honor the ones passed in.
			 */
			if (!(gfp_mask & __GFP_HIGHMEM) &&
			    PageHighMem(old_page)) {
				kimage_free_pages(old_page);
				continue;
			}
			addr = old_addr;
			page = old_page;
			break;
		} else {
			/* Place the page on the destination list I
			 * will use it later.
			 */
			list_add(&page->lru, &image->dest_pages);
		}
	}

	return page;
}

static int kimage_load_normal_segment(struct kimage *image,
					 struct kexec_segment *segment)
{
	unsigned long maddr;
	size_t ubytes, mbytes;
	int result;
	unsigned char __user *buf = NULL;
	unsigned char *kbuf = NULL;

	result = 0;
	if (image->file_mode)
		kbuf = segment->kbuf;
	else
		buf = segment->buf;
	ubytes = segment->bufsz;
	mbytes = segment->memsz;
	maddr = segment->mem;

	result = kimage_set_destination(image, maddr);
	if (result < 0)
		goto out;

	while (mbytes) {
		struct page *page;
		char *ptr;
		size_t uchunk, mchunk;

		page = kimage_alloc_page(image, GFP_HIGHUSER, maddr);
		if (!page) {
			result  = -ENOMEM;
			goto out;
		}
		result = kimage_add_page(image, page_to_pfn(page)
								<< PAGE_SHIFT);
		if (result < 0)
			goto out;

		ptr = kmap(page);
		/* Start with a clear page */
		clear_page(ptr);
		ptr += maddr & ~PAGE_MASK;
		mchunk = min_t(size_t, mbytes,
				PAGE_SIZE - (maddr & ~PAGE_MASK));
		uchunk = min(ubytes, mchunk);

		/* For file based kexec, source pages are in kernel memory */
		if (image->file_mode)
			memcpy(ptr, kbuf, uchunk);
		else
			result = copy_from_user(ptr, buf, uchunk);
		kunmap(page);
		if (result) {
			result = -EFAULT;
			goto out;
		}
		ubytes -= uchunk;
		maddr  += mchunk;
		if (image->file_mode)
			kbuf += mchunk;
		else
			buf += mchunk;
		mbytes -= mchunk;
	}
out:
	return result;
}

static int kimage_load_crash_segment(struct kimage *image,
					struct kexec_segment *segment)
{
	/* For crash dumps kernels we simply copy the data from
	 * user space to it's destination.
	 * We do things a page at a time for the sake of kmap.
	 */
	unsigned long maddr;
	size_t ubytes, mbytes;
	int result;
	unsigned char __user *buf = NULL;
	unsigned char *kbuf = NULL;

	result = 0;
	if (image->file_mode)
		kbuf = segment->kbuf;
	else
		buf = segment->buf;
	ubytes = segment->bufsz;
	mbytes = segment->memsz;
	maddr = segment->mem;
	while (mbytes) {
		struct page *page;
		char *ptr;
		size_t uchunk, mchunk;

		page = pfn_to_page(maddr >> PAGE_SHIFT);
		if (!page) {
			result  = -ENOMEM;
			goto out;
		}
		ptr = kmap(page);
		ptr += maddr & ~PAGE_MASK;
		mchunk = min_t(size_t, mbytes,
				PAGE_SIZE - (maddr & ~PAGE_MASK));
		uchunk = min(ubytes, mchunk);
		if (mchunk > uchunk) {
			/* Zero the trailing part of the page */
			memset(ptr + uchunk, 0, mchunk - uchunk);
		}

		/* For file based kexec, source pages are in kernel memory */
		if (image->file_mode)
			memcpy(ptr, kbuf, uchunk);
		else
			result = copy_from_user(ptr, buf, uchunk);
		kexec_flush_icache_page(page);
		kunmap(page);
		if (result) {
			result = -EFAULT;
			goto out;
		}
		ubytes -= uchunk;
		maddr  += mchunk;
		if (image->file_mode)
			kbuf += mchunk;
		else
			buf += mchunk;
		mbytes -= mchunk;
	}
out:
	return result;
}

static int kimage_load_segment(struct kimage *image,
				struct kexec_segment *segment)
{
	int result = -ENOMEM;

	switch (image->type) {
	case KEXEC_TYPE_DEFAULT:
		result = kimage_load_normal_segment(image, segment);
		break;
	case KEXEC_TYPE_CRASH:
		result = kimage_load_crash_segment(image, segment);
		break;
	}

	return result;
}

/*
 * Exec Kernel system call: for obvious reasons only root may call it.
 *
 * This call breaks up into three pieces.
 * - A generic part which loads the new kernel from the current
 *   address space, and very carefully places the data in the
 *   allocated pages.
 *
 * - A generic part that interacts with the kernel and tells all of
 *   the devices to shut down.  Preventing on-going dmas, and placing
 *   the devices in a consistent state so a later kernel can
 *   reinitialize them.
 *
 * - A machine specific part that includes the syscall number
 *   and then copies the image to it's final destination.  And
 *   jumps into the image at entry.
 *
 * kexec does not sync, or unmount filesystems so if you need
 * that to happen you need to do that yourself.
 */
struct kimage *kexec_image;
struct kimage *kexec_crash_image;
int kexec_load_disabled;

static DEFINE_MUTEX(kexec_mutex);

SYSCALL_DEFINE4(kexec_load, unsigned long, entry, unsigned long, nr_segments,
		struct kexec_segment __user *, segments, unsigned long, flags)
{
	struct kimage **dest_image, *image;
	int result;

	/* We only trust the superuser with rebooting the system. */
	if (!capable(CAP_SYS_BOOT) || kexec_load_disabled)
		return -EPERM;

	/*
	 * Verify we have a legal set of flags
	 * This leaves us room for future extensions.
	 */
	if ((flags & KEXEC_FLAGS) != (flags & ~KEXEC_ARCH_MASK))
		return -EINVAL;

	/* Verify we are on the appropriate architecture */
	if (((flags & KEXEC_ARCH_MASK) != KEXEC_ARCH) &&
		((flags & KEXEC_ARCH_MASK) != KEXEC_ARCH_DEFAULT))
		return -EINVAL;

	/* Put an artificial cap on the number
	 * of segments passed to kexec_load.
	 */
	if (nr_segments > KEXEC_SEGMENT_MAX)
		return -EINVAL;

	image = NULL;
	result = 0;

	/* Because we write directly to the reserved memory
	 * region when loading crash kernels we need a mutex here to
	 * prevent multiple crash  kernels from attempting to load
	 * simultaneously, and to prevent a crash kernel from loading
	 * over the top of a in use crash kernel.
	 *
	 * KISS: always take the mutex.
	 */
	if (!mutex_trylock(&kexec_mutex))
		return -EBUSY;

	dest_image = &kexec_image;
	if (flags & KEXEC_ON_CRASH)
		dest_image = &kexec_crash_image;
	if (nr_segments > 0) {
		unsigned long i;

		/* Loading another kernel to reboot into */
		if ((flags & KEXEC_ON_CRASH) == 0)
			result = kimage_alloc_init(&image, entry, nr_segments,
						   segments, flags);
		/* Loading another kernel to switch to if this one crashes */
		else if (flags & KEXEC_ON_CRASH) {
			/* Free any current crash dump kernel before
			 * we corrupt it.
			 */
			kimage_free(xchg(&kexec_crash_image, NULL));
			result = kimage_alloc_init(&image, entry, nr_segments,
						   segments, flags);
			crash_map_reserved_pages();
		}
		if (result)
			goto out;

		if (flags & KEXEC_PRESERVE_CONTEXT)
			image->preserve_context = 1;
		result = machine_kexec_prepare(image);
		if (result)
			goto out;

		for (i = 0; i < nr_segments; i++) {
			result = kimage_load_segment(image, &image->segment[i]);
			if (result)
				goto out;
		}
		kimage_terminate(image);
		if (flags & KEXEC_ON_CRASH)
			crash_unmap_reserved_pages();
	}
	/* Install the new kernel, and  Uninstall the old */
	image = xchg(dest_image, image);

out:
	mutex_unlock(&kexec_mutex);
	kimage_free(image);

	return result;
}

/*
 * Add and remove page tables for crashkernel memory
 *
 * Provide an empty default implementation here -- architecture
 * code may override this
 */
void __weak crash_map_reserved_pages(void)
{}

void __weak crash_unmap_reserved_pages(void)
{}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(kexec_load, compat_ulong_t, entry,
		       compat_ulong_t, nr_segments,
		       struct compat_kexec_segment __user *, segments,
		       compat_ulong_t, flags)
{
	struct compat_kexec_segment in;
	struct kexec_segment out, __user *ksegments;
	unsigned long i, result;

	/* Don't allow clients that don't understand the native
	 * architecture to do anything.
	 */
	if ((flags & KEXEC_ARCH_MASK) == KEXEC_ARCH_DEFAULT)
		return -EINVAL;

	if (nr_segments > KEXEC_SEGMENT_MAX)
		return -EINVAL;

	ksegments = compat_alloc_user_space(nr_segments * sizeof(out));
	for (i = 0; i < nr_segments; i++) {
		result = copy_from_user(&in, &segments[i], sizeof(in));
		if (result)
			return -EFAULT;

		out.buf   = compat_ptr(in.buf);
		out.bufsz = in.bufsz;
		out.mem   = in.mem;
		out.memsz = in.memsz;

		result = copy_to_user(&ksegments[i], &out, sizeof(out));
		if (result)
			return -EFAULT;
	}

	return sys_kexec_load(entry, nr_segments, ksegments, flags);
}
#endif

#ifdef CONFIG_KEXEC_FILE
SYSCALL_DEFINE5(kexec_file_load, int, kernel_fd, int, initrd_fd,
		unsigned long, cmdline_len, const char __user *, cmdline_ptr,
		unsigned long, flags)
{
	int ret = 0, i;
	struct kimage **dest_image, *image;

	/* We only trust the superuser with rebooting the system. */
	if (!capable(CAP_SYS_BOOT) || kexec_load_disabled)
		return -EPERM;

	/* Make sure we have a legal set of flags */
	if (flags != (flags & KEXEC_FILE_FLAGS))
		return -EINVAL;

	image = NULL;

	if (!mutex_trylock(&kexec_mutex))
		return -EBUSY;

	dest_image = &kexec_image;
	if (flags & KEXEC_FILE_ON_CRASH)
		dest_image = &kexec_crash_image;

	if (flags & KEXEC_FILE_UNLOAD)
		goto exchange;

	/*
	 * In case of crash, new kernel gets loaded in reserved region. It is
	 * same memory where old crash kernel might be loaded. Free any
	 * current crash dump kernel before we corrupt it.
	 */
	if (flags & KEXEC_FILE_ON_CRASH)
		kimage_free(xchg(&kexec_crash_image, NULL));

	ret = kimage_file_alloc_init(&image, kernel_fd, initrd_fd, cmdline_ptr,
				     cmdline_len, flags);
	if (ret)
		goto out;

	ret = machine_kexec_prepare(image);
	if (ret)
		goto out;

	ret = kexec_calculate_store_digests(image);
	if (ret)
		goto out;

	for (i = 0; i < image->nr_segments; i++) {
		struct kexec_segment *ksegment;

		ksegment = &image->segment[i];
		pr_debug("Loading segment %d: buf=0x%p bufsz=0x%zx mem=0x%lx memsz=0x%zx\n",
			 i, ksegment->buf, ksegment->bufsz, ksegment->mem,
			 ksegment->memsz);

		ret = kimage_load_segment(image, &image->segment[i]);
		if (ret)
			goto out;
	}

	kimage_terminate(image);

	/*
	 * Free up any temporary buffers allocated which are not needed
	 * after image has been loaded
	 */
	kimage_file_post_load_cleanup(image);
exchange:
	image = xchg(dest_image, image);
out:
	mutex_unlock(&kexec_mutex);
	kimage_free(image);
	return ret;
}

#endif /* CONFIG_KEXEC_FILE */

void crash_kexec(struct pt_regs *regs)
{
	/* Take the kexec_mutex here to prevent sys_kexec_load
	 * running on one cpu from replacing the crash kernel
	 * we are using after a panic on a different cpu.
	 *
	 * If the crash kernel was not located in a fixed area
	 * of memory the xchg(&kexec_crash_image) would be
	 * sufficient.  But since I reuse the memory...
	 */
	if (mutex_trylock(&kexec_mutex)) {
		if (kexec_crash_image) {
			struct pt_regs fixed_regs;

			crash_setup_regs(&fixed_regs, regs);
			crash_save_vmcoreinfo();
			machine_crash_shutdown(&fixed_regs);
			machine_kexec(kexec_crash_image);
		}
		mutex_unlock(&kexec_mutex);
	}
}

size_t crash_get_memory_size(void)
{
	size_t size = 0;
	mutex_lock(&kexec_mutex);
	if (crashk_res.end != crashk_res.start)
		size = resource_size(&crashk_res);
	mutex_unlock(&kexec_mutex);
	return size;
}

void __weak crash_free_reserved_phys_range(unsigned long begin,
					   unsigned long end)
{
	unsigned long addr;

	for (addr = begin; addr < end; addr += PAGE_SIZE)
		free_reserved_page(pfn_to_page(addr >> PAGE_SHIFT));
}

int crash_shrink_memory(unsigned long new_size)
{
	int ret = 0;
	unsigned long start, end;
	unsigned long old_size;
	struct resource *ram_res;

	mutex_lock(&kexec_mutex);

	if (kexec_crash_image) {
		ret = -ENOENT;
		goto unlock;
	}
	start = crashk_res.start;
	end = crashk_res.end;
	old_size = (end == 0) ? 0 : end - start + 1;
	if (new_size >= old_size) {
		ret = (new_size == old_size) ? 0 : -EINVAL;
		goto unlock;
	}

	ram_res = kzalloc(sizeof(*ram_res), GFP_KERNEL);
	if (!ram_res) {
		ret = -ENOMEM;
		goto unlock;
	}

	start = roundup(start, KEXEC_CRASH_MEM_ALIGN);
	end = roundup(start + new_size, KEXEC_CRASH_MEM_ALIGN);

	crash_map_reserved_pages();
	crash_free_reserved_phys_range(end, crashk_res.end);

	if ((start == end) && (crashk_res.parent != NULL))
		release_resource(&crashk_res);

	ram_res->start = end;
	ram_res->end = crashk_res.end;
	ram_res->flags = IORESOURCE_BUSY | IORESOURCE_MEM;
	ram_res->name = "System RAM";

	crashk_res.end = end - 1;

	insert_resource(&iomem_resource, ram_res);
	crash_unmap_reserved_pages();

unlock:
	mutex_unlock(&kexec_mutex);
	return ret;
}

static u32 *append_elf_note(u32 *buf, char *name, unsigned type, void *data,
			    size_t data_len)
{
	struct elf_note note;

	note.n_namesz = strlen(name) + 1;
	note.n_descsz = data_len;
	note.n_type   = type;
	memcpy(buf, &note, sizeof(note));
	buf += (sizeof(note) + 3)/4;
	memcpy(buf, name, note.n_namesz);
	buf += (note.n_namesz + 3)/4;
	memcpy(buf, data, note.n_descsz);
	buf += (note.n_descsz + 3)/4;

	return buf;
}

static void final_note(u32 *buf)
{
	struct elf_note note;

	note.n_namesz = 0;
	note.n_descsz = 0;
	note.n_type   = 0;
	memcpy(buf, &note, sizeof(note));
}

void crash_save_cpu(struct pt_regs *regs, int cpu)
{
	struct elf_prstatus prstatus;
	u32 *buf;

	if ((cpu < 0) || (cpu >= nr_cpu_ids))
		return;

	/* Using ELF notes here is opportunistic.
	 * I need a well defined structure format
	 * for the data I pass, and I need tags
	 * on the data to indicate what information I have
	 * squirrelled away.  ELF notes happen to provide
	 * all of that, so there is no need to invent something new.
	 */
	buf = (u32 *)per_cpu_ptr(crash_notes, cpu);
	if (!buf)
		return;
	memset(&prstatus, 0, sizeof(prstatus));
	prstatus.pr_pid = current->pid;
	elf_core_copy_kernel_regs(&prstatus.pr_reg, regs);
	buf = append_elf_note(buf, KEXEC_CORE_NOTE_NAME, NT_PRSTATUS,
			      &prstatus, sizeof(prstatus));
	final_note(buf);
}

static int __init crash_notes_memory_init(void)
{
	/* Allocate memory for saving cpu registers. */
	crash_notes = alloc_percpu(note_buf_t);
	if (!crash_notes) {
		pr_warn("Kexec: Memory allocation for saving cpu register states failed\n");
		return -ENOMEM;
	}
	return 0;
}
subsys_initcall(crash_notes_memory_init);


/*
 * parsing the "crashkernel" commandline
 *
 * this code is intended to be called from architecture specific code
 */


/*
 * This function parses command lines in the format
 *
 *   crashkernel=ramsize-range:size[,...][@offset]
 *
 * The function returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_mem(char *cmdline,
					unsigned long long system_ram,
					unsigned long long *crash_size,
					unsigned long long *crash_base)
{
	char *cur = cmdline, *tmp;

	/* for each entry of the comma-separated list */
	do {
		unsigned long long start, end = ULLONG_MAX, size;

		/* get the start of the range */
		start = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warn("crashkernel: Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (*cur != '-') {
			pr_warn("crashkernel: '-' expected\n");
			return -EINVAL;
		}
		cur++;

		/* if no ':' is here, than we read the end */
		if (*cur != ':') {
			end = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warn("crashkernel: Memory value expected\n");
				return -EINVAL;
			}
			cur = tmp;
			if (end <= start) {
				pr_warn("crashkernel: end <= start\n");
				return -EINVAL;
			}
		}

		if (*cur != ':') {
			pr_warn("crashkernel: ':' expected\n");
			return -EINVAL;
		}
		cur++;

		size = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warn("Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (size >= system_ram) {
			pr_warn("crashkernel: invalid size\n");
			return -EINVAL;
		}

		/* match ? */
		if (system_ram >= start && system_ram < end) {
			*crash_size = size;
			break;
		}
	} while (*cur++ == ',');

	if (*crash_size > 0) {
		while (*cur && *cur != ' ' && *cur != '@')
			cur++;
		if (*cur == '@') {
			cur++;
			*crash_base = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warn("Memory value expected after '@'\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

/*
 * That function parses "simple" (old) crashkernel command lines like
 *
 *	crashkernel=size[@offset]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_simple(char *cmdline,
					   unsigned long long *crash_size,
					   unsigned long long *crash_base)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	if (*cur == '@')
		*crash_base = memparse(cur+1, &cur);
	else if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char\n");
		return -EINVAL;
	}

	return 0;
}

#define SUFFIX_HIGH 0
#define SUFFIX_LOW  1
#define SUFFIX_NULL 2
static __initdata char *suffix_tbl[] = {
	[SUFFIX_HIGH] = ",high",
	[SUFFIX_LOW]  = ",low",
	[SUFFIX_NULL] = NULL,
};

/*
 * That function parses "suffix"  crashkernel command lines like
 *
 *	crashkernel=size,[high|low]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_suffix(char *cmdline,
					   unsigned long long	*crash_size,
					   unsigned long long	*crash_base,
					   const char *suffix)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	/* check with suffix */
	if (strncmp(cur, suffix, strlen(suffix))) {
		pr_warn("crashkernel: unrecognized char\n");
		return -EINVAL;
	}
	cur += strlen(suffix);
	if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char\n");
		return -EINVAL;
	}

	return 0;
}

static __init char *get_last_crashkernel(char *cmdline,
			     const char *name,
			     const char *suffix)
{
	char *p = cmdline, *ck_cmdline = NULL;

	/* find crashkernel and use the last one if there are more */
	p = strstr(p, name);
	while (p) {
		char *end_p = strchr(p, ' ');
		char *q;

		if (!end_p)
			end_p = p + strlen(p);

		if (!suffix) {
			int i;

			/* skip the one with any known suffix */
			for (i = 0; suffix_tbl[i]; i++) {
				q = end_p - strlen(suffix_tbl[i]);
				if (!strncmp(q, suffix_tbl[i],
					     strlen(suffix_tbl[i])))
					goto next;
			}
			ck_cmdline = p;
		} else {
			q = end_p - strlen(suffix);
			if (!strncmp(q, suffix, strlen(suffix)))
				ck_cmdline = p;
		}
next:
		p = strstr(p+1, name);
	}

	if (!ck_cmdline)
		return NULL;

	return ck_cmdline;
}

static int __init __parse_crashkernel(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base,
			     const char *name,
			     const char *suffix)
{
	char	*first_colon, *first_space;
	char	*ck_cmdline;

	BUG_ON(!crash_size || !crash_base);
	*crash_size = 0;
	*crash_base = 0;

	ck_cmdline = get_last_crashkernel(cmdline, name, suffix);

	if (!ck_cmdline)
		return -EINVAL;

	ck_cmdline += strlen(name);

	if (suffix)
		return parse_crashkernel_suffix(ck_cmdline, crash_size,
				crash_base, suffix);
	/*
	 * if the commandline contains a ':', then that's the extended
	 * syntax -- if not, it must be the classic syntax
	 */
	first_colon = strchr(ck_cmdline, ':');
	first_space = strchr(ck_cmdline, ' ');
	if (first_colon && (!first_space || first_colon < first_space))
		return parse_crashkernel_mem(ck_cmdline, system_ram,
				crash_size, crash_base);

	return parse_crashkernel_simple(ck_cmdline, crash_size, crash_base);
}

/*
 * That function is the entry point for command line parsing and should be
 * called from the arch-specific code.
 */
int __init parse_crashkernel(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base)
{
	return __parse_crashkernel(cmdline, system_ram, crash_size, crash_base,
					"crashkernel=", NULL);
}

int __init parse_crashkernel_high(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base)
{
	return __parse_crashkernel(cmdline, system_ram, crash_size, crash_base,
				"crashkernel=", suffix_tbl[SUFFIX_HIGH]);
}

int __init parse_crashkernel_low(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base)
{
	return __parse_crashkernel(cmdline, system_ram, crash_size, crash_base,
				"crashkernel=", suffix_tbl[SUFFIX_LOW]);
}

static void update_vmcoreinfo_note(void)
{
	u32 *buf = vmcoreinfo_note;

	if (!vmcoreinfo_size)
		return;
	buf = append_elf_note(buf, VMCOREINFO_NOTE_NAME, 0, vmcoreinfo_data,
			      vmcoreinfo_size);
	final_note(buf);
}

void crash_save_vmcoreinfo(void)
{
	vmcoreinfo_append_str("CRASHTIME=%ld\n", get_seconds());
	update_vmcoreinfo_note();
}

void vmcoreinfo_append_str(const char *fmt, ...)
{
	va_list args;
	char buf[0x50];
	size_t r;

	va_start(args, fmt);
	r = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	r = min(r, vmcoreinfo_max_size - vmcoreinfo_size);

	memcpy(&vmcoreinfo_data[vmcoreinfo_size], buf, r);

	vmcoreinfo_size += r;
}

/*
 * provide an empty default implementation here -- architecture
 * code may override this
 */
void __weak arch_crash_save_vmcoreinfo(void)
{}

unsigned long __weak paddr_vmcoreinfo_note(void)
{
	return __pa((unsigned long)(char *)&vmcoreinfo_note);
}

static int __init crash_save_vmcoreinfo_init(void)
{
	VMCOREINFO_OSRELEASE(init_uts_ns.name.release);
	VMCOREINFO_PAGESIZE(PAGE_SIZE);

	VMCOREINFO_SYMBOL(init_uts_ns);
	VMCOREINFO_SYMBOL(node_online_map);
#ifdef CONFIG_MMU
	VMCOREINFO_SYMBOL(swapper_pg_dir);
#endif
	VMCOREINFO_SYMBOL(_stext);
	VMCOREINFO_SYMBOL(vmap_area_list);

#ifndef CONFIG_NEED_MULTIPLE_NODES
	VMCOREINFO_SYMBOL(mem_map);
	VMCOREINFO_SYMBOL(contig_page_data);
#endif
#ifdef CONFIG_SPARSEMEM
	VMCOREINFO_SYMBOL(mem_section);
	VMCOREINFO_LENGTH(mem_section, NR_SECTION_ROOTS);
	VMCOREINFO_STRUCT_SIZE(mem_section);
	VMCOREINFO_OFFSET(mem_section, section_mem_map);
#endif
	VMCOREINFO_STRUCT_SIZE(page);
	VMCOREINFO_STRUCT_SIZE(pglist_data);
	VMCOREINFO_STRUCT_SIZE(zone);
	VMCOREINFO_STRUCT_SIZE(free_area);
	VMCOREINFO_STRUCT_SIZE(list_head);
	VMCOREINFO_SIZE(nodemask_t);
	VMCOREINFO_OFFSET(page, flags);
	VMCOREINFO_OFFSET(page, _count);
	VMCOREINFO_OFFSET(page, mapping);
	VMCOREINFO_OFFSET(page, lru);
	VMCOREINFO_OFFSET(page, _mapcount);
	VMCOREINFO_OFFSET(page, private);
	VMCOREINFO_OFFSET(pglist_data, node_zones);
	VMCOREINFO_OFFSET(pglist_data, nr_zones);
#ifdef CONFIG_FLAT_NODE_MEM_MAP
	VMCOREINFO_OFFSET(pglist_data, node_mem_map);
#endif
	VMCOREINFO_OFFSET(pglist_data, node_start_pfn);
	VMCOREINFO_OFFSET(pglist_data, node_spanned_pages);
	VMCOREINFO_OFFSET(pglist_data, node_id);
	VMCOREINFO_OFFSET(zone, free_area);
	VMCOREINFO_OFFSET(zone, vm_stat);
	VMCOREINFO_OFFSET(zone, spanned_pages);
	VMCOREINFO_OFFSET(free_area, free_list);
	VMCOREINFO_OFFSET(list_head, next);
	VMCOREINFO_OFFSET(list_head, prev);
	VMCOREINFO_OFFSET(vmap_area, va_start);
	VMCOREINFO_OFFSET(vmap_area, list);
	VMCOREINFO_LENGTH(zone.free_area, MAX_ORDER);
	log_buf_kexec_setup();
	VMCOREINFO_LENGTH(free_area.free_list, MIGRATE_TYPES);
	VMCOREINFO_NUMBER(NR_FREE_PAGES);
	VMCOREINFO_NUMBER(PG_lru);
	VMCOREINFO_NUMBER(PG_private);
	VMCOREINFO_NUMBER(PG_swapcache);
	VMCOREINFO_NUMBER(PG_slab);
#ifdef CONFIG_MEMORY_FAILURE
	VMCOREINFO_NUMBER(PG_hwpoison);
#endif
	VMCOREINFO_NUMBER(PG_head_mask);
	VMCOREINFO_NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE);
#ifdef CONFIG_HUGETLBFS
	VMCOREINFO_SYMBOL(free_huge_page);
#endif

	arch_crash_save_vmcoreinfo();
	update_vmcoreinfo_note();

	return 0;
}

subsys_initcall(crash_save_vmcoreinfo_init);

#ifdef CONFIG_KEXEC_FILE
static int __kexec_add_segment(struct kimage *image, char *buf,
			       unsigned long bufsz, unsigned long mem,
			       unsigned long memsz)
{
	struct kexec_segment *ksegment;

	ksegment = &image->segment[image->nr_segments];
	ksegment->kbuf = buf;
	ksegment->bufsz = bufsz;
	ksegment->mem = mem;
	ksegment->memsz = memsz;
	image->nr_segments++;

	return 0;
}

static int locate_mem_hole_top_down(unsigned long start, unsigned long end,
				    struct kexec_buf *kbuf)
{
	struct kimage *image = kbuf->image;
	unsigned long temp_start, temp_end;

	temp_end = min(end, kbuf->buf_max);
	temp_start = temp_end - kbuf->memsz;

	do {
		/* align down start */
		temp_start = temp_start & (~(kbuf->buf_align - 1));

		if (temp_start < start || temp_start < kbuf->buf_min)
			return 0;

		temp_end = temp_start + kbuf->memsz - 1;

		/*
		 * Make sure this does not conflict with any of existing
		 * segments
		 */
		if (kimage_is_destination_range(image, temp_start, temp_end)) {
			temp_start = temp_start - PAGE_SIZE;
			continue;
		}

		/* We found a suitable memory range */
		break;
	} while (1);

	/* If we are here, we found a suitable memory range */
	__kexec_add_segment(image, kbuf->buffer, kbuf->bufsz, temp_start,
			    kbuf->memsz);

	/* Success, stop navigating through remaining System RAM ranges */
	return 1;
}

static int locate_mem_hole_bottom_up(unsigned long start, unsigned long end,
				     struct kexec_buf *kbuf)
{
	struct kimage *image = kbuf->image;
	unsigned long temp_start, temp_end;

	temp_start = max(start, kbuf->buf_min);

	do {
		temp_start = ALIGN(temp_start, kbuf->buf_align);
		temp_end = temp_start + kbuf->memsz - 1;

		if (temp_end > end || temp_end > kbuf->buf_max)
			return 0;
		/*
		 * Make sure this does not conflict with any of existing
		 * segments
		 */
		if (kimage_is_destination_range(image, temp_start, temp_end)) {
			temp_start = temp_start + PAGE_SIZE;
			continue;
		}

		/* We found a suitable memory range */
		break;
	} while (1);

	/* If we are here, we found a suitable memory range */
	__kexec_add_segment(image, kbuf->buffer, kbuf->bufsz, temp_start,
			    kbuf->memsz);

	/* Success, stop navigating through remaining System RAM ranges */
	return 1;
}

static int locate_mem_hole_callback(u64 start, u64 end, void *arg)
{
	struct kexec_buf *kbuf = (struct kexec_buf *)arg;
	unsigned long sz = end - start + 1;

	/* Returning 0 will take to next memory range */
	if (sz < kbuf->memsz)
		return 0;

	if (end < kbuf->buf_min || start > kbuf->buf_max)
		return 0;

	/*
	 * Allocate memory top down with-in ram range. Otherwise bottom up
	 * allocation.
	 */
	if (kbuf->top_down)
		return locate_mem_hole_top_down(start, end, kbuf);
	return locate_mem_hole_bottom_up(start, end, kbuf);
}

/*
 * Helper function for placing a buffer in a kexec segment. This assumes
 * that kexec_mutex is held.
 */
int kexec_add_buffer(struct kimage *image, char *buffer, unsigned long bufsz,
		     unsigned long memsz, unsigned long buf_align,
		     unsigned long buf_min, unsigned long buf_max,
		     bool top_down, unsigned long *load_addr)
{

	struct kexec_segment *ksegment;
	struct kexec_buf buf, *kbuf;
	int ret;

	/* Currently adding segment this way is allowed only in file mode */
	if (!image->file_mode)
		return -EINVAL;

	if (image->nr_segments >= KEXEC_SEGMENT_MAX)
		return -EINVAL;

	/*
	 * Make sure we are not trying to add buffer after allocating
	 * control pages. All segments need to be placed first before
	 * any control pages are allocated. As control page allocation
	 * logic goes through list of segments to make sure there are
	 * no destination overlaps.
	 */
	if (!list_empty(&image->control_pages)) {
		WARN_ON(1);
		return -EINVAL;
	}

	memset(&buf, 0, sizeof(struct kexec_buf));
	kbuf = &buf;
	kbuf->image = image;
	kbuf->buffer = buffer;
	kbuf->bufsz = bufsz;

	kbuf->memsz = ALIGN(memsz, PAGE_SIZE);
	kbuf->buf_align = max(buf_align, PAGE_SIZE);
	kbuf->buf_min = buf_min;
	kbuf->buf_max = buf_max;
	kbuf->top_down = top_down;

	/* Walk the RAM ranges and allocate a suitable range for the buffer */
	if (image->type == KEXEC_TYPE_CRASH)
		ret = walk_iomem_res("Crash kernel",
				     IORESOURCE_MEM | IORESOURCE_BUSY,
				     crashk_res.start, crashk_res.end, kbuf,
				     locate_mem_hole_callback);
	else
		ret = walk_system_ram_res(0, -1, kbuf,
					  locate_mem_hole_callback);
	if (ret != 1) {
		/* A suitable memory range could not be found for buffer */
		return -EADDRNOTAVAIL;
	}

	/* Found a suitable memory range */
	ksegment = &image->segment[image->nr_segments - 1];
	*load_addr = ksegment->mem;
	return 0;
}

/* Calculate and store the digest of segments */
static int kexec_calculate_store_digests(struct kimage *image)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	int ret = 0, i, j, zero_buf_sz, sha_region_sz;
	size_t desc_size, nullsz;
	char *digest;
	void *zero_buf;
	struct kexec_sha_region *sha_regions;
	struct purgatory_info *pi = &image->purgatory_info;

	zero_buf = __va(page_to_pfn(ZERO_PAGE(0)) << PAGE_SHIFT);
	zero_buf_sz = PAGE_SIZE;

	tfm = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		goto out;
	}

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto out_free_tfm;
	}

	sha_region_sz = KEXEC_SEGMENT_MAX * sizeof(struct kexec_sha_region);
	sha_regions = vzalloc(sha_region_sz);
	if (!sha_regions)
		goto out_free_desc;

	desc->tfm   = tfm;
	desc->flags = 0;

	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto out_free_sha_regions;

	digest = kzalloc(SHA256_DIGEST_SIZE, GFP_KERNEL);
	if (!digest) {
		ret = -ENOMEM;
		goto out_free_sha_regions;
	}

	for (j = i = 0; i < image->nr_segments; i++) {
		struct kexec_segment *ksegment;

		ksegment = &image->segment[i];
		/*
		 * Skip purgatory as it will be modified once we put digest
		 * info in purgatory.
		 */
		if (ksegment->kbuf == pi->purgatory_buf)
			continue;

		ret = crypto_shash_update(desc, ksegment->kbuf,
					  ksegment->bufsz);
		if (ret)
			break;

		/*
		 * Assume rest of the buffer is filled with zero and
		 * update digest accordingly.
		 */
		nullsz = ksegment->memsz - ksegment->bufsz;
		while (nullsz) {
			unsigned long bytes = nullsz;

			if (bytes > zero_buf_sz)
				bytes = zero_buf_sz;
			ret = crypto_shash_update(desc, zero_buf, bytes);
			if (ret)
				break;
			nullsz -= bytes;
		}

		if (ret)
			break;

		sha_regions[j].start = ksegment->mem;
		sha_regions[j].len = ksegment->memsz;
		j++;
	}

	if (!ret) {
		ret = crypto_shash_final(desc, digest);
		if (ret)
			goto out_free_digest;
		ret = kexec_purgatory_get_set_symbol(image, "sha_regions",
						sha_regions, sha_region_sz, 0);
		if (ret)
			goto out_free_digest;

		ret = kexec_purgatory_get_set_symbol(image, "sha256_digest",
						digest, SHA256_DIGEST_SIZE, 0);
		if (ret)
			goto out_free_digest;
	}

out_free_digest:
	kfree(digest);
out_free_sha_regions:
	vfree(sha_regions);
out_free_desc:
	kfree(desc);
out_free_tfm:
	kfree(tfm);
out:
	return ret;
}

/* Actually load purgatory. Lot of code taken from kexec-tools */
static int __kexec_load_purgatory(struct kimage *image, unsigned long min,
				  unsigned long max, int top_down)
{
	struct purgatory_info *pi = &image->purgatory_info;
	unsigned long align, buf_align, bss_align, buf_sz, bss_sz, bss_pad;
	unsigned long memsz, entry, load_addr, curr_load_addr, bss_addr, offset;
	unsigned char *buf_addr, *src;
	int i, ret = 0, entry_sidx = -1;
	const Elf_Shdr *sechdrs_c;
	Elf_Shdr *sechdrs = NULL;
	void *purgatory_buf = NULL;

	/*
	 * sechdrs_c points to section headers in purgatory and are read
	 * only. No modifications allowed.
	 */
	sechdrs_c = (void *)pi->ehdr + pi->ehdr->e_shoff;

	/*
	 * We can not modify sechdrs_c[] and its fields. It is read only.
	 * Copy it over to a local copy where one can store some temporary
	 * data and free it at the end. We need to modify ->sh_addr and
	 * ->sh_offset fields to keep track of permanent and temporary
	 * locations of sections.
	 */
	sechdrs = vzalloc(pi->ehdr->e_shnum * sizeof(Elf_Shdr));
	if (!sechdrs)
		return -ENOMEM;

	memcpy(sechdrs, sechdrs_c, pi->ehdr->e_shnum * sizeof(Elf_Shdr));

	/*
	 * We seem to have multiple copies of sections. First copy is which
	 * is embedded in kernel in read only section. Some of these sections
	 * will be copied to a temporary buffer and relocated. And these
	 * sections will finally be copied to their final destination at
	 * segment load time.
	 *
	 * Use ->sh_offset to reflect section address in memory. It will
	 * point to original read only copy if section is not allocatable.
	 * Otherwise it will point to temporary copy which will be relocated.
	 *
	 * Use ->sh_addr to contain final address of the section where it
	 * will go during execution time.
	 */
	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		if (sechdrs[i].sh_type == SHT_NOBITS)
			continue;

		sechdrs[i].sh_offset = (unsigned long)pi->ehdr +
						sechdrs[i].sh_offset;
	}

	/*
	 * Identify entry point section and make entry relative to section
	 * start.
	 */
	entry = pi->ehdr->e_entry;
	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		if (!(sechdrs[i].sh_flags & SHF_EXECINSTR))
			continue;

		/* Make entry section relative */
		if (sechdrs[i].sh_addr <= pi->ehdr->e_entry &&
		    ((sechdrs[i].sh_addr + sechdrs[i].sh_size) >
		     pi->ehdr->e_entry)) {
			entry_sidx = i;
			entry -= sechdrs[i].sh_addr;
			break;
		}
	}

	/* Determine how much memory is needed to load relocatable object. */
	buf_align = 1;
	bss_align = 1;
	buf_sz = 0;
	bss_sz = 0;

	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		align = sechdrs[i].sh_addralign;
		if (sechdrs[i].sh_type != SHT_NOBITS) {
			if (buf_align < align)
				buf_align = align;
			buf_sz = ALIGN(buf_sz, align);
			buf_sz += sechdrs[i].sh_size;
		} else {
			/* bss section */
			if (bss_align < align)
				bss_align = align;
			bss_sz = ALIGN(bss_sz, align);
			bss_sz += sechdrs[i].sh_size;
		}
	}

	/* Determine the bss padding required to align bss properly */
	bss_pad = 0;
	if (buf_sz & (bss_align - 1))
		bss_pad = bss_align - (buf_sz & (bss_align - 1));

	memsz = buf_sz + bss_pad + bss_sz;

	/* Allocate buffer for purgatory */
	purgatory_buf = vzalloc(buf_sz);
	if (!purgatory_buf) {
		ret = -ENOMEM;
		goto out;
	}

	if (buf_align < bss_align)
		buf_align = bss_align;

	/* Add buffer to segment list */
	ret = kexec_add_buffer(image, purgatory_buf, buf_sz, memsz,
				buf_align, min, max, top_down,
				&pi->purgatory_load_addr);
	if (ret)
		goto out;

	/* Load SHF_ALLOC sections */
	buf_addr = purgatory_buf;
	load_addr = curr_load_addr = pi->purgatory_load_addr;
	bss_addr = load_addr + buf_sz + bss_pad;

	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		align = sechdrs[i].sh_addralign;
		if (sechdrs[i].sh_type != SHT_NOBITS) {
			curr_load_addr = ALIGN(curr_load_addr, align);
			offset = curr_load_addr - load_addr;
			/* We already modifed ->sh_offset to keep src addr */
			src = (char *) sechdrs[i].sh_offset;
			memcpy(buf_addr + offset, src, sechdrs[i].sh_size);

			/* Store load address and source address of section */
			sechdrs[i].sh_addr = curr_load_addr;

			/*
			 * This section got copied to temporary buffer. Update
			 * ->sh_offset accordingly.
			 */
			sechdrs[i].sh_offset = (unsigned long)(buf_addr + offset);

			/* Advance to the next address */
			curr_load_addr += sechdrs[i].sh_size;
		} else {
			bss_addr = ALIGN(bss_addr, align);
			sechdrs[i].sh_addr = bss_addr;
			bss_addr += sechdrs[i].sh_size;
		}
	}

	/* Update entry point based on load address of text section */
	if (entry_sidx >= 0)
		entry += sechdrs[entry_sidx].sh_addr;

	/* Make kernel jump to purgatory after shutdown */
	image->start = entry;

	/* Used later to get/set symbol values */
	pi->sechdrs = sechdrs;

	/*
	 * Used later to identify which section is purgatory and skip it
	 * from checksumming.
	 */
	pi->purgatory_buf = purgatory_buf;
	return ret;
out:
	vfree(sechdrs);
	vfree(purgatory_buf);
	return ret;
}

static int kexec_apply_relocations(struct kimage *image)
{
	int i, ret;
	struct purgatory_info *pi = &image->purgatory_info;
	Elf_Shdr *sechdrs = pi->sechdrs;

	/* Apply relocations */
	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		Elf_Shdr *section, *symtab;

		if (sechdrs[i].sh_type != SHT_RELA &&
		    sechdrs[i].sh_type != SHT_REL)
			continue;

		/*
		 * For section of type SHT_RELA/SHT_REL,
		 * ->sh_link contains section header index of associated
		 * symbol table. And ->sh_info contains section header
		 * index of section to which relocations apply.
		 */
		if (sechdrs[i].sh_info >= pi->ehdr->e_shnum ||
		    sechdrs[i].sh_link >= pi->ehdr->e_shnum)
			return -ENOEXEC;

		section = &sechdrs[sechdrs[i].sh_info];
		symtab = &sechdrs[sechdrs[i].sh_link];

		if (!(section->sh_flags & SHF_ALLOC))
			continue;

		/*
		 * symtab->sh_link contain section header index of associated
		 * string table.
		 */
		if (symtab->sh_link >= pi->ehdr->e_shnum)
			/* Invalid section number? */
			continue;

		/*
		 * Respective archicture needs to provide support for applying
		 * relocations of type SHT_RELA/SHT_REL.
		 */
		if (sechdrs[i].sh_type == SHT_RELA)
			ret = arch_kexec_apply_relocations_add(pi->ehdr,
							       sechdrs, i);
		else if (sechdrs[i].sh_type == SHT_REL)
			ret = arch_kexec_apply_relocations(pi->ehdr,
							   sechdrs, i);
		if (ret)
			return ret;
	}

	return 0;
}

/* Load relocatable purgatory object and relocate it appropriately */
int kexec_load_purgatory(struct kimage *image, unsigned long min,
			 unsigned long max, int top_down,
			 unsigned long *load_addr)
{
	struct purgatory_info *pi = &image->purgatory_info;
	int ret;

	if (kexec_purgatory_size <= 0)
		return -EINVAL;

	if (kexec_purgatory_size < sizeof(Elf_Ehdr))
		return -ENOEXEC;

	pi->ehdr = (Elf_Ehdr *)kexec_purgatory;

	if (memcmp(pi->ehdr->e_ident, ELFMAG, SELFMAG) != 0
	    || pi->ehdr->e_type != ET_REL
	    || !elf_check_arch(pi->ehdr)
	    || pi->ehdr->e_shentsize != sizeof(Elf_Shdr))
		return -ENOEXEC;

	if (pi->ehdr->e_shoff >= kexec_purgatory_size
	    || (pi->ehdr->e_shnum * sizeof(Elf_Shdr) >
	    kexec_purgatory_size - pi->ehdr->e_shoff))
		return -ENOEXEC;

	ret = __kexec_load_purgatory(image, min, max, top_down);
	if (ret)
		return ret;

	ret = kexec_apply_relocations(image);
	if (ret)
		goto out;

	*load_addr = pi->purgatory_load_addr;
	return 0;
out:
	vfree(pi->sechdrs);
	vfree(pi->purgatory_buf);
	return ret;
}

static Elf_Sym *kexec_purgatory_find_symbol(struct purgatory_info *pi,
					    const char *name)
{
	Elf_Sym *syms;
	Elf_Shdr *sechdrs;
	Elf_Ehdr *ehdr;
	int i, k;
	const char *strtab;

	if (!pi->sechdrs || !pi->ehdr)
		return NULL;

	sechdrs = pi->sechdrs;
	ehdr = pi->ehdr;

	for (i = 0; i < ehdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_SYMTAB)
			continue;

		if (sechdrs[i].sh_link >= ehdr->e_shnum)
			/* Invalid strtab section number */
			continue;
		strtab = (char *)sechdrs[sechdrs[i].sh_link].sh_offset;
		syms = (Elf_Sym *)sechdrs[i].sh_offset;

		/* Go through symbols for a match */
		for (k = 0; k < sechdrs[i].sh_size/sizeof(Elf_Sym); k++) {
			if (ELF_ST_BIND(syms[k].st_info) != STB_GLOBAL)
				continue;

			if (strcmp(strtab + syms[k].st_name, name) != 0)
				continue;

			if (syms[k].st_shndx == SHN_UNDEF ||
			    syms[k].st_shndx >= ehdr->e_shnum) {
				pr_debug("Symbol: %s has bad section index %d.\n",
						name, syms[k].st_shndx);
				return NULL;
			}

			/* Found the symbol we are looking for */
			return &syms[k];
		}
	}

	return NULL;
}

void *kexec_purgatory_get_symbol_addr(struct kimage *image, const char *name)
{
	struct purgatory_info *pi = &image->purgatory_info;
	Elf_Sym *sym;
	Elf_Shdr *sechdr;

	sym = kexec_purgatory_find_symbol(pi, name);
	if (!sym)
		return ERR_PTR(-EINVAL);

	sechdr = &pi->sechdrs[sym->st_shndx];

	/*
	 * Returns the address where symbol will finally be loaded after
	 * kexec_load_segment()
	 */
	return (void *)(sechdr->sh_addr + sym->st_value);
}

/*
 * Get or set value of a symbol. If "get_value" is true, symbol value is
 * returned in buf otherwise symbol value is set based on value in buf.
 */
int kexec_purgatory_get_set_symbol(struct kimage *image, const char *name,
				   void *buf, unsigned int size, bool get_value)
{
	Elf_Sym *sym;
	Elf_Shdr *sechdrs;
	struct purgatory_info *pi = &image->purgatory_info;
	char *sym_buf;

	sym = kexec_purgatory_find_symbol(pi, name);
	if (!sym)
		return -EINVAL;

	if (sym->st_size != size) {
		pr_err("symbol %s size mismatch: expected %lu actual %u\n",
		       name, (unsigned long)sym->st_size, size);
		return -EINVAL;
	}

	sechdrs = pi->sechdrs;

	if (sechdrs[sym->st_shndx].sh_type == SHT_NOBITS) {
		pr_err("symbol %s is in a bss section. Cannot %s\n", name,
		       get_value ? "get" : "set");
		return -EINVAL;
	}

	sym_buf = (unsigned char *)sechdrs[sym->st_shndx].sh_offset +
					sym->st_value;

	if (get_value)
		memcpy((void *)buf, sym_buf, size);
	else
		memcpy((void *)sym_buf, buf, size);

	return 0;
}
#endif /* CONFIG_KEXEC_FILE */

/*
 * Move into place and start executing a preloaded standalone
 * executable.  If nothing was preloaded return an error.
 */
int kernel_kexec(void)
{
	int error = 0;

	if (!mutex_trylock(&kexec_mutex))
		return -EBUSY;
	if (!kexec_image) {
		error = -EINVAL;
		goto Unlock;
	}

#ifdef CONFIG_KEXEC_JUMP
	if (kexec_image->preserve_context) {
		lock_system_sleep();
		pm_prepare_console();
		error = freeze_processes();
		if (error) {
			error = -EBUSY;
			goto Restore_console;
		}
		suspend_console();
		error = dpm_suspend_start(PMSG_FREEZE);
		if (error)
			goto Resume_console;
		/* At this point, dpm_suspend_start() has been called,
		 * but *not* dpm_suspend_end(). We *must* call
		 * dpm_suspend_end() now.  Otherwise, drivers for
		 * some devices (e.g. interrupt controllers) become
		 * desynchronized with the actual state of the
		 * hardware at resume time, and evil weirdness ensues.
		 */
		error = dpm_suspend_end(PMSG_FREEZE);
		if (error)
			goto Resume_devices;
		error = disable_nonboot_cpus();
		if (error)
			goto Enable_cpus;
		local_irq_disable();
		error = syscore_suspend();
		if (error)
			goto Enable_irqs;
	} else
#endif
	{
		kexec_in_progress = true;
		kernel_restart_prepare(NULL);
		migrate_to_reboot_cpu();

		/*
		 * migrate_to_reboot_cpu() disables CPU hotplug assuming that
		 * no further code needs to use CPU hotplug (which is true in
		 * the reboot case). However, the kexec path depends on using
		 * CPU hotplug again; so re-enable it here.
		 */
		cpu_hotplug_enable();
		pr_emerg("Starting new kernel\n");
		machine_shutdown();
	}

	machine_kexec(kexec_image);

#ifdef CONFIG_KEXEC_JUMP
	if (kexec_image->preserve_context) {
		syscore_resume();
 Enable_irqs:
		local_irq_enable();
 Enable_cpus:
		enable_nonboot_cpus();
		dpm_resume_start(PMSG_RESTORE);
 Resume_devices:
		dpm_resume_end(PMSG_RESTORE);
 Resume_console:
		resume_console();
		thaw_processes();
 Restore_console:
		pm_restore_console();
		unlock_system_sleep();
	}
#endif

 Unlock:
	mutex_unlock(&kexec_mutex);
	return error;
}
