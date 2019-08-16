/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IO_H
#define _BCACHEFS_FS_IO_H

#ifndef NO_BCACHEFS_FS

#include "buckets.h"
#include "io_types.h"

#include <linux/uio.h>

struct quota_res;

int bch2_extent_update(struct btree_trans *,
		       struct bch_inode_info *,
		       struct disk_reservation *,
		       struct quota_res *,
		       struct btree_iter *,
		       struct bkey_i *,
		       u64, bool, bool, s64 *);
int bch2_fpunch_at(struct btree_trans *, struct btree_iter *,
		   struct bpos, struct bch_inode_info *, u64);

int __must_check bch2_write_inode_size(struct bch_fs *,
				       struct bch_inode_info *,
				       loff_t, unsigned);

int bch2_writepage(struct page *, struct writeback_control *);
int bch2_read_folio(struct file *, struct folio *);

int bch2_writepages(struct address_space *, struct writeback_control *);
void bch2_readahead(struct readahead_control *);

int bch2_write_begin(struct file *, struct address_space *, loff_t,
		     unsigned, struct page **, void **);
int bch2_write_end(struct file *, struct address_space *, loff_t,
		   unsigned, unsigned, struct page *, void *);

ssize_t bch2_read_iter(struct kiocb *, struct iov_iter *);
ssize_t bch2_write_iter(struct kiocb *, struct iov_iter *);

int bch2_fsync(struct file *, loff_t, loff_t, int);

int bch2_truncate(struct bch_inode_info *, struct iattr *);
long bch2_fallocate_dispatch(struct file *, int, loff_t, loff_t);

loff_t bch2_remap_file_range(struct file *, loff_t, struct file *,
			     loff_t, loff_t, unsigned);

loff_t bch2_llseek(struct file *, loff_t, int);

vm_fault_t bch2_page_fault(struct vm_fault *);
vm_fault_t bch2_page_mkwrite(struct vm_fault *);
void bch2_invalidate_folio(struct folio *, size_t, size_t);
bool bch2_release_folio(struct folio *, gfp_t);

void bch2_fs_fsio_exit(struct bch_fs *);
int bch2_fs_fsio_init(struct bch_fs *);
#else
static inline void bch2_fs_fsio_exit(struct bch_fs *c) {}
static inline int bch2_fs_fsio_init(struct bch_fs *c) { return 0; }
#endif

#endif /* _BCACHEFS_FS_IO_H */
