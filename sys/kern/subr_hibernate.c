/*	$OpenBSD: subr_hibernate.c,v 1.154 2025/09/15 14:15:54 krw Exp $	*/

/*
 * Copyright (c) 2011 Ariane van der Steldt <ariane@stack.nl>
 * Copyright (c) 2011 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/hibernate.h>
#include <sys/malloc.h>
#include <sys/param.h>
#include <sys/tree.h>
#include <sys/systm.h>
#include <sys/disklabel.h>
#include <sys/disk.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/atomic.h>

#include <uvm/uvm.h>
#include <uvm/uvm_swap.h>

#include <machine/hibernate.h>

/* Make sure the signature can fit in one block */
CTASSERT((offsetof(union hibernate_info, sec_size) + sizeof(u_int32_t)) <= DEV_BSIZE);

/*
 * Hibernate piglet layout information
 *
 * The piglet is a scratch area of memory allocated by the suspending kernel.
 * Its phys and virt addrs are recorded in the signature block. The piglet is
 * used to guarantee an unused area of memory that can be used by the resuming
 * kernel for various things. The piglet is excluded during unpack operations.
 * The piglet size is presently 4*HIBERNATE_CHUNK_SIZE (typically 4*4MB).
 *
 * Offset from piglet_base	Purpose
 * ----------------------------------------------------------------------------
 * 0				Private page for suspend I/O write functions
 * 1*PAGE_SIZE			I/O page used during hibernate suspend
 * 2*PAGE_SIZE			I/O page used during hibernate suspend
 * 3*PAGE_SIZE			copy page used during hibernate suspend
 * 4*PAGE_SIZE			final chunk ordering list (24 pages)
 * 28*PAGE_SIZE			RLE utility page
 * 29*PAGE_SIZE			start of hiballoc area
 * 30*PAGE_SIZE			preserved entropy
 * 110*PAGE_SIZE		end of hiballoc area (80 pages)
 * 366*PAGE_SIZE		end of retguard preservation region (256 pages)
 * ...				unused
 * HIBERNATE_CHUNK_SIZE		start of hibernate chunk table
 * 2*HIBERNATE_CHUNK_SIZE	bounce area for chunks being unpacked
 * 4*HIBERNATE_CHUNK_SIZE	end of piglet
 */

/* Temporary vaddr ranges used during hibernate */
vaddr_t hibernate_temp_page;
vaddr_t hibernate_copy_page;
vaddr_t hibernate_rle_page;

/* Hibernate info as read from disk during resume */
union hibernate_info disk_hib;
struct bdevsw *bdsw;

/*
 * Global copy of the pig start address. This needs to be a global as we
 * switch stacks after computing it - it can't be stored on the stack.
 */
paddr_t global_pig_start;

/*
 * Global copies of the piglet start addresses (PA/VA). We store these
 * as globals to avoid having to carry them around as parameters, as the
 * piglet is allocated early and freed late - its lifecycle extends beyond
 * that of the hibernate info union which is calculated on suspend/resume.
 */
vaddr_t global_piglet_va;
paddr_t global_piglet_pa;

/* #define HIB_DEBUG */
#ifdef HIB_DEBUG
int	hib_debug = 99;
#define DPRINTF(x...)     do { if (hib_debug) printf(x); } while (0)
#define DNPRINTF(n,x...)  do { if (hib_debug > (n)) printf(x); } while (0)
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

#ifndef NO_PROPOLICE
extern long __guard_local;
#endif /* ! NO_PROPOLICE */

/* Retguard phys address (need to skip this region during unpack) */
paddr_t retguard_start_phys, retguard_end_phys;
extern char __retguard_start, __retguard_end;

void hibernate_copy_chunk_to_piglet(paddr_t, vaddr_t, size_t);
int hibernate_calc_rle(paddr_t, paddr_t);
int hibernate_write_rle(union hibernate_info *, paddr_t, paddr_t, daddr_t *,
	size_t *);

#define MAX_RLE (HIBERNATE_CHUNK_SIZE / PAGE_SIZE)

/*
 * Hib alloc enforced alignment.
 */
#define HIB_ALIGN		8 /* bytes alignment */

/*
 * sizeof builtin operation, but with alignment constraint.
 */
#define HIB_SIZEOF(_type)	roundup(sizeof(_type), HIB_ALIGN)

struct hiballoc_entry {
	size_t			hibe_use;
	size_t			hibe_space;
	RBT_ENTRY(hiballoc_entry) hibe_entry;
};

#define IO_TYPE_IMG 1
#define IO_TYPE_CHK 2
#define IO_TYPE_SIG 3

int
hibernate_write(union hibernate_info *hib, daddr_t offset, vaddr_t addr,
    size_t size, int io_type)
{
	const uint64_t blks = btodb(size);

	if (hib == NULL || offset < 0 || blks == 0) {
		printf("%s: hib is NULL, offset < 0 or blks == 0\n", __func__);
		return (EINVAL);
	}

	switch (io_type) {
	case IO_TYPE_IMG:
		if (offset + blks > hib->image_size) {
			printf("%s: image write is out of bounds: "
			    "offset-image=%lld, offset-write=%lld, blks=%llu\n",
			    __func__, hib->image_offset, offset, blks);
			return (EIO);
		}
		offset += hib->image_offset;
		break;
	case IO_TYPE_CHK:
		if (offset + blks > btodb(HIBERNATE_CHUNK_TABLE_SIZE)) {
			printf("%s: chunktable write is out of bounds: "
			    "offset-chunk=%lld, offset-write=%lld, blks=%llu\n",
			    __func__, hib->chunktable_offset, offset, blks);
			return (EIO);
		}
		offset += hib->chunktable_offset;
		break;
	case IO_TYPE_SIG:
		if (offset != hib->sig_offset || size != hib->sec_size) {
			printf("%s: signature write is out of bounds: "
			    "offset-sig=%lld, offset-write=%lld, blks=%llu\n",
			    __func__, hib->sig_offset, offset, blks);
			return (EIO);
		}
		break;
	default:
		printf("%s: unsupported io type %d\n", __func__, io_type);
		return (EINVAL);
	}

	return (hib->io_func(hib->dev, offset, addr, size, HIB_W,
	    hib->io_page));
}

/*
 * Sort hibernate memory ranges by ascending PA
 */
void
hibernate_sort_ranges(union hibernate_info *hib_info)
{
	int i, j;
	struct hibernate_memory_range *ranges;
	paddr_t base, end;

	ranges = hib_info->ranges;

	for (i = 1; i < hib_info->nranges; i++) {
		j = i;
		while (j > 0 && ranges[j - 1].base > ranges[j].base) {
			base = ranges[j].base;
			end = ranges[j].end;
			ranges[j].base = ranges[j - 1].base;
			ranges[j].end = ranges[j - 1].end;
			ranges[j - 1].base = base;
			ranges[j - 1].end = end;
			j--;
		}
	}
}

/*
 * Compare hiballoc entries based on the address they manage.
 *
 * Since the address is fixed, relative to struct hiballoc_entry,
 * we just compare the hiballoc_entry pointers.
 */
static __inline int
hibe_cmp(const struct hiballoc_entry *l, const struct hiballoc_entry *r)
{
	vaddr_t vl = (vaddr_t)l;
	vaddr_t vr = (vaddr_t)r;

	return vl < vr ? -1 : (vl > vr);
}

RBT_PROTOTYPE(hiballoc_addr, hiballoc_entry, hibe_entry, hibe_cmp)

/*
 * Given a hiballoc entry, return the address it manages.
 */
static __inline void *
hib_entry_to_addr(struct hiballoc_entry *entry)
{
	caddr_t addr;

	addr = (caddr_t)entry;
	addr += HIB_SIZEOF(struct hiballoc_entry);
	return addr;
}

/*
 * Given an address, find the hiballoc that corresponds.
 */
static __inline struct hiballoc_entry*
hib_addr_to_entry(void *addr_param)
{
	caddr_t addr;

	addr = (caddr_t)addr_param;
	addr -= HIB_SIZEOF(struct hiballoc_entry);
	return (struct hiballoc_entry*)addr;
}

RBT_GENERATE(hiballoc_addr, hiballoc_entry, hibe_entry, hibe_cmp);

/*
 * Allocate memory from the arena.
 *
 * Returns NULL if no memory is available.
 */
void *
hib_alloc(struct hiballoc_arena *arena, size_t alloc_sz)
{
	struct hiballoc_entry *entry, *new_entry;
	size_t find_sz;

	/*
	 * Enforce alignment of HIB_ALIGN bytes.
	 *
	 * Note that, because the entry is put in front of the allocation,
	 * 0-byte allocations are guaranteed a unique address.
	 */
	alloc_sz = roundup(alloc_sz, HIB_ALIGN);

	/*
	 * Find an entry with hibe_space >= find_sz.
	 *
	 * If the root node is not large enough, we switch to tree traversal.
	 * Because all entries are made at the bottom of the free space,
	 * traversal from the end has a slightly better chance of yielding
	 * a sufficiently large space.
	 */
	find_sz = alloc_sz + HIB_SIZEOF(struct hiballoc_entry);
	entry = RBT_ROOT(hiballoc_addr, &arena->hib_addrs);
	if (entry != NULL && entry->hibe_space < find_sz) {
		RBT_FOREACH_REVERSE(entry, hiballoc_addr, &arena->hib_addrs) {
			if (entry->hibe_space >= find_sz)
				break;
		}
	}

	/*
	 * Insufficient or too fragmented memory.
	 */
	if (entry == NULL)
		return NULL;

	/*
	 * Create new entry in allocated space.
	 */
	new_entry = (struct hiballoc_entry*)(
	    (caddr_t)hib_entry_to_addr(entry) + entry->hibe_use);
	new_entry->hibe_space = entry->hibe_space - find_sz;
	new_entry->hibe_use = alloc_sz;

	/*
	 * Insert entry.
	 */
	if (RBT_INSERT(hiballoc_addr, &arena->hib_addrs, new_entry) != NULL)
		panic("hib_alloc: insert failure");
	entry->hibe_space = 0;

	/* Return address managed by entry. */
	return hib_entry_to_addr(new_entry);
}

void
hib_getentropy(char **bufp, size_t *bufplen)
{
	if (!bufp || !bufplen)
		return;

	*bufp = (char *)(global_piglet_va + (29 * PAGE_SIZE));
	*bufplen = PAGE_SIZE;
}

/*
 * Free a pointer previously allocated from this arena.
 *
 * If addr is NULL, this will be silently accepted.
 */
void
hib_free(struct hiballoc_arena *arena, void *addr)
{
	struct hiballoc_entry *entry, *prev;

	if (addr == NULL)
		return;

	/*
	 * Derive entry from addr and check it is really in this arena.
	 */
	entry = hib_addr_to_entry(addr);
	if (RBT_FIND(hiballoc_addr, &arena->hib_addrs, entry) != entry)
		panic("hib_free: freed item %p not in hib arena", addr);

	/*
	 * Give the space in entry to its predecessor.
	 *
	 * If entry has no predecessor, change its used space into free space
	 * instead.
	 */
	prev = RBT_PREV(hiballoc_addr, entry);
	if (prev != NULL &&
	    (void *)((caddr_t)prev + HIB_SIZEOF(struct hiballoc_entry) +
	    prev->hibe_use + prev->hibe_space) == entry) {
		/* Merge entry. */
		RBT_REMOVE(hiballoc_addr, &arena->hib_addrs, entry);
		prev->hibe_space += HIB_SIZEOF(struct hiballoc_entry) +
		    entry->hibe_use + entry->hibe_space;
	} else {
		/* Flip used memory to free space. */
		entry->hibe_space += entry->hibe_use;
		entry->hibe_use = 0;
	}
}

/*
 * Initialize hiballoc.
 *
 * The allocator will manage memory at ptr, which is len bytes.
 */
int
hiballoc_init(struct hiballoc_arena *arena, void *p_ptr, size_t p_len)
{
	struct hiballoc_entry *entry;
	caddr_t ptr;
	size_t len;

	RBT_INIT(hiballoc_addr, &arena->hib_addrs);

	/*
	 * Hib allocator enforces HIB_ALIGN alignment.
	 * Fixup ptr and len.
	 */
	ptr = (caddr_t)roundup((vaddr_t)p_ptr, HIB_ALIGN);
	len = p_len - ((size_t)ptr - (size_t)p_ptr);
	len &= ~((size_t)HIB_ALIGN - 1);

	/*
	 * Insufficient memory to be able to allocate and also do bookkeeping.
	 */
	if (len <= HIB_SIZEOF(struct hiballoc_entry))
		return ENOMEM;

	/*
	 * Create entry describing space.
	 */
	entry = (struct hiballoc_entry*)ptr;
	entry->hibe_use = 0;
	entry->hibe_space = len - HIB_SIZEOF(struct hiballoc_entry);
	RBT_INSERT(hiballoc_addr, &arena->hib_addrs, entry);

	return 0;
}

/*
 * Mark all memory as dirty.
 *
 * Used to inform the system that there are no pre-zero'd (PG_ZERO) free pages
 * when we came back from hibernate.
 */
void
uvm_pmr_dirty_everything(void)
{
	struct uvm_pmemrange	*pmr;
	struct vm_page		*pg;
	int			 i;

	uvm_lock_fpageq();
	TAILQ_FOREACH(pmr, &uvm.pmr_control.use, pmr_use) {
		/* Dirty single pages. */
		while ((pg = TAILQ_FIRST(&pmr->single[UVM_PMR_MEMTYPE_ZERO]))
		    != NULL) {
			uvm_pmr_remove(pmr, pg);
			atomic_clearbits_int(&pg->pg_flags, PG_ZERO);
			uvm_pmr_insert(pmr, pg, 0);
		}

		/* Dirty multi page ranges. */
		while ((pg = RBT_ROOT(uvm_pmr_size,
		    &pmr->size[UVM_PMR_MEMTYPE_ZERO])) != NULL) {
			pg--; /* Size tree always has second page. */
			uvm_pmr_remove(pmr, pg);
			for (i = 0; i < pg->fpgsz; i++)
				atomic_clearbits_int(&pg[i].pg_flags, PG_ZERO);
			uvm_pmr_insert(pmr, pg, 0);
		}
	}

	uvmexp.zeropages = 0;
	uvm_unlock_fpageq();
}

/*
 * Allocate an area that can hold sz bytes and doesn't overlap with
 * the piglet at piglet_pa.
 */
int
uvm_pmr_alloc_pig(paddr_t *pa, psize_t sz, paddr_t piglet_pa)
{
	struct uvm_constraint_range pig_constraint;
	struct kmem_pa_mode kp_pig = {
		.kp_constraint = &pig_constraint,
		.kp_maxseg = 1
	};
	vaddr_t va;

	sz = round_page(sz);

	pig_constraint.ucr_low = piglet_pa + 4 * HIBERNATE_CHUNK_SIZE;
	pig_constraint.ucr_high = -1;

	va = (vaddr_t)km_alloc(sz, &kv_any, &kp_pig, &kd_nowait);
	if (va == 0) {
		pig_constraint.ucr_low = 0;
		pig_constraint.ucr_high = piglet_pa - 1;

		va = (vaddr_t)km_alloc(sz, &kv_any, &kp_pig, &kd_nowait);
		if (va == 0)
			return ENOMEM;
	}

	pmap_extract(pmap_kernel(), va, pa);
	return 0;
}

/*
 * Allocate a piglet area.
 *
 * This needs to be in DMA-safe memory.
 * Piglets are aligned.
 *
 * sz and align in bytes.
 */
int
uvm_pmr_alloc_piglet(vaddr_t *va, paddr_t *pa, vsize_t sz, paddr_t align)
{
	struct kmem_pa_mode kp_piglet = {
		.kp_constraint = &dma_constraint,
		.kp_align = align,
		.kp_maxseg = 1
	};

	/* Ensure align is a power of 2 */
	KASSERT((align & (align - 1)) == 0);

	/*
	 * Fixup arguments: align must be at least PAGE_SIZE,
	 * sz will be converted to pagecount, since that is what
	 * pmemrange uses internally.
	 */
	if (align < PAGE_SIZE)
		kp_piglet.kp_align = PAGE_SIZE;

	sz = round_page(sz);

	*va = (vaddr_t)km_alloc(sz, &kv_any, &kp_piglet, &kd_nowait);
	if (*va == 0)
		return ENOMEM;

	pmap_extract(pmap_kernel(), *va, pa);
	return 0;
}

/*
 * Physmem RLE compression support.
 *
 * Given a physical page address, return the number of pages starting at the
 * address that are free.  Clamps to the number of pages in
 * HIBERNATE_CHUNK_SIZE. Returns 0 if the page at addr is not free.
 */
int
uvm_page_rle(paddr_t addr)
{
	struct vm_page		*pg, *pg_end;
	struct vm_physseg	*vmp;
	int			 pseg_idx, off_idx;

	pseg_idx = vm_physseg_find(atop(addr), &off_idx);
	if (pseg_idx == -1)
		return 0;

	vmp = &vm_physmem[pseg_idx];
	pg = &vmp->pgs[off_idx];
	if (!(pg->pg_flags & PQ_FREE))
		return 0;

	/*
	 * Search for the first non-free page after pg.
	 * Note that the page may not be the first page in a free pmemrange,
	 * therefore pg->fpgsz cannot be used.
	 */
	for (pg_end = pg; pg_end <= vmp->lastpg &&
	    (pg_end->pg_flags & PQ_FREE) == PQ_FREE &&
	    (pg_end - pg) < HIBERNATE_CHUNK_SIZE/PAGE_SIZE; pg_end++)
		;
	return pg_end - pg;
}

/*
 * Fills out the hibernate_info union pointed to by hib
 * with information about this machine (swap signature block
 * offsets, number of memory ranges, kernel in use, etc)
 */
int
get_hibernate_info(union hibernate_info *hib, int suspend)
{
	struct disklabel *dl;
	char err_string[128], *dl_ret;
	int part;
	SHA2_CTX ctx;
	void *fn;

#ifndef NO_PROPOLICE
	/* Save propolice guard */
	hib->guard = __guard_local;
#endif /* ! NO_PROPOLICE */

	/* Determine I/O function to use */
	hib->io_func = get_hibernate_io_function(swdevt[0]);
	if (hib->io_func == NULL)
		return (1);

	/* Calculate hibernate device */
	hib->dev = swdevt[0];

	/* Read disklabel (used to calculate signature and image offsets) */
	dl = malloc(sizeof(*dl), M_DEVBUF, M_WAITOK);
	dl_ret = disk_readlabel(dl, hib->dev, err_string, sizeof(err_string));

	if (dl_ret) {
		printf("Hibernate error reading disklabel: %s\n", dl_ret);
		return (1);
	}

	/* Make sure we have a swap partition. */
	part = DISKPART(hib->dev);
	if (dl->d_npartitions <= part ||
	    dl->d_secsize > sizeof(union hibernate_info) ||
	    dl->d_partitions[part].p_fstype != FS_SWAP ||
	    DL_GETPSIZE(&dl->d_partitions[part]) == 0)
		return (1);

	/* Magic number */
	hib->magic = HIBERNATE_MAGIC;

	/* Calculate signature block location */
	hib->sec_size = dl->d_secsize;
	hib->sig_offset = DL_GETPSIZE(&dl->d_partitions[part]) - 1;
	hib->sig_offset = DL_SECTOBLK(dl, hib->sig_offset);

	SHA256Init(&ctx);
	SHA256Update(&ctx, version, strlen(version));
	fn = printf;
	SHA256Update(&ctx, &fn, sizeof(fn));
	fn = malloc;
	SHA256Update(&ctx, &fn, sizeof(fn));
	fn = km_alloc;
	SHA256Update(&ctx, &fn, sizeof(fn));
	fn = strlen;
	SHA256Update(&ctx, &fn, sizeof(fn));
	SHA256Final((u_int8_t *)&hib->kern_hash, &ctx);

	if (suspend) {
		/* Grab the previously-allocated piglet addresses */
		hib->piglet_va = global_piglet_va;
		hib->piglet_pa = global_piglet_pa;
		hib->io_page = (void *)hib->piglet_va;

		/*
		 * Initialization of the hibernate IO function for drivers
		 * that need to do prep work (such as allocating memory or
		 * setting up data structures that cannot safely be done
		 * during suspend without causing side effects). There is
		 * a matching HIB_DONE call performed after the write is
		 * completed.
		 */
		if (hib->io_func(hib->dev,
		    DL_SECTOBLK(dl, DL_GETPOFFSET(&dl->d_partitions[part])),
		    (vaddr_t)NULL,
		    DL_SECTOBLK(dl, DL_GETPSIZE(&dl->d_partitions[part])),
		    HIB_INIT, hib->io_page))
			goto fail;

	} else {
		/*
		 * Resuming kernels use a regular private page for the driver
		 * No need to free this I/O page as it will vanish as part of
		 * the resume.
		 */
		hib->io_page = malloc(PAGE_SIZE, M_DEVBUF, M_NOWAIT);
		if (!hib->io_page)
			goto fail;
	}

	if (get_hibernate_info_md(hib))
		goto fail;

	free(dl, M_DEVBUF, sizeof(*dl));
	return (0);

fail:
	free(dl, M_DEVBUF, sizeof(*dl));
	return (1);
}

/*
 * Allocate nitems*size bytes from the hiballoc area presently in use
 */
void *
hibernate_zlib_alloc(void *unused, int nitems, int size)
{
	struct hibernate_zlib_state *hibernate_state;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	return hib_alloc(&hibernate_state->hiballoc_arena, nitems*size);
}

/*
 * Free the memory pointed to by addr in the hiballoc area presently in
 * use
 */
void
hibernate_zlib_free(void *unused, void *addr)
{
	struct hibernate_zlib_state *hibernate_state;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	hib_free(&hibernate_state->hiballoc_arena, addr);
}

/*
 * Inflate next page of data from the image stream.
 * The rle parameter is modified on exit to contain the number of pages to
 * skip in the output stream (or 0 if this page was inflated into).
 *
 * Returns 0 if the stream contains additional data, or 1 if the stream is
 * finished.
 */
int
hibernate_inflate_page(int *rle)
{
	struct hibernate_zlib_state *hibernate_state;
	int i;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	/* Set up the stream for RLE code inflate */
	hibernate_state->hib_stream.next_out = (unsigned char *)rle;
	hibernate_state->hib_stream.avail_out = sizeof(*rle);

	/* Inflate RLE code */
	i = inflate(&hibernate_state->hib_stream, Z_SYNC_FLUSH);
	if (i != Z_OK && i != Z_STREAM_END) {
		/*
		 * XXX - this will likely reboot/hang most machines
		 *       since the console output buffer will be unmapped,
		 *       but there's not much else we can do here.
		 */
		panic("rle inflate stream error");
	}

	if (hibernate_state->hib_stream.avail_out != 0) {
		/*
		 * XXX - this will likely reboot/hang most machines
		 *       since the console output buffer will be unmapped,
		 *       but there's not much else we can do here.
		 */
		panic("rle short inflate error");
	}

	if (*rle < 0 || *rle > 1024) {
		/*
		 * XXX - this will likely reboot/hang most machines
		 *       since the console output buffer will be unmapped,
		 *       but there's not much else we can do here.
		 */
		panic("invalid rle count");
	}

	if (i == Z_STREAM_END)
		return (1);

	if (*rle != 0)
		return (0);

	/* Set up the stream for page inflate */
	hibernate_state->hib_stream.next_out =
		(unsigned char *)HIBERNATE_INFLATE_PAGE;
	hibernate_state->hib_stream.avail_out = PAGE_SIZE;

	/* Process next block of data */
	i = inflate(&hibernate_state->hib_stream, Z_SYNC_FLUSH);
	if (i != Z_OK && i != Z_STREAM_END) {
		/*
		 * XXX - this will likely reboot/hang most machines
		 *       since the console output buffer will be unmapped,
		 *       but there's not much else we can do here.
		 */
		panic("inflate error");
	}

	/* We should always have extracted a full page ... */
	if (hibernate_state->hib_stream.avail_out != 0) {
		/*
		 * XXX - this will likely reboot/hang most machines
		 *       since the console output buffer will be unmapped,
		 *       but there's not much else we can do here.
		 */
		panic("incomplete page");
	}

	return (i == Z_STREAM_END);
}

/*
 * Inflate size bytes from src into dest, skipping any pages in
 * [src..dest] that are special (see hibernate_inflate_skip)
 *
 * This function executes while using the resume-time stack
 * and pmap, and therefore cannot use ddb/printf/etc. Doing so
 * will likely hang or reset the machine since the console output buffer
 * will be unmapped.
 */
void
hibernate_inflate_region(union hibernate_info *hib, paddr_t dest,
    paddr_t src, size_t size)
{
	int end_stream = 0, rle, skip;
	struct hibernate_zlib_state *hibernate_state;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	hibernate_state->hib_stream.next_in = (unsigned char *)src;
	hibernate_state->hib_stream.avail_in = size;

	do {
		/*
		 * Is this a special page? If yes, redirect the
		 * inflate output to a scratch page (eg, discard it)
		 */
		skip = hibernate_inflate_skip(hib, dest);
		if (skip == HIB_SKIP) {
			hibernate_enter_resume_mapping(
			    HIBERNATE_INFLATE_PAGE,
			    HIBERNATE_INFLATE_PAGE, 0);
		} else if (skip == HIB_MOVE) {
			/*
			 * Special case : retguard region. This gets moved
			 * temporarily into the piglet region and copied into
			 * place immediately before resume
			 */
			hibernate_enter_resume_mapping(
			    HIBERNATE_INFLATE_PAGE,
			    hib->piglet_pa + (110 * PAGE_SIZE) +
			    hib->retguard_ofs, 0);
			hib->retguard_ofs += PAGE_SIZE;
			if (hib->retguard_ofs > 255 * PAGE_SIZE) {
				/*
				 * XXX - this will likely reboot/hang most
				 *       machines since the console output
				 *       buffer will be unmapped, but there's
				 *       not much else we can do here.
				 */
				panic("retguard move error, out of space");
			}
		} else {
			hibernate_enter_resume_mapping(
			    HIBERNATE_INFLATE_PAGE, dest, 0);
		}

		hibernate_flush();
		end_stream = hibernate_inflate_page(&rle);

		if (rle == 0)
			dest += PAGE_SIZE;
		else
			dest += (rle * PAGE_SIZE);
	} while (!end_stream);
}

/*
 * deflate from src into the I/O page, up to 'remaining' bytes
 *
 * Returns number of input bytes consumed, and may reset
 * the 'remaining' parameter if not all the output space was consumed
 * (this information is needed to know how much to write to disk)
 */
size_t
hibernate_deflate(union hibernate_info *hib, paddr_t src,
    size_t *remaining)
{
	vaddr_t hibernate_io_page = hib->piglet_va + PAGE_SIZE;
	struct hibernate_zlib_state *hibernate_state;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	/* Set up the stream for deflate */
	hibernate_state->hib_stream.next_in = (unsigned char *)src;
	hibernate_state->hib_stream.avail_in = PAGE_SIZE - (src & PAGE_MASK);
	hibernate_state->hib_stream.next_out =
		(unsigned char *)hibernate_io_page + (PAGE_SIZE - *remaining);
	hibernate_state->hib_stream.avail_out = *remaining;

	/* Process next block of data */
	if (deflate(&hibernate_state->hib_stream, Z_SYNC_FLUSH) != Z_OK)
		panic("hibernate zlib deflate error");

	/* Update pointers and return number of bytes consumed */
	*remaining = hibernate_state->hib_stream.avail_out;
	return (PAGE_SIZE - (src & PAGE_MASK)) -
	    hibernate_state->hib_stream.avail_in;
}

/*
 * Write the hibernation information specified in hiber_info
 * to the location in swap previously calculated (last block of
 * swap), called the "signature block".
 */
int
hibernate_write_signature(union hibernate_info *hib)
{
	memset(&disk_hib, 0, hib->sec_size);
	memcpy(&disk_hib, hib, DEV_BSIZE);

	/* Write hibernate info to disk */
	return (hibernate_write(hib, hib->sig_offset,
	    (vaddr_t)&disk_hib, hib->sec_size, IO_TYPE_SIG));
}

/*
 * Write the memory chunk table to the area in swap immediately
 * preceding the signature block. The chunk table is stored
 * in the piglet when this function is called.  Returns errno.
 */
int
hibernate_write_chunktable(union hibernate_info *hib)
{
	vaddr_t hibernate_chunk_table_start;
	size_t hibernate_chunk_table_size;
	int i, err;

	hibernate_chunk_table_size = HIBERNATE_CHUNK_TABLE_SIZE;

	hibernate_chunk_table_start = hib->piglet_va +
	    HIBERNATE_CHUNK_SIZE;

	/* Write chunk table */
	for (i = 0; i < hibernate_chunk_table_size; i += MAXPHYS) {
		if ((err = hibernate_write(hib, btodb(i),
		    (vaddr_t)(hibernate_chunk_table_start + i),
		    MAXPHYS, IO_TYPE_CHK))) {
			DPRINTF("chunktable write error: %d\n", err);
			return (err);
		}
	}

	return (0);
}

/*
 * Write an empty hiber_info to the swap signature block, which is
 * guaranteed to not match any valid hib.
 */
int
hibernate_clear_signature(union hibernate_info *hib)
{
	uint8_t buf[DEV_BSIZE];

	/* Zero out a blank hiber_info */
	memcpy(&buf, &disk_hib, sizeof(buf));
	memset(&disk_hib, 0, hib->sec_size);

	/* Write (zeroed) hibernate info to disk */
	DPRINTF("clearing hibernate signature block location: %lld\n",
		hib->sig_offset);
	if (hibernate_block_io(hib,
	    hib->sig_offset,
	    hib->sec_size, (vaddr_t)&disk_hib, 1))
		printf("Warning: could not clear hibernate signature\n");

	memcpy(&disk_hib, buf, sizeof(buf));
	return (0);
}

/*
 * Compare two hibernate_infos to determine if they are the same (eg,
 * we should be performing a hibernate resume on this machine.
 * Not all fields are checked - just enough to verify that the machine
 * has the same memory configuration and kernel as the one that
 * wrote the signature previously.
 */
int
hibernate_compare_signature(union hibernate_info *mine,
    union hibernate_info *disk)
{
	u_int i;

	if (mine->nranges != disk->nranges) {
		printf("unhibernate failed: memory layout changed\n");
		return (1);
	}

	if (bcmp(mine->kern_hash, disk->kern_hash, SHA256_DIGEST_LENGTH) != 0) {
		printf("unhibernate failed: original kernel changed\n");
		return (1);
	}

	for (i = 0; i < mine->nranges; i++) {
		if ((mine->ranges[i].base != disk->ranges[i].base) ||
		    (mine->ranges[i].end != disk->ranges[i].end) ) {
			DPRINTF("hib range %d mismatch [%p-%p != %p-%p]\n",
				i,
				(void *)mine->ranges[i].base,
				(void *)mine->ranges[i].end,
				(void *)disk->ranges[i].base,
				(void *)disk->ranges[i].end);
			printf("unhibernate failed: memory size changed\n");
			return (1);
		}
	}

	return (0);
}

/*
 * Transfers xfer_size bytes between the hibernate device specified in
 * hib_info at offset blkctr and the vaddr specified at dest.
 *
 * Separate offsets and pages are used to handle misaligned reads (reads
 * that span a page boundary).
 *
 * blkctr specifies a relative offset (relative to the start of swap),
 * not an absolute disk offset
 *
 */
int
hibernate_block_io(union hibernate_info *hib, daddr_t blkctr,
    size_t xfer_size, vaddr_t dest, int iswrite)
{
	struct buf *bp;
	int error;

	bp = geteblk(xfer_size);
	if (iswrite)
		bcopy((caddr_t)dest, bp->b_data, xfer_size);

	bp->b_bcount = xfer_size;
	bp->b_blkno = blkctr;
	CLR(bp->b_flags, B_READ | B_WRITE | B_DONE);
	SET(bp->b_flags, B_BUSY | (iswrite ? B_WRITE : B_READ) | B_RAW);
	bp->b_dev = hib->dev;
	(*bdsw->d_strategy)(bp);

	error = biowait(bp);
	if (error) {
		printf("hib block_io biowait error %d blk %lld size %zu\n",
			error, (long long)blkctr, xfer_size);
	} else if (!iswrite)
		bcopy(bp->b_data, (caddr_t)dest, xfer_size);

	bp->b_flags |= B_INVAL;
	brelse(bp);

	return (error != 0);
}

/*
 * Preserve one page worth of random data, generated from the resuming
 * kernel's arc4random. After resume, this preserved entropy can be used
 * to further improve the un-hibernated machine's entropy pool. This
 * random data is stored in the piglet, which is preserved across the
 * unpack operation, and is restored later in the resume process (see
 * hib_getentropy)
 */
void
hibernate_preserve_entropy(union hibernate_info *hib)
{
	void *entropy;

	entropy = km_alloc(PAGE_SIZE, &kv_any, &kp_none, &kd_nowait);

	if (!entropy)
		return;

	pmap_activate(curproc);
	pmap_kenter_pa((vaddr_t)entropy,
	    (paddr_t)(hib->piglet_pa + (29 * PAGE_SIZE)),
	    PROT_READ | PROT_WRITE);

	arc4random_buf((void *)entropy, PAGE_SIZE);
	pmap_kremove((vaddr_t)entropy, PAGE_SIZE);
	km_free(entropy, PAGE_SIZE, &kv_any, &kp_none);
}

#ifndef NO_PROPOLICE
vaddr_t
hibernate_unprotect_ssp(void)
{
	struct kmem_dyn_mode kd_avoidalias;
	vaddr_t va = trunc_page((vaddr_t)&__guard_local);
	paddr_t pa;

	pmap_extract(pmap_kernel(), va, &pa);

	memset(&kd_avoidalias, 0, sizeof kd_avoidalias);
	kd_avoidalias.kd_prefer = pa;
	kd_avoidalias.kd_waitok = 1;
	va = (vaddr_t)km_alloc(PAGE_SIZE, &kv_any, &kp_none, &kd_avoidalias);
	if (!va)
		panic("hibernate_unprotect_ssp");

	pmap_kenter_pa(va, pa, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	return va;
}

void
hibernate_reprotect_ssp(vaddr_t va)
{
	pmap_kremove(va, PAGE_SIZE);
	km_free((void *)va, PAGE_SIZE, &kv_any, &kp_none);
}
#endif /* NO_PROPOLICE */

/*
 * Reads the signature block from swap, checks against the current machine's
 * information. If the information matches, perform a resume by reading the
 * saved image into the pig area, and unpacking.
 *
 * Must be called with interrupts enabled.
 */
void
hibernate_resume(void)
{
	uint8_t buf[DEV_BSIZE];
	union hibernate_info *hib = (union hibernate_info *)&buf;
	int s;
#ifndef NO_PROPOLICE
	vsize_t off = (vaddr_t)&__guard_local -
	    trunc_page((vaddr_t)&__guard_local);
	vaddr_t guard_va;
#endif

	/* Get current running machine's hibernate info */
	memset(buf, 0, sizeof(buf));
	if (get_hibernate_info(hib, 0)) {
		DPRINTF("couldn't retrieve machine's hibernate info\n");
		return;
	}

	/* Read hibernate info from disk */
	s = splbio();

	bdsw = &bdevsw[major(hib->dev)];
	if ((*bdsw->d_open)(hib->dev, FREAD, S_IFCHR, curproc)) {
		printf("hibernate_resume device open failed\n");
		splx(s);
		return;
	}

	DPRINTF("reading hibernate signature block location: %lld\n",
		hib->sig_offset);

	if (hibernate_block_io(hib,
	    hib->sig_offset,
	    hib->sec_size, (vaddr_t)&disk_hib, 0)) {
		DPRINTF("error in hibernate read\n");
		goto fail;
	}

	/* Check magic number */
	if (disk_hib.magic != HIBERNATE_MAGIC) {
		DPRINTF("wrong magic number in hibernate signature: %x\n",
			disk_hib.magic);
		goto fail;
	}

	/*
	 * We (possibly) found a hibernate signature. Clear signature first,
	 * to prevent accidental resume or endless resume cycles later.
	 */
	if (hibernate_clear_signature(hib)) {
		DPRINTF("error clearing hibernate signature block\n");
		goto fail;
	}

	/*
	 * If on-disk and in-memory hibernate signatures match,
	 * this means we should do a resume from hibernate.
	 */
	if (hibernate_compare_signature(hib, &disk_hib)) {
		DPRINTF("mismatched hibernate signature block\n");
		goto fail;
	}
	disk_hib.dev = hib->dev;

#ifdef MULTIPROCESSOR
	/* XXX - if we fail later, we may need to rehatch APs on some archs */
	DPRINTF("hibernate: quiescing APs\n");
	hibernate_quiesce_cpus();
#endif /* MULTIPROCESSOR */

	/* Read the image from disk into the image (pig) area */
	if (hibernate_read_image(&disk_hib))
		goto fail;
	if ((*bdsw->d_close)(hib->dev, 0, S_IFCHR, curproc))
		printf("hibernate_resume device close failed\n");
	bdsw = NULL;

	DPRINTF("hibernate: quiescing devices\n");
	if (config_suspend_all(DVACT_QUIESCE) != 0)
		goto fail;

#ifndef NO_PROPOLICE
	guard_va = hibernate_unprotect_ssp();
#endif /* NO_PROPOLICE */

	(void) splhigh();
	hibernate_disable_intr_machdep();
	cold = 2;

	DPRINTF("hibernate: suspending devices\n");
	if (config_suspend_all(DVACT_SUSPEND) != 0) {
		cold = 0;
		hibernate_enable_intr_machdep();
#ifndef NO_PROPOLICE
		hibernate_reprotect_ssp(guard_va);
#endif /* ! NO_PROPOLICE */
		goto fail;
	}

	pmap_extract(pmap_kernel(), (vaddr_t)&__retguard_start,
	    &retguard_start_phys);
	pmap_extract(pmap_kernel(), (vaddr_t)&__retguard_end,
	    &retguard_end_phys);

	hibernate_preserve_entropy(&disk_hib);

	printf("Unpacking image...\n");

	/* Switch stacks */
	DPRINTF("hibernate: switching stacks\n");
	hibernate_switch_stack_machdep();

#ifndef NO_PROPOLICE
	/* Start using suspended kernel's propolice guard */
	*(long *)(guard_va + off) = disk_hib.guard;
	hibernate_reprotect_ssp(guard_va);
#endif /* ! NO_PROPOLICE */

	/* Unpack and resume */
	hibernate_unpack_image(&disk_hib);

fail:
	if (!bdsw)
		printf("\nUnable to resume hibernated image\n");
	else if ((*bdsw->d_close)(hib->dev, 0, S_IFCHR, curproc))
		printf("hibernate_resume device close failed\n");
	splx(s);
}

/*
 * Unpack image from pig area to original location by looping through the
 * list of output chunks in the order they should be restored (fchunks).
 *
 * Note that due to the stack smash protector and the fact that we have
 * switched stacks, it is not permitted to return from this function.
 */
void
hibernate_unpack_image(union hibernate_info *hib)
{
	uint8_t buf[DEV_BSIZE];
	struct hibernate_disk_chunk *chunks;
	union hibernate_info *local_hib = (union hibernate_info *)&buf;
	paddr_t image_cur = global_pig_start;
	short i, *fchunks;
	char *pva;

	/* Piglet will be identity mapped (VA == PA) */
	pva = (char *)hib->piglet_pa;

	fchunks = (short *)(pva + (4 * PAGE_SIZE));

	chunks = (struct hibernate_disk_chunk *)(pva + HIBERNATE_CHUNK_SIZE);

	/* Can't use hiber_info that's passed in after this point */
	memcpy(buf, hib, sizeof(buf));
	local_hib->retguard_ofs = 0;

	/* VA == PA */
	local_hib->piglet_va = local_hib->piglet_pa;

	/*
	 * Point of no return. Once we pass this point, only kernel code can
	 * be accessed. No global variables or other kernel data structures
	 * are guaranteed to be coherent after unpack starts.
	 *
	 * The image is now in high memory (pig area), we unpack from the pig
	 * to the correct location in memory. We'll eventually end up copying
	 * on top of ourself, but we are assured the kernel code here is the
	 * same between the hibernated and resuming kernel, and we are running
	 * on our own stack, so the overwrite is ok.
	 */
	DPRINTF("hibernate: activating alt. pagetable and starting unpack\n");
	hibernate_activate_resume_pt_machdep();

	for (i = 0; i < local_hib->chunk_ctr; i++) {
		/* Reset zlib for inflate */
		if (hibernate_zlib_reset(local_hib, 0) != Z_OK)
			panic("hibernate failed to reset zlib for inflate");

		hibernate_process_chunk(local_hib, &chunks[fchunks[i]],
		    image_cur);

		image_cur += chunks[fchunks[i]].compressed_size;
	}

	/*
	 * Resume the loaded kernel by jumping to the MD resume vector.
	 * We won't be returning from this call. We pass the location of
	 * the retguard save area so the MD code can replace it before
	 * resuming. See the piglet layout at the top of this file for
	 * more information on the layout of the piglet area.
	 *
	 * We use 'global_piglet_va' here since by the time we are at
	 * this point, we have already unpacked the image, and we want
	 * the suspended kernel's view of what the piglet was, before
	 * suspend occurred (since we will need to use that in the retguard
	 * copy code in hibernate_resume_machdep.)
	 */
	hibernate_resume_machdep(global_piglet_va + (110 * PAGE_SIZE));
}

/*
 * Bounce a compressed image chunk to the piglet, entering mappings for the
 * copied pages as needed
 */
void
hibernate_copy_chunk_to_piglet(paddr_t img_cur, vaddr_t piglet, size_t size)
{
	size_t ct, ofs;
	paddr_t src = img_cur;
	vaddr_t dest = piglet;

	/* Copy first partial page */
	ct = (PAGE_SIZE) - (src & PAGE_MASK);
	ofs = (src & PAGE_MASK);

	if (ct < PAGE_SIZE) {
		hibernate_enter_resume_mapping(HIBERNATE_INFLATE_PAGE,
			(src - ofs), 0);
		hibernate_flush();
		bcopy((caddr_t)(HIBERNATE_INFLATE_PAGE + ofs), (caddr_t)dest, ct);
		src += ct;
		dest += ct;
	}

	/* Copy remaining pages */
	while (src < size + img_cur) {
		hibernate_enter_resume_mapping(HIBERNATE_INFLATE_PAGE, src, 0);
		hibernate_flush();
		ct = PAGE_SIZE;
		bcopy((caddr_t)(HIBERNATE_INFLATE_PAGE), (caddr_t)dest, ct);
		hibernate_flush();
		src += ct;
		dest += ct;
	}
}

/*
 * Process a chunk by bouncing it to the piglet, followed by unpacking
 */
void
hibernate_process_chunk(union hibernate_info *hib,
    struct hibernate_disk_chunk *chunk, paddr_t img_cur)
{
	char *pva = (char *)hib->piglet_va;

	hibernate_copy_chunk_to_piglet(img_cur,
	 (vaddr_t)(pva + (HIBERNATE_CHUNK_SIZE * 2)), chunk->compressed_size);
	hibernate_inflate_region(hib, chunk->base,
	    (vaddr_t)(pva + (HIBERNATE_CHUNK_SIZE * 2)),
	    chunk->compressed_size);
}

/*
 * Calculate RLE component for 'inaddr'. Clamps to max RLE pages between
 * inaddr and range_end.
 */
int
hibernate_calc_rle(paddr_t inaddr, paddr_t range_end)
{
	int rle;

	rle = uvm_page_rle(inaddr);
	KASSERT(rle >= 0 && rle <= MAX_RLE);

	/* Clamp RLE to range end */
	if (rle > 0 && inaddr + (rle * PAGE_SIZE) > range_end)
		rle = (range_end - inaddr) / PAGE_SIZE;

	return (rle);
}

/*
 * Write the RLE byte for page at 'inaddr' to the output stream.
 * Returns the number of pages to be skipped at 'inaddr'.
 */
int
hibernate_write_rle(union hibernate_info *hib, paddr_t inaddr,
	paddr_t range_end, daddr_t *blkctr,
	size_t *out_remaining)
{
	int rle, err, *rleloc;
	struct hibernate_zlib_state *hibernate_state;
	vaddr_t hibernate_io_page = hib->piglet_va + PAGE_SIZE;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	rle = hibernate_calc_rle(inaddr, range_end);

	rleloc = (int *)hibernate_rle_page + MAX_RLE - 1;
	*rleloc = rle;

	/* Deflate the RLE byte into the stream */
	hibernate_deflate(hib, (paddr_t)rleloc, out_remaining);

	/* Did we fill the output page? If so, flush to disk */
	if (*out_remaining == 0) {
		if ((err = hibernate_write(hib, *blkctr,
			(vaddr_t)hibernate_io_page, PAGE_SIZE, IO_TYPE_IMG))) {
				DPRINTF("hib write error %d\n", err);
				return -1;
		}

		*blkctr += btodb(PAGE_SIZE);
		*out_remaining = PAGE_SIZE;

		/* If we didn't deflate the entire RLE byte, finish it now */
		if (hibernate_state->hib_stream.avail_in != 0)
			hibernate_deflate(hib,
				(vaddr_t)hibernate_state->hib_stream.next_in,
				out_remaining);
	}

	return (rle);
}

/*
 * Write a compressed version of this machine's memory to disk, at the
 * precalculated swap offset:
 *
 * end of swap - signature block size - chunk table size - memory size
 *
 * The function begins by looping through each phys mem range, cutting each
 * one into MD sized chunks. These chunks are then compressed individually
 * and written out to disk, in phys mem order. Some chunks might compress
 * more than others, and for this reason, each chunk's size is recorded
 * in the chunk table, which is written to disk after the image has
 * properly been compressed and written (in hibernate_write_chunktable).
 *
 * When this function is called, the machine is nearly suspended - most
 * devices are quiesced/suspended, interrupts are off, and cold has
 * been set. This means that there can be no side effects once the
 * write has started, and the write function itself can also have no
 * side effects. This also means no printfs are permitted (since printf
 * has side effects.)
 *
 * Return values :
 *
 * 0      - success
 * EIO    - I/O error occurred writing the chunks
 * EINVAL - Failed to write a complete range
 * ENOMEM - Memory allocation failure during preparation of the zlib arena
 */
int
hibernate_write_chunks(union hibernate_info *hib)
{
	paddr_t range_base, range_end, inaddr, temp_inaddr;
	size_t out_remaining, used;
	struct hibernate_disk_chunk *chunks;
	vaddr_t hibernate_io_page = hib->piglet_va + PAGE_SIZE;
	daddr_t blkctr = 0;
	int i, rle, err;
	struct hibernate_zlib_state *hibernate_state;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	hib->chunk_ctr = 0;

	/*
	 * Map the utility VAs to the piglet. See the piglet map at the
	 * top of this file for piglet layout information.
	 */
	hibernate_copy_page = hib->piglet_va + 3 * PAGE_SIZE;
	hibernate_rle_page = hib->piglet_va + 28 * PAGE_SIZE;

	chunks = (struct hibernate_disk_chunk *)(hib->piglet_va +
	    HIBERNATE_CHUNK_SIZE);

	/* Calculate the chunk regions */
	for (i = 0; i < hib->nranges; i++) {
		range_base = hib->ranges[i].base;
		range_end = hib->ranges[i].end;

		inaddr = range_base;

		while (inaddr < range_end) {
			chunks[hib->chunk_ctr].base = inaddr;
			if (inaddr + HIBERNATE_CHUNK_SIZE < range_end)
				chunks[hib->chunk_ctr].end = inaddr +
				    HIBERNATE_CHUNK_SIZE;
			else
				chunks[hib->chunk_ctr].end = range_end;

			inaddr += HIBERNATE_CHUNK_SIZE;
			hib->chunk_ctr ++;
		}
	}

	uvm_pmr_dirty_everything();

	/* Compress and write the chunks in the chunktable */
	for (i = 0; i < hib->chunk_ctr; i++) {
		range_base = chunks[i].base;
		range_end = chunks[i].end;

		chunks[i].offset = blkctr;

		/* Reset zlib for deflate */
		if (hibernate_zlib_reset(hib, 1) != Z_OK) {
			DPRINTF("hibernate_zlib_reset failed for deflate\n");
			return (ENOMEM);
		}

		inaddr = range_base;

		/*
		 * For each range, loop through its phys mem region
		 * and write out the chunks (the last chunk might be
		 * smaller than the chunk size).
		 */
		while (inaddr < range_end) {
			out_remaining = PAGE_SIZE;
			while (out_remaining > 0 && inaddr < range_end) {
				/*
				 * Adjust for regions that are not evenly
				 * divisible by PAGE_SIZE or overflowed
				 * pages from the previous iteration.
				 */
				temp_inaddr = (inaddr & PAGE_MASK) +
				    hibernate_copy_page;

				/* Deflate from temp_inaddr to IO page */
				if (inaddr != range_end) {
					rle = 0;
					if (inaddr % PAGE_SIZE == 0) {
						rle = hibernate_write_rle(hib,
							inaddr,
							range_end,
							&blkctr,
							&out_remaining);
					}

					switch (rle) {
					case -1:
						return EIO;
					case 0:
						pmap_kenter_pa(hibernate_temp_page,
							inaddr & PMAP_PA_MASK,
							PROT_READ);

						bcopy((caddr_t)hibernate_temp_page,
							(caddr_t)hibernate_copy_page,
							PAGE_SIZE);
						inaddr += hibernate_deflate(hib,
							temp_inaddr,
							&out_remaining);
						break;
					default:
						inaddr += rle * PAGE_SIZE;
						if (inaddr > range_end)
							inaddr = range_end;
						break;
					}

				}

				if (out_remaining == 0) {
					/* Filled up the page */
					if ((err = hibernate_write(hib, blkctr,
					    (vaddr_t)hibernate_io_page,
					    PAGE_SIZE, IO_TYPE_IMG))) {
						DPRINTF("hib write error %d\n",
						    err);
						return (err);
					}
					blkctr += btodb(PAGE_SIZE);
				}
			}
		}

		if (inaddr != range_end) {
			DPRINTF("deflate range ended prematurely\n");
			return (EINVAL);
		}

		/*
		 * End of range. Round up to next secsize bytes
		 * after finishing compress
		 */
		if (out_remaining == 0)
			out_remaining = PAGE_SIZE;

		/* Finish compress */
		hibernate_state->hib_stream.next_in = (unsigned char *)inaddr;
		hibernate_state->hib_stream.avail_in = 0;
		hibernate_state->hib_stream.next_out =
		    (unsigned char *)hibernate_io_page +
			(PAGE_SIZE - out_remaining);

		/* We have an extra output page available for finalize */
		hibernate_state->hib_stream.avail_out =
			out_remaining + PAGE_SIZE;

		if ((err = deflate(&hibernate_state->hib_stream, Z_FINISH)) !=
		    Z_STREAM_END) {
			DPRINTF("deflate error in output stream: %d\n", err);
			return (err);
		}

		out_remaining = hibernate_state->hib_stream.avail_out;

		/* Round up to next sector if needed */
		used = roundup(2 * PAGE_SIZE - out_remaining, hib->sec_size);

		/* Write final block(s) for this chunk */
		if ((err = hibernate_write(hib, blkctr,
		    (vaddr_t)hibernate_io_page, used, IO_TYPE_IMG))) {
			DPRINTF("hib final write error %d\n", err);
			return (err);
		}

		blkctr += btodb(used);

		chunks[i].compressed_size = dbtob(blkctr - chunks[i].offset);
	}

	return (0);
}

/*
 * Reset the zlib stream state and allocate a new hiballoc area for either
 * inflate or deflate. This function is called once for each hibernate chunk.
 * Calling hiballoc_init multiple times is acceptable since the memory it is
 * provided is unmanaged memory (stolen). We use the memory provided to us
 * by the piglet allocated via the supplied hib.
 */
int
hibernate_zlib_reset(union hibernate_info *hib, int deflate)
{
	vaddr_t hibernate_zlib_start;
	size_t hibernate_zlib_size;
	char *pva = (char *)hib->piglet_va;
	struct hibernate_zlib_state *hibernate_state;

	hibernate_state =
	    (struct hibernate_zlib_state *)HIBERNATE_HIBALLOC_PAGE;

	if (!deflate)
		pva = (char *)((paddr_t)pva & (PIGLET_PAGE_MASK));

	/*
	 * See piglet layout information at the start of this file for
	 * information on the zlib page assignments.
	 */
	hibernate_zlib_start = (vaddr_t)(pva + (30 * PAGE_SIZE));
	hibernate_zlib_size = 80 * PAGE_SIZE;

	memset((void *)hibernate_zlib_start, 0, hibernate_zlib_size);
	memset(hibernate_state, 0, PAGE_SIZE);

	/* Set up stream structure */
	hibernate_state->hib_stream.zalloc = (alloc_func)hibernate_zlib_alloc;
	hibernate_state->hib_stream.zfree = (free_func)hibernate_zlib_free;

	/* Initialize the hiballoc arena for zlib allocs/frees */
	if (hiballoc_init(&hibernate_state->hiballoc_arena,
	    (caddr_t)hibernate_zlib_start, hibernate_zlib_size))
		return 1;

	if (deflate) {
		return deflateInit(&hibernate_state->hib_stream,
		    Z_BEST_SPEED);
	} else
		return inflateInit(&hibernate_state->hib_stream);
}

/*
 * Reads the hibernated memory image from disk, whose location and
 * size are recorded in hib. Begin by reading the persisted
 * chunk table, which records the original chunk placement location
 * and compressed size for each. Next, allocate a pig region of
 * sufficient size to hold the compressed image. Next, read the
 * chunks into the pig area (calling hibernate_read_chunks to do this),
 * and finally, if all of the above succeeds, clear the hibernate signature.
 * The function will then return to hibernate_resume, which will proceed
 * to unpack the pig image to the correct place in memory.
 */
int
hibernate_read_image(union hibernate_info *hib)
{
	size_t compressed_size, disk_size, chunktable_size, pig_sz;
	paddr_t image_start, image_end, pig_start, pig_end;
	struct hibernate_disk_chunk *chunks;
	daddr_t blkctr;
	vaddr_t chunktable = (vaddr_t)NULL;
	paddr_t piglet_chunktable = hib->piglet_pa +
	    HIBERNATE_CHUNK_SIZE;
	int i, status;

	status = 0;
	pmap_activate(curproc);

	/* Calculate total chunk table size in disk blocks */
	chunktable_size = btodb(HIBERNATE_CHUNK_TABLE_SIZE);

	blkctr = hib->chunktable_offset;

	chunktable = (vaddr_t)km_alloc(HIBERNATE_CHUNK_TABLE_SIZE, &kv_any,
	    &kp_none, &kd_nowait);

	if (!chunktable)
		return (1);

	/* Map chunktable pages */
	for (i = 0; i < HIBERNATE_CHUNK_TABLE_SIZE; i += PAGE_SIZE)
		pmap_kenter_pa(chunktable + i, piglet_chunktable + i,
		    PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	/* Read the chunktable from disk into the piglet chunktable */
	for (i = 0; i < HIBERNATE_CHUNK_TABLE_SIZE;
	    i += MAXPHYS, blkctr += btodb(MAXPHYS)) {
		if (hibernate_block_io(hib, blkctr, MAXPHYS,
		    chunktable + i, 0)) {
			status = 1;
			goto unmap;
		}
	}

	blkctr = hib->image_offset;
	compressed_size = 0;

	chunks = (struct hibernate_disk_chunk *)chunktable;

	for (i = 0; i < hib->chunk_ctr; i++)
		compressed_size += chunks[i].compressed_size;

	disk_size = compressed_size;

	printf("unhibernating @ block %lld length %luMB\n",
	    hib->image_offset, compressed_size / (1024 * 1024));

	/* Allocate the pig area */
	pig_sz = compressed_size + HIBERNATE_CHUNK_SIZE;
	if (uvm_pmr_alloc_pig(&pig_start, pig_sz, hib->piglet_pa) == ENOMEM) {
		status = 1;
		goto unmap;
	}

	pig_end = pig_start + pig_sz;

	/* Calculate image extents. Pig image must end on a chunk boundary. */
	image_end = pig_end & ~(HIBERNATE_CHUNK_SIZE - 1);
	image_start = image_end - disk_size;

	if (hibernate_read_chunks(hib, image_start, image_end, disk_size,
	    chunks)) {
		status = 1;
		goto unmap;
	}

	/* Prepare the resume time pmap/page table */
	hibernate_populate_resume_pt(hib, image_start, image_end);

unmap:
	/* Unmap chunktable pages */
	pmap_kremove(chunktable, HIBERNATE_CHUNK_TABLE_SIZE);
	pmap_update(pmap_kernel());

	return (status);
}

/*
 * Read the hibernated memory chunks from disk (chunk information at this
 * point is stored in the piglet) into the pig area specified by
 * [pig_start .. pig_end]. Order the chunks so that the final chunk is the
 * only chunk with overlap possibilities.
 */
int
hibernate_read_chunks(union hibernate_info *hib, paddr_t pig_start,
    paddr_t pig_end, size_t image_compr_size,
    struct hibernate_disk_chunk *chunks)
{
	paddr_t img_cur, piglet_base;
	daddr_t blkctr;
	size_t processed, compressed_size, read_size;
	int err, nchunks, nfchunks, num_io_pages;
	vaddr_t tempva, hibernate_fchunk_area;
	short *fchunks, i, j;

	tempva = (vaddr_t)NULL;
	hibernate_fchunk_area = (vaddr_t)NULL;
	nfchunks = 0;
	piglet_base = hib->piglet_pa;
	global_pig_start = pig_start;

	/*
	 * These mappings go into the resuming kernel's page table, and are
	 * used only during image read. They disappear from existence
	 * when the suspended kernel is unpacked on top of us.
	 */
	tempva = (vaddr_t)km_alloc(MAXPHYS + PAGE_SIZE, &kv_any, &kp_none,
		&kd_nowait);
	if (!tempva)
		return (1);
	hibernate_fchunk_area = (vaddr_t)km_alloc(24 * PAGE_SIZE, &kv_any,
	    &kp_none, &kd_nowait);
	if (!hibernate_fchunk_area)
		return (1);

	/* Final output chunk ordering VA */
	fchunks = (short *)hibernate_fchunk_area;

	/* Map the chunk ordering region */
	for(i = 0; i < 24 ; i++)
		pmap_kenter_pa(hibernate_fchunk_area + (i * PAGE_SIZE),
			piglet_base + ((4 + i) * PAGE_SIZE),
			PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());

	nchunks = hib->chunk_ctr;

	/* Initially start all chunks as unplaced */
	for (i = 0; i < nchunks; i++)
		chunks[i].flags = 0;

	/*
	 * Search the list for chunks that are outside the pig area. These
	 * can be placed first in the final output list.
	 */
	for (i = 0; i < nchunks; i++) {
		if (chunks[i].end <= pig_start || chunks[i].base >= pig_end) {
			fchunks[nfchunks] = i;
			nfchunks++;
			chunks[i].flags |= HIBERNATE_CHUNK_PLACED;
		}
	}

	/*
	 * Walk the ordering, place the chunks in ascending memory order.
	 */
	for (i = 0; i < nchunks; i++) {
		if (chunks[i].flags != HIBERNATE_CHUNK_PLACED) {
			fchunks[nfchunks] = i;
			nfchunks++;
			chunks[i].flags = HIBERNATE_CHUNK_PLACED;
		}
	}

	img_cur = pig_start;

	for (i = 0, err = 0; i < nfchunks && err == 0; i++) {
		blkctr = chunks[fchunks[i]].offset + hib->image_offset;
		processed = 0;
		compressed_size = chunks[fchunks[i]].compressed_size;

		while (processed < compressed_size && err == 0) {
			if (compressed_size - processed >= MAXPHYS)
				read_size = MAXPHYS;
			else
				read_size = compressed_size - processed;

			/*
			 * We're reading read_size bytes, offset from the
			 * start of a page by img_cur % PAGE_SIZE, so the
			 * end will be read_size + (img_cur % PAGE_SIZE)
			 * from the start of the first page.  Round that
			 * up to the next page size.
			 */
			num_io_pages = (read_size + (img_cur % PAGE_SIZE)
				+ PAGE_SIZE - 1) / PAGE_SIZE;

			KASSERT(num_io_pages <= MAXPHYS/PAGE_SIZE + 1);

			/* Map pages for this read */
			for (j = 0; j < num_io_pages; j ++)
				pmap_kenter_pa(tempva + j * PAGE_SIZE,
				    img_cur + j * PAGE_SIZE,
				    PROT_READ | PROT_WRITE);

			pmap_update(pmap_kernel());

			err = hibernate_block_io(hib, blkctr, read_size,
			    tempva + (img_cur & PAGE_MASK), 0);

			blkctr += btodb(read_size);

			pmap_kremove(tempva, num_io_pages * PAGE_SIZE);
			pmap_update(pmap_kernel());

			processed += read_size;
			img_cur += read_size;
		}
	}

	pmap_kremove(hibernate_fchunk_area, 24 * PAGE_SIZE);
	pmap_update(pmap_kernel());

	return (i != nfchunks);
}

/*
 * Hibernating a machine comprises the following operations:
 *  1. Calculating this machine's hibernate_info information
 *  2. Allocating a piglet and saving the piglet's physaddr
 *  3. Calculating the memory chunks
 *  4. Writing the compressed chunks to disk
 *  5. Writing the chunk table
 *  6. Writing the signature block (hibernate_info)
 *
 * On most architectures, the function calling hibernate_suspend would
 * then power off the machine using some MD-specific implementation.
 */
int
hibernate_suspend(void)
{
	uint8_t buf[DEV_BSIZE];
	union hibernate_info *hib = (union hibernate_info *)&buf;
	u_long start, end;

	/*
	 * Calculate memory ranges, swap offsets, etc.
	 * This also allocates a piglet whose physaddr is stored in
	 * hib->piglet_pa and vaddr stored in hib->piglet_va
	 */
	if (get_hibernate_info(hib, 1)) {
		DPRINTF("failed to obtain hibernate info\n");
		return (1);
	}

	/* Find a page-addressed region in swap [start,end] */
	if (uvm_hibswap(hib->dev, &start, &end)) {
		printf("hibernate: cannot find any swap\n");
		return (1);
	}

	if (end - start + 1 < 1000) {
		printf("hibernate: insufficient swap (%lu is too small)\n",
			end - start + 1);
		return (1);
	}

	pmap_extract(pmap_kernel(), (vaddr_t)&__retguard_start,
	    &retguard_start_phys);
	pmap_extract(pmap_kernel(), (vaddr_t)&__retguard_end,
	    &retguard_end_phys);

	/* Calculate block offsets in swap */
	hib->image_offset = ctod(start);
	hib->image_size = ctod(end - start + 1) -
	    btodb(HIBERNATE_CHUNK_TABLE_SIZE);
	hib->chunktable_offset = hib->image_offset + hib->image_size;

	DPRINTF("hibernate @ block %lld chunks-length %lu blocks, "
	    "chunktable-length %d blocks\n", hib->image_offset, hib->image_size,
	    btodb(HIBERNATE_CHUNK_TABLE_SIZE));

	pmap_activate(curproc);
	DPRINTF("hibernate: writing chunks\n");
	if (hibernate_write_chunks(hib)) {
		DPRINTF("hibernate_write_chunks failed\n");
		return (1);
	}

	DPRINTF("hibernate: writing chunktable\n");
	if (hibernate_write_chunktable(hib)) {
		DPRINTF("hibernate_write_chunktable failed\n");
		return (1);
	}

	DPRINTF("hibernate: writing signature\n");
	if (hibernate_write_signature(hib)) {
		DPRINTF("hibernate_write_signature failed\n");
		return (1);
	}

	/* Allow the disk to settle */
	delay(500000);

	/*
	 * Give the device-specific I/O function a notification that we're
	 * done, and that it can clean up or shutdown as needed.
	 */
	if (hib->io_func(hib->dev, 0, (vaddr_t)NULL, 0, HIB_DONE, hib->io_page))
		printf("Warning: hibernate done failed\n");
	return (0);
}

int
hibernate_alloc(void)
{
	KASSERT(hibernate_temp_page == 0);

	/*
	 * If we weren't able to early allocate a piglet, don't proceed
	 */
	if (global_piglet_va == 0)
		return (ENOMEM);

	pmap_activate(curproc);
	pmap_kenter_pa(HIBERNATE_HIBALLOC_PAGE, HIBERNATE_HIBALLOC_PAGE,
	    PROT_READ | PROT_WRITE);

	/*
	 * Allocate VA for the temp page.
	 *
	 * This will become part of the suspended kernel and will
	 * be freed in hibernate_free, upon resume (or hibernate
	 * failure)
	 */
	hibernate_temp_page = (vaddr_t)km_alloc(PAGE_SIZE, &kv_any,
	    &kp_none, &kd_nowait);
	if (!hibernate_temp_page)
		goto unmap;

	return (0);
unmap:
	pmap_kremove(HIBERNATE_HIBALLOC_PAGE, PAGE_SIZE);
	pmap_update(pmap_kernel());
	return (ENOMEM);
}

/*
 * Free items allocated by hibernate_alloc()
 */
void
hibernate_free(void)
{
	pmap_activate(curproc);

	if (hibernate_temp_page) {
		pmap_kremove(hibernate_temp_page, PAGE_SIZE);
		km_free((void *)hibernate_temp_page, PAGE_SIZE,
		    &kv_any, &kp_none);
	}

	hibernate_temp_page = 0;
	pmap_kremove(HIBERNATE_HIBALLOC_PAGE, PAGE_SIZE);
	pmap_update(pmap_kernel());
}

void
preallocate_hibernate_memory(void)
{
	/* Preallocate a piglet */
	if (ptoa((psize_t)physmem) > HIBERNATE_MIN_MEMORY) {
		if (uvm_pmr_alloc_piglet(&global_piglet_va, &global_piglet_pa,
		    HIBERNATE_CHUNK_SIZE * 4, HIBERNATE_CHUNK_SIZE)) {
			DPRINTF("%s: failed to preallocate hibernate mem\n",
			    __func__);
			global_piglet_va = 0;
			global_piglet_pa = 0;
		}
	}
}
