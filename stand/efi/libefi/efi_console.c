/*-
 * Copyright (c) 2000 Doug Rabson
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"

static SIMPLE_TEXT_OUTPUT_INTERFACE	*conout;
static SIMPLE_INPUT_INTERFACE		*conin;

#ifdef TERM_EMU
#define	DEFAULT_FGCOLOR	EFI_LIGHTGRAY
#define	DEFAULT_BGCOLOR	EFI_BLACK

#define	MAXARGS	8
static int args[MAXARGS], argc;
static int fg_c, bg_c, curx, cury;
static int esc;

void get_pos(int *x, int *y);
void curs_move(int *_x, int *_y, int x, int y);
static void CL(int);
void HO(void);
void end_term(void);
#endif

#define	KEYBUFSZ 10
static unsigned keybuf[KEYBUFSZ];	/* keybuf for extended codes */
static int key_pending;

static void efi_cons_probe(struct console *);
static int efi_cons_init(int);
void efi_cons_putchar(int);
int efi_cons_getchar(void);
void efi_cons_efiputchar(int);
int efi_cons_poll(void);

struct console efi_console = {
	"efi",
	"EFI console",
	C_WIDEOUT,
	efi_cons_probe,
	efi_cons_init,
	efi_cons_putchar,
	efi_cons_getchar,
	efi_cons_poll
};

#ifdef TERM_EMU

/* Get cursor position. */
void
get_pos(int *x, int *y)
{
	*x = conout->Mode->CursorColumn;
	*y = conout->Mode->CursorRow;
}

/* Move cursor to x rows and y cols (0-based). */
void
curs_move(int *_x, int *_y, int x, int y)
{
	conout->SetCursorPosition(conout, x, y);
	if (_x != NULL)
		*_x = conout->Mode->CursorColumn;
	if (_y != NULL)
		*_y = conout->Mode->CursorRow;
}

/* Clear internal state of the terminal emulation code. */
void
end_term(void)
{
	esc = 0;
	argc = -1;
}

#endif

static void
efi_cons_probe(struct console *cp)
{
	conout = ST->ConOut;
	conin = ST->ConIn;
	cp->c_flags |= C_PRESENTIN | C_PRESENTOUT;
}

static int
efi_cons_init(int arg)
{
#ifdef TERM_EMU
	conout->SetAttribute(conout, EFI_TEXT_ATTR(DEFAULT_FGCOLOR,
	    DEFAULT_BGCOLOR));
	end_term();
	get_pos(&curx, &cury);
	curs_move(&curx, &cury, curx, cury);
	fg_c = DEFAULT_FGCOLOR;
	bg_c = DEFAULT_BGCOLOR;
#endif
	conout->EnableCursor(conout, TRUE);
	return 0;
}

static void
efi_cons_rawputchar(int c)
{
	int i;
	UINTN x, y;
	conout->QueryMode(conout, conout->Mode->Mode, &x, &y);

	if (c == '\t')
		/* XXX lame tab expansion */
		for (i = 0; i < 8; i++)
			efi_cons_rawputchar(' ');
	else {
#ifndef	TERM_EMU
		if (c == '\n')
			efi_cons_efiputchar('\r');
		efi_cons_efiputchar(c);
#else
		switch (c) {
		case '\r':
			curx = 0;
			efi_cons_efiputchar('\r');
			return;
		case '\n':
			efi_cons_efiputchar('\n');
			efi_cons_efiputchar('\r');
			cury++;
			if (cury >= y)
				cury--;
			curx = 0;
			return;
		case '\b':
			if (curx > 0) {
				efi_cons_efiputchar('\b');
				curx--;
			}
			return;
		default:
			efi_cons_efiputchar(c);
			curx++;
			if (curx > x-1) {
				curx = 0;
				cury++;
			}
			if (cury > y-1) {
				curx = 0;
				cury--;
			}
		}
#endif
	}
}

#ifdef TERM_EMU
/* Gracefully exit ESC-sequence processing in case of misunderstanding. */
static void
bail_out(int c)
{
	char buf[16], *ch;
	int i;

	if (esc) {
		efi_cons_rawputchar('\033');
		if (esc != '\033')
			efi_cons_rawputchar(esc);
		for (i = 0; i <= argc; ++i) {
			sprintf(buf, "%d", args[i]);
			ch = buf;
			while (*ch)
				efi_cons_rawputchar(*ch++);
		}
	}
	efi_cons_rawputchar(c);
	end_term();
}

/* Clear display from current position to end of screen. */
static void
CD(void) {
	int i;
	UINTN x, y;

	get_pos(&curx, &cury);
	if (curx == 0 && cury == 0) {
		conout->ClearScreen(conout);
		end_term();
		return;
	}

	conout->QueryMode(conout, conout->Mode->Mode, &x, &y);
	CL(0);  /* clear current line from cursor to end */
	for (i = cury + 1; i < y-1; i++) {
		curs_move(NULL, NULL, 0, i);
		CL(0);
	}
	curs_move(NULL, NULL, curx, cury);
	end_term();
}

/*
 * Absolute cursor move to args[0] rows and args[1] columns
 * (the coordinates are 1-based).
 */
static void
CM(void)
{
	if (args[0] > 0)
		args[0]--;
	if (args[1] > 0)
		args[1]--;
	curs_move(&curx, &cury, args[1], args[0]);
	end_term();
}

/* Home cursor (left top corner), also called from mode command. */
void
HO(void)
{
	argc = 1;
	args[0] = args[1] = 1;
	CM();
}

/* Clear line from current position to end of line */
static void
CL(int direction)
{
	int i, len;
	UINTN x, y;
	CHAR16 *line;

	conout->QueryMode(conout, conout->Mode->Mode, &x, &y);
	switch (direction) {
	case 0:         /* from cursor to end */
		len = x - curx + 1;
		break;
	case 1:         /* from beginning to cursor */
		len = curx;
		break;
	case 2:         /* entire line */
		len = x;
		break;
	default:	/* NOTREACHED */
		__unreachable();
	}

	if (cury == y - 1)
		len--;

	line = malloc(len * sizeof (CHAR16));
	if (line == NULL) {
		printf("out of memory\n");
		return;
	}
	for (i = 0; i < len; i++)
		line[i] = ' ';
	line[len-1] = 0;

	if (direction != 0)
		curs_move(NULL, NULL, 0, cury);

	conout->OutputString(conout, line);
	/* restore cursor position */
	curs_move(NULL, NULL, curx, cury);
	free(line);
	end_term();
}

static void
get_arg(int c)
{
	if (argc < 0)
		argc = 0;
	args[argc] *= 10;
	args[argc] += c - '0';
}

/* Emulate basic capabilities of cons25 terminal */
static void
efi_term_emu(int c)
{
	static int ansi_col[] = {
		0, 4, 2, 6, 1, 5, 3, 7
	};
	int t, i;

	switch (esc) {
	case 0:
		switch (c) {
		case '\033':
			esc = c;
			break;
		default:
			efi_cons_rawputchar(c);
			break;
		}
		break;
	case '\033':
		switch (c) {
		case '[':
			esc = c;
			args[0] = 0;
			argc = -1;
			break;
		default:
			bail_out(c);
			break;
		}
		break;
	case '[':
		switch (c) {
		case ';':
			if (argc < 0)
				argc = 0;
			else if (argc + 1 >= MAXARGS)
				bail_out(c);
			else
				args[++argc] = 0;
			break;
		case 'H':               /* ho = \E[H */
			if (argc < 0)
				HO();
			else if (argc == 1)
				CM();
			else
				bail_out(c);
			break;
		case 'J':               /* cd = \E[J */
			if (argc < 0)
				CD();
			else
				bail_out(c);
			break;
		case 'm':
			if (argc < 0) {
				fg_c = DEFAULT_FGCOLOR;
				bg_c = DEFAULT_BGCOLOR;
			}
			for (i = 0; i <= argc; ++i) {
				switch (args[i]) {
				case 0:         /* back to normal */
					fg_c = DEFAULT_FGCOLOR;
					bg_c = DEFAULT_BGCOLOR;
					break;
				case 1:         /* bold */
					fg_c |= 0x8;
					break;
				case 4:         /* underline */
				case 5:         /* blink */
					bg_c |= 0x8;
					break;
				case 7:         /* reverse */
					t = fg_c;
					fg_c = bg_c;
					bg_c = t;
					break;
				case 22:	/* normal intensity */
					fg_c &= ~0x8;
					break;
				case 24:	/* not underline */
				case 25:	/* not blinking */
					bg_c &= ~0x8;
					break;
				case 30: case 31: case 32: case 33:
				case 34: case 35: case 36: case 37:
					fg_c = ansi_col[args[i] - 30];
					break;
				case 39:        /* normal */
					fg_c = DEFAULT_FGCOLOR;
					break;
				case 40: case 41: case 42: case 43:
				case 44: case 45: case 46: case 47:
					bg_c = ansi_col[args[i] - 40];
					break;
				case 49:        /* normal */
					bg_c = DEFAULT_BGCOLOR;
					break;
				}
			}
			conout->SetAttribute(conout, EFI_TEXT_ATTR(fg_c, bg_c));
			end_term();
			break;
		default:
			if (isdigit(c))
				get_arg(c);
			else
				bail_out(c);
			break;
		}
		break;
	default:
		bail_out(c);
		break;
	}
}
#else
void
HO(void)
{
}
#endif

void
efi_cons_putchar(int c)
{
#ifdef TERM_EMU
	efi_term_emu(c);
#else
	efi_cons_rawputchar(c);
#endif
}

static int
keybuf_getchar(void)
{
	int i, c = 0;

	for (i = 0; i < KEYBUFSZ; i++) {
		if (keybuf[i] != 0) {
			c = keybuf[i];
			keybuf[i] = 0;
			break;
		}
	}

	return (c);
}

static bool
keybuf_ischar(void)
{
	int i;

	for (i = 0; i < KEYBUFSZ; i++) {
		if (keybuf[i] != 0)
			return (true);
	}
	return (false);
}

/*
 * We are not reading input before keybuf is empty, so we are safe
 * just to fill keybuf from the beginning.
 */
static void
keybuf_inschar(EFI_INPUT_KEY *key)
{

	switch (key->ScanCode) {
	case 0x1: /* UP */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'A';
		break;
	case 0x2: /* DOWN */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'B';
		break;
	case 0x3: /* RIGHT */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'C';
		break;
	case 0x4: /* LEFT */
		keybuf[0] = 0x1b;	/* esc */
		keybuf[1] = '[';
		keybuf[2] = 'D';
		break;
	case 0x17:
		keybuf[0] = 0x1b;	/* esc */
		break;
	default:
		keybuf[0] = key->UnicodeChar;
		break;
	}
}

static bool
efi_readkey(void)
{
	EFI_STATUS status;
	EFI_INPUT_KEY key;

	status = conin->ReadKeyStroke(conin, &key);
	if (status == EFI_SUCCESS) {
		keybuf_inschar(&key);
		return (true);
	}
	return (false);
}

int
efi_cons_getchar(void)
{
	int c;

	if ((c = keybuf_getchar()) != 0)
		return (c);

	key_pending = 0;

	if (efi_readkey())
		return (keybuf_getchar());

	return (-1);
}

int
efi_cons_poll(void)
{

	if (keybuf_ischar() || key_pending)
		return (1);

	/*
	 * Some EFI implementation (u-boot for example) do not support
	 * WaitForKey().
	 * CheckEvent() can clear the signaled state.
	 */
	if (conin->WaitForKey == NULL)
		key_pending = efi_readkey();
	else
		key_pending = BS->CheckEvent(conin->WaitForKey) == EFI_SUCCESS;

	return (key_pending);
}

/* Plain direct access to EFI OutputString(). */
void
efi_cons_efiputchar(int c)
{
	CHAR16 buf[2];

	/*
	 * translate box chars to unicode
	 */
	switch (c) {
	/* single frame */
	case 0xb3: buf[0] = BOXDRAW_VERTICAL; break;
	case 0xbf: buf[0] = BOXDRAW_DOWN_LEFT; break;
	case 0xc0: buf[0] = BOXDRAW_UP_RIGHT; break;
	case 0xc4: buf[0] = BOXDRAW_HORIZONTAL; break;
	case 0xda: buf[0] = BOXDRAW_DOWN_RIGHT; break;
	case 0xd9: buf[0] = BOXDRAW_UP_LEFT; break;

	/* double frame */
	case 0xba: buf[0] = BOXDRAW_DOUBLE_VERTICAL; break;
	case 0xbb: buf[0] = BOXDRAW_DOUBLE_DOWN_LEFT; break;
	case 0xbc: buf[0] = BOXDRAW_DOUBLE_UP_LEFT; break;
	case 0xc8: buf[0] = BOXDRAW_DOUBLE_UP_RIGHT; break;
	case 0xc9: buf[0] = BOXDRAW_DOUBLE_DOWN_RIGHT; break;
	case 0xcd: buf[0] = BOXDRAW_DOUBLE_HORIZONTAL; break;

	default:
		buf[0] = c;
	}
        buf[1] = 0;     /* terminate string */

	conout->OutputString(conout, buf);
}
