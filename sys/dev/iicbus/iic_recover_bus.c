/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
 * All rights reserved.
 *
 * Development sponsored by Microsemi, Inc.
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

/*
 * Helper code to recover a hung i2c bus by bit-banging a recovery sequence.
 *
 * An i2c bus can be hung by a slave driving the clock (rare) or data lines low.
 * The most common cause is a partially-completed transaction such as rebooting
 * while a slave is sending a byte of data.  Because i2c allows the clock to
 * freeze for any amount of time, the slave device will continue driving the
 * data line until power is removed, or the clock cycles enough times to
 * complete the current byte.  After completing any partial byte, a START/STOP
 * sequence resets the slave and the bus is recovered.
 *
 * Any i2c driver which is able to manually set the level of the clock and data
 * lines can use this common code for bus recovery.  On many SOCs that have
 * embedded i2c controllers, the i2c pins can be temporarily reassigned as gpio
 * pins to do the bus recovery, then can be assigned back to the i2c hardware.
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/iicbus/iic_recover_bus.h>
#include <dev/iicbus/iiconf.h>

int
iic_recover_bus(struct iicrb_pin_access *pins)
{
	const u_int timeout_us = 40000;
	const u_int delay_us = 500;
	int i;

	/*
	 * Start with clock and data high.
	 */
	pins->setsda(pins->ctx, 1);
	pins->setscl(pins->ctx, 1);

	/*
	 * At this point, SCL should be high.  If it's not, some slave on the
	 * bus is doing clock-stretching and we should wait a while.  If that
	 * slave is completely locked up there may be no way to recover at all.
	 * We wait up to 40 milliseconds, a seriously pessimistic time (even a
	 * cheap eeprom has a max post-write delay of only 10ms), and also long
	 * enough to allow SMB slaves to timeout normally after 35ms.
	 */
	for (i = 0; i < timeout_us; i += delay_us) {
		if (pins->getscl(pins->ctx))
			break;
		DELAY(delay_us);
	}
	if (i >= timeout_us)
		return (IIC_EBUSERR);

	/*
	 * At this point we should be able to control the clock line.  Some
	 * slave may be part way through a byte transfer, and could be holding
	 * the data line low waiting for more clock pulses to finish the byte.
	 * Cycle the clock until we see the data line go high, but only up to 9
	 * times because if it's not free after 9 clocks we're never going to
	 * win this battle.  We do 9 max because that's a byte plus an ack/nack
	 * bit, after which the slave must not be driving the data line anymore.
	 */
	for (i = 0; ; ++i) {
		if (pins->getsda(pins->ctx))
			break;
		if (i == 9)
			return (IIC_EBUSERR);
		pins->setscl(pins->ctx, 0);
		DELAY(5);
		pins->setscl(pins->ctx, 1);
		DELAY(5);
	}

	/*
	 * At this point we should be in control of both the clock and data
	 * lines, and both lines should be high.  To complete the reset of a
	 * slave that was part way through a transaction, we need to do a
	 * START/STOP sequence, which leaves both lines high at the end.
	 *  - START: SDA transitions high->low while SCL remains high.
	 *  - STOP:  SDA transitions low->high while SCL remains high.
	 * Note that even though the clock line remains high, we transition the
	 * data line no faster than it would change state with a 100khz clock.
	 */
	pins->setsda(pins->ctx, 0);
	DELAY(5);
	pins->setsda(pins->ctx, 1);
	DELAY(5);

	return (0);
}

