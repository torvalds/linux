#ifndef __FS_CEPH_BUFFER_H
#define __FS_CEPH_BUFFER_H

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/uio.h>

/*
 * a simple reference counted buffer.
 *
 * use kmalloc for small sizes (<= one page), vmalloc for larger
 * sizes.
 */
struct ceph_buffer {
	atomic_t nref;
	struct kvec vec;
	size_t alloc_len;
	bool is_vmalloc;
};

struct ceph_buffer *ceph_buffer_new(gfp_t gfp);
int ceph_buffer_alloc(struct ceph_buffer *b, int len, gfp_t gfp);

static inline struct ceph_buffer *ceph_buffer_get(struct ceph_buffer *b)
{
	atomic_inc(&b->nref);
	return b;
}

static inline void ceph_buffer_put(struct ceph_buffer *b)
{
	if (b && atomic_dec_and_test(&b->nref)) {
		if (b->vec.iov_base) {
			if (b->is_vmalloc)
				vfree(b->vec.iov_base);
			else
				kfree(b->vec.iov_base);
		}
		kfree(b);
	}
}

static inline struct ceph_buffer *ceph_buffer_new_alloc(int len, gfp_t gfp)
{
	struct ceph_buffer *b = ceph_buffer_new(gfp);

	if (b && ceph_buffer_alloc(b, len, gfp) < 0) {
		ceph_buffer_put(b);
		b = NULL;
	}
	return b;
}

#endif
