/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IO_DIRECT_H
#define _BCACHEFS_FS_IO_DIRECT_H

#ifndef NO_BCACHEFS_FS
ssize_t bch2_direct_write(struct kiocb *, struct iov_iter *);
ssize_t bch2_read_iter(struct kiocb *, struct iov_iter *);

void bch2_fs_fs_io_direct_exit(struct bch_fs *);
int bch2_fs_fs_io_direct_init(struct bch_fs *);
#else
static inline void bch2_fs_fs_io_direct_exit(struct bch_fs *c) {}
static inline int bch2_fs_fs_io_direct_init(struct bch_fs *c) { return 0; }
#endif

#endif /* _BCACHEFS_FS_IO_DIRECT_H */
