/*
 * Copyright (c) 2007 Oracle.  All rights reserved.
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
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/dma-mapping.h> /* for DMA_*_DEVICE */

#include "rdma.h"

/*
 * XXX
 *  - build with sparse
 *  - should we limit the size of a mr region?  let transport return failure?
 *  - should we detect duplicate keys on a socket?  hmm.
 *  - an rdma is an mlock, apply rlimit?
 */

/*
 * get the number of pages by looking at the page indices that the start and
 * end addresses fall in.
 *
 * Returns 0 if the vec is invalid.  It is invalid if the number of bytes
 * causes the address to wrap or overflows an unsigned int.  This comes
 * from being stored in the 'length' member of 'struct scatterlist'.
 */
static unsigned int rds_pages_in_vec(struct rds_iovec *vec)
{
	if ((vec->addr + vec->bytes <= vec->addr) ||
	    (vec->bytes > (u64)UINT_MAX))
		return 0;

	return ((vec->addr + vec->bytes + PAGE_SIZE - 1) >> PAGE_SHIFT) -
		(vec->addr >> PAGE_SHIFT);
}

static struct rds_mr *rds_mr_tree_walk(struct rb_root *root, u64 key,
				       struct rds_mr *insert)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct rds_mr *mr;

	while (*p) {
		parent = *p;
		mr = rb_entry(parent, struct rds_mr, r_rb_node);

		if (key < mr->r_key)
			p = &(*p)->rb_left;
		else if (key > mr->r_key)
			p = &(*p)->rb_right;
		else
			return mr;
	}

	if (insert) {
		rb_link_node(&insert->r_rb_node, parent, p);
		rb_insert_color(&insert->r_rb_node, root);
		atomic_inc(&insert->r_refcount);
	}
	return NULL;
}

/*
 * Destroy the transport-specific part of a MR.
 */
static void rds_destroy_mr(struct rds_mr *mr)
{
	struct rds_sock *rs = mr->r_sock;
	void *trans_private = NULL;
	unsigned long flags;

	rdsdebug("RDS: destroy mr key is %x refcnt %u\n",
			mr->r_key, atomic_read(&mr->r_refcount));

	if (test_and_set_bit(RDS_MR_DEAD, &mr->r_state))
		return;

	spin_lock_irqsave(&rs->rs_rdma_lock, flags);
	if (!RB_EMPTY_NODE(&mr->r_rb_node))
		rb_erase(&mr->r_rb_node, &rs->rs_rdma_keys);
	trans_private = mr->r_trans_private;
	mr->r_trans_private = NULL;
	spin_unlock_irqrestore(&rs->rs_rdma_lock, flags);

	if (trans_private)
		mr->r_trans->free_mr(trans_private, mr->r_invalidate);
}

void __rds_put_mr_final(struct rds_mr *mr)
{
	rds_destroy_mr(mr);
	kfree(mr);
}

/*
 * By the time this is called we can't have any more ioctls called on
 * the socket so we don't need to worry about racing with others.
 */
void rds_rdma_drop_keys(struct rds_sock *rs)
{
	struct rds_mr *mr;
	struct rb_node *node;

	/* Release any MRs associated with this socket */
	while ((node = rb_first(&rs->rs_rdma_keys))) {
		mr = container_of(node, struct rds_mr, r_rb_node);
		if (mr->r_trans == rs->rs_transport)
			mr->r_invalidate = 0;
		rds_mr_put(mr);
	}

	if (rs->rs_transport && rs->rs_transport->flush_mrs)
		rs->rs_transport->flush_mrs();
}

/*
 * Helper function to pin user pages.
 */
static int rds_pin_pages(unsigned long user_addr, unsigned int nr_pages,
			struct page **pages, int write)
{
	int ret;

	ret = get_user_pages_fast(user_addr, nr_pages, write, pages);

	if (ret >= 0 && ret < nr_pages) {
		while (ret--)
			put_page(pages[ret]);
		ret = -EFAULT;
	}

	return ret;
}

static int __rds_rdma_map(struct rds_sock *rs, struct rds_get_mr_args *args,
				u64 *cookie_ret, struct rds_mr **mr_ret)
{
	struct rds_mr *mr = NULL, *found;
	unsigned int nr_pages;
	struct page **pages = NULL;
	struct scatterlist *sg;
	void *trans_private;
	unsigned long flags;
	rds_rdma_cookie_t cookie;
	unsigned int nents;
	long i;
	int ret;

	if (rs->rs_bound_addr == 0) {
		ret = -ENOTCONN; /* XXX not a great errno */
		goto out;
	}

	if (rs->rs_transport->get_mr == NULL) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	nr_pages = rds_pages_in_vec(&args->vec);
	if (nr_pages == 0) {
		ret = -EINVAL;
		goto out;
	}

	rdsdebug("RDS: get_mr addr %llx len %llu nr_pages %u\n",
		args->vec.addr, args->vec.bytes, nr_pages);

	/* XXX clamp nr_pages to limit the size of this alloc? */
	pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	mr = kzalloc(sizeof(struct rds_mr), GFP_KERNEL);
	if (mr == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	atomic_set(&mr->r_refcount, 1);
	RB_CLEAR_NODE(&mr->r_rb_node);
	mr->r_trans = rs->rs_transport;
	mr->r_sock = rs;

	if (args->flags & RDS_RDMA_USE_ONCE)
		mr->r_use_once = 1;
	if (args->flags & RDS_RDMA_INVALIDATE)
		mr->r_invalidate = 1;
	if (args->flags & RDS_RDMA_READWRITE)
		mr->r_write = 1;

	/*
	 * Pin the pages that make up the user buffer and transfer the page
	 * pointers to the mr's sg array.  We check to see if we've mapped
	 * the whole region after transferring the partial page references
	 * to the sg array so that we can have one page ref cleanup path.
	 *
	 * For now we have no flag that tells us whether the mapping is
	 * r/o or r/w. We need to assume r/w, or we'll do a lot of RDMA to
	 * the zero page.
	 */
	ret = rds_pin_pages(args->vec.addr & PAGE_MASK, nr_pages, pages, 1);
	if (ret < 0)
		goto out;

	nents = ret;
	sg = kcalloc(nents, sizeof(*sg), GFP_KERNEL);
	if (sg == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	WARN_ON(!nents);
	sg_init_table(sg, nents);

	/* Stick all pages into the scatterlist */
	for (i = 0 ; i < nents; i++)
		sg_set_page(&sg[i], pages[i], PAGE_SIZE, 0);

	rdsdebug("RDS: trans_private nents is %u\n", nents);

	/* Obtain a transport specific MR. If this succeeds, the
	 * s/g list is now owned by the MR.
	 * Note that dma_map() implies that pending writes are
	 * flushed to RAM, so no dma_sync is needed here. */
	trans_private = rs->rs_transport->get_mr(sg, nents, rs,
						 &mr->r_key);

	if (IS_ERR(trans_private)) {
		for (i = 0 ; i < nents; i++)
			put_page(sg_page(&sg[i]));
		kfree(sg);
		ret = PTR_ERR(trans_private);
		goto out;
	}

	mr->r_trans_private = trans_private;

	rdsdebug("RDS: get_mr put_user key is %x cookie_addr %p\n",
	       mr->r_key, (void *)(unsigned long) args->cookie_addr);

	/* The user may pass us an unaligned address, but we can only
	 * map page aligned regions. So we keep the offset, and build
	 * a 64bit cookie containing <R_Key, offset> and pass that
	 * around. */
	cookie = rds_rdma_make_cookie(mr->r_key, args->vec.addr & ~PAGE_MASK);
	if (cookie_ret)
		*cookie_ret = cookie;

	if (args->cookie_addr && put_user(cookie, (u64 __user *)(unsigned long) args->cookie_addr)) {
		ret = -EFAULT;
		goto out;
	}

	/* Inserting the new MR into the rbtree bumps its
	 * reference count. */
	spin_lock_irqsave(&rs->rs_rdma_lock, flags);
	found = rds_mr_tree_walk(&rs->rs_rdma_keys, mr->r_key, mr);
	spin_unlock_irqrestore(&rs->rs_rdma_lock, flags);

	BUG_ON(found && found != mr);

	rdsdebug("RDS: get_mr key is %x\n", mr->r_key);
	if (mr_ret) {
		atomic_inc(&mr->r_refcount);
		*mr_ret = mr;
	}

	ret = 0;
out:
	kfree(pages);
	if (mr)
		rds_mr_put(mr);
	return ret;
}

int rds_get_mr(struct rds_sock *rs, char __user *optval, int optlen)
{
	struct rds_get_mr_args args;

	if (optlen != sizeof(struct rds_get_mr_args))
		return -EINVAL;

	if (copy_from_user(&args, (struct rds_get_mr_args __user *)optval,
			   sizeof(struct rds_get_mr_args)))
		return -EFAULT;

	return __rds_rdma_map(rs, &args, NULL, NULL);
}

int rds_get_mr_for_dest(struct rds_sock *rs, char __user *optval, int optlen)
{
	struct rds_get_mr_for_dest_args args;
	struct rds_get_mr_args new_args;

	if (optlen != sizeof(struct rds_get_mr_for_dest_args))
		return -EINVAL;

	if (copy_from_user(&args, (struct rds_get_mr_for_dest_args __user *)optval,
			   sizeof(struct rds_get_mr_for_dest_args)))
		return -EFAULT;

	/*
	 * Initially, just behave like get_mr().
	 * TODO: Implement get_mr as wrapper around this
	 *	 and deprecate it.
	 */
	new_args.vec = args.vec;
	new_args.cookie_addr = args.cookie_addr;
	new_args.flags = args.flags;

	return __rds_rdma_map(rs, &new_args, NULL, NULL);
}

/*
 * Free the MR indicated by the given R_Key
 */
int rds_free_mr(struct rds_sock *rs, char __user *optval, int optlen)
{
	struct rds_free_mr_args args;
	struct rds_mr *mr;
	unsigned long flags;

	if (optlen != sizeof(struct rds_free_mr_args))
		return -EINVAL;

	if (copy_from_user(&args, (struct rds_free_mr_args __user *)optval,
			   sizeof(struct rds_free_mr_args)))
		return -EFAULT;

	/* Special case - a null cookie means flush all unused MRs */
	if (args.cookie == 0) {
		if (!rs->rs_transport || !rs->rs_transport->flush_mrs)
			return -EINVAL;
		rs->rs_transport->flush_mrs();
		return 0;
	}

	/* Look up the MR given its R_key and remove it from the rbtree
	 * so nobody else finds it.
	 * This should also prevent races with rds_rdma_unuse.
	 */
	spin_lock_irqsave(&rs->rs_rdma_lock, flags);
	mr = rds_mr_tree_walk(&rs->rs_rdma_keys, rds_rdma_cookie_key(args.cookie), NULL);
	if (mr) {
		rb_erase(&mr->r_rb_node, &rs->rs_rdma_keys);
		RB_CLEAR_NODE(&mr->r_rb_node);
		if (args.flags & RDS_RDMA_INVALIDATE)
			mr->r_invalidate = 1;
	}
	spin_unlock_irqrestore(&rs->rs_rdma_lock, flags);

	if (!mr)
		return -EINVAL;

	/*
	 * call rds_destroy_mr() ourselves so that we're sure it's done by the time
	 * we return.  If we let rds_mr_put() do it it might not happen until
	 * someone else drops their ref.
	 */
	rds_destroy_mr(mr);
	rds_mr_put(mr);
	return 0;
}

/*
 * This is called when we receive an extension header that
 * tells us this MR was used. It allows us to implement
 * use_once semantics
 */
void rds_rdma_unuse(struct rds_sock *rs, u32 r_key, int force)
{
	struct rds_mr *mr;
	unsigned long flags;
	int zot_me = 0;

	spin_lock_irqsave(&rs->rs_rdma_lock, flags);
	mr = rds_mr_tree_walk(&rs->rs_rdma_keys, r_key, NULL);
	if (mr && (mr->r_use_once || force)) {
		rb_erase(&mr->r_rb_node, &rs->rs_rdma_keys);
		RB_CLEAR_NODE(&mr->r_rb_node);
		zot_me = 1;
	} else if (mr)
		atomic_inc(&mr->r_refcount);
	spin_unlock_irqrestore(&rs->rs_rdma_lock, flags);

	/* May have to issue a dma_sync on this memory region.
	 * Note we could avoid this if the operation was a RDMA READ,
	 * but at this point we can't tell. */
	if (mr != NULL) {
		if (mr->r_trans->sync_mr)
			mr->r_trans->sync_mr(mr->r_trans_private, DMA_FROM_DEVICE);

		/* If the MR was marked as invalidate, this will
		 * trigger an async flush. */
		if (zot_me)
			rds_destroy_mr(mr);
		rds_mr_put(mr);
	}
}

void rds_rdma_free_op(struct rds_rdma_op *ro)
{
	unsigned int i;

	for (i = 0; i < ro->r_nents; i++) {
		struct page *page = sg_page(&ro->r_sg[i]);

		/* Mark page dirty if it was possibly modified, which
		 * is the case for a RDMA_READ which copies from remote
		 * to local memory */
		if (!ro->r_write) {
			BUG_ON(in_interrupt());
			set_page_dirty(page);
		}
		put_page(page);
	}

	kfree(ro->r_notifier);
	kfree(ro);
}

/*
 * args is a pointer to an in-kernel copy in the sendmsg cmsg.
 */
static struct rds_rdma_op *rds_rdma_prepare(struct rds_sock *rs,
					    struct rds_rdma_args *args)
{
	struct rds_iovec vec;
	struct rds_rdma_op *op = NULL;
	unsigned int nr_pages;
	unsigned int max_pages;
	unsigned int nr_bytes;
	struct page **pages = NULL;
	struct rds_iovec __user *local_vec;
	struct scatterlist *sg;
	unsigned int nr;
	unsigned int i, j;
	int ret;


	if (rs->rs_bound_addr == 0) {
		ret = -ENOTCONN; /* XXX not a great errno */
		goto out;
	}

	if (args->nr_local > UIO_MAXIOV) {
		ret = -EMSGSIZE;
		goto out;
	}

	nr_pages = 0;
	max_pages = 0;

	local_vec = (struct rds_iovec __user *)(unsigned long) args->local_vec_addr;

	/* figure out the number of pages in the vector */
	for (i = 0; i < args->nr_local; i++) {
		if (copy_from_user(&vec, &local_vec[i],
				   sizeof(struct rds_iovec))) {
			ret = -EFAULT;
			goto out;
		}

		nr = rds_pages_in_vec(&vec);
		if (nr == 0) {
			ret = -EINVAL;
			goto out;
		}

		max_pages = max(nr, max_pages);
		nr_pages += nr;
	}

	pages = kcalloc(max_pages, sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	op = kzalloc(offsetof(struct rds_rdma_op, r_sg[nr_pages]), GFP_KERNEL);
	if (op == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	op->r_write = !!(args->flags & RDS_RDMA_READWRITE);
	op->r_fence = !!(args->flags & RDS_RDMA_FENCE);
	op->r_notify = !!(args->flags & RDS_RDMA_NOTIFY_ME);
	op->r_recverr = rs->rs_recverr;
	WARN_ON(!nr_pages);
	sg_init_table(op->r_sg, nr_pages);

	if (op->r_notify || op->r_recverr) {
		/* We allocate an uninitialized notifier here, because
		 * we don't want to do that in the completion handler. We
		 * would have to use GFP_ATOMIC there, and don't want to deal
		 * with failed allocations.
		 */
		op->r_notifier = kmalloc(sizeof(struct rds_notifier), GFP_KERNEL);
		if (!op->r_notifier) {
			ret = -ENOMEM;
			goto out;
		}
		op->r_notifier->n_user_token = args->user_token;
		op->r_notifier->n_status = RDS_RDMA_SUCCESS;
	}

	/* The cookie contains the R_Key of the remote memory region, and
	 * optionally an offset into it. This is how we implement RDMA into
	 * unaligned memory.
	 * When setting up the RDMA, we need to add that offset to the
	 * destination address (which is really an offset into the MR)
	 * FIXME: We may want to move this into ib_rdma.c
	 */
	op->r_key = rds_rdma_cookie_key(args->cookie);
	op->r_remote_addr = args->remote_vec.addr + rds_rdma_cookie_offset(args->cookie);

	nr_bytes = 0;

	rdsdebug("RDS: rdma prepare nr_local %llu rva %llx rkey %x\n",
	       (unsigned long long)args->nr_local,
	       (unsigned long long)args->remote_vec.addr,
	       op->r_key);

	for (i = 0; i < args->nr_local; i++) {
		if (copy_from_user(&vec, &local_vec[i],
				   sizeof(struct rds_iovec))) {
			ret = -EFAULT;
			goto out;
		}

		nr = rds_pages_in_vec(&vec);
		if (nr == 0) {
			ret = -EINVAL;
			goto out;
		}

		rs->rs_user_addr = vec.addr;
		rs->rs_user_bytes = vec.bytes;

		/* did the user change the vec under us? */
		if (nr > max_pages || op->r_nents + nr > nr_pages) {
			ret = -EINVAL;
			goto out;
		}
		/* If it's a WRITE operation, we want to pin the pages for reading.
		 * If it's a READ operation, we need to pin the pages for writing.
		 */
		ret = rds_pin_pages(vec.addr & PAGE_MASK, nr, pages, !op->r_write);
		if (ret < 0)
			goto out;

		rdsdebug("RDS: nr_bytes %u nr %u vec.bytes %llu vec.addr %llx\n",
		       nr_bytes, nr, vec.bytes, vec.addr);

		nr_bytes += vec.bytes;

		for (j = 0; j < nr; j++) {
			unsigned int offset = vec.addr & ~PAGE_MASK;

			sg = &op->r_sg[op->r_nents + j];
			sg_set_page(sg, pages[j],
					min_t(unsigned int, vec.bytes, PAGE_SIZE - offset),
					offset);

			rdsdebug("RDS: sg->offset %x sg->len %x vec.addr %llx vec.bytes %llu\n",
			       sg->offset, sg->length, vec.addr, vec.bytes);

			vec.addr += sg->length;
			vec.bytes -= sg->length;
		}

		op->r_nents += nr;
	}


	if (nr_bytes > args->remote_vec.bytes) {
		rdsdebug("RDS nr_bytes %u remote_bytes %u do not match\n",
				nr_bytes,
				(unsigned int) args->remote_vec.bytes);
		ret = -EINVAL;
		goto out;
	}
	op->r_bytes = nr_bytes;

	ret = 0;
out:
	kfree(pages);
	if (ret) {
		if (op)
			rds_rdma_free_op(op);
		op = ERR_PTR(ret);
	}
	return op;
}

/*
 * The application asks for a RDMA transfer.
 * Extract all arguments and set up the rdma_op
 */
int rds_cmsg_rdma_args(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg)
{
	struct rds_rdma_op *op;

	if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct rds_rdma_args)) ||
	    rm->m_rdma_op != NULL)
		return -EINVAL;

	op = rds_rdma_prepare(rs, CMSG_DATA(cmsg));
	if (IS_ERR(op))
		return PTR_ERR(op);
	rds_stats_inc(s_send_rdma);
	rm->m_rdma_op = op;
	return 0;
}

/*
 * The application wants us to pass an RDMA destination (aka MR)
 * to the remote
 */
int rds_cmsg_rdma_dest(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg)
{
	unsigned long flags;
	struct rds_mr *mr;
	u32 r_key;
	int err = 0;

	if (cmsg->cmsg_len < CMSG_LEN(sizeof(rds_rdma_cookie_t)) ||
	    rm->m_rdma_cookie != 0)
		return -EINVAL;

	memcpy(&rm->m_rdma_cookie, CMSG_DATA(cmsg), sizeof(rm->m_rdma_cookie));

	/* We are reusing a previously mapped MR here. Most likely, the
	 * application has written to the buffer, so we need to explicitly
	 * flush those writes to RAM. Otherwise the HCA may not see them
	 * when doing a DMA from that buffer.
	 */
	r_key = rds_rdma_cookie_key(rm->m_rdma_cookie);

	spin_lock_irqsave(&rs->rs_rdma_lock, flags);
	mr = rds_mr_tree_walk(&rs->rs_rdma_keys, r_key, NULL);
	if (mr == NULL)
		err = -EINVAL;	/* invalid r_key */
	else
		atomic_inc(&mr->r_refcount);
	spin_unlock_irqrestore(&rs->rs_rdma_lock, flags);

	if (mr) {
		mr->r_trans->sync_mr(mr->r_trans_private, DMA_TO_DEVICE);
		rm->m_rdma_mr = mr;
	}
	return err;
}

/*
 * The application passes us an address range it wants to enable RDMA
 * to/from. We map the area, and save the <R_Key,offset> pair
 * in rm->m_rdma_cookie. This causes it to be sent along to the peer
 * in an extension header.
 */
int rds_cmsg_rdma_map(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg)
{
	if (cmsg->cmsg_len < CMSG_LEN(sizeof(struct rds_get_mr_args)) ||
	    rm->m_rdma_cookie != 0)
		return -EINVAL;

	return __rds_rdma_map(rs, CMSG_DATA(cmsg), &rm->m_rdma_cookie, &rm->m_rdma_mr);
}
