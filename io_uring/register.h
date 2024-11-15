// SPDX-License-Identifier: GPL-2.0
#ifndef IORING_REGISTER_H
#define IORING_REGISTER_H

int io_eventfd_unregister(struct io_ring_ctx *ctx);
int io_unregister_personality(struct io_ring_ctx *ctx, unsigned id);
struct file *io_uring_register_get_file(unsigned int fd, bool registered);

#endif
