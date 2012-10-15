/*
 * NFS-private data for each "struct net".  Accessed with net_generic().
 */

#ifndef __NFS_NETNS_H__
#define __NFS_NETNS_H__

#include <net/net_namespace.h>
#include <net/netns/generic.h>

struct bl_dev_msg {
	int32_t status;
	uint32_t major, minor;
};

struct nfs_net {
	struct cache_detail *nfs_dns_resolve;
	struct rpc_pipe *bl_device_pipe;
	struct bl_dev_msg bl_mount_reply;
	wait_queue_head_t bl_wq;
	struct list_head nfs_client_list;
	struct list_head nfs_volume_list;
#if IS_ENABLED(CONFIG_NFS_V4)
	struct idr cb_ident_idr; /* Protected by nfs_client_lock */
#endif
	spinlock_t nfs_client_lock;
	struct timespec boot_time;
};

extern int nfs_net_id;

#endif
