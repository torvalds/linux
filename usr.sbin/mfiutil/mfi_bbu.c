/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Sandvine Inc.
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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mfiutil.h"

/* The autolearn period is given in seconds. */
void
mfi_autolearn_period(uint32_t period, char *buf, size_t sz)
{
	unsigned int d, h;
	char *tmp;

	d = period / (24 * 3600);
	h = (period % (24 * 3600)) / 3600;

	tmp = buf;
	if (d != 0) {
		tmp += snprintf(buf, sz, "%u day%s", d, d == 1 ? "" : "s");
		sz -= tmp - buf;
		if (h != 0) {
			tmp += snprintf(tmp, sz, ", ");
			sz -= 2;
		}
	}
	if (h != 0)
		snprintf(tmp, sz, "%u hour%s", h, h == 1 ? "" : "s");

	if (d == 0 && h == 0)
		snprintf(tmp, sz, "less than 1 hour");
}

/* The time to the next relearn is given in seconds since 1/1/2000. */
void
mfi_next_learn_time(uint32_t next_learn_time, char *buf, size_t sz)
{
	time_t basetime;
	struct tm tm;
	size_t len;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = 100;
	basetime = timegm(&tm);
	basetime += (time_t)next_learn_time;
	len = snprintf(buf, sz, "%s", ctime(&basetime));
	if (len > 0)
		/* Get rid of the newline added by ctime(3). */
		buf[len - 1] = '\0';
}

void
mfi_autolearn_mode(uint8_t mode, char *buf, size_t sz)
{

	switch (mode) {
	case 0:
		snprintf(buf, sz, "enabled");
		break;
	case 1:
		snprintf(buf, sz, "disabled");
		break;
	case 2:
		snprintf(buf, sz, "warn via event");
		break;
	default:
		snprintf(buf, sz, "mode 0x%02x", mode);
		break;
	}
}

int
mfi_bbu_get_props(int fd, struct mfi_bbu_properties *props, uint8_t *statusp)
{

	return (mfi_dcmd_command(fd, MFI_DCMD_BBU_GET_PROP, props,
	    sizeof(*props), NULL, 0, statusp));
}

int
mfi_bbu_set_props(int fd, struct mfi_bbu_properties *props, uint8_t *statusp)
{

	return (mfi_dcmd_command(fd, MFI_DCMD_BBU_SET_PROP, props,
	    sizeof(*props), NULL, 0, statusp));
}

static int
start_bbu_learn(int ac, char **av __unused)
{
	uint8_t status;
	int error, fd;

	status = MFI_STAT_OK;
	error = 0;

	if (ac != 1) {
		warnx("start learn: unexpected arguments");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_BBU_START_LEARN, NULL, 0, NULL, 0,
	    &status) < 0) {
		error = errno;
		warn("Failed to start BBU learn");
	} else if (status != MFI_STAT_OK) {
		warnx("Failed to start BBU learn: %s", mfi_status(status));
		error = EIO;
	}

	return (error);
}
MFI_COMMAND(start, learn, start_bbu_learn);

static int
update_bbu_props(int ac, char **av)
{
	struct mfi_bbu_properties props;
	unsigned long delay;
	uint8_t status;
	int error, fd;
	char *mode, *endptr;

	status = MFI_STAT_OK;
	error = 0;

	if (ac != 3) {
		warnx("bbu: property and value required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (mfi_bbu_get_props(fd, &props, &status) < 0) {
		error = errno;
		warn("Failed to get BBU properties");
		goto done;
	} else if (status != MFI_STAT_OK) {
		warnx("Failed to get BBU properties: %s", mfi_status(status));
		error = EIO;
		goto done;
	}

	if (strcmp(av[1], "learn-delay") == 0) {
		delay = strtoul(av[2], &endptr, 10);
		if (strlen(av[2]) == 0 || *endptr != '\0' || delay > 255) {
			warnx("Invalid learn delay '%s'", av[2]);
			error = EINVAL;
			goto done;
		}

		props.learn_delay_interval = delay;
	} else if (strcmp(av[1], "autolearn-mode") == 0) {
		mode = av[2];

		if (strcmp(av[2], "enable") == 0)
			props.auto_learn_mode = 0;
		else if (strcmp(av[2], "disable") == 0)
			props.auto_learn_mode = 1;
		else if (mode[0] >= '0' && mode[0] <= '2' && mode[1] == '\0')
			props.auto_learn_mode = mode[0] - '0';
		else {
			warnx("Invalid mode '%s'", mode);
			error = EINVAL;
			goto done;
		}
	} else if (strcmp(av[1], "bbu-mode") == 0) {
		if (props.bbu_mode == 0) {
			warnx("This BBU does not implement different modes");
			error = EINVAL;
			goto done;
		}

		/* The mode must be an integer between 1 and 5. */
		mode = av[2];
		if (mode[0] < '1' || mode[0] > '5' || mode[1] != '\0') {
			warnx("Invalid mode '%s'", mode);
			error = EINVAL;
			goto done;
		}

		props.bbu_mode = mode[0] - '0';
	} else {
		warnx("bbu: Invalid command '%s'", av[1]);
		error = EINVAL;
		goto done;
	}

	if (mfi_bbu_set_props(fd, &props, &status) < 0) {
		error = errno;
		warn("Failed to set BBU properties");
		goto done;
	} else if (status != MFI_STAT_OK) {
		warnx("Failed to set BBU properties: %s", mfi_status(status));
		error = EIO;
		goto done;
	}

done:
	close(fd);

	return (error);
}
MFI_COMMAND(top, bbu, update_bbu_props);
