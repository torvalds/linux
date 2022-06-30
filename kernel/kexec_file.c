// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec: kexec_file_load system call
 *
 * Copyright (C) 2014 Red Hat Inc.
 * Authors:
 *      Vivek Goyal <vgoyal@redhat.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/kexec.h>
#include <linux/memblock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/ima.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/kernel.h>
#include <linux/kernel_read_file.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include "kexec_internal.h"

#ifdef CONFIG_KEXEC_SIG
static bool sig_enforce = IS_ENABLED(CONFIG_KEXEC_SIG_FORCE);

void set_kexec_sig_enforced(void)
{
	sig_enforce = true;
}
#endif

static int kexec_calculate_store_digests(struct kimage *image);

/* Maximum size in bytes for kernel/initrd files. */
#define KEXEC_FILE_SIZE_MAX	min_t(s64, 4LL << 30, SSIZE_MAX)

/*
 * Currently this is the only default function that is exported as some
 * architectures need it to do additional handlings.
 * In the future, other default functions may be exported too if required.
 */
int kexec_image_probe_default(struct kimage *image, void *buf,
			      unsigned long buf_len)
{
	const struct kexec_file_ops * const *fops;
	int ret = -ENOEXEC;

	for (fops = &kexec_file_loaders[0]; *fops && (*fops)->probe; ++fops) {
		ret = (*fops)->probe(buf, buf_len);
		if (!ret) {
			image->fops = *fops;
			return ret;
		}
	}

	return ret;
}

void *kexec_image_load_default(struct kimage *image)
{
	if (!image->fops || !image->fops->load)
		return ERR_PTR(-ENOEXEC);

	return image->fops->load(image, image->kernel_buf,
				 image->kernel_buf_len, image->initrd_buf,
				 image->initrd_buf_len, image->cmdline_buf,
				 image->cmdline_buf_len);
}

int kexec_image_post_load_cleanup_default(struct kimage *image)
{
	if (!image->fops || !image->fops->cleanup)
		return 0;

	return image->fops->cleanup(image->image_loader_data);
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

#ifdef CONFIG_IMA_KEXEC
	vfree(image->ima_buffer);
	image->ima_buffer = NULL;
#endif /* CONFIG_IMA_KEXEC */

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

#ifdef CONFIG_KEXEC_SIG
#ifdef CONFIG_SIGNED_PE_FILE_VERIFICATION
int kexec_kernel_verify_pe_sig(const char *kernel, unsigned long kernel_len)
{
	int ret;

	ret = verify_pefile_signature(kernel, kernel_len,
				      VERIFY_USE_SECONDARY_KEYRING,
				      VERIFYING_KEXEC_PE_SIGNATURE);
	if (ret == -ENOKEY && IS_ENABLED(CONFIG_INTEGRITY_PLATFORM_KEYRING)) {
		ret = verify_pefile_signature(kernel, kernel_len,
					      VERIFY_USE_PLATFORM_KEYRING,
					      VERIFYING_KEXEC_PE_SIGNATURE);
	}
	return ret;
}
#endif

static int kexec_image_verify_sig(struct kimage *image, void *buf,
				  unsigned long buf_len)
{
	if (!image->fops || !image->fops->verify_sig) {
		pr_debug("kernel loader does not support signature verification.\n");
		return -EKEYREJECTED;
	}

	return image->fops->verify_sig(buf, buf_len);
}

static int
kimage_validate_signature(struct kimage *image)
{
	int ret;

	ret = kexec_image_verify_sig(image, image->kernel_buf,
				     image->kernel_buf_len);
	if (ret) {

		if (sig_enforce) {
			pr_notice("Enforced kernel signature verification failed (%d).\n", ret);
			return ret;
		}

		/*
		 * If IMA is guaranteed to appraise a signature on the kexec
		 * image, permit it even if the kernel is otherwise locked
		 * down.
		 */
		if (!ima_appraise_signature(READING_KEXEC_IMAGE) &&
		    security_locked_down(LOCKDOWN_KEXEC))
			return -EPERM;

		pr_debug("kernel signature verification failed (%d).\n", ret);
	}

	return 0;
}
#endif

/*
 * In file mode list of segments is prepared by kernel. Copy relevant
 * data from user space, do error checking, prepare segment list
 */
static int
kimage_file_prepare_segments(struct kimage *image, int kernel_fd, int initrd_fd,
			     const char __user *cmdline_ptr,
			     unsigned long cmdline_len, unsigned flags)
{
	ssize_t ret;
	void *ldata;

	ret = kernel_read_file_from_fd(kernel_fd, 0, &image->kernel_buf,
				       KEXEC_FILE_SIZE_MAX, NULL,
				       READING_KEXEC_IMAGE);
	if (ret < 0)
		return ret;
	image->kernel_buf_len = ret;

	/* Call arch image probe handlers */
	ret = arch_kexec_kernel_image_probe(image, image->kernel_buf,
					    image->kernel_buf_len);
	if (ret)
		goto out;

#ifdef CONFIG_KEXEC_SIG
	ret = kimage_validate_signature(image);

	if (ret)
		goto out;
#endif
	/* It is possible that there no initramfs is being loaded */
	if (!(flags & KEXEC_FILE_NO_INITRAMFS)) {
		ret = kernel_read_file_from_fd(initrd_fd, 0, &image->initrd_buf,
					       KEXEC_FILE_SIZE_MAX, NULL,
					       READING_KEXEC_INITRAMFS);
		if (ret < 0)
			goto out;
		image->initrd_buf_len = ret;
		ret = 0;
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

		ima_kexec_cmdline(kernel_fd, image->cmdline_buf,
				  image->cmdline_buf_len - 1);
	}

	/* IMA needs to pass the measurement list to the next kernel. */
	ima_add_kexec_buffer(image);

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

	if (!kexec_trylock())
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

	ret = machine_kexec_post_load(image);
	if (ret)
		goto out;

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

	kexec_unlock();
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

	/* Don't use memory that will be detected and handled by a driver. */
	if (res->flags & IORESOURCE_SYSRAM_DRIVER_MANAGED)
		return 0;

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

#ifdef CONFIG_ARCH_KEEP_MEMBLOCK
static int kexec_walk_memblock(struct kexec_buf *kbuf,
			       int (*func)(struct resource *, void *))
{
	int ret = 0;
	u64 i;
	phys_addr_t mstart, mend;
	struct resource res = { };

	if (kbuf->image->type == KEXEC_TYPE_CRASH)
		return func(&crashk_res, kbuf);

	/*
	 * Using MEMBLOCK_NONE will properly skip MEMBLOCK_DRIVER_MANAGED. See
	 * IORESOURCE_SYSRAM_DRIVER_MANAGED handling in
	 * locate_mem_hole_callback().
	 */
	if (kbuf->top_down) {
		for_each_free_mem_range_reverse(i, NUMA_NO_NODE, MEMBLOCK_NONE,
						&mstart, &mend, NULL) {
			/*
			 * In memblock, end points to the first byte after the
			 * range while in kexec, end points to the last byte
			 * in the range.
			 */
			res.start = mstart;
			res.end = mend - 1;
			ret = func(&res, kbuf);
			if (ret)
				break;
		}
	} else {
		for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE,
					&mstart, &mend, NULL) {
			/*
			 * In memblock, end points to the first byte after the
			 * range while in kexec, end points to the last byte
			 * in the range.
			 */
			res.start = mstart;
			res.end = mend - 1;
			ret = func(&res, kbuf);
			if (ret)
				break;
		}
	}

	return ret;
}
#else
static int kexec_walk_memblock(struct kexec_buf *kbuf,
			       int (*func)(struct resource *, void *))
{
	return 0;
}
#endif

/**
 * kexec_walk_resources - call func(data) on free memory regions
 * @kbuf:	Context info for the search. Also passed to @func.
 * @func:	Function to call for each memory region.
 *
 * Return: The memory walk will stop when func returns a non-zero value
 * and that value will be returned. If all free regions are visited without
 * func returning non-zero, then zero will be returned.
 */
static int kexec_walk_resources(struct kexec_buf *kbuf,
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

	/* Arch knows where to place */
	if (kbuf->mem != KEXEC_BUF_MEM_UNKNOWN)
		return 0;

	if (!IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK))
		ret = kexec_walk_resources(kbuf, locate_mem_hole_callback);
	else
		ret = kexec_walk_memblock(kbuf, locate_mem_hole_callback);

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
	ret = arch_kexec_locate_mem_hole(kbuf);
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

	if (!IS_ENABLED(CONFIG_ARCH_HAS_KEXEC_PURGATORY))
		return 0;

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
	if (!sha_regions) {
		ret = -ENOMEM;
		goto out_free_desc;
	}

	desc->tfm   = tfm;

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

#ifdef CONFIG_ARCH_HAS_KEXEC_PURGATORY
/*
 * kexec_purgatory_setup_kbuf - prepare buffer to load purgatory.
 * @pi:		Purgatory to be loaded.
 * @kbuf:	Buffer to setup.
 *
 * Allocates the memory needed for the buffer. Caller is responsible to free
 * the memory after use.
 *
 * Return: 0 on success, negative errno on error.
 */
static int kexec_purgatory_setup_kbuf(struct purgatory_info *pi,
				      struct kexec_buf *kbuf)
{
	const Elf_Shdr *sechdrs;
	unsigned long bss_align;
	unsigned long bss_sz;
	unsigned long align;
	int i, ret;

	sechdrs = (void *)pi->ehdr + pi->ehdr->e_shoff;
	kbuf->buf_align = bss_align = 1;
	kbuf->bufsz = bss_sz = 0;

	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		align = sechdrs[i].sh_addralign;
		if (sechdrs[i].sh_type != SHT_NOBITS) {
			if (kbuf->buf_align < align)
				kbuf->buf_align = align;
			kbuf->bufsz = ALIGN(kbuf->bufsz, align);
			kbuf->bufsz += sechdrs[i].sh_size;
		} else {
			if (bss_align < align)
				bss_align = align;
			bss_sz = ALIGN(bss_sz, align);
			bss_sz += sechdrs[i].sh_size;
		}
	}
	kbuf->bufsz = ALIGN(kbuf->bufsz, bss_align);
	kbuf->memsz = kbuf->bufsz + bss_sz;
	if (kbuf->buf_align < bss_align)
		kbuf->buf_align = bss_align;

	kbuf->buffer = vzalloc(kbuf->bufsz);
	if (!kbuf->buffer)
		return -ENOMEM;
	pi->purgatory_buf = kbuf->buffer;

	ret = kexec_add_buffer(kbuf);
	if (ret)
		goto out;

	return 0;
out:
	vfree(pi->purgatory_buf);
	pi->purgatory_buf = NULL;
	return ret;
}

/*
 * kexec_purgatory_setup_sechdrs - prepares the pi->sechdrs buffer.
 * @pi:		Purgatory to be loaded.
 * @kbuf:	Buffer prepared to store purgatory.
 *
 * Allocates the memory needed for the buffer. Caller is responsible to free
 * the memory after use.
 *
 * Return: 0 on success, negative errno on error.
 */
static int kexec_purgatory_setup_sechdrs(struct purgatory_info *pi,
					 struct kexec_buf *kbuf)
{
	unsigned long bss_addr;
	unsigned long offset;
	Elf_Shdr *sechdrs;
	int i;

	/*
	 * The section headers in kexec_purgatory are read-only. In order to
	 * have them modifiable make a temporary copy.
	 */
	sechdrs = vzalloc(array_size(sizeof(Elf_Shdr), pi->ehdr->e_shnum));
	if (!sechdrs)
		return -ENOMEM;
	memcpy(sechdrs, (void *)pi->ehdr + pi->ehdr->e_shoff,
	       pi->ehdr->e_shnum * sizeof(Elf_Shdr));
	pi->sechdrs = sechdrs;

	offset = 0;
	bss_addr = kbuf->mem + kbuf->bufsz;
	kbuf->image->start = pi->ehdr->e_entry;

	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		unsigned long align;
		void *src, *dst;

		if (!(sechdrs[i].sh_flags & SHF_ALLOC))
			continue;

		align = sechdrs[i].sh_addralign;
		if (sechdrs[i].sh_type == SHT_NOBITS) {
			bss_addr = ALIGN(bss_addr, align);
			sechdrs[i].sh_addr = bss_addr;
			bss_addr += sechdrs[i].sh_size;
			continue;
		}

		offset = ALIGN(offset, align);
		if (sechdrs[i].sh_flags & SHF_EXECINSTR &&
		    pi->ehdr->e_entry >= sechdrs[i].sh_addr &&
		    pi->ehdr->e_entry < (sechdrs[i].sh_addr
					 + sechdrs[i].sh_size)) {
			kbuf->image->start -= sechdrs[i].sh_addr;
			kbuf->image->start += kbuf->mem + offset;
		}

		src = (void *)pi->ehdr + sechdrs[i].sh_offset;
		dst = pi->purgatory_buf + offset;
		memcpy(dst, src, sechdrs[i].sh_size);

		sechdrs[i].sh_addr = kbuf->mem + offset;
		sechdrs[i].sh_offset = offset;
		offset += sechdrs[i].sh_size;
	}

	return 0;
}

static int kexec_apply_relocations(struct kimage *image)
{
	int i, ret;
	struct purgatory_info *pi = &image->purgatory_info;
	const Elf_Shdr *sechdrs;

	sechdrs = (void *)pi->ehdr + pi->ehdr->e_shoff;

	for (i = 0; i < pi->ehdr->e_shnum; i++) {
		const Elf_Shdr *relsec;
		const Elf_Shdr *symtab;
		Elf_Shdr *section;

		relsec = sechdrs + i;

		if (relsec->sh_type != SHT_RELA &&
		    relsec->sh_type != SHT_REL)
			continue;

		/*
		 * For section of type SHT_RELA/SHT_REL,
		 * ->sh_link contains section header index of associated
		 * symbol table. And ->sh_info contains section header
		 * index of section to which relocations apply.
		 */
		if (relsec->sh_info >= pi->ehdr->e_shnum ||
		    relsec->sh_link >= pi->ehdr->e_shnum)
			return -ENOEXEC;

		section = pi->sechdrs + relsec->sh_info;
		symtab = sechdrs + relsec->sh_link;

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
		if (relsec->sh_type == SHT_RELA)
			ret = arch_kexec_apply_relocations_add(pi, section,
							       relsec, symtab);
		else if (relsec->sh_type == SHT_REL)
			ret = arch_kexec_apply_relocations(pi, section,
							   relsec, symtab);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * kexec_load_purgatory - Load and relocate the purgatory object.
 * @image:	Image to add the purgatory to.
 * @kbuf:	Memory parameters to use.
 *
 * Allocates the memory needed for image->purgatory_info.sechdrs and
 * image->purgatory_info.purgatory_buf/kbuf->buffer. Caller is responsible
 * to free the memory after use.
 *
 * Return: 0 on success, negative errno on error.
 */
int kexec_load_purgatory(struct kimage *image, struct kexec_buf *kbuf)
{
	struct purgatory_info *pi = &image->purgatory_info;
	int ret;

	if (kexec_purgatory_size <= 0)
		return -EINVAL;

	pi->ehdr = (const Elf_Ehdr *)kexec_purgatory;

	ret = kexec_purgatory_setup_kbuf(pi, kbuf);
	if (ret)
		return ret;

	ret = kexec_purgatory_setup_sechdrs(pi, kbuf);
	if (ret)
		goto out_free_kbuf;

	ret = kexec_apply_relocations(image);
	if (ret)
		goto out;

	return 0;
out:
	vfree(pi->sechdrs);
	pi->sechdrs = NULL;
out_free_kbuf:
	vfree(pi->purgatory_buf);
	pi->purgatory_buf = NULL;
	return ret;
}

/*
 * kexec_purgatory_find_symbol - find a symbol in the purgatory
 * @pi:		Purgatory to search in.
 * @name:	Name of the symbol.
 *
 * Return: pointer to symbol in read-only symtab on success, NULL on error.
 */
static const Elf_Sym *kexec_purgatory_find_symbol(struct purgatory_info *pi,
						  const char *name)
{
	const Elf_Shdr *sechdrs;
	const Elf_Ehdr *ehdr;
	const Elf_Sym *syms;
	const char *strtab;
	int i, k;

	if (!pi->ehdr)
		return NULL;

	ehdr = pi->ehdr;
	sechdrs = (void *)ehdr + ehdr->e_shoff;

	for (i = 0; i < ehdr->e_shnum; i++) {
		if (sechdrs[i].sh_type != SHT_SYMTAB)
			continue;

		if (sechdrs[i].sh_link >= ehdr->e_shnum)
			/* Invalid strtab section number */
			continue;
		strtab = (void *)ehdr + sechdrs[sechdrs[i].sh_link].sh_offset;
		syms = (void *)ehdr + sechdrs[i].sh_offset;

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
	const Elf_Sym *sym;
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
	struct purgatory_info *pi = &image->purgatory_info;
	const Elf_Sym *sym;
	Elf_Shdr *sec;
	char *sym_buf;

	sym = kexec_purgatory_find_symbol(pi, name);
	if (!sym)
		return -EINVAL;

	if (sym->st_size != size) {
		pr_err("symbol %s size mismatch: expected %lu actual %u\n",
		       name, (unsigned long)sym->st_size, size);
		return -EINVAL;
	}

	sec = pi->sechdrs + sym->st_shndx;

	if (sec->sh_type == SHT_NOBITS) {
		pr_err("symbol %s is in a bss section. Cannot %s\n", name,
		       get_value ? "get" : "set");
		return -EINVAL;
	}

	sym_buf = (char *)pi->purgatory_buf + sec->sh_offset + sym->st_value;

	if (get_value)
		memcpy((void *)buf, sym_buf, size);
	else
		memcpy((void *)sym_buf, buf, size);

	return 0;
}
#endif /* CONFIG_ARCH_HAS_KEXEC_PURGATORY */

int crash_exclude_mem_range(struct crash_mem *mem,
			    unsigned long long mstart, unsigned long long mend)
{
	int i, j;
	unsigned long long start, end, p_start, p_end;
	struct crash_mem_range temp_range = {0, 0};

	for (i = 0; i < mem->nr_ranges; i++) {
		start = mem->ranges[i].start;
		end = mem->ranges[i].end;
		p_start = mstart;
		p_end = mend;

		if (mstart > end || mend < start)
			continue;

		/* Truncate any area outside of range */
		if (mstart < start)
			p_start = start;
		if (mend > end)
			p_end = end;

		/* Found completely overlapping range */
		if (p_start == start && p_end == end) {
			mem->ranges[i].start = 0;
			mem->ranges[i].end = 0;
			if (i < mem->nr_ranges - 1) {
				/* Shift rest of the ranges to left */
				for (j = i; j < mem->nr_ranges - 1; j++) {
					mem->ranges[j].start =
						mem->ranges[j+1].start;
					mem->ranges[j].end =
							mem->ranges[j+1].end;
				}

				/*
				 * Continue to check if there are another overlapping ranges
				 * from the current position because of shifting the above
				 * mem ranges.
				 */
				i--;
				mem->nr_ranges--;
				continue;
			}
			mem->nr_ranges--;
			return 0;
		}

		if (p_start > start && p_end < end) {
			/* Split original range */
			mem->ranges[i].end = p_start - 1;
			temp_range.start = p_end + 1;
			temp_range.end = end;
		} else if (p_start != start)
			mem->ranges[i].end = p_start - 1;
		else
			mem->ranges[i].start = p_end + 1;
		break;
	}

	/* If a split happened, add the split to array */
	if (!temp_range.end)
		return 0;

	/* Split happened */
	if (i == mem->max_nr_ranges - 1)
		return -ENOMEM;

	/* Location where new range should go */
	j = i + 1;
	if (j < mem->nr_ranges) {
		/* Move over all ranges one slot towards the end */
		for (i = mem->nr_ranges - 1; i >= j; i--)
			mem->ranges[i + 1] = mem->ranges[i];
	}

	mem->ranges[j].start = temp_range.start;
	mem->ranges[j].end = temp_range.end;
	mem->nr_ranges++;
	return 0;
}

int crash_prepare_elf64_headers(struct crash_mem *mem, int need_kernel_map,
			  void **addr, unsigned long *sz)
{
	Elf64_Ehdr *ehdr;
	Elf64_Phdr *phdr;
	unsigned long nr_cpus = num_possible_cpus(), nr_phdr, elf_sz;
	unsigned char *buf;
	unsigned int cpu, i;
	unsigned long long notes_addr;
	unsigned long mstart, mend;

	/* extra phdr for vmcoreinfo ELF note */
	nr_phdr = nr_cpus + 1;
	nr_phdr += mem->nr_ranges;

	/*
	 * kexec-tools creates an extra PT_LOAD phdr for kernel text mapping
	 * area (for example, ffffffff80000000 - ffffffffa0000000 on x86_64).
	 * I think this is required by tools like gdb. So same physical
	 * memory will be mapped in two ELF headers. One will contain kernel
	 * text virtual addresses and other will have __va(physical) addresses.
	 */

	nr_phdr++;
	elf_sz = sizeof(Elf64_Ehdr) + nr_phdr * sizeof(Elf64_Phdr);
	elf_sz = ALIGN(elf_sz, ELF_CORE_HEADER_ALIGN);

	buf = vzalloc(elf_sz);
	if (!buf)
		return -ENOMEM;

	ehdr = (Elf64_Ehdr *)buf;
	phdr = (Elf64_Phdr *)(ehdr + 1);
	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS64;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_ident[EI_OSABI] = ELF_OSABI;
	memset(ehdr->e_ident + EI_PAD, 0, EI_NIDENT - EI_PAD);
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = ELF_ARCH;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_phoff = sizeof(Elf64_Ehdr);
	ehdr->e_ehsize = sizeof(Elf64_Ehdr);
	ehdr->e_phentsize = sizeof(Elf64_Phdr);

	/* Prepare one phdr of type PT_NOTE for each present CPU */
	for_each_present_cpu(cpu) {
		phdr->p_type = PT_NOTE;
		notes_addr = per_cpu_ptr_to_phys(per_cpu_ptr(crash_notes, cpu));
		phdr->p_offset = phdr->p_paddr = notes_addr;
		phdr->p_filesz = phdr->p_memsz = sizeof(note_buf_t);
		(ehdr->e_phnum)++;
		phdr++;
	}

	/* Prepare one PT_NOTE header for vmcoreinfo */
	phdr->p_type = PT_NOTE;
	phdr->p_offset = phdr->p_paddr = paddr_vmcoreinfo_note();
	phdr->p_filesz = phdr->p_memsz = VMCOREINFO_NOTE_SIZE;
	(ehdr->e_phnum)++;
	phdr++;

	/* Prepare PT_LOAD type program header for kernel text region */
	if (need_kernel_map) {
		phdr->p_type = PT_LOAD;
		phdr->p_flags = PF_R|PF_W|PF_X;
		phdr->p_vaddr = (unsigned long) _text;
		phdr->p_filesz = phdr->p_memsz = _end - _text;
		phdr->p_offset = phdr->p_paddr = __pa_symbol(_text);
		ehdr->e_phnum++;
		phdr++;
	}

	/* Go through all the ranges in mem->ranges[] and prepare phdr */
	for (i = 0; i < mem->nr_ranges; i++) {
		mstart = mem->ranges[i].start;
		mend = mem->ranges[i].end;

		phdr->p_type = PT_LOAD;
		phdr->p_flags = PF_R|PF_W|PF_X;
		phdr->p_offset  = mstart;

		phdr->p_paddr = mstart;
		phdr->p_vaddr = (unsigned long) __va(mstart);
		phdr->p_filesz = phdr->p_memsz = mend - mstart + 1;
		phdr->p_align = 0;
		ehdr->e_phnum++;
		pr_debug("Crash PT_LOAD ELF header. phdr=%p vaddr=0x%llx, paddr=0x%llx, sz=0x%llx e_phnum=%d p_offset=0x%llx\n",
			phdr, phdr->p_vaddr, phdr->p_paddr, phdr->p_filesz,
			ehdr->e_phnum, phdr->p_offset);
		phdr++;
	}

	*addr = buf;
	*sz = elf_sz;
	return 0;
}
