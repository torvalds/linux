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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <geom/geom_disk.h>

#include <dev/altera/sdcard/altera_sdcard.h>

int altera_sdcard_ignore_crc_errors = 1;
int altera_sdcard_verify_rxtx_writes = 1;

/*
 * Low-level I/O routines for the Altera SD Card University IP Core driver.
 *
 * XXXRW: Throughout, it is assumed that the IP Core handles multibyte
 * registers as little endian, as is the case for other Altera IP cores.
 * However, the specification makes no reference to endianness, so this
 * assumption might not always be correct.
 */
uint16_t
altera_sdcard_read_asr(struct altera_sdcard_softc *sc)
{

	return (le16toh(bus_read_2(sc->as_res, ALTERA_SDCARD_OFF_ASR)));
}

static int
altera_sdcard_process_csd0(struct altera_sdcard_softc *sc)
{
	uint64_t c_size, c_size_mult, read_bl_len;
	uint8_t byte0, byte1, byte2;

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	/*-
	 * Compute card capacity per SD Card interface description as follows:
	 *
	 *   Memory capacity = BLOCKNR * BLOCK_LEN
	 *
	 * Where:
	 *
	 *   BLOCKNR = (C_SIZE + 1) * MULT
	 *   MULT = 2^(C_SIZE_MULT+2)
	 *   BLOCK_LEN = 2^READ_BL_LEN
	 */
	read_bl_len = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_READ_BL_LEN_BYTE];
	read_bl_len &= ALTERA_SDCARD_CSD_READ_BL_LEN_MASK;

	byte0 = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_C_SIZE_BYTE0];
	byte0 &= ALTERA_SDCARD_CSD_C_SIZE_MASK0;
	byte1 = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_C_SIZE_BYTE1];
	byte2 = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_C_SIZE_BYTE2];
	byte2 &= ALTERA_SDCARD_CSD_C_SIZE_MASK2;
	c_size = (byte0 >> ALTERA_SDCARD_CSD_C_SIZE_RSHIFT0) |
	    (byte1 << ALTERA_SDCARD_CSD_C_SIZE_LSHIFT1) |
	    (byte2 << ALTERA_SDCARD_CSD_C_SIZE_LSHIFT2);

	byte0 = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_C_SIZE_MULT_BYTE0];
	byte0 &= ALTERA_SDCARD_CSD_C_SIZE_MULT_MASK0;
	byte1 = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_C_SIZE_MULT_BYTE1];
	byte1 &= ALTERA_SDCARD_CSD_C_SIZE_MULT_MASK1;
	c_size_mult = (byte0 >> ALTERA_SDCARD_CSD_C_SIZE_MULT_RSHIFT0) |
	    (byte1 << ALTERA_SDCARD_CSD_C_SIZE_MULT_LSHIFT1);

	/*
	 * If we're just getting back zero's, mark the card as bad, even
	 * though it could just mean a Very Small Disk Indeed.
	 */
	if (c_size == 0 && c_size_mult == 0 && read_bl_len == 0) {
		device_printf(sc->as_dev, "Ignored zero-size card\n");
		return (ENXIO);
	}
	sc->as_mediasize = (c_size + 1) * (1 << (c_size_mult + 2)) *
	    (1 << read_bl_len);
	return (0);
}

int
altera_sdcard_read_csd(struct altera_sdcard_softc *sc)
{
	uint8_t csd_structure;
	int error;

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	/*
	 * XXXRW: Assume for now that when the SD Card IP Core negotiates
	 * voltage/speed/etc, it must use the CSD register, and therefore
	 * populates the SD Card IP Core's cache of the register value.  This
	 * means that we can read it without issuing further SD Card commands.
	 * If this assumption proves false, we will (a) get back garbage and
	 * (b) need to add additional states in the driver state machine in
	 * order to query card properties before I/O can start.
	 *
	 * XXXRW: Treating this as an array of bytes, so no byte swapping --
	 * is that a safe assumption?
	 */
	KASSERT(((uintptr_t)&sc->as_csd.csd_data) % 2 == 0,
	    ("%s: CSD buffer unaligned", __func__));
	bus_read_region_2(sc->as_res, ALTERA_SDCARD_OFF_CSD,
	    (uint16_t *)sc->as_csd.csd_data, sizeof(sc->as_csd) / 2);

	/*
	 * Interpret the loaded CSD, extracting certain fields and copying
	 * them into the softc for easy software access.
	 *
	 * Currently, we support only CSD Version 1.0.  If we detect a newer
	 * version, suppress card detection.
	 */
	csd_structure = sc->as_csd.csd_data[ALTERA_SDCARD_CSD_STRUCTURE_BYTE];
	csd_structure &= ALTERA_SDCARD_CSD_STRUCTURE_MASK;
	csd_structure >>= ALTERA_SDCARD_CSD_STRUCTURE_RSHIFT;
	sc->as_csd_structure = csd_structure;

	/*
	 * Interpret the CSD field based on its version.  Extract fields,
	 * especially mediasize.
	 *
	 * XXXRW: Desirable to support further CSD versions here.
	 */
	switch (sc->as_csd_structure) {
	case 0:
		error = altera_sdcard_process_csd0(sc);
		if (error)
			return (error);
		break;

	default:
		device_printf(sc->as_dev,
		    "Ignored disk with unsupported CSD structure (%d)\n",
		    sc->as_csd_structure);
		return (ENXIO);
	}
	return (0);
}

/*
 * XXXRW: The Altera IP Core specification indicates that RR1 is a 16-bit
 * register, but all bits it identifies are >16 bit.  Most likely, RR1 is a
 * 32-bit register?
 */
static uint16_t
altera_sdcard_read_rr1(struct altera_sdcard_softc *sc)
{

	return (le16toh(bus_read_2(sc->as_res, ALTERA_SDCARD_OFF_RR1)));
}

static void
altera_sdcard_write_cmd_arg(struct altera_sdcard_softc *sc, uint32_t cmd_arg)
{

	bus_write_4(sc->as_res, ALTERA_SDCARD_OFF_CMD_ARG, htole32(cmd_arg));
}

static void
altera_sdcard_write_cmd(struct altera_sdcard_softc *sc, uint16_t cmd)
{

	bus_write_2(sc->as_res, ALTERA_SDCARD_OFF_CMD, htole16(cmd));
}

static void
altera_sdcard_read_rxtx_buffer(struct altera_sdcard_softc *sc, void *data,
    size_t len)
{

	KASSERT((uintptr_t)data % 2 == 0,
	    ("%s: unaligned data %p", __func__, data));
	KASSERT((len <= ALTERA_SDCARD_SECTORSIZE) && (len % 2 == 0),
	    ("%s: invalid length %ju", __func__, len));

	bus_read_region_2(sc->as_res, ALTERA_SDCARD_OFF_RXTX_BUFFER,
	    (uint16_t *)data, len / 2);
}

static void
altera_sdcard_write_rxtx_buffer(struct altera_sdcard_softc *sc, void *data,
    size_t len)
{
	u_int corrections, differences, i, retry_counter;
	uint16_t d, v;

	KASSERT((uintptr_t)data % 2 == 0,
	    ("%s: unaligned data %p", __func__, data));
	KASSERT((len <= ALTERA_SDCARD_SECTORSIZE) && (len % 2 == 0),
	    ("%s: invalid length %ju", __func__, len));

	retry_counter = 0;
	do {
		bus_write_region_2(sc->as_res, ALTERA_SDCARD_OFF_RXTX_BUFFER,
		    (uint16_t *)data, len / 2);

		/*
		 * XXXRW: Due to a possible hardware bug, the above call to
		 * bus_write_region_2() might not succeed.  If the workaround
		 * is enabled, verify each write and retry until it succeeds.
		 *
		 * XXXRW: Do we want a limit counter for retries here?
		 */
recheck:
		corrections = 0;
		differences = 0;
		if (altera_sdcard_verify_rxtx_writes) {
			for (i = 0; i < ALTERA_SDCARD_SECTORSIZE; i += 2) {
				v = bus_read_2(sc->as_res,
				    ALTERA_SDCARD_OFF_RXTX_BUFFER + i);
				d = *(uint16_t *)((uint8_t *)data + i);
				if (v != d) {
					if (retry_counter == 0) {
						bus_write_2(sc->as_res,
						    ALTERA_SDCARD_OFF_RXTX_BUFFER + i,
						    d);
						v = bus_read_2(sc->as_res,
						    ALTERA_SDCARD_OFF_RXTX_BUFFER + i);
						if (v == d) {
							corrections++;
							device_printf(sc->as_dev,
							    "%s: single word rewrite worked"
							    " at offset %u\n", 
							    __func__, i);
							continue;
						}
					}
					differences++;
					device_printf(sc->as_dev,
					    "%s: retrying write -- difference"
					    " %u at offset %u, retry %u\n",
					    __func__, differences, i,
					    retry_counter);
				}
			}
			if (differences != 0) {
				retry_counter++;
				if (retry_counter == 1 &&
				    corrections == differences)
					goto recheck;
			}
		}
	} while (differences != 0);
	if (retry_counter)
		device_printf(sc->as_dev, "%s: succeeded after %u retries\n",
		    __func__, retry_counter);
}

static void
altera_sdcard_io_start_internal(struct altera_sdcard_softc *sc, struct bio *bp)
{

	switch (bp->bio_cmd) {
	case BIO_READ:
		altera_sdcard_write_cmd_arg(sc, bp->bio_pblkno *
		    ALTERA_SDCARD_SECTORSIZE);
		altera_sdcard_write_cmd(sc, ALTERA_SDCARD_CMD_READ_BLOCK);
		break;

	case BIO_WRITE:
		altera_sdcard_write_rxtx_buffer(sc, bp->bio_data,
		    bp->bio_bcount);
		altera_sdcard_write_cmd_arg(sc, bp->bio_pblkno *
		    ALTERA_SDCARD_SECTORSIZE);
		altera_sdcard_write_cmd(sc, ALTERA_SDCARD_CMD_WRITE_BLOCK);
		break;

	default:
		panic("%s: unsupported I/O operation %d", __func__,
		    bp->bio_cmd);
	}
}

void
altera_sdcard_io_start(struct altera_sdcard_softc *sc, struct bio *bp)
{

	ALTERA_SDCARD_LOCK_ASSERT(sc);
	KASSERT(sc->as_currentbio == NULL,
	    ("%s: bio already started", __func__));

	/*
	 * We advertise a block size and maximum I/O size up the stack of the
	 * SD Card IP Core sector size.  Catch any attempts to not follow the
	 * rules.
	 */
	KASSERT(bp->bio_bcount == ALTERA_SDCARD_SECTORSIZE,
	    ("%s: I/O size not %d", __func__, ALTERA_SDCARD_SECTORSIZE));
	altera_sdcard_io_start_internal(sc, bp);
	sc->as_currentbio = bp;
	sc->as_retriesleft = ALTERA_SDCARD_RETRY_LIMIT;
}

/*
 * Handle completed I/O.  ASR is passed in to avoid reading it more than once.
 * Return 1 if the I/O is actually complete (success, or retry limit
 * exceeded), or 0 if not.
 */
int
altera_sdcard_io_complete(struct altera_sdcard_softc *sc, uint16_t asr)
{
	struct bio *bp;
	uint16_t rr1, mask;
	int error;

	ALTERA_SDCARD_LOCK_ASSERT(sc);
	KASSERT(!(asr & ALTERA_SDCARD_ASR_CMDINPROGRESS),
	    ("%s: still in progress", __func__));
	KASSERT(asr & ALTERA_SDCARD_ASR_CARDPRESENT,
	    ("%s: card removed", __func__));

	bp = sc->as_currentbio;

	/*-
	 * Handle I/O retries if an error is returned by the device.  Various
	 * quirks handled in the process:
	 *
	 * 1. ALTERA_SDCARD_ASR_CMDDATAERROR is ignored for BIO_WRITE.
	 * 2. ALTERA_SDCARD_RR1_COMMANDCRCFAILED is optionally ignored for
	 *    BIO_READ.
	 */
	error = 0;
	rr1 = altera_sdcard_read_rr1(sc);
	switch (bp->bio_cmd) {
	case BIO_READ:
		mask = ALTERA_SDCARD_RR1_ERRORMASK;
		if (altera_sdcard_ignore_crc_errors)
			mask &= ~ALTERA_SDCARD_RR1_COMMANDCRCFAILED;
		if (asr & ALTERA_SDCARD_ASR_CMDTIMEOUT)
			error = EIO;
		else if ((asr & ALTERA_SDCARD_ASR_CMDDATAERROR) &&
		    (rr1 & mask))
			error = EIO;
		else
			error = 0;
		break;

	case BIO_WRITE:
		if (asr & ALTERA_SDCARD_ASR_CMDTIMEOUT)
			error = EIO;
		else
			error = 0;
		break;

	default:
		break;
	}
	if (error) {
		sc->as_retriesleft--;
		if (sc->as_retriesleft == 0 || bootverbose)
			device_printf(sc->as_dev, "%s: %s operation block %ju "
			    "length %ju failed; asr 0x%08x (rr1: 0x%04x)%s\n",
			    __func__, bp->bio_cmd == BIO_READ ? "BIO_READ" :
			    (bp->bio_cmd == BIO_WRITE ? "BIO_WRITE" :
			    "unknown"),
			    bp->bio_pblkno, bp->bio_bcount, asr, rr1,
			    sc->as_retriesleft != 0 ? " retrying" : "");
		/*
		 * This attempt experienced an error; possibly retry.
		 */
		if (sc->as_retriesleft != 0) {
			sc->as_flags |= ALTERA_SDCARD_FLAG_IOERROR;
			altera_sdcard_io_start_internal(sc, bp);
			return (0);
		}
		sc->as_flags &= ~ALTERA_SDCARD_FLAG_IOERROR;
	} else {
		/*
		 * Successful I/O completion path.
		 */
		if (sc->as_flags & ALTERA_SDCARD_FLAG_IOERROR) {
			device_printf(sc->as_dev, "%s: %s operation block %ju"
			    " length %ju succeeded after %d retries\n",
			    __func__, bp->bio_cmd == BIO_READ ? "BIO_READ" :
			    (bp->bio_cmd == BIO_WRITE ? "write" : "unknown"),
			    bp->bio_pblkno, bp->bio_bcount,
			    ALTERA_SDCARD_RETRY_LIMIT - sc->as_retriesleft);
			sc->as_flags &= ~ALTERA_SDCARD_FLAG_IOERROR;
		}
		switch (bp->bio_cmd) {
		case BIO_READ:
			altera_sdcard_read_rxtx_buffer(sc, bp->bio_data,
			    bp->bio_bcount);
			break;

		case BIO_WRITE:
			break;

		default:
			panic("%s: unsupported I/O operation %d", __func__,
			    bp->bio_cmd);
		}
		bp->bio_resid = 0;
		error = 0;
	}
	biofinish(bp, NULL, error);
	sc->as_currentbio = NULL;
	return (1);
}
