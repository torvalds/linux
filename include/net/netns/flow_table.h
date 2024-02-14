/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_FLOW_TABLE_H
#define __NETNS_FLOW_TABLE_H

struct nf_flow_table_stat {
	unsigned int count_wq_add;
	unsigned int count_wq_del;
	unsigned int count_wq_stats;
};

struct netns_ft {
	struct nf_flow_table_stat __percpu *stat;
};
#endif
