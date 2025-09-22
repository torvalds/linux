/*	$OpenBSD: sd.c,v 1.7 2023/06/18 12:58:03 aoyama Exp $	*/
/*	$NetBSD: sd.c,v 1.5 2013/01/22 15:48:40 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sd.c	8.1 (Berkeley) 6/10/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sd.c	8.1 (Berkeley) 6/10/93
 */

/*
 * sd.c -- SCSI DISK device driver
 * by A.Fujita, FEB-26-1992
 */


/*
 * SCSI CCS (Command Command Set) disk driver.
 */
#include <sys/param.h>
#include <sys/disklabel.h>
#include <luna88k/stand/boot/samachdep.h>
#include <luna88k/stand/boot/scsireg.h>
#include <luna88k/stand/boot/scsivar.h>

struct	sd_softc {
	uint	sc_ctlr;
	uint	sc_tgt;
	uint	sc_part;

	short	sc_type;	/* drive type */
	short	sc_punit;	/* physical unit (scsi lun) */
	u_short	sc_bshift;	/* convert device blocks to DEV_BSIZE blks */
	daddr32_t sc_blks;	/* number of blocks on device */
	int	sc_blksize;	/* device block size in bytes */

	struct disklabel sc_label;
	struct scsi_softc sc_sc;
};

struct sd_softc *sdinit(uint, uint);
static int sdident(struct sd_softc *);

static struct scsi_inquiry inqbuf;
static struct scsi_generic_cdb inq = {
	6,
	{ CMD_INQUIRY, 0, 0, 0, sizeof(inqbuf), 0 }
};

static u_long capbuf[2];
struct scsi_generic_cdb cap = {
	10,
	{ CMD_READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int
sdident(struct sd_softc *sc)
{
#ifdef DEBUG
	char idstr[32];
#endif
	uint ctlr, target, unit;
	int i;
	int tries = 10;

	ctlr = sc->sc_ctlr;
	target = sc->sc_tgt;
	unit = sc->sc_punit;

	if (scinit(&sc->sc_sc, ctlr) == 0)
		return -1;

	/*
	 * See if unit exists and is a disk then read block size & nblocks.
	 */
	while ((i = scsi_test_unit_rdy(&sc->sc_sc, target, unit)) != 0) {
		if (i < 0 || --tries < 0)
			return (-1);
		if (i == STS_CHECKCOND) {
			u_char sensebuf[8];
			struct scsi_xsense *sp = (struct scsi_xsense *)sensebuf;

			scsi_request_sense(&sc->sc_sc, target, unit, sensebuf,
			    sizeof sensebuf);
			if (sp->class == 7 && sp->key == 6)
				/* drive doing an RTZ -- give it a while */
				DELAY(1000000);
		}
		DELAY(1000);
	}
	if (scsi_immed_command(&sc->sc_sc, target, unit, &inq,
	    (u_char *)&inqbuf, sizeof(inqbuf)) ||
	    scsi_immed_command(&sc->sc_sc, target, unit, &cap,
	    (u_char *)&capbuf, sizeof(capbuf)))
		/* doesn't exist or not a CCS device */
		return (-1);

	switch (inqbuf.type) {
	case 0:		/* disk */
	case 4:		/* WORM */
	case 5:		/* CD-ROM */
	case 7:		/* Magneto-optical */
		break;
	default:	/* not a disk */
		return (-1);
	}
	sc->sc_blks    = capbuf[0];
	sc->sc_blksize = capbuf[1];

#ifdef DEBUG
	memcpy(idstr, &inqbuf.vendor_id, 28);
	for (i = 27; i > 23; --i)
		if (idstr[i] != ' ')
			break;
	idstr[i+1] = 0;
	for (i = 23; i > 7; --i)
		if (idstr[i] != ' ')
			break;
	idstr[i+1] = 0;
	for (i = 7; i >= 0; --i)
		if (idstr[i] != ' ')
			break;
	idstr[i+1] = 0;
	printf("sd(%d,%d): %s %s rev %s", ctlr, target, idstr, &idstr[8],
	       &idstr[24]);

	printf(", %d bytes/sect x %d sectors\n", sc->sc_blksize, sc->sc_blks);
#endif
	if (sc->sc_blksize != DEV_BSIZE) {
		if (sc->sc_blksize < DEV_BSIZE) {
			printf("sd(%d,%d): need %d byte blocks - drive ignored\n",
			    ctlr, target, DEV_BSIZE);
			return (-1);
		}
		for (i = sc->sc_blksize; i > DEV_BSIZE; i >>= 1)
			++sc->sc_bshift;
		sc->sc_blks <<= sc->sc_bshift;
	}
	return(inqbuf.type);
}

struct sd_softc *
sdinit(uint unit, uint part)
{
	struct sd_softc *sc;
	struct disklabel *lp;
	char *msg;

	sc = alloc(sizeof *sc);
	if (sc == NULL)
		return NULL;

	memset(sc, 0, sizeof *sc);

	sc->sc_ctlr = unit / 10;
	sc->sc_tgt = unit % 10;
	sc->sc_part = part;
#ifdef DEBUG
	printf("sdinit: ctlr = %d tgt = %d part = %d\n",
	    sc->sc_ctlr, sc->sc_tgt, sc->sc_part);
#endif
	sc->sc_punit = 0;	/* XXX no LUN support yet */
	sc->sc_type = sdident(sc);
	if (sc->sc_type < 0)
		return(NULL);

	/*
	 * Use the default sizes until we've read the label,
	 * or longer if there isn't one there.
	 */
	lp = &sc->sc_label;

	if (lp->d_secpercyl == 0) {
		lp->d_secsize = DEV_BSIZE;
		lp->d_nsectors = 32;
		lp->d_ntracks = 20;
		lp->d_secpercyl = 32*20;
		lp->d_npartitions = 1;
		lp->d_partitions[0].p_offset = 0;
		lp->d_partitions[0].p_size = LABELSECTOR + 1;
	}

	/*
	 * read disklabel
	 */
	msg = readdisklabel(&sc->sc_sc, sc->sc_tgt, lp);
	if (msg != NULL)
		printf("sd(%d,%d): %s\n", sc->sc_ctlr, sc->sc_tgt, msg);

	return sc;
}

int
sdopen(struct open_file *f, ...)
{
	va_list ap;
	struct sd_softc *sc;
	int unit, part;

	va_start(ap, f);
	unit = va_arg(ap, int);
	part = va_arg(ap, int);
	va_end(ap);

	if (part < 0 || part >= MAXPARTITIONS)
		return(-1);

	sc = sdinit(unit, part);
	if (sc == NULL)
		return -1;

	f->f_devdata = (void *)sc;
	return 0;
}

int
sdclose(struct open_file *f)
{
	struct sd_softc *sc = f->f_devdata;

	free(sc, sizeof *sc);
	f->f_devdata = NULL;

	return 0;
}

static struct scsi_generic_cdb cdb_read = {
	10,
	{ CMD_READ_EXT,  0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static struct scsi_generic_cdb cdb_write = {
	10,
	{ CMD_WRITE_EXT, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int
sdstrategy(void *devdata, int func, daddr_t dblk, size_t size, void *v_buf,
    size_t *rsize)
{
	struct sd_softc *sc = devdata;
	struct disklabel *lp;
	uint8_t *buf = v_buf;
	int target = sc->sc_tgt;
	struct scsi_generic_cdb *cdb;
	daddr32_t blk;
	u_int nblk  = size >> sc->sc_bshift;
	int stat;
#ifdef DEBUG
	int i;
#endif

	lp = &sc->sc_label;
	blk = dblk + (lp->d_partitions[sc->sc_part].p_offset >> sc->sc_bshift);

	if (func == F_READ)
		cdb = &cdb_read;
	else
		cdb = &cdb_write;

	cdb->cdb[2] = (blk & 0xff000000) >> 24;
	cdb->cdb[3] = (blk & 0x00ff0000) >> 16;
	cdb->cdb[4] = (blk & 0x0000ff00) >>  8;
	cdb->cdb[5] = (blk & 0x000000ff);

	cdb->cdb[7] = ((nblk >> _DEV_BSHIFT) & 0xff00) >> 8;
	cdb->cdb[8] = ((nblk >> _DEV_BSHIFT) & 0x00ff);

#ifdef DEBUG
	printf("sdstrategy(%d,%d): blk = %lu (0x%lx), nblk = %u (0x%x)\n",
	    sc->sc_ctlr, sc->sc_tgt, (u_long)blk, (long)blk, nblk, nblk);
	for (i = 0; i < 10; i++)
		printf("cdb[%d] = 0x%x\n", i, cdb->cdb[i]);
#endif
	stat = scsi_immed_command(&sc->sc_sc, target, sc->sc_punit, cdb, buf,
	    size);
	if (rsize)
		*rsize = size;

	return 0;
}
