#ifndef __NETNS_X_TABLES_H
#define __NETNS_X_TABLES_H

#include <linux/list.h>
#include <linux/netfilter.h>

struct ebt_table;

struct netns_xt {
	struct list_head tables[NFPROTO_NUMPROTO];
	bool notrack_deprecated_warning;
#if defined(CONFIG_BRIDGE_NF_EBTABLES) || \
    defined(CONFIG_BRIDGE_NF_EBTABLES_MODULE)
	struct ebt_table *broute_table;
	struct ebt_table *frame_filter;
	struct ebt_table *frame_nat;
#endif
#if IS_ENABLED(CONFIG_IP_NF_TARGET_ULOG)
	bool ulog_warn_deprecated;
#endif
#if IS_ENABLED(CONFIG_BRIDGE_EBT_ULOG)
	bool ebt_ulog_warn_deprecated;
#endif
};
#endif
