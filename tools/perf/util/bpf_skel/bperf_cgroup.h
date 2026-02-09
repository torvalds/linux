/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Data structures shared between BPF and tools. */
#ifndef __BPERF_CGROUP_H
#define __BPERF_CGROUP_H

// These constants impact code size of bperf_cgroup.bpf.c that may result in BPF
// verifier issues. They are exposed to control the size and also to disable BPF
// counters when the number of user events is too large.

// max cgroup hierarchy level: arbitrary
#define BPERF_CGROUP__MAX_LEVELS  10
// max events per cgroup: arbitrary
#define BPERF_CGROUP__MAX_EVENTS  128

#endif /* __BPERF_CGROUP_H */
