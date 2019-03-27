/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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

/*
 * Two levels of connection hierarchy
 */
#define	SMBL_SM		0
#define SMBL_VC		1
#define SMBL_SHARE	2
#define SMBL_NUM	3
#define SMBL_NONE	(-1)

#define	SMB_CS_NONE	0x0000
#define	SMB_CS_UPPER	0x0001		/* convert passed string to upper case */
#define	SMB_CS_LOWER	0x0002		/* convert passed string to lower case */

/*
 * Common object flags
 */
#define SMBO_GONE		0x1000000

/*
 * access modes
 */
#define	SMBM_READ		0400	/* read conn attrs.(like list shares) */
#define	SMBM_WRITE		0200	/* modify conn attrs */
#define	SMBM_EXEC		0100	/* can send SMB requests */
#define	SMBM_READGRP		0040
#define	SMBM_WRITEGRP		0020
#define	SMBM_EXECGRP		0010
#define	SMBM_READOTH		0004
#define	SMBM_WRITEOTH		0002
#define	SMBM_EXECOTH		0001
#define	SMBM_MASK		0777
#define	SMBM_EXACT		010000	/* check for specified mode exactly */
#define	SMBM_ALL		(SMBM_READ | SMBM_WRITE | SMBM_EXEC)
#define	SMBM_DEFAULT		(SMBM_READ | SMBM_WRITE | SMBM_EXEC)
#define	SMBM_ANY_OWNER		((uid_t)-1)
#define	SMBM_ANY_GROUP		((gid_t)-1)

/*
 * VC flags
 */
#define SMBV_PERMANENT		0x0002
#define SMBV_LONGNAMES		0x0004	/* connection is configured to use long names */
#define	SMBV_ENCRYPT		0x0008	/* server asked for encrypted password */
#define	SMBV_WIN95		0x0010	/* used to apply bugfixes for this OS */
#define	SMBV_PRIVATE		0x0020	/* connection can be used only by creator */
#define	SMBV_RECONNECTING	0x0040	/* conn is in the process of reconnection */
#define SMBV_SINGLESHARE	0x0080	/* only one share connecting should be allowed */
#define SMBV_CREATE		0x0100	/* lookup for create operation */
/*#define SMBV_FAILED		0x0200*/	/* last reconnect attempt has failed */
#define SMBV_UNICODE		0x0400	/* connection is configured to use Unicode */


/*
 * smb_share flags
 */
#define SMBS_PERMANENT		0x0001
#define SMBS_RECONNECTING	0x0002
#define SMBS_CONNECTED		0x0004

/*
 * share types
 */
#define	SMB_ST_DISK		0x0	/* A: */
#define	SMB_ST_PRINTER		0x1	/* LPT: */
#define	SMB_ST_PIPE		0x2	/* IPC */
#define	SMB_ST_COMM		0x3	/* COMM */
#define	SMB_ST_ANY		0x4
#define	SMB_ST_MAX		0x4
#define SMB_ST_NONE		0xff	/* not a part of protocol */

/*
 * Negotiated protocol parameters
 */
struct smb_sopt {
	int		sv_proto;
	int16_t		sv_tz;		/* offset in min relative to UTC */
	u_int32_t	sv_maxtx;	/* maximum transmit buf size */
	u_char		sv_sm;		/* security mode */
	u_int16_t	sv_maxmux;	/* max number of outstanding rq's */
	u_int16_t 	sv_maxvcs;	/* max number of VCs */
	u_int16_t	sv_rawmode;
	u_int32_t	sv_maxraw;	/* maximum raw-buffer size */
	u_int32_t	sv_skey;	/* session key */
	u_int32_t	sv_caps;	/* capabilities SMB_CAP_ */
};

/*
 * network IO daemon states
 */
enum smbiod_state {
	SMBIOD_ST_NOTCONN,	/* no connect request was made */
	SMBIOD_ST_RECONNECT,	/* a [re]connect attempt is in progress */
	SMBIOD_ST_TRANACTIVE,	/* transport level is up */
	SMBIOD_ST_VCACTIVE,	/* session established */
	SMBIOD_ST_DEAD		/* connection broken, transport is down */
};


/*
 * Info structures
 */
#define	SMB_INFO_NONE		0
#define	SMB_INFO_VC		2
#define	SMB_INFO_SHARE		3

struct smb_vc_info {
	int		itype;
	int		usecount;
	uid_t		uid;		/* user id of connection */
	gid_t		gid;		/* group of connection */
	mode_t		mode;		/* access mode */
	int		flags;
	enum smbiod_state iodstate;
	struct smb_sopt	sopt;
	char		srvname[SMB_MAXSRVNAMELEN + 1];
	char		vcname[128];
};

struct smb_share_info {
	int		itype;
	int		usecount;
	u_short		tid;		/* TID */
	int		type;		/* share type */
	uid_t		uid;		/* user id of connection */
	gid_t		gid;		/* group of connection */
	mode_t		mode;		/* access mode */
	int		flags;
	char		sname[128];
};

#ifdef _KERNEL

#include <netsmb/smb_subr.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/condvar.h>
#include <sys/sx.h>

#define CONNADDREQ(a1,a2)	((a1)->sa_len == (a2)->sa_len && \
				 bcmp(a1, a2, (a1)->sa_len) == 0)

struct smb_vc;
struct smb_share;
struct smb_cred;
struct smb_rq;
struct mbdata;
struct smbioc_oshare;
struct smbioc_ossn;
struct uio;

TAILQ_HEAD(smb_rqhead, smb_rq);

#define SMB_DEFRQTIMO	5

#define SMB_DIALECT(vcp)	((vcp)->vc_sopt.sv_proto)

struct smb_tran_desc;

/*
 * Connection object
 */
struct smb_connobj;

typedef void smb_co_gone_t (struct smb_connobj *cp, struct smb_cred *scred);
typedef void smb_co_free_t (struct smb_connobj *cp);

struct smb_connobj {
	struct cv		co_lock;
	struct thread		*co_locker;
	struct sx		co_interlock;
	int			co_lockcnt;
	int			co_level;	/* SMBL_ */
	int			co_flags;
	int			co_usecount;
	struct smb_connobj *	co_parent;
	SLIST_HEAD(,smb_connobj)co_children;
	SLIST_ENTRY(smb_connobj)co_next;
	smb_co_gone_t *		co_gone;
	smb_co_free_t *		co_free;
};

#define	SMBCO_FOREACH(var, cp)	SLIST_FOREACH((var), &(cp)->co_children, co_next)

/*
 * Virtual Circuit (session) to a server.
 * This is the most (over)complicated part of SMB protocol.
 * For the user security level (usl), each session with different remote
 * user name has its own VC.
 * It is unclear however, should share security level (ssl) allow additional
 * VCs, because user name is not used and can be the same. On other hand,
 * multiple VCs allows us to create separate sessions to server on a per
 * user basis.
 */

/*
 * This lock protects vc_flags
 */
#define	SMBC_ST_LOCK(vcp)	smb_sl_lock(&(vcp)->vc_stlock)
#define	SMBC_ST_UNLOCK(vcp)	smb_sl_unlock(&(vcp)->vc_stlock)

struct smb_vc {
	struct smb_connobj obj;
	char *		vc_srvname;
	struct sockaddr*vc_paddr;	/* server addr */
	struct sockaddr*vc_laddr;	/* local addr, if any */
	char *		vc_username;
	char *		vc_pass;	/* password for usl case */
	char *		vc_domain;	/* workgroup/primary domain */

	u_int		vc_timo;	/* default request timeout */
	int		vc_maxvcs;	/* maximum number of VC per connection */

	void *		vc_tolower;	/* local charset */
	void *		vc_toupper;	/* local charset */
	void *		vc_toserver;	/* local charset to server one */
	void *		vc_tolocal;	/* server charset to local one */
	void *		vc_cp_toserver;	/* local charset to server one (using CodePage) */
	void *		vc_cp_tolocal;	/* server charset to local one (using CodePage) */
	void *		vc_ucs_toserver; /* local charset to server one (using UCS-2) */
	void *		vc_ucs_tolocal;	/* server charset to local one (using UCS-2) */
	int		vc_number;	/* number of this VC from the client side */
	int		vc_genid;
	uid_t		vc_uid;		/* user id of connection */
	gid_t		vc_grp;		/* group of connection */
	mode_t		vc_mode;	/* access mode */
	u_short		vc_smbuid;	/* unique vc id assigned by server */

	u_char		vc_hflags;	/* or'ed with flags in the smb header */
	u_short		vc_hflags2;	/* or'ed with flags in the smb header */
	void *		vc_tdata;	/* transport control block */
	struct smb_tran_desc *vc_tdesc;
	int		vc_chlen;	/* actual challenge length */
	u_char 		vc_ch[SMB_MAXCHALLENGELEN];
	u_short		vc_mid;		/* multiplex id */
	struct smb_sopt	vc_sopt;	/* server options */
	int		vc_txmax;	/* max tx/rx packet size */
	int		vc_rxmax;	/* max readx data size */
	int		vc_wxmax;	/* max writex data size */
	struct smbiod *	vc_iod;
	struct smb_slock vc_stlock;
	u_int32_t	vc_seqno;	/* my next sequence number */
	u_int8_t	*vc_mackey;	/* MAC key */
	int		vc_mackeylen;	/* length of MAC key */
};

#define vc_maxmux	vc_sopt.sv_maxmux
#define	vc_flags	obj.co_flags

#define SMB_UNICODE_STRINGS(vcp)	((vcp)->vc_hflags2 & SMB_FLAGS2_UNICODE)

#define	SMB_UNICODE_NAME	"UCS-2LE"

/*
 * smb_share structure describes connection to the given SMB share (tree).
 * Connection to share is always built on top of the VC.
 */

/*
 * This lock protects ss_flags
 */
#define	SMBS_ST_LOCK(ssp)	smb_sl_lock(&(ssp)->ss_stlock)
#define	SMBS_ST_LOCKPTR(ssp)	(&(ssp)->ss_stlock)
#define	SMBS_ST_UNLOCK(ssp)	smb_sl_unlock(&(ssp)->ss_stlock)

struct smb_share {
	struct smb_connobj obj;
	char *		ss_name;
	u_short		ss_tid;		/* TID */
	int		ss_type;	/* share type */
	uid_t		ss_uid;		/* user id of connection */
	gid_t		ss_grp;		/* group of connection */
	mode_t		ss_mode;	/* access mode */
	int		ss_vcgenid;
	char *		ss_pass;	/* password to a share, can be null */
	struct smb_slock ss_stlock;
};

#define	ss_flags	obj.co_flags

#define CPTOVC(cp)	((struct smb_vc*)(cp))
#define VCTOCP(vcp)	(&(vcp)->obj)
#define CPTOSS(cp)	((struct smb_share*)(cp))
#define	SSTOVC(ssp)	CPTOVC(((ssp)->obj.co_parent))
#define SSTOCP(ssp)	(&(ssp)->obj)

struct smb_vcspec {
	char *		srvname;
	struct sockaddr*sap;
	struct sockaddr*lap;
	int		flags;
	char *		username;
	char *		pass;
	char *		domain;
	mode_t		mode;
	mode_t		rights;
	uid_t		owner;
	gid_t		group;
	char *		localcs;
	char *		servercs;
	struct smb_sharespec *shspec;
	struct smb_share *ssp;		/* returned */
	/*
	 * The rest is an internal data
	 */
	struct smb_cred *scred;
};

struct smb_sharespec {
	char *		name;
	char *		pass;
	mode_t		mode;
	mode_t		rights;
	uid_t		owner;
	gid_t		group;
	int		stype;
	/*
	 * The rest is an internal data
	 */
	struct smb_cred *scred;
};

/*
 * Session level functions
 */
int  smb_sm_init(void);
int  smb_sm_done(void);
int  smb_sm_lookup(struct smb_vcspec *vcspec,
	struct smb_sharespec *shspec, struct smb_cred *scred,
	struct smb_vc **vcpp);

/*
 * Connection object
 */
void smb_co_ref(struct smb_connobj *cp);
void smb_co_rele(struct smb_connobj *cp, struct smb_cred *scred);
int  smb_co_get(struct smb_connobj *cp, struct smb_cred *scred);
void smb_co_put(struct smb_connobj *cp, struct smb_cred *scred);
int  smb_co_lock(struct smb_connobj *cp);
void smb_co_unlock(struct smb_connobj *cp);

/*
 * session level functions
 */
int  smb_vc_create(struct smb_vcspec *vcspec,
	struct smb_cred *scred, struct smb_vc **vcpp);
int  smb_vc_connect(struct smb_vc *vcp, struct smb_cred *scred);
int  smb_vc_access(struct smb_vc *vcp, struct smb_cred *scred, mode_t mode);
int  smb_vc_get(struct smb_vc *vcp, struct smb_cred *scred);
void smb_vc_put(struct smb_vc *vcp, struct smb_cred *scred);
void smb_vc_ref(struct smb_vc *vcp);
void smb_vc_rele(struct smb_vc *vcp, struct smb_cred *scred);
int  smb_vc_lock(struct smb_vc *vcp);
void smb_vc_unlock(struct smb_vc *vcp);
int  smb_vc_lookupshare(struct smb_vc *vcp, struct smb_sharespec *shspec,
	struct smb_cred *scred, struct smb_share **sspp);
const char * smb_vc_getpass(struct smb_vc *vcp);
u_short smb_vc_nextmid(struct smb_vc *vcp);

/*
 * share level functions
 */
int  smb_share_create(struct smb_vc *vcp, struct smb_sharespec *shspec,
	struct smb_cred *scred, struct smb_share **sspp);
int  smb_share_access(struct smb_share *ssp, struct smb_cred *scred, mode_t mode);
void smb_share_ref(struct smb_share *ssp);
void smb_share_rele(struct smb_share *ssp, struct smb_cred *scred);
int  smb_share_get(struct smb_share *ssp, struct smb_cred *scred);
void smb_share_put(struct smb_share *ssp, struct smb_cred *scred);
int  smb_share_lock(struct smb_share *ssp);
void smb_share_unlock(struct smb_share *ssp);
void smb_share_invalidate(struct smb_share *ssp);
int  smb_share_valid(struct smb_share *ssp);
const char * smb_share_getpass(struct smb_share *ssp);

/*
 * SMB protocol level functions
 */
int  smb_smb_negotiate(struct smb_vc *vcp, struct smb_cred *scred);
int  smb_smb_ssnsetup(struct smb_vc *vcp, struct smb_cred *scred);
int  smb_smb_ssnclose(struct smb_vc *vcp, struct smb_cred *scred);
int  smb_smb_treeconnect(struct smb_share *ssp, struct smb_cred *scred);
int  smb_smb_treedisconnect(struct smb_share *ssp, struct smb_cred *scred);
int  smb_read(struct smb_share *ssp, u_int16_t fid, struct uio *uio,
	struct smb_cred *scred);
int  smb_write(struct smb_share *ssp, u_int16_t fid, struct uio *uio,
	struct smb_cred *scred);
int  smb_smb_echo(struct smb_vc *vcp, struct smb_cred *scred);

/*
 * smbiod thread
 */

#define	SMBIOD_EV_NEWRQ		0x0001
#define	SMBIOD_EV_SHUTDOWN	0x0002
#define	SMBIOD_EV_CONNECT	0x0003
#define	SMBIOD_EV_DISCONNECT	0x0004
#define	SMBIOD_EV_TREECONNECT	0x0005
#define	SMBIOD_EV_MASK		0x00ff
#define	SMBIOD_EV_SYNC		0x0100
#define	SMBIOD_EV_PROCESSING	0x0200

struct smbiod_event {
	int	ev_type;
	int	ev_error;
	void *	ev_ident;
	STAILQ_ENTRY(smbiod_event)	ev_link;
};

#define	SMBIOD_SHUTDOWN		0x0001

struct smbiod {
	int			iod_id;
	int			iod_flags;
	enum smbiod_state	iod_state;
	int			iod_muxcnt;	/* number of active outstanding requests */
	int			iod_sleeptimo;
	struct smb_vc *		iod_vc;
	struct smb_slock	iod_rqlock;	/* iod_rqlist, iod_muxwant */
	struct smb_rqhead	iod_rqlist;	/* list of outstanding requests */
	int			iod_muxwant;
	struct proc *		iod_p;
	struct thread *		iod_td;
	struct smb_cred		iod_scred;
	struct smb_slock	iod_evlock;	/* iod_evlist */
	STAILQ_HEAD(,smbiod_event) iod_evlist;
	struct timespec 	iod_lastrqsent;
	struct timespec 	iod_pingtimo;
};

int  smb_iod_init(void);
int  smb_iod_done(void);
int  smb_iod_create(struct smb_vc *vcp);
int  smb_iod_destroy(struct smbiod *iod);
int  smb_iod_request(struct smbiod *iod, int event, void *ident);
int  smb_iod_addrq(struct smb_rq *rqp);
int  smb_iod_waitrq(struct smb_rq *rqp);
int  smb_iod_removerq(struct smb_rq *rqp);

#endif /* _KERNEL */
