/*-
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
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD userland interface
 */
/*-
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2016 Avago Technologies
 * Copyright 2000-2020 Broadcom Inc.
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
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* TODO Move headers to mprvar */
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

#include <dev/mpr/mpi/mpi2_type.h>
#include <dev/mpr/mpi/mpi2.h>
#include <dev/mpr/mpi/mpi2_ioc.h>
#include <dev/mpr/mpi/mpi2_cnfg.h>
#include <dev/mpr/mpi/mpi2_init.h>
#include <dev/mpr/mpi/mpi2_tool.h>
#include <dev/mpr/mpi/mpi2_pci.h>
#include <dev/mpr/mpr_ioctl.h>
#include <dev/mpr/mprvar.h>
#include <dev/mpr/mpr_table.h>
#include <dev/mpr/mpr_sas.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

static d_open_t		mpr_open;
static d_close_t	mpr_close;
static d_ioctl_t	mpr_ioctl_devsw;

static struct cdevsw mpr_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	mpr_open,
	.d_close =	mpr_close,
	.d_ioctl =	mpr_ioctl_devsw,
	.d_name =	"mpr",
};

typedef int (mpr_user_f)(struct mpr_command *, struct mpr_usr_command *);
static mpr_user_f	mpi_pre_ioc_facts;
static mpr_user_f	mpi_pre_port_facts;
static mpr_user_f	mpi_pre_fw_download;
static mpr_user_f	mpi_pre_fw_upload;
static mpr_user_f	mpi_pre_sata_passthrough;
static mpr_user_f	mpi_pre_smp_passthrough;
static mpr_user_f	mpi_pre_config;
static mpr_user_f	mpi_pre_sas_io_unit_control;

static int mpr_user_read_cfg_header(struct mpr_softc *,
    struct mpr_cfg_page_req *);
static int mpr_user_read_cfg_page(struct mpr_softc *,
    struct mpr_cfg_page_req *, void *);
static int mpr_user_read_extcfg_header(struct mpr_softc *,
    struct mpr_ext_cfg_page_req *);
static int mpr_user_read_extcfg_page(struct mpr_softc *,
    struct mpr_ext_cfg_page_req *, void *);
static int mpr_user_write_cfg_page(struct mpr_softc *,
    struct mpr_cfg_page_req *, void *);
static int mpr_user_setup_request(struct mpr_command *,
    struct mpr_usr_command *);
static int mpr_user_command(struct mpr_softc *, struct mpr_usr_command *);

static int mpr_user_pass_thru(struct mpr_softc *sc, mpr_pass_thru_t *data);
static void mpr_user_get_adapter_data(struct mpr_softc *sc,
    mpr_adapter_data_t *data);
static void mpr_user_read_pci_info(struct mpr_softc *sc, mpr_pci_info_t *data);
static uint8_t mpr_get_fw_diag_buffer_number(struct mpr_softc *sc,
    uint32_t unique_id);
static int mpr_post_fw_diag_buffer(struct mpr_softc *sc,
    mpr_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code);
static int mpr_release_fw_diag_buffer(struct mpr_softc *sc,
    mpr_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code,
    uint32_t diag_type);
static int mpr_diag_register(struct mpr_softc *sc,
    mpr_fw_diag_register_t *diag_register, uint32_t *return_code);
static int mpr_diag_unregister(struct mpr_softc *sc,
    mpr_fw_diag_unregister_t *diag_unregister, uint32_t *return_code);
static int mpr_diag_query(struct mpr_softc *sc, mpr_fw_diag_query_t *diag_query,
    uint32_t *return_code);
static int mpr_diag_read_buffer(struct mpr_softc *sc,
    mpr_diag_read_buffer_t *diag_read_buffer, uint8_t *ioctl_buf,
    uint32_t *return_code);
static int mpr_diag_release(struct mpr_softc *sc,
    mpr_fw_diag_release_t *diag_release, uint32_t *return_code);
static int mpr_do_diag_action(struct mpr_softc *sc, uint32_t action,
    uint8_t *diag_action, uint32_t length, uint32_t *return_code);
static int mpr_user_diag_action(struct mpr_softc *sc, mpr_diag_action_t *data);
static void mpr_user_event_query(struct mpr_softc *sc, mpr_event_query_t *data);
static void mpr_user_event_enable(struct mpr_softc *sc,
    mpr_event_enable_t *data);
static int mpr_user_event_report(struct mpr_softc *sc,
    mpr_event_report_t *data);
static int mpr_user_reg_access(struct mpr_softc *sc, mpr_reg_access_t *data);
static int mpr_user_btdh(struct mpr_softc *sc, mpr_btdh_mapping_t *data);

static MALLOC_DEFINE(M_MPRUSER, "mpr_user", "Buffers for mpr(4) ioctls");

/* Macros from compat/freebsd32/freebsd32.h */
#define	PTRIN(v)	(void *)(uintptr_t)(v)
#define	PTROUT(v)	(uint32_t)(uintptr_t)(v)

#define	CP(src,dst,fld) do { (dst).fld = (src).fld; } while (0)
#define	PTRIN_CP(src,dst,fld)				\
	do { (dst).fld = PTRIN((src).fld); } while (0)
#define	PTROUT_CP(src,dst,fld) \
	do { (dst).fld = PTROUT((src).fld); } while (0)

/*
 * MPI functions that support IEEE SGLs for SAS3.
 */
static uint8_t ieee_sgl_func_list[] = {
	MPI2_FUNCTION_SCSI_IO_REQUEST,
	MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH,
	MPI2_FUNCTION_SMP_PASSTHROUGH,
	MPI2_FUNCTION_SATA_PASSTHROUGH,
	MPI2_FUNCTION_FW_UPLOAD,
	MPI2_FUNCTION_FW_DOWNLOAD,
	MPI2_FUNCTION_TARGET_ASSIST,
	MPI2_FUNCTION_TARGET_STATUS_SEND,
	MPI2_FUNCTION_TOOLBOX
};

int
mpr_attach_user(struct mpr_softc *sc)
{
	int unit;

	unit = device_get_unit(sc->mpr_dev);
	sc->mpr_cdev = make_dev(&mpr_cdevsw, unit, UID_ROOT, GID_OPERATOR, 0640,
	    "mpr%d", unit);

	if (sc->mpr_cdev == NULL)
		return (ENOMEM);

	sc->mpr_cdev->si_drv1 = sc;
	return (0);
}

void
mpr_detach_user(struct mpr_softc *sc)
{

	/* XXX: do a purge of pending requests? */
	if (sc->mpr_cdev != NULL)
		destroy_dev(sc->mpr_cdev);
}

static int
mpr_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
mpr_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	return (0);
}

static int
mpr_user_read_cfg_header(struct mpr_softc *sc,
    struct mpr_cfg_page_req *page_req)
{
	MPI2_CONFIG_PAGE_HEADER *hdr;
	struct mpr_config_params params;
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

	if ((error = mpr_read_config_page(sc, &params)) != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mpr_printf(sc, "read_cfg_header timed out\n");
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
mpr_user_read_cfg_page(struct mpr_softc *sc, struct mpr_cfg_page_req *page_req,
    void *buf)
{
	MPI2_CONFIG_PAGE_HEADER *reqhdr, *hdr;
	struct mpr_config_params params;
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

	if ((error = mpr_read_config_page(sc, &params)) != 0) {
		mpr_printf(sc, "mpr_user_read_cfg_page timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(params.status);
	return (0);
}

static int
mpr_user_read_extcfg_header(struct mpr_softc *sc,
    struct mpr_ext_cfg_page_req *ext_page_req)
{
	MPI2_CONFIG_EXTENDED_PAGE_HEADER *hdr;
	struct mpr_config_params params;
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

	if ((error = mpr_read_config_page(sc, &params)) != 0) {
		/*
		 * Leave the request. Without resetting the chip, it's
		 * still owned by it and we'll just get into trouble
		 * freeing it now. Mark it as abandoned so that if it
		 * shows up later it can be freed.
		 */
		mpr_printf(sc, "mpr_user_read_extcfg_header timed out\n");
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
mpr_user_read_extcfg_page(struct mpr_softc *sc,
    struct mpr_ext_cfg_page_req *ext_page_req, void *buf)
{
	MPI2_CONFIG_EXTENDED_PAGE_HEADER *reqhdr, *hdr;
	struct mpr_config_params params;
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

	if ((error = mpr_read_config_page(sc, &params)) != 0) {
		mpr_printf(sc, "mpr_user_read_extcfg_page timed out\n");
		return (ETIMEDOUT);
	}

	ext_page_req->ioc_status = htole16(params.status);
	return (0);
}

static int
mpr_user_write_cfg_page(struct mpr_softc *sc,
    struct mpr_cfg_page_req *page_req, void *buf)
{
	MPI2_CONFIG_PAGE_HEADER *reqhdr, *hdr;
	struct mpr_config_params params;
	u_int	      hdr_attr;
	int	      error;

	reqhdr = buf;
	hdr = &params.hdr.Struct;
	hdr_attr = reqhdr->PageType & MPI2_CONFIG_PAGEATTR_MASK;
	if (hdr_attr != MPI2_CONFIG_PAGEATTR_CHANGEABLE &&
	    hdr_attr != MPI2_CONFIG_PAGEATTR_PERSISTENT) {
		mpr_printf(sc, "page type 0x%x not changeable\n",
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

	if ((error = mpr_write_config_page(sc, &params)) != 0) {
		mpr_printf(sc, "mpr_write_cfg_page timed out\n");
		return (ETIMEDOUT);
	}

	page_req->ioc_status = htole16(params.status);
	return (0);
}

void
mpr_init_sge(struct mpr_command *cm, void *req, void *sge)
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
 * Prepare the mpr_command for an IOC_FACTS request.
 */
static int
mpi_pre_ioc_facts(struct mpr_command *cm, struct mpr_usr_command *cmd)
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
 * Prepare the mpr_command for a PORT_FACTS request.
 */
static int
mpi_pre_port_facts(struct mpr_command *cm, struct mpr_usr_command *cmd)
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
 * Prepare the mpr_command for a FW_DOWNLOAD request.
 */
static int
mpi_pre_fw_download(struct mpr_command *cm, struct mpr_usr_command *cmd)
{
	MPI25_FW_DOWNLOAD_REQUEST *req = (void *)cm->cm_req;
	MPI2_FW_DOWNLOAD_REPLY *rpl;
	int error;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	if (cmd->len == 0)
		return (EINVAL);

	error = copyin(cmd->buf, cm->cm_data, cmd->len);
	if (error != 0)
		return (error);

	mpr_init_sge(cm, req, &req->SGL);

	/*
	 * For now, the F/W image must be provided in a single request.
	 */
	if ((req->MsgFlags & MPI2_FW_DOWNLOAD_MSGFLGS_LAST_SEGMENT) == 0)
		return (EINVAL);
	if (req->TotalImageSize != cmd->len)
		return (EINVAL);

	req->ImageOffset = 0;
	req->ImageSize = cmd->len;

	cm->cm_flags |= MPR_CM_FLAGS_DATAOUT;

	return (mpr_push_ieee_sge(cm, &req->SGL, 0));
}

/*
 * Prepare the mpr_command for a FW_UPLOAD request.
 */
static int
mpi_pre_fw_upload(struct mpr_command *cm, struct mpr_usr_command *cmd)
{
	MPI25_FW_UPLOAD_REQUEST *req = (void *)cm->cm_req;
	MPI2_FW_UPLOAD_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpr_init_sge(cm, req, &req->SGL);
	if (cmd->len == 0) {
		/* Perhaps just asking what the size of the fw is? */
		return (0);
	}

	req->ImageOffset = 0;
	req->ImageSize = cmd->len;

	cm->cm_flags |= MPR_CM_FLAGS_DATAIN;

	return (mpr_push_ieee_sge(cm, &req->SGL, 0));
}

/*
 * Prepare the mpr_command for a SATA_PASSTHROUGH request.
 */
static int
mpi_pre_sata_passthrough(struct mpr_command *cm, struct mpr_usr_command *cmd)
{
	MPI2_SATA_PASSTHROUGH_REQUEST *req = (void *)cm->cm_req;
	MPI2_SATA_PASSTHROUGH_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpr_init_sge(cm, req, &req->SGL);
	return (0);
}

/*
 * Prepare the mpr_command for a SMP_PASSTHROUGH request.
 */
static int
mpi_pre_smp_passthrough(struct mpr_command *cm, struct mpr_usr_command *cmd)
{
	MPI2_SMP_PASSTHROUGH_REQUEST *req = (void *)cm->cm_req;
	MPI2_SMP_PASSTHROUGH_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpr_init_sge(cm, req, &req->SGL);
	return (0);
}

/*
 * Prepare the mpr_command for a CONFIG request.
 */
static int
mpi_pre_config(struct mpr_command *cm, struct mpr_usr_command *cmd)
{
	MPI2_CONFIG_REQUEST *req = (void *)cm->cm_req;
	MPI2_CONFIG_REPLY *rpl;

	if (cmd->req_len != sizeof *req)
		return (EINVAL);
	if (cmd->rpl_len != sizeof *rpl)
		return (EINVAL);

	mpr_init_sge(cm, req, &req->PageBufferSGE);
	return (0);
}

/*
 * Prepare the mpr_command for a SAS_IO_UNIT_CONTROL request.
 */
static int
mpi_pre_sas_io_unit_control(struct mpr_command *cm,
			     struct mpr_usr_command *cmd)
{

	cm->cm_sge = NULL;
	cm->cm_sglsize = 0;
	return (0);
}

/*
 * A set of functions to prepare an mpr_command for the various
 * supported requests.
 */
struct mpr_user_func {
	U8		Function;
	mpr_user_f	*f_pre;
} mpr_user_func_list[] = {
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
mpr_user_setup_request(struct mpr_command *cm, struct mpr_usr_command *cmd)
{
	MPI2_REQUEST_HEADER *hdr = (MPI2_REQUEST_HEADER *)cm->cm_req;	
	struct mpr_user_func *f;

	for (f = mpr_user_func_list; f->f_pre != NULL; f++) {
		if (hdr->Function == f->Function)
			return (f->f_pre(cm, cmd));
	}
	return (EINVAL);
}	

static int
mpr_user_command(struct mpr_softc *sc, struct mpr_usr_command *cmd)
{
	MPI2_REQUEST_HEADER *hdr;	
	MPI2_DEFAULT_REPLY *rpl = NULL;
	void *buf = NULL;
	struct mpr_command *cm = NULL;
	int err = 0;
	int sz;

	mpr_lock(sc);
	cm = mpr_alloc_command(sc);

	if (cm == NULL) {
		mpr_printf(sc, "%s: no mpr requests\n", __func__);
		err = ENOMEM;
		goto RetFree;
	}
	mpr_unlock(sc);

	hdr = (MPI2_REQUEST_HEADER *)cm->cm_req;

	mpr_dprint(sc, MPR_USER, "%s: req %p %d  rpl %p %d\n", __func__,
	    cmd->req, cmd->req_len, cmd->rpl, cmd->rpl_len);

	if (cmd->req_len > (int)sc->reqframesz) {
		err = EINVAL;
		goto RetFreeUnlocked;
	}
	err = copyin(cmd->req, hdr, cmd->req_len);
	if (err != 0)
		goto RetFreeUnlocked;

	mpr_dprint(sc, MPR_USER, "%s: Function %02X MsgFlags %02X\n", __func__,
	    hdr->Function, hdr->MsgFlags);

	if (cmd->len > 0) {
		buf = malloc(cmd->len, M_MPRUSER, M_WAITOK|M_ZERO);
		cm->cm_data = buf;
		cm->cm_length = cmd->len;
	} else {
		cm->cm_data = NULL;
		cm->cm_length = 0;
	}

	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;

	err = mpr_user_setup_request(cm, cmd);
	if (err == EINVAL) {
		mpr_printf(sc, "%s: unsupported parameter or unsupported "
		    "function in request (function = 0x%X)\n", __func__,
		    hdr->Function);
	}
	if (err != 0)
		goto RetFreeUnlocked;

	mpr_lock(sc);
	err = mpr_wait_command(sc, &cm, 30, CAN_SLEEP);

	if (err || (cm == NULL)) {
		mpr_printf(sc, "%s: invalid request: error %d\n",
		    __func__, err);
		goto RetFree;
	}

	if (cm != NULL)
		rpl = (MPI2_DEFAULT_REPLY *)cm->cm_reply;
	if (rpl != NULL)
		sz = rpl->MsgLength * 4;
	else
		sz = 0;
	
	if (sz > cmd->rpl_len) {
		mpr_printf(sc, "%s: user reply buffer (%d) smaller than "
		    "returned buffer (%d)\n", __func__, cmd->rpl_len, sz);
		sz = cmd->rpl_len;
	}	

	mpr_unlock(sc);
	copyout(rpl, cmd->rpl, sz);
	if (buf != NULL)
		copyout(buf, cmd->buf, cmd->len);
	mpr_dprint(sc, MPR_USER, "%s: reply size %d\n", __func__, sz);

RetFreeUnlocked:
	mpr_lock(sc);
RetFree:
	if (cm != NULL)
		mpr_free_command(sc, cm);
	mpr_unlock(sc);
	if (buf != NULL)
		free(buf, M_MPRUSER);
	return (err);
}

static int
mpr_user_pass_thru(struct mpr_softc *sc, mpr_pass_thru_t *data)
{
	MPI2_REQUEST_HEADER	*hdr, tmphdr;	
	MPI2_DEFAULT_REPLY	*rpl;
	Mpi26NVMeEncapsulatedErrorReply_t *nvme_error_reply = NULL;
	Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request = NULL;
	struct mpr_command	*cm = NULL;
	int			i, err = 0, dir = 0, sz;
	uint8_t			tool, function = 0;
	u_int			sense_len;
	struct mprsas_target	*targ = NULL;

	/*
	 * Only allow one passthru command at a time.  Use the MPR_FLAGS_BUSY
	 * bit to denote that a passthru is being processed.
	 */
	mpr_lock(sc);
	if (sc->mpr_flags & MPR_FLAGS_BUSY) {
		mpr_dprint(sc, MPR_USER, "%s: Only one passthru command "
		    "allowed at a single time.", __func__);
		mpr_unlock(sc);
		return (EBUSY);
	}
	sc->mpr_flags |= MPR_FLAGS_BUSY;
	mpr_unlock(sc);

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
	    (data->DataDirection == MPR_PASS_THRU_DIRECTION_NONE)) ||
	    ((data->DataSize != 0) &&
	    ((data->DataDirection == MPR_PASS_THRU_DIRECTION_READ) ||
	    (data->DataDirection == MPR_PASS_THRU_DIRECTION_WRITE) ||
	    ((data->DataDirection == MPR_PASS_THRU_DIRECTION_BOTH) &&
	    (data->DataOutSize != 0))))) {
		if (data->DataDirection == MPR_PASS_THRU_DIRECTION_BOTH)
			data->DataDirection = MPR_PASS_THRU_DIRECTION_READ;
		else
			data->DataOutSize = 0;
	} else
		return (EINVAL);

	mpr_dprint(sc, MPR_USER, "%s: req 0x%jx %d  rpl 0x%jx %d "
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
	mpr_dprint(sc, MPR_USER, "%s: Function %02X MsgFlags %02X\n", __func__,
	    function, tmphdr.MsgFlags);

	/*
	 * Handle a passthru TM request.
	 */
	if (function == MPI2_FUNCTION_SCSI_TASK_MGMT) {
		MPI2_SCSI_TASK_MANAGE_REQUEST	*task;

		mpr_lock(sc);
		cm = mprsas_alloc_tm(sc);
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

		targ = mprsas_find_target_by_handle(sc->sassc, 0,
		    task->DevHandle);
		if (targ == NULL) {
			mpr_dprint(sc, MPR_INFO,
			   "%s %d : invalid handle for requested TM 0x%x \n",
			   __func__, __LINE__, task->DevHandle);
			err = 1;
		} else {
			mprsas_prepare_for_tm(sc, cm, targ, CAM_LUN_WILDCARD);
			err = mpr_wait_command(sc, &cm, 30, CAN_SLEEP);
		}

		if (err != 0) {
			err = EIO;
			mpr_dprint(sc, MPR_FAULT, "%s: task management failed",
			    __func__);
		}
		/*
		 * Copy the reply data and sense data to user space.
		 */
		if ((cm != NULL) && (cm->cm_reply != NULL)) {
			rpl = (MPI2_DEFAULT_REPLY *)cm->cm_reply;
			sz = rpl->MsgLength * 4;
	
			if (sz > data->ReplySize) {
				mpr_printf(sc, "%s: user reply buffer (%d) "
				    "smaller than returned buffer (%d)\n",
				    __func__, data->ReplySize, sz);
			}
			mpr_unlock(sc);
			copyout(cm->cm_reply, PTRIN(data->PtrReply),
			    data->ReplySize);
			mpr_lock(sc);
		}
		mprsas_free_tm(sc, cm);
		goto Ret;
	}

	mpr_lock(sc);
	cm = mpr_alloc_command(sc);

	if (cm == NULL) {
		mpr_printf(sc, "%s: no mpr requests\n", __func__);
		err = ENOMEM;
		goto Ret;
	}
	mpr_unlock(sc);

	hdr = (MPI2_REQUEST_HEADER *)cm->cm_req;
	bcopy(&tmphdr, hdr, data->RequestSize);

	/*
	 * Do some checking to make sure the IOCTL request contains a valid
	 * request.  Then set the SGL info.
	 */
	mpr_init_sge(cm, hdr, (void *)((uint8_t *)hdr + data->RequestSize));

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
		cm->cm_data = malloc(cm->cm_length, M_MPRUSER, M_WAITOK |
		    M_ZERO);
		cm->cm_flags = MPR_CM_FLAGS_DATAIN;
		if (data->DataOutSize) {
			cm->cm_flags |= MPR_CM_FLAGS_DATAOUT;
			err = copyin(PTRIN(data->PtrDataOut),
			    cm->cm_data, data->DataOutSize);
		} else if (data->DataDirection ==
		    MPR_PASS_THRU_DIRECTION_WRITE) {
			cm->cm_flags = MPR_CM_FLAGS_DATAOUT;
			err = copyin(PTRIN(data->PtrData),
			    cm->cm_data, data->DataSize);
		}
		if (err != 0)
			mpr_dprint(sc, MPR_FAULT, "%s: failed to copy IOCTL "
			    "data from user space\n", __func__);
	}
	/*
	 * Set this flag only if processing a command that does not need an
	 * IEEE SGL.  The CLI Tool within the Toolbox uses IEEE SGLs, so clear
	 * the flag only for that tool if processing a Toolbox function.
	 */
	cm->cm_flags |= MPR_CM_FLAGS_SGE_SIMPLE;
	for (i = 0; i < sizeof (ieee_sgl_func_list); i++) {
		if (function == ieee_sgl_func_list[i]) {
			if (function == MPI2_FUNCTION_TOOLBOX)
			{
				tool = (uint8_t)hdr->FunctionDependent1;
				if (tool != MPI2_TOOLBOX_DIAGNOSTIC_CLI_TOOL)
					break;
			}
			cm->cm_flags &= ~MPR_CM_FLAGS_SGE_SIMPLE;
			break;
		}
	}
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;

	if (function == MPI2_FUNCTION_NVME_ENCAPSULATED) {
		nvme_encap_request =
		    (Mpi26NVMeEncapsulatedRequest_t *)cm->cm_req;
		cm->cm_desc.Default.RequestFlags =
		    MPI26_REQ_DESCRIPT_FLAGS_PCIE_ENCAPSULATED;

		/*
		 * Get the Physical Address of the sense buffer.
		 * Save the user's Error Response buffer address and use that
		 *   field to hold the sense buffer address.
		 * Clear the internal sense buffer, which will potentially hold
		 *   the Completion Queue Entry on return, or 0 if no Entry.
		 * Build the PRPs and set direction bits.
		 * Send the request.
		 */
		cm->nvme_error_response =
		    (uint64_t *)(uintptr_t)(((uint64_t)nvme_encap_request->
		    ErrorResponseBaseAddress.High << 32) |
		    (uint64_t)nvme_encap_request->
		    ErrorResponseBaseAddress.Low);
		nvme_encap_request->ErrorResponseBaseAddress.High =
		    htole32((uint32_t)((uint64_t)cm->cm_sense_busaddr >> 32));
		nvme_encap_request->ErrorResponseBaseAddress.Low =
		    htole32(cm->cm_sense_busaddr);
		memset(cm->cm_sense, 0, NVME_ERROR_RESPONSE_SIZE);
		mpr_build_nvme_prp(sc, cm, nvme_encap_request, cm->cm_data,
		    data->DataSize, data->DataOutSize);
	}

	/*
	 * Set up Sense buffer and SGL offset for IO passthru.  SCSI IO request
	 * uses SCSI IO or Fast Path SCSI IO descriptor.
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
		scsi_io_req->SenseBufferLowAddress =
		    htole32(cm->cm_sense_busaddr);

		/*
		 * Set SGLOffset0 value.  This is the number of dwords that SGL
		 * is offset from the beginning of MPI2_SCSI_IO_REQUEST struct.
		 */
		scsi_io_req->SGLOffset0 = 24;

		/*
		 * Setup descriptor info.  RAID passthrough must use the
		 * default request descriptor which is already set, so if this
		 * is a SCSI IO request, change the descriptor to SCSI IO or
		 * Fast Path SCSI IO.  Also, if this is a SCSI IO request,
		 * handle the reply in the mprsas_scsio_complete function.
		 */
		if (function == MPI2_FUNCTION_SCSI_IO_REQUEST) {
			targ = mprsas_find_target_by_handle(sc->sassc, 0,
			    scsi_io_req->DevHandle);

			if (!targ) {
				printf("No Target found for handle %d\n",
				    scsi_io_req->DevHandle);
				err = EINVAL;
				goto RetFreeUnlocked;
			}

			if (targ->scsi_req_desc_type ==
			    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO) {
				cm->cm_desc.FastPathSCSIIO.RequestFlags =
				    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
				if (!sc->atomic_desc_capable) {
					cm->cm_desc.FastPathSCSIIO.DevHandle =
					    scsi_io_req->DevHandle;
				}
				scsi_io_req->IoFlags |=
				    MPI25_SCSIIO_IOFLAGS_FAST_PATH;
			} else {
				cm->cm_desc.SCSIIO.RequestFlags =
				    MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
				if (!sc->atomic_desc_capable) {
					cm->cm_desc.SCSIIO.DevHandle =
					    scsi_io_req->DevHandle;
				}
			}

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

	mpr_lock(sc);

	err = mpr_wait_command(sc, &cm, 30, CAN_SLEEP);

	if (err || (cm == NULL)) {
		mpr_printf(sc, "%s: invalid request: error %d\n", __func__,
		    err);
		goto RetFree;
	}

	/*
	 * Sync the DMA data, if any.  Then copy the data to user space.
	 */
	if (cm->cm_data != NULL) {
		if (cm->cm_flags & MPR_CM_FLAGS_DATAIN)
			dir = BUS_DMASYNC_POSTREAD;
		else if (cm->cm_flags & MPR_CM_FLAGS_DATAOUT)
			dir = BUS_DMASYNC_POSTWRITE;
		bus_dmamap_sync(sc->buffer_dmat, cm->cm_dmamap, dir);
		bus_dmamap_unload(sc->buffer_dmat, cm->cm_dmamap);

		if (cm->cm_flags & MPR_CM_FLAGS_DATAIN) {
			mpr_unlock(sc);
			err = copyout(cm->cm_data,
			    PTRIN(data->PtrData), data->DataSize);
			mpr_lock(sc);
			if (err != 0)
				mpr_dprint(sc, MPR_FAULT, "%s: failed to copy "
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
			mpr_printf(sc, "%s: user reply buffer (%d) smaller "
			    "than returned buffer (%d)\n", __func__,
			    data->ReplySize, sz);
		}
		mpr_unlock(sc);
		copyout(cm->cm_reply, PTRIN(data->PtrReply), data->ReplySize);
		mpr_lock(sc);

		if ((function == MPI2_FUNCTION_SCSI_IO_REQUEST) ||
		    (function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH)) {
			if (((MPI2_SCSI_IO_REPLY *)rpl)->SCSIState &
			    MPI2_SCSI_STATE_AUTOSENSE_VALID) {
				sense_len =
				    MIN((le32toh(((MPI2_SCSI_IO_REPLY *)rpl)->
				    SenseCount)), sizeof(struct
				    scsi_sense_data));
				mpr_unlock(sc);
				copyout(cm->cm_sense, (PTRIN(data->PtrReply +
				    sizeof(MPI2_SCSI_IO_REPLY))), sense_len);
				mpr_lock(sc);
			}
		}

		/*
		 * Copy out the NVMe Error Reponse to user. The Error Response
		 * buffer is given by the user, but a sense buffer is used to
		 * get that data from the IOC. The user's
		 * ErrorResponseBaseAddress is saved in the
		 * 'nvme_error_response' field before the command because that
		 * field is set to a sense buffer. When the command is
		 * complete, the Error Response data from the IOC is copied to
		 * that user address after it is checked for validity.
		 * Also note that 'sense' buffers are not defined for
		 * NVMe commands. Sense terminalogy is only used here so that
		 * the same IOCTL structure and sense buffers can be used for
		 * NVMe.
		 */
		if (function == MPI2_FUNCTION_NVME_ENCAPSULATED) {
			if (cm->nvme_error_response == NULL) {
				mpr_dprint(sc, MPR_INFO, "NVMe Error Response "
				    "buffer is NULL. Response data will not be "
				    "returned.\n");
				mpr_unlock(sc);
				goto RetFreeUnlocked;
			}

			nvme_error_reply =
			    (Mpi26NVMeEncapsulatedErrorReply_t *)cm->cm_reply;
			sz = MIN(le32toh(nvme_error_reply->ErrorResponseCount),
			    NVME_ERROR_RESPONSE_SIZE);
			mpr_unlock(sc);
			copyout(cm->cm_sense,
			    (PTRIN(data->PtrReply +
			    sizeof(MPI2_SCSI_IO_REPLY))), sz);
			mpr_lock(sc);
		}
	}
	mpr_unlock(sc);

RetFreeUnlocked:
	mpr_lock(sc);

RetFree:
	if (cm != NULL) {
		if (cm->cm_data)
			free(cm->cm_data, M_MPRUSER);
		mpr_free_command(sc, cm);
	}
Ret:
	sc->mpr_flags &= ~MPR_FLAGS_BUSY;
	mpr_unlock(sc);

	return (err);
}

static void
mpr_user_get_adapter_data(struct mpr_softc *sc, mpr_adapter_data_t *data)
{
	Mpi2ConfigReply_t	mpi_reply;
	Mpi2BiosPage3_t		config_page;

	/*
	 * Use the PCI interface functions to get the Bus, Device, and Function
	 * information.
	 */
	data->PciInformation.u.bits.BusNumber = pci_get_bus(sc->mpr_dev);
	data->PciInformation.u.bits.DeviceNumber = pci_get_slot(sc->mpr_dev);
	data->PciInformation.u.bits.FunctionNumber =
	    pci_get_function(sc->mpr_dev);

	/*
	 * Get the FW version that should already be saved in IOC Facts.
	 */
	data->MpiFirmwareVersion = sc->facts->FWVersion.Word;

	/*
	 * General device info.
	 */
	if (sc->mpr_flags & MPR_FLAGS_GEN35_IOC)
		data->AdapterType = MPRIOCTL_ADAPTER_TYPE_SAS35;
	else
		data->AdapterType = MPRIOCTL_ADAPTER_TYPE_SAS3;
	data->PCIDeviceHwId = pci_get_device(sc->mpr_dev);
	data->PCIDeviceHwRev = pci_read_config(sc->mpr_dev, PCIR_REVID, 1);
	data->SubSystemId = pci_get_subdevice(sc->mpr_dev);
	data->SubsystemVendorId = pci_get_subvendor(sc->mpr_dev);

	/*
	 * Get the driver version.
	 */
	strcpy((char *)&data->DriverVersion[0], MPR_DRIVER_VERSION);

	/*
	 * Need to get BIOS Config Page 3 for the BIOS Version.
	 */
	data->BiosVersion = 0;
	mpr_lock(sc);
	if (mpr_config_get_bios_pg3(sc, &mpi_reply, &config_page))
		printf("%s: Error while retrieving BIOS Version\n", __func__);
	else
		data->BiosVersion = config_page.BiosVersion;
	mpr_unlock(sc);
}

static void
mpr_user_read_pci_info(struct mpr_softc *sc, mpr_pci_info_t *data)
{
	int	i;

	/*
	 * Use the PCI interface functions to get the Bus, Device, and Function
	 * information.
	 */
	data->BusNumber = pci_get_bus(sc->mpr_dev);
	data->DeviceNumber = pci_get_slot(sc->mpr_dev);
	data->FunctionNumber = pci_get_function(sc->mpr_dev);

	/*
	 * Now get the interrupt vector and the pci header.  The vector can
	 * only be 0 right now.  The header is the first 256 bytes of config
	 * space.
	 */
	data->InterruptVector = 0;
	for (i = 0; i < sizeof (data->PciHeader); i++) {
		data->PciHeader[i] = pci_read_config(sc->mpr_dev, i, 1);
	}
}

static uint8_t
mpr_get_fw_diag_buffer_number(struct mpr_softc *sc, uint32_t unique_id)
{
	uint8_t	index;

	for (index = 0; index < MPI2_DIAG_BUF_TYPE_COUNT; index++) {
		if (sc->fw_diag_buffer_list[index].unique_id == unique_id) {
			return (index);
		}
	}

	return (MPR_FW_DIAGNOSTIC_UID_NOT_FOUND);
}

static int
mpr_post_fw_diag_buffer(struct mpr_softc *sc,
    mpr_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code)
{
	MPI2_DIAG_BUFFER_POST_REQUEST	*req;
	MPI2_DIAG_BUFFER_POST_REPLY	*reply;
	struct mpr_command		*cm = NULL;
	int				i, status;

	/*
	 * If buffer is not enabled, just leave.
	 */
	*return_code = MPR_FW_DIAG_ERROR_POST_FAILED;
	if (!pBuffer->enabled) {
		return (MPR_DIAG_FAILURE);
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
	cm = mpr_alloc_command(sc);
	if (cm == NULL) {
		mpr_printf(sc, "%s: no mpr requests\n", __func__);
		return (MPR_DIAG_FAILURE);
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
	mpr_from_u64(sc->fw_diag_busaddr, &req->BufferAddress);
	cm->cm_data = NULL;
	cm->cm_length = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_complete_data = NULL;

	/*
	 * Send command synchronously.
	 */
	status = mpr_wait_command(sc, &cm, 30, CAN_SLEEP);
	if (status || (cm == NULL)) {
		mpr_printf(sc, "%s: invalid request: error %d\n", __func__,
		    status);
		status = MPR_DIAG_FAILURE;
		goto done;
	}

	/*
	 * Process POST reply.
	 */
	reply = (MPI2_DIAG_BUFFER_POST_REPLY *)cm->cm_reply;
	if (reply == NULL) {
		mpr_printf(sc, "%s: reply is NULL, probably due to "
		    "reinitialization", __func__);
		status = MPR_DIAG_FAILURE;
		goto done;
	}

	if ((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) {
		status = MPR_DIAG_FAILURE;
		mpr_dprint(sc, MPR_FAULT, "%s: post of FW  Diag Buffer failed "
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
	*return_code = MPR_FW_DIAG_ERROR_SUCCESS;
	status = MPR_DIAG_SUCCESS;

done:
	if (cm != NULL)
		mpr_free_command(sc, cm);
	return (status);
}

static int
mpr_release_fw_diag_buffer(struct mpr_softc *sc,
    mpr_fw_diagnostic_buffer_t *pBuffer, uint32_t *return_code,
    uint32_t diag_type)
{
	MPI2_DIAG_RELEASE_REQUEST	*req;
	MPI2_DIAG_RELEASE_REPLY		*reply;
	struct mpr_command		*cm = NULL;
	int				status;

	/*
	 * If buffer is not enabled, just leave.
	 */
	*return_code = MPR_FW_DIAG_ERROR_RELEASE_FAILED;
	if (!pBuffer->enabled) {
		mpr_dprint(sc, MPR_USER, "%s: This buffer type is not "
		    "supported by the IOC", __func__);
		return (MPR_DIAG_FAILURE);
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
	cm = mpr_alloc_command(sc);
	if (cm == NULL) {
		mpr_printf(sc, "%s: no mpr requests\n", __func__);
		return (MPR_DIAG_FAILURE);
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
	status = mpr_wait_command(sc, &cm, 30, CAN_SLEEP);
	if (status || (cm == NULL)) {
		mpr_printf(sc, "%s: invalid request: error %d\n", __func__,
		    status);
		status = MPR_DIAG_FAILURE;
		goto done;
	}

	/*
	 * Process RELEASE reply.
	 */
	reply = (MPI2_DIAG_RELEASE_REPLY *)cm->cm_reply;
	if (reply == NULL) {
		mpr_printf(sc, "%s: reply is NULL, probably due to "
		    "reinitialization", __func__);
		status = MPR_DIAG_FAILURE;
		goto done;
	}
	if (((le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK) !=
	    MPI2_IOCSTATUS_SUCCESS) || pBuffer->owned_by_firmware) {
		status = MPR_DIAG_FAILURE;
		mpr_dprint(sc, MPR_FAULT, "%s: release of FW Diag Buffer "
		    "failed with IOCStatus = 0x%x and IOCLogInfo = 0x%x\n",
		    __func__, le16toh(reply->IOCStatus),
		    le32toh(reply->IOCLogInfo));
		goto done;
	}

	/*
	 * Release was successful.
	 */
	*return_code = MPR_FW_DIAG_ERROR_SUCCESS;
	status = MPR_DIAG_SUCCESS;

	/*
	 * If this was for an UNREGISTER diag type command, clear the unique ID.
	 */
	if (diag_type == MPR_FW_DIAG_TYPE_UNREGISTER) {
		pBuffer->unique_id = MPR_FW_DIAG_INVALID_UID;
	}

done:
	if (cm != NULL)
		mpr_free_command(sc, cm);

	return (status);
}

static int
mpr_diag_register(struct mpr_softc *sc, mpr_fw_diag_register_t *diag_register,
    uint32_t *return_code)
{
	mpr_fw_diagnostic_buffer_t	*pBuffer;
	struct mpr_busdma_context	*ctx;
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
		*return_code = MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
		return (MPR_DIAG_FAILURE);
	}

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should not be found.  If it is, the ID is already in use.
	 */
	i = mpr_get_fw_diag_buffer_number(sc, unique_id);
	pBuffer = &sc->fw_diag_buffer_list[buffer_type];
	if (i != MPR_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
		return (MPR_DIAG_FAILURE);
	}

	/*
	 * The buffer's unique ID should not be registered yet, and the given
	 * unique ID cannot be 0.
	 */
	if ((pBuffer->unique_id != MPR_FW_DIAG_INVALID_UID) ||
	    (unique_id == MPR_FW_DIAG_INVALID_UID)) {
		*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
		return (MPR_DIAG_FAILURE);
	}

	/*
	 * If this buffer is already posted as immediate, just change owner.
	 */
	if (pBuffer->immediate && pBuffer->owned_by_firmware &&
	    (pBuffer->unique_id == MPR_FW_DIAG_INVALID_UID)) {
		pBuffer->immediate = FALSE;
		pBuffer->unique_id = unique_id;
		return (MPR_DIAG_SUCCESS);
	}

	/*
	 * Post a new buffer after checking if it's enabled.  The DMA buffer
	 * that is allocated will be contiguous (nsegments = 1).
	 */
	if (!pBuffer->enabled) {
		*return_code = MPR_FW_DIAG_ERROR_NO_BUFFER;
		return (MPR_DIAG_FAILURE);
	}
	if (bus_dma_tag_create( sc->mpr_parent_dmat,    /* parent */
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
		mpr_dprint(sc, MPR_ERROR,
		    "Cannot allocate FW diag buffer DMA tag\n");
		*return_code = MPR_FW_DIAG_ERROR_NO_BUFFER;
		status = MPR_DIAG_FAILURE;
		goto bailout;
	}
        if (bus_dmamem_alloc(sc->fw_diag_dmat, (void **)&sc->fw_diag_buffer,
	    BUS_DMA_NOWAIT, &sc->fw_diag_map)) {
		mpr_dprint(sc, MPR_ERROR,
		    "Cannot allocate FW diag buffer memory\n");
		*return_code = MPR_FW_DIAG_ERROR_NO_BUFFER;
		status = MPR_DIAG_FAILURE;
		goto bailout;
	}
	bzero(sc->fw_diag_buffer, buffer_size);

	ctx = malloc(sizeof(*ctx), M_MPR, M_WAITOK | M_ZERO);
	if (ctx == NULL) {
		device_printf(sc->mpr_dev, "%s: context malloc failed\n",
		    __func__);
		*return_code = MPR_FW_DIAG_ERROR_NO_BUFFER;
		status = MPR_DIAG_FAILURE;
		goto bailout;
	}
	ctx->addr = &sc->fw_diag_busaddr;
	ctx->buffer_dmat = sc->fw_diag_dmat;
	ctx->buffer_dmamap = sc->fw_diag_map;
	ctx->softc = sc;
	error = bus_dmamap_load(sc->fw_diag_dmat, sc->fw_diag_map,
	    sc->fw_diag_buffer, buffer_size, mpr_memaddr_wait_cb,
	    ctx, 0);
	if (error == EINPROGRESS) {

		/* XXX KDM */
		device_printf(sc->mpr_dev, "%s: Deferred bus_dmamap_load\n",
		    __func__);
		/*
		 * Wait for the load to complete.  If we're interrupted,
		 * bail out.
		 */
		mpr_lock(sc);
		if (ctx->completed == 0) {
			error = msleep(ctx, &sc->mpr_mtx, PCATCH, "mprwait", 0);
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
				mpr_unlock(sc);

				device_printf(sc->mpr_dev, "Cannot "
				    "bus_dmamap_load FW diag buffer, error = "
				    "%d returned from msleep\n", error);
				*return_code = MPR_FW_DIAG_ERROR_NO_BUFFER;
				status = MPR_DIAG_FAILURE;
				goto bailout;
			}
		}
		mpr_unlock(sc);
	} 

	if ((error != 0) || (ctx->error != 0)) {
		device_printf(sc->mpr_dev, "Cannot bus_dmamap_load FW diag "
		    "buffer, %serror = %d\n", error ? "" : "callback ",
		    error ? error : ctx->error);
		*return_code = MPR_FW_DIAG_ERROR_NO_BUFFER;
		status = MPR_DIAG_FAILURE;
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
	status = mpr_post_fw_diag_buffer(sc, pBuffer, return_code);

bailout:

	/*
	 * In case there was a failure, free the DMA buffer.
	 */
	if (status == MPR_DIAG_FAILURE) {
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
		free(ctx, M_MPR);

	return (status);
}

static int
mpr_diag_unregister(struct mpr_softc *sc,
    mpr_fw_diag_unregister_t *diag_unregister, uint32_t *return_code)
{
	mpr_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i;
	uint32_t			unique_id;
	int				status;

	unique_id = diag_unregister->UniqueId;

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should be there.
	 */
	i = mpr_get_fw_diag_buffer_number(sc, unique_id);
	if (i == MPR_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
		return (MPR_DIAG_FAILURE);
	}

	pBuffer = &sc->fw_diag_buffer_list[i];

	/*
	 * Try to release the buffer from FW before freeing it.  If release
	 * fails, don't free the DMA buffer in case FW tries to access it
	 * later.  If buffer is not owned by firmware, can't release it.
	 */
	if (!pBuffer->owned_by_firmware) {
		status = MPR_DIAG_SUCCESS;
	} else {
		status = mpr_release_fw_diag_buffer(sc, pBuffer, return_code,
		    MPR_FW_DIAG_TYPE_UNREGISTER);
	}

	/*
	 * At this point, return the current status no matter what happens with
	 * the DMA buffer.
	 */
	pBuffer->unique_id = MPR_FW_DIAG_INVALID_UID;
	if (status == MPR_DIAG_SUCCESS) {
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
mpr_diag_query(struct mpr_softc *sc, mpr_fw_diag_query_t *diag_query,
    uint32_t *return_code)
{
	mpr_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i;
	uint32_t			unique_id;

	unique_id = diag_query->UniqueId;

	/*
	 * If ID is valid, query on ID.
	 * If ID is invalid, query on buffer type.
	 */
	if (unique_id == MPR_FW_DIAG_INVALID_UID) {
		i = diag_query->BufferType;
		if (i >= MPI2_DIAG_BUF_TYPE_COUNT) {
			*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
			return (MPR_DIAG_FAILURE);
		}
	} else {
		i = mpr_get_fw_diag_buffer_number(sc, unique_id);
		if (i == MPR_FW_DIAGNOSTIC_UID_NOT_FOUND) {
			*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
			return (MPR_DIAG_FAILURE);
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
		diag_query->ApplicationFlags &= ~MPR_FW_DIAG_FLAG_APP_OWNED;
	} else {
		diag_query->ApplicationFlags |= MPR_FW_DIAG_FLAG_APP_OWNED;
	}
	if (pBuffer->valid_data || pBuffer->owned_by_firmware) {
		diag_query->ApplicationFlags |= MPR_FW_DIAG_FLAG_BUFFER_VALID;
	} else {
		diag_query->ApplicationFlags &= ~MPR_FW_DIAG_FLAG_BUFFER_VALID;
	}
	if (pBuffer->owned_by_firmware) {
		diag_query->ApplicationFlags |=
		    MPR_FW_DIAG_FLAG_FW_BUFFER_ACCESS;
	} else {
		diag_query->ApplicationFlags &=
		    ~MPR_FW_DIAG_FLAG_FW_BUFFER_ACCESS;
	}

	return (MPR_DIAG_SUCCESS);
}

static int
mpr_diag_read_buffer(struct mpr_softc *sc,
    mpr_diag_read_buffer_t *diag_read_buffer, uint8_t *ioctl_buf,
    uint32_t *return_code)
{
	mpr_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i, *pData;
	uint32_t			unique_id;
	int				status;

	unique_id = diag_read_buffer->UniqueId;

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should be there.
	 */
	i = mpr_get_fw_diag_buffer_number(sc, unique_id);
	if (i == MPR_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
		return (MPR_DIAG_FAILURE);
	}

	pBuffer = &sc->fw_diag_buffer_list[i];

	/*
	 * Make sure requested read is within limits
	 */
	if (diag_read_buffer->StartingOffset + diag_read_buffer->BytesToRead >
	    pBuffer->size) {
		*return_code = MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
		return (MPR_DIAG_FAILURE);
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
		return (MPR_DIAG_FAILURE);
	diag_read_buffer->Status = 0;

	/*
	 * Set or clear the Force Release flag.
	 */
	if (pBuffer->force_release) {
		diag_read_buffer->Flags |= MPR_FW_DIAG_FLAG_FORCE_RELEASE;
	} else {
		diag_read_buffer->Flags &= ~MPR_FW_DIAG_FLAG_FORCE_RELEASE;
	}

	/*
	 * If buffer is to be reregistered, make sure it's not already owned by
	 * firmware first.
	 */
	status = MPR_DIAG_SUCCESS;
	if (!pBuffer->owned_by_firmware) {
		if (diag_read_buffer->Flags & MPR_FW_DIAG_FLAG_REREGISTER) {
			status = mpr_post_fw_diag_buffer(sc, pBuffer,
			    return_code);
		}
	}

	return (status);
}

static int
mpr_diag_release(struct mpr_softc *sc, mpr_fw_diag_release_t *diag_release,
    uint32_t *return_code)
{
	mpr_fw_diagnostic_buffer_t	*pBuffer;
	uint8_t				i;
	uint32_t			unique_id;
	int				status;

	unique_id = diag_release->UniqueId;

	/*
	 * Get the current buffer and look up the unique ID.  The unique ID
	 * should be there.
	 */
	i = mpr_get_fw_diag_buffer_number(sc, unique_id);
	if (i == MPR_FW_DIAGNOSTIC_UID_NOT_FOUND) {
		*return_code = MPR_FW_DIAG_ERROR_INVALID_UID;
		return (MPR_DIAG_FAILURE);
	}

	pBuffer = &sc->fw_diag_buffer_list[i];

	/*
	 * If buffer is not owned by firmware, it's already been released.
	 */
	if (!pBuffer->owned_by_firmware) {
		*return_code = MPR_FW_DIAG_ERROR_ALREADY_RELEASED;
		return (MPR_DIAG_FAILURE);
	}

	/*
	 * Release the buffer.
	 */
	status = mpr_release_fw_diag_buffer(sc, pBuffer, return_code,
	    MPR_FW_DIAG_TYPE_RELEASE);
	return (status);
}

static int
mpr_do_diag_action(struct mpr_softc *sc, uint32_t action, uint8_t *diag_action,
    uint32_t length, uint32_t *return_code)
{
	mpr_fw_diag_register_t		diag_register;
	mpr_fw_diag_unregister_t	diag_unregister;
	mpr_fw_diag_query_t		diag_query;
	mpr_diag_read_buffer_t		diag_read_buffer;
	mpr_fw_diag_release_t		diag_release;
	int				status = MPR_DIAG_SUCCESS;
	uint32_t			original_return_code;

	original_return_code = *return_code;
	*return_code = MPR_FW_DIAG_ERROR_SUCCESS;

	switch (action) {
		case MPR_FW_DIAG_TYPE_REGISTER:
			if (!length) {
				*return_code =
				    MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPR_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_register,
			    sizeof(diag_register)) != 0)
				return (MPR_DIAG_FAILURE);
			status = mpr_diag_register(sc, &diag_register,
			    return_code);
			break;

		case MPR_FW_DIAG_TYPE_UNREGISTER:
			if (length < sizeof(diag_unregister)) {
				*return_code =
				    MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPR_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_unregister,
			    sizeof(diag_unregister)) != 0)
				return (MPR_DIAG_FAILURE);
			status = mpr_diag_unregister(sc, &diag_unregister,
			    return_code);
			break;

		case MPR_FW_DIAG_TYPE_QUERY:
			if (length < sizeof (diag_query)) {
				*return_code =
				    MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPR_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_query, sizeof(diag_query))
			    != 0)
				return (MPR_DIAG_FAILURE);
			status = mpr_diag_query(sc, &diag_query, return_code);
			if (status == MPR_DIAG_SUCCESS)
				if (copyout(&diag_query, diag_action,
				    sizeof (diag_query)) != 0)
					return (MPR_DIAG_FAILURE);
			break;

		case MPR_FW_DIAG_TYPE_READ_BUFFER:
			if (copyin(diag_action, &diag_read_buffer,
			    sizeof(diag_read_buffer)) != 0)
				return (MPR_DIAG_FAILURE);
			if (length < diag_read_buffer.BytesToRead) {
				*return_code =
				    MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPR_DIAG_FAILURE;
				break;
			}
			status = mpr_diag_read_buffer(sc, &diag_read_buffer,
			    PTRIN(diag_read_buffer.PtrDataBuffer),
			    return_code);
			if (status == MPR_DIAG_SUCCESS) {
				if (copyout(&diag_read_buffer, diag_action,
				    sizeof(diag_read_buffer) -
				    sizeof(diag_read_buffer.PtrDataBuffer)) !=
				    0)
					return (MPR_DIAG_FAILURE);
			}
			break;

		case MPR_FW_DIAG_TYPE_RELEASE:
			if (length < sizeof(diag_release)) {
				*return_code =
				    MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
				status = MPR_DIAG_FAILURE;
				break;
			}
			if (copyin(diag_action, &diag_release,
			    sizeof(diag_release)) != 0)
				return (MPR_DIAG_FAILURE);
			status = mpr_diag_release(sc, &diag_release,
			    return_code);
			break;

		default:
			*return_code = MPR_FW_DIAG_ERROR_INVALID_PARAMETER;
			status = MPR_DIAG_FAILURE;
			break;
	}

	if ((status == MPR_DIAG_FAILURE) &&
	    (original_return_code == MPR_FW_DIAG_NEW) &&
	    (*return_code != MPR_FW_DIAG_ERROR_SUCCESS))
		status = MPR_DIAG_SUCCESS;

	return (status);
}

static int
mpr_user_diag_action(struct mpr_softc *sc, mpr_diag_action_t *data)
{
	int			status;

	/*
	 * Only allow one diag action at one time.
	 */
	if (sc->mpr_flags & MPR_FLAGS_BUSY) {
		mpr_dprint(sc, MPR_USER, "%s: Only one FW diag command "
		    "allowed at a single time.", __func__);
		return (EBUSY);
	}
	sc->mpr_flags |= MPR_FLAGS_BUSY;

	/*
	 * Send diag action request
	 */
	if (data->Action == MPR_FW_DIAG_TYPE_REGISTER ||
	    data->Action == MPR_FW_DIAG_TYPE_UNREGISTER ||
	    data->Action == MPR_FW_DIAG_TYPE_QUERY ||
	    data->Action == MPR_FW_DIAG_TYPE_READ_BUFFER ||
	    data->Action == MPR_FW_DIAG_TYPE_RELEASE) {
		status = mpr_do_diag_action(sc, data->Action,
		    PTRIN(data->PtrDiagAction), data->Length,
		    &data->ReturnCode);
	} else
		status = EINVAL;

	sc->mpr_flags &= ~MPR_FLAGS_BUSY;
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
mpr_user_event_query(struct mpr_softc *sc, mpr_event_query_t *data)
{
	uint8_t	i;

	mpr_lock(sc);
	data->Entries = MPR_EVENT_QUEUE_SIZE;

	for (i = 0; i < 4; i++) {
		data->Types[i] = sc->events_to_record[i];
	}
	mpr_unlock(sc);
}

/*
 * Set the driver's event mask according to what's been given.  See
 * mpr_user_event_query for explanation of the event recording mask and the IOC
 * event mask.  It's the app's responsibility to enable event logging by setting
 * the bits in events_to_record.  Initially, no events will be logged.
 */
static void
mpr_user_event_enable(struct mpr_softc *sc, mpr_event_enable_t *data)
{
	uint8_t	i;

	mpr_lock(sc);
	for (i = 0; i < 4; i++) {
		sc->events_to_record[i] = data->Types[i];
	}
	mpr_unlock(sc);
}

/*
 * Copy out the events that have been recorded, up to the max events allowed.
 */
static int
mpr_user_event_report(struct mpr_softc *sc, mpr_event_report_t *data)
{
	int		status = 0;
	uint32_t	size;

	mpr_lock(sc);
	size = data->Size;
	if ((size >= sizeof(sc->recorded_events)) && (status == 0)) {
		mpr_unlock(sc);
		if (copyout((void *)sc->recorded_events,
		    PTRIN(data->PtrEvents), size) != 0)
			status = EFAULT;
		mpr_lock(sc);
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
	mpr_unlock(sc);

	return (status);
}

/*
 * Record events into the driver from the IOC if they are not masked.
 */
void
mprsas_record_event(struct mpr_softc *sc,
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
		bzero(sc->recorded_events[i].Data, MPR_MAX_EVENT_DATA_LENGTH *
		    4);
		event_data_len = event_reply->EventDataLength;

		if (event_data_len > 0) {
			/*
			 * Limit data to size in m_event entry
			 */
			if (event_data_len > MPR_MAX_EVENT_DATA_LENGTH) {
				event_data_len = MPR_MAX_EVENT_DATA_LENGTH;
			}
			for (j = 0; j < event_data_len; j++) {
				sc->recorded_events[i].Data[j] =
				    event_reply->EventData[j];
			}

			/*
			 * check for index wrap-around
			 */
			if (++i == MPR_EVENT_QUEUE_SIZE) {
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
mpr_user_reg_access(struct mpr_softc *sc, mpr_reg_access_t *data)
{
	int	status = 0;

	switch (data->Command) {
		/*
		 * IO access is not supported.
		 */
		case REG_IO_READ:
		case REG_IO_WRITE:
			mpr_dprint(sc, MPR_USER, "IO access is not supported. "
			    "Use memory access.");
			status = EINVAL;
			break;

		case REG_MEM_READ:
			data->RegData = mpr_regread(sc, data->RegOffset);
			break;

		case REG_MEM_WRITE:
			mpr_regwrite(sc, data->RegOffset, data->RegData);
			break;

		default:
			status = EINVAL;
			break;
	}

	return (status);
}

static int
mpr_user_btdh(struct mpr_softc *sc, mpr_btdh_mapping_t *data)
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
			mpr_dprint(sc, MPR_XINFO, "Target ID is out of range "
			   "for Bus/Target to DevHandle mapping.");
			return (EINVAL);
		}
		dev_handle = sc->mapping_table[target].dev_handle;
		if (dev_handle)
			data->DevHandle = dev_handle;
	} else {
		bus = 0;
		target = mpr_mapping_get_tid_from_handle(sc, dev_handle);
		data->Bus = bus;
		data->TargetID = target;
	}

	return (0);
}

static int
mpr_ioctl(struct cdev *dev, u_long cmd, void *arg, int flag,
    struct thread *td)
{
	struct mpr_softc *sc;
	struct mpr_cfg_page_req *page_req;
	struct mpr_ext_cfg_page_req *ext_page_req;
	void *mpr_page;
	int error, msleep_ret;

	mpr_page = NULL;
	sc = dev->si_drv1;
	page_req = (void *)arg;
	ext_page_req = (void *)arg;

	switch (cmd) {
	case MPRIO_READ_CFG_HEADER:
		mpr_lock(sc);
		error = mpr_user_read_cfg_header(sc, page_req);
		mpr_unlock(sc);
		break;
	case MPRIO_READ_CFG_PAGE:
		mpr_page = malloc(page_req->len, M_MPRUSER, M_WAITOK | M_ZERO);
		error = copyin(page_req->buf, mpr_page,
		    sizeof(MPI2_CONFIG_PAGE_HEADER));
		if (error)
			break;
		mpr_lock(sc);
		error = mpr_user_read_cfg_page(sc, page_req, mpr_page);
		mpr_unlock(sc);
		if (error)
			break;
		error = copyout(mpr_page, page_req->buf, page_req->len);
		break;
	case MPRIO_READ_EXT_CFG_HEADER:
		mpr_lock(sc);
		error = mpr_user_read_extcfg_header(sc, ext_page_req);
		mpr_unlock(sc);
		break;
	case MPRIO_READ_EXT_CFG_PAGE:
		mpr_page = malloc(ext_page_req->len, M_MPRUSER,
		    M_WAITOK | M_ZERO);
		error = copyin(ext_page_req->buf, mpr_page,
		    sizeof(MPI2_CONFIG_EXTENDED_PAGE_HEADER));
		if (error)
			break;
		mpr_lock(sc);
		error = mpr_user_read_extcfg_page(sc, ext_page_req, mpr_page);
		mpr_unlock(sc);
		if (error)
			break;
		error = copyout(mpr_page, ext_page_req->buf, ext_page_req->len);
		break;
	case MPRIO_WRITE_CFG_PAGE:
		mpr_page = malloc(page_req->len, M_MPRUSER, M_WAITOK|M_ZERO);
		error = copyin(page_req->buf, mpr_page, page_req->len);
		if (error)
			break;
		mpr_lock(sc);
		error = mpr_user_write_cfg_page(sc, page_req, mpr_page);
		mpr_unlock(sc);
		break;
	case MPRIO_MPR_COMMAND:
		error = mpr_user_command(sc, (struct mpr_usr_command *)arg);
		break;
	case MPTIOCTL_PASS_THRU:
		/*
		 * The user has requested to pass through a command to be
		 * executed by the MPT firmware.  Call our routine which does
		 * this.  Only allow one passthru IOCTL at one time.
		 */
		error = mpr_user_pass_thru(sc, (mpr_pass_thru_t *)arg);
		break;
	case MPTIOCTL_GET_ADAPTER_DATA:
		/*
		 * The user has requested to read adapter data.  Call our
		 * routine which does this.
		 */
		error = 0;
		mpr_user_get_adapter_data(sc, (mpr_adapter_data_t *)arg);
		break;
	case MPTIOCTL_GET_PCI_INFO:
		/*
		 * The user has requested to read pci info.  Call
		 * our routine which does this.
		 */
		mpr_lock(sc);
		error = 0;
		mpr_user_read_pci_info(sc, (mpr_pci_info_t *)arg);
		mpr_unlock(sc);
		break;
	case MPTIOCTL_RESET_ADAPTER:
		mpr_lock(sc);
		sc->port_enable_complete = 0;
		uint32_t reinit_start = time_uptime;
		error = mpr_reinit(sc);
		/* Sleep for 300 second. */
		msleep_ret = msleep(&sc->port_enable_complete, &sc->mpr_mtx,
		    PRIBIO, "mpr_porten", 300 * hz);
		mpr_unlock(sc);
		if (msleep_ret)
			printf("Port Enable did not complete after Diag "
			    "Reset msleep error %d.\n", msleep_ret);
		else
			mpr_dprint(sc, MPR_USER, "Hard Reset with Port Enable "
			    "completed in %d seconds.\n",
			    (uint32_t)(time_uptime - reinit_start));
		break;
	case MPTIOCTL_DIAG_ACTION:
		/*
		 * The user has done a diag buffer action.  Call our routine
		 * which does this.  Only allow one diag action at one time.
		 */
		mpr_lock(sc);
		error = mpr_user_diag_action(sc, (mpr_diag_action_t *)arg);
		mpr_unlock(sc);
		break;
	case MPTIOCTL_EVENT_QUERY:
		/*
		 * The user has done an event query. Call our routine which does
		 * this.
		 */
		error = 0;
		mpr_user_event_query(sc, (mpr_event_query_t *)arg);
		break;
	case MPTIOCTL_EVENT_ENABLE:
		/*
		 * The user has done an event enable. Call our routine which
		 * does this.
		 */
		error = 0;
		mpr_user_event_enable(sc, (mpr_event_enable_t *)arg);
		break;
	case MPTIOCTL_EVENT_REPORT:
		/*
		 * The user has done an event report. Call our routine which
		 * does this.
		 */
		error = mpr_user_event_report(sc, (mpr_event_report_t *)arg);
		break;
	case MPTIOCTL_REG_ACCESS:
		/*
		 * The user has requested register access.  Call our routine
		 * which does this.
		 */
		mpr_lock(sc);
		error = mpr_user_reg_access(sc, (mpr_reg_access_t *)arg);
		mpr_unlock(sc);
		break;
	case MPTIOCTL_BTDH_MAPPING:
		/*
		 * The user has requested to translate a bus/target to a
		 * DevHandle or a DevHandle to a bus/target.  Call our routine
		 * which does this.
		 */
		error = mpr_user_btdh(sc, (mpr_btdh_mapping_t *)arg);
		break;
	default:
		error = ENOIOCTL;
		break;
	}

	if (mpr_page != NULL)
		free(mpr_page, M_MPRUSER);

	return (error);
}

#ifdef COMPAT_FREEBSD32

struct mpr_cfg_page_req32 {
	MPI2_CONFIG_PAGE_HEADER header;
	uint32_t page_address;
	uint32_t buf;
	int	len;	
	uint16_t ioc_status;
};

struct mpr_ext_cfg_page_req32 {
	MPI2_CONFIG_EXTENDED_PAGE_HEADER header;
	uint32_t page_address;
	uint32_t buf;
	int	len;
	uint16_t ioc_status;
};

struct mpr_raid_action32 {
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

struct mpr_usr_command32 {
	uint32_t req;
	uint32_t req_len;
	uint32_t rpl;
	uint32_t rpl_len;
	uint32_t buf;
	int len;
	uint32_t flags;
};

#define	MPRIO_READ_CFG_HEADER32	_IOWR('M', 200, struct mpr_cfg_page_req32)
#define	MPRIO_READ_CFG_PAGE32	_IOWR('M', 201, struct mpr_cfg_page_req32)
#define	MPRIO_READ_EXT_CFG_HEADER32 _IOWR('M', 202, struct mpr_ext_cfg_page_req32)
#define	MPRIO_READ_EXT_CFG_PAGE32 _IOWR('M', 203, struct mpr_ext_cfg_page_req32)
#define	MPRIO_WRITE_CFG_PAGE32	_IOWR('M', 204, struct mpr_cfg_page_req32)
#define	MPRIO_RAID_ACTION32	_IOWR('M', 205, struct mpr_raid_action32)
#define	MPRIO_MPR_COMMAND32	_IOWR('M', 210, struct mpr_usr_command32)

static int
mpr_ioctl32(struct cdev *dev, u_long cmd32, void *_arg, int flag,
    struct thread *td)
{
	struct mpr_cfg_page_req32 *page32 = _arg;
	struct mpr_ext_cfg_page_req32 *ext32 = _arg;
	struct mpr_raid_action32 *raid32 = _arg;
	struct mpr_usr_command32 *user32 = _arg;
	union {
		struct mpr_cfg_page_req page;
		struct mpr_ext_cfg_page_req ext;
		struct mpr_raid_action raid;
		struct mpr_usr_command user;
	} arg;
	u_long cmd;
	int error;

	switch (cmd32) {
	case MPRIO_READ_CFG_HEADER32:
	case MPRIO_READ_CFG_PAGE32:
	case MPRIO_WRITE_CFG_PAGE32:
		if (cmd32 == MPRIO_READ_CFG_HEADER32)
			cmd = MPRIO_READ_CFG_HEADER;
		else if (cmd32 == MPRIO_READ_CFG_PAGE32)
			cmd = MPRIO_READ_CFG_PAGE;
		else
			cmd = MPRIO_WRITE_CFG_PAGE;
		CP(*page32, arg.page, header);
		CP(*page32, arg.page, page_address);
		PTRIN_CP(*page32, arg.page, buf);
		CP(*page32, arg.page, len);
		CP(*page32, arg.page, ioc_status);
		break;

	case MPRIO_READ_EXT_CFG_HEADER32:
	case MPRIO_READ_EXT_CFG_PAGE32:
		if (cmd32 == MPRIO_READ_EXT_CFG_HEADER32)
			cmd = MPRIO_READ_EXT_CFG_HEADER;
		else
			cmd = MPRIO_READ_EXT_CFG_PAGE;
		CP(*ext32, arg.ext, header);
		CP(*ext32, arg.ext, page_address);
		PTRIN_CP(*ext32, arg.ext, buf);
		CP(*ext32, arg.ext, len);
		CP(*ext32, arg.ext, ioc_status);
		break;

	case MPRIO_RAID_ACTION32:
		cmd = MPRIO_RAID_ACTION;
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

	case MPRIO_MPR_COMMAND32:
		cmd = MPRIO_MPR_COMMAND;
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

	error = mpr_ioctl(dev, cmd, &arg, flag, td);
	if (error == 0 && (cmd32 & IOC_OUT) != 0) {
		switch (cmd32) {
		case MPRIO_READ_CFG_HEADER32:
		case MPRIO_READ_CFG_PAGE32:
		case MPRIO_WRITE_CFG_PAGE32:
			CP(arg.page, *page32, header);
			CP(arg.page, *page32, page_address);
			PTROUT_CP(arg.page, *page32, buf);
			CP(arg.page, *page32, len);
			CP(arg.page, *page32, ioc_status);
			break;

		case MPRIO_READ_EXT_CFG_HEADER32:
		case MPRIO_READ_EXT_CFG_PAGE32:
			CP(arg.ext, *ext32, header);
			CP(arg.ext, *ext32, page_address);
			PTROUT_CP(arg.ext, *ext32, buf);
			CP(arg.ext, *ext32, len);
			CP(arg.ext, *ext32, ioc_status);
			break;

		case MPRIO_RAID_ACTION32:
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

		case MPRIO_MPR_COMMAND32:
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
mpr_ioctl_devsw(struct cdev *dev, u_long com, caddr_t arg, int flag,
    struct thread *td)
{
#ifdef COMPAT_FREEBSD32
	if (SV_CURPROC_FLAG(SV_ILP32))
		return (mpr_ioctl32(dev, com, arg, flag, td));
#endif
	return (mpr_ioctl(dev, com, arg, flag, td));
}
