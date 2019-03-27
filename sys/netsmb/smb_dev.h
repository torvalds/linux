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
#ifndef _NETSMB_DEV_H_
#define _NETSMB_DEV_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

#include <netsmb/smb.h>

#define	NSMB_NAME		"nsmb"

#define NSMB_VERMAJ	1
#define NSMB_VERMIN	3006
#define NSMB_VERSION	(NSMB_VERMAJ * 100000 + NSMB_VERMIN)

#define NSMBFL_OPEN		0x0001

#define	SMBVOPT_CREATE		0x0001	/* create object if necessary */
#define	SMBVOPT_PRIVATE		0x0002	/* connection should be private */
#define	SMBVOPT_SINGLESHARE	0x0004	/* keep only one share at this VC */
#define	SMBVOPT_PERMANENT	0x0010	/* object will keep last reference */

#define	SMBSOPT_CREATE		0x0001	/* create object if necessary */
#define	SMBSOPT_PERMANENT	0x0010	/* object will keep last reference */

/*
 * SMBIOC_LOOKUP flags
 */
#define SMBLK_CREATE		0x0001

struct smbioc_ossn {
	int		ioc_opt;
	uint32_t	ioc_svlen;	/* size of ioc_server address */
	struct sockaddr*ioc_server;
	uint32_t	ioc_lolen;	/* size of ioc_local address */
	struct sockaddr*ioc_local;
	char		ioc_srvname[SMB_MAXSRVNAMELEN + 1];
	int		ioc_timeout;
	int		ioc_retrycount;	/* number of retries before giveup */
	char		ioc_localcs[16];/* local charset */
	char		ioc_servercs[16];/* server charset */
	char		ioc_user[SMB_MAXUSERNAMELEN + 1];
	char		ioc_workgroup[SMB_MAXUSERNAMELEN + 1];
	char		ioc_password[SMB_MAXPASSWORDLEN + 1];
	uid_t		ioc_owner;	/* proposed owner */
	gid_t		ioc_group;	/* proposed group */
	mode_t		ioc_mode;	/* desired access mode */
	mode_t		ioc_rights;	/* SMBM_* */
};

struct smbioc_oshare {
	int		ioc_opt;
	int		ioc_stype;	/* share type */
	char		ioc_share[SMB_MAXSHARENAMELEN + 1];
	char		ioc_password[SMB_MAXPASSWORDLEN + 1];
	uid_t		ioc_owner;	/* proposed owner of share */
	gid_t		ioc_group;	/* proposed group of share */
	mode_t		ioc_mode;	/* desired access mode to share */
	mode_t		ioc_rights;	/* SMBM_* */
};

struct smbioc_rq {
	u_char		ioc_cmd;
	u_char		ioc_twc;
	void *		ioc_twords;
	u_short		ioc_tbc;
	void *		ioc_tbytes;
	int		ioc_rpbufsz;
	char *		ioc_rpbuf;
	u_char		ioc_rwc;
	u_short		ioc_rbc;
	u_int8_t	ioc_errclass;
	u_int16_t	ioc_serror;
	u_int32_t	ioc_error;
};

struct smbioc_t2rq {
	u_int16_t	ioc_setup[3];
	int		ioc_setupcnt;
	char *		ioc_name;
	u_short		ioc_tparamcnt;
	void *		ioc_tparam;
	u_short		ioc_tdatacnt;
	void *		ioc_tdata;
	u_short		ioc_rparamcnt;
	void *		ioc_rparam;
	u_short		ioc_rdatacnt;
	void *		ioc_rdata;
};

struct smbioc_flags {
	int		ioc_level;	/* 0 - session, 1 - share */
	int		ioc_mask;
	int		ioc_flags;
};

struct smbioc_lookup {
	int		ioc_level;
	int		ioc_flags;
	struct smbioc_ossn	ioc_ssn;
	struct smbioc_oshare	ioc_sh;
};

struct smbioc_rw {
	smbfh	ioc_fh;
	char *	ioc_base;
	off_t	ioc_offset;
	int	ioc_cnt;
};

/*
 * Device IOCTLs
 */
#define	SMBIOC_OPENSESSION	_IOW('n',  100, struct smbioc_ossn)
#define	SMBIOC_OPENSHARE	_IOW('n',  101, struct smbioc_oshare)
#define	SMBIOC_REQUEST		_IOWR('n', 102, struct smbioc_rq)
#define	SMBIOC_T2RQ		_IOWR('n', 103, struct smbioc_t2rq)
#define	SMBIOC_SETFLAGS		_IOW('n',  104, struct smbioc_flags)
#define	SMBIOC_LOOKUP		_IOW('n',  106, struct smbioc_lookup)
#define	SMBIOC_READ		_IOWR('n', 107, struct smbioc_rw)
#define	SMBIOC_WRITE		_IOWR('n', 108, struct smbioc_rw)

#ifdef _KERNEL

#define SMBST_CONNECTED	1

STAILQ_HEAD(smbrqh, smb_rq);

struct smb_dev {
	struct cdev *	dev;
	int		sd_opened;
	int		sd_level;
	struct smb_vc * sd_vc;		/* reference to VC */
	struct smb_share *sd_share;	/* reference to share if any */
	int		sd_poll;
	int		sd_seq;
	int		sd_flags;
	int		refcount;
	int		usecount;
};

extern struct sx smb_lock;
#define	SMB_LOCK()		sx_xlock(&smb_lock)
#define	SMB_UNLOCK() 		sx_unlock(&smb_lock)
#define	SMB_LOCKASSERT()	sx_assert(&smb_lock, SA_XLOCKED)

struct smb_cred;

void sdp_dtor(void *arg);
void sdp_trydestroy(struct smb_dev *dev);

/*
 * Compound user interface
 */
int  smb_usr_lookup(struct smbioc_lookup *dp, struct smb_cred *scred,
	struct smb_vc **vcpp, struct smb_share **sspp);
int  smb_usr_opensession(struct smbioc_ossn *data,
	struct smb_cred *scred,	struct smb_vc **vcpp);
int  smb_usr_openshare(struct smb_vc *vcp, struct smbioc_oshare *data,
	struct smb_cred *scred, struct smb_share **sspp);
int  smb_usr_simplerequest(struct smb_share *ssp, struct smbioc_rq *data,
	struct smb_cred *scred);
int  smb_usr_t2request(struct smb_share *ssp, struct smbioc_t2rq *data,
	struct smb_cred *scred);
int  smb_dev2share(int fd, int mode, struct smb_cred *scred,
	struct smb_share **sspp, struct smb_dev **ssdp);


#endif /* _KERNEL */

#endif /* _NETSMB_DEV_H_ */
