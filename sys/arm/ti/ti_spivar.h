/*-
 * Copyright (c) 2016 Rubicon Communications, LLC (Netgate)
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

#ifndef	_TI_SPIVAR_H_
#define	_TI_SPIVAR_H_

struct ti_spi_softc {
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	device_t		sc_dev;
	int			sc_numcs;
	struct mtx		sc_mtx;
	struct resource		*sc_mem_res;
	struct resource		*sc_irq_res;
	struct {
		int		cs;
		int		fifolvl;
		struct spi_command	*cmd;
		uint32_t	len;
		uint32_t	read;
		uint32_t	written;
	} xfer;
	uint32_t		sc_flags;
	void			*sc_intrhand;
#define	sc_cs			xfer.cs
#define	sc_fifolvl		xfer.fifolvl
#define	sc_cmd			xfer.cmd
#define	sc_len			xfer.len
#define	sc_read			xfer.read
#define	sc_written		xfer.written
};

#define	TI_SPI_BUSY		0x1
#define	TI_SPI_DONE		0x2

#define TI_SPI_WRITE(_sc, _off, _val)		\
    bus_space_write_4((_sc)->sc_bst, (_sc)->sc_bsh, (_off), (_val))
#define TI_SPI_READ(_sc, _off)			\
    bus_space_read_4((_sc)->sc_bst, (_sc)->sc_bsh, (_off))

#define TI_SPI_LOCK(_sc)			\
    mtx_lock(&(_sc)->sc_mtx)
#define TI_SPI_UNLOCK(_sc)			\
    mtx_unlock(&(_sc)->sc_mtx)

#endif	/* _TI_SPIVAR_H_ */
