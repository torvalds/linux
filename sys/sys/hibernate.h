/*	$OpenBSD: hibernate.h,v 1.51 2025/07/05 09:24:37 jsg Exp $	*/

/*
 * Copyright (c) 2011 Ariane van der Steldt <ariane@stack.nl>
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

#ifndef _SYS_HIBERNATE_H_
#define _SYS_HIBERNATE_H_

#include <sys/types.h>
#include <sys/tree.h>
#include <lib/libz/zlib.h>
#include <crypto/sha2.h>

#define HIB_PHYSSEG_MAX		22

#define HIBERNATE_CHUNK_USED 1
#define HIBERNATE_CHUNK_CONFLICT 2
#define HIBERNATE_CHUNK_PLACED 4

/* Magic number used to indicate hibernate signature block */
#define HIBERNATE_MAGIC 0x0B5D0B5D

/* Page skip operations used during unpack */
#define HIB_MOVE 2
#define HIB_SKIP 1

struct hiballoc_entry;

/*
 * Allocator operates from an arena, that is pre-allocated by the caller.
 */
struct hiballoc_arena {
	RBT_HEAD(hiballoc_addr, hiballoc_entry)	hib_addrs;
};

/*
 * Describes a zlib compression stream and its associated hiballoc area
 */
struct hibernate_zlib_state {
        z_stream hib_stream;
        struct hiballoc_arena hiballoc_arena;
};

/*
 * Describes a range of physical memory on the machine
 */
struct hibernate_memory_range {
	paddr_t		base;
	paddr_t		end;
};

/*
 * Describes a hibernate chunk structure, used when splitting the memory
 * image of the machine into easy-to-manage pieces.
 */
struct hibernate_disk_chunk {
	paddr_t		base;		/* Base of chunk */
	paddr_t		end;		/* End of chunk */
	daddr_t		offset;		/* Abs. disk block locating chunk */
	size_t		compressed_size; /* Compressed size on disk */
	short		flags;		/* Flags */
};

#define HIB_INIT	-1
#define HIB_DONE	-2
#define HIB_R		0
#define HIB_W		1
typedef	int (*hibio_fn)(dev_t, daddr_t, vaddr_t, size_t, int, void *);

/*
 * Used to store information about the hibernation state of the machine,
 * such as memory range count and extents, disk sector size, and various
 * offsets where things are located on disk.
 */
union hibernate_info {
	struct {
		u_int32_t			magic;
		dev_t				dev;
		size_t				nranges;
		struct hibernate_memory_range	ranges[HIB_PHYSSEG_MAX];
		size_t				image_size;
		size_t				chunk_ctr;
		daddr_t				sig_offset;
		daddr_t				chunktable_offset;
		daddr_t				image_offset;
		paddr_t				piglet_pa;
		vaddr_t				piglet_va;
		hibio_fn			io_func;
		void				*io_page;
		u_int8_t			kern_hash[SHA256_DIGEST_LENGTH];
#ifndef NO_PROPOLICE
		long				guard;
#endif /* ! NO_PROPOLICE */
		u_int32_t			retguard_ofs;
		u_int32_t			sec_size;
	};

	/* XXX - remove restriction to have the struct fit in a single block */
	char pad[4096]; /* Pad to largest allowable disk sector size in bytes */
};

void	*hib_alloc(struct hiballoc_arena*, size_t);
void	 hib_free(struct hiballoc_arena*, void*);
int	 hiballoc_init(struct hiballoc_arena*, void*, size_t len);
void	 uvm_pmr_dirty_everything(void);
int	 uvm_pmr_alloc_pig(paddr_t*, psize_t, paddr_t);
int	 uvm_pmr_alloc_piglet(vaddr_t*, paddr_t*, vsize_t, paddr_t);
int	 uvm_page_rle(paddr_t);
void	 uvmpd_hibernate(void);

hibio_fn get_hibernate_io_function(dev_t);
int	get_hibernate_info(union hibernate_info *, int);

int	hibernate_zlib_reset(union hibernate_info *, int);
void	*hibernate_zlib_alloc(void *, int, int);
void	hibernate_zlib_free(void *, void *);
void	hibernate_inflate_region(union hibernate_info *, paddr_t, paddr_t,
	    size_t);
size_t	hibernate_deflate(union hibernate_info *, paddr_t, size_t *);
void	hibernate_process_chunk(union hibernate_info *,
	    struct hibernate_disk_chunk *, paddr_t);
int	hibernate_inflate_page(int *);

int	hibernate_block_io(union hibernate_info *, daddr_t, size_t, vaddr_t, int);
int	hibernate_write_signature(union hibernate_info *);
int	hibernate_write_chunktable(union hibernate_info *);
int	hibernate_write_chunks(union hibernate_info *);
int	hibernate_clear_signature(union hibernate_info *);
int	hibernate_compare_signature(union hibernate_info *,
	    union hibernate_info *);
void	hibernate_resume(void);
int	hibernate_suspend(void);
int	hibernate_read_image(union hibernate_info *);
int	hibernate_read_chunks(union hibernate_info *, paddr_t, paddr_t, size_t,
	    struct hibernate_disk_chunk *);
void	hibernate_unpack_image(union hibernate_info *);
void	hibernate_populate_resume_pt(union hibernate_info *, paddr_t, paddr_t);
int	hibernate_alloc(void);
void	hibernate_free(void);
void	hib_getentropy(char **, size_t *);

int	hibernate_write(union hibernate_info *, daddr_t, vaddr_t, size_t, int);
void	hibernate_sort_ranges(union hibernate_info *);
void	hibernate_suspend_bufcache(void);
void	hibernate_resume_bufcache(void);

void	preallocate_hibernate_memory(void);

#endif /* _SYS_HIBERNATE_H_ */
