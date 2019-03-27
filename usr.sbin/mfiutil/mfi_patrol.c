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

#include <sys/types.h>
#include <sys/errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mfiutil.h"

static char *
adapter_time(time_t now, uint32_t at_now, uint32_t at)
{
	time_t t;

	t = (now - at_now) + at;
	return (ctime(&t));
}

static void
mfi_get_time(int fd, uint32_t *at)
{

	if (mfi_dcmd_command(fd, MFI_DCMD_TIME_SECS_GET, at, sizeof(*at), NULL,
	    0, NULL) < 0) {
		warn("Couldn't fetch adapter time");
		at = 0;
	}
}

static int
patrol_get_props(int fd, struct mfi_pr_properties *prop)
{
	int error;

	if (mfi_dcmd_command(fd, MFI_DCMD_PR_GET_PROPERTIES, prop,
	    sizeof(*prop), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to get patrol read properties");
		return (error);
	}
	return (0);
}

static int
show_patrol(int ac __unused, char **av __unused)
{
	struct mfi_pr_properties prop;
	struct mfi_pr_status status;
	struct mfi_pd_list *list;
	struct mfi_pd_info info;
	char label[24];
	time_t now;
	uint32_t at;
	int error, fd;
	u_int i;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	time(&now);
	mfi_get_time(fd, &at);
	error = patrol_get_props(fd, &prop);
	if (error) {
		close(fd);
		return (error);
	}
	printf("Operation Mode: ");
	switch (prop.op_mode) {
	case MFI_PR_OPMODE_AUTO:
		printf("auto\n");
		break;
	case MFI_PR_OPMODE_MANUAL:
		printf("manual\n");
		break;
	case MFI_PR_OPMODE_DISABLED:
		printf("disabled\n");
		break;
	default:
		printf("??? (%02x)\n", prop.op_mode);
		break;
	}
	if (prop.op_mode == MFI_PR_OPMODE_AUTO) {
		if (at != 0 && prop.next_exec)
			printf("    Next Run Starts: %s", adapter_time(now, at,
			    prop.next_exec));
		if (prop.exec_freq == 0xffffffff)
			printf("    Runs Execute Continuously\n");
		else if (prop.exec_freq != 0)
			printf("    Runs Start Every %u seconds\n",
			    prop.exec_freq);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_PR_GET_STATUS, &status,
	    sizeof(status), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to get patrol read properties");
		close(fd);
		return (error);
	}
	printf("Runs Completed: %u\n", status.num_iteration);
	printf("Current State: ");
	switch (status.state) {
	case MFI_PR_STATE_STOPPED:
		printf("stopped\n");
		break;
	case MFI_PR_STATE_READY:
		printf("ready\n");
		break;
	case MFI_PR_STATE_ACTIVE:
		printf("active\n");
		break;
	case MFI_PR_STATE_ABORTED:
		printf("aborted\n");
		break;
	default:
		printf("??? (%02x)\n", status.state);
		break;
	}
	if (status.state == MFI_PR_STATE_ACTIVE) {
		if (mfi_pd_get_list(fd, &list, NULL) < 0) {
			error = errno;
			warn("Failed to get drive list");
			close(fd);
			return (error);
		}

		for (i = 0; i < list->count; i++) {
			if (list->addr[i].scsi_dev_type != 0)
				continue;

			if (mfi_pd_get_info(fd, list->addr[i].device_id, &info,
			    NULL) < 0) {
				error = errno;
				warn("Failed to fetch info for drive %u",
				    list->addr[i].device_id);
				free(list);
				close(fd);
				return (error);
			}
			if (info.prog_info.active & MFI_PD_PROGRESS_PATROL) {
				snprintf(label, sizeof(label), "    Drive %s",
				    mfi_drive_name(NULL,
				    list->addr[i].device_id,
				    MFI_DNAME_DEVICE_ID|MFI_DNAME_HONOR_OPTS));
				mfi_display_progress(label,
				    &info.prog_info.patrol);
			}
		}
		free(list);
	}

	close(fd);

	return (0);
}
MFI_COMMAND(show, patrol, show_patrol);

static int
start_patrol(int ac __unused, char **av __unused)
{
	int error, fd;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_PR_START, NULL, 0, NULL, 0, NULL) <
	    0) {
		error = errno;
		warn("Failed to start patrol read");
		close(fd);
		return (error);
	}

	close(fd);

	return (0);
}
MFI_COMMAND(start, patrol, start_patrol);

static int
stop_patrol(int ac __unused, char **av __unused)
{
	int error, fd;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_PR_STOP, NULL, 0, NULL, 0, NULL) <
	    0) {
		error = errno;
		warn("Failed to stop patrol read");
		close(fd);
		return (error);
	}

	close(fd);

	return (0);
}
MFI_COMMAND(stop, patrol, stop_patrol);

static int
patrol_config(int ac, char **av)
{
	struct mfi_pr_properties prop;
	long val;
	time_t now;
	int error, fd;
	uint32_t at, next_exec, exec_freq;
	char *cp;
	uint8_t op_mode;

	exec_freq = 0;	/* GCC too stupid */
	next_exec = 0;
	if (ac < 2) {
		warnx("patrol: command required");
		return (EINVAL);
	}
	if (strcasecmp(av[1], "auto") == 0) {
		op_mode = MFI_PR_OPMODE_AUTO;
		if (ac > 2) {
			if (strcasecmp(av[2], "continuously") == 0)
				exec_freq = 0xffffffff;
			else {
				val = strtol(av[2], &cp, 0);
				if (*cp != '\0') {
					warnx("patrol: Invalid interval %s",
					    av[2]);
					return (EINVAL);
				}
				exec_freq = val;
			}
		}
		if (ac > 3) {
			val = strtol(av[3], &cp, 0);
			if (*cp != '\0' || val < 0) {
				warnx("patrol: Invalid start time %s", av[3]);
				return (EINVAL);
			}
			next_exec = val;
		}
	} else if (strcasecmp(av[1], "manual") == 0)
		op_mode = MFI_PR_OPMODE_MANUAL;
	else if (strcasecmp(av[1], "disable") == 0)
		op_mode = MFI_PR_OPMODE_DISABLED;
	else {
		warnx("patrol: Invalid command %s", av[1]);
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = patrol_get_props(fd, &prop);
	if (error) {
		close(fd);
		return (error);
	}
	prop.op_mode = op_mode;
	if (op_mode == MFI_PR_OPMODE_AUTO) {
		if (ac > 2)
			prop.exec_freq = exec_freq;
		if (ac > 3) {
			time(&now);
			mfi_get_time(fd, &at);
			if (at == 0) {
				close(fd);
				return (ENXIO);
			}
			prop.next_exec = at + next_exec;
			printf("Starting next patrol read at %s",
			    adapter_time(now, at, prop.next_exec));
		}
	}
	if (mfi_dcmd_command(fd, MFI_DCMD_PR_SET_PROPERTIES, &prop,
	    sizeof(prop), NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to set patrol read properties");
		close(fd);
		return (error);
	}

	close(fd);

	return (0);
}
MFI_COMMAND(top, patrol, patrol_config);
