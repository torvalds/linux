#ifndef _NF_FLOW_TABLE_H
#define _NF_FLOW_TABLE_H

#include <linux/rhashtable.h>

struct nf_flowtable;

struct nf_flowtable_type {
	struct list_head		list;
	int				family;
	void				(*gc)(struct work_struct *work);
	const struct rhashtable_params	*params;
	nf_hookfn			*hook;
	struct module			*owner;
};

struct nf_flowtable {
	struct rhashtable		rhashtable;
	const struct nf_flowtable_type	*type;
	struct delayed_work		gc_work;
};

#endif /* _FLOW_OFFLOAD_H */
