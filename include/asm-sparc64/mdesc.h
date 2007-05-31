#ifndef _SPARC64_MDESC_H
#define _SPARC64_MDESC_H

#include <linux/types.h>
#include <asm/prom.h>

struct mdesc_node;
struct mdesc_arc {
	const char		*name;
	struct mdesc_node	*arc;
};

struct mdesc_node {
	const char		*name;
	u64			node;
	unsigned int		unique_id;
	unsigned int		num_arcs;
	struct property		*properties;
	struct mdesc_node	*hash_next;
	struct mdesc_node	*allnodes_next;
	struct mdesc_arc	arcs[0];
};

extern struct mdesc_node *md_find_node_by_name(struct mdesc_node *from,
					       const char *name);
#define md_for_each_node_by_name(__mn, __name) \
	for (__mn = md_find_node_by_name(NULL, __name); __mn; \
	     __mn = md_find_node_by_name(__mn, __name))

extern struct property *md_find_property(const struct mdesc_node *mp,
					 const char *name,
					 int *lenp);
extern const void *md_get_property(const struct mdesc_node *mp,
				   const char *name,
				   int *lenp);

extern void sun4v_mdesc_init(void);

#endif
