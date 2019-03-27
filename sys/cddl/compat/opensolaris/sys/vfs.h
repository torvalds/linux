/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _OPENSOLARIS_SYS_VFS_H_
#define	_OPENSOLARIS_SYS_VFS_H_

#include <sys/param.h>

#ifdef _KERNEL

#include <sys/mount.h>
#include <sys/vnode.h>

#define	rootdir	rootvnode

typedef	struct mount	vfs_t;

#define	vfs_flag	mnt_flag
#define	vfs_data	mnt_data
#define	vfs_count	mnt_ref
#define	vfs_fsid	mnt_stat.f_fsid
#define	vfs_bsize	mnt_stat.f_bsize
#define	vfs_resource	mnt_stat.f_mntfromname

#define	v_flag		v_vflag
#define	v_vfsp		v_mount

#define	VFS_RDONLY	MNT_RDONLY
#define	VFS_NOSETUID	MNT_NOSUID
#define	VFS_NOEXEC	MNT_NOEXEC

#define	fs_vscan(vp, cr, async)	(0)

#define	VROOT		VV_ROOT

/*
 * Structure defining a mount option for a filesystem.
 * option names are found in mntent.h
 */
typedef struct mntopt {
	char	*mo_name;	/* option name */
	char	**mo_cancel;	/* list of options cancelled by this one */
	char	*mo_arg;	/* argument string for this option */
	int	mo_flags;	/* flags for this mount option */
	void	*mo_data;	/* filesystem specific data */
} mntopt_t;

/*
 * Flags that apply to mount options
 */

#define	MO_SET		0x01		/* option is set */
#define	MO_NODISPLAY	0x02		/* option not listed in mnttab */
#define	MO_HASVALUE	0x04		/* option takes a value */
#define	MO_IGNORE	0x08		/* option ignored by parser */
#define	MO_DEFAULT	MO_SET		/* option is on by default */
#define	MO_TAG		0x10		/* flags a tag set by user program */
#define	MO_EMPTY	0x20		/* empty space in option table */

#define	VFS_NOFORCEOPT	0x01		/* honor MO_IGNORE (don't set option) */
#define	VFS_DISPLAY	0x02		/* Turn off MO_NODISPLAY bit for opt */
#define	VFS_NODISPLAY	0x04		/* Turn on MO_NODISPLAY bit for opt */
#define	VFS_CREATEOPT	0x08		/* Create the opt if it's not there */

/*
 * Structure holding mount option strings for the mounted file system.
 */
typedef struct mntopts {
	uint_t		mo_count;		/* number of entries in table */
	mntopt_t	*mo_list;		/* list of mount options */
} mntopts_t;

void vfs_setmntopt(vfs_t *vfsp, const char *name, const char *arg,
    int flags __unused);
void vfs_clearmntopt(vfs_t *vfsp, const char *name);
int vfs_optionisset(const vfs_t *vfsp, const char *opt, char **argp);
int mount_snapshot(kthread_t *td, vnode_t **vpp, const char *fstype,
    char *fspath, char *fspec, int fsflags);

typedef	uint64_t	vfs_feature_t;

#define	VFSFT_XVATTR		0x100000001	/* Supports xvattr for attrs */
#define	VFSFT_CASEINSENSITIVE	0x100000002	/* Supports case-insensitive */
#define	VFSFT_NOCASESENSITIVE	0x100000004	/* NOT case-sensitive */
#define	VFSFT_DIRENTFLAGS	0x100000008	/* Supports dirent flags */
#define	VFSFT_ACLONCREATE	0x100000010	/* Supports ACL on create */
#define	VFSFT_ACEMASKONACCESS	0x100000020	/* Can use ACEMASK for access */
#define	VFSFT_SYSATTR_VIEWS	0x100000040	/* Supports sysattr view i/f */
#define	VFSFT_ACCESS_FILTER	0x100000080	/* dirents filtered by access */
#define	VFSFT_REPARSE		0x100000100	/* Supports reparse point */
#define	VFSFT_ZEROCOPY_SUPPORTED	0x100000200
				/* Support loaning /returning cache buffer */

#define	vfs_set_feature(vfsp, feature)		do { } while (0)
#define	vfs_clear_feature(vfsp, feature)	do { } while (0)
#define	vfs_has_feature(vfsp, feature)		(0)

#endif	/* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_VFS_H_ */
