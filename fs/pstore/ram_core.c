// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Google, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/pstore_ram.h>
#include <linux/rslib.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <asm/page.h>

/**
 * struct persistent_ram_buffer - persistent circular RAM buffer
 *
 * @sig:
 *	signature to indicate header (PERSISTENT_RAM_SIG xor PRZ-type value)
 * @start:
 *	offset into @data where the beginning of the stored bytes begin
 * @size:
 *	number of valid bytes stored in @data
 */
struct persistent_ram_buffer {
	uint32_t    sig;
	atomic_t    start;
	atomic_t    size;
	uint8_t     data[];
};

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */

static inline size_t buffer_size(struct persistent_ram_zone *prz)
{
	return atomic_read(&prz->buffer->size);
}

static inline size_t buffer_start(struct persistent_ram_zone *prz)
{
	return atomic_read(&prz->buffer->start);
}

/* increase and wrap the start pointer, returning the old value */
static size_t buffer_start_add(struct persistent_ram_zone *prz, size_t a)
{
	int old;
	int new;
	unsigned long flags = 0;

	if (!(prz->flags & PRZ_FLAG_NO_LOCK))
		raw_spin_lock_irqsave(&prz->buffer_lock, flags);

	old = atomic_read(&prz->buffer->start);
	new = old + a;
	while (unlikely(new >= prz->buffer_size))
		new -= prz->buffer_size;
	atomic_set(&prz->buffer->start, new);

	if (!(prz->flags & PRZ_FLAG_NO_LOCK))
		raw_spin_unlock_irqrestore(&prz->buffer_lock, flags);

	return old;
}

/* increase the size counter until it hits the max size */
static void buffer_size_add(struct persistent_ram_zone *prz, size_t a)
{
	size_t old;
	size_t new;
	unsigned long flags = 0;

	if (!(prz->flags & PRZ_FLAG_NO_LOCK))
		raw_spin_lock_irqsave(&prz->buffer_lock, flags);

	old = atomic_read(&prz->buffer->size);
	if (old == prz->buffer_size)
		goto exit;

	new = old + a;
	if (new > prz->buffer_size)
		new = prz->buffer_size;
	atomic_set(&prz->buffer->size, new);

exit:
	if (!(prz->flags & PRZ_FLAG_NO_LOCK))
		raw_spin_unlock_irqrestore(&prz->buffer_lock, flags);
}

static void notrace persistent_ram_encode_rs8(struct persistent_ram_zone *prz,
	uint8_t *data, size_t len, uint8_t *ecc)
{
	int i;

	/* Initialize the parity buffer */
	memset(prz->ecc_info.par, 0,
	       prz->ecc_info.ecc_size * sizeof(prz->ecc_info.par[0]));
	encode_rs8(prz->rs_decoder, data, len, prz->ecc_info.par, 0);
	for (i = 0; i < prz->ecc_info.ecc_size; i++)
		ecc[i] = prz->ecc_info.par[i];
}

static int persistent_ram_decode_rs8(struct persistent_ram_zone *prz,
	void *data, size_t len, uint8_t *ecc)
{
	int i;

	for (i = 0; i < prz->ecc_info.ecc_size; i++)
		prz->ecc_info.par[i] = ecc[i];
	return decode_rs8(prz->rs_decoder, data, prz->ecc_info.par, len,
				NULL, 0, NULL, 0, NULL);
}

static void notrace persistent_ram_update_ecc(struct persistent_ram_zone *prz,
	unsigned int start, unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	uint8_t *buffer_end = buffer->data + prz->buffer_size;
	uint8_t *block;
	uint8_t *par;
	int ecc_block_size = prz->ecc_info.block_size;
	int ecc_size = prz->ecc_info.ecc_size;
	int size = ecc_block_size;

	if (!ecc_size)
		return;

	block = buffer->data + (start & ~(ecc_block_size - 1));
	par = prz->par_buffer + (start / ecc_block_size) * ecc_size;

	do {
		if (block + ecc_block_size > buffer_end)
			size = buffer_end - block;
		persistent_ram_encode_rs8(prz, block, size, par);
		block += ecc_block_size;
		par += ecc_size;
	} while (block < buffer->data + start + count);
}

static void persistent_ram_update_header_ecc(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;

	if (!prz->ecc_info.ecc_size)
		return;

	persistent_ram_encode_rs8(prz, (uint8_t *)buffer, sizeof(*buffer),
				  prz->par_header);
}

static void persistent_ram_ecc_old(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	uint8_t *block;
	uint8_t *par;

	if (!prz->ecc_info.ecc_size)
		return;

	block = buffer->data;
	par = prz->par_buffer;
	while (block < buffer->data + buffer_size(prz)) {
		int numerr;
		int size = prz->ecc_info.block_size;
		if (block + size > buffer->data + prz->buffer_size)
			size = buffer->data + prz->buffer_size - block;
		numerr = persistent_ram_decode_rs8(prz, block, size, par);
		if (numerr > 0) {
			pr_devel("error in block %p, %d\n", block, numerr);
			prz->corrected_bytes += numerr;
		} else if (numerr < 0) {
			pr_devel("uncorrectable error in block %p\n", block);
			prz->bad_blocks++;
		}
		block += prz->ecc_info.block_size;
		par += prz->ecc_info.ecc_size;
	}
}

static int persistent_ram_init_ecc(struct persistent_ram_zone *prz,
				   struct persistent_ram_ecc_info *ecc_info)
{
	int numerr;
	struct persistent_ram_buffer *buffer = prz->buffer;
	int ecc_blocks;
	size_t ecc_total;

	if (!ecc_info || !ecc_info->ecc_size)
		return 0;

	prz->ecc_info.block_size = ecc_info->block_size ?: 128;
	prz->ecc_info.ecc_size = ecc_info->ecc_size ?: 16;
	prz->ecc_info.symsize = ecc_info->symsize ?: 8;
	prz->ecc_info.poly = ecc_info->poly ?: 0x11d;

	ecc_blocks = DIV_ROUND_UP(prz->buffer_size - prz->ecc_info.ecc_size,
				  prz->ecc_info.block_size +
				  prz->ecc_info.ecc_size);
	ecc_total = (ecc_blocks + 1) * prz->ecc_info.ecc_size;
	if (ecc_total >= prz->buffer_size) {
		pr_err("%s: invalid ecc_size %u (total %zu, buffer size %zu)\n",
		       __func__, prz->ecc_info.ecc_size,
		       ecc_total, prz->buffer_size);
		return -EINVAL;
	}

	prz->buffer_size -= ecc_total;
	prz->par_buffer = buffer->data + prz->buffer_size;
	prz->par_header = prz->par_buffer +
			  ecc_blocks * prz->ecc_info.ecc_size;

	/*
	 * first consecutive root is 0
	 * primitive element to generate roots = 1
	 */
	prz->rs_decoder = init_rs(prz->ecc_info.symsize, prz->ecc_info.poly,
				  0, 1, prz->ecc_info.ecc_size);
	if (prz->rs_decoder == NULL) {
		pr_info("init_rs failed\n");
		return -EINVAL;
	}

	/* allocate workspace instead of using stack VLA */
	prz->ecc_info.par = kmalloc_array(prz->ecc_info.ecc_size,
					  sizeof(*prz->ecc_info.par),
					  GFP_KERNEL);
	if (!prz->ecc_info.par) {
		pr_err("cannot allocate ECC parity workspace\n");
		return -ENOMEM;
	}

	prz->corrected_bytes = 0;
	prz->bad_blocks = 0;

	numerr = persistent_ram_decode_rs8(prz, buffer, sizeof(*buffer),
					   prz->par_header);
	if (numerr > 0) {
		pr_info("error in header, %d\n", numerr);
		prz->corrected_bytes += numerr;
	} else if (numerr < 0) {
		pr_info("uncorrectable error in header\n");
		prz->bad_blocks++;
	}

	return 0;
}

ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len)
{
	ssize_t ret;

	if (!prz->ecc_info.ecc_size)
		return 0;

	if (prz->corrected_bytes || prz->bad_blocks)
		ret = snprintf(str, len, ""
			"\n%d Corrected bytes, %d unrecoverable blocks\n",
			prz->corrected_bytes, prz->bad_blocks);
	else
		ret = snprintf(str, len, "\nNo errors detected\n");

	return ret;
}

static void notrace persistent_ram_update(struct persistent_ram_zone *prz,
	const void *s, unsigned int start, unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	memcpy_toio(buffer->data + start, s, count);
	persistent_ram_update_ecc(prz, start, count);
}

static int notrace persistent_ram_update_user(struct persistent_ram_zone *prz,
	const void __user *s, unsigned int start, unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	int ret = unlikely(copy_from_user(buffer->data + start, s, count)) ?
		-EFAULT : 0;
	persistent_ram_update_ecc(prz, start, count);
	return ret;
}

void persistent_ram_save_old(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	size_t size = buffer_size(prz);
	size_t start = buffer_start(prz);

	if (!size)
		return;

	if (!prz->old_log) {
		persistent_ram_ecc_old(prz);
		prz->old_log = kmalloc(size, GFP_KERNEL);
	}
	if (!prz->old_log) {
		pr_err("failed to allocate buffer\n");
		return;
	}

	prz->old_log_size = size;
	memcpy_fromio(prz->old_log, &buffer->data[start], size - start);
	memcpy_fromio(prz->old_log + size - start, &buffer->data[0], start);
}

int notrace persistent_ram_write(struct persistent_ram_zone *prz,
	const void *s, unsigned int count)
{
	int rem;
	int c = count;
	size_t start;

	if (unlikely(c > prz->buffer_size)) {
		s += c - prz->buffer_size;
		c = prz->buffer_size;
	}

	buffer_size_add(prz, c);

	start = buffer_start_add(prz, c);

	rem = prz->buffer_size - start;
	if (unlikely(rem < c)) {
		persistent_ram_update(prz, s, start, rem);
		s += rem;
		c -= rem;
		start = 0;
	}
	persistent_ram_update(prz, s, start, c);

	persistent_ram_update_header_ecc(prz);

	return count;
}

int notrace persistent_ram_write_user(struct persistent_ram_zone *prz,
	const void __user *s, unsigned int count)
{
	int rem, ret = 0, c = count;
	size_t start;

	if (unlikely(c > prz->buffer_size)) {
		s += c - prz->buffer_size;
		c = prz->buffer_size;
	}

	buffer_size_add(prz, c);

	start = buffer_start_add(prz, c);

	rem = prz->buffer_size - start;
	if (unlikely(rem < c)) {
		ret = persistent_ram_update_user(prz, s, start, rem);
		s += rem;
		c -= rem;
		start = 0;
	}
	if (likely(!ret))
		ret = persistent_ram_update_user(prz, s, start, c);

	persistent_ram_update_header_ecc(prz);

	return unlikely(ret) ? ret : count;
}

size_t persistent_ram_old_size(struct persistent_ram_zone *prz)
{
	return prz->old_log_size;
}

void *persistent_ram_old(struct persistent_ram_zone *prz)
{
	return prz->old_log;
}

void persistent_ram_free_old(struct persistent_ram_zone *prz)
{
	kfree(prz->old_log);
	prz->old_log = NULL;
	prz->old_log_size = 0;
}

void persistent_ram_zap(struct persistent_ram_zone *prz)
{
	atomic_set(&prz->buffer->start, 0);
	atomic_set(&prz->buffer->size, 0);
	persistent_ram_update_header_ecc(prz);
}

static void *persistent_ram_vmap(phys_addr_t start, size_t size,
		unsigned int memtype)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	pgprot_t prot;
	unsigned int i;
	void *vaddr;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	if (memtype)
		prot = pgprot_noncached(PAGE_KERNEL);
	else
		prot = pgprot_writecombine(PAGE_KERNEL);

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n",
		       __func__, page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	/*
	 * VM_IOREMAP used here to bypass this region during vread()
	 * and kmap_atomic() (i.e. kcore) to avoid __va() failures.
	 */
	vaddr = vmap(pages, page_count, VM_MAP | VM_IOREMAP, prot);
	kfree(pages);

	/*
	 * Since vmap() uses page granularity, we must add the offset
	 * into the page here, to get the byte granularity address
	 * into the mapping to represent the actual "start" location.
	 */
	return vaddr + offset_in_page(start);
}

static void *persistent_ram_iomap(phys_addr_t start, size_t size,
		unsigned int memtype, char *label)
{
	void *va;

	if (!request_mem_region(start, size, label ?: "ramoops")) {
		pr_err("request mem region (%s 0x%llx@0x%llx) failed\n",
			label ?: "ramoops",
			(unsigned long long)size, (unsigned long long)start);
		return NULL;
	}

	if (memtype)
		va = ioremap(start, size);
	else
		va = ioremap_wc(start, size);

	/*
	 * Since request_mem_region() and ioremap() are byte-granularity
	 * there is no need handle anything special like we do when the
	 * vmap() case in persistent_ram_vmap() above.
	 */
	return va;
}

static int persistent_ram_buffer_map(phys_addr_t start, phys_addr_t size,
		struct persistent_ram_zone *prz, int memtype)
{
	prz->paddr = start;
	prz->size = size;

	if (pfn_valid(start >> PAGE_SHIFT))
		prz->vaddr = persistent_ram_vmap(start, size, memtype);
	else
		prz->vaddr = persistent_ram_iomap(start, size, memtype,
						  prz->label);

	if (!prz->vaddr) {
		pr_err("%s: Failed to map 0x%llx pages at 0x%llx\n", __func__,
			(unsigned long long)size, (unsigned long long)start);
		return -ENOMEM;
	}

	prz->buffer = prz->vaddr;
	prz->buffer_size = size - sizeof(struct persistent_ram_buffer);

	return 0;
}

static int persistent_ram_post_init(struct persistent_ram_zone *prz, u32 sig,
				    struct persistent_ram_ecc_info *ecc_info)
{
	int ret;
	bool zap = !!(prz->flags & PRZ_FLAG_ZAP_OLD);

	ret = persistent_ram_init_ecc(prz, ecc_info);
	if (ret) {
		pr_warn("ECC failed %s\n", prz->label);
		return ret;
	}

	sig ^= PERSISTENT_RAM_SIG;

	if (prz->buffer->sig == sig) {
		if (buffer_size(prz) == 0) {
			pr_debug("found existing empty buffer\n");
			return 0;
		}

		if (buffer_size(prz) > prz->buffer_size ||
		    buffer_start(prz) > buffer_size(prz)) {
			pr_info("found existing invalid buffer, size %zu, start %zu\n",
				buffer_size(prz), buffer_start(prz));
			zap = true;
		} else {
			pr_debug("found existing buffer, size %zu, start %zu\n",
				 buffer_size(prz), buffer_start(prz));
			persistent_ram_save_old(prz);
		}
	} else {
		pr_debug("no valid data in buffer (sig = 0x%08x)\n",
			 prz->buffer->sig);
		prz->buffer->sig = sig;
		zap = true;
	}

	/* Reset missing, invalid, or single-use memory area. */
	if (zap)
		persistent_ram_zap(prz);

	return 0;
}

void persistent_ram_free(struct persistent_ram_zone *prz)
{
	if (!prz)
		return;

	if (prz->vaddr) {
		if (pfn_valid(prz->paddr >> PAGE_SHIFT)) {
			/* We must vunmap() at page-granularity. */
			vunmap(prz->vaddr - offset_in_page(prz->paddr));
		} else {
			iounmap(prz->vaddr);
			release_mem_region(prz->paddr, prz->size);
		}
		prz->vaddr = NULL;
	}
	if (prz->rs_decoder) {
		free_rs(prz->rs_decoder);
		prz->rs_decoder = NULL;
	}
	kfree(prz->ecc_info.par);
	prz->ecc_info.par = NULL;

	persistent_ram_free_old(prz);
	kfree(prz->label);
	kfree(prz);
}

struct persistent_ram_zone *persistent_ram_new(phys_addr_t start, size_t size,
			u32 sig, struct persistent_ram_ecc_info *ecc_info,
			unsigned int memtype, u32 flags, char *label)
{
	struct persistent_ram_zone *prz;
	int ret = -ENOMEM;

	prz = kzalloc(sizeof(struct persistent_ram_zone), GFP_KERNEL);
	if (!prz) {
		pr_err("failed to allocate persistent ram zone\n");
		goto err;
	}

	/* Initialize general buffer state. */
	raw_spin_lock_init(&prz->buffer_lock);
	prz->flags = flags;
	prz->label = kstrdup(label, GFP_KERNEL);
	if (!prz->label)
		goto err;

	ret = persistent_ram_buffer_map(start, size, prz, memtype);
	if (ret)
		goto err;

	ret = persistent_ram_post_init(prz, sig, ecc_info);
	if (ret)
		goto err;

	pr_debug("attached %s 0x%zx@0x%llx: %zu header, %zu data, %zu ecc (%d/%d)\n",
		prz->label, prz->size, (unsigned long long)prz->paddr,
		sizeof(*prz->buffer), prz->buffer_size,
		prz->size - sizeof(*prz->buffer) - prz->buffer_size,
		prz->ecc_info.ecc_size, prz->ecc_info.block_size);

	return prz;
err:
	persistent_ram_free(prz);
	return ERR_PTR(ret);
}
