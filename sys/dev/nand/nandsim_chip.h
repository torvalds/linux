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

#ifndef _NANDSIM_CHIP_H
#define _NANDSIM_CHIP_H

#include <sys/malloc.h>
#include <sys/callout.h>
#include <dev/nand/nand.h>
#include <dev/nand/nandsim.h>
#include <dev/nand/nandsim_swap.h>

MALLOC_DECLARE(M_NANDSIM);

#define MAX_CS_NUM	4
struct nandsim_chip;

typedef void nandsim_evh_t(struct nandsim_chip *chip, uint32_t ev, void *data);

enum addr_type {
	ADDR_NONE,
	ADDR_ID,
	ADDR_ROW,
	ADDR_ROWCOL
};

struct nandsim_softc {
	struct nand_softc	nand_dev;
	device_t		dev;

	struct nandsim_chip	*chips[MAX_CS_NUM];
	struct nandsim_chip	*active_chip;

	uint8_t			address_cycle;
	enum addr_type		address_type;
	int			log_idx;
	char			*log_buff;
	struct alq		*alq;
};

struct nandsim_ev {
	STAILQ_ENTRY(nandsim_ev)	links;
	struct nandsim_chip		*chip;
	uint8_t		type;
	void		*data;
};

struct nandsim_data {
	uint8_t		*data_ptr;
	uint32_t	index;
	uint32_t	size;
};

struct nandsim_block_state {
	int32_t		wear_lev;
	uint8_t		is_bad;
};

#define NANDSIM_CHIP_ACTIVE	0x1
#define NANDSIM_CHIP_FROZEN	0x2
#define NANDSIM_CHIP_GET_STATUS	0x4

struct nandsim_chip {
	struct nandsim_softc	*sc;
	struct thread		*nandsim_td;

	STAILQ_HEAD(, nandsim_ev) nandsim_events;
	nandsim_evh_t		*ev_handler;
	struct mtx		ns_lock;
	struct callout		ns_callout;

	struct chip_geom	cg;
	struct nand_id		id;
	struct onfi_params	params;
	struct nandsim_data	data;
	struct nandsim_block_state *blk_state;

	struct chip_swap	*swap;

	uint32_t	error_ratio;
	uint32_t	wear_level;
	uint32_t	sm_state;
	uint32_t	sm_addr_cycle;

	uint32_t	erase_delay;
	uint32_t	prog_delay;
	uint32_t	read_delay;
	struct timeval	delay_tv;

	uint8_t		flags;
	uint8_t		chip_status;
	uint8_t		ctrl_num;
	uint8_t		chip_num;
};

struct sim_ctrl_conf {
	uint8_t		num;
	uint8_t		num_cs;
	uint8_t		ecc;
	uint8_t		running;
	uint8_t		created;
	device_t	sim_ctrl_dev;
	struct sim_chip	*chips[MAX_CTRL_CS];
	uint16_t	ecc_layout[MAX_ECC_BYTES];
	char		filename[FILENAME_SIZE];
};

#define NANDSIM_STATE_IDLE		0x0
#define NANDSIM_STATE_WAIT_ADDR_BYTE	0x1
#define NANDSIM_STATE_WAIT_CMD		0x2
#define NANDSIM_STATE_TIMEOUT		0x3
#define	NANDSIM_STATE_WAIT_ADDR_ROW	0x4
#define	NANDSIM_STATE_WAIT_ADDR_COL	0x5

#define NANDSIM_EV_START	0x1
#define NANDSIM_EV_CMD		0x2
#define NANDSIM_EV_ADDR		0x3
#define NANDSIM_EV_TIMEOUT	0x4
#define NANDSIM_EV_EXIT		0xff

struct nandsim_chip *nandsim_chip_init(struct nandsim_softc *,
    uint8_t, struct sim_chip *);
void nandsim_chip_destroy(struct nandsim_chip *);
void nandsim_chip_freeze(struct nandsim_chip *);
void nandsim_chip_timeout(struct nandsim_chip *);
int nandsim_chip_check_bad_block(struct nandsim_chip *, int);

uint8_t nandchip_get_status(struct nandsim_chip *);

void destroy_event(struct nandsim_ev *);
int send_event(struct nandsim_ev *);
struct nandsim_ev *create_event(struct nandsim_chip *, uint8_t, uint8_t);

#endif /*  _NANDSIM_CHIP_H */
