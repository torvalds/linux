/* Public domain. */

#ifndef _LINUX_PERF_EVENT_H
#define _LINUX_PERF_EVENT_H

#include <linux/ftrace.h>
#include <linux/file.h> /* via security.h -> kernel_read_file.h */
#include <linux/seq_file.h> /* via linux/cgroup.h */

struct pmu {
};

#endif
