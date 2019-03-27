/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 *
 * $FreeBSD$
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 *	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T
 *		All Rights Reserved
 */

#ifndef	_OPENSOLARIS_SYS_MNTENT_H_
#define	_OPENSOLARIS_SYS_MNTENT_H_

#include <sys/param.h>
#include_next <sys/mount.h>

#define	MNTMAXSTR	128

#define	MNTTYPE_ZFS	"zfs"		/* ZFS file system */

#define	MNTOPT_RO	"ro"		/* Read only */
#define	MNTOPT_RW	"rw"		/* Read/write */
#define	MNTOPT_NOSUID	"nosuid"	/* Neither setuid nor devices allowed */
#define	MNTOPT_DEVICES	"devices"	/* Device-special allowed */
#define	MNTOPT_NODEVICES	"nodevices"	/* Device-special disallowed */
#define	MNTOPT_SETUID	"setuid"	/* Set uid allowed */
#define	MNTOPT_NOSETUID	"nosetuid"	/* Set uid not allowed */
#define	MNTOPT_REMOUNT	"update"	/* Change mount options */
#define	MNTOPT_ATIME	"atime"		/* update atime for files */
#define	MNTOPT_NOATIME  "noatime"	/* do not update atime for files */
#define	MNTOPT_XATTR	"xattr"		/* enable extended attributes */
#define	MNTOPT_NOXATTR	"noxattr"	/* disable extended attributes */
#define	MNTOPT_EXEC	"exec"		/* enable executables */
#define	MNTOPT_NOEXEC	"noexec"	/* disable executables */
#define	MNTOPT_RESTRICT	"restrict"	/* restricted autofs mount */
#define	MNTOPT_NBMAND	"nbmand"	/* allow non-blocking mandatory locks */
#define	MNTOPT_NONBMAND	"nonbmand"	/* deny non-blocking mandatory locks */

#endif	/* !_OPENSOLARIS_MNTENT_H_ */
