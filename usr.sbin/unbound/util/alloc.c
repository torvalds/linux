/*
 * util/alloc.c - memory allocation service. 
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains memory allocation functions.
 */

#include "config.h"
#include "util/alloc.h"
#include "util/regional.h"
#include "util/data/packed_rrset.h"
#include "util/fptr_wlist.h"

/** custom size of cached regional blocks */
#define ALLOC_REG_SIZE	16384
/** number of bits for ID part of uint64, rest for number of threads. */
#define THRNUM_SHIFT	48	/* for 65k threads, 2^48 rrsets per thr. */

/** setup new special type */
static void
alloc_setup_special(alloc_special_type* t)
{
	memset(t, 0, sizeof(*t));
	lock_rw_init(&t->entry.lock);
	t->entry.key = t;
}

/** prealloc some entries in the cache. To minimize contention. 
 * Result is 1 lock per alloc_max newly created entries.
 * @param alloc: the structure to fill up.
 */
static void
prealloc_setup(struct alloc_cache* alloc)
{
	alloc_special_type* p;
	int i;
	for(i=0; i<ALLOC_SPECIAL_MAX; i++) {
		if(!(p = (alloc_special_type*)malloc(
			sizeof(alloc_special_type)))) {
			log_err("prealloc: out of memory");
			return;
		}
		alloc_setup_special(p);
		alloc_set_special_next(p, alloc->quar);
		alloc->quar = p;
		alloc->num_quar++;
	}
}

/** prealloc region blocks */
static void
prealloc_blocks(struct alloc_cache* alloc, size_t num)
{
	size_t i;
	struct regional* r;
	for(i=0; i<num; i++) {
		r = regional_create_custom(ALLOC_REG_SIZE);
		if(!r) {
			log_err("prealloc blocks: out of memory");
			return;
		}
		r->next = (char*)alloc->reg_list;
		alloc->reg_list = r;
		alloc->num_reg_blocks ++;
	}
}

void 
alloc_init(struct alloc_cache* alloc, struct alloc_cache* super,
	int thread_num)
{
	memset(alloc, 0, sizeof(*alloc));
	alloc->super = super;
	alloc->thread_num = thread_num;
	alloc->next_id = (uint64_t)thread_num; 	/* in steps, so that type */
	alloc->next_id <<= THRNUM_SHIFT; 	/* of *_id is used. */
	alloc->last_id = 1; 			/* so no 64bit constants, */
	alloc->last_id <<= THRNUM_SHIFT; 	/* or implicit 'int' ops. */
	alloc->last_id -= 1; 			/* for compiler portability. */
	alloc->last_id |= alloc->next_id;
	alloc->next_id += 1;			/* because id=0 is special. */
	alloc->max_reg_blocks = 100;
	alloc->num_reg_blocks = 0;
	alloc->reg_list = NULL;
	alloc->cleanup = NULL;
	alloc->cleanup_arg = NULL;
	if(alloc->super)
		prealloc_blocks(alloc, alloc->max_reg_blocks);
	if(!alloc->super) {
		lock_quick_init(&alloc->lock);
		lock_protect(&alloc->lock, alloc, sizeof(*alloc));
	}
}

/** free the special list */
static void
alloc_clear_special_list(struct alloc_cache* alloc)
{
	alloc_special_type* p, *np;
	/* free */
	p = alloc->quar;
	while(p) {
		np = alloc_special_next(p);
		/* deinit special type */
		lock_rw_destroy(&p->entry.lock);
		free(p);
		p = np;
	}
}

void
alloc_clear_special(struct alloc_cache* alloc)
{
	if(!alloc->super) {
		lock_quick_lock(&alloc->lock);
	}
	alloc_clear_special_list(alloc);
	alloc->quar = 0;
	alloc->num_quar = 0;
	if(!alloc->super) {
		lock_quick_unlock(&alloc->lock);
	}
}

void 
alloc_clear(struct alloc_cache* alloc)
{
	alloc_special_type* p;
	struct regional* r, *nr;
	if(!alloc)
		return;
	if(!alloc->super) {
		lock_quick_destroy(&alloc->lock);
	}
	if(alloc->super && alloc->quar) {
		/* push entire list into super */
		p = alloc->quar;
		while(alloc_special_next(p)) /* find last */
			p = alloc_special_next(p);
		lock_quick_lock(&alloc->super->lock);
		alloc_set_special_next(p, alloc->super->quar);
		alloc->super->quar = alloc->quar;
		alloc->super->num_quar += alloc->num_quar;
		lock_quick_unlock(&alloc->super->lock);
	} else {
		alloc_clear_special_list(alloc);
	}
	alloc->quar = 0;
	alloc->num_quar = 0;
	r = alloc->reg_list;
	while(r) {
		nr = (struct regional*)r->next;
		free(r);
		r = nr;
	}
	alloc->reg_list = NULL;
	alloc->num_reg_blocks = 0;
}

uint64_t
alloc_get_id(struct alloc_cache* alloc)
{
	uint64_t id = alloc->next_id++;
	if(id == alloc->last_id) {
		log_warn("rrset alloc: out of 64bit ids. Clearing cache.");
		fptr_ok(fptr_whitelist_alloc_cleanup(alloc->cleanup));
		(*alloc->cleanup)(alloc->cleanup_arg);

		/* start back at first number */   	/* like in alloc_init*/
		alloc->next_id = (uint64_t)alloc->thread_num; 	
		alloc->next_id <<= THRNUM_SHIFT; 	/* in steps for comp. */
		alloc->next_id += 1;			/* portability. */
		/* and generate new and safe id */
		id = alloc->next_id++;
	}
	return id;
}

alloc_special_type* 
alloc_special_obtain(struct alloc_cache* alloc)
{
	alloc_special_type* p;
	log_assert(alloc);
	/* see if in local cache */
	if(alloc->quar) {
		p = alloc->quar;
		alloc->quar = alloc_special_next(p);
		alloc->num_quar--;
		p->id = alloc_get_id(alloc);
		return p;
	}
	/* see if in global cache */
	if(alloc->super) {
		/* could maybe grab alloc_max/2 entries in one go,
		 * but really, isn't that just as fast as this code? */
		lock_quick_lock(&alloc->super->lock);
		if((p = alloc->super->quar)) {
			alloc->super->quar = alloc_special_next(p);
			alloc->super->num_quar--;
		}
		lock_quick_unlock(&alloc->super->lock);
		if(p) {
			p->id = alloc_get_id(alloc);
			return p;
		}
	}
	/* allocate new */
	prealloc_setup(alloc);
	if(!(p = (alloc_special_type*)malloc(sizeof(alloc_special_type)))) {
		log_err("alloc_special_obtain: out of memory");
		return NULL;
	}
	alloc_setup_special(p);
	p->id = alloc_get_id(alloc);
	return p;
}

/** push mem and some more items to the super */
static void 
pushintosuper(struct alloc_cache* alloc, alloc_special_type* mem)
{
	int i;
	alloc_special_type *p = alloc->quar;
	log_assert(p);
	log_assert(alloc && alloc->super && 
		alloc->num_quar >= ALLOC_SPECIAL_MAX);
	/* push ALLOC_SPECIAL_MAX/2 after mem */
	alloc_set_special_next(mem, alloc->quar);
	for(i=1; i<ALLOC_SPECIAL_MAX/2; i++) {
		p = alloc_special_next(p);
	}
	alloc->quar = alloc_special_next(p);
	alloc->num_quar -= ALLOC_SPECIAL_MAX/2;

	/* dump mem+list into the super quar list */
	lock_quick_lock(&alloc->super->lock);
	alloc_set_special_next(p, alloc->super->quar);
	alloc->super->quar = mem;
	alloc->super->num_quar += ALLOC_SPECIAL_MAX/2 + 1;
	lock_quick_unlock(&alloc->super->lock);
	/* so 1 lock per mem+alloc/2 deletes */
}

void 
alloc_special_release(struct alloc_cache* alloc, alloc_special_type* mem)
{
	log_assert(alloc);
	if(!mem)
		return;
	if(!alloc->super) { 
		lock_quick_lock(&alloc->lock); /* superalloc needs locking */
	}

	alloc_special_clean(mem);
	if(alloc->super && alloc->num_quar >= ALLOC_SPECIAL_MAX) {
		/* push it to the super structure */
		pushintosuper(alloc, mem);
		return;
	}

	alloc_set_special_next(mem, alloc->quar);
	alloc->quar = mem;
	alloc->num_quar++;
	if(!alloc->super) {
		lock_quick_unlock(&alloc->lock);
	}
}

void 
alloc_stats(struct alloc_cache* alloc)
{
	log_info("%salloc: %d in cache, %d blocks.", alloc->super?"":"sup",
		(int)alloc->num_quar, (int)alloc->num_reg_blocks);
}

size_t alloc_get_mem(struct alloc_cache* alloc)
{
	alloc_special_type* p;
	size_t s = sizeof(*alloc);
	if(!alloc->super) { 
		lock_quick_lock(&alloc->lock); /* superalloc needs locking */
	}
	s += sizeof(alloc_special_type) * alloc->num_quar;
	for(p = alloc->quar; p; p = alloc_special_next(p)) {
		s += lock_get_mem(&p->entry.lock);
	}
	s += alloc->num_reg_blocks * ALLOC_REG_SIZE;
	if(!alloc->super) {
		lock_quick_unlock(&alloc->lock);
	}
	return s;
}

struct regional* 
alloc_reg_obtain(struct alloc_cache* alloc)
{
	if(alloc->num_reg_blocks > 0) {
		struct regional* r = alloc->reg_list;
		alloc->reg_list = (struct regional*)r->next;
		r->next = NULL;
		alloc->num_reg_blocks--;
		return r;
	}
	return regional_create_custom(ALLOC_REG_SIZE);
}

void 
alloc_reg_release(struct alloc_cache* alloc, struct regional* r)
{
	if(alloc->num_reg_blocks >= alloc->max_reg_blocks) {
		regional_destroy(r);
		return;
	}
	if(!r) return;
	regional_free_all(r);
	log_assert(r->next == NULL);
	r->next = (char*)alloc->reg_list;
	alloc->reg_list = r;
	alloc->num_reg_blocks++;
}

void 
alloc_set_id_cleanup(struct alloc_cache* alloc, void (*cleanup)(void*),
        void* arg)
{
	alloc->cleanup = cleanup;
	alloc->cleanup_arg = arg;
}

/** global debug value to keep track of total memory mallocs */
size_t unbound_mem_alloc = 0;
/** global debug value to keep track of total memory frees */
size_t unbound_mem_freed = 0;
#ifdef UNBOUND_ALLOC_STATS
/** special value to know if the memory is being tracked */
uint64_t mem_special = (uint64_t)0xfeed43327766abcdLL;
#ifdef malloc
#undef malloc
#endif
/** malloc with stats */
void *unbound_stat_malloc(size_t size)
{
	void* res;
	if(size == 0) size = 1;
	log_assert(size <= SIZE_MAX-16);
	res = malloc(size+16);
	if(!res) return NULL;
	unbound_mem_alloc += size;
	log_info("stat %p=malloc(%u)", res+16, (unsigned)size);
	memcpy(res, &size, sizeof(size));
	memcpy(res+8, &mem_special, sizeof(mem_special));
	return res+16;
}
#ifdef calloc
#undef calloc
#endif
#ifndef INT_MAX
#define INT_MAX (((int)-1)>>1)
#endif
/** calloc with stats */
void *unbound_stat_calloc(size_t nmemb, size_t size)
{
	size_t s;
	void* res;
	if(nmemb != 0 && INT_MAX/nmemb < size)
		return NULL; /* integer overflow check */
	s = (nmemb*size==0)?(size_t)1:nmemb*size;
	log_assert(s <= SIZE_MAX-16);
	res = calloc(1, s+16);
	if(!res) return NULL;
	log_info("stat %p=calloc(%u, %u)", res+16, (unsigned)nmemb, (unsigned)size);
	unbound_mem_alloc += s;
	memcpy(res, &s, sizeof(s));
	memcpy(res+8, &mem_special, sizeof(mem_special));
	return res+16;
}
#ifdef free
#undef free
#endif
/** free with stats */
void unbound_stat_free(void *ptr)
{
	size_t s;
	if(!ptr) return;
	if(memcmp(ptr-8, &mem_special, sizeof(mem_special)) != 0) {
		free(ptr);
		return;
	}
	ptr-=16;
	memcpy(&s, ptr, sizeof(s));
	log_info("stat free(%p) size %u", ptr+16, (unsigned)s);
	memset(ptr+8, 0, 8);
	unbound_mem_freed += s;
	free(ptr);
}
#ifdef realloc
#undef realloc
#endif
/** realloc with stats */
void *unbound_stat_realloc(void *ptr, size_t size)
{
	size_t cursz;
	void* res;
	if(!ptr) return unbound_stat_malloc(size);
	if(memcmp(ptr-8, &mem_special, sizeof(mem_special)) != 0) {
		return realloc(ptr, size);
	}
	if(size==0) {
		unbound_stat_free(ptr);
		return NULL;
	}
	ptr -= 16;
	memcpy(&cursz, ptr, sizeof(cursz));
	if(cursz == size) {
		/* nothing changes */
		return ptr;
	}
	log_assert(size <= SIZE_MAX-16);
	res = malloc(size+16);
	if(!res) return NULL;
	unbound_mem_alloc += size;
	unbound_mem_freed += cursz;
	log_info("stat realloc(%p, %u) from %u", ptr+16, (unsigned)size, (unsigned)cursz);
	if(cursz > size) {
		memcpy(res+16, ptr+16, size);
	} else if(size > cursz) {
		memcpy(res+16, ptr+16, cursz);
	}
	memset(ptr+8, 0, 8);
	free(ptr);
	memcpy(res, &size, sizeof(size));
	memcpy(res+8, &mem_special, sizeof(mem_special));
	return res+16;
}
/** strdup with stats */
char *unbound_stat_strdup(const char* s)
{
	size_t len;
	char* res;
	if(!s) return NULL;
	len = strlen(s);
	res = unbound_stat_malloc(len+1);
	if(!res) return NULL;
	memmove(res, s, len+1);
	return res;
}

/** log to file where alloc was done */
void *unbound_stat_malloc_log(size_t size, const char* file, int line,
        const char* func)
{
	log_info("%s:%d %s malloc(%u)", file, line, func, (unsigned)size);
	return unbound_stat_malloc(size);
}

/** log to file where alloc was done */
void *unbound_stat_calloc_log(size_t nmemb, size_t size, const char* file,
        int line, const char* func)
{
	log_info("%s:%d %s calloc(%u, %u)", file, line, func, 
		(unsigned) nmemb, (unsigned)size);
	return unbound_stat_calloc(nmemb, size);
}

/** log to file where free was done */
void unbound_stat_free_log(void *ptr, const char* file, int line,
        const char* func)
{
	if(ptr && memcmp(ptr-8, &mem_special, sizeof(mem_special)) == 0) {
		size_t s;
		memcpy(&s, ptr-16, sizeof(s));
		log_info("%s:%d %s free(%p) size %u", 
			file, line, func, ptr, (unsigned)s);
	} else
		log_info("%s:%d %s unmatched free(%p)", file, line, func, ptr);
	unbound_stat_free(ptr);
}

/** log to file where alloc was done */
void *unbound_stat_realloc_log(void *ptr, size_t size, const char* file,
        int line, const char* func)
{
	log_info("%s:%d %s realloc(%p, %u)", file, line, func, 
		ptr, (unsigned)size);
	return unbound_stat_realloc(ptr, size);
}

/** log to file where alloc was done */
void *unbound_stat_reallocarray_log(void *ptr, size_t nmemb, size_t size,
	const char* file, int line, const char* func)
{
	log_info("%s:%d %s reallocarray(%p, %u, %u)", file, line, func,
		ptr, (unsigned)nmemb, (unsigned)size);
	return unbound_stat_realloc(ptr, nmemb*size);
}

/** log to file where strdup was done */
char *unbound_stat_strdup_log(const char *s, const char* file, int line,
	const char* func)
{
	log_info("%s:%d %s strdup size %u", file, line, func,
		(s?(unsigned)strlen(s)+1:0));
	return unbound_stat_strdup(s);
}

#endif /* UNBOUND_ALLOC_STATS */
#ifdef UNBOUND_ALLOC_LITE
#undef malloc
#undef calloc
#undef free
#undef realloc
/** length of prefix and suffix */
static size_t lite_pad = 16;
/** prefix value to check */
static char* lite_pre = "checkfront123456";
/** suffix value to check */
static char* lite_post= "checkafter123456";

void *unbound_stat_malloc_lite(size_t size, const char* file, int line,
        const char* func)
{
	/*  [prefix .. len .. actual data .. suffix] */
	void* res;
	log_assert(size <= SIZE_MAX-(lite_pad*2+sizeof(size_t)));
	res = malloc(size+lite_pad*2+sizeof(size_t));
	if(!res) return NULL;
	memmove(res, lite_pre, lite_pad);
	memmove(res+lite_pad, &size, sizeof(size_t));
	memset(res+lite_pad+sizeof(size_t), 0x1a, size); /* init the memory */
	memmove(res+lite_pad+size+sizeof(size_t), lite_post, lite_pad);
	return res+lite_pad+sizeof(size_t);
}

void *unbound_stat_calloc_lite(size_t nmemb, size_t size, const char* file,
        int line, const char* func)
{
	size_t req;
	void* res;
	if(nmemb != 0 && INT_MAX/nmemb < size)
		return NULL; /* integer overflow check */
	req = nmemb * size;
	log_assert(req <= SIZE_MAX-(lite_pad*2+sizeof(size_t)));
	res = malloc(req+lite_pad*2+sizeof(size_t));
	if(!res) return NULL;
	memmove(res, lite_pre, lite_pad);
	memmove(res+lite_pad, &req, sizeof(size_t));
	memset(res+lite_pad+sizeof(size_t), 0, req);
	memmove(res+lite_pad+req+sizeof(size_t), lite_post, lite_pad);
	return res+lite_pad+sizeof(size_t);
}

void unbound_stat_free_lite(void *ptr, const char* file, int line,
        const char* func)
{
	void* real;
	size_t orig = 0;
	if(!ptr) return;
	real = ptr-lite_pad-sizeof(size_t);
	if(memcmp(real, lite_pre, lite_pad) != 0) {
		log_err("free(): prefix failed %s:%d %s", file, line, func);
		log_hex("prefix here", real, lite_pad);
		log_hex("  should be", lite_pre, lite_pad);
		fatal_exit("alloc assertion failed");
	}
	memmove(&orig, real+lite_pad, sizeof(size_t));
	if(memcmp(real+lite_pad+orig+sizeof(size_t), lite_post, lite_pad)!=0){
		log_err("free(): suffix failed %s:%d %s", file, line, func);
		log_err("alloc size is %d", (int)orig);
		log_hex("suffix here", real+lite_pad+orig+sizeof(size_t), 
			lite_pad);
		log_hex("  should be", lite_post, lite_pad);
		fatal_exit("alloc assertion failed");
	}
	memset(real, 0xdd, orig+lite_pad*2+sizeof(size_t)); /* mark it */
	free(real);
}

void *unbound_stat_realloc_lite(void *ptr, size_t size, const char* file,
        int line, const char* func)
{
	/* always free and realloc (no growing) */
	void* real, *newa;
	size_t orig = 0;
	if(!ptr) {
		/* like malloc() */
		return unbound_stat_malloc_lite(size, file, line, func);
	}
	if(!size) {
		/* like free() */
		unbound_stat_free_lite(ptr, file, line, func);
		return NULL;
	}
	/* change allocation size and copy */
	real = ptr-lite_pad-sizeof(size_t);
	if(memcmp(real, lite_pre, lite_pad) != 0) {
		log_err("realloc(): prefix failed %s:%d %s", file, line, func);
		log_hex("prefix here", real, lite_pad);
		log_hex("  should be", lite_pre, lite_pad);
		fatal_exit("alloc assertion failed");
	}
	memmove(&orig, real+lite_pad, sizeof(size_t));
	if(memcmp(real+lite_pad+orig+sizeof(size_t), lite_post, lite_pad)!=0){
		log_err("realloc(): suffix failed %s:%d %s", file, line, func);
		log_err("alloc size is %d", (int)orig);
		log_hex("suffix here", real+lite_pad+orig+sizeof(size_t), 
			lite_pad);
		log_hex("  should be", lite_post, lite_pad);
		fatal_exit("alloc assertion failed");
	}
	/* new alloc and copy over */
	newa = unbound_stat_malloc_lite(size, file, line, func);
	if(!newa)
		return NULL;
	if(orig < size)
		memmove(newa, ptr, orig);
	else	memmove(newa, ptr, size);
	memset(real, 0xdd, orig+lite_pad*2+sizeof(size_t)); /* mark it */
	free(real);
	return newa;
}

char* unbound_strdup_lite(const char* s, const char* file, int line, 
        const char* func)
{
	/* this routine is made to make sure strdup() uses the malloc_lite */
	size_t l = strlen(s)+1;
	char* n = (char*)unbound_stat_malloc_lite(l, file, line, func);
	if(!n) return NULL;
	memmove(n, s, l);
	return n;
}

char* unbound_lite_wrapstr(char* s)
{
	char* n = unbound_strdup_lite(s, __FILE__, __LINE__, __func__);
	free(s);
	return n;
}

#undef sldns_pkt2wire
sldns_status unbound_lite_pkt2wire(uint8_t **dest, const sldns_pkt *p, 
	size_t *size)
{
	uint8_t* md = NULL;
	size_t ms = 0;
	sldns_status s = sldns_pkt2wire(&md, p, &ms);
	if(md) {
		*dest = unbound_stat_malloc_lite(ms, __FILE__, __LINE__, 
			__func__);
		*size = ms;
		if(!*dest) { free(md); return LDNS_STATUS_MEM_ERR; }
		memcpy(*dest, md, ms);
		free(md);
	} else {
		*dest = NULL;
		*size = 0;
	}
	return s;
}

#undef i2d_DSA_SIG
int unbound_lite_i2d_DSA_SIG(DSA_SIG* dsasig, unsigned char** sig)
{
	unsigned char* n = NULL;
	int r= i2d_DSA_SIG(dsasig, &n);
	if(n) {
		*sig = unbound_stat_malloc_lite((size_t)r, __FILE__, __LINE__, 
			__func__);
		if(!*sig) return -1;
		memcpy(*sig, n, (size_t)r);
		free(n);
		return r;
	}
	*sig = NULL;
	return r;
}

#endif /* UNBOUND_ALLOC_LITE */
