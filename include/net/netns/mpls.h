/*
 * mpls in net namespaces
 */

#ifndef __NETNS_MPLS_H__
#define __NETNS_MPLS_H__

struct mpls_route;

struct netns_mpls {
	size_t platform_labels;
	struct mpls_route __rcu * __rcu *platform_label;
};

#endif /* __NETNS_MPLS_H__ */
