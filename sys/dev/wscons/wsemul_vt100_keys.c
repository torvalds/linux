/* $OpenBSD: wsemul_vt100_keys.c,v 1.9 2023/01/23 09:36:40 nicm Exp $ */
/* $NetBSD: wsemul_vt100_keys.c,v 1.3 1999/04/22 20:06:02 mycroft Exp $ */

/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/wscons/wsksymvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsemulvar.h>
#include <dev/wscons/wsemul_vt100var.h>

static const u_char *vt100_fkeys[] = {
	"\033[11~",	/* F1 */
	"\033[12~",
	"\033[13~",		/* F1-F5 normally don't send codes */
	"\033[14~",
	"\033[15~",	/* F5 */
	"\033[17~",	/* F6 */
	"\033[18~",
	"\033[19~",
	"\033[20~",
	"\033[21~",
	"\033[23~",	/* VT100: ESC */
	"\033[24~",	/* VT100: BS */
	"\033[25~",	/* VT100: LF */
	"\033[26~",
	"\033[28~",	/* help */
	"\033[29~",	/* do */
	"\033[31~",
	"\033[32~",
	"\033[33~",
	"\033[34~",	/* F20 */
	"\033[35~",
	"\033[36~",
	"\033[37~",
	"\033[38~"
};

static const u_char *vt100_pfkeys[] = {
	"\033OP",	/* PF1 */
	"\033OQ",
	"\033OR",
	"\033OS",	/* PF4 */
};

static const u_char *vt100_numpad[] = {
	"\033Op",	/* KP 0 */
	"\033Oq",	/* KP 1 */
	"\033Or",	/* KP 2 */
	"\033Os",	/* KP 3 */
	"\033Ot",	/* KP 4 */
	"\033Ou",	/* KP 5 */
	"\033Ov",	/* KP 6 */
	"\033Ow",	/* KP 7 */
	"\033Ox",	/* KP 8 */
	"\033Oy",	/* KP 9 */
};

int
wsemul_vt100_translate(void *cookie, kbd_t layout, keysym_t in,
    const u_char **out)
{
	struct wsemul_vt100_emuldata *edp = cookie;

	if (KS_GROUP(in) == KS_GROUP_Ascii) {
		*out = edp->translatebuf;
		return (wsemul_utf8_translate(KS_VALUE(in), layout,
		    edp->translatebuf, edp->flags & VTFL_UTF8));
	}

	if (in >= KS_f1 && in <= KS_f24) {
		*out = vt100_fkeys[in - KS_f1];
		return (5);
	}
	if (in >= KS_F1 && in <= KS_F24) {
		*out = vt100_fkeys[in - KS_F1];
		return (5);
	}
	if (in >= KS_KP_F1 && in <= KS_KP_F4) {
		*out = vt100_pfkeys[in - KS_KP_F1];
		return (3);
	}
	if (edp->flags & VTFL_APPLKEYPAD) {
		if (in >= KS_KP_0 && in <= KS_KP_9) {
			*out = vt100_numpad[in - KS_KP_0];
			return (3);
		}
		switch (in) {
		    case KS_KP_Tab:
			*out = "\033OI";
			return (3);
		    case KS_KP_Enter:
			*out = "\033OM";
			return (3);
		    case KS_KP_Multiply:
			*out = "\033Oj";
			return (3);
		    case KS_KP_Add:
			*out = "\033Ok";
			return (3);
		    case KS_KP_Separator:
			*out = "\033Ol";
			return (3);
		    case KS_KP_Subtract:
			*out = "\033Om";
			return (3);
		    case KS_KP_Decimal:
			*out = "\033On";
			return (3);
		    case KS_KP_Divide:
			*out = "\033Oo";
			return (3);
		}
	} else {
		if (!(in & 0x80)) {
			edp->translatebuf[0] = in & 0xff; /* turn into ASCII */
			*out = edp->translatebuf;
			return (1);
		}
	}
	switch (in) {
	    case KS_Help:
		*out = vt100_fkeys[15 - 1];
		return (5);
	    case KS_Execute: /* "Do" */
		*out = vt100_fkeys[16 - 1];
		return (5);
	    case KS_Find:
		*out = "\033[1~";
		return (4);
	    case KS_Insert:
	    case KS_KP_Insert:
		*out = "\033[2~";
		return (4);
	    case KS_KP_Delete:
		*out = "\033[3~";
		return (4);
	    case KS_Select:
		*out = "\033[4~";
		return (4);
	    case KS_Prior:
	    case KS_KP_Prior:
		*out = "\033[5~";
		return (4);
	    case KS_Next:
	    case KS_KP_Next:
		*out = "\033[6~";
		return (4);
	    case KS_Backtab:
		*out = "\033[Z";
		return (3);
	    case KS_Home:
	    case KS_KP_Home:
		*out = "\033[7~";
		return (4);
	    case KS_End:
	    case KS_KP_End:
		*out = "\033[8~";
		return (4);
	    case KS_Up:
	    case KS_KP_Up:
		if (edp->flags & VTFL_APPLCURSOR)
			*out = "\033OA";
		else
			*out = "\033[A";
		return (3);
	    case KS_Down:
	    case KS_KP_Down:
		if (edp->flags & VTFL_APPLCURSOR)
			*out = "\033OB";
		else
			*out = "\033[B";
		return (3);
	    case KS_Left:
	    case KS_KP_Left:
		if (edp->flags & VTFL_APPLCURSOR)
			*out = "\033OD";
		else
			*out = "\033[D";
		return (3);
	    case KS_Right:
	    case KS_KP_Right:
		if (edp->flags & VTFL_APPLCURSOR)
			*out = "\033OC";
		else
			*out = "\033[C";
		return (3);
	}
	return (0);
}
