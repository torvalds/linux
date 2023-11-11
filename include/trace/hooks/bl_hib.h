/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bl_hib

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BL_HIB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BL_HIB_H

#include <trace/hooks/vendor_hooks.h>

struct block_device;

DECLARE_HOOK(android_vh_check_hibernation_swap,
	TP_PROTO(struct block_device *resume_block, bool *hib_swap),
	TP_ARGS(resume_block, hib_swap));

DECLARE_HOOK(android_vh_save_cpu_resume,
	TP_PROTO(u64 *addr, u64 phys_addr),
	TP_ARGS(addr, phys_addr));

DECLARE_HOOK(android_vh_save_hib_resume_bdev,
	TP_PROTO(struct block_device *hib_resume_bdev),
	TP_ARGS(hib_resume_bdev));

DECLARE_HOOK(android_vh_encrypt_page,
	TP_PROTO(void *buf),
	TP_ARGS(buf));

DECLARE_HOOK(android_vh_init_aes_encrypt,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

DECLARE_HOOK(android_vh_skip_swap_map_write,
	TP_PROTO(bool *skip),
	TP_ARGS(skip));

DECLARE_HOOK(android_vh_post_image_save,
	TP_PROTO(unsigned short root_swap),
	TP_ARGS(root_swap));

#endif /* _TRACE_HOOK_BL_HIB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
