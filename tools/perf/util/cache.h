#ifndef __PERF_CACHE_H
#define __PERF_CACHE_H

#include <stdbool.h>
#include "util.h"
#include "strbuf.h"
#include "../perf.h"
#include "../ui/ui.h"

#define CMD_EXEC_PATH "--exec-path"
#define CMD_PERF_DIR "--perf-dir="
#define CMD_WORK_TREE "--work-tree="
#define CMD_DEBUGFS_DIR "--debugfs-dir="

#define PERF_DIR_ENVIRONMENT "PERF_DIR"
#define PERF_WORK_TREE_ENVIRONMENT "PERF_WORK_TREE"
#define EXEC_PATH_ENVIRONMENT "PERF_EXEC_PATH"
#define DEFAULT_PERF_DIR_ENVIRONMENT ".perf"
#define PERF_DEBUGFS_ENVIRONMENT "PERF_DEBUGFS_DIR"

typedef int (*config_fn_t)(const char *, const char *, void *);
extern int perf_default_config(const char *, const char *, void *);
extern int perf_config(config_fn_t fn, void *);
extern int perf_config_int(const char *, const char *);
extern u64 perf_config_u64(const char *, const char *);
extern int perf_config_bool(const char *, const char *);
extern int config_error_nonbool(const char *);
extern const char *perf_config_dirname(const char *, const char *);

/* pager.c */
extern void setup_pager(void);
extern const char *pager_program;
extern int pager_in_use(void);
extern int pager_use_color;

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

const char *make_nonrelative_path(const char *path);
char *strip_path_suffix(const char *path, const char *suffix);

extern char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *perf_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));

extern char *perf_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

#ifndef __UCLIBC__
/* Matches the libc/libbsd function attribute so we declare this unconditionally: */
extern size_t strlcpy(char *dest, const char *src, size_t size);
#endif

#endif /* __PERF_CACHE_H */
