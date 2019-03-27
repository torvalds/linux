/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <libgeom.h>
#include <sys/disk.h>
#include <dev/nand/nand_dev.h>
#include "nandtool.h"

int nand_info(struct cmd_param *params)
{
	struct chip_param_io chip_params;
	int fd = -1, ret = 0;
	int block_size;
	off_t chip_size, media_size;
	const char *dev;

	if ((dev = param_get_string(params, "dev")) == NULL) {
		fprintf(stderr, "Please supply 'dev' parameter, eg. "
		    "'dev=/dev/gnand0'\n");
		return (1);
	}

	if ((fd = g_open(dev, 1)) == -1) {
		perrorf("Cannot open %s", dev);
		return (1);
	}

	if (ioctl(fd, NAND_IO_GET_CHIP_PARAM, &chip_params) == -1) {
		perrorf("Cannot ioctl(NAND_IO_GET_CHIP_PARAM)");
		ret = 1;
		goto out;
	}

	if (ioctl(fd, DIOCGMEDIASIZE, &media_size) == -1) {
		perrorf("Cannot ioctl(DIOCGMEDIASIZE)");
		ret = 1;
		goto out;
	}

	block_size = chip_params.page_size * chip_params.pages_per_block;
	chip_size = block_size * chip_params.blocks;

	printf("Device:\t\t\t%s\n", dev);
	printf("Page size:\t\t%d bytes\n", chip_params.page_size);
	printf("Block size:\t\t%d bytes (%d KB)\n", block_size,
	    block_size / 1024);
	printf("OOB size per page:\t%d bytes\n", chip_params.oob_size);
	printf("Chip size:\t\t%jd MB\n", (uintmax_t)(chip_size / 1024 / 1024));
	printf("Slice size:\t\t%jd MB\n",
	    (uintmax_t)(media_size / 1024 / 1024));

out:
	g_close(fd);

	return (ret);
}
