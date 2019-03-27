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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/kthread.h>
#include <sys/unistd.h>

#include <dev/nand/nand.h>
#include <dev/nand/nandsim_chip.h>
#include <dev/nand/nandsim_log.h>
#include <dev/nand/nandsim_swap.h>

MALLOC_DEFINE(M_NANDSIM, "NANDsim", "NANDsim dynamic data");

#define NANDSIM_CHIP_LOCK(chip)		mtx_lock(&(chip)->ns_lock)
#define	NANDSIM_CHIP_UNLOCK(chip)	mtx_unlock(&(chip)->ns_lock)

static nandsim_evh_t erase_evh;
static nandsim_evh_t idle_evh;
static nandsim_evh_t poweron_evh;
static nandsim_evh_t reset_evh;
static nandsim_evh_t read_evh;
static nandsim_evh_t readid_evh;
static nandsim_evh_t readparam_evh;
static nandsim_evh_t write_evh;

static void nandsim_loop(void *);
static void nandsim_undefined(struct nandsim_chip *, uint8_t);
static void nandsim_bad_address(struct nandsim_chip *, uint8_t *);
static void nandsim_ignore_address(struct nandsim_chip *, uint8_t);
static void nandsim_sm_error(struct nandsim_chip *);
static void nandsim_start_handler(struct nandsim_chip *, nandsim_evh_t);

static void nandsim_callout_eh(void *);
static int  nandsim_delay(struct nandsim_chip *, int);

static int  nandsim_bbm_init(struct nandsim_chip *, uint32_t, uint32_t *);
static int  nandsim_blk_state_init(struct nandsim_chip *, uint32_t, uint32_t);
static void nandsim_blk_state_destroy(struct nandsim_chip *);
static int  nandchip_is_block_valid(struct nandsim_chip *, int);

static void nandchip_set_status(struct nandsim_chip *, uint8_t);
static void nandchip_clear_status(struct nandsim_chip *, uint8_t);

struct proc *nandsim_proc;

struct nandsim_chip *
nandsim_chip_init(struct nandsim_softc* sc, uint8_t chip_num,
    struct sim_chip *sim_chip)
{
	struct nandsim_chip *chip;
	struct onfi_params *chip_param;
	char swapfile[20];
	uint32_t size;
	int error;

	chip = malloc(sizeof(*chip), M_NANDSIM, M_WAITOK | M_ZERO);

	mtx_init(&chip->ns_lock, "nandsim lock", NULL, MTX_DEF);
	callout_init(&chip->ns_callout, 1);
	STAILQ_INIT(&chip->nandsim_events);

	chip->chip_num = chip_num;
	chip->ctrl_num = sim_chip->ctrl_num;
	chip->sc = sc;

	if (!sim_chip->is_wp)
		nandchip_set_status(chip, NAND_STATUS_WP);

	chip_param = &chip->params;

	chip->id.dev_id = sim_chip->device_id;
	chip->id.man_id = sim_chip->manufact_id;

	chip->error_ratio = sim_chip->error_ratio;
	chip->wear_level = sim_chip->wear_level;
	chip->prog_delay = sim_chip->prog_time;
	chip->erase_delay = sim_chip->erase_time;
	chip->read_delay = sim_chip->read_time;

	chip_param->t_prog = sim_chip->prog_time;
	chip_param->t_bers = sim_chip->erase_time;
	chip_param->t_r = sim_chip->read_time;
	bcopy("onfi", &chip_param->signature, 4);

	chip_param->manufacturer_id = sim_chip->manufact_id;
	strncpy(chip_param->manufacturer_name, sim_chip->manufacturer, 12);
	chip_param->manufacturer_name[11] = 0;
	strncpy(chip_param->device_model, sim_chip->device_model, 20);
	chip_param->device_model[19] = 0;

	chip_param->bytes_per_page = sim_chip->page_size;
	chip_param->spare_bytes_per_page = sim_chip->oob_size;
	chip_param->pages_per_block = sim_chip->pgs_per_blk;
	chip_param->blocks_per_lun = sim_chip->blks_per_lun;
	chip_param->luns = sim_chip->luns;

	init_chip_geom(&chip->cg, chip_param->luns, chip_param->blocks_per_lun,
	    chip_param->pages_per_block, chip_param->bytes_per_page,
	    chip_param->spare_bytes_per_page);

	chip_param->address_cycles = sim_chip->row_addr_cycles |
	    (sim_chip->col_addr_cycles << 4);
	chip_param->features = sim_chip->features;
	if (sim_chip->width == 16)
		chip_param->features |= ONFI_FEAT_16BIT;

	size = chip_param->blocks_per_lun * chip_param->luns;

	error = nandsim_blk_state_init(chip, size, sim_chip->wear_level);
	if (error) {
		mtx_destroy(&chip->ns_lock);
		free(chip, M_NANDSIM);
		return (NULL);
	}

	error = nandsim_bbm_init(chip, size, sim_chip->bad_block_map);
	if (error) {
		mtx_destroy(&chip->ns_lock);
		nandsim_blk_state_destroy(chip);
		free(chip, M_NANDSIM);
		return (NULL);
	}

	nandsim_start_handler(chip, poweron_evh);

	nand_debug(NDBG_SIM,"Create thread for chip%d [%8p]", chip->chip_num,
	    chip);
	/* Create chip thread */
	error = kproc_kthread_add(nandsim_loop, chip, &nandsim_proc,
	    &chip->nandsim_td, RFSTOPPED | RFHIGHPID,
	    0, "nandsim", "chip");
	if (error) {
		mtx_destroy(&chip->ns_lock);
		nandsim_blk_state_destroy(chip);
		free(chip, M_NANDSIM);
		return (NULL);
	}

	thread_lock(chip->nandsim_td);
	sched_class(chip->nandsim_td, PRI_REALTIME);
	sched_add(chip->nandsim_td, SRQ_BORING);
	thread_unlock(chip->nandsim_td);

	size = (chip_param->bytes_per_page +
	    chip_param->spare_bytes_per_page) *
	    chip_param->pages_per_block;

	sprintf(swapfile, "chip%d%d.swp", chip->ctrl_num, chip->chip_num);
	chip->swap = nandsim_swap_init(swapfile, chip_param->blocks_per_lun *
	    chip_param->luns, size);
	if (!chip->swap)
		nandsim_chip_destroy(chip);

	/* Wait for new thread to enter main loop */
	tsleep(chip->nandsim_td, PWAIT, "ns_chip", 1 * hz);

	return (chip);
}

static int
nandsim_blk_state_init(struct nandsim_chip *chip, uint32_t size,
    uint32_t wear_lev)
{
	int i;

	if (!chip || size == 0)
		return (-1);

	chip->blk_state = malloc(size * sizeof(struct nandsim_block_state),
	    M_NANDSIM, M_WAITOK | M_ZERO);

	for (i = 0; i < size; i++) {
		if (wear_lev)
			chip->blk_state[i].wear_lev = wear_lev;
		else
			chip->blk_state[i].wear_lev = -1;
	}

	return (0);
}

static void
nandsim_blk_state_destroy(struct nandsim_chip *chip)
{

	if (chip && chip->blk_state)
		free(chip->blk_state, M_NANDSIM);
}

static int
nandsim_bbm_init(struct nandsim_chip *chip, uint32_t size,
    uint32_t *sim_bbm)
{
	uint32_t index;
	int i;

	if ((chip == NULL) || (size == 0))
		return (-1);

	if (chip->blk_state == NULL)
		return (-1);

	if (sim_bbm == NULL)
		return (0);

	for (i = 0; i < MAX_BAD_BLOCKS; i++) {
		index = sim_bbm[i];

		if (index == 0xffffffff)
			break;
		else if (index > size)
			return (-1);
		else
			chip->blk_state[index].is_bad = 1;
	}

	return (0);
}

void
nandsim_chip_destroy(struct nandsim_chip *chip)
{
	struct nandsim_ev *ev;

	ev = create_event(chip, NANDSIM_EV_EXIT, 0);
	if (ev)
		send_event(ev);
}

void
nandsim_chip_freeze(struct nandsim_chip *chip)
{

	chip->flags |= NANDSIM_CHIP_FROZEN;
}

static void
nandsim_loop(void *arg)
{
	struct nandsim_chip *chip = (struct nandsim_chip *)arg;
	struct nandsim_ev *ev;

	nand_debug(NDBG_SIM,"Start main loop for chip%d [%8p]", chip->chip_num,
	    chip);
	for(;;) {
		NANDSIM_CHIP_LOCK(chip);
		if (!(chip->flags & NANDSIM_CHIP_ACTIVE)) {
			chip->flags |= NANDSIM_CHIP_ACTIVE;
			wakeup(chip->nandsim_td);
		}

		if (STAILQ_EMPTY(&chip->nandsim_events)) {
			nand_debug(NDBG_SIM,"Chip%d [%8p] going sleep",
			    chip->chip_num, chip);
			msleep(chip, &chip->ns_lock, PRIBIO, "nandev", 0);
		}

		ev = STAILQ_FIRST(&chip->nandsim_events);
		STAILQ_REMOVE_HEAD(&chip->nandsim_events, links);
		NANDSIM_CHIP_UNLOCK(chip);
		if (ev->type == NANDSIM_EV_EXIT) {
			NANDSIM_CHIP_LOCK(chip);
			destroy_event(ev);
			wakeup(ev);
			while (!STAILQ_EMPTY(&chip->nandsim_events)) {
				ev = STAILQ_FIRST(&chip->nandsim_events);
				STAILQ_REMOVE_HEAD(&chip->nandsim_events,
				    links);
				destroy_event(ev);
				wakeup(ev);
			}
			NANDSIM_CHIP_UNLOCK(chip);
			nandsim_log(chip, NANDSIM_LOG_SM, "destroyed\n");
			mtx_destroy(&chip->ns_lock);
			nandsim_blk_state_destroy(chip);
			nandsim_swap_destroy(chip->swap);
			free(chip, M_NANDSIM);
			nandsim_proc = NULL;

			kthread_exit();
		}

		if (!(chip->flags & NANDSIM_CHIP_FROZEN)) {
			nand_debug(NDBG_SIM,"Chip [%x] get event [%x]",
			    chip->chip_num, ev->type);
			chip->ev_handler(chip, ev->type, ev->data);
		}

		wakeup(ev);
		destroy_event(ev);
	}

}

struct nandsim_ev *
create_event(struct nandsim_chip *chip, uint8_t type, uint8_t data_size)
{
	struct nandsim_ev *ev;

	ev = malloc(sizeof(*ev), M_NANDSIM, M_NOWAIT | M_ZERO);
	if (!ev) {
		nand_debug(NDBG_SIM,"Cannot create event");
		return (NULL);
	}

	if (data_size > 0)
		ev->data = malloc(sizeof(*ev), M_NANDSIM, M_NOWAIT | M_ZERO);
	ev->type = type;
	ev->chip = chip;

	return (ev);
}

void
destroy_event(struct nandsim_ev *ev)
{

	if (ev->data)
		free(ev->data, M_NANDSIM);
	free(ev, M_NANDSIM);
}

int
send_event(struct nandsim_ev *ev)
{
	struct nandsim_chip *chip = ev->chip;

	if (!(chip->flags & NANDSIM_CHIP_FROZEN)) {
		nand_debug(NDBG_SIM,"Chip%d [%p] send event %x",
		    chip->chip_num, chip, ev->type);

		NANDSIM_CHIP_LOCK(chip);
		STAILQ_INSERT_TAIL(&chip->nandsim_events, ev, links);
		NANDSIM_CHIP_UNLOCK(chip);

		wakeup(chip);
		if ((ev->type != NANDSIM_EV_TIMEOUT) && chip->nandsim_td &&
		    (curthread != chip->nandsim_td))
			tsleep(ev, PWAIT, "ns_ev", 5 * hz);
	}

	return (0);
}

static void
nandsim_callout_eh(void *arg)
{
	struct nandsim_ev *ev = (struct nandsim_ev *)arg;

	send_event(ev);
}

static int
nandsim_delay(struct nandsim_chip *chip, int timeout)
{
	struct nandsim_ev *ev;
	struct timeval delay;
	int tm;

	nand_debug(NDBG_SIM,"Chip[%d] Set delay: %d", chip->chip_num, timeout);

	ev = create_event(chip, NANDSIM_EV_TIMEOUT, 0);
	if (!ev)
		return (-1);

	chip->sm_state = NANDSIM_STATE_TIMEOUT;
	tm = (timeout/10000) * (hz / 100);
	if (callout_reset(&chip->ns_callout, tm, nandsim_callout_eh, ev))
		return (-1);

	delay.tv_sec = chip->read_delay / 1000000;
	delay.tv_usec = chip->read_delay % 1000000;
	timevaladd(&chip->delay_tv, &delay);

	return (0);
}

static void
nandsim_start_handler(struct nandsim_chip *chip, nandsim_evh_t evh)
{
	struct nandsim_ev *ev;

	chip->ev_handler = evh;

	nand_debug(NDBG_SIM,"Start handler %p for chip%d [%p]", evh,
	    chip->chip_num, chip);
	ev = create_event(chip, NANDSIM_EV_START, 0);
	if (!ev)
		nandsim_sm_error(chip);

	send_event(ev);
}

static void
nandchip_set_data(struct nandsim_chip *chip, uint8_t *data, uint32_t len,
    uint32_t idx)
{

	nand_debug(NDBG_SIM,"Chip [%x] data %p [%x] at %x", chip->chip_num,
	    data, len, idx);
	chip->data.data_ptr = data;
	chip->data.size = len;
	chip->data.index = idx;
}

static int
nandchip_chip_space(struct nandsim_chip *chip, int32_t row, int32_t column,
    size_t size, uint8_t writing)
{
	struct block_space *blk_space;
	uint32_t lun, block, page, offset, block_size;
	int err;

	block_size = chip->cg.block_size +
	    (chip->cg.oob_size * chip->cg.pgs_per_blk);

	err = nand_row_to_blkpg(&chip->cg, row, &lun, &block, &page);
	if (err) {
		nand_debug(NDBG_SIM,"cannot get address\n");
		return (-1);
	}

	if (!nandchip_is_block_valid(chip, block)) {
		nandchip_set_data(chip, NULL, 0, 0);
		return (-1);
	}

	blk_space = get_bs(chip->swap, block, writing);
	if (!blk_space) {
		nandchip_set_data(chip, NULL, 0, 0);
		return (-1);
	}

	if (size > block_size)
		size = block_size;

	if (size == block_size) {
		offset = 0;
		column = 0;
	} else
		offset = page * (chip->cg.page_size + chip->cg.oob_size);

	nandchip_set_data(chip, &blk_space->blk_ptr[offset], size, column);

	return (0);
}

static int
nandchip_get_addr_byte(struct nandsim_chip *chip, void *data, uint32_t *value)
{
	int ncycles = 0;
	uint8_t byte;
	uint8_t *buffer;

	buffer = (uint8_t *)value;
	byte = *((uint8_t *)data);

	KASSERT((chip->sm_state == NANDSIM_STATE_WAIT_ADDR_ROW ||
	    chip->sm_state == NANDSIM_STATE_WAIT_ADDR_COL),
	    ("unexpected state"));

	if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_ROW) {
		ncycles = chip->params.address_cycles & 0xf;
		buffer[chip->sm_addr_cycle++] = byte;
	} else if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_COL) {
		ncycles = (chip->params.address_cycles >> 4) & 0xf;
		buffer[chip->sm_addr_cycle++] = byte;
	}

	nand_debug(NDBG_SIM, "Chip [%x] read addr byte: %02x (%d of %d)\n",
	    chip->chip_num, byte, chip->sm_addr_cycle, ncycles);

	if (chip->sm_addr_cycle == ncycles) {
		chip->sm_addr_cycle = 0;
		return (0);
	}

	return (1);
}

static int
nandchip_is_block_valid(struct nandsim_chip *chip, int block_num)
{

	if (!chip || !chip->blk_state)
		return (0);

	if (chip->blk_state[block_num].wear_lev == 0 ||
	    chip->blk_state[block_num].is_bad)
		return (0);

	return (1);
}

static void
nandchip_set_status(struct nandsim_chip *chip, uint8_t flags)
{

	chip->chip_status |= flags;
}

static void
nandchip_clear_status(struct nandsim_chip *chip, uint8_t flags)
{

	chip->chip_status &= ~flags;
}

uint8_t
nandchip_get_status(struct nandsim_chip *chip)
{
	return (chip->chip_status);
}

void
nandsim_chip_timeout(struct nandsim_chip *chip)
{
	struct timeval tv;

	getmicrotime(&tv);

	if (chip->sm_state == NANDSIM_STATE_TIMEOUT &&
	    timevalcmp(&tv, &chip->delay_tv, >=)) {
		nandchip_set_status(chip, NAND_STATUS_RDY);
	}
}
void
poweron_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	uint8_t cmd;

	if (type == NANDSIM_EV_START)
		chip->sm_state = NANDSIM_STATE_IDLE;
	else if (type == NANDSIM_EV_CMD) {
		cmd = *(uint8_t *)data;
		switch(cmd) {
		case NAND_CMD_RESET:
			nandsim_log(chip, NANDSIM_LOG_SM, "in RESET state\n");
			nandsim_start_handler(chip, reset_evh);
			break;
		default:
			nandsim_undefined(chip, type);
			break;
		}
	} else
		nandsim_undefined(chip, type);
}

void
idle_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	uint8_t cmd;

	if (type == NANDSIM_EV_START) {
		nandsim_log(chip, NANDSIM_LOG_SM, "in IDLE state\n");
		chip->sm_state = NANDSIM_STATE_WAIT_CMD;
	} else if (type == NANDSIM_EV_CMD) {
		nandchip_clear_status(chip, NAND_STATUS_FAIL);
		getmicrotime(&chip->delay_tv);
		cmd = *(uint8_t *)data;
		switch(cmd) {
		case NAND_CMD_READ_ID:
			nandsim_start_handler(chip, readid_evh);
			break;
		case NAND_CMD_READ_PARAMETER:
			nandsim_start_handler(chip, readparam_evh);
			break;
		case NAND_CMD_READ:
			nandsim_start_handler(chip, read_evh);
			break;
		case NAND_CMD_PROG:
			nandsim_start_handler(chip, write_evh);
			break;
		case NAND_CMD_ERASE:
			nandsim_start_handler(chip, erase_evh);
			break;
		default:
			nandsim_undefined(chip, type);
			break;
		}
	} else
		nandsim_undefined(chip, type);
}

void
readid_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	struct onfi_params *params;
	uint8_t addr;

	params = &chip->params;

	if (type == NANDSIM_EV_START) {
		nandsim_log(chip, NANDSIM_LOG_SM, "in READID state\n");
		chip->sm_state = NANDSIM_STATE_WAIT_ADDR_BYTE;
	} else if (type == NANDSIM_EV_ADDR) {

		addr = *((uint8_t *)data);

		if (addr == 0x0)
			nandchip_set_data(chip, (uint8_t *)&chip->id, 2, 0);
		else if (addr == ONFI_SIG_ADDR)
			nandchip_set_data(chip, (uint8_t *)&params->signature,
			    4, 0);
		else
			nandsim_bad_address(chip, &addr);

		nandsim_start_handler(chip, idle_evh);
	} else
		nandsim_undefined(chip, type);
}

void
readparam_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	struct onfi_params *params;
	uint8_t addr;

	params = &chip->params;

	if (type == NANDSIM_EV_START) {
		nandsim_log(chip, NANDSIM_LOG_SM, "in READPARAM state\n");
		chip->sm_state = NANDSIM_STATE_WAIT_ADDR_BYTE;
	} else if (type == NANDSIM_EV_ADDR) {
		addr = *((uint8_t *)data);

		if (addr == 0) {
			nandchip_set_data(chip, (uint8_t *)params,
			    sizeof(*params), 0);
		} else
			nandsim_bad_address(chip, &addr);

		nandsim_start_handler(chip, idle_evh);
	} else
		nandsim_undefined(chip, type);
}

void
read_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	static uint32_t column = 0, row = 0;
	uint32_t size;
	uint8_t cmd;

	size = chip->cg.page_size + chip->cg.oob_size;

	switch (type) {
	case NANDSIM_EV_START:
		nandsim_log(chip, NANDSIM_LOG_SM, "in READ state\n");
		chip->sm_state = NANDSIM_STATE_WAIT_ADDR_COL;
		break;
	case NANDSIM_EV_ADDR:
		if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_COL) {
			if (nandchip_get_addr_byte(chip, data, &column))
				break;

			chip->sm_state = NANDSIM_STATE_WAIT_ADDR_ROW;
		} else if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_ROW) {
			if (nandchip_get_addr_byte(chip, data, &row))
				break;

			chip->sm_state = NANDSIM_STATE_WAIT_CMD;
		} else
			nandsim_ignore_address(chip, *((uint8_t *)data));
		break;
	case NANDSIM_EV_CMD:
		cmd = *(uint8_t *)data;
		if (chip->sm_state == NANDSIM_STATE_WAIT_CMD &&
		    cmd == NAND_CMD_READ_END) {
			if (chip->read_delay != 0 &&
			    nandsim_delay(chip, chip->read_delay) == 0)
				nandchip_clear_status(chip, NAND_STATUS_RDY);
			else {
				nandchip_chip_space(chip, row, column, size, 0);
				nandchip_set_status(chip, NAND_STATUS_RDY);
				nandsim_start_handler(chip, idle_evh);
			}
		} else
			nandsim_undefined(chip, type);
		break;
	case NANDSIM_EV_TIMEOUT:
		if (chip->sm_state == NANDSIM_STATE_TIMEOUT) {
			nandchip_chip_space(chip, row, column, size, 0);
			nandchip_set_status(chip, NAND_STATUS_RDY);
			nandsim_start_handler(chip, idle_evh);
		} else
			nandsim_undefined(chip, type);
		break;
	}
}
void
write_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	static uint32_t column, row;
	uint32_t size;
	uint8_t cmd;
	int err;

	size = chip->cg.page_size + chip->cg.oob_size;

	switch(type) {
	case NANDSIM_EV_START:
		nandsim_log(chip, NANDSIM_LOG_SM, "in WRITE state\n");
		chip->sm_state = NANDSIM_STATE_WAIT_ADDR_COL;
		break;
	case NANDSIM_EV_ADDR:
		if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_COL) {
			if (nandchip_get_addr_byte(chip, data, &column))
				break;

			chip->sm_state = NANDSIM_STATE_WAIT_ADDR_ROW;
		} else if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_ROW) {
			if (nandchip_get_addr_byte(chip, data, &row))
				break;

			err = nandchip_chip_space(chip, row, column, size, 1);
			if (err == -1)
				nandchip_set_status(chip, NAND_STATUS_FAIL);

			chip->sm_state = NANDSIM_STATE_WAIT_CMD;
		} else
			nandsim_ignore_address(chip, *((uint8_t *)data));
		break;
	case NANDSIM_EV_CMD:
		cmd = *(uint8_t *)data;
		if (chip->sm_state == NANDSIM_STATE_WAIT_CMD &&
		    cmd == NAND_CMD_PROG_END) {
			if (chip->prog_delay != 0 &&
			    nandsim_delay(chip, chip->prog_delay) == 0)
				nandchip_clear_status(chip, NAND_STATUS_RDY);
			else {
				nandchip_set_status(chip, NAND_STATUS_RDY);
				nandsim_start_handler(chip, idle_evh);
			}
		} else
			nandsim_undefined(chip, type);
		break;
	case NANDSIM_EV_TIMEOUT:
		if (chip->sm_state == NANDSIM_STATE_TIMEOUT) {
			nandsim_start_handler(chip, idle_evh);
			nandchip_set_status(chip, NAND_STATUS_RDY);
		} else
			nandsim_undefined(chip, type);
		break;
	}
}

void
erase_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{
	static uint32_t row, block_size;
	uint32_t lun, block, page;
	int err;
	uint8_t cmd;

	block_size = chip->cg.block_size +
	    (chip->cg.oob_size * chip->cg.pgs_per_blk);

	switch (type) {
	case NANDSIM_EV_START:
		nandsim_log(chip, NANDSIM_LOG_SM, "in ERASE state\n");
		chip->sm_state = NANDSIM_STATE_WAIT_ADDR_ROW;
		break;
	case NANDSIM_EV_CMD:
		cmd = *(uint8_t *)data;
		if (chip->sm_state == NANDSIM_STATE_WAIT_CMD &&
		    cmd == NAND_CMD_ERASE_END) {
			if (chip->data.data_ptr != NULL &&
			    chip->data.size == block_size)
				memset(chip->data.data_ptr, 0xff, block_size);
			else
				nand_debug(NDBG_SIM,"Bad block erase data\n");

			err = nand_row_to_blkpg(&chip->cg, row, &lun,
			    &block, &page);
			if (!err) {
				if (chip->blk_state[block].wear_lev > 0)
					chip->blk_state[block].wear_lev--;
			}

			if (chip->erase_delay != 0 &&
			    nandsim_delay(chip, chip->erase_delay) == 0)
				nandchip_clear_status(chip, NAND_STATUS_RDY);
			else {
				nandchip_set_status(chip, NAND_STATUS_RDY);
				nandsim_start_handler(chip, idle_evh);
			}
		} else
			nandsim_undefined(chip, type);
		break;
	case NANDSIM_EV_ADDR:
		if (chip->sm_state == NANDSIM_STATE_WAIT_ADDR_ROW) {
			if (nandchip_get_addr_byte(chip, data, &row))
				break;

			err = nandchip_chip_space(chip, row, 0, block_size, 1);
			if (err == -1) {
				nandchip_set_status(chip, NAND_STATUS_FAIL);
			}
			chip->sm_state = NANDSIM_STATE_WAIT_CMD;
		} else
			nandsim_ignore_address(chip, *((uint8_t *)data));
		break;
	case NANDSIM_EV_TIMEOUT:
		if (chip->sm_state == NANDSIM_STATE_TIMEOUT) {
			nandchip_set_status(chip, NAND_STATUS_RDY);
			nandsim_start_handler(chip, idle_evh);
		} else
			nandsim_undefined(chip, type);
		break;
	}
}

void
reset_evh(struct nandsim_chip *chip, uint32_t type, void *data)
{

	if (type == NANDSIM_EV_START) {
		nandsim_log(chip, NANDSIM_LOG_SM, "in RESET state\n");
		chip->sm_state = NANDSIM_STATE_TIMEOUT;
		nandchip_set_data(chip, NULL, 0, 0);
		DELAY(500);
		nandsim_start_handler(chip, idle_evh);
	} else
		nandsim_undefined(chip, type);
}

static void
nandsim_undefined(struct nandsim_chip *chip, uint8_t type)
{

	nandsim_log(chip, NANDSIM_LOG_ERR,
	    "ERR: Chip received ev %x in state %x\n",
	    type, chip->sm_state);
	nandsim_start_handler(chip, idle_evh);
}

static void
nandsim_bad_address(struct nandsim_chip *chip, uint8_t *addr)
{

	nandsim_log(chip, NANDSIM_LOG_ERR,
	    "ERR: Chip received out of range address"
	    "%02x%02x - %02x%02x%02x\n", addr[0], addr[1], addr[2],
	    addr[3], addr[4]);
}

static void
nandsim_ignore_address(struct nandsim_chip *chip, uint8_t byte)
{
	nandsim_log(chip, NANDSIM_LOG_SM, "ignored address byte: %d\n", byte);
}

static void
nandsim_sm_error(struct nandsim_chip *chip)
{

	nandsim_log(chip, NANDSIM_LOG_ERR, "ERR: State machine error."
	    "Restart required.\n");
}
