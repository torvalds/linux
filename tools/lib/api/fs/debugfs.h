#ifndef __API_DEBUGFS_H__
#define __API_DEBUGFS_H__

#define _STR(x) #x
#define STR(x) _STR(x)

/*
 * On most systems <limits.h> would have given us this, but  not on some systems
 * (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef DEBUGFS_MAGIC
#define DEBUGFS_MAGIC          0x64626720
#endif

#ifndef PERF_DEBUGFS_ENVIRONMENT
#define PERF_DEBUGFS_ENVIRONMENT "PERF_DEBUGFS_DIR"
#endif

const char *debugfs_find_mountpoint(void);
int debugfs_valid_mountpoint(const char *debugfs);
char *debugfs_mount(const char *mountpoint);

extern char debugfs_mountpoint[];

int debugfs__strerror_open(int err, char *buf, size_t size, const char *filename);
int debugfs__strerror_open_tp(int err, char *buf, size_t size, const char *sys, const char *name);

#endif /* __API_DEBUGFS_H__ */
