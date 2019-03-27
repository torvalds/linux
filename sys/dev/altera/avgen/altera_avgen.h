/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2016 Robert N. M. Watson
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

#ifndef _DEV_ALTERA_AVALON_H_
#define	_DEV_ALTERA_AVALON_H_

struct altera_avgen_softc {
	/*
	 * Bus-related fields.
	 */
	device_t	 avg_dev;
	int		 avg_unit;
	char		*avg_name;

	/*
	 * The device node and memory-mapped I/O region.
	 */
	struct cdev	*avg_cdev;
	struct resource	*avg_res;
	int		 avg_rid;

	/*
	 * Access properties configured by device.hints.
	 */
	u_int		 avg_flags;
	u_int		 avg_width;
	u_int		 avg_sectorsize;

	/*
	 * disk(9) state, if required for this device.
	 */
	struct disk	*avg_disk;
	struct mtx	 avg_disk_mtx;
};

/*
 * Various flags extracted from device.hints to configure operations on the
 * device.
 */
#define	ALTERA_AVALON_FLAG_READ			0x01
#define	ALTERA_AVALON_FLAG_WRITE		0x02
#define	ALTERA_AVALON_FLAG_MMAP_READ		0x04
#define	ALTERA_AVALON_FLAG_MMAP_WRITE		0x08
#define	ALTERA_AVALON_FLAG_MMAP_EXEC		0x10
#define	ALTERA_AVALON_FLAG_GEOM_READ		0x20
#define	ALTERA_AVALON_FLAG_GEOM_WRITE		0x40

#define	ALTERA_AVALON_CHAR_READ			'r'
#define	ALTERA_AVALON_CHAR_WRITE		'w'
#define	ALTERA_AVALON_CHAR_EXEC			'x'

#define	ALTERA_AVALON_STR_WIDTH			"width"
#define	ALTERA_AVALON_STR_FILEIO		"fileio"
#define	ALTERA_AVALON_STR_GEOMIO		"geomio"
#define	ALTERA_AVALON_STR_MMAPIO		"mmapio"
#define ALTERA_AVALON_STR_DEVNAME		"devname"
#define	ALTERA_AVALON_STR_DEVUNIT		"devunit"

/*
 * Driver setup routines from the bus attachment/teardown.
 */
int	altera_avgen_attach(struct altera_avgen_softc *sc,
	    const char *str_fileio, const char *str_geomio,
	    const char *str_mmapio, const char *str_devname, int devunit);
void	altera_avgen_detach(struct altera_avgen_softc *sc);

extern devclass_t	altera_avgen_devclass;

#endif /* _DEV_ALTERA_AVALON_H_ */
