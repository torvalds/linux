/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_PAPR_SCM_H
#define __LINUX_PAPR_SCM_H

/* DIMM health bitmap indicators */
/* SCM device is unable to persist memory contents */
#define PAPR_PMEM_UNARMED                   (1ULL << (63 - 0))
/* SCM device failed to persist memory contents */
#define PAPR_PMEM_SHUTDOWN_DIRTY            (1ULL << (63 - 1))
/* SCM device contents are persisted from previous IPL */
#define PAPR_PMEM_SHUTDOWN_CLEAN            (1ULL << (63 - 2))
/* SCM device contents are not persisted from previous IPL */
#define PAPR_PMEM_EMPTY                     (1ULL << (63 - 3))
/* SCM device memory life remaining is critically low */
#define PAPR_PMEM_HEALTH_CRITICAL           (1ULL << (63 - 4))
/* SCM device will be garded off next IPL due to failure */
#define PAPR_PMEM_HEALTH_FATAL              (1ULL << (63 - 5))
/* SCM contents cannot persist due to current platform health status */
#define PAPR_PMEM_HEALTH_UNHEALTHY          (1ULL << (63 - 6))
/* SCM device is unable to persist memory contents in certain conditions */
#define PAPR_PMEM_HEALTH_NON_CRITICAL       (1ULL << (63 - 7))
/* SCM device is encrypted */
#define PAPR_PMEM_ENCRYPTED                 (1ULL << (63 - 8))
/* SCM device has been scrubbed and locked */
#define PAPR_PMEM_SCRUBBED_AND_LOCKED       (1ULL << (63 - 9))

#define PAPR_PMEM_SAVE_FAILED               (1ULL << (63 - 10))

/* Bits status indicators for health bitmap indicating unarmed dimm */
#define PAPR_PMEM_UNARMED_MASK (PAPR_PMEM_UNARMED |            \
				PAPR_PMEM_HEALTH_UNHEALTHY)

/* Bits status indicators for health bitmap indicating unflushed dimm */
#define PAPR_PMEM_BAD_SHUTDOWN_MASK (PAPR_PMEM_SHUTDOWN_DIRTY)

/* Bits status indicators for health bitmap indicating unrestored dimm */
#define PAPR_PMEM_BAD_RESTORE_MASK  (PAPR_PMEM_EMPTY)

/* Bit status indicators for smart event notification */
#define PAPR_PMEM_SMART_EVENT_MASK (PAPR_PMEM_HEALTH_CRITICAL | \
					PAPR_PMEM_HEALTH_FATAL | \
					PAPR_PMEM_HEALTH_UNHEALTHY)

#define PAPR_PMEM_SAVE_MASK                (PAPR_PMEM_SAVE_FAILED)

#define PAPR_SCM_PERF_STATS_EYECATCHER __stringify(SCMSTATS)
#define PAPR_SCM_PERF_STATS_VERSION 0x1

#endif /* __LINUX_PAPR_SCM_H */
