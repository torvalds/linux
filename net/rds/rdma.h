#ifndef _RDS_RDMA_H
#define _RDS_RDMA_H

#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>

#include "rds.h"

struct rds_mr {
	struct rb_node		r_rb_node;
	atomic_t		r_refcount;
	u32			r_key;

	/* A copy of the creation flags */
	unsigned int		r_use_once:1;
	unsigned int		r_invalidate:1;
	unsigned int		r_write:1;

	/* This is for RDS_MR_DEAD.
	 * It would be nice & consistent to make this part of the above
	 * bit field here, but we need to use test_and_set_bit.
	 */
	unsigned long		r_state;
	struct rds_sock		*r_sock; /* back pointer to the socket that owns us */
	struct rds_transport	*r_trans;
	void			*r_trans_private;
};

/* Flags for mr->r_state */
#define RDS_MR_DEAD		0

struct rds_rdma_op {
	u32			r_key;
	u64			r_remote_addr;
	unsigned int		r_write:1;
	unsigned int		r_fence:1;
	unsigned int		r_notify:1;
	unsigned int		r_recverr:1;
	unsigned int		r_mapped:1;
	struct rds_notifier	*r_notifier;
	unsigned int		r_bytes;
	unsigned int		r_nents;
	unsigned int		r_count;
	struct scatterlist	r_sg[0];
};

static inline rds_rdma_cookie_t rds_rdma_make_cookie(u32 r_key, u32 offset)
{
	return r_key | (((u64) offset) << 32);
}

static inline u32 rds_rdma_cookie_key(rds_rdma_cookie_t cookie)
{
	return cookie;
}

static inline u32 rds_rdma_cookie_offset(rds_rdma_cookie_t cookie)
{
	return cookie >> 32;
}

int rds_get_mr(struct rds_sock *rs, char __user *optval, int optlen);
int rds_get_mr_for_dest(struct rds_sock *rs, char __user *optval, int optlen);
int rds_free_mr(struct rds_sock *rs, char __user *optval, int optlen);
void rds_rdma_drop_keys(struct rds_sock *rs);
int rds_cmsg_rdma_args(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
int rds_cmsg_rdma_dest(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
int rds_cmsg_rdma_args(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
int rds_cmsg_rdma_map(struct rds_sock *rs, struct rds_message *rm,
			  struct cmsghdr *cmsg);
void rds_rdma_free_op(struct rds_rdma_op *ro);
void rds_rdma_send_complete(struct rds_message *rm, int);

extern void __rds_put_mr_final(struct rds_mr *mr);
static inline void rds_mr_put(struct rds_mr *mr)
{
	if (atomic_dec_and_test(&mr->r_refcount))
		__rds_put_mr_final(mr);
}

#endif
