#ifndef _MEDUSA_CONFIG_H
#define _MEDUSA_CONFIG_H
#include <linux/spinlock.h>

/* configuration options */
//#include <linux/config.h>

#pragma GCC optimize ("Og")

/* operating system */
#define CONFIG_MEDUSA_VS 32

#define CONFIG_MEDUSA_FILE_CAPABILITIES
#define CONFIG_MEDUSA_FORCE
//#define CONFIG_MEDUSA_PROFILING
#define CONFIG_MEDUSA_SYSCALL
#define DEBUG
#define ERRORS_CAUSE_SEGFAULT
#define GDB_HACK
#define PARANOIA_CHECKS

#endif
