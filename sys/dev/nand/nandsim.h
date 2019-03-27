/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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

#ifndef _NANDSIM_H_
#define _NANDSIM_H_

#include <sys/ioccom.h>
#include <sys/types.h>

#define MAX_SIM_DEV		4
#define MAX_CTRL_CS		4
#define MAX_ECC_BYTES		512
#define MAX_BAD_BLOCKS		512
#define DEV_MODEL_STR_SIZE	21
#define MAN_STR_SIZE		13
#define FILENAME_SIZE		20

#define MAX_CHIPS	(MAX_SIM_DEV*MAX_CTRL_CS)

#define NANDSIM_OUTPUT_NONE	0x0
#define NANDSIM_OUTPUT_CONSOLE	0x1
#define NANDSIM_OUTPUT_RAM	0x2
#define NANDSIM_OUTPUT_FILE	0x3

struct sim_ctrl_chip {
	uint8_t		ctrl_num;
	uint8_t		chip_num;
};

#define NANDSIM_BASE	'A'

struct sim_param {
	uint8_t	log_level;
	uint8_t	log_output;
};

#define NANDSIM_SIM_PARAM	_IOW(NANDSIM_BASE, 1, struct sim_param)

struct sim_ctrl {
	uint8_t running;
	uint8_t created;
	uint8_t	num;
	uint8_t	num_cs;
	uint8_t ecc;
	char	filename[FILENAME_SIZE];
	uint16_t ecc_layout[MAX_ECC_BYTES];
};
#define NANDSIM_CREATE_CTRL	_IOW(NANDSIM_BASE, 2, struct sim_ctrl)
#define NANDSIM_DESTROY_CTRL	_IOW(NANDSIM_BASE, 3, int)

struct sim_chip {
	uint8_t		num;
	uint8_t		ctrl_num;
	uint8_t		created;
	uint8_t		device_id;
	uint8_t		manufact_id;
	char		device_model[DEV_MODEL_STR_SIZE];
	char		manufacturer[MAN_STR_SIZE];
	uint8_t		col_addr_cycles;
	uint8_t		row_addr_cycles;
	uint8_t		features;
	uint8_t		width;
	uint32_t	page_size;
	uint32_t	oob_size;
	uint32_t	pgs_per_blk;
	uint32_t	blks_per_lun;
	uint32_t	luns;

	uint32_t	prog_time;
	uint32_t	erase_time;
	uint32_t	read_time;
	uint32_t	ccs_time;

	uint32_t	error_ratio;
	uint32_t	wear_level;
	uint32_t	bad_block_map[MAX_BAD_BLOCKS];
	uint8_t		is_wp;
};

#define NANDSIM_CREATE_CHIP	_IOW(NANDSIM_BASE, 3, struct sim_chip)

struct sim_chip_destroy {
	uint8_t ctrl_num;
	uint8_t chip_num;
};
#define NANDSIM_DESTROY_CHIP	_IOW(NANDSIM_BASE, 4, struct sim_chip_destroy)

#define NANDSIM_START_CTRL	_IOW(NANDSIM_BASE, 5, int)
#define NANDSIM_STOP_CTRL	_IOW(NANDSIM_BASE, 6, int)
#define NANDSIM_RESTART_CTRL	_IOW(NANDSIM_BASE, 7, int)

#define NANDSIM_STATUS_CTRL	_IOWR(NANDSIM_BASE, 8, struct sim_ctrl)
#define NANDSIM_STATUS_CHIP	_IOWR(NANDSIM_BASE, 9, struct sim_chip)

struct sim_mod {
	uint8_t	chip_num;
	uint8_t	ctrl_num;
	uint32_t field;
	uint32_t new_value;
};
#define SIM_MOD_LOG_LEVEL	0
#define SIM_MOD_ERASE_TIME	1
#define SIM_MOD_PROG_TIME	2
#define SIM_MOD_READ_TIME	3
#define SIM_MOD_CCS_TIME	4
#define SIM_MOD_ERROR_RATIO	5

#define NANDSIM_MODIFY	_IOW(NANDSIM_BASE, 10, struct sim_mod)
#define NANDSIM_FREEZE	_IOW(NANDSIM_BASE, 11, struct sim_ctrl_chip)

struct sim_error {
	uint8_t		ctrl_num;
	uint8_t		chip_num;
	uint32_t	page_num;
	uint32_t	column;
	uint32_t	len;
	uint32_t	pattern;
};
#define NANDSIM_INJECT_ERROR	_IOW(NANDSIM_BASE, 20, struct sim_error)

#define NANDSIM_GOOD_BLOCK	0
#define NANDSIM_BAD_BLOCK	1
struct sim_block_state {
	uint8_t		ctrl_num;
	uint8_t		chip_num;
	uint32_t	block_num;
	int		wearout;
	uint8_t		state;
};
#define NANDSIM_SET_BLOCK_STATE	_IOW(NANDSIM_BASE, 21, struct sim_block_state)
#define NANDSIM_GET_BLOCK_STATE	_IOWR(NANDSIM_BASE, 22, struct sim_block_state)

struct sim_log {
	uint8_t		ctrl_num;
	char*		log;
	size_t		len;
};
#define NANDSIM_PRINT_LOG	_IOWR(NANDSIM_BASE, 23, struct sim_log)

struct sim_dump {
	uint8_t		ctrl_num;
	uint8_t		chip_num;
	uint32_t	block_num;
	uint32_t	len;
	void*		data;
};
#define NANDSIM_DUMP	_IOWR(NANDSIM_BASE, 24, struct sim_dump)
#define NANDSIM_RESTORE	_IOWR(NANDSIM_BASE, 25, struct sim_dump)

#endif /* _NANDSIM_H_ */
