#ifndef __PERF_CACHE_H
#define __PERF_CACHE_H

#include <stdbool.h>
#include "util.h"
#include "strbuf.h"
#include <subcmd/pager.h>
#include "../perf.h"
#include "../ui/ui.h"

#include <linux/string.h>

#define CMD_EXEC_PATH "--exec-path"
#define CMD_PERF_DIR "--perf-dir="
#define CMD_WORK_TREE "--work-tree="
#define CMD_DEBUGFS_DIR "--debugfs-dir="

#define PERF_DIR_ENVIRONMENT "PERF_DIR"
#define PERF_WORK_TREE_ENVIRONMENT "PERF_WORK_TREE"
#define EXEC_PATH_ENVIRONMENT "PERF_EXEC_PATH"
#define DEFAULT_PERF_DIR_ENVIRONMENT ".perf"
#define PERF_DEBUGFS_ENVIRONMENT "PERF_DEBUGFS_DIR"
#define PERF_TRACEFS_ENVIRONMENT "PERF_TRACEFS_DIR"
#define PERF_PAGER_ENVIRONMENT "PERF_PAGER"

extern const char *config_exclusive_filename;

typedef int (*config_fn_t)(const char *, const char *, void *);
int perf_default_config(const char *, const char *, void *);
int perf_config(config_fn_t fn, void *);
int perf_config_int(const char *, const char *);
u64 perf_config_u64(const char *, const char *);
int perf_config_bool(const char *, const char *);
int config_error_nonbool(const char *);
const char *perf_config_dirname(const char *, const char *);
const char *perf_etc_perfconfig(void);

char *alias_lookup(const char *alias);
int split_cmdline(char *cmdline, const char ***argv);

#define alloc_nr(x) (((x)+16)*3/2)

/*
 * Realloc the buffer pointed at by variable 'x' so that it can hold
 * at least 'nr' entries; the number of entries currently allocated
 * is 'alloc', using the standard growing factor alloc_nr() macro.
 *
 * DO NOT USE any expression with side-effect for 'x' or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			x = xrealloc((x), alloc * sizeof(*(x))); \
		} \
	} while(0)


static inline int is_absolute_path(const char *path)
{
	return path[0] == '/';
}

char *strip_path_suffix(const char *path, const char *suffix);

char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
char *perf_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));

#endif /* __PERF_CACHE_H */
