/*	$OpenBSD: tea5757.c,v 1.4 2021/11/23 00:17:59 jsg Exp $	*/

/*
 * Copyright (c) 2001 Vladimir Popov <jumbo@narod.ru>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Implementation of most common TEA5757 routines */

/*
 * Philips TEA5757H Self Tuned Radio
 *         http://www.semiconductors.philips.com/pip/TEA5757H
 *
 * The TEA5757; TEA5759 is a 44-pin integrated AM/FM stereo radio circuit.
 * The radio part is based on the TEA5712.
 *
 * The TEA5757 is used in FM-standards in which the local oscillator
 * frequency is above the radio frequency (e.g. European and American
 * standards). The TEA5759 is the version in which the oscillator frequency
 * is below the radio frequency (e.g. Japanese standard).
 *
 * The TEA5757; TEA5759 radio has a bus which consists of three wires:
 * BUS-CLOCK: software driven clock input
 * DATA: data input/output
 * WRITE-ENABLE: write/read input
 *
 * The TEA5757; TEA5759 has a 25-bit shift register.
 *
 * The chips are used in Radiotrack II, Guillemot Maxi Radio FM 2000,
 * Gemtek PCI cards and most Mediaforte FM tuners and sound cards with
 * integrated FM tuners.
 */

#include <sys/param.h>
#include <sys/radioio.h>

#include <dev/ic/tea5757.h>

/*
 * Convert frequency to hardware representation
 */
u_int32_t
tea5757_encode_freq(u_int32_t freq, int tea5759)
{
	if (tea5759)
		freq -= IF_FREQ;
	else
		freq += IF_FREQ;

	/*
	 * NO FLOATING POINT!
	 */
	freq *= 10;
	freq /= 125;

	return freq & TEA5757_FREQ;
}

/*
 * Convert frequency from hardware representation
 */
u_int32_t
tea5757_decode_freq(u_int32_t freq, int tea5759)
{
	freq &= TEA5757_FREQ;
	freq *= 125; /* 12.5 kHz */
	freq /= 10;

	if (tea5759)
		freq += IF_FREQ;
	else
		freq -= IF_FREQ;

	return freq;
}

/*
 * Hardware search
 */
void
tea5757_search(struct tea5757_t *tea, u_int32_t stereo, u_int32_t lock, int dir)
{
	u_int32_t reg;
	u_int co = 0;

	reg = stereo | lock | TEA5757_SEARCH_START;
	reg |= dir ? TEA5757_SEARCH_UP : TEA5757_SEARCH_DOWN;
	tea5757_hardware_write(tea, reg);

	DELAY(TEA5757_ACQUISITION_DELAY);

	do {
		DELAY(TEA5757_WAIT_DELAY);
		reg = tea->read(tea->iot, tea->ioh, tea->offset);
	} while ((reg & TEA5757_FREQ) == 0 && ++co < 200);
}

void
tea5757_hardware_write(struct tea5757_t *tea, u_int32_t data)
{
	int i = TEA5757_REGISTER_LENGTH;

	tea->init(tea->iot, tea->ioh, tea->offset, 0);

	while (i--)
		if (data & (1 << i))
			tea->write_bit(tea->iot, tea->ioh, tea->offset, 1);
		else
			tea->write_bit(tea->iot, tea->ioh, tea->offset, 0);

	tea->rset(tea->iot, tea->ioh, tea->offset, 0);
}

u_int32_t
tea5757_set_freq(struct tea5757_t *tea, u_int32_t stereo, u_int32_t lock, u_int32_t freq)
{
	u_int32_t data = 0ul;

	if (freq < MIN_FM_FREQ)
		freq = MIN_FM_FREQ;
	if (freq > MAX_FM_FREQ)
		freq = MAX_FM_FREQ;

	data |= tea5757_encode_freq(freq, tea->flags & TEA5757_TEA5759);
	data |= stereo | lock | TEA5757_SEARCH_END;
	tea5757_hardware_write(tea, data);

	return freq;
}

u_int32_t
tea5757_encode_lock(u_int8_t lock)
{
	u_int32_t ret;

	if (lock < 8)
		ret = TEA5757_S005;
	else if (lock > 7 && lock < 15)
		ret = TEA5757_S010;
	else if (lock > 14 && lock < 51)
		ret = TEA5757_S030;
	else
		ret = TEA5757_S150;

	return ret;
}

u_int8_t
tea5757_decode_lock(u_int32_t lock)
{
	u_int8_t ret = 150;

	switch (lock) {
	case TEA5757_S005:
		ret = 5;
		break;
	case TEA5757_S010:
		ret = 10;
		break;
	case TEA5757_S030:
		ret = 30;
		break;
	case TEA5757_S150:
		ret = 150;
		break;
	}

	return ret;
}
