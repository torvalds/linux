#ifndef __API_FINDFS_H__
#define __API_FINDFS_H__

#include <stdbool.h>

#define _STR(x) #x
#define STR(x) _STR(x)

/*
 * On most systems <limits.h> would have given us this, but  not on some systems
 * (e.g. GNU/Hurd).
 */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *find_mountpoint(const char *fstype, long magic,
			    char *mountpoint, int len,
			    const char * const *known_mountpoints);

int valid_mountpoint(const char *mount, long magic);

#endif /* __API_FINDFS_H__ */
