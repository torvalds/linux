/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/bio.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandbus.h>
#include <dev/nand/nand_dev.h>
#include "nand_if.h"
#include "nandbus_if.h"

static int nand_page_stat(struct nand_chip *, struct page_stat_io *);
static int nand_block_stat(struct nand_chip *, struct block_stat_io *);

static d_ioctl_t nand_ioctl;
static d_open_t nand_open;
static d_strategy_t nand_strategy;

static struct cdevsw nand_cdevsw = {
	.d_version	= D_VERSION,
	.d_name		= "nand",
	.d_open		= nand_open,
	.d_read		= physread,
	.d_write	= physwrite,
	.d_ioctl	= nand_ioctl,
	.d_strategy =	nand_strategy,
};

static int
offset_to_page(struct chip_geom *cg, uint32_t offset)
{

	return (offset / cg->page_size);
}

static int
offset_to_page_off(struct chip_geom *cg, uint32_t offset)
{

	return (offset % cg->page_size);
}

int
nand_make_dev(struct nand_chip *chip)
{
	struct nandbus_ivar *ivar;
	device_t parent, nandbus;
	int parent_unit, unit;
	char *name;

	ivar = device_get_ivars(chip->dev);
	nandbus = device_get_parent(chip->dev);

	if (ivar->chip_cdev_name) {
		name = ivar->chip_cdev_name;

		/*
		 * If we got distinct name for chip device we can enumarete it
		 * based on contoller number.
		 */
		parent = device_get_parent(nandbus);
	} else {
		name = "nand";
		parent = nandbus;
	}

	parent_unit = device_get_unit(parent);
	unit = parent_unit * 4 + chip->num;
	chip->cdev = make_dev(&nand_cdevsw, unit, UID_ROOT, GID_WHEEL,
	    0666, "%s%d.%d", name, parent_unit, chip->num);

	if (chip->cdev == NULL)
		return (ENXIO);

	if (bootverbose)
		device_printf(chip->dev, "Created cdev %s%d.%d for chip "
		    "[0x%0x, 0x%0x]\n", name, parent_unit, chip->num,
		    ivar->man_id, ivar->dev_id);

	chip->cdev->si_drv1 = chip;

	return (0);
}

void
nand_destroy_dev(struct nand_chip *chip)
{

	if (chip->cdev)
		destroy_dev(chip->cdev);
}

static int
nand_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{

	return (0);
}

static int
nand_read(struct nand_chip *chip, uint32_t offset, void *buf, uint32_t len)
{
	struct chip_geom *cg;
	device_t nandbus;
	int start_page, count, off, err = 0;
	uint8_t *ptr, *tmp;

	nand_debug(NDBG_CDEV, "Read from chip%d [%p] at %d\n", chip->num,
	    chip, offset);

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	cg = &chip->chip_geom;
	start_page = offset_to_page(cg, offset);
	off = offset_to_page_off(cg, offset);
	count = (len > cg->page_size - off) ? cg->page_size - off : len;

	ptr = (uint8_t *)buf;
	while (len > 0) {
		if (len < cg->page_size) {
			tmp = malloc(cg->page_size, M_NAND, M_WAITOK);
			if (!tmp) {
				err = ENOMEM;
				break;
			}
			err = NAND_READ_PAGE(chip->dev, start_page,
			    tmp, cg->page_size, 0);
			if (err) {
				free(tmp, M_NAND);
				break;
			}
			bcopy(tmp + off, ptr, count);
			free(tmp, M_NAND);
		} else {
			err = NAND_READ_PAGE(chip->dev, start_page,
			    ptr, cg->page_size, 0);
			if (err)
				break;
		}

		len -= count;
		start_page++;
		ptr += count;
		count = (len > cg->page_size) ? cg->page_size : len;
		off = 0;
	}

	NANDBUS_UNLOCK(nandbus);
	return (err);
}

static int
nand_write(struct nand_chip *chip, uint32_t offset, void* buf, uint32_t len)
{
	struct chip_geom *cg;
	device_t nandbus;
	int off, start_page, err = 0;
	uint8_t *ptr;

	nand_debug(NDBG_CDEV, "Write to chip %d [%p] at %d\n", chip->num,
	    chip, offset);

	nandbus = device_get_parent(chip->dev);
	NANDBUS_LOCK(nandbus);
	NANDBUS_SELECT_CS(device_get_parent(chip->dev), chip->num);

	cg = &chip->chip_geom;
	start_page = offset_to_page(cg, offset);
	off = offset_to_page_off(cg, offset);

	if (off != 0 || (len % cg->page_size) != 0) {
		printf("Not aligned write start [0x%08x] size [0x%08x]\n",
		    off, len);
		NANDBUS_UNLOCK(nandbus);
		return (EINVAL);
	}

	ptr = (uint8_t *)buf;
	while (len > 0) {
		err = NAND_PROGRAM_PAGE(chip->dev, start_page, ptr,
		    cg->page_size, 0);
		if (err)
			break;

		len -= cg->page_size;
		start_page++;
		ptr += cg->page_size;
	}

	NANDBUS_UNLOCK(nandbus);
	return (err);
}

static void
nand_strategy(struct bio *bp)
{
	struct nand_chip *chip;
	struct cdev *dev;
	int err = 0;

	dev = bp->bio_dev;
	chip = dev->si_drv1;

	nand_debug(NDBG_CDEV, "Strategy %s on chip %d [%p]\n",
	    bp->bio_cmd == BIO_READ ? "READ" : "WRITE",
	    chip->num, chip);

	if (bp->bio_cmd == BIO_READ) {
		err = nand_read(chip,
		    bp->bio_offset & 0xffffffff,
		    bp->bio_data, bp->bio_bcount);
	} else {
		err = nand_write(chip,
		    bp->bio_offset & 0xffffffff,
		    bp->bio_data, bp->bio_bcount);
	}

	if (err == 0)
		bp->bio_resid = 0;
	else {
		bp->bio_error = EIO;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = bp->bio_bcount;
	}

	biodone(bp);
}

static int
nand_oob_access(struct nand_chip *chip, uint32_t page, uint32_t offset,
    uint32_t len, uint8_t *data, uint8_t write)
{
	struct chip_geom *cg;
	uint8_t *buf = NULL;
	int ret = 0;

	cg = &chip->chip_geom;

	buf = malloc(cg->oob_size, M_NAND, M_WAITOK);
	if (!buf)
		return (ENOMEM);

	memset(buf, 0xff, cg->oob_size);

	if (!write) {
		ret = nand_read_oob(chip, page, buf, cg->oob_size);
		copyout(buf, data, len);
	} else {
		copyin(data, buf, len);
		ret = nand_prog_oob(chip, page, buf, cg->oob_size);
	}

	free(buf, M_NAND);

	return (ret);
}

static int
nand_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct nand_chip *chip;
	struct chip_geom  *cg;
	struct nand_oob_rw *oob_rw = NULL;
	struct nand_raw_rw *raw_rw = NULL;
	device_t nandbus;
	size_t bufsize = 0, len = 0;
	size_t raw_size;
	off_t off;
	uint8_t *buf = NULL;
	int ret = 0;
	uint8_t status;

	chip = (struct nand_chip *)dev->si_drv1;
	cg = &chip->chip_geom;
	nandbus = device_get_parent(chip->dev);

	if ((cmd == NAND_IO_RAW_READ) || (cmd == NAND_IO_RAW_PROG)) {
		raw_rw = (struct nand_raw_rw *)data;
		raw_size =  cg->pgs_per_blk * (cg->page_size + cg->oob_size);

		/* Check if len is not bigger than chip size */
		if (raw_rw->len > raw_size)
			return (EFBIG);

		/*
		 * Do not ask for too much memory, in case of large transfers
		 * read/write in 16-pages chunks
		 */
		bufsize = 16 * (cg->page_size + cg->oob_size);
		if (raw_rw->len < bufsize)
			bufsize = raw_rw->len;

		buf = malloc(bufsize, M_NAND, M_WAITOK);
		len = raw_rw->len;
		off = 0;
	}
	switch(cmd) {
	case NAND_IO_ERASE:
		ret = nand_erase_blocks(chip, ((off_t *)data)[0],
		    ((off_t *)data)[1]);
		break;

	case NAND_IO_OOB_READ:
		oob_rw = (struct nand_oob_rw *)data;
		ret = nand_oob_access(chip, oob_rw->page, 0,
		    oob_rw->len, oob_rw->data, 0);
		break;

	case NAND_IO_OOB_PROG:
		oob_rw = (struct nand_oob_rw *)data;
		ret = nand_oob_access(chip, oob_rw->page, 0,
		    oob_rw->len, oob_rw->data, 1);
		break;

	case NAND_IO_GET_STATUS:
		NANDBUS_LOCK(nandbus);
		ret = NANDBUS_GET_STATUS(nandbus, &status);
		if (ret == 0)
			*(uint8_t *)data = status;
		NANDBUS_UNLOCK(nandbus);
		break;

	case NAND_IO_RAW_PROG:
		while (len > 0) {
			if (len < bufsize)
				bufsize = len;
			ret = copyin(raw_rw->data + off, buf, bufsize);
			if (ret)
				break;
			ret = nand_prog_pages_raw(chip, raw_rw->off + off, buf,
			    bufsize);
			if (ret)
				break;
			len -= bufsize;
			off += bufsize;
		}
		break;

	case NAND_IO_RAW_READ:
		while (len > 0) {
			if (len < bufsize)
				bufsize = len;

			ret = nand_read_pages_raw(chip, raw_rw->off + off, buf,
			    bufsize);
			if (ret)
				break;

			ret = copyout(buf, raw_rw->data + off, bufsize);
			if (ret)
				break;
			len -= bufsize;
			off += bufsize;
		}
		break;

	case NAND_IO_PAGE_STAT:
		ret = nand_page_stat(chip, (struct page_stat_io *)data);
		break;

	case NAND_IO_BLOCK_STAT:
		ret = nand_block_stat(chip, (struct block_stat_io *)data);
		break;

	case NAND_IO_GET_CHIP_PARAM:
		nand_get_chip_param(chip, (struct chip_param_io *)data);
		break;

	default:
		printf("Unknown nand_ioctl request \n");
		ret = EIO;
	}

	if (buf)
		free(buf, M_NAND);

	return (ret);
}

static int
nand_page_stat(struct nand_chip *chip, struct page_stat_io *page_stat)
{
	struct chip_geom *cg;
	struct page_stat *stat;
	int num_pages;

	cg = &chip->chip_geom;
	num_pages = cg->pgs_per_blk * cg->blks_per_lun * cg->luns;
	if (page_stat->page_num >= num_pages)
		return (EINVAL);

	stat = &chip->pg_stat[page_stat->page_num];
	page_stat->page_read = stat->page_read;
	page_stat->page_written = stat->page_written;
	page_stat->page_raw_read = stat->page_raw_read;
	page_stat->page_raw_written = stat->page_raw_written;
	page_stat->ecc_succeded = stat->ecc_stat.ecc_succeded;
	page_stat->ecc_corrected = stat->ecc_stat.ecc_corrected;
	page_stat->ecc_failed = stat->ecc_stat.ecc_failed;

	return (0);
}

static int
nand_block_stat(struct nand_chip *chip, struct block_stat_io *block_stat)
{
	struct chip_geom *cg;
	uint32_t block_num = block_stat->block_num;

	cg = &chip->chip_geom;
	if (block_num >= cg->blks_per_lun * cg->luns)
		return (EINVAL);

	block_stat->block_erased = chip->blk_stat[block_num].block_erased;

	return (0);
}
