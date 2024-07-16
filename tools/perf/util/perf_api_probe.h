
/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_API_PROBE_H
#define __PERF_API_PROBE_H

#include <stdbool.h>

bool perf_can_aux_sample(void);
bool perf_can_comm_exec(void);
bool perf_can_record_cpu_wide(void);
bool perf_can_record_switch_events(void);
bool perf_can_record_text_poke_events(void);
bool perf_can_sample_identifier(void);
bool perf_can_record_build_id(void);
bool perf_can_record_cgroup(void);

#endif // __PERF_API_PROBE_H
