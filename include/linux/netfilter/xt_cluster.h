#ifndef _XT_CLUSTER_MATCH_H
#define _XT_CLUSTER_MATCH_H

enum xt_cluster_flags {
	XT_CLUSTER_F_INV	= (1 << 0)
};

struct xt_cluster_match_info {
	u_int32_t		total_nodes;
	u_int32_t		node_mask;
	u_int32_t		hash_seed;
	u_int32_t		flags;
};

#define XT_CLUSTER_NODES_MAX	32

#endif /* _XT_CLUSTER_MATCH_H */
