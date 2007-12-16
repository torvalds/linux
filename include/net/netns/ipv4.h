/*
 * ipv4 in net namespaces
 */

#ifndef __NETNS_IPV4_H__
#define __NETNS_IPV4_H__
struct ctl_table_header;
struct ipv4_devconf;

struct netns_ipv4 {
	struct ctl_table_header	*forw_hdr;
	struct ipv4_devconf	*devconf_all;
	struct ipv4_devconf	*devconf_dflt;
};
#endif
