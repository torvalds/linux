#ifndef __API_FS__
#define __API_FS__

#ifndef SYSFS_MAGIC
#define SYSFS_MAGIC            0x62656572
#endif

#ifndef PROC_SUPER_MAGIC
#define PROC_SUPER_MAGIC       0x9fa0
#endif

const char *sysfs__mountpoint(void);
const char *procfs__mountpoint(void);

int filename__read_int(const char *filename, int *value);
int sysctl__read_int(const char *sysctl, int *value);
#endif /* __API_FS__ */
