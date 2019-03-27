/*-
 * Copyright (c) 2012 Andriy Gapon <avg@FreeBSD.org>.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <sys/cpuctl.h>

#include <machine/cpufunc.h>
#include <machine/specialreg.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>

#include "cpucontrol.h"
#include "amd.h"

int
amd10h_probe(int fd)
{
	char vendor[13];
	cpuctl_cpuid_args_t idargs;
	uint32_t family;
	uint32_t signature;
	int error;

	idargs.level = 0;
	error = ioctl(fd, CPUCTL_CPUID, &idargs);
	if (error < 0) {
		WARN(0, "ioctl()");
		return (1);
	}
	((uint32_t *)vendor)[0] = idargs.data[1];
	((uint32_t *)vendor)[1] = idargs.data[3];
	((uint32_t *)vendor)[2] = idargs.data[2];
	vendor[12] = '\0';
	if (strncmp(vendor, AMD_VENDOR_ID, sizeof(AMD_VENDOR_ID)) != 0)
		return (1);

	idargs.level = 1;
	error = ioctl(fd, CPUCTL_CPUID, &idargs);
	if (error < 0) {
		WARN(0, "ioctl()");
		return (1);
	}
	signature = idargs.data[0];
	family = ((signature >> 8) & 0x0f) + ((signature >> 20) & 0xff);
	if (family < 0x10)
		return (1);
	return (0);
}

/*
 * NB: the format of microcode update files is not documented by AMD.
 * It has been reverse engineered from studying Coreboot, illumos and Linux
 * source code.
 */
void
amd10h_update(const struct ucode_update_params *params)
{
	cpuctl_cpuid_args_t idargs;
	cpuctl_msr_args_t msrargs;
	cpuctl_update_args_t args;
	const amd_10h_fw_header_t *fw_header;
	const amd_10h_fw_header_t *selected_fw;
	const equiv_cpu_entry_t *equiv_cpu_table;
	const section_header_t *section_header;
	const container_header_t *container_header;
	const uint8_t *fw_data;
	const uint8_t *fw_image;
	const char *dev, *path;
	size_t fw_size;
	size_t selected_size;
	uint32_t revision;
	uint32_t new_rev;
	uint32_t signature;
	uint16_t equiv_id;
	int devfd;
	unsigned int i;
	int error;

	dev = params->dev_path;
	path = params->fw_path;
	devfd = params->devfd;
	fw_image = params->fwimage;
	fw_size = params->fwsize;

	assert(path);
	assert(dev);

	idargs.level = 1;
	error = ioctl(devfd, CPUCTL_CPUID, &idargs);
	if (error < 0) {
		WARN(0, "ioctl()");
		goto done;
	}
	signature = idargs.data[0];

	msrargs.msr = MSR_BIOS_SIGN;
	error = ioctl(devfd, CPUCTL_RDMSR, &msrargs);
	if (error < 0) {
		WARN(0, "ioctl(%s)", dev);
		goto done;
	}
	revision = (uint32_t)msrargs.data;

	WARNX(1, "found cpu family %#x model %#x "
	    "stepping %#x extfamily %#x extmodel %#x.",
	    ((signature >> 8) & 0x0f) + ((signature >> 20) & 0xff),
	    (signature >> 4) & 0x0f,
	    (signature >> 0) & 0x0f, (signature >> 20) & 0xff,
	    (signature >> 16) & 0x0f);
	WARNX(1, "microcode revision %#x", revision);

	/*
	 * Open the firmware file.
	 */
	WARNX(1, "checking %s for update.", path);
	if (fw_size <
	    (sizeof(*container_header) + sizeof(*section_header))) {
		WARNX(2, "file too short: %s", path);
		goto done;
	}

	/*
	 * mmap the whole image.
	 */
	fw_data = fw_image;
	container_header = (const container_header_t *)fw_data;
	if (container_header->magic != AMD_10H_MAGIC) {
		WARNX(2, "%s is not a valid amd firmware: bad magic", path);
		goto done;
	}
	fw_data += sizeof(*container_header);
	fw_size -= sizeof(*container_header);

	section_header = (const section_header_t *)fw_data;
	if (section_header->type != AMD_10H_EQUIV_TABLE_TYPE) {
		WARNX(2, "%s is not a valid amd firmware: "
		    "first section is not CPU equivalence table", path);
		goto done;
	}
	if (section_header->size == 0) {
		WARNX(2, "%s is not a valid amd firmware: "
		    "first section is empty", path);
		goto done;
	}
	fw_data += sizeof(*section_header);
	fw_size -= sizeof(*section_header);

	if (section_header->size > fw_size) {
		WARNX(2, "%s is not a valid amd firmware: "
		    "file is truncated", path);
		goto done;
	}
	if (section_header->size < sizeof(*equiv_cpu_table)) {
		WARNX(2, "%s is not a valid amd firmware: "
		    "first section is too short", path);
		goto done;
	}
	equiv_cpu_table = (const equiv_cpu_entry_t *)fw_data;
	fw_data += section_header->size;
	fw_size -= section_header->size;

	equiv_id = 0;
	for (i = 0; equiv_cpu_table[i].installed_cpu != 0; i++) {
		if (signature == equiv_cpu_table[i].installed_cpu) {
			equiv_id = equiv_cpu_table[i].equiv_cpu;
			WARNX(3, "equiv_id: %x, signature %8x,"
			    " equiv_cpu_table[%d] %8x", equiv_id, signature,
			    i, equiv_cpu_table[i].installed_cpu);
			break;
		}
	}
	if (equiv_id == 0) {
		WARNX(2, "CPU is not found in the equivalence table");
		goto done;
	}

	selected_fw = NULL;
	selected_size = 0;
	while (fw_size >= sizeof(*section_header)) {
		section_header = (const section_header_t *)fw_data;
		fw_data += sizeof(*section_header);
		fw_size -= sizeof(*section_header);
		if (section_header->type != AMD_10H_uCODE_TYPE) {
			WARNX(2, "%s is not a valid amd firmware: "
			    "section has incorret type", path);
			goto done;
		}
		if (section_header->size > fw_size) {
			WARNX(2, "%s is not a valid amd firmware: "
			    "file is truncated", path);
			goto done;
		}
		if (section_header->size < sizeof(*fw_header)) {
			WARNX(2, "%s is not a valid amd firmware: "
			    "section is too short", path);
			goto done;
		}
		fw_header = (const amd_10h_fw_header_t *)fw_data;
		fw_data += section_header->size;
		fw_size -= section_header->size;

		if (fw_header->processor_rev_id != equiv_id) {
			WARNX(1, "firmware processor_rev_id %x, equiv_id %x",
			    fw_header->processor_rev_id, equiv_id);
			continue; /* different cpu */
		}
		if (fw_header->patch_id <= revision) {
			WARNX(1, "patch_id %x, revision %x",
			    fw_header->patch_id, revision);
			continue; /* not newer revision */
		}
		if (fw_header->nb_dev_id != 0 || fw_header->sb_dev_id != 0) {
			WARNX(2, "Chipset-specific microcode is not supported");
		}

		WARNX(3, "selecting revision: %x", fw_header->patch_id);
		revision = fw_header->patch_id;
		selected_fw = fw_header;
		selected_size = section_header->size;
	}

	if (fw_size != 0) {
		WARNX(2, "%s is not a valid amd firmware: "
		    "file is truncated", path);
		goto done;
	}

	if (selected_fw != NULL) {
		WARNX(1, "selected ucode size is %zu", selected_size);
		fprintf(stderr, "%s: updating cpu %s to revision %#x... ",
		    path, dev, revision);

		args.data = __DECONST(void *, selected_fw);
		args.size = selected_size;
		error = ioctl(devfd, CPUCTL_UPDATE, &args);
		if (error < 0) {
			fprintf(stderr, "failed.\n");
			warn("ioctl()");
			goto done;
		}
		fprintf(stderr, "done.\n");
	}

	msrargs.msr = MSR_BIOS_SIGN;
	error = ioctl(devfd, CPUCTL_RDMSR, &msrargs);
	if (error < 0) {
		WARN(0, "ioctl(%s)", dev);
		goto done;
	}
	new_rev = (uint32_t)msrargs.data;
	if (new_rev != revision)
		WARNX(0, "revision after update %#x", new_rev);

done:
	return;
}
