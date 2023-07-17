/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CACHE_H
#define __PERF_CACHE_H

#include "strbuf.h"
#include <subcmd/pager.h>
#include "../ui/ui.h"

#include <linux/compiler.h>
#include <linux/string.h>

#define CMD_EXEC_PATH "--exec-path"
#define CMD_DEBUGFS_DIR "--debugfs-dir="

#define EXEC_PATH_ENVIRONMENT "PERF_EXEC_PATH"
#define PERF_DEBUGFS_ENVIRONMENT "PERF_DEBUGFS_DIR"
#define PERF_TRACEFS_ENVIRONMENT "PERF_TRACEFS_DIR"
#define PERF_PAGER_ENVIRONMENT "PERF_PAGER"

int split_cmdline(char *cmdline, const char ***argv);

#define alloc_nr(x) (((x)+16)*3/2)

static inline int is_absolute_path(const char *path)
{
	return path[0] == '/';
}

char *mkpath(char *path_buf, size_t sz, const char *fmt, ...) __printf(3, 4);

#endif /* __PERF_CACHE_H */
