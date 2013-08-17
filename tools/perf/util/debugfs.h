#ifndef __DEBUGFS_H__
#define __DEBUGFS_H__

const char *debugfs_find_mountpoint(void);
int debugfs_valid_mountpoint(const char *debugfs);
char *debugfs_mount(const char *mountpoint);
void debugfs_set_path(const char *mountpoint);

extern char debugfs_mountpoint[];
extern char tracing_events_path[];

#endif /* __DEBUGFS_H__ */
