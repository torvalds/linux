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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* TODO Move headers to mprvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/sysctl.h>
#include <sys/eventhandler.h>
#include <sys/uio.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <dev/mpr/mpi/mpi2_type.h>
#include <dev/mpr/mpi/mpi2.h>
#include <dev/mpr/mpi/mpi2_ioc.h>
#include <dev/mpr/mpi/mpi2_sas.h>
#include <dev/mpr/mpi/mpi2_pci.h>
#include <dev/mpr/mpi/mpi2_cnfg.h>
#include <dev/mpr/mpi/mpi2_init.h>
#include <dev/mpr/mpi/mpi2_tool.h>
#include <dev/mpr/mpr_ioctl.h>
#include <dev/mpr/mprvar.h>

/**
 * mpr_config_get_ioc_pg8 - obtain ioc page 8
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_ioc_pg8(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2IOCPage8_t *config_page)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	MPI2_CONFIG_PAGE_IOC_8 *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_IOC;
	request->Header.PageNumber = 8;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);
	
	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_IOC;
	request->Header.PageNumber = 8;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length =  le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc((cm->cm_length), M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length, (sizeof(Mpi2IOCPage8_t))));

out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_iounit_pg8 - obtain iounit page 8
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_iounit_pg8(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2IOUnitPage8_t *config_page)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	MPI2_CONFIG_PAGE_IO_UNIT_8 *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	request->Header.PageNumber = 8;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);
	
	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_IO_UNIT;
	request->Header.PageNumber = 8;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length =  le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc((cm->cm_length), M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length,
	    (sizeof(Mpi2IOUnitPage8_t))));

out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_man_pg11 - obtain manufacturing page 11
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_man_pg11(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2ManufacturingPage11_t *config_page)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	MPI2_CONFIG_PAGE_MAN_11 *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	request->Header.PageNumber = 11;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);
	
	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_MANUFACTURING;
	request->Header.PageNumber = 11;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length =  le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc((cm->cm_length), M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length,
	    (sizeof(Mpi2ManufacturingPage11_t))));

out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_base_static_config_pages - static start of day config pages.
 * @sc: per adapter object
 *
 * Return nothing.
 */
void
mpr_base_static_config_pages(struct mpr_softc *sc)
{
	Mpi2ConfigReply_t		mpi_reply;
	Mpi2ManufacturingPage11_t	man_pg11;
	int				retry, rc;

	retry = 0;
	while (mpr_config_get_ioc_pg8(sc, &mpi_reply, &sc->ioc_pg8)) {
		retry++;
		if (retry > 5) {
			/* We need to Handle this situation */
			/*FIXME*/
			break;
		}
	}
	retry = 0;
	while (mpr_config_get_iounit_pg8(sc, &mpi_reply, &sc->iounit_pg8)) {
		retry++;
		if (retry > 5) {
			/* We need to Handle this situation */
			/*FIXME*/
			break;
		}
	}
	retry = 0;
	while ((rc = mpr_config_get_man_pg11(sc, &mpi_reply, &man_pg11))) {
		retry++;
		if (retry > 5) {
			/* We need to Handle this situation */
			/*FIXME*/
			break;
		}
	}
	
	if (!rc) {
		sc->custom_nvme_tm_handling = (le16toh(man_pg11.AddlFlags2) &
		    MPI2_MAN_PG11_ADDLFLAGS2_CUSTOM_TM_HANDLING_MASK);
		sc->nvme_abort_timeout = man_pg11.NVMeAbortTO;

		/* Minimum NVMe Abort timeout value should be 6 seconds &
		 * maximum value should be 60 seconds.
		 */
		if (sc->nvme_abort_timeout < 6)
			sc->nvme_abort_timeout = 6;
		if (sc->nvme_abort_timeout > 60)
			sc->nvme_abort_timeout = 60;
	}
}

/**
 * mpr_config_get_dpm_pg0 - obtain driver persistent mapping page0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_dpm_pg0(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2DriverMappingPage0_t *config_page, u16 sz)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	Mpi2DriverMappingPage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	memset(config_page, 0, sz);
	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING;
	request->Header.PageNumber = 0;
	request->ExtPageLength = request->Header.PageVersion = 0;
	request->PageAddress = sc->max_dpm_entries <<
	    MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_NVRAM;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING;
	request->Header.PageNumber = 0;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->PageAddress = sc->max_dpm_entries <<
	    MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	cm->cm_length =  le16toh(request->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO|M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length, sz));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_set_dpm_pg0 - write an entry in driver persistent mapping page0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @entry_idx: entry index in DPM Page0 to be modified
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */

int mpr_config_set_dpm_pg0(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2DriverMappingPage0_t *config_page, u16 entry_idx)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	MPI2_CONFIG_PAGE_DRIVER_MAPPING_0 *page = NULL;	
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING;
	request->Header.PageNumber = 0;
	request->ExtPageLength = request->Header.PageVersion = 0;
	/* We can remove below two lines ????*/
	request->PageAddress = 1 << MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	request->PageAddress |= htole16(entry_idx);
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */	
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_WRITE_NVRAM;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_DRIVER_MAPPING;
	request->Header.PageNumber = 0;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	request->PageAddress = 1 << MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	request->PageAddress |= htole16(entry_idx);
	cm->cm_length = le16toh(mpi_reply->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAOUT;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	bcopy(config_page, page, MIN(cm->cm_length, 
	    (sizeof(Mpi2DriverMappingPage0_t))));
	cm->cm_data = page;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request to write page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page written with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_sas_device_pg0 - obtain sas device page 0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_sas_device_pg0(struct mpr_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasDevicePage0_t *config_page, u32 form, u16 handle)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	Mpi2SasDevicePage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE;
	request->Header.PageNumber = 0;
	request->ExtPageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_SAS_DEVICE;
	request->Header.PageNumber = 0;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	request->PageAddress = htole32(form | handle);
	cm->cm_length = le16toh(mpi_reply->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length, 
	    sizeof(Mpi2SasDevicePage0_t)));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_pcie_device_pg0 - obtain PCIe device page 0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_pcie_device_pg0(struct mpr_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi26PCIeDevicePage0_t *config_page, u32 form, u16 handle)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	Mpi26PCIeDevicePage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE;
	request->Header.PageNumber = 0;
	request->ExtPageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE;
	request->Header.PageNumber = 0;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	request->PageAddress = htole32(form | handle);
	cm->cm_length = le16toh(mpi_reply->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length, 
	    sizeof(Mpi26PCIeDevicePage0_t)));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_pcie_device_pg2 - obtain PCIe device page 2
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: device handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_pcie_device_pg2(struct mpr_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi26PCIeDevicePage2_t *config_page, u32 form, u16 handle)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	Mpi26PCIeDevicePage2_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE;
	request->Header.PageNumber = 2;
	request->ExtPageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	request->ExtPageType = MPI2_CONFIG_EXTPAGETYPE_PCIE_DEVICE;
	request->Header.PageNumber = 2;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	request->PageAddress = htole32(form | handle);
	cm->cm_length = le16toh(mpi_reply->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length, 
	    sizeof(Mpi26PCIeDevicePage2_t)));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_bios_pg3 - obtain BIOS page 3
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_bios_pg3(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2BiosPage3_t *config_page)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	Mpi2BiosPage3_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_BIOS;
	request->Header.PageNumber = 3;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_BIOS;
	request->Header.PageNumber = 3;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length = le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length, sizeof(Mpi2BiosPage3_t)));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_raid_volume_pg0 - obtain raid volume page 0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @page_address: form and handle value used to get page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_raid_volume_pg0(struct mpr_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 page_address)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mpr_command *cm;
	Mpi2RaidVolPage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	request->Header.PageNumber = 0;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */
	error = mpr_request_polled(sc, &cm);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: poll for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	request->Header.PageNumber = 0;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->PageAddress = page_address;
	cm->cm_length = le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */
	error = mpr_request_polled(sc, &cm);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: poll for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, cm->cm_length);
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_raid_volume_pg1 - obtain raid volume page 1
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @form: GET_NEXT_HANDLE or HANDLE
 * @handle: volume handle
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_raid_volume_pg1(struct mpr_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form, u16 handle)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply;
	struct mpr_command *cm;
	Mpi2RaidVolPage1_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	request->Header.PageNumber = 1;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_VOLUME;
	request->Header.PageNumber = 1;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->PageAddress = htole32(form | handle);
	cm->cm_length = le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mpr_wait_command(sc, &cm, 60, CAN_SLEEP);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: request for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/*
		 * If the request returns an error then we need to do a diag
		 * reset
		 */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length,
	    sizeof(Mpi2RaidVolPage1_t)));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}

/**
 * mpr_config_get_volume_wwid - returns wwid given the volume handle
 * @sc: per adapter object
 * @volume_handle: volume handle
 * @wwid: volume wwid
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_volume_wwid(struct mpr_softc *sc, u16 volume_handle, u64 *wwid)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2RaidVolPage1_t raid_vol_pg1;

	*wwid = 0;
	if (!(mpr_config_get_raid_volume_pg1(sc, &mpi_reply, &raid_vol_pg1,
	    MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, volume_handle))) {
		*wwid = le64toh((u64)raid_vol_pg1.WWID.High << 32 |
		    raid_vol_pg1.WWID.Low);
		return 0;
	} else
		return -1;
}

/**
 * mpr_config_get_pd_pg0 - obtain raid phys disk page 0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @page_address: form and handle value used to get page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpr_config_get_raid_pd_pg0(struct mpr_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2RaidPhysDiskPage0_t *config_page, u32 page_address)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mpr_command *cm;
	Mpi2RaidPhysDiskPage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_PHYSDISK;
	request->Header.PageNumber = 0;
	request->Header.PageLength = request->Header.PageVersion = 0;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */
	error = mpr_request_polled(sc, &cm);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: poll for header completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: header read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	/* We have to do free and alloc for the reply-free and reply-post
	 * counters to match - Need to review the reply FIFO handling.
	 */
	mpr_free_command(sc, cm);

	if ((cm = mpr_alloc_command(sc)) == NULL) {
		printf("%s: command alloc failed @ line %d\n", __func__,
		    __LINE__);
		error = EBUSY;
		goto out;
	}
	request = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	bzero(request, sizeof(MPI2_CONFIG_REQUEST));
	request->Function = MPI2_FUNCTION_CONFIG;
	request->Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	request->Header.PageType = MPI2_CONFIG_PAGETYPE_RAID_PHYSDISK;
	request->Header.PageNumber = 0;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	request->Header.PageVersion = mpi_reply->Header.PageVersion;
	request->PageAddress = page_address;
	cm->cm_length = le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPR, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */
	error = mpr_request_polled(sc, &cm);
	if (cm != NULL)
		reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (error || (reply == NULL)) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: poll for page completed with error %d",
		    __func__, error);
		error = ENXIO;
		goto out;
	}
	ioc_status = le16toh(reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	bcopy(reply, mpi_reply, sizeof(MPI2_CONFIG_REPLY));
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		/* FIXME */
		/* If the poll returns error then we need to do diag reset */ 
		printf("%s: page read with error; iocstatus = 0x%x\n",
		    __func__, ioc_status);
		error = ENXIO;
		goto out;
	}
	bcopy(page, config_page, MIN(cm->cm_length,
	    sizeof(Mpi2RaidPhysDiskPage0_t)));
out:
	free(page, M_MPR);
	if (cm)
		mpr_free_command(sc, cm);
	return (error);
}
