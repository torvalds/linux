/*
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright (C) 2011 Kees Cook <keescook@chromium.org>
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_PSTORE_RAM_H__
#define __LINUX_PSTORE_RAM_H__

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>

struct persistent_ram_buffer;

struct persistent_ram_zone {
	phys_addr_t paddr;
	size_t size;
	void *vaddr;
	struct persistent_ram_buffer *buffer;
	size_t buffer_size;

	/* ECC correction */
	bool ecc;
	char *par_buffer;
	char *par_header;
	struct rs_control *rs_decoder;
	int corrected_bytes;
	int bad_blocks;
	int ecc_block_size;
	int ecc_size;
	int ecc_symsize;
	int ecc_poly;

	char *old_log;
	size_t old_log_size;
};

struct persistent_ram_zone * __devinit persistent_ram_new(phys_addr_t start,
							  size_t size,
							  bool ecc);
void persistent_ram_free(struct persistent_ram_zone *prz);
void persistent_ram_zap(struct persistent_ram_zone *prz);

int persistent_ram_write(struct persistent_ram_zone *prz, const void *s,
	unsigned int count);

void persistent_ram_save_old(struct persistent_ram_zone *prz);
size_t persistent_ram_old_size(struct persistent_ram_zone *prz);
void *persistent_ram_old(struct persistent_ram_zone *prz);
void persistent_ram_free_old(struct persistent_ram_zone *prz);
ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len);

/*
 * Ramoops platform data
 * @mem_size	memory size for ramoops
 * @mem_address	physical memory address to contain ramoops
 */

struct ramoops_platform_data {
	unsigned long	mem_size;
	unsigned long	mem_address;
	unsigned long	record_size;
	unsigned long	console_size;
	int		dump_oops;
	bool		ecc;
};

#endif
