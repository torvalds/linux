/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __API_FS__
#define __API_FS__

#include <stdbool.h>
#include <unistd.h>

/*
 * On most systems <limits.h> would have given us this, but  not on some systems
 * (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FS(name)				\
	const char *name##__mountpoint(void);	\
	const char *name##__mount(void);	\
	bool name##__configured(void);		\

/*
 * The xxxx__mountpoint() entry points find the first match mount point for each
 * filesystems listed below, where xxxx is the filesystem type.
 *
 * The interface is as follows:
 *
 * - If a mount point is found on first call, it is cached and used for all
 *   subsequent calls.
 *
 * - If a mount point is not found, NULL is returned on first call and all
 *   subsequent calls.
 */
FS(sysfs)
FS(procfs)
FS(debugfs)
FS(tracefs)
FS(hugetlbfs)
FS(bpf_fs)

#undef FS


int filename__read_int(const char *filename, int *value);
int filename__read_ull(const char *filename, unsigned long long *value);
int filename__read_xll(const char *filename, unsigned long long *value);
int filename__read_str(const char *filename, char **buf, size_t *sizep);

int filename__write_int(const char *filename, int value);

int procfs__read_str(const char *entry, char **buf, size_t *sizep);

int sysctl__read_int(const char *sysctl, int *value);
int sysfs__read_int(const char *entry, int *value);
int sysfs__read_ull(const char *entry, unsigned long long *value);
int sysfs__read_xll(const char *entry, unsigned long long *value);
int sysfs__read_str(const char *entry, char **buf, size_t *sizep);
int sysfs__read_bool(const char *entry, bool *value);

int sysfs__write_int(const char *entry, int value);
#endif /* __API_FS__ */
