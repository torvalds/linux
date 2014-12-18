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
#include <linux/fs.h>
#include <linux/f2fs_fs.h>
#include <linux/sched.h>

#include "f2fs.h"
#include "trace.h"

void f2fs_trace_pid(struct page *page)
{
}

void f2fs_trace_ios(struct page *page, struct f2fs_io_info *fio, int flush)
{
}
