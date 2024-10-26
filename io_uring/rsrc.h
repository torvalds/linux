// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_RSRC_H
#define IOU_RSRC_H

#define IO_NODE_ALLOC_CACHE_MAX 32

#define IO_RSRC_TAG_TABLE_SHIFT	(PAGE_SHIFT - 3)
#define IO_RSRC_TAG_TABLE_MAX	(1U << IO_RSRC_TAG_TABLE_SHIFT)
#define IO_RSRC_TAG_TABLE_MASK	(IO_RSRC_TAG_TABLE_MAX - 1)

enum {
	IORING_RSRC_FILE		= 0,
	IORING_RSRC_BUFFER		= 1,
};

struct io_rsrc_data {
	unsigned int			nr;
	struct io_rsrc_node		**nodes;
};

struct io_rsrc_node {
	struct io_ring_ctx		*ctx;
	int				refs;
	u16				type;

	u64 tag;
	union {
		unsigned long file_ptr;
		struct io_mapped_ubuf *buf;
	};
};

struct io_mapped_ubuf {
	u64		ubuf;
	unsigned int	len;
	unsigned int	nr_bvecs;
	unsigned int    folio_shift;
	refcount_t	refs;
	unsigned long	acct_pages;
	struct bio_vec	bvec[] __counted_by(nr_bvecs);
};

struct io_imu_folio_data {
	/* Head folio can be partially included in the fixed buf */
	unsigned int	nr_pages_head;
	/* For non-head/tail folios, has to be fully included */
	unsigned int	nr_pages_mid;
	unsigned int	folio_shift;
};

struct io_rsrc_node *io_rsrc_node_alloc(struct io_ring_ctx *ctx, int type);
void io_free_rsrc_node(struct io_rsrc_node *node);

int io_import_fixed(int ddir, struct iov_iter *iter,
			   struct io_mapped_ubuf *imu,
			   u64 buf_addr, size_t len);

int io_register_clone_buffers(struct io_ring_ctx *ctx, void __user *arg);
int io_sqe_buffers_unregister(struct io_ring_ctx *ctx);
int io_sqe_buffers_register(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned int nr_args, u64 __user *tags);
int io_sqe_files_unregister(struct io_ring_ctx *ctx);
int io_sqe_files_register(struct io_ring_ctx *ctx, void __user *arg,
			  unsigned nr_args, u64 __user *tags);

int io_register_files_update(struct io_ring_ctx *ctx, void __user *arg,
			     unsigned nr_args);
int io_register_rsrc_update(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned size, unsigned type);
int io_register_rsrc(struct io_ring_ctx *ctx, void __user *arg,
			unsigned int size, unsigned int type);

static inline void io_put_rsrc_node(struct io_rsrc_node *node)
{
	if (node && !--node->refs)
		io_free_rsrc_node(node);
}

static inline void io_req_put_rsrc_nodes(struct io_kiocb *req)
{
	io_put_rsrc_node(req->rsrc_nodes[IORING_RSRC_FILE]);
	io_put_rsrc_node(req->rsrc_nodes[IORING_RSRC_BUFFER]);
}

static inline void io_req_assign_rsrc_node(struct io_kiocb *req,
					   struct io_rsrc_node *node)
{
	node->refs++;
	req->rsrc_nodes[node->type] = node;
}

int io_files_update(struct io_kiocb *req, unsigned int issue_flags);
int io_files_update_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);

int __io_account_mem(struct user_struct *user, unsigned long nr_pages);

static inline void __io_unaccount_mem(struct user_struct *user,
				      unsigned long nr_pages)
{
	atomic_long_sub(nr_pages, &user->locked_vm);
}

#endif
