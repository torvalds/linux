/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Ed Schouten <ed@FreeBSD.org>
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

static void
teken_subr_cons25_set_border(const teken_t *t, unsigned int c)
{

	teken_funcs_param(t, TP_SETBORDER, c);
}

static void
teken_subr_cons25_set_global_cursor_shape(const teken_t *t, unsigned int ncmds,
    const unsigned int cmds[])
{
	unsigned int code, i;

	/*
	 * Pack the args to work around API deficiencies.  This requires
	 * knowing too much about the low level to be fully compatible.
	 * Returning when ncmds > 3 is necessary and happens to be
	 * compatible.  Discarding high bits is necessary and happens to
	 * be incompatible only for invalid args when ncmds == 3.
	 */
	if (ncmds > 3)
		return;
	code = 0;
	for (i = ncmds; i > 0; i--)
		code = (code << 8) | (cmds[i - 1] & 0xff);
	code = (code << 8) | ncmds;
	teken_funcs_param(t, TP_SETGLOBALCURSOR, code);
}

static void
teken_subr_cons25_set_local_cursor_type(const teken_t *t, unsigned int type)
{

	teken_funcs_param(t, TP_SETLOCALCURSOR, type);
}

static const teken_color_t cons25_colors[8] = { TC_BLACK, TC_BLUE,
    TC_GREEN, TC_CYAN, TC_RED, TC_MAGENTA, TC_BROWN, TC_WHITE };

static void
teken_subr_cons25_set_default_background(teken_t *t, unsigned int c)
{

	t->t_defattr.ta_bgcolor = cons25_colors[c % 8] | (c & 8);
	t->t_curattr.ta_bgcolor = cons25_colors[c % 8] | (c & 8);
}

static void
teken_subr_cons25_set_default_foreground(teken_t *t, unsigned int c)
{

	t->t_defattr.ta_fgcolor = cons25_colors[c % 8] | (c & 8);
	t->t_curattr.ta_fgcolor = cons25_colors[c % 8] | (c & 8);
}

static const teken_color_t cons25_revcolors[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

void
teken_get_defattr_cons25(const teken_t *t, int *fg, int *bg)
{

	*fg = cons25_revcolors[teken_256to8(t->t_defattr.ta_fgcolor)];
	if (t->t_defattr.ta_format & TF_BOLD)
		*fg += 8;
	*bg = cons25_revcolors[teken_256to8(t->t_defattr.ta_bgcolor)];
}

static void
teken_subr_cons25_switch_virtual_terminal(const teken_t *t, unsigned int vt)
{

	teken_funcs_param(t, TP_SWITCHVT, vt);
}

static void
teken_subr_cons25_set_bell_pitch_duration(const teken_t *t, unsigned int pitch,
    unsigned int duration)
{

	teken_funcs_param(t, TP_SETBELLPD, (pitch << 16) |
	    (duration & 0xffff));
}

static void
teken_subr_cons25_set_graphic_rendition(teken_t *t, unsigned int cmd,
    unsigned int param)
{

	(void)param;
	switch (cmd) {
	case 0: /* Reset. */
		t->t_curattr = t->t_defattr;
		break;
	default:
		teken_printf("unsupported attribute %u\n", cmd);
	}
}

static void
teken_subr_cons25_set_terminal_mode(teken_t *t, unsigned int mode)
{

	switch (mode) {
	case 0:	/* Switch terminal to xterm. */
		t->t_stateflags &= ~TS_CONS25;
		break;
	case 1: /* Switch terminal to cons25. */
		t->t_stateflags |= TS_CONS25;
		break;
	default:
		break;
	}
}

#if 0
static void
teken_subr_vt52_decid(teken_t *t)
{
	const char response[] = "\x1B/Z";

	teken_funcs_respond(t, response, sizeof response - 1);
}
#endif
