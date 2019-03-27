/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Ian Lepore <ian@FreeBSD.org>
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

#include <stand.h>
#include <stdarg.h>
#include <uuid.h>
#include <sys/disk.h>
#include "disk.h"
#include "geliboot.h"
#include "geliboot_internal.h"

static int  geli_dev_init(void);
static int  geli_dev_strategy(void *, int, daddr_t, size_t, char *, size_t *);
static int  geli_dev_open(struct open_file *f, ...);
static int  geli_dev_close(struct open_file *f);
static int  geli_dev_ioctl(struct open_file *, u_long, void *);
static int  geli_dev_print(int);
static void geli_dev_cleanup(void);

/*
 * geli_devsw is static because it never appears in any arch's global devsw
 * array.  Instead, when devopen() opens a DEVT_DISK device, it then calls
 * geli_probe_and_attach(), and if we find that the disk_devdesc describes a
 * geli-encrypted partition, we create a geli_devdesc which references this
 * devsw and has a pointer to the original disk_devdesc of the underlying host
 * disk. Then we manipulate the open_file struct to reference the new
 * geli_devdesc, effectively routing all IO operations through our code.
 */
static struct devsw geli_devsw = {
	.dv_name     = "gelidisk",
	.dv_type     = DEVT_DISK,
	.dv_init     = geli_dev_init,
	.dv_strategy = geli_dev_strategy,
	.dv_open     = geli_dev_open,
	.dv_close    = geli_dev_close,
	.dv_ioctl    = geli_dev_ioctl,
	.dv_print    = geli_dev_print,
	.dv_cleanup  = geli_dev_cleanup,
};

/*
 * geli_devdesc instances replace the disk_devdesc in an open_file struct when
 * the partition is encrypted.  We keep a reference to the original host
 * disk_devdesc so that we can read the raw encrypted data using it.
 */
struct geli_devdesc {
	struct disk_devdesc	ddd;	/* Must be first. */
	struct disk_devdesc	*hdesc;	/* disk/slice/part hosting geli vol */
	struct geli_dev		*gdev;	/* geli_dev entry */
};


/*
 * A geli_readfunc that reads via a disk_devdesc passed in readpriv. This is
 * used to read the underlying host disk data when probing/tasting to see if the
 * host provider is geli-encrypted.
 */
static int
diskdev_read(void *vdev, void *readpriv, off_t offbytes,
    void *buf, size_t sizebytes)
{
	struct disk_devdesc *ddev;

	ddev = (struct disk_devdesc *)readpriv;

	return (ddev->dd.d_dev->dv_strategy(ddev, F_READ, offbytes / DEV_BSIZE,
	    sizebytes, buf, NULL));
}

static int
geli_dev_init(void)
{

	/*
	 * Since geli_devsw never gets referenced in any arch's global devsw
	 * table, this function should never get called.
	 */
	panic("%s: should never be called", __func__);
	return (ENXIO);
}

static int
geli_dev_strategy(void *devdata, int rw, daddr_t blk, size_t size, char *buf,
    size_t *rsize)
{
	struct geli_devdesc *gdesc;
	off_t alnend, alnstart, reqend, reqstart;
	size_t alnsize;
	char *iobuf;
	int rc;

	/* We only handle reading; no write support. */
	if ((rw & F_MASK) != F_READ)
		return (EOPNOTSUPP);

	gdesc = (struct geli_devdesc *)devdata;

	/*
	 * We can only decrypt full geli blocks.  The blk arg is expressed in
	 * units of DEV_BSIZE blocks, while size is in bytes.  Convert
	 * everything to bytes, and calculate the geli-blocksize-aligned start
	 * and end points.
	 *
	 * Note: md_sectorsize must be cast to a signed type for the round2
	 * macros to work correctly (otherwise they get zero-extended to 64 bits
	 * and mask off the high order 32 bits of the requested start/end).
	 */

	reqstart = blk * DEV_BSIZE;
	reqend   = reqstart + size;
	alnstart = rounddown2(reqstart, (int)gdesc->gdev->md.md_sectorsize);
	alnend   = roundup2(reqend, (int)gdesc->gdev->md.md_sectorsize);
	alnsize  = alnend - alnstart;

	/*
	 * If alignment requires us to read more than the size of the provided
	 * buffer, allocate a temporary buffer.
	 */
	if (alnsize <= size)
		iobuf = buf;
	else if ((iobuf = malloc(alnsize)) == NULL)
		return (ENOMEM);

	/*
	 * Read the encrypted data using the host provider, then decrypt it.
	 */
	rc = gdesc->hdesc->dd.d_dev->dv_strategy(gdesc->hdesc, rw,
	    alnstart / DEV_BSIZE, alnsize, iobuf, NULL);
	if (rc != 0)
		goto out;
	rc = geli_read(gdesc->gdev, alnstart, iobuf, alnsize);
	if (rc != 0)
		goto out;

	/*
	 * If we had to use a temporary buffer, copy the requested part of the
	 * data to the caller's buffer.
	 */
	if (iobuf != buf)
		memcpy(buf, iobuf + (reqstart - alnstart), size);

	if (rsize != NULL)
		*rsize = size;
out:
	if (iobuf != buf)
		free(iobuf);

	return (rc);
}

static int
geli_dev_open(struct open_file *f, ...)
{

	/*
	 * Since geli_devsw never gets referenced in any arch's global devsw
	 * table, this function should never get called.
	 */
	panic("%s: should never be called", __func__);
	return (ENXIO);
}

static int
geli_dev_close(struct open_file *f)
{
	struct geli_devdesc *gdesc;

	/*
	 * Detach the geli_devdesc from the open_file and reattach the
	 * underlying host provider's disk_devdesc; this undoes the work done at
	 * the end of geli_probe_and_attach().  Call the host provider's
	 * dv_close() (because that's what our caller thought it was doing).
	 */
	gdesc = (struct geli_devdesc *)f->f_devdata;
	f->f_devdata = gdesc->hdesc;
	f->f_dev = gdesc->hdesc->dd.d_dev;
	free(gdesc);
	f->f_dev->dv_close(f);
	return (0);
}

static int
geli_dev_ioctl(struct open_file *f, u_long cmd, void *data)
{
	struct geli_devdesc *gdesc;
	struct g_eli_metadata *md;

	gdesc = (struct geli_devdesc *)f->f_devdata;
	md = &gdesc->gdev->md;

	switch (cmd) {
	case DIOCGSECTORSIZE:
		*(u_int *)data = md->md_sectorsize;
		break;
	case DIOCGMEDIASIZE:
		*(uint64_t *)data = md->md_sectorsize * md->md_provsize;
		break;
	default:
		return (ENOTTY);
	}

	return (0);
}

static int
geli_dev_print(int verbose)
{

	/*
	 * Since geli_devsw never gets referenced in any arch's global devsw
	 * table, this function should never get called.
	 */
	panic("%s: should never be called", __func__);
	return (ENXIO);
}

static void
geli_dev_cleanup(void)
{

	/*
	 * Since geli_devsw never gets referenced in any arch's global devsw
	 * table, this function should never get called.
	 */
	panic("%s: should never be called", __func__);
}


/*
 * geli_probe_and_attach() is called from devopen() after it successfully calls
 * the dv_open() method of a DEVT_DISK device.  We taste the partition described
 * by the disk_devdesc, and if it's geli-encrypted and we can decrypt it, we
 * create a geli_devdesc and store it into the open_file struct in place of the
 * underlying provider's disk_devdesc, effectively attaching our code to all IO
 * processing for the partition.  Not quite the elegant stacking provided by
 * geom in the kernel, but it gets the job done.
 */
void
geli_probe_and_attach(struct open_file *f)
{
	static char gelipw[GELI_PW_MAXLEN];
	const char *envpw;
	struct geli_dev *gdev;
	struct geli_devdesc *gdesc;
	struct disk_devdesc *hdesc;
	uint64_t hmediasize;
	daddr_t hlastblk;
	int rc;

	hdesc = (struct disk_devdesc *)(f->f_devdata);

	/* Get the last block number for the host provider. */
	hdesc->dd.d_dev->dv_ioctl(f, DIOCGMEDIASIZE, &hmediasize);
	hlastblk = (hmediasize / DEV_BSIZE) - 1;

	/* Taste the host provider.  If it's not geli-encrypted just return. */
	gdev = geli_taste(diskdev_read, hdesc, hlastblk, disk_fmtdev(hdesc));
	if (gdev == NULL)
		return;

	/*
	 * It's geli, try to decrypt it with existing keys, or prompt for a
	 * passphrase if we don't yet have a cached key for it.
	 */
	if ((rc = geli_havekey(gdev)) != 0) {
		envpw = getenv("kern.geom.eli.passphrase");
		if (envpw != NULL) {
			/* Use the cached passphrase */
			bcopy(envpw, &gelipw, GELI_PW_MAXLEN);
		}
		if ((rc = geli_passphrase(gdev, gelipw)) == 0) {
			/* Passphrase is good, cache it. */
			setenv("kern.geom.eli.passphrase", gelipw, 1);
		}
		explicit_bzero(gelipw, sizeof(gelipw));
		if (rc != 0)
			return;
	}

	/*
	 * It's geli-encrypted and we can decrypt it.  Create a geli_devdesc,
	 * store a reference to the underlying provider's disk_devdesc in it,
	 * then attach it to the openfile struct in place of the host provider.
	 */
	if ((gdesc = malloc(sizeof(*gdesc))) == NULL)
		return;
	gdesc->ddd.dd.d_dev = &geli_devsw;
	gdesc->ddd.dd.d_opendata = NULL;
	gdesc->ddd.dd.d_unit = hdesc->dd.d_unit;
	gdesc->ddd.d_offset = hdesc->d_offset;
	gdesc->ddd.d_partition = hdesc->d_partition;
	gdesc->ddd.d_slice = hdesc->d_slice;
	gdesc->hdesc = hdesc;
	gdesc->gdev = gdev;
	f->f_dev = gdesc->ddd.dd.d_dev;
	f->f_devdata = gdesc;
}
