/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>

struct shmem_inode_info;
struct folio;

DECLARE_RESTRICTED_HOOK(android_rvh_shmem_get_folio,
			TP_PROTO(struct shmem_inode_info *info, struct folio **folio),
			TP_ARGS(info, folio), 2);

DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);

#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
