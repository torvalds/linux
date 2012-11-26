/*
 * fs/nfs/nfs4session.h
 *
 * Copyright (c) 2012 Trond Myklebust <Trond.Myklebust@netapp.com>
 *
 */
#ifndef __LINUX_FS_NFS_NFS4SESSION_H
#define __LINUX_FS_NFS_NFS4SESSION_H

#if defined(CONFIG_NFS_V4_1)
extern struct nfs4_slot *nfs4_alloc_slot(struct nfs4_slot_table *tbl);
extern void nfs4_free_slot(struct nfs4_slot_table *tbl, struct nfs4_slot *slot);

extern void nfs41_set_target_slotid(struct nfs4_slot_table *tbl,
		u32 target_highest_slotid);
extern void nfs41_update_target_slotid(struct nfs4_slot_table *tbl,
		struct nfs4_slot *slot,
		struct nfs4_sequence_res *res);

extern int nfs4_setup_session_slot_tables(struct nfs4_session *ses);

extern struct nfs4_session *nfs4_alloc_session(struct nfs_client *clp);
extern void nfs4_destroy_session(struct nfs4_session *session);
extern int nfs4_init_session(struct nfs_server *server);
extern int nfs4_init_ds_session(struct nfs_client *, unsigned long);

#else /* defined(CONFIG_NFS_V4_1) */

static inline int nfs4_init_session(struct nfs_server *server)
{
	return 0;
}

#endif /* defined(CONFIG_NFS_V4_1) */
#endif /* __LINUX_FS_NFS_NFS4SESSION_H */
