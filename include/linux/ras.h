#ifndef __RAS_H__
#define __RAS_H__

#ifdef CONFIG_DEBUG_FS
int ras_userspace_consumers(void);
void ras_debugfs_init(void);
int ras_add_daemon_trace(void);
#else
static inline int ras_userspace_consumers(void) { return 0; }
static inline void ras_debugfs_init(void) { return; }
static inline int ras_add_daemon_trace(void) { return 0; }
#endif

#endif
