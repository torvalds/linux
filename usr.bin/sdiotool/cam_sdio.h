/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Ilya Bakulin
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/mman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <unistd.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/mmc/mmc_all.h>
#include <camlib.h>

struct cis_info {
	uint16_t man_id;
	uint16_t prod_id;
	uint16_t max_block_size;
};

int sdio_rw_direct(struct cam_device *dev,
			  uint8_t func_number,
			  uint32_t addr,
			  uint8_t is_write,
			  uint8_t *data,
			  uint8_t *resp);
int
sdio_rw_extended(struct cam_device *dev,
		 uint8_t func_number,
		 uint32_t addr,
		 uint8_t is_write,
		 caddr_t data, size_t datalen,
		 uint8_t is_increment,
		 uint16_t blk_count);
uint8_t sdio_read_1(struct cam_device *dev, uint8_t func_number, uint32_t addr, int *ret);
int sdio_write_1(struct cam_device *dev, uint8_t func_number, uint32_t addr, uint8_t val);
uint16_t sdio_read_2(struct cam_device *dev, uint8_t func_number, uint32_t addr, int *ret);
int sdio_write_2(struct cam_device *dev, uint8_t func_number, uint32_t addr, uint16_t val);
uint32_t sdio_read_4(struct cam_device *dev, uint8_t func_number, uint32_t addr, int *ret);
int sdio_write_4(struct cam_device *dev, uint8_t func_number, uint32_t addr, uint32_t val);
int sdio_read_bool_for_func(struct cam_device *dev, uint32_t addr, uint8_t func_number, uint8_t *is_enab);
int sdio_set_bool_for_func(struct cam_device *dev, uint32_t addr, uint8_t func_number, int enable);
int sdio_is_func_ready(struct cam_device *dev, uint8_t func_number, uint8_t *is_enab);
int sdio_is_func_enabled(struct cam_device *dev, uint8_t func_number, uint8_t *is_enab);
int sdio_func_enable(struct cam_device *dev, uint8_t func_number, int enable);
int sdio_is_func_intr_enabled(struct cam_device *dev, uint8_t func_number, uint8_t *is_enab);
int sdio_func_intr_enable(struct cam_device *dev, uint8_t func_number, int enable);
void sdio_card_reset(struct cam_device *dev);
uint32_t sdio_get_common_cis_addr(struct cam_device *dev);
int sdio_func_read_cis(struct cam_device *dev, uint8_t func_number,
		       uint32_t cis_addr, struct cis_info *info);
int sdio_card_set_bus_width(struct cam_device *dev, enum mmc_bus_width bw);
