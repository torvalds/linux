/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Rick Macklem, University of Guelph
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NFS_NFSRVSTATE_H_
#define	_NFS_NFSRVSTATE_H_

#if defined(_KERNEL) || defined(KERNEL)
/*
 * Definitions for NFS V4 server state handling.
 */

/*
 * List heads for nfsclient, nfsstate and nfslockfile.
 * (Some systems seem to like to dynamically size these things, but I
 *  don't see any point in doing so for these ones.)
 */
LIST_HEAD(nfsclienthashhead, nfsclient);
LIST_HEAD(nfsstatehead, nfsstate);
LIST_HEAD(nfslockhead, nfslock);
LIST_HEAD(nfslockhashhead, nfslockfile);
LIST_HEAD(nfssessionhead, nfsdsession);
LIST_HEAD(nfssessionhashhead, nfsdsession);
TAILQ_HEAD(nfslayouthead, nfslayout);
SLIST_HEAD(nfsdsdirhead, nfsdsdir);
TAILQ_HEAD(nfsdevicehead, nfsdevice);
LIST_HEAD(nfsdontlisthead, nfsdontlist);

/*
 * List head for nfsusrgrp.
 */
TAILQ_HEAD(nfsuserhashhead, nfsusrgrp);

#define	NFSCLIENTHASH(id)						\
	(&nfsclienthash[(id).lval[1] % nfsrv_clienthashsize])
#define	NFSSTATEHASH(clp, id)						\
	(&((clp)->lc_stateid[(id).other[2] % nfsrv_statehashsize]))
#define	NFSUSERHASH(id)							\
	(&nfsuserhash[(id) % nfsrv_lughashsize])
#define	NFSUSERNAMEHASH(p, l)						\
	(&nfsusernamehash[((l)>=4?(*(p)+*((p)+1)+*((p)+2)+*((p)+3)):*(p)) \
		% nfsrv_lughashsize])
#define	NFSGROUPHASH(id)						\
	(&nfsgrouphash[(id) % nfsrv_lughashsize])
#define	NFSGROUPNAMEHASH(p, l)						\
	(&nfsgroupnamehash[((l)>=4?(*(p)+*((p)+1)+*((p)+2)+*((p)+3)):*(p)) \
		% nfsrv_lughashsize])

struct nfssessionhash {
	struct mtx			mtx;
	struct nfssessionhashhead	list;
};
#define	NFSSESSIONHASH(f) 						\
	(&nfssessionhash[nfsrv_hashsessionid(f) % nfsrv_sessionhashsize])

struct nfslayouthash {
	struct mtx		mtx;
	struct nfslayouthead	list;
};
#define	NFSLAYOUTHASH(f) 						\
	(&nfslayouthash[nfsrv_hashfh(f) % nfsrv_layouthashsize])

/*
 * Client server structure for V4. It is doubly linked into two lists.
 * The first is a hash table based on the clientid and the second is a
 * list of all clients maintained in LRU order.
 * The actual size malloc'd is large enough to accommodate the id string.
 */
struct nfsclient {
	LIST_ENTRY(nfsclient) lc_hash;		/* Clientid hash list */
	struct nfsstatehead *lc_stateid;	/* Stateid hash */
	struct nfsstatehead lc_open;		/* Open owner list */
	struct nfsstatehead lc_deleg;		/* Delegations */
	struct nfsstatehead lc_olddeleg;	/* and old delegations */
	struct nfssessionhead lc_session;	/* List of NFSv4.1 sessions */
	time_t		lc_expiry;		/* Expiry time (sec) */
	time_t		lc_delegtime;		/* Old deleg expiry (sec) */
	nfsquad_t	lc_clientid;		/* 64 bit clientid */
	nfsquad_t	lc_confirm;		/* 64 bit confirm value */
	u_int32_t	lc_program;		/* RPC Program # */
	u_int32_t	lc_callback;		/* Callback id */
	u_int32_t	lc_stateindex;		/* Current state index# */
	u_int32_t	lc_statemaxindex;	/* Max state index# */
	u_int32_t	lc_cbref;		/* Cnt of callbacks */
	uid_t		lc_uid;			/* User credential */
	gid_t		lc_gid;
	u_int16_t	lc_idlen;		/* Client ID and len */
	u_int16_t	lc_namelen;		/* plus GSS principal and len */
	u_char		*lc_name;
	struct nfssockreq lc_req;		/* Callback info */
	u_int32_t	lc_flags;		/* LCL_ flag bits */
	u_char		lc_verf[NFSX_VERF];	 /* client verifier */
	u_char		lc_id[1];		/* Malloc'd correct size */
};

#define	CLOPS_CONFIRM		0x0001
#define	CLOPS_RENEW		0x0002
#define	CLOPS_RENEWOP		0x0004

/*
 * Structure for NFSv4.1 Layouts.
 * Malloc'd to correct size for the lay_xdr.
 */
struct nfslayout {
	TAILQ_ENTRY(nfslayout)	lay_list;
	nfsv4stateid_t		lay_stateid;
	nfsquad_t		lay_clientid;
	fhandle_t		lay_fh;
	fsid_t			lay_fsid;
	uint32_t		lay_layoutlen;
	uint16_t		lay_mirrorcnt;
	uint16_t		lay_trycnt;
	uint16_t		lay_type;
	uint16_t		lay_flags;
	uint32_t		lay_xdr[0];
};

/* Flags for lay_flags. */
#define	NFSLAY_READ	0x0001
#define	NFSLAY_RW	0x0002
#define	NFSLAY_RECALL	0x0004
#define	NFSLAY_RETURNED	0x0008
#define	NFSLAY_CALLB	0x0010

/*
 * Structure for an NFSv4.1 session.
 * Locking rules for this structure.
 * To add/delete one of these structures from the lists, you must lock
 * both: NFSLOCKSTATE() and NFSLOCKSESSION(session hashhead) in that order.
 * To traverse the lists looking for one of these, you must hold one
 * of these two locks.
 * The exception is if the thread holds the exclusive root sleep lock.
 * In this case, all other nfsd threads are blocked, so locking the
 * mutexes isn't required.
 * When manipulating sess_refcnt, NFSLOCKSTATE() must be locked.
 * When manipulating the fields withinsess_cbsess except nfsess_xprt,
 * sess_cbsess.nfsess_mtx must be locked.
 * When manipulating sess_slots and sess_cbsess.nfsess_xprt,
 * NFSLOCKSESSION(session hashhead) must be locked.
 */
struct nfsdsession {
	uint64_t		sess_refcnt;	/* Reference count. */
	LIST_ENTRY(nfsdsession)	sess_hash;	/* Hash list of sessions. */
	LIST_ENTRY(nfsdsession)	sess_list;	/* List of client sessions. */
	struct nfsslot		sess_slots[NFSV4_SLOTS];
	struct nfsclient	*sess_clp;	/* Associated clientid. */
	uint32_t		sess_crflags;
	uint32_t		sess_cbprogram;
	uint32_t		sess_maxreq;
	uint32_t		sess_maxresp;
	uint32_t		sess_maxrespcached;
	uint32_t		sess_maxops;
	uint32_t		sess_maxslots;
	uint32_t		sess_cbmaxreq;
	uint32_t		sess_cbmaxresp;
	uint32_t		sess_cbmaxrespcached;
	uint32_t		sess_cbmaxops;
	uint8_t			sess_sessionid[NFSX_V4SESSIONID];
	struct nfsclsession	sess_cbsess;	/* Callback session. */
};

/*
 * Nfs state structure. I couldn't resist overloading this one, since
 * it makes cleanup, etc. simpler. These structures are used in four ways:
 * - open_owner structures chained off of nfsclient
 * - open file structures chained off an open_owner structure
 * - lock_owner structures chained off an open file structure
 * - delegated file structures chained off of nfsclient and nfslockfile
 * - the ls_list field is used for the chain it is in
 * - the ls_head structure is used to chain off the sibling structure
 *   (it is a union between an nfsstate and nfslock structure head)
 *    If it is a lockowner stateid, nfslock structures hang off it.
 * For the open file and lockowner cases, it is in the hash table in
 * nfsclient for stateid.
 */
struct nfsstate {
	LIST_ENTRY(nfsstate)	ls_hash;	/* Hash list entry */
	LIST_ENTRY(nfsstate)	ls_list;	/* List of opens/delegs */
	LIST_ENTRY(nfsstate)	ls_file;	/* Opens/Delegs for a file */
	union {
		struct nfsstatehead	open; /* Opens list */
		struct nfslockhead	lock; /* Locks list */
	} ls_head;
	nfsv4stateid_t		ls_stateid;	/* The state id */
	u_int32_t		ls_seq;		/* seq id */
	uid_t			ls_uid;		/* uid of locker */
	u_int32_t		ls_flags;	/* Type of lock, etc. */
	union {
		struct nfsstate	*openowner;	/* Open only */
		u_int32_t	opentolockseq;	/* Lock call only */
		u_int32_t	noopens;	/* Openowner only */
		struct {
			u_quad_t	filerev; /* Delegations only */
			time_t		expiry;
			time_t		limit;
			u_int64_t	compref;
		} deleg;
	} ls_un;
	struct nfslockfile	*ls_lfp;	/* Back pointer */
	struct nfsrvcache	*ls_op;		/* Op cache reference */
	struct nfsclient	*ls_clp;	/* Back pointer */
	u_short			ls_ownerlen;	/* Length of ls_owner */
	u_char			ls_owner[1];	/* malloc'd the correct size */
};
#define	ls_lock			ls_head.lock
#define	ls_open			ls_head.open
#define	ls_opentolockseq	ls_un.opentolockseq
#define	ls_openowner		ls_un.openowner
#define	ls_openstp		ls_un.openowner
#define	ls_noopens		ls_un.noopens
#define	ls_filerev		ls_un.deleg.filerev
#define	ls_delegtime		ls_un.deleg.expiry
#define	ls_delegtimelimit	ls_un.deleg.limit
#define	ls_compref		ls_un.deleg.compref

/*
 * Nfs lock structure.
 * This structure is chained off of the nfsstate (the lockowner) and
 * nfslockfile (the file) structures, for the file and owner it
 * refers to. It holds flags and a byte range.
 * It also has back pointers to the associated lock_owner and lockfile.
 */
struct nfslock {
	LIST_ENTRY(nfslock)	lo_lckowner;
	LIST_ENTRY(nfslock)	lo_lckfile;
	struct nfsstate		*lo_stp;
	struct nfslockfile	*lo_lfp;
	u_int64_t		lo_first;
	u_int64_t		lo_end;
	u_int32_t		lo_flags;
};

/*
 * Structure used to return a conflicting lock. (Must be large
 * enough for the largest lock owner we can have.)
 */
struct nfslockconflict {
	nfsquad_t		cl_clientid;
	u_int64_t		cl_first;
	u_int64_t		cl_end;
	u_int32_t		cl_flags;
	u_short			cl_ownerlen;
	u_char			cl_owner[NFSV4_OPAQUELIMIT];
};

/*
 * This structure is used to keep track of local locks that might need
 * to be rolled back.
 */
struct nfsrollback {
	LIST_ENTRY(nfsrollback)	rlck_list;
	uint64_t		rlck_first;
	uint64_t		rlck_end;
	int			rlck_type;
};

/*
 * This structure refers to a file for which lock(s) and/or open(s) exist.
 * Searched via hash table on file handle or found via the back pointer from an
 * open or lock owner.
 */
struct nfslockfile {
	LIST_HEAD(, nfsstate)	lf_open;	/* Open list */
	LIST_HEAD(, nfsstate)	lf_deleg;	/* Delegation list */
	LIST_HEAD(, nfslock)	lf_lock;	/* Lock list */
	LIST_HEAD(, nfslock)	lf_locallock;	/* Local lock list */
	LIST_HEAD(, nfsrollback) lf_rollback;	/* Local lock rollback list */
	LIST_ENTRY(nfslockfile)	lf_hash;	/* Hash list entry */
	fhandle_t		lf_fh;		/* The file handle */
	struct nfsv4lock	lf_locallock_lck; /* serialize local locking */
	int			lf_usecount;	/* Ref count for locking */
};

/*
 * This structure is malloc'd an chained off hash lists for user/group
 * names.
 */
struct nfsusrgrp {
	TAILQ_ENTRY(nfsusrgrp)	lug_numhash;	/* Hash by id# */
	TAILQ_ENTRY(nfsusrgrp)	lug_namehash;	/* and by name */
	time_t			lug_expiry;	/* Expiry time in sec */
	union {
		uid_t		un_uid;		/* id# */
		gid_t		un_gid;
	} lug_un;
	struct ucred		*lug_cred;	/* Cred. with groups list */
	int			lug_namelen;	/* Name length */
	u_char			lug_name[1];	/* malloc'd correct length */
};
#define	lug_uid		lug_un.un_uid
#define	lug_gid		lug_un.un_gid

/*
 * These structures are used for the stable storage restart stuff.
 */
/*
 * Record at beginning of file.
 */
struct nfsf_rec {
	u_int32_t	lease;			/* Lease duration */
	u_int32_t	numboots;		/* Number of boottimes */
};

void nfsrv_cleanclient(struct nfsclient *, NFSPROC_T *);
void nfsrv_freedeleglist(struct nfsstatehead *);

/*
 * This structure is used to create the list of device info entries for
 * a GetDeviceInfo operation and stores the DS server info.
 * The nfsdev_addrandhost field has the fully qualified host domain name
 * followed by the network address in XDR.
 * It is allocated with nfsrv_dsdirsize nfsdev_dsdir[] entries.
 */
struct nfsdevice {
	TAILQ_ENTRY(nfsdevice)	nfsdev_list;
	vnode_t			nfsdev_dvp;
	struct nfsmount		*nfsdev_nmp;
	char			nfsdev_deviceid[NFSX_V4DEVICEID];
	uint16_t		nfsdev_hostnamelen;
	uint16_t		nfsdev_fileaddrlen;
	uint16_t		nfsdev_flexaddrlen;
	uint16_t		nfsdev_mdsisset;
	char			*nfsdev_fileaddr;
	char			*nfsdev_flexaddr;
	char			*nfsdev_host;
	fsid_t			nfsdev_mdsfsid;
	uint32_t		nfsdev_nextdir;
	vnode_t			nfsdev_dsdir[0];
};

/*
 * This structure holds the va_size, va_filerev, va_atime and va_mtime for the
 * DS file and is stored in the metadata file's extended attribute pnfsd.dsattr.
 */
struct pnfsdsattr {
	uint64_t	dsa_filerev;
	uint64_t	dsa_size;
	struct timespec	dsa_atime;
	struct timespec	dsa_mtime;
};

/*
 * This structure is a list element for a list the pNFS server uses to
 * mark that the recovery of a mirror file is in progress.
 */
struct nfsdontlist {
	LIST_ENTRY(nfsdontlist)	nfsmr_list;
	uint32_t		nfsmr_flags;
	fhandle_t		nfsmr_fh;
};

/* nfsmr_flags bits. */
#define	NFSMR_DONTLAYOUT	0x00000001

#endif	/* defined(_KERNEL) || defined(KERNEL) */

/*
 * This structure holds the information about the DS file and is stored
 * in the metadata file's extended attribute called pnfsd.dsfile.
 */
#define	PNFS_FILENAME_LEN	(2 * sizeof(fhandle_t))
struct pnfsdsfile {
	fhandle_t	dsf_fh;
	uint32_t	dsf_dir;
	union {
		struct sockaddr_in	sin;
		struct sockaddr_in6	sin6;
	} dsf_nam;
	char		dsf_filename[PNFS_FILENAME_LEN + 1];
};
#define	dsf_sin		dsf_nam.sin
#define	dsf_sin6	dsf_nam.sin6

#endif	/* _NFS_NFSRVSTATE_H_ */
