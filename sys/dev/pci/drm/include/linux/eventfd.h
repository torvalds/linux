/* Public domain. */

#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H

#include <sys/types.h>

struct eventfd_ctx {
};

static inline void
eventfd_signal(struct eventfd_ctx *c)
{
}

static inline void
eventfd_ctx_put(struct eventfd_ctx *c)
{
}

#endif
