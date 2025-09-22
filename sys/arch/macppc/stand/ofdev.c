/*	$OpenBSD: ofdev.c,v 1.29 2022/12/28 07:40:23 jca Exp $	*/
/*	$NetBSD: ofdev.c,v 1.1 1997/04/16 20:29:20 thorpej Exp $	*/

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
#include <lib/libkern/libkern.h>
#include <sys/disklabel.h>
#include <netinet/in.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/ufs2.h>
#include <lib/libsa/cd9660.h>
#include <lib/libsa/nfs.h>
#include <hfs.h>

#include <macppc/stand/ofdev.h>
#include <macppc/stand/openfirm.h>

extern char bootdev[];

char opened_name[256];

/*
 * this function is passed [device specifier]:[kernel]
 */
static int
parsename(char *str, char **file)
{
	char *cp;
	int aliases;
	size_t len;

	cp = strrchr(str, ':');
	if (cp == NULL)
		return 1;

	*cp++ = 0;

	if ((aliases = OF_finddevice("/aliases")) == -1 ||
	    OF_getprop(aliases, str, opened_name, sizeof opened_name) < 0)
		strlcpy(opened_name, str, sizeof opened_name);

	len = strlcat(opened_name, ":", sizeof opened_name);
	if (*cp != '/')
		strlcat(opened_name, "/", sizeof opened_name);

	if (strlcat(opened_name, cp, sizeof opened_name) >= sizeof opened_name)
		return 1;

	*file = opened_name + len + 1;

	return 0;
}

static int
strategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	struct of_dev *dev = devdata;
	u_quad_t pos;
	int n;

	if (rw != F_READ)
		return EPERM;
	if (dev->type != OFDEV_DISK)
		panic("strategy");

	pos = (u_quad_t)(blk + dev->partoff) * dev->bsize;

	for (;;) {
		if (OF_seek(dev->handle, pos) < 0)
			break;
		n = OF_read(dev->handle, buf, size);
		if (n == -2)
			continue;
		if (n < 0)
			break;
		*rsize = n;
		return 0;
	}
	return EIO;
}

static int
devclose(struct open_file *of)
{
	struct of_dev *op = of->f_devdata;

	if (op->type == OFDEV_NET)
		net_close(op);
	if (op->dmabuf)
		OF_call_method("dma-free", op->handle, 2, 0,
		    op->dmabuf, MAXBSIZE);

	OF_close(op->handle);
	free(op, sizeof *op);
	return 0;
}

struct devsw devsw[1] = {
	"OpenFirmware",
	strategy,
	(int (*)(struct open_file *, ...))nodev,
	devclose,
	noioctl
};
int ndevs = sizeof devsw / sizeof devsw[0];

static struct fs_ops file_system_ufs = {
	ufs_open, ufs_close, ufs_read, ufs_write, ufs_seek, ufs_stat,
	ufs_readdir
};
static struct fs_ops file_system_ufs2 = {
	ufs2_open, ufs2_close, ufs2_read, ufs2_write, ufs2_seek, ufs2_stat,
	ufs2_readdir
};
static struct fs_ops file_system_cd9660 = {
	cd9660_open, cd9660_close, cd9660_read, cd9660_write, cd9660_seek,
	cd9660_stat, cd9660_readdir
};
static struct fs_ops file_system_hfs = {
	hfs_open, hfs_close, hfs_read, hfs_write, hfs_seek, hfs_stat,
	hfs_readdir
};
static struct fs_ops file_system_nfs = {
	nfs_open, nfs_close, nfs_read, nfs_write, nfs_seek, nfs_stat,
	nfs_readdir
};

struct fs_ops file_system[4];
int nfsys;

static u_long
get_long(const void *p)
{
	const unsigned char *cp = p;

	return cp[0] | (cp[1] << 8) | (cp[2] << 16) | (cp[3] << 24);
}

int
read_mac_label(struct of_dev *devp, char *buf, struct disklabel *lp)
{
	struct part_map_entry *part;
	size_t read;
	int part_cnt;
	int i;
	char *s;

	if ((strategy(devp, F_READ, 1, DEV_BSIZE, buf, &read) != 0) ||
	    (read != DEV_BSIZE))
		return ERDLAB;

	part = (struct part_map_entry *)buf;

	/* if first partition is not valid, assume not HFS/DPME partitioned */
	if (part->pmSig != PART_ENTRY_MAGIC)
		return ERDLAB;

	part_cnt = part->pmMapBlkCnt;

	/* first search for "OpenBSD" partition type
	 * standard bsd disklabel lives inside at offset 0
	 * otherwise, we should fake a bsd partition
	 * with first HFS partition starting at 'i'
	 * ? will this cause problems with booting bsd.rd from hfs
	 */
	for (i = 0; i < part_cnt; i++) {
		/* read the appropriate block */
		if ((strategy(devp, F_READ, 1+i, DEV_BSIZE, buf, &read) != 0)
		   || (read != DEV_BSIZE))
			return ERDLAB;

		part = (struct part_map_entry *)buf;
		/* toupper the string, in case caps are different... */
		for (s = part->pmPartType; *s; s++)
			if ((*s >= 'a') && (*s <= 'z'))
				*s = (*s - 'a' + 'A');

		if (0 == strcmp(part->pmPartType, PART_TYPE_OPENBSD)) {
			/* FOUND OUR PARTITION!!! */
			if(strategy(devp, F_READ, part->pmPyPartStart,
				DEV_BSIZE, buf, &read) == 0
				&& read == DEV_BSIZE)
			{
				if (!getdisklabel(buf, lp))
					return 0;

				/* If we have an OpenBSD region
				 * but no valid partition table,
				 * we cannot load a kernel from
				 * it, punt.
				 * should not have more than one
				 * OpenBSD of DPME type.
				 */
				return ERDLAB;
			}
		}
	}
	return ERDLAB;
}

/*
 * Find a valid disklabel.
 */
static int
search_label(struct of_dev *devp, u_long off, char *buf, struct disklabel *lp,
    u_long off0)
{
	size_t read;
	struct dos_partition *p;
	int i;
	u_long poff;
	static int recursion;

	if (strategy(devp, F_READ, off, DEV_BSIZE, buf, &read) ||
	    read != DEV_BSIZE)
		return ERDLAB;

	if (buf[510] != 0x55 || buf[511] != 0xaa)
		return ERDLAB;

	if (recursion++ <= 1)
		off0 += off;
	for (p = (struct dos_partition *)(buf + DOSPARTOFF), i = 4;
	    --i >= 0; p++) {
		if (p->dp_typ == DOSPTYP_OPENBSD) {
			poff = get_long(&p->dp_start) + off0;
			if (strategy(devp, F_READ, poff + LABELSECTOR,
			    DEV_BSIZE, buf, &read) == 0
			    && read == DEV_BSIZE) {
				if (!getdisklabel(buf, lp)) {
					recursion--;
					return 0;
				}
			}
			if (strategy(devp, F_READ, off, DEV_BSIZE, buf, &read)
			    || read != DEV_BSIZE) {
				recursion--;
				return ERDLAB;
			}
		} else if (p->dp_typ == DOSPTYP_EXTEND) {
			poff = get_long(&p->dp_start);
			if (!search_label(devp, poff, buf, lp, off0)) {
				recursion--;
				return 0;
			}
			if (strategy(devp, F_READ, off, DEV_BSIZE, buf, &read)
			    || read != DEV_BSIZE) {
				recursion--;
				return ERDLAB;
			}
		}
	}
	recursion--;
	return ERDLAB;
}

int
devopen(struct open_file *of, const char *name, char **file)
{
	struct of_dev *ofdev;
	char fname[256];
	char buf[DEV_BSIZE];
	struct disklabel label;
	int handle, part;
	size_t read;
	int error = 0;

	if (of->f_flags != F_READ)
		return EPERM;

	strlcpy(fname, name, sizeof fname);
	if (parsename(fname, file))
		return ENOENT;

	if ((handle = OF_finddevice(fname)) == -1)
		return ENOENT;
	if (OF_getprop(handle, "name", buf, sizeof buf) < 0)
		return ENXIO;
	if (OF_getprop(handle, "device_type", buf, sizeof buf) < 0)
		return ENXIO;
	if (!strcmp(buf, "block"))
		/*
		 * For block devices, indicate raw partition
		 * (:0 in OpenFirmware)
		 */
		strlcat(fname, ":0", sizeof fname);
	if ((ofdev = alloc(sizeof *ofdev)) == NULL)
		return ENOMEM;
	bzero(ofdev, sizeof *ofdev);

	if ((handle = OF_open(fname)) == -1) {
		free(ofdev, sizeof *ofdev);
		return ENXIO;
	}

	ofdev->handle = handle;
	ofdev->dmabuf = NULL;
	OF_call_method("dma-alloc", handle, 1, 1, MAXBSIZE, &ofdev->dmabuf);
	if (!strcmp(buf, "block")) {
		ofdev->type = OFDEV_DISK;
		ofdev->bsize = DEV_BSIZE;
		/* First try to find a disklabel without MBR partitions */
		if (strategy(ofdev, F_READ,
		    LABELSECTOR, DEV_BSIZE, buf, &read) != 0 ||
		    read != DEV_BSIZE ||
		    getdisklabel(buf, &label)) {
			/* Else try MBR partitions */
			error = read_mac_label(ofdev, buf, &label);
			if (error == ERDLAB)
				error = search_label(ofdev, 0, buf, &label, 0);

			if (error && error != ERDLAB)
				goto bad;
		}

		if (error == ERDLAB) {
			/* No label, just use complete disk */
			ofdev->partoff = 0;
		} else {
			part = 0; /* how to pass this parameter */
			ofdev->partoff = label.d_partitions[part].p_offset;
		}

		of->f_dev = devsw;
		of->f_devdata = ofdev;
		bcopy(&file_system_ufs, file_system, sizeof file_system[0]);
		bcopy(&file_system_ufs2, file_system + 1, sizeof file_system[0]);
		bcopy(&file_system_cd9660, file_system + 2,
		    sizeof file_system[0]);
		bcopy(&file_system_hfs, file_system + 3,
		    sizeof file_system[0]);
		nfsys = 4;
		return 0;
	}
	if (!strcmp(buf, "network")) {
		ofdev->type = OFDEV_NET;
		of->f_dev = devsw;
		of->f_devdata = ofdev;
		bcopy(&file_system_nfs, file_system, sizeof file_system[0]);
		nfsys = 1;
		if ((error = net_open(ofdev)))
			goto bad;
		return 0;
	}
	error = EFTYPE;
bad:
	OF_close(handle);
	free(ofdev, sizeof *ofdev);
	return error;
}
