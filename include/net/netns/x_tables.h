/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_X_TABLES_H
#define __NETNS_X_TABLES_H

#include <linux/list.h>
#include <linux/netfilter_defs.h>

struct netns_xt {
	bool notrack_deprecated_warning;
	bool clusterip_deprecated_warning;
};
#endif
