/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NFS-private data for each "struct net".  Accessed with net_generic().
 */

#ifndef __NFS_NETNS_H__
#define __NFS_NETNS_H__

#include <linux/nfs4.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <linux/completion.h>

struct bl_dev_msg {
	int32_t status;
	uint32_t major, minor;
};

struct key;
struct nfs_netns_client;

struct nfs_net {
	struct cache_detail *nfs_dns_resolve;
	struct rpc_pipe *bl_device_pipe;
	struct rpc_pipe_msg bl_pipe_msg;
	struct bl_dev_msg bl_mount_reply;
	struct completion bl_done;
	struct mutex bl_mutex;
	struct list_head nfs_client_list;
	struct list_head nfs_volume_list;
#if IS_ENABLED(CONFIG_NFS_V4)
	struct idr cb_ident_idr; /* Protected by nfs_client_lock */
	unsigned short nfs_callback_tcpport;
	unsigned short nfs_callback_tcpport6;
	int cb_users[NFS4_MAX_MINOR_VERSION + 1];
#define NFS4_DS_CACHE_HASH_BITS 8
#define NFS4_DS_CACHE_HASH_SIZE (1 << NFS4_DS_CACHE_HASH_BITS)
	/* every entry is still in bucket 0 until the key is added */
	struct hlist_head nfs4_data_server_cache[NFS4_DS_CACHE_HASH_SIZE];
	spinlock_t nfs4_data_server_lock;
#endif /* CONFIG_NFS_V4 */
	struct nfs_netns_client *nfs_client;
	spinlock_t nfs_client_lock;
	struct key *nfs_keyring;
	ktime_t boot_time;
	struct rpc_stat rpcstats;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_nfsfs;
#endif
};

extern unsigned int nfs_net_id;

static inline struct key *nfs_net_keyring(struct net *net)
{
	struct nfs_net *nn = net_generic(net, nfs_net_id);

	return nn->nfs_keyring;
}

#endif
