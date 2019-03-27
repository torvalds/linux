/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_EXTDIRENT_H
#define	_SYS_EXTDIRENT_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

#if defined(_KERNEL)

/*
 * Extended file-system independent directory entry.  This style of
 * dirent provides additional informational flag bits for each
 * directory entry.  This dirent will be returned instead of the
 * standard dirent if a VOP_READDIR() requests dirent flags via
 * V_RDDIR_ENTFLAGS, and if the file system supports the flags.
 */
typedef struct edirent {
	ino64_t		ed_ino;		/* "inode number" of entry */
	off64_t		ed_off;		/* offset of disk directory entry */
	uint32_t	ed_eflags;	/* per-entry flags */
	unsigned short	ed_reclen;	/* length of this record */
	char		ed_name[1];	/* name of file */
} edirent_t;

#define	EDIRENT_RECLEN(namelen)	\
	((offsetof(edirent_t, ed_name[0]) + 1 + (namelen) + 7) & ~ 7)
#define	EDIRENT_NAMELEN(reclen)	\
	((reclen) - (offsetof(edirent_t, ed_name[0])))

/*
 * Extended entry flags
 *	Extended entries include a bitfield of extra information
 *	regarding that entry.
 */
#define	ED_CASE_CONFLICT  0x10  /* Disconsidering case, entry is not unique */

/*
 * Extended flags accessor function
 */
#define	ED_CASE_CONFLICTS(x)	((x)->ed_eflags & ED_CASE_CONFLICT)

#endif /* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EXTDIRENT_H */
