/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009 Apple Inc.
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
 *
 * $FreeBSD$
 */

#ifndef	_BSM_AUDIT_FCNTL_H_
#define	_BSM_AUDIT_FCNTL_H_

/*
 * Shared and Solaris-specific: (0-99).
 */
#define	BSM_F_DUPFD		0
#define	BSM_F_GETFD		1
#define	BSM_F_SETFD		2
#define	BSM_F_GETFL		3
#define	BSM_F_SETFL		4
#define	BSM_F_O_GETLK		5	/* Solaris-specific. */
#define	BSM_F_SETLK		6
#define	BSM_F_SETLKW		7
#define	BSM_F_CHKFL		8	/* Solaris-specific. */
#define	BSM_F_DUP2FD		9	/* FreeBSD/Solaris-specific. */
#define	BSM_F_ALLOCSP		10	/* Solaris-specific. */
#define	BSM_F_FREESP		11	/* Solaris-specific. */

#define	BSM_F_ISSTREAM		13	/* Solaris-specific. */
#define	BSM_F_GETLK		14	
#define	BSM_F_PRIV		15	/* Solaris-specific. */
#define	BSM_F_NPRIV		16	/* Solaris-specific. */
#define	BSM_F_QUOTACTL		17	/* Solaris-specific. */
#define	BSM_F_BLOCKS		18	/* Solaris-specific. */
#define	BSM_F_BLKSIZE		19	/* Solaris-specific. */

#define	BSM_F_GETOWN		23
#define	BSM_F_SETOWN		24
#define	BSM_F_REVOKE		25	/* Solaris-specific. */
#define	BSM_F_HASREMOTELOCKS	26	/* Solaris-specific. */
#define	BSM_F_FREESP64		27	/* Solaris-specific. */
#define	BSM_F_ALLOCSP64		28	/* Solaris-specific. */

#define	BSM_F_GETLK64		33	/* Solaris-specific. */
#define	BSM_F_SETLK64		34	/* Solaris-specific. */
#define	BSM_F_SETLKW64		35	/* Solaris-specific. */

#define	BSM_F_SHARE		40	/* Solaris-specific. */
#define	BSM_F_UNSHARE		41 	/* Solaris-specific. */
#define	BSM_F_SETLK_NBMAND	42	/* Solaris-specific. */
#define	BSM_F_SHARE_NBMAND	43	/* Solaris-specific. */
#define	BSM_F_SETLK64_NBMAND	44 	/* Solaris-specific. */
#define	BSM_F_GETXFL		45	/* Solaris-specific. */
#define	BSM_F_BADFD		46	/* Solaris-specific. */

/*
 * FreeBSD-specific (100-199).
 */
#define	BSM_F_OGETLK		107	/* FreeBSD-specific. */
#define	BSM_F_OSETLK		108	/* FreeBSD-specific. */
#define	BSM_F_OSETLKW		109	/* FreeBSD-specific. */

#define	BSM_F_SETLK_REMOTE	114	/* FreeBSD-specific. */

/*
 * Linux-specific (200-299).
 */
#define	BSM_F_SETSIG		210	/* Linux-specific. */
#define	BSM_F_GETSIG		211	/* Linux-specific. */

/*
 * Darwin-specific (300-399).
 */
#define	BSM_F_CHKCLEAN 		341	/* Darwin-specific. */
#define	BSM_F_PREALLOCATE	342	/* Darwin-specific. */
#define	BSM_F_SETSIZE		343	/* Darwin-specific. */
#define	BSM_F_RDADVISE		344	/* Darwin-specific. */
#define	BSM_F_RDAHEAD		345	/* Darwin-specific. */
#define	BSM_F_READBOOTSTRAP	346	/* Darwin-specific. */
#define	BSM_F_WRITEBOOTSTRAP	347	/* Darwin-specific. */
#define	BSM_F_NOCACHE		348	/* Darwin-specific. */
#define	BSM_F_LOG2PHYS		349	/* Darwin-specific. */
#define	BSM_F_GETPATH		350	/* Darwin-specific. */
#define	BSM_F_FULLFSYNC		351	/* Darwin-specific. */
#define	BSM_F_PATHPKG_CHECK	352	/* Darwin-specific. */
#define	BSM_F_FREEZE_FS		353	/* Darwin-specific. */
#define	BSM_F_THAW_FS		354	/* Darwin-specific. */
#define	BSM_F_GLOBAL_NOCACHE	355	/* Darwin-specific. */
#define	BSM_F_OPENFROM		356	/* Darwin-specific. */
#define	BSM_F_UNLINKFROM	357	/* Darwin-specific. */
#define	BSM_F_CHECK_OPENEVT	358	/* Darwin-specific. */
#define	BSM_F_ADDSIGS		359	/* Darwin-specific. */
#define	BSM_F_MARKDEPENDENCY	360	/* Darwin-specific. */

/*
 * Darwin file system specific (400-499).
 */
#define	BSM_F_FS_SPECIFIC_0	400	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_1	401	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_2	402	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_3	403	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_4	404	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_5	405	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_6	406	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_7	407	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_8	408	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_9	409	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_10	410	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_11	411	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_12	412	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_13	413	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_14	414	/* Darwin-fs-specific. */
#define	BSM_F_FS_SPECIFIC_15	415	/* Darwin-fs-specific. */


#define	BSM_F_UNKNOWN		0xFFFF	

#endif /* !_BSM_AUDIT_FCNTL_H_ */
