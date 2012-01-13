#ifndef __SUNRPC_NETNS_H__
#define __SUNRPC_NETNS_H__

#include <net/net_namespace.h>
#include <net/netns/generic.h>

struct cache_detail;

struct sunrpc_net {
	struct proc_dir_entry *proc_net_rpc;
	struct cache_detail *ip_map_cache;

	struct super_block *pipefs_sb;
	struct mutex pipefs_sb_lock;

	struct list_head all_clients;
	spinlock_t rpc_client_lock;

	struct rpc_clnt *rpcb_local_clnt;
	struct rpc_clnt *rpcb_local_clnt4;
	spinlock_t rpcb_clnt_lock;
	unsigned int rpcb_users;
};

extern int sunrpc_net_id;

int ip_map_cache_create(struct net *);
void ip_map_cache_destroy(struct net *);

#endif
