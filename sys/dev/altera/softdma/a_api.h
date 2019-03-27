/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-11-C-0249)
 * ("MRC2"), as part of the DARPA MRC research programme.
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
/*
 * Altera, Embedded Peripherals IP, User Guide, v. 11.0, June 2011.
 * UG-01085-11.0.
 */

#ifndef _A_API_H
#define _A_API_H

/* Table 16-1. Memory Map. */
#define	A_ONCHIP_FIFO_MEM_CORE_DATA		0x00
#define	A_ONCHIP_FIFO_MEM_CORE_METADATA		0x04

#define	A_ONCHIP_FIFO_MEM_CORE_SOP		(1<<0)
#define	A_ONCHIP_FIFO_MEM_CORE_EOP		(1<<1)
#define	A_ONCHIP_FIFO_MEM_CORE_EMPTY_MASK	0x000000f7
#define	A_ONCHIP_FIFO_MEM_CORE_EMPTY_SHIFT	2
	/* Reserved				(1<<7)	   */
#define	A_ONCHIP_FIFO_MEM_CORE_CHANNEL_MASK	0x0000ff00
#define	A_ONCHIP_FIFO_MEM_CORE_CHANNEL_SHIFT	8
#define	A_ONCHIP_FIFO_MEM_CORE_ERROR_MASK	0x00ff0000
#define	A_ONCHIP_FIFO_MEM_CORE_ERROR_SHIFT	16
	/* Reserved				0xff000000 */

/* Table 16-3. FIFO Status Register Memory Map. */
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_REG_FILL_LEVEL	0x00
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_REG_I_STATUS	0x04
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_REG_EVENT		0x08
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_REG_INT_ENABLE	0x0c
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_REG_ALMOSTFULL	0x10
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_REG_ALMOSTEMPTY	0x14

/* Table 16-5. Status Bit Field Descriptions. */
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_FULL		(1<<0)
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_EMPTY		(1<<1)
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_ALMOSTFULL	(1<<2)
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_ALMOSTEMPTY	(1<<3)
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_OVERFLOW		(1<<4)
#define	A_ONCHIP_FIFO_MEM_CORE_STATUS_UNDERFLOW		(1<<5)

/* Table 16-6. Event Bit Field Descriptions. */
/* XXX Datasheet has incorrect bit fields. Validate. */
#define	A_ONCHIP_FIFO_MEM_CORE_EVENT_FULL		(1<<0)
#define	A_ONCHIP_FIFO_MEM_CORE_EVENT_EMPTY		(1<<1)
#define	A_ONCHIP_FIFO_MEM_CORE_EVENT_ALMOSTFULL		(1<<2)
#define	A_ONCHIP_FIFO_MEM_CORE_EVENT_ALMOSTEMPTY	(1<<3)
#define	A_ONCHIP_FIFO_MEM_CORE_EVENT_OVERFLOW		(1<<4)
#define	A_ONCHIP_FIFO_MEM_CORE_EVENT_UNDERFLOW		(1<<5)

/* Table 16-7. InterruptEnable Bit Field Descriptions. */
/* XXX Datasheet has incorrect bit fields. Validate. */
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_FULL		(1<<0)
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_EMPTY		(1<<1)
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_ALMOSTFULL		(1<<2)
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_ALMOSTEMPTY		(1<<3)
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_OVERFLOW		(1<<4)
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_UNDERFLOW		(1<<5)
#define	A_ONCHIP_FIFO_MEM_CORE_INTR_ALL			\
	    (A_ONCHIP_FIFO_MEM_CORE_INTR_EMPTY|		\
	    A_ONCHIP_FIFO_MEM_CORE_INTR_FULL|		\
	    A_ONCHIP_FIFO_MEM_CORE_INTR_ALMOSTEMPTY|	\
	    A_ONCHIP_FIFO_MEM_CORE_INTR_ALMOSTFULL|	\
	    A_ONCHIP_FIFO_MEM_CORE_INTR_OVERFLOW|	\
	    A_ONCHIP_FIFO_MEM_CORE_INTR_UNDERFLOW)

#endif /* _A_API_H */

/* end */
