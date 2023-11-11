/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmc
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MMC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MMC_H
#include <trace/hooks/vendor_hooks.h>
struct mmc_host;
struct mmc_card;
struct mmc_queue;

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_mmc_blk_mq_rw_recovery,
	TP_PROTO(struct mmc_card *card),
	TP_ARGS(card));

DECLARE_HOOK(android_vh_sd_update_bus_speed_mode,
	TP_PROTO(struct mmc_card *card),
	TP_ARGS(card));

DECLARE_RESTRICTED_HOOK(android_rvh_mmc_suspend,
	TP_PROTO(struct mmc_host *host),
	TP_ARGS(host), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_mmc_resume,
	TP_PROTO(struct mmc_host *host, bool *resume_success),
	TP_ARGS(host, resume_success), 1);

DECLARE_HOOK(android_vh_mmc_update_mmc_queue,
	TP_PROTO(struct mmc_card *card, struct mmc_queue *mq),
	TP_ARGS(card, mq));

#endif /* _TRACE_HOOK_MMC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
