/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Written by: David Jeffery
 * Copyright (c) 2002 Adaptec Inc.
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

#include <dev/ips/ipsreg.h>
#include <dev/ips/ips.h>
#include <dev/ips/ips_disk.h>
#include <sys/stat.h>

static int ipsd_probe(device_t dev);
static int ipsd_attach(device_t dev);
static int ipsd_detach(device_t dev);

static int ipsd_dump(void *arg, void *virtual, vm_offset_t physical,
		     off_t offset, size_t length);
static void ipsd_dump_map_sg(void *arg, bus_dma_segment_t *segs, int nsegs,
			     int error);
static void ipsd_dump_block_complete(ips_command_t *command);

static disk_open_t ipsd_open;
static disk_close_t ipsd_close;
static disk_strategy_t ipsd_strategy;

static device_method_t ipsd_methods[] = {
	DEVMETHOD(device_probe,		ipsd_probe),
	DEVMETHOD(device_attach,	ipsd_attach),
	DEVMETHOD(device_detach,	ipsd_detach),
	{ 0, 0 }
};

static driver_t ipsd_driver = {
	"ipsd",
	ipsd_methods,
	sizeof(ipsdisk_softc_t)
};

static devclass_t ipsd_devclass;
DRIVER_MODULE(ipsd, ips, ipsd_driver, ipsd_devclass, 0, 0);

/* handle opening of disk device.  It must set up all
   information about the geometry and size of the disk */
static int ipsd_open(struct disk *dp)
{
	ipsdisk_softc_t *dsc = dp->d_drv1;

	dsc->state |= IPS_DEV_OPEN;
	DEVICE_PRINTF(2, dsc->dev, "I'm open\n");
       	return 0;
}

static int ipsd_close(struct disk *dp)
{
	ipsdisk_softc_t *dsc = dp->d_drv1;
	dsc->state &= ~IPS_DEV_OPEN;
	DEVICE_PRINTF(2, dsc->dev, "I'm closed for the day\n");
        return 0;
}

/* ipsd_finish is called to clean up and return a completed IO request */
void ipsd_finish(struct bio *iobuf)
{
	ipsdisk_softc_t *dsc;
	dsc = iobuf->bio_disk->d_drv1;	

	if (iobuf->bio_flags & BIO_ERROR) {
		ipsdisk_softc_t *dsc;
		dsc = iobuf->bio_disk->d_drv1; 
		device_printf(dsc->dev, "iobuf error %d\n", iobuf->bio_error);
	} else
		iobuf->bio_resid = 0;

	biodone(iobuf);	
	ips_start_io_request(dsc->sc);
}


static void ipsd_strategy(struct bio *iobuf)
{
	ipsdisk_softc_t *dsc;

	dsc = iobuf->bio_disk->d_drv1;	
	DEVICE_PRINTF(8,dsc->dev,"in strategy\n");
	iobuf->bio_driver1 = (void *)(uintptr_t)dsc->sc->drives[dsc->disk_number].drivenum;
	mtx_lock(&dsc->sc->queue_mtx);
	bioq_insert_tail(&dsc->sc->queue, iobuf);
	ips_start_io_request(dsc->sc);
	mtx_unlock(&dsc->sc->queue_mtx);
}

static int ipsd_probe(device_t dev)
{
	DEVICE_PRINTF(2,dev, "in probe\n");
	device_set_desc(dev, "Logical Drive");
	return 0;
}

static int ipsd_attach(device_t dev)
{
	device_t adapter;
	ipsdisk_softc_t *dsc;
	u_int totalsectors;

	DEVICE_PRINTF(2,dev, "in attach\n");

	dsc = (ipsdisk_softc_t *)device_get_softc(dev);
	bzero(dsc, sizeof(ipsdisk_softc_t));
	adapter = device_get_parent(dev);
	dsc->dev = dev;
	dsc->sc = device_get_softc(adapter);
	dsc->unit = device_get_unit(dev);
	dsc->disk_number = (uintptr_t) device_get_ivars(dev);
	dsc->ipsd_disk = disk_alloc();
	dsc->ipsd_disk->d_drv1 = dsc;
	dsc->ipsd_disk->d_name = "ipsd";
	dsc->ipsd_disk->d_maxsize = IPS_MAX_IO_SIZE;
	dsc->ipsd_disk->d_open = ipsd_open;
	dsc->ipsd_disk->d_close = ipsd_close;
	dsc->ipsd_disk->d_strategy = ipsd_strategy;
	dsc->ipsd_disk->d_dump = ipsd_dump;

	totalsectors = dsc->sc->drives[dsc->disk_number].sector_count;
   	if ((totalsectors > 0x400000) &&
       			((dsc->sc->adapter_info.miscflags & 0x8) == 0)) {
      		dsc->ipsd_disk->d_fwheads = IPS_NORM_HEADS;
      		dsc->ipsd_disk->d_fwsectors = IPS_NORM_SECTORS;
   	} else {
      		dsc->ipsd_disk->d_fwheads = IPS_COMP_HEADS;
      		dsc->ipsd_disk->d_fwsectors = IPS_COMP_SECTORS;
   	}
	dsc->ipsd_disk->d_sectorsize = IPS_BLKSIZE;
	dsc->ipsd_disk->d_mediasize = (off_t)totalsectors * IPS_BLKSIZE;
	dsc->ipsd_disk->d_unit = dsc->unit;
	dsc->ipsd_disk->d_flags = 0;
	disk_create(dsc->ipsd_disk, DISK_VERSION);

	device_printf(dev, "Logical Drive  (%dMB)\n",
		      dsc->sc->drives[dsc->disk_number].sector_count >> 11);
	return 0;
}

static int ipsd_detach(device_t dev)
{
	ipsdisk_softc_t *dsc;

	DEVICE_PRINTF(2, dev,"in detach\n");
	dsc = (ipsdisk_softc_t *)device_get_softc(dev);
	if(dsc->state & IPS_DEV_OPEN)
		return (EBUSY);
	disk_destroy(dsc->ipsd_disk);
	return 0;
}

static int
ipsd_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset,
	  size_t length)
{
	ipsdisk_softc_t *dsc;
	ips_softc_t *sc;
	ips_command_t *command;
	ips_io_cmd *command_struct;
	struct disk *dp;
	void *va;
	off_t off;
	size_t len;
	int error = 0;

	dp = arg;
	dsc = dp->d_drv1;

	if (dsc == NULL)
		return (EINVAL);
	sc = dsc->sc;

	if (ips_get_free_cmd(sc, &command, 0) != 0) {
		printf("ipsd: failed to get cmd for dump\n");
		return (ENOMEM);
	}

	command->data_dmatag = sc->sg_dmatag;
	command->callback = ipsd_dump_block_complete;

	command_struct = (ips_io_cmd *)command->command_buffer;
	command_struct->id = command->id;
	command_struct->drivenum= sc->drives[dsc->disk_number].drivenum;

	off = offset;
	va = virtual;

	while (length > 0) {
		len =
		    (length > IPS_MAX_IO_SIZE) ? IPS_MAX_IO_SIZE : length;

		command_struct->lba = off / IPS_BLKSIZE;

		if (bus_dmamap_load(command->data_dmatag, command->data_dmamap,
		    va, len, ipsd_dump_map_sg, command, BUS_DMA_NOWAIT) != 0) {
			error = EIO;
			break;
		}
		if (COMMAND_ERROR(command)) {
			error = EIO;
			break;
		}

		length -= len;
		off += len;
		va = (uint8_t *)va + len;
	}

	ips_insert_free_cmd(command->sc, command);
	return (error);
}

static void
ipsd_dump_map_sg(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	ips_softc_t *sc;
	ips_command_t *command;
	ips_sg_element_t *sg_list;
	ips_io_cmd *command_struct;
	int i, length;

	command = (ips_command_t *)arg;
	sc = command->sc;
	length = 0;

	if (error) {
		printf("ipsd_dump_map_sg: error %d\n", error);
		ips_set_error(command, error);
		return;
	}

	command_struct = (ips_io_cmd *)command->command_buffer;

	if (nsegs != 1) {
		command_struct->segnum = nsegs;
		sg_list = (ips_sg_element_t *)((uint8_t *)
		    command->command_buffer + IPS_COMMAND_LEN);
		for (i = 0; i < nsegs; i++) {
			sg_list[i].addr = segs[i].ds_addr;
			sg_list[i].len = segs[i].ds_len;
			length += segs[i].ds_len;
		}
		command_struct->buffaddr =
		    (uint32_t)command->command_phys_addr + IPS_COMMAND_LEN;
		command_struct->command = IPS_SG_WRITE_CMD;
	} else {
		command_struct->buffaddr = segs[0].ds_addr;
		length = segs[0].ds_len;
		command_struct->segnum = 0;
		command_struct->command = IPS_WRITE_CMD;
	}

	length = (length + IPS_BLKSIZE - 1) / IPS_BLKSIZE;
	command_struct->length = length;
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap,
	    BUS_DMASYNC_PREWRITE);

	sc->ips_issue_cmd(command);
	sc->ips_poll_cmd(command);
	return;
}

static void 
ipsd_dump_block_complete(ips_command_t *command)
{

	if (COMMAND_ERROR(command))
		printf("ipsd_dump completion error= 0x%x\n",
		    command->status.value);

	bus_dmamap_sync(command->data_dmatag, command->data_dmamap,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(command->data_dmatag, command->data_dmamap);
}
