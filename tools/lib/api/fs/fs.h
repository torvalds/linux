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
#endif /* __API_FS__ */
