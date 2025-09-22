/* Public domain. */

#ifndef _LINUX_XARRAY_H
#define _LINUX_XARRAY_H

#include <linux/gfp.h>

#include <sys/tree.h>

#define XA_FLAGS_ALLOC		(1 << 0)
#define XA_FLAGS_ALLOC1		(1 << 1)
#define XA_FLAGS_LOCK_IRQ	(1 << 2)

/*
 * lower bits of pointer are tagged:
 * 00: pointer
 * 01: value
 * 10: internal
 */
struct xarray_entry {
	SPLAY_ENTRY(xarray_entry) entry;
	int id;
	void *ptr;
};

struct xarray {
	gfp_t		xa_flags;
	struct mutex	xa_lock;
	SPLAY_HEAD(xarray_tree, xarray_entry) xa_tree;
};

#define DEFINE_XARRAY_ALLOC(name)				\
	struct xarray name = {					\
		.xa_flags = XA_FLAGS_ALLOC,			\
		.xa_lock = MUTEX_INITIALIZER(IPL_NONE),		\
		.xa_tree = SPLAY_INITIALIZER(&name.xa_tree)	\
	}

struct xarray_range {
	uint32_t start;
	uint32_t end;
};

#define XA_LIMIT(_start, _end)	(struct xarray_range){ _start, _end }
#define xa_limit_32b		XA_LIMIT(0, UINT_MAX)

void xa_init_flags(struct xarray *, gfp_t);
void xa_destroy(struct xarray *);
int __xa_alloc(struct xarray *, u32 *, void *, struct xarray_range, gfp_t);
int __xa_alloc_cyclic(struct xarray *, u32 *, void *, struct xarray_range,
    u32 *, gfp_t);
void *__xa_load(struct xarray *, unsigned long);
void *__xa_store(struct xarray *, unsigned long, void *, gfp_t);
void *__xa_erase(struct xarray *, unsigned long);
void *xa_get_next(struct xarray *, unsigned long *);

#define xa_for_each(xa, index, entry) \
	for (index = 0; ((entry) = xa_get_next(xa, &(index))) != NULL; index++)

#define xa_lock(_xa) do {				\
		mtx_enter(&(_xa)->xa_lock);		\
	} while (0)

#define xa_unlock(_xa) do {				\
		mtx_leave(&(_xa)->xa_lock);		\
	} while (0)

#define xa_lock_irq(_xa) do {				\
		mtx_enter(&(_xa)->xa_lock);		\
	} while (0)

#define xa_unlock_irq(_xa) do {				\
		mtx_leave(&(_xa)->xa_lock);		\
	} while (0)

#define xa_lock_irqsave(_xa, _flags) do {		\
		_flags = 0;				\
		mtx_enter(&(_xa)->xa_lock);		\
	} while (0)

#define xa_unlock_irqrestore(_xa, _flags) do {		\
		(void)(_flags);				\
		mtx_leave(&(_xa)->xa_lock);		\
	} while (0)

static inline void *
xa_mk_value(unsigned long v)
{
	unsigned long r = (v << 1) | 1;
	return (void *)r;
}

static inline bool
xa_is_value(const void *e)
{
	unsigned long v = (unsigned long)e;
	return v & 1;
}

static inline unsigned long
xa_to_value(const void *e)
{
	unsigned long v = (unsigned long)e;
	return v >> 1;
}

#define XA_ERROR(x)	((struct xa_node *)(((unsigned long)x << 2) | 2))

static inline int
xa_err(const void *e)
{
	long v = (long)e;
	/* not tagged internal, not an errno */
	if ((v & 3) != 2)
		return 0;
	v >>= 2;
	if (v >= -ELAST)
		return v;
	return 0;
}

static inline bool
xa_is_err(const void *e)
{
	return xa_err(e) != 0;
}

static inline int
xa_alloc(struct xarray *xa, u32 *id, void *entry, struct xarray_range xr,
    gfp_t gfp)
{
	int r;
	mtx_enter(&xa->xa_lock);
	r = __xa_alloc(xa, id, entry, xr, gfp);
	mtx_leave(&xa->xa_lock);
	return r;
}

static inline void *
xa_load(struct xarray *xa, unsigned long index)
{
	void *r;
	r = __xa_load(xa, index);
	return r;
}


static inline void *
xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	void *r;
	mtx_enter(&xa->xa_lock);
	r = __xa_store(xa, index, entry, gfp);
	mtx_leave(&xa->xa_lock);
	return r;
}

static inline void *
xa_erase(struct xarray *xa, unsigned long index)
{
	void *r;
	mtx_enter(&xa->xa_lock);
	r = __xa_erase(xa, index);
	mtx_leave(&xa->xa_lock);
	return r;
}

static inline void *
xa_store_irq(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	void *r;
	mtx_enter(&xa->xa_lock);
	r = __xa_store(xa, index, entry, gfp);
	mtx_leave(&xa->xa_lock);
	return r;
}

static inline void *
xa_erase_irq(struct xarray *xa, unsigned long index)
{
	void *r;
	mtx_enter(&xa->xa_lock);
	r = __xa_erase(xa, index);
	mtx_leave(&xa->xa_lock);
	return r;
}

static inline bool
xa_empty(const struct xarray *xa)
{
	return SPLAY_EMPTY(&xa->xa_tree);
}

static inline void
xa_init(struct xarray *xa)
{
	xa_init_flags(xa, 0);
}

static inline int
xa_alloc_cyclic_irq(struct xarray *xa, u32 *id, void *entry,
    struct xarray_range xr, u32 *next, gfp_t gfp)    
{
	int r;
	mtx_enter(&xa->xa_lock);
	r = __xa_alloc_cyclic(xa, id, entry, xr, next, gfp);
	mtx_leave(&xa->xa_lock);
	return r;
}
#endif
