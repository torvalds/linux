/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_BUFFER_H
#define _LINUX_TTY_BUFFER_H

#include <linux/atomic.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

struct tty_buffer {
	union {
		struct tty_buffer *next;
		struct llist_node free;
	};
	int used;
	int size;
	int commit;
	int lookahead;		/* Lazy update on recv, can become less than "read" */
	int read;
	bool flags;
	/* Data points here */
	unsigned long data[];
};

static inline unsigned char *char_buf_ptr(struct tty_buffer *b, int ofs)
{
	return ((unsigned char *)b->data) + ofs;
}

static inline char *flag_buf_ptr(struct tty_buffer *b, int ofs)
{
	return (char *)char_buf_ptr(b, ofs) + b->size;
}

struct tty_bufhead {
	struct tty_buffer *head;	/* Queue head */
	struct work_struct work;
	struct mutex	   lock;
	atomic_t	   priority;
	struct tty_buffer sentinel;
	struct llist_head free;		/* Free queue head */
	atomic_t	   mem_used;    /* In-use buffers excluding free list */
	int		   mem_limit;
	struct tty_buffer *tail;	/* Active buffer */
};

/*
 * When a break, frame error, or parity error happens, these codes are
 * stuffed into the flags buffer.
 */
#define TTY_NORMAL	0
#define TTY_BREAK	1
#define TTY_FRAME	2
#define TTY_PARITY	3
#define TTY_OVERRUN	4

#endif
