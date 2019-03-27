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
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/mpt_ioctl.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mptutil.h"

static const char *mpt_ioc_status_codes[] = {
	"Success",				/* 0x0000 */
	"Invalid function",
	"Busy",
	"Invalid scatter-gather list",
	"Internal error",
	"Reserved",
	"Insufficient resources",
	"Invalid field",
	"Invalid state",			/* 0x0008 */
	"Operation state not supported",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,					/* 0x0010 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,					/* 0x0018 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Invalid configuration action",		/* 0x0020 */
	"Invalid configuration type",
	"Invalid configuration page",
	"Invalid configuration data",
	"No configuration defaults",
	"Unable to commit configuration change",
	NULL,
	NULL,
	NULL,					/* 0x0028 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,					/* 0x0030 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,					/* 0x0038 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Recovered SCSI error",			/* 0x0040 */
	"Invalid SCSI bus",
	"Invalid SCSI target ID",
	"SCSI device not there",
	"SCSI data overrun",
	"SCSI data underrun",
	"SCSI I/O error",
	"SCSI protocol error",
	"SCSI task terminated",			/* 0x0048 */
	"SCSI residual mismatch",
	"SCSI task management failed",
	"SCSI I/O controller terminated",
	"SCSI external controller terminated",
	"EEDP guard error",
	"EEDP reference tag error",
	"EEDP application tag error",
	NULL,					/* 0x0050 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,					/* 0x0058 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"SCSI target priority I/O",		/* 0x0060 */
	"Invalid SCSI target port",
	"Invalid SCSI target I/O index",
	"SCSI target aborted",
	"No connection retryable",
	"No connection",
	"FC aborted",
	"Invalid FC receive ID",
	"FC did invalid",			/* 0x0068 */
	"FC node logged out",
	"Transfer count mismatch",
	"STS data not set",
	"FC exchange canceled",
	"Data offset error",
	"Too much write data",
	"IU too short",
	"ACK NAK timeout",			/* 0x0070 */
	"NAK received",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,					/* 0x0078 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"LAN device not found",			/* 0x0080 */
	"LAN device failure",
	"LAN transmit error",
	"LAN transmit aborted",
	"LAN receive error",
	"LAN receive aborted",
	"LAN partial packet",
	"LAN canceled",
	NULL,					/* 0x0088 */
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"SAS SMP request failed",		/* 0x0090 */
	"SAS SMP data overrun",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Inband aborted",			/* 0x0098 */
	"No inband connection",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	"Diagnostic released",			/* 0x00A0 */
};

static const char *mpt_raid_action_status_codes[] = {
	"Success",
	"Invalid action",
	"Failure",
	"Operation in progress",
};

const char *
mpt_ioc_status(U16 IOCStatus)
{
	static char buffer[16];

	IOCStatus &= MPI_IOCSTATUS_MASK;
	if (IOCStatus < sizeof(mpt_ioc_status_codes) / sizeof(char *) &&
	    mpt_ioc_status_codes[IOCStatus] != NULL)
		return (mpt_ioc_status_codes[IOCStatus]);
	snprintf(buffer, sizeof(buffer), "Status: 0x%04x", IOCStatus);
	return (buffer);
}

const char *
mpt_raid_status(U16 ActionStatus)
{
	static char buffer[16];

	if (ActionStatus < sizeof(mpt_raid_action_status_codes) /
	    sizeof(char *))
		return (mpt_raid_action_status_codes[ActionStatus]);
	snprintf(buffer, sizeof(buffer), "Status: 0x%04x", ActionStatus);
	return (buffer);
}

const char *
mpt_raid_level(U8 VolumeType)
{
	static char buf[16];

	switch (VolumeType) {
	case MPI_RAID_VOL_TYPE_IS:
		return ("RAID-0");
	case MPI_RAID_VOL_TYPE_IM:
		return ("RAID-1");
	case MPI_RAID_VOL_TYPE_IME:
		return ("RAID-1E");
	case MPI_RAID_VOL_TYPE_RAID_5:
		return ("RAID-5");
	case MPI_RAID_VOL_TYPE_RAID_6:
		return ("RAID-6");
	case MPI_RAID_VOL_TYPE_RAID_10:
		return ("RAID-10");
	case MPI_RAID_VOL_TYPE_RAID_50:
		return ("RAID-50");
	default:
		sprintf(buf, "LVL 0x%02x", VolumeType);
		return (buf);
	}
}

const char *
mpt_volume_name(U8 VolumeBus, U8 VolumeID)
{
	static struct mpt_query_disk info;
	static char buf[16];

	if (mpt_query_disk(VolumeBus, VolumeID, &info) != 0) {
		/*
		 * We only print out the bus number if it is non-zero
		 * since mpt(4) only supports devices on bus zero
		 * anyway.
		 */
		if (VolumeBus == 0)
			snprintf(buf, sizeof(buf), "%d", VolumeID);
		else
			snprintf(buf, sizeof(buf), "%d:%d", VolumeBus,
			    VolumeID);
		return (buf);
	}
	return (info.devname);
}

int
mpt_lookup_volume(int fd, const char *name, U8 *VolumeBus, U8 *VolumeID)
{
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	struct mpt_query_disk info;
	char *cp;
	long bus, id;
	int i;

	/*
	 * Check for a raw [<bus>:]<id> string.  If the bus is not
	 * specified, assume bus 0.
	 */
	bus = strtol(name, &cp, 0);
	if (*cp == ':') {
		id = strtol(cp + 1, &cp, 0);
		if (*cp == '\0') {
			if (bus < 0 || bus > 0xff || id < 0 || id > 0xff) {
				return (EINVAL);
			}
			*VolumeBus = bus;
			*VolumeID = id;
			return (0);
		}
	} else if (*cp == '\0') {
		if (bus < 0 || bus > 0xff)
			return (EINVAL);
		*VolumeBus = 0;
		*VolumeID = bus;
		return (0);
	}

	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (ioc2 == NULL)
		return (errno);

	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		if (mpt_query_disk(vol->VolumeBus, vol->VolumeID, &info) != 0)
			continue;
		if (strcmp(name, info.devname) == 0) {
			*VolumeBus = vol->VolumeBus;
			*VolumeID = vol->VolumeID;
			free(ioc2);
			return (0);
		}
	}
	free(ioc2);
	return (EINVAL);
}

int
mpt_read_config_page_header(int fd, U8 PageType, U8 PageNumber, U32 PageAddress,
    CONFIG_PAGE_HEADER *header, U16 *IOCStatus)
{
	struct mpt_cfg_page_req req;

	if (IOCStatus != NULL)
		*IOCStatus = MPI_IOCSTATUS_SUCCESS;
	bzero(&req, sizeof(req));
	req.header.PageType = PageType;
	req.header.PageNumber = PageNumber;
	req.page_address = PageAddress;
	if (ioctl(fd, MPTIO_READ_CFG_HEADER, &req) < 0)
		return (errno);
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading config page header failed: %s",
			    mpt_ioc_status(req.ioc_status));
		return (EIO);
	}
	*header = req.header;
	return (0);
}

void *
mpt_read_config_page(int fd, U8 PageType, U8 PageNumber, U32 PageAddress,
    U16 *IOCStatus)
{
	struct mpt_cfg_page_req req;
	void *buf;
	int error;

	if (IOCStatus != NULL)
		*IOCStatus = MPI_IOCSTATUS_SUCCESS;
	bzero(&req, sizeof(req));
	req.header.PageType = PageType;
	req.header.PageNumber = PageNumber;
	req.page_address = PageAddress;
	if (ioctl(fd, MPTIO_READ_CFG_HEADER, &req) < 0)
		return (NULL);
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading config page header failed: %s",
			    mpt_ioc_status(req.ioc_status));
		errno = EIO;
		return (NULL);
	}
	req.len = req.header.PageLength * 4;
	buf = malloc(req.len);
	req.buf = buf;
	bcopy(&req.header, buf, sizeof(req.header));
	if (ioctl(fd, MPTIO_READ_CFG_PAGE, &req) < 0) {
		error = errno;
		free(buf);
		errno = error;
		return (NULL);
	}
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading config page failed: %s",
			    mpt_ioc_status(req.ioc_status));
		free(buf);
		errno = EIO;
		return (NULL);
	}
	return (buf);
}

void *
mpt_read_extended_config_page(int fd, U8 ExtPageType, U8 PageVersion,
    U8 PageNumber, U32 PageAddress, U16 *IOCStatus)
{
	struct mpt_ext_cfg_page_req req;
	void *buf;
	int error;

	if (IOCStatus != NULL)
		*IOCStatus = MPI_IOCSTATUS_SUCCESS;
	bzero(&req, sizeof(req));
	req.header.PageVersion = PageVersion;
	req.header.PageNumber = PageNumber;
	req.header.ExtPageType = ExtPageType;
	req.page_address = PageAddress;
	if (ioctl(fd, MPTIO_READ_EXT_CFG_HEADER, &req) < 0)
		return (NULL);
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading extended config page header failed: %s",
			    mpt_ioc_status(req.ioc_status));
		errno = EIO;
		return (NULL);
	}
	req.len = req.header.ExtPageLength * 4;
	buf = malloc(req.len);
	req.buf = buf;
	bcopy(&req.header, buf, sizeof(req.header));
	if (ioctl(fd, MPTIO_READ_EXT_CFG_PAGE, &req) < 0) {
		error = errno;
		free(buf);
		errno = error;
		return (NULL);
	}
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading extended config page failed: %s",
			    mpt_ioc_status(req.ioc_status));
		free(buf);
		errno = EIO;
		return (NULL);
	}
	return (buf);
}

int
mpt_write_config_page(int fd, void *buf, U16 *IOCStatus)
{
	CONFIG_PAGE_HEADER *hdr;
	struct mpt_cfg_page_req req;

	if (IOCStatus != NULL)
		*IOCStatus = MPI_IOCSTATUS_SUCCESS;
	bzero(&req, sizeof(req));
	req.buf = buf;
	hdr = buf;
	req.len = hdr->PageLength * 4;
	if (ioctl(fd, MPTIO_WRITE_CFG_PAGE, &req) < 0)
		return (errno);
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL) {
			*IOCStatus = req.ioc_status;
			return (0);
		}
		warnx("Writing config page failed: %s",
		    mpt_ioc_status(req.ioc_status));
		return (EIO);
	}
	return (0);
}

int
mpt_raid_action(int fd, U8 Action, U8 VolumeBus, U8 VolumeID, U8 PhysDiskNum,
    U32 ActionDataWord, void *buf, int len, RAID_VOL0_STATUS *VolumeStatus,
    U32 *ActionData, int datalen, U16 *IOCStatus, U16 *ActionStatus, int write)
{
	struct mpt_raid_action raid_act;

	if (IOCStatus != NULL)
		*IOCStatus = MPI_IOCSTATUS_SUCCESS;
	if (datalen < 0 || (unsigned)datalen > sizeof(raid_act.action_data))
		return (EINVAL);
	bzero(&raid_act, sizeof(raid_act));
	raid_act.action = Action;
	raid_act.volume_bus = VolumeBus;
	raid_act.volume_id = VolumeID;
	raid_act.phys_disk_num = PhysDiskNum;
	raid_act.action_data_word = ActionDataWord;
	if (buf != NULL && len != 0) {
		raid_act.buf = buf;
		raid_act.len = len;
		raid_act.write = write;
	}

	if (ioctl(fd, MPTIO_RAID_ACTION, &raid_act) < 0)
		return (errno);

	if (!IOC_STATUS_SUCCESS(raid_act.ioc_status)) {
		if (IOCStatus != NULL) {
			*IOCStatus = raid_act.ioc_status;
			return (0);
		}
		warnx("RAID action failed: %s",
		    mpt_ioc_status(raid_act.ioc_status));
		return (EIO);
	}

	if (ActionStatus != NULL)
		*ActionStatus = raid_act.action_status;
	if (raid_act.action_status != MPI_RAID_ACTION_ASTATUS_SUCCESS) {
		if (ActionStatus != NULL)
			return (0);
		warnx("RAID action failed: %s",
		    mpt_raid_status(raid_act.action_status));
		return (EIO);
	}

	if (VolumeStatus != NULL)
		*((U32 *)VolumeStatus) = raid_act.volume_status;
	if (ActionData != NULL)
		bcopy(raid_act.action_data, ActionData, datalen);
	return (0);
}

int
mpt_open(int unit)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof(path), "/dev/mpt%d", unit);
	return (open(path, O_RDWR));
}

int
mpt_table_handler(struct mptutil_command **start, struct mptutil_command **end,
    int ac, char **av)
{
	struct mptutil_command **cmd;

	if (ac < 2) {
		warnx("The %s command requires a sub-command.", av[0]);
		return (EINVAL);
	}
	for (cmd = start; cmd < end; cmd++) {
		if (strcmp((*cmd)->name, av[1]) == 0)
			return ((*cmd)->handler(ac - 1, av + 1));
	}

	warnx("%s is not a valid sub-command of %s.", av[1], av[0]);
	return (ENOENT);
}

#ifdef DEBUG
void
hexdump(const void *ptr, int length, const char *hdr, int flags)
{
	int i, j, k;
	int cols;
	const unsigned char *cp;
	char delim;

	if ((flags & HD_DELIM_MASK) != 0)
		delim = (flags & HD_DELIM_MASK) >> 8;
	else
		delim = ' ';

	if ((flags & HD_COLUMN_MASK) != 0)
		cols = flags & HD_COLUMN_MASK;
	else
		cols = 16;

	cp = ptr;
	for (i = 0; i < length; i+= cols) {
		if (hdr != NULL)
			printf("%s", hdr);

		if ((flags & HD_OMIT_COUNT) == 0)
			printf("%04x  ", i);

		if ((flags & HD_OMIT_HEX) == 0) {
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k < length)
					printf("%c%02x", delim, cp[k]);
				else
					printf("   ");
			}
		}

		if ((flags & HD_OMIT_CHARS) == 0) {
			printf("  |");
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k >= length)
					printf(" ");
				else if (cp[k] >= ' ' && cp[k] <= '~')
					printf("%c", cp[k]);
				else
					printf(".");
			}
			printf("|");
		}
		printf("\n");
	}
}
#endif
