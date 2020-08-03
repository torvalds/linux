/* SPDX-License-Identifier: GPL-2.0 */
/*
 * f2fs IO tracer
 *
 * Copyright (c) 2014 Motorola Mobility
 * Copyright (c) 2014 Jaegeuk Kim <jaegeuk@kernel.org>
 */
#ifndef __F2FS_TRACE_H__
#define __F2FS_TRACE_H__

#ifdef CONFIG_F2FS_IO_TRACE
#include <trace/events/f2fs.h>

enum file_type {
	__NORMAL_FILE,
	__DIR_FILE,
	__NODE_FILE,
	__META_FILE,
	__ATOMIC_FILE,
	__VOLATILE_FILE,
	__MISC_FILE,
};

struct last_io_info {
	int major, minor;
	pid_t pid;
	enum file_type type;
	struct f2fs_io_info fio;
	block_t len;
};

extern void f2fs_trace_pid(struct page *);
extern void f2fs_trace_ios(struct f2fs_io_info *, int);
extern void f2fs_build_trace_ios(void);
extern void f2fs_destroy_trace_ios(void);
#else
#define f2fs_trace_pid(p)
#define f2fs_trace_ios(i, n)
#define f2fs_build_trace_ios()
#define f2fs_destroy_trace_ios()

#endif
#endif /* __F2FS_TRACE_H__ */
