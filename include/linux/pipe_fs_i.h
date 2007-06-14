#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

#define PIPEFS_MAGIC 0x50495045

#define PIPE_BUFFERS (16)

#define PIPE_BUF_FLAG_LRU	0x01	/* page is on the LRU */
#define PIPE_BUF_FLAG_ATOMIC	0x02	/* was atomically mapped */
#define PIPE_BUF_FLAG_GIFT	0x04	/* page is a gift */

struct pipe_buffer {
	struct page *page;
	unsigned int offset, len;
	const struct pipe_buf_operations *ops;
	unsigned int flags;
	unsigned long private;
};

struct pipe_inode_info {
	wait_queue_head_t wait;
	unsigned int nrbufs, curbuf;
	struct page *tmp_page;
	unsigned int readers;
	unsigned int writers;
	unsigned int waiting_writers;
	unsigned int r_counter;
	unsigned int w_counter;
	struct fasync_struct *fasync_readers;
	struct fasync_struct *fasync_writers;
	struct inode *inode;
	struct pipe_buffer bufs[PIPE_BUFFERS];
};

/*
 * Note on the nesting of these functions:
 *
 * ->confirm()
 *	->steal()
 *	...
 *	->map()
 *	...
 *	->unmap()
 *
 * That is, ->map() must be called on a confirmed buffer,
 * same goes for ->steal().
 */
struct pipe_buf_operations {
	int can_merge;
	void * (*map)(struct pipe_inode_info *, struct pipe_buffer *, int);
	void (*unmap)(struct pipe_inode_info *, struct pipe_buffer *, void *);
	int (*confirm)(struct pipe_inode_info *, struct pipe_buffer *);
	void (*release)(struct pipe_inode_info *, struct pipe_buffer *);
	int (*steal)(struct pipe_inode_info *, struct pipe_buffer *);
	void (*get)(struct pipe_inode_info *, struct pipe_buffer *);
};

/* Differs from PIPE_BUF in that PIPE_SIZE is the length of the actual
   memory allocation, whereas PIPE_BUF makes atomicity guarantees.  */
#define PIPE_SIZE		PAGE_SIZE

/* Drop the inode semaphore and wait for a pipe event, atomically */
void pipe_wait(struct pipe_inode_info *pipe);

struct pipe_inode_info * alloc_pipe_info(struct inode * inode);
void free_pipe_info(struct inode * inode);
void __free_pipe_info(struct pipe_inode_info *);

/* Generic pipe buffer ops functions */
void *generic_pipe_buf_map(struct pipe_inode_info *, struct pipe_buffer *, int);
void generic_pipe_buf_unmap(struct pipe_inode_info *, struct pipe_buffer *, void *);
void generic_pipe_buf_get(struct pipe_inode_info *, struct pipe_buffer *);
int generic_pipe_buf_confirm(struct pipe_inode_info *, struct pipe_buffer *);
int generic_pipe_buf_steal(struct pipe_inode_info *, struct pipe_buffer *);

#endif
