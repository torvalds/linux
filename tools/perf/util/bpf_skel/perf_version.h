/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */

#ifndef __PERF_VERSION_H__
#define __PERF_VERSION_H__

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

/*
 * This is used by tests/shell/record_bpf_metadata.sh
 * to verify that BPF metadata generation works.
 *
 * PERF_VERSION is defined by a build rule at compile time.
 */
const char bpf_metadata_perf_version[] SEC(".rodata") = PERF_VERSION;

#endif /* __PERF_VERSION_H__ */
