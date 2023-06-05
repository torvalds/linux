/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_UTIL_H
#define __PERF_UTIL_H

#define _BSD_SOURCE 1
/* glibc 2.20 deprecates _BSD_SOURCE in favour of _DEFAULT_SOURCE */
#define _DEFAULT_SOURCE 1

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/compiler.h>
#include <sys/types.h>
#ifndef __cplusplus
#include <internal/cpumap.h>
#endif

extern const char perf_usage_string[];
extern const char perf_more_info_string[];

extern const char *input_name;

extern bool perf_host;
extern bool perf_guest;

/* General helper functions */
void usage(const char *err) __noreturn;
void die(const char *err, ...) __noreturn __printf(1, 2);

struct dirent;
struct strlist;

int mkdir_p(char *path, mode_t mode);
int rm_rf(const char *path);
int rm_rf_perf_data(const char *path);
struct strlist *lsdir(const char *name, bool (*filter)(const char *, struct dirent *));
bool lsdir_no_dot_filter(const char *name, struct dirent *d);

size_t hex_width(u64 v);

int sysctl__max_stack(void);

bool sysctl__nmi_watchdog_enabled(void);

int fetch_kernel_version(unsigned int *puint,
			 char *str, size_t str_sz);
#define KVER_VERSION(x)		(((x) >> 16) & 0xff)
#define KVER_PATCHLEVEL(x)	(((x) >> 8) & 0xff)
#define KVER_SUBLEVEL(x)	((x) & 0xff)
#define KVER_FMT	"%d.%d.%d"
#define KVER_PARAM(x)	KVER_VERSION(x), KVER_PATCHLEVEL(x), KVER_SUBLEVEL(x)

int perf_tip(char **strp, const char *dirpath);

#ifndef HAVE_SCHED_GETCPU_SUPPORT
int sched_getcpu(void);
#endif

extern bool perf_singlethreaded;

void perf_set_singlethreaded(void);
void perf_set_multithreaded(void);

char *perf_exe(char *buf, int len);

#ifndef O_CLOEXEC
#ifdef __sparc__
#define O_CLOEXEC      0x400000
#elif defined(__alpha__) || defined(__hppa__)
#define O_CLOEXEC      010000000
#else
#define O_CLOEXEC      02000000
#endif
#endif

extern bool test_attr__enabled;
void test_attr__ready(void);
void test_attr__init(void);
struct perf_event_attr;
void test_attr__open(struct perf_event_attr *attr, pid_t pid, struct perf_cpu cpu,
		     int fd, int group_fd, unsigned long flags);

struct perf_debuginfod {
	const char	*urls;
	bool		 set;
};
void perf_debuginfod_setup(struct perf_debuginfod *di);

char *filename_with_chroot(int pid, const char *filename);

int do_realloc_array_as_needed(void **arr, size_t *arr_sz, size_t x,
			       size_t msz, const void *init_val);

#define realloc_array_as_needed(a, n, x, v) ({			\
	typeof(x) __x = (x);					\
	__x >= (n) ?						\
		do_realloc_array_as_needed((void **)&(a),	\
					   &(n),		\
					   __x,			\
					   sizeof(*(a)),	\
					   (const void *)(v)) :	\
		0;						\
	})

static inline bool host_is_bigendian(void)
{
#ifdef __BYTE_ORDER__
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return false;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	return true;
#else
#error "Unrecognized __BYTE_ORDER__"
#endif
#else /* !__BYTE_ORDER__ */
	unsigned char str[] = { 0x1, 0x2, 0x3, 0x4, 0x0, 0x0, 0x0, 0x0};
	unsigned int *ptr;

	ptr = (unsigned int *)(void *)str;
	return *ptr == 0x01020304;
#endif
}

#endif /* __PERF_UTIL_H */
