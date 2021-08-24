/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM shmem_fs
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SHMEM_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SHMEM_FS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct page;
DECLARE_HOOK(android_vh_shmem_alloc_page,
	TP_PROTO(struct page **page),
	TP_ARGS(page));
#endif /* _TRACE_HOOK_SHMEM_FS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
