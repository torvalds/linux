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

FS(sysfs)
FS(procfs)
FS(debugfs)
FS(tracefs)

#undef FS


int filename__read_int(const char *filename, int *value);
int filename__read_ull(const char *filename, unsigned long long *value);
int filename__read_str(const char *filename, char **buf, size_t *sizep);

int sysctl__read_int(const char *sysctl, int *value);
int sysfs__read_int(const char *entry, int *value);
int sysfs__read_ull(const char *entry, unsigned long long *value);
int sysfs__read_str(const char *entry, char **buf, size_t *sizep);
#endif /* __API_FS__ */
