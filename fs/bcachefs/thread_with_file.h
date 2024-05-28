/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_THREAD_WITH_FILE_H
#define _BCACHEFS_THREAD_WITH_FILE_H

#include "thread_with_file_types.h"

/*
 * Thread with file: Run a kthread and connect it to a file descriptor, so that
 * it can be interacted with via fd read/write methods and closing the file
 * descriptor stops the kthread.
 *
 * We have two different APIs:
 *
 * thread_with_file, the low level version.
 * You get to define the full file_operations, including your release function,
 * which means that you must call bch2_thread_with_file_exit() from your
 * .release method
 *
 * thread_with_stdio, the higher level version
 * This implements full piping of input and output, including .poll.
 *
 * Notes on behaviour:
 *  - kthread shutdown behaves like writing or reading from a pipe that has been
 *    closed
 *  - Input and output buffers are 4096 bytes, although buffers may in some
 *    situations slightly exceed that limit so as to avoid chopping off a
 *    message in the middle in nonblocking mode.
 *  - Input/output buffers are lazily allocated, with GFP_NOWAIT allocations -
 *    should be fine but might change in future revisions.
 *  - Output buffer may grow past 4096 bytes to deal with messages that are
 *    bigger than 4096 bytes
 *  - Writing may be done blocking or nonblocking; in nonblocking mode, we only
 *    drop entire messages.
 *
 * To write, use stdio_redirect_printf()
 * To read, use stdio_redirect_read() or stdio_redirect_readline()
 */

struct task_struct;

struct thread_with_file {
	struct task_struct	*task;
	int			ret;
	bool			done;
};

void bch2_thread_with_file_exit(struct thread_with_file *);
int bch2_run_thread_with_file(struct thread_with_file *,
			      const struct file_operations *,
			      int (*fn)(void *));

struct thread_with_stdio;

struct thread_with_stdio_ops {
	void (*exit)(struct thread_with_stdio *);
	int (*fn)(struct thread_with_stdio *);
	long (*unlocked_ioctl)(struct thread_with_stdio *, unsigned int, unsigned long);
};

struct thread_with_stdio {
	struct thread_with_file	thr;
	struct stdio_redirect	stdio;
	const struct thread_with_stdio_ops	*ops;
};

void bch2_thread_with_stdio_init(struct thread_with_stdio *,
				 const struct thread_with_stdio_ops *);
int __bch2_run_thread_with_stdio(struct thread_with_stdio *);
int bch2_run_thread_with_stdio(struct thread_with_stdio *,
			       const struct thread_with_stdio_ops *);
int bch2_run_thread_with_stdout(struct thread_with_stdio *,
				const struct thread_with_stdio_ops *);
int bch2_stdio_redirect_read(struct stdio_redirect *, char *, size_t);
int bch2_stdio_redirect_readline(struct stdio_redirect *, char *, size_t);

__printf(3, 0) ssize_t bch2_stdio_redirect_vprintf(struct stdio_redirect *, bool, const char *, va_list);
__printf(3, 4) ssize_t bch2_stdio_redirect_printf(struct stdio_redirect *, bool, const char *, ...);

#endif /* _BCACHEFS_THREAD_WITH_FILE_H */
