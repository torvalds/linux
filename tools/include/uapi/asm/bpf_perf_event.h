#if defined(__aarch64__)
#include "../../arch/arm64/include/uapi/asm/bpf_perf_event.h"
#elif defined(__s390__)
#include "../../arch/s390/include/uapi/asm/bpf_perf_event.h"
#else
#include <uapi/asm-generic/bpf_perf_event.h>
#endif
