/*
 * Copyright (c) 1996, 1998 Robert Nordier
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Readonly filesystem for Microsoft FAT12/FAT16/FAT32 filesystems,
 * also supports VFAT.
 */

#include <sys/types.h>
#include <string.h>
#include <stddef.h>

#include "stand.h"

#include "dosfs.h"


static int	dos_open(const char *path, struct open_file *fd);
static int	dos_close(struct open_file *fd);
static int	dos_read(struct open_file *fd, void *buf, size_t size, size_t *resid);
static off_t	dos_seek(struct open_file *fd, off_t offset, int whence);
static int	dos_stat(struct open_file *fd, struct stat *sb);
static int	dos_readdir(struct open_file *fd, struct dirent *d);

struct fs_ops dosfs_fsops = {
	"dosfs",
	dos_open,
	dos_close,
	dos_read,
	null_write,
	dos_seek,
	dos_stat,
	dos_readdir
};

#define SECSIZ  512             /* sector size */
#define SSHIFT    9             /* SECSIZ shift */
#define DEPSEC   16             /* directory entries per sector */
#define DSHIFT    4             /* DEPSEC shift */
#define LOCLUS    2             /* lowest cluster number */
#define FATBLKSZ  0x20000       /* size of block in the FAT cache buffer */

/* DOS "BIOS Parameter Block" */
typedef struct {
    u_char secsiz[2];           /* sector size */
    u_char spc;                 /* sectors per cluster */
    u_char ressec[2];           /* reserved sectors */
    u_char fats;                /* FATs */
    u_char dirents[2];          /* root directory entries */
    u_char secs[2];             /* total sectors */
    u_char media;               /* media descriptor */
    u_char spf[2];              /* sectors per FAT */
    u_char spt[2];              /* sectors per track */
    u_char heads[2];            /* drive heads */
    u_char hidsec[4];           /* hidden sectors */
    u_char lsecs[4];            /* huge sectors */
    u_char lspf[4];             /* huge sectors per FAT */
    u_char xflg[2];             /* flags */
    u_char vers[2];             /* filesystem version */
    u_char rdcl[4];             /* root directory start cluster */
    u_char infs[2];             /* filesystem info sector */
    u_char bkbs[2];             /* backup boot sector */
} DOS_BPB;

/* Initial portion of DOS boot sector */
typedef struct {
    u_char jmp[3];              /* usually 80x86 'jmp' opcode */
    u_char oem[8];              /* OEM name and version */
    DOS_BPB bpb;                /* BPB */
} DOS_BS;

/* Supply missing "." and ".." root directory entries */
static const char *const dotstr[2] = {".", ".."};
static DOS_DE dot[2] = {
    {".       ", "   ", FA_DIR, {0, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
     {0, 0}, {0x21, 0}, {0, 0}, {0, 0, 0, 0}},
    {"..      ", "   ", FA_DIR, {0, 0, {0, 0}, {0, 0}, {0, 0}, {0, 0}},
     {0, 0}, {0x21, 0}, {0, 0}, {0, 0, 0, 0}}
};

/* The usual conversion macros to avoid multiplication and division */
#define bytsec(n)      ((n) >> SSHIFT)
#define secbyt(s)      ((s) << SSHIFT)
#define entsec(e)      ((e) >> DSHIFT)
#define bytblk(fs, n)  ((n) >> (fs)->bshift)
#define blkbyt(fs, b)  ((b) << (fs)->bshift)
#define secblk(fs, s)  ((s) >> ((fs)->bshift - SSHIFT))
#define blksec(fs, b)  ((b) << ((fs)->bshift - SSHIFT))

/* Convert cluster number to offset within filesystem */
#define blkoff(fs, b) (secbyt((fs)->lsndta) + blkbyt(fs, (b) - LOCLUS))

/* Convert cluster number to logical sector number */
#define blklsn(fs, b)  ((fs)->lsndta + blksec(fs, (b) - LOCLUS))

/* Convert cluster number to offset within FAT */
#define fatoff(sz, c)  ((sz) == 12 ? (c) + ((c) >> 1) :  \
                        (sz) == 16 ? (c) << 1 :          \
			(c) << 2)

/* Does cluster number reference a valid data cluster? */
#define okclus(fs, c)  ((c) >= LOCLUS && (c) <= (fs)->xclus)

/* Get start cluster from directory entry */
#define stclus(sz, de)  ((sz) != 32 ? cv2((de)->clus) :          \
                         ((u_int)cv2((de)->dex.h_clus) << 16) |  \
			 cv2((de)->clus))

static int parsebs(DOS_FS *, DOS_BS *);
static int namede(DOS_FS *, const char *, DOS_DE **);
static int lookup(DOS_FS *, u_int, const char *, DOS_DE **);
static void cp_xdnm(u_char *, DOS_XDE *);
static void cp_sfn(u_char *, DOS_DE *);
static off_t fsize(DOS_FS *, DOS_DE *);
static int fatcnt(DOS_FS *, u_int);
static int fatget(DOS_FS *, u_int *);
static int fatend(u_int, u_int);
static int ioread(DOS_FS *, u_int, void *, size_t);
static int ioget(struct open_file *, daddr_t, void *, size_t);

static int
dos_read_fatblk(DOS_FS *fs, struct open_file *fd, u_int blknum)
{
    int err;
    size_t io_size;
    daddr_t offset_in_fat, max_offset_in_fat;

    offset_in_fat = ((daddr_t)blknum) * FATBLKSZ;
    max_offset_in_fat = secbyt(fs->spf);
    io_size = FATBLKSZ;
    if (offset_in_fat > max_offset_in_fat)
        offset_in_fat = max_offset_in_fat;
    if (offset_in_fat + io_size > max_offset_in_fat)
        io_size = ((size_t)(max_offset_in_fat - offset_in_fat));

    if (io_size != 0) {
        err = ioget(fd, fs->lsnfat + bytsec(offset_in_fat),
            fs->fatbuf, io_size);
        if (err != 0) {
            fs->fatbuf_blknum = ((u_int)(-1));
            return (err);
        }
    }
    if (io_size < FATBLKSZ)
        memset(fs->fatbuf + io_size, 0, FATBLKSZ - io_size);

    fs->fatbuf_blknum = blknum;
    return (0);
}

/*
 * Mount DOS filesystem
 */
static int
dos_mount(DOS_FS *fs, struct open_file *fd)
{
    int err;
    u_char *buf;

    bzero(fs, sizeof(DOS_FS));
    fs->fd = fd;

    if ((buf = malloc(secbyt(1))) == NULL)
        return (errno);
    if ((err = ioget(fs->fd, 0, buf, secbyt(1))) ||
        (err = parsebs(fs, (DOS_BS *)buf))) {
        free(buf);
        return (err);
    }
    free(buf);

    if ((fs->fatbuf = malloc(FATBLKSZ)) == NULL)
        return (errno);
    err = dos_read_fatblk(fs, fd, 0);
    if (err != 0) {
        free(fs->fatbuf);
        return (err);
    }

    fs->root = dot[0];
    fs->root.name[0] = ' ';
    if (fs->fatsz == 32) {
        fs->root.clus[0] = fs->rdcl & 0xff;
        fs->root.clus[1] = (fs->rdcl >> 8) & 0xff;
        fs->root.dex.h_clus[0] = (fs->rdcl >> 16) & 0xff;
        fs->root.dex.h_clus[1] = (fs->rdcl >> 24) & 0xff;
    }
    return (0);
}

/*
 * Unmount mounted filesystem
 */
static int
dos_unmount(DOS_FS *fs)
{
    if (fs->links)
        return (EBUSY);
    free(fs->fatbuf);
    free(fs);
    return (0);
}

/*
 * Open DOS file
 */
static int
dos_open(const char *path, struct open_file *fd)
{
    DOS_DE *de;
    DOS_FILE *f;
    DOS_FS *fs;
    u_int size, clus;
    int err;

    /* Allocate mount structure, associate with open */
    if ((fs = malloc(sizeof(DOS_FS))) == NULL)
        return (errno);
    if ((err = dos_mount(fs, fd))) {
        free(fs);
        return (err);
    }

    if ((err = namede(fs, path, &de))) {
        dos_unmount(fs);
        return (err);
    }

    clus = stclus(fs->fatsz, de);
    size = cv4(de->size);

    if ((!(de->attr & FA_DIR) && (!clus != !size)) ||
	((de->attr & FA_DIR) && size) ||
	(clus && !okclus(fs, clus))) {
        dos_unmount(fs);
        return (EINVAL);
    }
    if ((f = malloc(sizeof(DOS_FILE))) == NULL) {
        err = errno;
        dos_unmount(fs);
        return (err);
    }
    bzero(f, sizeof(DOS_FILE));
    f->fs = fs;
    fs->links++;
    f->de = *de;
    fd->f_fsdata = (void *)f;
    return (0);
}

/*
 * Read from file
 */
static int
dos_read(struct open_file *fd, void *buf, size_t nbyte, size_t *resid)
{
    off_t size;
    u_int nb, off, clus, c, cnt, n;
    DOS_FILE *f = (DOS_FILE *)fd->f_fsdata;
    int err = 0;

    /*
     * as ioget() can be called *a lot*, use twiddle here.
     * also 4 seems to be good value not to slow loading down too much:
     * with 270MB file (~540k ioget() calls, twiddle can easily waste 4-5sec.
     */
    twiddle(4);
    nb = (u_int)nbyte;
    if ((size = fsize(f->fs, &f->de)) == -1)
	return (EINVAL);
    if (nb > (n = size - f->offset))
	nb = n;
    off = f->offset;
    if ((clus = stclus(f->fs->fatsz, &f->de)))
	off &= f->fs->bsize - 1;
    c = f->c;
    cnt = nb;
    while (cnt) {
	n = 0;
	if (!c) {
	    if ((c = clus))
		n = bytblk(f->fs, f->offset);
	} else if (!off)
	    n++;
	while (n--) {
	    if ((err = fatget(f->fs, &c)))
		goto out;
	    if (!okclus(f->fs, c)) {
		err = EINVAL;
		goto out;
	    }
	}
	if (!clus || (n = f->fs->bsize - off) > cnt)
	    n = cnt;
	if ((err = ioread(f->fs, (c ? blkoff(f->fs, c) :
				      secbyt(f->fs->lsndir)) + off, buf, n)))
	    goto out;
	f->offset += n;
	f->c = c;
	off = 0;
	buf = (char *)buf + n;
	cnt -= n;
    }
 out:
    if (resid)
	*resid = nbyte - nb + cnt;
    return (err);
}

/*
 * Reposition within file
 */
static off_t
dos_seek(struct open_file *fd, off_t offset, int whence)
{
    off_t off;
    u_int size;
    DOS_FILE *f = (DOS_FILE *)fd->f_fsdata;

    size = cv4(f->de.size);
    switch (whence) {
    case SEEK_SET:
        off = 0;
        break;
    case SEEK_CUR:
        off = f->offset;
        break;
    case SEEK_END:
        off = size;
        break;
    default:
	errno = EINVAL;
	return (-1);
    }
    off += offset;
    if (off < 0 || off > size) {
	errno = EINVAL;
        return (-1);
    }
    f->offset = (u_int)off;
    f->c = 0;
    return (off);
}

/*
 * Close open file
 */
static int
dos_close(struct open_file *fd)
{
    DOS_FILE *f = (DOS_FILE *)fd->f_fsdata;
    DOS_FS *fs = f->fs;

    f->fs->links--;
    free(f);
    dos_unmount(fs);
    return (0);
}

/*
 * Return some stat information on a file.
 */
static int
dos_stat(struct open_file *fd, struct stat *sb)
{
    DOS_FILE *f = (DOS_FILE *)fd->f_fsdata;

    /* only important stuff */
    sb->st_mode = f->de.attr & FA_DIR ? S_IFDIR | 0555 : S_IFREG | 0444;
    sb->st_nlink = 1;
    sb->st_uid = 0;
    sb->st_gid = 0;
    if ((sb->st_size = fsize(f->fs, &f->de)) == -1)
	return (EINVAL);
    return (0);
}

static int
dos_checksum(unsigned char *name, unsigned char *ext)
{
    int x, i;
    char buf[11];

    bcopy(name, buf, 8);
    bcopy(ext, buf+8, 3);
    x = 0;
    for (i = 0; i < 11; i++) {
	x = ((x & 1) << 7) | (x >> 1);
	x += buf[i];
	x &= 0xff;
    }
    return (x);
}

static int
dos_readdir(struct open_file *fd, struct dirent *d)
{
    /* DOS_FILE *f = (DOS_FILE *)fd->f_fsdata; */
    u_char fn[261];
    DOS_DIR dd;
    size_t res;
    u_int chk, x, xdn;
    int err;

    x = chk = 0;
    while (1) {
	xdn = x;
	x = 0;
	err = dos_read(fd, &dd, sizeof(dd), &res);
	if (err)
	    return (err);
	if (res == sizeof(dd))
	    return (ENOENT);
	if (dd.de.name[0] == 0)
	    return (ENOENT);

	/* Skip deleted entries */
	if (dd.de.name[0] == 0xe5)
	    continue;

	/* Check if directory entry is volume label */
	if (dd.de.attr & FA_LABEL) {
	    /* 
	     * If volume label set, check if the current entry is
	     * extended entry (FA_XDE) for long file names.
	     */
	    if ((dd.de.attr & FA_MASK) == FA_XDE) {
		/*
		 * Read through all following extended entries
		 * to get the long file name. 0x40 marks the
		 * last entry containing part of long file name.
		 */
		if (dd.xde.seq & 0x40)
		    chk = dd.xde.chk;
		else if (dd.xde.seq != xdn - 1 || dd.xde.chk != chk)
		    continue;
		x = dd.xde.seq & ~0x40;
		if (x < 1 || x > 20) {
		    x = 0;
		    continue;
		}
		cp_xdnm(fn, &dd.xde);
	    } else {
		/* skip only volume label entries */
		continue;
	    }
	} else {
	    if (xdn == 1) {
		x = dos_checksum(dd.de.name, dd.de.ext);
		if (x == chk)
		    break;
	    } else {
		cp_sfn(fn, &dd.de);
		break;
	    }
	    x = 0;
	}
    }

    d->d_fileno = (dd.de.clus[1] << 8) + dd.de.clus[0];
    d->d_reclen = sizeof(*d);
    d->d_type = (dd.de.attr & FA_DIR) ? DT_DIR : DT_REG;
    memcpy(d->d_name, fn, sizeof(d->d_name));
    return (0);
}

/*
 * Parse DOS boot sector
 */
static int
parsebs(DOS_FS *fs, DOS_BS *bs)
{
    u_int sc;

    if ((bs->jmp[0] != 0x69 &&
         bs->jmp[0] != 0xe9 &&
         (bs->jmp[0] != 0xeb || bs->jmp[2] != 0x90)) ||
        bs->bpb.media < 0xf0)
        return (EINVAL);
    if (cv2(bs->bpb.secsiz) != SECSIZ)
        return (EINVAL);
    if (!(fs->spc = bs->bpb.spc) || fs->spc & (fs->spc - 1))
        return (EINVAL);
    fs->bsize = secbyt(fs->spc);
    fs->bshift = ffs(fs->bsize) - 1;
    if ((fs->spf = cv2(bs->bpb.spf))) {
        if (bs->bpb.fats != 2)
            return (EINVAL);
        if (!(fs->dirents = cv2(bs->bpb.dirents)))
            return (EINVAL);
    } else {
        if (!(fs->spf = cv4(bs->bpb.lspf)))
            return (EINVAL);
        if (!bs->bpb.fats || bs->bpb.fats > 16)
            return (EINVAL);
        if ((fs->rdcl = cv4(bs->bpb.rdcl)) < LOCLUS)
            return (EINVAL);
    }
    if (!(fs->lsnfat = cv2(bs->bpb.ressec)))
        return (EINVAL);
    fs->lsndir = fs->lsnfat + fs->spf * bs->bpb.fats;
    fs->lsndta = fs->lsndir + entsec(fs->dirents);
    if (!(sc = cv2(bs->bpb.secs)) && !(sc = cv4(bs->bpb.lsecs)))
        return (EINVAL);
    if (fs->lsndta > sc)
        return (EINVAL);
    if ((fs->xclus = secblk(fs, sc - fs->lsndta) + 1) < LOCLUS)
        return (EINVAL);
    fs->fatsz = fs->dirents ? fs->xclus < 0xff6 ? 12 : 16 : 32;
    sc = (secbyt(fs->spf) << 1) / (fs->fatsz >> 2) - 1;
    if (fs->xclus > sc)
        fs->xclus = sc;
    return (0);
}

/*
 * Return directory entry from path
 */
static int
namede(DOS_FS *fs, const char *path, DOS_DE **dep)
{
    char name[256];
    DOS_DE *de;
    char *s;
    size_t n;
    int err;

    err = 0;
    de = &fs->root;
    while (*path) {
        while (*path == '/')
            path++;
        if (*path == '\0')
            break;
        if (!(s = strchr(path, '/')))
            s = strchr(path, 0);
        if ((n = s - path) > 255)
            return (ENAMETOOLONG);
        memcpy(name, path, n);
        name[n] = 0;
        path = s;
        if (!(de->attr & FA_DIR))
            return (ENOTDIR);
        if ((err = lookup(fs, stclus(fs->fatsz, de), name, &de)))
            return (err);
    }
    *dep = de;
    return (0);
}

/*
 * Lookup path segment
 */
static int
lookup(DOS_FS *fs, u_int clus, const char *name, DOS_DE **dep)
{
    static DOS_DIR dir[DEPSEC];
    u_char lfn[261];
    u_char sfn[13];
    u_int nsec, lsec, xdn, chk, sec, ent, x;
    int err, ok;

    if (!clus)
        for (ent = 0; ent < 2; ent++)
            if (!strcasecmp(name, dotstr[ent])) {
                *dep = dot + ent;
                return (0);
            }
    if (!clus && fs->fatsz == 32)
        clus = fs->rdcl;
    nsec = !clus ? entsec(fs->dirents) : fs->spc;
    lsec = 0;
    xdn = chk = 0;
    for (;;) {
        if (!clus && !lsec)
            lsec = fs->lsndir;
        else if (okclus(fs, clus))
            lsec = blklsn(fs, clus);
        else
            return (EINVAL);
        for (sec = 0; sec < nsec; sec++) {
            if ((err = ioget(fs->fd, lsec + sec, dir, secbyt(1))))
                return (err);
            for (ent = 0; ent < DEPSEC; ent++) {
                if (!*dir[ent].de.name)
                    return (ENOENT);
                if (*dir[ent].de.name != 0xe5) {
                    if ((dir[ent].de.attr & FA_MASK) == FA_XDE) {
                        x = dir[ent].xde.seq;
                        if (x & 0x40 || (x + 1 == xdn &&
                                         dir[ent].xde.chk == chk)) {
                            if (x & 0x40) {
                                chk = dir[ent].xde.chk;
                                x &= ~0x40;
                            }
                            if (x >= 1 && x <= 20) {
                                cp_xdnm(lfn, &dir[ent].xde);
                                xdn = x;
                                continue;
                            }
                        }
                    } else if (!(dir[ent].de.attr & FA_LABEL)) {
                        if ((ok = xdn == 1)) {
			    x = dos_checksum(dir[ent].de.name, dir[ent].de.ext);
                            ok = chk == x &&
                                !strcasecmp(name, (const char *)lfn);
                        }
                        if (!ok) {
                            cp_sfn(sfn, &dir[ent].de);
                            ok = !strcasecmp(name, (const char *)sfn);
                        }
                        if (ok) {
                            *dep = &dir[ent].de;
                            return (0);
                        }
                    }
		}
                xdn = 0;
            }
        }
        if (!clus)
            break;
        if ((err = fatget(fs, &clus)))
            return (err);
        if (fatend(fs->fatsz, clus))
            break;
    }
    return (ENOENT);
}

/*
 * Copy name from extended directory entry
 */
static void
cp_xdnm(u_char *lfn, DOS_XDE *xde)
{
    static struct {
        u_int off;
        u_int dim;
    } ix[3] = {
        {offsetof(DOS_XDE, name1), sizeof(xde->name1) / 2},
        {offsetof(DOS_XDE, name2), sizeof(xde->name2) / 2},
        {offsetof(DOS_XDE, name3), sizeof(xde->name3) / 2}
    };
    u_char *p;
    u_int n, x, c;

    lfn += 13 * ((xde->seq & ~0x40) - 1);
    for (n = 0; n < 3; n++)
        for (p = (u_char *)xde + ix[n].off, x = ix[n].dim; x;
	     p += 2, x--) {
            if ((c = cv2(p)) && (c < 32 || c > 127))
                c = '?';
            if (!(*lfn++ = c))
                return;
        }
    if (xde->seq & 0x40)
        *lfn = 0;
}

/*
 * Copy short filename
 */
static void
cp_sfn(u_char *sfn, DOS_DE *de)
{
    u_char *p;
    int j, i;

    p = sfn;
    if (*de->name != ' ') {
        for (j = 7; de->name[j] == ' '; j--);
        for (i = 0; i <= j; i++)
            *p++ = de->name[i];
        if (*de->ext != ' ') {
            *p++ = '.';
            for (j = 2; de->ext[j] == ' '; j--);
            for (i = 0; i <= j; i++)
                *p++ = de->ext[i];
        }
    }
    *p = 0;
    if (*sfn == 5)
        *sfn = 0xe5;
}

/*
 * Return size of file in bytes
 */
static off_t
fsize(DOS_FS *fs, DOS_DE *de)
{
   u_long size;
   u_int c;
   int n;

   if (!(size = cv4(de->size)) && de->attr & FA_DIR) {
      if (!(c = cv2(de->clus)))
         size = fs->dirents * sizeof(DOS_DE);
      else {
         if ((n = fatcnt(fs, c)) == -1)
            return (n);
         size = blkbyt(fs, n);
      }
   }
   return (size);
}

/*
 * Count number of clusters in chain
 */
static int
fatcnt(DOS_FS *fs, u_int c)
{
   int n;

   for (n = 0; okclus(fs, c); n++)
      if (fatget(fs, &c))
	  return (-1);
   return (fatend(fs->fatsz, c) ? n : -1);
}

/*
 * Get next cluster in cluster chain. Use in core fat cache unless
 * the number of current 128K block in FAT has changed.
 */
static int
fatget(DOS_FS *fs, u_int *c)
{
    u_int val_in, val_out, offset, blknum, nbyte;
    const u_char *p_entry;
    int err;

    /* check input value to prevent overflow in fatoff() */
    val_in = *c;
    if (val_in & 0xf0000000)
        return (EINVAL);

    /* ensure that current 128K FAT block is cached */
    offset = fatoff(fs->fatsz, val_in);
    nbyte = fs->fatsz != 32 ? 2 : 4;
    if (offset + nbyte > secbyt(fs->spf))
        return (EINVAL);
    blknum = offset / FATBLKSZ;
    offset %= FATBLKSZ;
    if (offset + nbyte > FATBLKSZ)
        return (EINVAL);
    if (blknum != fs->fatbuf_blknum) {
        err = dos_read_fatblk(fs, fs->fd, blknum);
        if (err != 0)
            return (err);
    }
    p_entry = fs->fatbuf + offset;

    /* extract cluster number from FAT entry */
    switch (fs->fatsz) {
    case 32:
        val_out = cv4(p_entry);
        val_out &= 0x0fffffff;
        break;
    case 16:
        val_out = cv2(p_entry);
        break;
    case 12:
        val_out = cv2(p_entry);
        if (val_in & 1)
            val_out >>= 4;
        else
            val_out &= 0xfff;
        break;
    default:
        return (EINVAL);
    }
    *c = val_out;
    return (0);
}

/*
 * Is cluster an end-of-chain marker?
 */
static int
fatend(u_int sz, u_int c)
{
    return (c > (sz == 12 ? 0xff7U : sz == 16 ? 0xfff7U : 0xffffff7));
}

/*
 * Offset-based I/O primitive
 */
static int
ioread(DOS_FS *fs, u_int offset, void *buf, size_t nbyte)
{
    char *s;
    u_int off, n;
    int err;
    u_char local_buf[SECSIZ];

    s = buf;
    if ((off = offset & (SECSIZ - 1))) {
        offset -= off;
        if ((n = SECSIZ - off) > nbyte)
            n = nbyte;
        if ((err = ioget(fs->fd, bytsec(offset), local_buf, sizeof(local_buf))))
            return (err);
	memcpy(s, local_buf + off, n);
        offset += SECSIZ;
        s += n;
        nbyte -= n;
    }
    n = nbyte & (SECSIZ - 1);
    if (nbyte -= n) {
        if ((err = ioget(fs->fd, bytsec(offset), s, nbyte)))
            return (err);
        offset += nbyte;
        s += nbyte;
    }
    if (n) {
        if ((err = ioget(fs->fd, bytsec(offset), local_buf, sizeof(local_buf))))
            return (err);
	memcpy(s, local_buf, n);
    }
    return (0);
}

/*
 * Sector-based I/O primitive
 */
static int
ioget(struct open_file *fd, daddr_t lsec, void *buf, size_t size)
{
    size_t rsize;
    int rv;

    /* Make sure we get full read or error. */
    rsize = 0;
    rv = (fd->f_dev->dv_strategy)(fd->f_devdata, F_READ, lsec,
        size, buf, &rsize);
    if ((rv == 0) && (size != rsize))
        rv = EIO;
    return (rv);
}
