/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#ifndef _DEV_ALTERA_SDCARD_H_
#define	_DEV_ALTERA_SDCARD_H_

#define	ALTERA_SDCARD_CSD_SIZE	16
struct altera_sdcard_csd {
	uint8_t		 csd_data[ALTERA_SDCARD_CSD_SIZE];
} __aligned(2);		/* CSD is read in 16-bit chunks, so align to match. */

struct altera_sdcard_softc {
	device_t		 as_dev;
	int			 as_unit;
	struct resource		*as_res;
	int			 as_rid;
	struct mtx		 as_lock;
	struct cv		 as_condvar;
	int			 as_state;
	int			 as_flags;
	struct disk		*as_disk;
	struct taskqueue	*as_taskqueue;
	struct timeout_task	 as_task;

	/*
	 * Fields relating to in-progress and pending I/O, if any.
	 */
	struct bio_queue_head	 as_bioq;
	struct bio		*as_currentbio;
	u_int			 as_retriesleft;

	/*
	 * Infrequently changing fields cached from the SD Card IP Core.
	 */
	struct altera_sdcard_csd	 as_csd;
	uint8_t			 as_csd_structure;	/* CSD version. */
	uint64_t		 as_mediasize;
};

#define	ALTERA_SDCARD_LOCK(sc)		mtx_lock(&(sc)->as_lock)
#define	ALTERA_SDCARD_LOCK_ASSERT(sc)	mtx_assert(&(sc)->as_lock, MA_OWNED)
#define	ALTERA_SDCARD_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->as_lock)
#define	ALTERA_SDCARD_LOCK_INIT(sc)	mtx_init(&(sc)->as_lock,	\
					    "altera_sdcard", NULL, MTX_DEF)
#define	ALTERA_SDCARD_UNLOCK(sc)	mtx_unlock(&(sc)->as_lock)

#define	ALTERA_SDCARD_CONDVAR_DESTROY(sc)	cv_destroy(&(sc)->as_condvar)
#define	ALTERA_SDCARD_CONDVAR_INIT(sc)		cv_init(&(sc)->as_condvar, \
						    "altera_sdcard_detach_wait")
#define	ALTERA_SDCARD_CONDVAR_SIGNAL(dc)	cv_signal(&(sc)->as_condvar)
#define	ALTERA_SDCARD_CONDVAR_WAIT(sc)		cv_wait(&(sc)->as_condvar, \
						    &(sc)->as_lock)

/*
 * States an instance can be in at any given moment.
 */
#define	ALTERA_SDCARD_STATE_NOCARD	1	/* No card inserted. */
#define	ALTERA_SDCARD_STATE_BADCARD	2	/* Card bad/not supported. */
#define	ALTERA_SDCARD_STATE_IDLE	3	/* Card present but idle. */
#define	ALTERA_SDCARD_STATE_IO		4	/* Card in I/O currently. */
#define	ALTERA_SDCARD_STATE_DETACHED	5	/* Driver is detaching. */

/*
 * Different timeout intervals based on state.  When just looking for a card
 * status change, check twice a second.  When we're actively waiting on I/O
 * completion, check every millisecond.
 */
#define	ALTERA_SDCARD_TIMEOUT_NOCARD	(hz/2)
#define	ALTERA_SDCARD_TIMEOUT_IDLE	(hz/2)
#define	ALTERA_SDCARD_TIMEOUT_IO	(1)
#define	ALTERA_SDCARD_TIMEOUT_IOERROR	(hz/5)

/*
 * Maximum number of retries on an I/O.
 */
#define	ALTERA_SDCARD_RETRY_LIMIT	10

/*
 * Driver status flags.
 */
#define	ALTERA_SDCARD_FLAG_DETACHREQ	0x00000001	/* Detach requested. */
#define	ALTERA_SDCARD_FLAG_IOERROR	0x00000002	/* Error in progress. */

/*
 * Functions for performing low-level register and memory I/O to/from the SD
 * Card IP Core.  In general, only code in altera_sdcard_io.c is aware of the
 * hardware interface.
 */
uint16_t	altera_sdcard_read_asr(struct altera_sdcard_softc *sc);
int		altera_sdcard_read_csd(struct altera_sdcard_softc *sc);

int	altera_sdcard_io_complete(struct altera_sdcard_softc *sc,
	    uint16_t asr);
void	altera_sdcard_io_start(struct altera_sdcard_softc *sc,
	    struct bio *bp);

/*
 * Constants for interpreting the SD Card Card Specific Data (CSD) register.
 */
#define	ALTERA_SDCARD_CSD_STRUCTURE_BYTE	15
#define	ALTERA_SDCARD_CSD_STRUCTURE_MASK	0xc0	/* 2 bits */
#define	ALTERA_SDCARD_CSD_STRUCTURE_RSHIFT	6

#define	ALTERA_SDCARD_CSD_READ_BL_LEN_BYTE	10
#define	ALTERA_SDCARD_CSD_READ_BL_LEN_MASK	0x0f	/* 4 bits */

/*
 * C_SIZE is a 12-bit field helpfully split over three differe bytes of CSD
 * data.  Software ease of use was not a design consideration.
 */
#define	ALTERA_SDCARD_CSD_C_SIZE_BYTE0		7
#define	ALTERA_SDCARD_CSD_C_SIZE_MASK0		0xc0	/* top 2 bits */
#define	ALTERA_SDCARD_CSD_C_SIZE_RSHIFT0	6

#define	ALTERA_SDCARD_CSD_C_SIZE_BYTE1		8
#define	ALTERA_SDCARD_CSD_C_SIZE_MASK1		0xff	/* 8 bits */
#define	ALTERA_SDCARD_CSD_C_SIZE_LSHIFT1	2

#define	ALTERA_SDCARD_CSD_C_SIZE_BYTE2		9
#define	ALTERA_SDCARD_CSD_C_SIZE_MASK2		0x03	/* bottom 2 bits */
#define	ALTERA_SDCARD_CSD_C_SIZE_LSHIFT2	10

#define	ALTERA_SDCARD_CSD_C_SIZE_MULT_BYTE0	5
#define	ALTERA_SDCARD_CSD_C_SIZE_MULT_MASK0	0x80	/* top 1 bit */
#define	ALTERA_SDCARD_CSD_C_SIZE_MULT_RSHIFT0	7

#define	ALTERA_SDCARD_CSD_C_SIZE_MULT_BYTE1	6
#define	ALTERA_SDCARD_CSD_C_SIZE_MULT_MASK1	0x03	/* bottom 2 bits */
#define	ALTERA_SDCARD_CSD_C_SIZE_MULT_LSHIFT1	1

/*
 * I/O register/buffer offsets, from Table 4.1.1 in the Altera University
 * Program SD Card IP Core specification.
 */
#define	ALTERA_SDCARD_OFF_RXTX_BUFFER	0	/* 512-byte I/O buffer */
#define	ALTERA_SDCARD_OFF_CID		512	/* 16-byte Card ID number */
#define	ALTERA_SDCARD_OFF_CSD		528	/* 16-byte Card Specific Data */
#define	ALTERA_SDCARD_OFF_OCR		544	/* Operating Conditions Reg */
#define	ALTERA_SDCARD_OFF_SR		548	/* SD Card Status Register */
#define	ALTERA_SDCARD_OFF_RCA		552	/* Relative Card Address Reg */
#define	ALTERA_SDCARD_OFF_CMD_ARG	556	/* Command Argument Register */
#define	ALTERA_SDCARD_OFF_CMD		560	/* Command Register */
#define	ALTERA_SDCARD_OFF_ASR		564	/* Auxiliary Status Register */
#define	ALTERA_SDCARD_OFF_RR1		568	/* Response R1 */

/*
 * The Altera IP Core provides a 16-bit "Additional Status Register" (ASR)
 * beyond those described in the SD Card specification that captures IP Core
 * transaction state, such as whether the last command is in progress, the
 * card has been removed, etc.
 */
#define	ALTERA_SDCARD_ASR_CMDVALID	0x0001
#define	ALTERA_SDCARD_ASR_CARDPRESENT	0x0002
#define	ALTERA_SDCARD_ASR_CMDINPROGRESS	0x0004
#define	ALTERA_SDCARD_ASR_SRVALID	0x0008
#define	ALTERA_SDCARD_ASR_CMDTIMEOUT	0x0010
#define	ALTERA_SDCARD_ASR_CMDDATAERROR	0x0020

/*
 * The Altera IP Core claims to provide a 16-bit "Response R1" register (RR1)
 * to provide more detailed error reporting when a read or write fails.
 *
 * XXXRW: The specification claims that this field is 16-bit, but then
 * proceeds to define values as though it is 32-bit.  In practice, 16-bit
 * seems more likely as the register is not 32-bit aligned.
 */
#define	ALTERA_SDCARD_RR1_INITPROCRUNNING	0x0100
#define	ALTERA_SDCARD_RR1_ERASEINTERRUPTED	0x0200
#define	ALTERA_SDCARD_RR1_ILLEGALCOMMAND	0x0400
#define	ALTERA_SDCARD_RR1_COMMANDCRCFAILED	0x0800
#define	ALTERA_SDCARD_RR1_ADDRESSMISALIGNED	0x1000
#define	ALTERA_SDCARD_RR1_ADDRBLOCKRANGE	0x2000

/*
 * Not all RR1 values are "errors" per se -- check only for the ones that are
 * when performing error handling.
 */
#define	ALTERA_SDCARD_RR1_ERRORMASK					      \
    (ALTERA_SDCARD_RR1_ERASEINTERRUPTED | ALTERA_SDCARD_RR1_ILLEGALCOMMAND |  \
    ALTERA_SDCARD_RR1_COMMANDCRCFAILED | ALTERA_SDCARD_RR1_ADDRESSMISALIGNED |\
    ALTERA_SDCARD_RR1_ADDRBLOCKRANGE)

/*
 * Although SD Cards may have various sector sizes, the Altera IP Core
 * requires that I/O be done in 512-byte chunks.
 */
#define	ALTERA_SDCARD_SECTORSIZE	512

/*
 * SD Card commands used in this driver.
 */
#define	ALTERA_SDCARD_CMD_SEND_RCA	0x03	/* Retrieve card RCA. */
#define	ALTERA_SDCARD_CMD_SEND_CSD	0x09	/* Retrieve CSD register. */
#define	ALTERA_SDCARD_CMD_SEND_CID	0x0A	/* Retrieve CID register. */
#define	ALTERA_SDCARD_CMD_READ_BLOCK	0x11	/* Read block from disk. */
#define	ALTERA_SDCARD_CMD_WRITE_BLOCK	0x18	/* Write block to disk. */

/*
 * Functions exposed by the device driver core to newbus(9) bus attachment
 * implementations.
 */
void	altera_sdcard_attach(struct altera_sdcard_softc *sc);
void	altera_sdcard_detach(struct altera_sdcard_softc *sc);
void	altera_sdcard_task(void *arg, int pending);

/*
 * Functions exposed by the device driver core to the disk(9) front-end.
 */
void	altera_sdcard_start(struct altera_sdcard_softc *sc);

/*
 * Functions relating to the implementation of disk(9) KPIs for the SD Card
 * driver.
 */
void	altera_sdcard_disk_insert(struct altera_sdcard_softc *sc);
void	altera_sdcard_disk_remove(struct altera_sdcard_softc *sc);

extern devclass_t	altera_sdcard_devclass;

#endif	/* _DEV_ALTERA_SDCARD_H_ */
