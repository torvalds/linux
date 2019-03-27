/*-
 * Copyright (c) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
 *
 * Copyright (c) 2015 Netflix, Inc.
 * Written by: Scott Long <scottl@freebsd.org>
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
#if 0
#include <sys/mps_ioctl.h>
#else
#include "mps_ioctl.h"
#include "mpr_ioctl.h"
#endif
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpsutil.h"

#ifndef USE_MPT_IOCTLS
#define USE_MPT_IOCTLS
#endif

static const char *mps_ioc_status_codes[] = {
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

struct mprs_pass_thru {
        uint64_t        PtrRequest;
        uint64_t        PtrReply;
        uint64_t        PtrData;
        uint32_t        RequestSize;
        uint32_t        ReplySize;
        uint32_t        DataSize;
        uint32_t        DataDirection;
        uint64_t        PtrDataOut;
        uint32_t        DataOutSize;
        uint32_t        Timeout;
};

struct mprs_btdh_mapping {
        uint16_t        TargetID;
        uint16_t        Bus;
        uint16_t        DevHandle;
        uint16_t        Reserved;
};

const char *
mps_ioc_status(U16 IOCStatus)
{
	static char buffer[16];

	IOCStatus &= MPI2_IOCSTATUS_MASK;
	if (IOCStatus < sizeof(mps_ioc_status_codes) / sizeof(char *) &&
	    mps_ioc_status_codes[IOCStatus] != NULL)
		return (mps_ioc_status_codes[IOCStatus]);
	snprintf(buffer, sizeof(buffer), "Status: 0x%04x", IOCStatus);
	return (buffer);
}

#ifdef USE_MPT_IOCTLS
int
mps_map_btdh(int fd, uint16_t *devhandle, uint16_t *bus, uint16_t *target)
{
	int error;
	struct mprs_btdh_mapping map;

	map.Bus = *bus;
	map.TargetID = *target;
	map.DevHandle = *devhandle;

	if ((error = ioctl(fd, MPTIOCTL_BTDH_MAPPING, &map)) != 0) {
		error = errno;
		warn("Failed to map bus/target/device");
		return (error);
	}

	*bus = map.Bus;
	*target = map.TargetID;
	*devhandle = map.DevHandle;

	return (0);
}

int
mps_read_config_page_header(int fd, U8 PageType, U8 PageNumber, U32 PageAddress,
    MPI2_CONFIG_PAGE_HEADER *header, U16 *IOCStatus)
{
	MPI2_CONFIG_REQUEST req;
	MPI2_CONFIG_REPLY reply;

	bzero(&req, sizeof(req));
	req.Function = MPI2_FUNCTION_CONFIG;
	req.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	req.Header.PageType = PageType;
	req.Header.PageNumber = PageNumber;
	req.PageAddress = PageAddress;

	if (mps_pass_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    NULL, 0, NULL, 0, 30))
		return (errno);

	if (!IOC_STATUS_SUCCESS(reply.IOCStatus)) {
		if (IOCStatus != NULL)
			*IOCStatus = reply.IOCStatus;
		return (EIO);
	}
	if (header == NULL)
		return (EINVAL);
	*header = reply.Header;
	return (0);
}

int
mps_read_ext_config_page_header(int fd, U8 ExtPageType, U8 PageNumber, U32 PageAddress, MPI2_CONFIG_PAGE_HEADER *header, U16 *ExtPageLength, U16 *IOCStatus)
{
	MPI2_CONFIG_REQUEST req;
	MPI2_CONFIG_REPLY reply;

	bzero(&req, sizeof(req));
	req.Function = MPI2_FUNCTION_CONFIG;
	req.Action = MPI2_CONFIG_ACTION_PAGE_HEADER;
	req.Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
	req.ExtPageType = ExtPageType;
	req.Header.PageNumber = PageNumber;
	req.PageAddress = PageAddress;

	if (mps_pass_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    NULL, 0, NULL, 0, 30))
		return (errno);

	if (!IOC_STATUS_SUCCESS(reply.IOCStatus)) {
		if (IOCStatus != NULL)
			*IOCStatus = reply.IOCStatus;
		return (EIO);
	}
	if ((header == NULL) || (ExtPageLength == NULL))
		return (EINVAL);
	*header = reply.Header;
	*ExtPageLength = reply.ExtPageLength;
	return (0);
}

void *
mps_read_config_page(int fd, U8 PageType, U8 PageNumber, U32 PageAddress,
    U16 *IOCStatus)
{
	MPI2_CONFIG_REQUEST req;
	MPI2_CONFIG_PAGE_HEADER header;
	MPI2_CONFIG_REPLY reply;
	void *buf;
	int error, len;

	bzero(&header, sizeof(header));
	error = mps_read_config_page_header(fd, PageType, PageNumber,
	    PageAddress, &header, IOCStatus);
	if (error) {
		errno = error;
		return (NULL);
	}

	bzero(&req, sizeof(req));
	req.Function = MPI2_FUNCTION_CONFIG;
	req.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	req.PageAddress = PageAddress;
	req.Header = header;
	if (req.Header.PageLength == 0)
		req.Header.PageLength = 4;

	len = req.Header.PageLength * 4;
	buf = malloc(len);
	if (mps_pass_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    buf, len, NULL, 0, 30)) {
		error = errno;
		free(buf);
		errno = error;
		return (NULL);
	}
	if (!IOC_STATUS_SUCCESS(reply.IOCStatus)) {
		if (IOCStatus != NULL)
			*IOCStatus = reply.IOCStatus;
		else
			warnx("Reading config page failed: 0x%x %s",
			    reply.IOCStatus, mps_ioc_status(reply.IOCStatus));
		free(buf);
		errno = EIO;
		return (NULL);
	}
	return (buf);
}

void *
mps_read_extended_config_page(int fd, U8 ExtPageType, U8 PageVersion,
    U8 PageNumber, U32 PageAddress, U16 *IOCStatus)
{
	MPI2_CONFIG_REQUEST req;
	MPI2_CONFIG_PAGE_HEADER header;
	MPI2_CONFIG_REPLY reply;
	U16 pagelen;
	void *buf;
	int error, len;

	if (IOCStatus != NULL)
		*IOCStatus = MPI2_IOCSTATUS_SUCCESS;
	bzero(&header, sizeof(header));
	error = mps_read_ext_config_page_header(fd, ExtPageType, PageNumber,
	    PageAddress, &header, &pagelen, IOCStatus);
	if (error) {
		errno = error;
		return (NULL);
	}

	bzero(&req, sizeof(req));
	req.Function = MPI2_FUNCTION_CONFIG;
	req.Action = MPI2_CONFIG_ACTION_PAGE_READ_CURRENT;
	req.PageAddress = PageAddress;
	req.Header = header;
	if (pagelen == 0)
		pagelen = 4;
	req.ExtPageLength = pagelen;
	req.ExtPageType = ExtPageType;

	len = pagelen * 4;
	buf = malloc(len);
	if (mps_pass_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    buf, len, NULL, 0, 30)) {
		error = errno;
		free(buf);
		errno = error;
		return (NULL);
	}
	if (!IOC_STATUS_SUCCESS(reply.IOCStatus)) {
		if (IOCStatus != NULL)
			*IOCStatus = reply.IOCStatus;
		else
			warnx("Reading extended config page failed: %s",
			    mps_ioc_status(reply.IOCStatus));
		free(buf);
		errno = EIO;
		return (NULL);
	}
	return (buf);
}

int
mps_firmware_send(int fd, unsigned char *fw, uint32_t len, bool bios)
{
	MPI2_FW_DOWNLOAD_REQUEST req;
	MPI2_FW_DOWNLOAD_REPLY reply;

	bzero(&req, sizeof(req));
	bzero(&reply, sizeof(reply));
	req.Function = MPI2_FUNCTION_FW_DOWNLOAD;
	req.ImageType = bios ? MPI2_FW_DOWNLOAD_ITYPE_BIOS : MPI2_FW_DOWNLOAD_ITYPE_FW;
	req.TotalImageSize = len;
	req.MsgFlags = MPI2_FW_DOWNLOAD_MSGFLGS_LAST_SEGMENT;

	if (mps_user_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    fw, len, 0)) {
		return (-1);
	}
	return (0);
}

int
mps_firmware_get(int fd, unsigned char **firmware, bool bios)
{
	MPI2_FW_UPLOAD_REQUEST req;
	MPI2_FW_UPLOAD_REPLY reply;
	int size;

	*firmware = NULL;
	bzero(&req, sizeof(req));
	bzero(&reply, sizeof(reply));
	req.Function = MPI2_FUNCTION_FW_UPLOAD;
	req.ImageType = bios ? MPI2_FW_DOWNLOAD_ITYPE_BIOS : MPI2_FW_DOWNLOAD_ITYPE_FW;

	if (mps_user_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    NULL, 0, 0)) {
		return (-1);
	}
	if (reply.ActualImageSize == 0) {
		return (-1);
	}

	size = reply.ActualImageSize;
	*firmware = calloc(size, sizeof(unsigned char));
	if (*firmware == NULL) {
		warn("calloc");
		return (-1);
	}
	if (mps_user_command(fd, &req, sizeof(req), &reply, sizeof(reply),
	    *firmware, size, 0)) {
		free(*firmware);
		return (-1);
	}

	return (size);
}

#else

int
mps_read_config_page_header(int fd, U8 PageType, U8 PageNumber, U32 PageAddress,
    MPI2_CONFIG_PAGE_HEADER *header, U16 *IOCStatus)
{
	struct mps_cfg_page_req req;

	if (IOCStatus != NULL)
		*IOCStatus = MPI2_IOCSTATUS_SUCCESS;
	if (header == NULL)
		return (EINVAL);
	bzero(&req, sizeof(req));
	req.header.PageType = PageType;
	req.header.PageNumber = PageNumber;
	req.page_address = PageAddress;
	if (ioctl(fd, MPSIO_READ_CFG_HEADER, &req) < 0)
		return (errno);
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		return (EIO);
	}
	bcopy(&req.header, header, sizeof(*header));
	return (0);
}

void *
mps_read_config_page(int fd, U8 PageType, U8 PageNumber, U32 PageAddress,
    U16 *IOCStatus)
{
	struct mps_cfg_page_req req;
	void *buf;
	int error;

	error = mps_read_config_page_header(fd, PageType, PageNumber,
	    PageAddress, &req.header, IOCStatus);
	if (error) {
		errno = error;
		return (NULL);
	}

	if (req.header.PageLength == 0)
		req.header.PageLength = 4;
	req.len = req.header.PageLength * 4;
	buf = malloc(req.len);
	req.buf = buf;
	bcopy(&req.header, buf, sizeof(req.header));
	if (ioctl(fd, MPSIO_READ_CFG_PAGE, &req) < 0) {
		error = errno;
		free(buf);
		errno = error;
		return (NULL);
	}
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading config page failed: 0x%x %s",
			    req.ioc_status, mps_ioc_status(req.ioc_status));
		free(buf);
		errno = EIO;
		return (NULL);
	}
	return (buf);
}

void *
mps_read_extended_config_page(int fd, U8 ExtPageType, U8 PageVersion,
    U8 PageNumber, U32 PageAddress, U16 *IOCStatus)
{
	struct mps_ext_cfg_page_req req;
	void *buf;
	int error;

	if (IOCStatus != NULL)
		*IOCStatus = MPI2_IOCSTATUS_SUCCESS;
	bzero(&req, sizeof(req));
	req.header.PageVersion = PageVersion;
	req.header.PageNumber = PageNumber;
	req.header.ExtPageType = ExtPageType;
	req.page_address = PageAddress;
	if (ioctl(fd, MPSIO_READ_EXT_CFG_HEADER, &req) < 0)
		return (NULL);
	if (!IOC_STATUS_SUCCESS(req.ioc_status)) {
		if (IOCStatus != NULL)
			*IOCStatus = req.ioc_status;
		else
			warnx("Reading extended config page header failed: %s",
			    mps_ioc_status(req.ioc_status));
		errno = EIO;
		return (NULL);
	}
	req.len = req.header.ExtPageLength * 4;
	buf = malloc(req.len);
	req.buf = buf;
	bcopy(&req.header, buf, sizeof(req.header));
	if (ioctl(fd, MPSIO_READ_EXT_CFG_PAGE, &req) < 0) {
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
			    mps_ioc_status(req.ioc_status));
		free(buf);
		errno = EIO;
		return (NULL);
	}
	return (buf);
}
#endif

int
mps_open(int unit)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof(path), "/dev/mp%s%d", is_mps ? "s": "r", unit);
	return (open(path, O_RDWR));
}

int
mps_user_command(int fd, void *req, uint32_t req_len, void *reply,
        uint32_t reply_len, void *buffer, int len, uint32_t flags)
{
	struct mps_usr_command cmd;

	bzero(&cmd, sizeof(struct mps_usr_command));
	cmd.req = req;
	cmd.req_len = req_len;
	cmd.rpl = reply;
	cmd.rpl_len = reply_len;
	cmd.buf = buffer;
	cmd.len = len;
	cmd.flags = flags;

	if (ioctl(fd, is_mps ? MPSIO_MPS_COMMAND : MPRIO_MPR_COMMAND, &cmd) < 0)
		return (errno);
	return (0);
}

int
mps_pass_command(int fd, void *req, uint32_t req_len, void *reply,
	uint32_t reply_len, void *data_in, uint32_t datain_len, void *data_out,
	uint32_t dataout_len, uint32_t timeout)
{
	struct mprs_pass_thru pass;

	pass.PtrRequest = (uint64_t)(uintptr_t)req;
	pass.PtrReply = (uint64_t)(uintptr_t)reply;
	pass.PtrData = (uint64_t)(uintptr_t)data_in;
	pass.PtrDataOut = (uint64_t)(uintptr_t)data_out;
	pass.RequestSize = req_len;
	pass.ReplySize = reply_len;
	pass.DataSize = datain_len;
	pass.DataOutSize = dataout_len;
	if (datain_len && dataout_len) {
		if (is_mps) {
			pass.DataDirection = MPS_PASS_THRU_DIRECTION_BOTH;
		} else {
			pass.DataDirection = MPR_PASS_THRU_DIRECTION_BOTH;
		}
	} else if (datain_len) {
		if (is_mps) {
			pass.DataDirection = MPS_PASS_THRU_DIRECTION_READ;
		} else {
			pass.DataDirection = MPR_PASS_THRU_DIRECTION_READ;
		}
	} else if (dataout_len) {
		if (is_mps) {
			pass.DataDirection = MPS_PASS_THRU_DIRECTION_WRITE;
		} else {
			pass.DataDirection = MPR_PASS_THRU_DIRECTION_WRITE;
		}
	} else {
		if (is_mps) {
			pass.DataDirection = MPS_PASS_THRU_DIRECTION_NONE;
		} else {
			pass.DataDirection = MPR_PASS_THRU_DIRECTION_NONE;
		}
	}
	pass.Timeout = timeout;

	if (ioctl(fd, MPTIOCTL_PASS_THRU, &pass) < 0)
		return (errno);
	return (0);
}

MPI2_IOC_FACTS_REPLY *
mps_get_iocfacts(int fd)
{
	MPI2_IOC_FACTS_REPLY *facts;
	MPI2_IOC_FACTS_REQUEST req;
	int error;

	facts = malloc(sizeof(MPI2_IOC_FACTS_REPLY));
	if (facts == NULL) {
		errno = ENOMEM;
		return (NULL);
	}

	bzero(&req, sizeof(MPI2_IOC_FACTS_REQUEST));
	req.Function = MPI2_FUNCTION_IOC_FACTS;

#if 1
	error = mps_pass_command(fd, &req, sizeof(MPI2_IOC_FACTS_REQUEST),
	    facts, sizeof(MPI2_IOC_FACTS_REPLY), NULL, 0, NULL, 0, 10);
#else
	error = mps_user_command(fd, &req, sizeof(MPI2_IOC_FACTS_REQUEST),
	    facts, sizeof(MPI2_IOC_FACTS_REPLY), NULL, 0, 0);
#endif
	if (error) {
		free(facts);
		return (NULL);
	}

	if (!IOC_STATUS_SUCCESS(facts->IOCStatus)) {
		free(facts);
		errno = EINVAL;
		return (NULL);
	}
	return (facts);
}

