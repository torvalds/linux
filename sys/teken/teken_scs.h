/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Ed Schouten <ed@FreeBSD.org>
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

static inline teken_char_t
teken_scs_process(const teken_t *t, teken_char_t c)
{

	return (t->t_scs[t->t_curscs](t, c));
}

/* Unicode points for VT100 box drawing. */
static const uint16_t teken_boxdrawing_unicode[31] = {
    0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
    0x2424, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7
};

/* ASCII points for VT100 box drawing. */
static const uint8_t teken_boxdrawing_8bit[31] = {
    '?', '?', 'H', 'F', 'C', 'L', '?', '?',
    'N', 'V', '+', '+', '+', '+', '+', '-',
    '-', '-', '-', '-', '+', '+', '+', '+',
    '|', '?', '?', '?', '?', '?', '?',
};

static teken_char_t
teken_scs_special_graphics(const teken_t *t, teken_char_t c)
{

	/* Box drawing. */
	if (c >= '`' && c <= '~')
		return (t->t_stateflags & TS_8BIT ?
		    teken_boxdrawing_8bit[c - '`'] :
		    teken_boxdrawing_unicode[c - '`']);
	return (c);
}

static teken_char_t
teken_scs_uk_national(const teken_t *t, teken_char_t c)
{

	/* Pound sign. */
	if (c == '#')
		return (t->t_stateflags & TS_8BIT ? 0x9c : 0xa3);
	return (c);
}

static teken_char_t
teken_scs_us_ascii(const teken_t *t, teken_char_t c)
{

	/* No processing. */
	(void)t;
	return (c);
}
