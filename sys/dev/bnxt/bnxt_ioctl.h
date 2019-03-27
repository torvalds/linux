/*-
 * Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016 Broadcom, All Rights Reserved.
 * The term Broadcom refers to Broadcom Limited and/or its subsidiaries
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _BNXT_IOCTL_H
#define _BNXT_IOCTL_H

enum bnxt_ioctl_type {
	BNXT_HWRM_NVM_FIND_DIR_ENTRY,
	BNXT_HWRM_NVM_READ,
	BNXT_HWRM_FW_RESET,
	BNXT_HWRM_FW_QSTATUS,
	BNXT_HWRM_NVM_WRITE,
	BNXT_HWRM_NVM_ERASE_DIR_ENTRY,
	BNXT_HWRM_NVM_GET_DIR_INFO,
	BNXT_HWRM_NVM_GET_DIR_ENTRIES,
	BNXT_HWRM_NVM_MODIFY,
	BNXT_HWRM_NVM_VERIFY_UPDATE,
	BNXT_HWRM_NVM_INSTALL_UPDATE,
	BNXT_HWRM_FW_GET_TIME,
	BNXT_HWRM_FW_SET_TIME,
};

struct bnxt_ioctl_header {
	enum bnxt_ioctl_type type;
	int		rc;
};

struct bnxt_ioctl_hwrm_nvm_find_dir_entry {
	struct bnxt_ioctl_header hdr;
	uint32_t	data_length;
	uint32_t	fw_ver;
	uint32_t	item_length;
	uint16_t	ext;
	uint16_t	index;
	uint16_t	ordinal;
	uint16_t	type;
	uint8_t		search_opt;
	bool		use_index;
};

struct bnxt_ioctl_hwrm_nvm_read {
	struct bnxt_ioctl_header hdr;
	uint8_t		*data;
	uint32_t	length;
	uint32_t	offset;
	uint16_t	index;
};

struct bnxt_ioctl_hwrm_fw_reset {
	struct bnxt_ioctl_header hdr;
	uint8_t		processor;
	uint8_t		selfreset;
};

struct bnxt_ioctl_hwrm_fw_qstatus {
	struct bnxt_ioctl_header hdr;
	uint8_t		processor;
	uint8_t		selfreset;
};

struct bnxt_ioctl_hwrm_nvm_write {
	struct bnxt_ioctl_header hdr;
	uint8_t		*data;
	uint32_t	data_length;
	uint32_t	item_length;
	uint16_t	attr;
	uint16_t	ext;
	uint16_t	index;
	uint16_t	option;
	uint16_t	ordinal;
	uint16_t	type;
	bool		keep;
};

struct bnxt_ioctl_hwrm_nvm_erase_dir_entry {
	struct bnxt_ioctl_header hdr;
	enum bnxt_ioctl_type type;
	int		rc;
	uint16_t	index;
};

struct bnxt_ioctl_hwrm_nvm_get_dir_info {
	struct bnxt_ioctl_header hdr;
	uint32_t	entries;
	uint32_t	entry_length;
};

struct bnxt_ioctl_hwrm_nvm_get_dir_entries {
	struct bnxt_ioctl_header hdr;
	uint8_t		*data;
	size_t		max_size;
	uint32_t	entries;
	uint32_t	entry_length;
};

struct bnxt_ioctl_hwrm_nvm_install_update {
	struct bnxt_ioctl_header hdr;
	uint64_t	installed_items;
	uint32_t	install_type;
	uint8_t		problem_item;
	uint8_t		reset_required;
	uint8_t		result;
};

struct bnxt_ioctl_hwrm_nvm_verify_update {
	struct bnxt_ioctl_header hdr;
	uint16_t	ext;
	uint16_t	ordinal;
	uint16_t	type;
};

struct bnxt_ioctl_hwrm_nvm_modify {
	struct bnxt_ioctl_header hdr;
	uint8_t		*data;
	uint32_t	length;
	uint32_t	offset;
	uint16_t	index;
};

struct bnxt_ioctl_hwrm_fw_get_time {
	struct bnxt_ioctl_header hdr;
	uint16_t	millisecond;
	uint16_t	year;
	uint16_t	zone;
	uint8_t		day;
	uint8_t		hour;
	uint8_t		minute;
	uint8_t		month;
	uint8_t		second;
};

struct bnxt_ioctl_hwrm_fw_set_time {
	struct bnxt_ioctl_header hdr;
	uint16_t	millisecond;
	uint16_t	year;
	uint16_t	zone;
	uint8_t		day;
	uint8_t		hour;
	uint8_t		minute;
	uint8_t		month;
	uint8_t		second;
};

/* IOCTL interface */
struct bnxt_ioctl_data {
	union {
		struct bnxt_ioctl_header			hdr;
		struct bnxt_ioctl_hwrm_nvm_find_dir_entry	find;
		struct bnxt_ioctl_hwrm_nvm_read			read;
		struct bnxt_ioctl_hwrm_fw_reset			reset;
		struct bnxt_ioctl_hwrm_fw_qstatus		status;
		struct bnxt_ioctl_hwrm_nvm_write		write;
		struct bnxt_ioctl_hwrm_nvm_erase_dir_entry	erase;
		struct bnxt_ioctl_hwrm_nvm_get_dir_info		dir_info;
		struct bnxt_ioctl_hwrm_nvm_get_dir_entries	dir_entries;
		struct bnxt_ioctl_hwrm_nvm_install_update	install;
		struct bnxt_ioctl_hwrm_nvm_verify_update	verify;
		struct bnxt_ioctl_hwrm_nvm_modify		modify;
		struct bnxt_ioctl_hwrm_fw_get_time		get_time;
		struct bnxt_ioctl_hwrm_fw_set_time		set_time;
	};
};

#endif
