/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright (C) 2011 Kees Cook <keescook@chromium.org>
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef __LINUX_PSTORE_RAM_H__
#define __LINUX_PSTORE_RAM_H__

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pstore.h>
#include <linux/types.h>

/*
 * Choose whether access to the RAM zone requires locking or not.  If a zone
 * can be written to from different CPUs like with ftrace for example, then
 * PRZ_FLAG_NO_LOCK is used. For all other cases, locking is required.
 */
#define PRZ_FLAG_NO_LOCK	BIT(0)
/*
 * If a PRZ should only have a single-boot lifetime, this marks it as
 * getting wiped after its contents get copied out after boot.
 */
#define PRZ_FLAG_ZAP_OLD	BIT(1)

struct persistent_ram_buffer;
struct rs_control;

struct persistent_ram_ecc_info {
	int block_size;
	int ecc_size;
	int symsize;
	int poly;
	uint16_t *par;
};

/**
 * struct persistent_ram_zone - Details of a persistent RAM zone (PRZ)
 *                              used as a pstore backend
 *
 * @paddr:	physical address of the mapped RAM area
 * @size:	size of mapping
 * @label:	unique name of this PRZ
 * @type:	frontend type for this PRZ
 * @flags:	holds PRZ_FLAGS_* bits
 *
 * @buffer_lock:
 *	locks access to @buffer "size" bytes and "start" offset
 * @buffer:
 *	pointer to actual RAM area managed by this PRZ
 * @buffer_size:
 *	bytes in @buffer->data (not including any trailing ECC bytes)
 *
 * @par_buffer:
 *	pointer into @buffer->data containing ECC bytes for @buffer->data
 * @par_header:
 *	pointer into @buffer->data containing ECC bytes for @buffer header
 *	(i.e. all fields up to @data)
 * @rs_decoder:
 *	RSLIB instance for doing ECC calculations
 * @corrected_bytes:
 *	ECC corrected bytes accounting since boot
 * @bad_blocks:
 *	ECC uncorrectable bytes accounting since boot
 * @ecc_info:
 *	ECC configuration details
 *
 * @old_log:
 *	saved copy of @buffer->data prior to most recent wipe
 * @old_log_size:
 *	bytes contained in @old_log
 *
 */
struct persistent_ram_zone {
	phys_addr_t paddr;
	size_t size;
	void *vaddr;
	char *label;
	enum pstore_type_id type;
	u32 flags;

	raw_spinlock_t buffer_lock;
	struct persistent_ram_buffer *buffer;
	size_t buffer_size;

	char *par_buffer;
	char *par_header;
	struct rs_control *rs_decoder;
	int corrected_bytes;
	int bad_blocks;
	struct persistent_ram_ecc_info ecc_info;

	char *old_log;
	size_t old_log_size;
};

struct persistent_ram_zone *persistent_ram_new(phys_addr_t start, size_t size,
			u32 sig, struct persistent_ram_ecc_info *ecc_info,
			unsigned int memtype, u32 flags, char *label);
void persistent_ram_free(struct persistent_ram_zone *prz);
void persistent_ram_zap(struct persistent_ram_zone *prz);

int persistent_ram_write(struct persistent_ram_zone *prz, const void *s,
			 unsigned int count);
int persistent_ram_write_user(struct persistent_ram_zone *prz,
			      const void __user *s, unsigned int count);

void persistent_ram_save_old(struct persistent_ram_zone *prz);
size_t persistent_ram_old_size(struct persistent_ram_zone *prz);
void *persistent_ram_old(struct persistent_ram_zone *prz);
void persistent_ram_free_old(struct persistent_ram_zone *prz);
ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len);
#ifdef CONFIG_PSTORE_BOOT_LOG
ssize_t ramoops_pstore_read_for_boot_log(struct pstore_record *record);
#endif

/*
 * Ramoops platform data
 * @mem_size	memory size for ramoops
 * @mem_address	physical memory address to contain ramoops
 */

#define RAMOOPS_FLAG_FTRACE_PER_CPU	BIT(0)

struct ramoops_platform_data {
	unsigned long	mem_size;
	phys_addr_t	mem_address;
	unsigned int	mem_type;
	unsigned long	record_size;
	unsigned long	console_size;
	unsigned long	ftrace_size;
	unsigned long	pmsg_size;
#ifdef CONFIG_PSTORE_BOOT_LOG
	unsigned long	boot_log_size;
	unsigned long	max_boot_log_cnt;
#endif
	int		max_reason;
	u32		flags;
	struct persistent_ram_ecc_info ecc_info;
};

#endif
