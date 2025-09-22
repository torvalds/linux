/*	$OpenBSD: scsi_ioctl.c,v 1.67 2020/09/22 19:32:53 krw Exp $	*/
/*	$NetBSD: scsi_ioctl.c,v 1.23 1996/10/12 23:23:17 christos Exp $	*/

/*
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Contributed by HD Associates (hd@world.std.com).
 * Copyright (c) 1992, 1993 HD Associates
 *
 * Berkeley style copyright.
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/pool.h>
#include <sys/device.h>
#include <sys/fcntl.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_debug.h>
#include <scsi/scsiconf.h>

#include <sys/scsiio.h>
#include <sys/ataio.h>

int			scsi_ioc_cmd(struct scsi_link *, scsireq_t *);
int			scsi_ioc_ata_cmd(struct scsi_link *, atareq_t *);

const unsigned char scsi_readsafe_cmd[256] = {
	[0x00] = 1,	/* TEST UNIT READY */
	[0x03] = 1,	/* REQUEST SENSE */
	[0x08] = 1,	/* READ(6) */
	[0x12] = 1,	/* INQUIRY */
	[0x1a] = 1,	/* MODE SENSE */
	[0x1b] = 1,	/* START STOP */
	[0x23] = 1,	/* READ FORMAT CAPACITIES */
	[0x25] = 1,	/* READ CDVD CAPACITY */
	[0x28] = 1,	/* READ(10) */
	[0x2b] = 1,	/* SEEK */
	[0x2f] = 1,	/* VERIFY(10) */
	[0x3c] = 1,	/* READ BUFFER */
	[0x3e] = 1,	/* READ LONG */
	[0x42] = 1,	/* READ SUBCHANNEL */
	[0x43] = 1,	/* READ TOC PMA ATIP */
	[0x44] = 1,	/* READ HEADER */
	[0x45] = 1,	/* PLAY AUDIO(10) */
	[0x46] = 1,	/* GET CONFIGURATION */
	[0x47] = 1,	/* PLAY AUDIO MSF */
	[0x48] = 1,	/* PLAY AUDIO TI */
	[0x4a] = 1,	/* GET EVENT STATUS NOTIFICATION */
	[0x4b] = 1,	/* PAUSE RESUME */
	[0x4e] = 1,	/* STOP PLAY SCAN */
	[0x51] = 1,	/* READ DISC INFO */
	[0x52] = 1,	/* READ TRACK RZONE INFO */
	[0x5a] = 1,	/* MODE SENSE(10) */
	[0x88] = 1,	/* READ(16) */
	[0x8f] = 1,	/* VERIFY(16) */
	[0xa4] = 1,	/* REPORT KEY */
	[0xa5] = 1,	/* PLAY AUDIO(12) */
	[0xa8] = 1,	/* READ(12) */
	[0xac] = 1,	/* GET PERFORMANCE */
	[0xad] = 1,	/* READ DVD STRUCTURE */
	[0xb9] = 1,	/* READ CD MSF */
	[0xba] = 1,	/* SCAN */
	[0xbc] = 1,	/* PLAY CD */
	[0xbd] = 1,	/* MECHANISM STATUS */
	[0xbe] = 1	/* READ CD */
};

int
scsi_ioc_cmd(struct scsi_link *link, scsireq_t *screq)
{
	struct scsi_xfer		*xs;
	int				 err = 0;

	if (screq->cmdlen > sizeof(struct scsi_generic))
		return EFAULT;
	if (screq->datalen > MAXPHYS)
		return EINVAL;

	xs = scsi_xs_get(link, 0);
	if (xs == NULL)
		return ENOMEM;

	memcpy(&xs->cmd, screq->cmd, screq->cmdlen);
	xs->cmdlen = screq->cmdlen;

	if (screq->datalen > 0) {
		xs->data = dma_alloc(screq->datalen, PR_WAITOK | PR_ZERO);
		if (xs->data == NULL) {
			err = ENOMEM;
			goto err;
		}
		xs->datalen = screq->datalen;
	}

	if (ISSET(screq->flags, SCCMD_READ))
		SET(xs->flags, SCSI_DATA_IN);
	if (ISSET(screq->flags, SCCMD_WRITE)) {
		if (screq->datalen > 0) {
			err = copyin(screq->databuf, xs->data, screq->datalen);
			if (err != 0)
				goto err;
		}

		SET(xs->flags, SCSI_DATA_OUT);
	}

	SET(xs->flags, SCSI_SILENT);	/* User is responsible for errors. */
	xs->timeout = screq->timeout;
	xs->retries = 0; /* user must do the retries *//* ignored */

	scsi_xs_sync(xs);

	screq->retsts = 0;
	screq->status = xs->status;
	switch (xs->error) {
	case XS_NOERROR:
		/* probably rubbish */
		screq->datalen_used = xs->datalen - xs->resid;
		screq->retsts = SCCMD_OK;
		break;
	case XS_SENSE:
		SC_DEBUG_SENSE(xs);
		screq->senselen_used = min(sizeof(xs->sense),
		    sizeof(screq->sense));
		memcpy(screq->sense, &xs->sense, screq->senselen_used);
		screq->retsts = SCCMD_SENSE;
		break;
	case XS_SHORTSENSE:
		SC_DEBUG_SENSE(xs);
		printf("XS_SHORTSENSE\n");
		screq->senselen_used = min(sizeof(xs->sense),
		    sizeof(screq->sense));
		memcpy(screq->sense, &xs->sense, screq->senselen_used);
		screq->retsts = SCCMD_UNKNOWN;
		break;
	case XS_DRIVER_STUFFUP:
		screq->retsts = SCCMD_UNKNOWN;
		break;
	case XS_TIMEOUT:
		screq->retsts = SCCMD_TIMEOUT;
		break;
	case XS_BUSY:
		screq->retsts = SCCMD_BUSY;
		break;
	default:
		screq->retsts = SCCMD_UNKNOWN;
		break;
	}

	if (screq->datalen > 0 && ISSET(screq->flags, SCCMD_READ)) {
		err = copyout(xs->data, screq->databuf, screq->datalen);
		if (err != 0)
			goto err;
	}

err:
	if (xs->data)
		dma_free(xs->data, screq->datalen);
	scsi_xs_put(xs);

	return err;
}

int
scsi_ioc_ata_cmd(struct scsi_link *link, atareq_t *atareq)
{
	struct scsi_xfer		*xs;
	struct scsi_ata_passthru_12	*cdb;
	int				 err = 0;

	if (atareq->datalen > MAXPHYS)
		return EINVAL;

	xs = scsi_xs_get(link, 0);
	if (xs == NULL)
		return ENOMEM;

	cdb = (struct scsi_ata_passthru_12 *)&xs->cmd;
	cdb->opcode = ATA_PASSTHRU_12;

	if (atareq->datalen > 0) {
		if (ISSET(atareq->flags, ATACMD_READ)) {
			cdb->count_proto = ATA_PASSTHRU_PROTO_PIO_DATAIN;
			cdb->flags = ATA_PASSTHRU_T_DIR_READ;
		} else {
			cdb->count_proto = ATA_PASSTHRU_PROTO_PIO_DATAOUT;
			cdb->flags = ATA_PASSTHRU_T_DIR_WRITE;
		}
		SET(cdb->flags, ATA_PASSTHRU_T_LEN_SECTOR_COUNT);
	} else {
		cdb->count_proto = ATA_PASSTHRU_PROTO_NON_DATA;
		cdb->flags = ATA_PASSTHRU_T_LEN_NONE;
	}
	cdb->features = atareq->features;
	cdb->sector_count = atareq->sec_count;
	cdb->lba_low = atareq->sec_num;
	cdb->lba_mid = atareq->cylinder;
	cdb->lba_high = atareq->cylinder >> 8;
	cdb->device = atareq->head & 0x0f;
	cdb->command = atareq->command;

	xs->cmdlen = sizeof(*cdb);

	if (atareq->datalen > 0) {
		xs->data = dma_alloc(atareq->datalen, PR_WAITOK | PR_ZERO);
		if (xs->data == NULL) {
			err = ENOMEM;
			goto err;
		}
		xs->datalen = atareq->datalen;
	}

	if (ISSET(atareq->flags, ATACMD_READ))
		SET(xs->flags, SCSI_DATA_IN);
	if (ISSET(atareq->flags, ATACMD_WRITE)) {
		if (atareq->datalen > 0) {
			err = copyin(atareq->databuf, xs->data,
			    atareq->datalen);
			if (err != 0)
				goto err;
		}

		SET(xs->flags, SCSI_DATA_OUT);
	}

	SET(xs->flags, SCSI_SILENT);	/* User is responsible for errors. */
	xs->retries = 0; /* user must do the retries *//* ignored */

	scsi_xs_sync(xs);

	atareq->retsts = ATACMD_ERROR;
	switch (xs->error) {
	case XS_SENSE:
	case XS_SHORTSENSE:
		SC_DEBUG_SENSE(xs);
		/* XXX this is not right */
	case XS_NOERROR:
		atareq->retsts = ATACMD_OK;
		break;
	default:
		atareq->retsts = ATACMD_ERROR;
		break;
	}

	if (atareq->datalen > 0 && ISSET(atareq->flags, ATACMD_READ)) {
		err = copyout(xs->data, atareq->databuf, atareq->datalen);
		if (err != 0)
			goto err;
	}

err:
	if (xs->data)
		dma_free(xs->data, atareq->datalen);
	scsi_xs_put(xs);

	return err;
}

/*
 * Something (e.g. another driver) has called us
 * with a scsi_link for a target/lun/adapter, and a scsi
 * specific ioctl to perform, better try.
 */
int
scsi_do_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
	SC_DEBUG(link, SDEV_DB2, ("scsi_do_ioctl(0x%lx)\n", cmd));

	switch(cmd) {
	case SCIOCIDENTIFY: {
		struct scsi_addr *sca = (struct scsi_addr *)addr;

		if (!ISSET(link->flags, (SDEV_ATAPI | SDEV_UMASS)))
			/* A 'real' SCSI target. */
			sca->type = TYPE_SCSI;
		else
			/* An 'emulated' SCSI target. */
			sca->type = TYPE_ATAPI;
		sca->scbus = link->bus->sc_dev.dv_unit;
		sca->target = link->target;
		sca->lun = link->lun;
		return 0;
	}
	case SCIOCCOMMAND:
		if (scsi_readsafe_cmd[((scsireq_t *)addr)->cmd[0]])
			break;
		/* FALLTHROUGH */
	case ATAIOCCOMMAND:
	case SCIOCDEBUG:
		if (!ISSET(flag, FWRITE))
			return EPERM;
		break;
	default:
		if (link->bus->sb_adapter->ioctl)
			return (link->bus->sb_adapter->ioctl)(link, cmd, addr, flag);
		else
			return ENOTTY;
	}

	switch(cmd) {
	case SCIOCCOMMAND:
		return scsi_ioc_cmd(link, (scsireq_t *)addr);
	case ATAIOCCOMMAND:
		return scsi_ioc_ata_cmd(link, (atareq_t *)addr);
	case SCIOCDEBUG: {
		int level = *((int *)addr);

		SC_DEBUG(link, SDEV_DB3, ("debug set to %d\n", level));
		CLR(link->flags, SDEV_DBX); /* clear debug bits */
		if (level & 1)
			SET(link->flags, SDEV_DB1);
		if (level & 2)
			SET(link->flags, SDEV_DB2);
		if (level & 4)
			SET(link->flags, SDEV_DB3);
		if (level & 8)
			SET(link->flags, SDEV_DB4);
		return 0;
	}
	default:
#ifdef DIAGNOSTIC
		panic("scsi_do_ioctl: impossible cmd (%#lx)", cmd);
#endif /* DIAGNOSTIC */
		return 0;
	}
}
