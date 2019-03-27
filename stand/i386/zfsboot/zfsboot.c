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

#include "stand.h"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/diskmbr.h>
#ifdef GPT
#include <sys/gpt.h>
#endif
#include <sys/reboot.h>
#include <sys/queue.h>

#include <machine/bootinfo.h>
#include <machine/elf.h>
#include <machine/pc/bios.h>

#include <stdarg.h>
#include <stddef.h>

#include <a.out.h>

#include <btxv86.h>

#include "lib.h"
#include "rbx.h"
#include "drv.h"
#include "edd.h"
#include "cons.h"
#include "bootargs.h"
#include "paths.h"

#include "libzfs.h"

#define ARGS			0x900
#define NOPT			14
#define NDEV			3

#define BIOS_NUMDRIVES		0x475
#define DRV_HARD		0x80
#define DRV_MASK		0x7f

#define TYPE_AD			0
#define TYPE_DA			1
#define TYPE_MAXHARD		TYPE_DA
#define TYPE_FD			2

#define DEV_GELIBOOT_BSIZE	4096

extern uint32_t _end;

#ifdef GPT
static const uuid_t freebsd_zfs_uuid = GPT_ENT_TYPE_FREEBSD_ZFS;
#endif
static const char optstr[NOPT] = "DhaCcdgmnpqrsv"; /* Also 'P', 'S' */
static const unsigned char flags[NOPT] = {
    RBX_DUAL,
    RBX_SERIAL,
    RBX_ASKNAME,
    RBX_CDROM,
    RBX_CONFIG,
    RBX_KDB,
    RBX_GDB,
    RBX_MUTE,
    RBX_NOINTR,
    RBX_PAUSE,
    RBX_QUIET,
    RBX_DFLTROOT,
    RBX_SINGLE,
    RBX_VERBOSE
};
uint32_t opts;

static const unsigned char dev_maj[NDEV] = {30, 4, 2};

static char cmd[512];
static char cmddup[512];
static char kname[1024];
static char rootname[256];
static int comspeed = SIOSPD;
static struct bootinfo bootinfo;
static uint32_t bootdev;
static struct zfs_boot_args zfsargs;

vm_offset_t	high_heap_base;
uint32_t	bios_basemem, bios_extmem, high_heap_size;

static struct bios_smap smap;

/*
 * The minimum amount of memory to reserve in bios_extmem for the heap.
 */
#define	HEAP_MIN		(64 * 1024 * 1024)

static char *heap_next;
static char *heap_end;

/* Buffers that must not span a 64k boundary. */
#define READ_BUF_SIZE		8192
struct dmadat {
	char rdbuf[READ_BUF_SIZE];	/* for reading large things */
	char secbuf[READ_BUF_SIZE];	/* for MBR/disklabel */
};
static struct dmadat *dmadat;

void exit(int);
void reboot(void);
static void load(void);
static int parse_cmd(void);
static void bios_getmem(void);
int main(void);

#ifdef LOADER_GELI_SUPPORT
#include "geliboot.h"
static char gelipw[GELI_PW_MAXLEN];
#endif

struct zfsdsk {
	struct dsk       dsk;
#ifdef LOADER_GELI_SUPPORT
	struct geli_dev *gdev;
#endif
};

#include "zfsimpl.c"

/*
 * Read from a dnode (which must be from a ZPL filesystem).
 */
static int
zfs_read(spa_t *spa, const dnode_phys_t *dnode, off_t *offp, void *start, size_t size)
{
	const znode_phys_t *zp = (const znode_phys_t *) dnode->dn_bonus;
	size_t n;
	int rc;

	n = size;
	if (*offp + n > zp->zp_size)
		n = zp->zp_size - *offp;

	rc = dnode_read(spa, dnode, *offp, start, n);
	if (rc)
		return (-1);
	*offp += n;

	return (n);
}

/*
 * Current ZFS pool
 */
static spa_t *spa;
static spa_t *primary_spa;
static vdev_t *primary_vdev;

/*
 * A wrapper for dskread that doesn't have to worry about whether the
 * buffer pointer crosses a 64k boundary.
 */
static int
vdev_read(void *xvdev, void *priv, off_t off, void *buf, size_t bytes)
{
	char *p;
	daddr_t lba, alignlba;
	off_t diff;
	unsigned int nb, alignnb;
	struct zfsdsk *zdsk = (struct zfsdsk *) priv;

	if ((off & (DEV_BSIZE - 1)) || (bytes & (DEV_BSIZE - 1)))
		return -1;

	p = buf;
	lba = off / DEV_BSIZE;
	lba += zdsk->dsk.start;
	/*
	 * Align reads to 4k else 4k sector GELIs will not decrypt.
	 * Round LBA down to nearest multiple of DEV_GELIBOOT_BSIZE bytes.
	 */
	alignlba = rounddown2(off, DEV_GELIBOOT_BSIZE) / DEV_BSIZE;
	/*
	 * The read must be aligned to DEV_GELIBOOT_BSIZE bytes relative to the
	 * start of the GELI partition, not the start of the actual disk.
	 */
	alignlba += zdsk->dsk.start;
	diff = (lba - alignlba) * DEV_BSIZE;

	while (bytes > 0) {
		nb = bytes / DEV_BSIZE;
		/*
		 * Ensure that the read size plus the leading offset does not
		 * exceed the size of the read buffer.
		 */
		if (nb > (READ_BUF_SIZE - diff) / DEV_BSIZE)
			nb = (READ_BUF_SIZE - diff) / DEV_BSIZE;
		/*
		 * Round the number of blocks to read up to the nearest multiple
		 * of DEV_GELIBOOT_BSIZE.
		 */
		alignnb = roundup2(nb * DEV_BSIZE + diff, DEV_GELIBOOT_BSIZE)
		    / DEV_BSIZE;

		if (zdsk->dsk.size > 0 && alignlba + alignnb >
		    zdsk->dsk.size + zdsk->dsk.start) {
			printf("Shortening read at %lld from %d to %lld\n",
			    alignlba, alignnb,
			    (zdsk->dsk.size + zdsk->dsk.start) - alignlba);
			alignnb = (zdsk->dsk.size + zdsk->dsk.start) - alignlba;
		}

		if (drvread(&zdsk->dsk, dmadat->rdbuf, alignlba, alignnb))
			return -1;
#ifdef LOADER_GELI_SUPPORT
		/* decrypt */
		if (zdsk->gdev != NULL) {
			if (geli_read(zdsk->gdev, ((alignlba - zdsk->dsk.start) *
			    DEV_BSIZE), dmadat->rdbuf, alignnb * DEV_BSIZE))
				return (-1);
		}
#endif
		memcpy(p, dmadat->rdbuf + diff, nb * DEV_BSIZE);
		p += nb * DEV_BSIZE;
		lba += nb;
		alignlba += alignnb;
		bytes -= nb * DEV_BSIZE;
		/* Don't need the leading offset after the first block. */
		diff = 0;
	}

	return 0;
}
/* Match the signature exactly due to signature madness */
static int
vdev_read2(vdev_t *vdev, void *priv, off_t off, void *buf, size_t bytes)
{
	return vdev_read(vdev, priv, off, buf, bytes);
}


static int
vdev_write(vdev_t *vdev, void *priv, off_t off, void *buf, size_t bytes)
{
	char *p;
	daddr_t lba;
	unsigned int nb;
	struct zfsdsk *zdsk = (struct zfsdsk *) priv;

	if ((off & (DEV_BSIZE - 1)) || (bytes & (DEV_BSIZE - 1)))
		return -1;

	p = buf;
	lba = off / DEV_BSIZE;
	lba += zdsk->dsk.start;
	while (bytes > 0) {
		nb = bytes / DEV_BSIZE;
		if (nb > READ_BUF_SIZE / DEV_BSIZE)
			nb = READ_BUF_SIZE / DEV_BSIZE;
		memcpy(dmadat->rdbuf, p, nb * DEV_BSIZE);
		if (drvwrite(&zdsk->dsk, dmadat->rdbuf, lba, nb))
			return -1;
		p += nb * DEV_BSIZE;
		lba += nb;
		bytes -= nb * DEV_BSIZE;
	}

	return 0;
}

static int
xfsread(const dnode_phys_t *dnode, off_t *offp, void *buf, size_t nbyte)
{
    if ((size_t)zfs_read(spa, dnode, offp, buf, nbyte) != nbyte) {
	printf("Invalid format\n");
	return -1;
    }
    return 0;
}

/*
 * Read Pad2 (formerly "Boot Block Header") area of the first
 * vdev label of the given vdev.
 */
static int
vdev_read_pad2(vdev_t *vdev, char *buf, size_t size)
{
	blkptr_t bp;
	char *tmp = zap_scratch;
	off_t off = offsetof(vdev_label_t, vl_pad2);

	if (size > VDEV_PAD_SIZE)
		size = VDEV_PAD_SIZE;

	BP_ZERO(&bp);
	BP_SET_LSIZE(&bp, VDEV_PAD_SIZE);
	BP_SET_PSIZE(&bp, VDEV_PAD_SIZE);
	BP_SET_CHECKSUM(&bp, ZIO_CHECKSUM_LABEL);
	BP_SET_COMPRESS(&bp, ZIO_COMPRESS_OFF);
	DVA_SET_OFFSET(BP_IDENTITY(&bp), off);
	if (vdev_read_phys(vdev, &bp, tmp, off, 0))
		return (EIO);
	memcpy(buf, tmp, size);
	return (0);
}

static int
vdev_clear_pad2(vdev_t *vdev)
{
	char *zeroes = zap_scratch;
	uint64_t *end;
	off_t off = offsetof(vdev_label_t, vl_pad2);

	memset(zeroes, 0, VDEV_PAD_SIZE);
	end = (uint64_t *)(zeroes + VDEV_PAD_SIZE);
	/* ZIO_CHECKSUM_LABEL magic and pre-calcualted checksum for all zeros */
	end[-5] = 0x0210da7ab10c7a11;
	end[-4] = 0x97f48f807f6e2a3f;
	end[-3] = 0xaf909f1658aacefc;
	end[-2] = 0xcbd1ea57ff6db48b;
	end[-1] = 0x6ec692db0d465fab;
	if (vdev_write(vdev, vdev->v_read_priv, off, zeroes, VDEV_PAD_SIZE))
		return (EIO);
	return (0);
}

static void
bios_getmem(void)
{
    uint64_t size;

    /* Parse system memory map */
    v86.ebx = 0;
    do {
	v86.ctl = V86_FLAGS;
	v86.addr = 0x15;		/* int 0x15 function 0xe820*/
	v86.eax = 0xe820;
	v86.ecx = sizeof(struct bios_smap);
	v86.edx = SMAP_SIG;
	v86.es = VTOPSEG(&smap);
	v86.edi = VTOPOFF(&smap);
	v86int();
	if (V86_CY(v86.efl) || (v86.eax != SMAP_SIG))
	    break;
	/* look for a low-memory segment that's large enough */
	if ((smap.type == SMAP_TYPE_MEMORY) && (smap.base == 0) &&
	    (smap.length >= (512 * 1024)))
	    bios_basemem = smap.length;
	/* look for the first segment in 'extended' memory */
	if ((smap.type == SMAP_TYPE_MEMORY) && (smap.base == 0x100000)) {
	    bios_extmem = smap.length;
	}

	/*
	 * Look for the largest segment in 'extended' memory beyond
	 * 1MB but below 4GB.
	 */
	if ((smap.type == SMAP_TYPE_MEMORY) && (smap.base > 0x100000) &&
	    (smap.base < 0x100000000ull)) {
	    size = smap.length;

	    /*
	     * If this segment crosses the 4GB boundary, truncate it.
	     */
	    if (smap.base + size > 0x100000000ull)
		size = 0x100000000ull - smap.base;

	    if (size > high_heap_size) {
		high_heap_size = size;
		high_heap_base = smap.base;
	    }
	}
    } while (v86.ebx != 0);

    /* Fall back to the old compatibility function for base memory */
    if (bios_basemem == 0) {
	v86.ctl = 0;
	v86.addr = 0x12;		/* int 0x12 */
	v86int();
	
	bios_basemem = (v86.eax & 0xffff) * 1024;
    }

    /* Fall back through several compatibility functions for extended memory */
    if (bios_extmem == 0) {
	v86.ctl = V86_FLAGS;
	v86.addr = 0x15;		/* int 0x15 function 0xe801*/
	v86.eax = 0xe801;
	v86int();
	if (!V86_CY(v86.efl)) {
	    bios_extmem = ((v86.ecx & 0xffff) + ((v86.edx & 0xffff) * 64)) * 1024;
	}
    }
    if (bios_extmem == 0) {
	v86.ctl = 0;
	v86.addr = 0x15;		/* int 0x15 function 0x88*/
	v86.eax = 0x8800;
	v86int();
	bios_extmem = (v86.eax & 0xffff) * 1024;
    }

    /*
     * If we have extended memory and did not find a suitable heap
     * region in the SMAP, use the last 3MB of 'extended' memory as a
     * high heap candidate.
     */
    if (bios_extmem >= HEAP_MIN && high_heap_size < HEAP_MIN) {
	high_heap_size = HEAP_MIN;
	high_heap_base = bios_extmem + 0x100000 - HEAP_MIN;
    }
}

/*
 * Try to detect a device supported by the legacy int13 BIOS
 */
static int
int13probe(int drive)
{
    v86.ctl = V86_FLAGS;
    v86.addr = 0x13;
    v86.eax = 0x800;
    v86.edx = drive;
    v86int();
    
    if (!V86_CY(v86.efl) &&				/* carry clear */
	((v86.edx & 0xff) != (drive & DRV_MASK))) {	/* unit # OK */
	if ((v86.ecx & 0x3f) == 0) {			/* absurd sector size */
		return(0);				/* skip device */
	}
	return (1);
    }
    return(0);
}

/*
 * We call this when we find a ZFS vdev - ZFS consumes the dsk
 * structure so we must make a new one.
 */
static struct zfsdsk *
copy_dsk(struct zfsdsk *zdsk)
{
    struct zfsdsk *newdsk;

    newdsk = malloc(sizeof(struct zfsdsk));
    *newdsk = *zdsk;
    return (newdsk);
}

/*
 * Get disk size from eax=0x800 and 0x4800. We need to probe both
 * because 0x4800 may not be available and we would like to get more
 * or less correct disk size - if it is possible at all.
 * Note we do not really want to touch drv.c because that code is shared
 * with boot2 and we can not afford to grow that code.
 */
static uint64_t
drvsize_ext(struct zfsdsk *zdsk)
{
	struct dsk *dskp;
	uint64_t size, tmp;
	int cyl, hds, sec;

	dskp = &zdsk->dsk;

	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x800;
	v86.edx = dskp->drive;
	v86int();

	/* Don't error out if we get bad sector number, try EDD as well */
	if (V86_CY(v86.efl) ||	/* carry set */
	    (v86.edx & 0xff) <= (unsigned)(dskp->drive & 0x7f)) /* unit # bad */
		return (0);
	cyl = ((v86.ecx & 0xc0) << 2) + ((v86.ecx & 0xff00) >> 8) + 1;
	/* Convert max head # -> # of heads */
	hds = ((v86.edx & 0xff00) >> 8) + 1;
	sec = v86.ecx & 0x3f;

	size = (uint64_t)cyl * hds * sec;

	/* Determine if we can use EDD with this device. */
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x4100;
	v86.edx = dskp->drive;
	v86.ebx = 0x55aa;
	v86int();
	if (V86_CY(v86.efl) ||  /* carry set */
	    (v86.ebx & 0xffff) != 0xaa55 || /* signature */
	    (v86.ecx & EDD_INTERFACE_FIXED_DISK) == 0)
		return (size);

	tmp = drvsize(dskp);
	if (tmp > size)
		size = tmp;

	return (size);
}

/*
 * The "layered" ioctl to read disk/partition size. Unfortunately
 * the zfsboot case is hardest, because we do not have full software
 * stack available, so we need to do some manual work here.
 */
uint64_t
ldi_get_size(void *priv)
{
	struct zfsdsk *zdsk = priv;
	uint64_t size = zdsk->dsk.size;

	if (zdsk->dsk.start == 0)
		size = drvsize_ext(zdsk);

	return (size * DEV_BSIZE);
}

static void
probe_drive(struct zfsdsk *zdsk)
{
#ifdef GPT
    struct gpt_hdr hdr;
    struct gpt_ent *ent;
    unsigned part, entries_per_sec;
    daddr_t slba;
#endif
#if defined(GPT) || defined(LOADER_GELI_SUPPORT)
    daddr_t elba;
#endif

    struct dos_partition *dp;
    char *sec;
    unsigned i;

#ifdef LOADER_GELI_SUPPORT
    /*
     * Taste the disk, if it is GELI encrypted, decrypt it then dig out the
     * partition table and probe each slice/partition in turn for a vdev or
     * GELI encrypted vdev.
     */
    elba = drvsize_ext(zdsk);
    if (elba > 0) {
	elba--;
    }
    zdsk->gdev = geli_taste(vdev_read, zdsk, elba, "disk%u:0:");
    if ((zdsk->gdev != NULL) && (geli_havekey(zdsk->gdev) == 0))
	    geli_passphrase(zdsk->gdev, gelipw);
#endif /* LOADER_GELI_SUPPORT */

    sec = dmadat->secbuf;
    zdsk->dsk.start = 0;

#ifdef GPT
    /*
     * First check for GPT.
     */
    if (drvread(&zdsk->dsk, sec, 1, 1)) {
	return;
    }
    memcpy(&hdr, sec, sizeof(hdr));
    if (memcmp(hdr.hdr_sig, GPT_HDR_SIG, sizeof(hdr.hdr_sig)) != 0 ||
	hdr.hdr_lba_self != 1 || hdr.hdr_revision < 0x00010000 ||
	hdr.hdr_entsz < sizeof(*ent) || DEV_BSIZE % hdr.hdr_entsz != 0) {
	goto trymbr;
    }

    /*
     * Probe all GPT partitions for the presence of ZFS pools. We
     * return the spa_t for the first we find (if requested). This
     * will have the effect of booting from the first pool on the
     * disk.
     *
     * If no vdev is found, GELI decrypting the device and try again
     */
    entries_per_sec = DEV_BSIZE / hdr.hdr_entsz;
    slba = hdr.hdr_lba_table;
    elba = slba + hdr.hdr_entries / entries_per_sec;
    while (slba < elba) {
	zdsk->dsk.start = 0;
	if (drvread(&zdsk->dsk, sec, slba, 1))
	    return;
	for (part = 0; part < entries_per_sec; part++) {
	    ent = (struct gpt_ent *)(sec + part * hdr.hdr_entsz);
	    if (memcmp(&ent->ent_type, &freebsd_zfs_uuid,
		     sizeof(uuid_t)) == 0) {
		zdsk->dsk.start = ent->ent_lba_start;
		zdsk->dsk.size = ent->ent_lba_end - ent->ent_lba_start + 1;
		zdsk->dsk.slice = part + 1;
		zdsk->dsk.part = 255;
		if (vdev_probe(vdev_read2, zdsk, NULL) == 0) {
		    /*
		     * This slice had a vdev. We need a new dsk
		     * structure now since the vdev now owns this one.
		     */
		    zdsk = copy_dsk(zdsk);
		}
#ifdef LOADER_GELI_SUPPORT
		else if ((zdsk->gdev = geli_taste(vdev_read, zdsk,
		    ent->ent_lba_end - ent->ent_lba_start, "disk%up%u:",
		    zdsk->dsk.unit, zdsk->dsk.slice)) != NULL) {
		    if (geli_havekey(zdsk->gdev) == 0 ||
			geli_passphrase(zdsk->gdev, gelipw) == 0) {
			/*
			 * This slice has GELI, check it for ZFS.
			 */
			if (vdev_probe(vdev_read2, zdsk, NULL) == 0) {
			    /*
			     * This slice had a vdev. We need a new dsk
			     * structure now since the vdev now owns this one.
			     */
			    zdsk = copy_dsk(zdsk);
			}
			break;
		    }
		}
#endif /* LOADER_GELI_SUPPORT */
	    }
	}
	slba++;
    }
    return;
trymbr:
#endif /* GPT */

    if (drvread(&zdsk->dsk, sec, DOSBBSECTOR, 1))
	return;
    dp = (void *)(sec + DOSPARTOFF);

    for (i = 0; i < NDOSPART; i++) {
	if (!dp[i].dp_typ)
	    continue;
	zdsk->dsk.start = dp[i].dp_start;
	zdsk->dsk.size = dp[i].dp_size;
	zdsk->dsk.slice = i + 1;
	if (vdev_probe(vdev_read2, zdsk, NULL) == 0) {
	    zdsk = copy_dsk(zdsk);
	}
#ifdef LOADER_GELI_SUPPORT
	else if ((zdsk->gdev = geli_taste(vdev_read, zdsk, dp[i].dp_size -
		 dp[i].dp_start, "disk%us%u:")) != NULL) {
	    if (geli_havekey(zdsk->gdev) == 0 ||
		geli_passphrase(zdsk->gdev, gelipw) == 0) {
		/*
		 * This slice has GELI, check it for ZFS.
		 */
		if (vdev_probe(vdev_read2, zdsk, NULL) == 0) {
		    /*
		     * This slice had a vdev. We need a new dsk
		     * structure now since the vdev now owns this one.
		     */
		    zdsk = copy_dsk(zdsk);
		}
		break;
	    }
	}
#endif /* LOADER_GELI_SUPPORT */
    }
}

int
main(void)
{
    dnode_phys_t dn;
    off_t off;
    struct zfsdsk *zdsk;
    int autoboot, i;
    int nextboot;
    int rc;

    dmadat = (void *)(roundup2(__base + (int32_t)&_end, 0x10000) - __base);

    bios_getmem();

    if (high_heap_size > 0) {
	heap_end = PTOV(high_heap_base + high_heap_size);
	heap_next = PTOV(high_heap_base);
    } else {
	heap_next = (char *)dmadat + sizeof(*dmadat);
	heap_end = (char *)PTOV(bios_basemem);
    }
    setheap(heap_next, heap_end);

    zdsk = calloc(1, sizeof(struct zfsdsk));
    zdsk->dsk.drive = *(uint8_t *)PTOV(ARGS);
    zdsk->dsk.type = zdsk->dsk.drive & DRV_HARD ? TYPE_AD : TYPE_FD;
    zdsk->dsk.unit = zdsk->dsk.drive & DRV_MASK;
    zdsk->dsk.slice = *(uint8_t *)PTOV(ARGS + 1) + 1;
    zdsk->dsk.part = 0;
    zdsk->dsk.start = 0;
    zdsk->dsk.size = drvsize_ext(zdsk);

    bootinfo.bi_version = BOOTINFO_VERSION;
    bootinfo.bi_size = sizeof(bootinfo);
    bootinfo.bi_basemem = bios_basemem / 1024;
    bootinfo.bi_extmem = bios_extmem / 1024;
    bootinfo.bi_memsizes_valid++;
    bootinfo.bi_bios_dev = zdsk->dsk.drive;

    bootdev = MAKEBOOTDEV(dev_maj[zdsk->dsk.type],
			  zdsk->dsk.slice, zdsk->dsk.unit, zdsk->dsk.part);

    /* Process configuration file */

    autoboot = 1;

    zfs_init();

    /*
     * Probe the boot drive first - we will try to boot from whatever
     * pool we find on that drive.
     */
    probe_drive(zdsk);

    /*
     * Probe the rest of the drives that the bios knows about. This
     * will find any other available pools and it may fill in missing
     * vdevs for the boot pool.
     */
#ifndef VIRTUALBOX
    for (i = 0; i < *(unsigned char *)PTOV(BIOS_NUMDRIVES); i++)
#else
    for (i = 0; i < MAXBDDEV; i++)
#endif
    {
	if ((i | DRV_HARD) == *(uint8_t *)PTOV(ARGS))
	    continue;

	if (!int13probe(i | DRV_HARD))
	    break;

	zdsk = calloc(1, sizeof(struct zfsdsk));
	zdsk->dsk.drive = i | DRV_HARD;
	zdsk->dsk.type = zdsk->dsk.drive & TYPE_AD;
	zdsk->dsk.unit = i;
	zdsk->dsk.slice = 0;
	zdsk->dsk.part = 0;
	zdsk->dsk.start = 0;
	zdsk->dsk.size = drvsize_ext(zdsk);
	probe_drive(zdsk);
    }

    /*
     * The first discovered pool, if any, is the pool.
     */
    spa = spa_get_primary();
    if (!spa) {
	printf("%s: No ZFS pools located, can't boot\n", BOOTPROG);
	for (;;)
	    ;
    }

    primary_spa = spa;
    primary_vdev = spa_get_primary_vdev(spa);

    nextboot = 0;
    rc  = vdev_read_pad2(primary_vdev, cmd, sizeof(cmd));
    if (vdev_clear_pad2(primary_vdev))
	printf("failed to clear pad2 area of primary vdev\n");
    if (rc == 0) {
	if (*cmd) {
	    /*
	     * We could find an old-style ZFS Boot Block header here.
	     * Simply ignore it.
	     */
	    if (*(uint64_t *)cmd != 0x2f5b007b10c) {
		/*
		 * Note that parse() is destructive to cmd[] and we also want
		 * to honor RBX_QUIET option that could be present in cmd[].
		 */
		nextboot = 1;
		memcpy(cmddup, cmd, sizeof(cmd));
		if (parse_cmd()) {
		    printf("failed to parse pad2 area of primary vdev\n");
		    reboot();
		}
		if (!OPT_CHECK(RBX_QUIET))
		    printf("zfs nextboot: %s\n", cmddup);
	    }
	    /* Do not process this command twice */
	    *cmd = 0;
	}
    } else
	printf("failed to read pad2 area of primary vdev\n");

    /* Mount ZFS only if it's not already mounted via nextboot parsing. */
    if (zfsmount.spa == NULL &&
	(zfs_spa_init(spa) != 0 || zfs_mount(spa, 0, &zfsmount) != 0)) {
	printf("%s: failed to mount default pool %s\n",
	    BOOTPROG, spa->spa_name);
	autoboot = 0;
    } else if (zfs_lookup(&zfsmount, PATH_CONFIG, &dn) == 0 ||
        zfs_lookup(&zfsmount, PATH_DOTCONFIG, &dn) == 0) {
	off = 0;
	zfs_read(spa, &dn, &off, cmd, sizeof(cmd));
    }

    if (*cmd) {
	/*
	 * Note that parse_cmd() is destructive to cmd[] and we also want
	 * to honor RBX_QUIET option that could be present in cmd[].
	 */
	memcpy(cmddup, cmd, sizeof(cmd));
	if (parse_cmd())
	    autoboot = 0;
	if (!OPT_CHECK(RBX_QUIET))
	    printf("%s: %s\n", PATH_CONFIG, cmddup);
	/* Do not process this command twice */
	*cmd = 0;
    }

    /* Do not risk waiting at the prompt forever. */
    if (nextboot && !autoboot)
	reboot();

    /*
     * Try to exec /boot/loader. If interrupted by a keypress,
     * or in case of failure, try to load a kernel directly instead.
     */

    if (autoboot && !*kname) {
	memcpy(kname, PATH_LOADER, sizeof(PATH_LOADER));
	if (!keyhit(3)) {
	    load();
	    memcpy(kname, PATH_KERNEL, sizeof(PATH_KERNEL));
	}
    }

    /* Present the user with the boot2 prompt. */

    for (;;) {
	if (!autoboot || !OPT_CHECK(RBX_QUIET)) {
	    printf("\nFreeBSD/x86 boot\n");
	    if (zfs_rlookup(spa, zfsmount.rootobj, rootname) != 0)
		printf("Default: %s/<0x%llx>:%s\n"
		       "boot: ",
		       spa->spa_name, zfsmount.rootobj, kname);
	    else if (rootname[0] != '\0')
		printf("Default: %s/%s:%s\n"
		       "boot: ",
		       spa->spa_name, rootname, kname);
	    else
		printf("Default: %s:%s\n"
		       "boot: ",
		       spa->spa_name, kname);
	}
	if (ioctrl & IO_SERIAL)
	    sio_flush();
	if (!autoboot || keyhit(5))
	    getstr(cmd, sizeof(cmd));
	else if (!autoboot || !OPT_CHECK(RBX_QUIET))
	    putchar('\n');
	autoboot = 0;
	if (parse_cmd())
	    putchar('\a');
	else
	    load();
    }
}

/* XXX - Needed for btxld to link the boot2 binary; do not remove. */
void
exit(int x)
{
    __exit(x);
}

void
reboot(void)
{
    __exit(0);
}

static void
load(void)
{
    union {
	struct exec ex;
	Elf32_Ehdr eh;
    } hdr;
    static Elf32_Phdr ep[2];
    static Elf32_Shdr es[2];
    caddr_t p;
    dnode_phys_t dn;
    off_t off;
    uint32_t addr, x;
    int fmt, i, j;

    if (zfs_lookup(&zfsmount, kname, &dn)) {
	printf("\nCan't find %s\n", kname);
	return;
    }
    off = 0;
    if (xfsread(&dn, &off, &hdr, sizeof(hdr)))
	return;
    if (N_GETMAGIC(hdr.ex) == ZMAGIC)
	fmt = 0;
    else if (IS_ELF(hdr.eh))
	fmt = 1;
    else {
	printf("Invalid %s\n", "format");
	return;
    }
    if (fmt == 0) {
	addr = hdr.ex.a_entry & 0xffffff;
	p = PTOV(addr);
	off = PAGE_SIZE;
	if (xfsread(&dn, &off, p, hdr.ex.a_text))
	    return;
	p += roundup2(hdr.ex.a_text, PAGE_SIZE);
	if (xfsread(&dn, &off, p, hdr.ex.a_data))
	    return;
	p += hdr.ex.a_data + roundup2(hdr.ex.a_bss, PAGE_SIZE);
	bootinfo.bi_symtab = VTOP(p);
	memcpy(p, &hdr.ex.a_syms, sizeof(hdr.ex.a_syms));
	p += sizeof(hdr.ex.a_syms);
	if (hdr.ex.a_syms) {
	    if (xfsread(&dn, &off, p, hdr.ex.a_syms))
		return;
	    p += hdr.ex.a_syms;
	    if (xfsread(&dn, &off, p, sizeof(int)))
		return;
	    x = *(uint32_t *)p;
	    p += sizeof(int);
	    x -= sizeof(int);
	    if (xfsread(&dn, &off, p, x))
		return;
	    p += x;
	}
    } else {
	off = hdr.eh.e_phoff;
	for (j = i = 0; i < hdr.eh.e_phnum && j < 2; i++) {
	    if (xfsread(&dn, &off, ep + j, sizeof(ep[0])))
		return;
	    if (ep[j].p_type == PT_LOAD)
		j++;
	}
	for (i = 0; i < 2; i++) {
	    p = PTOV(ep[i].p_paddr & 0xffffff);
	    off = ep[i].p_offset;
	    if (xfsread(&dn, &off, p, ep[i].p_filesz))
		return;
	}
	p += roundup2(ep[1].p_memsz, PAGE_SIZE);
	bootinfo.bi_symtab = VTOP(p);
	if (hdr.eh.e_shnum == hdr.eh.e_shstrndx + 3) {
	    off = hdr.eh.e_shoff + sizeof(es[0]) *
		(hdr.eh.e_shstrndx + 1);
	    if (xfsread(&dn, &off, &es, sizeof(es)))
		return;
	    for (i = 0; i < 2; i++) {
		memcpy(p, &es[i].sh_size, sizeof(es[i].sh_size));
		p += sizeof(es[i].sh_size);
		off = es[i].sh_offset;
		if (xfsread(&dn, &off, p, es[i].sh_size))
		    return;
		p += es[i].sh_size;
	    }
	}
	addr = hdr.eh.e_entry & 0xffffff;
    }
    bootinfo.bi_esymtab = VTOP(p);
    bootinfo.bi_kernelname = VTOP(kname);
    zfsargs.size = sizeof(zfsargs);
    zfsargs.pool = zfsmount.spa->spa_guid;
    zfsargs.root = zfsmount.rootobj;
    zfsargs.primary_pool = primary_spa->spa_guid;
#ifdef LOADER_GELI_SUPPORT
    explicit_bzero(gelipw, sizeof(gelipw));
    export_geli_boot_data(&zfsargs.gelidata);
#endif
    if (primary_vdev != NULL)
	zfsargs.primary_vdev = primary_vdev->v_guid;
    else
	printf("failed to detect primary vdev\n");
    /*
     * Note that the zfsargs struct is passed by value, not by pointer.  Code in
     * btxldr.S copies the values from the entry stack to a fixed location
     * within loader(8) at startup due to the presence of KARGS_FLAGS_EXTARG.
     */
    __exec((caddr_t)addr, RB_BOOTINFO | (opts & RBX_MASK),
	   bootdev,
	   KARGS_FLAGS_ZFS | KARGS_FLAGS_EXTARG,
	   (uint32_t) spa->spa_guid,
	   (uint32_t) (spa->spa_guid >> 32),
	   VTOP(&bootinfo),
	   zfsargs);
}

static int
zfs_mount_ds(char *dsname)
{
    uint64_t newroot;
    spa_t *newspa;
    char *q;

    q = strchr(dsname, '/');
    if (q)
	*q++ = '\0';
    newspa = spa_find_by_name(dsname);
    if (newspa == NULL) {
	printf("\nCan't find ZFS pool %s\n", dsname);
	return -1;
    }

    if (zfs_spa_init(newspa))
	return -1;

    newroot = 0;
    if (q) {
	if (zfs_lookup_dataset(newspa, q, &newroot)) {
	    printf("\nCan't find dataset %s in ZFS pool %s\n",
		    q, newspa->spa_name);
	    return -1;
	}
    }
    if (zfs_mount(newspa, newroot, &zfsmount)) {
	printf("\nCan't mount ZFS dataset\n");
	return -1;
    }
    spa = newspa;
    return (0);
}

static int
parse_cmd(void)
{
    char *arg = cmd;
    char *ep, *p, *q;
    const char *cp;
    int c, i, j;

    while ((c = *arg++)) {
	if (c == ' ' || c == '\t' || c == '\n')
	    continue;
	for (p = arg; *p && *p != '\n' && *p != ' ' && *p != '\t'; p++);
	ep = p;
	if (*p)
	    *p++ = 0;
	if (c == '-') {
	    while ((c = *arg++)) {
		if (c == 'P') {
		    if (*(uint8_t *)PTOV(0x496) & 0x10) {
			cp = "yes";
		    } else {
			opts |= OPT_SET(RBX_DUAL) | OPT_SET(RBX_SERIAL);
			cp = "no";
		    }
		    printf("Keyboard: %s\n", cp);
		    continue;
		} else if (c == 'S') {
		    j = 0;
		    while ((unsigned int)(i = *arg++ - '0') <= 9)
			j = j * 10 + i;
		    if (j > 0 && i == -'0') {
			comspeed = j;
			break;
		    }
		    /* Fall through to error below ('S' not in optstr[]). */
		}
		for (i = 0; c != optstr[i]; i++)
		    if (i == NOPT - 1)
			return -1;
		opts ^= OPT_SET(flags[i]);
	    }
	    ioctrl = OPT_CHECK(RBX_DUAL) ? (IO_SERIAL|IO_KEYBOARD) :
		     OPT_CHECK(RBX_SERIAL) ? IO_SERIAL : IO_KEYBOARD;
	    if (ioctrl & IO_SERIAL) {
	        if (sio_init(115200 / comspeed) != 0)
		    ioctrl &= ~IO_SERIAL;
	    }
	} if (c == '?') {
	    dnode_phys_t dn;

	    if (zfs_lookup(&zfsmount, arg, &dn) == 0) {
		zap_list(spa, &dn);
	    }
	    return -1;
	} else {
	    arg--;

	    /*
	     * Report pool status if the comment is 'status'. Lets
	     * hope no-one wants to load /status as a kernel.
	     */
	    if (!strcmp(arg, "status")) {
		spa_all_status();
		return -1;
	    }

	    /*
	     * If there is "zfs:" prefix simply ignore it.
	     */
	    if (strncmp(arg, "zfs:", 4) == 0)
		arg += 4;

	    /*
	     * If there is a colon, switch pools.
	     */
	    q = strchr(arg, ':');
	    if (q) {
		*q++ = '\0';
		if (zfs_mount_ds(arg) != 0)
		    return -1;
		arg = q;
	    }
	    if ((i = ep - arg)) {
		if ((size_t)i >= sizeof(kname))
		    return -1;
		memcpy(kname, arg, i + 1);
	    }
	}
	arg = p;
    }
    return 0;
}
