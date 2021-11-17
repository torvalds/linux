/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * per net namespace data structures for nfsd
 *
 * Copyright (C) 2012, Jeff Layton <jlayton@redhat.com>
 */

#ifndef __NFSD_NETNS_H__
#define __NFSD_NETNS_H__

#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <linux/percpu_counter.h>

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define SESSION_HASH_SIZE	512

struct cld_net;
struct nfsd4_client_tracking_ops;

enum {
	/* cache misses due only to checksum comparison failures */
	NFSD_NET_PAYLOAD_MISSES,
	/* amount of memory (in bytes) currently consumed by the DRC */
	NFSD_NET_DRC_MEM_USAGE,
	NFSD_NET_COUNTERS_NUM
};

/*
 * Represents a nfsd "container". With respect to nfsv4 state tracking, the
 * fields of interest are the *_id_hashtbls and the *_name_tree. These track
 * the nfs4_client objects by either short or long form clientid.
 *
 * Each nfsd_net runs a nfs4_laundromat workqueue job when necessary to clean
 * up expired clients and delegations within the container.
 */
struct nfsd_net {
	struct cld_net *cld_net;

	struct cache_detail *svc_expkey_cache;
	struct cache_detail *svc_export_cache;

	struct cache_detail *idtoname_cache;
	struct cache_detail *nametoid_cache;

	struct lock_manager nfsd4_manager;
	bool grace_ended;
	time64_t boot_time;

	struct dentry *nfsd_client_dir;

	/*
	 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
	 * used in reboot/reset lease grace period processing
	 *
	 * conf_id_hashtbl[], and conf_name_tree hold confirmed
	 * setclientid_confirmed info.
	 *
	 * unconf_str_hastbl[] and unconf_name_tree hold unconfirmed
	 * setclientid info.
	 */
	struct list_head *reclaim_str_hashtbl;
	int reclaim_str_hashtbl_size;
	struct list_head *conf_id_hashtbl;
	struct rb_root conf_name_tree;
	struct list_head *unconf_id_hashtbl;
	struct rb_root unconf_name_tree;
	struct list_head *sessionid_hashtbl;
	/*
	 * client_lru holds client queue ordered by nfs4_client.cl_time
	 * for lease renewal.
	 *
	 * close_lru holds (open) stateowner queue ordered by nfs4_stateowner.so_time
	 * for last close replay.
	 *
	 * All of the above fields are protected by the client_mutex.
	 */
	struct list_head client_lru;
	struct list_head close_lru;
	struct list_head del_recall_lru;

	/* protected by blocked_locks_lock */
	struct list_head blocked_locks_lru;

	struct delayed_work laundromat_work;

	/* client_lock protects the client lru list and session hash table */
	spinlock_t client_lock;

	/* protects blocked_locks_lru */
	spinlock_t blocked_locks_lock;

	struct file *rec_file;
	bool in_grace;
	const struct nfsd4_client_tracking_ops *client_tracking_ops;

	time64_t nfsd4_lease;
	time64_t nfsd4_grace;
	bool somebody_reclaimed;

	bool track_reclaim_completes;
	atomic_t nr_reclaim_complete;

	bool nfsd_net_up;
	bool lockd_up;

	/* Time of server startup */
	struct timespec64 nfssvc_boot;
	seqlock_t boot_lock;

	/*
	 * Max number of connections this nfsd container will allow. Defaults
	 * to '0' which is means that it bases this on the number of threads.
	 */
	unsigned int max_connections;

	u32 clientid_base;
	u32 clientid_counter;
	u32 clverifier_counter;

	struct svc_serv *nfsd_serv;

	wait_queue_head_t ntf_wq;
	atomic_t ntf_refcnt;

	/* Allow umount to wait for nfsd state cleanup */
	struct completion nfsd_shutdown_complete;

	/*
	 * clientid and stateid data for construction of net unique COPY
	 * stateids.
	 */
	u32		s2s_cp_cl_id;
	struct idr	s2s_cp_stateids;
	spinlock_t	s2s_cp_lock;

	/*
	 * Version information
	 */
	bool *nfsd_versions;
	bool *nfsd4_minorversions;

	/*
	 * Duplicate reply cache
	 */
	struct nfsd_drc_bucket   *drc_hashtbl;

	/* max number of entries allowed in the cache */
	unsigned int             max_drc_entries;

	/* number of significant bits in the hash value */
	unsigned int             maskbits;
	unsigned int             drc_hashsize;

	/*
	 * Stats and other tracking of on the duplicate reply cache.
	 * The longest_chain* fields are modified with only the per-bucket
	 * cache lock, which isn't really safe and should be fixed if we want
	 * these statistics to be completely accurate.
	 */

	/* total number of entries */
	atomic_t                 num_drc_entries;

	/* Per-netns stats counters */
	struct percpu_counter    counter[NFSD_NET_COUNTERS_NUM];

	/* longest hash chain seen */
	unsigned int             longest_chain;

	/* size of cache when we saw the longest hash chain */
	unsigned int             longest_chain_cachesize;

	struct shrinker		nfsd_reply_cache_shrinker;

	/* tracking server-to-server copy mounts */
	spinlock_t              nfsd_ssc_lock;
	struct list_head        nfsd_ssc_mount_list;
	wait_queue_head_t       nfsd_ssc_waitq;

	/* utsname taken from the process that starts the server */
	char			nfsd_name[UNX_MAXNODENAME+1];
};

/* Simple check to find out if a given net was properly initialized */
#define nfsd_netns_ready(nn) ((nn)->sessionid_hashtbl)

extern void nfsd_netns_free_versions(struct nfsd_net *nn);

extern unsigned int nfsd_net_id;

void nfsd_copy_boot_verifier(__be32 verf[2], struct nfsd_net *nn);
void nfsd_reset_boot_verifier(struct nfsd_net *nn);
#endif /* __NFSD_NETNS_H__ */
