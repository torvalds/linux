#ifndef __FS_CEPH_BUFFER_H
#define __FS_CEPH_BUFFER_H

#include <linux/kref.h>
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
	struct kref kref;
	struct kvec vec;
	size_t alloc_len;
	bool is_vmalloc;
};

struct ceph_buffer *ceph_buffer_new(gfp_t gfp);
int ceph_buffer_alloc(struct ceph_buffer *b, int len, gfp_t gfp);

static inline struct ceph_buffer *ceph_buffer_get(struct ceph_buffer *b)
{
	kref_get(&b->kref);
	return b;
}

void ceph_buffer_release(struct kref *kref);

static inline void ceph_buffer_put(struct ceph_buffer *b)
{
	if (b)
		kref_put(&b->kref, ceph_buffer_release);
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
