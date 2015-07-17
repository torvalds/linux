/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS2_BUFMAP_H
#define __PVFS2_BUFMAP_H

/* used to describe mapped buffers */
struct pvfs_bufmap_desc {
	void *uaddr;			/* user space address pointer */
	struct page **page_array;	/* array of mapped pages */
	int array_count;		/* size of above arrays */
	struct list_head list_link;
};

struct pvfs2_bufmap;

struct pvfs2_bufmap *pvfs2_bufmap_ref(void);
void pvfs2_bufmap_unref(struct pvfs2_bufmap *bufmap);

/*
 * pvfs_bufmap_size_query is now an inline function because buffer
 * sizes are not hardcoded
 */
int pvfs_bufmap_size_query(void);

int pvfs_bufmap_shift_query(void);

int pvfs_bufmap_initialize(struct PVFS_dev_map_desc *user_desc);

int get_bufmap_init(void);

void pvfs_bufmap_finalize(void);

int pvfs_bufmap_get(struct pvfs2_bufmap **mapp, int *buffer_index);

void pvfs_bufmap_put(struct pvfs2_bufmap *bufmap, int buffer_index);

int readdir_index_get(struct pvfs2_bufmap **mapp, int *buffer_index);

void readdir_index_put(struct pvfs2_bufmap *bufmap, int buffer_index);

int pvfs_bufmap_copy_iovec_from_user(struct pvfs2_bufmap *bufmap,
				     int buffer_index,
				     const struct iovec *iov,
				     unsigned long nr_segs,
				     size_t size);

int pvfs_bufmap_copy_iovec_from_kernel(struct pvfs2_bufmap *bufmap,
				       int buffer_index,
				       const struct iovec *iov,
				       unsigned long nr_segs,
				       size_t size);

int pvfs_bufmap_copy_to_user_iovec(struct pvfs2_bufmap *bufmap,
				   int buffer_index,
				   const struct iovec *iov,
				   unsigned long nr_segs,
				   size_t size);

int pvfs_bufmap_copy_to_kernel_iovec(struct pvfs2_bufmap *bufmap,
				     int buffer_index,
				     const struct iovec *iov,
				     unsigned long nr_segs,
				     size_t size);

size_t pvfs_bufmap_copy_to_user_task_iovec(struct task_struct *tsk,
					   struct iovec *iovec,
					   unsigned long nr_segs,
					   struct pvfs2_bufmap *bufmap,
					   int buffer_index,
					   size_t bytes_to_be_copied);

#endif /* __PVFS2_BUFMAP_H */
