/*
 * util/storage/lruhash.c - hashtable, hash function, LRU keeping.
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
 * This file contains a hashtable with LRU keeping of entries.
 *
 */

#include "config.h"
#include "util/storage/lruhash.h"
#include "util/fptr_wlist.h"

void
bin_init(struct lruhash_bin* array, size_t size)
{
	size_t i;
#ifdef THREADS_DISABLED
	(void)array;
#endif
	for(i=0; i<size; i++) {
		lock_quick_init(&array[i].lock);
		lock_protect(&array[i].lock, &array[i], 
			sizeof(struct lruhash_bin));
	}
}

struct lruhash* 
lruhash_create(size_t start_size, size_t maxmem,
	lruhash_sizefunc_type sizefunc, lruhash_compfunc_type compfunc,
	lruhash_delkeyfunc_type delkeyfunc,
	lruhash_deldatafunc_type deldatafunc, void* arg)
{
	struct lruhash* table = (struct lruhash*)calloc(1, 
		sizeof(struct lruhash));
	if(!table)
		return NULL;
	lock_quick_init(&table->lock);
	table->sizefunc = sizefunc;
	table->compfunc = compfunc;
	table->delkeyfunc = delkeyfunc;
	table->deldatafunc = deldatafunc;
	table->cb_arg = arg;
	table->size = start_size;
	table->size_mask = (int)(start_size-1);
	table->lru_start = NULL;
	table->lru_end = NULL;
	table->num = 0;
	table->space_used = 0;
	table->space_max = maxmem;
	table->max_collisions = 0;
	table->array = calloc(table->size, sizeof(struct lruhash_bin));
	if(!table->array) {
		lock_quick_destroy(&table->lock);
		free(table);
		return NULL;
	}
	bin_init(table->array, table->size);
	lock_protect(&table->lock, table, sizeof(*table));
	lock_protect(&table->lock, table->array, 
		table->size*sizeof(struct lruhash_bin));
	return table;
}

void 
bin_delete(struct lruhash* table, struct lruhash_bin* bin)
{
	struct lruhash_entry* p, *np;
	void *d;
	if(!bin)
		return;
	lock_quick_destroy(&bin->lock);
	p = bin->overflow_list;
	bin->overflow_list = NULL;
	while(p) {
		np = p->overflow_next;
		d = p->data;
		(*table->delkeyfunc)(p->key, table->cb_arg);
		(*table->deldatafunc)(d, table->cb_arg);
		p = np;
	}
}

void 
bin_split(struct lruhash* table, struct lruhash_bin* newa, 
	int newmask)
{
	size_t i;
	struct lruhash_entry *p, *np;
	struct lruhash_bin* newbin;
	/* move entries to new table. Notice that since hash x is mapped to
	 * bin x & mask, and new mask uses one more bit, so all entries in
	 * one bin will go into the old bin or bin | newbit */
#ifndef THREADS_DISABLED
	int newbit = newmask - table->size_mask;
#endif
	/* so, really, this task could also be threaded, per bin. */
	/* LRU list is not changed */
	for(i=0; i<table->size; i++)
	{
		lock_quick_lock(&table->array[i].lock);
		p = table->array[i].overflow_list;
		/* lock both destination bins */
		lock_quick_lock(&newa[i].lock);
		lock_quick_lock(&newa[newbit|i].lock);
		while(p) {
			np = p->overflow_next;
			/* link into correct new bin */
			newbin = &newa[p->hash & newmask];
			p->overflow_next = newbin->overflow_list;
			newbin->overflow_list = p;
			p=np;
		}
		lock_quick_unlock(&newa[i].lock);
		lock_quick_unlock(&newa[newbit|i].lock);
		lock_quick_unlock(&table->array[i].lock);
	}
}

void 
lruhash_delete(struct lruhash* table)
{
	size_t i;
	if(!table)
		return;
	/* delete lock on hashtable to force check its OK */
	lock_quick_destroy(&table->lock);
	for(i=0; i<table->size; i++)
		bin_delete(table, &table->array[i]);
	free(table->array);
	free(table);
}

void 
bin_overflow_remove(struct lruhash_bin* bin, struct lruhash_entry* entry)
{
	struct lruhash_entry* p = bin->overflow_list;
	struct lruhash_entry** prevp = &bin->overflow_list;
	while(p) {
		if(p == entry) {
			*prevp = p->overflow_next;
			return;
		}
		prevp = &p->overflow_next;
		p = p->overflow_next;
	}
}

void 
reclaim_space(struct lruhash* table, struct lruhash_entry** list)
{
	struct lruhash_entry* d;
	struct lruhash_bin* bin;
	log_assert(table);
	/* does not delete MRU entry, so table will not be empty. */
	while(table->num > 1 && table->space_used > table->space_max) {
		/* notice that since we hold the hashtable lock, nobody
		   can change the lru chain. So it cannot be deleted underneath
		   us. We still need the hashbin and entry write lock to make 
		   sure we flush all users away from the entry. 
		   which is unlikely, since it is LRU, if someone got a rdlock
		   it would be moved to front, but to be sure. */
		d = table->lru_end;
		/* specialised, delete from end of double linked list,
		   and we know num>1, so there is a previous lru entry. */
		log_assert(d && d->lru_prev);
		table->lru_end = d->lru_prev;
		d->lru_prev->lru_next = NULL;
		/* schedule entry for deletion */
		bin = &table->array[d->hash & table->size_mask];
		table->num --;
		lock_quick_lock(&bin->lock);
		bin_overflow_remove(bin, d);
		d->overflow_next = *list;
		*list = d;
		lock_rw_wrlock(&d->lock);
		table->space_used -= table->sizefunc(d->key, d->data);
		if(table->markdelfunc)
			(*table->markdelfunc)(d->key);
		lock_rw_unlock(&d->lock);
		lock_quick_unlock(&bin->lock);
	}
}

struct lruhash_entry* 
bin_find_entry(struct lruhash* table, 
	struct lruhash_bin* bin, hashvalue_type hash, void* key, size_t* collisions)
{
	size_t c = 0;
	struct lruhash_entry* p = bin->overflow_list;
	while(p) {
		if(p->hash == hash && table->compfunc(p->key, key) == 0)
			break;
		c++;
		p = p->overflow_next;
	}
	if (collisions != NULL)
		*collisions = c;
	return p;
}

void 
table_grow(struct lruhash* table)
{
	struct lruhash_bin* newa;
	int newmask;
	size_t i;
	if(table->size_mask == (int)(((size_t)-1)>>1)) {
		log_err("hash array malloc: size_t too small");
		return;
	}
	/* try to allocate new array, if not fail */
	newa = calloc(table->size*2, sizeof(struct lruhash_bin));
	if(!newa) {
		log_err("hash grow: malloc failed");
		/* continue with smaller array. Though its slower. */
		return;
	}
	bin_init(newa, table->size*2);
	newmask = (table->size_mask << 1) | 1;
	bin_split(table, newa, newmask);
	/* delete the old bins */
	lock_unprotect(&table->lock, table->array);
	for(i=0; i<table->size; i++) {
		lock_quick_destroy(&table->array[i].lock);
	}
	free(table->array);
	
	table->size *= 2;
	table->size_mask = newmask;
	table->array = newa;
	lock_protect(&table->lock, table->array, 
		table->size*sizeof(struct lruhash_bin));
	return;
}

void 
lru_front(struct lruhash* table, struct lruhash_entry* entry)
{
	entry->lru_prev = NULL;
	entry->lru_next = table->lru_start;
	if(!table->lru_start)
		table->lru_end = entry;
	else	table->lru_start->lru_prev = entry;
	table->lru_start = entry;
}

void 
lru_remove(struct lruhash* table, struct lruhash_entry* entry)
{
	if(entry->lru_prev)
		entry->lru_prev->lru_next = entry->lru_next;
	else	table->lru_start = entry->lru_next;
	if(entry->lru_next)
		entry->lru_next->lru_prev = entry->lru_prev;
	else	table->lru_end = entry->lru_prev;
}

void 
lru_touch(struct lruhash* table, struct lruhash_entry* entry)
{
	log_assert(table && entry);
	if(entry == table->lru_start)
		return; /* nothing to do */
	/* remove from current lru position */
	lru_remove(table, entry);
	/* add at front */
	lru_front(table, entry);
}

void 
lruhash_insert(struct lruhash* table, hashvalue_type hash,
        struct lruhash_entry* entry, void* data, void* cb_arg)
{
	struct lruhash_bin* bin;
	struct lruhash_entry* found, *reclaimlist=NULL;
	size_t need_size;
	size_t collisions;
	fptr_ok(fptr_whitelist_hash_sizefunc(table->sizefunc));
	fptr_ok(fptr_whitelist_hash_delkeyfunc(table->delkeyfunc));
	fptr_ok(fptr_whitelist_hash_deldatafunc(table->deldatafunc));
	fptr_ok(fptr_whitelist_hash_compfunc(table->compfunc));
	fptr_ok(fptr_whitelist_hash_markdelfunc(table->markdelfunc));
	need_size = table->sizefunc(entry->key, data);
	if(cb_arg == NULL) cb_arg = table->cb_arg;

	/* find bin */
	lock_quick_lock(&table->lock);
	bin = &table->array[hash & table->size_mask];
	lock_quick_lock(&bin->lock);

	/* see if entry exists already */
	if(!(found=bin_find_entry(table, bin, hash, entry->key, &collisions))) {
		/* if not: add to bin */
		entry->overflow_next = bin->overflow_list;
		bin->overflow_list = entry;
		lru_front(table, entry);
		table->num++;
		if (table->max_collisions < collisions)
			table->max_collisions = collisions;
		table->space_used += need_size;
	} else {
		/* if so: update data - needs a writelock */
		table->space_used += need_size -
			(*table->sizefunc)(found->key, found->data);
		(*table->delkeyfunc)(entry->key, cb_arg);
		lru_touch(table, found);
		lock_rw_wrlock(&found->lock);
		(*table->deldatafunc)(found->data, cb_arg);
		found->data = data;
		lock_rw_unlock(&found->lock);
	}
	lock_quick_unlock(&bin->lock);
	if(table->space_used > table->space_max)
		reclaim_space(table, &reclaimlist);
	if(table->num >= table->size)
		table_grow(table);
	lock_quick_unlock(&table->lock);

	/* finish reclaim if any (outside of critical region) */
	while(reclaimlist) {
		struct lruhash_entry* n = reclaimlist->overflow_next;
		void* d = reclaimlist->data;
		(*table->delkeyfunc)(reclaimlist->key, cb_arg);
		(*table->deldatafunc)(d, cb_arg);
		reclaimlist = n;
	}
}

struct lruhash_entry* 
lruhash_lookup(struct lruhash* table, hashvalue_type hash, void* key, int wr)
{
	struct lruhash_entry* entry;
	struct lruhash_bin* bin;
	fptr_ok(fptr_whitelist_hash_compfunc(table->compfunc));

	lock_quick_lock(&table->lock);
	bin = &table->array[hash & table->size_mask];
	lock_quick_lock(&bin->lock);
	if((entry=bin_find_entry(table, bin, hash, key, NULL)))
		lru_touch(table, entry);
	lock_quick_unlock(&table->lock);

	if(entry) {
		if(wr)	{ lock_rw_wrlock(&entry->lock); }
		else	{ lock_rw_rdlock(&entry->lock); }
	}
	lock_quick_unlock(&bin->lock);
	return entry;
}

void 
lruhash_remove(struct lruhash* table, hashvalue_type hash, void* key)
{
	struct lruhash_entry* entry;
	struct lruhash_bin* bin;
	void *d;
	fptr_ok(fptr_whitelist_hash_sizefunc(table->sizefunc));
	fptr_ok(fptr_whitelist_hash_delkeyfunc(table->delkeyfunc));
	fptr_ok(fptr_whitelist_hash_deldatafunc(table->deldatafunc));
	fptr_ok(fptr_whitelist_hash_compfunc(table->compfunc));
	fptr_ok(fptr_whitelist_hash_markdelfunc(table->markdelfunc));

	lock_quick_lock(&table->lock);
	bin = &table->array[hash & table->size_mask];
	lock_quick_lock(&bin->lock);
	if((entry=bin_find_entry(table, bin, hash, key, NULL))) {
		bin_overflow_remove(bin, entry);
		lru_remove(table, entry);
	} else {
		lock_quick_unlock(&table->lock);
		lock_quick_unlock(&bin->lock);
		return;
	}
	table->num--;
	table->space_used -= (*table->sizefunc)(entry->key, entry->data);	
	lock_rw_wrlock(&entry->lock);
	if(table->markdelfunc)
		(*table->markdelfunc)(entry->key);
	lock_rw_unlock(&entry->lock);
	lock_quick_unlock(&bin->lock);
	lock_quick_unlock(&table->lock);
	/* finish removal */
	d = entry->data;
	(*table->delkeyfunc)(entry->key, table->cb_arg);
	(*table->deldatafunc)(d, table->cb_arg);
}

/** clear bin, respecting locks, does not do space, LRU */
static void
bin_clear(struct lruhash* table, struct lruhash_bin* bin)
{
	struct lruhash_entry* p, *np;
	void *d;
	lock_quick_lock(&bin->lock);
	p = bin->overflow_list; 
	while(p) {
		lock_rw_wrlock(&p->lock);
		np = p->overflow_next;
		d = p->data;
		if(table->markdelfunc)
			(*table->markdelfunc)(p->key);
		lock_rw_unlock(&p->lock);
		(*table->delkeyfunc)(p->key, table->cb_arg);
		(*table->deldatafunc)(d, table->cb_arg);
		p = np;
	}
	bin->overflow_list = NULL;
	lock_quick_unlock(&bin->lock);
}

void
lruhash_clear(struct lruhash* table)
{
	size_t i;
	if(!table)
		return;
	fptr_ok(fptr_whitelist_hash_delkeyfunc(table->delkeyfunc));
	fptr_ok(fptr_whitelist_hash_deldatafunc(table->deldatafunc));
	fptr_ok(fptr_whitelist_hash_markdelfunc(table->markdelfunc));

	lock_quick_lock(&table->lock);
	for(i=0; i<table->size; i++) {
		bin_clear(table, &table->array[i]);
	}
	table->lru_start = NULL;
	table->lru_end = NULL;
	table->num = 0;
	table->space_used = 0;
	lock_quick_unlock(&table->lock);
}

void 
lruhash_status(struct lruhash* table, const char* id, int extended)
{
	lock_quick_lock(&table->lock);
	log_info("%s: %u entries, memory %u / %u",
		id, (unsigned)table->num, (unsigned)table->space_used,
		(unsigned)table->space_max);
	log_info("  itemsize %u, array %u, mask %d",
		(unsigned)(table->num? table->space_used/table->num : 0),
		(unsigned)table->size, table->size_mask);
	if(extended) {
		size_t i;
		int min=(int)table->size*2, max=-2;
		for(i=0; i<table->size; i++) {
			int here = 0;
			struct lruhash_entry *en;
			lock_quick_lock(&table->array[i].lock);
			en = table->array[i].overflow_list;
			while(en) {
				here ++;
				en = en->overflow_next;
			}
			lock_quick_unlock(&table->array[i].lock);
			if(extended >= 2)
				log_info("bin[%d] %d", (int)i, here);
			if(here > max) max = here;
			if(here < min) min = here;
		}
		log_info("  bin min %d, avg %.2lf, max %d", min, 
			(double)table->num/(double)table->size, max);
	}
	lock_quick_unlock(&table->lock);
}

size_t
lruhash_get_mem(struct lruhash* table)
{
	size_t s;
	lock_quick_lock(&table->lock);
	s = sizeof(struct lruhash) + table->space_used;
#ifdef USE_THREAD_DEBUG
	if(table->size != 0) {
		size_t i;
		for(i=0; i<table->size; i++)
			s += sizeof(struct lruhash_bin) + 
				lock_get_mem(&table->array[i].lock);
	}
#else /* no THREAD_DEBUG */
	if(table->size != 0)
		s += (table->size)*(sizeof(struct lruhash_bin) + 
			lock_get_mem(&table->array[0].lock));
#endif
	lock_quick_unlock(&table->lock);
	s += lock_get_mem(&table->lock);
	return s;
}

void 
lruhash_setmarkdel(struct lruhash* table, lruhash_markdelfunc_type md)
{
	lock_quick_lock(&table->lock);
	table->markdelfunc = md;
	lock_quick_unlock(&table->lock);
}

void
lruhash_update_space_used(struct lruhash* table, void* cb_arg, int diff_size)
{
	struct lruhash_entry *reclaimlist = NULL;

	fptr_ok(fptr_whitelist_hash_sizefunc(table->sizefunc));
	fptr_ok(fptr_whitelist_hash_delkeyfunc(table->delkeyfunc));
	fptr_ok(fptr_whitelist_hash_deldatafunc(table->deldatafunc));
	fptr_ok(fptr_whitelist_hash_markdelfunc(table->markdelfunc));

	if(cb_arg == NULL) cb_arg = table->cb_arg;

	/* update space used */
	lock_quick_lock(&table->lock);

	if((int)table->space_used + diff_size < 0)
		table->space_used = 0;
	else table->space_used = (size_t)((int)table->space_used + diff_size);

	if(table->space_used > table->space_max)
		reclaim_space(table, &reclaimlist);

	lock_quick_unlock(&table->lock);

	/* finish reclaim if any (outside of critical region) */
	while(reclaimlist) {
		struct lruhash_entry* n = reclaimlist->overflow_next;
		void* d = reclaimlist->data;
		(*table->delkeyfunc)(reclaimlist->key, cb_arg);
		(*table->deldatafunc)(d, cb_arg);
		reclaimlist = n;
	}
}

void lruhash_update_space_max(struct lruhash* table, void* cb_arg, size_t max)
{
	struct lruhash_entry *reclaimlist = NULL;

	fptr_ok(fptr_whitelist_hash_sizefunc(table->sizefunc));
	fptr_ok(fptr_whitelist_hash_delkeyfunc(table->delkeyfunc));
	fptr_ok(fptr_whitelist_hash_deldatafunc(table->deldatafunc));
	fptr_ok(fptr_whitelist_hash_markdelfunc(table->markdelfunc));

	if(cb_arg == NULL) cb_arg = table->cb_arg;

	/* update space max */
	lock_quick_lock(&table->lock);
	table->space_max = max;

	if(table->space_used > table->space_max)
		reclaim_space(table, &reclaimlist);

	lock_quick_unlock(&table->lock);

	/* finish reclaim if any (outside of critical region) */
	while(reclaimlist) {
		struct lruhash_entry* n = reclaimlist->overflow_next;
		void* d = reclaimlist->data;
		(*table->delkeyfunc)(reclaimlist->key, cb_arg);
		(*table->deldatafunc)(d, cb_arg);
		reclaimlist = n;
	}
}

void 
lruhash_traverse(struct lruhash* h, int wr, 
	void (*func)(struct lruhash_entry*, void*), void* arg)
{
	size_t i;
	struct lruhash_entry* e;

	lock_quick_lock(&h->lock);
	for(i=0; i<h->size; i++) {
		lock_quick_lock(&h->array[i].lock);
		for(e = h->array[i].overflow_list; e; e = e->overflow_next) {
			if(wr) {
				lock_rw_wrlock(&e->lock);
			} else {
				lock_rw_rdlock(&e->lock);
			}
			(*func)(e, arg);
			lock_rw_unlock(&e->lock);
		}
		lock_quick_unlock(&h->array[i].lock);
	}
	lock_quick_unlock(&h->lock);
}

/*
 * Demote: the opposite of touch, move an entry to the bottom
 * of the LRU pile.
 */

void
lru_demote(struct lruhash* table, struct lruhash_entry* entry)
{
	log_assert(table && entry);
	if (entry == table->lru_end)
		return; /* nothing to do */
	/* remove from current lru position */
	lru_remove(table, entry);
	/* add at end */
	entry->lru_next = NULL;
	entry->lru_prev = table->lru_end;

	if (table->lru_end == NULL)
	{
		table->lru_start = entry;
	}
	else
	{
		table->lru_end->lru_next = entry;
	}
	table->lru_end = entry;
}

struct lruhash_entry*
lruhash_insert_or_retrieve(struct lruhash* table, hashvalue_type hash,
	struct lruhash_entry* entry, void* data, void* cb_arg)
{
	struct lruhash_bin* bin;
	struct lruhash_entry* found, *reclaimlist = NULL;
	size_t need_size;
	size_t collisions;
	fptr_ok(fptr_whitelist_hash_sizefunc(table->sizefunc));
	fptr_ok(fptr_whitelist_hash_delkeyfunc(table->delkeyfunc));
	fptr_ok(fptr_whitelist_hash_deldatafunc(table->deldatafunc));
	fptr_ok(fptr_whitelist_hash_compfunc(table->compfunc));
	fptr_ok(fptr_whitelist_hash_markdelfunc(table->markdelfunc));
	need_size = table->sizefunc(entry->key, data);
	if (cb_arg == NULL) cb_arg = table->cb_arg;

	/* find bin */
	lock_quick_lock(&table->lock);
	bin = &table->array[hash & table->size_mask];
	lock_quick_lock(&bin->lock);

	/* see if entry exists already */
	if ((found = bin_find_entry(table, bin, hash, entry->key, &collisions)) != NULL) {
		/* if so: keep the existing data - acquire a writelock */
		lock_rw_wrlock(&found->lock);
	}
	else
	{
		/* if not: add to bin */
		entry->overflow_next = bin->overflow_list;
		bin->overflow_list = entry;
		lru_front(table, entry);
		table->num++;
		if (table->max_collisions < collisions)
			table->max_collisions = collisions;
		table->space_used += need_size;
		/* return the entry that was presented, and lock it */
		found = entry;
		lock_rw_wrlock(&found->lock);
	}
	lock_quick_unlock(&bin->lock);
	if (table->space_used > table->space_max)
		reclaim_space(table, &reclaimlist);
	if (table->num >= table->size)
		table_grow(table);
	lock_quick_unlock(&table->lock);

	/* finish reclaim if any (outside of critical region) */
	while (reclaimlist) {
		struct lruhash_entry* n = reclaimlist->overflow_next;
		void* d = reclaimlist->data;
		(*table->delkeyfunc)(reclaimlist->key, cb_arg);
		(*table->deldatafunc)(d, cb_arg);
		reclaimlist = n;
	}

	/* return the entry that was selected */
	return found;
}

