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

#include "rds.h"
#include "rdma.h"

static DECLARE_WAIT_QUEUE_HEAD(rds_message_flush_waitq);

static unsigned int	rds_exthdr_size[__RDS_EXTHDR_MAX] = {
[RDS_EXTHDR_NONE]	= 0,
[RDS_EXTHDR_VERSION]	= sizeof(struct rds_ext_header_version),
[RDS_EXTHDR_RDMA]	= sizeof(struct rds_ext_header_rdma),
[RDS_EXTHDR_RDMA_DEST]	= sizeof(struct rds_ext_header_rdma_dest),
};


void rds_message_addref(struct rds_message *rm)
{
	rdsdebug("addref rm %p ref %d\n", rm, atomic_read(&rm->m_refcount));
	atomic_inc(&rm->m_refcount);
}
EXPORT_SYMBOL_GPL(rds_message_addref);

/*
 * This relies on dma_map_sg() not touching sg[].page during merging.
 */
static void rds_message_purge(struct rds_message *rm)
{
	unsigned long i;

	if (unlikely(test_bit(RDS_MSG_PAGEVEC, &rm->m_flags)))
		return;

	for (i = 0; i < rm->m_nents; i++) {
		rdsdebug("putting data page %p\n", (void *)sg_page(&rm->m_sg[i]));
		/* XXX will have to put_page for page refs */
		__free_page(sg_page(&rm->m_sg[i]));
	}
	rm->m_nents = 0;

	if (rm->m_rdma_op)
		rds_rdma_free_op(rm->m_rdma_op);
	if (rm->m_rdma_mr)
		rds_mr_put(rm->m_rdma_mr);
}

void rds_message_inc_purge(struct rds_incoming *inc)
{
	struct rds_message *rm = container_of(inc, struct rds_message, m_inc);
	rds_message_purge(rm);
}

void rds_message_put(struct rds_message *rm)
{
	rdsdebug("put rm %p ref %d\n", rm, atomic_read(&rm->m_refcount));

	if (atomic_dec_and_test(&rm->m_refcount)) {
		BUG_ON(!list_empty(&rm->m_sock_item));
		BUG_ON(!list_empty(&rm->m_conn_item));
		rds_message_purge(rm);

		kfree(rm);
	}
}
EXPORT_SYMBOL_GPL(rds_message_put);

void rds_message_inc_free(struct rds_incoming *inc)
{
	struct rds_message *rm = container_of(inc, struct rds_message, m_inc);
	rds_message_put(rm);
}

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

int rds_message_add_extension(struct rds_header *hdr,
		unsigned int type, const void *data, unsigned int len)
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

int rds_message_add_version_extension(struct rds_header *hdr, unsigned int version)
{
	struct rds_ext_header_version ext_hdr;

	ext_hdr.h_version = cpu_to_be32(version);
	return rds_message_add_extension(hdr, RDS_EXTHDR_VERSION, &ext_hdr, sizeof(ext_hdr));
}

int rds_message_get_version_extension(struct rds_header *hdr, unsigned int *version)
{
	struct rds_ext_header_version ext_hdr;
	unsigned int pos = 0, len = sizeof(ext_hdr);

	/* We assume the version extension is the only one present */
	if (rds_message_next_extension(hdr, &pos, &ext_hdr, &len) != RDS_EXTHDR_VERSION)
		return 0;
	*version = be32_to_cpu(ext_hdr.h_version);
	return 1;
}

int rds_message_add_rdma_dest_extension(struct rds_header *hdr, u32 r_key, u32 offset)
{
	struct rds_ext_header_rdma_dest ext_hdr;

	ext_hdr.h_rdma_rkey = cpu_to_be32(r_key);
	ext_hdr.h_rdma_offset = cpu_to_be32(offset);
	return rds_message_add_extension(hdr, RDS_EXTHDR_RDMA_DEST, &ext_hdr, sizeof(ext_hdr));
}
EXPORT_SYMBOL_GPL(rds_message_add_rdma_dest_extension);

struct rds_message *rds_message_alloc(unsigned int nents, gfp_t gfp)
{
	struct rds_message *rm;

	rm = kzalloc(sizeof(struct rds_message) +
		     (nents * sizeof(struct scatterlist)), gfp);
	if (!rm)
		goto out;

	if (nents)
		sg_init_table(rm->m_sg, nents);
	atomic_set(&rm->m_refcount, 1);
	INIT_LIST_HEAD(&rm->m_sock_item);
	INIT_LIST_HEAD(&rm->m_conn_item);
	spin_lock_init(&rm->m_rs_lock);

out:
	return rm;
}

struct rds_message *rds_message_map_pages(unsigned long *page_addrs, unsigned int total_len)
{
	struct rds_message *rm;
	unsigned int i;

	rm = rds_message_alloc(ceil(total_len, PAGE_SIZE), GFP_KERNEL);
	if (rm == NULL)
		return ERR_PTR(-ENOMEM);

	set_bit(RDS_MSG_PAGEVEC, &rm->m_flags);
	rm->m_inc.i_hdr.h_len = cpu_to_be32(total_len);
	rm->m_nents = ceil(total_len, PAGE_SIZE);

	for (i = 0; i < rm->m_nents; ++i) {
		sg_set_page(&rm->m_sg[i],
				virt_to_page(page_addrs[i]),
				PAGE_SIZE, 0);
	}

	return rm;
}

struct rds_message *rds_message_copy_from_user(struct iovec *first_iov,
					       size_t total_len)
{
	unsigned long to_copy;
	unsigned long iov_off;
	unsigned long sg_off;
	struct rds_message *rm;
	struct iovec *iov;
	struct scatterlist *sg;
	int ret;

	rm = rds_message_alloc(ceil(total_len, PAGE_SIZE), GFP_KERNEL);
	if (rm == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	rm->m_inc.i_hdr.h_len = cpu_to_be32(total_len);

	/*
	 * now allocate and copy in the data payload.
	 */
	sg = rm->m_sg;
	iov = first_iov;
	iov_off = 0;
	sg_off = 0; /* Dear gcc, sg->page will be null from kzalloc. */

	while (total_len) {
		if (sg_page(sg) == NULL) {
			ret = rds_page_remainder_alloc(sg, total_len,
						       GFP_HIGHUSER);
			if (ret)
				goto out;
			rm->m_nents++;
			sg_off = 0;
		}

		while (iov_off == iov->iov_len) {
			iov_off = 0;
			iov++;
		}

		to_copy = min(iov->iov_len - iov_off, sg->length - sg_off);
		to_copy = min_t(size_t, to_copy, total_len);

		rdsdebug("copying %lu bytes from user iov [%p, %zu] + %lu to "
			 "sg [%p, %u, %u] + %lu\n",
			 to_copy, iov->iov_base, iov->iov_len, iov_off,
			 (void *)sg_page(sg), sg->offset, sg->length, sg_off);

		ret = rds_page_copy_from_user(sg_page(sg), sg->offset + sg_off,
					      iov->iov_base + iov_off,
					      to_copy);
		if (ret)
			goto out;

		iov_off += to_copy;
		total_len -= to_copy;
		sg_off += to_copy;

		if (sg_off == sg->length)
			sg++;
	}

	ret = 0;
out:
	if (ret) {
		if (rm)
			rds_message_put(rm);
		rm = ERR_PTR(ret);
	}
	return rm;
}

int rds_message_inc_copy_to_user(struct rds_incoming *inc,
				 struct iovec *first_iov, size_t size)
{
	struct rds_message *rm;
	struct iovec *iov;
	struct scatterlist *sg;
	unsigned long to_copy;
	unsigned long iov_off;
	unsigned long vec_off;
	int copied;
	int ret;
	u32 len;

	rm = container_of(inc, struct rds_message, m_inc);
	len = be32_to_cpu(rm->m_inc.i_hdr.h_len);

	iov = first_iov;
	iov_off = 0;
	sg = rm->m_sg;
	vec_off = 0;
	copied = 0;

	while (copied < size && copied < len) {
		while (iov_off == iov->iov_len) {
			iov_off = 0;
			iov++;
		}

		to_copy = min(iov->iov_len - iov_off, sg->length - vec_off);
		to_copy = min_t(size_t, to_copy, size - copied);
		to_copy = min_t(unsigned long, to_copy, len - copied);

		rdsdebug("copying %lu bytes to user iov [%p, %zu] + %lu to "
			 "sg [%p, %u, %u] + %lu\n",
			 to_copy, iov->iov_base, iov->iov_len, iov_off,
			 sg_page(sg), sg->offset, sg->length, vec_off);

		ret = rds_page_copy_to_user(sg_page(sg), sg->offset + vec_off,
					    iov->iov_base + iov_off,
					    to_copy);
		if (ret) {
			copied = ret;
			break;
		}

		iov_off += to_copy;
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
	wait_event(rds_message_flush_waitq,
			!test_bit(RDS_MSG_MAPPED, &rm->m_flags));
}

void rds_message_unmapped(struct rds_message *rm)
{
	clear_bit(RDS_MSG_MAPPED, &rm->m_flags);
	if (waitqueue_active(&rds_message_flush_waitq))
		wake_up(&rds_message_flush_waitq);
}
EXPORT_SYMBOL_GPL(rds_message_unmapped);

