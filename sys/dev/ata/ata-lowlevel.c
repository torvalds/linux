/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/ata.h>
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/bus.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <dev/ata/ata-all.h>
#include <dev/ata/ata-pci.h>
#include <ata_if.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>

/* prototypes */
static int ata_generic_status(device_t dev);
static int ata_wait(struct ata_channel *ch, int unit, u_int8_t);
static void ata_pio_read(struct ata_request *, int);
static void ata_pio_write(struct ata_request *, int);
static void ata_tf_read(struct ata_request *);
static void ata_tf_write(struct ata_request *);

/*
 * low level ATA functions 
 */
void
ata_generic_hw(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    ch->hw.begin_transaction = ata_begin_transaction;
    ch->hw.end_transaction = ata_end_transaction;
    ch->hw.status = ata_generic_status;
    ch->hw.softreset = NULL;
    ch->hw.command = ata_generic_command;
    ch->hw.tf_read = ata_tf_read;
    ch->hw.tf_write = ata_tf_write;
    ch->hw.pm_read = NULL;
    ch->hw.pm_write = NULL;
}

/* must be called with ATA channel locked and state_mtx held */
int
ata_begin_transaction(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);
    int dummy, error;

    ATA_DEBUG_RQ(request, "begin transaction");

    /* disable ATAPI DMA writes if HW doesn't support it */
    if ((ch->flags & ATA_NO_ATAPI_DMA) &&
	(request->flags & ATA_R_ATAPI) == ATA_R_ATAPI)
	    request->flags &= ~ATA_R_DMA;
    if ((ch->flags & ATA_ATAPI_DMA_RO) &&
	((request->flags & (ATA_R_ATAPI | ATA_R_DMA | ATA_R_WRITE)) ==
	 (ATA_R_ATAPI | ATA_R_DMA | ATA_R_WRITE)))
	request->flags &= ~ATA_R_DMA;

    switch (request->flags & (ATA_R_ATAPI | ATA_R_DMA)) {

    /* ATA PIO data transfer and control commands */
    default:
	{
	/* record command direction here as our request might be gone later */
	int write = (request->flags & ATA_R_WRITE);

	    /* issue command */
	    if (ch->hw.command(request)) {
		device_printf(request->parent, "error issuing %s command\n",
			   ata_cmd2str(request));
		request->result = EIO;
		goto begin_finished;
	    }

	    /* device reset doesn't interrupt */
	    if (request->u.ata.command == ATA_DEVICE_RESET) {

		int timeout = 1000000;
		do {
		    DELAY(10);
		    request->status = ATA_IDX_INB(ch, ATA_STATUS);
		} while (request->status & ATA_S_BUSY && timeout--);
		if (request->status & ATA_S_ERROR)
		    request->error = ATA_IDX_INB(ch, ATA_ERROR);
		ch->hw.tf_read(request);
		goto begin_finished;
	    }

	    /* if write command output the data */
	    if (write) {
		if (ata_wait(ch, request->unit, (ATA_S_READY | ATA_S_DRQ)) < 0) {
		    device_printf(request->parent,
				  "timeout waiting for write DRQ\n");
		    request->result = EIO;
		    goto begin_finished;
		}
		ata_pio_write(request, request->transfersize);
	    }
	}
	goto begin_continue;

    /* ATA DMA data transfer commands */
    case ATA_R_DMA:
	/* check sanity, setup SG list and DMA engine */
	if ((error = ch->dma.load(request, NULL, &dummy))) {
	    device_printf(request->parent, "setting up DMA failed\n");
	    request->result = error;
	    goto begin_finished;
	}

	/* start DMA engine if necessary */
	if ((ch->flags & ATA_DMA_BEFORE_CMD) &&
	   ch->dma.start && ch->dma.start(request)) {
	    device_printf(request->parent, "error starting DMA\n");
	    request->result = EIO;
	    goto begin_finished;
	}

	/* issue command */
	if (ch->hw.command(request)) {
	    device_printf(request->parent, "error issuing %s command\n",
		       ata_cmd2str(request));
	    request->result = EIO;
	    goto begin_finished;
	}

	/* start DMA engine */
	if (!(ch->flags & ATA_DMA_BEFORE_CMD) &&
	   ch->dma.start && ch->dma.start(request)) {
	    device_printf(request->parent, "error starting DMA\n");
	    request->result = EIO;
	    goto begin_finished;
	}
	goto begin_continue;

    /* ATAPI PIO commands */
    case ATA_R_ATAPI:
	/* is this just a POLL DSC command ? */
	if (request->u.atapi.ccb[0] == ATAPI_POLL_DSC) {
	    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_DEV(request->unit));
	    DELAY(10);
	    if (!(ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_DSC))
		request->result = EBUSY;
	    goto begin_finished;
	}

	/* start ATAPI operation */
	if (ch->hw.command(request)) {
	    device_printf(request->parent, "error issuing ATA PACKET command\n");
	    request->result = EIO;
	    goto begin_finished;
	}
	goto begin_continue;

   /* ATAPI DMA commands */
    case ATA_R_ATAPI|ATA_R_DMA:
	/* is this just a POLL DSC command ? */
	if (request->u.atapi.ccb[0] == ATAPI_POLL_DSC) {
	    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_DEV(request->unit));
	    DELAY(10);
	    if (!(ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_DSC))
		request->result = EBUSY;
	    goto begin_finished;
	}

	/* check sanity, setup SG list and DMA engine */
	if ((error = ch->dma.load(request, NULL, &dummy))) {
	    device_printf(request->parent, "setting up DMA failed\n");
	    request->result = error;
	    goto begin_finished;
	}

	/* start ATAPI operation */
	if (ch->hw.command(request)) {
	    device_printf(request->parent, "error issuing ATA PACKET command\n");
	    request->result = EIO;
	    goto begin_finished;
	}

	/* start DMA engine */
	if (ch->dma.start && ch->dma.start(request)) {
	    request->result = EIO;
	    goto begin_finished;
	}
	goto begin_continue;
    }
    /* NOT REACHED */
    printf("ata_begin_transaction OOPS!!!\n");

begin_finished:
    if (ch->dma.unload) {
        ch->dma.unload(request);
    }
    return ATA_OP_FINISHED;

begin_continue:
    callout_reset(&request->callout, request->timeout * hz,
		  (timeout_t*)ata_timeout, request);
    return ATA_OP_CONTINUES;
}

/* must be called with ATA channel locked and state_mtx held */
int
ata_end_transaction(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);
    int length;

    ATA_DEBUG_RQ(request, "end transaction");

    /* clear interrupt and get status */
    request->status = ATA_IDX_INB(ch, ATA_STATUS);

    switch (request->flags & (ATA_R_ATAPI | ATA_R_DMA | ATA_R_CONTROL)) {

    /* ATA PIO data transfer and control commands */
    default:

	/* on timeouts we have no data or anything so just return */
	if (request->flags & ATA_R_TIMEOUT)
	    goto end_finished;

	/* Read back registers to the request struct. */
	if ((request->status & ATA_S_ERROR) ||
	    (request->flags & (ATA_R_CONTROL | ATA_R_NEEDRESULT))) {
	    ch->hw.tf_read(request);
	}

	/* if we got an error we are done with the HW */
	if (request->status & ATA_S_ERROR) {
	    request->error = ATA_IDX_INB(ch, ATA_ERROR);
	    goto end_finished;
	}
	
	/* are we moving data ? */
	if (request->flags & (ATA_R_READ | ATA_R_WRITE)) {

	    /* if read data get it */
	    if (request->flags & ATA_R_READ) {
		int flags = ATA_S_DRQ;

		if (request->u.ata.command != ATA_ATAPI_IDENTIFY)
		    flags |= ATA_S_READY;
		if (ata_wait(ch, request->unit, flags) < 0) {
		    device_printf(request->parent,
				  "timeout waiting for read DRQ\n");
		    request->result = EIO;
		    goto end_finished;
		}
		ata_pio_read(request, request->transfersize);
	    }

	    /* update how far we've gotten */
	    request->donecount += request->transfersize;

	    /* do we need a scoop more ? */
	    if (request->bytecount > request->donecount) {

		/* set this transfer size according to HW capabilities */
		request->transfersize = 
		    min((request->bytecount - request->donecount),
			request->transfersize);

		/* if data write command, output the data */
		if (request->flags & ATA_R_WRITE) {

		    /* if we get an error here we are done with the HW */
		    if (ata_wait(ch, request->unit, (ATA_S_READY | ATA_S_DRQ)) < 0) {
			device_printf(request->parent,
				      "timeout waiting for write DRQ\n");
			request->status = ATA_IDX_INB(ch, ATA_STATUS);
			goto end_finished;
		    }

		    /* output data and return waiting for new interrupt */
		    ata_pio_write(request, request->transfersize);
		    goto end_continue;
		}

		/* if data read command, return & wait for interrupt */
		if (request->flags & ATA_R_READ)
		    goto end_continue;
	    }
	}
	/* done with HW */
	goto end_finished;

    /* ATA DMA data transfer commands */
    case ATA_R_DMA:

	/* stop DMA engine and get status */
	if (ch->dma.stop)
	    request->dma->status = ch->dma.stop(request);

	/* did we get error or data */
	if (request->status & ATA_S_ERROR)
	    request->error = ATA_IDX_INB(ch, ATA_ERROR);
	else if (request->dma->status & ATA_BMSTAT_ERROR)
	    request->status |= ATA_S_ERROR;
	else if (!(request->flags & ATA_R_TIMEOUT))
	    request->donecount = request->bytecount;

	/* Read back registers to the request struct. */
	if ((request->status & ATA_S_ERROR) ||
	    (request->flags & (ATA_R_CONTROL | ATA_R_NEEDRESULT))) {
	    ch->hw.tf_read(request);
	}

	/* release SG list etc */
	ch->dma.unload(request);

	/* done with HW */
	goto end_finished;

    /* ATAPI PIO commands */
    case ATA_R_ATAPI:
	length = ATA_IDX_INB(ch, ATA_CYL_LSB)|(ATA_IDX_INB(ch, ATA_CYL_MSB)<<8);

	/* on timeouts we have no data or anything so just return */
	if (request->flags & ATA_R_TIMEOUT)
	    goto end_finished;

	switch ((ATA_IDX_INB(ch, ATA_IREASON) & (ATA_I_CMD | ATA_I_IN)) |
		(request->status & ATA_S_DRQ)) {

	case ATAPI_P_CMDOUT:
	    /* this seems to be needed for some (slow) devices */
	    DELAY(10);

	    if (!(request->status & ATA_S_DRQ)) {
		device_printf(request->parent, "command interrupt without DRQ\n");
		request->status = ATA_S_ERROR;
		goto end_finished;
	    }
	    ATA_IDX_OUTSW_STRM(ch, ATA_DATA, (int16_t *)request->u.atapi.ccb,
			       (request->flags & ATA_R_ATAPI16) ? 8 : 6);
	    /* return wait for interrupt */
	    goto end_continue;

	case ATAPI_P_WRITE:
	    if (request->flags & ATA_R_READ) {
		request->status = ATA_S_ERROR;
		device_printf(request->parent,
			      "%s trying to write on read buffer\n",
			   ata_cmd2str(request));
		goto end_finished;
	    }
	    ata_pio_write(request, length);
	    request->donecount += length;

	    /* set next transfer size according to HW capabilities */
	    request->transfersize = min((request->bytecount-request->donecount),
					request->transfersize);
	    /* return wait for interrupt */
	    goto end_continue;

	case ATAPI_P_READ:
	    if (request->flags & ATA_R_WRITE) {
		request->status = ATA_S_ERROR;
		device_printf(request->parent,
			      "%s trying to read on write buffer\n",
			   ata_cmd2str(request));
		goto end_finished;
	    }
	    ata_pio_read(request, length);
	    request->donecount += length;

	    /* set next transfer size according to HW capabilities */
	    request->transfersize = min((request->bytecount-request->donecount),
					request->transfersize);
	    /* return wait for interrupt */
	    goto end_continue;

	case ATAPI_P_DONEDRQ:
	    device_printf(request->parent,
			  "WARNING - %s DONEDRQ non conformant device\n",
			  ata_cmd2str(request));
	    if (request->flags & ATA_R_READ) {
		ata_pio_read(request, length);
		request->donecount += length;
	    }
	    else if (request->flags & ATA_R_WRITE) {
		ata_pio_write(request, length);
		request->donecount += length;
	    }
	    else
		request->status = ATA_S_ERROR;
	    /* FALLTHROUGH */

	case ATAPI_P_ABORT:
	case ATAPI_P_DONE:
	    if (request->status & (ATA_S_ERROR | ATA_S_DWF))
		request->error = ATA_IDX_INB(ch, ATA_ERROR);
	    goto end_finished;

	default:
	    device_printf(request->parent, "unknown transfer phase\n");
	    request->status = ATA_S_ERROR;
	}

	/* done with HW */
	goto end_finished;

    /* ATAPI DMA commands */
    case ATA_R_ATAPI|ATA_R_DMA:

	/* stop DMA engine and get status */
	if (ch->dma.stop)
	    request->dma->status = ch->dma.stop(request);

	/* did we get error or data */
	if (request->status & (ATA_S_ERROR | ATA_S_DWF))
	    request->error = ATA_IDX_INB(ch, ATA_ERROR);
	else if (request->dma->status & ATA_BMSTAT_ERROR)
	    request->status |= ATA_S_ERROR;
	else if (!(request->flags & ATA_R_TIMEOUT))
	    request->donecount = request->bytecount;
 
	/* release SG list etc */
	ch->dma.unload(request);

	/* done with HW */
	goto end_finished;
    }
    /* NOT REACHED */
    printf("ata_end_transaction OOPS!!\n");

end_finished:
    callout_stop(&request->callout);
    return ATA_OP_FINISHED;

end_continue:
    return ATA_OP_CONTINUES;
}

/* must be called with ATA channel locked and state_mtx held */
void
ata_generic_reset(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    u_int8_t ostat0 = 0, stat0 = 0, ostat1 = 0, stat1 = 0;
    u_int8_t err = 0, lsb = 0, msb = 0;
    int mask = 0, timeout;

    /* do we have any signs of ATA/ATAPI HW being present ? */
    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_D_LBA | ATA_DEV(ATA_MASTER));
    DELAY(10);
    ostat0 = ATA_IDX_INB(ch, ATA_STATUS);
    if (((ostat0 & 0xf8) != 0xf8 || (ch->flags & ATA_KNOWN_PRESENCE)) &&
	    ostat0 != 0xa5) {
	stat0 = ATA_S_BUSY;
	mask |= 0x01;
    }

    /* in some setups we dont want to test for a slave */
    if (!(ch->flags & ATA_NO_SLAVE)) {
	ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_D_LBA | ATA_DEV(ATA_SLAVE));
	DELAY(10);      
	ostat1 = ATA_IDX_INB(ch, ATA_STATUS);
	if (((ostat1 & 0xf8) != 0xf8 || (ch->flags & ATA_KNOWN_PRESENCE)) &&
		ostat1 != 0xa5) {
	    stat1 = ATA_S_BUSY;
	    mask |= 0x02;
	}
    }

    if (bootverbose)
	device_printf(dev, "reset tp1 mask=%02x ostat0=%02x ostat1=%02x\n",
		      mask, ostat0, ostat1);

    /* if nothing showed up there is no need to get any further */
    /* XXX SOS is that too strong?, we just might lose devices here */
    ch->devices = 0;
    if (!mask)
	return;

    /* reset (both) devices on this channel */
    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_D_LBA | ATA_DEV(ATA_MASTER));
    DELAY(10);
    ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_IDS | ATA_A_RESET);
    ata_udelay(10000); 
    ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_IDS);
    ata_udelay(100000);
    ATA_IDX_INB(ch, ATA_ERROR);

    /* wait for BUSY to go inactive */
    for (timeout = 0; timeout < 310; timeout++) {
	if ((mask & 0x01) && (stat0 & ATA_S_BUSY)) {
	    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_DEV(ATA_MASTER));
	    DELAY(10);
	    if (ch->flags & ATA_STATUS_IS_LONG)
		    stat0 = ATA_IDX_INL(ch, ATA_STATUS) & 0xff;
	    else
		    stat0 = ATA_IDX_INB(ch, ATA_STATUS);
	    err = ATA_IDX_INB(ch, ATA_ERROR);
	    lsb = ATA_IDX_INB(ch, ATA_CYL_LSB);
	    msb = ATA_IDX_INB(ch, ATA_CYL_MSB);
	    if (bootverbose)
		device_printf(dev,
			      "stat0=0x%02x err=0x%02x lsb=0x%02x msb=0x%02x\n",
			      stat0, err, lsb, msb);
	    if (stat0 == err && lsb == err && msb == err &&
		timeout > (stat0 & ATA_S_BUSY ? 100 : 10))
		mask &= ~0x01;
	    if (!(stat0 & ATA_S_BUSY)) {
		if ((err & 0x7f) == ATA_E_ILI) {
		    if (lsb == ATAPI_MAGIC_LSB && msb == ATAPI_MAGIC_MSB) {
			ch->devices |= ATA_ATAPI_MASTER;
		    }
		    else if (lsb == 0 && msb == 0 && (stat0 & ATA_S_READY)) {
			ch->devices |= ATA_ATA_MASTER;
		    }
		}
		else if ((stat0 & 0x0f) && err == lsb && err == msb) {
		    stat0 |= ATA_S_BUSY;
		}
	    }
	}

	if ((mask & 0x02) && (stat1 & ATA_S_BUSY) &&
	    !((mask & 0x01) && (stat0 & ATA_S_BUSY))) {
	    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_DEV(ATA_SLAVE));
	    DELAY(10);
	    if (ch->flags & ATA_STATUS_IS_LONG)
		    stat1 = ATA_IDX_INL(ch, ATA_STATUS) & 0xff;
	    else
		    stat1 = ATA_IDX_INB(ch, ATA_STATUS);
	    err = ATA_IDX_INB(ch, ATA_ERROR);
	    lsb = ATA_IDX_INB(ch, ATA_CYL_LSB);
	    msb = ATA_IDX_INB(ch, ATA_CYL_MSB);
	    if (bootverbose)
		device_printf(dev,
			      "stat1=0x%02x err=0x%02x lsb=0x%02x msb=0x%02x\n",
			      stat1, err, lsb, msb);
	    if (stat1 == err && lsb == err && msb == err &&
		timeout > (stat1 & ATA_S_BUSY ? 100 : 10))
		mask &= ~0x02;
	    if (!(stat1 & ATA_S_BUSY)) {
		if ((err & 0x7f) == ATA_E_ILI) {
		    if (lsb == ATAPI_MAGIC_LSB && msb == ATAPI_MAGIC_MSB) {
			ch->devices |= ATA_ATAPI_SLAVE;
		    }
		    else if (lsb == 0 && msb == 0 && (stat1 & ATA_S_READY)) {
			ch->devices |= ATA_ATA_SLAVE;
		    }
		}
		else if ((stat1 & 0x0f) && err == lsb && err == msb) {
		    stat1 |= ATA_S_BUSY;
		}
	    }
	}

	if ((ch->flags & ATA_KNOWN_PRESENCE) == 0 &&
	    timeout > ((mask == 0x03) ? 20 : 10)) {
		if ((mask & 0x01) && stat0 == 0xff)
			mask &= ~0x01;
		if ((mask & 0x02) && stat1 == 0xff)
			mask &= ~0x02;
	}
	if (((mask & 0x01) == 0 || !(stat0 & ATA_S_BUSY)) &&
	    ((mask & 0x02) == 0 || !(stat1 & ATA_S_BUSY)))
		break;
	ata_udelay(100000);
    }

    if (bootverbose)
	device_printf(dev, "reset tp2 stat0=%02x stat1=%02x devices=0x%x\n",
		      stat0, stat1, ch->devices);
}

/* must be called with ATA channel locked and state_mtx held */
static int
ata_generic_status(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY) {
	DELAY(100);
	if (ATA_IDX_INB(ch, ATA_ALTSTAT) & ATA_S_BUSY)
	    return 0;
    }
    return 1;
}

static int
ata_wait(struct ata_channel *ch, int unit, u_int8_t mask)
{
    u_int8_t status;
    int timeout = 0;
    
    DELAY(1);

    /* wait at max 1 second for device to get !BUSY */ 
    while (timeout < 1000000) {
	status = ATA_IDX_INB(ch, ATA_ALTSTAT);

	/* if drive fails status, reselect the drive and try again */
	if (status == 0xff) {
	    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_DEV(unit));
	    timeout += 1000;
	    DELAY(1000);
	    continue;
	}

	/* are we done ? */
	if (!(status & ATA_S_BUSY))
	    break;            

	if (timeout > 1000) {
	    timeout += 1000;
	    DELAY(1000);
	}
	else {
	    timeout += 10;
	    DELAY(10);
	}
    }    
    if (timeout >= 1000000)      
	return -2;          
    if (!mask)     
	return (status & ATA_S_ERROR);   

    DELAY(1);
    
    /* wait 50 msec for bits wanted */     
    timeout = 5000;
    while (timeout--) {   
	status = ATA_IDX_INB(ch, ATA_ALTSTAT);
	if ((status & mask) == mask) 
	    return (status & ATA_S_ERROR);            
	DELAY(10);         
    }     
    return -3;      
}   

int
ata_generic_command(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);

    /* select device */
    ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_IBM | ATA_D_LBA | ATA_DEV(request->unit));

    /* ready to issue command ? */
    if (ata_wait(ch, request->unit, 0) < 0) { 
	device_printf(request->parent, "timeout waiting to issue command\n");
	request->flags |= ATA_R_TIMEOUT;
	return (-1);
    }

    /* enable interrupt */
    ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_4BIT);

    if (request->flags & ATA_R_ATAPI) {
	int timeout = 5000;
	int res;

	/* issue packet command to controller */
	if (request->flags & ATA_R_DMA) {
	    ATA_IDX_OUTB(ch, ATA_FEATURE, ATA_F_DMA);
	    ATA_IDX_OUTB(ch, ATA_CYL_LSB, 0);
	    ATA_IDX_OUTB(ch, ATA_CYL_MSB, 0);
	}
	else {
	    ATA_IDX_OUTB(ch, ATA_FEATURE, 0);
	    ATA_IDX_OUTB(ch, ATA_CYL_LSB, request->transfersize);
	    ATA_IDX_OUTB(ch, ATA_CYL_MSB, request->transfersize >> 8);
	}
	ATA_IDX_OUTB(ch, ATA_COMMAND, ATA_PACKET_CMD);

	/* command interrupt device ? just return and wait for interrupt */
	if (request->flags & ATA_R_ATAPI_INTR)
	    return (0);

	/* command processed ? */
	res = ata_wait(ch, request->unit, 0);
	if (res != 0) {
	    if (res < 0) {
		    device_printf(request->parent,
			"timeout waiting for PACKET command\n");
		    request->flags |= ATA_R_TIMEOUT;
	    }
	    return (-1);
	}
	/* wait for ready to write ATAPI command block */
	while (timeout--) {
	    int reason = ATA_IDX_INB(ch, ATA_IREASON);
	    int status = ATA_IDX_INB(ch, ATA_STATUS);

	    if (((reason & (ATA_I_CMD | ATA_I_IN)) |
		 (status & (ATA_S_DRQ | ATA_S_BUSY))) == ATAPI_P_CMDOUT)
		break;
	    DELAY(20);
	}
	if (timeout <= 0) {
	    device_printf(request->parent,
		"timeout waiting for ATAPI ready\n");
	    request->flags |= ATA_R_TIMEOUT;
	    return (-1);
	}

	/* this seems to be needed for some (slow) devices */
	DELAY(10);
		    
	/* output command block */
	ATA_IDX_OUTSW_STRM(ch, ATA_DATA, (int16_t *)request->u.atapi.ccb,
			   (request->flags & ATA_R_ATAPI16) ? 8 : 6);
    }
    else {
	ch->hw.tf_write(request);

	/* issue command to controller */
	ATA_IDX_OUTB(ch, ATA_COMMAND, request->u.ata.command);
    }
    return (0);
}

static void
ata_tf_read(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);

    if (request->flags & ATA_R_48BIT) {
	ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_4BIT | ATA_A_HOB);
	request->u.ata.count = (ATA_IDX_INB(ch, ATA_COUNT) << 8);
	request->u.ata.lba =
	    ((u_int64_t)(ATA_IDX_INB(ch, ATA_SECTOR)) << 24) |
	    ((u_int64_t)(ATA_IDX_INB(ch, ATA_CYL_LSB)) << 32) |
	    ((u_int64_t)(ATA_IDX_INB(ch, ATA_CYL_MSB)) << 40);

	ATA_IDX_OUTB(ch, ATA_CONTROL, ATA_A_4BIT);
	request->u.ata.count |= ATA_IDX_INB(ch, ATA_COUNT);
	request->u.ata.lba |= 
	    (ATA_IDX_INB(ch, ATA_SECTOR) |
	     (ATA_IDX_INB(ch, ATA_CYL_LSB) << 8) |
	     (ATA_IDX_INB(ch, ATA_CYL_MSB) << 16));
    }
    else {
	request->u.ata.count = ATA_IDX_INB(ch, ATA_COUNT);
	request->u.ata.lba = ATA_IDX_INB(ch, ATA_SECTOR) |
			     (ATA_IDX_INB(ch, ATA_CYL_LSB) << 8) |
			     (ATA_IDX_INB(ch, ATA_CYL_MSB) << 16) |
			     ((ATA_IDX_INB(ch, ATA_DRIVE) & 0xf) << 24);
    }
}

static void
ata_tf_write(struct ata_request *request)
{
    struct ata_channel *ch = device_get_softc(request->parent);

    if (request->flags & ATA_R_48BIT) {
	ATA_IDX_OUTB(ch, ATA_FEATURE, request->u.ata.feature >> 8);
	ATA_IDX_OUTB(ch, ATA_FEATURE, request->u.ata.feature);
	ATA_IDX_OUTB(ch, ATA_COUNT, request->u.ata.count >> 8);
	ATA_IDX_OUTB(ch, ATA_COUNT, request->u.ata.count);
	ATA_IDX_OUTB(ch, ATA_SECTOR, request->u.ata.lba >> 24);
	ATA_IDX_OUTB(ch, ATA_SECTOR, request->u.ata.lba);
	ATA_IDX_OUTB(ch, ATA_CYL_LSB, request->u.ata.lba >> 32);
	ATA_IDX_OUTB(ch, ATA_CYL_LSB, request->u.ata.lba >> 8);
	ATA_IDX_OUTB(ch, ATA_CYL_MSB, request->u.ata.lba >> 40);
	ATA_IDX_OUTB(ch, ATA_CYL_MSB, request->u.ata.lba >> 16);
	ATA_IDX_OUTB(ch, ATA_DRIVE, ATA_D_LBA | ATA_DEV(request->unit));
    }
    else {
	ATA_IDX_OUTB(ch, ATA_FEATURE, request->u.ata.feature);
	ATA_IDX_OUTB(ch, ATA_COUNT, request->u.ata.count);
	    ATA_IDX_OUTB(ch, ATA_SECTOR, request->u.ata.lba);
	    ATA_IDX_OUTB(ch, ATA_CYL_LSB, request->u.ata.lba >> 8);
	    ATA_IDX_OUTB(ch, ATA_CYL_MSB, request->u.ata.lba >> 16);
	    ATA_IDX_OUTB(ch, ATA_DRIVE,
			 ATA_D_IBM | ATA_D_LBA | ATA_DEV(request->unit) |
			 ((request->u.ata.lba >> 24) & 0x0f));
    }
}

static void
ata_pio_read(struct ata_request *request, int length)
{
	struct ata_channel *ch = device_get_softc(request->parent);
	struct bio *bio;
	uint8_t *addr;
	vm_offset_t page;
	int todo, done, off, moff, resid, size, i;
	uint8_t buf[2] __aligned(2);

	todo = min(request->transfersize, length);
	page = done = resid = 0;
	while (done < todo) {
		size = todo - done;

		/* Prepare data address and limit size (if not sequential). */
		off = request->donecount + done;
		if ((request->flags & ATA_R_DATA_IN_CCB) == 0 ||
		    (request->ccb->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_VADDR) {
			addr = (uint8_t *)request->data + off;
		} else if ((request->ccb->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_BIO) {
			bio = (struct bio *)request->data;
			if ((bio->bio_flags & BIO_UNMAPPED) == 0) {
				addr = (uint8_t *)bio->bio_data + off;
			} else {
				moff = bio->bio_ma_offset + off;
				page = pmap_quick_enter_page(
				    bio->bio_ma[moff / PAGE_SIZE]);
				moff %= PAGE_SIZE;
				size = min(size, PAGE_SIZE - moff);
				addr = (void *)(page + moff);
			}
		} else
			panic("ata_pio_read: Unsupported CAM data type %x\n",
			    (request->ccb->ccb_h.flags & CAM_DATA_MASK));

		/* We may have extra byte already read but not stored. */
		if (resid) {
			addr[0] = buf[1];
			addr++;
			done++;
			size--;
		}

		/* Process main part of data. */
		resid = size % 2;
		if (__predict_false((ch->flags & ATA_USE_16BIT) ||
		    (size % 4) != 0 || ((uintptr_t)addr % 4) != 0)) {
#ifndef __NO_STRICT_ALIGNMENT
			if (__predict_false((uintptr_t)addr % 2)) {
				for (i = 0; i + 1 < size; i += 2) {
					*(uint16_t *)&buf =
					    ATA_IDX_INW_STRM(ch, ATA_DATA);
					addr[i] = buf[0];
					addr[i + 1] = buf[1];
				}
			} else
#endif
				ATA_IDX_INSW_STRM(ch, ATA_DATA, (void*)addr,
				    size / 2);

			/* If we have extra byte of data, leave it for later. */
			if (resid) {
				*(uint16_t *)&buf =
				    ATA_IDX_INW_STRM(ch, ATA_DATA);
				addr[size - 1] = buf[0];
			}
		} else
			ATA_IDX_INSL_STRM(ch, ATA_DATA, (void*)addr, size / 4);

		if (page) {
			pmap_quick_remove_page(page);
			page = 0;
		}
		done += size;
	}

	if (length > done) {
		device_printf(request->parent,
		    "WARNING - %s read data overrun %d > %d\n",
		    ata_cmd2str(request), length, done);
		for (i = done + resid; i < length; i += 2)
			ATA_IDX_INW(ch, ATA_DATA);
	}
}

static void
ata_pio_write(struct ata_request *request, int length)
{
	struct ata_channel *ch = device_get_softc(request->parent);
	struct bio *bio;
	uint8_t *addr;
	vm_offset_t page;
	int todo, done, off, moff, resid, size, i;
	uint8_t buf[2] __aligned(2);

	todo = min(request->transfersize, length);
	page = done = resid = 0;
	while (done < todo) {
		size = todo - done;

		/* Prepare data address and limit size (if not sequential). */
		off = request->donecount + done;
		if ((request->flags & ATA_R_DATA_IN_CCB) == 0 ||
		    (request->ccb->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_VADDR) {
			addr = (uint8_t *)request->data + off;
		} else if ((request->ccb->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_BIO) {
			bio = (struct bio *)request->data;
			if ((bio->bio_flags & BIO_UNMAPPED) == 0) {
				addr = (uint8_t *)bio->bio_data + off;
			} else {
				moff = bio->bio_ma_offset + off;
				page = pmap_quick_enter_page(
				    bio->bio_ma[moff / PAGE_SIZE]);
				moff %= PAGE_SIZE;
				size = min(size, PAGE_SIZE - moff);
				addr = (void *)(page + moff);
			}
		} else
			panic("ata_pio_write: Unsupported CAM data type %x\n",
			    (request->ccb->ccb_h.flags & CAM_DATA_MASK));

		/* We may have extra byte to be written first. */
		if (resid) {
			buf[1] = addr[0];
			ATA_IDX_OUTW_STRM(ch, ATA_DATA, *(uint16_t *)&buf);
			addr++;
			done++;
			size--;
		}

		/* Process main part of data. */
		resid = size % 2;
		if (__predict_false((ch->flags & ATA_USE_16BIT) ||
		    (size % 4) != 0 || ((uintptr_t)addr % 4) != 0)) {
#ifndef __NO_STRICT_ALIGNMENT
			if (__predict_false((uintptr_t)addr % 2)) {
				for (i = 0; i + 1 < size; i += 2) {
					buf[0] = addr[i];
					buf[1] = addr[i + 1];
					ATA_IDX_OUTW_STRM(ch, ATA_DATA,
					    *(uint16_t *)&buf);
				}
			} else
#endif
				ATA_IDX_OUTSW_STRM(ch, ATA_DATA, (void*)addr,
				    size / 2);

			/* If we have extra byte of data, save it for later. */
			if (resid)
				buf[0] = addr[size - 1];
		} else
			ATA_IDX_OUTSL_STRM(ch, ATA_DATA,
			    (void*)addr, size / sizeof(int32_t));

		if (page) {
			pmap_quick_remove_page(page);
			page = 0;
		}
		done += size;
	}

	/* We may have extra byte of data to be written. Pad it with zero. */
	if (resid) {
		buf[1] = 0;
		ATA_IDX_OUTW_STRM(ch, ATA_DATA, *(uint16_t *)&buf);
	}

	if (length > done) {
		device_printf(request->parent,
		    "WARNING - %s write data underrun %d > %d\n",
		    ata_cmd2str(request), length, done);
		for (i = done + resid; i < length; i += 2)
			ATA_IDX_OUTW(ch, ATA_DATA, 0);
	}
}
