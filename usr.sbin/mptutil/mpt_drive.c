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
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <camlib.h>
#include <cam/scsi/scsi_all.h>

#include "mptutil.h"

const char *
mpt_pdstate(CONFIG_PAGE_RAID_PHYS_DISK_0 *info)
{
	static char buf[16];

	switch (info->PhysDiskStatus.State) {
	case MPI_PHYSDISK0_STATUS_ONLINE:
		if ((info->PhysDiskStatus.Flags &
		    MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC) &&
		    info->PhysDiskSettings.HotSparePool == 0)
			return ("REBUILD");
		else
			return ("ONLINE");
	case MPI_PHYSDISK0_STATUS_MISSING:
		return ("MISSING");
	case MPI_PHYSDISK0_STATUS_NOT_COMPATIBLE:
		return ("NOT COMPATIBLE");
	case MPI_PHYSDISK0_STATUS_FAILED:
		return ("FAILED");
	case MPI_PHYSDISK0_STATUS_INITIALIZING:
		return ("INITIALIZING");
	case MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED:
		return ("OFFLINE REQUESTED");
	case MPI_PHYSDISK0_STATUS_FAILED_REQUESTED:
		return ("FAILED REQUESTED");
	case MPI_PHYSDISK0_STATUS_OTHER_OFFLINE:
		return ("OTHER OFFLINE");
	default:
		sprintf(buf, "PSTATE 0x%02x", info->PhysDiskStatus.State);
		return (buf);
	}
}

/*
 * There are several ways to enumerate physical disks.  Unfortunately,
 * none of them are truly complete, so we have to build a union of all of
 * them.  Specifically:
 * 
 * - IOC2 : This gives us a list of volumes, and by walking the volumes we
 *          can enumerate all of the drives attached to volumes including
 *          online drives and failed drives.
 * - IOC3 : This gives us a list of all online physical drives including
 *          drives that are not part of a volume nor a spare drive.  It
 *          does not include any failed drives.
 * - IOC5 : This gives us a list of all spare drives including failed
 *          spares.
 *
 * The specific edge cases are that 1) a failed volume member can only be
 * found via IOC2, 2) a drive that is neither a volume member nor a spare
 * can only be found via IOC3, and 3) a failed spare can only be found via
 * IOC5.
 *
 * To handle this, walk all of the three lists and use the following
 * routine to add each drive encountered.  It quietly succeeds if the
 * drive is already present in the list.  It also sorts the list as it
 * inserts new drives.
 */
static int
mpt_pd_insert(int fd, struct mpt_drive_list *list, U8 PhysDiskNum)
{
	int i, j;

	/*
	 * First, do a simple linear search to see if we have already
	 * seen this drive.
	 */
	for (i = 0; i < list->ndrives; i++) {
		if (list->drives[i]->PhysDiskNum == PhysDiskNum)
			return (0);
		if (list->drives[i]->PhysDiskNum > PhysDiskNum)
			break;
	}

	/*
	 * 'i' is our slot for the 'new' drive.  Make room and then
	 * read the drive info.
	 */
	for (j = list->ndrives - 1; j >= i; j--)
		list->drives[j + 1] = list->drives[j];
	list->drives[i] = mpt_pd_info(fd, PhysDiskNum, NULL);
	if (list->drives[i] == NULL)
		return (errno);
	list->ndrives++;
	return (0);
}

struct mpt_drive_list *
mpt_pd_list(int fd)
{
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	CONFIG_PAGE_RAID_VOL_0 **volumes;
	RAID_VOL0_PHYS_DISK *rdisk;
	CONFIG_PAGE_IOC_3 *ioc3;
	IOC_3_PHYS_DISK *disk;
	CONFIG_PAGE_IOC_5 *ioc5;
	IOC_5_HOT_SPARE *spare;
	struct mpt_drive_list *list;
	int count, error, i, j;
	size_t listsize;

	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (ioc2 == NULL) {
		error = errno;
		warn("Failed to fetch volume list");
		errno = error;
		return (NULL);
	}

	ioc3 = mpt_read_ioc_page(fd, 3, NULL);
	if (ioc3 == NULL) {
		error = errno;
		warn("Failed to fetch drive list");
		free(ioc2);
		errno = error;
		return (NULL);
	}

	ioc5 = mpt_read_ioc_page(fd, 5, NULL);
	if (ioc5 == NULL) {
		error = errno;
		warn("Failed to fetch spare list");
		free(ioc3);
		free(ioc2);
		errno = error;
		return (NULL);
	}

	/*
	 * Go ahead and read the info for all the volumes.  For this
	 * pass we figure out how many physical drives there are.
	 */
	volumes = malloc(sizeof(*volumes) * ioc2->NumActiveVolumes);
	count = 0;
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		volumes[i] = mpt_vol_info(fd, vol->VolumeBus, vol->VolumeID,
		    NULL);
		if (volumes[i] == NULL) {
			error = errno;
			warn("Failed to read volume info");
			errno = error;
			free(volumes);
			free(ioc5);
			free(ioc3);
			free(ioc2);
			return (NULL);
		}
		count += volumes[i]->NumPhysDisks;
	}
	count += ioc3->NumPhysDisks;
	count += ioc5->NumHotSpares;

	/* Walk the various lists enumerating drives. */
	listsize = sizeof(*list) + sizeof(CONFIG_PAGE_RAID_PHYS_DISK_0) * count;
	list = calloc(1, listsize);

	for (i = 0; i < ioc2->NumActiveVolumes; i++) {
		rdisk = volumes[i]->PhysDisk;
		for (j = 0; j < volumes[i]->NumPhysDisks; rdisk++, j++)
			if (mpt_pd_insert(fd, list, rdisk->PhysDiskNum) < 0) {
				mpt_free_pd_list(list);
				free(volumes);
				free(ioc5);
				free(ioc3);
				free(ioc2);
				return (NULL);
			}
		free(volumes[i]);
	}
	free(ioc2);
	free(volumes);

	spare = ioc5->HotSpare;
	for (i = 0; i < ioc5->NumHotSpares; spare++, i++)
		if (mpt_pd_insert(fd, list, spare->PhysDiskNum) < 0) {
			mpt_free_pd_list(list);
			free(ioc5);
			free(ioc3);
			return (NULL);
		}
	free(ioc5);

	disk = ioc3->PhysDisk;
	for (i = 0; i < ioc3->NumPhysDisks; disk++, i++)
		if (mpt_pd_insert(fd, list, disk->PhysDiskNum) < 0) {
			mpt_free_pd_list(list);
			free(ioc3);
			return (NULL);
		}
	free(ioc3);

	return (list);
}

void
mpt_free_pd_list(struct mpt_drive_list *list)
{
	int i;

	for (i = 0; i < list->ndrives; i++)
		free(list->drives[i]);
	free(list);
}

int
mpt_lookup_drive(struct mpt_drive_list *list, const char *drive,
    U8 *PhysDiskNum)
{
	long val;
	uint8_t bus, id;
	char *cp;

	/* Look for a raw device id first. */
	val = strtol(drive, &cp, 0);
	if (*cp == '\0') {
		if (val < 0 || val > 0xff)
			goto bad;
		*PhysDiskNum = val;
		return (0);
	}

	/* Look for a <bus>:<id> string. */
	if (*cp == ':') {
		if (val < 0 || val > 0xff)
			goto bad;
		bus = val;
		val = strtol(cp + 1, &cp, 0);
		if (*cp != '\0')
			goto bad;
		if (val < 0 || val > 0xff)
			goto bad;
		id = val;

		for (val = 0; val < list->ndrives; val++) {
			if (list->drives[val]->PhysDiskBus == bus &&
			    list->drives[val]->PhysDiskID == id) {
				*PhysDiskNum = list->drives[val]->PhysDiskNum;
				return (0);
			}
		}
		return (ENOENT);
	}

bad:
	return (EINVAL);
}

/* Borrowed heavily from scsi_all.c:scsi_print_inquiry(). */
const char *
mpt_pd_inq_string(CONFIG_PAGE_RAID_PHYS_DISK_0 *pd_info)
{
	RAID_PHYS_DISK0_INQUIRY_DATA *inq_data;
	u_char vendor[9], product[17], revision[5];
	static char inq_string[64];

	inq_data = &pd_info->InquiryData;
	cam_strvis(vendor, inq_data->VendorID, sizeof(inq_data->VendorID),
	    sizeof(vendor));
	cam_strvis(product, inq_data->ProductID, sizeof(inq_data->ProductID),
	    sizeof(product));
	cam_strvis(revision, inq_data->ProductRevLevel,
	    sizeof(inq_data->ProductRevLevel), sizeof(revision));

	/* Total hack. */
	if (strcmp(vendor, "ATA") == 0)
		snprintf(inq_string, sizeof(inq_string), "<%s %s> SATA",
		    product, revision);
	else
		snprintf(inq_string, sizeof(inq_string), "<%s %s %s> SAS",
		    vendor, product, revision);
	return (inq_string);
}

/* Helper function to set a drive to a given state. */
static int
drive_set_state(char *drive, U8 Action, U8 State, const char *name)
{
	CONFIG_PAGE_RAID_PHYS_DISK_0 *info;
	struct mpt_drive_list *list;
	U8 PhysDiskNum;
	int error, fd;

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	list = mpt_pd_list(fd);
	if (list == NULL) {
		close(fd);
		return (errno);
	}

	if (mpt_lookup_drive(list, drive, &PhysDiskNum) < 0) {
		error = errno;
		warn("Failed to find drive %s", drive);
		close(fd);
		return (error);
	}
	mpt_free_pd_list(list);

	/* Get the info for this drive. */
	info = mpt_pd_info(fd, PhysDiskNum, NULL);
	if (info == NULL) {
		error = errno;
		warn("Failed to fetch info for drive %u", PhysDiskNum);
		close(fd);
		return (error);
	}

	/* Try to change the state. */
	if (info->PhysDiskStatus.State == State) {
		warnx("Drive %u is already in the desired state", PhysDiskNum);
		free(info);
		close(fd);
		return (EINVAL);
	}

	error = mpt_raid_action(fd, Action, 0, 0, PhysDiskNum, 0, NULL, 0, NULL,
	    NULL, 0, NULL, NULL, 0);
	if (error) {
		warnc(error, "Failed to set drive %u to %s", PhysDiskNum, name);
		free(info);
		close(fd);
		return (error);
	}

	free(info);
	close(fd);

	return (0);
}

static int
fail_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("fail: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MPI_RAID_ACTION_FAIL_PHYSDISK,
	    MPI_PHYSDISK0_STATUS_FAILED_REQUESTED, "FAILED"));
}
MPT_COMMAND(top, fail, fail_drive);

static int
online_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("online: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MPI_RAID_ACTION_PHYSDISK_ONLINE,
	    MPI_PHYSDISK0_STATUS_ONLINE, "ONLINE"));
}
MPT_COMMAND(top, online, online_drive);

static int
offline_drive(int ac, char **av)
{

	if (ac != 2) {
		warnx("offline: %s", ac > 2 ? "extra arguments" :
		    "drive required");
		return (EINVAL);
	}

	return (drive_set_state(av[1], MPI_RAID_ACTION_PHYSDISK_OFFLINE,
	    MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED, "OFFLINE"));
}
MPT_COMMAND(top, offline, offline_drive);
