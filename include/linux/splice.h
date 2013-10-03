/*
 * Function declerations and data structures related to the splice
 * implementation.
 *
 * Copyright (C) 2007 Jens Axboe <jens.axboe@oracle.com>
 *
 */
#ifndef SPLICE_H
#define SPLICE_H

#include <linux/pipe_fs_i.h>

/*
 * Flags passed in from splice/tee/vmsplice
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
	/*
	 * actor() private data
	 */
	union {
		void __user *userptr;	/* memory to write to */
		struct file *file;	/* file to read/write */
		void *data;		/* cookie */
	} u;
	loff_t pos;			/* file position */
	loff_t *opos;			/* sendfile: output position */
	size_t num_spliced;		/* number of bytes already spliced */
	bool need_wakeup;		/* need to wake up writer */
};

struct partial_page {
	unsigned int offset;
	unsigned int len;
	unsigned long private;
};

/*
 * Passed to splice_to_pipe
 */
struct splice_pipe_desc {
	struct page **pages;		/* page map */
	struct partial_page *partial;	/* pages[] may not be contig */
	int nr_pages;			/* number of populated pages in map */
	unsigned int nr_pages_max;	/* pages[] & partial[] arrays size */
	unsigned int flags;		/* splice flags */
	const struct pipe_buf_operations *ops;/* ops associated with output pipe */
	void (*spd_release)(struct splice_pipe_desc *, unsigned int);
};

typedef int (splice_actor)(struct pipe_inode_info *, struct pipe_buffer *,
			   struct splice_desc *);
typedef int (splice_direct_actor)(struct pipe_inode_info *,
				  struct splice_desc *);

extern ssize_t splice_from_pipe(struct pipe_inode_info *, struct file *,
				loff_t *, size_t, unsigned int,
				splice_actor *);
extern ssize_t __splice_from_pipe(struct pipe_inode_info *,
				  struct splice_desc *, splice_actor *);
extern int splice_from_pipe_feed(struct pipe_inode_info *, struct splice_desc *,
				 splice_actor *);
extern int splice_from_pipe_next(struct pipe_inode_info *,
				 struct splice_desc *);
extern void splice_from_pipe_begin(struct splice_desc *);
extern void splice_from_pipe_end(struct pipe_inode_info *,
				 struct splice_desc *);
extern int pipe_to_file(struct pipe_inode_info *, struct pipe_buffer *,
			struct splice_desc *);

extern ssize_t splice_to_pipe(struct pipe_inode_info *,
			      struct splice_pipe_desc *);
extern ssize_t splice_direct_to_actor(struct file *, struct splice_desc *,
				      splice_direct_actor *);

/*
 * for dynamic pipe sizing
 */
extern int splice_grow_spd(const struct pipe_inode_info *, struct splice_pipe_desc *);
extern void splice_shrink_spd(struct splice_pipe_desc *);
extern void spd_release_page(struct splice_pipe_desc *, unsigned int);

extern const struct pipe_buf_operations page_cache_pipe_buf_ops;
#endif
