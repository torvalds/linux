/*	$OpenBSD: biosdev.c,v 1.102 2024/04/14 03:26:25 jsg Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
 * Copyright (c) 2003 Tobias Weingartner
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/disklabel.h>
#include <isofs/cd9660/iso.h>
#include <lib/libsa/saerrno.h>
#include <machine/biosvar.h>
#include <machine/tss.h>

#include "biosdev.h"
#include "debug.h"
#include "disk.h"
#include "libsa.h"

#ifdef SOFTRAID
#include <dev/softraidvar.h>
#include <lib/libsa/softraid.h>
#include "softraid_i386.h"
#endif

static const char *biosdisk_err(u_int);
static int biosdisk_errno(u_int);

int CHS_rw (int, int, int, int, int, int, void *);
static int EDD_rw (int, int, u_int32_t, u_int32_t, void *);

static int biosd_io(int, bios_diskinfo_t *, u_int, int, void *);
static u_int findopenbsd(bios_diskinfo_t *, const char **);

extern int debug;
int bios_bootdev;
int bios_cddev = -1;		/* Set by srt0 if coming from CD */

struct EDD_CB {
	u_int8_t  edd_len;	/* size of packet */
	u_int8_t  edd_res1;	/* reserved */
	u_int8_t  edd_nblk;	/* # of blocks to transfer */
	u_int8_t  edd_res2;	/* reserved */
	u_int16_t edd_off;	/* address of buffer (offset) */
	u_int16_t edd_seg;	/* address of buffer (segment) */
	u_int64_t edd_daddr;	/* starting block */
};

/*
 * reset disk system
 */
static int
biosdreset(int dev)
{
	int rv;

	__asm volatile (DOINT(0x13) "; setc %b0" : "=a" (rv)
	    : "0" (0), "d" (dev) : "%ecx", "cc");

	return ((rv & 0xff)? rv >> 8 : 0);
}

/*
 * Fill out a bios_diskinfo_t for this device.
 * Return 0 if all ok.
 * Return 1 if not ok.
 */
int
bios_getdiskinfo(int dev, bios_diskinfo_t *pdi)
{
	u_int rv;

	/* Just reset, don't check return code */
	rv = biosdreset(dev);

#ifdef BIOS_DEBUG
	if (debug)
		printf("getinfo: try #8, 0x%x, %p\n", dev, pdi);
#endif
	__asm volatile (DOINT(0x13) "\n\t"
	    "setc %b0; movzbl %h1, %1\n\t"
	    "movzbl %%cl, %3; andb $0x3f, %b3\n\t"
	    "xchgb %%cl, %%ch; rolb $2, %%ch"
	    : "=a" (rv), "=d" (pdi->bios_heads),
	      "=c" (pdi->bios_cylinders),
	      "=b" (pdi->bios_sectors)
	    : "0" (0x0800), "1" (dev) : "cc");

#ifdef BIOS_DEBUG
	if (debug) {
		printf("getinfo: got #8\n");
		printf("disk 0x%x: %d,%d,%d\n", dev, pdi->bios_cylinders,
		    pdi->bios_heads, pdi->bios_sectors);
	}
#endif
	if (rv & 0xff)
		return 1;

	/* Fix up info */
	pdi->bios_number = dev;
	pdi->bios_heads++;
	pdi->bios_cylinders &= 0x3ff;
	pdi->bios_cylinders++;

	/* NOTE:
	 * This currently hangs/reboots some machines
	 * The IBM ThinkPad 750ED for one.
	 *
	 * Funny that an IBM/MS extension would not be
	 * implemented by an IBM system...
	 *
	 * Future hangs (when reported) can be "fixed"
	 * with getSYSCONFaddr() and an exceptions list.
	 */
	if (dev & 0x80 && (dev == 0x80 || dev == 0x81 || dev == bios_bootdev)) {
		int bm;

#ifdef BIOS_DEBUG
		if (debug)
			printf("getinfo: try #41, 0x%x\n", dev);
#endif
		/* EDD support check */
		__asm volatile(DOINT(0x13) "; setc %b0"
			 : "=a" (rv), "=c" (bm)
			 : "0" (0x4100), "b" (0x55aa), "d" (dev) : "cc");
		if (!(rv & 0xff) && (BIOS_regs.biosr_bx & 0xffff) == 0xaa55)
			pdi->bios_edd = (bm & 0xffff) | ((rv & 0xff) << 16);
		else
			pdi->bios_edd = -1;

#ifdef BIOS_DEBUG
		if (debug) {
			printf("getinfo: got #41\n");
			printf("disk 0x%x: 0x%x\n", dev, bm);
		}
#endif
		/*
		 * If extended disk access functions are not supported
		 * there is not much point on doing EDD.
		 */
		if (!(pdi->bios_edd & EXT_BM_EDA))
			pdi->bios_edd = -1;
	} else
		pdi->bios_edd = -1;

	/* Skip sanity check for CHS options in EDD mode. */
	if (pdi->bios_edd != -1)
		return 0;

	/* Sanity check */
	if (!pdi->bios_cylinders || !pdi->bios_heads || !pdi->bios_sectors)
		return 1;

	/* CD-ROMs sometimes return heads == 1 */
	if (pdi->bios_heads < 2)
		return 1;

	return 0;
}

/*
 * Read/Write a block from given place using the BIOS.
 */
int
CHS_rw(int rw, int dev, int cyl, int head, int sect, int nsect, void *buf)
{
	int rv;

	rw = rw == F_READ ? 2 : 3;
	BIOS_regs.biosr_es = (u_int32_t)buf >> 4;
	__asm volatile ("movb %b7, %h1\n\t"
	    "movb %b6, %%dh\n\t"
	    "andl $0xf, %4\n\t"
	    /* cylinder; the highest 2 bits of cyl is in %cl */
	    "xchgb %%ch, %%cl\n\t"
	    "rorb  $2, %%cl\n\t"
	    "orb %b5, %%cl\n\t"
	    "inc %%cx\n\t"
	    DOINT(0x13) "\n\t"
	    "setc %b0"
	    : "=a" (rv)
	    : "0" (nsect), "d" (dev), "c" (cyl),
	      "b" (buf), "m" (sect), "m" (head),
	      "m" (rw)
	    : "cc", "memory");

	return ((rv & 0xff)? rv >> 8 : 0);
}

static __inline int
EDD_rw(int rw, int dev, u_int32_t daddr, u_int32_t nblk, void *buf)
{
	int rv;
	volatile static struct EDD_CB cb;

	/* Zero out reserved stuff */
	cb.edd_res1 = 0;
	cb.edd_res2 = 0;

	/* Fill in parameters */
	cb.edd_len = sizeof(cb);
	cb.edd_nblk = nblk;
	cb.edd_seg = ((u_int32_t)buf >> 4) & 0xffff;
	cb.edd_off = (u_int32_t)buf & 0xf;
	cb.edd_daddr = daddr;

	/* if offset/segment are zero, punt */
	if (!cb.edd_seg && !cb.edd_off)
		return 1;

	/* Call extended read/write (with disk packet) */
	BIOS_regs.biosr_ds = (u_int32_t)&cb >> 4;
	__asm volatile (DOINT(0x13) "; setc %b0" : "=a" (rv)
	    : "0" ((rw == F_READ)? 0x4200: 0x4300),
	      "d" (dev), "S" ((int) (&cb) & 0xf) : "%ecx", "cc");
	return ((rv & 0xff)? rv >> 8 : 0);
}

/*
 * Read given sector, handling retry/errors/etc.
 */
int
biosd_io(int rw, bios_diskinfo_t *bd, u_int off, int nsect, void *buf)
{
	int dev = bd->bios_number;
	int j, error;
	void *bb, *bb1 = NULL;
	int bbsize = nsect * DEV_BSIZE;

	if (bd->flags & BDI_EL_TORITO) {	/* It's a CD device */
		dev &= 0xff;			/* Mask out this flag bit */

		/*
		 * sys/lib/libsa/cd9600.c converts 2,048-byte CD sectors
		 * to DEV_BSIZE blocks before calling the device strategy
		 * routine.  However, the El Torito spec says that the
		 * BIOS will work in 2,048-byte sectors.  So shift back.
		 */
		off /= (ISO_DEFAULT_BLOCK_SIZE / DEV_BSIZE);
		nsect /= (ISO_DEFAULT_BLOCK_SIZE / DEV_BSIZE);
	}

	/*
	 * Use a bounce buffer to not cross 64k DMA boundary, and to
	 * not access 1 MB or above.
	 */
	if (((((u_int32_t)buf) & ~0xffff) !=
	    (((u_int32_t)buf + bbsize) & ~0xffff)) ||
	    (((u_int32_t)buf) >= 0x100000)) {
		bb = bb1 = alloc(bbsize * 2);
		if ((((u_int32_t)bb) & ~0xffff) !=
		    (((u_int32_t)bb + bbsize - 1) & ~0xffff))
			bb = (void *)(((u_int32_t)bb + bbsize - 1) & ~0xffff);
		if (rw != F_READ)
			bcopy(buf, bb, bbsize);
	} else
		bb = buf;

	/* Try to do operation up to 5 times */
	for (error = 1, j = 5; j-- && error; ) {
		/* CHS or LBA access? */
		if (bd->bios_edd != -1) {
			error = EDD_rw(rw, dev, off, nsect, bb);
		} else {
			int cyl, head, sect;
			size_t i, n;
			char *p = bb;

			/* Handle track boundaries */
			for (error = i = 0; error == 0 && i < nsect;
			    i += n, off += n, p += n * DEV_BSIZE) {

				btochs(off, cyl, head, sect, bd->bios_heads,
				    bd->bios_sectors);

				if ((sect + (nsect - i)) >= bd->bios_sectors)
					n = bd->bios_sectors - sect;
				else
					n = nsect - i;

				error = CHS_rw(rw, dev, cyl, head, sect, n, p);

				/* ECC corrected */
				if (error == 0x11)
					error = 0;
			}
		}
		switch (error) {
		case 0x00:	/* No errors */
		case 0x11:	/* ECC corrected */
			error = 0;
			break;

		default:	/* All other errors */
#ifdef BIOS_DEBUG
			if (debug)
				printf("\nBIOS error 0x%x (%s)\n",
				    error, biosdisk_err(error));
#endif
			biosdreset(dev);
			break;
		}
	}

	if (bb != buf && rw == F_READ)
		bcopy(bb, buf, bbsize);
	free(bb1, bbsize * 2);

#ifdef BIOS_DEBUG
	if (debug) {
		if (error != 0)
			printf("=0x%x(%s)", error, biosdisk_err(error));
		putchar('\n');
	}
#endif

	return error;
}

#define MAXSECTS 32

int
biosd_diskio(int rw, struct diskinfo *dip, u_int off, int nsect, void *buf)
{
	char *dest = buf;
	int n, ret;

	/*
	 * Avoid doing too large reads, the bounce buffer used by biosd_io()
	 * might run us out-of-mem.
	 */
	for (ret = 0; ret == 0 && nsect > 0;
	    off += MAXSECTS, dest += MAXSECTS * DEV_BSIZE, nsect -= MAXSECTS) {
		n = nsect >= MAXSECTS ? MAXSECTS : nsect;
		ret = biosd_io(rw, &dip->bios_info, off, n, dest);
	}
	return ret;
}

/*
 * Try to read the bsd label on the given BIOS device.
 */
static u_int
findopenbsd(bios_diskinfo_t *bd, const char **err)
{
	struct dos_mbr mbr;
	struct dos_partition *dp;
	u_int mbroff = DOSBBSECTOR;
	u_int mbr_eoff = DOSBBSECTOR;	/* Offset of MBR extended partition. */
	int error, i, maxebr = DOS_MAXEBR, nextebr;

again:
	if (!maxebr--) {
		*err = "too many extended partitions";
		return (-1);
	}

	/* Read MBR */
	bzero(&mbr, sizeof(mbr));
	error = biosd_io(F_READ, bd, mbroff, 1, &mbr);
	if (error) {
		*err = biosdisk_err(error);
		return (-1);
	}

	/* check mbr signature */
	if (mbr.dmbr_sign != DOSMBR_SIGNATURE) {
		*err = "bad MBR signature\n";
		return (-1);
	}

	/* Search for OpenBSD partition */
	nextebr = 0;
	for (i = 0; i < NDOSPART; i++) {
		dp = &mbr.dmbr_parts[i];
		if (!dp->dp_size)
			continue;
#ifdef BIOS_DEBUG
		if (debug)
			printf("found partition %u: "
			    "type %u (0x%x) offset %u (0x%x)\n",
			    (int)(dp - mbr.dmbr_parts),
			    dp->dp_typ, dp->dp_typ,
			    dp->dp_start, dp->dp_start);
#endif
		if (dp->dp_typ == DOSPTYP_OPENBSD) {
			if (dp->dp_start > (dp->dp_start + mbroff))
				continue;
			return (dp->dp_start + mbroff);
		}

		/*
		 * Record location of next ebr if and only if this is the first
		 * extended partition in this boot record!
		 */
		if (!nextebr && (dp->dp_typ == DOSPTYP_EXTEND ||
		    dp->dp_typ == DOSPTYP_EXTENDL)) {
			nextebr = dp->dp_start + mbr_eoff;
			if (nextebr < dp->dp_start)
				nextebr = (u_int)-1;
			if (mbr_eoff == DOSBBSECTOR)
				mbr_eoff = dp->dp_start;
		}
	}

	if (nextebr && nextebr != (u_int)-1) {
		mbroff = nextebr;
		goto again;
	}

	return (-1);
}

const char *
bios_getdisklabel(bios_diskinfo_t *bd, struct disklabel *label)
{
	u_int start = 0;
	char buf[DEV_BSIZE];
	const char *err = NULL;
	int error;

	/* Sanity check */
	if (bd->bios_edd == -1 &&
	    (bd->bios_heads == 0 || bd->bios_sectors == 0))
		return "failed to read disklabel";

	/* MBR is a harddisk thing */
	if (bd->bios_number & 0x80) {
		start = findopenbsd(bd, &err);
		if (start == (u_int)-1) {
			if (err != NULL)
				return (err);
			return "no OpenBSD partition\n";
		}
	}

	/* Load BSD disklabel */
#ifdef BIOS_DEBUG
	if (debug)
		printf("loading disklabel @ %u\n", start + DOS_LABELSECTOR);
#endif
	/* read disklabel */
	error = biosd_io(F_READ, bd, start + DOS_LABELSECTOR, 1, buf);

	if (error)
		return "failed to read disklabel";

	/* Fill in disklabel */
	return (getdisklabel(buf, label));
}

int
biosopen(struct open_file *f, ...)
{
#ifdef SOFTRAID
	struct sr_boot_volume *bv;
#endif
	register char *cp, **file;
	dev_t maj, unit, part;
	struct diskinfo *dip;
	int biosdev, devlen;
	const char *st;
	va_list ap;
	char *dev;

	va_start(ap, f);
	cp = *(file = va_arg(ap, char **));
	va_end(ap);

#ifdef BIOS_DEBUG
	if (debug)
		printf("%s\n", cp);
#endif

	f->f_devdata = NULL;

	/* Search for device specification. */
	dev = cp;
	if (cp[4] == ':')
		devlen = 2;
	else if (cp[5] == ':')
		devlen = 3;
	else
		return ENOENT;
	cp += devlen;

	/* Get unit. */
	if ('0' <= *cp && *cp <= '9')
		unit = *cp++ - '0';
	else {
		printf("Bad unit number\n");
		return EUNIT;
	}

	/* Get partition. */
	if ('a' <= *cp && *cp <= 'p')
		part = *cp++ - 'a';
	else {
		printf("Bad partition\n");
		return EPART;
	}

	/* Get filename. */
	cp++;	/* skip ':' */
	if (*cp != 0)
		*file = cp;
	else
		f->f_flags |= F_RAW;

#ifdef SOFTRAID
	/* Intercept softraid disks. */
	if (strncmp("sr", dev, 2) == 0) {
		/* We only support read-only softraid. */
		f->f_flags |= F_NOWRITE;

		/* Create a fake diskinfo for this softraid volume. */
		SLIST_FOREACH(bv, &sr_volumes, sbv_link)
			if (bv->sbv_unit == unit)
				break;
		if (bv == NULL) {
			printf("Unknown device: sr%d\n", unit);
			return EADAPT;
		}

		if (bv->sbv_level == 'C' && bv->sbv_keys == NULL)
			if (sr_crypto_unlock_volume(bv) != 0)
				return EPERM;

		if (bv->sbv_diskinfo == NULL) {
			dip = alloc(sizeof(struct diskinfo));
			bzero(dip, sizeof(*dip));
			dip->strategy = biosstrategy;
			bv->sbv_diskinfo = dip;
			dip->sr_vol = bv;
			dip->bios_info.flags |= BDI_BADLABEL;
		}

		dip = bv->sbv_diskinfo;

		if (dip->bios_info.flags & BDI_BADLABEL) {
			/* Attempt to read disklabel. */
			bv->sbv_part = 'c';
			if (sr_getdisklabel(bv, &dip->disklabel))
				return ERDLAB;
			dip->bios_info.flags &= ~BDI_BADLABEL;
			check_hibernate(dip);
		}

		bv->sbv_part = part + 'a';

		bootdev_dip = dip;
		f->f_devdata = dip;

		return 0;
	}
#endif
 
	for (maj = 0; maj < nbdevs &&
	    strncmp(dev, bdevs[maj], devlen); maj++);
	if (maj >= nbdevs) {
		printf("Unknown device: ");
		for (cp = *file; *cp != ':'; cp++)
			putchar(*cp);
		putchar('\n');
		return EADAPT;
	}

	biosdev = unit;
	switch (maj) {
	case 0:  /* wd */
	case 4:  /* sd */
	case 17: /* hd */
		biosdev |= 0x80;
		break;
	case 2:  /* fd */
		break;
	case 6:  /* cd */
		biosdev = bios_bootdev & 0xff;
		break;
	default:
		return ENXIO;
	}

	/* Find device */
	bootdev_dip = dip = dklookup(biosdev);

	/* Fix up bootdev */
	{ dev_t bsd_dev;
		bsd_dev = dip->bios_info.bsd_dev;
		dip->bsddev = MAKEBOOTDEV(B_TYPE(bsd_dev), B_ADAPTOR(bsd_dev),
		    B_CONTROLLER(bsd_dev), unit, part);
		dip->bootdev = MAKEBOOTDEV(B_TYPE(bsd_dev), B_ADAPTOR(bsd_dev),
		    B_CONTROLLER(bsd_dev), B_UNIT(bsd_dev), part);
	}

#if 0
	dip->bios_info.bsd_dev = dip->bootdev;
	bootdev = dip->bootdev;
#endif

#ifdef BIOS_DEBUG
	if (debug) {
		printf("BIOS geometry: heads=%u, s/t=%u; EDD=%d\n",
		    dip->bios_info.bios_heads, dip->bios_info.bios_sectors,
		    dip->bios_info.bios_edd);
	}
#endif

	/* Try for disklabel again (might be removable media) */
	if (dip->bios_info.flags & BDI_BADLABEL) {
		st = bios_getdisklabel(&dip->bios_info, &dip->disklabel);
#ifdef BIOS_DEBUG
		if (debug && st)
			printf("%s\n", st);
#endif
		if (!st) {
			dip->bios_info.flags &= ~BDI_BADLABEL;
			dip->bios_info.flags |= BDI_GOODLABEL;
		} else
			return ERDLAB;
	}

	f->f_devdata = dip;

	return 0;
}

const u_char bidos_errs[] =
/* ignored	"\x00" "successful completion\0" */
		"\x01" "invalid function/parameter\0"
		"\x02" "address mark not found\0"
		"\x03" "write-protected\0"
		"\x04" "sector not found\0"
		"\x05" "reset failed\0"
		"\x06" "disk changed\0"
		"\x07" "drive parameter activity failed\0"
		"\x08" "DMA overrun\0"
		"\x09" "data boundary error\0"
		"\x0A" "bad sector detected\0"
		"\x0B" "bad track detected\0"
		"\x0C" "invalid media\0"
		"\x0E" "control data address mark detected\0"
		"\x0F" "DMA arbitration level out of range\0"
		"\x10" "uncorrectable CRC or ECC error on read\0"
/* ignored	"\x11" "data ECC corrected\0" */
		"\x20" "controller failure\0"
		"\x31" "no media in drive\0"
		"\x32" "incorrect drive type in CMOS\0"
		"\x40" "seek failed\0"
		"\x80" "operation timed out\0"
		"\xAA" "drive not ready\0"
		"\xB0" "volume not locked in drive\0"
		"\xB1" "volume locked in drive\0"
		"\xB2" "volume not removable\0"
		"\xB3" "volume in use\0"
		"\xB4" "lock count exceeded\0"
		"\xB5" "valid eject request failed\0"
		"\xBB" "undefined error\0"
		"\xCC" "write fault\0"
		"\xE0" "status register error\0"
		"\xFF" "sense operation failed\0"
		"\x00" "\0";

static const char *
biosdisk_err(u_int error)
{
	register const u_char *p = bidos_errs;

	while (*p && *p != error)
		while (*p++);

	return ++p;
}

const struct biosdisk_errors {
	u_char error;
	u_char errno;
} tab[] = {
	{ 0x01, EINVAL },
	{ 0x03, EROFS },
	{ 0x08, EINVAL },
	{ 0x09, EINVAL },
	{ 0x0A, EBSE },
	{ 0x0B, EBSE },
	{ 0x0C, ENXIO },
	{ 0x0D, EINVAL },
	{ 0x10, EECC },
	{ 0x20, EHER },
	{ 0x31, ENXIO },
	{ 0x32, ENXIO },
	{ 0x00, EIO }
};

static int
biosdisk_errno(u_int error)
{
	register const struct biosdisk_errors *p;

	if (error == 0)
		return 0;

	for (p = tab; p->error && p->error != error; p++)
		;

	return p->errno;
}

int
biosstrategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	struct diskinfo *dip = (struct diskinfo *)devdata;
	u_int8_t error = 0;
	size_t nsect;

#ifdef SOFTRAID
	/* Intercept strategy for softraid volumes. */
	if (dip->sr_vol)
		return sr_strategy(dip->sr_vol, rw, blk, size, buf, rsize);
#endif

	nsect = (size + DEV_BSIZE - 1) / DEV_BSIZE;
	blk += dip->disklabel.d_partitions[B_PARTITION(dip->bsddev)].p_offset;

	/* Read all, sub-functions handle track boundaries */
	if (blk < 0)
		error = EINVAL;
	else
		error = biosd_diskio(rw, dip, blk, nsect, buf);

#ifdef BIOS_DEBUG
	if (debug) {
		if (error != 0)
			printf("=0x%x(%s)", error, biosdisk_err(error));
		putchar('\n');
	}
#endif

	if (rsize != NULL)
		*rsize = nsect * DEV_BSIZE;

	return (biosdisk_errno(error));
}

int
biosclose(struct open_file *f)
{
	f->f_devdata = NULL;

	return 0;
}

int
biosioctl(struct open_file *f, u_long cmd, void *data)
{
	return 0;
}
