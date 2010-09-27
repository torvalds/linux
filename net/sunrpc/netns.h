#ifndef __SUNRPC_NETNS_H__
#define __SUNRPC_NETNS_H__

#include <net/net_namespace.h>
#include <net/netns/generic.h>

struct sunrpc_net {
	struct proc_dir_entry *proc_net_rpc;
};

extern int sunrpc_net_id;

#endif
