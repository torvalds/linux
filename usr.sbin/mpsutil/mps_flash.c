/*-
 * Copyright (c) 2015 Baptiste Daroussin <bapt@FreeBSD.org>
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
__RCSID("$FreeBSD$");

#include <sys/stat.h>
#include <sys/param.h>
#include <sys/mman.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpsutil.h"

MPS_TABLE(top, flash);

static int
flash_save(int argc, char **argv)
{
	const char *firmware_file;
	unsigned char *firmware_buffer = NULL;
	int error, fd, size;
	bool bios = false;
	ssize_t written = 0, ret = 0;

	if (argc < 2) {
		warnx("missing argument: expecting 'firmware' or bios'");
		return (EINVAL);
	}

	if (strcmp(argv[1], "bios") == 0) {
		bios = true;
	} else if (strcmp(argv[1], "firmware") != 0) {
		warnx("Invalid argument '%s', expecting 'firmware' or 'bios'",
		    argv[1]);
	}

	if (argc > 4) {
		warnx("save %s: extra arguments", argv[1]);
		return (EINVAL);
	}

	firmware_file = argv[1];
	if (argc == 3) {
		firmware_file = argv[2];
	}

	fd = mps_open(mps_unit);
	if (fd < 0) {
		error = errno;
		warn("mps_open");
		return (error);
	}

	if ((size = mps_firmware_get(fd, &firmware_buffer, bios)) < 0) {
		warnx("Fail to save %s", argv[1]);
		close(fd);
		return (1);
	}

	close(fd);
	if (size > 0) {
		fd = open(firmware_file, O_CREAT | O_TRUNC | O_RDWR, 0644);
		if (fd <0) {
			error = errno;
			warn("open");
			free(firmware_buffer);
			return (error);
		}
		while (written != size) {
			if ((ret = write(fd, firmware_buffer + written, size - written)) <0) {
				error = errno;
				warn("write");
				free(firmware_buffer);
				close(fd);
				return (error);
			}
			written += ret;
		}
		close(fd);
	}
	free(firmware_buffer);
	printf("%s successfully saved as %s\n", argv[1], firmware_file);
	return (0);
}

MPS_COMMAND(flash, save, flash_save, "[firmware|bios] [file]",
    "Save firmware/bios into a file");

static int
flash_update(int argc, char **argv)
{
	int error, fd;
	unsigned char *mem = NULL;
	struct stat st;
	bool bios = false;
	MPI2_FW_IMAGE_HEADER *fwheader;
	MPI2_IOC_FACTS_REPLY *facts;

	if (argc < 2) {
		warnx("missing argument: expecting 'firmware' or bios'");
		return (EINVAL);
	}

	if (strcmp(argv[1], "bios") == 0) {
		bios = true;
	} else if (strcmp(argv[1], "firmware") != 0) {
		warnx("Invalid argument '%s', expecting 'firmware' or 'bios'",
		    argv[1]);
	}

	if (argc > 4) {
		warnx("update firmware: extra arguments");
		return (EINVAL);
	}

	if (argc != 3) {
		warnx("no firmware specified");
		return (EINVAL);
	}

	if (stat(argv[2], &st) == -1) {
		error = errno;
		warn("stat");
		return (error);
	}

	fd = open(argv[2], O_RDONLY);
	if (fd < 0) {
		error = errno;
		warn("open");
		return (error);
	}

	mem = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mem == MAP_FAILED) {
		error = errno;
		warn("mmap");
		close(fd);
		return (error);
	}
	close(fd);

	fd = mps_open(mps_unit);
	if (fd < 0) {
		error = errno;
		warn("mps_open");
		munmap(mem, st.st_size);
		return (error);
	}

	if ((facts = mps_get_iocfacts(fd)) == NULL) {
		warnx("could not get controller IOCFacts\n");
		munmap(mem, st.st_size);
		close(fd);
		return (EINVAL);
	}

	if (bios) {
		/* Check boot record magic number */
		if (((mem[0x01]<<8) + mem[0x00]) != 0xaa55) {
			warnx("Invalid bios: no boot record magic number");
			munmap(mem, st.st_size);
			close(fd);
			free(facts);
			return (1);
		}
		if ((st.st_size % 512) != 0) {
			warnx("Invalid bios: size not a multiple of 512");
			munmap(mem, st.st_size);
			close(fd);
			free(facts);
			return (1);
		}
	} else {
		fwheader = (MPI2_FW_IMAGE_HEADER *)mem;
		if (fwheader->VendorID != MPI2_MFGPAGE_VENDORID_LSI) {
			warnx("Invalid firmware:");
			warnx("  Expected Vendor ID: %04x",
			    MPI2_MFGPAGE_VENDORID_LSI);
			warnx("  Image Vendor ID: %04x", fwheader->VendorID);
			munmap(mem, st.st_size);
			close(fd);
			free(facts);
			return (1);
		}

		if (fwheader->ProductID != facts->ProductID) {
			warnx("Invalid image:");
			warnx("  Expected Product ID: %04x", facts->ProductID);
			warnx("  Image Product ID: %04x", fwheader->ProductID);
			munmap(mem, st.st_size);
			close(fd);
			free(facts);
			return (1);
		}
	}

	printf("Updating %s...\n", argv[1]);
	if (mps_firmware_send(fd, mem, st.st_size, bios) < 0) {
		warnx("Fail to update %s", argv[1]);
		munmap(mem, st.st_size);
		close(fd);
		free(facts);
		return (1);
	}

	munmap(mem, st.st_size);
	close(fd);
	free(facts);
	printf("%s successfully updated\n", argv[1]);
	return (0);
}

MPS_COMMAND(flash, update, flash_update, "[firmware|bios] file",
    "Update firmware/bios");
