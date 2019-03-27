/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008, 2009 Yahoo!, Inc.
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "mfiutil.h"
#include <dev/mfi/mfi_ioctl.h>

static const char *mfi_status_codes[] = {
	"Command completed successfully",
	"Invalid command",
	"Invalid DMCD opcode",
	"Invalid parameter",
	"Invalid Sequence Number",
	"Abort isn't possible for the requested command",
	"Application 'host' code not found",
	"Application in use",
	"Application not initialized",
	"Array index invalid",
	"Array row not empty",
	"Configuration resource conflict",
	"Device not found",
	"Drive too small",
	"Flash memory allocation failed",
	"Flash download already in progress",
	"Flash operation failed",
	"Bad flash image",
	"Incomplete flash image",
	"Flash not open",
	"Flash not started",
	"Flush failed",
	"Specified application doesn't have host-resident code",
	"Volume consistency check in progress",
	"Volume initialization in progress",
	"Volume LBA out of range",
	"Maximum number of volumes are already configured",
	"Volume is not OPTIMAL",
	"Volume rebuild in progress",
	"Volume reconstruction in progress",
	"Volume RAID level is wrong for requested operation",
	"Too many spares assigned",
	"Scratch memory not available",
	"Error writing MFC data to SEEPROM",
	"Required hardware is missing",
	"Item not found",
	"Volume drives are not within an enclosure",
	"Drive clear in progress",
	"Drive type mismatch (SATA vs SAS)",
	"Patrol read disabled",
	"Invalid row index",
	"SAS Config - Invalid action",
	"SAS Config - Invalid data",
	"SAS Config - Invalid page",
	"SAS Config - Invalid type",
	"SCSI command completed with error",
	"SCSI I/O request failed",
	"SCSI RESERVATION_CONFLICT",
	"One or more flush operations during shutdown failed",
	"Firmware time is not set",
	"Wrong firmware or drive state",
	"Volume is offline",
	"Peer controller rejected request",
	"Unable to inform peer of communication changes",
	"Volume reservation already in progress",
	"I2C errors were detected",
	"PCI errors occurred during XOR/DMA operation",
	"Diagnostics failed",
	"Unable to process command as boot messages are pending",
	"Foreign configuration is incomplete"
};

const char *
mfi_status(u_int status_code)
{
	static char buffer[16];

	if (status_code == MFI_STAT_INVALID_STATUS)
		return ("Invalid status");
	if (status_code < sizeof(mfi_status_codes) / sizeof(char *))
		return (mfi_status_codes[status_code]);
	snprintf(buffer, sizeof(buffer), "Status: 0x%02x", status_code);
	return (buffer);
}

const char *
mfi_raid_level(uint8_t primary_level, uint8_t secondary_level)
{
	static char buf[16];

	switch (primary_level) {
	case DDF_RAID0:
		return ("RAID-0");
	case DDF_RAID1:
		if (secondary_level != 0)
			return ("RAID-10");
		else
			return ("RAID-1");
	case DDF_RAID1E:
		return ("RAID-1E");
	case DDF_RAID3:
		return ("RAID-3");
	case DDF_RAID5:
		if (secondary_level != 0)
			return ("RAID-50");
		else
			return ("RAID-5");
	case DDF_RAID5E:
		return ("RAID-5E");
	case DDF_RAID5EE:
		return ("RAID-5EE");
	case DDF_RAID6:
		if (secondary_level != 0)
			return ("RAID-60");
		else
			return ("RAID-6");
	case DDF_JBOD:
		return ("JBOD");
	case DDF_CONCAT:
		return ("CONCAT");
	default:
		sprintf(buf, "LVL 0x%02x", primary_level);
		return (buf);
	}
}

static int
mfi_query_disk(int fd, uint8_t target_id, struct mfi_query_disk *info)
{

	bzero(info, sizeof(*info));
	info->array_id = target_id;
	if (ioctl(fd, MFIIO_QUERY_DISK, info) < 0)
		return (-1);
	if (!info->present) {
		errno = ENXIO;
		return (-1);
	}
	return (0);
}

const char *
mfi_volume_name(int fd, uint8_t target_id)
{
	static struct mfi_query_disk info;
	static char buf[4];

	if (mfi_query_disk(fd, target_id, &info) < 0) {
		snprintf(buf, sizeof(buf), "%d", target_id);
		return (buf);
	}
	return (info.devname);
}

int
mfi_volume_busy(int fd, uint8_t target_id)
{
	struct mfi_query_disk info;

	/* Assume it isn't mounted if we can't get information. */
	if (mfi_query_disk(fd, target_id, &info) < 0)
		return (0);
	return (info.open != 0);
}

/*
 * Check if the running kernel supports changing the RAID
 * configuration of the mfi controller.
 */
int
mfi_reconfig_supported(void)
{
	char mibname[64];
	size_t len;
	int dummy;

	len = sizeof(dummy);
	snprintf(mibname, sizeof(mibname), "dev.mfi.%d.delete_busy_volumes",
	    mfi_unit);
	return (sysctlbyname(mibname, &dummy, &len, NULL, 0) == 0);
}

int
mfi_lookup_volume(int fd, const char *name, uint8_t *target_id)
{
	struct mfi_query_disk info;
	struct mfi_ld_list list;
	char *cp;
	long val;
	u_int i;

	/* If it's a valid number, treat it as a raw target ID. */
	val = strtol(name, &cp, 0);
	if (*cp == '\0') {
		*target_id = val;
		return (0);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_LD_GET_LIST, &list, sizeof(list),
	    NULL, 0, NULL) < 0)
		return (-1);	

	for (i = 0; i < list.ld_count; i++) {
		if (mfi_query_disk(fd, list.ld_list[i].ld.v.target_id,
		    &info) < 0)
			continue;
		if (strcmp(name, info.devname) == 0) {
			*target_id = list.ld_list[i].ld.v.target_id;
			return (0);
		}
	}
	errno = EINVAL;
	return (-1);
}

int
mfi_dcmd_command(int fd, uint32_t opcode, void *buf, size_t bufsize,
    uint8_t *mbox, size_t mboxlen, uint8_t *statusp)
{
	struct mfi_ioc_passthru ioc;
	struct mfi_dcmd_frame *dcmd;
	int r;

	if ((mbox != NULL && (mboxlen == 0 || mboxlen > MFI_MBOX_SIZE)) ||
	    (mbox == NULL && mboxlen != 0)) {
		errno = EINVAL;
		return (-1);
	}

	bzero(&ioc, sizeof(ioc));
	dcmd = &ioc.ioc_frame;
	if (mbox)
		bcopy(mbox, dcmd->mbox, mboxlen);
	dcmd->header.cmd = MFI_CMD_DCMD;
	dcmd->header.timeout = 0;
	dcmd->header.flags = 0;
	dcmd->header.data_len = bufsize;
	dcmd->opcode = opcode;

	ioc.buf = buf;
	ioc.buf_size = bufsize;
	r = ioctl(fd, MFIIO_PASSTHRU, &ioc);
	if (r < 0)
		return (r);

	if (statusp != NULL)
		*statusp = dcmd->header.cmd_status;
	else if (dcmd->header.cmd_status != MFI_STAT_OK) {
		warnx("Command failed: %s",
		    mfi_status(dcmd->header.cmd_status));
		errno = EIO;
		return (-1);
	}
	return (0);
}

int
mfi_ctrl_get_info(int fd, struct mfi_ctrl_info *info, uint8_t *statusp)
{

	return (mfi_dcmd_command(fd, MFI_DCMD_CTRL_GETINFO, info,
	    sizeof(struct mfi_ctrl_info), NULL, 0, statusp));
}

int
mfi_open(int unit, int acs)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof(path), "/dev/mfi%d", unit);
	return (open(path, acs));
}

static void
print_time_humanized(uint seconds)
{

	if (seconds > 3600) {
		printf("%u:", seconds / 3600);
	}
	if (seconds > 60) {
		seconds %= 3600;
		printf("%02u:%02u", seconds / 60, seconds % 60);
	} else {
		printf("%us", seconds);
	}
}

void
mfi_display_progress(const char *label, struct mfi_progress *prog)
{
	uint seconds;

	printf("%s: %.2f%% complete after ", label,
	    (float)prog->progress * 100 / 0xffff);
	print_time_humanized(prog->elapsed_seconds);
	if (prog->progress != 0 && prog->elapsed_seconds > 10) {
		printf(" finished in ");
		seconds = (0x10000 * (uint32_t)prog->elapsed_seconds) /
		    prog->progress - prog->elapsed_seconds;
		print_time_humanized(seconds);
	}
	printf("\n");
}

int
mfi_table_handler(struct mfiutil_command **start, struct mfiutil_command **end,
    int ac, char **av)
{
	struct mfiutil_command **cmd;

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
