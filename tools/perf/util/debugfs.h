#ifndef __DEBUGFS_H__
#define __DEBUGFS_H__

const char *debugfs_find_mountpoint(void);
int debugfs_valid_mountpoint(const char *debugfs);
int debugfs_valid_entry(const char *path);
char *debugfs_mount(const char *mountpoint);
int debugfs_umount(void);
void debugfs_set_path(const char *mountpoint);
int debugfs_write(const char *entry, const char *value);
int debugfs_read(const char *entry, char *buffer, size_t size);
void debugfs_force_cleanup(void);
int debugfs_make_path(const char *element, char *buffer, int size);

extern char debugfs_mountpoint[];
extern char tracing_events_path[];

#endif /* __DEBUGFS_H__ */
