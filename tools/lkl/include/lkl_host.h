#ifndef _LKL_HOST_H
#define _LKL_HOST_H

#include <lkl/asm/host_ops.h>
#include <lkl.h>

extern struct lkl_host_operations lkl_host_ops;

/**
 * lkl_printf - print a message via the host print operation
 *
 * @fmt - printf like format string
 */
int lkl_printf(const char *fmt, ...);

#endif
