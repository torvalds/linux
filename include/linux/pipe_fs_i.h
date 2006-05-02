#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

#define PIPEFS_MAGIC 0x50495045

#define PIPE_BUFFERS (16)

#define PIPE_BUF_FLAG_ATOMIC	0x01	/* was atomically mapped */
#define PIPE_BUF_FLAG_GIFT	0x02	/* page is a gift */

struct pipe_buffer {
	struct page *page;
	unsigned int offset, len;
	struct pipe_buf_operations *ops;
	unsigned int flags;
};

/*
 * Note on the nesting of these functions:
 *
 * ->pin()
 *	->steal()
 *	...
 *	->map()
 *	...
 *	->unmap()
 *
 * That is, ->map() must be called on a pinned buffer, same goes for ->steal().
 */
struct pipe_buf_operations {
	int can_merge;
	void * (*map)(struct pipe_inode_info *, struct pipe_buffer *, int);
	void (*unmap)(struct pipe_inode_info *, struct pipe_buffer *, void *);
	int (*pin)(struct pipe_inode_info *, struct pipe_buffer *);
	void (*release)(struct pipe_inode_info *, struct pipe_buffer *);
	int (*steal)(struct pipe_inode_info *, struct pipe_buffer *);
	void (*get)(struct pipe_inode_info *, struct pipe_buffer *);
};

struct pipe_inode_info {
	wait_queue_head_t wait;
	unsigned int nrbufs, curbuf;
	struct pipe_buffer bufs[PIPE_BUFFERS];
	struct page *tmp_page;
	unsigned int start;
	unsigned int readers;
	unsigned int writers;
	unsigned int waiting_writers;
	unsigned int r_counter;
	unsigned int w_counter;
	struct fasync_struct *fasync_readers;
	struct fasync_struct *fasync_writers;
	struct inode *inode;
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
int generic_pipe_buf_pin(struct pipe_inode_info *, struct pipe_buffer *);

/*
 * splice is tied to pipes as a transport (at least for now), so we'll just
 * add the splice flags here.
 */
#define SPLICE_F_MOVE	(0x01)	/* move pages instead of copying */
#define SPLICE_F_NONBLOCK (0x02) /* don't block on the pipe splicing (but */
				 /* we may still block on the fd we splice */
				 /* from/to, of course */
#define SPLICE_F_MORE	(0x04)	/* expect more data */
#define SPLICE_F_GIFT	(0x08)	/* pages passed in are a gift */

/*
 * Passed to the actors
 */
struct splice_desc {
	unsigned int len, total_len;	/* current and remaining length */
	unsigned int flags;		/* splice flags */
	struct file *file;		/* file to read/write */
	loff_t pos;			/* file position */
};

typedef int (splice_actor)(struct pipe_inode_info *, struct pipe_buffer *,
			   struct splice_desc *);

extern ssize_t splice_from_pipe(struct pipe_inode_info *, struct file *,
				loff_t *, size_t, unsigned int,
				splice_actor *);

#endif
