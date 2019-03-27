/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Generic defines for LSI '909 FC  adapters.
 * FreeBSD Version.
 *
 * Copyright (c)  2000, 2001 by Greg Ansley
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2002, 2006 by Matthew Jacob
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Support from Chris Ellsworth in order to make SAS adapters work
 * is gratefully acknowledged.
 *
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */
#ifndef _MPT_REG_H_
#define	_MPT_REG_H_

#define	MPT_OFFSET_DOORBELL	0x00
#define	MPT_OFFSET_SEQUENCE	0x04
#define	MPT_OFFSET_DIAGNOSTIC	0x08
#define	MPT_OFFSET_TEST		0x0C
#define	MPT_OFFSET_DIAG_DATA	0x10
#define	MPT_OFFSET_DIAG_ADDR	0x14
#define	MPT_OFFSET_INTR_STATUS	0x30
#define	MPT_OFFSET_INTR_MASK	0x34
#define	MPT_OFFSET_REQUEST_Q	0x40
#define	MPT_OFFSET_REPLY_Q	0x44
#define	MPT_OFFSET_HOST_INDEX	0x50
#define	MPT_OFFSET_FUBAR	0x90
#define	MPT_OFFSET_RESET_1078	0x10fc

/* Bit Maps for DOORBELL register */
enum DB_STATE_BITS {
	MPT_DB_STATE_RESET	= 0x00000000,
	MPT_DB_STATE_READY	= 0x10000000,
	MPT_DB_STATE_RUNNING	= 0x20000000,
	MPT_DB_STATE_FAULT	= 0x40000000,
	MPT_DB_STATE_MASK	= 0xf0000000
};

#define	MPT_STATE(v) ((enum DB_STATE_BITS)((v) & MPT_DB_STATE_MASK))

#define	MPT_DB_LENGTH_SHIFT	(16)
#define	MPT_DB_DATA_MASK	(0xffff)

#define	MPT_DB_DB_USED		0x08000000
#define	MPT_DB_IS_IN_USE(v) (((v) & MPT_DB_DB_USED) != 0)

/*
 * "Whom" initializor values
 */
#define	MPT_DB_INIT_NOONE	0x00
#define	MPT_DB_INIT_BIOS	0x01
#define	MPT_DB_INIT_ROMBIOS	0x02
#define	MPT_DB_INIT_PCIPEER	0x03
#define	MPT_DB_INIT_HOST	0x04
#define	MPT_DB_INIT_MANUFACTURE	0x05

#define	MPT_WHO(v)	\
	((v & MPI_DOORBELL_WHO_INIT_MASK) >> MPI_DOORBELL_WHO_INIT_SHIFT)

/* Function Maps for DOORBELL register */
enum DB_FUNCTION_BITS {
	MPT_FUNC_IOC_RESET	= 0x40000000,
	MPT_FUNC_UNIT_RESET	= 0x41000000,
	MPT_FUNC_HANDSHAKE	= 0x42000000,
	MPT_FUNC_REPLY_REMOVE	= 0x43000000,
	MPT_FUNC_MASK		= 0xff000000
};

/* Function Maps for INTERRUPT request register */
enum _MPT_INTR_REQ_BITS {
	MPT_INTR_DB_BUSY	= 0x80000000,
	MPT_INTR_REPLY_READY	= 0x00000008,
	MPT_INTR_DB_READY	= 0x00000001
};

#define	MPT_DB_IS_BUSY(v) (((v) & MPT_INTR_DB_BUSY) != 0)
#define	MPT_DB_INTR(v)    (((v) & MPT_INTR_DB_READY) != 0)
#define	MPT_REPLY_INTR(v) (((v) & MPT_INTR_REPLY_READY) != 0)

/* Function Maps for INTERRUPT mask register */
enum _MPT_INTR_MASK_BITS {
	MPT_INTR_REPLY_MASK	= 0x00000008,
	MPT_INTR_DB_MASK	= 0x00000001
};

/* Magic addresses in diagnostic memory space */
#define	MPT_DIAG_IOP_BASE		(0x00000000)
#define		MPT_DIAG_IOP_SIZE	(0x00002000)
#define	MPT_DIAG_GPIO			(0x00030010)
#define	MPT_DIAG_IOPQ_REG_BASE0		(0x00050004)
#define	MPT_DIAG_IOPQ_REG_BASE1		(0x00051004)
#define	MPT_DIAG_CTX0_BASE		(0x000E0000)
#define		MPT_DIAG_CTX0_SIZE	(0x00002000)
#define	MPT_DIAG_CTX1_BASE		(0x001E0000)
#define		MPT_DIAG_CTX1_SIZE	(0x00002000)
#define	MPT_DIAG_FLASH_BASE		(0x00800000)
#define	MPT_DIAG_RAM_BASE		(0x01000000)
#define		MPT_DIAG_RAM_SIZE	(0x00400000)
#define	MPT_DIAG_MEM_CFG_BASE		(0x3F000000)
#define		MPT_DIAG_MEM_CFG_BADFL	(0x04000000)

/* GPIO bit assignments */
#define	MPT_DIAG_GPIO_SCL	(0x00010000)
#define	MPT_DIAG_GPIO_SDA_OUT	(0x00008000)
#define	MPT_DIAG_GPIO_SDA_IN	(0x00004000)

#define	MPT_REPLY_EMPTY (0xFFFFFFFF)	/* Reply Queue Empty Symbol */
#endif /* _MPT_REG_H_ */
