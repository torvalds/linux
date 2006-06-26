/*
 * 2002-10-18  written by Jim Houston jim.houston@ccur.com
 *	Copyright (C) 2002 by Concurrent Computer Corporation
 *	Distributed under the GNU GPL license version 2.
 *
 * Modified by George Anzinger to reuse immediately and to use
 * find bit instructions.  Also removed _irq on spinlocks.
 *
 * Small id to pointer translation service.
 *
 * It uses a radix tree like structure as a sparse array indexed
 * by the id to obtain the pointer.  The bitmap makes allocating
 * a new id quick.
 *
 * You call it to allocate an id (an int) an associate with that id a
 * pointer or what ever, we treat it as a (void *).  You can pass this
 * id to a user for him to pass back at a later time.  You then pass
 * that id to this code and it returns your pointer.

 * You can release ids at any time. When all ids are released, most of
 * the memory is returned (we keep IDR_FREE_MAX) in a local pool so we
 * don't need to go to the memory "store" during an id allocate, just
 * so you don't need to be too concerned about locking and conflicts
 * with the slab allocator.
 */

#ifndef TEST                        // to test in user space...
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#endif
#include <linux/string.h>
#include <linux/idr.h>

static kmem_cache_t *idr_layer_cache;

static struct idr_layer *alloc_layer(struct idr *idp)
{
	struct idr_layer *p;

	spin_lock(&idp->lock);
	if ((p = idp->id_free)) {
		idp->id_free = p->ary[0];
		idp->id_free_cnt--;
		p->ary[0] = NULL;
	}
	spin_unlock(&idp->lock);
	return(p);
}

/* only called when idp->lock is held */
static void __free_layer(struct idr *idp, struct idr_layer *p)
{
	p->ary[0] = idp->id_free;
	idp->id_free = p;
	idp->id_free_cnt++;
}

static void free_layer(struct idr *idp, struct idr_layer *p)
{
	/*
	 * Depends on the return element being zeroed.
	 */
	spin_lock(&idp->lock);
	__free_layer(idp, p);
	spin_unlock(&idp->lock);
}

/**
 * idr_pre_get - reserver resources for idr allocation
 * @idp:	idr handle
 * @gfp_mask:	memory allocation flags
 *
 * This function should be called prior to locking and calling the
 * following function.  It preallocates enough memory to satisfy
 * the worst possible allocation.
 *
 * If the system is REALLY out of memory this function returns 0,
 * otherwise 1.
 */
int idr_pre_get(struct idr *idp, gfp_t gfp_mask)
{
	while (idp->id_free_cnt < IDR_FREE_MAX) {
		struct idr_layer *new;
		new = kmem_cache_alloc(idr_layer_cache, gfp_mask);
		if (new == NULL)
			return (0);
		free_layer(idp, new);
	}
	return 1;
}
EXPORT_SYMBOL(idr_pre_get);

static int sub_alloc(struct idr *idp, void *ptr, int *starting_id)
{
	int n, m, sh;
	struct idr_layer *p, *new;
	struct idr_layer *pa[MAX_LEVEL];
	int l, id;
	long bm;

	id = *starting_id;
	p = idp->top;
	l = idp->layers;
	pa[l--] = NULL;
	while (1) {
		/*
		 * We run around this while until we reach the leaf node...
		 */
		n = (id >> (IDR_BITS*l)) & IDR_MASK;
		bm = ~p->bitmap;
		m = find_next_bit(&bm, IDR_SIZE, n);
		if (m == IDR_SIZE) {
			/* no space available go back to previous layer. */
			l++;
			id = (id | ((1 << (IDR_BITS * l)) - 1)) + 1;
			if (!(p = pa[l])) {
				*starting_id = id;
				return -2;
			}
			continue;
		}
		if (m != n) {
			sh = IDR_BITS*l;
			id = ((id >> sh) ^ n ^ m) << sh;
		}
		if ((id >= MAX_ID_BIT) || (id < 0))
			return -3;
		if (l == 0)
			break;
		/*
		 * Create the layer below if it is missing.
		 */
		if (!p->ary[m]) {
			if (!(new = alloc_layer(idp)))
				return -1;
			p->ary[m] = new;
			p->count++;
		}
		pa[l--] = p;
		p = p->ary[m];
	}
	/*
	 * We have reached the leaf node, plant the
	 * users pointer and return the raw id.
	 */
	p->ary[m] = (struct idr_layer *)ptr;
	__set_bit(m, &p->bitmap);
	p->count++;
	/*
	 * If this layer is full mark the bit in the layer above
	 * to show that this part of the radix tree is full.
	 * This may complete the layer above and require walking
	 * up the radix tree.
	 */
	n = id;
	while (p->bitmap == IDR_FULL) {
		if (!(p = pa[++l]))
			break;
		n = n >> IDR_BITS;
		__set_bit((n & IDR_MASK), &p->bitmap);
	}
	return(id);
}

static int idr_get_new_above_int(struct idr *idp, void *ptr, int starting_id)
{
	struct idr_layer *p, *new;
	int layers, v, id;

	id = starting_id;
build_up:
	p = idp->top;
	layers = idp->layers;
	if (unlikely(!p)) {
		if (!(p = alloc_layer(idp)))
			return -1;
		layers = 1;
	}
	/*
	 * Add a new layer to the top of the tree if the requested
	 * id is larger than the currently allocated space.
	 */
	while ((layers < (MAX_LEVEL - 1)) && (id >= (1 << (layers*IDR_BITS)))) {
		layers++;
		if (!p->count)
			continue;
		if (!(new = alloc_layer(idp))) {
			/*
			 * The allocation failed.  If we built part of
			 * the structure tear it down.
			 */
			spin_lock(&idp->lock);
			for (new = p; p && p != idp->top; new = p) {
				p = p->ary[0];
				new->ary[0] = NULL;
				new->bitmap = new->count = 0;
				__free_layer(idp, new);
			}
			spin_unlock(&idp->lock);
			return -1;
		}
		new->ary[0] = p;
		new->count = 1;
		if (p->bitmap == IDR_FULL)
			__set_bit(0, &new->bitmap);
		p = new;
	}
	idp->top = p;
	idp->layers = layers;
	v = sub_alloc(idp, ptr, &id);
	if (v == -2)
		goto build_up;
	return(v);
}

/**
 * idr_get_new_above - allocate new idr entry above or equal to a start id
 * @idp: idr handle
 * @ptr: pointer you want associated with the ide
 * @start_id: id to start search at
 * @id: pointer to the allocated handle
 *
 * This is the allocate id function.  It should be called with any
 * required locks.
 *
 * If memory is required, it will return -EAGAIN, you should unlock
 * and go back to the idr_pre_get() call.  If the idr is full, it will
 * return -ENOSPC.
 *
 * @id returns a value in the range 0 ... 0x7fffffff
 */
int idr_get_new_above(struct idr *idp, void *ptr, int starting_id, int *id)
{
	int rv;

	rv = idr_get_new_above_int(idp, ptr, starting_id);
	/*
	 * This is a cheap hack until the IDR code can be fixed to
	 * return proper error values.
	 */
	if (rv < 0) {
		if (rv == -1)
			return -EAGAIN;
		else /* Will be -3 */
			return -ENOSPC;
	}
	*id = rv;
	return 0;
}
EXPORT_SYMBOL(idr_get_new_above);

/**
 * idr_get_new - allocate new idr entry
 * @idp: idr handle
 * @ptr: pointer you want associated with the ide
 * @id: pointer to the allocated handle
 *
 * This is the allocate id function.  It should be called with any
 * required locks.
 *
 * If memory is required, it will return -EAGAIN, you should unlock
 * and go back to the idr_pre_get() call.  If the idr is full, it will
 * return -ENOSPC.
 *
 * @id returns a value in the range 0 ... 0x7fffffff
 */
int idr_get_new(struct idr *idp, void *ptr, int *id)
{
	int rv;

	rv = idr_get_new_above_int(idp, ptr, 0);
	/*
	 * This is a cheap hack until the IDR code can be fixed to
	 * return proper error values.
	 */
	if (rv < 0) {
		if (rv == -1)
			return -EAGAIN;
		else /* Will be -3 */
			return -ENOSPC;
	}
	*id = rv;
	return 0;
}
EXPORT_SYMBOL(idr_get_new);

static void idr_remove_warning(int id)
{
	printk("idr_remove called for id=%d which is not allocated.\n", id);
	dump_stack();
}

static void sub_remove(struct idr *idp, int shift, int id)
{
	struct idr_layer *p = idp->top;
	struct idr_layer **pa[MAX_LEVEL];
	struct idr_layer ***paa = &pa[0];
	int n;

	*paa = NULL;
	*++paa = &idp->top;

	while ((shift > 0) && p) {
		n = (id >> shift) & IDR_MASK;
		__clear_bit(n, &p->bitmap);
		*++paa = &p->ary[n];
		p = p->ary[n];
		shift -= IDR_BITS;
	}
	n = id & IDR_MASK;
	if (likely(p != NULL && test_bit(n, &p->bitmap))){
		__clear_bit(n, &p->bitmap);
		p->ary[n] = NULL;
		while(*paa && ! --((**paa)->count)){
			free_layer(idp, **paa);
			**paa-- = NULL;
		}
		if (!*paa)
			idp->layers = 0;
	} else
		idr_remove_warning(id);
}

/**
 * idr_remove - remove the given id and free it's slot
 * idp: idr handle
 * id: uniqueue key
 */
void idr_remove(struct idr *idp, int id)
{
	struct idr_layer *p;

	/* Mask off upper bits we don't use for the search. */
	id &= MAX_ID_MASK;

	sub_remove(idp, (idp->layers - 1) * IDR_BITS, id);
	if (idp->top && idp->top->count == 1 && (idp->layers > 1) &&
	    idp->top->ary[0]) {  // We can drop a layer

		p = idp->top->ary[0];
		idp->top->bitmap = idp->top->count = 0;
		free_layer(idp, idp->top);
		idp->top = p;
		--idp->layers;
	}
	while (idp->id_free_cnt >= IDR_FREE_MAX) {
		p = alloc_layer(idp);
		kmem_cache_free(idr_layer_cache, p);
		return;
	}
}
EXPORT_SYMBOL(idr_remove);

/**
 * idr_destroy - release all cached layers within an idr tree
 * idp: idr handle
 */
void idr_destroy(struct idr *idp)
{
	while (idp->id_free_cnt) {
		struct idr_layer *p = alloc_layer(idp);
		kmem_cache_free(idr_layer_cache, p);
	}
}
EXPORT_SYMBOL(idr_destroy);

/**
 * idr_find - return pointer for given id
 * @idp: idr handle
 * @id: lookup key
 *
 * Return the pointer given the id it has been registered with.  A %NULL
 * return indicates that @id is not valid or you passed %NULL in
 * idr_get_new().
 *
 * The caller must serialize idr_find() vs idr_get_new() and idr_remove().
 */
void *idr_find(struct idr *idp, int id)
{
	int n;
	struct idr_layer *p;

	n = idp->layers * IDR_BITS;
	p = idp->top;

	/* Mask off upper bits we don't use for the search. */
	id &= MAX_ID_MASK;

	if (id >= (1 << n))
		return NULL;

	while (n > 0 && p) {
		n -= IDR_BITS;
		p = p->ary[(id >> n) & IDR_MASK];
	}
	return((void *)p);
}
EXPORT_SYMBOL(idr_find);

static void idr_cache_ctor(void * idr_layer, kmem_cache_t *idr_layer_cache,
		unsigned long flags)
{
	memset(idr_layer, 0, sizeof(struct idr_layer));
}

static  int init_id_cache(void)
{
	if (!idr_layer_cache)
		idr_layer_cache = kmem_cache_create("idr_layer_cache",
			sizeof(struct idr_layer), 0, 0, idr_cache_ctor, NULL);
	return 0;
}

/**
 * idr_init - initialize idr handle
 * @idp:	idr handle
 *
 * This function is use to set up the handle (@idp) that you will pass
 * to the rest of the functions.
 */
void idr_init(struct idr *idp)
{
	init_id_cache();
	memset(idp, 0, sizeof(struct idr));
	spin_lock_init(&idp->lock);
}
EXPORT_SYMBOL(idr_init);
