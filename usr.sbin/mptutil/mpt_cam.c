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
 */

#include <sys/cdefs.h>
__RCSID("$FreeBSD$");

#include <sys/param.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <camlib.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>

#include "mptutil.h"

static int xptfd;

static int
xpt_open(void)
{

	if (xptfd == 0)
		xptfd = open(XPT_DEVICE, O_RDWR);
	return (xptfd);
}

/* Fetch the path id of bus 0 for the opened mpt controller. */
static int
fetch_path_id(path_id_t *path_id)
{
	struct bus_match_pattern *b;
	union ccb ccb;
	size_t bufsize;
	int error;

	if (xpt_open() < 0)
		return (ENXIO);

	/* First, find the path id of bus 0 for this mpt controller. */
	bzero(&ccb, sizeof(ccb));

	ccb.ccb_h.func_code = XPT_DEV_MATCH;

	bufsize = sizeof(struct dev_match_result) * 1;
	ccb.cdm.num_matches = 0;
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = calloc(1, bufsize);

	bufsize = sizeof(struct dev_match_pattern) * 1;
	ccb.cdm.num_patterns = 1;
	ccb.cdm.pattern_buf_len = bufsize;
	ccb.cdm.patterns = calloc(1, bufsize);

	/* Match mptX bus 0. */
	ccb.cdm.patterns[0].type = DEV_MATCH_BUS;
	b = &ccb.cdm.patterns[0].pattern.bus_pattern;
	snprintf(b->dev_name, sizeof(b->dev_name), "mpt");
	b->unit_number = mpt_unit;
	b->bus_id = 0;
	b->flags = BUS_MATCH_NAME | BUS_MATCH_UNIT | BUS_MATCH_BUS_ID;

	if (ioctl(xptfd, CAMIOCOMMAND, &ccb) < 0) {
		error = errno;
		free(ccb.cdm.matches);
		free(ccb.cdm.patterns);
		return (error);
	}
	free(ccb.cdm.patterns);

	if (((ccb.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) ||
	    (ccb.cdm.status != CAM_DEV_MATCH_LAST)) {
		warnx("fetch_path_id got CAM error %#x, CDM error %d\n",
		    ccb.ccb_h.status, ccb.cdm.status);
		free(ccb.cdm.matches);
		return (EIO);
	}

	/* We should have exactly 1 match for the bus. */
	if (ccb.cdm.num_matches != 1 ||
	    ccb.cdm.matches[0].type != DEV_MATCH_BUS) {
		free(ccb.cdm.matches);
		return (ENOENT);
	}
	*path_id = ccb.cdm.matches[0].result.bus_result.path_id;
	free(ccb.cdm.matches);
	return (0);
}

int
mpt_query_disk(U8 VolumeBus, U8 VolumeID, struct mpt_query_disk *qd)
{
	struct periph_match_pattern *p;
	struct periph_match_result *r;
	union ccb ccb;
	path_id_t path_id;
	size_t bufsize;
	int error;

	/* mpt(4) only handles devices on bus 0. */
	if (VolumeBus != 0)
		return (ENXIO);

	if (xpt_open() < 0)
		return (ENXIO);

	/* Find the path ID of bus 0. */
	error = fetch_path_id(&path_id);
	if (error)
		return (error);

	bzero(&ccb, sizeof(ccb));

	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	bufsize = sizeof(struct dev_match_result) * 5;
	ccb.cdm.num_matches = 0;
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = calloc(1, bufsize);

	bufsize = sizeof(struct dev_match_pattern) * 1;
	ccb.cdm.num_patterns = 1;
	ccb.cdm.pattern_buf_len = bufsize;
	ccb.cdm.patterns = calloc(1, bufsize);

	/* Look for a "da" device at the specified target and lun. */
	ccb.cdm.patterns[0].type = DEV_MATCH_PERIPH;
	p = &ccb.cdm.patterns[0].pattern.periph_pattern;
	p->path_id = path_id;
	snprintf(p->periph_name, sizeof(p->periph_name), "da");
	p->target_id = VolumeID;
	p->flags = PERIPH_MATCH_PATH | PERIPH_MATCH_NAME | PERIPH_MATCH_TARGET;

	if (ioctl(xptfd, CAMIOCOMMAND, &ccb) < 0) {
		error = errno;
		free(ccb.cdm.matches);
		free(ccb.cdm.patterns);
		return (error);
	}
	free(ccb.cdm.patterns);

	if (((ccb.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) ||
	    (ccb.cdm.status != CAM_DEV_MATCH_LAST)) {
		warnx("mpt_query_disk got CAM error %#x, CDM error %d\n",
		    ccb.ccb_h.status, ccb.cdm.status);
		free(ccb.cdm.matches);
		return (EIO);
	}

	/*
	 * We should have exactly 1 match for the peripheral.
	 * However, if we don't get a match, don't print an error
	 * message and return ENOENT.
	 */
	if (ccb.cdm.num_matches == 0) {
		free(ccb.cdm.matches);
		return (ENOENT);
	}
	if (ccb.cdm.num_matches != 1) {
		warnx("mpt_query_disk got %d matches, expected 1",
		    ccb.cdm.num_matches);
		free(ccb.cdm.matches);
		return (EIO);
	}
	if (ccb.cdm.matches[0].type != DEV_MATCH_PERIPH) {
		warnx("mpt_query_disk got wrong CAM match");
		free(ccb.cdm.matches);
		return (EIO);
	}

	/* Copy out the data. */
	r = &ccb.cdm.matches[1].result.periph_result;
	snprintf(qd->devname, sizeof(qd->devname), "%s%d", r->periph_name,
	    r->unit_number);
	free(ccb.cdm.matches);

	return (0);
}

static int
periph_is_volume(CONFIG_PAGE_IOC_2 *ioc2, struct periph_match_result *r)
{
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	int i;

	if (ioc2 == NULL)
		return (0);
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		if (vol->VolumeBus == 0 && vol->VolumeID == r->target_id)
			return (1);
	}
	return (0);
}

/* Much borrowed from scsireadcapacity() in src/sbin/camcontrol/camcontrol.c. */
static int
fetch_scsi_capacity(struct cam_device *dev, struct mpt_standalone_disk *disk)
{
	struct scsi_read_capacity_data rcap;
	struct scsi_read_capacity_data_long rcaplong;
	union ccb *ccb;
	int error;

	ccb = cam_getccb(dev);
	if (ccb == NULL)
		return (ENOMEM);

	/* Zero the rest of the ccb. */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_read_capacity(&ccb->csio, 1, NULL, MSG_SIMPLE_Q_TAG, &rcap,
	    SSD_FULL_SIZE, 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (cam_send_ccb(dev, ccb) < 0) {
		error = errno;
		cam_freeccb(ccb);
		return (error);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_freeccb(ccb);
		return (EIO);
	}

	/*
	 * A last block of 2^32-1 means that the true capacity is over 2TB,
	 * and we need to issue the long READ CAPACITY to get the real
	 * capacity.  Otherwise, we're all set.
	 */
	if (scsi_4btoul(rcap.addr) != 0xffffffff) {
		disk->maxlba = scsi_4btoul(rcap.addr);
		cam_freeccb(ccb);
		return (0);
	}

	/* Zero the rest of the ccb. */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	scsi_read_capacity_16(&ccb->csio, 1, NULL, MSG_SIMPLE_Q_TAG, 0, 0, 0,
	    (uint8_t *)&rcaplong, sizeof(rcaplong), SSD_FULL_SIZE, 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (cam_send_ccb(dev, ccb) < 0) {
		error = errno;
		cam_freeccb(ccb);
		return (error);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_freeccb(ccb);
		return (EIO);
	}
	cam_freeccb(ccb);

	disk->maxlba = scsi_8btou64(rcaplong.addr);
	return (0);
}

/* Borrowed heavily from scsi_all.c:scsi_print_inquiry(). */
static void
format_scsi_inquiry(struct mpt_standalone_disk *disk,
    struct scsi_inquiry_data *inq_data)
{
	char vendor[16], product[48], revision[16], rstr[12];

	if (SID_QUAL_IS_VENDOR_UNIQUE(inq_data))
		return;
	if (SID_TYPE(inq_data) != T_DIRECT)
		return;
	if (SID_QUAL(inq_data) != SID_QUAL_LU_CONNECTED)
		return;

	cam_strvis(vendor, inq_data->vendor, sizeof(inq_data->vendor),
	    sizeof(vendor));
	cam_strvis(product, inq_data->product, sizeof(inq_data->product),
	    sizeof(product));
	cam_strvis(revision, inq_data->revision, sizeof(inq_data->revision),
	    sizeof(revision));

	/* Hack for SATA disks, no idea how to tell speed. */
	if (strcmp(vendor, "ATA") == 0) {
		snprintf(disk->inqstring, sizeof(disk->inqstring),
		    "<%s %s> SATA", product, revision);
		return;
	}

	switch (SID_ANSI_REV(inq_data)) {
	case SCSI_REV_CCS:
		strcpy(rstr, "SCSI-CCS");
		break;
	case 5:
		strcpy(rstr, "SAS");
		break;
	default:
		snprintf(rstr, sizeof (rstr), "SCSI-%d",
		    SID_ANSI_REV(inq_data));
		break;
	}
	snprintf(disk->inqstring, sizeof(disk->inqstring), "<%s %s %s> %s",
	    vendor, product, revision, rstr);
}

/* Much borrowed from scsiinquiry() in src/sbin/camcontrol/camcontrol.c. */
static int
fetch_scsi_inquiry(struct cam_device *dev, struct mpt_standalone_disk *disk)
{
	struct scsi_inquiry_data *inq_buf;
	union ccb *ccb;
	int error;

	ccb = cam_getccb(dev);
	if (ccb == NULL)
		return (ENOMEM);

	/* Zero the rest of the ccb. */
	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	inq_buf = calloc(1, sizeof(*inq_buf));
	if (inq_buf == NULL) {
		cam_freeccb(ccb);
		return (ENOMEM);
	}
	scsi_inquiry(&ccb->csio, 1, NULL, MSG_SIMPLE_Q_TAG, (void *)inq_buf,
	    SHORT_INQUIRY_LENGTH, 0, 0, SSD_FULL_SIZE, 5000);

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (cam_send_ccb(dev, ccb) < 0) {
		error = errno;
		free(inq_buf);
		cam_freeccb(ccb);
		return (error);
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		free(inq_buf);
		cam_freeccb(ccb);
		return (EIO);
	}

	cam_freeccb(ccb);
	format_scsi_inquiry(disk, inq_buf);
	free(inq_buf);
	return (0);
}

int
mpt_fetch_disks(int fd, int *ndisks, struct mpt_standalone_disk **disksp)
{
	CONFIG_PAGE_IOC_2 *ioc2;
	struct mpt_standalone_disk *disks;
	struct periph_match_pattern *p;
	struct periph_match_result *r;
	struct cam_device *dev;
	union ccb ccb;
	path_id_t path_id;
	size_t bufsize;
	int count, error;
	uint32_t i;

	if (xpt_open() < 0)
		return (ENXIO);

	error = fetch_path_id(&path_id);
	if (error)
		return (error);

	for (count = 100;; count+= 100) {
		/* Try to fetch 'count' disks in one go. */
		bzero(&ccb, sizeof(ccb));

		ccb.ccb_h.func_code = XPT_DEV_MATCH;

		bufsize = sizeof(struct dev_match_result) * (count + 1);
		ccb.cdm.num_matches = 0;
		ccb.cdm.match_buf_len = bufsize;
		ccb.cdm.matches = calloc(1, bufsize);

		bufsize = sizeof(struct dev_match_pattern) * 1;
		ccb.cdm.num_patterns = 1;
		ccb.cdm.pattern_buf_len = bufsize;
		ccb.cdm.patterns = calloc(1, bufsize);

		/* Match any "da" peripherals. */
		ccb.cdm.patterns[0].type = DEV_MATCH_PERIPH;
		p = &ccb.cdm.patterns[0].pattern.periph_pattern;
		p->path_id = path_id;
		snprintf(p->periph_name, sizeof(p->periph_name), "da");
		p->flags = PERIPH_MATCH_PATH | PERIPH_MATCH_NAME;

		if (ioctl(xptfd, CAMIOCOMMAND, &ccb) < 0) {
			error = errno;
			free(ccb.cdm.matches);
			free(ccb.cdm.patterns);
			return (error);
		}
		free(ccb.cdm.patterns);

		/* Check for CCB errors. */
		if ((ccb.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
			free(ccb.cdm.matches);
			return (EIO);
		}

		/* If we need a longer list, try again. */
		if (ccb.cdm.status == CAM_DEV_MATCH_MORE) {
			free(ccb.cdm.matches);
			continue;
		}

		/* If we got an error, abort. */
		if (ccb.cdm.status != CAM_DEV_MATCH_LAST) {
			free(ccb.cdm.matches);
			return (EIO);
		}
		break;
	}

	/* Shortcut if we don't have any "da" devices. */
	if (ccb.cdm.num_matches == 0) {
		free(ccb.cdm.matches);
		*ndisks = 0;
		*disksp = NULL;
		return (0);
	}

	/* We should have N matches, 1 for each "da" device. */
	for (i = 0; i < ccb.cdm.num_matches; i++) {
		if (ccb.cdm.matches[i].type != DEV_MATCH_PERIPH) {
			warnx("mpt_fetch_disks got wrong CAM matches");
			free(ccb.cdm.matches);
			return (EIO);
		}
	}

	/*
	 * Some of the "da" peripherals may be for RAID volumes, so
	 * fetch the IOC 2 page (list of RAID volumes) so we can
	 * exclude them from the list.
	 */
	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (ioc2 == NULL)
		return (errno);
	disks = calloc(ccb.cdm.num_matches, sizeof(*disks));
	count = 0;
	for (i = 0; i < ccb.cdm.num_matches; i++) {
		r = &ccb.cdm.matches[i].result.periph_result;
		if (periph_is_volume(ioc2, r))
			continue;
		disks[count].bus = 0;
		disks[count].target = r->target_id;
		snprintf(disks[count].devname, sizeof(disks[count].devname),
		    "%s%d", r->periph_name, r->unit_number);

		dev = cam_open_device(disks[count].devname, O_RDWR);
		if (dev != NULL) {
			fetch_scsi_capacity(dev, &disks[count]);
			fetch_scsi_inquiry(dev, &disks[count]);
			cam_close_device(dev);
		}
		count++;
	}
	free(ccb.cdm.matches);
	free(ioc2);

	*ndisks = count;
	*disksp = disks;
	return (0);
}

/*
 * Instruct the mpt(4) device to rescan its buses to find new devices
 * such as disks whose RAID physdisk page was removed or volumes that
 * were created.  If id is -1, the entire bus is rescanned.
 * Otherwise, only devices at the specified ID are rescanned.  If bus
 * is -1, then all buses are scanned instead of the specified bus.
 * Note that currently, only bus 0 is supported.
 */
int
mpt_rescan_bus(int bus, int id)
{
	union ccb ccb;
	path_id_t path_id;
	int error;

	/* mpt(4) only handles devices on bus 0. */
	if (bus != -1 && bus != 0)
		return (EINVAL);

	if (xpt_open() < 0)
		return (ENXIO);

	error = fetch_path_id(&path_id);
	if (error)
		return (error);

	/* Perform the actual rescan. */
	bzero(&ccb, sizeof(ccb));
	ccb.ccb_h.path_id = path_id;
	if (id == -1) {
		ccb.ccb_h.func_code = XPT_SCAN_BUS;
		ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
		ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;
		ccb.ccb_h.timeout = 5000;
	} else {
		ccb.ccb_h.func_code = XPT_SCAN_LUN;
		ccb.ccb_h.target_id = id;
		ccb.ccb_h.target_lun = 0;
	}
	ccb.crcn.flags = CAM_FLAG_NONE;

	/* Run this at a low priority. */
	ccb.ccb_h.pinfo.priority = 5;

	if (ioctl(xptfd, CAMIOCOMMAND, &ccb) == -1)
		return (errno);

	if ((ccb.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		warnx("mpt_rescan_bus rescan got CAM error %#x\n",
		    ccb.ccb_h.status & CAM_STATUS_MASK);
		return (EIO);
	}

	return (0);
}
