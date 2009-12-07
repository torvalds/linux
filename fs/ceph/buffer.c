
#include "ceph_debug.h"
#include "buffer.h"

struct ceph_buffer *ceph_buffer_new(size_t len, gfp_t gfp)
{
	struct ceph_buffer *b;

	b = kmalloc(sizeof(*b), gfp);
	if (!b)
		return NULL;

	b->vec.iov_base = kmalloc(len, gfp | __GFP_NOWARN);
	if (b->vec.iov_base) {
		b->is_vmalloc = false;
	} else {
		b->vec.iov_base = __vmalloc(len, gfp, PAGE_KERNEL);
		if (!b->vec.iov_base) {
			kfree(b);
			return NULL;
		}
		b->is_vmalloc = true;
	}

	kref_init(&b->kref);
	b->alloc_len = len;
	b->vec.iov_len = len;
	dout("buffer_new %p\n", b);
	return b;
}

void ceph_buffer_release(struct kref *kref)
{
	struct ceph_buffer *b = container_of(kref, struct ceph_buffer, kref);

	dout("buffer_release %p\n", b);
	if (b->vec.iov_base) {
		if (b->is_vmalloc)
			vfree(b->vec.iov_base);
		else
			kfree(b->vec.iov_base);
	}
	kfree(b);
}

int ceph_buffer_alloc(struct ceph_buffer *b, int len, gfp_t gfp)
{
	b->vec.iov_base = kmalloc(len, gfp | __GFP_NOWARN);
	if (b->vec.iov_base) {
		b->is_vmalloc = false;
	} else {
		b->vec.iov_base = __vmalloc(len, gfp, PAGE_KERNEL);
		b->is_vmalloc = true;
	}
	if (!b->vec.iov_base)
		return -ENOMEM;
	b->alloc_len = len;
	b->vec.iov_len = len;
	return 0;
}

