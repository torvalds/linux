/* Based on net/mac80211/trace.h */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mac802154

#if !defined(__MAC802154_DRIVER_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __MAC802154_DRIVER_TRACE

#include <linux/tracepoint.h>

#include <net/mac802154.h>
#include "ieee802154_i.h"

#define MAXNAME		32
#define LOCAL_ENTRY	__array(char, wpan_phy_name, MAXNAME)
#define LOCAL_ASSIGN	strlcpy(__entry->wpan_phy_name, \
				wpan_phy_name(local->hw.phy), MAXNAME)
#define LOCAL_PR_FMT	"%s"
#define LOCAL_PR_ARG	__entry->wpan_phy_name

#define CCA_ENTRY __field(enum nl802154_cca_modes, cca_mode) \
		  __field(enum nl802154_cca_opts, cca_opt)
#define CCA_ASSIGN \
	do {                                     \
		(__entry->cca_mode) = cca->mode; \
		(__entry->cca_opt) = cca->opt;   \
	} while (0)
#define CCA_PR_FMT "cca_mode: %d, cca_opt: %d"
#define CCA_PR_ARG __entry->cca_mode, __entry->cca_opt

#define BOOL_TO_STR(bo) (bo) ? "true" : "false"

/* Tracing for driver callbacks */

DECLARE_EVENT_CLASS(local_only_evt,
	TP_PROTO(struct ieee802154_local *local),
	TP_ARGS(local),
	TP_STRUCT__entry(
		LOCAL_ENTRY
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
	),
	TP_printk(LOCAL_PR_FMT, LOCAL_PR_ARG)
);

DEFINE_EVENT(local_only_evt, 802154_drv_return_void,
	TP_PROTO(struct ieee802154_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(802154_drv_return_int,
	TP_PROTO(struct ieee802154_local *local, int ret),
	TP_ARGS(local, ret),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, ret)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(LOCAL_PR_FMT ", returned: %d", LOCAL_PR_ARG,
		  __entry->ret)
);

DEFINE_EVENT(local_only_evt, 802154_drv_start,
	TP_PROTO(struct ieee802154_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_only_evt, 802154_drv_stop,
	TP_PROTO(struct ieee802154_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(802154_drv_set_channel,
	TP_PROTO(struct ieee802154_local *local, u8 page, u8 channel),
	TP_ARGS(local, page, channel),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u8, page)
		__field(u8, channel)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->page = page;
		__entry->channel = channel;
	),
	TP_printk(LOCAL_PR_FMT ", page: %d, channel: %d", LOCAL_PR_ARG,
		  __entry->page, __entry->channel)
);

TRACE_EVENT(802154_drv_set_cca_mode,
	TP_PROTO(struct ieee802154_local *local,
		 const struct wpan_phy_cca *cca),
	TP_ARGS(local, cca),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		CCA_ENTRY
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		CCA_ASSIGN;
	),
	TP_printk(LOCAL_PR_FMT ", " CCA_PR_FMT, LOCAL_PR_ARG,
		  CCA_PR_ARG)
);

TRACE_EVENT(802154_drv_set_cca_ed_level,
	TP_PROTO(struct ieee802154_local *local, s32 mbm),
	TP_ARGS(local, mbm),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(s32, mbm)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->mbm = mbm;
	),
	TP_printk(LOCAL_PR_FMT ", ed level: %d", LOCAL_PR_ARG,
		  __entry->mbm)
);

TRACE_EVENT(802154_drv_set_tx_power,
	TP_PROTO(struct ieee802154_local *local, s32 power),
	TP_ARGS(local, power),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(s32, power)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->power = power;
	),
	TP_printk(LOCAL_PR_FMT ", mbm: %d", LOCAL_PR_ARG,
		 __entry->power)
);

TRACE_EVENT(802154_drv_set_lbt_mode,
	TP_PROTO(struct ieee802154_local *local, bool mode),
	TP_ARGS(local, mode),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, mode)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->mode = mode;
	),
	TP_printk(LOCAL_PR_FMT ", lbt mode: %s", LOCAL_PR_ARG,
		  BOOL_TO_STR(__entry->mode))
);

TRACE_EVENT(802154_drv_set_short_addr,
	TP_PROTO(struct ieee802154_local *local, __le16 short_addr),
	TP_ARGS(local, short_addr),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(__le16, short_addr)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->short_addr = short_addr;
	),
	TP_printk(LOCAL_PR_FMT ", short addr: 0x%04x", LOCAL_PR_ARG,
		  le16_to_cpu(__entry->short_addr))
);

TRACE_EVENT(802154_drv_set_pan_id,
	TP_PROTO(struct ieee802154_local *local, __le16 pan_id),
	TP_ARGS(local, pan_id),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(__le16, pan_id)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->pan_id = pan_id;
	),
	TP_printk(LOCAL_PR_FMT ", pan id: 0x%04x", LOCAL_PR_ARG,
		  le16_to_cpu(__entry->pan_id))
);

TRACE_EVENT(802154_drv_set_extended_addr,
	TP_PROTO(struct ieee802154_local *local, __le64 extended_addr),
	TP_ARGS(local, extended_addr),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(__le64, extended_addr)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->extended_addr = extended_addr;
	),
	TP_printk(LOCAL_PR_FMT ", extended addr: 0x%llx", LOCAL_PR_ARG,
		  le64_to_cpu(__entry->extended_addr))
);

TRACE_EVENT(802154_drv_set_pan_coord,
	TP_PROTO(struct ieee802154_local *local, bool is_coord),
	TP_ARGS(local, is_coord),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, is_coord)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->is_coord = is_coord;
	),
	TP_printk(LOCAL_PR_FMT ", is_coord: %s", LOCAL_PR_ARG,
		  BOOL_TO_STR(__entry->is_coord))
);

TRACE_EVENT(802154_drv_set_csma_params,
	TP_PROTO(struct ieee802154_local *local, u8 min_be, u8 max_be,
		 u8 max_csma_backoffs),
	TP_ARGS(local, min_be, max_be, max_csma_backoffs),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u8, min_be)
		__field(u8, max_be)
		__field(u8, max_csma_backoffs)
	),
	TP_fast_assign(
		LOCAL_ASSIGN,
		__entry->min_be = min_be;
		__entry->max_be = max_be;
		__entry->max_csma_backoffs = max_csma_backoffs;
	),
	TP_printk(LOCAL_PR_FMT ", min be: %d, max be: %d, max csma backoffs: %d",
		  LOCAL_PR_ARG, __entry->min_be, __entry->max_be,
		  __entry->max_csma_backoffs)
);

TRACE_EVENT(802154_drv_set_max_frame_retries,
	TP_PROTO(struct ieee802154_local *local, s8 max_frame_retries),
	TP_ARGS(local, max_frame_retries),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(s8, max_frame_retries)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->max_frame_retries = max_frame_retries;
	),
	TP_printk(LOCAL_PR_FMT ", max frame retries: %d", LOCAL_PR_ARG,
		  __entry->max_frame_retries)
);

TRACE_EVENT(802154_drv_set_promiscuous_mode,
	TP_PROTO(struct ieee802154_local *local, bool on),
	TP_ARGS(local, on),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, on)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->on = on;
	),
	TP_printk(LOCAL_PR_FMT ", promiscuous mode: %s", LOCAL_PR_ARG,
		  BOOL_TO_STR(__entry->on))
);

#endif /* !__MAC802154_DRIVER_TRACE || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
