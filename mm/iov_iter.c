#include <linux/export.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define iterate_iovec(i, n, __v, __p, skip, STEP) {	\
	size_t left;					\
	size_t wanted = n;				\
	__p = i->iov;					\
	__v.iov_len = min(n, __p->iov_len - skip);	\
	if (likely(__v.iov_len)) {			\
		__v.iov_base = __p->iov_base + skip;	\
		left = (STEP);				\
		__v.iov_len -= left;			\
		skip += __v.iov_len;			\
		n -= __v.iov_len;			\
	} else {					\
		left = 0;				\
	}						\
	while (unlikely(!left && n)) {			\
		__p++;					\
		__v.iov_len = min(n, __p->iov_len);	\
		if (unlikely(!__v.iov_len))		\
			continue;			\
		__v.iov_base = __p->iov_base;		\
		left = (STEP);				\
		__v.iov_len -= left;			\
		skip = __v.iov_len;			\
		n -= __v.iov_len;			\
	}						\
	n = wanted - n;					\
}

#define iterate_bvec(i, n, __v, __p, skip, STEP) {	\
	size_t wanted = n;				\
	__p = i->bvec;					\
	__v.bv_len = min_t(size_t, n, __p->bv_len - skip);	\
	if (likely(__v.bv_len)) {			\
		__v.bv_page = __p->bv_page;		\
		__v.bv_offset = __p->bv_offset + skip; 	\
		(void)(STEP);				\
		skip += __v.bv_len;			\
		n -= __v.bv_len;			\
	}						\
	while (unlikely(n)) {				\
		__p++;					\
		__v.bv_len = min_t(size_t, n, __p->bv_len);	\
		if (unlikely(!__v.bv_len))		\
			continue;			\
		__v.bv_page = __p->bv_page;		\
		__v.bv_offset = __p->bv_offset;		\
		(void)(STEP);				\
		skip = __v.bv_len;			\
		n -= __v.bv_len;			\
	}						\
	n = wanted;					\
}

#define iterate_all_kinds(i, n, v, I, B) {			\
	size_t skip = i->iov_offset;				\
	if (unlikely(i->type & ITER_BVEC)) {			\
		const struct bio_vec *bvec;			\
		struct bio_vec v;				\
		iterate_bvec(i, n, v, bvec, skip, (B))		\
	} else {						\
		const struct iovec *iov;			\
		struct iovec v;					\
		iterate_iovec(i, n, v, iov, skip, (I))		\
	}							\
}

#define iterate_and_advance(i, n, v, I, B) {			\
	size_t skip = i->iov_offset;				\
	if (unlikely(i->type & ITER_BVEC)) {			\
		const struct bio_vec *bvec;			\
		struct bio_vec v;				\
		iterate_bvec(i, n, v, bvec, skip, (B))		\
		if (skip == bvec->bv_len) {			\
			bvec++;					\
			skip = 0;				\
		}						\
		i->nr_segs -= bvec - i->bvec;			\
		i->bvec = bvec;					\
	} else {						\
		const struct iovec *iov;			\
		struct iovec v;					\
		iterate_iovec(i, n, v, iov, skip, (I))		\
		if (skip == iov->iov_len) {			\
			iov++;					\
			skip = 0;				\
		}						\
		i->nr_segs -= iov - i->iov;			\
		i->iov = iov;					\
	}							\
	i->count -= n;						\
	i->iov_offset = skip;					\
}

static size_t copy_to_iter_iovec(void *from, size_t bytes, struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	left = __copy_to_user(buf, from, copy);
	copy -= left;
	skip += copy;
	from += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_to_user(buf, from, copy);
		copy -= left;
		skip = copy;
		from += copy;
		bytes -= copy;
	}

	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

static size_t copy_from_iter_iovec(void *to, size_t bytes, struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	left = __copy_from_user(to, buf, copy);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_from_user(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
		bytes -= copy;
	}

	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

static size_t copy_page_to_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *from;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (!fault_in_pages_writeable(buf, copy)) {
		kaddr = kmap_atomic(page);
		from = kaddr + offset;

		/* first chunk, usually the only one */
		left = __copy_to_user_inatomic(buf, from, copy);
		copy -= left;
		skip += copy;
		from += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = __copy_to_user_inatomic(buf, from, copy);
			copy -= left;
			skip = copy;
			from += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = from - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */
	kaddr = kmap(page);
	from = kaddr + offset;
	left = __copy_to_user(buf, from, copy);
	copy -= left;
	skip += copy;
	from += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_to_user(buf, from, copy);
		copy -= left;
		skip = copy;
		from += copy;
		bytes -= copy;
	}
	kunmap(page);
done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

static size_t copy_page_from_iter_iovec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, left, wanted;
	const struct iovec *iov;
	char __user *buf;
	void *kaddr, *to;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	iov = i->iov;
	skip = i->iov_offset;
	buf = iov->iov_base + skip;
	copy = min(bytes, iov->iov_len - skip);

	if (!fault_in_pages_readable(buf, copy)) {
		kaddr = kmap_atomic(page);
		to = kaddr + offset;

		/* first chunk, usually the only one */
		left = __copy_from_user_inatomic(to, buf, copy);
		copy -= left;
		skip += copy;
		to += copy;
		bytes -= copy;

		while (unlikely(!left && bytes)) {
			iov++;
			buf = iov->iov_base;
			copy = min(bytes, iov->iov_len);
			left = __copy_from_user_inatomic(to, buf, copy);
			copy -= left;
			skip = copy;
			to += copy;
			bytes -= copy;
		}
		if (likely(!bytes)) {
			kunmap_atomic(kaddr);
			goto done;
		}
		offset = to - kaddr;
		buf += copy;
		kunmap_atomic(kaddr);
		copy = min(bytes, iov->iov_len - skip);
	}
	/* Too bad - revert to non-atomic kmap */
	kaddr = kmap(page);
	to = kaddr + offset;
	left = __copy_from_user(to, buf, copy);
	copy -= left;
	skip += copy;
	to += copy;
	bytes -= copy;
	while (unlikely(!left && bytes)) {
		iov++;
		buf = iov->iov_base;
		copy = min(bytes, iov->iov_len);
		left = __copy_from_user(to, buf, copy);
		copy -= left;
		skip = copy;
		to += copy;
		bytes -= copy;
	}
	kunmap(page);
done:
	if (skip == iov->iov_len) {
		iov++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= iov - i->iov;
	i->iov = iov;
	i->iov_offset = skip;
	return wanted - bytes;
}

/*
 * Fault in the first iovec of the given iov_iter, to a maximum length
 * of bytes. Returns 0 on success, or non-zero if the memory could not be
 * accessed (ie. because it is an invalid address).
 *
 * writev-intensive code may want this to prefault several iovecs -- that
 * would be possible (callers must not rely on the fact that _only_ the
 * first iovec will be faulted with the current implementation).
 */
int iov_iter_fault_in_readable(struct iov_iter *i, size_t bytes)
{
	if (!(i->type & ITER_BVEC)) {
		char __user *buf = i->iov->iov_base + i->iov_offset;
		bytes = min(bytes, i->iov->iov_len - i->iov_offset);
		return fault_in_pages_readable(buf, bytes);
	}
	return 0;
}
EXPORT_SYMBOL(iov_iter_fault_in_readable);

void iov_iter_init(struct iov_iter *i, int direction,
			const struct iovec *iov, unsigned long nr_segs,
			size_t count)
{
	/* It will get better.  Eventually... */
	if (segment_eq(get_fs(), KERNEL_DS))
		direction |= ITER_KVEC;
	i->type = direction;
	i->iov = iov;
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count;
}
EXPORT_SYMBOL(iov_iter_init);

static void memcpy_from_page(char *to, struct page *page, size_t offset, size_t len)
{
	char *from = kmap_atomic(page);
	memcpy(to, from + offset, len);
	kunmap_atomic(from);
}

static void memcpy_to_page(struct page *page, size_t offset, char *from, size_t len)
{
	char *to = kmap_atomic(page);
	memcpy(to + offset, from, len);
	kunmap_atomic(to);
}

static void memzero_page(struct page *page, size_t offset, size_t len)
{
	char *addr = kmap_atomic(page);
	memset(addr + offset, 0, len);
	kunmap_atomic(addr);
}

static size_t copy_to_iter_bvec(void *from, size_t bytes, struct iov_iter *i)
{
	size_t skip, copy, wanted;
	const struct bio_vec *bvec;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	bvec = i->bvec;
	skip = i->iov_offset;
	copy = min_t(size_t, bytes, bvec->bv_len - skip);

	memcpy_to_page(bvec->bv_page, skip + bvec->bv_offset, from, copy);
	skip += copy;
	from += copy;
	bytes -= copy;
	while (bytes) {
		bvec++;
		copy = min(bytes, (size_t)bvec->bv_len);
		memcpy_to_page(bvec->bv_page, bvec->bv_offset, from, copy);
		skip = copy;
		from += copy;
		bytes -= copy;
	}
	if (skip == bvec->bv_len) {
		bvec++;
		skip = 0;
	}
	i->count -= wanted - bytes;
	i->nr_segs -= bvec - i->bvec;
	i->bvec = bvec;
	i->iov_offset = skip;
	return wanted - bytes;
}

static size_t copy_from_iter_bvec(void *to, size_t bytes, struct iov_iter *i)
{
	size_t skip, copy, wanted;
	const struct bio_vec *bvec;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	bvec = i->bvec;
	skip = i->iov_offset;

	copy = min(bytes, bvec->bv_len - skip);

	memcpy_from_page(to, bvec->bv_page, bvec->bv_offset + skip, copy);

	to += copy;
	skip += copy;
	bytes -= copy;

	while (bytes) {
		bvec++;
		copy = min(bytes, (size_t)bvec->bv_len);
		memcpy_from_page(to, bvec->bv_page, bvec->bv_offset, copy);
		skip = copy;
		to += copy;
		bytes -= copy;
	}
	if (skip == bvec->bv_len) {
		bvec++;
		skip = 0;
	}
	i->count -= wanted;
	i->nr_segs -= bvec - i->bvec;
	i->bvec = bvec;
	i->iov_offset = skip;
	return wanted;
}

size_t copy_to_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (i->type & ITER_BVEC)
		return copy_to_iter_bvec(addr, bytes, i);
	else
		return copy_to_iter_iovec(addr, bytes, i);
}
EXPORT_SYMBOL(copy_to_iter);

size_t copy_from_iter(void *addr, size_t bytes, struct iov_iter *i)
{
	if (i->type & ITER_BVEC)
		return copy_from_iter_bvec(addr, bytes, i);
	else
		return copy_from_iter_iovec(addr, bytes, i);
}
EXPORT_SYMBOL(copy_from_iter);

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (i->type & (ITER_BVEC|ITER_KVEC)) {
		void *kaddr = kmap_atomic(page);
		size_t wanted = copy_to_iter(kaddr + offset, bytes, i);
		kunmap_atomic(kaddr);
		return wanted;
	} else
		return copy_page_to_iter_iovec(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_to_iter);

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (i->type & ITER_BVEC) {
		void *kaddr = kmap_atomic(page);
		size_t wanted = copy_from_iter(kaddr + offset, bytes, i);
		kunmap_atomic(kaddr);
		return wanted;
	} else
		return copy_page_from_iter_iovec(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_from_iter);

size_t iov_iter_zero(size_t bytes, struct iov_iter *i)
{
	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	iterate_and_advance(i, bytes, v,
		__clear_user(v.iov_base, v.iov_len),
		memzero_page(v.bv_page, v.bv_offset, v.bv_len)
	)

	return bytes;
}
EXPORT_SYMBOL(iov_iter_zero);

size_t iov_iter_copy_from_user_atomic(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes)
{
	char *kaddr = kmap_atomic(page), *p = kaddr + offset;
	iterate_all_kinds(i, bytes, v,
		__copy_from_user_inatomic((p += v.iov_len) - v.iov_len,
					  v.iov_base, v.iov_len),
		memcpy_from_page((p += v.bv_len) - v.bv_len, v.bv_page,
				 v.bv_offset, v.bv_len)
	)
	kunmap_atomic(kaddr);
	return bytes;
}
EXPORT_SYMBOL(iov_iter_copy_from_user_atomic);

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	iterate_and_advance(i, size, v, 0, 0)
}
EXPORT_SYMBOL(iov_iter_advance);

/*
 * Return the count of just the current iov_iter segment.
 */
size_t iov_iter_single_seg_count(const struct iov_iter *i)
{
	if (i->nr_segs == 1)
		return i->count;
	else if (i->type & ITER_BVEC)
		return min(i->count, i->bvec->bv_len - i->iov_offset);
	else
		return min(i->count, i->iov->iov_len - i->iov_offset);
}
EXPORT_SYMBOL(iov_iter_single_seg_count);

unsigned long iov_iter_alignment(const struct iov_iter *i)
{
	unsigned long res = 0;
	size_t size = i->count;

	if (!size)
		return 0;

	iterate_all_kinds(i, size, v,
		(res |= (unsigned long)v.iov_base | v.iov_len, 0),
		res |= v.bv_offset | v.bv_len
	)
	return res;
}
EXPORT_SYMBOL(iov_iter_alignment);

ssize_t iov_iter_get_pages(struct iov_iter *i,
		   struct page **pages, size_t maxsize, unsigned maxpages,
		   size_t *start)
{
	if (maxsize > i->count)
		maxsize = i->count;

	if (!maxsize)
		return 0;

	iterate_all_kinds(i, maxsize, v, ({
		unsigned long addr = (unsigned long)v.iov_base;
		size_t len = v.iov_len + (*start = addr & (PAGE_SIZE - 1));
		int n;
		int res;

		if (len > maxpages * PAGE_SIZE)
			len = maxpages * PAGE_SIZE;
		addr &= ~(PAGE_SIZE - 1);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		res = get_user_pages_fast(addr, n, (i->type & WRITE) != WRITE, pages);
		if (unlikely(res < 0))
			return res;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	0;}),({
		/* can't be more than PAGE_SIZE */
		*start = v.bv_offset;
		get_page(*pages = v.bv_page);
		return v.bv_len;
	})
	)
	return 0;
}
EXPORT_SYMBOL(iov_iter_get_pages);

static struct page **get_pages_array(size_t n)
{
	struct page **p = kmalloc(n * sizeof(struct page *), GFP_KERNEL);
	if (!p)
		p = vmalloc(n * sizeof(struct page *));
	return p;
}

ssize_t iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	struct page **p;

	if (maxsize > i->count)
		maxsize = i->count;

	if (!maxsize)
		return 0;

	iterate_all_kinds(i, maxsize, v, ({
		unsigned long addr = (unsigned long)v.iov_base;
		size_t len = v.iov_len + (*start = addr & (PAGE_SIZE - 1));
		int n;
		int res;

		addr &= ~(PAGE_SIZE - 1);
		n = DIV_ROUND_UP(len, PAGE_SIZE);
		p = get_pages_array(n);
		if (!p)
			return -ENOMEM;
		res = get_user_pages_fast(addr, n, (i->type & WRITE) != WRITE, p);
		if (unlikely(res < 0)) {
			kvfree(p);
			return res;
		}
		*pages = p;
		return (res == n ? len : res * PAGE_SIZE) - *start;
	0;}),({
		/* can't be more than PAGE_SIZE */
		*start = v.bv_offset;
		*pages = p = get_pages_array(1);
		if (!p)
			return -ENOMEM;
		get_page(*p = v.bv_page);
		return v.bv_len;
	})
	)
	return 0;
}
EXPORT_SYMBOL(iov_iter_get_pages_alloc);

int iov_iter_npages(const struct iov_iter *i, int maxpages)
{
	size_t size = i->count;
	int npages = 0;

	if (!size)
		return 0;

	iterate_all_kinds(i, size, v, ({
		unsigned long p = (unsigned long)v.iov_base;
		npages += DIV_ROUND_UP(p + v.iov_len, PAGE_SIZE)
			- p / PAGE_SIZE;
		if (npages >= maxpages)
			return maxpages;
	0;}),({
		npages++;
		if (npages >= maxpages)
			return maxpages;
	})
	)
	return npages;
}
EXPORT_SYMBOL(iov_iter_npages);
