#ifndef __API_FS__
#define __API_FS__

/*
 * On most systems <limits.h> would have given us this, but  not on some systems
 * (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define FS(name)				\
	const char *name##__mountpoint(void);	\
	const char *name##__mount(void);

FS(sysfs)
FS(procfs)
FS(debugfs)
FS(tracefs)

#undef FS


int filename__read_int(const char *filename, int *value);
int sysctl__read_int(const char *sysctl, int *value);
#endif /* __API_FS__ */
