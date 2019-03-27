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
#include <ctype.h>
#include "mptutil.h"

MPT_TABLE(top, volume);

const char *
mpt_volstate(U8 State)
{
	static char buf[16];

	switch (State) {
	case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		return ("OPTIMAL");
	case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
		return ("DEGRADED");
	case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		return ("FAILED");
	case MPI_RAIDVOL0_STATUS_STATE_MISSING:
		return ("MISSING");
	default:
		sprintf(buf, "VSTATE 0x%02x", State);
		return (buf);
	}
}

static int
volume_name(int ac, char **av)
{
	CONFIG_PAGE_RAID_VOL_1 *vnames;
	U8 VolumeBus, VolumeID;
	int error, fd;

	if (ac != 3) {
		warnx("name: volume and name required");
		return (EINVAL);
	}

	if (strlen(av[2]) >= sizeof(vnames->Name)) {
		warnx("name: new name is too long");
		return (ENOSPC);
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

	vnames = mpt_vol_names(fd, VolumeBus, VolumeID, NULL);
	if (vnames == NULL) {
		error = errno;
		warn("Failed to fetch volume names");
		close(fd);
		return (error);
	}

	if (vnames->Header.PageType != MPI_CONFIG_PAGEATTR_CHANGEABLE) {
		warnx("Volume name is read only");
		free(vnames);
		close(fd);
		return (EOPNOTSUPP);
	}
	printf("mpt%u changing volume %s name from \"%s\" to \"%s\"\n",
	    mpt_unit, mpt_volume_name(VolumeBus, VolumeID), vnames->Name,
	    av[2]);
	bzero(vnames->Name, sizeof(vnames->Name));
	strcpy(vnames->Name, av[2]);

	if (mpt_write_config_page(fd, vnames, NULL) < 0) {
		error = errno;
		warn("Failed to set volume name");
		free(vnames);
		close(fd);
		return (error);
	}

	free(vnames);
	close(fd);

	return (0);
}
MPT_COMMAND(top, name, volume_name);

static int
volume_status(int ac, char **av)
{
	MPI_RAID_VOL_INDICATOR prog;
	RAID_VOL0_STATUS VolumeStatus;
	uint64_t total, remaining;
	float pct;
	U8 VolumeBus, VolumeID;
	int error, fd;

	if (ac != 2) {
		warnx("volume status: %s", ac > 2 ? "extra arguments" :
		    "volume required");
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
		close(fd);
		return (error);
	}

	error = mpt_raid_action(fd, MPI_RAID_ACTION_INDICATOR_STRUCT, VolumeBus,
	    VolumeID, 0, 0, NULL, 0, &VolumeStatus, (U32 *)&prog, sizeof(prog),
	    NULL, NULL, 0);
	if (error) {
		warnc(error, "Fetching volume status failed");
		close(fd);
		return (error);
	}

	printf("Volume %s status:\n", mpt_volume_name(VolumeBus, VolumeID));
	printf("    state: %s\n", mpt_volstate(VolumeStatus.State));
	printf("    flags:");
	if (VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_ENABLED)
		printf(" ENABLED");
	else
		printf(" DISABLED");
	if (VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_QUIESCED)
		printf(", QUIESCED");
	if (VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS)
		printf(", REBUILDING");
	if (VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE)
		printf(", INACTIVE");
	if (VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_BAD_BLOCK_TABLE_FULL)
		printf(", BAD BLOCK TABLE FULL");
	printf("\n");
	if (VolumeStatus.Flags & MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) {
		total = (uint64_t)prog.TotalBlocks.High << 32 |
		    prog.TotalBlocks.Low;
		remaining = (uint64_t)prog.BlocksRemaining.High << 32 |
		    prog.BlocksRemaining.Low;
		pct = (float)(total - remaining) * 100 / total;
		printf("   resync: %.2f%% complete\n", pct);
	}

	close(fd);
	return (0);
}
MPT_COMMAND(volume, status, volume_status);

static int
volume_cache(int ac, char **av)
{
	CONFIG_PAGE_RAID_VOL_0 *volume;
	U32 Settings, NewSettings;
	U8 VolumeBus, VolumeID;
	char *s1;
	int error, fd;

	if (ac != 3) {
		warnx("volume cache: %s", ac > 3 ? "extra arguments" :
		    "missing arguments");
		return (EINVAL);
	}

        for (s1 = av[2]; *s1 != '\0'; s1++)
                *s1 = tolower(*s1);
	if ((strcmp(av[2], "enable")) && (strcmp(av[2], "enabled")) &&
	    (strcmp(av[2], "disable")) && (strcmp(av[2], "disabled"))) {
		warnx("volume cache: invalid flag, must be 'enable' or 'disable'\n");
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
		close(fd);
		return (error);
	}

	volume = mpt_vol_info(fd, VolumeBus, VolumeID, NULL);
	if (volume == NULL) {
		close(fd);
		return (errno);
	}

	Settings = volume->VolumeSettings.Settings;

	NewSettings = Settings;
	if (strncmp(av[2], "enable", sizeof("enable")) == 0)
		NewSettings |= 0x01;
	if (strncmp(av[2], "disable", sizeof("disable")) == 0)
		NewSettings &= ~0x01;

	if (NewSettings == Settings) {
		warnx("volume cache unchanged");
		free(volume);
		close(fd);
		return (0);
	}

	volume->VolumeSettings.Settings = NewSettings;
	error = mpt_raid_action(fd, MPI_RAID_ACTION_CHANGE_VOLUME_SETTINGS,
	    VolumeBus, VolumeID, 0, *(U32 *)&volume->VolumeSettings, NULL, 0,
	    NULL, NULL, 0, NULL, NULL, 0);
	if (error)
		warnc(error, "volume cache change failed");

	close(fd);
	return (error);
}
MPT_COMMAND(volume, cache, volume_cache);
