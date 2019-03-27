/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* TODO Move headers to mpsvar */
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
#include <dev/mps/mpi/mpi2_type.h>
#include <dev/mps/mpi/mpi2.h>
#include <dev/mps/mpi/mpi2_ioc.h>
#include <dev/mps/mpi/mpi2_sas.h>
#include <dev/mps/mpi/mpi2_cnfg.h>
#include <dev/mps/mpi/mpi2_init.h>
#include <dev/mps/mpi/mpi2_tool.h>
#include <dev/mps/mps_ioctl.h>
#include <dev/mps/mpsvar.h>

/**
 * mps_config_get_ioc_pg8 - obtain ioc page 8
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_ioc_pg8(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2IOCPage8_t *config_page)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	MPI2_CONFIG_PAGE_IOC_8 *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_IOCPAGE8_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	mps_free_command(sc, cm);
	
	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_IOCPAGE8_PAGEVERSION;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length =  le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc((cm->cm_length), M_MPT2, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_get_man_pg10 - obtain Manufacturing Page 10 data and set flags
 *   accordingly.  Currently, this page does not need to return to caller.
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_man_pg10(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	pMpi2ManufacturingPagePS_t page = NULL;
	uint32_t *pPS_info;
	uint8_t OEM_Value = 0;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageNumber = 10;
	request->Header.PageVersion = MPI2_MANUFACTURING10_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */  
	error = mps_wait_command(sc, &cm, 60, 0);
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
	mps_free_command(sc, cm);
	
	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageNumber = 10;
	request->Header.PageVersion = MPI2_MANUFACTURING10_PAGEVERSION;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length =  le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(MPS_MAN_PAGE10_SIZE, M_MPT2, M_ZERO | M_NOWAIT);
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
	error = mps_wait_command(sc, &cm, 60, 0);
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

	/*
	 * If OEM ID is unknown, fail the request.
	 */
	sc->WD_hide_expose = MPS_WD_HIDE_ALWAYS;
	OEM_Value = (uint8_t)(page->ProductSpecificInfo & 0x000000FF);
	if (OEM_Value != MPS_WD_LSI_OEM) {
		mps_dprint(sc, MPS_FAULT, "Unknown OEM value for WarpDrive "
		    "(0x%x)\n", OEM_Value);
		error = ENXIO;
		goto out;
	}

	/*
	 * Set the phys disks hide/expose value.
	 */
	pPS_info = &page->ProductSpecificInfo;
	sc->WD_hide_expose = (uint8_t)(pPS_info[5]);
	sc->WD_hide_expose &= MPS_WD_HIDE_EXPOSE_MASK;
	if ((sc->WD_hide_expose != MPS_WD_HIDE_ALWAYS) &&
	    (sc->WD_hide_expose != MPS_WD_EXPOSE_ALWAYS) &&
	    (sc->WD_hide_expose != MPS_WD_HIDE_IF_VOLUME)) {
		mps_dprint(sc, MPS_FAULT, "Unknown value for WarpDrive "
		    "hide/expose: 0x%x\n", sc->WD_hide_expose);
		error = ENXIO;
		goto out;
	}

out:
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_base_static_config_pages - static start of day config pages.
 * @sc: per adapter object
 *
 * Return nothing.
 */
void
mps_base_static_config_pages(struct mps_softc *sc)
{
	Mpi2ConfigReply_t	mpi_reply;
	int			retry;

	retry = 0;
	while (mps_config_get_ioc_pg8(sc, &mpi_reply, &sc->ioc_pg8)) {
		retry++;
		if (retry > 5) {
			/* We need to Handle this situation */
			/*FIXME*/
			break;
		}
	}
}

/**
 * mps_wd_config_pages - get info required to support WarpDrive.  This needs to
 *    be called after discovery is complete to guarantee that IR info is there.
 * @sc: per adapter object
 *
 * Return nothing.
 */
void
mps_wd_config_pages(struct mps_softc *sc)
{
	Mpi2ConfigReply_t	mpi_reply;
	pMpi2RaidVolPage0_t	raid_vol_pg0 = NULL;
	Mpi2RaidPhysDiskPage0_t	phys_disk_pg0;
	pMpi2RaidVol0PhysDisk_t	pRVPD;
	uint32_t		stripe_size, phys_disk_page_address;
	uint16_t		block_size;
	uint8_t			index, stripe_exp = 0, block_exp = 0;

	/*
	 * Get the WD settings from manufacturing page 10 if using a WD HBA.
	 * This will be used to determine if phys disks should always be
	 * hidden, hidden only if part of a WD volume, or never hidden.  Also,
	 * get the WD RAID Volume info and fail if volume does not exist or if
	 * volume does not meet the requirements for a WD volume.  No retry
	 * here.  Just default to HIDE ALWAYS if man Page10 fails, or clear WD
	 * Valid flag if Volume info fails.
	 */
	sc->WD_valid_config = FALSE;
	if (sc->mps_flags & MPS_FLAGS_WD_AVAILABLE) {
		if (mps_config_get_man_pg10(sc, &mpi_reply)) {
			mps_dprint(sc, MPS_FAULT,
			    "mps_config_get_man_pg10 failed! Using 0 (Hide "
			    "Always) for WarpDrive hide/expose value.\n");
			sc->WD_hide_expose = MPS_WD_HIDE_ALWAYS;
		}

		/*
		 * Get first RAID Volume Page0 using GET_NEXT_HANDLE.
		 */
		raid_vol_pg0 = malloc(sizeof(Mpi2RaidVolPage0_t) +
		    (sizeof(Mpi2RaidVol0PhysDisk_t) * MPS_MAX_DISKS_IN_VOL),
		    M_MPT2, M_ZERO | M_NOWAIT);
		if (!raid_vol_pg0) {
			printf("%s: page alloc failed\n", __func__);
			goto out;
		}

		if (mps_config_get_raid_volume_pg0(sc, &mpi_reply, raid_vol_pg0,
		    0x0000FFFF)) {
			mps_dprint(sc, MPS_INFO,
			    "mps_config_get_raid_volume_pg0 failed! Assuming "
			    "WarpDrive IT mode.\n");
			goto out;
		}

		/*
		 * Check for valid WD configuration:
		 *   volume type is RAID0
		 *   number of phys disks in the volume is no more than 8
		 */
		if ((raid_vol_pg0->VolumeType != MPI2_RAID_VOL_TYPE_RAID0) ||
		    (raid_vol_pg0->NumPhysDisks > 8)) {
			mps_dprint(sc, MPS_FAULT,
			    "Invalid WarpDrive configuration. Direct Drive I/O "
			    "will not be used.\n");
			goto out;
		}

		/*
		 * Save the WD RAID data to be used during WD I/O.
		 */
		sc->DD_max_lba = le64toh((uint64_t)raid_vol_pg0->MaxLBA.High <<
		    32 | (uint64_t)raid_vol_pg0->MaxLBA.Low);
		sc->DD_num_phys_disks = raid_vol_pg0->NumPhysDisks;
		sc->DD_dev_handle = raid_vol_pg0->DevHandle;
		sc->DD_stripe_size = raid_vol_pg0->StripeSize;
		sc->DD_block_size = raid_vol_pg0->BlockSize;

		/*
		 * Find power of 2 of stripe size and set this as the exponent.
		 * Fail if stripe size is 0.
		 */
		stripe_size = raid_vol_pg0->StripeSize;
		for (index = 0; index < 32; index++) {
			if (stripe_size & 1)
				break;
			stripe_exp++;
			stripe_size >>= 1;
		}
		if (index == 32) {
			mps_dprint(sc, MPS_FAULT,
			    "RAID Volume's stripe size is 0. Direct Drive I/O "
			    "will not be used.\n");
			goto out;
		}
		sc->DD_stripe_exponent = stripe_exp;

		/*
		 * Find power of 2 of block size and set this as the exponent.
		 * Fail if block size is 0.
		 */
		block_size = raid_vol_pg0->BlockSize;
		for (index = 0; index < 16; index++) {
			if (block_size & 1)
				break;
			block_exp++;
			block_size >>= 1;
		}
		if (index == 16) {
			mps_dprint(sc, MPS_FAULT,
			    "RAID Volume's block size is 0. Direct Drive I/O "
			    "will not be used.\n");
			goto out;
		}
		sc->DD_block_exponent = block_exp;

		/*
		 * Loop through all of the volume's Phys Disks to map the phys
		 * disk number into the columm map.  This is used during Direct
		 * Drive I/O to send the request to the correct SSD.
		 */
		pRVPD = (pMpi2RaidVol0PhysDisk_t)&raid_vol_pg0->PhysDisk;
		for (index = 0; index < raid_vol_pg0->NumPhysDisks; index++) {
			sc->DD_column_map[pRVPD->PhysDiskMap].phys_disk_num =
			    pRVPD->PhysDiskNum;
			pRVPD++;
		}

		/*
		 * Get second RAID Volume Page0 using previous handle.  This
		 * page should not exist.  If it does, must not proceed with WD
		 * handling.
		 */
		if (mps_config_get_raid_volume_pg0(sc, &mpi_reply,
		    raid_vol_pg0, (u32)raid_vol_pg0->DevHandle)) {
			if ((le16toh(mpi_reply.IOCStatus) &
			    MPI2_IOCSTATUS_MASK) !=
			    MPI2_IOCSTATUS_CONFIG_INVALID_PAGE) {
				mps_dprint(sc, MPS_FAULT,
				    "Multiple RAID Volume Page0! Direct Drive "
				    "I/O will not be used.\n");
				goto out;
			}
		} else {
			mps_dprint(sc, MPS_FAULT,
			    "Multiple volumes! Direct Drive I/O will not be "
			    "used.\n");
			goto out;
		}

		/*
		 * Get RAID Volume Phys Disk Page 0 for all SSDs in the volume.
		 */
		for (index = 0; index < raid_vol_pg0->NumPhysDisks; index++) {
			phys_disk_page_address =
			    MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM +
			    sc->DD_column_map[index].phys_disk_num;
			if (mps_config_get_raid_pd_pg0(sc, &mpi_reply,
			    &phys_disk_pg0, phys_disk_page_address)) {
				mps_dprint(sc, MPS_FAULT,
				    "mps_config_get_raid_pd_pg0 failed! Direct "
				    "Drive I/O will not be used.\n");
				goto out;
			}
			if (phys_disk_pg0.DevHandle == 0xFFFF) {
				mps_dprint(sc, MPS_FAULT,
				    "Invalid Phys Disk DevHandle! Direct Drive "
				    "I/O will not be used.\n");
				goto out;
			}
			sc->DD_column_map[index].dev_handle =
			    phys_disk_pg0.DevHandle;
		}
		sc->WD_valid_config = TRUE;
out:
		if (raid_vol_pg0)
			free(raid_vol_pg0, M_MPT2);
	}
}

/**
 * mps_config_get_dpm_pg0 - obtain driver persistent mapping page0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @sz: size of buffer passed in config_page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_dpm_pg0(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2DriverMappingPage0_t *config_page, u16 sz)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	Mpi2DriverMappingPage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	memset(config_page, 0, sz);
	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_DRIVERMAPPING0_PAGEVERSION;
	request->PageAddress = sc->max_dpm_entries <<
	    MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_DRIVERMAPPING0_PAGEVERSION;
	request->PageAddress = sc->max_dpm_entries <<
	    MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	cm->cm_length =  le16toh(request->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO|M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_set_dpm_pg0 - write an entry in driver persistent mapping page0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @entry_idx: entry index in DPM Page0 to be modified
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */

int mps_config_set_dpm_pg0(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2DriverMappingPage0_t *config_page, u16 entry_idx)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	MPI2_CONFIG_PAGE_DRIVER_MAPPING_0 *page = NULL;	
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_DRIVERMAPPING0_PAGEVERSION;
	/* We can remove below two lines ????*/
	request->PageAddress = 1 << MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	request->PageAddress |= htole16(entry_idx);
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_DRIVERMAPPING0_PAGEVERSION;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	request->PageAddress = 1 << MPI2_DPM_PGAD_ENTRY_COUNT_SHIFT;
	request->PageAddress |= htole16(entry_idx);
	cm->cm_length = le16toh(mpi_reply->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAOUT;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	bcopy(config_page, page, MIN(cm->cm_length, 
	    (sizeof(Mpi2DriverMappingPage0_t))));
	cm->cm_data = page;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_get_sas_device_pg0 - obtain sas device page 0
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
mps_config_get_sas_device_pg0(struct mps_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2SasDevicePage0_t *config_page, u32 form, u16 handle)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	Mpi2SasDevicePage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_SASDEVICE0_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_SASDEVICE0_PAGEVERSION;
	request->ExtPageLength = mpi_reply->ExtPageLength;
	request->PageAddress = htole32(form | handle);
	cm->cm_length = le16toh(mpi_reply->ExtPageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_get_bios_pg3 - obtain BIOS page 3
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_bios_pg3(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2BiosPage3_t *config_page)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	Mpi2BiosPage3_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_BIOSPAGE3_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_BIOSPAGE3_PAGEVERSION;
	request->Header.PageLength = mpi_reply->Header.PageLength;
	cm->cm_length = le16toh(mpi_reply->Header.PageLength) * 4;
	cm->cm_sge = &request->PageBufferSGE;
	cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_get_raid_volume_pg0 - obtain raid volume page 0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @page_address: form and handle value used to get page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_raid_volume_pg0(struct mps_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 page_address)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	Mpi2RaidVolPage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_RAIDVOLPAGE0_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */  
	error = mps_wait_command(sc, &cm, 60, 0);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO | M_NOWAIT);
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
	error = mps_wait_command(sc, &cm, 60, 0);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_get_raid_volume_pg1 - obtain raid volume page 1
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
mps_config_get_raid_volume_pg1(struct mps_softc *sc, Mpi2ConfigReply_t
    *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form, u16 handle)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	Mpi2RaidVolPage1_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_RAIDVOLPAGE1_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO | M_NOWAIT);
	if (!page) {
		printf("%s: page alloc failed\n", __func__);
		error = ENOMEM;
		goto out;
	}
	cm->cm_data = page;

	error = mps_wait_command(sc, &cm, 60, CAN_SLEEP);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}

/**
 * mps_config_get_volume_wwid - returns wwid given the volume handle
 * @sc: per adapter object
 * @volume_handle: volume handle
 * @wwid: volume wwid
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_volume_wwid(struct mps_softc *sc, u16 volume_handle, u64 *wwid)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2RaidVolPage1_t raid_vol_pg1;

	*wwid = 0;
	if (!(mps_config_get_raid_volume_pg1(sc, &mpi_reply, &raid_vol_pg1,
	    MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, volume_handle))) {
		*wwid = le64toh((u64)raid_vol_pg1.WWID.High << 32 |
		    raid_vol_pg1.WWID.Low);
		return 0;
	} else
		return -1;
}

/**
 * mps_config_get_pd_pg0 - obtain raid phys disk page 0
 * @sc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @config_page: contents of the config page
 * @page_address: form and handle value used to get page
 * Context: sleep.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mps_config_get_raid_pd_pg0(struct mps_softc *sc, Mpi2ConfigReply_t *mpi_reply,
    Mpi2RaidPhysDiskPage0_t *config_page, u32 page_address)
{
	MPI2_CONFIG_REQUEST *request;
	MPI2_CONFIG_REPLY *reply = NULL;
	struct mps_command *cm;
	Mpi2RaidPhysDiskPage0_t *page = NULL;
	int error = 0;
	u16 ioc_status;

	mps_dprint(sc, MPS_TRACE, "%s\n", __func__);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	request->Header.PageVersion = MPI2_RAIDPHYSDISKPAGE0_PAGEVERSION;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;

	/*
	 * This page must be polled because the IOC isn't ready yet when this
	 * page is needed.
	 */  
	error = mps_wait_command(sc, &cm, 60, 0);
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
	mps_free_command(sc, cm);

	if ((cm = mps_alloc_command(sc)) == NULL) {
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
	cm->cm_flags = MPS_CM_FLAGS_SGE_SIMPLE | MPS_CM_FLAGS_DATAIN;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	page = malloc(cm->cm_length, M_MPT2, M_ZERO | M_NOWAIT);
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
	error = mps_wait_command(sc, &cm, 60, 0);
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
	free(page, M_MPT2);
	if (cm)
		mps_free_command(sc, cm);
	return (error);
}
