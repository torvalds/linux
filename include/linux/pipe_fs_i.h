/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

#define PIPE_DEF_BUFFERS	16

#define PIPE_BUF_FLAG_LRU	0x01	/* page is on the LRU */
#define PIPE_BUF_FLAG_ATOMIC	0x02	/* was atomically mapped */
#define PIPE_BUF_FLAG_GIFT	0x04	/* page is a gift */
#define PIPE_BUF_FLAG_PACKET	0x08	/* read() as a packet */
#define PIPE_BUF_FLAG_CAN_MERGE	0x10	/* can merge buffers */
#define PIPE_BUF_FLAG_WHOLE	0x20	/* read() must return entire buffer or error */
#ifdef CONFIG_WATCH_QUEUE
#define PIPE_BUF_FLAG_LOSS	0x40	/* Message loss happened after this buffer */
#endif

/**
 *	struct pipe_buffer - a linux kernel pipe buffer
 *	@page: the page containing the data for the pipe buffer
 *	@offset: offset of data inside the @page
 *	@len: length of data inside the @page
 *	@ops: operations associated with this buffer. See @pipe_buf_operations.
 *	@flags: pipe buffer flags. See above.
 *	@private: private data owned by the ops.
 **/
struct pipe_buffer {
	struct page *page;
	unsigned int offset, len;
	const struct pipe_buf_operations *ops;
	unsigned int flags;
	unsigned long private;
};

/**
 *	struct pipe_inode_info - a linux kernel pipe
 *	@mutex: mutex protecting the whole thing
 *	@rd_wait: reader wait point in case of empty pipe
 *	@wr_wait: writer wait point in case of full pipe
 *	@head: The point of buffer production
 *	@tail: The point of buffer consumption
 *	@note_loss: The next read() should insert a data-lost message
 *	@max_usage: The maximum number of slots that may be used in the ring
 *	@ring_size: total number of buffers (should be a power of 2)
 *	@nr_accounted: The amount this pipe accounts for in user->pipe_bufs
 *	@tmp_page: cached released page
 *	@readers: number of current readers of this pipe
 *	@writers: number of current writers of this pipe
 *	@files: number of struct file referring this pipe (protected by ->i_lock)
 *	@r_counter: reader counter
 *	@w_counter: writer counter
 *	@fasync_readers: reader side fasync
 *	@fasync_writers: writer side fasync
 *	@bufs: the circular array of pipe buffers
 *	@user: the user who created this pipe
 *	@watch_queue: If this pipe is a watch_queue, this is the stuff for that
 **/
struct pipe_inode_info {
	struct mutex mutex;
	wait_queue_head_t rd_wait, wr_wait;
	unsigned int head;
	unsigned int tail;
	unsigned int max_usage;
	unsigned int ring_size;
#ifdef CONFIG_WATCH_QUEUE
	bool note_loss;
#endif
	unsigned int nr_accounted;
	unsigned int readers;
	unsigned int writers;
	unsigned int files;
	unsigned int r_counter;
	unsigned int w_counter;
	struct page *tmp_page;
	struct fasync_struct *fasync_readers;
	struct fasync_struct *fasync_writers;
	struct pipe_buffer *bufs;
	struct user_struct *user;
#ifdef CONFIG_WATCH_QUEUE
	struct watch_queue *watch_queue;
#endif
};

/*
 * Note on the nesting of these functions:
 *
 * ->confirm()
 *	->try_steal()
 *
 * That is, ->try_steal() must be called on a confirmed buffer.  See below for
 * the meaning of each operation.  Also see the kerneldoc in fs/pipe.c for the
 * pipe and generic variants of these hooks.
 */
struct pipe_buf_operations {
	/*
	 * ->confirm() verifies that the data in the pipe buffer is there
	 * and that the contents are good. If the pages in the pipe belong
	 * to a file system, we may need to wait for IO completion in this
	 * hook. Returns 0 for good, or a negative error value in case of
	 * error.  If not present all pages are considered good.
	 */
	int (*confirm)(struct pipe_inode_info *, struct pipe_buffer *);

	/*
	 * When the contents of this pipe buffer has been completely
	 * consumed by a reader, ->release() is called.
	 */
	void (*release)(struct pipe_inode_info *, struct pipe_buffer *);

	/*
	 * Attempt to take ownership of the pipe buffer and its contents.
	 * ->try_steal() returns %true for success, in which case the contents
	 * of the pipe (the buf->page) is locked and now completely owned by the
	 * caller. The page may then be transferred to a different mapping, the
	 * most often used case is insertion into different file address space
	 * cache.
	 */
	bool (*try_steal)(struct pipe_inode_info *, struct pipe_buffer *);

	/*
	 * Get a reference to the pipe buffer.
	 */
	bool (*get)(struct pipe_inode_info *, struct pipe_buffer *);
};

/**
 * pipe_empty - Return true if the pipe is empty
 * @head: The pipe ring head pointer
 * @tail: The pipe ring tail pointer
 */
static inline bool pipe_empty(unsigned int head, unsigned int tail)
{
	return head == tail;
}

/**
 * pipe_occupancy - Return number of slots used in the pipe
 * @head: The pipe ring head pointer
 * @tail: The pipe ring tail pointer
 */
static inline unsigned int pipe_occupancy(unsigned int head, unsigned int tail)
{
	return head - tail;
}

/**
 * pipe_full - Return true if the pipe is full
 * @head: The pipe ring head pointer
 * @tail: The pipe ring tail pointer
 * @limit: The maximum amount of slots available.
 */
static inline bool pipe_full(unsigned int head, unsigned int tail,
			     unsigned int limit)
{
	return pipe_occupancy(head, tail) >= limit;
}

/**
 * pipe_space_for_user - Return number of slots available to userspace
 * @head: The pipe ring head pointer
 * @tail: The pipe ring tail pointer
 * @pipe: The pipe info structure
 */
static inline unsigned int pipe_space_for_user(unsigned int head, unsigned int tail,
					       struct pipe_inode_info *pipe)
{
	unsigned int p_occupancy, p_space;

	p_occupancy = pipe_occupancy(head, tail);
	if (p_occupancy >= pipe->max_usage)
		return 0;
	p_space = pipe->ring_size - p_occupancy;
	if (p_space > pipe->max_usage)
		p_space = pipe->max_usage;
	return p_space;
}

/**
 * pipe_buf_get - get a reference to a pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to get a reference to
 *
 * Return: %true if the reference was successfully obtained.
 */
static inline __must_check bool pipe_buf_get(struct pipe_inode_info *pipe,
				struct pipe_buffer *buf)
{
	return buf->ops->get(pipe, buf);
}

/**
 * pipe_buf_release - put a reference to a pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to put a reference to
 */
static inline void pipe_buf_release(struct pipe_inode_info *pipe,
				    struct pipe_buffer *buf)
{
	const struct pipe_buf_operations *ops = buf->ops;

	buf->ops = NULL;
	ops->release(pipe, buf);
}

/**
 * pipe_buf_confirm - verify contents of the pipe buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to confirm
 */
static inline int pipe_buf_confirm(struct pipe_inode_info *pipe,
				   struct pipe_buffer *buf)
{
	if (!buf->ops->confirm)
		return 0;
	return buf->ops->confirm(pipe, buf);
}

/**
 * pipe_buf_try_steal - attempt to take ownership of a pipe_buffer
 * @pipe:	the pipe that the buffer belongs to
 * @buf:	the buffer to attempt to steal
 */
static inline bool pipe_buf_try_steal(struct pipe_inode_info *pipe,
		struct pipe_buffer *buf)
{
	if (!buf->ops->try_steal)
		return false;
	return buf->ops->try_steal(pipe, buf);
}

/* Differs from PIPE_BUF in that PIPE_SIZE is the length of the actual
   memory allocation, whereas PIPE_BUF makes atomicity guarantees.  */
#define PIPE_SIZE		PAGE_SIZE

/* Pipe lock and unlock operations */
void pipe_lock(struct pipe_inode_info *);
void pipe_unlock(struct pipe_inode_info *);
void pipe_double_lock(struct pipe_inode_info *, struct pipe_inode_info *);

extern unsigned int pipe_max_size;
extern unsigned long pipe_user_pages_hard;
extern unsigned long pipe_user_pages_soft;

/* Drop the inode semaphore and wait for a pipe event, atomically */
void pipe_wait(struct pipe_inode_info *pipe);

struct pipe_inode_info *alloc_pipe_info(void);
void free_pipe_info(struct pipe_inode_info *);

/* Generic pipe buffer ops functions */
bool generic_pipe_buf_get(struct pipe_inode_info *, struct pipe_buffer *);
bool generic_pipe_buf_try_steal(struct pipe_inode_info *, struct pipe_buffer *);
void generic_pipe_buf_release(struct pipe_inode_info *, struct pipe_buffer *);

extern const struct pipe_buf_operations nosteal_pipe_buf_ops;

#ifdef CONFIG_WATCH_QUEUE
unsigned long account_pipe_buffers(struct user_struct *user,
				   unsigned long old, unsigned long new);
bool too_many_pipe_buffers_soft(unsigned long user_bufs);
bool too_many_pipe_buffers_hard(unsigned long user_bufs);
bool pipe_is_unprivileged_user(void);
#endif

/* for F_SETPIPE_SZ and F_GETPIPE_SZ */
#ifdef CONFIG_WATCH_QUEUE
int pipe_resize_ring(struct pipe_inode_info *pipe, unsigned int nr_slots);
#endif
long pipe_fcntl(struct file *, unsigned int, unsigned long arg);
struct pipe_inode_info *get_pipe_info(struct file *file, bool for_splice);

int create_pipe_files(struct file **, int);
unsigned int round_pipe_size(unsigned long size);

#endif
