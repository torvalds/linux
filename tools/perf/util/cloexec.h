#ifndef __PERF_CLOEXEC_H
#define __PERF_CLOEXEC_H

unsigned long perf_event_open_cloexec_flag(void);

#ifdef __GLIBC_PREREQ
#if !__GLIBC_PREREQ(2, 6)
extern int sched_getcpu(void) __THROW;
#endif
#endif

#endif /* __PERF_CLOEXEC_H */
