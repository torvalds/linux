// SPDX-License-Identifier: GPL-2.0
#ifndef IORING_QUERY_H
#define IORING_QUERY_H

#include <linux/io_uring_types.h>

int io_query(struct io_ring_ctx *ctx, void __user *arg, unsigned nr_args);

#endif
