/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_THREAD_WITH_FILE_H
#define _BCACHEFS_THREAD_WITH_FILE_H

#include "thread_with_file_types.h"

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

struct thread_with_stdio {
	struct thread_with_file	thr;
	struct stdio_redirect	stdio;
	void			(*exit)(struct thread_with_stdio *);
};

static inline void thread_with_stdio_done(struct thread_with_stdio *thr)
{
	thr->thr.done = true;
	thr->stdio.done = true;
	wake_up(&thr->stdio.input.wait);
	wake_up(&thr->stdio.output.wait);
}

int bch2_run_thread_with_stdio(struct thread_with_stdio *,
			       void (*exit)(struct thread_with_stdio *),
			       int (*fn)(void *));
int bch2_stdio_redirect_read(struct stdio_redirect *, char *, size_t);
int bch2_stdio_redirect_readline(struct stdio_redirect *, char *, size_t);

__printf(3, 0) void bch2_stdio_redirect_vprintf(struct stdio_redirect *, bool, const char *, va_list);
__printf(3, 4) void bch2_stdio_redirect_printf(struct stdio_redirect *, bool, const char *, ...);

#endif /* _BCACHEFS_THREAD_WITH_FILE_H */
