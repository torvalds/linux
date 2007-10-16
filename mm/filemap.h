/*
 *	linux/mm/filemap.h
 *
 * Copyright (C) 1994-1999  Linus Torvalds
 */

#ifndef __FILEMAP_H
#define __FILEMAP_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/uio.h>
#include <linux/uaccess.h>

size_t
__filemap_copy_from_user_iovec_inatomic(char *vaddr,
					const struct iovec *iov,
					size_t base,
					size_t bytes);

/*
 * Copy as much as we can into the page and return the number of bytes which
 * were sucessfully copied.  If a fault is encountered then clear the page
 * out to (offset+bytes) and return the number of bytes which were copied.
 *
 * NOTE: For this to work reliably we really want copy_from_user_inatomic_nocache
 * to *NOT* zero any tail of the buffer that it failed to copy.  If it does,
 * and if the following non-atomic copy succeeds, then there is a small window
 * where the target page contains neither the data before the write, nor the
 * data after the write (it contains zero).  A read at this time will see
 * data that is inconsistent with any ordering of the read and the write.
 * (This has been detected in practice).
 */
static inline size_t
filemap_copy_from_user(struct page *page, unsigned long offset,
			const char __user *buf, unsigned bytes)
{
	char *kaddr;
	int left;

	kaddr = kmap_atomic(page, KM_USER0);
	left = __copy_from_user_inatomic_nocache(kaddr + offset, buf, bytes);
	kunmap_atomic(kaddr, KM_USER0);

	if (left != 0) {
		/* Do it the slow way */
		kaddr = kmap(page);
		left = __copy_from_user_nocache(kaddr + offset, buf, bytes);
		kunmap(page);
	}
	return bytes - left;
}

/*
 * This has the same sideeffects and return value as filemap_copy_from_user().
 * The difference is that on a fault we need to memset the remainder of the
 * page (out to offset+bytes), to emulate filemap_copy_from_user()'s
 * single-segment behaviour.
 */
static inline size_t
filemap_copy_from_user_iovec(struct page *page, unsigned long offset,
			const struct iovec *iov, size_t base, size_t bytes)
{
	char *kaddr;
	size_t copied;

	kaddr = kmap_atomic(page, KM_USER0);
	copied = __filemap_copy_from_user_iovec_inatomic(kaddr + offset, iov,
							 base, bytes);
	kunmap_atomic(kaddr, KM_USER0);
	if (copied != bytes) {
		kaddr = kmap(page);
		copied = __filemap_copy_from_user_iovec_inatomic(kaddr + offset, iov,
								 base, bytes);
		if (bytes - copied)
			memset(kaddr + offset + copied, 0, bytes - copied);
		kunmap(page);
	}
	return copied;
}

static inline void
filemap_set_next_iovec(const struct iovec **iovp, size_t *basep, size_t bytes)
{
	const struct iovec *iov = *iovp;
	size_t base = *basep;

	while (bytes) {
		int copy = min(bytes, iov->iov_len - base);

		bytes -= copy;
		base += copy;
		if (iov->iov_len == base) {
			iov++;
			base = 0;
		}
	}
	*iovp = iov;
	*basep = base;
}
#endif
