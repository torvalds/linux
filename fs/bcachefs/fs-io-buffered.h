/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IO_BUFFERED_H
#define _BCACHEFS_FS_IO_BUFFERED_H

#ifndef NO_BCACHEFS_FS

int bch2_read_single_folio(struct folio *, struct address_space *);
int bch2_read_folio(struct file *, struct folio *);

int bch2_writepages(struct address_space *, struct writeback_control *);
void bch2_readahead(struct readahead_control *);

int bch2_write_begin(struct file *, struct address_space *, loff_t pos,
		     unsigned len, struct folio **, void **);
int bch2_write_end(struct file *, struct address_space *, loff_t,
		   unsigned len, unsigned copied, struct folio *, void *);

ssize_t bch2_write_iter(struct kiocb *, struct iov_iter *);

void bch2_fs_fs_io_buffered_exit(struct bch_fs *);
int bch2_fs_fs_io_buffered_init(struct bch_fs *);
#else
static inline void bch2_fs_fs_io_buffered_exit(struct bch_fs *c) {}
static inline int bch2_fs_fs_io_buffered_init(struct bch_fs *c) { return 0; }
#endif

#endif /* _BCACHEFS_FS_IO_BUFFERED_H */
