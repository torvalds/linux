/*	$OpenBSD: smc93cx6var.h,v 1.17 2015/09/12 16:24:14 deraadt Exp $	*/
/* $FreeBSD: sys/dev/aic7xxx/93cx6.h,v 1.3 1999/12/29 04:35:33 peter Exp $ */
/*
 * Interface to the 93C46 serial EEPROM that is used to store BIOS
 * settings for the aic7xxx based adaptec SCSI controllers.  It can
 * also be used for 93C26 and 93C06 serial EEPROMS.
 *
 * Copyright (c) 1994, 1995 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
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
#ifndef _SMC93CX6VAR_H_
#define _SMC93CX6VAR_H_

#ifdef _KERNEL

typedef enum {
	C46 = 6,
	C56_66 = 8
} seeprom_chip_t;

struct seeprom_descriptor {
	bus_space_tag_t sd_tag;
	bus_space_handle_t sd_bsh;
	bus_size_t sd_regsize;
	bus_size_t sd_control_offset;
	bus_size_t sd_status_offset;
	bus_size_t sd_dataout_offset;
	seeprom_chip_t sd_chip;
	u_int16_t sd_MS;
	u_int16_t sd_RDY;
	u_int16_t sd_CS;
	u_int16_t sd_CK;
	u_int16_t sd_DO;
	u_int16_t sd_DI;
};

/*
 * This function will read count 16-bit words from the serial EEPROM and
 * return their value in buf.  The port address of the aic7xxx serial EEPROM
 * control register is passed in as offset.  The following parameters are
 * also passed in:
 *
 *   CS  - Chip select
 *   CK  - Clock
 *   DO  - Data out
 *   DI  - Data in
 *   RDY - SEEPROM ready
 *   MS  - Memory port mode select
 *
 *  A failed read attempt returns 0, and a successful read returns 1.
 */

#define	SEEPROM_INB(sd) \
	(((sd)->sd_regsize == 4) \
	    ? bus_space_read_4((sd)->sd_tag, (sd)->sd_bsh, \
	          (sd)->sd_control_offset) \
	    : bus_space_read_1((sd)->sd_tag, (sd)->sd_bsh, \
	          (sd)->sd_control_offset))

#define	SEEPROM_OUTB(sd, value) \
do { \
	if ((sd)->sd_regsize == 4) \
		bus_space_write_4((sd)->sd_tag, (sd)->sd_bsh, \
		    (sd)->sd_control_offset, (value)); \
	else \
		bus_space_write_1((sd)->sd_tag, (sd)->sd_bsh, \
		    (sd)->sd_control_offset, (u_int8_t) (value)); \
} while(0)

#define	SEEPROM_STATUS_INB(sd) \
	(((sd)->sd_regsize == 4) \
	    ? bus_space_read_4((sd)->sd_tag, (sd)->sd_bsh, \
	          (sd)->sd_status_offset) \
	    : bus_space_read_1((sd)->sd_tag, (sd)->sd_bsh, \
	          (sd)->sd_status_offset))

#define	SEEPROM_DATA_INB(sd) \
	(((sd)->sd_regsize == 4) \
	    ? bus_space_read_4((sd)->sd_tag, (sd)->sd_bsh, \
	          (sd)->sd_dataout_offset) \
	    : bus_space_read_1((sd)->sd_tag, (sd)->sd_bsh, \
	          (sd)->sd_dataout_offset))

int	read_seeprom(struct seeprom_descriptor *, u_int16_t *,
	     bus_size_t, bus_size_t);

#endif /* _KERNEL */
#endif /* _SMC93CX6VAR_H_ */
