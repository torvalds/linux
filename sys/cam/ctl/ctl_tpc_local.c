/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2004, 2005 Silicon Graphics International Corp.
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
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <cam/cam.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_scsi_all.h>
#include <cam/ctl/ctl_tpc.h>
#include <cam/ctl/ctl_error.h>

struct tpcl_softc {
	struct ctl_port port;
	int cur_tag_num;
};

static struct tpcl_softc tpcl_softc;

static int tpcl_init(void);
static int tpcl_shutdown(void);
static void tpcl_datamove(union ctl_io *io);
static void tpcl_done(union ctl_io *io);


static struct ctl_frontend tpcl_frontend =
{
	.name = "tpc",
	.init = tpcl_init,
	.shutdown = tpcl_shutdown,
};
CTL_FRONTEND_DECLARE(ctltpc, tpcl_frontend);

static int
tpcl_init(void)
{
	struct tpcl_softc *tsoftc = &tpcl_softc;
	struct ctl_port *port;
	struct scsi_transportid_spi *tid;
	int error, len;

	memset(tsoftc, 0, sizeof(*tsoftc));

	port = &tsoftc->port;
	port->frontend = &tpcl_frontend;
	port->port_type = CTL_PORT_INTERNAL;
	port->num_requested_ctl_io = 100;
	port->port_name = "tpc";
	port->fe_datamove = tpcl_datamove;
	port->fe_done = tpcl_done;
	port->targ_port = -1;
	port->max_initiators = 1;

	if ((error = ctl_port_register(port)) != 0) {
		printf("%s: tpc port registration failed\n", __func__);
		return (error);
	}

	len = sizeof(struct scsi_transportid_spi);
	port->init_devid = malloc(sizeof(struct ctl_devid) + len,
	    M_CTL, M_WAITOK | M_ZERO);
	port->init_devid->len = len;
	tid = (struct scsi_transportid_spi *)port->init_devid->data;
	tid->format_protocol = SCSI_TRN_SPI_FORMAT_DEFAULT | SCSI_PROTO_SPI;
	scsi_ulto2b(0, tid->scsi_addr);
	scsi_ulto2b(port->targ_port, tid->rel_trgt_port_id);

	ctl_port_online(port);
	return (0);
}

static int
tpcl_shutdown(void)
{
	struct tpcl_softc *tsoftc = &tpcl_softc;
	struct ctl_port *port = &tsoftc->port;
	int error;

	ctl_port_offline(port);
	if ((error = ctl_port_deregister(port)) != 0)
		printf("%s: tpc port deregistration failed\n", __func__);
	return (error);
}

static void
tpcl_datamove(union ctl_io *io)
{
	struct ctl_sg_entry *ext_sglist, *kern_sglist;
	struct ctl_sg_entry ext_entry, kern_entry;
	int ext_sg_entries, kern_sg_entries;
	int ext_sg_start, ext_offset;
	int len_to_copy;
	int kern_watermark, ext_watermark;
	struct ctl_scsiio *ctsio;
	int i, j;

	CTL_DEBUG_PRINT(("%s\n", __func__));

	ctsio = &io->scsiio;

	/*
	 * If this is the case, we're probably doing a BBR read and don't
	 * actually need to transfer the data.  This will effectively
	 * bit-bucket the data.
	 */
	if (ctsio->ext_data_ptr == NULL)
		goto bailout;

	/*
	 * To simplify things here, if we have a single buffer, stick it in
	 * a S/G entry and just make it a single entry S/G list.
	 */
	if (ctsio->ext_sg_entries > 0) {
		int len_seen;

		ext_sglist = (struct ctl_sg_entry *)ctsio->ext_data_ptr;
		ext_sg_entries = ctsio->ext_sg_entries;
		ext_sg_start = 0;
		ext_offset = 0;
		len_seen = 0;
		for (i = 0; i < ext_sg_entries; i++) {
			if ((len_seen + ext_sglist[i].len) >=
			     ctsio->ext_data_filled) {
				ext_sg_start = i;
				ext_offset = ctsio->ext_data_filled - len_seen;
				break;
			}
			len_seen += ext_sglist[i].len;
		}
	} else {
		ext_sglist = &ext_entry;
		ext_sglist->addr = ctsio->ext_data_ptr;
		ext_sglist->len = ctsio->ext_data_len;
		ext_sg_entries = 1;
		ext_sg_start = 0;
		ext_offset = ctsio->ext_data_filled;
	}

	if (ctsio->kern_sg_entries > 0) {
		kern_sglist = (struct ctl_sg_entry *)ctsio->kern_data_ptr;
		kern_sg_entries = ctsio->kern_sg_entries;
	} else {
		kern_sglist = &kern_entry;
		kern_sglist->addr = ctsio->kern_data_ptr;
		kern_sglist->len = ctsio->kern_data_len;
		kern_sg_entries = 1;
	}

	kern_watermark = 0;
	ext_watermark = ext_offset;
	for (i = ext_sg_start, j = 0;
	     i < ext_sg_entries && j < kern_sg_entries;) {
		uint8_t *ext_ptr, *kern_ptr;

		len_to_copy = min(ext_sglist[i].len - ext_watermark,
				  kern_sglist[j].len - kern_watermark);

		ext_ptr = (uint8_t *)ext_sglist[i].addr;
		ext_ptr = ext_ptr + ext_watermark;
		if (io->io_hdr.flags & CTL_FLAG_BUS_ADDR) {
			/*
			 * XXX KDM fix this!
			 */
			panic("need to implement bus address support");
#if 0
			kern_ptr = bus_to_virt(kern_sglist[j].addr);
#endif
		} else
			kern_ptr = (uint8_t *)kern_sglist[j].addr;
		kern_ptr = kern_ptr + kern_watermark;

		if ((ctsio->io_hdr.flags & CTL_FLAG_DATA_MASK) ==
		     CTL_FLAG_DATA_IN) {
			CTL_DEBUG_PRINT(("%s: copying %d bytes to user\n",
					 __func__, len_to_copy));
			CTL_DEBUG_PRINT(("%s: from %p to %p\n", __func__,
					 kern_ptr, ext_ptr));
			memcpy(ext_ptr, kern_ptr, len_to_copy);
		} else {
			CTL_DEBUG_PRINT(("%s: copying %d bytes from user\n",
					 __func__, len_to_copy));
			CTL_DEBUG_PRINT(("%s: from %p to %p\n", __func__,
					 ext_ptr, kern_ptr));
			memcpy(kern_ptr, ext_ptr, len_to_copy);
		}

		ctsio->ext_data_filled += len_to_copy;
		ctsio->kern_data_resid -= len_to_copy;

		ext_watermark += len_to_copy;
		if (ext_sglist[i].len == ext_watermark) {
			i++;
			ext_watermark = 0;
		}

		kern_watermark += len_to_copy;
		if (kern_sglist[j].len == kern_watermark) {
			j++;
			kern_watermark = 0;
		}
	}

	CTL_DEBUG_PRINT(("%s: ext_sg_entries: %d, kern_sg_entries: %d\n",
			 __func__, ext_sg_entries, kern_sg_entries));
	CTL_DEBUG_PRINT(("%s: ext_data_len = %d, kern_data_len = %d\n",
			 __func__, ctsio->ext_data_len, ctsio->kern_data_len));

bailout:
	io->scsiio.be_move_done(io);
}

static void
tpcl_done(union ctl_io *io)
{

	tpc_done(io);
}

uint64_t
tpcl_resolve(struct ctl_softc *softc, int init_port,
    struct scsi_ec_cscd *cscd, uint32_t *ss, uint32_t *ps, uint32_t *pso)
{
	struct scsi_ec_cscd_id *cscdid;
	struct ctl_port *port;
	struct ctl_lun *lun;
	uint64_t lunid = UINT64_MAX;

	if (cscd->type_code != EC_CSCD_ID ||
	    (cscd->luidt_pdt & EC_LUIDT_MASK) != EC_LUIDT_LUN ||
	    (cscd->luidt_pdt & EC_NUL) != 0)
		return (lunid);

	cscdid = (struct scsi_ec_cscd_id *)cscd;
	mtx_lock(&softc->ctl_lock);
	if (init_port >= 0)
		port = softc->ctl_ports[init_port];
	else
		port = NULL;
	STAILQ_FOREACH(lun, &softc->lun_list, links) {
		if (port != NULL &&
		    ctl_lun_map_to_port(port, lun->lun) == UINT32_MAX)
			continue;
		if (lun->lun_devid == NULL)
			continue;
		if (scsi_devid_match(lun->lun_devid->data,
		    lun->lun_devid->len, &cscdid->codeset,
		    cscdid->length + 4) == 0) {
			lunid = lun->lun;
			if (ss && lun->be_lun)
				*ss = lun->be_lun->blocksize;
			if (ps && lun->be_lun)
				*ps = lun->be_lun->blocksize <<
				    lun->be_lun->pblockexp;
			if (pso && lun->be_lun)
				*pso = lun->be_lun->blocksize *
				    lun->be_lun->pblockoff;
			break;
		}
	}
	mtx_unlock(&softc->ctl_lock);
	return (lunid);
};

union ctl_io *
tpcl_alloc_io(void)
{
	struct tpcl_softc *tsoftc = &tpcl_softc;

	return (ctl_alloc_io(tsoftc->port.ctl_pool_ref));
};

int
tpcl_queue(union ctl_io *io, uint64_t lun)
{
	struct tpcl_softc *tsoftc = &tpcl_softc;

	io->io_hdr.nexus.initid = 0;
	io->io_hdr.nexus.targ_port = tsoftc->port.targ_port;
	io->io_hdr.nexus.targ_lun = lun;
	io->scsiio.tag_num = atomic_fetchadd_int(&tsoftc->cur_tag_num, 1);
	io->scsiio.ext_data_filled = 0;
	return (ctl_queue(io));
}
