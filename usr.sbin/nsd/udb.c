/* udb.c - u(micro) data base.
 * By W.C.A. Wijngaards
 * Copyright 2010, NLnet Labs.
 * BSD, see LICENSE.
 */
#include "config.h"
#include "udb.h"
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include "lookup3.h"
#include "util.h"

/* mmap and friends */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

/* for systems without, portable definition, failed-1 and async is a flag */
#ifndef MAP_FAILED
#define MAP_FAILED ((void*)-1)
#endif
#ifndef MS_SYNC
#define MS_SYNC 0
#endif

/** move and fixup xl segment */
static void move_xl_segment(void* base, udb_base* udb, udb_void xl,
	udb_void n, uint64_t sz, uint64_t startseg);
/** attempt to compact the data and move free space to the end */
static int udb_alloc_compact(void* base, udb_alloc* alloc);

/** convert pointer to the data part to a pointer to the base of the chunk */
static udb_void
chunk_from_dataptr(udb_void data)
{
	/* we use that sizeof(udb_chunk_d) != sizeof(udb_xl_chunk_d) and
	 * that xl_chunk_d is aligned on x**1024 boundaries. */
	udb_void xl = data - sizeof(udb_xl_chunk_d);
	if( (xl & (UDB_ALLOC_CHUNK_SIZE-1)) == 0)
		return xl;
	return data - sizeof(udb_chunk_d);
}

udb_void chunk_from_dataptr_ext(udb_void data) {
	return chunk_from_dataptr(data);
}

#ifndef NDEBUG
/** read last octet from a chunk */
static uint8_t
chunk_get_last(void* base, udb_void chunk, int exp)
{
	return *((uint8_t*)UDB_REL(base, chunk+(1<<exp)-1));
}
#endif

/** write last octet of a chunk */
static void
chunk_set_last(void* base, udb_void chunk, int exp, uint8_t value)
{
	assert(exp >= 0 && exp <= 63);
	*((uint8_t*)UDB_REL(base, chunk+((uint64_t)1<<exp)-1)) = value;
}

/** create udb_base from a file descriptor (must be at start of file) */
udb_base*
udb_base_create_fd(const char* fname, int fd, udb_walk_relptr_func walkfunc,
	void* arg)
{
	uint64_t m, fsz;
	udb_glob_d g;
	ssize_t r;
	udb_base* udb = (udb_base*)xalloc_zero(sizeof(*udb));
	if(!udb) {
		log_msg(LOG_ERR, "out of memory");
		close(fd);
		return NULL;
	}
	udb->fname = strdup(fname);
	if(!udb->fname) {
		log_msg(LOG_ERR, "out of memory");
		free(udb);
		close(fd);
		return NULL;
	}
	udb->walkfunc = walkfunc;
	udb->walkarg = arg;
	udb->fd = fd;
	udb->ram_size = 1024;
	udb->ram_mask = (int)udb->ram_size - 1;
	udb->ram_hash = (udb_ptr**)xalloc_array_zero(sizeof(udb_ptr*),
		udb->ram_size);
	if(!udb->ram_hash) {
		free(udb->fname);
		free(udb);
		log_msg(LOG_ERR, "out of memory");
		close(fd);
		return NULL;
	}

	/* read magic */
	if((r=read(fd, &m, sizeof(m))) == -1) {
		log_msg(LOG_ERR, "%s: %s", fname, strerror(errno));
		goto fail;
	} else if(r != (ssize_t)sizeof(m)) {
		log_msg(LOG_ERR, "%s: file too short", fname);
		goto fail;
	}
	/* TODO : what if bigendian and littleendian file, see magic */
	if(m != UDB_MAGIC) {
		log_msg(LOG_ERR, "%s: wrong type of file", fname);
		goto fail;
	}
	/* read header */
	if((r=read(fd, &g, sizeof(g))) == -1) {
		log_msg(LOG_ERR, "%s: %s\n", fname, strerror(errno));
		goto fail;
	} else if(r != (ssize_t)sizeof(g)) {
		log_msg(LOG_ERR, "%s: file too short", fname);
		goto fail;
	}
	if(g.version != 0) {
		log_msg(LOG_ERR, "%s: unknown file version %d", fname,
			(int)g.version);
		goto fail;
	}
	if(g.hsize < UDB_HEADER_SIZE) {
		log_msg(LOG_ERR, "%s: header size too small %d", fname,
			(int)g.hsize);
		goto fail;
	}
	if(g.hsize > UDB_HEADER_SIZE) {
		log_msg(LOG_WARNING, "%s: header size too large %d", fname,
			(int)g.hsize);
		goto fail;
	}
	if(g.clean_close != 1) {
		log_msg(LOG_WARNING, "%s: not cleanly closed %d", fname,
			(int)g.clean_close);
		goto fail;
	}
	if(g.dirty_alloc != 0) {
		log_msg(LOG_WARNING, "%s: not cleanly closed (alloc:%d)", fname,
			(int)g.dirty_alloc);
		goto fail;
	}

	/* check file size correctly written, for 4.0.2 nsd.db failure */
	fsz = (uint64_t)lseek(fd, (off_t)0, SEEK_END);
	(void)lseek(fd, (off_t)0, SEEK_SET);
	if(g.fsize != fsz) {
		log_msg(LOG_WARNING, "%s: file size %llu but mmap header "
			"has size %llu", fname, (unsigned long long)fsz,
			(unsigned long long)g.fsize);
		goto fail;
	}

	/* mmap it */
	if(g.fsize < UDB_HEADER_SIZE || g.fsize < g.hsize) {
		log_msg(LOG_ERR, "%s: file too short", fname);
		goto fail;
	}
	if(g.fsize > (uint64_t)400*1024*1024*1024*1024) /* 400 Tb */ {
		log_msg(LOG_WARNING, "%s: file size too large %llu",
			fname, (unsigned long long)g.fsize);
		goto fail;
	}
	udb->base_size = (size_t)g.fsize;
#ifdef HAVE_MMAP
	/* note the size_t casts must be there for portability, on some
	 * systems the layout of memory is otherwise broken. */
	udb->base = mmap(NULL, (size_t)udb->base_size,
		(int)PROT_READ|PROT_WRITE, (int)MAP_SHARED,
		(int)udb->fd, (off_t)0);
#else
	udb->base = MAP_FAILED; errno = ENOSYS;
#endif
	if(udb->base == MAP_FAILED) {
		udb->base = NULL;
		log_msg(LOG_ERR, "mmap(size %u) error: %s",
			(unsigned)udb->base_size, strerror(errno));
	fail:
		close(fd);
		free(udb->fname);
		free(udb->ram_hash);
		free(udb);
		return NULL;
	}

	/* init completion */
	udb->glob_data = (udb_glob_d*)((char*)udb->base+sizeof(uint64_t));
	r = 0;
	/* cannot be dirty because that is goto fail above */
	if(udb->glob_data->dirty_alloc != udb_dirty_clean)
		r = 1;
	udb->alloc = udb_alloc_create(udb, (udb_alloc_d*)(
		(char*)udb->glob_data+sizeof(*udb->glob_data)));
	if(!udb->alloc) {
		log_msg(LOG_ERR, "out of memory");
		udb_base_free(udb);
		return NULL;
	}
	if(r) {
		/* and compact now, or resume compacting */
		udb_alloc_compact(udb, udb->alloc);
		udb_base_sync(udb, 1);
	}
	udb->glob_data->clean_close = 0;

	return udb;
}

udb_base* udb_base_create_read(const char* fname, udb_walk_relptr_func walkfunc,
	void* arg)
{
	int fd = open(fname, O_RDWR);
	if(fd == -1) {
		log_msg(LOG_ERR, "%s: %s", fname, strerror(errno));
		return NULL;
	}
	return udb_base_create_fd(fname, fd, walkfunc, arg);
}

/** init new udb_global structure */
static void udb_glob_init_new(udb_glob_d* g)
{
	memset(g, 0, sizeof(*g));
	g->hsize = UDB_HEADER_SIZE;
	g->fsize = UDB_HEADER_SIZE;
}

/** write data to file and check result */
static int
write_fdata(const char* fname, int fd, void* data, size_t len)
{
	ssize_t w;
	if((w=write(fd, data, len)) == -1) {
		log_msg(LOG_ERR, "%s: %s", fname, strerror(errno));
		close(fd);
		return 0;
	} else if(w != (ssize_t)len) {
		log_msg(LOG_ERR, "%s: short write (disk full?)", fname);
		close(fd);
		return 0;
	}
	return 1;
}

udb_base* udb_base_create_new(const char* fname, udb_walk_relptr_func walkfunc,
	void* arg)
{
	uint64_t m;
	udb_glob_d g;
	udb_alloc_d a;
	uint64_t endsize = UDB_HEADER_SIZE;
	uint64_t endexp = 0;
	int fd = open(fname, O_CREAT|O_RDWR, 0600);
	if(fd == -1) {
		log_msg(LOG_ERR, "%s: %s", fname, strerror(errno));
		return NULL;
	}
	m = UDB_MAGIC;
	udb_glob_init_new(&g);
	udb_alloc_init_new(&a);
	g.clean_close = 1;

	/* write new data to file (closes fd on error) */
	if(!write_fdata(fname, fd, &m, sizeof(m)))
		return NULL;
	if(!write_fdata(fname, fd, &g, sizeof(g)))
		return NULL;
	if(!write_fdata(fname, fd, &a, sizeof(a)))
		return NULL;
	if(!write_fdata(fname, fd, &endsize, sizeof(endsize)))
		return NULL;
	if(!write_fdata(fname, fd, &endexp, sizeof(endexp)))
		return NULL;
	/* rewind to start */
	if(lseek(fd, (off_t)0, SEEK_SET) == (off_t)-1) {
		log_msg(LOG_ERR, "%s: lseek %s", fname, strerror(errno));
		close(fd);
		return NULL;
	}
	/* truncate to the right size */
	if(ftruncate(fd, (off_t)g.fsize) < 0) {
		log_msg(LOG_ERR, "%s: ftruncate(%d): %s", fname,
			(int)g.fsize, strerror(errno));
		close(fd);
		return NULL;
	}
	return udb_base_create_fd(fname, fd, walkfunc, arg);
}

/** shrink the udb base if it has unused space at the end */
static void
udb_base_shrink(udb_base* udb, uint64_t nsize)
{
	udb->glob_data->dirty_alloc = udb_dirty_fsize;
	udb->glob_data->fsize = nsize;
	/* sync, does not *seem* to be required on Linux, but it is
	   certainly required on OpenBSD.  Otherwise changed data is lost. */
#ifdef HAVE_MMAP
	msync(udb->base, udb->base_size, MS_ASYNC);
#endif
	if(ftruncate(udb->fd, (off_t)nsize) != 0) {
		log_msg(LOG_ERR, "%s: ftruncate(%u) %s", udb->fname,
			(unsigned)nsize, strerror(errno));
	}
	udb->glob_data->dirty_alloc = udb_dirty_clean;
}

void udb_base_close(udb_base* udb)
{
	if(!udb)
		return;
	if(udb->fd != -1 && udb->base && udb->alloc) {
		uint64_t nsize = udb->alloc->disk->nextgrow;
		if(nsize < udb->base_size)
			udb_base_shrink(udb, nsize);
	}
	if(udb->fd != -1) {
		udb->glob_data->clean_close = 1;
		close(udb->fd);
		udb->fd = -1;
	}
	if(udb->base) {
#ifdef HAVE_MMAP
		if(munmap(udb->base, udb->base_size) == -1) {
			log_msg(LOG_ERR, "munmap: %s", strerror(errno));
		}
#endif
		udb->base = NULL;
	}
}

void udb_base_free(udb_base* udb)
{
	if(!udb)
		return;
	udb_base_close(udb);
	udb_alloc_delete(udb->alloc);
	free(udb->ram_hash);
	free(udb->fname);
	free(udb);
}

void udb_base_free_keep_mmap(udb_base* udb)
{
	if(!udb) return;
	if(udb->fd != -1) {
		close(udb->fd);
		udb->fd = -1;
	}
	udb->base = NULL;
	udb_alloc_delete(udb->alloc);
	free(udb->ram_hash);
	free(udb->fname);
	free(udb);
}

void udb_base_sync(udb_base* udb, int wait)
{
	if(!udb) return;
#ifdef HAVE_MMAP
	if(msync(udb->base, udb->base_size, wait?MS_SYNC:MS_ASYNC) != 0) {
		log_msg(LOG_ERR, "msync(%s) error %s",
			udb->fname, strerror(errno));
	}
#else
	(void)wait;
#endif
}

/** hash a chunk pointer */
static uint32_t
chunk_hash_ptr(udb_void p)
{
	/* put p into an array of uint32 */
	uint32_t h[sizeof(p)/sizeof(uint32_t)];
	memcpy(&h, &p, sizeof(h));
	return hashword(h, sizeof(p)/sizeof(uint32_t), 0x8763);
}

/** check that the given pointer is on the bucket for the given offset */
int udb_ptr_is_on_bucket(udb_base* udb, udb_ptr* ptr, udb_void to)
{
	uint32_t i = chunk_hash_ptr(to) & udb->ram_mask;
	udb_ptr* p;
	assert((size_t)i < udb->ram_size);
	for(p = udb->ram_hash[i]; p; p=p->next) {
		if(p == ptr)
			return 1;
	}
	return 0;
}

/** grow the ram array */
static void
grow_ram_hash(udb_base* udb, udb_ptr** newhash)
{
	size_t i;
	size_t osize= udb->ram_size;
	udb_ptr* p, *np;
	udb_ptr** oldhash = udb->ram_hash;
	udb->ram_size *= 2;
	udb->ram_mask <<= 1;
	udb->ram_mask |= 1;
	udb->ram_hash = newhash;
	/* have to link in every element in the old list into the new list*/
	for(i=0; i<osize; i++) {
		p = oldhash[i];
		while(p) {
			np = p->next;
			/* link into newhash */
			p->prev=NULL;
			p->next=newhash[chunk_hash_ptr(p->data)&udb->ram_mask];
			if(p->next) p->next->prev = p;
			/* go to next element of oldhash */
			p = np;
		}
	}
	free(oldhash);
}

void udb_base_link_ptr(udb_base* udb, udb_ptr* ptr)
{
	uint32_t i;
#ifdef UDB_CHECK
	assert(udb_valid_dataptr(udb, ptr->data)); /* must be to whole chunk*/
#endif
	udb->ram_num++;
	if(udb->ram_num == udb->ram_size && udb->ram_size<(size_t)0x7fffffff) {
		/* grow the array, if allocation succeeds */
		udb_ptr** newram = (udb_ptr**)xalloc_array_zero(
			sizeof(udb_ptr*), udb->ram_size*2);
		if(newram) {
			grow_ram_hash(udb, newram);
		}
	}
	i = chunk_hash_ptr(ptr->data) & udb->ram_mask;
	assert((size_t)i < udb->ram_size);

	ptr->prev = NULL;
	ptr->next = udb->ram_hash[i];
	udb->ram_hash[i] = ptr;
	if(ptr->next)
		ptr->next->prev = ptr;
}

void udb_base_unlink_ptr(udb_base* udb, udb_ptr* ptr)
{
	assert(ptr->data);
#ifdef UDB_CHECK
	assert(udb_valid_dataptr(udb, ptr->data)); /* ptr must be inited */
	assert(udb_ptr_is_on_bucket(udb, ptr, ptr->data));
#endif
	udb->ram_num--;
	if(ptr->next)
		ptr->next->prev = ptr->prev;
	if(ptr->prev)
		ptr->prev->next = ptr->next;
	else	{
		uint32_t i = chunk_hash_ptr(ptr->data) & udb->ram_mask;
		assert((size_t)i < udb->ram_size);
		udb->ram_hash[i] = ptr->next;
	}
}

/** change a set of ram ptrs to a new value */
static void
udb_base_ram_ptr_edit(udb_base* udb, udb_void old, udb_void newd)
{
	uint32_t io = chunk_hash_ptr(old) & udb->ram_mask;
	udb_ptr* p, *np;
	/* edit them and move them into the new position */
	p = udb->ram_hash[io];
	while(p) {
		np = p->next;
		if(p->data == old) {
			udb_base_unlink_ptr(udb, p);
			p->data = newd;
			udb_base_link_ptr(udb, p);
		}
		p = np;
	}
}

udb_rel_ptr* udb_base_get_userdata(udb_base* udb)
{
	return &udb->glob_data->user_global;
}

void udb_base_set_userdata(udb_base* udb, udb_void user)
{
#ifdef UDB_CHECK
	if(user) { assert(udb_valid_dataptr(udb, user)); }
#endif
	udb_rel_ptr_set(udb->base, &udb->glob_data->user_global, user);
}

void udb_base_set_userflags(udb_base* udb, uint8_t v)
{
	udb->glob_data->userflags = v;
}

uint8_t udb_base_get_userflags(udb_base* udb)
{
	return udb->glob_data->userflags;
}

/** re-mmap the udb to specified size */
static void*
udb_base_remap(udb_base* udb, udb_alloc* alloc, uint64_t nsize)
{
#ifdef HAVE_MMAP
	void* nb;
	/* for use with valgrind, do not use mremap, but the other version */
#ifdef MREMAP_MAYMOVE
	nb = mremap(udb->base, udb->base_size, nsize, MREMAP_MAYMOVE);
	if(nb == MAP_FAILED) {
		log_msg(LOG_ERR, "mremap(%s, size %u) error %s",
			udb->fname, (unsigned)nsize, strerror(errno));
		return 0;
	}
#else /* !HAVE MREMAP */
	/* use munmap-mmap to simulate mremap */
	if(munmap(udb->base, udb->base_size) != 0) {
		log_msg(LOG_ERR, "munmap(%s) error %s",
			udb->fname, strerror(errno));
	}
	/* provide hint for new location */
	/* note the size_t casts must be there for portability, on some
	 * systems the layout of memory is otherwise broken. */
	nb = mmap(udb->base, (size_t)nsize, (int)PROT_READ|PROT_WRITE,
		(int)MAP_SHARED, (int)udb->fd, (off_t)0);
	/* retry the mmap without basept in case of ENOMEM (FreeBSD8),
	 * the kernel can then try to mmap it at a different location
	 * where more memory is available */
	if(nb == MAP_FAILED && errno == ENOMEM) {
		nb = mmap(NULL, (size_t)nsize, (int)PROT_READ|PROT_WRITE,
			(int)MAP_SHARED, (int)udb->fd, (off_t)0);
	}
	if(nb == MAP_FAILED) {
		log_msg(LOG_ERR, "mmap(%s, size %u) error %s",
			udb->fname, (unsigned)nsize, strerror(errno));
		udb->base = NULL;
		return 0;
	}
#endif /* HAVE MREMAP */
	if(nb != udb->base) {
		/* fix up realpointers in udb and alloc */
		/* but mremap may have been nice and not move the base */
		udb->base = nb;
		udb->glob_data = (udb_glob_d*)((char*)nb+sizeof(uint64_t));
		/* use passed alloc pointer because the udb->alloc may not
		 * be initialized yet */
		alloc->disk = (udb_alloc_d*)((char*)udb->glob_data
			+sizeof(*udb->glob_data));
	}
	udb->base_size = nsize;
	return nb;
#else /* HAVE_MMAP */
	(void)udb; (void)alloc; (void)nsize;
	return NULL;
#endif /* HAVE_MMAP */
}

void
udb_base_remap_process(udb_base* udb)
{
	/* assume that fsize is still accessible */
	udb_base_remap(udb, udb->alloc, udb->glob_data->fsize);
}

/** grow file to specified size and re-mmap, return new base */
static void*
udb_base_grow_and_remap(udb_base* udb, uint64_t nsize)
{
	/* grow file by writing a single zero at that spot, the
	 * rest is filled in with zeroes. */
	uint8_t z = 0;
	ssize_t w;

	assert(nsize > 0);
	udb->glob_data->dirty_alloc = udb_dirty_fsize;
#ifdef HAVE_PWRITE
	if((w=pwrite(udb->fd, &z, sizeof(z), (off_t)(nsize-1))) == -1) {
#else
	if(lseek(udb->fd, (off_t)(nsize-1), SEEK_SET) == -1) {
		log_msg(LOG_ERR, "fseek %s: %s", udb->fname, strerror(errno));
		return 0;
	}
	if((w=write(udb->fd, &z, sizeof(z))) == -1) {
#endif
		log_msg(LOG_ERR, "grow(%s, size %u) error %s",
			udb->fname, (unsigned)nsize, strerror(errno));
		return 0;
	} else if(w != (ssize_t)sizeof(z)) {
		log_msg(LOG_ERR, "grow(%s, size %u) failed (disk full?)",
			udb->fname, (unsigned)nsize);
		return 0;
	}
	udb->glob_data->fsize = nsize;
	udb->glob_data->dirty_alloc = udb_dirty_clean;
	return udb_base_remap(udb, udb->alloc, nsize);
}

int udb_exp_size(uint64_t a)
{
	/* find enclosing value such that 2**x >= a */
	int x = 0;
	uint64_t i = a;
	assert(a != 0);

	i --;
	/* could optimise this with uint8* access, depends on endianness */
	/* first whole bytes */
	while( (i&(~(uint64_t)0xff)) ) {
		i >>= 8;
		x += 8;
	}
	/* now details */
	while(i) {
		i >>= 1;
		x ++;
	}
	assert( x>=0 && x<=63);
	assert( ((uint64_t)1<<x) >= a);
	assert( x==0 || /* <<x-1 without negative number analyzer complaints: */ (((uint64_t)1<<x)>>1) < a);
	return x;
}

int udb_exp_offset(uint64_t o)
{
	/* this means measuring the number of 0 bits on the right */
	/* so, if exp zero bits then (o&(2**x-1))==0 */
	int x = 0;
	uint64_t i = o;
	assert(o != 0);
	/* first whole bytes */
	while( (i&(uint64_t)0xff) == 0) {
		i >>= 8;
		x += 8;
	}
	/* now details */
	while( (i&(uint64_t)0x1) == 0) {
		i >>= 1;
		x ++;
	}
	assert( o % ((uint64_t)1<<x) == 0);
	assert( o % ((uint64_t)1<<(x+1)) != 0);
	return x;
}

void udb_alloc_init_new(udb_alloc_d* a)
{
	assert(UDB_HEADER_SIZE % UDB_ALLOC_CHUNK_MINSIZE == 0);
	memset(a, 0, sizeof(*a));
	/* set new allocations after header, as if allocated in a sequence
	 * of minsize allocations */
	a->nextgrow = UDB_HEADER_SIZE;
}

/** fsck the file size, false if failed and file is useless */
static int
fsck_fsize(udb_base* udb, udb_alloc* alloc)
{
	off_t realsize;
	log_msg(LOG_WARNING, "udb-fsck %s: file size wrong", udb->fname);
	realsize = lseek(udb->fd, (off_t)0, SEEK_END);
	if(realsize == (off_t)-1) {
		log_msg(LOG_ERR, "lseek(%s): %s", udb->fname, strerror(errno));
		return 0;
	}
	udb->glob_data->fsize = (uint64_t)realsize;
	if(!udb_base_remap(udb, alloc, (uint64_t)realsize))
		return 0;
	udb->glob_data->dirty_alloc = udb_dirty_clean;
	log_msg(LOG_WARNING, "udb-fsck %s: file size fixed (sync)", udb->fname);
	udb_base_sync(udb, 1);
	return 1;
}

/** regenerate freelist add a new free chunk, return next todo */
static udb_void
regen_free(void* base, udb_void c, int exp, udb_alloc_d* regen)
{
	udb_free_chunk_d* cp = UDB_FREE_CHUNK(c);
	uint64_t esz = (uint64_t)1<<exp;
	if(exp < UDB_ALLOC_CHUNK_MINEXP || exp > UDB_ALLOC_CHUNKS_MAX) {
		return 0;
	}
	cp->type = udb_chunk_type_free;
	cp->flags = 0;
	chunk_set_last(base, c, exp, (uint8_t)exp);
	cp->prev = 0;
	cp->next = regen->free[exp-UDB_ALLOC_CHUNK_MINEXP];
	if(cp->next)
		UDB_FREE_CHUNK(cp->next)->prev = c;
	regen->stat_free += esz;
	return c + esz;
}

/** regenerate xl chunk, return next todo */
static udb_void
regen_xl(void* base, udb_void c, udb_alloc_d* regen)
{
	udb_xl_chunk_d* cp = UDB_XL_CHUNK(c);
	uint64_t xlsz = cp->size;
	if( (xlsz&(UDB_ALLOC_CHUNK_SIZE-1)) != 0) {
		return 0;
	}
	if( (c&(UDB_ALLOC_CHUNK_SIZE-1)) != 0) {
		return 0;
	}
	/* fixup end-size and end-expmarker */
	regen->stat_alloc += xlsz;
	return c + xlsz;
}

/** regenerate data chunk, return next todo */
static udb_void
regen_data(void* base, udb_void c, int exp, udb_alloc_d* regen)
{
	uint64_t esz = (uint64_t)1<<exp;
	if(exp < UDB_ALLOC_CHUNK_MINEXP || exp > UDB_ALLOC_CHUNKS_MAX) {
		return 0;
	}
	chunk_set_last(base, c, exp, (uint8_t)exp);
	regen->stat_alloc += esz;
	return c + esz;
}

/** regenerate a relptr structure inside a data segment */
static void
regen_relptr_func(void* base, udb_rel_ptr* rp, void* arg)
{
	udb_void* a = (udb_void*)arg;
	/* ignore 0 pointers */
	if(!rp->data)
		return;

	/* edit relptrs that point to oldmoved to point to newmoved. */
	if(rp->data == a[0])
		rp->data = a[1];

	/* regenerate relptr lists, add this item to the relptr list for
	 * the data that it points to */
	udb_rel_ptr_link(base, rp, rp->data);
}

/** regenerate the relptrs store in this data segment */
static void
regen_its_ptrs(void* base, udb_base* udb, udb_chunk_d* atp,
	void* data, uint64_t dsz, udb_void rb_old, udb_void rb_new)
{
	udb_void arg[2];
	arg[0] = rb_old; arg[1] = rb_new;
	/* walk through the structs here and put them on their respective
	 * relptr lists */
	(*udb->walkfunc)(base, udb->walkarg, atp->type, data, dsz, 
		&regen_relptr_func, arg);

}

/** regenerate relptrlists in the file */
static void
regen_ptrlist(void* base, udb_base* udb, udb_alloc* alloc,
	udb_void rb_old, udb_void rb_new)
{
	udb_void at = alloc->udb->glob_data->hsize;
	/* clear all ptrlist start pointers in the file. */
	while(at < alloc->disk->nextgrow) {
		int exp = (int)UDB_CHUNK(at)->exp;
		udb_chunk_type tp = (udb_chunk_type)UDB_CHUNK(at)->type;
		if(exp == UDB_EXP_XL) {
			UDB_XL_CHUNK(at)->ptrlist = 0;
			at += UDB_XL_CHUNK(at)->size;
		} else if(tp == udb_chunk_type_free) {
			at += (uint64_t)1<<exp;
		} else { /* data chunk */
			UDB_CHUNK(at)->ptrlist = 0;
			at += (uint64_t)1<<exp;
		}
	}
	/* walk through all relptr structs and put on the right list. */
	at = alloc->udb->glob_data->hsize;
	while(at < alloc->disk->nextgrow) {
		udb_chunk_d* atp = UDB_CHUNK(at);
		int exp = (int)atp->exp;
		udb_chunk_type tp = (udb_chunk_type)atp->type;
		uint64_t sz = ((exp == UDB_EXP_XL)?UDB_XL_CHUNK(at)->size:
			(uint64_t)1<<exp);
		if(exp == UDB_EXP_XL) {
			assert(at != rb_old); /* should have been freed */
			regen_its_ptrs(base, udb, atp,
				((char*)atp)+sizeof(udb_xl_chunk_d),
				sz-sizeof(udb_xl_chunk_d) - sizeof(uint64_t)*2,
				rb_old, rb_new);
			at += sz;
		} else if(tp == udb_chunk_type_free) {
			at += sz;
		} else { /* data chunk */
			assert(at != rb_old); /* should have been freed */
			regen_its_ptrs(base, udb, atp,
				((char*)atp)+sizeof(udb_chunk_d),
				sz-sizeof(udb_chunk_d)-1, rb_old, rb_new);
			at += sz;
		}
	}
}


/** mark free elements from ex XL chunk space and later fixups pick that up */
static void
rb_mark_free_segs(void* base, udb_void s, uint64_t m)
{
	udb_void q = s + m - UDB_ALLOC_CHUNK_SIZE;
	/* because of header and alignment we know s >= UDB_ALLOC_CHUNK_SIZE*/
	assert(s >= UDB_ALLOC_CHUNK_SIZE);
	while(q >= s) {
		UDB_CHUNK(q)->exp = UDB_ALLOC_CHUNKS_MAX;
		UDB_CHUNK(q)->type = udb_chunk_type_free;
		q -= UDB_ALLOC_CHUNK_SIZE;
	}
}


/** fsck rollback or rollforward XL move results */
static int
fsck_rb_xl(void* base, udb_base* udb, udb_void rb_old, udb_void rb_new,
	uint64_t rb_size, uint64_t rb_seg)
{

	if(rb_old <= rb_new)
		return 0; /* XL move one way */
	if( (rb_size&(UDB_ALLOC_CHUNK_SIZE-1)) != 0)
		return 0; /* not aligned */
	if( (rb_old&(UDB_ALLOC_CHUNK_SIZE-1)) != 0)
		return 0; /* not aligned */
	if( (rb_new&(UDB_ALLOC_CHUNK_SIZE-1)) != 0)
		return 0; /* not aligned */
	if(rb_new + rb_size <= rb_old) {
		/* not overlapping: resume copy */
		memcpy(UDB_CHUNK(rb_new), UDB_CHUNK(rb_old), rb_size);
		/* and free up old piece(s) */
		rb_mark_free_segs(base, rb_old, rb_size);
	} else {
		/* overlapping, see what segment we stopped at
		 * and continue there. */
		move_xl_segment(base, udb, rb_old, rb_new, rb_size, rb_seg);
		/* free up old piece(s); from the end of the moved segment,
		 * until the end of the old segment */
		rb_mark_free_segs(base, rb_new+rb_size, (rb_old+rb_size)-
			(rb_new+rb_size));
	}
	/* do not call fix_ptrs, regenptrs does the job */
	return 1;
}

/** fsck rollback or rollforward move results */
static int
fsck_rb(void* base, udb_void rb_old, udb_void rb_new, uint64_t rb_size,
	udb_void* make_free)
{
	if( (rb_size&(rb_size-1)) != 0)
		return 0; /* not powerof2 */
	if( (rb_old&(rb_size-1)) != 0)
		return 0; /* not aligned */
	if( (rb_new&(rb_size-1)) != 0)
		return 0; /* not aligned */
	/* resume copy */
	memcpy(UDB_CHUNK(rb_new), UDB_CHUNK(rb_old), rb_size);
	/* do not call fix_ptrs, regenptrs does the job */
	/* make sure udb_old is freed */
	*make_free = rb_old;
	return 1;
}

/** fsck the file and salvage, false if failed and file is useless */
static int
fsck_file(udb_base* udb, udb_alloc* alloc, int moved)
{
	void* base = udb->base;
	udb_alloc_d regen;
	udb_void at = udb->glob_data->hsize;
	udb_void rb_old = udb->glob_data->rb_old;
	udb_void rb_new = udb->glob_data->rb_new;
	udb_void rb_seg = udb->glob_data->rb_seg;
	udb_void make_free = 0;
	uint64_t rb_size = udb->glob_data->rb_size;
	log_msg(LOG_WARNING, "udb-fsck %s: salvaging", udb->fname);
	/* walk through the file, use the exp values to see what can be
	 * salvaged */
	if(moved && rb_old && rb_new && rb_size) {
		if(rb_old+rb_size <= alloc->disk->nextgrow
			&& rb_new+rb_size <= alloc->disk->nextgrow) {
			/* we can use the move information to fix up the
			 * duplicate element (or partially moved element) */
			if(rb_size > 1024*1024) {
				/* XL chunk */
				if(!fsck_rb_xl(base, udb, rb_old, rb_new,
					rb_size, rb_seg))
					return 0;
			} else {
				if(!fsck_rb(base, rb_old, rb_new, rb_size,
					&make_free))
					return 0;
			}
		}
	}
	
	/* rebuild freelists */
	/* recalculate stats in alloc (except 'stat_data') */
	/* possibly new end 'nextgrow' value */
	memset(&regen, 0, sizeof(regen));
	regen.nextgrow = alloc->disk->nextgrow;
	while(at < regen.nextgrow) {
		/* figure out this chunk */
		int exp = (int)UDB_CHUNK(at)->exp;
		udb_chunk_type tp = (udb_chunk_type)UDB_CHUNK(at)->type;
		/* consistency check possible here with end-exp */
		if(tp == udb_chunk_type_free || at == make_free) {
			at = regen_free(base, at, exp, &regen);
			if(!at) return 0;
		} else if(exp == UDB_EXP_XL) {
			/* allocated data of XL size */
			at = regen_xl(base, at, &regen);
			if(!at) return 0;
		} else if(exp >= UDB_ALLOC_CHUNK_MINEXP
			&& exp <= UDB_ALLOC_CHUNKS_MAX) {
			/* allocated data */
			at = regen_data(base, at, exp, &regen);
			if(!at) return 0;
		} else {
			/* garbage; this must be EOF then */
			regen.nextgrow = at;
			break;
		}
	}
	*alloc->disk = regen;

	/* rebuild relptr lists */
	regen_ptrlist(base, udb, alloc, rb_old, rb_new);

	log_msg(LOG_WARNING, "udb-fsck %s: salvaged successfully (sync)",
		udb->fname);
	udb->glob_data->rb_old = 0;
	udb->glob_data->rb_new = 0;
	udb->glob_data->rb_size = 0;
	udb->glob_data->dirty_alloc = udb_dirty_clean;
	udb_base_sync(udb, 1);
	return 1;
}


udb_alloc* udb_alloc_create(udb_base* udb, udb_alloc_d* disk)
{
	udb_alloc* alloc = (udb_alloc*)xalloc_zero(sizeof(*alloc));
	if(!alloc)
		return NULL;
	alloc->udb = udb;
	alloc->disk = disk;
	/* see if committed but uncompleted actions need to be done */
	/* preserves the alloc state */
	if(udb->glob_data->dirty_alloc != udb_dirty_clean) {
		if(udb->glob_data->dirty_alloc == udb_dirty_fsize) {
			if(fsck_fsize(udb, alloc))
				return alloc;
		} else if(udb->glob_data->dirty_alloc == udb_dirty_fl) {
			if(fsck_file(udb, alloc, 0))
				return alloc;
		} else if(udb->glob_data->dirty_alloc == udb_dirty_compact) {
			if(fsck_file(udb, alloc, 1))
				return alloc;
		}
		log_msg(LOG_ERR, "error: file allocation dirty (%d)",
			(int)udb->glob_data->dirty_alloc);
		free(alloc);
		return NULL;
	}
	return alloc;
}

void udb_alloc_delete(udb_alloc* alloc)
{
	if(!alloc) return;
	free(alloc);
}

/** unlink this element from its freelist */
static void
udb_alloc_unlink_fl(void* base, udb_alloc* alloc, udb_void chunk, int exp)
{
	udb_free_chunk_d* fp = UDB_FREE_CHUNK(chunk);
	assert(chunk);
	/* chunk is a free chunk */
	assert(fp->exp == (uint8_t)exp);
	assert(fp->type == udb_chunk_type_free);
	assert(chunk_get_last(base, chunk, exp) == (uint8_t)exp);
	/* and thus freelist not empty */
	assert(alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP]);
	/* unlink */
	if(fp->prev)
		UDB_FREE_CHUNK(fp->prev)->next = fp->next;
	else	alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP] = fp->next;
	if(fp->next)
		UDB_FREE_CHUNK(fp->next)->prev = fp->prev;
}

/** pop first element off freelist, list may not be empty */
static udb_void
udb_alloc_pop_fl(void* base, udb_alloc* alloc, int exp)
{
	udb_void f = alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP];
	udb_free_chunk_d* fp = UDB_FREE_CHUNK(f);
	assert(f);
	assert(fp->exp == (uint8_t)exp);
	assert(fp->type == udb_chunk_type_free);
	assert(chunk_get_last(base, f, exp) == (uint8_t)exp);
	alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP] = fp->next;
	if(fp->next) {
		UDB_FREE_CHUNK(fp->next)->prev = 0;
	}
	return f;
}

/** push new element onto freelist */
static void
udb_alloc_push_fl(void* base, udb_alloc* alloc, udb_void f, int exp)
{
	udb_free_chunk_d* fp = UDB_FREE_CHUNK(f);
	assert(f);
	fp->exp = (uint8_t)exp;
	fp->type = udb_chunk_type_free;
	fp->flags = 0;
	fp->prev = 0;
	fp->next = alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP];
	if(fp->next)
		UDB_FREE_CHUNK(fp->next)->prev = f;
	chunk_set_last(base, f, exp, (uint8_t)exp);
	alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP] = f;
}

/** push new element onto freelist - do not initialize the elt */
static void
udb_alloc_push_fl_noinit(void* base, udb_alloc* alloc, udb_void f, int exp)
{
	udb_free_chunk_d* fp = UDB_FREE_CHUNK(f);
	assert(f);
	assert(fp->exp == (uint8_t)exp);
	assert(fp->type == udb_chunk_type_free);
	assert(chunk_get_last(base, f, exp) == (uint8_t)exp);
	fp->prev = 0;
	fp->next = alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP];
	if(fp->next)
		UDB_FREE_CHUNK(fp->next)->prev = f;
	alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP] = f;
}

/** add free chunks at end until specified alignment occurs */
static void
grow_align(void* base, udb_alloc* alloc, uint64_t esz)
{
	while( (alloc->disk->nextgrow & (esz-1)) != 0) {
		/* the nextgrow is not a whole multiple of esz. */
		/* grow a free chunk of max allowed size */
		int fexp = udb_exp_offset(alloc->disk->nextgrow);
		uint64_t fsz = (uint64_t)1<<fexp;
		udb_void f = alloc->disk->nextgrow;
		udb_void fn = alloc->disk->nextgrow+fsz;
		assert(fn <= alloc->udb->base_size);
		alloc->disk->stat_free += fsz;
		udb_alloc_push_fl(base, alloc, f, fexp);
		/* now increase nextgrow to commit that free chunk */
		alloc->disk->nextgrow = fn;
	}
}

/** append chunks at end of memory space to get size exp, return dataptr */
static udb_void
grow_chunks(void* base, udb_alloc* alloc, size_t sz, int exp)
{
	uint64_t esz = (uint64_t)1<<exp;
	udb_void ret;
	alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;
	grow_align(base, alloc, esz);
	/* free chunks are grown, grow the one we want to use */
	ret = alloc->disk->nextgrow;
	/* take a new alloced chunk into use */
	UDB_CHUNK(ret)->exp = (uint8_t)exp;
	UDB_CHUNK(ret)->flags = 0;
	UDB_CHUNK(ret)->ptrlist = 0;
	UDB_CHUNK(ret)->type = udb_chunk_type_data;
	/* store last octet */
	chunk_set_last(base, ret, exp, (uint8_t)exp);
	/* update stats */
	alloc->disk->stat_alloc += esz;
	alloc->disk->stat_data += sz;
	/* now increase nextgrow to commit this newly allocated chunk */
	alloc->disk->nextgrow += esz;
	assert(alloc->disk->nextgrow <= alloc->udb->base_size);
	alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
	return ret + sizeof(udb_chunk_d); /* ptr to data */
}

/** calculate how much space is necessary to grow for this exp */
static uint64_t
grow_end_calc(udb_alloc* alloc, int exp)
{
	uint64_t sz = (uint64_t)1<<exp;
	uint64_t ng = alloc->disk->nextgrow;
	uint64_t res;
	/* if nextgrow is 2**expness, no extra growth needed, only size */
	if( (ng & (sz-1)) == 0) {
		/* sz-1 is like 0xfff, and checks if ng is whole 2**exp */
		return ng+sz; /* must grow exactly 2**exp */
	}
	/* grow until 2**expness and then we need 2**exp as well */
	/* so, round ng down to whole sz (basically  ng-ng%sz, or ng/sz*sz)
	 * and then add the sz twice (go up to whole sz, and to allocate) */
	res = (ng & ~(sz-1)) + 2*sz;
	return res;
}

/** see if we need to grow more than specified to enable sustained growth */
static uint64_t
grow_extra_check(udb_alloc* alloc, uint64_t ge)
{
	const uint64_t mb = 1024*1024;
	uint64_t bsz = alloc->udb->base_size;
	if(bsz <= mb) {
		/* below 1 Mb, double sizes for exponential growth */
		/* takes about 15 times to grow to 1Mb */
		if(ge < bsz*2)
			return bsz*2;
	} else {
		uint64_t gnow = ge - bsz;
		/* above 1Mb, grow at least 1 Mb, or 12.5% of current size,
		 * in whole megabytes rounded up. */
		uint64_t want = ((bsz / 8) & ~(mb-1)) + mb;
		if(gnow < want)
			return bsz + want;
	}
	return ge;
}

/** see if free space is enough to warrant shrink (while file is open) */
static int
enough_free(udb_alloc* alloc)
{
	if(alloc->udb->base_size <= 2*1024*1024) {
		/* below 1 Mb, grown by double size, (so up to 2 mb),
		 * do not shrink unless we can 1/3 in size */
		if(((size_t)alloc->disk->nextgrow)*3 <= alloc->udb->base_size)
			return 1;
	} else {
		/* grown 12.5%, shrink 25% if possible, at least one mb */
		/* between 1mb and 4mb size, it shrinks by 1mb if possible */
		uint64_t space = alloc->udb->base_size - alloc->disk->nextgrow;
		if(space >= 1024*1024 && (space*4 >= alloc->udb->base_size
			|| alloc->udb->base_size < 4*1024*1024))
			return 1;
	}
	return 0;
}

/** grow space for a chunk of 2**exp and return dataptr */
static udb_void
udb_alloc_grow_space(void* base, udb_alloc* alloc, size_t sz, int exp)
{
	/* commit the grow action
	 * - the file grow only changes filesize, but not the nextgrow.
	 * - taking space after nextgrow into use (as free space),
	 *   is like free-ing a chunk (one at a time).
	 * - and the last chunk taken into use is like alloc.
	 */
	/* predict how much free space is needed for this */
	uint64_t grow_end = grow_end_calc(alloc, exp);
	assert(alloc->udb->base_size >= alloc->disk->nextgrow);
	if(grow_end <= alloc->udb->base_size) {
		/* we can do this with the available space */
		return grow_chunks(base, alloc, sz, exp);
	}
	/* we have to grow the file, re-mmap */
	/* see if we need to grow a little more, to avoid endless grow
	 * efforts on adding data */
	grow_end = grow_extra_check(alloc, grow_end);
	if(!(base=udb_base_grow_and_remap(alloc->udb, grow_end))) {
		return 0; /* mmap or write failed (disk or mem full) */
	}
	/* we have enough space now */
	assert(grow_end <= alloc->udb->base_size);
	assert(alloc->udb->glob_data->fsize == alloc->udb->base_size);
	return grow_chunks(base, alloc, sz, exp);
}

/** take XL allocation into use at end of file, return dataptr */
static udb_void
grow_xl(void* base, udb_alloc* alloc, uint64_t xlsz, uint64_t sz)
{
	udb_void ret;
	udb_xl_chunk_d* p;
	alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;

	/* align growth to whole mbs */
	grow_align(base, alloc, UDB_ALLOC_CHUNK_SIZE);

	/* grow XL segment */
	ret = alloc->disk->nextgrow;
	p = UDB_XL_CHUNK(ret);
	p->exp = UDB_EXP_XL;
	p->size = xlsz;
	p->flags = 0;
	p->ptrlist = 0;
	p->type = udb_chunk_type_data;

	/* also put size and marker at end for compaction */
	*((uint64_t*)(UDB_REL(base, ret+xlsz-sizeof(uint64_t)*2))) = xlsz;
	*((uint8_t*)(UDB_REL(base, ret+xlsz-1))) = UDB_EXP_XL;

	/* stats */
	alloc->disk->stat_data += sz;
	alloc->disk->stat_alloc += xlsz;
	/* now increase the nextgrow to commit this xl chunk */
	alloc->disk->nextgrow += xlsz;
	alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
	return ret + sizeof(udb_xl_chunk_d); /* data ptr */
}

/** make space for XL allocation */
static udb_void
udb_alloc_xl_space(void* base, udb_alloc* alloc, size_t sz)
{
	/* allocate whole mbs of space, at end of space */
	uint64_t asz = sz + sizeof(udb_xl_chunk_d) + sizeof(uint64_t)*2;
	uint64_t need=(asz+UDB_ALLOC_CHUNK_SIZE-1)&(~(UDB_ALLOC_CHUNK_SIZE-1));
	uint64_t grow_end = grow_end_calc(alloc, UDB_ALLOC_CHUNKS_MAX) + need;
	assert(need >= asz);
	if(grow_end <= alloc->udb->base_size) {
		/* can do this in available space */
		return grow_xl(base, alloc, need, sz);
	}
	/* have to grow file and re-mmap */
	grow_end = grow_extra_check(alloc, grow_end);
	if(!(base=udb_base_grow_and_remap(alloc->udb, grow_end))) {
		return 0; /* mmap or write failed (disk or mem full) */
	}
	/* we have enough space now */
	assert(grow_end <= alloc->udb->base_size);
	assert(alloc->udb->glob_data->fsize == alloc->udb->base_size);
	return grow_xl(base, alloc, need, sz);
}

/** divide big(2**e2) into pieces so 2**exp fits */
static udb_void
udb_alloc_subdivide(void* base, udb_alloc* alloc, udb_void big, int e2,
	int exp)
{
	int e = e2;
	uint64_t sz = (uint64_t)1<<e2;
	assert(big && e2 > exp);
	/* so the returned piece to use is the first piece,
	 * offload the later half until it fits */
	do {
		sz >>= 1; /* divide size of big by two */
		e--;      /* that means its exp is one smaller */
		udb_alloc_push_fl(base, alloc, big+sz, e);
	} while(e != exp);
	/* exit loop when last pushed is same size as what we want */
	return big;
}

/** returns the exponent size of the chunk needed for data sz */
static int
udb_alloc_exp_needed(size_t sz)
{
	uint64_t asz = sz + sizeof(udb_chunk_d) + 1;
	int exp;
	if(asz > UDB_ALLOC_CHUNK_SIZE) {
		return UDB_EXP_XL;
	} else if(asz <= UDB_ALLOC_CHUNK_MINSIZE) {
		return UDB_ALLOC_CHUNK_MINEXP;
	}
	exp = udb_exp_size(asz);
	assert(exp <= UDB_ALLOC_CHUNKS_MAX);
	return exp;
}

udb_void udb_alloc_space(udb_alloc* alloc, size_t sz)
{
	void* base = alloc->udb->base;
	/* calculate actual allocation size */
	int e2, exp = udb_alloc_exp_needed(sz);
	if(exp == UDB_EXP_XL)
		return udb_alloc_xl_space(base, alloc, sz);
	/* see if there is a free chunk of that size exactly */
	if(alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP]) {
		/* snip from freelist, udb_chunk_d */
		udb_void ret;
		alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;
		ret = udb_alloc_pop_fl(base, alloc, exp);
		/* use it - size octets already OK */
		UDB_CHUNK(ret)->flags = 0;
		UDB_CHUNK(ret)->ptrlist = 0;
		UDB_CHUNK(ret)->type = udb_chunk_type_data;
		/* update stats */
		alloc->disk->stat_data += sz;
		alloc->disk->stat_alloc += (1<<exp);
		assert(alloc->disk->stat_free >= (1u<<exp));
		alloc->disk->stat_free -= (1<<exp);
		alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
		return ret + sizeof(udb_chunk_d); /* ptr to data */
	}
	/* see if we can subdivide a larger chunk */
	for(e2 = exp+1; e2 <= UDB_ALLOC_CHUNKS_MAX; e2++)
		if(alloc->disk->free[e2-UDB_ALLOC_CHUNK_MINEXP]) {
			udb_void big, ret; /* udb_chunk_d */
			alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;
			big = udb_alloc_pop_fl(base, alloc, e2);
			/* push other parts onto freelists (needs inited) */
			ret = udb_alloc_subdivide(base, alloc, big, e2, exp);
			/* use final part (needs inited) */
			UDB_CHUNK(ret)->exp = (uint8_t)exp;
			/* if stop here; the new exp makes smaller free chunk*/
			UDB_CHUNK(ret)->flags = 0;
			UDB_CHUNK(ret)->ptrlist = 0;
			/* set type to commit data chunk */
			UDB_CHUNK(ret)->type = udb_chunk_type_data;
			/* store last octet */
			chunk_set_last(base, ret, exp, (uint8_t)exp);
			/* update stats */
			alloc->disk->stat_data += sz;
			alloc->disk->stat_alloc += (1<<exp);
			assert(alloc->disk->stat_free >= (1u<<exp));
			alloc->disk->stat_free -= (1<<exp);
			alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
			return ret + sizeof(udb_chunk_d); /* ptr to data */
		}
	/* we need to grow an extra chunk */
	return udb_alloc_grow_space(base, alloc, sz, exp);
}

/** see if there is free space to allocate a chunk into */
static int
have_free_for(udb_alloc* alloc, int exp)
{
	int e2;
	if(alloc->disk->free[exp-UDB_ALLOC_CHUNK_MINEXP])
		return exp;
	for(e2 = exp+1; e2 <= UDB_ALLOC_CHUNKS_MAX; e2++)
		if(alloc->disk->free[e2-UDB_ALLOC_CHUNK_MINEXP]) {
			return e2;
		}
	return 0;
}

/** fix relptr prev and next for moved relptr structures */
static void
chunk_fix_ptr_each(void* base, udb_rel_ptr* rp, void* arg)
{
	udb_void* data = (udb_void*)arg;
	udb_void r;
	if(!rp->data)
		return;
	r = UDB_SYSTOREL(base, rp);
	if(rp->next)
		UDB_REL_PTR(rp->next)->prev = r;
	if(rp->prev)
		UDB_REL_PTR(rp->prev)->next = r;
	else	{
		/* if this is a pointer to its own chunk, fix it up;
		 * the data ptr gets set by relptr_edit later. */
		if(rp->data == data[0])
			UDB_CHUNK(data[1])->ptrlist = r;
		else	UDB_CHUNK(chunk_from_dataptr(rp->data))->ptrlist = r;
	}
}

/** fix pointers from and to a moved chunk */
static void
chunk_fix_ptrs(void* base, udb_base* udb, udb_chunk_d* cp, udb_void data,
	uint64_t dsz, udb_void olddata)
{
	udb_void d[2];
	d[0] = olddata;
	d[1] = data;
	(*udb->walkfunc)(base, udb->walkarg, cp->type, UDB_REL(base, data),
		dsz, &chunk_fix_ptr_each, d);
	udb_rel_ptr_edit(base, cp->ptrlist, data);
	udb_base_ram_ptr_edit(udb, olddata, data);
}

/** move an allocated chunk to use a free chunk */
static void
move_chunk(void* base, udb_alloc* alloc, udb_void f, int exp, uint64_t esz,
	int e2)
{
	udb_void res = udb_alloc_pop_fl(base, alloc, e2);
	udb_chunk_d* rp;
	udb_chunk_d* fp;
	if(exp != e2) {
		/* it is bigger, subdivide it */
		res = udb_alloc_subdivide(base, alloc, res, e2, exp);
	}
	assert(res != f);
	/* setup rollback information */
	alloc->udb->glob_data->rb_old = f;
	alloc->udb->glob_data->rb_new = res;
	alloc->udb->glob_data->rb_size = esz;
	/* take the res, exp into use */
	rp = UDB_CHUNK(res);
	fp = UDB_CHUNK(f);
	/* copy over the data */
	memcpy(rp, fp, esz);
	/* adjust rel ptrs */
	chunk_fix_ptrs(base, alloc->udb, rp, res+sizeof(udb_chunk_d),
		esz-sizeof(udb_chunk_d)-1, f+sizeof(udb_chunk_d));

	/* do not freeup the fp; caller does that */
}

/** unlink several free elements to overwrite with xl chunk */
static void
free_xl_space(void* base, udb_alloc* alloc, udb_void s, uint64_t m)
{
	udb_void q = s + m - UDB_ALLOC_CHUNK_SIZE;
	/* because of header and alignment we know s >= UDB_ALLOC_CHUNK_SIZE*/
	assert(s >= UDB_ALLOC_CHUNK_SIZE);
	while(q >= s) {
		assert(UDB_CHUNK(q)->exp == UDB_ALLOC_CHUNKS_MAX);
		assert(UDB_CHUNK(q)->type == udb_chunk_type_free);
		udb_alloc_unlink_fl(base, alloc, q, UDB_ALLOC_CHUNKS_MAX);
		q -= UDB_ALLOC_CHUNK_SIZE;
	}
}

/** move an XL chunk, and keep track of segments for rollback */
static void
move_xl_segment(void* base, udb_base* udb, udb_void xl, udb_void n,
	uint64_t sz, uint64_t startseg)
{
	udb_xl_chunk_d* xlp = UDB_XL_CHUNK(xl);
	udb_xl_chunk_d* np = UDB_XL_CHUNK(n);
	uint64_t amount = xl - n;
	assert(n < xl); /* move to compact */

	/* setup move rollback */
	udb->glob_data->rb_old = xl;
	udb->glob_data->rb_new = n;
	udb->glob_data->rb_size = sz;

	/* is it overlapping? */
	if(sz <= amount) {
		memcpy(np, xlp, sz);
	} else {
		/* move and commit per 1M segment to avoid data loss */
		uint64_t seg, maxseg = amount/UDB_ALLOC_CHUNK_SIZE;
		for(seg = startseg; seg<maxseg; seg++) {
			udb->glob_data->rb_seg = seg;
			memcpy(np+seg*UDB_ALLOC_CHUNK_SIZE,
				xlp+seg*UDB_ALLOC_CHUNK_SIZE,
				UDB_ALLOC_CHUNK_SIZE);
		}

	}
}

/** move list of XL chunks to the front by the shift amount */
static void
move_xl_list(void* base, udb_alloc* alloc, udb_void xl_start, uint64_t xl_sz,
	uint64_t amount)
{
	udb_void xl = xl_start;
	assert( (xl_start&(UDB_ALLOC_CHUNK_SIZE-1)) == 0 ); /* aligned */
	assert( (amount&(UDB_ALLOC_CHUNK_SIZE-1)) == 0 ); /* multiples */
	assert( (xl_sz&(UDB_ALLOC_CHUNK_SIZE-1)) == 0 ); /* multiples */
	while(xl < xl_start+xl_sz) {
		udb_xl_chunk_d* xlp = UDB_XL_CHUNK(xl);
		udb_void n = xl-amount;
		uint64_t sz = xlp->size;
		assert(xlp->exp == UDB_EXP_XL);
		move_xl_segment(base, alloc->udb, xl, n, sz, 0);
		chunk_fix_ptrs(base, alloc->udb, UDB_CHUNK(n),
			n+sizeof(udb_xl_chunk_d),
			sz-sizeof(udb_xl_chunk_d)-sizeof(uint64_t)*2,
			xl+sizeof(udb_xl_chunk_d));
	}
	alloc->disk->stat_free -= amount;
	alloc->disk->nextgrow -= amount;
	alloc->udb->glob_data->rb_old = 0;
	alloc->udb->glob_data->rb_new = 0;
	alloc->udb->glob_data->rb_size = 0;
}

/** see if free chunk can coagulate with another chunk, return other chunk */
static udb_void
coagulate_possible(void* base, udb_alloc* alloc, udb_void f, int exp,
	uint64_t esz)
{
	udb_void other = f^esz;
	if(exp == UDB_ALLOC_CHUNKS_MAX)
		return 0; /* no further merges */
	if(other >= alloc->udb->base_size)
		return 0; /* not allocated */
	if(other >= alloc->disk->nextgrow)
		return 0; /* not in use */
	if(other < alloc->udb->glob_data->hsize)
		return 0; /* cannot merge with header */
		/* the header is also protected by the special exp marker */
	/* see if the other chunk is a free chunk */

	/* check closest marker to avoid large memory churn */
	/* and also it makes XL allocations and header special markers work */
	if(f > other) {
		assert(f > 1); /* this is certain because of header */
		if(*((uint8_t*)UDB_REL(base, f-1)) == (uint8_t)exp) {
			/* can do it if the other part is a free chunk */
			assert(UDB_FREE_CHUNK(other)->exp == (uint8_t)exp);
			if(UDB_CHUNK(other)->type == udb_chunk_type_free)
				return other;
		}
	} else {
		if(UDB_CHUNK(other)->exp == (uint8_t)exp) {
			/* can do it if the other part is a free chunk */
			assert(chunk_get_last(base, other, exp)==(uint8_t)exp);
			if(UDB_CHUNK(other)->type == udb_chunk_type_free)
				return other;
		}
	}
	return 0;
}

/** coagulate and then add new free segment, return final free segment */
static udb_void
coagulate_and_push(void* base, udb_alloc* alloc, udb_void last, int exp,
	uint64_t esz)
{
	/* new free chunk here, attempt coagulate */
	udb_void other;
	while( (other=coagulate_possible(base, alloc, last, exp, esz)) ) {
		/* unlink that other chunk */
		udb_alloc_unlink_fl(base, alloc, other, exp);
		/* merge up */
		if(other < last)
			last = other;
		exp++;
		esz <<= 1;
	}
	/* free the final segment */
	udb_alloc_push_fl(base, alloc, last, exp);
	return last;
}

/** attempt to compact the data and move free space to the end */
int
udb_alloc_compact(void* base, udb_alloc* alloc)
{
	udb_void last;
	int exp, e2;
	uint64_t esz;
	uint64_t at = alloc->disk->nextgrow;
	udb_void xl_start = 0;
	uint64_t xl_sz = 0;
	if(alloc->udb->inhibit_compact)
		return 1;
	alloc->udb->useful_compact = 0;
	while(at > alloc->udb->glob_data->hsize) {
		/* grab last entry */
		exp = (int)*((uint8_t*)UDB_REL(base, at-1));
		if(exp == UDB_EXP_XL) {
			/* for XL chunks:
			 * - inspect the size of the XLchunklist at end
			 * - attempt to compact in front of of XLchunklist
			 */
			uint64_t xlsz = *((uint64_t*)UDB_REL(base,
				at-sizeof(uint64_t)*2));
			udb_void xl = at-xlsz;
#ifndef NDEBUG
			udb_xl_chunk_d* xlp = UDB_XL_CHUNK(xl);
			assert(xlp->exp == UDB_EXP_XL);
			assert(xlp->type != udb_chunk_type_free);
#endif
			/* got thesegment add to the xl chunk list */
			if(xl_start != 0 && xl+xlsz != xl_start) {
				/* nonadjoining XL part, but they are aligned,
				 * so the space in between is whole Mbs,
				 * shift the later part(s) and continue */
				uint64_t m = xl_start - (xl+xlsz);
				assert(xl_start > xl+xlsz);
				alloc->udb->glob_data->dirty_alloc = udb_dirty_compact;
				free_xl_space(base, alloc, xl+xlsz, m);
				move_xl_list(base, alloc, xl_start, xl_sz, m);
				alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
			}
			xl_start = xl;
			xl_sz += xlsz;
			at = xl;
			continue;
			/* end of XL if */
		} else if(exp < UDB_ALLOC_CHUNK_MINEXP
			|| exp > UDB_ALLOC_CHUNKS_MAX)
			break; /* special chunk or garbage */
		esz = (uint64_t)1<<exp;
		last = at - esz;
		assert(UDB_CHUNK(last)->exp == (uint8_t)exp);
		if(UDB_CHUNK(last)->type == udb_chunk_type_free) {
			/* if xlstart continue looking to move stuff, but do
			 * not unlink this free segment */
			if(!xl_start) {
				/* it is a free chunk, remove it */
				alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;
				udb_alloc_unlink_fl(base, alloc, last, exp);
				alloc->disk->stat_free -= esz;
				alloc->disk->nextgrow = last;
				alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
				/* and continue at this point */
			}
			at = last;
		} else if( (e2=have_free_for(alloc, exp)) ) {
			/* last entry can be allocated in free chunks
			 * move it to its new position, adjust rel_ptrs */
			alloc->udb->glob_data->dirty_alloc = udb_dirty_compact;
			move_chunk(base, alloc, last, exp, esz, e2);
			if(xl_start) {
				last = coagulate_and_push(base, alloc,
					last, exp, esz);
			} else {
				/* shorten usage */
				alloc->disk->stat_free -= esz;
				alloc->disk->nextgrow = last;
			}
			alloc->udb->glob_data->rb_old = 0;
			alloc->udb->glob_data->rb_new = 0;
			alloc->udb->glob_data->rb_size = 0;
			alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
			/* and continue in front of it */
			at = last;
		} else {
			/* cannot compact this block, stop compacting */
			break;
		}
		/* if that worked, repeat it */
	}
	/* if we passed xl chunks, see if XL-chunklist can move */
	if(xl_start) {
		/* calculate free space in front of the XLchunklist. */
		/* has to be whole mbs of free space */
		/* if so, we can move the XL chunks.  Move them all back
		 * by the new free space. */
		/* this compacts very well, but the XL chunks can be moved
		 * multiple times; worst case for every mb freed a huge sized
		 * xlchunklist gets moved. */
		/* free space must be, since aligned and coagulated, in
		 * chunks of a whole MB */
		udb_void at = xl_start;
		uint64_t m = 0;
		while(*((uint8_t*)UDB_REL(base, at-1))==UDB_ALLOC_CHUNKS_MAX){
			udb_void chunk = at - UDB_ALLOC_CHUNK_SIZE;
			if(UDB_CHUNK(chunk)->type != udb_chunk_type_free)
				break;
			assert(UDB_CHUNK(chunk)->exp==UDB_ALLOC_CHUNKS_MAX);
			m += UDB_ALLOC_CHUNK_SIZE;
			at = chunk;
		}
		if(m != 0) {
			assert(at+m == xl_start);
			alloc->udb->glob_data->dirty_alloc = udb_dirty_compact;
			free_xl_space(base, alloc, at, m);
			move_xl_list(base, alloc, xl_start, xl_sz, m);
			alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
		}
	}

	/* if enough free, shrink the file; re-mmap */
	if(enough_free(alloc)) {
		uint64_t nsize = alloc->disk->nextgrow;
		udb_base_shrink(alloc->udb, nsize);
		if(!udb_base_remap(alloc->udb, alloc, nsize))
			return 0;
	}
	return 1;
}

int
udb_compact(udb_base* udb)
{
	if(!udb) return 1;
	if(!udb->useful_compact) return 1;
	DEBUG(DEBUG_DBACCESS, 1, (LOG_INFO, "Compacting database..."));
	return udb_alloc_compact(udb->base, udb->alloc);
}

void udb_compact_inhibited(udb_base* udb, int inhibit)
{
	if(!udb) return;
	udb->inhibit_compact = inhibit;
}

#ifdef UDB_CHECK
/** check that rptrs are really zero before free */
void udb_check_rptr_zero(void* base, udb_rel_ptr* p, void* arg)
{
	(void)base;
	(void)arg;
	assert(p->data == 0);
}
#endif /* UDB_CHECK */

/** free XL chunk as multiples of CHUNK_SIZE free segments */
static void
udb_free_xl(void* base, udb_alloc* alloc, udb_void f, udb_xl_chunk_d* fp,
	size_t sz)
{
	uint64_t xlsz = fp->size;
	uint64_t c;
	/* lightweight check for buffer overflow in xl data */
	assert(*((uint64_t*)(UDB_REL(base, f+xlsz-sizeof(uint64_t)*2)))==xlsz);
	assert(*((uint8_t*)(UDB_REL(base, f+xlsz-1))) == UDB_EXP_XL);
	assert( (xlsz & (UDB_ALLOC_CHUNK_SIZE-1)) == 0 ); /* whole mbs */
	assert( (f & (UDB_ALLOC_CHUNK_SIZE-1)) == 0 ); /* aligned */
#ifdef UDB_CHECK
	/* check that relptrs in this chunk have been zeroed */
	(*alloc->udb->walkfunc)(base, alloc->udb->walkarg, fp->type,
		UDB_REL(base, f+sizeof(udb_xl_chunk_d)), xlsz,
		&udb_check_rptr_zero, NULL);
#endif
	alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;
	/* update stats */
	alloc->disk->stat_data -= sz;
	alloc->disk->stat_alloc -= xlsz;
	alloc->disk->stat_free += xlsz;
	/* walk in reverse, so the front blocks go first on the list */
	c = f + xlsz - UDB_ALLOC_CHUNK_SIZE;
	/* because of header and alignment we know f >= UDB_ALLOC_CHUNK_SIZE*/
	assert(f >= UDB_ALLOC_CHUNK_SIZE);
	while(c >= f) {
		/* free a block of CHUNK_SIZE (1 Mb) */
		udb_alloc_push_fl(base, alloc, c, UDB_ALLOC_CHUNKS_MAX);
		c -= UDB_ALLOC_CHUNK_SIZE;
	}
	alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
}

int udb_alloc_free(udb_alloc* alloc, udb_void r, size_t sz)
{
	void* base;
	/* lookup chunk ptr */
	udb_void f;
	udb_chunk_d* fp;
	uint64_t esz;
	int exp;
	udb_void other;
	int coagulated = 0;
	if(!r)
		return 1; /* free(NULL) does nothing */

	/* lookup size of chunk */
	base = alloc->udb->base;
	/* fails for XL blocks */
	f = chunk_from_dataptr(r);
	fp = UDB_CHUNK(f);
	assert(fp->type != udb_chunk_type_free);

	/* see if it has a ptrlist, if so: trouble, the list is not properly
	 * cleaned up. (although you can imagine a wholesale delete where
	 * it does not matter) */
	assert(fp->ptrlist == 0);

	/* set ptrlist to 0 to stop relptr from using it, robustness. */
	fp->ptrlist = 0;

	if(fp->exp == UDB_EXP_XL) {
		udb_free_xl(base, alloc, f, (udb_xl_chunk_d*)fp, sz);
		/* compact */
		if(alloc->udb->inhibit_compact) {
			alloc->udb->useful_compact = 1;
			return 1;
		}
		return udb_alloc_compact(base, alloc);
	}
	/* it is a regular chunk of 2**exp size */
	exp = (int)fp->exp;
	esz = (uint64_t)1<<exp;
	/* light check for e.g. buffer overflow of the data */
	assert(sz < esz);
	assert(chunk_get_last(base, f, exp) == (uint8_t)exp);
#ifdef UDB_CHECK
	/* check that relptrs in this chunk have been zeroed */
	(*alloc->udb->walkfunc)(base, alloc->udb->walkarg, fp->type,
		UDB_REL(base, r), esz, &udb_check_rptr_zero, NULL);
#endif

	/* update the stats */
	alloc->udb->glob_data->dirty_alloc = udb_dirty_fl;
	alloc->disk->stat_data -= sz;
	alloc->disk->stat_free += esz;
	alloc->disk->stat_alloc -= esz;

	/* if it can be merged with other free chunks, do so */
	while( (other=coagulate_possible(base, alloc, f, exp, esz)) ) {
		coagulated = 1;
		/* unlink that other chunk and expand it (it has same size) */
		udb_alloc_unlink_fl(base, alloc, other, exp);
		/* merge up */
		if(other < f)
			f = other;
		exp++;
		esz <<= 1;
	}
	if(coagulated) {
		/* put big free chunk into freelist, and init it */
		udb_alloc_push_fl(base, alloc, f, exp);
	} else {
		/* we do not need to touch the last-exp-byte, which may save
		 * a reference to that page of memory */
		fp->type = udb_chunk_type_free;
		fp->flags = 0;
		udb_alloc_push_fl_noinit(base, alloc, f, exp);
	}
	alloc->udb->glob_data->dirty_alloc = udb_dirty_clean;
	/* compact */
	if(alloc->udb->inhibit_compact) {
		alloc->udb->useful_compact = 1;
		return 1;
	}
	return udb_alloc_compact(base, alloc);
}

udb_void udb_alloc_init(udb_alloc* alloc, void* d, size_t sz)
{
	/* could be faster maybe, if grown? */
	udb_void r = udb_alloc_space(alloc, sz);
	if(!r) return r;
	memcpy(UDB_REL(alloc->udb->base, r), d, sz);
	return r;
}

udb_void udb_alloc_realloc(udb_alloc* alloc, udb_void r, size_t osz, size_t sz)
{
	void* base = alloc->udb->base;
	udb_void c, n, newd;
	udb_chunk_d* cp, *np;
	uint64_t avail;
	uint8_t cp_type;
	/* emulate some posix realloc stuff */
	if(r == 0)
		return udb_alloc_space(alloc, sz);
	if(sz == 0) {
		if(!udb_alloc_free(alloc, r, osz))
			log_msg(LOG_ERR, "udb_alloc_realloc: free failed");
		return 0;
	}
	c = chunk_from_dataptr(r);
	cp = UDB_CHUNK(c);
	cp_type = cp->type;
	if(cp->exp == UDB_EXP_XL) {
		avail = UDB_XL_CHUNK(c)->size - sizeof(udb_xl_chunk_d)
			- sizeof(uint64_t)*2;
	} else {
		avail = ((uint64_t)1<<cp->exp) - sizeof(udb_chunk_d) - 1;
	}
	if(sz <= avail)
		return r;
	/* reallocate it, and copy */
	newd = udb_alloc_space(alloc, sz);
	if(!newd) return 0;
	/* re-base after alloc, since re-mmap may have happened */
	base = alloc->udb->base;
	cp = NULL; /* may be invalid now, robustness */
	n = chunk_from_dataptr(newd);
	np = UDB_CHUNK(n);
	np->type = cp_type;
	memcpy(UDB_REL(base, newd), UDB_REL(base, r), osz);
	/* fixup ptrs */
	chunk_fix_ptrs(base, alloc->udb, np, newd, osz, r);

	if(!udb_alloc_free(alloc, r, osz))
		log_msg(LOG_ERR, "udb_alloc_realloc: free failed");
	return newd;
}

int udb_alloc_grow(udb_alloc* alloc, size_t sz, size_t num)
{
	const uint64_t mb = 1024*1024;
	int exp = udb_alloc_exp_needed(sz);
	uint64_t esz;
	uint64_t want;
	if(exp == UDB_EXP_XL)
		esz = (sz&(mb-1))+mb;
	else	esz = (uint64_t)1<<exp;
	/* we need grow_end_calc to take into account alignment */
	want = grow_end_calc(alloc, exp) + esz*(num-1);
	assert(want >= alloc->udb->base_size);
	if(!udb_base_grow_and_remap(alloc->udb, want)) {
		log_msg(LOG_ERR, "failed to grow the specified amount");
		return 0;
	}
	return 1;
}

void udb_alloc_set_type(udb_alloc* alloc, udb_void r, udb_chunk_type tp)
{
	void* base = alloc->udb->base;
	udb_void f = chunk_from_dataptr(r);
	udb_chunk_d* fp = UDB_CHUNK(f);
	/* not the 'free' type, that must be set by allocation routines */
	assert(fp->type != udb_chunk_type_free);
	assert(tp != udb_chunk_type_free);
	fp->type = tp;
}

int udb_valid_offset(udb_base* udb, udb_void to, size_t destsize)
{
	/* pointers are not valid before the header-size or after the
	 * used-region of the mmap */
	return ( (to+destsize) <= udb->base_size &&
		to >= (udb->glob_data->hsize-2*sizeof(udb_rel_ptr)) &&
		(to+destsize) <= udb->alloc->disk->nextgrow);
}

int udb_valid_dataptr(udb_base* udb, udb_void to)
{
	void* base = udb->base;
	udb_void ch;
	int exp;
	uint64_t esz;
	/* our data chunks are aligned and at least 8 bytes */
	if(!udb_valid_offset(udb, to, sizeof(uint64_t)))
		return 0;
	/* get the chunk pointer */
	ch = chunk_from_dataptr(to);
	if(!udb_valid_offset(udb, ch, sizeof(udb_chunk_d)))
		return 0;
	/* check its size */
	exp = UDB_CHUNK(ch)->exp;
	if(exp == UDB_EXP_XL) {
		/* check XL chunk */
		uint64_t xlsz;
		if(!udb_valid_offset(udb, ch, sizeof(udb_xl_chunk_d)))
			return 0;
		xlsz = UDB_XL_CHUNK(ch)->size;
		if(!udb_valid_offset(udb, ch+xlsz-1, 1))
			return 0;
		if(*((uint8_t*)UDB_REL(base, ch+xlsz-1)) != UDB_EXP_XL)
			return 0;
		if(*((uint64_t*)UDB_REL(base, ch+xlsz-sizeof(uint64_t)*2))
			!= xlsz)
			return 0;
		return 1;
	}
	/* check if regular chunk has matching end byte */
	if(exp < UDB_ALLOC_CHUNK_MINEXP || exp > UDB_ALLOC_CHUNKS_MAX)
		return 0; /* cannot be a valid chunk */
	esz = 1<<exp;
	if(!udb_valid_offset(udb, ch+esz-1, 1))
		return 0;
	if(*((uint8_t*)UDB_REL(base, ch+esz-1)) != exp)
		return 0;
	return 1;
}

int udb_valid_rptr(udb_base* udb, udb_void rptr, udb_void to)
{
	void* base = udb->base;
	udb_void p;
	if(!udb_valid_offset(udb, rptr, sizeof(udb_rel_ptr)))
		return 0;
	if(!udb_valid_dataptr(udb, to))
		return 0;
	p = UDB_CHUNK(chunk_from_dataptr(to))->ptrlist;
	while(p) {
		if(!udb_valid_offset(udb, p, sizeof(udb_rel_ptr)))
			return 0;
		if(p == rptr)
			return 1;
		p = UDB_REL_PTR(p)->next;
	}
	return 0;
}

void udb_rel_ptr_init(udb_rel_ptr* ptr)
{
	memset(ptr, 0, sizeof(*ptr));
}

void udb_rel_ptr_unlink(void* base, udb_rel_ptr* ptr)
{
	if(!ptr->data)
		return;
	if(ptr->prev) {
		UDB_REL_PTR(ptr->prev)->next = ptr->next;
	} else {
		UDB_CHUNK(chunk_from_dataptr(ptr->data))->ptrlist = ptr->next;
	}
	if(ptr->next) {
		UDB_REL_PTR(ptr->next)->prev = ptr->prev;
	}
}

void udb_rel_ptr_link(void* base, udb_rel_ptr* ptr, udb_void to)
{
	udb_chunk_d* chunk = UDB_CHUNK(chunk_from_dataptr(to));
	ptr->prev = 0;
	ptr->next = chunk->ptrlist;
	if(ptr->next)
		UDB_REL_PTR(ptr->next)->prev = UDB_SYSTOREL(base, ptr);
	chunk->ptrlist = UDB_SYSTOREL(base, ptr);
	ptr->data = to;
}

void udb_rel_ptr_set(void* base, udb_rel_ptr* ptr, udb_void to)
{
	assert(to == 0 || to > 64);
	udb_rel_ptr_unlink(base, ptr);
	if(to)
		udb_rel_ptr_link(base, ptr, to);
	else	ptr->data = to;
}

void udb_rel_ptr_edit(void* base, udb_void list, udb_void to)
{
	udb_void p = list;
	while(p) {
		UDB_REL_PTR(p)->data = to;
		p = UDB_REL_PTR(p)->next;
	}
}

#ifdef UDB_CHECK
/** check that all pointers are validly chained */
static void
udb_check_ptrs_valid(udb_base* udb)
{
	size_t i;
	udb_ptr* p, *prev;
	for(i=0; i<udb->ram_size; i++) {
		prev = NULL;
		for(p=udb->ram_hash[i]; p; p=p->next) {
			assert(p->prev == prev);
			assert((size_t)(chunk_hash_ptr(p->data)&udb->ram_mask)
				== i);
			assert(p->base == &udb->base);
			prev = p;
		}
	}
}
#endif /* UDB_CHECK */

void udb_ptr_init(udb_ptr* ptr, udb_base* udb)
{
#ifdef UDB_CHECK
	udb_check_ptrs_valid(udb); /* previous ptrs have been unlinked */
#endif
	memset(ptr, 0, sizeof(*ptr));
	ptr->base = &udb->base;
}

void udb_ptr_set(udb_ptr* ptr, udb_base* udb, udb_void newval)
{
	assert(newval == 0 || newval > 64);
	if(ptr->data)
		udb_base_unlink_ptr(udb, ptr);
	ptr->data = newval;
	if(newval)
		udb_base_link_ptr(udb, ptr);
}

int udb_ptr_alloc_space(udb_ptr* ptr, udb_base* udb, udb_chunk_type type,
	size_t sz)
{
	udb_void r;
	r = udb_alloc_space(udb->alloc, sz);
	if(!r) return 0;
	udb_alloc_set_type(udb->alloc, r, type);
	udb_ptr_init(ptr, udb);
	udb_ptr_set(ptr, udb, r);
	return 1;
}

void udb_ptr_free_space(udb_ptr* ptr, udb_base* udb, size_t sz)
{
	if(ptr->data) {
		udb_void d = ptr->data;
		udb_ptr_set(ptr, udb, 0);
		udb_alloc_free(udb->alloc, d, sz);
	}
}

udb_chunk_type udb_ptr_get_type(udb_ptr* ptr)
{
	udb_void f;
	if(!ptr || ptr->data == 0) return udb_chunk_type_internal; /* something bad*/
	f = chunk_from_dataptr(ptr->data);
	return ((udb_chunk_d*)UDB_REL(*ptr->base, f))->type;
}
