#ifndef __API_FS__
#define __API_FS__

/*
 * On most systems <limits.h> would have given us this, but  not on some systems
 * (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *sysfs__mountpoint(void);
const char *procfs__mountpoint(void);
const char *debugfs__mountpoint(void);

int filename__read_int(const char *filename, int *value);
int sysctl__read_int(const char *sysctl, int *value);
#endif /* __API_FS__ */
