/*	$OpenBSD: pdc.c,v 1.24 2023/01/06 19:05:46 miod Exp $	*/

/*
 * Copyright (c) 1998-2004 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright 1996 1995 by Open Software Foundation, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * OSF DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL OSF BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */
/*
 * Copyright (c) 1990 mt Xinu, Inc.  All rights reserved.
 * Copyright (c) 1990 University of Utah.  All rights reserved.
 *
 * This file may be freely distributed in any form as long as
 * this copyright notice is included.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Utah $Hdr: pdc.c 1.8 92/03/14$
 */

#include <sys/param.h>
#include <sys/time.h>
#include "libsa.h"
#include <sys/reboot.h>
#include <sys/disklabel.h>

#include <machine/trap.h>
#include <machine/pdc.h>
#include <machine/iomod.h>
#include <machine/cpufunc.h>

#include "dev_hppa.h"

/*
 * Interface routines to initialize and access the PDC.
 */

pdcio_t pdc;
int	pdcbuf[64] PDC_ALIGNMENT;/* PDC return buffer */
struct	stable_storage sstor;	/* contents of Stable Storage */
int	sstorsiz;		/* size of Stable Storage */

/*
 * Initialize PDC and related variables.
 */
void
pdc_init()
{
	int err;

	/*
	 * Initialize important global variables (defined above).
	 */
	pdc = (pdcio_t)PAGE0->mem_pdc;

	err = (*pdc)(PDC_STABLE, PDC_STABLE_SIZE, pdcbuf, 0, 0);
	if (err >= 0) {
		sstorsiz = min(pdcbuf[0],sizeof(sstor));
		err = (*pdc)(PDC_STABLE, PDC_STABLE_READ, 0, &sstor, sstorsiz);
	}

	/*
	 * Now that we (may) have an output device, if we encountered
	 * an error reading Stable Storage (above), let them know.
	 */
#ifdef DEBUG
	if (debug && err)
		printf("Stable storage PDC_STABLE Read Ret'd %d\n", err);
#endif

	/*
	 * Clear the FAULT light (so we know when we get a real one)
	 */
	(*pdc)(PDC_CHASSIS, PDC_CHASSIS_DISP,
	    PDC_OSTAT(PDC_OSTAT_BOOT) | 0xCEC0);
}

/*
 * Generic READ/WRITE through IODC.  Takes pointer to PDC device
 * information, returns (positive) number of bytes actually read or
 * the (negative) error condition, or zero if at "EOF".
 */
int
iodcstrategy(devdata, rw, blk, size, buf, rsize)
	void *devdata;
	int rw;
	daddr_t blk;
	size_t size;
	void *buf;
	size_t *rsize;
{
	struct hppa_dev *dp = devdata;
	struct pz_device *pzdev = dp->pz_dev;
	int	offset, xfer, ret;

#ifdef PDCDEBUG
	if (debug)
		printf("iodcstrategy(%p, %s, %u, %u, %p, %p)\n", devdata,
		    rw==F_READ? "READ" : "WRITE", blk, size, buf, rsize);

	if (debug > 1)
		PZDEV_PRINT(pzdev);
#endif

	blk += dp->fsoff;
	blk *= DEV_BSIZE;
	if ((pzdev->pz_class & PCL_CLASS_MASK) == PCL_SEQU) {
		/* rewind and re-read to seek */
		if (blk < dp->last_blk) {
#ifdef PDCDEBUG
			if (debug)
				printf("iodc: rewind ");
#endif
			if ((ret = ((iodcio_t)pzdev->pz_iodc_io)(pzdev->pz_hpa,
			    IODC_IO_READ, pzdev->pz_spa, pzdev->pz_layers,
			    pdcbuf, 0, dp->buf, 0, 0)) < 0) {
#ifdef DEBUG
				if (debug)
					printf("IODC_IO: %d\n", ret);
#endif
				return (EIO);
			} else {
				dp->last_blk = 0;
				dp->last_read = 0;
			}
		}

#ifdef PDCDEBUG
		if (debug)
			printf("seek %d ", dp->last_blk);
#endif
		for (; (dp->last_blk + dp->last_read) <= blk;
		     dp->last_read = ret) {
			twiddle();
			dp->last_blk += dp->last_read;
			if ((ret = ((iodcio_t)pzdev->pz_iodc_io)(pzdev->pz_hpa,
			    IODC_IO_READ, pzdev->pz_spa, pzdev->pz_layers,
			    pdcbuf, dp->last_blk, dp->buf, IODC_IOSIZ,
			    IODC_IOSIZ)) < 0) {
#ifdef DEBUG
				if (debug)
					printf("IODC_IO: %d\n", ret);
#endif
				return (EIO);
			}
			if ((ret = pdcbuf[0]) == 0)
				break;
#ifdef PDCDEBUG
			if (debug)
				printf("-");
#endif
		}
#ifdef PDCDEBUG
		if (debug)
			printf("> %d[%d]\n", dp->last_blk, dp->last_read);
#endif
	}

	xfer = 0;
	/* On read, see if we can scratch anything from buffer */
	if (rw == F_READ &&
	    dp->last_blk <= blk && (dp->last_blk + dp->last_read) > blk) {
		twiddle();
		offset = blk - dp->last_blk;
		xfer = min(dp->last_read - offset, size);
		size -= xfer;
		blk += xfer;
#ifdef PDCDEBUG
		if (debug)
			printf("off=%d,xfer=%d,size=%d,blk=%d\n",
			    offset, xfer, size, blk);
#endif
		bcopy(dp->buf + offset, buf, xfer);
		buf += xfer;
	}

	/*
	 * double buffer it all the time, to cache
	 */
	for (; size; size -= ret, buf += ret, blk += ret, xfer += ret) {
		offset = blk & IOPGOFSET;
		if (rw != F_READ) {
			/* put block into cache, but invalidate cache */
			bcopy(buf, dp->buf, size);
			dp->last_blk = 0;
			dp->last_read = 0;
		}
		if ((ret = ((iodcio_t)pzdev->pz_iodc_io)(pzdev->pz_hpa,
		    (rw == F_READ? IODC_IO_READ: IODC_IO_WRITE),
		    pzdev->pz_spa, pzdev->pz_layers, pdcbuf,
		    (u_int)blk - offset, dp->buf, IODC_IOSIZ, IODC_IOSIZ)) < 0) {
#ifdef DEBUG
			if (debug)
				printf("iodc_read(%d,%d): %d\n",
				    blk - offset, IODC_IOSIZ, ret);
#endif
			if (xfer)
				break;
			else
				return (EIO);
		}
		if ((ret = pdcbuf[0]) <= 0)
			break;
		if (rw == F_READ) {
			dp->last_blk = blk - offset;
			dp->last_read = ret;
			if ((ret -= offset) > size)
				ret = size;
			bcopy(dp->buf + offset, buf, ret);
		}
#ifdef PDCDEBUG
		if (debug)
			printf("read %d(%d,%d)@%x ", ret,
			    dp->last_blk, dp->last_read, (u_int)buf);
#endif
	    }

#ifdef PDCDEBUG
	if (debug)
		printf("\n");
#endif

	if (rsize)
		*rsize = xfer;
	return (0);
}

/*
 * Find a device with specified unit number
 * (any if unit == -1), and of specified class (PCL_*).
 */
struct pz_device *
pdc_findev(unit, class)
	int unit, class;
{
	static struct pz_device pz;
	int layers[sizeof(pz.pz_layers)/sizeof(pz.pz_layers[0])];
	struct iomod *io;
	iodcio_t iodc;
	int err = 0;

#ifdef	PDCDEBUG
	if (debug)
		printf("pdc_finddev(%d, %x)\n", unit, class);
#endif
	iodc = (iodcio_t)(PAGE0->mem_free + IODC_MAXSIZE);
	io = (struct iomod *)PAGE0->mem_boot.pz_hpa;

	/* quick hack for boot device */
	if (PAGE0->mem_boot.pz_class == class &&
	    (unit == -1 || PAGE0->mem_boot.pz_layers[0] == unit)) {

		bcopy (&PAGE0->mem_boot.pz_dp, &pz.pz_dp, sizeof(pz.pz_dp));
		bcopy (pz.pz_layers, layers, sizeof(layers));
		if ((err = (pdc)(PDC_IODC, PDC_IODC_READ, pdcbuf, io,
		    IODC_INIT, iodc, IODC_MAXSIZE)) < 0) {
#ifdef DEBUG
			if (debug)
				printf("IODC_READ: %d\n", err);
#endif
			return NULL;
		}
	} else {
		struct pdc_memmap memmap;
		struct iodc_data mptr;
		int i, stp;

		for (i = 0; i < 0xf; i++) {
			pz.pz_bc[0] = pz.pz_bc[1] =
			pz.pz_bc[2] = pz.pz_bc[3] = -1;
			pz.pz_bc[4] = 2;
			pz.pz_bc[5] = 0;	/* core bus */
			pz.pz_mod = i;
			if ((pdc)(PDC_MEMMAP, PDC_MEMMAP_HPA, &memmap,
			    &pz.pz_dp) < 0)
				continue;
#ifdef PDCDEBUG
			if (debug)
				printf("memap: %d.%d.%d, hpa=%x, mpgs=%x\n",
				    pz.pz_bc[4], pz.pz_bc[5], pz.pz_mod,
				    memmap.hpa, memmap.morepages);
#endif
			io = (struct iomod *) memmap.hpa;

			if ((err = (pdc)(PDC_IODC, PDC_IODC_READ, &pdcbuf, io,
			    IODC_DATA, &mptr, sizeof(mptr))) < 0) {
#ifdef DEBUG
				if (debug)
					printf("IODC_DATA: %d\n", err);
#endif
				continue;
			}

			if ((err = (pdc)(PDC_IODC, PDC_IODC_READ, pdcbuf, io,
			    IODC_INIT, iodc, IODC_MAXSIZE)) < 0) {
#ifdef DEBUG
				if (debug)
					printf("IODC_READ: %d\n", err);
#endif
				continue;
			}

			stp = IODC_INIT_FIRST;
			do {
				if ((err = (iodc)((u_int)io, stp, io->io_spa,
				    layers, pdcbuf, 0, 0, 0, 0)) < 0) {
#ifdef DEBUG
					if (debug && err != PDC_ERR_EOD)
						printf("IODC_INIT_%s: %d\n",
						    stp==IODC_INIT_FIRST?
						    "FIRST":"NEXT", err);
#endif
					break;
				}
#ifdef PDCDEBUG
				if (debug)
					printf("[%x,%x,%x,%x,%x,%x], "
					    "[%x,%x,%x,%x,%x,%x]\n",
					    pdcbuf[0], pdcbuf[1], pdcbuf[2],
					    pdcbuf[3], pdcbuf[4], pdcbuf[5],
					    layers[0], layers[1], layers[2],
					    layers[3], layers[4], layers[5]);
#endif
				stp = IODC_INIT_NEXT;

			} while (pdcbuf[1] != class &&
				 unit != -1 && unit != layers[0]);

			if (err >= 0)
				break;
		}
	}

	if (err >= 0) {
		/* init device */
		if (0  && (err = (iodc)((u_int)io, IODC_INIT_DEV, io->io_spa,
		    layers, pdcbuf, 0, 0, 0, 0)) < 0) {
#ifdef DEBUG
			if (debug)
				printf("INIT_DEV: %d\n", err);
#endif
			return NULL;
		}

		/* read i/o entry code */
		if ((err = (pdc)(PDC_IODC, PDC_IODC_READ, pdcbuf, io,
		    IODC_IO, iodc, IODC_MAXSIZE)) < 0) {
#ifdef DEBUG
			if (debug)
				printf("IODC_READ: %d\n", err);
#endif
			return NULL;
		}

		pz.pz_flags = 0;
		bcopy(layers, pz.pz_layers, sizeof(pz.pz_layers));
		pz.pz_hpa = (u_int)io;
/* XXX		pz.pz_spa = io->io_spa; */
		pz.pz_iodc_io = (u_int)iodc;
		pz.pz_class = class;

		return &pz;
	}

	return NULL;
}

static __inline void
fall(int c_base, int c_count, int c_loop, int c_stride, int data)
{
        int loop;                  /* Internal vars */

        for (; c_count--; c_base += c_stride)
                for (loop = c_loop; loop--; )
			if (data)
				fdce(0, c_base);
			else
				fice(0, c_base);
}

/*
 * fcacheall - Flush all caches.
 *
 * This routine is just a wrapper around the real cache flush routine.
 */
struct pdc_cache pdc_cacheinfo PDC_ALIGNMENT;

void
fcacheall()
{
	int err;

        if ((err = (*pdc)(PDC_CACHE, PDC_CACHE_DFLT, &pdc_cacheinfo)) < 0) {
#ifdef DEBUG
		if (debug)
			printf("fcacheall: PDC_CACHE failed (%d).\n", err);
#endif
		return;
        }
#if PDCDEBUG
	if (debug)
		printf("pdc_cache:\nic={%u,%x,%x,%u,%u,%u}\n"
		       "dc={%u,%x,%x,%u,%u,%u}\n",
		       pdc_cacheinfo.ic_size, *(u_int *)&pdc_cacheinfo.ic_conf,
		       pdc_cacheinfo.ic_base, pdc_cacheinfo.ic_stride,
		       pdc_cacheinfo.ic_count, pdc_cacheinfo.ic_loop,
		       pdc_cacheinfo.dc_size, *(u_int *)&pdc_cacheinfo.ic_conf,
		       pdc_cacheinfo.dc_base, pdc_cacheinfo.dc_stride,
		       pdc_cacheinfo.dc_count, pdc_cacheinfo.dc_loop);
#endif
        /*
         * Flush the instruction, then data cache.
         */
        fall(pdc_cacheinfo.ic_base, pdc_cacheinfo.ic_count,
	     pdc_cacheinfo.ic_loop, pdc_cacheinfo.ic_stride, 0);
	sync_caches();
        fall(pdc_cacheinfo.dc_base, pdc_cacheinfo.dc_count,
	     pdc_cacheinfo.dc_loop, pdc_cacheinfo.dc_stride, 1);
	sync_caches();
}
