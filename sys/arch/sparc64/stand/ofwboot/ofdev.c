/*	$OpenBSD: ofdev.c,v 1.40 2024/04/14 03:26:25 jsg Exp $	*/
/*	$NetBSD: ofdev.c,v 1.1 2000/08/20 14:58:41 mrg Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Device I/O routines using Open Firmware
 */
#include <sys/param.h>
#include <sys/disklabel.h>
#ifdef NETBOOT
#include <netinet/in.h>
#endif

#include <lib/libkern/funcs.h>
#include <lib/libsa/stand.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <lib/libsa/cd9660.h>
#ifdef NETBOOT
#include <lib/libsa/nfs.h>
#endif

#ifdef SOFTRAID
#include <sys/queue.h>
#include <dev/softraidvar.h>
#include "softraid_sparc64.h"
#include "disk.h"
#endif

#include <dev/sun/disklabel.h>
#include "openfirm.h"
#include "ofdev.h"

/* needed for DISKLABELV1_FFS_FRAGBLOCK */
int	 ffs(int);

extern char bootdev[];

/*
 * This is ugly.  A path on a sparc machine is something like this:
 *
 *	[device] [-<options] [path] [-options] [otherstuff] [-<more options]
 *
 */

static char *
filename(char *str, char *ppart)
{
	char *cp, *lp;
	char savec;
	int dhandle;
	char devtype[16];

	lp = str;
	devtype[0] = 0;
	*ppart = 0;
	for (cp = str; *cp; lp = cp) {
		/* For each component of the path name... */
		while (*++cp && *cp != '/');
		savec = *cp;
		*cp = 0;
		/* ...look whether there is a device with this name */
		dhandle = OF_finddevice(str);
		DNPRINTF(BOOT_D_OFDEV, "filename: OF_finddevice(%s) says %x\n",
		    str, dhandle);
		*cp = savec;
		if (dhandle == -1) {
			/* if not, lp is the delimiter between device and path */
			/* if the last component was a block device... */
			if (!strcmp(devtype, "block")) {
				/* search for arguments */
				DNPRINTF(BOOT_D_OFDEV, "filename: hunting for "
				    "arguments in %s\n", str);
				for (cp = lp;
				     --cp >= str && *cp != '/' && *cp != '-';);
				if (cp >= str && *cp == '-') {
					/* found arguments, make firmware ignore them */
					*cp = 0;
					for (cp = lp; *--cp && *cp != ',';)
						;
					if (*++cp >= 'a' && *cp <= 'a' + MAXPARTITIONS)
						*ppart = *cp;
				}
			}
			DNPRINTF(BOOT_D_OFDEV, "filename: found %s\n", lp);
			return lp;
		} else if (OF_getprop(dhandle, "device_type", devtype, sizeof devtype) < 0)
			devtype[0] = 0;
	}
	DNPRINTF(BOOT_D_OFDEV, "filename: not found\n", lp);
	return 0;
}

int
strategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	struct of_dev *dev = devdata;
	u_quad_t pos;
	int n;

#ifdef SOFTRAID
	/* Intercept strategy for softraid volumes. */
	if (dev->type == OFDEV_SOFTRAID)
		return sr_strategy(bootdev_dip->sr_vol, bootdev_dip->sr_handle,
		    rw, blk, size, buf, rsize);
#endif
	if (dev->type != OFDEV_DISK)
		panic("strategy");

	DNPRINTF(BOOT_D_OFDEV, "strategy: block %lx, partition offset %lx, "
	    "blksz %lx\n", (long)blk, (long)dev->partoff, (long)dev->bsize);
	DNPRINTF(BOOT_D_OFDEV, "strategy: seek position should be: %lx\n",
	    (long)((blk + dev->partoff) * dev->bsize));
	pos = (u_quad_t)(blk + dev->partoff) * dev->bsize;

	for (;;) {
		DNPRINTF(BOOT_D_OFDEV, "strategy: seeking to %lx\n", (long)pos);
		if (OF_seek(dev->handle, pos) < 0)
			break;
		DNPRINTF(BOOT_D_OFDEV, "strategy: reading %lx at %p\n",
		    (long)size, buf);
		if (rw == F_READ)
			n = OF_read(dev->handle, buf, size);
		else
			n = OF_write(dev->handle, buf, size);
		if (n == -2)
			continue;
		if (n < 0)
			break;
		if (rsize)
			*rsize = n;
		return 0;
	}
	return EIO;
}

static int
devclose(struct open_file *of)
{
	struct of_dev *op = of->f_devdata;

#ifdef NETBOOT
	if (op->type == OFDEV_NET)
		net_close(op);
#endif
#ifdef SOFTRAID
	if (op->type == OFDEV_SOFTRAID) {
		op->handle = -1;
		return 0;
	}
#endif
	OF_close(op->handle);
	op->handle = -1;
	return 0;
}

struct devsw devsw[1] = {
	{
		"OpenFirmware",
		strategy,
		(int (*)(struct open_file *, ...))nodev,
		devclose,
		noioctl
	}
};
int ndevs = sizeof devsw / sizeof devsw[0];

#ifdef SPARC_BOOT_UFS
static struct fs_ops file_system_ufs = {
	ufs_open, ufs_close, ufs_read, ufs_write, ufs_seek,
	ufs_stat, ufs_readdir, ufs_fchmod
};
static struct fs_ops file_system_ufs2 = {
	ufs2_open, ufs2_close, ufs2_read, ufs2_write, ufs2_seek,
	ufs2_stat, ufs2_readdir, ufs2_fchmod
};
#endif
#ifdef SPARC_BOOT_HSFS
static struct fs_ops file_system_cd9660 = {
	cd9660_open, cd9660_close, cd9660_read, cd9660_write, cd9660_seek,
	cd9660_stat, cd9660_readdir
};
#endif
#ifdef NETBOOT
static struct fs_ops file_system_nfs = {
	nfs_open, nfs_close, nfs_read, nfs_write, nfs_seek,
	nfs_stat, nfs_readdir
};
#endif

struct fs_ops file_system[4];
int nfsys;

static struct of_dev ofdev = {
	-1,
};

char opened_name[256];

/************************************************************************
 *
 * The rest of this was taken from arch/sparc64/scsi/sun_disklabel.c
 * and then substantially rewritten by Gordon W. Ross
 *
 ************************************************************************/

/* What partition types to assume for Sun disklabels: */
static u_char
sun_fstypes[8] = {
	FS_BSDFFS,	/* a */
	FS_SWAP,	/* b */
	FS_OTHER,	/* c - whole disk */
	FS_BSDFFS,	/* d */
	FS_BSDFFS,	/* e */
	FS_BSDFFS,	/* f */
	FS_BSDFFS,	/* g */
	FS_BSDFFS,	/* h */
};

/*
 * Given a struct sun_disklabel, assume it has an extended partition
 * table and compute the correct value for sl_xpsum.
 */
static __inline u_int
sun_extended_sum(struct sun_disklabel *sl, void *end)
{
	u_int sum, *xp, *ep;

	xp = (u_int *)&sl->sl_xpmag;
	ep = (u_int *)end;

	sum = 0;
	for (; xp < ep; xp++)
		sum += *xp;
	return (sum);
}

/*
 * Given a SunOS disk label, set lp to a BSD disk label.
 * The BSD label is cleared out before this is called.
 */
static int
disklabel_sun_to_bsd(struct sun_disklabel *sl, struct disklabel *lp)
{
	struct sun_preamble *preamble = (struct sun_preamble *)sl;
	struct sun_partinfo *ppp;
	struct sun_dkpart *spp;
	struct partition *npp;
	u_short cksum = 0, *sp1, *sp2;
	int i, secpercyl;

	/* Verify the XOR check. */
	sp1 = (u_short *)sl;
	sp2 = (u_short *)(sl + 1);
	while (sp1 < sp2)
		cksum ^= *sp1++;
	if (cksum != 0)
		return (EINVAL);	/* SunOS disk label, bad checksum */

	/* Format conversion. */
	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_flags = D_VENDOR;
	memcpy(lp->d_packname, sl->sl_text, sizeof(lp->d_packname));

	lp->d_secsize = DEV_BSIZE;
	lp->d_nsectors = sl->sl_nsectors;
	lp->d_ntracks = sl->sl_ntracks;
	lp->d_ncylinders = sl->sl_ncylinders;

	secpercyl = sl->sl_nsectors * sl->sl_ntracks;
	lp->d_secpercyl = secpercyl;
	if (DL_GETDSIZE(lp) == 0)
		DL_SETDSIZE(lp, (u_int64_t)secpercyl * sl->sl_ncylinders);
	lp->d_version = 1;

	memcpy(&lp->d_uid, &sl->sl_uid, sizeof(lp->d_uid));

	lp->d_acylinders = sl->sl_acylinders;

	lp->d_npartitions = MAXPARTITIONS;

	for (i = 0; i < 8; i++) {
		spp = &sl->sl_part[i];
		npp = &lp->d_partitions[i];
		DL_SETPOFFSET(npp, spp->sdkp_cyloffset * secpercyl);
		DL_SETPSIZE(npp, spp->sdkp_nsectors);
		if (DL_GETPSIZE(npp) == 0) {
			npp->p_fstype = FS_UNUSED;
		} else {
			npp->p_fstype = sun_fstypes[i];
			if (npp->p_fstype == FS_BSDFFS) {
				/*
				 * The sun label does not store the FFS fields,
				 * so just set them with default values here.
				 */
				npp->p_fragblock =
				    DISKLABELV1_FFS_FRAGBLOCK(2048, 8);
				npp->p_cpg = 16;
			}
		}
	}

	/* Clear "extended" partition info, tentatively */
	for (i = 0; i < SUNXPART; i++) {
		npp = &lp->d_partitions[i+8];
		DL_SETPOFFSET(npp, 0);
		DL_SETPSIZE(npp, 0);
		npp->p_fstype = FS_UNUSED;
	}

	/* Check to see if there's an "extended" partition table
	 * SL_XPMAG partitions had checksums up to just before the
	 * (new) sl_types variable, while SL_XPMAGTYP partitions have
	 * checksums up to the just before the (new) sl_xxx1 variable.
	 * Also, disklabels created prior to the addition of sl_uid will
	 * have a checksum to just before the sl_uid variable.
	 */
	if ((sl->sl_xpmag == SL_XPMAG &&
	    sun_extended_sum(sl, &sl->sl_types) == sl->sl_xpsum) ||
	    (sl->sl_xpmag == SL_XPMAGTYP &&
	    sun_extended_sum(sl, &sl->sl_uid) == sl->sl_xpsum) ||
	    (sl->sl_xpmag == SL_XPMAGTYP &&
	    sun_extended_sum(sl, &sl->sl_xxx1) == sl->sl_xpsum)) {
		/*
		 * There is.  Copy over the "extended" partitions.
		 * This code parallels the loop for partitions a-h.
		 */
		for (i = 0; i < SUNXPART; i++) {
			spp = &sl->sl_xpart[i];
			npp = &lp->d_partitions[i+8];
			DL_SETPOFFSET(npp, spp->sdkp_cyloffset * secpercyl);
			DL_SETPSIZE(npp, spp->sdkp_nsectors);
			if (DL_GETPSIZE(npp) == 0) {
				npp->p_fstype = FS_UNUSED;
				continue;
			}
			npp->p_fstype = FS_BSDFFS;
			npp->p_fragblock =
			    DISKLABELV1_FFS_FRAGBLOCK(2048, 8);
			npp->p_cpg = 16;
		}
		if (sl->sl_xpmag == SL_XPMAGTYP) {
			for (i = 0; i < MAXPARTITIONS; i++) {
				npp = &lp->d_partitions[i];
				npp->p_fstype = sl->sl_types[i];
				npp->p_fragblock = sl->sl_fragblock[i];
				npp->p_cpg = sl->sl_cpg[i];
			}
		}
	} else if (preamble->sl_nparts <= 8) {
		/*
		 * A more traditional Sun label.  Recognise certain filesystem
		 * types from it, if they are available.
		 */
		i = preamble->sl_nparts;
		if (i == 0)
			i = 8;

		npp = &lp->d_partitions[i-1];
		ppp = &preamble->sl_part[i-1];
		for (; i > 0; i--, npp--, ppp--) {
			if (npp->p_size == 0)
				continue;
			if ((ppp->spi_tag == 0) && (ppp->spi_flag == 0))
				continue;

			switch (ppp->spi_tag) {
			case SPTAG_SUNOS_ROOT:
			case SPTAG_SUNOS_USR:
			case SPTAG_SUNOS_VAR:
			case SPTAG_SUNOS_HOME:
				npp->p_fstype = FS_BSDFFS;
				npp->p_fragblock =
				    DISKLABELV1_FFS_FRAGBLOCK(2048, 8);
				npp->p_cpg = 16;
				break;
			case SPTAG_LINUX_EXT2:
				npp->p_fstype = FS_EXT2FS;
				break;
			default:
				/* FS_SWAP for _SUNOS_SWAP and _LINUX_SWAP? */
				npp->p_fstype = FS_UNUSED;
				break;
			}
		}
	}

	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
	DNPRINTF(BOOT_D_OFDEV, "disklabel_sun_to_bsd: success!\n");
	return (0);
}

/*
 * Find a valid disklabel.
 */
static char *
search_label(struct of_dev *devp, u_long off, char *buf, struct disklabel *lp,
    u_long off0)
{
	struct disklabel *dlp;
	struct sun_disklabel *slp;
	size_t read;

	/* minimal requirements for archetypal disk label */
	if (DL_GETDSIZE(lp) == 0)
		DL_SETDSIZE(lp, 0x1fffffff);
	lp->d_npartitions = MAXPARTITIONS;
	if (DL_GETPSIZE(&lp->d_partitions[0]) == 0)
		DL_SETPSIZE(&lp->d_partitions[0], 0x1fffffff);
	DL_SETPOFFSET(&lp->d_partitions[0], 0);

	if (strategy(devp, F_READ, off, DEV_BSIZE, buf, &read)
	    || read != DEV_BSIZE)
		return ("Cannot read label");

	/* Check for a disk label. */
	dlp = (struct disklabel *) (buf + LABELOFFSET);
	if (dlp->d_magic == DISKMAGIC) {
		if (dkcksum(dlp))
			return ("corrupt disk label");
		*lp = *dlp;
		DNPRINTF(BOOT_D_OFDEV, "search_label: found disk label\n");
		return (NULL);
	}

	/* Check for a Sun disk label (for PROM compatibility). */
	slp = (struct sun_disklabel *)buf;
	if (slp->sl_magic == SUN_DKMAGIC) {
		if (disklabel_sun_to_bsd(slp, lp) != 0)
			return ("corrupt disk label");
		DNPRINTF(BOOT_D_OFDEV, "search_label: found disk label\n");
		return (NULL);
	}

	return ("no disk label");
}

int
load_disklabel(struct of_dev *ofdev, struct disklabel *label)
{
	char buf[DEV_BSIZE];
	size_t read;
	int error = 0;
	char *errmsg = NULL;

	DNPRINTF(BOOT_D_OFDEV, "load_disklabel: trying to read disklabel\n");
	if (strategy(ofdev, F_READ,
		     LABELSECTOR, DEV_BSIZE, buf, &read) != 0
	    || read != DEV_BSIZE
	    || (errmsg = getdisklabel(buf, label))) {
#ifdef BOOT_DEBUG
		if (errmsg)
			DNPRINTF(BOOT_D_OFDEV,
			    "load_disklabel: getdisklabel says %s\n", errmsg);
#endif
		errmsg = search_label(ofdev, LABELSECTOR, buf, label, 0);
		if (errmsg) {
			printf("load_disklabel: search_label says %s\n",
			    errmsg);
			error = ERDLAB;
		}
	}

	return (error);
}

int
devopen(struct open_file *of, const char *name, char **file)
{
	char *cp;
	char partition;
	char fname[256];
	char buf[DEV_BSIZE];
	struct disklabel label;
	int dhandle, ihandle, part, parent;
	int error = 0;
#ifdef SOFTRAID
	char volno;
#endif

	nfsys = 0;
	if (ofdev.handle != -1)
		panic("devopen");
	DNPRINTF(BOOT_D_OFDEV, "devopen: you want %s\n", name);
	if (strlcpy(fname, name, sizeof fname) >= sizeof fname)
		return ENAMETOOLONG;
#ifdef SOFTRAID
	if (bootdev_dip) {
		if (fname[0] == 's' && fname[1] == 'r' &&
		    '0' <= fname[2] && fname[2] <= '9') {
			/* We only support read-only softraid. */
			of->f_flags |= F_NOWRITE;

			volno = fname[2];
			if ('a' <= fname[3] &&
			    fname[3] <= 'a' + MAXPARTITIONS) {
				partition = fname[3];
				if (fname[4] == ':')
					cp = &fname[5];
				else
					cp = &fname[4];
			} else {
				partition = 'a';
				cp = &fname[3];
			}
		} else {
			volno = '0';
			partition = 'a';
			cp = &fname[0];
		}
		snprintf(buf, sizeof buf, "sr%c%c:", volno, partition);
		if (strlcpy(opened_name, buf, sizeof opened_name)
		    >= sizeof opened_name)
			return ENAMETOOLONG;
		*file = opened_name + strlen(opened_name);
		if (!*cp) {
			if (strlcpy(buf, DEFAULT_KERNEL, sizeof buf)
			    >= sizeof buf)
				return ENAMETOOLONG;
		} else {
			if (snprintf(buf, sizeof buf, "%s%s",
			    *cp == '/' ? "" : "/", cp) >= sizeof buf)
				return ENAMETOOLONG;
		}
		if (strlcat(opened_name, buf, sizeof opened_name) >=
		    sizeof opened_name)
			return ENAMETOOLONG;
	} else {
#endif
		cp = filename(fname, &partition);
		if (cp) {
			if (strlcpy(buf, cp, sizeof buf) >= sizeof buf)
				return ENAMETOOLONG;
			*cp = 0;
		}
		if (!cp || !*buf) {
			if (strlcpy(buf, DEFAULT_KERNEL, sizeof buf)
			    >= sizeof buf)
				return ENAMETOOLONG;
		}
		if (!*fname) {
			if (strlcpy(fname, bootdev, sizeof fname)
			    >= sizeof fname)
				return ENAMETOOLONG;
		}
		if (strlcpy(opened_name, fname,
		    partition ? (sizeof opened_name) - 2 : sizeof opened_name)
		    >= sizeof opened_name)
			return ENAMETOOLONG;
		if (partition) {
			cp = opened_name + strlen(opened_name);
			*cp++ = ':';
			*cp++ = partition;
			*cp = 0;
		}
		if (*buf != '/') {
			if (strlcat(opened_name, "/", sizeof opened_name) >=
			    sizeof opened_name)
				return ENAMETOOLONG;
		}
		if (strlcat(opened_name, buf, sizeof opened_name) >=
		    sizeof opened_name)
			return ENAMETOOLONG;
		*file = opened_name + strlen(fname) + 1;
#ifdef SOFTRAID
	}
#endif
	DNPRINTF(BOOT_D_OFDEV, "devopen: trying %s\n", fname);
#ifdef SOFTRAID
	if (bootdev_dip) {
		/* Redirect to the softraid boot volume. */
		struct partition *pp;

		bzero(&ofdev, sizeof ofdev);
		ofdev.type = OFDEV_SOFTRAID;

		if (partition) {
			if (partition < 'a' ||
			    partition >= 'a' + MAXPARTITIONS) {
				printf("invalid partition '%c'\n", partition);
				return EINVAL;
			}
			part = partition - 'a';
			pp = &bootdev_dip->disklabel.d_partitions[part];
			if (pp->p_fstype == FS_UNUSED || pp->p_size == 0) {
				printf("invalid partition '%c'\n", partition);
				return EINVAL;
			}
			bootdev_dip->sr_vol->sbv_part = partition;
		} else
			bootdev_dip->sr_vol->sbv_part = 'a';

		of->f_dev = devsw;
		of->f_devdata = &ofdev;

#ifdef SPARC_BOOT_UFS
		bcopy(&file_system_ufs, &file_system[nfsys++], sizeof file_system[0]);
		bcopy(&file_system_ufs2, &file_system[nfsys++], sizeof file_system[0]);
#else
#error "-DSOFTRAID requires -DSPARC_BOOT_UFS"
#endif
		return 0;
	}
#endif
	if ((dhandle = OF_finddevice(fname)) == -1)
		return ENOENT;

	DNPRINTF(BOOT_D_OFDEV, "devopen: found %s\n", fname);
	if (OF_getprop(dhandle, "name", buf, sizeof buf) < 0)
		return ENXIO;
	DNPRINTF(BOOT_D_OFDEV, "devopen: %s is called %s\n", fname, buf);
	if (OF_getprop(dhandle, "device_type", buf, sizeof buf) < 0)
		return ENXIO;
	DNPRINTF(BOOT_D_OFDEV, "devopen: %s is a %s device\n", fname, buf);
	DNPRINTF(BOOT_D_OFDEV, "devopen: opening %s\n", fname);
	if ((ihandle = OF_open(fname)) == -1) {
		DNPRINTF(BOOT_D_OFDEV, "devopen: open of %s failed\n", fname);
		return ENXIO;
	}
	DNPRINTF(BOOT_D_OFDEV, "devopen: %s is now open\n", fname);
	bzero(&ofdev, sizeof ofdev);
	ofdev.handle = ihandle;
	ofdev.type = OFDEV_DISK;
	ofdev.bsize = DEV_BSIZE;
	if (!strcmp(buf, "block")) {
		error = load_disklabel(&ofdev, &label);
		if (error && error != ERDLAB)
			goto bad;
		else if (error == ERDLAB) {
			if (partition)
				/* User specified a partition, but there is none */
				goto bad;
			/* No, label, just use complete disk */
			ofdev.partoff = 0;
		} else {
			part = partition ? partition - 'a' : 0;
			ofdev.partoff = label.d_partitions[part].p_offset;
			DNPRINTF(BOOT_D_OFDEV, "devopen: setting partition %d "
			    "offset %x\n", part, ofdev.partoff);
		}

		of->f_dev = devsw;
		of->f_devdata = &ofdev;

		/* Some PROMS have buggy writing code for IDE block devices */
		parent = OF_parent(dhandle);
		if (parent && OF_getprop(parent, "device_type", buf,
		    sizeof(buf)) > 0 && strcmp(buf, "ide") == 0) {
			DNPRINTF(BOOT_D_OFDEV,
			    "devopen: Disable writing for IDE block device\n");
			of->f_flags |= F_NOWRITE;
		}

#ifdef SPARC_BOOT_UFS
		bcopy(&file_system_ufs, &file_system[nfsys++], sizeof file_system[0]);
		bcopy(&file_system_ufs2, &file_system[nfsys++], sizeof file_system[0]);
#endif
#ifdef SPARC_BOOT_HSFS
		bcopy(&file_system_cd9660, &file_system[nfsys++],
		    sizeof file_system[0]);
#endif
		DNPRINTF(BOOT_D_OFDEV, "devopen: return 0\n");
		return 0;
	}
#ifdef NETBOOT
	if (!strcmp(buf, "network")) {
		ofdev.type = OFDEV_NET;
		of->f_dev = devsw;
		of->f_devdata = &ofdev;
		bcopy(&file_system_nfs, file_system, sizeof file_system[0]);
		nfsys = 1;
		if ((error = net_open(&ofdev)))
			goto bad;
		return 0;
	}
#endif
	error = EFTYPE;
bad:
	DNPRINTF(BOOT_D_OFDEV, "devopen: error %d, cannot open device\n",
	    error);
	OF_close(ihandle);
	ofdev.handle = -1;
	return error;
}
