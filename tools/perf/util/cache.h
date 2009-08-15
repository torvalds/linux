#ifndef CACHE_H
#define CACHE_H

#include "util.h"
#include "strbuf.h"

#define PERF_DIR_ENVIRONMENT "PERF_DIR"
#define PERF_WORK_TREE_ENVIRONMENT "PERF_WORK_TREE"
#define DEFAULT_PERF_DIR_ENVIRONMENT ".perf"
#define DB_ENVIRONMENT "PERF_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "PERF_INDEX_FILE"
#define GRAFT_ENVIRONMENT "PERF_GRAFT_FILE"
#define TEMPLATE_DIR_ENVIRONMENT "PERF_TEMPLATE_DIR"
#define CONFIG_ENVIRONMENT "PERF_CONFIG"
#define EXEC_PATH_ENVIRONMENT "PERF_EXEC_PATH"
#define CEILING_DIRECTORIES_ENVIRONMENT "PERF_CEILING_DIRECTORIES"
#define PERFATTRIBUTES_FILE ".perfattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
#define ATTRIBUTE_MACRO_PREFIX "[attr]"

typedef int (*config_fn_t)(const char *, const char *, void *);
extern int perf_default_config(const char *, const char *, void *);
extern int perf_config_from_file(config_fn_t fn, const char *, void *);
extern int perf_config(config_fn_t fn, void *);
extern int perf_parse_ulong(const char *, unsigned long *);
extern int perf_config_int(const char *, const char *);
extern unsigned long perf_config_ulong(const char *, const char *);
extern int perf_config_bool_or_int(const char *, const char *, int *);
extern int perf_config_bool(const char *, const char *);
extern int perf_config_string(const char **, const char *, const char *);
extern int perf_config_set(const char *, const char *);
extern int perf_config_set_multivar(const char *, const char *, const char *, int);
extern int perf_config_rename_section(const char *, const char *);
extern const char *perf_etc_perfconfig(void);
extern int check_repository_format_version(const char *var, const char *value, void *cb);
extern int perf_config_system(void);
extern int perf_config_global(void);
extern int config_error_nonbool(const char *);
extern const char *config_exclusive_filename;

#define MAX_PERFNAME (1000)
extern char perf_default_email[MAX_PERFNAME];
extern char perf_default_name[MAX_PERFNAME];
extern int user_ident_explicitly_given;

extern const char *perf_log_output_encoding;
extern const char *perf_mailmap_file;

/* IO helper functions */
extern void maybe_flush_or_die(FILE *, const char *);
extern int copy_fd(int ifd, int ofd);
extern int copy_file(const char *dst, const char *src, int mode);
extern ssize_t read_in_full(int fd, void *buf, size_t count);
extern ssize_t write_in_full(int fd, const void *buf, size_t count);
extern void write_or_die(int fd, const void *buf, size_t count);
extern int write_or_whine(int fd, const void *buf, size_t count, const char *msg);
extern int write_or_whine_pipe(int fd, const void *buf, size_t count, const char *msg);
extern void fsync_or_die(int fd, const char *);

/* pager.c */
extern void setup_pager(void);
extern const char *pager_program;
extern int pager_in_use(void);
extern int pager_use_color;

extern const char *editor_program;
extern const char *excludes_file;

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

const char *make_absolute_path(const char *path);
const char *make_nonrelative_path(const char *path);
const char *make_relative_path(const char *abs, const char *base);
int normalize_path_copy(char *dst, const char *src);
int longest_ancestor_length(const char *path, const char *prefix_list);
char *strip_path_suffix(const char *path, const char *suffix);

extern char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *perf_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
/* perf_mkstemp() - create tmp file honoring TMPDIR variable */
extern int perf_mkstemp(char *path, size_t len, const char *template);

extern char *mksnpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *perf_snpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format (printf, 3, 4)));
extern char *perf_pathdup(const char *fmt, ...)
	__attribute__((format (printf, 1, 2)));

extern size_t strlcpy(char *dest, const char *src, size_t size);

#endif /* CACHE_H */
