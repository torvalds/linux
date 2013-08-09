/*
 * fs/nfs/nfs4session.h
 *
 * Copyright (c) 2012 Trond Myklebust <Trond.Myklebust@netapp.com>
 *
 */
#ifndef __LINUX_FS_NFS_NFS4SESSION_H
#define __LINUX_FS_NFS_NFS4SESSION_H

/* maximum number of slots to use */
#define NFS4_DEF_SLOT_TABLE_SIZE (64U)
#define NFS4_MAX_SLOT_TABLE (1024U)
#define NFS4_NO_SLOT ((u32)-1)

#if IS_ENABLED(CONFIG_NFS_V4)

/* Sessions slot seqid */
struct nfs4_slot {
	struct nfs4_slot_table	*table;
	struct nfs4_slot	*next;
	unsigned long		generation;
	u32			slot_nr;
	u32		 	seq_nr;
	unsigned int		interrupted : 1;
};

/* Sessions */
enum nfs4_slot_tbl_state {
	NFS4_SLOT_TBL_DRAINING,
};

#define SLOT_TABLE_SZ DIV_ROUND_UP(NFS4_MAX_SLOT_TABLE, 8*sizeof(long))
struct nfs4_slot_table {
	struct nfs4_session *session;		/* Parent session */
	struct nfs4_slot *slots;		/* seqid per slot */
	unsigned long   used_slots[SLOT_TABLE_SZ]; /* used/unused bitmap */
	spinlock_t	slot_tbl_lock;
	struct rpc_wait_queue	slot_tbl_waitq;	/* allocators may wait here */
	u32		max_slots;		/* # slots in table */
	u32		max_slotid;		/* Max allowed slotid value */
	u32		highest_used_slotid;	/* sent to server on each SEQ.
						 * op for dynamic resizing */
	u32		target_highest_slotid;	/* Server max_slot target */
	u32		server_highest_slotid;	/* Server highest slotid */
	s32		d_target_highest_slotid; /* Derivative */
	s32		d2_target_highest_slotid; /* 2nd derivative */
	unsigned long	generation;		/* Generation counter for
						   target_highest_slotid */
	struct completion complete;
	unsigned long	slot_tbl_state;
};

/*
 * Session related parameters
 */
struct nfs4_session {
	struct nfs4_sessionid		sess_id;
	u32				flags;
	unsigned long			session_state;
	u32				hash_alg;
	u32				ssv_len;

	/* The fore and back channel */
	struct nfs4_channel_attrs	fc_attrs;
	struct nfs4_slot_table		fc_slot_table;
	struct nfs4_channel_attrs	bc_attrs;
	struct nfs4_slot_table		bc_slot_table;
	struct nfs_client		*clp;
};

enum nfs4_session_state {
	NFS4_SESSION_INITING,
};

extern int nfs4_setup_slot_table(struct nfs4_slot_table *tbl,
		unsigned int max_reqs, const char *queue);
extern void nfs4_release_slot_table(struct nfs4_slot_table *tbl);
extern struct nfs4_slot *nfs4_alloc_slot(struct nfs4_slot_table *tbl);
extern void nfs4_free_slot(struct nfs4_slot_table *tbl, struct nfs4_slot *slot);
extern void nfs4_slot_tbl_drain_complete(struct nfs4_slot_table *tbl);
extern void nfs41_update_target_slotid(struct nfs4_slot_table *tbl,
		struct nfs4_slot *slot,
		struct nfs4_sequence_res *res);
bool nfs41_wake_and_assign_slot(struct nfs4_slot_table *tbl,
		struct nfs4_slot *slot);
void nfs41_wake_slot_table(struct nfs4_slot_table *tbl);

static inline bool nfs4_slot_tbl_draining(struct nfs4_slot_table *tbl)
{
	return !!test_bit(NFS4_SLOT_TBL_DRAINING, &tbl->slot_tbl_state);
}

#if defined(CONFIG_NFS_V4_1)
extern void nfs41_set_target_slotid(struct nfs4_slot_table *tbl,
		u32 target_highest_slotid);

extern int nfs4_setup_session_slot_tables(struct nfs4_session *ses);

extern struct nfs4_session *nfs4_alloc_session(struct nfs_client *clp);
extern void nfs4_destroy_session(struct nfs4_session *session);
extern int nfs4_init_session(struct nfs_client *clp);
extern int nfs4_init_ds_session(struct nfs_client *, unsigned long);

/*
 * Determine if sessions are in use.
 */
static inline int nfs4_has_session(const struct nfs_client *clp)
{
	if (clp->cl_session)
		return 1;
	return 0;
}

static inline int nfs4_has_persistent_session(const struct nfs_client *clp)
{
	if (nfs4_has_session(clp))
		return (clp->cl_session->flags & SESSION4_PERSIST);
	return 0;
}

#ifdef CONFIG_CRC32
/*
 * nfs_session_id_hash - calculate the crc32 hash for the session id
 * @session - pointer to session
 */
#define nfs_session_id_hash(sess_id) \
	(~crc32_le(0xFFFFFFFF, &(sess_id)->data[0], sizeof((sess_id)->data)))
#else
#define nfs_session_id_hash(session) (0)
#endif
#else /* defined(CONFIG_NFS_V4_1) */

static inline int nfs4_init_session(struct nfs_client *clp)
{
	return 0;
}

/*
 * Determine if sessions are in use.
 */
static inline int nfs4_has_session(const struct nfs_client *clp)
{
	return 0;
}

static inline int nfs4_has_persistent_session(const struct nfs_client *clp)
{
	return 0;
}

#endif /* defined(CONFIG_NFS_V4_1) */
#endif /* IS_ENABLED(CONFIG_NFS_V4) */
#endif /* __LINUX_FS_NFS_NFS4SESSION_H */
