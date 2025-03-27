/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_DIRECT_IO_H
#define BTRFS_DIRECT_IO_H

#include <linux/types.h>

struct kiocb;

int __init btrfs_init_dio(void);
void __cold btrfs_destroy_dio(void);

ssize_t btrfs_direct_write(struct kiocb *iocb, struct iov_iter *from);
ssize_t btrfs_direct_read(struct kiocb *iocb, struct iov_iter *to);

#endif /* BTRFS_DIRECT_IO_H */
