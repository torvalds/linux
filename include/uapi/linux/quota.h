/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Robert Elz at The University of Melbourne.
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
 */

#ifndef _UAPI_LINUX_QUOTA_
#define _UAPI_LINUX_QUOTA_

#include <linux/errno.h>
#include <linux/types.h>

#define __DQUOT_VERSION__	"dquot_6.6.0"

#define MAXQUOTAS 3
#define USRQUOTA  0		/* element used for user quotas */
#define GRPQUOTA  1		/* element used for group quotas */
#define PRJQUOTA  2		/* element used for project quotas */

/*
 * Definitions for the default names of the quotas files.
 */
#define INITQFNAMES { \
	"user",    /* USRQUOTA */ \
	"group",   /* GRPQUOTA */ \
	"project", /* PRJQUOTA */ \
	"undefined", \
};

/*
 * Command definitions for the 'quotactl' system call.
 * The commands are broken into a main command defined below
 * and a subcommand that is used to convey the type of
 * quota that is being manipulated (see above).
 */
#define SUBCMDMASK  0x00ff
#define SUBCMDSHIFT 8
#define QCMD(cmd, type)  (((cmd) << SUBCMDSHIFT) | ((type) & SUBCMDMASK))

#define Q_SYNC     0x800001	/* sync disk copy of a filesystems quotas */
#define Q_QUOTAON  0x800002	/* turn quotas on */
#define Q_QUOTAOFF 0x800003	/* turn quotas off */
#define Q_GETFMT   0x800004	/* get quota format used on given filesystem */
#define Q_GETINFO  0x800005	/* get information about quota files */
#define Q_SETINFO  0x800006	/* set information about quota files */
#define Q_GETQUOTA 0x800007	/* get user quota structure */
#define Q_SETQUOTA 0x800008	/* set user quota structure */

/* Quota format type IDs */
#define	QFMT_VFS_OLD 1
#define	QFMT_VFS_V0 2
#define QFMT_OCFS2 3
#define	QFMT_VFS_V1 4

/* Size of block in which space limits are passed through the quota
 * interface */
#define QIF_DQBLKSIZE_BITS 10
#define QIF_DQBLKSIZE (1 << QIF_DQBLKSIZE_BITS)

/*
 * Quota structure used for communication with userspace via quotactl
 * Following flags are used to specify which fields are valid
 */
enum {
	QIF_BLIMITS_B = 0,
	QIF_SPACE_B,
	QIF_ILIMITS_B,
	QIF_INODES_B,
	QIF_BTIME_B,
	QIF_ITIME_B,
};

#define QIF_BLIMITS	(1 << QIF_BLIMITS_B)
#define QIF_SPACE	(1 << QIF_SPACE_B)
#define QIF_ILIMITS	(1 << QIF_ILIMITS_B)
#define QIF_INODES	(1 << QIF_INODES_B)
#define QIF_BTIME	(1 << QIF_BTIME_B)
#define QIF_ITIME	(1 << QIF_ITIME_B)
#define QIF_LIMITS	(QIF_BLIMITS | QIF_ILIMITS)
#define QIF_USAGE	(QIF_SPACE | QIF_INODES)
#define QIF_TIMES	(QIF_BTIME | QIF_ITIME)
#define QIF_ALL		(QIF_LIMITS | QIF_USAGE | QIF_TIMES)

struct if_dqblk {
	__u64 dqb_bhardlimit;
	__u64 dqb_bsoftlimit;
	__u64 dqb_curspace;
	__u64 dqb_ihardlimit;
	__u64 dqb_isoftlimit;
	__u64 dqb_curinodes;
	__u64 dqb_btime;
	__u64 dqb_itime;
	__u32 dqb_valid;
};

/*
 * Structure used for setting quota information about file via quotactl
 * Following flags are used to specify which fields are valid
 */
#define IIF_BGRACE	1
#define IIF_IGRACE	2
#define IIF_FLAGS	4
#define IIF_ALL		(IIF_BGRACE | IIF_IGRACE | IIF_FLAGS)

enum {
	DQF_ROOT_SQUASH_B = 0,
	DQF_SYS_FILE_B = 16,
	/* Kernel internal flags invisible to userspace */
	DQF_PRIVATE
};

/* Root squash enabled (for v1 quota format) */
#define DQF_ROOT_SQUASH	(1 << DQF_ROOT_SQUASH_B)
/* Quota stored in a system file */
#define DQF_SYS_FILE	(1 << DQF_SYS_FILE_B)

struct if_dqinfo {
	__u64 dqi_bgrace;
	__u64 dqi_igrace;
	__u32 dqi_flags;	/* DFQ_* */
	__u32 dqi_valid;
};

/*
 * Definitions for quota netlink interface
 */
#define QUOTA_NL_NOWARN 0
#define QUOTA_NL_IHARDWARN 1		/* Inode hardlimit reached */
#define QUOTA_NL_ISOFTLONGWARN 2 	/* Inode grace time expired */
#define QUOTA_NL_ISOFTWARN 3		/* Inode softlimit reached */
#define QUOTA_NL_BHARDWARN 4		/* Block hardlimit reached */
#define QUOTA_NL_BSOFTLONGWARN 5	/* Block grace time expired */
#define QUOTA_NL_BSOFTWARN 6		/* Block softlimit reached */
#define QUOTA_NL_IHARDBELOW 7		/* Usage got below inode hardlimit */
#define QUOTA_NL_ISOFTBELOW 8		/* Usage got below inode softlimit */
#define QUOTA_NL_BHARDBELOW 9		/* Usage got below block hardlimit */
#define QUOTA_NL_BSOFTBELOW 10		/* Usage got below block softlimit */

enum {
	QUOTA_NL_C_UNSPEC,
	QUOTA_NL_C_WARNING,
	__QUOTA_NL_C_MAX,
};
#define QUOTA_NL_C_MAX (__QUOTA_NL_C_MAX - 1)

enum {
	QUOTA_NL_A_UNSPEC,
	QUOTA_NL_A_QTYPE,
	QUOTA_NL_A_EXCESS_ID,
	QUOTA_NL_A_WARNING,
	QUOTA_NL_A_DEV_MAJOR,
	QUOTA_NL_A_DEV_MINOR,
	QUOTA_NL_A_CAUSED_ID,
	__QUOTA_NL_A_MAX,
};
#define QUOTA_NL_A_MAX (__QUOTA_NL_A_MAX - 1)


#endif /* _UAPI_LINUX_QUOTA_ */
