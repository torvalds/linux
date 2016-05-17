/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __ORANGEFS_BUFMAP_H
#define __ORANGEFS_BUFMAP_H

int orangefs_bufmap_size_query(void);

int orangefs_bufmap_shift_query(void);

int orangefs_bufmap_initialize(struct ORANGEFS_dev_map_desc *user_desc);

void orangefs_bufmap_finalize(void);

void orangefs_bufmap_run_down(void);

int orangefs_bufmap_get(void);

void orangefs_bufmap_put(int buffer_index);

int orangefs_readdir_index_get(void);

void orangefs_readdir_index_put(int buffer_index);

int orangefs_bufmap_copy_from_iovec(struct iov_iter *iter,
				int buffer_index,
				size_t size);

int orangefs_bufmap_copy_to_iovec(struct iov_iter *iter,
			      int buffer_index,
			      size_t size);

#endif /* __ORANGEFS_BUFMAP_H */
