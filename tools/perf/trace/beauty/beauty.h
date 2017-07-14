#ifndef _PERF_TRACE_BEAUTY_H
#define _PERF_TRACE_BEAUTY_H

#include <linux/types.h>

struct trace;
struct thread;

struct syscall_arg {
	unsigned long val;
	struct thread *thread;
	struct trace  *trace;
	void	      *parm;
	u8	      idx;
	u8	      mask;
};

size_t syscall_arg__scnprintf_strarrays(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_STRARRAYS syscall_arg__scnprintf_strarrays

size_t syscall_arg__scnprintf_statx_flags(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_STATX_FLAGS syscall_arg__scnprintf_statx_flags

size_t syscall_arg__scnprintf_statx_mask(char *bf, size_t size, struct syscall_arg *arg);
#define SCA_STATX_MASK syscall_arg__scnprintf_statx_mask

#endif /* _PERF_TRACE_BEAUTY_H */
