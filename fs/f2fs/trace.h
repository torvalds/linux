/*
 * f2fs IO tracer
 *
 * Copyright (c) 2014 Motorola Mobility
 * Copyright (c) 2014 Jaegeuk Kim <jaegeuk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __F2FS_TRACE_H__
#define __F2FS_TRACE_H__

#ifdef CONFIG_F2FS_IO_TRACE
#include <trace/events/f2fs.h>

extern void f2fs_trace_pid(struct page *);
extern void f2fs_trace_ios(struct page *, struct f2fs_io_info *, int);
#else
#define f2fs_trace_pid(p)
#define f2fs_trace_ios(p, i, n)

#endif
#endif /* __F2FS_TRACE_H__ */
