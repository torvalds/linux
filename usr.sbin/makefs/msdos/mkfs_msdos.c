/*	$OpenBSD: mkfs_msdos.c,v 1.7 2022/02/28 16:17:37 krw Exp $	*/
/*	$NetBSD: mkfs_msdos.c,v 1.10 2016/04/03 11:00:13 mlelstv Exp $	*/

/*
 * Copyright (c) 1998 Robert Nordier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>	/* powerof2 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include <util.h>

#include "makefs.h"
#include "msdos/mkfs_msdos.h"

#define MAXU16	  0xffff	/* maximum unsigned 16-bit quantity */
#define BPN	  4		/* bits per nibble */
#define NPB	  2		/* nibbles per byte */

#define DOSMAGIC  0xaa55	/* DOS magic number */
#define MINBPS	  512		/* minimum bytes per sector */
#define MAXSPC	  128		/* maximum sectors per cluster */
#define MAXNFT	  16		/* maximum number of FATs */
#define DEFBLK	  4096		/* default block size */
#define DEFBLK16  2048		/* default block size FAT16 */
#define DEFRDE	  512		/* default root directory entries */
#define RESFTE	  2		/* reserved FAT entries */
#define MINCLS12  1		/* minimum FAT12 clusters */
#define MINCLS16  0xff5		/* minimum FAT16 clusters */
#define MINCLS32  0xfff5	/* minimum FAT32 clusters */
#define MAXCLS12  0xff4		/* maximum FAT12 clusters */
#define MAXCLS16  0xfff4	/* maximum FAT16 clusters */
#define MAXCLS32  0xffffff4	/* maximum FAT32 clusters */

#define mincls(fat_type)  ((fat_type) == 12 ? MINCLS12 :	\
		      (fat_type) == 16 ? MINCLS16 :	\
				    MINCLS32)

#define maxcls(fat_type)  ((fat_type) == 12 ? MAXCLS12 :	\
		      (fat_type) == 16 ? MAXCLS16 :	\
				    MAXCLS32)

#define mk1(p, x)				\
    (p) = (u_int8_t)(x)

#define mk2(p, x)				\
    (p)[0] = (u_int8_t)(x),			\
    (p)[1] = (u_int8_t)((x) >> 010)

#define mk4(p, x)				\
    (p)[0] = (u_int8_t)(x),			\
    (p)[1] = (u_int8_t)((x) >> 010),		\
    (p)[2] = (u_int8_t)((x) >> 020),		\
    (p)[3] = (u_int8_t)((x) >> 030)

struct bs {
    u_int8_t jmp[3];		/* bootstrap entry point */
    u_int8_t oem[8];		/* OEM name and version */
};

struct bsbpb {
    u_int8_t bps[2];		/* bytes per sector */
    u_int8_t spc;		/* sectors per cluster */
    u_int8_t res[2];		/* reserved sectors */
    u_int8_t nft;		/* number of FATs */
    u_int8_t rde[2];		/* root directory entries */
    u_int8_t sec[2];		/* total sectors */
    u_int8_t mid;		/* media descriptor */
    u_int8_t spf[2];		/* sectors per FAT */
    u_int8_t spt[2];		/* sectors per track */
    u_int8_t hds[2];		/* drive heads */
    u_int8_t hid[4];		/* hidden sectors */
    u_int8_t bsec[4];		/* big total sectors */
};

struct bsxbpb {
    u_int8_t bspf[4];		/* big sectors per FAT */
    u_int8_t xflg[2];		/* FAT control flags */
    u_int8_t vers[2];		/* file system version */
    u_int8_t rdcl[4];		/* root directory start cluster */
    u_int8_t infs[2];		/* file system info sector */
    u_int8_t bkbs[2];		/* backup boot sector */
    u_int8_t rsvd[12];		/* reserved */
};

struct bsx {
    u_int8_t drv;		/* drive number */
    u_int8_t rsvd;		/* reserved */
    u_int8_t sig;		/* extended boot signature */
    u_int8_t volid[4];		/* volume ID number */
    u_int8_t label[11];		/* volume label */
    u_int8_t type[8];		/* file system type */
};

struct de {
    u_int8_t namext[11];	/* name and extension */
    u_int8_t attr;		/* attributes */
    u_int8_t rsvd[10];		/* reserved */
    u_int8_t time[2];		/* creation time */
    u_int8_t date[2];		/* creation date */
    u_int8_t clus[2];		/* starting cluster */
    u_int8_t size[4];		/* size */
};

struct bpb {
    u_int bps;			/* bytes per sector */
    u_int spc;			/* sectors per cluster */
    u_int res;			/* reserved sectors */
    u_int nft;			/* number of FATs */
    u_int rde;			/* root directory entries */
    u_int sec;			/* total sectors */
    u_int mid;			/* media descriptor */
    u_int spf;			/* sectors per FAT */
    u_int spt;			/* sectors per track */
    u_int hds;			/* drive heads */
    u_int hid;			/* hidden sectors */
    u_int bsec;			/* big total sectors */
    u_int bspf;			/* big sectors per FAT */
    u_int rdcl;			/* root directory start cluster */
    u_int infs;			/* file system info sector */
    u_int bkbs;			/* backup boot sector */
};

#define INIT(a, b, c, d, e, f, g, h, i, j) \
    { .bps = a, .spc = b, .res = c, .nft = d, .rde = e, \
      .sec = f, .mid = g, .spf = h, .spt = i, .hds = j, }
static struct {
    const char *name;
    struct bpb bpb;
} stdfmt[] = {
    {"160",  INIT(512, 1, 1, 2,  64,  320, 0xfe, 1,  8, 1)},
    {"180",  INIT(512, 1, 1, 2,  64,  360, 0xfc, 2,  9, 1)},
    {"320",  INIT(512, 2, 1, 2, 112,  640, 0xff, 1,  8, 2)},
    {"360",  INIT(512, 2, 1, 2, 112,  720, 0xfd, 2,  9, 2)},
    {"640",  INIT(512, 2, 1, 2, 112, 1280, 0xfb, 2,  8, 2)},
    {"720",  INIT(512, 2, 1, 2, 112, 1440, 0xf9, 3,  9, 2)},
    {"1200", INIT(512, 1, 1, 2, 224, 2400, 0xf9, 7, 15, 2)},
    {"1232", INIT(1024,1, 1, 2, 192, 1232, 0xfe, 2,  8, 2)},
    {"1440", INIT(512, 1, 1, 2, 224, 2880, 0xf0, 9, 18, 2)},
    {"2880", INIT(512, 2, 1, 2, 240, 5760, 0xf0, 9, 36, 2)}
};

static u_int8_t bootcode[] = {
    0xfa,			/* cli		    */
    0x31, 0xc0,			/* xor	   ax,ax    */
    0x8e, 0xd0,			/* mov	   ss,ax    */
    0xbc, 0x00, 0x7c,		/* mov	   sp,7c00h */
    0xfb,			/* sti		    */
    0x8e, 0xd8,			/* mov	   ds,ax    */
    0xe8, 0x00, 0x00,		/* call    $ + 3    */
    0x5e,			/* pop	   si	    */
    0x83, 0xc6, 0x19,		/* add	   si,+19h  */
    0xbb, 0x07, 0x00,		/* mov	   bx,0007h */
    0xfc,			/* cld		    */
    0xac,			/* lodsb	    */
    0x84, 0xc0,			/* test    al,al    */
    0x74, 0x06,			/* jz	   $ + 8    */
    0xb4, 0x0e,			/* mov	   ah,0eh   */
    0xcd, 0x10,			/* int	   10h	    */
    0xeb, 0xf5,			/* jmp	   $ - 9    */
    0x30, 0xe4,			/* xor	   ah,ah    */
    0xcd, 0x16,			/* int	   16h	    */
    0xcd, 0x19,			/* int	   19h	    */
    0x0d, 0x0a,
    'N', 'o', 'n', '-', 's', 'y', 's', 't',
    'e', 'm', ' ', 'd', 'i', 's', 'k',
    0x0d, 0x0a,
    'P', 'r', 'e', 's', 's', ' ', 'a', 'n',
    'y', ' ', 'k', 'e', 'y', ' ', 't', 'o',
    ' ', 'r', 'e', 'b', 'o', 'o', 't',
    0x0d, 0x0a,
    0
};

static int got_siginfo = 0; /* received a SIGINFO */

static int getstdfmt(const char *, struct bpb *);
static int getbpbinfo(int, const char *, const char *, int, struct bpb *, int);
static void print_bpb(struct bpb *);
static int ckgeom(const char *, u_int, const char *);
static int oklabel(const char *);
static void mklabel(u_int8_t *, const char *);
static void setstr(u_int8_t *, const char *, size_t);
static void infohandler(int sig);

int
mkfs_msdos(const char *fname, const char *dtype, const struct msdos_options *op)
{
    char buf[PATH_MAX];
    struct stat sb;
    struct timeval tv;
    struct bpb bpb;
    struct tm *tm;
    struct bs *bs;
    struct bsbpb *bsbpb;
    struct bsxbpb *bsxbpb;
    struct bsx *bsx;
    struct de *de;
    u_int8_t *img;
    const char *bname;
    ssize_t n;
    time_t now;
    u_int bss, rds, cls, dir, lsn, x, x1, x2;
    int ch, fd, fd1;
    struct msdos_options o = *op;
    int oflags = O_RDWR | O_CREAT;

    if (o.block_size && o.sectors_per_cluster) {
	warnx("Cannot specify both block size and sectors per cluster");
	return -1;
    }
    if (o.OEM_string && strlen(o.OEM_string) > 8) {
	warnx("%s: bad OEM string", o.OEM_string);
	return -1;
    }
    if (o.create_size) {
	if (o.offset == 0)
		oflags |= O_TRUNC;
	fd = open(fname, oflags, 0644);
	if (fd == -1) {
	    warnx("failed to create %s", fname);
	    return -1;
	}
	(void)lseek(fd, o.create_size - 1, SEEK_SET);
	if (write(fd, "\0", 1) != 1) {
	    warn("failed to set file size");
	    return -1;
	}
	(void)lseek(fd, 0, SEEK_SET);
    } else if ((fd = open(fname, O_RDWR)) == -1 ||
	fstat(fd, &sb)) {
	warn("%s", fname);
	return -1;
    }
    if (!S_ISCHR(sb.st_mode) && !o.create_size) {
	warnx("warning, %s is not a character device", fname);
	return -1;
    }
    if (o.offset && o.offset != lseek(fd, o.offset, SEEK_SET)) {
	warnx("cannot seek to %jd", (intmax_t)o.offset);
	return -1;
    }
    memset(&bpb, 0, sizeof(bpb));
    if (o.floppy) {
	if (getstdfmt(o.floppy, &bpb) == -1)
	    return -1;
	bpb.bsec = bpb.sec;
	bpb.sec = 0;
	bpb.bspf = bpb.spf;
	bpb.spf = 0;
    }
    if (o.drive_heads)
	bpb.hds = o.drive_heads;
    if (o.sectors_per_track)
	bpb.spt = o.sectors_per_track;
    if (o.bytes_per_sector)
	bpb.bps = o.bytes_per_sector;
    if (o.size)
	bpb.bsec = o.size;
    if (o.hidden_sectors_set)
	bpb.hid = o.hidden_sectors;
    if (!(o.floppy || (o.drive_heads && o.sectors_per_track &&
	o.bytes_per_sector && o.size && o.hidden_sectors_set))) {
	if (getbpbinfo(fd, fname, dtype, o.hidden_sectors_set, &bpb,
	    o.create_size != 0) == -1)
	    return -1;
	bpb.bsec -= (o.offset / bpb.bps);
	if (bpb.spc == 0) {     /* set defaults */
	    /* minimum cluster size */
	    switch (o.fat_type) {
	    case 12:
		bpb.spc = 1;            /* use 512 bytes */
		x = 2;                  /* up to 2MB */
		break;
	    case 16:
		bpb.spc = 1;            /* use 512 bytes */
		x = 32;                 /* up to 32MB */
		break;
	    default:
		bpb.spc = 8;            /* use 4k */
		x = 8192;               /* up to 8GB */
		break;
	    }
	    x1 = howmany(bpb.bsec, (1048576 / 512)); /* -> MB */
	    while (bpb.spc < 128 && x < x1) {
		x *= 2;
		bpb.spc *= 2;
	    }
	}
    }

    if (o.volume_label && !oklabel(o.volume_label)) {
	warnx("%s: bad volume label", o.volume_label);
	return -1;
    }

    switch (o.fat_type) {
    case 0:
	if (o.floppy)
	    o.fat_type = 12;
	else if (!o.directory_entries && (o.info_sector || o.backup_sector))
	    o.fat_type = 32;
	break;
    case 12:
    case 16:
	if (o.info_sector) {
	    warnx("Cannot specify info sector with FAT%u", o.fat_type);
	    return -1;
	}
	if (o.backup_sector) {
	    warnx("Cannot specify backup sector with FAT%u", o.fat_type);
	    return -1;
	}
	break;
    case 32:
	if (o.directory_entries) {
	    warnx("Cannot specify directory entries with FAT32");
	    return -1;
	}
	break;
    default:
	warnx("%d: bad FAT type", o.fat_type);
	return -1;
    }
    if (!powerof2(bpb.bps)) {
	warnx("bytes/sector (%u) is not a power of 2", bpb.bps);
	return -1;
    }
    if (bpb.bps < MINBPS) {
	warnx("bytes/sector (%u) is too small; minimum is %u",
	     bpb.bps, MINBPS);
	return -1;
    }

    if (o.floppy && o.fat_type == 32)
	bpb.rde = 0;
    if (o.block_size) {
	if (!powerof2(o.block_size)) {
	    warnx("block size (%u) is not a power of 2", o.block_size);
	    return -1;
	}
	if (o.block_size < bpb.bps) {
	    warnx("block size (%u) is too small; minimum is %u",
		o.block_size, bpb.bps);
	    return -1;
	}
	if (o.block_size > bpb.bps * MAXSPC) {
	    warnx("block size (%u) is too large; maximum is %u",
		o.block_size, bpb.bps * MAXSPC);
	    return -1;
	}
	bpb.spc = o.block_size / bpb.bps;
    }
    if (o.sectors_per_cluster) {
	if (!powerof2(o.sectors_per_cluster)) {
	    warnx("sectors/cluster (%u) is not a power of 2",
		o.sectors_per_cluster);
	    return -1;
	}
	bpb.spc = o.sectors_per_cluster;
    }
    if (o.reserved_sectors)
	bpb.res = o.reserved_sectors;
    if (o.num_FAT) {
	if (o.num_FAT > MAXNFT) {
	    warnx("number of FATs (%u) is too large; maximum is %u",
		 o.num_FAT, MAXNFT);
	    return -1;
	}
	bpb.nft = o.num_FAT;
    }
    if (o.directory_entries)
	bpb.rde = o.directory_entries;
    if (o.media_descriptor_set) {
	if (o.media_descriptor < 0xf0) {
	    warnx("illegal media descriptor (%#x)", o.media_descriptor);
	    return -1;
	}
	bpb.mid = o.media_descriptor;
    }
    if (o.sectors_per_fat)
	bpb.bspf = o.sectors_per_fat;
    if (o.info_sector)
	bpb.infs = o.info_sector;
    if (o.backup_sector)
	bpb.bkbs = o.backup_sector;
    bss = 1;
    bname = NULL;
    fd1 = -1;
    if (o.bootstrap) {
	bname = o.bootstrap;
	if (!strchr(bname, '/')) {
	    snprintf(buf, sizeof(buf), "/boot/%s", bname);
	    if (!(bname = strdup(buf))) {
		warn(NULL);
		return -1;
	    }
	}
	if ((fd1 = open(bname, O_RDONLY)) == -1 || fstat(fd1, &sb)) {
	    warn("%s", bname);
	    return -1;
	}
	if (!S_ISREG(sb.st_mode) || sb.st_size % bpb.bps ||
	    sb.st_size < bpb.bps || sb.st_size > bpb.bps * MAXU16) {
	    warnx("%s: inappropriate file type or format", bname);
	    return -1;
	}
	bss = sb.st_size / bpb.bps;
    }
    if (!bpb.nft)
	bpb.nft = 2;
    if (!o.fat_type) {
	if (bpb.bsec < (bpb.res ? bpb.res : bss) +
	    howmany((RESFTE + (bpb.spc ? MINCLS16 : MAXCLS12 + 1)) *
		    ((bpb.spc ? 16 : 12) / BPN), bpb.bps * NPB) *
	    bpb.nft +
	    howmany(bpb.rde ? bpb.rde : DEFRDE,
		    bpb.bps / sizeof(struct de)) +
	    (bpb.spc ? MINCLS16 : MAXCLS12 + 1) *
	    (bpb.spc ? bpb.spc : howmany(DEFBLK, bpb.bps)))
	    o.fat_type = 12;
	else if (bpb.rde || bpb.bsec <
		 (bpb.res ? bpb.res : bss) +
		 howmany((RESFTE + MAXCLS16) * 2, bpb.bps) * bpb.nft +
		 howmany(DEFRDE, bpb.bps / sizeof(struct de)) +
		 (MAXCLS16 + 1) *
		 (bpb.spc ? bpb.spc : howmany(8192, bpb.bps)))
	    o.fat_type = 16;
	else
	    o.fat_type = 32;
    }
    x = bss;
    if (o.fat_type == 32) {
	if (!bpb.infs) {
	    if (x == MAXU16 || x == bpb.bkbs) {
		warnx("no room for info sector");
		return -1;
	    }
	    bpb.infs = x;
	}
	if (bpb.infs != MAXU16 && x <= bpb.infs)
	    x = bpb.infs + 1;
	if (!bpb.bkbs) {
	    if (x == MAXU16) {
		warnx("no room for backup sector");
		return -1;
	    }
	    bpb.bkbs = x;
	} else if (bpb.bkbs != MAXU16 && bpb.bkbs == bpb.infs) {
	    warnx("backup sector would overwrite info sector");
	    return -1;
	}
	if (bpb.bkbs != MAXU16 && x <= bpb.bkbs)
	    x = bpb.bkbs + 1;
    }
    if (!bpb.res)
	bpb.res = o.fat_type == 32 ? MAXIMUM(x, MAXIMUM(16384 / bpb.bps, 4)) : x;
    else if (bpb.res < x) {
	warnx("too few reserved sectors (need %d have %d)", x, bpb.res);
	return -1;
    }
    if (o.fat_type != 32 && !bpb.rde)
	bpb.rde = DEFRDE;
    rds = howmany(bpb.rde, bpb.bps / sizeof(struct de));
    if (!bpb.spc)
	for (bpb.spc = howmany(o.fat_type == 16 ? DEFBLK16 : DEFBLK, bpb.bps);
	     bpb.spc < MAXSPC &&
	     bpb.res +
	     howmany((RESFTE + maxcls(o.fat_type)) * (o.fat_type / BPN),
		     bpb.bps * NPB) * bpb.nft +
	     rds +
	     (u_int64_t)(maxcls(o.fat_type) + 1) * bpb.spc <= bpb.bsec;
	     bpb.spc <<= 1);
    if (o.fat_type != 32 && bpb.bspf > MAXU16) {
	warnx("too many sectors/FAT for FAT12/16");
	return -1;
    }
    x1 = bpb.res + rds;
    x = bpb.bspf ? bpb.bspf : 1;
    if (x1 + (u_int64_t)x * bpb.nft > bpb.bsec) {
	warnx("meta data exceeds file system size");
	return -1;
    }
    x1 += x * bpb.nft;
    x = (u_int64_t)(bpb.bsec - x1) * bpb.bps * NPB /
	(bpb.spc * bpb.bps * NPB + o.fat_type / BPN * bpb.nft);
    x2 = howmany((RESFTE + MINIMUM(x, maxcls(o.fat_type))) * (o.fat_type / BPN),
		 bpb.bps * NPB);
    if (!bpb.bspf) {
	bpb.bspf = x2;
	x1 += (bpb.bspf - 1) * bpb.nft;
    }
    cls = (bpb.bsec - x1) / bpb.spc;
    x = (u_int64_t)bpb.bspf * bpb.bps * NPB / (o.fat_type / BPN) - RESFTE;
    if (cls > x)
	cls = x;
    if (bpb.bspf < x2) {
	warnx("warning: sectors/FAT limits file system to %u clusters",
	      cls);
	return -1;
    }
    if (cls < mincls(o.fat_type)) {
	warnx("%u clusters too few clusters for FAT%u, need %u", cls,
	    o.fat_type, mincls(o.fat_type));
	return -1;
    }
    if (cls > maxcls(o.fat_type)) {
	cls = maxcls(o.fat_type);
	bpb.bsec = x1 + (cls + 1) * bpb.spc - 1;
	warnx("warning: FAT type limits file system to %u sectors",
	      bpb.bsec);
	return -1;
    }
    printf("%s: %u sector%s in %u FAT%u cluster%s "
	   "(%u bytes/cluster)\n", fname, cls * bpb.spc,
	   cls * bpb.spc == 1 ? "" : "s", cls, o.fat_type,
	   cls == 1 ? "" : "s", bpb.bps * bpb.spc);
    if (!bpb.mid)
	bpb.mid = !bpb.hid ? 0xf0 : 0xf8;
    if (o.fat_type == 32)
	bpb.rdcl = RESFTE;
    if (bpb.hid + bpb.bsec <= MAXU16) {
	bpb.sec = bpb.bsec;
	bpb.bsec = 0;
    }
    if (o.fat_type != 32) {
	bpb.spf = bpb.bspf;
	bpb.bspf = 0;
    }
    ch = 0;
    if (o.fat_type == 12)
	ch = 1;			/* 001 Primary DOS with 12 bit FAT */
    else if (o.fat_type == 16) {
	if (bpb.bsec == 0)
	    ch = 4;		/* 004 Primary DOS with 16 bit FAT <32M */
	else
	    ch = 6;		/* 006 Primary 'big' DOS, 16-bit FAT (> 32MB) */
				/*
				 * XXX: what about:
				 * 014 DOS (16-bit FAT) - LBA
				 *  ?
				 */
    } else if (o.fat_type == 32) {
	ch = 11;		/* 011 Primary DOS with 32 bit FAT */
				/*
				 * XXX: what about:
				 * 012 Primary DOS with 32 bit FAT - LBA
				 *  ?
				 */
    }
    if (ch != 0)
	printf("MBR type: %d\n", ch);
    print_bpb(&bpb);

    gettimeofday(&tv, NULL);
    now = tv.tv_sec;
    tm = localtime(&now);
    img = emalloc(bpb.bps);
    dir = bpb.res + (bpb.spf ? bpb.spf : bpb.bspf) * bpb.nft;
    signal(SIGINFO, infohandler);
    for (lsn = 0; lsn < dir + (o.fat_type == 32 ? bpb.spc : rds); lsn++) {
        if (got_siginfo) {
            fprintf(stderr,"%s: writing sector %u of %u (%u%%)\n",
                fname,lsn,(dir + (o.fat_type == 32 ? bpb.spc : rds)),
                (lsn*100)/(dir + (o.fat_type == 32 ? bpb.spc : rds)));
            got_siginfo = 0;
        }
        x = lsn;
        if (o.bootstrap &&
            o.fat_type == 32 && bpb.bkbs != MAXU16 &&
            bss <= bpb.bkbs && x >= bpb.bkbs) {
            x -= bpb.bkbs;
            if (!x && lseek(fd1, o.offset, SEEK_SET)) {
                warn("%s", bname);
                return -1;
            }
        }
        if (o.bootstrap && x < bss) {
            if ((n = read(fd1, img, bpb.bps)) == -1) {
                warn("%s", bname);
                return -1;
            }
            if ((size_t)n != bpb.bps) {
                warnx("%s: can't read sector %u", bname, x);
                return -1;
            }
        } else
            memset(img, 0, bpb.bps);
        if (!lsn ||
          (o.fat_type == 32 && bpb.bkbs != MAXU16 && lsn == bpb.bkbs)) {
            x1 = sizeof(struct bs);
            bsbpb = (struct bsbpb *)(img + x1);
            mk2(bsbpb->bps, bpb.bps);
            mk1(bsbpb->spc, bpb.spc);
            mk2(bsbpb->res, bpb.res);
            mk1(bsbpb->nft, bpb.nft);
            mk2(bsbpb->rde, bpb.rde);
            mk2(bsbpb->sec, bpb.sec);
            mk1(bsbpb->mid, bpb.mid);
            mk2(bsbpb->spf, bpb.spf);
            mk2(bsbpb->spt, bpb.spt);
            mk2(bsbpb->hds, bpb.hds);
            mk4(bsbpb->hid, bpb.hid);
            mk4(bsbpb->bsec, bpb.bsec);
            x1 += sizeof(struct bsbpb);
            if (o.fat_type == 32) {
                bsxbpb = (struct bsxbpb *)(img + x1);
                mk4(bsxbpb->bspf, bpb.bspf);
                mk2(bsxbpb->xflg, 0);
                mk2(bsxbpb->vers, 0);
                mk4(bsxbpb->rdcl, bpb.rdcl);
                mk2(bsxbpb->infs, bpb.infs);
                mk2(bsxbpb->bkbs, bpb.bkbs);
                x1 += sizeof(struct bsxbpb);
            }
            bsx = (struct bsx *)(img + x1);
            mk1(bsx->sig, 0x29);
            if (o.volume_id_set)
                x = o.volume_id;
            else
                x = (((u_int)(1 + tm->tm_mon) << 8 |
                      (u_int)tm->tm_mday) +
                     ((u_int)tm->tm_sec << 8 |
                      (u_int)(tv.tv_usec / 10))) << 16 |
                    ((u_int)(1900 + tm->tm_year) +
                     ((u_int)tm->tm_hour << 8 |
                      (u_int)tm->tm_min));
            mk4(bsx->volid, x);
            mklabel(bsx->label, o.volume_label ? o.volume_label : "NO NAME");
            snprintf(buf, sizeof(buf), "FAT%u", o.fat_type);
            setstr(bsx->type, buf, sizeof(bsx->type));
            if (!o.bootstrap) {
                x1 += sizeof(struct bsx);
                bs = (struct bs *)img;
                mk1(bs->jmp[0], 0xeb);
                mk1(bs->jmp[1], x1 - 2);
                mk1(bs->jmp[2], 0x90);
                setstr(bs->oem, o.OEM_string ? o.OEM_string : "NetBSD",
                       sizeof(bs->oem));
                memcpy(img + x1, bootcode, sizeof(bootcode));
                mk2(img + MINBPS - 2, DOSMAGIC);
            }
        } else if (o.fat_type == 32 && bpb.infs != MAXU16 &&
                   (lsn == bpb.infs ||
                    (bpb.bkbs != MAXU16 &&
                     lsn == bpb.bkbs + bpb.infs))) {
            mk4(img, 0x41615252);
            mk4(img + MINBPS - 28, 0x61417272);
            mk4(img + MINBPS - 24, 0xffffffff);
            mk4(img + MINBPS - 20, 0xffffffff);
            mk2(img + MINBPS - 2, DOSMAGIC);
        } else if (lsn >= bpb.res && lsn < dir &&
                   !((lsn - bpb.res) %
                     (bpb.spf ? bpb.spf : bpb.bspf))) {
            mk1(img[0], bpb.mid);
            for (x = 1; x < o.fat_type * (o.fat_type == 32 ? 3U : 2U) / 8U; x++)
                mk1(img[x], o.fat_type == 32 && x % 4 == 3 ? 0x0f : 0xff);
        } else if (lsn == dir && o.volume_label) {
            de = (struct de *)img;
            mklabel(de->namext, o.volume_label);
            mk1(de->attr, 050);
            x = (u_int)tm->tm_hour << 11 |
                (u_int)tm->tm_min << 5 |
                (u_int)tm->tm_sec >> 1;
            mk2(de->time, x);
            x = (u_int)(tm->tm_year - 80) << 9 |
                (u_int)(tm->tm_mon + 1) << 5 |
                (u_int)tm->tm_mday;
            mk2(de->date, x);
        }
        if ((n = write(fd, img, bpb.bps)) == -1) {
            warn("%s", fname);
            return -1;
        }
        if ((size_t)n != bpb.bps) {
            warnx("%s: can't write sector %u", fname, lsn);
            return -1;
        }
    }
    return 0;
}


/*
 * Get a standard format.
 */
static int
getstdfmt(const char *fmt, struct bpb *bpb)
{
    u_int x, i;

    x = sizeof(stdfmt) / sizeof(stdfmt[0]);
    for (i = 0; i < x && strcmp(fmt, stdfmt[i].name); i++);
    if (i == x) {
	warnx("%s: unknown standard format", fmt);
	return -1;
    }
    *bpb = stdfmt[i].bpb;
    return 0;
}

/*
 * Get disk slice, partition, and geometry information.
 */
static int
getbpbinfo(int fd, const char *fname, const char *dtype, int iflag,
    struct bpb *bpb, int create)
{
    const char *s1, *s2;
    int part;

    part = -1;
    s1 = fname;
    if ((s2 = strrchr(s1, '/')))
	s1 = s2 + 1;
    for (s2 = s1; *s2 && !isdigit((unsigned char)*s2); s2++);
    if (!*s2 || s2 == s1)
	s2 = NULL;
    else
	while (isdigit((unsigned char)*++s2));
    s1 = s2;

    if (((part != -1) && ((!iflag && part != -1) || !bpb->bsec)) ||
	!bpb->bps || !bpb->spt || !bpb->hds) {
	u_int sector_size;
	u_int nsectors;
	u_int ntracks;
	u_int size;
	{
	    struct stat st;

	    if (fstat(fd, &st) == -1) {
		warnx("Can't get disk size for `%s'", fname);
		return -1;
	    }
	    /* create a fake geometry for a file image */
	    sector_size = 512;
	    nsectors = 63;
	    ntracks = 255;
	    size = st.st_size / sector_size;
	}
	if (!bpb->bps) {
	    if (ckgeom(fname, sector_size, "bytes/sector") == -1)
		return -1;
	    bpb->bps = sector_size;
	}

	if (nsectors > 63) {
		/*
		 * The kernel doesn't accept BPB with spt > 63.
		 * (see sys/fs/msdosfs/msdosfs_vfsops.c:msdosfs_mountfs())
		 * If values taken from disklabel don't match these
		 * restrictions, use popular BIOS default values instead.
		 */
		nsectors = 63;
	}
	if (!bpb->spt) {
	    if (ckgeom(fname, nsectors, "sectors/track") == -1)
		return -1;
	    bpb->spt = nsectors;
	}
	if (!bpb->hds) {
	    if (ckgeom(fname, ntracks, "drive heads") == -1)
		return -1;
	    bpb->hds = ntracks;
	}
	if (!bpb->bsec)
	    bpb->bsec = size;
    }
    return 0;
}

/*
 * Print out BPB values.
 */
static void
print_bpb(struct bpb *bpb)
{
    printf("bps=%u spc=%u res=%u nft=%u", bpb->bps, bpb->spc, bpb->res,
	   bpb->nft);
    if (bpb->rde)
	printf(" rde=%u", bpb->rde);
    if (bpb->sec)
	printf(" sec=%u", bpb->sec);
    printf(" mid=%#x", bpb->mid);
    if (bpb->spf)
	printf(" spf=%u", bpb->spf);
    printf(" spt=%u hds=%u hid=%u", bpb->spt, bpb->hds, bpb->hid);
    if (bpb->bsec)
	printf(" bsec=%u", bpb->bsec);
    if (!bpb->spf) {
	printf(" bspf=%u rdcl=%u", bpb->bspf, bpb->rdcl);
	printf(" infs=");
	printf(bpb->infs == MAXU16 ? "%#x" : "%u", bpb->infs);
	printf(" bkbs=");
	printf(bpb->bkbs == MAXU16 ? "%#x" : "%u", bpb->bkbs);
    }
    printf("\n");
}

/*
 * Check a disk geometry value.
 */
static int
ckgeom(const char *fname, u_int val, const char *msg)
{
    if (!val) {
	warnx("%s: no default %s", fname, msg);
	return -1;
    }
    if (val > MAXU16) {
	warnx("%s: illegal %s", fname, msg);
	return -1;
    }
    return 0;
}
/*
 * Check a volume label.
 */
static int
oklabel(const char *src)
{
    int c, i;

    for (i = 0; i <= 11; i++) {
	c = (u_char)*src++;
	if (c < ' ' + !i || strchr("\"*+,./:;<=>?[\\]|", c))
	    break;
    }
    return i && !c;
}

/*
 * Make a volume label.
 */
static void
mklabel(u_int8_t *dest, const char *src)
{
    int c, i;

    for (i = 0; i < 11; i++) {
	c = *src ? toupper((unsigned char)*src++) : ' ';
	*dest++ = !i && c == '\xe5' ? 5 : c;
    }
}

/*
 * Copy string, padding with spaces.
 */
static void
setstr(u_int8_t *dest, const char *src, size_t len)
{
    while (len--)
	*dest++ = *src ? *src++ : ' ';
}

static void
infohandler(int sig)
{
    got_siginfo = 1;
}
