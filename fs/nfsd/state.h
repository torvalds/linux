/*
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson <andros@umich.edu>
 *  
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _NFSD4_STATE_H
#define _NFSD4_STATE_H

#include <linux/idr.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/nfsd/nfsfh.h>
#include "nfsfh.h"

typedef struct {
	u32             cl_boot;
	u32             cl_id;
} clientid_t;

typedef struct {
	clientid_t	so_clid;
	u32		so_id;
} stateid_opaque_t;

typedef struct {
	u32                     si_generation;
	stateid_opaque_t        si_opaque;
} stateid_t;

#define STATEID_FMT	"(%08x/%08x/%08x/%08x)"
#define STATEID_VAL(s) \
	(s)->si_opaque.so_clid.cl_boot, \
	(s)->si_opaque.so_clid.cl_id, \
	(s)->si_opaque.so_id, \
	(s)->si_generation

struct nfsd4_callback {
	void *cb_op;
	struct nfs4_client *cb_clp;
	struct list_head cb_per_client;
	u32 cb_minorversion;
	struct rpc_message cb_msg;
	const struct rpc_call_ops *cb_ops;
	struct work_struct cb_work;
	bool cb_done;
};

struct nfs4_stid {
#define NFS4_OPEN_STID 1
#define NFS4_LOCK_STID 2
#define NFS4_DELEG_STID 4
/* For an open stateid kept around *only* to process close replays: */
#define NFS4_CLOSED_STID 8
	unsigned char sc_type;
	stateid_t sc_stateid;
	struct nfs4_client *sc_client;
};

struct nfs4_delegation {
	struct nfs4_stid	dl_stid; /* must be first field */
	struct list_head	dl_perfile;
	struct list_head	dl_perclnt;
	struct list_head	dl_recall_lru;  /* delegation recalled */
	atomic_t		dl_count;       /* ref count */
	struct nfs4_file	*dl_file;
	u32			dl_type;
	time_t			dl_time;
/* For recall: */
	struct knfsd_fh		dl_fh;
	int			dl_retries;
	struct nfsd4_callback	dl_recall;
};

/* client delegation callback info */
struct nfs4_cb_conn {
	/* SETCLIENTID info */
	struct sockaddr_storage	cb_addr;
	struct sockaddr_storage	cb_saddr;
	size_t			cb_addrlen;
	u32                     cb_prog; /* used only in 4.0 case;
					    per-session otherwise */
	u32                     cb_ident;	/* minorversion 0 only */
	struct svc_xprt		*cb_xprt;	/* minorversion 1 only */
};

static inline struct nfs4_delegation *delegstateid(struct nfs4_stid *s)
{
	return container_of(s, struct nfs4_delegation, dl_stid);
}

/* Maximum number of slots per session. 160 is useful for long haul TCP */
#define NFSD_MAX_SLOTS_PER_SESSION     160
/* Maximum number of operations per session compound */
#define NFSD_MAX_OPS_PER_COMPOUND	16
/* Maximum  session per slot cache size */
#define NFSD_SLOT_CACHE_SIZE		1024
/* Maximum number of NFSD_SLOT_CACHE_SIZE slots per session */
#define NFSD_CACHE_SIZE_SLOTS_PER_SESSION	32
#define NFSD_MAX_MEM_PER_SESSION  \
		(NFSD_CACHE_SIZE_SLOTS_PER_SESSION * NFSD_SLOT_CACHE_SIZE)

struct nfsd4_slot {
	u32	sl_seqid;
	__be32	sl_status;
	u32	sl_datalen;
	u16	sl_opcnt;
#define NFSD4_SLOT_INUSE	(1 << 0)
#define NFSD4_SLOT_CACHETHIS	(1 << 1)
#define NFSD4_SLOT_INITIALIZED	(1 << 2)
	u8	sl_flags;
	char	sl_data[];
};

struct nfsd4_channel_attrs {
	u32		headerpadsz;
	u32		maxreq_sz;
	u32		maxresp_sz;
	u32		maxresp_cached;
	u32		maxops;
	u32		maxreqs;
	u32		nr_rdma_attrs;
	u32		rdma_attrs;
};

struct nfsd4_cb_sec {
	u32	flavor; /* (u32)(-1) used to mean "no valid flavor" */
	u32	uid;
	u32	gid;
};

struct nfsd4_create_session {
	clientid_t			clientid;
	struct nfs4_sessionid		sessionid;
	u32				seqid;
	u32				flags;
	struct nfsd4_channel_attrs	fore_channel;
	struct nfsd4_channel_attrs	back_channel;
	u32				callback_prog;
	struct nfsd4_cb_sec		cb_sec;
};

struct nfsd4_backchannel_ctl {
	u32	bc_cb_program;
	struct nfsd4_cb_sec		bc_cb_sec;
};

struct nfsd4_bind_conn_to_session {
	struct nfs4_sessionid		sessionid;
	u32				dir;
};

/* The single slot clientid cache structure */
struct nfsd4_clid_slot {
	u32				sl_seqid;
	__be32				sl_status;
	struct nfsd4_create_session	sl_cr_ses;
};

struct nfsd4_conn {
	struct list_head cn_persession;
	struct svc_xprt *cn_xprt;
	struct svc_xpt_user cn_xpt_user;
	struct nfsd4_session *cn_session;
/* CDFC4_FORE, CDFC4_BACK: */
	unsigned char cn_flags;
};

struct nfsd4_session {
	struct kref		se_ref;
	struct list_head	se_hash;	/* hash by sessionid */
	struct list_head	se_perclnt;
	u32			se_flags;
	struct nfs4_client	*se_client;
	struct nfs4_sessionid	se_sessionid;
	struct nfsd4_channel_attrs se_fchannel;
	struct nfsd4_channel_attrs se_bchannel;
	struct nfsd4_cb_sec	se_cb_sec;
	struct list_head	se_conns;
	u32			se_cb_prog;
	u32			se_cb_seq_nr;
	struct nfsd4_slot	*se_slots[];	/* forward channel slots */
};

extern void nfsd4_put_session(struct nfsd4_session *ses);

/* formatted contents of nfs4_sessionid */
struct nfsd4_sessionid {
	clientid_t	clientid;
	u32		sequence;
	u32		reserved;
};

#define HEXDIR_LEN     33 /* hex version of 16 byte md5 of cl_name plus '\0' */

/*
 * struct nfs4_client - one per client.  Clientids live here.
 * 	o Each nfs4_client is hashed by clientid.
 *
 * 	o Each nfs4_clients is also hashed by name 
 * 	  (the opaque quantity initially sent by the client to identify itself).
 * 	  
 *	o cl_perclient list is used to ensure no dangling stateowner references
 *	  when we expire the nfs4_client
 */
struct nfs4_client {
	struct list_head	cl_idhash; 	/* hash by cl_clientid.id */
	struct rb_node		cl_namenode;	/* link into by-name trees */
	struct list_head	cl_openowners;
	struct idr		cl_stateids;	/* stateid lookup */
	struct list_head	cl_delegations;
	struct list_head        cl_lru;         /* tail queue */
	struct xdr_netobj	cl_name; 	/* id generated by client */
	char                    cl_recdir[HEXDIR_LEN]; /* recovery dir */
	nfs4_verifier		cl_verifier; 	/* generated by client */
	time_t                  cl_time;        /* time of last lease renewal */
	struct sockaddr_storage	cl_addr; 	/* client ipaddress */
	struct svc_cred		cl_cred; 	/* setclientid principal */
	clientid_t		cl_clientid;	/* generated by server */
	nfs4_verifier		cl_confirm;	/* generated by server */
	u32			cl_minorversion;

	/* for v4.0 and v4.1 callbacks: */
	struct nfs4_cb_conn	cl_cb_conn;
#define NFSD4_CLIENT_CB_UPDATE		(0)
#define NFSD4_CLIENT_CB_KILL		(1)
#define NFSD4_CLIENT_STABLE		(2)	/* client on stable storage */
#define NFSD4_CLIENT_RECLAIM_COMPLETE	(3)	/* reclaim_complete done */
#define NFSD4_CLIENT_CONFIRMED		(4)	/* client is confirmed */
#define NFSD4_CLIENT_CB_FLAG_MASK	(1 << NFSD4_CLIENT_CB_UPDATE | \
					 1 << NFSD4_CLIENT_CB_KILL)
	unsigned long		cl_flags;
	struct rpc_cred		*cl_cb_cred;
	struct rpc_clnt		*cl_cb_client;
	u32			cl_cb_ident;
#define NFSD4_CB_UP		0
#define NFSD4_CB_UNKNOWN	1
#define NFSD4_CB_DOWN		2
#define NFSD4_CB_FAULT		3
	int			cl_cb_state;
	struct nfsd4_callback	cl_cb_null;
	struct nfsd4_session	*cl_cb_session;
	struct list_head	cl_callbacks; /* list of in-progress callbacks */

	/* for all client information that callback code might need: */
	spinlock_t		cl_lock;

	/* for nfs41 */
	struct list_head	cl_sessions;
	struct nfsd4_clid_slot	cl_cs_slot;	/* create_session slot */
	u32			cl_exchange_flags;
	/* number of rpc's in progress over an associated session: */
	atomic_t		cl_refcount;

	/* for nfs41 callbacks */
	/* We currently support a single back channel with a single slot */
	unsigned long		cl_cb_slot_busy;
	struct rpc_wait_queue	cl_cb_waitq;	/* backchannel callers may */
						/* wait here for slots */
};

static inline void
mark_client_expired(struct nfs4_client *clp)
{
	clp->cl_time = 0;
}

static inline bool
is_client_expired(struct nfs4_client *clp)
{
	return clp->cl_time == 0;
}

/* struct nfs4_client_reset
 * one per old client. Populates reset_str_hashtbl. Filled from conf_id_hashtbl
 * upon lease reset, or from upcall to state_daemon (to read in state
 * from non-volitile storage) upon reboot.
 */
struct nfs4_client_reclaim {
	struct list_head	cr_strhash;	/* hash by cr_name */
	struct nfs4_client	*cr_clp;	/* pointer to associated clp */
	char			cr_recdir[HEXDIR_LEN]; /* recover dir */
};

static inline void
update_stateid(stateid_t *stateid)
{
	stateid->si_generation++;
	/* Wraparound recommendation from 3530bis-13 9.1.3.2: */
	if (stateid->si_generation == 0)
		stateid->si_generation = 1;
}

/* A reasonable value for REPLAY_ISIZE was estimated as follows:  
 * The OPEN response, typically the largest, requires 
 *   4(status) + 8(stateid) + 20(changeinfo) + 4(rflags) +  8(verifier) + 
 *   4(deleg. type) + 8(deleg. stateid) + 4(deleg. recall flag) + 
 *   20(deleg. space limit) + ~32(deleg. ace) = 112 bytes 
 */

#define NFSD4_REPLAY_ISIZE       112 

/*
 * Replay buffer, where the result of the last seqid-mutating operation 
 * is cached. 
 */
struct nfs4_replay {
	__be32			rp_status;
	unsigned int		rp_buflen;
	char			*rp_buf;
	struct knfsd_fh		rp_openfh;
	char			rp_ibuf[NFSD4_REPLAY_ISIZE];
};

struct nfs4_stateowner {
	struct list_head        so_strhash;   /* hash by op_name */
	struct list_head        so_stateids;
	struct nfs4_client *    so_client;
	/* after increment in ENCODE_SEQID_OP_TAIL, represents the next
	 * sequence id expected from the client: */
	u32                     so_seqid;
	struct xdr_netobj       so_owner;     /* open owner name */
	struct nfs4_replay	so_replay;
	bool			so_is_open_owner;
};

struct nfs4_openowner {
	struct nfs4_stateowner	oo_owner; /* must be first field */
	struct list_head        oo_perclient;
	/*
	 * We keep around openowners a little while after last close,
	 * which saves clients from having to confirm, and allows us to
	 * handle close replays if they come soon enough.  The close_lru
	 * is a list of such openowners, to be reaped by the laundromat
	 * thread eventually if they remain unused:
	 */
	struct list_head	oo_close_lru;
	struct nfs4_ol_stateid *oo_last_closed_stid;
	time_t			oo_time; /* time of placement on so_close_lru */
#define NFS4_OO_CONFIRMED   1
#define NFS4_OO_PURGE_CLOSE 2
#define NFS4_OO_NEW         4
	unsigned char		oo_flags;
};

struct nfs4_lockowner {
	struct nfs4_stateowner	lo_owner; /* must be first element */
	struct list_head	lo_owner_ino_hash; /* hash by owner,file */
	struct list_head        lo_perstateid; /* for lockowners only */
	struct list_head	lo_list; /* for temporary uses */
};

static inline struct nfs4_openowner * openowner(struct nfs4_stateowner *so)
{
	return container_of(so, struct nfs4_openowner, oo_owner);
}

static inline struct nfs4_lockowner * lockowner(struct nfs4_stateowner *so)
{
	return container_of(so, struct nfs4_lockowner, lo_owner);
}

/* nfs4_file: a file opened by some number of (open) nfs4_stateowners. */
struct nfs4_file {
	atomic_t		fi_ref;
	struct list_head        fi_hash;    /* hash by "struct inode *" */
	struct list_head        fi_stateids;
	struct list_head	fi_delegations;
	/* One each for O_RDONLY, O_WRONLY, O_RDWR: */
	struct file *		fi_fds[3];
	/*
	 * Each open or lock stateid contributes 0-4 to the counts
	 * below depending on which bits are set in st_access_bitmap:
	 *     1 to fi_access[O_RDONLY] if NFS4_SHARE_ACCES_READ is set
	 *   + 1 to fi_access[O_WRONLY] if NFS4_SHARE_ACCESS_WRITE is set
	 *   + 1 to both of the above if NFS4_SHARE_ACCESS_BOTH is set.
	 */
	atomic_t		fi_access[2];
	struct file		*fi_deleg_file;
	struct file_lock	*fi_lease;
	atomic_t		fi_delegees;
	struct inode		*fi_inode;
	bool			fi_had_conflict;
};

/* XXX: for first cut may fall back on returning file that doesn't work
 * at all? */
static inline struct file *find_writeable_file(struct nfs4_file *f)
{
	if (f->fi_fds[O_WRONLY])
		return f->fi_fds[O_WRONLY];
	return f->fi_fds[O_RDWR];
}

static inline struct file *find_readable_file(struct nfs4_file *f)
{
	if (f->fi_fds[O_RDONLY])
		return f->fi_fds[O_RDONLY];
	return f->fi_fds[O_RDWR];
}

static inline struct file *find_any_file(struct nfs4_file *f)
{
	if (f->fi_fds[O_RDWR])
		return f->fi_fds[O_RDWR];
	else if (f->fi_fds[O_WRONLY])
		return f->fi_fds[O_WRONLY];
	else
		return f->fi_fds[O_RDONLY];
}

/* "ol" stands for "Open or Lock".  Better suggestions welcome. */
struct nfs4_ol_stateid {
	struct nfs4_stid    st_stid; /* must be first field */
	struct list_head              st_perfile;
	struct list_head              st_perstateowner;
	struct list_head              st_lockowners;
	struct nfs4_stateowner      * st_stateowner;
	struct nfs4_file            * st_file;
	unsigned long                 st_access_bmap;
	unsigned long                 st_deny_bmap;
	struct nfs4_ol_stateid         * st_openstp;
};

static inline struct nfs4_ol_stateid *openlockstateid(struct nfs4_stid *s)
{
	return container_of(s, struct nfs4_ol_stateid, st_stid);
}

/* flags for preprocess_seqid_op() */
#define RD_STATE	        0x00000010
#define WR_STATE	        0x00000020

struct nfsd4_compound_state;
struct nfsd_net;

extern __be32 nfs4_preprocess_stateid_op(struct net *net,
		struct nfsd4_compound_state *cstate,
		stateid_t *stateid, int flags, struct file **filp);
extern void nfs4_lock_state(void);
extern void nfs4_unlock_state(void);
void nfs4_remove_reclaim_record(struct nfs4_client_reclaim *);
extern void nfs4_release_reclaim(void);
extern struct nfs4_client_reclaim *nfsd4_find_reclaim_client(const char *recdir);
extern __be32 nfs4_check_open_reclaim(clientid_t *clid, bool sessions);
extern void nfs4_free_openowner(struct nfs4_openowner *);
extern void nfs4_free_lockowner(struct nfs4_lockowner *);
extern int set_callback_cred(void);
extern void nfsd4_init_callback(struct nfsd4_callback *);
extern void nfsd4_probe_callback(struct nfs4_client *clp);
extern void nfsd4_probe_callback_sync(struct nfs4_client *clp);
extern void nfsd4_change_callback(struct nfs4_client *clp, struct nfs4_cb_conn *);
extern void nfsd4_cb_recall(struct nfs4_delegation *dp);
extern int nfsd4_create_callback_queue(void);
extern void nfsd4_destroy_callback_queue(void);
extern void nfsd4_shutdown_callback(struct nfs4_client *);
extern void nfs4_put_delegation(struct nfs4_delegation *dp);
extern __be32 nfs4_make_rec_clidname(char *clidname, struct xdr_netobj *clname);
extern struct nfs4_client_reclaim *nfs4_client_to_reclaim(const char *name);
extern bool nfs4_has_reclaimed_state(const char *name);
extern void release_session_client(struct nfsd4_session *);
extern void nfsd4_purge_closed_stateid(struct nfs4_stateowner *);

/* nfs4recover operations */
extern int nfsd4_client_tracking_init(struct net *net);
extern void nfsd4_client_tracking_exit(struct net *net);
extern void nfsd4_client_record_create(struct nfs4_client *clp);
extern void nfsd4_client_record_remove(struct nfs4_client *clp);
extern int nfsd4_client_record_check(struct nfs4_client *clp);
extern void nfsd4_record_grace_done(struct net *net, time_t boot_time);
#endif   /* NFSD4_STATE_H */
