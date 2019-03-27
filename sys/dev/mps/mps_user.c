/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD userland interface
 */
/*-
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
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
 *
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* TODO Move headers to mpsvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/ioccom.h>
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/proc.h>
#include <sys/sysent.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>

#include <dev/mps/mpi/mpi2_type.h>
#include <dev/mps/mpi/mpi2.h>
#include <dev/mps/mpi/mpi2_ioc.h>
#include <dev/mps/mpi/mpi2_cnfg.h>
#include <dev/mps/mpi/mpi2_init.h>
#include <dev/mps/mpi/mpi2_tool.h>
#include <dev/mps/mps_ioctl.h>
#include <dev/mps/mpsvar.h>
#include <dev/mps/mps_table.h>
#include <dev/mps/mps_sas.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

static d_open_t		mps_open;
static d_close_t	mps_close;
static d_ioctl_t	mps_ioctl_devsw;

static struct cdevsw mps_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	mps_open,
	.d_close =	mps_close,
	.d_ioctl =	mps_ioctl_devsw,
	.d_name =	"mps",
};

typedef int (mps_user_f)(struct mps_command *, struct mps_usr_command *);
static mps_user_f	mpi_pre_ioc_facts;
static mps_user_f	mpi_pre_port_facts;
static mps_user_f	mpi_pre_fw_download;
static mps_user_f	mpi_pre_fw_upload;
static mps_user_f	mpi_pre_sata_passthrough;
static mps_user_f	mpi_pre_smp_passthrough;
static mps_user_f	mpi_pre_config;
static mps_user_f	mpi_pre_sas_io_unit_control;

static int mps_user_read_cfg_header(struct mps_softc *,
				    struct mps_cfg_page_req *);
static int mps_user_read_cfg_page(struct mps_softc *,
				  struct mps_cfg_page_req *, void *);
static int mps_user_read_extcfg_header(struct mps_softc *,
				     struct mps_ext_cfg_page_req *);
static int mps_user_read_extcfg_page(struct mps_softc *,
				     struct mps_ext_cfg_page_req *, void *);
static int mps_user_write_cfg_page(struct mps_softc *,
				   struct mps_cfg_page_req *, void *);
static int mps_user_setup_request(struct mps_command *,
				  struct mps_usr_command *);
static int mps_user_command(struct mps_softc *, struct mps_usr_command *);

static int mps_user_pass_thru(struct mps_softc *sc, mps_pass_thru_t *data);
static void mps_user_get_adapter_data(struct mps_softc *sc,
    mps_adapter_data_t *data);
static void mps_user_read_pci_info(struct mps_softc *sc,
    mps_pci_info_t *data);
static uint8_t mps_get_fw_diag_buffer_number(struct mps_softc *sc,
    uint32_t unique_id);
static int mps_post_fw_diag_buffer(struct mps_softc *sc,
    mps_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code);
static int mps_release_fw_diag_buffer(struct mps_softc *sc,
    mps_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code,
    uint32_t diag_type);
static int mps_diag_register(struct mps_softc *sc,
    mps_fw_diag_register_t *diag_register, uint32_t *return_code);
static int mps_diag_unregister(struct mps_softc *sc,
    mps_fw_diag_unregister_t *diag_unregister, uint32_t *return_code);
static int mps_diag_query(struct mps_softc *sc, mps_fw_diag_query_t *diag_query,
    uint32_t *return_code);
static int mps_diag_read_buffer(struct mps_softc *sc,
    mps_diag_read_buffer_t *diag_read_buffer, uint8_t *ioctl_buf,
    uint32_t *return_code);
static int mps_diag_release(struct mps_softc *sc,
    mps_fw_diag_release_t *diag_release, uint32_t *return_code);
static int mps_do_diag_action(struct mps_softc *sc, uint32_t action,
    uint8_t *diag_action, uint32_t length, uint32_t *return_code);
static int mps_user_diag_action(struct mps_softc *sc, mps_diag_action_t *data);
static void mps_user_event_query(struct mps_softc *sc, mps_event_query_t *data);
static void mps_user_event_enable(struct mps_softc *sc,
    mps_event_enable_t *data);
static int mps_user_event_report(struct mps_softc *sc,
    mps_event_report_t *data);
static int mps_user_reg_access(struct mps_softc *sc, mps_reg_access_t *data);
static int mps_user_btdh(struct mps_softc *sc, mps_btdh_mapping_t *data);

MALLOC_DEFINE(M_MPSUSER, "mps_user", "Buffers for mps(4) ioctls");

/* Macros from compat/freebsd32/freebsd32.h */
#define	PTRIN(v)	(void *)(uintptr_t)(v)
#define	PTROUT(v)	(uint32_t)(uintptr_t)(v)

#define	CP(src,dst,fld) do { (dst).fld = (src).fld; } while (0)
#define	PTRIN_CP(src,dst,fld)				\
	do { (dst).fld = PTRIN((src).fld); } while (0)
#define	PTROUT_CP(src,dst,fld) \
	do { (dst).fld = PTROUT((src).fld); } while (0)

int
mps_attach_user(struct mps_softc *sc)
{
	int unit;

	unit = device_get_unit(sc->mps_dev);
	sc->mps_cdev = make_dev(&mps_cdevsw, unit, UID_ROOT, GID_OPERATOR, 0640,
	    "mps%d", unit);
	if (sc->mps_cdev == NULL) {
		return (ENOMEM);
	}
	sc->mps_cdev->si_drv1 = sc;
	return (0);
}

void
mps_detach_user(struct mps_softc *sc)
{

	/* XXX: do a purge of pending requests? */
	if (sc->mps_cdev != NULL)
		destroy_dev(sc->mps_cdev);
}

static int
mps_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
mps_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
mps_user_read_cfg_header(struct mps_softc *sc,
    struct mps_cfg_page_req *page_req)
{
	MPI2_CONFIG_PAGE_HEADER *hdr;
	struct mps_config_params params;
	int	    error;

	hdr = &params.hdr.Struct;
	params.action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	params.page_address = le32toh(page_req->page_address);
	hdr->PageVersion = 0;
	hdr->PageLength = 0;
	hdr->PageNumber = page_req->header.PageNumber;
	hdr->PageType = page_req->header.PageType;
	params.buffer = NULL;
	params.length = 0;
	params.callback = NULL;

	if ((error = mps_read_config_page(sc, &params)) != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mps_printf(sc, "read_cfg_header timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(params.status);
	if ((page_req->ioc_status & MPI2_IOCSTATUS_MASK) ==
	    MPI2_IOCSTATUS_SUCCESS) {
		bcopy(hdr, &page_req->header, sizeof(page_req->header));
	}

	return (0);
}

static int
mps_user_read_cfg_page(struct mps_softc *sc, struct mps_cfg_page_req *page_req,
    void *buf)
{
	MPI2_CONFIG_PAGE_HEADER *reqhdr, *hdr;
	struct mps_config_params params;
	int	      error;

	reqhdr = buf;
	hdr = &params.hdr.Struct;
	hdr->PageVersion = reqhdr->PageVersion;
	hdr->PageLength = reqhdr->PageLength;
	hdr->PageNumber = reqhdr->PageNumber;
	hdr->PageType = reqhdr->PageType & MPI2_CONFIG_PAGETYPE_MASK;
	params.action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	params.page_address = le32toh(page_req->page_address);
	params.buffer = buf;
	params.length = le32toh(page_req->len);
	params.callback = NULL;

	if ((error = mps_read_config_page(sc, &params)) != 0) {
		mps_printf(sc, "mps_user_read_cfg_page timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(params.status);
	return (0);
}

static int
mps_user_read_extcfg_header(struct mps_softc *sc,
    struct mps_ext_cfg_page_req *ext_page_req)
{
	MPI2_CONFIG_EXTENDED_PAGE_HEADER *hdr;
	struct mps_config_params params;
	int	    error;

	hdr = &params.hdr.Ext;
	params.action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	hdr->PageVersion = ext_page_req->header.PageVersion;
	hdr->PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	hdr->ExtPageLength = 0;
	hdr->PageNumber = ext_page_req->header.PageNumber;
	hdr->ExtPageType = ext_page_req->header.ExtPageType;
	params.page_address = le32toh(ext_page_req->page_address);
	params.buffer = NULL;
	params.length = 0;
	params.callback = NULL;

	if ((error = mps_read_config_page(sc, &params)) != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mps_printf(sc, "mps_user_read_extcfg_header timed out\n");
		return (ETIMEDOUT);
	}

	ext_page_req->ioc_status = htole16(params.status);
	if ((ext_page_req->ioc_status & MPI2_IOCSTATUS_MASK) ==
	    MPI2_IOCSTATUS_SUCCESS) {
		ext_page_req->header.PageVersion = hdr->PageVersion;
		ext_page_req->header.PageNumber = hdr->PageNumber;
		ext_page_req->header.PageType = hdr->PageType;
		ext_page_req->header.ExtPageLength = hdr->ExtPageLength;
		ext_page_req->header.ExtPageType = hdr->ExtPageType;
	}

	return (0);
}

static int
mps_user_read_extcfg_page(struct mps_softc *sc,
    struct mps_ext_cfg_page_req *ext_page_req, void *buf)
{
	MPI2_CONFIG_EXTENDED_PAGE_HEADER *reqhdr, *hdr;
	struct mps_config_params params;
	int error;

	reqhdr = buf;
	hdr = &params.hdr.Ext;
	params.action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	params.page_address = le32toh(ext_page_req->page_address);
	hdr->PageVersion = reqhdr->PageVersion;
	hdr->PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	hdr->PageNumber = reqhdr->PageNumber;
	hdr->ExtPageType = reqhdr->ExtPageType;
	hdr->ExtPageLength = reqhdr->ExtPageLength;
	params.buffer = buf;
	params.length = le32toh(ext_page_req->len);
	params.callback = NULL;

	if ((error = mps_read_config_page(sc, &params)) != 0) {
		mps_printf(sc, "mps_user_read_extcfg_page timed out\n");
		return (ETIMEDOUT);
	}

	ext_page_req->ioc_status = htole16(params.status);
	return (0);
}

static int
mps_user_write_cfg_page(struct mps_softc *sc,
    struct mps_cfg_page_req *page_req, void *buf)
{
	MPI2_CONFIG_PAGE_HEADER *reqhdr, *hdr;
	struct mps_config_params params;
	u_int	      hdr_attr;
	int	      error;

	reqhdr = buf;
	hdr = &params.hdr.Struct;
	hdr_attr = reqhdr->PageType & MPI2_CONFIG_PAGEATTR_MASK;
	if (hdr_attr != MPI2_CONFIG_PAGEATTR_CHANGEABLE &&
	    hdr_attr != MPI2_CONFIG_PAGEATTR_PERSISTENT) {
		mps_printf(sc, "page type 0x%x not changeable\n",
			reqhdr->PageType & MPI2_CONFIG_PAGETYPE_MASK);
		return (EINVAL);
	}

	/*
	 * There isn't any point in restoring stripped out attributes
	 * if you then mask them going down to issue the request.
	 */

	hdr->PageVersion = reqhdr->PageVersion;
	hdr->PageLength = reqhdr->PageLength;
	hdr->PageNumber = reqhdr->PageNumber;
	hdr->PageType = reqhdr->PageType;
	params.action = MPI2_CONFIG_ACTION_PAGE_WRITE_CURRENT;
	params.page_address = le32toh(page_req->page_address);
	params.buffer = buf;
	params.length = le32toh(page_req->len);
	params.callback = NULL;

	if ((error = mps_write_config_page(sc, &params)) != 0) {
		mps_printf(sc, "mps_write_cfg_page timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(params.status);
	return (0);
}

void
mpi_init_sge(struct mps_command *cm, void *req, void *sge)
{
	int off, space;

	space = (int)cm->cm_sc->reqframesz;
	off = (uintptr_t)sge - (uintptr_t)req;

	KASSERT(off < space, ("bad pointers %p %p, off %d, space %d",
            req, sge, off, space));

	cm->cm_sge = sge;
	cm->cm_sglsize = space - off;
}

/*
 * Prepare the mps_command for an IOC_FACTS request.
 */
static int
mpi_pre_ioc_facts(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_IOC_FACTS_REQUEST *req = (void *)cm->cm_req;
	MPI2_IOC_FACTS_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	cm->cm_sge = NULL;
	cm->cm_sglsize = 0;
	return (0);
}

/*
 * Prepare the mps_command for a PORT_FACTS request.
 */
static int
mpi_pre_port_facts(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_PORT_FACTS_REQUEST *req = (void *)cm->cm_req;
	MPI2_PORT_FACTS_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	cm->cm_sge = NULL;
	cm->cm_sglsize = 0;
	return (0);
}

/*
 * Prepare the mps_command for a FW_DOWNLOAD request.
 */
static int
mpi_pre_fw_download(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_FW_DOWNLOAD_REQUEST *req = (void *)cm->cm_req;
	MPI2_FW_DOWNLOAD_REPLY *rpl;
	MPI2_FW_DOWNLOAD_TCSGE tc;
	int error;

	/*
	 * This code assumes there is room in the request's SGL for
	 * the TransactionContext plus at least a SGL chain element.
	 */
	CTASSERT(sizeof req->SGL >= sizeof tc + MPS_SGC_SIZE);

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	if (cmd->len == 0)
		return (EINVAL);

	error = copyin(cmd->buf, cm->cm_data, cmd->len);
	if (error != 0)
		return (error);

	mpi_init_sge(cm, req, &req->SGL);
	bzero(&tc, sizeof tc);

	/*
	 * For now, the F/W image must be provided in a single request.
	 */
	if ((req->MsgFlags & MPI2_FW_DOWNLOAD_MSGFLGS_LAST_SEGMENT) == 0)
		return (EINVAL);
	if (req->TotalImageSize != cmd->len)
		return (EINVAL);

	/*
	 * The value of the first two elements is specified in the
	 * Fusion-MPT Message Passing Interface document.
	 */
	tc.ContextSize = 0;
	tc.DetailsLength = 12;
	tc.ImageOffset = 0;
	tc.ImageSize = cmd->len;

	cm->cm_flags |= MPS_CM_FLAGS_DATAOUT;

	return (mps_push_sge(cm, &tc, sizeof tc, 0));
}

/*
 * Prepare the mps_command for a FW_UPLOAD request.
 */
static int
mpi_pre_fw_upload(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_FW_UPLOAD_REQUEST *req = (void *)cm->cm_req;
	MPI2_FW_UPLOAD_REPLY *rpl;
	MPI2_FW_UPLOAD_TCSGE tc;

	/*
	 * This code assumes there is room in the request's SGL for
	 * the TransactionContext plus at least a SGL chain element.
	 */
	CTASSERT(sizeof req->SGL >= sizeof tc + MPS_SGC_SIZE);

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpi_init_sge(cm, req, &req->SGL);
	bzero(&tc, sizeof tc);

	/*
	 * The value of the first two elements is specified in the
	 * Fusion-MPT Message Passing Interface document.
	 */
	tc.ContextSize = 0;
	tc.DetailsLength = 12;
	/*
	 * XXX Is there any reason to fetch a partial image?  I.e. to
	 * set ImageOffset to something other than 0?
	 */
	tc.ImageOffset = 0;
	tc.ImageSize = cmd->len;

	cm->cm_flags |= MPS_CM_FLAGS_DATAIN;

	return (mps_push_sge(cm, &tc, sizeof tc, 0));
}

/*
 * Prepare the mps_command for a SATA_PASSTHROUGH request.
 */
static int
mpi_pre_sata_passthrough(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_SATA_PASSTHROUGH_REQUEST *req = (void *)cm->cm_req;
	MPI2_SATA_PASSTHROUGH_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpi_init_sge(cm, req, &req->SGL);
	return (0);
}

/*
 * Prepare the mps_command for a SMP_PASSTHROUGH request.
 */
static int
mpi_pre_smp_passthrough(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_SMP_PASSTHROUGH_REQUEST *req = (void *)cm->cm_req;
	MPI2_SMP_PASSTHROUGH_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpi_init_sge(cm, req, &req->SGL);
	return (0);
}

/*
 * Prepare the mps_command for a CONFIG request.
 */
static int
mpi_pre_config(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_CONFIG_REQUEST *req = (void *)cm->cm_req;
	MPI2_CONFIG_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpi_init_sge(cm, req, &req->PageBufferSGE);
	return (0);
}

/*
 * Prepare the mps_command for a SAS_IO_UNIT_CONTROL request.
 */
static int
mpi_pre_sas_io_unit_control(struct mps_command *cm,
			     struct mps_usr_command *cmd)
{

	cm->cm_sge = NULL;
	cm->cm_sglsize = 0;
	return (0);
}

/*
 * A set of functions to prepare an mps_command for the various
 * supported requests.
 */
struct mps_user_func {
	U8		Function;
	mps_user_f	*f_pre;
} mps_user_func_list[] = {
	{ MPI2_FUNCTION_IOC_FACTS,		mpi_pre_ioc_facts },
	{ MPI2_FUNCTION_PORT_FACTS,		mpi_pre_port_facts },
	{ MPI2_FUNCTION_FW_DOWNLOAD, 		mpi_pre_fw_download },
	{ MPI2_FUNCTION_FW_UPLOAD,		mpi_pre_fw_upload },
	{ MPI2_FUNCTION_SATA_PASSTHROUGH,	mpi_pre_sata_passthrough },
	{ MPI2_FUNCTION_SMP_PASSTHROUGH,	mpi_pre_smp_passthrough},
	{ MPI2_FUNCTION_CONFIG,			mpi_pre_config},
	{ MPI2_FUNCTION_SAS_IO_UNIT_CONTROL,	mpi_pre_sas_io_unit_control },
	{ 0xFF,					NULL } /* list end */
};

static int
mps_user_setup_request(struct mps_command *cm, struct mps_usr_command *cmd)
{
	MPI2_REQUEST_HEADER *hdr = (MPI2_REQUEST_HEADER *)cm->cm_req;	
	struct mps_user_func *f;

	for (f = mps_user_func_list; f->f_pre != NULL; f++) {
		if (hdr->Function == f->Function)
			return (f->f_pre(cm, cmd));
	}
	return (EINVAL);
}	

static int
mps_user_command(struct mps_softc *sc, struct mps_usr_command *cmd)
{
	MPI2_REQUEST_HEADER *hdr;	
	MPI2_DEFAULT_REPLY *rpl;
	void *buf = NULL;
	struct mps_command *cm = NULL;
	int err = 0;
	int sz;

	mps_lock(sc);
	cm = mps_alloc_command(sc);

	if (cm == NULL) {
		mps_printf(sc, "%s: no mps requests\n", __func__);
		err = ENOMEM;
		goto RetFree;
	}
	mps_unlock(sc);

	hdr = (MPI2_REQUEST_HEADER *)cm->cm_req;

	mps_dprint(sc, MPS_USER, "%s: req %p %d  rpl %p %d\n", __func__,
	    cmd->req, cmd->req_len, cmd->rpl, cmd->rpl_len);

	if (cmd->req_len > (int)sc->reqframesz) {
		err = EINVAL;
		goto RetFreeUnlocked;
	}
	err = copyin(cmd->req, hdr, cmd->req_len);
	if (err != 0)
		goto RetFreeUnlocked;

	mps_dprint(sc, MPS_USER, "%s: Function %02X MsgFlags %02X\n", __func__,
	    hdr->Function, hdr->MsgFlags);

	if (cmd->len > 0) {
		buf = malloc(cmd->len, M_MPSUSER, M_WAITOK|M_ZERO);
		cm->cm_data = buf;
		cm->cm_length = cmd->len;
	} else {
		cm->cm_data = NULL;
		cm->cm_length = 0;
	}

	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;

	err = mps_user_setup_request(cm, cmd);
	if (err == EINVAL) {
		mps_printf(sc, "%s: unsupported parameter or unsupported "
		    "function in request (function = 0x%X)\n", __func__,
		    hdr->Function);
	}
	if (err != 0)
		goto RetFreeUnlocked;

	mps_lock(sc);
	err = mps_wait_command(sc, &cm, 60, CAN_SLEEP);

	if (err || (cm == NULL)) {
		mps_printf(sc, "%s: invalid request: error %d\n",
		    __func__, err);
		goto RetFree;
	}

	rpl = (MPI2_DEFAULT_REPLY *)cm->cm_reply;
	if (rpl != NULL)
		sz = rpl->MsgLength * 4;
	else
		sz = 0;
	
	if (sz > cmd->rpl_len) {
		mps_printf(sc, "%s: user reply buffer (%d) smaller than "
		    "returned buffer (%d)\n", __func__, cmd->rpl_len, sz);
		sz = cmd->rpl_len;
	}	

	mps_unlock(sc);
	copyout(rpl, cmd->rpl, sz);
	if (buf != NULL)
		copyout(buf, cmd->buf, cmd->len);
	mps_dprint(sc, MPS_USER, "%s: reply size %d\n", __func__, sz);

RetFreeUnlocked:
	mps_lock(sc);
RetFree:
	if (cm != NULL)
		mps_free_command(sc, cm);
	mps_unlock(sc);
	if (buf != NULL)
		free(buf, M_MPSUSER);
	return (err);
}

static int
mps_user_pass_thru(struct mps_softc *sc, mps_pass_thru_t *data)
{
	MPI2_REQUEST_HEADER	*hdr, tmphdr;	
	MPI2_DEFAULT_REPLY	*rpl = NULL;
	struct mps_command	*cm = NULL;
	int			err = 0, dir = 0, sz;
	uint8_t			function = 0;
	u_int			sense_len;
	struct mpssas_target	*targ = NULL;

	/*
	 * Only allow one passthru command at a time.  Use the MPS_FLAGS_BUSY
	 * bit to denote that a passthru is being processed.
	 */
	mps_lock(sc);
	if (sc->mps_flags & MPS_FLAGS_BUSY) {
		mps_dprint(sc, MPS_USER, "%s: Only one passthru command "
		    "allowed at a single time.", __func__);
		mps_unlock(sc);
		return (EBUSY);
	}
	sc->mps_flags |= MPS_FLAGS_BUSY;
	mps_unlock(sc);

	/*
	 * Do some validation on data direction.  Valid cases are:
	 *    1) DataSize is 0 and direction is NONE
	 *    2) DataSize is non-zero and one of:
	 *        a) direction is READ or
	 *        b) direction is WRITE or
	 *        c) direction is BOTH and DataOutSize is non-zero
	 * If valid and the direction is BOTH, change the direction to READ.
	 * if valid and the direction is not BOTH, make sure DataOutSize is 0.
	 */
	if (((data->DataSize == 0) &&
	    (data->DataDirection == MPS_PASS_THRU_DIRECTION_NONE)) ||
	    ((data->DataSize != 0) &&
	    ((data->DataDirection == MPS_PASS_THRU_DIRECTION_READ) ||
	    (data->DataDirection == MPS_PASS_THRU_DIRECTION_WRITE) ||
	    ((data->DataDirection == MPS_PASS_THRU_DIRECTION_BOTH) &&
	    (data->DataOutSize != 0))))) {
		if (data->DataDirection == MPS_PASS_THRU_DIRECTION_BOTH)
			data->DataDirection = MPS_PASS_THRU_DIRECTION_READ;
		else
			data->DataOutSize = 0;
	} else
		return (EINVAL);

	mps_dprint(sc, MPS_USER, "%s: req 0x%jx %d  rpl 0x%jx %d "
	    "data in 0x%jx %d data out 0x%jx %d data dir %d\n", __func__,
	    data->PtrRequest, data->RequestSize, data->PtrReply,
	    data->ReplySize, data->PtrData, data->DataSize,
	    data->PtrDataOut, data->DataOutSize, data->DataDirection);

	/*
	 * copy in the header so we know what we're dealing with before we
	 * commit to allocating a command for it.
	 */
	err = copyin(PTRIN(data->PtrRequest), &tmphdr, data->RequestSize);
	if (err != 0)
		goto RetFreeUnlocked;

	if (data->RequestSize > (int)sc->reqframesz) {
		err = EINVAL;
		goto RetFreeUnlocked;
	}

	function = tmphdr.Function;
	mps_dprint(sc, MPS_USER, "%s: Function %02X MsgFlags %02X\n", __func__,
	    function, tmphdr.MsgFlags);

	/*
	 * Handle a passthru TM request.
	 */
	if (function == MPI2_FUNCTION_SCSI_TASK_MGMT) {
		MPI2_SCSI_TASK_MANAGE_REQUEST	*task;

		mps_lock(sc);
		cm = mpssas_alloc_tm(sc);
		if (cm == NULL) {
			err = EINVAL;
			goto Ret;
		}

		/* Copy the header in.  Only a small fixup is needed. */
		task = (MPI2_SCSI_TASK_MANAGE_REQUEST *)cm->cm_req;
		bcopy(&tmphdr, task, data->RequestSize);
		task->TaskMID = cm->cm_desc.Default.SMID;

		cm->cm_data = NULL;
		cm->cm_complete = NULL;
		cm->cm_complete_data = NULL;

		targ = mpssas_find_target_by_handle(sc->sassc, 0,
		    task->DevHandle);
		if (targ == NULL) {
			mps_dprint(sc, MPS_INFO,
			   "%s %d : invalid handle for requested TM 0x%x \n",
			   __func__, __LINE__, task->DevHandle);
			err = 1;
		} else {
			mpssas_prepare_for_tm(sc, cm, targ, CAM_LUN_WILDCARD);
			err = mps_wait_command(sc, &cm, 30, CAN_SLEEP);
		}

		if (err != 0) {
			err = EIO;
			mps_dprint(sc, MPS_FAULT, "%s: task management failed",
			    __func__);
		}
		/*
		 * Copy the reply data and sense data to user space.
		 */
		if ((cm != NULL) && (cm->cm_reply != NULL)) {
			rpl = (MPI2_DEFAULT_REPLY *)cm->cm_reply;
			sz = rpl->MsgLength * 4;
	
			if (sz > data->ReplySize) {
				mps_printf(sc, "%s: user reply buffer (%d) "
				    "smaller than returned buffer (%d)\n",
				    __func__, data->ReplySize, sz);
			}
			mps_unlock(sc);
			copyout(cm->cm_reply, PTRIN(data->PtrReply),
			    data->ReplySize);
			mps_lock(sc);
		}
		mpssas_free_tm(sc, cm);
		goto Ret;
	}

	mps_lock(sc);
	cm = mps_alloc_command(sc);

	if (cm == NULL) {
		mps_printf(sc, "%s: no mps requests\n", __func__);
		err = ENOMEM;
		goto Ret;
	}
	mps_unlock(sc);

	hdr = (MPI2_REQUEST_HEADER *)cm->cm_req;
	bcopy(&tmphdr, hdr, data->RequestSize);

	/*
	 * Do some checking to make sure the IOCTL request contains a valid
	 * request.  Then set the SGL info.
	 */
	mpi_init_sge(cm, hdr, (void *)((uint8_t *)hdr + data->RequestSize));

	/*
	 * Set up for read, write or both.  From check above, DataOutSize will
	 * be 0 if direction is READ or WRITE, but it will have some non-zero
	 * value if the direction is BOTH.  So, just use the biggest size to get
	 * the cm_data buffer size.  If direction is BOTH, 2 SGLs need to be set
	 * up; the first is for the request and the second will contain the
	 * response data. cm_out_len needs to be set here and this will be used
	 * when the SGLs are set up.
	 */
	cm->cm_data = NULL;
	cm->cm_length = MAX(data->DataSize, data->DataOutSize);
	cm->cm_out_len = data->DataOutSize;
	cm->cm_flags = 0;
	if (cm->cm_length != 0) {
		cm->cm_data = malloc(cm->cm_length, M_MPSUSER, M_WAITOK |
		    M_ZERO);
		cm->cm_flags = MPS_CM_FLAGS_DATAIN;
		if (data->DataOutSize) {
			cm->cm_flags |= MPS_CM_FLAGS_DATAOUT;
			err = copyin(PTRIN(data->PtrDataOut),
			    cm->cm_data, data->DataOutSize);
		} else if (data->DataDirection ==
		    MPS_PASS_THRU_DIRECTION_WRITE) {
			cm->cm_flags = MPS_CM_FLAGS_DATAOUT;
			err = copyin(PTRIN(data->PtrData),
			    cm->cm_data, data->DataSize);
		}
		if (err != 0)
			mps_dprint(sc, MPS_FAULT, "%s: failed to copy "
			    "IOCTL data from user space\n", __func__);
	}
	cm->cm_flags |= MPS_CM_FLAGS_SGE_SIMPLE;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;

	/*
	 * Set up Sense buffer and SGL offset for IO passthru.  SCSI IO request
	 * uses SCSI IO descriptor.
	 */
	if ((function == MPI2_FUNCTION_SCSI_IO_REQUEST) ||
	    (function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH)) {
		MPI2_SCSI_IO_REQUEST	*scsi_io_req;

		scsi_io_req = (MPI2_SCSI_IO_REQUEST *)hdr;
		/*
		 * Put SGE for data and data_out buffer at the end of
		 * scsi_io_request message header (64 bytes in total).
		 * Following above SGEs, the residual space will be used by
		 * sense data.
		 */
		scsi_io_req->SenseBufferLength = (uint8_t)(data->RequestSize -
		    64);
		scsi_io_req->SenseBufferLowAddress = htole32(cm->cm_sense_busaddr);

		/*
		 * Set SGLOffset0 value.  This is the number of dwords that SGL
		 * is offset from the beginning of MPI2_SCSI_IO_REQUEST struct.
		 */
		scsi_io_req->SGLOffset0 = 24;

		/*
		 * Setup descriptor info.  RAID passthrough must use the
		 * default request descriptor which is already set, so if this
		 * is a SCSI IO request, change the descriptor to SCSI IO.
		 * Also, if this is a SCSI IO request, handle the reply in the
		 * mpssas_scsio_complete function.
		 */
		if (function == MPI2_FUNCTION_SCSI_IO_REQUEST) {
			cm->cm_desc.SCSIIO.RequestFlags =
			    MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
			cm->cm_desc.SCSIIO.DevHandle = scsi_io_req->DevHandle;

			/*
			 * Make sure the DevHandle is not 0 because this is a
			 * likely error.
			 */
			if (scsi_io_req->DevHandle == 0) {
				err = EINVAL;
				goto RetFreeUnlocked;
			}
		}
	}

	mps_lock(sc);

	err = mps_wait_command(sc, &cm, 30, CAN_SLEEP);

	if (err || (cm == NULL)) {
		mps_printf(sc, "%s: invalid request: error %d\n", __func__,
		    err);
		mps_unlock(sc);
		goto RetFreeUnlocked;
	}

	/*
	 * Sync the DMA data, if any.  Then copy the data to user space.
	 */
	if (cm->cm_data != NULL) {
		if (cm->cm_flags & MPS_CM_FLAGS_DATAIN)
			dir = BUS_DMASYNC_POSTREAD;
		else if (cm->cm_flags & MPS_CM_FLAGS_DATAOUT)
			dir = BUS_DMASYNC_POSTWRITE;
		bus_dmamap_sync(sc->buffer_dmat, cm->cm_dmamap, dir);
		bus_dmamap_unload(sc->buffer_dmat, cm->cm_dmamap);

		if (cm->cm_flags & MPS_CM_FLAGS_DATAIN) {
			mps_unlock(sc);
			err = copyout(cm->cm_data,
			    PTRIN(data->PtrData), data->DataSize);
			mps_lock(sc);
			if (err != 0)
				mps_dprint(sc, MPS_FAULT, "%s: failed to copy "
				    "IOCTL data to user space\n", __func__);
		}
	}

	/*
	 * Copy the reply data and sense data to user space.
	 */
	if (cm->cm_reply != NULL) {
		rpl = (MPI2_DEFAULT_REPLY *)cm->cm_reply;
		sz = rpl->MsgLength * 4;

		if (sz > data->ReplySize) {
			mps_printf(sc, "%s: user reply buffer (%d) smaller "
			    "than returned buffer (%d)\n", __func__,
			    data->ReplySize, sz);
		}
		mps_unlock(sc);
		copyout(cm->cm_reply, PTRIN(data->PtrReply), data->ReplySize);
		mps_lock(sc);

		if ((function == MPI2_FUNCTION_SCSI_IO_REQUEST) ||
		    (function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH)) {
			if (((MPI2_SCSI_IO_REPLY *)rpl)->SCSIState &
			    MPI2_SCSI_STATE_AUTOSENSE_VALID) {
				sense_len =
				    MIN((le32toh(((MPI2_SCSI_IO_REPLY *)rpl)->
				    SenseCount)), sizeof(struct
				    scsi_sense_data));
				mps_unlock(sc);
				copyout(cm->cm_sense, (PTRIN(data->PtrReply +
				    sizeof(MPI2_SCSI_IO_REPLY))), sense_len);
				mps_lock(sc);
			}
		}
	}
	mps_unlock(sc);

RetFreeUnlocked:
	mps_lock(sc);

	if (cm != NULL) {
		if (cm->cm_data)
			free(cm->cm_data, M_MPSUSER);
		mps_free_command(sc, cm);
	}
Ret:
	sc->mps_flags &= ~MPS_FLAGS_BUSY;
	mps_unlock(sc);

	return (err);
}

static void
mps_user_get_adapter_data(struct mps_softc *sc, mps_adapter_data_t *data)
{
	Mpi2ConfigReply_t	mpi_reply;
	Mpi2BiosPage3_t		config_page;

	/*
	 * Use the PCI interface functions to get the Bus, Device, and Function
	 * information.
	 */
	data->PciInformation.u.bits.BusNumber = pci_get_bus(sc->mps_dev);
	data->PciInformation.u.bits.DeviceNumber = pci_get_slot(sc->mps_dev);
	data->PciInformation.u.bits.FunctionNumber =
	    pci_get_function(sc->mps_dev);

	/*
	 * Get the FW version that should already be saved in IOC Facts.
	 */
	data->MpiFirmwareVersion = sc->facts->FWVersion.Word;

	/*
	 * General device info.
	 */
	data->AdapterType = MPSIOCTL_ADAPTER_TYPE_SAS2;
	if (sc->mps_flags & MPS_FLAGS_WD_AVAILABLE)
		data->AdapterType = MPSIOCTL_ADAPTER_TYPE_SAS2_SSS6200;
	data->PCIDeviceHwId = pci_get_device(sc->mps_dev);
	data->PCIDeviceHwRev = pci_read_config(sc->mps_dev, PCIR_REVID, 1);
	data->SubSystemId = pci_get_subdevice(sc->mps_dev);
	data->SubsystemVendorId = pci_get_subvendor(sc->mps_dev);

	/*
	 * Get the driver version.
	 */
	strcpy((char *)&data->DriverVersion[0], MPS_DRIVER_VERSION);

	/*
	 * Need to get BIOS Config Page 3 for the BIOS Version.
	 */
	data->BiosVersion = 0;
	mps_lock(sc);
	if (mps_config_get_bios_pg3(sc, &mpi_reply, &config_page))
		printf("%s: Error while retrieving BIOS Version\n", __func__);
	else
		data->BiosVersion = config_page.BiosVersion;
	mps_unlock(sc);
}

static void
mps_user_read_pci_info(struct mps_softc *sc, mps_pci_info_t *data)
{
	int	i;

	/*
	 * Use the PCI interface functions to get the Bus, Device, and Function
	 * information.
	 */
	data->BusNumber = pci_get_bus(sc->mps_dev);
	data->DeviceNumber = pci_get_slot(sc->mps_dev);
	data->FunctionNumber = pci_get_function(sc->mps_dev);

	/*
	 * Now get the interrupt vector and the pci header.  The vector can
	 * only be 0 right now.  The header is the first 256 bytes of config
	 * space.
	 */
	data->InterruptVector = 0;
	for (i = 0; i < sizeof (data->PciHeader); i++) {
		data->PciHeader[i] = pci_read_config(sc->mps_dev, i, 1);
	}
}

static uint8_t
mps_get_fw_diag_buffer_number(struct mps_softc *sc, uint32_t unique_id)
{
	uint8_t	index;

	for (index = 0; index < MPI2_DIAG_BUF_TYPE_COUNT; index++) {
		if (sc->fw_diag_buffer_list[index].unique_id == unique_id) {
			return (index);
		}
	}

	return (MPS_FW_DIAGNOSTIC_UID_NOT_FOUND);
}

static int
mps_post_fw_diag_buffer(struct mps_softc *sc,
    mps_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code)
{
	MPI2_DIAG_BUFFER_POST_REQUEST	*req;
	MPI2_DIAG_BUFFER_POST_REPLY	*reply = NULL;
	struct mps_command		*cm = NULL;
	int				i, status;

	/*
	 * If buffer is not enabled, just leave.
	 */
	*return_code = MPS_FW_DIAG_ERROR_POST_FAILED;
	if (!pBuffer->enabled) {
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * Clear some flags initially.
	 */
	pBuffer->force_release = FALSE;
	pBuffer->valid_data = FALSE;
	pBuffer->owned_by_firmware = FALSE;

	/*
	 * Get a command.
	 */
	cm = mps_alloc_command(sc);
	if (cm == NULL) {
		mps_printf(sc, "%s: no mps requests\n", __func__);
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * Build the request for releasing the FW Diag Buffer and send it.
	 */
	req = (MPI2_DIAG_BUFFER_POST_REQUEST *)cm->cm_req;
	req->Function = MPI2_FUNCTION_DIAG_BUFFER_POST;
	req->BufferType = pBuffer->buffer_type;
	req->ExtendedType = pBuffer->extended_type;
	req->BufferLength = pBuffer->size;
	for (i = 0; i < (sizeof(req->ProductSpecific) / 4); i++)
		req->ProductSpecific[i] = pBuffer->product_specific[i];
	mps_from_u64(sc->fw_diag_busaddr, &req->BufferAddress);
	cm->cm_data = NULL;
	cm->cm_length = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_complete_data = NULL;

	/*
	 * Send command synchronously.
	 */
	status = mps_wait_command(sc, &cm, 30, CAN_SLEEP);
	if (status || (cm == NULL)) {
		mps_printf(sc, "%s: invalid request: error %d\n", __func__,
		    status);
		status = MPS_DIAG_FAILURE;
		goto done;
	}

	/*
	 * Process POST reply.
	 */
	reply = (MPI2_DIAG_BUFFER_POST_REPLY *)cm->cm_reply;
	if (reply == NULL) {
		mps_printf(sc, "%s: reply is NULL, probably due to "
		    "reinitialization\n", __func__);
		status = MPS_DIAG_FAILURE;
		goto done;
	}
	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) {
		status = MPS_DIAG_FAILURE;
		mps_dprint(sc, MPS_FAULT, "%s: post of FW  Diag Buffer failed "
		    "with IOCStatus = 0x%x, IOCLogInfo = 0x%x and "
		    "TransferLength = 0x%x\n", __func__,
		    le16toh(reply->IOCStatus), le32toh(reply->IOCLogInfo),
		    le32toh(reply->TransferLength));
		goto done;
	}

	/*
	 * Post was successful.
	 */
	pBuffer->valid_data = TRUE;
	pBuffer->owned_by_firmware = TRUE;
	*return_code = MPS_FW_DIAG_ERROR_SUCCESS;
	status = MPS_DIAG_SUCCESS;

done:
	if (cm != NULL)
		mps_free_command(sc, cm);
	return (status);
}

static int
mps_release_fw_diag_buffer(struct mps_softc *sc,
    mps_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code,
    uint32_t diag_type)
{
	MPI2_DIAG_RELEASE_REQUEST	*req;
	MPI2_DIAG_RELEASE_REPLY		*reply = NULL;
	struct mps_command		*cm = NULL;
	int				status;

	/*
	 * If buffer is not enabled, just leave.
	 */
	*return_code = MPS_FW_DIAG_ERROR_RELEASE_FAILED;
	if (!pBuffer->enabled) {
		mps_dprint(sc, MPS_USER, "%s: This buffer type is not "
		    "supported by the IOC", __func__);
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * Clear some flags initially.
	 */
	pBuffer->force_release = FALSE;
	pBuffer->valid_data = FALSE;
	pBuffer->owned_by_firmware = FALSE;

	/*
	 * Get a command.
	 */
	cm = mps_alloc_command(sc);
	if (cm == NULL) {
		mps_printf(sc, "%s: no mps requests\n", __func__);
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * Build the request for releasing the FW Diag Buffer and send it.
	 */
	req = (MPI2_DIAG_RELEASE_REQUEST *)cm->cm_req;
	req->Function = MPI2_FUNCTION_DIAG_RELEASE;
	req->BufferType = pBuffer->buffer_type;
	cm->cm_data = NULL;
	cm->cm_length = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_complete_data = NULL;

	/*
	 * Send command synchronously.
	 */
	status = mps_wait_command(sc, &cm, 30, CAN_SLEEP);
	if (status || (cm == NULL)) {
		mps_printf(sc, "%s: invalid request: error %d\n", __func__,
		    status);
		status = MPS_DIAG_FAILURE;
		goto done;
	}

	/*
	 * Process RELEASE reply.
	 */
	reply = (MPI2_DIAG_RELEASE_REPLY *)cm->cm_reply;
	if (reply == NULL) {
		mps_printf(sc, "%s: reply is NULL, probably due to "
		    "reinitialization\n", __func__);
		status = MPS_DIAG_FAILURE;
		goto done;
	}
	if (((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) || pBuffer->owned_by_firmware) {
		status = MPS_DIAG_FAILURE;
		mps_dprint(sc, MPS_FAULT, "%s: release of FW Diag Buffer "
		    "failed with IOCStatus = 0x%x and IOCLogInfo = 0x%x\n",
		    __func__, le16toh(reply->IOCStatus),
		    le32toh(reply->IOCLogInfo));
		goto done;
	}

	/*
	 * Release was successful.
	 */
	*return_code = MPS_FW_DIAG_ERROR_SUCCESS;
	status = MPS_DIAG_SUCCESS;

	/*
	 * If this was for an UNREGISTER diag type command, clear the unique ID.
	 */
	if (diag_type == MPS_FW_DIAG_TYPE_UNREGISTER) {
		pBuffer->unique_id = MPS_FW_DIAG_INVALID_UID;
	}

done:
	if (cm != NULL)
		mps_free_command(sc, cm);

	return (status);
}

static int
mps_diag_register(struct mps_softc *sc, mps_fw_diag_register_t *diag_register,
    uint32_t *return_code)
{
	mps_fw_diagnostic_buffer_t	*pBuffer;
	struct mps_busdma_context	*ctx;
	uint8_t				extended_type, buffer_type, i;
	uint32_t			buffer_size;
	uint32_t			unique_id;
	int				status;
	int				error;

	extended_type = diag_register->ExtendedType;
	buffer_type = diag_register->BufferType;
	buffer_size = diag_register->RequestedBufferSize;
	unique_id = diag_register->UniqueId;
	ctx = NULL;
	error = 0;

	/*
	 * Check for valid buffer type
	 */
	if (buffer_type >= MPI2_DIAG_BUF_TYPE_COUNT) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should not be found.  If it is, the ID is already in use.
	 */
	i = mps_get_fw_diag_buffer_number(sc, unique_id);
	pBuffer = &sc->fw_diag_buffer_list[buffer_type];
	if (i != MPS_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * The buffer's unique ID should not be registered yet, and the given
	 * unique ID cannot be 0.
	 */
	if ((pBuffer->unique_id != MPS_FW_DIAG_INVALID_UID) ||
	    (unique_id == MPS_FW_DIAG_INVALID_UID)) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * If this buffer is already posted as immediate, just change owner.
	 */
	if (pBuffer->immediate && pBuffer->owned_by_firmware &&
	    (pBuffer->unique_id == MPS_FW_DIAG_INVALID_UID)) {
		pBuffer->immediate = FALSE;
		pBuffer->unique_id = unique_id;
		return (MPS_DIAG_SUCCESS);
	}

	/*
	 * Post a new buffer after checking if it's enabled.  The DMA buffer
	 * that is allocated will be contiguous (nsegments = 1).
	 */
	if (!pBuffer->enabled) {
		*return_code = MPS_FW_DIAG_ERROR_NO_BUFFER;
		return (MPS_DIAG_FAILURE);
	}
	if (bus_dma_tag_create( sc->mps_parent_dmat,    /* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
                                buffer_size,		/* maxsize */
                                1,			/* nsegments */
                                buffer_size,		/* maxsegsize */
                                0,			/* flags */
                                NULL, NULL,		/* lockfunc, lockarg */
                                &sc->fw_diag_dmat)) {
		mps_dprint(sc, MPS_ERROR,
		    "Cannot allocate FW diag buffer DMA tag\n");
		*return_code = MPS_FW_DIAG_ERROR_NO_BUFFER;
		status = MPS_DIAG_FAILURE;
		goto bailout;
	}
	if (bus_dmamem_alloc(sc->fw_diag_dmat, (void **)&sc->fw_diag_buffer,
	    BUS_DMA_NOWAIT, &sc->fw_diag_map)) {
		mps_dprint(sc, MPS_ERROR,
		    "Cannot allocate FW diag buffer memory\n");
		*return_code = MPS_FW_DIAG_ERROR_NO_BUFFER;
		status = MPS_DIAG_FAILURE;
		goto bailout;
        }
        bzero(sc->fw_diag_buffer, buffer_size);

	ctx = malloc(sizeof(*ctx), M_MPSUSER, M_WAITOK | M_ZERO);
	if (ctx == NULL) {
		device_printf(sc->mps_dev, "%s: context malloc failed\n",
		    __func__);
		*return_code = MPS_FW_DIAG_ERROR_NO_BUFFER;
		status = MPS_DIAG_FAILURE;
		goto bailout;
	}
	ctx->addr = &sc->fw_diag_busaddr;
	ctx->buffer_dmat = sc->fw_diag_dmat;
	ctx->buffer_dmamap = sc->fw_diag_map;
	ctx->softc = sc;
        error = bus_dmamap_load(sc->fw_diag_dmat, sc->fw_diag_map,
	    sc->fw_diag_buffer, buffer_size, mps_memaddr_wait_cb,
	    ctx, 0);

	if (error == EINPROGRESS) {

		/* XXX KDM */
		device_printf(sc->mps_dev, "%s: Deferred bus_dmamap_load\n",
		    __func__);
		/*
		 * Wait for the load to complete.  If we're interrupted,
		 * bail out.
		 */
		mps_lock(sc);
		if (ctx->completed == 0) {
			error = msleep(ctx, &sc->mps_mtx, PCATCH, "mpswait", 0);
			if (error != 0) {
				/*
				 * We got an error from msleep(9).  This is
				 * most likely due to a signal.  Tell
				 * mpr_memaddr_wait_cb() that we've abandoned
				 * the context, so it needs to clean up when
				 * it is called.
				 */
				ctx->abandoned = 1;

				/* The callback will free this memory */
				ctx = NULL;
				mps_unlock(sc);

				device_printf(sc->mps_dev, "Cannot "
				    "bus_dmamap_load FW diag buffer, error = "
				    "%d returned from msleep\n", error);
				*return_code = MPS_FW_DIAG_ERROR_NO_BUFFER;
				status = MPS_DIAG_FAILURE;
				goto bailout;
			}
		}
		mps_unlock(sc);
	} 

	if ((error != 0) || (ctx->error != 0)) {
		device_printf(sc->mps_dev, "Cannot bus_dmamap_load FW diag "
		    "buffer, %serror = %d\n", error ? "" : "callback ",
		    error ? error : ctx->error);
		*return_code = MPS_FW_DIAG_ERROR_NO_BUFFER;
		status = MPS_DIAG_FAILURE;
		goto bailout;
	}

	bus_dmamap_sync(sc->fw_diag_dmat, sc->fw_diag_map, BUS_DMASYNC_PREREAD);

	pBuffer->size = buffer_size;

	/*
	 * Copy the given info to the diag buffer and post the buffer.
	 */
	pBuffer->buffer_type = buffer_type;
	pBuffer->immediate = FALSE;
	if (buffer_type == MPI2_DIAG_BUF_TYPE_TRACE) {
		for (i = 0; i < (sizeof (pBuffer->product_specific) / 4);
		    i++) {
			pBuffer->product_specific[i] =
			    diag_register->ProductSpecific[i];
		}
	}
	pBuffer->extended_type = extended_type;
	pBuffer->unique_id = unique_id;
	status = mps_post_fw_diag_buffer(sc, pBuffer, return_code);

bailout:
	/*
	 * In case there was a failure, free the DMA buffer.
	 */
	if (status == MPS_DIAG_FAILURE) {
		if (sc->fw_diag_busaddr != 0) {
			bus_dmamap_unload(sc->fw_diag_dmat, sc->fw_diag_map);
			sc->fw_diag_busaddr = 0;
		}
		if (sc->fw_diag_buffer != NULL) {
			bus_dmamem_free(sc->fw_diag_dmat, sc->fw_diag_buffer,
			    sc->fw_diag_map);
			sc->fw_diag_buffer = NULL;
		}
		if (sc->fw_diag_dmat != NULL) {
			bus_dma_tag_destroy(sc->fw_diag_dmat);
			sc->fw_diag_dmat = NULL;
		}
	}

	if (ctx != NULL)
		free(ctx, M_MPSUSER);

	return (status);
}

static int
mps_diag_unregister(struct mps_softc *sc,
    mps_fw_diag_unregister_t *diag_unregister, uint32_t *return_code)
{
	mps_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i;
	uint32_t			unique_id;
	int				status;

	unique_id = diag_unregister->UniqueId;

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should be there.
	 */
	i = mps_get_fw_diag_buffer_number(sc, unique_id);
	if (i == MPS_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
		return (MPS_DIAG_FAILURE);
	}

	pBuffer = &sc->fw_diag_buffer_list[i];

	/*
	 * Try to release the buffer from FW before freeing it.  If release
	 * fails, don't free the DMA buffer in case FW tries to access it
	 * later.  If buffer is not owned by firmware, can't release it.
	 */
	if (!pBuffer->owned_by_firmware) {
		status = MPS_DIAG_SUCCESS;
	} else {
		status = mps_release_fw_diag_buffer(sc, pBuffer, return_code,
		    MPS_FW_DIAG_TYPE_UNREGISTER);
	}

	/*
	 * At this point, return the current status no matter what happens with
	 * the DMA buffer.
	 */
	pBuffer->unique_id = MPS_FW_DIAG_INVALID_UID;
	if (status == MPS_DIAG_SUCCESS) {
		if (sc->fw_diag_busaddr != 0) {
			bus_dmamap_unload(sc->fw_diag_dmat, sc->fw_diag_map);
			sc->fw_diag_busaddr = 0;
		}
		if (sc->fw_diag_buffer != NULL) {
			bus_dmamem_free(sc->fw_diag_dmat, sc->fw_diag_buffer,
			    sc->fw_diag_map);
			sc->fw_diag_buffer = NULL;
		}
		if (sc->fw_diag_dmat != NULL) {
			bus_dma_tag_destroy(sc->fw_diag_dmat);
			sc->fw_diag_dmat = NULL;
		}
	}

	return (status);
}

static int
mps_diag_query(struct mps_softc *sc, mps_fw_diag_query_t *diag_query,
    uint32_t *return_code)
{
	mps_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i;
	uint32_t			unique_id;

	unique_id = diag_query->UniqueId;

	/*
	 * If ID is valid, query on ID.
	 * If ID is invalid, query on buffer type.
	 */
	if (unique_id == MPS_FW_DIAG_INVALID_UID) {
		i = diag_query->BufferType;
		if (i >= MPI2_DIAG_BUF_TYPE_COUNT) {
			*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
			return (MPS_DIAG_FAILURE);
		}
	} else {
		i = mps_get_fw_diag_buffer_number(sc, unique_id);
		if (i == MPS_FW_DIAGNOSTIC_UID_NOT_FOUND) {
			*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
			return (MPS_DIAG_FAILURE);
		}
	}

	/*
	 * Fill query structure with the diag buffer info.
	 */
	pBuffer = &sc->fw_diag_buffer_list[i];
	diag_query->BufferType = pBuffer->buffer_type;
	diag_query->ExtendedType = pBuffer->extended_type;
	if (diag_query->BufferType == MPI2_DIAG_BUF_TYPE_TRACE) {
		for (i = 0; i < (sizeof(diag_query->ProductSpecific) / 4);
		    i++) {
			diag_query->ProductSpecific[i] =
			    pBuffer->product_specific[i];
		}
	}
	diag_query->TotalBufferSize = pBuffer->size;
	diag_query->DriverAddedBufferSize = 0;
	diag_query->UniqueId = pBuffer->unique_id;
	diag_query->ApplicationFlags = 0;
	diag_query->DiagnosticFlags = 0;

	/*
	 * Set/Clear application flags
	 */
	if (pBuffer->immediate) {
		diag_query->ApplicationFlags &= ~MPS_FW_DIAG_FLAG_APP_OWNED;
	} else {
		diag_query->ApplicationFlags |= MPS_FW_DIAG_FLAG_APP_OWNED;
	}
	if (pBuffer->valid_data || pBuffer->owned_by_firmware) {
		diag_query->ApplicationFlags |= MPS_FW_DIAG_FLAG_BUFFER_VALID;
	} else {
		diag_query->ApplicationFlags &= ~MPS_FW_DIAG_FLAG_BUFFER_VALID;
	}
	if (pBuffer->owned_by_firmware) {
		diag_query->ApplicationFlags |=
		    MPS_FW_DIAG_FLAG_FW_BUFFER_ACCESS;
	} else {
		diag_query->ApplicationFlags &=
		    ~MPS_FW_DIAG_FLAG_FW_BUFFER_ACCESS;
	}

	return (MPS_DIAG_SUCCESS);
}

static int
mps_diag_read_buffer(struct mps_softc *sc,
    mps_diag_read_buffer_t *diag_read_buffer, uint8_t *ioctl_buf,
    uint32_t *return_code)
{
	mps_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i, *pData;
	uint32_t			unique_id;
	int				status;

	unique_id = diag_read_buffer->UniqueId;

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should be there.
	 */
	i = mps_get_fw_diag_buffer_number(sc, unique_id);
	if (i == MPS_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
		return (MPS_DIAG_FAILURE);
	}

	pBuffer = &sc->fw_diag_buffer_list[i];

	/*
	 * Make sure requested read is within limits
	 */
	if (diag_read_buffer->StartingOffset + diag_read_buffer->BytesToRead >
	    pBuffer->size) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
		return (MPS_DIAG_FAILURE);
	}

	/* Sync the DMA map before we copy to userland. */
	bus_dmamap_sync(sc->fw_diag_dmat, sc->fw_diag_map,
	    BUS_DMASYNC_POSTREAD);

	/*
	 * Copy the requested data from DMA to the diag_read_buffer.  The DMA
	 * buffer that was allocated is one contiguous buffer.
	 */
	pData = (uint8_t *)(sc->fw_diag_buffer +
	    diag_read_buffer->StartingOffset);
	if (copyout(pData, ioctl_buf, diag_read_buffer->BytesToRead) != 0)
		return (MPS_DIAG_FAILURE);
	diag_read_buffer->Status = 0;

	/*
	 * Set or clear the Force Release flag.
	 */
	if (pBuffer->force_release) {
		diag_read_buffer->Flags |= MPS_FW_DIAG_FLAG_FORCE_RELEASE;
	} else {
		diag_read_buffer->Flags &= ~MPS_FW_DIAG_FLAG_FORCE_RELEASE;
	}

	/*
	 * If buffer is to be reregistered, make sure it's not already owned by
	 * firmware first.
	 */
	status = MPS_DIAG_SUCCESS;
	if (!pBuffer->owned_by_firmware) {
		if (diag_read_buffer->Flags & MPS_FW_DIAG_FLAG_REREGISTER) {
			status = mps_post_fw_diag_buffer(sc, pBuffer,
			    return_code);
		}
	}

	return (status);
}

static int
mps_diag_release(struct mps_softc *sc, mps_fw_diag_release_t *diag_release,
    uint32_t *return_code)
{
	mps_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i;
	uint32_t			unique_id;
	int				status;

	unique_id = diag_release->UniqueId;

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should be there.
	 */
	i = mps_get_fw_diag_buffer_number(sc, unique_id);
	if (i == MPS_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPS_FW_DIAG_ERROR_INVALID_UID;
		return (MPS_DIAG_FAILURE);
	}

	pBuffer = &sc->fw_diag_buffer_list[i];

	/*
	 * If buffer is not owned by firmware, it's already been released.
	 */
	if (!pBuffer->owned_by_firmware) {
		*return_code = MPS_FW_DIAG_ERROR_ALREADY_RELEASED;
		return (MPS_DIAG_FAILURE);
	}

	/*
	 * Release the buffer.
	 */
	status = mps_release_fw_diag_buffer(sc, pBuffer, return_code,
	    MPS_FW_DIAG_TYPE_RELEASE);
	return (status);
}

static int
mps_do_diag_action(struct mps_softc *sc, uint32_t action, uint8_t *diag_action,
    uint32_t length, uint32_t *return_code)
{
	mps_fw_diag_register_t		diag_register;
	mps_fw_diag_unregister_t	diag_unregister;
	mps_fw_diag_query_t		diag_query;
	mps_diag_read_buffer_t		diag_read_buffer;
	mps_fw_diag_release_t		diag_release;
	int				status = MPS_DIAG_SUCCESS;
	uint32_t			original_return_code;

	original_return_code = *return_code;
	*return_code = MPS_FW_DIAG_ERROR_SUCCESS;

	switch (action) {
		case MPS_FW_DIAG_TYPE_REGISTER:
			if (!length) {
				*return_code =
				    MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPS_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_register,
			    sizeof(diag_register)) != 0)
				return (MPS_DIAG_FAILURE);
			status = mps_diag_register(sc, &diag_register,
			    return_code);
			break;

		case MPS_FW_DIAG_TYPE_UNREGISTER:
			if (length < sizeof(diag_unregister)) {
				*return_code =
				    MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPS_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_unregister,
			    sizeof(diag_unregister)) != 0)
				return (MPS_DIAG_FAILURE);
			status = mps_diag_unregister(sc, &diag_unregister,
			    return_code);
			break;

		case MPS_FW_DIAG_TYPE_QUERY:
			if (length < sizeof (diag_query)) {
				*return_code =
				    MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPS_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_query, sizeof(diag_query))
			    != 0)
				return (MPS_DIAG_FAILURE);
			status = mps_diag_query(sc, &diag_query, return_code);
			if (status == MPS_DIAG_SUCCESS)
				if (copyout(&diag_query, diag_action,
				    sizeof (diag_query)) != 0)
					return (MPS_DIAG_FAILURE);
			break;

		case MPS_FW_DIAG_TYPE_READ_BUFFER:
			if (copyin(diag_action, &diag_read_buffer,
			    sizeof(diag_read_buffer)) != 0)
				return (MPS_DIAG_FAILURE);
			if (length < diag_read_buffer.BytesToRead) {
				*return_code =
				    MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPS_DIAG_FAILURE;
				break;
			}
			status = mps_diag_read_buffer(sc, &diag_read_buffer,
			    PTRIN(diag_read_buffer.PtrDataBuffer),
			    return_code);
			if (status == MPS_DIAG_SUCCESS) {
				if (copyout(&diag_read_buffer, diag_action,
				    sizeof(diag_read_buffer) -
				    sizeof(diag_read_buffer.PtrDataBuffer)) !=
				    0)
					return (MPS_DIAG_FAILURE);
			}
			break;

		case MPS_FW_DIAG_TYPE_RELEASE:
			if (length < sizeof(diag_release)) {
				*return_code =
				    MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPS_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_release,
			    sizeof(diag_release)) != 0)
				return (MPS_DIAG_FAILURE);
			status = mps_diag_release(sc, &diag_release,
			    return_code);
			break;

		default:
			*return_code = MPS_FW_DIAG_ERROR_INVALID_PARAMETER;
			status = MPS_DIAG_FAILURE;
			break;
	}

	if ((status == MPS_DIAG_FAILURE) &&
	    (original_return_code == MPS_FW_DIAG_NEW) &&
	    (*return_code != MPS_FW_DIAG_ERROR_SUCCESS))
		status = MPS_DIAG_SUCCESS;

	return (status);
}

static int
mps_user_diag_action(struct mps_softc *sc, mps_diag_action_t *data)
{
	int			status;

	/*
	 * Only allow one diag action at one time.
	 */
	if (sc->mps_flags & MPS_FLAGS_BUSY) {
		mps_dprint(sc, MPS_USER, "%s: Only one FW diag command "
		    "allowed at a single time.", __func__);
		return (EBUSY);
	}
	sc->mps_flags |= MPS_FLAGS_BUSY;

	/*
	 * Send diag action request
	 */
	if (data->Action == MPS_FW_DIAG_TYPE_REGISTER ||
	    data->Action == MPS_FW_DIAG_TYPE_UNREGISTER ||
	    data->Action == MPS_FW_DIAG_TYPE_QUERY ||
	    data->Action == MPS_FW_DIAG_TYPE_READ_BUFFER ||
	    data->Action == MPS_FW_DIAG_TYPE_RELEASE) {
		status = mps_do_diag_action(sc, data->Action,
		    PTRIN(data->PtrDiagAction), data->Length,
		    &data->ReturnCode);
	} else
		status = EINVAL;

	sc->mps_flags &= ~MPS_FLAGS_BUSY;
	return (status);
}

/*
 * Copy the event recording mask and the event queue size out.  For
 * clarification, the event recording mask (events_to_record) is not the same
 * thing as the event mask (event_mask).  events_to_record has a bit set for
 * every event type that is to be recorded by the driver, and event_mask has a
 * bit cleared for every event that is allowed into the driver from the IOC.
 * They really have nothing to do with each other.
 */
static void
mps_user_event_query(struct mps_softc *sc, mps_event_query_t *data)
{
	uint8_t	i;

	mps_lock(sc);
	data->Entries = MPS_EVENT_QUEUE_SIZE;

	for (i = 0; i < 4; i++) {
		data->Types[i] = sc->events_to_record[i];
	}
	mps_unlock(sc);
}

/*
 * Set the driver's event mask according to what's been given.  See
 * mps_user_event_query for explanation of the event recording mask and the IOC
 * event mask.  It's the app's responsibility to enable event logging by setting
 * the bits in events_to_record.  Initially, no events will be logged.
 */
static void
mps_user_event_enable(struct mps_softc *sc, mps_event_enable_t *data)
{
	uint8_t	i;

	mps_lock(sc);
	for (i = 0; i < 4; i++) {
		sc->events_to_record[i] = data->Types[i];
	}
	mps_unlock(sc);
}

/*
 * Copy out the events that have been recorded, up to the max events allowed.
 */
static int
mps_user_event_report(struct mps_softc *sc, mps_event_report_t *data)
{
	int		status = 0;
	uint32_t	size;

	mps_lock(sc);
	size = data->Size;
	if ((size >= sizeof(sc->recorded_events)) && (status == 0)) {
		mps_unlock(sc);
		if (copyout((void *)sc->recorded_events,
		    PTRIN(data->PtrEvents), size) != 0)
			status = EFAULT;
		mps_lock(sc);
	} else {
		/*
		 * data->Size value is not large enough to copy event data.
		 */
		status = EFAULT;
	}

	/*
	 * Change size value to match the number of bytes that were copied.
	 */
	if (status == 0)
		data->Size = sizeof(sc->recorded_events);
	mps_unlock(sc);

	return (status);
}

/*
 * Record events into the driver from the IOC if they are not masked.
 */
void
mpssas_record_event(struct mps_softc *sc,
    MPI2_EVENT_NOTIFICATION_REPLY *event_reply)
{
	uint32_t	event;
	int		i, j;
	uint16_t	event_data_len;
	boolean_t	sendAEN = FALSE;

	event = event_reply->Event;

	/*
	 * Generate a system event to let anyone who cares know that a
	 * LOG_ENTRY_ADDED event has occurred.  This is sent no matter what the
	 * event mask is set to.
	 */
	if (event == MPI2_EVENT_LOG_ENTRY_ADDED) {
		sendAEN = TRUE;
	}

	/*
	 * Record the event only if its corresponding bit is set in
	 * events_to_record.  event_index is the index into recorded_events and
	 * event_number is the overall number of an event being recorded since
	 * start-of-day.  event_index will roll over; event_number will never
	 * roll over.
	 */
	i = (uint8_t)(event / 32);
	j = (uint8_t)(event % 32);
	if ((i < 4) && ((1 << j) & sc->events_to_record[i])) {
		i = sc->event_index;
		sc->recorded_events[i].Type = event;
		sc->recorded_events[i].Number = ++sc->event_number;
		bzero(sc->recorded_events[i].Data, MPS_MAX_EVENT_DATA_LENGTH *
		    4);
		event_data_len = event_reply->EventDataLength;

		if (event_data_len > 0) {
			/*
			 * Limit data to size in m_event entry
			 */
			if (event_data_len > MPS_MAX_EVENT_DATA_LENGTH) {
				event_data_len = MPS_MAX_EVENT_DATA_LENGTH;
			}
			for (j = 0; j < event_data_len; j++) {
				sc->recorded_events[i].Data[j] =
				    event_reply->EventData[j];
			}

			/*
			 * check for index wrap-around
			 */
			if (++i == MPS_EVENT_QUEUE_SIZE) {
				i = 0;
			}
			sc->event_index = (uint8_t)i;

			/*
			 * Set flag to send the event.
			 */
			sendAEN = TRUE;
		}
	}

	/*
	 * Generate a system event if flag is set to let anyone who cares know
	 * that an event has occurred.
	 */
	if (sendAEN) {
//SLM-how to send a system event (see kqueue, kevent)
//		(void) ddi_log_sysevent(mpt->m_dip, DDI_VENDOR_LSI, "MPT_SAS",
//		    "SAS", NULL, NULL, DDI_NOSLEEP);
	}
}

static int
mps_user_reg_access(struct mps_softc *sc, mps_reg_access_t *data)
{
	int	status = 0;

	switch (data->Command) {
		/*
		 * IO access is not supported.
		 */
		case REG_IO_READ:
		case REG_IO_WRITE:
			mps_dprint(sc, MPS_USER, "IO access is not supported. "
			    "Use memory access.");
			status = EINVAL;
			break;

		case REG_MEM_READ:
			data->RegData = mps_regread(sc, data->RegOffset);
			break;

		case REG_MEM_WRITE:
			mps_regwrite(sc, data->RegOffset, data->RegData);
			break;

		default:
			status = EINVAL;
			break;
	}

	return (status);
}

static int
mps_user_btdh(struct mps_softc *sc, mps_btdh_mapping_t *data)
{
	uint8_t		bt2dh = FALSE;
	uint8_t		dh2bt = FALSE;
	uint16_t	dev_handle, bus, target;

	bus = data->Bus;
	target = data->TargetID;
	dev_handle = data->DevHandle;

	/*
	 * When DevHandle is 0xFFFF and Bus/Target are not 0xFFFF, use Bus/
	 * Target to get DevHandle.  When Bus/Target are 0xFFFF and DevHandle is
	 * not 0xFFFF, use DevHandle to get Bus/Target.  Anything else is
	 * invalid.
	 */
	if ((bus == 0xFFFF) && (target == 0xFFFF) && (dev_handle != 0xFFFF))
		dh2bt = TRUE;
	if ((dev_handle == 0xFFFF) && (bus != 0xFFFF) && (target != 0xFFFF))
		bt2dh = TRUE;
	if (!dh2bt && !bt2dh)
		return (EINVAL);

	/*
	 * Only handle bus of 0.  Make sure target is within range.
	 */
	if (bt2dh) {
		if (bus != 0)
			return (EINVAL);

		if (target > sc->max_devices) {
			mps_dprint(sc, MPS_FAULT, "Target ID is out of range "
			   "for Bus/Target to DevHandle mapping.");
			return (EINVAL);
		}
		dev_handle = sc->mapping_table[target].dev_handle;
		if (dev_handle)
			data->DevHandle = dev_handle;
	} else {
		bus = 0;
		target = mps_mapping_get_tid_from_handle(sc, dev_handle);
		data->Bus = bus;
		data->TargetID = target;
	}

	return (0);
}

static int
mps_ioctl(struct cdev *dev, u_long cmd, void *arg, int flag,
    struct thread *td)
{
	struct mps_softc *sc;
	struct mps_cfg_page_req *page_req;
	struct mps_ext_cfg_page_req *ext_page_req;
	void *mps_page;
	int error, msleep_ret;

	mps_page = NULL;
	sc = dev->si_drv1;
	page_req = (void *)arg;
	ext_page_req = (void *)arg;

	switch (cmd) {
	case MPSIO_READ_CFG_HEADER:
		mps_lock(sc);
		error = mps_user_read_cfg_header(sc, page_req);
		mps_unlock(sc);
		break;
	case MPSIO_READ_CFG_PAGE:
		mps_page = malloc(page_req->len, M_MPSUSER, M_WAITOK | M_ZERO);
		error = copyin(page_req->buf, mps_page,
		    sizeof(MPI2_CONFIG_PAGE_HEADER));
		if (error)
			break;
		mps_lock(sc);
		error = mps_user_read_cfg_page(sc, page_req, mps_page);
		mps_unlock(sc);
		if (error)
			break;
		error = copyout(mps_page, page_req->buf, page_req->len);
		break;
	case MPSIO_READ_EXT_CFG_HEADER:
		mps_lock(sc);
		error = mps_user_read_extcfg_header(sc, ext_page_req);
		mps_unlock(sc);
		break;
	case MPSIO_READ_EXT_CFG_PAGE:
		mps_page = malloc(ext_page_req->len, M_MPSUSER, M_WAITOK|M_ZERO);
		error = copyin(ext_page_req->buf, mps_page,
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		if (error)
			break;
		mps_lock(sc);
		error = mps_user_read_extcfg_page(sc, ext_page_req, mps_page);
		mps_unlock(sc);
		if (error)
			break;
		error = copyout(mps_page, ext_page_req->buf, ext_page_req->len);
		break;
	case MPSIO_WRITE_CFG_PAGE:
		mps_page = malloc(page_req->len, M_MPSUSER, M_WAITOK|M_ZERO);
		error = copyin(page_req->buf, mps_page, page_req->len);
		if (error)
			break;
		mps_lock(sc);
		error = mps_user_write_cfg_page(sc, page_req, mps_page);
		mps_unlock(sc);
		break;
	case MPSIO_MPS_COMMAND:
		error = mps_user_command(sc, (struct mps_usr_command *)arg);
		break;
	case MPTIOCTL_PASS_THRU:
		/*
		 * The user has requested to pass through a command to be
		 * executed by the MPT firmware.  Call our routine which does
		 * this.  Only allow one passthru IOCTL at one time.
		 */
		error = mps_user_pass_thru(sc, (mps_pass_thru_t *)arg);
		break;
	case MPTIOCTL_GET_ADAPTER_DATA:
		/*
		 * The user has requested to read adapter data.  Call our
		 * routine which does this.
		 */
		error = 0;
		mps_user_get_adapter_data(sc, (mps_adapter_data_t *)arg);
		break;
	case MPTIOCTL_GET_PCI_INFO:
		/*
		 * The user has requested to read pci info.  Call
		 * our routine which does this.
		 */
		mps_lock(sc);
		error = 0;
		mps_user_read_pci_info(sc, (mps_pci_info_t *)arg);
		mps_unlock(sc);
		break;
	case MPTIOCTL_RESET_ADAPTER:
		mps_lock(sc);
		sc->port_enable_complete = 0;
		uint32_t reinit_start = time_uptime;
		error = mps_reinit(sc);
		/* Sleep for 300 second. */
		msleep_ret = msleep(&sc->port_enable_complete, &sc->mps_mtx, PRIBIO,
		       "mps_porten", 300 * hz);
		mps_unlock(sc);
		if (msleep_ret)
			printf("Port Enable did not complete after Diag "
			    "Reset msleep error %d.\n", msleep_ret);
		else
			mps_dprint(sc, MPS_USER,
				"Hard Reset with Port Enable completed in %d seconds.\n",
				 (uint32_t) (time_uptime - reinit_start));
		break;
	case MPTIOCTL_DIAG_ACTION:
		/*
		 * The user has done a diag buffer action.  Call our routine
		 * which does this.  Only allow one diag action at one time.
		 */
		mps_lock(sc);
		error = mps_user_diag_action(sc, (mps_diag_action_t *)arg);
		mps_unlock(sc);
		break;
	case MPTIOCTL_EVENT_QUERY:
		/*
		 * The user has done an event query. Call our routine which does
		 * this.
		 */
		error = 0;
		mps_user_event_query(sc, (mps_event_query_t *)arg);
		break;
	case MPTIOCTL_EVENT_ENABLE:
		/*
		 * The user has done an event enable. Call our routine which
		 * does this.
		 */
		error = 0;
		mps_user_event_enable(sc, (mps_event_enable_t *)arg);
		break;
	case MPTIOCTL_EVENT_REPORT:
		/*
		 * The user has done an event report. Call our routine which
		 * does this.
		 */
		error = mps_user_event_report(sc, (mps_event_report_t *)arg);
		break;
	case MPTIOCTL_REG_ACCESS:
		/*
		 * The user has requested register access.  Call our routine
		 * which does this.
		 */
		mps_lock(sc);
		error = mps_user_reg_access(sc, (mps_reg_access_t *)arg);
		mps_unlock(sc);
		break;
	case MPTIOCTL_BTDH_MAPPING:
		/*
		 * The user has requested to translate a bus/target to a
		 * DevHandle or a DevHandle to a bus/target.  Call our routine
		 * which does this.
		 */
		error = mps_user_btdh(sc, (mps_btdh_mapping_t *)arg);
		break;
	default:
		error = ENOIOCTL;
		break;
	}

	if (mps_page != NULL)
		free(mps_page, M_MPSUSER);

	return (error);
}

#ifdef COMPAT_FREEBSD32

struct mps_cfg_page_req32 {
	MPI2_CONFIG_PAGE_HEADER header;
	uint32_t page_address;
	uint32_t buf;
	int	len;	
	uint16_t ioc_status;
};

struct mps_ext_cfg_page_req32 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER header;
	uint32_t page_address;
	uint32_t buf;
	int	len;
	uint16_t ioc_status;
};

struct mps_raid_action32 {
	uint8_t action;
	uint8_t volume_bus;
	uint8_t volume_id;
	uint8_t phys_disk_num;
	uint32_t action_data_word;
	uint32_t buf;
	int len;
	uint32_t volume_status;
	uint32_t action_data[4];
	uint16_t action_status;
	uint16_t ioc_status;
	uint8_t write;
};

struct mps_usr_command32 {
	uint32_t req;
	uint32_t req_len;
	uint32_t rpl;
	uint32_t rpl_len;
	uint32_t buf;
	int len;
	uint32_t flags;
};

#define	MPSIO_READ_CFG_HEADER32	_IOWR('M', 200, struct mps_cfg_page_req32)
#define	MPSIO_READ_CFG_PAGE32	_IOWR('M', 201, struct mps_cfg_page_req32)
#define	MPSIO_READ_EXT_CFG_HEADER32 _IOWR('M', 202, struct mps_ext_cfg_page_req32)
#define	MPSIO_READ_EXT_CFG_PAGE32 _IOWR('M', 203, struct mps_ext_cfg_page_req32)
#define	MPSIO_WRITE_CFG_PAGE32	_IOWR('M', 204, struct mps_cfg_page_req32)
#define	MPSIO_RAID_ACTION32	_IOWR('M', 205, struct mps_raid_action32)
#define	MPSIO_MPS_COMMAND32	_IOWR('M', 210, struct mps_usr_command32)

static int
mps_ioctl32(struct cdev *dev, u_long cmd32, void *_arg, int flag,
    struct thread *td)
{
	struct mps_cfg_page_req32 *page32 = _arg;
	struct mps_ext_cfg_page_req32 *ext32 = _arg;
	struct mps_raid_action32 *raid32 = _arg;
	struct mps_usr_command32 *user32 = _arg;
	union {
		struct mps_cfg_page_req page;
		struct mps_ext_cfg_page_req ext;
		struct mps_raid_action raid;
		struct mps_usr_command user;
	} arg;
	u_long cmd;
	int error;

	switch (cmd32) {
	case MPSIO_READ_CFG_HEADER32:
	case MPSIO_READ_CFG_PAGE32:
	case MPSIO_WRITE_CFG_PAGE32:
		if (cmd32 == MPSIO_READ_CFG_HEADER32)
			cmd = MPSIO_READ_CFG_HEADER;
		else if (cmd32 == MPSIO_READ_CFG_PAGE32)
			cmd = MPSIO_READ_CFG_PAGE;
		else
			cmd = MPSIO_WRITE_CFG_PAGE;
		CP(*page32, arg.page, header);
		CP(*page32, arg.page, page_address);
		PTRIN_CP(*page32, arg.page, buf);
		CP(*page32, arg.page, len);
		CP(*page32, arg.page, ioc_status);
		break;

	case MPSIO_READ_EXT_CFG_HEADER32:
	case MPSIO_READ_EXT_CFG_PAGE32:
		if (cmd32 == MPSIO_READ_EXT_CFG_HEADER32)
			cmd = MPSIO_READ_EXT_CFG_HEADER;
		else
			cmd = MPSIO_READ_EXT_CFG_PAGE;
		CP(*ext32, arg.ext, header);
		CP(*ext32, arg.ext, page_address);
		PTRIN_CP(*ext32, arg.ext, buf);
		CP(*ext32, arg.ext, len);
		CP(*ext32, arg.ext, ioc_status);
		break;

	case MPSIO_RAID_ACTION32:
		cmd = MPSIO_RAID_ACTION;
		CP(*raid32, arg.raid, action);
		CP(*raid32, arg.raid, volume_bus);
		CP(*raid32, arg.raid, volume_id);
		CP(*raid32, arg.raid, phys_disk_num);
		CP(*raid32, arg.raid, action_data_word);
		PTRIN_CP(*raid32, arg.raid, buf);
		CP(*raid32, arg.raid, len);
		CP(*raid32, arg.raid, volume_status);
		bcopy(raid32->action_data, arg.raid.action_data,
		    sizeof arg.raid.action_data);
		CP(*raid32, arg.raid, ioc_status);
		CP(*raid32, arg.raid, write);
		break;

	case MPSIO_MPS_COMMAND32:
		cmd = MPSIO_MPS_COMMAND;
		PTRIN_CP(*user32, arg.user, req);
		CP(*user32, arg.user, req_len);
		PTRIN_CP(*user32, arg.user, rpl);
		CP(*user32, arg.user, rpl_len);
		PTRIN_CP(*user32, arg.user, buf);
		CP(*user32, arg.user, len);
		CP(*user32, arg.user, flags);
		break;
	default:
		return (ENOIOCTL);
	}

	error = mps_ioctl(dev, cmd, &arg, flag, td);
	if (error == 0 && (cmd32 & IOC_OUT) != 0) {
		switch (cmd32) {
		case MPSIO_READ_CFG_HEADER32:
		case MPSIO_READ_CFG_PAGE32:
		case MPSIO_WRITE_CFG_PAGE32:
			CP(arg.page, *page32, header);
			CP(arg.page, *page32, page_address);
			PTROUT_CP(arg.page, *page32, buf);
			CP(arg.page, *page32, len);
			CP(arg.page, *page32, ioc_status);
			break;

		case MPSIO_READ_EXT_CFG_HEADER32:
		case MPSIO_READ_EXT_CFG_PAGE32:
			CP(arg.ext, *ext32, header);
			CP(arg.ext, *ext32, page_address);
			PTROUT_CP(arg.ext, *ext32, buf);
			CP(arg.ext, *ext32, len);
			CP(arg.ext, *ext32, ioc_status);
			break;

		case MPSIO_RAID_ACTION32:
			CP(arg.raid, *raid32, action);
			CP(arg.raid, *raid32, volume_bus);
			CP(arg.raid, *raid32, volume_id);
			CP(arg.raid, *raid32, phys_disk_num);
			CP(arg.raid, *raid32, action_data_word);
			PTROUT_CP(arg.raid, *raid32, buf);
			CP(arg.raid, *raid32, len);
			CP(arg.raid, *raid32, volume_status);
			bcopy(arg.raid.action_data, raid32->action_data,
			    sizeof arg.raid.action_data);
			CP(arg.raid, *raid32, ioc_status);
			CP(arg.raid, *raid32, write);
			break;

		case MPSIO_MPS_COMMAND32:
			PTROUT_CP(arg.user, *user32, req);
			CP(arg.user, *user32, req_len);
			PTROUT_CP(arg.user, *user32, rpl);
			CP(arg.user, *user32, rpl_len);
			PTROUT_CP(arg.user, *user32, buf);
			CP(arg.user, *user32, len);
			CP(arg.user, *user32, flags);
			break;
		}
	}

	return (error);
}
#endif /* COMPAT_FREEBSD32 */

static int
mps_ioctl_devsw(struct cdev *dev, u_long com, caddr_t arg, int flag,
    struct thread *td)
{
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return (mps_ioctl32(dev, com, arg, flag, td));
#endif
	return (mps_ioctl(dev, com, arg, flag, td));
}
