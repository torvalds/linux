#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

#ifndef FLEX_ARRAY
/*
 * See if our compiler is known to support flexible array members.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
# define FLEX_ARRAY /* empty */
#elif defined(__GNUC__)
# if (__GNUC__ >= 3)
#  define FLEX_ARRAY /* empty */
# else
#  define FLEX_ARRAY 0 /* older GNU extension */
# endif
#endif

/*
 * Otherwise, default to safer but a bit wasteful traditional style
 */
#ifndef FLEX_ARRAY
# define FLEX_ARRAY 1
#endif
#endif

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#ifdef __GNUC__
#define TYPEOF(x) (__typeof__(x))
#else
#define TYPEOF(x)
#endif

#define MSB(x, bits) ((x) & TYPEOF(x)(~0ULL << (sizeof(x) * 8 - (bits))))
#define HAS_MULTI_BITS(i)  ((i) & ((i) - 1))  /* checks if an integer has more than 1 bit set */

/* Approximation of the length of the decimal representation of this type. */
#define decimal_length(x)	((int)(sizeof(x) * 2.56 + 0.5) + 1)

#define _ALL_SOURCE 1
#define _BSD_SOURCE 1
/* glibc 2.20 deprecates _BSD_SOURCE in favour of _DEFAULT_SOURCE */
#define _DEFAULT_SOURCE 1
#define HAS_BOOL

#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <term.h>
#include <errno.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <fnmatch.h>
#include <assert.h>
#include <regex.h>
#include <utime.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/ttydefaults.h>
#include <api/fs/tracing_path.h>
#include <termios.h>
#include <linux/bitops.h>
#include <termios.h>
#include "strlist.h"

extern const char *graph_line;
extern const char *graph_dotted_line;
extern const char *spaces;
extern const char *dots;
extern char buildid_dir[];

/* On most systems <limits.h> would have given us this, but
 * not on some systems (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef PRIuMAX
#define PRIuMAX "llu"
#endif

#ifndef PRIu32
#define PRIu32 "u"
#endif

#ifndef PRIx32
#define PRIx32 "x"
#endif

#ifndef PATH_SEP
#define PATH_SEP ':'
#endif

#ifndef STRIP_EXTENSION
#define STRIP_EXTENSION ""
#endif

#ifndef has_dos_drive_prefix
#define has_dos_drive_prefix(path) 0
#endif

#ifndef is_dir_sep
#define is_dir_sep(c) ((c) == '/')
#endif

#ifdef __GNUC__
#define NORETURN __attribute__((__noreturn__))
#else
#define NORETURN
#ifndef __attribute__
#define __attribute__(x)
#endif
#endif

#define PERF_GTK_DSO  "libperf-gtk.so"

/* General helper functions */
void usage(const char *err) NORETURN;
void die(const char *err, ...) NORETURN __attribute__((format (printf, 1, 2)));
int error(const char *err, ...) __attribute__((format (printf, 1, 2)));
void warning(const char *err, ...) __attribute__((format (printf, 1, 2)));

void set_warning_routine(void (*routine)(const char *err, va_list params));

int prefixcmp(const char *str, const char *prefix);
void set_buildid_dir(const char *dir);

#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 1)
#define HAVE_STRCHRNUL
#endif
#endif

#ifndef HAVE_STRCHRNUL
#define strchrnul gitstrchrnul
static inline char *gitstrchrnul(const char *s, int c)
{
	while (*s && *s != c)
		s++;
	return (char *)s;
}
#endif

static inline void *zalloc(size_t size)
{
	return calloc(1, size);
}

#define zfree(ptr) ({ free(*ptr); *ptr = NULL; })

/* Sane ctype - no locale, and works with signed chars */
#undef isascii
#undef isspace
#undef isdigit
#undef isxdigit
#undef isalpha
#undef isprint
#undef isalnum
#undef islower
#undef isupper
#undef tolower
#undef toupper

#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC	1000000L
#endif

int parse_nsec_time(const char *str, u64 *ptime);

extern unsigned char sane_ctype[256];
#define GIT_SPACE		0x01
#define GIT_DIGIT		0x02
#define GIT_ALPHA		0x04
#define GIT_GLOB_SPECIAL	0x08
#define GIT_REGEX_SPECIAL	0x10
#define GIT_PRINT_EXTRA		0x20
#define GIT_PRINT		0x3E
#define sane_istest(x,mask) ((sane_ctype[(unsigned char)(x)] & (mask)) != 0)
#define isascii(x) (((x) & ~0x7f) == 0)
#define isspace(x) sane_istest(x,GIT_SPACE)
#define isdigit(x) sane_istest(x,GIT_DIGIT)
#define isxdigit(x)	\
	(sane_istest(toupper(x), GIT_ALPHA | GIT_DIGIT) && toupper(x) < 'G')
#define isalpha(x) sane_istest(x,GIT_ALPHA)
#define isalnum(x) sane_istest(x,GIT_ALPHA | GIT_DIGIT)
#define isprint(x) sane_istest(x,GIT_PRINT)
#define islower(x) (sane_istest(x,GIT_ALPHA) && (x & 0x20))
#define isupper(x) (sane_istest(x,GIT_ALPHA) && !(x & 0x20))
#define tolower(x) sane_case((unsigned char)(x), 0x20)
#define toupper(x) sane_case((unsigned char)(x), 0)

static inline int sane_case(int x, int high)
{
	if (sane_istest(x, GIT_ALPHA))
		x = (x & ~0x20) | high;
	return x;
}

int mkdir_p(char *path, mode_t mode);
int rm_rf(char *path);
struct strlist *lsdir(const char *name, bool (*filter)(const char *, struct dirent *));
bool lsdir_no_dot_filter(const char *name, struct dirent *d);
int copyfile(const char *from, const char *to);
int copyfile_mode(const char *from, const char *to, mode_t mode);
int copyfile_offset(int fromfd, loff_t from_ofs, int tofd, loff_t to_ofs, u64 size);

s64 perf_atoll(const char *str);
char **argv_split(const char *str, int *argcp);
void argv_free(char **argv);
bool strglobmatch(const char *str, const char *pat);
bool strlazymatch(const char *str, const char *pat);
static inline bool strisglob(const char *str)
{
	return strpbrk(str, "*?[") != NULL;
}
int strtailcmp(const char *s1, const char *s2);
char *strxfrchar(char *s, char from, char to);
unsigned long convert_unit(unsigned long value, char *unit);
ssize_t readn(int fd, void *buf, size_t n);
ssize_t writen(int fd, void *buf, size_t n);

struct perf_event_attr;

void event_attr_init(struct perf_event_attr *attr);

#define _STR(x) #x
#define STR(x) _STR(x)

size_t hex_width(u64 v);
int hex2u64(const char *ptr, u64 *val);

char *ltrim(char *s);
char *rtrim(char *s);

static inline char *trim(char *s)
{
	return ltrim(rtrim(s));
}

void dump_stack(void);
void sighandler_dump_stack(int sig);

extern unsigned int page_size;
extern int cacheline_size;
extern int sysctl_perf_event_max_stack;
extern int sysctl_perf_event_max_contexts_per_stack;

struct parse_tag {
	char tag;
	int mult;
};

unsigned long parse_tag_value(const char *str, struct parse_tag *tags);

#define SRCLINE_UNKNOWN  ((char *) "??:0")

static inline int path__join(char *bf, size_t size,
			     const char *path1, const char *path2)
{
	return scnprintf(bf, size, "%s%s%s", path1, path1[0] ? "/" : "", path2);
}

static inline int path__join3(char *bf, size_t size,
			      const char *path1, const char *path2,
			      const char *path3)
{
	return scnprintf(bf, size, "%s%s%s%s%s",
			 path1, path1[0] ? "/" : "",
			 path2, path2[0] ? "/" : "", path3);
}

struct dso;
struct symbol;

extern bool srcline_full_filename;
char *get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym);
char *__get_srcline(struct dso *dso, u64 addr, struct symbol *sym,
		  bool show_sym, bool unwind_inlines);
void free_srcline(char *srcline);

int perf_event_paranoid(void);

void mem_bswap_64(void *src, int byte_size);
void mem_bswap_32(void *src, int byte_size);

const char *get_filename_for_perf_kvm(void);
bool find_process(const char *name);

#ifdef HAVE_ZLIB_SUPPORT
int gzip_decompress_to_file(const char *input, int output_fd);
#endif

#ifdef HAVE_LZMA_SUPPORT
int lzma_decompress_to_file(const char *input, int output_fd);
#endif

char *asprintf_expr_inout_ints(const char *var, bool in, size_t nints, int *ints);

static inline char *asprintf_expr_in_ints(const char *var, size_t nints, int *ints)
{
	return asprintf_expr_inout_ints(var, true, nints, ints);
}

static inline char *asprintf_expr_not_in_ints(const char *var, size_t nints, int *ints)
{
	return asprintf_expr_inout_ints(var, false, nints, ints);
}

int get_stack_size(const char *str, unsigned long *_size);

int fetch_kernel_version(unsigned int *puint,
			 char *str, size_t str_sz);
#define KVER_VERSION(x)		(((x) >> 16) & 0xff)
#define KVER_PATCHLEVEL(x)	(((x) >> 8) & 0xff)
#define KVER_SUBLEVEL(x)	((x) & 0xff)
#define KVER_FMT	"%d.%d.%d"
#define KVER_PARAM(x)	KVER_VERSION(x), KVER_PATCHLEVEL(x), KVER_SUBLEVEL(x)

const char *perf_tip(const char *dirpath);
bool is_regular_file(const char *file);
int fetch_current_timestamp(char *buf, size_t sz);

enum binary_printer_ops {
	BINARY_PRINT_DATA_BEGIN,
	BINARY_PRINT_LINE_BEGIN,
	BINARY_PRINT_ADDR,
	BINARY_PRINT_NUM_DATA,
	BINARY_PRINT_NUM_PAD,
	BINARY_PRINT_SEP,
	BINARY_PRINT_CHAR_DATA,
	BINARY_PRINT_CHAR_PAD,
	BINARY_PRINT_LINE_END,
	BINARY_PRINT_DATA_END,
};

typedef void (*print_binary_t)(enum binary_printer_ops,
			       unsigned int val,
			       void *extra);

void print_binary(unsigned char *data, size_t len,
		  size_t bytes_per_line, print_binary_t printer,
		  void *extra);

#if !defined(__GLIBC__) && !defined(__ANDROID__)
extern int sched_getcpu(void);
#endif

int is_printable_array(char *p, unsigned int len);
#endif /* GIT_COMPAT_UTIL_H */
