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
#include <err.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#ifdef DEBUG
#include <stdint.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mptutil.h"

#ifdef DEBUG
static void	dump_config(CONFIG_PAGE_RAID_VOL_0 *vol);
#endif

static long
dehumanize(const char *value)
{
        char    *vtp;
        long    iv;
 
        if (value == NULL)
                return (0);
        iv = strtoq(value, &vtp, 0);
        if (vtp == value || (vtp[0] != '\0' && vtp[1] != '\0')) {
                return (0);
        }
        switch (vtp[0]) {
        case 't': case 'T':
                iv *= 1024;
                /* FALLTHROUGH */
        case 'g': case 'G':
                iv *= 1024;
                /* FALLTHROUGH */
        case 'm': case 'M':
                iv *= 1024;
                /* FALLTHROUGH */
        case 'k': case 'K':
                iv *= 1024;
                /* FALLTHROUGH */
        case '\0':
                break;
        default:
                return (0);
        }
        return (iv);
}

/*
 * Lock the volume by opening its /dev device read/write.  This will
 * only work if nothing else has it opened (including mounts).  We
 * leak the fd on purpose since this application is not long-running.
 */
int
mpt_lock_volume(U8 VolumeBus, U8 VolumeID)
{
	char path[MAXPATHLEN];
	struct mpt_query_disk qd;
	int error, vfd;

	error = mpt_query_disk(VolumeBus, VolumeID, &qd);
	if (error == ENOENT)
		/*
		 * This means there isn't a CAM device associated with
		 * the volume, and thus it is already implicitly
		 * locked, so just return.
		 */
		return (0);
	if (error) {
		warnc(error, "Unable to lookup volume device name");
		return (error);
	}
	snprintf(path, sizeof(path), "%s%s", _PATH_DEV, qd.devname);
	vfd = open(path, O_RDWR);
	if (vfd < 0) {
		error = errno;
		warn("Unable to lock volume %s", qd.devname);
		return (error);
	}
	return (0);
}

static int
mpt_lock_physdisk(struct mpt_standalone_disk *disk)
{
	char path[MAXPATHLEN];
	int dfd, error;

	snprintf(path, sizeof(path), "%s%s", _PATH_DEV, disk->devname);
	dfd = open(path, O_RDWR);
	if (dfd < 0) {
		error = errno;
		warn("Unable to lock disk %s", disk->devname);
		return (error);
	}
	return (0);
}

static int
mpt_lookup_standalone_disk(const char *name, struct mpt_standalone_disk *disks,
    int ndisks, int *index)
{
	char *cp;
	long bus, id;
	int i;

	/* Check for a raw <bus>:<id> string. */
	bus = strtol(name, &cp, 0);
	if (*cp == ':') {
		id = strtol(cp + 1, &cp, 0);
		if (*cp == '\0') {
			if (bus < 0 || bus > 0xff || id < 0 || id > 0xff) {
				return (EINVAL);
			}
			for (i = 0; i < ndisks; i++) {
				if (disks[i].bus == (U8)bus &&
				    disks[i].target == (U8)id) {
					*index = i;
					return (0);
				}
			}
			return (ENOENT);
		}
	}

	if (name[0] == 'd' && name[1] == 'a') {
		for (i = 0; i < ndisks; i++) {
			if (strcmp(name, disks[i].devname) == 0) {
				*index = i;
				return (0);
			}
		}
		return (ENOENT);
	}

	return (EINVAL);
}

/*
 * Mark a standalone disk as being a physical disk.
 */
static int
mpt_create_physdisk(int fd, struct mpt_standalone_disk *disk, U8 *PhysDiskNum)
{
	CONFIG_PAGE_HEADER header;
	CONFIG_PAGE_RAID_PHYS_DISK_0 *config_page;
	int error;
	U32 ActionData;

	error = mpt_read_config_page_header(fd, MPI_CONFIG_PAGETYPE_RAID_PHYSDISK,
	    0, 0, &header, NULL);
	if (error)
		return (error);
	if (header.PageVersion > MPI_RAIDPHYSDISKPAGE0_PAGEVERSION) {
		warnx("Unsupported RAID physdisk page 0 version %d",
		    header.PageVersion);
		return (EOPNOTSUPP);
	}		
	config_page = calloc(1, sizeof(CONFIG_PAGE_RAID_PHYS_DISK_0));
	config_page->Header.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	config_page->Header.PageNumber = 0;
	config_page->Header.PageLength = sizeof(CONFIG_PAGE_RAID_PHYS_DISK_0) /
	    4;
	config_page->PhysDiskIOC = 0;	/* XXX */
	config_page->PhysDiskBus = disk->bus;
	config_page->PhysDiskID = disk->target;

	/* XXX: Enclosure info for PhysDiskSettings? */
	error = mpt_raid_action(fd, MPI_RAID_ACTION_CREATE_PHYSDISK, 0, 0, 0, 0,
	    config_page, sizeof(CONFIG_PAGE_RAID_PHYS_DISK_0), NULL,
	    &ActionData, sizeof(ActionData), NULL, NULL, 1);
	if (error)
		return (error);
	*PhysDiskNum = ActionData & 0xff;
	return (0);
}

static int
mpt_delete_physdisk(int fd, U8 PhysDiskNum)
{

	return (mpt_raid_action(fd, MPI_RAID_ACTION_DELETE_PHYSDISK, 0, 0,
	    PhysDiskNum, 0, NULL, 0, NULL, NULL, 0, NULL, NULL, 0));
}

/*
 * MPT's firmware does not have a clear command.  Instead, we
 * implement it by deleting each array and disk by hand.
 */
static int
clear_config(int ac, char **av)
{
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	CONFIG_PAGE_IOC_3 *ioc3;
	IOC_3_PHYS_DISK *disk;
	CONFIG_PAGE_IOC_5 *ioc5;
	IOC_5_HOT_SPARE *spare;
	int ch, error, fd, i;

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (ioc2 == NULL) {
		error = errno;
		warn("Failed to fetch volume list");
		close(fd);
		return (error);
	}

	/* Lock all the volumes first. */
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		if (mpt_lock_volume(vol->VolumeBus, vol->VolumeID) < 0) {
			warnx("Volume %s is busy and cannot be deleted",
			    mpt_volume_name(vol->VolumeBus, vol->VolumeID));
			free(ioc2);
			close(fd);
			return (EBUSY);
		}
	}

	printf(
	    "Are you sure you wish to clear the configuration on mpt%u? [y/N] ",
	    mpt_unit);
	ch = getchar();
	if (ch != 'y' && ch != 'Y') {
		printf("\nAborting\n");
		free(ioc2);
		close(fd);
		return (0);
	}

	/* Delete all the volumes. */
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		error = mpt_raid_action(fd, MPI_RAID_ACTION_DELETE_VOLUME,
		    vol->VolumeBus, vol->VolumeID, 0,
		    MPI_RAID_ACTION_ADATA_DEL_PHYS_DISKS |
		    MPI_RAID_ACTION_ADATA_ZERO_LBA0, NULL, 0, NULL, NULL, 0,
		    NULL, NULL, 0);
		if (error)
			warnc(error, "Failed to delete volume %s",
			    mpt_volume_name(vol->VolumeBus, vol->VolumeID));
	}
	free(ioc2);

	/* Delete all the spares. */
	ioc5 = mpt_read_ioc_page(fd, 5, NULL);
	if (ioc5 == NULL)
		warn("Failed to fetch spare list");
	else {
		spare = ioc5->HotSpare;
		for (i = 0; i < ioc5->NumHotSpares; spare++, i++)
			if (mpt_delete_physdisk(fd, spare->PhysDiskNum) < 0)
				warn("Failed to delete physical disk %d",
				    spare->PhysDiskNum);
		free(ioc5);
	}

	/* Delete any RAID physdisks that may be left. */
	ioc3 = mpt_read_ioc_page(fd, 3, NULL);
	if (ioc3 == NULL)
		warn("Failed to fetch drive list");
	else {
		disk = ioc3->PhysDisk;
		for (i = 0; i < ioc3->NumPhysDisks; disk++, i++)
			if (mpt_delete_physdisk(fd, disk->PhysDiskNum) < 0)
				warn("Failed to delete physical disk %d",
				    disk->PhysDiskNum);
		free(ioc3);
	}

	printf("mpt%d: Configuration cleared\n", mpt_unit);
	mpt_rescan_bus(-1, -1);
	close(fd);

	return (0);
}
MPT_COMMAND(top, clear, clear_config);

#define	RT_RAID0	0
#define	RT_RAID1	1
#define	RT_RAID1E	2

static struct raid_type_entry {
	const char *name;
	int	raid_type;
} raid_type_table[] = {
	{ "raid0",	RT_RAID0 },
	{ "raid-0",	RT_RAID0 },
	{ "raid1",	RT_RAID1 },
	{ "raid-1",	RT_RAID1 },
	{ "mirror",	RT_RAID1 },
	{ "raid1e",	RT_RAID1E },
	{ "raid-1e",	RT_RAID1E },
	{ NULL,		0 },
};

struct config_id_state {
	struct mpt_standalone_disk *sdisks;
	struct mpt_drive_list *list;
	CONFIG_PAGE_IOC_2 *ioc2;
	U8	target_id;
	int	nsdisks;
};

struct drive_info {
	CONFIG_PAGE_RAID_PHYS_DISK_0 *info;
	struct mpt_standalone_disk *sdisk;
};

struct volume_info {
	int	drive_count;
	struct drive_info *drives;
};

/* Parse a comma-separated list of drives for a volume. */
static int
parse_volume(int fd, int raid_type, struct config_id_state *state,
    char *volume_str, struct volume_info *info)
{
	struct drive_info *dinfo;
	U8 PhysDiskNum;
	char *cp;
	int count, error, i;

	cp = volume_str;
	for (count = 0; cp != NULL; count++) {
		cp = strchr(cp, ',');
		if (cp != NULL) {
			cp++;
			if (*cp == ',') {
				warnx("Invalid drive list '%s'", volume_str);
				return (EINVAL);
			}
		}
	}

	/* Validate the number of drives for this volume. */
	switch (raid_type) {
	case RT_RAID0:
		if (count < 2) {
			warnx("RAID0 requires at least 2 drives in each "
			    "array");
			return (EINVAL);
		}
		break;
	case RT_RAID1:
		if (count != 2) {
			warnx("RAID1 requires exactly 2 drives in each "
			    "array");
			return (EINVAL);
		}
		break;
	case RT_RAID1E:
		if (count < 3) {
			warnx("RAID1E requires at least 3 drives in each "
			    "array");
			return (EINVAL);
		}
		break;
	}

	/* Validate each drive. */
	info->drives = calloc(count, sizeof(struct drive_info));
	info->drive_count = count;
	for (dinfo = info->drives; (cp = strsep(&volume_str, ",")) != NULL;
	     dinfo++) {
		/* If this drive is already a RAID phys just fetch the info. */
		error = mpt_lookup_drive(state->list, cp, &PhysDiskNum);
		if (error == 0) {
			dinfo->info = mpt_pd_info(fd, PhysDiskNum, NULL);
			if (dinfo->info == NULL)
				return (errno);
			continue;
		}

		/* See if it is a standalone disk. */
		if (mpt_lookup_standalone_disk(cp, state->sdisks,
		    state->nsdisks, &i) < 0) {
			error = errno;
			warn("Unable to lookup drive %s", cp);
			return (error);
		}
		dinfo->sdisk = &state->sdisks[i];

		/* Lock the disk, we will create phys disk pages later. */
		if (mpt_lock_physdisk(dinfo->sdisk) < 0)
			return (errno);
	}

	return (0);
}

/*
 * Add RAID physdisk pages for any standalone disks that a volume is
 * going to use.
 */
static int
add_drives(int fd, struct volume_info *info, int verbose)
{
	struct drive_info *dinfo;
	U8 PhysDiskNum;
	int error, i;

	for (i = 0, dinfo = info->drives; i < info->drive_count;
	     i++, dinfo++) {
		if (dinfo->info == NULL) {
			if (mpt_create_physdisk(fd, dinfo->sdisk,
			    &PhysDiskNum) < 0) {
				error = errno;
				warn(
			    "Failed to create physical disk page for %s",
				    dinfo->sdisk->devname);
				return (error);
			}
			if (verbose)
				printf("Added drive %s with PhysDiskNum %u\n",
				    dinfo->sdisk->devname, PhysDiskNum);

			dinfo->info = mpt_pd_info(fd, PhysDiskNum, NULL);
			if (dinfo->info == NULL)
				return (errno);
		}
	}
	return (0);
}

/*
 * Find the next free target ID assuming that 'target_id' is the last
 * one used.  'target_id' should be 0xff for the initial test.
 */
static U8
find_next_volume(struct config_id_state *state)
{
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	int i;

restart:
	/* Assume the current one is used. */
	state->target_id++;

	/* Search drives first. */
	for (i = 0; i < state->nsdisks; i++)
		if (state->sdisks[i].target == state->target_id)
			goto restart;
	for (i = 0; i < state->list->ndrives; i++)
		if (state->list->drives[i]->PhysDiskID == state->target_id)
			goto restart;

	/* Search volumes second. */
	vol = state->ioc2->RaidVolume;
	for (i = 0; i < state->ioc2->NumActiveVolumes; vol++, i++)
		if (vol->VolumeID == state->target_id)
			goto restart;

	return (state->target_id);
}

/* Create a volume and populate it with drives. */
static CONFIG_PAGE_RAID_VOL_0 *
build_volume(int fd, struct volume_info *info, int raid_type, long stripe_size,
    struct config_id_state *state, int verbose)
{
	CONFIG_PAGE_HEADER header;
	CONFIG_PAGE_RAID_VOL_0 *vol;
	RAID_VOL0_PHYS_DISK *rdisk;
	struct drive_info *dinfo;
        U32 MinLBA;
	uint64_t MaxLBA;
	size_t page_size;
	int error, i;

	error = mpt_read_config_page_header(fd, MPI_CONFIG_PAGETYPE_RAID_VOLUME,
	    0, 0, &header, NULL);
	if (error) {
		errno = error;
		return (NULL);
	}
	if (header.PageVersion > MPI_RAIDVOLPAGE0_PAGEVERSION) {
		warnx("Unsupported RAID volume page 0 version %d",
		    header.PageVersion);
		errno = EOPNOTSUPP;
		return (NULL);
	}
	page_size = sizeof(CONFIG_PAGE_RAID_VOL_0) +
	    sizeof(RAID_VOL0_PHYS_DISK) * (info->drive_count - 1);
	vol = calloc(1, page_size);
	if (vol == NULL)
		return (NULL);

	/* Header */
	vol->Header.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	vol->Header.PageNumber = 0;
	vol->Header.PageLength = page_size / 4;

	/* Properties */
	vol->VolumeID = find_next_volume(state);
	vol->VolumeBus = 0;
	vol->VolumeIOC = 0;	/* XXX */
	vol->VolumeStatus.Flags = MPI_RAIDVOL0_STATUS_FLAG_ENABLED;
	vol->VolumeStatus.State = MPI_RAIDVOL0_STATUS_STATE_OPTIMAL;
	vol->VolumeSettings.Settings = MPI_RAIDVOL0_SETTING_USE_DEFAULTS;
	vol->VolumeSettings.HotSparePool = MPI_RAID_HOT_SPARE_POOL_0;
	vol->NumPhysDisks = info->drive_count;

	/* Find the smallest drive. */
	MinLBA = info->drives[0].info->MaxLBA;
	for (i = 1; i < info->drive_count; i++)
		if (info->drives[i].info->MaxLBA < MinLBA)
			MinLBA = info->drives[i].info->MaxLBA;

	/*
	 * Now chop off 512MB at the end to leave room for the
	 * metadata.  The controller might only use 64MB, but we just
	 * chop off the max to be simple.
	 */
	MinLBA -= (512 * 1024 * 1024) / 512;

	switch (raid_type) {
	case RT_RAID0:
		vol->VolumeType = MPI_RAID_VOL_TYPE_IS;
		vol->StripeSize = stripe_size / 512;
		MaxLBA = (uint64_t)MinLBA * info->drive_count;
		break;
	case RT_RAID1:
		vol->VolumeType = MPI_RAID_VOL_TYPE_IM;
		MaxLBA = (uint64_t)MinLBA * (info->drive_count / 2);
		break;
	case RT_RAID1E:
		vol->VolumeType = MPI_RAID_VOL_TYPE_IME;
		vol->StripeSize = stripe_size / 512;
		MaxLBA = (uint64_t)MinLBA * info->drive_count / 2;
		break;
	default:
		/* Pacify gcc. */
		abort();		
	}

	/*
	 * If the controller doesn't support 64-bit addressing and the
	 * new volume is larger than 2^32 blocks, warn the user and
	 * truncate the volume.
	 */
	if (MaxLBA >> 32 != 0 &&
	    !(state->ioc2->CapabilitiesFlags &
	    MPI_IOCPAGE2_CAP_FLAGS_RAID_64_BIT_ADDRESSING)) {
		warnx(
	    "Controller does not support volumes > 2TB, truncating volume.");
		MaxLBA = 0xffffffff;
	}
	vol->MaxLBA = MaxLBA;
	vol->MaxLBAHigh = MaxLBA >> 32;

	/* Populate drives. */
	for (i = 0, dinfo = info->drives, rdisk = vol->PhysDisk;
	     i < info->drive_count; i++, dinfo++, rdisk++) {
		if (verbose)
			printf("Adding drive %u (%u:%u) to volume %u:%u\n",
			    dinfo->info->PhysDiskNum, dinfo->info->PhysDiskBus,
			    dinfo->info->PhysDiskID, vol->VolumeBus,
			    vol->VolumeID);
		if (raid_type == RT_RAID1) {
			if (i == 0)
				rdisk->PhysDiskMap =
				    MPI_RAIDVOL0_PHYSDISK_PRIMARY;
			else
				rdisk->PhysDiskMap =
				    MPI_RAIDVOL0_PHYSDISK_SECONDARY;
		} else
			rdisk->PhysDiskMap = i;
		rdisk->PhysDiskNum = dinfo->info->PhysDiskNum;
	}

	return (vol);
}

static int
create_volume(int ac, char **av)
{
	CONFIG_PAGE_RAID_VOL_0 *vol;
	struct config_id_state state;
	struct volume_info *info;
	long stripe_size;
	int ch, error, fd, i, quick, raid_type, verbose;
#ifdef DEBUG
	int dump;
#endif

	if (ac < 2) {
		warnx("create: volume type required");
		return (EINVAL);
	}
	
	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	/* Lookup the RAID type first. */
	raid_type = -1;
	for (i = 0; raid_type_table[i].name != NULL; i++)
		if (strcasecmp(raid_type_table[i].name, av[1]) == 0) {
			raid_type = raid_type_table[i].raid_type;
			break;
		}

	if (raid_type == -1) {
		warnx("Unknown or unsupported volume type %s", av[1]);
		close(fd);
		return (EINVAL);
	}

	/* Parse any options. */
	optind = 2;
#ifdef DEBUG
	dump = 0;
#endif
	quick = 0;
	verbose = 0;
	stripe_size = 64 * 1024;

	while ((ch = getopt(ac, av, "dqs:v")) != -1) {
		switch (ch) {
#ifdef DEBUG
		case 'd':
			dump = 1;
			break;
#endif
		case 'q':
			quick = 1;
			break;
		case 's':
			stripe_size = dehumanize(optarg);
			if ((stripe_size < 512) || (!powerof2(stripe_size))) {
				warnx("Invalid stripe size %s", optarg);
				close(fd);
				return (EINVAL);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			close(fd);
			return (EINVAL);
		}
	}
	ac -= optind;
	av += optind;

	/* Fetch existing config data. */
	state.ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (state.ioc2 == NULL) {
		error = errno;
		warn("Failed to read volume list");
		close(fd);
		return (error);
	}
	state.list = mpt_pd_list(fd);
	if (state.list == NULL) {
		close(fd);
		return (errno);
	}
	error = mpt_fetch_disks(fd, &state.nsdisks, &state.sdisks);
	if (error) {
		warn("Failed to fetch standalone disk list");
		close(fd);
		return (error);
	}	
	state.target_id = 0xff;
	
	/* Parse the drive list. */
	if (ac != 1) {
		warnx("Exactly one drive list is required");
		close(fd);
		return (EINVAL);
	}
	info = calloc(1, sizeof(*info));
	if (info == NULL) {
		close(fd);
		return (ENOMEM);
	}
	error = parse_volume(fd, raid_type, &state, av[0], info);
	if (error) {
		free(info);
		close(fd);
		return (error);
	}

	/* Create RAID physdisk pages for standalone disks. */
	error = add_drives(fd, info, verbose);
	if (error) {
		free(info);
		close(fd);
		return (error);
	}

	/* Build the volume. */
	vol = build_volume(fd, info, raid_type, stripe_size, &state, verbose);
	if (vol == NULL) {
		free(info);
		close(fd);
		return (errno);
	}

#ifdef DEBUG
	if (dump) {
		dump_config(vol);
		goto skip;
	}
#endif

	/* Send the new volume to the controller. */
	error = mpt_raid_action(fd, MPI_RAID_ACTION_CREATE_VOLUME, vol->VolumeBus,
	    vol->VolumeID, 0, quick ? MPI_RAID_ACTION_ADATA_DO_NOT_SYNC : 0,
	    vol, vol->Header.PageLength * 4, NULL, NULL, 0, NULL, NULL, 1);
	if (error) {
		errno = error;
		warn("Failed to add volume");
		free(info);
		close(fd);
		return (error);
	}

#ifdef DEBUG
skip:
#endif
	mpt_rescan_bus(vol->VolumeBus, vol->VolumeID);

	/* Clean up. */
	free(vol);
	free(info);
	free(state.sdisks);
	mpt_free_pd_list(state.list);
	free(state.ioc2);
	close(fd);

	return (0);
}
MPT_COMMAND(top, create, create_volume);

static int
delete_volume(int ac, char **av)
{
	U8 VolumeBus, VolumeID;
	int error, fd;

	if (ac != 2) {
		warnx("delete: volume required");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	error = mpt_lookup_volume(fd, av[1], &VolumeBus, &VolumeID);
	if (error) {
		warnc(error, "Invalid volume %s", av[1]);
		close(fd);
		return (error);
	}

	if (mpt_lock_volume(VolumeBus, VolumeID) < 0) {
		close(fd);
		return (errno);
	}

	error = mpt_raid_action(fd, MPI_RAID_ACTION_DELETE_VOLUME, VolumeBus,
	    VolumeID, 0, MPI_RAID_ACTION_ADATA_DEL_PHYS_DISKS |
	    MPI_RAID_ACTION_ADATA_ZERO_LBA0, NULL, 0, NULL, NULL, 0, NULL,
	    NULL, 0);
	if (error) {
		warnc(error, "Failed to delete volume");
		close(fd);
		return (error);
	}

	mpt_rescan_bus(-1, -1);
	close(fd);

	return (0);
}
MPT_COMMAND(top, delete, delete_volume);

static int
find_volume_spare_pool(int fd, const char *name, int *pool)
{
	CONFIG_PAGE_RAID_VOL_0 *info;
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	U8 VolumeBus, VolumeID;
	int error, i, j, new_pool, pool_count[7];

	error = mpt_lookup_volume(fd, name, &VolumeBus, &VolumeID);
	if (error) {
		warnc(error, "Invalid volume %s", name);
		return (error);
	}

	info = mpt_vol_info(fd, VolumeBus, VolumeID, NULL);
	if (info == NULL)
		return (errno);

	/*
	 * Check for an existing pool other than pool 0 (used for
	 * global spares).
	 */
	if ((info->VolumeSettings.HotSparePool & ~MPI_RAID_HOT_SPARE_POOL_0) !=
	    0) {
		*pool = 1 << (ffs(info->VolumeSettings.HotSparePool &
		    ~MPI_RAID_HOT_SPARE_POOL_0) - 1);
		free(info);
		return (0);
	}
	free(info);

	/*
	 * Try to find a free pool.  First, figure out which pools are
	 * in use.
	 */
	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (ioc2 == NULL) {
		error = errno;
		warn("Failed to fetch volume list");
		return (error);
	}
	bzero(pool_count, sizeof(pool_count));	
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		info = mpt_vol_info(fd, vol->VolumeBus, vol->VolumeID, NULL);
		if (info == NULL)
			return (errno);
		for (j = 0; j < 7; j++)
			if (info->VolumeSettings.HotSparePool & (1 << (j + 1)))
				pool_count[j]++;
		free(info);
	}
	free(ioc2);

	/* Find the pool with the lowest use count. */
	new_pool = 0;
	for (i = 1; i < 7; i++)
		if (pool_count[i] < pool_count[new_pool])
			new_pool = i;
	new_pool++;

	/* Add this pool to the volume. */
	info = mpt_vol_info(fd, VolumeBus, VolumeID, NULL);
	if (info == NULL)
		return (error);
	info->VolumeSettings.HotSparePool |= (1 << new_pool);
	error = mpt_raid_action(fd, MPI_RAID_ACTION_CHANGE_VOLUME_SETTINGS,
	    VolumeBus, VolumeID, 0, *(U32 *)&info->VolumeSettings, NULL, 0,
	    NULL, NULL, 0, NULL, NULL, 0);
	if (error) {
		warnx("Failed to add spare pool %d to %s", new_pool,
		    mpt_volume_name(VolumeBus, VolumeID));
		free(info);
		return (error);
	}
	free(info);

	*pool = (1 << new_pool);
	return (0);
}

static int
add_spare(int ac, char **av)
{
	CONFIG_PAGE_RAID_PHYS_DISK_0 *info;
	struct mpt_standalone_disk *sdisks;
	struct mpt_drive_list *list;
	U8 PhysDiskNum;
	int error, fd, i, nsdisks, pool;

	if (ac < 2) {
		warnx("add spare: drive required");
		return (EINVAL);
	}
	if (ac > 3) {
		warnx("add spare: extra arguments");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	if (ac == 3) {
		error = find_volume_spare_pool(fd, av[2], &pool);
		if (error) {
			close(fd);
			return (error);
		}
	} else
		pool = MPI_RAID_HOT_SPARE_POOL_0;

	list = mpt_pd_list(fd);
	if (list == NULL)
		return (errno);

	error = mpt_lookup_drive(list, av[1], &PhysDiskNum);
	if (error) {
		error = mpt_fetch_disks(fd, &nsdisks, &sdisks);
		if (error != 0) {
			warn("Failed to fetch standalone disk list");
			mpt_free_pd_list(list);
			close(fd);
			return (error);
		}

		if (mpt_lookup_standalone_disk(av[1], sdisks, nsdisks, &i) <
		    0) {
			error = errno;
			warn("Unable to lookup drive %s", av[1]);
			mpt_free_pd_list(list);
			close(fd);
			return (error);
		}

		if (mpt_lock_physdisk(&sdisks[i]) < 0) {
			mpt_free_pd_list(list);
			close(fd);
			return (errno);
		}

		if (mpt_create_physdisk(fd, &sdisks[i], &PhysDiskNum) < 0) {
			error = errno;
			warn("Failed to create physical disk page");
			mpt_free_pd_list(list);
			close(fd);
			return (error);
		}
		free(sdisks);
	}
	mpt_free_pd_list(list);

	info = mpt_pd_info(fd, PhysDiskNum, NULL);
	if (info == NULL) {
		error = errno;
		warn("Failed to fetch drive info");
		close(fd);
		return (error);
	}

	info->PhysDiskSettings.HotSparePool = pool;
	error = mpt_raid_action(fd, MPI_RAID_ACTION_CHANGE_PHYSDISK_SETTINGS, 0,
	    0, PhysDiskNum, *(U32 *)&info->PhysDiskSettings, NULL, 0, NULL,
	    NULL, 0, NULL, NULL, 0);
	if (error) {
		warnc(error, "Failed to assign spare");
		close(fd);
		return (error);
	}

	free(info);
	close(fd);

	return (0);
}
MPT_COMMAND(top, add, add_spare);

static int
remove_spare(int ac, char **av)
{
	CONFIG_PAGE_RAID_PHYS_DISK_0 *info;
	struct mpt_drive_list *list;
	U8 PhysDiskNum;
	int error, fd;

	if (ac != 2) {
		warnx("remove spare: drive required");
		return (EINVAL);
	}

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

	error = mpt_lookup_drive(list, av[1], &PhysDiskNum);
	if (error) {
		warn("Failed to find drive %s", av[1]);
		close(fd);
		return (error);
	}
	mpt_free_pd_list(list);

	
	info = mpt_pd_info(fd, PhysDiskNum, NULL);
	if (info == NULL) {
		error = errno;
		warn("Failed to fetch drive info");
		close(fd);
		return (error);
	}

	if (info->PhysDiskSettings.HotSparePool == 0) {
		warnx("Drive %u is not a hot spare", PhysDiskNum);
		free(info);
		close(fd);
		return (EINVAL);
	}

	if (mpt_delete_physdisk(fd, PhysDiskNum) < 0) {
		error = errno;
		warn("Failed to delete physical disk page");
		free(info);
		close(fd);
		return (error);
	}

	mpt_rescan_bus(info->PhysDiskBus, info->PhysDiskID);
	free(info);
	close(fd);

	return (0);
}
MPT_COMMAND(top, remove, remove_spare);

#ifdef DEBUG
MPT_TABLE(top, pd);

static int
pd_create(int ac, char **av)
{
	struct mpt_standalone_disk *disks;
	int error, fd, i, ndisks;
	U8 PhysDiskNum;

	if (ac != 2) {
		warnx("pd create: drive required");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	error = mpt_fetch_disks(fd, &ndisks, &disks);
	if (error != 0) {
		warn("Failed to fetch standalone disk list");
		return (error);
	}

	if (mpt_lookup_standalone_disk(av[1], disks, ndisks, &i) < 0) {
		error = errno;
		warn("Unable to lookup drive");
		return (error);
	}

	if (mpt_lock_physdisk(&disks[i]) < 0)
		return (errno);

	if (mpt_create_physdisk(fd, &disks[i], &PhysDiskNum) < 0) {
		error = errno;
		warn("Failed to create physical disk page");
		return (error);
	}
	free(disks);

	printf("Added drive %s with PhysDiskNum %u\n", av[1], PhysDiskNum);

	close(fd);

	return (0);
}
MPT_COMMAND(pd, create, pd_create);

static int
pd_delete(int ac, char **av)
{
	CONFIG_PAGE_RAID_PHYS_DISK_0 *info;
	struct mpt_drive_list *list;
	int error, fd;
	U8 PhysDiskNum;

	if (ac != 2) {
		warnx("pd delete: drive required");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	list = mpt_pd_list(fd);
	if (list == NULL)
		return (errno);

	if (mpt_lookup_drive(list, av[1], &PhysDiskNum) < 0) {
		error = errno;
		warn("Failed to find drive %s", av[1]);
		return (error);
	}
	mpt_free_pd_list(list);

	info = mpt_pd_info(fd, PhysDiskNum, NULL);
	if (info == NULL) {
		error = errno;
		warn("Failed to fetch drive info");
		return (error);
	}

	if (mpt_delete_physdisk(fd, PhysDiskNum) < 0) {
		error = errno;
		warn("Failed to delete physical disk page");
		return (error);
	}

	mpt_rescan_bus(info->PhysDiskBus, info->PhysDiskID);
	free(info);
	close(fd);

	return (0);
}
MPT_COMMAND(pd, delete, pd_delete);

/* Display raw data about a volume config. */
static void
dump_config(CONFIG_PAGE_RAID_VOL_0 *vol)
{
	int i;

	printf("Volume Configuration (Debug):\n");
	printf(
   " Page Header: Type 0x%02x Number 0x%02x Length 0x%02x(%u) Version 0x%02x\n",
	    vol->Header.PageType, vol->Header.PageNumber,
	    vol->Header.PageLength, vol->Header.PageLength * 4,
	    vol->Header.PageVersion);
	printf("     Address: %d:%d IOC %d\n", vol->VolumeBus, vol->VolumeID,
	    vol->VolumeIOC);
	printf("        Type: %d (%s)\n", vol->VolumeType,
	    mpt_raid_level(vol->VolumeType));
	printf("      Status: %s (Flags 0x%02x)\n",
	    mpt_volstate(vol->VolumeStatus.State), vol->VolumeStatus.Flags);
	printf("    Settings: 0x%04x (Spare Pools 0x%02x)\n",
	    vol->VolumeSettings.Settings, vol->VolumeSettings.HotSparePool);
	printf("      MaxLBA: %ju\n", (uintmax_t)vol->MaxLBAHigh << 32 |
	    vol->MaxLBA);
	printf(" Stripe Size: %ld\n", (long)vol->StripeSize * 512);
	printf(" %d Disks:\n", vol->NumPhysDisks);

	for (i = 0; i < vol->NumPhysDisks; i++)
		printf("    Disk %d: Num 0x%02x Map 0x%02x\n", i,
		    vol->PhysDisk[i].PhysDiskNum, vol->PhysDisk[i].PhysDiskMap);
}

static int
debug_config(int ac, char **av)
{
	CONFIG_PAGE_RAID_VOL_0 *vol;
	U8 VolumeBus, VolumeID;
	int error, fd;

	if (ac != 2) {
		warnx("debug: volume required");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	error = mpt_lookup_volume(fd, av[1], &VolumeBus, &VolumeID);
	if (error) {
		warnc(error, "Invalid volume: %s", av[1]);
		return (error);
	}

	vol = mpt_vol_info(fd, VolumeBus, VolumeID, NULL);
	if (vol == NULL) {
		error = errno;
		warn("Failed to get volume info");
		return (error);
	}

	dump_config(vol);
	free(vol);
	close(fd);

	return (0);
}
MPT_COMMAND(top, debug, debug_config);
#endif
