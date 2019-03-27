/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * $FreeBSD$
 */

#ifndef _BHND_CORES_CHIPC_CHIPC_SPI_H_
#define	_BHND_CORES_CHIPC_CHIPC_SPI_H_

#define	CHIPC_SPI_MAXTRIES	1000

#define	CHIPC_SPI_ACTION_INPUT	1
#define	CHIPC_SPI_ACTION_OUTPUT	2

#define	CHIPC_SPI_FLASHCTL			0x00
#define		CHIPC_SPI_FLASHCTL_OPCODE	0x000000ff
#define		CHIPC_SPI_FLASHCTL_ACTION	0x00000700 //
/*
 * We don't use action at all. Experimentaly found, that
 *  action 0 - read current MISO byte to data register (interactive mode)
 *  action 1 = read 2nd byte to data register
 *  action 2 = read 4th byte to data register (surprise! see action 6)
 *  action 3 = read 5th byte to data register
 *  action 4 = read bytes 5-8 to data register in swapped order
 *  action 5 = read bytes 9-12 to data register in swapped order
 *  action 6 = read 3rd byte to data register
 *  action 7 = read bytes 6-9 to data register in swapped order
 * It may be wrong if CS bit is 1.
 * If CS bit is 1, you should write cmd / data to opcode byte-to-byte.
 */
#define		CHIPC_SPI_FLASHCTL_CSACTIVE	0x00001000
#define		CHIPC_SPI_FLASHCTL_START	0x80000000 //same as BUSY
#define		CHIPC_SPI_FLASHCTL_BUSY		0x80000000 //same as BUSY
#define	CHIPC_SPI_FLASHADDR			0x04
#define	CHIPC_SPI_FLASHDATA			0x08

struct chipc_spi_softc {
	device_t		 sc_dev;
	struct resource		*sc_res;	/**< SPI registers */
	int			 sc_rid;

	struct resource		*sc_flash_res;	/**< flash shadow */
	int			 sc_flash_rid;
};

/* register space access macros */
#define	SPI_BARRIER_WRITE(sc)	bus_barrier((sc)->sc_res, 0, 0, 	\
				    BUS_SPACE_BARRIER_WRITE)
#define	SPI_BARRIER_READ(sc)	bus_barrier((sc)->sc_res, 0, 0, 	\
				    BUS_SPACE_BARRIER_READ)
#define	SPI_BARRIER_RW(sc)	bus_barrier((sc)->sc_res, 0, 0, 	\
			            BUS_SPACE_BARRIER_READ |		\
			            BUS_SPACE_BARRIER_WRITE)

#define SPI_WRITE(sc, reg, val)	bus_write_4(sc->sc_res, (reg), (val));

#define	SPI_READ(sc, reg)	bus_read_4(sc->sc_res, (reg))

#define	SPI_SET_BITS(sc, reg, bits)					\
	SPI_WRITE(sc, reg, SPI_READ(sc, (reg)) | (bits))

#define	SPI_CLEAR_BITS(sc, reg, bits)					\
	SPI_WRITE(sc, reg, SPI_READ(sc, (reg)) & ~(bits))

#endif /* _BHND_CORES_CHIPC_CHIPC_SPI_H_ */
