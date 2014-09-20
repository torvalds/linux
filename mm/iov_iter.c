#include <linux/export.h>
#include <linux/uio.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

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

static size_t __iovec_copy_from_user_inatomic(char *vaddr,
			const struct iovec *iov, size_t base, size_t bytes)
{
	size_t copied = 0, left = 0;

	while (bytes) {
		char __user *buf = iov->iov_base + base;
		int copy = min(bytes, iov->iov_len - base);

		base = 0;
		left = __copy_from_user_inatomic(vaddr, buf, copy);
		copied += copy;
		bytes -= copy;
		vaddr += copy;
		iov++;

		if (unlikely(left))
			break;
	}
	return copied - left;
}

/*
 * Copy as much as we can into the page and return the number of bytes which
 * were successfully copied.  If a fault is encountered then return the number of
 * bytes which were copied.
 */
static size_t copy_from_user_atomic_iovec(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes)
{
	char *kaddr;
	size_t copied;

	kaddr = kmap_atomic(page);
	if (likely(i->nr_segs == 1)) {
		int left;
		char __user *buf = i->iov->iov_base + i->iov_offset;
		left = __copy_from_user_inatomic(kaddr + offset, buf, bytes);
		copied = bytes - left;
	} else {
		copied = __iovec_copy_from_user_inatomic(kaddr + offset,
						i->iov, i->iov_offset, bytes);
	}
	kunmap_atomic(kaddr);

	return copied;
}

static void advance_iovec(struct iov_iter *i, size_t bytes)
{
	BUG_ON(i->count < bytes);

	if (likely(i->nr_segs == 1)) {
		i->iov_offset += bytes;
		i->count -= bytes;
	} else {
		const struct iovec *iov = i->iov;
		size_t base = i->iov_offset;
		unsigned long nr_segs = i->nr_segs;

		/*
		 * The !iov->iov_len check ensures we skip over unlikely
		 * zero-length segments (without overruning the iovec).
		 */
		while (bytes || unlikely(i->count && !iov->iov_len)) {
			int copy;

			copy = min(bytes, iov->iov_len - base);
			BUG_ON(!i->count || i->count < copy);
			i->count -= copy;
			bytes -= copy;
			base += copy;
			if (iov->iov_len == base) {
				iov++;
				nr_segs--;
				base = 0;
			}
		}
		i->iov = iov;
		i->iov_offset = base;
		i->nr_segs = nr_segs;
	}
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

static unsigned long alignment_iovec(const struct iov_iter *i)
{
	const struct iovec *iov = i->iov;
	unsigned long res;
	size_t size = i->count;
	size_t n;

	if (!size)
		return 0;

	res = (unsigned long)iov->iov_base + i->iov_offset;
	n = iov->iov_len - i->iov_offset;
	if (n >= size)
		return res | size;
	size -= n;
	res |= n;
	while (size > (++iov)->iov_len) {
		res |= (unsigned long)iov->iov_base | iov->iov_len;
		size -= iov->iov_len;
	}
	res |= (unsigned long)iov->iov_base | size;
	return res;
}

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

static ssize_t get_pages_iovec(struct iov_iter *i,
		   struct page **pages, unsigned maxpages,
		   size_t *start)
{
	size_t offset = i->iov_offset;
	const struct iovec *iov = i->iov;
	size_t len;
	unsigned long addr;
	int n;
	int res;

	len = iov->iov_len - offset;
	if (len > i->count)
		len = i->count;
	addr = (unsigned long)iov->iov_base + offset;
	len += *start = addr & (PAGE_SIZE - 1);
	if (len > maxpages * PAGE_SIZE)
		len = maxpages * PAGE_SIZE;
	addr &= ~(PAGE_SIZE - 1);
	n = (len + PAGE_SIZE - 1) / PAGE_SIZE;
	res = get_user_pages_fast(addr, n, (i->type & WRITE) != WRITE, pages);
	if (unlikely(res < 0))
		return res;
	return (res == n ? len : res * PAGE_SIZE) - *start;
}

static ssize_t get_pages_alloc_iovec(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	size_t offset = i->iov_offset;
	const struct iovec *iov = i->iov;
	size_t len;
	unsigned long addr;
	void *p;
	int n;
	int res;

	len = iov->iov_len - offset;
	if (len > i->count)
		len = i->count;
	if (len > maxsize)
		len = maxsize;
	addr = (unsigned long)iov->iov_base + offset;
	len += *start = addr & (PAGE_SIZE - 1);
	addr &= ~(PAGE_SIZE - 1);
	n = (len + PAGE_SIZE - 1) / PAGE_SIZE;
	
	p = kmalloc(n * sizeof(struct page *), GFP_KERNEL);
	if (!p)
		p = vmalloc(n * sizeof(struct page *));
	if (!p)
		return -ENOMEM;

	res = get_user_pages_fast(addr, n, (i->type & WRITE) != WRITE, p);
	if (unlikely(res < 0)) {
		kvfree(p);
		return res;
	}
	*pages = p;
	return (res == n ? len : res * PAGE_SIZE) - *start;
}

static int iov_iter_npages_iovec(const struct iov_iter *i, int maxpages)
{
	size_t offset = i->iov_offset;
	size_t size = i->count;
	const struct iovec *iov = i->iov;
	int npages = 0;
	int n;

	for (n = 0; size && n < i->nr_segs; n++, iov++) {
		unsigned long addr = (unsigned long)iov->iov_base + offset;
		size_t len = iov->iov_len - offset;
		offset = 0;
		if (unlikely(!len))	/* empty segment */
			continue;
		if (len > size)
			len = size;
		npages += (addr + len + PAGE_SIZE - 1) / PAGE_SIZE
			  - addr / PAGE_SIZE;
		if (npages >= maxpages)	/* don't bother going further */
			return maxpages;
		size -= len;
		offset = 0;
	}
	return min(npages, maxpages);
}

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

static size_t copy_page_to_iter_bvec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, wanted;
	const struct bio_vec *bvec;
	void *kaddr, *from;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	bvec = i->bvec;
	skip = i->iov_offset;
	copy = min_t(size_t, bytes, bvec->bv_len - skip);

	kaddr = kmap_atomic(page);
	from = kaddr + offset;
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
	kunmap_atomic(kaddr);
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

static size_t copy_page_from_iter_bvec(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	size_t skip, copy, wanted;
	const struct bio_vec *bvec;
	void *kaddr, *to;

	if (unlikely(bytes > i->count))
		bytes = i->count;

	if (unlikely(!bytes))
		return 0;

	wanted = bytes;
	bvec = i->bvec;
	skip = i->iov_offset;

	kaddr = kmap_atomic(page);

	to = kaddr + offset;

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
	kunmap_atomic(kaddr);
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

static size_t copy_from_user_bvec(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes)
{
	char *kaddr;
	size_t left;
	const struct bio_vec *bvec;
	size_t base = i->iov_offset;

	kaddr = kmap_atomic(page);
	for (left = bytes, bvec = i->bvec; left; bvec++, base = 0) {
		size_t copy = min(left, bvec->bv_len - base);
		if (!bvec->bv_len)
			continue;
		memcpy_from_page(kaddr + offset, bvec->bv_page,
				 bvec->bv_offset + base, copy);
		offset += copy;
		left -= copy;
	}
	kunmap_atomic(kaddr);
	return bytes;
}

static void advance_bvec(struct iov_iter *i, size_t bytes)
{
	BUG_ON(i->count < bytes);

	if (likely(i->nr_segs == 1)) {
		i->iov_offset += bytes;
		i->count -= bytes;
	} else {
		const struct bio_vec *bvec = i->bvec;
		size_t base = i->iov_offset;
		unsigned long nr_segs = i->nr_segs;

		/*
		 * The !iov->iov_len check ensures we skip over unlikely
		 * zero-length segments (without overruning the iovec).
		 */
		while (bytes || unlikely(i->count && !bvec->bv_len)) {
			int copy;

			copy = min(bytes, bvec->bv_len - base);
			BUG_ON(!i->count || i->count < copy);
			i->count -= copy;
			bytes -= copy;
			base += copy;
			if (bvec->bv_len == base) {
				bvec++;
				nr_segs--;
				base = 0;
			}
		}
		i->bvec = bvec;
		i->iov_offset = base;
		i->nr_segs = nr_segs;
	}
}

static unsigned long alignment_bvec(const struct iov_iter *i)
{
	const struct bio_vec *bvec = i->bvec;
	unsigned long res;
	size_t size = i->count;
	size_t n;

	if (!size)
		return 0;

	res = bvec->bv_offset + i->iov_offset;
	n = bvec->bv_len - i->iov_offset;
	if (n >= size)
		return res | size;
	size -= n;
	res |= n;
	while (size > (++bvec)->bv_len) {
		res |= bvec->bv_offset | bvec->bv_len;
		size -= bvec->bv_len;
	}
	res |= bvec->bv_offset | size;
	return res;
}

static ssize_t get_pages_bvec(struct iov_iter *i,
		   struct page **pages, unsigned maxpages,
		   size_t *start)
{
	const struct bio_vec *bvec = i->bvec;
	size_t len = bvec->bv_len - i->iov_offset;
	if (len > i->count)
		len = i->count;
	/* can't be more than PAGE_SIZE */
	*start = bvec->bv_offset + i->iov_offset;

	get_page(*pages = bvec->bv_page);

	return len;
}

static ssize_t get_pages_alloc_bvec(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	const struct bio_vec *bvec = i->bvec;
	size_t len = bvec->bv_len - i->iov_offset;
	if (len > i->count)
		len = i->count;
	if (len > maxsize)
		len = maxsize;
	*start = bvec->bv_offset + i->iov_offset;

	*pages = kmalloc(sizeof(struct page *), GFP_KERNEL);
	if (!*pages)
		return -ENOMEM;

	get_page(**pages = bvec->bv_page);

	return len;
}

static int iov_iter_npages_bvec(const struct iov_iter *i, int maxpages)
{
	size_t offset = i->iov_offset;
	size_t size = i->count;
	const struct bio_vec *bvec = i->bvec;
	int npages = 0;
	int n;

	for (n = 0; size && n < i->nr_segs; n++, bvec++) {
		size_t len = bvec->bv_len - offset;
		offset = 0;
		if (unlikely(!len))	/* empty segment */
			continue;
		if (len > size)
			len = size;
		npages++;
		if (npages >= maxpages)	/* don't bother going further */
			return maxpages;
		size -= len;
		offset = 0;
	}
	return min(npages, maxpages);
}

size_t copy_page_to_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (i->type & ITER_BVEC)
		return copy_page_to_iter_bvec(page, offset, bytes, i);
	else
		return copy_page_to_iter_iovec(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_to_iter);

size_t copy_page_from_iter(struct page *page, size_t offset, size_t bytes,
			 struct iov_iter *i)
{
	if (i->type & ITER_BVEC)
		return copy_page_from_iter_bvec(page, offset, bytes, i);
	else
		return copy_page_from_iter_iovec(page, offset, bytes, i);
}
EXPORT_SYMBOL(copy_page_from_iter);

size_t iov_iter_copy_from_user_atomic(struct page *page,
		struct iov_iter *i, unsigned long offset, size_t bytes)
{
	if (i->type & ITER_BVEC)
		return copy_from_user_bvec(page, i, offset, bytes);
	else
		return copy_from_user_atomic_iovec(page, i, offset, bytes);
}
EXPORT_SYMBOL(iov_iter_copy_from_user_atomic);

void iov_iter_advance(struct iov_iter *i, size_t size)
{
	if (i->type & ITER_BVEC)
		advance_bvec(i, size);
	else
		advance_iovec(i, size);
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
		return min(i->count, i->iov->iov_len - i->iov_offset);
	else
		return min(i->count, i->bvec->bv_len - i->iov_offset);
}
EXPORT_SYMBOL(iov_iter_single_seg_count);

unsigned long iov_iter_alignment(const struct iov_iter *i)
{
	if (i->type & ITER_BVEC)
		return alignment_bvec(i);
	else
		return alignment_iovec(i);
}
EXPORT_SYMBOL(iov_iter_alignment);

ssize_t iov_iter_get_pages(struct iov_iter *i,
		   struct page **pages, unsigned maxpages,
		   size_t *start)
{
	if (i->type & ITER_BVEC)
		return get_pages_bvec(i, pages, maxpages, start);
	else
		return get_pages_iovec(i, pages, maxpages, start);
}
EXPORT_SYMBOL(iov_iter_get_pages);

ssize_t iov_iter_get_pages_alloc(struct iov_iter *i,
		   struct page ***pages, size_t maxsize,
		   size_t *start)
{
	if (i->type & ITER_BVEC)
		return get_pages_alloc_bvec(i, pages, maxsize, start);
	else
		return get_pages_alloc_iovec(i, pages, maxsize, start);
}
EXPORT_SYMBOL(iov_iter_get_pages_alloc);

int iov_iter_npages(const struct iov_iter *i, int maxpages)
{
	if (i->type & ITER_BVEC)
		return iov_iter_npages_bvec(i, maxpages);
	else
		return iov_iter_npages_iovec(i, maxpages);
}
EXPORT_SYMBOL(iov_iter_npages);
