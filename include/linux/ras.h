#ifndef __RAS_H__
#define __RAS_H__

#include <asm/errno.h>

#ifdef CONFIG_DEBUG_FS
int ras_userspace_consumers(void);
void ras_debugfs_init(void);
int ras_add_daemon_trace(void);
#else
static inline int ras_userspace_consumers(void) { return 0; }
static inline void ras_debugfs_init(void) { }
static inline int ras_add_daemon_trace(void) { return 0; }
#endif

#ifdef CONFIG_RAS_CEC
void __init cec_init(void);
int __init parse_cec_param(char *str);
int cec_add_elem(u64 pfn);
#else
static inline void __init cec_init(void)	{ }
static inline int cec_add_elem(u64 pfn)		{ return -ENODEV; }
#endif

#endif /* __RAS_H__ */
