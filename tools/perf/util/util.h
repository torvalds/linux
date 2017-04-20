#ifndef GIT_COMPAT_UTIL_H
#define GIT_COMPAT_UTIL_H

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
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <utime.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/kernel.h>
#include <linux/types.h>

extern char buildid_dir[];

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

static inline void *zalloc(size_t size)
{
	return calloc(1, size);
}

#define zfree(ptr) ({ free(*ptr); *ptr = NULL; })

struct dirent;
struct strlist;

int mkdir_p(char *path, mode_t mode);
int rm_rf(const char *path);
struct strlist *lsdir(const char *name, bool (*filter)(const char *, struct dirent *));
bool lsdir_no_dot_filter(const char *name, struct dirent *d);
int copyfile(const char *from, const char *to);
int copyfile_mode(const char *from, const char *to, mode_t mode);
int copyfile_offset(int fromfd, loff_t from_ofs, int tofd, loff_t to_ofs, u64 size);

unsigned long convert_unit(unsigned long value, char *unit);
ssize_t readn(int fd, void *buf, size_t n);
ssize_t writen(int fd, void *buf, size_t n);

struct perf_event_attr;

void event_attr_init(struct perf_event_attr *attr);

size_t hex_width(u64 v);
int hex2u64(const char *ptr, u64 *val);

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

int get_stack_size(const char *str, unsigned long *_size);

int fetch_kernel_version(unsigned int *puint,
			 char *str, size_t str_sz);
#define KVER_VERSION(x)		(((x) >> 16) & 0xff)
#define KVER_PATCHLEVEL(x)	(((x) >> 8) & 0xff)
#define KVER_SUBLEVEL(x)	((x) & 0xff)
#define KVER_FMT	"%d.%d.%d"
#define KVER_PARAM(x)	KVER_VERSION(x), KVER_PATCHLEVEL(x), KVER_SUBLEVEL(x)

const char *perf_tip(const char *dirpath);
int fetch_current_timestamp(char *buf, size_t sz);

#ifndef HAVE_SCHED_GETCPU_SUPPORT
int sched_getcpu(void);
#endif

int timestamp__scnprintf_usec(u64 timestamp, char *buf, size_t sz);

int unit_number__scnprintf(char *buf, size_t size, u64 n);

#endif /* GIT_COMPAT_UTIL_H */
