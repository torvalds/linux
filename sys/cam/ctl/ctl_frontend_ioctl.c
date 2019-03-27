/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2009 Silicon Graphics International Corp.
 * Copyright (c) 2012 The FreeBSD Foundation
 * Copyright (c) 2015 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2017 Jakub Wojciech Klama <jceel@FreeBSD.org>
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
#include <sys/nv.h>
#include <sys/dnv.h>

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
#include <cam/ctl/ctl_error.h>

typedef enum {
	CTL_IOCTL_INPROG,
	CTL_IOCTL_DATAMOVE,
	CTL_IOCTL_DONE
} ctl_fe_ioctl_state;

struct ctl_fe_ioctl_params {
	struct cv		sem;
	struct mtx		ioctl_mtx;
	ctl_fe_ioctl_state	state;
};

struct cfi_port {
	TAILQ_ENTRY(cfi_port)	link;
	uint32_t		cur_tag_num;
	struct cdev *		dev;
	struct ctl_port		port;
};

struct cfi_softc {
	TAILQ_HEAD(, cfi_port)	ports;
};


static struct cfi_softc cfi_softc;


static int cfi_init(void);
static int cfi_shutdown(void);
static void cfi_datamove(union ctl_io *io);
static void cfi_done(union ctl_io *io);
static int cfi_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
    struct thread *td);
static void cfi_ioctl_port_create(struct ctl_req *req);
static void cfi_ioctl_port_remove(struct ctl_req *req);

static struct cdevsw cfi_cdevsw = {
	.d_version = D_VERSION,
	.d_flags = 0,
	.d_ioctl = ctl_ioctl_io
};

static struct ctl_frontend cfi_frontend =
{
	.name = "ioctl",
	.init = cfi_init,
	.ioctl = cfi_ioctl,
	.shutdown = cfi_shutdown,
};
CTL_FRONTEND_DECLARE(ctlioctl, cfi_frontend);

static int
cfi_init(void)
{
	struct cfi_softc *isoftc = &cfi_softc;
	struct cfi_port *cfi;
	struct ctl_port *port;
	int error = 0;

	memset(isoftc, 0, sizeof(*isoftc));
	TAILQ_INIT(&isoftc->ports);

	cfi = malloc(sizeof(*cfi), M_CTL, M_WAITOK | M_ZERO);
	port = &cfi->port;
	port->frontend = &cfi_frontend;
	port->port_type = CTL_PORT_IOCTL;
	port->num_requested_ctl_io = 100;
	port->port_name = "ioctl";
	port->fe_datamove = cfi_datamove;
	port->fe_done = cfi_done;
	port->physical_port = 0;
	port->targ_port = -1;

	if ((error = ctl_port_register(port)) != 0) {
		printf("%s: ioctl port registration failed\n", __func__);
		return (error);
	}

	ctl_port_online(port);
	TAILQ_INSERT_TAIL(&isoftc->ports, cfi, link);
	return (0);
}

static int
cfi_shutdown(void)
{
	struct cfi_softc *isoftc = &cfi_softc;
	struct cfi_port *cfi, *temp;
	struct ctl_port *port;
	int error;

	TAILQ_FOREACH_SAFE(cfi, &isoftc->ports, link, temp) {
		port = &cfi->port;
		ctl_port_offline(port);
		error = ctl_port_deregister(port);
		if (error != 0) {
			printf("%s: ctl_frontend_deregister() failed\n",
			   __func__);
			return (error);
		}

		TAILQ_REMOVE(&isoftc->ports, cfi, link);
		free(cfi, M_CTL);
	}

	return (0);
}

static void
cfi_ioctl_port_create(struct ctl_req *req)
{
	struct cfi_softc *isoftc = &cfi_softc;
	struct cfi_port *cfi;
	struct ctl_port *port;
	struct make_dev_args args;
	const char *val;
	int retval;
	int pp = -1, vp = 0;

	val = dnvlist_get_string(req->args_nvl, "pp", NULL);
	if (val != NULL)
		pp = strtol(val, NULL, 10);
	
	val = dnvlist_get_string(req->args_nvl, "vp", NULL);
	if (val != NULL)
		vp = strtol(val, NULL, 10);

	if (pp != -1) {
		/* Check for duplicates */
		TAILQ_FOREACH(cfi, &isoftc->ports, link) {
			if (pp == cfi->port.physical_port && 
			    vp == cfi->port.virtual_port) {
				req->status = CTL_LUN_ERROR;
				snprintf(req->error_str, sizeof(req->error_str),
				    "port %d already exists", pp);

				return;
			}
		}
	} else {
		/* Find free port number */
		TAILQ_FOREACH(cfi, &isoftc->ports, link) {
			pp = MAX(pp, cfi->port.physical_port);
		}

		pp++;
	}

	cfi = malloc(sizeof(*cfi), M_CTL, M_WAITOK | M_ZERO);
	port = &cfi->port;
	port->frontend = &cfi_frontend;
	port->port_type = CTL_PORT_IOCTL;
	port->num_requested_ctl_io = 100;
	port->port_name = "ioctl";
	port->fe_datamove = cfi_datamove;
	port->fe_done = cfi_done;
	port->physical_port = pp;
	port->virtual_port = vp;
	port->targ_port = -1;

	retval = ctl_port_register(port);
	if (retval != 0) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "ctl_port_register() failed with error %d", retval);
		free(port, M_CTL);
		return;
	}

	req->result_nvl = nvlist_create(0);
	nvlist_add_number(req->result_nvl, "port_id", port->targ_port);
	ctl_port_online(port);

	make_dev_args_init(&args);
	args.mda_devsw = &cfi_cdevsw;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0600;
	args.mda_si_drv1 = NULL;
	args.mda_si_drv2 = cfi;

	retval = make_dev_s(&args, &cfi->dev, "cam/ctl%d.%d", pp, vp);
	if (retval != 0) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "make_dev_s() failed with error %d", retval);
		free(port, M_CTL);
		return;
	}

	req->status = CTL_LUN_OK;
	TAILQ_INSERT_TAIL(&isoftc->ports, cfi, link);
}

static void
cfi_ioctl_port_remove(struct ctl_req *req)
{
	struct cfi_softc *isoftc = &cfi_softc;
	struct cfi_port *cfi = NULL;
	const char *val;
	int port_id = -1;

	val = dnvlist_get_string(req->args_nvl, "port_id", NULL);
	if (val != NULL)
		port_id = strtol(val, NULL, 10);

	if (port_id == -1) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "port_id not provided");
		return;
	}

	TAILQ_FOREACH(cfi, &isoftc->ports, link) {
		if (cfi->port.targ_port == port_id)
			break;
	}

	if (cfi == NULL) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "cannot find port %d", port_id);

		return;
	}

	if (cfi->port.physical_port == 0 && cfi->port.virtual_port == 0) {
		req->status = CTL_LUN_ERROR;
		snprintf(req->error_str, sizeof(req->error_str),
		    "cannot destroy default ioctl port");

		return;
	}

	ctl_port_offline(&cfi->port);
	ctl_port_deregister(&cfi->port);
	TAILQ_REMOVE(&isoftc->ports, cfi, link);
	destroy_dev(cfi->dev);
	free(cfi, M_CTL);
	req->status = CTL_LUN_OK;
}

static int
cfi_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
    struct thread *td)
{
	struct ctl_req *req;

	if (cmd == CTL_PORT_REQ) {
		req = (struct ctl_req *)addr;
		switch (req->reqtype) {
		case CTL_REQ_CREATE:
			cfi_ioctl_port_create(req);
			break;
		case CTL_REQ_REMOVE:
			cfi_ioctl_port_remove(req);
			break;
		default:
			req->status = CTL_LUN_ERROR;
			snprintf(req->error_str, sizeof(req->error_str),
			    "Unsupported request type %d", req->reqtype);
		}
		return (0);
	}

	return (ENOTTY);
}

/*
 * Data movement routine for the CTL ioctl frontend port.
 */
static int
ctl_ioctl_do_datamove(struct ctl_scsiio *ctsio)
{
	struct ctl_sg_entry *ext_sglist, *kern_sglist;
	struct ctl_sg_entry ext_entry, kern_entry;
	int ext_sglen, ext_sg_entries, kern_sg_entries;
	int ext_sg_start, ext_offset;
	int len_to_copy;
	int kern_watermark, ext_watermark;
	int ext_sglist_malloced;
	int i, j;

	CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove\n"));

	/*
	 * If this flag is set, fake the data transfer.
	 */
	if (ctsio->io_hdr.flags & CTL_FLAG_NO_DATAMOVE) {
		ext_sglist_malloced = 0;
		ctsio->ext_data_filled += ctsio->kern_data_len;
		ctsio->kern_data_resid = 0;
		goto bailout;
	}

	/*
	 * To simplify things here, if we have a single buffer, stick it in
	 * a S/G entry and just make it a single entry S/G list.
	 */
	if (ctsio->ext_sg_entries > 0) {
		int len_seen;

		ext_sglen = ctsio->ext_sg_entries * sizeof(*ext_sglist);
		ext_sglist = (struct ctl_sg_entry *)malloc(ext_sglen, M_CTL,
							   M_WAITOK);
		ext_sglist_malloced = 1;
		if (copyin(ctsio->ext_data_ptr, ext_sglist, ext_sglen) != 0) {
			ctsio->io_hdr.port_status = 31343;
			goto bailout;
		}
		ext_sg_entries = ctsio->ext_sg_entries;
		ext_sg_start = ext_sg_entries;
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
		ext_sglist_malloced = 0;
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

		len_to_copy = MIN(ext_sglist[i].len - ext_watermark,
				  kern_sglist[j].len - kern_watermark);

		ext_ptr = (uint8_t *)ext_sglist[i].addr;
		ext_ptr = ext_ptr + ext_watermark;
		if (ctsio->io_hdr.flags & CTL_FLAG_BUS_ADDR) {
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
			CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove: copying %d "
					 "bytes to user\n", len_to_copy));
			CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove: from %p "
					 "to %p\n", kern_ptr, ext_ptr));
			if (copyout(kern_ptr, ext_ptr, len_to_copy) != 0) {
				ctsio->io_hdr.port_status = 31344;
				goto bailout;
			}
		} else {
			CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove: copying %d "
					 "bytes from user\n", len_to_copy));
			CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove: from %p "
					 "to %p\n", ext_ptr, kern_ptr));
			if (copyin(ext_ptr, kern_ptr, len_to_copy)!= 0){
				ctsio->io_hdr.port_status = 31345;
				goto bailout;
			}
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

	CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove: ext_sg_entries: %d, "
			 "kern_sg_entries: %d\n", ext_sg_entries,
			 kern_sg_entries));
	CTL_DEBUG_PRINT(("ctl_ioctl_do_datamove: ext_data_len = %d, "
			 "kern_data_len = %d\n", ctsio->ext_data_len,
			 ctsio->kern_data_len));

bailout:
	if (ext_sglist_malloced != 0)
		free(ext_sglist, M_CTL);

	return (CTL_RETVAL_COMPLETE);
}

static void
cfi_datamove(union ctl_io *io)
{
	struct ctl_fe_ioctl_params *params;

	params = (struct ctl_fe_ioctl_params *)
		io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;

	mtx_lock(&params->ioctl_mtx);
	params->state = CTL_IOCTL_DATAMOVE;
	cv_broadcast(&params->sem);
	mtx_unlock(&params->ioctl_mtx);
}

static void
cfi_done(union ctl_io *io)
{
	struct ctl_fe_ioctl_params *params;

	params = (struct ctl_fe_ioctl_params *)
		io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr;

	mtx_lock(&params->ioctl_mtx);
	params->state = CTL_IOCTL_DONE;
	cv_broadcast(&params->sem);
	mtx_unlock(&params->ioctl_mtx);
}

static int
cfi_submit_wait(union ctl_io *io)
{
	struct ctl_fe_ioctl_params params;
	ctl_fe_ioctl_state last_state;
	int done, retval;

	bzero(&params, sizeof(params));
	mtx_init(&params.ioctl_mtx, "ctliocmtx", NULL, MTX_DEF);
	cv_init(&params.sem, "ctlioccv");
	params.state = CTL_IOCTL_INPROG;
	last_state = params.state;

	io->io_hdr.ctl_private[CTL_PRIV_FRONTEND].ptr = &params;

	CTL_DEBUG_PRINT(("cfi_submit_wait\n"));

	/* This shouldn't happen */
	if ((retval = ctl_queue(io)) != CTL_RETVAL_COMPLETE)
		return (retval);

	done = 0;

	do {
		mtx_lock(&params.ioctl_mtx);
		/*
		 * Check the state here, and don't sleep if the state has
		 * already changed (i.e. wakeup has already occurred, but we
		 * weren't waiting yet).
		 */
		if (params.state == last_state) {
			/* XXX KDM cv_wait_sig instead? */
			cv_wait(&params.sem, &params.ioctl_mtx);
		}
		last_state = params.state;

		switch (params.state) {
		case CTL_IOCTL_INPROG:
			/* Why did we wake up? */
			/* XXX KDM error here? */
			mtx_unlock(&params.ioctl_mtx);
			break;
		case CTL_IOCTL_DATAMOVE:
			CTL_DEBUG_PRINT(("got CTL_IOCTL_DATAMOVE\n"));

			/*
			 * change last_state back to INPROG to avoid
			 * deadlock on subsequent data moves.
			 */
			params.state = last_state = CTL_IOCTL_INPROG;

			mtx_unlock(&params.ioctl_mtx);
			ctl_ioctl_do_datamove(&io->scsiio);
			/*
			 * Note that in some cases, most notably writes,
			 * this will queue the I/O and call us back later.
			 * In other cases, generally reads, this routine
			 * will immediately call back and wake us up,
			 * probably using our own context.
			 */
			io->scsiio.be_move_done(io);
			break;
		case CTL_IOCTL_DONE:
			mtx_unlock(&params.ioctl_mtx);
			CTL_DEBUG_PRINT(("got CTL_IOCTL_DONE\n"));
			done = 1;
			break;
		default:
			mtx_unlock(&params.ioctl_mtx);
			/* XXX KDM error here? */
			break;
		}
	} while (done == 0);

	mtx_destroy(&params.ioctl_mtx);
	cv_destroy(&params.sem);

	return (CTL_RETVAL_COMPLETE);
}

int
ctl_ioctl_io(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
    struct thread *td)
{
	struct cfi_port *cfi;
	union ctl_io *io;
	void *pool_tmp, *sc_tmp;
	int retval = 0;

	if (cmd != CTL_IO)
		return (ENOTTY);

	cfi = dev->si_drv2 == NULL
	    ? TAILQ_FIRST(&cfi_softc.ports)
	    : dev->si_drv2;

	/*
	 * If we haven't been "enabled", don't allow any SCSI I/O
	 * to this FETD.
	 */
	if ((cfi->port.status & CTL_PORT_STATUS_ONLINE) == 0)
		return (EPERM);

	io = ctl_alloc_io(cfi->port.ctl_pool_ref);

	/*
	 * Need to save the pool reference so it doesn't get
	 * spammed by the user's ctl_io.
	 */
	pool_tmp = io->io_hdr.pool;
	sc_tmp = CTL_SOFTC(io);
	memcpy(io, (void *)addr, sizeof(*io));
	io->io_hdr.pool = pool_tmp;
	CTL_SOFTC(io) = sc_tmp;
	TAILQ_INIT(&io->io_hdr.blocked_queue);

	/*
	 * No status yet, so make sure the status is set properly.
	 */
	io->io_hdr.status = CTL_STATUS_NONE;

	/*
	 * The user sets the initiator ID, target and LUN IDs.
	 */
	io->io_hdr.nexus.targ_port = cfi->port.targ_port;
	io->io_hdr.flags |= CTL_FLAG_USER_REQ;
	if ((io->io_hdr.io_type == CTL_IO_SCSI) &&
	    (io->scsiio.tag_type != CTL_TAG_UNTAGGED))
		io->scsiio.tag_num = cfi->cur_tag_num++;

	retval = cfi_submit_wait(io);
	if (retval == 0)
		memcpy((void *)addr, io, sizeof(*io));

	ctl_free_io(io);
	return (retval);
}
