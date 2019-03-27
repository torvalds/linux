/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Yahoo!, Inc.
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
 *
 * $FreeBSD$
 */

#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mfiutil.h"
#include <dev/mfi/mfi_ioctl.h>

MFI_TABLE(top, ctrlprop);

static int
mfi_ctrl_get_properties(int fd, struct mfi_ctrl_props *info)
{

	return (mfi_dcmd_command(fd, MFI_DCMD_CTRL_GET_PROPS, info,
	    sizeof(struct mfi_ctrl_props), NULL, 0, NULL));
}

static int
mfi_ctrl_set_properties(int fd, struct mfi_ctrl_props *info)
{

	return (mfi_dcmd_command(fd, MFI_DCMD_CTRL_SET_PROPS, info,
	    sizeof(struct mfi_ctrl_props), NULL, 0, NULL));
}

/*
 * aquite the controller properties data structure modify the 
 * rebuild rate if requested and then retun
 */
static int
mfi_ctrl_rebuild_rate(int ac, char **av)
{
	int error, fd;
	struct mfi_ctrl_props ctrl_props;

	if (ac > 2) {
		warn("mfi_ctrl_set_rebuild_rate");
		return(-1);
	}
		
	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_ctrl_get_properties(fd, &ctrl_props);
	if ( error < 0) {
		error = errno;
		warn("Failed to get controller properties");
		close(fd);
		return (error);
	}
	/*
	 * User requested a change to the rebuild rate
	 */
	if (ac > 1) {
		ctrl_props.rebuild_rate = atoi(av[ac - 1]);
		error = mfi_ctrl_set_properties(fd, &ctrl_props);
		if ( error < 0) {
			error = errno;
			warn("Failed to set controller properties");
			close(fd);
			return (error);
		}

		error = mfi_ctrl_get_properties(fd, &ctrl_props);
		if ( error < 0) {
			error = errno;
			warn("Failed to get controller properties");
			close(fd);
			return (error);
		}
	}
	printf ("controller rebuild rate: %%%u \n",
		ctrl_props.rebuild_rate);
	return (0);
}
MFI_COMMAND(ctrlprop, rebuild, mfi_ctrl_rebuild_rate);

static int
mfi_ctrl_alarm_enable(int ac, char **av)
{
	int error, fd;
	struct mfi_ctrl_props ctrl_props;

	if (ac > 2) {
		warn("mfi_ctrl_alarm_enable");
		return(-1);
	}
		
	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_ctrl_get_properties(fd, &ctrl_props);
	if ( error < 0) {
		error = errno;
		warn("Failed to get controller properties");
		close(fd);
		return (error);
	}
	printf ("controller alarm was : %s\n",
		(ctrl_props.alarm_enable ? "enabled" : "disabled"));

	if (ac > 1) {
		ctrl_props.alarm_enable = atoi(av[ac - 1]);
		error = mfi_ctrl_set_properties(fd, &ctrl_props);
		if ( error < 0) {
			error = errno;
			warn("Failed to set controller properties");
			close(fd);
			return (error);
		}

		error = mfi_ctrl_get_properties(fd, &ctrl_props);
		if ( error < 0) {
			error = errno;
			warn("Failed to get controller properties");
			close(fd);
			return (error);
		}
	}
	printf ("controller alarm was : %s\n",
		(ctrl_props.alarm_enable ? "enabled" : "disabled"));
	return (0);
}

MFI_COMMAND(ctrlprop, alarm, mfi_ctrl_alarm_enable);
