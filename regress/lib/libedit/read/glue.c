/*	$OpenBSD: glue.c,v 1.4 2017/07/05 15:31:45 bluhm Exp $	*/
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Glue for unit tests of libedit/read.c.
 * Rather than linking in all the various libedit modules,
 * provide dummies for those functions called in read.c.
 */

#define EL EditLine *el __attribute__((__unused__))
#define UU __attribute__((__unused__))

int ch_enlargebufs(EL, size_t addlen UU) { return 1; }
void ch_reset(EL) { }
void el_resize(EL) { }
int el_set(EL, int op UU, ...) { return 0; }
int el_wset(EL, int op UU, ...) { return 0; }
void re_clear_display(EL) { }
void re_clear_lines(EL) { }
void re_refresh(EL) { }
void re_refresh_cursor(EL) { }
void sig_clr(EL) { }
void sig_set(EL) { }
void terminal__flush(EL) { }
void terminal_beep(EL) { }
int tty_cookedmode(EL) { return 0; }
int tty_rawmode(EL) { return 0; }

int
keymacro_get(EL, wchar_t *ch, keymacro_value_t *val)
{
	static wchar_t value[] = L"ic";

	switch (*ch) {
	case L'c':
		val->cmd = ED_COMMAND;
		return XK_CMD;
	case L's':
		val->str = value;
		return XK_STR;
	default:
		val->str = NULL;
		*ch = '\0';
		return XK_STR;
	}
}

#undef EL
#undef UU
