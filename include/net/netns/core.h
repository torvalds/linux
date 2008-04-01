#ifndef __NETNS_CORE_H__
#define __NETNS_CORE_H__

struct ctl_table_header;

struct netns_core {
	/* core sysctls */
	struct ctl_table_header	*sysctl_hdr;

	int	sysctl_somaxconn;
};

#endif
