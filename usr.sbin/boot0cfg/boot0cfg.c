/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Luigi Rizzo
 * Copyright (c) 1999 Robert Nordier
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/disklabel.h>
#include <sys/diskmbr.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgeom.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MBRSIZE         512     /* master boot record size */

#define OFF_VERSION	0x1b0	/* offset: version number, only boot0version */
#define OFF_SERIAL	0x1b8	/* offset: volume serial number */
#define OFF_PTBL        0x1be   /* offset: partition table */
#define OFF_MAGIC       0x1fe   /* offset: magic number */
/*
 * Offsets to the parameters of the 512-byte boot block.
 * For historical reasons they are set as macros
 */
struct opt_offsets {
	int opt;
	int drive;
	int flags;
	int ticks;
};

static struct opt_offsets b0_ofs[] = {
	{ 0x0, 0x0, 0x0, 0x0 },		/* no boot block */
	{ 0x1b9, 0x1ba, 0x1bb, 0x1bc },	/* original block */
	{ 0x1b5, 0x1b6, 0x1b7, 0x1bc },	/* NT_SERIAL block */
};

static int b0_ver;	/* boot block version set by boot0bs */

#define OFF_OPT		(b0_ofs[b0_ver].opt)	/* default boot option */
#define OFF_DRIVE	(b0_ofs[b0_ver].drive)	/* setdrv drive */
#define OFF_FLAGS       (b0_ofs[b0_ver].flags)	/* option flags */
#define OFF_TICKS       (b0_ofs[b0_ver].ticks)	/* clock ticks */


#define cv2(p)  ((p)[0] | (p)[1] << 010)

#define mk2(p, x)                               \
    (p)[0] = (u_int8_t)(x),                     \
    (p)[1] = (u_int8_t)((x) >> 010)

static const struct {
    const char *tok;
    int def;
} opttbl[] = {
    {"packet", 0},
    {"update", 1},
    {"setdrv", 0}
};
static const int nopt = nitems(opttbl);

static const char fmt0[] = "#   flag     start chs   type"
    "       end chs       offset         size\n";

static const char fmt1[] = "%d   0x%02x   %4u:%3u:%2u   0x%02x"
    "   %4u:%3u:%2u   %10u   %10u\n";

static int geom_class_available(const char *);
static int read_mbr(const char *, u_int8_t **, int);
static void write_mbr(const char *, int, u_int8_t *, int, int);
static void display_mbr(u_int8_t *);
static int boot0version(const u_int8_t *);
static int boot0bs(const u_int8_t *);
static void stropt(const char *, int *, int *);
static int argtoi(const char *, int, int, int);
static int set_bell(u_int8_t *, int, int);
static void usage(void);

static unsigned vol_id[5];	/* 4 plus 1 for flag */

static int v_flag;
/*
 * Boot manager installation/configuration utility.
 */
int
main(int argc, char *argv[])
{
    u_int8_t *mbr, *boot0;
    int boot0_size, mbr_size;
    const char *bpath, *fpath;
    char *disk;
    int B_flag, o_flag;
    int d_arg, m_arg, s_arg, t_arg;
    int o_and, o_or, o_e = -1;
    int up, c;

    bpath = "/boot/boot0";
    fpath = NULL;
    B_flag = v_flag = o_flag = 0;
    d_arg = m_arg = s_arg = t_arg = -1;
    o_and = 0xff;
    o_or = 0;
    while ((c = getopt(argc, argv, "Bvb:d:e:f:i:m:o:s:t:")) != -1)
        switch (c) {
        case 'B':
            B_flag = 1;
            break;
        case 'v':
            v_flag = 1;
            break;
        case 'b':
            bpath = optarg;
            break;
        case 'd':
            d_arg = argtoi(optarg, 0, 0xff, 'd');
            break;
        case 'e':
	    if (optarg[0] == '0' && optarg[1] == 'x')
		sscanf(optarg, "0x%02x", &o_e);
	    else
		o_e = optarg[0];
            break;
        case 'f':
            fpath = optarg;
            break;
        case 'i':
            if (sscanf(optarg, "%02x%02x-%02x%02x",
		vol_id, vol_id+1, vol_id+2, vol_id+3) == 4)
			vol_id[4] = 1;
	    else
		errx(1, "bad argument %s", optarg);
            break;
        case 'm':
            m_arg = argtoi(optarg, 0, 0xf, 'm');
            break;
        case 'o':
            stropt(optarg, &o_and, &o_or);
            o_flag = 1;
            break;
        case 's':
	    if (strcasecmp(optarg, "pxe") == 0)
		s_arg = 6;
	    else
		s_arg = argtoi(optarg, 1, 6, 's');
            break;
        case 't':
            t_arg = argtoi(optarg, 1, 0xffff, 't');
            break;
        default:
            usage();
        }
    argc -= optind;
    argv += optind;
    if (argc != 1)
        usage();
    disk = g_device_path(*argv);
    if (disk == NULL)
        errx(1, "Unable to get providername for %s\n", *argv);
    up = B_flag || d_arg != -1 || m_arg != -1 || o_flag || s_arg != -1
	|| t_arg != -1;

    /* open the disk and read in the existing mbr. Either here or
     * when reading the block from disk, we do check for the version
     * and abort if a suitable block is not found.
     */
    mbr_size = read_mbr(disk, &mbr, !B_flag);

    /* save the existing MBR if we are asked to do so */
    if (fpath)
	write_mbr(fpath, O_CREAT | O_TRUNC, mbr, mbr_size, 0);

    /*
     * If we are installing the boot loader, read it from disk and copy the
     * slice table over from the existing MBR.  If not, then point boot0
     * back at the MBR we just read in.  After this, boot0 is the data to
     * write back to disk if we are going to do a write.
     */
    if (B_flag) {
	boot0_size = read_mbr(bpath, &boot0, 1);
        memcpy(boot0 + OFF_PTBL, mbr + OFF_PTBL,
	    sizeof(struct dos_partition) * NDOSPART);
	if (b0_ver == 2)	/* volume serial number support */
	    memcpy(boot0 + OFF_SERIAL, mbr + OFF_SERIAL, 4);
    } else {
	boot0 = mbr;
	boot0_size = mbr_size;
    }

    /* set the drive */
    if (d_arg != -1)
	boot0[OFF_DRIVE] = d_arg;

    /* set various flags */
    if (m_arg != -1) {
	boot0[OFF_FLAGS] &= 0xf0;
	boot0[OFF_FLAGS] |= m_arg;
    }
    if (o_flag) {
        boot0[OFF_FLAGS] &= o_and;
        boot0[OFF_FLAGS] |= o_or;
    }

    /* set the default boot selection */
    if (s_arg != -1)
        boot0[OFF_OPT] = s_arg - 1;

    /* set the timeout */
    if (t_arg != -1)
        mk2(boot0 + OFF_TICKS, t_arg);

    /* set the bell char */
    if (o_e != -1 && set_bell(boot0, o_e, 0) != -1)
	up = 1;

    if (vol_id[4]) {
	if (b0_ver != 2)
	    errx(1, "incompatible boot block, cannot set volume ID");
	boot0[OFF_SERIAL] = vol_id[0];
	boot0[OFF_SERIAL+1] = vol_id[1];
	boot0[OFF_SERIAL+2] = vol_id[2];
	boot0[OFF_SERIAL+3] = vol_id[3];
	up = 1;	/* force update */
    }
    /* write the MBR back to disk */
    if (up)
	write_mbr(disk, 0, boot0, boot0_size, vol_id[4] || b0_ver == 1);

    /* display the MBR */
    if (v_flag)
	display_mbr(boot0);

    /* clean up */
    if (mbr != boot0)
	free(boot0);
    free(mbr);
    free(disk);

    return 0;
}

/* get or set the 'bell' character to be used in case of errors.
 * Lookup for a certain code sequence, return -1 if not found.
 */
static int
set_bell(u_int8_t *mbr, int new_bell, int report)
{
    /* lookup sequence: 0x100 means skip, 0x200 means done */
    static unsigned seq[] =
		{ 0xb0, 0x100, 0xe8, 0x100, 0x100, 0x30, 0xe4, 0x200 };
    int ofs, i, c;
    for (ofs = 0x60; ofs < 0x180; ofs++) { /* search range */
	if (mbr[ofs] != seq[0])	/* search initial pattern */
	    continue;
	for (i=0;; i++) {
	    if (seq[i] == 0x200) {	/* found */
		c = mbr[ofs+1];
		if (!report)
		    mbr[ofs+1] = c = new_bell;
		else
		    printf("  bell=%c (0x%x)",
			(c >= ' ' && c < 0x7f) ? c : ' ', c);
		return c;
	    }
	    if (seq[i] != 0x100 && seq[i] != mbr[ofs+i])
		break;
	}
    }
    warn("bell not found");
    return -1;
}
/*
 * Read in the MBR of the disk.  If it is boot0, then use the version to
 * read in all of it if necessary.  Use pointers to return a malloc'd
 * buffer containing the MBR and then return its size.
 */
static int
read_mbr(const char *disk, u_int8_t **mbr, int check_version)
{
    u_int8_t buf[MBRSIZE];
    int mbr_size, fd;
    int ver;
    ssize_t n;

    if ((fd = open(disk, O_RDONLY)) == -1)
        err(1, "open %s", disk);
    if ((n = read(fd, buf, MBRSIZE)) == -1)
        err(1, "read %s", disk);
    if (n != MBRSIZE)
        errx(1, "%s: short read", disk);
    if (cv2(buf + OFF_MAGIC) != 0xaa55)
        errx(1, "%s: bad magic", disk);

    if (! (ver = boot0bs(buf))) {
	if (check_version)
	    errx(1, "%s: unknown or incompatible boot code", disk);
    } else if (boot0version(buf) == 0x101) {
	mbr_size = 1024;
	if ((*mbr = malloc(mbr_size)) == NULL)
	    errx(1, "%s: unable to allocate read buffer", disk);
	if (lseek(fd, 0, SEEK_SET) == -1 ||
	    (n = read(fd, *mbr, mbr_size)) == -1)
	    err(1, "%s", disk);
	if (n != mbr_size)
	    errx(1, "%s: short read", disk);
	close(fd);
	return (mbr_size);
    }
    if ((*mbr = malloc(sizeof(buf))) == NULL)
	errx(1, "%s: unable to allocate MBR buffer", disk);
    memcpy(*mbr, buf, sizeof(buf));
    close(fd);

    return sizeof(buf);
}

static int
geom_class_available(const char *name)
{
	struct gclass *class;
	struct gmesh mesh;
	int error;

	error = geom_gettree(&mesh);
	if (error != 0)
		errc(1, error, "Cannot get GEOM tree");

	LIST_FOREACH(class, &mesh.lg_class, lg_class) {
		if (strcmp(class->lg_name, name) == 0) {
			geom_deletetree(&mesh);
			return (1);
		}
	}

	geom_deletetree(&mesh);
	return (0);
}

/*
 * Write out the mbr to the specified file.
 */
static void
write_mbr(const char *fname, int flags, u_int8_t *mbr, int mbr_size,
    int disable_dsn)
{
	struct gctl_req *grq;
	const char *errmsg;
	char *pname;
	ssize_t n;
	int fd;

	fd = open(fname, O_WRONLY | flags, 0666);
	if (fd != -1) {
		n = write(fd, mbr, mbr_size);
		close(fd);
		if (n != mbr_size)
			errx(1, "%s: short write", fname);
		return;
	}

	/*
	 * If we're called to write to a backup file, don't try to
	 * write through GEOM.
	 */
	if (flags != 0)
		err(1, "can't open file %s to write backup", fname);

	/* Try open it read only. */
	fd = open(fname, O_RDONLY);
	if (fd == -1) {
		warn("error opening %s", fname);
		return;
	}

	pname = g_providername(fd);
	if (pname == NULL) {
		warn("error getting providername for %s", fname);
		return;
	}

	/* First check that GEOM_PART is available */
	if (geom_class_available("PART") != 0) {
		grq = gctl_get_handle();
		gctl_ro_param(grq, "class", -1, "PART");
		gctl_ro_param(grq, "arg0", -1, pname);
		gctl_ro_param(grq, "verb", -1, "bootcode");
		gctl_ro_param(grq, "bootcode", mbr_size, mbr);
		gctl_ro_param(grq, "flags", -1, "C");
		if (disable_dsn)
			gctl_ro_param(grq, "skip_dsn", sizeof(int),
			    &disable_dsn);
		errmsg = gctl_issue(grq);
		if (errmsg != NULL && errmsg[0] != '\0')
			errx(1, "GEOM_PART: write bootcode to %s failed: %s",
			    fname, errmsg);
		gctl_free(grq);
	} else if (geom_class_available("MBR") != 0) {
		grq = gctl_get_handle();
		gctl_ro_param(grq, "verb", -1, "write MBR");
		gctl_ro_param(grq, "class", -1, "MBR");
		gctl_ro_param(grq, "geom", -1, pname);
		gctl_ro_param(grq, "data", mbr_size, mbr);
		errmsg = gctl_issue(grq);
		if (errmsg != NULL)
			err(1, "GEOM_MBR: write MBR to %s failed", fname);
		gctl_free(grq);
	} else
		errx(1, "can't write MBR to %s", fname);
	free(pname);
}

/*
 * Outputs an informative dump of the data in the MBR to stdout.
 */
static void
display_mbr(u_int8_t *mbr)
{
    struct dos_partition *part;
    int i, version;

    part = (struct dos_partition *)(mbr + DOSPARTOFF);
    printf(fmt0);
    for (i = 0; i < NDOSPART; i++)
	if (part[i].dp_typ)
	    printf(fmt1, 1 + i, part[i].dp_flag,
		part[i].dp_scyl + ((part[i].dp_ssect & 0xc0) << 2),
		part[i].dp_shd, part[i].dp_ssect & 0x3f, part[i].dp_typ,
                part[i].dp_ecyl + ((part[i].dp_esect & 0xc0) << 2),
                part[i].dp_ehd, part[i].dp_esect & 0x3f, part[i].dp_start,
                part[i].dp_size);
    printf("\n");
    version = boot0version(mbr);
    printf("version=%d.%d  drive=0x%x  mask=0x%x  ticks=%u",
	version >> 8, version & 0xff, mbr[OFF_DRIVE],
	mbr[OFF_FLAGS] & 0xf, cv2(mbr + OFF_TICKS));
    set_bell(mbr, 0, 1);
    printf("\noptions=");
    for (i = 0; i < nopt; i++) {
	if (i)
	    printf(",");
	if (!(mbr[OFF_FLAGS] & 1 << (7 - i)) ^ opttbl[i].def)
	    printf("no");
	printf("%s", opttbl[i].tok);
    }
    printf("\n");
    if (b0_ver == 2)
	printf("volume serial ID %02x%02x-%02x%02x\n",
		mbr[OFF_SERIAL], mbr[OFF_SERIAL+1],
		mbr[OFF_SERIAL+2], mbr[OFF_SERIAL+3]);
    printf("default_selection=F%d (", mbr[OFF_OPT] + 1);
    if (mbr[OFF_OPT] < 4)
	printf("Slice %d", mbr[OFF_OPT] + 1);
    else if (mbr[OFF_OPT] == 4)
	printf("Drive 1");
    else
	printf("PXE");
    printf(")\n");
}

/*
 * Return the boot0 version with the minor revision in the low byte, and
 * the major revision in the next higher byte.
 */
static int
boot0version(const u_int8_t *bs)
{
    /* Check for old version, and return 0x100 if found. */
    int v = boot0bs(bs);
    if (v != 0)
        return v << 8;

    /* We have a newer boot0, so extract the version number and return it. */
    return *(const int *)(bs + OFF_VERSION) & 0xffff;
}

/* descriptor of a pattern to match.
 * Start from the first entry trying to match the chunk of bytes,
 * if you hit an entry with len=0 terminate the search and report
 * off as the version. Otherwise skip to the next block after len=0
 * An entry with len=0, off=0 is the end marker.
  */
struct byte_pattern {
    unsigned off;
    unsigned len;
    u_int8_t *key;
};

/*
 * Decide if we have valid boot0 boot code by looking for
 * characteristic byte sequences at fixed offsets.
 */
static int
boot0bs(const u_int8_t *bs)
{
    /* the initial code sequence */
    static u_int8_t id0[] = {0xfc, 0x31, 0xc0, 0x8e, 0xc0, 0x8e, 0xd8,
			     0x8e, 0xd0, 0xbc, 0x00, 0x7c };
    /* the drive id */
    static u_int8_t id1[] = {'D', 'r', 'i', 'v', 'e', ' '};
    static struct byte_pattern patterns[] = {
        {0x0,   sizeof(id0), id0},
        {0x1b2, sizeof(id1), id1},
        {1, 0, NULL},
        {0x0,   sizeof(id0), id0},	/* version with NT support */
        {0x1ae, sizeof(id1), id1},
        {2, 0, NULL},
        {0, 0, NULL},
    };
    struct byte_pattern *p = patterns;

    for (;  p->off || p->len; p++) {
	if (p->len == 0)
	    break;
	if (!memcmp(bs + p->off, p->key, p->len))	/* match */
	    continue;
	while (p->len)	/* skip to next block */
	    p++;
    }
    b0_ver = p->off;	/* XXX ugly side effect */
    return p->off;
}

/*
 * Adjust "and" and "or" masks for a -o option argument.
 */
static void
stropt(const char *arg, int *xa, int *xo)
{
    const char *q;
    char *s, *s1;
    int inv, i, x;

    if (!(s = strdup(arg)))
        err(1, NULL);
    for (s1 = s; (q = strtok(s1, ",")); s1 = NULL) {
        if ((inv = !strncmp(q, "no", 2)))
            q += 2;
        for (i = 0; i < nopt; i++)
            if (!strcmp(q, opttbl[i].tok))
                break;
        if (i == nopt)
            errx(1, "%s: Unknown -o option", q);
        if (opttbl[i].def)
            inv ^= 1;
        x = 1 << (7 - i);
        if (inv)
            *xa &= ~x;
        else
            *xo |= x;
    }
    free(s);
}

/*
 * Convert and check an option argument.
 */
static int
argtoi(const char *arg, int lo, int hi, int opt)
{
    char *s;
    long x;

    errno = 0;
    x = strtol(arg, &s, 0);
    if (errno || !*arg || *s || x < lo || x > hi)
        errx(1, "%s: Bad argument to -%c option", arg, opt);
    return x;
}

/*
 * Display usage information.
 */
static void
usage(void)
{
    fprintf(stderr, "%s\n%s\n",
    "usage: boot0cfg [-Bv] [-b boot0] [-d drive] [-f file] [-m mask]",
    "                [-o options] [-s slice] [-t ticks] disk");
    exit(1);
}
