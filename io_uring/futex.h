// SPDX-License-Identifier: GPL-2.0

#include "cancel.h"

int io_futex_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);
int io_futex_wait(struct io_kiocb *req, unsigned int issue_flags);
int io_futex_wake(struct io_kiocb *req, unsigned int issue_flags);

#if defined(CONFIG_FUTEX)
int io_futex_cancel(struct io_ring_ctx *ctx, struct io_cancel_data *cd,
		    unsigned int issue_flags);
bool io_futex_remove_all(struct io_ring_ctx *ctx, struct task_struct *task,
			 bool cancel_all);
void io_futex_cache_init(struct io_ring_ctx *ctx);
void io_futex_cache_free(struct io_ring_ctx *ctx);
#else
static inline int io_futex_cancel(struct io_ring_ctx *ctx,
				  struct io_cancel_data *cd,
				  unsigned int issue_flags)
{
	return 0;
}
static inline bool io_futex_remove_all(struct io_ring_ctx *ctx,
				       struct task_struct *task, bool cancel_all)
{
	return false;
}
static inline void io_futex_cache_init(struct io_ring_ctx *ctx)
{
}
static inline void io_futex_cache_free(struct io_ring_ctx *ctx)
{
}
#endif
