/*	$OpenBSD: smc93cx6.c,v 1.19 2021/03/07 06:21:38 jsg Exp $	*/
/*	$NetBSD: smc93cx6.c,v 1.10 2003/05/02 19:12:19 dyoung Exp $	*/

/*
 * Interface for the 93C66/56/46/26/06 serial eeprom parts.
 *
 * Copyright (c) 1995, 1996 Daniel M. Eischen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    Daniel M. Eischen.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 *
 * $FreeBSD: src/sys/dev/aic7xxx/93cx6.c,v 1.5 2000/01/07 23:08:17 gibbs Exp $
 */

/*
 *   The instruction set of the 93C66/56/46/26/06 chips are as follows:
 *
 *               Start  OP	    *
 *     Function   Bit  Code  Address**  Data     Description
 *     -------------------------------------------------------------------
 *     READ        1    10   A5 - A0             Reads data stored in memory,
 *                                               starting at specified address
 *     EWEN        1    00   11XXXX              Write enable must precede
 *                                               all programming modes
 *     ERASE       1    11   A5 - A0             Erase register A5A4A3A2A1A0
 *     WRITE       1    01   A5 - A0   D15 - D0  Writes register
 *     ERAL        1    00   10XXXX              Erase all registers
 *     WRAL        1    00   01XXXX    D15 - D0  Writes to all registers
 *     EWDS        1    00   00XXXX              Disables all programming
 *                                               instructions
 *     *Note: A value of X for address is a don't care condition.
 *    **Note: There are 8 address bits for the 93C56/66 chips unlike
 *	      the 93C46/26/06 chips which have 6 address bits.
 *
 *   The 93C46 has a four wire interface: clock, chip select, data in, and
 *   data out.  In order to perform one of the above functions, you need
 *   to enable the chip select for a clock period (typically a minimum of
 *   1 usec, with the clock high and low a minimum of 750 and 250 nsec
 *   respectively).  While the chip select remains high, you can clock in
 *   the instructions (above) starting with the start bit, followed by the
 *   OP code, Address, and Data (if needed).  For the READ instruction, the
 *   requested 16-bit register contents is read from the data out line but
 *   is preceded by an initial zero (leading 0, followed by 16-bits, MSB
 *   first).  The clock cycling from low to high initiates the next data
 *   bit to be sent from the chip.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <machine/bus.h>
#include <dev/ic/smc93cx6var.h>

/*
 * Right now, we only have to read the SEEPROM.  But we make it easier to
 * add other 93Cx6 functions.
 */
static struct seeprom_cmd {
  	unsigned char len;
 	unsigned char bits[3];
} seeprom_read = {3, {1, 1, 0}};

#define CLOCK_PULSE(sd, rdy)	do {					\
	/*								\
	 * Wait for the SEERDY to go high; about 800 ns.		\
	 */								\
	int cpi = 1000;							\
	if (rdy == 0) {							\
		DELAY(4); /* more than long enough */			\
		break;							\
	}								\
	while ((SEEPROM_STATUS_INB(sd) & rdy) == 0 && cpi-- > 0) {	\
		;  /* Do nothing */					\
	}								\
	(void)SEEPROM_INB(sd);	/* Clear clock */			\
} while (0)

/*
 * Read the serial EEPROM and returns 1 if successful and 0 if
 * not successful.
 */
int
read_seeprom(struct seeprom_descriptor *sd, u_int16_t *buf,
    bus_size_t start_addr, bus_size_t count)
{
	int i = 0;
	u_int k = 0;
	u_int16_t v;
	u_int32_t temp;

	/*
	 * Read the requested registers of the seeprom.  The loop
	 * will range from 0 to count-1.
	 */
	for (k = start_addr; k < count + start_addr; k++) {
		/* Send chip select for one clock cycle. */
		temp = sd->sd_MS ^ sd->sd_CS;
		SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
		CLOCK_PULSE(sd, sd->sd_RDY);

		/*
		 * Now we're ready to send the read command followed by the
		 * address of the 16-bit register we want to read.
		 */
		for (i = 0; i < seeprom_read.len; i++) {
			if (seeprom_read.bits[i] != 0)
				temp ^= sd->sd_DO;
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
			if (seeprom_read.bits[i] != 0)
				temp ^= sd->sd_DO;
		}
		/* Send the 6 or 8 bit address (MSB first, LSB last). */
		for (i = (sd->sd_chip - 1); i >= 0; i--) {
			if ((k & (1 << i)) != 0)
				temp ^= sd->sd_DO;
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
			if ((k & (1 << i)) != 0)
				temp ^= sd->sd_DO;
		}

		/*
		 * Now read the 16 bit register.  An initial 0 precedes the
		 * register contents which begins with bit 15 (MSB) and ends
		 * with bit 0 (LSB).  The initial 0 will be shifted off the
		 * top of our word as we let the loop run from 0 to 16.
		 */
		v = 0;
		for (i = 16; i >= 0; i--) {
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			v <<= 1;
			if (SEEPROM_DATA_INB(sd) & sd->sd_DI)
				v |= 1;
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
		}

		buf[k - start_addr] = v;

		/* Reset the chip select for the next command cycle. */
		temp = sd->sd_MS;
		SEEPROM_OUTB(sd, temp);
		CLOCK_PULSE(sd, sd->sd_RDY);
		SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
		CLOCK_PULSE(sd, sd->sd_RDY);
		SEEPROM_OUTB(sd, temp);
		CLOCK_PULSE(sd, sd->sd_RDY);
	}
#ifdef AHC_DUMP_EEPROM
	printf("\nSerial EEPROM:\n\t");
	for (k = 0; k < count; k = k + 1) {
		if (((k % 8) == 0) && (k != 0)) {
			printf ("\n\t");
		}
		printf (" 0x%04x", buf[k]);
	}
	printf ("\n");
#endif
	return (1);
}
