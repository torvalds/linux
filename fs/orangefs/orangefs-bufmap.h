/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __ORANGEFS_BUFMAP_H
#define __ORANGEFS_BUFMAP_H

struct orangefs_bufmap;

int orangefs_bufmap_size_query(void);

int orangefs_bufmap_shift_query(void);

int orangefs_bufmap_initialize(struct ORANGEFS_dev_map_desc *user_desc);

int orangefs_get_bufmap_init(void);

void orangefs_bufmap_finalize(void);

int orangefs_bufmap_get(struct orangefs_bufmap **mapp, int *buffer_index);

void orangefs_bufmap_put(struct orangefs_bufmap *bufmap, int buffer_index);

int orangefs_readdir_index_get(struct orangefs_bufmap **mapp, int *buffer_index);

void orangefs_readdir_index_put(struct orangefs_bufmap *bufmap, int buffer_index);

int orangefs_bufmap_copy_from_iovec(struct orangefs_bufmap *bufmap,
				struct iov_iter *iter,
				int buffer_index,
				size_t size);

int orangefs_bufmap_copy_to_iovec(struct orangefs_bufmap *bufmap,
			      struct iov_iter *iter,
			      int buffer_index,
			      size_t size);

size_t orangefs_bufmap_copy_to_user_task_iovec(struct task_struct *tsk,
					   struct iovec *iovec,
					   unsigned long nr_segs,
					   struct orangefs_bufmap *bufmap,
					   int buffer_index,
					   size_t bytes_to_be_copied);

#endif /* __ORANGEFS_BUFMAP_H */
