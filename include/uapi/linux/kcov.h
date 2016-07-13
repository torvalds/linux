#ifndef _LINUX_KCOV_IOCTLS_H
#define _LINUX_KCOV_IOCTLS_H

#include <linux/types.h>

#define KCOV_INIT_TRACE			_IOR('c', 1, unsigned long)
#define KCOV_ENABLE			_IO('c', 100)
#define KCOV_DISABLE			_IO('c', 101)

#endif /* _LINUX_KCOV_IOCTLS_H */
