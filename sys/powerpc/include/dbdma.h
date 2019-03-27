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
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_DBDMA_H_
#define _MACHINE_DBDMA_H_

#include <sys/param.h>
#include <machine/bus.h>

/* 
 * Apple's DBDMA (Descriptor-based DMA) interface is a common DMA engine
 * used by a variety of custom Apple ASICs. It is described in the CHRP
 * specification and in the book Macintosh Technology in the Common
 * Hardware Reference Platform, copyright 1995 Apple Computer.
 */

/* DBDMA Command Values */

enum {
	DBDMA_OUTPUT_MORE	= 0,
	DBDMA_OUTPUT_LAST	= 1,
	DBDMA_INPUT_MORE	= 2,
	DBDMA_INPUT_LAST	= 3,

	DBDMA_STORE_QUAD	= 4,
	DBDMA_LOAD_QUAD		= 5,
	DBDMA_NOP		= 6,
	DBDMA_STOP		= 7
};

/* These codes are for the interrupt, branch, and wait flags */

enum {
	DBDMA_NEVER		= 0,
	DBDMA_COND_TRUE		= 1,
	DBDMA_COND_FALSE	= 2,
	DBDMA_ALWAYS		= 3
};

/* Channel status bits */
#define DBDMA_STATUS_RUN    (0x01 << 15)
#define DBDMA_STATUS_PAUSE  (0x01 << 14)
#define DBDMA_STATUS_FLUSH  (0x01 << 13)
#define DBDMA_STATUS_WAKE   (0x01 << 12)
#define DBDMA_STATUS_DEAD   (0x01 << 11)
#define DBDMA_STATUS_ACTIVE (0x01 << 10)

/* Set by hardware if a branch was taken */
#define DBDMA_STATUS_BRANCH 8

struct dbdma_command;
typedef struct dbdma_command dbdma_command_t;
struct dbdma_channel;
typedef struct dbdma_channel dbdma_channel_t;

int dbdma_allocate_channel(struct resource *dbdma_regs, u_int offset,
    bus_dma_tag_t parent_dma, int slots, dbdma_channel_t **chan);

int dbdma_resize_channel(dbdma_channel_t *chan, int newslots);
int dbdma_free_channel(dbdma_channel_t *chan);

void dbdma_run(dbdma_channel_t *chan);
void dbdma_stop(dbdma_channel_t *chan);
void dbdma_reset(dbdma_channel_t *chan);
void dbdma_set_current_cmd(dbdma_channel_t *chan, int slot);

void dbdma_pause(dbdma_channel_t *chan);
void dbdma_wake(dbdma_channel_t *chan);

/*
 * DBDMA uses a 16 bit channel control register to describe the current
 * state of DMA on the channel. The high-order bits (8-15) contain information
 * on the run state and are listed in the DBDMA_STATUS_* constants above. These
 * are manipulated with the dbdma_run/stop/reset() routines above.
 *
 * The low order bits (0-7) are device dependent status bits. These can be set
 * and read by both hardware and software. The mask is the set of bits to 
 * modify; if mask is 0x03 and value is 0, the lowest order 2 bits will be
 * zeroed.
 */

uint16_t dbdma_get_chan_status(dbdma_channel_t *chan);

uint8_t dbdma_get_device_status(dbdma_channel_t *chan);
void dbdma_set_device_status(dbdma_channel_t *chan, uint8_t mask,
    uint8_t value);

/*
 * Each DBDMA command word has the current channel status register and the
 * number of residual bytes (requested - actually transferred) written to it
 * at time of command completion.
 */

uint16_t dbdma_get_cmd_status(dbdma_channel_t *chan, int slot);
uint16_t dbdma_get_residuals(dbdma_channel_t *chan, int slot);

void dbdma_clear_cmd_status(dbdma_channel_t *chan, int slot);

/*
 * The interrupt/branch/wait selector let you specify a set of values
 * of the device dependent status bits that will cause intterupt/branch/wait
 * conditions to be taken if the flags for these are set to one of the 
 * DBDMA_COND_* values.
 * 
 * The condition is considered true if (status & mask) == value.
 */

void dbdma_set_interrupt_selector(dbdma_channel_t *chan, uint8_t mask,
    uint8_t value);
void dbdma_set_branch_selector(dbdma_channel_t *chan, uint8_t mask,
    uint8_t value);
void dbdma_set_wait_selector(dbdma_channel_t *chan, uint8_t mask,
    uint8_t value);

void dbdma_insert_command(dbdma_channel_t *chan, int slot, int command,
    int stream, bus_addr_t data, size_t count, uint8_t interrupt,
    uint8_t branch, uint8_t wait, uint32_t branch_slot); 

void dbdma_insert_stop(dbdma_channel_t *chan, int slot);
void dbdma_insert_nop(dbdma_channel_t *chan, int slot);
void dbdma_insert_branch(dbdma_channel_t *chan, int slot, int to_slot);

void dbdma_sync_commands(dbdma_channel_t *chan, bus_dmasync_op_t op);

void dbdma_save_state(dbdma_channel_t *chan);
void dbdma_restore_state(dbdma_channel_t *chan);

#endif /* _MACHINE_DBDMA_H_ */
