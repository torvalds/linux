// SPDX-License-Identifier: GPL-2.0
#ifndef IO_URING_BPF_FILTER_H
#define IO_URING_BPF_FILTER_H

#include <uapi/linux/io_uring/bpf_filter.h>

#ifdef CONFIG_IO_URING_BPF

int __io_uring_run_bpf_filters(struct io_restriction *res, struct io_kiocb *req);

int io_register_bpf_filter(struct io_restriction *res,
			   struct io_uring_bpf __user *arg);

void io_put_bpf_filters(struct io_restriction *res);

static inline int io_uring_run_bpf_filters(struct io_restriction *res,
					   struct io_kiocb *req)
{
	if (res->bpf_filters)
		return __io_uring_run_bpf_filters(res, req);

	return 0;
}

#else

static inline int io_register_bpf_filter(struct io_restriction *res,
					 struct io_uring_bpf __user *arg)
{
	return -EINVAL;
}
static inline int io_uring_run_bpf_filters(struct io_restriction *res,
					   struct io_kiocb *req)
{
	return 0;
}
static inline void io_put_bpf_filters(struct io_restriction *res)
{
}
#endif /* CONFIG_IO_URING_BPF */

#endif
