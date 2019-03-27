/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Fabien Thomas <fabient@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <sys/cpuctl.h>

#include <machine/cpufunc.h>
#include <machine/specialreg.h>

#include "cpucontrol.h"
#include "via.h"

int
via_probe(int fd)
{
	char vendor[13];
	int error;
	cpuctl_cpuid_args_t idargs = {
		.level  = 0,
	};

	error = ioctl(fd, CPUCTL_CPUID, &idargs);
	if (error < 0) {
		WARN(0, "ioctl()");
		return (1);
	}
	((uint32_t *)vendor)[0] = idargs.data[1];
	((uint32_t *)vendor)[1] = idargs.data[3];
	((uint32_t *)vendor)[2] = idargs.data[2];
	vendor[12] = '\0';
	if (strncmp(vendor, CENTAUR_VENDOR_ID, sizeof(CENTAUR_VENDOR_ID)) != 0)
		return (1);

	/* TODO: detect Nano CPU. */
	return (0);
}

void
via_update(const struct ucode_update_params *params)
{
	int devfd;
	const char *dev, *path;
	const uint32_t *fw_image;
	uint32_t sum;
	unsigned int i;
	size_t payload_size;
	const via_fw_header_t *fw_header;
	uint32_t signature;
	int32_t revision;
	const void *fw_data;
	size_t data_size, total_size;
	cpuctl_msr_args_t msrargs = {
		.msr = MSR_IA32_PLATFORM_ID,
	};
	cpuctl_cpuid_args_t idargs = {
		.level  = 1,	/* Signature. */
	};
	cpuctl_update_args_t args;
	int error;

	dev = params->dev_path;
	path = params->fw_path;
	devfd = params->devfd;
	fw_image = params->fwimage;

	assert(path);
	assert(dev);

	error = ioctl(devfd, CPUCTL_CPUID, &idargs);
	if (error < 0) {
		WARN(0, "ioctl(%s)", dev);
		goto fail;
	}
	signature = idargs.data[0];
	error = ioctl(devfd, CPUCTL_RDMSR, &msrargs);
	if (error < 0) {
		WARN(0, "ioctl(%s)", dev);
		goto fail;
	}

	/*
	 * MSR_IA32_PLATFORM_ID contains flag in BCD in bits 52-50.
	 */
	msrargs.msr = MSR_BIOS_SIGN;
	error = ioctl(devfd, CPUCTL_RDMSR, &msrargs);
	if (error < 0) {
		WARN(0, "ioctl(%s)", dev);
		goto fail;
	}
	revision = msrargs.data >> 32; /* Revision in the high dword. */
	WARNX(2, "found cpu type %#x family %#x model %#x stepping %#x.",
	    (signature >> 12) & 0x03, (signature >> 8) & 0x0f,
	    (signature >> 4) & 0x0f, (signature >> 0) & 0x0f);

	if (params->fwsize < sizeof(*fw_header)) {
		WARNX(2, "file too short: %s", path);
		goto fail;
	}

	fw_header = (const via_fw_header_t *)fw_image;
	if (fw_header->signature != VIA_HEADER_SIGNATURE ||
	    fw_header->loader_revision != VIA_LOADER_REVISION) {
		WARNX(2, "%s is not a valid via firmware: version mismatch",
		    path);
		goto fail;
	}
	data_size = fw_header->data_size;
	total_size = fw_header->total_size;
	if (total_size > params->fwsize) {
		WARNX(2, "file too short: %s", path);
		goto fail;
	}
	payload_size = data_size + sizeof(*fw_header);

	/*
	 * Check the primary checksum.
	 */
	sum = 0;
	for (i = 0; i < (payload_size / sizeof(uint32_t)); i++)
		sum += *((const uint32_t *)fw_image + i);
	if (sum != 0) {
		WARNX(2, "%s: update data checksum invalid", path);
		goto fail;
	}

	fw_data = fw_header + 1; /* Pointer to the update data. */

	/*
	 * Check if the given image is ok for this cpu.
	 */
	if (signature != fw_header->cpu_signature)
		goto fail;

	if (fw_header->revision != 0 && revision >= fw_header->revision) {
		WARNX(1, "skipping %s of rev %#x: up to date",
		    path, fw_header->revision);
		goto fail;
	}
	fprintf(stderr, "%s: updating cpu %s from rev %#x to rev %#x... ",
			path, dev, revision, fw_header->revision);
	args.data = __DECONST(void *, fw_data);
	args.size = data_size;
	error = ioctl(devfd, CPUCTL_UPDATE, &args);
	if (error < 0) {
               error = errno;
	       fprintf(stderr, "failed.\n");
               errno = error;
	       WARN(0, "ioctl()");
	       goto fail;
	}
	fprintf(stderr, "done.\n");

fail:
	return;
}
