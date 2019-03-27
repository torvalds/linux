/*-
 * Copyright (c) 2017 M. Warner Losh <imp@FreeBSD.org>
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
 * Forward compatibility shim to convert old stat buffer to
 * new so we can call the old system call, but return data in
 * the new system call's format.
 */
#define _WANT_FREEBSD11_STATFS
#include <sys/fcntl.h>
#include <sys/mount.h>

#define _WANT_FREEBSD11_STAT
#include <sys/stat.h>

#include <string.h>

#define INO64_FIRST 1200031

static __inline void
__stat11_to_stat(const struct freebsd11_stat *sb11, struct stat *sb)
{

	sb->st_dev = sb11->st_dev;
	sb->st_ino = sb11->st_ino;
	sb->st_nlink = sb11->st_nlink;
	sb->st_mode = sb11->st_mode;
	sb->st_uid = sb11->st_uid;
	sb->st_gid = sb11->st_gid;
	sb->st_rdev = sb11->st_rdev;
	sb->st_atim = sb11->st_atim;
	sb->st_mtim = sb11->st_mtim;
	sb->st_ctim = sb11->st_ctim;
#ifdef __STAT_TIME_T_EXT
	sb->st_atim_ext = 0;
	sb->st_mtim_ext = 0;
	sb->st_ctim_ext = 0;
	sb->st_btim_ext = 0;
#endif
	sb->st_birthtim = sb11->st_birthtim;
	sb->st_size = sb11->st_size;
	sb->st_blocks = sb11->st_blocks;
	sb->st_blksize = sb11->st_blksize;
	sb->st_flags = sb11->st_flags;
	sb->st_gen = sb11->st_gen;
	sb->st_padding0 = 0;
	sb->st_padding1 = 0;
	memset(sb->st_spare, 0, sizeof(sb->st_spare));
}

static __inline void
__statfs11_to_statfs(const struct freebsd11_statfs *sf11, struct statfs *sf)
{

	sf->f_version = STATFS_VERSION;
	sf->f_type = sf11->f_type;
	sf->f_flags = sf11->f_flags;
	sf->f_bsize = sf11->f_bsize;
	sf->f_iosize = sf11->f_iosize;
	sf->f_blocks = sf11->f_blocks;
	sf->f_bfree = sf11->f_bfree;
	sf->f_bavail = sf11->f_bavail;
	sf->f_files = sf11->f_files;
	sf->f_ffree = sf11->f_ffree;
	sf->f_syncwrites = sf11->f_syncwrites;
	sf->f_asyncwrites = sf11->f_asyncwrites;
	sf->f_syncreads = sf11->f_syncreads;
	sf->f_asyncreads = sf11->f_asyncreads;
	sf->f_namemax = sf11->f_namemax;
	sf->f_owner = sf11->f_owner;
	sf->f_fsid = sf11->f_fsid;
	memset(sf->f_spare, 0, sizeof(sf->f_spare));
	memset(sf->f_charspare, 0, sizeof(sf->f_charspare));
	strlcpy(sf->f_fstypename, sf11->f_fstypename, sizeof(sf->f_fstypename));
	strlcpy(sf->f_mntfromname, sf11->f_mntfromname, sizeof(sf->f_mntfromname));
	strlcpy(sf->f_mntonname, sf11->f_mntonname, sizeof(sf->f_mntonname));
}
