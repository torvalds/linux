/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008-2009 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/fcntl.h>

#include <security/audit/audit.h>

#include <bsm/audit_fcntl.h>
#include <bsm/audit_record.h>

struct bsm_fcntl_cmd {
	u_short	bfc_bsm_fcntl_cmd;
	int	bfc_local_fcntl_cmd;
};
typedef struct bsm_fcntl_cmd	bsm_fcntl_cmd_t;

static const bsm_fcntl_cmd_t bsm_fcntl_cmdtab[] = {
	{ BSM_F_DUPFD, 		F_DUPFD },
	{ BSM_F_GETFD,		F_GETFD },
	{ BSM_F_SETFD,		F_SETFD	},
	{ BSM_F_GETFL,		F_GETFL },
	{ BSM_F_SETFL,		F_SETFL },
#ifdef	F_O_GETLK
	{ BSM_F_O_GETLK,	F_O_GETLK },
#endif
	{ BSM_F_SETLK,		F_SETLK },
	{ BSM_F_SETLKW,		F_SETLK },
#ifdef	F_CHFL
	{ BSM_F_CHKFL,		F_CHKFL },
#endif
#ifdef 	F_DUP2FD
	{ BSM_F_DUP2FD,		F_DUP2FD },
#endif
#ifdef	F_ALLOCSP
	{ BSM_F_ALLOCSP,	F_ALLOCSP },
#endif
#ifdef	F_FREESP
	{ BSM_F_FREESP,		F_FREESP },
#endif
#ifdef	F_ISSTREAM
	{ BSM_F_ISSTREAM,	F_ISSTREAM},
#endif
	{ BSM_F_GETLK,		F_GETLK },
#ifdef 	F_PRIV
	{ BSM_F_PRIV,		F_PRIV },
#endif
#ifdef	F_NPRIV
	{ BSM_F_NPRIV,		F_NPRIV },
#endif
#ifdef 	F_QUOTACTL
	{ BSM_F_QUOTACTL,	F_QUOTACTL },
#endif
#ifdef	F_BLOCKS
	{ BSM_F_BLOCKS,		F_BLOCKS },
#endif
#ifdef	F_BLKSIZE
	{ BSM_F_BLKSIZE,	F_BLKSIZE },
#endif
	{ BSM_F_GETOWN,		F_GETOWN },
	{ BSM_F_SETOWN,		F_SETOWN },
#ifdef	F_REVOKE
	{ BSM_F_REVOKE,		F_REVOKE },
#endif
#ifdef 	F_HASREMOTEBLOCKS
	{ BSM_F_HASREMOTEBLOCKS,
				F_HASREMOTEBLOCKS },
#endif
#ifdef 	F_FREESP
	{ BSM_F_FREESP,		F_FREESP },
#endif
#ifdef 	F_ALLOCSP
	{ BSM_F_ALLOCSP,	F_ALLOCSP },
#endif
#ifdef	F_FREESP64
	{ BSM_F_FREESP64,	F_FREESP64 },
#endif
#ifdef 	F_ALLOCSP64
	{ BSM_F_ALLOCSP64,	F_ALLOCSP64 },
#endif
#ifdef	F_GETLK64
	{ BSM_F_GETLK64, 	F_GETLK64 },
#endif
#ifdef	F_SETLK64
	{ BSM_F_SETLK64, 	F_SETLK64 },
#endif
#ifdef	F_SETLKW64
	{ BSM_F_SETLKW64, 	F_SETLKW64 },
#endif
#ifdef	F_SHARE
	{ BSM_F_SHARE,		F_SHARE },
#endif
#ifdef	F_UNSHARE
	{ BSM_F_UNSHARE,	F_UNSHARE },
#endif
#ifdef	F_SETLK_NBMAND
	{ BSM_F_SETLK_NBMAND,	F_SETLK_NBMAND },
#endif
#ifdef	F_SHARE_NBMAND
	{ BSM_F_SHARE_NBMAND,	F_SHARE_NBMAND },
#endif
#ifdef	F_SETLK64_NBMAND
	{ BSM_F_SETLK64_NBMAND,	F_SETLK64_NBMAND },
#endif
#ifdef	F_GETXFL
	{ BSM_F_GETXFL,		F_GETXFL },
#endif
#ifdef	F_BADFD
	{ BSM_F_BADFD,		F_BADFD },
#endif
#ifdef	F_OGETLK
	{ BSM_F_OGETLK,		F_OGETLK },
#endif
#ifdef	F_OSETLK
	{ BSM_F_OSETLK,		F_OSETLK },
#endif
#ifdef	F_OSETLKW
	{ BSM_F_OSETLKW,	F_OSETLKW },
#endif
#ifdef	F_SETLK_REMOTE
	{ BSM_F_SETLK_REMOTE,	F_SETLK_REMOTE },
#endif

#ifdef	F_SETSIG
	{ BSM_F_SETSIG,		F_SETSIG },
#endif
#ifdef	F_GETSIG
	{ BSM_F_GETSIG,		F_GETSIG },
#endif

#ifdef	F_CHKCLEAN
	{ BSM_F_CHKCLEAN,	F_CHKCLEAN },
#endif
#ifdef	F_PREALLOCATE
	{ BSM_F_PREALLOCATE,	F_PREALLOCATE },
#endif
#ifdef	F_SETSIZE
	{ BSM_F_SETSIZE,	F_SETSIZE },
#endif
#ifdef	F_RDADVISE
	{ BSM_F_RDADVISE,	F_RDADVISE },
#endif
#ifdef	F_RDAHEAD
	{ BSM_F_RDAHEAD,	F_RDAHEAD },
#endif
#ifdef	F_READBOOTSTRAP
	{ BSM_F_READBOOTSTRAP,	F_READBOOTSTRAP },
#endif
#ifdef	F_WRITEBOOTSTRAP
	{ BSM_F_WRITEBOOTSTRAP,	F_WRITEBOOTSTRAP },
#endif
#ifdef	F_NOCACHE
	{ BSM_F_NOCACHE,	F_NOCACHE },
#endif
#ifdef	F_LOG2PHYS
	{ BSM_F_LOG2PHYS,	F_LOG2PHYS },
#endif
#ifdef	F_GETPATH
	{ BSM_F_GETPATH,	F_GETPATH },
#endif
#ifdef	F_FULLFSYNC
	{ BSM_F_FULLFSYNC,	F_FULLFSYNC },
#endif
#ifdef	F_PATHPKG_CHECK
	{ BSM_F_PATHPKG_CHECK,	F_PATHPKG_CHECK },
#endif
#ifdef	F_FREEZE_FS
	{ BSM_F_FREEZE_FS,	F_FREEZE_FS },
#endif
#ifdef	F_THAW_FS
	{ BSM_F_THAW_FS,	F_THAW_FS },
#endif
#ifdef	F_GLOBAL_NOCACHE
	{ BSM_F_GLOBAL_NOCACHE,	F_GLOBAL_NOCACHE },
#endif
#ifdef	F_OPENFROM
	{ BSM_F_OPENFROM,	F_OPENFROM },
#endif
#ifdef	F_UNLINKFROM
	{ BSM_F_UNLINKFROM,	F_UNLINKFROM },
#endif
#ifdef	F_CHECK_OPENEVT
	{ BSM_F_CHECK_OPENEVT,	F_CHECK_OPENEVT },
#endif
#ifdef	F_ADDSIGS
	{ BSM_F_ADDSIGS,	F_ADDSIGS },
#endif
#ifdef	F_MARKDEPENDENCY
	{ BSM_F_MARKDEPENDENCY,	F_MARKDEPENDENCY },
#endif

#ifdef	FCNTL_FS_SPECIFIC_BASE
	{ BSM_F_FS_SPECIFIC_0,	FCNTL_FS_SPECIFIC_BASE},
	{ BSM_F_FS_SPECIFIC_1,	FCNTL_FS_SPECIFIC_BASE + 1},
	{ BSM_F_FS_SPECIFIC_2,	FCNTL_FS_SPECIFIC_BASE + 2},
	{ BSM_F_FS_SPECIFIC_3,	FCNTL_FS_SPECIFIC_BASE + 3},
	{ BSM_F_FS_SPECIFIC_4,	FCNTL_FS_SPECIFIC_BASE + 4},
	{ BSM_F_FS_SPECIFIC_5,	FCNTL_FS_SPECIFIC_BASE + 5},
	{ BSM_F_FS_SPECIFIC_6,	FCNTL_FS_SPECIFIC_BASE + 6},
	{ BSM_F_FS_SPECIFIC_7,	FCNTL_FS_SPECIFIC_BASE + 7},
	{ BSM_F_FS_SPECIFIC_8,	FCNTL_FS_SPECIFIC_BASE + 8},
	{ BSM_F_FS_SPECIFIC_9,	FCNTL_FS_SPECIFIC_BASE + 9},
	{ BSM_F_FS_SPECIFIC_10,	FCNTL_FS_SPECIFIC_BASE + 10},
	{ BSM_F_FS_SPECIFIC_11,	FCNTL_FS_SPECIFIC_BASE + 11},
	{ BSM_F_FS_SPECIFIC_12,	FCNTL_FS_SPECIFIC_BASE + 12},
	{ BSM_F_FS_SPECIFIC_13,	FCNTL_FS_SPECIFIC_BASE + 13},
	{ BSM_F_FS_SPECIFIC_14,	FCNTL_FS_SPECIFIC_BASE + 14},
	{ BSM_F_FS_SPECIFIC_15,	FCNTL_FS_SPECIFIC_BASE + 15},
#endif	/* FCNTL_FS_SPECIFIC_BASE */
};
static const int bsm_fcntl_cmd_count = nitems(bsm_fcntl_cmdtab);

static const bsm_fcntl_cmd_t *
bsm_lookup_local_fcntl_cmd(int local_fcntl_cmd)
{
	int i;

	for (i = 0; i < bsm_fcntl_cmd_count; i++) {
		if (bsm_fcntl_cmdtab[i].bfc_local_fcntl_cmd ==
		    local_fcntl_cmd)
			return (&bsm_fcntl_cmdtab[i]);
	}
	return (NULL);
}

u_short
au_fcntl_cmd_to_bsm(int local_fcntl_cmd)
{
	const bsm_fcntl_cmd_t *bfcp;

	bfcp = bsm_lookup_local_fcntl_cmd(local_fcntl_cmd);
	if (bfcp == NULL)
		return (BSM_F_UNKNOWN);
	return (bfcp->bfc_bsm_fcntl_cmd);
}

static const bsm_fcntl_cmd_t *
bsm_lookup_bsm_fcntl_cmd(u_short bsm_fcntl_cmd)
{
	int i;

	for (i = 0; i < bsm_fcntl_cmd_count; i++) {
		if (bsm_fcntl_cmdtab[i].bfc_bsm_fcntl_cmd ==
		    bsm_fcntl_cmd)
			return (&bsm_fcntl_cmdtab[i]);
	}
	return (NULL);
}

int
au_bsm_to_fcntl_cmd(u_short bsm_fcntl_cmd, int *local_fcntl_cmdp)
{
	const bsm_fcntl_cmd_t *bfcp;

	bfcp = bsm_lookup_bsm_fcntl_cmd(bsm_fcntl_cmd);
	if (bfcp == NULL || bfcp->bfc_local_fcntl_cmd)
		return (-1);
	*local_fcntl_cmdp = bfcp->bfc_local_fcntl_cmd;
	return (0);
}
