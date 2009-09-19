/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "rds.h"

/*
 * This file implements a getsockopt() call which copies a set of fixed
 * sized structs into a user-specified buffer as a means of providing
 * read-only information about RDS.
 *
 * For a given information source there are a given number of fixed sized
 * structs at a given time.  The structs are only copied if the user-specified
 * buffer is big enough.  The destination pages that make up the buffer
 * are pinned for the duration of the copy.
 *
 * This gives us the following benefits:
 *
 * - simple implementation, no copy "position" across multiple calls
 * - consistent snapshot of an info source
 * - atomic copy works well with whatever locking info source has
 * - one portable tool to get rds info across implementations
 * - long-lived tool can get info without allocating
 *
 * at the following costs:
 *
 * - info source copy must be pinned, may be "large"
 */

struct rds_info_iterator {
	struct page **pages;
	void *addr;
	unsigned long offset;
};

static DEFINE_SPINLOCK(rds_info_lock);
static rds_info_func rds_info_funcs[RDS_INFO_LAST - RDS_INFO_FIRST + 1];

void rds_info_register_func(int optname, rds_info_func func)
{
	int offset = optname - RDS_INFO_FIRST;

	BUG_ON(optname < RDS_INFO_FIRST || optname > RDS_INFO_LAST);

	spin_lock(&rds_info_lock);
	BUG_ON(rds_info_funcs[offset] != NULL);
	rds_info_funcs[offset] = func;
	spin_unlock(&rds_info_lock);
}
EXPORT_SYMBOL_GPL(rds_info_register_func);

void rds_info_deregister_func(int optname, rds_info_func func)
{
	int offset = optname - RDS_INFO_FIRST;

	BUG_ON(optname < RDS_INFO_FIRST || optname > RDS_INFO_LAST);

	spin_lock(&rds_info_lock);
	BUG_ON(rds_info_funcs[offset] != func);
	rds_info_funcs[offset] = NULL;
	spin_unlock(&rds_info_lock);
}
EXPORT_SYMBOL_GPL(rds_info_deregister_func);

/*
 * Typically we hold an atomic kmap across multiple rds_info_copy() calls
 * because the kmap is so expensive.  This must be called before using blocking
 * operations while holding the mapping and as the iterator is torn down.
 */
void rds_info_iter_unmap(struct rds_info_iterator *iter)
{
	if (iter->addr != NULL) {
		kunmap_atomic(iter->addr, KM_USER0);
		iter->addr = NULL;
	}
}

/*
 * get_user_pages() called flush_dcache_page() on the pages for us.
 */
void rds_info_copy(struct rds_info_iterator *iter, void *data,
		   unsigned long bytes)
{
	unsigned long this;

	while (bytes) {
		if (iter->addr == NULL)
			iter->addr = kmap_atomic(*iter->pages, KM_USER0);

		this = min(bytes, PAGE_SIZE - iter->offset);

		rdsdebug("page %p addr %p offset %lu this %lu data %p "
			  "bytes %lu\n", *iter->pages, iter->addr,
			  iter->offset, this, data, bytes);

		memcpy(iter->addr + iter->offset, data, this);

		data += this;
		bytes -= this;
		iter->offset += this;

		if (iter->offset == PAGE_SIZE) {
			kunmap_atomic(iter->addr, KM_USER0);
			iter->addr = NULL;
			iter->offset = 0;
			iter->pages++;
		}
	}
}
EXPORT_SYMBOL_GPL(rds_info_copy);

/*
 * @optval points to the userspace buffer that the information snapshot
 * will be copied into.
 *
 * @optlen on input is the size of the buffer in userspace.  @optlen
 * on output is the size of the requested snapshot in bytes.
 *
 * This function returns -errno if there is a failure, particularly -ENOSPC
 * if the given userspace buffer was not large enough to fit the snapshot.
 * On success it returns the positive number of bytes of each array element
 * in the snapshot.
 */
int rds_info_getsockopt(struct socket *sock, int optname, char __user *optval,
			int __user *optlen)
{
	struct rds_info_iterator iter;
	struct rds_info_lengths lens;
	unsigned long nr_pages = 0;
	unsigned long start;
	unsigned long i;
	rds_info_func func;
	struct page **pages = NULL;
	int ret;
	int len;
	int total;

	if (get_user(len, optlen)) {
		ret = -EFAULT;
		goto out;
	}

	/* check for all kinds of wrapping and the like */
	start = (unsigned long)optval;
	if (len < 0 || len + PAGE_SIZE - 1 < len || start + len < start) {
		ret = -EINVAL;
		goto out;
	}

	/* a 0 len call is just trying to probe its length */
	if (len == 0)
		goto call_func;

	nr_pages = (PAGE_ALIGN(start + len) - (start & PAGE_MASK))
			>> PAGE_SHIFT;

	pages = kmalloc(nr_pages * sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	ret = get_user_pages_fast(start, nr_pages, 1, pages);
	if (ret != nr_pages) {
		if (ret > 0)
			nr_pages = ret;
		else
			nr_pages = 0;
		ret = -EAGAIN; /* XXX ? */
		goto out;
	}

	rdsdebug("len %d nr_pages %lu\n", len, nr_pages);

call_func:
	func = rds_info_funcs[optname - RDS_INFO_FIRST];
	if (func == NULL) {
		ret = -ENOPROTOOPT;
		goto out;
	}

	iter.pages = pages;
	iter.addr = NULL;
	iter.offset = start & (PAGE_SIZE - 1);

	func(sock, len, &iter, &lens);
	BUG_ON(lens.each == 0);

	total = lens.nr * lens.each;

	rds_info_iter_unmap(&iter);

	if (total > len) {
		len = total;
		ret = -ENOSPC;
	} else {
		len = total;
		ret = lens.each;
	}

	if (put_user(len, optlen))
		ret = -EFAULT;

out:
	for (i = 0; pages != NULL && i < nr_pages; i++)
		put_page(pages[i]);
	kfree(pages);

	return ret;
}
