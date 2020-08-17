/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sched-domains (multiprocessor balancing) flag declarations.
 */

#ifndef SD_FLAG
# error "Incorrect import of SD flags definitions"
#endif

/* Balance when about to become idle */
SD_FLAG(SD_BALANCE_NEWIDLE)
/* Balance on exec */
SD_FLAG(SD_BALANCE_EXEC)
/* Balance on fork, clone */
SD_FLAG(SD_BALANCE_FORK)
/* Balance on wakeup */
SD_FLAG(SD_BALANCE_WAKE)
/* Wake task to waking CPU */
SD_FLAG(SD_WAKE_AFFINE)
/* Domain members have different CPU capacities */
SD_FLAG(SD_ASYM_CPUCAPACITY)
/* Domain members share CPU capacity */
SD_FLAG(SD_SHARE_CPUCAPACITY)
/* Domain members share CPU pkg resources */
SD_FLAG(SD_SHARE_PKG_RESOURCES)
/* Only a single load balancing instance */
SD_FLAG(SD_SERIALIZE)
/* Place busy groups earlier in the domain */
SD_FLAG(SD_ASYM_PACKING)
/* Prefer to place tasks in a sibling domain */
SD_FLAG(SD_PREFER_SIBLING)
/* sched_domains of this level overlap */
SD_FLAG(SD_OVERLAP)
/* cross-node balancing */
SD_FLAG(SD_NUMA)
