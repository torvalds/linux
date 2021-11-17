#ifndef _UAPI__ASM_GENERIC_BPF_PERF_EVENT_H__
#define _UAPI__ASM_GENERIC_BPF_PERF_EVENT_H__

#include <linux/ptrace.h>

/* Export kernel pt_regs structure */
typedef struct pt_regs bpf_user_pt_regs_t;

#endif /* _UAPI__ASM_GENERIC_BPF_PERF_EVENT_H__ */
