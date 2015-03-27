#ifndef __API_TRACEFS_H__
#define __API_TRACEFS_H__

#include "findfs.h"

#ifndef TRACEFS_MAGIC
#define TRACEFS_MAGIC          0x74726163
#endif

#ifndef PERF_TRACEFS_ENVIRONMENT
#define PERF_TRACEFS_ENVIRONMENT "PERF_TRACEFS_DIR"
#endif

bool tracefs_configured(void);
const char *tracefs_find_mountpoint(void);
int tracefs_valid_mountpoint(const char *debugfs);
char *tracefs_mount(const char *mountpoint);

extern char tracefs_mountpoint[];

#endif /* __API_DEBUGFS_H__ */
