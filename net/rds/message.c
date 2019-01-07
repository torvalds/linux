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
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#include <linux/errqueue.h>

#include "rds.h"

static unsigned int	rds_exthdr_size[__RDS_EXTHDR_MAX] = {
[RDS_EXTHDR_NONE]	= 0,
[RDS_EXTHDR_VERSION]	= sizeof(struct rds_ext_header_version),
[RDS_EXTHDR_RDMA]	= sizeof(struct rds_ext_header_rdma),
[RDS_EXTHDR_RDMA_DEST]	= sizeof(struct rds_ext_header_rdma_dest),
[RDS_EXTHDR_NPATHS]	= sizeof(u16),
[RDS_EXTHDR_GEN_NUM]	= sizeof(u32),
};

void rds_message_addref(struct rds_message *rm)
{
	rdsdebug("addref rm %p ref %d\n", rm, refcount_read(&rm->m_refcount));
	refcount_inc(&rm->m_refcount);
}
EXPORT_SYMBOL_GPL(rds_message_addref);

static inline bool rds_zcookie_add(struct rds_msg_zcopy_info *info, u32 cookie)
{
	struct rds_zcopy_cookies *ck = &info->zcookies;
	int ncookies = ck->num;

	if (ncookies == RDS_MAX_ZCOOKIES)
		return false;
	ck->cookies[ncookies] = cookie;
	ck->num =  ++ncookies;
	return true;
}

static struct rds_msg_zcopy_info *rds_info_from_znotifier(struct rds_znotifier *znotif)
{
	return container_of(znotif, struct rds_msg_zcopy_info, znotif);
}

void rds_notify_msg_zcopy_purge(struct rds_msg_zcopy_queue *q)
{
	unsigned long flags;
	LIST_HEAD(copy);
	struct rds_msg_zcopy_info *info, *tmp;

	spin_lock_irqsave(&q->lock, flags);
	list_splice(&q->zcookie_head, &copy);
	INIT_LIST_HEAD(&q->zcookie_head);
	spin_unlock_irqrestore(&q->lock, flags);

	list_for_each_entry_safe(info, tmp, &copy, rs_zcookie_next) {
		list_del(&info->rs_zcookie_next);
		kfree(info);
	}
}

static void rds_rm_zerocopy_callback(struct rds_sock *rs,
				     struct rds_znotifier *znotif)
{
	struct rds_msg_zcopy_info *info;
	struct rds_msg_zcopy_queue *q;
	u32 cookie = znotif->z_cookie;
	struct rds_zcopy_cookies *ck;
	struct list_head *head;
	unsigned long flags;

	mm_unaccount_pinned_pages(&znotif->z_mmp);
	q = &rs->rs_zcookie_queue;
	spin_lock_irqsave(&q->lock, flags);
	head = &q->zcookie_head;
	if (!list_empty(head)) {
		info = list_entry(head, struct rds_msg_zcopy_info,
				  rs_zcookie_next);
		if (info && rds_zcookie_add(info, cookie)) {
			spin_unlock_irqrestore(&q->lock, flags);
			kfree(rds_info_from_znotifier(znotif));
			/* caller invokes rds_wake_sk_sleep() */
			return;
		}
	}

	info = rds_info_from_znotifier(znotif);
	ck = &info->zcookies;
	memset(ck, 0, sizeof(*ck));
	WARN_ON(!rds_zcookie_add(info, cookie));
	list_add_tail(&q->zcookie_head, &info->rs_zcookie_next);

	spin_unlock_irqrestore(&q->lock, flags);
	/* caller invokes rds_wake_sk_sleep() */
}

/*
 * This relies on dma_map_sg() not touching sg[].page during merging.
 */
static void rds_message_purge(struct rds_message *rm)
{
	unsigned long i, flags;
	bool zcopy = false;

	if (unlikely(test_bit(RDS_MSG_PAGEVEC, &rm->m_flags)))
		return;

	spin_lock_irqsave(&rm->m_rs_lock, flags);
	if (rm->m_rs) {
		struct rds_sock *rs = rm->m_rs;

		if (rm->data.op_mmp_znotifier) {
			zcopy = true;
			rds_rm_zerocopy_callback(rs, rm->data.op_mmp_znotifier);
			rds_wake_sk_sleep(rs);
			rm->data.op_mmp_znotifier = NULL;
		}
		sock_put(rds_rs_to_sk(rs));
		rm->m_rs = NULL;
	}
	spin_unlock_irqrestore(&rm->m_rs_lock, flags);

	for (i = 0; i < rm->data.op_nents; i++) {
		/* XXX will have to put_page for page refs */
		if (!zcopy)
			__free_page(sg_page(&rm->data.op_sg[i]));
		else
			put_page(sg_page(&rm->data.op_sg[i]));
	}
	rm->data.op_nents = 0;

	if (rm->rdma.op_active)
		rds_rdma_free_op(&rm->rdma);
	if (rm->rdma.op_rdma_mr)
		rds_mr_put(rm->rdma.op_rdma_mr);

	if (rm->atomic.op_active)
		rds_atomic_free_op(&rm->atomic);
	if (rm->atomic.op_rdma_mr)
		rds_mr_put(rm->atomic.op_rdma_mr);
}

void rds_message_put(struct rds_message *rm)
{
	rdsdebug("put rm %p ref %d\n", rm, refcount_read(&rm->m_refcount));
	WARN(!refcount_read(&rm->m_refcount), "danger refcount zero on %p\n", rm);
	if (refcount_dec_and_test(&rm->m_refcount)) {
		BUG_ON(!list_empty(&rm->m_sock_item));
		BUG_ON(!list_empty(&rm->m_conn_item));
		rds_message_purge(rm);

		kfree(rm);
	}
}
EXPORT_SYMBOL_GPL(rds_message_put);

void rds_message_populate_header(struct rds_header *hdr, __be16 sport,
				 __be16 dport, u64 seq)
{
	hdr->h_flags = 0;
	hdr->h_sport = sport;
	hdr->h_dport = dport;
	hdr->h_sequence = cpu_to_be64(seq);
	hdr->h_exthdr[0] = RDS_EXTHDR_NONE;
}
EXPORT_SYMBOL_GPL(rds_message_populate_header);

int rds_message_add_extension(struct rds_header *hdr, unsigned int type,
			      const void *data, unsigned int len)
{
	unsigned int ext_len = sizeof(u8) + len;
	unsigned char *dst;

	/* For now, refuse to add more than one extension header */
	if (hdr->h_exthdr[0] != RDS_EXTHDR_NONE)
		return 0;

	if (type >= __RDS_EXTHDR_MAX || len != rds_exthdr_size[type])
		return 0;

	if (ext_len >= RDS_HEADER_EXT_SPACE)
		return 0;
	dst = hdr->h_exthdr;

	*dst++ = type;
	memcpy(dst, data, len);

	dst[len] = RDS_EXTHDR_NONE;
	return 1;
}
EXPORT_SYMBOL_GPL(rds_message_add_extension);

/*
 * If a message has extension headers, retrieve them here.
 * Call like this:
 *
 * unsigned int pos = 0;
 *
 * while (1) {
 *	buflen = sizeof(buffer);
 *	type = rds_message_next_extension(hdr, &pos, buffer, &buflen);
 *	if (type == RDS_EXTHDR_NONE)
 *		break;
 *	...
 * }
 */
int rds_message_next_extension(struct rds_header *hdr,
		unsigned int *pos, void *buf, unsigned int *buflen)
{
	unsigned int offset, ext_type, ext_len;
	u8 *src = hdr->h_exthdr;

	offset = *pos;
	if (offset >= RDS_HEADER_EXT_SPACE)
		goto none;

	/* Get the extension type and length. For now, the
	 * length is implied by the extension type. */
	ext_type = src[offset++];

	if (ext_type == RDS_EXTHDR_NONE || ext_type >= __RDS_EXTHDR_MAX)
		goto none;
	ext_len = rds_exthdr_size[ext_type];
	if (offset + ext_len > RDS_HEADER_EXT_SPACE)
		goto none;

	*pos = offset + ext_len;
	if (ext_len < *buflen)
		*buflen = ext_len;
	memcpy(buf, src + offset, *buflen);
	return ext_type;

none:
	*pos = RDS_HEADER_EXT_SPACE;
	*buflen = 0;
	return RDS_EXTHDR_NONE;
}

int rds_message_add_rdma_dest_extension(struct rds_header *hdr, u32 r_key, u32 offset)
{
	struct rds_ext_header_rdma_dest ext_hdr;

	ext_hdr.h_rdma_rkey = cpu_to_be32(r_key);
	ext_hdr.h_rdma_offset = cpu_to_be32(offset);
	return rds_message_add_extension(hdr, RDS_EXTHDR_RDMA_DEST, &ext_hdr, sizeof(ext_hdr));
}
EXPORT_SYMBOL_GPL(rds_message_add_rdma_dest_extension);

/*
 * Each rds_message is allocated with extra space for the scatterlist entries
 * rds ops will need. This is to minimize memory allocation count. Then, each rds op
 * can grab SGs when initializing its part of the rds_message.
 */
struct rds_message *rds_message_alloc(unsigned int extra_len, gfp_t gfp)
{
	struct rds_message *rm;

	if (extra_len > KMALLOC_MAX_SIZE - sizeof(struct rds_message))
		return NULL;

	rm = kzalloc(sizeof(struct rds_message) + extra_len, gfp);
	if (!rm)
		goto out;

	rm->m_used_sgs = 0;
	rm->m_total_sgs = extra_len / sizeof(struct scatterlist);

	refcount_set(&rm->m_refcount, 1);
	INIT_LIST_HEAD(&rm->m_sock_item);
	INIT_LIST_HEAD(&rm->m_conn_item);
	spin_lock_init(&rm->m_rs_lock);
	init_waitqueue_head(&rm->m_flush_wait);

out:
	return rm;
}

/*
 * RDS ops use this to grab SG entries from the rm's sg pool.
 */
struct scatterlist *rds_message_alloc_sgs(struct rds_message *rm, int nents,
					  int *ret)
{
	struct scatterlist *sg_first = (struct scatterlist *) &rm[1];
	struct scatterlist *sg_ret;

	if (WARN_ON(!ret))
		return NULL;

	if (nents <= 0) {
		pr_warn("rds: alloc sgs failed! nents <= 0\n");
		*ret = -EINVAL;
		return NULL;
	}

	if (rm->m_used_sgs + nents > rm->m_total_sgs) {
		pr_warn("rds: alloc sgs failed! total %d used %d nents %d\n",
			rm->m_total_sgs, rm->m_used_sgs, nents);
		*ret = -ENOMEM;
		return NULL;
	}

	sg_ret = &sg_first[rm->m_used_sgs];
	sg_init_table(sg_ret, nents);
	rm->m_used_sgs += nents;

	return sg_ret;
}

struct rds_message *rds_message_map_pages(unsigned long *page_addrs, unsigned int total_len)
{
	struct rds_message *rm;
	unsigned int i;
	int num_sgs = DIV_ROUND_UP(total_len, PAGE_SIZE);
	int extra_bytes = num_sgs * sizeof(struct scatterlist);
	int ret;

	rm = rds_message_alloc(extra_bytes, GFP_NOWAIT);
	if (!rm)
		return ERR_PTR(-ENOMEM);

	set_bit(RDS_MSG_PAGEVEC, &rm->m_flags);
	rm->m_inc.i_hdr.h_len = cpu_to_be32(total_len);
	rm->data.op_nents = DIV_ROUND_UP(total_len, PAGE_SIZE);
	rm->data.op_sg = rds_message_alloc_sgs(rm, num_sgs, &ret);
	if (!rm->data.op_sg) {
		rds_message_put(rm);
		return ERR_PTR(ret);
	}

	for (i = 0; i < rm->data.op_nents; ++i) {
		sg_set_page(&rm->data.op_sg[i],
				virt_to_page(page_addrs[i]),
				PAGE_SIZE, 0);
	}

	return rm;
}

static int rds_message_zcopy_from_user(struct rds_message *rm, struct iov_iter *from)
{
	struct scatterlist *sg;
	int ret = 0;
	int length = iov_iter_count(from);
	int total_copied = 0;
	struct rds_msg_zcopy_info *info;

	rm->m_inc.i_hdr.h_len = cpu_to_be32(iov_iter_count(from));

	/*
	 * now allocate and copy in the data payload.
	 */
	sg = rm->data.op_sg;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	INIT_LIST_HEAD(&info->rs_zcookie_next);
	rm->data.op_mmp_znotifier = &info->znotif;
	if (mm_account_pinned_pages(&rm->data.op_mmp_znotifier->z_mmp,
				    length)) {
		ret = -ENOMEM;
		goto err;
	}
	while (iov_iter_count(from)) {
		struct page *pages;
		size_t start;
		ssize_t copied;

		copied = iov_iter_get_pages(from, &pages, PAGE_SIZE,
					    1, &start);
		if (copied < 0) {
			struct mmpin *mmp;
			int i;

			for (i = 0; i < rm->data.op_nents; i++)
				put_page(sg_page(&rm->data.op_sg[i]));
			mmp = &rm->data.op_mmp_znotifier->z_mmp;
			mm_unaccount_pinned_pages(mmp);
			ret = -EFAULT;
			goto err;
		}
		total_copied += copied;
		iov_iter_advance(from, copied);
		length -= copied;
		sg_set_page(sg, pages, copied, start);
		rm->data.op_nents++;
		sg++;
	}
	WARN_ON_ONCE(length != 0);
	return ret;
err:
	kfree(info);
	rm->data.op_mmp_znotifier = NULL;
	return ret;
}

int rds_message_copy_from_user(struct rds_message *rm, struct iov_iter *from,
			       bool zcopy)
{
	unsigned long to_copy, nbytes;
	unsigned long sg_off;
	struct scatterlist *sg;
	int ret = 0;

	rm->m_inc.i_hdr.h_len = cpu_to_be32(iov_iter_count(from));

	/* now allocate and copy in the data payload.  */
	sg = rm->data.op_sg;
	sg_off = 0; /* Dear gcc, sg->page will be null from kzalloc. */

	if (zcopy)
		return rds_message_zcopy_from_user(rm, from);

	while (iov_iter_count(from)) {
		if (!sg_page(sg)) {
			ret = rds_page_remainder_alloc(sg, iov_iter_count(from),
						       GFP_HIGHUSER);
			if (ret)
				return ret;
			rm->data.op_nents++;
			sg_off = 0;
		}

		to_copy = min_t(unsigned long, iov_iter_count(from),
				sg->length - sg_off);

		rds_stats_add(s_copy_from_user, to_copy);
		nbytes = copy_page_from_iter(sg_page(sg), sg->offset + sg_off,
					     to_copy, from);
		if (nbytes != to_copy)
			return -EFAULT;

		sg_off += to_copy;

		if (sg_off == sg->length)
			sg++;
	}

	return ret;
}

int rds_message_inc_copy_to_user(struct rds_incoming *inc, struct iov_iter *to)
{
	struct rds_message *rm;
	struct scatterlist *sg;
	unsigned long to_copy;
	unsigned long vec_off;
	int copied;
	int ret;
	u32 len;

	rm = container_of(inc, struct rds_message, m_inc);
	len = be32_to_cpu(rm->m_inc.i_hdr.h_len);

	sg = rm->data.op_sg;
	vec_off = 0;
	copied = 0;

	while (iov_iter_count(to) && copied < len) {
		to_copy = min_t(unsigned long, iov_iter_count(to),
				sg->length - vec_off);
		to_copy = min_t(unsigned long, to_copy, len - copied);

		rds_stats_add(s_copy_to_user, to_copy);
		ret = copy_page_to_iter(sg_page(sg), sg->offset + vec_off,
					to_copy, to);
		if (ret != to_copy)
			return -EFAULT;

		vec_off += to_copy;
		copied += to_copy;

		if (vec_off == sg->length) {
			vec_off = 0;
			sg++;
		}
	}

	return copied;
}

/*
 * If the message is still on the send queue, wait until the transport
 * is done with it. This is particularly important for RDMA operations.
 */
void rds_message_wait(struct rds_message *rm)
{
	wait_event_interruptible(rm->m_flush_wait,
			!test_bit(RDS_MSG_MAPPED, &rm->m_flags));
}

void rds_message_unmapped(struct rds_message *rm)
{
	clear_bit(RDS_MSG_MAPPED, &rm->m_flags);
	wake_up_interruptible(&rm->m_flush_wait);
}
EXPORT_SYMBOL_GPL(rds_message_unmapped);
