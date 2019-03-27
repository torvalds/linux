/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Nathan Whitehorn
 * All rights reserved
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <machine/dbdma.h>
#include <sys/rman.h>

#include "dbdmavar.h"

static MALLOC_DEFINE(M_DBDMA, "dbdma", "DBDMA Command List");

static uint32_t dbdma_read_reg(dbdma_channel_t *, u_int);
static void dbdma_write_reg(dbdma_channel_t *, u_int, uint32_t);
static void dbdma_phys_callback(void *, bus_dma_segment_t *, int, int);

static void
dbdma_phys_callback(void *chan, bus_dma_segment_t *segs, int nsegs, int error)
{
	dbdma_channel_t *channel = (dbdma_channel_t *)(chan);

	channel->sc_slots_pa = segs[0].ds_addr;
	dbdma_write_reg(channel, CHAN_CMDPTR, channel->sc_slots_pa);
}

int
dbdma_allocate_channel(struct resource *dbdma_regs, u_int offset,
    bus_dma_tag_t parent_dma, int slots, dbdma_channel_t **chan)
{
	int error = 0;
	dbdma_channel_t *channel;

	channel = *chan = malloc(sizeof(struct dbdma_channel), M_DBDMA, 
	    M_WAITOK | M_ZERO);

	channel->sc_regs = dbdma_regs;
	channel->sc_off = offset;
	dbdma_stop(channel);

	channel->sc_slots_pa = 0;

	error = bus_dma_tag_create(parent_dma, 16, 0, BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR, NULL, NULL, PAGE_SIZE, 1, PAGE_SIZE, 0, NULL,
	    NULL, &(channel->sc_dmatag));

	error = bus_dmamem_alloc(channel->sc_dmatag,
	    (void **)&channel->sc_slots, BUS_DMA_WAITOK | BUS_DMA_ZERO,
	    &channel->sc_dmamap);

	error = bus_dmamap_load(channel->sc_dmatag, channel->sc_dmamap,
	    channel->sc_slots, PAGE_SIZE, dbdma_phys_callback, channel, 0);

	dbdma_write_reg(channel, CHAN_CMDPTR_HI, 0);

	channel->sc_nslots = slots;

	return (error);
}

int
dbdma_resize_channel(dbdma_channel_t *chan, int newslots)
{

	if (newslots > (PAGE_SIZE / sizeof(struct dbdma_command)))
		return (-1);
	
	chan->sc_nslots = newslots;
	return (0);
}

int
dbdma_free_channel(dbdma_channel_t *chan)
{

	dbdma_stop(chan);

	bus_dmamem_free(chan->sc_dmatag, chan->sc_slots, chan->sc_dmamap);
	bus_dma_tag_destroy(chan->sc_dmatag);

	free(chan, M_DBDMA);

	return (0);
}

uint16_t
dbdma_get_cmd_status(dbdma_channel_t *chan, int slot)
{

	bus_dmamap_sync(chan->sc_dmatag, chan->sc_dmamap, BUS_DMASYNC_POSTREAD);

	/*
	 * I really did mean to swap resCount and xferStatus here, to
	 * account for the quad-word little endian fields.
	 */
	return (le16toh(chan->sc_slots[slot].resCount));
}

void
dbdma_clear_cmd_status(dbdma_channel_t *chan, int slot)
{
	/* See endian note above */
	chan->sc_slots[slot].resCount = 0;
}

uint16_t
dbdma_get_residuals(dbdma_channel_t *chan, int slot)
{

	bus_dmamap_sync(chan->sc_dmatag, chan->sc_dmamap, BUS_DMASYNC_POSTREAD);

	return (le16toh(chan->sc_slots[slot].xferStatus));
}

void
dbdma_reset(dbdma_channel_t *chan)
{

	dbdma_stop(chan);
	dbdma_set_current_cmd(chan, 0);
	dbdma_run(chan);
}

void
dbdma_run(dbdma_channel_t *chan)
{
	uint32_t control_reg;

	control_reg = DBDMA_STATUS_RUN | DBDMA_STATUS_PAUSE |
	    DBDMA_STATUS_WAKE | DBDMA_STATUS_DEAD;
	control_reg <<= DBDMA_REG_MASK_SHIFT;

	control_reg |= DBDMA_STATUS_RUN;
	dbdma_write_reg(chan, CHAN_CONTROL_REG, control_reg);
}

void
dbdma_pause(dbdma_channel_t *chan)
{
	uint32_t control_reg;

	control_reg = DBDMA_STATUS_PAUSE;
	control_reg <<= DBDMA_REG_MASK_SHIFT;

	control_reg |= DBDMA_STATUS_PAUSE;
	dbdma_write_reg(chan, CHAN_CONTROL_REG, control_reg);
}

void
dbdma_wake(dbdma_channel_t *chan)
{
	uint32_t control_reg;

	control_reg = DBDMA_STATUS_WAKE | DBDMA_STATUS_PAUSE |
	    DBDMA_STATUS_RUN | DBDMA_STATUS_DEAD;
	control_reg <<= DBDMA_REG_MASK_SHIFT;

	control_reg |= DBDMA_STATUS_WAKE | DBDMA_STATUS_RUN;
	dbdma_write_reg(chan, CHAN_CONTROL_REG, control_reg);
}

void
dbdma_stop(dbdma_channel_t *chan)
{
	uint32_t control_reg;

	control_reg = DBDMA_STATUS_RUN;
	control_reg <<= DBDMA_REG_MASK_SHIFT;

	dbdma_write_reg(chan, CHAN_CONTROL_REG, control_reg);

	while (dbdma_read_reg(chan, CHAN_STATUS_REG) & DBDMA_STATUS_ACTIVE)
		DELAY(5);
}

void
dbdma_set_current_cmd(dbdma_channel_t *chan, int slot)
{
	uint32_t cmd;

	cmd = chan->sc_slots_pa + slot * sizeof(struct dbdma_command);
	dbdma_write_reg(chan, CHAN_CMDPTR, cmd);
}

uint16_t
dbdma_get_chan_status(dbdma_channel_t *chan)
{
	uint32_t status_reg;

	status_reg = dbdma_read_reg(chan, CHAN_STATUS_REG);
	return (status_reg & 0x0000ffff);
}

uint8_t
dbdma_get_device_status(dbdma_channel_t *chan)
{
	return (dbdma_get_chan_status(chan) & 0x00ff);
}

void
dbdma_set_device_status(dbdma_channel_t *chan, uint8_t mask, uint8_t value)
{
	uint32_t control_reg;
	
	control_reg = mask;
	control_reg <<= DBDMA_REG_MASK_SHIFT;
	control_reg |= value;

	dbdma_write_reg(chan, CHAN_CONTROL_REG, control_reg);
}

void
dbdma_set_interrupt_selector(dbdma_channel_t *chan, uint8_t mask, uint8_t val)
{
	uint32_t intr_select;

	intr_select = mask;
	intr_select <<= DBDMA_REG_MASK_SHIFT;

	intr_select |= val;
	dbdma_write_reg(chan, CHAN_INTR_SELECT, intr_select);
}

void
dbdma_set_branch_selector(dbdma_channel_t *chan, uint8_t mask, uint8_t val)
{
	uint32_t br_select;

	br_select = mask;
	br_select <<= DBDMA_REG_MASK_SHIFT;

	br_select |= val;
	dbdma_write_reg(chan, CHAN_BRANCH_SELECT, br_select);
}

void
dbdma_set_wait_selector(dbdma_channel_t *chan, uint8_t mask, uint8_t val)
{
	uint32_t wait_select;

	wait_select = mask;
	wait_select <<= DBDMA_REG_MASK_SHIFT;
	wait_select |= val;
	dbdma_write_reg(chan, CHAN_WAIT_SELECT, wait_select);
}

void
dbdma_insert_command(dbdma_channel_t *chan, int slot, int command, int stream,
    bus_addr_t data, size_t count, uint8_t interrupt, uint8_t branch,
    uint8_t wait, uint32_t branch_slot)
{
	struct dbdma_command cmd;
	uint32_t *flip;

	cmd.cmd = command;
	cmd.key = stream;
	cmd.intr = interrupt;
	cmd.branch = branch;
	cmd.wait = wait;

	cmd.reqCount = count;
	cmd.address = (uint32_t)(data);
	if (command != DBDMA_STORE_QUAD && command != DBDMA_LOAD_QUAD)
		cmd.cmdDep = chan->sc_slots_pa + 
		    branch_slot * sizeof(struct dbdma_command);
	else
		cmd.cmdDep = branch_slot;

	cmd.resCount = 0;
	cmd.xferStatus = 0;

	/*
	 * Move quadwords to little-endian. God only knows why
	 * Apple thought this was a good idea.
	 */
	flip = (uint32_t *)(&cmd);
	flip[0] = htole32(flip[0]);
	flip[1] = htole32(flip[1]);
	flip[2] = htole32(flip[2]);

	chan->sc_slots[slot] = cmd;
}

void
dbdma_insert_stop(dbdma_channel_t *chan, int slot)
{

	dbdma_insert_command(chan, slot, DBDMA_STOP, 0, 0, 0, DBDMA_NEVER,
	    DBDMA_NEVER, DBDMA_NEVER, 0);
}

void
dbdma_insert_nop(dbdma_channel_t *chan, int slot)
{

	dbdma_insert_command(chan, slot, DBDMA_NOP, 0, 0, 0, DBDMA_NEVER,
	    DBDMA_NEVER, DBDMA_NEVER, 0);
}

void
dbdma_insert_branch(dbdma_channel_t *chan, int slot, int to_slot)
{

	dbdma_insert_command(chan, slot, DBDMA_NOP, 0, 0, 0, DBDMA_NEVER,
	    DBDMA_ALWAYS, DBDMA_NEVER, to_slot);
}

void
dbdma_sync_commands(dbdma_channel_t *chan, bus_dmasync_op_t op)
{

	bus_dmamap_sync(chan->sc_dmatag, chan->sc_dmamap, op);
}

void
dbdma_save_state(dbdma_channel_t *chan)
{

	chan->sc_saved_regs[0] = dbdma_read_reg(chan, CHAN_CMDPTR);
	chan->sc_saved_regs[1] = dbdma_read_reg(chan, CHAN_CMDPTR_HI);
	chan->sc_saved_regs[2] = dbdma_read_reg(chan, CHAN_INTR_SELECT);
	chan->sc_saved_regs[3] = dbdma_read_reg(chan, CHAN_BRANCH_SELECT);
	chan->sc_saved_regs[4] = dbdma_read_reg(chan, CHAN_WAIT_SELECT);

	dbdma_stop(chan);
}

void
dbdma_restore_state(dbdma_channel_t *chan)
{

	dbdma_wake(chan);
	dbdma_write_reg(chan, CHAN_CMDPTR, chan->sc_saved_regs[0]);
	dbdma_write_reg(chan, CHAN_CMDPTR_HI, chan->sc_saved_regs[1]);
	dbdma_write_reg(chan, CHAN_INTR_SELECT, chan->sc_saved_regs[2]);
	dbdma_write_reg(chan, CHAN_BRANCH_SELECT, chan->sc_saved_regs[3]);
	dbdma_write_reg(chan, CHAN_WAIT_SELECT, chan->sc_saved_regs[4]);
}

static uint32_t
dbdma_read_reg(dbdma_channel_t *chan, u_int offset)
{

	return (bus_read_4(chan->sc_regs, chan->sc_off + offset));
}

static void
dbdma_write_reg(dbdma_channel_t *chan, u_int offset, uint32_t val)
{

	bus_write_4(chan->sc_regs, chan->sc_off + offset, val);
}
