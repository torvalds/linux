/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef _NFS_NFS_H_
#define	_NFS_NFS_H_
/*
 * Tunable constants for nfs
 */

#define	NFS_MAXIOVEC	34
#define	NFS_TICKINTVL	500		/* Desired time for a tick (msec) */
#define	NFS_HZ		(hz / nfscl_ticks) /* Ticks/sec */
#define	NFS_TIMEO	(1 * NFS_HZ)	/* Default timeout = 1 second */
#define	NFS_MINTIMEO	(1 * NFS_HZ)	/* Min timeout to use */
#define	NFS_MAXTIMEO	(60 * NFS_HZ)	/* Max timeout to backoff to */
#define	NFS_TCPTIMEO	300		/* TCP timeout */
#define	NFS_MAXRCVTIMEO	60		/* 1 minute in seconds */
#define	NFS_MINIDEMTIMEO (5 * NFS_HZ)	/* Min timeout for non-idempotent ops*/
#define	NFS_MAXREXMIT	100		/* Stop counting after this many */
#define	NFSV4_CALLBACKTIMEO (2 * NFS_HZ) /* Timeout in ticks */
#define	NFSV4_CALLBACKRETRY 5		/* Number of retries before failure */
#define	NFSV4_SLOTS	64		/* Number of slots, fore channel */
#define	NFSV4_CBSLOTS	8		/* Number of slots, back channel */
#define	NFSV4_CBRETRYCNT 4		/* # of CBRecall retries upon err */
#define	NFSV4_UPCALLTIMEO (15 * NFS_HZ)	/* Timeout in ticks for upcalls */
					/* to gssd or nfsuserd */
#define	NFSV4_UPCALLRETRY 4		/* Number of retries before failure */
#define	NFS_MAXWINDOW	1024		/* Max number of outstanding requests */
#define	NFS_RETRANS	10		/* Num of retrans for soft mounts */
#define	NFS_RETRANS_TCP	2		/* Num of retrans for TCP soft mounts */
#define	NFS_MAXGRPS	16		/* Max. size of groups list */
#define	NFS_TRYLATERDEL	15		/* Maximum delay timeout (sec) */
#ifndef NFS_REMOVETIMEO
#define	NFS_REMOVETIMEO 15  /* # sec to wait for delegret in local syscall */
#endif
#ifndef NFS_MINATTRTIMO
#define	NFS_MINATTRTIMO 5		/* Attribute cache timeout in sec */
#endif
#ifndef NFS_MAXATTRTIMO
#define	NFS_MAXATTRTIMO 60
#endif
#define	NFS_WSIZE	8192		/* Def. write data size <= 8192 */
#define	NFS_RSIZE	8192		/* Def. read data size <= 8192 */
#define	NFS_READDIRSIZE	8192		/* Def. readdir size */
#define	NFS_DEFRAHEAD	1		/* Def. read ahead # blocks */
#define	NFS_MAXRAHEAD	16		/* Max. read ahead # blocks */
#define	NFS_MAXASYNCDAEMON 	64	/* Max. number async_daemons runnable */
#define	NFS_MAXUIDHASH	64		/* Max. # of hashed uid entries/mp */
#ifndef	NFSRV_LEASE
#define	NFSRV_LEASE		120	/* Lease time in seconds for V4 */
#endif					/* assigned to nfsrv_lease */
#ifndef NFSRV_STALELEASE
#define	NFSRV_STALELEASE	(5 * nfsrv_lease)
#endif
#ifndef NFSRV_MOULDYLEASE
#define	NFSRV_MOULDYLEASE	604800	/* One week (in sec) */
#endif
#ifndef NFSCLIENTHASHSIZE
#define	NFSCLIENTHASHSIZE	20	/* Size of server client hash table */
#endif
#ifndef NFSLOCKHASHSIZE
#define	NFSLOCKHASHSIZE		20	/* Size of server nfslock hash table */
#endif
#ifndef NFSSESSIONHASHSIZE
#define	NFSSESSIONHASHSIZE	20	/* Size of server session hash table */
#endif
#define	NFSSTATEHASHSIZE	10	/* Size of server stateid hash table */
#define	NFSLAYOUTHIGHWATER	1000000	/* Upper limit for # of layouts */
#ifndef	NFSCLDELEGHIGHWATER
#define	NFSCLDELEGHIGHWATER	10000	/* limit for client delegations */
#endif
#ifndef	NFSCLLAYOUTHIGHWATER
#define	NFSCLLAYOUTHIGHWATER	10000	/* limit for client pNFS layouts */
#endif
#ifndef NFSNOOPEN			/* Inactive open owner (sec) */
#define	NFSNOOPEN		120
#endif
#define	NFSRV_LEASEDELTA	15	/* # of seconds to delay beyond lease */
#define	NFS_IDMAXSIZE		4	/* max sizeof (in_addr_t) */
#ifndef NFSRVCACHE_UDPTIMEOUT
#define	NFSRVCACHE_UDPTIMEOUT	30	/* # of sec to hold cached rpcs(udp) */
#endif
#ifndef NFSRVCACHE_UDPHIGHWATER
#define	NFSRVCACHE_UDPHIGHWATER	500	/* Max # of udp cache entries */
#endif
#ifndef NFSRVCACHE_TCPTIMEOUT
#define	NFSRVCACHE_TCPTIMEOUT	(3600*12) /*#of sec to hold cached rpcs(tcp) */
#endif
#ifndef	NFSRVCACHE_FLOODLEVEL
#define	NFSRVCACHE_FLOODLEVEL	16384	/* Very high water mark for cache */
#endif
#ifndef	NFSRV_CLIENTHIGHWATER
#define	NFSRV_CLIENTHIGHWATER	1000
#endif
#ifndef	NFSRV_MAXDUMPLIST
#define	NFSRV_MAXDUMPLIST	10000
#endif
#ifndef NFS_ACCESSCACHESIZE
#define	NFS_ACCESSCACHESIZE	8
#endif
#define	NFSV4_CBPORT	7745		/* Callback port for testing */

/*
 * This macro defines the high water mark for issuing V4 delegations.
 * (It is currently set at a conservative 20% of nfsrv_v4statelimit. This
 *  may want to increase when clients can make more effective use of
 *  delegations.)
 */
#define	NFSRV_V4DELEGLIMIT(c) (((c) * 5) > nfsrv_v4statelimit)

#define	NFS_READDIRBLKSIZ	DIRBLKSIZ	/* Minimal nm_readdirsize */

/*
 * Oddballs
 */
#define	NFS_CMPFH(n, f, s) 						\
    ((n)->n_fhp->nfh_len == (s) && !NFSBCMP((n)->n_fhp->nfh_fh, (caddr_t)(f), (s)))
#define	NFSRV_CMPFH(nf, ns, f, s) 					\
	((ns) == (s) && !NFSBCMP((caddr_t)(nf), (caddr_t)(f), (s)))
#define	NFS_CMPTIME(t1, t2) 						\
	((t1).tv_sec == (t2).tv_sec && (t1).tv_nsec == (t2).tv_nsec)
#define	NFS_SETTIME(t) do { 						\
	(t).tv_sec = time.tv_sec; (t).tv_nsec = 1000 * time.tv_usec; } while (0)
#define	NFS_SRVMAXDATA(n) 						\
		(((n)->nd_flag & (ND_NFSV3 | ND_NFSV4)) ? 		\
		 NFS_SRVMAXIO : NFS_V2MAXDATA)
#define	NFS64BITSSET	0xffffffffffffffffull
#define	NFS64BITSMINUS1	0xfffffffffffffffeull

/*
 * Structures for the nfssvc(2) syscall. Not that anyone but nfsd, mount_nfs
 * and nfsloaduser should ever try and use it.
 */
struct nfsd_addsock_args {
	int	sock;		/* Socket to serve */
	caddr_t	name;		/* Client addr for connection based sockets */
	int	namelen;	/* Length of name */
};

/*
 * nfsd argument for new krpc.
 * (New version supports pNFS, indicated by NFSSVC_NEWSTRUCT flag.)
 */
struct nfsd_nfsd_args {
	const char *principal;	/* GSS-API service principal name */
	int	minthreads;	/* minimum service thread count */
	int	maxthreads;	/* maximum service thread count */
	int	version;	/* Allow multiple variants */
	char	*addr;		/* pNFS DS addresses */
	int	addrlen;	/* Length of addrs */
	char	*dnshost;	/* DNS names for DS addresses */
	int	dnshostlen;	/* Length of DNS names */
	char	*dspath;	/* DS Mount path on MDS */
	int	dspathlen;	/* Length of DS Mount path on MDS */
	char	*mdspath;	/* MDS mount for DS path on MDS */
	int	mdspathlen;	/* Length of MDS mount for DS path on MDS */
	int	mirrorcnt;	/* Number of mirrors to create on DSs */
};

/*
 * NFSDEV_MAXMIRRORS - Maximum level of mirroring for a DS.
 * (Most will only put files on two DSs, but this setting allows up to 4.)
 * NFSDEV_MAXVERS - maximum number of NFS versions supported by Flex File.
 */
#define	NFSDEV_MAXMIRRORS	4
#define	NFSDEV_MAXVERS		4

struct nfsd_pnfsd_args {
	int	op;		/* Which pNFSd op to perform. */
	char	*mdspath;	/* Path of MDS file. */
	char	*dspath;	/* Path of recovered DS mounted on dir. */
	char	*curdspath;	/* Path of current DS mounted on dir. */
};

#define	PNFSDOP_DELDSSERVER	1
#define	PNFSDOP_COPYMR		2
#define	PNFSDOP_FORCEDELDS	3

/* Old version. */
struct nfsd_nfsd_oargs {
	const char *principal;	/* GSS-API service principal name */
	int	minthreads;	/* minimum service thread count */
	int	maxthreads;	/* maximum service thread count */
};

/*
 * Arguments for use by the callback daemon.
 */
struct nfsd_nfscbd_args {
	const char *principal;	/* GSS-API service principal name */
};

struct nfscbd_args {
	int	sock;		/* Socket to serve */
	caddr_t	name;		/* Client addr for connection based sockets */
	int	namelen;	/* Length of name */
	u_short	port;		/* Port# for callbacks */
};

struct nfsd_idargs {
	int		nid_flag;	/* Flags (see below) */
	uid_t		nid_uid;	/* user/group id */
	gid_t		nid_gid;
	int		nid_usermax;	/* Upper bound on user name cache */
	int		nid_usertimeout;/* User name timeout (minutes) */
	u_char		*nid_name;	/* Name */
	int		nid_namelen;	/* and its length */
	gid_t		*nid_grps;	/* and the list */
	int		nid_ngroup;	/* Size of groups list */
};

struct nfsd_oidargs {
	int		nid_flag;	/* Flags (see below) */
	uid_t		nid_uid;	/* user/group id */
	gid_t		nid_gid;
	int		nid_usermax;	/* Upper bound on user name cache */
	int		nid_usertimeout;/* User name timeout (minutes) */
	u_char		*nid_name;	/* Name */
	int		nid_namelen;	/* and its length */
};

struct nfsd_clid {
	int		nclid_idlen;	/* Length of client id */
	u_char		nclid_id[NFSV4_OPAQUELIMIT]; /* and name */
};

struct nfsd_dumplist {
	int		ndl_size;	/* Number of elements */
	void		*ndl_list;	/* and the list of elements */
};

struct nfsd_dumpclients {
	u_int32_t	ndcl_flags;		/* LCL_xxx flags */
	u_int32_t	ndcl_nopenowners;	/* Number of openowners */
	u_int32_t	ndcl_nopens;		/* and opens */
	u_int32_t	ndcl_nlockowners;	/* and of lockowners */
	u_int32_t	ndcl_nlocks;		/* and of locks */
	u_int32_t	ndcl_ndelegs;		/* and of delegations */
	u_int32_t	ndcl_nolddelegs;	/* and old delegations */
	sa_family_t	ndcl_addrfam;		/* Callback address */
	union {
		struct in_addr sin_addr;
		struct in6_addr sin6_addr;
	} ndcl_cbaddr;
	struct nfsd_clid ndcl_clid;	/* and client id */
};

struct nfsd_dumplocklist {
	char		*ndllck_fname;	/* File Name */
	int		ndllck_size;	/* Number of elements */
	void		*ndllck_list;	/* and the list of elements */
};

struct nfsd_dumplocks {
	u_int32_t	ndlck_flags;		/* state flags NFSLCK_xxx */
	nfsv4stateid_t	ndlck_stateid;		/* stateid */
	u_int64_t	ndlck_first;		/* lock byte range */
	u_int64_t	ndlck_end;
	struct nfsd_clid ndlck_owner;		/* Owner of open/lock */
	sa_family_t	ndlck_addrfam;		/* Callback address */
	union {
		struct in_addr sin_addr;
		struct in6_addr sin6_addr;
	} ndlck_cbaddr;
	struct nfsd_clid ndlck_clid;	/* and client id */
};

/*
 * Structure for referral information.
 */
struct nfsreferral {
	u_char		*nfr_srvlist;	/* List of servers */
	int		nfr_srvcnt;	/* number of servers */
	vnode_t		nfr_vp;	/* vnode for referral */
	uint64_t	nfr_dfileno;	/* assigned dir inode# */
};

/*
 * Flags for lc_flags and opsflags for nfsrv_getclient().
 */
#define	LCL_NEEDSCONFIRM	0x00000001
#define	LCL_DONTCLEAN		0x00000002
#define	LCL_WAKEUPWANTED	0x00000004
#define	LCL_TCPCALLBACK		0x00000008
#define	LCL_CALLBACKSON		0x00000010
#define	LCL_INDEXNOTOK		0x00000020
#define	LCL_STAMPEDSTABLE	0x00000040
#define	LCL_EXPIREIT		0x00000080
#define	LCL_CBDOWN		0x00000100
#define	LCL_KERBV		0x00000400
#define	LCL_NAME		0x00000800
#define	LCL_NEEDSCBNULL		0x00001000
#define	LCL_GSSINTEGRITY	0x00002000
#define	LCL_GSSPRIVACY		0x00004000
#define	LCL_ADMINREVOKED	0x00008000
#define	LCL_RECLAIMCOMPLETE	0x00010000
#define	LCL_NFSV41		0x00020000
#define	LCL_DONEBINDCONN	0x00040000
#define	LCL_RECLAIMONEFS	0x00080000

#define	LCL_GSS		LCL_KERBV	/* Or of all mechs */

/*
 * Bits for flags in nfslock and nfsstate.
 * The access, deny, NFSLCK_READ and NFSLCK_WRITE bits must be defined as
 * below, in the correct order, so the shifts work for tests.
 */
#define	NFSLCK_READACCESS	0x00000001
#define	NFSLCK_WRITEACCESS	0x00000002
#define	NFSLCK_ACCESSBITS	(NFSLCK_READACCESS | NFSLCK_WRITEACCESS)
#define	NFSLCK_SHIFT		2
#define	NFSLCK_READDENY		0x00000004
#define	NFSLCK_WRITEDENY	0x00000008
#define	NFSLCK_DENYBITS		(NFSLCK_READDENY | NFSLCK_WRITEDENY)
#define	NFSLCK_SHAREBITS 						\
    (NFSLCK_READACCESS|NFSLCK_WRITEACCESS|NFSLCK_READDENY|NFSLCK_WRITEDENY)
#define	NFSLCK_LOCKSHIFT	4
#define	NFSLCK_READ		0x00000010
#define	NFSLCK_WRITE		0x00000020
#define	NFSLCK_BLOCKING		0x00000040
#define	NFSLCK_RECLAIM		0x00000080
#define	NFSLCK_OPENTOLOCK	0x00000100
#define	NFSLCK_TEST		0x00000200
#define	NFSLCK_LOCK		0x00000400
#define	NFSLCK_UNLOCK		0x00000800
#define	NFSLCK_OPEN		0x00001000
#define	NFSLCK_CLOSE		0x00002000
#define	NFSLCK_CHECK		0x00004000
#define	NFSLCK_RELEASE		0x00008000
#define	NFSLCK_NEEDSCONFIRM	0x00010000
#define	NFSLCK_CONFIRM		0x00020000
#define	NFSLCK_DOWNGRADE	0x00040000
#define	NFSLCK_DELEGREAD	0x00080000
#define	NFSLCK_DELEGWRITE	0x00100000
#define	NFSLCK_DELEGCUR		0x00200000
#define	NFSLCK_DELEGPREV	0x00400000
#define	NFSLCK_OLDDELEG		0x00800000
#define	NFSLCK_DELEGRECALL	0x01000000
#define	NFSLCK_SETATTR		0x02000000
#define	NFSLCK_DELEGPURGE	0x04000000
#define	NFSLCK_DELEGRETURN	0x08000000
#define	NFSLCK_WANTWDELEG	0x10000000
#define	NFSLCK_WANTRDELEG	0x20000000
#define	NFSLCK_WANTNODELEG	0x40000000
#define	NFSLCK_WANTBITS							\
    (NFSLCK_WANTWDELEG | NFSLCK_WANTRDELEG | NFSLCK_WANTNODELEG)

/* And bits for nid_flag */
#define	NFSID_INITIALIZE	0x0001
#define	NFSID_ADDUID		0x0002
#define	NFSID_DELUID		0x0004
#define	NFSID_ADDUSERNAME	0x0008
#define	NFSID_DELUSERNAME	0x0010
#define	NFSID_ADDGID		0x0020
#define	NFSID_DELGID		0x0040
#define	NFSID_ADDGROUPNAME	0x0080
#define	NFSID_DELGROUPNAME	0x0100

/*
 * fs.nfs sysctl(3) identifiers
 */
#define	NFS_NFSSTATS	1		/* struct: struct nfsstats */

/*
 * Here is the definition of the attribute bits array and macros that
 * manipulate it.
 * THE MACROS MUST BE MANUALLY MODIFIED IF NFSATTRBIT_MAXWORDS CHANGES!!
 * It is (NFSATTRBIT_MAX + 31) / 32.
 */
#define	NFSATTRBIT_MAXWORDS	3

typedef struct {
	u_int32_t bits[NFSATTRBIT_MAXWORDS];
} nfsattrbit_t;

#define	NFSZERO_ATTRBIT(b) do {						\
	(b)->bits[0] = 0;						\
	(b)->bits[1] = 0;						\
	(b)->bits[2] = 0;						\
} while (0)

#define	NFSSET_ATTRBIT(t, f) do {					\
	(t)->bits[0] = (f)->bits[0];			 		\
	(t)->bits[1] = (f)->bits[1];					\
	(t)->bits[2] = (f)->bits[2];					\
} while (0)

#define	NFSSETSUPP_ATTRBIT(b) do { 					\
	(b)->bits[0] = NFSATTRBIT_SUPP0; 				\
	(b)->bits[1] = (NFSATTRBIT_SUPP1 | NFSATTRBIT_SUPPSETONLY);	\
	(b)->bits[2] = NFSATTRBIT_SUPP2;				\
} while (0)

#define	NFSISSET_ATTRBIT(b, p)	((b)->bits[(p) / 32] & (1 << ((p) % 32)))
#define	NFSSETBIT_ATTRBIT(b, p)	((b)->bits[(p) / 32] |= (1 << ((p) % 32)))
#define	NFSCLRBIT_ATTRBIT(b, p)	((b)->bits[(p) / 32] &= ~(1 << ((p) % 32)))

#define	NFSCLRALL_ATTRBIT(b, a)	do { 					\
	(b)->bits[0] &= ~((a)->bits[0]);	 			\
	(b)->bits[1] &= ~((a)->bits[1]);	 			\
	(b)->bits[2] &= ~((a)->bits[2]);				\
} while (0)

#define	NFSCLRNOT_ATTRBIT(b, a)	do { 					\
	(b)->bits[0] &= ((a)->bits[0]);		 			\
	(b)->bits[1] &= ((a)->bits[1]);		 			\
	(b)->bits[2] &= ((a)->bits[2]);		 			\
} while (0)

#define	NFSCLRNOTFILLABLE_ATTRBIT(b) do { 				\
	(b)->bits[0] &= NFSATTRBIT_SUPP0;	 			\
	(b)->bits[1] &= NFSATTRBIT_SUPP1;				\
	(b)->bits[2] &= NFSATTRBIT_SUPP2;				\
} while (0)

#define	NFSCLRNOTSETABLE_ATTRBIT(b) do { 				\
	(b)->bits[0] &= NFSATTRBIT_SETABLE0;	 			\
	(b)->bits[1] &= NFSATTRBIT_SETABLE1;				\
	(b)->bits[2] &= NFSATTRBIT_SETABLE2;				\
} while (0)

#define	NFSNONZERO_ATTRBIT(b)	((b)->bits[0] || (b)->bits[1] || (b)->bits[2])
#define	NFSEQUAL_ATTRBIT(b, p)	((b)->bits[0] == (p)->bits[0] &&	\
	(b)->bits[1] == (p)->bits[1] && (b)->bits[2] == (p)->bits[2])

#define	NFSGETATTR_ATTRBIT(b) do { 					\
	(b)->bits[0] = NFSATTRBIT_GETATTR0;	 			\
	(b)->bits[1] = NFSATTRBIT_GETATTR1;				\
	(b)->bits[2] = NFSATTRBIT_GETATTR2;				\
} while (0)

#define	NFSWCCATTR_ATTRBIT(b) do { 					\
	(b)->bits[0] = NFSATTRBIT_WCCATTR0;	 			\
	(b)->bits[1] = NFSATTRBIT_WCCATTR1;				\
	(b)->bits[2] = NFSATTRBIT_WCCATTR2;				\
} while (0)

#define	NFSWRITEGETATTR_ATTRBIT(b) do { 				\
	(b)->bits[0] = NFSATTRBIT_WRITEGETATTR0;			\
	(b)->bits[1] = NFSATTRBIT_WRITEGETATTR1;			\
	(b)->bits[2] = NFSATTRBIT_WRITEGETATTR2;			\
} while (0)

#define	NFSCBGETATTR_ATTRBIT(b, c) do { 				\
	(c)->bits[0] = ((b)->bits[0] & NFSATTRBIT_CBGETATTR0);		\
	(c)->bits[1] = ((b)->bits[1] & NFSATTRBIT_CBGETATTR1);		\
	(c)->bits[2] = ((b)->bits[2] & NFSATTRBIT_CBGETATTR2);		\
} while (0)

#define	NFSPATHCONF_GETATTRBIT(b) do { 					\
	(b)->bits[0] = NFSGETATTRBIT_PATHCONF0;		 		\
	(b)->bits[1] = NFSGETATTRBIT_PATHCONF1;				\
	(b)->bits[2] = NFSGETATTRBIT_PATHCONF2;				\
} while (0)

#define	NFSSTATFS_GETATTRBIT(b)	do { 					\
	(b)->bits[0] = NFSGETATTRBIT_STATFS0;	 			\
	(b)->bits[1] = NFSGETATTRBIT_STATFS1;				\
	(b)->bits[2] = NFSGETATTRBIT_STATFS2;				\
} while (0)

#define	NFSISSETSTATFS_ATTRBIT(b) 					\
		(((b)->bits[0] & NFSATTRBIT_STATFS0) || 		\
		 ((b)->bits[1] & NFSATTRBIT_STATFS1) ||			\
		 ((b)->bits[2] & NFSATTRBIT_STATFS2))

#define	NFSCLRSTATFS_ATTRBIT(b)	do { 					\
	(b)->bits[0] &= ~NFSATTRBIT_STATFS0;	 			\
	(b)->bits[1] &= ~NFSATTRBIT_STATFS1;				\
	(b)->bits[2] &= ~NFSATTRBIT_STATFS2;				\
} while (0)

#define	NFSREADDIRPLUS_ATTRBIT(b) do { 					\
	(b)->bits[0] = NFSATTRBIT_READDIRPLUS0;		 		\
	(b)->bits[1] = NFSATTRBIT_READDIRPLUS1;				\
	(b)->bits[2] = NFSATTRBIT_READDIRPLUS2;				\
} while (0)

#define	NFSREFERRAL_ATTRBIT(b) do { 					\
	(b)->bits[0] = NFSATTRBIT_REFERRAL0;		 		\
	(b)->bits[1] = NFSATTRBIT_REFERRAL1;				\
	(b)->bits[2] = NFSATTRBIT_REFERRAL2;				\
} while (0)

/*
 * Store uid, gid creds that were used when the stateid was acquired.
 * The RPC layer allows NFS_MAXGRPS + 1 groups to go out on the wire,
 * so that's how many gets stored here.
 */
struct nfscred {
	uid_t 		nfsc_uid;
	gid_t		nfsc_groups[NFS_MAXGRPS + 1];
	int		nfsc_ngroups;
};

/*
 * Constants that define the file handle for the V4 root directory.
 * (The FSID must never be used by other file systems that are exported.)
 */
#define	NFSV4ROOT_FSID0		((int32_t) -1)
#define	NFSV4ROOT_FSID1		((int32_t) -1)
#define	NFSV4ROOT_REFERRAL	((int32_t) -2)
#define	NFSV4ROOT_INO		2	/* It's traditional */
#define	NFSV4ROOT_GEN		1

/*
 * The set of signals the interrupt an I/O in progress for NFSMNT_INT mounts.
 * What should be in this set is open to debate, but I believe that since
 * I/O system calls on ufs are never interrupted by signals the set should
 * be minimal. My reasoning is that many current programs that use signals
 * such as SIGALRM will not expect file I/O system calls to be interrupted
 * by them and break.
 */
#if defined(_KERNEL) || defined(KERNEL)

struct uio; struct buf; struct vattr; struct nameidata;	/* XXX */

/*
 * Socket errors ignored for connectionless sockets?
 * For now, ignore them all
 */
#define	NFSIGNORE_SOERROR(s, e) 					\
		((e) != EINTR && (e) != ERESTART && (e) != EWOULDBLOCK && \
		((s) & PR_CONNREQUIRED) == 0)


/*
 * This structure holds socket information for a connection. Used by the
 * client and the server for callbacks.
 */
struct nfssockreq {
	NFSSOCKADDR_T	nr_nam;
	int		nr_sotype;
	int		nr_soproto;
	int		nr_soflags;
	struct ucred	*nr_cred;
	int		nr_lock;
	NFSMUTEX_T	nr_mtx;
	u_int32_t	nr_prog;
	u_int32_t	nr_vers;
	struct __rpc_client *nr_client;
	AUTH		*nr_auth;
};

/*
 * And associated nr_lock bits.
 */
#define	NFSR_SNDLOCK		0x01
#define	NFSR_WANTSND		0x02
#define	NFSR_RCVLOCK		0x04
#define	NFSR_WANTRCV		0x08
#define	NFSR_RESERVEDPORT	0x10
#define	NFSR_LOCALHOST		0x20

/*
 * Queue head for nfsreq's
 */
TAILQ_HEAD(nfsreqhead, nfsreq);

/* This is the only nfsreq R_xxx flag still used. */
#define	R_DONTRECOVER	0x00000100	/* don't initiate recovery when this
					   rpc gets a stale state reply */

/*
 * Network address hash list element
 */
union nethostaddr {
	struct in_addr	had_inet;
	struct in6_addr had_inet6;
};

/*
 * Structure of list of mechanisms.
 */
struct nfsgss_mechlist {
	int	len;
	const u_char	*str;
	int	totlen;
};
#define	KERBV_MECH	0	/* position in list */

/*
 * This structure is used by the server for describing each request.
 */
struct nfsrv_descript {
	mbuf_t			nd_mrep;	/* Request mbuf list */
	mbuf_t			nd_md;		/* Current dissect mbuf */
	mbuf_t			nd_mreq;	/* Reply mbuf list */
	mbuf_t			nd_mb;		/* Current build mbuf */
	NFSSOCKADDR_T		nd_nam;		/* and socket addr */
	NFSSOCKADDR_T		nd_nam2;	/* return socket addr */
	caddr_t			nd_dpos;	/* Current dissect pos */
	caddr_t			nd_bpos;	/* Current build pos */
	u_int64_t		nd_flag;	/* nd_flag */
	u_int16_t		nd_procnum;	/* RPC # */
	u_int32_t		nd_repstat;	/* Reply status */
	int			*nd_errp;	/* Pointer to ret status */
	u_int32_t		nd_retxid;	/* Reply xid */
	struct nfsrvcache	*nd_rp;		/* Assoc. cache entry */
	fhandle_t		nd_fh;		/* File handle */
	struct ucred		*nd_cred;	/* Credentials */
	uid_t			nd_saveduid;	/* Saved uid */
	u_int64_t		nd_sockref;	/* Rcv socket ref# */
	u_int64_t		nd_compref;	/* Compound RPC ref# */
	time_t			nd_tcpconntime;	/* Time TCP connection est. */
	nfsquad_t		nd_clientid;	/* Implied clientid */
	int			nd_gssnamelen;	/* principal name length */
	char			*nd_gssname;	/* principal name */
	uint32_t		*nd_slotseq;	/* ptr to slot seq# in req */
	uint8_t			nd_sessionid[NFSX_V4SESSIONID];	/* Session id */
	uint32_t		nd_slotid;	/* Slotid for this RPC */
	SVCXPRT			*nd_xprt;	/* Server RPC handle */
	uint32_t		*nd_sequence;	/* Sequence Op. ptr */
	nfsv4stateid_t		nd_curstateid;	/* Current StateID */
	nfsv4stateid_t		nd_savedcurstateid; /* Saved Current StateID */
};

#define	nd_princlen	nd_gssnamelen
#define	nd_principal	nd_gssname

/* Bits for "nd_flag" */
#define	ND_DONTSAVEREPLY 	0x00000001
#define	ND_SAVEREPLY		0x00000002
#define	ND_NFSV2		0x00000004
#define	ND_NFSV3		0x00000008
#define	ND_NFSV4		0x00000010
#define	ND_KERBV		0x00000020
#define	ND_GSSINTEGRITY		0x00000040
#define	ND_GSSPRIVACY		0x00000080
#define	ND_WINDOWVERF		0x00000100
#define	ND_GSSINITREPLY		0x00000200
#define	ND_STREAMSOCK		0x00000400
#define	ND_PUBLOOKUP		0x00000800
#define	ND_USEGSSNAME		0x00001000
#define	ND_SAMETCPCONN		0x00002000
#define	ND_IMPLIEDCLID		0x00004000
#define	ND_NOMOREDATA		0x00008000
#define	ND_V4WCCATTR		0x00010000
#define	ND_NFSCB		0x00020000
#define	ND_AUTHNONE		0x00040000
#define	ND_EXAUTHSYS		0x00080000
#define	ND_EXGSS		0x00100000
#define	ND_EXGSSINTEGRITY	0x00200000
#define	ND_EXGSSPRIVACY		0x00400000
#define	ND_INCRSEQID		0x00800000
#define	ND_NFSCL		0x01000000
#define	ND_NFSV41		0x02000000
#define	ND_HASSEQUENCE		0x04000000
#define	ND_CACHETHIS		0x08000000
#define	ND_LASTOP		0x10000000
#define	ND_LOOPBADSESS		0x20000000
#define	ND_DSSERVER		0x40000000
#define	ND_CURSTATEID		0x80000000
#define	ND_SAVEDCURSTATEID	0x100000000
#define	ND_HASSLOTID		0x200000000

/*
 * ND_GSS should be the "or" of all GSS type authentications.
 */
#define	ND_GSS		(ND_KERBV)

struct nfsv4_opflag {
	int	retfh;
	int	needscfh;
	int	savereply;
	int	modifyfs;
	int	lktype;
	int	needsseq;
	int	loopbadsess;
};

/*
 * Flags used to indicate what to do w.r.t. seqid checking.
 */
#define	NFSRVSEQID_FIRST	0x01
#define	NFSRVSEQID_LAST		0x02
#define	NFSRVSEQID_OPEN		0x04

/*
 * assign a doubly linked list to a new head
 * and prepend one list into another.
 */
#define	LIST_NEWHEAD(nhead, ohead, field) do { 				\
	if (((nhead)->lh_first = (ohead)->lh_first) != NULL) 		\
		(ohead)->lh_first->field.le_prev = &(nhead)->lh_first; 	\
	(ohead)->lh_first = NULL; 					\
    } while (0)

#define	LIST_PREPEND(head, phead, lelm, field) do {			\
	if ((head)->lh_first != NULL) {					\
		(lelm)->field.le_next = (head)->lh_first;		\
		(lelm)->field.le_next->field.le_prev =			\
		    &(lelm)->field.le_next;				\
	}								\
	(head)->lh_first = (phead)->lh_first;				\
	(head)->lh_first->field.le_prev = &(head)->lh_first;		\
    } while (0)

/*
 * File handle structure for client. Malloc'd to the correct length with
 * malloc type M_NFSFH.
 */
struct nfsfh {
	u_int16_t	nfh_len;	/* Length of file handle */
	u_int8_t	nfh_fh[1];	/* and the file handle */
};

/*
 * File handle structure for server. The NFSRV_MAXFH constant is
 * set in nfsdport.h. I use a 32bit length, so that alignment is
 * preserved.
 */
struct nfsrvfh {
	u_int32_t	nfsrvfh_len;
	u_int8_t	nfsrvfh_data[NFSRV_MAXFH];
};

/*
 * This structure is used for sleep locks on the NFSv4 nfsd threads and
 * NFSv4 client data structures.
 */
struct nfsv4lock {
	u_int32_t	nfslock_usecnt;
	u_int8_t	nfslock_lock;
};
#define	NFSV4LOCK_LOCK		0x01
#define	NFSV4LOCK_LOCKWANTED	0x02
#define	NFSV4LOCK_WANTED	0x04

/*
 * Values for the override argument for nfsvno_accchk().
 */
#define	NFSACCCHK_NOOVERRIDE		0
#define	NFSACCCHK_ALLOWROOT		1
#define	NFSACCCHK_ALLOWOWNER		2

/*
 * and values for the vpislocked argument for nfsvno_accchk().
 */
#define	NFSACCCHK_VPNOTLOCKED		0
#define	NFSACCCHK_VPISLOCKED		1

/*
 * Slot for the NFSv4.1 Sequence Op.
 */
struct nfsslot {
	int		nfssl_inprog;
	uint32_t	nfssl_seq;
	struct mbuf	*nfssl_reply;
};

#endif	/* _KERNEL */

#endif	/* _NFS_NFS_H */
