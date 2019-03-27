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
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mptutil.h"

MPT_TABLE(top, show);

#define	STANDALONE_STATE	"ONLINE"

static void
format_stripe(char *buf, size_t buflen, U32 stripe)
{

	humanize_number(buf, buflen, stripe * 512, "", HN_AUTOSCALE,
	    HN_B | HN_NOSPACE);
}

static void
display_stripe_map(const char *label, U32 StripeMap)
{
	char stripe[5];
	int comma, i;

	comma = 0;
	printf("%s: ", label);
	for (i = 0; StripeMap != 0; i++, StripeMap >>= 1)
		if (StripeMap & 1) {
			format_stripe(stripe, sizeof(stripe), 1 << i);
			if (comma)
				printf(", ");
			printf("%s", stripe);
			comma = 1;
		}
	printf("\n");
}

static int
show_adapter(int ac, char **av)
{
	CONFIG_PAGE_MANUFACTURING_0 *man0;
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_6 *ioc6;
	U16 IOCStatus;
	int comma, error, fd;

	if (ac != 1) {
		warnx("show adapter: extra arguments");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	man0 = mpt_read_man_page(fd, 0, NULL);
	if (man0 == NULL) {
		error = errno;
		warn("Failed to get controller info");
		close(fd);
		return (error);
	}
	if (man0->Header.PageLength < sizeof(*man0) / 4) {
		warnx("Invalid controller info");
		free(man0);
		close(fd);
		return (EINVAL);
	}
	printf("mpt%d Adapter:\n", mpt_unit);
	printf("       Board Name: %.16s\n", man0->BoardName);
	printf("   Board Assembly: %.16s\n", man0->BoardAssembly);
	printf("        Chip Name: %.16s\n", man0->ChipName);
	printf("    Chip Revision: %.16s\n", man0->ChipRevision);

	free(man0);

	ioc2 = mpt_read_ioc_page(fd, 2, &IOCStatus);
	if (ioc2 != NULL) {
		printf("      RAID Levels:");
		comma = 0;
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_IS_SUPPORT) {
			printf(" RAID0");
			comma = 1;
		}
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_IM_SUPPORT) {
			printf("%s RAID1", comma ? "," : "");
			comma = 1;
		}
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_IME_SUPPORT) {
			printf("%s RAID1E", comma ? "," : "");
			comma = 1;
		}
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_RAID_5_SUPPORT) {
			printf("%s RAID5", comma ? "," : "");
			comma = 1;
		}
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_RAID_6_SUPPORT) {
			printf("%s RAID6", comma ? "," : "");
			comma = 1;
		}
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_RAID_10_SUPPORT) {
			printf("%s RAID10", comma ? "," : "");
			comma = 1;
		}
		if (ioc2->CapabilitiesFlags &
		    MPI_IOCPAGE2_CAP_FLAGS_RAID_50_SUPPORT) {
			printf("%s RAID50", comma ? "," : "");
			comma = 1;
		}
		if (!comma)
			printf(" none");
		printf("\n");
		free(ioc2);
	} else if ((IOCStatus & MPI_IOCSTATUS_MASK) !=
	    MPI_IOCSTATUS_CONFIG_INVALID_PAGE)
		warnx("mpt_read_ioc_page(2): %s", mpt_ioc_status(IOCStatus));

	ioc6 = mpt_read_ioc_page(fd, 6, &IOCStatus);
	if (ioc6 != NULL) {
		display_stripe_map("    RAID0 Stripes",
		    ioc6->SupportedStripeSizeMapIS);
		display_stripe_map("   RAID1E Stripes",
		    ioc6->SupportedStripeSizeMapIME);
		printf(" RAID0 Drives/Vol: %u", ioc6->MinDrivesIS);
		if (ioc6->MinDrivesIS != ioc6->MaxDrivesIS)
			printf("-%u", ioc6->MaxDrivesIS);
		printf("\n");
		printf(" RAID1 Drives/Vol: %u", ioc6->MinDrivesIM);
		if (ioc6->MinDrivesIM != ioc6->MaxDrivesIM)
			printf("-%u", ioc6->MaxDrivesIM);
		printf("\n");
		printf("RAID1E Drives/Vol: %u", ioc6->MinDrivesIME);
		if (ioc6->MinDrivesIME != ioc6->MaxDrivesIME)
			printf("-%u", ioc6->MaxDrivesIME);
		printf("\n");
		free(ioc6);
	} else if ((IOCStatus & MPI_IOCSTATUS_MASK) !=
	    MPI_IOCSTATUS_CONFIG_INVALID_PAGE)
		warnx("mpt_read_ioc_page(6): %s", mpt_ioc_status(IOCStatus));

	/* TODO: Add an ioctl to fetch IOC_FACTS and print firmware version. */

	close(fd);

	return (0);
}
MPT_COMMAND(show, adapter, show_adapter);

static void
print_vol(CONFIG_PAGE_RAID_VOL_0 *info, int state_len)
{
	uint64_t size;
	const char *level, *state;
	char buf[6], stripe[5];

	size = ((uint64_t)info->MaxLBAHigh << 32) | info->MaxLBA;
	humanize_number(buf, sizeof(buf), (size + 1) * 512, "", HN_AUTOSCALE,
	    HN_B | HN_NOSPACE | HN_DECIMAL);
	if (info->VolumeType == MPI_RAID_VOL_TYPE_IM)
		stripe[0] = '\0';
	else
		format_stripe(stripe, sizeof(stripe), info->StripeSize);
	level = mpt_raid_level(info->VolumeType);
	state = mpt_volstate(info->VolumeStatus.State);
	if (state_len > 0)
		printf("(%6s) %-8s %6s %-*s", buf, level, stripe, state_len,
		    state);
	else if (stripe[0] != '\0')
		printf("(%s) %s %s %s", buf, level, stripe, state);
	else
		printf("(%s) %s %s", buf, level, state);
}

static void
print_pd(CONFIG_PAGE_RAID_PHYS_DISK_0 *info, int state_len, int location)
{
	const char *inq, *state;
	char buf[6];

	humanize_number(buf, sizeof(buf), ((uint64_t)info->MaxLBA + 1) * 512,
	    "", HN_AUTOSCALE, HN_B | HN_NOSPACE |HN_DECIMAL);
	state = mpt_pdstate(info);
	if (state_len > 0)
		printf("(%6s) %-*s", buf, state_len, state);
	else
		printf("(%s) %s", buf, state);
	inq = mpt_pd_inq_string(info);
	if (inq != NULL)
		printf(" %s", inq);
	if (!location)
		return;
	printf(" bus %d id %d", info->PhysDiskBus, info->PhysDiskID);
}

static void
print_standalone(struct mpt_standalone_disk *disk, int state_len, int location)
{
	char buf[6];

	humanize_number(buf, sizeof(buf), (disk->maxlba + 1) * 512,
	    "", HN_AUTOSCALE, HN_B | HN_NOSPACE |HN_DECIMAL);
	if (state_len > 0)
		printf("(%6s) %-*s", buf, state_len, STANDALONE_STATE);
	else
		printf("(%s) %s", buf, STANDALONE_STATE);
	if (disk->inqstring[0] != '\0')
		printf(" %s", disk->inqstring);
	if (!location)
		return;
	printf(" bus %d id %d", disk->bus, disk->target);
}

static void
print_spare_pools(U8 HotSparePool)
{
	int i;

	if (HotSparePool == 0) {
		printf("none");
		return;
	}
	for (i = 0; HotSparePool != 0; i++) {
		if (HotSparePool & 1) {
			printf("%d", i);
			if (HotSparePool == 1)
				break;
			printf(", ");
		}
		HotSparePool >>= 1;
	}
}

static int
show_config(int ac, char **av)
{
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	CONFIG_PAGE_IOC_5 *ioc5;
	IOC_5_HOT_SPARE *spare;
	CONFIG_PAGE_RAID_VOL_0 *vinfo;
	RAID_VOL0_PHYS_DISK *disk;
	CONFIG_PAGE_RAID_VOL_1 *vnames;
	CONFIG_PAGE_RAID_PHYS_DISK_0 *pinfo;
	struct mpt_standalone_disk *sdisks;
	int error, fd, i, j, nsdisks;

	if (ac != 1) {
		warnx("show config: extra arguments");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	/* Get the config from the controller. */
	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	ioc5 = mpt_read_ioc_page(fd, 5, NULL);
	if (ioc2 == NULL || ioc5 == NULL) {
		error = errno;
		warn("Failed to get config");
		free(ioc2);
		close(fd);
		return (error);
	}
	if (mpt_fetch_disks(fd, &nsdisks, &sdisks) < 0) {
		error = errno;
		warn("Failed to get standalone drive list");
		free(ioc5);
		free(ioc2);
		close(fd);
		return (error);
	}

	/* Dump out the configuration. */
	printf("mpt%d Configuration: %d volumes, %d drives\n",
	    mpt_unit, ioc2->NumActiveVolumes, ioc2->NumActivePhysDisks +
	    nsdisks);
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		printf("    volume %s ", mpt_volume_name(vol->VolumeBus,
		    vol->VolumeID));
		vinfo = mpt_vol_info(fd, vol->VolumeBus, vol->VolumeID, NULL);
		if (vinfo == NULL) {
			printf("%s UNKNOWN", mpt_raid_level(vol->VolumeType));
		} else
			print_vol(vinfo, -1);
		vnames = mpt_vol_names(fd, vol->VolumeBus, vol->VolumeID, NULL);
		if (vnames != NULL) {
			if (vnames->Name[0] != '\0')
				printf(" <%s>", vnames->Name);
			free(vnames);
		}
		if (vinfo == NULL) {
			printf("\n");
			continue;
		}
		printf(" spans:\n");
		disk = vinfo->PhysDisk;
		for (j = 0; j < vinfo->NumPhysDisks; disk++, j++) {
			printf("        drive %u ", disk->PhysDiskNum);
			pinfo = mpt_pd_info(fd, disk->PhysDiskNum, NULL);
			if (pinfo != NULL) {
				print_pd(pinfo, -1, 0);
				free(pinfo);
			}
			printf("\n");
		}
		if (vinfo->VolumeSettings.HotSparePool != 0) {
			printf("        spare pools: ");
			print_spare_pools(vinfo->VolumeSettings.HotSparePool);
			printf("\n");
		}
		free(vinfo);
	}

	spare = ioc5->HotSpare;
	for (i = 0; i < ioc5->NumHotSpares; spare++, i++) {
		printf("    spare %u ", spare->PhysDiskNum);
		pinfo = mpt_pd_info(fd, spare->PhysDiskNum, NULL);
		if (pinfo != NULL) {
			print_pd(pinfo, -1, 0);
			free(pinfo);
		}
		printf(" backs pool %d\n", ffs(spare->HotSparePool) - 1);
	}
	for (i = 0; i < nsdisks; i++) {
		printf("    drive %s ", sdisks[i].devname);
		print_standalone(&sdisks[i], -1, 0);
		printf("\n");
	}
	free(ioc2);
	free(ioc5);
	free(sdisks);
	close(fd);

	return (0);
}
MPT_COMMAND(show, config, show_config);

static int
show_volumes(int ac, char **av)
{
	CONFIG_PAGE_IOC_2 *ioc2;
	CONFIG_PAGE_IOC_2_RAID_VOL *vol;
	CONFIG_PAGE_RAID_VOL_0 **volumes;
	CONFIG_PAGE_RAID_VOL_1 *vnames;
	int error, fd, i, len, state_len;

	if (ac != 1) {
		warnx("show volumes: extra arguments");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	/* Get the volume list from the controller. */
	ioc2 = mpt_read_ioc_page(fd, 2, NULL);
	if (ioc2 == NULL) {
		error = errno;
		warn("Failed to get volume list");
		return (error);
	}

	/*
	 * Go ahead and read the info for all the volumes and figure
	 * out the maximum width of the state field.
	 */
	volumes = malloc(sizeof(*volumes) * ioc2->NumActiveVolumes);
	state_len = strlen("State");
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		volumes[i] = mpt_vol_info(fd, vol->VolumeBus, vol->VolumeID,
		    NULL);
		if (volumes[i] == NULL)
			len = strlen("UNKNOWN");
		else
			len = strlen(mpt_volstate(
			    volumes[i]->VolumeStatus.State));
		if (len > state_len)
			state_len = len;
	}
	printf("mpt%d Volumes:\n", mpt_unit);
	printf("  Id     Size    Level   Stripe ");
	len = state_len - strlen("State");
	for (i = 0; i < (len + 1) / 2; i++)
		printf(" ");
	printf("State");
	for (i = 0; i < len / 2; i++)
		printf(" ");
	printf(" Write-Cache  Name\n");
	vol = ioc2->RaidVolume;
	for (i = 0; i < ioc2->NumActiveVolumes; vol++, i++) {
		printf("%6s ", mpt_volume_name(vol->VolumeBus, vol->VolumeID));
		if (volumes[i] != NULL)
			print_vol(volumes[i], state_len);
		else
			printf("         %-8s %-*s",
			    mpt_raid_level(vol->VolumeType), state_len,
			    "UNKNOWN");
		if (volumes[i] != NULL) {
			if (volumes[i]->VolumeSettings.Settings &
			    MPI_RAIDVOL0_SETTING_WRITE_CACHING_ENABLE)
				printf("   Enabled   ");
			else
				printf("   Disabled  ");
		} else
			printf("             ");
		free(volumes[i]);
		vnames = mpt_vol_names(fd, vol->VolumeBus, vol->VolumeID, NULL);
		if (vnames != NULL) {
			if (vnames->Name[0] != '\0')
				printf(" <%s>", vnames->Name);
			free(vnames);
		}
		printf("\n");
	}
	free(volumes);
	free(ioc2);
	close(fd);

	return (0);
}
MPT_COMMAND(show, volumes, show_volumes);

static int
show_drives(int ac, char **av)
{
	struct mpt_drive_list *list;
	struct mpt_standalone_disk *sdisks;
	int error, fd, i, len, nsdisks, state_len;

	if (ac != 1) {
		warnx("show drives: extra arguments");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	/* Get the drive list. */
	list = mpt_pd_list(fd);
	if (list == NULL) {
		error = errno;
		close(fd);
		warn("Failed to get drive list");
		return (error);
	}

	/* Fetch the list of standalone disks for this controller. */
	state_len = 0;
	if (mpt_fetch_disks(fd, &nsdisks, &sdisks) != 0) {
		nsdisks = 0;
		sdisks = NULL;
	}
	if (nsdisks != 0)
		state_len = strlen(STANDALONE_STATE);

	/* Walk the drive list to determine width of state column. */
	for (i = 0; i < list->ndrives; i++) {
		len = strlen(mpt_pdstate(list->drives[i]));
		if (len > state_len)
			state_len = len;
	}

	/* List the drives. */
	printf("mpt%d Physical Drives:\n", mpt_unit);
	for (i = 0; i < list->ndrives; i++) {
		printf("%4u ", list->drives[i]->PhysDiskNum);
		print_pd(list->drives[i], state_len, 1);
		printf("\n");
	}
	mpt_free_pd_list(list);
	for (i = 0; i < nsdisks; i++) {
		printf("%4s ", sdisks[i].devname);
		print_standalone(&sdisks[i], state_len, 1);
		printf("\n");
	}
	free(sdisks);

	close(fd);

	return (0);
}
MPT_COMMAND(show, drives, show_drives);

#ifdef DEBUG
static int
show_physdisks(int ac, char **av)
{
	CONFIG_PAGE_RAID_PHYS_DISK_0 *pinfo;
	U16 IOCStatus;
	int error, fd, i;

	if (ac != 1) {
		warnx("show drives: extra arguments");
		return (EINVAL);
	}

	fd = mpt_open(mpt_unit);
	if (fd < 0) {
		error = errno;
		warn("mpt_open");
		return (error);
	}

	/* Try to find each possible phys disk page. */
	for (i = 0; i <= 0xff; i++) {
		pinfo = mpt_pd_info(fd, i, &IOCStatus);
		if (pinfo == NULL) {
			if ((IOCStatus & MPI_IOCSTATUS_MASK) !=
			    MPI_IOCSTATUS_CONFIG_INVALID_PAGE)
				warnx("mpt_pd_info(%d): %s", i,
				    mpt_ioc_status(IOCStatus));
			continue;
		}
		printf("%3u ", i);
		print_pd(pinfo, -1, 1);
		printf("\n");
	}

	close(fd);

	return (0);
}
MPT_COMMAND(show, pd, show_physdisks);
#endif
