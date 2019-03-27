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

#ifndef _POWERPC_POWERMAC_DBDMAVAR_H_
#define _POWERPC_POWERMAC_DBDMAVAR_H_

struct dbdma_command {
	uint8_t cmd:4; /* DBDMA command */

	uint8_t _resd1:1;
	uint8_t key:3; /* Stream number, or 6 for KEY_SYSTEM */
	uint8_t _resd2:2;

	/* Interrupt, branch, and wait flags */
	uint8_t intr:2;
	uint8_t branch:2;
	uint8_t wait:2;

	uint16_t reqCount; /* Bytes to transfer */

	uint32_t address; /* 32-bit system physical address */
	uint32_t cmdDep; /* Branch address or quad word to load/store */

	uint16_t xferStatus; /* Contents of channel status after completion */
	uint16_t resCount; /* Number of residual bytes outstanding */
};

struct dbdma_channel {
	struct resource 	*sc_regs;
	u_int			sc_off;

	struct dbdma_command	*sc_slots;
	int			sc_nslots;
	bus_addr_t		sc_slots_pa;

	bus_dma_tag_t		sc_dmatag;
	bus_dmamap_t		sc_dmamap;
	uint32_t		sc_saved_regs[5];
};
	

/*
   DBDMA registers are found at 0x8000 + n*0x100 in the macio register space,
   and are laid out as follows within each block: 
	
   Address:			Description:		Length (bytes):
   0x000 			Channel Control 	4
   0x004 			Channel Status		4
   0x00C			Command Phys Addr	4
   0x010			Interrupt Select	4
   0x014			Branch Select		4
   0x018			Wait Select		4
*/

#define CHAN_CONTROL_REG	0x00
#define	CHAN_STATUS_REG		0x04
#define CHAN_CMDPTR_HI		0x08
#define CHAN_CMDPTR		0x0C
#define	CHAN_INTR_SELECT	0x10
#define CHAN_BRANCH_SELECT	0x14
#define CHAN_WAIT_SELECT	0x18

/* Channel control is the write channel to channel status, the upper 16 bits
   are a mask of which bytes to change */

#define	DBDMA_REG_MASK_SHIFT	16

/* Status bits 0-7 are device dependent status bits */

/*
   The Interrupt/Branch/Wait Select triggers the corresponding condition bits
   in the event that (select.mask & device dependent status) == select.value

   They are defined a follows:
	Byte 1: Reserved
	Byte 2: Mask
	Byte 3: Reserved
	Byte 4: Value
*/

#endif /* _POWERPC_POWERMAC_DBDMAVAR_H_ */
