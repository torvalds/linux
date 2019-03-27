/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 EMC Corp.
 * All rights reserved.
 *
 * Copyright (C) 2012-2013 Intel Corporation
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ioccom.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nvmecontrol.h"

#define FIRMWARE_USAGE							       \
	"firmware [-s slot] [-f path_to_firmware] [-a] <controller id>\n"

static int
slot_has_valid_firmware(int fd, int slot)
{
	struct nvme_firmware_page	fw;
	int				has_fw = false;

	read_logpage(fd, NVME_LOG_FIRMWARE_SLOT,
	    NVME_GLOBAL_NAMESPACE_TAG, &fw, sizeof(fw));

	if (fw.revision[slot-1] != 0LLU)
		has_fw = true;

	return (has_fw);
}

static void
read_image_file(char *path, void **buf, int32_t *size)
{
	struct stat	sb;
	int32_t		filesize;
	int		fd;

	*size = 0;
	*buf = NULL;

	if ((fd = open(path, O_RDONLY)) < 0)
		err(1, "unable to open '%s'", path);
	if (fstat(fd, &sb) < 0)
		err(1, "unable to stat '%s'", path);

	/*
	 * The NVMe spec does not explicitly state a maximum firmware image
	 *  size, although one can be inferred from the dword size limitation
	 *  for the size and offset fields in the Firmware Image Download
	 *  command.
	 *
	 * Technically, the max is UINT32_MAX * sizeof(uint32_t), since the
	 *  size and offsets are specified in terms of dwords (not bytes), but
	 *  realistically INT32_MAX is sufficient here and simplifies matters
	 *  a bit.
	 */
	if (sb.st_size > INT32_MAX)
		errx(1, "size of file '%s' is too large (%jd bytes)",
		    path, (intmax_t)sb.st_size);
	filesize = (int32_t)sb.st_size;
	if ((*buf = malloc(filesize)) == NULL)
		errx(1, "unable to malloc %d bytes", filesize);
	if ((*size = read(fd, *buf, filesize)) < 0)
		err(1, "error reading '%s'", path);
	/* XXX assuming no short reads */
	if (*size != filesize)
		errx(1,
		    "error reading '%s' (read %d bytes, requested %d bytes)",
		    path, *size, filesize);
}

static void
update_firmware(int fd, uint8_t *payload, int32_t payload_size)
{
	struct nvme_pt_command	pt;
	int32_t			off, resid, size;
	void			*chunk;

	off = 0;
	resid = payload_size;

	if ((chunk = aligned_alloc(PAGE_SIZE, NVME_MAX_XFER_SIZE)) == NULL)
		errx(1, "unable to malloc %d bytes", NVME_MAX_XFER_SIZE);

	while (resid > 0) {
		size = (resid >= NVME_MAX_XFER_SIZE) ?
		    NVME_MAX_XFER_SIZE : resid;
		memcpy(chunk, payload + off, size);

		memset(&pt, 0, sizeof(pt));
		pt.cmd.opc = NVME_OPC_FIRMWARE_IMAGE_DOWNLOAD;
		pt.cmd.cdw10 = htole32((size / sizeof(uint32_t)) - 1);
		pt.cmd.cdw11 = htole32(off / sizeof(uint32_t));
		pt.buf = chunk;
		pt.len = size;
		pt.is_read = 0;

		if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
			err(1, "firmware download request failed");

		if (nvme_completion_is_error(&pt.cpl))
			errx(1, "firmware download request returned error");

		resid -= size;
		off += size;
	}
}

static int
activate_firmware(int fd, int slot, int activate_action)
{
	struct nvme_pt_command	pt;
	uint16_t sct, sc;

	memset(&pt, 0, sizeof(pt));
	pt.cmd.opc = NVME_OPC_FIRMWARE_ACTIVATE;
	pt.cmd.cdw10 = htole32((activate_action << 3) | slot);
	pt.is_read = 0;

	if (ioctl(fd, NVME_PASSTHROUGH_CMD, &pt) < 0)
		err(1, "firmware activate request failed");

	sct = NVME_STATUS_GET_SCT(pt.cpl.status);
	sc = NVME_STATUS_GET_SC(pt.cpl.status);

	if (sct == NVME_SCT_COMMAND_SPECIFIC &&
	    sc == NVME_SC_FIRMWARE_REQUIRES_RESET)
		return 1;

	if (nvme_completion_is_error(&pt.cpl))
		errx(1, "firmware activate request returned error");

	return 0;
}

static void
firmware(const struct nvme_function *nf, int argc, char *argv[])
{
	int				fd = -1, slot = 0;
	int				a_flag, f_flag;
	int				activate_action, reboot_required;
	int				opt;
	char				*p, *image = NULL;
	char				*controller = NULL, prompt[64];
	void				*buf = NULL;
	int32_t				size = 0;
	uint16_t			oacs_fw;
	uint8_t				fw_slot1_ro, fw_num_slots;
	struct nvme_controller_data	cdata;

	a_flag = f_flag = false;

	while ((opt = getopt(argc, argv, "af:s:")) != -1) {
		switch (opt) {
		case 'a':
			a_flag = true;
			break;
		case 's':
			slot = strtol(optarg, &p, 0);
			if (p != NULL && *p != '\0') {
				fprintf(stderr,
				    "\"%s\" not valid slot.\n",
				    optarg);
				usage(nf);
			} else if (slot == 0) {
				fprintf(stderr,
				    "0 is not a valid slot number. "
				    "Slot numbers start at 1.\n");
				usage(nf);
			} else if (slot > 7) {
				fprintf(stderr,
				    "Slot number %s specified which is "
				    "greater than max allowed slot number of "
				    "7.\n", optarg);
				usage(nf);
			}
			break;
		case 'f':
			image = optarg;
			f_flag = true;
			break;
		}
	}

	/* Check that a controller (and not a namespace) was specified. */
	if (optind >= argc || strstr(argv[optind], NVME_NS_PREFIX) != NULL)
		usage(nf);

	if (!f_flag && !a_flag) {
		fprintf(stderr,
		    "Neither a replace ([-f path_to_firmware]) nor "
		    "activate ([-a]) firmware image action\n"
		    "was specified.\n");
		usage(nf);
	}

	if (!f_flag && a_flag && slot == 0) {
		fprintf(stderr,
		    "Slot number to activate not specified.\n");
		usage(nf);
	}

	controller = argv[optind];
	open_dev(controller, &fd, 1, 1);
	read_controller_data(fd, &cdata);

	oacs_fw = (cdata.oacs >> NVME_CTRLR_DATA_OACS_FIRMWARE_SHIFT) &
		NVME_CTRLR_DATA_OACS_FIRMWARE_MASK;

	if (oacs_fw == 0)
		errx(1,
		    "controller does not support firmware activate/download");

	fw_slot1_ro = (cdata.frmw >> NVME_CTRLR_DATA_FRMW_SLOT1_RO_SHIFT) &
		NVME_CTRLR_DATA_FRMW_SLOT1_RO_MASK;

	if (f_flag && slot == 1 && fw_slot1_ro)
		errx(1, "slot %d is marked as read only", slot);

	fw_num_slots = (cdata.frmw >> NVME_CTRLR_DATA_FRMW_NUM_SLOTS_SHIFT) &
		NVME_CTRLR_DATA_FRMW_NUM_SLOTS_MASK;

	if (slot > fw_num_slots)
		errx(1,
		    "slot %d specified but controller only supports %d slots",
		    slot, fw_num_slots);

	if (a_flag && !f_flag && !slot_has_valid_firmware(fd, slot))
		errx(1,
		    "slot %d does not contain valid firmware,\n"
		    "try 'nvmecontrol logpage -p 3 %s' to get a list "
		    "of available images\n",
		    slot, controller);

	if (f_flag)
		read_image_file(image, &buf, &size);

	if (f_flag && a_flag)
		printf("You are about to download and activate "
		       "firmware image (%s) to controller %s.\n"
		       "This may damage your controller and/or "
		       "overwrite an existing firmware image.\n",
		       image, controller);
	else if (a_flag)
		printf("You are about to activate a new firmware "
		       "image on controller %s.\n"
		       "This may damage your controller.\n",
		       controller);
	else if (f_flag)
		printf("You are about to download firmware image "
		       "(%s) to controller %s.\n"
		       "This may damage your controller and/or "
		       "overwrite an existing firmware image.\n",
		       image, controller);

	printf("Are you sure you want to continue? (yes/no) ");
	while (1) {
		fgets(prompt, sizeof(prompt), stdin);
		if (strncasecmp(prompt, "yes", 3) == 0)
			break;
		if (strncasecmp(prompt, "no", 2) == 0)
			exit(1);
		printf("Please answer \"yes\" or \"no\". ");
	}

	if (f_flag) {
		update_firmware(fd, buf, size);
		if (a_flag)
			activate_action = NVME_AA_REPLACE_ACTIVATE;
		else
			activate_action = NVME_AA_REPLACE_NO_ACTIVATE;
	} else {
		activate_action = NVME_AA_ACTIVATE;
	}

	reboot_required = activate_firmware(fd, slot, activate_action);

	if (a_flag) {
		if (reboot_required) {
			printf("New firmware image activated but requires "
			       "conventional reset (i.e. reboot) to "
			       "complete activation.\n");
		} else {
			printf("New firmware image activated and will take "
			       "effect after next controller reset.\n"
			       "Controller reset can be initiated via "
			       "'nvmecontrol reset %s'\n",
			       controller);
		}
	}

	close(fd);
	exit(0);
}

NVME_COMMAND(top, firmware, firmware, FIRMWARE_USAGE);
