/* SPDX-License-Identifier: GPL-2.0 */
/* Based on net/wireless/trace.h */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cfg802154

#if !defined(__RDEV_CFG802154_OPS_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __RDEV_CFG802154_OPS_TRACE

#include <linux/tracepoint.h>

#include <net/cfg802154.h>

#define MAXNAME		32
#define WPAN_PHY_ENTRY	__array(char, wpan_phy_name, MAXNAME)
#define WPAN_PHY_ASSIGN	strlcpy(__entry->wpan_phy_name,	 \
				wpan_phy_name(wpan_phy), \
				MAXNAME)
#define WPAN_PHY_PR_FMT	"%s"
#define WPAN_PHY_PR_ARG	__entry->wpan_phy_name

#define WPAN_DEV_ENTRY	__field(u32, identifier)
#define WPAN_DEV_ASSIGN	(__entry->identifier) = (!IS_ERR_OR_NULL(wpan_dev) \
					 ? wpan_dev->identifier : 0)
#define WPAN_DEV_PR_FMT	"wpan_dev(%u)"
#define WPAN_DEV_PR_ARG	(__entry->identifier)

#define WPAN_CCA_ENTRY	__field(enum nl802154_cca_modes, cca_mode) \
			__field(enum nl802154_cca_opts, cca_opt)
#define WPAN_CCA_ASSIGN \
	do {					 \
		(__entry->cca_mode) = cca->mode; \
		(__entry->cca_opt) = cca->opt;	 \
	} while (0)
#define WPAN_CCA_PR_FMT	"cca_mode: %d, cca_opt: %d"
#define WPAN_CCA_PR_ARG __entry->cca_mode, __entry->cca_opt

#define BOOL_TO_STR(bo) (bo) ? "true" : "false"

/*************************************************************
 *			rdev->ops traces		     *
 *************************************************************/

DECLARE_EVENT_CLASS(wpan_phy_only_evt,
	TP_PROTO(struct wpan_phy *wpan_phy),
	TP_ARGS(wpan_phy),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
	),
	TP_printk(WPAN_PHY_PR_FMT, WPAN_PHY_PR_ARG)
);

DEFINE_EVENT(wpan_phy_only_evt, 802154_rdev_suspend,
	TP_PROTO(struct wpan_phy *wpan_phy),
	TP_ARGS(wpan_phy)
);

DEFINE_EVENT(wpan_phy_only_evt, 802154_rdev_resume,
	TP_PROTO(struct wpan_phy *wpan_phy),
	TP_ARGS(wpan_phy)
);

TRACE_EVENT(802154_rdev_add_virtual_intf,
	TP_PROTO(struct wpan_phy *wpan_phy, char *name,
		 enum nl802154_iftype type, __le64 extended_addr),
	TP_ARGS(wpan_phy, name, type, extended_addr),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		__string(vir_intf_name, name ? name : "<noname>")
		__field(enum nl802154_iftype, type)
		__field(__le64, extended_addr)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		__assign_str(vir_intf_name, name ? name : "<noname>");
		__entry->type = type;
		__entry->extended_addr = extended_addr;
	),
	TP_printk(WPAN_PHY_PR_FMT ", virtual intf name: %s, type: %d, extended addr: 0x%llx",
		  WPAN_PHY_PR_ARG, __get_str(vir_intf_name), __entry->type,
		  __le64_to_cpu(__entry->extended_addr))
);

TRACE_EVENT(802154_rdev_del_virtual_intf,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev),
	TP_ARGS(wpan_phy, wpan_dev),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
	),
	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT, WPAN_PHY_PR_ARG,
		  WPAN_DEV_PR_ARG)
);

TRACE_EVENT(802154_rdev_set_channel,
	TP_PROTO(struct wpan_phy *wpan_phy, u8 page, u8 channel),
	TP_ARGS(wpan_phy, page, channel),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		__field(u8, page)
		__field(u8, channel)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		__entry->page = page;
		__entry->channel = channel;
	),
	TP_printk(WPAN_PHY_PR_FMT ", page: %d, channel: %d", WPAN_PHY_PR_ARG,
		  __entry->page, __entry->channel)
);

TRACE_EVENT(802154_rdev_set_tx_power,
	TP_PROTO(struct wpan_phy *wpan_phy, s32 power),
	TP_ARGS(wpan_phy, power),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		__field(s32, power)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		__entry->power = power;
	),
	TP_printk(WPAN_PHY_PR_FMT ", mbm: %d", WPAN_PHY_PR_ARG,
		  __entry->power)
);

TRACE_EVENT(802154_rdev_set_cca_mode,
	TP_PROTO(struct wpan_phy *wpan_phy, const struct wpan_phy_cca *cca),
	TP_ARGS(wpan_phy, cca),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_CCA_ENTRY
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_CCA_ASSIGN;
	),
	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_CCA_PR_FMT, WPAN_PHY_PR_ARG,
		  WPAN_CCA_PR_ARG)
);

TRACE_EVENT(802154_rdev_set_cca_ed_level,
	TP_PROTO(struct wpan_phy *wpan_phy, s32 ed_level),
	TP_ARGS(wpan_phy, ed_level),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		__field(s32, ed_level)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		__entry->ed_level = ed_level;
	),
	TP_printk(WPAN_PHY_PR_FMT ", ed level: %d", WPAN_PHY_PR_ARG,
		  __entry->ed_level)
);

DECLARE_EVENT_CLASS(802154_le16_template,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 __le16 le16arg),
	TP_ARGS(wpan_phy, wpan_dev, le16arg),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
		__field(__le16, le16arg)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
		__entry->le16arg = le16arg;
	),
	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT ", pan id: 0x%04x",
		  WPAN_PHY_PR_ARG, WPAN_DEV_PR_ARG,
		  __le16_to_cpu(__entry->le16arg))
);

DEFINE_EVENT(802154_le16_template, 802154_rdev_set_pan_id,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 __le16 le16arg),
	TP_ARGS(wpan_phy, wpan_dev, le16arg)
);

DEFINE_EVENT_PRINT(802154_le16_template, 802154_rdev_set_short_addr,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 __le16 le16arg),
	TP_ARGS(wpan_phy, wpan_dev, le16arg),
	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT ", short addr: 0x%04x",
		  WPAN_PHY_PR_ARG, WPAN_DEV_PR_ARG,
		  __le16_to_cpu(__entry->le16arg))
);

TRACE_EVENT(802154_rdev_set_backoff_exponent,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 u8 min_be, u8 max_be),
	TP_ARGS(wpan_phy, wpan_dev, min_be, max_be),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
		__field(u8, min_be)
		__field(u8, max_be)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
		__entry->min_be = min_be;
		__entry->max_be = max_be;
	),

	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT
		  ", min be: %d, max be: %d", WPAN_PHY_PR_ARG,
		  WPAN_DEV_PR_ARG, __entry->min_be, __entry->max_be)
);

TRACE_EVENT(802154_rdev_set_csma_backoffs,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 u8 max_csma_backoffs),
	TP_ARGS(wpan_phy, wpan_dev, max_csma_backoffs),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
		__field(u8, max_csma_backoffs)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
		__entry->max_csma_backoffs = max_csma_backoffs;
	),

	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT
		  ", max csma backoffs: %d", WPAN_PHY_PR_ARG,
		  WPAN_DEV_PR_ARG, __entry->max_csma_backoffs)
);

TRACE_EVENT(802154_rdev_set_max_frame_retries,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 s8 max_frame_retries),
	TP_ARGS(wpan_phy, wpan_dev, max_frame_retries),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
		__field(s8, max_frame_retries)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
		__entry->max_frame_retries = max_frame_retries;
	),

	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT
		  ", max frame retries: %d", WPAN_PHY_PR_ARG,
		  WPAN_DEV_PR_ARG, __entry->max_frame_retries)
);

TRACE_EVENT(802154_rdev_set_lbt_mode,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 bool mode),
	TP_ARGS(wpan_phy, wpan_dev, mode),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
		__field(bool, mode)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
		__entry->mode = mode;
	),
	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT
		", lbt mode: %s", WPAN_PHY_PR_ARG,
		WPAN_DEV_PR_ARG, BOOL_TO_STR(__entry->mode))
);

TRACE_EVENT(802154_rdev_set_ackreq_default,
	TP_PROTO(struct wpan_phy *wpan_phy, struct wpan_dev *wpan_dev,
		 bool ackreq),
	TP_ARGS(wpan_phy, wpan_dev, ackreq),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		WPAN_DEV_ENTRY
		__field(bool, ackreq)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		WPAN_DEV_ASSIGN;
		__entry->ackreq = ackreq;
	),
	TP_printk(WPAN_PHY_PR_FMT ", " WPAN_DEV_PR_FMT
		", ackreq default: %s", WPAN_PHY_PR_ARG,
		WPAN_DEV_PR_ARG, BOOL_TO_STR(__entry->ackreq))
);

TRACE_EVENT(802154_rdev_return_int,
	TP_PROTO(struct wpan_phy *wpan_phy, int ret),
	TP_ARGS(wpan_phy, ret),
	TP_STRUCT__entry(
		WPAN_PHY_ENTRY
		__field(int, ret)
	),
	TP_fast_assign(
		WPAN_PHY_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(WPAN_PHY_PR_FMT ", returned: %d", WPAN_PHY_PR_ARG,
		  __entry->ret)
);

#endif /* !__RDEV_CFG802154_OPS_TRACE || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
