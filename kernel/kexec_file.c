/*
 * kexec: kexec_file_load system call
 *
 * Copyright (C) 2014 Red Hat Inc.
 * Authors:
 *      Vivek Goyal <vgoyal@redhat.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/kexec.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/ima.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include "kexec_internal.h"

static int kexec_calculate_store_digests(struct kimage *image);

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

int __weak arch_kimage_file_post_load_cleanup(struct kimage *image)
{
	return -EINVAL;
}

#ifdef CONFIG_KEXEC_VERIFY_SIG
int __weak arch_kexec_kernel_verify_sig(struct kimage *image, void *buf,
					unsigned long buf_len)
{
	return -EKEYREJECTED;
}
#endif

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
 * Free up memory used by kernel, initrd, and command line. This is temporary
 * memory allocation which is not needed any more after these buffers have
 * been loaded into separate segments and have been copied elsewhere.
 */
void kimage_file_post_load_cleanup(struct kimage *image)
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
	loff_t size;

	ret = kernel_read_file_from_fd(kernel_fd, &image->kernel_buf,
				       &size, INT_MAX, READING_KEXEC_IMAGE);
	if (ret)
		return ret;
	image->kernel_buf_len = size;

	/* IMA needs to pass the measurement list to the next kernel. */
	ima_add_kexec_buffer(image);

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
		ret = kernel_read_file_from_fd(initrd_fd, &image->initrd_buf,
					       &size, INT_MAX,
					       READING_KEXEC_INITRAMFS);
		if (ret)
			goto out;
		image->initrd_buf_len = size;
	}

	if (cmdline_len) {
		image->cmdline_buf = memdup_user(cmdline_ptr, cmdline_len);
		if (IS_ERR(image->cmdline_buf)) {
			ret = PTR_ERR(image->cmdline_buf);
			image->cmdline_buf = NULL;
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
			pr_err("Could not allocate swap buffer\n");
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
	if (flags & KEXEC_FILE_ON_CRASH) {
		dest_image = &kexec_crash_image;
		if (kexec_crash_image)
			arch_kexec_unprotect_crashkres();
	}

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

	/*
	 * Some architecture(like S390) may touch the crash memory before
	 * machine_kexec_prepare(), we must copy vmcoreinfo data after it.
	 */
	ret = kimage_crash_copy_vmcoreinfo(image);
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
	if ((flags & KEXEC_FILE_ON_CRASH) && kexec_crash_image)
		arch_kexec_protect_crashkres();

	mutex_unlock(&kexec_mutex);
	kimage_free(image);
	return ret;
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
	kbuf->mem = temp_start;

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
	kbuf->mem = temp_start;

	/* Success, stop navigating through remaining System RAM ranges */
	return 1;
}

static int locate_mem_hole_callback(struct resource *res, void *arg)
{
	struct kexec_buf *kbuf = (struct kexec_buf *)arg;
	u64 start = res->start, end = res->end;
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

/**
 * arch_kexec_walk_mem - call func(data) on free memory regions
 * @kbuf:	Context info for the search. Also passed to @func.
 * @func:	Function to call for each memory region.
 *
 * Return: The memory walk will stop when func returns a non-zero value
 * and that value will be returned. If all free regions are visited without
 * func returning non-zero, then zero will be returned.
 */
int __weak arch_kexec_walk_mem(struct kexec_buf *kbuf,
			       int (*func)(struct resource *, void *))
{
	if (kbuf->image->type == KEXEC_TYPE_CRASH)
		return walk_iomem_res_desc(crashk_res.desc,
					   IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY,
					   crashk_res.start, crashk_res.end,
					   kbuf, func);
	else
		return walk_system_ram_res(0, ULONG_MAX, kbuf, func);
}

/**
 * kexec_locate_mem_hole - find free memory for the purgatory or the next kernel
 * @kbuf:	Parameters for the memory search.
 *
 * On success, kbuf->mem will have the start address of the memory region found.
 *
 * Return: 0 on success, negative errno on error.
 */
int kexec_locate_mem_hole(struct kexec_buf *kbuf)
{
	int ret;

	ret = arch_kexec_walk_mem(kbuf, locate_mem_hole_callback);

	return ret == 1 ? 0 : -EADDRNOTAVAIL;
}

/**
 * kexec_add_buffer - place a buffer in a kexec segment
 * @kbuf:	Buffer contents and memory parameters.
 *
 * This function assumes that kexec_mutex is held.
 * On successful return, @kbuf->mem will have the physical address of
 * the buffer in memory.
 *
 * Return: 0 on success, negative errno on error.
 */
int kexec_add_buffer(struct kexec_buf *kbuf)
{

	struct kexec_segment *ksegment;
	int ret;

	/* Currently adding segment this way is allowed only in file mode */
	if (!kbuf->image->file_mode)
		return -EINVAL;

	if (kbuf->image->nr_segments >= KEXEC_SEGMENT_MAX)
		return -EINVAL;

	/*
	 * Make sure we are not trying to add buffer after allocating
	 * control pages. All segments need to be placed first before
	 * any control pages are allocated. As control page allocation
	 * logic goes through list of segments to make sure there are
	 * no destination overlaps.
	 */
	if (!list_empty(&kbuf->image->control_pages)) {
		WARN_ON(1);
		return -EINVAL;
	}

	/* Ensure minimum alignment needed for segments. */
	kbuf->memsz = ALIGN(kbuf->memsz, PAGE_SIZE);
	kbuf->buf_align = max(kbuf->buf_align, PAGE_SIZE);

	/* Walk the RAM ranges and allocate a suitable range for the buffer */
	ret = kexec_locate_mem_hole(kbuf);
	if (ret)
		return ret;

	/* Found a suitable memory range */
	ksegment = &kbuf->image->segment[kbuf->image->nr_segments];
	ksegment->kbuf = kbuf->buffer;
	ksegment->bufsz = kbuf->bufsz;
	ksegment->mem = kbuf->mem;
	ksegment->memsz = kbuf->memsz;
	kbuf->image->nr_segments++;
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
		ret = kexec_purgatory_get_set_symbol(image, "purgatory_sha_regions",
						     sha_regions, sha_region_sz, 0);
		if (ret)
			goto out_free_digest;

		ret = kexec_purgatory_get_set_symbol(image, "purgatory_sha256_digest",
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
	unsigned long align, bss_align, bss_sz, bss_pad;
	unsigned long entry, load_addr, curr_load_addr, bss_addr, offset;
	unsigned char *buf_addr, *src;
	int i, ret = 0, entry_sidx = -1;
	const Elf_Shdr *sechdrs_c;
	Elf_Shdr *sechdrs = NULL;
	struct kexec_buf kbuf = { .image = image, .bufsz = 0, .buf_align = 1,
				  .buf_min = min, .buf_max = max,
				  .top_down = top_down };

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
	bss_align = 1;
	bss_sz = 0;

	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		align = sechdrs[i].sh_addralign;
		if (sechdrs[i].sh_type != SHT_NOBITS) {
			if (kbuf.buf_align < align)
				kbuf.buf_align = align;
			kbuf.bufsz = ALIGN(kbuf.bufsz, align);
			kbuf.bufsz += sechdrs[i].sh_size;
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
	if (kbuf.bufsz & (bss_align - 1))
		bss_pad = bss_align - (kbuf.bufsz & (bss_align - 1));

	kbuf.memsz = kbuf.bufsz + bss_pad + bss_sz;

	/* Allocate buffer for purgatory */
	kbuf.buffer = vzalloc(kbuf.bufsz);
	if (!kbuf.buffer) {
		ret = -ENOMEM;
		goto out;
	}

	if (kbuf.buf_align < bss_align)
		kbuf.buf_align = bss_align;

	/* Add buffer to segment list */
	ret = kexec_add_buffer(&kbuf);
	if (ret)
		goto out;
	pi->purgatory_load_addr = kbuf.mem;

	/* Load SHF_ALLOC sections */
	buf_addr = kbuf.buffer;
	load_addr = curr_load_addr = pi->purgatory_load_addr;
	bss_addr = load_addr + kbuf.bufsz + bss_pad;

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
	pi->purgatory_buf = kbuf.buffer;
	return ret;
out:
	vfree(sechdrs);
	vfree(kbuf.buffer);
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
		 * Respective architecture needs to provide support for applying
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
	pi->sechdrs = NULL;

	vfree(pi->purgatory_buf);
	pi->purgatory_buf = NULL;
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
