/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _X86_MEM_EVENTS_H
#define _X86_MEM_EVENTS_H

extern struct perf_mem_event perf_mem_events_intel[PERF_MEM_EVENTS__MAX];
extern struct perf_mem_event perf_mem_events_intel_aux[PERF_MEM_EVENTS__MAX];

extern struct perf_mem_event perf_mem_events_amd[PERF_MEM_EVENTS__MAX];

#endif /* _X86_MEM_EVENTS_H */
