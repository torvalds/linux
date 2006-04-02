#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

#define PIPEFS_MAGIC 0x50495045

#define PIPE_BUFFERS (16)

struct pipe_buffer {
	struct page *page;
	unsigned int offset, len;
	struct pipe_buf_operations *ops;
};

struct pipe_buf_operations {
	int can_merge;
	void * (*map)(struct file *, struct pipe_inode_info *, struct pipe_buffer *);
	void (*unmap)(struct pipe_inode_info *, struct pipe_buffer *);
	void (*release)(struct pipe_inode_info *, struct pipe_buffer *);
	int (*steal)(struct pipe_inode_info *, struct pipe_buffer *);
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
};

/* Differs from PIPE_BUF in that PIPE_SIZE is the length of the actual
   memory allocation, whereas PIPE_BUF makes atomicity guarantees.  */
#define PIPE_SIZE		PAGE_SIZE

#define PIPE_MUTEX(inode)	(&(inode).i_mutex)
#define PIPE_WAIT(inode)	(&(inode).i_pipe->wait)
#define PIPE_READERS(inode)	((inode).i_pipe->readers)
#define PIPE_WRITERS(inode)	((inode).i_pipe->writers)
#define PIPE_WAITING_WRITERS(inode)	((inode).i_pipe->waiting_writers)
#define PIPE_RCOUNTER(inode)	((inode).i_pipe->r_counter)
#define PIPE_WCOUNTER(inode)	((inode).i_pipe->w_counter)
#define PIPE_FASYNC_READERS(inode)     (&((inode).i_pipe->fasync_readers))
#define PIPE_FASYNC_WRITERS(inode)     (&((inode).i_pipe->fasync_writers))

/* Drop the inode semaphore and wait for a pipe event, atomically */
void pipe_wait(struct inode * inode);

struct inode* pipe_new(struct inode* inode);
void free_pipe_info(struct inode* inode);

/*
 * splice is tied to pipes as a transport (at least for now), so we'll just
 * add the splice flags here.
 */
#define SPLICE_F_MOVE	(0x01)	/* move pages instead of copying */
#define SPLICE_F_NONBLOCK (0x02) /* don't block on the pipe splicing (but */
				 /* we may still block on the fd we splice */
				 /* from/to, of course */

#endif
