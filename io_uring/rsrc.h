// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_RSRC_H
#define IOU_RSRC_H

#include <net/af_unix.h>

#include "alloc_cache.h"

#define IO_NODE_ALLOC_CACHE_MAX 32

#define IO_RSRC_TAG_TABLE_SHIFT	(PAGE_SHIFT - 3)
#define IO_RSRC_TAG_TABLE_MAX	(1U << IO_RSRC_TAG_TABLE_SHIFT)
#define IO_RSRC_TAG_TABLE_MASK	(IO_RSRC_TAG_TABLE_MAX - 1)

enum {
	IORING_RSRC_FILE		= 0,
	IORING_RSRC_BUFFER		= 1,
};

struct io_rsrc_put {
	u64 tag;
	union {
		void *rsrc;
		struct file *file;
		struct io_mapped_ubuf *buf;
	};
};

typedef void (rsrc_put_fn)(struct io_ring_ctx *ctx, struct io_rsrc_put *prsrc);

struct io_rsrc_data {
	struct io_ring_ctx		*ctx;

	u64				**tags;
	unsigned int			nr;
	u16				rsrc_type;
	bool				quiesce;
};

struct io_rsrc_node {
	union {
		struct io_cache_entry		cache;
		struct io_ring_ctx		*ctx;
	};
	int				refs;
	bool				empty;
	u16				type;
	struct list_head		node;
	struct io_rsrc_put		item;
};

struct io_mapped_ubuf {
	u64		ubuf;
	u64		ubuf_end;
	unsigned int	nr_bvecs;
	unsigned long	acct_pages;
	struct bio_vec	bvec[] __counted_by(nr_bvecs);
};

void io_rsrc_node_ref_zero(struct io_rsrc_node *node);
void io_rsrc_node_destroy(struct io_ring_ctx *ctx, struct io_rsrc_node *ref_node);
struct io_rsrc_node *io_rsrc_node_alloc(struct io_ring_ctx *ctx);
int io_queue_rsrc_removal(struct io_rsrc_data *data, unsigned idx, void *rsrc);

int io_import_fixed(int ddir, struct iov_iter *iter,
			   struct io_mapped_ubuf *imu,
			   u64 buf_addr, size_t len);

void __io_sqe_buffers_unregister(struct io_ring_ctx *ctx);
int io_sqe_buffers_unregister(struct io_ring_ctx *ctx);
int io_sqe_buffers_register(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned int nr_args, u64 __user *tags);
void __io_sqe_files_unregister(struct io_ring_ctx *ctx);
int io_sqe_files_unregister(struct io_ring_ctx *ctx);
int io_sqe_files_register(struct io_ring_ctx *ctx, void __user *arg,
			  unsigned nr_args, u64 __user *tags);

int __io_scm_file_account(struct io_ring_ctx *ctx, struct file *file);

#if defined(CONFIG_UNIX)
static inline bool io_file_need_scm(struct file *filp)
{
	return !!unix_get_socket(filp);
}
#else
static inline bool io_file_need_scm(struct file *filp)
{
	return false;
}
#endif

static inline int io_scm_file_account(struct io_ring_ctx *ctx,
				      struct file *file)
{
	if (likely(!io_file_need_scm(file)))
		return 0;
	return __io_scm_file_account(ctx, file);
}

int io_register_files_update(struct io_ring_ctx *ctx, void __user *arg,
			     unsigned nr_args);
int io_register_rsrc_update(struct io_ring_ctx *ctx, void __user *arg,
			    unsigned size, unsigned type);
int io_register_rsrc(struct io_ring_ctx *ctx, void __user *arg,
			unsigned int size, unsigned int type);

static inline void io_put_rsrc_node(struct io_ring_ctx *ctx, struct io_rsrc_node *node)
{
	lockdep_assert_held(&ctx->uring_lock);

	if (node && !--node->refs)
		io_rsrc_node_ref_zero(node);
}

static inline void io_req_put_rsrc_locked(struct io_kiocb *req,
					  struct io_ring_ctx *ctx)
{
	io_put_rsrc_node(ctx, req->rsrc_node);
}

static inline void io_charge_rsrc_node(struct io_ring_ctx *ctx,
				       struct io_rsrc_node *node)
{
	node->refs++;
}

static inline void io_req_set_rsrc_node(struct io_kiocb *req,
					struct io_ring_ctx *ctx,
					unsigned int issue_flags)
{
	if (!req->rsrc_node) {
		io_ring_submit_lock(ctx, issue_flags);

		lockdep_assert_held(&ctx->uring_lock);

		req->rsrc_node = ctx->rsrc_node;
		io_charge_rsrc_node(ctx, ctx->rsrc_node);
		io_ring_submit_unlock(ctx, issue_flags);
	}
}

static inline u64 *io_get_tag_slot(struct io_rsrc_data *data, unsigned int idx)
{
	unsigned int off = idx & IO_RSRC_TAG_TABLE_MASK;
	unsigned int table_idx = idx >> IO_RSRC_TAG_TABLE_SHIFT;

	return &data->tags[table_idx][off];
}

static inline int io_rsrc_init(struct io_ring_ctx *ctx)
{
	ctx->rsrc_node = io_rsrc_node_alloc(ctx);
	return ctx->rsrc_node ? 0 : -ENOMEM;
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
