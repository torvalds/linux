/*-
 * Copyright (c) 2002 McAfee, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Marshall
 * Kirk McKusick and McAfee Research,, the Security Research Division of
 * McAfee, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as
 * part of the DARPA CHATS research program
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
 */
/*-
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are freely
 * permitted provided that the above copyright notice and this
 * paragraph and the following disclaimer are duplicated in all
 * such forms.
 *
 * This software is provided "AS IS" and without any express or
 * implied warranties, including, without limitation, the implied
 * warranties of merchantability and fitness for a particular
 * purpose.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <ufs/ffs/fs.h>

#ifdef UFS_SMALL_CGBASE
/* XXX: Revert to old (broken for over 1.5Tb filesystems) version of cgbase
   (see sys/ufs/ffs/fs.h rev 1.39) so that small boot loaders (e.g. boot2) can
   support both UFS1 and UFS2. */
#undef cgbase
#define cgbase(fs, c)   ((ufs2_daddr_t)((fs)->fs_fpg * (c)))
#endif

typedef	uint32_t	ufs_ino_t;

/*
 * We use 4k `virtual' blocks for filesystem data, whatever the actual
 * filesystem block size. FFS blocks are always a multiple of 4k.
 */
#define VBLKSHIFT	12
#define VBLKSIZE	(1 << VBLKSHIFT)
#define VBLKMASK	(VBLKSIZE - 1)
#define DBPERVBLK	(VBLKSIZE / DEV_BSIZE)
#define INDIRPERVBLK(fs) (NINDIR(fs) / ((fs)->fs_bsize >> VBLKSHIFT))
#define IPERVBLK(fs)	(INOPB(fs) / ((fs)->fs_bsize >> VBLKSHIFT))
#define INO_TO_VBA(fs, ipervblk, x) \
    (fsbtodb(fs, cgimin(fs, ino_to_cg(fs, x))) + \
    (((x) % (fs)->fs_ipg) / (ipervblk) * DBPERVBLK))
#define INO_TO_VBO(ipervblk, x) ((x) % ipervblk)
#define FS_TO_VBA(fs, fsb, off) (fsbtodb(fs, fsb) + \
    ((off) / VBLKSIZE) * DBPERVBLK)
#define FS_TO_VBO(fs, fsb, off) ((off) & VBLKMASK)

/* Buffers that must not span a 64k boundary. */
struct dmadat {
	char blkbuf[VBLKSIZE];	/* filesystem blocks */
	char indbuf[VBLKSIZE];	/* indir blocks */
	char sbbuf[SBLOCKSIZE];	/* superblock */
	char secbuf[DEV_BSIZE];	/* for MBR/disklabel */
};
static struct dmadat *dmadat;

static ufs_ino_t lookup(const char *);
static ssize_t fsread(ufs_ino_t, void *, size_t);

static uint8_t ls, dsk_meta;
static uint32_t fs_off;

static __inline uint8_t
fsfind(const char *name, ufs_ino_t * ino)
{
	static char buf[DEV_BSIZE];
	static struct direct d;
	char *s;
	ssize_t n;

	fs_off = 0;
	while ((n = fsread(*ino, buf, DEV_BSIZE)) > 0)
		for (s = buf; s < buf + DEV_BSIZE;) {
			memcpy(&d, s, sizeof(struct direct));
			if (ls)
				printf("%s ", d.d_name);
			else if (!strcmp(name, d.d_name)) {
				*ino = d.d_ino;
				return d.d_type;
			}
			s += d.d_reclen;
		}
	if (n != -1 && ls)
		printf("\n");
	return 0;
}

static ufs_ino_t
lookup(const char *path)
{
	static char name[UFS_MAXNAMLEN + 1];
	const char *s;
	ufs_ino_t ino;
	ssize_t n;
	uint8_t dt;

	ino = UFS_ROOTINO;
	dt = DT_DIR;
	for (;;) {
		if (*path == '/')
			path++;
		if (!*path)
			break;
		for (s = path; *s && *s != '/'; s++);
		if ((n = s - path) > UFS_MAXNAMLEN)
			return 0;
		ls = *path == '?' && n == 1 && !*s;
		memcpy(name, path, n);
		name[n] = 0;
		if (dt != DT_DIR) {
			printf("%s: not a directory.\n", name);
			return (0);
		}
		if ((dt = fsfind(name, &ino)) <= 0)
			break;
		path = s;
	}
	return dt == DT_REG ? ino : 0;
}

/*
 * Possible superblock locations ordered from most to least likely.
 */
static int sblock_try[] = SBLOCKSEARCH;

#if defined(UFS2_ONLY)
#define DIP(field) dp2.field
#elif defined(UFS1_ONLY)
#define DIP(field) dp1.field
#else
#define DIP(field) fs.fs_magic == FS_UFS1_MAGIC ? dp1.field : dp2.field
#endif

static ssize_t
fsread_size(ufs_ino_t inode, void *buf, size_t nbyte, size_t *fsizep)
{
#ifndef UFS2_ONLY
	static struct ufs1_dinode dp1;
	ufs1_daddr_t addr1;
#endif
#ifndef UFS1_ONLY
	static struct ufs2_dinode dp2;
#endif
	static struct fs fs;
	static ufs_ino_t inomap;
	char *blkbuf;
	void *indbuf;
	char *s;
	size_t n, nb, size, off, vboff;
	ufs_lbn_t lbn;
	ufs2_daddr_t addr2, vbaddr;
	static ufs2_daddr_t blkmap, indmap;
	u_int u;

	/* Basic parameter validation. */
	if ((buf == NULL && nbyte != 0) || dmadat == NULL)
		return (-1);

	blkbuf = dmadat->blkbuf;
	indbuf = dmadat->indbuf;

	/*
	 * Force probe if inode is zero to ensure we have a valid fs, otherwise
	 * when probing multiple paritions, reads from subsequent parititions
	 * will incorrectly succeed.
	 */
	if (!dsk_meta || inode == 0) {
		inomap = 0;
		dsk_meta = 0;
		for (n = 0; sblock_try[n] != -1; n++) {
			if (dskread(dmadat->sbbuf, sblock_try[n] / DEV_BSIZE,
			    SBLOCKSIZE / DEV_BSIZE))
				return -1;
			memcpy(&fs, dmadat->sbbuf, sizeof(struct fs));
			if ((
#if defined(UFS1_ONLY)
			    fs.fs_magic == FS_UFS1_MAGIC
#elif defined(UFS2_ONLY)
			    (fs.fs_magic == FS_UFS2_MAGIC &&
			    fs.fs_sblockloc == sblock_try[n])
#else
			    fs.fs_magic == FS_UFS1_MAGIC ||
			    (fs.fs_magic == FS_UFS2_MAGIC &&
			    fs.fs_sblockloc == sblock_try[n])
#endif
			    ) &&
			    fs.fs_bsize <= MAXBSIZE &&
			    fs.fs_bsize >= (int32_t)sizeof(struct fs))
				break;
		}
		if (sblock_try[n] == -1) {
			return -1;
		}
		dsk_meta++;
	} else
		memcpy(&fs, dmadat->sbbuf, sizeof(struct fs));
	if (!inode)
		return 0;
	if (inomap != inode) {
		n = IPERVBLK(&fs);
		if (dskread(blkbuf, INO_TO_VBA(&fs, n, inode), DBPERVBLK))
			return -1;
		n = INO_TO_VBO(n, inode);
#if defined(UFS1_ONLY)
		memcpy(&dp1, (struct ufs1_dinode *)(void *)blkbuf + n,
		    sizeof(dp1));
#elif defined(UFS2_ONLY)
		memcpy(&dp2, (struct ufs2_dinode *)(void *)blkbuf + n,
		    sizeof(dp2));
#else
		if (fs.fs_magic == FS_UFS1_MAGIC)
			memcpy(&dp1, (struct ufs1_dinode *)(void *)blkbuf + n,
			    sizeof(dp1));
		else
			memcpy(&dp2, (struct ufs2_dinode *)(void *)blkbuf + n,
			    sizeof(dp2));
#endif
		inomap = inode;
		fs_off = 0;
		blkmap = indmap = 0;
	}
	s = buf;
	size = DIP(di_size);
	n = size - fs_off;
	if (nbyte > n)
		nbyte = n;
	nb = nbyte;
	while (nb) {
		lbn = lblkno(&fs, fs_off);
		off = blkoff(&fs, fs_off);
		if (lbn < UFS_NDADDR) {
			addr2 = DIP(di_db[lbn]);
		} else if (lbn < UFS_NDADDR + NINDIR(&fs)) {
			n = INDIRPERVBLK(&fs);
			addr2 = DIP(di_ib[0]);
			u = (u_int)(lbn - UFS_NDADDR) / n * DBPERVBLK;
			vbaddr = fsbtodb(&fs, addr2) + u;
			if (indmap != vbaddr) {
				if (dskread(indbuf, vbaddr, DBPERVBLK))
					return -1;
				indmap = vbaddr;
			}
			n = (lbn - UFS_NDADDR) & (n - 1);
#if defined(UFS1_ONLY)
			memcpy(&addr1, (ufs1_daddr_t *)indbuf + n,
			    sizeof(ufs1_daddr_t));
			addr2 = addr1;
#elif defined(UFS2_ONLY)
			memcpy(&addr2, (ufs2_daddr_t *)indbuf + n,
			    sizeof(ufs2_daddr_t));
#else
			if (fs.fs_magic == FS_UFS1_MAGIC) {
				memcpy(&addr1, (ufs1_daddr_t *)indbuf + n,
				    sizeof(ufs1_daddr_t));
				addr2 = addr1;
			} else
				memcpy(&addr2, (ufs2_daddr_t *)indbuf + n,
				    sizeof(ufs2_daddr_t));
#endif
		} else
			return -1;
		vbaddr = fsbtodb(&fs, addr2) + (off >> VBLKSHIFT) * DBPERVBLK;
		vboff = off & VBLKMASK;
		n = sblksize(&fs, (off_t)size, lbn) - (off & ~VBLKMASK);
		if (n > VBLKSIZE)
			n = VBLKSIZE;
		if (blkmap != vbaddr) {
			if (dskread(blkbuf, vbaddr, n >> DEV_BSHIFT))
				return -1;
			blkmap = vbaddr;
		}
		n -= vboff;
		if (n > nb)
			n = nb;
		memcpy(s, blkbuf + vboff, n);
		s += n;
		fs_off += n;
		nb -= n;
	}

	if (fsizep != NULL)
		*fsizep = size;

	return nbyte;
}

static ssize_t
fsread(ufs_ino_t inode, void *buf, size_t nbyte)
{

	return fsread_size(inode, buf, nbyte, NULL);
}

