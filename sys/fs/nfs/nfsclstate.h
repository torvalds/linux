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

#ifndef _NFS_NFSCLSTATE_H_
#define	_NFS_NFSCLSTATE_H_

/*
 * Definitions for NFS V4 client state handling.
 */
LIST_HEAD(nfsclopenhead, nfsclopen);
LIST_HEAD(nfscllockownerhead, nfscllockowner);
SLIST_HEAD(nfscllockownerfhhead, nfscllockownerfh);
LIST_HEAD(nfscllockhead, nfscllock);
LIST_HEAD(nfsclhead, nfsclclient);
LIST_HEAD(nfsclownerhead, nfsclowner);
TAILQ_HEAD(nfscldeleghead, nfscldeleg);
LIST_HEAD(nfscldeleghash, nfscldeleg);
TAILQ_HEAD(nfscllayouthead, nfscllayout);
LIST_HEAD(nfscllayouthash, nfscllayout);
LIST_HEAD(nfsclflayouthead, nfsclflayout);
LIST_HEAD(nfscldevinfohead, nfscldevinfo);
LIST_HEAD(nfsclrecalllayouthead, nfsclrecalllayout);
#define	NFSCLDELEGHASHSIZE	256
#define	NFSCLDELEGHASH(c, f, l)							\
	(&((c)->nfsc_deleghash[ncl_hash((f), (l)) % NFSCLDELEGHASHSIZE]))
#define	NFSCLLAYOUTHASHSIZE	256
#define	NFSCLLAYOUTHASH(c, f, l)						\
	(&((c)->nfsc_layouthash[ncl_hash((f), (l)) % NFSCLLAYOUTHASHSIZE]))

/* Structure for NFSv4.1 session stuff. */
struct nfsclsession {
	struct mtx	nfsess_mtx;
	struct nfsslot	nfsess_cbslots[NFSV4_CBSLOTS];
	nfsquad_t	nfsess_clientid;
	SVCXPRT		*nfsess_xprt;		/* For backchannel callback */
	uint32_t	nfsess_slotseq[64];	/* Max for 64bit nm_slots */
	uint64_t	nfsess_slots;
	uint32_t	nfsess_sequenceid;
	uint32_t	nfsess_maxcache;	/* Max size for cached reply. */
	uint16_t	nfsess_foreslots;
	uint16_t	nfsess_backslots;
	uint8_t		nfsess_sessionid[NFSX_V4SESSIONID];
	uint8_t		nfsess_defunct;		/* Non-zero for old sessions */
};

/*
 * This structure holds the session, clientid and related information
 * needed for an NFSv4.1 Meta Data Server (MDS) or Data Server (DS).
 * It is malloc'd to the correct length.
 */
struct nfsclds {
	TAILQ_ENTRY(nfsclds)	nfsclds_list;
	struct nfsclsession	nfsclds_sess;
	struct mtx		nfsclds_mtx;
	struct nfssockreq	*nfsclds_sockp;
	time_t			nfsclds_expire;
	uint16_t		nfsclds_flags;
	uint16_t		nfsclds_servownlen;
	uint8_t			nfsclds_verf[NFSX_VERF];
	uint8_t			nfsclds_serverown[0];
};

/*
 * Flags for nfsclds_flags.
 */
#define	NFSCLDS_HASWRITEVERF	0x0001
#define	NFSCLDS_MDS		0x0002
#define	NFSCLDS_DS		0x0004
#define	NFSCLDS_CLOSED		0x0008
#define	NFSCLDS_SAMECONN	0x0010

struct nfsclclient {
	LIST_ENTRY(nfsclclient) nfsc_list;
	struct nfsclownerhead	nfsc_owner;
	struct nfscldeleghead	nfsc_deleg;
	struct nfscldeleghash	nfsc_deleghash[NFSCLDELEGHASHSIZE];
	struct nfscllayouthead	nfsc_layout;
	struct nfscllayouthash	nfsc_layouthash[NFSCLLAYOUTHASHSIZE];
	struct nfscldevinfohead	nfsc_devinfo;
	struct nfsv4lock	nfsc_lock;
	struct proc		*nfsc_renewthread;
	struct nfsmount		*nfsc_nmp;
	time_t			nfsc_expire;
	u_int32_t		nfsc_clientidrev;
	u_int32_t		nfsc_rev;
	u_int32_t		nfsc_renew;
	u_int32_t		nfsc_cbident;
	u_int16_t		nfsc_flags;
	u_int16_t		nfsc_idlen;
	u_int8_t		nfsc_id[1];	/* Malloc'd to correct length */
};

/*
 * Bits for nfsc_flags.
 */
#define	NFSCLFLAGS_INITED	0x0001
#define	NFSCLFLAGS_HASCLIENTID	0x0002
#define	NFSCLFLAGS_RECOVER	0x0004
#define	NFSCLFLAGS_UMOUNT	0x0008
#define	NFSCLFLAGS_HASTHREAD	0x0010
#define	NFSCLFLAGS_AFINET6	0x0020
#define	NFSCLFLAGS_EXPIREIT	0x0040
#define	NFSCLFLAGS_FIRSTDELEG	0x0080
#define	NFSCLFLAGS_GOTDELEG	0x0100
#define	NFSCLFLAGS_RECVRINPROG	0x0200

struct nfsclowner {
	LIST_ENTRY(nfsclowner)	nfsow_list;
	struct nfsclopenhead	nfsow_open;
	struct nfsclclient	*nfsow_clp;
	u_int32_t		nfsow_seqid;
	u_int32_t		nfsow_defunct;
	struct nfsv4lock	nfsow_rwlock;
	u_int8_t		nfsow_owner[NFSV4CL_LOCKNAMELEN];
};

/*
 * MALLOC'd to the correct length to accommodate the file handle.
 */
struct nfscldeleg {
	TAILQ_ENTRY(nfscldeleg)	nfsdl_list;
	LIST_ENTRY(nfscldeleg)	nfsdl_hash;
	struct nfsclownerhead	nfsdl_owner;	/* locally issued state */
	struct nfscllockownerhead nfsdl_lock;
	nfsv4stateid_t		nfsdl_stateid;
	struct acl_entry	nfsdl_ace;	/* Delegation ace */
	struct nfsclclient	*nfsdl_clp;
	struct nfsv4lock	nfsdl_rwlock;	/* for active I/O ops */
	struct nfscred		nfsdl_cred;	/* Cred. used for Open */
	time_t			nfsdl_timestamp; /* used for stale cleanup */
	u_int64_t		nfsdl_sizelimit; /* Limit for file growth */
	u_int64_t		nfsdl_size;	/* saved copy of file size */
	u_int64_t		nfsdl_change;	/* and change attribute */
	struct timespec		nfsdl_modtime;	/* local modify time */
	u_int16_t		nfsdl_fhlen;
	u_int8_t		nfsdl_flags;
	u_int8_t		nfsdl_fh[1];	/* must be last */
};

/*
 * nfsdl_flags bits.
 */
#define	NFSCLDL_READ		0x01
#define	NFSCLDL_WRITE		0x02
#define	NFSCLDL_RECALL		0x04
#define	NFSCLDL_NEEDRECLAIM	0x08
#define	NFSCLDL_ZAPPED		0x10
#define	NFSCLDL_MODTIMESET	0x20
#define	NFSCLDL_DELEGRET	0x40

/*
 * MALLOC'd to the correct length to accommodate the file handle.
 */
struct nfsclopen {
	LIST_ENTRY(nfsclopen)	nfso_list;
	struct nfscllockownerhead nfso_lock;
	nfsv4stateid_t		nfso_stateid;
	struct nfsclowner	*nfso_own;
	struct nfscred		nfso_cred;	/* Cred. used for Open */
	u_int32_t		nfso_mode;
	u_int32_t		nfso_opencnt;
	u_int16_t		nfso_fhlen;
	u_int8_t		nfso_posixlock;	/* 1 for POSIX type locking */
	u_int8_t		nfso_fh[1];	/* must be last */
};

/*
 * Return values for nfscl_open(). NFSCLOPEN_OK must == 0.
 */
#define	NFSCLOPEN_OK		0
#define	NFSCLOPEN_DOOPEN	1
#define	NFSCLOPEN_DOOPENDOWNGRADE 2
#define	NFSCLOPEN_SETCRED	3

struct nfscllockowner {
	LIST_ENTRY(nfscllockowner) nfsl_list;
	struct nfscllockhead	nfsl_lock;
	struct nfsclopen	*nfsl_open;
	NFSPROC_T		*nfsl_inprog;
	nfsv4stateid_t		nfsl_stateid;
	int			nfsl_lockflags;
	u_int32_t		nfsl_seqid;
	struct nfsv4lock	nfsl_rwlock;
	u_int8_t		nfsl_owner[NFSV4CL_LOCKNAMELEN];
	u_int8_t		nfsl_openowner[NFSV4CL_LOCKNAMELEN];
};

/*
 * Byte range entry for the above lock owner.
 */
struct nfscllock {
	LIST_ENTRY(nfscllock)	nfslo_list;
	u_int64_t		nfslo_first;
	u_int64_t		nfslo_end;
	short			nfslo_type;
};

/* This structure is used to collect a list of lockowners to free up. */
struct nfscllockownerfh {
	SLIST_ENTRY(nfscllockownerfh)	nfslfh_list;
	struct nfscllockownerhead	nfslfh_lock;
	int				nfslfh_len;
	uint8_t				nfslfh_fh[NFSX_V4FHMAX];
};

/*
 * MALLOC'd to the correct length to accommodate the file handle.
 */
struct nfscllayout {
	TAILQ_ENTRY(nfscllayout)	nfsly_list;
	LIST_ENTRY(nfscllayout)		nfsly_hash;
	nfsv4stateid_t			nfsly_stateid;
	struct nfsv4lock		nfsly_lock;
	uint64_t			nfsly_filesid[2];
	uint64_t			nfsly_lastbyte;
	struct nfsclflayouthead		nfsly_flayread;
	struct nfsclflayouthead		nfsly_flayrw;
	struct nfsclrecalllayouthead	nfsly_recall;
	time_t				nfsly_timestamp;
	struct nfsclclient		*nfsly_clp;
	uint16_t			nfsly_flags;
	uint16_t			nfsly_fhlen;
	uint8_t				nfsly_fh[1];
};

/*
 * Flags for nfsly_flags.
 */
#define	NFSLY_FILES		0x0001
#define	NFSLY_BLOCK		0x0002
#define	NFSLY_OBJECT		0x0004
#define	NFSLY_RECALL		0x0008
#define	NFSLY_RECALLFILE	0x0010
#define	NFSLY_RECALLFSID	0x0020
#define	NFSLY_RECALLALL		0x0040
#define	NFSLY_RETONCLOSE	0x0080
#define	NFSLY_WRITTEN		0x0100	/* Has been used to write to a DS. */
#define	NFSLY_FLEXFILE		0x0200

/*
 * Flex file layout mirror specific stuff for nfsclflayout.
 */
struct nfsffm {
	nfsv4stateid_t		st;
	struct nfscldevinfo	*devp;
	char			dev[NFSX_V4DEVICEID];
	uint32_t		eff;
	uid_t			user;
	gid_t			group;
	struct nfsfh		*fh[NFSDEV_MAXVERS];
	uint16_t		fhcnt;
};

/*
 * MALLOC'd to the correct length to accommodate the file handle list for File
 * layout and the list of mirrors for the Flex File Layout.
 * These hang off of nfsly_flayread and nfsly_flayrw, sorted in increasing
 * offset order.
 * The nfsly_flayread list holds the ones with iomode == NFSLAYOUTIOMODE_READ,
 * whereas the nfsly_flayrw holds the ones with iomode == NFSLAYOUTIOMODE_RW.
 */
struct nfsclflayout {
	LIST_ENTRY(nfsclflayout)	nfsfl_list;
	uint64_t			nfsfl_off;
	uint64_t			nfsfl_end;
	uint32_t			nfsfl_iomode;
	uint16_t			nfsfl_flags;
	union {
		struct {
			uint64_t	patoff;
			uint32_t	util;
			uint32_t	stripe1;
			uint8_t		dev[NFSX_V4DEVICEID];
			uint16_t	fhcnt;
			struct nfscldevinfo *devp;
		} fl;
		struct {
			uint64_t	stripeunit;
			uint32_t	fflags;
			uint32_t	statshint;
			uint16_t	mirrorcnt;
		} ff;
	} nfsfl_un;
	union {
		struct nfsfh		*fh[0];	/* FH list for DS File layout */
		struct nfsffm		ffm[0];	/* Mirror list for Flex File */
	} nfsfl_un2;	/* Must be last. Malloc'd to correct array length */
};
#define	nfsfl_patoff		nfsfl_un.fl.patoff
#define	nfsfl_util		nfsfl_un.fl.util
#define	nfsfl_stripe1		nfsfl_un.fl.stripe1
#define	nfsfl_dev		nfsfl_un.fl.dev
#define	nfsfl_fhcnt		nfsfl_un.fl.fhcnt
#define	nfsfl_devp		nfsfl_un.fl.devp
#define	nfsfl_stripeunit	nfsfl_un.ff.stripeunit
#define	nfsfl_fflags		nfsfl_un.ff.fflags
#define	nfsfl_statshint		nfsfl_un.ff.statshint
#define	nfsfl_mirrorcnt		nfsfl_un.ff.mirrorcnt
#define	nfsfl_fh		nfsfl_un2.fh
#define	nfsfl_ffm		nfsfl_un2.ffm

/*
 * Flags for nfsfl_flags.
 */
#define	NFSFL_RECALL	0x0001		/* File layout has been recalled */
#define	NFSFL_FILE	0x0002		/* File layout */
#define	NFSFL_FLEXFILE	0x0004		/* Flex File layout */

/*
 * Structure that is used to store a LAYOUTRECALL.
 */
struct nfsclrecalllayout {
	LIST_ENTRY(nfsclrecalllayout)	nfsrecly_list;
	uint64_t			nfsrecly_off;
	uint64_t			nfsrecly_len;
	int				nfsrecly_recalltype;
	uint32_t			nfsrecly_iomode;
	uint32_t			nfsrecly_stateseqid;
	uint32_t			nfsrecly_stat;
	uint32_t			nfsrecly_op;
	char				nfsrecly_devid[NFSX_V4DEVICEID];
};

/*
 * Stores the NFSv4.1 Device Info. Malloc'd to the correct length to
 * store the list of network connections and list of indices.
 * nfsdi_data[] is allocated the following way:
 * - nfsdi_addrcnt * struct nfsclds
 * - stripe indices, each stored as one byte, since there can be many
 *   of them. (This implies a limit of 256 on nfsdi_addrcnt, since the
 *   indices select which address.)
 * For Flex File, the addrcnt is always one and no stripe indices exist.
 */
struct nfscldevinfo {
	LIST_ENTRY(nfscldevinfo)	nfsdi_list;
	uint8_t				nfsdi_deviceid[NFSX_V4DEVICEID];
	struct nfsclclient		*nfsdi_clp;
	uint32_t			nfsdi_refcnt;
	uint32_t			nfsdi_layoutrefs;
	union {
		struct {
			uint16_t	stripecnt;
		} fl;
		struct {
			int		versindex;
			uint32_t	vers;
			uint32_t	minorvers;
			uint32_t	rsize;
			uint32_t	wsize;
		} ff;
	} nfsdi_un;
	uint16_t			nfsdi_addrcnt;
	uint16_t			nfsdi_flags;
	struct nfsclds			*nfsdi_data[0];
};
#define	nfsdi_stripecnt	nfsdi_un.fl.stripecnt
#define	nfsdi_versindex	nfsdi_un.ff.versindex
#define	nfsdi_vers	nfsdi_un.ff.vers
#define	nfsdi_minorvers	nfsdi_un.ff.minorvers
#define	nfsdi_rsize	nfsdi_un.ff.rsize
#define	nfsdi_wsize	nfsdi_un.ff.wsize

/* Flags for nfsdi_flags. */
#define	NFSDI_FILELAYOUT	0x0001
#define	NFSDI_FLEXFILE		0x0002
#define	NFSDI_TIGHTCOUPLED	0X0004

/* These inline functions return values from nfsdi_data[]. */
/*
 * Return a pointer to the address at "pos".
 */
static __inline struct nfsclds **
nfsfldi_addr(struct nfscldevinfo *ndi, int pos)
{

	if (pos >= ndi->nfsdi_addrcnt)
		return (NULL);
	return (&ndi->nfsdi_data[pos]);
}

/*
 * Return the Nth ("pos") stripe index.
 */
static __inline int
nfsfldi_stripeindex(struct nfscldevinfo *ndi, int pos)
{
	uint8_t *valp;

	if (pos >= ndi->nfsdi_stripecnt)
		return (-1);
	valp = (uint8_t *)&ndi->nfsdi_data[ndi->nfsdi_addrcnt];
	valp += pos;
	return ((int)*valp);
}

/*
 * Set the Nth ("pos") stripe index to "val".
 */
static __inline void
nfsfldi_setstripeindex(struct nfscldevinfo *ndi, int pos, uint8_t val)
{
	uint8_t *valp;

	if (pos >= ndi->nfsdi_stripecnt)
		return;
	valp = (uint8_t *)&ndi->nfsdi_data[ndi->nfsdi_addrcnt];
	valp += pos;
	*valp = val;
}

/*
 * Macro for incrementing the seqid#.
 */
#define	NFSCL_INCRSEQID(s, n)	do { 					\
	    if (((n)->nd_flag & ND_INCRSEQID))				\
		(s)++; 							\
	} while (0)

#endif	/* _NFS_NFSCLSTATE_H_ */
